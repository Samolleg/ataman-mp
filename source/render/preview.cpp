#include "res_file.h"
#include "object_assembly.h"
#include "shader_locations.h"
#include "preview.h"
#include "gl_dispatch.h"
#include "view.h"
#include "node.h"
#include "camera.h"
#include "objects/unit.h"
#include "log.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "native_output.h"
#include "rebuild64_viewport_widget.h"

CPreview::CPreview(QWidget *parent, const QGLWidget* shareWidget):
    QGLWidget(parent, shareWidget)
  ,m_pNode(nullptr)
  ,m_pView(nullptr)
{
    m_nativeLabel=new QLabel(this);m_nativeLabel->setAlignment(Qt::AlignCenter);m_nativeLabel->setWordWrap(true);
    m_nativeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_nativeLabel->setStyleSheet("background:#292929;color:white");m_nativeLabel->hide();
    m_nativeTimer.setSingleShot(true);m_nativeTimer.setInterval(33);connect(&m_nativeTimer,&QTimer::timeout,this,&CPreview::requestNative);
    m_cam.reset(new CCamera(QVector3D(0.0, 0.5, 5.0), QVector3D(0.0, 0.0, 0.0), -70.0f, -45.0f));
}

void CPreview::attachNode(CNode *pNode, bool resetView)
{
    m_pNode = pNode;m_nativeSerial=0;m_nativePending=false;m_nativeFallback=false;m_nativeImage={};m_nativeRequestKey.clear();
    if(nativeSelected()){if(resetView){m_pan={};m_modelPitch=m_modelYaw=0;m_nativeZoom=1;m_nativeOrbit={};}m_nativeLabel->setText(tr("Rebuild64: подготовка просмотра…"));m_nativeLabel->show();m_nativeTimer.start();return;}
    if(resetView)
        this->resetView();
    else
        updateGL();
}

void CPreview::resetView(const QVector3D& cameraRotation,float modelPitch,float modelYaw)
{
    m_pan = QVector3D();
    m_modelYaw = modelYaw;
    m_modelPitch = modelPitch;
    m_resetCameraRotation=cameraRotation;
    if(nativeSelected()){m_nativeTimer.start();return;}
    if(m_pNode)
        refreshCam(m_pNode->getBBox());
}

void CPreview::attachSettings(CSettings *pSet)
{
    m_cam->attachSettings(pSet);
}


CPreview::~CPreview()
{
    //!!! MUST DESTROY PARENT OPENGL WIDGET FIRST
    //doneCurrent();
}

void CPreview::initializeGL()
{
    if(nativeSelected())return;
    makeCurrent();
    // Textures/geometry are shared with the viewport, shader state is not.
    // Map overlays leave customColor and environment uniforms behind.
    m_program.removeAllShaders();
    if(!m_program.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/vshader.vsh")
       || !m_program.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/fshader.fsh")
       || !ei::linkShaderProgram(m_program))
        ei::log(eLogWarning, "Preview shader failure: " + m_program.log());
    qglClearColor(Qt::darkGray);
    //ei::gl::Enable(GL_EXT_texture_filter_anisotropic);
    ei::gl::Enable(GL_DEPTH_TEST);
    //ei::gl::Enable(GL_CULL_FACE); //draw only front faces (disable if need draw both sides)

    QOpenGLContext* context = QOpenGLContext::currentContext();
    if(context && !context->isOpenGLES())
        ei::gl::Enable(GL_ALPHA_TEST);
    ei::gl::Enable(GL_BLEND);
    ei::gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //doneCurrent();
}

void CPreview::resizeGL(int width, int height)
{
    makeCurrent();
    m_height = height;
    m_width = width;
    m_nativeLabel->setGeometry(rect());if(nativeSelected()){m_nativeTimer.start();return;}
    const float ratio = float(width)/qMax(1,height);
    ei::gl::Viewport(0, 0, width, height);
    m_projection.setToIdentity();
    m_projection.perspective(45, ratio, 0.02f, 1000.0f);
}

void CPreview::showEvent(QShowEvent* event)
{
    if(nativeSelected()){m_nativeLabel->setGeometry(rect());m_nativeLabel->show();m_nativeTimer.start();}
    QGLWidget::showEvent(event);
}

void CPreview::paintGL()
{
    if(nativeSelected()){m_nativeLabel->setGeometry(rect());m_nativeLabel->show();if(nativeStateKey()!=m_nativeRequestKey&&!m_nativeTimer.isActive())m_nativeTimer.start();return;}
    m_nativeLabel->hide();
    if(!m_program.isLinked()){initializeGL();resizeGL(width(),height());}
    makeCurrent();
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
}



void CPreview::wheelEvent(QWheelEvent *event)
{
    if(nativeSelected()){m_nativeZoom=qBound(.1f,m_nativeZoom*(event->angleDelta().y()>0?1.15f:1/1.15f),10.f);m_nativeTimer.start();event->accept();return;}
    event->angleDelta().y() > 0 ? m_cam->strafeForward(false) : m_cam->strafeBack(false);
    updateGL();
    event->accept();
}

void CPreview::mousePressEvent(QMouseEvent *event)
{
    m_lastMouse = event->pos();
    m_dragButton = event->button();
    event->accept();
}

void CPreview::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint delta = event->pos() - m_lastMouse;
    m_lastMouse = event->pos();
    if(nativeSelected()) {
        if(m_dragButton==Qt::LeftButton)m_pan+=QVector3D(-2.f*delta.x()/qMax(1,width()),0,2.f*delta.y()/qMax(1,height()));
        else if(m_dragButton==Qt::MiddleButton){m_modelYaw+=delta.x()*.45f;m_modelPitch=qBound(-89.f,m_modelPitch+delta.y()*.45f,89.f);}
        else if(m_dragButton==Qt::RightButton)m_nativeOrbit+=QPointF(delta.x()*.5f,-delta.y()*.4f);
        m_nativeTimer.start();event->accept();return;
    }
    if(m_dragButton == Qt::LeftButton)
    {
        const float scale = m_pNode ? qMax(0.1f, m_pNode->getBBox().radius()) : 1.0f;
        m_pan += QVector3D(delta.x() * scale / qMax(1, width()), 0.0f,
                          -delta.y() * scale / qMax(1, height()));
    }
    else if(m_dragButton == Qt::MiddleButton)
    {
        m_modelYaw += delta.x() * 0.45f;
        m_modelPitch = qBound(-89.0f, m_modelPitch + delta.y() * 0.45f, 89.0f);
    }
    else if(m_dragButton == Qt::RightButton)
    {
        float xAngle = delta.y() * 0.4f;
        float zAngle = delta.x() * 0.5f;
        m_cam->xRotate(xAngle);
        m_cam->zRotate(zAngle);
    }
    updateGL();
    event->accept();
}

void CPreview::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragButton = Qt::NoButton;
    event->accept();
}

void CPreview::draw()
{
    makeCurrent();
    if(nullptr == m_pNode || nullptr == m_pView)
        return;


    QMatrix4x4 camMatrix = m_cam->update();
    camMatrix.translate(m_pan);
    camMatrix.rotate(m_modelPitch, 1.0f, 0.0f, 0.0f);
    camMatrix.rotate(m_modelYaw, 0.0f, 0.0f, 1.0f);

    // Bind shader pipeline for use
    QOpenGLShaderProgram* pProgram = &m_program;
    if(!pProgram->bind()) return;

    pProgram->setUniformValue(ei::uniformLocation(pProgram, "customColor"), QVector4D());
    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_instanced"), false);
    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_partAnimated"), false);
    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_highlight"), false);
    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_shadowCascadeDebug"), false);
    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_environmentEnabled"), false);

    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_projMmatrix"), m_projection);
    pProgram->setUniformValue(ei::uniformLocation(pProgram, "u_viewMmatrix"), camMatrix);

    if(CUnit* unit = dynamic_cast<CUnit*>(m_pNode.data()))
        unit->drawObjectVisual(true, pProgram);
    else
        m_pNode->draw(true, pProgram);
    pProgram->release();
}

void CPreview::refreshCam(CBox box)
{
    const float distance=qMax(0.25f,box.radius()*2.6f);
    const QVector3D pivot=box.center();
    m_cam->setLocation(pivot+QVector3D(0.0f,0.0f,distance),pivot,m_resetCameraRotation);
    updateGL();
}


QByteArray CPreview::nativeStateKey() const {
    if(!m_pNode)return {};
    return QJsonDocument(QJsonObject{{"node",m_pNode->toJson()},{"transient",m_pNode->independentPreviewSource()},{"width",width()},{"height",height()},{"pitch",m_modelPitch},{"yaw",m_modelYaw},{"orbitX",m_nativeOrbit.x()},{"orbitY",m_nativeOrbit.y()},{"zoom",m_nativeZoom},{"panX",m_pan.x()},{"panZ",m_pan.z()}}).toJson(QJsonDocument::Compact);
}
bool CPreview::nativeSelected() const {return m_pView&&m_pView->property("rebuild64Selected").toBool()&&!m_nativeFallback;}
void CPreview::attachView(CView* view) {
    m_pView=view;if(view&&view->settings())m_cam->attachSettings(view->settings());
    if(auto* viewport=nativeOutputViewport(view)) {
        if(m_nativeViewport==viewport)return;
        if(m_nativeViewport)disconnect(m_nativeViewport,nullptr,this,nullptr);m_nativeViewport=viewport;
        connect(viewport,&CRebuild64Viewport::outputFinished,this,[this](quint64 serial,const QImage& image,const QString& error,quint64,const QString&,int){
            if(serial!=m_nativeSerial)return;m_nativePending=false;
            if(nativeStateKey()!=m_nativeRequestKey){m_nativeImage={};m_nativeLabel->setText(tr("Rebuild64: подготовка просмотра…"));m_nativeTimer.start();return;}
            if(!error.isEmpty()) {
                m_nativeLabel->setText(error);qWarning("NATIVE_PREVIEW %s",error.toUtf8().constData());
                // Only this preview falls back. The displayed map, camera and
                // documents remain in their current renderer.
                if(m_pNode&&(dynamic_cast<CUnit*>(m_pNode.data())||m_pNode->modelName().compare("stst8",Qt::CaseInsensitive)==0)){if(m_pView)emit m_pView->updateMsg(error);return;}
                m_nativeFallback=true;if(m_pView)emit m_pView->updateMsg(tr("Просмотр Rebuild64 недоступен; для этого просмотра используется OpenGL. %1").arg(error));
                if(m_pNode)refreshCam(m_pNode->getBBox());m_nativeLabel->hide();updateGL();return;
            }
            if(nativeStateKey()!=m_nativeRequestKey){m_nativeImage={};m_nativeLabel->setText(tr("Rebuild64: подготовка просмотра…"));m_nativeTimer.start();return;}
            m_nativeImage=image;m_nativeLabel->setText({});m_nativeLabel->setPixmap(QPixmap::fromImage(image).scaled(size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        });
        connect(viewport,&CRebuild64Viewport::rendererSelectionChanged,this,[this]{m_nativeSerial=0;m_nativePending=false;m_nativeFallback=false;m_nativeTimer.start();updateGL();});
    }
}
void CPreview::requestNative() {
    if(!nativeSelected()||!isVisible())return;
    if(!m_pNode){m_nativeLabel->clear();return;}
    const auto transient=m_pNode->independentPreviewSource();

    if(m_nativePending){m_nativeTimer.start();return;}
    auto* viewport=nativeOutputViewport(m_pView);if(!viewport)return;
    if(m_nativeViewport!=viewport)attachView(m_pView.data());
    const auto object=ei::assemblyWorldObject(m_pNode->toJson());
    const auto c=m_pNode->complection();const QVector3D complection(c.y(),c.x(),c.z());
    const auto unit=dynamic_cast<CUnit*>(m_pNode.data());
    const auto resolved=unit?unit->nativeVisualPlan():QJsonObject{};
    const auto texture=unit?resolved["primaryTexture"].toString():transient.value("texture").toString(m_pNode->textureName());
    QStringList parts;for(const auto& part:(transient.contains("parts")?transient.value("parts"):object.value("Object parts")).toArray())parts.append(part.toString());
    // Built-in logic markers live in the mapper's own resource bundle. Pass
    // their exact bytes; the renderer must not search the game's archives for them.
    static const QMap<QString,QByteArray> auxiliary=[] {CResFile archive(QStringLiteral(":/auxData.res"));return archive.bufferOfFiles();}();
    const auto baseRotation=m_pNode->rotation();
    const auto model=unit?resolved["model"].toString():transient.value("model").toString(m_pNode->modelName());const auto modelBytes=auxiliary.value(model.toLower()+".mod");
    if(qEnvironmentVariableIsSet("ATAMAN_REBUILD64_LAUNCH_CASE"))setProperty("nativeOutputSource",QJsonObject{{"model",model},{"texture",texture},{"parts",QJsonArray::fromStringList(parts)},{"transient",transient}});
    if(qEnvironmentVariableIsSet("ATAMAN_REBUILD64_LAUNCH_CASE"))qInfo("NATIVE_PREVIEW_SOURCE model=%s texture=%s parts=%s transient=%s",qPrintable(model),qPrintable(texture),qPrintable(parts.join(",")),QJsonDocument(transient).toJson(QJsonDocument::Compact).constData());
    m_nativeRequestKey=nativeStateKey();m_nativeImage={};m_nativeLabel->setText(tr("Rebuild64: подготовка просмотра…"));
    m_nativePending=true;
    m_nativeSerial=viewport->requestOutput(size(),3,viewport->outputCamera(),model,texture,complection,
        {m_modelPitch,m_modelYaw,float(m_nativeOrbit.x()),float(m_nativeOrbit.y()),m_nativeZoom,m_pan.x(),m_pan.z(),baseRotation.scalar(),baseRotation.x(),baseRotation.y(),baseRotation.z()},{},(transient.isEmpty()?object.value("Secondary texture").toString():transient.value("secondary").toString(texture)),parts,
        modelBytes,auxiliary.value(model.toLower()+".bon"),auxiliary.value(texture.toLower()+".mmp"),viewport->appearanceVisuals(m_pNode));
}

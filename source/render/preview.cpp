#include "preview.h"
#include "gl_dispatch.h"
#include "view.h"
#include "node.h"
#include "camera.h"
#include "objects/unit.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QOpenGLContext>

CPreview::CPreview(QWidget *parent, const QGLWidget* shareWidget):
    QGLWidget(parent, shareWidget)
  ,m_pNode(nullptr)
  ,m_pView(nullptr)
{
    m_cam.reset(new CCamera(QVector3D(0.0, 0.5, 5.0), QVector3D(0.0, 0.0, 0.0), -70.0f, -45.0f));
}

void CPreview::attachNode(CNode *pNode, bool resetView)
{
    m_pNode = pNode;
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
    makeCurrent();
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
    const float ratio = float(width)/height;
    ei::gl::Viewport(0, 0, width, height);
    m_projection.setToIdentity();
    m_projection.perspective(45, ratio, 0.02f, 1000.0f);
}

void CPreview::paintGL()
{
    makeCurrent();
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
}



void CPreview::wheelEvent(QWheelEvent *event)
{
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
    QOpenGLShaderProgram* pProgram = &m_pView->shaderObject();
    if (nullptr != pProgram && !pProgram->bind())
        close();

    pProgram->setUniformValue("u_projMmatrix", m_projection);
    pProgram->setUniformValue("u_viewMmatrix", camMatrix);

    if(CUnit* unit = dynamic_cast<CUnit*>(m_pNode))
        unit->drawObjectVisual(true, pProgram);
    else
        m_pNode->draw(true, pProgram);

}

void CPreview::refreshCam(CBox box)
{
    const float distance=qMax(0.25f,box.radius()*2.6f);
    const QVector3D pivot=box.center();
    m_cam->setLocation(pivot+QVector3D(0.0f,0.0f,distance),pivot,m_resetCameraRotation);
    updateGL();
}


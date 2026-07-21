#include <QtWidgets>
#include <QDebug>
#include <QtMath>       //for fabs
#include <QMap>         //for parameters
#include <QAbstractScrollArea>
#include <QUndoStack>
#include <QTreeWidget>
#include <QPair>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QCryptographicHash>

#include "view.h"
#include "render_scheduling.h"
#include "camera.h"
#include "unit_animation.h"
#include <QDateTime>
#include "mob\mob.h"
#include "resourcemanager.h"
#include "node.h"
#include "landscape.h"
#include "key_manager.h"
#include "log.h"
#include "settings.h"
#include "undo.h"
#include "objects/object_base.h"
#include "objects/worldobj.h"
#include "objects/unit.h"
#include "objects/magictrap.h"
#include "mob\mob_parameters.h"
#include "layout_components/progressview.h"
#include "layout_components/tablemanager.h"
#include "operationmanager.h"
#include "ogl_utils.h"
#include "scene.h"
#include "round_mob_form.h"
#include "layout_components/tree_view.h"
#include "property.h"
#include "tile.h"
#include "view_frustum.h"
#include "render_performance.h"
#include "render_queue.h"
#include "part.h"
#include "external_script_workspace.h"
#include "mob_script_window.h"
#include "script_reference_analyzer.h"

class CLogic;

CView::CView(QWidget *parent, const QGLWidget *pShareWidget) : QGLWidget(parent, pShareWidget)
  ,m_gpuTimer(nullptr)
  ,m_gpuTimerPending(false)
  ,m_pSettings(nullptr)
  ,m_pUndoStack(nullptr)
  ,m_pProgress(nullptr)
  ,m_pendingOperationOwnsPaste(false)
  ,m_pendingLogicPlacementMacro(false)
  ,m_pendingPasteMob(nullptr)
  ,m_clipboard_buffer_file(QString("%1%2%3").arg(QDir::tempPath()).arg(QDir::separator()).arg("copy_paste_buffer.json"))
  ,m_recentOpenedFile_file(QString("%1%2%3").arg(QDir::tempPath()).arg(QDir::separator()).arg("recent_opened.json"))
  ,m_activeMob(nullptr)
  ,m_pTree(nullptr)
  ,m_pRoundForm(nullptr)
  ,m_bDrawLand(true)
  ,m_bDrawWater(true)
  ,m_bPreviewTile(false)
  ,m_tileBrushCommandId(0)
  ,m_hasViewportMousePos(false)
  ,m_testSpawnMarkerValid(false)
  ,m_testSpawnMarkerAdjusted(false)
  ,m_spatialIndexDirty(true)
  ,m_sceneFingerprintDirty(true)
{
    setFocusPolicy(Qt::ClickFocus);

    m_cam.reset(new CCamera);
    m_timer = new QTimer;
    m_aReadState.resize(eReadCount);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(updateWindow()));
    m_mprModifyTimer = new QTimer;
    connect(m_mprModifyTimer, SIGNAL(timeout()), this, SLOT(checkNewLandVersion()));
    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    m_selectFrame.reset(new CSelectFrame);
    //qDebug() << format();
    //qDebug() << isSharing();
    QApplication* application = static_cast<QApplication *>(QApplication::instance());
    application->inputMethod()->reset();
    QLocale a = application->inputMethod()->locale();
    QString lang = a.languageToString(a.language());
    a.setDefault(QLocale::English);
    m_pLand = CLandscape::getInstance();
    m_scriptWorkspaces.reset(new CExternalScriptWorkspaceManager(this, this));
    connect(m_scriptWorkspaces.get(), &CExternalScriptWorkspaceManager::scriptImportAccepted,
            this, &CView::importExternalMobScript);
}

void CView::updateReadState(EReadState state)
{
    m_aReadState[state] = true;
    qDebug() << state << " read";
}

CView::~CView()
{
//    for(auto& param : m_arrParamWindow)
//    {
//        param->close();
//        delete param;
//    }

    // QOpenGLBuffer/QOpenGLShaderProgram/QOpenGLTimerQuery destruction requires
    // this widget's context. MainWindow deletes CView explicitly, and without
    // reacquiring it Qt can reach QOpenGLContext::surface() through a null
    // currentContext after the window has already disappeared.
    if(m_timer)
        m_timer->stop();
    if(m_mprModifyTimer)
        m_mprModifyTimer->stop();
    ei::setCrashContext("shutdown/view-opengl-resources");
    if(isValid())
        makeCurrent();
    ei::setCrashContext("shutdown/view-gpu-timer");
    if(m_gpuTimer)
    {
        QOpenGLContext* current = QOpenGLContext::currentContext();
        if(current && current->surface())
        {
            m_gpuTimer->destroy();
            delete m_gpuTimer;
        }
        else
        {
            // Some window-system shutdown paths destroy the QGLWidget surface
            // before MainWindow explicitly deletes the view. Qt's timer-query
            // destructor dereferences the absent current surface in that state.
            // Detach this one process-lifetime diagnostic object rather than
            // crashing while the application is already terminating.
            ei::log(eLogWarning,
                    "GPU timer query retained at shutdown because its OpenGL surface is unavailable");
            m_gpuTimer->setParent(nullptr);
        }
        m_gpuTimer = nullptr;
        m_gpuTimerPending = false;
    }
    RenderQueueContext::instance().releaseGpuResources();
    ei::setCrashContext("shutdown/view-mobs");

    for(auto& mob: m_aMob)
    {
        m_scriptWorkspaces->unregisterMob(mob);
        delete mob;
    }
    if(isValid() && !m_testEntranceLabelTextures.isEmpty())
    {
        makeCurrent();
        glDeleteTextures(m_testEntranceLabelTextures.size(),
                         m_testEntranceLabelTextures.constData());
        m_testEntranceLabelTextures.clear();
        // Keep the context current for renderer member destruction below.
    }
    m_aMob.clear();

    delete m_timer;
    m_timer = nullptr;
    delete m_mprModifyTimer;
    m_mprModifyTimer = nullptr;
    // Deliberately leave the context current: shader-program members are
    // destroyed after this destructor body and before QGLWidget tears down the
    // context itself.
    ei::setCrashContext("shutdown/view-members");
}


void CView::attach(CSettings* pSettings, QTableWidget* pParam, QUndoStack* pStack, CProgressView* pProgress, QLineEdit* pMouseCoord, CTreeView *pTree, CTileForm* pTileForm) //todo: use signal\slots
{
    m_pSettings = pSettings;
    connect(m_pSettings, &CSettings::renderSettingsChanged,
            this, &CView::applyRenderScheduling);
    m_cam->attachSettings(pSettings);
    m_cam->reset();
    m_tableManager.reset(new CTableManager(pParam));
    QObject::connect(m_tableManager.get(), SIGNAL(onUpdateProperty(const QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(const QSharedPointer<IPropertyBase>&)));
    QObject::connect(m_tableManager.get(), SIGNAL(onUpdateProperties(const QList<QSharedPointer<IPropertyBase>>&)),
                     this, SLOT(onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>&)));
    m_pUndoStack = pStack;
    connect(m_pUndoStack, &QUndoStack::indexChanged, this, [this](int)
    {
        refreshMobDirtyStates();
        m_spatialIndexDirty = true;
    });
    connect(this, &CView::selectionChanged, this, [this]()
    {
        m_spatialIndexDirty = true;
    });

    m_pProgress = pProgress;
    m_pOp.reset(new COperation(new CSelect(this)));
    m_pOp->attachCam(m_cam.get());
    m_cam->attachKeyManager(m_pOp->keyManager());
    m_pOp->attachMouseCoordFiled(pMouseCoord);
    CLogger::getInstance()->attachSettings(pSettings);

    m_pTree = pTree;
    m_pTree->attachView(this);
    m_pTileForm = pTileForm;

    // MOB-only workspaces need the same responsive viewport as terrain-backed
    // workspaces. Previously this timer was started only by loadLandscape().
    // Swap interval controls normal display pacing. A zero-interval precise
    // timer removes the former artificial ~67 FPS ceiling and lets benchmark
    // mode measure uncapped throughput.
    m_timer->setTimerType(Qt::PreciseTimer);
    applyRenderScheduling();
    m_timer->start();
}

void CView::applyRenderScheduling()
{
    const COptInt* capOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderFpsCap")) : nullptr;
    const bool benchmarkMode = qApp->property("renderBenchmark").toBool();
    const int configuredCap = capOption ? qBound(0, capOption->value(), 1000) : 0;
    const int effectiveCap = benchmarkMode ? 0 : configuredCap;
    m_timer->setInterval(ei::renderTimerIntervalMs(effectiveCap));
    ei::log(eLogInfo, QString("Render scheduler fps-cap:%1 timer-ms:%2 benchmark-override:%3")
            .arg(effectiveCap == 0 ? QString("unlimited") : QString::number(effectiveCap))
            .arg(m_timer->interval())
            .arg(benchmarkMode ? "yes" : "no"));
}

void CView::refreshAfterResourceSetup()
{
    CObjectList::getInstance()->refreshConfiguredSources();
    CTextureList::getInstance()->refreshConfiguredSources();
    m_spatialIndexDirty = true;
    updateGeometry();
    if(width() > 0 && height() > 0)
        resizeGL(width(), height());
    updateGL();
    ei::log(eLogInfo, "Viewport and resource lists refreshed after game/mod setup");
}

void CView::updateWindow()
{
    updateGL();
}

void CView::rebuildSpatialIndices()
{
    m_spatialIndices.clear();
    m_spatialOverflowNodes.clear();
    for(CMob* mob : m_aMob)
    {
        QSharedPointer<CSpatialGrid<CNode*>> grid(new CSpatialGrid<CNode*>(32.0f));
        QVector<CNode*> overflow;
        for(CNode* node : mob->nodes())
        {
            if(!node || node->isMarkDeleted() || node->nodeState() == ENodeState::eHidden)
                continue;
            const bool selected = node->nodeState() == ENodeState::eSelect;
            CBox bounds = node->getBBox();
            const QVector3D center = bounds.center() + node->drawPosition();
            const float radius = bounds.radius();
            const bool invalid = !bounds.isInit() || !qIsFinite(center.x())
                || !qIsFinite(center.y()) || !qIsFinite(center.z()) || !qIsFinite(radius);
            bool oversized = false;
            if(!invalid)
            {
                const int minX = qFloor((center.x() - radius) / 32.0f);
                const int maxX = qFloor((center.x() + radius) / 32.0f);
                const int minY = qFloor((center.y() - radius) / 32.0f);
                const int maxY = qFloor((center.y() + radius) / 32.0f);
                oversized = qint64(maxX - minX + 1) * qint64(maxY - minY + 1) > 64;
            }
            if(invalid || oversized)
                overflow.append(node);
            if(selected || invalid)
                grid->insert(node, node->drawPosition(), 0.0f, true);
            else
                grid->insert(node, center, radius);
        }
        m_spatialIndices.insert(mob, grid);
        m_spatialOverflowNodes.insert(mob, overflow);
        if(!overflow.isEmpty())
            ei::log(eLogWarning, QString("Spatial grid retained %1 diagnostic overflow nodes for %2")
                    .arg(overflow.size()).arg(mob->mobName()));
    }
    m_spatialIndexDirty = false;
}

QVector<CNode*> CView::spatiallyVisibleNodes(CMob* mob, const CViewFrustum* frustum,
                                             SSpatialQueryStats* stats)
{
    if(m_spatialIndexDirty)
        rebuildSpatialIndices();
    const QSharedPointer<CSpatialGrid<CNode*>> grid = m_spatialIndices.value(mob);
    if(!grid)
    {
        QVector<CNode*> nodes;
        if(mob)
            for(CNode* node : mob->nodes())
                nodes.append(node);
        return nodes;
    }
    return grid->query(frustum, stats);
}

QString CView::sceneSourceFingerprint()
{
    if(!m_sceneFingerprintDirty)
        return m_sceneFingerprint;
    QCryptographicHash sceneHash(QCryptographicHash::Sha256);
    const auto addFile = [&sceneHash](const QFileInfo& info)
    {
        sceneHash.addData(info.fileName().toLower().toUtf8());
        QFile file(info.absoluteFilePath());
        QCryptographicHash fileHash(QCryptographicHash::Sha256);
        if(file.open(QIODevice::ReadOnly))
            while(!file.atEnd())
                fileHash.addData(file.read(1024 * 1024));
        sceneHash.addData(fileHash.result());
    };
    if(m_pLand && m_pLand->isMprLoad())
        addFile(m_pLand->filePath());
    for(CMob* mob : m_aMob)
        addFile(mob->filePath());
    m_sceneFingerprint = QString::fromLatin1(sceneHash.result().toHex());
    m_sceneFingerprintDirty = false;
    return m_sceneFingerprint;
}

void CView::initShaders()
{
    // Compile vertex shader
    if (!m_program.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/vshader.vsh"))
        close();

    // Compile fragment shader
    if (!m_program.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/fshader.fsh"))
        close();

    // Link shader pipeline
    if (!m_program.link())
        close();

    m_program.bind();
    m_program.setUniformValue("customColor", QVector4D(0.0, 0.0, 0.0, 0.0));
    m_program.setUniformValue("u_instanced", false);
    //release shader program?

    // Compile land shader
    if (!m_landProgram.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/vshader.vsh"))
        close();
    if (!m_landProgram.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/landshader.fsh"))  //use absolute path for recompile shader every app rise
        close();
    if (!m_landProgram.link())
        close();

    m_landProgram.bind();
    m_landProgram.setUniformValue("u_instanced", false);
    m_landProgram.setUniformValue("u_lightPosition", QVector4D(0.0, 0.0, 2.0, 1.0));
    m_landProgram.setUniformValue("u_lightPower", 5.0f);
    m_landProgram.setUniformValue("u_lightColor", QVector4D(1.0, 1.0, 1.0, 1.0));
    m_landProgram.setUniformValue("u_highlight", false);


    // Compile select shader
    if (!m_selectProgram.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/vshader.vsh"))
        close();
    if (!m_selectProgram.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/select.fsh"))
        close();
    if (!m_selectProgram.link())
        close();
    m_selectProgram.bind();
    m_selectProgram.setUniformValue("u_instanced", false);

}

void CView::initializeGL()
{
    ei::setCrashContext("opengl/initialize");
    makeCurrent();
    logOpenGlData();
    qglClearColor(Qt::black);
    //glEnable(GL_EXT_texture_filter_anisotropic);
    glEnable(GL_DEPTH_TEST);
    //glEnable(GL_CULL_FACE); //draw only front faces (disable if need draw both sides)
    glAlphaFunc(GL_GREATER, 0.1f);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    initShaders();
    checkOpenGlError();

    CTextureList::getInstance()->initResource();
    CObjectList::getInstance()->initResource();
    m_gpuTimer = new QOpenGLTimerQuery(this);
    if(!m_gpuTimer->create())
    {
        delete m_gpuTimer;
        m_gpuTimer = nullptr;
        ei::log(eLogWarning, "GPU timer queries are unavailable; render counters will report CPU submission only");
    }
    ei::setCrashContext("idle");
    //doneCurrent();
}

void CView::resizeGL(int width, int height)
{
    makeCurrent();
    m_height = height;
    m_width = width;
    //const int side = qMin(width, height);
    //glViewport((width - side) / 2, (height - side) / 2, side, side);
    const float ratio = float(width)/height;
    glViewport(0, 0, width, height);

    m_projection.setToIdentity();
    m_projection.perspective(45, ratio, 1, 1000);
    //glOrtho(-2, +2, -2, +2, -2.0, 2.0);   // draw objects in orthogaphic view
    //glFrustum (-2.0, 2.0, -2.0, 2.0, 1.5, 20.0); //convert project matrix to perpective view
    //doneCurrent();
}

void CView::paintGL()
{
    makeCurrent();
    glViewport(0, 0, width(), height());
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const COptBool* statsOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("renderPerformanceCounters")) : nullptr;
    const bool collectStats = (statsOption && statsOption->value())
        || qApp->property("renderBenchmark").toBool();
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    performance.beginFrame(collectStats);
    if(collectStats && m_gpuTimer && m_gpuTimerPending && m_gpuTimer->isResultAvailable())
    {
        performance.recordGpuTime(m_gpuTimer->waitForResult());
        m_gpuTimerPending = false;
    }
    const bool timeGpu = collectStats && m_gpuTimer && !m_gpuTimerPending;
    if(timeGpu)
        m_gpuTimer->begin();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
    if(timeGpu)
    {
        m_gpuTimer->end();
        m_gpuTimerPending = true;
    }
    performance.endFrame();
    drawTestOverlaysOpenGl();
    //doneCurrent();
}

bool CView::prepareForApplicationShutdown()
{
    if(m_timer)
        m_timer->stop();
    if(m_mprModifyTimer)
        m_mprModifyTimer->stop();
    if(!isValid())
        return false;
    makeCurrent();
    QOpenGLContext* current = QOpenGLContext::currentContext();
    return current && current->surface();
}

void CView::drawTestOverlaysOpenGl()
{
    if((m_testEntranceRectangles.isEmpty() && !m_testSpawnMarkerValid)
       || width() <= 0 || height() <= 0)
        return;

    // QPainter and QGLWidget do not compose reliably on every double-buffered
    // driver. In particular, painting the entrance labels on the QGLWidget's
    // back buffer caused alternating dark frames on current NVIDIA drivers.
    // Keep the entire per-frame overlay in the native OpenGL pass.
    if(!m_program.bind())
        return;

    // Mesh rendering may leave a VAO with normal/UV/instance attributes
    // enabled. The overlay below uses client-side arrays. If those stale
    // attributes remain enabled after their VBO is released, NVIDIA's driver
    // attempts to fetch offsets such as 0x0c as host pointers and can access-
    // violate in glDrawArrays. Always start the overlay on the default VAO and
    // with a clean attribute set.
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* functions = context ? context->extraFunctions() : nullptr;
    if(functions)
    {
        functions->glBindVertexArray(0);
        functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
        functions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        GLint attributeCount = 0;
        functions->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributeCount);
        for(GLint attribute = 0; attribute < attributeCount; ++attribute)
            functions->glDisableVertexAttribArray(GLuint(attribute));
    }
    QOpenGLBuffer::release(QOpenGLBuffer::VertexBuffer);
    QOpenGLBuffer::release(QOpenGLBuffer::IndexBuffer);
    const int position = m_program.attributeLocation("a_position");
    if(position < 0)
        return;

    m_program.enableAttributeArray(position);
    QMatrix4x4 identity;
    identity.setToIdentity();
    m_program.setUniformValue("u_modelMmatrix", identity);
    m_program.setUniformValue("u_viewMmatrix", m_cam->viewMatrix());
    m_program.setUniformValue("u_projMmatrix", m_projection);
    m_program.setUniformValue("u_highlight", false);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    for(int index = 0; index < m_testEntranceRectangles.size(); ++index)
    {
        const QRectF area = m_testEntranceRectangles.at(index).normalized();
        const QPointF corners[] = {
            area.topLeft(), area.topRight(), area.bottomRight(), area.bottomLeft()
        };
        QVector<QVector3D> vertices;
        vertices.reserve(4);
        for(const QPointF& corner : corners)
        {
            QVector3D world(corner.x(), corner.y(), 0.0f);
            if(!CLandscape::getInstance()->projectPt(world))
            {
                vertices.clear();
                break;
            }
            world.setZ(world.z() + 0.15f);
            vertices.append(world);
        }
        if(vertices.size() != 4)
            continue;

        const bool selected = index == m_selectedTestEntrance;
        m_program.setUniformValue("customColor",
            selected ? QVector4D(0.20f, 1.0f, 0.38f, 0.37f)
                     : QVector4D(0.92f, 0.70f, 0.12f, 0.20f));
        m_program.setAttributeArray(position, vertices.constData());
        glDrawArrays(GL_TRIANGLE_FAN, 0, vertices.size());

        m_program.setUniformValue("customColor",
            selected ? QVector4D(0.38f, 1.0f, 0.50f, 0.95f)
                     : QVector4D(0.92f, 0.75f, 0.18f, 0.80f));
        glLineWidth(selected ? 3.0f : 1.0f);
        glDrawArrays(GL_LINE_LOOP, 0, vertices.size());
    }

    if(m_testEntranceLabelTexturesDirty)
    {
        if(!m_testEntranceLabelTextures.isEmpty())
            glDeleteTextures(m_testEntranceLabelTextures.size(),
                             m_testEntranceLabelTextures.constData());
        m_testEntranceLabelTextures.clear();
        for(const QImage& image : m_testEntranceLabelImages)
            m_testEntranceLabelTextures.append(bindTexture(image, GL_TEXTURE_2D, GL_RGBA));
        m_testEntranceLabelTexturesDirty = false;
    }

    // Labels are painted once into off-screen images and then composited as
    // ordinary OpenGL textures. QPainter never touches QGLWidget's framebuffer.
    const int texture = m_program.attributeLocation("a_texture");
    if(texture >= 0 && !m_testEntranceLabelTextures.isEmpty())
    {
        m_program.enableAttributeArray(texture);
        m_program.setUniformValue("u_viewMmatrix", identity);
        m_program.setUniformValue("u_projMmatrix", identity);
        m_program.setUniformValue("customColor", QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
        m_program.setUniformValue("qt_Texture0", 0);
        glDisable(GL_DEPTH_TEST);
        const QRect viewport(0, 0, width(), height());
        const QVector<QVector2D> textureCoordinates = {
            QVector2D(0.0f, 0.0f), QVector2D(1.0f, 0.0f),
            QVector2D(1.0f, 1.0f), QVector2D(0.0f, 1.0f)
        };

        const int count = qMin(m_testEntranceRectangles.size(),
                               m_testEntranceLabelTextures.size());
        for(int index = 0; index < count; ++index)
        {
            const QRectF area = m_testEntranceRectangles.at(index).normalized();
            QVector3D center(area.center().x(), area.center().y(), 0.0f);
            if(!CLandscape::getInstance()->projectPt(center))
                continue;
            center.setZ(center.z() + 0.25f);
            const QVector3D projected = center.project(
                m_cam->viewMatrix(), m_projection, viewport);
            if(projected.z() < 0.0f || projected.z() > 1.0f)
                continue;

            const QImage& image = m_testEntranceLabelImages.at(index);
            const float centerX = (2.0f * projected.x() / float(width())) - 1.0f;
            const float centerY = (2.0f * projected.y() / float(height())) - 1.0f;
            const float halfWidth = float(image.width()) / float(width());
            const float halfHeight = float(image.height()) / float(height());
            const QVector<QVector3D> quad = {
                QVector3D(centerX - halfWidth, centerY - halfHeight, 0.0f),
                QVector3D(centerX + halfWidth, centerY - halfHeight, 0.0f),
                QVector3D(centerX + halfWidth, centerY + halfHeight, 0.0f),
                QVector3D(centerX - halfWidth, centerY + halfHeight, 0.0f)
            };
            glBindTexture(GL_TEXTURE_2D, m_testEntranceLabelTextures.at(index));
            m_program.setAttributeArray(position, quad.constData());
            m_program.setAttributeArray(texture, textureCoordinates.constData());
            glDrawArrays(GL_TRIANGLE_FAN, 0, quad.size());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        m_program.disableAttributeArray(texture);
    }

    if(m_testSpawnMarkerValid)
    {
        m_program.setUniformValue("u_viewMmatrix", m_cam->viewMatrix());
        m_program.setUniformValue("u_projMmatrix", m_projection);
        m_program.setUniformValue("customColor",
            m_testSpawnMarkerSelected ? QVector4D(0.92f, 1.0f, 0.32f, 1.0f)
            : m_testSpawnMarkerAdjusted ? QVector4D(1.0f, 0.67f, 0.18f, 0.82f)
                                        : QVector4D(0.31f, 1.0f, 0.47f, 0.72f));
        glEnable(GL_DEPTH_TEST);
        glLineWidth(3.0f);
        QVector<QVector3D> figure;
        const float z = m_testSpawnMarker.z() + 0.08f;
        const int segments = 24;
        for(int i = 0; i < segments; ++i)
        {
            const float angle = float(i) * 2.0f * float(M_PI) / float(segments);
            figure.append(QVector3D(m_testSpawnMarker.x() + qCos(angle) * 0.55f,
                                    m_testSpawnMarker.y() + qSin(angle) * 0.55f, z));
        }
        m_program.setAttributeArray(position, figure.constData());
        glDrawArrays(GL_LINE_LOOP, 0, figure.size());
        const QVector<QVector3D> body = {
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 1.72f),
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 0.72f),
            QVector3D(m_testSpawnMarker.x() - 0.42f, m_testSpawnMarker.y(), z + 1.25f),
            QVector3D(m_testSpawnMarker.x() + 0.42f, m_testSpawnMarker.y(), z + 1.25f),
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 0.72f),
            QVector3D(m_testSpawnMarker.x() - 0.34f, m_testSpawnMarker.y(), z),
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 0.72f),
            QVector3D(m_testSpawnMarker.x() + 0.34f, m_testSpawnMarker.y(), z)
        };
        m_program.setAttributeArray(position, body.constData());
        glDrawArrays(GL_LINES, 0, body.size());
        QVector<QVector3D> head;
        for(int i = 0; i < segments; ++i)
        {
            const float angle = float(i) * 2.0f * float(M_PI) / float(segments);
            head.append(QVector3D(m_testSpawnMarker.x() + qCos(angle) * 0.24f,
                                  m_testSpawnMarker.y(), z + 1.93f + qSin(angle) * 0.24f));
        }
        m_program.setAttributeArray(position, head.constData());
        glDrawArrays(GL_LINE_LOOP, 0, head.size());
    }

    m_program.disableAttributeArray(position);
    m_program.setUniformValue("customColor", QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void CView::draw()
{

    QMatrix4x4 camMatrix = m_cam->update();
    QVector3D cameraPosition, cameraPivot, cameraRotation;
    m_cam->getLocation(cameraPosition, cameraPivot, cameraRotation);
    const COptBool* animationOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("animateUnits")) : nullptr;
    const bool animationEnabled = !animationOption || animationOption->value();
    AnimationFrameContext::instance().beginFrame(cameraPosition,
        QDateTime::currentMSecsSinceEpoch(), animationEnabled);
    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const bool cullingEnabled = !cullingOption || cullingOption->value();
    const CViewFrustum frustum(m_projection * camMatrix);
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const COptBool* drawHelpOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawHelp")) : nullptr;
    const bool drawLogic = drawLogicOption && drawLogicOption->value();
    const bool drawHelp = drawHelpOption && drawHelpOption->value();
    const bool logicMode = CScene::getInstance()->getMode() == eEditModeLogic;
    const bool traverseHelpers = drawLogic || drawHelp || logicMode;

    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
    {
        const COptString* profileOption = m_pSettings
            ? dynamic_cast<const COptString*>(m_pSettings->opt("resourceProfile")) : nullptr;
        SRenderFrameMetadata metadata;
        metadata.viewportWidth = m_width;
        metadata.viewportHeight = m_height;
        metadata.cameraPosition = cameraPosition;
        metadata.cameraPivot = cameraPivot;
        metadata.cameraRotation = cameraRotation;
        metadata.profileId = profileOption ? profileOption->value() : QString();
        metadata.mapName = m_pLand && m_pLand->isMprLoad()
            ? m_pLand->filePath().baseName()
            : (m_activeMob ? m_activeMob->filePath().baseName() : QString());
        metadata.contentFingerprint = sceneSourceFingerprint();
        metadata.benchmarkMode = qApp->property("renderBenchmark").toBool();
        metadata.vsyncEnabled = metadata.benchmarkMode
            ? false : qApp->property("renderConfiguredVSync").toBool();
        const COptInt* fpsCapOption = m_pSettings
            ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderFpsCap")) : nullptr;
        metadata.frameRateCap = metadata.benchmarkMode
            ? 0 : (fpsCapOption ? qBound(0, fpsCapOption->value(), 1000) : 0);
        metadata.animationEnabled = animationEnabled;
        metadata.landEnabled = m_bDrawLand;
        metadata.waterEnabled = m_bDrawWater;
        metadata.logicEnabled = drawLogic;
        metadata.helpEnabled = drawHelp;
        metadata.frustumCullingEnabled = cullingEnabled;
        performance.setFrameMetadata(metadata);
    }

    const auto visibleNodes = [this, &frustum, cullingEnabled, &performance](CMob* mob)
    {
        SSpatialQueryStats stats;
        const QVector<CNode*> result = spatiallyVisibleNodes(
            mob, cullingEnabled ? &frustum : nullptr, &stats);
        if(performance.collecting())
        {
            performance.counters().nodesVisited += stats.entriesTested;
            const QSharedPointer<CSpatialGrid<CNode*>> grid = m_spatialIndices.value(mob);
            if(grid)
                performance.counters().nodesCulled += qMax(0, grid->entryCount() - result.size());
            performance.counters().spatialCellsTested += stats.cellsTested;
            performance.counters().spatialCellsVisible += stats.cellsVisible;
            performance.counters().spatialCandidates += stats.candidates;
            performance.counters().spatialOverflowEntries += m_spatialOverflowNodes.value(mob).size();
        }
        return result;
    };
    // Set modelview-projection matrix

    // Bind shader pipeline for use
    if (!m_landProgram.bind())
        close();

    // Set modelview-projection matrix
    //m_landProgram.setUniformValue("u_watcherPos", m_cam->pos());
    m_landProgram.setUniformValue("u_projMmatrix", m_projection);
    m_landProgram.setUniformValue("u_viewMmatrix", camMatrix);
//    m_landProgram.setUniformValue("u_color", QVector4D(0.0, 1.0, 0.0, 1.0));
//    m_landProgram.setUniformValue("u_lightPosition", QVector4D(0.0, 0.0, 2.0, 1.0));
//    m_landProgram.setUniformValue("u_lightPower", 5.0f);
//    m_landProgram.setUniformValue("u_lightColor", QVector4D(1.0, 1.0, 1.0, 1.0));
//    m_landProgram.setUniformValue("u_highlight", false);
    if (m_landscapeVisible && m_pLand && m_pLand->isMprLoad() && m_bPreviewTile) // draw preview tile on top
        drawTilePreview(&m_landProgram);

    if (m_landscapeVisible && m_pLand && m_pLand->isMprLoad() && m_bDrawLand)
        m_pLand->draw(&m_landProgram, cullingEnabled ? &frustum : nullptr);

    // Bind shader pipeline for use
    if (!m_program.bind())
        close();

    m_program.setUniformValue("u_projMmatrix", m_projection);
    m_program.setUniformValue("u_viewMmatrix", camMatrix);
    const QVector4D activeHighlight(0.05f, 0.78f, 0.93f, 0.0f);
    const QVector4D lockedHighlight(1.0f, 0.55f, 0.05f, 0.0f);
    m_program.setUniformValue("u_highlightColor", activeHighlight);
    m_program.setUniformValue("qt_Texture0", 0);
    RenderQueueContext& renderQueue = RenderQueueContext::instance();
    renderQueue.begin(&m_program, cameraPosition);

    if(m_activeMob != nullptr)
    {
        for(auto& mob: m_aMob) //TODO: draw inactive mobs with custom color|alfa or something
        {
            if(!isMobVisible(mob))
                continue;
            if(mob->mobName() == m_activeMob->mobName())
                continue; //skip drawing active mob

            const QVector<CNode*> candidates = visibleNodes(mob);
            for(CNode* node : candidates)
            {
                // Selected nodes from every visible MOB are rendered in the
                // final depth pass below, including locked MOBs.
                if(node->nodeState() == ENodeState::eSelect)
                    continue;
                node->drawObjectVisual(false, &m_program);
                if(performance.collecting())
                    ++performance.counters().nodesSubmitted;
                if(!traverseHelpers && node->nodeState() == ENodeState::eSelect)
                {
                    node->drawLogicVisual(false, &m_program);
                    if(performance.collecting())
                        ++performance.counters().logicSubmissions;
                }
            }
            if(traverseHelpers)
                for(CNode* node : mob->nodes())
                {
                    node->drawLogicVisual(false, &m_program);
                    if(performance.collecting())
                        ++performance.counters().logicSubmissions;
                }
        }

        // Draw the active MOB in two passes so a selected duplicate remains
        // visible when it occupies exactly the same depth as its source.
        const QVector<CNode*> activeCandidates = visibleNodes(m_activeMob);
        for(CNode* node : activeCandidates)
        {
            if(isMobVisible(m_activeMob) && node->nodeState() != ENodeState::eSelect)
            {
                node->drawObjectVisual(true, &m_program);
                if(performance.collecting())
                    ++performance.counters().nodesSubmitted;
            }
        }
        if(traverseHelpers)
            for(CNode* node : m_activeMob->nodes())
                if(node->nodeState() != ENodeState::eSelect)
                {
                    node->drawLogicVisual(true, &m_program);
                    if(performance.collecting())
                        ++performance.counters().logicSubmissions;
                }
        renderQueue.flush();
        glDepthFunc(GL_LEQUAL);
        for(CMob* mob : m_aMob)
            if(isMobVisible(mob))
                for(CNode* node : mob->nodes())
                    if(node->nodeState() == ENodeState::eSelect)
                    {
                        m_program.setUniformValue("u_highlightColor",
                            mob == m_activeMob ? activeHighlight : lockedHighlight);
                        node->drawObjectVisual(mob == m_activeMob, &m_program);
                        renderQueue.flush();
                        node->drawLogicVisual(mob == m_activeMob, &m_program);
                        if(performance.collecting())
                        {
                            ++performance.counters().nodesSubmitted;
                            ++performance.counters().logicSubmissions;
                        }
                    }
        m_program.setUniformValue("u_highlightColor", activeHighlight);
        renderQueue.flush();
        glDepthFunc(GL_LESS);
    }

    renderQueue.end();
    if(performance.collecting())
    {
        performance.counters().sharedMeshes = quint64(CPart::sharedMeshCount());
        performance.counters().gpuMemoryEstimateBytes = CPart::sharedMeshBytes();
        const SAnimationPerformanceCounters& animation = AnimationFrameContext::instance().counters();
        performance.counters().animationPoseEvaluations = animation.poseEvaluations;
        performance.counters().animationPoseCacheHits = animation.poseCacheHits;
        performance.counters().animationComponentsEvaluated = animation.componentsEvaluated;
    }

    if (m_landscapeVisible && m_pLand && m_pLand->isMprLoad() && m_bDrawWater)
    {
        //turn to landshader again
        if (!m_landProgram.bind())
            close();

        m_landProgram.setUniformValue("transparency", 0.3f);
        m_pLand->drawWater(&m_landProgram, cullingEnabled ? &frustum : nullptr);
        m_landProgram.setUniformValue("transparency", 0.0f);
    }


    //draw selection frame using m_program shader
    if (!m_program.bind())
        close();

    if (m_pLand && m_pLand->isMprLoad() && !m_aMob.isEmpty())
    {
        QMatrix4x4 mtrx;
        mtrx.setToIdentity();
        m_program.setUniformValue("u_projMmatrix", mtrx);
        m_program.setUniformValue("u_viewMmatrix", mtrx);
        m_selectFrame->draw(&m_program);
    }
}

void CView::loadLandscape(const QFileInfo& filePath)
{
    if(m_pLand->isMprLoad())
    {
        QMessageBox::warning(this, "Warning","Landscape already loaded. Please close before opening new zone (*mpr)");
        //LOG_FATAL("ahtung"); //test logging critial error
        return;
    }

    ei::setCrashContext("mpr/open/dispatch:" + filePath.fileName());
    m_pLand->readMap(filePath);
    if(!m_pLand->isMprLoad())
    {
        ei::log(eLogWarning, "Landscape failed to load: " + filePath.absoluteFilePath());
        return;
    }
    for(CMob* mob : m_aMob)
        mob->reprojectForCurrentTerrain();
    m_landscapeVisible = true;
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr, filePath.baseName());
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, m_pLand->isDirty() ? "*" : "");
    m_lastModifiedLand = filePath.lastModified();
    connect(m_pTileForm, SIGNAL(applyChangesSignal()), this, SLOT(onMapMaterialUpdate()));
    COptInt* pOpt = dynamic_cast<COptInt*>(settings()->opt("landCheckTime"));
    if (pOpt and pOpt->value() != 0)
    {
        m_mprModifyTimer->setInterval(pOpt->value());
        m_mprModifyTimer->start();
    }
    m_pPreviewTile.reset(new CPreviewTile());
    updateTileForm(true);
    emit loadedMapStateChanged();
}

void CView::unloadLand(bool promptForSave)
{
    if(!m_pLand->isMprLoad())
        return;

    if(promptForSave && m_pLand->isDirty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Unload MPR", m_pLand->filePath().baseName() + " has unsaved changes.\nDo you want to save changes?", QMessageBox::Save|QMessageBox::No|QMessageBox::Cancel);
        if(reply == QMessageBox::Save)
        {
            if(!saveLandscapeDocument(m_pLand->filePath()))
                return;
        }
        else if(reply == QMessageBox::Cancel)
            return;

    }

    m_mprModifyTimer->stop();
    m_pLand->unloadMpr();
    for(CMob* mob : m_aMob)
        mob->reprojectForCurrentTerrain();
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr, "");
    m_pPreviewTile.clear();
    ei::log(eLogInfo, "Landscape unloaded");
    m_landscapeVisible = true;
    emit loadedMapStateChanged();
}

void CView::saveLand()
{
    if(m_pLand->isMprLoad())
    {
        if(saveLandscapeDocument(m_pLand->filePath()))
            emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, "");
    }
}

void CView::saveLandAs()
{
    if(!m_pLand->isMprLoad())
        return;

    const QFileInfo fileName = QFileDialog::getSaveFileName(this, "Save " + m_pLand->filePath().fileName() + " as... ", "" , tr("Landscape (*.mpr);;)"));
    if(fileName.filePath().isEmpty())
        return;
    //QFileInfo fileName("c:\\konst\\Проклятые Земли (Дополнение)\\Mods\\ferneo_mod\\Maps\\zone1gTest.mpr");

    if(saveLandscapeDocument(fileName))
    {
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr, fileName.baseName());
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, "");
    }
}

bool CView::saveLandscapeDocument(const QFileInfo& destination)
{
    QProgressDialog progress(tr("Preparing safe MPR save..."), QString(), 0, 4, this);
    progress.setWindowTitle(tr("Saving MPR"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(250);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    const auto updateProgress = [&progress](const QString& label, int value, int maximum)
    {
        progress.setMaximum(maximum);
        progress.setLabelText(label);
        progress.setValue(value);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const bool saved = m_pLand->saveMapAs(destination, updateProgress);
    progress.setValue(progress.maximum());
    return saved;
}

int CView::select(const SSelect &selectParam, bool bAddToSelect)
{
    if(m_aMob.isEmpty())
        return 0;

    const QList<CNode*> selectionBefore = selectionSnapshot();

    CNode* pNode = nullptr;
    foreach (pNode, m_activeMob->nodes())
    {
        switch (selectParam.type) {
        case eSelectType_Id_range:
        {
            uint id_min = selectParam.param1.toUInt();
            uint id_max = selectParam.param2.toUInt();
            // set equal value if only one defined
            if(id_min!=0 && id_max==0)
                id_max = id_min;
            else if(id_max!=0 && id_min==0)
                id_min = id_max;

            if (id_max < id_min)
            {
                uint temp = id_min;
                id_min = id_max;
                id_max = temp;
            }
            if (pNode->mapId() >= id_min && pNode->mapId() <=id_max)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Map_name:
        {
            auto pObj = dynamic_cast<CObjectBase*>(pNode);
            if (!pObj)
                break;
            bool bAppropriate = selectParam.exactMatch ? pNode->mapName() == selectParam.param1 : pNode->mapName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Texture_name:
        {
            if (!(pNode->nodeType() & ENodeType::eWorldObject))
                break;
            bool bAppropriate = selectParam.exactMatch ? pNode->textureName() == selectParam.param1 : pNode->textureName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Model_name:
        {
            if (!(pNode->nodeType() & ENodeType::eWorldObject))
                break;

            bool bAppropriate = selectParam.exactMatch ? pNode->modelName() == selectParam.param1 : pNode->modelName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Position_circle:
        {
            //TODO. create 'brush' for selecti by circle
            break;
        }
        case eSelectType_Position_rectangle:
        {
            //TODO. create 'brush' for selecti by circle
            break;
        }
        case eSelectType_Diplomacy_group:
        {
            auto pObj = dynamic_cast<CWorldObj*>(pNode);
            if (!pObj)
            {//deselect if we are trying to object diplomacy group with selected particles(and others)
                if(!bAddToSelect && (pNode->nodeState() & eSelect))
                    pNode->setState(eDraw);
                break;
            }

            int group_min = selectParam.param1.toUInt();
            int group_max = selectParam.param2.toUInt();
            if (group_max < group_min)
            {
                uint temp = group_min;
                group_min = group_max;
                group_max = temp;
            }
            if (pObj->dipGroup() >= group_min && pObj->dipGroup() <= group_max )
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
            {
                pNode->setState(eDraw);
            }
            break;
        }
        case eSelectType_Database_name:
        {
            auto pObj = dynamic_cast<CUnit*>(pNode);
            if (!pObj)
            { //deselect if we are trying to find unit database name with selected world object(and others)
                if(!bAddToSelect && (pNode->nodeState() & eSelect))
                    pNode->setState(eDraw);
                break;
            }

            bool bAppropriate = selectParam.exactMatch ? pObj->databaseName() == selectParam.param1 : pObj->databaseName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
            {
                pNode->setState(eDraw);
            }
            break;
        }
        case eSelectType_Template:
        {
            auto pWo = dynamic_cast<CWorldObj*>(pNode);
            if(nullptr == pWo)
                break;

            QSharedPointer<IPropertyBase> templ;
            pWo->getParam(templ, eObjParam_PARENT_TEMPLATE);
            bool bAppropriate = selectParam.exactMatch ? templ->toString() == selectParam.param1 : templ->toString().toLower().contains(selectParam.param1.toLower());
            if(bAppropriate)
                pNode->setState(ENodeState::eSelect);
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
            {
                pNode->setState(eDraw);
            }

            break;
        }
        case eSelectType_all:
        {
            if(selectParam.param1.toLower()=="logic")
            {
                if(m_activeMob->logicNodes().contains(pNode))
                    pNode->setState(eSelect);
                else
                    pNode->setState(eDraw);
            }
            else
                pNode->setState(eSelect);
            break;
        }
        default:
            break;
        }
    }
    recordSelectionChange(selectionBefore);
    viewParameters();
    emit loadedMapStateChanged();
    return nSelectedNodes();
}

CMob *CView::mob(QString mobName)
{
    const QString wanted = QDir::cleanPath(mobName).toLower();
    for(CMob* pMob : m_aMob)
    {
        if(!pMob)
            continue;
        if(QDir::cleanPath(pMob->filePath().absoluteFilePath()).toLower() == wanted)
            return pMob;

        if(pMob->mobName().toLower() == mobName.toLower())
            return pMob;
    }
    return nullptr;
}

void CView::pickObject(QPoint mousePos, bool bAddToSelect)
{
    QRect rect(mousePos, mousePos);
    pickObject(rect, bAddToSelect);
    return;
}

QDebug operator<<(QDebug stream, const SColor& color)
{
    stream << '{' << color.rgb[0] << ',' << color.rgb[1] << ',' << color.rgb[2];
    if (color.hasAlpha)
        stream << ',' << color.rgb[3];
    stream << '}';
    return stream;
}

void CView::getColorFromRect(const QRect& rect, QVector<SColor>& aColor)
{
    //get colors from rectangle
    const int component = 3; //rgb
    //revert Oy for picking
    const QPoint topLeft (rect.left(), m_height - rect.bottom());
    const QPoint btmRight(rect.right(), m_height - rect.top());
    const QRect inversedRect(topLeft, btmRight);

    const int h = inversedRect.height() == 0 ? 1 : qAbs(inversedRect.height());
    const int w = inversedRect.width() == 0 ? 1 :qAbs(inversedRect.width());
    uint size = component * w * h;
    QByteArray* pixel = new QByteArray();
    pixel->resize(size);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); // glReadPixels use 4 component by default. for RGB must define pack alignment
    glReadPixels(inversedRect.left(), inversedRect.bottom(), w, h, GL_RGB, GL_UNSIGNED_BYTE, pixel->data());
    SColor color;
    for(int i(0); i<component * w * h; i+=3)
    {
        color = SColor(pixel->at(i), pixel->at(i+1), pixel->at(i+2));
        if (!color.isBlack() && !aColor.contains(color))
            aColor.append(SColor(color));
    }
    delete pixel;
}

void CView::changeCurrentMob(CMob *pMob)
{
    if(pMob != m_activeMob)
        cancelPendingOperation();
    m_activeMob = pMob;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataActiveMob, nullptr == pMob ? "" : pMob->mobName());
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, (nullptr == pMob || !pMob->isDirty()) ? "" : "*");
    collectObjectTreeData();
    viewParameters();
    emit loadedMapStateChanged();
}

void CView::changeCurrentMob(QString mobName)
{
    for(auto& mob : m_aMob)
    {
        if(mob->mobName() == mobName)
        {
            changeCurrentMob(mob);
            break;
        }

    }
}

void CView::onParamChangeLogic(CNode *pNode, const QSharedPointer<IPropertyBase>& prop)
{
    if(!pNode)
        return;
    switch(pNode->nodeType())
    {
    case eMagicTrap:
    case eUnit:
    case ePatrolPoint:
    case eLookPoint:
    case eTrapActZone:
    case eTrapCastPoint:
    {
        CChangeLogicParam* pOp = new CChangeLogicParam(this, pNode, prop);
        QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
        m_pUndoStack->push(pOp);
        break;
    }
    default:
    {
        Q_ASSERT(false && "not implemented");
        break;
    }
    }
}

QString stringFromUnsignedChar(const unsigned char *str ){
    QString result = "";
    int lengthOfString = strlen( reinterpret_cast<const char*>(str) );

    // print string in reverse order
    QString s;
    for( int i = 0; i < lengthOfString; i++ ){
        s = QString( "%1" ).arg( str[i], 0, 16 );

        // account for single-digit hex values (always must serialize as two digits)
        if( s.length() == 1 )
            result.append( "0" );

        result.append( s );
    }

    return result;
}

void CView::logOpenGlData()
{
    const GLubyte * version = glGetString(GL_VERSION);
    const GLubyte * vendor = glGetString(GL_VENDOR);
    const GLubyte * renderer = glGetString(GL_RENDERER);
    ei::log(eLogInfo, QString("OpenGL module:%1 version:%2 vendor:%3 renderer:%4 swap-interval-request:%5")
            .arg(QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES ? "GLES/ANGLE" : "Desktop")
            .arg(version ? reinterpret_cast<const char*>(version) : "unavailable")
            .arg(vendor ? reinterpret_cast<const char*>(vendor) : "unavailable")
            .arg(renderer ? reinterpret_cast<const char*>(renderer) : "unavailable")
            .arg(format().swapInterval()));
}

CMob* CView::ownerMob(CNode* node) const
{
    if(!node)
        return nullptr;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        if(mob->nodes().contains(node) || mob->logicNodes().contains(node))
            return mob;
    }
    return nullptr;
}

bool CView::isMobVisible(CMob* mob) const
{
    return mob && !m_hiddenMobs.contains(mob);
}

void CView::setMobVisible(CMob* mob, bool visible)
{
    if(!mob || !m_aMob.contains(mob) || isMobVisible(mob) == visible)
        return;
    if(visible)
        m_hiddenMobs.remove(mob);
    else
    {
        m_hiddenMobs.insert(mob);
        mob->clearSelect(true);
        if(m_pTree)
            m_pTree->clearViewportSelectionMarker();
        viewParameters();
    }
    collectObjectTreeData();
    update();
    emit loadedMapStateChanged();
}

void CView::setWorkspaceVisibility(const QList<CMob*>& mobs,
                                   bool includeLandscape, bool visible)
{
    bool changed = false;
    bool selectionChanged = false;
    if(includeLandscape && m_landscapeVisible != visible)
    {
        m_landscapeVisible = visible;
        changed = true;
        if(!visible)
        {
            m_bPreviewTile = false;
            changeOperation(EButtonOpSelect);
        }
    }
    for(CMob* mob : mobs)
    {
        if(!mob || !m_aMob.contains(mob) || isMobVisible(mob) == visible)
            continue;
        changed = true;
        if(visible)
            m_hiddenMobs.remove(mob);
        else
        {
            m_hiddenMobs.insert(mob);
            for(CNode* node : selectionSnapshot())
                if(ownerMob(node) == mob) selectionChanged = true;
            mob->clearSelect(true);
        }
    }
    if(!changed)
        return;
    if(selectionChanged && m_pTree)
        m_pTree->clearViewportSelectionMarker();
    collectObjectTreeData();
    viewParameters();
    update();
    emit loadedMapStateChanged();
}

void CView::setLandscapeVisible(bool visible)
{
    if(m_landscapeVisible == visible)
        return;
    m_landscapeVisible = visible;
    if(!visible)
    {
        m_bPreviewTile = false;
        changeOperation(EButtonOpSelect);
    }
    update();
    emit loadedMapStateChanged();
}

bool CView::hasEditableSelection() const
{
    if(!m_activeMob || !isMobVisible(m_activeMob))
        return false;
    bool found = false;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        for(CNode* node : mob->nodes())
            if(node && node->nodeState() == ENodeState::eSelect)
            {
                if(mob != m_activeMob)
                    return false;
                found = true;
            }
        for(CNode* node : mob->logicNodes())
            if(node && node->nodeState() == ENodeState::eSelect)
            {
                if(mob != m_activeMob)
                    return false;
                found = true;
            }
    }
    return found;
}

bool CView::selectNode(CNode* node, bool addToSelection, bool activateOwner)
{
    CMob* owner = ownerMob(node);
    if(!owner || !isMobVisible(owner))
        return false;
    if(activateOwner && owner != m_activeMob)
        changeCurrentMob(owner);
    return selectNodes(QList<CNode*>() << node, addToSelection);
}

bool CView::selectNodes(const QList<CNode*>& nodes, bool addToSelection)
{
    QList<CNode*> selectable;
    for(CNode* node : nodes)
    {
        CMob* owner = ownerMob(node);
        if(node && owner && isMobVisible(owner) && !selectable.contains(node))
            selectable.append(node);
    }
    if(selectable.isEmpty())
        return false;
    const QList<CNode*> before = selectionSnapshot();
    if(!addToSelection)
        for(CMob* mob : m_aMob)
            if(mob) mob->clearSelect(true);
    for(CNode* node : selectable)
    {
        if(addToSelection && node->nodeState() == ENodeState::eSelect)
            node->setState(ENodeState::eDraw);
        else
            node->setState(ENodeState::eSelect);
    }
    recordSelectionChange(before);
    viewParameters();
    update();
    emit loadedMapStateChanged();
    return true;
}

bool CView::activateSelectedObjectMob()
{
    const QList<CNode*> selected = selectionSnapshot();
    if(selected.isEmpty())
        return false;
    CMob* owner = ownerMob(selected.back());
    if(!owner || owner == m_activeMob)
        return false;
    CNode* selectedNode = selected.back();
    changeCurrentMob(owner);
    selectedNode->setState(ENodeState::eSelect);
    viewParameters();
    update();
    if(m_pTree)
        m_pTree->revealNode(selectedNode);
    return true;
}

void CView::checkOpenGlError()
{
    GLenum errCode;
    errCode = glGetError();
    if (errCode != GL_NO_ERROR)
    {
        ei::log(eLogFatal, "OpenGL error: " + QString::number(errCode));

    }
}

void CView::drawTilePreview(QOpenGLShaderProgram* program)
{
    if(m_pPreviewTile.isNull())
        return;

    m_pLand->glTexture()->bind(0); // todo: update preview tile texture after mpr loaded
    program->setUniformValue("qt_Texture0", 0);
    m_pPreviewTile->draw(program);
}

void CView::updateTileForm(bool bGenerateNewTable)
{
    if(bGenerateNewTable)
        m_pTileForm->fillTable(m_pLand->mapName(), m_pLand->textureNum());
    m_pTileForm->setTileTypes(m_pLand->tileTypes());
    m_pTileForm->setMaterial(m_pLand->materials());
    m_pTileForm->setAnimTile(m_pLand->animTiles());
}

void CView::onChangeCursorTile(QPixmap ico)
{
    if(m_pOp->operationMethod() != eOperationMethodTileBrush)
        return;
    // Создаем базовое изображение для курсора (например, простой квадрат)
    QPixmap baseCursorPixmap(42, 42);
    baseCursorPixmap.fill(Qt::transparent);  // Прозрачный фон

    QPainter painter(&baseCursorPixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Устанавливаем цвет и толщину пера для стрелки
    QPen pen(CScene::getInstance()->isLandTileEditMode() ? Qt::red : Qt::green);
    pen.setWidth(2);
    painter.setPen(pen);

    // Рисуем стрелку в левом верхнем углу
    painter.drawLine(2, 2, 10, 10);   // Линия тела стрелки
    painter.drawLine(2, 2, 2, 6);    // Линия нижней части стрелки
    painter.drawLine(2, 2, 6, 2);    // Линия верхней части стрелки

    painter.end();

    // Загружаем дополнительное изображение, которое нужно добавить к курсору
    //QPixmap additionalPixmap(":/images/icon.png");

    // Совмещаем оба изображения: рисуем дополнительное поверх базового
    QPixmap combinedPixmap(baseCursorPixmap.size());
    combinedPixmap.fill(Qt::transparent);

    painter.begin(&combinedPixmap);
    painter.drawPixmap(0, 0, baseCursorPixmap);  // Базовый курсор
    painter.drawPixmap(10, 10, ico); // Дополнительное изображение
    painter.end();

    // Создаем новый курсор из объединенного изображения
    QCursor customCursor(combinedPixmap, 0, 0);

    // Устанавливаем новый курсор для виджета
    setCursor(customCursor);

//    QCursor curs(ico, 0, 0);
    //    setCursor(curs);
}

void CView::onMapMaterialUpdate()
{
    if(!m_pLand->isMprLoad())
        return;

    m_pLand->setMaterials(m_pTileForm->material());
    m_pLand->setAnimTils(m_pTileForm->animTile());
    m_pLand->setTileTypes(m_pTileForm->tileTypes());
}

void CView::onRestoreTileData()
{

}

void CView::checkNewLandVersion()
{
    return; //todo: set option to auto-check changes in mpr. Dont re-upload when save by ourselfes
    // Dont use QFileSystemWatcher because it spams signals several time for 1 MapEd save
    QFileInfo landPath(m_pLand->filePath());
    landPath.refresh();
    QDateTime lastM = landPath.lastModified();
    if (lastM > m_lastModifiedLand)
    {
        unloadLand();
        loadLandscape(landPath);
        m_lastModifiedLand = lastM;
    }
}

void CView::drawSelectFrame(QRect &rect)
{
    //convert frame to [(-1, 1), (-1, 1)]
    float leftX = rect.bottomLeft().x()/(float)m_width*2-1.0f;
    float leftY = rect.bottomLeft().y()/(float)m_height*2-1.0f;
    float rightX = rect.topRight().x()/(float)m_width*2-1.0f;
    float rightY = rect.topRight().y()/(float)m_height*2-1.0f;
    QPointF bottomLeft(leftX, -leftY);
    QPointF topRight(rightX, -rightY);
    m_selectFrame->updateFrame(bottomLeft, topRight);
}

void CView::pickObject(const QRect &rect, bool bAddToSelect)
{
    //TODO: use different buffer => save picking in image
    // Bind shader pipeline for use
    if(nullptr == m_activeMob)
    {
        m_selectFrame->reset();
        return;
    }

    const QList<CNode*> selectionBefore = selectionSnapshot();

    if (!m_selectProgram.bind())
        close();

    const QMatrix4x4 viewMatrix = m_cam->update();
    m_selectProgram.setUniformValue("u_projMmatrix", m_projection);
    m_selectProgram.setUniformValue("u_viewMmatrix", viewMatrix);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const bool cullingEnabled = (!cullingOption || cullingOption->value())
        && (!drawLogicOption || !drawLogicOption->value())
        && CScene::getInstance()->getMode() != eEditModeLogic;
    const CViewFrustum frustum(m_projection * viewMatrix);

    QList<CNode*> pickableNodes;
    const bool logicPicking = CScene::getInstance()->getMode() == eEditModeLogic;
    if(!logicPicking)
        RenderQueueContext::instance().beginSelection(&m_selectProgram);
    for(CMob* mob : m_aMob)
        if(isMobVisible(mob))
        {
            const QVector<CNode*> spatialCandidates = logicPicking
                ? QVector<CNode*>()
                : spatiallyVisibleNodes(mob, cullingEnabled ? &frustum : nullptr, nullptr);
            const QList<CNode*>& legacyCandidates = logicPicking
                ? mob->logicNodes() : mob->nodes();
            if(logicPicking)
                for(CNode* node : legacyCandidates)
                {
                    if(node && !pickableNodes.contains(node))
                    {
                        pickableNodes.append(node);
                        node->drawSelect(&m_selectProgram);
                    }
                }
            else
                for(CNode* node : spatialCandidates)
                {
                    if(node && !pickableNodes.contains(node))
                    {
                        pickableNodes.append(node);
                        node->drawSelect(&m_selectProgram);
                    }
                }
        }
    if(!logicPicking)
        RenderQueueContext::instance().end();

    QVector<SColor> aColor;
    getColorFromRect(rect, aColor);

    //if(m_operationType == EOperationTypeObjects)

    if (!bAddToSelect) // clear selection buffer if we click out of objects in single selection mode
        for(CMob* mob : m_aMob)
            if(mob) mob->clearSelect(true);

    CNode* lastPickedNode = nullptr;
    if (!aColor.isEmpty())
        foreach (CNode* node, pickableNodes)
            foreach (const auto& color, aColor)
            {
                if (node->isColorSuitable(color))
                {
                    lastPickedNode = node;
                    if (bAddToSelect)
                    {//shift pressed

                        if (node->nodeState() & ENodeState::eSelect)
                        {//remove from select
                            node->setState(ENodeState::eDraw);
                        }
                        else
                        {// add to select
                            node->setState(ENodeState::eSelect);
                        }
                        break;
                    }
                    else
                    {//shift not pressed
                        node->setState(ENodeState::eSelect);
                        break;
                    }
                    break;
                }
            }

    m_selectFrame->reset();
    recordSelectionChange(selectionBefore);
    if(m_pTree)
    {
        if(lastPickedNode && lastPickedNode->nodeState() == ENodeState::eSelect)
            m_pTree->revealNode(lastPickedNode);
        else
        {
            const QList<CNode*> selected = selectionSnapshot();
            if(selected.isEmpty())
                m_pTree->clearViewportSelectionMarker();
            else
                m_pTree->revealNode(selected.back());
        }
    }
    emit loadedMapStateChanged();
}

QVector3D CView::getTerrainPos(bool bLand)
{
    QPoint globalCursorPos = QCursor::pos();
    QPoint localCursorPos = mapFromGlobal(globalCursorPos);
    return getTerrainPos(localCursorPos.x(), localCursorPos.y(), bLand);
}

bool CView::loadMob(QFileInfo &filePath)
{
    const auto normalizedPath = [](const QFileInfo& info)
    {
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical)
            .toCaseFolded();
    };
    const QString requestedPath = normalizedPath(filePath);
    for(CMob* loaded : m_aMob)
    {
        if(loaded && normalizedPath(loaded->filePath()) == requestedPath)
        {
            changeCurrentMob(loaded);
            ei::log(eLogInfo, QString("MOB open reused the already loaded document path:%1")
                .arg(filePath.absoluteFilePath()));
            emit updateMsg(tr("%1 is already loaded; its existing document was activated.")
                .arg(filePath.fileName()));
            return true;
        }
    }

    m_pProgress->reset();
    CMob* pMob = new CMob;
    pMob->attach(this, m_pProgress);
    const int result = pMob->readMob(filePath);
    if(result != 0)
    {
        ei::log(eLogWarning, QString("MOB load was rejected without registering a partial document path:%1 result:%2")
            .arg(filePath.absoluteFilePath()).arg(result));
        delete pMob;
        return false;
    }
    QElapsedTimer preloadTimer;
    preloadTimer.start();
    int preloadedUnits = 0;
    int unitCount = 0;
    for(CNode* node : pMob->nodes())
        if(dynamic_cast<CUnit*>(node))
            ++unitCount;
    if(unitCount > 0)
        m_pProgress->reset();
    // Keep the UI responsive during large MOB loads without forcing an event
    // pump and GL-context reacquisition for every unit. Twenty evenly spaced
    // updates keeps the longest zone18 preload interval below a second.
    const int progressStride = qMax(1, unitCount / 20);
    int unitsSinceProgress = 0;
    makeCurrent();
    for(CNode* node : pMob->nodes())
        if(CUnit* unit = dynamic_cast<CUnit*>(node))
        {
            // Progress updates process the Qt event queue. A paint or window
            // event may change/release the thread's current GL context, so a
            // single makeCurrent() before the loop is not a valid guarantee
            // for the following unit's buffer/texture uploads.
            ei::setCrashContext(QString("mob/unit-preload:%1").arg(unit->mapId()));
            if(!QOpenGLContext::currentContext())
            {
                ei::log(eLogFatal, QString(
                    "Unit visual preload stopped because no OpenGL context could be made current; unit:%1")
                    .arg(unit->mapId()));
                break;
            }
            unit->preloadVisualResources();
            ++preloadedUnits;
            ++unitsSinceProgress;
            if(unitsSinceProgress >= progressStride || preloadedUnits == unitCount)
            {
                m_pProgress->update(100.0 * unitsSinceProgress / unitCount);
                unitsSinceProgress = 0;
                // Progress processing may allow another widget to change the
                // current context. Reacquire only after that event boundary,
                // rather than once for every unit.
                makeCurrent();
            }
        }
    makeCurrent();
    doneCurrent();
    ei::setCrashContext("idle after MOB preload");
    if(unitCount > 0)
        m_pProgress->finish();
    ei::log(eLogInfo, QString("Preloaded visual resources for %1 units during MOB load in %2 ms")
            .arg(preloadedUnits).arg(preloadTimer.elapsed()));
    m_aMob.append(pMob);
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;
    m_scriptWorkspaces->registerMob(pMob);
    changeCurrentMob(pMob);
    emit mobStateChanged();
    return true;
}

void CView::saveMobAs()
{
    if(nullptr == m_activeMob)
        return;

    const QFileInfo fileName = QFileDialog::getSaveFileName(
            this, "Save " + m_activeMob->mobName() + " as... ",
            m_activeMob->filePath().absoluteFilePath(),
            tr("Map objects (*.mob);;Mob as JSON(*.json)"));
    if(fileName.filePath().isEmpty())
        return;

    if (fileName.fileName().endsWith(".json"))
        m_activeMob->serializeJson(fileName);
    else if (fileName.fileName().endsWith(".mob"))
    {
        if(!saveMobDocument(m_activeMob, fileName))
            return;
        m_activeMob->setFileName(fileName);
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataActiveMob, fileName.baseName());
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, "");
    }
}

void CView::saveActiveMob()
{
    if(nullptr == m_activeMob)
        return;

    if(saveMobDocument(m_activeMob, m_activeMob->filePath()))
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, "");
}

void CView::saveAllMob()
{
    QList<CMob*> dirtyMobs;
    for(CMob* mob : m_aMob)
        if(mob && mob->isDirty())
            dirtyMobs.append(mob);
    if(dirtyMobs.isEmpty())
        return;

    QProgressDialog progress(tr("Preparing safe MOB saves..."), QString(),
                             0, dirtyMobs.size() * 5, this);
    progress.setWindowTitle(tr("Saving all MOBs"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(250);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    for(int index = 0; index < dirtyMobs.size(); ++index)
    {
        CMob* mob = dirtyMobs.at(index);
        const auto updateProgress = [&progress, index, mob](const QString& label,
                                                            int value, int maximum)
        {
            Q_UNUSED(maximum);
            progress.setLabelText(QString("%1: %2").arg(mob->mobName(), label));
            progress.setValue(index * 5 + value);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        };
        if(!mob->save(updateProgress))
            break;
    }
    progress.setValue(progress.maximum());
    if(m_activeMob && !m_activeMob->isDirty())
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, "");
}

bool CView::saveMobDocument(CMob* mob, const QFileInfo& destination)
{
    if(!mob)
        return false;
    QProgressDialog progress(tr("Preparing safe MOB save..."), QString(), 0, 5, this);
    progress.setWindowTitle(tr("Saving %1").arg(mob->mobName()));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(250);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    const auto updateProgress = [&progress](const QString& label, int value, int maximum)
    {
        progress.setMaximum(maximum);
        progress.setLabelText(label);
        progress.setValue(value);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const bool saved = mob->saveAs(destination, updateProgress);
    progress.setValue(progress.maximum());
    return saved;
}

void CView::unloadActiveMob()
{
    unloadMobDocument(m_activeMob);
}

bool CView::unloadMobDocument(CMob* mob, bool promptForSave)
{
    if(!mob || !m_aMob.contains(mob))
        return false;
    cancelPendingOperation();
    if(!closeMobParameterWindow(mob))
        return false;
    if(!closeMobScriptWindow(mob))
        return false;
    if(promptForSave && mob->isDirty())
    {
        const QMessageBox::StandardButton reply = QMessageBox::question(this,
            tr("Unload MOB"),
            tr("%1 has unsaved changes.\nDo you want to save changes?").arg(mob->mobName()),
            QMessageBox::Save | QMessageBox::No | QMessageBox::Cancel);
        if(reply == QMessageBox::Save && !saveMobDocument(mob, mob->filePath()))
            return false;
        if(reply == QMessageBox::Cancel)
            return false;
    }
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;

    const int removedIndex = m_aMob.indexOf(mob);
    const bool removedActive = mob == m_activeMob;
    ei::log(eLogInfo, "unloading mob " + mob->mobName());
    if(m_pUndoStack)
        m_pUndoStack->clear();
    m_hiddenMobs.remove(mob);
    m_aMob.removeAt(removedIndex);
    m_scriptWorkspaces->unregisterMob(mob);
    emit unloadMob(mob);
    delete mob;

    if(removedActive)
    {
        CMob* next = nullptr;
        if(!m_aMob.isEmpty())
            next = m_aMob.at(qMin(removedIndex, m_aMob.size() - 1));
        changeCurrentMob(next);
    }
    else
    {
        collectObjectTreeData();
        viewParameters();
        emit loadedMapStateChanged();
    }
    if(m_aMob.isEmpty())
    {
        makeCurrent();
        CTextureList::getInstance()->clearTransientVisualCache();
    }
    ei::log(eLogInfo, "Mob unloaded");
    emit mobStateChanged();
    return true;
}

void CView::openActiveMobEditParams()
{
    if(nullptr == m_activeMob)
        return;

    CMobParameters* pParamWindow = nullptr;

    foreach(pParamWindow, m_arrParamWindow)
    {
        if(m_activeMob == pParamWindow->mob())
        {
            qDebug() << "mob already opened";
            return;
        }
    }

    auto pParam = new CMobParameters(this, m_activeMob, this);
    QObject::connect(this, SIGNAL(unloadMob(CMob*)), pParam, SLOT(onMobUnload(CMob*)));
    QObject::connect(pParam, SIGNAL(editFinishedSignal(CMobParameters*)), this, SLOT(onMobParamEditFinished(CMobParameters*)));
    m_arrParamWindow.append(pParam);
    pParam->show();
}

void CView::openActiveMobScript()
{
    if(!m_activeMob)
        return;
    QPointer<CMobScriptWindow> existing = m_scriptWindows.value(m_activeMob);
    if(existing)
    {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    CMob* mob = m_activeMob;
    CMobScriptWindow* window = new CMobScriptWindow(mob, this, this);
    m_scriptWindows.insert(mob, window);
    connect(this, &CView::mobScriptChanged, window, &CMobScriptWindow::onMobScriptChanged);
    connect(this, &CView::externalMobScriptImported,
            window, &CMobScriptWindow::onExternalMobScriptImported);
    connect(window, &QObject::destroyed, this, [this, mob]() { m_scriptWindows.remove(mob); });
    window->show();
    window->raise();
    window->activateWindow();
}

void CView::openExternalMobScript(CMob* mob)
{
    if(mob)
    {
        QString editorExecutable;
        if(m_pSettings)
        {
            if(COptString* option = dynamic_cast<COptString*>(m_pSettings->opt("externalEditorPath")))
                editorExecutable = option->value();
        }
        m_scriptWorkspaces->openEditor(mob, editorExecutable);
    }
}

void CView::commitMobParameterEdit(CMob* mob, const SMobParameterState& before,
                                   const SMobParameterState& after)
{
    if(!mob || before == after)
    {
        setDurty(mob);
        return;
    }
    m_pUndoStack->push(new CChangeMobParametersCommand(this, mob, before, after));
}

void CView::commitMobScriptEdit(CMob* mob, const QString& storedScript,
                                const QString& commandText)
{
    if(!mob || mob->script() == storedScript || !m_pUndoStack)
        return;
    m_pUndoStack->push(new CChangeMobScriptCommand(
        this, mob, mob->script(), storedScript, commandText));
}

void CView::notifyMobScriptChanged(CMob* mob)
{
    if(!mob)
        return;
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    emit mobScriptChanged(mob, cp1251->toUnicode(mob->script().toLatin1()));
}

void CView::importExternalMobScript(CMob* mob, const QString& storedScript,
                                    const QString& displayScript)
{
    commitMobScriptEdit(mob, storedScript, tr("Import external MOB script"));
    // An accepted import is authoritative. If the internal editor happens to
    // be open (for example because the managed file was edited directly), show
    // the imported document state instead of retaining a stale local draft.
    if(mob && mob->script() == storedScript)
        emit externalMobScriptImported(mob, displayScript);
}

bool CView::closeMobScriptWindow(CMob* mob)
{
    QPointer<CMobScriptWindow> window = m_scriptWindows.value(mob);
    return !window || window->requestClose();
}

bool CView::closeMobParameterWindow(CMob* mob)
{
    const QList<CMobParameters*> windows = m_arrParamWindow;
    for(CMobParameters* window : windows)
    {
        if(window && window->mob() == mob && !window->close())
            return false;
    }
    return true;
}

bool CView::closeAllMobParameterWindows()
{
    const QList<CMobParameters*> windows = m_arrParamWindow;
    for(CMobParameters* window : windows)
    {
        if(window && !window->close())
            return false;
    }
    return true;
}

bool CView::closeAllMobScriptWindows()
{
    const QList<QPointer<CMobScriptWindow>> windows = m_scriptWindows.values();
    for(const QPointer<CMobScriptWindow>& window : windows)
    {
        if(window && !window->requestClose())
            return false;
    }
    return true;
}

void CView::unloadMob(QString mobName)
{
    if(m_aMob.isEmpty())
        return;

    cancelPendingOperation();
    if(mobName.isEmpty())
    {
        if(!closeAllMobParameterWindows())
            return;
        if(!closeAllMobScriptWindows())
            return;
    }
    else
    {
        CMob* closingMob = mob(mobName);
        if(closingMob && !closeMobParameterWindow(closingMob))
            return;
        if(closingMob && !closeMobScriptWindow(closingMob))
            return;
    }
    if(m_pUndoStack)
        m_pUndoStack->clear();
    ei::log(eLogInfo, "unloading mob " + mobName);
    CMob* pMob = nullptr;
    if(mobName.isEmpty())
    {
        foreach(pMob, m_aMob)
        {
            m_hiddenMobs.remove(pMob);
            m_scriptWorkspaces->unregisterMob(pMob);
            emit unloadMob(pMob);
            delete pMob;
        }
        m_aMob.clear();
        m_activeMob = nullptr;
    }
    else
    {
        foreach(pMob, m_aMob)
        {
            if (pMob->mobName().toLower() == mobName.toLower())
            {
                m_scriptWorkspaces->unregisterMob(pMob);
                m_hiddenMobs.remove(pMob);
                emit unloadMob(pMob);
                delete pMob;
                m_aMob.removeOne(pMob);
                break;
            }
        }
    }
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;

    if(!m_aMob.isEmpty())
        changeCurrentMob(m_aMob.back());
    else
    {
        changeCurrentMob(nullptr);
        makeCurrent();
        CTextureList::getInstance()->clearTransientVisualCache();
        // Keep the view context current for the next map's terrain/resource
        // uploads; this matches the renderer's existing context lifetime.
    }

    ei::log(eLogInfo, "Mob unloaded");
    viewParameters();
    emit mobStateChanged();
    emit selectionChanged();
}

int CView::nSelectedNodes()
{
    int n = 0;
    for(const auto& mob: m_aMob)
    {
        QList<CNode*> counted;
        for(const auto& node: mob->nodes())
            if(node->nodeState() == ENodeState::eSelect && !counted.contains(node))
            {
                counted.append(node);
                ++n;
            }
        for(const auto& node: mob->logicNodes())
            if(node->nodeState() == ENodeState::eSelect && !counted.contains(node))
            {
                counted.append(node);
                ++n;
            }
    }
    return n;
}

QList<CNode *> CView::selectedNodes()
{
    QList<CNode*> arrNode;
    for(const auto& mob: m_aMob)
    {
        for(const auto& node: mob->nodes())
            if(node->nodeState() == ENodeState::eSelect)
                arrNode.append(node);
    }
    return arrNode;
}

QList<CNode*> CView::selectionSnapshot()
{
    QList<CNode*> selection;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        for(CNode* node : mob->nodes())
        {
            if(node && node->nodeState() == ENodeState::eSelect && !selection.contains(node))
                selection.append(node);
        }
        for(CNode* node : mob->logicNodes())
        {
            if(node && node->nodeState() == ENodeState::eSelect && !selection.contains(node))
                selection.append(node);
        }
    }
    return selection;
}

void CView::restoreSelection(const QList<CNode*>& selection)
{
    QList<CNode*> available;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        mob->clearSelect(true);
        for(CNode* node : mob->nodes())
        {
            if(node && !available.contains(node))
                available.append(node);
        }
        for(CNode* node : mob->logicNodes())
        {
            if(node && !available.contains(node))
                available.append(node);
        }
    }

    for(CNode* node : selection)
    {
        if(node && available.contains(node) && isMobVisible(ownerMob(node)))
            node->setState(ENodeState::eSelect);
    }
    if(m_pTree)
    {
        if(selection.isEmpty())
            m_pTree->clearViewportSelectionMarker();
        else
            m_pTree->revealNode(selection.back());
    }
    viewParameters();
    update();
    emit loadedMapStateChanged();
    emit selectionChanged();
}

bool CView::selectObjectByProvenance(const QString& mobPath, quint32 nid,
                                     const QString& sectionPath)
{
    const QString wantedPath = QDir::cleanPath(QFileInfo(mobPath).absoluteFilePath());
    CMob* targetMob = nullptr;
    for(CMob* mob : m_aMob)
    {
        if(mob && QDir::cleanPath(mob->filePath().absoluteFilePath()).compare(
               wantedPath, Qt::CaseInsensitive) == 0)
        {
            targetMob = mob;
            break;
        }
    }
    if(!targetMob)
        return false;

    CNode* target = nullptr;
    QList<CNode*> nidMatches;
    for(CNode* node : targetMob->nodes())
    {
        if(!node || node->mapId() != nid)
            continue;
        nidMatches.append(node);
        if(!sectionPath.isEmpty() && node->rawSourceObjectPath() == sectionPath)
            target = node;
    }
    if(!target && nidMatches.size() == 1)
        target = nidMatches.first();
    if(!target)
        return false;

    const QList<CNode*> before = selectionSnapshot();
    changeCurrentMob(targetMob);
    restoreSelection(QList<CNode*>() << target);
    recordSelectionChange(before, tr("Select object from validation"));
    return true;
}

void CView::clearObjectSelection()
{
    if(m_aMob.isEmpty())
        return;
    const QList<CNode*> before = selectionSnapshot();
    if(before.isEmpty())
    {
        if(m_pTree)
            m_pTree->clearViewportSelectionMarker();
        return;
    }
    for(CMob* mob : m_aMob)
        if(mob) mob->clearSelect(true);
    recordSelectionChange(before, "Clear selection");
    if(m_pTree)
        m_pTree->clearViewportSelectionMarker();
    viewParameters();
    update();
    emit loadedMapStateChanged();
}

void CView::recordSelectionChange(const QList<CNode*>& before, const QString& text)
{
    const QList<CNode*> after = selectionSnapshot();
    QSet<CNode*> beforeSet;
    QSet<CNode*> afterSet;
    for(CNode* node : before)
        beforeSet.insert(node);
    for(CNode* node : after)
        afterSet.insert(node);
    if(beforeSet == afterSet)
        return;

    m_pUndoStack->push(new CSelectionCommand(this, before, after, text));
    emit selectionChanged();
}

void CView::setRandomComplection(const EObjParam param, const float min, const float max)
{
    if(nullptr == m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : m_activeMob->nodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;
    const bool grouped = targets.size() > 1;
    if(grouped)
        m_pUndoStack->beginMacro(QString("Randomize %1 objects").arg(targets.size()));

    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        float value = util::randomFloat(min ,max);
        QSharedPointer<propFloat> complexity(new propFloat(param, value));
        CChangeProp* pChanger = new CChangeProp(this, pNode, complexity);
        QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
        //QObject::connect(pChanger, SIGNAL(updatePosOnLand(CNode*)), this, SLOT(landPositionUpdate(CNode*)));
        m_pUndoStack->push(pChanger);
    }
    if(grouped)
        m_pUndoStack->endMacro();
    updateParameter(param);
}

void CView::resetSelectedId()
{
    if(nullptr == m_activeMob)
        return;

    // get MOB ranges - is a high priority. Chck if valid data (10+)
    QVector<SRange> arrRange = m_activeMob->ranges(!m_activeMob->isQuestMob());
    if(arrRange.isEmpty())
    {
        ei::log(eLogWarning, "not found available MOB ID range to reset IDs");
        QMessageBox::warning(this, "Warning", "Not found available MOB ID range to reset IDs. Check MOB parameters,");
        return;
    }
    for(auto& range: arrRange)
    {
        if(range.minRange < 10)
            range.minRange = 10;
        if(range.maxRange <= range.minRange)
        {
            ei::log(eLogWarning, "Incorrect MOB ranges:"+QString::number(range.minRange) + ":" + QString::number(range.maxRange));
            break;
        }
    }
    // collect busy IDs (skip ID from selected objects)
    QVector<uint> arrBusyId;
    QVector<uint> arrNodes;
    CNode* pNode = nullptr;
    foreach(pNode, m_activeMob->nodes())
    {
        if (pNode->nodeState() == ENodeState::eSelect)
            arrNodes.append(pNode->mapId());
        else
        {
            arrBusyId.append(pNode->mapId());
        }
    }
    if(arrNodes.isEmpty())
        return;
    if(!confirmScriptReferencedObjectIds(arrNodes, tr("reset these object IDs")))
        return;
    // find range of free IDs
    std::sort(arrBusyId.begin(), arrBusyId.end());
    uint min;
    uint max;
    uint startId(0);
    const auto nElem = arrNodes.size();
    bool bFound = false;
    for(auto& range: arrRange)
    {
        if(nElem > int(range.maxRange-range.minRange))
            continue; // skip too small ranges
        min = range.minRange;
        max = min;
        for(int i(0); i<arrBusyId.size(); ++i)
        {
            if(arrBusyId[i]<min)
                continue; //filter left-side value
            if(arrBusyId[i]==min)
            {// filter conflict value
                ++min;
                max = min;
                continue;
            }
            if(arrBusyId[i]>=range.maxRange)
                break; //filter right-side value
            if(arrBusyId[i] > max)
            { // found value in range
                max = arrBusyId[i];
            }
            else
            {
                continue;
            }
            if(int(max-min) >= nElem)
            {
                startId = min;
                bFound = true;
                break;
            }
            else
            {
                min = arrBusyId[i]+1;
                max = min;
            }
        }
        if(bFound)
            break;
        //not found between busy ids
        if(int(range.maxRange - max) >= nElem)
        {
            startId = max;
            break;
        }
    }

    QMap<uint, uint> reconnectId;
    for(int i(0); i<nElem; ++i)
    {
        reconnectId[arrNodes[i]] = startId;
        ++startId;
    }
    for(auto pair: reconnectId.toStdMap())
        qDebug() << pair.first << ": " << pair.second;
    CResetIdCommand* pReset = new CResetIdCommand(this, reconnectId);
    QObject::connect(pReset, SIGNAL(updateParam()), this, SLOT(viewParameters()));
    m_pUndoStack->push(pReset);
    m_activeMob->setDirty();
    // update tree view?
}


void CView::openMapParameters()
{
    if(!m_pLand->isMprLoad())
        return;

    if(m_pTileForm->isVisible())
    {
        m_pTileForm->show();
        return; // dont update params if window already opened
    }

    updateTileForm();
    m_pTileForm->show();
}

void CView::pickTile(QVector3D posOnLand, bool bLand)
{
    if(!m_pLand->isMprLoad())
        return;

    CTile* pTile = nullptr;
    STileLocation tileLoc;
    if(!m_pLand->pickTile(posOnLand, pTile, tileLoc, bLand))
        return;

    m_pTileForm->selectTile(pTile->tileIndex());
    m_pTileForm->setTileRotation(pTile->tileRotation());
    if(!bLand)
        m_pTileForm->setActiveMatIndex(pTile->materialIndex());

    m_pPreviewTile->updateTile(*pTile, pTile->tileIndex(), pTile->tileRotation(), pTile->materialIndex(), tileLoc.xSec, tileLoc.ySec);
}

void CView::setTile(QVector3D posOnLand, bool bLand)
{
    if(!m_pLand->isMprLoad())
        return;

    CTile* pTile = nullptr;
    STileLocation tileLoc;
    if(!m_pLand->pickTile(posOnLand, pTile, tileLoc, bLand))
        return;

    int index, rotNum, matIndex;
    m_pTileForm->getSelectedTile(index, rotNum);
    if(index < 0)
        return;

    matIndex = m_pTileForm->activeMaterialindex();

    STileInfo tileInfoNew{index, rotNum, matIndex};
    STileInfo tileInfoOld{pTile->tileIndex(), pTile->tileRotation(), pTile->materialIndex()};
    CBrushTileCommand* pReset = new CBrushTileCommand(this, tileInfoNew, tileLoc, tileInfoOld, m_tileBrushCommandId);
    m_pUndoStack->push(pReset);

//    pTile->setTile(index, rotNum);
//    if(!bLand)
//        pTile->setMaterialIndex(matIndex);

//    m_pLand->updateSectorDrawData(tileLoc.xSec, tileLoc.ySec);
}

void CView::setTile(QMap<STileLocation, STileInfo>& arrTileData)
{
    if(!m_pLand->isMprLoad())
        return;

    m_pLand->setTile(arrTileData);
}

void CView::updatePreviewTile(QVector3D posOnLand, bool bLand)
{
    if(!m_pLand->isMprLoad())
        return;

    CTile* pTile = nullptr;
    STileLocation tileLoc;
    if(!m_pLand->pickTile(posOnLand, pTile, tileLoc, bLand))
        return;

    onChangeCursorTile(m_pTileForm->tileWithRot(pTile->tileIndex()));
    int index, rotNum, matIndex;
    m_pTileForm->getSelectedTile(index, rotNum);
    if(index < 0)
        return;

    matIndex = m_pTileForm->activeMaterialindex();
    m_pPreviewTile->updateTile(*pTile, index, rotNum, matIndex, tileLoc.xSec, tileLoc.ySec);
}

void CView::addTileRotation(int step)
{
    int curRot = (m_pTileForm->tileRotation() + step)%4;
    if(curRot < 0)
        curRot = 3;
    m_pTileForm->setTileRotation(curRot);
}

void CView::showOutliner(bool bShow)
{
    emit showOutlinerSignal(bShow);
}

void CView::pickQuickAccessTile(int index)
{
    if(!m_pLand->isMprLoad())
        return;

    m_pTileForm->selectQuickAccessTile(index);
}

void CView::endTileBrushGroup()
{
    m_tileBrushCommandId = (m_tileBrushCommandId+1)%9;
}

bool CView::onExit()
{
    if(!closeAllMobScriptWindows())
        return false;
    bool bCloseAllowed = true;
    CMob* pMob = nullptr;
    foreach(pMob, m_aMob)
    {
        if(!pMob->isDirty())
            continue;

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Exit", pMob->mobName() + " has unsaved changes.\nDo you want to save changes?", QMessageBox::Save|QMessageBox::No|QMessageBox::Cancel);
        if(reply == QMessageBox::Save)
        {
            if(!saveMobDocument(pMob, pMob->filePath()))
            {
                bCloseAllowed = false;
                break;
            }
        }
        else if(reply == QMessageBox::Cancel)
        {
            bCloseAllowed = false;
            break;
        }
    }
    if(!bCloseAllowed)
        return bCloseAllowed;

    if(m_pLand && m_pLand->isDirty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Exit", m_pLand->filePath().baseName() + " has unsaved changes.\nDo you want to save changes?", QMessageBox::Save|QMessageBox::No|QMessageBox::Cancel);
        if(reply == QMessageBox::Save)
        {
            if(!saveLandscapeDocument(m_pLand->filePath()))
                bCloseAllowed = false;
        }
        else if(reply == QMessageBox::Cancel)
        {
            bCloseAllowed = false;
        }
    }
    if(bCloseAllowed)
    {
        saveSession();
    }
    return bCloseAllowed;
}

void CView::updateParameter(EObjParam propType)
{
    return; // not all prop types can be updated for each node. Use direct updating for each operation separately.

    if(nullptr == m_activeMob)
        return;

    QSharedPointer<IPropertyBase> valueToUpdate;
    QSharedPointer<IPropertyBase> value;
    for (const auto& node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;

        if(node->nodeType() == eTrapActZone || node->nodeType() == eTrapCastPoint || node->nodeType() == eLookPoint || node->nodeType() == ePatrolPoint)
            node->getLogicParam(value, propType);
        else
            node->getParam(value, propType);

        if(!valueToUpdate.isNull() && !valueToUpdate->isEqual(value.get()))
        {
            valueToUpdate.reset();
            break;
        }
        valueToUpdate.reset(value->clone()); //same values will be copied every time
    }
    //todo: pipe to direct update value ot x,y,z, in cell
    //m_tableManager->updateParam(param, valueToUpdate);
}

void CView::viewParameters()
{
    if(m_aMob.isEmpty())
    {
        m_tableManager->reset(); //clear table data if empty current mob
        m_tableManager->setReadOnly(false);
        emit selectionStateChanged(false);
        return;
    }

    QList<CNode*> selected;
    for(CMob* mob : m_aMob)
        if(mob && isMobVisible(mob))
            for(CNode* node : CScene::getInstance()->getMode() == eEditModeLogic
                    ? mob->logicNodes() : mob->nodes())
                if(node && node->nodeState() == ENodeState::eSelect
                   && !selected.contains(node))
                    selected.append(node);

    QSet<ENodeType> aType;
    //find unique selected node types
    //CMob* pMob = nullptr;
    //foreach(pMob, m_aMob)
    CNode* pNode = nullptr;
    foreach(pNode, selected)
        aType.insert(pNode->nodeType());

    if (aType.isEmpty())
    {//reset parameters if not selected objects
        m_tableManager->reset();
        m_tableManager->setReadOnly(false);
        emit selectionStateChanged(false);
        return;
    }

    //define common type for parameter display
    int ttt(aType.toList()[0]);
    foreach (const ENodeType& type, aType)
        ttt &= type;
    const ENodeType commonType = ENodeType(ttt);

    QList<QSharedPointer<IPropertyBase>> aProp;
    foreach(pNode, selected)
    {
        if(CScene::getInstance()->getMode() == eEditModeLogic)
            pNode->collectlogicParams(aProp, commonType);
        else
            pNode->collectParams(aProp, commonType);
    }

    m_tableManager->setNewData(aProp);
    m_tableManager->setReadOnly(!hasEditableSelection());
    emit selectionStateChanged(true);
}

void CView::onParamChange(const QSharedPointer<IPropertyBase>& prop)
{
    if(nullptr == m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;
    if(prop->type() == eObjParam_NID && targets.size() > 1)
    {
        ei::log(eLogWarning, "Cannot assign one identifier to multiple objects");
        QMessageBox::warning(this, "Operation Error",
                             "One identifier cannot be assigned to multiple objects.");
        return;
    }

    const bool grouped = targets.size() > 1;
    if(grouped)
        m_pUndoStack->beginMacro(QString("Change %1 objects").arg(targets.size()));

    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        switch (prop->type()) {

        case eObjParam_ROTATION: //todo: need recalc landscape position for objects?
        case eObjParam_COMPLECTION:
        {
            CChangeProp* pChanger = new CChangeProp(this, pNode, prop);
            QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
            //QObject::connect(pChanger, SIGNAL(updatePosOnLand(CNode*)), this, SLOT(landPositionUpdate(CNode*)));
            m_pUndoStack->push(pChanger);
            break;
        }
        case eObjParam_TEMPLATE:
        {
            CChangeProp* pChanger = new CChangeProp(this, pNode, prop); // #todo: update node pos after undo-redo
            QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
            //QObject::connect(pChanger, SIGNAL(updatePosOnLand(CNode*)), this, SLOT(landPositionUpdate(CNode*)));
            m_pUndoStack->push(pChanger);
            break;
        }
        case eObjParam_NID:
        {
            auto pId = dynamic_cast<propUint*>(prop.get());
            if(!pId || pId->value() == pNode->mapId())
                break;
            emit nidChangeProposed(pId->value(), m_activeMob->filePath().absoluteFilePath());
            if(!confirmScriptReferencedObjectIds({pNode->mapId()},
                                                 tr("change this object ID")))
                return;
            CChangeProp* pChanger = new CChangeProp(this, pNode, prop);
            QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
            QObject::connect(pChanger, SIGNAL(changeIdSignal(uint,uint)), m_pTree, SLOT(onChangeNodeId(uint,uint)));
            QObject::connect(pChanger, SIGNAL(changeTreeName(CNode*)), m_pTree, SLOT(onChangeObjectName(CNode*)));
            m_pUndoStack->push(pChanger);
            break;
        }
        default:
        {
            if(CScene::getInstance()->getMode() == eEditModeLogic)
            {
                onParamChangeLogic(pNode, prop);
            }
            else
            {
                CChangeProp* pChanger = new CChangeProp(this, pNode, prop);
                QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
                QObject::connect(pChanger, SIGNAL(changeIdSignal(uint,uint)), m_pTree, SLOT(onChangeNodeId(uint,uint)));
                QObject::connect(pChanger, SIGNAL(changeTreeName(CNode*)), m_pTree, SLOT(onChangeObjectName(CNode*)));
                m_pUndoStack->push(pChanger);
            }
            break;
        }
        }
    }

    if(grouped)
        m_pUndoStack->endMacro();
}

bool CView::applyFamilyNidRepairs(const QVector<SNidRepair>& repairs, QString* error)
{
    struct ResolvedRepair { CMob* mob; CNode* node; SNidRepair repair; };
    QVector<ResolvedRepair> resolved;
    QSet<quint32> replacements;
    for(const SNidRepair& repair : repairs)
    {
        if(repair.oldNid == repair.newNid || replacements.contains(repair.newNid))
        {
            if(error) *error = tr("The repair contains a duplicate or unchanged replacement ID.");
            return false;
        }
        CMob* targetMob = nullptr;
        const QString wanted = QDir::cleanPath(QFileInfo(repair.mobPath).absoluteFilePath());
        for(CMob* mob : m_aMob)
            if(mob && QDir::cleanPath(mob->filePath().absoluteFilePath()).compare(
                   wanted, Qt::CaseInsensitive) == 0)
            {
                targetMob = mob;
                break;
            }
        if(!targetMob)
        {
            if(error) *error = tr("Repair target is not loaded: %1").arg(repair.mobPath);
            return false;
        }
        CNode* targetNode = nullptr;
        for(CNode* node : targetMob->nodes())
            if(node && node->mapId() == repair.oldNid
               && node->rawSourceObjectPath() == repair.sectionPath)
            {
                targetNode = node;
                break;
            }
        if(!targetNode)
        {
            if(error) *error = tr("Could not map NID %1 at %2 onto a loaded raw object.")
                .arg(repair.oldNid).arg(repair.sectionPath);
            return false;
        }
        replacements.insert(repair.newNid);
        resolved.append({targetMob, targetNode, repair});
    }
    if(resolved.isEmpty())
    {
        if(error) *error = tr("No repair rows were selected.");
        return false;
    }

    m_pUndoStack->beginMacro(tr("Repair %1 family NID collision(s)").arg(resolved.size()));
    for(const ResolvedRepair& item : resolved)
    {
        QSharedPointer<IPropertyBase> value(new propUint(eObjParam_NID, item.repair.newNid));
        CChangeProp* command = new CChangeProp(this, item.mob, item.node, value);
        connect(command, SIGNAL(updateParam()), this, SLOT(viewParameters()));
        connect(command, SIGNAL(changeIdSignal(uint,uint)), m_pTree, SLOT(onChangeNodeId(uint,uint)));
        m_pUndoStack->push(command);
    }
    m_pUndoStack->endMacro();
    emit mobStateChanged();
    return true;
}

quint32 CView::suggestFamilyNid(CMob* mob, quint32 localSuggestion)
{
    quint32 suggestion = localSuggestion;
    if(mob)
        emit nidAllocationRequested(mob->filePath().absoluteFilePath(), &suggestion);
    return suggestion;
}

void CView::onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>& properties)
{
    if(properties.isEmpty() || !m_pUndoStack)
        return;
    if(properties.size() > 1)
        m_pUndoStack->beginMacro(QString("Paste %1 properties").arg(properties.size()));
    for(const QSharedPointer<IPropertyBase>& property : properties)
        onParamChange(property);
    if(properties.size() > 1)
        m_pUndoStack->endMacro();
}



void CView::collectObjectTreeData()
{
    m_pTree->prepareForRebuild();
    m_pTree->clear();
    if(m_aMob.isEmpty())
    {
        m_pTree->applyFilter();
        return;
    }

    m_pTree->setColumnCount(2);
    QStringList labels;
    labels << "Objects" << "Count";
    m_pTree->setHeaderLabels(labels);
    m_pTree->header()->setStretchLastSection(false);
    m_pTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pTree->resizeColumnToContents(1);
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        const bool visible = isMobVisible(mob);
        auto mobItem = new CTreeObject(m_pTree);
        QString mobLabel = mob->mobName() + (mob->isDirty() ? " *" : "");
        if(mob == m_activeMob)
            mobLabel += tr(" [Active]");
        else
            mobLabel += tr(" [Locked]");
        if(!visible)
            mobLabel += tr(" [Hidden]");
        mobItem->setText(0, mobLabel);
        mobItem->setText(1, QString::number(mob->nodes().size()));
        mobItem->setData(0, Qt::UserRole + 1,
                         QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(mob)));
        mobItem->setExpanded(true);
        m_pTree->addTopLevelItem(mobItem);

        QMap<QString, QMap<QString, QList<CNode*>>> objects;
        for(CNode* node : mob->nodes())
        {
            if(!node)
                continue;
            const QString typeName = m_pTree->objectNameByType(node->nodeType());
            if(typeName.isEmpty())
                continue;
            CUnit* unit = dynamic_cast<CUnit*>(node);
            const QString group = unit ? unit->databaseName() : node->mapName();
            objects[typeName][group].append(node);
        }

        for(auto typeIt = objects.constBegin(); typeIt != objects.constEnd(); ++typeIt)
        {
            auto typeItem = new CTreeObject(mobItem, nullptr);
            typeItem->setText(0, typeIt.key());
            int typeCount = 0;
            for(const QList<CNode*>& nodes : typeIt.value())
                typeCount += nodes.size();
            typeItem->setText(1, QString::number(typeCount));
            for(auto groupIt = typeIt.value().constBegin(); groupIt != typeIt.value().constEnd(); ++groupIt)
            {
                auto groupItem = new CTreeObject(typeItem, nullptr);
                groupItem->setText(0, groupIt.key());
                groupItem->setText(1, QString::number(groupIt.value().size()));
                if(groupIt.value().size() == 1)
                {
                    groupItem->setNode(groupIt.value().front());
                    groupItem->setToolTip(0, mob == m_activeMob
                        ? tr("Belongs to active MOB %1").arg(mob->mobName())
                        : tr("Belongs to locked MOB %1. Double-click to make it active.")
                            .arg(mob->mobName()));
                }
                else
                    for(CNode* node : groupIt.value())
                    {
                        auto objectItem = new CTreeObject(groupItem, node);
                        objectItem->setText(0, QString::number(node->mapId()));
                        objectItem->setToolTip(0, mob == m_activeMob
                            ? tr("Belongs to active MOB %1").arg(mob->mobName())
                            : tr("Belongs to locked MOB %1. Double-click to make it active.")
                                .arg(mob->mobName()));
                        if(!visible)
                            objectItem->setFlags(objectItem->flags() & ~Qt::ItemIsSelectable);
                    }
                if(!visible)
                    groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);
            }
        }

        if(!visible)
        {
            const QBrush hiddenTextBrush(QColor(128, 128, 128));
            QList<QTreeWidgetItem*> pendingItems;
            pendingItems.append(mobItem);
            while(!pendingItems.isEmpty())
            {
                QTreeWidgetItem* item = pendingItems.takeLast();
                for(int column = 0; column < item->columnCount(); ++column)
                    item->setForeground(column, hiddenTextBrush);
                for(int childIndex = 0; childIndex < item->childCount(); ++childIndex)
                    pendingItems.append(item->child(childIndex));
            }
        }
    }
    m_pTree->applyFilter();
    const QList<CNode*> selected = selectionSnapshot();
    if(!selected.isEmpty())
        m_pTree->markNode(selected.back(), false);
}

void CView::onRestoreCursor()
{
    unsetCursor();
}

void CView::moveCamToSelectedObjects()
{
    CNode* pNode = nullptr;
    CBox box;
    foreach(pNode, m_activeMob->nodes())
    {
        if (pNode->nodeState() != ENodeState::eSelect)
            continue;
        // calculate world position
        CBox nodeBox = pNode->getBBox();
        nodeBox.move(pNode->drawPosition());
        box.expand(nodeBox);
    }

    if(box.isInit())
    {
        m_cam->moveTo(box.center());
        m_cam->moveAwayOn(box.radius()*2.5f); // too much for multi large objects
    }
}

void CView::mousePressEvent(QMouseEvent* event)
{
    m_lastViewportMousePos = event->pos();
    m_hasViewportMousePos = rect().contains(event->pos());
    m_pOp->mousePressEvent(event);
}

void CView::mouseMoveEvent(QMouseEvent* event)
{
    m_lastViewportMousePos = event->pos();
    m_hasViewportMousePos = rect().contains(event->pos());
    m_pOp->mouseMoveEvent(event);
}

void CView::mouseReleaseEvent(QMouseEvent *event)
{
    m_lastViewportMousePos = event->pos();
    m_hasViewportMousePos = rect().contains(event->pos());
    m_pOp->mouseReleaseEvent(event);
}

QPoint CView::operationAnchor() const
{
    if(m_hasViewportMousePos && rect().contains(m_lastViewportMousePos))
        return m_lastViewportMousePos;
    return rect().center();
}

void CView::setTestSpawnMarker(const QVector3D& position, bool adjusted)
{
    m_testSpawnMarker = position;
    m_testSpawnMarkerValid = true;
    m_testSpawnMarkerAdjusted = adjusted;
    update();
}

void CView::setTestSpawnMarkerSelected(bool selected)
{
    m_testSpawnMarkerSelected = m_testSpawnMarkerValid && selected;
    update();
}

bool CView::testSpawnMarkerAt(const QPoint& viewportPosition, int tolerance) const
{
    if(!m_testSpawnMarkerValid || !m_cam || width() <= 0 || height() <= 0)
        return false;
    const QVector3D marker = QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(),
                                       m_testSpawnMarker.z() + 1.0f).project(
        m_cam->viewMatrix(), m_projection, QRect(0, 0, width(), height()));
    if(marker.z() < 0.0f || marker.z() > 1.0f)
        return false;
    // QVector3D::project uses the OpenGL bottom-left viewport origin, while
    // QMouseEvent positions use Qt's top-left widget origin.
    const QPointF markerInWidget(marker.x(), height() - marker.y());
    const QPointF delta = markerInWidget - viewportPosition;
    return delta.x() * delta.x() + delta.y() * delta.y()
        <= double(tolerance * tolerance);
}

void CView::clearTestSpawnMarker()
{
    m_testSpawnMarkerValid = false;
    m_testSpawnMarkerSelected = false;
    update();
}

void CView::setTestEntranceOverlays(const QVector<QRectF>& rectangles,
                                    const QStringList& labels,
                                    int selectedIndex)
{
    m_testEntranceRectangles = rectangles;
    m_testEntranceLabels = labels;
    m_testEntranceLabelImages.clear();
    const QFont labelFont = font();
    const QFontMetrics metrics(labelFont);
    for(const QString& label : labels)
    {
        QRect bounds = metrics.boundingRect(QRect(0, 0, 240, 96),
                                            Qt::AlignCenter | Qt::TextWordWrap,
                                            label);
        bounds.setSize(bounds.size().expandedTo(QSize(36, metrics.height())));
        QImage image(bounds.width() + 12, bounds.height() + 8,
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 180));
        painter.drawRoundedRect(image.rect().adjusted(0, 0, -1, -1), 4, 4);
        painter.setFont(labelFont);
        painter.setPen(QColor(255, 245, 180));
        painter.drawText(image.rect().adjusted(6, 4, -6, -4),
                         Qt::AlignCenter | Qt::TextWordWrap, label);
        m_testEntranceLabelImages.append(image);
    }
    m_testEntranceLabelTexturesDirty = true;
    m_selectedTestEntrance = selectedIndex;
    update();
}

void CView::moveCameraToPoint(const QVector3D& position)
{
    QVector3D terrainPosition(position.x(), position.y(), 0.0f);
    if(CLandscape::getInstance()->projectPt(terrainPosition))
    {
        m_cam->moveTo(terrainPosition);
        update();
    }
}

bool CView::terrainPositionAt(const QPoint& viewportPosition, QVector3D* position)
{
    if(!position || !m_pLand || !m_pLand->isMprLoad()
       || !rect().contains(viewportPosition) || width() <= 0 || height() <= 0)
        return false;

    const QRect viewport(0, 0, width(), height());
    const float screenY = float(height() - viewportPosition.y());
    const QMatrix4x4 view = m_cam->viewMatrix();
    const QVector3D rayStart = QVector3D(float(viewportPosition.x()), screenY, 0.0f)
        .unproject(view, m_projection, viewport);
    const QVector3D rayEnd = QVector3D(float(viewportPosition.x()), screenY, 1.0f)
        .unproject(view, m_projection, viewport);

    bool havePrevious = false;
    float previousT = 0.0f;
    float previousDelta = 0.0f;
    constexpr int steps = 512;
    for(int step = 0; step <= steps; ++step)
    {
        const float t = float(step) / float(steps);
        const QVector3D rayPoint = rayStart + (rayEnd - rayStart) * t;
        QVector3D terrain(rayPoint.x(), rayPoint.y(), 0.0f);
        if(!CLandscape::getInstance()->projectPt(terrain))
        {
            havePrevious = false;
            continue;
        }
        const float delta = rayPoint.z() - terrain.z();
        if(havePrevious && previousDelta >= 0.0f && delta <= 0.0f)
        {
            float low = previousT;
            float high = t;
            QVector3D resolved = terrain;
            for(int iteration = 0; iteration < 14; ++iteration)
            {
                const float middle = (low + high) * 0.5f;
                const QVector3D sample = rayStart + (rayEnd - rayStart) * middle;
                QVector3D ground(sample.x(), sample.y(), 0.0f);
                if(!CLandscape::getInstance()->projectPt(ground))
                {
                    low = middle;
                    continue;
                }
                resolved = ground;
                if(sample.z() - ground.z() > 0.0f)
                    low = middle;
                else
                    high = middle;
            }
            *position = resolved;
            return true;
        }
        havePrevious = true;
        previousT = t;
        previousDelta = delta;
    }
    return false;
}

void CView::wheelEvent(QWheelEvent* event)
{
    m_pOp->wheelEvent(event);
}

void CView::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    auto keySet = m_pOp->keyManager()->keys();
    for(auto& key : keySet)
    {
        m_pOp->keyRelease(key);
    }
}

// draw objects without light and textures, only colored triangles; find suitable object
// x,y - mouse position
CNode* CView::pickObject(QList<CNode*>& aNode, int x, int y)
{
    // Bind shader pipeline for use
    if (!m_selectProgram.bind())
        close();

    const QMatrix4x4 viewMatrix = m_cam->update();
    m_selectProgram.setUniformValue("u_projMmatrix", m_projection);
    m_selectProgram.setUniformValue("u_viewMmatrix", viewMatrix);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const bool cullingEnabled = (!cullingOption || cullingOption->value())
        && (!drawLogicOption || !drawLogicOption->value())
        && CScene::getInstance()->getMode() != eEditModeLogic;
    const CViewFrustum frustum(m_projection * viewMatrix);

    for (auto node: aNode)
    {
        bool drawNode = true;
        if(cullingEnabled && node && node->nodeState() != ENodeState::eSelect)
        {
            CBox bounds = node->getBBox();
            drawNode = !bounds.isInit() || frustum.intersectsSphere(
                bounds.center() + node->drawPosition(), bounds.radius());
        }
        if(drawNode)
            node->drawSelect(&m_selectProgram);
    }

    GLubyte pixel[3];
    glReadPixels(x, m_height-y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    SColor pickedColor(pixel[0], pixel[1], pixel[2]);

    for (auto& node: aNode)
    {
        if (node->isColorSuitable(pickedColor))
            return node;
    }

    return nullptr;
}

//clear buffer, draw only landscape, project point and return vector3D
QVector3D CView::getTerrainPos(const int cursorPosX, const int cursorPosY, bool bLand)
{
    QVector3D point(0,0,0);
    if(!m_pLand || !m_pLand->isMprLoad())
        return point;

    QMatrix4x4 camMatrix = m_cam->viewMatrix();
    // Set modelview-projection matrix

    // Bind shader pipeline for use
    if (!m_landProgram.bind())
        close();

    // Set modelview-projection matrix
    m_landProgram.setUniformValue("u_projMmatrix", m_projection);
    m_landProgram.setUniformValue("u_viewMmatrix", camMatrix);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(bLand)
        m_pLand->draw(&m_landProgram);
    else
    {
        m_pLand->drawWater(&m_landProgram);
    }
    const int posY (height() - cursorPosY);
    float z;
    glReadPixels(cursorPosX, posY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z );
    //QVector3D point (cursorPosX, posY, z);
    point.setX(cursorPosX);
    point.setY(posY);
    point.setZ(z);

    QMatrix4x4 view = m_cam->viewMatrix();
    GLint viewPort[4];
    glGetIntegerv(GL_VIEWPORT, viewPort);
    QRect viewPortRect(viewPort[0], viewPort[1], viewPort[2], viewPort[3]);
    return point.unproject(view, m_projection, viewPortRect); //return Vector3D here
}

void CView::changeOperation(EButtonOp type)
{
    cancelPendingOperation();
    switch (type) {
    case EButtonOpSelect:
        m_pOp->changeState(new CSelect(this)); break;
    case EButtonOpMove:
        m_pOp->changeState(new CMoveAxis(this, EOperateAxisXY)); break;
    case EButtonOpRotate:
        m_pOp->changeState(new CRotateAxis(this, EOperateAxisZ)); break;
    case EButtonOpScale:
        m_pOp->changeState(new CScaleAxis(this, EOperateAxisZ)); break;
    case EButtonOpTilebrush:
        m_pOp->changeState(new CTileBrush(this)); break;
    }
}

bool CView::cancelPendingOperation()
{
    if(!m_pOp)
        return false;

    const EOperationMethod method = m_pOp->operationMethod();
    const bool cancelPastePreview = m_pendingOperationOwnsPaste;
    const bool cancelLogicPlacement = m_pendingLogicPlacementMacro;
    CMob* pasteMob = m_pendingPasteMob;
    const QList<CNode*> pasteNodes = m_pendingPasteNodes;
    const QList<CNode*> originalSelection = m_pendingPasteOriginalSelection;
    QString methodName;
    switch(method)
    {
    case eOperationMethodMoveAxis:
        methodName = "move";
        operationRevert(EOperationAxisType::eMove);
        break;
    case eOperationMethodRotateAxis:
        methodName = "rotate";
        operationRevert(EOperationAxisType::eRotate);
        break;
    case eOperationMethodScaleAxis:
        methodName = "scale";
        operationRevert(EOperationAxisType::eScale);
        break;
    case eOperationMethodTileBrush:
        methodName = "tile-brush";
        endTileBrushGroup();
        break;
    case eOperationMethodSelect:
        return false;
    }

    const int trackedObjects = m_operationBackup.size();
    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    m_pendingOperationOwnsPaste = false;
    m_pendingLogicPlacementMacro = false;
    m_pendingPasteMob = nullptr;
    m_pendingPasteNodes.clear();
    m_pendingPasteOriginalSelection.clear();

    if(cancelPastePreview && pasteMob)
    {
        for(CNode* node : pasteNodes)
        {
            if(!node || !pasteMob->nodes().contains(node))
                continue;
            const uint nodeId = node->mapId();
            pasteMob->discardUncommittedNode(node);
            if(m_pTree)
                m_pTree->onNodeDelete(nodeId);
        }
        restoreSelection(originalSelection);
        setDurty(pasteMob);
    }
    if(cancelLogicPlacement && m_pUndoStack)
    {
        m_pUndoStack->endMacro();
        m_pUndoStack->undo();
    }
    m_pOp->changeState(new CSelect(this));
    ei::log(eLogInfo, QString("Cancelled pending viewport operation method:%1 trackedObjects:%2")
            .arg(methodName).arg(trackedObjects));
    return true;
}

bool CView::hasPendingOperation() const
{
    return m_pOp && m_pOp->operationMethod() != eOperationMethodSelect;
}

bool CView::prepareUndo()
{
    if(!m_pOp || m_pOp->operationMethod() == eOperationMethodSelect)
        return true;

    // Unfinished transforms are previews, not committed history entries.
    // Cancelling a paste preview also removes its temporary objects.
    cancelPendingOperation();
    return false;
}

void CView::operationSetBackup(EOperationAxisType operationType)
{
    if(nullptr == m_activeMob)
        return;

    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    for (auto& node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;
        if(!node->isOperationAxisAllow(operationType))
            continue;

        switch (operationType)
        {
        case EOperationAxisType::eMove:
            m_operationBackup[node] = node->position();
            if(CUnit* unit = dynamic_cast<CUnit*>(node))
                m_unitMovementBackup.insert(unit, unit->movementState());
            break;
        case EOperationAxisType::eRotate:
            m_operationBackup[node] = node->getEulerRotation(); break;
        case EOperationAxisType::eScale:
            m_operationBackup[node] = node->constitution(); break;
        }
    }
}

void CView::operationRevert(EOperationAxisType operationType)
{
    QQuaternion quat;
    if(m_operationBackup.isEmpty())
        return;

    for(auto& pair : m_operationBackup.toStdMap())
        switch (operationType)
        {
        case EOperationAxisType::eMove:
        {
            CUnit* unit = dynamic_cast<CUnit*>(pair.first);
            if(unit && m_unitMovementBackup.contains(unit))
                unit->restoreMovementState(m_unitMovementBackup.value(unit));
            else
                pair.first->updatePos(pair.second);
            break;
        }
        case EOperationAxisType::eRotate:
        {
            quat = QQuaternion::fromEulerAngles(pair.second);
            pair.first->setRot(quat);
            break;
        }
        case EOperationAxisType::eScale:
            pair.first->setConstitution(pair.second); break;
        }

}

void CView::operationApply(EOperationAxisType operationType)
{
    const bool committingPaste = m_pendingOperationOwnsPaste
            && m_pendingPasteMob && !m_pendingPasteNodes.isEmpty();
    if(committingPaste)
    {
        m_pUndoStack->beginMacro(QString("Paste %1 object%2")
                .arg(m_pendingPasteNodes.size())
                .arg(m_pendingPasteNodes.size() == 1 ? "" : "s"));
        for(CNode* node : m_pendingPasteNodes)
        {
            CCreateNodeCommand* create = new CCreateNodeCommand(this, m_pendingPasteMob, node);
            QObject::connect(create, SIGNAL(addNodeSignal(CNode*)), m_pTree, SLOT(addNodeToTree(CNode*)));
            QObject::connect(create, SIGNAL(undo_addNodeSignal(uint)), m_pTree, SLOT(onNodeDelete(uint)));
            m_pUndoStack->push(create);
        }
        m_pUndoStack->push(new CSelectionCommand(this, m_pendingPasteOriginalSelection,
                                                  m_pendingPasteNodes,
                                                  "Select pasted objects"));
    }

    QQuaternion quat;
    QVector3D rot;
    // A paste and its automatically-started placement transform are one user
    // action. Keep the Paste macro open through the transform commands so one
    // Undo removes the pasted objects instead of merely moving them back.
    const bool grouped = !committingPaste && !m_pendingLogicPlacementMacro
            && m_operationBackup.size() > 1;
    if(grouped)
    {
        QString action = "Transform";
        if(operationType == EOperationAxisType::eMove)
            action = "Move";
        else if(operationType == EOperationAxisType::eRotate)
            action = "Rotate";
        else if(operationType == EOperationAxisType::eScale)
            action = "Scale";
        m_pUndoStack->beginMacro(QString("%1 %2 objects").arg(action).arg(m_operationBackup.size()));
    }
    for(auto& pair : m_operationBackup.toStdMap())
    {
        switch (operationType)
        {
        case EOperationAxisType::eMove:
        {
            if(CScene::getInstance()->getMode() == eEditModeLogic)
            {
                ENodeType type = pair.first->nodeType();
                switch(type)
                {
                case eUnit:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeProp* pOp = new CChangeProp(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]);
                    m_pUndoStack->push(pOp);
                    break;
                }
                case ePatrolPoint:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                case eLookPoint:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                case eTrapActZone:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                case eTrapCastPoint:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                default:
                    break;
                }


            }
            else
            {
                QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                CChangeProp* pOp = new CChangeProp(this, pair.first, pos);
                QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                CUnit* unit = dynamic_cast<CUnit*>(pair.first);
                if(unit && m_unitMovementBackup.contains(unit))
                    unit->restoreMovementState(m_unitMovementBackup.value(unit));
                else
                    pair.first->updatePos(m_operationBackup[pair.first]);
                m_pUndoStack->push(pOp);
            }

            break;
        }
        case EOperationAxisType::eRotate:
        {
            rot = pair.first->getEulerRotation();
            QSharedPointer<IPropertyBase> rotation (new prop3D(eObjParam_ROTATION, rot));
            CChangeProp* pOp = new CChangeProp(this, pair.first, rotation);
            QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            quat = QQuaternion::fromEulerAngles(m_operationBackup[pair.first]);
            pair.first->setRot(quat);
            m_pUndoStack->push(pOp);
            break;
        }
        case EOperationAxisType::eScale:
        {
            QSharedPointer<IPropertyBase> complection (new prop3D(eObjParam_COMPLECTION, pair.first->constitution()));
            CChangeProp* pOp = new CChangeProp(this, pair.first, complection);
            QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            pair.first->setConstitution(m_operationBackup[pair.first]);
            m_pUndoStack->push(pOp);
            break;
        }
        }

    }
    if(grouped)
        m_pUndoStack->endMacro();
    if(committingPaste)
        m_pUndoStack->endMacro();
    if(m_pendingLogicPlacementMacro)
        m_pUndoStack->endMacro();
    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    m_pendingOperationOwnsPaste = false;
    m_pendingLogicPlacementMacro = false;
    m_pendingPasteMob = nullptr;
    m_pendingPasteNodes.clear();
    m_pendingPasteOriginalSelection.clear();
}

void CView::moveTo(QVector3D &dir)
{
    if(nullptr == m_activeMob)
        return;

    QVector3D pos;
    for (auto& node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;

        pos = node->position() + dir;
        node->updatePos(pos);
    }

    updateParameter(EObjParam::eObjParam_POSITION);
}

void CView::rotateTo(QVector3D &rot)
{
    const float gimbalAvoidStep = 0.001f;
    QVector3D rotation; //this rotation will be tested
    QQuaternion quat; //this rotation as quat be applied to object
    QVector3D eulerRot; // temp variable
    const auto setNotNan = [&quat, &eulerRot, &gimbalAvoidStep](QVector3D& ch_rot)
    {
        for(int i(0); i< 100; ++i)
        {
            quat = QQuaternion::fromEulerAngles(ch_rot);
            eulerRot = quat.toEulerAngles();
            if(eulerRot.x() != eulerRot.x())
            {
                ch_rot.setX(ch_rot.x() + gimbalAvoidStep);
            }
            else if(eulerRot.y() != eulerRot.y())
            {
                ch_rot.setY(ch_rot.y() + gimbalAvoidStep);
            }
            else if(eulerRot.z() != eulerRot.z())
            {
                ch_rot.setZ(ch_rot.z() + gimbalAvoidStep);
            }
            else
                return;
        }
        return;
    };


    for(auto& node : m_operationBackup.keys())
    {
        rotation = node->getEulerRotation() + rot;
        setNotNan(rotation);
        node->setRot(quat);
    }

    updateParameter(EObjParam::eObjParam_ROTATION);
}

void CView::scaleTo(QVector3D &scale)
{
    if(nullptr == m_activeMob)
        return;

    QVector3D constitution;
    for (auto& node : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;

        constitution = node->constitution() + scale;
        node->setConstitution(constitution);
    }

    updateParameter(EObjParam::eObjParam_COMPLECTION);
}

void CView::resetUnitLogicPaths()
{
    if(nullptr == m_activeMob)
        return;

    //todo: make undo for this operation
    CNode* pNode = nullptr;
    foreach(pNode, m_activeMob->logicNodes())
    {
        if(pNode->nodeState() != ENodeState::eSelect)
            continue;

        if(pNode->nodeType() != ENodeType::eUnit)
            continue;

        auto pUnit = dynamic_cast<CUnit*>(pNode);
        pUnit->clearPaths();
    }
    m_activeMob->logicNodesUpdate();
    m_activeMob->setDirty();
}

void CView::deleteSelectedNodes()
{
    if(!m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : CScene::getInstance()->getMode()==eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;

    QVector<uint> objectIds;
    for(CNode* node : targets)
    {
        switch(node->nodeType())
        {
        case ePatrolPoint:
        case eLookPoint:
        case eTrapActZone:
        case eTrapCastPoint:
            break;
        default:
            objectIds.append(node->mapId());
            break;
        }
    }
    if(!confirmScriptReferencedObjectIds(objectIds, tr("delete these objects")))
        return;

    const bool grouped = targets.size() > 1;
    if(grouped)
        m_pUndoStack->beginMacro(QString("Delete %1 objects").arg(targets.size()));

    QVector<uint> arrMapId;
    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        auto type = pNode->nodeType();
        switch(type)
        {
        case ePatrolPoint:
        {
            auto pPoint = dynamic_cast<CPatrolPoint*>(pNode);
            int unitId(0);
            int patrolId(0);
            m_activeMob->getPatrolHash(unitId, patrolId, pPoint);
            QString hash = QString("%1.%2").arg(unitId).arg(patrolId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        case eLookPoint:
        {
            auto pPoint = dynamic_cast<CLookPoint*>(pNode);
            int unitId(0);
            int patrolId(0);
            int viewId(0);
            m_activeMob->getViewHash(unitId, patrolId, viewId, pPoint);
            QString hash = QString("%1.%2.%3").arg(unitId).arg(patrolId).arg(viewId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        case eTrapActZone:
        {
            auto pZone = dynamic_cast<CActivationZone*>(pNode);
            int unitId(0);
            int patrolId(-2);
            int viewId(-2);
            int zoneId(0);
            m_activeMob->getTrapZoneHash(unitId, zoneId, pZone);
            QString hash = QString("%1.%2.%3.%4").arg(unitId).arg(patrolId).arg(viewId).arg(zoneId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        case eTrapCastPoint:
        {
            auto pCast = dynamic_cast<CTrapCastPoint*>(pNode);
            int unitId(0);
            int patrolId(-2);
            int viewId(-2);
            int zoneId(-2);
            int castPointId(0);
            m_activeMob->getTrapCastHash(unitId, castPointId, pCast);
            QString hash = QString("%1.%2.%3.%4.%5").arg(unitId).arg(patrolId).arg(viewId).arg(zoneId).arg(castPointId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        default:
        {
            arrMapId.append(pNode->mapId());
            break;
        }
        }
    }

    for(auto& id : arrMapId)
    {
        CDeleteNodeCommand* pUndo = new CDeleteNodeCommand(this, id);
        QObject::connect(pUndo, SIGNAL(deleteNodeSignal(uint)), m_pTree, SLOT(onNodeDelete(uint)));
        QObject::connect(pUndo, SIGNAL(undo_deleteNodeSignal(CNode*)), m_pTree, SLOT(addNodeToTree(CNode*)));
        m_pUndoStack->push(pUndo);
    }

    if(grouped)
        m_pUndoStack->endMacro();
    viewParameters();
    //updateViewTree();
}

void CView::selectedObjectToClipboardBuffer()
{
    if(nullptr == m_activeMob)
        return;

    QJsonArray arrObj;
    for(CNode* pNode : selectedNodes())
        if (pNode->nodeState() & ENodeState::eSelect)
        {
            QJsonObject nodeData = pNode->toJson();
            if(pNode->hasRawObjectProvenance())
            {
                QJsonObject provenance;
                provenance.insert("sourceDocumentSha256",
                    QString::fromLatin1(pNode->rawSourceDocumentSha256().toHex()));
                provenance.insert("sourceObjectPath", pNode->rawSourceObjectPath());
                provenance.insert("sourceSubtreeSha256",
                    QString::fromLatin1(pNode->rawSourceSubtreeSha256().toHex()));
                QByteArray sourceSubtree;
                QByteArray cleanSubtree;
                CMob* sourceMob = ownerMob(pNode);
                if(sourceMob && sourceMob->rawObjectTemplateBytes(pNode, &sourceSubtree, &cleanSubtree))
                {
                    provenance.insert("sourceSubtreeBase64",
                        QString::fromLatin1(sourceSubtree.toBase64()));
                    provenance.insert("cleanSubtreeBase64",
                        QString::fromLatin1(cleanSubtree.toBase64()));
                }
                nodeData.insert("_Endurance raw provenance", provenance);
            }
            arrObj.append(nodeData);
        }

    QJsonObject obj;
    obj.insert("Version", 2);
    auto pos = QWidget::mapFromGlobal(QCursor::pos());
    auto posOnLand = getTerrainPos(pos.x(), pos.y());
    QJsonArray point;
    point.append(QJsonValue::fromVariant(posOnLand.x()));
    point.append(QJsonValue::fromVariant(posOnLand.y()));
    point.append(QJsonValue::fromVariant(posOnLand.z()));
    obj.insert("Mouse position", point);
    obj.insert("Data", arrObj);

    QJsonDocument doc(obj);

    if (!m_clipboard_buffer_file.open(QIODevice::WriteOnly))
    {
        Q_ASSERT("Couldn't open option file." && false);
    }
    else
    {
        m_clipboard_buffer_file.write(doc.toJson(QJsonDocument::JsonFormat::Indented));
        m_clipboard_buffer_file.close();
    }

}

void CView::clipboradObjectsToScene()
{
    if(!m_activeMob)
        return;

    if (!m_clipboard_buffer_file.open(QIODevice::ReadOnly))
    {
        Q_ASSERT("Couldn't open option file." && false);
        return;
    }

    if (m_clipboard_buffer_file.size() == 0)
    {
        qDebug() << "empty copypasteBuffer file";
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(m_clipboard_buffer_file.readAll(), &parseError);
    m_clipboard_buffer_file.close();
    //todo: check if has no error
    QJsonObject obj = doc.object();
    if (obj["Version"].toInt() != 2)
        return;

    auto array = obj["Data"].toArray();
    if(array.isEmpty())
    {
        ei::log(eLogInfo, "clipboard empty");
        return;
    }

    const QList<CNode*> originalSelection = selectionSnapshot();
    QList<CNode*> previewNodes;
    for(CMob* mob : m_aMob)
        if(mob) mob->clearSelect(true);
    for(auto it = array.begin(); it < array.end(); ++it)
    {
        CNode* node = m_activeMob->createNode(it->toObject());
        if(!node)
            continue;
        previewNodes.append(node);
        m_pTree->addNodeToTree(node);
    }
    if(previewNodes.isEmpty())
    {
        restoreSelection(originalSelection);
        return;
    }
    //move copyed nodes to new mouse position
    QVector3D oldMousePos;
    QJsonArray arrPoint = obj["Mouse position"].toArray();
    if(arrPoint.size()==3)
        oldMousePos = QVector3D(arrPoint[0].toVariant().toFloat(), arrPoint[1].toVariant().toFloat(), arrPoint[2].toVariant().toFloat());

    auto pos = QWidget::mapFromGlobal(QCursor::pos());
    auto posOnLand = getTerrainPos(pos.x(), pos.y());
    auto mouseDif = posOnLand - oldMousePos;
    mouseDif.setZ(0.0f);
    m_pendingOperationOwnsPaste = true;
    m_pendingPasteMob = m_activeMob;
    m_pendingPasteNodes = previewNodes;
    m_pendingPasteOriginalSelection = originalSelection;
    m_pOp->changeState(new CMoveAxis(this, EOperateAxisXY));
    moveTo(mouseDif);

    viewParameters();
    emit loadedMapStateChanged();
}

void CView::hideSelectedNodes()
{
    CMob* pMob = nullptr;
    CNode* pNode = nullptr;
    foreach (pMob, m_aMob)
    {
        foreach(pNode, pMob->nodes())
            if(pNode->nodeState() == ENodeState::eSelect)
                pNode->setState(ENodeState::eHidden);
    }
    viewParameters();
}

void CView::unHideAll()
{
    CMob* pMob = nullptr;
    CNode* pNode = nullptr;
    foreach (pMob, m_aMob)
    {
        foreach(pNode, pMob->nodes())
        if(pNode->nodeState() == ENodeState::eHidden)
            pNode->setState(ENodeState::eDraw);
    }

}

void CView::setDirtyMpr()
{
    m_pLand->setDirty(true);
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, "*");
}

void CView::setDurty(CMob* pMob)
{
    CMob* changedMob = pMob ? pMob : m_activeMob;
    if(!changedMob)
        return;

    changedMob->refreshDirtyFromProjection();
    // Undo/redo commands retain their owning MOB. If the user switched the
    // active destination after creating a command, reveal and unlock that
    // owner as soon as the command applies instead of changing a locked MOB
    // invisibly in the background.
    if(changedMob != m_activeMob && m_aMob.contains(changedMob))
        changeCurrentMob(changedMob);
    else
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, changedMob->isDirty() ? "*" : "");
    emit mobStateChanged();
}

bool CView::confirmScriptReferencedObjectIds(const QVector<uint>& objectIds,
                                             const QString& operation)
{
    if(!m_activeMob || objectIds.isEmpty())
        return true;
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(m_activeMob->script(), objectIds);
    if(references.isEmpty())
        return true;

    QStringList preview;
    const int previewLimit = qMin(10, references.size());
    for(int index = 0; index < references.size(); ++index)
    {
        const SMobScriptReference& reference = references.at(index);
        const QString detail = QString("ID %1, line %2:%3: %4")
            .arg(reference.objectId).arg(reference.line).arg(reference.column)
            .arg(reference.context);
        ei::log(eLogWarning, QString("Script reference before object edit (%1): %2")
            .arg(operation, detail));
        if(index < previewLimit)
            preview.append(detail);
    }
    if(references.size() > previewLimit)
        preview.append(tr("...and %1 more reference(s); see the workflow log.")
            .arg(references.size() - previewLimit));

    QMessageBox warning(QMessageBox::Warning, tr("Object referenced by MOB script"),
        tr("The active MOB script contains %1 possible reference(s) to the selected object ID(s).\n\n"
           "Continuing may break script logic. Endurance will not rewrite the script automatically.\n\n%2\n\n"
           "Do you want to %3?")
            .arg(references.size()).arg(preview.join('\n'), operation),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    warning.setDefaultButton(QMessageBox::Cancel);
    return warning.exec() == QMessageBox::Yes;
}

void CView::duplicateSelectedObjects()
{
    // Logic points have their own P/L creation workflow and must not cause
    // Ctrl+D to replay a stale normal-object clipboard.
    if(!m_activeMob || selectedNodes().isEmpty())
        return;
    selectedObjectToClipboardBuffer();
    clipboradObjectsToScene();
}

void CView::refreshMobDirtyStates()
{
    for(CMob* mob : m_aMob)
    {
        if(mob)
            mob->refreshDirtyFromProjection();
    }
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag,
            (m_activeMob && m_activeMob->isDirty()) ? "*" : "");
}

void CView::resetCamPosition()
{
    m_cam->reset();
}

void CView::addLogicPoint(bool bLookPoint)
{
    if(nullptr == m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : m_activeMob->logicNodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;
    bool bChangeToMove = false;
    int createdCount = 0;
    const auto beginPlacement = [this, &createdCount, &targets]()
    {
        if(createdCount == 0)
            m_pUndoStack->beginMacro(targets.size() > 1
                ? QString("Create and place logic points on %1 objects").arg(targets.size())
                : QString("Create and place logic point"));
        ++createdCount;
    };

    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        ENodeType type = pNode->nodeType();
        switch(type)
        {
        case eLookPoint:
        {
            if(!bLookPoint)
                break; //dont create patrol point for view point

            int unitId(0); //parent unit map id
            int patrolId(0); //patrol parent id
            int viewId(0); //parent view id
            auto pLook = dynamic_cast<CLookPoint*>(pNode);
            m_activeMob->getViewHash(unitId, patrolId, viewId, pLook);
            QString hash = QString("%1.%2.%3").arg(unitId).arg(patrolId).arg(viewId);
            beginPlacement();
            CCreatePatrolCommand* pUndo = new CCreatePatrolCommand(this, hash);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case ePatrolPoint:
        {
            int unitId(0); //parent unit map id
            int patrolId(0); //patrol parent id
            int viewId(-1); //parent view id
            auto pPoint = dynamic_cast<CPatrolPoint*>(pNode);
            QString hash;
            m_activeMob->getPatrolHash(unitId, patrolId, pPoint);
            if(bLookPoint)
                hash = QString("%1.%2.%3").arg(unitId).arg(patrolId).arg(viewId); // for creating first looking point
            else
                hash = QString("%1.%2").arg(unitId).arg(patrolId); // for creating patrol point

            beginPlacement();
            CCreatePatrolCommand* pUndo = new CCreatePatrolCommand(this, hash);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eUnit:
        {
            if(bLookPoint)
                break; //dont create look point for unit

            QString hash = QString("%1.%2").arg(pNode->mapId()).arg(-1);
            beginPlacement();
            CCreatePatrolCommand* pUndo = new CCreatePatrolCommand(this, hash);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eMagicTrap:
        {
            beginPlacement();
            CCreateTrapPointCommand* pUndo = new CCreateTrapPointCommand(this, pNode->mapId(), !bLookPoint);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eTrapActZone:
        {
            uint trapId = m_activeMob->trapIdByPoint(dynamic_cast<CActivationZone*>(pNode));
            beginPlacement();
            CCreateTrapPointCommand* pUndo = new CCreateTrapPointCommand(this, trapId, !bLookPoint);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eTrapCastPoint:
        {
            uint trapId = m_activeMob->trapIdByPoint(dynamic_cast<CTrapCastPoint*>(pNode));
            beginPlacement();
            CCreateTrapPointCommand* pUndo = new CCreateTrapPointCommand(this, trapId, !bLookPoint);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        default:
            break;
        }

        pNode->setState(ENodeState::eDraw);

    }

    if(bChangeToMove)
    {
        m_pendingLogicPlacementMacro = true;
        m_pOp->changeState(new CMoveAxis(this, EOperateAxisXY));
    }
}

void CView::copySelectedIDsToClipboard()
{
    QVector<uint> arrId;
    CNode* pNode = nullptr;
    foreach(pNode, m_activeMob->nodes())
    {
        if(pNode->nodeState() != eSelect)
            continue;
        arrId.append(pNode->mapId());
    }
    std::sort(arrId.begin(), arrId.end());
    QString text;
    for(auto id : arrId)
    {
        text += QString::number(id) + "\n";
    }
    QClipboard* pClipboard = QGuiApplication::clipboard();
    pClipboard->setText(text);
}

void CView::execMobSwitch()
{
    if(nullptr != m_activeMob && m_aMob.size() == 1) //dont switch beetwen 1 mob
        return;

    CRoundMobCommand* pUndo = new CRoundMobCommand(this);
    m_pUndoStack->push(pUndo);
}

void CView::clearHistory()
{
    m_pUndoStack->clear();
}

void CView::onMobParamEditFinished(CMobParameters *pMob)
{
    m_arrParamWindow.removeOne(pMob);
}

void CView::roundActiveMob()
{
    if(m_aMob.isEmpty())
        return;

    if(nullptr == m_activeMob)
    {
        changeCurrentMob(m_aMob.front());
        return;
    }


    m_activeMob->clearSelect();
    int actIndex = m_aMob.indexOf(m_activeMob);
    if(actIndex == m_aMob.size()-1)
        actIndex = 0;
    else
        ++actIndex;

    changeCurrentMob(m_aMob[actIndex]);
}

void CView::undo_roundActiveMob()
{
    if(m_aMob.size() <1)
        return;

    if(m_aMob.size() == 1)
    {
        if(nullptr != m_activeMob)
            changeCurrentMob(nullptr);

        return;
    }

    m_activeMob->clearSelect();
    int actIndex = m_aMob.indexOf(m_activeMob);
    if(actIndex == 0)
        actIndex = m_aMob.size()-1;
    else
        --actIndex;

    changeCurrentMob(m_aMob[actIndex]);
}

void CView::execUnloadCommand()
{
    if(!m_pLand->isMprLoad() && m_activeMob == nullptr)
        return;

    if(m_activeMob == nullptr)
    {
        cancelPendingOperation();
        m_pUndoStack->clear();
        unloadLand();
        return;
    }

    cancelPendingOperation();
    m_pUndoStack->clear();
    unloadActiveMob();
}

void CView::iterateRoundMob()
{
    if(nullptr == m_activeMob)
        return;

    if(nullptr == m_pRoundForm)
    {
        QList<QString> arrMob;
        arrMob.append(m_activeMob->mobName());
        for(auto& mob: m_aMob)
        {
            if(mob == m_activeMob)
                continue;
            arrMob.append(mob->mobName());
        }
        m_pRoundForm = new CRoundMobForm(this);
        m_pRoundForm->initMobList(arrMob);
        m_pRoundForm->show();
        return;
    }
    m_pRoundForm->round();
}

void CView::applyRoundMob()
{
    if(nullptr == m_pRoundForm)
        return;

    QString newMobName = m_pRoundForm->selectedMob();
    delete m_pRoundForm;
    m_pRoundForm = nullptr;
    if (newMobName == m_activeMob->mobName())
        return;

    auto pChangeMobCommand = new CChangeActiveMobCommand(this, newMobName);
    m_pUndoStack->push(pChangeMobCommand);
}

void CView::saveSession()
{
    if(!m_pLand->isMprLoad())
        return;

    QString mprPath(m_pLand->filePath().absoluteFilePath());
    QVector<QString> arrMobPath;

    for(auto& mob: m_aMob)
    {
        if(mob == m_activeMob)
            continue;
        arrMobPath.append(mob->filePath().absoluteFilePath());
    }
    if(m_activeMob)
        arrMobPath.append(m_activeMob->filePath().absoluteFilePath());

    auto pManager = CSessionDataManager::getInstance();
    pManager->reset();
    pManager->addZoneData(mprPath, arrMobPath);
    QVector3D pos, pivot, rot;
    m_cam->getLocation(pos, pivot, rot);
    pManager->addCameraData(pos, pivot, rot);
    pManager->addQuickTileIndices(m_pTileForm->quickTileSet());
    pManager->saveSession();
}

void CView::loadSession()
{
    auto pManager = CSessionDataManager::getInstance();
    pManager->reset();
    pManager->loadSession();

    QString mprPath;
    QVector<QString> arrMobPath;
    if(!pManager->getZoneData(mprPath, arrMobPath))
        return;

    if(mprPath.isEmpty())
        return;

    QFileInfo mprFile(mprPath);
    if(!mprFile.exists())
    {
        ei::log(eLogWarning, "Cannot find MPR file: " + mprFile.absoluteFilePath());
        return;
    }
    cancelPendingOperation();
    m_pUndoStack->clear();
    loadLandscape(mprFile);

    for(const auto& mobPath: arrMobPath)
    {
        QFileInfo mobFile(mobPath);
        if(!mobFile.exists())
        {
            ei::log(eLogWarning, "Cannot find MOB file: " + mobFile.absoluteFilePath());
            continue;
        }
        loadMob(mobFile);
        roundActiveMob();
    }

    QVector3D pos, pivot, rot;
    if(pManager->getCameraData(pos, pivot, rot))
    {
        m_cam->moveTo(pos);
        m_cam->setLocation(pos, pivot, rot);
    }

    QVector<int> arrQuickInd;
    if(pManager->getdQuickTileIndices(arrQuickInd))
    {
        m_pTileForm->setQuickTileSet(arrQuickInd);
    }
}

bool CView::isRecentAvailable()
{
    return m_recentOpenedFile_file.exists();
}

int CView::renameActiveMobUnits(QMap<QString, QString> &mapName)
{
    ei::log(ELogMessageType::eLogInfo, "processing map " + m_activeMob->mobName());
    int n=0;
    CUnit* pUnit = nullptr;
    QString sourceName;
    for(auto& node:m_activeMob->nodes())
    {
        if(node->nodeType() != ENodeType::eUnit)
            continue;
        pUnit = dynamic_cast<CUnit*>(node);
        sourceName = pUnit->databaseName();
        if(mapName.contains(sourceName))
        {
            QSharedPointer<IPropertyBase> newName(new propStr(eObjParam_UNIT_PROTOTYPE, mapName[sourceName]));
            pUnit->applyParam(newName);
            ++n;
            ei::log(eLogInfo, "Unit:" + QString::number(pUnit->mapId()) + " last name:" + sourceName + " new name:" + newName->toString());
        }
        else
            ei::log(ELogMessageType::eLogFatal, "not found:" + sourceName);
    }
    ei::log(ELogMessageType::eLogInfo, "processing map finished:" + m_activeMob->mobName());
    return n;
}

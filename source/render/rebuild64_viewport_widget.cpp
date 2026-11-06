#include "rebuild64_viewport_widget.h"
#include "view.h"
#include "camera.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "light.h"
#include "torch.h"
#include "view_frustum.h"
#include <QSaveFile>
#include "operationmanager.h"
#include "key_manager.h"
#include "settings.h"
#include "options.h"
#include "resourcemanager.h"
#include "render_environment.h"
#include "rebuild64_startup_guard.h"
#include "layout_components/viewport_widget.h"
#include "endurance_document.h"
#include <QActionGroup>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLibrary>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QTimer>
#include <QWheelEvent>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

class CRebuild64NativeSurface : public QWidget {
public:
    explicit CRebuild64NativeSurface(QWidget* parent):QWidget(parent) {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_PaintOnScreen);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }
    QPaintEngine* paintEngine() const override { return nullptr; }
};

struct CRebuild64Viewport::Worker {
    struct Input {
        QByteArray terrain,name;
        QVector<QByteArray> mobs;
        QStringList paths;
        quint64 generation{0};
        EirCamera camera{};
        QSize size;
        QString capture;
    } input;
    CRebuild64Viewport* owner;
    void* window;
    QString scratch;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> loaded{false};
    bool stop{false},paused{false};
    std::thread thread;
    Worker(CRebuild64Viewport* target,void* hwnd,const QString& cache)
        :owner(target),window(hwnd),scratch(cache),thread([this]{run();}) {}
    ~Worker() {
        { std::lock_guard<std::mutex> lock(mutex); stop=true; }
        wake.notify_all();
        if(thread.joinable())thread.join();
    }
    void pause(bool value) {
        {std::lock_guard<std::mutex> lock(mutex);paused=value;} wake.notify_all();
    }
    void failure(const QString& detail) {
        QMetaObject::invokeMethod(owner,[target=owner,detail]{target->fallback(detail);},Qt::QueuedConnection);
    }
    void run() {
        CRebuild64StartupGuard startup(scratch);
        if(!startup.arm()) {failure(QString::fromUtf8("Не удалось записать метку безопасного запуска Rebuild64."));return;}
        // QLibrary and every DLL/D3D call live on this one worker, including teardown.
        QLibrary library(QDir(QCoreApplication::applicationDirPath()).filePath("ataman_rebuild64.dll"));
        const QByteArray hook=qgetenv("ATAMAN_REBUILD64_TEST_FAILURE");
        if(hook=="missing")library.setFileName(library.fileName()+".missing");
        if(hook=="load")library.setFileName(library.fileName()+".bad");
        if(!library.load()) {
            failure(QString::fromUtf8("Не удалось загрузить Rebuild64: ")+library.errorString());return;
        }
        auto getApi=reinterpret_cast<EirGetApi>(library.resolve("ataman_renderer_get_api"));
        EirApi api{};api.size=sizeof(api);
        if(!getApi || getApi(EIR_ABI,&api) || api.abi!=EIR_ABI || api.size!=sizeof(api)) {
            failure(QString::fromUtf8("Несовместимая DLL Rebuild64 (C ABI)."));return;
        }
        loaded=true;
        void* handle=nullptr;quint64 generation=0;QSize size;QElapsedTimer pending;QStringList activePaths;
        bool injected=false;
        for(;;) {
            const auto frameStart=std::chrono::steady_clock::now();
            Input next;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(16),[this]{return stop || (!paused && input.generation);});
                if(stop)break;
                if(paused || !input.generation)continue;
                next=input; input.capture.clear();
            }
            EirError error{};
            if(handle&&activePaths!=next.paths){api.destroy(handle);handle=nullptr;generation=0;}
            if(!handle) {
                std::vector<std::wstring> paths;
                for(const auto& path:next.paths)paths.push_back(path.toStdWString());
                std::vector<EirMount> mounts;
                for(size_t i=0;i<paths.size();++i)mounts.push_back({reinterpret_cast<const uint16_t*>(paths[i].c_str()),QFileInfo(next.paths[int(i)]).isDir()?3u:0u,0});
                const auto cache=scratch.toStdWString();
                EirCreate info{sizeof(info),EIR_ABI,uint32_t(next.size.width()),uint32_t(next.size.height()),window,mounts.data(),uint32_t(mounts.size()),hook=="no-d3d11"?1u:hook=="init"?2u:0u,reinterpret_cast<const uint16_t*>(cache.c_str())};
                if(api.create(&info,&handle,&error)) {failure(QString::fromUtf8(error.message));break;}
                size=next.size;
                activePaths=next.paths;
            }
            if(generation!=next.generation) {
                if(!startup.arm()){failure(QString::fromUtf8("Не удалось записать метку загрузки Rebuild64."));break;}
                std::vector<EirSpan> mobs;
                for(const auto& bytes:next.mobs)mobs.push_back({bytes.constData(),uint64_t(bytes.size())});
                EirDocument doc{sizeof(doc),uint32_t(mobs.size()),next.generation,{next.terrain.constData(),uint64_t(next.terrain.size())},mobs.data(),next.name.constData()};
                if(api.document(handle,&doc,&error)) {failure(QString::fromUtf8(error.message));break;}
                generation=next.generation;pending.restart();
            }
            if(next.terrain.isEmpty()) {
                std::lock_guard<std::mutex> lock(mutex);paused=true;continue;
            }
            if(size!=next.size) {
                if(api.resize(handle,uint32_t(next.size.width()),uint32_t(next.size.height()),&error)){failure(QString::fromUtf8(error.message));break;}
                size=next.size;
            }
            if(api.camera(handle,&next.camera,&error)){failure(QString::fromUtf8(error.message));break;}
            if(hook=="device-lost"&&!injected){api.test_device_loss(handle);injected=true;}
            EirStatistics stats{};const int rc=api.frame(handle,&stats,&error);
            if(rc!=0 && rc!=1){failure(QString::fromUtf8(error.message));break;}
            if(rc==1 && pending.isValid() && pending.elapsed()>30000) {
                failure(QString::fromUtf8("Rebuild64: данные нового кадра не подготовлены за 30 секунд. Открыт OpenGL."));break;
            }
            if(!rc) {
                startup.clear();
                pending.invalidate();
                QMetaObject::invokeMethod(owner,[target=owner,stats]{
                    target->m_statistics=stats;
                    if(target->m_selected && stats.ready_generation==target->m_generation) {
                        target->m_stack->setCurrentWidget(target->m_native);
                        target->movePanels(true);
                        emit target->frameCommitted(stats.ready_generation);
                    }
                },Qt::QueuedConnection);
                if(!next.capture.isEmpty()) {
                    const auto path=next.capture.toStdWString();
                    if(api.capture(handle,reinterpret_cast<const uint16_t*>(path.c_str()),&error))failure(QString::fromUtf8(error.message));
                }
            }
            // No busy loop when the condition predicate already has input.
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_until(lock,frameStart+std::chrono::milliseconds(16),[this]{return stop||paused;});
        }
        if(handle)api.destroy(handle);
        loaded=false;
    }
};

CRebuild64Viewport::CRebuild64Viewport(CView* gl,QMenu* menu,QWidget* parent)
    :QWidget(parent),m_gl(gl),m_native(new CRebuild64NativeSurface(this)),m_stack(new QStackedLayout(this)),m_timer(new QTimer(this)) {
    setObjectName("rebuild64Viewport");m_native->setObjectName("rebuild64NativeSurface");
    m_stack->setContentsMargins(0,0,0,0);m_stack->setSpacing(0);m_stack->addWidget(gl);m_stack->addWidget(m_native);m_stack->setCurrentWidget(gl);
    auto* viewMenu=menu->addMenu(QString::fromUtf8("Вид карты"));
    auto* group=new QActionGroup(this);group->setExclusive(true);
    m_glAction=viewMenu->addAction("OpenGL");m_rebuildAction=viewMenu->addAction(QString::fromUtf8("Rebuild64 (просмотр)"));
    for(auto* action:{m_glAction,m_rebuildAction}){action->setCheckable(true);group->addAction(action);}
    m_glAction->setChecked(true);
    connect(m_glAction,&QAction::triggered,this,[this]{selectRebuild(false);});
    connect(m_rebuildAction,&QAction::triggered,this,[this]{selectRebuild(true);});
    m_native->installEventFilter(this);
    m_timer->setInterval(16);connect(m_timer,&QTimer::timeout,this,&CRebuild64Viewport::tick);
    m_guardDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("rebuild64");
    m_interruptedStarts=CRebuild64StartupGuard::interrupted(m_guardDirectory);
    connect(gl,&QObject::destroyed,this,[this]{m_selected=false;m_timer->stop();m_worker.reset();m_gl=nullptr;});
    connect(gl,&CView::mobStateChanged,this,[this]{m_documentRefreshPending=true;});
    connect(gl,&CView::loadedMapStateChanged,this,[this]{m_documentRefreshPending=true;});
    if(!m_interruptedStarts.isEmpty()) {
        QTimer::singleShot(0,this,[this]{
            if(m_interruptedStarts.isEmpty()||!m_gl)return;
            // Latch before acknowledging the crash: removing its marker must
            // never re-enable a crashing saved backend on the next startup.
            QSaveFile latch(QDir(m_guardDirectory).filePath("manual-retry-required"));
            const bool recorded=latch.open(QIODevice::WriteOnly)
                &&latch.write("Rebuild64 requires an explicit menu selection\n")>0&&latch.commit();
            emit m_gl->updateMsg(QString::fromUtf8(
                "Предыдущая загрузка Rebuild64 завершилась аварийно. Открыт OpenGL. Для повторного запуска выберите Rebuild64 в Настройках → Вид карты."));
            if(recorded){for(const auto& path:m_interruptedStarts)QFile::remove(path);m_interruptedStarts.clear();}
        });
    } else if(!QFileInfo::exists(QDir(m_guardDirectory).filePath("manual-retry-required"))
              &&QSettings().value("ui/renderer",QStringLiteral("OpenGL")).toString()=="Rebuild64")
        QTimer::singleShot(0,this,[this]{selectRebuild(true,false);});
}
CRebuild64Viewport::~CRebuild64Viewport(){m_timer->stop();m_worker.reset();movePanels(false);}
void CRebuild64Viewport::movePanels(bool native) {
    if(!m_gl)return;
    if(native) {
        for(auto* child:m_gl->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
            if(dynamic_cast<CViewportWidget*>(child)&&!m_panels.contains(child))m_panels.append(child);
    }
    for(const auto& pointer:m_panels) {
        auto* panel=pointer.data();if(!panel)continue;
        QWidget* parent=native?static_cast<QWidget*>(m_native):static_cast<QWidget*>(m_gl);
        if(panel->parentWidget()==parent)continue;
        const bool shown=!panel->isHidden();const auto geometry=panel->geometry();
        panel->setParent(parent);
        // These controls already own an opaque backing rectangle in OpenGL.
        // Native child HWNDs keep the same paint/input path above D3D, without
        // a transparent top-level window, screen capture or coordinate scaling.
        panel->setAttribute(Qt::WA_NativeWindow,native);
        panel->setGeometry(geometry);panel->setVisible(shown);panel->raise();
    }
    if(!native)m_panels.clear();
}
bool CRebuild64Viewport::backendLoaded() const {return m_worker&&m_worker->loaded.load();}
void CRebuild64Viewport::selectRebuild(bool selected,bool persist) {
    if(!m_gl)return;
    m_selected=selected;m_glAction->setChecked(!selected);m_rebuildAction->setChecked(selected);
    if(persist)QSettings().setValue("ui/renderer",selected?"Rebuild64":"OpenGL");
    if(persist) {
        for(const auto& path:m_interruptedStarts)QFile::remove(path);
        m_interruptedStarts.clear();
        QFile::remove(QDir(m_guardDirectory).filePath("manual-retry-required"));
    }
    movePanels(false);
    m_stack->setCurrentWidget(m_gl);
    if(!selected) {
        m_timer->stop();if(m_worker)m_worker->pause(true);
        m_gl->resumeRendering();m_gl->setFocus();return;
    }
    m_failure.clear();m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();
    emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: подготовка вида; редактирование — в OpenGL."));
    m_timer->start();tick();
}
void CRebuild64Viewport::fallback(const QString& message) {
    if(!m_gl)return;
    m_failure=message;
    selectRebuild(false,false);
    m_worker.reset();
    emit m_gl->updateMsg(QString::fromUtf8("Rebuild64 недоступен. Открыт OpenGL. ")+message);
    emit backendFailure(message);
}
EirCamera CRebuild64Viewport::cameraSnapshot() {
    EirCamera result{};
    const auto view=m_stack->currentWidget()==m_native?m_gl->m_cam->update():m_gl->m_cam->viewMatrix();
    const auto clip=m_gl->m_projection*view;
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)
        result.world_to_clip[row*4+col]=row==2?.5f*(clip(2,col)+clip(3,col)):clip(row,col);
    const auto inverse=view.inverted();const auto eye=inverse.map(QVector3D());const auto target=inverse.map(QVector3D(0,0,-1));
    result.eye[0]=eye.x();result.eye[1]=eye.y();result.eye[2]=eye.z();
    result.target[0]=target.x();result.target[1]=target.y();result.target[2]=target.z();
    result.hour=m_gl->effectiveWorldTimeHours();
    const auto environment=ei::renderEnvironmentForValues(float(result.hour),1,1,m_gl->m_caveEnvironment,m_gl->m_weatherRegion);
    for(int axis=0;axis<3;++axis) {
        result.ambient[axis]=environment.ambientColor[axis]*environment.ambientStrength;
        result.sun_diffuse[axis]=environment.sunColor[axis]*environment.sunStrength;
        result.sun_direction[axis]=-environment.sunDirection[axis];
        result.fog_color[axis]=environment.fogColor[axis];
    }
    // Match CView's Full environment LIGHT/TORCH admission. Copy live values
    // every camera tick, including preview positions; no saved-file light cache.
    const auto enabled=[this](const char* name,bool fallback){const auto* o=m_gl->m_pSettings?dynamic_cast<const COptBool*>(m_gl->m_pSettings->opt(name)):nullptr;return o?o->value():fallback;};
    const auto* mode=m_gl->m_pSettings?dynamic_cast<const COptInt*>(m_gl->m_pSettings->opt("renderLightingMode")):nullptr;
    const CViewFrustum frustum(clip);uint32_t source=0;
    if((!mode||mode->value()==2)&&m_gl->m_sceneLightsVisible&&enabled("renderSceneLights",true))
        for(auto* mob:m_gl->m_aMob)for(auto* node:mob->nodes()) {
            ++source;if(node->nodeState()==ENodeState::eHidden)continue;
            QVector3D position,color;float radius=0;bool shadow=false;
            if(auto* light=dynamic_cast<CLight*>(node)) {
                position=(light->drawPosition().isNull()?light->position():light->drawPosition())+QVector3D(0,0,2);
                color=light->lightColor();radius=light->range();shadow=light->shadowEnabled();
            } else if(auto* torch=dynamic_cast<CTorch*>(node)) {
                position=torch->particleWorldPosition();color={.8f,.8f,.8f};radius=torch->power()*10;shadow=true;
            }
            if(radius<=0||(enabled("frustumCulling",true)&&!frustum.intersectsSphere(position,radius)))continue;
            if(result.point_light_count==EIR_MAX_POINT_LIGHTS)break;
            if(qMax(color.x(),qMax(color.y(),color.z()))>1.01f)color/=255.f;
            auto& light=result.point_lights[result.point_light_count++];
            for(int axis=0;axis<3;++axis){light.position[axis]=position[axis];light.color[axis]=color[axis];}
            light.radius=radius;light.casts_shadow=shadow;light.source_id=source;
        }
    result.fog_start=environment.fogStart;result.fog_end=environment.fogEnd;
    result.region=m_gl->m_weatherRegion=="ingos"?2:m_gl->m_weatherRegion=="suslanger"?3:1;result.cave=m_gl->m_caveEnvironment?1:0;
    auto skyView=view;skyView(0,3)=skyView(1,3)=skyView(2,3)=0;
    skyView.translate(0,0,-6.95752f);skyView.rotate(environment.skyRotationDegrees,0,0,1);
    const auto skyClip=m_gl->m_projection*skyView;
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)
        result.sky_to_clip[row*4+col]=skyClip(row==2?3:row,col);
    for(int axis=0;axis<3;++axis)result.sky_tint[axis]=environment.skyColor[axis];
    result.visuals=(m_gl->m_skyboxVisible&&!m_gl->m_caveEnvironment?1u:0u)
        |(m_gl->m_fogVisible?2u:0u)|(m_gl->m_bDrawLand?4u:0u)
        |(m_gl->m_bDrawWater?8u:0u)|(m_gl->m_staticObjectsVisible?16u:0u);
    return result;
}
void CRebuild64Viewport::tick() {
    if(!m_selected||!m_gl||m_gl->m_applicationShutdown)return;
    if(!m_gl->m_pLand || !m_gl->m_pLand->isMprLoad()) {
        movePanels(false);
        m_stack->setCurrentWidget(m_gl);
        if(m_worker&&!m_documentHash.isEmpty()) {
            std::lock_guard<std::mutex> lock(m_worker->mutex);
            m_worker->input.terrain.clear();m_worker->input.mobs.clear();m_worker->input.generation=++m_generation;m_worker->paused=false;
            m_worker->wake.notify_all();
        }
        m_documentHash.clear();return;
    }
    if(m_worker&&m_sceneSnapshotTimer.isValid()&&m_sceneSnapshotTimer.elapsed()<100) {
        const auto camera=cameraSnapshot();
        const QSize size(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
        {std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.camera=camera;m_worker->input.size=size;m_worker->paused=false;}
        return;
    }
    m_sceneSnapshotTimer.restart();
    const auto terrainState=m_gl->m_terrainEditAvailable?m_gl->m_terrainEdit.stateSha256():QByteArray();
    if(m_worker&&!m_documentRefreshPending&&terrainState==m_terrainState) {
        const auto camera=cameraSnapshot();
        std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.camera=camera;
        m_worker->input.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
        return;
    }
    m_terrainState=terrainState;m_documentRefreshPending=false;
    Worker::Input next;
    CMprEditTransaction transaction(QStringLiteral("Rebuild64 memory snapshot"));CValidationReport report;
    if(!m_gl->m_terrainEditAvailable) {
        // CView defers its editor model after visible load. Its original bytes
        // are already owned in memory; preview must not reopen the MPR meanwhile.
        next.terrain=m_gl->m_pLand->originalMprData();
    } else if(!m_gl->m_terrainEdit.appendSectorEdits(&transaction,&report)
       || !m_gl->m_mprDocument.appendMapParametersEdit(m_gl->m_terrainEdit.maxZ(),m_gl->m_mprMaterials,m_gl->m_mprAnimatedTiles,&transaction,&report)
       || !m_gl->m_mprDocument.applyTransaction(transaction,&next.terrain,&report)) {
        fallback(QString::fromUtf8("Не удалось создать снимок рельефа из памяти."));return;
    }
    if(next.terrain.isEmpty()){fallback(QString::fromUtf8("В памяти нет загруженного рельефа."));return;}
    for(auto* mob:m_gl->m_aMob)if(mob&&m_gl->isMobVisible(mob)) {
        QByteArray bytes;mob->serializeMob(bytes,false);next.mobs.append(bytes);
    }
    next.name=m_gl->m_pLand->filePath().fileName().toUtf8();
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(next.name);hash.addData(next.terrain);
    for(const auto& mob:next.mobs){hash.addData(QByteArray::number(mob.size()));hash.addData(mob);}
    const auto fingerprint=hash.result();if(fingerprint!=m_documentHash){m_documentHash=fingerprint;++m_generation;}
    next.generation=m_generation;next.camera=cameraSnapshot();
    next.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
    next.paths=configuredResourceSourcePaths(m_gl->settings());
    // CView's validated map-local atlas can override the profile's RES order.
    // It is a resource source, never a file used for geometry or save output.
    if(!m_gl->m_activeTerrainArchivePath.isEmpty()) {
        next.paths.removeAll(m_gl->m_activeTerrainArchivePath);
        next.paths.prepend(m_gl->m_activeTerrainArchivePath);
    }
    const QString mapRoot=m_gl->m_pLand->filePath().absolutePath();if(!next.paths.contains(mapRoot))next.paths.prepend(mapRoot);
    if(!m_worker) {
        const QString scratch=m_guardDirectory;
        QDir().mkpath(scratch);
        m_worker.reset(new Worker(this,reinterpret_cast<void*>(m_native->winId()),scratch));
    }
    {std::lock_guard<std::mutex> lock(m_worker->mutex);next.capture=m_worker->input.capture;m_worker->input=std::move(next);m_worker->paused=false;}
    m_worker->wake.notify_all();
}
void CRebuild64Viewport::capture(const QString& path) {
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.capture=path;}
}
bool CRebuild64Viewport::eventFilter(QObject* object,QEvent* event) {
    if(object==m_native) {
        switch(event->type()) {
        case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::MouseButtonPress:case QEvent::MouseMove:case QEvent::MouseButtonRelease:case QEvent::Wheel:case QEvent::FocusOut:
            cameraInput(event);return true;
        case QEvent::Resize:if(m_selected){m_gl->resize(m_native->size());QTimer::singleShot(0,this,&CRebuild64Viewport::tick);}break;
        default:break;
        }
    }
    return QWidget::eventFilter(object,event);
}
void CRebuild64Viewport::cameraInput(QEvent* event) {
    if(event->type()==QEvent::FocusOut){m_gl->m_pOp->keyManager()->keys().clear();return;}
    if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease) {
        auto* key=static_cast<QKeyEvent*>(event);if(key->isAutoRepeat())return;
        EKeyCode code;
        switch(key->key()) {
        case Qt::Key_W:code=eKey_W;break;case Qt::Key_A:code=eKey_A;break;case Qt::Key_S:code=eKey_S;break;case Qt::Key_D:code=eKey_D;break;
        case Qt::Key_Q:code=eKey_Q;break;case Qt::Key_E:code=eKey_E;break;case Qt::Key_Shift:code=eKey_Shift;break;
        case Qt::Key_Up:code=eKey_UpArrow;break;case Qt::Key_Down:code=eKey_DownArrow;break;case Qt::Key_Left:code=eKey_LeftArrow;break;case Qt::Key_Right:code=eKey_RightArrow;break;
        default:return;
        }
        if(event->type()==QEvent::KeyPress)m_gl->m_pOp->keyManager()->press(code);else m_gl->m_pOp->keyManager()->release(code);
    } else if(event->type()==QEvent::Wheel) {
        m_gl->m_cam->enlarge(static_cast<QWheelEvent*>(event)->angleDelta().y()>0);
    } else {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(event->type()==QEvent::MouseButtonPress){m_native->setFocus();m_lastMouse=mouse->pos();if(mouse->button()==Qt::MiddleButton)m_gl->anchorCameraRotationToViewportCenter();}
        if(event->type()==QEvent::MouseMove&&(mouse->buttons()&Qt::MiddleButton)) {
            const auto delta=mouse->pos()-m_lastMouse;
            auto* sx=dynamic_cast<COptInt*>(m_gl->settings()->opt(eOptSetGeneral,"mouseSenseX"));
            auto* sy=dynamic_cast<COptInt*>(m_gl->settings()->opt(eOptSetGeneral,"mouseSenseY"));
            float x=delta.y()/(12.f-(sy?sy->value():50)/10.f),z=delta.x()/(12.f-(sx?sx->value():50)/10.f);
            m_gl->m_cam->xRotate(x);m_gl->m_cam->zRotate(z);
        }
        m_lastMouse=mouse->pos();
    }
}

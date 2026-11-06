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
#include "resource_profile.h"
#include "rebuild64_startup_guard.h"
#include "layout_components/viewport_widget.h"
#include "endurance_document.h"
#include <QActionGroup>
#include <QApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <cstring>
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
        QString gameRoot, neighbourLayout;
        quint64 generation{0};
        EirCamera camera{};
        QImage service;
        QVector<EirObjectPose> poses,documentObjects;
        quint64 serviceSerial{0};double serviceCpuMs{0};
        QSize size;
        QString capture;bool captureWorld{false};
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
        // A settled in-process DLL can also fault during an object update.
        // Retain one live-session marker without a filesystem write per frame.
        CRebuild64StartupGuard activity(scratch);
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
        void* handle=nullptr;quint64 generation=0;QSize size;QElapsedTimer pending;QStringList activePaths;QString activeGameRoot, activeLayout;
        bool injected=false,startupArmed=true,previousNeighbours=false;
        for(;;) {
            const auto frameStart=std::chrono::steady_clock::now();
            Input next;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(16),[this]{return stop || (!paused && input.generation);});
                if(stop)break;
                if(paused || !input.generation) {
                    lock.unlock();
                    if(handle){api.destroy(handle);handle=nullptr;generation=0;startup.clear();activity.clear();}
                    continue;
                }
                next=input; input.capture.clear();
            }
            EirError error{};
            if(handle&&(activePaths!=next.paths||activeGameRoot!=next.gameRoot||activeLayout!=next.neighbourLayout)){api.destroy(handle);handle=nullptr;generation=0;}
            if(!handle) {
                if(!activity.arm()){failure("Cannot arm renderer session recovery marker");break;}
                std::vector<std::wstring> paths;
                for(const auto& path:next.paths)paths.push_back(path.toStdWString());
                std::vector<EirMount> mounts;
                for(size_t i=0;i<paths.size();++i)mounts.push_back({reinterpret_cast<const uint16_t*>(paths[i].c_str()),QFileInfo(next.paths[int(i)]).isDir()?3u:0u,0});
                // The renderer can save config. Give it an isolated copy;
                // never hand its config writer the owner's Config64 path.
                const QDir game(next.gameRoot);
                const QDir config(game.filePath("Config64"));
                for(const auto& file:QStringList{"rddraw.ini","settings.ini"}) {
                    QFile source(config.filePath(file));QSaveFile copy(QDir(scratch).filePath(file=="rddraw.ini"?"renderer.ini":file));
                    if(!source.open(QIODevice::ReadOnly)||!copy.open(QIODevice::WriteOnly)) {failure(QString::fromUtf8("Не удалось прочитать конфигурацию EIRebuild64: ")+source.fileName());return;}
                    const auto bytes=source.readAll();if(copy.write(bytes)!=bytes.size()||!copy.commit()){failure("Cannot copy renderer configuration to isolated cache");return;}
                }
                QSettings local(QDir(scratch).filePath("renderer.ini"),QSettings::IniFormat);
                local.setValue("graphics/renderer_backend",0); // host device is D3D11
                local.setValue("graphics/upscaler",0); // no DLSS/NGX SDK in mapper
                local.setValue("graphics/frame_generation",0); // no Streamline
                local.setValue("graphics/reflex",0); // no NGX/Reflex integration
                local.remove("setup/nvidia_cache_id"); // process-local cache bookkeeping
                local.sync();
                const QDir work(QDir(scratch).filePath("game-work"));
                if(!QDir().mkpath(work.filePath("Config"))||!QDir().mkpath(work.filePath("Maps"))){failure("Cannot create isolated renderer work paths");return;}
                const QDir sourceConfig(QDir(resourceGameDataRoot(next.gameRoot)).filePath("Config"));
                for(const auto& region:QStringList{"gipat","ingos","suslanger"})for(const auto& prefix:QStringList{"lights","lightscave"}) {
                    const auto file=prefix+region+".ini";QFile source(sourceConfig.filePath(file));QSaveFile copy(work.filePath("Config/"+file));
                    if(!source.open(QIODevice::ReadOnly)||!copy.open(QIODevice::WriteOnly)){failure("Cannot read game light table: "+source.fileName());return;}
                    const auto bytes=source.readAll();if(copy.write(bytes)!=bytes.size()||!copy.commit()){failure("Cannot copy game light table");return;}
                }
                const auto dataRoot=work.absolutePath().toStdWString();
                const auto cache=scratch.toStdWString();
                const auto layout=next.neighbourLayout.toStdWString();
                EirCreate info{sizeof(info),EIR_ABI,uint32_t(next.size.width()),uint32_t(next.size.height()),window,mounts.data(),uint32_t(mounts.size()),hook=="no-d3d11"?1u:hook=="init"?2u:0u,reinterpret_cast<const uint16_t*>(cache.c_str()),reinterpret_cast<const uint16_t*>(layout.c_str()),reinterpret_cast<const uint16_t*>(dataRoot.c_str())};
                if(api.create(&info,&handle,&error)) {failure(QString::fromUtf8(error.message));break;}
                size=next.size;
                activePaths=next.paths;activeGameRoot=next.gameRoot;activeLayout=next.neighbourLayout;
            }
            if(generation!=next.generation) {
                if(!startup.arm()){failure(QString::fromUtf8("Не удалось записать метку загрузки Rebuild64."));break;}
                std::vector<EirSpan> mobs;
                for(const auto& bytes:next.mobs)mobs.push_back({bytes.constData(),uint64_t(bytes.size())});
                EirDocument doc{sizeof(doc),uint32_t(mobs.size()),next.generation,{next.terrain.constData(),uint64_t(next.terrain.size())},mobs.data(),next.name.constData(),next.documentObjects.constData(),uint32_t(next.documentObjects.size())};
                if(api.document(handle,&doc,&error)) {failure(QString::fromUtf8(error.message));break;}
                generation=next.generation;pending.restart();startupArmed=true;
            }
            if(next.terrain.isEmpty()) {
                api.destroy(handle);handle=nullptr;generation=0;startup.clear();
                std::lock_guard<std::mutex> lock(mutex);paused=true;continue;
            }
            if(size!=next.size) {
                if(api.resize(handle,uint32_t(next.size.width()),uint32_t(next.size.height()),&error)){failure(QString::fromUtf8(error.message));break;}
                size=next.size;
            }
            if(next.camera.neighbours_visible && !previousNeighbours && !startupArmed) {
                if(!startup.arm()){failure("Cannot arm neighbour load crash marker");break;}
                startupArmed=true;
            }
            previousNeighbours=next.camera.neighbours_visible!=0;
            if(api.service) {
                EirService service{sizeof(EirService),uint32_t(next.service.width()),uint32_t(next.service.height()),uint32_t(next.service.bytesPerLine()),next.serviceSerial,next.generation,next.serviceCpuMs,{next.service.constBits(),uint64_t(next.service.sizeInBytes())},next.poses.constData(),uint32_t(next.poses.size())};
                if(api.service(handle,&service,&error)){failure(QString::fromUtf8(error.message));break;}
            }
            if(api.camera(handle,&next.camera,&error)){failure(QString::fromUtf8(error.message));break;}
            if(hook=="device-lost"&&!injected){api.test_device_loss(handle);injected=true;}
            EirStatistics stats{};const int rc=api.frame(handle,&stats,&error);
            if(rc!=0 && rc!=1){failure(QString::fromUtf8(error.message));break;}
            if(rc==1 && pending.isValid() && pending.elapsed()>120000) {
                failure(QString::fromUtf8("Rebuild64: данные нового кадра не подготовлены за 120 секунд. Открыт OpenGL."));break;
            }
            if(!rc) {
                // Main frame is usable while the native neighbour stream runs.
                // Keep the crash marker until its async load/adoption settles.
                if(!stats.neighbours_pending){startup.clear();startupArmed=false;}
                pending.invalidate();
                QMetaObject::invokeMethod(owner,[target=owner,stats]{
                    target->m_statistics=stats;
                    if(target->m_selected && stats.ready_generation==target->m_generation) {
                        target->m_stack->setCurrentWidget(target->m_native);
                        target->movePanels(true);
                        target->syncEditorViewport();
                        emit target->frameCommitted(stats.ready_generation);
                    }
                },Qt::QueuedConnection);
                if(!next.capture.isEmpty()) {
                    const auto path=next.capture.toStdWString();
                    if((next.captureWorld?api.capture_world:api.capture)(handle,reinterpret_cast<const uint16_t*>(path.c_str()),&error))failure(QString::fromUtf8(error.message));
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
    m_glAction=viewMenu->addAction("OpenGL");m_rebuildAction=viewMenu->addAction(QString::fromUtf8("Rebuild64"));
    for(auto* action:{m_glAction,m_rebuildAction}){action->setCheckable(true);group->addAction(action);}
    m_glAction->setChecked(true);
    connect(m_glAction,&QAction::triggered,this,[this]{selectRebuild(false);});
    connect(m_rebuildAction,&QAction::triggered,this,[this]{selectRebuild(true);});
    m_native->installEventFilter(this);
    m_previewClock.start();
    m_timer->setInterval(16);connect(m_timer,&QTimer::timeout,this,&CRebuild64Viewport::tick);
    m_guardDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("rebuild64");
    m_interruptedStarts=CRebuild64StartupGuard::interrupted(m_guardDirectory);
    connect(gl,&QObject::destroyed,this,[this]{m_selected=false;m_timer->stop();m_worker.reset();m_gl=nullptr;});
    connect(gl,&CView::mobStateChanged,this,[this]{m_documentRefreshPending=true;});
    connect(gl,&CView::loadedMapStateChanged,this,[this]{m_documentRefreshPending=true;});
    connect(gl,&CView::selectionChanged,this,[this]{
        if(!m_selected)return;
        for(auto* node:m_gl->selectionSnapshot())if(node->nodeType()==ENodeType::eUnit||node->modelName().compare("stst8",Qt::CaseInsensitive)==0){
            emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: персонажи и stst8 требуют OpenGL для достоверного отображения правок. Переключите вид через меню; правки остаются в документе."));
            break;
        }
    });
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
        // Restore QGLWidget's original physical draw/query extent. Native mode
        // uses logical editor queries; it must not alter the retained GL path.
        m_gl->m_width=qMax(1,qRound(m_gl->width()*m_gl->devicePixelRatioF()));
        m_gl->m_height=qMax(1,qRound(m_gl->height()*m_gl->devicePixelRatioF()));
        m_gl->resumeRendering();m_gl->setFocus();return;
    }
    m_failure.clear();m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();
    emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: подготовка вида и инструментов редактора."));
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
    // Hour/region are editor controls; colours are read by the DLL from the
    // game's Lights[Cave]<region>.ini, never from GL's environment policy.
    result.world_time=m_previewClock.isValid()?m_previewClock.elapsed()/55.0:0.0;
    if(m_gl->m_activeMob){const auto& world=m_gl->m_activeMob->worldSet();const auto wind=util::vec3FromString(world.data(eWsTypeWindDir));for(int axis=0;axis<3;++axis)result.wind_direction[axis]=wind[axis];result.wind_force=world.data(eWsTypeWindStr).toFloat();}
    result.fog_projection[0]=.5f*(1-m_gl->m_projection(2,2));result.fog_projection[1]=1;result.fog_projection[2]=.5f*m_gl->m_projection(2,3);
    // Publish map light facts. The renderer owns attenuation and brightness. Copy live values
    // every camera tick, including preview positions; no saved-file light cache.
    uint32_t source=0;
        for(auto* mob:m_gl->m_aMob)for(auto* node:mob->nodes()) {
            ++source;if(node->nodeState()==ENodeState::eHidden)continue;
            QVector3D position,color;float radius=0;bool shadow=false;
            if(auto* light=dynamic_cast<CLight*>(node)) {
                position=(light->drawPosition().isNull()?light->position():light->drawPosition());
                color=light->lightColor();radius=light->range();shadow=light->shadowEnabled();
            } else if(auto* torch=dynamic_cast<CTorch*>(node)) {
                Q_UNUSED(torch)
                continue; // MOB -> runtime torch CLight contract is not exposed by the pinned adapter.
            }
            if(radius<=0)continue;
            if(result.point_light_count==EIR_MAX_POINT_LIGHTS)break;
            auto& light=result.point_lights[result.point_light_count++];
            for(int axis=0;axis<3;++axis){light.position[axis]=position[axis];light.color[axis]=color[axis];}
            light.radius=radius;light.casts_shadow=shadow;light.source_id=source;
        }
    result.region=m_gl->m_weatherRegion=="ingos"?2:m_gl->m_weatherRegion=="suslanger"?3:1;result.cave=m_gl->m_caveEnvironment?1:0;
    result.neighbours_visible=m_gl->m_neighborLandscapesVisible?1u:0u;
    result.visuals=31; // full world content; graphics policy comes from Config64
    return result;
}
namespace {
QVector<quintptr> editorMobOrder(CView* view) {
    QVector<quintptr> result;for(auto* mob:view->mobs())if(mob&&view->isMobVisible(mob))result.append(quintptr(mob));return result;
}
QVector<EirObjectPose> editorPoses(CView* view) {
    QVector<EirObjectPose> poses;uint32_t mobIndex=0;
    for(auto* mob:view->mobs())if(mob&&view->isMobVisible(mob)) {
        for(auto* node:mob->nodes()) {
            EirObjectPose pose{};pose.mob=mobIndex;pose.nid=node->mapId();
            const auto p=node->drawPosition();const auto q=node->rotation();const auto shape=node->constitution();
            for(int a=0;a<3;++a){pose.position[a]=p[a];pose.shape[a]=shape[a];}
            pose.shape[0]=shape.y();pose.shape[1]=shape.x();
            pose.quaternion[0]=q.scalar();pose.quaternion[1]=q.x();pose.quaternion[2]=q.y();pose.quaternion[3]=q.z();
            pose.visible=node->nodeState()!=ENodeState::eHidden&&!node->isMarkDeleted();poses.append(pose);
        }++mobIndex;
    }return poses;
}
}
void CRebuild64Viewport::tick() {
    if(!m_selected||!m_gl||m_gl->m_applicationShutdown||m_gl->m_unitVisualPreparationSuspended)return;
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
    if(m_worker && m_gl->m_pLand->filePath().fileName().toUtf8()!=m_worker->input.name) {
        movePanels(false);m_stack->setCurrentWidget(m_gl);
    }
    const auto mobOrder=editorMobOrder(m_gl);
    QByteArray visibility;
    for(auto* mob:m_gl->m_aMob)if(m_gl->isMobVisible(mob))for(auto* node:mob->nodes())
        if(node->nodeState()==ENodeState::eHidden||node->isMarkDeleted())visibility+=QByteArray::number(quintptr(mob))+":"+QByteArray::number(node->mapId())+";";
    const bool cohortChanged=m_worker&&(mobOrder!=m_documentMobOrder||visibility!=m_documentVisibility);
    // MOB ordinals belong to one immutable generation. Never apply the new
    // visible cohort's poses to the preceding generation's numeric IDs.
    if(cohortChanged){m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();}
    const bool preview=m_gl->hasPendingOperation();
    if(preview&&!m_transformPreviewMessage)emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: предпросмотр преобразования. Мир обновится после подтверждения."));
    m_transformPreviewMessage=preview;
    serviceInput();
    if(m_worker&&m_sceneSnapshotTimer.isValid()&&m_sceneSnapshotTimer.elapsed()<100) {
        const auto camera=cameraSnapshot();
        const QSize size(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
        {std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.camera=camera;m_worker->input.size=size;m_worker->paused=false;}
        return;
    }
    m_sceneSnapshotTimer.restart();
    const auto terrainState=m_gl->m_terrainEditAvailable?m_gl->m_terrainEdit.stateSha256():QByteArray();
    if(m_worker&&!cohortChanged&&(!m_documentRefreshPending || m_pointerDown || m_gl->hasPendingOperation())&&terrainState==m_terrainState) {
        const auto camera=cameraSnapshot();
        std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.camera=camera;
        m_worker->input.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
        return;
    }
    m_documentMobOrder=mobOrder;m_documentVisibility=visibility;
    m_terrainState=terrainState;m_documentRefreshPending=false;
    m_documentPoses.clear();m_poseOverrides.clear();
    for(const auto& pose:editorPoses(m_gl))m_documentPoses.insert((quint64(pose.mob)<<32)|pose.nid,pose);
    Worker::Input next;next.documentObjects=editorPoses(m_gl);
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
    hash.addData(visibility);
    const auto fingerprint=hash.result();const bool documentChanged=fingerprint!=m_documentHash;if(documentChanged){m_documentHash=fingerprint;++m_generation;}
    if(documentChanged&&m_worker)emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: обновление вида после правки; пока показан последний готовый кадр. Правки находятся в документе."));
    next.generation=m_generation;next.camera=cameraSnapshot();
    next.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
    next.paths=configuredResourceSourcePaths(m_gl->settings());
    if(const auto* root=dynamic_cast<const COptString*>(m_gl->m_pSettings->opt("gameRoot")))next.gameRoot=root->value();
    if(const auto* selected=dynamic_cast<const COptString*>(m_gl->m_pSettings->opt("resourceProfile"))) {
        for(const auto& profile:builtInResourceProfiles())if(profile.id==selected->value()) {
            const auto evaluated=evaluateResourceProfile(next.gameRoot,profile);
            if(evaluated.valid&&!evaluated.modRoot.isEmpty())
                next.neighbourLayout=QDir(evaluated.modRoot).filePath("world-map-layouts.json");
            break;
        }
    }
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
    {std::lock_guard<std::mutex> lock(m_worker->mutex);next.capture=m_worker->input.capture;next.captureWorld=m_worker->input.captureWorld;next.serviceSerial=m_worker->input.serviceSerial;next.serviceCpuMs=m_worker->input.serviceCpuMs;if(!documentChanged){next.service=m_worker->input.service;next.poses=m_worker->input.poses;}m_worker->input=std::move(next);m_worker->paused=false;}
    m_worker->wake.notify_all();
}
void CRebuild64Viewport::capture(const QString& path,bool worldOnly) {
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.capture=path;m_worker->input.captureWorld=worldOnly;}
}
bool CRebuild64Viewport::eventFilter(QObject* object,QEvent* event) {
    if(object==m_native && m_selected) {
        switch(event->type()) {
        case QEvent::ShortcutOverride:case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::MouseButtonPress:case QEvent::MouseMove:case QEvent::MouseButtonRelease:case QEvent::Wheel:case QEvent::FocusOut:case QEvent::ContextMenu:
            editorInput(event);return true;
        case QEvent::Resize:
            if(m_stack->currentWidget()==m_native)syncEditorViewport();
            QTimer::singleShot(0,this,&CRebuild64Viewport::tick);break;
        default:break;
        }
    }
    return QWidget::eventFilter(object,event);
}
void CRebuild64Viewport::editorInput(QEvent* event) {
    if(event->type()==QEvent::FocusOut){m_gl->m_pOp->keyManager()->keys().clear();return;}
    if((event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)
       && m_gl->m_landscapeTools && m_gl->m_landscapeTools->editingEnabled()) {
        const auto* key=static_cast<QKeyEvent*>(event);
        const auto cameraKeys=QVector<int>{Qt::Key_W,Qt::Key_A,Qt::Key_S,Qt::Key_D,Qt::Key_Q,Qt::Key_E,Qt::Key_Up,Qt::Key_Down,Qt::Key_Left,Qt::Key_Right,Qt::Key_Shift,Qt::Key_Escape};
        if(!cameraKeys.contains(key->key())&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier))) {
            if(event->type()==QEvent::KeyPress)emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: инструменты рельефа требуют OpenGL."));
            event->accept();return;
        }
    }
    const bool mouse=event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease;
    if(mouse) {
        auto* e=static_cast<QMouseEvent*>(event);
        if(event->type()==QEvent::MouseButtonPress&&e->button()==Qt::LeftButton
           &&!m_gl->hasPendingOperation()
           &&(m_documentRefreshPending||m_statistics.ready_generation!=m_generation)) {
            emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: кадр после правки ещё обновляется. Дождитесь готового кадра перед новым выбором мышью; Undo и сохранение доступны."));
            event->accept();return;
        }
        const bool terrain=m_gl->m_landscapeTools && m_gl->m_landscapeTools->editingEnabled();
        if(terrain && (e->button()!=Qt::MiddleButton && !(e->buttons()&Qt::MiddleButton))) {
            if(event->type()==QEvent::MouseButtonPress)
                emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: кисти рельефа требуют OpenGL. Выберите OpenGL через меню вида; текущие правки сохранены в документе."));
            event->accept();return;
        }
        if(event->type()==QEvent::MouseButtonPress)m_pointerDown=true;
        if(event->type()==QEvent::MouseButtonRelease)m_pointerDown=e->buttons()!=Qt::NoButton;
    }
    // Native surface and hidden CView share one logical rectangle. Deliver the
    // original event (native key/modifiers included) to the canonical Qt widget.
    syncEditorViewport();
    QApplication::sendEvent(m_gl,event);
    if(mouse && event->type()==QEvent::MouseButtonPress)m_native->setFocus(Qt::MouseFocusReason);
}
void CRebuild64Viewport::serviceInput() {
    if(!m_worker || m_stack->currentWidget()!=m_native)return;
    if(editorMobOrder(m_gl)!=m_documentMobOrder)return;
    syncEditorViewport();
    // Advance the single editor camera once, before both consumers.
    m_gl->m_cam->update();
    QElapsedTimer timer;timer.start();QString error;
    const QSize size(qMax(1,qRound(m_native->width()*devicePixelRatioF())),qMax(1,qRound(m_native->height()*devicePixelRatioF())));
    auto image=m_gl->captureEditorServiceLayer(size,&error).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    m_serviceMilliseconds=timer.nsecsElapsed()/1e6;
    if(image.isNull()){emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: служебный слой недоступен: ")+error);return;}
    m_serviceImage=image;
    auto poses=editorPoses(m_gl);
    for(auto& pose:poses) {
        const auto key=(quint64(pose.mob)<<32)|pose.nid;
        const auto baseline=m_documentPoses.constFind(key);
        if(baseline!=m_documentPoses.cend() &&
           (std::memcmp(pose.position,baseline->position,sizeof(pose.position)) || std::memcmp(pose.quaternion,baseline->quaternion,sizeof(pose.quaternion)) || std::memcmp(pose.shape,baseline->shape,sizeof(pose.shape))))m_poseOverrides.insert(key);
        pose.changed=m_poseOverrides.contains(key)?1u:0u;
    }
    std::lock_guard<std::mutex> lock(m_worker->mutex);
    m_worker->input.camera=cameraSnapshot();m_worker->input.size=size;
    m_worker->input.serviceCpuMs=m_serviceMilliseconds;m_worker->input.service=image;m_worker->input.poses=std::move(poses);++m_worker->input.serviceSerial;
}

void CRebuild64Viewport::syncEditorViewport() {
    const QSize size=m_native->size();if(size.isEmpty())return;
    const auto projection=m_gl->m_projection,normal=m_gl->m_normalProjection;
    const double before=m_gl->m_height>0?double(m_gl->m_width)/m_gl->m_height:0;
    const double after=double(size.width())/size.height();
    m_gl->resize(size);
    // QGLWidget reports physical dimensions to resizeGL on Windows. Editor
    // events and query rectangles are logical. A DPR-only change must not
    // rebuild the shared lens (including the current far/telephoto projection).
    if(qAbs(before-after)>.000001)m_gl->resizeGL(size.width(),size.height());
    else {m_gl->m_projection=projection;m_gl->m_normalProjection=normal;}
    m_gl->m_width=size.width();m_gl->m_height=size.height();
}

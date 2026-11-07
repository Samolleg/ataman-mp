#include "rebuild64_viewport_widget.h"
#include "view.h"
#include "opening_trace.h"
#include "legacy_projection_pulse.h"
#include "mob_family_validation.h"
#include <QtConcurrent>
#include "main_window.h"
#include "renderer_preferences.h"
#include "resource_write_lease.h"
#include <QEventLoop>
#include <QThread>
#include <QProgressDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QTemporaryDir>
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
#include <QFocusEvent>
#include <QResizeEvent>
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

namespace {
struct EarlyWorld {QByteArray terrain,name;QVector<QByteArray> mobs;QString error;CWorldSet world;bool hasWorld{false};QVector<EirPointLight> lights;};
struct EarlyRead {QFuture<EarlyWorld> future;std::shared_ptr<std::atomic_bool> cancel{std::make_shared<std::atomic_bool>(false)};};
EarlyWorld readOpening(const QFileInfo& mpr,const QStringList& roots,const QStringList& search,const std::shared_ptr<std::atomic_bool>& cancel) {
    opening::Scope total("cpu.cohort",mpr.fileName());EarlyWorld world;world.name=mpr.fileName().toUtf8();
    {opening::Scope timing("cpu.mpr.read",mpr.fileName());QFile file(mpr.absoluteFilePath());
        if(!file.open(QIODevice::ReadOnly)){world.error=file.errorString();return world;}world.terrain=file.readAll();}
    QStringList queue=roots;QSet<QString> seen;uint32_t lightSource=0;
    while(!queue.isEmpty()&&!cancel->load()) {
        const QFileInfo source(queue.takeFirst());const auto key=source.canonicalFilePath().toCaseFolded();
        if(key.isEmpty()){world.error="MOB disappeared: "+source.absoluteFilePath();return world;}
        if(seen.contains(key))continue;seen.insert(key);
        CMobDocument document;CValidationReport report;
        {opening::Scope timing("cpu.mob.read_parse",source.fileName());
            if(!document.load(source,&report)||report.hasErrors()){world.error=report.toText();return world;}}
        if(world.mobs.isEmpty()) {SMobParameterState parameters;QString error;
            opening::Scope timing("cpu.mob.environment",source.fileName());
            if(!CMob::readTemplateParameters(source,&parameters,&error)){world.error=error;return world;}
            world.world=parameters.worldSet;world.hasWorld=true;
        }
        // Copy authored light facts from the CPU document. Constructing CLight
        // here would request GL resources; no editor objects exist on this worker.
        for(const auto& group:document.sections().front().children)if(group.type==45056u)
            for(const auto& object:group.children) {
                ++lightSource;if(object.type!=43521u||world.lights.size()>=EIR_MAX_POINT_LIGHTS)continue;
                EirPointLight light{};light.radius=10;light.source_id=lightSource;
                for(int i=0;i<3;++i)light.color[i]=1;
                for(const auto& field:object.children) {
                    QDataStream data(field.rawBytes.mid(8));util::formatStream(data);
                    if(field.type==43522u)data>>light.radius;
                    else if(field.type==43524u)for(int i=0;i<3;++i)data>>light.position[i];
                    else if(field.type==43527u)for(int i=0;i<3;++i)data>>light.color[i];
                    else if(field.type==43526u){quint8 shadow=0;data>>shadow;light.casts_shadow=shadow?1u:0u;}
                }
                if(light.radius>0)world.lights.append(light);
            }
        world.mobs.append(document.originalBytes());QString script;
        if(!document.embeddedScript(&script,&report)){world.error=report.toText();return world;}
        {opening::Scope timing("cpu.addmob.resolve",source.fileName());QStringList warnings;
            queue.append(CMobFamilyValidator::addMobPaths(script,source.absoluteFilePath(),search,&warnings));
            // Resolution uses the same validator as CView's background cohort.
            // Missing optional AddMob paths are handled by that contract, not guessed here.
        }
    }
    if(cancel->load())world.error="cancelled";return world;
}
}
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
        quint64 serviceSerial{0},captureGeneration{0};double serviceCpuMs{0};
        QSize size;
        QString capture;bool captureWorld{false},documentDirty{false},operationPreview{false},editorGeometryReady{false};
        bool resourcesReady{false},overrideHour{false},openingClock{false};
        std::chrono::steady_clock::time_point cameraStamp{std::chrono::steady_clock::now()};
        std::shared_ptr<EarlyRead> opening;
        void resources(CSettings* settings,const QSize& extent) {
            size=extent;paths=configuredResourceSourcePaths(settings);
            if(const auto* root=dynamic_cast<const COptString*>(settings->opt("gameRoot")))gameRoot=root->value();
            if(const auto* selected=dynamic_cast<const COptString*>(settings->opt("resourceProfile")))
                for(const auto& profile:loadResourceProfiles())if(profile.id==selected->value()) {
                    const auto evaluated=evaluateResourceProfile(gameRoot,profile);
                    if(evaluated.valid&&!evaluated.modRoot.isEmpty())neighbourLayout=QDir(evaluated.modRoot).filePath("world-map-layouts.json");
                    break;
                }
            resourcesReady=!gameRoot.isEmpty();
        }
    } input;
    CRebuild64Viewport* owner;
    void* window;
    QTemporaryDir sessionDirectory;
    QString scratch,guardDirectory;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> loaded{false},prepared{false},resourcesRetired{true},finished{false};
    bool stop{false},paused{false},retainPausedHandle{false};
    QString lastPhase; // Construct every worker field before starting its thread.
    std::thread thread;
    Worker(CRebuild64Viewport* target,void* hwnd,const QString& cache)
        :owner(target),window(hwnd),sessionDirectory(QDir(cache).filePath("session-%1-XXXXXX").arg(QCoreApplication::applicationPid())),scratch(sessionDirectory.path()),guardDirectory(cache),thread([this]{run();}) {}
    ~Worker() {
        { std::lock_guard<std::mutex> lock(mutex); stop=true; }
        wake.notify_all();
        if(thread.joinable())thread.join();
    }
    void pause(bool value) {
        {std::lock_guard<std::mutex> lock(mutex);paused=value;retainPausedHandle=false;} wake.notify_all();
    }
    void phase(const QString& text) {
        if(lastPhase==text)return;
        lastPhase=text;
        QMetaObject::invokeMethod(owner,[target=owner,text]{
            if(target->m_stopping)return;target->m_preparationPhase=text;target->updatePreparationText();
        },Qt::QueuedConnection);
    }
    void failure(const QString& detail) {
        QMetaObject::invokeMethod(owner,[target=owner,detail]{if(!target->m_stopping)target->fallback(detail);},Qt::QueuedConnection);
    }
    static uint32_t EIR_CALL canPresent(void* context,uint64_t generation,uint32_t neighbours,uint32_t phase) {
        auto& worker=*static_cast<Worker*>(context);
        if(phase==1){worker.mutex.unlock();return 1;}
        worker.mutex.lock();
        const bool current=!worker.stop&&!worker.paused&&worker.input.generation==generation
            &&worker.input.camera.neighbours_visible==neighbours
            &&(!worker.input.documentDirty||worker.input.operationPreview)
            ; // Native world readiness is independent of editor query geometry.
        if(phase==2||!current)worker.mutex.unlock();
        return current;
    }
    void run() {
        struct Completion {std::atomic<bool>& value;~Completion(){value=true;}} completion{finished};
        if(scratch.isEmpty()){failure(QString::fromUtf8("Не удалось создать отдельный кеш сеанса Rebuild64."));return;}
        CRebuild64StartupGuard startup(guardDirectory);
        // A settled in-process DLL can also fault during an object update.
        // Retain one live-session marker without a filesystem write per frame.
        CRebuild64StartupGuard activity(guardDirectory);
        phase(QString::fromUtf8("Загрузка модуля Rebuild64"));
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
                wake.wait_for(lock,std::chrono::milliseconds(16),[this]{return stop || (!paused && input.resourcesReady);});
                if(stop)break;
                if(paused || !input.resourcesReady) {
                    const bool retire=!retainPausedHandle;
                    lock.unlock();
                    if(handle&&retire){api.destroy(handle);handle=nullptr;generation=0;prepared=false;startup.clear();activity.clear();}
                    resourcesRetired=!handle;
                    continue;
                }
                next=input; input.capture.clear();
            }
            EirError error{};
            if(next.terrain.isEmpty() && !next.opening) {
                // There is no world to prepare. A device created for the empty
                // profile would be discarded when map-local mounts/layout arrive.
                // Closing a document also retires its device instead of submitting
                // an empty MPR into the pending-frame/120-second fallback loop.
                if(!prepared.load()||activeGameRoot!=next.gameRoot) {
                    for(const auto& file:QStringList{"rddraw.ini","settings.ini"}) {
                        QFile config(QDir(next.gameRoot).filePath("Config64/"+file));
                        if(!config.open(QIODevice::ReadOnly)) {
                            failure(QString::fromUtf8("Не удалось прочитать конфигурацию EIRebuild64: ")+config.fileName());return;
                        }
                    }
                    activeGameRoot=next.gameRoot;
                }
                if(handle){api.destroy(handle);handle=nullptr;generation=0;}
                resourcesRetired=true;prepared=true;pending.invalidate();
                startup.clear();activity.clear();
                phase(QString::fromUtf8("Готов к открытию карты"));
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(100),[this]{return stop||paused||!input.terrain.isEmpty();});
                continue;
            }
            if(handle&&(activePaths!=next.paths||activeGameRoot!=next.gameRoot||activeLayout!=next.neighbourLayout)){prepared=false;api.destroy(handle);handle=nullptr;generation=0;}
            {std::lock_guard<std::mutex> lock(mutex);if(stop)break;if(paused||input.generation!=next.generation)continue;}
            if(!handle) {
                resourcesRetired=false;prepared=false;
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
                EirCreate info{sizeof(info),EIR_ABI,uint32_t(next.size.width()),uint32_t(next.size.height()),window,mounts.data(),uint32_t(mounts.size()),hook=="no-d3d11"?1u:hook=="init"?2u:hook=="shader-hold"?3u:0u,reinterpret_cast<const uint16_t*>(cache.c_str()),reinterpret_cast<const uint16_t*>(layout.c_str()),reinterpret_cast<const uint16_t*>(dataRoot.c_str()),&Worker::canPresent,this};
                phase(QString::fromUtf8("Создание D3D11 и подготовка шейдеров"));
                opening::Scope deviceTiming("native.create");
                if(api.create(&info,&handle,&error)) {failure(QString::fromUtf8(error.message));break;}
                prepared=true;size=next.size;
                activePaths=next.paths;activeGameRoot=next.gameRoot;activeLayout=next.neighbourLayout;
            }
            if(next.opening) {
                if(next.opening->cancel->load())continue;
                if(!next.opening->future.isFinished()) {
                    std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(10));continue;
                }
                const auto raw=next.opening->future.result();
                if(!raw.error.isEmpty()){failure(QString::fromUtf8("Не удалось подготовить полный мир Rebuild64: ")+raw.error);break;}
                next.terrain=raw.terrain;next.name=raw.name;next.mobs=raw.mobs;
                if(raw.hasWorld) {
                    if(!next.overrideHour)next.camera.hour=ei::renderEnvironment(&raw.world,false,float(next.camera.hour),next.camera.cave!=0).timeHours;
                    const auto wind=util::vec3FromString(raw.world.data(eWsTypeWindDir));
                    for(int i=0;i<3;++i)next.camera.wind_direction[i]=wind[i];next.camera.wind_force=raw.world.data(eWsTypeWindStr).toFloat();
                }
                next.camera.point_light_count=uint32_t(raw.lights.size());
                for(int i=0;i<raw.lights.size();++i)next.camera.point_lights[i]=raw.lights[i];
                // Publish the immutable raw cohort under the same generation guard.
                {std::lock_guard<std::mutex> lock(mutex);if(stop||paused||input.generation!=next.generation)continue;
                    input.terrain=next.terrain;input.name=next.name;input.mobs=next.mobs;input.camera=next.camera;input.opening.reset();}
                QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(next.name);hash.addData(next.terrain);
                for(const auto& mob:next.mobs){hash.addData(QByteArray::number(mob.size()));hash.addData(mob);}
                const auto fingerprint=hash.result();const auto ticket=next.generation;
                QMetaObject::invokeMethod(owner,[target=owner,fingerprint,ticket]{if(target->m_generation==ticket&&target->m_earlyDocument)target->m_documentHash=fingerprint;},Qt::QueuedConnection);
                next.opening.reset();
            }
            {std::lock_guard<std::mutex> lock(mutex);if(stop)break;if(paused||input.generation!=next.generation)continue;}
            if(!next.generation) {
                startup.clear();prepared=true;phase(QString::fromUtf8("Готов к открытию карты"));
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(100),[this]{return stop||paused||input.generation;});
                continue;
            }
            if(handle&&generation&&next.camera.neighbours_visible!=uint32_t(previousNeighbours)) {
                // Re-adopt before a live visibility transition can invalidate
                // material registrations. The host applies the requested flag
                // before publishing the replacement world on this display.
                generation=0;
                QMetaObject::invokeMethod(owner,[target=owner]{if(target->m_gl)emit target->m_gl->updateMsg(QString::fromUtf8("Rebuild64: обновление соседних карт; показан последний готовый кадр."));},Qt::QueuedConnection);
            }
            if(generation!=next.generation) {
                if(!startup.arm()){failure(QString::fromUtf8("Не удалось записать метку загрузки Rebuild64."));break;}
                std::vector<EirSpan> mobs;
                for(const auto& bytes:next.mobs)mobs.push_back({bytes.constData(),uint64_t(bytes.size())});
                EirDocument doc{sizeof(doc),uint32_t(mobs.size()),next.generation,{next.terrain.constData(),uint64_t(next.terrain.size())},mobs.data(),next.name.constData(),next.documentObjects.constData(),uint32_t(next.documentObjects.size())};
                // Supply visibility before document adoption. Applying the INI
                // default and changing it after world upload retires materials.
                if(api.camera(handle,&next.camera,&error)) {failure(QString::fromUtf8(error.message));break;}
                phase(QString::fromUtf8("Подготовка карты и материалов"));
                opening::Scope documentTiming("native.document");
                if(api.document(handle,&doc,&error)) {failure(QString::fromUtf8(error.message));break;}
                generation=next.generation;previousNeighbours=next.camera.neighbours_visible!=0;pending.restart();startupArmed=true;
                // Document decode may take seconds. Do not render a retired
                // request or reuse the camera/service tuple from before it.
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(stop||paused||input.generation!=generation)continue;
                    if(input.camera.neighbours_visible!=next.camera.neighbours_visible){generation=0;continue;}
                    next.camera=input.camera;next.size=input.size;
                    next.service=input.service;next.poses=input.poses;
                    next.serviceSerial=input.serviceSerial;next.serviceCpuMs=input.serviceCpuMs;
                }
            }
            if(next.terrain.isEmpty()) {
                startup.clear();startupArmed=false;pending.invalidate();prepared=true;
                phase(QString::fromUtf8("Готов к открытию карты"));
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop||paused||input.generation!=generation;});
                continue;
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
            if(next.openingClock)next.camera.world_time+=std::chrono::duration<double>(std::chrono::steady_clock::now()-next.cameraStamp).count()/.055;
            if(api.camera(handle,&next.camera,&error)){failure(QString::fromUtf8(error.message));break;}
            if(hook=="device-lost"&&!injected){api.test_device_loss(handle);injected=true;}
            EirStatistics stats{};const int rc=api.frame(handle,&stats,&error);
            if(rc!=0 && rc!=1){failure(QString::fromUtf8(error.message));break;}
            if(rc==1)phase(QString::fromUtf8("Подготовка кадра и соседних карт"));
            if(rc==1 && pending.isValid() && pending.elapsed()>120000) {
                failure(QString::fromUtf8("Rebuild64: данные нового кадра не подготовлены за 120 секунд. Открыт OpenGL."));break;
            }
            if(!rc) {
                prepared=true;
                // Main frame is usable while the native neighbour stream runs.
                // Keep the crash marker until its async load/adoption settles.
                if(!stats.neighbours_pending){startup.clear();startupArmed=false;}
                pending.invalidate();
                QMetaObject::invokeMethod(owner,[target=owner,stats]{
                    if(target->m_stopping||stats.ready_generation!=target->m_generation)return;
                    target->m_statistics=stats;
                    if(target->m_selected && !target->m_documentRefreshPending && stats.ready_generation==target->m_generation) {
                        const bool firstShow=target->m_stack->currentWidget()!=target->m_native;
                        target->m_stack->setCurrentWidget(target->m_native);
                        target->movePanels(true);
                        if(firstShow&&!QApplication::activeModalWidget())target->m_native->setFocus(Qt::OtherFocusReason);
                        target->syncEditorViewport();
                        emit target->frameCommitted(stats.ready_generation);
                    }
                },Qt::QueuedConnection);
                if(!next.capture.isEmpty()) {
                    const auto path=next.capture.toStdWString();
                    const auto captureError=next.captureGeneration!=generation?QString::fromUtf8("Документ изменился до получения кадра Rebuild64. Повторите window_snapshot."):(next.captureWorld?api.capture_world:api.capture)(handle,reinterpret_cast<const uint16_t*>(path.c_str()),&error)?QString::fromUtf8(error.message):QString();
                    QMetaObject::invokeMethod(owner,[target=owner,path=next.capture,captureError,generation]{emit target->captureFinished(path,captureError,generation);},Qt::QueuedConnection);
                }
            }
            // No busy loop when the condition predicate already has input.
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_until(lock,frameStart+std::chrono::milliseconds(16),[this]{return stop||paused;});
        }
        if(handle)api.destroy(handle);
        resourcesRetired=true;loaded=false;
    }
};

CRebuild64Viewport::CRebuild64Viewport(CView* gl,QMenu* menu,QWidget* parent)
    :QWidget(parent),m_gl(gl),m_native(new CRebuild64NativeSurface(this)),m_preparation(new QWidget(this)),m_preparationText(new QLabel(m_preparation)),m_stack(new QStackedLayout(this)),m_timer(new QTimer(this)) {
    setObjectName("rebuild64Viewport");m_native->setObjectName("rebuild64NativeSurface");
    m_preparation->setObjectName("rebuild64Preparation");
    m_preparation->setAutoFillBackground(true);
    auto* stateLayout=new QVBoxLayout(m_preparation);stateLayout->addStretch();stateLayout->addWidget(m_preparationText);stateLayout->addStretch();
    m_preparationText->setAlignment(Qt::AlignCenter);m_preparationText->setWordWrap(true);
    m_stack->setContentsMargins(0,0,0,0);m_stack->setSpacing(0);m_stack->addWidget(gl);m_stack->addWidget(m_native);m_stack->addWidget(m_preparation);m_stack->setCurrentWidget(m_preparation);
    connect(gl,&CView::legacyViewportRequested,this,&CRebuild64Viewport::requireOpenGL);
    auto* viewMenu=menu->addMenu(QString::fromUtf8("Вид карты"));
    auto* group=new QActionGroup(this);group->setExclusive(true);
    m_glAction=viewMenu->addAction("OpenGL");m_rebuildAction=viewMenu->addAction(QString::fromUtf8("Rebuild64"));
    for(auto* action:{m_glAction,m_rebuildAction}){action->setCheckable(true);group->addAction(action);}
    m_glAction->setChecked(true);
    connect(m_glAction,&QAction::triggered,this,[this]{selectRebuild(false);});
    connect(m_rebuildAction,&QAction::triggered,this,[this]{selectRebuild(true);});
    m_native->installEventFilter(this);m_gl->installEventFilter(this);
    m_previewClock.start();
    m_timer->setInterval(16);connect(m_timer,&QTimer::timeout,this,&CRebuild64Viewport::tick);
    m_guardDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("rebuild64");
    m_interruptedStarts=CRebuild64StartupGuard::interrupted(m_guardDirectory);
    connect(gl,&QObject::destroyed,this,[this]{m_selected=false;m_timer->stop();m_worker.reset();m_gl=nullptr;});
    connect(gl,&CView::mobStateChanged,this,[this]{if(m_earlyDocument)return;m_documentRefreshPending=true;if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.documentDirty=true;}});
    connect(gl,&CView::loadedMapStateChanged,this,[this]{
        if(m_earlyDocument && m_gl->m_pLand && m_gl->m_pLand->isMprLoad()
           &&m_earlyMap==m_gl->m_pLand->filePath().absoluteFilePath())return;
        m_documentRefreshPending=true;
        if(m_selected && (!m_gl->m_pLand || !m_gl->m_pLand->isMprLoad()
           || m_documentMapPath!=m_gl->m_pLand->filePath().absoluteFilePath()))beginDocumentTransition();
        if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.documentDirty=true;}
    });
    connect(gl,&CView::selectionChanged,this,[this]{
        if(!m_selected)return;
        for(auto* node:m_gl->selectionSnapshot())if(node->nodeType()==ENodeType::eUnit||node->modelName().compare("stst8",Qt::CaseInsensitive)==0){
            requireOpenGL(QString::fromUtf8("Персонажи, appearance и stst8"));
            break;
        }
    });
    const QPointer<CRebuild64Viewport> weak(this);
    CResourceWriteLease::setAcquirer([weak](const QStringList&,QString* error)->std::shared_ptr<void>{
        if(!weak)return {};
        bool acquired=false;QString diagnostic;
        const auto acquire=[&]{if(weak)acquired=weak->beginResourceWrite(&diagnostic);};
        if(QThread::currentThread()==weak->thread())acquire();
        else QMetaObject::invokeMethod(weak,acquire,Qt::BlockingQueuedConnection);
        if(!acquired){if(error)*error=diagnostic;return {};}
        return std::shared_ptr<void>(new int(0),[weak](void* token){delete static_cast<int*>(token);if(!weak)return;
            if(QThread::currentThread()==weak->thread())weak->endResourceWrite();
            else QMetaObject::invokeMethod(weak,[weak]{if(weak)weak->endResourceWrite();},Qt::BlockingQueuedConnection);
        });
    });
    QSettings preferences;
    const bool requested=atamanRebuild64Preference(preferences,qApp->property("agentHeadless").toBool());
    const bool latched=QFileInfo::exists(QDir(m_guardDirectory).filePath("manual-retry-required"));
    const bool interrupted=!m_interruptedStarts.isEmpty();
    if(interrupted) {
        QSaveFile latch(QDir(m_guardDirectory).filePath("manual-retry-required"));
        const bool recorded=latch.open(QIODevice::WriteOnly)
            &&latch.write("Rebuild64 requires an explicit menu selection\n")>0&&latch.commit();
        if(recorded){for(const auto& path:m_interruptedStarts)QFile::remove(path);m_interruptedStarts.clear();}
    }
    // Select before MainWindow can show. Migration does not acknowledge a guard.
    selectRebuild(requested&&!latched&&!interrupted,false);
    if(latched||interrupted)QTimer::singleShot(0,this,[this]{
        emit m_gl->updateMsg(tr("Предыдущая загрузка Rebuild64 завершилась аварийно. Открыт OpenGL. Для повторного запуска выберите Rebuild64 в Настройках → Вид карты."));
    });
}
CRebuild64Viewport::~CRebuild64Viewport(){CResourceWriteLease::setAcquirer({});finishApplicationShutdown();movePanels(false);}
void CRebuild64Viewport::movePanels(bool native) {
    if(!m_gl)return;
    if(native) {
        for(auto* child:m_gl->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
            if(dynamic_cast<CViewportWidget*>(child)&&!m_panels.contains(child))m_panels.append(child);
    }
    for(const auto& pointer:m_panels) {
        auto* panel=pointer.data();if(!panel)continue;
        QWidget* parent=native?(m_stack->currentWidget()==m_preparation?m_preparation:static_cast<QWidget*>(m_native)):static_cast<QWidget*>(m_gl);
        if(panel->parentWidget()==parent)continue;
        const bool shown=!panel->isHidden();const auto geometry=panel->geometry();
        panel->setParent(parent);
        // These controls already own an opaque backing rectangle in OpenGL.
        // Native child HWNDs keep the same paint/input path above D3D, without
        // a transparent top-level window, screen capture or coordinate scaling.
        panel->setAttribute(Qt::WA_NativeWindow,native&&parent==m_native);
        panel->setGeometry(geometry);panel->setVisible(shown);panel->raise();
    }
    if(!native)m_panels.clear();
}
bool CRebuild64Viewport::backendLoaded() const {return m_worker&&m_worker->loaded.load();}
bool CRebuild64Viewport::backendPrepared() const {return m_worker&&m_worker->prepared.load();}
bool CRebuild64Viewport::backendResourcesRetired() const {return !m_worker||m_worker->resourcesRetired.load();}
void CRebuild64Viewport::stopForApplicationShutdown() {
    if(m_stopping)return;m_stopping=true;m_selected=false;m_timer->stop();
    if(m_gl){m_gl->suspendRendering();m_gl->setFocusProxy(nullptr);}movePanels(false);
    if(m_worker){{std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->stop=true;}m_worker->wake.notify_all();}
}
bool CRebuild64Viewport::applicationShutdownFinished() const {return !m_worker||m_worker->finished.load();}
void CRebuild64Viewport::finishApplicationShutdown(){stopForApplicationShutdown();m_worker.reset();}

void CRebuild64Viewport::prepareBackend() {
    if(m_resourceWriteDepth || !m_selected || !m_gl || !m_gl->settings() || (m_gl->m_pLand&&m_gl->m_pLand->isMprLoad()))return;
    // Empty-window ticks must not enumerate resource archives and re-evaluate
    // profiles sixty times per second. Settings values are GUI-owned and cheap;
    // document/resource transitions explicitly invalidate this preparation key.
    QByteArray resourceKey;
    for(const auto& name:{"gameRoot","resourceProfile"})if(const auto* option=dynamic_cast<const COptString*>(m_gl->settings()->opt(name)))resourceKey+=option->value().toUtf8()+'\0';
    for(const auto& name:{"resourceRoots","mapRoots"})if(const auto* option=dynamic_cast<const COptStringList*>(m_gl->settings()->opt(eOptSetResource,name)))for(const auto& path:option->value())resourceKey+=path.toUtf8()+'\0';
    resourceKey=QCryptographicHash::hash(resourceKey,QCryptographicHash::Sha256);
    if(m_backendResourceKey==resourceKey)return;
    m_statistics={};
    m_backendResourceKey=resourceKey;++m_backendResourcePreparations;
    Worker::Input next;
    next.resources(m_gl->settings(),QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF()))));
    if(!next.resourcesReady)return; // first-run setup has not selected a game yet
    if(!m_worker){QDir().mkpath(m_guardDirectory);m_worker.reset(new Worker(this,reinterpret_cast<void*>(m_native->winId()),m_guardDirectory));}
    {
        std::lock_guard<std::mutex> lock(m_worker->mutex);
        const auto& old=m_worker->input;
        if(old.terrain.isEmpty() && !m_worker->paused && old.paths==next.paths && old.gameRoot==next.gameRoot && old.neighbourLayout==next.neighbourLayout)return;
        next.generation=old.terrain.isEmpty()?old.generation:++m_generation;
        m_worker->input=std::move(next);m_worker->paused=false;m_worker->retainPausedHandle=false;
    }
    m_worker->wake.notify_all();
}

void CRebuild64Viewport::selectRebuild(bool selected,bool persist) {
    if(!m_gl||m_stopping)return;
    if(selected && qApp->property("agentHeadless").toBool())return;
    if(selected)for(auto* node:m_gl->selectionSnapshot())if(node->nodeType()==ENodeType::eUnit||node->modelName().compare("stst8",Qt::CaseInsensitive)==0){
        if(m_selected)selectRebuild(false,false);m_glAction->setChecked(true);m_rebuildAction->setChecked(false);
        emit m_gl->updateMsg(tr("Выделенные персонажи и stst8 пока требуют OpenGL. Выделение и правки сохранены; снимите выделение для перехода в Rebuild64."));return;
    }

    if(selected&&m_worker&&m_worker->finished.load()){m_worker.reset();m_restartWhenFinished=false;}
    else if(selected&&m_worker&&!m_failure.isEmpty())m_restartWhenFinished=true;
    if(!selected)m_restartWhenFinished=false;
    m_gl->setFocusProxy(selected?m_native:nullptr);
    m_pointerDown=false;
    m_selected=selected;m_gl->setProperty("rebuild64Selected",selected);
    m_gl->setProperty("nativeLegacyVisualState",m_gl->legacyVisualState());
    m_glAction->setChecked(!selected);m_rebuildAction->setChecked(selected);
    if(persist)QSettings().setValue("ui/renderer",selected?"Rebuild64":"OpenGL");
    if(persist) {
        for(const auto& path:m_interruptedStarts)QFile::remove(path);
        m_interruptedStarts.clear();
        QFile::remove(QDir(m_guardDirectory).filePath("manual-retry-required"));
    }
    movePanels(false);
    if(!selected) {
        m_stack->setCurrentWidget(m_gl);
        if(m_waitingInput.isEmpty())m_timer->stop();else m_timer->start();if(m_worker)m_worker->pause(true);
        // Restore QGLWidget's original physical draw/query extent. Native mode
        // uses logical editor queries; it must not alter the retained GL path.
        m_gl->m_width=qMax(1,qRound(m_gl->width()*m_gl->devicePixelRatioF()));
        m_gl->m_height=qMax(1,qRound(m_gl->height()*m_gl->devicePixelRatioF()));
        // The constructor runs before CView::attach. Lazy resources are safe
        // only after that production attachment has completed.
        if(m_gl->settings()) {
            m_gl->ensureLegacyQueryContext();m_gl->ensureLegacyWorldResources();
            if(m_gl->m_pLand && m_gl->m_pLand->isMprLoad())m_gl->loadNeighborLandscapes();
        }
        m_gl->resumeRendering();m_gl->setFocus();emit rendererSelectionChanged(false);return;
    }
    m_gl->suspendRendering();
    m_failure.clear();m_backendResourceKey.clear();m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();
    showPreparation(tr("Откройте карту через меню Файл, недавние документы или карту мира."));
    emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: подготовка вида и инструментов. Параметры изображения OpenGL действуют в OpenGL; неподдержанные инструменты переключат вид с сообщением."));
    m_timer->start();tick();emit rendererSelectionChanged(true);
}
bool CRebuild64Viewport::beginResourceWrite(QString* error) {
    if(m_stopping){if(error)*error=tr("Редактор завершает работу; новая запись ресурсов отменена.");return false;}
    ++m_resourceWriteDepth;
    if(!m_worker || m_worker->resourcesRetired.load())return true;
    if(m_selected)beginDocumentTransition();
    m_worker->pause(true);
    if(m_selected)showPreparation(tr("Освобождение архивов перед сохранением"));
    QProgressDialog progress(tr("Rebuild64 освобождает архивы перед сохранением. Окно работает."),QString(),0,0,window());
    progress.setCancelButton(nullptr);progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);progress.show();
    QElapsedTimer elapsed;elapsed.start();QEventLoop loop;QTimer timer;
    connect(&timer,&QTimer::timeout,&loop,[&]{if(!m_worker||m_worker->resourcesRetired.load()||elapsed.elapsed()>=120000)loop.quit();});timer.start(10);
    if(m_worker&&!m_worker->resourcesRetired.load())loop.exec(QEventLoop::ExcludeUserInputEvents);
    if(m_worker&&!m_worker->resourcesRetired.load()) {
        if(error)*error=tr("Rebuild64 не освободил архивы за 120 секунд. Сохранение отменено; документы и правки сохранены в памяти.");
        endResourceWrite();return false;
    }
    return true;
}
void CRebuild64Viewport::endResourceWrite() {
    if(m_resourceWriteDepth>0)--m_resourceWriteDepth;
    if(!m_resourceWriteDepth&&m_selected){m_backendResourceKey.clear();m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();}
}
void CRebuild64Viewport::showPreparation(const QString& phase) {
    m_preparationPhase=phase;m_preparationClock.restart();
    m_stack->setCurrentWidget(m_preparation);
    movePanels(true);updatePreparationText();
}
void CRebuild64Viewport::updatePreparationText() {
    const bool map=m_gl&&m_gl->m_pLand&&m_gl->m_pLand->isMprLoad();
    m_preparationText->setText(map
        ?tr("Rebuild64 — %1\nПрошло %2 с. Окно работает. Можно выбрать OpenGL в Настройках → Вид карты.").arg(m_preparationPhase).arg(m_preparationClock.isValid()?m_preparationClock.elapsed()/1000:0)
        :(m_worker&&!backendPrepared()
            ?tr("Rebuild64 — %1\nПрошло %2 с. Окно работает; OpenGL доступен в Настройках → Вид карты.\nКарта не открыта. Можно выбрать или создать карту через меню Файл.").arg(m_preparationPhase).arg(m_preparationClock.isValid()?m_preparationClock.elapsed()/1000:0)
            :tr("Rebuild64\nКарта не открыта. Откройте или создайте карту через меню Файл.\nВосстановление сохранений и настройки доступны в обычных меню.")));
}
void CRebuild64Viewport::beginDocumentTransition() {
    if(!m_selected)return;
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);if(m_worker->input.opening)m_worker->input.opening->cancel->store(true);}
    m_earlyDocument=false;m_earlyMap.clear();
    // Resource-write transitions on the same map retain pending tool input.
    ++m_generation;m_backendResourceKey.clear();
    m_gl->setProperty("rebuild64LegacyNeighborsDemanded",false);
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->paused=true;m_worker->retainPausedHandle=true;m_worker->input.documentDirty=true;m_worker->input.generation=m_generation;}
    emit documentTransitionStarted(m_generation);
    m_documentHash.clear();m_documentMapPath.clear();m_sceneSnapshotTimer.invalidate();m_documentRefreshPending=true;
    showPreparation(tr("Открытие карты"));
}
void CRebuild64Viewport::requireOpenGL(const QString& action) {
    if(!m_selected)return;
    selectRebuild(false,false);
    emit m_gl->updateMsg(tr("%1 пока требует OpenGL. Открыт OpenGL; документы и правки сохранены. Вернуться можно через Настройки → Вид карты.").arg(action));
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
    if(!m_selected&&m_gl&&!m_waitingInput.isEmpty()) {
        auto* main=qobject_cast<MainWindow*>(window());
        if(main&&main->rebuild64DocumentReady()&&!m_gl->m_unitVisualPreparationSuspended&&!m_gl->m_openingProjectionDepth&&editorQueryReady()&&!ei::legacyProjectionPulse()) {
            m_earlyDocument=false;auto pending=std::move(m_waitingInput);m_waitingInput.clear();
            for(const auto& event:pending)QApplication::sendEvent(m_gl,event.get());m_timer->stop();
        }
        return;
    }
    if(!m_selected||!m_gl||m_gl->m_applicationShutdown)return;
    updatePreparationText();
    if(m_restartWhenFinished){
        if(m_worker&&!m_worker->finished.load())return;
        m_worker.reset();m_restartWhenFinished=false;m_backendResourceKey.clear();m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();
    }
    if(m_resourceWriteDepth||ei::legacyProjectionPulse()||window()->property("nativeMapRequestPending").toBool())return;
    if(!m_earlyDocument && !m_waitingInput.isEmpty() && frameIsCurrent() && editorQueryReady()){
        auto pending=std::move(m_waitingInput);m_waitingInput.clear();for(const auto& event:pending)editorInput(event.get());
    }
    if(m_earlyDocument) {
        auto* main=qobject_cast<MainWindow*>(window());
        const bool ready=main&&main->rebuild64DocumentReady()&&!m_gl->m_unitVisualPreparationSuspended&&!m_gl->m_openingProjectionDepth
            &&!qApp->property("rebuild64StartupDocumentBatch").toBool();
        if(!ready) {
            // Camera motion does not require the tool projection. Keep the raw
            // document's environment/light facts while updating the shared lens.
            if(m_worker) {
                // Initializing/resizing the hidden QGLWidget must not replace
                // the native viewport's lens while tool geometry is loading.
                const int logicalWidth=qMax(1,width()),logicalHeight=qMax(1,height());
                m_gl->m_width=logicalWidth;m_gl->m_height=logicalHeight;
                m_gl->m_normalProjection(0,0)=m_gl->m_normalProjection(1,1)*float(logicalHeight)/logicalWidth;
                m_gl->updateCameraProjection();
                const auto view=frameIsCurrent()?m_gl->m_cam->update():m_gl->m_cam->viewMatrix(),clip=m_gl->m_projection*view;
                const auto inverse=view.inverted();const auto eye=inverse.map(QVector3D()),target=inverse.map(QVector3D(0,0,-1));
                std::lock_guard<std::mutex> lock(m_worker->mutex);auto& camera=m_worker->input.camera;
                for(int row=0;row<4;++row)for(int col=0;col<4;++col)camera.world_to_clip[row*4+col]=row==2?.5f*(clip(2,col)+clip(3,col)):clip(row,col);
                for(int axis=0;axis<3;++axis){camera.eye[axis]=eye[axis];camera.target[axis]=target[axis];}
                m_worker->input.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
            }
            return;
        }
        // Reconcile with the canonical editable document before enabling tools.
        // A mismatch is submitted as a new guarded generation, never ignored.
        m_earlyDocument=false;m_documentRefreshPending=true;
        m_sceneSnapshotTimer.invalidate();
    }
    if(m_gl->m_unitVisualPreparationSuspended||m_gl->m_openingProjectionDepth || qApp->property("rebuild64StartupDocumentBatch").toBool())return;
    if(auto* main=qobject_cast<MainWindow*>(window()))if(!main->rebuild64DocumentReady())return;
    if(!m_gl->m_pLand || !m_gl->m_pLand->isMprLoad()) {
        // A standalone MOB/terrain-less collection is a real document, not an
        // empty editor. The pinned native scene ingress requires MPR terrain.
        if(!m_gl->m_aMob.isEmpty()) {
            if(window()->property("aiCommitBusy").toBool())return;
            requireOpenGL(QString::fromUtf8("Просмотр MOB или коллекции без MPR"));return;
        }
        m_gl->resize(this->size());
        m_stack->setCurrentWidget(m_preparation);movePanels(true);
        prepareBackend();m_documentHash.clear();return;
    }
    if(m_worker && m_gl->m_pLand->filePath().fileName().toUtf8()!=m_worker->input.name) {
        showPreparation(tr("Открытие карты"));
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
    m_documentMapPath=m_gl->m_pLand->filePath().absoluteFilePath();
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
    next.resources(m_gl->settings(),QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF()))));
    if(!next.resourcesReady){fallback(tr("Для Rebuild64 выберите установленную игру в настройках игры и мода. Правки остаются в документе."));return;}
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
    {std::lock_guard<std::mutex> lock(m_worker->mutex);next.capture=m_worker->input.capture;next.captureGeneration=m_worker->input.captureGeneration;next.captureWorld=m_worker->input.captureWorld;next.serviceSerial=m_worker->input.serviceSerial;next.serviceCpuMs=m_worker->input.serviceCpuMs;if(!documentChanged){next.service=m_worker->input.service;next.poses=m_worker->input.poses;}m_worker->input=std::move(next);m_worker->paused=false;}
    m_worker->wake.notify_all();
    if(!m_waitingInput.isEmpty() && frameIsCurrent() && editorQueryReady()){auto pending=std::move(m_waitingInput);m_waitingInput.clear();for(const auto& event:pending)editorInput(event.get());}
}
void CRebuild64Viewport::openDocumentEarly(const QFileInfo& file,const QStringList& mobs,const QStringList& search) {
    if(!m_selected||m_resourceWriteDepth||m_stopping||window()->property("nativeMapRequestPending").toBool())return;
    opening::Scope timing("gui.native_dispatch",file.fileName());
    if(auto* main=qobject_cast<MainWindow*>(window()))main->configureOpeningEnvironment(file);
    beginDocumentTransition();m_earlyDocument=true;m_earlyMap=file.absoluteFilePath();
    Worker::Input next;next.resources(m_gl->settings(),QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF()))));
    if(!m_gl->m_activeTerrainArchivePath.isEmpty()){next.paths.removeAll(m_gl->m_activeTerrainArchivePath);next.paths.prepend(m_gl->m_activeTerrainArchivePath);}
    const auto mapRoot=file.absolutePath();if(!next.paths.contains(mapRoot))next.paths.prepend(mapRoot);
    if(!next.resourcesReady)return;
    // The profile's authoring archive is validated by CView before its world
    // projection. Raw MPR/MOB remain immutable until that projection is ready.
    next.generation=m_generation;next.camera=cameraSnapshot();next.overrideHour=m_gl->overridesWorldTime();next.openingClock=true;next.cameraStamp=std::chrono::steady_clock::now();
    next.opening=std::make_shared<EarlyRead>();const auto cancel=next.opening->cancel;
    next.opening->future=QtConcurrent::run([file,mobs,search,cancel]{return readOpening(file,mobs,search,cancel);});
    m_documentRefreshPending=false;
    if(!m_worker){QDir().mkpath(m_guardDirectory);m_worker.reset(new Worker(this,reinterpret_cast<void*>(m_native->winId()),m_guardDirectory));}
    {std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input=std::move(next);m_worker->paused=false;m_worker->retainPausedHandle=false;}
    m_worker->wake.notify_all();
}
void CRebuild64Viewport::capture(const QString& path,bool worldOnly) {
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.capture=path;m_worker->input.captureGeneration=m_generation;m_worker->input.captureWorld=worldOnly;}
}
QImage CRebuild64Viewport::emptyViewportImage() {
    if(!m_selected||!m_gl||m_gl->m_pLand->isMprLoad()||!m_gl->m_aMob.isEmpty())return {};
    QImage image(m_preparation->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);m_preparation->render(&image);return image;
}
void CRebuild64Viewport::cancelCapture(const QString& path) {
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);if(m_worker->input.capture==path)m_worker->input.capture.clear();}
}
bool CRebuild64Viewport::eventFilter(QObject* object,QEvent* event) {
    if(object==m_gl&&!m_selected&&m_gl->property("nativeOpeningInput").toBool()&&(m_gl->m_unitVisualPreparationSuspended||m_gl->m_openingProjectionDepth)) {
        switch(event->type()) {
        case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::MouseButtonPress:case QEvent::MouseButtonDblClick:case QEvent::MouseMove:case QEvent::MouseButtonRelease:case QEvent::Wheel:
            m_earlyDocument=true;editorInput(event);if(!m_waitingInput.isEmpty())m_timer->start();return true;
        default:break;
        }
    }
    if(object==m_native && m_selected) {
        switch(event->type()) {
        case QEvent::ShortcutOverride:case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::MouseButtonPress:case QEvent::MouseButtonDblClick:case QEvent::MouseMove:case QEvent::MouseButtonRelease:case QEvent::Wheel:case QEvent::FocusOut:case QEvent::ContextMenu:
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
    if(event->type()==QEvent::FocusOut){
        m_gl->m_pOp->keyManager()->keys().clear();
        if((m_earlyDocument||!editorQueryReady())&&!m_waitingInput.isEmpty())m_waitingInput.append(std::make_shared<QFocusEvent>(*static_cast<QFocusEvent*>(event)));
        return;
    }
    if(m_earlyDocument || !editorQueryReady()) {
        if(event->type()==QEvent::MouseMove && static_cast<QMouseEvent*>(event)->buttons()==Qt::NoButton && m_waitingInput.isEmpty())return;
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)
            m_waitingInput.append(std::make_shared<QKeyEvent>(*static_cast<QKeyEvent*>(event)));
        else if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseButtonDblClick||event->type()==QEvent::MouseMove)
            m_waitingInput.append(std::make_shared<QMouseEvent>(*static_cast<QMouseEvent*>(event)));
        else if(event->type()==QEvent::Wheel)m_waitingInput.append(std::make_shared<QWheelEvent>(*static_cast<QWheelEvent*>(event)));
        if(!m_waitingInput.isEmpty())emit m_gl->updateMsg(tr("Rebuild64: подготовка геометрии инструментов. Ввод сохранён в очереди и будет выполнен после подготовки."));
        event->accept();return;
    }
    if(event->type()==QEvent::KeyPress && m_gl->m_pendingOperationOwnsPaste) {
        const auto* key=static_cast<QKeyEvent*>(event);
        if((key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)
           && !m_gl->m_pOp->canConfirmWithEnter()) {
            // Keep the canonical placement operation alive. Empty Enter in
            // the legacy move state exits it without committing this paste.
            emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: пустой Enter не подтверждает вставку. Подтвердите её щелчком мыши; предпросмотр и правки сохранены."));
            event->accept();return;
        }
    }
    if((event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)
       && m_gl->m_landscapeTools && m_gl->m_landscapeTools->editingEnabled()) {
        const auto* key=static_cast<QKeyEvent*>(event);
        const auto cameraKeys=QVector<int>{Qt::Key_W,Qt::Key_A,Qt::Key_S,Qt::Key_D,Qt::Key_Q,Qt::Key_E,Qt::Key_Up,Qt::Key_Down,Qt::Key_Left,Qt::Key_Right,Qt::Key_Shift,Qt::Key_Escape};
        if(!cameraKeys.contains(key->key())&&!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier))) {
            if(event->type()==QEvent::KeyPress)emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: инструменты рельефа требуют OpenGL."));
            event->accept();return;
        }
    }
    if(event->type()==QEvent::MouseButtonDblClick)syncEditorViewport();
    if(event->type()==QEvent::MouseButtonDblClick && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton
       && m_gl->m_neighborLandscapesVisible && !m_gl->m_neighborPlacements.isEmpty()
       && m_gl->m_neighborLandscapes.size()<m_gl->m_neighborPlacements.size()
       && !m_gl->neighborMapAtViewport(static_cast<QMouseEvent*>(event)->pos(),nullptr)) {
        // The canonical neighbour ray query uses its landscape geometry.
        // Materialize it only for this explicit navigation gesture; retain the
        // native image and replay the original event into the same CView logic.
        if(!m_legacyNavigationPending) {
            m_legacyNavigationPending=true;
            m_gl->setProperty("rebuild64LegacyNeighborsDemanded",true);
            m_gl->loadNeighborLandscapes();
            emit m_gl->updateMsg(tr("Rebuild64: подготовка геометрии соседей для перехода двойным щелчком. Вид карты сохраняется."));
            auto original=*static_cast<QMouseEvent*>(event);
            const auto map=m_gl->m_pLand->filePath().absoluteFilePath();
            const auto clickView=m_gl->m_cam->viewMatrix(),clickProjection=m_gl->m_projection;
            auto* timer=new QTimer(this);timer->setInterval(25);QElapsedTimer elapsed;elapsed.start();
            connect(timer,&QTimer::timeout,this,[this,timer,original,map,elapsed,clickView,clickProjection]()mutable{
                const bool cancelled=!m_selected || !m_gl || !m_gl->m_pLand
                    || m_gl->m_pLand->filePath().absoluteFilePath()!=map;
                const bool ready=!cancelled && !m_gl->m_neighborLoadStage && !m_gl->m_neighborLoadJobs
                    && !m_gl->m_neighborObjectUploadScheduled;
                if(!cancelled&&!ready&&elapsed.elapsed()<120000)return;
                timer->stop();timer->deleteLater();m_legacyNavigationPending=false;
                if(ready && !m_gl->m_neighborLandscapes.isEmpty()) {
                    if(m_gl->m_cam->viewMatrix()!=clickView || m_gl->m_projection!=clickProjection) {
                        emit m_gl->updateMsg(tr("Камера изменилась во время подготовки геометрии соседей. Повторите двойной щелчок; правки сохранены."));return;
                    }
                    syncEditorViewport();QApplication::sendEvent(m_gl,&original);
                }
                else if(!cancelled)emit m_gl->updateMsg(tr("Переход к соседней карте не выполнен: геометрия соседей не готова. Повторите или выберите OpenGL; правки сохранены."));
            });timer->start();
        }
        event->accept();return;
    }
    const bool mouse=event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease;
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
    if(m_selected && mouse && event->type()==QEvent::MouseButtonPress)m_native->setFocus(Qt::MouseFocusReason);
}
bool CRebuild64Viewport::editorQueryReady() const {
    if(m_gl->m_openingProjectionDepth)return false;
    if(!m_gl->m_neighborLandscapesVisible)return true;
    const QSizeF size(m_gl->m_pLand->widthInWorldUnits(),m_gl->m_pLand->heightInWorldUnits());
    for(const auto& placement:m_gl->m_neighborPlacements) {
        if(!CNeighborMapLayout::touchesActiveMap(size,placement))continue;
        bool ready=false;
        for(const auto& loaded:m_gl->m_neighborLandscapes)
            if(loaded.placement.mprPath==placement.mprPath) {
                ready=loaded.landscape && loaded.landscape->isMprLoad();break;
            }
        if(!ready)return false;
    }
    return true;
}
void CRebuild64Viewport::serviceInput() {
    if(!m_worker)return;
    if(editorMobOrder(m_gl)!=m_documentMobOrder)return;
    m_gl->ensureLegacyQueryContext();
    syncEditorViewport();
    // Advance the single editor camera once, before both consumers.
    if(m_stack->currentWidget()==m_native)m_gl->m_cam->update();
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
    m_worker->input.editorGeometryReady=editorQueryReady();m_worker->input.serviceCpuMs=m_serviceMilliseconds;m_worker->input.service=image;m_worker->input.operationPreview=m_gl->hasPendingOperation();m_worker->input.poses=std::move(poses);++m_worker->input.serviceSerial;
}

void CRebuild64Viewport::syncEditorViewport() {
    const QSize size=this->size();if(size.isEmpty())return;
    m_native->resize(size);
    const auto projection=m_gl->m_projection,normal=m_gl->m_normalProjection;
    const double before=m_gl->m_height>0?double(m_gl->m_width)/m_gl->m_height:0;
    const double after=double(size.width())/size.height();
    m_gl->resize(size);
    // QGLWidget reports physical dimensions to resizeGL on Windows. Editor
    // events and query rectangles are logical. A DPR-only change must not
    // rebuild the shared lens (including the current far/telephoto projection).
    if(qAbs(before-after)>.000001 || m_editorViewportDirty){m_gl->resizeGL(size.width(),size.height());if(qAbs(before-after)<=.000001){m_gl->m_projection=projection;m_gl->m_normalProjection=normal;}}
    else {m_gl->m_projection=projection;m_gl->m_normalProjection=normal;}
    m_gl->m_width=size.width();m_gl->m_height=size.height();m_editorViewportDirty=false;
}

void CRebuild64Viewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);if(m_stopping||!m_gl||!m_selected||event->size().isEmpty())return;
    const auto extent=event->size();m_gl->m_width=extent.width();m_gl->m_height=extent.height();
    m_gl->m_normalProjection.setToIdentity();m_gl->m_normalProjection.perspective(45,float(extent.width())/extent.height(),1,1000);
    m_gl->updateCameraProjection();m_editorViewportDirty=true;
    QTimer::singleShot(0,this,&CRebuild64Viewport::tick);
}

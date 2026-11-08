#include <QContextMenuEvent>
#include "rebuild64_viewport_widget.h"
#include "view.h"
#include "log.h"
#include "opening_trace.h"
#include "camera_latency293_trace.h"
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
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QTemporaryDir>
#include "camera.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "unit.h"
#include "scene.h"
#include "mmp_codec.h"
#include "types.h"
#include "utils.h"
#include "res_archive_document.h"
#include "terrain_virtual_atlas.h"
#include "diplomacy_colors.h"
#include "resource_name.h"
#include "light.h"
#include "torch.h"
#include "view_frustum.h"
#include <QSaveFile>
#include "operationmanager.h"
#include "navmesh_panel.h"
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
#include <algorithm>
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
struct CRebuild64Viewport::VisualCache292 {
    struct Entry {QByteArray key,body;QString composite;};
    QHash<quintptr,Entry> nodes;
    QHash<QString,QByteArray> composites;
    QHash<QString,QFuture<QByteArray>> composing;
    QHash<QString,QString> textures;
    QString resources;
    quint64 builds{0},hits{0};
};
struct CRebuild64Viewport::Worker {
    struct OutputJob {quint64 serial,generation;QSize size;int mode;EirCamera camera;QByteArray model,texture;QVector3D shape;QVector<float> preview;QVector<quint64> objectKeys;QByteArray secondary,parts,modelBytes,boneBytes,textureBytes,visuals;};
    QList<OutputJob> outputs;

    struct Query {quint64 serial=0,generation=0,terrainSerial=0;EirCamera camera{};QMatrix4x4 clip;QSize size,logicalSize;QVector<EirPickPixel> pixels;};
    #include "rebuild64_atlas292.inc"
    struct TerrainPacket {
        QByteArray bytes,name;QRect rect;quint32 flags{0};quint64 serial{0};bool preserveObjects{false};std::shared_ptr<AtlasSource> atlas;
    };
    struct Input {
        QByteArray terrain,name,visuals;
        std::shared_ptr<AtlasSource> atlas;
        TerrainPacket terrainUpdate;
        QMap<QByteArray,TerrainPacket> neighborUpdates;
        QVector<QByteArray> mobs;
        QStringList paths;
        QString gameRoot, neighbourLayout;
        quint64 generation{0};
        EirCamera camera{};
        QImage service;
        std::shared_ptr<const CNativeEditorService> serviceScene;
        std::shared_ptr<Query> query;
        QVector<EirObjectPose> poses,documentObjects;
        quint64 serviceSerial{0},captureGeneration{0},captureServiceSerial{0};double serviceCpuMs{0};
        QSize size;
        QString capture;bool captureWorld{false},documentDirty{false},operationPreview{false},editorGeometryReady{false};
        bool resourcesReady{false},overrideHour{false},openingClock{false};
        std::chrono::steady_clock::time_point cameraStamp{std::chrono::steady_clock::now()};
        std::shared_ptr<EarlyRead> opening;
        void resources(CSettings* settings,const QSize& extent) {
            opening::Scope timing("gui.293.Input.resources");
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
    Input liveInput; // retains spans supplied by the output-only callback
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
    QImage frameService293;
    static uint32_t EIR_CALL rasterService293(void* context,uint64_t generation,const EirCamera* camera,uint32_t width,uint32_t height,const float* pixels,EirService* result) {
        latency293::Scope timing(latency293::Raster);
        auto& worker=*static_cast<Worker*>(context);
        std::shared_ptr<const CNativeEditorService> snapshot;quint64 serial=0;
        {latency293::Lock lock(worker.mutex,latency293::WorkerSnapshotWait);snapshot=worker.input.serviceScene;serial=worker.input.serviceSerial;}
        if(!snapshot||snapshot->generation!=generation)return 0;
        // Snapshot copies are implicitly shared values; only the projection is
        // frame-specific. Labels and geometry use exactly the world frame lens.
        auto frame=*snapshot;
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)frame.clip(r,c)=r==2?2*camera->world_to_clip[8+c]-camera->world_to_clip[12+c]:camera->world_to_clip[r*4+c];
        QVector<float> depth(int(width*height));std::copy_n(pixels,depth.size(),depth.data());
        QElapsedTimer timer;timer.start();worker.frameService293=frame.raster(QSize(width,height),depth);
        const auto& image=worker.frameService293;
        *result={sizeof(EirService),width,height,uint32_t(image.bytesPerLine()),serial,generation,timer.nsecsElapsed()/1e6,{image.constBits(),uint64_t(image.sizeInBytes())},nullptr,0};
        result->image_update=1;std::copy_n(camera->world_to_clip,16,result->world_to_clip);return 1;
    }
    static uint32_t EIR_CALL liveValues(void* context,uint64_t generation,uint32_t neighbours,EirCamera* camera,EirService* service) {
        auto& worker=*static_cast<Worker*>(context);std::lock_guard<std::mutex> lock(worker.mutex);
        if(worker.stop||worker.paused||worker.input.generation!=generation||worker.input.documentDirty||worker.input.camera.neighbours_visible!=neighbours)return 0;
        worker.liveInput=worker.input;const auto& current=worker.liveInput;*camera=current.camera;
        *service={sizeof(EirService),uint32_t(current.service.width()),uint32_t(current.service.height()),uint32_t(current.service.bytesPerLine()),current.serviceSerial,current.generation,current.serviceCpuMs,{current.service.constBits(),uint64_t(current.service.sizeInBytes())},current.poses.constData(),uint32_t(current.poses.size())};
        return 1;
    }
    qint64 presentLeaseStart293{0};int presentLeasePhase293{0};
    static uint32_t EIR_CALL canPresent(void* context,uint64_t generation,uint32_t neighbours,uint32_t phase) {
        auto& worker=*static_cast<Worker*>(context);
        if(phase==1){latency293::add(latency293::PresentLease,worker.presentLeasePhase293,worker.presentLeaseStart293);worker.mutex.unlock();return 1;}
        worker.mutex.lock();
        const bool current=!worker.stop&&!worker.paused&&worker.input.generation==generation
            &&worker.input.camera.neighbours_visible==neighbours
            &&(!worker.input.documentDirty||worker.input.operationPreview)
            ; // Native world readiness is independent of editor query geometry.
        if(phase==2||!current)worker.mutex.unlock();
        else {worker.presentLeasePhase293=latency293::state().phase.load();worker.presentLeaseStart293=worker.presentLeasePhase293?latency293::clock():0;}
        return current;
    }
    void processOutput(void* handle,EirApi& api,quint64 generation,const EirStatistics& stats) {
        EirError error{};
                OutputJob job{};bool hasJob=false;
                {std::lock_guard<std::mutex> lock(mutex);if(!outputs.isEmpty()){job=outputs.takeFirst();hasJob=true;}}
                if(hasJob) {
                    const QSize extent=job.mode<2?QSize(stats.width,stats.height):job.size;
                    QImage image(extent,QImage::Format_ARGB32);EirOutput value{};value.size=sizeof(value);
                    value.bgra=image.bits();value.capacity=image.sizeInBytes();
                    EirOutputRequest request{};request.size=sizeof(request);request.mode=job.mode;request.width=extent.width();request.height=extent.height();
                    request.generation=job.generation;request.serial=job.serial;request.camera=job.camera;
                    request.model=job.model.constData();request.texture=job.texture.constData();for(int a=0;a<3;++a)request.shape[a]=job.shape[a];request.preview[4]=1;for(int a=0;a<qMin(12,job.preview.size());++a)request.preview[a]=job.preview[a];request.object_keys={job.objectKeys.constData(),uint64_t(job.objectKeys.size())*sizeof(quint64)};request.secondary_texture=job.secondary.constData();request.part_names=job.parts.constData();
                    request.visuals={job.visuals.constData(),uint64_t(job.visuals.size())};request.model_bytes={job.modelBytes.constData(),uint64_t(job.modelBytes.size())};request.bone_bytes={job.boneBytes.constData(),uint64_t(job.boneBytes.size())};request.texture_bytes={job.textureBytes.constData(),uint64_t(job.textureBytes.size())};
                    QString diagnostic;
                    if(job.generation!=generation)diagnostic=QString::fromUtf8("Документ изменился до offscreen-вывода Rebuild64. Повторите запрос.");
                    else if(!qEnvironmentVariable("ATAMAN_REBUILD64_LAUNCH_CASE").isEmpty()&&QFileInfo::exists(qEnvironmentVariable("ATAMAN_REBUILD64_LAUNCH_CASE")+".offscreen-error"))diagnostic=QString::fromUtf8("Тестовая ошибка offscreen Rebuild64.");
                    else if(api.output(handle,&request,&value,&error))diagnostic=QString::fromUtf8(error.message);
                    else image=image.copy(0,0,int(value.width),int(value.height));
                    if(qEnvironmentVariableIsSet("ATAMAN_REBUILD64_LAUNCH_CASE"))qInfo("NATIVE_OUTPUT mode=%d serial=%llu generation=%llu map=%s neighbours_accepted=%u pending=%u extent=%ux%u error=%s",job.mode,(unsigned long long)job.serial,(unsigned long long)value.generation,value.map,value.neighbours_accepted,value.pending,value.width,value.height,qPrintable(diagnostic));

                    QMetaObject::invokeMethod(owner,[target=owner,job,image,diagnostic,value]{
                        QString error=diagnostic;
                        if(target->m_stopping||(!target->m_selected&&!target->m_outputOnly)||job.generation!=target->m_generation||(job.mode!=3&&target->m_documentRefreshPending))error=QString::fromUtf8("Offscreen отменён: карта закрыта или изменена.");
                        emit target->outputFinished(job.serial,error.isEmpty()?image:QImage(),error,value.generation,QString::fromUtf8(value.map),int(value.neighbours_accepted));
                    },Qt::QueuedConnection);
                }
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
        void* handle=nullptr;quint64 generation=0,readyGeneration=0;QSize size;QElapsedTimer pending;QStringList activePaths;QString activeGameRoot, activeLayout;
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
                next=input; // A pending frame must not consume a capture request without a response.
            }
            EirError error{};
            bool detachedPreview=false;
            {std::lock_guard<std::mutex> lock(mutex);detachedPreview=!outputs.isEmpty()&&outputs.front().mode==3;}
            if(next.terrain.isEmpty() && !next.opening&&!detachedPreview) {
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
                EirCreate info{sizeof(info),EIR_ABI,uint32_t(next.size.width()),uint32_t(next.size.height()),window,mounts.data(),uint32_t(mounts.size()),hook=="no-d3d11"?1u:hook=="init"?2u:hook=="shader-hold"?3u:0u,reinterpret_cast<const uint16_t*>(cache.c_str()),reinterpret_cast<const uint16_t*>(layout.c_str()),reinterpret_cast<const uint16_t*>(dataRoot.c_str()),&Worker::canPresent,this,&Worker::liveValues,EIR_PREVIEW_STATIC_FOOTPRINTS,&Worker::rasterService293};
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
            if(detachedPreview&&next.terrain.isEmpty())processOutput(handle,api,next.generation,{});
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
                QVector<QByteArray> atlasNames,atlasPages;QByteArray atlasProfile;QString atlasError;
                if(!atlasPackage(next.terrain,next.atlas,atlasNames,atlasPages,atlasProfile,atlasError)){failure(atlasError);break;}
                QVector<EirTexturePage> documentPages;for(int i=0;i<atlasPages.size();++i)documentPages.append({atlasNames[i].constData(),{atlasPages[i].constData(),uint64_t(atlasPages[i].size())}});
                EirDocument doc{sizeof(doc),uint32_t(mobs.size()),next.generation,{next.terrain.constData(),uint64_t(next.terrain.size())},mobs.data(),next.name.constData(),next.documentObjects.constData(),uint32_t(next.documentObjects.size())};doc.visuals={next.visuals.constData(),uint64_t(next.visuals.size())};
                doc.pages=documentPages.constData();doc.page_count=uint32_t(documentPages.size());doc.terrain_profile={atlasProfile.constData(),uint64_t(atlasProfile.size())};
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
            // The source snapshot remains owned by Input. Coalescing only replaces
            // unsent rectangles; generation and native world are checked at ingress.
            EirUpdateState updates{};
            if(api.update_state(handle,&updates,&error)){failure(QString::fromUtf8(error.message));break;}
            if(next.terrainUpdate.flags&&next.terrainUpdate.serial>updates.applied_serial && updates.world && readyGeneration==generation) {
                const auto& p=next.terrainUpdate;
                QByteArray terrain=p.bytes,profile;QVector<QByteArray> names,pages;QString diagnostic;
                if(!atlasPackage(terrain,p.atlas,names,pages,profile,diagnostic)){failure(diagnostic);break;}
                QVector<EirTexturePage> inputPages;for(int i=0;i<pages.size();++i)inputPages.append({names[i].constData(),{pages[i].constData(),uint64_t(pages[i].size())}});
                EirTerrainUpdate update{};update.size=sizeof(update);update.flags=p.flags&15u;
                update.generation=generation;update.world=updates.world;update.serial=p.serial;
                update.x0=p.rect.x();update.y0=p.rect.y();update.x1=p.rect.x()+p.rect.width();update.y1=p.rect.y()+p.rect.height();
                update.preserve_object_height=p.preserveObjects;update.terrain_name=p.name.constData();update.terrain={terrain.constData(),uint64_t(terrain.size())};
                update.pages=inputPages.constData();update.page_count=uint32_t(inputPages.size());update.terrain_profile={profile.constData(),uint64_t(profile.size())};
                const auto call=p.atlas?api.atlas_update:api.terrain_update;
                if(call(handle,&update,&error)){failure(QString::fromUtf8(error.message));break;}
                {std::lock_guard<std::mutex> lock(mutex);if(input.generation==generation&&input.terrainUpdate.serial==p.serial)input.terrainUpdate=TerrainPacket();}
            }
            for(auto it=next.neighborUpdates.cbegin();it!=next.neighborUpdates.cend();++it) {
                const auto& p=it.value();EirTerrainUpdate update{};update.size=sizeof(update);update.flags=p.flags;
                update.generation=generation;update.world=updates.world;update.serial=p.serial;
                update.x0=p.rect.x();update.y0=p.rect.y();update.x1=p.rect.x()+p.rect.width();update.y1=p.rect.y()+p.rect.height();
                update.terrain_name=p.name.constData();update.terrain={p.bytes.constData(),uint64_t(p.bytes.size())};update.preserve_object_height=p.preserveObjects;
                const int rc=api.neighbour_terrain(handle,&update,&error);
                if(rc<0){failure(QString::fromUtf8(error.message));return;}
                if(!rc){std::lock_guard<std::mutex> lock(mutex);auto pending=input.neighborUpdates.find(it.key());
                    if(input.generation==generation&&pending!=input.neighborUpdates.end()&&pending->serial==p.serial)input.neighborUpdates.erase(pending);}
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
            if(!injected&&(hook=="device-lost"||(hook=="device-lost-late"&&QFile::exists(QString::fromUtf8(qgetenv("ATAMAN_REBUILD64_LAUNCH_CASE"))+".device-loss")))){api.test_device_loss(handle);injected=true;}
            EirStatistics stats{};const int rc=api.frame(handle,&stats,&error);
            if(rc!=0 && rc!=1){failure(QString::fromUtf8(error.message));break;}
            if(rc==1)phase(QString::fromUtf8("Подготовка кадра и соседних карт"));
            if(rc==1 && pending.isValid() && pending.elapsed()>120000) {
                failure(QString::fromUtf8("Rebuild64: данные нового кадра не подготовлены за 120 секунд. Открыт OpenGL."));break;
            }
            if(!rc) {
                readyGeneration=generation;
                prepared=true;
                if(next.query){auto request=next.query;
                    request->pixels.resize(request->size.width()*request->size.height());
                    EirQuery query{sizeof(EirQuery),uint32_t(request->size.width()),uint32_t(request->size.height()),request->generation,request->camera,request->pixels.data(),uint64_t(request->pixels.size())};
                    const int queryRc=api.query(handle,&query,&error);const QString diagnostic=queryRc?QString::fromUtf8(error.message):QString();
                    {std::lock_guard<std::mutex> lock(mutex);if(input.query==request)input.query.reset();}
                    QMetaObject::invokeMethod(owner,[target=owner,request,diagnostic]{
                        if(target->m_stopping||request->serial!=target->m_querySerial||request->terrainSerial!=target->m_terrainSubmitted)return;
                        target->m_queryInFlight=false;auto pending=std::move(target->m_queryInput);target->m_queryInput.clear();
                        if(!target->m_selected||request->generation!=target->m_generation||request->logicalSize!=target->size())return;
                        if(!diagnostic.isEmpty()){emit target->m_gl->updateMsg(target->tr("Rebuild64: точный запрос недоступен: %1").arg(diagnostic));return;}
                        target->m_gl->m_nativeQueryPixels=request->pixels;target->m_gl->m_nativeQueryClip=request->clip;target->m_gl->m_nativeQuerySize=request->size;
                        for(const auto& input:pending){target->m_replayInput=&input;target->m_queryReplaying=request->clip==input.projection*input.view;target->editorInput(input.event.get());target->m_queryReplaying=false;target->m_replayInput=nullptr;target->m_gl->m_cam->clearPreviewView();}
                    },Qt::QueuedConnection);
                }
                // Main frame is usable while the native neighbour stream runs.
                // Keep the crash marker until its async load/adoption settles.
                if(!stats.neighbours_pending){startup.clear();startupArmed=false;}
                pending.invalidate();
                if(api.update_state(handle,&updates,&error)){failure(QString::fromUtf8(error.message));break;}
                QMetaObject::invokeMethod(owner,[target=owner,stats,updates]{
                    if(target->m_stopping||stats.ready_generation!=target->m_generation)return;
                    target->m_statistics=stats;
                    bool neighborPending=false;{std::lock_guard<std::mutex> lock(target->m_worker->mutex);neighborPending=!target->m_worker->input.neighborUpdates.isEmpty();}
                    if(!neighborPending)target->m_terrainPresented=updates.presented_serial;
                    if(!qEnvironmentVariableIsEmpty("ATAMAN_REBUILD64_LAUNCH_CASE")){
                        target->setProperty("nativeFinalized292",QVariant::fromValue<qulonglong>(updates.finalized_serial));
                        target->setProperty("nativeDerivedPending292",bool(updates.derived_pending||neighborPending));
                    }
                    if(target->m_selected && !target->m_documentRefreshPending && stats.ready_generation==target->m_generation) {
                        const bool firstShow=target->m_stack->currentWidget()!=target->m_native;
                        target->m_stack->setCurrentWidget(target->m_native);
                        target->movePanels(true);
                        if(firstShow&&!QApplication::activeModalWidget())target->m_native->setFocus(Qt::OtherFocusReason);
                        target->syncEditorViewport();
                        emit target->frameCommitted(stats.ready_generation);
                    }
                },Qt::QueuedConnection);
                if(!next.capture.isEmpty()&&(next.captureWorld||stats.service_serial>=next.captureServiceSerial)) {
                    bool currentRequest=false;
                    {std::lock_guard<std::mutex> lock(mutex);currentRequest=input.capture==next.capture&&input.captureGeneration==next.captureGeneration;
                        if(currentRequest)input.capture.clear();}
                    if(!currentRequest)continue;
                    QString captureError;
                    if(next.captureGeneration!=generation)captureError=QString::fromUtf8("Документ изменился до получения кадра Rebuild64. Повторите window_snapshot.");
                    else {
                        QImage image(int(stats.width),int(stats.height),QImage::Format_ARGB32);
                        EirOutput value{};value.size=sizeof(value);value.bgra=image.bits();value.capacity=image.sizeInBytes();
                        EirOutputRequest request{};request.size=sizeof(request);request.mode=next.captureWorld?1:0;request.generation=generation;
                        if(!qEnvironmentVariable("ATAMAN_REBUILD64_LAUNCH_CASE").isEmpty()&&QFileInfo::exists(qEnvironmentVariable("ATAMAN_REBUILD64_LAUNCH_CASE")+".offscreen-error"))captureError=QString::fromUtf8("Тестовая ошибка offscreen Rebuild64.");
                        else if(api.output(handle,&request,&value,&error))captureError=QString::fromUtf8(error.message);
                        else if(!image.save(next.capture,"PNG"))captureError=QString::fromUtf8("Не удалось записать снимок Rebuild64.");
                    }
                    QMetaObject::invokeMethod(owner,[target=owner,path=next.capture,captureError,generation]{emit target->captureFinished(path,captureError,generation);},Qt::QueuedConnection);
                }
            }
            if(!rc)processOutput(handle,api,generation,stats);
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
    connect(this,&CRebuild64Viewport::captureFinished,this,[this](const QString& path,const QString&,quint64){if(m_activeCapturePath==path)m_activeCapturePath.clear();});
    setObjectName("rebuild64Viewport");m_native->setObjectName("rebuild64NativeSurface");
    m_preparation->setObjectName("rebuild64Preparation");
    m_preparation->setAutoFillBackground(true);
    auto* stateLayout=new QVBoxLayout(m_preparation);stateLayout->addStretch();stateLayout->addWidget(m_preparationText);stateLayout->addStretch();
    m_preparationText->setAlignment(Qt::AlignCenter);m_preparationText->setWordWrap(true);
    m_stack->setContentsMargins(0,0,0,0);m_stack->setSpacing(0);m_stack->addWidget(gl);m_stack->addWidget(m_native);m_stack->addWidget(m_preparation);m_stack->setCurrentWidget(m_preparation);
    connect(gl,&CView::legacyViewportRequested,this,&CRebuild64Viewport::requireOpenGL);
    connect(gl,&CView::nativeTerrainChanged,this,&CRebuild64Viewport::queueTerrain);
    connect(gl,&CView::nativeNeighborTerrainChanged,this,&CRebuild64Viewport::queueNeighborTerrain);
    connect(gl,&CView::nativeSceneReloadRequested,this,&CRebuild64Viewport::reloadScene);
    connect(gl,&CView::nativeTerrainPosesChanged,this,[this]{m_gl->setProperty("nativeTerrainPoses293",true);});
    const auto dirtyService=[this]{m_serviceDirty293=true;};
    connect(gl,&CView::selectionChanged,this,dirtyService);
    connect(gl,&CView::mobStateChanged,this,dirtyService);
    connect(gl,&CView::loadedMapStateChanged,this,dirtyService);
    connect(gl,&CView::nativeTerrainChanged,this,dirtyService);
    connect(gl,&CView::nativeNeighborTerrainChanged,this,dirtyService);
    connect(gl,&CView::diplomacyMapModeChanged,this,dirtyService);
    connect(gl->m_pSettings,&CSettings::renderSettingsChanged,this,dirtyService);
    connect(CScene::getInstance(),&CScene::modeChanged,this,dirtyService);
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
    connect(gl,&CView::mobStateChanged,this,[this]{if(m_earlyDocument||m_gl->property("nativeTerrainPoseCommit293").toBool())return;m_mobDocumentRefresh=true;m_documentRefreshPending=true;if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.documentDirty=true;}});
    connect(gl,&CView::loadedMapStateChanged,this,[this]{
        if(m_earlyDocument && m_gl->m_pLand && m_gl->m_pLand->isMprLoad()
           &&m_earlyMap==m_gl->m_pLand->filePath().absoluteFilePath())return;
        m_documentRefreshPending=true;
        if((m_selected||m_outputOnly) && (!m_gl->m_pLand || !m_gl->m_pLand->isMprLoad()
           || m_documentMapPath!=m_gl->m_pLand->filePath().absoluteFilePath()))beginDocumentTransition();
        if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.documentDirty=true;}
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
    ei::log(eLogInfo,QString("Renderer startup requested:%1 latched:%2 interrupted:%3")
        .arg(requested?"Rebuild64":"OpenGL").arg(latched).arg(interrupted));
    // Select before MainWindow can show. Migration does not acknowledge a guard.
    selectRebuild(requested&&!latched&&!interrupted,false,QStringLiteral("startup"));
    if(latched||interrupted)QTimer::singleShot(0,this,[this]{
        emit m_gl->updateMsg(tr("Предыдущая загрузка Rebuild64 завершилась аварийно. Открыт OpenGL. Для повторного запуска выберите Rebuild64 в Настройках → Вид карты."));
    });
}
CRebuild64Viewport::~CRebuild64Viewport(){CResourceWriteLease::setAcquirer({});finishApplicationShutdown();movePanels(false);}
void CRebuild64Viewport::movePanels(bool native) {
    if(!m_gl)return;
    const bool legend=native&&m_gl->m_navmeshPanel&&m_gl->m_navmeshPanel->layerVisible()&&m_gl->m_navmeshPanel->legendVisible();
    if(legend&&!m_nativeLegend){
        auto* panel=new CViewportWidget(m_gl);panel->setObjectName("rebuild64NavmeshLegend");
        auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(4,4,4,4);
        auto* text=new QTextBrowser(panel);text->setObjectName("rebuild64NavmeshLegendText");text->setFrameShape(QFrame::NoFrame);text->setOpenLinks(false);text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(text);m_nativeLegend=panel;
    }
    if(m_nativeLegend){
        m_nativeLegend->setVisible(legend);
        if(legend){
            auto* text=m_nativeLegend->findChild<QTextBrowser*>("rebuild64NavmeshLegendText");const auto html=m_gl->m_navmeshPanel->legendHtml();
            if(text->property("legendHtml").toString()!=html){text->setHtml(html);text->setProperty("legendHtml",html);}
        }
    }
    m_gl->setProperty("rebuild64NativeLegend",legend);
    if(native) {
        for(auto* child:m_gl->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
            if(!child->isWindow()&&!m_panels.contains(child))m_panels.append(child);
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
    if(legend&&m_nativeLegend){
        // Exact obstacle edges rather than a coarse search grid: in a narrow
        // view the remaining bottom-right strip can be only a few pixels
        // shorter than the full legend. Scroll its content inside that strip.
        const QRect bounds(12,12,qMax(1,width()-24),qMax(1,height()-24));QList<QRect> occupied;
        QList<int> xs{bounds.left(),bounds.right()+1},ys{bounds.top(),bounds.bottom()+1};
        for(const auto& widget:m_panels)if(widget&&widget!=m_nativeLegend&&widget->isVisible()){
            const auto box=widget->geometry().adjusted(-6,-6,6,6).intersected(bounds);if(box.isEmpty())continue;occupied<<box;
            xs<<box.left()<<box.right()+1;ys<<box.top()<<box.bottom()+1;
        }
        std::sort(xs.begin(),xs.end());std::sort(ys.begin(),ys.end());
        QRect best;int bestArea=0;
        for(int left:xs)for(int right:xs)if(right-left>=160)for(int top:ys)for(int bottom:ys)if(bottom-top>=72){
            QRect candidate(left,top,qMin(337,right-left),qMin(360,bottom-top));if(!bounds.contains(candidate))continue;
            bool blocked=false;for(const auto& box:occupied)if(candidate.intersects(box)){blocked=true;break;}if(blocked)continue;
            const int area=candidate.width()*candidate.height();if(area>bestArea){bestArea=area;best=candidate;}
        }
        if(best.isEmpty())best=QRect(qMax(12,width()-212),qMax(12,height()-132),qMin(200,width()-24),qMin(120,height()-24));
        m_nativeLegend->setGeometry(best);m_nativeLegend->raise();m_gl->m_navmeshLegendRect=best;
    }
    if(!native)m_panels.clear();
}
bool CRebuild64Viewport::backendLoaded() const {return m_worker&&m_worker->loaded.load();}
bool CRebuild64Viewport::cameraNavigationActive() const {
    return m_selected&&m_gl&&(m_serviceOrbit293||m_gl->m_cam->movementInputActive());
}
bool CRebuild64Viewport::backendPrepared() const {return m_worker&&m_worker->prepared.load();}
bool CRebuild64Viewport::backendResourcesRetired() const {return !m_worker||m_worker->resourcesRetired.load();}
void CRebuild64Viewport::stopForApplicationShutdown() {
    if(m_stopping)return;cancelOutputs(tr("Offscreen отменён: приложение завершает работу."));m_stopping=true;m_selected=false;m_timer->stop();
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
    {opening::Scope stage("gui.native.resource_snapshot");next.resources(m_gl->settings(),QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF()))));}
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

void CRebuild64Viewport::selectRebuild(bool selected,bool persist,const QString& action) {
    if(!m_gl||m_stopping)return;
    if(!selected){cancelOutputs(tr("Offscreen отменён: выбран OpenGL."));if(!m_activeCapturePath.isEmpty())emit captureFinished(m_activeCapturePath,tr("Снимок отменён: выбран OpenGL."),m_generation);}
    if(selected && qApp->property("agentHeadless").toBool())return;

    if(selected&&m_worker&&m_worker->finished.load()){m_worker.reset();m_restartWhenFinished=false;}
    else if(selected&&m_worker&&!m_failure.isEmpty())m_restartWhenFinished=true;
    if(!selected)m_restartWhenFinished=false;
    m_gl->setFocusProxy(selected?m_native:nullptr);
    m_pointerDown=false;
    ei::log(eLogInfo,QString("Renderer selection from:%1 to:%2 persist:%3 action:%4")
        .arg(m_selected?"Rebuild64":"OpenGL").arg(selected?"Rebuild64":"OpenGL")
        .arg(persist).arg(action.isEmpty()?QStringLiteral("menu"):action));
    m_gl->setProperty("rendererAction",action.isEmpty()?QStringLiteral("menu"):action);
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
            emit m_gl->updateMsg(tr("OpenGL: подготовка атласов и геометрии. Дождитесь готовности; правки сохранены."));
            m_gl->ensureLegacyQueryContext();m_gl->ensureLegacyWorldResources();
            m_gl->m_pLand->ensureRenderResources();
            for(const auto& neighbour:m_gl->m_neighborLandscapes)if(neighbour.landscape)neighbour.landscape->ensureRenderResources();
            if(m_gl->m_pLand && m_gl->m_pLand->isMprLoad())m_gl->loadNeighborLandscapes();
        }
        for(auto* mob:m_gl->mobs())for(auto* node:mob->nodes())if(node->nodeType()==eUnit)m_gl->m_pLand->projectPosition(node);
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
    m_terrainFlags=0;m_strokeFlags=0;m_terrainDirty=QRect();m_strokeDirty=QRect();m_terrainSubmitted=0;m_terrainPresented=0;m_neighborDirty.clear();m_neighborStroke.clear();
    cancelOutputs(tr("Offscreen отменён: карта закрывается или меняется."));
    if(!m_activeCapturePath.isEmpty())emit captureFinished(m_activeCapturePath,tr("Снимок отменён: карта закрывается или меняется."),m_generation);
    if(!m_selected&&!m_outputOnly)return;
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);if(m_worker->input.opening)m_worker->input.opening->cancel->store(true);}
    m_earlyDocument=false;m_earlyMap.clear();m_visualCache292.reset();
    // Resource-write transitions on the same map retain pending tool input.
    ++m_generation;m_gl->m_nativeQueryPixels.clear();m_queryInput.clear();++m_querySerial;m_queryInFlight=false;m_backendResourceKey.clear();
    if(m_selected)m_gl->setProperty("rebuild64LegacyNeighborsDemanded",false);
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->paused=true;m_worker->retainPausedHandle=true;m_worker->input.documentDirty=true;m_worker->input.generation=m_generation;}
    emit documentTransitionStarted(m_generation);
    m_documentHash.clear();m_documentMapPath.clear();m_sceneSnapshotTimer.invalidate();m_documentRefreshPending=true;
    showPreparation(tr("Открытие карты"));
}
void CRebuild64Viewport::requireOpenGL(const QString& action) {
    if(!m_selected)return;
    ei::log(eLogInfo,QString("Renderer compatibility action:%1").arg(action));
    selectRebuild(false,false,action);
    emit m_gl->updateMsg(tr("%1 пока требует OpenGL. Открыт OpenGL; документы и правки сохранены. Вернуться можно через Настройки → Вид карты.").arg(action));
}
void CRebuild64Viewport::fallback(const QString& message) {
    if(!m_gl)return;
    ei::log(eLogWarning,QString("Renderer fallback action:backend failure detail:%1").arg(message));
    if(m_outputOnly){m_failure=message;m_outputOnly=false;m_timer->stop();
        if(m_worker){QList<Worker::OutputJob> jobs;{std::lock_guard<std::mutex> lock(m_worker->mutex);jobs=m_worker->outputs;m_worker->outputs.clear();}
            for(const auto& job:jobs)emit outputFinished(job.serial,{},message,job.generation,{},0);}
        m_worker.reset();emit backendFailure(message);return;}
    const auto pendingCapture=m_activeCapturePath;m_activeCapturePath.clear();
    m_failure=message;
    // A failed device is an output failure, not a user cancellation. GUI
    // function adapters may complete through their ordinary GL fallback.
    cancelOutputs(message);
    selectRebuild(false,false,QString::fromUtf8("Сбой Rebuild64: ")+message);
    m_worker.reset();
    emit m_gl->updateMsg(QString::fromUtf8("Rebuild64 недоступен. Открыт OpenGL. ")+message);
    if(!pendingCapture.isEmpty())emit captureFinished(pendingCapture,message,m_generation);
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
    result.world_time=m_diagnosticFrame?double(m_diagnosticFrame)/3.0:(m_previewClock.isValid()?m_previewClock.elapsed()/55.0:0.0);
    // The diagnostic freezes animation time, not the renderer's monotonic frame index.
    result.test_frame=0;
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
    result.visuals=(m_gl->m_skyboxVisible?1u:0u)|(m_gl->m_fogVisible?2u:0u)
        |(m_gl->m_landscapeVisible&&m_gl->m_bDrawLand?4u:0u)|(m_gl->m_landscapeVisible&&m_gl->m_bDrawWater?8u:0u)
        |(m_gl->m_staticObjectsVisible||m_gl->m_dynamicObjectsVisible?16u:0u);
    return result;
}
namespace {
void projectNativeUnits289(CView* view) {
    opening::Scope timing("gui.293.projectNativeUnits289");
    auto* land=CLandscape::getInstance();if(!land||!land->isMprLoad())return;
    QVector<QPair<CNode*,QVector3D>> projected;
    for(auto* mob:view->mobs())for(auto* node:mob->nodes())if(node->nodeType()==eUnit||node->modelName().compare("stst8",Qt::CaseInsensitive)==0) {
        auto position=node->position();QVector3D surface;
        if(node->nodeType()==eUnit)surface=land->unitSurfacePosition(position);
        else {surface=QVector3D(position.x(),position.y(),0);land->projectPt(surface);}
        position.setZ(position.z()+surface.z());projected.append(qMakePair(node,position));
    }
    for(const auto& value:projected)value.first->setDrawPosition(value.second);
}
QVector<quintptr> editorMobOrder(CView* view) {
    QVector<quintptr> result;for(auto* mob:view->mobs())if(mob&&view->isMobVisible(mob))result.append(quintptr(mob));return result;
}
QVector<EirObjectPose> editorPoses(CView* view) {
    opening::Scope timing("gui.293.editorPoses");
    QVector<EirObjectPose> poses;uint32_t mobIndex=0;
    for(auto* mob:view->mobs())if(mob&&view->isMobVisible(mob)) {
        for(auto* node:mob->nodes()) {
            EirObjectPose pose{};pose.mob=mobIndex;pose.nid=node->mapId();
            if(view->diplomacyMapMode())if(auto* unit=dynamic_cast<CUnit*>(node)) {
                const auto tint=diplomacy::mapTint(view->diplomacyNpcColor(unit));for(int a=0;a<4;++a)pose.editor_tint[a]=tint[a];
            }
            const auto p=node->drawPosition();const auto q=node->rotation();const auto shape=node->constitution();
            for(int a=0;a<3;++a){pose.position[a]=p[a];pose.shape[a]=shape[a];}
            pose.shape[0]=shape.y();pose.shape[1]=shape.x();
            pose.quaternion[0]=q.scalar();pose.quaternion[1]=q.x();pose.quaternion[2]=q.y();pose.quaternion[3]=q.z();
            pose.visible=node->nodeState()!=ENodeState::eHidden&&!node->isMarkDeleted()&&view->nativeObjectVisible(node);pose.selected=node->nodeState()==ENodeState::eSelect;poses.append(pose);
        }++mobIndex;
    }return poses;
}
}
QByteArray CRebuild64Viewport::editorVisuals292(CNode* only) {
    opening::Scope timing("gui.292.editorVisuals.cached");
    if(!m_visualCache292)m_visualCache292.reset(new VisualCache292);
    auto& cache=*m_visualCache292;QElapsedTimer budget;budget.start();
    const auto resources=configuredResourceFingerprint(m_gl->settings());
    if(resources!=cache.resources){cache.nodes.clear();cache.composites.clear();cache.composing.clear();cache.textures.clear();cache.resources=resources;}
    qint64 coldWorkNs=0;
    QByteArray bytes;QDataStream out(&bytes,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    const auto blob=[&](const QByteArray& value){out<<quint32(value.size());out.writeRawData(value.constData(),value.size());};
    QVector<QPair<quint32,CNode*>> entries;quint32 ordinal=0;
    if(only)entries.append(qMakePair(0u,only));
    else for(auto* mob:m_gl->mobs())if(mob&&m_gl->isMobVisible(mob)){for(auto* node:mob->nodes())entries.append(qMakePair(ordinal,node));++ordinal;}
    QSet<quintptr> live;QSet<QString> emitted;bool pending=false;
    for(const auto& entry:entries) {
        auto* node=entry.second;auto* unit=dynamic_cast<CUnit*>(node);
        if(!unit&&node->modelName().compare("stst8",Qt::CaseInsensitive))continue;
        const auto id=quintptr(node);live.insert(id);
        QJsonObject identity;
        if(!unit){const auto data=node->toJson();identity={{"model",data["Model name"]},{"primaryTexture",data["Primary texture"]},{"secondaryTexture",data["Secondary texture"]},{"visibleParts",data["Object parts"]}};}
        const auto key=unit?unit->nativeVisualCacheKey(resources):QJsonDocument(identity).toJson(QJsonDocument::Compact);
        auto found=cache.nodes.find(id);
        if(found==cache.nodes.end()||found->key!=key) {
            // Snapshot the existing semantic plan without building a duplicate
            // GL character. Pixel composition/encoding below runs on a worker.
            // Charge only cold work: serialization of hits must not starve a
            // later unresolved node in a large document.
            if(coldWorkNs>=12000000){pending=true;continue;}
            QElapsedTimer nodeWork;nodeWork.start();const auto begin=opening::clock();
            auto plan=unit?unit->nativeVisualPlan():identity;
            if(plan.isEmpty()){
                coldWorkNs+=nodeWork.nsecsElapsed();
                if(unit&&unit->needsVisualResolution())pending=true;
                else {VisualCache292::Entry omitted;omitted.key=key;cache.nodes.insert(id,omitted);}
                continue;
            }
            const auto model=plan["model"].toString();
            const auto selectedTexture=[&](const QString& name){
                const auto textureKey=key+model.toUtf8()+"/"+name.toUtf8();const auto k=QString::fromLatin1(QCryptographicHash::hash(textureKey,QCryptographicHash::Sha256).toHex());
                if(cache.textures.contains(k))return cache.textures.value(k);
                for(const auto& candidate:ei::textureResourceCandidates(name,model))if(!CObjectList::getInstance()->resourceData(candidate).isEmpty()){cache.textures[k]=candidate;return candidate;}
                cache.textures[k]=name;return name;
            };
            plan["primaryTexture"]=selectedTexture(plan["primaryTexture"].toString());
            plan["secondaryTexture"]=selectedTexture(plan["secondaryTexture"].toString());
            auto textures=plan["componentTextures"].toObject();for(auto i=textures.begin();i!=textures.end();++i)i.value()=selectedTexture(i.value().toString());
            auto layers=plan["bodyTextureLayers"].toArray();for(int i=0;i<layers.size();++i)layers[i]=selectedTexture(layers[i].toString());
            VisualCache292::Entry value;value.key=key;
            QDataStream packet(&value.body,QIODevice::WriteOnly);packet.setByteOrder(QDataStream::LittleEndian);
            const auto text=[&](const QString& text){const auto raw=text.toUtf8();packet<<quint32(raw.size());packet.writeRawData(raw.constData(),raw.size());};
            const auto list=[&](const QJsonArray& list){packet<<quint32(list.size());for(const auto& v:list)text(v.toString());};
            text(model);text(plan["primaryTexture"].toString());text(plan["secondaryTexture"].toString());
            const auto animation=plan["animation"].toObject();text(animation["actionName"].toString());packet<<quint32(animation["length"].toInt());
            list(plan["visibleParts"].toArray());packet<<quint32(textures.size());for(auto i=textures.begin();i!=textures.end();++i){text(i.key());text(i.value().toString());}
            list(layers);const auto emissions=plan["partEmissions"].toObject();packet<<quint32(emissions.size());for(auto i=emissions.begin();i!=emissions.end();++i)text(i.key());
            if(!layers.isEmpty()) {
                QStringList names;for(const auto& v:layers)names.append(v.toString());
                value.composite=model+"\n"+names.join("\n");
                if(!cache.composites.contains(value.composite)&&!cache.composing.contains(value.composite)) {
                    // Copy only source payloads on Qt. Decoder/compositor/MMP
                    // encoder see immutable values, no QObject/cache/GL access.
                    QVector<QByteArray> payloads;
                    for(const auto& name:names)payloads.append(CObjectList::getInstance()->resourceData(name));
                    cache.composing[value.composite]=QtConcurrent::run([payloads]{
                        QVector<QImage> images;
                        for(const auto& raw:payloads){
                            if(raw.isEmpty()){if(images.isEmpty())return QByteArray();else continue;}
                            QDataStream stream(raw);util::formatStream(stream);SMmpHeader header;stream>>header;
                            if(header.m_signature!=0x00504D4D||header.m_format!=ETextureFormat::eMMP_PNT3){if(images.isEmpty())return QByteArray();else continue;}
                            QImage image(header.m_width,header.m_height,QImage::Format_RGBA64);QString error;
                            if(!ei::decodePnt3Image(raw.mid(76,int(header.m_mipcount)),header.m_width,header.m_height,&image,&error)){if(images.isEmpty())return QByteArray();else continue;}
                            images.append(image);
                        }
                        QImage composite;QString error;
                        if(!ei::compositeTextureLayers(images,&composite,&error))return QByteArray();
                        return ei::encodeMmp8888(composite);
                    });
                }
            }
            found=cache.nodes.insert(id,std::move(value));++cache.builds;
            coldWorkNs+=nodeWork.nsecsElapsed();
            opening::span("gui.292.visual.node",begin,QString::number(node->mapId()));
        } else ++cache.hits;
        if(found->body.isEmpty())continue;
        if(!found->composite.isEmpty()&&!cache.composites.contains(found->composite)) {
            auto job=cache.composing.find(found->composite);
            if(job==cache.composing.end()||!job->isFinished()){pending=true;continue;}
            cache.composites[found->composite]=job->result();cache.composing.erase(job);
        }
        out<<quint32(entry.first)<<quint32(node->mapId())<<quint32(unit?1:0);
        out.writeRawData(found->body.constData(),found->body.size());
        if(!found->composite.isEmpty()&&!emitted.contains(found->composite)){blob(cache.composites.value(found->composite));emitted.insert(found->composite);}else blob({});
    }
    if(!only){
        for(auto i=cache.nodes.begin();i!=cache.nodes.end();)if(!live.contains(i.key()))i=cache.nodes.erase(i);else ++i;
        m_visualsPending292=pending;m_gl->setProperty("nativeUnitsPending289",pending);
        m_gl->setProperty("nativeVisualBuilds292",QVariant::fromValue(cache.builds));
        m_gl->setProperty("nativeVisualHits292",QVariant::fromValue(cache.hits));
    }
    return bytes;
}
void CRebuild64Viewport::updateLiveCamera() {
            if(m_worker) {
                // Initializing/resizing the hidden QGLWidget must not replace
                // the native viewport's lens while tool geometry is loading.
                const int logicalWidth=qMax(1,width()),logicalHeight=qMax(1,height());
                m_gl->m_width=logicalWidth;m_gl->m_height=logicalHeight;
                m_gl->m_normalProjection(0,0)=m_gl->m_normalProjection(1,1)*float(logicalHeight)/logicalWidth;
                m_gl->updateCameraProjection();
                const auto view=m_gl->m_cam->update(),clip=m_gl->m_projection*view;
                const auto inverse=view.inverted();const auto eye=inverse.map(QVector3D()),target=inverse.map(QVector3D(0,0,-1));
                latency293::Lock lock(m_worker->mutex,latency293::LiveCameraWait);auto& camera=m_worker->input.camera;
                for(int row=0;row<4;++row)for(int col=0;col<4;++col)camera.world_to_clip[row*4+col]=row==2?.5f*(clip(2,col)+clip(3,col)):clip(row,col);
                for(int axis=0;axis<3;++axis){camera.eye[axis]=eye[axis];camera.target[axis]=target[axis];}
                m_worker->input.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
            }
}
void CRebuild64Viewport::tick() {
    latency293::Scope latencyTiming(latency293::Tick);
    opening::Scope tickTiming("gui.native.tick");
    if(!m_selected&&m_gl&&!m_waitingInput.isEmpty()) {
        auto* main=qobject_cast<MainWindow*>(window());
        if(main&&main->rebuild64DocumentReady()&&!m_gl->m_unitVisualPreparationSuspended&&!m_gl->m_openingProjectionDepth&&editorQueryReady()&&!ei::legacyProjectionPulse()) {
            m_earlyDocument=false;replayEditorInput();m_timer->stop();
        }
        return;
    }
    if((!m_selected&&!m_outputOnly)||!m_gl||m_gl->m_applicationShutdown)return;
    // Tool-context initialization can restart CView's uncapped GL timer after
    // selectRebuild stopped it. Native owns the display; do not leave a hidden
    // zero timer flooding Qt while its no-op updateWindow cannot draw anything.
    if(m_selected&&m_gl->m_timer&&m_gl->m_timer->isActive()){
        latency293::state().legacyStops.fetch_add(1);latency293::state().legacyInterval.store(m_gl->m_timer->interval());
        m_gl->suspendRendering();
    }
    updatePreparationText();
    if(m_restartWhenFinished){
        if(m_worker&&!m_worker->finished.load())return;
        m_worker.reset();m_restartWhenFinished=false;m_backendResourceKey.clear();m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();
    }
    if(m_resourceWriteDepth||window()->property("nativeMapRequestPending").toBool()||window()->property("aiCommitBusy").toBool())return;
    const bool openingBusy=m_gl->m_openingProjectionDepth||m_gl->m_unitVisualPreparationSuspended||!editorQueryReady();
    if(openingBusy)updateLiveCamera();
    if(!m_earlyDocument && !m_waitingInput.isEmpty() && frameIsCurrent() && editorQueryReady()){
        replayEditorInput();
    }
    if(m_earlyDocument) {
        auto* main=qobject_cast<MainWindow*>(window());
        const bool ready=main&&main->rebuild64DocumentReady()&&!m_gl->m_unitVisualPreparationSuspended&&!m_gl->m_openingProjectionDepth
            &&!qApp->property("rebuild64StartupDocumentBatch").toBool();
        if(!ready) {
            // Camera motion does not require the tool projection. Keep the raw
            // document's environment/light facts while updating the shared lens.
            if(!openingBusy)updateLiveCamera();
            return;
        }
        // Reconcile with the canonical editable document before enabling tools.
        // A mismatch is submitted as a new guarded generation, never ignored.
        m_earlyDocument=false;m_documentRefreshPending=true;
        m_sceneSnapshotTimer.invalidate();
    }
    if(ei::legacyProjectionPulse()||m_gl->m_unitVisualPreparationSuspended||m_gl->m_openingProjectionDepth || qApp->property("rebuild64StartupDocumentBatch").toBool())return;
    if(auto* main=qobject_cast<MainWindow*>(window()))if(!main->rebuild64DocumentReady())return;
    if(!m_gl->m_pLand || !m_gl->m_pLand->isMprLoad()) {
        if(m_outputOnly) {
            cancelOutputs(tr("Offscreen отменён: карта закрыта."));
            if(m_worker){{latency293::Lock lock(m_worker->mutex,latency293::TickCameraWait);m_worker->stop=true;}m_worker->wake.notify_all();}
            m_outputOnly=false;m_statistics={};m_timer->stop();return;
        }

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
    QByteArray visibility=QByteArray::number(m_gl->m_staticObjectsVisible)+QByteArray::number(m_gl->m_dynamicObjectsVisible);
    for(auto* mob:m_gl->m_aMob)if(m_gl->isMobVisible(mob))for(auto* node:mob->nodes())
        if(node->nodeState()==ENodeState::eHidden||node->isMarkDeleted())visibility+=QByteArray::number(quintptr(mob))+":"+QByteArray::number(node->mapId())+";";
    if(m_gl->property("nativeUnitsPending289").toBool())m_documentRefreshPending=true;
    const bool cohortChanged=m_worker&&(mobOrder!=m_documentMobOrder||visibility!=m_documentVisibility);
    // MOB ordinals belong to one immutable generation. Never apply the new
    // visible cohort's poses to the preceding generation's numeric IDs.
    if(cohortChanged){m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();}
    const bool preview=m_gl->hasPendingOperation();
    if(preview&&!m_transformPreviewMessage)emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: предпросмотр преобразования. Мир обновится после подтверждения."));
    m_transformPreviewMessage=preview;
    // A camera gesture changes no object pose or editor geometry. Publish only
    // its lens; mutations and visibility/cohort changes take the ordinary path.
    if(m_worker&&!cohortChanged&&!m_serviceDirty293&&!m_documentRefreshPending
       &&!preview&&!m_terrainFlags&&m_neighborDirty.isEmpty()
       &&(m_serviceOrbit293||m_gl->m_cam->movementInputActive())){
        updateLiveCamera();return;
    }
    projectNativeUnits289(m_gl);
    if(!m_outputOnly)serviceInput();
    if(m_worker&&!cohortChanged&&(m_terrainFlags||!m_neighborDirty.isEmpty())&&submitTerrain()) {
        updateLiveCamera();return;
    }
    if(m_worker&&m_sceneSnapshotTimer.isValid()&&m_sceneSnapshotTimer.elapsed()<100) {
        const auto camera=cameraSnapshot();
        const QSize size(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
        {latency293::Lock lock(m_worker->mutex,latency293::TickCameraWait);m_worker->input.camera=camera;m_worker->input.size=size;m_worker->paused=false;}
        return;
    }
    m_sceneSnapshotTimer.restart();
    const auto terrainFingerprintStart=opening::clock();
    const auto terrainState=m_gl->m_terrainEditAvailable?m_gl->m_terrainEdit.stateSha256():QByteArray();
    opening::span("gui.native.terrain_fingerprint",terrainFingerprintStart);
    if(m_worker&&!cohortChanged&&(!m_documentRefreshPending || m_pointerDown || (m_gl->hasPendingOperation()&&!m_gl->m_pendingDatabasePlacement))&&terrainState==m_terrainState) {
        const auto camera=cameraSnapshot();
        latency293::Lock lock(m_worker->mutex,latency293::TickCameraWait);m_worker->input.camera=camera;
        m_worker->input.size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));
        return;
    }
    const auto serializationStart=opening::clock();
    m_documentMobOrder=mobOrder;m_documentVisibility=visibility;
    m_documentMapPath=m_gl->m_pLand->filePath().absoluteFilePath();
    m_terrainState=terrainState;m_documentRefreshPending=false;m_mobDocumentRefresh=false;
    m_documentPoses.clear();m_poseOverrides.clear();
    for(const auto& pose:editorPoses(m_gl))m_documentPoses.insert((quint64(pose.mob)<<32)|pose.nid,pose);
    Worker::Input next;
    next.visuals=editorVisuals292();
    projectNativeUnits289(m_gl);next.documentObjects=editorPoses(m_gl);
    next.atlas=Worker::atlasSource(m_gl);
    CMprEditTransaction transaction(QStringLiteral("Rebuild64 memory snapshot"));CValidationReport report;
    if(!m_gl->m_terrainEditAvailable) {
        // CView defers its editor model after visible load. Its original bytes
        // are already owned in memory; preview must not reopen the MPR meanwhile.
        next.terrain=m_gl->m_pLand->originalMprData();
    } else if(!m_gl->m_terrainEdit.appendSectorEdits(&transaction,&report)
       || !(next.atlas?m_gl->m_mprDocument.appendMaxZEdit(m_gl->m_terrainEdit.maxZ(),&transaction,&report)
            :m_gl->m_mprDocument.appendMapParametersEdit(m_gl->m_terrainEdit.maxZ(),m_gl->m_mprMaterials,m_gl->m_mprAnimatedTiles,&transaction,&report))
       || !m_gl->m_mprDocument.applyTransaction(transaction,&next.terrain,&report)) {
        fallback(QString::fromUtf8("Не удалось создать снимок рельефа из памяти."));return;
    }
    if(next.terrain.isEmpty()){fallback(QString::fromUtf8("В памяти нет загруженного рельефа."));return;}
    for(auto* mob:m_gl->m_aMob)if(mob&&m_gl->isMobVisible(mob)) {
        opening::Scope timing("gui.293.serializeMob");
        QByteArray bytes;mob->serializeMob(bytes,false);next.mobs.append(bytes);
    }
    opening::span("gui.native.document_serialize",serializationStart);
    next.name=m_gl->m_pLand->filePath().fileName().toUtf8();
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(next.name);hash.addData(next.terrain);
    for(const auto& mob:next.mobs){hash.addData(QByteArray::number(mob.size()));hash.addData(mob);}
    hash.addData(visibility);hash.addData(next.visuals);
    for(const auto& placement:m_gl->m_neighborPlacements){hash.addData(placement.mprPath.toUtf8());hash.addData(QByteArray::number(placement.origin.x(),'g',17));hash.addData(QByteArray::number(placement.origin.y(),'g',17));hash.addData(QByteArray::number(placement.size.width(),'g',17));hash.addData(QByteArray::number(placement.size.height(),'g',17));}
    const auto fingerprint=hash.result();const bool documentChanged=fingerprint!=m_documentHash;if(documentChanged){m_documentHash=fingerprint;++m_generation;m_gl->m_nativeQueryPixels.clear();m_terrainSubmitted=0;m_terrainPresented=0;m_terrainFlags=0;m_terrainDirty=QRect();m_strokeDirty=QRect();m_strokeFlags=0;}
    if(documentChanged&&m_worker)emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: обновление вида после правки; пока показан последний готовый кадр. Правки находятся в документе."));
    next.generation=m_generation;next.camera=cameraSnapshot();
    if(documentChanged)for(auto it=m_gl->m_worldTerrainSession.documents.cbegin();it!=m_gl->m_worldTerrainSession.documents.cend();++it)
        if(it.key()!=CWorldTerrainSession::key(m_documentMapPath)) {
            const auto& doc=*it.value();
            queueNeighborTerrain(doc.path,QRect(0,0,doc.baseline.mapInfo().sectorCountX,doc.baseline.mapInfo().sectorCountY),9u);
        }
    {opening::Scope stage("gui.native.resource_snapshot");next.resources(m_gl->settings(),QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF()))));}
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
    const auto inputLockStart293=opening::clock();
    {latency293::Lock lock(m_worker->mutex,latency293::TickCameraWait);opening::span("gui.293.publishInput.lockWait",inputLockStart293);opening::Scope inputTiming("gui.293.publishInput.assign");next.query=m_worker->input.query;next.serviceScene=m_worker->input.serviceScene;next.capture=m_worker->input.capture;next.captureGeneration=m_worker->input.captureGeneration;next.captureServiceSerial=m_worker->input.captureServiceSerial;next.captureWorld=m_worker->input.captureWorld;next.serviceSerial=m_worker->input.serviceSerial;next.serviceCpuMs=m_worker->input.serviceCpuMs;if(!documentChanged){next.service=m_worker->input.service;next.poses=m_worker->input.poses;}m_worker->input=std::move(next);m_worker->paused=false;}
    m_worker->wake.notify_all();
    if(!m_waitingInput.isEmpty() && frameIsCurrent() && editorQueryReady()){replayEditorInput();}
}
// Terrain notifications never change the canonical document or its undo/save path.
void CRebuild64Viewport::queueTerrain(const QRect& rect,quint32 flags) {
    if(!m_selected)return;
    // Native pick/depth is cached by camera AND terrain revision. Preserve
    // queued gestures, but never replay an old depth map over the new terrain.
    m_gl->m_nativeQueryPixels.clear();++m_querySerial;m_queryInFlight=false;
    for(const auto& pending:m_queryInput)m_waitingInput.append(pending);m_queryInput.clear();
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.query.reset();}
    if(flags&16u)m_terrainDirty=QRect(0,0,int(m_gl->m_mprDocument.mapInfo().sectorCountX),int(m_gl->m_mprDocument.mapInfo().sectorCountY));
    if(!rect.isEmpty()){m_terrainDirty=m_terrainDirty.united(rect);m_strokeDirty=m_strokeDirty.united(rect);m_strokeFlags|=flags&7u;}
    if((flags&8u)&&m_terrainDirty.isEmpty())m_terrainDirty=m_strokeDirty;
    if(m_terrainDirty.isEmpty())return;
    m_terrainFlags|=flags|m_strokeFlags;
    if(flags&8u){m_strokeDirty=QRect();m_strokeFlags=0;}
}
void CRebuild64Viewport::reloadScene() {
    if(!m_selected&&!m_outputOnly)return;
    m_terrainFlags=m_strokeFlags=0;m_terrainDirty=m_strokeDirty=QRect();
    m_documentHash.clear();m_documentRefreshPending=true;m_sceneSnapshotTimer.invalidate();
    m_gl->m_nativeQueryPixels.clear();++m_querySerial;m_queryInFlight=false;
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.documentDirty=true;m_worker->input.terrainUpdate={};m_worker->input.neighborUpdates.clear();}
}
void CRebuild64Viewport::queueNeighborTerrain(const QString& path,const QRect& rect,quint32 flags) {
    if(!m_selected&&!m_outputOnly)return;
    const auto key=CWorldTerrainSession::key(path);
    auto stroke=m_neighborStroke.value(key);
    if(!rect.isEmpty()){stroke.first=stroke.first.united(rect);stroke.second|=flags&7u;}
    auto& dirty=m_neighborDirty[key];dirty.first=dirty.first.united(rect);dirty.second|=flags|stroke.second;
    if((flags&8u)&&dirty.first.isEmpty())dirty.first=stroke.first;
    if(flags&8u)m_neighborStroke.remove(key);else m_neighborStroke[key]=stroke;
    if(dirty.first.isEmpty())m_neighborDirty.remove(key);
    m_gl->m_nativeQueryPixels.clear();
}
bool CRebuild64Viewport::submitTerrain() {
    if(!m_worker||!m_gl->m_terrainEditAvailable||m_earlyDocument||m_mobDocumentRefresh)return false;
    {std::lock_guard<std::mutex> lock(m_worker->mutex);
        if(m_worker->input.generation!=m_generation||m_worker->input.name!=m_gl->m_pLand->filePath().fileName().toUtf8())return false;}
    Worker::TerrainPacket packet;CValidationReport report;
    if(m_terrainFlags&18u)packet.atlas=Worker::atlasSource(m_gl);
    CMprEditTransaction transaction(QStringLiteral("Rebuild64 brush snapshot"));
    if(!m_gl->m_terrainEdit.appendSectorEdits(&transaction,&report)
       ||!(packet.atlas?m_gl->m_mprDocument.appendMaxZEdit(m_gl->m_terrainEdit.maxZ(),&transaction,&report)
            :m_gl->m_mprDocument.appendMapParametersEdit(m_gl->m_terrainEdit.maxZ(),m_gl->m_mprMaterials,m_gl->m_mprAnimatedTiles,&transaction,&report))
       ||!m_gl->m_mprDocument.applyTransaction(transaction,&packet.bytes,&report)) {
        fallback(tr("РќРµ СѓРґР°Р»РѕСЃСЊ РїРѕРґРіРѕС‚РѕРІРёС‚СЊ РѕР±РЅРѕРІР»РµРЅРёРµ РєРёСЃС‚Рё: ")+report.toText());return false;
    }
    packet.name=m_gl->m_pLand->filePath().fileName().toUtf8();
    packet.rect=m_terrainDirty.intersected(QRect(0,0,int(m_gl->m_mprDocument.mapInfo().sectorCountX),int(m_gl->m_mprDocument.mapInfo().sectorCountY)));
    packet.preserveObjects=m_gl->m_landscapeTools&&!m_gl->m_landscapeTools->moveMobObjectsWithTerrain();
    packet.flags=m_terrainFlags;packet.serial=++m_terrainSubmitted;
    QMap<QByteArray,Worker::TerrainPacket> neighbors;
    for(auto it=m_neighborDirty.cbegin();it!=m_neighborDirty.cend();++it) {
        const auto doc=m_gl->m_worldTerrainSession.documents.value(it.key());if(!doc)continue;
        Worker::TerrainPacket n;CMprEditTransaction tx(QStringLiteral("Rebuild64 neighbour brush"));
        if(!doc->terrain.appendSectorEdits(&tx,&report)
           ||!doc->baseline.appendMapParametersEdit(doc->terrain.maxZ(),doc->materials,doc->baseline.mapInfo().animatedTiles,&tx,&report)
           ||!doc->baseline.applyTransaction(tx,&n.bytes,&report)){fallback(report.toText());return false;}
        n.name=QFileInfo(doc->path).fileName().toUtf8();n.rect=it->first;n.flags=it->second;n.serial=packet.serial;neighbors[n.name]=std::move(n);
    }
    m_terrainState=m_gl->m_terrainEdit.stateSha256();m_documentRefreshPending=false;
    {std::lock_guard<std::mutex> lock(m_worker->mutex);
        auto& pending=m_worker->input.terrainUpdate;
        packet.rect=packet.rect.united(pending.rect);packet.flags|=pending.flags;
        pending=std::move(packet);m_worker->input.documentDirty=false;
        for(auto it=neighbors.begin();it!=neighbors.end();++it){auto& old=m_worker->input.neighborUpdates[it.key()];it->rect=it->rect.united(old.rect);it->flags|=old.flags;old=std::move(it.value());}
    }
    m_neighborDirty.clear();
    m_terrainFlags=0;m_terrainDirty=QRect();m_worker->wake.notify_all();return true;
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
    m_activeCapturePath=path;
    if(m_selected){projectNativeUnits289(m_gl);serviceInput();}
    if(m_worker){std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.capture=path;m_worker->input.captureGeneration=m_generation;m_worker->input.captureServiceSerial=m_worker->input.serviceSerial;m_worker->input.captureWorld=worldOnly;}
}
QImage CRebuild64Viewport::emptyViewportImage() {
    if(!m_selected||!m_gl||m_gl->m_pLand->isMprLoad()||!m_gl->m_aMob.isEmpty())return {};
    QImage image(m_preparation->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);m_preparation->render(&image);return image;
}
void CRebuild64Viewport::cancelCapture(const QString& path) {
    if(m_activeCapturePath==path)m_activeCapturePath.clear();
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
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseButtonDblClick||event->type()==QEvent::MouseMove){
            const auto pos=static_cast<QMouseEvent*>(event)->pos();
            if(!m_gl->property("rebuild64NativeLegend").toBool()&&m_gl->m_navmeshPanel&&m_gl->m_navmeshPanel->layerVisible()&&m_gl->m_navmeshPanel->legendVisible()&&m_gl->m_navmeshLegendRect.contains(pos)){event->accept();return true;}
            for(const auto& panel:m_panels)if(panel&&panel->isVisible()&&panel->parentWidget()==m_native&&panel->geometry().contains(pos)){event->accept();return true;}
        }
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
void CRebuild64Viewport::queueEditorInput(std::shared_ptr<QEvent> event) {
    WaitingInput input;input.event=std::move(event);input.view=m_gl->m_cam->viewMatrix();input.projection=m_gl->m_projection;input.extent=size();m_waitingInput.append(input);
}
void CRebuild64Viewport::replayEditorInput() {
    auto pending=std::move(m_waitingInput);m_waitingInput.clear();
    for(const auto& input:pending) {
        if(size()!=input.extent) {
            emit m_gl->updateMsg(tr("Отложенный ввод отменён: размер вида изменился. Повторите выбор и правку; документ сохранён."));return;
        }
        bool shortcut=false;
        if(input.event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(input.event.get());
            const QKeySequence sequence(int(key->modifiers())|key->key());
            for(const char* name:{"actionUndo","actionRedo"})if(auto* action=window()->findChild<QAction*>(name))
                if(action->shortcut()==sequence){action->trigger();shortcut=true;break;}}
        if(!shortcut){m_replayInput=&input;editorInput(input.event.get());m_replayInput=nullptr;}
        m_gl->m_cam->clearPreviewView();
    }
    syncEditorViewport();
}
void CRebuild64Viewport::editorInput(QEvent* event) {
    latency293::Scope latencyTiming(latency293::Input);
    if(event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseButtonDblClick){
        m_lastMouse=static_cast<QMouseEvent*>(event)->pos();m_gl->setProperty("nativePointer",m_lastMouse);
    }
    if(event->type()==QEvent::FocusOut){
        m_serviceOrbit293=false;
        m_gl->m_pOp->keyManager()->keys().clear();
        if((m_earlyDocument||!editorQueryReady())&&!m_waitingInput.isEmpty())queueEditorInput(std::make_shared<QFocusEvent>(*static_cast<QFocusEvent*>(event)));
        return;
    }
    // Navigation uses the same CView / COperation / CCamera handlers. It has
    // no dependency on object geometry or a terrain atlas. Never queue it.
    bool navigation=event->type()==QEvent::Wheel;
    if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease) {
        auto* key=static_cast<QKeyEvent*>(event);
        navigation=!(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier))
            &&QVector<int>{Qt::Key_W,Qt::Key_A,Qt::Key_S,Qt::Key_D,Qt::Key_Q,Qt::Key_E,Qt::Key_Up,Qt::Key_Down,Qt::Key_Left,Qt::Key_Right,Qt::Key_Shift}.contains(key->key());
    }
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseMove) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        navigation=mouse->button()==Qt::MiddleButton||(mouse->buttons()&Qt::MiddleButton);
    }
    if(navigation&&!m_replayInput&&m_selected) {
        if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseMove)
            m_serviceOrbit293=bool(static_cast<QMouseEvent*>(event)->buttons()&Qt::MiddleButton);
        // CPU-only extent/lens update: no GL initialization during preparation.
        m_gl->m_width=qMax(1,width());m_gl->m_height=qMax(1,height());
        m_gl->m_normalProjection(0,0)=m_gl->m_normalProjection(1,1)*float(m_gl->m_height)/m_gl->m_width;m_gl->updateCameraProjection();
        QApplication::sendEvent(m_gl,event);tick();return;
    }
    m_serviceDirty293=true; // Non-navigation editor input may change helpers/brushes.
    if(m_earlyDocument || !editorQueryReady()) {
        if(event->type()==QEvent::MouseMove && static_cast<QMouseEvent*>(event)->buttons()==Qt::NoButton && m_waitingInput.isEmpty())return;
        if(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)
            queueEditorInput(std::make_shared<QKeyEvent>(*static_cast<QKeyEvent*>(event)));
        else if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease||event->type()==QEvent::MouseButtonDblClick||event->type()==QEvent::MouseMove)
            queueEditorInput(std::make_shared<QMouseEvent>(*static_cast<QMouseEvent*>(event)));
        else if(event->type()==QEvent::ContextMenu)queueEditorInput(std::make_shared<QContextMenuEvent>(*static_cast<QContextMenuEvent*>(event)));
        else if(event->type()==QEvent::Wheel)queueEditorInput(std::make_shared<QWheelEvent>(*static_cast<QWheelEvent*>(event)));
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
    if(event->type()==QEvent::MouseButtonDblClick)syncEditorViewport();
    if(m_selected&&!m_queryReplaying&&queryEditorInput(event)){event->accept();return;}
    const bool mouse=event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease;
    if(mouse) {
        auto* e=static_cast<QMouseEvent*>(event);
        if(event->type()==QEvent::MouseButtonPress&&e->button()==Qt::LeftButton
           &&!m_gl->hasPendingOperation()
           &&(m_documentRefreshPending||m_statistics.ready_generation!=m_generation)) {
            emit m_gl->updateMsg(QString::fromUtf8("Rebuild64: кадр после правки ещё обновляется. Дождитесь готового кадра перед новым выбором мышью; Undo и сохранение доступны."));
            event->accept();return;
        }
        if(event->type()==QEvent::MouseButtonPress)m_pointerDown=true;
        if(event->type()==QEvent::MouseButtonRelease)m_pointerDown=e->buttons()!=Qt::NoButton;
    }
    // Native surface and hidden CView share one logical rectangle. Deliver the
    // original event (native key/modifiers included) to the canonical Qt widget.
    syncEditorViewport();
    const auto projection=m_gl->m_projection;
    auto* keys=m_gl->m_pOp->keyManager();
    const bool shift=keys->isPressed(eKey_Shift),control=keys->isPressed(eKey_Ctrl),alt=keys->isPressed(eKey_Alt);
    const auto modifier=[&](EKeyCode key,bool pressed){if(pressed)keys->press(key);else keys->release(key);};
    if(m_replayInput){
        m_gl->m_projection=m_replayInput->projection;m_gl->m_cam->setPreviewView(m_replayInput->view);
        const auto mods=static_cast<QInputEvent*>(event)->modifiers();modifier(eKey_Shift,mods&Qt::ShiftModifier);modifier(eKey_Ctrl,mods&Qt::ControlModifier);modifier(eKey_Alt,mods&Qt::AltModifier);
    }
    projectNativeUnits289(m_gl);QApplication::sendEvent(m_gl,event);
    if(m_replayInput){m_gl->m_projection=projection;modifier(eKey_Shift,shift);modifier(eKey_Ctrl,control);modifier(eKey_Alt,alt);}
    if(m_selected && mouse && event->type()==QEvent::MouseButtonPress)m_native->setFocus(Qt::MouseFocusReason);
}
bool CRebuild64Viewport::queryEditorInput(QEvent* event) {
    // Brush ray queries use the canonical CPU terrain during the gesture.
    // Object/ground selection resumes native ID/depth after leaving the tool.
    if(m_gl->m_landscapeTools&&m_gl->m_landscapeTools->editingEnabled()&&m_gl->m_terrainEditAvailable)return false;

    std::shared_ptr<QEvent> copy;
    switch(event->type()){
    case QEvent::MouseMove:case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:case QEvent::MouseButtonDblClick:
        copy=std::make_shared<QMouseEvent>(*static_cast<QMouseEvent*>(event));break;
    case QEvent::ContextMenu:copy=std::make_shared<QContextMenuEvent>(*static_cast<QContextMenuEvent*>(event));break;
    case QEvent::KeyPress:{auto* key=static_cast<QKeyEvent*>(event);if(key->key()!=Qt::Key_G&&key->key()!=Qt::Key_R)return false;copy=std::make_shared<QKeyEvent>(*key);break;}
    default:return false;
    }
    WaitingInput input;
    if(m_replayInput)input=*m_replayInput;
    else{input.view=m_gl->m_cam->viewMatrix();input.projection=m_gl->m_projection;input.extent=size();}
    input.event=copy;const auto clip=input.projection*input.view;
    if(event->type()==QEvent::MouseButtonPress)m_gl->m_nativeQueryPixels.clear(); // animated UNIT ownership must use this click
    if(!m_gl->m_nativeQueryPixels.isEmpty()&&m_gl->m_nativeQueryClip==clip&&m_gl->m_nativeQuerySize==QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF()))))return false;
    if(!frameIsCurrent()||!m_worker){queueEditorInput(copy);return true;}
    // Coalesce hover events while retaining press/release/key order.
    if(event->type()==QEvent::MouseMove&&!m_queryInput.isEmpty()&&m_queryInput.back().event->type()==QEvent::MouseMove)m_queryInput.back()=input;
    else m_queryInput.append(input);
    if(m_queryInFlight)return true;
    auto request=std::make_shared<Worker::Query>();request->serial=++m_querySerial;request->generation=m_generation;request->terrainSerial=m_terrainSubmitted;request->clip=clip;request->logicalSize=size();
    request->size=QSize(qMax(1,qRound(width()*devicePixelRatioF())),qMax(1,qRound(height()*devicePixelRatioF())));request->camera=cameraSnapshot();
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)request->camera.world_to_clip[r*4+c]=r==2?.5f*(clip(2,c)+clip(3,c)):clip(r,c);
    const auto inverse=input.view.inverted();const auto eye=inverse.map(QVector3D()),target=inverse.map(QVector3D(0,0,-1));for(int a=0;a<3;++a){request->camera.eye[a]=eye[a];request->camera.target[a]=target[a];}
    m_queryInFlight=true;{std::lock_guard<std::mutex> lock(m_worker->mutex);m_worker->input.query=request;}m_worker->wake.notify_all();return true;
}
bool CRebuild64Viewport::editorQueryReady() const {
    if(m_gl->m_openingProjectionDepth)return false;
    if(m_selected||!m_gl->m_neighborLandscapesVisible)return true;
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
    latency293::Scope latencyTiming(latency293::Service);
    opening::Scope serviceTiming("gui.service.total");
    if(window()->property("aiCommitBusy").toBool())return;
    if(!m_worker)return;
    if(editorMobOrder(m_gl)!=m_documentMobOrder)return;
    syncEditorViewport();
    if(m_stack->currentWidget()==m_native)m_gl->m_cam->update();
    const QSize size(qMax(1,qRound(m_native->width()*devicePixelRatioF())),qMax(1,qRound(m_native->height()*devicePixelRatioF())));
    movePanels(true);
    const auto clip=m_gl->m_projection*m_gl->m_cam->viewMatrix();
    QByteArray options;QDataStream optionStream(&options,QIODevice::WriteOnly);
    for(const char* key:{"drawLogic","drawHelp"}){const auto* o=dynamic_cast<const COptBool*>(m_gl->m_pSettings->opt(key));optionStream<<bool(o&&o->value());}
    optionStream<<m_generation<<quint64(quintptr(m_gl->m_activeMob))<<int(CScene::getInstance()->getMode())<<m_gl->m_objectHelpersVisible<<size;
    std::shared_ptr<const CNativeEditorService> scene;
    // Camera-only publication reuses world geometry; all editor mutations,
    // visibility changes and generation/extent changes invalidate this reuse.
    if(!m_serviceDirty293&&options==m_serviceOptions293&&(m_serviceOrbit293||m_gl->m_cam->movementInputActive()||clip!=m_serviceCamera293)){
        latency293::Lock lock(m_worker->mutex,latency293::ReuseWait);scene=m_worker->input.serviceScene;
    }
    const bool rebuilt=!scene;
    if(rebuilt){latency293::Scope snapshotTiming(latency293::Snapshot);auto snapshot=std::make_shared<CNativeEditorService>(m_gl->nativeServiceSnapshot());snapshot->generation=m_generation;scene=std::move(snapshot);}
    m_serviceDirty293=false;m_serviceCamera293=clip;m_serviceOptions293=options;
    const auto poseStart=opening::clock();
    auto poses=editorPoses(m_gl);
    for(auto& pose:poses) {
        const auto key=(quint64(pose.mob)<<32)|pose.nid;
        const auto baseline=m_documentPoses.constFind(key);
        if(baseline!=m_documentPoses.cend() &&
           (std::memcmp(pose.position,baseline->position,sizeof(pose.position)) || std::memcmp(pose.quaternion,baseline->quaternion,sizeof(pose.quaternion)) || std::memcmp(pose.shape,baseline->shape,sizeof(pose.shape))))m_poseOverrides.insert(key);
        pose.changed=(m_poseOverrides.contains(key)||m_gl->property("nativeTerrainPoses293").toBool())?1u:0u;
    }
    m_gl->setProperty("nativeTerrainPoses293",false);
    opening::span("gui.service.poses",poseStart);
    latency293::Lock lock(m_worker->mutex,latency293::PublishWait);
    m_worker->input.camera=cameraSnapshot();m_worker->input.size=size;
    m_worker->input.editorGeometryReady=editorQueryReady();m_worker->input.serviceCpuMs=m_serviceMilliseconds;m_worker->input.serviceScene=std::move(scene);m_worker->input.operationPreview=m_gl->hasPendingOperation();m_worker->input.poses=std::move(poses);if(rebuilt)++m_worker->input.serviceSerial;
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
    m_serviceDirty293=true;
    QWidget::resizeEvent(event);if(m_stopping||!m_gl||!m_selected||event->size().isEmpty())return;
    const auto extent=event->size();m_gl->m_width=extent.width();m_gl->m_height=extent.height();
    m_gl->m_normalProjection.setToIdentity();m_gl->m_normalProjection.perspective(45,float(extent.width())/extent.height(),1,1000);
    m_gl->updateCameraProjection();m_editorViewportDirty=true;
    QTimer::singleShot(0,this,&CRebuild64Viewport::tick);
}

QByteArray CRebuild64Viewport::appearanceVisuals(CNode* node){return editorVisuals292(node);}
quint64 CRebuild64Viewport::requestOutput(const QSize& size,int mode,const EirCamera& camera,const QString& model,const QString& texture,const QVector3D& shape,const QVector<float>& preview,const QVector<quint64>& objectKeys,const QString& secondary,const QStringList& parts,const QByteArray& modelBytes,const QByteArray& boneBytes,const QByteArray& textureBytes,const QByteArray& visuals) {
    const auto serial=++m_outputSerial;
    if(!m_selected&&qApp->property("agentHeadless").toBool()&&qApp->property("agentNativeOutput").toBool()) {
        if(!m_gl->m_pLand||!m_gl->m_pLand->isMprLoad()) {
            QTimer::singleShot(0,this,[this,serial]{emit outputFinished(serial,{},tr("Для native-вывода сначала откройте MPR."),0,{},0);});return serial;
        }
        if(m_worker&&m_worker->finished.load())m_worker.reset();

        m_outputOnly=true;m_documentRefreshPending=true;m_timer->start();tick();
    }
    if((!m_selected&&!m_outputOnly)||!m_worker||(mode!=3&&!m_outputOnly&&!frameIsCurrent())) {
        QTimer::singleShot(0,this,[this,serial]{emit outputFinished(serial,{},tr("Rebuild64: мир, материалы и соседние карты ещё готовятся. Повторите вывод после готовности кадра."),0,{},0);});return serial;
    }
    {std::lock_guard<std::mutex> lock(m_worker->mutex);
        if(m_worker->outputs.size()>=8){QTimer::singleShot(0,this,[this,serial]{emit outputFinished(serial,{},tr("Rebuild64: очередь вывода заполнена. Повторите запрос."),0,{},0);});return serial;}
        m_worker->outputs.append({serial,m_generation,size,mode,camera,model.toUtf8(),texture.toUtf8(),shape,preview,objectKeys,secondary.toUtf8(),parts.join("\n").toUtf8(),modelBytes,boneBytes,textureBytes,visuals});}
    m_worker->wake.notify_all();return serial;
}

void CRebuild64Viewport::cancelOutputs(const QString& reason) {
    if(!m_worker)return;QList<Worker::OutputJob> jobs;
    {std::lock_guard<std::mutex> lock(m_worker->mutex);jobs=m_worker->outputs;m_worker->outputs.clear();}
    for(const auto& job:jobs)emit outputFinished(job.serial,{},reason,job.generation,{},0);
}

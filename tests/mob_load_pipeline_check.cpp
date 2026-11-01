#include <QtWidgets>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "settings.h"
#include "camera.h"
#include "render_performance.h"
#include "profile_database.h"
#include "resource_profile.h"
#include "unit.h"
#include "ai_agent_edit.h"
#include "endurance_document.h"
#include "collection_document.h"
#include <QtEndian>

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object)) if(widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show) if(auto* dialog=qobject_cast<QMessageBox*>(object))
            QTimer::singleShot(0,dialog,[dialog]() {
                if(auto* cancel=dialog->button(QMessageBox::Cancel))cancel->click();
                else dialog->reject();
            });
        return false;
    }
};
QByteArray bytes(const QString& path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& path,const QByteArray& data) {QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw path;}
QByteArray u32(quint32 v){QByteArray b(4,'\0');qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()));return b;}
QByteArray section(quint32 id,const QByteArray& b={}){return u32(id)+u32(b.size()+8)+b;}
QByteArray scriptedMob(const QByteArray& script) {
    quint32 key=0x12345678;QByteArray encrypted=u32(key);
    for(char c:script){quint32 t=((((key*13)<<4)+key)<<8)-key;key+=(t<<2)+2531011;encrypted.append(char(c^char(key>>16)));}
    return section(0xA000,section(0xC000)+section(0xB000)+section(0xACCEECCB,encrypted));
}
bool waitFor(const std::function<bool()>& ready,int timeout=120000) {
    QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<timeout){qApp->processEvents();QThread::msleep(1);}return ready();
}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    const auto check=[&](bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!ok)throw text;++passed;};
    Hidden hidden;qApp->installEventFilter(&hidden);qApp->setQuitOnLastWindowClosed(false);
    try {
        const QString root=qEnvironmentVariable("EI_MOB_LOAD_ROOT"),maps=qEnvironmentVariable("EI_MOB_LOAD_MAPS");
        check(!root.isEmpty()&&!maps.isEmpty(),"explicit isolated paths");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(2900,1800);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();
        check(view.isValid()&&view.isVisible(),"native renderer logically visible but hidden from desktop");
        ei::SProfileDatabaseLoadRequest databaseRequest;QMap<QString,QString> bindings;
        databaseRequest.resourceDirectories=dynamic_cast<COptStringList*>(view.settings()->opt(eOptSetResource,"resourceRoots"))->value().toList();
        for(const auto& profile:builtInResourceProfiles())if(profile.id=="deja-vu") {
            databaseRequest.profileId=profile.id;databaseRequest.runtimeManifestId=profile.runtimeManifestId;
            databaseRequest.providerId=profile.providers.value("database").id;
            for(auto it=profile.providers.begin();it!=profile.providers.end();++it)bindings[it.key()]=it.value().id;
        }
        ei::ProfileDatabaseService databases;const auto database=databases.snapshot(databaseRequest);
        check(database&&database->usable(),"real private profile database parsed");
        ei::ProfileDatabaseContext::instance().activate(database,bindings);
        check(!bindings.value("characterAppearance").isEmpty()&&!bindings.value("equipmentLayout").isEmpty(),"production character providers active");
        if(qEnvironmentVariableIsSet("EI_MOB_EXTRA_ONLY")) {
            qApp->setProperty("workspaceLoadSmoke",true);view.m_timer->stop();
            const QString synth=root+"/extra";QDir().mkpath(synth);
            write(synth+"/large.mob",scriptedMob(("AddMob(\""+QDir::fromNativeSeparators(maps)+"/zone3obr.mob\")").toUtf8()));
            write(synth+"/manual.mob",scriptedMob("// manually selected"));
            auto projecting=[&](){return window.property("backgroundLoadPhase").toString()=="addmob-project";};
            check(window.loadMobWithProgress(QFileInfo(synth+"/large.mob"),false)&&waitFor(projecting),"large background dependency begins");
            check(window.loadMobWithProgress(QFileInfo(synth+"/manual.mob"),false),"second explicit open interrupts projection");
            auto* selected=view.currentMob();
            // Adoption must not clear the existing Undo history.
            window.m_undoStack->push(new QUndoCommand("owner history marker"));const int history=window.m_undoStack->count();
            check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==10,"interrupted primary traversal resumes alongside second explicit MOB");
            check(view.currentMob()==selected&&window.m_undoStack->count()==history,"background family preserves active MOB and Undo history");
            for(auto* mob:view.mobs()){QByteArray candidate;check(mob->buildValidatedCandidate(&candidate)&&candidate==bytes(mob->filePath().absoluteFilePath()),"large-background no-op bytes: "+mob->mobName());}
            window.closeAll();
            write(synth+"/copy.mob",bytes(maps+"/zone3obr.mob"));
            write(synth+"/changing.mob",scriptedMob("AddMob(\"copy.mob\")"));
            window.loadMobWithProgress(QFileInfo(synth+"/changing.mob"),false);check(waitFor(projecting),"mutable private dependency reaches projection");
            write(synth+"/copy.mob","changed during projection");
            check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==1,"external source change rejects detached MOB");
            window.closeAll();window.loadMobWithProgress(QFileInfo(synth+"/large.mob"),false);check(waitFor(projecting),"resource-change case reaches projection");
            qApp->setProperty("workspaceVisualSmoke",true);qApp->setProperty("workspaceVisualSequence","staged-test");
            int completed=0;QObject::connect(&view,&CView::unitVisualPreparationComplete,&window,[&](){++completed;});
            view.m_unitAppearanceProgressTotal=1;
            view.scheduleUnitAppearancePreparation({},QMatrix4x4(),false);
            check(completed==0,"visual diagnostic cannot finish before AddMob barrier");
            selected=view.currentMob();view.refreshAfterResourceSetup();
            check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==1&&view.currentMob()==selected,"resource generation change cancels old projection");
            view.m_unitAppearanceProgressTotal=1;view.scheduleUnitAppearancePreparation({},QMatrix4x4(),false);
            check(completed==1,"visual diagnostic resumes after background cancellation");
            qApp->setProperty("workspaceVisualSmoke",false);qApp->setProperty("workspaceVisualSequence",QVariant());
            window.closeAll();out<<passed<<" PASS / 0 FAIL\n";out.flush();return 0;
        }
        check(window.loadLandscapeWithProgress(QFileInfo(maps+"/zone3obr.mpr")),"private terrain loaded");
        view.m_timer->stop();
        check(view.ensureTerrainAutoCatalogReady(),"terrain editor initialization settled before MOB timing");
        view.m_cam->moveTo({119,71,0});view.m_cam->moveAwayOn(80);
        const bool render=qEnvironmentVariable("EI_MOB_LOAD_RENDER")!="0";
        const bool stats=qEnvironmentVariable("EI_MOB_LOAD_STATS")!="0";
        if(auto* option=dynamic_cast<COptBool*>(view.settings()->opt("renderPerformanceCounters"))) option->setValue(stats);
        qint64 renderMs=0;int callbacks=0;bool painting=false;
        QTimer pump;pump.setInterval(0);
        QObject::connect(&pump,&QTimer::timeout,[&](){
            if(painting)return;QScopedValueRollback<bool> guard(painting,true);++callbacks;
            if(render){QElapsedTimer frame;frame.start();view.paintGL();renderMs+=frame.elapsed();}
        });
        const int diagnosticFramesBefore=view.m_openGlDiagnosticFrame;
        QElapsedTimer elapsed;elapsed.start();pump.start();
        check(window.loadMobWithProgress(QFileInfo(maps+"/zone3obr.mob"),false),"production MOB family load completes");
        const qint64 loadMs=elapsed.elapsed();pump.stop();view.m_timer->stop();
        const int loadingFrames=view.m_openGlDiagnosticFrame-diagnosticFramesBefore;
        if(qEnvironmentVariableIsSet("EI_EXPECT_LOAD_FRAME_FENCE"))
            check(loadingFrames==0,"workspace load performs no intermediate scene frames");
        QElapsedTimer firstFrame;firstFrame.start();view.paintGL();
        const qint64 firstFrameMs=firstFrame.elapsed();
        check(RenderPerformanceContext::instance().currentFrameCounters().drawCalls>0,"completed scene renders after loading");
        check(view.m_aMob.size()==1&&window.backgroundMobLoading(),"primary is usable before any AddMob is adopted");
        check(CAiEditAdapter::busy(&window),"automation waits for complete editable family");
        qint64 backgroundGap=0,lastPulse=elapsed.elapsed();int heartbeats=0;
        QTimer heartbeat;heartbeat.setInterval(10);
        QObject::connect(&heartbeat,&QTimer::timeout,[&](){const auto now=elapsed.elapsed();backgroundGap=qMax(backgroundGap,now-lastPulse);lastPulse=now;++heartbeats;});
        heartbeat.start();
        check(waitFor([&](){return !window.backgroundMobLoading();}),"background AddMob queue completes");
        heartbeat.stop();const qint64 familyReadyMs=elapsed.elapsed();
        check(heartbeats>2,"GUI heartbeat runs between background load pulses");
        check(view.m_aMob.size()==8,"primary plus seven AddMob dependencies loaded");
        check(view.currentMob()->filePath().fileName()=="zone3obr.mob","primary MOB remains active");
        // First-frame lazy visual state depends on prior frame count. Compare
        // stored coordinates immediately, and display coordinates only after
        // identical explicit resolution, outside the timed load/first frame.
        QJsonArray authored;
        for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())authored.append(QJsonArray{mob->filePath().fileName(),int(node->mapId()),
            node->position().x(),node->position().y(),node->position().z()});
        view.makeCurrent();
        QElapsedTimer settling;settling.start();int unresolved=0;
        do {
            unresolved=0;
            for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())if(auto* unit=dynamic_cast<CUnit*>(node)) {
                if(unit->needsVisualResolution())unit->preloadVisualResources();
                if(unit->needsVisualResolution())++unresolved;
            }
            if(unresolved) {qApp->processEvents(QEventLoop::ExcludeUserInputEvents);QThread::msleep(1);view.makeCurrent();}
        } while(unresolved && settling.elapsed()<60000);
        check(unresolved==0,"all asynchronous layer composites resolved before display-coordinate comparison");
        view.ensureUnitSupportSurface();
        for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())if(auto* unit=dynamic_cast<CUnit*>(node)) {
            view.m_pLand->projectPosition(unit);unit->refreshLogicProjection();
        }
        QJsonArray documents,objects,details;int count=0;
        for(auto* mob:view.m_aMob) {
            count+=mob->nodes().size();
            documents.append(QJsonObject{{"file",mob->filePath().fileName()},{"nodes",mob->nodes().size()},{"dirty",mob->isDirty()}});
            check(!mob->isDirty(),"clean "+mob->filePath().fileName());
            QByteArray candidate;CValidationReport validation;
            check(mob->buildValidatedCandidate(&candidate,&validation)&&candidate==bytes(mob->filePath().absoluteFilePath()),
                  "no-op save preserves all raw bytes: "+mob->filePath().fileName());
            for(auto* node:mob->nodes())objects.append(QJsonArray{mob->filePath().fileName(),int(node->mapId()),
                node->drawPosition().x(),node->drawPosition().y(),node->drawPosition().z()});
            for(auto* node:mob->nodes())if(node->mapId()==4623||node->mapId()==3323||node->mapId()==800003||node->mapId()==800005) {
                details.append(QJsonObject{{"file",mob->filePath().fileName()},{"id",int(node->mapId())},
                    {"type",int(node->nodeType())},{"minimumZ",node->minPosition().z()},{"node",node->toJson()}});
            }
        }
        QJsonObject result{{"loadMs",double(loadMs)},{"renderMs",double(renderMs)},{"eventPumpCallbacks",callbacks},
            {"firstFrameMs",double(firstFrameMs)},{"readyMs",double(loadMs+firstFrameMs)},
            {"familyReadyMs",double(familyReadyMs)},{"backgroundHeartbeats",heartbeats},{"backgroundMaxGapMs",double(backgroundGap)},
            {"diagnosticFramesDuringLoad",loadingFrames},
            {"positionsSha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(objects).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())},
            {"authoredPositionsSha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(authored).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())},
            {"renderEnabled",render},{"statsEnabled",stats},{"nodes",count},{"documents",documents},
            {"viewportWidth",view.width()},{"viewportHeight",view.height()}};
        QFile evidence(root+"/result.json");check(evidence.open(QIODevice::WriteOnly),"evidence file");evidence.write(QJsonDocument(result).toJson());evidence.close();
        QFile positions(root+"/positions.json");check(positions.open(QIODevice::WriteOnly),"position evidence file");
        positions.write(QJsonDocument(objects).toJson());positions.close();
        QFile detailFile(root+"/details.json");if(detailFile.open(QIODevice::WriteOnly))detailFile.write(QJsonDocument(details).toJson());
        check(!window.loadMobWithProgress(QFileInfo(maps+"/absent-regression.mob"),false),"missing MOB rejected");
        check(!view.m_unitVisualPreparationSuspended && view.m_aMob.size()==8,
              "failed load restores workspace state and retains existing documents");
        view.paintGL();
        check(RenderPerformanceContext::instance().currentFrameCounters().drawCalls>0,"render resumes after failed load too");
        check(window.beginWorkspaceLoad("standalone regression"),"begin isolated workspace replacement");
        view.unloadMob("");view.unloadLand(false);window.endWorkspaceLoad();
        check(window.loadMobWithProgress(QFileInfo(maps+"/Autozone3obrclown.mob"),false),"standalone MOB opens without MPR");
        check(!view.m_pLand->isMprLoad()&&view.m_aMob.size()==1&&view.currentMob()->nodes().size()==8,
              "standalone document and all eight objects retained");
        view.paintGL();
        check(!view.m_unitVisualPreparationSuspended&&RenderPerformanceContext::instance().currentFrameCounters().drawCalls>0,
              "standalone viewport resumes and renders");
        check(waitFor([&](){return !window.backgroundMobLoading();}),"standalone queue finishes");

        const QString synth=root+"/synthetic";QDir().mkpath(synth);
        write(synth+"/root.mob",scriptedMob("AddMob(\"first.mob\")\nAddMob(\"FIRST.MOB\")\nAddMob(\"broken.mob\")\nAddMob(\"last.mob\")\nAddMob(\"missing.mob\")"));
        write(synth+"/first.mob",scriptedMob("AddMob(StrCat(\"nest\",\"ed.mob\"))\nAddMob(\"last.mob\")"));
        write(synth+"/nested.mob",scriptedMob("AddMob(\"root.mob\")\nAddMob(\"first.mob\")"));
        write(synth+"/last.mob",scriptedMob("// AddMob(\"ignored.mob\")"));write(synth+"/broken.mob","invalid");
        window.closeAll();
        check(window.loadMobWithProgress(QFileInfo(synth+"/root.mob"),false)&&view.mobs().size()==1,"synthetic root returns before recursive loading");
        auto* primary=view.currentMob();
        check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==4,"cycles/shared/case aliases/malformed/missing: complete unique family");
        check(view.currentMob()==primary,"background adoptions preserve active MOB");
        for(auto* mob:view.mobs()){QByteArray candidate;check(mob->buildValidatedCandidate(&candidate)&&candidate==bytes(mob->filePath().absoluteFilePath()),"synthetic raw provenance: "+mob->mobName());}
        SEnduranceCollection collection;collection.name="staged";collection.mprReference=maps+"/zone2.mpr";
        collection.mobReferences<<synth+"/first.mob"<<synth+"/last.mob";QString error;
        check(CCollectionDocument::save(synth+"/test.collection",collection,&error),"isolated collection fixture");
        window.openCollection(synth+"/test.collection");
        check(view.mobs().size()==2&&view.currentMob()->mobName()=="last.mob","explicit collection MOBs precede background dependencies");
        check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==4&&view.currentMob()->mobName()=="last.mob","collection traversal keeps explicit active MOB");
        primary=view.currentMob();primary->setScript(primary->script()+"\n// retained edit");primary->setDirty();
        window.openMapPair(maps+"/zone3obr.mpr",maps+"/zone3obr.mob","switch-cancel");
        check(view.currentMob()==primary&&primary->isDirty()&&view.mobs().size()==4,"cancel dirty transition preserves loaded documents and edits");primary->setDirty(false);

        // A whole map used as an AddMob forces many projection pulses. Cancel
        // after the CPU work, before any partial document can enter the registry.
        write(synth+"/large.mob",scriptedMob(("AddMob(\""+QDir::fromNativeSeparators(maps)+"/zone3obr.mob\")").toUtf8()));
        window.closeAll();check(window.loadMobWithProgress(QFileInfo(synth+"/large.mob"),false),"large dependency root");
        check(waitFor([&](){return window.property("backgroundLoadPhase").toString()=="addmob-project";},60000),"large dependency reaches sliced projection");
        check(view.mobs().size()==1,"partially projected MOB stays detached");
        window.closeAll();check(window.loadMobWithProgress(QFileInfo(synth+"/last.mob"),false),"switch during background projection");
        check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==1&&view.currentMob()->mobName()=="last.mob","cancelled epoch cannot adopt stale objects");
        window.closeAll();window.loadMobWithProgress(QFileInfo(synth+"/large.mob"),false);
        check(waitFor([&](){return window.property("backgroundLoadPhase").toString()=="addmob-project";},60000),"large dependency second projection");
        primary=view.currentMob();primary->setScript("// dependency removed while loading");primary->setDirty();
        check(waitFor([&](){return !window.backgroundMobLoading();})&&view.mobs().size()==1&&primary->isDirty(),"script edit cancels stale queue and preserves dirty source");primary->setDirty(false);

        window.closeAll();check(window.loadLandscapeWithProgress(QFileInfo(maps+"/zone3obr.mpr")),"terrain for staged-neighbor check");
        check(waitFor([&](){return !window.m_worldMetadataWatcher;}),"world metadata settled");
        // The next fixture supplies two explicit preview placements. Prevent
        // the real profile's external-world poll from replacing them mid-check.
        QScopedValueRollback<bool> syntheticLayoutGuard(window.m_worldAlignmentPreviewActive,true);
        view.m_neighborPlacements.clear();
        const qreal width=view.m_pLand->widthInWorldUnits();
        for(int i=0;i<2;++i) {
            SNeighborMapPlacement p;p.zoneId=i?"far":"touch";p.mprPath=maps+(i?"/zone9.mpr":"/zone1.mpr");p.mobPath=maps+(i?"/zone9.mob":"/zone1.mob");
            CMprDocument document;check(document.load(QFileInfo(p.mprPath)),"neighbor MPR fixture");
            p.size=QSizeF(document.mapInfo().sectorCountX*32,document.mapInfo().sectorCountY*32);p.origin=QPointF(i?width+1024:width,0);p.worldOrigin=p.origin;view.m_neighborPlacements.append(p);
        }
        view.m_neighborLandscapesVisible=false;view.loadNeighborLandscapes();
        check(view.m_neighborLoadStage==0&&view.m_neighborLandscapes.isEmpty(),"disabled neighbors start no background scenes");
        view.m_neighborLandscapesVisible=true;view.deferNeighborLoading(true);view.loadNeighborLandscapes();
        check(view.m_neighborLoadStage==0,"neighbor work remains deferred before AddMob barrier");
        view.deferNeighborLoading(false);
        auto completeNeighbors=[&]() {
            int phase=0;bool ordered=true;QJsonArray phases;
            const bool finished=waitFor([&]() {
                const int stage=view.m_neighborLoadStage;
                if(stage!=phase) {
                    phase=stage;phases.append(stage);
                    if(stage==2)ordered=ordered&&view.m_neighborLandscapes.size()==1&&view.m_neighborLandscapes[0].staticObjects.isEmpty();
                    if(stage==3)ordered=ordered&&view.m_neighborLandscapes.size()==1&&!view.m_neighborLandscapes[0].staticObjects.isEmpty()&&view.m_neighborLandscapes[0].pendingObjects.isEmpty();
                }
                return stage==0;
            });
            out<<"NEIGHBOR_PHASES "<<QJsonDocument(phases).toJson(QJsonDocument::Compact)<<'\n';out.flush();
            check(finished&&ordered,"touch terrain then touch objects then distant terrain");
            check(view.m_neighborLandscapes.size()==2,"both requested neighbor terrains accepted");
            for(const auto& n:view.m_neighborLandscapes)check(n.showObjects==(n.placement.zoneId=="touch"),"neighbor object scope: "+n.placement.zoneId);
        };
        completeNeighbors();
        // Terrain/objects retained in cache must obey the same ordering.
        view.loadNeighborLandscapes();
        // Cached objects are retained but hidden until stage 2.
        check(waitFor([&](){return view.m_neighborLoadStage==2;}),"warm load reaches object barrier");
        check(view.m_neighborLandscapes.size()==1&&!view.m_neighborLandscapes[0].showObjects,"warm touching terrain precedes cached objects");
        check(waitFor([&](){return view.m_neighborLoadStage==0;})&&view.m_neighborLandscapes.size()==2,"warm staged neighbors complete");
        view.loadNeighborLandscapes();view.m_neighborLandscapesVisible=false;view.clearNeighborLandscapes();
        check(waitFor([&](){return view.m_neighborScenePool->activeThreadCount()==0;})&&view.m_neighborLandscapes.isEmpty(),"toggle-off rejects pending neighbor results");
        window.closeAll();
        out<<"BENCH "<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n'<<passed<<" PASS / 0 FAIL\n";out.flush();
        return 0;
    } catch(const QString& failure){out<<"FAIL "<<failure<<'\n';return 1;}
}

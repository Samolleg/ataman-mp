#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "camera.h"
#include "settings.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "resource_setup_dialog.h"
#include "neighbor_scene_data.h"
#include "endurance_document.h"
#include "mob.h"
#include "worldobj.h"
#include "render_performance.h"
#include "resource_profile.h"
#include <windows.h>
#include <psapi.h>
#include <cstring>
#include <QtEndian>

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* o,QEvent* e) override {
        if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
quint64 memory() { PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));return m.PrivateUsage; }
PROCESS_MEMORY_COUNTERS_EX memoryCounters() { PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));return m; }
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try {
        const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MIXED_FIXTURE"));
        const QString game=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MIXED_GAME_ROOT"));
        check(!fixture.isEmpty()&&!game.isEmpty(),"explicit private fixture");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("Ataman214NeighborsValidation");
        QTemporaryDir scratch;QDir::setCurrent(scratch.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.m_currentCollectionPath=scratch.filePath("private.collection");
        auto& settings=*window.m_settings;
        for(const auto& candidate:loadResourceProfiles())if(candidate.id=="deja-vu") {
            auto profile=evaluateResourceProfile(game,candidate);
            check(profile.valid,"private Dejavu profile is complete for editor resources");
            profile.resourceDirectories=QStringList{fixture+"/res",qEnvironmentVariable("EI_NEIGHBOR_DONOR")+"/MODS/DejaVu/res",qEnvironmentVariable("EI_NEIGHBOR_DONOR")+"/Base/Res"};
            profile.mapDirectories=QStringList{fixture+"/Maps"};settings.applyResourceProfile(profile);break;
        }
        dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(game);
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"mapRoots"))->setValue({fixture+"/Maps"});
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res",qEnvironmentVariable("EI_NEIGHBOR_DONOR")+"/MODS/DejaVu/res",qEnvironmentVariable("EI_NEIGHBOR_DONOR")+"/Base/Res"});
        dynamic_cast<COptString*>(settings.opt(eOptSetResource,"terrainAuthoringArchive"))->setValue(qEnvironmentVariable("EI_MIXED_REFERENCE"));
        dynamic_cast<COptBool*>(settings.opt("renderPerformanceCounters"))->setValue(true);
        CTextureList::getInstance()->attachSettings(&settings);CObjectList::getInstance()->attachSettings(&settings);
        auto& view=*window.view();window.resize(1200,850);window.show();view.show();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"production hidden MainWindow OpenGL");
        view.m_neighborLandscapesVisible=true;
        // Keep a retained CPU world only when it has edited documents; ordinary
        // navigation exercises production cache ownership, not test snapshots.
        window.refreshEditorWorld();
        QTimer messages;QObject::connect(&messages,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){out<<"DIALOG "<<b->text()<<"\n";out.flush();if(auto* save=b->button(QMessageBox::Save))save->click();else b->reject();}});messages.start(50);
        QMap<QString,SWorldMapZoneOrigin> maps;
        QElapsedTimer metadata;metadata.start();
        while(metadata.elapsed()<30000 && window.m_worldMetadataWatcher)QTest::qWait(20);
        for(const auto& map:window.m_editorWorldMaps)if(map.allodId=="gipat")maps.insert(map.mprIdentity,map);
        for(const auto& key:maps.keys())out<<"WORLD_MAP "<<key<<" zone="<<maps[key].zoneId<<"\n";
        out.flush();
        check(maps.size()==10,"actual copied world layout has ten Gipat maps");
        auto ready=[&]() {
            if(view.m_neighborLandscapes.size()!=view.m_neighborPlacements.size() || view.m_neighborObjectUploadScheduled)return false;
            for(const auto& n:view.m_neighborLandscapes)
                if((n.showObjects&&!n.objectsPrepared)||!n.pendingObjects.isEmpty()||n.landscape->shadowPreparationPending())return false;
            if(view.m_pLand->shadowPreparationPending())return false;
            return !window.m_worldMetadataWatcher&&!view.m_terrainAutoCatalogPending&&!view.m_terrainAutoCatalogBuilding;
        };
        QJsonArray transitions;
        auto open=[&](const QString& name) {
            check(maps.contains(name),"world map identity exists: "+name);
            const auto map=maps[name];QElapsedTimer timer;timer.start();
            const auto source=view.m_activeMapWorldOrigin;
            window.openNeighborMapPair(fixture+"/Maps/"+name+".mpr",map.mobPath.isEmpty()?QString():fixture+"/"+map.mobPath,map.zoneId,source,map.origin);
            // A hidden QGLWidget does not receive normal expose-driven paints.
            // Drive the same viewport frame/deferred-catalog boundary explicitly.
            view.makeCurrent();view.paintGL();view.ensureTerrainAutoCatalogReady();
            while(timer.elapsed()<90000&&!ready()) {QTest::qWait(10);view.makeCurrent();view.paintGL();}
            if(!ready())out<<"NOT_READY loaded="<<view.m_neighborLandscapes.size()<<" placements="<<view.m_neighborPlacements.size()<<" upload="<<view.m_neighborObjectUploadScheduled<<" catalog="<<view.m_terrainAutoCatalogPending<<"/"<<view.m_terrainAutoCatalogBuilding<<"\n";
            check(ready(),"all neighbor scenes and object uploads ready: "+name);
            check(view.m_pLand->filePath().baseName()==name,"requested map is active: "+name);
            view.makeCurrent();view.paintGL();
            const auto elapsed=timer.elapsed();
            QJsonObject row;row["map"]=name;row["toCompleteFrameMs"]=double(elapsed);row["privateMiB"]=double(memory())/1048576;
            const auto mem=memoryCounters();row["peakPrivateMiB"]=double(mem.PeakPagefileUsage)/1048576;row["workingSetMiB"]=double(mem.WorkingSetSize)/1048576;row["peakWorkingSetMiB"]=double(mem.PeakWorkingSetSize)/1048576;
            row["visible"]=view.m_neighborLandscapes.size();row["retained"]=view.m_neighborRenderCache.size();
            transitions.append(row);out<<"TIMING "<<QJsonDocument(row).toJson(QJsonDocument::Compact)<<"\n";out.flush();
            return elapsed;
        };

        open("zone6");QString error;
        check(view.m_worldTerrainSession.load(fixture+"/Maps/zone6.mpr",&error),"retain clean real MPR baseline");view.syncWorldTerrainActive();
        CValidationReport retainedReport;
        check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(fixture+"/Maps/zone6.mpr"),&retainedReport),"retain clean world atlas snapshot");
        check(!view.m_worldTerrainSession.documents[CWorldTerrainSession::key(fixture+"/Maps/zone6.mpr")]->terrain.isDirty(),"retained source is clean");
        open("gipat2");
        QVector3D objectPoint;float beforeRendered=0;bool found=false;
        for(const auto& n:view.m_neighborLandscapes)if(n.placement.zoneId==maps["zone6"].zoneId&&!n.staticObjects.isEmpty()) {objectPoint=n.staticObjects.first()->position();QVector3D p(objectPoint.x(),objectPoint.y(),0);n.landscape->projectPt(p,true);beforeRendered=p.z();found=true;}
        check(found,"real zone6 object for external height probe");
        const QString path=fixture+"/Maps/zone6.mpr";const auto original=read(path);const auto mtime=QFileInfo(path).lastModified();
        CMprDocument disk;CTerrainEditModel model;CValidationReport report;
        check(disk.load(QFileInfo(path),&report)&&model.load(disk,&report),"parse copied external MPR");
        STerrainBrush brush;brush.centerX=qRound(objectPoint.x());brush.centerY=qRound(objectPoint.y());brush.radius=4;
        const float before=model.terrainSurfaceHeight(objectPoint.x(),objectPoint.y());
        check(model.apply(model.makeRaiseLower(brush,4096),true,&report),"edit terrain under real neighbor object outside mapper session");
        const float expected=model.terrainSurfaceHeight(objectPoint.x(),objectPoint.y());
        check(qAbs(expected-before)>0.05f,"external change has a visible world height delta");
        CMprEditTransaction tx("Private external height edit");
        for(const auto& sec:disk.sectors()) {const auto old=disk.memberPayload(sec.memberName);const auto now=model.sectorPayload(sec.sectorX,sec.sectorY);if(old!=now)tx.replaceMemberPayload(sec.memberName,QCryptographicHash::hash(old,QCryptographicHash::Sha256).toHex(),now);}
        QByteArray changed;check(disk.applyTransaction(tx,&changed,&report),"lossless MPR transaction on private copy");
        QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(changed)==changed.size(),"write external MPR copy");file.flush();file.setFileTime(mtime,QFileDevice::FileModificationTime);file.close();
        view.makeCurrent();auto diskReference=CLandscape::createReadOnlyBackground();
        check(diskReference->readBackgroundMap(QFileInfo(path)),"fresh independent renderer reads changed disk snapshot");
        QVector3D expectedSurface(objectPoint.x(),objectPoint.y(),0);check(diskReference->projectPt(expectedSurface,true),"independent changed relief is projectable");
        check(qAbs(expectedSurface.z()-beforeRendered)>0.05f,"external edit visibly changes renderer relief");
        diskReference.clear();
        open("zone1");open("gipat2");
        found=false;
        for(const auto& n:view.m_neighborLandscapes)if(n.placement.zoneId==maps["zone6"].zoneId) {
            QVector3D surface(objectPoint.x(),objectPoint.y(),0);check(n.landscape->baselineSha256()==QCryptographicHash::hash(changed,QCryptographicHash::Sha256),"cached background owns changed disk bytes");check(n.landscape->projectPt(surface,true),"fresh external relief is projectable");
            check(qAbs(surface.z()-expectedSurface.z())<0.01f,"background terrain uses new external MPR");
            check(!n.staticLocalPositions.isEmpty()&&qAbs(n.staticLocalPositions.first().z()-(expectedSurface.z()+objectPoint.z()))<0.01f,"neighbor object projects onto new relief instead of retained baseline");found=true;
        }
        check(found,"changed MPR is present in final neighbor frame");
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(original)==original.size(),"restore only private external MPR");file.close();
        view.m_worldTerrainSession.clear();view.unloadMob("");view.unloadLand(false);qApp->removeEventFilter(&hidden);
        out<<"PASS external MPR "<<passes<<" checks\n";return 0;
    }catch(const QString& e){out<<"FAILED "<<e<<"\n";return 1;}
}

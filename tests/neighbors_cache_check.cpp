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
            return !view.m_terrainAutoCatalogPending&&!view.m_terrainAutoCatalogBuilding;
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
        auto adjacentCheck=[&]() {
            const QSizeF active(view.m_pLand->widthInWorldUnits(),view.m_pLand->heightInWorldUnits());
            int touchingWithObjects=0;
            for(auto& n:view.m_neighborLandscapes) {
                const bool expected=CNeighborMapLayout::touchesActiveMap(active,n.placement);
                check(n.showObjects==expected,"adjacency gates visible objects: "+n.placement.zoneId);
                if(expected&&!n.placement.mobPath.isEmpty()) {
                    check(!n.staticObjects.isEmpty(),"touching neighbor has uploaded objects: "+n.placement.zoneId);++touchingWithObjects;
                }
                if(!expected)check(!n.showObjects,"distant objects remain hidden even when cached");
            }
            check(touchingWithObjects>0,"active map has object-bearing touching neighbors");
        };
        open("zone1");adjacentCheck();
        open("gipat2");adjacentCheck();check(!view.m_activeMob,"gipat2 deliberately has no active MOB");
        check(view.m_pLand->widthInWorldUnits()==96&&view.m_pLand->heightInWorldUnits()==192,"actual gipat2 is 96x192");
        // Camera spans the left border and neighboring landscape. Compare real
        // renderer frames with only the neighbor objects channel toggled.
        QVector3D look(20,32,0);view.m_pLand->projectPt(look,true);view.moveCameraToPoint(look);view.m_cam->moveAwayOn(180);
        view.makeCurrent();view.paintGL();const auto with=view.grabFrameBuffer().convertToFormat(QImage::Format_RGB32);
        view.m_staticObjectsVisible=false;view.paintGL();const auto without=view.grabFrameBuffer().convertToFormat(QImage::Format_RGB32);
        quint64 pixels=0;for(int y=0;y<with.height();++y)for(int x=0;x<with.width();++x)if(with.pixel(x,y)!=without.pixel(x,y))++pixels;
        view.m_staticObjectsVisible=true;
        check(pixels>100,"objects change actual gipat2 framebuffer without active MOB");
        check(with.save(fixture+"/gipat2-neighbor-objects.png"),"save active gipat2 screenshot");
        out<<"OBJECT_PIXELS "<<pixels<<"\n";
        // Warm back-and-forth must retain existing neighbor landscapes exactly.
        QMap<QString,QWeakPointer<CLandscape>> before;
        QMap<QString,GLuint> atlasBefore;
        for(const auto& n:view.m_neighborLandscapes)before[n.placement.zoneId]=n.landscape;
        for(const auto& n:view.m_neighborLandscapes)atlasBefore[n.placement.zoneId]=n.landscape->glTexture()->textureId();
        open("zone1");open("gipat2");
        for(const auto& n:view.m_neighborLandscapes)if(before.contains(n.placement.zoneId)) {
            if(n.placement.zoneId!=maps["zone1"].zoneId)check(before[n.placement.zoneId].toStrongRef()==n.landscape,"same terrain retained across roundtrip: "+n.placement.zoneId);
            check(atlasBefore[n.placement.zoneId]==n.landscape->glTexture()->textureId(),"same GPU atlas retained across active/background roundtrip: "+n.placement.zoneId);
        }
        before.clear();
        // Real active height edit/Save, then immediately inspect it as a neighbor.
        open("zone6");view.makeCurrent();view.ensureTerrainAutoCatalogReady();
        QString worldError;check(view.m_worldTerrainSession.load(fixture+"/Maps/zone6.mpr",&worldError),"retain edited world document");
        STerrainBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=1;
        CValidationReport report;const auto change=view.m_terrainEdit.makeRaiseLower(brush,8);
        check(view.m_terrainEdit.apply(change,true,&report),"edit copied zone6 relief");
        for(const auto& sector:view.m_mprDocument.sectors())
            view.m_pLand->previewCanonicalSector(sector.sectorX,sector.sectorY,view.m_terrainEdit.sectorPayload(sector.sectorX,sector.sectorY));
        view.m_pLand->setDirty(true);view.syncWorldTerrainActive();
        check(view.saveWorldTerrainMaps(),"production Save copied neighbor");
        const float saved=view.m_terrainEdit.terrainSurfaceHeight(8,8);
        open("gipat2");
        bool found=false;for(const auto& n:view.m_neighborLandscapes)if(n.placement.zoneId==maps["zone6"].zoneId){QVector3D p(8,8,0);n.landscape->projectPt(p,true);check(qAbs(p.z()-saved)<0.01f,"saved neighbor relief visible after transition");found=true;}
        check(found,"saved zone6 is in neighbor set");
        // Same-size / restored-mtime MOB mutation must invalidate object cache.
        const QString mob=fixture+"/Maps/zone6.mob";const auto fingerprint=neighborContentFingerprint(mob);const auto oldTime=QFileInfo(mob).lastModified();
        QFile file(mob);check(file.open(QIODevice::ReadWrite),"open private MOB for fingerprint probe");
        const auto original=file.readAll();check(!original.isEmpty(),"MOB probe content exists");file.seek(original.size()-1);char changed=original.back()^1;file.write(&changed,1);file.flush();file.setFileTime(oldTime,QFileDevice::FileModificationTime);file.close();
        check(neighborContentFingerprint(mob)!=fingerprint,"content change detected despite same size and mtime");
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(original)==original.size(),"restore private fingerprint probe");file.close();
        CMobDocument external;check(external.load(QFileInfo(mob),&report),"parse copied neighbor before external edit");
        QString objectPath;float updatedX=0;
        for(const auto& object:external.objects())if(!object.unit&&object.sectionType==0xB001&&object.hasPosition&&!object.model.isEmpty()) {objectPath=object.sectionPath;updatedX=object.positionX+0.375f;break;}
        quint64 positionOffset=0;
        std::function<void(const QVector<SMobSection>&)> locate=[&](const QVector<SMobSection>& sections) {
            for(const auto& section:sections) {
                if(section.path==objectPath)for(const auto& field:section.children)if(field.type==0xB009)positionOffset=field.offset+8;
                locate(section.children);
            }
        };locate(external.sections());
        check(positionOffset>0&&positionOffset+4<=quint64(original.size()),"locate real static object position");
        QByteArray externalBytes=original;quint32 bits=0;std::memcpy(&bits,&updatedX,4);qToLittleEndian(bits,reinterpret_cast<uchar*>(externalBytes.data()+positionOffset));
        QWeakPointer<CLandscape> terrainBefore;
        for(const auto& n:view.m_neighborLandscapes)if(n.placement.zoneId==maps["zone6"].zoneId)terrainBefore=n.landscape;
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(externalBytes)==externalBytes.size(),"save external edit on private MOB");file.flush();file.setFileTime(oldTime,QFileDevice::FileModificationTime);file.close();
        open("zone1");open("gipat2");
        for(const auto& n:view.m_neighborLandscapes)if(n.placement.zoneId==maps["zone6"].zoneId) {
            check(!n.staticLocalPositions.isEmpty()&&qAbs(n.staticLocalPositions.first().x()-updatedX)<0.001f,"new external MOB position appears after transition");
            check(terrainBefore.toStrongRef()==n.landscape,"MOB change preserves terrain and sun state");
        }
        check(file.open(QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(original)==original.size(),"restore private external edit after cache validation");file.close();
        QJsonArray roundPeaks;
        for(int round=0;round<3;++round) {
            quint64 peak=0;
            for(const auto& name:QStringList{"zone1","zone2","zone3obr","zone6","zone7","zone8","zone9","gipat2"}) {open(name);peak=qMax(peak,memory());}
            peak=qMax(peak,quint64(memoryCounters().PeakPagefileUsage));
            roundPeaks.append(double(peak)/1048576);out<<"ROUND_PEAK_MIB "<<round<<" "<<double(peak)/1048576<<"\n";out.flush();
        }
        check(roundPeaks[2].toDouble()<roundPeaks[1].toDouble()+192,"third 8-map loop has bounded memory growth");
        // Deliberately shrink the budget to exercise actual LRU eviction. Visible
        // maps remain pinned; inactive holders cannot survive an exhausted budget.
        view.makeCurrent();view.m_neighborCacheBudget=1;view.trimNeighborRenderCache();
        check(view.m_neighborRenderCache.isEmpty()&&view.m_neighborCacheEvictions>0,"LRU evicts unpinned render state under memory pressure");
        QJsonObject result;result["transitions"]=transitions;result["roundPeakPrivateMiB"]=roundPeaks;result["objectPixels"]=double(pixels);result["passes"]=passes;
        QFile receipt(fixture+"/neighbors-214-result.json");check(receipt.open(QIODevice::WriteOnly)&&receipt.write(QJsonDocument(result).toJson())>0,"write transition/memory receipt");receipt.close();
        view.m_worldTerrainSession.clear();view.unloadMob("");view.unloadLand(false);qApp->removeEventFilter(&hidden);
        out<<"PASS neighbors cache "<<passes<<" checks\n";return 0;
    }catch(const QString& e){out<<"FAILED "<<e<<"\n";return 1;}
}

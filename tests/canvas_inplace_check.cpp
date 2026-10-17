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
#include "terrain_atlas_archive_binding.h"
#include "new_terrain_canvas.h"
#include "endurance_document.h"
#include "resource_name.h"
#include "res_archive_document.h"
#include "mob.h"
#include "unit.h"
#include "magictrap.h"
#include "profile_database.h"
#include "technical_model_catalog.h"
#include "canvas_navigation_state.h"
#include "staged_save_validator.h"
namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* o,QEvent* e) override {
        if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o))
                if(w->isWindow()) w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray read(const QString& p) { QFile f(p); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
QByteArray heights(const CTerrainEditModel& model) {
    QByteArray b;
    QDataStream s(&b,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);
    for(int y=0;y<model.vertexCountY();++y) for(int x=0;x<model.vertexCountX();++x) s<<model.height(x,y);
    return b;
}
QByteArray terrain(const CMprDocument& map,const CTerrainEditModel& model) {
    QByteArray b;for(const auto& s:map.sectors())b+=model.sectorPayload(s.sectorX,s.sectorY);return b;
}
}
int runAssemblyCatalogCheck() {
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try {
        const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MIXED_FIXTURE"));
        check(!fixture.isEmpty(),"explicit private fixture");
        QTemporaryDir scratch;QDir::setCurrent(scratch.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.m_currentCollectionPath=scratch.filePath("private.collection");
        auto& settings=*window.m_settings;
        dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(qEnvironmentVariable("EI_MIXED_GAME_ROOT"));
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res",qEnvironmentVariable("EI_MIXED_GAME_ROOT")+"/MODS/DejaVu/res",qEnvironmentVariable("EI_MIXED_GAME_ROOT")+"/Base/Res"});
        dynamic_cast<COptString*>(settings.opt(eOptSetResource,"terrainAuthoringArchive"))->setValue(qEnvironmentVariable("EI_MIXED_REFERENCE"));
        CTextureList::getInstance()->attachSettings(&settings);
        auto& view=*window.view();view.setMouseTracking(true);window.resize(1200,850);window.show();view.show();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"production hidden MainWindow GL");
        auto& panel=*window.m_landscapeTools;
        QObject::connect(&view,&CView::terrainPublicationPreflightRequested,[&](const QString& p,bool* accept){
            const QString target=QFileInfo(p).absoluteFilePath();
            *accept=target==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath()
                || target==QFileInfo(fixture+"/Maps/new204-atlas.res").absoluteFilePath();
        });

        int unexpected=0; bool cancelNext=false; QString persistChoice; QString racePath; QByteArray raceBytes;
        QTimer messages;QObject::connect(&messages,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){
            if(!b->isVisible())continue;
            out<<"DIALOG "<<b->text()<<"\n";out.flush();
            if(b->windowTitle()==QString::fromUtf8("Применить размер к текущей MPR")){
                if(!racePath.isEmpty()){QFile f(racePath);f.open(QIODevice::WriteOnly);f.write(raceBytes);racePath.clear();}
                b->button(cancelNext?QMessageBox::Cancel:QMessageBox::Yes)->click();cancelNext=false;
            }else if(!persistChoice.isEmpty()&&b->findChild<QPushButton*>(persistChoice)){b->findChild<QPushButton*>(persistChoice)->click();}
            else if(auto* save=b->button(QMessageBox::Save))save->click();
            else if(auto* ok=b->button(QMessageBox::Ok)) {if(b->icon()==QMessageBox::Critical || b->icon()==QMessageBox::Warning)++unexpected;ok->click();}
            else {++unexpected;b->reject();}
        }});messages.start(30);
        auto write=[&](const QString& path,const QByteArray& bytes){QSaveFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit(),"write private fixture "+QFileInfo(path).fileName());};
        auto open=[&](const QString& path){view.m_worldTerrainSession.clear();view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(path),false);QTest::qWait(100);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"open real atlas with materials");};
        const QString donor=fixture+"/Maps/zone1.mpr", created=fixture+"/Maps/canvas205.mpr", atlasPath=fixture+"/Maps/canvas205-atlas.res";
        const QByteArray donorBytes=read(donor);check(!donorBytes.isEmpty(),"private real donor");
        open(donor);CMprDocument source;CValidationReport report;QString error;
        check(source.loadBytes(donorBytes,donor,&report),"source document");
        SMprCreationRequest fresh;fresh.innerNamespace="canvas205";fresh.sectorCountX=2;fresh.sectorCountY=2;fresh.maxZ=100;
        fresh.heightmapWidth=65;fresh.heightmapHeight=65;for(int y=0;y<65;++y)for(int x=0;x<65;++x)fresh.heightmapSamples<<float(x+y)/128;
        fresh.heightmapMinimumRaw=2000;fresh.heightmapMaximumRaw=20000;fresh.createOverlayPlane=true;fresh.overlayRawHeight=50000;fresh.overlayMaterialIndex=0;
        QByteArray freshBytes;check(source.buildNewCanvas(fresh,&freshBytes,&report),"nonflat land and raised full overlay source");
        QFile::remove(canvasNavigationMarkerPath(QFileInfo(created)));auto published=commitNewTerrainCanvas(QFileInfo(donor),freshBytes,QFileInfo(created),CTextureList::getInstance()->configuredResourceArchivePaths(),scratch.filePath("backups"));check(published.success,"create private coherent pair "+published.error);
        open(created);
        auto db=QSharedPointer<ei::SProfileDatabaseSnapshot>::create();db->sourceArchivePath=fixture+"/res/database.res";db->profileId="deja-vu";ei::ProfileDatabaseContext::instance().activate(db);
        const QString catalogPath=fixture+"/res/model-catalog.json";ei::TechnicalModelCatalog catalog;check(catalog.load(catalogPath,&error),"own grouping catalog");
        const QString mobPath=fixture+"/Maps/canvas205.mob",extraPath=fixture+"/Maps/extra205.mob",unloadedPath=fixture+"/Maps/unloaded205.mob",grpPath=fixture+"/Maps/canvas205.grp";
        SAiGraphPreviewBuildInput graphInput;graphInput.graphWidth=16;graphInput.graphHeight=16;graphInput.maximumTerrainHeight=100;graphInput.terrainHeights.fill(0,128*128);graphInput.waterDepths.fill(0,128*128);graphInput.terrainTypes.fill(0,128*128);
        const auto graph=CAiGraphPreviewBuilder::build(graphInput);check(graph.valid,"seed old navigation");const QByteArray oldGraph=graph.document.serialize(&error);check(!oldGraph.isEmpty(),"old graph serializes");write(grpPath,oldGraph);
        { CMob mob;mob.setFileName(QFileInfo(mobPath));mob.setQuestMob(false);mob.ensureObjectSection();mob.setRanges(true,{SRange(1,1000)});mob.generateDiplomacyTable();
          for(int id:{101,102}){auto* u=new CUnit;u->setMapId(id);QVector3D initial=id==101?QVector3D(8,8,3):QVector3D(50,50,7);u->setPos(initial);
            for(int i=0;i<5;++i){u->createPatrolByIndex(i,-1);u->createViewByIndex(i,0,-1);}auto state=u->movementState();for(int i=1;i<state.size();++i)state[i]=QVector3D(9+i,10+i,0);check(u->restoreMovementState(state),"five logic movement fixture");mob.addNode(u);
          }
          auto* trap=new CMagicTrap;trap->setMapId(103);QVector3D tp(15,15,5),tz(16,17,0),tc(18,19,0);trap->setPos(tp);trap->createActZone()->setPos(tz);trap->createCastPoint()->setPos(tc);mob.addNode(trap);
          mob.m_aiGraph=oldGraph;QByteArray bytes;mob.serializeMob(bytes,false);check(!bytes.isEmpty(),"synthetic valid MOB");write(mobPath,bytes);write(extraPath,bytes);write(unloadedPath,bytes);
        }
        const QJsonObject tag{{"instanceId","{20261017-0205-4020-8000-000000000001}"},{"name","Fixture group"},{"assemblyId","fixture-assembly"}};
        QJsonArray members{QJsonObject{{"id",101},{"type",int(ENodeType::eUnit)},{"model",""}}};
        catalog.setAssemblyInstances(mobPath,QJsonObject{{"mobSha256",QString::fromLatin1(QCryptographicHash::hash(read(mobPath),QCryptographicHash::Sha256).toHex())},{"groups",QJsonArray{QJsonObject{{"tag",tag},{"members",members}}}}});
        write(catalogPath,catalog.stagedBytes());
        QFileInfo mobFile(mobPath),extraFile(extraPath);check(view.loadMob(mobFile)&&view.loadMob(extraFile),"load primary and supplementary MOB");
        view.m_environmentMob=view.m_aMob.first();view.changeCurrentMob(view.m_aMob.first());view.refreshAiGraphOwnership();
        // Production catalog lookup supplies an associated MOB which is not open.
        SMapCatalogEntry entry;entry.mprPath=created;entry.mobPath=unloadedPath;entry.mobName="unloaded205";window.m_mapCatalog.entries.append(entry);
        window.m_mapTestEntry.mobName="canvas205";
        QMap<QString,QVector<QVector3D>> originalMovement;QVector<QVector3D> trapPoints;
        for(auto* mob:view.m_aMob){for(int id:{101,102}){auto* unit=dynamic_cast<CUnit*>(mob->nodeByMapId(id));check(unit&&unit->movementState().size()==16,"all five guard/patrol/look sets loaded");originalMovement[mob->filePath().absoluteFilePath()+QString::number(id)]=unit->movementState();}}
        auto* initialTrap=dynamic_cast<CMagicTrap*>(view.m_aMob.first()->nodeByMapId(103));trapPoints<<initialTrap->position()<<initialTrap->actZones().first()->position()<<initialTrap->castPoints().first()->position();
        check(!view.m_aMob.first()->nodeByMapId(101)->assemblyTag().isEmpty(),"grouping restored before resize");
        view.openMapParameters();auto* apply=view.m_mprSettingsDock->findChild<QPushButton*>("applyCurrentMprCanvas");check(apply,"real current-MPR Apply button");auto* tabs=view.m_mprSettingsDock->findChild<QTabWidget*>("mprSettingsTabs");for(int i=0;i<tabs->count();++i)if(tabs->widget(i)->isAncestorOf(apply))tabs->setCurrentIndex(i);
        auto* width=view.m_mprSettingsDock->findChild<QSpinBox*>("mprCanvasWidth");auto* height=view.m_mprSettingsDock->findChild<QSpinBox*>("mprCanvasHeight");auto* xOffset=view.m_mprSettingsDock->findChild<QSpinBox*>("mprCanvasOffsetX");auto* yOffset=view.m_mprSettingsDock->findChild<QSpinBox*>("mprCanvasOffsetY");
        check(width&&height&&xOffset&&yOffset,"real canvas controls");
        view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString worldError;check(view.m_worldTerrainSession.load(created,&worldError),"dirty world base");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(created),&report),"world atlas");
        panel.activateMode(ELandscapeEditMode::TilePaint);panel.showAllPaintMaterials();check(panel.selectPaintTerrainType(2013464699),"dirty ground material");panel.findChild<QToolButton*>("vertexPaintTarget")->click();panel.findChild<QCheckBox*>("modernAutoResolveBlends")->setChecked(true);panel.findChild<QSlider*>("terrainPaintRadius")->setValue(1);QTest::qWait(300);
        QVector3D world(24,24,0);view.m_pLand->projectPt(world,false);view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();QTest::qWait(100);view.makeCurrent();
        const auto prior=terrain(view.m_mprDocument,view.m_terrainEdit);const auto p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));const QPoint pt(qRound(p.x()),qRound(view.height()-p.y()));QMouseEvent hover(QEvent::MouseMove,pt,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&hover);QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,pt);
        QElapsedTimer wait;wait.start();while(wait.elapsed()<12000){QTest::qWait(10);if(!view.m_tileStrokeFinishing&&terrain(view.m_mprDocument,view.m_terrainEdit)!=prior)break;}
        check(terrain(view.m_mprDocument,view.m_terrainEdit)!=prior,"real unsaved ground mouse stroke");
        auto recovery=view.prepareLandscapeRecoveryTask(&report);check(bool(recovery),"capture unsaved atlas artwork baseline");auto prepared=recovery();check(prepared.success&&!prepared.atlasBytes.isEmpty(),"dirty preview has coherent recovery atlas");CResArchiveDocument expectedArchive;check(expectedArchive.loadBytes(prepared.atlasBytes),"decode expected artwork archive");const CTerrainEditModel expected=view.m_terrainEdit;const QByteArray beforeMap=read(created),beforeAtlas=read(atlasPath),beforeMob=read(mobPath),beforeExtra=read(extraPath),beforeUnloaded=read(unloadedPath),beforeCatalog=read(catalogPath);
        width->setValue(3);height->setValue(3);xOffset->setValue(5);yOffset->setValue(3);cancelNext=true;QTest::mouseClick(apply,Qt::LeftButton);
        check(read(created)==beforeMap&&read(atlasPath)==beforeAtlas&&read(mobPath)==beforeMob&&read(extraPath)==beforeExtra&&read(unloadedPath)==beforeUnloaded&&read(catalogPath)==beforeCatalog&&!canvasNavigationIsStale(QFileInfo(created)),"Cancel leaves entire file cohort exact");
        check(terrain(view.m_mprDocument,view.m_terrainEdit)==terrain(view.m_mprDocument,expected),"Cancel preserves unsaved painting");
        QTest::mouseClick(apply,Qt::LeftButton);check(unexpected==0,"Apply has no warning/failure dialogs");check(view.m_pLand->filePath().absoluteFilePath()==QFileInfo(created).absoluteFilePath(),"same name and absolute path after real UI Apply");
        CResArchiveDocument actualArchive;check(actualArchive.loadBytes(read(atlasPath)),"reopen saved artwork archive");int pages=0;bool exactPages=true;for(const auto& member:expectedArchive.members())if(member.name.endsWith(".mmp",Qt::CaseInsensitive)){++pages;exactPages&=actualArchive.memberPayload(member.name)==member.payload;}check(pages>=19&&exactPages,"all original SD/HD and generated MMP page payloads byte-exact");check(view.m_terrainEdit.tileCountX()==48&&view.m_terrainEdit.tileCountY()==48,"expanded dimensions 3x3 sectors");
        bool sameVertices=true,sameTiles=true;for(int y=0;y<expected.vertexCountY();++y)for(int x=0;x<expected.vertexCountX();++x){sameVertices&=expected.height(x,y)==view.m_terrainEdit.height(x+10,y+26);sameVertices&=expected.terrainVertexOffset(x,y)==view.m_terrainEdit.terrainVertexOffset(x+10,y+26);}
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})sameTiles&=expected.packedTile(x,y,layer)==view.m_terrainEdit.packedTile(x+5,y+13,layer);sameTiles&=expected.overlayMaterialIndex(x,y)==view.m_terrainEdit.overlayMaterialIndex(x+5,y+13);}
        check(sameVertices,"every nonflat height and vertex offset retained at odd tile offset");check(sameTiles,"all ground and overlay tiles/materials retained including unsaved paint");check(view.m_terrainEdit.overlayMaterialIndex(0,0)<0,"expansion does not invent overlay outside original");
        for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}){QVector<qint32> a,b,ignored;expected.copyVirtualMaterialFieldState(layer,&a,&ignored);view.m_terrainEdit.copyVirtualMaterialFieldState(layer,&b,&ignored);bool same=true;for(int y=0;y<33;++y)for(int x=0;x<33;++x)same&=a.value(y*33+x)==b.value((y+13)*49+x+5);check(same,"semantic material field translated and survives reload");}
        check(view.canvasNavigationStale()&&canvasNavigationIsStale(QFileInfo(created))&&!view.m_aiGraphRegeneratedFresh,"persistent stale graph flag, no automatic regeneration");
        QByteArray candidate;check(!view.buildGeneratedAiGraphCandidate(&candidate,&error,nullptr,true),"even forced staging refuses obsolete graph before explicit recalc");
        auto verifyMob=[&](const QString& path,int dx,int dy,bool cropped){CMob mob;mob.attach(&view,view.m_pProgress);QFileInfo file(path);view.makeCurrent();check(mob.readMob(file,false)==0,"reopen related MOB "+file.fileName());
            for(int id:{101,102}){auto* unit=dynamic_cast<CUnit*>(mob.nodeByMapId(id));if(cropped&&id==102){check(!unit,"outside object removed explicitly");continue;}check(unit,"ID survives");auto baseline=originalMovement.value(mobPath+QString::number(id));auto moved=unit->movementState();bool exact=moved.size()==baseline.size();for(int i=0;i<moved.size();++i)exact&=moved[i].x()==baseline[i].x()+dx&&moved[i].y()==baseline[i].y()+dy;check(exact,"root and all five guard/patrol/look XY translated");check(unit->position().z()==baseline.first().z(),"raw object Z unchanged");}
            auto* trap=dynamic_cast<CMagicTrap*>(mob.nodeByMapId(103));check(trap,"trap ID survives");QVector<QVector3D> points{trap->position(),trap->actZones().first()->position(),trap->castPoints().first()->position()};bool exact=true;for(int i=0;i<3;++i)exact&=points[i].x()==trapPoints[i].x()+dx&&points[i].y()==trapPoints[i].y()+dy;check(exact,"trap root/activation/cast points translated");check(mob.aiGraphData()==oldGraph,"embedded graph byte-exact for comparison");};
        for(const auto& path:{mobPath,extraPath,unloadedPath})verifyMob(path,10,26,false);
        check(view.m_aMob.size()==2&&view.currentMob()->filePath().absoluteFilePath()==mobPath,"loaded composition and active MOB retained");check(!view.m_aMob.first()->nodeByMapId(101)->assemblyTag().isEmpty(),"grouping/hash updated with MOB commit");check(read(grpPath)==oldGraph,"external GRP remains byte-exact");
        SMprCanvasResizeRequest back;back.sectorCountX=2;back.sectorCountY=2;back.oldTileOffsetX=-5;back.oldTileOffsetY=-13;
        check(view.applyCanvasResizeToCurrent(back,false,&error),"crop back in place "+error);check(terrain(view.m_mprDocument,view.m_terrainEdit)==terrain(view.m_mprDocument,expected),"roundtrip entire sector payloads exact: heights normals paint surfaces");
        for(const auto& path:{mobPath,extraPath,unloadedPath})verifyMob(path,0,0,false);
        // The source guard must cover all files before writing any candidate.
        const auto raceMap=read(created),raceAtlas=read(atlasPath),raceMob=read(mobPath);racePath=created;raceBytes=raceMap+"concurrent-owner-edit";
        SMprCanvasResizeRequest expand;expand.sectorCountX=3;expand.sectorCountY=3;
        check(!view.applyCanvasResizeToCurrent(expand,false,&error),"concurrent disk edit refuses whole batch");check(read(created)==raceBytes&&read(atlasPath)==raceAtlas&&read(mobPath)==raceMob,"race retains new owner bytes and unchanged companions");write(created,raceMap);
        SMprCanvasResizeRequest crop;crop.sectorCountX=1;crop.sectorCountY=1;
        check(!view.applyCanvasResizeToCurrent(crop,false,&error)&&read(created)==raceMap,"crop refuses to discard outside objects without explicit flag");
        check(view.applyCanvasResizeToCurrent(crop,true,&error),"explicit destructive crop "+error);for(const auto& path:{mobPath,extraPath,unloadedPath})verifyMob(path,0,0,true);
        check(view.canvasNavigationStale(),"stale flag survives repeated resize/reload");
        view.m_aiGraphRegenerateButton->click();wait.restart();while(wait.elapsed()<60000&&!view.m_aiGraphRegeneratedFresh)QTest::qWait(10);
        check(view.m_aiGraphRegeneratedFresh,"explicit regenerate button completes");check(view.buildGeneratedAiGraphCandidate(&candidate,&error),"fresh navigation staging available "+error);CAiGraphDocument generated;check(generated.parse(candidate,&error)&&generated.width()==8&&generated.height()==8,"recalc uses new terrain dimensions not old embedded graph");check(view.canvasNavigationStale(),"preview recalc does not falsely mark disk graph current");
        window.m_mapTestEntry.mobName="canvas205";persistChoice="aiGraphBakePrimary";window.persistAiGraph();persistChoice.clear();check(!view.canvasNavigationStale()&&!QFileInfo::exists(canvasNavigationMarkerPath(QFileInfo(created))),"explicit bake publishes fresh graph and retires marker");check(unexpected==0,"no unexpected warnings in successful flow");
        // Save As remains available and retains its distinct-source contract.
        CMprDocument disk;check(disk.load(QFileInfo(created),&report),"Save As source");SMprSaveCommitRequest saveAs;saveAs.source=QFileInfo(created);saveAs.destination=QFileInfo(fixture+"/Maps/copy205.mpr");saveAs.backupRootOverride=scratch.filePath("backups");const auto beforeSaveAs=read(created);auto copy=commitStagedMprCanvasResize(disk,disk,crop,saveAs);check(copy.success&&read(created)==beforeSaveAs&&!read(saveAs.destination.filePath()).isEmpty(),"Save As regression keeps source unchanged "+copy.error);
        const QString a=scratch.filePath("rollback-a"),b=scratch.filePath("rollback-b");write(a,"old-a");write(b,"old-b");SMapSaveFile f1;f1.path=a;f1.before="old-a";f1.after="new-a";SMapSaveFile f2;f2.path=b;f2.before="old-b";f2.after="new-b";auto rollback=commitMapSaveBatch({f1,f2},scratch.filePath("rollback"),1);check(!rollback.success&&rollback.rollbackComplete&&read(a)=="old-a"&&read(b)=="old-b","mid-batch failure restores already committed files");
        check(read(donor)==donorBytes,"donor untouched");view.m_worldTerrainSession.clear();view.unloadMob("");view.unloadLand(false);qApp->removeEventFilter(&hidden);out<<"PASS canvas in place "<<passes<<" checks\n";return 0;
    }catch(const QString& failure){out<<"FAIL canvas in place "<<failure<<"\n";return 1;}
}

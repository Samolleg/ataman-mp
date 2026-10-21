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
#include "res_archive_index.h"
#include "terrain_atlas_compact.h"
#include "neighbor_scene_data.h"
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
                || target==QFileInfo(fixture+"/Maps/new213-atlas.res").absoluteFilePath();
        });
        int badDialogs=0;
        QTimer messages;QObject::connect(&messages,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){
            out<<"DIALOG "<<b->text()<<"\n";out.flush();
            if(auto* save=b->button(QMessageBox::Save))save->click();else {++badDialogs;b->reject();QTimer::singleShot(0,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))if(d->findChild<QLineEdit*>("newMprNamespace"))d->reject();});}
        }});messages.start(50);
        auto open=[&](const QString& path){view.m_worldTerrainSession.clear();view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(path),false);QTest::qWait(100);view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(view.m_mprDocument.mapInfo().valid&&view.m_terrainAtlasAnalysis.usable,"open with usable real atlas");view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString e;CValidationReport r;check(view.m_worldTerrainSession.load(path,&e),"load world document");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(path),&r),"world atlas ready");};
        CValidationReport report;QString error;
        const QString donor=fixture+"/Maps/zone7.mpr",created=fixture+"/Maps/new213.mpr";
        const auto donorBytes=read(donor);
        auto bindingCheck=[&](const QString& path) {
            CMprDocument map;CValidationReport r;check(map.load(QFileInfo(path),&r),"map structural reopen");
            auto b=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({fixture+"/res/HDPack_Terrain.res"},map.mapInfo().innerNamespace);
            check(b.success()&&b.profile.textureCount==int(map.mapInfo().textureCount),"complete exact hash-bound atlas in HDPack");
            check(terrainAtlasHasHash(map.mapInfo().innerNamespace),"map has hash namespace");
            return b;
        };
        auto neighbors=[&]() {
            open(donor);view.m_neighborLandscapesVisible=true;
            QStringList paths{created,fixture+"/Maps/zone1.mpr"};
            for(const auto& path:paths) {
                CMprDocument m;CValidationReport r;check(m.load(QFileInfo(path),&r),"neighbor MPR readable");
                SNeighborMapPlacement p;p.zoneId=QFileInfo(path).baseName();p.mprPath=path;
                p.size={m.mapInfo().sectorCountX*32.0,m.mapInfo().sectorCountY*32.0};
                p.origin={double(view.m_pLand->widthInWorldUnits()),0};p.worldOrigin=p.origin;
                view.m_neighborPlacements.append(p);
            }
            view.loadNeighborLandscapes();QElapsedTimer timer;timer.start();
            while(timer.elapsed()<30000&&view.m_neighborLandscapes.size()!=2)QTest::qWait(20);
            check(view.m_neighborLandscapes.size()==2,"real async background loader accepts new and existing Dejavu neighbors");
            for(const auto& n:view.m_neighborLandscapes)check(n.landscape&&n.landscape->glTexture()&&n.landscape->glTexture()->width()>256,"neighbor has authored GPU texture");
            const auto id=CWorldTerrainSession::key(created);QString e;
            check(view.m_worldTerrainSession.load(created.toUpper(),&e),"case-variant world document load");
            check(view.ensureWorldTerrainAtlas(id,&report),"new map resolves through world with case-variant path");
            view.clearNeighborLandscapes();view.m_neighborPlacements.clear();
        };
        if(qEnvironmentVariableIsSet("EI_NEW_MAP_MIGRATE")) {
            const QString source=fixture+"/Maps/gipat2.mpr",legacyPath=fixture+"/Maps/legacy213.mpr",sidecar=fixture+"/Maps/legacy213-atlas.res";
            CMprDocument m;QByteArray candidate,atlas;CMprEditTransaction tx("Legacy sidecar fixture");
            check(m.load(QFileInfo(source),&report)&&m.applyIsolatedNamespaceTransaction(tx,"legacy213",&candidate,&report),"prepare isolated legacy MPR namespace");
            check(buildNewTerrainAtlas(QFileInfo(source),candidate,{fixture+"/Maps/gipat2-atlas.res"},&atlas,&error),"prepare exact legacy sidecar from copied gipat2");
            for(const auto& pair:QMap<QString,QByteArray>{{legacyPath,candidate},{sidecar,atlas}}.toStdMap()) {QFile f(pair.first);check(f.open(QIODevice::WriteOnly)&&f.write(pair.second)==pair.second.size(),"write private legacy fixture");}
            open(legacyPath);check(view.m_activeTerrainArchivePath.compare(sidecar,Qt::CaseInsensitive)==0,"legacy map initially binds sidecar");
            STerrainBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=1;
            const auto edit=view.m_terrainEdit.makeRaiseLower(brush,20);
            check(view.m_terrainEdit.apply(edit,true,&report),"edit legacy map height");
            const auto expected=heights(view.m_terrainEdit);
            check(view.saveWorldTerrainMaps(),"world Save migrates legacy source to runtime pack");
            bindingCheck(legacyPath);
            check(read(sidecar)==atlas,"migration never rewrites legacy sidecar");
            open(legacyPath.toUpper());check(heights(view.m_terrainEdit)==expected,"migrated map reopens with exact edited height");
            check(view.m_activeTerrainArchivePath.compare(fixture+"/res/HDPack_Terrain.res",Qt::CaseInsensitive)==0,"migrated map binds runtime pack on reopen");
            view.m_worldTerrainSession.clear();view.unloadLand(false);qApp->removeEventFilter(&hidden);out<<"PASS migration "<<passes<<" checks\n";return 0;
        }
        if(qEnvironmentVariableIsSet("EI_NEW_MAP_SCREENSHOT")) {
            open(created);
            const auto capture=view.captureMinimapScene(false,1024,false);
            check(capture.capturedReviewImage.save(fixture+"/new213-reopened-top.png"),"capture textured terrain with native offscreen renderer");
            QVector3D look(32,16,0);view.m_pLand->projectPt(look,false);
            view.moveCameraToPoint(look);view.m_cam->moveAwayOn(80);
            view.makeCurrent();view.paintGL();
            check(view.grabFrameBuffer().save(fixture+"/new213-reopened-view.png"),"capture viewport above actual relief height");
            view.m_worldTerrainSession.clear();view.unloadLand(false);qApp->removeEventFilter(&hidden);out<<"PASS screenshots "<<passes<<" checks\n";return 0;
        }
        if(qEnvironmentVariableIsSet("EI_NEW_MAP_REOPEN")) {
            open(created.toUpper());bindingCheck(created);
            const auto expected=read(fixture+"/expected-terrain.bin");
            check(terrain(view.m_mprDocument,view.m_terrainEdit)==expected,"new process restores exact saved terrain");
            check(heights(view.m_terrainEdit)==read(fixture+"/expected-heights.bin"),"new process restores exact relief");
            const auto capture=view.captureMinimapScene(false,1024,false);
            check(capture.capturedReviewImage.save(fixture+"/new213-reopened-top.png"),"save real OpenGL terrain screenshot after fresh-process reopen");
            neighbors();
            const auto rescue=bindingCheck(fixture+"/Maps/gipat2.mpr");
            const auto original=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({fixture+"/Maps/gipat2-atlas.res"},rescue.profile.mprNamespace);
            check(original.success()&&original.decodedAtlasPixelSha256==rescue.decodedAtlasPixelSha256,"recovered gipat2 matches exact original sidecar pixels");
            open(fixture+"/Maps/gipat2.mpr");
            check(view.m_terrainAtlasAnalysis.usable,"recovered gipat2 opens with complete material palette");
            view.m_worldTerrainSession.clear();view.unloadLand(false);qApp->removeEventFilter(&hidden);out<<"PASS fresh session "<<passes<<" checks\n";return 0;
        }
        open(donor);
        // Legacy sidecar background load before migration, separate from active resource.
        auto legacy=CLandscape::createReadOnlyBackground();CMprDocument legacyMap;check(legacyMap.load(QFileInfo(fixture+"/Maps/gipat2.mpr"),&report),"legacy current gipat2 decode");
        QMap<QString,QByteArray> legacyMembers;for(const auto& m:legacyMap.members())legacyMembers[m.name]=m.payload;
        view.makeCurrent();check(legacy->readPreparedBackground(QFileInfo((fixture+"/Maps/gipat2.mpr").toUpper()),legacyMembers),"legacy sidecar read as background with different path case");
        check(legacy->glTexture()&&legacy->glTexture()->width()>512,"legacy background uses full real pages rather than 256px fallback");legacy.clear();
        QImage heightmap(65,33,QImage::Format_RGB32);for(int y=0;y<33;++y)for(int x=0;x<65;++x)heightmap.setPixel(x,y,qRgb(x*3+y,x*3+y,x*3+y));
        const QString hm=fixture+"/heightmap.png";check(heightmap.save(hm),"nonflat source heightmap");
        bool configured=false;int createClicks=0;bool savedDialog=false;
        panel.findChild<QCheckBox*>("terrainUsedMaterialsOnly")->setChecked(true);
        QTimer drive;QObject::connect(&drive,&QTimer::timeout,[&](){
            for(auto* w:QApplication::topLevelWidgets()) {
                if(auto* file=qobject_cast<QFileDialog*>(w)) {if(!savedDialog){savedDialog=true;file->selectFile(created);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);}continue;}
                auto* dialog=qobject_cast<QDialog*>(w);if(!dialog||configured)continue;
                auto* ns=dialog->findChild<QLineEdit*>("newMprNamespace");if(!ns)continue;
                configured=true;ns->setText("fresh213");dialog->findChild<QSpinBox*>("newMprSectorsX")->setValue(2);dialog->findChild<QSpinBox*>("newMprSectorsY")->setValue(1);dialog->findChild<QLineEdit*>("newMprHeightmapPath")->setText(hm);
                auto* buttons=dialog->findChild<QDialogButtonBox*>();++createClicks;
                QTimer::singleShot(0,buttons->button(QDialogButtonBox::Ok),&QAbstractButton::click);
            }
        });drive.start(30);
        QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))d->reject();});watchdog.start(60000);
        check(QMetaObject::invokeMethod(&window,"on_actionNew_MPR_triggered",Qt::DirectConnection),"invoke production New MPR action");drive.stop();watchdog.stop();
        check(configured&&savedDialog&&createClicks==1&&badDialogs==0,"real New MPR dialog created one MPR/atlas pair");
        check(QFileInfo(created).isFile()&&!QFileInfo(fixture+"/Maps/new213-atlas.res").exists(),"new map has no sidecar RES");bindingCheck(created);
        view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(terrainAtlasHasHash(view.m_mprDocument.mapInfo().innerNamespace)&&view.m_terrainAtlasAnalysis.usable,"created map opened with its independent namespace");
        check(!panel.findChild<QCheckBox*>("terrainUsedMaterialsOnly")->isChecked(),"new map shows all materials despite prior used-only filter");
        const auto minimap=view.captureMinimapScene(false,256,false);
        check(!minimap.capturedReviewImage.isNull()&&!minimap.capturedUnlitReviewImage.isNull(),
              "new map hardware minimap lit and unlit capture after document transition");
        view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString worldError;check(view.m_worldTerrainSession.load(created,&worldError),"new world document");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(created),&report),"new world atlas");const QByteArray nonflat=heights(view.m_terrainEdit);check(view.m_terrainEdit.height(8,8)!=view.m_terrainEdit.height(40,8),"created relief remains nonflat");

        auto stroke=[&](qint32 material,bool surface){panel.activateMode(surface?ELandscapeEditMode::OverlaySurface:ELandscapeEditMode::TilePaint);panel.showAllPaintMaterials();check(panel.selectPaintTerrainType(material),"select paint material");if(auto* b=panel.findChild<QToolButton*>("vertexPaintTarget"))b->click();if(auto* b=panel.findChild<QCheckBox*>("modernAutoResolveBlends"))b->setChecked(true);if(auto* b=panel.findChild<QSlider*>("terrainPaintRadius"))b->setValue(1);QTest::qWait(500);QVector3D world(24,24,0);view.m_pLand->projectPt(world,false);view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();QTest::qWait(100);QCoreApplication::processEvents();view.makeCurrent();view.ensureTerrainAutoCatalogReady();const auto prior=terrain(view.m_mprDocument,view.m_terrainEdit);
            const QVector3D p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));const QPoint point(qRound(p.x()),qRound(view.height()-p.y()));check(view.rect().contains(point),"paint point in viewport");QMouseEvent hover(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&hover);QCoreApplication::processEvents();QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,point);QElapsedTimer wait;wait.start();while(wait.elapsed()<12000){QTest::qWait(10);if(!view.m_tileStrokeFinishing&&terrain(view.m_mprDocument,view.m_terrainEdit)!=prior)break;}
            check(terrain(view.m_mprDocument,view.m_terrainEdit)!=prior,surface?"real add-surface mouse stroke":"real ground mouse stroke");check(heights(view.m_terrainEdit)==nonflat,"painting preserves full relief");};
        const auto t0=terrain(view.m_mprDocument,view.m_terrainEdit);stroke(2013464699,false);const auto t1=terrain(view.m_mprDocument,view.m_terrainEdit);view.worldTerrainHistory(false,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t0,"ground Undo exact");view.worldTerrainHistory(true,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t1,"ground Redo exact");
        qint32 overlay=0;for(auto f:CTerrainAtlasPresetCatalog::globalUniformFamilies())if(f.layer==ETerrainAtlasLayer::Overlay){overlay=f.id;break;}check(overlay!=0,"surface brush available");stroke(overlay,true);const auto t2=terrain(view.m_mprDocument,view.m_terrainEdit);view.worldTerrainHistory(false,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t1,"surface Undo exact");view.worldTerrainHistory(true,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t2,"surface Redo exact");
        view.saveLand();check(badDialogs==0&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"Save MPR with land and new surface");
        const auto saved=terrain(view.m_mprDocument,view.m_terrainEdit);open(created);check(terrain(view.m_mprDocument,view.m_terrainEdit)==saved&&heights(view.m_terrainEdit)==nonflat,"Save/reopen artwork surfaces and relief exact");
        stroke(2142727678,false);view.saveLand();check(badDialogs==0&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"paint and Save again after reopen");
        const auto savedAgain=terrain(view.m_mprDocument,view.m_terrainEdit);open(created);
        check(terrain(view.m_mprDocument,view.m_terrainEdit)==savedAgain&&heights(view.m_terrainEdit)==nonflat,"second reopen remains editable with exact relief");
        bindingCheck(created);
        for(const auto& item:QMap<QString,QByteArray>{{"expected-terrain.bin",savedAgain},{"expected-heights.bin",nonflat}}.toStdMap()) {QFile f(fixture+"/"+item.first);check(f.open(QIODevice::WriteOnly)&&f.write(item.second)==item.second.size(),"persist fresh-session expectation");}
        neighbors();check(read(donor)==donorBytes,"existing Dejavu donor map unchanged");
        view.m_worldTerrainSession.clear();view.unloadLand(false);qApp->removeEventFilter(&hidden);out<<"PASS new terrain canvas "<<passes<<" checks\n";return 0;
    }catch(const QString& failure){out<<"FAIL new terrain canvas "<<failure<<"\n";return 1;}
}

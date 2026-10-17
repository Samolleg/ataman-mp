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
        int badDialogs=0;
        QTimer messages;QObject::connect(&messages,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* b=qobject_cast<QMessageBox*>(w)){
            out<<"DIALOG "<<b->text()<<"\n";out.flush();
            if(auto* save=b->button(QMessageBox::Save))save->click();else {++badDialogs;b->reject();}
        }});messages.start(50);
        auto open=[&](const QString& path){view.m_worldTerrainSession.clear();view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(path),false);QTest::qWait(100);view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(view.m_mprDocument.mapInfo().valid&&view.m_terrainAtlasAnalysis.usable,"open with usable real atlas");view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString e;CValidationReport r;check(view.m_worldTerrainSession.load(path,&e),"load world document");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(path),&r),"world atlas ready");};
        const QString donor=fixture+"/Maps/zone1.mpr",broken=fixture+"/Maps/gipat1.mpr";
        const QByteArray brokenBytes=read(broken),donorBytes=read(donor);check(!brokenBytes.isEmpty(),"gipat1 rescue readable");
        QByteArray repairedAtlas;QString error;
        check(buildNewTerrainAtlas(QFileInfo(donor),brokenBytes,CTextureList::getInstance()->configuredResourceArchivePaths(),&repairedAtlas,&error),"build gipat1 atlas from exact template metadata: "+error);
        const QString repairPath=fixture+"/Maps/gipat1-atlas.res";
        {QFile f(repairPath);check(f.open(QIODevice::WriteOnly)&&f.write(repairedAtlas)==repairedAtlas.size(),"stage repair sidecar");}
        check(read(broken)==brokenBytes&&read(donor)==donorBytes,"repair leaves both MPRs byte-exact");
        auto binding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({repairPath},"gipat1");
        check(binding.success()&&binding.profile.textureCount==19,"gipat1 coherent exact 19-page binding");
        CResArchiveDocument donorArchive,repairArchive;check(repairArchive.loadBytes(repairedAtlas),"repair archive reopen");
        auto originalBinding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(CTextureList::getInstance()->configuredResourceArchivePaths(),"zone1_d5d6e7d2f1b5");
        check(originalBinding.success()&&binding.decodedAtlasPixelSha256==originalBinding.decodedAtlasPixelSha256,"gipat1 exact donor decoded artwork");
        open(broken);const QByteArray originalHeights=heights(view.m_terrainEdit);
        auto fullPalette=[&](){QSet<qint32> land,overlay;for(auto c:view.m_modernLandPaintChoices)land.insert(c.id);for(auto c:view.m_modernOverlayPaintChoices)overlay.insert(c.id);
            int expectedLand=0,expectedOverlay=0;for(auto f:CTerrainAtlasPresetCatalog::globalUniformFamilies()) {const bool ground=f.layer==ETerrainAtlasLayer::Land;check((ground?land:overlay).contains(f.id),"global material "+f.name);if(ground)++expectedLand;else ++expectedOverlay;}
            check(land.size()==expectedLand&&overlay.size()==expectedOverlay,"complete palette without omitted or invented materials");out<<"CATALOG "<<expectedLand<<" ground "<<expectedOverlay<<" overlay\n";};
        fullPalette();
        CMprDocument before;CValidationReport report;check(before.loadBytes(brokenBytes,"gipat1.mpr",&report),"original terrain decode");CTerrainEditModel beforeModel;check(beforeModel.load(before,&report)&&heights(beforeModel)==originalHeights,"repaired gipat1 entire height grid unchanged");
        open(donor);
        QImage heightmap(65,33,QImage::Format_RGB32);for(int y=0;y<33;++y)for(int x=0;x<65;++x)heightmap.setPixel(x,y,qRgb(x*3+y,x*3+y,x*3+y));
        const QString hm=fixture+"/heightmap.png";check(heightmap.save(hm),"nonflat source heightmap");
        const QString created=fixture+"/Maps/new204.mpr";bool configured=false;int createClicks=0;bool savedDialog=false;
        panel.findChild<QCheckBox*>("terrainUsedMaterialsOnly")->setChecked(true);
        QTimer drive;QObject::connect(&drive,&QTimer::timeout,[&](){
            for(auto* w:QApplication::topLevelWidgets()) {
                if(auto* file=qobject_cast<QFileDialog*>(w)) {if(!savedDialog){savedDialog=true;file->selectFile(created);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);}continue;}
                auto* dialog=qobject_cast<QDialog*>(w);if(!dialog||configured)continue;
                auto* ns=dialog->findChild<QLineEdit*>("newMprNamespace");if(!ns)continue;
                configured=true;ns->setText("fresh204");dialog->findChild<QSpinBox*>("newMprSectorsX")->setValue(2);dialog->findChild<QSpinBox*>("newMprSectorsY")->setValue(1);dialog->findChild<QLineEdit*>("newMprHeightmapPath")->setText(hm);
                auto* buttons=dialog->findChild<QDialogButtonBox*>();++createClicks;
                QTimer::singleShot(0,buttons->button(QDialogButtonBox::Ok),&QAbstractButton::click);
            }
        });drive.start(30);
        QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))d->reject();});watchdog.start(60000);
        check(QMetaObject::invokeMethod(&window,"on_actionNew_MPR_triggered",Qt::DirectConnection),"invoke production New MPR action");drive.stop();watchdog.stop();
        check(configured&&savedDialog&&createClicks==1&&badDialogs==0,"real New MPR dialog created one MPR/atlas pair");
        check(QFileInfo(created).isFile()&&QFileInfo(fixture+"/Maps/new204-atlas.res").isFile(),"both creation files exist");
        view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(view.m_mprDocument.mapInfo().innerNamespace=="fresh204"&&view.m_terrainAtlasAnalysis.usable,"created map opened with its independent namespace");
        check(!panel.findChild<QCheckBox*>("terrainUsedMaterialsOnly")->isChecked(),"new map shows all materials despite prior used-only filter");fullPalette();
        const auto minimap=view.captureMinimapScene(false,256,false);
        check(!minimap.capturedReviewImage.isNull()&&!minimap.capturedUnlitReviewImage.isNull(),
              "new map hardware minimap lit and unlit capture after document transition");
        for(const auto& family:CTerrainAtlasPresetCatalog::globalUniformFamilies()) {
            STerrainTransitionSignature signature;
            signature.northWest=signature.northEast=signature.southEast=signature.southWest=family.id;
            CValidationReport materialReport;
            check(view.ensureGlobalTerrainMaterialSources(signature,&materialReport),
                  "real paint artwork available "+family.name+": "+materialReport.toText());
        }
        view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString worldError;check(view.m_worldTerrainSession.load(created,&worldError),"new world document");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(created),&report),"new world atlas");const QByteArray nonflat=heights(view.m_terrainEdit);check(view.m_terrainEdit.height(8,8)!=view.m_terrainEdit.height(40,8),"created relief remains nonflat");
        QByteArray rejected;check(!buildNewTerrainAtlas(QFileInfo(donor),read(created),{},&rejected,&error)&&rejected.isEmpty(),"missing source atlas refuses before publication");
        auto stroke=[&](qint32 material,bool surface){panel.activateMode(surface?ELandscapeEditMode::OverlaySurface:ELandscapeEditMode::TilePaint);panel.showAllPaintMaterials();check(panel.selectPaintTerrainType(material),"select paint material");if(auto* b=panel.findChild<QToolButton*>("vertexPaintTarget"))b->click();if(auto* b=panel.findChild<QCheckBox*>("modernAutoResolveBlends"))b->setChecked(true);if(auto* b=panel.findChild<QSlider*>("terrainPaintRadius"))b->setValue(1);QTest::qWait(500);QVector3D world(24,24,0);view.m_pLand->projectPt(world,false);view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();QTest::qWait(100);QCoreApplication::processEvents();view.makeCurrent();view.ensureTerrainAutoCatalogReady();const auto prior=terrain(view.m_mprDocument,view.m_terrainEdit);
            const QVector3D p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));const QPoint point(qRound(p.x()),qRound(view.height()-p.y()));check(view.rect().contains(point),"paint point in viewport");QMouseEvent hover(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&hover);QCoreApplication::processEvents();QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,point);QElapsedTimer wait;wait.start();while(wait.elapsed()<12000){QTest::qWait(10);if(!view.m_tileStrokeFinishing&&terrain(view.m_mprDocument,view.m_terrainEdit)!=prior)break;}
            check(terrain(view.m_mprDocument,view.m_terrainEdit)!=prior,surface?"real add-surface mouse stroke":"real ground mouse stroke");check(heights(view.m_terrainEdit)==nonflat,"painting preserves full relief");};
        const auto t0=terrain(view.m_mprDocument,view.m_terrainEdit);stroke(2013464699,false);const auto t1=terrain(view.m_mprDocument,view.m_terrainEdit);view.worldTerrainHistory(false,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t0,"ground Undo exact");view.worldTerrainHistory(true,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t1,"ground Redo exact");
        qint32 overlay=0;for(auto f:CTerrainAtlasPresetCatalog::globalUniformFamilies())if(f.layer==ETerrainAtlasLayer::Overlay){overlay=f.id;break;}check(overlay!=0,"surface brush available");stroke(overlay,true);const auto t2=terrain(view.m_mprDocument,view.m_terrainEdit);view.worldTerrainHistory(false,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t1,"surface Undo exact");view.worldTerrainHistory(true,true);check(terrain(view.m_mprDocument,view.m_terrainEdit)==t2,"surface Redo exact");
        view.saveLand();check(badDialogs==0&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"Save MPR with land and new surface");
        const auto saved=terrain(view.m_mprDocument,view.m_terrainEdit);open(created);check(terrain(view.m_mprDocument,view.m_terrainEdit)==saved&&heights(view.m_terrainEdit)==nonflat,"Save/reopen artwork surfaces and relief exact");fullPalette();
        stroke(2142727678,false);view.saveLand();check(badDialogs==0&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"paint and Save again after reopen");
        const auto savedAgain=terrain(view.m_mprDocument,view.m_terrainEdit);open(created);
        check(terrain(view.m_mprDocument,view.m_terrainEdit)==savedAgain&&heights(view.m_terrainEdit)==nonflat,"second reopen remains editable with exact relief");
        check(read(broken)==brokenBytes&&read(donor)==donorBytes,"source MPRs still unchanged after all regressions");
        const QString deliver=qEnvironmentVariable("EI_REPAIR_OUTPUT");if(!deliver.isEmpty()){QSaveFile f(deliver);check(f.open(QIODevice::WriteOnly)&&f.write(repairedAtlas)==repairedAtlas.size()&&f.commit(),"write tested gipat1 repair candidate");}
        view.m_worldTerrainSession.clear();view.unloadLand(false);qApp->removeEventFilter(&hidden);out<<"PASS new terrain canvas "<<passes<<" checks\n";return 0;
    }catch(const QString& failure){out<<"FAIL new terrain canvas "<<failure<<"\n";return 1;}
}

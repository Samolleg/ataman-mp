#include <QtWidgets>
#include <QtTest>
#include "view.h"
#include "camera.h"
#include "settings.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "endurance_document.h"
#include "terrain_image_sizes.h"
#include "res_archive_document.h"
#include "backup_manager.h"

int runScenarioLayerCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try{
        QTemporaryDir scratch;QDir::setCurrent(scratch.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        const QString fixture=qEnvironmentVariable("EI_MIXED_FIXTURE"),game=qEnvironmentVariable("EI_MIXED_GAME_ROOT"); check(!fixture.isEmpty()&&!game.isEmpty(),"explicit private fixture and game roots");
        CSettings settings;
        dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(game);
        dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res",game+"/MODS/DejaVu/res",game+"/Base/Res"});
        dynamic_cast<COptString*>(settings.opt(eOptSetResource,"terrainAuthoringArchive"))->setValue(qEnvironmentVariable("EI_MIXED_REFERENCE"));
        CTextureList::getInstance()->attachSettings(&settings);
        QUndoStack undo;QTableWidget table;table.setColumnCount(2);CProgressView progress;QLineEdit coords;CTreeView tree;CTileForm tiles;
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.setMouseTracking(true);
        QObject::connect(&view,&CView::terrainPublicationPreflightRequested,[&](const QString& path,bool* accepted){*accepted=QFileInfo(path).absoluteFilePath()==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath();});
        view.attach(&settings,&table,&undo,&progress,&coords,&tree,&tiles);view.resize(800,600);view.show();QCoreApplication::processEvents();
        check(view.isValid(),"hidden native GL");CLandscapeTools panel;view.attachLandscapeTools(&panel);
        const QFileInfo map(fixture+"/Maps/zone3obr.mpr");
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)){
            out<<"DIALOG "<<box->text()<<"\n";out.flush();
            if(auto* save=box->button(QMessageBox::Save)) save->click(); else if(box->windowTitle()=="Replace shared terrain atlas pages?"&&QFileInfo(view.m_activeTerrainArchivePath).absoluteFilePath()==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath())box->button(QMessageBox::Yes)->click();else box->reject();
        }});dialogs.start(50);
        view.loadLandscape(map,false);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"recovery map catalog");view.m_worldTerrainEnabled=true;
        { const auto initial=view.m_workingTerrainAtlas->activeTileImages();check(initial.front().width()==128&&terrainMaximumTileSize(initial)==256,"owner mixed128/256 fixture"); }
        auto setupWorld=[&](){QString error; CValidationReport report;view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();check(view.m_worldTerrainSession.load(map.absoluteFilePath(),&error),"world document loaded");view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(CWorldTerrainSession::key(map.absoluteFilePath()),&report),"world atlas bound");};setupWorld();
        const auto sectorOrder=view.m_mprDocument.sectors(); const auto sectors=[&](){QByteArray bytes;for(const auto& s:sectorOrder)bytes+=view.m_terrainEdit.sectorPayload(s.sectorX,s.sectorY);return bytes;};
        auto stroke=[&](int attempt){ view.m_worldTerrainEnabled=true;
            panel.activateMode(ELandscapeEditMode::TilePaint,true);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
            check(panel.selectPaintTerrainType(attempt==0?2013464699:2142727678),"select reported material");
            if(auto* button=panel.findChild<QToolButton*>("vertexPaintTarget"))button->click();
            if(auto* toggle=panel.findChild<QCheckBox*>("modernAutoResolveBlends"))toggle->setChecked(true);
            if(auto* radius=panel.findChild<QSlider*>("terrainPaintRadius"))radius->setValue(1);
            QElapsedTimer warm;warm.start();while(warm.elapsed()<4000){QCoreApplication::processEvents();QTest::qWait(10);}
            const auto before=sectors();
            QVector3D world(47,75,0);view.m_pLand->projectPt(world,false);view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();QTest::qWait(100);
            const auto projected=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));
            const QPoint point(qRound(projected.x()),qRound(view.height()-projected.y()));check(view.rect().contains(point),"paint projects into viewport");
            QMouseEvent hover(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&hover);QCoreApplication::processEvents();
            QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,point);
            QElapsedTimer done;done.start();while(done.elapsed()<8000){QCoreApplication::processEvents();QTest::qWait(10);if(!view.m_tileStrokeFinishing&&(view.nextWorldTerrainStroke(false)!=nullptr))break;}
            const auto after=sectors();check(after!=before&&(view.nextWorldTerrainStroke(false)!=nullptr),"actual vertex paint changes terrain with one Undo");
            check(view.worldTerrainHistory(false,true),"world Undo");check(sectors()==before,"paint Undo exact");check(view.worldTerrainHistory(true,true),"world Redo");check(sectors()==after,"paint Redo exact");return after;
        };
        const auto recoveryTerrain=sectors();
        const auto untouchedBefore=view.m_workingTerrainAtlas->activeTileImages();
        QFile atlasFile(fixture+"/res/HDPack_Terrain.res");check(atlasFile.open(QIODevice::ReadOnly),"read original atlas");CResArchiveDocument originalArchive;check(originalArchive.loadBytes(atlasFile.readAll()),"original atlas parsed");atlasFile.close();
        auto painted=stroke(0);const auto paintedImages=view.m_workingTerrainAtlas->activeTileImages();
        check(view.saveWorldTerrainMaps(),"Save painted private map");
        check(sectors()==painted,"Save preserves painting");
        const auto savedImages=view.m_workingTerrainAtlas->activeTileImages();int untouched=0;bool pixelsPreserved=true;
        for(int i=0;i<untouchedBefore.size();++i)if(untouchedBefore[i]==paintedImages[i]){++untouched;if(savedImages[i]!=untouchedBefore[i])pixelsPreserved=false;}
        check(pixelsPreserved&&untouched>0,"unmodified tiles including water and transitions exact");
        check(atlasFile.open(QIODevice::ReadOnly),"read saved atlas");CResArchiveDocument savedArchive;check(savedArchive.loadBytes(atlasFile.readAll()),"saved atlas parsed");atlasFile.close();
        bool headers=true;for(const auto& m:originalArchive.members())if(m.name.endsWith(".mmp",Qt::CaseInsensitive))headers&=m.payload.left(76)==savedArchive.memberPayload(m.name).left(76);check(headers,"all authored page headers and sizes preserved");check(!(view.nextWorldTerrainStroke(false)!=nullptr),"world Save explicit history checkpoint");
        view.m_worldTerrainSession.clear();view.unloadLand(false);view.makeCurrent();view.loadLandscape(map,false);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"reopened painted catalog");check(sectors()==painted,"reopened painting exact");setupWorld();
        stroke(1);check(view.saveWorldTerrainMaps(),"second world paint Save");
        const QDir rescue(qEnvironmentVariable("EI_SAVE_RESCUE"));check(rescue.exists(),"explicit rescue directory");
        const auto rescueMaps=rescue.entryInfoList({"*.mpr"},QDir::Files);const auto rescueMobs=rescue.entryInfoList({"*.mob"},QDir::Files);
        check(rescueMaps.size()==1&&rescueMobs.size()==1,"one pinned rescue generation");
        QString recoveryError;check(CBackupManager::validateAutosave(rescueMaps[0],&recoveryError),"pinned MPR and atlas recovery hashes");check(CBackupManager::validateAutosave(rescueMobs[0],&recoveryError),"pinned MOB recovery hash");
        CMobDocument rescuedMob;CValidationReport mobReport;check(rescuedMob.load(rescueMobs[0],&mobReport),"rescued MOB with patrols parses");
        QFile mobFile(rescueMobs[0].absoluteFilePath());check(mobFile.open(QIODevice::ReadOnly)&&rescuedMob.serialize()==mobFile.readAll(),"rescued MOB byte-exact roundtrip");
        view.m_worldTerrainSession.clear();view.unloadLand(false);view.makeCurrent();view.loadLandscape(rescueMaps[0],false);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"pinned recovery opened with paired local atlas");check(sectors()==recoveryTerrain,"pinned recovery terrain exact");check(view.m_workingTerrainAtlas->activeTileImages()==untouchedBefore,"pinned recovery atlas exact");
        out<<"PASS world save "<<passes<<" checks\n";return 0;
    }catch(const QString& error){out<<"FAIL world save "<<error<<"\n";return 1;}
}

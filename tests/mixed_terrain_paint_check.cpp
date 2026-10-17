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
        view.attach(&settings,&table,&undo,&progress,&coords,&tree,&tiles);view.resize(800,600);view.show();QCoreApplication::processEvents();
        check(view.isValid(),"hidden native GL");CLandscapeTools panel;view.attachLandscapeTools(&panel);
        const QFileInfo map(fixture+"/Maps/zone3obr.mpr");
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)){
            out<<"DIALOG "<<box->text()<<"\n";out.flush();
            if(box->windowTitle()=="Replace shared terrain atlas pages?"&&QFileInfo(view.m_activeTerrainArchivePath).absoluteFilePath()==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath())box->button(QMessageBox::Yes)->click();else box->reject();
        }});dialogs.start(50);
        view.loadLandscape(map,false);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"recovery map catalog");view.m_worldTerrainEnabled=false;
        { const auto initial=view.m_workingTerrainAtlas->activeTileImages();check(initial.front().width()==128&&terrainMaximumTileSize(initial)==256,"owner mixed128/256 fixture"); }
        const auto sectorOrder=view.m_mprDocument.sectors(); const auto sectors=[&](){QByteArray bytes;for(const auto& s:sectorOrder)bytes+=view.m_terrainEdit.sectorPayload(s.sectorX,s.sectorY);return bytes;};
        auto stroke=[&](int attempt){ view.m_worldTerrainEnabled=false;
            panel.activateMode(ELandscapeEditMode::TilePaint,true);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
            check(panel.selectPaintTerrainType(attempt==0?2013464699:2142727678),"select reported material");
            if(auto* button=panel.findChild<QToolButton*>("vertexPaintTarget"))button->click();
            if(auto* toggle=panel.findChild<QCheckBox*>("modernAutoResolveBlends"))toggle->setChecked(true);
            if(auto* radius=panel.findChild<QSlider*>("terrainPaintRadius"))radius->setValue(1);
            QElapsedTimer warm;warm.start();while(warm.elapsed()<4000){QCoreApplication::processEvents();QTest::qWait(10);}
            const auto before=sectors();const int count=undo.count();
            QVector3D world(47,75,0);view.m_pLand->projectPt(world,false);view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();QTest::qWait(100);
            const auto projected=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));
            const QPoint point(qRound(projected.x()),qRound(view.height()-projected.y()));check(view.rect().contains(point),"paint projects into viewport");
            QMouseEvent hover(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&hover);QCoreApplication::processEvents();
            QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,point);
            QElapsedTimer done;done.start();while(done.elapsed()<8000){QCoreApplication::processEvents();QTest::qWait(10);if(!view.m_tileStrokeFinishing&&undo.count()>count)break;}
            const auto after=sectors();check(after!=before&&undo.count()==count+1,"actual vertex paint changes terrain with one Undo");
            undo.undo();check(sectors()==before,"paint Undo exact");undo.redo();check(sectors()==after,"paint Redo exact");return after;
        };
        auto painted=stroke(0);
        check(view.saveChangedLandscapeDocument(map,true),"Save painted private map");
        check(sectors()==painted,"Save preserves painting");undo.undo();undo.redo();check(sectors()==painted,"UndoRedo after Save");
        view.unloadLand(false);view.makeCurrent();view.loadLandscape(map,false);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"reopened painted catalog");check(sectors()==painted,"reopened painting exact");
        stroke(1);check(view.saveChangedLandscapeDocument(map,true),"second paint Save");
        out<<"PASS mixed paint "<<passes<<" checks\n";return 0;
    }catch(const QString& error){out<<"FAIL mixed paint "<<error<<"\n";return 1;}
}

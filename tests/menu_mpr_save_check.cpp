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
#include "endurance_document.h"
namespace {
class MenuCheckHidden : public QObject { bool eventFilter(QObject* o,QEvent* e) override {if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);return false;} };
}
int runAssemblyCatalogCheck(){
 QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
 auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passes;};
 try {
 QTemporaryDir scratch;QDir::setCurrent(scratch.path());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
 MenuCheckHidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
 QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
 MainWindow window;setup.stop();window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.m_currentCollectionPath=scratch.filePath("test.collection");
 const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MIXED_FIXTURE")),game=qEnvironmentVariable("EI_MIXED_GAME_ROOT");check(!fixture.isEmpty()&&!game.isEmpty(),"explicit private fixture");
 auto& settings=*window.m_settings;
 dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(game);
 dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({fixture+"/res",game+"/MODS/DejaVu/res",game+"/Base/Res"});
 dynamic_cast<COptString*>(settings.opt(eOptSetResource,"terrainAuthoringArchive"))->setValue(qEnvironmentVariable("EI_MIXED_REFERENCE"));CTextureList::getInstance()->attachSettings(&settings);
 auto& view=*window.view();view.setMouseTracking(true);window.resize(1200,850);window.show();view.show();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"real MainWindow hidden GL");auto& panel=*window.m_landscapeTools;
 QObject::connect(&view,&CView::terrainPublicationPreflightRequested,[&](const QString& path,bool* accepted){*accepted=QFileInfo(path).absoluteFilePath()==QFileInfo(fixture+"/res/HDPack_Terrain.res").absoluteFilePath();});
 int prompts=0,badDialogs=0;bool cancelNext=false;QString lastPrompt;
 QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){
  out<<"DIALOG "<<box->text()<<"\n";out.flush();
  if(auto* save=box->button(QMessageBox::Save)){if(!box->text().contains(fixture)){++badDialogs;box->reject();continue;}++prompts;lastPrompt=box->text();if(cancelNext){cancelNext=false;box->button(QMessageBox::Cancel)->click();}else save->click();}
  else {++badDialogs;box->reject();}
 }});dialogs.start(50);
 auto id=[&](const QString& name){return CWorldTerrainSession::key(fixture+"/Maps/"+name+".mpr");};
 auto payload=[](const CMprDocument& map,const CTerrainEditModel& model){QMap<QPair<int,int>,QByteArray> sorted;for(const auto& s:map.sectors())sorted[{s.sectorX,s.sectorY}]=model.sectorPayload(s.sectorX,s.sectorY);QByteArray bytes;for(const auto& b:sorted)bytes+=b;return bytes;};
 auto current=[&](const QString& name){auto d=view.m_worldTerrainSession.documents.value(id(name));return d?payload(d->baseline,d->terrain):QByteArray();};
 auto disk=[&](const QString& name){CMprDocument map;CTerrainEditModel model;CValidationReport report;check(map.load(QFileInfo(fixture+"/Maps/"+name+".mpr"),&report)&&model.load(map,&report),"read saved "+name);auto binding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({fixture+"/res/HDPack_Terrain.res"},map.mapInfo().innerNamespace,fixture+"/res/HDPack_Terrain.res");check(binding.success(),"saved atlas binds "+name);return payload(map,model);};
 auto open=[&](const QString& name){if(view.m_pLand->isMprLoad()){view.syncWorldTerrainActive();view.unloadLand(false);}view.makeCurrent();view.loadLandscape(QFileInfo(fixture+"/Maps/"+name+".mpr"),false);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"catalog "+name);view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();QString error;CValidationReport report;check(view.m_worldTerrainSession.load(fixture+"/Maps/"+name+".mpr",&error),"world document "+name);view.syncWorldTerrainActive();check(view.ensureWorldTerrainAtlas(id(name),&report),"world atlas "+name);QCoreApplication::processEvents();};
 auto stroke=[&](int material){panel.activateMode(ELandscapeEditMode::TilePaint,true);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);check(panel.selectPaintTerrainType(material),"select material");if(auto* b=panel.findChild<QToolButton*>("vertexPaintTarget"))b->click();if(auto* b=panel.findChild<QCheckBox*>("modernAutoResolveBlends"))b->setChecked(true);if(auto* b=panel.findChild<QSlider*>("terrainPaintRadius"))b->setValue(1);QTest::qWait(1000);
  const auto before=payload(view.m_mprDocument,view.m_terrainEdit);QVector3D world(47,75,0);view.m_pLand->projectPt(world,false);view.moveCameraToPoint(world);view.m_cam->moveAwayOn(35);view.update();QTest::qWait(100);
  const auto p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));const QPoint point(qRound(p.x()),qRound(view.height()-p.y()));check(view.rect().contains(point),"stroke inside viewport");QMouseEvent hover(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&hover);QCoreApplication::processEvents();QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,point);QElapsedTimer wait;wait.start();while(wait.elapsed()<8000){QTest::qWait(10);if(!view.m_tileStrokeFinishing&&payload(view.m_mprDocument,view.m_terrainEdit)!=before)break;}
  view.syncWorldTerrainActive();check(payload(view.m_mprDocument,view.m_terrainEdit)!=before,"actual paint changed active map");};
 auto* action=window.findChild<QAction*>("actionSave_landscape_MPR");auto* menu=window.findChild<QMenu*>("menuFile");check(action&&menu&&menu->actions().contains(action),"production File Save MPR action");QSignalSpy triggered(action,SIGNAL(triggered(bool)));
 auto saveMenu=[&](int expectedMaps,bool cancel=false){check(action->isEnabled(),"menu action enabled");const int count=triggered.count(),beforePrompts=prompts;cancelNext=cancel;
  QTest::mouseClick(window.menuBar(),Qt::LeftButton,Qt::NoModifier,window.menuBar()->actionGeometry(menu->menuAction()).center());QCoreApplication::processEvents();check(menu->isVisible(),"File menu opened by mouse");QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());QCoreApplication::processEvents();
  check(triggered.count()==count+1,"Save MPR menu action triggered once");check(badDialogs==0&&prompts==beforePrompts+1,"one shared Save dialog without fallback refusal");check(lastPrompt.contains(fixture+"/Maps/zone3obr.mpr")+(lastPrompt.contains(fixture+"/Maps/zone9.mpr"))==expectedMaps,"dialog contains all and only dirty maps");};
 open("zone3obr");const auto a0=current("zone3obr");stroke(2013464699);const auto a1=current("zone3obr");check(view.m_worldTerrainSession.dirtyPaths().size()==1,"one active dirty map");
 saveMenu(1,true);check(current("zone3obr")==a1&&view.m_worldTerrainSession.canUndo(),"Cancel preserves data and Undo");saveMenu(1);check(view.m_worldTerrainSession.dirtyPaths().isEmpty()&&disk("zone3obr")==a1,"single menu save commits map and atlas");
 view.worldTerrainHistory(false,true);check(current("zone3obr")==a0,"Undo after menu Save");view.worldTerrainHistory(true,true);check(current("zone3obr")==a1&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"Redo after menu Save clean");
 stroke(2142727678);const auto a2=current("zone3obr");open("zone9");const auto b0=current("zone9");stroke(2013464699);const auto b1=current("zone9");check(view.m_worldTerrainSession.dirtyPaths().size()==2,"two dirty map documents");const QString companionPath=fixture+"/companion.bin";{QFile f(companionPath);check(f.open(QIODevice::WriteOnly)&&f.write("before")==6,"private companion baseline");}
 int accepted=0;SMapSaveFile companion;companion.path=companionPath;companion.before="before";companion.after="after";view.m_mapCohortActive=true;view.m_mapCohortCommitted=false;view.m_mapCompanions={companion};view.m_mapCompanionsCheckpoint=[&](){++accepted;};
 saveMenu(2);check(view.m_mapCohortCommitted&&accepted==1,"shared menu keeps successful companion checkpoint");{QFile f(companionPath);check(f.open(QIODevice::ReadOnly)&&f.readAll()=="after","companion saved in same batch");}view.m_mapCohortActive=false;view.m_mapCompanions.clear();view.m_mapCompanionsCheckpoint={};
 check(disk("zone3obr")==a2&&disk("zone9")==b1&&view.m_worldTerrainSession.dirtyPaths().isEmpty(),"menu commits both MPRs and both atlas bindings");
 view.worldTerrainHistory(false,true);check(current("zone9")==b0&&current("zone3obr")==a2,"two-map Undo after Save");open("zone1");check(!view.m_pLand->isDirty()&&view.m_worldTerrainSession.dirtyPaths().size()==1,"active clean with another dirty map");saveMenu(1);check(disk("zone9")==b0&&current("zone3obr")==a2,"clean-active menu saves dirty neighbor");view.worldTerrainHistory(true,true);check(current("zone9")==b1,"Redo survives saving neighbor Undo");saveMenu(1);check(disk("zone9")==b1,"resave neighbor Redo");
 view.m_worldTerrainSession.clear();open("zone3obr");check(current("zone3obr")==a2,"fresh reopen zone3obr exact");open("zone9");check(current("zone9")==b1,"fresh reopen zone9 exact");view.m_worldTerrainSession.clear();view.unloadLand(false);window.m_minimapRefreshTimer->stop();qApp->removeEventFilter(&hidden);
 out<<"PASS menu save "<<passes<<" checks\n";return 0;
 }catch(const QString& error){out<<"FAIL menu save "<<error<<"\n";return 1;}
}

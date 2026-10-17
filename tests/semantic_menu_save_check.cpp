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
#include "res_archive_document.h"
#include "terrain_semantic_field_store.h"
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
 CResArchiveDocument originalArchive;
 // Seed the same stale external semantic member under the private map path.
 {QFile f(fixture+"/res/HDPack_Terrain.res");check(f.open(QIODevice::ReadOnly),"read private RES");CResArchiveDocument archive;check(archive.loadBytes(f.readAll()),"parse private RES");originalArchive=archive;f.close();SResArchiveNewMember old;old.name=STerrainSemanticFieldDocument::memberName("zone2",QFileInfo(fixture+"/Maps/zone2.mpr"));old.payload=archive.memberPayload("ei_terrain_semantics_zone2_4ec9cef40f80630c.json");check(!old.payload.isEmpty(),"owner stale semantic member exists");QByteArray output;check(archive.buildWithPublishedMembers({old},true,&output),"seed private legacy semantic key");check(f.open(QIODevice::WriteOnly)&&f.write(output)==output.size(),"write private semantic fixture");}
 auto stroke=[&](int seed){
  const QString active=CWorldTerrainSession::key(view.m_pLand->filePath().absoluteFilePath());view.syncWorldTerrainActive();auto d=view.m_worldTerrainSession.documents[active];
  SWorldTerrainPaintChange change;change.before={d->terrain,d->atlas->createStagingCopy(),d->materials};change.after=change.before;change.after.atlas=d->atlas->createStagingCopy();
  QImage tile=change.after.atlas->activeTileImages().first().convertToFormat(QImage::Format_RGBA8888);tile.setPixelColor(seed%tile.width(),seed%tile.height(),QColor(seed%200+20,33,71));CValidationReport report;
  const int slot=change.after.atlas->importPhysicalTile(tile,0,nullptr,"private-regression",seed,&report,true);check(slot>=0,"physical artwork import without semantic painting");
  STerrainTileCommand cmd;STerrainTileDelta delta;delta.globalX=4+seed%3;delta.globalY=5;delta.before=change.after.terrain.packedTile(delta.globalX,delta.globalY,delta.layer);SMprPackedTile packed;packed.atlasIndex=slot/64;packed.tileIndex=slot%64;delta.after=packed.encode();cmd.deltas.append(delta);check(change.after.terrain.apply(cmd,true,&report),"physical tile edit");
  SWorldTerrainStroke command;command.paint[active]=change;QString error;check(view.m_worldTerrainSession.apply(command,true,&error),"apply complete stroke");view.m_worldTerrainSession.record(command);check(view.previewWorldPaintState(active),"preview physical edit");view.updateWorldTerrainStatus();view.syncMprDirtyState();
 };
 auto* action=window.findChild<QAction*>("actionSave_landscape_MPR");auto* menu=window.findChild<QMenu*>("menuFile");check(action&&menu&&menu->actions().contains(action),"production File Save MPR action");QSignalSpy triggered(action,SIGNAL(triggered(bool)));
 auto saveMenu=[&](int expectedMaps,bool cancel=false){check(action->isEnabled(),"menu action enabled");const int count=triggered.count(),beforePrompts=prompts;cancelNext=cancel;
  QTest::mouseClick(window.menuBar(),Qt::LeftButton,Qt::NoModifier,window.menuBar()->actionGeometry(menu->menuAction()).center());QCoreApplication::processEvents();check(menu->isVisible(),"File menu opened by mouse");QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());QCoreApplication::processEvents();
  check(triggered.count()==count+1,"Save MPR menu action triggered once");check(badDialogs==0&&prompts==beforePrompts+1,"one shared Save dialog without fallback refusal");check(lastPrompt.contains(fixture+"/Maps/zone1.mpr")+(lastPrompt.contains(fixture+"/Maps/zone2.mpr"))==expectedMaps,"dialog contains all and only dirty maps");};
 open("zone1");QVector<qint32> zone1Land,zone1Overlay,ignored;view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&zone1Land,&ignored);view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Overlay,&zone1Overlay,&ignored);check(!zone1Land.isEmpty(),"owner zone1 has existing material field");const auto a0=current("zone1");stroke(3);const auto a1=current("zone1");saveMenu(1,true);check(current("zone1")==a1&&view.m_worldTerrainSession.canUndo(),"Cancel retains state/history");
 open("zone2");const auto b0=current("zone2");QVector<qint32> field,baseline;view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&field,&baseline);check(field.isEmpty(),"owner zone2 empty semantic field");stroke(8);const auto b1=current("zone2");check(view.m_worldTerrainSession.dirtyPaths().size()==2,"zone1 and zone2 both dirty");
 {CValidationReport report;auto task=view.prepareLandscapeRecoveryTask(&report);check(bool(task),"live recovery task available");auto recovery=task();check(!recovery.mprBytes.isEmpty()&&!recovery.atlasBytes.isEmpty()&&!recovery.report.hasErrors(),"recovery stages paired MPR and atlas");CMprDocument m;check(m.loadBytes(recovery.mprBytes,"private-recovery.mpr",&report),"recovery MPR reopens");STerrainSemanticFieldDocument semantics;check(STerrainSemanticFieldDocument::fromBytes(m.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&semantics,&report)&&semantics.land.isEmpty()&&semantics.overlay.isEmpty(),"recovery embeds authoritative empty fields");}
 saveMenu(2);check(view.m_worldTerrainSession.dirtyPaths().isEmpty(),"both saved clean");check(disk("zone1")==a1&&disk("zone2")==b1,"disk sector payloads exact");
 {CMprDocument m;CValidationReport report;check(m.load(QFileInfo(fixture+"/Maps/zone2.mpr"),&report),"zone2 saved MPR parsed");STerrainSemanticFieldDocument semantics;check(STerrainSemanticFieldDocument::fromBytes(m.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&semantics,&report),"explicit embedded empty semantics exists");check(semantics.land.isEmpty()&&semantics.overlay.isEmpty(),"empty semantics stays empty");}
 view.worldTerrainHistory(false,true);check(current("zone2")==b0&&current("zone1")==a1,"Undo after multi-map Save exact");saveMenu(1);check(view.m_worldTerrainSession.canRedo()&&disk("zone2")==b0,"Save Undo preserves Redo");view.worldTerrainHistory(true,true);check(current("zone2")==b1,"Redo after saving Undo exact");saveMenu(1);check(disk("zone2")==b1,"resave Redo exact");
 view.worldTerrainHistory(false,true);view.worldTerrainHistory(false,true);check(current("zone1")==a0&&current("zone2")==b0,"both Undo states exact");view.worldTerrainHistory(true,true);view.worldTerrainHistory(true,true);check(current("zone1")==a1&&current("zone2")==b1,"both Redo states exact");
 view.m_worldTerrainSession.clear();open("zone1");check(current("zone1")==a1,"fresh reopen zone1 exact");QVector<qint32> reopenedLand,reopenedOverlay;view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&reopenedLand,&ignored);view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Overlay,&reopenedOverlay,&ignored);check(reopenedLand==zone1Land&&reopenedOverlay==zone1Overlay,"zone1 nonempty land and overlay semantics exact");open("zone2");check(current("zone2")==b1,"fresh reopen zone2 exact");view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&field,&baseline);check(field.isEmpty(),"fresh zone2 cannot resurrect old semantic field");{QFile f(fixture+"/res/HDPack_Terrain.res");check(f.open(QIODevice::ReadOnly),"read final atlas");CResArchiveDocument archive;check(archive.loadBytes(f.readAll()),"parse final atlas");int count=0;for(const auto& member:originalArchive.members())if(member.name.endsWith(".mmp",Qt::CaseInsensitive)){check(archive.memberPayload(member.name)==member.payload,"unchanged authored MMP "+member.name);++count;}check(count>0,"authored water transitions and all original artwork byte exact");}
 view.m_worldTerrainSession.clear();view.unloadLand(false);window.m_minimapRefreshTimer->stop();qApp->removeEventFilter(&hidden);
 out<<"PASS menu save "<<passes<<" checks\n";return 0;
 }catch(const QString& error){out<<"FAIL menu save "<<error<<"\n";return 1;}
}

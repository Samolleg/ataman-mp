// Hidden integration harness. Reuse the isolated paths, readback and window
// guards of stage3, then exercise only the neighbour ingress/lifetime contract.
#define main stage3UnusedMain
#include "rebuild64_stage3_check.cpp"
#undef main
#include "neighbor_map_layout.h"
#include "world_map_layout.h"
#include "world_map_layout_document.h"

// Enumerate only this hidden test process's disk handles. No owner process is
// opened. This establishes retirement independently of frame/epoch counters.
QStringList ownResourceHandles() {
 struct Entry {HANDLE handle;ULONG_PTR handleCount,pointerCount;ULONG access,type,attributes,reserved;};
 struct Snapshot {ULONG_PTR count,reserved;Entry entries[1];};
 using Query=LONG (WINAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*);
 auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));
 if(!query)throw QString("own handle query unavailable");QByteArray bytes(4*1024*1024,0);ULONG used=0;
 if(query(GetCurrentProcess(),51,bytes.data(),ULONG(bytes.size()),&used)<0)throw QString("own handle snapshot unavailable");
 auto* snapshot=reinterpret_cast<Snapshot*>(bytes.data());QStringList paths;
 using FinalPath=DWORD (WINAPI*)(HANDLE,LPWSTR,DWORD,DWORD);
 auto finalPath=reinterpret_cast<FinalPath>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetFinalPathNameByHandleW"));if(!finalPath)throw QString("own disk path query unavailable");
 for(ULONG_PTR i=0;i<snapshot->count;++i){const auto handle=snapshot->entries[i].handle;if(GetFileType(handle)!=FILE_TYPE_DISK)continue;wchar_t path[32768];const DWORD n=finalPath(handle,path,32768,0);if(!n||n>=32768)continue;const auto file=QString::fromWCharArray(path,int(n)).toLower();if(file.endsWith(".mpr")||file.endsWith(".mob")||file.endsWith(".res"))paths.append(file);}
 paths.sort();return paths;
}

int main(int argc,char** argv) {
 if(qEnvironmentVariableIsEmpty("EI_EMBED_CASE")||qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))return 2;
 isolate();QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
 QCoreApplication::setOrganizationName("Ataman isolated neighbours285");QCoreApplication::setApplicationName("neighbours285");
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR"));
 app.setProperty("workspaceLoadSmoke",true);Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_CASE"));
 const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_FIXTURE"));
 try {
  check(root.contains("/work/rebuild64-neighbours-285-")&&fixture.contains("/work/rebuild64-neighbours-285-"),"writable paths owned");
  QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[]{EnumWindows(inspect,0);for(QWidget* w:QApplication::topLevelWidgets()){if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();if(auto* b=qobject_cast<QMessageBox*>(w))b->reject();}});dialogs.start(5);
  MainWindow window;window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
  SResourceProfileEvaluation profile;for(const auto& p:builtInResourceProfiles())if(p.id=="deja-vu")profile=evaluateResourceProfile("C:/GOG Games/PZ",p);
  check(profile.valid,"DejaVu read-only profile");window.m_settings->applyResourceProfile(profile);
  dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"mapRoots"))->setValue({fixture+"/Maps"});auto* view=window.view();view->refreshAfterResourceSetup();
  // Native neighbours read their saved layout and files, as in the game. GL
  // control intentionally uses the same active fixture as the previous R3 run.
  window.m_mapCatalog=CMapCatalog::load({"C:/GOG Games/PZ/MODS/Dejavu/Res","C:/GOG Games/PZ/Base/Res"},{fixture+"/Maps"});
  dynamic_cast<COptBool*>(window.m_settings->opt("overrideWorldTime"))->setValue(true);dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(1240);
  if(auto* units=dynamic_cast<COptBool*>(window.m_settings->opt("drawUnits")))units->setValue(false);
  auto* embedded=window.findChild<CRebuild64Viewport*>("rebuild64Viewport");check(embedded,"production wrapper");check(!embedded->backendLoaded(),"GL lazy DLL");
  quint64 frames=0;QObject::connect(embedded,&CRebuild64Viewport::frameCommitted,[&](quint64){++frames;});
  QObject::connect(embedded,&CRebuild64Viewport::backendFailure,[&](const QString& s){out<<"BACKEND_FAILURE "<<s<<'\n';out.flush();});
  window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1700,1100);window.show();QApplication::processEvents();
  auto open=[&](const QString& name){QString zone;
   for(const auto& entry:window.m_mapCatalog.entries)if((entry.mprName==name||QFileInfo(entry.mprName).completeBaseName()==name)&&!entry.isBriefing()){zone=entry.zoneId;break;}
   if(name=="zone1")zone="gz1g";
   view->deferNeighborLoading(true); // only suppress the unrelated GL background loader
   window.openMapPair(fixture+"/Maps/"+name+".mpr",fixture+"/Maps/"+name+".mob",zone);
   check(wait([&]{return !view->hasPendingOperation()&&!window.backgroundMobLoading()&&view->hasActiveMob();},120000),"open "+name);
   window.m_minimapRefreshTimer->stop();view->m_freezeAnimations=true;view->m_animationFreezeTime=0;view->m_overlayAnimationTimer.invalidate();view->m_tileGridVisible=false;view->m_bPreviewTile=false;view->m_particleVisualsVisible=false;
   view->deferNeighborLoading(true);
   for(auto* mob:view->m_aMob)for(auto* node:mob->nodes())if(dynamic_cast<CUnit*>(node))node->setState(ENodeState::eHidden);
   view->setFixedSize(1280,720);embedded->setFixedSize(1280,720);QApplication::processEvents();
   check(view->ensureTerrainAutoCatalogReady()&&view->m_terrainEditAvailable,"memory terrain "+name);
  };
  auto camera=[&](QVector3D eye,QVector3D target){QMatrix4x4 matrix;matrix.lookAt(eye,target,{0,0,1});view->m_cam->setPreviewView(matrix);view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1280.f/720.f,1,2000);view->m_projection=view->m_normalProjection;};
  auto ready=[&]{return embedded->lastFailure().isEmpty()&&embedded->statistics().ready_generation==embedded->m_generation&&embedded->statistics().ready_generation>0&&embedded->m_stack->currentWidget()!=view;};
  auto capture=[&](QString label){QString path=root+"/"+label+".png";QFile::remove(path);embedded->capture(path);check(wait([&]{return !QImage(path).isNull();}),"native readback "+label);};
  auto sample=[&](QString label,bool expected){
   const quint64 startFrames=frames;QVector<double> samples;quint64 sampled=frames;QElapsedTimer clock;clock.start();qint64 first=-1,neighbours=-1;int mainAccepted=-1;
   int pulses=0;QTimer pulse;QObject::connect(&pulse,&QTimer::timeout,[&]{++pulses;});pulse.start(5);
   embedded->selectRebuild(true,false);embedded->m_previewClock.invalidate();
   check(wait([&]{if(!embedded->lastFailure().isEmpty())return true;const auto stats=embedded->statistics();if(frames!=sampled){sampled=frames;if(ready()){if(first<0){first=clock.elapsed();mainAccepted=int(stats.neighbours_accepted);}samples.append(stats.frame_ms);if(neighbours<0&&!stats.neighbours_pending)neighbours=clock.elapsed();}}return first>=0&&neighbours>=0&&frames>startFrames+60;},180000)&&embedded->lastFailure().isEmpty(),"main and native stream settled "+label);
   const auto stats=embedded->statistics();check(expected?stats.neighbours_accepted>0:stats.neighbours_accepted==0,"accepted neighbour membership "+label);
   check(pulses>20,"UI event loop stays responsive "+label);
   check(QDir(embedded->m_guardDirectory).entryList({"startup-*.json"},QDir::Files).isEmpty(),"crash marker cleared only after stream settled "+label);
   std::sort(samples.begin(),samples.end());out<<"MEASURE "<<label<<" main_ms="<<first<<" neighbours_ms="<<neighbours<<" main_accepted="<<mainAccepted<<" accepted="<<stats.neighbours_accepted<<" n="<<samples.size()<<" frame_median_ms="<<samples[samples.size()/2]<<" frame_p95_ms="<<samples[qMin(samples.size()-1,int(samples.size()*.95))]<<" private_bytes="<<stats.private_bytes<<" video_bytes="<<stats.video_bytes<<" ui_pulses="<<pulses<<'\n';out.flush();capture(label);
   if(app.arguments().contains("--steady")){
    QVector<double> stable;quint64 seen=frames;const auto begin=frames;
    check(wait([&]{if(frames!=seen){seen=frames;stable.append(embedded->statistics().frame_ms);}return frames>begin+60;}),"resident frame sample "+label);
    std::sort(stable.begin(),stable.end());const auto s=embedded->statistics();out<<"STEADY "<<label<<" n="<<stable.size()<<" median_ms="<<stable[stable.size()/2]<<" p95_ms="<<stable[qMin(stable.size()-1,int(stable.size()*.95))]<<" private_bytes="<<s.private_bytes<<" video_bytes="<<s.video_bytes<<'\n';out.flush();
   }
  };
  if(app.arguments().contains("--layout")){
   CWorldMapLayoutDocument document;document.setProfileDirectory(profile.modRoot,profile.profileId,QString());
   const auto stored=document.loadEditorWorld(profile.profileId,{profile.modRoot+"/Maps"});check(!stored.storageBlocked,"saved workspace read only");
   QVector<SMapCatalogEntry> topology;
   for(const auto& map:stored.zones){SMapCatalogEntry e;e.allodId=map.allodId;e.zoneId=map.zoneId;e.mprName=map.mprIdentity;e.mprPath=document.absoluteResourcePath(map.mprPath);e.mobPath=document.absoluteResourcePath(map.mobPath);e.sectorWidth=map.sectorWidth;e.sectorHeight=map.sectorHeight;topology.append(e);}
   for(const auto& active:stored.zones)if(QStringList{"zone1","zone7"}.contains(active.mprIdentity)){
    const auto layout=CNeighborMapLayout::build(topology,stored.zones,profile.profileId,active.allodId,active.zoneId);
    check(layout.hasActiveWorldOrigin,"saved mapper layout "+active.mprIdentity);
    for(const auto& p:layout.placements)out<<"MAPPER_PLACEMENT active="<<active.mprIdentity<<" zone="<<p.zoneId<<" mpr="<<QFileInfo(p.mprPath).completeBaseName()<<" x="<<p.origin.x()<<" y="<<p.origin.y()<<" width="<<p.size.width()<<" height="<<p.size.height()<<" touches="<<CNeighborMapLayout::touchesActiveMap({active.sectorWidth*32.,active.sectorHeight*32.},p)<<'\n';
   }
   out.flush();EnumWindows(inspect,0);check(!visible,"zero visible HWNDs");window.close();return 0;
  }
  open("zone1");camera({240,110,175},{240,171,10});
  if(app.arguments().contains("--lifetime")){
   const auto beforeHandles=ownResourceHandles();out<<"HANDLES_BEFORE "<<beforeHandles.join('|')<<'\n';
   view->m_neighborLandscapesVisible=true;camera({176,-180,300},{260,300,5});
   bool sawAsyncMarker=false;embedded->selectRebuild(true,false);embedded->m_previewClock.invalidate();
   check(wait([&]{const auto s=embedded->statistics();if(s.neighbours_pending&&ready())sawAsyncMarker|=!QDir(embedded->m_guardDirectory).entryList({"startup-*.json"},QDir::Files).isEmpty();return ready()&&!s.neighbours_pending&&s.neighbours_accepted>0;},180000),"lifetime stream prepared");
   check(sawAsyncMarker,"crash marker covers neighbours after main frame");capture("unsaved-before");const auto generation=embedded->m_generation;
   STerrainBrush brush;brush.centerX=240;brush.centerY=176;brush.radius=20;
   const auto change=view->m_terrainEdit.makeRaiseLower(brush,400,true);check(view->applyTerrainHeightPreview(change,true),"unsaved active terrain mutation");
   check(wait([&]{return embedded->m_generation>generation&&ready()&&!embedded->statistics().neighbours_pending;},180000),"unsaved snapshot reaches renderer and weld stream");capture("unsaved-after");
   check(QImage(root+"/unsaved-before.png")!=QImage(root+"/unsaved-after.png"),"unsaved active edit changes own readback");
   check(view->applyTerrainHeightPreview(change,false),"restore active memory edit");view->m_pLand->setDirty(false);
   embedded->selectRebuild(false,false);view->unloadLand(false);QTest::qWait(200);
   const auto retired=wait([&]{auto extra=ownResourceHandles();for(const auto& p:beforeHandles)extra.removeOne(p);return extra.isEmpty();},60000);out<<"HANDLES_OBSERVED_AFTER "<<ownResourceHandles().join('|')<<'\n';out.flush();check(retired,"all stream resource handles retired after close");
   out<<"HANDLES_AFTER "<<ownResourceHandles().join('|')<<" private_bytes="<<privateBytes()<<" video_bytes="<<videoBytes()<<'\n';
  }
  else if(app.arguments().contains("--gl")){captureGL(view,"gl-zone1-village-1240");check(!embedded->backendLoaded(),"GL does not load DLL");}
  else {
   view->m_neighborLandscapesVisible=false;camera({176,-180,300},{260,300,5});sample("zone1-far-off",false);
   embedded->selectRebuild(false,false);view->m_neighborLandscapesVisible=true;sample("zone1-far-on",true);
   if(app.arguments().contains("--steady")){embedded->selectRebuild(false,false);view->unloadLand(false);QTest::qWait(200);EnumWindows(inspect,0);check(!visible,"zero visible HWNDs");window.close();return 0;}
   camera({320,180,70},{480,180,5});sample("zone1-boundary-on",true);
   // Live checkbox: don't switch the backend while toggling it.
   view->m_neighborLandscapesVisible=false;check(wait([&]{return embedded->statistics().neighbours_accepted==0&&!embedded->statistics().neighbours_pending;}),"live human toggle off");capture("zone1-boundary-off");
   view->m_neighborLandscapesVisible=true;check(wait([&]{return embedded->statistics().neighbours_accepted>0&&!embedded->statistics().neighbours_pending;},180000),"live human toggle on");
   open("zone7");camera({120,-160,240},{100,180,8});sample("zone7-far-on",true);
   open("zone1");camera({176,-180,300},{260,300,5});sample("zone1-return-on",true);
   embedded->selectRebuild(false,false);QTest::qWait(100);check(view==embedded->m_stack->currentWidget(),"Rebuild64 to GL");
   sample("zone1-reselect-on",true);
   embedded->selectRebuild(false,false);view->unloadLand(false);QTest::qWait(50);open("zone12dun2");camera({60,35,110},{60,90,8});sample("cave-on",false);
   view->unloadLand(false);check(wait([&]{return embedded->m_stack->currentWidget()==view;}),"closed map returns to GL surface");QTest::qWait(100);
   open("zone1");camera({176,-180,300},{260,300,5});sample("zone1-reopen-on",true);
   // Close while work may be in flight, then return to OpenGL. A responsive
   // caller does not wait for the native stop/join on the UI thread.
   view->m_neighborLandscapesVisible=false;QTest::qWait(30);view->m_neighborLandscapesVisible=true;QTest::qWait(30);QElapsedTimer close;close.start();view->unloadLand(false);embedded->selectRebuild(false,false);check(close.elapsed()<1000,"close/toggle caller remains nonblocking");
  }
  embedded->selectRebuild(false,false);QTest::qWait(100);EnumWindows(inspect,0);check(!visible,"zero visible HWNDs");window.close();out<<"PASS neighbours285 checks="<<passes<<'\n';out.flush();return 0;
 } catch(const QString& error){out<<"FAIL "<<error<<'\n';out.flush();return 1;}
}

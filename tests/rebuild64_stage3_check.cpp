// Real document/view/ABI scenarios; all writable paths and HWNDs belong to this process.
#include <QtWidgets>
#include <QtTest>
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <cstring>
#include <memory>
#define private public
#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "viewport_minimap_widget.h"
#include "scenario_panel.h"
#include "minimap_export_dialog.h"
#ifndef STAGE3_BASELINE
#include "rebuild64_viewport_widget.h"
#endif
#undef private
#include "camera.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "light.h"
#include "torch.h"
#include "unit.h"
#include "settings.h"
#include "resource_profile.h"
#include "resource_setup_dialog.h"
#include "resourcemanager.h"
#include "res_archive_index.h"
#include "backup_manager.h"
#include "map_save_batch.h"
#include "staged_save_validator.h"
#ifndef STAGE3_BASELINE
#include "rebuild64_startup_guard.h"
#endif

namespace {
QString root;int passes=0,visible=0;QTextStream out(stdout);
void check(bool value,const QString& text){out<<(value?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!value)throw text;++passes;}
QString writable(QStandardPaths::StandardLocation location){return qEnvironmentVariable("EI_EMBED_CASE")+"/known-folders/"+QString::number(int(location));}
qint64 frozenWallTime(){return 1793960547000LL;}
void isolate(){
 auto entry=GetProcAddress(GetModuleHandleW(L"Qt5Core.dll"),"_ZN14QStandardPaths16writableLocationENS_16StandardLocationE");
 if(!entry)throw QString("Qt writableLocation export unavailable");
 unsigned char jump[12]={0x48,0xb8};auto target=reinterpret_cast<quint64>(&writable);std::memcpy(jump+2,&target,8);jump[10]=0xff;jump[11]=0xe0;
 DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(entry),12,PAGE_EXECUTE_READWRITE,&old))throw QString("isolation failed");
 std::memcpy(reinterpret_cast<void*>(entry),jump,12);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),12);DWORD ignored;VirtualProtect(reinterpret_cast<void*>(entry),12,old,&ignored);
 auto clock=GetProcAddress(GetModuleHandleW(L"Qt5Core.dll"),"_ZN9QDateTime22currentMSecsSinceEpochEv");
 if(!clock)throw QString("Qt clock export unavailable");target=reinterpret_cast<quint64>(&frozenWallTime);std::memcpy(jump+2,&target,8);
 if(!VirtualProtect(reinterpret_cast<void*>(clock),12,PAGE_EXECUTE_READWRITE,&old))throw QString("clock isolation failed");
 std::memcpy(reinterpret_cast<void*>(clock),jump,12);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(clock),12);VirtualProtect(reinterpret_cast<void*>(clock),12,old,&ignored);
}
struct Hidden:QObject{bool eventFilter(QObject* object,QEvent* e)override{
 if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(object))if(w->isWindow()){w->setAttribute(Qt::WA_DontShowOnScreen);w->setAttribute(Qt::WA_ShowWithoutActivating);w->move(-32000,-32000);}return false;
}};
BOOL CALLBACK inspect(HWND hwnd,LPARAM){DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(hwnd))++visible;return TRUE;}
bool wait(const std::function<bool()>& predicate,int limit=60000){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<limit)QTest::qWait(10);return predicate();}
void setCamera(CView* view,bool coast){
 QMatrix4x4 camera;camera.lookAt(coast?QVector3D(90,22,52):QVector3D(240,110,175),coast?QVector3D(90,140,6):QVector3D(240,171,10),{0,0,1});view->m_cam->setPreviewView(camera);
 view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1280.f/720.f,1,2000);view->m_projection=view->m_normalProjection;
}
void captureGL(CView* view,const QString& label){
 view->m_overlayAnimationTimer.invalidate(); // water uses its own clock in 281
 QString error;QImage image,previous;int stable=0;QElapsedTimer t;t.start();
 while(t.elapsed()<30000&&stable<2){QApplication::processEvents();view->m_overlayAnimationTimer.invalidate();image=view->captureTopdownPass(ETopdownPass::None,{1280,720},{},&error);stable=(!image.isNull()&&image==previous)?stable+1:0;previous=image;QTest::qWait(20);}
 check(!image.isNull()&&image.save(root+"/"+label+".png"),"GL readback saved: "+label);
 view->makeCurrent();float waterTime=-999;QOpenGLContext::currentContext()->functions()->glGetUniformfv(view->m_landProgram.programId(),view->m_landProgram.uniformLocation("u_overlayTimeSeconds"),&waterTime);int count=-1;QOpenGLContext::currentContext()->functions()->glGetUniformiv(view->m_landProgram.programId(),view->m_landProgram.uniformLocation("u_pointLightCount"),&count);out<<"GL_POINT_LIGHTS "<<count<<"\n";out<<"GL_WATER_TIME "<<waterTime<<"\n";check(waterTime==0,"fixed GL water shader time");
 out<<"GL stable="<<stable<<" wait_ms="<<t.elapsed()<<" size="<<image.width()<<"x"<<image.height()<<"\n";out.flush();
}
quint64 privateBytes(){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));return m.PrivateUsage;}
quint64 videoBytes(){IDXGIFactory1* factory=nullptr;IDXGIAdapter1* adapter=nullptr;IDXGIAdapter3* budget=nullptr;quint64 bytes=0;
 if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)))){
  if(SUCCEEDED(factory->EnumAdapters1(0,&adapter))&&SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),reinterpret_cast<void**>(&budget)))){DXGI_QUERY_VIDEO_MEMORY_INFO info{};if(SUCCEEDED(budget->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info)))bytes=info.CurrentUsage;}
 }if(budget)budget->Release();if(adapter)adapter->Release();if(factory)factory->Release();return bytes;}
}

int mapSaveFailureForTest(){return -1;}
void mapSaveReplacedForTest(int i){if(qEnvironmentVariableIsSet("EI_STAGE3_SAVE_CRASH")&&i==0){QFile f(root+"/save-crash.marker");f.open(QIODevice::WriteOnly);f.write("first atomic replacement committed");f.close();TerminateProcess(GetCurrentProcess(),77);}}
int main(int argc,char** argv){
 SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
 if(qEnvironmentVariableIsEmpty("EI_EMBED_CASE")||qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))return 2;
 isolate();QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QCoreApplication::setAttribute(qEnvironmentVariableIsSet("EI_EMBED_DPR")?Qt::AA_EnableHighDpiScaling:Qt::AA_DisableHighDpiScaling);
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
 QCoreApplication::setOrganizationName("Ataman isolated stage3");QCoreApplication::setApplicationName("renderer-stage3-check");
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR"));
 app.setProperty("workspaceLoadSmoke",true);Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_CASE"));
 const auto mode=app.arguments().value(1,"--visual");const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_FIXTURE"));
 try{
  check(root.contains("/work/rebuild64-renderer-stage3-283-20261106/")&&fixture.contains("/work/rebuild64-renderer-stage3-283-20261106/"),"all writable paths isolated");
  check(QStringList{"--r3-native","--r3-gl","--guard-first","--guard-second","--r2-controls","--visual","--save-recover","--recover-crash","--save-crash","--crash","--quality","--panels","--close-native","--close-probe","--minimap-export"}.contains(mode),"recognized test scenario");
  if(mode=="--save-recover"){
   const auto warnings=recoverMapSaveBatches(root+"/backups",false,[](const QString&){return true;});out<<"RECOVERY "<<warnings.join('\n')<<'\n';
   for(const auto& name:QStringList{"zone1.mob","recovery-second.mob"}){QFile a(fixture+"/Maps/"+name),b(root+"/before-"+name);check(a.open(QIODevice::ReadOnly)&&b.open(QIODevice::ReadOnly),"recovery inputs "+name);auto bytes=a.readAll();check(bytes==b.readAll(),"recovery restores exact before bytes "+name);CMob mob;check(mob.deserialize(bytes),"recovered MOB strict parse "+name);}
  }
  QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[]{EnumWindows(inspect,0);for(QWidget* w:QApplication::topLevelWidgets()){if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();if(auto* b=qobject_cast<QMessageBox*>(w))b->reject();}});dialogs.start(5);
  if(!qEnvironmentVariableIsEmpty("EI_R2_RENDERER_PREF"))QSettings().setValue("ui/renderer",qEnvironmentVariable("EI_R2_RENDERER_PREF"));
  MainWindow window;int crashMessages=0;QObject::connect(window.view(),&CView::updateMsg,[&](const QString& text){if(text.contains(QString::fromUtf8("Предыдущая загрузка Rebuild64")))++crashMessages;});
#ifndef STAGE3_BASELINE
  if(mode=="--guard-first"||mode=="--guard-second"){
   QTest::qWait(300);auto* wrapper=window.findChild<CRebuild64Viewport*>("rebuild64Viewport");
   check(crashMessages==(mode=="--guard-first"?1:0),"one message per crash across restarts");
   check(!wrapper->rebuildSelected()&&!wrapper->backendLoaded(),"automatic native restart suppressed");
   check(QSettings().value("ui/renderer").toString()==qEnvironmentVariable("EI_R2_RENDERER_PREF"),"saved renderer preference unchanged");
   check(wrapper->m_interruptedStarts.isEmpty()&&QDir(wrapper->m_guardDirectory).entryList({"startup-*.json"},QDir::Files).isEmpty(),"old crash markers acknowledged");
   check(QFileInfo::exists(QDir(wrapper->m_guardDirectory).filePath("manual-retry-required")),"retry latch persists without repeated warning");
   if(mode=="--guard-second"){wrapper->selectRebuild(false);check(!QFileInfo::exists(QDir(wrapper->m_guardDirectory).filePath("manual-retry-required")),"manual OpenGL selection clears latch");}
   EnumWindows(inspect,0);check(!visible,"zero visible HWNDs");window.close();return 0;
  }
#endif
  window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
  SResourceProfileEvaluation profile;for(const auto& p:builtInResourceProfiles())if(p.id=="deja-vu")profile=evaluateResourceProfile("C:/GOG Games/PZ",p);
  check(profile.valid,"DejaVu read-only resources evaluated");window.m_settings->applyResourceProfile(profile);
  dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"mapRoots"))->setValue({fixture+"/Maps"});auto* view=window.view();view->refreshAfterResourceSetup();
  window.m_mapCatalog=CMapCatalog::load({"C:/GOG Games/PZ/MODS/Dejavu/Res","C:/GOG Games/PZ/Base/Res"},{fixture+"/Maps"});check(!window.m_mapCatalog.entries.isEmpty(),"real DejaVu map.txt catalog in isolated fixture");
  dynamic_cast<COptBool*>(window.m_settings->opt("overrideWorldTime"))->setValue(true);dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(1240);
  if(auto* units=dynamic_cast<COptBool*>(window.m_settings->opt("drawUnits")))units->setValue(false);
  window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1700,1100);window.show();QApplication::processEvents();
  auto open=[&](const QString& name){QString zone;
   for(const auto& entry:window.m_mapCatalog.entries)if((entry.mprName==name||QFileInfo(entry.mprName).completeBaseName()==name)&&!entry.isBriefing()){zone=entry.zoneId;break;}
   if(name=="zone1")zone="gz1g";
   window.openMapPair(fixture+"/Maps/"+name+".mpr",fixture+"/Maps/"+name+".mob",zone);
   check(wait([&]{return !view->hasPendingOperation()&&!window.backgroundMobLoading()&&view->hasActiveMob();},120000),"production open "+name);
   window.m_minimapRefreshTimer->stop();view->m_freezeAnimations=true;view->m_animationFreezeTime=0;view->m_overlayAnimationTimer.invalidate();view->m_tileGridVisible=false;view->m_bPreviewTile=false;view->m_particleVisualsVisible=false;
   for(auto* mob:view->m_aMob)for(auto* node:mob->nodes())if(dynamic_cast<CUnit*>(node))node->setState(ENodeState::eHidden);
   view->setFixedSize(1280,720);
#ifndef STAGE3_BASELINE
   window.findChild<CRebuild64Viewport*>("rebuild64Viewport")->setFixedSize(1280,720);
#endif
   QApplication::processEvents();check(view->ensureTerrainAutoCatalogReady()&&view->m_terrainEditAvailable,"canonical terrain available "+name);
   out<<"MAP "<<name<<" zone="<<window.m_mapTestEntry.zoneId<<" region="<<view->m_weatherRegion<<" cave="<<view->m_caveEnvironment<<" size="<<view->m_pLand->widthInWorldUnits()<<"x"<<view->m_pLand->heightInWorldUnits()<<'\n';out.flush();
  };
#ifndef STAGE3_BASELINE
  auto* embedded=window.findChild<CRebuild64Viewport*>("rebuild64Viewport");check(embedded,"production wrapper exists");
  QObject::connect(embedded,&CRebuild64Viewport::backendFailure,[&](const QString& s){out<<"BACKEND_FAILURE "<<s<<'\n';out.flush();});
  if(mode=="--recover-crash"){check(!embedded->rebuildSelected()&&!embedded->backendLoaded(),"crashed startup suppressed to GL");check(QSettings().value("ui/renderer").toString()=="Rebuild64","saved renderer choice preserved");check(crashMessages==1&&QFileInfo::exists(QDir(embedded->m_guardDirectory).filePath("manual-retry-required")),"interrupted startup acknowledged once and retry latched");}
  else check(!embedded->backendLoaded(),"GL startup does not load DLL");
  quint64 frames=0;QObject::connect(embedded,&CRebuild64Viewport::frameCommitted,[&](quint64){++frames;});
  auto ready=[&]{return wait([&]{return !embedded->lastFailure().isEmpty()||(embedded->statistics().ready_generation==embedded->m_generation&&embedded->statistics().ready_generation>0&&embedded->m_stack->currentWidget()!=view);},60000)&&embedded->lastFailure().isEmpty();};
  auto owned=[&](const QString& label){const auto previous=frames;QVector<double> samples;quint64 sampled=frames;QElapsedTimer elapsed;elapsed.start();check(wait([&]{if(frames!=sampled){samples.push_back(embedded->statistics().frame_ms);sampled=frames;}return frames>previous+20&&embedded->statistics().pending==0;}),"settled renderer programs and frames "+label);std::sort(samples.begin(),samples.end());if(!samples.isEmpty())out<<"R3_FRAME "<<label<<" n="<<samples.size()<<" min_ms="<<samples.first()<<" median_ms="<<samples[samples.size()/2]<<" p95_ms="<<samples[qMin(samples.size()-1,int(samples.size()*.95))]<<" max_ms="<<samples.last()<<" settle_ms="<<elapsed.elapsed()<<'\n';out.flush();const auto path=root+"/"+label+".png";QFile::remove(path);embedded->capture(path);check(wait([&]{return !QImage(path).isNull();}),"renderer-owned readback "+label);};
#endif
  const auto only=qEnvironmentVariable("EI_STAGE3_MAP_ONLY");open(only.isEmpty()?QString("zone1"):only);
  if(mode=="--visual"||mode=="--r3-native"||mode=="--r3-gl"){
   auto pair=[&](const QString& label,QVector3D eye,QVector3D target,int clock){
    if(mode=="--r3-gl"&&label!="zone1-village-1240")return;
    if(!qEnvironmentVariableIsEmpty("EI_R2_FRAME_ONLY")&&label!=qEnvironmentVariable("EI_R2_FRAME_ONLY"))return;
#ifndef STAGE3_BASELINE
    embedded->selectRebuild(false,false);
#endif
    dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(clock);view->m_overlayAnimationTimer.invalidate();QMatrix4x4 camera;camera.lookAt(eye,target,{0,0,1});view->m_cam->setPreviewView(camera);view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1280.f/720.f,1,2000);view->m_projection=view->m_normalProjection;
    if(mode!="--r3-native")captureGL(view,"gl-"+label);
#ifndef STAGE3_BASELINE
    if(mode=="--r3-native")embedded->m_previewClock.invalidate(); // explicit static world segment0 for these readbacks
#endif
    if(mode=="--r3-gl")return;
#ifndef STAGE3_BASELINE
    QElapsedTimer load;load.start();embedded->selectRebuild(true,false);check(ready(),"new view ready "+label+": "+embedded->lastFailure());out<<"R3_READY "<<label<<" ms="<<load.elapsed()<<'\n';out.flush();owned("owned-"+label);embedded->selectRebuild(false,false);
#endif
   };
   if(only.isEmpty()||only=="zone1")for(int clock:{1240,1930}){const auto h=QString::number(clock);pair("zone1-village-"+h,{240,110,175},{240,171,10},clock);pair("zone1-coast-"+h,{90,22,52},{90,140,6},clock);pair("zone1-top-"+h,{240,160,260},{240,171,10},clock);}
   for(const auto& name:QStringList{"zone12","zone12dun2"})if(only.isEmpty()||only==name){if(only.isEmpty())open(name);const float x=view->m_pLand->widthInWorldUnits()*.5f,y=view->m_pLand->heightInWorldUnits()*.5f,z=view->m_terrainEdit.terrainSurfaceHeight(x,y);pair(name+"-1240",{x,y-80,z+140},{x,y,z},1240);}
  }
#ifndef STAGE3_BASELINE
  else if(mode=="--crash"){embedded->selectRebuild(true,false);check(ready(),"unexpectedly survived injected DLL access violation");throw QString("fault injection did not crash");}
  else if(mode=="--recover-crash"){embedded->selectRebuild(true);check(ready(),"manual opt-in after crash works");check(CRebuild64StartupGuard::interrupted(embedded->m_guardDirectory).isEmpty(),"stale marker cleared after explicit opt-in");check(QDir(embedded->m_guardDirectory).entryList({"startup-*.json"},QDir::Files).isEmpty(),"first accepted frame clears marker");embedded->selectRebuild(false,false);}
  else if(mode=="--save-crash"){
   embedded->selectRebuild(true,false);check(ready(),"Rebuild64 selected during journaled save");QFile input(fixture+"/Maps/zone1.mob");check(input.open(QIODevice::ReadOnly),"save original MOB");const auto before=input.readAll();input.close();view->m_activeMob->setScript(view->m_activeMob->script()+"\r\n// isolated stage3 recovery test\r\n");QByteArray after;CValidationReport report;QString saveClass;check(view->m_activeMob->buildValidatedCandidate(&after,&report,&saveClass),"validated journal candidate");QVector<SMapSaveFile> packet;
   for(const auto& name:QStringList{"zone1.mob","recovery-second.mob"}){QFile original(root+"/before-"+name);check(original.open(QIODevice::WriteOnly)&&original.write(before)==before.size(),"record recovery before "+name);QFile target(fixture+"/Maps/"+name);if(name!="zone1.mob"){check(target.open(QIODevice::WriteOnly)&&target.write(before)==before.size(),"prepare second journal target");target.close();}SMapSaveFile file;file.path=target.fileName();file.before=before;file.after=after;packet.append(file);}
   qputenv("EI_STAGE3_SAVE_CRASH","1");const auto saved=commitMapSaveBatch(packet,root+"/backups");check(saved.success,"journal test save failed before interruption: "+saved.error);throw QString("save interruption hook did not run");
  }
  else if(mode=="--r2-controls"){
   const auto command=root+"/r2-options.txt";SetEnvironmentVariableW(L"ATAMAN_STAGE3_OPTIONS_FILE",reinterpret_cast<const wchar_t*>(command.utf16()));
   auto write=[&](int blend,int aa,int stock,int reflections,int lights,float cave){QSaveFile f(command);check(f.open(QIODevice::WriteOnly),"write R2 controls");f.write(QString("terrain_blend 1 %1\nsmaa 0 %2\nwater_simple 0 %3\nwater_reflections 0 %4\nwater_physical 0 1\nwater_surf_debug 1 0\nmaterial_terrain 0 1\neditor_lights 0 %5\ncave_brightness 2 %6\n").arg(blend).arg(aa).arg(stock).arg(reflections).arg(lights).arg(cave).toLatin1());check(f.commit(),"R2 control commit");};
   auto frame=[&](const QString& name,QVector3D eye,QVector3D target,int time){
    if(qEnvironmentVariableIsSet("EI_R2_WATER_ONLY")&&!name.contains("coast")&&!name.contains("top"))return;
    embedded->selectRebuild(false,false);dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(time);QMatrix4x4 camera;camera.lookAt(eye,target,{0,0,1});view->m_cam->setPreviewView(camera);view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1280.f/720.f,1,2000);view->m_projection=view->m_normalProjection;
    captureGL(view,"gl-"+name);write(1,0,0,1,1,-1);embedded->selectRebuild(true,false);check(ready(),"R2 controls ready");
    const auto cameraValues=embedded->cameraSnapshot();out<<"POINT_LIGHTS "<<name<<" count="<<cameraValues.point_light_count<<" hour="<<cameraValues.hour<<'\n';for(unsigned i=0;i<cameraValues.point_light_count;++i){const auto& l=cameraValues.point_lights[i];out<<"LIGHT "<<l.source_id<<' '<<l.position[0]<<' '<<l.position[1]<<' '<<l.position[2]<<' '<<l.radius<<' '<<l.color[0]<<' '<<l.color[1]<<' '<<l.color[2]<<'\n';}out.flush();
    owned(name+"-blend1");write(0,0,0,1,1,-1);owned(name+"-blend0");write(0,1,0,1,1,-1);owned(name+"-blend0-aa");write(0,1,1,1,1,-1);owned(name+"-stock-water");{QFile f(command);check(f.open(QIODevice::Append),"editor material command");f.write("material_terrain 0 0\n");f.close();owned(name+"-editor");}
    if(time==1930||only=="zone12dun2"){write(0,1,1,1,0,-1);owned(name+"-no-lights");}
    write(0,1,0,0,1,-1);owned(name+"-no-ssr");
    if(name.contains("coast")||name.contains("top")){write(0,1,0,1,1,-1);QFile f(command);check(f.open(QIODevice::Append),"water physical control");f.write("water_physical 0 0\n");f.close();owned(name+"-old-physical");write(0,0,0,1,1,-1);check(f.open(QIODevice::Append),"sea foam debug control");f.write("water_surf_debug 1 3\n");f.close();owned(name+"-foam-debug");}
    if(only=="zone12dun2"){write(0,1,0,1,1,.7f);owned(name+"-cave-default");write(1,1,0,1,1,-1);QFile material(command);check(material.open(QIODevice::Append),"material control");material.write("material_terrain 0 0\n");material.close();owned(name+"-no-terrain-pbr");}
    if(only=="zone1"&&time==1930){
     write(0,1,1,1,1,-1);CLight* source=nullptr;for(auto* mob:view->m_aMob)for(auto* node:mob->nodes())if(!source)source=dynamic_cast<CLight*>(node);check(source,"real map LIGHT available for live update");
     const auto before=embedded->cameraSnapshot();const auto original=source->drawPosition();source->setDrawPosition((original.isNull()?source->position():original)+QVector3D(1,0,0));const auto after=embedded->cameraSnapshot();check(memcmp(before.point_lights,after.point_lights,sizeof(before.point_lights))!=0,"live LIGHT preview changes ABI without save");owned(name+"-light-moved");source->setDrawPosition(original);
     dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(2000);check(embedded->cameraSnapshot().hour==20,"live hour updates camera tuple");dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(time);
    }
    embedded->selectRebuild(false,false);
   };
   if(only=="zone1"){frame("zone1-village-1930",{240,110,175},{240,171,10},1930);frame("zone1-coast-1240",{90,22,52},{90,140,6},1240);frame("zone1-top-1240",{240,160,260},{240,171,10},1240);frame("zone1-village-1240",{240,110,175},{240,171,10},1240);}
   else {float x=view->m_pLand->widthInWorldUnits()*.5f,y=view->m_pLand->heightInWorldUnits()*.5f,z=view->m_terrainEdit.terrainSurfaceHeight(x,y);frame(only+"-1240",{x,y-80,z+140},{x,y,z},1240);}
   SetEnvironmentVariableW(L"ATAMAN_STAGE3_OPTIONS_FILE",nullptr);
  }
  else if(mode=="--quality"){
   QMatrix4x4 camera;if(only.isEmpty()||only=="zone1")camera.lookAt({240,160,260},{240,171,10},{0,0,1});else{const float x=view->m_pLand->widthInWorldUnits()*.5f,y=view->m_pLand->heightInWorldUnits()*.5f,z=view->m_terrainEdit.terrainSurfaceHeight(x,y);camera.lookAt({x,y-80,z+140},{x,y,z},{0,0,1});}view->m_cam->setPreviewView(camera);view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1280.f/720.f,1,2000);view->m_projection=view->m_normalProjection;captureGL(view,"quality-gl");
   embedded->selectRebuild(true,false);check(ready(),"quality control native ready");
   const auto path=root+"/quality-options.txt";SetEnvironmentVariableW(L"ATAMAN_STAGE3_OPTIONS_FILE",reinterpret_cast<const wchar_t*>(path.utf16()));
   auto control=[&](const QString& label,int distance,int smaa,int material,int filtering){QSaveFile file(path);check(file.open(QIODevice::WriteOnly),"quality command file");file.write(QString("draw_distance 1 %1\nsmaa 0 %2\nmaterial_quality 1 %3\nanisotropy 1 %4\n").arg(distance).arg(smaa).arg(material).arg(filtering).toLatin1());check(file.commit(),"quality command committed");owned(label);};
   control("quality-distance300-smaa",300,1,2,3);control("quality-distance2000-smaa",2000,1,2,3);control("quality-distance2000-no-smaa",2000,0,2,3);control("quality-game-materials-nativeAA",2000,0,3,4);SetEnvironmentVariableW(L"ATAMAN_STAGE3_OPTIONS_FILE",nullptr);embedded->selectRebuild(false,false);
   if(only.isEmpty()||only=="zone1"){
    dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(1930);captureGL(view,"quality-gl-night");embedded->selectRebuild(true,false);check(ready(),"night control native ready");SetEnvironmentVariableW(L"ATAMAN_STAGE3_OPTIONS_FILE",reinterpret_cast<const wchar_t*>(path.utf16()));
    for(int variant:{0,1,2}){QSaveFile file(path);check(file.open(QIODevice::WriteOnly),"night quality command");file.write(QString("draw_distance 1 1000\nsmaa 0 0\nmaterial_quality 1 3\nanisotropy 1 4\nao_quality 1 %1\ncave_gi 1 %2\n").arg(variant==1?0:2).arg(variant==2?0:2).toLatin1());check(file.commit(),"night quality command committed");owned(QString("quality-night-")+QString::number(variant));}
    SetEnvironmentVariableW(L"ATAMAN_STAGE3_OPTIONS_FILE",nullptr);embedded->selectRebuild(false,false);
   }
  }
  else if(mode=="--panels"){
   const auto dpr=embedded->devicePixelRatioF();out<<"DPR "<<dpr<<'\n';check(!qEnvironmentVariableIsSet("EI_EMBED_DPR")||qAbs(dpr-1.5)<.01,"requested DPR1.5 is actual");
   CRebuild64StartupGuard liveMarker(embedded->m_guardDirectory);check(liveMarker.arm(),"live process marker armed");check(CRebuild64StartupGuard::interrupted(embedded->m_guardDirectory).isEmpty(),"live process marker never treated as crash");liveMarker.clear();
   QJsonArray inventory;for(auto* child:view->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly)){inventory.append(QJsonObject{{"class",child->metaObject()->className()},{"name",child->objectName()},{"hidden",child->isHidden()}});}QFile inv(root+"/child-inventory.json");inv.open(QIODevice::WriteOnly);inv.write(QJsonDocument(inventory).toJson());inv.close();
   auto composite=[&](const QString& label){QImage image;
    if(embedded->rebuildSelected()){owned("view-"+label);image=QImage(root+"/view-"+label+".png");}else{captureGL(view,"view-"+label);image=QImage(root+"/view-"+label+".png");}
    image=image.convertToFormat(QImage::Format_ARGB32);QPainter painter(&image);painter.scale(image.width()/double(1280),image.height()/double(720));
    for(auto* panel:QList<QWidget*>{window.m_viewportMinimap,view->m_timeOfDayWidget,view->m_environmentPreviewWidget,view->m_aiGraphPreviewWidget})if(panel->isVisible()){QPixmap pixels(panel->size()*panel->devicePixelRatioF());pixels.setDevicePixelRatio(panel->devicePixelRatioF());pixels.fill(Qt::transparent);panel->render(&pixels);painter.drawPixmap(panel->pos(),pixels);}painter.end();check(image.save(root+"/panels-"+label+".png"),"offscreen panel composition "+label);
   };
   QMap<QString,QRect> baseline;
   for(bool native:{false,true,false}){
    embedded->selectRebuild(native,false);if(native)check(ready(),"panels native accepted");view->m_aiGraphPreviewWidget->show();view->positionEnvironmentPreviewWidget();window.positionViewportMinimap();QApplication::processEvents();
    const auto label=native?QString("native"):baseline.isEmpty()?QString("gl-before"):QString("gl-after");
    for(auto* panel:QList<QWidget*>{window.m_viewportMinimap,view->m_timeOfDayWidget,view->m_environmentPreviewWidget,view->m_aiGraphPreviewWidget}){check(panel->isVisible(),"panel visible "+label+" "+panel->objectName());if(baseline.contains(panel->objectName()))check(panel->geometry()==baseline.value(panel->objectName()),"same logical geometry "+panel->objectName());else baseline[panel->objectName()]=panel->geometry();if(native)check(panel->testAttribute(Qt::WA_NativeWindow)&&GetParent(reinterpret_cast<HWND>(panel->winId()))==reinterpret_cast<HWND>(embedded->findChild<QWidget*>("rebuild64NativeSurface")->winId()),"panel native parent above D3D "+panel->objectName());}
    composite(label);
    auto* expand=window.m_viewportMinimap->findChild<QToolButton*>("viewportMinimapExpand");const auto height=window.m_viewportMinimap->height();QTest::mouseClick(expand,Qt::LeftButton);QApplication::processEvents();check(window.m_viewportMinimap->height()!=height,"minimap collapse responds "+label);QTest::mouseClick(expand,Qt::LeftButton);
    view->m_cam->clearPreviewView();view->m_cam->setLocation({240,110,175},{240,171,10},{-70,0,0});view->m_cam->update();const auto beforeCamera=view->m_cam->viewMatrix();auto* mini=window.m_viewportMinimap->findChild<QLabel*>("viewportMinimapImage");QTest::mouseClick(mini,Qt::LeftButton,Qt::NoModifier,{mini->width()*3/4,mini->height()/2});view->m_cam->update();check(beforeCamera!=view->m_cam->viewMatrix(),"minimap click moves shared camera "+label);
    auto* time=view->m_timeOfDayWidget->findChild<QSlider*>();check(time&&time->isEnabled()&&time->width()>20,"time slider enabled "+label);const auto oldTime=time->value();QTest::mouseClick(time,Qt::LeftButton,Qt::NoModifier,{time->width()*(oldTime>720?1:3)/4,time->height()/2});QApplication::processEvents();out<<"TIME "<<label<<" before="<<oldTime<<" after="<<time->value()<<'\n';check(time->value()!=oldTime,"time slider click event "+label);check(qAbs(view->effectiveWorldTimeHours()-time->value()/60.f)<.02,"time changes shared environment immediately "+label);if(native){embedded->tick();check(qAbs(embedded->cameraSnapshot().hour-view->effectiveWorldTimeHours())<.001,"new view receives time immediately");}
    auto* collapse=view->m_environmentPreviewWidget->findChild<QToolButton*>("visualsCollapse");const bool body=view->m_environmentPreviewBody->isVisible();QTest::mouseClick(collapse,Qt::LeftButton);check(view->m_environmentPreviewBody->isVisible()!=body,"environment collapse responds "+label);QTest::mouseClick(collapse,Qt::LeftButton);
    auto* nav=view->m_aiGraphPreviewWidget->findChild<QToolButton*>("navigationCollapse");const bool navBody=view->m_aiGraphPreviewBody->isVisible();QTest::mouseClick(nav,Qt::LeftButton);check(view->m_aiGraphPreviewBody->isVisible()!=navBody,"AI navigation collapse responds "+label);QTest::mouseClick(nav,Qt::LeftButton);
    auto* dock=window.findChild<QDockWidget*>("mapScenarioDock");dock->show();QApplication::processEvents();check(window.m_scenarioPanel->isVisible(),"scenario dock visible "+label);auto* tab=window.m_scenarioPanel->findChild<QTabWidget*>();if(tab&&tab->count()>1){const auto index=tab->currentIndex();QTest::mouseClick(tab->tabBar(),Qt::LeftButton,Qt::NoModifier,tab->tabBar()->tabRect((index+1)%tab->count()).center());check(tab->currentIndex()!=index,"scenario tab click responds "+label);}else{dock->toggleViewAction()->trigger();check(!dock->isVisible(),"scenario dock action responds "+label);dock->show();}QPixmap shot(window.m_scenarioPanel->size()*dpr);shot.setDevicePixelRatio(dpr);window.m_scenarioPanel->render(&shot);shot.save(root+"/scenario-"+label+".png");dock->hide();
   }
   embedded->selectRebuild(true,false);check(ready(),"native ready before minimap and autosave");view->setDiplomacyMapMode(true);check(wait([&]{return view->m_diplomacyOverlay&&view->m_diplomacyOverlay->parentWidget()!=view&&view->m_diplomacyOverlay->isVisible();}),"lazy diplomacy panel discovered above native surface");QPixmap diplomacy(view->m_diplomacyOverlay->size()*dpr);diplomacy.setDevicePixelRatio(dpr);view->m_diplomacyOverlay->render(&diplomacy);diplomacy.save(root+"/diplomacy-native.png");QTest::mouseClick(view->m_diplomacyOverlay->findChild<QPushButton*>("diplomacyGroupColors"),Qt::LeftButton);check(view->diplomacyMapMode(),"diplomacy panel receives click");view->setDiplomacyMapMode(false);
   const auto dirty=view->m_activeMob->isDirty();window.m_minimapTargetTextureSize=256;check(window.ensureGeneratedMinimap(),"minimap generation while Rebuild64 selected");check(embedded->rebuildSelected()&&!window.m_generatedMinimapMmp.isEmpty(),"minimap generation retains backend and encoded export");check(view->m_activeMob->isDirty()==dirty,"minimap generation preserves dirty state");
   check(window.m_generatedMinimapImage.save(root+"/generated-minimap.png"),"minimap preview saved offscreen");QFile exportFile(root+"/generated-minimap.mmp");check(exportFile.open(QIODevice::WriteOnly)&&exportFile.write(window.m_generatedMinimapMmp)==window.m_generatedMinimapMmp.size(),"minimap encoded export written to own path");exportFile.close();
   const auto original=view->m_activeMob->script();view->m_activeMob->setScript(original+"\r\n// stage3 autosave fixture\r\n");view->m_activeMob->setDirty(true);STerrainBrush brush;brush.centerX=240;brush.centerY=176;brush.radius=2;const auto change=view->m_terrainEdit.makeRaiseLower(brush,50,true);check(view->applyTerrainHeightPreview(change,true),"unsaved terrain before native autosave");window.performAutosave();check(wait([&]{return !window.m_mprAutosaveWatcher;},120000),"background MPR autosave completed");int mobs=0,maps=0;QDirIterator it(root+"/backups",{"*.mob","*.mpr"},QDir::Files,QDirIterator::Subdirectories);while(it.hasNext()){const auto path=it.next();QString error;if(CBackupManager::validateAutosave(QFileInfo(path),&error)){if(path.endsWith(".mob"))++mobs;else ++maps;}}check(mobs>0&&maps>0,"valid MOB and MPR autosave payloads while Rebuild64 selected");check(view->m_activeMob->isDirty()&&view->m_pLand->isDirty(),"autosave retains unsaved dirty state");check(view->applyTerrainHeightPreview(change,false),"restore test terrain in memory");view->m_pLand->setDirty(false);view->m_activeMob->setScript(original);view->m_activeMob->setDirty(dirty);embedded->selectRebuild(false,false);
  }
  else if(mode=="--close-native"){embedded->selectRebuild(true,false);check(ready(),"native selected at normal close");check(QDir(embedded->m_guardDirectory).entryList({"startup-*.json"},QDir::Files).isEmpty(),"healthy native marker cleared before close");}
  else if(mode=="--minimap-export"){
   embedded->selectRebuild(true,false);check(ready(),"native selected before production minimap export");const auto terrain=view->m_terrainEdit.stateSha256();const auto dirty=view->m_activeMob->isDirty();window.m_minimapTargetTextureSize=256;check(window.ensureGeneratedMinimap(),"minimap generated with hidden GL capture");
   window.generateMinimap();check(window.m_minimapExportDialog&&window.m_minimapExportDialog->isVisible(),"production minimap export dialog open offscreen");auto* dialog=window.m_minimapExportDialog.data();QDir().mkpath(root+"/export");const auto destination=root+"/export/stage3-minimap.res";dialog->m_destination->setText(destination);dialog->m_textureName->setText("stage3minimap");dialog->m_registerCatalog->setChecked(false);dialog->updateReadyState();check(dialog->m_exportButton->isEnabled(),"production export button enabled for own destination");QTest::mouseClick(dialog->m_exportButton,Qt::LeftButton);check(dialog->exportSucceeded(),"production exportTexture succeeds while Rebuild64 selected");CResArchiveIndex archive;QString error;check(archive.open(destination,&error),"exported RES strict reopen: "+error);const auto member=archive.readMember("stage3minimap.mmp",&error);check(!member.isEmpty(),"exported minimap MMP read back from RES");check(embedded->rebuildSelected()&&view->m_terrainEdit.stateSha256()==terrain&&view->m_activeMob->isDirty()==dirty,"export retains selected backend, terrain and dirty state");dialog->hide();embedded->selectRebuild(false,false);
  }
#endif
  EnumWindows(inspect,0);check(visible==0,"zero visible process HWNDs");
  view->m_pLand->setDirty(false);for(auto* mob:view->m_aMob)mob->setDirty(false);
  view->unloadLand(false);QApplication::processEvents();view->makeCurrent();if(auto* context=QOpenGLContext::currentContext())context->functions()->glFinish();view->doneCurrent();
  QFile result(root+"/result.json");check(result.open(QIODevice::WriteOnly),"result receipt");result.write(QJsonDocument(QJsonObject{{"pass",passes},{"visible",visible},{"mode",mode}}).toJson());result.close();window.close();check(wait([&]{return !window.isVisible();}),"normal shutdown");return 0;
 }catch(const QString& s){out<<"FAIL "<<s<<'\n';out.flush();return 1;}
}

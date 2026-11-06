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
#ifndef EIR_BASELINE
#include "rebuild64_viewport_widget.h"
#endif
#undef private
#include "camera.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "unit.h"
#include "settings.h"
#include "resource_profile.h"
#include "resource_setup_dialog.h"
#include "resourcemanager.h"

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
 while(t.elapsed()<30000&&stable<2){QApplication::processEvents();image=view->captureTopdownPass(ETopdownPass::None,{1280,720},{},&error);stable=(!image.isNull()&&image==previous)?stable+1:0;previous=image;QTest::qWait(20);}
 check(!image.isNull()&&image.save(root+"/"+label+".png"),"GL readback saved: "+label);
 out<<"GL stable="<<stable<<" wait_ms="<<t.elapsed()<<" size="<<image.width()<<"x"<<image.height()<<"\n";out.flush();
}
quint64 privateBytes(){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));return m.PrivateUsage;}
quint64 videoBytes(){IDXGIFactory1* factory=nullptr;IDXGIAdapter1* adapter=nullptr;IDXGIAdapter3* budget=nullptr;quint64 bytes=0;
 if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory)))){
  if(SUCCEEDED(factory->EnumAdapters1(0,&adapter))&&SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),reinterpret_cast<void**>(&budget)))){DXGI_QUERY_VIDEO_MEMORY_INFO info{};if(SUCCEEDED(budget->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info)))bytes=info.CurrentUsage;}
 }if(budget)budget->Release();if(adapter)adapter->Release();if(factory)factory->Release();return bytes;}
}
int main(int argc,char** argv){
 SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
 if(qEnvironmentVariableIsEmpty("EI_EMBED_CASE")||qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))return 2;
 isolate();QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QCoreApplication::setAttribute(qEnvironmentVariableIsSet("EI_EMBED_DPR")?Qt::AA_EnableHighDpiScaling:Qt::AA_DisableHighDpiScaling);
 QApplication app(argc,argv);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);app.setQuitOnLastWindowClosed(false);
 QCoreApplication::setOrganizationName("Ataman isolated embed282");QCoreApplication::setApplicationName("renderer-embed-check");
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR"));
 app.setProperty("workspaceLoadSmoke",true);Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_CASE"));
 const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_FIXTURE"));
 try{
  check(root.contains("/work/rebuild64-renderer-embed-282-20261106/")&&fixture.contains("/work/rebuild64-renderer-embed-282-20261106/"),"owned paths");
  QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[]{EnumWindows(inspect,0);for(QWidget* w:QApplication::topLevelWidgets()){if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();if(auto* b=qobject_cast<QMessageBox*>(w))b->reject();}});dialogs.start(5);
  MainWindow window;window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
  SResourceProfileEvaluation profile;for(const auto& p:builtInResourceProfiles())if(p.id=="deja-vu")profile=evaluateResourceProfile("C:/GOG Games/PZ",p);
  check(profile.valid,"readonly DejaVu resources evaluated");window.m_settings->applyResourceProfile(profile);
  dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"mapRoots"))->setValue({fixture+"/Maps"});
  auto* view=window.view();view->refreshAfterResourceSetup();
  if(auto* value=dynamic_cast<COptBool*>(window.m_settings->opt("overrideWorldTime")))value->setValue(true);
  if(auto* value=dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay")))value->setValue(1240);
  if(auto* value=dynamic_cast<COptBool*>(window.m_settings->opt("drawUnits")))value->setValue(false);
  window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1700,1100);window.show();QApplication::processEvents();
  auto open=[&](const QString& name,const QString& zone){
   window.openMapPair(fixture+"/Maps/"+name+".mpr",fixture+"/Maps/"+name+".mob",zone);
   check(wait([&]{return !view->hasPendingOperation()&&!window.backgroundMobLoading()&&view->hasActiveMob();},120000),"production open "+name);
   window.m_minimapRefreshTimer->stop();view->m_freezeAnimations=true;view->m_animationFreezeTime=0;view->m_tileGridVisible=false;view->m_bPreviewTile=false;view->m_particleVisualsVisible=false;
   for(auto* mob:view->m_aMob)for(auto* node:mob->nodes())if(dynamic_cast<CUnit*>(node))node->setState(ENodeState::eHidden);
   view->setFixedSize(1280,720);
#ifndef EIR_BASELINE
   if(auto* viewport=window.findChild<CRebuild64Viewport*>("rebuild64Viewport"))viewport->setFixedSize(1280,720);
#endif
   QApplication::processEvents();
   check(view->ensureTerrainAutoCatalogReady()&&view->m_terrainEditAvailable,"canonical in-memory terrain available");
  };
  open("zone1","gz1g");setCamera(view,false);
#ifndef EIR_BASELINE
  auto* embedded=window.findChild<CRebuild64Viewport*>("rebuild64Viewport");check(embedded,"native viewport wrapper exists");
  QObject::connect(embedded,&CRebuild64Viewport::backendFailure,[&](const QString& reason){out<<"BACKEND_FAILURE "<<reason<<'\n';out.flush();});
  check(!embedded->backendLoaded()&&!GetModuleHandleW(L"ataman_rebuild64.dll"),"OpenGL startup never loads DLL");
#endif
  captureGL(view,"gl-village");setCamera(view,true);captureGL(view,"gl-coast");
  out<<"gl_only_private="<<privateBytes()<<" gl_only_vram="<<videoBytes()<<'\n';out.flush();
  out<<"active_atlas="<<view->m_activeTerrainArchivePath<<" sources="<<configuredResourceSourcePaths(window.m_settings.data()).join(';')<<'\n';out.flush();
#ifndef EIR_BASELINE
  if(!app.arguments().contains("--gl-only")){
   const QByteArray hook=qgetenv("ATAMAN_REBUILD64_TEST_FAILURE");
   setCamera(view,false);
   if(!hook.isEmpty()) {
    STerrainBrush faultBrush;faultBrush.centerX=240;faultBrush.centerY=176;faultBrush.radius=2;
    auto faultEdit=view->m_terrainEdit.makeRaiseLower(faultBrush,50,true);
    check(view->applyTerrainHeightPreview(faultEdit,true),"unsaved height before failure");view->m_activeMob->setDirty(true);
   }
   const auto beforeCamera=view->m_cam->viewMatrix();const auto terrainHash=view->m_terrainEdit.stateSha256();const auto mobDirty=view->m_activeMob->isDirty();
   QElapsedTimer heartbeatClock;heartbeatClock.start();qint64 heartbeatLast=0,heartbeatMax=0;int heartbeatCount=0;QTimer heartbeat;
   QObject::connect(&heartbeat,&QTimer::timeout,[&]{const auto now=heartbeatClock.elapsed();heartbeatMax=qMax(heartbeatMax,now-heartbeatLast);heartbeatLast=now;++heartbeatCount;});heartbeat.start(10);
   QElapsedTimer launch;launch.start();embedded->selectRebuild(true);
   if(!hook.isEmpty()){
    check(wait([&]{return !embedded->lastFailure().isEmpty();}),"failure hook handled: "+QString::fromLatin1(hook));
    check(!embedded->rebuildSelected(),"fallback selects OpenGL");
    check(view->m_terrainEdit.stateSha256()==terrainHash&&view->m_activeMob->isDirty()==mobDirty,"failure preserves document/dirty state");
    check(view->m_cam->viewMatrix()==beforeCamera,"failure preserves camera");
   }else{
    check(wait([&]{return !embedded->lastFailure().isEmpty()||(embedded->statistics().ready_generation==embedded->m_generation&&embedded->statistics().ready_generation>0);})&&embedded->lastFailure().isEmpty(),"Rebuild64 current generation committed: "+embedded->lastFailure());
    heartbeat.stop();out<<"first_view_ms="<<launch.elapsed()<<" ui_heartbeats="<<heartbeatCount<<" ui_max_gap_ms="<<heartbeatMax<<" dpr="<<embedded->devicePixelRatioF()<<" physical="<<embedded->statistics().width<<"x"<<embedded->statistics().height<<" private="<<privateBytes()<<" vram="<<embedded->statistics().video_bytes<<" frame_ms="<<embedded->statistics().frame_ms<<'\n';out.flush();
    auto capture=[&](const QString& label){const auto path=root+"/"+label+".png";embedded->capture(path);check(wait([&]{QImage image(path);return !image.isNull();}),"renderer-owned complete readback: "+label);};
    capture("owned-village");
    const auto noEditGeneration=embedded->m_generation;embedded->m_documentHash.clear();embedded->m_documentRefreshPending=true;embedded->m_sceneSnapshotTimer.invalidate();embedded->tick();
    check(wait([&]{return embedded->m_generation>noEditGeneration&&embedded->statistics().ready_generation==embedded->m_generation;}),"identical document republished control");capture("owned-noedit");
    setCamera(view,true);QTest::qWait(500);capture("owned-coast");
    view->m_cam->clearPreviewView();view->m_cam->setLocation({240,110,175},{0,0,0},{-70,0,0});
    const auto beforeInput=view->m_cam->viewMatrix();
    auto* native=embedded->findChild<QWidget*>("rebuild64NativeSurface");check(native,"native camera input target");
    QKeyEvent press(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier);QApplication::sendEvent(native,&press);QTest::qWait(150);
    QKeyEvent release(QEvent::KeyRelease,Qt::Key_W,Qt::NoModifier);QApplication::sendEvent(native,&release);QTest::qWait(150);
    check(view->m_cam->viewMatrix()!=beforeInput,"native W camera movement uses shared controller");
    const auto matrix=view->m_cam->viewMatrix();embedded->selectRebuild(false);check(view->m_cam->viewMatrix()==matrix,"Rebuild64 to OpenGL camera exact");
    setCamera(view,true);
    launch.restart();embedded->selectRebuild(true);check(wait([&]{return embedded->statistics().ready_generation==embedded->m_generation;}),"OpenGL to Rebuild64 without restart");out<<"repeat_switch_ms="<<launch.elapsed()<<'\n';
    embedded->selectRebuild(false);setCamera(view,false);
    check(QSettings().value("ui/renderer").toString()=="OpenGL","OpenGL choice persisted");
    STerrainBrush brush;brush.centerX=240;brush.centerY=176;brush.radius=10;
    const auto command=view->m_terrainEdit.makeRaiseLower(brush,6.f*65535.f/view->m_terrainEdit.maxZ(),true);
    check(!command.isEmpty()&&view->applyTerrainHeightPreview(command,true),"unsaved canonical terrain edit");
    CNode* moved=nullptr;for(auto* n:view->m_activeMob->nodes())if(n&&!dynamic_cast<CUnit*>(n)&&n->position().x()>210&&n->position().x()<270&&n->position().y()>145&&n->position().y()<210&&n->modelName().startsWith("stbuho")){moved=n;break;}
    if(!moved)for(auto* n:view->m_activeMob->nodes())if(n&&!dynamic_cast<CUnit*>(n)&&n->position().x()>210&&n->position().x()<270&&n->position().y()>145&&n->position().y()<210){moved=n;break;}
    check(moved,"static object selected for memory edit");const auto position=moved->position();QVector3D changed=position+QVector3D(12,0,0);moved->setPos(changed);view->m_activeMob->setDirty(true);
    const auto generation=embedded->m_generation;embedded->selectRebuild(true);check(wait([&]{return embedded->m_generation>generation&&embedded->statistics().ready_generation==embedded->m_generation;}),"unsaved height and object snapshot committed");capture("owned-edited");
    check(QSettings().value("ui/renderer").toString()=="Rebuild64","Rebuild64 choice persisted");
    out<<"edited_object_id="<<moved->mapId()<<" before="<<position.x()<<","<<position.y()<<","<<position.z()<<" after="<<changed.x()<<","<<changed.y()<<","<<changed.z()<<'\n';out.flush();
    check(view->applyTerrainHeightPreview(command,false),"height reverted for isolated object image");const auto objectOnlyGeneration=embedded->m_generation;
    check(wait([&]{return embedded->m_generation>objectOnlyGeneration&&embedded->statistics().ready_generation==embedded->m_generation;}),"object-only memory snapshot committed");capture("owned-object-only");
    check(view->applyTerrainHeightPreview(command,true),"height reapplied before restore");
    embedded->selectRebuild(false);QVector3D restore=position;moved->setPos(restore);view->m_activeMob->setDirty(mobDirty);check(view->applyTerrainHeightPreview(command,false),"test edit reverted in memory");view->m_pLand->setDirty(false);
    if(app.arguments().contains("--bench")) {
     embedded->selectRebuild(true);check(wait([&]{return embedded->statistics().ready_generation==embedded->m_generation;}),"baseline generation before benchmark");
     QJsonArray samples;const auto privateBefore=privateBytes(),videoBefore=embedded->statistics().video_bytes;
     for(int sample=0;sample<50;++sample) {
      const bool forward=(sample%2)==0;QElapsedTimer timer;timer.start();const auto previous=embedded->m_generation;
      check(view->applyTerrainHeightPreview(command,forward),"benchmark memory height toggle "+QString::number(sample));
      check(wait([&]{return embedded->m_generation>previous&&embedded->statistics().ready_generation==embedded->m_generation;}),"benchmark generation ready "+QString::number(sample));
      const QString path=root+"/bench-last.png";QFile::remove(path);embedded->capture(path);check(wait([&]{return QFileInfo(path).size()>0;}),"benchmark accepted readback "+QString::number(sample));
      samples.append(QJsonObject{{"sample",sample},{"ms",timer.elapsed()},{"generation",double(embedded->m_generation)},{"private",double(privateBytes())},{"vram",double(embedded->statistics().video_bytes)}});
     }
     const auto privateAfter=privateBytes(),videoAfter=embedded->statistics().video_bytes;
     QJsonArray frames;QMetaObject::Connection connection=QObject::connect(embedded,&CRebuild64Viewport::frameCommitted,[&](quint64){if(frames.size()<120)frames.append(embedded->statistics().frame_ms);});
     check(wait([&]{return frames.size()==120;},30000),"120 completed frame samples");QObject::disconnect(connection);
     QTest::qWait(10000);
     QFile file(root+"/benchmark.json");check(file.open(QIODevice::WriteOnly),"benchmark receipt");file.write(QJsonDocument(QJsonObject{{"samples",samples},{"frames",frames},{"privateBefore",double(privateBefore)},{"privateAfter50",double(privateAfter)},{"privateDrained",double(privateBytes())},{"videoBefore",double(videoBefore)},{"videoAfter50",double(videoAfter)},{"videoDrained",double(embedded->statistics().video_bytes)}}).toJson());
     embedded->selectRebuild(false);view->m_pLand->setDirty(false);
    }
    open("zone2","gz2g");setCamera(view,false);embedded->selectRebuild(true);check(wait([&]{return embedded->statistics().ready_generation==embedded->m_generation;}),"A to B committed");
    embedded->selectRebuild(false);open("zone1","gz1g");setCamera(view,false);embedded->selectRebuild(true);check(wait([&]{return embedded->statistics().ready_generation==embedded->m_generation;}),"B to A committed in same DLL");capture("owned-A2");
    embedded->setFixedSize(1417,803);view->setFixedSize(1417,803);check(wait([&]{auto s=embedded->statistics();return s.width==uint32_t(qRound(embedded->width()*embedded->devicePixelRatioF()))&&s.height==uint32_t(qRound(embedded->height()*embedded->devicePixelRatioF()));}),"native resize uses physical DPR dimensions");
    embedded->selectRebuild(false);setCamera(view,false);captureGL(view,"gl-after-switches");
    embedded->selectRebuild(true);check(wait([&]{return embedded->statistics().ready_generation==embedded->m_generation;}),"native ready before explicit close");
    view->unloadLand(false);check(wait([&]{return !view->m_pLand->isMprLoad()&&embedded->m_stack->currentWidget()==view;}),"closing terrain returns empty OpenGL view");
    open("zone1","gz1g");setCamera(view,false);check(wait([&]{return embedded->statistics().ready_generation==embedded->m_generation;}),"reopen after explicit close in same DLL");embedded->selectRebuild(false);
   }
  }
#endif
  EnumWindows(inspect,0);check(visible==0,"zero visible process HWNDs");
  view->m_pLand->setDirty(false);for(auto* mob:view->m_aMob)mob->setDirty(false);
  QFile result(root+"/result.json");check(result.open(QIODevice::WriteOnly),"result file");result.write(QJsonDocument(QJsonObject{{"pass",passes},{"visible",visible}}).toJson());
  window.close();check(wait([&]{return !window.isVisible();}),"normal window shutdown");
  return 0;
 }catch(const QString& error){out<<"FAIL "<<error<<'\n';out.flush();return 1;}
}

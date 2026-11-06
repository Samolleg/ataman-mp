// Isolated real Qt widget/document/renderer integration. Never a desktop capture.
#define main stage3UnusedMain
#include "rebuild64_stage3_check.cpp"
#undef main
#include "operationmanager.h"
#include "key_manager.h"
#include "scene.h"
#include <QOpenGLFramebufferObject>
#include "object_base.h"
#include <QShortcutEvent>
#include <QContextMenuEvent>
#include "tree_view.h"
#include <cstdio>

LONG WINAPI ownTestFault(EXCEPTION_POINTERS* fault) {
 wchar_t rootPath[32768];const auto count=GetEnvironmentVariableW(L"EI_EMBED_CASE",rootPath,32768);if(!count||count>32700)return EXCEPTION_EXECUTE_HANDLER;
 wcscat_s(rootPath,L"/fault-result.json");MODULEINFO module{};GetModuleInformation(GetCurrentProcess(),GetModuleHandleW(L"ataman_rebuild64.dll"),&module,sizeof(module));
 const auto address=reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress),base=reinterpret_cast<uintptr_t>(module.lpBaseOfDll);
 const auto file=CreateFileW(rootPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file!=INVALID_HANDLE_VALUE){char text[256];const auto n=std::snprintf(text,sizeof(text),"{\"exception\":%lu,\"insideTestDll\":%s}",fault->ExceptionRecord->ExceptionCode,address>=base&&address<base+module.SizeOfImage?"true":"false");DWORD written;WriteFile(file,text,DWORD(n),&written,nullptr);CloseHandle(file);}
 // The real AV still terminates this process. Suppress WER/UI and any OS dump
 // outside the explicitly isolated test tree.
 return EXCEPTION_EXECUTE_HANDLER;
}

void keyEvent(QWidget* receiver,int qt,int native,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
 QKeyEvent press(QEvent::KeyPress,qt,modifiers,0,native,0);QApplication::sendEvent(receiver,&press);
 QKeyEvent release(QEvent::KeyRelease,qt,modifiers,0,native,0);QApplication::sendEvent(receiver,&release);
}
void pointer(QWidget* receiver,QEvent::Type type,QPoint p,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers mods=Qt::NoModifier) {
 QMouseEvent e(type,p,receiver->mapToGlobal(p),button,buttons,mods);QApplication::sendEvent(receiver,&e);
}
void click(QWidget* receiver,QPoint p,Qt::KeyboardModifiers mods=Qt::NoModifier) {
 pointer(receiver,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,mods);pointer(receiver,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,mods);
}
QByteArray mobBytes(CMob* mob){QByteArray bytes;mob->serializeMob(bytes,false);return bytes;}
QStringList ownResourceHandles286() {
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

void testGroupMenu(CView* view) {
    QApplication::processEvents();int menus=0,actions=0;
    QTimer closer;QObject::connect(&closer,&QTimer::timeout,[&]{for(QWidget* widget:QApplication::topLevelWidgets())if(auto* menu=qobject_cast<QMenu*>(widget))if(menu->isVisible()){++menus;actions=qMax(actions,menu->actions().size());menu->close();}});closer.start(10);
    QTreeWidgetItem* item=nullptr;for(QTreeWidgetItemIterator it(view->m_pTree);*it;++it)if(auto* group=dynamic_cast<CTreeObject*>(*it))if(!group->node()&&group->childCount()){item=group;break;}
    check(item,"outliner group with existing context menu");view->m_pTree->scrollToItem(item);const auto at=view->m_pTree->visualItemRect(item).center();QContextMenuEvent context(QContextMenuEvent::Mouse,at,view->m_pTree->viewport()->mapToGlobal(at));QApplication::sendEvent(view->m_pTree->viewport(),&context);closer.stop();check(menus>0&&actions==2,"canonical outliner group menu in native view");
}
int main(int argc,char** argv) {
 SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
 if(qEnvironmentVariableIsEmpty("EI_EMBED_CASE")||qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))return 2;
 isolate();QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QCoreApplication::setAttribute(qEnvironmentVariableIsSet("EI_EMBED_DPR")?Qt::AA_EnableHighDpiScaling:Qt::AA_DisableHighDpiScaling);
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);app.setAttribute(Qt::AA_DontUseNativeDialogs);
 QCoreApplication::setOrganizationName("Ataman isolated tools286");QCoreApplication::setApplicationName("tools286");
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR"));
 app.setProperty("workspaceLoadSmoke",true);Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_CASE"));const auto fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EMBED_FIXTURE"));
 try {
  check(root.contains("/work/rebuild64-tools-286-")&&fixture.contains("/work/rebuild64-tools-286-"),"owned writable paths");
  QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[]{EnumWindows(inspect,0);for(QWidget* w:QApplication::topLevelWidgets()){if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();if(auto* b=qobject_cast<QMessageBox*>(w))b->reject();}});dialogs.start(5);
  MainWindow window;window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
  SResourceProfileEvaluation profile;for(const auto& p:builtInResourceProfiles())if(p.id=="deja-vu")profile=evaluateResourceProfile("C:/GOG Games/PZ",p);
  check(profile.valid,"read-only DejaVu profile");window.m_settings->applyResourceProfile(profile);dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"mapRoots"))->setValue({fixture+"/Maps"});
  auto* view=window.view();view->refreshAfterResourceSetup();window.m_mapCatalog=CMapCatalog::load({"C:/GOG Games/PZ/MODS/Dejavu/Res","C:/GOG Games/PZ/Base/Res"},{fixture+"/Maps"});
  dynamic_cast<COptBool*>(window.m_settings->opt("overrideWorldTime"))->setValue(true);dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->setValue(1240);
  auto* embedded=window.findChild<CRebuild64Viewport*>("rebuild64Viewport");auto* native=embedded->findChild<QWidget*>("rebuild64NativeSurface");check(embedded&&native&&!embedded->backendLoaded(),"GL default and lazy backend");
  window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1700,1100);window.show();QApplication::processEvents();
  quint64 frames=0;QObject::connect(embedded,&CRebuild64Viewport::frameCommitted,[&](quint64){++frames;});
  QObject::connect(embedded,&CRebuild64Viewport::backendFailure,[&](const QString& s){out<<"BACKEND_FAILURE "<<s<<'\n';out.flush();});
  auto open=[&](QString name){view->deferNeighborLoading(true);window.openMapPair(fixture+"/Maps/"+name+".mpr",fixture+"/Maps/"+name+".mob",name=="zone1"?"gz1g":"gz7g");check(wait([&]{return !view->hasPendingOperation()&&!window.backgroundMobLoading()&&view->hasActiveMob();},120000),"open "+name);view->deferNeighborLoading(true);window.m_minimapRefreshTimer->stop();view->m_freezeAnimations=true;view->m_animationFreezeTime=0;view->m_overlayAnimationTimer.invalidate();view->m_particleVisualsVisible=false;for(auto* mob:view->m_aMob)for(auto* n:mob->nodes())if(dynamic_cast<CUnit*>(n))n->setState(ENodeState::eHidden);embedded->setFixedSize(960,640);view->setFixedSize(960,640);QApplication::processEvents();check(view->ensureTerrainAutoCatalogReady(),"terrain model "+name);
   QMatrix4x4 camera;if(name=="zone1")camera.lookAt({240,110,175},{240,171,10},{0,0,1});else camera.lookAt({120,-100,190},{120,150,5},{0,0,1});view->m_cam->setPreviewView(camera);view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1.5,1,2000);view->m_projection=view->m_normalProjection;};
  auto ready=[&]{return embedded->lastFailure().isEmpty()&&embedded->statistics().ready_generation==embedded->m_generation&&embedded->statistics().ready_generation>0&&embedded->m_stack->currentWidget()!=view;};
  auto capture=[&](QString name){const auto path=root+"/"+name+".png";QFile::remove(path);embedded->capture(path);check(wait([&]{return !QImage(path).isNull();}),"own native readback "+name);};
  auto services=[&](QString name){const auto serial=embedded->statistics().service_serial;embedded->tick();check(wait([&]{return embedded->statistics().service_serial>serial;}),"fresh service accepted "+name);QString error;auto gl=view->captureEditorServiceLayer(QSize(qRound(960*embedded->devicePixelRatioF()),qRound(640*embedded->devicePixelRatioF())),&error);check(!gl.isNull()&&gl.save(root+"/"+name+"-gl-service.png"),"common GL service output "+name);check(wait([&]{return !embedded->m_serviceImage.isNull();}),"native service available");check(embedded->m_serviceImage.save(root+"/"+name+"-native-service.png"),"native service source output "+name);};
  if(app.arguments().contains("--recover")) {
   QFile expected(root+"/expected-autosave.mob");check(expected.open(QIODevice::ReadOnly),"recorded autosave expectation");const auto bytes=expected.readAll();
   const auto recoveries=CBackupManager::recoverableAutosaves();check(!recoveries.isEmpty(),"production autosave discovery after AV");bool found=false;
   for(const auto& r:recoveries){QFile payload(r.autosavePath);if(payload.open(QIODevice::ReadOnly)&&payload.readAll()==bytes){check(CBackupManager::validateAutosave(QFileInfo(r.autosavePath)),"autosave commit metadata validates");window.openMapPair(fixture+"/Maps/zone1.mpr",r.autosavePath,"gz1g");check(wait([&]{return view->hasActiveMob()&&!window.backgroundMobLoading();},120000),"open actual recovered autosave");check(mobBytes(view->m_activeMob)==bytes,"recovery restores exact autosaved object edit");found=true;break;}}
   check(found,"correct autosave generation found");check(!embedded->rebuildSelected()&&QFileInfo::exists(QDir(embedded->m_guardDirectory).filePath("manual-retry-required")),"AV session marker latches safe GL startup");view->unloadLand(false);EnumWindows(inspect,0);check(!visible,"recovery zero visible HWNDs");window.close();return 0;
  }
  QStringList maps=(app.arguments().contains("--smoke")||app.arguments().contains("--crash")||app.arguments().contains("--extras")||app.arguments().contains("--context")||app.arguments().contains("--visual-poses"))?QStringList{"zone1"}:QStringList{"zone1","zone7"};
  QStringList resourceBaseline;bool recordedResources=false;
  for(const auto& map:maps) {
   embedded->selectRebuild(false,false);open(map);const auto baseline=mobBytes(view->m_activeMob);view->m_tileGridVisible=true;
   if(app.arguments().contains("--gl")){embedded->setFixedSize(1280,720);view->setFixedSize(1280,720);QApplication::processEvents();view->m_tileGridVisible=false;view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,1280.f/720.f,1,2000);view->m_projection=view->m_normalProjection;captureGL(view,"gl-control");check(!embedded->backendLoaded(),"GL control no DLL");break;}
   out<<"GL_QUERY logical="<<view->width()<<'x'<<view->height()<<" query="<<view->m_width<<'x'<<view->m_height<<" projection="<<view->m_projection(0,0)<<','<<view->m_projection(1,1)<<','<<view->m_projection(2,2)<<','<<view->m_projection(2,3)<<'\n';out.flush();
   struct Pick {QPoint p;QList<CNode*> nodes;QList<CNode*> legacy;};QVector<Pick> picks;CNode* chosen=nullptr;QPoint chosenPoint;
   const auto legacyWidth=view->m_width,legacyHeight=view->m_height;
   auto referencePick=[&](QPoint point){view->m_width=legacyWidth;view->m_height=legacyHeight;click(view,point);const auto legacy=view->selectionSnapshot();view->m_width=view->width();view->m_height=view->height();click(view,point);const auto logical=view->selectionSnapshot();view->m_width=legacyWidth;view->m_height=legacyHeight;return Pick{point,logical,legacy};};
   // Object points plus viewport edges. All queries use real CView GL FBOs.
   for(auto* node:view->m_activeMob->nodes()) {
    if(node->nodeType()!=ENodeType::eWorldObject||node->nodeState()==ENodeState::eHidden)continue;
    auto box=node->getBBox();auto point=node->drawPosition()+(box.isInit()?box.center():QVector3D());auto projected=view->m_projection*view->m_cam->viewMatrix()*QVector4D(point,1);if(projected.w()<=0)continue;
    QPoint p(qRound((projected.x()/projected.w()+1)*480),qRound((1-projected.y()/projected.w())*320));if(!QRect(100,60,760,520).contains(p))continue;
    const auto sample=referencePick(p);auto selected=sample.nodes;if(selected.isEmpty())continue;picks.append(sample);if(!chosen){chosen=selected.first();chosenPoint=p;}if(picks.size()>=12)break;
   }
   check(chosen&&picks.size()>=3,"visible real object point set "+map);
   for(QPoint p:QVector<QPoint>{{0,0},{959,0},{0,639},{959,639},{950,320}}){picks.append(referencePick(p));}
   if(!recordedResources){resourceBaseline=ownResourceHandles286();recordedResources=true;}
   view->clearObjectSelection();embedded->selectRebuild(true,false);embedded->m_previewClock.invalidate();check(wait([&]{return ready()||!embedded->lastFailure().isEmpty();},180000)&&ready(),"native ready "+map);check(wait([&]{return !embedded->statistics().neighbours_pending;},180000),"neighbours settled "+map);
   if(app.arguments().contains("--layers")){
    QString error;auto original=view->captureTopdownPass(ETopdownPass::None,{960,640},{},&error);check(!original.isNull()&&original.save(root+"/ordinary-gl-grid.png"),"ordinary GL grid readback");
    services("shared-layer");capture("native-grid");
    CNode* editTarget=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==ENodeType::eWorldObject){editTarget=node;break;}check(editTarget,"refresh selection fence target");view->restoreSelection({editTarget});keyEvent(native,Qt::Key_G,'G');keyEvent(native,Qt::Key_X,'X');keyEvent(native,Qt::Key_1,'1');keyEvent(native,Qt::Key_Return,VK_RETURN);const auto selectionBeforeRefresh=view->selectionSnapshot();QString fenced;auto fence=QObject::connect(view,&CView::updateMsg,[&](const QString& text){if(text.contains(QString::fromUtf8("Дождитесь")))fenced=text;});click(native,{20,20});QObject::disconnect(fence);check(!fenced.isEmpty()&&view->selectionSnapshot()==selectionBeforeRefresh,"new mouse selection refused against stale confirmed-world frame");check(wait([&]{return !embedded->m_documentRefreshPending&&ready()&&!embedded->statistics().neighbours_pending;},120000),"refresh selection fence ends at ready world");
    QString message;auto connection=QObject::connect(view,&CView::updateMsg,[&](const QString& text){if(text.contains("OpenGL"))message=text;});CNode* unit=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==ENodeType::eUnit){unit=node;break;}check(unit,"unit selection diagnostic target");const auto state=unit->nodeState();view->restoreSelection({unit});check(!message.isEmpty(),"unit native visibility requires OpenGL message");unit->setState(state);view->restoreSelection({});QObject::disconnect(connection);
    embedded->selectRebuild(false,false);view->m_pUndoStack->setClean();view->unloadLand(false);EnumWindows(inspect,0);check(!visible,"layers zero visible HWNDs");window.close();return 0;
   }
   if(app.arguments().contains("--context")){
    testGroupMenu(view);const auto beforeVisibility=mobBytes(view->m_activeMob);auto visibilityGeneration=embedded->statistics().generation;
    view->setMobVisible(view->m_activeMob,false);check(wait([&]{return embedded->statistics().generation>visibilityGeneration&&ready();},120000),"hidden MOB cohort accepted without stale poses");visibilityGeneration=embedded->statistics().generation;
    view->setMobVisible(view->m_activeMob,true);check(wait([&]{return embedded->statistics().generation>visibilityGeneration&&ready();},120000),"restored MOB cohort accepted");check(mobBytes(view->m_activeMob)==beforeVisibility,"MOB visibility preserves exact data");
    embedded->selectRebuild(false,false);view->unloadLand(false);QTest::qWait(100);EnumWindows(inspect,0);check(!visible,"context zero visible HWNDs");window.close();return 0;
   }
   out<<"DPR "<<embedded->devicePixelRatioF()<<" query="<<view->m_width<<'x'<<view->m_height<<" native="<<embedded->statistics().width<<'x'<<embedded->statistics().height<<'\n';out.flush();
   check(view->m_width==960&&view->m_height==640,"hidden query logical size");if(qEnvironmentVariableIsSet("EI_EMBED_DPR"))check(qAbs(embedded->devicePixelRatioF()-1.5)<.01,"actual DPR1.5");
   if(app.arguments().contains("--visual-poses")) {
    CNode* house=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==ENodeType::eWorldObject&&node->modelName().startsWith("stbu")){auto p=view->m_projection*view->m_cam->viewMatrix()*QVector4D(node->drawPosition()+node->getBBox().center(),1);if(p.w()>0&&qAbs(p.x()/p.w())<.8&&qAbs(p.y()/p.w())<.8){house=node;break;}}
    check(house,"visible house for independent native pose readback");out<<"VISUAL_TARGET id="<<house->mapId()<<" model="<<house->modelName()<<'\n';out.flush();view->restoreSelection({house});
    auto rawPose=[&](QString label){const auto path=root+"/"+label+".png";QFile::remove(path);embedded->capture(path,true);check(wait([&]{return !QImage(path).isNull();}),"raw native pose "+label);return QImage(path).convertToFormat(QImage::Format_RGB32);};
    const auto originalBytes=mobBytes(view->m_activeMob);const auto a=rawPose("pose-baseline-a"),b=rawPose("pose-baseline-b");const auto documents=embedded->statistics().document_updates;
    keyEvent(native,Qt::Key_G,'G');keyEvent(native,Qt::Key_X,'X');keyEvent(native,Qt::Key_4,'4');keyEvent(native,Qt::Key_0,'0');const auto serial=embedded->statistics().service_serial;check(wait([&]{return embedded->statistics().service_serial>serial;}),"native pose accepted while pending");const auto moved=rawPose("pose-moved-pending");
    int changed=0;auto delta=[](QRgb x,QRgb y){return qMax(qAbs(qRed(x)-qRed(y)),qMax(qAbs(qGreen(x)-qGreen(y)),qAbs(qBlue(x)-qBlue(y))));};for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x)if(delta(b.pixel(x,y),moved.pixel(x,y))>qMax(4,delta(a.pixel(x,y),b.pixel(x,y))+3))++changed;
    out<<"VISUAL_PREVIEW unchanged_native_pixels_check changed="<<changed<<" document_updates="<<embedded->statistics().document_updates-documents<<'\n';out.flush();check(changed<500&&embedded->statistics().document_updates==documents,"pending transform is service preview without world reload");
    const auto committedGeneration=embedded->statistics().generation;QElapsedTimer commitClock;commitClock.start();keyEvent(native,Qt::Key_Return,VK_RETURN);view->clearObjectSelection();check(wait([&]{return embedded->statistics().generation>committedGeneration&&ready()&&!embedded->statistics().neighbours_pending;},120000),"committed native world accepted without selection");const auto accepted=rawPose("pose-moved-deselected");int worldChanged=0;for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x)if(delta(b.pixel(x,y),accepted.pixel(x,y))>qMax(4,delta(a.pixel(x,y),b.pixel(x,y))+3))++worldChanged;out<<"VISUAL_COMMIT changed_native_pixels="<<worldChanged<<" ms="<<commitClock.elapsed()<<'\n';out.flush();check(worldChanged>500&&worldChanged<a.width()*a.height()/5,"committed house changes a bounded native world area");int magenta=0;for(int y=0;y<accepted.height();++y)for(int x=0;x<accepted.width();++x){const auto c=accepted.pixel(x,y);if(qRed(c)>120&&qBlue(c)>120&&qGreen(c)<40)++magenta;}check(magenta<100,"confirmed world has no missing-material magenta field");const auto undoGeneration=embedded->statistics().generation;window.m_undoStack->undo();check(view->selectionSnapshot().contains(house),"Undo deselection first as in GL");window.m_undoStack->undo();check(mobBytes(view->m_activeMob)==originalBytes,"native pose Undo exact data");check(wait([&]{return embedded->statistics().generation>undoGeneration&&ready()&&!embedded->statistics().neighbours_pending;},120000),"native pose Undo accepted");const auto restored=rawPose("pose-undo");int undoPixels=0;for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x)if(delta(b.pixel(x,y),restored.pixel(x,y))>qMax(4,delta(a.pixel(x,y),b.pixel(x,y))+3))++undoPixels;out<<"VISUAL_UNDO changed_native_pixels="<<undoPixels<<'\n';check(undoPixels<a.width()*a.height()/30,"native Undo restores baseline world within temporal noise bound");
    embedded->selectRebuild(false,false);view->m_pUndoStack->setClean();view->unloadLand(false);EnumWindows(inspect,0);check(!visible,"pose zero visible HWNDs");window.close();return 0;
   }
   int legacyGaps=0;
   for(const auto& sample:picks){click(native,sample.p);if(sample.legacy!=view->selectionSnapshot()){++legacyGaps;out<<"LEGACY_GL_DPR_GAP "<<map<<" at="<<sample.p.x()<<','<<sample.p.y()<<" legacy=";for(auto* n:sample.legacy)out<<n->mapId()<<',';out<<" native=";for(auto* n:view->selectionSnapshot())out<<n->mapId()<<',';out<<'\n';out.flush();}check(view->selectionSnapshot()==sample.nodes,"native equals logical GL query at "+QString::number(sample.p.x())+","+QString::number(sample.p.y())+" "+map);}
   out<<"PICK_SUMMARY "<<map<<" samples="<<picks.size()<<" logical_mismatch=0 legacy_GL_mismatch="<<legacyGaps<<'\n';out.flush();
   // Measure the observable footprint discrepancy, without pretending RGB is
   // a renderer ID/depth API. Shadow/contact changes are included explicitly.
   view->clearObjectSelection();const auto worldSerial=embedded->statistics().service_serial;
   check(wait([&]{return embedded->statistics().service_serial>worldSerial;}),"cleared overlay accepted before world measurement");
   auto raw=[&](QString label){auto path=root+"/"+map+"-"+label+".png";QFile::remove(path);embedded->capture(path,true);check(wait([&]{return !QImage(path).isNull();}),"untouched renderer readback "+label);return QImage(path).convertToFormat(QImage::Format_RGB32);};
   const auto worldA=raw("raw-a"),worldB=raw("raw-b");
   view->pickObject(chosenPoint,false);auto ids=view->m_editorQueryFbo->toImage().convertToFormat(QImage::Format_RGB32);QImage mask(ids.size(),QImage::Format_Grayscale8);mask.fill(0);int glPixels=0;
   for(int y=0;y<ids.height();++y)for(int x=0;x<ids.width();++x){const auto pixel=ids.pixel(x,y);if(chosen->isColorSuitable(SColor(qRed(pixel),qGreen(pixel),qBlue(pixel)))){mask.scanLine(y)[x]=255;++glPixels;}}
   check(glPixels>0&&mask.save(root+"/"+map+"-gl-id-mask.png"),"GL query object footprint");view->clearObjectSelection();chosen->setState(ENodeState::eHidden);const auto hideSerial=embedded->statistics().generation;
   check(wait([&]{return embedded->statistics().generation>hideSerial&&ready()&&!embedded->statistics().neighbours_pending;},120000),"native object visibility update accepted");const auto hiddenWorld=raw("raw-object-hidden");
   int noisePixels=0,changedPixels=0,intersection=0,glPhysical=0;
   auto difference=[](QRgb a,QRgb b){return qMax(qAbs(qRed(a)-qRed(b)),qMax(qAbs(qGreen(a)-qGreen(b)),qAbs(qBlue(a)-qBlue(b))));};
   for(int y=0;y<worldB.height();++y)for(int x=0;x<worldB.width();++x){const auto noise=difference(worldA.pixel(x,y),worldB.pixel(x,y));const bool changed=difference(worldB.pixel(x,y),hiddenWorld.pixel(x,y))>qMax(3,noise+2);const bool gl=mask.constScanLine(y*mask.height()/worldB.height())[x*mask.width()/worldB.width()]!=0;if(noise>3)++noisePixels;if(gl)++glPhysical;if(changed)++changedPixels;if(changed&&gl)++intersection;}
   out<<"FOOTPRINT "<<map<<" id="<<chosen->mapId()<<" model="<<chosen->modelName()<<" gl_physical_px="<<glPhysical<<" native_change_px="<<changedPixels<<" intersection_px="<<intersection<<" noise_px="<<noisePixels<<" (includes native shadows/contact; not ID accuracy)"<<'\n';out.flush();
   chosen->setState(ENodeState::eDraw);const auto restoreSerial=embedded->statistics().generation;check(wait([&]{return embedded->statistics().generation>restoreSerial&&ready()&&!embedded->statistics().neighbours_pending;},120000),"native visibility restored");
   click(native,chosenPoint);view->pickObject(picks[1].p,true);const auto additive=view->selectionSnapshot();view->restoreSelection({chosen});
   QKeyEvent shiftDown(QEvent::KeyPress,Qt::Key_Shift,Qt::ShiftModifier,0,VK_SHIFT,0);QApplication::sendEvent(native,&shiftDown);click(native,picks[1].p,Qt::ShiftModifier);QKeyEvent shiftUp(QEvent::KeyRelease,Qt::Key_Shift,Qt::NoModifier,0,VK_SHIFT,0);QApplication::sendEvent(native,&shiftUp);check(view->selectionSnapshot()==additive,"Shift additive selection through native events");
   // Rectangle and modifier state go through the native surface.
   auto a=chosenPoint-QPoint(22,22),b=chosenPoint+QPoint(22,22);view->pickObject(QRect(a,b),false);const auto rectangle=view->selectionSnapshot();
   pointer(native,QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton);pointer(native,QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton);services(map+"-marquee");pointer(native,QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton);check(view->selectionSnapshot()==rectangle,"marquee selection "+map);
   click(native,chosenPoint);check(view->selectionSnapshot().contains(chosen),"choose tool target");const auto before=chosen->position();
   keyEvent(native,Qt::Key_G,'G');keyEvent(native,Qt::Key_X,'X');keyEvent(native,Qt::Key_5,'5');keyEvent(native,Qt::Key_Return,VK_RETURN);check(qAbs(chosen->position().x()-before.x()-5)<.001,"move X numeric shared operation "+map);
   if(app.arguments().contains("--crash")) {
    QByteArray expected;CValidationReport report;QString saveClass;check(view->m_activeMob->buildValidatedCandidate(&expected,&report,&saveClass),"validated edited MOB before autosave");
    QFile recorded(root+"/expected-autosave.mob");check(recorded.open(QIODevice::WriteOnly)&&recorded.write(expected)==expected.size(),"persist expected recovered bytes");recorded.close();
    window.performAutosave();check(!CBackupManager::recoverableAutosaves().isEmpty(),"production autosave after native object edit");
    keyEvent(native,Qt::Key_G,'G');keyEvent(native,Qt::Key_X,'X');keyEvent(native,Qt::Key_1,'1');keyEvent(native,Qt::Key_Return,VK_RETURN);
    check(mobBytes(view->m_activeMob)!=expected,"one completed edit after last autosave");
    SetUnhandledExceptionFilter(&ownTestFault);QFile trigger(qEnvironmentVariable("EI_TOOLS_FAULT_FILE"));check(trigger.open(QIODevice::WriteOnly)&&trigger.write("AV after autosave")>0,"arm actual test DLL access violation");trigger.close();
    check(wait([&]{return false;},20000),"DLL must access-violate this process");return 9;
   }
   const auto moved=mobBytes(view->m_activeMob);window.m_undoStack->undo();check((chosen->position()-before).length()<.001,"Undo object move");window.m_undoStack->redo();check(mobBytes(view->m_activeMob)==moved,"Redo exact bytes");
   keyEvent(native,Qt::Key_R,'R');keyEvent(native,Qt::Key_Z,'Z');keyEvent(native,Qt::Key_3,'3');keyEvent(native,Qt::Key_0,'0');keyEvent(native,Qt::Key_Return,VK_RETURN);check(chosen->rotation()!=QQuaternion(),"rotate shared operation");
   view->changeOperation(EButtonOpScale);keyEvent(native,Qt::Key_X,'X');keyEvent(native,Qt::Key_1,'1');keyEvent(native,Qt::Key_Return,VK_RETURN);check(!view->hasPendingOperation(),"scale shared operation committed");
   check(wait([&]{return !embedded->m_documentRefreshPending&&ready()&&!embedded->statistics().neighbours_pending;},120000),"earlier confirmed tools settled before drag sample");
   const auto dragDocument=embedded->statistics().document_updates;const auto dragSerial=embedded->statistics().service_serial;
   keyEvent(native,Qt::Key_G,'G');keyEvent(native,Qt::Key_X,'X');
   QVector<double> response;QPoint drag=chosenPoint;
   for(int i=1;i<=4;++i){QElapsedTimer clock;clock.start();const auto serial=embedded->statistics().service_serial;pointer(native,QEvent::MouseMove,drag+QPoint(8*i,0),Qt::NoButton,Qt::NoButton);embedded->tick();check(wait([&]{return embedded->statistics().service_serial>serial;}),"accepted service preview during drag");response.append(clock.nsecsElapsed()/1e6);}
   check(embedded->statistics().document_updates==dragDocument,"no document reload during pointer preview");
   QElapsedTimer release;release.start();const auto releaseGeneration=embedded->statistics().generation;click(native,drag+QPoint(32,0));check(wait([&]{return embedded->statistics().generation>releaseGeneration&&ready()&&!embedded->statistics().neighbours_pending;},120000),"accepted confirmed world after release");
   out<<"RESPONSE "<<map<<" drag_ms=";for(auto value:response)out<<value<<',';out<<" release_ms="<<release.nsecsElapsed()/1e6<<" confirmed_document_updates="<<embedded->statistics().document_updates-dragDocument<<" serial_start="<<dragSerial<<'\n';out.flush();
   window.m_undoStack->undo();
   check(wait([&]{return !embedded->m_documentRefreshPending&&ready()&&!embedded->statistics().neighbours_pending;},120000),"confirmed document settled before capture");services(map+"-selected");capture(map+"-selected");
   const auto shortcutBytes=mobBytes(view->m_activeMob);const auto shortcutIndex=window.m_undoStack->index();
   auto* undoAction=window.findChild<QAction*>("actionUndo");auto* redoAction=window.findChild<QAction*>("actionRedo");check(undoAction&&redoAction,"canonical Undo/Redo actions exist");
   QShortcutEvent undoShortcut(undoAction->shortcut(),0,false);QApplication::sendEvent(undoAction,&undoShortcut);check(window.m_undoStack->index()==shortcutIndex-1,"Qt Undo shortcut dispatch in native view");QShortcutEvent redoShortcut(redoAction->shortcut(),0,false);QApplication::sendEvent(redoAction,&redoShortcut);check(window.m_undoStack->index()==shortcutIndex&&mobBytes(view->m_activeMob)==shortcutBytes,"Qt Redo shortcut dispatch exact bytes");
   const auto selected=view->selectionSnapshot();embedded->selectRebuild(false,false);check(view->selectionSnapshot()==selected,"selection survives GL switch");embedded->selectRebuild(true,false);check(wait([&]{return ready()||!embedded->lastFailure().isEmpty();},180000)&&ready()&&view->selectionSnapshot()==selected,"selection survives native switch");
   // Production journaled Save to a new owned copy and strict reopen.
   const auto edited=mobBytes(view->m_activeMob);const auto saved=root+"/edited-"+map+".mob";check(view->saveMobDocument(view->m_activeMob,QFileInfo(saved)),"save edited copy");QFile file(saved);check(file.open(QIODevice::ReadOnly),"read saved copy");auto savedBytes=file.readAll();CMob reopened;check(reopened.deserialize(savedBytes),"strict reopen saved copy");check(mobBytes(&reopened)==edited,"reopen equals canonical edit bytes");
   check(QDir::fromNativeSeparators(view->m_clipboard_buffer_file.fileName()).startsWith(root+"/tmp/"),"object copy buffer isolated under own TEMP");
   keyEvent(native,Qt::Key_Control,VK_CONTROL);view->m_pOp->keyManager()->press(eKey_Ctrl);keyEvent(native,Qt::Key_C,'C',Qt::ControlModifier);view->m_pOp->keyManager()->release(eKey_Ctrl);
   check(QFileInfo(view->m_clipboard_buffer_file.fileName()).size()>0,"copy native hotkey writes only isolated file buffer");
   const auto count=view->m_activeMob->nodes().size();view->m_pOp->keyManager()->press(eKey_Ctrl);keyEvent(native,Qt::Key_V,'V',Qt::ControlModifier);view->m_pOp->keyManager()->release(eKey_Ctrl);
   check(view->hasPendingOperation()&&view->m_activeMob->nodes().size()>count,"paste begins shared placement preview");
   keyEvent(native,Qt::Key_Escape,VK_ESCAPE);check(!view->hasPendingOperation()&&view->m_activeMob->nodes().size()==count,"Escape cancels pasted preview without losing edits");
   view->restoreSelection({chosen});
   if(app.arguments().contains("--extras")) {
    const auto baseCount=view->m_activeMob->nodes().size();const auto insertGeneration=embedded->statistics().generation;
    view->m_pOp->keyManager()->press(eKey_Ctrl);keyEvent(native,Qt::Key_V,'V',Qt::ControlModifier);view->m_pOp->keyManager()->release(eKey_Ctrl);
    pointer(native,QEvent::MouseMove,chosenPoint+QPoint(30,10),Qt::NoButton,Qt::NoButton);QElapsedTimer insert;insert.start();click(native,chosenPoint+QPoint(30,10));
    check(!view->hasPendingOperation()&&view->m_activeMob->nodes().size()==baseCount+1,"paste confirms shared object placement");
    check(wait([&]{return embedded->statistics().generation>insertGeneration&&ready();},120000),"inserted object reaches accepted native scene");out<<"STRUCTURAL insert_ms="<<insert.elapsed()<<'\n';out.flush();
    const auto undoGeneration=embedded->statistics().generation;window.m_undoStack->undo();check(view->m_activeMob->nodes().size()==baseCount&&mobBytes(view->m_activeMob)==edited,"Undo insertion preserves exact prior MOB");check(wait([&]{return embedded->statistics().generation>undoGeneration&&ready();},120000),"Undo insertion reaches accepted scene");
    testGroupMenu(view);
   }
   // Delete and restore through the same operation/Undo owner.
   const auto chosenId=chosen->mapId();const auto deleteCount=view->m_activeMob->nodes().size();keyEvent(native,Qt::Key_Delete,VK_DELETE);check(!view->m_activeMob->nodes().contains(chosen)&&view->m_activeMob->nodes().size()==deleteCount-1,"Delete native hotkey");window.m_undoStack->undo();chosen=view->m_activeMob->nodeByMapId(chosenId);check(chosen&&view->m_activeMob->nodes().size()==deleteCount,"Undo Delete");
   const auto terrainBefore=view->m_terrainEdit.stateSha256();const auto brushMob=mobBytes(view->m_activeMob);QString refusal;
   auto refused=QObject::connect(view,&CView::updateMsg,[&](const QString& message){if(message.contains("OpenGL"))refusal=message;});
   view->m_landscapeTools->setEditingEnabled(true,false);click(native,{480,320});view->m_landscapeTools->setEditingEnabled(false,false);QObject::disconnect(refused);
   check(!refusal.isEmpty()&&embedded->rebuildSelected()&&view->m_terrainEdit.stateSha256()==terrainBefore&&mobBytes(view->m_activeMob)==brushMob,"terrain brush explicit refusal with no data change or switch");
   check(wait([&]{return ready()&&!embedded->statistics().pending&&!embedded->statistics().neighbours_pending;}),"resident scene settled before service sample");
   QVector<double> service,frame,composite;quint64 sampledService=embedded->statistics().service_serial;
   check(wait([&]{const auto stats=embedded->statistics();if(ready()&&stats.service_serial>sampledService){sampledService=stats.service_serial;service.append(stats.service_cpu_ms);frame.append(stats.frame_ms);composite.append(stats.service_composite_ms);}return service.size()>=25;},60000),"25 distinct accepted service samples");
   std::sort(service.begin(),service.end());std::sort(frame.begin(),frame.end());std::sort(composite.begin(),composite.end());out<<"MEASURE "<<map<<" n="<<service.size()<<" service_median_ms="<<service[service.size()/2]<<" service_p95_ms="<<service[int(service.size()*.95)]<<" composite_median_ms="<<composite[composite.size()/2]<<" composite_p95_ms="<<composite[int(composite.size()*.95)]<<" frame_median_ms="<<frame[frame.size()/2]<<" frame_p95_ms="<<frame[int(frame.size()*.95)]<<'\n';out.flush();
   const auto fixedCamera=view->m_cam->viewMatrix();view->m_cam->clearPreviewView();const auto cameraBefore=view->m_cam->viewMatrix();
   QKeyEvent forwardDown(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier,0,'W',0);QApplication::sendEvent(native,&forwardDown);QTest::qWait(120);QKeyEvent forwardUp(QEvent::KeyRelease,Qt::Key_W,Qt::NoModifier,0,'W',0);QApplication::sendEvent(native,&forwardUp);check(view->m_cam->viewMatrix()!=cameraBefore,"W camera key shares CView path");
   const auto beforeOrbit=view->m_cam->viewMatrix();pointer(native,QEvent::MouseButtonPress,{480,320},Qt::MiddleButton,Qt::MiddleButton);pointer(native,QEvent::MouseMove,{500,330},Qt::NoButton,Qt::MiddleButton);pointer(native,QEvent::MouseButtonRelease,{500,330},Qt::MiddleButton,Qt::NoButton);embedded->tick();check(view->m_cam->viewMatrix()!=beforeOrbit,"middle-button camera shares CView path");
   const auto beforeWheel=view->m_cam->viewMatrix();QWheelEvent wheel(QPointF(480,320),QPointF(native->mapToGlobal({480,320})),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(native,&wheel);embedded->tick();check(view->m_cam->viewMatrix()!=beforeWheel,"wheel camera shares CView path");view->m_cam->setPreviewView(fixedCamera);
   embedded->selectRebuild(false,false);view->cancelPendingOperation();view->m_pUndoStack->setClean();view->m_pLand->setDirty(false);
   open(map);view->restoreSelection({view->m_activeMob->nodeByMapId(chosenId)});keyEvent(view,Qt::Key_G,'G');keyEvent(view,Qt::Key_X,'X');keyEvent(view,Qt::Key_5,'5');keyEvent(view,Qt::Key_Return,VK_RETURN);
   keyEvent(view,Qt::Key_R,'R');keyEvent(view,Qt::Key_Z,'Z');keyEvent(view,Qt::Key_3,'3');keyEvent(view,Qt::Key_0,'0');keyEvent(view,Qt::Key_Return,VK_RETURN);
   view->changeOperation(EButtonOpScale);keyEvent(view,Qt::Key_X,'X');keyEvent(view,Qt::Key_1,'1');keyEvent(view,Qt::Key_Return,VK_RETURN);
   check(mobBytes(view->m_activeMob)==edited,"same real OpenGL edits produce byte-identical MOB");
   view->m_pUndoStack->setClean();view->m_pLand->setDirty(false);
  }
  view->unloadLand(false);QTest::qWait(100);if(recordedResources)check(wait([&]{auto extra=ownResourceHandles286();for(const auto& path:resourceBaseline)extra.removeOne(path);return extra.isEmpty();},60000),"native map/resource handles retired after close");EnumWindows(inspect,0);check(!visible,"zero visible HWNDs");window.close();out<<"PASS tools286 checks="<<passes<<'\n';out.flush();return 0;
 }catch(const QString& e){out<<"FAIL "<<e<<'\n';out.flush();return 1;}
}

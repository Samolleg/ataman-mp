#include "resource_browser.h"
#include "res_archive_index.h"
#include "mmp_inspector.h"
#include "gallery_thumbnail_cache.h"
#include "minimap_export_dialog.h"
#include "createobjectform.h"
#include "item_spell_constructor_dialog.h"
#include "model_gallery.h"
#include "native_output.h"
#include "topdown_export.h"
#include "preview.h"
#include "opening_cpu_value.h"
#include "operationmanager.h"
#include "key_manager.h"
#include "rebuild64_launch_check.h"
#include "main_window.h"
#include "view.h"
#include "rebuild64_viewport_widget.h"
#include "rebuild64_startup_guard.h"
#include "ai_agent_server.h"
#include "ai_agent_edit.h"
#include "ai_agent_read.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "camera.h"
#include "settings.h"
#include "resource_setup_dialog.h"
#include "maps_browser.h"
#include "backup_manager.h"
#include "options.h"
#include "resource_write_lease.h"
#include "map_save_batch.h"
#include "profile_database.h"
#include "endurance_document.h"
#include "legacy_projection_pulse.h"
#include "resourcemanager.h"
#include <atomic>
#include <QtWidgets>
#include <QtNetwork>
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include "isolated_resource_handles.h"
#include <cstring>
#include <GL/gl.h>
#include <functional>

namespace {
// Patched entry points can run from late Qt/static destructors after main.
// Keep diagnostic isolation state alive until the OS reclaims this process.
QString& caseRoot=*new QString;QString& knownFolderRoot=*new QString;
QJsonObject& configuration=*new QJsonObject;
bool isolatedSharedCache=false;
QString isolatedWritable(QStandardPaths::StandardLocation location)
{return location==QStandardPaths::CacheLocation&&isolatedSharedCache?knownFolderRoot+"/qt-cache":caseRoot+"/known-folders/"+QString::number(int(location));}
HRESULT WINAPI isolatedKnownFolder(REFKNOWNFOLDERID id,DWORD,HANDLE,PWSTR* result)
{
    if(!result)return E_POINTER;
    wchar_t guid[64]{};StringFromGUID2(id,guid,64);
    const QString path=knownFolderRoot+"/known-win/"+QString::fromWCharArray(guid);
    QDir().mkpath(path);const auto native=QDir::toNativeSeparators(path);
    *result=static_cast<PWSTR>(CoTaskMemAlloc(size_t(native.size()+1)*sizeof(wchar_t)));
    if(!*result)return E_OUTOFMEMORY;
    std::memcpy(*result,native.utf16(),size_t(native.size()+1)*sizeof(wchar_t));return S_OK;
}
bool processLocalJump(void* entry,void* target)
{
    if(!entry)return false;
    unsigned char jump[12]={0x48,0xb8};const auto address=reinterpret_cast<quint64>(target);
    std::memcpy(jump+2,&address,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old=0;if(!VirtualProtect(entry,12,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(entry,jump,12);FlushInstructionCache(GetCurrentProcess(),entry,12);
    DWORD ignored=0;return VirtualProtect(entry,12,old,&ignored)!=0;
}
QString fileHash(const QString& path)
{
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}
qint64 qpc(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
void glPresented(HDC dc,qint64 begin,qint64 end,BOOL ok);
using SwapBuffersFunction=BOOL (WINAPI*)(HDC);
SwapBuffersFunction originalSwap=nullptr,originalWglSwap=nullptr;
BOOL WINAPI measuredSwap(HDC dc){const auto begin=qpc();const auto ok=originalSwap(dc);const auto end=qpc();glPresented(dc,begin,end,ok);return ok;}
BOOL WINAPI measuredWglSwap(HDC dc){const auto begin=qpc();const auto ok=originalWglSwap(dc);const auto end=qpc();glPresented(dc,begin,end,ok);return ok;}
bool installSwapTrace(){
 bool installed=false;
 for(const auto* moduleName:{L"qwindows.dll",L"Qt5OpenGL.dll",L"Qt5Gui.dll",L"Qt5Widgets.dll"}) {
  auto module=reinterpret_cast<unsigned char*>(GetModuleHandleW(moduleName));if(!module)continue;
  auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(module+dos->e_lfanew);
  auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
  for(;descriptor->Name;++descriptor){if(!descriptor->OriginalFirstThunk)continue;
   auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(module+descriptor->OriginalFirstThunk);auto* importSlots=reinterpret_cast<IMAGE_THUNK_DATA64*>(module+descriptor->FirstThunk);
   for(;names->u1.AddressOfData;++names,++importSlots){if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
    auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(module+names->u1.AddressOfData);
    const bool gdi=std::strcmp(reinterpret_cast<const char*>(name->Name),"SwapBuffers")==0;
    const bool wgl=std::strcmp(reinterpret_cast<const char*>(name->Name),"wglSwapBuffers")==0;
    if(!gdi&&!wgl)continue;DWORD old=0;if(!VirtualProtect(&importSlots->u1.Function,sizeof(void*),PAGE_READWRITE,&old))continue;
    if(gdi){if(!originalSwap)originalSwap=reinterpret_cast<SwapBuffersFunction>(importSlots->u1.Function);importSlots->u1.Function=reinterpret_cast<ULONG_PTR>(&measuredSwap);}
    else {if(!originalWglSwap)originalWglSwap=reinterpret_cast<SwapBuffersFunction>(importSlots->u1.Function);importSlots->u1.Function=reinterpret_cast<ULONG_PTR>(&measuredWglSwap);}
    DWORD ignored=0;VirtualProtect(&importSlots->u1.Function,sizeof(void*),old,&ignored);installed=true;
   }
  }
 }
 return installed;
}

std::atomic<int> deniedReads{0};
std::atomic<int> deniedWrites{0};QString& writableRoot=*new QString;QString& writableShortRoot=*new QString;QString& allowedNamedPipe=*new QString;
bool allowedWrite(LPCWSTR file) {
    if(!file)return false;const auto literal=QDir::fromNativeSeparators(QString::fromWCharArray(file));
    if(literal.startsWith("//./pipe/",Qt::CaseInsensitive)&&!allowedNamedPipe.isEmpty()&&literal.endsWith(allowedNamedPipe,Qt::CaseInsensitive))return true;
    if(literal.startsWith("//./pipe/qt-"+QString::number(GetCurrentProcessId(),16)+"-",Qt::CaseInsensitive))return true; // QProcess pipes owned by this isolated process
    wchar_t full[32768]{};if(!GetFullPathNameW(file,32768,full,nullptr))return false;
    QString path=QDir::fromNativeSeparators(QString::fromWCharArray(full));if(path.startsWith("//?/"))path.remove(0,4);
    return path.startsWith(writableRoot+"/",Qt::CaseInsensitive)||path.startsWith(writableShortRoot+"/",Qt::CaseInsensitive);
}
bool denyWrite(LPCWSTR file) {if(allowedWrite(file))return false;++deniedWrites;std::fwprintf(stderr,L"ISOLATION denied write: %ls\n",file?file:L"<null>");SetLastError(ERROR_ACCESS_DENIED);return true;}
using CreateFileFn=HANDLE(WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using MoveFileFn=BOOL(WINAPI*)(LPCWSTR,LPCWSTR,DWORD);
using MoveSimpleFn=BOOL(WINAPI*)(LPCWSTR,LPCWSTR);
MoveSimpleFn originalMoveSimple=nullptr;
using DeleteFileFn=BOOL(WINAPI*)(LPCWSTR);
using ReplaceFileFn=BOOL(WINAPI*)(LPCWSTR,LPCWSTR,LPCWSTR,DWORD,LPVOID,LPVOID);
CreateFileFn originalCreateFile=nullptr;MoveFileFn originalMoveFile=nullptr;DeleteFileFn originalDeleteFile=nullptr;ReplaceFileFn originalReplaceFile=nullptr;
HANDLE WINAPI guardedCreate(LPCWSTR file,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templateFile) {
    if(file){auto path=QDir::fromNativeSeparators(QString::fromWCharArray(file));if(path.startsWith("//?/"))path.remove(0,4);
        const auto root=QDir::fromNativeSeparators(configuration.value("forbidLiveModRoot").toString());
        if(!root.isEmpty()&&path.startsWith(root+"/",Qt::CaseInsensitive)&&(path.endsWith(".res",Qt::CaseInsensitive)||path.endsWith(".mpr",Qt::CaseInsensitive)||path.endsWith(".mob",Qt::CaseInsensitive))){++deniedReads;std::fwprintf(stderr,L"ISOLATION denied live resource read: %ls\n",file);SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}}

    if((access&(GENERIC_WRITE|DELETE|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|WRITE_DAC|WRITE_OWNER))||creation!=OPEN_EXISTING)
        if(denyWrite(file))return INVALID_HANDLE_VALUE;
    return originalCreateFile(file,access,share,security,creation,flags,templateFile);
}
BOOL WINAPI guardedMove(LPCWSTR from,LPCWSTR to,DWORD flags){if(denyWrite(from)||denyWrite(to))return FALSE;return originalMoveFile(from,to,flags);}
BOOL WINAPI guardedMoveSimple(LPCWSTR from,LPCWSTR to){if(denyWrite(from)||denyWrite(to))return FALSE;return originalMoveSimple(from,to);}
BOOL WINAPI guardedDelete(LPCWSTR file){return denyWrite(file)?FALSE:originalDeleteFile(file);}
BOOL WINAPI guardedReplace(LPCWSTR to,LPCWSTR from,LPCWSTR backup,DWORD flags,LPVOID a,LPVOID b){if(denyWrite(to)||denyWrite(from)||(backup&&denyWrite(backup)))return FALSE;return originalReplaceFile(to,from,backup,flags,a,b);}
bool patchCoreImport(const char* target,void* replacement,void** original) {
 auto* module=reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Qt5Core.dll"));if(!module)return false;
 auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(module+dos->e_lfanew);
 auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
 for(;descriptor->Name;++descriptor){if(!descriptor->OriginalFirstThunk)continue;
  auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(module+descriptor->OriginalFirstThunk);auto* entries=reinterpret_cast<IMAGE_THUNK_DATA64*>(module+descriptor->FirstThunk);
  for(;names->u1.AddressOfData;++names,++entries){if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
   auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(module+names->u1.AddressOfData);if(std::strcmp(reinterpret_cast<const char*>(name->Name),target))continue;
   DWORD old=0;if(!VirtualProtect(&entries->u1.Function,sizeof(void*),PAGE_READWRITE,&old))return false;
   *original=reinterpret_cast<void*>(entries->u1.Function);entries->u1.Function=reinterpret_cast<ULONG_PTR>(replacement);DWORD ignored=0;VirtualProtect(&entries->u1.Function,sizeof(void*),old,&ignored);return true;
  }
 }return false;
}
int ownVisibleWindows=0;
BOOL CALLBACK ownWindow(HWND window,LPARAM)
{
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId()&&IsWindowVisible(window))++ownVisibleWindows;
    return TRUE;
}
}

// Compiled into the real executable, like the existing widget checks. It does
// not replace main or MainWindow. Activated only by an explicit private case.
class CRebuild64LaunchCheck : public QObject {
public:
    explicit CRebuild64LaunchCheck(QApplication& app):QObject(&app)
    {
        log.setFileName(caseRoot+"/events.jsonl");log.open(QIODevice::WriteOnly|QIODevice::Append);
        app.installEventFilter(this);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const QString guard=QDir(isolatedWritable(QStandardPaths::CacheLocation)).filePath("rebuild64");
        if(configuration["guard"].toString()=="latch") {QDir().mkpath(guard);QFile f(guard+"/manual-retry-required");f.open(QIODevice::WriteOnly);f.write("isolated diagnostic\n");}
        if(configuration["guard"].toString()=="interrupted") {
            QDir().mkpath(guard);QFile f(guard+"/startup-999999-isolated.json");f.open(QIODevice::WriteOnly);
            f.write(QJsonDocument(QJsonObject{{"schema",1},{"pid","999999"},{"birth","invalid"}}).toJson());
        }
        swapTraceInstalled=installSwapTrace();
        lastPulse=qpc();pulse.setInterval(10);connect(&pulse,&QTimer::timeout,this,[this]{const auto now=qpc();const auto gap=now-lastPulse;maxGap=qMax(maxGap,gap);if(gap>500000)record("qt_gap",{{"beginQpc",QString::number(lastPulse)},{"ticks",QString::number(gap)}});lastPulse=now;});pulse.start();
        record("isolation",{{"root",caseRoot},{"mainUnchanged",true},{"knownFoldersRedirected",true},{"settings",qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR")}});
    }
    void attach(MainWindow& value)
    {
        window=&value;view=value.view();native=value.findChild<CRebuild64Viewport*>();
        native->setFixedSize(1920,1080);
        window->resize(2560,1440);
        connect(view,&CView::updateMsg,this,[this](const QString& text){record("message",{{"text",text}});});
        connect(native,&CRebuild64Viewport::backendFailure,this,[this](const QString& text){++failures;record("backend_failure",{{"text",text}});});
        connect(native,&CRebuild64Viewport::documentTransitionStarted,this,[this](quint64 generation){record("transition_begin",{{"retiredBefore",QString::number(generation)}});});
        connect(native,&CRebuild64Viewport::frameCommitted,this,[this](quint64 generation){
            if(generation==lastGeneration)return;
            lastGeneration=generation;
            const auto s=native->statistics();
            if(liveWanted&&!liveSent){liveSent=true;QTimer::singleShot(0,this,[this]{liveCameraEvents();});}
            if(configuration["earlyInput"].toBool()&&!earlyInputSent&&native->openingDocument()) {
                earlyInputSent=true;earlyInputQpc=qpc();earlyUndoIndex=window->m_undoStack->index();
                auto* target=native->findChild<QWidget*>("rebuild64NativeSurface");const auto point=target->rect().center();
                QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(target,&press);QApplication::sendEvent(target,&release);
                key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
                record("early_input_queued",{{"queryReady",native->editorQueryReady()},{"pendingInput",native->pendingEditorInput()},{"undoUnchanged",window->m_undoStack->index()==earlyUndoIndex},{"targetNid",int(earlyTargetNid)}});
            }
            QSaveFile shown(caseRoot+"/qt-page-shown-"+QString::number(generation)+".json");if(shown.open(QIODevice::WriteOnly)){shown.write(QJsonDocument(QJsonObject{{"renderer","Rebuild64"},{"neighbours_accepted",int(s.neighbours_accepted)},{"exeSha256",exeSha},{"dllSha256",dllSha},{"qpc",QString::number(qpc())}}).toJson());shown.commit();}
            const auto actualMap=native->openingDocument()?native->m_earlyMap:CLandscape::getInstance()->filePath().absoluteFilePath();
            const int expected=QFileInfo(actualMap).completeBaseName()=="zone1"?5:QFileInfo(actualMap).completeBaseName()=="zone7"?4:0;
            if(int(s.neighbours_accepted)!=expected||s.neighbours_pending)record("FAIL",{{"check","first displayed frame neighbour cohort"}});
            record("first_frame",{{"expectedNeighbours",expected},{"cohortValid",int(s.neighbours_accepted)==expected&&!s.neighbours_pending},{"generation",QString::number(generation)},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"map",CLandscape::getInstance()->filePath().absoluteFilePath()},
                {"neighbours_accepted",int(s.neighbours_accepted)},{"neighbours_pending",int(s.neighbours_pending)},
                {"commandQpc",QString::number(commandQpc)},{"exeSha256",exeSha},{"dllSha256",dllSha}});
        });
        exeSha=fileHash(QCoreApplication::applicationFilePath());dllSha=fileHash(QDir(QCoreApplication::applicationDirPath()).filePath("ataman_rebuild64.dll"));
    }
    void windowReady(){record("interactive",{{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"exeSha256",exeSha},{"dllSha256",dllSha}});QTimer::singleShot(100,this,[this]{startWhenIdle();});}
    void swapped(HDC dc,qint64 begin,qint64 end,BOOL ok){
        if(!window||!view)return;
        if(WindowFromDC(dc)!=reinterpret_cast<HWND>(view->internalWinId())&&QGLContext::currentContext()!=view->context())return;
        const auto path=CLandscape::getInstance()->filePath().absoluteFilePath();
        const bool ready=CLandscape::getInstance()->isMprLoad()&&window->rebuild64DocumentReady()&&!view->m_neighborLoadStage&&!view->m_neighborLoadJobs&&!view->m_neighborObjectUploadScheduled;
        record("gl_present",{{"beginQpc",QString::number(begin)},{"endQpc",QString::number(end)},{"ok",bool(ok)},{"map",path},{"complete",ready},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"neighbours_accepted",view->m_neighborLandscapes.size()},{"exeSha256",exeSha},{"dllSha256",dllSha}});
        if(ready&&ok&&!glCaptured.contains(path)){
            glCaptured.insert(path);GLint previous=0;::glGetIntegerv(GL_READ_BUFFER,&previous);::glReadBuffer(GL_FRONT);
            QImage image(qMax(1,view->width()),qMax(1,view->height()),QImage::Format_RGBA8888);
            ::glReadPixels(0,0,image.width(),image.height(),GL_RGBA,GL_UNSIGNED_BYTE,image.bits());::glReadBuffer(GLenum(previous));
            image.mirrored().save(caseRoot+"/stream-gl-"+QFileInfo(path).completeBaseName()+".png");
        }
    }
    void record(const QString& event,QJsonObject value={})
    {
        value["event"]=event;value["qpc"]=QString::number(qpc());
        const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n';log.write(bytes);log.flush();
    }
    void check(bool ok,const QString& what)
    {
        record(ok?"PASS":"FAIL",{{"check",what}});
        std::fprintf(stdout,"%s %s\n",ok?"PASS":"FAIL",what.toUtf8().constData());std::fflush(stdout);
        if(!ok)throw what;
    }
    bool wait(const std::function<bool()>& ready,int timeout=240000)
    {
        QElapsedTimer time;time.start();
        for(;;){if(ready())return true;if(time.elapsed()>=timeout)return false;QCoreApplication::processEvents(QEventLoop::AllEvents,10);Sleep(1);}
    }
    QString fixture()const{return configuration["fixture"].toString();}
    void auditOpenResources() {
        if(!configuration["fullyFrozenResources"].toBool())return;
        const auto audit=ei::openingCpuValue(true,[]{bool observed=false;auto paths=isolatedOpenResourceHandles(observed);return qMakePair(observed,paths);});
        check(audit.first,"own process resource handles enumerated");const auto& paths=audit.second;
        QJsonArray list;const auto live=QDir::fromNativeSeparators(configuration["forbidLiveModRoot"].toString());
        for(const auto& path:paths){list.append(path);check(!path.startsWith(live+"/",Qt::CaseInsensitive),"no live mod archive/map handle: "+path);}
        record("resource_handles",{{"paths",list},{"deniedLiveReads",deniedReads.load()},{"liveModHandles",0}});
        check(deniedReads.load()==0,"no attempted live mod resource opens");
    }
    void validateReady(const QString& name,bool gl=false)
    {
        if(gl) {
            check(wait([&]{return window->rebuild64DocumentReady()&&view->m_neighborLoadStage==0&&view->m_neighborLoadJobs==0&&!view->m_neighborObjectUploadScheduled;}),"GL load settled");
            check(!native->rebuildSelected(),"OpenGL selected");sawGL=true;
            int touching=0;for(const auto& item:view->m_neighborLandscapes)if(item.landscape&&item.landscape->isMprLoad()&&CNeighborMapLayout::touchesActiveMap({view->m_pLand->widthInWorldUnits(),view->m_pLand->heightInWorldUnits()},item.placement))++touching;
            const int expected=name.contains("zone7")?4:name.contains("zone1")?5:0;check(touching==expected,"GL touching neighbours exact "+QString::number(expected));

            record("ready",{{"name",name},{"renderer","OpenGL"},{"neighbours_accepted",touching},{"legacyNeighbours",view->m_neighborLandscapes.size()},{"neighbourSource","CView/GL"},{"nativeLoaded",native->backendLoaded()},{"exeSha256",exeSha},{"dllSha256",dllSha}});
            auditOpenResources();QString error;auto image=view->captureTopdownPass(ETopdownPass::None,QSize(960,640),{},&error);
            check(!image.isNull(),"GL own FBO readback: "+error);image.save(caseRoot+"/"+name+"-gl.png");
            return;
        }
        check(wait([&]{return native->frameIsCurrent()||!native->lastFailure().isEmpty();}),"native load reached terminal state");
        check(native->rebuildSelected(),"Rebuild64 selected: "+native->lastFailure());
        const int expected=name.contains("zone7")?4:name.contains("zone1")?5:0;
        const auto stats=native->statistics();
        check(int(stats.neighbours_accepted)==expected&&stats.neighbours_pending==0,"native neighbours exact "+QString::number(expected));
        if(!sawGL) {
            check(wait([&]{return window->rebuild64DocumentReady()&&!native->openingDocument()&&native->editorQueryReady()&&native->frameIsCurrent();}),"shared query geometry ready (later than first world allowed)");
            check(view->m_neighborLandscapes.size()==expected,"only touching GL neighbours loaded");
        }
        record("ready",{{"name",name},{"renderer","Rebuild64"},{"neighbours_accepted",int(stats.neighbours_accepted)},{"legacyNeighbours",view->m_neighborLandscapes.size()},{"width",int(stats.width)},{"height",int(stats.height)},{"exeSha256",exeSha},{"dllSha256",dllSha}});
        auditOpenResources();check(wait([&]{return native->frameIsCurrent();}),"snapshot uses latest ready generation after observers");const auto path=caseRoot+"/"+name+"-native.png";QFile::remove(path);bool done=false;QString error;quint64 captured=0;
        const auto connection=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& output,const QString& failure,quint64 generation){if(output==path){done=true;error=failure;captured=generation;}});
        native->capture(path);const bool finished=wait([&]{return done;},15000);disconnect(connection);
        check(finished&&error.isEmpty()&&captured==native->statistics().ready_generation&&QFileInfo(path).size()>0,"fresh native own readback from current Present: "+error);
    }
    void openPair(const QString& map)
    {
        if(configuration["benchmarkCamera"].toBool())view->m_cam->setLocation({256,128,200},{256,128,0},{0,0,0});
        commandQpc=qpc();lastGeneration=0;
        window->openMapPair(fixture()+"/Maps/"+map+".mpr",fixture()+"/Maps/"+map+".mob",map);
    }
    void action(const QString& objectName) {
        auto* found=window->findChild<QAction*>(objectName);check(found,"real QAction "+objectName);found->trigger();
    }
    void openMenu(const QString& map,bool mprOnly=false) {
        commandQpc=qpc();nextFile=fixture()+"/Maps/"+map+".mpr";
        action(mprOnly?"actionOpen_MPR":"actionOpen");
        check(nextFile.isEmpty(),"real file dialog consumed own path");validateReady(map);
    }
    void openRecent(const QString& map) {
        window->rememberRecentFile(QFileInfo(fixture()+"/Maps/"+map+".mpr"));
        check(window->m_recentFilesMenu&&!window->m_recentFilesMenu->actions().isEmpty(),"real recent files menu");
        commandQpc=qpc();window->m_recentFilesMenu->actions().first()->trigger();validateReady(map);
    }
    void gallery(const QString& map) {
        window->m_mapsDock->show();window->refreshMapsBrowser();
        auto* tree=window->findChild<QTreeWidget*>("mapsBrowserTree");check(tree,"real maps gallery tree");
        QTreeWidgetItem* found=nullptr;
        for(QTreeWidgetItemIterator it(tree);*it;++it)if(QFileInfo((*it)->data(0,Qt::UserRole).toString()).canonicalFilePath()==QFileInfo(fixture()+"/Maps/"+map+".mpr").canonicalFilePath()){found=*it;break;}
        check(found,"gallery resolves own map copy");
        for(auto* item=found->parent();item;item=item->parent())item->setExpanded(true);
        tree->setCurrentItem(found);tree->scrollToItem(found);QApplication::processEvents();
        const auto point=tree->visualItemRect(found).center();check(tree->viewport()->rect().contains(point),"gallery event position valid");
        commandQpc=qpc();
        QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&press);
        QMouseEvent release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&release);
        QMouseEvent dbl(QEvent::MouseButtonDblClick,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&dbl);
        QApplication::sendEvent(tree->viewport(),&release);validateReady(map);
    }
    void worldDoubleClick(bool gl=false) {
        SNeighborMapPlacement target;
        for(const auto& p:view->m_neighborPlacements)if(QFileInfo(p.mprPath).completeBaseName()=="zone7"){target=p;break;}
        check(!target.mprPath.isEmpty(),"zone7 is in the real world layout");
        const QVector3D center(float(target.origin.x()+target.size.width()/2),float(target.origin.y()+target.size.height()/2),0);
        QMatrix4x4 camera;camera.lookAt(center+QVector3D(0,0,800),center,QVector3D(0,1,0));view->m_cam->setPreviewView(camera);
        view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,float(native->width())/qMax(1,native->height()),1,2000);view->m_projection=view->m_normalProjection;
        QElapsedTimer settle;settle.start();wait([&]{return settle.elapsed()>150;},1000);
        QWidget* clickSurface=gl?static_cast<QWidget*>(view):native->findChild<QWidget*>("rebuild64NativeSurface");
        const auto point=clickSurface->rect().center();commandQpc=qpc();
        QMouseEvent dbl(QEvent::MouseButtonDblClick,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(clickSurface,&dbl);
        check(wait([&]{return CLandscape::getInstance()->filePath().completeBaseName()=="zone7";},240000),"native double click took the canonical neighbour route");
        validateReady("zone7-world",gl);view->m_cam->clearPreviewView();
    }
    void createMap() {
        const auto privatePack=configuration["privateTerrainPack"].toString();QString publication;
        for(const auto& path:CTextureList::getInstance()->configuredResourceArchivePaths())if(QFileInfo(path).fileName().compare("HDPack_Terrain.res",Qt::CaseInsensitive)==0){publication=path;break;}
        check(QFileInfo(publication).canonicalFilePath()==QFileInfo(privatePack).canonicalFilePath(),"New MPR publication targets only the private pack");
        creating=true;nextFile=caseRoot+"/launch287new.mpr";commandQpc=qpc();action("actionNew_MPR");creating=false;
        check(QFileInfo(nextCreated).isFile(),"real New MPR dialog created own copy");
        validateReady("launch287new",!configuration["expectedNative"].toBool(true));check(native->rebuildSelected()==configuration["expectedNative"].toBool(true),"New MPR opened in the selected renderer");
    }
    QJsonObject request(const QString& method,const QJsonObject& args={})
    {
        if(!socket) {
            check(window->m_aiAgent&&window->m_aiAgent->enabled(),"isolated MCP enabled by fixture settings");
            allowedNamedPipe=window->m_aiAgent->m_server.serverName();
            socket=new QLocalSocket(this);socket->connectToServer(window->m_aiAgent->m_server.serverName());
            check(wait([&]{return socket->state()==QLocalSocket::ConnectedState;},10000),"own MCP transport connected");
        }
        const auto id=++requestId;
        const auto data=QJsonDocument(QJsonObject{{"id",id},{"secret",window->m_aiAgent->m_secret},{"protocolMajor",1},{"method",method},{"arguments",args}}).toJson(QJsonDocument::Compact)+'\n';
        socket->write(data);socket->flush();QJsonObject response;
        check(wait([&]{buffer+=socket->readAll();if(!buffer.contains('\n'))return false;const auto end=buffer.indexOf('\n');response=QJsonDocument::fromJson(buffer.left(end)).object();buffer.remove(0,end+1);return true;},180000),"MCP response "+method);
        check(response["id"].toInt()==id,"MCP response identity");return response["result"].toObject();
    }
    QJsonObject operation(QJsonObject result,bool allowError=false)
    {
        check(!result.contains("error"),"MCP request accepted: "+result.value("error").toString());
        const auto id=result["operation"].toString();if(id.isEmpty())return result;
        QElapsedTimer poll;poll.start();
        check(wait([&]{if(poll.elapsed()<50)return false;poll.restart();result=request("operation_status",{{"operation",id}});return result["status"].toString()!="running";},240000),"MCP operation terminal");
        record("mcp_result",result);if(!allowError)check(!result.contains("error"),"MCP operation success");
        if(result["result"].isObject())result=result["result"].toObject();
        if(!allowError)check(!result.contains("error"),"MCP operation payload success: "+result.value("error").toString());return result;
    }
    void key(int value,const QString& text={}) {
        QWidget* target=native->rebuildSelected()?native->findChild<QWidget*>("rebuild64NativeSurface"):view;
        QKeyEvent press(QEvent::KeyPress,value,Qt::NoModifier,0,value==Qt::Key_Return?VK_RETURN:value,0,text);QApplication::sendEvent(target,&press);
        QKeyEvent release(QEvent::KeyRelease,value,Qt::NoModifier,0,value==Qt::Key_Return?VK_RETURN:value,0,text);QApplication::sendEvent(target,&release);
    }
    QJsonArray latestPresentClip() {
        QFile file(caseRoot+"/presented.jsonl");if(!file.open(QIODevice::ReadOnly))return {};
        const auto rows=file.readAll().split('\n');for(int i=rows.size()-1;i>=0;--i){const auto row=QJsonDocument::fromJson(rows[i]).object();if(row["event"]=="present"&&row["qtPageShown"].toBool())return row["cameraClip"].toArray();}return {};
    }
    void outputCheck(const QString& name) {
        const auto stats=native->statistics();const int expected=CLandscape::getInstance()->filePath().completeBaseName()=="zone1"?5:4;
        record("output_barrier",{{"name",name},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"generation",QString::number(stats.generation)},{"neighbours_accepted",int(stats.neighbours_accepted)},{"pending",int(stats.neighbours_pending)},{"exeSha256",qEnvironmentVariable("ATAMAN_REBUILD64_EXE_SHA")},{"dllSha256",qEnvironmentVariable("ATAMAN_REBUILD64_DLL_SHA")}});
        check(native->rebuildSelected()&&int(stats.neighbours_accepted)==expected&&!stats.neighbours_pending&&native->frameIsCurrent(),"native output barrier: "+name);
    }
    void previewCheck(CPreview* preview,const QString& name) {
        check(preview,"real preview widget: "+name);const qint64 begin=qpc();qint64 previous=begin,maxGap=0;
        QTimer heartbeat;heartbeat.setInterval(10);connect(&heartbeat,&QTimer::timeout,this,[&]{const auto now=qpc();maxGap=qMax(maxGap,now-previous);previous=now;});heartbeat.start();
        const bool delivered=wait([&]{return !preview->nativeImage().isNull();},20000);if(!delivered){QStringList labels;for(auto* label:preview->findChildren<QLabel*>())labels<<label->text();record("preview_wait_failure",{{"name",name},{"labels",labels.join(" | ")}});}check(delivered,"native preview delivered: "+name);heartbeat.stop();maxGap=qMax(maxGap,qpc()-previous);
        record("preview_source_value",{{"name",name},{"source",QJsonObject::fromVariantMap(preview->property("nativeOutputSource").toMap())}});outputCheck(name);preview->nativeImage().save(caseRoot+"/"+name+".png");record("preview_timing",{{"name",name},{"elapsedMs",double(qpc()-begin)/10000},{"maxQtGapMs",double(maxGap)/10000}});
    }
    void resourceInspector() {
        auto* browser=window->m_resourceBrowser;check(browser,"ordinary resource browser");window->m_resourceBrowserDock->show();
        check(wait([&]{return browser->selectResourceReference("humanhouse06.mmp");},30000),"resource texture selected through ordinary browser");
        auto* stack=browser->findChild<QStackedWidget*>("resourcePreviewStack");auto* imageView=browser->findChild<QGraphicsView*>("resourceImagePanZoom");
        check(wait([&]{return stack&&stack->currentIndex()==1&&imageView&&imageView->scene()&&!imageView->scene()->items().isEmpty();},30000),"CPU texture inspector delivered");
        auto* pixmapItem=dynamic_cast<QGraphicsPixmapItem*>(imageView->scene()->items().first());check(pixmapItem,"resource inspector image output");outputCheck("resource-texture");pixmapItem->pixmap().toImage().save(caseRoot+"/resource-texture.png");
        nextFile=caseRoot+"/resource-humanhouse06.mmp";browser->findChild<QPushButton*>("resourceExportButton")->click();QFile texture(nextFile.isEmpty()?caseRoot+"/resource-humanhouse06.mmp":nextFile);check(texture.open(QIODevice::ReadOnly)&&ei::inspectMmp(texture.readAll()).valid,"resource byte export is a valid unchanged MMP");texture.close();
        check(browser->selectResourceReference("stbuho7.mod"),"resource model selected through ordinary browser");auto* canvas=browser->findChild<QLabel*>("resourceModelCanvas");
        check(wait([&]{return stack->currentIndex()==2&&canvas&&canvas->pixmap()&&!canvas->pixmap()->isNull();},30000),"CPU structural model inspector delivered");outputCheck("resource-model");canvas->pixmap()->toImage().save(caseRoot+"/resource-model.png");
        nextFile=caseRoot+"/resource-stbuho7.mod";browser->findChild<QPushButton*>("resourceExportButton")->click();check(QFileInfo(caseRoot+"/resource-stbuho7.mod").size()>0,"resource model byte export");window->m_resourceBrowserDock->hide();
    }
    void independentWidgets() {
        const auto begin=qpc();window->generateMinimap();check(window->m_minimapExportDialog,"menu minimap dialog");outputCheck("menu-minimap");
        auto* minimap=window->m_minimapExportDialog.data();check(minimap->capture().nativeOutput,"menu minimap native provenance");minimap->gradedImage().save(caseRoot+"/menu-minimap.png");
        auto* shading=minimap->findChild<QSlider*>("minimapShadingSlider");check(shading&&!shading->isEnabled()&&!shading->toolTip().isEmpty(),"unsupported native relighting is explicit");
        nextFile=caseRoot+"/menu-review.png";QMetaObject::invokeMethod(minimap,"saveReviewPng",Qt::DirectConnection);check(!QImage(caseRoot+"/menu-review.png").isNull(),"menu minimap review PNG normal path");minimap->findChild<QLineEdit*>("minimapTextureName")->setText("native288_zone1");minimap->findChild<QLineEdit*>("minimapDestinationRes")->setText(caseRoot+"/native-minimap.res");for(auto* box:minimap->findChildren<QCheckBox*>())box->setChecked(false);
        QMetaObject::invokeMethod(minimap,"exportTexture",Qt::DirectConnection);check(minimap->exportSucceeded(),"menu exports native MMP to private RES");CResArchiveIndex archive;QString archiveError;check(archive.open(caseRoot+"/native-minimap.res",&archiveError),"native minimap RES index readback");const auto decoded=ei::inspectMmp(archive.readMember("native288_zone1.mmp",&archiveError));check(decoded.valid&&decoded.width==minimap->capture().targetTextureSize,"native MMP format and extent preserved");decoded.baseImage.save(caseRoot+"/native-minimap-res-readback.png");
        minimap->close();record("menu_minimap_ms",{{"elapsed",double(qpc()-begin)/10000}});
        qint64 previewPulse=qpc(),previewMaxGap=0;QTimer previewHeartbeat;previewHeartbeat.setInterval(10);connect(&previewHeartbeat,&QTimer::timeout,&previewHeartbeat,[&]{const auto now=qpc();previewMaxGap=qMax(previewMaxGap,now-previewPulse);previewPulse=now;});previewHeartbeat.start();
        CNode* galleryNode=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==eWorldObject&&node->modelName().startsWith("stbuho",Qt::CaseInsensitive)){galleryNode=node;break;}
        check(galleryNode,"gallery model source");QString galleryFailure;
        QTimer::singleShot(0,this,[&]{auto* gallery=view->findChild<CModelGallery*>();try {
            check(gallery,"ordinary CView object-gallery dialog");gallery->findChild<QLineEdit*>("modelGallerySearch")->setText(galleryNode->modelName());gallery->setCurrentModel(galleryNode->modelName());gallery->setCurrentTexture(galleryNode->textureName());
            auto* tiles=gallery->findChild<QListWidget*>("modelGalleryTiles");check(tiles&&tiles->count(),"real gallery model tile");QListWidgetItem* tile=nullptr;for(int row=0;row<tiles->count();++row)if(tiles->item(row)->data(Qt::UserRole).toString().contains(galleryNode->modelName(),Qt::CaseInsensitive)){tile=tiles->item(row);break;}check(tile,"selected real house tile");
            check(wait([&]{return !tile->data(Qt::UserRole+2).toString().isEmpty()&&!tile->icon().isNull();},30000),"gallery thumbnail delivered");outputCheck("gallery-thumbnail");check(!tile->toolTip().contains("CPU"),"gallery native thumbnail, no fallback");tile->icon().pixmap(192,132).toImage().save(caseRoot+"/gallery-thumbnail.png");
            check(ei::galleryThumbnailDirectory("Rebuild64")!=ei::galleryThumbnailDirectory(),"native and legacy disk cache directories differ");
        }catch(const QString& error){galleryFailure=error;}if(gallery)gallery->reject();});
        view->openObjectGallery();check(galleryFailure.isEmpty(),"ordinary object gallery: "+galleryFailure);record("preview_open_qt_gap",{{"name","object-gallery"},{"maxQtGapMs",double(qMax(previewMaxGap,qpc()-previewPulse))/10000}});previewPulse=qpc();previewMaxGap=0;
        window->showItemSpellConstructor();check(wait([&]{return !window->m_itemSpellConstructorDialog.isNull();},30000),"ordinary async item constructor path");auto* constructor=window->m_itemSpellConstructorDialog.data();
        auto* items=constructor->findChild<QListWidget*>("constructorItemList");check(wait([&]{return items&&items->count();},30000),"constructor real database items");
        auto* category=constructor->findChild<QComboBox*>("constructorItemCategory");category->setCurrentIndex(1);
        check(items->count(),"weapon database candidates");items->setCurrentRow(0);auto* itemPreview=constructor->findChild<CPreview*>();previewCheck(itemPreview,"constructor-item");constructor->hide();record("preview_open_qt_gap",{{"name","constructor"},{"maxQtGapMs",double(qMax(previewMaxGap,qpc()-previewPulse))/10000}});previewPulse=qpc();previewMaxGap=0;
        auto* create=window->m_createDialog.data();check(create,"ordinary new-object dialog");create->show();
        auto* databaseCategory=create->findChild<QComboBox*>("databaseCategory");check(databaseCategory,"database category widget");databaseCategory->setCurrentIndex(1);
        auto* entries=create->findChild<QListWidget*>("databaseObjects");check(wait([&]{return entries&&entries->count()>0;},30000),"database placement candidates");bool databasePreview=false;int chosen=-1;
        for(int row=0;row<qMin(entries->count(),20)&&!databasePreview;++row){entries->setCurrentRow(row);QElapsedTimer attempt;attempt.start();wait([&]{return !create->viewWidget()->nativeImage().isNull()||attempt.elapsed()>500;},1000);if(!create->viewWidget()->nativeImage().isNull()){chosen=row;databasePreview=true;}}
        if(!databasePreview){databaseCategory->setCurrentIndex(2);for(int row=0;row<qMin(entries->count(),20)&&!databasePreview;++row){entries->setCurrentRow(row);QElapsedTimer attempt;attempt.start();wait([&]{return !create->viewWidget()->nativeImage().isNull()||attempt.elapsed()>500;},1000);if(!create->viewWidget()->nativeImage().isNull()){chosen=row;databasePreview=true;}}}
        if(!databasePreview){QStringList text;for(auto* label:create->findChildren<QLabel*>())if(!label->text().isEmpty())text<<label->objectName()+":"+label->text();record("database_preview_unavailable",{{"details",text.join(" | ")},{"visible",create->viewWidget()->isVisible()},{"width",create->viewWidget()->width()},{"height",create->viewWidget()->height()},{"source",create->viewWidget()->property("nativeOutputSource").toJsonObject()}});}check(databasePreview,"database has a real available native preview");record("database_preview_source",{{"row",chosen},{"prototype",entries->item(chosen)->data(Qt::UserRole+1).toString()}});previewCheck(create->viewWidget(),"database-item");create->hide();previewHeartbeat.stop();record("preview_open_qt_gap",{{"name","database/new-object"},{"maxQtGapMs",double(qMax(previewMaxGap,qpc()-previewPulse))/10000}});
    }
    void measureCamera(const QString& kind,const std::function<void()>& input) {
        const auto before=latestPresentClip();const auto begin=qpc();input();
        record("camera_input_state",{{"kind",kind},{"keysW",view->m_pOp->keyManager()->isPressed(eKey_W)},{"cameraMoving",view->m_cam->movementInputActive()},{"frameCurrent",native->frameIsCurrent()},{"early",native->m_earlyDocument},{"queuedEvents",native->m_waitingInput.size()},{"selected",native->m_selected},{"resourceWriteDepth",native->m_resourceWriteDepth},{"projectionDepth",view->m_openingProjectionDepth},{"timerActive",native->m_timer->isActive()}});
        const bool advanced=wait([&]{auto next=latestPresentClip();if(next.size()!=16||before.size()!=16)return false;for(int i=0;i<16;++i)if(qAbs(next[i].toDouble()-before[i].toDouble())>1e-6)return true;return false;},5000);
        record("camera_after_wait",{{"kind",kind},{"keysW",view->m_pOp->keyManager()->isPressed(eKey_W)},{"cameraMoving",view->m_cam->movementInputActive()},{"frameCurrent",native->frameIsCurrent()},{"early",native->m_earlyDocument},{"queuedEvents",native->m_waitingInput.size()},{"selected",native->m_selected},{"resourceWriteDepth",native->m_resourceWriteDepth},{"projectionDepth",view->m_openingProjectionDepth},{"timerActive",native->m_timer->isActive()}});
        check(advanced,"live camera has new accepted Present: "+kind);
        QFile file(caseRoot+"/presented.jsonl");file.open(QIODevice::ReadOnly);qint64 accepted=0;
        for(const auto& line:file.readAll().split('\n')){auto row=QJsonDocument::fromJson(line).object();if(row["event"]!="present"||row["qpc"].toDouble()<begin)continue;auto clip=row["cameraClip"].toArray();if(clip.size()!=16)continue;bool changed=false;for(int i=0;i<16;++i)changed|=qAbs(clip[i].toDouble()-before[i].toDouble())>1e-6;if(changed){accepted=qint64(row["qpc"].toDouble());break;}}
        check(accepted>=begin,"camera latency uses actual Present QPC");record("camera_latency",{{"kind",kind},{"milliseconds",double(accepted-begin)/10000.0},{"inputQpc",QString::number(begin)},{"presentQpc",QString::number(accepted)},{"queryReady",native->editorQueryReady()}});
    }
    void liveCameraEvents() {
        // Fast runs can finish query geometry before the first native frame.
        // Exercise the same pending-input branch deterministically in this one
        // functional case; never enable this gate in performance cases.
        const bool controlledHold=native->editorQueryReady()&&!native->openingDocument();
        const QScopedValueRollback<int> hold(view->m_openingProjectionDepth,view->m_openingProjectionDepth+(controlledHold?1:0));
        try {
            record("camera_readiness_gate",{{"controlledHold",controlledHold},{"naturalPending",!controlledHold}});
            check(wait([&]{return latestPresentClip().size()==16;}),"first shown stream camera available");
            check(CLandscape::getInstance()->isMprLoad()&&CLandscape::getInstance()->renderResourcesDeferred(),"first-frame navigation has CPU terrain without world GL atlas");
            earlyUndoIndex=window->m_undoStack->index();auto* target=native->findChild<QWidget*>("rebuild64NativeSurface");const auto point=target->rect().center();
            QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(target,&press);QApplication::sendEvent(target,&release);
            check(native->pendingEditorInput(),"click is queued while camera remains live");
            measureCamera("W",[&]{QKeyEvent e(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier,0,'W',0,"w");QApplication::sendEvent(target,&e);});
            QKeyEvent up(QEvent::KeyRelease,Qt::Key_W,Qt::NoModifier,0,'W',0,"w");QApplication::sendEvent(target,&up);
            measureCamera("wheel",[&]{QWheelEvent e(QPointF(point),QPointF(target->mapToGlobal(point)),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(target,&e);});
            measureCamera("middle-drag",[&]{QMouseEvent a(QEvent::MouseButtonPress,point,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier),b(QEvent::MouseMove,point+QPoint(24,8),Qt::NoButton,Qt::MiddleButton,Qt::NoModifier),c(QEvent::MouseButtonRelease,point+QPoint(24,8),Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(target,&a);QApplication::sendEvent(target,&b);QApplication::sendEvent(target,&c);});
            key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
            QKeyEvent undo(QEvent::KeyPress,Qt::Key_Z,Qt::ControlModifier,0,'Z',0,"z"),undoUp(QEvent::KeyRelease,Qt::Key_Z,Qt::ControlModifier,0,'Z',0,"z");QApplication::sendEvent(target,&undo);QApplication::sendEvent(target,&undoUp);
            check(window->m_undoStack->index()==earlyUndoIndex,"Undo/edit wait for canonical geometry in input order");
            record("camera_live_before_geometry",{{"pending",native->pendingEditorInput()},{"queryReady",native->editorQueryReady()},{"syntheticQueryHold",controlledHold}});
        }catch(const QString& error){liveError=error;}
        liveDone=true;
    }
    void liveCameraScenario() {
        CMobDocument document;CValidationReport report;check(document.load(QFileInfo(fixture()+"/Maps/zone1.mob"),&report),"real CPU click target source");const SMobObjectSnapshot* chosen=nullptr;
        for(const auto& object:document.objects())if(object.sectionType==45057u&&object.hasPosition&&!object.unit&&object.model.compare("stst8",Qt::CaseInsensitive)){if(!chosen)chosen=&object;if(object.model.contains("stbuho",Qt::CaseInsensitive)||object.model.contains("rock",Qt::CaseInsensitive)){chosen=&object;break;}}
        check(chosen,"live-camera real static target");earlyTargetNid=chosen->nid;earlyTargetPosition={chosen->positionX,chosen->positionY,chosen->positionZ};
        QMatrix4x4 camera;camera.lookAt(earlyTargetPosition+QVector3D(0,-1,100),earlyTargetPosition,QVector3D(0,1,0));view->m_cam->adoptView(camera);liveWanted=true;
        openPair("zone1");check(wait([&]{return liveDone;}),"live camera scenario completed");check(liveError.isEmpty(),"live camera error: "+liveError);validateReady("zone1-live-camera");
        check(wait([&]{return !native->pendingEditorInput();}),"frozen-camera queued input drained");
        check(view->selectionSnapshot().size()==1,"queued click selects one object after flight");auto* node=view->selectionSnapshot().first();check(node->mapId()==earlyTargetNid,"queued click selects object at click-time camera");
        check(window->m_undoStack->index()==earlyUndoIndex+1,"queued numeric edit then Undo keeps canonical history order");
        auto before=node->position();window->on_actionRedo_triggered();check(qAbs(node->position().x()-before.x()-1)<.001f,"Redo proves queued edit was undone");window->on_actionUndo_triggered();
        validateReady("zone1-live-camera-undone");window->closeAll();
    }
    void earlyInputScenario() {
        CMobDocument document;CValidationReport report;
        check(document.load(QFileInfo(fixture()+"/Maps/zone1.mob"),&report),"CPU source for first-frame click target");
        const SMobObjectSnapshot* chosen=nullptr;
        for(const auto& object:document.objects())if(object.sectionType==45057u&&object.hasPosition&&!object.unit&&object.model.compare("stst8",Qt::CaseInsensitive)) {
            if(!chosen)chosen=&object;
            if(object.model.contains("stbuho",Qt::CaseInsensitive)||object.model.contains("rock",Qt::CaseInsensitive)){chosen=&object;break;}
        }
        check(chosen,"real static target in zone1 CPU document");earlyTargetNid=chosen->nid;
        earlyTargetPosition=QVector3D(chosen->positionX,chosen->positionY,chosen->positionZ);
        QMatrix4x4 camera;camera.lookAt(earlyTargetPosition+QVector3D(0,0,100),earlyTargetPosition,QVector3D(0,1,0));view->m_cam->adoptView(camera);
        record("early_input_target",{{"nid",int(earlyTargetNid)},{"model",chosen->model}});
        openPair("zone1");validateReady("zone1-early-input");
        check(earlyInputSent,"click and numeric edit sent on the first displayed world frame");
        const bool applied=wait([&]{return !native->pendingEditorInput()&&window->m_undoStack->index()>=earlyUndoIndex+2;},15000);
        record("early_input_state",{{"pendingInput",native->pendingEditorInput()},{"undoIndex",window->m_undoStack->index()},{"initialUndoIndex",earlyUndoIndex},{"selectionCount",view->selectionSnapshot().size()},{"pendingOperation",view->hasPendingOperation()}});
        check(applied&&window->m_undoStack->index()==earlyUndoIndex+2,"retained click and numeric edit produce exactly their two canonical Undo commands");
        check(native->rebuildSelected(),"retained click does not select an unsupported/garbage object");
        const auto selection=view->selectionSnapshot();check(selection.size()==1,"retained click selects one canonical object");
        auto* node=selection.first();check(node->nodeType()==eWorldObject,"retained click selects real static geometry");
        const auto after=node->position();window->m_undoStack->undo();const auto before=node->position();
        check((after-before-QVector3D(1,0,0)).length()<.0001f,"retained G X 1 Enter produces the canonical one-metre edit");
        window->m_undoStack->redo();check(node->position()==after,"retained edit supports Redo");window->m_undoStack->undo();
        record("early_input_applied",{{"queueMilliseconds",double(qpc()-earlyInputQpc)/10000.0},{"nid",int(node->mapId())},{"targetNid",int(earlyTargetNid)}});
        validateReady("zone1-early-input-undone");window->closeAll();
    }
    void preparationRaces() {
        const auto pendingPath=caseRoot+"/during-prepare-native.png";
        auto* captureTimer=new QTimer(this);captureTimer->setInterval(15);
        const auto response=connect(native,&CRebuild64Viewport::captureFinished,this,[this,pendingPath](const QString& path,const QString& error,quint64 generation){if(path==pendingPath){prepareCaptureDone=true;prepareCaptureError=error;record("capture_during_prepare_response",{{"error",error},{"generation",QString::number(generation)}});}});
        connect(captureTimer,&QTimer::timeout,this,[this,captureTimer,pendingPath]{if(!prepareCaptureSent&&native->openingDocument()&&native->m_earlyMap.endsWith("zone7.mpr",Qt::CaseInsensitive)&&native->m_worker){prepareCaptureSent=true;captureTimer->stop();native->capture(pendingPath);record("capture_during_prepare_requested");}});captureTimer->start();
        bool closed=false;auto* closeTimer=new QTimer(this);closeTimer->setInterval(15);
        connect(closeTimer,&QTimer::timeout,this,[&]{
            if(closed||ei::legacyProjectionPulse()||!window->m_workspaceLoadInProgress||!native->openingDocument()||!CLandscape::getInstance()->isMprLoad())return;
            closeQpc=qpc();window->closeAll();closed=!CLandscape::getInstance()->isMprLoad();
            if(closed){closeTimer->stop();record("close_during_prepare",{{"qpcAccepted",QString::number(closeQpc)}});}
        });closeTimer->start();openPair("zone1");closeTimer->stop();delete closeTimer;
        check(closed,"real close-map accepted during parallel preparation");
        check(!CLandscape::getInstance()->isMprLoad()&&view->mobs().isEmpty(),"cancelled opening does not register a late MOB/document");
        check(wait([&]{return native->backendPrepared()&&native->backendResourcesRetired();},30000),"closed preparing map retires native resources");
        bool queued=false;auto* timer=new QTimer(this);timer->setInterval(15);
        connect(timer,&QTimer::timeout,this,[&]{
            if(queued||!window->m_workspaceLoadInProgress||!native->openingDocument())return;
            switchQpc=qpc();queued=true;timer->stop();
            window->openMapPair(fixture()+"/Maps/zone7.mpr",fixture()+"/Maps/zone7.mob","zone7");
            record("map_switch_during_prepare",{{"queued",window->property("nativeMapRequestPending").toBool()},{"qpcAccepted",QString::number(switchQpc)}});
        });timer->start();openPair("zone1");
        check(wait([&]{return queued&&!window->property("nativeMapRequestPending").toBool()&&CLandscape::getInstance()->filePath().completeBaseName()=="zone7";},120000),"latest queued map becomes the canonical document");
        timer->stop();delete timer;validateReady("zone7-after-prepare-race");
        captureTimer->stop();delete captureTimer;check(prepareCaptureSent&&wait([&]{return prepareCaptureDone;},15000),"pending frame readback request receives a response");disconnect(response);
        check(prepareCaptureError.isEmpty()||prepareCaptureError.contains(QString::fromUtf8("Документ изменился")),"pending capture is current or explicitly invalidated");
        for(auto* mob:view->mobs())check(!mob->filePath().fileName().contains("zone1",Qt::CaseInsensitive),"retired opening has no MOB in final cohort");
        QFile trace(caseRoot+"/presented.jsonl");check(trace.open(QIODevice::ReadOnly),"race Present trace readable");
        for(const auto& line:trace.readAll().split('\n')){const auto row=QJsonDocument::fromJson(line).object();
            if(row["event"]=="present"&&row["map"]=="zone1.mpr") {
                const auto at=qint64(row["qpc"].toDouble());check(at<=closeQpc,"no retired map Present after close/latest map request");
            }
        }
        window->closeAll();
    }
    void recoveryCreate() {
        openPair("zone1");validateReady("zone1-before-edit");CNode* selected=nullptr;
        for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==eWorldObject&&node->modelName().compare("stst8",Qt::CaseInsensitive)){selected=node;break;}
        check(selected,"static recovery edit target");view->restoreSelection({selected});const auto before=selected->position();
        key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
        check(selected->position()!=before,"native Qt keyboard edit committed");
        window->m_undoStack->undo();check(selected->position()==before,"shared Undo");window->m_undoStack->redo();check(selected->position()!=before,"shared Redo");
        validateReady("zone1-after-edit");window->performAutosave();
        const auto recoveries=CBackupManager::recoverableAutosaves();QString backup;
        for(const auto& item:recoveries)if(QFileInfo(item.sourcePath).canonicalFilePath()==view->m_activeMob->filePath().canonicalFilePath())backup=item.autosavePath;
        check(!backup.isEmpty(),"production autosave payload exists");QString error;check(CBackupManager::validateAutosave(QFileInfo(backup),&error),"production autosave validated: "+error);
        const auto position=selected->position();QFile receipt(caseRoot+"/recovery.json");check(receipt.open(QIODevice::WriteOnly),"recovery receipt writable");
        receipt.write(QJsonDocument(QJsonObject{{"backup",backup},{"sha256",fileHash(backup)},{"nid",int(selected->mapId())},{"position",QJsonArray{position.x(),position.y(),position.z()}}}).toJson());receipt.close();
        record("unclean_exit_after_autosave");TerminateProcess(GetCurrentProcess(),86);
    }
    void recoveryRestore() {
        check(!native->rebuildSelected(),"unclean native session guard retained");
        QFile receipt(caseRoot+"/recovery.json");check(receipt.open(QIODevice::ReadOnly),"recovery receipt readable");const auto saved=QJsonDocument::fromJson(receipt.readAll()).object();
        const auto backup=saved["backup"].toString();check(fileHash(backup)==saved["sha256"].toString(),"autosave bytes retained across restart");
        const auto copy=caseRoot+"/recovered.mob";check(QFile::copy(backup,copy),"manual recovery copy created");commandQpc=qpc();
        window->openMapPair(fixture()+"/Maps/zone1.mpr",copy,"zone1-recovered");validateReady("zone1-recovered",true);
        CNode* node=nullptr;for(auto* mob:view->m_aMob)for(auto* n:mob->nodes())if(n->mapId()==quint32(saved["nid"].toInt()))node=n;
        check(node,"recovered object exists");const auto expected=saved["position"].toArray();check(node->position()==QVector3D(expected[0].toDouble(),expected[1].toDouble(),expected[2].toDouble()),"recovered object edit retained exactly");
        native->m_rebuildAction->trigger();validateReady("zone1-recovered-native");window->closeAll();
    }
 void startWhenIdle(){
  if(!window||qApp->property("rebuild64StartupDocumentBatch").toBool()||window->m_workspaceLoadInProgress||window->backgroundMobLoading()||window->m_worldMetadataWatcher){QTimer::singleShot(50,this,[this]{startWhenIdle();});return;}
  run();
 }
    void run()
    {
        int exitCode=0;
        try {
            LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
            record("check_begin",{{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"exeSha256",exeSha},{"dllSha256",dllSha},{"frequency",QString::number(frequency.QuadPart)},{"dpr",window->devicePixelRatioF()}});
            check(swapTraceInstalled,"own Qt SwapBuffers trace installed");
            if(!configuration["firstSetup"].toBool()||configuration["firstSetupAccept"].toBool()) {
                auto& context=ei::ProfileDatabaseContext::instance();const auto database=context.current();
                for(const auto& role:{"database","characterAppearance","equipmentLayout","unitAnimation","semanticValidation"})check(context.providerForRole(role)=="jmv.v1",QString("real DejaVu provider: ")+role);
                check(database&&database->providerId=="jmv.v1"&&!database->monsters.isEmpty()&&!database->raceModels.isEmpty(),"real DejaVu database loaded");
                record("profile",{{"provider",database->providerId},{"monsters",database->monsters.size()},{"races",database->raceModels.size()},{"source",database->sourceArchivePath}});
            }

            const auto mode=configuration["mode"].toString("open");
            check(QSettings().value("ui/viewportVisuals/neighborLandscapes",false).toBool()==configuration["seedNeighbors"].toBool(true),"neighbour preference preserved");
            const auto map=configuration["map"].toString("zone1");
            const bool headless=qApp->property("agentHeadless").toBool();
            if(mode=="offscreen") {
                openPair(map);validateReady(map);auditOpenResources();
                if(!configuration["skipPackage"].toBool()) {
                auto snapshot=operation(request("window_snapshot"));check(!snapshot.contains("error")&&snapshot["captureKind"].toString().startsWith("Rebuild64"),"MCP window_snapshot actual native path");
                const QString exported=caseRoot+"/native-package";
                check(exported.startsWith(writableRoot+"/"),"export path stays in the owned stage directory");
                const auto package=operation(request("export_map_topdown",{{"revision",request("get_revision")["revision"]},{"outputDir",exported},{"pixelsPerWorldUnit",1}}));
                check(!package.contains("error"),"MCP native eight-layer package: "+package["error"].toString());
                for(const auto& name:{"composite.png","terrain.png","relief.png","water.png","water-mask.png","structures.png","structures-mask.png","vegetation.png"})check(!QImage(exported+"/"+name).isNull(),"native export layer "+QString(name));
                }
                const int expected=map=="zone1"?5:4;check(int(native->statistics().neighbours_accepted)==expected,"offscreen accepted neighbours exact");
                outputCheck("memory-current");QString error;const auto camera=native->outputCamera();const auto generation=native->statistics().generation;
                const auto current=nativeOutput(view,1,QSize(640,480),camera,&error);check(!current.isNull(),"accepted native memory frame: "+error);current.save(caseRoot+"/memory-current.png");
                outputCheck("native-topdown");auto top=nativeTopdown(view,QSize(768,768),true,&error);check(!top.isNull(),"independent native topdown: "+error);top.save(caseRoot+"/native-topdown.png");
                check(native->rebuildSelected()&&native->statistics().generation==generation,"offscreen leaves native world generation selected");
                const auto after=native->outputCamera();check(!std::memcmp(camera.world_to_clip,after.world_to_clip,sizeof(camera.world_to_clip)),"offscreen leaves editor camera exact");
                outputCheck("native-minimap");const auto minimap=view->captureMinimapScene(false,256);check(minimap.bake.success,"normal minimap path uses native offscreen");minimap.bake.gameImage.save(caseRoot+"/native-minimap.png");
                CNode* selected=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==eWorldObject&&node->modelName().startsWith("stbuho",Qt::CaseInsensitive)){selected=node;break;}
                check(selected,"static house preview source");record("preview_source",{{"model",selected->modelName()},{"texture",selected->textureName()}});CPreview preview(nullptr,view);preview.setAttribute(Qt::WA_DontShowOnScreen);preview.resize(480,360);preview.attachView(view);preview.attachNode(selected);preview.show();
                previewCheck(&preview,"native-preview");
                CNode* other=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==eWorldObject&&node->modelName()!=selected->modelName()&&node->modelName().compare("stst8",Qt::CaseInsensitive)){other=node;break;}
                check(other,"second native model source");preview.attachNode(other);previewCheck(&preview,"native-preview-second");const auto previousImage=preview.nativeImage().cacheKey();
                QWheelEvent zoom(QPointF(200,150),QPointF(200,150),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&preview,&zoom);
                check(wait([&]{return !preview.nativeImage().isNull()&&preview.nativeImage().cacheKey()!=previousImage;},30000),"real preview wheel refreshes native output");outputCheck("native-preview-wheel");preview.nativeImage().save(caseRoot+"/native-preview-wheel.png");preview.attachNode(nullptr,false);preview.hide();
                if(configuration["widgets"].toBool()){independentWidgets();resourceInspector();}
                if(configuration["glOverlay"].toBool()){native->m_glAction->trigger();validateReady(map+"-overlay-gl",true);QString glError;QImage gl=view->captureTopdownPass(ETopdownPass::Composite,QSize(768,768),{},&glError);check(!gl.isNull(),"GL geometry comparison capture: "+glError);gl.save(caseRoot+"/gl-topdown.png");native->m_rebuildAction->trigger();validateReady(map+"-overlay-return");}
                auditOpenResources();window->closeAll();
            }
            else if(mode=="offscreen-races") {
                openPair(map);validateReady(map);outputCheck("race-begin");
                const auto before=native->outputCamera();QElapsedTimer baseline;baseline.start();wait([&]{return baseline.elapsed()>1000;},2000);record("offscreen_interval_begin");
                QTimer cameraInput;cameraInput.setSingleShot(true);connect(&cameraInput,&QTimer::timeout,this,[this]{key(Qt::Key_W,"w");});cameraInput.start(10);
                QString error;auto image=nativeTopdown(view,QSize(2048,2048),true,&error);check(!image.isNull(),"offscreen while native Present: "+error);record("offscreen_interval_end");outputCheck("race-topdown");image.save(caseRoot+"/race-topdown.png");
                check(native->rebuildSelected(),"offscreen never switches main renderer");
                auto serial=native->requestOutput(QSize(4096,4096),2,native->outputCamera());bool cancelled=false;
                auto connection=connect(native,&CRebuild64Viewport::outputFinished,this,[&](quint64 id,const QImage&,const QString& why,quint64,const QString&,int){if(id==serial)cancelled=!why.isEmpty();});
                window->closeAll();check(wait([&]{return cancelled;},30000),"close cancels queued or executing output");disconnect(connection);
                openPair("zone7");validateReady("zone7");outputCheck("race-last-map");
                serial=native->requestOutput(QSize(4096,4096),2,native->outputCamera());cancelled=false;connection=connect(native,&CRebuild64Viewport::outputFinished,this,[&](quint64 id,const QImage&,const QString& why,quint64,const QString&,int){if(id==serial)cancelled=!why.isEmpty();});
                openPair("zone1");check(wait([&]{return cancelled;},30000),"map switch cancels obsolete offscreen result");disconnect(connection);validateReady("zone1");outputCheck("race-last-map-2");
                QFile failure(caseRoot+"/case.json.offscreen-error");check(failure.open(QIODevice::WriteOnly),"own offscreen failure injection");failure.write("1");failure.close();
                const auto fallback=operation(request("export_map_topdown",{{"revision",request("get_revision")["revision"]},{"outputDir",caseRoot+"/fallback-package"},{"pixelsPerWorldUnit",1}}));check(!fallback.contains("error"),"offscreen failure uses this function's GL export");QFile::remove(failure.fileName());
                check(native->rebuildSelected(),"per-function GL fallback preserves selected native view");
                QFile loss(caseRoot+"/case.json.device-loss");check(loss.open(QIODevice::WriteOnly),"own device loss");loss.write("1");loss.close();check(wait([&]{return !native->lastFailure().isEmpty();}),"device-loss fallback");validateReady("zone1-device-fallback",true);
                check(view->m_neighborLandscapes.size()==11,"fallback restores all 11 GL landscapes");window->closeAll();
            }
            else if(mode=="gl-camera") {
                check(!native->rebuildSelected(),"GL control selected by saved preference");openPair("zone1");validateReady("zone1-gl-control",true);
                check(view->m_neighborLandscapes.size()==11,"ordinary OpenGL retains all eleven neighbours");
                CNode* target=nullptr;for(auto* node:view->m_activeMob->nodes())if(node->nodeType()==eWorldObject&&node->modelName().compare("stst8",Qt::CaseInsensitive)){target=node;break;}
                check(target,"GL static operation target");view->restoreSelection({target});const auto before=target->position();
                key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");check(target->position()!=before,"ordinary GL canonical keyboard move");
                window->m_undoStack->undo();check(target->position()==before,"ordinary GL Undo");window->m_undoStack->redo();window->m_undoStack->undo();
                worldDoubleClick(true);createMap();check(!GetModuleHandleW(L"ataman_rebuild64.dll"),"GL control never loads native DLL");window->closeAll();
            }
            else if(mode=="live-camera") {liveCameraScenario();}
            else if(mode=="switch-gl"||mode=="device-late") {
                openPair(map);validateReady(map);
                if(mode=="device-late"){check(CLandscape::getInstance()->renderResourcesDeferred(),"native terrain atlas really deferred before device loss");QFile marker(caseRoot+"/case.json.device-loss");check(marker.open(QIODevice::WriteOnly),"own device loss request");marker.write("1");marker.close();check(wait([&]{return !native->lastFailure().isEmpty();}),"late device loss fallback");check(failures==1,"single late device loss message");}
                else native->m_glAction->trigger();
                validateReady(map+"-gl-fallback",true);check(view->m_neighborLandscapes.size()==11,"GL retains all 11 landscapes");
                check(!CLandscape::getInstance()->renderResourcesDeferred()&&CLandscape::getInstance()->glTexture(),"deferred GL atlas materialized");
                CNode* selected=nullptr;for(auto* mob:view->mobs())for(auto* node:mob->nodes())if(!selected&&node->nodeType()==eWorldObject&&node->modelName().compare("stst8",Qt::CaseInsensitive))selected=node;
                check(selected,"GL static edit target");view->restoreSelection({selected});const auto before=selected->position();key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");check(qAbs(selected->position().x()-before.x()-1)<.001f,"GL tool works after lazy materialization");window->on_actionUndo_triggered();check((selected->position()-before).length()<.001f,"GL Undo exact");if(mode=="switch-gl"){native->m_rebuildAction->trigger();validateReady(map+"-native-return");}window->closeAll();
            }
            else if(mode=="early-input") {earlyInputScenario();}
            else if(mode=="prepare-races") {preparationRaces();}
            else if(mode=="recover-create") {recoveryCreate();}
            else if(mode=="recover-restore") {recoveryRestore();}
            else if(mode=="empty"||mode=="choice"||mode=="guard"||mode=="cold-switch") {
                check(!CLandscape::getInstance()->isMprLoad(),"no startup map");
                const bool expected=mode!="guard"&&configuration["expectedNative"].toBool(true);
                check(native->rebuildSelected()==expected,"startup preference/guard");
                if(mode=="cold-switch") {
                    bool switched=false,running=false;auto* timer=new QTimer(this);timer->setInterval(10);
                    connect(timer,&QTimer::timeout,this,[&]{QFile trace(caseRoot+"/presented.jsonl");if(!trace.open(QIODevice::ReadOnly))return;
                        const auto data=trace.readAll();if(!data.contains("shader_prepare_begin")||data.contains("shader_prepare_end"))return;
                        timer->stop();running=true;const auto before=qpc();native->m_glAction->trigger();switched=!native->rebuildSelected();
                        record("shader_switch_gl",{{"ticks",QString::number(qpc()-before)},{"shaderStillRunning",running}});
                    });timer->start();openPair(map);timer->stop();delete timer;
                    check(switched&&running,"real menu switched while parallel shader preparation was running");validateReady(map,true);
                }
                if(expected&&mode!="cold-switch"&&(!configuration["firstSetup"].toBool()||configuration["firstSetupAccept"].toBool()))check(wait([&]{return native->backendPrepared()||!native->lastFailure().isEmpty();}),"background startup preparation terminates");
                check(!native->statistics().ready_generation,"no world frame without a document");if(mode=="guard")check(QFileInfo(native->m_guardDirectory+"/manual-retry-required").isFile(),"migration did not remove the startup guard latch");
                if(expected&&mode!="cold-switch")check(native->m_stack->currentWidget()==native->m_preparation,"Qt empty state, no GL frame");
                if(expected&&mode!="cold-switch") {
                    check(native->backendResourcesRetired(),"no-map startup does not create a D3D device");
                    QFile trace(caseRoot+"/presented.jsonl");check(!trace.open(QIODevice::ReadOnly)||!trace.readAll().contains("device_begin"),"no-map startup has no device/shader preparation");
                }
                if(mode=="choice") {native->m_glAction->trigger();check(QSettings().value("ui/renderer").toString()=="OpenGL","explicit GL choice persisted");}
                record("empty_state",{{"text",native->m_preparationText->text()},{"migration",QSettings().value("ui/rebuild64DefaultMigration287").toBool()},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"neighbours_accepted",0},{"backendResourcePreparations",native->m_backendResourcePreparations},{"exeSha256",exeSha},{"dllSha256",dllSha}});
                QImage empty(native->m_preparation->size(),QImage::Format_ARGB32_Premultiplied);empty.fill(Qt::transparent);native->m_preparation->render(&empty);empty.save(caseRoot+"/empty-state.png");
                if(mode=="cold-switch")window->closeAll();
            } else if(mode=="mob-only") {
                if(!configuration["startupMobOnly"].toBool()){commandQpc=qpc();window->openDocument(QFileInfo(fixture()+"/Maps/zone1.mob"));}
                check(wait([&]{return !native->rebuildSelected()&&window->rebuild64DocumentReady()&&!view->m_aMob.isEmpty();}),"standalone MOB explicitly uses GL");
                check(!CLandscape::getInstance()->isMprLoad(),"standalone MOB has no synthetic terrain");validateReady("mob-only",true);
                check(QSettings().value("ui/renderer").toString()=="Rebuild64","MOB-only compatibility path keeps the conscious native preference");window->closeAll();
            } else if(mode=="queued-capabilities") {
                openPair("zone1");validateReady("zone1");
                const auto projectionFamily=view->mobs();QMap<CMob*,int> projectionCounts;
                for(auto* mob:projectionFamily)projectionCounts[mob]=mob->nodes().size();
                view->ensureUnitSupportSurface();const auto revision=request("get_revision")["revision"].toString();
                check(view->mobs()==projectionFamily,"lazy canonical projection preserves the complete MOB family");
                for(auto* mob:projectionFamily)check(mob&&mob->nodes().size()==projectionCounts.value(mob),"canonical projection retains each real MOB document and node count");
                auto* server=window->m_aiAgent;check(server->m_clients.size()==1,"only the owned MCP client is connected");const auto owner=server->m_clients.firstKey();const auto index=window->m_undoStack->index();
                for(const auto& method:{"sculpt","edit_units","edit_objects"}) {
                    auto plan=QSharedPointer<ai::EditPlan>::create();plan->method=method;plan->revision=revision;
                    if(plan->method=="edit_objects")plan->changes=QJsonArray{QJsonObject{{"after",QJsonObject{{"Model name","stst8"}}}}};
                    const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);CAiAgentServer::Job job;job.edit=true;job.owner=owner;server->m_jobs[id]=job;
                    QTimer::singleShot(0,server,[server,id,plan]{server->finishCommit(id,plan,false);});
                    check(wait([&]{return server->m_jobs[id].complete;},5000),"queued unsupported commit terminates");check(server->m_jobs[id].result["code"]=="REQUIRES_OPENGL","queued commit rechecks native capability: "+plan->method);server->m_jobs.remove(id);
                    job.plan=plan;job.markComplete();server->m_jobs[id]=job;
                    const auto refused=request("apply",{{"revision",revision},{"operation",id}});check(refused["code"]=="REQUIRES_OPENGL"&&server->m_jobs.contains(id)&&server->m_jobs[id].plan,"MCP apply keeps the unsupported preview plan: "+plan->method);server->m_jobs.remove(id);
                }
                check(window->m_undoStack->index()==index&&request("get_revision")["revision"]==revision,"unsupported queued plans preserve document revision and Undo");window->closeAll();
            } else if(mode=="resource-write-guard") {
                openPair("zone1");validateReady("zone1");const auto path=caseRoot+"/lease-check.res";QFile file(path);check(file.open(QIODevice::WriteOnly),"synthetic RES created in own case");file.write("before");file.close();
                SMapSaveFile update;update.path=path;update.before="before";update.after="after";update.existed=true;
                const auto previous=CResourceWriteLease::setAcquirer([](const QStringList&,QString* error)->std::shared_ptr<void>{*error="Isolated resource lease refusal";return {};});
                const auto result=commitMapSaveBatch({update},caseRoot+"/backups");CResourceWriteLease::setAcquirer(previous);
                check(!result.success&&result.error=="Isolated resource lease refusal","resource lease failure refuses transaction");check(file.open(QIODevice::ReadOnly)&&file.readAll()=="before","refused resource publication preserved original bytes");file.close();validateReady("zone1-after-refusal");window->closeAll();
            } else if(mode=="create") {openPair("zone1");validateReady("zone1",!configuration["expectedNative"].toBool(true));createMap();window->closeAll();}
            else if(mode=="routes") {
                openMenu("zone1");openRecent("zone7");gallery("zone1");
                openPair("zone7");validateReady("zone7-change");window->closeAll();
                openMenu("zone1",true);window->closeAll();gallery("zone1");worldDoubleClick();
                const auto revision=request("get_revision")["revision"].toString();commandQpc=qpc();
                operation(request("open_map",{{"revision",revision},{"map","zone1"}}));validateReady("zone1-mcp");
                createMap();window->closeAll();
            } else if(mode=="session-create") {openPair(map);validateReady(map);
                if(configuration["validSavedCamera"].toBool()) {
                    QMatrix4x4 camera;camera.lookAt(QVector3D(160,120,180),QVector3D(160,120,10),QVector3D(0,1,0));view->m_cam->adoptView(camera);
                    QJsonArray values;for(int i=0;i<16;++i)values.append(double(view->m_cam->viewMatrix().constData()[i]));
                    QFile saved(caseRoot+"/saved-camera.json");saved.open(QIODevice::WriteOnly);saved.write(QJsonDocument(values).toJson());
                }
                view->saveSession();check(QFileInfo(QDir::tempPath()+"/ei_mapper_endurance_session.json").size()>0,"real session journal written");}
            else if(mode=="session-restore") {check(CLandscape::getInstance()->isMprLoad(),"real main restored startup session");commandQpc=configuration["processStartQpc"].toString().toLongLong();validateReady(map);
                if(configuration["validSavedCamera"].toBool()) {
                    QFile saved(caseRoot+"/saved-camera.json");check(saved.open(QIODevice::ReadOnly),"saved camera proof exists");const auto values=QJsonDocument::fromJson(saved.readAll()).array();
                    const auto matrix=view->m_cam->viewMatrix();double error=0;for(int i=0;i<16;++i)error=qMax(error,qAbs(double(matrix.constData()[i])-values[i].toDouble()));
                    check(error<.0001,"correct saved camera retained on real main session restore");record("saved_camera_retained",{{"maximumMatrixError",error}});
                }
                window->closeAll();}
            else if(mode=="mcp" || headless) {
                check(headless?!native->rebuildSelected():native->rebuildSelected(),"MCP startup renderer");
                if(!headless){const auto empty=operation(request("window_snapshot"));check(empty["captureKind"].toString().startsWith("Qt Rebuild64"),"MCP no-map snapshot uses the real Qt preparation page");}
                const auto catalog=operation(request("list_profile_maps",{{"limit",200}}));QString mapKey;
                for(const auto& item:catalog["maps"].toArray())if(item.toObject()["file"]==map+".mpr"){mapKey=item.toObject()["map"].toString();break;}
                check(!mapKey.isEmpty(),"isolated CPU read worker lists real profile maps");
                const auto unopened=operation(request("read_profile_map",{{"map",mapKey},{"width",2},{"height",2},{"limit",3}}));
                check(unopened["terrain"].toObject()["samples"].toArray().size()==4,"isolated CPU read worker reads unopened map");
                auto revision=request("get_revision")["revision"].toString();
                commandQpc=qpc();operation(request("open_map",{{"revision",revision},{"map",map}}));
                if(!headless)validateReady(map);else validateReady(map,true);
                if(!headless) {
                    auto unsupported=request("edit_units",{{"revision",request("get_revision")["revision"]},{"action","transform"},{"ids",QJsonArray{1}},{"delta",QJsonArray{1,0,0}},{"layer",3}});check(unsupported["code"].toString()=="REQUIRES_OPENGL","MCP units honestly require GL");
                    unsupported=request("preview",{{"tool","sculpt"},{"arguments",QJsonObject{{"revision",request("get_revision")["revision"]},{"action","raise"},{"area",QJsonObject{{"shape","circle"},{"x",1},{"y",1},{"radius",1}}}}}});check(unsupported["code"].toString()=="REQUIRES_OPENGL","MCP terrain preview honestly requires GL");
                    unsupported=request("export_map_topdown",{{"revision",request("get_revision")["revision"]},{"outputDir",caseRoot+"/topdown-refused"}});check(unsupported["code"].toString()=="REQUIRES_OPENGL","MCP topdown honestly requires GL");
                }
                const auto read=operation(request("read_terrain",{}));check(!read.contains("error"),"MCP read terrain");
                const auto context=request("get_context");check(!context["mpr"].toString().isEmpty(),"MCP context map");
                for(auto* mob:view->m_aMob)check(QDir::fromNativeSeparators(mob->filePath().canonicalFilePath()).startsWith(QDir::fromNativeSeparators(fixture()+"/Maps/"),Qt::CaseInsensitive),"Save target is a private MOB copy: "+mob->filePath().fileName());
                revision=request("get_revision")["revision"].toString();operation(request("save",{{"revision",revision}}));
                if(headless)check(!native->backendLoaded()&&!GetModuleHandleW(L"ataman_rebuild64.dll"),"headless has no D3D before/after open/read/Save");
                QJsonObject snapshot;
                if(headless&&qApp->property("agentNativeOutput").toBool()) {
                    QElapsedTimer preparation;preparation.start();
                    do {snapshot=operation(request("window_snapshot"),true);if(!snapshot.contains("error")||configuration["failure"].toString()=="missing")break;QElapsedTimer pause;pause.start();wait([&]{return pause.elapsed()>100;},1000);}while(preparation.elapsed()<120000);
                    if(configuration["failure"].toString()=="missing")check(snapshot.contains("error")&&!GetModuleHandleW(L"ataman_rebuild64.dll"),"explicit native request without DLL is an error, no quiet GL");
                    else {check(!snapshot.contains("error")&&snapshot["captureKind"].toString().contains("Rebuild64"),"explicit headless native snapshot");check(native->backendLoaded(),"headless creates D3D only for explicit output");
                        const auto stats=native->statistics();check(int(stats.neighbours_accepted)==(map=="zone1"?5:4)&&!stats.neighbours_pending,"headless native neighbours exact");
                        operation(request("export_map_topdown",{{"revision",request("get_revision")["revision"]},{"outputDir",caseRoot+"/headless-native-package"},{"pixelsPerWorldUnit",1}}));}
                } else {snapshot=operation(request("window_snapshot"));check(!snapshot.contains("error"),"MCP snapshot");}
                record("snapshot_kind",{{"kind",snapshot["captureKind"]}});
                revision=request("get_revision")["revision"].toString();operation(request("close_map",{{"revision",revision}}));
            } else {
                if(mode!="fallback")check(native->rebuildSelected()==configuration["expectedNative"].toBool(true),"startup selected renderer");
                if(!configuration["startupDocument"].toBool())openPair(map);else commandQpc=configuration["processStartQpc"].toString().toLongLong();
                if(mode=="fallback") {
                    check(wait([&]{return !native->lastFailure().isEmpty();}),"fallback reported");
                    check(failures==1,"one fallback message");validateReady(map,true);
                    check(CLandscape::getInstance()->isMprLoad(),"fallback retained document");
                    if(configuration["retry"].toBool()) {
                        qunsetenv("ATAMAN_REBUILD64_TEST_FAILURE");
                        if(configuration["missingConfig"].toBool())for(const auto& name:{"rddraw.ini","settings.ini"}){const auto destination=caseRoot+"/fixture/Config64/"+name;QFile::remove(destination);check(QFile::copy(fixture()+"/Config64/"+name,destination),"repair own missing Config64 copy");}
                        native->m_rebuildAction->trigger();validateReady(map+"-retry");check(failures==1,"retry has no repeated fallback message");
                    }

                } else {
                    validateReady(map,!configuration["expectedNative"].toBool(true));
                    if(configuration["switch"].toBool()) {
                        native->m_glAction->trigger();validateReady(map+"-switch",true);
                        native->m_rebuildAction->trigger();validateReady(map+"-return");
                    }
                }
                window->closeAll();check(!CLandscape::getInstance()->isMprLoad(),"close map");
                if(native->rebuildSelected())check(native->m_stack->currentWidget()==native->m_preparation,"close retires previous frame");
                if(native->rebuildSelected()) {
                    check(wait([&]{return native->backendPrepared()&&native->backendResourcesRetired();},30000),"no-map state retires D3D/resources instead of pending an empty MPR");
                    check(!native->statistics().ready_generation,"closed map has no ready world generation");
                }
            }
            if(configuration["holdMs"].toInt()>0){QElapsedTimer hold;hold.start();check(wait([&]{return hold.elapsed()>=configuration["holdMs"].toInt();},configuration["holdMs"].toInt()+2000),"concurrent process observation interval");check(native->rebuildSelected(),"other live guard did not force OpenGL");}
            ownVisibleWindows=0;EnumWindows(ownWindow,0);check(ownVisibleWindows==0,"no visible process windows");
            check(unexpectedDialogs==0,"no unexpected modal dialogs");
            check(deniedWrites.load()==0,"all Qt write targets stayed inside the owned workspace");
            check(deniedReads.load()==0,"no live replaceable mod resources read");
            record("scenario_complete",{{"success",true},{"maxQtGapTicks",QString::number(maxGap)},{"nativeLoaded",native->backendLoaded()}});
        }catch(const QString& error) {record("complete",{{"success",false},{"error",error}});exitCode=1;}
        if(socket)socket->disconnectFromServer();pulse.stop();
        if(!exitCode){window->close();if(!wait([&]{return window->m_closeReady&&!window->view();},125000)){record("complete",{{"success",false},{"error","Real MainWindow close did not finish"}});exitCode=1;}
            else record("complete",{{"success",deniedWrites.load()==0},{"realClose",true},{"maxQtGapTicks",QString::number(maxGap)}});
        }
        qApp->setProperty("nativeCheckComplete",true);QCoreApplication::exit(exitCode);
    }
    bool eventFilter(QObject* object,QEvent* event)override
    {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)if(auto* widget=qobject_cast<QWidget*>(object))if(widget->isWindow()) {
            widget->setAttribute(Qt::WA_DontShowOnScreen);widget->setAttribute(Qt::WA_ShowWithoutActivating);widget->move(-32000,-32000);
            if(event->type()==QEvent::Show) {
                if(creating)if(auto* dialog=qobject_cast<QDialog*>(widget))if(dialog->findChild<QLineEdit*>("newMprTemplatePath")) {
                    QTimer::singleShot(0,dialog,[this,dialog]{
                        dialog->findChild<QLineEdit*>("newMprTemplatePath")->setText(fixture()+"/Maps/zone1.mpr");
                        dialog->findChild<QLineEdit*>("newMprNamespace")->setText("launch287new");
                        dialog->findChild<QSpinBox*>("newMprSectorsX")->setValue(1);dialog->findChild<QSpinBox*>("newMprSectorsY")->setValue(1);
                        nextCreated=nextFile;dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                    });
                }

                if(auto* dialog=qobject_cast<QFileDialog*>(widget)) {
                    if(!nextFile.isEmpty()){const auto path=nextFile;nextFile.clear();QTimer::singleShot(0,dialog,[dialog,path]{dialog->selectFile(path);QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);});}
                } else if(auto* setup=qobject_cast<CResourceSetupDialog*>(widget)) {
                    record("setup_dialog");
                    if(configuration["firstSetupAccept"].toBool())QTimer::singleShot(0,setup,[this,setup]{
                        auto* root=setup->findChild<QLineEdit*>();auto* preset=setup->findChild<QComboBox*>();check(root&&preset,"first setup has real controls");root->setText(fixture());QMetaObject::invokeMethod(root,"editingFinished",Qt::DirectConnection);preset->setCurrentIndex(preset->findData("deja-vu"));
                        auto* buttons=setup->findChild<QDialogButtonBox*>();check(buttons&&buttons->button(QDialogButtonBox::Ok)->isEnabled(),"first setup fixture profile valid");buttons->button(QDialogButtonBox::Ok)->click();
                    });
                    else QTimer::singleShot(0,setup,&QDialog::reject);
                } else if(auto* message=qobject_cast<QMessageBox*>(widget)) {
                    const bool knownDataNotice=(message->text().contains("INFO:")&&message->text().contains("Autozone"))||(configuration["firstSetupAccept"].toBool()&&message->icon()==QMessageBox::Information&&message->text().contains("DejaVu launch287 isolated"));
                    if(!knownDataNotice)++unexpectedDialogs;record(knownDataNotice?"existing_map_validation_notice":"unexpected_dialog",{{"text",message->text()}});QTimer::singleShot(0,message,&QDialog::reject);if(!knownDataNotice)QTimer::singleShot(1,this,[]{for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))if(d->findChild<QLineEdit*>("newMprNamespace"))d->reject();});
                }
            }
        }
        return false;
    }
private:
    QFile log;QPointer<MainWindow> window;QPointer<CView> view;QPointer<CRebuild64Viewport> native;
    bool prepareCaptureSent=false,prepareCaptureDone=false;QString prepareCaptureError;
    bool liveWanted=false,liveSent=false,liveDone=false;QString liveError;
    bool earlyInputSent=false;quint32 earlyTargetNid=0;QVector3D earlyTargetPosition;int earlyUndoIndex=0;
    qint64 earlyInputQpc=0,closeQpc=0,switchQpc=0;
    QTimer pulse;qint64 lastPulse=0,maxGap=0,commandQpc=0;quint64 lastGeneration=0;
    bool sawGL=false,swapTraceInstalled=false,creating=false;QSet<QString> glCaptured;QString exeSha,dllSha,nextFile,nextCreated;int failures=0,unexpectedDialogs=0,requestId=0;
    QLocalSocket* socket=nullptr;QByteArray buffer;
};
namespace {CRebuild64LaunchCheck* driver=nullptr;
void glPresented(HDC dc,qint64 begin,qint64 end,BOOL ok){if(qApp&&driver&&!qApp->property("nativeCheckComplete").toBool())driver->swapped(dc,begin,end,ok);}
}
int initializeRebuild64LaunchCheck(int argc,char** argv)
{
    QString file;
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--rebuild64-launch-check")==0&&i+1<argc)file=QString::fromLocal8Bit(argv[++i]);
    if(file.isEmpty())for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--ai-read-worker")==0)file=qEnvironmentVariable("ATAMAN_REBUILD64_LAUNCH_CASE");
    if(file.isEmpty())return 0;
    const QFileInfo info(file);caseRoot=info.canonicalPath();const QString normalized=QDir::fromNativeSeparators(caseRoot).toLower();
    if((!normalized.contains("/work/rebuild64-launch-287-")&&!normalized.contains("/work/rebuild64-offscreen-288-"))||QDir::currentPath().compare(caseRoot,Qt::CaseInsensitive)!=0)return 97;
    QFile input(info.absoluteFilePath());if(!input.open(QIODevice::ReadOnly))return 97;
    configuration=QJsonDocument::fromJson(input.readAll()).object();if(configuration["schema"].toInt()!=287)return 97;
    if(QDir::cleanPath(qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR")).compare(QDir(caseRoot).filePath("settings"),Qt::CaseInsensitive)!=0)return 97;
    isolatedSharedCache=configuration.value("sharedCache").toBool();
    knownFolderRoot=QDir::fromNativeSeparators(QDir::cleanPath(configuration["cacheRoot"].toString()));
    if(!knownFolderRoot.startsWith(QFileInfo(caseRoot).absolutePath()+"/",Qt::CaseInsensitive))return 97;
    writableRoot=QFileInfo(caseRoot).absolutePath();wchar_t shortRoot[32768]{};GetShortPathNameW(reinterpret_cast<LPCWSTR>(writableRoot.utf16()),shortRoot,32768);writableShortRoot=QDir::fromNativeSeparators(QString::fromWCharArray(shortRoot));
    if(!patchCoreImport("CreateFileW",reinterpret_cast<void*>(&guardedCreate),reinterpret_cast<void**>(&originalCreateFile))
       ||!patchCoreImport("MoveFileExW",reinterpret_cast<void*>(&guardedMove),reinterpret_cast<void**>(&originalMoveFile))
       ||!patchCoreImport("DeleteFileW",reinterpret_cast<void*>(&guardedDelete),reinterpret_cast<void**>(&originalDeleteFile))
       ||!patchCoreImport("MoveFileW",reinterpret_cast<void*>(&guardedMoveSimple),reinterpret_cast<void**>(&originalMoveSimple)))return 98;
    patchCoreImport("ReplaceFileW",reinterpret_cast<void*>(&guardedReplace),reinterpret_cast<void**>(&originalReplaceFile));
    SetErrorMode(SetErrorMode(0)|SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    auto core=GetModuleHandleW(L"Qt5Core.dll");auto shell=GetModuleHandleW(L"shell32.dll");
    if(!processLocalJump(reinterpret_cast<void*>(GetProcAddress(core,"_ZN14QStandardPaths16writableLocationENS_16StandardLocationE")),reinterpret_cast<void*>(&isolatedWritable))
       ||!processLocalJump(reinterpret_cast<void*>(GetProcAddress(shell,"SHGetKnownFolderPath")),reinterpret_cast<void*>(&isolatedKnownFolder)))return 98;
    PWSTR probe=nullptr;
    const auto hr=SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&probe);
    const QString verified=SUCCEEDED(hr)&&probe?QString::fromWCharArray(probe):QString();CoTaskMemFree(probe);
    if(!QDir::fromNativeSeparators(verified).startsWith(knownFolderRoot,Qt::CaseInsensitive))return 98;
    return 0;
}
void installRebuild64LaunchCheck(QApplication& app)
{if(!caseRoot.isEmpty())driver=new CRebuild64LaunchCheck(app);}
void attachRebuild64LaunchCheck(MainWindow& window)
{if(driver)driver->attach(window);}

void markRebuild64LaunchWindowReady(){if(driver)driver->windowReady();}

void applyRebuild64LaunchFixture(CSettings& settings) {
    if(caseRoot.isEmpty()||!settings.hasConfiguredResources())return;
    const auto privatePack=configuration["privateTerrainPack"].toString();
    if(privatePack.isEmpty()||!QFileInfo(privatePack).isFile()||!QDir::fromNativeSeparators(privatePack).startsWith(writableRoot+"/",Qt::CaseInsensitive))qFatal("Private terrain fixture is outside diagnostic workspace");
    auto paths=configuredResourceSourcePaths(&settings);int replaced=0;
    for(auto& path:paths)if(QFileInfo(path).fileName().compare("HDPack_Terrain.res",Qt::CaseInsensitive)==0){path=privatePack;++replaced;}
    if(replaced<1)qFatal("Expected exactly one terrain resource fixture");
    paths.removeAll(privatePack);paths.prepend(privatePack);
    if(configuration["fullyFrozenResources"].toBool())for(const auto& path:paths)
        if(!QDir::fromNativeSeparators(path).startsWith(writableRoot+"/",Qt::CaseInsensitive))qFatal("Non-private resource mount in fully frozen launch test");
    // CMapCatalog still expects directories. Keep them after explicit archive
    // priority; the general resource enumerator deduplicates identical files.
    QStringList catalogs;for(const auto& path:paths)if(QFileInfo(path).fileName().compare("texts.res",Qt::CaseInsensitive)==0)catalogs.append(QFileInfo(path).absolutePath());
    for(const auto& directory:catalogs)if(!paths.contains(directory))paths.append(directory);
    dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue(paths.toVector());
}

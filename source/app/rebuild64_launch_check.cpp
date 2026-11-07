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
#include "resourcemanager.h"
#include <atomic>
#include <QtWidgets>
#include <QtNetwork>
#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
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
SwapBuffersFunction originalSwap=nullptr;
BOOL WINAPI measuredSwap(HDC dc){const auto begin=qpc();const auto ok=originalSwap(dc);const auto end=qpc();glPresented(dc,begin,end,ok);return ok;}
bool installSwapTrace(){
 auto module=reinterpret_cast<unsigned char*>(GetModuleHandleW(L"qwindows.dll"));if(!module)return false;
 auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(module+dos->e_lfanew);
 auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
 for(;descriptor->Name;++descriptor){if(!descriptor->OriginalFirstThunk)continue;
  auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(module+descriptor->OriginalFirstThunk);auto* thunkSlots=reinterpret_cast<IMAGE_THUNK_DATA64*>(module+descriptor->FirstThunk);
  for(;names->u1.AddressOfData;++names,++thunkSlots){if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
   auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(module+names->u1.AddressOfData);
   if(std::strcmp(reinterpret_cast<const char*>(name->Name),"SwapBuffers"))continue;
   DWORD old=0;if(!VirtualProtect(&thunkSlots->u1.Function,sizeof(void*),PAGE_READWRITE,&old))return false;
   originalSwap=reinterpret_cast<SwapBuffersFunction>(thunkSlots->u1.Function);thunkSlots->u1.Function=reinterpret_cast<ULONG_PTR>(&measuredSwap);
   DWORD ignored=0;VirtualProtect(&thunkSlots->u1.Function,sizeof(void*),old,&ignored);return true;
  }
 }return false;
}
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
        window->resize(2560,1440);
        connect(view,&CView::updateMsg,this,[this](const QString& text){record("message",{{"text",text}});});
        connect(native,&CRebuild64Viewport::backendFailure,this,[this](const QString& text){++failures;record("backend_failure",{{"text",text}});});
        connect(native,&CRebuild64Viewport::documentTransitionStarted,this,[this](quint64 generation){record("transition_begin",{{"retiredBefore",QString::number(generation)}});});
        connect(native,&CRebuild64Viewport::frameCommitted,this,[this](quint64 generation){
            if(generation==lastGeneration)return;
            lastGeneration=generation;
            const auto s=native->statistics();
            QSaveFile shown(caseRoot+"/qt-page-shown-"+QString::number(generation)+".json");if(shown.open(QIODevice::WriteOnly)){shown.write(QJsonDocument(QJsonObject{{"renderer","Rebuild64"},{"neighbours_accepted",int(s.neighbours_accepted)},{"exeSha256",exeSha},{"dllSha256",dllSha},{"qpc",QString::number(qpc())}}).toJson());shown.commit();}
            record("first_frame",{{"generation",QString::number(generation)},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"map",CLandscape::getInstance()->filePath().absoluteFilePath()},
                {"neighbours_accepted",int(s.neighbours_accepted)},{"neighbours_pending",int(s.neighbours_pending)},
                {"commandQpc",QString::number(commandQpc)},{"exeSha256",exeSha},{"dllSha256",dllSha}});
        });
        exeSha=fileHash(QCoreApplication::applicationFilePath());dllSha=fileHash(QDir(QCoreApplication::applicationDirPath()).filePath("ataman_rebuild64.dll"));
    }
    void windowReady(){record("interactive",{{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"},{"exeSha256",exeSha},{"dllSha256",dllSha}});QTimer::singleShot(100,this,[this]{startWhenIdle();});}
    void swapped(HDC dc,qint64 begin,qint64 end,BOOL ok){
        if(!window||!view)return;
        if(WindowFromDC(dc)!=reinterpret_cast<HWND>(view->internalWinId()))return;
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
    void validateReady(const QString& name,bool gl=false)
    {
        if(gl) {
            check(wait([&]{return window->rebuild64DocumentReady()&&view->m_neighborLoadStage==0&&view->m_neighborLoadJobs==0&&!view->m_neighborObjectUploadScheduled;}),"GL load settled");
            check(!native->rebuildSelected(),"OpenGL selected");sawGL=true;
            int touching=0;for(const auto& item:view->m_neighborLandscapes)if(item.landscape&&item.landscape->isMprLoad()&&CNeighborMapLayout::touchesActiveMap({view->m_pLand->widthInWorldUnits(),view->m_pLand->heightInWorldUnits()},item.placement))++touching;
            const int expected=name.contains("zone7")?4:name.contains("zone1")?5:0;check(touching==expected,"GL touching neighbours exact "+QString::number(expected));

            record("ready",{{"name",name},{"renderer","OpenGL"},{"neighbours_accepted",touching},{"legacyNeighbours",view->m_neighborLandscapes.size()},{"neighbourSource","CView/GL"},{"nativeLoaded",native->backendLoaded()},{"exeSha256",exeSha},{"dllSha256",dllSha}});
            QString error;auto image=view->captureTopdownPass(ETopdownPass::None,QSize(960,640),{},&error);
            check(!image.isNull(),"GL own FBO readback: "+error);image.save(caseRoot+"/"+name+"-gl.png");
            return;
        }
        check(wait([&]{return native->frameIsCurrent()||!native->lastFailure().isEmpty();}),"native load reached terminal state");
        check(native->rebuildSelected(),"Rebuild64 selected: "+native->lastFailure());
        const int expected=name.contains("zone7")?4:name.contains("zone1")?5:0;
        const auto stats=native->statistics();
        check(int(stats.neighbours_accepted)==expected&&stats.neighbours_pending==0,"native neighbours exact "+QString::number(expected));
        if(!sawGL) {
            check(native->editorQueryReady(),"shared query geometry ready");
            check(view->m_neighborLandscapes.size()==expected,"only touching GL neighbours loaded");
        }
        record("ready",{{"name",name},{"renderer","Rebuild64"},{"neighbours_accepted",int(stats.neighbours_accepted)},{"legacyNeighbours",view->m_neighborLandscapes.size()},{"width",int(stats.width)},{"height",int(stats.height)},{"exeSha256",exeSha},{"dllSha256",dllSha}});
        const auto path=caseRoot+"/"+name+"-native.png";QFile::remove(path);bool done=false;QString error;quint64 captured=0;
        const auto connection=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& output,const QString& failure,quint64 generation){if(output==path){done=true;error=failure;captured=generation;}});
        native->capture(path);const bool finished=wait([&]{return done;},15000);disconnect(connection);
        check(finished&&error.isEmpty()&&captured==native->statistics().ready_generation&&QFileInfo(path).size()>0,"fresh native own readback from current Present: "+error);
    }
    void openPair(const QString& map)
    {
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
    void worldDoubleClick() {
        SNeighborMapPlacement target;
        for(const auto& p:view->m_neighborPlacements)if(QFileInfo(p.mprPath).completeBaseName()=="zone7"){target=p;break;}
        check(!target.mprPath.isEmpty(),"zone7 is in the real world layout");
        const QVector3D center(float(target.origin.x()+target.size.width()/2),float(target.origin.y()+target.size.height()/2),0);
        QMatrix4x4 camera;camera.lookAt(center+QVector3D(0,0,800),center,QVector3D(0,1,0));view->m_cam->setPreviewView(camera);
        view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,float(native->width())/qMax(1,native->height()),1,2000);view->m_projection=view->m_normalProjection;
        QElapsedTimer settle;settle.start();wait([&]{return settle.elapsed()>150;},1000);
        const auto point=native->rect().center();commandQpc=qpc();
        QMouseEvent dbl(QEvent::MouseButtonDblClick,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(native->findChild<QWidget*>("rebuild64NativeSurface"),&dbl);
        check(wait([&]{return CLandscape::getInstance()->filePath().completeBaseName()=="zone7";},240000),"native double click took the canonical neighbour route");
        validateReady("zone7-world");view->m_cam->clearPreviewView();
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
    QJsonObject operation(QJsonObject result)
    {
        check(!result.contains("error"),"MCP request accepted: "+result.value("error").toString());
        const auto id=result["operation"].toString();if(id.isEmpty())return result;
        QElapsedTimer poll;poll.start();
        check(wait([&]{if(poll.elapsed()<50)return false;poll.restart();result=request("operation_status",{{"operation",id}});return result["status"].toString()!="running";},240000),"MCP operation terminal");
        record("mcp_result",result);check(!result.contains("error"),"MCP operation success");
        if(result["result"].isObject())result=result["result"].toObject();
        check(!result.contains("error"),"MCP operation payload success: "+result.value("error").toString());return result;
    }
    void key(int value,const QString& text={}) {
        auto* target=native->findChild<QWidget*>("rebuild64NativeSurface");
        QKeyEvent press(QEvent::KeyPress,value,Qt::NoModifier,0,value==Qt::Key_Return?VK_RETURN:value,0,text);QApplication::sendEvent(target,&press);
        QKeyEvent release(QEvent::KeyRelease,value,Qt::NoModifier,0,value==Qt::Key_Return?VK_RETURN:value,0,text);QApplication::sendEvent(target,&release);
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
            if(mode=="recover-create") {recoveryCreate();}
            else if(mode=="recover-restore") {recoveryRestore();}
            else if(mode=="empty"||mode=="choice"||mode=="guard"||mode=="cold-switch") {
                check(!CLandscape::getInstance()->isMprLoad(),"no startup map");
                const bool expected=mode!="guard"&&configuration["expectedNative"].toBool(true);
                check(native->rebuildSelected()==expected,"startup preference/guard");
                if(mode=="cold-switch") {
                    openPair(map);
                    check(wait([&]{QFile trace(caseRoot+"/presented.jsonl");return trace.open(QIODevice::ReadOnly)&&trace.readAll().contains("shader_prepare_begin");},30000),"real shader preparation entered");
                    QFile trace(caseRoot+"/presented.jsonl");trace.open(QIODevice::ReadOnly);const bool running=!trace.readAll().contains("shader_prepare_end");
                    const auto before=qpc();native->m_glAction->trigger();record("shader_switch_gl",{{"ticks",QString::number(qpc()-before)},{"shaderStillRunning",running}});
                    if(configuration["failure"].toString()=="shader-hold")check(running,"injected blocking shader preparation was still running at the menu action");
                    check(!native->rebuildSelected(),"menu switched while shader work running");
                    QElapsedTimer settle;settle.start();wait([&]{return settle.elapsed()>1000;},2000);
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
            } else if(mode=="session-create") {openPair(map);validateReady(map);view->saveSession();check(QFileInfo(QDir::tempPath()+"/ei_mapper_endurance_session.json").size()>0,"real session journal written");}
            else if(mode=="session-restore") {check(CLandscape::getInstance()->isMprLoad(),"real main restored startup session");commandQpc=configuration["processStartQpc"].toString().toLongLong();validateReady(map);window->closeAll();}
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
                auto snapshot=operation(request("window_snapshot"));check(!snapshot.contains("error"),"MCP snapshot");
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
            record("scenario_complete",{{"success",true},{"maxQtGapTicks",QString::number(maxGap)},{"nativeLoaded",native->backendLoaded()}});
        }catch(const QString& error) {record("complete",{{"success",false},{"error",error}});exitCode=1;}
        if(socket)socket->disconnectFromServer();pulse.stop();
        if(!exitCode){window->close();if(!wait([&]{return window->m_closeReady&&!window->view();},125000)){record("complete",{{"success",false},{"error","Real MainWindow close did not finish"}});exitCode=1;}
            else record("complete",{{"success",deniedWrites.load()==0},{"realClose",true},{"maxQtGapTicks",QString::number(maxGap)}});
        }
        QCoreApplication::exit(exitCode);
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
    QFile log;MainWindow* window=nullptr;CView* view=nullptr;CRebuild64Viewport* native=nullptr;
    QTimer pulse;qint64 lastPulse=0,maxGap=0,commandQpc=0;quint64 lastGeneration=0;
    bool sawGL=false,swapTraceInstalled=false,creating=false;QSet<QString> glCaptured;QString exeSha,dllSha,nextFile,nextCreated;int failures=0,unexpectedDialogs=0,requestId=0;
    QLocalSocket* socket=nullptr;QByteArray buffer;
};
namespace {CRebuild64LaunchCheck* driver=nullptr;
void glPresented(HDC dc,qint64 begin,qint64 end,BOOL ok){if(driver)driver->swapped(dc,begin,end,ok);}
}
int initializeRebuild64LaunchCheck(int argc,char** argv)
{
    QString file;
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--rebuild64-launch-check")==0&&i+1<argc)file=QString::fromLocal8Bit(argv[++i]);
    if(file.isEmpty())for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--ai-read-worker")==0)file=qEnvironmentVariable("ATAMAN_REBUILD64_LAUNCH_CASE");
    if(file.isEmpty())return 0;
    const QFileInfo info(file);caseRoot=info.canonicalPath();const QString normalized=QDir::fromNativeSeparators(caseRoot).toLower();
    if(!normalized.contains("/work/rebuild64-launch-287-")||QDir::currentPath().compare(caseRoot,Qt::CaseInsensitive)!=0)return 97;
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
    // CMapCatalog still expects directories. Keep them after explicit archive
    // priority; the general resource enumerator deduplicates identical files.
    QStringList catalogs;for(const auto& path:paths)if(QFileInfo(path).fileName().compare("texts.res",Qt::CaseInsensitive)==0)catalogs.append(QFileInfo(path).absolutePath());
    for(const auto& directory:catalogs)if(!paths.contains(directory))paths.append(directory);
    dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue(paths.toVector());
}

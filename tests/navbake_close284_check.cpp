// Hidden process only. Patch this process's Qt known-folder lookup before Qt
// initializes; owner AppData/ProgramData and mapper instances are never used.
#include <QtWidgets>
#include <QtTest>
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#define private public
#define protected public
#include "main_window.h"
#include "view.h"
#include "navmesh_panel.h"
#include "ai_agent_server.h"
#undef protected
#undef private
#include "resource_setup_dialog.h"
#include "log.h"

namespace {
QString root;int passes=0,visible=0;QTextStream out(stdout);
void check(bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!ok)throw text;++passes;}
QString writable(QStandardPaths::StandardLocation loc){return root+"/known-folders/"+QString::number(int(loc));}
void isolate(){
    const auto entry=GetProcAddress(GetModuleHandleW(L"Qt5Core.dll"),"_ZN14QStandardPaths16writableLocationENS_16StandardLocationE");
    if(!entry)throw QString("Qt isolation export unavailable");
    unsigned char jump[12]={0x48,0xb8};const auto target=reinterpret_cast<quint64>(&writable);std::memcpy(jump+2,&target,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old,ignored;if(!VirtualProtect(reinterpret_cast<void*>(entry),12,PAGE_EXECUTE_READWRITE,&old))throw QString("isolation failed");
    std::memcpy(reinterpret_cast<void*>(entry),jump,12);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),12);VirtualProtect(reinterpret_cast<void*>(entry),12,old,&ignored);
}
struct Hidden:QObject {bool eventFilter(QObject* obj,QEvent* e)override{
    if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(obj))if(w->isWindow()){
        w->setAttribute(Qt::WA_DontShowOnScreen);w->setAttribute(Qt::WA_ShowWithoutActivating);
    }return false;
}};
struct FilterProbeWindow:MainWindow {
    int unrelatedEvents=0;
    bool eventFilter(QObject* watched,QEvent* event)override{
        if(event->type()==QEvent::User)++unrelatedEvents;
        return MainWindow::eventFilter(watched,event);
    }
};
BOOL CALLBACK inspect(HWND hwnd,LPARAM){DWORD pid;GetWindowThreadProcessId(hwnd,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(hwnd))++visible;return TRUE;}
bool wait(const std::function<bool()>& done,int ms=180000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
LONG WINAPI crash(EXCEPTION_POINTERS* info){
    const auto path=root+"/crash-stack.txt";FILE* file=_wfopen(reinterpret_cast<const wchar_t*>(path.utf16()),L"wb");
    if(!file)return EXCEPTION_EXECUTE_HANDLER;
    auto context=*info->ContextRecord;STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;frame.AddrFrame.Offset=context.Rbp;
    frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
    SymInitialize(GetCurrentProcess(),nullptr,TRUE);std::fprintf(file,"exception %08lx\n",info->ExceptionRecord->ExceptionCode);
    for(int i=0;i<24&&frame.AddrPC.Offset;++i){
        HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<const wchar_t*>(frame.AddrPC.Offset),&module);
        wchar_t name[MAX_PATH]{};GetModuleFileNameW(module,name,MAX_PATH);char utf8[MAX_PATH*4]{};WideCharToMultiByte(CP_UTF8,0,name,-1,utf8,sizeof(utf8),nullptr,nullptr);
        std::fprintf(file,"%s + 0x%llx pc 0x%llx\n",utf8,frame.AddrPC.Offset-reinterpret_cast<quint64>(module),frame.AddrPC.Offset);std::fflush(file);
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,GetCurrentProcess(),GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }
    std::fclose(file);return EXCEPTION_EXECUTE_HANDLER;
}
void childGone(qint64 pid){HANDLE p=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));check(!p||WaitForSingleObject(p,0)==WAIT_OBJECT_0,"owned baker exited");if(p)CloseHandle(p);}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAVBAKE_CASE"));if(root.isEmpty()||qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))return 2;
    SetUnhandledExceptionFilter(crash);
    isolate();QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    app.setOrganizationName("Ataman isolated navbake284");app.setApplicationName("navbake-close-check");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
    app.setProperty("workspaceLoadSmoke",true);Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");
    const auto mode=app.arguments().value(1);const auto game=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAVBAKE_GAME"));
    const auto maps=mode.startsWith("real-")?root+"/mod/Maps":game+"/MODS/Dejavu/Maps";
    const auto configure=[&](CNavmeshPanel* p){p->setScene(maps+"/a.mpr",maps+"/a.mob",game,maps);p->setBakerPath(mode.startsWith("real-")?game+"/Engine64/EIRebuild64NavBake.exe":app.applicationDirPath()+"/mock.exe");};
    try {
        check(root.contains("/work/navbake-close-284-"),"owned writable root");
        if(mode=="queue-after-finish"){
            CNavmeshPanel panel;configure(&panel);check(panel.startBake(true,root+"/output"),"start baseline journal");
            check(wait([&]{return !panel.bakeActive();},10000)&&panel.lastBakeSucceeded(),"baseline success");
            const auto previous=panel.m_reportRoot+"/bake.log";const auto saved=bytes(previous);
            panel.requestBakeOnSave(root+"/output");panel.interruptBakeForShutdown(QStringLiteral("закрытие без диалога (очередь)"));
            check(!saved.isEmpty()&&bytes(previous)==saved,"queued interruption preserves previous successful journal");
            check(panel.m_reportRoot+"/bake.log"!=previous&&QString::fromUtf8(bytes(panel.m_reportRoot+"/bake.log")).contains(QStringLiteral("ещё не начата")),"queued interruption gets separate truthful journal");
        }else if(mode=="destroy"||mode=="cancel"){
            auto* panel=new CNavmeshPanel;configure(panel);qputenv("NAVBAKE_TEST_LONG_LOG","1");qputenv("NAVBAKE_TEST_DELAY","10000");
            check(panel->startBake(true,root+"/output"),"start destructor case");
            check(wait([&]{return panel->m_currentBakeMap=="a / a"&&panel->bakeLog().contains("DETAIL 2099");},10000),"long output accumulated");
            const auto report=panel->m_reportRoot;const auto pid=panel->m_process->processId();int callbacks=0;
            if(mode=="cancel"){panel->cancelBake();check(wait([&]{return !panel->bakeActive();},10000),"ordinary panel cancellation finishes");}
            QObject::connect(panel,&CNavmeshPanel::contentChanged,[&]{++callbacks;});delete panel;
            const auto log=QString::fromUtf8(bytes(report+"/bake.log"));
            check(callbacks==0,"no contentChanged during destruction (269)");
            check(log.contains("DETAIL 0\n")&&log.contains("DETAIL 2099")&&log.contains("stdout remainder a")&&log.contains("stderr remainder a"),"full transcript beyond 2000 UI blocks and both output tails");
            check(log.contains(mode=="cancel"?QStringLiteral("Отмена в панели"):QStringLiteral("уничтожение панели"))&&log.contains("a / a")&&log.contains(QStringLiteral("0 из 3")),"interruption reason and last map/progress");childGone(pid);
        }else{
            QTimer setup;QObject::connect(&setup,&QTimer::timeout,[]{for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
            FilterProbeWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.resize(900,700);window.show();
            QObject unrelated;QEvent ordinaryEvent(QEvent::User);QCoreApplication::sendEvent(&unrelated,&ordinaryEvent);
            check(window.unrelatedEvents==0,"Quit guard does not invoke the widget filter for unrelated objects");
            auto* panel=window.view()->navmeshPanel();configure(panel);
            if(mode=="plain"){
                check(window.close()==false,"ordinary close starts existing async shutdown");check(wait([&]{return window.m_closeReady&&!window.view();},15000),"ordinary shutdown completes without bake question");
            }else{
                if(mode.startsWith("queued"))panel->requestBakeOnSave(root+"/output");
                else if(mode.startsWith("real-")){
                    auto* button=panel->findChild<QPushButton*>(mode.contains("single")?"navmeshBakeMap":"navmeshBakeAll");
                    check(button&&button->isEnabled(),"real production bake button enabled");button->click();
                    check(panel->busy(),"actual bake button launches real baker into private mod/output root");
                }
                else check(panel->startBake(!mode.contains("single"),root+"/output"),"start bake via production panel");
                if(mode!="queued"&&mode!="queued-stop")check(wait([&]{return mode.startsWith("real-")&&!mode.contains("single")?panel->m_mapsCompleted>=1&&panel->busy():panel->m_mapsTotal>0&&!panel->m_currentBakeMap.contains(QStringLiteral("не начата"));},90000),"mid-batch MAP_BEGIN/progress observed");
                auto report=panel->m_reportRoot;const auto pid=panel->m_process->processId();
                bool sawQuestion=false,defaultWait=false,deadlineUntouched=false,updated=false;QString initial;
                const bool stop=mode.endsWith("stop"),finish=mode.endsWith("finish"),headless=mode=="headless"||mode=="mcp-headless",session=mode=="session",quit=mode=="quit";
                QTimer driver;QObject::connect(&driver,&QTimer::timeout,[&]{
                    EnumWindows(inspect,0);
                    auto* box=window.findChild<QMessageBox*>("navBakeCloseQuestion");if(!box)return;
                    sawQuestion=true;defaultWait=box->defaultButton()&&box->defaultButton()->objectName()=="navBakeCloseWait";
                    deadlineUntouched=!window.m_shutdownInProgress&&!window.m_closeWaiting&&!window.m_closeReady;
                    if(initial.isEmpty())initial=box->text();else updated|=initial!=box->text();
                    if(finish)return;
                    if(mode=="escape")QTest::keyClick(box,Qt::Key_Escape);
                    else if(mode=="x")box->close();
                    else if(stop)box->findChild<QPushButton*>("navBakeCloseStop")->click();
                    else box->findChild<QPushButton*>("navBakeCloseWait")->click();
                });driver.start(20);
                if(headless)app.setProperty("agentHeadless",true);
                bool closed=false;
                if(session){
                    // Exercise the actual Qt Windows session callback without ending Windows.
                    SendMessageW(reinterpret_cast<HWND>(window.winId()),WM_QUERYENDSESSION,0,ENDSESSION_LOGOFF);
                }else if(quit){QCoreApplication::postEvent(qApp,new QEvent(QEvent::Quit));check(wait([&]{return sawQuestion;},10000),"QApplication quit event reaches bake gate");}
                else if(mode=="mcp-headless"){
                    QSettings authority(root+"/authority.ini",QSettings::IniFormat);authority.setValue("ui/aiAgentMode",1);authority.sync();
                    app.setProperty("agentAuthorityFile",authority.fileName());app.setProperty("agentAuthorityFormat",int(QSettings::IniFormat));
                    {QSignalBlocker block(window.m_aiAgent->m_mode);window.m_aiAgent->m_mode->setCurrentIndex(1);}
                    window.m_aiAgent->m_committing=true;
                    check(window.m_aiAgent->dispatch("shutdown_headless",{}).contains("error"),"unchanged MCP guard refuses active commit");window.m_aiAgent->m_committing=false;
                    const auto reply=window.m_aiAgent->dispatch("shutdown_headless",{});
                    check(reply.value("status").toString()=="shutting_down","unchanged shutdown_headless permits bake-only busy state");
                    check(wait([&]{return !window.view();},20000),"MCP headless closes without dialog");
                }else if(mode=="native-wait"){
                    // WA_DontShowOnScreen does not dispatch the default native
                    // SC_CLOSE action. Feed the native callback, then Qt Close.
                    MSG message{};message.message=WM_SYSCOMMAND;message.wParam=SC_CLOSE;long result=0;
                    window.nativeEvent("windows_generic_MSG",&message,&result);closed=window.close();
                }
                else closed=window.close();
                if(mode=="queued-stop")report=panel->m_reportRoot;
                if(headless||session){check(!sawQuestion,"unattended close has no question");}
                else check(sawQuestion&&defaultWait&&deadlineUntouched,"question/default wait precedes shutdown/deadlines");
                driver.stop();
                if(!stop&&!finish&&!headless&&!session){
                    check(!closed&&window.view()&&!window.m_shutdownInProgress,"wait/Esc/X retains live mapper");
                    check(wait([&]{return !panel->bakeActive();}),"batch completes after close was cancelled");
                    check(panel->lastBakeSucceeded(),"successful BAKE_DONE/exit0 accepted normally");
                    check(!bytes(panel->m_reportRoot+"/bake.log").isEmpty(),"successful bake.log persisted");
                    const auto logPath=panel->m_reportRoot+"/bake.log";const auto before=bytes(logPath);panel->interruptBakeForShutdown("must not rewrite");check(bytes(logPath)==before,"successful journal not overwritten");
                    if(pid)childGone(pid);
                }else if(finish){
                    check(updated,"question text refreshes while baking");check(wait([&]{return !window.view();},20000),"finished batch dismisses question and continues close");
                    check(QString::fromUtf8(bytes(report+"/bake.log")).contains("BAKE_DONE"),"finish during question preserves normal journal");childGone(pid);
                }else{
                    check(!bytes(report+"/bake.log").isEmpty(),"interrupted bake.log persisted");const auto log=QString::fromUtf8(bytes(report+"/bake.log"));
                    check(log.contains(QStringLiteral("Пакет прерван"))&&log.contains(QStringLiteral("Последняя начатая карта")),"interrupted last-map/progress receipt");childGone(pid);
                    if(!session)check(wait([&]{return !window.view();},20000),"interruption continues normal close");
                }
                app.setProperty("agentHeadless",false);
            }
        }
        EnumWindows(inspect,0);check(visible==0,"zero visible owned windows");
        out<<"RESULT "<<passes<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"ERROR "<<error<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";out.flush();return 1;}
}

#include <QtWidgets>
#include <QtTest>
#include <QtNetwork>
#include <functional>
#include "main_window.h"
#include "ai_agent_server.h"
#include "view.h"
#include "landscape.h"
#include "resource_setup_dialog.h"

namespace {
QTextStream out(stdout);int checks=0;
void check(bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<"\n";out.flush();if(!ok)throw text;++checks;}
bool until(const std::function<bool()>& done,int ms=15000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
QJsonObject read(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(f.readAll()).object():QJsonObject{};}
void write(const QString& path,const QJsonObject& value){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(value).toJson())<0||!f.commit())throw QString("fixture write: ")+path;}
struct Hidden:QObject{bool eventFilter(QObject*,QEvent* e)override{if(e->type()==QEvent::Polish||e->type()==QEvent::Show)for(auto* w:QApplication::topLevelWidgets())w->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
class Mapper {
    QProcess p;QString root;int serial=0;
public:
    explicit Mapper(QString folder):root(folder){QDir().mkpath(root);}
    ~Mapper(){if(p.state()!=QProcess::NotRunning){p.kill();p.waitForFinished();}}
    void start(){QFile::remove(root+"/state.json");QFile::remove(root+"/command.json");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("EI_MCP_PERSISTENT_CHILD",root);p.setProcessEnvironment(env);
        p.setStandardOutputFile(root+"/stdout.txt",QIODevice::Append);p.setStandardErrorFile(root+"/stderr.txt",QIODevice::Append);
        p.start(QCoreApplication::applicationFilePath(),{"--assembly-catalog-check"});check(p.waitForStarted(),"new owned mapper process starts");
        check(until([&](){return !state().isEmpty()||p.state()==QProcess::NotRunning;},60000)&&p.state()!=QProcess::NotRunning,"production MainWindow ready before opening a map");}
    QJsonObject state(){return read(root+"/state.json");}
    QJsonObject command(QJsonObject c={}){const int n=++serial;c["serial"]=n;write(root+"/command.json",c);
        check(until([&](){return state()["serial"].toInt()==n;}),"human UI command acknowledged");return state();}
    void close(){command({{"quit",true}});check(until([&](){return p.state()==QProcess::NotRunning;},30000)&&p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0,"mapper closes normally, no forced termination");}
};
class Client {
    QProcess p;QByteArray buffer;int serial=0;
public:
    explicit Client(QString exe){p.start(exe,QStringList{});check(p.waitForStarted(),"owned stdio bridge starts");
        check(call("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","persistent-check"},{"version","1"}}}}).contains("result"),"MCP handshake");
        p.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");}
    ~Client(){p.closeWriteChannel();if(!p.waitForFinished(3000)){p.kill();p.waitForFinished();}}
    qint64 pid()const{return p.processId();}
    QJsonObject call(QString method,QJsonObject params={}){const auto id=QString::number(++serial);
        p.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        while(true){check(until([&](){buffer+=p.readAllStandardOutput();return buffer.contains('\n');},35000),"MCP response received");
            QJsonObject r;QString error;check(ai::takeFrame(buffer,&r,&error),"valid MCP frame");if(r["method"]=="notifications/tools/list_changed")continue;
            check(r["id"]==id,"MCP response correlated");return r;}}
    QJsonObject tool(QString name,QJsonObject args={}){const auto r=call("tools/call",{{"name",name},{"arguments",args}})["result"].toObject();
        return QJsonDocument::fromJson(r["content"].toArray()[0].toObject()["text"].toString().toUtf8()).object();}
};
}

int runAssemblyCatalogCheck()
{
    try {
        out.setCodec("UTF-8");
        const auto child=qEnvironmentVariable("EI_MCP_PERSISTENT_CHILD");
        if(!child.isEmpty()) {
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,child+"/settings");
            QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,child+"/system-settings");
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
            Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
            QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
            MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
            auto* server=window.m_aiAgent;auto* mode=window.findChild<QComboBox*>("aiAgentMode");auto* save=window.findChild<QCheckBox*>("aiAgentAllowSave");
            int accepts=0,events=0;
            QObject::connect(&server->m_server,&QLocalServer::newConnection,&window,[&](){++accepts;});
            QObject::connect(server->m_events,&QPlainTextEdit::textChanged,&window,[&](){++events;});
            const auto state=[&](int serial){int authenticated=0;for(auto* s:server->m_clients.keys())authenticated+=s->property("authenticated").toBool();
                write(child+"/state.json",{{"serial",serial},{"mode",mode->currentIndex()},{"allowSave",save->isChecked()},{"saveEnabled",save->isEnabled()},
                    {"listening",server->enabled()},{"mapOpen",window.view()&&window.view()->m_pLand->isMprLoad()},
                    {"clients",authenticated},{"accepts",accepts},{"events",events},{"dock",server->m_status->text()},
                    {"status",server->m_statusBar?server->m_statusBar->text():QString()},{"pid",double(QCoreApplication::applicationPid())}});};
            state(0);int last=0;QElapsedTimer deadline;deadline.start();
            while(deadline.elapsed()<180000){QTest::qWait(10);const auto c=read(child+"/command.json");const int serial=c["serial"].toInt();if(serial<=last)continue;last=serial;
                if(c.contains("mode"))mode->setCurrentIndex(c["mode"].toInt());
                if(c.contains("allowSave"))save->setChecked(c["allowSave"].toBool());
                if(c["offButton"].toBool())window.findChild<QPushButton*>("aiAgentDisconnect")->click();
                state(last);if(c["quit"].toBool()){window.close();return 0;}}
            throw QString("owned child command deadline");
        }
        const auto root=qEnvironmentVariable("EI_MCP_TEST_ROOT");check(!root.isEmpty(),"explicit isolated root");
        Mapper mapper(root+"/mapper");mapper.start();auto s=mapper.state();
        check(s["mode"]==0&&!s["allowSave"].toBool()&&!s["listening"].toBool()&&!s["mapOpen"].toBool(),"first launch defaults Off and Save false, no open map");
        s=mapper.command({{"mode",2},{"allowSave",true}});check(s["listening"]==true&&s["allowSave"]==true&&s["saveEnabled"]==true,"human enables Edit and Save once");
        Client a(qEnvironmentVariable("EI_MCP_TEST_BRIDGE")),b(qEnvironmentVariable("EI_MCP_TEST_BRIDGE")),c(qEnvironmentVariable("EI_MCP_TEST_BRIDGE"));
        const auto apid=a.pid(),bpid=b.pid(),cpid=c.pid();QTest::qWait(1500);
        check(!a.tool("get_context").contains("error"),"first tool automatically selects sole mapper");
        check(!b.tool("connect_instance").contains("error"),"connect_instance accepts omitted instance");
        check(c.call("resources/read",{{"uri","ataman://context"}}).contains("result"),"resource auto-selects sole mapper");
        s=mapper.command();check(s["clients"]==3&&s["dock"]==s["status"]&&s["status"].toString().contains("3"),"dock and status bar show mode and three authenticated agents");
        const auto accepts=s["accepts"],events=s["events"];QTest::qWait(6500);s=mapper.command();
        check(s["accepts"]==accepts&&s["events"]==events&&s["clients"]==3,"223 short idle regression: three bridges, zero new streams or events");
        s=mapper.command({{"mode",1}});check(s["allowSave"]==true&&s["saveEnabled"]==false,"Read-only retains Save preference, control disabled");
        const auto readOnlyRevision=a.tool("get_revision");
        check(readOnlyRevision["allowSave"]==false,"Read-only reports effective Save disabled");
        check(a.tool("save",{{"revision",readOnlyRevision["revision"]}})["error"].toString().contains("Read-only"),"remembered Save grants no writes in Read-only");
        s=mapper.command({{"mode",2}});check(s["allowSave"]==true&&s["saveEnabled"]==true,"return to Edit restores remembered Save");
        check(!a.tool("get_revision").contains("error"),"tool reselects after mode change");
        mapper.close();QTest::qWait(2200);mapper.start();s=mapper.state();
        check(s["mode"]==2&&s["allowSave"]==true&&s["listening"]==true&&!s["mapOpen"].toBool(),"fresh process restores Edit and Save and listens before any map or human action");
        check(!a.tool("get_context").contains("error"),"first request after restart succeeds without human or explicit connect");
        check(!b.tool("get_revision").contains("error"),"second bridge reads revision after restart");
        check(a.tool("list_objects")["objects"].isArray(),"read tool works after restart in the empty editor");
        check(c.call("resources/read",{{"uri","ataman://context"}}).contains("result"),"third bridge reads context resource after restart");
        check(a.pid()==apid&&b.pid()==bpid&&c.pid()==cpid,"all idle bridge processes survive mapper restart");
        QTest::qWait(1200);s=mapper.command();check(s["clients"]==3,"three bridges reattached automatically");
        Mapper second(root+"/second");second.start();second.command({{"mode",1}});
        {Client ambiguous(qEnvironmentVariable("EI_MCP_TEST_BRIDGE"));
            check(ambiguous.tool("get_context")["error"].toString().contains("Multiple enabled mappers"),"automatic selection rejects multiple mappers clearly");
            check(ambiguous.tool("connect_instance")["error"].toString().contains("Multiple enabled mappers"),"no-argument connect rejects ambiguity");
            const auto list=ambiguous.tool("list_instances")["instances"].toArray();check(list.size()==2,"both isolated mappers listed");
            check(!ambiguous.tool("connect_instance",{{"instance",list[0].toObject()["instance"]}}).contains("error"),"explicit ID selects one of multiple mappers");}
        second.close();
        a.tool("disconnect_instance");check(a.tool("get_context")["error"].toString().contains("explicitly disconnected"),"explicit disconnect suppresses auto-selection");
        check(!a.tool("connect_instance").contains("error"),"explicit reconnect reenables selection");
        s=mapper.command({{"mode",0}});check(!s["listening"].toBool()&&s["allowSave"]==true&&!s["saveEnabled"].toBool(),"human Off stops server and retains inactive Save preference");
        mapper.close();mapper.start();s=mapper.state();
        check(s["mode"]==0&&!s["listening"].toBool()&&s["allowSave"]==true,"Off persists across process restart");
        check(a.tool("get_context").contains("error"),"agent cannot restore access after human Off");
        s=mapper.command({{"mode",2},{"allowSave",false}});check(s["listening"]==true&&s["allowSave"]==false,"human revokes Save");
        check(a.tool("get_revision")["allowSave"]==false,"Save revocation reaches agent");
        mapper.close();mapper.start();s=mapper.state();check(s["mode"]==2&&s["allowSave"]==false&&s["listening"]==true,"Save false also persists independently of Edit");
        mapper.command({{"mode",1}});mapper.close();mapper.start();s=mapper.state();
        check(s["mode"]==1&&s["listening"]==true&&!s["saveEnabled"].toBool(),"Read-only also restores across process restart");
        check(!a.tool("get_context").contains("error"),"restored Read-only permits context");
        mapper.command({{"offButton",true}});mapper.close();mapper.start();check(mapper.state()["mode"]==0&&!mapper.state()["listening"].toBool(),"Disconnect button persists human Off");mapper.close();
        out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    } catch(const QString& e){out<<"FAIL "<<e<<"\n";out.flush();return 1;}
}

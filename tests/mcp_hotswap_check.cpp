#include "ai_agent_protocol.h"
#include <QtNetwork>
#include <QtTest>
#include <functional>

namespace {
QTextStream out(stdout);int checks=0;
void check(bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++checks;}
bool until(const std::function<bool()>& done,int ms=10000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(5);return done();}
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw QString("fixture write ")+path;}
class Mapper : public QObject {
    QLocalServer server;QMap<QLocalSocket*,QByteArray> buffers;
public:
    QString id{QUuid::createUuid().toString(QUuid::WithoutBraces)},secret{QString(72,'s')};
    int generation{1};bool legacy{false},incompatible{false};int catalogReads{0};
    QJsonObject catalog() {
        auto c=ai::mcpCatalog();c["version"]=QString("future-%1").arg(generation);
        c["instructions"]=QString("mapper-owned-instructions-%1").arg(generation);
        auto tools=c["tools"].toArray();tools.append(QJsonObject{{"name",QString("future_tool_%1").arg(generation)},
            {"description","Tool absent from bridge fallback"},{"inputSchema",QJsonObject{{"type","object"}}}});c["tools"]=tools;
        auto resources=c["resources"].toArray();resources.append(QJsonObject{{"uri","ataman://future"},{"name","Future resource"}});c["resources"]=resources;
        c["resourceTemplates"]=QJsonArray{QJsonObject{{"uriTemplate","ataman://future/{id}"},{"name","Future template"}}};
        if(incompatible)c["protocolMajor"]=99;return c;
    }
    Mapper() {
        server.setSocketOptions(QLocalServer::UserAccessOption);check(server.listen("ei-ataman-"+id),"owned mapper pipe listens");
        QDir().mkpath(ai::discoveryDirectory());
        write(QDir(ai::discoveryDirectory()).filePath(id+".json"),QJsonDocument(QJsonObject{{"instance",id},{"secret",secret},
            {"pipe",server.serverName()},{"version","test-current-code"}}).toJson());
        QObject::connect(&server,&QLocalServer::newConnection,this,[this](){while(server.hasPendingConnections()){
            auto* s=server.nextPendingConnection();buffers[s]={};
            QObject::connect(s,&QLocalSocket::disconnected,this,[this,s](){buffers.remove(s);s->deleteLater();});
            QObject::connect(s,&QLocalSocket::readyRead,this,[this,s](){auto& b=buffers[s];b+=s->readAll();while(b.contains('\n')){
                QJsonObject r;QString error;check(ai::takeFrame(b,&r,&error),"local JSON frame");check(r["secret"]==secret,"local secret preserved");
                const auto m=r["method"].toString();QJsonObject result;
                if(m=="mcp_catalog"){++catalogReads;result=legacy?ai::error("Unknown read tool"):catalog();}
                else if(m=="get_context")result={{"version",QString("future-%1").arg(generation)}};
                else if(m=="mcp_resource_read")result={{"source","mapper"},{"instructions",catalog()["instructions"]}};
                else if(m==QString("future_tool_%1").arg(generation))result={{"forwarded",true}};
                else result=ai::error("Unknown read tool");
                s->write(QJsonDocument(QJsonObject{{"id",r["id"]},{"result",result}}).toJson(QJsonDocument::Compact)+'\n');s->flush();
            }});
        }});
    }
    ~Mapper(){for(auto* s:buffers.keys())s->disconnect(this);}
};
class Client {
    QProcess p;QByteArray buffer;int serial{0};
    bool frame(QJsonObject* value,int timeout=10000) {
        if(!until([&](){buffer+=p.readAllStandardOutput();return buffer.contains('\n')||p.state()==QProcess::NotRunning;},timeout)||!buffer.contains('\n'))return false;
        QString error;check(ai::takeFrame(buffer,value,&error),"valid MCP frame");return true;
    }
public:
    int notifications{0};
    Client(const QString& exe,bool initialize=true) {
        p.start(exe,QStringList{});check(p.waitForStarted(),"current task bridge starts");
        if(initialize){auto hello=call("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","test"},{"version","1"}}}});
            check(hello["result"].toObject()["capabilities"].toObject()["tools"].toObject()["listChanged"]==true,"tools.listChanged declared");
            p.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");}
    }
    ~Client(){p.closeWriteChannel();if(!p.waitForFinished(5000)){p.kill();p.waitForFinished();}}
    qint64 pid()const{return p.processId();}
    QJsonObject call(const QString& method,QJsonObject params={}) {
        const auto id=QString::number(++serial);p.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        QJsonObject r;while(frame(&r)){if(r["method"]=="notifications/tools/list_changed"){check(!r.contains("id"),"notification has no id");++notifications;continue;}check(r["id"]==id,"response correlation");return r;}
        throw QString("MCP response timeout ")+QString::fromUtf8(p.readAllStandardError());
    }
    QJsonObject tool(const QString& name,QJsonObject args={}) {
        const auto result=call("tools/call",{{"name",name},{"arguments",args}})["result"].toObject();
        return QJsonDocument::fromJson(result["content"].toArray()[0].toObject()["text"].toString().toUtf8()).object();
    }
    void waitNotification() {QJsonObject r;check(frame(&r,5000)&&r["method"]=="notifications/tools/list_changed"&&!r.contains("id"),"idle list_changed received without request");++notifications;}
    void close(){p.closeWriteChannel();check(until([&](){return p.state()==QProcess::NotRunning;}),"bridge exits cleanly on EOF");check(p.exitCode()==0,"bridge exit0");}
};
void script(const QString& path,QStringList args) {
    QProcess p;args.prepend(path);args.prepend("-File");args.prepend("Bypass");args.prepend("-ExecutionPolicy");args.prepend("-NoProfile");
    p.start("powershell.exe",args);check(p.waitForStarted(),"installer/rollback starts");
    check(until([&](){return p.state()==QProcess::NotRunning;},90000),"installer/rollback completes while bridge alive");
    out<<p.readAllStandardOutput();out.flush();const auto errors=p.readAllStandardError();if(p.exitCode())out<<errors;
    check(p.exitCode()==0,"installer/rollback exit0");
}
bool hasTool(QJsonObject response,const QString& name){for(const auto& v:response["result"].toObject()["tools"].toArray())if(v.toObject()["name"]==name)return true;return false;}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);out.setCodec("UTF-8");
    try {
        const auto args=app.arguments();check(args.size()==6,"explicit isolated target/package/scripts/backups/probe arguments");
        const QString target=args[1],package=args[2],scripts=args[3],backups=args[4],probe=args[5];
        QFile versionFile(package+"/VERSION");check(versionFile.open(QIODevice::ReadOnly),"package version readable");const auto expectedVersion=versionFile.readAll().trimmed();
        Mapper mapper;
        {
            Client old(target+"/EIMapperMcp.exe");const auto pid=old.pid();
            check(old.tool("connect_instance",{{"instance",mapper.id}})["version"]=="future-1","connected to owned mapper");old.waitNotification();
            check(hasTool(old.call("tools/list"),"future_tool_1"),"mapper-owned future tool visible");
            check(old.tool("future_tool_1")["forwarded"]==true,"tool absent from compiled fallback forwarded");
            check(old.call("resources/list")["result"].toObject()["resources"].toArray().size()==3,"resources sourced from mapper");
            check(old.call("resources/templates/list")["result"].toObject()["resourceTemplates"].toArray().size()==1,"templates sourced from mapper");
            check(old.call("resources/read",{{"uri","ataman://future"}})["result"].toObject()["contents"].toArray()[0].toObject()["text"].toString().contains("mapper-owned-instructions-1"),"resources/instructions forwarded");
            script(scripts+"/install_program.ps1",{"-PackageRoot",package,"-TargetRoot",target,"-RollbackRoot",backups+"/first"});
            check(old.pid()==pid&&old.call("ping").contains("result"),"same running bridge survives installation");
            check(!QDir(target).entryList({"EIMapperMcp.exe.old-ataman-*"},QDir::Files).isEmpty(),"occupied exe renamed aside");
            check(!QDir(target).entryList({"Qt5Core.dll.old-ataman-*"},QDir::Files).isEmpty(),"occupied Qt DLL renamed aside");
            QProcess version;version.start(target+"/EIMapperMcp.exe",{"--version"});check(version.waitForFinished()&&version.readAllStandardOutput().trimmed()=="EIMapperMcp "+expectedVersion,"new canonical bridge matches package version");
            mapper.generation=2;old.waitNotification();check(hasTool(old.call("tools/list"),"future_tool_2"),"same bridge receives changed mapper tools");
            check(!hasTool(old.call("tools/list"),"future_tool_1"),"retired tool removed from list");
            {Client fresh(target+"/EIMapperMcp.exe");check(hasTool(fresh.call("tools/list"),"future_tool_2"),"plain reconnect sees current catalog before selecting editor");
                check(fresh.tool("get_context").contains("error"),"metadata discovery does not select or authorize workspace");
                check(fresh.tool("connect_instance",{{"instance",mapper.id}})["version"]=="future-2","new bridge connects");fresh.waitNotification();
                script(backups+"/first/rollback.ps1",{"-VerifyOnly"});
                script(backups+"/first/rollback.ps1",{});
                check(fresh.call("ping").contains("result")&&old.call("ping").contains("result"),"both generations survive rollback");fresh.close();}
            old.close();
        }
        script(scripts+"/install_program.ps1",{"-PackageRoot",package,"-TargetRoot",target,"-RollbackRoot",backups+"/second"});
        check(QDir(target).entryList({"*.old-ataman-*"},QDir::Files).isEmpty(),"repeat installation removes unlocked retired files");
        {Client c(target+"/EIMapperMcp.exe");mapper.legacy=true;check(!c.tool("connect_instance",{{"instance",mapper.id}}).contains("error"),"218-221 legacy pipe fallback");c.waitNotification();
            check(!hasTool(c.call("tools/list"),"future_tool_2"),"legacy mapper uses compiled fallback");check(c.tool("get_context")["version"]=="future-2","legacy read continues");
            mapper.legacy=false;mapper.incompatible=true;
            check(c.tool("connect_instance",{{"instance",mapper.id}}).contains("error"),"incompatible connect reports clear error");
            check(c.call("tools/list").contains("error"),"incompatible major yields explicit RPC error");
            mapper.incompatible=false;check(hasTool(c.call("tools/list"),"future_tool_2"),"compatible catalog recovers");c.close();}
        {Client modern(target+"/EIMapperMcp.exe",false);QJsonObject meta{{"io.modelcontextprotocol/protocolVersion","2026-07-28"},
            {"io.modelcontextprotocol/clientInfo",QJsonObject{{"name","modern-test"},{"version","1"}}},{"io.modelcontextprotocol/clientCapabilities",QJsonObject{}}};
            check(modern.call("server/discover",{{"_meta",meta}})["result"].toObject()["capabilities"].toObject()["tools"].toObject()["listChanged"]==true,"modern capability works");modern.close();}
        Q_UNUSED(probe);
        out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\n";out.flush();return 1;}
}

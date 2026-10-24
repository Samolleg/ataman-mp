// Isolated real-client notification probe. No credentials or model turns.
// --server implements only the standard MCP handshake/list/change messages.
#include <QtCore>
#include <QtTest>
#include <thread>
#include <functional>
#include <cstdio>
#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif
namespace {
bool until(const std::function<bool()>& done,int ms=15000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
void file(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw QString("write ")+path;}
void append(const QString& path,QJsonObject value){QFile f(path);if(!f.open(QIODevice::Append))throw QString("trace write");f.write(QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n');}
QByteArray read(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void send(QJsonObject value){const auto b=QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n';fwrite(b.constData(),1,size_t(b.size()),stdout);fflush(stdout);}
int server(QCoreApplication& app,const QString& trace) {
    int generation=QFileInfo::exists(trace+".generation2")?2:1;bool scheduled=false;
    std::thread([&](){QByteArray line;while(true){int c=fgetc(stdin);if(c==EOF)break;line.append(char(c));if(c!='\n')continue;const auto bytes=line;line.clear();
        QMetaObject::invokeMethod(&app,[&,bytes](){const auto r=QJsonDocument::fromJson(bytes).object();const auto method=r["method"].toString();append(trace,{{"method",method},{"generation",generation},{"pid",double(QCoreApplication::applicationPid())}});
            if(!r.contains("id"))return;QJsonObject result;
            if(method=="initialize")result={{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{{"tools",QJsonObject{{"listChanged",true}}}}},{"serverInfo",QJsonObject{{"name","ataman-notification-probe"},{"version","222-test"}}}};
            else if(method=="tools/list"){
                result={{"tools",QJsonArray{QJsonObject{{"name",QString("probe_tool_%1").arg(generation)},{"description","Synthetic notification probe"},{"inputSchema",QJsonObject{{"type","object"},{"properties",QJsonObject{}}}}}}}};
                if(!scheduled&&generation==1){scheduled=true;QTimer::singleShot(2000,&app,[&](){generation=2;file(trace+".generation2","generation2");append(trace,{{"sent","notifications/tools/list_changed"},{"pid",double(QCoreApplication::applicationPid())}});send({{"jsonrpc","2.0"},{"method","notifications/tools/list_changed"}});});}
            }else if(method=="resources/list")result={{"resources",QJsonArray{}}};
            else if(method=="tools/call")result={{"content",QJsonArray{QJsonObject{{"type","text"},{"text",r["params"].toObject()["name"].toString()}}}},{"isError",false}};
            else if(method=="resources/templates/list")result={{"resourceTemplates",QJsonArray{}}};
            else if(method=="prompts/list")result={{"prompts",QJsonArray{}}};
            else if(method!="ping"){send({{"jsonrpc","2.0"},{"id",r["id"]},{"error",QJsonObject{{"code",-32601},{"message","Method not found"}}}});return;}
            send({{"jsonrpc","2.0"},{"id",r["id"]},{"result",result}});
        },Qt::BlockingQueuedConnection);
    }QMetaObject::invokeMethod(&app,"quit",Qt::QueuedConnection);}).detach();return app.exec();
}
QProcessEnvironment isolated(const QString& root){auto env=QProcessEnvironment::systemEnvironment();for(const auto& key:env.keys())if(key.contains("TOKEN")||key.contains("API_KEY")||key.startsWith("OPENAI_")||key.startsWith("ANTHROPIC_")||key.startsWith("CLAUDE_"))env.remove(key);
    for(const auto& key:{"APPDATA","LOCALAPPDATA","USERPROFILE","HOME","TEMP","TMP","CODEX_HOME","CLAUDE_CONFIG_DIR"}){auto path=root+"/"+key;QDir().mkpath(path);env.insert(key,path);}
    env.insert("CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC","1");env.insert("DISABLE_AUTOUPDATER","1");env.insert("DISABLE_TELEMETRY","1");return env;}
QJsonObject rpc(QProcess& p,QByteArray& buffer,const QString& id,const QString& method,QJsonObject params={}) {
    p.write(QJsonDocument(QJsonObject{{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
    QElapsedTimer t;t.start();while(t.elapsed()<20000){QCoreApplication::processEvents();buffer+=p.readAllStandardOutput();while(buffer.contains('\n')){int n=buffer.indexOf('\n');auto r=QJsonDocument::fromJson(buffer.left(n)).object();buffer.remove(0,n+1);if(r["id"]==id)return r;}if(p.state()==QProcess::NotRunning)break;QTest::qWait(10);}return {{"probeError","timeout/client exited"}};
}
void stop(QProcess& p){p.closeWriteChannel();if(!until([&](){return p.state()==QProcess::NotRunning;},5000)){p.terminate();if(!p.waitForFinished(3000)){p.kill();p.waitForFinished();}}}
QJsonObject traceResult(const QString& trace){const auto bytes=read(trace);int first=0,second=0,sent=0;QSet<qint64> pids;for(const auto& line:bytes.split('\n')){auto r=QJsonDocument::fromJson(line).object();if(r["method"]=="tools/list"){if(r["generation"]==1)++first;else ++second;pids.insert(qint64(r["pid"].toDouble()));}if(r.contains("sent"))++sent;}return {{"initialLists",first},{"listsAfterNotification",second},{"notifications",sent},{"serverProcesses",pids.size()}};}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    const auto args=app.arguments();if(args.size()==3&&args[1]=="--server")return server(app,args[2]);
    try {
        if(args.size()!=4)throw QString("usage: run-root codex-exe claude-exe");const auto root=args[1];if(QFileInfo::exists(root))throw QString("fresh run-root required");QDir().mkpath(root);
        const auto self=QCoreApplication::applicationFilePath();QJsonObject evidence;
        {
            const auto dir=root+"/codex";QDir().mkpath(dir);auto env=isolated(dir);const auto trace=dir+"/mcp-trace.jsonl";
            const auto config=QString("model = 'ataman-probe-no-model'\nmodel_provider = 'probe_local'\n[model_providers.probe_local]\nname = 'offline probe'\nbase_url = 'http://127.0.0.1:9/v1'\nwire_api = 'responses'\n[mcp_servers.ataman_probe]\ncommand = '%1'\nargs = ['--server', '%2']\nstartup_timeout_sec = 10\n[analytics]\nenabled = false\n").arg(self,trace).toUtf8();file(env.value("CODEX_HOME")+"/config.toml",config);
            QProcess p;p.setProcessEnvironment(env);p.setWorkingDirectory(dir);p.start(args[2],{"app-server","--stdio"});if(!p.waitForStarted())throw QString("Codex probe start");QByteArray buffer;
            auto hello=rpc(p,buffer,"1","initialize",{{"clientInfo",QJsonObject{{"name","ataman_222_probe"},{"version","1"}}},{"capabilities",QJsonObject{{"experimentalApi",true}}}});
            p.write("{\"method\":\"initialized\"}\n");
            auto thread=rpc(p,buffer,"thread","thread/start",{{"cwd",dir},{"ephemeral",true}});
            const auto threadId=thread["result"].toObject()["thread"].toObject()["id"].toString();
            QJsonObject statusArgs{{"detail","toolsAndAuthOnly"}};if(!threadId.isEmpty())statusArgs["threadId"]=threadId;
            auto first=rpc(p,buffer,"2","mcpServerStatus/list",statusArgs);
            QJsonObject toolArgs{{"threadId",threadId},{"server","ataman_probe"},{"tool","probe_tool_1"},{"arguments",QJsonObject{}}};
            auto firstCall=rpc(p,buffer,"call1","mcpServer/tool/call",toolArgs);
            until([&](){return read(trace).contains("\"sent\"");},10000);QTest::qWait(2000);
            auto second=rpc(p,buffer,"3","mcpServerStatus/list",statusArgs);
            toolArgs["tool"]="probe_tool_2";auto secondCall=rpc(p,buffer,"call2","mcpServer/tool/call",toolArgs);
            auto reload=rpc(p,buffer,"reload","config/mcpServer/reload");
            auto reloadedCall=rpc(p,buffer,"call3","mcpServer/tool/call",toolArgs);
            auto afterReload=rpc(p,buffer,"4","mcpServerStatus/list",statusArgs);
            auto stats=traceResult(trace);stats["hello"]=hello;stats["thread"]=thread;stats["first"]=first;stats["second"]=second;stats["firstCall"]=firstCall;stats["secondCall"]=secondCall;stats["reloadedCall"]=reloadedCall;stats["reload"]=reload;stats["afterReload"]=afterReload;stats["aliveAfterNotification"]=p.state()==QProcess::Running;
            file(dir+"/stderr.txt",p.readAllStandardError());stop(p);evidence["codex"]=stats;
        }
        if(!qEnvironmentVariableIsSet("EI_MCP_SKIP_CLAUDE")) {
            const auto dir=root+"/claude";QDir().mkpath(dir);auto env=isolated(dir);const auto trace=dir+"/mcp-trace.jsonl";
            QJsonObject config{{"mcpServers",QJsonObject{{"ataman_probe",QJsonObject{{"type","stdio"},{"command",self},{"args",QJsonArray{"--server",trace}}}}}}};
            const auto configPath=dir+"/mcp.json";file(configPath,QJsonDocument(config).toJson());
            QProcess p;p.setProcessEnvironment(env);p.setWorkingDirectory(dir);p.start(args[3],{"-p","--input-format","stream-json","--output-format","stream-json","--verbose","--strict-mcp-config","--mcp-config",configPath,"--setting-sources","","--debug-file",dir+"/debug.txt"});if(!p.waitForStarted())throw QString("Claude probe start");
            p.write("{\"type\":\"control_request\",\"request_id\":\"init\",\"request\":{\"subtype\":\"initialize\"}}\n");
            until([&](){return read(trace).contains("\"sent\"")||p.state()==QProcess::NotRunning;},15000);QTest::qWait(2000);
            auto stats=traceResult(trace);stats["aliveAfterNotification"]=p.state()==QProcess::Running;file(dir+"/stdout.jsonl",p.readAllStandardOutput());file(dir+"/stderr.txt",p.readAllStandardError());stop(p);evidence["claude"]=stats;
        }
        file(root+"/result.json",QJsonDocument(evidence).toJson());QTextStream(stdout)<<QJsonDocument(evidence).toJson();return 0;
    }catch(const QString& e){QTextStream(stderr)<<e<<"\n";return 1;}
}

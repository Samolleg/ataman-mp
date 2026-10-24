#include "ai_agent_protocol.h"
#include <QLocalSocket>
#include <cstdio>
#include <cmath>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {
const QJsonArray versions{"2026-07-28","2025-11-25","2025-06-18","2024-11-05"};
const QJsonObject capabilities{{"tools",QJsonObject{}},{"resources",QJsonObject{}}};
const QString instructions="Ataman MCP. list_instances then connect_instance. Never launch an editor or enable modes. Only the human can enable Read-only/Edit and Allow agent to save in the dock. get_revision before edits; preview or direct sculpt/paint/create_map return an operation ID, poll operation_status; cancel_operation discards pending work without changing live terrain. Every edit is one Undo; revision conflicts require re-reading. Save uses the normal map pipeline only with human permission. Read tools and their protocol remain compatible with218.";
QJsonObject serverInfo(){return {{"name","ei-mapper-ataman"},{"version",ENDURANCE_VERSION}};}
QJsonObject rpcError(int code,const QString& message,QJsonObject data={})
{
    QJsonObject error{{"code",code},{"message",message}};if(!data.isEmpty())error.insert("data",data);return {{"error",error}};
}
QJsonObject readDescriptor(const QString& id)
{
    if(!QRegularExpression("^[a-f0-9-]{36}$").match(id).hasMatch())return {};
    QFileInfo info(QDir(ai::discoveryDirectory()).filePath(id+".json"));
    if(info.isSymLink()||!info.isFile()||info.size()>16384)return {};
    QFile file(info.absoluteFilePath());if(!file.open(QIODevice::ReadOnly))return {};
    auto value=QJsonDocument::fromJson(file.read(16385)).object();
    if(value["instance"].toString()!=id||value["pipe"].toString()!="ei-ataman-"+id
        ||value["secret"].toString().size()!=72)return {};
    return value;
}
class Bridge {
    QLocalSocket socket;
    QString secret;
    QByteArray buffer;
    quint64 sequence{0};
    bool legacyInitialized{false};
    QMap<QString,qint64> editJobs;
public:
    void detach(){socket.abort();secret.clear();buffer.clear();editJobs.clear();}
    QJsonObject local(const QString& method,const QJsonObject& args,int timeout=2000)
    {
        if(socket.state()!=QLocalSocket::ConnectedState||secret.isEmpty())return ai::error("Not connected. Enable read-only in the editor and connect_instance.");
        const QString id=QString::number(++sequence);
        const QByteArray payload=QJsonDocument(QJsonObject{{"id",id},{"secret",secret},{"method",method},{"arguments",args}}).toJson(QJsonDocument::Compact)+'\n';
        if(socket.write(payload)!=payload.size()){detach();return ai::error("Local write failed; reconnect");}
        QElapsedTimer elapsed;elapsed.start();
        while(elapsed.elapsed()<timeout) {
            buffer+=socket.readAll();
            while(buffer.contains('\n')) {
                QJsonObject response;QString error;
                if(!ai::takeFrame(buffer,&response,&error)){detach();return ai::error("Invalid local frame; reconnect");}
                // Correlate on our own ID, never on the client's reused/request ID.
                // Late responses are ignored, and timeout destroys the stream.
                if(response["id"].toString()==id) {
                    if(!response["result"].isObject()){detach();return ai::error("Invalid local result");}
                    return response["result"].toObject();
                }
            }
            if(buffer.size()>ai::MaxFrame){detach();return ai::error("Local frame limit");}
            if(socket.state()!=QLocalSocket::ConnectedState){detach();return ai::error("Editor disconnected or read mode disabled");}
            socket.waitForReadyRead(qMax(1,timeout-int(elapsed.elapsed())));
        }
        detach();return ai::error("Local request timeout; connection discarded. Reconnect before retrying.");
    }
    QJsonObject tool(const QString& name,const QJsonObject& args)
    {
        QString error;if(!ai::validateArguments(name,args,&error))return ai::error(error);
        if(name=="list_instances") {
            QJsonArray list;int scanned=0;
            const QDir dir(ai::discoveryDirectory());
            for(const auto& file:dir.entryInfoList({"*.json"},QDir::Files,QDir::Name)) {
                if(++scanned>100||list.size()>=32)break;
                auto d=readDescriptor(file.completeBaseName());if(d.isEmpty())continue;
                QLocalSocket probe;probe.connectToServer(d["pipe"].toString());
                if(!probe.waitForConnected(50))continue;probe.abort();
                d.remove("secret");d.remove("pipe");list.append(d);
            }
            return {{"instances",list},{"limit",32},{"scanLimit",100}};
        }
        if(name=="disconnect_instance"){detach();return {{"status","disconnected"}};}
        if(name=="connect_instance") {
            detach();const auto d=readDescriptor(args["instance"].toString());
            if(d.isEmpty())return ai::error("Instance not available; user must enable read-only");
            secret=d["secret"].toString();socket.connectToServer(d["pipe"].toString());
            if(!socket.waitForConnected(1000)){detach();return ai::error("Editor is not running or read mode disabled");}
            return local("get_context",{});
        }
        const auto now=QDateTime::currentMSecsSinceEpoch();
        for(auto it=editJobs.begin();it!=editJobs.end();)if(it.value()<now)it=editJobs.erase(it);else ++it;
        // Only new219 edit IDs get the normal serializer's bounded commit
        // allowance.218 reads and all ordinary calls retain the2s deadline,
        // ID correlation and stream discard on timeout.
        const bool editPoll=(name=="operation_status"||name=="cancel_operation")&&editJobs.contains(args["operation"].toString());
        const auto value=local(name,args,editPoll?30000:2000);
        const QSet<QString> edits{"create_map","sculpt","paint","preview","apply","save"};
        if(edits.contains(name)&&value["operation"].isString()) {
            if(editJobs.size()>=8)editJobs.erase(editJobs.begin());
            editJobs[value["operation"].toString()]=now+60000;
            if(name=="apply")editJobs.remove(args["operation"].toString());
        }
        if(name=="cancel_operation"&&value["status"]=="cancelled")editJobs.remove(args["operation"].toString());
        return value;
    }
    QJsonObject handle(const QJsonObject& request)
    {
        if(request["jsonrpc"]!="2.0"||!request["method"].isString()
            ||(request.contains("id")&&!request["id"].isString()&&!request["id"].isDouble())
            ||(request.contains("params")&&!request["params"].isObject()))return rpcError(-32600,"Invalid JSON-RPC request");
        const auto method=request["method"].toString();const auto params=request["params"].toObject();
        const auto meta=params["_meta"].toObject();const auto version=meta["io.modelcontextprotocol/protocolVersion"].toString();
        const bool modern=!version.isEmpty();
        if(modern&&version!="2026-07-28")return rpcError(-32022,"Unsupported protocol version",{{"supported",versions},{"requested",version}});
        if(modern&&(!meta["io.modelcontextprotocol/clientInfo"].isObject()||!meta["io.modelcontextprotocol/clientCapabilities"].isObject()))
            return rpcError(-32602,"Modern requests require clientInfo and clientCapabilities metadata");
        if(method=="initialize") {
            const auto offered=params["protocolVersion"].toString();
            legacyInitialized=true;
            return {{"result",QJsonObject{{"protocolVersion",versions.contains(offered)&&offered!="2026-07-28"?offered:QString("2025-11-25")},
                {"capabilities",capabilities},{"serverInfo",serverInfo()},{"instructions",instructions}}}};
        }
        QJsonObject result;
        if(method=="server/discover")result={{"supportedVersions",versions},{"capabilities",capabilities},
            {"_meta",QJsonObject{{"io.modelcontextprotocol/serverInfo",serverInfo()}}},{"instructions",instructions}};
        else if(method=="ping") result={};
        else if(!modern&&!legacyInitialized)return rpcError(-32000,"Initialize the MCP session first");
        else if(method=="tools/list") {
            if(params.contains("cursor"))return rpcError(-32602,"No pagination cursor for tools");
            result={{"tools",ai::toolDefinitions()}};
        } else if(method=="tools/call") {
            if(!params["name"].isString()||(params.contains("arguments")&&!params["arguments"].isObject()))return rpcError(-32602,"Expected tool name and object arguments");
            auto value=tool(params["name"].toString(),params["arguments"].toObject());
            QJsonArray content;
            if(value["mimeType"]=="image/png"&&value.contains("data"))content.append(QJsonObject{{"type","image"},{"mimeType","image/png"},{"data",value["data"]}});
            else content.append(QJsonObject{{"type","text"},{"text",QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))}});
            const auto appendImages=[&](const QJsonObject& o) {
                for(const auto& key:{QString("preview"),QString("snapshot")}) {
                    const auto png=o[key].toObject();if(png["mimeType"]=="image/png"&&png["data"].isString())
                        content.append(QJsonObject{{"type","image"},{"mimeType","image/png"},{"data",png["data"]}});
                }
            };
            appendImages(value);appendImages(value["result"].toObject());
            result={{"content",content},{"isError",value.contains("error")}};
        } else if(method=="resources/list")result={{"resources",QJsonArray{
            QJsonObject{{"uri","ataman://context"},{"name","Open editor context"},{"mimeType","application/json"}},
            QJsonObject{{"uri","ataman://tools"},{"name","Read tool catalog and limits"},{"mimeType","application/json"}}}}};
        else if(method=="resources/templates/list")result={{"resourceTemplates",QJsonArray{}}};
        else if(method=="resources/read") {
            const auto uri=params["uri"].toString();QJsonObject value;
            if(uri=="ataman://context")value=local("get_context",{});
            else if(uri=="ataman://tools")value={{"tools",ai::toolDefinitions()},{"instructions",instructions}};
            else return rpcError(-32002,"Resource not found");
            if(value.contains("error"))return rpcError(-32000,value["error"].toString());
            result={{"contents",QJsonArray{QJsonObject{{"uri",uri},{"mimeType","application/json"},
                {"text",QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))}}}}};
        } else return rpcError(-32601,"Method not found");
        if(modern) {
            result.insert("resultType","complete");
            if(method=="server/discover"||method=="tools/list"||method.startsWith("resources/")) {
                result.insert("ttlMs",0);result.insert("cacheScope","private");
            }
        }
        return {{"result",result}};
    }
};
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(app.arguments().contains("--version")){printf("EIMapperMcp %s\n",ENDURANCE_VERSION);return 0;}
    Bridge bridge;QByteArray line;
    const auto send=[](QJsonObject response,QJsonValue id) {
        response.insert("jsonrpc","2.0");response.insert("id",id);
        auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact);
        if(bytes.size()>ai::MaxFrame)bytes=QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"error",QJsonObject{{"code",-32603},{"message","Response limit"}}}}).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);fputc('\n',stdout);fflush(stdout);
    };
    while(true) {
        const int c=fgetc(stdin);if(c==EOF)break;
        line.append(char(c));
        if(line.size()>ai::MaxFrame){send(rpcError(-32700,"Frame limit exceeded"),QJsonValue::Null);return 2;}
        if(c!='\n')continue;
        QJsonObject request;QString error;
        if(!ai::takeFrame(line,&request,&error)){send(rpcError(-32700,error),QJsonValue::Null);continue;}
        if(!request.contains("id")&&request["method"].toString().startsWith("notifications/"))continue;
        const auto result=bridge.handle(request);
        if(request.contains("id")||result.contains("error"))send(result,request.value("id").isUndefined()?QJsonValue(QJsonValue::Null):request["id"]);
    }
    return 0;
}

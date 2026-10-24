#include "ai_agent_protocol.h"
#include <QLocalSocket>
#include <cstdio>
#include <cmath>
#include <thread>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {
const QJsonArray versions{"2026-07-28","2025-11-25","2025-06-18","2024-11-05"};
const QJsonObject capabilities{{"tools",QJsonObject{{"listChanged",true}}},{"resources",QJsonObject{}}};
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
bool available(const QJsonObject& descriptor)
{
    // Inspect the named pipe without opening a client handle. A connect/abort
    // probe is a real pending client on Windows and races other bridges.
#ifdef Q_OS_WIN
    const auto pipe=QString("\\\\.\\pipe\\")+descriptor["pipe"].toString();
    if(WaitNamedPipeW(reinterpret_cast<LPCWSTR>(pipe.utf16()),1))return true;
    const auto error=GetLastError();return error==ERROR_SEM_TIMEOUT||error==ERROR_PIPE_BUSY;
#else
    return QFileInfo::exists(QDir::temp().filePath(descriptor["pipe"].toString()));
#endif
}
class Bridge {
    QLocalSocket socket;
    QString secret;
    QByteArray buffer;
    quint64 sequence{0};
    bool legacyInitialized{false};
    QMap<QString,qint64> editJobs;
    QMap<QString,qint64> activeJobs;
    QJsonObject catalog{ai::mcpCatalog()};
    bool remoteCatalog{false}, changed{false}, ready{false};
    bool selected{false}, discoverMetadata{true};
    bool legacyCatalog{false};
    QString catalogError;
    QElapsedTimer clock;
    qint64 retryAt{0};int retryDelay{1000};QString attemptedPipe;
    void connectionFailed() {
        detach();retryAt=clock.elapsed()+retryDelay;retryDelay=qMin(60000,retryDelay*2);
    }
    void connectionHealthy(){retryAt=0;retryDelay=1000;}
    void useCatalog(const QJsonObject& value,bool remote) {
        if(catalog!=value||remoteCatalog!=remote)changed=true;
        catalog=value;remoteCatalog=remote;
    }
public:
    Bridge(){clock.start();}
    void detach(){socket.abort();selected=false;legacyCatalog=false;secret.clear();buffer.clear();editJobs.clear();activeJobs.clear();catalogError.clear();useCatalog(ai::mcpCatalog(),false);}
    void initialized(){ready=true;}
    bool takeChanged(){if(!ready||!changed)return false;changed=false;return true;}
    void refreshCatalog() {
        if(socket.state()!=QLocalSocket::ConnectedState) {
            if(!secret.isEmpty())connectionFailed();if(!discoverMetadata)return;
            // A client that ignores list_changed must still see the current
            // catalog on a plain MCP reconnect, before connect_instance runs.
            // Fetch metadata only when there is one unambiguous enabled editor.
            // This does NOT select a workspace or authorize any tool calls.
            QJsonObject candidate;int scanned=0;
            for(const auto& file:QDir(ai::discoveryDirectory()).entryInfoList({"*.json"},QDir::Files,QDir::Name)) {
                if(++scanned>100)break;
                const auto d=readDescriptor(file.completeBaseName());if(d.isEmpty())continue;
                if(!available(d))continue;
                if(!candidate.isEmpty())return;candidate=d;
            }
            if(candidate.isEmpty())return;
            const auto pipe=candidate["pipe"].toString();
            if(attemptedPipe!=pipe){attemptedPipe=pipe;connectionHealthy();}
            if(clock.elapsed()<retryAt)return;
            secret=candidate["secret"].toString();socket.connectToServer(candidate["pipe"].toString());
            if(!socket.waitForConnected(250)){connectionFailed();return;}
        }
        if(legacyCatalog)return;
        // Never let a background metadata probe's short read deadline destroy
        // a stream while a normal asynchronous edit is committing on the GUI.
        const auto now=QDateTime::currentMSecsSinceEpoch();
        for(auto it=activeJobs.begin();it!=activeJobs.end();)if(it.value()<now)it=activeJobs.erase(it);else ++it;
        if(!activeJobs.isEmpty())return;
        const auto value=local("mcp_catalog",{});
        if(value.contains("error")) {
            if(socket.state()!=QLocalSocket::ConnectedState){catalogError.clear();return;}
            // 218-221 return this exact error for an unknown method. Do not
            // hide a malformed/incompatible new protocol behind stale tools.
            if(value["error"]=="Unknown read tool") {connectionHealthy();legacyCatalog=true;catalogError.clear();useCatalog(ai::mcpCatalog(),false);}
            else catalogError=value["error"].toString();
            return;
        }
        if(value["protocolMajor"]!=ai::LocalProtocolMajor||value["catalogVersion"]!=1
            ||!value["tools"].isArray()||!value["resources"].isArray()
            ||!value["resourceTemplates"].isArray()||!value["instructions"].isString()) {
            catalogError="Incompatible mapper catalog/protocol. Update the MCP bridge or reconnect to a compatible editor.";return;
        }
        // The three discovery/connection tools belong to the transport. Always
        // keep these available even if a future editor omits them from its list.
        auto current=value;auto tools=value["tools"].toArray();
        for(const auto& item:ai::toolDefinitions()) {
            const auto name=item.toObject()["name"].toString();
            if(name!="list_instances"&&name!="connect_instance"&&name!="disconnect_instance")continue;
            bool found=false;for(const auto& t:tools)if(t.toObject()["name"]==name)found=true;
            if(!found)tools.prepend(item);
        }
        current["tools"]=tools;connectionHealthy();catalogError.clear();useCatalog(current,true);
    }
    QJsonObject local(const QString& method,const QJsonObject& args,int timeout=2000)
    {
        if(socket.state()!=QLocalSocket::ConnectedState||secret.isEmpty())return ai::error("Not connected. Enable read-only in the editor and connect_instance.");
        const QString id=QString::number(++sequence);
        const QByteArray payload=QJsonDocument(QJsonObject{{"id",id},{"secret",secret},{"protocolMajor",ai::LocalProtocolMajor},{"method",method},{"arguments",args}}).toJson(QJsonDocument::Compact)+'\n';
        if(socket.write(payload)!=payload.size()){connectionFailed();return ai::error("Local write failed; reconnect");}
        QElapsedTimer elapsed;elapsed.start();
        while(elapsed.elapsed()<timeout) {
            buffer+=socket.readAll();
            while(buffer.contains('\n')) {
                QJsonObject response;QString error;
                if(!ai::takeFrame(buffer,&response,&error)){connectionFailed();return ai::error("Invalid local frame; reconnect");}
                // Correlate on our own ID, never on the client's reused/request ID.
                // Late responses are ignored, and timeout destroys the stream.
                if(response["id"].toString()==id) {
                    if(!response["result"].isObject()){connectionFailed();return ai::error("Invalid local result");}
                    return response["result"].toObject();
                }
            }
            if(buffer.size()>ai::MaxFrame){connectionFailed();return ai::error("Local frame limit");}
            if(socket.state()!=QLocalSocket::ConnectedState){connectionFailed();return ai::error("Editor disconnected or read mode disabled");}
            socket.waitForReadyRead(qMax(1,timeout-int(elapsed.elapsed())));
        }
        connectionFailed();return ai::error("Local request timeout; connection discarded. Reconnect before retrying.");
    }
    QJsonObject tool(const QString& name,const QJsonObject& args)
    {
        // Mapper owns validation of forwarded tools. An old bridge must not
        // reject a new tool just because its compiled fallback predates it.
        if(name=="list_instances"||name=="connect_instance"||name=="disconnect_instance") {
            QString error;if(!ai::validateArguments(name,args,&error))return ai::error(error);
        }
        if(name=="list_instances") {
            QJsonArray list;int scanned=0;
            const QDir dir(ai::discoveryDirectory());
            for(const auto& file:dir.entryInfoList({"*.json"},QDir::Files,QDir::Name)) {
                if(++scanned>100||list.size()>=32)break;
                auto d=readDescriptor(file.completeBaseName());if(d.isEmpty())continue;
                if(!available(d))continue;
                d.remove("secret");d.remove("pipe");list.append(d);
            }
            return {{"instances",list},{"limit",32},{"scanLimit",100}};
        }
        if(name=="disconnect_instance"){discoverMetadata=false;detach();return {{"status","disconnected"}};}
        if(name=="connect_instance") {
            discoverMetadata=true;const auto d=readDescriptor(args["instance"].toString());
            if(d.isEmpty()){detach();return ai::error("Instance not available; user must enable read-only");}
            if(socket.state()!=QLocalSocket::ConnectedState||socket.serverName()!=d["pipe"].toString()||secret!=d["secret"].toString()) {
                detach();attemptedPipe=d["pipe"].toString();secret=d["secret"].toString();socket.connectToServer(attemptedPipe);
                if(!socket.waitForConnected(1000)){connectionFailed();return ai::error("Editor is not running or read mode disabled");}
            }
            // An explicit selection must re-negotiate a former legacy catalog,
            // even when its healthy transport can be retained.
            legacyCatalog=false;catalogError.clear();selected=true;
            const auto context=local("get_context",{});refreshCatalog();changed=true;
            return catalogError.isEmpty()?context:ai::error(catalogError);
        }
        if(!selected)return ai::error("No editor selected. Use list_instances and connect_instance before reading or editing.");
        if(!catalogError.isEmpty())return ai::error(catalogError);
        const auto now=QDateTime::currentMSecsSinceEpoch();
        for(auto it=editJobs.begin();it!=editJobs.end();)if(it.value()<now)it=editJobs.erase(it);else ++it;
        // Only new219 edit IDs get the normal serializer's bounded commit
        // allowance.218 reads and all ordinary calls retain the2s deadline,
        // ID correlation and stream discard on timeout.
        const bool editPoll=(name=="operation_status"||name=="cancel_operation")&&editJobs.contains(args["operation"].toString());
        const auto value=local(name,args,editPoll?30000:2000);
        if(value["operation"].isString())activeJobs[value["operation"].toString()]=now+180000;
        if(name=="cancel_operation"||(name=="operation_status"&&(value["status"]=="complete"||value.contains("error"))))activeJobs.remove(args["operation"].toString());
        const QSet<QString> edits{"create_map","open_map","close_map","place_map_in_world","set_world_position","create_mob","sculpt","paint","generate_terrain","erode","carve_river","auto_paint","preview","apply","save"};
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
            refreshCatalog();
            const auto offered=params["protocolVersion"].toString();
            legacyInitialized=true;
            return {{"result",QJsonObject{{"protocolVersion",versions.contains(offered)&&offered!="2026-07-28"?offered:QString("2025-11-25")},
                {"capabilities",capabilities},{"serverInfo",serverInfo()},{"instructions",catalog["instructions"]}}}};
        }
        if(modern)ready=true;
        if(method=="tools/list"||method.startsWith("resources/")||method=="server/discover")refreshCatalog();
        if(!catalogError.isEmpty()&&(method=="tools/list"||method.startsWith("resources/")))return rpcError(-32000,catalogError);
        QJsonObject result;
        if(method=="server/discover")result={{"supportedVersions",versions},{"capabilities",capabilities},
            {"_meta",QJsonObject{{"io.modelcontextprotocol/serverInfo",serverInfo()}}},{"instructions",catalog["instructions"]}};
        else if(method=="ping") result={};
        else if(!modern&&!legacyInitialized)return rpcError(-32000,"Initialize the MCP session first");
        else if(method=="tools/list") {
            if(params.contains("cursor"))return rpcError(-32602,"No pagination cursor for tools");
            result={{"tools",catalog["tools"]}};
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
        } else if(method=="resources/list")result={{"resources",catalog["resources"]}};
        else if(method=="resources/templates/list")result={{"resourceTemplates",catalog["resourceTemplates"]}};
        else if(method=="resources/read") {
            const auto uri=params["uri"].toString();QJsonObject value;
            if(!selected&&uri!="ataman://tools")return rpcError(-32000,"Select an editor with connect_instance before reading resources");
            if(remoteCatalog)value=local("mcp_resource_read",{{"uri",uri}});
            else if(uri=="ataman://context")value=local("get_context",{});
            else if(uri=="ataman://tools")value=catalog;
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
    Bridge bridge;
    const auto send=[](QJsonObject response,QJsonValue id) {
        response.insert("jsonrpc","2.0");response.insert("id",id);
        auto bytes=QJsonDocument(response).toJson(QJsonDocument::Compact);
        if(bytes.size()>ai::MaxFrame)bytes=QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"error",QJsonObject{{"code",-32603},{"message","Response limit"}}}}).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);fputc('\n',stdout);fflush(stdout);
    };
    const auto notify=[&bridge]() {
        if(!bridge.takeChanged())return;
        const QByteArray bytes="{\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}\n";
        fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);fflush(stdout);
    };
    // stdin may stay silent for hours. Keep the Qt/socket timer alive so catalog
    // changes are announced without requiring a client request. Only this Qt
    // thread owns the socket, Bridge and stdout. Blocking delivery bounds input.
    QTimer poll;QObject::connect(&poll,&QTimer::timeout,&app,[&](){bridge.refreshCatalog();notify();});poll.start(1000);
    std::thread([&]() {
        QByteArray line;
        while(true) {
            const int c=fgetc(stdin);if(c==EOF)break;line.append(char(c));
            const bool oversized=line.size()>ai::MaxFrame;
            if(c!='\n'&&!oversized)continue;
            const QByteArray frame=line;line.clear();
            QMetaObject::invokeMethod(&app,[&,frame,oversized]() {
                if(oversized){send(rpcError(-32700,"Frame limit exceeded"),QJsonValue::Null);app.exit(2);return;}
                auto bytes=frame;QJsonObject request;QString error;
                if(!ai::takeFrame(bytes,&request,&error)){send(rpcError(-32700,error),QJsonValue::Null);return;}
                if(!request.contains("id")&&request["method"].toString().startsWith("notifications/")) {
                    if(request["method"]=="notifications/initialized")bridge.initialized();
                    notify();return;
                }
                const auto result=bridge.handle(request);
                if(request.contains("id")||result.contains("error"))send(result,request.value("id").isUndefined()?QJsonValue(QJsonValue::Null):request["id"]);
                notify();
            },Qt::BlockingQueuedConnection);
            if(oversized)return;
        }
        QMetaObject::invokeMethod(&app,"quit",Qt::QueuedConnection);
    }).detach();
    return app.exec();
}

#include <QtWidgets>
#include <QtTest>
#include <QtNetwork>
#include "main_window.h"
#include "ai_agent_server.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "settings.h"
#include "log.h"
#include "resource_setup_dialog.h"
#include "profile_database.h"
#include "resourcemanager.h"
#include "camera.h"
#include <functional>

namespace {
QTextStream out(stdout);int checks=0;
void check(bool value,const QString& message){out<<(value?"PASS ":"FAIL ")<<message<<"\n";out.flush();if(!value)throw message;++checks;}
bool until(const std::function<bool()>& done,int timeout=10000){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<timeout)QTest::qWait(5);return done();}
QByteArray read(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& path,const QByteArray& bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw QString("fixture write");}
struct Hidden:QObject{bool eventFilter(QObject* o,QEvent* e)override{if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
class Client {
    QProcess process;QByteArray buffer;int serial=0;
public:
    explicit Client(const QString& exe){process.start(exe,QStringList{});check(process.waitForStarted(10000),"stdio bridge starts independently");}
    ~Client(){process.closeWriteChannel();if(!process.waitForFinished(2000)){process.kill();process.waitForFinished(2000);}}
    QJsonObject raw(QByteArray payload){process.write(payload);check(until([&](){buffer+=process.readAllStandardOutput();return buffer.contains('\n');},10000),"stdio response deadline");QJsonObject value;QString error;check(ai::takeFrame(buffer,&value,&error),"valid single JSON response frame");return value;}
    QJsonObject call(QString method,QJsonObject params={},bool modern=false){
        if(modern)params.insert("_meta",QJsonObject{{"io.modelcontextprotocol/protocolVersion","2026-07-28"},
            {"io.modelcontextprotocol/clientInfo",QJsonObject{{"name","Ataman native test"},{"version","1"}}},{"io.modelcontextprotocol/clientCapabilities",QJsonObject{}}});
        const auto id=QString("request-%1").arg(++serial);
        auto response=raw(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        check(response["id"]==id,"MCP string request ID preserved");return response;
    }
    QJsonObject tool(QString name,QJsonObject args={}) {
        const auto response=call("tools/call",{{"name",name},{"arguments",args}});
        check(response.contains("result"),"tool result envelope");
        const auto result=response["result"].toObject();const auto content=result["content"].toArray();
        if(!content.isEmpty()&&content[0].toObject()["type"]=="image")return content[0].toObject();
        return QJsonDocument::fromJson(content.at(0).toObject()["text"].toString().toUtf8()).object();
    }
    QJsonObject finish(QJsonObject start) {
        check(start.contains("operation"),"long read immediately returns operation ID");
        QJsonObject status;check(until([&](){status=tool("operation_status",{{"operation",start["operation"]}});return status["status"]=="complete";},35000),"background operation completes");
        const auto result=status["result"].toObject();check(!result.contains("error"),"background result successful");return result;
    }
};
}

int runAssemblyCatalogCheck()
{
    try {
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const auto bridge=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_BRIDGE"));
        check(!root.isEmpty()&&!bridge.isEmpty(),"explicit private test directories");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        QVector<QString> resourceRoots;
        for(const auto& path:qEnvironmentVariable("EI_MCP_TEST_RESOURCE_ROOTS").split(';',QString::SkipEmptyParts))resourceRoots.append(path);
        dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"resourceRoots"))->setValue(resourceRoots);
        CTextureList::getInstance()->attachSettings(window.m_settings.get());
        CObjectList::getInstance()->attachSettings(window.m_settings.get());
        window.m_currentCollectionPath=root+"/private.collection";window.resize(1100,800);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();check(view.isValid(),"real hidden mapper has OpenGL context");
        auto* mode=window.findChild<QComboBox*>("aiAgentMode");check(mode&&mode->currentIndex()==0,"automation defaults OFF");
        check(!qobject_cast<QStandardItemModel*>(mode->model())->item(2)->isEnabled(),"edit mode disabled");
        check(QDir(ai::discoveryDirectory()).entryList({"*.json"},QDir::Files).isEmpty(),"OFF publishes no discovery");

        QByteArray partial="{\"id\":\"x\"";QJsonObject frame;QString error;
        check(!ai::takeFrame(partial,&frame,&error)&&error.isEmpty(),"partial local frame retained");
        partial+="}\n{\"id\":2}\n";check(ai::takeFrame(partial,&frame,&error)&&frame["id"]=="x","fragmented frame reassembled");
        check(ai::takeFrame(partial,&frame,&error)&&frame["id"]==2,"coalesced frame separated");
        partial="[]\n";error.clear();check(!ai::takeFrame(partial,&frame,&error)&&!error.isEmpty(),"batch/nonobject frame refused");
        partial=QByteArray(ai::MaxFrame+1,'x');error.clear();check(!ai::takeFrame(partial,&frame,&error)&&!error.isEmpty(),"oversized partial frame refused");
        QString diagnostic;check(!ai::validateArguments("read_terrain",{{"width",65}},&diagnostic),"sample grid bounds validated");
        check(!ai::validateArguments("get_context",{{"write",true}},&diagnostic),"unexpected arguments refused");
        check(!ai::validateArguments("save_map",{},&diagnostic),"no mutation dispatch route");

        Client client(bridge);
        check(client.call("tools/list").contains("error"),"legacy requests require initialization");
        check(client.call("server/discover",{},true)["result"].toObject()["supportedVersions"].toArray().contains("2026-07-28"),"current MCP discovery");
        check(client.call("tools/list",{},true)["result"].toObject()["resultType"]=="complete","modern result discriminator");
        QJsonObject invalidMeta{{"_meta",QJsonObject{{"io.modelcontextprotocol/protocolVersion","1900-01-01"}}}};
        check(client.call("server/discover",invalidMeta)["error"].toObject()["code"]==-32022,"unsupported modern version returns supported versions");
        check(client.call("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","test"},{"version","1"}}}})["result"].toObject()["protocolVersion"]=="2025-11-25","legacy MCP initialize");
        check(client.tool("list_instances")["instances"].toArray().isEmpty(),"bridge cannot start or enable editor");
        check(client.tool("get_context").contains("error"),"unconnected bridge fails closed");

        const auto mapRoot=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));
        check(window.loadLandscapeWithProgress(QFileInfo(mapRoot+"/zone1.mpr")),"zone1 private copy loaded through production UI path");
        QFileInfo mobPath(mapRoot+"/zone1.mob");check(window.loadMobWithProgress(mobPath),"zone1 MOB private copy loaded");
        view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"real terrain material palette available");
        check(until([&](){return !view.m_neighborPlacements.isEmpty();},30000),"zone1 has world layout neighbours");
        for(const auto& n:view.m_neighborPlacements)check(n.mprPath.startsWith(mapRoot),"neighbour is a private map copy");
        mode->setCurrentIndex(1);qApp->processEvents();
        const auto descriptors=QDir(ai::discoveryDirectory()).entryInfoList({"*.json"},QDir::Files);
        check(descriptors.size()==1,"human read-only enable publishes one instance");
        const auto descriptor=QJsonDocument::fromJson(read(descriptors[0].absoluteFilePath())).object();
        check(descriptor["secret"].toString().size()==72,"per-session secret created");
        QLocalSocket unauthorized;unauthorized.connectToServer(descriptor["pipe"].toString());check(unauthorized.waitForConnected(1000),"local test client reaches user-only pipe");
        unauthorized.write("{\"id\":1,\"method\":\"get_context\"}\n");unauthorized.flush();
        check(until([&](){return unauthorized.state()==QLocalSocket::UnconnectedState;}),"missing secret connection rejected");
        const auto instances=client.tool("list_instances")["instances"].toArray();
        check(instances.size()==1&&!instances[0].toObject().contains("secret"),"instance enumeration excludes secret");
        auto context=client.tool("connect_instance",{{"instance",descriptor["instance"]}});
        check(context["version"]=="218"&&context["profile"]=="deja-vu","bridge attached to real mapper218/profile");
        check(context["mpr"].toString().endsWith("zone1.mpr")&&!context["mobs"].toArray().isEmpty(),"active MPR/MOB context");
        check(!context["neighbors"].toArray().isEmpty()&&context["verticesX"].toInt()>32,"world neighbours and dimensions returned");
        const auto terrain=client.finish(client.tool("read_terrain",{{"width",4},{"height",3}}));
        check(terrain["samples"].toArray().size()==12&&terrain["statistics"].toObject()["count"].toDouble()>1000,"full terrain statistics and bounded sample");
        check(terrain["samples"].toArray()[0].toObject()["raw"].toInt()==view.m_terrainEdit.height(0,0),"heights match live canonical terrain");
        const int undoIndex=window.m_undoStack->index();const bool dirty=view.m_terrainEdit.isDirty();
        auto paint=client.tool("read_paint",{{"width",2},{"height",2}});
        check(paint["cells"].toArray().size()==4,"paint region bounded");
        check(paint["cells"].toArray()[0].toObject()["packed"].toInt()==view.m_terrainEdit.packedTile(0,0,ETerrainTileLayer::Land),"packed paint reflects live model");
        check(!client.tool("material_palette")["materials"].toArray().isEmpty(),"palette material identities returned");
        auto objects=client.tool("list_objects",{{"limit",3}});
        check(objects["objects"].toArray().size()==3&&objects.contains("nextOffset"),"object pagination");
        check(client.tool("list_objects",{{"query","NO_SUCH_OBJECT_218"}})["objects"].toArray().isEmpty(),"object filter");
        auto* selected=view.currentMob()->nodes().front();const auto state=selected->nodeState();selected->setState(eSelect);
        check(!client.tool("list_objects",{{"selectedOnly",true}})["objects"].toArray().isEmpty(),"selection read without changing it");selected->setState(state);
        auto db=client.finish(client.tool("search_database",{{"limit",3}}));
        check(db["records"].toArray().size()==3&&db["profile"]=="deja-vu","real profile database search");
        check(client.finish(client.tool("search_database",{{"query","NO_SUCH_PROTOTYPE_218"}}))["records"].toArray().isEmpty(),"database filter");
        auto maps=client.finish(client.tool("list_profile_maps",{{"limit",200}}));
        QString mapKey;for(const auto& m:maps["maps"].toArray())if(m.toObject()["file"]=="zone2.mpr")mapKey=m.toObject()["map"].toString();
        check(!mapKey.isEmpty(),"profile catalog contains private zone2 neighbour");
        const auto active=CLandscape::getInstance()->filePath().absoluteFilePath();
        auto other=client.finish(client.tool("read_profile_map",{{"map",mapKey},{"width",2},{"height",2},{"limit",3}}));
        check(other["terrain"].toObject()["samples"].toArray().size()==4&&!other["objects"].toArray().isEmpty(),"background unopened map heights and MOB objects");
        check(CLandscape::getInstance()->filePath().absoluteFilePath()==active,"background read retains active document");
        auto denied=client.tool("read_profile_map",{{"map","../outside.mpr"}});QJsonObject deniedStatus;
        check(until([&](){deniedStatus=client.tool("operation_status",{{"operation",denied["operation"]}});return deniedStatus["status"]=="complete";}),"invalid path read finishes asynchronously");
        check(deniedStatus["result"].toObject().contains("error"),"profile path traversal refused");
        check(client.tool("cancel_operation",{{"operation",denied["operation"]}})["status"]=="cancelled","background operation cancellation");
        check(window.m_undoStack->index()==undoIndex&&view.m_terrainEdit.isDirty()==dirty,"all reads preserve dirty and Undo state");
        QVector3D look(64,64,0);CLandscape::getInstance()->projectPt(look,true);view.moveCameraToPoint(look);view.m_cam->moveAwayOn(100);
        view.makeCurrent();view.paintGL();
        view.grabFrameBuffer(false).save(root+"/framebuffer.png");
        auto snapshot=client.tool("window_snapshot");auto png=QByteArray::fromBase64(snapshot["data"].toString().toLatin1());QImage image;image.loadFromData(png,"PNG");
        check(!image.isNull()&&image.width()<=1024&&image.height()<=768&&png.size()<=384*1024,"bounded real window PNG");write(root+"/window.png",png);
        check(client.call("resources/read",{{"uri","ataman://context"}})["result"].toObject()["contents"].toArray().size()==1,"MCP context resource reads real mapper");

        // Exercise the REAL setup UI against only a private project config.
        const auto configPath=root+"/.mcp.json";
        const QByteArray prior="{\"mcpServers\":{\"preserved\":{\"command\":\"keep.exe\"}},\"custom\":true}";
        write(configPath,prior);int stage=0;QTimer configDriver;
        QObject::connect(&configDriver,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets()) {
            if(auto* file=qobject_cast<QFileDialog*>(widget)) {
                if(stage==1&&file->isVisible()){stage=2;file->selectFile(configPath);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);}continue;
            }
            if(auto* message=qobject_cast<QMessageBox*>(widget)) {
                if(stage==2&&message->isVisible()) {
                    if(message->text().contains("\"mcpServers\""))stage=3;
                    if(auto* yes=message->button(QMessageBox::Yes))yes->click();
                }continue;
            }
            if(auto* d=qobject_cast<QDialog*>(widget))if(stage==0&&d->isVisible()&&d->findChild<QDialogButtonBox*>()) {
                if(auto* combo=d->findChild<QComboBox*>()){combo->setCurrentIndex(1);stage=1;d->accept();}
            }
        }});configDriver.start(10);window.m_aiAgent->configureClient();configDriver.stop();
        const auto config=QJsonDocument::fromJson(read(configPath)).object();
        check(stage==3&&config["custom"].toBool()&&config["mcpServers"].toObject().contains("preserved"),"confirmed Claude project setup preserves unrelated config");
        check(config["mcpServers"].toObject()["ataman"].toObject()["command"].toString().endsWith("EIMapperMcp.exe"),"Claude setup writes stdio bridge command");
        const auto backups=QDir(root).entryInfoList({".mcp.json.ataman-*.bak"},QDir::Files|QDir::Hidden);
        check(backups.size()==1&&read(backups[0].absoluteFilePath())==prior,"config update has exact reversible backup");
        QTimer cancelSetup;QObject::connect(&cancelSetup,&QTimer::timeout,[](){for(auto* widget:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(widget))if(d->isVisible()&&d->findChild<QDialogButtonBox*>())d->reject();});
        cancelSetup.start(10);const auto acceptedConfig=read(configPath);window.m_aiAgent->configureClient();cancelSetup.stop();
        check(read(configPath)==acceptedConfig&&!QFileInfo::exists(qEnvironmentVariable("CODEX_HOME")+"/config.toml"),"setup Cancel does not write agent configs");

        // Exact cache bound and expiry, without sleeping through a minute.
        for(auto& j:window.m_aiAgent->m_jobs)j.created=QDateTime::currentDateTimeUtc().addSecs(-61);
        window.m_aiAgent->prune();check(window.m_aiAgent->m_jobs.isEmpty(),"expired operation results are evicted");
        QStringList boundedJobs;
        for(int i=0;i<8;++i){auto start=client.tool("read_terrain",{{"width",1},{"height",1}});client.finish(start);boundedJobs.append(start["operation"].toString());}
        check(client.tool("read_terrain").contains("error"),"operation/result cache bounded to eight");
        for(const auto& id:boundedJobs)client.tool("cancel_operation",{{"operation",id}});
        check(window.m_aiAgent->m_jobs.isEmpty(),"explicit cancellations release all cached results");
        auto pending=client.tool("list_profile_maps");check(pending.contains("operation"),"background read active before OFF");
        mode->setCurrentIndex(0);qApp->processEvents();check(!window.m_aiAgent->enabled(),"OFF stops local listener");
        check(QDir(ai::discoveryDirectory()).entryList({"*.json"},QDir::Files).isEmpty(),"OFF removes owned discovery");
        check(client.tool("get_context").contains("error"),"OFF tears down attached bridge");
        mode->setCurrentIndex(1);qApp->processEvents();
        const auto nextFiles=QDir(ai::discoveryDirectory()).entryInfoList({"*.json"},QDir::Files);
        const auto next=QJsonDocument::fromJson(read(nextFiles[0].absoluteFilePath())).object();
        check(next["secret"]!=descriptor["secret"],"re-enable rotates authority");
        check(!client.tool("connect_instance",{{"instance",next["instance"]}}).contains("error"),"bridge reconnects after human re-enable");

        // A fake local peer exercises late/out-of-order replies through the REAL bridge.
        QLocalServer fake;fake.setSocketOptions(QLocalServer::UserAccessOption);
        const auto fakeId=QUuid::createUuid().toString(QUuid::WithoutBraces);check(fake.listen("ei-ataman-"+fakeId),"isolated fake local peer");
        const QString fakeSecret=QString(72,'a');const auto fakePath=QDir(ai::discoveryDirectory()).filePath(fakeId+".json");
        write(fakePath,QJsonDocument(QJsonObject{{"instance",fakeId},{"pipe",fake.serverName()},{"secret",fakeSecret}}).toJson());
        int count=0;QObject::connect(&fake,&QLocalServer::newConnection,&fake,[&](){
            while(fake.hasPendingConnections()) {
                auto* s=fake.nextPendingConnection();QObject::connect(s,&QLocalSocket::readyRead,s,[&,s](){
                    auto bytes=s->property("buffer").toByteArray()+s->readAll();QJsonObject request;QString diagnostic;
                    if(!ai::takeFrame(bytes,&request,&diagnostic)){s->setProperty("buffer",bytes);return;}
                    const int n=++count;auto reply=QJsonDocument(QJsonObject{{"id",request["id"]},{"result",QJsonObject{{"echo",n}}}}).toJson(QJsonDocument::Compact)+'\n';
                    if(n==2)QTimer::singleShot(2500,s,[s,reply](){s->write(reply);});
                    else {s->write("{\"id\":\"LATE-WRONG-ID\",\"result\":{\"wrong\":true}}\n");s->write(reply);}
                });
            }
        });
        check(client.tool("connect_instance",{{"instance",fakeId}})["echo"]==1,"out-of-order response ID discarded");
        check(client.tool("get_context")["error"].toString().contains("timeout"),"local deadline discards connection");
        QTest::qWait(600);
        check(client.tool("get_context").contains("error"),"no stale reply accepted after timeout");
        check(client.tool("connect_instance",{{"instance",fakeId}})["echo"]==3,"reconnect recovers after late reply");
        client.tool("disconnect_instance");fake.close();QFile::remove(fakePath);
        check(!client.tool("connect_instance",{{"instance",next["instance"]}}).contains("error"),"reattach before real mapper close");
        const auto events=window.findChild<QPlainTextEdit*>();Q_UNUSED(events);
        for(auto* log:window.m_aiAgent->dock()->findChildren<QPlainTextEdit*>())check(!log->toPlainText().contains(next["secret"].toString()),"panel event journal contains no secret");

        auto pendingClose=client.tool("read_profile_map",{{"map",mapKey}});check(pendingClose.contains("operation"),"background indexing/read pending at close");
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){if(auto* no=box->button(QMessageBox::No))no->click();else box->reject();}});dialogs.start(10);
        QElapsedTimer closeTime;closeTime.start();QPointer<CView> guarded=window.view();
        QTimer::singleShot(0,&window,[&](){window.close();});
        QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){std::_Exit(124);});watchdog.start(30000);
        qApp->exec();watchdog.stop();
        check(!window.view()&&guarded.isNull(),"211 deletes viewport while automation is already stopped");
        check(closeTime.elapsed()<30000,"close with attached agent and worker does not hang");
        check(!window.m_aiAgent->enabled()&&QDir(ai::discoveryDirectory()).entryList({"*.json"},QDir::Files).isEmpty(),"close retires server and discovery");
        check(client.tool("get_context").contains("error"),"closed mapper cannot be read");
        check(read(CLogger::getInstance()->logFilePath()).contains("Shutdown complete status:clean"),"211 production clean shutdown journal");
        out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"FAIL "<<error<<"\n";out.flush();return 1;}
}

#include <QtWidgets>
#include <QtTest>
#include <QtNetwork>
#include "main_window.h"
#include "backup_manager.h"
#include "res_save_coordinator.h"
#include "ai_agent_server.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "settings.h"
#include "log.h"
#include "resource_setup_dialog.h"
#include "resource_profile.h"
#include "profile_database.h"
#include "resourcemanager.h"
#include "camera.h"
#include "ai_agent_edit.h"
#include "landscape_tools.h"
#include "terrain_save_flow_policy.h"
#include "terrain_atlas_archive_binding.h"
#include "natural_terrain.h"
#include <QtEndian>
#include <QOpenGLTexture>
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
    int notifications{0};
    void beginTool(QString name,QJsonObject args={}){auto payload=QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",QString("concurrent")},{"method","tools/call"},{"params",QJsonObject{{"name",name},{"arguments",args}}}}).toJson(QJsonDocument::Compact)+'\n';process.write(payload);}
    QJsonObject endTool(){auto r=raw({})["result"].toObject();return QJsonDocument::fromJson(r["content"].toArray()[0].toObject()["text"].toString().toUtf8()).object();}
    explicit Client(const QString& exe){process.start(exe,QStringList{});check(process.waitForStarted(10000),"stdio bridge starts independently");}
    ~Client(){process.closeWriteChannel();if(!process.waitForFinished(2000)){process.kill();process.waitForFinished(2000);}}
    QJsonObject raw(QByteArray payload){process.write(payload);while(true){if(!until([&](){buffer+=process.readAllStandardOutput();return buffer.contains('\n');},35000))throw QString("stdio response deadline");QJsonObject value;QString error;if(!ai::takeFrame(buffer,&value,&error))throw error;if(value["method"]=="notifications/tools/list_changed"){++notifications;continue;}return value;}}
    QJsonObject call(QString method,QJsonObject params={},bool modern=false){
        if(modern)params.insert("_meta",QJsonObject{{"io.modelcontextprotocol/protocolVersion","2026-07-28"},
            {"io.modelcontextprotocol/clientInfo",QJsonObject{{"name","Ataman native test"},{"version","1"}}},{"io.modelcontextprotocol/clientCapabilities",QJsonObject{}}});
        const auto id=QString("request-%1").arg(++serial);
        auto response=raw(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        if(response["id"]!=id)throw QString("MCP response ID mismatch");
        if(method=="initialize")process.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        return response;
    }
    QJsonObject tool(QString name,QJsonObject args={}) {
        const auto response=call("tools/call",{{"name",name},{"arguments",args}});
        if(!response.contains("result"))throw QString::fromUtf8(QJsonDocument(response).toJson());
        const auto result=response["result"].toObject();const auto content=result["content"].toArray();
        if(!content.isEmpty()&&content[0].toObject()["type"]=="image")return content[0].toObject();
        return QJsonDocument::fromJson(content.at(0).toObject()["text"].toString().toUtf8()).object();
    }
    QJsonObject finish(QJsonObject start,bool allowError=false) {
        if(!start.contains("operation"))throw QString::fromUtf8(QJsonDocument(start).toJson());
        QJsonObject status;check(until([&](){status=tool("operation_status",{{"operation",start["operation"]}});if(status.contains("error")&&!status["error"].toString().startsWith("Normal map commit in progress"))throw status["error"].toString();return status["status"]=="complete";},140000),"background operation completes");
        const auto result=status["result"].toObject();if(result["status"]!="preview")tool("cancel_operation",{{"operation",start["operation"]}});if(result.contains("error")&&!allowError)throw result["error"].toString();return result;
    }
};
}

#include <windows.h>
namespace {
QString ownedExe;
QSet<DWORD> ownedPids;int visibleWindows=0;
bool owned(DWORD pid) {
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!h)return false;
    using Query=BOOL(WINAPI*)(HANDLE,DWORD,LPWSTR,PDWORD);
    static const auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"QueryFullProcessImageNameW"));
    wchar_t name[32768];DWORD length=32768;const bool ok=query&&query(h,0,name,&length);CloseHandle(h);
    return ok&&QDir::fromNativeSeparators(QString::fromWCharArray(name,int(length))).compare(ownedExe,Qt::CaseInsensitive)==0;
}
BOOL CALLBACK inspectWindow(HWND w,LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    if(IsWindowVisible(w)&&owned(pid)){++visibleWindows;ownedPids.insert(pid);}return TRUE;
}
bool alive(DWORD pid){HANDLE h=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!h)return false;const bool yes=WaitForSingleObject(h,0)==WAIT_TIMEOUT;CloseHandle(h);return yes;}
QJsonObject background() {
    for(const auto& f:QDir(ai::discoveryDirectory()).entryInfoList({"*.json"},QDir::Files)) {
        const auto d=QJsonDocument::fromJson(read(f.absoluteFilePath())).object();
        const auto pid=DWORD(d["pid"].toDouble());if(d["background"].toBool()&&owned(pid)&&alive(pid)){ownedPids.insert(pid);return d;}
    }return {};
}
struct OwnedCleanup {~OwnedCleanup(){for(auto pid:ownedPids)if(owned(pid)&&alive(pid)){HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pid);if(h){TerminateProcess(h,99);CloseHandle(h);}}}};
QJsonObject terrain(Client& c){return c.finish(c.tool("read_terrain",{{"width",8},{"height",8},{"step",8}}));}
QJsonObject run(Client& c,QString name,QJsonObject args={}) {
    QElapsedTimer deadline;deadline.start();
    while(deadline.elapsed()<30000) {
        args["revision"]=c.tool("get_revision")["revision"];
        const auto result=c.tool(name,args);
        if(result.contains("operation"))return c.finish(result);
        const auto error=result["error"].toString();
        // These refusals happen before accepting an operation. Never replay an
        // accepted mutation or a transport failure whose outcome is unknown.
        if(error.contains("interaction/load in progress")||error=="Editor is busy"||error.startsWith("Normal map commit in progress")){QTest::qWait(100);continue;}
        if(!error.isEmpty())throw name+": "+error;return result;
    }
    throw name+": bounded wait for workspace readiness exceeded";
}
}

int runAssemblyCatalogCheck()
{
    OwnedCleanup cleanup;
    try {
        out.setCodec("UTF-8");const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const auto maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));
        const auto bridge=qEnvironmentVariable("EI_MCP_TEST_BRIDGE");ownedExe=root+"/app/EIMapperEndurance.exe";
        check(!root.isEmpty()&&!bridge.isEmpty(),"explicit isolated272 profile and bridge");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QSettings authority;authority.setValue("ui/aiAgentMode",2);authority.setValue("ui/aiAgentAllowSave",true);authority.sync();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);QTimer visibility;
        QObject::connect(&visibility,&QTimer::timeout,[](){EnumWindows(inspectWindow,0);background();});visibility.start(10);
        const auto original=read(maps+"/zone1.mob"),options=read(root+"/app/endurance-options.json");
        Client agent(bridge);auto init=agent.call("initialize",{{"protocolVersion","2025-11-25"}});
        check(init["result"].toObject()["capabilities"].toObject()["tools"].toObject()["listChanged"]==true,"bridge advertises dynamic catalog notifications222");
        check(agent.tool("connect_instance",{{"target","background"}})["background"]==true,"real bridge starts invisible headless under saved224 authority");
        auto catalog=agent.call("tools/list")["result"].toObject()["tools"].toArray();QSet<QString> names;
        for(const auto& t:catalog)names.insert(t.toObject()["name"].toString());
        check(names.contains("edit_objects")&&names.contains("query_passability")&&names.contains("rebuild_ai_graph")&&names.contains("validate_map")&&agent.notifications>0,"live catalog272 and list_changed arrive without restarting client");
        run(agent,"open_map",{{"map","zone1"}});
        check(until([&](){return !agent.tool("get_context")["loadingAddMobs"].toBool();},120000),"full zone1/AddMob family loaded before edit");
        auto db=agent.finish(agent.tool("search_database",{{"category","prototypes"},{"query","stbuho7"}}));
        check(!db["records"].toArray().isEmpty(),"agent finds stbuho7 through search_database");
        const auto baselineValidation=agent.finish(agent.tool("validate_map"));
        write(root+"/validation-before.json",QJsonDocument(baselineValidation).toJson());
        if(qEnvironmentVariableIsSet("EI_MCP_272_GUARDS_ONLY")){
            const QJsonObject spec{{"prototype","stbuho7"},{"x",20},{"y",20}};
            QJsonArray tooMany;for(int i=0;i<501;++i)tooMany.append(spec);
            check(agent.tool("edit_objects",{{"revision",agent.tool("get_revision")["revision"]},{"action","place"},{"objects",tooMany}}).contains("error"),"501 object request rejected before mutation");
            QJsonArray points201;for(int i=0;i<201;++i)points201.append(QJsonArray{20,20});
            check(agent.tool("query_passability",{{"layer",3},{"points",points201}}).contains("error"),"201 point request rejected at limit200");
            auto preview=run(agent,"edit_objects",{{"action","delete"},{"ids",QJsonArray{324}},{"preview",true}});
            run(agent,"edit_objects",{{"action","place"},{"objects",QJsonArray{spec}}});
            auto conflict=agent.tool("apply",{{"operation",preview["operation"]},{"revision",preview["revision"]}});
            check(conflict["error"].toString().contains("conflict"),"real intervening document edit invalidates completed object preview");
            agent.tool("cancel_operation",{{"operation",preview["operation"]}});run(agent,"undo_last_agent_operation");run(agent,"save");
            check(read(maps+"/zone1.mob")==original,"revision conflict plus Undo preserves original disk MOB");
            preview=run(agent,"edit_objects",{{"action","delete"},{"ids",QJsonArray{324}},{"preview",true}});
            auto externallyChanged=original;externallyChanged[externallyChanged.size()-1]=char(externallyChanged.back()^1);write(maps+"/zone1.mob",externallyChanged);
            conflict=agent.finish(agent.tool("apply",{{"operation",preview["operation"]},{"revision",preview["revision"]}}),true);
            write(maps+"/zone1.mob",original);
            check(conflict["error"].toString().contains("revision conflict"),"external disk change invalidates captured MOB candidate before apply");
            authority.setValue("ui/aiAgentAllowSave",false);authority.sync();
            run(agent,"edit_objects",{{"action","place"},{"objects",QJsonArray{spec}}});
            const auto noSave=agent.tool("save",{{"revision",agent.tool("get_revision")["revision"]}});
            check(noSave.contains("error")&&read(maps+"/zone1.mob")==original,"Edit without Save permission changes memory but refuses disk publication");
            run(agent,"undo_last_agent_operation");authority.setValue("ui/aiAgentAllowSave",true);authority.sync();
            auto start=agent.tool("edit_objects",{{"revision",agent.tool("get_revision")["revision"]},{"action","place"},{"objects",QJsonArray{spec}},{"preview",true}});
            check(start.contains("operation")&&!agent.tool("cancel_operation",{{"operation",start["operation"]}}).contains("error"),"cancel a pending object preview through real bridge");
            check(agent.tool("get_context")["dirty"]==false&&read(maps+"/zone1.mob")==original,"cancelled preparation leaves memory and disk unchanged");
            QTest::qWait(250);
            check(agent.tool("shutdown_headless")["status"]=="shutting_down"&&until([](){return background().isEmpty();},10000),"guard-check headless closes cleanly");
            visibility.stop();check(visibleWindows==0,"guard checks create0 visible native windows");
            out<<"RESULT "<<checks<<" PASS,0 FAIL\n";out.flush();return 0;
        }
        const QJsonObject selector{{"action","delete"},{"area",QJsonObject{{"shape","rectangle"},{"x",180},{"y",155},{"width",2},{"height",2}}},{"prototype","stbuho7"},{"preview",true}};
        auto preview=run(agent,"edit_objects",selector);write(root+"/delete-preview.json",QJsonDocument(preview).toJson());
        check(preview["count"].toInt()==1&&preview["affected"].toArray()[0].toObject()["id"].toInt()==324,"area/prototype dry-run selects exactly R4 house324 stbuho7");
        check(read(maps+"/zone1.mob")==original,"preview does not write MOB");
        auto stale=agent.tool("apply",{{"operation",preview["operation"]},{"revision","stale"}});
        check(stale["error"].toString().contains("conflict"),"revision conflict refuses preview application");
        check(!agent.tool("cancel_operation",{{"operation",preview["operation"]}}).contains("error"),"cancel discards object preview");
        auto deleted=run(agent,"edit_objects",{{"action","delete"},{"ids",QJsonArray{324}}});
        check(deleted["count"].toInt()==1&&agent.tool("get_context")["dirty"]==true,"real MCP deletion sets document dirty");
        auto points=QJsonDocument::fromJson(read(root+"/points.json")).array();
        auto passBefore=agent.finish(agent.tool("query_passability",{{"layer",0},{"points",points},{"radius",8}}));
        check(passBefore["graphStale"]==true&&!passBefore["warning"].toString().isEmpty(),"passability explicitly reports stale graph after object deletion");
        run(agent,"undo_last_agent_operation");run(agent,"save");
        check(read(maps+"/zone1.mob")==original,"Undo deletion then normal Save restores byte-identical MOB");
        QJsonArray specs;for(int i=0;i<3;++i)specs.append(QJsonObject{{"prototype","stbuho7"},{"x",20+i*6},{"y",20},{"rotation",QJsonArray{0,0,15}},{"scale",QJsonArray{0.9,0.3,0.7}}});
        auto placed=run(agent,"edit_objects",{{"action","place"},{"objects",specs}});QJsonArray ids;
        for(const auto& r:placed["affected"].toArray())ids.append(r.toObject()["id"]);
        check(ids.size()==3&&ids[0]!=ids[1]&&ids[1]!=ids[2],"three static database objects receive distinct family IDs");
        run(agent,"edit_objects",{{"action","transform"},{"ids",ids},{"delta",QJsonArray{2,3,0}},{"rotation",45}});
        auto copied=run(agent,"edit_objects",{{"action","copy"},{"ids",ids},{"delta",QJsonArray{0,15,0}},{"rotation",30}});
        QSet<int> allIds;for(const auto& id:ids)allIds.insert(id.toInt());for(const auto& c:copied["affected"].toArray())allIds.insert(c.toObject()["id"].toInt());
        check(copied["count"].toInt()==3&&allIds.size()==6,"group copy with rotation uses another three unique IDs");
        run(agent,"undo_last_agent_operation");run(agent,"undo_last_agent_operation");run(agent,"undo_last_agent_operation");run(agent,"save");
        check(read(maps+"/zone1.mob")==original,"Undo copy/transform/place restores original MOB byte for byte");
        const auto units=agent.tool("list_objects",{{"type",7},{"limit",1}});
        write(root+"/unit-query.json",QJsonDocument(units).toJson());
        const QJsonValue unitId=units["objects"].toArray()[0].toObject()["id"];
        auto denied=agent.tool("edit_objects",{{"revision",agent.tool("get_revision")["revision"]},{"action","delete"},{"ids",QJsonArray{unitId}}});
        if(denied.contains("operation"))denied=agent.finish(denied,true);
        check(denied["error"].toString().contains("Units are read-only"),"unit edit explicitly refused");
        // Original R4 scenario, now performed exclusively through tools/call.
        run(agent,"edit_objects",{{"action","delete"},{"prototype","stbuho7"},{"area",QJsonObject{{"shape","polygon"},{"points",QJsonArray{QJsonArray{179,154},QJsonArray{182,154},QJsonArray{182,158},QJsonArray{179,158}}}}}});
        auto graphPreview=run(agent,"rebuild_ai_graph",{{"preview",true}});
        check(graphPreview["layers"].toInt()==8,"graph preview builds all8 native layers without applying");
        agent.tool("cancel_operation",{{"operation",graphPreview["operation"]}});
        auto rebuilt=run(agent,"rebuild_ai_graph");write(root+"/graph-result.json",QJsonDocument(rebuilt).toJson());
        auto pass=agent.finish(agent.tool("query_passability",{{"layer",3},{"points",points},{"radius",8}}));
        write(root+"/passability.json",QJsonDocument(pass).toJson());
        check(pass["graphStale"]==false&&pass["points"].toArray().size()==points.size(),"fresh graph and basecam squad point results returned through MCP");
        for(int layer=0;layer<8;++layer){if(layer==3)continue;auto r=agent.finish(agent.tool("query_passability",{{"layer",layer},{"points",QJsonArray{points[0]}},{"radius",0}}));check(r["layer"].toInt()==layer&&r["points"].toArray().size()==1,"selected legacy layer"+QString::number(layer)+" classifies point");}
        auto validation=agent.finish(agent.tool("validate_map"));write(root+"/validation.json",QJsonDocument(validation).toJson());
        const bool absoluteClean=validation["warnings"].toInt()==0&&validation["errors"].toInt()==0;
        write(root+"/validation-gate.json",QJsonDocument(QJsonObject{{"zeroAllWarningError",absoluteClean},{"warnings",validation["warnings"]},{"errors",validation["errors"]}}).toJson());
        if(!absoluteClean)out<<"OPEN absolute0 Warning/Error gate; original findings="<<baselineValidation["warnings"].toInt()<<" Warning/"<<baselineValidation["errors"].toInt()<<" Error\n";
        const auto stableFindings=[](const QJsonValue& values){QJsonArray rows;for(const auto& value:values.toArray()){
            auto row=value.toObject();auto path=row["path"].toString();path.replace(QRegularExpression("/OBJECT\\[\\d+\\]"),"/OBJECT[*]");row["path"]=path;rows.append(row);}return rows;};
        check(validation["errors"].toInt()==0&&stableFindings(validation["findings"])==stableFindings(baselineValidation["findings"]),"native full validation:0 Error and0 new findings; only raw object ordinals shift after deletion");
        run(agent,"save");CMobDocument result,reference;CValidationReport report;QByteArray actualGraph,r4Graph;
        check(result.loadBytes(read(maps+"/zone1.mob"),"zone1.mob",&report)&&reference.loadBytes(read(root+"/r4-zone1.mob"),"R4.mob",&report),"saved MCP result and independent R4 reference parse");
        check(result.embeddedAiGraph(&actualGraph,&report)&&reference.embeddedAiGraph(&r4Graph,&report),"both results contain embedded graphs");
        CAiGraphDocument actual,r4;QString error;
        check(actual.parse(actualGraph,&error)&&r4.parse(r4Graph,&error),"both saved graphs parse");
        const bool equivalent=actual.compareNavigation(r4).identical();
        write(root+"/graph-equivalence-gate.json",QJsonDocument(QJsonObject{{"equivalentToR4",equivalent}}).toJson());
        if(equivalent)check(true,"MCP rebuilt graph is navigation-equivalent to R4 in all8 layers");
        else out<<"OPEN graph equivalence to R4; continuing independent checks\n";
        bool hasHouse=false;for(const auto& o:result.objects())hasHouse|=o.nid==324;
        check(!hasHouse&&result.objects().size()+1==reference.objects().size()+1,"exact R4 house is absent and remaining object count agrees");
        QByteArray normalized;check(result.replaceEmbeddedAiGraph(r4Graph,&normalized,&report)&&normalized==read(root+"/r4-zone1.mob"),"MCP result matches R4 byte for byte outside the graph");
        if(equivalent)check(read(maps+"/zone1.mob")==read(root+"/r4-zone1.mob"),"MCP deletion+graph result byte-identical to accepted R4");
        check(agent.tool("shutdown_headless")["status"]=="shutting_down"&&until([](){return background().isEmpty();},10000),"clean headless shuts down through real bridge");
        agent.tool("disconnect_instance");
        // Production MainWindow on an invisible surface. The bridge edits the
        // exact window document; snapshots render this test process only.
        qApp->setProperty("workspaceLoadSmoke",true);MainWindow editor;editor.configureAutosaveTimer(false,300000);
        editor.m_currentCollectionPath=root+"/test.collection";editor.resize(2100,1400);editor.setAttribute(Qt::WA_DontShowOnScreen);editor.show();editor.view()->makeCurrent();
        Client visibleAdapter(bridge);visibleAdapter.call("initialize",{{"protocolVersion","2025-11-25"}});
        check(visibleAdapter.tool("connect_instance",{{"target","editor"}})["background"]==false,"bridge selects offscreen production MainWindow");
        run(visibleAdapter,"open_map",{{"map","zone1"}});
        check(until([&](){return !visibleAdapter.tool("get_context")["loadingAddMobs"].toBool();},120000),"editor completes MOB family loading");
        const auto baseline=read(maps+"/zone1.mob");auto* primary=editor.view()->currentMob();
        for(auto* m:editor.view()->mobs())if(m->filePath().fileName().compare("zone1.mob",Qt::CaseInsensitive)==0)primary=m;
        check(primary!=nullptr,"editor primary MOB available");
        auto* viewport=editor.view();viewport->m_cam->setLocation(QVector3D(176,144,430),QVector3D(176,144,0),QVector3D(0,0,0));
        for(int i=0;i<14;++i){viewport->makeCurrent();viewport->paintGL();qApp->processEvents(QEventLoop::AllEvents,20);QTest::qWait(20);}
        SAiGraphPreviewBuildInput diagnostic;check(editor.view()->captureAiGraphPreviewBuildInput(&diagnostic,&error),"native editor classifier capture");
        auto diagnosticResult=CAiGraphPreviewBuilder::build(diagnostic);auto diagnosticGraph=diagnosticResult.document;diagnosticGraph.recolorConnectedComponents(&error);
        write(root+"/native-graph-diagnostic.json",QJsonDocument(QJsonObject{{"collisionProxies",diagnostic.collisionProxies.size()},{"authoredProxies",diagnosticResult.authoredCollisionProxyCount},{"blockedAiTiles",diagnosticResult.blockedAiTiles},{"sameAsHeadless",diagnosticGraph.compareNavigation(actual).identical()},{"sameAsR4",diagnosticGraph.compareNavigation(r4).identical()}}).toJson());
        const auto captureWindow=[&](const QString& path){viewport->makeCurrent();viewport->paintGL();const auto gl=viewport->grabFrameBuffer();
            auto shell=editor.grab().toImage();QPainter painter(&shell);painter.drawImage(viewport->mapTo(&editor,QPoint()),gl);
            for(auto* child:viewport->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(child->isVisible())child->render(&painter,child->mapTo(&editor,QPoint()));
            painter.end();check(!gl.isNull()&&shell.save(path),"offscreen production MainWindow widgets + native GL framebuffer capture");};
        const auto count=primary->nodes().size();editor.view()->moveCameraToPoint(QVector3D(26,20,0));QVector3D cp,focus,cr;viewport->m_cam->getLocation(cp,focus,cr);viewport->m_cam->setLocation(focus+QVector3D(0,0,70),focus,QVector3D(-45,0,0));QTest::qWait(150);
        captureWindow(root+"/mainwindow-before.png");
        auto three=run(visibleAdapter,"edit_objects",{{"action","place"},{"objects",specs}});
        check(primary->nodes().size()==count+3&&primary->isDirty()&&editor.m_undoStack->count()==1,"agent changes visible immediately in production window document/tree with one Undo");
        QTest::qWait(150);captureWindow(root+"/mainwindow-after.png");
        QByteArray placedBytes;check(primary->buildValidatedCandidate(&placedBytes,&report),"placed editor candidate uses native serializer");
        run(visibleAdapter,"rebuild_ai_graph");run(visibleAdapter,"undo_last_agent_operation");QByteArray graphUndoBytes;
        check(primary->buildValidatedCandidate(&graphUndoBytes,&report)&&graphUndoBytes==placedBytes,"Undo graph rebuild restores exact prior MOB including graph and object changes");
        QJsonArray editorIds;for(const auto& r:three["affected"].toArray())editorIds.append(r.toObject()["id"]);
        run(visibleAdapter,"edit_objects",{{"action","transform"},{"ids",editorIds},{"delta",QJsonArray{3,0,0}},{"rotation",25}});
        QByteArray movedBytes;check(primary->buildValidatedCandidate(&movedBytes,&report)&&movedBytes!=placedBytes,"native group move changes candidate");
        run(visibleAdapter,"edit_objects",{{"action","copy"},{"ids",editorIds},{"delta",QJsonArray{0,20,0}}});
        run(visibleAdapter,"undo_last_agent_operation");QByteArray undone;
        check(primary->buildValidatedCandidate(&undone,&report)&&undone==movedBytes,"Undo copy restores exact previous MOB candidate");
        run(visibleAdapter,"undo_last_agent_operation");
        check(primary->buildValidatedCandidate(&undone,&report)&&undone==placedBytes,"Undo transform restores exact previous MOB candidate");
        run(visibleAdapter,"undo_last_agent_operation");
        check(primary->buildValidatedCandidate(&undone,&report)&&undone==baseline&&!primary->isDirty(),"Undo placement restores complete byte-identical original MOB and clean state");
        // A real incoming script reference is added only to this isolated
        // document; refusal must include its owner/line and leave bytes alone.
        auto row=run(visibleAdapter,"edit_objects",{{"action","place"},{"objects",QJsonArray{specs[0]}}});const QJsonValue newId=row["affected"].toArray()[0].toObject()["id"];
        const auto script=primary->script();primary->setScript(script+QString("\nCheck272=GetObjectByID(\"%1\");\n").arg(newId.toInt()));editor.view()->setDurty(primary);
        auto blocked=visibleAdapter.tool("edit_objects",{{"revision",visibleAdapter.tool("get_revision")["revision"]},{"action","delete"},{"ids",QJsonArray{newId}}});
        if(blocked.contains("operation"))blocked=visibleAdapter.finish(blocked,true);
        check(blocked["error"].toString().contains("Referenced object ID")&&blocked["error"].toString().contains("zone1.mob:"),"incoming script reference refuses deletion with path and line");
        primary->setScript(script);editor.view()->setDurty(primary);run(visibleAdapter,"undo_last_agent_operation");
        const auto terrainBefore=terrain(visibleAdapter);
        run(visibleAdapter,"sculpt",{{"action","raise"},{"amount",0.25},{"area",QJsonObject{{"shape","circle"},{"x",20},{"y",20},{"radius",3}}}});
        run(visibleAdapter,"undo_last_agent_operation");check(terrain(visibleAdapter)==terrainBefore,"existing terrain sculpt and single Undo regression219");
        editor.m_aiAgent->m_mode->setCurrentIndex(1);
        visibleAdapter.tool("connect_instance",{{"target","editor"}});
        auto readonly=visibleAdapter.tool("edit_objects",{{"revision",visibleAdapter.tool("get_revision")["revision"]},{"action","delete"},{"ids",QJsonArray{1}}});
        check(readonly["error"].toString().contains("Read-only"),"Read-only dock refuses object mutations");
        auto readCheck=visibleAdapter.finish(visibleAdapter.tool("query_passability",{{"layer",0},{"points",QJsonArray{points[0]}},{"radius",1}}));
        check(readCheck.contains("points"),"passability remains available in Read-only mode");
        visibleAdapter.tool("disconnect_instance");editor.close();
        visibility.stop();check(visibleWindows==0,"zero visible native windows across test-owned mapper processes");
        check(read(root+"/app/endurance-options.json")!=QByteArray(),"isolated options remain readable; owner options are externally hash-guarded");
        out<<"RESULT "<<checks<<" PASS,0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\nRESULT "<<checks<<" PASS,1 FAIL\n";out.flush();return 1;}
}

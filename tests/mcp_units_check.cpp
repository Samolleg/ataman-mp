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
#include "unit.h"
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

namespace {
QJsonObject readUnits(Client& c,QJsonObject a={}){return c.finish(c.tool("list_units",a));}
QJsonObject getUnit(Client& c,QJsonValue id){return c.finish(c.tool("get_unit",{{"id",id}}))["units"].toArray()[0].toObject();}
QJsonObject denial(Client& c,QJsonObject a){a["revision"]=c.tool("get_revision")["revision"];auto r=c.tool("edit_units",a);return r.contains("operation")?c.finish(r,true):r;}
QJsonArray nativePoints(QJsonArray logics){QJsonArray result;for(const auto& l:logics){auto o=l.toObject();result.append(o["Guard placement"]);for(const auto& p:o["Patrol Points"].toArray()){auto t=p.toObject();result.append(t["Position"]);for(const auto& v:t["Looking points"].toArray())result.append(v.toObject()["Position"]);}}return result;}
QVector3D xyz(QJsonArray a){return {float(a[0].toDouble()),float(a[1].toDouble()),float(a[2].toDouble())};}
void strict(const QByteArray& bytes,QString name){CMobDocument doc;CValidationReport report;check(doc.loadBytes(bytes,name,&report)&&!report.hasErrors()&&doc.serialize()==bytes,"strict parse/coverage/lossless "+name);bool coverage=false;for(const auto& f:report.findings())coverage|=f.code.contains("coverage");write(qEnvironmentVariable("EI_MCP_TEST_ROOT")+"/parse-"+name+".json",QJsonDocument(report.toJson()).toJson());check(coverage,"strict byte coverage reported "+name);}
}

int runAssemblyCatalogCheck()
{
    OwnedCleanup cleanup;
    try{
        out.setCodec("UTF-8");const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const auto maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));const auto bridge=qEnvironmentVariable("EI_MCP_TEST_BRIDGE");ownedExe=root+"/app/EIMapperEndurance.exe";
        check(!root.isEmpty()&&!bridge.isEmpty(),"explicit isolated273 fixture");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QSettings authority;authority.setValue("ui/aiAgentMode",2);authority.setValue("ui/aiAgentAllowSave",true);authority.sync();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);Hidden hidden;qApp->installEventFilter(&hidden);
        QTimer visibility;QObject::connect(&visibility,&QTimer::timeout,[](){EnumWindows(inspectWindow,0);background();});visibility.start(10);
        const auto original=read(maps+"/basecam.mob"),targetOriginal=read(maps+"/zone1.mob");
        Client agent(bridge);auto init=agent.call("initialize",{{"protocolVersion","2025-11-25"}});
        check(init["result"].toObject()["capabilities"].toObject()["tools"].toObject()["listChanged"]==true,"dynamic catalog listChanged222");
        check(agent.tool("connect_instance",{{"target","background"}})["background"]==true,"real production bridge starts isolated headless225");
        QSet<QString> names;for(const auto& t:agent.call("tools/list")["result"].toObject()["tools"].toArray())names.insert(t.toObject()["name"].toString());
        check(names.contains("list_units")&&names.contains("get_unit")&&names.contains("edit_units")&&agent.notifications>0,"hot273 catalog and notifications without client restart");
        const auto open=[&](QString map){run(agent,"open_map",{{"map",map}});check(until([&](){return !agent.tool("get_context")["loadingAddMobs"].toBool();},120000),"complete MOB/AddMob family "+map);};
        open("basecam");
        const auto baseline=agent.finish(agent.tool("validate_map"));write(root+"/validation-before.json",QJsonDocument(baseline).toJson());
        auto all=readUnits(agent);write(root+"/units.json",QJsonDocument(all).toJson());check(all["count"].toInt()>=5,"list native briefing units with full behavior");
        if(qEnvironmentVariableIsSet("EI_MCP_273_ROUTES_ONLY")){
            const QJsonArray routeIds{167,183332};const auto routeBefore=readUnits(agent,{{"ids",routeIds}})["units"].toArray();
            const QJsonArray layers{QJsonObject{{"id",167},{"layer",3}},QJsonObject{{"id",183332},{"layer",1}}};
            auto transformed=run(agent,"edit_units",{{"action","transform"},{"ids",routeIds},{"delta",QJsonArray{2,1,0}},{"rotation",22},{"pivot",QJsonArray{190,150,0}},{"layers",layers}});
            write(root+"/native-routes-transform.json",QJsonDocument(transformed).toJson());auto routeAfter=readUnits(agent,{{"ids",routeIds}})["units"].toArray();
            int checkedPoints=0;const QVector3D pivot(190,150,0),delta(2,1,0);const auto rotation=QQuaternion::fromAxisAndAngle(0,0,1,22);
            for(int i=0;i<routeBefore.size();++i){auto b=routeBefore[i].toObject(),a=routeAfter[i].toObject();auto old=nativePoints(b["logics"].toArray()),next=nativePoints(a["logics"].toArray());
                check(old.size()>5&&old.size()==next.size(),"real NPC/animal has authored patrol and look points beyond5 guards");
                for(int j=0;j<old.size();++j){check((xyz(next[j].toArray())-(pivot+rotation.rotatedVector(xyz(old[j].toArray())-pivot)+delta)).length()<0.0001,"authored guard/patrol/look rigid transform");++checkedPoints;}
                auto stats=b["stats"].toObject();check(transformed["affected"].toArray()[i].toObject()["passability"].toObject()["layer"]==stats["AI Class Stay"],"mixed group uses appropriate stored native AIClassStay3/1 via layers");
                check(b["position"].toArray()[2]==a["position"].toArray()[2],"real NPC/animal relative Z retained");
            }
            check(checkedPoints>20,"authored paths and targets covered");run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/basecam.mob")==original,"native NPC/animal path transform Undo byte exact");
            auto start=agent.tool("list_units",{{"ids",QJsonArray{159,160,167}}});QJsonObject status;
            check(until([&](){status=agent.tool("operation_status",{{"operation",start["operation"]}});return status["status"]=="complete";},30000),"three-NPC source snapshot with authored patrol/look");const auto source=status["result"].toObject();
            open("zone1");auto targetBaseline=agent.finish(agent.tool("validate_map"));
            auto copy=run(agent,"edit_units",{{"action","copy"},{"sourceOperation",source["sourceOperation"]},{"delta",QJsonArray{-10,-10,0}},{"rotation",10},{"layer",3}});
            agent.tool("cancel_operation",{{"operation",start["operation"]}});write(root+"/native-routes-cross-copy.json",QJsonDocument(copy).toJson());
            bool hasRoute=false;for(const auto& value:copy["affected"].toArray()){auto r=value.toObject();hasRoute|=r["points"].toArray().size()>5;auto actual=getUnit(agent,r["id"]);check(actual["logics"]==r["logicsAfter"],"cross-map copied authored logic equals prepared complete projection");}
            check(hasRoute&&copy["count"].toInt()==3&&!copy["untransferredReferences"].toArray().isEmpty(),"copy3 NPCs includes real authored route/look and explicit scenario warning");
            auto targetAfter=agent.finish(agent.tool("validate_map"));write(root+"/target-validation-before.json",QJsonDocument(targetBaseline).toJson());write(root+"/target-validation-after.json",QJsonDocument(targetAfter).toJson());check(targetBaseline["findings"]==targetAfter["findings"],"authored route copy adds0 Warning/Error");
            run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/zone1.mob")==targetOriginal,"cross-map authored route copy one target Undo byte exact");
            check(agent.tool("shutdown_headless")["status"]=="shutting_down"&&until([](){return background().isEmpty();},10000),"route-check headless closes cleanly");visibility.stop();check(visibleWindows==0,"route checks create0 visible windows");
            out<<"RESULT "<<checks<<" PASS,0 FAIL\n";out.flush();return 0;
        }
        const QJsonArray squad{159,160,161,162,1107534722};auto before=readUnits(agent,{{"ids",squad}})["units"].toArray();
        check(before.size()==5,"find exact rigid squad from briefing R4");
        auto first=before[0].toObject();check(first["logics"].toArray().size()==5&&first.contains("absoluteHeight")&&first.contains("references"),"all5 logic slots, relative Z, absolute reference height and incoming refs");
        auto filtered=readUnits(agent,{{"ids",QJsonArray{first["id"]}},{"prototype",first["prototype"]},{"player",first["player"]},{"group",first["group"]},{"area",QJsonObject{{"shape","rectangle"},{"x",0},{"y",0},{"width",350},{"height",280}}}});
        check(filtered["count"].toInt()>0,"ID/area/prototype/player/group selector intersection");
        auto preview=run(agent,"edit_units",{{"action","transform"},{"ids",squad},{"delta",QJsonArray{3,2,0}},{"rotation",15},{"pivot",QJsonArray{170,170,0}},{"layer",3},{"preview",true}});
        write(root+"/squad-preview.json",QJsonDocument(preview).toJson());check(preview["count"].toInt()==5&&read(maps+"/basecam.mob")==original,"squad preview leaves disk unchanged");
        auto badRevision=agent.tool("apply",{{"operation",preview["operation"]},{"revision","stale"}});check(badRevision["error"].toString().contains("conflict"),"stale apply refuses atomically");
        agent.tool("cancel_operation",{{"operation",preview["operation"]}});
        auto moved=run(agent,"edit_units",{{"action","transform"},{"ids",squad},{"delta",QJsonArray{3,2,0}},{"rotation",15},{"pivot",QJsonArray{170,170,0}},{"layer",3}});
        write(root+"/squad-applied.json",QJsonDocument(moved).toJson());auto after=readUnits(agent,{{"ids",squad}})["units"].toArray();
        const auto rotation=QQuaternion::fromAxisAndAngle(0,0,1,15);const QVector3D pivot(170,170,0),delta(3,2,0);
        for(int i=0;i<before.size();++i){auto b=before[i].toObject(),a=after[i].toObject();auto bp=b["position"].toArray(),ap=a["position"].toArray();
            check(bp[2]==ap[2]&&(xyz(ap)-(pivot+rotation.rotatedVector(xyz(bp)-pivot)+delta)).length()<0.0001,"squad unit XY rigid and relative Z retained "+QString::number(i));
            auto oldPoints=nativePoints(b["logics"].toArray()),newPoints=nativePoints(a["logics"].toArray());check(oldPoints.size()==newPoints.size(),"logic point count retained");
            for(int j=0;j<oldPoints.size();++j)check((xyz(newPoints[j].toArray())-(pivot+rotation.rotatedVector(xyz(oldPoints[j].toArray())-pivot)+delta)).length()<0.0001,"rigid guard/patrol/look all slots");
            const auto r=moved["affected"].toArray()[i].toObject();check(r["passability"].toObject()["layer"].toInt()==3&&r["points"].toArray().size()==oldPoints.size(),"native layer/passability flags for unit and every point");
        }
        run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/basecam.mob")==original,"squad single Undo + normal save restores exact original MOB bytes");
        QJsonValue referenced;for(const auto& v:all["units"].toArray())if(!v.toObject()["references"].toArray().isEmpty()){referenced=v.toObject()["id"];break;}
        check(!referenced.isUndefined()&&!referenced.isNull(),"native source has scenario-linked unit");
        auto refused=denial(agent,{{"action","delete"},{"ids",QJsonArray{referenced}},{"layer",3}});write(root+"/delete-refused.json",QJsonDocument(refused).toJson());check(refused["error"].toString().contains("Referenced unit"),"incoming script/trigger reference refuses delete without force");
        QJsonValue unreferenced;for(const auto& v:all["units"].toArray())if(v.toObject()["references"].toArray().isEmpty()){unreferenced=v.toObject()["id"];break;}
        if(!unreferenced.isNull()&&!unreferenced.isUndefined()){
            run(agent,"edit_units",{{"action","delete"},{"ids",QJsonArray{unreferenced}},{"layer",3}});run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/basecam.mob")==original,"unreferenced native unit delete + one Undo byte exact");
        }else check(false,"need an unreferenced source unit");
        // Add/move/direction/remove use the same native point serialization.
        const QJsonValue pointUnit=squad[0];QJsonObject add{{"id",pointUnit},{"logic",0},{"kind","patrol"},{"action","add"},{"patrol",0},{"position",QJsonArray{165,175,6}}};
        run(agent,"edit_units",{{"action","points"},{"ids",QJsonArray{pointUnit}},{"layer",3},{"edits",QJsonArray{add}}});
        auto added=getUnit(agent,pointUnit);check(added["logics"].toArray()[0].toObject()["Patrol Points"].toArray()[0].toObject()["Position"]==add["position"],"new patrol point appears in native logic");
        auto move=add;move["action"]="move";move.remove("position");move["delta"]=QJsonArray{1,2,0};
        run(agent,"edit_units",{{"action","points"},{"ids",QJsonArray{pointUnit}},{"layer",3},{"edits",QJsonArray{move}}});
        run(agent,"undo_last_agent_operation");check(getUnit(agent,pointUnit)["logics"]==added["logics"],"patrol point move Undo restores exact logic");
        auto look=add;look["kind"]="look";look["look"]=0;look["position"]=QJsonArray{168,175,6};
        run(agent,"edit_units",{{"action","points"},{"ids",QJsonArray{pointUnit}},{"layer",3},{"edits",QJsonArray{look}}});
        auto direction=look;direction["action"]="direction";direction.remove("position");direction["direction"]=90;direction["distance"]=4;
        run(agent,"edit_units",{{"action","points"},{"ids",QJsonArray{pointUnit}},{"layer",3},{"edits",QJsonArray{direction}}});
        auto facing=getUnit(agent,pointUnit)["logics"].toArray()[0].toObject()["Patrol Points"].toArray()[0].toObject()["Looking points"].toArray()[0].toObject()["Position"].toArray();check((xyz(facing)-QVector3D(165,179,6)).length()<0.0001,"look direction becomes world target at requested azimuth");
        auto remove=look;remove["action"]="remove";remove.remove("position");run(agent,"edit_units",{{"action","points"},{"ids",QJsonArray{pointUnit}},{"layer",3},{"edits",QJsonArray{remove}}});
        for(int i=0;i<4;++i)run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/basecam.mob")==original,"point add/direction/remove Undo restores byte-identical native MOB");
        const auto oversized=denial(agent,{{"action","transform"},{"ids",squad},{"delta",QJsonArray{1,0,0}}});check(oversized["error"].toString().contains("layer"),"missing layer refuses rather than guessing animal dimensions");
        QJsonArray ids501;for(int i=1;i<=501;++i)ids501.append(i);check(denial(agent,{{"action","delete"},{"ids",ids501},{"layer",3}}).contains("error"),"501 units schema limit refuses");
        QJsonArray budgetIds;for(const auto& v:all["units"].toArray()){budgetIds.append(v.toObject()["id"]);if(budgetIds.size()==41)break;}
        auto pointBudget=denial(agent,{{"action","transform"},{"ids",budgetIds},{"delta",QJsonArray{1,0,0}},{"layer",3}});
        check(pointBudget["error"].toString().contains("budget200")&&read(maps+"/basecam.mob")==original,"over200 logic points refuses complete batch without truncation/mutation");
        auto tokenStart=agent.tool("list_units",{{"ids",QJsonArray{squad[0],squad[1],squad[2]}}});
        QJsonObject tokenStatus;check(until([&](){tokenStatus=agent.tool("operation_status",{{"operation",tokenStart["operation"]}});return tokenStatus["status"]=="complete";},30000),"source group snapshot completes");
        const auto token=tokenStatus["result"].toObject();check(!token.contains("error"),"source snapshot available for cross-map copy");
        open("zone1");const auto targetValidation=agent.finish(agent.tool("validate_map"));write(root+"/target-validation-before.json",QJsonDocument(targetValidation).toJson());
        auto copied=run(agent,"edit_units",{{"action","copy"},{"sourceOperation",token["sourceOperation"]},{"delta",QJsonArray{-10,-10,0}},{"rotation",10},{"layer",3}});
        write(root+"/cross-copy.json",QJsonDocument(copied).toJson());QSet<quint32> ids;
        for(const auto& v:copied["affected"].toArray()){auto r=v.toObject();ids.insert(quint32(r["id"].toDouble()));check(r["nameAfter"]!=r["sourceName"],"copy unique name prevents original GetObjectByName binding");}
        check(ids.size()==3&&!copied["warning"].toString().isEmpty()&&!copied["untransferredReferences"].toArray().isEmpty(),"cross-map copy3: unique target-family IDs and explicit untransferred script references");
        run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/zone1.mob")==targetOriginal,"cross-map copy single target Undo restores original MOB bytes");
        copied=run(agent,"edit_units",{{"action","copy"},{"sourceOperation",token["sourceOperation"]},{"delta",QJsonArray{-10,-10,0}},{"rotation",10},{"layer",3}});
        agent.tool("cancel_operation",{{"operation",tokenStart["operation"]}});ids.clear();for(const auto& v:copied["affected"].toArray())ids.insert(quint32(v.toObject()["id"].toDouble()));
        run(agent,"save");strict(read(maps+"/zone1.mob"),"copied.mob");CMobDocument copiedDoc,targetDoc;CValidationReport report;copiedDoc.loadBytes(read(maps+"/zone1.mob"),"copy",&report);targetDoc.loadBytes(targetOriginal,"target",&report);QString copiedScript,targetScript;copiedDoc.embeddedScript(&copiedScript);targetDoc.embeddedScript(&targetScript);check(copiedScript==targetScript,"cross-copy never modifies target SS_TEXT");
        // Save creates the established disk/history boundary, so exercise target
        // Undo before a separate copy/save/reload scenario below.
        open("basecam");open("zone1");
        // The first saved copy is now the clean baseline; test another group
        // copy and byte-exact Undo in the destination document.
        QJsonArray copiedIds;for(auto id:ids)copiedIds.append(double(id));
        run(agent,"edit_units",{{"action","copy"},{"ids",copiedIds},{"delta",QJsonArray{1,1,0}},{"layer",3}});
        run(agent,"undo_last_agent_operation");run(agent,"save");auto reloaded=read(maps+"/zone1.mob");check(reloaded==copiedDoc.serialize(),"target single Undo byte exact after copy");
        auto afterReload=agent.finish(agent.tool("validate_map"));write(root+"/target-validation-after.json",QJsonDocument(afterReload).toJson());
        check(afterReload["errors"]==targetValidation["errors"]&&afterReload["warnings"]==targetValidation["warnings"],"copy/save/reload introduces0 new Warning/Error; units do not invalidate static graph");
        run(agent,"rebuild_ai_graph");run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/zone1.mob")==reloaded,"272 graph rebuild + Undo preserves units");
        run(agent,"edit_objects",{{"action","place"},{"objects",QJsonArray{QJsonObject{{"prototype","stbuho7"},{"x",20},{"y",20}}}}});run(agent,"undo_last_agent_operation");run(agent,"save");check(read(maps+"/zone1.mob")==reloaded,"272 static object placement/Undo regression");
        check(agent.tool("shutdown_headless")["status"]=="shutting_down"&&until([](){return background().isEmpty();},10000),"clean test headless shuts down");agent.tool("disconnect_instance");
        // Production MainWindow framebuffer before/after, no owner desktop.
        qApp->setProperty("workspaceLoadSmoke",true);MainWindow editor;editor.configureAutosaveTimer(false,300000);editor.m_currentCollectionPath=root+"/test.collection";editor.resize(2100,1400);editor.setAttribute(Qt::WA_DontShowOnScreen);editor.show();editor.view()->makeCurrent();
        Client ui(bridge);ui.call("initialize",{{"protocolVersion","2025-11-25"}});check(ui.tool("connect_instance",{{"target","editor"}})["background"]==false,"bridge selects own invisible production MainWindow");
        run(ui,"open_map",{{"map","zone1"}});check(until([&](){return !ui.tool("get_context")["loadingAddMobs"].toBool();},120000),"saved target reloaded through native MainWindow");
        QByteArray repeated;for(auto* m:editor.view()->mobs())if(m->filePath().fileName()=="zone1.mob")check(m->buildValidatedCandidate(&repeated,&report)&&repeated==reloaded,"repeat native serialization after fresh load byte-identical");
        run(ui,"open_map",{{"map","basecam"}});check(until([&](){return !ui.tool("get_context")["loadingAddMobs"].toBool();},120000),"production window MOB family ready");
        auto* mob=editor.view()->currentMob();for(auto* m:editor.view()->mobs())if(m->filePath().fileName()=="basecam.mob")mob=m;
        editor.view()->moveCameraToPoint(QVector3D(170,170,0));QVector3D cp,focus,cr;editor.view()->m_cam->getLocation(cp,focus,cr);editor.view()->m_cam->setLocation(focus+QVector3D(0,0,50),focus,QVector3D(-45,0,0));
        const auto capture=[&](QString path){auto* v=editor.view();v->makeCurrent();v->paintGL();const auto gl=v->grabFrameBuffer();auto shell=editor.grab().toImage();QPainter painter(&shell);painter.drawImage(v->mapTo(&editor,QPoint()),gl);painter.end();check(!gl.isNull()&&shell.save(path),"production widgets + offscreen GL framebuffer");};
        QTest::qWait(200);capture(root+"/mainwindow-before.png");const int depth=editor.m_undoStack->index();
        run(ui,"edit_units",{{"action","transform"},{"ids",squad},{"delta",QJsonArray{3,2,0}},{"rotation",15},{"pivot",QJsonArray{170,170,0}},{"layer",3}});
        check(editor.m_undoStack->index()==depth+1&&mob->isDirty(),"squad edit updates production window with one native Undo");capture(root+"/mainwindow-after.png");
        run(ui,"undo_last_agent_operation");QByteArray undone;check(mob->buildValidatedCandidate(&undone,&report)&&undone==original&&!mob->isDirty(),"window native candidate Undo exact original and clean");
        // Synthetic internal ParentID on a private document only; scripts stay
        // unchanged. This makes remapping observable even if native squad has
        // no ParentID links. No product fixture is repaired.
        auto* child=dynamic_cast<CUnit*>(mob->nodeByMapId(160));const auto oldParent=child->toJson()["World object"].toObject()["Parent Id"].toVariant().toUInt();QSharedPointer<IPropertyBase> prop(new propInt(eObjParam_PARENT_ID,159));child->applyParam(prop);editor.view()->setDurty(mob);
        auto linked=run(ui,"edit_units",{{"action","copy"},{"ids",QJsonArray{159,160}},{"delta",QJsonArray{-5,0,0}},{"layer",3}});QMap<int,int> mapping;for(const auto& v:linked["affected"].toArray()){auto r=v.toObject();mapping[r["sourceId"].toInt()]=r["id"].toInt();}
        check(getUnit(ui,mapping[160])["parentId"].toInt()==mapping[159],"internal group ParentID remapped to target allocated ID");run(ui,"undo_last_agent_operation");prop.reset(new propInt(eObjParam_PARENT_ID,int(oldParent)));child->applyParam(prop);editor.view()->setDurty(mob);
        editor.m_aiAgent->m_mode->setCurrentIndex(1);ui.tool("connect_instance",{{"target","editor"}});auto ro=denial(ui,{{"action","delete"},{"ids",QJsonArray{159}},{"layer",3}});check(ro["error"].toString().contains("Read-only"),"Read-only dock refuses unit writes");check(getUnit(ui,159).contains("logics"),"Read-only unit logic remains readable");
        ui.tool("disconnect_instance");editor.close();visibility.stop();check(visibleWindows==0,"0 visible native windows");
        out<<"RESULT "<<checks<<" PASS,0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\nRESULT "<<checks<<" PASS,1 FAIL\n";out.flush();return 1;}
}

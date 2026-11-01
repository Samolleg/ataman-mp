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
        if(response["id"]!=id)throw QString("MCP response ID mismatch");return response;
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
        const auto maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS")),bridge=qEnvironmentVariable("EI_MCP_TEST_BRIDGE");ownedExe=root+"/app/EIMapperEndurance.exe";
        check(!root.isEmpty()&&!bridge.isEmpty(),"explicit isolated225 installation, settings and profile");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        for(const auto& profile:loadResourceProfiles())if(profile.id=="deja-vu") {
            const auto evaluation=evaluateResourceProfile(root,profile);
            check(evaluation.valid&&!evaluation.runtimeStackValidated,"complete isolated Dejavu editing profile; non-executable runtime marker, no game launch");
        }
        {
            CResArchiveDocument empty,base;QByteArray initial;QStringList findings;
            const auto member=[](QString name,QByteArray bytes,quint32 time){SResArchiveNewMember m;m.name=name;m.payload=bytes;m.lastWriteTime=time;return m;};
            check(empty.loadBytes(read(root+"/res/isolated-empty.res")),"synthetic RES fixture parses");
            check(empty.buildWithAppendedMembers({member("A","old-A",1),member("B","old-B",2)},&initial,&findings)&&base.loadBytes(initial),"synthetic shared RES has two independent members");
            const QFileInfo path(root+"/shared.res");write(path.absoluteFilePath(),initial);
            SResMemberSaveRequest a;a.source=a.destination=path;a.memberName="A";a.expectedMemberSha256=QCryptographicHash::hash("old-A",QCryptographicHash::Sha256).toHex();a.replacementPayload="new-A";a.backupRootOverride=root+"/res-backups";
            auto b=a;b.memberName="B";b.expectedMemberSha256=QCryptographicHash::hash("old-B",QCryptographicHash::Sha256).toHex();b.replacementPayload="new-B";
            check(commitStagedResMemberReplacement(base,a).success&&commitStagedResMemberReplacement(base,b).success,"stale RES member write re-reads and merges another writer's independent member");
            CResArchiveDocument result;check(result.loadBytes(read(path.absoluteFilePath()))&&result.memberPayload("A")=="new-A"&&result.memberPayload("B")=="new-B","shared RES retains both member updates");
            check(!commitStagedResMemberReplacement(base,a).success,"stale same-member overwrite is refused");
            SResPublishedMembersSaveRequest first,second;first.source=first.destination=second.source=second.destination=path;first.published={member("C","published-C",3)};second.published={member("D","published-D",4)};first.backupRootOverride=second.backupRootOverride=root+"/res-backups";
            check(commitStagedResPublishedMembers(result,first).success&&commitStagedResPublishedMembers(result,second).success,"two stale RES publications merge unrelated new members");
            check(result.loadBytes(read(path.absoluteFilePath()))&&result.memberPayload("C")=="published-C"&&result.memberPayload("D")=="published-D","merged RES publication keeps all additions");
        }
        QSettings authority;authority.setValue("ui/aiAgentMode",2);authority.setValue("ui/aiAgentAllowSave",true);authority.sync();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);
        QTimer visibility;QObject::connect(&visibility,&QTimer::timeout,[](){EnumWindows(inspectWindow,0);background();});visibility.start(10);
        CMprDocument source;CValidationReport report;check(source.load(QFileInfo(maps+"/gipat2.mpr"),&report),"private template parses");
        SMprCreationRequest request;request.innerNamespace=source.mapInfo().innerNamespace;request.sectorCountX=request.sectorCountY=8;request.maxZ=64;request.flatRawHeight=qRound(8*65535.0/64);
        QByteArray canvas;check(source.buildNewCanvas(request,&canvas,&report),"bounded8x8 template built through213");write(maps+"/gipat2.mpr",canvas);
        SMobParameterState parameters;QString error;check(CMob::readTemplateParameters(QFileInfo(maps+"/gipat2.mob"),&parameters,&error),"private MOB217 parameters parse");
        CMob mob;mob.applyParameterState(parameters);mob.ensureObjectSection();QByteArray mobBytes;mob.serializeMob(mobBytes,false);write(maps+"/gipat2.mob",mobBytes);
        const auto optionsBefore=read(root+"/app/endurance-options.json");
        Client a(bridge),b(bridge);a.call("initialize",{{"protocolVersion","2025-11-25"}});b.call("initialize",{{"protocolVersion","2025-11-25"}});
        check(background().isEmpty(),"initialize and catalog discovery do not start a mapper");
        a.beginTool("get_context");b.beginTool("get_context");const auto ac=a.endTool(),bc=b.endTool();
        out<<"FIRST_CONTEXT_A "<<QJsonDocument(ac).toJson(QJsonDocument::Compact)<<"\nFIRST_CONTEXT_B "<<QJsonDocument(bc).toJson(QJsonDocument::Compact)<<"\n";out.flush();
        check(ac["background"]==true&&bc["background"]==true&&ac["mpr"].toString().isEmpty(),"two first requests launch and attach to invisible empty mapper without human action");
        const auto first=background();const DWORD firstPid=DWORD(first["pid"].toDouble());
        int backgrounds=0;for(const auto& value:a.tool("list_instances")["instances"].toArray())backgrounds+=value.toObject()["background"].toBool();
        check(backgrounds==1&&firstPid,"two bridges own exactly one background mapper");
        check(ac["profile"]=="deja-vu"&&a.tool("get_revision")["allowSave"]==true,"saved Dejavu profile paths and224 Edit+Save restored before first tool");
        if(qEnvironmentVariableIsSet("EI_MCP_LOCKS_ONLY")) {
            qApp->setProperty("workspaceLoadSmoke",true);MainWindow owner;owner.configureAutosaveTimer(false,300000);
            owner.m_currentCollectionPath=root+"/owner.collection";owner.resize(1000,800);owner.show();owner.view()->makeCurrent();
            Client editor(bridge);editor.call("initialize",{{"protocolVersion","2025-11-25"}});
            check(editor.tool("connect_instance",{{"target","editor"}})["background"]==false,"explicit editor target uses hidden production owner");
            run(editor,"open_map",{{"map","gipat2"}});
            run(editor,"sculpt",{{"action","raise"},{"amount",2},{"area",QJsonObject{{"shape","circle"},{"x",32},{"y",32},{"radius",12}}}});
            const auto ownerTerrain=terrain(editor);const auto ownerDisk=read(maps+"/gipat2.mpr");
            const auto ownerOptions=read(root+"/app/endurance-options.json");
            check(a.tool("connect_instance",{{"target","background"}})["background"]==true,"bridge retains independent background mapper");
            run(a,"create_map",{{"name","parallel271"},{"sectorsX",8},{"sectorsY",8},{"height",8},{"maxZ",64},{"template","0/gipat2.mpr"}});
            run(a,"sculpt",{{"action","raise"},{"amount",1},{"area",QJsonObject{{"shape","circle"},{"x",64},{"y",64},{"radius",12}}}});
            run(a,"save");
            check(terrain(editor)==ownerTerrain&&read(maps+"/gipat2.mpr")==ownerDisk&&CAiEditAdapter::dirty(&owner),"two mapper processes edit different maps; background Save preserves dirty owner A");
            check(read(root+"/app/endurance-options.json")==ownerOptions,"background preserves isolated owner options");
            auto refused=a.tool("open_map",{{"revision",a.tool("get_revision")["revision"]},{"map","gipat2"}});
            if(refused.contains("operation"))refused=a.finish(refused,true);
            check(refused["error"].toString().contains(QString::fromUtf8("Карта открыта владельцем")),"second mapper cannot open same map through bridge");
            check(!owner.replaceLoadedLandscape(QFileInfo(maps+"/parallel271.mpr"))&&terrain(editor)==ownerTerrain,"hidden owner refuses background map before changing its own document");
            run(editor,"save");check(!CAiEditAdapter::dirty(&owner)&&read(maps+"/gipat2.mpr")!=ownerDisk,"owner independently saves A after background saves B");
            check(a.tool("shutdown_headless")["status"]=="shutting_down","clean background closes through bridge");
            check(until([](){return background().isEmpty();},10000),"background shutdown completes");
            a.tool("disconnect_instance");b.tool("disconnect_instance");editor.tool("disconnect_instance");owner.close();
            check(!QFileInfo::exists(maps+"/.mapper-service")&&!QFileInfo::exists(root+"/.mapper-service"),"open/save/close leaves map and resource data free of .mapper-service");
            visibility.stop();check(visibleWindows==0,"no visible native windows in either mapper");
            out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
        }
        authority.setValue("ui/aiAgentAllowSave",false);authority.sync();
        check(a.tool("get_revision")["allowSave"]==false,"owner's Save revocation is effective on the next request without waiting for timer");
        const auto denied=a.tool("create_map",{{"revision",a.tool("get_revision")["revision"]},{"name","denied225"},{"sectorsX",1},{"sectorsY",1}});
        check(denied.contains("error")&&!QFileInfo::exists(maps+"/denied225.mpr"),"revoked persistence refuses creation before any disk write");
        authority.setValue("ui/aiAgentAllowSave",true);authority.sync();
        run(a,"create_map",{{"name","agent225"},{"sectorsX",8},{"sectorsY",8},{"height",8},{"maxZ",64},{"template","0/gipat2.mpr"}});
        run(a,"place_map_in_world",{{"map","gipat2"},{"world","gipat"},{"x",0},{"y",0}});
        run(a,"place_map_in_world",{{"relativeTo","gipat2"},{"side","E"}});
        run(a,"generate_terrain",{{"preset","hills"},{"seed",225}});
        run(a,"auto_paint",{{"preset","hills"},{"seed",225}});
        run(a,"create_mob",{{"template","0/gipat2.mob"},{"copyWorld",true},{"copyRanges",true},{"copyDiplomacy",true}});
        run(a,"save");const auto savedTerrain=terrain(a),savedPaint=a.tool("read_paint");
        CMprDocument saved;CMobDocument savedMob;check(saved.load(QFileInfo(maps+"/agent225.mpr"),&report)&&!report.hasErrors()&&savedMob.load(QFileInfo(maps+"/agent225.mob"),&report)&&!report.hasErrors(),"autonomous221 MPR and MOB are valid on disk");
        run(a,"open_map",{{"map","gipat2"}});run(a,"open_map",{{"map","agent225"}});
        check(terrain(a)==savedTerrain&&a.tool("read_paint")==savedPaint&&a.tool("get_context")["mobs"].toArray().size()==1,"reopen preserves terrain, paint and primary MOB");
        check(read(root+"/app/endurance-options.json")==optionsBefore,"headless never writes owner options");
        check(until([&](){return !alive(firstPid);},15000),"clean idle background exits despite metadata polls");
        check(background().isEmpty(),"idle bridge metadata never relaunches mapper");
        check(a.tool("connect_instance",{{"target","background"}})["background"]==true,"same bridge restarts on explicit demand with224 authority");
        const auto dirtyPid=DWORD(background()["pid"].toDouble());run(a,"open_map",{{"map","agent225"}});
        const auto clean=terrain(a);run(a,"sculpt",{{"action","raise"},{"amount",1},{"area",QJsonObject{{"shape","circle"},{"x",32},{"y",32},{"radius",12}}}});
        check(a.tool("shutdown_headless")["error"].toString().contains("Unsaved"),"explicit shutdown refuses unsaved edits");
        QTest::qWait(9500);check(alive(dirtyPid)&&a.tool("get_context")["dirty"]==true,"idle never discards dirty documents");
        a.tool("disconnect_instance");b.tool("disconnect_instance");QTest::qWait(3000);
        check(alive(dirtyPid),"all bridges disconnected: dirty mapper retained");
        a.tool("connect_instance",{{"target","background"}});run(a,"undo_last_agent_operation");check(terrain(a)==clean,"one Undo restores complete agent operation after reconnect");
        a.tool("disconnect_instance");check(until([&](){return !alive(dirtyPid);},10000),"all bridges disconnected: clean mapper closes automatically");
        authority.setValue("ui/aiAgentMode",0);authority.sync();
        const int launchFiles=QDir(ai::discoveryDirectory()).entryList({"headless-start-*.stdout"},QDir::Files).size();
        a.tool("connect_instance",{{"target","background"}});const auto off=a.tool("get_context");
        check(off["error"].toString().contains(QString::fromUtf8("Владелец выключил доступ агентов в доке"))&&background().isEmpty()
              &&QDir(ai::discoveryDirectory()).entryList({"headless-start-*.stdout"},QDir::Files).size()==launchFiles,"saved Off refuses clearly before spawning any mapper");
        authority.setValue("ui/aiAgentMode",2);authority.sync();
        qApp->setProperty("workspaceLoadSmoke",true);MainWindow owner;owner.configureAutosaveTimer(false,300000);
        check(owner.m_aiAgent->m_events->toPlainText().contains("agent225.mpr")&&owner.m_aiAgent->m_events->toPlainText().contains("save: saved"),"next ordinary dock loads previous background maps and Save history");
        owner.m_currentCollectionPath=root+"/owner.collection";owner.resize(1000,800);owner.show();owner.view()->makeCurrent();
        owner.view()->m_neighborLandscapesVisible=true;
        owner.view()->persistViewportVisualState();
        Client editor(bridge);editor.call("initialize",{{"protocolVersion","2025-11-25"}});
        check(editor.tool("connect_instance",{{"target","editor"}})["background"]==false,"explicit editor target uses normal mapper");
        run(editor,"open_map",{{"map","gipat2"}});run(editor,"sculpt",{{"action","raise"},{"amount",2},{"area",QJsonObject{{"shape","circle"},{"x",80},{"y",80},{"radius",16}}}});
        const auto ownerTerrain=terrain(editor);const auto ownerDisk=read(maps+"/gipat2.mpr");
        const auto ownerMobDisk=read(maps+"/gipat2.mob");
        const auto fullOwnerTerrain=[&](){QByteArray bytes;for(const auto& sector:owner.view()->m_mprDocument.sectors())bytes.append(owner.view()->m_terrainEdit.sectorPayload(sector.sectorX,sector.sectorY));return bytes;};
        const auto ownerSectorPayloads=fullOwnerTerrain();
        QTest::qWait(1500);QSettings().sync();const auto uiBefore=read(authority.fileName()),ownerOptions=read(root+"/app/endurance-options.json");
        check(a.tool("connect_instance")["background"]==true,"auto chooses background while owner's other map is dirty");
        auto bgPid=DWORD(background()["pid"].toDouble());
        run(a,"create_map",{{"name","parallel225"},{"sectorsX",8},{"sectorsY",8},{"height",8},{"maxZ",64},{"template","0/gipat2.mpr"}});
        auto staleWorld=owner.m_worldMapLayoutDocument;
        run(a,"place_map_in_world",{{"relativeTo","gipat2"},{"side","N"}});
        owner.view()->ensureTerrainAutoCatalogReady();
        auto neighborHeight=[&](){for(const auto& n:owner.view()->m_neighborLandscapes)if(n.placement.mprPath.endsWith("/parallel225.mpr")){QVector3D p(64,64,0);n.landscape->projectPt(p,true);return p.z();}return -9999.0f;};
        check(until([&](){owner.view()->makeCurrent();owner.view()->paintGL();return neighborHeight()>-9000;},30000),"ordinary mapper shows agent B as neighbor while editing A");
        const auto beforeHeight=neighborHeight();
        const auto beforeExplicitWorld=terrain(a);
        const auto worldRefused=a.tool("generate_terrain",{{"revision",a.tool("get_revision")["revision"]},{"preset","hills"},{"seed",225},{"includeNeighbors",true}});
        check(worldRefused["error"].toString().contains(QString::fromUtf8("Карта открыта владельцем"))&&terrain(a)==beforeExplicitWorld,"explicit shared generation refuses locked A before changing either map");
        QElapsedTimer responsiveness;responsiveness.start();qint64 previousTick=0,maxGap=0;
        QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,[&](){const auto now=responsiveness.elapsed();maxGap=qMax(maxGap,now-previousTick);previousTick=now;});heartbeat.start(10);
        QTest::qWait(1000);const auto baselineGap=maxGap;maxGap=0;previousTick=responsiveness.elapsed();
        run(a,"generate_terrain",{{"preset","hills"},{"seed",225}});run(a,"create_mob");run(a,"save");
        heartbeat.stop();out<<"OWNER_RESPONSIVENESS baselineMaxMs="<<baselineGap<<" backgroundMaxMs="<<maxGap<<"\n";out.flush();
        check(maxGap<250||maxGap<baselineGap+50,"ordinary mapper event loop stays responsive during background generation and save");
        check(until([&](){owner.view()->makeCurrent();owner.view()->paintGL();return qAbs(neighborHeight()-beforeHeight)>0.05;},30000),"external-save214 refreshes visible neighboring B without reopening owner A");
        check(terrain(editor)==ownerTerrain&&read(maps+"/gipat2.mpr")==ownerDisk&&CAiEditAdapter::dirty(&owner),"owner A's unsaved relief and disk bytes are unchanged by agent B");
        check(fullOwnerTerrain()==ownerSectorPayloads&&read(maps+"/gipat2.mob")==ownerMobDisk,"every in-memory sector payload of A and its disk MOB remain byte identical");
        check(read(root+"/app/endurance-options.json")==ownerOptions&&read(authority.fileName())==uiBefore,"parallel headless preserves owner options and window/dock QSettings byte for byte");
        auto world=staleWorld.loadEditorWorld("deja-vu",{maps});SWorldMapZoneOrigin oldA;
        for(const auto& z:world.zones)if(z.zoneId=="gipat2")oldA=z;
        SWorldMapLayoutChange change;change.before=change.after=oldA;change.after.displayName="Owner A retained";
        check(staleWorld.applyChanges({change},true,&error),"stale owner world document merges nonconflicting changes under interprocess lock: "+error);
        staleWorld.reload();world=staleWorld.loadEditorWorld("deja-vu",{maps});bool hasA=false,hasB=false,hasPrevious=false;
        for(const auto& z:world.zones){hasA|=z.zoneId=="gipat2"&&z.displayName=="Owner A retained";hasB|=z.zoneId=="parallel225";hasPrevious|=z.zoneId=="agent225";}
        check(hasA&&hasB&&hasPrevious,"world merge retains owner entry, B and previous records");
        auto refused=a.tool("open_map",{{"revision",a.tool("get_revision")["revision"]},{"map","gipat2"}});
        if(refused.contains("operation"))refused=a.finish(refused,true);
        check(refused["error"].toString().contains(QString::fromUtf8("Карта открыта владельцем в другом маппере")),"agent cannot open/edit owner A: clear MCP ownership refusal");
        check(!owner.replaceLoadedLandscape(QFileInfo(maps+"/parallel225.mpr"))&&terrain(editor)==ownerTerrain,"human opening agent B is refused nonmodally before touching dirty A");
        run(editor,"save");check(!CAiEditAdapter::dirty(&owner)&&read(maps+"/gipat2.mpr")!=ownerDisk,"owner can independently save A after agent saves B");
        const auto resumed=a.tool("get_context");
        check(resumed["background"]==true,"agent retains background target after owner's independent Save");
        if(!resumed["mpr"].toString().endsWith("/parallel225.mpr"))run(a,"open_map",{{"map","parallel225"}});
        bgPid=DWORD(background()["pid"].toDouble());
        run(a,"sculpt",{{"action","raise"},{"amount",1},{"area",QJsonObject{{"shape","circle"},{"x",64},{"y",64},{"radius",10}}}});
        QString recovery;const auto sourceBHash=QCryptographicHash::hash(read(maps+"/parallel225.mpr"),QCryptographicHash::Sha256).toHex();
        check(until([&](){QDirIterator it(root+"/backups",{"*.mpr"},QDir::Files,QDirIterator::Subdirectories);while(it.hasNext()){const auto p=it.next();if(p.contains("parallel225")&&p.contains("-autosave-")&&QJsonDocument::fromJson(read(p+".json")).object()["sourceSha256"].toString().toLatin1()==sourceBHash&&CBackupManager::validateAutosave(QFileInfo(p))){recovery=p;return true;}}return false;},95000),"dirty background performs standard periodic recovery autosave without dialogs");
        check(CBackupManager::validateAutosave(QFileInfo(recovery),&error),"background recovery autosave validates with its exact atlas: "+error);
        check(owned(bgPid),"crash test targets only isolated owned mapper");
        HANDLE killed=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,bgPid);check(killed!=nullptr,"owned background handle available");
        TerminateProcess(killed,98);WaitForSingleObject(killed,5000);CloseHandle(killed);QTest::qWait(1200);
        check(QFileInfo::exists(CProcessEditLock::lockFilePath(maps+"/parallel225.mpr")),"abandoned map lease remains in service store after simulated crash");
        check(QDir(maps).entryList({"*.mapper-edit.lock"},QDir::Files).isEmpty(),"map directory has no edit-lock sidecars");
        check(a.tool("connect_instance",{{"target","background"}})["background"]==true,"headless singleton lock recovers after dead process");
        run(a,"open_map",{{"map","parallel225"}});check(a.tool("get_context")["mpr"].toString().endsWith("parallel225.mpr"),"dead map owner detected; abandoned lease does not block reopen");
        check(CBackupManager::validateAutosave(QFileInfo(recovery),&error),"prior crash recovery remains intact after restarting background");
        check(a.tool("shutdown_headless")["status"]=="shutting_down","agent explicitly closes clean background");
        check(until([](){return background().isEmpty();},10000),"background shutdown completes and ordinary owner remains available");
        check(!QFileInfo::exists(maps+"/.mapper-service"),"open/save/close never creates .mapper-service in isolated map data");
        check(editor.tool("shutdown_headless").contains("error")&&!editor.tool("get_context").contains("error"),"agent cannot shut down owner mapper");
        int arrivals=0;QObject::connect(&owner.m_aiAgent->m_server,&QLocalServer::newConnection,&owner,[&](){++arrivals;});
        a.tool("connect_instance",{{"target","editor"}});b.tool("connect_instance",{{"target","editor"}});QTest::qWait(1000);const auto beforeArrivals=arrivals;
        QTest::qWait(6500);check(arrivals==beforeArrivals,"223 regression: three idle bridges produce zero new connections");
        const auto history=QString::fromUtf8(read(root+"/app/logs/agent-workflow.log"));
        check(history.contains("parallel225.mpr")&&history.contains(QString::fromUtf8("Операция save: saved")),"durable agent journal names changed maps and completed saves");
        a.tool("disconnect_instance");b.tool("disconnect_instance");editor.tool("disconnect_instance");owner.close();
        visibility.stop();check(visibleWindows==0,"no visible native windows in any background process");
        out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\nRESULT "<<checks<<" PASS, 1 FAIL\n";out.flush();return 1;}
}

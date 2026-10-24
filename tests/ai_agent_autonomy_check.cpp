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
    explicit Client(const QString& exe){process.start(exe,QStringList{});check(process.waitForStarted(10000),"stdio bridge starts independently");}
    ~Client(){process.closeWriteChannel();if(!process.waitForFinished(2000)){process.kill();process.waitForFinished(2000);}}
    QJsonObject raw(QByteArray payload){process.write(payload);if(!until([&](){buffer+=process.readAllStandardOutput();return buffer.contains('\n');},35000))throw QString("stdio response deadline");QJsonObject value;QString error;if(!ai::takeFrame(buffer,&value,&error))throw error;return value;}
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
    QJsonObject finish(QJsonObject start) {
        if(!start.contains("operation"))throw QString::fromUtf8(QJsonDocument(start).toJson());
        QJsonObject status;check(until([&](){status=tool("operation_status",{{"operation",start["operation"]}});return status["status"]=="complete";},140000),"background operation completes");
        const auto result=status["result"].toObject();if(result["status"]!="preview")tool("cancel_operation",{{"operation",start["operation"]}});if(result.contains("error"))throw result["error"].toString();return result;
    }
};
}

int runAssemblyCatalogCheck()
{
    try {
        out.setCodec("UTF-8");
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const auto maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));
        check(!root.isEmpty()&&!maps.isEmpty(),"explicit isolated profile221");
        if(qEnvironmentVariable("EI_MCP_AUTONOMY_VERIFY_SAVED")=="1") {
            CMprDocument active,neighbor;CValidationReport report;CTerrainEditModel a,n;
            check(active.load(QFileInfo(maps+"/gipat-test.mpr"),&report)&&neighbor.load(QFileInfo(maps+"/gipat2.mpr"),&report)
                &&a.load(active,&report)&&n.load(neighbor,&report),"saved cohort reopens in independent process");
            double maximum=0;for(int y=0;y<a.vertexCountY();++y)maximum=qMax(maximum,std::abs(a.height(0,y)*a.maxZ()/65535.0-n.height(n.vertexCountX()-1,y)*n.maxZ()/65535.0));
            check(maximum<0.001,"saved shared edge matches below raw-height quantum");
            write(root+"/saved-seam.json",QJsonDocument(QJsonObject{{"maximumHeightDifferenceMetres",maximum},{"vertices",a.vertexCountY()}}).toJson());
            out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
        }
        // Bounded8x8 neighbour with real profile atlas and original217 settings.
        // Only these private test copies are changed; the source profile is retained.
        CMprDocument source;CValidationReport report;check(source.load(QFileInfo(maps+"/gipat2.mpr"),&report),"copied source template parses");
        SMprCreationRequest request;request.innerNamespace=source.mapInfo().innerNamespace;request.sectorCountX=request.sectorCountY=8;request.maxZ=64;request.flatRawHeight=qRound(8*65535.0/64);
        QByteArray canvas;check(source.buildNewCanvas(request,&canvas,&report),"bounded neighbour fixture built via213");write(maps+"/gipat2.mpr",canvas);
        SMobParameterState settings;QString settingsError;check(CMob::readTemplateParameters(QFileInfo(maps+"/gipat2.mob"),&settings,&settingsError),"source217 settings readable");
        CMob templateMob;templateMob.applyParameterState(settings);templateMob.ensureObjectSection();QByteArray templateBytes;templateMob.serializeMob(templateBytes,false);write(maps+"/gipat2.mob",templateBytes);
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        QVector<QString> roots;for(const auto& p:qEnvironmentVariable("EI_MCP_TEST_RESOURCE_ROOTS").split(';',QString::SkipEmptyParts))roots.append(p);
        dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"resourceRoots"))->setValue(roots);
        CTextureList::getInstance()->attachSettings(window.m_settings.get());CObjectList::getInstance()->attachSettings(window.m_settings.get());
        window.m_currentCollectionPath=root+"/private.collection";window.resize(1100,850);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();check(view.isValid(),"current production mapper221 has hidden desktop GL");
        auto* mode=window.findChild<QComboBox*>("aiAgentMode");auto* allow=window.findChild<QCheckBox*>("aiAgentAllowSave");
        check(mode&&allow&&mode->currentIndex()==0&&!allow->isChecked(),"fresh human authority Off and Save disabled");
        check(!view.m_pLand->isMprLoad(),"scenario starts without any open map");
        check(window.activeResourceProfile().valid,"isolated profile valid with synthetic non-launchable runtime marker");
        mode->setCurrentIndex(1);
        Client client(qEnvironmentVariable("EI_MCP_TEST_BRIDGE"));
        client.call("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","221-e2e"},{"version","1"}}}});
        auto list=client.tool("list_instances")["instances"].toArray();check(list.size()==1,"only isolated mapper is discoverable");
        check(client.tool("connect_instance",{{"instance",list[0].toObject()["instance"]}})["version"]=="221","current isolated221 stdio bridge connects");
        auto revision=[&](){return client.tool("get_revision")["revision"].toString();};
        auto run=[&](QString method,QJsonObject args=QJsonObject{}){args["revision"]=revision();auto start=client.tool(method,args);if(start.contains("operation"))return client.finish(start);if(start.contains("error"))throw start["error"].toString();return start;};
        auto rejected=[&](QString method,QJsonObject args=QJsonObject{}){args["revision"]=revision();auto result=client.tool(method,args);if(result.contains("operation")){
            QJsonObject status;check(until([&](){status=client.tool("operation_status",{{"operation",result["operation"]}});return status["status"]=="complete";},140000),"rejection completes asynchronously");result=status["result"].toObject();}
            check(result.contains("error"),"reject "+method+": "+result["error"].toString());return result;};
        auto idle=[&](){check(until([&](){return window.m_aiAgent->m_editWorkers->load()==0&&!window.m_worldMetadataWatcher;}),"workers settle");};
        auto heights=[&](){QCryptographicHash h(QCryptographicHash::Sha256);for(const auto& s:view.m_mprDocument.sectors())h.addData(view.m_terrainEdit.sectorPayload(s.sectorX,s.sectorY));return h.result().toHex();};
        auto appearance=[&](){QCryptographicHash h(QCryptographicHash::Sha256);QMap<quint16,QByteArray> cache;const auto images=view.m_workingTerrainAtlas->activeTileImages();
            for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})for(int y=0;y<view.m_terrainEdit.tileCountY();++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){bool exists=false;auto packed=view.m_terrainEdit.packedTile(x,y,layer,&exists);if(!exists||packed==0xffff){h.addData("absent");continue;}
                if(!cache.contains(packed)){auto tile=SMprPackedTile::decode(packed);if(tile.atlasSlot()>=images.size())throw QString("Unresolved packed tile after reopen");
                    auto image=images[tile.atlasSlot()].transformed(QTransform().rotate(90*tile.rotation)).convertToFormat(QImage::Format_RGBA8888);QCryptographicHash pixels(QCryptographicHash::Sha256);
                    for(int row=0;row<image.height();++row)pixels.addData(reinterpret_cast<const char*>(image.constScanLine(row)),image.width()*4);cache[packed]=pixels.result();}h.addData(cache[packed]);}check(true,"all land/overlay tiles resolve into current atlas");return h.result().toHex();};
        rejected("open_map",{{"map","gipat2"}});mode->setCurrentIndex(2);
        list=client.tool("list_instances")["instances"].toArray();check(list.size()==1,"mode transition publishes one fresh isolated descriptor");
        check(client.tool("connect_instance",{{"instance",list[0].toObject()["instance"]}})["mode"]=="edit","reconnect after human changes mode");
        rejected("create_map",{{"name","denied"},{"sectorsX",8},{"sectorsY",8}});
        rejected("place_map_in_world",{{"map","gipat2"},{"world","gipat"},{"x",0},{"y",0}});
        allow->setChecked(true);
        auto catalog=client.finish(client.tool("list_profile_maps"));check(catalog["maps"].toArray().size()>=2,"profile templates listed without open map");
        rejected("create_map",{{"name","../escape"},{"sectorsX",8},{"sectorsY",8}});
        rejected("create_map",{{"name","missing"},{"sectorsX",8},{"sectorsY",8},{"template","../escape"}});
        auto cancelled=client.tool("create_map",{{"revision",revision()},{"name","cancelled"},{"sectorsX",8},{"sectorsY",8}});
        check(cancelled.contains("operation"),"create_map returns async ID before commit");client.tool("cancel_operation",{{"operation",cancelled["operation"]}});idle();
        check(!QFileInfo::exists(maps+"/cancelled.mpr")&&!view.m_pLand->isMprLoad(),"cancelled create leaves empty editor and no file");
        auto revoked=client.tool("create_map",{{"revision",revision()},{"name","revoked"},{"sectorsX",8},{"sectorsY",8}});allow->setChecked(false);
        QJsonObject status;check(until([&](){status=client.tool("operation_status",{{"operation",revoked["operation"]}});return status["status"]=="complete";},35000),"revoked creation finishes");
        check(status["result"].toObject().contains("error")&&!QFileInfo::exists(maps+"/revoked.mpr"),"permission rechecked before disk commit");allow->setChecked(true);idle();
        auto created=run("create_map",{{"name","gipat-test"},{"sectorsX",8},{"sectorsY",8},{"height",8},{"maxZ",64}});
        check(created["status"]=="applied"&&view.m_terrainEdit.vertexCountX()==257&&view.m_terrainEdit.vertexCountY()==257,"empty editor creates gipat-test8x8 via default profile template");
        check(run("undo_last_agent_operation")["status"]=="undone"&&!view.m_pLand->isMprLoad(),"one Undo creation returns empty editor");
        window.on_actionRedo_triggered();check(view.m_pLand->isMprLoad(),"creation Redo reopens map");idle();
        rejected("create_map",{{"name","gipat-test"},{"sectorsX",8},{"sectorsY",8}});
        run("place_map_in_world",{{"map","gipat2"},{"world","gipat"},{"x",0},{"y",0}});idle();
        const auto layoutBefore=read(window.m_worldMapLayoutDocument.path());
        const auto placement=run("place_map_in_world",{{"relativeTo","gipat2"},{"side","E"}});idle();
        const QPointF position(placement["origin"].toArray()[0].toDouble(),placement["origin"].toArray()[1].toDouble());
        check(position.x()>0&&position.y()==0&&view.m_hasActiveMapWorldOrigin,"relative placement persists active world origin");
        allow->setChecked(false);rejected("undo_last_agent_operation");allow->setChecked(true);
        run("undo_last_agent_operation");idle();check(read(window.m_worldMapLayoutDocument.path())==layoutBefore,"single placement Undo restores exact prior world JSON");
        window.on_actionRedo_triggered();idle();check(view.m_activeMapWorldOrigin==position,"placement Redo restores world coordinate");
        const auto placed=read(window.m_worldMapLayoutDocument.path());
        rejected("set_world_position",{{"x",0},{"y",0}});check(read(window.m_worldMapLayoutDocument.path())==placed,"intersection rejection preserves layout bytes");
        const auto layoutPermissions=QFile::permissions(window.m_worldMapLayoutDocument.path());
        check(QFile::setPermissions(window.m_worldMapLayoutDocument.path(),QFile::ReadOwner),"isolate world commit failure with readonly test JSON");
        const int undoBeforeFailure=window.m_undoStack->index();rejected("set_world_position",{{"x",1024},{"y",0}});
        check(QFile::setPermissions(window.m_worldMapLayoutDocument.path(),layoutPermissions),"restore owned test JSON permissions");
        check(read(window.m_worldMapLayoutDocument.path())==placed&&window.m_undoStack->index()==undoBeforeFailure,"failed world commit changes neither disk nor Undo");
        rejected("open_map",{{"map","../outside.mpr"}});
        run("generate_terrain",{{"preset","hills"},{"seed",221},{"parameters",QJsonObject{{"thermalIterations",1},{"hydraulicIterations",1}}}});
        const auto unsaved=heights();rejected("open_map",{{"map","gipat2"}});rejected("close_map");
        check(heights()==unsaved&&view.m_pLand->isDirty(),"dirty open/close preserve all unsaved terrain");
        run("sculpt",{{"action","smooth"},{"strength",0.2},{"area",QJsonObject{{"shape","circle"},{"x",128},{"y",128},{"radius",24}}}});
        run("auto_paint",{{"preset","hills"},{"seed",221}});
        allow->setChecked(false);rejected("create_mob");rejected("save");allow->setChecked(true);
        check(QFile::setPermissions(window.m_worldMapLayoutDocument.path(),QFile::ReadOwner),"inject failure after MOB creation before world link commit");
        const int beforeMobRollback=window.m_undoStack->index();
        rejected("create_mob",{{"template","0/gipat2.mob"},{"copyWorld",true}});
        check(QFile::setPermissions(window.m_worldMapLayoutDocument.path(),layoutPermissions),"restore owned world permissions after MOB failure");
        check(!QFileInfo::exists(maps+"/gipat-test.mob")&&view.m_aMob.isEmpty()&&window.m_undoStack->index()==beforeMobRollback,"failed MOB/world cohort removes new MOB, unloads and retains terrain Undo");
        run("create_mob",{{"template","0/gipat2.mob"},{"copyRanges",true},{"copyWorld",true},{"copyDiplomacy",true}});idle();
        check(view.m_aMob.size()==1&&!view.m_aMob[0]->isQuestMob()&&view.m_aMob[0]->nodes().isEmpty(),"primary MOB217 loaded without copied objects");
        const auto mobBytes=read(maps+"/gipat-test.mob");SMobParameterState templateParameters;QString error;check(CMob::readTemplateParameters(QFileInfo(maps+"/gipat2.mob"),&templateParameters,&error),"template metadata independently read");
        QJsonObject actualWorld,sourceWorld;auto actualSet=view.m_aMob[0]->worldSet();actualSet.serializeJson(actualWorld);templateParameters.worldSet.serializeJson(sourceWorld);check(actualWorld==sourceWorld,"requested world settings copied from template");
        run("undo_last_agent_operation");check(view.m_aMob.isEmpty()&&read(maps+"/gipat-test.mob")==mobBytes,"one MOB Undo unloads and retains initial file");
        window.on_actionRedo_triggered();check(view.m_aMob.size()==1,"MOB Redo reloads primary");idle();
        rejected("create_mob");
        view.m_aMob[0]->setDirty(true);rejected("open_map",{{"map","gipat2"}});check(view.m_aMob[0]->isDirty(),"dirty MOB survives rejected navigation");
        run("save");idle();check(!CAiEditAdapter::dirty(&window),"normal Save leaves terrain, world and MOB clean");
        const auto expectedHeights=heights(),expectedAppearance=appearance();
        run("open_map",{{"map","gipat2"}});idle();check(QFileInfo(view.m_pLand->filePath()).completeBaseName()=="gipat2","agent opens another profile map");
        run("open_map",{{"map","0/gipat-test.mpr"}});idle();
        check(heights()==expectedHeights,"saved sectors/heights survive MCP open away and back");
        check(appearance()==expectedAppearance,"saved land/overlay textures survive MCP reopen");
        check(view.m_activeMapWorldOrigin==position&&view.m_aMob.size()==1,"world position and primary MOB survive reopen");
        bool neighbor=false;for(const auto& n:view.m_neighborPlacements)neighbor|=QFileInfo(n.mprPath).completeBaseName()=="gipat2";
        check(neighbor,"gipat2 is restored as real world neighbour");
        write(root+"/expected.json",QJsonDocument(QJsonObject{{"heights",QString::fromLatin1(expectedHeights)},{"appearance",QString::fromLatin1(expectedAppearance)},{"origin",QJsonArray{position.x(),position.y()}},{"context",client.tool("get_context")}}).toJson());
        run("close_map");check(!view.m_pLand->isMprLoad()&&view.m_aMob.isEmpty(),"agent closes clean workspace without dialog");
        run("open_map",{{"map","gipat-test"}});idle();check(heights()==expectedHeights&&appearance()==expectedAppearance,"open from empty editor restores saved map");
        run("create_map",{{"name","explicit221"},{"sectorsX",1},{"sectorsY",1},{"height",3},{"maxZ",64},{"template","0/gipat2.mpr"}});idle();
        check(view.m_terrainEdit.vertexCountX()==33,"explicit arbitrary profile template creates requested small map");
        run("undo_last_agent_operation");idle();check(QFileInfo(view.m_pLand->filePath()).completeBaseName()=="gipat-test"&&view.m_aMob.size()==1&&view.m_activeMapWorldOrigin==position,"creation Undo restores previous map, MOB and neighbours");
        const auto faultLayoutPath=window.m_worldMapLayoutDocument.path(),beforeFault=QString::fromUtf8(read(faultLayoutPath));
        run("set_world_position",{{"x",1024},{"y",0}});idle();
        auto foreignLayout=QJsonDocument::fromJson(read(faultLayoutPath)).object();foreignLayout["foreignWriter221"]=true;
        const auto foreignBytes=QJsonDocument(foreignLayout).toJson();write(faultLayoutPath,foreignBytes);
        rejected("undo_last_agent_operation");check(read(faultLayoutPath)==foreignBytes,"failed Undo reports external layout conflict without overwriting it");
        write(faultLayoutPath,beforeFault.toUtf8());
        run("open_map",{{"map","gipat-test"}});idle();check(view.m_activeMapWorldOrigin==position,"owned fault fixture restored to scenario result");
        mode->setCurrentIndex(0);
        QPointer<CView> guarded=window.view();QTimer::singleShot(0,&window,[&](){window.close();});QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){std::_Exit(124);});watchdog.start(30000);qApp->exec();watchdog.stop();
        check(guarded.isNull(),"production clean close");out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\n";out.flush();return 1;}
}

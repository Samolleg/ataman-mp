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
#include "ai_agent_edit.h"
#include "landscape_tools.h"
#include "terrain_save_flow_policy.h"
#include "terrain_atlas_archive_binding.h"
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
        QJsonObject status;check(until([&](){status=tool("operation_status",{{"operation",start["operation"]}});return status["status"]=="complete";},35000),"background operation completes");
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
        const auto bridge=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_BRIDGE"));
        check(!root.isEmpty()&&!maps.isEmpty(),"explicit isolated profile");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        QVector<QString> resourceRoots;for(const auto& path:qEnvironmentVariable("EI_MCP_TEST_RESOURCE_ROOTS").split(';',QString::SkipEmptyParts))resourceRoots.append(path);
        dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"resourceRoots"))->setValue(resourceRoots);
        CTextureList::getInstance()->attachSettings(window.m_settings.get());CObjectList::getInstance()->attachSettings(window.m_settings.get());
        window.m_currentCollectionPath=root+"/private.collection";window.resize(1150,850);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();check(view.isValid(),"real hidden mapper219 has desktop GL");
        auto* mode=window.findChild<QComboBox*>("aiAgentMode");auto* allow=window.findChild<QCheckBox*>("aiAgentAllowSave");
        check(mode&&allow&&mode->currentIndex()==0&&!allow->isChecked(),"fresh process authority and Save default OFF");
        const auto connect=[&](Client& client){
            client.call("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","219-e2e"},{"version","1"}}}});
            auto list=client.tool("list_instances")["instances"].toArray();check(list.size()==1,"only isolated mapper is discoverable");
            const auto context=client.tool("connect_instance",{{"instance",list[0].toObject()["instance"]}});
            check(context["version"]=="219","real stdio bridge attached to219");
        };
        const auto heights=[&](){QCryptographicHash hash(QCryptographicHash::Sha256);for(int y=0;y<view.m_terrainEdit.vertexCountY();++y)for(int x=0;x<view.m_terrainEdit.vertexCountX();++x){auto p=view.m_terrainEdit.terrainVertexOffset(x,y);hash.addData(QByteArray::number(view.m_terrainEdit.height(x,y))+','+QByteArray::number(p.x())+','+QByteArray::number(p.y())+';');}return hash.result().toHex();};
        QHash<qint64,QByteArray> pixelCache;
        const auto state=[&](){QCryptographicHash hash(QCryptographicHash::Sha256);for(const auto& sec:view.m_mprDocument.sectors())hash.addData(view.m_terrainEdit.sectorPayload(sec.sectorX,sec.sectorY));
            for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}){QVector<qint32> current,base;view.m_terrainEdit.copyVirtualMaterialFieldState(layer,&current,&base);hash.addData(QByteArray(reinterpret_cast<const char*>(current.constData()),current.size()*4));hash.addData(QByteArray(reinterpret_cast<const char*>(base.constData()),base.size()*4));}
            if(view.m_workingTerrainAtlas) {
                const auto images=view.m_workingTerrainAtlas->activeTileImages(),authored=view.m_workingTerrainAtlas->authoredTileImages();Q_UNUSED(authored);
                for(int slot=0;slot<images.size();++slot){const auto& image=images[slot];const auto key=image.cacheKey();
                    if(!pixelCache.contains(key)){QCryptographicHash pixels(QCryptographicHash::Sha256);for(int row=0;row<image.height();++row)pixels.addData(reinterpret_cast<const char*>(image.constScanLine(row)),image.bytesPerLine());pixelCache[key]=pixels.result();}
                    hash.addData(pixelCache[key]);hash.addData(QByteArray::number(view.m_workingTerrainAtlas->runtimeTerrainTypeForSlot(slot)));}
                for(const auto& signature:view.m_workingTerrainAtlas->catalog().gridSignatures())hash.addData(QByteArray::number(signature.northWest)+','+QByteArray::number(signature.northEast)+','+QByteArray::number(signature.southEast)+','+QByteArray::number(signature.southWest)+';');
            }return hash.result().toHex();};
        const auto appearance=[&](){QCryptographicHash hash(QCryptographicHash::Sha256);QMap<quint16,QByteArray> cache;
            const auto images=view.m_workingTerrainAtlas->activeTileImages();
            for(int y=0;y<view.m_terrainEdit.tileCountY();++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){const auto packed=view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Land);
                if(!cache.contains(packed)){auto tile=SMprPackedTile::decode(packed);if(tile.atlasSlot()>=images.size())throw QString("bad packed atlas slot");
                    auto image=images[tile.atlasSlot()].transformed(QTransform().rotate(90*tile.rotation)).convertToFormat(QImage::Format_RGBA8888);QCryptographicHash pixels(QCryptographicHash::Sha256);
                    pixels.addData(QByteArray::number(view.m_workingTerrainAtlas->runtimeTerrainTypeForSlot(tile.atlasSlot())));
                    for(int row=0;row<image.height();++row)pixels.addData(reinterpret_cast<const char*>(image.constScanLine(row)),image.width()*4);cache[packed]=pixels.result();}
                hash.addData(cache[packed]);}return hash.result().toHex();};
        const auto shot=[&](Client& client,QString name){auto snapshot=client.tool("window_snapshot");auto png=QByteArray::fromBase64(snapshot["data"].toString().toLatin1());QImage image;image.loadFromData(png,"PNG");check(!image.isNull(),"real mapper PNG "+name);write(root+"/"+name+".png",png);};
        const auto frame=[&](QString name){for(auto* dock:window.findChildren<QDockWidget*>())dock->hide();window.resize(1100,850);qApp->processEvents();QVector3D center(64,64,10);view.moveCameraToPoint(center);view.m_cam->moveAwayOn(145);view.makeCurrent();view.paintGL();view.grabFrameBuffer(false).save(root+"/"+name+".png");};

        std::unique_ptr<Client> persistent;
        if(qEnvironmentVariable("EI_MCP_EDIT_PHASE")!="edit") {
            check(window.loadLandscapeWithProgress(QFileInfo(maps+"/mcp219.mpr")),"fresh process reopens created and saved MPR");
            view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"fresh process decodes published atlas");
            auto expected=QJsonDocument::fromJson(read(qEnvironmentVariable("EI_MCP_EDIT_EXPECTED"))).object();
            check(heights()==expected["heights"].toString().toLatin1(),"saved height and XY lattice survive fresh process");
            check(appearance()==expected["appearance"].toString().toLatin1(),"every saved cell retains artwork, rotation and runtime material");
            check(view.m_activeTerrainArchivePath.startsWith(QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_PRIVATE_RES"))),"fresh binding belongs to private HDPack");
            const auto path=maps+"/mcp219.mpr";auto neighbor=CLandscape::createReadOnlyBackground();
            view.makeCurrent();check(neighbor->readBackgroundMap(QFileInfo(path)),"saved map opens as real background neighbour");
            check(neighbor->glTexture()!=nullptr,"background neighbour has a GL terrain texture array");
            CView::SLoadedNeighborLandscape loaded;loaded.landscape=neighbor;loaded.placement.zoneId="mcp219-copy";loaded.placement.mprPath=path;loaded.placement.origin={128,0};loaded.placement.worldOrigin={128,0};loaded.placement.size={128,128};
            neighbor->setWorldOffset({128,0,0});view.m_neighborLandscapes.append(loaded);view.m_neighborPlacements={loaded.placement};view.m_neighborLandscapesVisible=true;
            mode->setCurrentIndex(1);persistent.reset(new Client(bridge));auto& client=*persistent;connect(client);frame("reopened-frame");shot(client,"reopened-neighbour");
            if(qEnvironmentVariable("EI_MCP_EDIT_PHASE")=="capabilities") {
                mode->setCurrentIndex(2);connect(client);const auto rev=[&](){return client.tool("get_revision")["revision"].toString();};
                int foreign=0;const auto catalog=view.m_workingTerrainAtlas->catalog();
                for(const auto& family:view.m_terrainGlobalFamilies) {
                    STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=family.id;bool found=false;catalog.selectForPaint(pure,0,0,0,&found);
                    if(found)continue;
                    for(const auto& source:family.sources)if(QFileInfo(maps+"/"+source.mprNamespace+".mpr").isFile()){foreign=family.id;break;}
                    if(foreign)break;
                }
                check(foreign>0,"profile supplies an initial material absent from active atlas");
                auto created=client.finish(client.tool("create_map",{{"revision",rev()},{"name",QString("foreign219_")+QString::fromLatin1(QCryptographicHash::hash(root.toUtf8(),QCryptographicHash::Sha256).toHex().left(6))},{"sectorsX",1},{"sectorsY",1},{"height",3},{"maxZ",100},{"material",foreign}}));
                check(created["status"]=="applied","create_map selects authorized palette source template");
                bool known=false;auto signature=view.m_workingTerrainAtlas->catalog().gridSignatureForPacked(view.m_terrainEdit.packedTile(0,0,ETerrainTileLayer::Land),&known);
                check(known&&signature.northWest==foreign&&signature.northEast==foreign&&signature.southEast==foreign&&signature.southWest==foreign,"foreign initial material is present in real created map");
                window.findChild<QAction*>("actionUndo")->trigger();
                check(window.replaceLoadedLandscape(QFileInfo(maps+"/zone1.mpr")),"private existing overlay fixture opened");
                view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"overlay fixture catalog ready");
                QTest::qWait(250);connect(client); // Reattach after explicit human document load.
                QPoint overlay(-1,-1);for(int y=0;y<view.m_terrainEdit.tileCountY()&&overlay.x()<0;++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){bool available=false;auto packed=view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Overlay,&available);if(available&&packed!=0xffff){overlay={x,y};break;}}
                check(overlay.x()>=0,"fixture has an existing overlay surface");int material=0;
                for(const auto& family:view.m_terrainGlobalFamilies)if(family.layer==ETerrainAtlasLayer::Overlay&&family.name.contains("water",Qt::CaseInsensitive)){material=family.id;break;}
                check(material>0,"profile supplies real overlay water material");const auto before=state();const int index=window.m_undoStack->index();
                auto painted=client.finish(client.tool("paint",{{"revision",rev()},{"layer","overlay"},{"material",material},{"seed",219},{"shuffle",true},{"area",QJsonObject{{"shape","circle"},{"x",overlay.x()*2+1},{"y",overlay.y()*2+1},{"radius",4},{"falloff",0}}}}));
                check(painted["status"]=="applied"&&window.m_undoStack->index()==index+1,"existing overlay painted through same bounded Modern operation");
                window.findChild<QAction*>("actionUndo")->trigger();check(state()==before,"one Undo restores complete overlay and atlas");
            }
            mode->setCurrentIndex(0);
        } else {
            check(window.loadLandscapeWithProgress(QFileInfo(maps+"/zone1.mpr")),"private zone1 template loaded");
            view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"production131 material catalog ready");
            auto archives=CTextureList::getInstance()->configuredResourceArchivePaths();QString firstPack;
            for(const auto& path:archives)if(terrainPublicationIsHDPackArchive(path)){firstPack=path;break;}
            check(firstPack.startsWith(QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_PRIVATE_RES"))),"all publications target isolated HDPack first");
            mode->setCurrentIndex(1);persistent.reset(new Client(bridge));auto& client=*persistent;connect(client);
            auto revision=[&](){return client.tool("get_revision")["revision"].toString();};
            check(client.tool("create_map",{{"revision",revision()},{"name","denied"},{"sectorsX",4},{"sectorsY",4}}).contains("error"),"read-only rejects create_map");
            auto circle=QJsonObject{{"shape","circle"},{"x",64},{"y",64},{"radius",32},{"falloff",0.7}};
            for(const auto& name:{"sculpt","paint","preview","apply","undo_last_agent_operation","save"}) {
                QJsonObject args{{"revision",revision()}};if(QString(name)=="sculpt"){args["action"]="hills";args["amount"]=5;args["area"]=circle;}
                if(QString(name)=="paint"){args["material"]=1;args["area"]=circle;}
                if(QString(name)=="preview")args={{"tool","sculpt"},{"arguments",QJsonObject{{"revision",revision()},{"action","hills"},{"amount",5},{"area",circle}}}};
                if(QString(name)=="apply")args["operation"]="absent";
                check(client.tool(name,args).contains("error"),"read-only rejects "+QString(name));
            }
            mode->setCurrentIndex(2);connect(client);check(!allow->isChecked(),"human Edit enable does not grant Save");
            // Select the template's real uniform meadow, then use the public
            // palette IDs for all other material choices.
            int meadow=0,stone=0,water=0,road=0,forest=0;
            const auto catalog=view.m_workingTerrainAtlas->catalog();
            QJsonArray palette;
            for(const auto& family:view.m_terrainGlobalFamilies){palette.append(QJsonObject{{"id",family.id},{"name",family.name},{"type",family.runtimeTerrainType}});
                const auto name=family.name.toLower();
                STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=family.id;bool uniform=false;catalog.selectForPaint(pure,0,0,0,&uniform);
                if(!meadow&&uniform&&(name.contains("grass")||name.contains(QString::fromUtf8("трава"))))meadow=family.id;
                if(!stone&&(name.contains("stone")||name.contains(QString::fromUtf8("камень"))))stone=family.id;
                if(!water&&(name.contains("water")||name.contains(QString::fromUtf8("вода"))))water=family.id;
                if(!road&&(name.contains("gravel")||name.contains("paving")||name.contains(QString::fromUtf8("гравий"))))road=family.id;
                if(!forest&&name.contains("grass")&&family.id!=meadow)forest=family.id;
            }
            write(root+"/palette.json",QJsonDocument(palette).toJson());
            check(meadow>0&&stone>0&&water>0&&road>0,"real palette supplies meadow/stone/road/water");
            auto created=client.finish(client.tool("create_map",{{"revision",revision()},{"name","mcp219"},{"sectorsX",4},{"sectorsY",4},{"height",10},{"maxZ",100},{"material",meadow}}));
            check(created["status"]=="applied"&&view.m_terrainEdit.vertexCountX()==129&&view.m_terrainEdit.vertexCountY()==129,"create_map opens requested4x4 terrain");
            check(QFileInfo::exists(maps+"/mcp219.mpr")&&view.m_activeTerrainArchivePath.startsWith(QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_PRIVATE_RES"))),"creation publishes initial hash-bound atlas in private HDPack");
            check(client.tool("create_map",{{"revision",revision()},{"name","mcp219"},{"sectorsX",1},{"sectorsY",1}}).contains("error"),"create_map never overwrites");
            check(client.tool("create_map",{{"revision",revision()},{"name","../escape"},{"sectorsX",1},{"sectorsY",1}}).contains("error"),"creation rejects path traversal");
            check(client.tool("save",{{"revision",revision()}}).contains("error"),"Save denied without human checkbox");
            auto* undoAction=window.findChild<QAction*>("actionUndo");auto* redoAction=window.findChild<QAction*>("actionRedo");
            check(undoAction&&redoAction&&undoAction->shortcut().matches(QKeySequence("Ctrl+Z"))==QKeySequence::ExactMatch,"production Ctrl+Z action exists");
            undoAction->trigger();check(view.m_pLand->filePath().completeBaseName()=="zone1","one Ctrl+Z undoes create by restoring prior map; created file retained");
            redoAction->trigger();check(view.m_pLand->filePath().completeBaseName()=="mcp219","Redo reopens created map");view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"new terrain palette ready");
            const auto perform=[&](QString tool,QJsonObject args,bool doUndo=true){
                args["revision"]=revision();const auto before=state();const int index=window.m_undoStack->index();
                auto result=client.finish(client.tool(tool,args));check(result["status"]=="applied",tool+" applies requested operation");
                check(window.m_undoStack->index()==index+1&&window.m_undoStack->undoText().startsWith(QString::fromUtf8("ИИ: ")),"operation is one clearly named Undo");
                if(doUndo){const auto after=state();undoAction->trigger();check(state()==before,"Ctrl+Z restores complete terrain/semantics/atlas");
                    args["revision"]=revision();client.finish(client.tool(tool,args));check(state()==after,"repeat with same seed produces identical terrain/artwork");}
                return result;
            };
            client.finish(client.tool("create_map",{{"revision",revision()},{"name","mcp219_neighbor"},{"sectorsX",4},{"sectorsY",4},{"height",10},{"maxZ",100},{"material",meadow}}));
            undoAction->trigger();check(view.m_pLand->filePath().completeBaseName()=="mcp219","creation Undo returns first synthetic map");
            view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;
            const auto otherPath=maps+"/mcp219_neighbor.mpr",otherId=CWorldTerrainSession::key(otherPath);
            SNeighborMapPlacement placement;placement.zoneId="mcp219_neighbor";placement.mprPath=otherPath;placement.worldOrigin={128,0};placement.origin={128,0};placement.size={128,128};
            view.m_neighborPlacements={placement};view.m_worldTerrainEnabledPaths={otherId};view.m_neighborLandscapesVisible=true;
            CView::SLoadedNeighborLandscape loaded;loaded.placement=placement;view.makeCurrent();loaded.landscape=CLandscape::createReadOnlyBackground();
            check(loaded.landscape->readBackgroundMap(QFileInfo(otherPath)),"real neighbour renderer loads second created map");loaded.landscape->setWorldOffset({128,0,0});view.m_neighborLandscapes.append(loaded);
            auto seamArea=QJsonObject{{"shape","circle"},{"x",128},{"y",64},{"radius",12},{"falloff",0.5}};
            auto cross=perform("sculpt",{{"action","raise"},{"amount",4},{"area",seamArea}});
            check(cross["maps"].toInt()==2,"agent common sculpt edits both permitted maps in one Undo");
            auto neighbor=view.m_worldTerrainSession.documents.value(otherId);check(neighbor&&neighbor->terrain.height(0,64)==view.m_terrainEdit.height(128,64),"canonical shared seam has identical world height");
            const auto edgeBefore=neighbor->terrain.sectorPayload(0,1);undoAction->trigger();check(neighbor->terrain.sectorPayload(0,1)!=edgeBefore,"Ctrl+Z restores neighbour as well as active map");redoAction->trigger();check(neighbor->terrain.sectorPayload(0,1)==edgeBefore,"Redo restores entire cross-map sculpt");
            auto crossPaint=perform("paint",{{"material",road},{"area",seamArea},{"seed",219}});
            check(crossPaint["maps"].toInt()==2,"agent common Modern paint stages both maps");
            neighbor=view.m_worldTerrainSession.documents.value(otherId);
            for(int y=29;y<=35;++y)check(view.m_terrainEdit.virtualMaterialAt(ETerrainTileLayer::Land,64,y)==neighbor->terrain.virtualMaterialAt(ETerrainTileLayer::Land,0,y),"cross-map semantic seam agrees");
            const auto neighborBeforeLocal=neighbor->terrain.sectorPayload(0,1);view.m_worldTerrainEnabled=false;
            perform("sculpt",{{"action","raise"},{"amount",1},{"area",seamArea}});
            check(neighbor->terrain.sectorPayload(0,1)==neighborBeforeLocal,"disabled common brush preserves neighbouring map");view.m_worldTerrainEnabled=true;
            auto rectangle=QJsonObject{{"shape","rectangle"},{"x",0},{"y",0},{"width",128},{"height",128},{"falloff",0}};
            perform("sculpt",{{"action","hills"},{"amount",18},{"seed",219},{"area",rectangle}});
            perform("sculpt",{{"action","flatten"},{"height",11},{"area",QJsonObject{{"shape","circle"},{"x",64},{"y",64},{"radius",37},{"falloff",0.7}}}});
            auto river=QJsonObject{{"shape","line"},{"width",13},{"falloff",0.45},{"points",QJsonArray{QJsonArray{0,58},QJsonArray{35,49},QJsonArray{65,66},QJsonArray{95,57},QJsonArray{128,70}}}};
            perform("sculpt",{{"action","flatten"},{"height",6},{"area",river}});
            // Preview must leave live state unchanged and apply must be stale-safe.
            const auto beforePreview=state();auto preview=client.finish(client.tool("preview",{{"tool","sculpt"},{"arguments",QJsonObject{{"revision",revision()},{"action","smooth"},{"strength",0.4},{"area",circle}}}}));
            check(preview["status"]=="preview"&&state()==beforePreview,"preview prepares without live mutation");
            write(root+"/plan-preview.png",QByteArray::fromBase64(preview["preview"].toObject()["data"].toString().toLatin1()));
            window.m_undoStack->push(new QUndoCommand("Human action"));
            check(client.tool("apply",{{"operation",preview["operation"]},{"revision",preview["revision"]}}).contains("error"),"revision conflict rejects stale preview and preserves human history");
            undoAction->trigger();client.tool("cancel_operation",{{"operation",preview["operation"]}});
            auto smooth=client.finish(client.tool("sculpt",{{"revision",revision()},{"action","smooth"},{"strength",0.3},{"area",circle},{"preview",true}}));
            client.finish(client.tool("apply",{{"operation",smooth["operation"]},{"revision",smooth["revision"]}}));undoAction->trigger();check(state()==beforePreview,"preview/apply is one reversible command");
            perform("sculpt",{{"action","heightmap"},{"area",QJsonObject{{"shape","rectangle"},{"x",4},{"y",4},{"width",14},{"height",14},{"falloff",0.5}}},{"grid",QJsonObject{{"width",2},{"height",2},{"values",QJsonArray{13,16,20,15}}}}});
            perform("paint",{{"material",meadow},{"shuffle",true},{"seed",219},{"area",rectangle}});
            perform("paint",{{"material",stone},{"minimumSlope",27},{"area",rectangle},{"seed",219}});
            if(forest>0)perform("paint",{{"material",forest},{"area",QJsonObject{{"shape","circle"},{"x",96},{"y",102},{"radius",20},{"falloff",0.3}}},{"seed",219}});
            auto bank=river;bank["width"]=19;perform("paint",{{"material",road},{"area",bank},{"seed",219}});
            auto waterArea=river;waterArea["width"]=8;waterArea["falloff"]=0;perform("paint",{{"material",water},{"area",waterArea},{"seed",219}});
            auto path=QJsonObject{{"shape","line"},{"width",7},{"falloff",0},{"points",QJsonArray{QJsonArray{5,15},QJsonArray{35,25},QJsonArray{64,43},QJsonArray{88,34},QJsonArray{125,18}}}};
            auto after=perform("paint",{{"material",road},{"area",path},{"seed",219},{"snapshot",true}});
            check(after["snapshot"].toObject()["mimeType"]=="image/png","apply can return real after snapshot");
            frame("map-brief-frame");shot(client,"map-brief-window");
            const auto withRoad=state();client.tool("undo_last_agent_operation",{{"revision",revision()}});check(state()!=withRoad,"undo_last_agent_operation uses normal history");redoAction->trigger();check(state()==withRoad,"normal Redo restores last agent operation");
            Client second(bridge);connect(second);
            check(second.tool("sculpt",{{"revision",revision()},{"action","raise"},{"amount",1},{"area",circle}}).contains("error"),"second writer refused while first owns session");
            second.tool("disconnect_instance");
            QString invalid;
            check(!ai::validateArguments("sculpt",{{"revision",revision()},{"action","heightmap"},{"area",circle},{"grid",QJsonObject{{"width",65},{"height",2},{"values",QJsonArray{1,2,3,4}}}}},&invalid),"oversized height grid refused before work");
            auto masked=QJsonObject{{"shape","rectangle"},{"x",5},{"y",100},{"width",12},{"height",12},{"falloff",0},{"maskWidth",2},{"maskHeight",2},{"mask",QJsonArray{1,0,0,1}}};
            perform("sculpt",{{"action","raise"},{"amount",2},{"area",masked}});
            const auto beforeHuman=state();auto conflict=client.tool("paint",{{"revision",revision()},{"material",stone},{"area",rectangle},{"shuffle",true},{"seed",1212}});
            check(conflict.contains("operation")&&!window.m_aiAgent->m_jobs[conflict["operation"].toString()].complete,"worker candidate remains pending while human edits");
            STerrainHeightCommand human;STerrainHeightDelta hd;hd.globalX=8;hd.globalY=8;hd.before=view.m_terrainEdit.height(8,8);hd.after=hd.before+1;human.deltas.append(hd);
            check(view.applyTerrainHeightPreview(human,true),"human can edit live terrain while candidate is preparing");window.m_undoStack->push(new QUndoCommand("Human terrain edit"));
            QJsonObject conflictStatus;check(until([&](){conflictStatus=client.tool("operation_status",{{"operation",conflict["operation"]}});return conflictStatus["status"]=="complete";},35000),"conflicted candidate finishes");
            check(conflictStatus["result"].toObject()["error"].toString().contains("revision")&&view.m_terrainEdit.height(8,8)==hd.after,"stale worker refuses and preserves human terrain value");
            client.tool("cancel_operation",{{"operation",conflict["operation"]}});undoAction->trigger();check(view.applyTerrainHeightPreview(human,false)&&state()==beforeHuman,"human edit can still be undone independently");
            const auto beforeCancelled=state();auto pending=client.tool("paint",{{"revision",revision()},{"material",stone},{"area",rectangle},{"shuffle",true},{"seed",1234}});
            check(pending.contains("operation"),"large edit returns asynchronous ID before application");client.tool("disconnect_instance");
            check(until([&](){return window.m_aiAgent->m_editWorkers->load()==0;}),"disconnected worker releases bounded snapshot");
            check(state()==beforeCancelled,"disconnect mid-operation leaves no partial edit or Undo");connect(client);
            pending=client.tool("sculpt",{{"revision",revision()},{"action","noise"},{"amount",3},{"area",rectangle},{"seed",50}});
            client.tool("cancel_operation",{{"operation",pending["operation"]}});check(until([&](){return window.m_aiAgent->m_editWorkers->load()==0;}),"explicit cancel drains worker");check(state()==beforeCancelled,"cancel leaves no partial operation");
            allow->setChecked(true);const auto saved=client.finish(client.tool("save",{{"revision",revision()}}));
            check(saved["status"]=="saved"&&!view.m_pLand->isDirty()&&!view.hasDirtyWorldTerrain(),"human-granted Save completes normal map/atlas publication");
            check(view.m_mprDocument.mapInfo().textureCount<=126,"normal publication respects126-page bound");
            write(root+"/expected.json",QJsonDocument(QJsonObject{{"heights",QString::fromLatin1(heights())},{"appearance",QString::fromLatin1(appearance())},{"namespace",view.m_mprDocument.mapInfo().innerNamespace}}).toJson());
            frame("saved-frame");shot(client,"saved-window");
            auto closeJob=client.tool("paint",{{"revision",revision()},{"material",stone},{"area",rectangle},{"shuffle",true},{"seed",7331}});
            check(closeJob.contains("operation"),"real writer pending immediately before mapper close");
        }
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){if(auto* no=box->button(QMessageBox::No))no->click();else if(auto* discard=box->button(QMessageBox::Discard))discard->click();else box->reject();}});dialogs.start(10);
        QPointer<CView> guarded=window.view();QElapsedTimer timer;timer.start();QTimer::singleShot(0,&window,[&](){window.close();});
        QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){std::_Exit(124);});watchdog.start(30000);qApp->exec();watchdog.stop();
        check(guarded.isNull()&&!window.view()&&timer.elapsed()<30000,"211 clean close deletes live viewport without hang");
        check(!window.m_aiAgent->enabled()&&QDir(ai::discoveryDirectory()).entryList({"*.json"},QDir::Files).isEmpty(),"close retires all automation authority");
        check(read(CLogger::getInstance()->logFilePath()).contains("Shutdown complete status:clean"),"production shutdown journal is clean");
        out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    } catch(const QString& error){out<<"FAIL "<<error<<"\n";out.flush();return 1;}
}

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
        check(!root.isEmpty()&&!maps.isEmpty(),"explicit isolated profile220");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        QTimer unexpectedDialog;QObject::connect(&unexpectedDialog,&QTimer::timeout,[](){if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
            out<<"DIALOG "<<box->windowTitle()<<": "<<box->text()<<"\n";out.flush();
            if(box->windowTitle()==QString::fromUtf8("Большой атлас")&&box->button(QMessageBox::Yes))box->button(QMessageBox::Yes)->click();else box->reject();}});unexpectedDialog.start(50);
        QVector<QString> roots;for(const auto& path:qEnvironmentVariable("EI_MCP_TEST_RESOURCE_ROOTS").split(';',QString::SkipEmptyParts))roots.append(path);
        dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"resourceRoots"))->setValue(roots);
        CTextureList::getInstance()->attachSettings(window.m_settings.get());CObjectList::getInstance()->attachSettings(window.m_settings.get());
        window.m_currentCollectionPath=root+"/private.collection";window.resize(1100,850);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();check(view.isValid(),"current production mapper220 has hidden desktop GL");
        auto* mode=window.findChild<QComboBox*>("aiAgentMode");auto* allow=window.findChild<QCheckBox*>("aiAgentAllowSave");
        check(mode&&allow&&mode->currentIndex()==0&&!allow->isChecked(),"fresh authority Off and Save disabled");
        const auto frame=[&](QString name,bool top){for(auto* dock:window.findChildren<QDockWidget*>())dock->hide();qApp->processEvents();
            const float x=(view.m_terrainEdit.vertexCountX()-1)/2.0f,y=(view.m_terrainEdit.vertexCountY()-1)/2.0f;
            const float distance=qMax(x,y)*2.7f;QVector3D pivot(x,y,10),pos(x,y,distance+10);
            view.m_cam->setLocation(pos,pivot,QVector3D(top?0:-48,0,top?0:15));view.makeCurrent();view.paintGL();
            auto image=view.grabFrameBuffer(false);check(!image.isNull(),"native framebuffer "+name);image.save(root+"/"+name+"-full.png");
            const int size=qMin(image.width(),image.height());image.copy((image.width()-size)/2,(image.height()-size)/2,size,size).save(root+"/"+name+".png");};
        const auto fingerprint=[&](){QCryptographicHash h(QCryptographicHash::Sha256);for(const auto& sec:view.m_mprDocument.sectors())h.addData(view.m_terrainEdit.sectorPayload(sec.sectorX,sec.sectorY));
            for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}){QVector<qint32> a,b;view.m_terrainEdit.copyVirtualMaterialFieldState(layer,&a,&b);h.addData(QByteArray(reinterpret_cast<const char*>(a.constData()),a.size()*4));}
            for(const auto& image:view.m_workingTerrainAtlas->activeTileImages())for(int y=0;y<image.height();++y)h.addData(reinterpret_cast<const char*>(image.constScanLine(y)),image.bytesPerLine());return h.result().toHex();};
        const auto heightPrint=[&](){QCryptographicHash h(QCryptographicHash::Sha256);for(int y=0;y<view.m_terrainEdit.vertexCountY();++y)for(int x=0;x<view.m_terrainEdit.vertexCountX();++x)h.addData(QByteArray::number(view.m_terrainEdit.height(x,y))+',');return h.result().toHex();};
        int geometrySerial=0;
        const auto overlayGeometry=[&](){QCryptographicHash h(QCryptographicHash::Sha256);QMap<QPair<int,int>,QByteArray> ordered;QJsonArray evidence;
            for(const auto& sec:view.m_mprDocument.sectors()){
                const auto b=view.m_terrainEdit.sectorPayload(sec.sectorX,sec.sectorY);QByteArray geometry=b.mid(4,1);if(quint8(b[4])==3)for(int i=0;i<1089;++i)geometry+=b.mid(5+1089*8+i*8,4);
                ordered[{sec.sectorX,sec.sectorY}]=geometry;evidence.append(QJsonObject{{"x",sec.sectorX},{"y",sec.sectorY},{"sha256",QString::fromLatin1(QCryptographicHash::hash(geometry,QCryptographicHash::Sha256).toHex())}});}
            for(const auto& b:ordered)h.addData(b);write(root+QString("/overlay-geometry-%1.json").arg(++geometrySerial),QJsonDocument(evidence).toJson());return h.result().toHex();};
        int appearanceSerial=0;
        const auto appearance=[&](){QCryptographicHash h(QCryptographicHash::Sha256);QMap<quint16,QByteArray> cache;QJsonArray evidence;const auto images=view.m_workingTerrainAtlas->activeTileImages();
            for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})for(int y=0;y<view.m_terrainEdit.tileCountY();++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){bool available=false;const auto packed=view.m_terrainEdit.packedTile(x,y,layer,&available);
                if(!available||packed==0xffff){h.addData("absent");continue;}
                if(!cache.contains(packed)){const auto tile=SMprPackedTile::decode(packed);if(tile.atlasSlot()>=images.size())throw QString("appearance packed slot outside atlas");
                    const auto image=images[tile.atlasSlot()].transformed(QTransform().rotate(90*tile.rotation)).convertToFormat(QImage::Format_RGBA8888);QCryptographicHash pixels(QCryptographicHash::Sha256);
                    pixels.addData(QByteArray::number(view.m_workingTerrainAtlas->runtimeTerrainTypeForSlot(tile.atlasSlot())));for(int row=0;row<image.height();++row)pixels.addData(reinterpret_cast<const char*>(image.constScanLine(row)),image.width()*4);cache[packed]=pixels.result();}
                h.addData(cache[packed]);evidence.append(QJsonObject{{"layer",layer==ETerrainTileLayer::Land?"land":"overlay"},{"x",x},{"y",y},{"packed",packed},{"type",view.m_workingTerrainAtlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(packed).atlasSlot())},{"sha256",QString::fromLatin1(cache[packed].toHex())}});}
            write(root+QString("/appearance-%1.json").arg(++appearanceSerial),QJsonDocument(evidence).toJson());return h.result().toHex();};
        const auto statistics=[&](){natural::Grid grid;grid.width=view.m_terrainEdit.vertexCountX();grid.height=view.m_terrainEdit.vertexCountY();
            for(int y=0;y<grid.height;++y)for(int x=0;x<grid.width;++x)grid.heights.append(view.m_terrainEdit.height(x,y)*view.m_terrainEdit.maxZ()/65535.0);
            auto result=natural::metrics(grid);result["maxZ"]=view.m_terrainEdit.maxZ();
            auto terrain=view.m_terrainEdit;CValidationReport report;check(terrain.initializeVirtualMaterialField(ETerrainTileLayer::Land,view.m_workingTerrainAtlas->catalog(),&report,ETerrainMaterialFieldRecovery::RecoverDeterministically),"bound material metrics");
            QMap<int,int> counts;int total=0;for(int y=1;y<terrain.tileCountY();++y)for(int x=1;x<terrain.tileCountX();++x){++counts[terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y)];++total;}
            QJsonObject fractions,groups;for(auto it=counts.begin();it!=counts.end();++it){QString name="unknown";for(const auto& f:view.m_terrainGlobalFamilies)if(f.id==it.key())name=f.name;
                const double fraction=double(it.value())/qMax(1,total);fractions[name]=fraction;const QString group=name.startsWith("Grass")?"grass":name.startsWith("Sand")?"sand":name.startsWith("Stone")||name.startsWith("Rock")?"rock":"soil";groups[group]=groups[group].toDouble()+fraction;}
            result["materialFractions"]=fractions;result["materialGroups"]=groups;
            const int w=terrain.tileCountX()+1,h=terrain.tileCountY()+1;QVector<bool> seen(w*h,false);QVector<int> patches;int transition=0;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int index=y*w+x,id=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y);
                if(x+1<w&&y+1<h)transition+=id!=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x+1,y)||id!=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y+1)||id!=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x+1,y+1);
                if(seen[index])continue;QVector<int> queue{index};seen[index]=true;
                for(int k=0;k<queue.size();++k){const int a=queue[k];for(int d:{-1,1,-w,w}){const int b=a+d;if(b<0||b>=seen.size()||std::abs(b%w-a%w)+std::abs(b/w-a/w)!=1||seen[b]||terrain.virtualMaterialAt(ETerrainTileLayer::Land,b%w,b/w)!=id)continue;seen[b]=true;queue.append(b);}}patches.append(queue.size()*4);}
            std::sort(patches.begin(),patches.end());result["patchP50"]=patches[patches.size()/2];result["patchP90"]=patches[int((patches.size()-1)*0.9)];result["transitionFraction"]=double(transition)/(terrain.tileCountX()*terrain.tileCountY());return result;};
        QJsonArray references;
        int liquid=-1,coastLiquid=-1,coastWater=0;
        for(const auto& name:{"zone1","zone2","zone6","zone9","zone7"}){
            const auto file=QFileInfo(maps+"/"+name+".mpr");check(view.m_pLand->isMprLoad()?window.replaceLoadedLandscape(file):window.loadLandscapeWithProgress(file),"read-only private reference "+QString(name));view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"reference palette ready");
            if(QString(name)=="zone1")for(int y=0;y<view.m_terrainEdit.tileCountY()&&liquid<0;++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){auto index=view.m_terrainEdit.overlayMaterialIndex(x,y);if(index>=0){liquid=index;break;}}
            if(QString(name)=="zone7"){
                for(const auto& family:view.m_terrainGlobalFamilies)if(family.layer==ETerrainAtlasLayer::Overlay&&family.name=="Water / Emerald")coastWater=family.id;
                QMap<int,int> used;for(int y=0;y<view.m_terrainEdit.tileCountY();++y)for(int x=0;x<view.m_terrainEdit.tileCountX();++x){const int i=view.m_terrainEdit.overlayMaterialIndex(x,y);if(i>=0)++used[i];}
                int maximum=0;for(auto it=used.begin();it!=used.end();++it)if(it.value()>maximum){coastLiquid=it.key();maximum=it.value();}
                check(coastWater>0&&coastLiquid>=0,"exact zone7 emerald water and dominant liquid record");
                QJsonArray records;for(const auto& r:view.m_mprMaterials)records.append(QJsonObject{{"type",r.type},{"opacity",r.opacity},{"waveMultiplier",r.waveMultiplier},{"warpSpeed",r.warpSpeed}});
                write(root+"/reference-water.json",QJsonDocument(QJsonObject{{"material",coastWater},{"liquid",coastLiquid},{"records",records}}).toJson());
            }
            auto metric=statistics();metric["map"]=name;QJsonObject materials;auto terrain=view.m_terrainEdit;CValidationReport report;
            check(terrain.initializeVirtualMaterialField(ETerrainTileLayer::Land,view.m_workingTerrainAtlas->catalog(),&report,ETerrainMaterialFieldRecovery::RecoverDeterministically),"reference semantics from actual bound catalog");
            struct Values {int count=0;double height=0,slope=0;int nearWater=0;};QMap<int,Values> values;
            for(int y=1;y<terrain.tileCountY();++y)for(int x=1;x<terrain.tileCountX();++x){int id=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y);auto& v=values[id];++v.count;
                const auto z=[&](int a,int b){return terrain.height(a,b)*terrain.maxZ()/65535.0;};
                v.height+=z(x*2,y*2);v.slope+=std::atan(std::hypot((z(x*2+1,y*2)-z(x*2-1,y*2))/2,(z(x*2,y*2+1)-z(x*2,y*2-1))/2))*180/3.141592653589793;
                bool wet=false;for(int dy=-3;dy<=3;++dy)for(int dx=-3;dx<=3;++dx)if(x+dx>=0&&y+dy>=0&&x+dx<terrain.tileCountX()&&y+dy<terrain.tileCountY())wet|=terrain.overlayMaterialIndex(x+dx,y+dy)>=0;v.nearWater+=wet;}
            for(auto it=values.begin();it!=values.end();++it){QString name="unknown";for(const auto& f:view.m_terrainGlobalFamilies)if(f.id==it.key())name=f.name;const auto& v=it.value();materials[QString::number(it.key())+":"+name]=QJsonObject{{"count",v.count},{"meanHeight",v.height/v.count},{"meanSlope",v.slope/v.count},{"within6mOfOverlayFraction",double(v.nearWater)/v.count}};}
            const int vw=terrain.tileCountX()+1,vh=terrain.tileCountY()+1;QVector<bool> seen(vw*vh,false);QVector<int> areas;int transitions=0;
            for(int y=0;y<vh;++y)for(int x=0;x<vw;++x){const int index=y*vw+x,id=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y);
                if(x+1<vw&&y+1<vh)transitions+=id!=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x+1,y)||id!=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x,y+1)||id!=terrain.virtualMaterialAt(ETerrainTileLayer::Land,x+1,y+1);
                if(seen[index])continue;QVector<int> queue{index};seen[index]=true;
                for(int k=0;k<queue.size();++k){const int a=queue[k];for(int step:{-1,1,-vw,vw}){const int b=a+step;if(b<0||b>=seen.size()||std::abs(b%vw-a%vw)+std::abs(b/vw-a/vw)!=1||seen[b]||terrain.virtualMaterialAt(ETerrainTileLayer::Land,b%vw,b/vw)!=id)continue;seen[b]=true;queue.append(b);}}
                areas.append(queue.size()*4);}
            std::sort(areas.begin(),areas.end());metric["semanticPatchSquareMetresP50"]=areas[areas.size()/2];metric["semanticPatchSquareMetresP90"]=areas[int((areas.size()-1)*0.9)];metric["semanticTransitionCellFraction"]=double(transitions)/(terrain.tileCountX()*terrain.tileCountY());
            metric["materials"]=materials;references.append(metric);frame(QString("reference-%1-top").arg(name),true);frame(QString("reference-%1-angle").arg(name),false);
        }
        write(root+"/reference-materials.json",QJsonDocument(references).toJson());
        check(window.replaceLoadedLandscape(QFileInfo(maps+"/zone1.mpr")),"clean template for generated maps");view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"template palette ready");
        int grass=0,water=0;
        for(const auto& family:view.m_terrainGlobalFamilies){if(!grass&&family.layer==ETerrainAtlasLayer::Land&&family.name.contains("Grass / Common",Qt::CaseInsensitive))grass=family.id;
            if(!water&&family.layer==ETerrainAtlasLayer::Overlay&&family.name.contains("Water",Qt::CaseInsensitive))water=family.id;}
        check(grass>0&&water>0&&liquid>=0,"real Dejavu meadow/water and liquid record");
        mode->setCurrentIndex(1);Client client(qEnvironmentVariable("EI_MCP_TEST_BRIDGE"));
        const auto connect=[&](){client.call("initialize",{{"protocolVersion","2025-11-25"},{"capabilities",QJsonObject{}},{"clientInfo",QJsonObject{{"name","220-e2e"},{"version","1"}}}});
            auto list=client.tool("list_instances")["instances"].toArray();check(list.size()==1,"only isolated mapper discoverable");auto c=client.tool("connect_instance",{{"instance",list[0].toObject()["instance"]}});check(c["version"]=="220","isolated production stdio bridge connected220");};connect();
        const auto revision=[&](){return client.tool("get_revision")["revision"].toString();};
        for(const auto& tool:{"generate_terrain","erode","carve_river","auto_paint"}){QJsonObject args{{"revision",revision()},{"preset","plain"},{"seed",220}};
            if(QString(tool)=="erode")args.remove("preset");if(QString(tool)=="carve_river"){args.remove("preset");args["points"]=QJsonArray{QJsonArray{0,0},QJsonArray{20,20}};args["width"]=8;args["depth"]=2;}
            check(client.tool(tool,args).contains("error"),"read-only refuses "+QString(tool));}
        mode->setCurrentIndex(2);connect();auto* undo=window.findChild<QAction*>("actionUndo");auto* redo=window.findChild<QAction*>("actionRedo");
        check(undo->shortcut().matches(QKeySequence("Ctrl+Z"))==QKeySequence::ExactMatch,"Ctrl+Z production action");
        const auto perform=[&](QString tool,QJsonObject args,bool repeat){args["revision"]=revision();const auto before=fingerprint();const int index=window.m_undoStack->index();
            QElapsedTimer timer;timer.start();const auto result=client.finish(client.tool(tool,args));check(result["status"]=="applied",tool+" applies");
            check(window.m_undoStack->index()==index+1&&window.m_undoStack->undoText().startsWith(QString::fromUtf8("ИИ: ")),"one agent Undo per "+tool);
            if(repeat){const auto after=fingerprint();undo->trigger();check(fingerprint()==before,"Ctrl+Z restores geometry/semantics/artwork for "+tool);
                args["revision"]=revision();client.finish(client.tool(tool,args));check(fingerprint()==after,"deterministic repeated "+tool);}
            auto r=result;r["wallMs"]=int(timer.elapsed());return r;};
        QJsonArray generatedMetrics;
        for(int scenario=0;scenario<3;++scenario){const QString name=scenario==0?"forest_river220":scenario==1?"mountain_pass220":"coast_bay220";
            const QString preset=scenario==0?"plain":scenario==1?"mountains":"coast";
            if(scenario==2){check(window.replaceLoadedLandscape(QFileInfo(maps+"/zone7.mpr")),"coast uses real zone7 template material/animation records");view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"zone7 template ready");connect();}
            client.finish(client.tool("create_map",{{"revision",revision()},{"name",name},{"sectorsX",8},{"sectorsY",8},{"height",8},{"maxZ",64},{"material",grass}}));
            auto params=natural::preset(preset);
            const auto before=fingerprint();auto pending=client.finish(client.tool("preview",{{"tool","generate_terrain"},{"arguments",QJsonObject{{"revision",revision()},{"preset",preset},{"seed",220+scenario},{"parameters",params}}}}));
            check(pending["status"]=="preview"&&fingerprint()==before,"natural preview leaves live document unchanged");
            client.finish(client.tool("apply",{{"operation",pending["operation"]},{"revision",pending["revision"]}}));undo->trigger();check(fingerprint()==before,"preview/apply is one reversible operation");
            auto gen=perform("generate_terrain",{{"preset",preset},{"seed",220+scenario},{"parameters",params}},true);
            if(scenario<2){QJsonArray line=scenario==0?QJsonArray{QJsonArray{20,0},QJsonArray{40,25},QJsonArray{90,45},QJsonArray{110,80},QJsonArray{80,110},QJsonArray{95,150},QJsonArray{160,170},QJsonArray{200,185},QJsonArray{210,225},QJsonArray{240,256}}:QJsonArray{QJsonArray{0,125},QJsonArray{75,110},QJsonArray{135,130},QJsonArray{205,140},QJsonArray{256,120}};
                perform("carve_river",{{"points",line},{"width",scenario==0?7:6},{"depth",3},{"level",scenario==0?1:10},{"floodplain",scenario==0?24:12},{"seed",220+scenario}},true);}
            auto erosion=perform("erode",{{"parameters",QJsonObject{{"thermalIterations",3},{"hydraulicIterations",6}}}},true);
            undo->trigger(); // Utility erode verified independently; generate already includes calibrated erosion.
            QJsonObject paint{{"preset",preset},{"seed",220+scenario},{"parameters",params},
                {"road",QJsonObject{{"shape","line"},{"points",QJsonArray{QJsonArray{15,40},QJsonArray{65,67},QJsonArray{130,108},QJsonArray{200,133},QJsonArray{250,155}}},{"width",4},{"falloff",0.5}}}};
            if(scenario!=1)paint["water"]=QJsonObject{{"material",scenario==2?coastWater:water},{"liquid",scenario==2?coastLiquid:liquid},{"level",params["waterLevel"]}};
            auto debugArgs=paint;debugArgs["revision"]=revision();write(root+"/"+name+"-resolved.json",QJsonDocument(CAiEditAdapter::capture(&window,"auto_paint",debugArgs).args).toJson());
            auto painting=perform("auto_paint",paint,true);frame(name+"-top",true);frame(name+"-angle",false);
            auto m=statistics();m["name"]=name;m["generate"]=gen;m["erode"]=erosion;m["paint"]=painting;generatedMetrics.append(m);
            // Explicit cancellation before commit, including bounded eroder.
            const auto unchanged=fingerprint();auto start=client.tool("erode",{{"revision",revision()},{"parameters",QJsonObject{{"thermalIterations",64},{"hydraulicIterations",64}}}});
            client.tool("cancel_operation",{{"operation",start["operation"]}});check(until([&](){return window.m_aiAgent->m_editWorkers->load()==0;}),"cancelled eroder releases worker");check(fingerprint()==unchanged,"cancelled eroder leaves complete map unchanged");
            allow->setChecked(true);const auto heights=heightPrint(),overlay=overlayGeometry(),pixels=appearance();check(client.finish(client.tool("save",{{"revision",revision()}}))["status"]=="saved","normal Save pipeline");
            check(window.replaceLoadedLandscape(QFileInfo(maps+"/"+name+".mpr")),"reopen saved natural map");view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"reopened palette ready");check(heightPrint()==heights,"all raw heights survive save/reopen");connect();
            check(overlayGeometry()==overlay,"overlay geometry survives save/reopen");check(appearance()==pixels,"land/overlay pixels and runtime types survive save/reopen");
        }
        write(root+"/generated-metrics.json",QJsonDocument(generatedMetrics).toJson());
        // Generate across two real8x8 maps in one operation: one257x513 grid.
        client.finish(client.tool("create_map",{{"revision",revision()},{"name","seam220"},{"sectorsX",8},{"sectorsY",8},{"height",8},{"maxZ",64},{"material",grass}}));undo->trigger();
        view.m_worldTerrainEnabled=true;view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;
        const auto other=maps+"/seam220.mpr",id=CWorldTerrainSession::key(other);SNeighborMapPlacement n;n.zoneId="seam220";n.mprPath=other;n.worldOrigin={256,0};n.origin={256,0};n.size={256,256};view.m_neighborPlacements={n};view.m_worldTerrainEnabledPaths={id};
        auto seam=perform("generate_terrain",{{"preset","hills"},{"seed",220},{"area",QJsonObject{{"shape","rectangle"},{"x",0},{"y",0},{"width",512},{"height",256},{"falloff",0}}}},true);
        auto neighbor=view.m_worldTerrainSession.documents.value(id);check(bool(neighbor),"native neighbour in world session");double maximum=0;
        for(int y=0;y<=256;++y)maximum=qMax(maximum,std::abs(view.m_terrainEdit.height(256,y)*view.m_terrainEdit.maxZ()/65535.0-neighbor->terrain.height(0,y)*neighbor->terrain.maxZ()/65535.0));
        check(maximum<0.001,"entire shared edge matches within raw-height quantum");write(root+"/seam.json",QJsonDocument(QJsonObject{{"maxWorldHeightDifference",maximum},{"result",seam}}).toJson());
        undo->trigger();check(!view.m_pLand->isDirty()&&!view.hasDirtyWorldTerrain(),"common generation Undo restores both clean maps");
        mode->setCurrentIndex(0);
        QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){if(auto* no=box->button(QMessageBox::No))no->click();else box->reject();}});dialogs.start(10);
        QPointer<CView> guarded=window.view();QTimer::singleShot(0,&window,[&](){window.close();});QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){std::_Exit(124);});watchdog.start(30000);qApp->exec();watchdog.stop();
        check(guarded.isNull(),"clean production close");out<<"RESULT "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\n";out.flush();return 1;}
}

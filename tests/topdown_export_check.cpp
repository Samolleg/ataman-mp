#include <QtWidgets>
#include <QtTest>
#include <QtNetwork>
#include "main_window.h"
#include "view.h"
#include "camera.h"
#include "settings.h"
#include "landscape.h"
#include "sector.h"
#include "math_utils.h"
#include "mob.h"
#include "worldobj.h"
#include "ai_agent_edit.h"
#include "topdown_export.h"
#include "resourcemanager.h"
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <functional>
#include <windows.h>
namespace {
QTextStream out(stdout);int checks=0;
void check(bool b,QString text){out<<(b?"PASS ":"FAIL ")<<text<<"\n";out.flush();if(!b)throw text;++checks;}
bool until(const std::function<bool()>& done,int ms=10000){QElapsedTimer t;t.start();do{if(done())return true;QTest::qWait(5);}while(t.elapsed()<ms);return false;}
QByteArray read(QString path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(QString path,QByteArray bytes){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw QString("write failed: ")+path;}
QJsonObject json(QString path){return QJsonDocument::fromJson(read(path)).object();}
struct Hidden:QObject {bool eventFilter(QObject* o,QEvent* e)override {
    if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
    if(e->type()==QEvent::Show)if(auto* dialog=qobject_cast<QMessageBox*>(o)){out<<"DIALOG "<<dialog->text()<<"\n";QTimer::singleShot(0,dialog,&QDialog::reject);}
    return false;
}};
class Client {
    QProcess p;QByteArray buffer;int serial=0;
public:
    QJsonArray lastContent;
    explicit Client(QString exe){p.start(exe,QStringList{});check(p.waitForStarted(10000),"real stdio bridge starts");call("initialize",{{"protocolVersion","2025-11-25"}});}
    ~Client(){p.closeWriteChannel();if(!p.waitForFinished(2000)){p.kill();p.waitForFinished();}}
    QJsonObject call(QString method,QJsonObject params={}) {
        const int id=++serial;p.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        while(true){
            if(!until([&](){buffer+=p.readAllStandardOutput();return buffer.contains('\n');},40000))throw QString("MCP response timeout: ")+method+" "+QString::fromUtf8(p.readAllStandardError());
            const int end=buffer.indexOf('\n');auto r=QJsonDocument::fromJson(buffer.left(end)).object();buffer.remove(0,end+1);
            if(!r.contains("id"))continue;if(r["id"].toInt()!=id)throw QString("response ID mismatch");
            if(r.contains("error"))throw QString::fromUtf8(QJsonDocument(r).toJson());return r["result"].toObject();
        }
    }
    QJsonObject tool(QString name,QJsonObject args={}) {
        auto r=call("tools/call",{{"name",name},{"arguments",args}});lastContent=r["content"].toArray();
        return QJsonDocument::fromJson(lastContent[0].toObject()["text"].toString().toUtf8()).object();
    }
    QJsonObject finish(QJsonObject start,bool exportJob=false) {
        if(!start.contains("operation"))throw QString::fromUtf8(QJsonDocument(start).toJson());
        QJsonObject status;check(until([&](){status=tool("operation_status",{{"operation",start["operation"]}});
            if(status.contains("error")){if(status["error"].toString().startsWith("Normal map commit"))return false;throw status["error"].toString();}
            return status["status"]!="running";},140000),"operation reaches terminal status");
        if(!exportJob)tool("cancel_operation",{{"operation",start["operation"]}});
        return exportJob?status:status["result"].toObject();
    }
    void open(QString map) {
        QJsonObject result;
        check(until([&](){auto start=tool("open_map",{{"map",map},{"revision",tool("get_revision")["revision"]}});
            if(!start.contains("operation")){if(start["error"].toString().contains("progress")||start["error"].toString().contains("busy"))return false;throw QString::fromUtf8(QJsonDocument(start).toJson());}
            result=finish(start);return true;},60000),"open_map via MCP "+map);
        if(result.contains("error"))throw result["error"].toString();
        QTest::qWait(100);
    }
    QJsonObject startExport(QString path,int ppu=4) {
        QElapsedTimer t;t.start();const auto result=tool("export_map_topdown",{{"revision",tool("get_revision")["revision"]},{"outputDir",path},{"pixelsPerWorldUnit",ppu}});
        check(result["status"]=="running"&&result["deadlineMs"]==120000&&t.elapsed()<2000,"export acknowledges ID before rendering, explicit120s deadline");return result;
    }
    QJsonObject exportMap(QString path,int ppu=4) {
        const auto status=finish(startExport(path,ppu),true);
        if(status["status"]!="complete")throw QString::fromUtf8(QJsonDocument(status).toJson());
        check(lastContent.size()==2&&lastContent[1].toObject()["type"]=="image","MCP terminal result carries standard small image content");
        return status["result"].toObject();
    }
};
void verify(QString dir,QSize size) {
    const auto manifest=json(dir+"/manifest.json");check(manifest["status"]=="complete"&&manifest["width"]==size.width()&&manifest["height"]==size.height(),"manifest dimensions/status "+dir);
    for(const auto& value:manifest["files"].toArray()) {
        const auto f=value.toObject();const auto bytes=read(dir+"/"+f["name"].toString());QImage image;image.loadFromData(bytes);
        check(QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==f["sha256"].toString()&&bytes.size()==f["bytes"].toInt(),"file hash/size "+f["name"].toString());
        check(f["name"]=="preview.png"?qMax(image.width(),image.height())<=1024:image.size()==size,"aligned image size "+f["name"].toString());
    }
    const auto c=manifest["counts"].toObject();check(c["objects"].toInt()==manifest["objects"].toArray().size()&&c["objects"].toInt()==c["structures"].toInt()+c["vegetation"].toInt()+c["excluded"].toInt(),"all scope objects accounted separately from mesh parts");
    check(manifest["roadsMask"].toObject()["status"]=="unavailable","road semantics explicitly unavailable, textured rendering retained");
    for(auto name:{"water-mask.png","structures-mask.png"}){QImage image(dir+"/"+name);int mid=0,white=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){int v=qGray(image.pixel(x,y));mid+=v!=0&&v!=255;white+=v==255;}check(mid==0&&white>0,QString("geometric binary coverage with real pixels: ")+name);}
}
}

int runAssemblyCatalogCheck() {
    try {
        out.setCodec("UTF-8");const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const QString exports=root+"/exports",maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS")),bridge=root+"/app/EIMapperMcp.exe";
        check(!root.isEmpty()&&QDir(exports).exists(),"explicit isolated fixture/export roots");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QSettings authority;authority.setValue("ui/aiAgentMode",2);authority.setValue("ui/aiAgentAllowSave",false);authority.sync();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);Hidden hidden;qApp->installEventFilter(&hidden);
        check(ai::validateTopdownOutput(exports+"/new",ai::topdownWorkspaceRoot(),{})["path"].isString(),"canonical workspace output allowed");
        check(ai::validateTopdownOutput(maps+"/no",ai::topdownWorkspaceRoot(),{maps})["code"]=="OUTPUT_PATH_ERROR","game/profile destination refused");
        check(ai::validateTopdownOutput(exports,ai::topdownWorkspaceRoot(),{})["code"]=="OUTPUT_EXISTS","existing output refused");
        check(ai::validateTopdownOutput(root+"/linked-export/new",ai::topdownWorkspaceRoot(),{})["code"]=="OUTPUT_PATH_ERROR","junction ancestor cannot redirect export into profile sources");
        Client background(bridge);
        check(background.tool("connect_instance",{{"target","background"}})["background"]==true,"production background mapper starts invisibly through MCP");
        const auto tools=background.call("tools/list")["tools"].toArray();bool contract=false;
        write(root+"/mcp-catalog.json",QJsonDocument(tools).toJson());
        for(const auto& t:tools)if(t.toObject()["name"]=="export_map_topdown")contract=t.toObject()["annotations"].toObject()["readOnlyHint"]==false&&t.toObject()["annotations"].toObject()["destructiveHint"]==false;
        check(contract,"dynamic MCP catalog advertises non-destructive file-producing export");
        auto status=background.finish(background.startExport(exports+"/nomap"),true);
        check(status["status"]=="failed"&&status["result"].toObject()["code"]=="NO_MAP","NO_MAP returns a terminal error without breaking connection");
        background.open("zone1");
        const auto before=background.tool("get_context"),rev=background.tool("get_revision");
        auto result=background.exportMap(exports+"/zone1");verify(exports+"/zone1",QSize(1408,1152));
        check(background.tool("get_revision")==rev&&background.tool("get_context")==before,"production export retains revision/dirty/context");
        check(QFileInfo(result["manifestPath"].toString()).exists(),"all final files reachable without GUI");
        const auto stale=background.tool("export_map_topdown",{{"revision","stale"},{"outputDir",exports+"/stale"}});
        status=background.finish(stale,true);check(status["result"].toObject()["code"]=="REVISION_CONFLICT","stale revision refuses complete export");
        auto cancel=background.startExport(exports+"/cancelled");check(background.tool("cancel_operation",{{"operation",cancel["operation"]}})["status"]=="cancelled"&&!QDir(exports+"/cancelled").exists(),"cancel resource wait never publishes a partial export");
        background.open("zone2");
        CMprDocument second;CValidationReport report;check(second.load(QFileInfo(maps+"/zone2.mpr"),&report),"second private MPR reads with production document loader");
        const auto info=second.mapInfo();check(info.sectorCountX!=info.sectorCountY&&double(info.sectorCountX)/info.sectorCountY!=11.0/9,"second rectangle has distinct aspect ratio");
        background.exportMap(exports+"/zone2");verify(exports+"/zone2",QSize(info.sectorCountX*128,info.sectorCountY*128));
        check(background.tool("shutdown_headless")["status"]=="shutting_down","clean background shuts down normally");QTest::qWait(300);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);window.resize(1100,800);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();check(view.isValid(),"owner-window renderer runs with no visible windows");
        Client editor(bridge);check(editor.tool("connect_instance",{{"target","editor"}})["background"]==false,"explicit editor instance selected");editor.open("zone1");
        check(until([&](){return !CAiEditAdapter::busy(&window);},30000),"scene/resource initialization settles");
        // Owner-window and background geography must agree despite user camera.
        auto* node=static_cast<CNode*>(nullptr);for(auto* mob:view.m_aMob)for(auto* n:mob->nodes())if(n->modelName()=="stbuho7"){node=n;break;}
        check(node!=nullptr,"real house chosen from current scene");
        const auto projection=view.m_projection;const auto camera=view.m_cam->viewMatrix();
        node->setState(eHidden);view.m_staticObjectsVisible=false;view.m_landscapeVisible=false;view.m_bDrawWater=false;
        const auto savedState=[&](){return QJsonDocument(QJsonObject{{"revision",CAiEditAdapter::revision(&window)},{"node",node->toJson()},{"state",int(node->nodeState())},{"activeMob",view.m_activeMob->filePath().absoluteFilePath()},{"static",view.m_staticObjectsVisible},{"land",view.m_landscapeVisible},{"water",view.m_bDrawWater}}).toJson();};
        window.findChild<QComboBox*>("aiAgentMode")->setCurrentIndex(1);QTest::qWait(30);editor.tool("connect_instance",{{"target","editor"}});
        const auto saved=savedState();editor.exportMap(exports+"/editor-hidden");
        check(savedState()==saved&&view.m_projection==projection&&view.m_cam->viewMatrix()==camera,"hidden/layer/camera/projection/MOB/revision preserved in read-only mode");
        check(read(exports+"/editor-hidden/structures-mask.png")==read(exports+"/zone1/structures-mask.png"),"editor-hidden house and background share exact geographic geometry/mask");
        {
            // Independent world-space triangle queries check actual liquid
            // depth, including dry land over an authored overlay. No tile-color
            // or common-water-level inference participates in this oracle.
            QImage water(exports+"/zone1/water-mask.png"),structures(exports+"/zone1/structures-mask.png");
            view.makeCurrent();QHash<CSector*,QVector<ushort>> landIndices,waterIndices;
            for(const auto& row:view.m_pLand->m_aSector)for(auto* sector:row) {
                QVector<ushort> land(256*24),overlay(sector->m_waterIndexCount);
                sector->m_indexBuf.bind();check(sector->m_indexBuf.read(0,land.data(),land.size()*int(sizeof(ushort))),"read actual terrain GPU triangle topology");sector->m_indexBuf.release();
                if(!overlay.isEmpty()){sector->m_waterIndexBuf.bind();check(sector->m_waterIndexBuf.read(0,overlay.data(),overlay.size()*int(sizeof(ushort))),"read actual overlay GPU triangle topology");sector->m_waterIndexBuf.release();}
                landIndices[sector]=land;waterIndices[sector]=overlay;
            }
            const auto renderedHeight=[&](int tx,int ty,float x,float y,bool overlay,float* height)->bool {
                auto* sector=view.m_pLand->m_aSector[ty/16][tx/16];const int tile=(ty%16)*16+tx%16;
                const auto& indices=overlay?waterIndices[sector]:landIndices[sector];const auto& vertices=overlay?sector->m_arrWaterVrtData:sector->m_arrLandVrtData;
                bool hit=false;*height=-1e9f;
                for(int i=0;i+2<indices.size();i+=3){if(indices[i]/9!=tile)continue;
                    auto a=sector->m_modelMatrix.map(vertices[indices[i]].position),b=sector->m_modelMatrix.map(vertices[indices[i+1]].position),c=sector->m_modelMatrix.map(vertices[indices[i+2]].position);
                    QVector3D origin(x,y,0),direction(0,0,1);float t,u,v;
                    if(util::ptToTriangle(t,u,v,origin,direction,a,b,c)){hit=true;*height=qMax(*height,t);}
                }
                return hit;
            };
            int wet=0,dry=0,mismatches=0,bridgePixels=0;QJsonArray shoreAnchors,geometryAnchors,depthErrors;
            for(int ty=0;ty<view.m_terrainEdit.tileCountY();++ty)for(int tx=0;tx<view.m_terrainEdit.tileCountX();++tx) {
                const int material=view.m_terrainEdit.overlayMaterialIndex(tx,ty);
                if(material<0||material>=view.m_pLand->materials().size())continue;
                const int type=view.m_pLand->materials()[material].type;if(type!=2&&type!=3)continue;
                const float x=tx*2+1.125f,y=ty*2+1.125f;QVector3D land(x,y,0),overlay(x,y,0);
                float landHeight,waterHeight;if(!renderedHeight(tx,ty,x,y,false,&landHeight)||!renderedHeight(tx,ty,x,y,true,&waterHeight))continue;
                land.setZ(landHeight);overlay.setZ(waterHeight);
                const float delta=overlay.z()-land.z();if(qAbs(delta)<0.025f)continue;
                const int px=int(x*4),py=int((288-y)*4);const bool expected=delta>0;
                const bool actual=qGray(water.pixel(px,py))>127;mismatches+=actual!=expected;
                if(actual!=expected&&depthErrors.size()<12)depthErrors.append(QJsonObject{{"x",x},{"y",y},{"material",material},{"type",type},{"land",land.z()},{"water",overlay.z()},{"actual",actual}});
                if(expected)++wet;else ++dry;
                bool boundary=false;if(px>1&&py>1&&px<water.width()-2&&py<water.height()-2)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)boundary|=(qGray(water.pixel(px+dx,py+dy))>127)!=actual;
                if(boundary && (shoreAnchors.isEmpty()||QVector2D(x-shoreAnchors[0].toObject()["x"].toDouble(),y-shoreAnchors[0].toObject()["y"].toDouble()).length()>70))
                    shoreAnchors.append(QJsonObject{{"x",x},{"y",y},{"pixel",QJsonArray{px,py}},{"terrainZ",land.z()},{"waterZ",overlay.z()},{"mask",actual}});
            }
            write(root+"/depth-diagnostic.json",QJsonDocument(QJsonObject{{"wet",wet},{"dry",dry},{"mismatches",mismatches},{"errors",depthErrors}}).toJson());
            check(wet>100&&dry>100&&mismatches==0,"native overlay/terrain triangle depths agree with mask, including land above water");
            check(shoreAnchors.size()>=2,"two separated bank/slope control points recorded in canonical pixel transform");
            const auto manifest=json(exports+"/zone1/manifest.json");QSet<quint32> included;
            for(const auto& value:manifest["objects"].toArray()){const auto e=value.toObject();if(e["included"].toBool()&&e["category"]=="structures")included.insert(quint32(e["id"].toDouble()));}
            struct Anchor {CNode* node;QVector3D world;};QVector<Anchor> candidates;
            for(auto* mob:view.m_aMob)for(auto* n:mob->nodes())if(included.contains(n->mapId()))if(auto* object=dynamic_cast<CObjectBase*>(n)) {
                float highest=-1e9f;QVector3D point;
                for(const auto& item:object->staticRenderItems()){const auto& vertices=item.part->vertData();for(int i=0;i+2<vertices.size();i+=3) {
                    const auto a=item.instanceMatrix.map(vertices[i].position),b=item.instanceMatrix.map(vertices[i+1].position),c=item.instanceMatrix.map(vertices[i+2].position);
                    const float area=qAbs((b.x()-a.x())*(c.y()-a.y())-(b.y()-a.y())*(c.x()-a.x()));
                    const auto center=(a+b+c)/3;if(area>0.2f&&center.z()>highest){highest=center.z();point=center;}
                }}
                const int px=int(point.x()*4),py=int((288-point.y())*4);
                if(px<1||py<1||px>=structures.width()-1||py>=structures.height()-1)continue;
                bool hit=false;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)hit|=qGray(structures.pixel(px+dx,py+dy))>127;
                const QString model=n->modelName();const bool landmark=model.startsWith("stbr")||model.startsWith("stbuho")||model.startsWith("stho")||model.startsWith("samvl")||model.startsWith("gpjun")||model=="stdoor";
                if(hit&&highest>-1e8&&landmark)candidates.append({n,point});
                if(n->modelName().startsWith("stbr")){
                    QVector<SRenderItem> items=object->staticRenderItems();QString error;
                    const auto bridge=view.captureTopdownPass(ETopdownPass::StructuresMask,water.size(),items,&error);
                    for(int y=0;y<water.height();++y)for(int x=0;x<water.width();++x)if(qGray(bridge.pixel(x,y))>127&&qGray(water.pixel(x,y))>127)++bridgePixels;
                }
            }
            QVector<QVector3D> chosen;
            for(int count=0;count<4;++count){int best=-1;float distance=-1;for(int i=0;i<candidates.size();++i){if(count==0&&!candidates[i].node->modelName().startsWith("stbr"))continue;float d=1e9f;for(const auto& selected:chosen)d=qMin(d,(selected-candidates[i].world).lengthSquared());if(d>distance){distance=d;best=i;}}check(best>=0,"actual assembled building/bridge triangle anchor available");const auto a=candidates.takeAt(best);chosen.append(a.world);geometryAnchors.append(QJsonObject{{"id",double(a.node->mapId())},{"model",a.node->modelName()},{"world",QJsonArray{a.world.x(),a.world.y(),a.world.z()}},{"pixel",QJsonArray{a.world.x()*4,(288-a.world.y())*4}},{"tolerancePixels",1},{"source","centroid of highest nondegenerate authored mesh triangle; transform applied"}});}
            check(bridgePixels>40,"actual bridge geometry covers uninterrupted geographic water mask below it");
            write(root+"/geographic-controls.json",QJsonDocument(QJsonObject{{"wetSamples",wet},{"dryOverOverlay",dry},{"mismatches",mismatches},{"bridgeWaterPixels",bridgePixels},{"banks",shoreAnchors},{"structures",geometryAnchors}}).toJson());
            const auto oldMinimap=view.captureMinimapScene(true,256);check(oldMinimap.bake.success,"existing minimap capture/bake remains operational");
            view.makeCurrent();auto* gl=QOpenGLContext::currentContext()->functions();QOpenGLFramebufferObject sentinel(QSize(48,48),QOpenGLFramebufferObject::CombinedDepthStencil);
            sentinel.bind();gl->glViewport(3,5,37,39);gl->glDepthMask(GL_FALSE);gl->glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);gl->glDisable(GL_BLEND);
            QString captureError;const auto captured=view.captureTopdownPass(ETopdownPass::Terrain,QSize(352,288),{},&captureError);
            GLint fb=0,vp[4];GLboolean dm,cm[4];gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);gl->glGetIntegerv(GL_VIEWPORT,vp);gl->glGetBooleanv(GL_DEPTH_WRITEMASK,&dm);gl->glGetBooleanv(GL_COLOR_WRITEMASK,cm);
            check(!captured.isNull()&&GLuint(fb)==sentinel.handle()&&vp[0]==3&&vp[1]==5&&vp[2]==37&&vp[3]==39&&!dm&&cm[0]&&!cm[1]&&cm[2]&&!cm[3]&&!gl->glIsEnabled(GL_BLEND),"owning FBO/viewport/color/depth/blend restored exactly after capture");
            const auto oversized=view.captureTopdownPass(ETopdownPass::Terrain,QSize(1000000,4),{},&captureError);gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fb);
            check(oversized.isNull()&&captureError.startsWith("EXPORT_SIZE_LIMIT")&&GLuint(fb)==sentinel.handle(),"GL size refusal occurs before capture and restores owning target");
            out<<"Framebuffer image format="<<int(sentinel.toImage().format())<<" (ARGB32 premultiplied="<<int(QImage::Format_ARGB32_Premultiplied)<<")\n";
            QOpenGLFramebufferObject::bindDefault();gl->glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);gl->glDepthMask(GL_TRUE);gl->glEnable(GL_BLEND);gl->glViewport(0,0,view.width(),view.height());
        }
        auto* originalMob=view.m_activeMob;
        // One additional explicit MOB, plus a deleted object and a rotated
        // high roof whose origin lies outside bounds. These are test-owned
        // user edits; export itself creates no runtime/document objects.
        const auto originalData=node->toJson();
        QDir().mkpath(root+"/mob-fixtures");
        {CMob extra;extra.applyParameterState(originalMob->parameterState());extra.ensureObjectSection();auto data=originalData;auto* added=new CWorldObj(data);added->setMapId(49001);QVector3D p(170,100,160);added->setPos(p);added->setDrawPosition(p);extra.addNode(added);QByteArray bytes;extra.serializeMob(bytes,false);write(root+"/mob-fixtures/zone1-extra.mob",bytes);}
        QFileInfo extraPath(root+"/mob-fixtures/zone1-extra.mob");check(view.loadMob(extraPath),"second MOB explicitly joins current scene");
        auto* clipped=new CWorldObj(originalData);clipped->setMapId(49002);QVector3D clipPosition(352.4f,144,160);clipped->setPos(clipPosition);clipped->setDrawPosition(clipPosition);clipped->setRot(QQuaternion::fromAxisAndAngle(0,0,1,37));clipped->setState(eHidden);clipped->loadFigure();clipped->loadTexture();originalMob->addNode(clipped);
        auto* deleted=new CWorldObj(originalData);deleted->setMapId(49003);deleted->markAsDeleted();originalMob->addNode(deleted);
        auto pos=node->position();pos+=QVector3D(9,5,0);node->setPos(pos);node->setDrawPosition(pos);view.m_activeMob->setDirty(true);
        const auto dirty=savedState();editor.exportMap(exports+"/dirty",2);
        check(savedState()==dirty&&json(exports+"/dirty/manifest.json")["unsavedChanges"]==true,"unsaved moved hidden object exported at2ppwu and remains dirty");
        {const auto manifest=json(exports+"/dirty/manifest.json");bool deletedExcluded=false,extraIncluded=false,clippedIncluded=false;for(const auto& v:manifest["objects"].toArray()){const auto e=v.toObject();if(e["id"]==49003)deletedExcluded=!e["included"].toBool();if(e["id"]==49001)extraIncluded=e["included"].toBool();if(e["id"]==49002)clippedIncluded=e["included"].toBool();}check(manifest["mobs"].toArray().size()==2&&extraIncluded&&deletedExcluded&&clippedIncluded,"all explicit MOBs included; deleted stays absent; out-of-bounds origin retained");QImage mask(exports+"/dirty/structures-mask.png");int pixels=0;for(int y=270;y<306;++y)for(int x=mask.width()-5;x<mask.width();++x)pixels+=qGray(mask.pixel(x,y))>127;check(pixels>5,"rotated roof crossing MPR edge is clipped geometrically, not dropped by origin");}
        const auto oldName=node->modelName();auto* object=dynamic_cast<CWorldObj*>(node);check(object!=nullptr,"house is a real world object");object->setModelName("missing_export_model_238");
        status=editor.finish(editor.startExport(exports+"/missing"),true);
        check(status["status"]=="failed"&&status["result"].toObject()["code"]=="MISSING_RESOURCES"&&!QDir(exports+"/missing").exists(),"missing model reports ID/resource and never completes");
        object->setModelName(oldName);
        {auto data=originalData;data["Primary texture"]="missing_export_texture_238";auto* missing=new CWorldObj(data);missing->setMapId(49004);missing->loadFigure();missing->loadTexture();originalMob->addNode(missing);
        status=editor.finish(editor.startExport(exports+"/missing-texture"),true);check(status["result"].toObject()["code"]=="MISSING_RESOURCES"&&status["result"].toObject()["object"].toObject()["texture"]=="missing_export_texture_238","real missing texture fails with object/resource identity");originalMob->discardUncommittedNode(missing);}
        auto running=editor.startExport(exports+"/conflict");pos+=QVector3D(1,0,0);node->setPos(pos);node->setDrawPosition(pos);
        status=editor.finish(running,true);check(status["result"].toObject()["code"]=="REVISION_CONFLICT"&&!QDir(exports+"/conflict").exists(),"mid-operation scene change fails rather than mixing passes");
        {const auto baseline=originalMob->worldSet();auto changed=baseline;auto job=editor.startExport(exports+"/environment-conflict");changed.setData(eWsTypeAmbient,QString::number(baseline.data(eWsTypeAmbient).toFloat()+0.125f));originalMob->setWorldSet(changed);
        status=editor.finish(job,true);check(status["result"].toObject()["code"]=="REVISION_CONFLICT","authored environment changes invalidate snapshot even without a new Undo index");originalMob->setWorldSet(baseline);}
        for(const QString phase:{QString("capturing"),QString("writing")}) {
            const auto directory=exports+"/cancel-"+phase;auto job=editor.startExport(directory,2);QJsonObject poll;
            check(until([&](){poll=editor.tool("operation_status",{{"operation",job["operation"]}});if(poll["status"]!="running")throw QString::fromUtf8(QJsonDocument(poll).toJson());return poll["phase"]==phase;},30000),"reach cancellable phase "+phase);
            if(phase=="capturing")check(!QDir().rename(exports,exports+"-renamed"),"validated output parent lease prevents rename/junction substitution");
            const auto unchangedState=savedState();const auto cancelled=editor.tool("cancel_operation",{{"operation",job["operation"]}});
            check(cancelled["status"]=="cancelled"&&savedState()==unchangedState&&!QDir(directory).exists(),"cancel "+phase+" restores scene and never publishes partial files");
            const auto incomplete=cancelled["result"].toObject()["incompletePath"].toString();QTest::qWait(400);
            if(!incomplete.isEmpty())check(json(incomplete+"/manifest.json")["status"]!="complete","cancelled writer leaves no complete manifest");
        }
        // Test-owned edits are reverted in memory before native window cleanup.
        {const bool visible=window.isVisible();const auto state=savedState();view.m_landProgram.removeAllShaders();QString error;
        const auto unavailable=view.captureTopdownPass(ETopdownPass::Terrain,QSize(352,288),{},&error);
        check(unavailable.isNull()&&error.contains("shader")&&window.isVisible()==visible&&savedState()==state,"unavailable shader renderer refuses capture without closing viewport/documents");}
        for(auto* mob:view.m_aMob)mob->setDirty(false);window.findChild<QComboBox*>("aiAgentMode")->setCurrentIndex(0);while(!view.m_aMob.isEmpty())view.unloadMobDocument(view.m_aMob.last(),false);view.m_pLand->setDirty(false);
        out<<checks<<" PASS / 0 FAIL\n";return 0;
    } catch(const QString& error){out<<"FAIL "<<error<<"\n";out.flush();return 1;}
}

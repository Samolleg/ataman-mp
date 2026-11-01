#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "worldobj.h"
#include "ai_agent_edit.h"
#include "ai_agent_server.h"
#include "topdown_export.h"
#include "resourcemanager.h"
#include "neighbor_scene_data.h"
#include "neighbor_visual_preparation.h"
#include "render_performance.h"
#include "settings.h"
#include <QOpenGLFramebufferObject>
#include <windows.h>

namespace {
struct RestoreProviderTime {
    QString path;FILETIME stamp{};
    HANDLE open() const {return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),FILE_READ_ATTRIBUTES|FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);}
    bool change(const QString& filePath){
        path=filePath;HANDLE file=open();if(file==INVALID_HANDLE_VALUE){path.clear();return false;}
        if(!GetFileTime(file,nullptr,nullptr,&stamp)){CloseHandle(file);path.clear();return false;}
        ULARGE_INTEGER time;time.LowPart=stamp.dwLowDateTime;time.HighPart=stamp.dwHighDateTime;time.QuadPart+=50000000;
        FILETIME changed{time.LowPart,time.HighPart};const bool ok=SetFileTime(file,nullptr,nullptr,&changed);CloseHandle(file);return ok;
    }
    bool restore(){
        if(path.isEmpty())return true;
        HANDLE file=open();if(file==INVALID_HANDLE_VALUE)return false;
        const bool ok=SetFileTime(file,nullptr,nullptr,&stamp);CloseHandle(file);if(!ok)return false;
        path.clear();return true;
    }
    ~RestoreProviderTime(){restore();}
};
QTextStream output(stdout);
int checks=0;
void check(bool ok,const QString& text) {
    output<<(ok?"PASS ":"FAIL ")<<text<<'\n';output.flush();
    if(!ok)throw text;
    ++checks;
}
bool until(const std::function<bool()>& done,int timeout=140000) {
    QElapsedTimer time;time.start();
    do { if(done())return true;QTest::qWait(10); } while(time.elapsed()<timeout);
    return false;
}
QByteArray read(const QString& path) { QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
void save(const QString& path,const QJsonObject& value) {
    QFile f(path);check(f.open(QIODevice::WriteOnly),"write evidence "+QFileInfo(path).fileName());
    f.write(QJsonDocument(value).toJson());
}
class Hidden:public QObject {
    bool eventFilter(QObject* object,QEvent* event)override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show)if(auto* dialog=qobject_cast<QMessageBox*>(object)) {
            output<<"DIALOG "<<dialog->text()<<'\n';
            QTimer::singleShot(0,dialog,&QDialog::reject);
        }
        return false;
    }
};
class Client {
    QProcess process;QByteArray buffer;int serial=0;
public:
    explicit Client(const QString& executable) {
        process.start(executable,QStringList{});
        check(process.waitForStarted(10000),"production MCP bridge starts");
        call("initialize",{{"protocolVersion","2025-11-25"}});
    }
    ~Client(){process.closeWriteChannel();if(!process.waitForFinished(2000)){process.kill();process.waitForFinished();}}
    QJsonObject call(const QString& method,const QJsonObject& params) {
        const int id=++serial;
        process.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        for(;;) {
            if(!until([&](){buffer+=process.readAllStandardOutput();return buffer.contains('\n');},40000))throw QString("Bridge timeout: ")+method;
            const int end=buffer.indexOf('\n');const auto reply=QJsonDocument::fromJson(buffer.left(end)).object();buffer.remove(0,end+1);
            if(reply["id"].toInt()!=id)continue;
            if(reply.contains("error"))throw QString::fromUtf8(QJsonDocument(reply).toJson());
            return reply["result"].toObject();
        }
    }
    QJsonObject tool(const QString& name,const QJsonObject& args) {
        const auto result=call("tools/call",{{"name",name},{"arguments",args}});
        return QJsonDocument::fromJson(result["content"].toArray().first().toObject()["text"].toString().toUtf8()).object();
    }
};
QJsonObject verifyExport(const QString& directory) {
    const auto manifest=QJsonDocument::fromJson(read(directory+"/manifest.json")).object();
    check(manifest["status"]=="complete","complete zone9 manifest");
    const QSize size(manifest["width"].toInt(),manifest["height"].toInt());
    check(size.width()>0&&size.height()>0,"positive full-map dimensions");
    check(manifest["files"].toArray().size()==11,"all eleven PNG layers/previews present");
    for(const auto& value:manifest["files"].toArray()) {
        const auto file=value.toObject();const auto name=file["name"].toString();const auto bytes=read(directory+'/'+name);
        check(bytes.size()==file["bytes"].toInt()
            &&QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==file["sha256"].toString(),"PNG hash/size "+name);
        const QImage image=QImage::fromData(bytes);
        check(!image.isNull()&&(name.contains("preview")?qMax(image.width(),image.height())<=1024:image.size()==size),"aligned readable PNG "+name);
    }
    bool tree=false;
    for(const auto& value:manifest["objects"].toArray()) {
        const auto object=value.toObject();
        if(object["id"].toInt()==29&&object["model"]=="nafltr59")
            tree=object["included"].toBool()&&object["category"]=="vegetation"&&object["meshParts"].toInt()>0;
    }
    check(tree,"original zone9 tree ID29 included with real mesh parts");
    QImage vegetation(directory+"/vegetation.png");bool opaque=false;
    for(int y=0;y<vegetation.height()&&!opaque;++y)for(int x=0;x<vegetation.width();++x)
        if(qAlpha(vegetation.pixel(x,y))){opaque=true;break;}
    check(opaque,"vegetation layer contains rendered pixels");
    return manifest;
}
}

// Link the actual production objects; only the existing diagnostic entry point
// is replaced. No alternate renderer, loader, export implementation or live data.
int runAssemblyCatalogCheck() {
    try {
        output.setCodec("UTF-8");
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const bool baseline=qEnvironmentVariableIsSet("EI_EXPORT_EXPECT_MISSING");
        const bool ordinaryFirst=qEnvironmentVariableIsSet("EI_EXPORT_ORDINARY_FIRST");
        check(!root.isEmpty()&&QDir(root+"/exports").exists(),"explicit isolated test root");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QSettings authority;authority.setValue("ui/aiAgentMode",2);authority.setValue("ui/aiAgentAllowSave",false);authority.sync();
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(1100,800);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();check(view.isValid(),"hidden native OpenGL scene ready");
        auto* textures=CTextureList::getInstance();
        QOpenGLTexture* first=nullptr;QJsonArray firstSources;
        if(ordinaryFirst) { first=textures->texture("tree28");firstSources=textures->textureSources(first);check(!firstSources.isEmpty(),"ordinary name-only loader records source identity"); }
        SNeighborSceneData scene;
        scene.staticObjects.append(QJsonObject{{"Model name","nafltr59"},{"Primary texture","tree28"},
            {"Base object",QJsonObject{{"Complection",QJsonArray{0.5,0.5,1.0}}}},{"Object parts",QJsonArray{}}});
        prepareNeighborVisuals(scene,configuredResourceSourcePaths(CObjectList::getInstance()->settings()),[](){return true;});
        check(scene.visuals.size()==1&&scene.visuals.first()->texture.valid(),"real tree28 prepared through neighbor CPU loader");
        view.makeCurrent();auto* uploaded=textures->uploadStaticTexture(scene.visuals.first()->texture,"tree28","nafltr59");
        check(renderTextureReady(uploaded),"neighbor texture uploaded to GPU");
        auto sources=textures->textureSources(uploaded);
        if(baseline)check(sources.isEmpty(),"254 reproduces lost provider after neighbor upload");
        else check(!sources.isEmpty(),"neighbor upload retains exact resource identity");
        auto* cached=textures->texture("tree28","nafltr59");
        check(cached==uploaded&&textures->textureSources(cached)==sources,"ordinary cache hit preserves GPU object and source identity");
        if(ordinaryFirst)check(first==uploaded&&firstSources==sources,"neighbor cache hit does not relabel existing texture");
        if(!baseline) {
            const auto source=sources.first().toObject();QString provider;
            const auto bytes=CObjectList::getInstance()->resourceData(source["member"].toString(),&provider);
            check(source["member"]=="tree28.mmp"&&source["provider"]==QFileInfo(provider).canonicalFilePath()
                &&source["sha256"]==QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()),"tree28 identity matches winning provider and actual bytes");
        }
        // Open synchronously through the normal editor path. Initial terrain
        // hydration on an uncached private profile can outlive open_map's TTL;
        // this regression targets the export operation, not opening deadlines.
        const auto maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));
        check(!maps.isEmpty(),"explicit private map directory");
        window.openMapPair(maps+"/zone9.mpr",maps+"/zone9.mob","zone9");
        check(view.m_pLand->isMprLoad()&&QFileInfo(view.m_pLand->filePath()).canonicalFilePath()==QFileInfo(maps+"/zone9.mpr").canonicalFilePath()
            &&!view.m_aMob.isEmpty(),"production opens private zone9 MPR/MOB");
        check(until([&](){return !CAiEditAdapter::busy(&window);},30000),"map initialization settled");
        Client client(root+"/app/EIMapperMcp.exe");
        check(client.tool("connect_instance",{{"target","editor"}})["background"]==false,"bridge selects isolated hidden editor");
        const auto call=[&](const QString& method,const QJsonObject& args=QJsonObject()){return client.tool(method,args);};
        const auto finish=[&](const QJsonObject& start) {
            check(start.contains("operation"),"asynchronous operation accepted: "+QString::fromUtf8(QJsonDocument(start).toJson(QJsonDocument::Compact)));QJsonObject status;
            check(until([&](){status=call("operation_status",{{"operation",start["operation"]}});return status["status"]!="running";}),"operation reaches terminal status");
            return status;
        };
        if(qEnvironmentVariableIsSet("EI_EXPORT_MEMORY_REGRESSION")) {
            check(view.ensureTerrainAutoCatalogReady()&&view.m_workingTerrainAtlas,"CPU atlas cache hydrated for memory measurement");
            auto* counters=dynamic_cast<COptBool*>(view.settings()->opt("renderPerformanceCounters"));
            check(counters!=nullptr,"performance option available");counters->setValue(true);
            view.makeCurrent();QOpenGLFramebufferObject frame(256,256,QOpenGLFramebufferObject::CombinedDepthStencil);
            check(frame.isValid()&&frame.bind(),"hidden telemetry framebuffer ready");
            view.m_renderMemorySampleTimer.invalidate();RenderPerformanceContext::instance().beginFrame(true,false);
            view.draw(nullptr);frame.release();
            const auto memory=RenderPerformanceContext::instance().currentFrameCounters();
            RenderPerformanceContext::instance().endFrame();
            check(memory.gpuTextureEstimateBytes>=view.m_renderMemorySample.gpuTerrainTextureBytes&&memory.gpuTextureEstimateBytes>0,"telemetry includes actual loaded textures and terrain atlases");
            check(memory.gpuMemoryEstimateBytes==memory.gpuMeshEstimateBytes+memory.gpuTextureEstimateBytes+memory.gpuTerrainBufferEstimateBytes+memory.gpuFramebufferEstimateBytes,"known GPU total equals disjoint allocation categories");
            check(memory.gpuMemoryEstimateBytes>memory.gpuMeshEstimateBytes&&memory.cpuImageEstimateBytes>0&&memory.cpuTerrainEstimateBytes>0,"CPU/image and GPU/mesh values remain separate");
            save(root+"/memory-estimates.json",{{"gpuMesh",double(memory.gpuMeshEstimateBytes)},{"gpuTextures",double(memory.gpuTextureEstimateBytes)},
                {"gpuTerrainTexturesSubset",double(memory.gpuTerrainTextureEstimateBytes)},{"gpuTerrainBuffers",double(memory.gpuTerrainBufferEstimateBytes)},
                {"gpuFramebuffers",double(memory.gpuFramebufferEstimateBytes)},{"cpuImages",double(memory.cpuImageEstimateBytes)},{"cpuTerrain",double(memory.cpuTerrainEstimateBytes)}});
            counters->setValue(false);
        }
        const auto before=call("get_context");const auto revision=CAiEditAdapter::revision(&window);
        save(root+"/context.json",before);
        auto result=finish(call("export_map_topdown",{{"outputDir",root+"/exports/zone9"},{"pixelsPerWorldUnit",4},{"revision",revision}}));
        save(root+"/export-result.json",result);
        check(CAiEditAdapter::revision(&window)==revision&&call("get_context")==before,"export preserves revision and complete context");
        if(baseline) {
            const auto failure=result["result"].toObject();const auto object=failure["object"].toObject();
            check(result["status"]=="failed"&&failure["code"]=="MISSING_RESOURCES"&&object["model"]=="nafltr59"&&object["id"].toInt()==29,
                "254 reproduces original export failure on tree ID29 after neighbor preload");
        } else {
            check(result["status"]=="complete","zone9 export succeeds after neighbor preload");
            verifyExport(root+"/exports/zone9");
            CObjectBase* tree=nullptr;
            for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())if(node->mapId()==29&&node->modelName()=="nafltr59")tree=dynamic_cast<CObjectBase*>(node);
            check(tree!=nullptr,"original tree accessible for missing-resource regression");
            if(qEnvironmentVariableIsSet("EI_EXPORT_WRITER_REGRESSION")) {
                const auto rotation=tree->rotation();const auto state=tree->nodeState();const auto position=tree->drawPosition();
                auto* environmentMob=view.m_aMob.first();const auto environment=environmentMob->worldSet();
                const QStringList cases{"rotation","hidden","deleted","draw-position","environment","cancel","provider"};
                for(const auto& scenario:cases) {
                    const auto directory=root+"/exports/writer-"+scenario;
                    CTopdownExport job(&window,{{"outputDir",directory},{"pixelsPerWorldUnit",4},{"revision",CAiEditAdapter::revision(&window)}});
                    job.begin();check(until([&](){return job.phase()=="writing"||job.done();})&&job.phase()=="writing","reach immutable writer for "+scenario+": "+QString::fromUtf8(QJsonDocument(job.result()).toJson(QJsonDocument::Compact)));
                    RestoreProviderTime providerTime;
                    if(scenario=="rotation")tree->setRot(QQuaternion::fromAxisAndAngle(0,0,1,17)*rotation);
                    else if(scenario=="hidden")tree->setState(state==eHidden?eDraw:eHidden);
                    else if(scenario=="deleted")tree->markAsDeleted(true);
                    else if(scenario=="draw-position")tree->setDrawPosition(position+QVector3D(1,0,0));
                    else if(scenario=="environment"){auto changed=environment;changed.setData(eWsTypeAmbient,QString::number(environment.data(eWsTypeAmbient).toFloat()+0.125f));environmentMob->setWorldSet(changed);}
                    else if(scenario=="provider") {
                        const auto fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_EXPORT_MUTABLE_FIXTURE_ROOT"));
                        const auto provider=textures->textureSources(cached).first().toObject()["provider"].toString();
                        check(!fixture.isEmpty()&&provider.startsWith(fixture+'/',Qt::CaseInsensitive),"provider metadata test restricted to explicit private fixture");
                        check(providerTime.change(provider),"change private provider timestamp without changing bytes");
                    } else job.cancel();
                    check(until([&](){return job.done();})&&job.result()["code"]==(scenario=="cancel"?"CANCELLED":"REVISION_CONFLICT")&&!QDir(directory).exists(),"writer refuses changed/cancelled publication: "+scenario);
                    tree->setRot(rotation);tree->setState(state);tree->markAsDeleted(false);tree->setDrawPosition(position);environmentMob->setWorldSet(environment);
                    check(providerTime.restore(),"restore private provider timestamp");
                    if(scenario=="provider")QTest::qWait(1500); // drain both QFileSystemWatcher notifications/dialog rejections
                }
            }
            view.makeCurrent();auto* missing=textures->texture("__missing_texture_export_regression__","nafltr59");
            check(textures->textureSources(missing).isEmpty(),"truly missing resource still has no false provenance");
            tree->setTexture(missing);
            result=finish(call("export_map_topdown",{{"outputDir",root+"/exports/missing"},{"pixelsPerWorldUnit",1},{"revision",CAiEditAdapter::revision(&window)}}));
            save(root+"/missing-result.json",result);tree->setTexture(cached);
            check(result["status"]=="failed"&&result["result"].toObject()["code"]=="MISSING_RESOURCES"&&!QDir(root+"/exports/missing").exists(),"real missing texture refuses publication of incomplete map");
        }
        for(auto* mob:view.m_aMob)mob->setDirty(false);
        window.findChild<QComboBox*>("aiAgentMode")->setCurrentIndex(0);
        while(!view.m_aMob.isEmpty())view.unloadMobDocument(view.m_aMob.last(),false);
        view.m_pLand->setDirty(false);
        output<<checks<<" PASS / 0 FAIL\n";return 0;
    } catch(const QString& error) { output<<"FAIL "<<error<<'\n';output.flush();return 1; }
}

#include <QtWidgets>
#include <QtTest>
#include <QOpenGLFunctions>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "camera.h"
#include "mob.h"
#include "settings.h"
#include "resource_setup_dialog.h"
#include "resourcemanager.h"
#include "worldobj.h"
#include "part.h"
#include "process_edit_lock.h"

namespace {
struct Hidden:QObject {
    bool eventFilter(QObject* o,QEvent* e) override {
        if(e->type()==QEvent::Polish||e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray read(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
class Client {
    QProcess process;QByteArray buffer;int serial=0;
public:
    int busyCommitPolls{0};
    explicit Client(const QString& exe){process.start(exe,QStringList{});if(!process.waitForStarted(10000))throw QString("bridge start failed");}
    ~Client(){process.closeWriteChannel();if(!process.waitForFinished(3000)){process.kill();process.waitForFinished(3000);}}
    QJsonObject call(const QString& method,const QJsonObject& params={}){
        const int id=++serial;process.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');
        QElapsedTimer timer;timer.start();while(timer.elapsed()<120000){
            buffer+=process.readAllStandardOutput();const int end=buffer.indexOf('\n');
            if(end<0){QTest::qWait(5);continue;}
            const auto response=QJsonDocument::fromJson(buffer.left(end)).object();buffer.remove(0,end+1);
            if(response.value("id").toInt(-1)==id)return response;
        }throw QString("MCP response timeout: ")+method;
    }
    QJsonObject tool(const QString& name,const QJsonObject& args={}){
        const auto response=call("tools/call",{{"name",name},{"arguments",args}});
        const auto content=response["result"].toObject()["content"].toArray();
        if(content.isEmpty())throw QString::fromUtf8(QJsonDocument(response).toJson());
        return QJsonDocument::fromJson(content.first().toObject()["text"].toString().toUtf8()).object();
    }
    QJsonObject open(const QString& map){
        const auto start=tool("open_map",{{"map",map},{"revision",tool("get_revision")["revision"]}});
        if(!start.contains("operation"))throw QString::fromUtf8(QJsonDocument(start).toJson());
        QElapsedTimer timer;timer.start();while(timer.elapsed()<180000){
            const auto status=tool("operation_status",{{"operation",start["operation"]}});
            if(status["status"]=="complete"){
                const auto result=status["result"].toObject();if(result.contains("error"))throw result["error"].toString();
                tool("cancel_operation",{{"operation",start["operation"]}});return result;
            }
            if(status["error"].toString().startsWith("Normal map commit in progress"))++busyCommitPolls;
            else if(status.contains("error"))throw status["error"].toString();
            QTest::qWait(20); // poll the accepted operation; never replay navigation
        }throw QString("headless open timeout");
    }
};
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NEIGHBOR_TEST_ROOT"));
        const QString mod=root+"/Starter/MODS/DejaVu",mapsRoot=mod+"/Maps";
        check(!root.isEmpty(),"explicit isolated fixture");
        if(qEnvironmentVariableIsSet("EI_NEIGHBOR_HEADLESS")&&!qEnvironmentVariable("EI_NEIGHBOR_HEADLESS").isEmpty()){
            // The native-check entry point installs its own temporary settings
            // directory. Use the same explicit private authority as the child.
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
            QSettings authority;authority.setValue("ui/aiAgentMode",2);authority.setValue("ui/aiAgentAllowSave",false);authority.sync();
            const auto options=read(root+"/app/endurance-options.json");
            Client client(root+"/app/EIMapperMcp.exe");client.call("initialize",{{"protocolVersion","2025-11-25"}});
            const auto connected=client.tool("connect_instance",{{"target","background"}});
            if(connected.contains("error"))out<<"CONNECT_ERROR "<<connected["error"].toString()<<"\n";
            check(connected["background"].toBool(),"225 bridge starts isolated background mapper");
            for(const auto& name:{"zone7","zone6"}){
                client.open(name);const auto context=client.tool("get_context");
                check(context["mpr"].toString().endsWith(QString(name)+".mpr"),"225 background opens requested map: "+QString(name));
                check(context["neighbors"].toArray().size()==10,"225 background opens ten world neighbors: "+QString(name));
            }
            check(client.busyCommitPolls>0,"225 busy commit status retains connection through normal map open");
            out<<"HEADLESS_BUSY_POLLS "<<client.busyCommitPolls<<"\n";
            check(client.tool("shutdown_headless")["status"]=="shutting_down","225 clean background shuts down normally");
            check(read(root+"/app/endurance-options.json")==options,"225 background preserves copied options");
            out<<"RESULT "<<passes<<" PASS 0 FAIL\n";out.flush();return 0;
        }
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        auto& view=*window.view();window.resize(1000,720);window.show();view.show();QCoreApplication::processEvents();view.makeCurrent();
        check(view.isValid(),"hidden production OpenGL context");view.m_neighborLandscapesVisible=true;
        {
            const auto count=CPart::sharedMeshCount();CPart cpu;
            SVertexData v{};v.position=QVector3D(0,0,0);cpu.vertData().append(v);
            v.position=QVector3D(1,0,0);cpu.vertData().append(v);v.position=QVector3D(0,1,0);cpu.vertData().append(v);
            view.doneCurrent();cpu.update();CPart copyWithoutContext(cpu);
            check(!cpu.meshIdentity()&&!copyWithoutContext.meshIdentity()&&CPart::sharedMeshCount()==count,"context-free preflight retains CPU geometry without poisoning GPU cache");
            view.makeCurrent();CPart uploaded(cpu);
            check(uploaded.meshIdentity()!=0&&copyWithoutContext.meshIdentity()==uploaded.meshIdentity(),"CPU-only copies upload and share a valid mesh in render context");
        }
        window.refreshEditorWorld();
        QElapsedTimer metadata;metadata.start();while(metadata.elapsed()<60000&&window.m_worldMetadataWatcher)QTest::qWait(20);
        QMap<QString,SWorldMapZoneOrigin> maps;
        for(const auto& map:window.m_editorWorldMaps)if(map.allodId=="gipat")maps.insert(map.mprIdentity.toLower(),map);
        check(maps.contains("zone7")&&maps.contains("zone6")&&maps.contains("gipat3"),"copied current Gipat topology including gipat3");
        bool cancel=false;int prompts=0;
        QTimer messages;QObject::connect(&messages,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){
            out<<"DIALOG "<<box->text()<<"\n";out.flush();++prompts;
            auto* button=box->button(cancel?QMessageBox::Cancel:QMessageBox::Save);
            if(button)button->click();else box->reject();
        }});messages.start(20);
        auto ready=[&](){
            if(view.m_neighborLandscapes.size()!=view.m_neighborPlacements.size()||view.m_neighborObjectUploadScheduled)return false;
            for(const auto& n:view.m_neighborLandscapes)if((n.showObjects&&!n.objectsPrepared)||!n.pendingObjects.isEmpty()||n.landscape->shadowPreparationPending())return false;
            return !view.m_pLand->shadowPreparationPending()&&!view.m_terrainAutoCatalogPending&&!view.m_terrainAutoCatalogBuilding;
        };
        auto open=[&](const QString& name){
            check(maps.contains(name),"target exists: "+name);const auto m=maps[name];QElapsedTimer timer;timer.start();
            const QString mob=m.mobPath.isEmpty()?QString():mod+"/"+m.mobPath;
            window.openNeighborMapPair(mapsRoot+"/"+name+".mpr",mob,m.zoneId,view.m_activeMapWorldOrigin,m.origin);
            if(cancel)return;
            view.makeCurrent();view.paintGL();view.ensureTerrainAutoCatalogReady();
            while(!ready()&&timer.elapsed()<120000){QTest::qWait(10);view.makeCurrent();view.paintGL();}
            check(ready(),"neighbor uploads and shadows complete: "+name);
            check(view.m_pLand->filePath().baseName().compare(name,Qt::CaseInsensitive)==0,"requested map active: "+name);
            out<<"TIMING "<<name<<" ms="<<timer.elapsed()<<" visible="<<view.m_neighborLandscapes.size()<<" retained="<<view.m_neighborRenderCache.size()<<"\n";out.flush();
            int visibleObjects=0;for(const auto& n:view.m_neighborLandscapes)if(n.showObjects)visibleObjects+=n.staticObjects.size();
            check(visibleObjects>0,"touching neighbor objects remain visible: "+name);
        };
        auto extra=[&](const QString& name,bool mixedCase){
            QString path=mapsRoot+"/"+name;
            if(mixedCase)path.replace("/DejaVu/","/Dejavu/");
            check(window.loadMobWithProgress(QFileInfo(path)),"load supplementary MOB: "+name);
            auto* mob=view.currentMob();check(mob&&mob->mobName().compare(name,Qt::CaseInsensitive)==0,"supplementary MOB active: "+name);
            const int count=view.mobs().size();QFileInfo alias(path.toUpper());
            check(view.loadMob(alias)&&view.currentMob()==mob&&view.mobs().size()==count,"case alias activates same MOB without duplicate");
            view.makeCurrent();view.paintGL();QTest::qWait(25);return mob;
        };
        open("zone7");
        view.makeCurrent();auto* gl=QOpenGLContext::currentContext()->functions();
        gl->glBindBuffer(GL_ARRAY_BUFFER,0);gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
        while(gl->glGetError()!=GL_NO_ERROR){}
        const auto unboundBytes=view.m_pLand->residentBytes();
        check(unboundBytes>0&&gl->glGetError()==GL_NO_ERROR,"cache accounting works with unbound GL buffers and emits no error");
        {QOpenGLBuffer unrelated;check(unrelated.create()&&unrelated.bind(),"accounting probe buffer created");unrelated.allocate(1024);
            check(view.m_pLand->residentBytes()==unboundBytes,"cache charge is independent of unrelated current GL buffer");unrelated.release();}
        // Reload an unchanged visible set while its accounting exceeds budget.
        // Weak references do not themselves keep an evicted renderer alive.
        QMap<QString,QWeakPointer<CLandscape>> originalLand;
        QMap<QString,GLuint> originalAtlas;
        QMap<QString,QWeakPointer<CNode>> originalObject;
        for(const auto& n:view.m_neighborLandscapes){
            originalLand[n.placement.zoneId]=n.landscape;originalAtlas[n.placement.zoneId]=n.landscape->glTexture()->textureId();
            if(!n.staticObjects.isEmpty())originalObject[n.placement.zoneId]=n.staticObjects.first();
        }
        const auto evictions=view.m_neighborCacheEvictions;view.loadNeighborLandscapes();
        check(view.m_neighborLandscapes.size()==originalLand.size(),"unchanged over-budget visible set reused immediately");
        for(const auto& n:view.m_neighborLandscapes){
            check(originalLand[n.placement.zoneId].toStrongRef()==n.landscape,"214 same neighbor renderer on refresh: "+n.placement.zoneId);
            check(originalAtlas[n.placement.zoneId]==n.landscape->glTexture()->textureId(),"214 same GPU atlas on refresh: "+n.placement.zoneId);
            if(originalObject.contains(n.placement.zoneId))check(originalObject[n.placement.zoneId].toStrongRef()==n.staticObjects.first(),"214 same neighbor visual on refresh: "+n.placement.zoneId);
        }
        check(view.m_neighborCacheEvictions==evictions,"refresh does not evict still-visible entries");
        originalLand.clear();originalAtlas.clear();originalObject.clear();
        open("zone6");open("zone7");
        check(true,"baseline without supplementary MOB navigates both ways");
        int primaryObjects=0;for(auto* node:view.currentMob()->nodes())if(node->nodeType()==ENodeType::eWorldObject&&!node->isMarkDeleted())++primaryObjects;
        // This enters the exact synchronous scenario-panel callback in226.
        // The freshly compiled baseline host is expected to crash here.
        extra("Autozone7letter.mob",true);
        out<<"OWNER_SCENARIO zone7 + active Autozone7letter -> zone6\n";out.flush();
        open("zone6");
        bool retainedPrimary=false;
        for(const auto& n:view.m_neighborLandscapes)if(QFileInfo(n.placement.mprPath).baseName()=="zone7"){
            check(n.staticObjects.size()==primaryObjects&&primaryObjects>0,"retained primary object count survives supplementary unload");retainedPrimary=true;
        }
        check(retainedPrimary,"zone7 retained as a visible neighbor");open("zone7");
        check(true,"owner scenario roundtrip completed");
        for(int i=0;i<3;++i){extra("Autozone7letter.mob",i%2);open("zone6");open("zone7");}
        for(const auto& name:{"Autozone7altar.mob","Autozone7chest.mob","Autozone7zasada.mob"}){
            extra(name,true);open("zone6");open("zone7");
        }
        // All loaded family members, then switch while the last one is active.
        for(const auto& name:{"Autozone7letter.mob","Autozone7altar.mob","Autozone7chest.mob","Autozone7zasada.mob"})extra(name,false);
        check(view.mobs().size()==5,"primary and four supplements loaded once");
        open("zone6");open("zone7");
        extra("Autozone7letter.mob",true);open("gipat2");
        check(!view.currentMob(),"214 terrain-only gipat2 has no active MOB");
        QVector3D look(20,32,0);view.m_pLand->projectPt(look,true);view.moveCameraToPoint(look);view.m_cam->moveAwayOn(180);
        view.makeCurrent();view.paintGL();const auto withObjects=view.grabFrameBuffer().convertToFormat(QImage::Format_RGB32);
        view.m_staticObjectsVisible=false;view.paintGL();const auto withoutObjects=view.grabFrameBuffer().convertToFormat(QImage::Format_RGB32);
        view.m_staticObjectsVisible=true;quint64 objectPixels=0;
        for(int y=0;y<withObjects.height();++y)for(int x=0;x<withObjects.width();++x)if(withObjects.pixel(x,y)!=withoutObjects.pixel(x,y))++objectPixels;
        check(objectPixels>100,"214/215 neighbor objects contribute to real framebuffer without active MOB");
        check(withObjects.save(root+"/neighbor-objects.png"),"final neighbor framebuffer receipt");out<<"OBJECT_PIXELS "<<objectPixels<<"\n";
        open("zone7");
        // Both close APIs must publish only live documents during notifications.
        bool consistent=true;int notifications=0;
        auto connection=QObject::connect(&view,QOverload<CMob*>::of(&CView::unloadMob),&window,[&](CMob* closing){
            ++notifications;consistent=consistent&&!view.mobs().contains(closing)&&view.currentMob()!=closing;
            for(auto* mob:view.mobs())consistent=consistent&&mob&&mob->nodes().size()>=0;
            view.selectedNodes();
        });
        auto* one=extra("Autozone7letter.mob",false);
        check(view.unloadMobDocument(one,false),"single supplementary document closes");
        one=extra("Autozone7letter.mob",false);view.unloadMob(one->mobName());
        check(view.mobs().size()==1,"named supplementary unload preserves primary");
        auto* primary=view.currentMob();extra("Autozone7letter.mob",false);view.changeCurrentMob(primary);
        check(view.unloadMobDocument(primary,false),"single primary closes while supplement survives");
        view.unloadMob(QString());
        check(consistent&&notifications>=4,"all unload callbacks see detached live registry and active pointer");
        QObject::disconnect(connection);open("zone7");
        // Cancel must keep edits and identities; Save must persist each family member.
        primary=view.currentMob();one=extra("Autozone7letter.mob",true);
        const QString primaryScript=primary->script(),extraScript=one->script();
        const auto primaryDisk=read(primary->filePath().absoluteFilePath()),extraDisk=read(one->filePath().absoluteFilePath());
        primary->setScript(primaryScript+"\n-- retain227 primary\n");primary->setDirty();
        one->setScript(extraScript+"\n-- retain227 extra\n");one->setDirty();
        cancel=true;const int before=prompts;open("zone6");cancel=false;
        check(prompts>before&&view.currentMob()==one&&primary->isDirty()&&one->isDirty(),"Cancel retains both dirty documents and active supplement");
        check(read(primary->filePath().absoluteFilePath())==primaryDisk&&read(one->filePath().absoluteFilePath())==extraDisk,"Cancel leaves both copied files unchanged");
        open("zone6");open("zone7");primary=view.currentMob();one=extra("Autozone7letter.mob",false);
        check(primary->script().contains("retain227 primary")&&one->script().contains("retain227 extra"),"Save and reopen preserve edits in both family members");
        primary->setScript(primaryScript);primary->setDirty();one->setScript(extraScript);one->setDirty();
        check(view.saveMobDocument(primary,primary->filePath())&&view.saveMobDocument(one,one->filePath()),"restore copied scripts through production save");
        // Exceed budget deliberately: only invisible cache entries may disappear.
        open("zone6");QVector<QWeakPointer<CLandscape>> visible;
        int objects=0;for(const auto& n:view.m_neighborLandscapes){visible.append(n.landscape);objects+=n.staticObjects.size();}
        // An invisible cache holder shares the same buffers as a visible scene.
        // The test keeps only weak refs, so it cannot mask premature destruction.
        view.m_neighborRenderCache.append(view.m_neighborLandscapes.first());
        const auto beforeEviction=view.m_neighborCacheEvictions;
        const auto budget=view.m_neighborCacheBudget;view.m_neighborCacheBudget=1;view.makeCurrent();view.trimNeighborRenderCache();
        check(view.m_neighborRenderCache.isEmpty()&&view.m_neighborCacheEvictions>beforeEviction,"over-budget eviction actually removes invisible buffer holder");
        check(view.m_neighborLandscapes.size()==visible.size(),"visible landscapes remain pinned over budget");
        int after=0;for(int i=0;i<visible.size();++i){check(view.m_neighborLandscapes[i].landscape==visible[i].toStrongRef(),"visible landscape identity retained");after+=view.m_neighborLandscapes[i].staticObjects.size();}
        check(objects==after&&objects>0,"visible object holders survive eviction");visible.clear();view.paintGL();view.m_neighborCacheBudget=budget;
        open("zone7");open("zone6");
        check(true,"post-eviction transitions render successfully");
        check(window.isExitAllowed(),"final documents clean");window.closeAll();window.close();
        out<<"RESULT "<<passes<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"RESULT FAIL "<<error<<"\n";out.flush();return 1;}
}

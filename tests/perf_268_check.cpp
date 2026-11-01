#include <QtWidgets>
#include <QOpenGLFunctions>
#define private public
#include "main_window.h"
#include "view.h"
#include "resource_browser.h"
#include "navmesh_panel.h"
#include "resourcemanager.h"
#include "render_performance.h"
#include "render_stall_trace.h"
#undef private
#include "landscape.h"
#include "camera.h"
#include "mob.h"
#include "node.h"
#include "worldobj.h"
#include "settings.h"
#include "profile_database.h"
#include "resource_profile.h"
#include "resourcemanager.h"
#include "part.h"
#include "figure.h"
#include "res_file.h"
#include <algorithm>
#include <cmath>

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(object))
            QTimer::singleShot(0,box,[box](){if(auto* button=box->button(QMessageBox::Cancel))button->click();else box->reject();});
        return false;
    }
};
void write(const QString& path,const QJsonObject& data) {
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))throw path;
    f.write(QJsonDocument(data).toJson());if(!f.commit())throw path;
}
double percentile(QVector<double> v,double p){if(v.isEmpty())return 0;std::sort(v.begin(),v.end());return v[qMin(v.size()-1,int((v.size()-1)*p))];}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!ok)throw text;++passed;};
    const QString root=qEnvironmentVariable("EI_PERF_ROOT"),fixture=qEnvironmentVariable("EI_PERF_FIXTURE");
    Hidden hidden;qApp->installEventFilter(&hidden);qApp->setQuitOnLastWindowClosed(false);
    try {
        check(!root.isEmpty()&&!fixture.isEmpty(),"explicit isolated run/profile");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QSettings().setValue("ui/viewportVisuals/neighborLandscapes",true);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(2900,1800);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();view.m_timer->stop();
        check(view.isValid()&&view.isVisible()&&window.testAttribute(Qt::WA_DontShowOnScreen),"native GL renderer isolated from desktop");
        ei::SProfileDatabaseLoadRequest request;QMap<QString,QString> bindings;
        request.resourceDirectories=dynamic_cast<COptStringList*>(view.settings()->opt(eOptSetResource,"resourceRoots"))->value().toList();
        for(const auto& profile:builtInResourceProfiles())if(profile.id=="deja-vu") {
            request.profileId=profile.id;request.runtimeManifestId=profile.runtimeManifestId;request.providerId=profile.providers.value("database").id;
            for(auto it=profile.providers.begin();it!=profile.providers.end();++it)bindings[it.key()]=it.value().id;
        }
        ei::ProfileDatabaseService databases;auto db=databases.snapshot(request);check(db&&db->usable(),"private current database parsed");
        ei::ProfileDatabaseContext::instance().activate(db,bindings);
        if(qEnvironmentVariableIsSet("EI_PERF_PARTICLE_GRID")) {
            view.m_neighborLandscapesVisible=false;
            check(window.loadLandscapeWithProgress(QFileInfo(fixture+"/particle-grid.mpr")),"synthetic particle terrain opened");
            check(window.loadMobWithProgress(QFileInfo(fixture+"/particle-grid.mob"),false),"synthetic effect grid opened");
            view.m_timer->stop();view.m_cam->moveTo({112,80,0});view.m_cam->moveAwayOn(180);view.makeCurrent();
            auto* gl=QOpenGLContext::currentContext()->functions();while(gl->glGetError()!=GL_NO_ERROR){}
            for(int i=0;i<3;++i)view.paintGL();
            check(gl->glGetError()==GL_NO_ERROR,"cold and warm effect grid preserves GL state");
            check(view.grabFrameBuffer().save(root+"/particle-grid.png"),"particle grid offscreen image");
            out<<passed<<" PASS / 0 FAIL\n";return 0;
        }
        if(qEnvironmentVariableIsSet("EI_PERF_FOCUSED")) {
            auto* browser=window.m_resourceBrowser;
            browser->setActiveProfileSources(configuredResourceSourcePaths(view.settings()).toVector(),"isolated");
            QElapsedTimer catalogWait;catalogWait.start();
            while(browser->m_loadWatcher->isRunning()&&catalogWait.elapsed()<60000){qApp->processEvents();QThread::msleep(1);}
            qApp->processEvents();check(!browser->m_loadWatcher->isRunning(),"resource catalog finished");
            browser->m_search->clear();browser->m_typeFilter->setCurrentIndex(0);browser->m_statusFilter->setCurrentIndex(0);
            QElapsedTimer publication;publication.start();browser->rebuildList();const double catalogMs=publication.nsecsElapsed()/1e6;
            check(browser->m_resources->topLevelItemCount()==browser->m_visibleEntries.size()&&browser->m_visibleEntries.size()>30000,"complete real catalog rows");
            check(browser->m_resources->isSortingEnabled(),"catalog sorting retained");
            browser->m_search->setText("stwali1");check(browser->m_resources->topLevelItemCount()>0,"catalog search retained");
            auto* parent=new QWidget;new QVBoxLayout(parent);auto* panel=new CNavmeshPanel(parent);parent->layout()->addWidget(panel);
            int lateSignals=0;bool tearingDown=false;
            QObject::connect(panel,&CNavmeshPanel::contentChanged,&window,[&](){if(tearingDown)++lateSignals;});
            panel->m_process->blockSignals(true);
#ifdef Q_OS_WIN
            panel->m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=0x08000000;});
#endif
            panel->m_process->start("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe",{"-NoProfile","-NonInteractive","-Command","[Console]::WriteLine('PROGRESS pending teardown'); Start-Sleep -Seconds 30"});
            check(panel->m_process->waitForStarted(5000)&&panel->m_process->waitForReadyRead(5000),"owned hidden process has buffered output");
            panel->m_process->blockSignals(false);tearingDown=true;delete parent;
            write(root+"/focused.json",{{"catalogMs",catalogMs},{"lateTeardownSignals",lateSignals}});
            check(lateSignals==0,"no panel callback escapes destruction");
            auto* objects=CObjectList::getInstance();
            auto* originalFigure=objects->getFigure("stwali1");auto originalLease=objects->retainSceneFigure(originalFigure);
            CResFile originalModel(objects->resourceData("stwali1.mod"));
            const QByteArray validComponent=originalModel.bufferOfFiles().value("fence00");
            check(!validComponent.isEmpty(),"real component available for private validation");
            objects->refreshConfiguredSources();
            check(objects->getFigure("stwali1")==originalFigure,"unchanged MOD and BON reuse validated figure");
            auto* roots=dynamic_cast<COptStringList*>(view.settings()->opt(eOptSetResource,"resourceRoots"));
            const auto savedRoots=roots->value();
            CResFile model;QByteArray links;QDataStream linkStream(&links,QIODevice::WriteOnly);linkStream.setByteOrder(QDataStream::LittleEndian);
            linkStream<<qint32(1)<<qint32(4);linkStream.writeRawData("part",4);linkStream<<qint32(0);
            model.addFiledata("bad",links);model.addFiledata("part","invalid FIG");CResFile archive;archive.addFiledata("bad.mod",model.generateResData());archive.saveToFile(root+"/bad.res");
            roots->setValue(QVector<QString>{root+"/bad.res"});objects->refreshConfiguredSources();
            objects->getFigure("bad");check(objects->m_missingFigureNames.contains("bad.mod"),"malformed assembly negatively cached");
            for(int i=0;i<100;++i)objects->getFigure("bad");
            check(objects->figureProblems("bad.mod").size()==1,"repeated failure preserves one diagnostic");
            model.addFiledata("part",validComponent);archive.addFiledata("bad.mod",model.generateResData());archive.saveToFile(root+"/bad.res");
            objects->refreshConfiguredSources();auto* repaired=objects->getFigure("bad");auto repairedLease=objects->retainSceneFigure(repaired);
            check(repaired&&repaired->name()=="part"&&!objects->m_missingFigureNames.contains("bad.mod"),"repaired private resource is retried");
            auto changed=validComponent;changed[360]=char(changed[360]^1);model.addFiledata("part",changed);archive.addFiledata("bad.mod",model.generateResData());archive.saveToFile(root+"/bad.res");
            objects->refreshConfiguredSources();
            check(objects->getFigure("bad")!=repaired,"changed same-path MOD does not reuse old geometry");
            roots->setValue(savedRoots);objects->refreshConfiguredSources();check(!objects->m_missingFigureNames.contains("bad.mod"),"resource refresh permits retry");
            out<<passed<<" PASS / 0 FAIL\n";return 0;
        }
        view.m_neighborLandscapesVisible=true;
        if(auto* option=dynamic_cast<COptBool*>(view.settings()->opt("renderPerformanceCounters")))option->setValue(true);
        QElapsedTimer clock;clock.start();QString phase="startup";QJsonArray frames,stalls,opens,refreshes,samples;
        QVector<double> steadyCpu,tailCpu;int steadyFrames=0,tailFrames=0;bool painting=false;double lastPulse=0;QTimer heartbeat,paint;
        QElapsedTimer steady;const bool motion=qEnvironmentVariableIsSet("EI_PERF_MOTION");
        int seconds=qEnvironmentVariableIntValue("EI_PERF_SECONDS");if(seconds<=0)seconds=300;
        const qint64 tailStart=qMax(0,seconds-60)*1000;
        heartbeat.setInterval(10);QObject::connect(&heartbeat,&QTimer::timeout,[&](){
            const double now=clock.nsecsElapsed()/1e6,delta=now-lastPulse;lastPulse=now;
            if(delta>100)stalls.append(QJsonObject{{"phase",phase},{"atMs",now},{"ms",delta}});
        });
        paint.setInterval(8);QObject::connect(&paint,&QTimer::timeout,[&](){
            if(painting||!view.m_pLand->isMprLoad())return;
            QScopedValueRollback<bool> guard(painting,true);
            QElapsedTimer t;t.start();view.makeCurrent();view.paintGL();const double elapsed=t.nsecsElapsed()/1e6;
            const auto& c=RenderPerformanceContext::instance().currentFrameCounters();
            if(elapsed>100)frames.append(QJsonObject{{"phase",phase},{"atMs",clock.elapsed()},{"cpuMs",elapsed},{"draws",double(c.drawCalls)}});
            if(phase=="steady") {
                steadyCpu.append(elapsed);++steadyFrames;
                if(steady.elapsed()>=tailStart){tailCpu.append(elapsed);++tailFrames;}
            }
        });
        auto waitFor=[&](const std::function<bool()>& ready,int timeout=180000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<timeout){qApp->processEvents();QThread::msleep(1);}return ready();};
        heartbeat.start();paint.start();window.refreshEditorWorld();
        check(waitFor([&](){return !window.m_worldMetadataWatcher;}),"world topology loaded");
        QMap<QString,SWorldMapZoneOrigin> maps;
        for(const auto& map:window.m_editorWorldMaps)if(map.allodId=="gipat")maps.insert(map.mprIdentity.toLower(),map);
        check(maps.contains("zone1")&&maps.contains("zone2"),"zone1 and zone2 topology present");
        const QString mod=fixture+"/Starter/MODS/DejaVu",mapRoot=mod+"/Maps";
        auto ready=[&](){return !window.backgroundMobLoading()&&!window.m_worldMetadataWatcher&&view.m_neighborLoadStage==0&&!view.m_neighborObjectUploadScheduled;};
        auto open=[&](const QString& name){
            phase="open:"+name;check(maps.contains(name),"map available: "+name);const auto target=maps[name];QElapsedTimer t;t.start();
            window.openNeighborMapPair(mapRoot+"/"+name+".mpr",target.mobPath.isEmpty()?QString():mod+"/"+target.mobPath,target.zoneId,view.m_activeMapWorldOrigin,target.origin);
            const double foreground=t.nsecsElapsed()/1e6;view.m_timer->stop();view.makeCurrent();view.paintGL();
            const double first=t.nsecsElapsed()/1e6;
            check(waitFor(ready),"background completed: "+name);
            check(!view.m_neighborPlacements.isEmpty()&&!view.m_neighborLandscapes.isEmpty(),"neighbors enabled and loaded: "+name);
            check(view.m_pLand->filePath().baseName().compare(name,Qt::CaseInsensitive)==0,"requested active map: "+name);
            opens.append(QJsonObject{{"map",name},{"foregroundMs",foreground},{"firstFrameMs",first},{"completeMs",t.nsecsElapsed()/1e6},{"mobs",view.mobs().size()},{"neighbors",view.m_neighborLandscapes.size()}});
            out<<"OPEN "<<name<<' '<<QJsonDocument(opens.last().toObject()).toJson(QJsonDocument::Compact)<<'\n';out.flush();
        };
        open("zone1");
        CNode* target=nullptr;for(auto* mob:view.mobs())if(mob->filePath().baseName().compare("zone1",Qt::CaseInsensitive)==0)for(auto* node:mob->nodes())if(node->mapId()==9)target=node;
        check(target&&target->modelName().compare("stwali1",Qt::CaseInsensitive)==0,"owner object9 stwali1 present");
        view.m_cam->moveTo({231.3f,92.46f,0});view.m_cam->moveAwayOn(32);view.makeCurrent();view.paintGL();
        check(view.grabFrameBuffer().save(root+"/zone1-object9.png"),"offscreen object9 image saved");
        int objectVertices=0;QVector<CPart*> geometry;QVector3D shape(.5f,1,1);QStringList visible;
        CObjectList::getInstance()->getFigure("stwali1")->getVertexData(geometry,shape,visible,false);
        for(auto* part:geometry)objectVertices+=part->vertData().size();
        qDeleteAll(geometry);
        const auto beforeMeshes=CPart::sharedMeshCount();
        if(qEnvironmentVariableIsSet("EI_PERF_TERRAIN_NOTIFICATION")) {
            auto* browser=window.m_resourceBrowser;const auto sources=configuredResourceSourcePaths(view.settings());
            browser->setActiveProfileSources(sources.toVector(),"isolated");
            check(waitFor([&](){return !browser->m_loadWatcher->isRunning();}),"catalog prepared for publication notification");qApp->processEvents();
            QString terrain;for(const auto& source:sources)if(QFileInfo(source).fileName().compare("HDPack_Terrain.res",Qt::CaseInsensitive)==0)terrain=source;
            check(!terrain.isEmpty(),"owned terrain archive indexed");
            const auto generation=CObjectList::getInstance()->sourceGeneration();const auto atlas=view.m_pLand->glTexture();
            const auto mobs=view.mobs();auto* active=view.currentMob();const int undo=window.m_undoStack->index();
            QElapsedTimer notify;notify.start();view.terrainPublicationFinished(terrain,true);const double callback=notify.nsecsElapsed()/1e6;
            check(waitFor([&](){return !browser->m_loadWatcher->isRunning();}),"terrain catalog publication completes");qApp->processEvents();
            write(root+"/terrain-notification.json",{{"callbackMs",callback},{"catalogCompleteMs",notify.nsecsElapsed()/1e6},{"generationBefore",double(generation)},{"generationAfter",double(CObjectList::getInstance()->sourceGeneration())}});
            check(CObjectList::getInstance()->sourceGeneration()==generation,"terrain-only publication does not reset model generation");
            check(view.m_pLand->glTexture()==atlas&&view.mobs()==mobs&&view.currentMob()==active,"terrain notification retains atlas and MOB instances");
            check(window.m_undoStack->index()==undo,"terrain notification retains Undo position");
            view.terrainPublicationFinished(terrain,false);
            check(CObjectList::getInstance()->sourceGeneration()==generation,"cancelled publication does not reset resources");
            browser->activeResourcesPublished(terrain);
            check(CObjectList::getInstance()->sourceGeneration()>generation,"explicit full resource publication still invalidates models");
            paint.stop();heartbeat.stop();out<<passed<<" PASS / 0 FAIL\n";return 0;
        }
        if(qEnvironmentVariableIsSet("EI_PERF_PARTICLE_TIMING")) {
            paint.stop();heartbeat.stop();view.m_timer->stop();
            window.m_minimapRefreshTimer->stop();window.m_minimapCameraTimer->stop();
            view.makeCurrent();auto* gl=QOpenGLContext::currentContext()->functions();
            const auto camera=view.m_cam->viewMatrix();
            for(int i=0;i<20;++i)view.drawParticles(camera,nullptr);
            gl->glFinish();double wallMs=0,cpuMs=0;quint64 triangles=0,draws=0;
            for(int i=0;i<600;++i) {
                auto& performance=RenderPerformanceContext::instance();performance.beginFrame(true,false);
                QElapsedTimer t;t.start();const auto cpu=CRenderStallTrace::threadCpuNsecs();
                view.drawParticles(camera,nullptr);
                cpuMs+=(CRenderStallTrace::threadCpuNsecs()-cpu)/1e6;wallMs+=t.nsecsElapsed()/1e6;
                triangles+=performance.currentFrameCounters().triangles;draws+=performance.currentFrameCounters().drawCalls;
                performance.endFrame();gl->glFinish();QThread::msleep(1);
            }
            QJsonArray modes;
            const bool originalStatic=view.m_staticObjectsVisible;
            for(bool visible:{true,false,false,true}) {
                view.m_staticObjectsVisible=visible;view.m_spatialIndexDirty=true;
                for(int i=0;i<5;++i){view.paintGL();gl->glFinish();}
                double submit=0,cpuSubmit=0,drain=0,frameDraws=0,frameTriangles=0;
                for(int i=0;i<120;++i) {
                    QElapsedTimer t;t.start();const auto cpu=CRenderStallTrace::threadCpuNsecs();
                    view.paintGL();cpuSubmit+=(CRenderStallTrace::threadCpuNsecs()-cpu)/1e6;submit+=t.nsecsElapsed()/1e6;
                    const auto& counters=RenderPerformanceContext::instance().currentFrameCounters();frameDraws+=counters.drawCalls;frameTriangles+=counters.triangles;
                    t.restart();gl->glFinish();drain+=t.nsecsElapsed()/1e6;QThread::msleep(1);
                }
                modes.append(QJsonObject{{"staticObjects",visible},{"submitMs",submit/120},{"threadCpuMs",cpuSubmit/120},{"gpuDrainMs",drain/120},{"draws",frameDraws/120},{"triangles",frameTriangles/120}});
            }
            view.m_staticObjectsVisible=originalStatic;view.m_spatialIndexDirty=true;
            write(root+"/particle-timing.json",{{"frames",600},{"wallMsPerFrame",wallMs/600},{"threadCpuMsPerFrame",cpuMs/600},{"trianglesPerFrame",double(triangles)/600},{"drawsPerFrame",double(draws)/600},{"frameModes",modes}});
            check(gl->glGetError()==GL_NO_ERROR,"particle timing leaves valid GL state");
            if(!qEnvironmentVariableIsSet("EI_PERF_NEIGHBOR_ONLY")){out<<passed<<" PASS / 0 FAIL\n";return 0;}
            heartbeat.start();paint.start();
        }
        if(qEnvironmentVariableIsSet("EI_PERF_NEIGHBOR_ONLY")) {
            check(view.mobs().size()>1,"supplementary MOB loaded for227");
            view.changeCurrentMob(view.mobs().last());
            check(view.currentMob()->filePath().baseName().compare("zone1",Qt::CaseInsensitive)!=0,"supplementary MOB active before neighbor switch");
            open("zone2");open("zone1");
            check(view.mobs().size()==3&&view.currentMob()->filePath().baseName().compare("zone1",Qt::CaseInsensitive)==0,"227 return restores primary and unique AddMob family");
            const auto evictions=view.m_neighborCacheEvictions;
            QMap<QString,QSharedPointer<CLandscape>> retained;
            for(const auto& n:view.m_neighborLandscapes)retained[n.placement.zoneId]=n.landscape;
            view.loadNeighborLandscapes();check(waitFor(ready),"warm neighbor refresh completes");
            for(const auto& n:view.m_neighborLandscapes)check(retained.value(n.placement.zoneId)==n.landscape,"warm neighbor terrain identity retained");
            check(view.m_neighborCacheEvictions==evictions,"visible maps not evicted solely for budget excess");
            auto* primary=view.currentMob();primary->setDirty();const auto destination=maps["zone2"];
            window.openNeighborMapPair(mapRoot+"/zone2.mpr",mod+"/"+destination.mobPath,destination.zoneId,view.m_activeMapWorldOrigin,destination.origin);
            check(view.currentMob()==primary&&view.m_pLand->filePath().baseName()=="zone1","dirty Cancel retains current map and MOB");primary->setDirty(false);
            paint.stop();heartbeat.stop();out<<passed<<" PASS / 0 FAIL\n";out.flush();return 0;
        }
        open("zone2");open("zone1");
        // Same callback used by publication/external resource refresh, isolated
        // from file watchers and owner dialogs. No resource files are rewritten.
        for(int i=0;i<4;++i){phase="refresh:"+QString::number(i);QElapsedTimer t;t.start();view.refreshAfterResourceSetup();
            view.m_timer->stop();check(waitFor(ready),"resource refresh background complete");
            refreshes.append(QJsonObject{{"ms",t.nsecsElapsed()/1e6},{"meshes",double(CPart::sharedMeshCount())}});
        }
        view.m_cam->moveTo({231.3f,92.46f,0});view.m_cam->moveAwayOn(32);
        phase="steady";steady.start();qint64 nextSample=30000,nextMotion=0;bool fixedTail=false;
        while(steady.elapsed()<seconds*1000){
            qApp->processEvents();QThread::msleep(1);
            if(motion&&steady.elapsed()<tailStart&&steady.elapsed()>=nextMotion) {
                const double t=steady.elapsed()/1000.0;
                view.m_cam->moveTo({231.3f+float(8*std::sin(t/13)),92.46f+float(6*std::sin(t/19)),0});
                view.m_cam->moveAwayOn(32+float(4*std::sin(t/11)));nextMotion+=250;
            } else if(motion&&steady.elapsed()>=tailStart&&!fixedTail) {
                view.m_cam->moveTo({231.3f,92.46f,0});view.m_cam->moveAwayOn(32);fixedTail=true;
            }
            if(steady.elapsed()>=nextSample){
                const auto& c=RenderPerformanceContext::instance().currentFrameCounters();
                samples.append(QJsonObject{{"elapsedMs",steady.elapsed()},{"meshes",double(CPart::sharedMeshCount())},{"draws",double(c.drawCalls)},{"triangles",double(c.triangles)}});
                out<<"STEADY "<<steady.elapsed()<<"ms meshes="<<CPart::sharedMeshCount()<<'\n';out.flush();nextSample+=30000;
            }
        }
        paint.stop();heartbeat.stop();phase="done";
        const double wall=steady.nsecsElapsed()/1e9;double sum=0,tailSum=0;for(double n:steadyCpu)sum+=n;for(double n:tailCpu)tailSum+=n;
        QJsonObject result{{"opens",opens},{"refreshes",refreshes},{"stallsOver100ms",stalls},{"framesOver100ms",frames},{"steadySamples",samples},
            {"viewportWidth",view.width()},{"viewportHeight",view.height()},
            {"steadySeconds",wall},{"steadyFrames",steadyFrames},{"offscreenFps",steadyFrames/wall},{"cpuSubmitMs",steadyCpu.isEmpty()?0:sum/steadyCpu.size()},
            {"cpuP95Ms",percentile(steadyCpu,.95)},{"cpuMaxMs",percentile(steadyCpu,1)},{"initialMeshes",double(beforeMeshes)},{"object9Vertices",objectVertices},
            {"cameraMotion",motion},{"tailSeconds",wall-tailStart/1000.0},{"tailFps",tailFrames/(wall-tailStart/1000.0)},
            {"tailCpuMs",tailCpu.isEmpty()?0:tailSum/tailCpu.size()},{"tailP95Ms",percentile(tailCpu,.95)}};
        write(root+"/performance.json",result);
        out<<passed<<" PASS / 0 FAIL; metrics saved\n";out.flush();
        return 0;
    } catch(const QString& error){out<<"ERROR "<<error<<'\n';out.flush();return 1;}
}

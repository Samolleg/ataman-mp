#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "navmesh_panel.h"
#include "navigation_save_events.h"
#include "file_write_activity.h"
#include "endurance_document.h"
#include "staged_save_validator.h"
#include "map_save_batch.h"
#include "landscape.h"
#include "mob.h"
#include "resource_setup_dialog.h"

namespace {
struct HiddenWindows:QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(object))if(w->isWindow()){
                w->setAttribute(Qt::WA_DontShowOnScreen);w->setAttribute(Qt::WA_ShowWithoutActivating);
            }
        return false;
    }
};
QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& path,const QByteArray& data){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit())throw path;}
bool until(const std::function<bool()>& done,int ms=120000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
const SMobSection* find(const QVector<SMobSection>& sections,const QString& path){for(const auto& s:sections){if(s.path==path)return &s;if(auto* child=find(s.children,path))return child;}return nullptr;}
const SMobSection* field(const SMobSection& parent,quint32 type){for(const auto& s:parent.children){if(s.type==type)return &s;if(auto* child=field(s,type))return child;}return nullptr;}
}
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passed;};
    try {
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_TEST_ROOT"));
        const auto game=root+"/game",maps=game+"/MODS/Dejavu/Maps",mock=root+"/app/mock-baker.exe";
        const auto mobPath=maps+"/zone1.mob",mprPath=maps+"/zone1.mpr";
        const auto originalMob=bytes(mobPath),originalMpr=bytes(mprPath);
        check(!root.isEmpty()&&!originalMob.isEmpty()&&!originalMpr.isEmpty(),"private real zone1 inputs");
        CMobDocument mob;CValidationReport report;check(mob.loadBytes(originalMob,mobPath,&report),"lossless zone1 MOB");
        const SMobObjectSnapshot *stat=nullptr,*unit=nullptr;
        for(const auto& o:mob.objects()){if(o.unit&&!unit)unit=&o;if(!o.unit&&o.hasPosition&&o.sectionType==0xB001&&!stat)stat=&o;}
        check(stat&&unit,"static and dynamic records present");
        const auto mutate=[&](const SMobObjectSnapshot& obj,quint32 tag){
            const auto* s=find(mob.sections(),obj.sectionPath);const auto* leaf=s?field(*s,tag):nullptr;
            if(!leaf)throw QString("missing test field %1").arg(tag,0,16);
            auto data=leaf->rawBytes.mid(8);if(data.isEmpty())throw QString("empty field");data[0]=char(uchar(data[0])^1);
            CMobEditTransaction tx;tx.replaceLeafPayload(leaf->path,leaf->sha256,data);QByteArray edited;
            if(!mob.applyTransaction(tx,&edited,&report))throw report.toText();
            return edited;
        };
        for(quint32 tag:{0xB009u,0xB00Au,0xB00Cu,0xB006u})
            check(CNavigationSaveEvents::geometryChanged(mobPath,originalMob,mutate(*stat,tag)),QString("static geometry change detected %1").arg(tag,0,16));
        for(quint32 tag:{0xB009u,0xB00Au,0xB006u})
            check(!CNavigationSaveEvents::geometryChanged(mobPath,originalMob,mutate(*unit,tag)),QString("dynamic unit change ignored %1").arg(tag,0,16));
        check(!CNavigationSaveEvents::geometryChanged(mobPath,originalMob,mutate(*stat,0xB004)),"static name does not affect navigation");
        check(!CNavigationSaveEvents::geometryChanged(mobPath,originalMob,mutate(*stat,0xB007)),"static texture does not affect navigation");
        QByteArray deleted;check(mob.removeObjectSubtree(stat->sectionPath,stat->subtreeSha256,&deleted,&report)&&CNavigationSaveEvents::geometryChanged(mobPath,originalMob,deleted),"static deletion detected");
        QByteArray added;check(mob.duplicateObjectSubtree(stat->sectionPath,stat->subtreeSha256,&added,&report)&&CNavigationSaveEvents::geometryChanged(mobPath,originalMob,added),"static addition detected");
        QByteArray script;check(mob.replaceEmbeddedScript("// navmesh save regression\n",&script,&report)&&!CNavigationSaveEvents::geometryChanged(mobPath,originalMob,script),"script-only save ignored");
        check(!CNavigationSaveEvents::geometryChanged(mprPath,originalMpr,originalMpr),"unchanged MPR ignored");

        HiddenWindows hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        window.resize(1400,900);window.show();auto& view=*window.view();view.m_neighborLandscapesVisible=false;
        window.openNeighborMapPair(mprPath,mobPath,"zone1",{},{});view.makeCurrent();view.ensureTerrainAutoCatalogReady();
        auto* panel=view.navmeshPanel();check(view.isValid()&&view.m_environmentMob&&window.testAttribute(Qt::WA_DontShowOnScreen),"real production view and primary MOB, offscreen");
        panel->setBakerPath(mock);qputenv("NAV64_MOCK","slow");
        QStringList runs;QObject::connect(panel,&CNavmeshPanel::bakeFinished,panel,[&](bool){runs<<panel->bakeLog();});
        auto* model=view.m_environmentMob;CNode *stone=nullptr,*actor=nullptr;
        QSignalSpy saves(CNavigationSaveEvents::instance(),&CNavigationSaveEvents::mapSaved);
        SMapSaveFile failed;failed.path=mobPath;failed.before=originalMob;failed.after=mutate(*stat,0xB009);
        const auto failedBatch=commitMapSaveBatch({failed},root+"/failed-save-backups",0);
        check(!failedBatch.success&&failedBatch.rollbackComplete&&saves.isEmpty()&&bytes(mobPath)==originalMob,"failed batch save publishes no event or file");
        check(!model->saveAs(maps+"/absent/zone1.mob")&&saves.isEmpty(),"failed ordinary MOB save publishes no event");
        for(auto* n:model->nodes()){if(n->nodeType()==eWorldObject&&!stone)stone=n;if(n->nodeType()==eUnit&&!actor)actor=n;}
        check(stone&&actor,"production static object and unit");
        auto pos=actor->position()+QVector3D(.01f,0,0);actor->setPos(pos);model->refreshDirtyFromProjection();
        check(view.saveMobDocument(model,mobPath),"normal unit save succeeds");QTest::qWait(250);check(runs.isEmpty()&&!panel->busy(),"normal unit save does not launch baker");
        pos=stone->position()+QVector3D(.01f,0,0);stone->setPos(pos);model->refreshDirtyFromProjection();
        check(view.saveMobDocument(model,mobPath),"normal static object save succeeds");
        check(until([&](){return panel->busy();},5000),"successful static save automatically starts background baker");
        pos+=QVector3D(.01f,0,0);stone->setPos(pos);model->refreshDirtyFromProjection();check(view.saveMobDocument(model,mobPath),"save while baking succeeds");
        pos+=QVector3D(.01f,0,0);stone->setPos(pos);model->refreshDirtyFromProjection();check(view.saveMobDocument(model,mobPath),"third saved revision succeeds");
        check(until([&](){return runs.size()==2&&!panel->busy();},10000),"two saves during bake coalesce to one follow-up");
        QTest::qWait(250);check(runs.size()==2&&panel->lastBakeSucceeded(),"no unrequested third run");
        check(runs[0].contains("--map zone1 --mob zone1")&&runs[1].contains("--map zone1 --mob zone1"),"automatic CLI uses saved primary map pair");

        const int before=runs.size();panel->requestBakeOnSave();panel->requestBakeOnSave();
        {CFileWriteActivity writing;QTest::qWait(250);check(!panel->busy()&&runs.size()==before,"nested save event loop cannot launch baker");}
        check(until([&](){return panel->busy();},5000),"deferred save launches after publication ends");
        panel->requestBakeOnSave();panel->setScene(maps+"/zone7.mpr",maps+"/zone7.mob",game,maps);panel->requestBakeOnSave();
        check(until([&](){return runs.size()==before+3&&!panel->busy();},10000),"queue survives switching maps");
        check(runs[before+1].contains("--map zone1 --mob zone1")&&runs[before+2].contains("--map zone7 --mob zone7"),"pending bake retains its original map");
        panel->setScene(mprPath,mobPath,game,maps);qputenv("NAV64_MOCK","quality");panel->requestBakeOnSave();
        const int failures=runs.size();check(until([&](){return runs.size()>failures&&!panel->busy();},5000)&&!panel->lastBakeSucceeded(),"automatic quality failure reported without retry loop");
        auto* freshness=panel->findChild<QLabel*>("navmeshFreshness");check(freshness->text().contains(QStringLiteral("старше")),"failed bake leaves geometry stale");
        qputenv("NAV64_MOCK","slow");panel->requestBakeOnSave();check(until([&](){return panel->busy();},5000),"cancel scenario starts");panel->requestBakeOnSave();panel->cancelBake();
        check(until([&](){return !panel->busy();},5000),"cancel stops running bake");const int cancelled=runs.size();QTest::qWait(400);check(runs.size()==cancelled&&!panel->busy()&&freshness->text().contains(QStringLiteral("старше")),"cancel clears follow-up and retains stale state");

        // Use the ordinary production terrain Save path and the actual installed
        // algorithm24, with all writes confined to this private game snapshot.
        panel->setBakerPath(game+"/Engine64/EIRebuild64NavBake.exe");
        STerrainHeightCommand height;height.description="private save/bake check";
        STerrainHeightDelta delta;delta.globalX=4;delta.globalY=4;delta.before=view.m_terrainEdit.height(4,4);delta.after=delta.before+8;height.deltas.append(delta);
        check(view.applyTerrainHeightPreview(height,true),"real terrain edit applied");
        const auto terrainRuns=runs.size();view.saveLand();
        check(bytes(mprPath)!=originalMpr,"ordinary terrain Save publishes changed MPR");
        check(until([&](){return runs.size()>terrainRuns&&!panel->busy();}),"real algorithm24 automatic bake completes");
        write(root+"/real-auto-baker.log",panel->bakeLog().toUtf8());
        check(panel->lastBakeSucceeded()&&panel->lastBakeExitCode()==0&&panel->bakeLog().contains("algorithm=24"),"terrain save triggers real complete algorithm24 exit0");
        for(const auto& profile:nav64Profiles()){const auto d=loadNav64(maps+"/Nav64/zone1--zone1."+profile+".nav64");check(d.valid&&d.header.algorithm==24,"automatic output reads: "+profile);}
        check(until([&](){return panel->document().valid&&!freshness->text().contains(QStringLiteral("старше"));}),"successful automatic bake reloads layer and clears stale state");
        const auto cleanRuns=runs.size();check(view.saveMobDocument(model,mobPath),"unchanged MOB save succeeds");view.saveLand();QTest::qWait(300);check(runs.size()==cleanRuns&&!panel->busy(),"clean MPR/MOB saves do not launch another bake");
        check(!freshness->text().contains(QStringLiteral("Карта изменена"))&&!freshness->text().contains(QStringLiteral("старше")),"non-geometric save does not produce false stale date");
        pos=actor->position()+QVector3D(.01f,0,0);actor->setPos(pos);model->refreshDirtyFromProjection();check(view.saveMobDocument(model,mobPath),"dynamic save after successful bake succeeds");QTest::qWait(300);
        check(runs.size()==cleanRuns&&!panel->busy()&&!freshness->text().contains(QStringLiteral("Карта изменена"))&&!freshness->text().contains(QStringLiteral("старше")),"dynamic save retains fresh geometry without rebaking");
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n";out.flush();return 0;
    } catch(const QString& error){out<<"ERROR "<<error<<"\nRESULT "<<passed<<" PASS 1 FAIL\n";out.flush();return 1;}
}

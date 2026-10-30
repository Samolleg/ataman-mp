#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "settings.h"
#include "landscape.h"
#include "resource_setup_dialog.h"
#include "mob.h"
#include "endurance_document.h"
#include "ai_agent_edit.h"

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray read(const QString& path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
bool write(const QString& path,const QByteArray& bytes) {
    QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
}
QString physical(const QString& path) {return CProcessEditLock::identity(path);}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    const auto check=[&](bool ok,const QString& message) {
        out<<(ok?"PASS ":"FAIL ")<<message<<"\n";out.flush();
        if(!ok)throw message;++passed;
    };
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MOB_BIND_ROOT"));
        const QString maps=root+"/Maps",mpr=maps+"/gipat2.mpr",mob=maps+"/gipat2.mob";
        check(!root.isEmpty()&&QFileInfo(mpr).isFile(),"explicit isolated profile and MPR fixture");
        if(qEnvironmentVariableIsSet("EI_MOB_BIND_READBACK"))
        {
            CWorldMapLayoutDocument fresh;fresh.setProfileDirectory(root,"base-game",QString());
            const QByteArray before=read(fresh.path());
            const auto world=fresh.loadEditorWorld("base-game",{maps});
            check(!world.storageBlocked&&world.zones.size()==2,"new process reads saved world");
            for(const auto& map:world.zones)
            {
                CMobDocument document;CValidationReport report;
                check(!map.mobPathInferred&&!map.mobPath.isEmpty()
                    &&document.load(QFileInfo(fresh.absoluteResourcePath(map.mobPath)),&report)
                    &&!report.hasErrors()&&!document.objects().isEmpty(),
                    "new process resolves durable MOB and native objects for "+map.zoneId);
            }
            check(read(fresh.path())==before,"new-process readback does not rewrite layout");
            out<<"PASS mob-mpr-binding readback "<<passed<<" checks\n";out.flush();return 0;
        }
        const QByteArray terrainBefore=read(mpr),forest=read(maps+"/forest-source.mob");
        CMobDocument forestDoc;CValidationReport report;
        check(forestDoc.loadBytes(forest,"forest-source.mob",&report)&&!report.hasErrors()
              && !forestDoc.objects().isEmpty(),"native populated MOB fixture");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        Hidden hidden;qApp->installEventFilter(&hidden);
        qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[] {
            for(auto* w:QApplication::topLevelWidgets())
                if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();
        });setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        window.m_currentCollectionPath=root+"/private.collection";
        auto& view=*window.view();window.resize(1200,850);window.show();view.show();
        QCoreApplication::processEvents();view.makeCurrent();
        check(view.isValid(),"real hidden production OpenGL view");
        check(window.replaceLoadedLandscape(QFileInfo(mpr)),"normal MPR-only opening succeeds");
        check(view.ensureTerrainAutoCatalogReady(),"deferred MPR document initialized for immediate authoring");
        check(view.mobs().isEmpty(),"MPR without MOB is supported");
        window.refreshEditorWorld();
        auto& layout=window.m_worldMapLayoutDocument;
        check(physical(layout.path())==physical(root+"/world-map-layouts.json"),"private profile owns layout");
        SWorldMapZoneOrigin map;map.profileId="base-game";map.allodId="gipat";
        map.zoneId="gipat2";map.componentAnchorZoneId="gipat2";map.mprIdentity="gipat2";
        map.mprPath="Maps/gipat2.mpr";map.origin=QPointF(864,-128);
        map.sectorWidth=view.activeTerrainSectorSize().width();
        map.sectorHeight=view.activeTerrainSectorSize().height();
        auto neighbor=map;neighbor.zoneId="neighbor";neighbor.mprIdentity="neighbor";
        neighbor.mprPath="Maps/neighbor.mpr";neighbor.origin=QPointF(960,-128);
        check(write(maps+"/neighbor.mpr",terrainBefore),"private neighbor terrain");
        QString error;const bool registered=layout.addEditorMaps({map,neighbor},&error);
        check(registered,QString("register MPR-only maps %1x%2: %3")
              .arg(map.sectorWidth).arg(map.sectorHeight).arg(error));
        window.refreshEditorWorld();
        const auto storedMob=[&](const QString& zone) {
            const auto json=QJsonDocument::fromJson(read(layout.path())).object();
            for(const auto& p:json["profiles"].toArray())
                for(const auto& a:p.toObject()["allods"].toArray())
                    for(const auto& z:a.toObject()["zones"].toArray())
                        if(z.toObject()["zoneId"].toString()==zone)return z.toObject()["mobPath"].toString();
            return QStringLiteral("<absent>");
        };
        const auto create=[&](const QString& target,int expectedRole,bool cancel,bool failBinding) {
            bool driven=false,fileDriven=false,errorSeen=false;QString failure;
            QLockFile blocker(layout.path()+".lock");blocker.setStaleLockTime(0);
            if(failBinding)check(blocker.tryLock(0),"hold world lock to exercise create rollback");
            QTimer modals;modals.setInterval(20);
            QObject::connect(&modals,&QTimer::timeout,[&] {
                for(auto* widget:QApplication::topLevelWidgets()) {
                    if(!widget->isVisible())continue;
                    if(auto* message=qobject_cast<QMessageBox*>(widget)) {
                        out<<"DIALOG "<<message->text()<<"\n";out.flush();
                        if(message->standardButtons().testFlag(QMessageBox::Yes))
                        {message->button(QMessageBox::Yes)->click();continue;}
                        errorSeen=true;if(!failBinding)failure=message->text();
                        message->accept();continue;
                    }
                    if(auto* file=qobject_cast<QFileDialog*>(widget)) {
                        if(fileDriven)continue;fileDriven=true;
                        if(physical(file->selectedFiles().value(0))!=physical(target))
                            failure="default filename/folder did not match active MPR";
                        file->selectFile(target);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);
                        continue;
                    }
                }
            });
            QTimer driver;driver.setInterval(20);
            QObject::connect(&driver,&QTimer::timeout,[&] {
                for(auto* widget:QApplication::topLevelWidgets()) {
                    if(!widget->isVisible())continue;
                    auto* dialog=qobject_cast<QDialog*>(widget);if(!dialog)continue;
                    auto* role=dialog->findChild<QComboBox*>("newMobRole");if(!role)continue;
                    if(errorSeen){dialog->reject();continue;}
                    if(driven)continue;driven=true;
                    if(role->currentIndex()!=expectedRole)failure="incorrect default MOB role";
                    if(cancel){dialog->reject();continue;}
                    dialog->findChild<QCheckBox*>("newMobOpenParameters")->setChecked(false);
                    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                }
            });
            QTimer timeout;timeout.setSingleShot(true);
            QObject::connect(&timeout,&QTimer::timeout,[&] {
                failure="creation dialog timed out";
                for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))d->reject();
            });
            modals.start();driver.start();timeout.start(60000);window.on_actionNew_MOB_triggered();
            driver.stop();modals.stop();timeout.stop();blocker.unlock();
            check(driven&&failure.isEmpty(),"production New MOB default/filename/modal: "+failure);
            if(!cancel)check(fileDriven&&errorSeen==failBinding,"creation result matches injected failure");
        };
        const QByteArray beforeCreate=read(layout.path());
        create(mob,0,true,false);
        check(!QFileInfo::exists(mob)&&read(layout.path())==beforeCreate,"cancel creates no file or binding");
        {
            CMob onlyAdditional;onlyAdditional.setQuestMob(true);onlyAdditional.ensureObjectSection();
            onlyAdditional.setRanges(false,{SRange(1000000,1999999)});
            QByteArray bytes;onlyAdditional.serializeMob(bytes,false);
            const QString path=maps+"/additional-only.mob";
            check(write(path,bytes)&&window.loadMobWithProgress(QFileInfo(path)),"MPR has only a supplemental MOB loaded");
            create(mob,0,true,false);
            check(storedMob("gipat2").isEmpty(),"supplemental-only map still defaults to a new primary");
            view.unloadMob("");
        }
        create(mob,0,false,true);
        check(!QFileInfo::exists(mob)&&view.mobs().isEmpty()
              &&read(layout.path())==beforeCreate,"binding failure rolls back file and loaded MOB");
        check(write(mob,forest),"owned pre-existing MOB for replacement rollback");
        create(mob,0,false,true);
        check(read(mob)==forest&&view.mobs().isEmpty()&&read(layout.path())==beforeCreate,
              "binding failure restores pre-existing MOB bytes and preserves world JSON");
        check(QFile::remove(mob),"remove restored owned fixture before fresh successful creation");
        create(mob,0,false,false);
        check(view.mobs().size()==1&&!view.currentMob()->isQuestMob(),"New MOB creates SC primary for empty MPR");
        check(storedMob("gipat2")=="Maps/gipat2.mob","new primary immediately persists portable binding");
        check(storedMob("neighbor").isEmpty(),"neighbor binding stays untouched");
        const QByteArray afterCreate=read(layout.path());
        create(maps+"/unused.mob",1,true,false);
        check(read(layout.path())==afterCreate,"additional-role dialog cancellation preserves binding");
        const QString renamed=maps+"/chosen-primary.mob";
        check(view.saveMobDocument(view.currentMob(),QFileInfo(renamed)),"normal primary Save As");
        check(storedMob("gipat2")=="Maps/chosen-primary.mob","Save As immediately migrates binding");
        view.unloadMob("");
        layout.reload();map.mobPath.clear();
        check(layout.replaceEditorMap(map,&error),"reset binding for explicit custom-name MOB attachment");
        check(window.loadMobWithProgress(QFileInfo(renamed))
              &&storedMob("gipat2")=="Maps/chosen-primary.mob",
              "explicit Open MOB attaches custom-name SC primary to unbound MPR");
        const QByteArray explicitPrimary=read(layout.path());
        const QString comparison=maps+"/comparison-primary.mob";
        check(write(comparison,read(renamed))&&window.loadMobWithProgress(QFileInfo(comparison))
              &&read(layout.path())==explicitPrimary,"opening another SC primary preserves existing explicit binding");
        view.unloadMob("");
        check(window.openDocument(QFileInfo(mpr))&&view.mobs().size()==1
              &&physical(view.currentMob()->filePath().absoluteFilePath())==physical(renamed),
              "ordinary reopen uses explicit custom name ahead of same-name file");
        const QByteArray beforeCollection=read(layout.path());
        window.setWorkspaceIdentity("collection",root+"/unmapped.collection","unmapped",QString());
        check(view.saveMobDocument(view.currentMob(),QFileInfo(maps+"/collection-only.mob"))
              &&read(layout.path())==beforeCollection,"collection Save As cannot change global map MOB binding");
        create(maps+"/unused.mob",1,true,false);
        view.unloadMob("");
        check(window.openDocument(QFileInfo(mpr))&&view.mobs().isEmpty()
              &&window.m_workspaceIdentityKind=="collection"&&read(layout.path())==beforeCollection,
              "re-selecting collection terrain preserves its explicit empty MOB set and identity");
        window.setWorkspaceIdentity(QString(),QString(),QString(),QString());
        check(window.openDocument(QFileInfo(mpr))&&view.mobs().size()==1,
              "leaving collection restores ordinary map companion opening");
        CMob extra;extra.setQuestMob(true);extra.ensureObjectSection();
        extra.setRanges(false,{SRange(1000000,1999999)});QByteArray extraBytes;extra.serializeMob(extraBytes,false);
        const QString additional=maps+"/autozone.mob";
        check(write(additional,extraBytes)&&window.loadMobWithProgress(QFileInfo(additional)),"open supplemental MOB");
        const QByteArray explicitLayout=read(layout.path());
        check(view.saveMobDocument(view.currentMob(),QFileInfo(maps+"/autozone-renamed.mob")),"supplemental Save As");
        check(read(layout.path())==explicitLayout,"supplemental open/save cannot steal primary binding");
        view.unloadMob("");
        check(QFile::remove(mob),"remove owned old same-name MOB for late-appearance scenario");
        layout.reload();map.mobPath.clear();check(layout.replaceEditorMap(map,&error),"reset fixture to explicit MPR-only layout");
        window.refreshEditorWorld();
        check(write(mob,forest),"MOB with objects appears after MPR registration");
        CLandscape::getInstance()->setDirty(true);
        check(window.replaceLoadedLandscape(QFileInfo(mpr)),"reopen same active MPR attaches late MOB");
        check(CLandscape::getInstance()->isDirty(),"same-MPR attachment preserves unsaved terrain");
        check(view.mobs().size()==1&&view.currentMob()->nodes().size()==forestDoc.objects().size(),
              "all native objects immediately loaded from late companion");
        check(storedMob("gipat2")=="Maps/gipat2.mob","late companion persists normal-open binding");
        CLandscape::getInstance()->setDirty(false);
        const QString bad=maps+"/bad.mpr";
        check(write(bad,terrainBefore)&&write(maps+"/bad.mob","broken"),"owned corrupt companion fixture");
        int warnings=0;QTimer dismiss;
        QObject::connect(&dismiss,&QTimer::timeout,[&] {
            for(auto* w:QApplication::topLevelWidgets())if(auto* m=qobject_cast<QMessageBox*>(w))
                if(m->isVisible()){++warnings;m->accept();}
        });dismiss.start(10);
        check(!window.openDocument(QFileInfo(bad)),"corrupt companion rejected before workspace replacement");
        dismiss.stop();
        check(warnings==1&&physical(CLandscape::getInstance()->filePath().absoluteFilePath())==physical(mpr)
              &&view.currentMob()->nodes().size()==forestDoc.objects().size(),"failed opening retains current terrain and objects");
        check(write(maps+"/neighbor.mob",forest),"late companion for neighbor scene");
        window.openNeighborMapPair(maps+"/neighbor.mpr",QString(),"neighbor",map.origin,neighbor.origin);
        check(physical(CLandscape::getInstance()->filePath().absoluteFilePath())==physical(maps+"/neighbor.mpr")
              &&view.currentMob()&&view.currentMob()->nodes().size()==forestDoc.objects().size(),
              "neighbor activation with empty cached MOB path resolves all objects");
        check(storedMob("neighbor")=="Maps/neighbor.mob","neighbor activation persists its binding");
        // Exercise only the affected MCP lifecycle adapter, not terrain baking
        // or unrelated protocol/painting suites. Server authorization is tested
        // separately; these are the production prepare/apply/Undo paths.
        view.unloadMob("");
        check(QFile::remove(maps+"/neighbor.mob"),"owned neighbor MOB removed for MCP creation");
        layout.reload();neighbor.mobPath.clear();
        check(layout.replaceEditorMap(neighbor,&error),"MCP fixture starts with durable empty binding");
        window.refreshEditorWorld();
        check(view.ensureTerrainAutoCatalogReady(),"neighbor editor initialization settled");
        const auto runLifecycle=[&](const QString& method,QJsonObject args) {
            QElapsedTimer timer;timer.start();
            while((CAiEditAdapter::busy(&window)||window.m_worldMetadataWatcher)&&timer.elapsed()<15000)
                QTest::qWait(20);
            args["revision"]=CAiEditAdapter::revision(&window);
            auto plan=CAiEditAdapter::captureLifecycle(&window,method,args);
            check(plan.error.isEmpty(),"MCP capture "+method+": "+plan.error);
            plan=CAiEditAdapter::prepareLifecycle(plan,std::make_shared<std::atomic_bool>(false));
            check(plan.error.isEmpty(),"MCP prepare "+method+": "+plan.error);
            const auto result=CAiEditAdapter::applyLifecycle(&window,plan);
            check(!result.contains("error"),"MCP apply "+method+": "+result["error"].toString());
        };
        const QByteArray beforeMcp=read(layout.path());
        runLifecycle("open_map",{{"map","neighbor"}});
        check(view.mobs().isEmpty()&&window.m_workspaceIdentityKind=="world"
              &&window.m_openedMapZoneId.isEmpty()&&read(layout.path())==beforeMcp,
              "MCP MPR-only navigation preserves unmapped world identity and empty binding");
        runLifecycle("create_mob",{});
        check(view.currentMob()&&!view.currentMob()->isQuestMob()
              &&storedMob("neighbor")=="Maps/neighbor.mob","MCP creates and durably attaches primary");
        const QByteArray createdMcp=read(maps+"/neighbor.mob");
        const auto undone=CAiEditAdapter::undo(&window);
        check(!undone.contains("error")&&view.mobs().isEmpty()
              &&read(maps+"/neighbor.mob")==createdMcp&&read(layout.path())==beforeMcp,
              "MCP Undo retains new file and restores exact empty-binding JSON");
        window.on_actionRedo_triggered();
        check(view.mobs().size()==1&&storedMob("neighbor")=="Maps/neighbor.mob",
              "MCP Redo restores loaded primary and durable binding after read-time discovery");
        view.unloadMob("");layout.reload();
        check(layout.replaceEditorMap(neighbor,&error),"reset only neighbor binding for read-only MCP navigation");
        window.refreshEditorWorld();
        const QByteArray beforeReadOnly=read(layout.path());
        runLifecycle("open_map",{{"map","neighbor"}});
        check(view.mobs().size()==1&&read(layout.path())==beforeReadOnly,
              "MCP navigation loads late companion and leaves world JSON unchanged");
        view.currentMob()->setDirty(true);
        const auto rejected=CAiEditAdapter::captureLifecycle(&window,"open_map",
            {{"map","gipat2"},{"revision",CAiEditAdapter::revision(&window)}});
        check(!rejected.error.isEmpty()&&view.currentMob()->isDirty(),"MCP navigation refuses unsaved MOB without discarding it");
        view.currentMob()->setDirty(false);
        // Finish with populated durable pairs so a separate process can verify
        // real object resolution, without a same-name fallback masking a gap.
        view.unloadMob("");check(write(maps+"/neighbor.mob",forest),"restore populated owned neighbor MOB");
        check(window.replaceLoadedLandscape(QFileInfo(maps+"/neighbor.mpr")),"ordinary open persists companion after read-only navigation");
        CWorldMapLayoutDocument fresh;fresh.setProfileDirectory(root,"base-game",QString());
        const auto reopened=fresh.loadEditorWorld("base-game",{maps});
        check(reopened.zones.size()==2&&!reopened.zones[0].mobPathInferred
              &&!reopened.zones[1].mobPathInferred,"fresh document reads both durable bindings without fallback");
        check(read(mpr)==terrainBefore&&read(maps+"/forest-source.mob")==forest,"source MPR and populated MOB preserved");
        view.unloadMob("");CLandscape::getInstance()->setDirty(false);view.unloadLand(false);
        out<<"PASS mob-mpr-binding "<<passed<<" checks\n";out.flush();return 0;
    } catch(const QString& error) {out<<"FAIL mob-mpr-binding "<<error<<"\n";out.flush();return 1;}
}

#include <QtWidgets>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "settings.h"
#include "camera.h"
#include "render_performance.h"
#include "profile_database.h"
#include "resource_profile.h"
#include "unit.h"

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object)) if(widget->isWindow())
                widget->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show) if(auto* dialog=qobject_cast<QMessageBox*>(object))
            QTimer::singleShot(0,dialog,&QDialog::reject);
        return false;
    }
};
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    const auto check=[&](bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();if(!ok)throw text;++passed;};
    Hidden hidden;qApp->installEventFilter(&hidden);qApp->setQuitOnLastWindowClosed(false);
    try {
        const QString root=qEnvironmentVariable("EI_MOB_LOAD_ROOT"),maps=qEnvironmentVariable("EI_MOB_LOAD_MAPS");
        check(!root.isEmpty()&&!maps.isEmpty(),"explicit isolated paths");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);
        window.resize(2900,1800);window.show();qApp->processEvents();
        auto& view=*window.view();view.show();view.makeCurrent();
        check(view.isValid()&&view.isVisible(),"native renderer logically visible but hidden from desktop");
        ei::SProfileDatabaseLoadRequest databaseRequest;QMap<QString,QString> bindings;
        databaseRequest.resourceDirectories=dynamic_cast<COptStringList*>(view.settings()->opt(eOptSetResource,"resourceRoots"))->value().toList();
        for(const auto& profile:builtInResourceProfiles())if(profile.id=="deja-vu") {
            databaseRequest.profileId=profile.id;databaseRequest.runtimeManifestId=profile.runtimeManifestId;
            databaseRequest.providerId=profile.providers.value("database").id;
            for(auto it=profile.providers.begin();it!=profile.providers.end();++it)bindings[it.key()]=it.value().id;
        }
        ei::ProfileDatabaseService databases;const auto database=databases.snapshot(databaseRequest);
        check(database&&database->usable(),"real private profile database parsed");
        ei::ProfileDatabaseContext::instance().activate(database,bindings);
        check(!bindings.value("characterAppearance").isEmpty()&&!bindings.value("equipmentLayout").isEmpty(),"production character providers active");
        check(window.loadLandscapeWithProgress(QFileInfo(maps+"/zone3obr.mpr")),"private terrain loaded");
        view.m_timer->stop();
        check(view.ensureTerrainAutoCatalogReady(),"terrain editor initialization settled before MOB timing");
        view.m_cam->moveTo({119,71,0});view.m_cam->moveAwayOn(80);
        const bool render=qEnvironmentVariable("EI_MOB_LOAD_RENDER")!="0";
        const bool stats=qEnvironmentVariable("EI_MOB_LOAD_STATS")!="0";
        if(auto* option=dynamic_cast<COptBool*>(view.settings()->opt("renderPerformanceCounters"))) option->setValue(stats);
        qint64 renderMs=0;int callbacks=0;bool painting=false;
        QTimer pump;pump.setInterval(0);
        QObject::connect(&pump,&QTimer::timeout,[&](){
            if(painting)return;QScopedValueRollback<bool> guard(painting,true);++callbacks;
            if(render){QElapsedTimer frame;frame.start();view.paintGL();renderMs+=frame.elapsed();}
        });
        const int diagnosticFramesBefore=view.m_openGlDiagnosticFrame;
        QElapsedTimer elapsed;elapsed.start();pump.start();
        check(window.loadMobWithProgress(QFileInfo(maps+"/zone3obr.mob"),false),"production MOB family load completes");
        const qint64 loadMs=elapsed.elapsed();pump.stop();view.m_timer->stop();
        const int loadingFrames=view.m_openGlDiagnosticFrame-diagnosticFramesBefore;
        if(qEnvironmentVariableIsSet("EI_EXPECT_LOAD_FRAME_FENCE"))
            check(loadingFrames==0,"workspace load performs no intermediate scene frames");
        QElapsedTimer firstFrame;firstFrame.start();view.paintGL();
        const qint64 firstFrameMs=firstFrame.elapsed();
        check(RenderPerformanceContext::instance().currentFrameCounters().drawCalls>0,"completed scene renders after loading");
        check(view.m_aMob.size()==8,"primary plus seven AddMob dependencies loaded");
        check(view.currentMob()->filePath().fileName()=="zone3obr.mob","primary MOB remains active");
        // First-frame lazy visual state depends on prior frame count. Compare
        // stored coordinates immediately, and display coordinates only after
        // identical explicit resolution, outside the timed load/first frame.
        QJsonArray authored;
        for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())authored.append(QJsonArray{mob->filePath().fileName(),int(node->mapId()),
            node->position().x(),node->position().y(),node->position().z()});
        view.makeCurrent();
        QElapsedTimer settling;settling.start();int unresolved=0;
        do {
            unresolved=0;
            for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())if(auto* unit=dynamic_cast<CUnit*>(node)) {
                if(unit->needsVisualResolution())unit->preloadVisualResources();
                if(unit->needsVisualResolution())++unresolved;
            }
            if(unresolved) {qApp->processEvents(QEventLoop::ExcludeUserInputEvents);QThread::msleep(1);view.makeCurrent();}
        } while(unresolved && settling.elapsed()<60000);
        check(unresolved==0,"all asynchronous layer composites resolved before display-coordinate comparison");
        view.ensureUnitSupportSurface();
        for(auto* mob:view.m_aMob)for(auto* node:mob->nodes())if(auto* unit=dynamic_cast<CUnit*>(node)) {
            view.m_pLand->projectPosition(unit);unit->refreshLogicProjection();
        }
        QJsonArray documents,objects,details;int count=0;
        for(auto* mob:view.m_aMob) {
            count+=mob->nodes().size();
            documents.append(QJsonObject{{"file",mob->filePath().fileName()},{"nodes",mob->nodes().size()},{"dirty",mob->isDirty()}});
            check(!mob->isDirty(),"clean "+mob->filePath().fileName());
            for(auto* node:mob->nodes())objects.append(QJsonArray{mob->filePath().fileName(),int(node->mapId()),
                node->drawPosition().x(),node->drawPosition().y(),node->drawPosition().z()});
            for(auto* node:mob->nodes())if(node->mapId()==4623||node->mapId()==3323||node->mapId()==800003||node->mapId()==800005) {
                details.append(QJsonObject{{"file",mob->filePath().fileName()},{"id",int(node->mapId())},
                    {"type",int(node->nodeType())},{"minimumZ",node->minPosition().z()},{"node",node->toJson()}});
            }
        }
        QJsonObject result{{"loadMs",double(loadMs)},{"renderMs",double(renderMs)},{"eventPumpCallbacks",callbacks},
            {"firstFrameMs",double(firstFrameMs)},{"readyMs",double(loadMs+firstFrameMs)},
            {"diagnosticFramesDuringLoad",loadingFrames},
            {"positionsSha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(objects).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())},
            {"authoredPositionsSha256",QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(authored).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex())},
            {"renderEnabled",render},{"statsEnabled",stats},{"nodes",count},{"documents",documents},
            {"viewportWidth",view.width()},{"viewportHeight",view.height()}};
        QFile evidence(root+"/result.json");check(evidence.open(QIODevice::WriteOnly),"evidence file");evidence.write(QJsonDocument(result).toJson());evidence.close();
        QFile positions(root+"/positions.json");check(positions.open(QIODevice::WriteOnly),"position evidence file");
        positions.write(QJsonDocument(objects).toJson());positions.close();
        QFile detailFile(root+"/details.json");if(detailFile.open(QIODevice::WriteOnly))detailFile.write(QJsonDocument(details).toJson());
        check(!window.loadMobWithProgress(QFileInfo(maps+"/absent-regression.mob"),false),"missing MOB rejected");
        check(!view.m_unitVisualPreparationSuspended && view.m_aMob.size()==8,
              "failed load restores workspace state and retains existing documents");
        view.paintGL();
        check(RenderPerformanceContext::instance().currentFrameCounters().drawCalls>0,"render resumes after failed load too");
        check(window.beginWorkspaceLoad("standalone regression"),"begin isolated workspace replacement");
        view.unloadMob("");view.unloadLand(false);window.endWorkspaceLoad();
        check(window.loadMobWithProgress(QFileInfo(maps+"/Autozone3obrclown.mob"),false),"standalone MOB opens without MPR");
        check(!view.m_pLand->isMprLoad()&&view.m_aMob.size()==1&&view.currentMob()->nodes().size()==8,
              "standalone document and all eight objects retained");
        view.paintGL();
        check(!view.m_unitVisualPreparationSuspended&&RenderPerformanceContext::instance().currentFrameCounters().drawCalls>0,
              "standalone viewport resumes and renders");
        out<<"BENCH "<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n'<<passed<<" PASS / 0 FAIL\n";out.flush();
        return 0;
    } catch(const QString& failure){out<<"FAIL "<<failure<<'\n';return 1;}
}

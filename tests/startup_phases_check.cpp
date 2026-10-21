#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "backup_manager.h"
#include "terrain_atlas_recovery_store.h"
#include "endurance_ui_scale.h"
#include "log.h"
#include "neighbor_scene_data.h"
#include "resource_browser.h"
#include "resource_name.h"

namespace {
class CHiddenStartupWindows : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
}
;
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");
    if(qEnvironmentVariableIsSet("EI_STARTUP_PATH_PROBE")) {
        QJsonObject paths;
        paths["home"]=QDir::homePath();
        paths["normalLocal"]=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        paths["normalGeneric"]=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QStandardPaths::setTestModeEnabled(true);
        paths["testLocal"]=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        paths["testGeneric"]=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        out<<QJsonDocument(paths).toJson();out.flush();return 0;
    }
#ifndef EI_STARTUP_BASELINE
    if(qEnvironmentVariableIsSet("EI_STARTUP_HASH_PROBE")) {
        out<<neighborFileSha256(qEnvironmentVariable("EI_STARTUP_HASH_PROBE")).toHex()<<"\n";
        out.flush();return 0;
    }
#endif
    const QString output=QDir::fromNativeSeparators(qEnvironmentVariable("EI_STARTUP_OUTPUT"));
    if(output.isEmpty() || !QDir::isAbsolutePath(output))return 2;
    const auto localData=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if(!localData.startsWith(output+"/"))return 9;
    // Explicit read-only registry source. All subsequent default QSettings are INI.
    QSettings owner(QSettings::NativeFormat,QSettings::UserScope,
                    "EI Mapper Endurance","EI Mapper Endurance Edition");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,output+"/settings");
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,output+"/settings");
    QSettings isolated;
    for(const auto& key:owner.allKeys())isolated.setValue(key,owner.value(key));
    isolated.sync();
    QJsonObject isolation;isolation["localData"]=localData;
    isolation["genericData"]=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    isolation["settings"]=isolated.fileName();isolation["backupRoot"]=CBackupManager::defaultRootPath();
    QFile receipt(output+"/isolation.json");if(!receipt.open(QIODevice::WriteOnly))return 10;
    receipt.write(QJsonDocument(isolation).toJson());receipt.close();
    if(qEnvironmentVariableIsSet("EI_STARTUP_BROWSER_PROBE")) {
        QFile options("endurance-options.json");if(!options.open(QIODevice::ReadOnly))return 11;
        const auto roots=QJsonDocument::fromJson(options.readAll()).object().value("Resource").toObject().value("resourceRoots").toArray();
        QVector<QString> sources;for(const auto& root:roots)sources.append(root.toString());
        CResourceBrowser browser;QElapsedTimer elapsed;elapsed.start();
        browser.setActiveProfileSources(ei::orderedResourceSources(sources),"isolated startup");
        auto* tree=browser.findChild<QTreeWidget*>("resourceContentView");
        while(tree && tree->topLevelItemCount()==0 && elapsed.elapsed()<10000)QTest::qWait(5);
        out<<"BROWSER entries="<<(tree?tree->topLevelItemCount():0)<<" readyMs="<<elapsed.elapsed()<<"\n";out.flush();
        if(tree && tree->topLevelItemCount()) {
            const auto name=tree->topLevelItem(0)->text(0);
            if(!browser.selectResourceReference(name) || !tree->currentItem() || tree->currentItem()->text(0)!=name)return 13;
            out<<"PASS exact resource navigation\n";out.flush();
        }
        return tree && tree->topLevelItemCount()>0?0:12;
    }
    if(qEnvironmentVariableIsSet("EI_STARTUP_FULL_MAIN")) {
        auto* hidden=new CHiddenStartupWindows;hidden->setParent(qApp);qApp->installEventFilter(hidden);
        auto* ready=new QTimer(qApp);ready->setInterval(10);
        QObject::connect(ready,&QTimer::timeout,[ready]() {
            if(qApp->property("enduranceStartupScreenGeometry").isValid())return;
            for(auto* widget:QApplication::topLevelWidgets())if(auto* window=qobject_cast<MainWindow*>(widget)) {
                if(!window->isVisible() || !window->m_pView || !window->m_pView->isValid())return;
                ready->stop();window->m_pView->makeCurrent();window->m_pView->paintGL();
                QTextStream(stdout)<<"PASS full production startup path\n";
                QTimer::singleShot(1000,window,[window](){window->close();});return;
            }
        });ready->start();
        // runHeadlessCommand returns -1 to execute the real splash/translator/
        // MainWindow/show/first-frame/event-loop/shutdown path in qMain.
        return -1;
    }
    CHiddenStartupWindows hidden;qApp->installEventFilter(&hidden);
    ei::applyUiScalePercent(ei::persistedUiScalePercent());
    QElapsedTimer timer;timer.start();
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    auto* view=window.findChild<CView*>();
    if(view){view->makeCurrent();view->paintGL();}
    const qint64 ready=timer.elapsed();
    out<<"STARTUP readyWindowMs="<<ready<<" registryKeys="<<owner.allKeys().size()<<" validGL="<<(view&&view->isValid())
       <<" viewVisible="<<(view&&view->isVisible())<<" minimized="<<window.isMinimized()
       <<" firstFrame="<<qApp->property("startupFrameComplete").toBool()<<"\n";out.flush();
    ei::log(eLogInfo,QString("Startup phases validationReady totalMs:%1").arg(ready));
    if(!view||!view->isValid())return 3;
    CTerrainAtlasRecoveryStore().scheduleCleanup();CBackupManager().scheduleCleanup();
    if(qEnvironmentVariableIsSet("EI_STARTUP_WAIT_RECOVERY")) {
        QElapsedTimer wait;wait.start();
        while(QThreadPool::globalInstance()->activeThreadCount() && wait.elapsed()<60000)QTest::qWait(50);
    } else QTest::qWait(1000);
    QPointer<CView> closing(view);QElapsedTimer closeTimer;closeTimer.start();
    window.close();
    while(closing && closeTimer.elapsed()<120000)QTest::qWait(50);
    if(closing)return 4;
    out<<"CLOSE elapsedMs="<<closeTimer.elapsed()<<"\n";
    out<<"PASS isolated startup\n";out.flush();return 0;
}

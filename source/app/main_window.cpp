#include "main_window.h"
#include "ui_main_window.h"

#include <QWidget>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QUndoView>
#include <QSettings>
#include <QAction>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QMenu>
#include <QApplication>
#include <QStatusBar>
#include <QProgressDialog>
#include <QMouseEvent>
#include <QDockWidget>
#include <QFile>
#include <QTimer>

#include "resourcemanager.h"
#include "landscape.h"
#include "node.h"
#include "settings.h"
#include "select_window.h"
#include "createobjectform.h"
#include "randomize_form.h"
#include "undo.h"
#include "mob\mob.h"
#include "mob\mob_parameters.h"
#include "layout_components/connectors_ui.h"
#include "preview.h"
#include "log.h"
#include "scene.h"
#include "backup_manager.h"
#include "resource_setup_dialog.h"
#include "mob_diff_dialog.h"
#include "window_lifecycle.h"
#include "maps_browser.h"
#include "map_catalog.h"
#include "map_spawn_planner.h"
#include "jmv_map_test_launcher.h"
#include "resource_profile.h"
#include "endurance_document.h"

void testFunc()
{
    ei::log(eLogDebug, "test func start");
    //auto aaa = CResourceManager::getInstance()->tempFolder();
    ei::log(eLogDebug, "test func end");
}


void MainWindow::on_toolButton_2_clicked()
{
    ei::log(eLogDebug, "btn test start");

    ei::log(eLogDebug, "btn test end");
}

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    m_ui(new Ui::MainWindow),
    m_pView(nullptr),
    m_recentFilesMenu(nullptr),
    m_externalScriptAction(nullptr),
    m_mapsDock(nullptr),
    m_mapsBrowser(nullptr),
    m_mapTestAction(nullptr),
    m_mapTestLauncher(nullptr),
    m_closeAfterTestStop(false)
{
#ifdef QT_DEBUG
    testFunc();
#endif
    CIconManager::getInstance()->init();

    m_settings.reset(new CSettings(this));
    m_selector.reset(new CSelectForm(this));
    m_createDialog.reset(new CCreateObjectForm(this));
    m_randomizeForm.reset(new CRandomizeForm(this));
    m_pTileForm.reset(new CTileForm(this));
    m_settings->attachMainWindow(this);
    m_ui->setupUi(this); //init CView core also
    m_mapsBrowser = new CMapsBrowser(this);
    m_mapsDock = new QDockWidget(tr("Maps and Collections"), this);
    m_mapsDock->setObjectName("mapsAndCollectionsDock");
    m_mapsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_mapsDock->setWidget(m_mapsBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, m_mapsDock);
    QMenu* viewMenu = new QMenu(tr("View"), this);
    m_ui->menuBar->insertMenu(m_ui->menuSettings->menuAction(), viewMenu);
    viewMenu->addAction(m_mapsDock->toggleViewAction());
    connect(m_mapsBrowser, &CMapsBrowser::openMapRequested,
            this, &MainWindow::openMapPair);
    m_externalScriptAction = new QAction(tr("Open MOB Script in External Editor"), this);
    m_ui->menuEdit->addAction(m_externalScriptAction);
    connect(m_externalScriptAction, &QAction::triggered, this, [this]()
    {
        if(m_pView && m_pView->hasActiveMob())
            m_pView->openExternalMobScript(m_pView->currentMob());
    });
    m_recentFilesMenu = new QMenu(tr("Open Recent"), this);
    m_ui->menuFile->insertMenu(m_ui->actionOpen_recent, m_recentFilesMenu);
    m_ui->actionCreate_new_object->setEnabled(false);

    QAction* openBackupFolder = new QAction(tr("Open Backup Folder"), this);
    QAction* openLogsFolder = new QAction(tr("Open Logs Folder"), this);
    QAction* openBugReportingGuide = new QAction(tr("Open Bug Reporting Guide"), this);
    QAction* gameAndModSetup = new QAction(tr("Game and Mod Setup..."), this);
    QAction* compareMobFiles = new QAction(tr("Compare MOB Files..."), this);
    m_mapTestAction = new QAction(tr("Test Current Map in JMV"), this);
    m_mapTestAction->setToolTip(tr("Stages unique temporary MPR/MOB copies and launches the validated JMV runtime. No source map is overwritten."));
    m_ui->menuSettings->insertAction(m_ui->actionOpen_2, gameAndModSetup);
    m_ui->menuSettings->insertSeparator(m_ui->actionOpen_2);
    m_ui->menuTools->addSeparator();
    m_ui->menuTools->addAction(compareMobFiles);
    m_ui->menuTools->addAction(m_mapTestAction);
    m_ui->menuTools->addAction(openBackupFolder);
    m_ui->menuTools->addAction(openLogsFolder);
    m_ui->menuTools->addAction(openBugReportingGuide);
    connect(openBackupFolder, &QAction::triggered, this, [this]()
    {
        QString error;
        if(!CBackupManager::openBackupFolder(&error))
            QMessageBox::warning(this, tr("Endurance backups"), error);
    });
    connect(openLogsFolder, &QAction::triggered, this, [this]()
    {
        const QString path = QDir::current().absoluteFilePath("logs");
        QDir().mkpath(path);
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("Endurance logs"),
                tr("Could not open the logs folder:\n%1").arg(path));
    });
    connect(openBugReportingGuide, &QAction::triggered, this, [this]()
    {
        QString path = QDir::current().absoluteFilePath("BUG_REPORTING.md");
        if(!QFileInfo::exists(path))
            path = QDir::current().absoluteFilePath("docs/BUG_REPORTING.md");
        if(!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("Endurance bug reports"),
                tr("BUG_REPORTING.md was not found beside the editor executable."));
    });
    connect(gameAndModSetup, &QAction::triggered, this, &MainWindow::showGameAndModSetup);
    connect(compareMobFiles, &QAction::triggered, this, [this]()
    {
        activateWindow();
        raise();
        QString startDirectory;
        if(COptString* lastFolder = dynamic_cast<COptString*>(m_settings->opt("lastVisitedFolder")))
            startDirectory = lastFolder->value();
        const QString baseline = QFileDialog::getOpenFileName(this, tr("Select baseline MOB"),
            startDirectory, tr("Evil Islands MOB (*.mob)"));
        if(baseline.isEmpty())
            return;
        const QString candidate = QFileDialog::getOpenFileName(this, tr("Select candidate MOB"),
            QFileInfo(baseline).absolutePath(), tr("Evil Islands MOB (*.mob)"));
        if(candidate.isEmpty())
            return;
        CMobDiffDialog* dialog = new CMobDiffDialog(QFileInfo(baseline), QFileInfo(candidate), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    });
    connect(m_mapTestAction, &QAction::triggered, this, [this]()
    {
        if(m_mapTestLauncher && m_mapTestLauncher->isRunning())
        {
            if(QMessageBox::question(this, tr("Stop JMV test"),
                    tr("Stop the game process launched by Endurance?")) == QMessageBox::Yes)
                m_mapTestLauncher->stop();
            return;
        }
        launchCurrentMapTest();
    });

    const bool firstRunSetup = !m_settings->hasConfiguredResources();
    // Complete first-run setup before constructing the OpenGL view. Opening the
    // wizard later allowed initializeGL() to run against empty legacy settings.
    if(firstRunSetup)
        showGameAndModSetup();

    QSettings uiSettings;
    restoreGeometry(uiSettings.value("ui/mainWindowGeometry").toByteArray());
    restoreState(uiSettings.value("ui/mainWindowState").toByteArray());
    m_ui->workspaceSplitter->restoreState(uiSettings.value("ui/workspaceSplitter").toByteArray());
    m_ui->outlinerSplitter->restoreState(uiSettings.value("ui/outlinerSplitter").toByteArray());

    m_pView = new CView(m_ui->centralWidget, m_createDialog.get()->viewWidget());
    m_mapTestLauncher = new CJmvMapTestLauncher(this);
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testStarted, this,
            [this](const QString& runId, const QString& mapName)
    {
        ei::log(eLogInfo, QString("JMV test launch started run:%1 map:%2").arg(runId, mapName));
        statusBar()->showMessage(tr("JMV test is running with temporary map %1").arg(mapName));
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testFinished, this,
            [this](const QString& runId, int exitCode, const QString& cleanupMessage)
    {
        ei::log(eLogInfo, QString("JMV test launch finished run:%1 exit:%2 cleanup:%3")
                .arg(runId).arg(exitCode).arg(cleanupMessage));
        statusBar()->showMessage(tr("JMV test finished (exit code %1). Temporary files were cleaned up.").arg(exitCode), 8000);
        updateMapTestAction();
        if(m_closeAfterTestStop)
        {
            m_closeAfterTestStop = false;
            QTimer::singleShot(0, this, &MainWindow::close);
        }
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testError, this,
            [this](const QString& message)
    {
        ei::log(eLogFatal, message);
        QMessageBox::critical(this, tr("JMV test launch"), message);
        updateMapTestAction();
    });
    const QString recovery = CJmvMapTestLauncher::recoverInterruptedRuns();
    if(!recovery.isEmpty())
        ei::log(eLogWarning, "Recovered interrupted JMV test run(s):\n" + recovery);
    m_createDialog->viewWidget()->attachView(m_pView);
    m_createDialog->viewWidget()->attachSettings(m_settings.get());

    m_pView->setObjectName(QString::fromUtf8("myGLWidget"));
    QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(m_pView->sizePolicy().hasHeightForWidth());
    m_pView->setSizePolicy(sizePolicy);
    m_pView->setMinimumSize(QSize(200, 200));
    m_pView->setMouseTracking(true);
    m_ui->workSpaceLayout->addWidget(m_pView);
    m_ui->workspaceSplitter->setStretchFactor(0, 4);
    m_ui->workspaceSplitter->setStretchFactor(1, 1);
    if(firstRunSetup)
    {
        // Do not carry a cramped, partially initialized splitter state into the
        // first usable session after setup.
        m_ui->workspaceSplitter->setSizes(QList<int>() << 960 << 320);
        m_ui->outlinerSplitter->setSizes(QList<int>() << 420 << 300);
    }

#ifndef QT_DEBUG
  m_ui->toolButton_2->hide();
#endif

    m_selector->attachParents(m_pView);
    m_randomizeForm->attachView(m_pView);
    m_undoStack = new QUndoStack(this);
    int undoHistoryLimit = 512;
    if(COptInt* configuredLimit = dynamic_cast<COptInt*>(m_settings->opt(eOptSetGeneral, "undoHistoryLimit")))
        undoHistoryLimit = qBound(1, configuredLimit->value(), 1000000);
    m_undoStack->setUndoLimit(undoHistoryLimit);
    ei::log(eLogInfo, QString("Undo history limit:%1").arg(undoHistoryLimit));
    createUndoView();
    CStatusConnector::getInstance()->attach(m_ui->statusIco, m_ui->statusBar);
    connectUiButtons();
    m_pView->attach(m_settings.get(), m_ui->tableWidget, m_undoStack, m_ui->progressBar, m_ui->mousePosText, m_ui->treeWidget, m_pTileForm.get());
    initShortcuts();
    connect(m_settings.get(), &CSettings::shortcutsChanged, this, &MainWindow::initShortcuts);
    m_ui->action_Mob_parameters->setEnabled(false);
    m_ui->actionOpen_MOB_Script->setEnabled(false);
    m_externalScriptAction->setEnabled(false);
    QObject::connect(m_pView, SIGNAL(updateMainWindowTitle(eTitleTypeData,QString)), this, SLOT(updateWindowTitle(eTitleTypeData,QString)));
    QObject::connect(CScene::getInstance(), SIGNAL(modeChanged()), m_pView, SLOT(viewParameters()));
    QObject::connect(m_pView, SIGNAL(showOutlinerSignal(bool)), this, SLOT(showOutliner(bool)));


//    m_ui->progressBar->setValue(0);
    m_ui->progressBar->reset();
//    m_ui->progressBar->setVisible(false);

    m_ui->mousePosText->setStyleSheet("* { background-color: rgba(0, 0, 0, 0); }");
    CTextureList::getInstance()->attachSettings(m_settings.get());
    CObjectList::getInstance()->attachSettings(m_settings.get());
    m_createDialog.get()->attach(m_pView, m_undoStack);
    rebuildRecentFilesMenu();
    refreshMapsBrowser();
    updateMapTestAction();
//    if(m_pView->isRecentAvailable())
//        m_ui->actionOpen_recent->setEnabled(true);
}

MainWindow::~MainWindow()
{
    QSettings uiSettings;
    uiSettings.setValue("ui/mainWindowGeometry", saveGeometry());
    uiSettings.setValue("ui/mainWindowState", saveState());
    uiSettings.setValue("ui/workspaceSplitter", m_ui->workspaceSplitter->saveState());
    uiSettings.setValue("ui/outlinerSplitter", m_ui->outlinerSplitter->saveState());
    delete m_pView;
    delete m_ui;
    CIconManager::getInstance()->~CIconManager();
}

void MainWindow::testFunc1()
{
    ei::log(eLogDebug, "test func 1 started");
    QMap<QString, QString> mapName;
    QFile inputFile("c:\\konst\\unit_rename.txt");
    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);
       while (!in.atEnd())
       {
          QString line = in.readLine();
          QStringList list = line.split(";");
          mapName.insert(list[0], list[1]);
       }
       inputFile.close();
    }


    QDir mobFolder("c:\\konst\\Проклятые Земли (Дополнение)\\Mods\\ferneo_mod\\Maps");
    QFileInfo land(mobFolder.absolutePath() + "\\zone1g.mpr");
    m_pView->loadLandscape(land);
    QStringList mobFilter;
    mobFilter << "*.mob";
    QFileInfoList mobFiles = mobFolder.entryInfoList(mobFilter);
    int n=0;
    foreach(QFileInfo filename, mobFiles) {
        m_pView->loadMob(filename);
        m_pView->roundActiveMob();
        n+=m_pView->renameActiveMobUnits(mapName);
        m_pView->saveActiveMob();
        m_pView->unloadActiveMob();
    }
    ei::log(eLogInfo, "renamed:" + QString::number(n) + "units");
    ei::log(eLogDebug, "test func 1 ended");
}

void MainWindow::createUndoView()
{
    m_undoView = new QUndoView(m_undoStack, this);
    m_undoView->setWindowFlag(Qt::Window, true);
    m_undoView->setWindowTitle(tr("History"));
    m_undoView->setAttribute(Qt::WA_QuitOnClose, false);
    m_undoView->viewport()->installEventFilter(this);
}

namespace {
int undoWorkUnits(const QUndoCommand* command)
{
    if(!command)
        return 0;
    int units = 1;
    for(int index = 0; index < command->childCount(); ++index)
        units += undoWorkUnits(command->child(index));
    return units;
}
}

void MainWindow::runUndoToIndex(int targetIndex)
{
    if(!m_undoStack)
        return;
    targetIndex = qBound(0, targetIndex, m_undoStack->count());
    const int currentIndex = m_undoStack->index();
    if(targetIndex == currentIndex)
        return;
    int workUnits = 0;
    const int first = qMin(currentIndex, targetIndex);
    const int last = qMax(currentIndex, targetIndex);
    for(int index = first; index < last; ++index)
        workUnits += undoWorkUnits(m_undoStack->command(index));

    QScopedPointer<QProgressDialog> progress;
    if(last - first > 1 || workUnits >= 8)
    {
        progress.reset(new QProgressDialog(
            targetIndex < currentIndex ? tr("Applying undo history...")
                                       : tr("Applying redo history..."),
            QString(), 0, 0, this));
        progress->setWindowTitle(tr("Updating map"));
        progress->setCancelButton(nullptr);
        progress->setWindowModality(Qt::ApplicationModal);
        progress->setMinimumDuration(0);
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->show();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    ei::log(eLogInfo, QString("Applying undo history current:%1 target:%2 workUnits:%3 progress:%4")
            .arg(currentIndex).arg(targetIndex).arg(workUnits).arg(!progress.isNull()));
    m_undoStack->setIndex(targetIndex);
    if(progress)
        progress->close();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if(m_undoView && watched == m_undoView->viewport()
       && event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
        if(mouse->button() == Qt::LeftButton)
        {
            const QModelIndex item = m_undoView->indexAt(mouse->pos());
            if(item.isValid())
            {
                if(m_pView->cancelPendingOperation())
                    return true;
                runUndoToIndex(item.row());
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::initShortcuts()
{
    const auto shortcut = [this](const char* name, const char* fallback)
    {
        COptString* option = dynamic_cast<COptString*>(m_settings->opt(eOptSetKeyBinding, name));
        return QKeySequence(option ? option->value() : QString::fromLatin1(fallback));
    };
    m_ui->actionSelect_by->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F));
    m_ui->actionOpen->setShortcut(shortcut("shortcutOpen", "Ctrl+O"));
    m_ui->actionOpen_MOB->setShortcut(shortcut("shortcutOpenMob", "Alt+Shift+O"));
    m_ui->actionOpen_MPR->setShortcut(shortcut("shortcutOpenMpr", "Ctrl+Shift+O"));

    m_ui->actionSave->setShortcut(shortcut("shortcutSaveActiveMob", "Ctrl+S"));
    m_ui->actionSave_all_MOB_s->setShortcut(shortcut("shortcutSaveAllMobs", "Alt+Shift+S"));
    //m_ui->actionSave_all_MOB_s->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + eKey_S));
    //m_ui->actionSave_as->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + eKey_S));
    m_ui->actionSave_landscape_MPR->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + eKey_S));

    m_ui->actionSelect_All->setShortcut(QKeySequence(Qt::CTRL + eKey_A));
    m_ui->actionClose_all->setShortcut(QKeySequence(Qt::CTRL + eKey_Q));
    m_ui->actionUndo->setShortcut(shortcut("shortcutUndo", "Ctrl+Z"));
    m_ui->actionRedo->setShortcut(shortcut("shortcutRedo", "Ctrl+Y"));
    m_ui->actionShow_undo_redo->setShortcut(shortcut("shortcutHistory", "Ctrl+L"));
    m_ui->actionCreate_new_object->setShortcut(shortcut("shortcutCreateObject", "Ctrl+N"));
    m_ui->actionDuplicate_selected_objects->setShortcut(shortcut("shortcutDuplicateObjects", "Ctrl+D"));
    m_ui->actionReset_cam_position->setShortcut(shortcut("shortcutResetCamera", "Ctrl+R"));
    m_ui->actionChange_mod_e->setShortcut(shortcut("shortcutSwitchMode", "Ctrl+Tab"));
    //m_ui->actionSwitch_active_MOB->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_T));
    m_ui->action_Mob_parameters->setShortcut(shortcut("shortcutMobParameters", "Ctrl+M"));
    m_ui->actionOpen_MOB_Script->setShortcut(shortcut("shortcutMobScript", "Ctrl+Shift+M"));
    m_externalScriptAction->setShortcut(shortcut("shortcutExternalMobScript", "Ctrl+Alt+M"));
    m_ui->actionReset_logic_paths->setShortcut(shortcut("shortcutResetLogicPaths", "Ctrl+P"));
    m_mapsDock->toggleViewAction()->setShortcut(shortcut("shortcutMapsBrowser", "Ctrl+Space"));
    m_mapTestAction->setShortcut(shortcut("shortcutTestMap", "Ctrl+F5"));
}

void MainWindow::updateMapTestAction()
{
    if(!m_mapTestAction)
        return;
    if(m_mapTestLauncher && m_mapTestLauncher->isRunning())
    {
        m_mapTestAction->setText(tr("Stop JMV Test"));
        m_mapTestAction->setEnabled(true);
        return;
    }
    m_mapTestAction->setText(tr("Test Current Map in JMV"));
    COptString* profile = dynamic_cast<COptString*>(m_settings->opt(eOptSetResource, "resourceProfile"));
    const bool isJmv = profile && profile->value().compare("jamais-vu", Qt::CaseInsensitive) == 0;
    const bool hasPair = m_pView && m_pView->hasActiveMob()
            && m_pView->mobs().size() == 1 && CLandscape::getInstance()->isMprLoad();
    m_mapTestAction->setEnabled(isJmv && hasPair);
    if(!isJmv)
        m_mapTestAction->setToolTip(tr("Direct map testing is currently an experimental JMV-only capability."));
    else if(!hasPair)
        m_mapTestAction->setToolTip(tr("Load one MPR and exactly one active MOB to test the current map."));
    else
        m_mapTestAction->setToolTip(tr("Stages unique temporary map copies and launches the validated JMV runtime (Ctrl+F5)."));
}

void MainWindow::launchCurrentMapTest()
{
    activateWindow();
    raise();
    if(!m_pView || !m_pView->hasActiveMob() || m_pView->mobs().size() != 1
       || !CLandscape::getInstance()->isMprLoad())
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("Load one MPR and exactly one MOB before starting a test."));
        return;
    }
    if(m_pView->cancelPendingOperation())
        statusBar()->showMessage(tr("Pending object movement was cancelled before staging the test."), 4000);
    CLandscape* landscape = CLandscape::getInstance();
    if(landscape->isDirty())
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("Unsaved MPR edits cannot yet be staged losslessly. Save or revert the landscape first."));
        return;
    }

    COptString* profileId = dynamic_cast<COptString*>(m_settings->opt(eOptSetResource, "resourceProfile"));
    SResourceProfileEvaluation profile;
    for(const SResourceProfile& candidate : loadResourceProfiles())
    {
        if(profileId && candidate.id.compare(profileId->value(), Qt::CaseInsensitive) == 0)
        {
            profile = evaluateResourceProfile(m_settings->configuredGameRoot(), candidate);
            break;
        }
    }
    if(profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) != 0
       || !profile.valid || !profile.runtimeStackValidated)
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("The active JMV installation does not match the validated runtime-stack manifest. Run Game and Mod Setup again and review its diagnostics."));
        return;
    }

    QVector<QString> resourceRoots;
    QVector<QString> mapRoots;
    if(COptStringList* value = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "resourceRoots")))
        resourceRoots = value->value();
    if(COptStringList* value = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "mapRoots")))
        mapRoots = value->value();
    const SMapCatalogResult catalog = CMapCatalog::load(resourceRoots, mapRoots);
    SMapCatalogEntry entry;
    const QString loadedMpr = QFileInfo(landscape->filePath()).absoluteFilePath();
    for(const SMapCatalogEntry& candidate : catalog.entries)
    {
        if(QFileInfo(candidate.mprPath).absoluteFilePath().compare(loadedMpr, Qt::CaseInsensitive) == 0
           || candidate.mprName.compare(QFileInfo(loadedMpr).completeBaseName(), Qt::CaseInsensitive) == 0)
        {
            entry = candidate;
            break;
        }
    }
    if(entry.travelZones.isEmpty())
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("No travel/deploy zone was found for the loaded MPR in the active map table. Endurance will not guess a player spawn."));
        return;
    }

    QVector<SMapSpawnObstacle> obstacles;
    CMob* mob = m_pView->currentMob();
    for(CNode* node : mob->nodes())
    {
        if(!node || node->isMarkDeleted())
            continue;
        switch(node->nodeType())
        {
        case eWorldObject:
        case eUnit:
        case eTorch:
        case eLever:
            break;
        default:
            continue;
        }
        CBox box = node->getBBox();
        const double radius = box.isInit() ? qMax(0.75, double(box.radius()) * 0.5) : 0.75;
        SMapSpawnObstacle obstacle;
        obstacle.position = QPointF(node->position().x(), node->position().y());
        obstacle.radius = radius;
        obstacles.append(obstacle);
    }
    const SMapSpawnPlan spawn = CMapSpawnPlanner::choose(entry.travelZones, obstacles);
    if(!spawn.valid)
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("No sufficiently clear point exists inside any travel-zone deploy rectangle. Move blocking objects or choose another map before testing.\n\n%1").arg(spawn.reason));
        return;
    }

    QFile mprFile(landscape->filePath().absoluteFilePath());
    if(!mprFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("JMV test launch"), mprFile.errorString());
        return;
    }
    QByteArray mobData;
    CValidationReport mobReport;
    QString saveClass;
    if(!mob->buildValidatedCandidate(&mobData, &mobReport, &saveClass))
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("The current MOB edits could not be staged safely:\n\n%1").arg(mobReport.toText()));
        return;
    }

    const QString confirmation = tr(
        "Endurance will create uniquely named temporary MPR/MOB files in the active JMV Maps folder, temporarily append one server command to Engine\\2utoexec.cfg, and launch the validated JMV game.exe.\n\n"
        "Source maps are not changed. The exact autoexec bytes are backed up and restored, and temporary maps are deleted after exit. Unsaved MOB edits are included.\n\n"
        "Spawn: travel exit %1 at (%2, %3), verified clearance %4.\n\nContinue?")
        .arg(spawn.exitId).arg(spawn.position.x(), 0, 'f', 2)
        .arg(spawn.position.y(), 0, 'f', 2).arg(spawn.clearance, 0, 'f', 2);
    if(QMessageBox::question(this, tr("Experimental JMV test launch"), confirmation,
                             QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes)
        return;

    SJmvMapTestLaunchInput input;
    input.profile = profile;
    input.sourceMpr = landscape->filePath();
    input.sourceMob = mob->filePath();
    input.mprData = mprFile.readAll();
    input.mobData = mobData;
    input.embeddedScript = mob->script();
    input.mapEntry = entry;
    input.spawn = spawn;
    QString error;
    if(!m_mapTestLauncher->launch(input, &error))
        QMessageBox::critical(this, tr("JMV test launch"), error);
    updateMapTestAction();
}

void MainWindow::refreshMapsBrowser()
{
    if(!m_mapsBrowser)
        return;
    QVector<QString> resources;
    QVector<QString> maps;
    if(COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "resourceRoots")))
        resources = option->value();
    if(COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "mapRoots")))
        maps = option->value();
    m_mapsBrowser->refresh(resources, maps);
}

void MainWindow::openMapPair(const QString& mprPath, const QString& mobPath,
                             const QString& zoneId)
{
    if(!QFileInfo::exists(mprPath) || !QFileInfo::exists(mobPath))
    {
        QMessageBox::warning(this, tr("Open map"),
            tr("The map pair for %1 is incomplete. Refresh the active game/mod profile.").arg(zoneId));
        refreshMapsBrowser();
        return;
    }
    if(!isExitAllowed())
        return;
    m_undoStack->clear();
    m_pView->unloadMob("");
    m_pView->unloadLand();
    m_pView->loadLandscape(QFileInfo(mprPath));
    QFileInfo mobFile(mobPath);
    m_pView->loadMob(mobFile);
    rememberRecentFile(QFileInfo(mprPath));
    rememberRecentFile(QFileInfo(mobPath));
    ei::log(eLogInfo, QString("Opened profile map zone:%1 mpr:%2 mob:%3")
        .arg(zoneId, mprPath, mobPath));
}

void MainWindow::connectUiButtons()
{
    CButtonConnector::getInstance()->attach(m_pView);
    CButtonConnector::getInstance()->addButton(EButtonOpSelect, m_ui->selectButton);
    CButtonConnector::getInstance()->addButton(EButtonOpMove, m_ui->moveButton);
    CButtonConnector::getInstance()->addButton(EButtonOpRotate, m_ui->rotateButton);
    CButtonConnector::getInstance()->addButton(EButtonOpScale, m_ui->scaleButton);
    CButtonConnector::getInstance()->addButton(EButtonOpTilebrush, m_ui->tileBrushButton);
    //disable buttons bcs operations works bad for mouse'moove' action without start point. op's starts work frommouse button position
    m_ui->selectButton->setEnabled(false);
    m_ui->moveButton->setEnabled(false);
    m_ui->rotateButton->setEnabled(false);
    m_ui->scaleButton->setEnabled(false);
    m_ui->tileBrushButton->setEnabled(false);
}

bool MainWindow::isExitAllowed()
{
    m_pView->cancelPendingOperation();
    return m_pView->onExit();
}

void MainWindow::closeAll()
{
    if(isExitAllowed())
    {
        m_undoStack->clear();
        m_pView->unloadMob("");
        m_pView->unloadLand();
    }
}

void MainWindow::showOutliner(bool bShow)
{
    if(bShow)
    {
        m_ui->treeWidget->show();
        m_ui->tableWidget->show();
    }
    else
    {
        m_ui->treeWidget->hide();
        m_ui->tableWidget->hide();
    }
}

void MainWindow::on_actionExit_triggered()
{
    close();
}

void MainWindow::on_actionOpen_triggered()
{
    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    Q_ASSERT(opt);

    QFileInfo fileName = QFileDialog::getOpenFileName(this, "Open MPR or MOB", opt->value(),
                                                       tr("Evil Islands maps (*.mpr *.mob);;MPR (*.mpr);;MOB (*.mob)"));

    if(!fileName.path().isEmpty())
        opt->setValue(fileName.dir().path());

    if(fileName.fileName().toLower().endsWith(".mpr"))
    {
        m_pView->cancelPendingOperation();
        m_undoStack->clear();
        m_pView->loadLandscape(fileName);
        rememberRecentFile(fileName);
    }
    else if(fileName.fileName().toLower().endsWith(".mob"))
    {
        m_pView->cancelPendingOperation();
        m_pView->loadMob(fileName);
        rememberRecentFile(fileName);
    }
}

void MainWindow::showGameAndModSetup()
{
    CResourceSetupDialog dialog(m_settings->configuredGameRoot(), this);
    if(dialog.exec() != QDialog::Accepted)
        return;
    const SResourceProfileEvaluation profile = dialog.evaluation();
    m_settings->applyResourceProfile(profile);
    if(m_pView)
        m_pView->refreshAfterResourceSetup();
    refreshMapsBrowser();
    QMessageBox::information(this, tr("Game and mod setup"),
        tr("%1 is ready. Endurance found %2 RES archives across %3 ordered resource folders.")
            .arg(profile.profileName).arg(profile.archiveCount)
            .arg(profile.resourceDirectories.size()));
}

void MainWindow::rememberRecentFile(const QFileInfo& fileName)
{
    if(!fileName.exists())
        return;
    COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles"));
    if(!option)
        return;

    const QString path = fileName.absoluteFilePath();
    QVector<QString> files = option->value();
    for(int i = files.size() - 1; i >= 0; --i)
    {
        if(QString::compare(files.at(i), path, Qt::CaseInsensitive) == 0)
            files.remove(i);
    }
    files.prepend(path);
    while(files.size() > 12)
        files.removeLast();
    option->setValue(files);
    rebuildRecentFilesMenu();
}

void MainWindow::rebuildRecentFilesMenu()
{
    if(!m_recentFilesMenu)
        return;
    m_recentFilesMenu->clear();
    COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles"));
    QVector<QString> files = option ? option->value() : QVector<QString>();
    if(files.isEmpty())
    {
        QAction* empty = m_recentFilesMenu->addAction(tr("No recent files"));
        empty->setEnabled(false);
        return;
    }

    for(const QString& path : files)
    {
        QFileInfo file(path);
        QAction* action = m_recentFilesMenu->addAction(QString("%1  —  %2")
                .arg(file.fileName(), QDir::toNativeSeparators(file.absolutePath())));
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path]()
        {
            QFileInfo recent(path);
            if(!recent.exists())
            {
                COptStringList* recentOption = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles"));
                if(recentOption)
                {
                    QVector<QString> remaining = recentOption->value();
                    for(int i = remaining.size() - 1; i >= 0; --i)
                        if(QString::compare(remaining.at(i), path, Qt::CaseInsensitive) == 0)
                            remaining.remove(i);
                    recentOption->setValue(remaining);
                }
                rebuildRecentFilesMenu();
                QMessageBox::warning(this, tr("Open recent file"), tr("The file no longer exists:\n%1").arg(path));
                return;
            }
            openDocument(recent);
        });
    }
    m_recentFilesMenu->addSeparator();
    QAction* clear = m_recentFilesMenu->addAction(tr("Clear Recent Files"));
    connect(clear, &QAction::triggered, this, [this]()
    {
        if(COptStringList* option = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetGeneral, "recentFiles")))
            option->setValue({});
        rebuildRecentFilesMenu();
    });
}

bool MainWindow::openDocument(const QFileInfo& fileName)
{
    const QString suffix = fileName.suffix().toLower();
    if(!fileName.exists() || (suffix != "mpr" && suffix != "mob"))
    {
        ei::log(eLogWarning, "Cannot open document: " + fileName.absoluteFilePath());
        return false;
    }

    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    if(opt)
        opt->setValue(fileName.dir().path());
    m_pView->cancelPendingOperation();
    if(suffix == "mpr")
    {
        m_undoStack->clear();
        m_pView->loadLandscape(fileName);
    }
    else
    {
        QFileInfo mobFile(fileName);
        m_pView->loadMob(mobFile);
    }
    rememberRecentFile(fileName);
    return true;
}

void MainWindow::on_actionOpen_MPR_triggered()
{
    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    Q_ASSERT(opt);
    QFileInfo fileName = QFileDialog::getOpenFileName(this, "Open MPR", opt->value(), tr("MPR (*.mpr)"));
    if(fileName.fileName().isEmpty())
        return;
    opt->setValue(fileName.dir().path());
    m_pView->cancelPendingOperation();
    m_undoStack->clear();
    m_pView->loadLandscape(fileName);
    rememberRecentFile(fileName);
}

void MainWindow::on_actionOpen_MOB_triggered()
{
    COptString* opt = dynamic_cast<COptString*>(m_settings->opt(eOptSetGeneral, "lastVisitedFolder"));
    Q_ASSERT(opt);
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Open one or more MOB files", opt->value(), tr("MOB (*.mob)"));
    m_pView->cancelPendingOperation();
    for(const QString& path : paths)
    {
        QFileInfo fileName(path);
        opt->setValue(fileName.dir().path());
        m_pView->loadMob(fileName);
        rememberRecentFile(fileName);
    }
}

void MainWindow::on_actionSettings_triggered()
{
}

void MainWindow::on_actionSave_as_triggered()
{
    m_pView->saveMobAs();
}


void MainWindow::on_actionSelect_All_triggered()
{
    m_selector->selectAll();
}

void MainWindow::on_actionSelect_by_triggered()
{
    m_selector->show();
    m_selector->activateWindow();
}

void MainWindow::on_actionShow_undo_redo_triggered()
{
    m_undoView->show();
    m_undoView->activateWindow();
}

void MainWindow::on_actionClose_all_triggered()
{
    closeAll();
}

void MainWindow::on_actionSave_triggered()
{
    m_pView->saveActiveMob();
}

void MainWindow::on_action_Mob_parameters_triggered()
{
    if(m_pView && m_pView->hasActiveMob())
        m_pView->openActiveMobEditParams();
}


void MainWindow::on_actionUndo_triggered()
{
    if(m_pView->prepareUndo())
        runUndoToIndex(m_undoStack->index() - 1);
}

void MainWindow::on_selectButton_clicked()
{
    qDebug() << "hello Select";
    CButtonConnector::getInstance()->clickButton(EButtonOpSelect);
}

void MainWindow::on_moveButton_clicked()
{
    qDebug() << "TODO: insert gizmo to Move";
    CButtonConnector::getInstance()->clickButton(EButtonOpMove);
}

void MainWindow::on_actionRedo_triggered()
{
    if(m_pView->cancelPendingOperation())
        return;
    runUndoToIndex(m_undoStack->index() + 1);
}

void MainWindow::on_actionOpen_MOB_Script_triggered()
{
    if(m_pView && m_pView->hasActiveMob())
        m_pView->openActiveMobScript();
}

void MainWindow::on_actionDuplicate_selected_objects_triggered()
{
    if(m_pView)
        m_pView->duplicateSelectedObjects();
}


void MainWindow::on_actionCreate_new_object_triggered()
{
    if(!m_pView || !m_pView->hasActiveMob())
    {
        statusBar()->showMessage(tr("Open or activate a MOB before creating an object."), 4000);
        return;
    }
    m_createDialog->show();
    m_createDialog->activateWindow();
}

void MainWindow::updateWindowTitle(eTitleTypeData type, QString data)
{
    switch (type) {
        case eTitleTypeData::eTitleTypeDataActiveMob:
    {
        m_sWindowTitle.activeMob = data;
        m_ui->action_Mob_parameters->setEnabled(!data.isEmpty());
        m_ui->actionOpen_MOB_Script->setEnabled(!data.isEmpty());
        m_externalScriptAction->setEnabled(!data.isEmpty());
        m_ui->actionCreate_new_object->setEnabled(!data.isEmpty());
        break;
    }
    case eTitleTypeData::eTitleTypeDataMpr:
    {
        m_sWindowTitle.mpr = data;
        break;
    }
    case eTitleTypeData::eTitleTypeDataMprDirtyFlag:
    {
        m_sWindowTitle.mprDirty = !data.isEmpty();
        break;
    }
    case eTitleTypeData::eTitleTypeDataDirtyFlag:
    {
        m_sWindowTitle.durty = !data.isEmpty();
        break;
    }
    default:
        Q_ASSERT(false);
        break;
    }
    QString title = QString("EI Mapper: Endurance Edition %1").arg(QCoreApplication::applicationVersion());
    if(!m_sWindowTitle.mpr.isEmpty())
    {
        title += QString(" MPR: (%1)").arg(m_sWindowTitle.mpr);
        if(m_sWindowTitle.mprDirty)
            title += "*";
    }

    if(!m_sWindowTitle.activeMob.isEmpty())
    {
        title += QString(" Active MOB: (%1)").arg(m_sWindowTitle.activeMob);
        if(m_sWindowTitle.durty) //show durty flag only if mob is loaded. if it will be possible to edit mpr, move flag outside this block
            title += "*";
    }


    setWindowTitle(title);
    updateMapTestAction();
}


void MainWindow::on_actionSave_all_MOB_s_triggered()
{
    m_pView->saveAllMob();
}


void MainWindow::on_rotateButton_clicked()
{
    CButtonConnector::getInstance()->clickButton(EButtonOpRotate);
}


void MainWindow::on_scaleButton_clicked()
{
    CButtonConnector::getInstance()->clickButton(EButtonOpScale);
}


void MainWindow::on_actionReset_cam_position_triggered()
{
    m_pView->resetCamPosition();
}


void MainWindow::on_actionChange_mod_e_triggered()
{
    CScene::getInstance()->changeMode(CScene::getInstance()->getMode() == eEditModeObjects ? eEditModeLogic : eEditModeObjects);
}


void MainWindow::on_actionCopy_IDs_to_clipboard_triggered()
{
    m_pView->copySelectedIDsToClipboard();
}


void MainWindow::on_actionOpen_2_triggered()
{
    m_settings->onShow(eOptSetGeneral);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if(m_mapTestLauncher && m_mapTestLauncher->isRunning())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(this,
            tr("JMV test is running"),
            tr("Stop the game process launched by Endurance, clean up its temporary files, and exit?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if(answer != QMessageBox::Yes)
        {
            e->ignore();
            return;
        }
        m_closeAfterTestStop = true;
        m_mapTestLauncher->stop();
        e->ignore();
        return;
    }
    if(!isExitAllowed())
    {
        e->ignore();
        return;
    }

    ei::closeSecondaryWindows(this);
    e->accept();
}

void MainWindow::on_actionOpen_recent_triggered()
{
    m_pView->loadSession();
}


void MainWindow::on_actionReset_logic_paths_triggered()
{
    m_pView->resetUnitLogicPaths();
}


void MainWindow::on_actionRandomize_parameter_triggered()
{
    m_randomizeForm->show();
    m_randomizeForm->activateWindow();
}


void MainWindow::on_actionReset_selected_IDs_triggered()
{
    m_pView->resetSelectedId();
}


void MainWindow::on_action_About_triggered()
{
    QMessageBox::information(this, "About",
        QString("EI Mapper: Endurance Edition %1\n\n"
                "A private, independently versioned derivative of EI Mapper by konstvest.\n"
                "Upstream: https://github.com/konstvest/ei_maper\n"
                "License: GNU GPL-3.0")
            .arg(QCoreApplication::applicationVersion()));
}


void MainWindow::on_actionSave_landscape_MPR_as_triggered()
{
    m_pView->saveLandAs();
}


void MainWindow::on_actionMap_parameters_triggered()
{
    m_pView->openMapParameters();
}


void MainWindow::on_tileBrushButton_clicked()
{
    CButtonConnector::getInstance()->clickButton(EButtonOpTilebrush);
}


void MainWindow::on_actionSave_landscape_MPR_triggered()
{
    m_pView->saveLand();
}


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
#include <QCheckBox>
#include <QToolButton>
#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QActionGroup>
#include <QInputDialog>
#include <QTreeWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QClipboard>
#include <QLineEdit>
#include <QFontMetrics>
#include <QSplitter>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>

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
#include "collection_document.h"
#include "collection_editor_dialog.h"
#include "script_reference_analyzer.h"

namespace
{
constexpr int kValidationPathRole = Qt::UserRole;
constexpr int kValidationCopyRole = Qt::UserRole + 1;
constexpr int kValidationNidRole = Qt::UserRole + 2;
constexpr int kValidationSectionRole = Qt::UserRole + 3;

QString conciseValidationLine(const SMobFamilyFinding& finding)
{
    QString severity;
    switch(finding.severity)
    {
    case EMobFamilyFindingSeverity::Critical: severity = "CRITICAL"; break;
    case EMobFamilyFindingSeverity::Warning: severity = "WARNING"; break;
    case EMobFamilyFindingSeverity::Incomplete: severity = "INCOMPLETE"; break;
    case EMobFamilyFindingSeverity::Info: severity = "INFO"; break;
    }
    const QString line = QString("%1: %2").arg(severity, finding.message.simplified());
    return QFontMetrics(QApplication::font()).elidedText(line, Qt::ElideRight, 960);
}
}

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
    m_undoStack(nullptr),
    m_undoView(nullptr),
    m_pView(nullptr),
    m_testTable(nullptr),
    m_recentFilesMenu(nullptr),
    m_externalScriptAction(nullptr),
    m_mapsDock(nullptr),
    m_validationDock(nullptr),
    m_validationTree(nullptr),
    m_validationSummary(nullptr),
    m_validationTimer(nullptr),
    m_mapsBrowser(nullptr),
    m_mapTestAction(nullptr),
    m_stopMapTestAction(nullptr),
    m_mapTestLauncher(nullptr),
    m_closeAfterTestStop(false),
    m_entranceCombo(nullptr),
    m_playFromHereButton(nullptr),
    m_gotoEntranceButton(nullptr),
    m_godmodeCheckBox(nullptr),
    m_spawnPositionLabel(nullptr),
    m_borrowedMapIdentity(false),
    m_pickSpawnPending(false)
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
    QWidget* outlinerContainer = new QWidget(m_ui->outlinerSplitter);
    QVBoxLayout* outlinerLayout = new QVBoxLayout(outlinerContainer);
    outlinerLayout->setContentsMargins(0, 0, 0, 0);
    outlinerLayout->setSpacing(3);
    QHBoxLayout* filterLayout = new QHBoxLayout;
    filterLayout->setContentsMargins(0, 0, 0, 0);
    m_outlinerFilterEdit = new QLineEdit(outlinerContainer);
    m_outlinerFilterEdit->setClearButtonEnabled(true);
    m_outlinerFilterEdit->setPlaceholderText(tr("Filter objects (name, ID, model, texture, database...)") );
    m_outlinerFilterEdit->setToolTip(tr(
        "Searches for contiguous text by default. Combine terms with spaces. "
        "Use field=value for an exact match or prefix a value with ~ for fuzzy subsequence matching "
        "(for example id:123, id=123, or id:~123). Optional fields: "
        "name:, id:, type:, model:, texture:, db:, prototype:, mob:, validation:."));
    m_outlinerFilterStatus = new QLabel(tr("0 / 0"), outlinerContainer);
    m_outlinerFilterStatus->setMinimumWidth(72);
    m_outlinerFilterStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filterLayout->addWidget(m_outlinerFilterEdit, 1);
    filterLayout->addWidget(m_outlinerFilterStatus);
    outlinerLayout->addLayout(filterLayout);
    m_ui->treeWidget->setParent(outlinerContainer);
    outlinerLayout->addWidget(m_ui->treeWidget, 1);
    m_ui->outlinerSplitter->insertWidget(0, outlinerContainer);
    connect(m_outlinerFilterEdit, &QLineEdit::textChanged,
            m_ui->treeWidget, &CTreeView::setFilterText);
    connect(m_ui->treeWidget, &CTreeView::filterMatchCountChanged,
            this, [this](int visible, int total)
    {
        m_outlinerFilterStatus->setText(tr("%1 / %2").arg(visible).arg(total));
        m_outlinerFilterStatus->setToolTip(tr("%1 matching objects out of %2").arg(visible).arg(total));
    });
    m_mapsBrowser = new CMapsBrowser(this);
    m_mapsDock = new QDockWidget(tr("Maps and Collections"), this);
    m_mapsDock->setObjectName("mapsAndCollectionsDock");
    m_mapsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_mapsDock->setWidget(m_mapsBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, m_mapsDock);
    QWidget* validationWidget = new QWidget(this);
    QVBoxLayout* validationLayout = new QVBoxLayout(validationWidget);
    validationLayout->setContentsMargins(4, 4, 4, 4);
    m_validationSummary = new QLabel(tr("No active MOB family."), validationWidget);
    m_validationSummary->setWordWrap(true);
    m_validationTree = new QTreeWidget(validationWidget);
    m_validationTree->setColumnCount(4);
    m_validationTree->setHeaderLabels(QStringList() << tr("Severity") << tr("MOB / Type")
                                      << tr("Source path") << tr("Details"));
    m_validationTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_validationTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_validationTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_validationTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_validationTree->setUniformRowHeights(true);
    m_validationTree->setContextMenuPolicy(Qt::CustomContextMenu);
    validationLayout->addWidget(m_validationSummary);
    validationLayout->addWidget(m_validationTree);
    m_validationShowMore = new QPushButton(tr("Show 250 more findings"), validationWidget);
    m_validationShowMore->hide();
    validationLayout->addWidget(m_validationShowMore);
    m_validationDock = new QDockWidget(tr("MOB Family Validation"), this);
    m_validationDock->setObjectName("mobFamilyValidationDock");
    m_validationDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea |
                                      Qt::RightDockWidgetArea);
    m_validationDock->setFeatures(QDockWidget::DockWidgetClosable |
                                  QDockWidget::DockWidgetMovable |
                                  QDockWidget::DockWidgetFloatable);
    m_validationDock->setWidget(validationWidget);
    addDockWidget(Qt::BottomDockWidgetArea, m_validationDock);
    m_validationDock->setFloating(true);
    m_validationDock->setWindowFlag(Qt::WindowMinMaxButtonsHint, true);
    m_validationDock->resize(1100, 650);
    m_validationDock->hide();
    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    connect(m_validationTimer, &QTimer::timeout, this, &MainWindow::refreshMobFamilyValidation);
    m_validationWatcher = new QFutureWatcher<CMobFamilyValidationReport>(this);
    connect(m_validationWatcher, &QFutureWatcher<CMobFamilyValidationReport>::finished,
            this, &MainWindow::finishMobFamilyValidation);
    connect(m_validationShowMore, &QPushButton::clicked, this, [this]() {
        m_validationFindingLimit += 250;
        updateMobFamilyValidationPanel();
    });
    connect(m_validationTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& point) {
        QTreeWidgetItem* item = m_validationTree->itemAt(point);
        if(!item) return;
        QMenu menu(this);
        QAction* copyPath = menu.addAction(tr("Copy source path"));
        QAction* copyFinding = menu.addAction(tr("Copy complete entry"));
        menu.addSeparator();
        QAction* selectObject = menu.addAction(tr("Select object in editor"));
        copyPath->setEnabled(!item->data(0, kValidationPathRole).toString().isEmpty());
        copyFinding->setEnabled(!item->data(0, kValidationCopyRole).toString().isEmpty());
        selectObject->setEnabled(item->data(0, kValidationNidRole).isValid());
        QAction* selected = menu.exec(m_validationTree->viewport()->mapToGlobal(point));
        if(selected == copyPath)
            QApplication::clipboard()->setText(item->data(0, kValidationPathRole).toString());
        else if(selected == copyFinding)
            QApplication::clipboard()->setText(item->data(0, kValidationCopyRole).toString());
        else if(selected == selectObject)
            selectValidationObject(item);
    });
    connect(m_validationTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) { selectValidationObject(item); });
    QMenu* viewMenu = new QMenu(tr("View"), this);
    m_ui->menuBar->insertMenu(m_ui->menuSettings->menuAction(), viewMenu);
    viewMenu->addAction(m_mapsDock->toggleViewAction());
    QAction* openValidationPanel = viewMenu->addAction(tr("MOB Family Validation"));
    connect(openValidationPanel, &QAction::triggered,
            this, &MainWindow::showMobFamilyValidation);
    connect(m_mapsBrowser, &CMapsBrowser::openMapRequested,
            this, &MainWindow::openMapPair);
    connect(m_mapsBrowser, &CMapsBrowser::openCollectionRequested,
            this, &MainWindow::openCollection);
    connect(m_mapsBrowser, &CMapsBrowser::refreshRequested,
            this, [this]() { refreshMapsBrowser(); refreshMapTestContext(); });
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

    QMenu* collectionMenu = m_ui->menuFile->addMenu(tr("Collections"));
    QAction* newCollectionAction = collectionMenu->addAction(tr("New Collection..."));
    QAction* openCollectionAction = collectionMenu->addAction(tr("Open Collection..."));
    QAction* editCollectionAction = collectionMenu->addAction(tr("Edit Collection..."));
    collectionMenu->addSeparator();
    QAction* saveCollectionAction = collectionMenu->addAction(tr("Save Collection"));
    QAction* saveCollectionAsAction = collectionMenu->addAction(tr("Save Collection As..."));
    connect(newCollectionAction, &QAction::triggered, this, &MainWindow::newCollection);
    connect(openCollectionAction, &QAction::triggered, this, [this]()
    {
        QDir().mkpath(CCollectionDocument::defaultDirectory());
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Collection"),
            CCollectionDocument::defaultDirectory(), tr("Endurance Collection (*.collection)"));
        if(!path.isEmpty()) openCollection(path);
    });
    connect(editCollectionAction, &QAction::triggered, this, &MainWindow::editCollection);
    connect(saveCollectionAction, &QAction::triggered, this, [this]() { saveCollection(false); });
    connect(saveCollectionAsAction, &QAction::triggered, this, [this]() { saveCollection(true); });

    QAction* openBackupFolder = new QAction(tr("Open Backup Folder"), this);
    QAction* openLogsFolder = new QAction(tr("Open Logs Folder"), this);
    QAction* openTestRunsFolder = new QAction(tr("Open JMV Test Runs Folder"), this);
    QAction* openBugReportingGuide = new QAction(tr("Open Bug Reporting Guide"), this);
    QAction* gameAndModSetup = new QAction(tr("Game and Mod Setup..."), this);
    QAction* compareMobFiles = new QAction(tr("Compare MOB Files..."), this);
    QAction* validateMobFamily = new QAction(tr("Validate Active MOB Family"), this);
    QAction* validateCurrentMob = new QAction(tr("Validate Current MOB Only"), this);
    QAction* validateSelectedMobs = new QAction(tr("Validate Selected MOB Files..."), this);
    QAction* repairMobFamily = new QAction(tr("Review and Repair NID Collisions..."), this);
    m_mapTestAction = new QAction(tr("Test Current Map"), this);
    m_mapTestAction->setToolTip(tr("Stages unique temporary MPR/MOB copies and launches the active validated runtime profile. JMV uses hash-owned temporary aliases in the mod Maps directory; source maps are never overwritten."));
    m_stopMapTestAction = new QAction(tr("Stop Test"), this);
    m_stopMapTestAction->setEnabled(false);
    m_stopMapTestAction->setToolTip(tr("Stops only the game process launched by Endurance."));
    m_ui->menuSettings->insertAction(m_ui->actionOpen_2, gameAndModSetup);
    m_ui->menuSettings->insertSeparator(m_ui->actionOpen_2);
    m_ui->menuTools->addSeparator();
    m_ui->menuTools->addAction(compareMobFiles);
    m_ui->menuTools->addAction(validateMobFamily);
    m_ui->menuTools->addAction(validateCurrentMob);
    m_ui->menuTools->addAction(validateSelectedMobs);
    m_ui->menuTools->addAction(repairMobFamily);
    m_ui->menuTools->addAction(m_mapTestAction);
    m_ui->menuTools->addAction(m_stopMapTestAction);
    m_ui->menuTools->addAction(openBackupFolder);
    m_ui->menuTools->addAction(openLogsFolder);
    m_ui->menuTools->addAction(openTestRunsFolder);
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
    connect(openTestRunsFolder, &QAction::triggered, this, [this]()
    {
        const QString path = CJmvMapTestLauncher::testRunsRootPath();
        QDir().mkpath(path);
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, tr("JMV test runs"),
                tr("Could not open the JMV test-runs folder:\n%1").arg(path));
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
    connect(validateMobFamily, &QAction::triggered, this, &MainWindow::showMobFamilyValidation);
    connect(validateCurrentMob, &QAction::triggered, this, [this]()
    {
        if(!m_pView || !m_pView->currentMob())
        {
            QMessageBox::information(this, tr("MOB validation"), tr("No active MOB is loaded."));
            return;
        }
        SMobFamilyValidationRequest request;
        request.familyLabel = m_pView->currentMob()->filePath().fileName();
        request.profileId = activeResourceProfile().profileId;
        request.explicitComposition = true;
        request.includeProfileWideMobs = false;
        request.mapRoots = activeMapRoots().toList();
        const QString path = m_pView->currentMob()->filePath().absoluteFilePath();
        request.seedMobPaths << path;
        QByteArray data;
        CValidationReport candidateReport;
        if(m_pView->currentMob()->buildValidatedCandidate(&data, &candidateReport, nullptr))
            request.inMemoryMobData.insert(QDir::cleanPath(path).toLower(), data);
        m_familyValidation = CMobFamilyValidator::validate(request);
        updateMobFamilyValidationPanel();
        m_validationDock->show();
        m_validationDock->raise();
        ei::log(m_familyValidation.criticalCount() ? eLogWarning : eLogInfo,
                m_familyValidation.toText());
    });
    connect(validateSelectedMobs, &QAction::triggered, this, [this]()
    {
        const QString start = activeMapRoots().isEmpty() ? QString()
            : activeMapRoots().first();
        const QStringList files = QFileDialog::getOpenFileNames(this,
            tr("Validate Selected MOB Files"), start, tr("MOB files (*.mob)"));
        if(files.isEmpty())
            return;
        SMobFamilyValidationRequest request;
        request.familyLabel = tr("selected files");
        request.profileId = activeResourceProfile().profileId;
        request.explicitComposition = true;
        request.includeProfileWideMobs = false;
        request.seedMobPaths = files;
        request.mapRoots = activeMapRoots().toList();
        m_familyValidation = CMobFamilyValidator::validate(request);
        updateMobFamilyValidationPanel();
        m_validationDock->show();
        m_validationDock->raise();
        ei::log(m_familyValidation.criticalCount() ? eLogWarning : eLogInfo,
                m_familyValidation.toText());
    });
    connect(repairMobFamily, &QAction::triggered, this, &MainWindow::reviewAndRepairNidCollisions);
    connect(m_mapTestAction, &QAction::triggered, this, [this]()
    {
        launchCurrentMapTest();
    });
    connect(m_stopMapTestAction, &QAction::triggered, this, [this]()
    {
        if(m_mapTestLauncher && m_mapTestLauncher->isRunning() &&
           QMessageBox::question(this, tr("Stop JMV test"),
                tr("Stop the game process launched by Endurance?")) == QMessageBox::Yes)
            m_mapTestLauncher->stop();
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
        ei::log(eLogInfo, QString("JMV direct map test started run:%1 staged-map:%2").arg(runId, mapName));
        statusBar()->showMessage(tr("JMV direct map test is waiting for the normal menu lifecycle, then will load the staged map automatically."));
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testFinished, this,
            [this](const QString& runId, int exitCode, const QString& cleanupMessage)
    {
        ei::log(eLogInfo, QString("JMV test launch finished run:%1 exit:%2 cleanup:%3")
                .arg(runId).arg(exitCode).arg(cleanupMessage));
        statusBar()->showMessage(tr("JMV direct map test finished (exit code %1). Temporary files were cleaned up.").arg(exitCode), 8000);
        updateMapTestAction();
        if(m_closeAfterTestStop)
        {
            m_closeAfterTestStop = false;
            QTimer::singleShot(0, this, &MainWindow::close);
        }
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::hotReloadReady, this, [this]()
    {
        statusBar()->showMessage(tr("JMV map test is ready for Ctrl+F5 hot reload."), 6000);
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::hotReloadFinished, this,
            [this](quint64, bool success, const QString& message)
    {
        ei::log(success ? eLogInfo : eLogWarning, message);
        statusBar()->showMessage(message, 10000);
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::testError, this,
            [this](const QString& message)
    {
        ei::log(eLogFatal, message);
        QMessageBox::critical(this, tr("JMV test launch"), message);
        updateMapTestAction();
    });
    connect(m_mapTestLauncher, &CJmvMapTestLauncher::recoveryStatusChanged, this,
            [this](const QString& message, bool pending)
    {
        if(pending)
        {
            statusBar()->showMessage(tr("Waiting for an interrupted Endurance test game to exit; cleanup will run automatically."));
            if(!message.isEmpty())
                ei::log(eLogWarning, message);
        }
        else
        {
            if(!message.isEmpty())
            {
                statusBar()->showMessage(tr("Interrupted test-run cleanup completed."), 8000);
                ei::log(eLogInfo, message);
            }
        }
        updateMapTestAction();
    });
    m_mapTestLauncher->refreshInterruptedRecovery();
    QString interruptedTest;
    if(CJmvMapTestLauncher::hasActiveInterruptedRun(&interruptedTest))
    {
        QTimer::singleShot(0, this, [this, interruptedTest]()
        {
            QMessageBox prompt(QMessageBox::Warning,
                tr("Interrupted map test is still running"),
                tr("Endurance found a verified game process left by an interrupted map-test session.\n\n%1\n\n"
                   "Close it now? Any unsaved progress in that game process will be lost. "
                   "If you leave it running, Endurance will monitor it and clean up automatically after it exits.")
                    .arg(interruptedTest),
                QMessageBox::NoButton, this);
            QPushButton* closeGame = prompt.addButton(tr("Close Game"), QMessageBox::AcceptRole);
            prompt.addButton(tr("Leave Running"), QMessageBox::RejectRole);
            prompt.exec();
            if(prompt.clickedButton() != closeGame)
                return;
            QString error;
            if(!CJmvMapTestLauncher::requestCloseActiveInterruptedRun(&error))
                QMessageBox::warning(this, tr("Interrupted map test"), error);
            else
                statusBar()->showMessage(tr("Close requested. Waiting for game exit and automatic cleanup."));
        });
    }
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
    createViewportToolbars();
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
    // View construction and reparenting can dispatch events immediately. Do
    // not let eventFilter() inspect constructor-owned widgets until every
    // pointer it uses (notably m_undoView) has been initialized.
    m_pView->installEventFilter(this);
    CStatusConnector::getInstance()->attach(m_ui->statusIco, m_ui->statusBar);
    connectUiButtons();
    m_pView->attach(m_settings.get(), m_ui->tableWidget, m_undoStack, m_ui->progressBar, m_ui->mousePosText, m_ui->treeWidget, m_pTileForm.get());
    connect(m_pView, &CView::mobStateChanged, this, [this]() { scheduleMobFamilyValidation(); });
    connect(m_pView, &CView::nidChangeProposed, this, &MainWindow::notifyProposedNid);
    connect(m_pView, &CView::nidAllocationRequested, this,
            [this](const QString& mobPath, quint32* suggestion)
    {
        if(!suggestion)
            return;
        bool familyContainsOwner = false;
        for(const SMobFamilyMember& member : m_familyValidation.members)
            familyContainsOwner = familyContainsOwner || QDir::cleanPath(member.path).compare(
                QDir::cleanPath(mobPath), Qt::CaseInsensitive) == 0;
        if(!familyContainsOwner)
            m_familyValidation = CMobFamilyValidator::validate(mobFamilyValidationRequest());
        quint32 familySuggestion = 0;
        bool shared = false;
        if(m_familyValidation.suggestReplacementNid(mobPath, &familySuggestion, &shared,
                                                     m_sessionAllocatedNids))
        {
            *suggestion = familySuggestion;
            m_sessionAllocatedNids.insert(familySuggestion);
            ei::log(shared ? eLogWarning : eLogInfo,
                QString("Allocated family-aware NID %1 for %2%3")
                    .arg(familySuggestion).arg(QFileInfo(mobPath).fileName())
                    .arg(shared ? " inside a legacy-shared range" : ""));
            const QString foldedMob = QDir::cleanPath(mobPath).toLower();
            if(shared && !m_warnedSharedAllocationMobs.contains(foldedMob))
            {
                m_warnedSharedAllocationMobs.insert(foldedMob);
                QMessageBox::warning(this, tr("Shared MOB ID range"),
                    tr("Every available ID in %1's effective range is also declared by another "
                       "runtime-family MOB. Endurance selected unused NID %2 and will continue, "
                       "but the family ranges should eventually be reviewed.")
                        .arg(QFileInfo(mobPath).fileName()).arg(familySuggestion));
            }
        }
    }, Qt::DirectConnection);
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](int) { scheduleMobFamilyValidation(500); });
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
    refreshMapTestContext();
    updateMapTestAction();
    updateWindowTitle(eTitleTypeData::eTitleTypeDataMpr, QString());
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
    if(m_pView && watched == m_pView && m_suppressSpawnRelease)
    {
        // Placement/marker selection owns the complete pointer sequence. If
        // move events leak through before the matching release, CView can
        // begin a landscape selection rectangle from the same click.
        if(event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->buttons().testFlag(Qt::LeftButton))
                return true;
            m_suppressSpawnRelease = false;
        }
        if(event->type() == QEvent::MouseButtonRelease)
        {
            m_suppressSpawnRelease = false;
            return true;
        }
    }
    if(m_pView && watched == m_pView && m_customSpawn.valid && !m_pickSpawnPending)
    {
        if(event->type() == QEvent::KeyPress && m_customSpawnSelected)
        {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            if(key->key() == Qt::Key_Delete)
            {
                m_playFromHereButton->setChecked(false);
                return true;
            }
        }
        if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->button() == Qt::LeftButton)
            {
                const bool hit = m_pView->testSpawnMarkerAt(mouse->pos());
                m_customSpawnSelected = hit;
                m_pView->setTestSpawnMarkerSelected(hit);
                updateOperationAvailability();
                if(hit)
                {
                    m_suppressSpawnRelease = true;
                    return true;
                }
            }
        }
    }
    if(m_pView && watched == m_pView && m_pickSpawnPending)
    {
        if(event->type() == QEvent::KeyPress)
        {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            if(key->key() == Qt::Key_Escape)
            {
                if(m_movingCustomSpawn)
                {
                    m_pickSpawnPending = false;
                    m_movingCustomSpawn = false;
                    m_customSpawnSelected = true;
                    m_pView->unsetCursor();
                    m_pView->setTestSpawnMarker(m_customSpawnPosition,
                                                m_customSpawn.adjusted);
                    m_pView->setTestSpawnMarkerSelected(true);
                    updateCustomSpawnControls();
                    updateEntranceOverlays();
                    updateOperationAvailability();
                    updateMapTestAction();
                }
                else
                    m_playFromHereButton->setChecked(false);
                return true;
            }
        }
        if(event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            QVector3D preview;
            if(m_pView->terrainPositionAt(mouse->pos(), &preview))
                m_pView->setTestSpawnMarker(preview, false);
            else
                m_pView->clearTestSpawnMarker();
        }
        if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            if(mouse->button() == Qt::LeftButton)
            {
                const SMapTravelZone* entrance = selectedTravelZone();
                if(!entrance)
                {
                    m_playFromHereButton->setChecked(false);
                    return true;
                }
                QVector3D requested;
                if(!m_pView->terrainPositionAt(mouse->pos(), &requested))
                {
                    statusBar()->showMessage(tr("The terrain was not hit at that point. Pick visible terrain."), 5000);
                    return true;
                }
                SMapSpawnPlan plan = CMapSpawnPlanner::chooseCustom(
                    QPointF(requested.x(), requested.y()), entrance->exitId,
                    mapTestObstacles());
                QVector3D resolved(plan.position.x(), plan.position.y(), 0.0f);
                if(!plan.valid || !CLandscape::getInstance()->projectPt(resolved))
                {
                    statusBar()->showMessage(tr("No clear terrain spawn was found near that point. Pick another location."), 5000);
                    return true;
                }
                m_customSpawn = plan;
                m_customSpawnPosition = resolved;
                m_pickSpawnPending = false;
                m_movingCustomSpawn = false;
                m_customSpawnSelected = true;
                m_suppressSpawnRelease = true;
                if(m_playFromHereButton && !m_playFromHereButton->isChecked())
                {
                    const QSignalBlocker blocker(m_playFromHereButton);
                    m_playFromHereButton->setChecked(true);
                }
                m_pView->unsetCursor();
                m_pView->setTestSpawnMarker(resolved, plan.adjusted);
                m_pView->setTestSpawnMarkerSelected(true);
                m_spawnPositionLabel->setText(tr("Custom: %1, %2, %3%4")
                    .arg(resolved.x(), 0, 'f', 2).arg(resolved.y(), 0, 'f', 2)
                    .arg(resolved.z(), 0, 'f', 2)
                    .arg(plan.adjusted ? tr(" (adjusted)") : QString()));
                updateCustomSpawnControls();
                updateEntranceOverlays();
                updateOperationAvailability();
                updateMapTestAction();
                return true;
            }
        }
    }
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

SResourceProfileEvaluation MainWindow::activeResourceProfile() const
{
    COptString* profileId = dynamic_cast<COptString*>(
        m_settings->opt(eOptSetResource, "resourceProfile"));
    for(const SResourceProfile& candidate : loadResourceProfiles())
        if(profileId && candidate.id.compare(profileId->value(), Qt::CaseInsensitive) == 0)
            return evaluateResourceProfile(m_settings->configuredGameRoot(), candidate);
    return SResourceProfileEvaluation();
}

QString MainWindow::activeProfileDisplayName() const
{
    const SResourceProfileEvaluation profile = activeResourceProfile();
    return profile.profileName.isEmpty() ? tr("Not configured") : profile.profileName;
}

QVector<SMapSpawnObstacle> MainWindow::mapTestObstacles() const
{
    QVector<SMapSpawnObstacle> obstacles;
    if(!m_pView || !m_pView->currentMob())
        return obstacles;
    for(CNode* node : m_pView->currentMob()->nodes())
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
        SMapSpawnObstacle obstacle;
        obstacle.position = QPointF(node->position().x(), node->position().y());
        obstacle.radius = box.isInit() ? qMax(0.75, double(box.radius()) * 0.5) : 0.75;
        obstacles.append(obstacle);
    }
    return obstacles;
}

const SMapTravelZone* MainWindow::selectedTravelZone() const
{
    if(!m_entranceCombo)
        return nullptr;
    const int index = m_entranceCombo->currentData().toInt();
    return index >= 0 && index < m_validEntrances.size()
        ? &m_validEntrances[index] : nullptr;
}

void MainWindow::populateEntranceCombo()
{
    if(!m_entranceCombo)
        return;
    const QSignalBlocker blocker(m_entranceCombo);
    m_entranceCombo->clear();
    m_validEntrances.clear();
    if(!CLandscape::getInstance()->isMprLoad())
    {
        m_entranceCombo->addItem(tr("Open an MPR"), -2);
        m_entranceCombo->setEnabled(false);
        return;
    }
    if(m_mapTestEntry.zoneId.isEmpty())
    {
        m_entranceCombo->addItem(tr("Choose map.txt identity..."), -1);
        m_entranceCombo->setEnabled(true);
        return;
    }
    for(const SMapTravelZone& entrance : m_mapTestEntry.travelZones)
    {
        if(entrance.exitId <= 0 || !entrance.hasDeployRectangle
           || entrance.deployRectangle.normalized().isEmpty())
            continue;
        const int dataIndex = m_validEntrances.size();
        m_validEntrances.append(entrance);
        const QPointF center = entrance.spawnPoint();
        const QString destination = entrance.destinationZoneId.isEmpty()
            ? tr("none") : entrance.destinationZoneId;
        m_entranceCombo->addItem(tr("Entrance %1 -> %2:%3 — (%4, %5)")
            .arg(entrance.exitId).arg(destination).arg(entrance.destinationExitId)
            .arg(center.x(), 0, 'f', 1).arg(center.y(), 0, 'f', 1), dataIndex);
    }
    if(m_validEntrances.isEmpty())
    {
        m_entranceCombo->addItem(tr("No valid #deploy entrances"), -2);
        m_entranceCombo->setEnabled(false);
    }
    else
        m_entranceCombo->setEnabled(true);
    updateEntranceOverlays();
}

void MainWindow::updateEntranceOverlays()
{
    if(!m_pView)
        return;
    QVector<QRectF> rectangles;
    QStringList labels;
    for(const SMapTravelZone& entrance : m_validEntrances)
    {
        rectangles.append(entrance.deployRectangle.normalized());
        labels.append(entrance.destinationZoneId.isEmpty()
            ? tr("Entrance %1").arg(entrance.exitId)
            : tr("Entrance %1 -> %2:%3")
                .arg(entrance.exitId)
                .arg(entrance.destinationZoneId)
                .arg(entrance.destinationExitId));
    }
    const int selected = (m_customSpawn.valid && m_customSpawn.custom) ? -1
        : (m_entranceCombo ? m_entranceCombo->currentData().toInt() : -1);
    m_pView->setTestEntranceOverlays(rectangles, labels, selected);
    updateCustomSpawnControls();
}

void MainWindow::refreshMapTestContext()
{
    m_mapTestEntry = SMapCatalogEntry();
    m_borrowedMapIdentity = false;
    m_customSpawn = SMapSpawnPlan();
    m_movingCustomSpawn = false;
    m_customSpawnSelected = false;
    if(m_pView)
        m_pView->clearTestSpawnMarker();
    if(m_playFromHereButton)
    {
        const QSignalBlocker blocker(m_playFromHereButton);
        m_playFromHereButton->setChecked(false);
    }
    if(m_spawnPositionLabel)
        m_spawnPositionLabel->setText(tr("Entrance spawn"));

    QVector<QString> resources;
    QVector<QString> maps;
    if(COptStringList* value = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "resourceRoots")))
        resources = value->value();
    if(COptStringList* value = dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource, "mapRoots")))
        maps = value->value();
    m_mapCatalog = CMapCatalog::load(resources, maps);
    if(CLandscape::getInstance()->isMprLoad())
    {
        const QString loaded = QFileInfo(CLandscape::getInstance()->filePath()).absoluteFilePath();
        QVector<SMapCatalogEntry> nameMatches;
        for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
        {
            if(!m_openedMapZoneId.isEmpty()
               && entry.zoneId.compare(m_openedMapZoneId, Qt::CaseInsensitive) == 0)
            {
                m_mapTestEntry = entry;
                break;
            }
            if(!entry.mprPath.isEmpty()
               && QFileInfo(entry.mprPath).absoluteFilePath().compare(loaded, Qt::CaseInsensitive) == 0)
            {
                m_mapTestEntry = entry;
                break;
            }
            if(entry.mprName.compare(QFileInfo(loaded).completeBaseName(), Qt::CaseInsensitive) == 0)
                nameMatches.append(entry);
        }
        if(m_mapTestEntry.zoneId.isEmpty() && nameMatches.size() == 1)
            m_mapTestEntry = nameMatches.first();
    }
    populateEntranceCombo();
    updateCustomSpawnControls();
    updateMapTestAction();
}

void MainWindow::chooseBorrowedMapIdentity()
{
    QStringList labels;
    QVector<SMapCatalogEntry> choices;
    for(const SMapCatalogEntry& entry : m_mapCatalog.entries)
    {
        bool valid = false;
        for(const SMapTravelZone& zone : entry.travelZones)
            valid = valid || (zone.exitId > 0 && zone.hasDeployRectangle
                              && !zone.deployRectangle.normalized().isEmpty());
        if(valid)
        {
            choices.append(entry);
            labels.append(QString("%1 — %2 / %3").arg(entry.zoneId, entry.mprName, entry.mobName));
        }
    }
    bool accepted = false;
    const QString selected = QInputDialog::getItem(this, tr("Borrow map.txt identity"),
        tr("This gives the test map another zone's minimap, quest, travel and script identity. It is remembered only while the current MPR is loaded."),
        labels, 0, true, &accepted);
    const int index = labels.indexOf(selected);
    if(!accepted || index < 0)
    {
        populateEntranceCombo();
        return;
    }
    m_mapTestEntry = choices[index];
    m_borrowedMapIdentity = true;
    ei::log(eLogWarning, QString("Borrowed map.txt identity zone:%1 for unregistered MPR:%2")
        .arg(m_mapTestEntry.zoneId, CLandscape::getInstance()->filePath().absoluteFilePath()));
    populateEntranceCombo();
    m_entranceCombo->setToolTip(tr("Borrowed map identity: %1. Runtime metadata and scripts belong to that registered zone.")
        .arg(m_mapTestEntry.zoneId));
    updateMapTestAction();
}

void MainWindow::setCustomSpawnPicking(bool enabled)
{
    if(!m_pView || !enabled)
    {
        m_pickSpawnPending = false;
        m_movingCustomSpawn = false;
        m_customSpawnSelected = false;
        m_customSpawn = SMapSpawnPlan();
        if(m_pView)
        {
            m_pView->unsetCursor();
            m_pView->clearTestSpawnMarker();
        }
        if(m_spawnPositionLabel)
            m_spawnPositionLabel->setText(tr("Entrance spawn"));
        updateCustomSpawnControls();
        updateEntranceOverlays();
        updateOperationAvailability();
        updateMapTestAction();
        return;
    }
    if(!selectedTravelZone())
    {
        QSignalBlocker blocker(m_playFromHereButton);
        m_playFromHereButton->setChecked(false);
        statusBar()->showMessage(tr("Select a valid map.txt entrance before choosing a custom spawn."), 5000);
        return;
    }
    m_pView->cancelPendingOperation();
    m_pickSpawnPending = true;
    m_movingCustomSpawn = false;
    m_customSpawnSelected = false;
    m_customSpawn = SMapSpawnPlan();
    m_pView->setCursor(Qt::CrossCursor);
    m_pView->setFocus();
    m_spawnPositionLabel->setText(tr("Click terrain (Esc cancels)"));
    updateCustomSpawnControls();
    updateEntranceOverlays();
    updateOperationAvailability();
    updateMapTestAction();
}

void MainWindow::updateCustomSpawnControls()
{
    const bool customActive = m_customSpawn.valid && m_customSpawn.custom;
    if(m_playFromHereButton)
        m_playFromHereButton->setText(customActive && !m_pickSpawnPending ? tr("Clear entrance")
                                                   : (m_pickSpawnPending ? (m_movingCustomSpawn
                                                       ? tr("Cancel Move Entrance") : tr("Cancel Play from Here"))
                                                                         : tr("Play from Here")));
    if(m_entranceCombo && (!m_mapTestLauncher || !m_mapTestLauncher->isRunning()))
        m_entranceCombo->setEnabled(!customActive && !m_pickSpawnPending
            && CLandscape::getInstance()->isMprLoad()
            && (!m_validEntrances.isEmpty() || m_entranceCombo->findData(-1) >= 0));
    if(m_gotoEntranceButton)
        m_gotoEntranceButton->setEnabled(customActive || selectedTravelZone());
}

void MainWindow::moveCameraToActiveSpawn()
{
    if(!m_pView)
        return;
    if(m_customSpawn.valid && m_customSpawn.custom)
    {
        m_pView->moveCameraToPoint(m_customSpawnPosition);
        return;
    }
    const SMapTravelZone* entrance = selectedTravelZone();
    if(!entrance)
        return;
    const QPointF center = entrance->spawnPoint();
    m_pView->moveCameraToPoint(QVector3D(center.x(), center.y(), 0.0f));
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
    m_stopMapTestAction->setShortcut(shortcut("shortcutStopMapTest", "Shift+Esc"));
    const struct { EButtonOp type; const char* option; const char* fallback; const char* label; } operations[] = {
        {EButtonOpSelect, "shortcutSelectTool", "Esc", "Select / cancel tool"},
        {EButtonOpMove, "shortcutMoveTool", "G", "Move selected objects"},
        {EButtonOpRotate, "shortcutRotateTool", "R", "Rotate selected objects"},
        {EButtonOpScale, "shortcutScaleTool", "T", "Scale selected objects"},
        {EButtonOpTilebrush, "shortcutTileBrushTool", "B", "Terrain tile brush"}
    };
    for(const auto& operation : operations)
    {
        QAction* action = m_operationActions.value(operation.type, nullptr);
        if(!action)
            continue;
        action->setShortcut(shortcut(operation.option, operation.fallback));
        action->setShortcutContext(Qt::ApplicationShortcut);
        const QString tip = tr(operation.label) + " (" + action->shortcut().toString(QKeySequence::NativeText) + ")";
        action->setToolTip(tip);
    }
    if(m_ui->selectButton) m_ui->selectButton->setToolTip(m_operationActions.value(EButtonOpSelect)->toolTip());
    if(m_ui->moveButton) m_ui->moveButton->setToolTip(m_operationActions.value(EButtonOpMove)->toolTip());
    if(m_ui->rotateButton) m_ui->rotateButton->setToolTip(m_operationActions.value(EButtonOpRotate)->toolTip());
    if(m_ui->scaleButton) m_ui->scaleButton->setToolTip(m_operationActions.value(EButtonOpScale)->toolTip());
    if(m_ui->tileBrushButton) m_ui->tileBrushButton->setToolTip(m_operationActions.value(EButtonOpTilebrush)->toolTip());
}

void MainWindow::createViewportToolbars()
{
    QWidget* testBar = new QWidget(m_ui->workspaceContainer);
    testBar->setObjectName("mapTestToolbar");
    QHBoxLayout* testLayout = new QHBoxLayout(testBar);
    testLayout->setContentsMargins(4, 3, 4, 3);
    testLayout->setSpacing(5);
    QToolButton* testButton = new QToolButton(testBar);
    testButton->setDefaultAction(m_mapTestAction);
    testButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QToolButton* stopButton = new QToolButton(testBar);
    stopButton->setDefaultAction(m_stopMapTestAction);
    stopButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_entranceCombo = new QComboBox(testBar);
    m_entranceCombo->setMinimumContentsLength(28);
    m_entranceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_entranceCombo->setToolTip(tr("Selects the map.txt entrance used to initialize the runtime session."));
    m_playFromHereButton = new QToolButton(testBar);
    m_playFromHereButton->setText(tr("Play from Here"));
    m_playFromHereButton->setCheckable(true);
    m_playFromHereButton->setToolTip(tr("Pick a custom terrain spawn. The selected entrance still initializes zone metadata."));
    m_gotoEntranceButton = new QToolButton(testBar);
    m_gotoEntranceButton->setText(tr("Go to Entrance"));
    m_gotoEntranceButton->setToolTip(tr("Centers the editor camera on the selected map.txt deploy area."));
    m_godmodeCheckBox = new QCheckBox(tr("Godmode"), testBar);
    m_godmodeCheckBox->setToolTip(tr("For this JMV test process, invoke GodMode(0,3) through a run-local lossless copy of SpellAddon's autorun MOB. The installed MOB is never changed."));
    if(COptBool* option = dynamic_cast<COptBool*>(m_settings->opt(eOptSetGeneral, "mapTestGodmode")))
        m_godmodeCheckBox->setChecked(option->value());
    m_spawnPositionLabel = new QLabel(tr("Entrance spawn"), testBar);
    m_spawnPositionLabel->setMinimumWidth(150);
    testLayout->addWidget(testButton);
    testLayout->addWidget(stopButton);
    testLayout->addSpacing(8);
    testLayout->addWidget(new QLabel(tr("Entrance:"), testBar));
    testLayout->addWidget(m_entranceCombo, 1);
    testLayout->addWidget(m_gotoEntranceButton);
    testLayout->addWidget(m_playFromHereButton);
    testLayout->addWidget(m_spawnPositionLabel);
    testLayout->addWidget(m_godmodeCheckBox);

    QWidget* operationBar = new QWidget(m_ui->workspaceContainer);
    operationBar->setObjectName("viewportOperationToolbar");
    QHBoxLayout* operationLayout = new QHBoxLayout(operationBar);
    operationLayout->setContentsMargins(4, 2, 4, 2);
    operationLayout->setSpacing(4);
    const struct { EButtonOp type; QToolButton* button; const char* text; } operations[] = {
        {EButtonOpSelect, m_ui->selectButton, "Select"},
        {EButtonOpMove, m_ui->moveButton, "Move"},
        {EButtonOpRotate, m_ui->rotateButton, "Rotate"},
        {EButtonOpScale, m_ui->scaleButton, "Scale"},
        {EButtonOpTilebrush, m_ui->tileBrushButton, "Tile Brush"}
    };
    QActionGroup* actionGroup = new QActionGroup(this);
    actionGroup->setExclusive(true);
    for(const auto& operation : operations)
    {
        QAction* action = new QAction(tr(operation.text), this);
        action->setCheckable(true);
        actionGroup->addAction(action);
        addAction(action);
        m_operationActions.insert(operation.type, action);
        operation.button->setCheckable(true);
        operation.button->setAutoRaise(true);
        operation.button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        operation.button->setText(tr(operation.text));
        operationLayout->addWidget(operation.button);
        connect(action, &QAction::triggered, this, [this, operation]()
        {
            if(!m_pView)
                return;
            if(operation.type == EButtonOpMove && m_customSpawnSelected
               && m_customSpawn.valid && m_customSpawn.custom)
            {
                m_pView->cancelPendingOperation();
                m_pickSpawnPending = true;
                m_movingCustomSpawn = true;
                m_pView->setCursor(Qt::CrossCursor);
                m_pView->setFocus();
                m_spawnPositionLabel->setText(tr("Click terrain to move entrance (Esc cancels)"));
                updateCustomSpawnControls();
                updateEntranceOverlays();
                updateMapTestAction();
                return;
            }
            if(operation.type == EButtonOpSelect)
            {
                if(m_pickSpawnPending && m_playFromHereButton)
                {
                    m_playFromHereButton->setChecked(false);
                    return;
                }
                if(!m_pView->cancelPendingOperation())
                    m_pView->clearObjectSelection();
                CButtonConnector::getInstance()->pressButton(EButtonOpSelect);
            }
            else
                CButtonConnector::getInstance()->clickButton(operation.type);
            m_pView->setFocus();
        });
    }
    operationLayout->addStretch(1);
    m_ui->toolsLayout->setContentsMargins(0, 0, 0, 0);
    m_ui->toolsLayout->setSpacing(0);

    m_ui->workSpaceLayout->addWidget(testBar);
    m_ui->workSpaceLayout->addWidget(m_pView, 1);
    m_ui->workSpaceLayout->addWidget(operationBar);

    connect(m_entranceCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index)
    {
        if(m_entranceCombo->itemData(index).toInt() == -1)
            chooseBorrowedMapIdentity();
        if(m_playFromHereButton->isChecked())
            m_playFromHereButton->setChecked(false);
        updateEntranceOverlays();
        updateMapTestAction();
    });
    connect(m_gotoEntranceButton, &QToolButton::clicked, this, [this]()
    {
        moveCameraToActiveSpawn();
    });
    connect(m_playFromHereButton, &QToolButton::toggled,
            this, &MainWindow::setCustomSpawnPicking);
    connect(m_godmodeCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        if(COptBool* option = dynamic_cast<COptBool*>(m_settings->opt(eOptSetGeneral, "mapTestGodmode")))
        {
            option->setValue(checked);
            m_settings->persistOptions();
        }
    });
}

void MainWindow::updateMapTestAction()
{
    if(!m_mapTestAction || !m_stopMapTestAction)
        return;
    const SResourceProfileEvaluation profile = activeResourceProfile();
    const bool isJmv = profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) == 0;
    const bool isBaseProbe = profile.profileId.compare("base-game", Qt::CaseInsensitive) == 0;
    const bool hasMobs = m_pView && m_pView->hasActiveMob()
            && !m_pView->mobs().isEmpty() && CLandscape::getInstance()->isMprLoad();
    const bool hasPair = hasMobs && (isJmv || m_pView->mobs().size() == 1);
    const bool hasEntrance = selectedTravelZone() != nullptr;
    const bool customRequested = (m_customSpawn.valid && m_customSpawn.custom)
        || (m_playFromHereButton && m_playFromHereButton->isChecked());
    const bool customReady = !customRequested
        || (isJmv && m_customSpawn.valid && !m_pickSpawnPending);
    const bool profileReady = (isJmv || isBaseProbe) && profile.valid
        && profile.runtimeStackValidated;
    if(m_mapTestLauncher && m_mapTestLauncher->isRunning())
    {
        const bool canConfigureReload = isJmv && profileReady && hasPair && hasEntrance;
        const bool runtimeReady = m_mapTestLauncher->isHotReloadReady();
        const bool pendingReload = m_mapTestLauncher->isHotReloadPending();
        const QString runtimeState = m_mapTestLauncher->runtimeState();
        const quint64 saveGeneration = m_mapTestLauncher->saveGeneration();
        const bool canReplaceDeferred = pendingReload
            && (runtimeState == "Transitioning" || runtimeState == "Initializing"
                || runtimeState == "Unknown");
        const bool canHotReload = canConfigureReload && customReady
            && runtimeReady && (!pendingReload || canReplaceDeferred);
        m_mapTestAction->setText(tr("Hot Reload Map"));
        m_mapTestAction->setEnabled(canHotReload);
        m_stopMapTestAction->setEnabled(true);
        if(m_entranceCombo)
            m_entranceCombo->setEnabled(canConfigureReload && !customRequested);
        if(m_gotoEntranceButton) m_gotoEntranceButton->setEnabled(canConfigureReload);
        if(m_playFromHereButton) m_playFromHereButton->setEnabled(canConfigureReload);
        if(m_godmodeCheckBox) m_godmodeCheckBox->setEnabled(canConfigureReload);
        m_mapTestAction->setToolTip(canHotReload
            ? tr("Restage the exact editor pair and reload it in the Endurance-owned JMV process (Ctrl+F5).\nRuntime state: %1\nSave generation: %2")
                .arg(runtimeState).arg(saveGeneration)
            : (pendingReload && !canReplaceDeferred
                ? tr("Waiting for the current hot reload to complete.")
                : (!runtimeReady
                ? tr("Waiting for the initial staged map to report hot-reload readiness.")
                : tr("Hot reload requires a validated JMV profile, one MPR with its MOB set, and a valid entrance."))));
        return;
    }
    m_mapTestAction->setText(tr("Test Current Map"));
    m_stopMapTestAction->setEnabled(false);
    QString interruptedRun;
    const bool recoveryPending = (m_mapTestLauncher && m_mapTestLauncher->isRecoveryPending())
        || CJmvMapTestLauncher::hasActiveInterruptedRun(&interruptedRun);
    // JMV applies a placed custom entrance through its process-owned autorun
    // provider. Base keeps the picker unavailable until it has an equivalent
    // exact-hash runtime position provider.
    m_mapTestAction->setEnabled(!recoveryPending && profileReady && hasPair
                                && hasEntrance && customReady);
    if(m_entranceCombo)
        m_entranceCombo->setEnabled(hasPair && !customRequested &&
            (!m_validEntrances.isEmpty() || m_entranceCombo->findData(-1) >= 0));
    if(m_gotoEntranceButton)
        m_gotoEntranceButton->setEnabled(hasPair && hasEntrance);
    if(m_playFromHereButton) m_playFromHereButton->setEnabled(isJmv && hasPair && hasEntrance);
    if(m_godmodeCheckBox) m_godmodeCheckBox->setEnabled(isJmv && hasPair);
    if(recoveryPending)
        m_mapTestAction->setToolTip(interruptedRun.isEmpty()
            ? tr("Endurance is waiting for an interrupted test game to finish exiting and for hash-owned cleanup to complete.")
            : tr("An orphaned Endurance test game is still running. Exit it normally; cleanup will run automatically before another test.\n%1")
                .arg(interruptedRun));
    else if(!isJmv && !isBaseProbe)
        m_mapTestAction->setToolTip(tr("The active profile has no Endurance direct-map-test provider."));
    else if(!profileReady)
        m_mapTestAction->setToolTip(tr("The active runtime does not match an exact experimental manifest."));
    else if(!hasPair)
        m_mapTestAction->setToolTip(isJmv
            ? tr("Load one MPR and at least one MOB before launching a map test.")
            : tr("The Base probe requires exactly one MPR/MOB pair."));
    else if(!hasEntrance)
        m_mapTestAction->setToolTip(tr("Select a valid map.txt entrance or explicitly borrow a registered map identity."));
    else if(!customReady)
        m_mapTestAction->setToolTip(tr("Finish placing the JMV custom entrance before launching the map test."));
    else
        m_mapTestAction->setToolTip(isBaseProbe
            ? tr("Runs the exact-hash Base EI direct-launch feasibility probe (Ctrl+F5).")
            : tr("Stages the current pair and launches it through JMV's normal single-player New Game path (Ctrl+F5)."));
}

void MainWindow::launchCurrentMapTest()
{
    activateWindow();
    raise();
    const bool hotReloadRequested = m_mapTestLauncher && m_mapTestLauncher->isRunning();
    if(!m_pView || !m_pView->hasActiveMob() || m_pView->mobs().isEmpty()
       || !CLandscape::getInstance()->isMprLoad())
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("Load one MPR and at least one MOB before starting a test."));
        return;
    }
    notifyFamilyPreflight(hotReloadRequested ? tr("Hot Reload Map") : tr("Test Current Map"));
    QProgressDialog progress(hotReloadRequested ? tr("Preparing hot reload...")
                                               : tr("Preparing map test..."),
                             QString(), 0, 6, this);
    progress.setWindowTitle(hotReloadRequested ? tr("JMV hot reload")
                                               : tr("Map test launch"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();
    if(m_pView->cancelPendingOperation())
        statusBar()->showMessage(tr("Pending object movement was cancelled before staging the test."), 4000);
    CLandscape* landscape = CLandscape::getInstance();
    if(landscape->isDirty())
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("Unsaved MPR edits cannot yet be staged losslessly. Save or revert the landscape first."));
        return;
    }
    progress.setLabelText(tr("Validating the active runtime profile..."));
    progress.setValue(1);
    QApplication::processEvents();

    const SResourceProfileEvaluation profile = activeResourceProfile();
    const bool isJmv = profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) == 0;
    const bool isBase = profile.profileId.compare("base-game", Qt::CaseInsensitive) == 0;
    if(hotReloadRequested && !isJmv)
    {
        QMessageBox::warning(this, tr("Hot reload"),
            tr("Hot reload is currently available only for an Endurance-owned JMV test process."));
        return;
    }
    if((!isJmv && !isBase) || !profile.valid || !profile.runtimeStackValidated)
    {
        QMessageBox::warning(this, tr("Map test launch"),
            tr("The active installation does not match an exact supported or experimental runtime manifest. Run Game and Mod Setup again and review its diagnostics."));
        return;
    }
    progress.setLabelText(tr("Reading map-table metadata..."));
    progress.setValue(2);
    QApplication::processEvents();

    const SMapCatalogEntry entry = m_mapTestEntry;
    const SMapTravelZone* entrance = selectedTravelZone();
    if(entry.zoneId.isEmpty() || !entrance)
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("No travel/deploy zone was found for the loaded MPR in the active map table. Endurance will not guess a player spawn."));
        return;
    }
    progress.setLabelText(tr("Choosing a safe map-table entrance..."));
    progress.setValue(3);
    QApplication::processEvents();

    CMob* mob = nullptr;
    const QString expectedMain = entry.mobName;
    for(CMob* candidate : m_pView->mobs())
    {
        if(candidate && (QFileInfo(candidate->filePath()).completeBaseName().compare(
                             expectedMain, Qt::CaseInsensitive) == 0
                          || QFileInfo(candidate->filePath()).fileName().compare(
                              expectedMain, Qt::CaseInsensitive) == 0))
        {
            mob = candidate;
            break;
        }
    }
    if(!mob && !m_pView->mobs().isEmpty())
        mob = m_pView->mobs().first();
    if(isBase && m_pView->mobs().size() != 1)
    {
        QMessageBox::warning(this, tr("Base EI feasibility probe"),
            tr("The Base probe currently accepts exactly one MOB. Supplemental MOB redirection is JMV-only."));
        return;
    }
    const SMapSpawnPlan spawn = m_customSpawn.valid && m_customSpawn.custom
        ? m_customSpawn
        : CMapSpawnPlanner::chooseEntrance(*entrance, mapTestObstacles());
    if(!spawn.valid)
    {
        QMessageBox::warning(this, tr("JMV test launch"),
            tr("No deploy rectangle was found in the selected zone's map-table entrances. Endurance will not invent a player entrance.\n\n%1").arg(spawn.reason));
        return;
    }
    progress.setLabelText(tr("Validating the current in-memory map pair..."));
    progress.setValue(4);
    QApplication::processEvents();

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
    QVector<SJmvSupplementalMobInput> supplementalMobs;
    for(CMob* supplemental : m_pView->mobs())
    {
        if(!supplemental || supplemental == mob)
            continue;
        QByteArray data;
        CValidationReport supplementalReport;
        QString supplementalSaveClass;
        if(!supplemental->buildValidatedCandidate(
               &data, &supplementalReport, &supplementalSaveClass))
        {
            QMessageBox::warning(this, tr("JMV test launch"),
                tr("Supplemental MOB '%1' could not be staged safely:\n\n%2")
                    .arg(supplemental->filePath().fileName(), supplementalReport.toText()));
            return;
        }
        SJmvSupplementalMobInput staged;
        staged.sourceMob = supplemental->filePath();
        staged.mobData = data;
        supplementalMobs.append(staged);
    }

    const QString entranceAssessment = spawn.unobstructed
        ? tr("its checked clear point is (%1, %2)")
              .arg(spawn.position.x(), 0, 'f', 2).arg(spawn.position.y(), 0, 'f', 2)
        : tr("the editor could not prove that its center is clear, so the game will perform the final deployment");
    const QString confirmation = isJmv ? tr(
        "This experimental JMV-only test stages the exact current MPR/MOB pair, including unsaved in-memory edits, under a unique temporary name. After JMV completes its normal menu load, Endurance invokes the fingerprinted single-player New Game path on the game thread and supplies the staged pair. No console/server command or fixed delay is used.\n\n"
        "JMV initializes one normal player through map-table zone %1 and hand-placed entrance %2; %3.\n\n"
        "Source maps, JMV configuration, and live campaign saves are not changed. Complete payloads remain under Endurance AppData; the proven JMV loader also uses one randomly named, hash-owned MPR/MOB alias in the mod Maps directory and removes it on guarded cleanup. Every save/load path is redirected into a persistent profile-specific AppData folder shared by Endurance test runs. Intro movies are hidden only for the test process. Returning to the menu or travelling away may use original installed maps; use Stop Test when finished.\n\nStart the direct JMV map test?")
        .arg(entry.zoneId).arg(spawn.exitId).arg(entranceAssessment)
        : tr("This exact-hash Base EI feasibility probe uses an editor-owned 32-bit host. It creates Base game.exe suspended, loads the run-local bridge only into that owned process, calls its explicit initializer outside DllMain, and resumes only after fingerprint and byte guards pass.\n\nRun the probe levels in order. Source maps and the Base game installation are not modified.\n\nContinue to probe-level selection?");
    constexpr int kMapTestWarningVersion = 4;
    COptInt* warningVersion = dynamic_cast<COptInt*>(
                m_settings->opt(eOptSetGeneral, "mapTestWarningVersion"));
    if(!hotReloadRequested
       && (!warningVersion || warningVersion->value() < kMapTestWarningVersion))
    {
        progress.hide();
        QMessageBox confirmationBox(QMessageBox::Question,
            isJmv ? tr("Direct JMV map test (experimental)")
                  : tr("Base EI feasibility probe (experimental)"), confirmation,
            QMessageBox::Yes | QMessageBox::Cancel, this);
        QCheckBox* dontShowAgain = new QCheckBox(tr("Don't show this warning again"), &confirmationBox);
        confirmationBox.setCheckBox(dontShowAgain);
        if(confirmationBox.exec() != QMessageBox::Yes)
            return;
        if(warningVersion && dontShowAgain->isChecked())
        {
            warningVersion->setValue(kMapTestWarningVersion);
            m_settings->persistOptions();
        }
        progress.show();
    }
    progress.setLabelText(tr("Staging temporary maps and isolated test state..."));
    progress.setValue(5);
    QApplication::processEvents();

    SJmvMapTestLaunchInput input;
    input.profile = profile;
    input.sourceMpr = landscape->filePath();
    input.sourceMob = mob->filePath();
    input.mprData = mprFile.readAll();
    input.mobData = mobData;
    input.supplementalMobs = supplementalMobs;
    input.embeddedScript = mob->script();
    input.mapEntry = entry;
    input.spawn = spawn;
    QVector3D runtimeSpawn(spawn.position.x(), spawn.position.y(), 0.0f);
    if(!CLandscape::getInstance()->projectPt(runtimeSpawn))
    {
        QMessageBox::warning(this, tr("Map test launch"),
            tr("The selected entrance could not be projected onto the loaded terrain."));
        return;
    }
    input.spawnZ = runtimeSpawn.z();
    input.godmode = isJmv && m_godmodeCheckBox && m_godmodeCheckBox->isChecked();
    input.borrowedMapIdentity = m_borrowedMapIdentity;
    if(isBase && !hotReloadRequested)
    {
        const QStringList levels = {
            tr("1 - Observation only (menu and normal exit)"),
            tr("2 - Pass-through hooks (start Base New Game manually)"),
            tr("3 - Byte-identical staged redirect"),
            tr("4 - Current editor-state redirect")
        };
        bool accepted = false;
        const QString selected = QInputDialog::getItem(this,
            tr("Base EI feasibility probe"),
            tr("Run probe levels in order. A failure leaves Base direct launch unsupported."),
            levels, 0, false, &accepted);
        if(!accepted)
            return;
        input.baseProbeLevel = levels.indexOf(selected) + 1;
    }
    QString error;
    const bool accepted = hotReloadRequested
        ? m_mapTestLauncher->hotReload(input, &error)
        : m_mapTestLauncher->launch(input, &error);
    if(!accepted)
    {
        ei::log(eLogWarning, QString("Map test %1 refused: %2")
            .arg(hotReloadRequested ? "hot reload" : "launch", error));
        QMessageBox::critical(this,
            hotReloadRequested ? tr("Hot reload") : tr("Map test launch"), error);
    }
    else if(hotReloadRequested)
        statusBar()->showMessage(tr("Hot reload queued on the verified JMV game thread."), 6000);
    progress.setValue(6);
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
    QDir().mkpath(CCollectionDocument::defaultDirectory());
    m_mapsBrowser->refresh(resources, maps, CCollectionDocument::defaultDirectory());
}

QVector<QString> MainWindow::activeMapRoots() const
{
    if(COptStringList* option = dynamic_cast<COptStringList*>(
           m_settings->opt(eOptSetResource, "mapRoots")))
        return option->value();
    return QVector<QString>();
}

SMobFamilyValidationRequest MainWindow::mobFamilyValidationRequest()
{
    SMobFamilyValidationRequest request;
    const SResourceProfileEvaluation profile = activeResourceProfile();
    request.profileId = profile.profileId;
    request.mapRoots = activeMapRoots().toList();
    request.explicitComposition = !m_currentCollectionPath.isEmpty();
    if(!request.explicitComposition && !m_mapTestEntry.mobPath.isEmpty())
        request.mappedPrimaryMobPath = m_mapTestEntry.mobPath;
    request.familyLabel = !m_mapTestEntry.zoneId.isEmpty()
        ? m_mapTestEntry.zoneId
        : (!m_currentCollectionPath.isEmpty()
            ? QFileInfo(m_currentCollectionPath).completeBaseName()
            : QString("standalone"));
    if(!m_pView)
        return request;
    if(m_pView->currentMob())
        request.preferredMobPath = m_pView->currentMob()->filePath().absoluteFilePath();
    for(CMob* mob : m_pView->mobs())
    {
        if(!mob)
            continue;
        const QString path = mob->filePath().absoluteFilePath();
        request.seedMobPaths.append(path);
        const QString workspaceRoot = QFileInfo(path).absolutePath();
        if(!request.workspaceRoots.contains(workspaceRoot, Qt::CaseInsensitive))
            request.workspaceRoots.prepend(workspaceRoot);
        QByteArray data;
        CValidationReport candidateReport;
        QString saveClass;
        if(mob->buildValidatedCandidate(&data, &candidateReport, &saveClass))
            request.inMemoryMobData.insert(QDir::cleanPath(path).toLower(), data);
        else
            ei::log(eLogWarning, QString("Family validation used the on-disk MOB because the current in-memory candidate could not be mapped safely: %1\n%2")
                .arg(path, candidateReport.toText()));
    }
    request.seedMobPaths.removeDuplicates();
    return request;
}

void MainWindow::scheduleMobFamilyValidation(int delayMs)
{
    if(m_validationTimer)
        m_validationTimer->start(qMax(0, delayMs));
}

void MainWindow::refreshMobFamilyValidation()
{
    if(!m_pView || m_pView->mobs().isEmpty())
    {
        ++m_validationGeneration;
        m_hasPendingValidation = false;
        m_familyValidation = CMobFamilyValidationReport();
        m_acknowledgedFindingDigest.clear();
        m_previousFamilyRiskKeys.clear();
        m_sessionAllocatedNids.clear();
        m_warnedSharedAllocationMobs.clear();
        m_familyRiskBaselineReady = false;
        if(m_ui && m_ui->treeWidget)
            m_ui->treeWidget->setValidationNids(QSet<uint>(), QSet<uint>());
        updateMobFamilyValidationPanel();
        return;
    }
    m_pendingValidationRequest = mobFamilyValidationRequest();
    m_hasPendingValidation = true;
    ++m_validationGeneration;
    m_validationSummary->setText(tr("Validating runtime MOB family…"));
    startPendingMobFamilyValidation();
}

void MainWindow::startPendingMobFamilyValidation()
{
    if(!m_validationWatcher || m_validationWatcher->isRunning() || !m_hasPendingValidation)
        return;
    const SMobFamilyValidationRequest request = m_pendingValidationRequest;
    m_hasPendingValidation = false;
    m_runningValidationGeneration = m_validationGeneration;
    m_validationWatcher->setFuture(QtConcurrent::run([request]() {
        return CMobFamilyValidator::validate(request);
    }));
}

void MainWindow::finishMobFamilyValidation()
{
    const CMobFamilyValidationReport result = m_validationWatcher->result();
    if(m_runningValidationGeneration == m_validationGeneration)
        applyMobFamilyValidationReport(result);
    startPendingMobFamilyValidation();
}

void MainWindow::applyMobFamilyValidationReport(const CMobFamilyValidationReport& report)
{
    const QByteArray previousDigest = m_familyValidation.findingDigest();
    m_familyValidation = report;
    m_validationFindingLimit = 250;
    const QByteArray currentDigest = m_familyValidation.findingDigest();
    if(previousDigest != currentDigest)
        m_acknowledgedFindingDigest.clear();
    QSet<QString> currentRisks;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
    {
        if(finding.code != "nid.collision" && finding.code != "range.legacy_overlap")
            continue;
        QStringList locations = finding.locations;
        locations.sort(Qt::CaseInsensitive);
        currentRisks.insert(QString("%1|%2|%3")
            .arg(finding.code).arg(finding.nid).arg(locations.join('|')));
    }
    if(m_familyRiskBaselineReady)
    {
        QSet<QString> introduced = currentRisks;
        introduced.subtract(m_previousFamilyRiskKeys);
        if(!introduced.isEmpty())
        {
            QVector<SMobFamilyFinding> introducedFindings;
            for(const SMobFamilyFinding& finding : m_familyValidation.findings)
            {
                QStringList locations = finding.locations;
                locations.sort(Qt::CaseInsensitive);
                const QString key = QString("%1|%2|%3")
                    .arg(finding.code).arg(finding.nid).arg(locations.join('|'));
                if(introduced.contains(key))
                    introducedFindings.append(finding);
            }
            ei::log(eLogWarning, QString("Editing introduced %1 new runtime-family NID/range risk(s)")
                .arg(introduced.size()));
            showValidationWarning(tr("New family ID risk"),
                                  validationWarningPreview(introducedFindings));
        }
    }
    m_previousFamilyRiskKeys = currentRisks;
    m_familyRiskBaselineReady = true;
    QSet<uint> criticalNids;
    QSet<uint> warningNids;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
    {
        if(!finding.hasNid)
            continue;
        if(finding.severity == EMobFamilyFindingSeverity::Critical)
            criticalNids.insert(finding.nid);
        else if(finding.severity == EMobFamilyFindingSeverity::Warning
                || finding.severity == EMobFamilyFindingSeverity::Incomplete)
            warningNids.insert(finding.nid);
    }
    m_ui->treeWidget->setValidationNids(criticalNids, warningNids);
    updateMobFamilyValidationPanel();
    ei::log(m_familyValidation.criticalCount() > 0 ? eLogWarning : eLogInfo,
            m_familyValidation.toText());
}

void MainWindow::updateMobFamilyValidationPanel()
{
    if(!m_validationTree || !m_validationSummary)
        return;
    m_validationTree->clear();
    if(m_validationShowMore)
        m_validationShowMore->hide();
    if(m_familyValidation.members.isEmpty())
    {
        m_validationSummary->setText(tr("No active MOB family."));
        return;
    }
    m_validationSummary->setText(m_familyValidation.summary());
    QTreeWidgetItem* membersRoot = new QTreeWidgetItem(m_validationTree,
        QStringList() << tr("INFO") << tr("Family members")
                      << QString() << tr("%1 MOB(s)").arg(m_familyValidation.members.size()));
    for(const SMobFamilyMember& member : m_familyValidation.members)
    {
        const QString status = member.missing ? tr("missing")
            : (!member.error.isEmpty() ? tr("unreadable")
                                       : (member.inMemory ? tr("in-memory") : tr("disk")));
        QTreeWidgetItem* item = new QTreeWidgetItem(membersRoot,
            QStringList() << status.toUpper() << member.fileName << member.path
                          << QString("%1; %2").arg(member.sourceRole, member.inclusionReason));
        item->setData(0, kValidationPathRole, member.path);
        item->setData(0, kValidationCopyRole,
                      QString("%1\nSource: %2\nRole: %3\nReason: %4")
                          .arg(member.fileName, member.path, member.sourceRole,
                               member.inclusionReason));
        item->setToolTip(2, member.path + (member.requestedBy.isEmpty()
            ? QString() : QString("\nRequested by: %1").arg(member.requestedBy))
            + (member.sourceSha256.isEmpty() ? QString()
               : QString("\nSHA-256: %1").arg(QString::fromLatin1(member.sourceSha256.toHex()))));
        for(const QString& shadowed : member.shadowedPaths)
        {
            QTreeWidgetItem* shadow = new QTreeWidgetItem(item,
                QStringList() << tr("SHADOWED") << member.fileName << shadowed
                              << tr("Not collision-checked; workspace source overrides it"));
            shadow->setData(0, kValidationPathRole, shadowed);
            shadow->setData(0, kValidationCopyRole,
                            tr("Shadowed source for %1\n%2").arg(member.fileName, shadowed));
        }
    }
    const int visibleFindingCount = qMin(m_validationFindingLimit,
                                         m_familyValidation.findings.size());
    for(int findingIndex = 0; findingIndex < visibleFindingCount; ++findingIndex)
    {
        const SMobFamilyFinding& finding = m_familyValidation.findings.at(findingIndex);
        QString severity;
        QColor color;
        switch(finding.severity)
        {
        case EMobFamilyFindingSeverity::Critical:
            severity = tr("CRITICAL"); color = QColor(220, 70, 70); break;
        case EMobFamilyFindingSeverity::Warning:
            severity = tr("WARNING"); color = QColor(230, 160, 45); break;
        case EMobFamilyFindingSeverity::Incomplete:
            severity = tr("INCOMPLETE"); color = QColor(190, 120, 230); break;
        case EMobFamilyFindingSeverity::Info:
            severity = tr("INFO"); color = QColor(130, 160, 190); break;
        }
        QTreeWidgetItem* item = new QTreeWidgetItem(m_validationTree,
            QStringList() << severity << finding.category
                          << (finding.locations.isEmpty() ? QString() : finding.locations.first())
                          << finding.message);
        item->setForeground(0, QBrush(color));
        item->setToolTip(2, finding.locations.join('\n'));
        item->setData(0, kValidationPathRole,
                      finding.locations.isEmpty() ? QString() : finding.locations.first());
        item->setData(0, kValidationCopyRole,
                      QString("%1 %2\n%3\n%4")
                          .arg(severity, finding.category, finding.message,
                               finding.locations.join('\n')));
        for(const QString& location : finding.locations)
            new QTreeWidgetItem(item, QStringList() << QString() << tr("Location")
                                                    << location << QString());
        if(finding.hasNid)
        {
            const QVector<SMobNidOwner> owners = m_familyValidation.nidOwners.value(finding.nid);
            for(const SMobNidOwner& owner : owners)
            {
                QString detail = tr("NID %1 — %2")
                    .arg(owner.nid)
                    .arg(owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName);
                if(finding.code == "nid.collision")
                {
                    quint32 suggestion = 0;
                    bool shared = false;
                    detail += m_familyValidation.suggestReplacementNid(
                        owner.mobPath, &suggestion, &shared)
                        ? tr("; suggested replacement %1%2")
                            .arg(suggestion)
                            .arg(shared ? tr(" (inside a shared range)") : QString())
                        : tr("; no free ID remains in this MOB's effective range");
                }
                QTreeWidgetItem* ownerItem = new QTreeWidgetItem(item,
                    QStringList() << QString() << tr("Object") << owner.mobPath << detail);
                ownerItem->setData(0, kValidationPathRole, owner.mobPath);
                ownerItem->setData(0, kValidationCopyRole,
                    QString("NID %1\nMOB: %2\nObject: %3\nSection: %4")
                        .arg(owner.nid).arg(owner.mobPath,
                            owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName,
                            owner.sectionPath));
                ownerItem->setData(0, kValidationNidRole, owner.nid);
                ownerItem->setData(0, kValidationSectionRole, owner.sectionPath);
                ownerItem->setToolTip(1, tr("Double-click to select this object if its MOB is loaded."));
            }
        }
    }
    if(m_validationShowMore && visibleFindingCount < m_familyValidation.findings.size())
    {
        m_validationShowMore->setText(tr("Show 250 more findings (%1 remaining)")
            .arg(m_familyValidation.findings.size() - visibleFindingCount));
        m_validationShowMore->show();
    }
    membersRoot->setExpanded(true);
    if(m_familyValidation.criticalCount() > 0 && m_validationDock)
        m_validationDock->show();
}

void MainWindow::showMobFamilyValidation()
{
    refreshMobFamilyValidation();
    if(m_validationDock)
    {
        m_validationDock->show();
        m_validationDock->raise();
        m_validationDock->activateWindow();
        m_validationDock->setFocus();
    }
}

void MainWindow::showValidationWarning(const QString& title, const QString& text)
{
    QMessageBox message(QMessageBox::Warning, title, text, QMessageBox::NoButton, this);
    message.setTextFormat(Qt::PlainText);
    // Target only the text label. Applying a minimum width to every QLabel also
    // expands QMessageBox's icon label and pushes the useful text offscreen.
    message.setStyleSheet("QLabel#qt_msgbox_label { min-width: 980px; }");
    message.setMinimumWidth(1100);
    if(QLabel* textLabel = message.findChild<QLabel*>("qt_msgbox_label"))
    {
        textLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        textLabel->setWordWrap(false);
        textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    QPushButton* openPanel = message.addButton(tr("Open Validation Panel"),
                                               QMessageBox::ActionRole);
    message.addButton(QMessageBox::Ok);
    message.exec();
    if(message.clickedButton() == openPanel)
        showMobFamilyValidation();
}

QString MainWindow::validationWarningPreview(
    const QVector<SMobFamilyFinding>& findings) const
{
    QStringList lines;
    const int shown = qMin(3, findings.size());
    for(int index = 0; index < shown; ++index)
        lines.append(conciseValidationLine(findings.at(index)));
    if(findings.size() > shown)
        lines.append("...");
    return lines.isEmpty() ? tr("Validation found a possible MOB-family problem.")
                           : lines.join('\n');
}

void MainWindow::selectValidationObject(QTreeWidgetItem* item)
{
    if(!item || !m_pView || !item->data(0, kValidationNidRole).isValid())
        return;
    const QString mobPath = item->data(0, kValidationPathRole).toString();
    const quint32 nid = item->data(0, kValidationNidRole).toUInt();
    const QString sectionPath = item->data(0, kValidationSectionRole).toString();
    if(!m_pView->selectObjectByProvenance(mobPath, nid, sectionPath))
    {
        statusBar()->showMessage(
            tr("Object NID %1 is not selectable: load its MOB or resolve its ambiguous provenance.")
                .arg(nid), 7000);
        return;
    }
    activateWindow();
    raise();
    statusBar()->showMessage(tr("Selected NID %1 from MOB validation.").arg(nid), 4000);
}

void MainWindow::reviewAndRepairNidCollisions()
{
    refreshMobFamilyValidation();
    QVector<SMobNidOwner> rows;
    for(auto it = m_familyValidation.nidOwners.constBegin();
        it != m_familyValidation.nidOwners.constEnd(); ++it)
    {
        if(it.value().size() < 2)
            continue;
        bool critical = false;
        for(const SMobFamilyFinding& finding : m_familyValidation.findings)
            if(finding.code == "nid.collision" && finding.hasNid && finding.nid == it.key())
            {
                critical = true;
                break;
            }
        if(critical)
            rows += it.value();
    }
    if(rows.isEmpty())
    {
        QMessageBox::information(this, tr("NID repair"),
            tr("No repairable critical NID collision is present in the active runtime family."));
        return;
    }

    QSet<QString> loadedPaths;
    for(CMob* mob : m_pView->mobs())
        if(mob)
            loadedPaths.insert(QDir::cleanPath(mob->filePath().absoluteFilePath()).toLower());
    const QString primaryPath = QDir::cleanPath(m_mapTestEntry.mobPath).toLower();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Review and Repair NID Collisions"));
    dialog.resize(1080, 520);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* explanation = new QLabel(tr(
        "Nothing is changed automatically. Select the newly added/edited owners to renumber. "
        "Endurance preserves the map-table primary by default and refuses automatic repair when "
        "a script or typed ParentID reference is ambiguous. Save and Test Current Map remain available."),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    QTableWidget* table = new QTableWidget(rows.size(), 7, &dialog);
    table->setHorizontalHeaderLabels(QStringList() << tr("Repair") << tr("Current NID")
        << tr("MOB") << tr("Object") << tr("OBJTYPE") << tr("Replacement") << tr("Safety"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);

    QSet<quint32> suggestedIds;
    for(int row = 0; row < rows.size(); ++row)
    {
        const SMobNidOwner& owner = rows.at(row);
        QTableWidgetItem* check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        const QString ownerPath = QDir::cleanPath(owner.mobPath).toLower();
        const bool loaded = loadedPaths.contains(ownerPath);
        QStringList blockers;
        QVector<quint32> ids{owner.nid};
        for(const SMobFamilyMember& member : m_familyValidation.members)
        {
            const QVector<SMobScriptReference> references =
                CScriptReferenceAnalyzer::findObjectIds(member.script, ids);
            for(const SMobScriptReference& reference : references)
                blockers.append(tr("script %1:%2").arg(member.fileName).arg(reference.line));
            for(const SMobObjectSnapshot& object : member.objects)
                if(object.hasParentId && object.parentId == owner.nid)
                    blockers.append(tr("ParentID in %1:%2").arg(member.fileName, object.sectionPath));
        }
        if(!loaded)
            blockers.prepend(tr("MOB is not loaded for editing"));
        const bool safe = blockers.isEmpty();
        check->setCheckState(safe && ownerPath != primaryPath ? Qt::Checked : Qt::Unchecked);
        if(!safe)
            check->setFlags(Qt::NoItemFlags);
        table->setItem(row, 0, check);
        table->setItem(row, 1, new QTableWidgetItem(QString::number(owner.nid)));
        table->setItem(row, 2, new QTableWidgetItem(owner.mobName));
        table->setItem(row, 3, new QTableWidgetItem(QString("%1 / %2")
            .arg(owner.objectName.isEmpty() ? owner.sectionPath : owner.objectName,
                 owner.prototype.isEmpty() ? QString("-") : owner.prototype)));
        table->setItem(row, 4, new QTableWidgetItem(owner.hasObjectType
            ? QString::number(owner.objectType) : tr("unknown")));
        quint32 suggestion = 0;
        bool shared = false;
        const bool hasSuggestion = m_familyValidation.suggestReplacementNid(
            owner.mobPath, &suggestion, &shared, suggestedIds);
        if(hasSuggestion)
            suggestedIds.insert(suggestion);
        QTableWidgetItem* replacement = new QTableWidgetItem(
            hasSuggestion ? QString::number(suggestion) : QString());
        if(!safe || !hasSuggestion)
            replacement->setFlags(Qt::NoItemFlags);
        table->setItem(row, 5, replacement);
        QString safety = blockers.isEmpty()
            ? (shared ? tr("Unused, but declared by another MOB range") : tr("Ready"))
            : tr("Manual resolution required: %1").arg(blockers.join(", "));
        table->setItem(row, 6, new QTableWidgetItem(safety));
        for(int column = 1; column < 5; ++column)
            table->item(row, column)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        table->item(row, 6)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    layout->addWidget(table);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
            &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            &dialog, &QDialog::accept);
    if(dialog.exec() != QDialog::Accepted)
        return;

    QVector<CView::SNidRepair> repairs;
    QSet<quint32> replacements;
    QMap<quint32, int> selectedPerCollision;
    for(int row = 0; row < rows.size(); ++row)
    {
        if(!table->item(row, 0) || table->item(row, 0)->checkState() != Qt::Checked)
            continue;
        bool valid = false;
        const quint64 parsed = table->item(row, 5)->text().toULongLong(&valid);
        if(!valid || parsed > std::numeric_limits<quint32>::max())
        {
            QMessageBox::warning(this, tr("NID repair"), tr("Row %1 has an invalid replacement ID.").arg(row + 1));
            return;
        }
        const quint32 replacement = quint32(parsed);
        if(replacements.contains(replacement) || m_familyValidation.nidOwners.contains(replacement))
        {
            QMessageBox::warning(this, tr("NID repair"),
                tr("Replacement NID %1 is already used by the family or another selected repair.").arg(replacement));
            return;
        }
        replacements.insert(replacement);
        selectedPerCollision[rows.at(row).nid] += 1;
        CView::SNidRepair repair;
        repair.mobPath = rows.at(row).mobPath;
        repair.sectionPath = rows.at(row).sectionPath;
        repair.oldNid = rows.at(row).nid;
        repair.newNid = replacement;
        repairs.append(repair);
    }
    if(repairs.isEmpty())
    {
        QMessageBox::information(this, tr("NID repair"), tr("No repair rows were selected."));
        return;
    }
    for(auto it = selectedPerCollision.constBegin(); it != selectedPerCollision.constEnd(); ++it)
        if(it.value() >= m_familyValidation.nidOwners.value(it.key()).size())
        {
            QMessageBox::warning(this, tr("NID repair"),
                tr("At least one owner of NID %1 must keep the established ID.").arg(it.key()));
            return;
        }
    QString error;
    if(!m_pView->applyFamilyNidRepairs(repairs, &error))
    {
        QMessageBox::warning(this, tr("NID repair"), error);
        return;
    }
    ei::log(eLogInfo, QString("Applied one undoable family NID repair transaction with %1 object(s)")
        .arg(repairs.size()));
    scheduleMobFamilyValidation(0);
}

void MainWindow::notifyFamilyPreflight(const QString& operation)
{
    if(!m_pView || m_pView->mobs().isEmpty())
        return;
    refreshMobFamilyValidation();
    if(!m_familyValidation.hasActionableFindings())
        return;
    const QByteArray digest = m_familyValidation.findingDigest();
    if(digest == m_acknowledgedFindingDigest)
        return;
    m_acknowledgedFindingDigest = digest;
    ei::log(eLogWarning, QString("User continued with %1 after non-blocking MOB family validation findings:\n%2")
        .arg(operation, m_familyValidation.toText()));
    QVector<SMobFamilyFinding> actionable;
    for(const SMobFamilyFinding& finding : m_familyValidation.findings)
        if(finding.severity != EMobFamilyFindingSeverity::Info)
            actionable.append(finding);
    showValidationWarning(tr("MOB family validation"),
                          validationWarningPreview(actionable));
}

void MainWindow::notifyProposedNid(quint32 nid, const QString& mobPath)
{
    refreshMobFamilyValidation();
    const QString details = m_familyValidation.describeProposedNid(nid, mobPath);
    if(details.isEmpty())
        return;
    ei::log(eLogWarning, QString("Proposed NID %1 overlaps the active family:\n%2")
        .arg(nid).arg(details));
    QStringList detailLines = details.split('\n', QString::SkipEmptyParts);
    QVector<SMobFamilyFinding> preview;
    for(const QString& detail : detailLines)
    {
        SMobFamilyFinding finding;
        finding.severity = EMobFamilyFindingSeverity::Warning;
        finding.message = tr("NID %1: %2").arg(nid).arg(detail);
        preview.append(finding);
    }
    showValidationWarning(tr("NID overlap"), validationWarningPreview(preview));
}

void MainWindow::openCollection(const QString& path)
{
    SEnduranceCollection document;
    QString error;
    if(!CCollectionDocument::load(path, &document, &error))
    {
        QMessageBox::warning(this, tr("Open Collection"), error);
        return;
    }
    const SResolvedCollection resolved = CCollectionDocument::resolve(document, activeMapRoots());
    if(!resolved.isValid())
    {
        QMessageBox::warning(this, tr("Open Collection"),
            tr("These collection references could not be resolved:\n\n%1")
                .arg(resolved.errors.join('\n')));
        return;
    }
    const QString profileId = activeResourceProfile().profileId;
    if(!document.profileId.isEmpty()
       && document.profileId.compare(profileId, Qt::CaseInsensitive) != 0
       && QMessageBox::warning(this, tr("Collection profile"),
            tr("This collection was saved for profile '%1', while '%2' is active. "
               "Opening it will not switch resources automatically. Continue?")
                .arg(document.profileId, profileId),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if(!isExitAllowed())
        return;
    m_undoStack->clear();
    m_pView->unloadMob("");
    m_pView->unloadLand();
    m_openedMapZoneId = document.mapZoneId;
    m_currentCollectionPath = QFileInfo(path).absoluteFilePath();
    m_pView->loadLandscape(QFileInfo(resolved.mprPath));
    for(const QString& mobPath : resolved.mobPaths)
    {
        QFileInfo mob(mobPath);
        m_pView->loadMob(mob);
        rememberRecentFile(mob);
    }
    rememberRecentFile(QFileInfo(resolved.mprPath));
    refreshMapTestContext();
    ei::log(eLogInfo, QString("Opened collection path:%1 map:%2 mpr:%3 mobs:%4")
        .arg(path, document.mapZoneId, resolved.mprPath)
        .arg(resolved.mobPaths.size()));
}

void MainWindow::newCollection()
{
    SEnduranceCollection initial;
    initial.profileId = activeResourceProfile().profileId;
    initial.mapZoneId = m_mapTestEntry.zoneId;
    initial.name = m_mapTestEntry.zoneId.isEmpty() ? tr("New collection")
                                                   : m_mapTestEntry.zoneId;
    if(CLandscape::getInstance()->isMprLoad())
        initial.mprReference = CLandscape::getInstance()->filePath().absoluteFilePath();
    if(m_pView)
        for(CMob* mob : m_pView->mobs())
            if(mob) initial.mobReferences.append(mob->filePath().absoluteFilePath());
    CCollectionEditorDialog dialog(initial, m_mapCatalog.entries, this);
    if(dialog.exec() != QDialog::Accepted)
        return;
    QDir().mkpath(CCollectionDocument::defaultDirectory());
    QString suggested = dialog.collection().name;
    suggested.replace(QRegularExpression("[^A-Za-z0-9_.-]+"), "-");
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Collection"),
        QDir(CCollectionDocument::defaultDirectory()).filePath(suggested + ".collection"),
        tr("Endurance Collection (*.collection)"));
    if(path.isEmpty())
        return;
    SEnduranceCollection value = dialog.collection();
    value.mprReference = CCollectionDocument::portableReference(
        value.mprReference, path, activeMapRoots());
    for(QString& mob : value.mobReferences)
        mob = CCollectionDocument::portableReference(mob, path, activeMapRoots());
    QString error;
    if(!CCollectionDocument::save(path, value, &error))
        QMessageBox::warning(this, tr("Save Collection"), error);
    else
    {
        m_currentCollectionPath = QFileInfo(path).absoluteFilePath();
        refreshMapsBrowser();
    }
}

void MainWindow::editCollection()
{
    QDir().mkpath(CCollectionDocument::defaultDirectory());
    const QString path = QFileDialog::getOpenFileName(this, tr("Edit Collection"),
        m_currentCollectionPath.isEmpty() ? CCollectionDocument::defaultDirectory()
                                          : m_currentCollectionPath,
        tr("Endurance Collection (*.collection)"));
    if(path.isEmpty()) return;
    SEnduranceCollection value;
    QString error;
    if(!CCollectionDocument::load(path, &value, &error))
    {
        QMessageBox::warning(this, tr("Edit Collection"), error);
        return;
    }
    const SResolvedCollection resolved = CCollectionDocument::resolve(value, activeMapRoots());
    if(!resolved.mprPath.isEmpty()) value.mprReference = resolved.mprPath;
    if(resolved.mobPaths.size() == value.mobReferences.size())
        value.mobReferences = resolved.mobPaths;
    CCollectionEditorDialog dialog(value, m_mapCatalog.entries, this);
    if(dialog.exec() != QDialog::Accepted) return;
    value = dialog.collection();
    value.mprReference = CCollectionDocument::portableReference(
        value.mprReference, path, activeMapRoots());
    for(QString& mob : value.mobReferences)
        mob = CCollectionDocument::portableReference(mob, path, activeMapRoots());
    if(!CCollectionDocument::save(path, value, &error))
        QMessageBox::warning(this, tr("Edit Collection"), error);
    else
    {
        m_currentCollectionPath = QFileInfo(path).absoluteFilePath();
        refreshMapsBrowser();
    }
}

void MainWindow::saveCollection(bool saveAs)
{
    if(!m_pView || !CLandscape::getInstance()->isMprLoad() || m_pView->mobs().isEmpty())
    {
        QMessageBox::warning(this, tr("Save Collection"),
            tr("Load one MPR and at least one MOB first."));
        return;
    }
    QString path = saveAs ? QString() : m_currentCollectionPath;
    if(path.isEmpty())
    {
        QDir().mkpath(CCollectionDocument::defaultDirectory());
        path = QFileDialog::getSaveFileName(this, tr("Save Collection As"),
            CCollectionDocument::defaultDirectory(), tr("Endurance Collection (*.collection)"));
    }
    if(path.isEmpty()) return;
    if(!path.endsWith(".collection", Qt::CaseInsensitive)) path += ".collection";
    SEnduranceCollection value;
    value.name = QFileInfo(path).completeBaseName();
    value.profileId = activeResourceProfile().profileId;
    value.mapZoneId = m_mapTestEntry.zoneId;
    value.mprReference = CCollectionDocument::portableReference(
        CLandscape::getInstance()->filePath().absoluteFilePath(), path, activeMapRoots());
    for(CMob* mob : m_pView->mobs())
        if(mob) value.mobReferences.append(CCollectionDocument::portableReference(
            mob->filePath().absoluteFilePath(), path, activeMapRoots()));
    QString error;
    if(!CCollectionDocument::save(path, value, &error))
        QMessageBox::warning(this, tr("Save Collection"), error);
    else
    {
        m_currentCollectionPath = QFileInfo(path).absoluteFilePath();
        refreshMapsBrowser();
        statusBar()->showMessage(tr("Collection saved: %1").arg(path), 5000);
    }
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
    m_openedMapZoneId = zoneId;
    m_currentCollectionPath.clear();
    m_pView->loadLandscape(QFileInfo(mprPath));
    QFileInfo mobFile(mobPath);
    m_pView->loadMob(mobFile);
    rememberRecentFile(QFileInfo(mprPath));
    rememberRecentFile(QFileInfo(mobPath));
    ei::log(eLogInfo, QString("Opened profile map zone:%1 mpr:%2 mob:%3")
        .arg(zoneId, mprPath, mobPath));
    QTimer::singleShot(0, this, [this]()
    {
        if(!m_validEntrances.isEmpty())
        {
            m_entranceCombo->setCurrentIndex(0);
            updateEntranceOverlays();
            moveCameraToActiveSpawn();
        }
    });
}

void MainWindow::connectUiButtons()
{
    CButtonConnector::getInstance()->attach(m_pView);
    CButtonConnector::getInstance()->addButton(EButtonOpSelect, m_ui->selectButton);
    CButtonConnector::getInstance()->addButton(EButtonOpMove, m_ui->moveButton);
    CButtonConnector::getInstance()->addButton(EButtonOpRotate, m_ui->rotateButton);
    CButtonConnector::getInstance()->addButton(EButtonOpScale, m_ui->scaleButton);
    CButtonConnector::getInstance()->addButton(EButtonOpTilebrush, m_ui->tileBrushButton);
    connect(m_pView, &CView::selectionStateChanged,
            this, &MainWindow::updateOperationAvailability);
    updateOperationAvailability(false);
}

void MainWindow::updateOperationAvailability(bool hasSelection)
{
    const bool hasMob = m_pView && m_pView->hasActiveMob();
    // Selection notifications can arrive while a batched tree/viewport update is
    // still settling.  The view is the source of truth for toolbar and shortcut
    // availability, so never leave the controls disabled because of a stale
    // notification argument.
    hasSelection = hasMob && m_pView->nSelectedNodes() > 0;
    const bool hasMpr = CLandscape::getInstance()->isMprLoad();
    const bool hasMovableSelection = (hasMob && hasSelection)
        || (m_customSpawnSelected && m_customSpawn.valid && m_customSpawn.custom);
    const struct { EButtonOp type; QToolButton* button; bool enabled; } states[] = {
        {EButtonOpSelect, m_ui->selectButton, hasMob},
        {EButtonOpMove, m_ui->moveButton, hasMovableSelection},
        {EButtonOpRotate, m_ui->rotateButton, hasMob && hasSelection},
        {EButtonOpScale, m_ui->scaleButton, hasMob && hasSelection},
        {EButtonOpTilebrush, m_ui->tileBrushButton, hasMpr}
    };
    for(const auto& state : states)
    {
        state.button->setEnabled(state.enabled);
        if(QAction* action = m_operationActions.value(state.type, nullptr))
            action->setEnabled(state.enabled);
    }
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
        m_openedMapZoneId.clear();
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
    COptString* configuredProfile = dynamic_cast<COptString*>(
        m_settings->opt(eOptSetResource, "resourceProfile"));
    CResourceSetupDialog dialog(m_settings->configuredGameRoot(),
        configuredProfile ? configuredProfile->value() : QString(), this);
    if(dialog.exec() != QDialog::Accepted)
        return;
    const SResourceProfileEvaluation profile = dialog.evaluation();
    const QString previousProfile = configuredProfile ? configuredProfile->value() : QString();
    const bool profileChanged = previousProfile.compare(profile.profileId,
        Qt::CaseInsensitive) != 0;
    if(profileChanged && m_pView
       && (CLandscape::getInstance()->isMprLoad() || m_pView->hasActiveMob()))
    {
        if(!isExitAllowed())
            return;
        m_undoStack->clear();
        m_pView->unloadMob("");
        m_pView->unloadLand();
        m_openedMapZoneId.clear();
        ei::log(eLogInfo, QString("Unloaded documents before resource profile switch %1 -> %2")
            .arg(previousProfile, profile.profileId));
    }
    m_settings->applyResourceProfile(profile);
    if(m_pView)
        m_pView->refreshAfterResourceSetup();
    refreshMapsBrowser();
    updateWindowTitle(eTitleTypeData::eTitleTypeDataMpr, m_sWindowTitle.mpr);
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
        m_openedMapZoneId.clear();
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
    m_openedMapZoneId.clear();
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
    notifyFamilyPreflight(tr("Save As"));
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
    notifyFamilyPreflight(tr("Save"));
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
        updateOperationAvailability(m_pView && m_pView->nSelectedNodes() > 0);
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
    QString title = QString("EI Mapper: Endurance Edition %1 [%2]")
        .arg(QCoreApplication::applicationVersion(), activeProfileDisplayName());
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
    if(type == eTitleTypeData::eTitleTypeDataMpr)
        refreshMapTestContext();
}


void MainWindow::on_actionSave_all_MOB_s_triggered()
{
    notifyFamilyPreflight(tr("Save All MOBs"));
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


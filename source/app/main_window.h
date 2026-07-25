#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QKeyEvent>
#include <QSharedPointer>
#include <QUndoStack>
#include <QUndoView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector3D>
#include <QFutureWatcher>
#include "types.h"
#include "map_catalog.h"
#include "map_spawn_planner.h"
#include "mob_family_validation.h"

namespace Ui {
class MainWindow;
}

class CSettings;
class CSelectForm;
class CCreateObjectForm;
class CView;
class CMob;
class CRandomizeForm;
class CTileForm;
class QMenu;
class QAction;
class QDockWidget;
class CMapsBrowser;
class CJmvMapTestLauncher;
class CLandscapeTools;
struct SResourceProfileEvaluation;
struct SEnduranceCollection;
class QComboBox;
class QCheckBox;
class QToolButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;
class QPushButton;
class QLineEdit;
class QFrame;
class QResizeEvent;
class QJsonObject;
class QProgressDialog;
class QDialog;
class QScrollArea;
class QListWidget;
class QSlider;
class QDoubleSpinBox;

struct SWindowTitle
{
    QString mpr;
    bool mprDirty{false};
    QString activeMob;
    bool durty{false};
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    bool openDocument(const QFileInfo& fileName);
    bool replaceLoadedLandscape(const QFileInfo& fileName);
    bool loadMobWithProgress(QFileInfo fileName);
    bool loadLandscapeWithProgress(const QFileInfo& fileName);
    void configureAutosaveTimer();
    void configureAutosaveTimer(bool enabled, int intervalMinutes);
    void performAutosave();
    void beginAutosaveCountdown();
    void showRecoverableAutosaves();

    void testFunc1();

private:
    enum class ETransientNotificationAction
    {
        None,
        OpenLog,
        OpenBackupFolder
    };

    void createUndoView();
    void initShortcuts();
    void connectUiButtons();
    bool isExitAllowed();
    void closeAll();
    void showGameAndModSetup();
    void rememberRecentFile(const QFileInfo& fileName);
    void rebuildRecentFilesMenu();
    void loadLastSession();
    void setWorkspaceIdentity(const QString& kind, const QString& identifier,
                              const QString& displayName, const QString& mapZoneId,
                              bool syncView = true);
    void rebuildWindowTitle();
    void runUndoToIndex(int targetIndex);
    void refreshMapsBrowser();
    void openMapPair(const QString& mprPath, const QString& mobPath, const QString& zoneId);
    void openCollection(const QString& path);
    void newCollection();
    void editCollection();
    void editCollection(const QString& path);
    void duplicateCollection(const QString& path);
    void saveCollectionAs(const QString& path);
    void deleteCollection(const QString& path);
    void duplicateMapAsCollection(const QString& zoneId, const QString& mprPath,
                                  const QString& mobPath);
    void createCollectionFromTemplate(const SEnduranceCollection& initial,
                                      const QString& suggestedBaseName,
                                      bool promptToLoad = false);
    void saveCollection(bool saveAs);
    QVector<QString> activeMapRoots() const;
    void updateMapTestAction();
    CMob* mapTestStagingMob() const;
    void launchCurrentMapTest();
    void createViewportToolbars();
    void refreshMapTestContext();
    void populateEntranceCombo();
    void chooseBorrowedMapIdentity();
    void setCustomSpawnPicking(bool enabled);
    void updateCustomSpawnControls();
    QString currentMapContextKey() const;
    void moveCameraToActiveSpawn();
    void updateEntranceOverlays();
    void updateOperationAvailability(bool hasSelection = false);
    void updateDocumentActionAvailability();
    bool beginWorkspaceLoad(const QString& description);
    void endWorkspaceLoad();
    void updateLoadedMapPanel();
    void updateOutlinerSelectionPath();
    void previewLoadedMapVisibility(QTreeWidgetItem* item);
    void commitLoadedMapVisibilityDrag();
    void showLandscapeTools();
    void hideLandscapeTools();
    void toggleLandscapeTools();
    void restoreLayoutAfterLandscapeTools();
    void handleEscapeShortcut();
    void saveUiState();
    void closeHotReloadProgress();
    QVector<SMapSpawnObstacle> mapTestObstacles() const;
    const SMapTravelZone* selectedTravelZone() const;
    bool automaticLooseMapBootstrap(SMapCatalogEntry* entry,
                                    SMapTravelZone* entrance) const;
    SResourceProfileEvaluation activeResourceProfile() const;
    QString activeProfileDisplayName() const;
    SMobFamilyValidationRequest mobFamilyValidationRequest();
    void scheduleMobFamilyValidation(int delayMs = 300);
    void refreshMobFamilyValidation();
    void startPendingMobFamilyValidation();
    void finishMobFamilyValidation();
    void applyMobFamilyValidationReport(const CMobFamilyValidationReport& report);
    void updateMobFamilyValidationPanel();
    void showMobFamilyValidation();
    void showValidationWarning(const QString& title, const QString& text);
    QString validationWarningPreview(const QVector<SMobFamilyFinding>& findings) const;
    void selectValidationObject(QTreeWidgetItem* item);
    void reviewAndRepairNidCollisions();
    void notifyFamilyPreflight(const QString& operation);
    void notifyProposedNid(quint32 nid, const QString& mobPath);
    void showMapTestResultNotification(const QJsonObject& report);
    void showTransientResultNotification(const QString& text, bool failed, bool warned,
                                         const QString& actionText,
                                         ETransientNotificationAction action,
                                         int timeoutMs);
    void positionMapTestResultNotification();
    void positionAutosaveNotification();
    void showUnitTexturePreview();
    void updateUnitTexturePreview();
    void renderUnitTexturePreview();

private slots:
    void showOutliner(bool bShow);
    void on_actionExit_triggered();
    void on_actionOpen_triggered();
    void on_actionOpen_MPR_triggered();
    void on_actionOpen_MOB_triggered();
    void on_actionSettings_triggered();
    void on_actionSave_as_triggered();
    void on_actionSelect_All_triggered();
    void on_actionSelect_by_triggered();
    void on_actionShow_undo_redo_triggered();
    void on_actionClose_all_triggered();
    void on_actionSave_triggered();
    void on_action_Mob_parameters_triggered();
    void on_actionOpen_MOB_Script_triggered();
    void on_actionDuplicate_selected_objects_triggered();
    void on_actionUndo_triggered();
    void on_toolButton_2_clicked();
    void on_selectButton_clicked();
    void on_moveButton_clicked();
    void on_actionRedo_triggered();
    void on_actionCreate_new_object_triggered();
    void updateWindowTitle(eTitleTypeData type, QString data);
    void on_actionSave_all_MOB_s_triggered();
    void on_rotateButton_clicked();
    void on_scaleButton_clicked();
    void on_actionReset_cam_position_triggered();
    void on_actionChange_mod_e_triggered();
    void on_actionCopy_IDs_to_clipboard_triggered();
    void on_actionOpen_2_triggered();
    void on_actionOpen_recent_triggered();
    void on_actionReset_logic_paths_triggered();
    void on_actionRandomize_parameter_triggered();
    void on_actionReset_selected_IDs_triggered();
    void on_action_About_triggered();
    void on_actionSave_landscape_MPR_as_triggered();
    void on_actionMap_parameters_triggered();
    void on_tileBrushButton_clicked();
    void on_actionSave_landscape_MPR_triggered();

protected:
    void closeEvent(QCloseEvent* e);
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Ui::MainWindow* m_ui;
    QSharedPointer<CSettings> m_settings;
    QSharedPointer<CSelectForm> m_selector;
    QSharedPointer<CCreateObjectForm> m_createDialog;
    QSharedPointer<CRandomizeForm> m_randomizeForm;
    QSharedPointer<CTileForm> m_pTileForm;
    QUndoStack* m_undoStack;
    QUndoView* m_undoView;
    CView* m_pView;
    SWindowTitle m_sWindowTitle;
    QTableWidget* m_testTable;
    QMenu* m_recentFilesMenu;
    QAction* m_externalScriptAction;
    QAction* m_convertWorldObjectToLeverAction;
    QAction* m_landscapeToolsAction{nullptr};
    QDockWidget* m_mapsDock;
    QDockWidget* m_landscapeToolsDock;
    CLandscapeTools* m_landscapeTools;
    QByteArray m_layoutBeforeLandscapeTools;
    QByteArray m_landscapeToolsLayout;
    Qt::DockWidgetArea m_landscapeDockArea{Qt::RightDockWidgetArea};
    int m_landscapeDockHeight{420};
    bool m_landscapeDockFloating{false};
    bool m_restoringLandscapeDockLayout{false};
    bool m_suppressLandscapeDockRestore{false};
    quint64 m_landscapeVisibilityGeneration{0};
    QDockWidget* m_historyDock;
    QDockWidget* m_validationDock;
    QDockWidget* m_outlinerDock;
    QDockWidget* m_propertiesDock;
    QDockWidget* m_loadedMapDock;
    QTreeWidget* m_loadedMapTree;
    QTreeWidget* m_validationTree;
    QLabel* m_validationSummary;
    QTimer* m_validationTimer;
    QFutureWatcher<CMobFamilyValidationReport>* m_validationWatcher{nullptr};
    SMobFamilyValidationRequest m_pendingValidationRequest;
    quint64 m_validationGeneration{0};
    quint64 m_runningValidationGeneration{0};
    bool m_hasPendingValidation{false};
    QByteArray m_lastValidationRequestDigest;
    int m_validationFindingLimit{250};
    QPushButton* m_validationShowMore{nullptr};
    QLineEdit* m_outlinerFilterEdit{nullptr};
    QLabel* m_outlinerFilterStatus{nullptr};
    QLabel* m_outlinerSelectionPath{nullptr};
    bool m_loadedMapVisibilityDragging{false};
    Qt::CheckState m_loadedMapVisibilityTarget{Qt::Unchecked};
    QSet<QString> m_loadedMapVisibilityKeys;
    QFrame* m_mapTestResultNotification{nullptr};
    QLabel* m_mapTestResultIcon{nullptr};
    QLabel* m_mapTestResultLabel{nullptr};
    QPushButton* m_mapTestResultActionButton{nullptr};
    QTimer* m_mapTestResultTimer{nullptr};
    QProgressDialog* m_hotReloadProgress{nullptr};
    QTimer* m_autosaveTimer{nullptr};
    QTimer* m_autosaveCountdownTimer{nullptr};
    QTimer* m_cameraSpeedPersistTimer{nullptr};
    QFrame* m_autosaveNotification{nullptr};
    QLabel* m_autosaveCountdownLabel{nullptr};
    QPushButton* m_cancelAutosaveButton{nullptr};
    int m_autosaveCountdownSeconds{0};
    int m_recoverableAutosaveNoticeCount{0};
    QStringList m_recoverableAutosaveNoticeKeys;
    bool m_backupSessionStarted{false};
    QSlider* m_cameraSpeedSlider{nullptr};
    QDoubleSpinBox* m_cameraSpeedSpin{nullptr};
    bool m_uiStateSaved{false};
    bool m_workspaceLoadInProgress{false};
    QString m_mapTestResultLogPath;
    ETransientNotificationAction m_transientNotificationAction{
        ETransientNotificationAction::None};
    CMobFamilyValidationReport m_familyValidation;
    QByteArray m_acknowledgedFindingDigest;
    QSet<QString> m_previousFamilyRiskKeys;
    QSet<quint32> m_sessionAllocatedNids;
    QSet<QString> m_warnedSharedAllocationMobs;
    bool m_familyRiskBaselineReady{false};
    CMapsBrowser* m_mapsBrowser;
    QAction* m_mapTestAction;
    QAction* m_stopMapTestAction;
    CJmvMapTestLauncher* m_mapTestLauncher;
    QDialog* m_unitTextureDialog{nullptr};
    QLabel* m_unitTextureDetails{nullptr};
    QLabel* m_unitTextureImage{nullptr};
    QScrollArea* m_unitTextureScroll{nullptr};
    QListWidget* m_unitTextureLayers{nullptr};
    QListWidget* m_unitTextureParts{nullptr};
    bool m_unitTextureUpdating{false};
    bool m_closeAfterTestStop;
    QMap<EButtonOp, QAction*> m_operationActions;
    QComboBox* m_entranceCombo;
    QToolButton* m_showEntrancesButton;
    QToolButton* m_playFromHereButton;
    QToolButton* m_gotoEntranceButton;
    QCheckBox* m_godmodeCheckBox;
    QLabel* m_spawnPositionLabel;
    SMapCatalogResult m_mapCatalog;
    SMapCatalogEntry m_mapTestEntry;
    QVector<SMapTravelZone> m_validEntrances;
    QString m_openedMapZoneId;
    QString m_currentCollectionPath;
    QString m_workspaceIdentityKind;
    QString m_workspaceDisplayName;
    bool m_borrowedMapIdentity;
    bool m_pickSpawnPending;
    bool m_movingCustomSpawn{false};
    bool m_customSpawnSelected{false};
    bool m_suppressSpawnRelease{false};
    SMapSpawnPlan m_customSpawn;
    QVector3D m_customSpawnPosition;
    QString m_customSpawnContextKey;
};

#endif // MAIN_WINDOW_H

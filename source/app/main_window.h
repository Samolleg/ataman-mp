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
class CRandomizeForm;
class CTileForm;
class QMenu;
class QAction;
class QDockWidget;
class CMapsBrowser;
class CJmvMapTestLauncher;
struct SResourceProfileEvaluation;
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

    void testFunc1();

private:
    void createUndoView();
    void initShortcuts();
    void connectUiButtons();
    bool isExitAllowed();
    void closeAll();
    void showGameAndModSetup();
    void rememberRecentFile(const QFileInfo& fileName);
    void rebuildRecentFilesMenu();
    void runUndoToIndex(int targetIndex);
    void refreshMapsBrowser();
    void openMapPair(const QString& mprPath, const QString& mobPath, const QString& zoneId);
    void openCollection(const QString& path);
    void newCollection();
    void editCollection();
    void saveCollection(bool saveAs);
    QVector<QString> activeMapRoots() const;
    void updateMapTestAction();
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
    void updateLoadedMapPanel();
    void saveUiState();
    void closeHotReloadProgress();
    QVector<SMapSpawnObstacle> mapTestObstacles() const;
    const SMapTravelZone* selectedTravelZone() const;
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
    void positionMapTestResultNotification();

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
    QAction* m_activateOwnerMobAction;
    QDockWidget* m_mapsDock;
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
    QFrame* m_mapTestResultNotification{nullptr};
    QLabel* m_mapTestResultIcon{nullptr};
    QLabel* m_mapTestResultLabel{nullptr};
    QTimer* m_mapTestResultTimer{nullptr};
    QProgressDialog* m_hotReloadProgress{nullptr};
    bool m_uiStateSaved{false};
    QString m_mapTestResultLogPath;
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
    bool m_closeAfterTestStop;
    QMap<EButtonOp, QAction*> m_operationActions;
    QComboBox* m_entranceCombo;
    QToolButton* m_playFromHereButton;
    QToolButton* m_gotoEntranceButton;
    QCheckBox* m_godmodeCheckBox;
    QLabel* m_spawnPositionLabel;
    SMapCatalogResult m_mapCatalog;
    SMapCatalogEntry m_mapTestEntry;
    QVector<SMapTravelZone> m_validEntrances;
    QString m_openedMapZoneId;
    QString m_currentCollectionPath;
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

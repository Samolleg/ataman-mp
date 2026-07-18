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
#include <QVector3D>
#include "types.h"
#include "map_catalog.h"
#include "map_spawn_planner.h"

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
    void moveCameraToActiveSpawn();
    void updateEntranceOverlays();
    void updateOperationAvailability(bool hasSelection = false);
    QVector<SMapSpawnObstacle> mapTestObstacles() const;
    const SMapTravelZone* selectedTravelZone() const;
    SResourceProfileEvaluation activeResourceProfile() const;
    QString activeProfileDisplayName() const;

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
    QDockWidget* m_mapsDock;
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
};

#endif // MAIN_WINDOW_H

#ifndef MYGLWIDGET_H
#define MYGLWIDGET_H

#include <QGLWidget>
#include <QOpenGLShader>
#include <QFileInfo>
#include <QTimer>
#include <QSharedPointer>
#include <QTableWidget>
#include <QTreeWidgetItem>
#include <QDateTime>
#include <QHash>
#include <QPointer>

#include "types.h"
#include "select_window.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"

class QLineEdit;
class CKeyManager;
class CMob;
class CCamera;
class CNode;
class CUnit;
class CKeyManager;
class CSettings;
class CStringItem;
class QUndoStack;
struct SParam;
class CComboBoxItem;
class CProgressView;
class CTableManager;
class COperation;
struct SColor;
class CSelectFrame;
class QTreeWidget;
class CMobParameters;
class CRoundMobForm;
class CTreeView;
class IPropertyBase;
class CTileForm;
class CLandscapeTools;
class CPreviewTile;
class CLandscape;
class CExternalScriptWorkspaceManager;
class CMobScriptWindow;
struct SMobParameterState;

///
/// \brief The CView class is the main class for managing and editing 3D scene contents. It is also a link between read out *.mob files and their editing. It also controls the camera and current operations.
/// \todo Separate "core" functionality from rendering so that you can use the editor without using the UI
///
class CView : public QGLWidget
{
    Q_OBJECT

public:
    explicit CView(QWidget* parent=nullptr) = delete;
    explicit CView(QWidget* parent=nullptr, const QGLWidget* pShareWidget=nullptr);
    ~CView();

    void loadLandscape(const QFileInfo& filePath);
    void refreshAfterResourceSetup();
    void unloadLand();
    void saveLand();
    void saveLandAs();
    void loadMob(QFileInfo& filePath);
    void saveMobAs();
    void saveActiveMob();
    void saveAllMob();
    void unloadActiveMob();
    void openActiveMobEditParams();
    void openActiveMobScript();
    void openExternalMobScript(CMob* mob);
    void commitMobParameterEdit(CMob* mob, const SMobParameterState& before,
                                const SMobParameterState& after);
    void commitMobScriptEdit(CMob* mob, const QString& storedScript,
                             const QString& commandText);
    void notifyMobScriptChanged(CMob* mob);
    bool hasActiveMob() const { return m_activeMob != nullptr; }
    void unloadMob(QString mobName);
    void attach(CSettings* pSettings, QTableWidget* pParam, QUndoStack* pStack, CProgressView* pProgress, QLineEdit* pMouseCoord, CTreeView* pTree, CTileForm* pTileForm);
    void attachLandscapeTools(CLandscapeTools* tools);
    void activateLandscapeTilePainting();
    void prepareLandscapeTilePainting();
    void setLandscapeEditingEnabled(bool enabled);
    void setTileGridVisible(bool visible) { m_tileGridVisible = visible; update(); }
    CSettings* settings() {Q_ASSERT(m_pSettings); return m_pSettings;}
    int select(const SSelect& selectParam, bool bAddToSelect = false);
    CMob* mob(QString mobName);
    const QVector<CMob*> mobs() {return m_aMob;}
    void drawSelectFrame(QRect& rect);
    void pickObject(QPoint mousePos, bool bAddToSelect);
    void pickObject(const QRect& rect, bool bAddToSelect);
    QVector3D getTerrainPos(bool bLand = true);
    QVector3D getTerrainPos(const int cursorPosX, const int cursorPosY, bool bLand = true);
    void changeOperation(EButtonOp type);
    void operationSetBackup(EOperationAxisType operationType);
    void operationRevert(EOperationAxisType operationType);
    void operationApply(EOperationAxisType operationType);
    bool cancelPendingOperation();
    bool prepareHistoryNavigation();
    void moveTo(QVector3D& dir);
    void rotateTo(QVector3D& rot);
    void scaleTo(QVector3D& scale);
    void resetUnitLogicPaths();
    void deleteSelectedNodes();
    void selectedObjectToClipboardBuffer();
    void clipboradObjectsToScene();
    void duplicateSelectedObjects();
    void hideSelectedNodes();
    void unHideAll();
    CMob* currentMob() {return m_activeMob;}
    QOpenGLShaderProgram& shaderObject() {return m_program;}
    void setDirtyMpr();
    void setDurty(CMob* pMob = nullptr);
    void resetCamPosition();
    void addLogicPoint(bool bLookPoint = false);
    void copySelectedIDsToClipboard();
    void changeCurrentMob(CMob* pMob);
    void changeCurrentMob(QString mobName);
    void roundActiveMob();
    void undo_roundActiveMob();
    void execUnloadCommand();
    void iterateRoundMob();
    void applyRoundMob();
    bool isRecentAvailable();
    int renameActiveMobUnits(QMap<QString, QString>& mapName);
    void moveCamToSelectedObjects();
    CTreeView* objectTree() {return m_pTree;}
    int nSelectedNodes();
    QList<CNode*> selectedNodes();
    QList<CNode*> selectionSnapshot();
    void restoreSelection(const QList<CNode*>& selection);
    void clearObjectSelection();
    void recordSelectionChange(const QList<CNode*>& before, const QString& text = QString("Select objects"));
    void setRandomComplection(const EObjParam param, const float min, const float max);
    void resetSelectedId();
    void openMapParameters();
    void pickTile(QVector3D posOnLand, bool bLand = true);
    void setTile(QVector3D posOnLand, bool bLand = true);
    void updatePreviewTile(QVector3D posOnLand, bool bLand = true);
    void addTileRotation(int step);
    void setDrawWater(bool bDraw = true) {m_bDrawWater = bDraw;}
    void setDrawLand(bool bDraw = true) {m_bDrawLand = bDraw;}
    bool isPreviewTile() const {return m_bPreviewTile;}
    void setPreviewTile(bool bDraw = true) {m_bPreviewTile = bDraw;}
    void showOutliner(bool bShow = true);
    void pickQuickAccessTile(int index);
    void endTileBrushGroup();
    bool applyTerrainHeightPreview(const STerrainHeightCommand& command, bool forward);
    void loadSession();
    bool onExit();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void refreshMobDirtyStates();
    void initShaders();
    void draw();
    CNode* pickObject(QList<CNode*>& aNode, int x, int y);
    void applyParam(SParam& param);
    void getColorFromRect(const QRect& rect, QVector<SColor>& aColor);
    void onParamChangeLogic(CNode* pNode, const QSharedPointer<IPropertyBase>& prop);
    void logOpenGlData();
    void checkOpenGlError();
    void drawTilePreview(QOpenGLShaderProgram* program);
    void updateTileForm(bool bGenerateNewTable = false);
    void saveSession();
    bool closeMobScriptWindow(CMob* mob);
    bool closeAllMobScriptWindows();
    bool confirmScriptReferencedObjectIds(const QVector<uint>& objectIds,
                                          const QString& operation);
    void sculptTerrainAt(const QVector3D& position);
    void finishSculptStroke();
    void syncLandscapeToolState();


public slots:
    void updateWindow();
    void updateParameter(EObjParam param);
    void viewParameters();
    void updateReadState(EReadState state); //get signal from reading texture/objects/map/mob
    void onParamChange(const QSharedPointer<IPropertyBase>& prop);
    void onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>& properties);
    void collectObjectTreeData();
    void onRestoreCursor();
    void onChangeCursorTile(QPixmap ico);
    void onMapMaterialUpdate();
    void onRestoreTileData();
    void setTile(QMap<STileLocation, STileInfo>& arrTileData);

    void execMobSwitch();
    void clearHistory();
    void onMobParamEditFinished(CMobParameters* pMob);
    void checkNewLandVersion();
    void importExternalMobScript(CMob* mob, const QString& storedScript,
                                 const QString& displayScript);
    void importTerrainHeightmap();

signals:
    void updateMsg(QString msg);
    void unloadMob(CMob*);
    void updateMainWindowTitle(eTitleTypeData, QString);
    void showOutlinerSignal(bool bShow);
    void showLandscapeToolsSignal();
    void mobScriptChanged(CMob* mob, const QString& displayScript);
    void externalMobScriptImported(CMob* mob, const QString& displayScript);

private:
    int m_height; //for redraw window
    int m_width;
    QSharedPointer<CCamera> m_cam;
    QOpenGLShaderProgram m_program;
    QOpenGLShaderProgram m_landProgram;
    QOpenGLShaderProgram m_selectProgram;
    QMatrix4x4 m_projection;
    QVector<CMob*> m_aMob;
    QTimer* m_timer;
    CSettings* m_pSettings;
    QVector<bool> m_aReadState;
    QSharedPointer<CTableManager> m_tableManager;
    QUndoStack* m_pUndoStack;
    CProgressView* m_pProgress;
    QSharedPointer<COperation> m_pOp;
    QMap<CNode*, QVector3D> m_operationBackup;
    QMap<CUnit*, QVector<QVector3D>> m_unitMovementBackup;
    bool m_pendingOperationOwnsPaste;
    bool m_pendingLogicPlacementMacro;
    CMob* m_pendingPasteMob;
    QList<CNode*> m_pendingPasteNodes;
    QList<CNode*> m_pendingPasteOriginalSelection;
    //EOperationType m_operationType;
    QSharedPointer<CSelectFrame> m_selectFrame;
    QFile m_clipboard_buffer_file;
    QFile m_recentOpenedFile_file;
    CMob* m_activeMob;
    CTreeView* m_pTree;
    QList<CMobParameters*> m_arrParamWindow;
    QSharedPointer<CExternalScriptWorkspaceManager> m_scriptWorkspaces;
    QHash<CMob*, QPointer<CMobScriptWindow>> m_scriptWindows;
    CRoundMobForm* m_pRoundForm;
    QTimer* m_mprModifyTimer;
    QDateTime m_lastModifiedLand;
    bool m_bDrawLand;
    bool m_bDrawWater;
    bool m_bPreviewTile;
    CTileForm* m_pTileForm;
    QSharedPointer<CPreviewTile> m_pPreviewTile;
    CLandscape* m_pLand;
    short m_tileBrushCommandId;
    CLandscapeTools* m_landscapeTools;
    CMprDocument m_mprDocument;
    CTerrainEditModel m_terrainEdit;
    bool m_terrainEditAvailable;
    bool m_sculptStrokeActive;
    QVector3D m_lastSculptPoint;
    QVector3D m_lastSculptStampPoint;
    QTimer* m_sculptRepeatTimer;
    bool m_tileGridVisible;
};

#endif // MYGLWIDGET_H

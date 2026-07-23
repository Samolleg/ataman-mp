#ifndef MYGLWIDGET_H
#define MYGLWIDGET_H

#include <QGLWidget>
#include <QOpenGLShader>
#include <QOpenGLTimerQuery>
#include <QFileInfo>
#include <QTimer>
#include <QSharedPointer>
#include <QTableWidget>
#include <QTreeWidgetItem>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QOpenGLBuffer>
#include <QElapsedTimer>
#include <QRectF>
#include <QSet>
#include <QStringList>

#include "types.h"
#include "select_window.h"
#include "spatial_grid.h"
#include "particle_animation.h"

class QLineEdit;
class QFrame;
class QLabel;
class QComboBox;
class QSlider;
class QToolButton;
class QResizeEvent;
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
class CLight;
class QOpenGLFramebufferObject;
class RenderQueueContext;
class CViewFrustum;
class CPreviewTile;
class CLandscape;
class CExternalScriptWorkspaceManager;
class CMobScriptWindow;
struct SMobParameterState;
namespace ei { struct SRenderEnvironment; struct SScenePointLight; }

///
/// \brief The CView class is the main class for managing and editing 3D scene contents. It is also a link between read out *.mob files and their editing. It also controls the camera and current operations.
/// \todo Separate "core" functionality from rendering so that you can use the editor without using the UI
///
class CView : public QGLWidget
{
    Q_OBJECT

public:
    struct SNidRepair
    {
        QString mobPath;
        QString sectionPath;
        quint32 oldNid = 0;
        quint32 newNid = 0;
    };
    explicit CView(QWidget* parent=nullptr) = delete;
    explicit CView(QWidget* parent=nullptr, const QGLWidget* pShareWidget=nullptr);
    ~CView();
    bool prepareForApplicationShutdown();

    void loadLandscape(const QFileInfo& filePath);
    void refreshAfterResourceSetup();
    void unloadLand(bool promptForSave = true);
    void saveLand();
    void saveLandAs();
    bool loadMob(QFileInfo& filePath);
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
    CSettings* settings() {Q_ASSERT(m_pSettings); return m_pSettings;}
    int select(const SSelect& selectParam, bool bAddToSelect = false);
    CMob* mob(QString mobName);
    const QVector<CMob*> mobs() const {return m_aMob;}
    CMob* ownerMob(CNode* node) const;
    bool isMobVisible(CMob* mob) const;
    void setMobVisible(CMob* mob, bool visible);
    void setWorkspaceVisibility(const QList<CMob*>& mobs,
                                bool includeLandscape, bool visible);
    bool isLandscapeVisible() const { return m_landscapeVisible; }
    void setLandscapeVisible(bool visible);
    bool unloadMobDocument(CMob* mob, bool promptForSave = true);
    void drawSelectFrame(QRect& rect);
    void pickObject(QPoint mousePos, bool bAddToSelect);
    void pickObject(const QRect& rect, bool bAddToSelect);
    QVector3D getTerrainPos(bool bLand = true);
    QVector3D getTerrainPos(const int cursorPosX, const int cursorPosY,
                            bool bLand = true, bool* hit = nullptr);
    bool terrainPositionAt(const QPoint& viewportPosition, QVector3D* position);
    QPoint operationAnchor() const;
    void setTestSpawnMarker(const QVector3D& position, bool adjusted = false);
    void setTestSpawnMarkerSelected(bool selected);
    bool testSpawnMarkerAt(const QPoint& viewportPosition, int padding = 8) const;
    void clearTestSpawnMarker();
    void setMapSkyType(const QString& skyType,
                       const QString& region = QStringLiteral("gipat"));
    void setTestEntranceOverlays(const QVector<QRectF>& rectangles,
                                 const QStringList& labels,
                                 int selectedIndex);
    void moveCameraToPoint(const QVector3D& position);
    void anchorCameraRotationToViewportCenter();
    void changeOperation(EButtonOp type);
    void operationSetBackup(EOperationAxisType operationType);
    void operationRevert(EOperationAxisType operationType);
    void operationApply(EOperationAxisType operationType);
    bool cancelPendingOperation();
    bool hasPendingOperation() const;
    bool prepareUndo();
    void moveTo(QVector3D& dir);
    void rotateTo(QVector3D& rot);
    void scaleTo(QVector3D& scale);
    void resetUnitLogicPaths();
    void deleteSelectedNodes();
    void selectedObjectToClipboardBuffer();
    void clipboradObjectsToScene();
    void duplicateSelectedObjects();
    bool canConvertSelectedWorldObjectToLever() const;
    void convertSelectedWorldObjectToLever();
    void hideSelectedNodes();
    void unHideAll();
    CMob* currentMob() {return m_activeMob;}
    bool hasEditableSelection() const;
    QOpenGLShaderProgram& shaderObject() {return m_program;}
    void setDirtyMpr();
    void setDurty(CMob* pMob = nullptr);
    void resetCamPosition();
    double cameraSpeedMultiplier() const;
    void setCameraSpeedMultiplier(double multiplier);
    bool overridesWorldTime() const;
    double effectiveWorldTimeHours() const;
    void addLogicPoint(bool bLookPoint = false);
    void copySelectedIDsToClipboard();
    void changeCurrentMob(CMob* pMob);
    void changeCurrentMob(QString mobName);
    bool activateSelectedObjectMob();
    bool selectNode(CNode* node, bool addToSelection = false,
                    bool activateOwner = false);
    bool selectNodes(const QList<CNode*>& nodes, bool addToSelection = false);
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
    bool selectObjectByProvenance(const QString& mobPath, quint32 nid,
                                  const QString& sectionPath);
    void clearObjectSelection();
    void recordSelectionChange(const QList<CNode*>& before, const QString& text = QString("Select objects"));
    void setRandomComplection(const EObjParam param, const float min, const float max);
    void resetSelectedId();
    bool applyFamilyNidRepairs(const QVector<SNidRepair>& repairs, QString* error = nullptr);
    quint32 suggestFamilyNid(CMob* mob, quint32 localSuggestion);
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
    void loadSession();
    bool onExit();
    QString captureShadowDiagnostics(QString* error = nullptr);

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
    void resizeEvent(QResizeEvent* event) override;

private:
    bool saveMobDocument(CMob* mob, const QFileInfo& destination);
    bool saveLandscapeDocument(const QFileInfo& destination);
    void refreshMobDirtyStates();
    void initShaders();
    void draw();
    void drawTestOverlaysOpenGl();
    CNode* pickObject(QList<CNode*>& aNode, int x, int y);
    void applyParam(SParam& param);
    void getColorFromRect(const QRect& rect, QVector<SColor>& aColor);
    void onParamChangeLogic(CNode* pNode, const QSharedPointer<IPropertyBase>& prop);
    void logOpenGlData();
    void armOpenGlDiagnostics(const QString& reason, int frameCount = 4);
    bool logOpenGlErrors(const QString& stage);
    void logOpenGlDiagnosticState(const QString& stage, bool samplePixel = false);
    void logShaderProgramDiagnostics(const QString& name,
                                     const QOpenGLShaderProgram& program,
                                     bool success);
    void drawTilePreview(QOpenGLShaderProgram* program);
    void updateTileForm(bool bGenerateNewTable = false);
    void saveSession();
    bool closeMobParameterWindow(CMob* mob);
    bool closeAllMobParameterWindows();
    bool closeMobScriptWindow(CMob* mob);
    bool closeAllMobScriptWindows();
    bool confirmScriptReferencedObjectIds(const QVector<uint>& objectIds,
                                          const QString& operation);
    void rebuildSpatialIndices();
    QVector<CNode*> spatiallyVisibleNodes(CMob* mob, const CViewFrustum* frustum,
                                          SSpatialQueryStats* stats);
    QString sceneSourceFingerprint();
    void drawSkybox(const QMatrix4x4& cameraMatrix,
                    const ei::SRenderEnvironment& environment);
    void drawParticles(const QMatrix4x4& cameraMatrix,
                       const CViewFrustum* frustum);
    bool ensureParticleRendererInitialized();
    void initEnvironmentPreviewWidget();
    void applyEnvironmentCompatibilityMode();
    void positionEnvironmentPreviewWidget();
    void syncEnvironmentPreviewWidget(float mapTimeHours);
    void applyEnvironmentUniforms(QOpenGLShaderProgram& program,
                                  const ei::SRenderEnvironment& environment,
                                  const QVector<ei::SScenePointLight>& lights,
                                  int lightingMode, bool fogEnabled);
    bool renderShadowMap(const QVector3D& cameraPosition,
                         const QVector3D& shadowFocus,
                         const ei::SRenderEnvironment& environment,
                         RenderQueueContext& queue,
                         float maximumDistance);
    bool ensureShadowFramebuffers(int quality);
    void releaseShadowFramebuffers();
    void applyShadowUniforms(QOpenGLShaderProgram& program, bool enabled,
                             bool receiveStatic = true,
                             bool receiveDynamic = true);


public slots:
    void updateWindow();
    void applyRenderScheduling();
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

signals:
    void updateMsg(QString msg);
    void unloadMob(CMob*);
    void selectionChanged();
    void updateMainWindowTitle(eTitleTypeData, QString);
    void showOutlinerSignal(bool bShow);
    void mobScriptChanged(CMob* mob, const QString& displayScript);
    void externalMobScriptImported(CMob* mob, const QString& displayScript);
    void selectionStateChanged(bool hasSelection);
    void mobStateChanged();
    void loadedMapStateChanged();
    void nidChangeProposed(quint32 nid, const QString& mobPath);
    void nidAllocationRequested(const QString& mobPath, quint32* suggestion);
    void cameraSpeedMultiplierChanged(double multiplier);

private:
    int m_height; //for redraw window
    int m_width;
    QSharedPointer<CCamera> m_cam;
    QOpenGLShaderProgram m_program;
    QOpenGLShaderProgram m_landProgram;
    QOpenGLShaderProgram m_selectProgram;
    QOpenGLShaderProgram m_skyProgram;
    QOpenGLBuffer m_skyBuffer{QOpenGLBuffer::VertexBuffer};
    int m_skyVertexCount{0};
    bool m_skyResourcesChecked{false};
    bool m_skyResourcesAvailable{false};
    QOpenGLShaderProgram m_particleProgram;
    QOpenGLBuffer m_particleBuffer{QOpenGLBuffer::VertexBuffer};
    bool m_particleInitializationAttempted{false};
    bool m_particleRendererReady{false};
    QHash<const CNode*, ei::particle::TendriEmitter> m_tendriEmitters;
    QOpenGLShaderProgram m_shadowProgram;
    QOpenGLFramebufferObject* m_shadowFbo{nullptr};
    QOpenGLFramebufferObject* m_dynamicShadowFbo{nullptr};
    QOpenGLFramebufferObject* m_farShadowFbo{nullptr};
    QOpenGLFramebufferObject* m_farDynamicShadowFbo{nullptr};
    int m_shadowResolution{0};
    QMatrix4x4 m_staticLightSpaceMatrix;
    QMatrix4x4 m_dynamicLightSpaceMatrix;
    QMatrix4x4 m_farStaticLightSpaceMatrix;
    QMatrix4x4 m_farDynamicLightSpaceMatrix;
    float m_nearShadowSplit{64.0f};
    bool m_shadowCascadeDiagnostic{false};
    QElapsedTimer m_shadowUpdateTimer;
    bool m_shadowInitialized{false};
    QByteArray m_staticShadowFingerprint;
    QByteArray m_farStaticShadowFingerprint;
    QOpenGLTimerQuery* m_gpuTimer;
    bool m_gpuTimerPending;
    QMatrix4x4 m_projection;
    QVector<CMob*> m_aMob;
    // World lighting belongs to the loaded map, not to whichever MOB happens
    // to be selected for editing. Keep the first loaded document as the
    // environment owner until it is unloaded.
    CMob* m_environmentMob{nullptr};
    bool m_caveEnvironment{false};
    QString m_weatherRegion{QStringLiteral("gipat")};
    QSet<CMob*> m_hiddenMobs;
    QTimer* m_timer;
    CSettings* m_pSettings;
    QVector<bool> m_aReadState;
    QSharedPointer<CTableManager> m_tableManager;
    QUndoStack* m_pUndoStack;
    int m_lastUndoStackIndex{-1};
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
    bool m_landscapeVisible{true};
    bool m_bPreviewTile;
    CTileForm* m_pTileForm;
    QSharedPointer<CPreviewTile> m_pPreviewTile;
    CLandscape* m_pLand;
    short m_tileBrushCommandId;
    QPoint m_lastViewportMousePos;
    bool m_hasViewportMousePos;
    QVector3D m_testSpawnMarker;
    bool m_testSpawnMarkerValid;
    bool m_testSpawnMarkerAdjusted;
    bool m_testSpawnMarkerSelected{false};
    QVector<QRectF> m_testEntranceRectangles;
    QStringList m_testEntranceLabels;
    QVector<QImage> m_testEntranceLabelImages;
    QVector<GLuint> m_testEntranceLabelTextures;
    bool m_testEntranceLabelTexturesDirty{false};
    int m_selectedTestEntrance{-1};
    QHash<CMob*, QSharedPointer<CSpatialGrid<CNode*>>> m_spatialIndices;
    QHash<CMob*, QVector<CNode*>> m_spatialOverflowNodes;
    bool m_spatialIndexDirty;
    bool m_sceneFingerprintDirty;
    QString m_sceneFingerprint;
    QFrame* m_environmentPreviewWidget{nullptr};
    QWidget* m_environmentPreviewBody{nullptr};
    QToolButton* m_environmentPreviewCollapse{nullptr};
    QComboBox* m_environmentPreviewMode{nullptr};
    QFrame* m_timeOfDayWidget{nullptr};
    QLabel* m_timeOfDayLabel{nullptr};
    QSlider* m_environmentPreviewTime{nullptr};
    bool m_environmentPreviewSyncing{false};
    bool m_openGlesCompatibility{false};
    int m_openGlDiagnosticFramesRemaining{0};
    quint64 m_openGlDiagnosticFrame{0};
    QString m_openGlDiagnosticReason;
};

#endif // MYGLWIDGET_H

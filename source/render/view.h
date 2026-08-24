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
#include <QPolygonF>
#include <QSet>
#include <QStringList>
#include <QFutureWatcher>
#include <functional>

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
inline uint qHash(const QPoint& point, uint seed = 0)
{
    return qHash(qMakePair(point.x(), point.y()), seed);
}
#endif

#include "types.h"
#include "select_window.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"
#include "terrain_resolve_audit.h"
#include "terrain_tile_information_policy.h"
#include "landscape_edit_policy.h"
#include "terrain_save_flow_policy.h"
#include "terrain_atlas_analyzer.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_profile.h"
#include "landscape_tools.h"
#include "spatial_grid.h"
#include "particle_animation.h"
#include "ai_graph_document.h"
#include "ai_graph_preview_builder.h"
#include "unit_visual_preparation.h"
#include "minimap_bake.h"
#include "tile_brush_stroke_policy.h"
#include "shadow_cascade.h"
#include "frame_presentation_telemetry.h"

class QLineEdit;
class QFrame;
class QLabel;
class QComboBox;
class QSlider;
class QToolButton;
class QCheckBox;
class QDialog;
class QResizeEvent;
class CKeyManager;
class CMob;
class CCamera;
class CNode;
class CTile;
class CUnit;
class CMagicTrap;
class CKeyManager;
class CSettings;
class CStringItem;
class QUndoStack;
class QThreadPool;
struct SParam;
class CComboBoxItem;
class CProgressView;
class CTableManager;
class COperation;
struct SColor;
struct SMprSaveCommitResult;
struct SResArchiveNewMember;

struct STerrainPaintAuditAction
{
    int actionId{-1};
    int strokeId{-1};
    QString outcome;
    QString validation;
    STerrainSemanticPaintAudit semantic;
    QVector<STerrainTileDelta> deltas;
};
class CSelectFrame;
class QTreeWidget;
class CMobParameters;
class CRoundMobForm;
class CTreeView;
class IPropertyBase;
class CTileForm;
class CLandscapeTools;
class CTerrainWorkingAtlas;
struct SWorkingTerrainResolveStage;
class CLight;
class QOpenGLFramebufferObject;
class RenderQueueContext;
class CViewFrustum;
class CPreviewTile;
class CLandscape;
class CExternalScriptWorkspaceManager;
class CMobScriptWindow;
struct SMobParameterState;

struct SMinimapSceneCaptureResult
{
    SMinimapBakeResult bake;
    QImage unlitGameImage;
    int sectorWidth{0};
    int sectorHeight{0};
    int staticObjectCount{0};
    int hiddenStaticObjectCount{0};
    int excludedDynamicUnitCount{0};
    bool staticGeometrySourcePresent{false};
    QString framingDescription;
    QString orientationDescription;
    int targetTextureSize{256};
    bool targetDxt1{false};
    QString exportProfileDescription;
};
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
    void setWorkspaceLoadInProgress(bool inProgress);
    void unloadLand(bool promptForSave = true);
    void saveLand();
    void saveLandAs();
    void saveModernDraftAs();
    bool buildValidatedLandscapeCandidate(QByteArray* data,
                                          CValidationReport* report = nullptr,
                                          QByteArray* terrainAtlasResource = nullptr,
                                          int* terrainAtlasPages = nullptr,
                                          int* terrainAtlasSlots = nullptr);
    QString activeTerrainArchivePath() const
    {
        return m_activeTerrainArchivePath;
    }
    bool buildGeneratedAiGraphCandidate(QByteArray* data,
                                        QString* error = nullptr,
                                        QString* sourceDescription = nullptr,
                                        bool forceRegeneration = false);
    void refreshAiGraphOwnership();
    bool loadMob(QFileInfo& filePath);
    void saveMobAs();
    void saveActiveMob();
    void saveAllMob();
    bool saveMobDocument(CMob* mob, const QFileInfo& destination);
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
    bool hasPendingTerrainPublication() const
    {
        return modernTerrainPublicationRequired(true);
    }
    ETerrainMprSavePresentation terrainMprSavePresentation() const;
    void unloadMob(QString mobName);
    void attach(CSettings* pSettings, QTableWidget* pParam, QUndoStack* pStack, CProgressView* pProgress, QLineEdit* pMouseCoord, CTreeView* pTree, CTileForm* pTileForm);
    void attachLandscapeTools(CLandscapeTools* tools);
    void activateLandscapeTilePainting();
    void prepareLandscapeTilePainting(bool showTools = true);
    void setLandscapeEditingEnabled(bool enabled);
    void setLandscapeInteractionSuspended(bool suspended);
    void closeLandscapeTools();
    void setTileGridVisible(bool visible) { m_tileGridVisible = visible; update(); }
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
    QVector3D getTerrainBrushPos(bool* hit = nullptr);
    QVector3D getTerrainBrushPos(const int cursorPosX, const int cursorPosY,
                                 bool* hit = nullptr, bool forcePick = false);
    // The position returned by getTerrainBrushPos already records which
    // surface supplied its XY/Z. Footprint admission must query that same
    // layer or a valid hover can disappear before preview/commit.
    static bool terrainPaintPickUsesLandLayer(bool land,
                                              bool projectionLand)
    {
        return land || (!land && projectionLand);
    }
    bool terrainPositionAt(const QPoint& viewportPosition, QVector3D* position,
                           bool land = true);
    bool projectToRegeneratedNavigation(const QPointF& requested,
                                        QVector3D* position,
                                        float maximumDistance = 8.0f) const;
    QPolygonF minimapCameraFootprint() const;
    void moveCameraToMinimapPoint(const QPointF& worldPosition);
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
    bool entranceOverlaysVisible() const { return m_entranceOverlaysVisible; }
    void refreshUiScale();
    void moveCameraToPoint(const QVector3D& position);
    void anchorCameraRotationToViewportCenter();
    void changeOperation(EButtonOp type,
                         bool preserveLandscapeEditing = false);
    void operationSetBackup(EOperationAxisType operationType);
    void operationRevert(EOperationAxisType operationType);
    void operationApply(EOperationAxisType operationType);
    bool cancelPendingOperation();
    bool prepareDocumentTransition();
    bool prepareHistoryNavigation();
    bool hasPendingOperation() const;
    bool prepareUndo();
    void moveTo(QVector3D& dir);
    void rotateTo(QVector3D& rot);
    void scaleTo(QVector3D& scale);
    void resetUnitLogicPaths();
    void deleteSelectedNodes();
    void selectedObjectToClipboardBuffer();
    void clipboradObjectsToScene();
    bool pasteBehaviorPoints(const QJsonObject& clipboard);
    bool copyLogicSlot(CUnit* owner, int logicSlot);
    bool pasteLogicSlot(const QJsonObject& clipboard, bool preserveWorldPositions = false);
    bool pasteLogicSlotFromClipboard(bool preserveWorldPositions = false);
    void selectBehaviorParent();
    void selectPreviousBehaviorPoint();
    void selectNextBehaviorPoint();
    void duplicateSelectedObjects();
    bool canConvertSelectedWorldObjectToLever() const;
    void convertSelectedWorldObjectToLever();
    void hideSelectedNodes();
    void unHideAll();
    CMob* currentMob() {return m_activeMob;}
    int logicSlotIndex() const { return m_logicSlotIndex; }
    void setLogicSlotIndex(int index);
    bool hasEditableSelection() const;
    QOpenGLShaderProgram& shaderObject() {return m_program;}
    void setDirtyMpr();
    void setDurty(CMob* pMob = nullptr);
    void refreshBehaviorOrder(CNode* owner, int logicSlot);
    void resetCamPosition();
    double cameraSpeedMultiplier() const;
    void setCameraSpeedMultiplier(double multiplier);
    bool overridesWorldTime() const;
    double effectiveWorldTimeHours() const;
    void refreshSelectContextHelp();
    SMinimapSceneCaptureResult captureMinimapScene(
        bool includeLoadedStaticGeometry, int targetTextureSize = 256);
    void addLogicPoint(bool bLookPoint = false);
    void copySelectedIDsToClipboard();
    void changeCurrentMob(CMob* pMob);
    void changeCurrentMob(QString mobName);
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
    void refreshDatabaseVisuals(const QString& tableKey = QString(),
                                const QString& rowKey = QString());
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
    void openMprMaterials();
    void openAtlasTiles();
    bool pickTile(QVector3D posOnLand, bool bLand = true);
    void beginTerrainPaintSampling();
    void endTerrainPaintSampling();
    void setTile(QVector3D posOnLand, bool bLand = true);
    void updatePreviewTile(QVector3D posOnLand, bool bLand = true);
    void addTileRotation(int step);
    void adjustTerrainPaintSize(int steps);
    void adjustTerrainPaintMaterial(int steps);
    void selectTerrainPaintPaletteMode(ETerrainPaintPaletteMode mode);
    bool manualTilePaintingEnabled() const;
    float terrainPaintStampSpacing() const;
    bool terrainTileEditLayerIsLand() const;
    bool terrainBrushProjectsToLand() const;
    void setDrawWater(bool bDraw = true);
    void setDrawLand(bool bDraw = true);
    bool drawWater() const { return m_bDrawWater; }
    bool drawLand() const { return m_bDrawLand; }
    bool isPreviewTile() const {return m_bPreviewTile;}
    void setPreviewTile(bool bDraw = true) {m_bPreviewTile = bDraw;}
    void showOutliner(bool bShow = true);
    void pickQuickAccessTile(int index);
    void beginTileBrushGroup();
    void endTileBrushGroup();
    void cancelTileBrushGroup();
    void beginTerrainPreviewBatch();
    void endTerrainPreviewBatch();
    bool applyTerrainHeightPreview(const STerrainHeightCommand& command, bool forward);
    bool applyTerrainVerticalScalePreview(const STerrainVerticalScaleCommand& command,
                                          bool forward);
    bool applyTerrainTilePreview(const STerrainTileCommand& command, bool forward);
    bool terrainTileCommandInvalidatesAiGraph(
        const STerrainTileCommand& command) const;
    bool applyOverlaySurfacePreview(const SOverlaySurfaceCommand& command, bool forward);
    bool applyMprParametersPreview(const QVector<SMprMaterialRecord>& materials,
                                   const QVector<SMprAnimatedTileRecord>& animatedTiles);
    bool applyMprParametersPreviewTransaction(
        const QVector<SMprMaterialRecord>& materials,
        const QVector<SMprAnimatedTileRecord>& animatedTiles,
        const QSharedPointer<CTerrainWorkingAtlas>& atlasState,
        const STerrainTileCommand& slotRemapCommand, bool forward);
    void onLandscapeToolsShown();
    void loadSession();
    void setSessionWorkspaceIdentity(const QString& kind, const QString& identifier,
                                     const QString& displayName, const QString& mapZoneId);
    QString sessionWorkspaceKind() const { return m_sessionWorkspaceKind; }
    QString sessionWorkspaceIdentifier() const { return m_sessionWorkspaceIdentifier; }
    QString sessionWorkspaceDisplayName() const { return m_sessionWorkspaceDisplayName; }
    QString sessionWorkspaceMapZoneId() const { return m_sessionWorkspaceMapZoneId; }
    bool onExit();
    QString captureShadowDiagnostics(QString* error = nullptr);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    CUnit* behaviorCollectionOwner(const QList<CNode*>& selection) const;
    CMagicTrap* trapCollectionOwner(const QList<CNode*>& selection) const;
    void closeMprSettingsDock();
    void refreshMprSettingsDock();
    bool mprParametersDirty() const;
    void syncMprDirtyState();
    bool saveLandscapeDocument(const QFileInfo& destination);
    bool saveChangedLandscapeDocument(const QFileInfo& destination,
                                      bool adoptAsOpenBaseline,
                                      bool allowModernDraft = false);
    bool modernTerrainPublicationRequired(bool saveAs = false) const;
    ETerrainMprSaveRoute currentTerrainMprSaveRoute(bool saveAs) const;
    bool persistedModernDraftRequiresPublication() const;
    bool adoptCommittedLandscapeBaseline(const QFileInfo& destination,
                                         const SMprSaveCommitResult& commit);
    bool adoptCompleteTerrainBaseline(
        const QFileInfo& destination, const QString& expectedSha256,
        const QVector<QImage>& physicalTiles,
        const QString& terrainArchivePath,
        const STerrainAtlasProfile& expectedProfile,
        bool requireSemanticMember);
    bool buildTerrainSemanticFieldMember(
        const QByteArray& candidateMprBytes,
        const QFileInfo& destinationMpr,
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        const QByteArray& profileBytes,
        SResArchiveNewMember* member,
        CValidationReport* report = nullptr) const;
    bool restoreTerrainSemanticFieldFromArchive(
        const QFileInfo& mprFile, const QString& terrainArchivePath,
        bool requireMember = false);
    bool restoreTerrainSemanticFieldFromArchive(
        const CMprDocument& document,
        const STerrainAtlasProfile& profile,
        CTerrainEditModel* terrain,
        const QFileInfo& mprFile, const QString& terrainArchivePath,
        bool requireMember) const;
    void refreshMobDirtyStates();
    void initShaders();
    void draw(SFramePresentationTelemetry* frameTelemetry = nullptr);
    void scheduleUnitAppearancePreparation(const QVector3D& cameraPosition,
                                           bool cameraMoving);
    void drawTestOverlaysOpenGl();
    void rebuildOverlayMaterialSlotLabels();
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
    void armSceneFrameTelemetry();
    void markObservedSceneFrameFlicker();
    void drawTilePreview(QOpenGLShaderProgram* program);
    void drawTerrainPaintOutline(QOpenGLShaderProgram* program);
    bool terrainCellAtViewport(const QPoint& viewportPosition, QPoint* cell);
    void showTerrainTileInformation(const QPoint& viewportPosition);
    void appendMarkedTerrainPaintIssue(const QSet<QPoint>& cells,
                                       const QString& category);
    void updateTileForm(bool bGenerateNewTable = false);
    void saveSession();
    bool closeMobParameterWindow(CMob* mob);
    bool closeAllMobParameterWindows();
    bool closeMobScriptWindow(CMob* mob);
    bool closeAllMobScriptWindows();
    bool confirmScriptReferencedObjectIds(const QVector<uint>& objectIds,
                                          const QString& operation);
    void sculptTerrainAt(const QVector3D& position, float stampFraction);
    void sculptTerrainTowardLatestPoint();
    void updateOverlaySculptPointer();
    void updateLandscapeSculptCoordinates(const QVector3D& position, bool valid);
    quint16 rawTerrainHeight(float worldHeight) const;
    void finishSculptStroke();
    void syncLandscapeToolState();
    bool handleLandscapeShortcut(QKeyEvent* event, bool pressed);
    void clearLandscapeBrushSelection();
    void selectAdjacentBehaviorPoint(int offset);
    void ensureManualTileSelection();
    void deferTerrainAutoCatalogBuild(const QFileInfo& mprFile);
    void queueDeferredTerrainAutoCatalogBuild();
    bool ensureTerrainAutoCatalogReady();
    void rebuildTerrainAutoCatalog();
    void startDeferredGlobalTerrainPaletteHydration(
        const QString& currentNamespace, const QSize& requiredSize,
        quint64 generation);
    void applyDeferredGlobalTerrainPaletteHydration(quint64 generation);
    void refreshClassicAutoPalette();
    void refreshManualAtlasPalette(bool invalidate = true);
    void refreshWorkingTerrainPalettes();
    void refreshTerrainFamilyUsage();
    int activeTerrainAtlasSlotCount() const;
    bool terrainResolveUiActive() const;
    void requestResolveMissingTerrainBlends(ETerrainResolveAuditTrigger trigger);
    void resolveMissingTerrainBlends();
    QVector<STerrainAtlasSlotUsage> terrainAtlasUsage() const;
    QMap<int, quint64> workingTerrainAtlasUsage() const;
    bool terrainLayerUsesWorkingExtension(ETerrainTileLayer layer) const;
    bool realizeWorkingTerrainSignature(
        const STerrainTransitionSignature& signature, int cellX, int cellY,
        QMap<int, quint64>* usage, CValidationReport* report,
        bool* generated = nullptr,
        qint32 preferredPaintMaterial = 0,
        qint32 preferredRuntimeType = -1,
        SWorkingTerrainResolveStage* staging = nullptr,
        bool requirePreferredRuntimeType = false);
    bool ensureGlobalTerrainMaterialSources(
        const STerrainTransitionSignature& signature,
        CValidationReport* report,
        SWorkingTerrainResolveStage* staging = nullptr);
    bool syncWorkingTerrainAtlas(CValidationReport* report,
                                 CTerrainWorkingAtlas* staging = nullptr);
    bool ensureModernTerrainArtworkAssigned(
        ETerrainTileLayer layer,
        const CTerrainTransitionCatalog& catalog);
    void refreshTerrainTilePreview(const STerrainTileCommand& command,
                                   bool forward);
    void reviewTerrainAtlasProfile();
    void reviewTerrainAtlasProfileAtSlot(int initialSlot);
    void finalizeVirtualTerrainAtlas();
    void updateTerrainAtlasViewerAnnotations(const QString& status);
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
    void initAiGraphPreviewWidget();
    void applyEnvironmentCompatibilityMode();
    void positionEnvironmentPreviewWidget();
    void syncVisualChannelButtons();
    void updateVisualChannelAvailability();
    void persistViewportVisualState();
    bool isObjectVisualVisible(CNode* node) const;
    void refreshAiGraphPreviewSource();
    void syncAiGraphPreviewWidget();
    void rebuildAiGraphOverlayGeometry();
    void startAiGraphPreviewRegeneration();
    void beginAiGraphPreviewRegeneration(bool selectRegenerated);
    void scheduleAiGraphPreviewRegeneration();
    void stopAiGraphPreviewAutoRegeneration();
    bool captureAiGraphPreviewBuildInput(SAiGraphPreviewBuildInput* input,
                                         QString* error);
    void syncEnvironmentPreviewWidget(float mapTimeHours);
    void applyEnvironmentUniforms(QOpenGLShaderProgram& program,
                                  const ei::SRenderEnvironment& environment,
                                  const QVector<ei::SScenePointLight>& lights,
                                  int lightingMode, bool fogEnabled);
    bool renderShadowMap(const QVector3D& cameraPosition,
                         const QVector3D& shadowFocus,
                         const ei::SRenderEnvironment& environment,
                         const QVector<ei::SScenePointLight>& sceneLights,
                         RenderQueueContext& queue,
                         float maximumDistance,
                         SFramePresentationTelemetry* frameTelemetry);
    bool ensureShadowFramebuffers(int quality);
    void releaseShadowFramebuffers();
    bool beginEditorQueryFramebuffer();
    void endEditorQueryFramebuffer();
    void applyShadowUniforms(QOpenGLShaderProgram& program, bool enabled,
                             bool receiveStatic = true,
                             bool receiveDynamic = true);
    bool resolveTerrainPaintFootprint(QVector3D position, bool land,
                                      CTile** tile, STileLocation* location,
                                      STerrainTileBrush* brush,
                                      QPoint* detailVertex,
                                      STerrainPaintFootprintSnapshot* footprint);


public slots:
    void updateWindow();
    void applyRenderScheduling();
    void updateParameter(EObjParam param);
    void viewParameters();
    void updateReadState(EReadState state); //get signal from reading texture/objects/map/mob
    void onParamChange(const QSharedPointer<IPropertyBase>& prop);
    void onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>& properties);
    void collectObjectTreeData();
    void refreshLogicSlotOutliner();
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
    void exportTerrainPaintDiagnostic();

signals:
    void updateMsg(QString msg);
    void unloadMob(CMob*);
    void selectionChanged();
    void updateMainWindowTitle(eTitleTypeData, QString);
    void showOutlinerSignal(bool bShow);
    void showLandscapeToolsSignal();
    void hideLandscapeToolsSignal();
    void mobScriptChanged(CMob* mob, const QString& displayScript);
    void externalMobScriptImported(CMob* mob, const QString& displayScript);
    void selectionStateChanged(bool hasSelection);
    void openDatabasePrototypeRequested(const QString& prototype);
    void mobStateChanged();
    void loadedMapStateChanged();
    void mprSaveStateChanged();
    void nidChangeProposed(quint32 nid, const QString& mobPath);
    void nidAllocationRequested(const QString& mobPath, quint32* suggestion);
    void cameraSpeedMultiplierChanged(double multiplier);
    void entranceOverlayVisibilityChanged(bool visible);
    void unitVisualPreparationComplete();
    void aiGraphPersistenceRequested();

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
    QHash<const CNode*, ei::particle::VulcanSmokeEmitter> m_vulcanSmokeEmitters;
    QHash<const CNode*, ei::particle::FireballEmitter> m_fireballEmitters;
    QHash<const CNode*, ei::particle::FireBlastEmitter> m_fireBlastEmitters;
    QHash<const CNode*, ei::particle::HealingEmitter> m_healingEmitters;
    QHash<const CNode*, ei::particle::PoisonFogEmitter> m_poisonFogEmitters;
    QHash<const CNode*, ei::particle::GeyserEmitter> m_geyserEmitters;
    QHash<const CNode*, ei::particle::MushroomEmitter> m_mushroomEmitters;
    QHash<const CNode*, ei::particle::Blood1Emitter> m_blood1Emitters;
    QHash<const CNode*, ei::particle::A4BloodEmitter> m_a4BloodEmitters;
    QHash<const CNode*, ei::particle::FireArrowEmitter> m_fireArrowEmitters;
    QHash<const CNode*, ei::particle::AcidRayEmitter> m_acidRayEmitters;
    QHash<const CNode*, ei::particle::BlueGasEmitter> m_blueGasEmitters;
    QHash<const CNode*, ei::particle::LinkEmitter> m_linkEmitters;
    QHash<const CNode*, ei::particle::SphereAcidEmitter> m_sphereAcidEmitters;
    QHash<const CNode*, ei::particle::SphereElectricityEmitter> m_sphereElectricityEmitters;
    QHash<const CNode*, ei::particle::SphereFireEmitter> m_sphereFireEmitters;
    QHash<const CNode*, ei::particle::AntimagicEmitter> m_antimagicEmitters;
    QHash<const CNode*, ei::particle::ModifierEmitter> m_modifierEmitters;
    QHash<const CNode*, ei::particle::ClayRingEmitter> m_clayRingEmitters;
    QHash<const CNode*, ei::particle::TeleportEmitter> m_teleportEmitters;
    QHash<const CNode*, ei::particle::ZoneExitEmitter> m_zoneExitEmitters;
    QHash<const CNode*, ei::particle::PathEmitter> m_pathEmitters;
    QHash<const CNode*, ei::particle::PathDestinationEmitter> m_pathDestinationEmitters;
    QHash<const CNode*, ei::particle::MoshkaEmitter> m_moshkaEmitters;
    QHash<const CNode*, ei::particle::PortalStarEmitter> m_portalStarEmitters;
    QHash<const CNode*, ei::particle::PortalEmitter> m_portalEmitters;
    QHash<const CNode*, ei::particle::CylinderEmitter> m_cylinderEmitters;
    QHash<const CNode*, ei::particle::StarFlashEmitter> m_starFlashEmitters;
    QHash<const CNode*, ei::particle::SparksEmitter> m_sparksEmitters;
    QHash<const CNode*, ei::particle::VisionStarEmitter> m_visionStarEmitters;
    QHash<const CNode*, ei::particle::RegenerationEmitter> m_regenerationEmitters;
    QHash<const CNode*, ei::particle::SilenceEmitter> m_silenceEmitters;
    QHash<const CNode*, ei::particle::FeeblemindEmitter> m_feeblemindEmitters;
    QHash<const CNode*, ei::particle::FeetCloudEmitter> m_feetCloudEmitters;
    QHash<const CNode*, ei::particle::BallStarsEmitter> m_ballStarsEmitters;
    QHash<const CNode*, ei::particle::CurseStarsEmitter> m_curseStarsEmitters;
    QHash<const CNode*, ei::particle::StartTransEmitter> m_startTransEmitters;
    QHash<const CNode*, ei::particle::CastingGatherEmitter> m_castingGatherEmitters;
    QHash<const CNode*, ei::particle::CastingStarRoundEmitter> m_castingRoundEmitters;
    QHash<const CNode*, ei::particle::CastingFailedEmitter> m_castingFailedEmitters;
    QHash<const CNode*, ei::particle::TornadoEmitter> m_tornadoEmitters;
    QHash<const CNode*, ei::particle::LightningBlastEmitter> m_lightningBlastEmitters;
    QHash<const CNode*, ei::particle::CastingEmitter> m_castingEmitters;
    QHash<const CNode*, ei::particle::NukeEmitter> m_nukeEmitters;
    QHash<const CNode*, ei::particle::BansheeCastingEmitter> m_bansheeCastingEmitters;
    QOpenGLShaderProgram m_shadowProgram;
    QOpenGLFramebufferObject* m_shadowFbo{nullptr};
    QOpenGLFramebufferObject* m_dynamicShadowFbo{nullptr};
    QOpenGLFramebufferObject* m_farShadowFbo{nullptr};
    QOpenGLFramebufferObject* m_farDynamicShadowFbo{nullptr};
    QOpenGLFramebufferObject* m_localShadowFbo{nullptr};
    QOpenGLFramebufferObject* m_editorQueryFbo{nullptr};
    bool m_editorQueryFramebufferFailureLogged{false};
    int m_shadowResolution{0};
    QMatrix4x4 m_staticLightSpaceMatrix;
    QMatrix4x4 m_dynamicLightSpaceMatrix;
    QMatrix4x4 m_farStaticLightSpaceMatrix;
    QMatrix4x4 m_farDynamicLightSpaceMatrix;
    QVector<QMatrix4x4> m_localShadowMatrices;
    QVector<QVector4D> m_localShadowAtlasRects;
    QVector<quintptr> m_localShadowOwners;
    float m_nearShadowSplit{64.0f};
    bool m_shadowCascadeDiagnostic{false};
    QElapsedTimer m_shadowUpdateTimer;
    QElapsedTimer m_overlayAnimationTimer;
    bool m_shadowInitialized{false};
    QByteArray m_staticShadowFingerprint;
    QByteArray m_farStaticShadowFingerprint;
    QOpenGLTimerQuery* m_gpuTimer;
    bool m_gpuTimerPending;
    QMatrix4x4 m_projection;
    QVector3D m_visualWarmupCameraAnchor;
    QVector3D m_visualWarmupCameraPivot;
    QVector3D m_visualWarmupCameraRotation;
    bool m_visualWarmupCameraValid{false};
    bool m_minimapCaptureActive{false};
    bool m_minimapCaptureUnlitPass{false};
    bool m_minimapCaptureIncludeStaticGeometry{false};
    QMatrix4x4 m_minimapCaptureView;
    QVector3D m_minimapCaptureCameraPosition;
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
    QList<CNode*> m_pendingLogicPlacementOriginalSelection;
    CMob* m_pendingPasteMob;
    QList<CNode*> m_pendingPasteNodes;
    QList<CNode*> m_pendingPasteOriginalSelection;
    QList<CNode*> m_pendingBehaviorPasteNodes;
    QList<CNode*> m_pendingBehaviorPasteOriginalSelection;
    QString m_pendingBehaviorPasteKind;
    //EOperationType m_operationType;
    QSharedPointer<CSelectFrame> m_selectFrame;
    QFile m_clipboard_buffer_file;
    QFile m_recentOpenedFile_file;
    CMob* m_activeMob;
    int m_logicSlotIndex{0};
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
    QPointer<QDialog> m_mprSettingsDock;
    std::function<void()> m_refreshMprSettingsDock;
    QSharedPointer<CPreviewTile> m_pPreviewTile;
    CLandscape* m_pLand;
    int m_tileBrushCommandId;
    CLandscapeTools* m_landscapeTools;
    CMprDocument m_mprDocument;
    CTerrainEditModel m_terrainEdit;
    QVector<SMprMaterialRecord> m_mprMaterials;
    QVector<SMprAnimatedTileRecord> m_mprAnimatedTiles;
    STerrainAtlasAnalysis m_terrainAtlasAnalysis;
    STerrainAtlasProfile m_terrainAtlasProfile;
    STerrainAtlasProfileSource m_terrainAtlasProfileSource;
    QVector<QImage> m_authoredTerrainTileImages;
    QSharedPointer<CTerrainWorkingAtlas> m_workingTerrainAtlas;
    bool m_terrainAutoCatalogPending{false};
    bool m_terrainAutoCatalogBuildQueued{false};
    bool m_terrainAutoCatalogBuilding{false};
    QFileInfo m_pendingTerrainAutoCatalogMprPath;
    QElapsedTimer m_terrainAutoCatalogLoadTimer;
    qint64 m_terrainAutoCatalogFirstFrameMs{-1};
    bool m_manualAtlasPaletteDirty{true};
    bool m_deferManualAtlasPalette{false};
    bool m_workingAtlasPaletteRefreshPending{false};
    STerrainAtlasEmbeddedBindingResult
        m_prevalidatedTerrainEmbeddedBinding;
    bool m_prevalidatedTerrainTilesAdopted{false};
    QSet<int> m_workingAtlasProtectedSlots;
    QString m_lastAtlasReviewPromptBinding;
    QMap<qint32, int> m_terrainMaterialRepresentatives;
    QVector<STerrainGlobalMaterialFamily> m_terrainGlobalFamilies;
    QVector<STerrainPaintMaterialChoice> m_modernLandPaintChoices;
    QVector<STerrainPaintMaterialChoice> m_modernUnderwaterPaintChoices;
    QVector<STerrainPaintMaterialChoice> m_modernOverlayPaintChoices;
    QMap<qint32, QImage> m_terrainGlobalMaterialSources;
    QMap<qint32, STerrainGlobalMaterialSource>
        m_terrainGlobalMaterialSourceIdentities;
    QMap<QString, QImage> m_terrainGlobalUniformTileCache;
    QMultiMap<QByteArray, STerrainGlobalTransitionSource>
        m_terrainGlobalTransitionSources;
    QMap<QString, STerrainAtlasArchiveBindingResult>
        m_terrainGlobalBoundAtlasCache;
    QSet<QString> m_terrainGlobalTransitionMissingPages;
    QMap<QString, QImage> m_terrainGlobalTransitionTileCache;
    QThreadPool* m_terrainGlobalHydrationPool{nullptr};
    quint64 m_terrainGlobalHydrationGeneration{0};
    int m_terrainGlobalHydrationPendingJobs{0};
    bool m_terrainGlobalHydrationPublishQueued{false};
    QMap<qint32, int> m_terrainGlobalHydrationBestRanks;
    QMap<qint32, QImage> m_terrainGlobalHydrationBestThumbnails;
    QMap<qint32, STerrainGlobalMaterialSource>
        m_terrainGlobalHydrationBestSources;
    QString m_terrainPaintPaletteStatus;
    QString m_terrainPaintPaletteSourceLabel;
    QString m_terrainPaintPaletteSourcePath;
    QMultiMap<QByteArray, QPair<STerrainTransitionSignature, QImage>>
        m_importedTerrainTransitions;
    QVector<STerrainTileDelta> m_terrainPaintAudit;
    STerrainTileCommand m_tileStrokeCommand;
    SOverlaySurfaceCommand m_overlaySurfaceStrokeCommand;
    SOverlaySurfaceSnapshot m_overlaySurfaceStrokeBaseline;
    STerrainTileCommand m_classicAutoStrokePreviewCommand;
    QSet<QPair<int, int>> m_classicAutoStrokeVertices;
    QSet<QPair<int, int>> m_classicAutoStrokeAnchors;
    bool m_classicAutoStrokePending{false};
    ETerrainTileLayer m_classicAutoStrokeLayer{ETerrainTileLayer::Land};
    qint32 m_classicAutoStrokeMaterialId{-1};
    QVector<STerrainPaintAuditAction> m_terrainPaintActions;
    int m_terrainPaintActionId{0};
    ETerrainResolveAuditTrigger m_pendingTerrainResolveAuditTrigger{
        ETerrainResolveAuditTrigger::Programmatic};
    quint64 m_terrainResolveAuditActionId{0};
    quint32 m_terrainSemanticSeed{0};
    quint32 m_tileStrokeDistributionSeed{0};
    QString m_requestedTerrainResourceIdentity;
    bool m_requestedTerrainIdentityReplacement{false};
    bool m_requestedTerrainIdentityFinalization{false};
    QFileInfo m_requestedTerrainMprDestination;
    QString m_activeTerrainArchivePath;
    bool m_legacyTerrainProfileCollision{false};
    bool m_terrainEditAvailable;
    bool m_landscapeInteractionSuspended{false};
    CTileBrushStrokePolicy m_tileBrushStrokePolicy;
    bool m_terrainAssignmentPromptPending{false};
    bool m_tilePaintStrokeActive{false};
    bool m_tileStrokeFinishing{false};
    bool m_tileStrokeClassicAuto{false};
    bool m_terrainPaintOutlineValid{false};
    bool m_terrainPaintOutlineGeometryDirty{true};
    QVector<QVector3D> m_terrainPaintOutlineVertices;
    bool m_terrainPaintProjectionLand{true};
    STerrainPaintFootprintSnapshot m_terrainPaintFootprint;
    bool m_terrainPaintFootprintValid{false};
    bool m_terrainSampleGestureActive{false};
    bool m_terrainSampleKeyValid{false};
    QPoint m_lastTerrainSamplePoint;
    int m_lastTerrainSampleLayer{-1};
    int m_lastTerrainSampleMode{-1};
    bool m_paintIssueDragActive{false};
    bool m_paintIssueHoverValid{false};
    CTerrainTileInformationPolicy m_terrainTileInformationPolicy;
    bool m_paintIssueTooltipVisible{false};
    QPoint m_paintIssueHoverCell;
    QSet<QPoint> m_paintIssueSelectedCells;
    QPoint m_lastTerrainPaintCenter;
    bool m_lastTerrainPaintCenterValid{false};
    bool m_lastTerrainPaintLayerLand{true};
    int m_tileStrokeActions{0};
    int m_tileStrokeApplied{0};
    int m_tileStrokeNoOps{0};
    int m_tileStrokeRefused{0};
    int m_tileStrokeChangedTiles{0};
    int m_tileStrokeSolverNodes{0};
    int m_tileStrokeMinimumX{0};
    int m_tileStrokeMinimumY{0};
    int m_tileStrokeMaximumX{0};
    int m_tileStrokeMaximumY{0};
    int m_tileStrokeMaximumCenterJump{0};
    bool m_tileStrokeBoundsValid{false};
    qint64 m_overlayCommandNsecs{0};
    qint64 m_overlayModelNsecs{0};
    qint64 m_overlayRenderNsecs{0};
    qint64 m_overlayPickNsecs{0};
    int m_overlayPickCount{0};
    int m_overlayPreviewSectorCount{0};
    QElapsedTimer m_overlayBrushPickTimer;
    QPoint m_overlayBrushPickCursor;
    QVector3D m_overlayBrushPickPosition;
    bool m_overlayBrushPickValid{false};
    bool m_overlayBrushPickProjectionLand{true};
    int m_tileStrokePaletteMode{-1};
    int m_tileStrokeTargetMode{-1};
    bool m_tileStrokeSafeMode{false};
    qint32 m_tileStrokeMaterialId{-1};
    ETerrainTileLayer m_tileStrokeLayer{ETerrainTileLayer::Land};
    int m_tileStrokeSafeSkippedVertices{0};
    int m_tileStrokeSafeSkippedBoundaryCells{0};
    bool m_tileStrokeUndoCheckpointed{false};
    QString m_classicAutoStrokeValidation;
    bool m_classicAutoStrokePreviewRefused{false};
    bool m_terrainPreviewBatchActive{false};
    QSet<QPair<int, int>> m_pendingTerrainPreviewSectors;
    QMap<QPair<int, int>, QByteArray> m_pendingTerrainPreviewBefore;
    QSet<QPair<int, int>> m_pendingOverlayPreviewSectors;
    QMap<QPair<int, int>, QByteArray> m_pendingOverlayPreviewBefore;
    QSet<QPair<int, int>> m_pendingTilePreviewSectors;
    bool m_sculptOutlineValid{false};
    bool m_sculptOutlineGeometryDirty{true};
    QVector<QVector3D> m_sculptOutlineVertices;
    QVector3D m_sculptOutlineCenter;
    bool m_sculptStrokeActive;
    bool m_sculptGestureCaptured{false};
    bool m_sculptPointerValid{false};
    STerrainHeightCommand m_sculptStrokeCommand;
    QVector3D m_lastSculptPoint;
    QVector3D m_lastSculptStampPoint;
    bool m_sculptStrokeChanged{false};
    int m_sculptStrokeStamps{0};
    int m_sculptStrokeVertices{0};
    quint32 m_sculptStrokeNoiseSeed{0};
    QTimer* m_sculptRepeatTimer;
    QTimer* m_overlayPointerTimer;
    bool m_tileGridVisible;
    int m_lastLandscapeEditMode;
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
    QVector<QVector3D> m_overlayMaterialSlotLabelPositions;
    QVector<int> m_overlayMaterialSlotLabelSlots;
    QVector<QImage> m_overlayMaterialSlotLabelImages;
    QVector<GLuint> m_overlayMaterialSlotLabelTextures;
    bool m_overlayMaterialSlotLabelsDirty{true};
    bool m_overlayMaterialSlotLabelTexturesDirty{true};
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
    QToolButton* m_entranceVisualToggle{nullptr};
    QToolButton* m_objectHelpersVisualToggle{nullptr};
    QToolButton* m_staticObjectsVisualToggle{nullptr};
    QToolButton* m_dynamicObjectsVisualToggle{nullptr};
    QToolButton* m_terrainVisualToggle{nullptr};
    QToolButton* m_overlaysVisualToggle{nullptr};
    QToolButton* m_fogVisualToggle{nullptr};
    QToolButton* m_sceneLightsVisualToggle{nullptr};
    QToolButton* m_terrainShadowsVisualToggle{nullptr};
    QToolButton* m_objectShadowsVisualToggle{nullptr};
    QToolButton* m_skyboxVisualToggle{nullptr};
    QToolButton* m_particleVisualsToggle{nullptr};
    QCheckBox* m_freezeAnimationsToggle{nullptr};
    QFrame* m_timeOfDayWidget{nullptr};
    QLabel* m_timeOfDayLabel{nullptr};
    QSlider* m_environmentPreviewTime{nullptr};
    QFrame* m_aiGraphPreviewWidget{nullptr};
    QWidget* m_aiGraphPreviewBody{nullptr};
    QToolButton* m_aiGraphPreviewToggle{nullptr};
    QToolButton* m_aiGraphPreviewCollapse{nullptr};
    QToolButton* m_aiGraphRegenerateButton{nullptr};
    QToolButton* m_aiGraphCompareButton{nullptr};
    QToolButton* m_aiGraphSourceSelectorButton{nullptr};
    QToolButton* m_aiGraphPersistButton{nullptr};
    QSlider* m_aiGraphLayerSlider{nullptr};
    QLabel* m_aiGraphLayerLabel{nullptr};
    QLabel* m_aiGraphSourceLabel{nullptr};
    CAiGraphDocument m_aiGraphDocument;
    CAiGraphDocument m_aiGraphLoadedDocument;
    QByteArray m_aiGraphLoadedBytes;
    CAiGraphDocument m_aiGraphRegeneratedDocument;
    SAiGraphPreviewBuildResult m_aiGraphRegeneratedResult;
    QString m_aiGraphSource;
    QString m_aiGraphLoadedSource;
    QString m_aiGraphRegeneratedSource;
    QFutureWatcher<SAiGraphPreviewBuildResult>* m_aiGraphBuildWatcher{nullptr};
    QFutureWatcher<QVector<SUnitAppearancePreparationResult>>* m_unitAppearanceWatcher{nullptr};
    QTimer* m_unitVisualPreparationTimer{nullptr};
    QHash<quintptr, QPointer<CUnit>> m_unitAppearanceTargets;
    bool m_unitAppearanceBatchPending{false};
    int m_unitAppearanceProgressTotal{0};
    int m_unitAppearanceProgressCompleted{0};
    int m_unitAppearanceCacheHits{0};
    int m_unitAppearanceSharedPreparations{0};
    int m_unitAppearanceProviderExecutions{0};
    int m_unitAppearanceLastLoggedCompleted{-1};
    bool m_unitAppearanceProgressPending{false};
    bool m_unitVisualPreparationSuspended{false};
    QTimer* m_aiGraphRegenerationTimer{nullptr};
    quint64 m_aiGraphBuildRequest{0};
    bool m_aiGraphAutoRegenerationEnabled{false};
    bool m_aiGraphRegenerationPending{false};
    bool m_aiGraphRegeneratedFresh{false};
    bool m_aiGraphPreviewUsesLoadedSource{true};
    bool m_aiGraphPreviewVisible{false};
    bool m_aiGraphOverlayDirty{true};
    QVector<QVector3D> m_aiGraphOpenLinks;
    QVector<QVector3D> m_aiGraphNodes;
    QVector<QVector3D> m_aiGraphIsolatedNodes;
    bool m_environmentPreviewSyncing{false};
    bool m_entranceOverlaysVisible{true};
    bool m_objectHelpersVisible{true};
    bool m_staticObjectsVisible{true};
    bool m_dynamicObjectsVisible{true};
    bool m_particleVisualsVisible{true};
    bool m_fogVisible{true};
    bool m_sceneLightsVisible{true};
    bool m_terrainShadowsVisible{true};
    bool m_objectShadowsVisible{true};
    bool m_skyboxVisible{true};
    bool m_freezeAnimations{false};
    qint64 m_animationFreezeTime{-1};
    bool m_openGlesCompatibility{false};
    int m_openGlDiagnosticFramesRemaining{0};
    quint64 m_openGlDiagnosticFrame{0};
    QString m_openGlDiagnosticReason;
    CFramePresentationHistory m_sceneFrameTelemetryHistory{512};
    quint64 m_sceneFrameTelemetryFrame{0};
    quint64 m_sceneFrameTelemetrySession{0};
    QElapsedTimer m_sceneFrameTelemetryTimer;
    bool m_lastRenderedCameraFinite{true};
    QString m_sessionWorkspaceKind;
    QString m_sessionWorkspaceIdentifier;
    QString m_sessionWorkspaceDisplayName;
    QString m_sessionWorkspaceMapZoneId;
};

#endif // MYGLWIDGET_H

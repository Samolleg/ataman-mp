#ifndef MYGLWIDGET_H
#define MYGLWIDGET_H
#include "travel_area_geometry.h"
#include "map_save_batch.h"
#include "camera_document.h"
#include "technical_model_catalog.h"
#include "endurance_document.h"
#include "render_memory.h"

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
#include <QPointF>
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
#include "unit_support_surface.h"
#include "particle_animation.h"
#include "ai_graph_document.h"
#include "ai_graph_preview_builder.h"
#include "unit_visual_preparation.h"
#include "minimap_bake.h"
#include "topdown_capture.h"
#include "tile_brush_stroke_policy.h"
#include "shadow_cascade.h"
#include "frame_presentation_telemetry.h"
#include "map_catalog.h"
#include "neighbor_map_layout.h"
#include "world_map_snap.h"
#include "neighbor_scene_data.h"
#include "neighbor_render_cache.h"
#include <atomic>
#include "world_terrain_session.h"
#include "world_material_boundary.h"
#include "terrain_mob_height_state.h"

class QLineEdit;
class QFrame;
class QLabel;
class QComboBox;
class QScrollArea;
class QSlider;
class QToolButton;
class QCheckBox;
class QDialog;
class QJsonObject;
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
class QUndoCommand;
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
struct SWorkingTerrainResolveStage
{
    QSharedPointer<CTerrainWorkingAtlas> atlas;
    CTerrainEditModel terrainEdit;
    QSet<int> protectedSlots;
    QMultiMap<QByteArray, QPair<STerrainTransitionSignature, QImage>>
        importedTransitions;
    QMap<QString, STerrainAtlasArchiveBindingResult> boundAtlasCache;
    QSet<QString> missingPages;
    QMap<QString, QImage> decodedTileCache;
    QMap<QString, QImage> uniformTileCache;
    QMap<qint32, QImage> materialSources;
    QMap<qint32, STerrainGlobalMaterialSource> materialSourceIdentities;
    bool undoCheckpointRequired{false};
    bool allowSlotRecycling{true};
};

struct SWorldTerrainAtlasSource {
    STerrainAtlasProfile profile;
    QString archive;
    QVector<QByteArray> pages;
};
class CLight;
class QOpenGLFramebufferObject;
class RenderQueueContext;
class CNavmeshPanel;
class QMainWindow;
class QMenu;
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
    QImage capturedReviewImage;
    QImage capturedUnlitReviewImage;
    QImage preparedGameImage;
    QByteArray preparedMmp;
    SMinimapGradeSettings preparedGrade;
};
namespace ei { struct SRenderEnvironment; struct SScenePointLight; }

///
/// \brief The CView class is the main class for managing and editing 3D scene contents. It is also a link between read out *.mob files and their editing. It also controls the camera and current operations.
/// \todo Separate "core" functionality from rendering so that you can use the editor without using the UI
///
struct STerrainRecoveryCandidate
{
    bool success{false};
    QByteArray mprBytes, atlasBytes;
    CValidationReport report;
};

#include "process_edit_lock.h"
class CView : public QGLWidget
{
    friend class CAiReadAdapter;
    friend class CAiEditAdapter;
    friend class CAiAgentCommand;
    friend class CAiObjectCommand;
    friend class CAiLifecycleCommand;
    friend class CAiAgentServer;
    friend class CTopdownExport;
    QSharedPointer<CProcessEditLock> m_landEditLease;
    QMap<QString,QSharedPointer<CProcessEditLock>> m_mobEditLeases;
    Q_OBJECT
    friend int runDatabasePreviewCheck();
    friend int runScenarioLayerCheck();
    friend int runDiplomacyCheck();
    friend int runObjectHierarchyCheck();
    friend int runGroupRotationCheck();
    friend int runProfileModelCatalogCheck();
    friend int runModelConnectionCheck();
    friend int runAssemblyCatalogCheck();
    friend int runObjectRandomizationCheck();
    friend int runWindowLayoutCheck();
    friend int runStartCameraCheck();
    friend bool patrolAuthoringIntegrationCheck(CView* view);
    friend bool terrainBaselineAnimationIntegrationCheck(CView* view);
    friend int runNeighborPerformanceCheck();
    friend int runTerrainMobHeightCheck();
    friend int runTerrainHeightResSaveCheck();
    friend int runPointerModesCheck();
    friend int runWorldMapSnapCheck();
    friend int runAiGraphRegenerationCheck();

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
    void stopForApplicationShutdown();
    void suspendRendering() { if(m_timer)m_timer->stop(); }
    void resumeRendering() { if(!m_applicationShutdown && isVisible() && m_timer)m_timer->start(); }
    bool shutdownWorkersFinished() const;
    QSize activeTerrainSectorSize() const {
        return QSize(m_mprDocument.mapInfo().sectorCountX,m_mprDocument.mapInfo().sectorCountY);
    }

    void loadLandscape(const QFileInfo& filePath, bool createRecoverySnapshot = true);
    void refreshAfterResourceSetup();
    void setWorkspaceLoadInProgress(bool inProgress);
    void unloadLand(bool promptForSave = true);
    void setTravelAreas(const QVector<STravelArea>& areas,int selectedExit);
    void setTravelEditing(bool enabled);
    bool travelEditing() const {return m_travelEditing;}
    void beginTravelArea(bool remove,bool newExit);
    void focusTravelArea(int exitIndex,bool remove);
    void cancelTravelGesture();
    void saveLand();
    bool saveMapCompanions(const QVector<SMapSaveFile>& files, QString* error, const QString& backupRoot = QString(), const std::function<void()>& committed = {});
    void saveLandAs();
    void saveModernDraftAs();
    bool buildValidatedLandscapeCandidate(QByteArray* data,
                                          CValidationReport* report = nullptr,
                                          QByteArray* terrainAtlasResource = nullptr,
                                          int* terrainAtlasPages = nullptr,
                                          int* terrainAtlasSlots = nullptr,
                                          std::function<STerrainRecoveryCandidate()>* recoveryTask = nullptr);
    std::function<STerrainRecoveryCandidate()> prepareLandscapeRecoveryTask(CValidationReport* report);
    QString activeTerrainArchivePath() const
    {
        return m_activeTerrainArchivePath;
    }
    bool buildGeneratedAiGraphCandidate(QByteArray* data,
                                        QString* error = nullptr,
                                        QString* sourceDescription = nullptr,
                                        bool forceRegeneration = false);
    void refreshAiGraphOwnership();
    bool canvasNavigationStale() const { return m_canvasNavigationStale; }
    bool clearCanvasNavigationStale(QString* error = nullptr);
    bool applyCanvasResizeToCurrent(const SMprCanvasResizeRequest& resize,
                                    bool removeOutsideObjects, QString* error = nullptr);
    bool loadMob(QFileInfo& filePath, QStringList* nonModalWarnings = nullptr);
    void adoptLoadedMob(CMob* mob, const QSharedPointer<CProcessEditLock>& lease, bool activate);
    void saveMobAs();
    void saveActiveMob();
    void saveAllMob();
    bool saveMobDocument(CMob* mob, const QFileInfo& destination);
    void unloadActiveMob();
    void openActiveMobEditParams();
    void openMobDiplomacyTable(CMob* mob);
    void setDiplomacyMapMode(bool enabled);
    bool diplomacyMapMode() const { return m_diplomacyMapMode; }
    void clearDiplomacyAnchor();
    bool focusDiplomacyNpc(const QString& source, const QString& key);
    QColor diplomacyNpcColor(CUnit* unit) const;
    bool editDiplomacyRelation(const QString& source, int row, int column,
                              uint value, bool symmetric, uint expected, uint expectedReverse);
    bool createDiplomacyTable(const QString& source);
    void openActiveMobScript();
    QString scenarioScriptText(CMob* mob, bool* draft = nullptr) const;
    bool openScenarioSource(CMob* mob, const QString& analyzedText, int offset);
    void setScenarioOverlay(const QVector<QVector3D>& participants,
                            const QVector<QVector3D>& spawns,
                            const QVector<QVector3D>& destinations,
                            const QVector<QVector3D>& links);
    bool scenarioMarkerPosition(QVector3D* position, bool hasZ) const;
    void focusScenarioPosition(const QVector3D& position);
    void openExternalMobScript(CMob* mob);
    void commitMobParameterEdit(CMob* mob, const SMobParameterState& before,
                                const SMobParameterState& after);
    void commitMobScriptEdit(CMob* mob, const QString& storedScript,
                             const QString& commandText);
    void malachiteHistoryStep(bool redo);
    void notifyMobScriptChanged(CMob* mob);
    bool hasActiveMob() const { return m_activeMob != nullptr; }
    bool hasDirtyWorldTerrain() const { return !m_worldTerrainSession.dirtyPaths().isEmpty(); }
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
    void createNavmeshMenu(QMenu* menu);
    CNavmeshPanel* navmeshPanel() const { return m_navmeshPanel; }
    void navigationMapSaved(const QString& path, bool navigationChanged);
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
    bool objectPlacementPositionAt(const QPoint& viewportPosition, QVector3D* position);
    bool unitPlacementPositionAt(const QPoint& viewportPosition, QVector3D* position);
    QVector3D movementPointerPosition(const QPoint& viewportPosition);
    void moveAlongUnitSurface(QVector3D direction);
    void ensureUnitSupportSurface();
    CUnitSupportSurface m_unitSupportSurface;
    quint64 m_unitSupportRevision{0};
    QVector<CMob*> m_unitSupportMobs;
    void setLeverApproachOverlay(const QVector<QVector3D>& lines) { m_leverApproachLines=lines; update(); }
    bool leverApproachScreenPoint(const QVector3D& world, QPointF* screen) const { return connectionScreenPoint(world,screen); }
    bool projectToRegeneratedNavigation(const QPointF& requested,
                                        QVector3D* position,
                                        float maximumDistance = 8.0f) const;
    QPolygonF minimapCameraFootprint() const;
    void moveCameraToMinimapPoint(const QPointF& worldPosition);
    bool captureCameraLocation(QVector3D* position, QVector3D* pivot,
                               QVector3D* rotation) const;
    bool captureCamPose(SCameraPose* pose) const;
    void setCameraTrack(const QVector<SCameraPose>& poses, int selected, bool preview);
    void drawCameraTrack(int positionAttribute);
    void rebaseCameraForOpenedNeighbor(
        const QPointF& sourceWorldOrigin,
        const QPointF& targetWorldOrigin,
        const QVector3D& sourcePosition,
        const QVector3D& sourcePivot,
        const QVector3D& sourceRotation);
    QPoint operationAnchor() const;
    bool beginDatabasePlacement(CNode* prototype, QString* diagnostic = nullptr);
    bool beginTechnicalPlacement(const QJsonObject& descriptor,QString* diagnostic = nullptr);
    bool beginPreparedAssemblyPlacement(const ei::ObjectAssembly& assembly, const QPoint& cursor, const QVector3D& anchor, QString* error = nullptr, bool groupMembers = true);
    bool captureSelectedAssembly(const QString& name, ei::ObjectAssembly* assembly, QString* error = nullptr);
    bool beginAssemblyPlacement(const ei::ObjectAssembly& assembly, QString* error = nullptr, bool groupMembers = true);
    bool beginGalleryPlacement(const QVector<QPair<QString,QString>>& entries,QString* error = nullptr);
    void openObjectGallery();
    void openAssemblyCatalog();
    void saveSelectionAsAssembly();
    void groupSelectedAssembly(const QString& name, const QString& catalogId = QString());
    void disassembleSelection();
    QList<CNode*> assemblySelection(const QList<CNode*>& nodes);
    void normalizeAssemblySelection(const QList<CNode*>& before);
    static void setAssemblyWorldPosition(CNode* node, const QVector3D& world);
    bool isDatabasePlacementPending() const { return m_pendingDatabasePlacement; }
    bool updateDatabasePlacementPointer(const QPoint& position);
    bool isLogicPlacementPending() const { return m_pendingLogicPlacementMacro; }
    bool updateLogicPlacementPointer(const QPoint& position);
    void setTestSpawnMarker(const QVector3D& position, bool adjusted = false);
    void setTestSpawnMarkerSelected(bool selected);
    bool testSpawnMarkerAt(const QPoint& viewportPosition, int padding = 8) const;
    void clearTestSpawnMarker();
    void setMapSkyType(const QString& skyType,
                       const QString& region = QStringLiteral("gipat"));
    void setTestEntranceOverlays(const QVector<QRectF>& rectangles,
                                 const QStringList& labels,
                                 int selectedIndex);
    void setNeighborMapContext(
        const QVector<SMapCatalogEntry>& topology,
        const QVector<SWorldMapZoneOrigin>& worldOrigins,
        const QString& profileId, const QString& activeAllodId,
        const QString& activeZoneId,
        const QString& layoutWarning = QString());
    void setNeighborCacheTransition(bool active);
    void deferNeighborLoading(bool deferred);
    bool entranceOverlaysVisible() const { return m_entranceOverlaysVisible; }
    void setWorldTravelAreas(const QVector<STravelArea>& areas);
    bool projectTravelOverlay(int index, QVector3D& point) const;
    void beginWorldMapPlacement(const QSizeF& size);
    void beginWorldMapPlacement(const QSizeF& size, const QPointF& anchor, bool hasAnchor);
    void cancelWorldMapPlacement();
    void showWorldMap(const QString& mapId);
    void previewWorldMapOrigins(const QMap<QString,QPointF>& origins);
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
    void setModelMagnetEnabled(bool enabled);
    void setModelConnectionSource(const QString& id);
    void cycleModelConnectionTarget();
    void updateModelMagnet(const QPoint& pointer);
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
    void beginObjectEditBatch();
    void endObjectEditBatch();
    void refreshBehaviorOrder(CNode* owner, int logicSlot);
    void resetCamPosition();
    double cameraSpeedMultiplier() const;
    void setCameraSpeedMultiplier(double multiplier);
    bool telephotoCameraEnabled() const;
    void setTelephotoCameraEnabled(bool enabled);
    double telephotoCameraMultiplier() const;
    void setTelephotoCameraMultiplier(double multiplier);
    int telephotoCameraDirection() const;
    void setTelephotoCameraDirection(int direction);
    bool overridesWorldTime() const;
    double effectiveWorldTimeHours() const;
    void refreshSelectContextHelp();
    SMinimapSceneCaptureResult captureMinimapScene(
        bool includeLoadedStaticGeometry, int targetTextureSize = 256,
        bool finishBake = true);
    static SMinimapSceneCaptureResult finishMinimapSceneCapture(SMinimapSceneCaptureResult capture);
    QImage captureTopdownPass(ETopdownPass pass, const QSize& size,
                             const QVector<SRenderItem>& items, QString* error);
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
    void randomizeSelectedObjects(int axes = 7, double strengthPercent = 25.0);
    void randomizeSelectedObjectRotation(int axis, double maximumDegrees = 25.0);
    void randomizeSelectedObjectsCombined(int axes, double strengthPercent,
                                         bool rotateX, bool rotateY, double maximumDegrees);
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
    // Non-interactive, lossless regression entry point. It executes one real
    // Modern viewport stroke against the loaded MPR, validates undo/redo, and
    // samples every frame presented during the transaction. The caller must
    // discard the in-memory document afterwards; this method never saves.
    bool terrainStampRegressionReady();
    bool runTerrainStampRegression(qint32 materialId, int width, int height,
                                   QJsonObject* result);
    void beginTerrainPreviewBatch();
    void endTerrainPreviewBatch();
    bool applyTerrainHeightPreview(const STerrainHeightCommand& command, bool forward);
    QSharedPointer<STerrainMobHeightState> makeTerrainMobHeightState() const;
    void captureTerrainMobHeights(const STerrainHeightCommand& command,
                                 STerrainMobHeightState* state);
    void applyTerrainMobHeights(STerrainMobHeightState* state, bool forward,
                               bool captureAfter = false);
    bool terrainMobHeightStateAvailable(const STerrainMobHeightState* state) const;
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
    bool convertBaseTerrainToHd(const QString& library, quint32 seed,
                               QString* summary = nullptr, bool interactive = false);
    void convertBaseTerrainToHdRequested();
    void compactTerrainAtlasRequested();
    void cleanTerrainPackRequested();
    void loadSession();
    void setSessionWorkspaceIdentity(const QString& kind, const QString& identifier,
                                     const QString& displayName, const QString& mapZoneId);
    QString sessionWorkspaceKind() const { return m_sessionWorkspaceKind; }
    QString sessionWorkspaceIdentifier() const { return m_sessionWorkspaceIdentifier; }
    QString sessionWorkspaceDisplayName() const { return m_sessionWorkspaceDisplayName; }
    QString sessionWorkspaceMapZoneId() const { return m_sessionWorkspaceMapZoneId; }
    bool onExit(bool retainWorldTerrain = false);
    bool worldTerrainHistory(bool forward, bool force = false);
    void recordWorldTerrainStroke(const SWorldTerrainStroke& stroke);
    const SWorldTerrainStroke* nextWorldTerrainStroke(bool forward) const;
    bool isWorldTerrainHistoryCommand(const QUndoCommand* command) const;
    bool validateHistoryCommand(const QUndoCommand* command, bool forward, QString* error);
    bool replayWorldTerrainStroke(const SWorldTerrainStroke& stroke, bool forward);
    QString captureShadowDiagnostics(QString* error = nullptr);

protected:
    bool event(QEvent* event) override;
    void initializeGL() override;
    void paintGL() override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeGL(int width, int height) override;
    QSize viewportFramebufferSize() const;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool stageMapCompanionFile(const QString& path,const QString& candidate,const QByteArray& expected,QString* error);
    bool stageMapCompanion(const QString& path, const QByteArray& bytes, QString* error, const QByteArray* expected = nullptr);
    bool commitMapCohortMpr(const QFileInfo& destination, const QByteArray& bytes, QString* error);
    bool m_mapCohortActive{false};
    bool m_mapCohortCommitted{false};
    QVector<SMapSaveFile> m_mapCompanions;
    QString m_mapCohortBackupRoot;
    std::function<void()> m_mapCompanionsCheckpoint;
    void acceptMapCompanionsCheckpoint();
    bool travelMousePress(QMouseEvent* event);
    bool travelMouseMove(QMouseEvent* event);
    bool travelMouseRelease(QMouseEvent* event);
    QVector<STravelArea> m_travelAreas;
    QVector<QVector3D> m_cameraTrackLines;
    bool m_camPreviewActive{false};
    bool m_travelEditing{false};
    bool m_travelSelectedRemove{false};
    bool m_travelDrawing{false};
    bool m_travelNewExit{false};
    bool m_travelRemove{false};
    bool m_travelDragging{false};
    int m_travelHit{-1};
    int m_travelHandle{-1};
    int m_travelSelectedExit{-1};
    QRectF m_travelOriginal;
    QPointF m_travelStart;

    void updateCameraProjection();
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
        bool requireSemanticMember, bool preserveUndo = false,
        const QVector<SResArchiveNewMember>* verifiedPublication = nullptr);
    bool buildTerrainSemanticFieldMember(
        const QByteArray& candidateMprBytes,
        const QFileInfo& destinationMpr,
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        const QByteArray& profileBytes,
        SResArchiveNewMember* member,
        CValidationReport* report = nullptr,
        const CTerrainEditModel* terrainEdit = nullptr,
        QByteArray* embeddedMprBytes = nullptr) const;
    bool restoreTerrainSemanticFieldFromArchive(
        const QFileInfo& mprFile, const QString& terrainArchivePath,
        bool requireMember = false);
    static bool restoreTerrainSemanticFieldFromArchive(
        const CMprDocument& document,
        const STerrainAtlasProfile& profile,
        CTerrainEditModel* terrain,
        const QFileInfo& mprFile, const QString& terrainArchivePath,
        bool requireMember);
    void refreshMobDirtyStates();
    void initShaders();
    void draw(SFramePresentationTelemetry* frameTelemetry = nullptr);
    void scheduleUnitAppearancePreparation(const QVector3D& cameraPosition,
                                           const QMatrix4x4& cameraMatrix,
                                           bool cameraMoving);
    void drawTestOverlaysOpenGl();
    void drawNavmeshOverlay(int position);
    void drawNavmeshLegend();
    bool navmeshComparisonClick(const QPoint& point);
    void syncNavmeshContext();
    CNavmeshPanel* m_navmeshPanel{nullptr};
    QString m_navmeshLegendHtml;
    QSize m_navmeshLegendSize;
    QRect m_navmeshLegendRect;
    GLuint m_navmeshLegendTexture{0};
    int m_navmeshDrawnTiles{0};
    quint64 m_navmeshDrawnVertices{0};
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
    void loadNeighborLandscapes();
    void startNeighborLoadStage(quint64 generation);
    void finishNeighborLoadStage(quint64 generation);
    void acceptNeighborScene(const SNeighborSceneData& result, quint64 generation,
        const QByteArray& terrainFingerprint, const QByteArray& objectFingerprint, bool objects);
    void populateNeighborObjects(quint64 generation);
    void chooseWorldTerrainMaps();
    void updateWorldTerrainStatus();
    void syncWorldTerrainActive();
    void restoreWorldTerrainActive();
    void refreshWorldTerrainNeighbors();
    bool saveWorldTerrainMaps();
    bool resolveWorldTerrainClose(bool retain);
    bool ensureWorldTerrainAtlas(const QString& id,CValidationReport* report);
    bool paintWorldTerrainAt(const QVector3D& position,bool land);
    bool worldTerrainTile(QVector3D& position, bool land, CTile*& tile,
                          STileLocation& location, CLandscape** landscape = nullptr,
                          QPointF* origin = nullptr) const;
    bool projectWorldTerrainPoint(QVector3D* point, bool land) const;
    bool previewWorldPaintState(const QString& id,
                               const CTerrainEditModel* previousTerrain = nullptr);
    bool eraseWorldOverlayAt(const QVector3D& position);
    bool worldMaterialNeighbors(QVector<SWorldMaterialField>* land, QVector<SWorldMaterialField>* overlay, CValidationReport* report);
    bool resolveWorldMaterialBlends(QString* summary, CValidationReport* report,
                                   STerrainResolveAuditSession* audit = nullptr);
    bool loadWorldTerrainMaterialSources(const QSet<qint32>& materials,
        const QSize& tileSize, QMap<qint32,QImage>* sources, CValidationReport* report);
    bool worldTerrainPosition(const QPoint& viewport, QVector3D* position);
    bool sculptWorldTerrainAt(const QVector3D& position, float fraction);
    void clearNeighborLandscapes(bool reloading = false);
    bool neighborMapAtViewport(const QPoint& viewportPosition,
                               SNeighborMapPlacement* placement,
                               QVector3D* hitPoint = nullptr,
                               bool preferNeighbor = false, bool land = true);
    bool viewportPointOnHorizontalPlane(const QPoint& viewportPosition,
                                        float planeZ,
                                        QPointF* worldPoint) const;
    bool updateNeighborLayoutDrag(const QPoint& viewportPosition);
    SWorldMapSnapResult snapWorldMap(const QPointF& origin, const QSizeF& size,
        const QString& excludedZone, const QPoint& pointer, float planeZ,
        const QPointF& anchor, bool hasAnchor);
    void cancelNeighborLayoutDrag(bool restoreOrigin);
    void updateNeighborLayoutStatus(const QString& zoneId = QString(),
                                    const QPointF& worldOrigin = QPointF());
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
    void queueModernTerrainFamilyPrewarm(qint32 terrainType);
    void prewarmModernTerrainFamily(qint32 terrainType,
                                    ETerrainTileLayer layer,
                                    quint64 requestGeneration);
    bool adoptPrewarmedTerrainFamily(qint32 terrainType,
                                     ETerrainTileLayer layer,
                                     CValidationReport* report);
    bool prepareExactModernTerrainStamp(STerrainTileCommand* command,
                                        qint32 terrainType,
                                        CValidationReport* report,
                                        bool* atlasPublicationPending);
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
    void retainTerrainSourceCaches(const SWorkingTerrainResolveStage& staged);
    bool ensureGlobalTerrainMaterialSources(
        const STerrainTransitionSignature& signature,
        CValidationReport* report,
        SWorkingTerrainResolveStage* staging = nullptr);
    bool syncWorkingTerrainAtlas(CValidationReport* report,
                                 CTerrainWorkingAtlas* staging = nullptr,
                                 bool presentCompletedFrame = false);
    bool primeWorkingTerrainAtlasPreview(CValidationReport* report);
    void presentCompletedTerrainPublication();
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
    void beginAiGraphPreviewRegeneration();
    void invalidateAiGraphPreview();
    void cancelPendingAiGraphPreviewRegeneration();
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
    bool drawSelectionTerrainDepth(const QMatrix4x4& viewMatrix);
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
    void diplomacyMapModeChanged(bool enabled);
    void diplomacyWindowRequested();
    void canvasResizeMobPathsRequested(QStringList* paths);
    void modelConnectionSourcesChanged(const QStringList& labels,const QStringList& ids);
    void modelConnectionStatusChanged(const QString& status);
    void modelConnectionTargetsChanged(int availablePairs);
    void travelAreaSelected(int exitIndex, bool* accepted);
    void travelDrawingStateChanged(bool active, bool remove, bool newExit);
    void travelAreaCommitted(int exitIndex,bool remove,const QRectF& rectangle,bool newExit);

    void updateMsg(QString msg);
    void unloadMob(CMob*);
    void selectionChanged();
    void updateMainWindowTitle(eTitleTypeData, QString);
    void showOutlinerSignal(bool bShow);
    void showLandscapeToolsSignal();
    void hideLandscapeToolsSignal();
    void mobScriptChanged(CMob* mob, const QString& displayScript);
    void scenarioBufferChanged();
    void scenarioSourceLocationRequested(CMob* mob, int offset);
    void externalMobScriptImported(CMob* mob, const QString& displayScript);
    void selectionStateChanged(bool hasSelection);
    void openDatabasePrototypeRequested(const QString& prototype);
    void leverApproachRequested();
    void mobStateChanged();
    void mobDocumentSaved(const QString& previousPath, const QString& savedPath);
    void loadedMapStateChanged();
    void mprSaveStateChanged();
    void terrainPublicationPreflightRequested(
        const QString& resourceArchivePath, bool* accepted);
    void terrainPublicationFinished(
        const QString& resourceArchivePath, bool committed);
    void nidChangeProposed(quint32 nid, const QString& mobPath);
    void nidAllocationRequested(const QString& mobPath, quint32* suggestion);
    void cameraSpeedMultiplierChanged(double multiplier);
    void telephotoCameraEnabledChanged(bool enabled);
    void telephotoCameraMultiplierChanged(double multiplier);
    void telephotoCameraDirectionChanged(int direction);
    void entranceOverlayVisibilityChanged(bool visible);
    void neighborMapOpenRequested(const QString& mprPath,
                                  const QString& mobPath,
                                  const QString& zoneId,
                                  const QPointF& sourceWorldOrigin,
                                  const QPointF& targetWorldOrigin);
    void neighborMapOriginChangeRequested(const QString& zoneId,
                                          const QPointF& worldOrigin);
    void neighborMapLayoutResetRequested();
    void worldMapAlignmentRequested();
    void addWorldMapRequested();
    void manageWorldMapsRequested();
    void worldMapPlacementConfirmed(const QPointF& origin);
    void worldMapPlacementCancelled();
    void unitVisualPreparationComplete();
    void aiGraphPersistenceRequested();

private:
    bool m_worldPlacementActive{false};
    bool m_worldPlacementValid{false};
    bool m_worldPlacementOverlaps{false};
    QSizeF m_worldPlacementSize;
    QPointF m_worldPlacementOrigin;
    float m_worldPlacementZ{0};
    QString m_preferredWorldMap;
    bool updateWorldMapPlacement(const QPoint& position);
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
    QMatrix4x4 m_normalProjection;
    QMatrix4x4 m_projection;
    QVector3D m_visualWarmupCameraAnchor;
    QVector3D m_visualWarmupCameraPivot;
    QVector3D m_visualWarmupCameraRotation;
    bool m_visualWarmupCameraValid{false};
    bool m_minimapCaptureActive{false};
    bool m_snapshotCaptureActive{false};
    GLuint m_snapshotFramebuffer{0};
    QSize m_snapshotSize;
    ETopdownPass m_topdownPass{ETopdownPass::None};
    const QVector<SRenderItem>* m_topdownItems{nullptr};
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
    bool m_applicationShutdown{false};
    CSettings* m_pSettings;
    QVector<bool> m_aReadState;
    QSharedPointer<CTableManager> m_tableManager;
    QUndoStack* m_pUndoStack;
    int m_lastUndoStackIndex{-1};
    CProgressView* m_pProgress;
    QSharedPointer<COperation> m_pOp;
    QMap<CNode*, QVector3D> m_operationBackup;
    int m_objectEditBatchDepth{0};
    QSet<CMob*> m_objectEditBatchMobs;
    bool m_objectEditBatchParameters{false};
    bool m_objectEditBatchTree{false};
    void prepareModelMagnet();
    bool beginPreparedObjectPlacement(const QJsonObject& descriptor,const QPoint& cursor,QString* diagnostic);
    void clearModelMagnet();
    void restoreFreeModelMove();
    bool applyModelConnection(int source,int target);
    bool connectionScreenPoint(const QVector3D& world,QPointF* screen) const;
    QVector<QVector3D> m_leverApproachLines;
    ei::TechnicalModelCatalog m_connectionCatalog;
    QVector<ei::ModelConnection> m_sourceConnections;
    struct ConnectionTarget {
        QPointer<CNode> owner;
        ei::ModelConnection point;
        ei::ConnectionPose pose;
        QVector3D world;
        QString label;
    };
    QVector<ConnectionTarget> m_connectionTargets;
    ei::ConnectionIndex m_connectionIndex;
    QMap<CNode*,QQuaternion> m_connectionOrientations;
    QVector3D m_connectionFreePosition;
    bool m_modelMagnetEnabled=false,m_modelMagnetApplied=false;
    QString m_connectionSourceId;
    QPoint m_connectionPointer;
    int m_connectionHeldTarget=-1,m_connectionHeldSource=-1;
    QVector<QPair<int,int>> m_connectionChoices;
    QVector<QVector3D> m_connectionGlyphs;
    QPointer<CNode> m_connectionGhostNode;
    QVector<QVector3D> m_connectionGhostVertices;
    QMap<CNode*, QVector3D> m_rotationPositionBackup;
    QMap<CNode*, QQuaternion> m_rotationOrientationBackup;
    QVector3D m_rotationPivot;
    QQuaternion m_rotationDelta;
    QMap<CUnit*, QVector<QVector3D>> m_unitMovementBackup;
    bool m_pendingOperationOwnsPaste;
    bool m_pendingLogicPlacementMacro;
    QList<CNode*> m_pendingLogicPlacementOriginalSelection;
    CMob* m_pendingPasteMob;
    QList<CNode*> m_pendingPasteNodes;
    bool m_pendingDatabasePlacement{false};
    bool m_pendingAssemblyPlacement{false};
    QHash<CNode*,QVector3D> m_pendingAssemblyOffsets;
    QMap<CNode*,QVector3D> m_assemblyRotationWorldBackup;
    QVector3D m_assemblyRotationPivot;
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
    QVector<QVector3D> m_scenarioParticipants, m_scenarioSpawns,
                       m_scenarioDestinations, m_scenarioLinks;
    bool m_diplomacyMapMode{false};
    QPointer<CUnit> m_diplomacyAnchor;
    QWidget* m_diplomacyOverlay{nullptr};
    void setDiplomacyAnchor(CUnit* unit);
    void updateDiplomacyOverlay();
    void drawDiplomacyObjectVisual(CNode* node, bool active);
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
    struct SLoadedNeighborLandscape
    {
        SNeighborMapPlacement placement;
        QByteArray terrainFingerprint, objectFingerprint;
        bool objectsPrepared{false};
        bool showObjects{false};
        quint64 lastUse{0};
        QSharedPointer<CLandscape> landscape;
        QMap<QPair<int,int>,QByteArray> terrainPreviewPayloads;
        QSharedPointer<CTerrainWorkingAtlas> terrainPreviewAtlas;
        QVector<QJsonObject> pendingObjects;
        QVector<QSharedPointer<const SPreparedStaticVisual>> pendingVisuals;
        int nextObject{0};
        QVector<QSharedPointer<CNode>> staticObjects;
        QVector<QSharedPointer<QOpenGLTexture>> textureLeases;
        QVector<QVector3D> staticLocalPositions;
        QVector<QVector<QSharedPointer<CPart>>> distantParts;
        QVector<SNeighborDrawRecord> drawRecords;
        QMap<QPair<int,int>,SNeighborSpatialGroup> spatialGroups;
        QPointF cachedOrigin;
        bool drawCacheDirty{true};
    };
    void refreshNeighborRenderCache(SLoadedNeighborLandscape& neighbor);
    QVector<SLoadedNeighborLandscape> m_neighborLandscapes;
    QVector<SLoadedNeighborLandscape> m_neighborRenderCache;
    quint64 m_neighborCacheClock{0};
    quint64 m_neighborCacheBudget{1536ull*1024*1024}; // combined CPU + conservative GPU accounting
    quint64 m_neighborCacheEvictions{0};
    SRenderMemoryEstimate m_renderMemorySample;
    quint64 m_framebufferMemorySample{0};
    QElapsedTimer m_renderMemorySampleTimer;
    bool m_neighborCacheTransition{false};
    QByteArray m_activeRenderResources;
    struct SGlobalPaletteReuse {
        QMap<qint32,QImage> sources;
        QMap<qint32,STerrainGlobalMaterialSource> identities;
    };
    QMap<QByteArray,SGlobalPaletteReuse> m_globalPaletteReuseCache;
    QByteArray m_globalPaletteReuseKey;
    QString m_neighborActiveMobPath;
    QByteArray neighborResourceFingerprint() const;
    QByteArray neighborObjectResourceFingerprint() const;
    QByteArray neighborTerrainFingerprint(const QString& path, const QByteArray& resources) const;
    void trimNeighborRenderCache();
    void retainActiveLandscape();
    void retainActiveNeighborObjects(CMob* mob);
    QThreadPool* m_neighborScenePool{nullptr};
    bool m_neighborObjectUploadScheduled{false};
    bool m_neighborLoadingDeferred{false};
    bool m_neighborLoadRequested{false};
    int m_neighborLoadStage{0}; // 1 touching terrain, 2 touching objects, 3 remaining terrain
    int m_neighborLoadJobs{0};
    int m_neighborLoadNext{0};
    bool m_neighborLoadPulseScheduled{false};
    QSharedPointer<std::atomic<quint64>> m_neighborSceneGeneration{
        new std::atomic<quint64>(0)};
    CWorldTerrainSession m_worldTerrainSession;
    QMap<QString,SWorldTerrainAtlasSource> m_worldTerrainAtlasSources;
    SWorldTerrainStroke m_worldTerrainStroke;
    QSet<QString> m_worldTerrainEnabledPaths;
    QSet<QString> m_worldTerrainExcludedPaths;
    bool m_worldTerrainEnabled{true};
    bool m_agentSaveAuthorized{false};
    bool m_worldTerrainSavePendingNotified{false};
    QVector<SNeighborMapPlacement> m_neighborPlacements;
    QByteArray m_neighborContextKey;
    QPointF m_activeMapWorldOrigin;
    QString m_neighborActiveZoneId;
    QPointF m_worldTileGridAnchor;
    bool m_hasWorldTileGridAnchor{false};
    bool m_worldTileSnapEnabled{true};
    QPointF m_worldPlacementTileAnchor;
    bool m_hasWorldPlacementTileAnchor{false};
    SWorldMapSnapResult m_worldMapSnapPreview;
    bool m_neighborLayoutDragValid{false};
    QString m_neighborMapAnchorZoneId;
    bool m_hasActiveMapWorldOrigin{false};
    bool m_neighborLayoutStorageWritable{false};
    bool m_neighborLayoutEditEnabled{false};
    bool m_neighborLayoutDragActive{false};
    bool m_neighborLayoutDragChanged{false};
    QString m_neighborLayoutDragZoneId;
    QPointF m_neighborLayoutDragStartOrigin;
    QPointF m_neighborLayoutDragWorldOrigin;
    QPointF m_neighborLayoutDragStartPointerWorld;
    float m_neighborLayoutDragPlaneZ{0.0f};
    bool m_neighborPointerGestureCaptured{false};
    Qt::MouseButton m_neighborPointerGestureButton{Qt::NoButton};
    bool m_neighborNavigationInProgress{false};
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
    QSharedPointer<SWorkingTerrainResolveStage> m_preparedTerrainFamilyStage;
    qint32 m_preparedTerrainFamily{-1};
    ETerrainTileLayer m_preparedTerrainFamilyLayer{ETerrainTileLayer::Land};
    quint64 m_terrainFamilyPrewarmGeneration{0};
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
    bool m_terrainStampRegressionFootprintOverride{false};
    STerrainTileBrush m_terrainStampRegressionBrush;
    STerrainTileBrush m_terrainStampRegressionAppliedBrush;
    STerrainPaintFootprintSnapshot m_terrainStampRegressionFootprint;
    int m_terrainStampRegressionLiveProbeCoreCells{-1};
    int m_terrainStampRegressionLiveProbeDeltas{-1};
    QMap<int, quint16> m_terrainStampRegressionBaselinePacked;
    int m_terrainStampRegressionPreCommandPackedChanges{-1};
    bool m_terrainStampRegressionFrameCapture{false};
    QVector<QImage> m_terrainStampRegressionFrames;
    bool m_terrainSampleGestureActive{false};
    bool m_terrainSampleKeyValid{false};
    QPoint m_lastTerrainSamplePoint;
    QString m_lastTerrainSampleMap;
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
    bool m_tileStrokeAutoResolveBlends{true};
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
    Qt::MouseButtons m_landscapeGestureButtons{Qt::NoButton};
    bool m_landscapeGestureInterrupted{false};
    bool m_sculptPointerValid{false};
    STerrainHeightCommand m_sculptStrokeCommand;
    QSharedPointer<STerrainMobHeightState> m_sculptStrokeMobHeightState;
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
    QToolButton* m_neighborLandscapesVisualToggle{nullptr};
    QToolButton* m_neighborLayoutEditToggle{nullptr};
    QCheckBox* m_worldTileSnapToggle{nullptr};
    QToolButton* m_neighborLayoutResetButton{nullptr};
    QLabel* m_neighborLayoutStatus{nullptr};
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
    QComboBox* m_navigationSource{nullptr};
    QWidget* m_navigationContent{nullptr};
    QScrollArea* m_navigationScroll{nullptr};
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
    QVector<float> m_aiGraphLoadedSurfaceHeights;
    bool m_aiGraphLoadedSurfacesResolved{false};
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
    // Only another explicit button click can queue a follow-up calculation.
    bool m_aiGraphRegenerationPending{false};
    bool m_aiGraphRegeneratedFresh{false};
    bool m_aiGraphPreviewUsesLoadedSource{true};
    bool m_canvasNavigationStale{false};
    bool m_aiGraphPreviewVisible{false};
    bool m_aiGraphOverlayDirty{true};
    QVector<QVector3D> m_aiGraphOpenLinks;
    QVector<QVector3D> m_aiGraphNodes;
    QVector<QVector3D> m_aiGraphIsolatedNodes;
    bool m_environmentPreviewSyncing{false};
    QVector<STravelArea> m_worldTravelAreas;
    QVector<STravelArea> m_displayTravelAreas;
    bool m_entranceOverlaysVisible{true};
    bool m_neighborLandscapesVisible{false};
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

bool patrolAuthoringIntegrationCheck(CView* view);
bool terrainBaselineAnimationIntegrationCheck(CView* view);

#endif // MYGLWIDGET_H

#ifndef TERRAIN_EDIT_MODEL_H
#define TERRAIN_EDIT_MODEL_H

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QPair>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QVector>

class CMprDocument;
class CTerrainVirtualMaterialField;
class CMprEditTransaction;
class CValidationReport;

struct STerrainSectorSource
{
    QString memberName;
    QByteArray expectedPayloadSha256;
    int sectorX{-1};
    int sectorY{-1};
    quint8 type{0};
    quint64 overlayVertexOffset{0};
    quint64 landTileOffset{0};
    quint64 overlayTileOffset{0};
    quint64 overlayMaterialOffset{0};
    QByteArray payload;
};

enum class ETerrainTileLayer
{
    Land,
    Overlay
};

enum class ETerrainMaterialFieldRecovery
{
    RequireExactTopology,
    RecoverDeterministically
};

enum class ETerrainSemanticPaintPolicy
{
    AllowLocalHardEdges,
    ExactExistingOnly
};

enum class ETerrainTileBrushShape
{
    Circle,
    Square
};

struct STerrainTileBrush
{
    float centerX{0.0f};
    float centerY{0.0f};
    float radius{0.0f};
    ETerrainTileBrushShape shape{ETerrainTileBrushShape::Circle};
};

// Returns both endpoints and enough integer grid points that consecutive
// samples always touch. Authored-atlas painting resolves the collected stroke
// once, so sparse mouse events must not leave gaps between requested vertices.
QVector<QPoint> terrainVertexStrokeLine(const QPoint& from, const QPoint& to);

struct STerrainTransitionSignature
{
    qint32 northWest{0};
    qint32 northEast{0};
    qint32 southEast{0};
    qint32 southWest{0};

    bool operator==(const STerrainTransitionSignature& other) const
    {
        return northWest == other.northWest && northEast == other.northEast
            && southEast == other.southEast && southWest == other.southWest;
    }
};

struct STerrainTileDelta
{
    int globalX{0};
    int globalY{0};
    ETerrainTileLayer layer{ETerrainTileLayer::Land};
    quint16 before{0};
    quint16 after{0};
    bool hasSemanticRequest{false};
    int brushCenterX{0};
    int brushCenterY{0};
    int diagnosticStrokeId{-1};
    qint32 requestedMaterialId{0};
    STerrainTransitionSignature requestedSignature;
};

struct STerrainSemanticPaintAudit
{
    bool valid{false};
    STerrainTileBrush brush;
    ETerrainTileLayer layer{ETerrainTileLayer::Land};
    qint32 requestedMaterialId{0};
    qint32 backgroundMaterialId{0};
    QVector<qint32> materialPath;
    QVector<QPoint> paintedCoreCells;
    QVector<QPoint> paintedVertices;
    QVector<QPoint> affectedCells;
    QString solverMode{"not-run"};
    int junctionPadding{0};
    int solverNodes{0};
    int solverLimit{0};
};

struct STerrainTileCommand
{
    QString description;
    QVector<STerrainTileDelta> deltas;
    STerrainSemanticPaintAudit semanticAudit;
    struct SVirtualVertexDelta
    {
        int globalX{0};
        int globalY{0};
        ETerrainTileLayer layer{ETerrainTileLayer::Land};
        qint32 before{0};
        qint32 after{0};
    };
    QVector<SVirtualVertexDelta> virtualVertices;
    bool isEmpty() const { return deltas.isEmpty() && virtualVertices.isEmpty(); }
};

// Compose sequential live-preview commands into one reversible before/after
// transaction. Repeated cells retain their first value and final value.
void appendTerrainTileCommand(STerrainTileCommand* stroke,
                              const STerrainTileCommand& stamp);

struct STerrainTransitionVariant
{
    STerrainTransitionSignature signature;
    quint16 packedTile{0};
    // Weight belongs to the physical artwork, not to each packed rotation.
    // All rotations of one atlas slot/signature must use the same value.
    int distributionWeight{1};
    // Decode-only entries remain available to signatureForPacked(), while
    // automatic painting considers only reviewed semantic catalog entries.
    bool paintSelectable{true};
};

class CTerrainTransitionCatalog
{
public:
    static STerrainTransitionSignature rotated(
        const STerrainTransitionSignature& signature, int clockwiseQuarterTurns);
    static STerrainTransitionSignature mirroredGridY(
        const STerrainTransitionSignature& signature);
    bool addVariant(const STerrainTransitionVariant& variant,
                    CValidationReport* report = nullptr);
    quint16 select(const STerrainTransitionSignature& signature,
                   int globalX, int globalY, quint32 distributionSeed,
                   bool* found = nullptr) const;
    quint16 selectForPaint(const STerrainTransitionSignature& signature,
                           int globalX, int globalY, quint32 distributionSeed,
                           bool* found = nullptr) const;
    // Exact map repair sometimes has a reviewed runtime type inherited from
    // the physical predecessor. Restrict the deterministic paint selection to
    // physical slots carrying that type instead of substituting another equal
    // semantic signature.
    quint16 selectForPaint(const STerrainTransitionSignature& signature,
                           int globalX, int globalY, quint32 distributionSeed,
                           const QSet<int>& allowedPhysicalSlots,
                           bool* found = nullptr) const;
    quint16 lowestPaintRotationForSlot(
        const STerrainTransitionSignature& signature, quint16 selectedPacked,
        bool* found = nullptr) const;
    STerrainTransitionVariant variantForPacked(
        quint16 packedTile, bool* found = nullptr) const;
    QVector<STerrainTransitionVariant> paintVariants(
        const STerrainTransitionSignature& signature) const;
    void setMirrorPaintGridY(bool enabled) { m_mirrorPaintGridY = enabled; }
    STerrainTransitionSignature signatureForPacked(
        quint16 packedTile, bool* found = nullptr) const;
    STerrainTransitionSignature gridSignatureForPacked(
        quint16 packedTile, bool* found = nullptr) const;
    bool hasMaterial(qint32 materialId) const;
    void declareMaterial(qint32 materialId) { m_materialIds.insert(materialId); }
    QVector<qint32> materialIds() const;
    QVector<qint32> shortestMaterialPath(qint32 fromMaterial,
                                         qint32 toMaterial,
                                         const QMap<qint32, int>& preferredMaterials = {}) const;
    QVector<STerrainTransitionSignature> gridSignatures() const;
    void removeAtlasSlot(int atlasSlot);
    bool paintGridMirrored() const { return m_mirrorPaintGridY; }
    int size() const { return m_variants.size(); }

private:
    static QByteArray signatureKey(const STerrainTransitionSignature& signature);
    quint16 selectCandidates(const STerrainTransitionSignature& signature,
                              int globalX, int globalY,
                              quint32 distributionSeed, bool paintOnly,
                              const QSet<int>* allowedPhysicalSlots,
                              bool* found) const;
    QVector<STerrainTransitionVariant> m_variants;
    QHash<quint16, int> m_packedLookup;
    QHash<QByteArray, QVector<int>> m_signatureLookup;
    QSet<qint32> m_materialIds;
    bool m_mirrorPaintGridY{false};
};

struct STerrainHeightDelta
{
    int globalX{0};
    int globalY{0};
    quint16 before{0};
    quint16 after{0};
    bool clamped{false};
};

struct STerrainHeightCommand
{
    QString description;
    QVector<STerrainHeightDelta> deltas;
    bool isEmpty() const { return deltas.isEmpty(); }
    bool requiresMaxZDecision() const
    {
        for(const STerrainHeightDelta& delta : deltas)
            if(delta.clamped)
                return true;
        return false;
    }
};

struct SOverlaySectorDelta
{
    int sectorX{-1};
    int sectorY{-1};
    QByteArray beforePayload;
    QByteArray afterPayload;
};

struct SOverlaySurfaceCommand
{
    QString description;
    QVector<SOverlaySectorDelta> sectors;
    int heightClampedVertexCount{0};
    bool isEmpty() const { return sectors.isEmpty(); }
};

struct STerrainHeightRange
{
    int vertexCount{0};
    quint16 minimumRaw{0};
    quint16 maximumRaw{0};
    float minimumWorld{0.0f};
    float maximumWorld{0.0f};
    float worldUnitsPerRawStep{0.0f};
};

enum class ETerrainVerticalScalePolicy
{
    ScaleTerrain,
    PreserveWorldHeights
};

struct STerrainVerticalScaleCommand
{
    bool valid{false};
    QString description;
    ETerrainVerticalScalePolicy policy{ETerrainVerticalScalePolicy::ScaleTerrain};
    float beforeMaxZ{0.0f};
    float afterMaxZ{0.0f};
    STerrainHeightCommand heights;
    int roundedVertexCount{0};
    int clampedVertexCount{0};
    float maximumWorldError{0.0f};
    bool isEmpty() const { return valid && beforeMaxZ == afterMaxZ && heights.isEmpty(); }
};

// Compose sequential live-preview height commands into one reversible
// before/after transaction. Repeated vertices retain their first value and
// final value so a whole sculpt stroke rebuilds each affected sector once.
void appendTerrainHeightCommand(STerrainHeightCommand* stroke,
                                const STerrainHeightCommand& stamp);

struct STerrainObjectHeightProbe
{
    QString identity;
    // Positions use the canonical terrain lattice. Height diagnostics sample
    // that lattice bilinearly; this is a conservative editor warning, not the
    // engine's collision/contact projection.
    float positionX{0.0f};
    float positionY{0.0f};
    float positionZ{0.0f};
};

struct STerrainObjectHeightFinding
{
    QString identity;
    float objectZ{0.0f};
    float terrainBeforeZ{0.0f};
    float terrainAfterZ{0.0f};
    float terrainDelta{0.0f};
    float clearanceBefore{0.0f};
    float clearanceAfter{0.0f};
    bool exceedsThreshold{false};
};

struct STerrainDiagnosticPolicy
{
    // Negative values disable threshold classification. The exact engine
    // navigation and placement limits are not known, so callers must choose
    // and label profile-specific thresholds explicitly.
    float steepSlopeDegrees{-1.0f};
    float objectVerticalDelta{-1.0f};
};

struct STerrainEditDiagnostics
{
    bool valid{false};
    bool requiresMaxZDecision{false};
    bool navigationGraphMayBeStale{false};
    bool steepSlopeRisk{false};
    int changedVertexCount{0};
    int sharedSeamVertexCount{0};
    int affectedSectorCount{0};
    quint16 minimumBefore{0};
    quint16 maximumBefore{0};
    quint16 minimumAfter{0};
    quint16 maximumAfter{0};
    float minimumWorldDelta{0.0f};
    float maximumWorldDelta{0.0f};
    float maximumSlopeBeforeDegrees{0.0f};
    float maximumSlopeAfterDegrees{0.0f};
    QVector<STerrainObjectHeightFinding> objectFindings;
};

enum class ETerrainBrushFalloff
{
    Constant,
    Linear,
    Smooth
};

struct STerrainBrush
{
    float centerX{0.0f};
    float centerY{0.0f};
    float radius{1.0f};
    ETerrainBrushFalloff falloff{ETerrainBrushFalloff::Smooth};
};

struct STerrainHeightmapGrid
{
    int width{0};
    int height{0};
    // Normalized samples in row-major order. Image decoding and orientation
    // are UI/import-adapter concerns and must be explicit before this stage.
    QVector<float> samples;
};

enum class ETerrainHeightmapPlacement
{
    StretchToTerrain,
    OneSamplePerVertex
};

struct STerrainHeightmapImportSettings
{
    ETerrainHeightmapPlacement placement{ETerrainHeightmapPlacement::StretchToTerrain};
    int destinationX{0};
    int destinationY{0};
    float minimumRawHeight{0.0f};
    float maximumRawHeight{65535.0f};
    float strength{1.0f};
};

class CTerrainEditModel
{
public:
    bool load(const CMprDocument& document, CValidationReport* report = nullptr);
    bool loadSources(int sectorCountX, int sectorCountY, float maxZ,
                     const QVector<STerrainSectorSource>& sources,
                     CValidationReport* report = nullptr,
                     int textureCount = 0,
                     const QVector<qint32>& tileTypes = QVector<qint32>(),
                     int materialCount = 0);
    void enableWorkingAtlasSlots(int slotCount);
    bool setWorkingAtlasSlotType(int slot, qint32 runtimeTerrainType);

    int vertexCountX() const { return m_vertexCountX; }
    int vertexCountY() const { return m_vertexCountY; }
    quint16 height(int globalX, int globalY) const;
    float maxZ() const { return m_maxZ; }
    STerrainHeightRange heightRange() const;
    bool hasSeamConflicts() const;
    bool isDirty() const;
    int tileCountX() const { return m_sectorCountX * 16; }
    int tileCountY() const { return m_sectorCountY * 16; }
    quint16 packedTile(int globalX, int globalY, ETerrainTileLayer layer,
                       bool* available = nullptr) const;
    qint32 tileType(quint16 packed, bool* found = nullptr) const;
    // Source-compatible half-metre navigation samples. Each 1 m terrain quad
    // contributes four samples using the three adjacent SEC vertices; this is
    // deliberately not ordinary bilinear filtering.
    float navigationHeightSample(int aiTileX, int aiTileY,
                                 ETerrainTileLayer layer,
                                 bool* available = nullptr) const;
    qint16 overlayMaterialIndex(int globalTileX, int globalTileY,
                                bool* available = nullptr) const;
    SOverlaySurfaceCommand makeOverlayActivation(
        const STerrainTileBrush& brush, qint16 materialIndex,
        quint8 atlasIndex, quint8 tileIndex, quint8 rotation,
        CValidationReport* report = nullptr) const;
    SOverlaySurfaceCommand makeOverlayDeactivation(
        const STerrainTileBrush& brush,
        CValidationReport* report = nullptr) const;
    SOverlaySurfaceCommand makeOverlayRaiseLower(
        const STerrainBrush& brush, float rawHeightDelta,
        CValidationReport* report = nullptr) const;
    SOverlaySurfaceCommand makeOverlayFlatten(
        const STerrainBrush& brush, quint16 targetHeight, float strength,
        CValidationReport* report = nullptr) const;
    SOverlaySurfaceCommand makeOverlaySmooth(
        const STerrainBrush& brush, float strength,
        CValidationReport* report = nullptr) const;

    STerrainHeightCommand makeRaiseLower(const STerrainBrush& brush,
                                         float rawHeightDelta) const;
    STerrainHeightCommand makeFlatten(const STerrainBrush& brush,
                                      quint16 targetHeight,
                                      float strength) const;
    STerrainHeightCommand makeSmooth(const STerrainBrush& brush,
                                     float strength) const;
    STerrainHeightCommand makeRamp(float startX, float startY, quint16 startHeight,
                                   float endX, float endY, quint16 endHeight,
                                   float radius, float strength,
                                   ETerrainBrushFalloff falloff) const;
    STerrainHeightCommand makeHeightmapImport(
        const STerrainHeightmapGrid& grid,
        const STerrainHeightmapImportSettings& settings,
        CValidationReport* report = nullptr) const;
    STerrainHeightCommand makeGlobalOffset(
        float rawHeightDelta,
        CValidationReport* report = nullptr) const;
    STerrainHeightCommand makeGlobalFlatten(quint16 targetHeight) const;
    STerrainHeightCommand makeGlobalClamp(
        quint16 minimumHeight, quint16 maximumHeight,
        CValidationReport* report = nullptr) const;
    STerrainHeightCommand makeGlobalNormalize(
        quint16 outputMinimum, quint16 outputMaximum,
        CValidationReport* report = nullptr) const;
    STerrainVerticalScaleCommand makeVerticalScale(
        float newMaxZ, ETerrainVerticalScalePolicy policy,
        bool allowClamping = false,
        CValidationReport* report = nullptr) const;
    STerrainEditDiagnostics diagnose(
        const STerrainHeightCommand& command,
        const QVector<STerrainObjectHeightProbe>& objects = QVector<STerrainObjectHeightProbe>(),
        const STerrainDiagnosticPolicy& policy = STerrainDiagnosticPolicy(),
        CValidationReport* report = nullptr) const;
    bool apply(const STerrainHeightCommand& command, bool forward,
               CValidationReport* report = nullptr,
               bool allowHeightClamping = false);
    bool apply(const STerrainVerticalScaleCommand& command, bool forward,
               CValidationReport* report = nullptr);
    STerrainTileCommand makeTilePaint(const STerrainTileBrush& brush,
                                     ETerrainTileLayer layer,
                                     quint8 atlasIndex, quint8 tileIndex,
                                     quint8 rotation) const;
    // Manual painting remains a direct physical operation, but a caller with
    // the active catalog can also keep the model-owned semantic field honest.
    // Each affected vertex is recomputed from its post-command incident
    // physical cells: one unanimous decoded value is retained; a disagreement
    // or unassigned packed cell is represented as an unset vertex instead of
    // privileging either the old field or the Manual stamp.
    STerrainTileCommand makeTilePaint(const STerrainTileBrush& brush,
                                     ETerrainTileLayer layer,
                                     quint8 atlasIndex, quint8 tileIndex,
                                     quint8 rotation,
                                     const CTerrainTransitionCatalog& catalog,
                                     CValidationReport* report = nullptr);
    STerrainTileCommand makeSemanticTilePaint(
        const STerrainTileBrush& brush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed,
        CValidationReport* report = nullptr,
        ETerrainSemanticPaintPolicy policy =
            ETerrainSemanticPaintPolicy::AllowLocalHardEdges);
    STerrainTileCommand makeSemanticVertexPaint(
        int selectedCellX, int selectedCellY,
        const STerrainTileBrush& vertexBrush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed,
        CValidationReport* report = nullptr,
        qint32 preferredSupportId = 0,
        ETerrainSemanticPaintPolicy policy =
            ETerrainSemanticPaintPolicy::AllowLocalHardEdges);
    STerrainTileCommand makeSemanticVertexStrokePaint(
        const QVector<QPoint>& requestedVertices,
        const QVector<QPoint>& anchorVertices,
        ETerrainTileLayer layer, qint32 materialId,
        const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed,
        CValidationReport* report = nullptr,
        ETerrainSemanticPaintPolicy policy =
            ETerrainSemanticPaintPolicy::AllowLocalHardEdges);
    // Modern flow: material identity is committed to an independent vertex
    // field. Drag preview is deliberately uniform; release prefers authored
    // exact tiles. The caller must realize absent tuples in the live working
    // atlas before requesting release resolution; identities are never deferred.
    STerrainTileCommand makeVirtualMaterialPaint(
        int selectedCellX, int selectedCellY,
        const STerrainTileBrush& vertexBrush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed, CValidationReport* report = nullptr,
        bool randomizeUniformRotation = false);
    STerrainTileCommand makeVirtualTileMaterialPaint(
        const STerrainTileBrush& tileBrush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed, CValidationReport* report = nullptr,
        bool randomizeUniformRotation = false);
    STerrainTileCommand makeVirtualStrokeResolution(
        const STerrainTileCommand& stroke,
        const CTerrainTransitionCatalog& catalog, quint32 distributionSeed,
        CValidationReport* report = nullptr,
        bool randomizeUniformRotation = false) const;
    STerrainTileCommand makeVirtualMapResolution(
        ETerrainTileLayer layer, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed, CValidationReport* report = nullptr,
        bool randomizeUniformRotation = false,
        const CTerrainVirtualMaterialField* repairField = nullptr,
        bool allowPartialMaterialField = false,
        bool repairDecodedCells = true) const;
    bool recoverUnambiguousVirtualMaterialField(
        ETerrainTileLayer layer, const CTerrainTransitionCatalog& catalog,
        CTerrainVirtualMaterialField* output,
        CValidationReport* report = nullptr) const;
    bool inferCellSignatureFromNeighbors(
        int cellX, int cellY, ETerrainTileLayer layer,
        const CTerrainTransitionCatalog& catalog,
        STerrainTransitionSignature* output,
        const QMap<int, STerrainTransitionSignature>* plannedSignatures = nullptr) const;
    bool missingBlendRepairSignature(
        int cellX, int cellY, ETerrainTileLayer layer,
        const CTerrainTransitionCatalog& catalog,
        const CTerrainVirtualMaterialField* repairField,
        bool repairDecodedCells,
        STerrainTransitionSignature* output,
        const QMap<int, STerrainTransitionSignature>* plannedSignatures = nullptr) const;
    bool copyVirtualMaterialField(ETerrainTileLayer layer,
        CTerrainVirtualMaterialField* output,
        bool requireComplete = true) const;
    bool initializeVirtualMaterialField(
        ETerrainTileLayer layer, const CTerrainTransitionCatalog& catalog,
        CValidationReport* report = nullptr,
        ETerrainMaterialFieldRecovery recovery =
            ETerrainMaterialFieldRecovery::RequireExactTopology);
    bool virtualMaterialFieldActive(ETerrainTileLayer layer) const;
    qint32 virtualMaterialAt(ETerrainTileLayer layer, int vertexX,
                             int vertexY, bool* found = nullptr) const;
    bool virtualMaterialCellChanged(
        ETerrainTileLayer layer, int cellX, int cellY) const;
    STerrainTileCommand makeSemanticCornerMaterialPaint(
        int selectedCellX, int selectedCellY, int vertexX, int vertexY,
        ETerrainTileLayer layer, qint32 materialId,
        const CTerrainTransitionCatalog& catalog, quint32 distributionSeed,
        CValidationReport* report = nullptr);
    bool apply(const STerrainTileCommand& command, bool forward,
               CValidationReport* report = nullptr);
    bool apply(const SOverlaySurfaceCommand& command, bool forward,
               CValidationReport* report = nullptr);
    bool appendSectorEdits(CMprEditTransaction* transaction,
                           CValidationReport* report = nullptr) const;
    QByteArray sectorPayload(int sectorX, int sectorY) const;

private:
    struct SVertexRef
    {
        int sectorIndex{-1};
        int localX{0};
        int localY{0};
    };
    struct SSector
    {
        STerrainSectorSource source;
        QByteArray currentPayload;
        bool dirty{false};
    };

    int globalIndex(int x, int y) const;
    quint32 packedNormal(int globalX, int globalY) const;
    bool normalNeighborhoodIsEditable(int globalX, int globalY) const;
    float brushWeight(float distance, float radius, ETerrainBrushFalloff falloff) const;
    STerrainHeightCommand makeBlendedCommand(const QString& description,
                                             const STerrainBrush& brush,
                                             const QVector<float>& targets,
                                             float strength) const;
    bool tileLocation(int globalX, int globalY, int* sectorIndex,
                      int* localTile, CValidationReport* report = nullptr) const;
    bool overlayTileIsActive(const SSector& sector, int localTile) const;
    bool refreshSectorLayout(SSector* sector,
                             CValidationReport* report = nullptr) const;
    SOverlaySurfaceCommand makeOverlayHeightEdit(
        const QString& description, const STerrainBrush& brush,
        int operation, float value, float strength,
        CValidationReport* report) const;
    STerrainTileCommand makeBoundedSemanticTilePaint(
        const STerrainTileBrush& brush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed, CValidationReport* report,
        ETerrainSemanticPaintPolicy policy) const;
    STerrainTileCommand makeVirtualMaterialPaintVertices(
        int selectedCellX, int selectedCellY,
        const QVector<QPoint>& requestedVertices,
        const QSet<int>& previewCoreCells,
        const STerrainTileBrush& auditBrush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed, CValidationReport* report,
        bool randomizeUniformRotation);
    bool appendSemanticVirtualVertices(
        STerrainTileCommand* command,
        const QVector<QPoint>& acceptedVertices,
        ETerrainTileLayer layer, qint32 materialId,
        const CTerrainTransitionCatalog& catalog,
        CValidationReport* report);

    int m_sectorCountX{0};
    int m_sectorCountY{0};
    int m_vertexCountX{0};
    int m_vertexCountY{0};
    float m_maxZ{0.0f};
    float m_originalMaxZ{0.0f};
    int m_textureCount{0};
    int m_materialCount{0};
    QVector<qint32> m_tileTypes;
    QVector<quint16> m_heights;
    QVector<quint16> m_originalHeights;
    QVector<qint8> m_xOffsets;
    QVector<qint8> m_yOffsets;
    QVector<QVector<SVertexRef>> m_refs;
    QVector<bool> m_conflictedVertices;
    QVector<SSector> m_sectors;
    QMap<QPair<int, int>, int> m_sectorLookup;
    QVector<qint32> m_virtualLandMaterials;
    QVector<qint32> m_virtualOverlayMaterials;
    QVector<qint32> m_originalVirtualLandMaterials;
    QVector<qint32> m_originalVirtualOverlayMaterials;
};

class CTerrainTileEditHistory
{
public:
    bool execute(CTerrainEditModel* model, const STerrainTileCommand& command,
                 CValidationReport* report = nullptr);
    bool undo(CTerrainEditModel* model, CValidationReport* report = nullptr);
    bool redo(CTerrainEditModel* model, CValidationReport* report = nullptr);
    int undoCount() const { return m_undo.size(); }
    int redoCount() const { return m_redo.size(); }
    void clear();

private:
    QVector<STerrainTileCommand> m_undo;
    QVector<STerrainTileCommand> m_redo;
};

class COverlaySurfaceEditHistory
{
public:
    bool execute(CTerrainEditModel* model, const SOverlaySurfaceCommand& command,
                 CValidationReport* report = nullptr);
    bool undo(CTerrainEditModel* model, CValidationReport* report = nullptr);
    bool redo(CTerrainEditModel* model, CValidationReport* report = nullptr);
    int undoCount() const { return m_undo.size(); }
    int redoCount() const { return m_redo.size(); }
    void clear();

private:
    QVector<SOverlaySurfaceCommand> m_undo;
    QVector<SOverlaySurfaceCommand> m_redo;
};

class CTerrainEditHistory
{
public:
    bool execute(CTerrainEditModel* model, const STerrainHeightCommand& command,
                 CValidationReport* report = nullptr,
                 bool allowHeightClamping = false);
    bool undo(CTerrainEditModel* model, CValidationReport* report = nullptr);
    bool redo(CTerrainEditModel* model, CValidationReport* report = nullptr,
              bool allowHeightClamping = false);
    int undoCount() const { return m_undo.size(); }
    int redoCount() const { return m_redo.size(); }
    void clear();

private:
    QVector<STerrainHeightCommand> m_undo;
    QVector<STerrainHeightCommand> m_redo;
};

#endif // TERRAIN_EDIT_MODEL_H

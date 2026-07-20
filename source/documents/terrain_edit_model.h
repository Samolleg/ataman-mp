#ifndef TERRAIN_EDIT_MODEL_H
#define TERRAIN_EDIT_MODEL_H

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

class CMprDocument;
class CMprEditTransaction;
class CValidationReport;

struct STerrainSectorSource
{
    QString memberName;
    QByteArray expectedPayloadSha256;
    int sectorX{-1};
    int sectorY{-1};
    quint8 type{0};
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

struct STerrainTileDelta
{
    int globalX{0};
    int globalY{0};
    ETerrainTileLayer layer{ETerrainTileLayer::Land};
    quint16 before{0};
    quint16 after{0};
};

struct STerrainTileCommand
{
    QString description;
    QVector<STerrainTileDelta> deltas;
    bool isEmpty() const { return deltas.isEmpty(); }
};

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

struct STerrainTransitionVariant
{
    STerrainTransitionSignature signature;
    quint16 packedTile{0};
    int distributionWeight{1};
};

class CTerrainTransitionCatalog
{
public:
    static STerrainTransitionSignature rotated(
        const STerrainTransitionSignature& signature, int clockwiseQuarterTurns);
    bool addVariant(const STerrainTransitionVariant& variant,
                    CValidationReport* report = nullptr);
    quint16 select(const STerrainTransitionSignature& signature,
                   int globalX, int globalY, quint32 distributionSeed,
                   bool* found = nullptr) const;
    STerrainTransitionSignature signatureForPacked(
        quint16 packedTile, bool* found = nullptr) const;
    bool hasMaterial(qint32 materialId) const;
    QVector<qint32> materialIds() const;
    int size() const { return m_variants.size(); }

private:
    static QByteArray signatureKey(const STerrainTransitionSignature& signature);
    QVector<STerrainTransitionVariant> m_variants;
    QHash<quint16, int> m_packedLookup;
    QHash<QByteArray, QVector<int>> m_signatureLookup;
    QSet<qint32> m_materialIds;
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
                     const QVector<qint32>& tileTypes = QVector<qint32>());

    int vertexCountX() const { return m_vertexCountX; }
    int vertexCountY() const { return m_vertexCountY; }
    quint16 height(int globalX, int globalY) const;
    bool hasSeamConflicts() const;
    bool isDirty() const;
    int tileCountX() const { return m_sectorCountX * 16; }
    int tileCountY() const { return m_sectorCountY * 16; }
    quint16 packedTile(int globalX, int globalY, ETerrainTileLayer layer,
                       bool* available = nullptr) const;
    qint32 tileType(quint16 packed, bool* found = nullptr) const;

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
    STerrainEditDiagnostics diagnose(
        const STerrainHeightCommand& command,
        const QVector<STerrainObjectHeightProbe>& objects = QVector<STerrainObjectHeightProbe>(),
        const STerrainDiagnosticPolicy& policy = STerrainDiagnosticPolicy(),
        CValidationReport* report = nullptr) const;
    bool apply(const STerrainHeightCommand& command, bool forward,
               CValidationReport* report = nullptr,
               bool allowHeightClamping = false);
    STerrainTileCommand makeTilePaint(const STerrainTileBrush& brush,
                                     ETerrainTileLayer layer,
                                     quint8 atlasIndex, quint8 tileIndex,
                                     quint8 rotation) const;
    STerrainTileCommand makeSemanticTilePaint(
        const STerrainTileBrush& brush, ETerrainTileLayer layer,
        qint32 materialId, const CTerrainTransitionCatalog& catalog,
        quint32 distributionSeed,
        CValidationReport* report = nullptr) const;
    bool apply(const STerrainTileCommand& command, bool forward,
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

    int m_sectorCountX{0};
    int m_sectorCountY{0};
    int m_vertexCountX{0};
    int m_vertexCountY{0};
    float m_maxZ{0.0f};
    int m_textureCount{0};
    QVector<qint32> m_tileTypes;
    QVector<quint16> m_heights;
    QVector<quint16> m_originalHeights;
    QVector<qint8> m_xOffsets;
    QVector<qint8> m_yOffsets;
    QVector<QVector<SVertexRef>> m_refs;
    QVector<bool> m_conflictedVertices;
    QVector<SSector> m_sectors;
    QMap<QPair<int, int>, int> m_sectorLookup;
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

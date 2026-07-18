#ifndef TERRAIN_EDIT_MODEL_H
#define TERRAIN_EDIT_MODEL_H

#include <QByteArray>
#include <QMap>
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
    QByteArray payload;
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

class CTerrainEditModel
{
public:
    bool load(const CMprDocument& document, CValidationReport* report = nullptr);
    bool loadSources(int sectorCountX, int sectorCountY, float maxZ,
                     const QVector<STerrainSectorSource>& sources,
                     CValidationReport* report = nullptr);

    int vertexCountX() const { return m_vertexCountX; }
    int vertexCountY() const { return m_vertexCountY; }
    quint16 height(int globalX, int globalY) const;
    bool hasSeamConflicts() const;
    bool isDirty() const;

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
    STerrainEditDiagnostics diagnose(
        const STerrainHeightCommand& command,
        const QVector<STerrainObjectHeightProbe>& objects = QVector<STerrainObjectHeightProbe>(),
        const STerrainDiagnosticPolicy& policy = STerrainDiagnosticPolicy(),
        CValidationReport* report = nullptr) const;
    bool apply(const STerrainHeightCommand& command, bool forward,
               CValidationReport* report = nullptr,
               bool allowHeightClamping = false);
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

    int m_sectorCountX{0};
    int m_sectorCountY{0};
    int m_vertexCountX{0};
    int m_vertexCountY{0};
    float m_maxZ{0.0f};
    QVector<quint16> m_heights;
    QVector<quint16> m_originalHeights;
    QVector<qint8> m_xOffsets;
    QVector<qint8> m_yOffsets;
    QVector<QVector<SVertexRef>> m_refs;
    QVector<bool> m_conflictedVertices;
    QVector<SSector> m_sectors;
    QMap<QPair<int, int>, int> m_sectorLookup;
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

#ifndef MPR_DOCUMENT_H
#define MPR_DOCUMENT_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QVector>

class CValidationReport;

enum class EMprMemberKind
{
    Unknown,
    MapParameters,
    Sector
};

struct SMprArchiveMember
{
    int tableIndex{-1};
    quint32 nextIndex{quint32(-1)};
    quint32 dataSize{0};
    quint32 dataOffset{0};
    quint32 lastWriteTime{0};
    quint16 nameLength{0};
    quint32 nameOffset{0};
    QByteArray encodedName;
    QString name;
    QByteArray payload;
    QByteArray sha256;
    EMprMemberKind kind{EMprMemberKind::Unknown};
};

struct SMprMapInfo
{
    bool valid{false};
    QString memberName;
    QString innerNamespace;
    quint32 signature{0};
    float maxZ{0.0f};
    quint32 sectorCountX{0};
    quint32 sectorCountY{0};
    quint32 textureCount{0};
    quint32 textureSize{0};
    quint32 tileCount{0};
    quint32 tileSize{0};
    quint16 materialCount{0};
    quint32 animatedTileCount{0};
    quint64 parsedSize{0};
    // Raw, read-only atlas slot categories from the MP member. Endurance uses
    // these for selection/diagnostics; changing them would be atlas editing.
    QVector<qint32> tileTypes;

    qint32 tileType(int atlasSlot, bool* found = nullptr) const;
};

struct SMprPackedTile
{
    quint8 tileIndex{0};
    quint8 atlasIndex{0};
    quint8 rotation{0};

    static SMprPackedTile decode(quint16 packed);
    quint16 encode() const;
    int atlasSlot() const { return int(atlasIndex) * 64 + int(tileIndex); }
};

struct SMprSectorInfo
{
    int memberIndex{-1};
    QString memberName;
    int sectorX{-1};
    int sectorY{-1};
    quint8 type{0};
    bool hasOverlay{false};
    quint64 landVertexOffset{5};
    quint64 overlayVertexOffset{0};
    quint64 landTileOffset{0};
    quint64 overlayTileOffset{0};
    quint64 overlayMaterialOffset{0};
    quint64 parsedSize{0};
    QVector<qint16> overlayMaterialIndices;
};

struct SMprMemberEdit
{
    QString memberName;
    QByteArray expectedPayloadSha256;
    QByteArray replacementPayload;
};

class CMprEditTransaction
{
public:
    explicit CMprEditTransaction(const QString& description = QString())
        : m_description(description) {}

    void replaceMemberPayload(const QString& memberName,
                              const QByteArray& expectedPayloadSha256,
                              const QByteArray& replacementPayload);
    const QString& description() const { return m_description; }
    const QVector<SMprMemberEdit>& edits() const { return m_edits; }
    bool isEmpty() const { return m_edits.isEmpty(); }

private:
    QString m_description;
    QVector<SMprMemberEdit> m_edits;
};

struct SMprDocumentDiffEntry
{
    QString kind;
    QString memberName;
    int baselineIndex{-1};
    int candidateIndex{-1};
    QByteArray baselineSha256;
    QByteArray candidateSha256;
    QString details;
};

// Structural canvas changes are intentionally separate from ordinary member
// edits. oldTileOffset places the old canvas inside the new canvas in the same
// two-world-unit tiles used by the terrain grid. Positive values add space on
// the west/north sides; negative values crop it there.
struct SMprCanvasResizeRequest
{
    quint32 sectorCountX{0};
    quint32 sectorCountY{0};
    int oldTileOffsetX{0};
    int oldTileOffsetY{0};
    bool closeEdges{false};
    int edgeTaperTiles{2};
};

struct SMprCreationRequest
{
    QString innerNamespace;
    quint32 sectorCountX{1};
    quint32 sectorCountY{1};
    float maxZ{100.0f};
    quint16 flatRawHeight{0};
    quint16 initialPackedTile{0};
    bool closeEdges{false};
    int edgeTaperTiles{2};
    int heightmapWidth{0};
    int heightmapHeight{0};
    QVector<float> heightmapSamples;
    float heightmapMinimumRaw{0.0f};
    float heightmapMaximumRaw{65535.0f};
    bool createOverlayPlane{false};
    quint16 overlayRawHeight{32768};
    quint16 overlayPackedTile{0};
    qint16 overlayMaterialIndex{0};
    bool taperOverlayEdges{false};
    int overlayEdgeTaperTiles{2};
};

class CMprSemanticDiff
{
public:
    static CMprSemanticDiff compare(const class CMprDocument& baseline,
                                    const class CMprDocument& candidate);
    const QVector<SMprDocumentDiffEntry>& entries() const { return m_entries; }
    QString toText() const;

private:
    QVector<SMprDocumentDiffEntry> m_entries;
};

class CMprDocument
{
public:
    bool load(const QFileInfo& file, CValidationReport* report = nullptr);
    bool loadBytes(const QByteArray& bytes, const QString& sourceName,
                   CValidationReport* report = nullptr);

    QByteArray serialize() const { return m_originalBytes; }
    const QByteArray& originalBytes() const { return m_originalBytes; }
    const QVector<SMprArchiveMember>& members() const { return m_members; }
    const SMprMapInfo& mapInfo() const { return m_mapInfo; }
    const QVector<SMprSectorInfo>& sectors() const { return m_sectors; }
    QByteArray memberPayload(const QString& name) const;
    bool applyTransaction(const CMprEditTransaction& transaction,
                          QByteArray* output,
                          CValidationReport* report = nullptr) const;
    bool appendMaxZEdit(float newMaxZ, CMprEditTransaction* transaction,
                        CValidationReport* report = nullptr) const;
    bool buildResizedCanvas(const SMprCanvasResizeRequest& request,
                            QByteArray* output,
                            CValidationReport* report = nullptr) const;
    // Uses this document only as a validated atlas/material template. The new
    // archive contains a freshly named MP and deterministic flat SEC members.
    bool buildNewCanvas(const SMprCreationRequest& request,
                        QByteArray* output,
                        CValidationReport* report = nullptr) const;
    bool isDirty() const { return false; }

private:
    friend class CMprSemanticDiff;
    int uniqueMemberIndex(const QString& name) const;

    QByteArray m_originalBytes;
    QVector<SMprArchiveMember> m_members;
    SMprMapInfo m_mapInfo;
    QVector<SMprSectorInfo> m_sectors;
    quint32 m_tableOffset{0};
    quint32 m_namesLength{0};
};

#endif // MPR_DOCUMENT_H

#ifndef TERRAIN_VIRTUAL_ATLAS_H
#define TERRAIN_VIRTUAL_ATLAS_H

#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QPoint>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include "terrain_edit_model.h"

class CValidationReport;
class CMprDocument;

// Shipped JMV runtime validation covers complete custom terrain atlases through
// 16 pages. Keep this separate from the 16,384-slot packed-format maximum so a
// future higher limit requires its own controlled runtime proof.
constexpr int kValidatedShippedTerrainAtlasSlotLimit = 1024;
constexpr int kMprPackedTerrainAtlasSlotLimit = 16384;

enum class ETerrainBlendStyle
{
    Organic,
    Grass,
    Soil,
    Ice,
    Lava,
    Paving,
    Stone,
    Gravel,
    Sand,
    Snow,
    Swamp,
    Water
};

struct STerrainVirtualTile
{
    STerrainTransitionSignature signature;
    bool authored{false};
    bool importedAuthored{false};
    quint16 authoredPacked{0};
    int generatedIndex{-1};
    quint8 generatedRotation{0};
    qint32 runtimeTerrainType{-1};
};

struct STerrainAtlasUsage
{
    QMap<int, quint64> authoredSlots;
    QMap<int, quint64> generatedTiles;
    quint64 cellCount{0};
};

struct STerrainFinalizedTile
{
    STerrainTransitionSignature canonicalSignature;
    QImage pixels;
    QByteArray pixelSha256;
    bool authored{false};
    int sourceSlot{-1};
    // Empty for original map slots. Imported exact tiles and generated tiles
    // record their physical artwork provenance.
    QString blendProvenance;
    bool stockDerivedBlend{false};
    bool canonicalGeneratedBlend{false};
    // Runtime MP terrain class. This is physical-slot metadata, independent
    // of semantic material IDs and packed rotation.
    qint32 runtimeTerrainType{-1};
};

struct STerrainBlendMaskTemplate
{
    STerrainTransitionSignature signature;
    QImage ownershipMask;
    qint32 ownerMaterial{0};
    qint32 otherMaterial{0};
    ETerrainBlendStyle ownerStyle{ETerrainBlendStyle::Organic};
    ETerrainBlendStyle otherStyle{ETerrainBlendStyle::Organic};
    QString sourceLabel;
    QByteArray maskSha256;
};

struct STerrainAtlasFinalization
{
    bool success{false};
    QString error;
    QString isolatedNamespace;
    int capacityLimit{kValidatedShippedTerrainAtlasSlotLimit};
    int requiredSlots{0};
    int textureCount{0};
    QVector<quint16> packedCells;
    QVector<STerrainFinalizedTile> tiles;
    QVector<QImage> pages;
    QVector<QByteArray> pageMmpPayloads;
    QVector<qint32> tileTypes;
    QMap<int, int> authoredSlotRemap;
    STerrainAtlasUsage usage;
    QByteArray stableSha256;
};

struct STerrainPortableAtlasPackage
{
    bool success{false};
    QString error;
    QByteArray mprBytes;
    QByteArray resourceArchiveBytes;
    QStringList resourceMembers;
};

// Canonical desired material ownership lives on terrain vertices and is not
// limited by authored atlas slots. Physical atlas references are derived only
// for preview/finalization.
class CTerrainVirtualMaterialField
{
public:
    bool resize(int tileWidth, int tileHeight, qint32 initialMaterial = 0);
    int tileWidth() const { return m_tileWidth; }
    int tileHeight() const { return m_tileHeight; }
    int vertexWidth() const { return m_tileWidth + 1; }
    int vertexHeight() const { return m_tileHeight + 1; }
    qint32 material(int vertexX, int vertexY, bool* found = nullptr) const;
    bool setMaterial(int vertexX, int vertexY, qint32 material);
    int paint(const STerrainTileBrush& brush, qint32 material);
    STerrainTransitionSignature cellSignature(int cellX, int cellY,
                                               bool* found = nullptr) const;
    QByteArray stableSha256() const;

private:
    int m_tileWidth{0};
    int m_tileHeight{0};
    QVector<qint32> m_materials;
};

// Resolves a virtual field against authored tiles first, then deterministically
// synthesizes exact missing tuples. Generated entries are canonicalized across
// quarter-turns and deduplicated again by pixels.
class CTerrainVirtualAtlas
{
public:
    CTerrainVirtualAtlas(const CTerrainTransitionCatalog& catalog,
                         const QVector<QImage>& authoredTiles,
                         quint32 distributionSeed = 0,
                         const QMap<qint32, QImage>& externalMaterialSources = {},
                         const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles = {},
                         bool generatedTilesOpaque = true,
                         const QVector<qint32>& sourceTileTypes = {},
                         const QMap<qint32, qint32>& materialRuntimeTypes = {});

    static ETerrainBlendStyle blendStyleForMaterialName(const QString& name);
    static QString blendStyleName(ETerrainBlendStyle style);
    static int blendVariationCount() { return 5; }
    static int blendVariationLimit(
        const STerrainTransitionSignature& signature);

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }
    void setDistributionSeed(quint32 seed) { m_distributionSeed = seed; }
    STerrainVirtualTile resolve(const STerrainTransitionSignature& signature,
                                int cellX, int cellY,
                                CValidationReport* report = nullptr,
                                qint32 preferredPaintMaterial = 0,
                                qint32 preferredRuntimeType = -1);
    bool addAuthoredLibraryTransition(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel = QString(),
        CValidationReport* report = nullptr);
    QVector<STerrainVirtualTile> authoredLibraryVariants(
        const STerrainTransitionSignature& signature,
        qint32 preferredPaintMaterial = 0,
        CValidationReport* report = nullptr);
    bool addStockBlendMaskTemplate(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel = QString(),
        CValidationReport* report = nullptr)
    { return registerBlendMaskTemplate(signature, pixels, sourceLabel, report); }
    int stockBlendMaskCountFor(
        const STerrainTransitionSignature& signature) const;
    // Returns the exact clean-room canonical ownership mask used by fallback
    // generation. Registered stock masks are calibration evidence only and
    // cannot change this result.
    // White belongs to ownerMaterial; black belongs to the other material.
    // This is exposed for deterministic human visual review, not editing.
    QImage reviewBlendMask(
        const STerrainTransitionSignature& gridSignature,
        qint32 ownerMaterial, int variation = 0,
        QString* provenance = nullptr) const;
    // Renders the exact generated pixels used for a missing transition while
    // deliberately bypassing authored-tile lookup. Review tooling uses this to
    // show the canonical mask outcome beside its uniform material sources.
    QImage reviewGeneratedBlend(
        const STerrainTransitionSignature& gridSignature,
        int variation = 0, QString* provenance = nullptr,
        CValidationReport* report = nullptr) const;
    // Review-only A/B compositor. Runtime generation remains encoded-RGB
    // until the focused visual comparison is explicitly accepted.
    QImage reviewGeneratedBlendLinearLight(
        const STerrainTransitionSignature& gridSignature,
        int variation = 0, QString* provenance = nullptr,
        CValidationReport* report = nullptr) const;
    const QVector<STerrainFinalizedTile>& generatedTiles() const
        { return m_generatedTiles; }

    STerrainAtlasFinalization finalize(const CTerrainVirtualMaterialField& field,
                                       const QString& sourceNamespace,
                                       int capacityLimit = kValidatedShippedTerrainAtlasSlotLimit,
                                       const QVector<qint32>& sourceTileTypes = {},
                                       CValidationReport* report = nullptr,
                                       const QVector<quint16>& pinnedPackedTiles = {},
                                       const QVector<qint32>& preferredCellTypes = {},
                                       const QVector<quint16>& preferredPackedCells = {});

    static QImage composePage(const QVector<QImage>& tiles, int firstTile,
                              int tileSize);
    // Decode the exact physical slot vector that the editor/game will observe
    // after MMP DXT1 encoding. This includes page padding slots and is the only
    // valid pixel/count identity for a portable finalized profile.
    static bool decodePhysicalTileImages(
        const STerrainAtlasFinalization& finalization,
        QVector<QImage>* images,
        CValidationReport* report = nullptr);
    static STerrainPortableAtlasPackage buildPortablePackage(
        const CMprDocument& source, const STerrainAtlasFinalization& finalization,
        ETerrainTileLayer layer, CValidationReport* report = nullptr);
    static STerrainPortableAtlasPackage buildWorkingPreviewPackage(
        const CMprDocument& source, const QVector<QImage>& activeTiles,
        const QVector<qint32>& activeTileTypes,
        const QVector<QByteArray>& authoredPagePayloads,
        CValidationReport* report = nullptr);

private:
    QByteArray signatureKey(const STerrainTransitionSignature& signature) const;
    STerrainTransitionSignature canonicalSignature(
        const STerrainTransitionSignature& signature, quint8* rotation) const;
    QImage generateTile(const STerrainTransitionSignature& gridSignature,
                        ETerrainBlendStyle style, int variation,
                        QString* provenance,
                        CValidationReport* report,
                        bool linearLight = false) const;
    bool registerBlendMaskTemplate(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel, CValidationReport* report);
    QImage alignedStockMask(
        const STerrainTransitionSignature& requestedPreview,
        qint32 ownerMaterial, qint32 otherMaterial, int variation,
        QString* provenance) const;
    ETerrainBlendStyle blendStyleForSignature(
        const STerrainTransitionSignature& signature) const;
    int blendVariation(int cellX, int cellY,
                       ETerrainBlendStyle style, int limit) const;
    qint32 resolvedRuntimeType(
        const STerrainTransitionSignature& signature,
        qint32 preferredPaintMaterial, qint32 preferredRuntimeType,
        CValidationReport* report) const;
    int typedTileVariant(int sourceIndex, qint32 runtimeType);

    const CTerrainTransitionCatalog& m_catalog;
    QVector<QImage> m_authoredTiles;
    QMap<qint32, QImage> m_materialSources;
    QMap<qint32, int> m_materialSourceSlots;
    QVector<qint32> m_sourceTileTypes;
    QMap<qint32, qint32> m_materialRuntimeTypes;
    QMap<qint32, ETerrainBlendStyle> m_materialBlendStyles;
    QMap<QByteArray, int> m_generatedBySignature;
    QMap<QByteArray, int> m_generatedByPixels;
    QVector<STerrainFinalizedTile> m_generatedTiles;
    QVector<STerrainBlendMaskTemplate> m_blendMaskTemplates;
    CTerrainTransitionCatalog m_authoredLibraryCatalog;
    QVector<int> m_authoredLibraryGeneratedIndices;
    quint32 m_distributionSeed{0};
    bool m_generatedTilesOpaque{true};
    bool m_valid{false};
    QString m_error;
};

// A bounded physical cache used only for the live editor preview. It realizes
// authored or generated exact tuples into currently unused slots, while the
// unbounded vertex-material field remains authoritative. Finalization does not
// trust this cache; it rebuilds a clean isolated atlas from the field.
class CTerrainWorkingAtlas
{
public:
    CTerrainWorkingAtlas(const CTerrainTransitionCatalog& authoredCatalog,
                         const QVector<QImage>& authoredTiles,
                         quint32 distributionSeed = 0,
                         int capacityLimit = 512,
                         const QMap<qint32, QImage>& externalMaterialSources = {},
                         const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles = {},
                         const QVector<qint32>& sourceTileTypes = {},
                         const QMap<qint32, qint32>& materialRuntimeTypes = {});

    bool isValid() const;
    QString error() const;
    const CTerrainTransitionCatalog& catalog() const { return m_workingCatalog; }
    const QVector<QImage>& tileImages() const { return m_workingImages; }
    QVector<QImage> activeTileImages() const;
    QVector<qint32> activeTileTypes() const;
    qint32 runtimeTerrainTypeForSlot(int slot) const;
    int capacityLimit() const { return m_capacityLimit; }
    int generatedTileCount() const;
    int activeTextureCount() const;
    void setDistributionSeed(quint32 seed)
    { if(m_resolver) m_resolver->setDistributionSeed(seed); }
    QSet<int> takeChangedSlots();
    bool addAuthoredLibraryTransition(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel = QString(),
        CValidationReport* report = nullptr);
    bool addStockBlendMaskTemplate(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel = QString(),
        CValidationReport* report = nullptr);
    int stockBlendMaskCountFor(
        const STerrainTransitionSignature& signature) const;
    bool realizeAuthoredLibraryVariants(
        const STerrainTransitionSignature& signature,
        QMap<int, quint64>* workingUsage,
        const QSet<int>& protectedSlots,
        CValidationReport* report = nullptr,
        qint32 preferredPaintMaterial = 0);

    quint16 resolve(const STerrainTransitionSignature& signature,
                    int cellX, int cellY,
                    QMap<int, quint64>* workingUsage,
                    const QSet<int>& protectedSlots,
                    bool* generated = nullptr,
                    CValidationReport* report = nullptr,
                    qint32 preferredPaintMaterial = 0);

private:
    int realize(bool generated, int sourceIndex,
                QMap<int, quint64>* workingUsage,
                const QSet<int>& protectedSlots,
                CValidationReport* report);
    void installCatalogVariants(bool generated, int sourceIndex,
                                int workingSlot);

    CTerrainTransitionCatalog m_authoredCatalog;
    CTerrainTransitionCatalog m_workingCatalog;
    QVector<QImage> m_authoredImages;
    QVector<QImage> m_workingImages;
    QVector<qint32> m_workingTileTypes;
    QSharedPointer<CTerrainVirtualAtlas> m_resolver;
    QMap<QPair<int, int>, int> m_virtualToWorking;
    QMap<int, QPair<int, int>> m_workingToVirtual;
    QSet<int> m_changedSlots;
    int m_capacityLimit{0};
};

#endif // TERRAIN_VIRTUAL_ATLAS_H

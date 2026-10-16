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
struct SMprMaterialRecord;
struct SMprAnimatedTileRecord;

// Shipped JMV runtime validation covers complete custom terrain atlases through
// 16 pages. Keep this separate from the 16,384-slot packed-format maximum so a
// future higher limit requires its own controlled runtime proof.
constexpr int kValidatedShippedTerrainAtlasSlotLimit = 1024;
constexpr int kMprPackedTerrainAtlasSlotLimit = 16384;
constexpr int kTerrainAtlasSlotsPerPage = 64;

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
    bool paintSelectable{true};
    bool referencePaintSelectable{true};
    int sourceSlot{-1};
    QString sourceNamespace;
    // Every authored physical slot represented by this retained artwork.
    // Exact-pixel duplicates with the same runtime terrain type may share one
    // finalized slot, but diagnostics and portable profiles keep the complete
    // source identity set. Same-pixel aliases with different runtime types are
    // distinct authored identities and must remain separate.
    QVector<int> sourceSlotAliases;
    // Empty for original map slots. Imported exact tiles and generated tiles
    // record their physical artwork provenance.
    QString blendProvenance;
    bool stockDerivedBlend{false};
    bool canonicalGeneratedBlend{false};
    // Exact material whose reviewed route generated this transition. Zero is
    // reserved for authored/legacy route-neutral provenance.
    qint32 routeOwnerMaterial{0};
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

struct STerrainAtlasAnimationRange
{
    quint16 tileIndex{0};
    quint16 frameCount{0};

    bool operator==(const STerrainAtlasAnimationRange& other) const
    {
        return tileIndex == other.tileIndex
            && frameCount == other.frameCount;
    }
};

struct STerrainAtlasFinalization
{
    QByteArray paintReferenceSha256;
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
    QVector<STerrainAtlasAnimationRange> animations;
    QMap<int, int> authoredSlotRemap;
    STerrainAtlasUsage usage;
    QByteArray stableSha256;
};

enum class ETerrainWorkingTileOrigin
{
    Unknown,
    AuthoredMap,
    ImportedAuthored,
    CanonicalGenerated
};

// Stable diagnostic identity for one physical slot in the live working
// cache.  A working slot number is intentionally not source provenance: the
// slot may be recycled while editing, so issue reports must capture this
// record while the marked artwork is still live.
struct STerrainWorkingTileIdentity
{
    ETerrainWorkingTileOrigin origin{ETerrainWorkingTileOrigin::Unknown};
    int workingSlot{-1};
    int sourceSlot{-1};
    QVector<int> sourceSlotAliases;
    QString sourceNamespace;
    STerrainTransitionSignature canonicalSignature;
    QByteArray physicalPixelSha256;
    QString blendProvenance;
    bool stockDerivedBlend{false};
    bool canonicalGeneratedBlend{false};
    qint32 routeOwnerMaterial{0};
    qint32 runtimeTerrainType{-1};
};

struct STerrainWorkingFinalizationOrigins
{
    QSet<int> retainedMapOutputs;
    QSet<int> importedAuthoredOutputs;
    QSet<int> canonicalGeneratedOutputs;
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
    static bool validateResourceIdentity(const QString& identity,
                                         QString* error = nullptr);
    static QString contentResourceIdentity(const QString& sourceNamespace,
                                           const QByteArray& stableSha256);

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }
    void setDistributionSeed(quint32 seed) { m_distributionSeed = seed; }
    bool hasMaterialSource(qint32 material) const
    { return m_materialSources.contains(material); }
    bool addExternalMaterialSource(qint32 material, const QImage& pixels);
    // Map repair can carry a reviewed predecessor runtime type that must be
    // retained exactly.  The default leaves this as the historical soft
    // tie-break for normal paint/finalization callers.
    STerrainVirtualTile resolve(const STerrainTransitionSignature& signature,
                                int cellX, int cellY,
                                CValidationReport* report = nullptr,
                                qint32 preferredPaintMaterial = 0,
                                qint32 preferredRuntimeType = -1,
                                bool requirePreferredRuntimeType = false);
    bool addAuthoredLibraryTransition(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel = QString(),
        CValidationReport* report = nullptr,
        const QString& sourceNamespace = QString(),
        int sourceSlot = -1);
    QVector<STerrainVirtualTile> authoredLibraryVariants(
        const STerrainTransitionSignature& signature,
        qint32 preferredPaintMaterial = 0,
        qint32 preferredRuntimeType = -1,
        CValidationReport* report = nullptr,
        bool requirePreferredRuntimeType = false);
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
        QString* provenance = nullptr,
        qint32 preferredPaintMaterial = 0) const;
    // Returns the ownership actually consumed by the compositor after its
    // narrow ownership remap and shared-edge continuity profile. Questionnaire
    // outcomes must be compared against this mask, while reviewBlendMask()
    // remains the exact reviewed source asset used to derive it.
    QImage reviewEffectiveBlendMask(
        const STerrainTransitionSignature& gridSignature,
        qint32 ownerMaterial, int variation = 0,
        QString* provenance = nullptr,
        qint32 preferredPaintMaterial = 0) const;
    // Renders the exact generated pixels used for a missing transition while
    // deliberately bypassing authored-tile lookup. Review tooling uses this to
    // show the canonical mask outcome beside its uniform material sources.
    QImage reviewGeneratedBlend(
        const STerrainTransitionSignature& gridSignature,
        int variation = 0, QString* provenance = nullptr,
        CValidationReport* report = nullptr,
        qint32 preferredPaintMaterial = 0) const;
    // Review-only A/B compositor. Runtime generation remains encoded-RGB
    // until the focused visual comparison is explicitly accepted.
    QImage reviewGeneratedBlendLinearLight(
        const STerrainTransitionSignature& gridSignature,
        int variation = 0, QString* provenance = nullptr,
        CValidationReport* report = nullptr,
        qint32 preferredPaintMaterial = 0) const;
    // Returns one exact ownership input currently used while composing a
    // three-/four-material generated junction. Review tools use this to prove
    // the approved authored family/topology asset, its legal rotation, and its
    // resulting multi-material application all refer to the same mask.
    QImage reviewGeneratedJunctionOwnershipMask(
        const STerrainTransitionSignature& gridSignature,
        qint32 ownerMaterial, int variation = 0,
        QString* provenance = nullptr) const;
    const QVector<STerrainFinalizedTile>& generatedTiles() const
        { return m_generatedTiles; }

    STerrainAtlasFinalization finalize(const CTerrainVirtualMaterialField& field,
                                       const QString& sourceNamespace,
                                       int capacityLimit = kValidatedShippedTerrainAtlasSlotLimit,
                                       const QVector<qint32>& sourceTileTypes = {},
                                       CValidationReport* report = nullptr,
                                       const QVector<quint16>& pinnedPackedTiles = {},
                                       const QVector<qint32>& preferredCellTypes = {},
                                       const QVector<quint16>& preferredPackedCells = {},
                                       const QVector<qint32>& preferredPaintMaterials = {},
                                       const CTerrainTransitionCatalog* authoredDecodeCatalog = nullptr,
                                       const QVector<bool>& preservePreferredPackedCells = {},
                                       const QVector<STerrainAtlasAnimationRange>& animations = {});

    static QImage composePage(const QVector<QImage>& tiles, int firstTile,
                              int tileSize);
    static QImage composeAtlasGrid(const QVector<QImage>& tiles,
                                   int textureCount, int columns);
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
    static STerrainPortableAtlasPackage buildPortablePackage(
        const CMprDocument& source, const CTerrainEditModel& editedTerrain,
        const STerrainAtlasFinalization& finalization,
        ETerrainTileLayer layer, CValidationReport* report = nullptr);
    static STerrainPortableAtlasPackage buildPortablePackage(
        const CMprDocument& source, const CTerrainEditModel& editedTerrain,
        const QVector<SMprMaterialRecord>& materials,
        const STerrainAtlasFinalization& finalization,
        ETerrainTileLayer layer, CValidationReport* report = nullptr);
    static STerrainPortableAtlasPackage buildWorkingPreviewPackage(
        const CMprDocument& source, const QVector<QImage>& activeTiles,
        const QVector<qint32>& activeTileTypes,
        const QVector<QByteArray>& authoredPagePayloads,
        CValidationReport* report = nullptr);
    static STerrainPortableAtlasPackage buildWorkingPreviewPackage(
        const CMprDocument& source, const QVector<QImage>& activeTiles,
        const QVector<qint32>& activeTileTypes,
        const QVector<QByteArray>& authoredPagePayloads,
        const QVector<SMprMaterialRecord>& materials,
        const QVector<SMprAnimatedTileRecord>& animations,
        CValidationReport* report = nullptr);

private:
    CTerrainVirtualAtlas(const CTerrainVirtualAtlas& other,
                         const CTerrainTransitionCatalog& catalog);
    CTerrainVirtualAtlas(const CTerrainVirtualAtlas&) = delete;
    CTerrainVirtualAtlas& operator=(const CTerrainVirtualAtlas&) = delete;
    friend class CTerrainWorkingAtlas;

    QByteArray signatureKey(const STerrainTransitionSignature& signature) const;
    STerrainTransitionSignature canonicalSignature(
        const STerrainTransitionSignature& signature, quint8* rotation) const;
    QImage generateTile(const STerrainTransitionSignature& gridSignature,
                        ETerrainBlendStyle style, int variation,
                        qint32 reviewedStyleMaterial,
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
        const STerrainTransitionSignature& signature,
        qint32 preferredPaintMaterial = 0) const;
    qint32 reviewedMaskStyleMaterial(
        const STerrainTransitionSignature& signature,
        ETerrainBlendStyle style,
        qint32 preferredPaintMaterial = 0) const;
    int blendVariation(int cellX, int cellY,
                       ETerrainBlendStyle style, int limit) const;
    qint32 resolvedRuntimeType(
        const STerrainTransitionSignature& signature,
        qint32 preferredPaintMaterial, qint32 preferredRuntimeType,
        CValidationReport* report,
        bool requirePreferredRuntimeType) const;
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
    // MP terrain types are logical per-slot metadata, while MMP imagery is
    // always stored in complete 64-tile physical pages.
    int activeTileCount() const;
    QVector<QImage> activeTileImages() const;
    QVector<qint32> activeTileTypes() const;
    // Choose existing packed artwork jointly along visible edges. This never
    // synthesizes pixels, allocates slots or changes the requested materials.
    STerrainTileCommand joinAuthoredTransitionEdges(
        const CTerrainEditModel& terrain, const STerrainTileCommand& resolution,
        CValidationReport* report = nullptr,
        const QMap<QPair<int,int>, QPair<QImage,quint8>>& boundaryTiles = {}) const;
    // Complete physical pages are required by the renderer, but only a slot
    // with a live identity can be painted manually.  Empty entries mark
    // physical page padding or an otherwise unaddressable slot.
    QVector<QByteArray> manualAtlasPhysicalIdentities() const;
    qint32 runtimeTerrainTypeForSlot(int slot) const;
    // Cheap slot classification for hot paint/save-state scans. Unlike
    // tileIdentityForSlot(), this never hashes the 256x256 artwork.
    ETerrainWorkingTileOrigin tileOriginForSlot(int slot) const;
    bool tileIdentityForSlot(int slot,
                             STerrainWorkingTileIdentity* identity) const;
    bool restoreFinalizedProvenance(
        STerrainAtlasFinalization* finalization,
        STerrainWorkingFinalizationOrigins* origins,
        const QString& sourceNamespace,
        const QByteArray& materialFieldSha256,
        const QVector<quint16>& pinnedPackedTiles = {},
        CValidationReport* report = nullptr) const;
    int capacityLimit() const { return m_capacityLimit; }
    bool preserveNativePages() const { return m_preserveNativePages; }
    void setPreserveNativePages(bool enabled) { m_preserveNativePages = enabled; }
    int authoredSlotCount() const { return m_authoredSlotCount; }
    const QVector<QImage>& authoredTileImages() const { return m_authoredImages; }
    int generatedTileCount() const;
    int activeTextureCount() const;
    // Resolve Missing Blends plans against an isolated copy.  The live cache
    // is changed only when the caller has accepted the complete repair.
    QSharedPointer<CTerrainWorkingAtlas> createStagingCopy() const;
    bool canCommitStaging(const CTerrainWorkingAtlas& staged) const;
    bool commitStaging(const CTerrainWorkingAtlas& staged);
    // Runtime flipbooks are contiguous physical ranges inside one MMP page.
    // When authored uniform frames were interleaved with generated blend slots
    // or crossed a page boundary, restore that runtime contract in safe
    // extension slots and return the exact start-slot map needed to update
    // every activating terrain reference.
    bool normalizeAnimationRanges(
        const QVector<SMprAnimatedTileRecord>& animations,
        QVector<SMprAnimatedTileRecord>* normalizedAnimations,
        QMap<int, int>* slotRemap,
        CValidationReport* report = nullptr);
    int workingMappingCount() const { return m_virtualToWorking.size(); }
    int catalogVariantCount() const { return m_workingCatalog.size(); }
    QSet<int> changedSlots() const { return m_changedSlots; }
    QByteArray stateFingerprint() const;
    void acknowledgeChangedSlots(const QSet<int>& changed);
    void setDistributionSeed(quint32 seed)
    { if(m_resolver) m_resolver->setDistributionSeed(seed); }
    bool hasMaterialSource(qint32 material) const
    { return m_resolver && m_resolver->hasMaterialSource(material); }
    bool addExternalMaterialSource(qint32 material, const QImage& pixels)
    { return m_resolver && m_resolver->addExternalMaterialSource(material, pixels); }
    QSet<int> takeChangedSlots();
    int importPhysicalTile(const QImage& pixels, qint32 runtimeType,
        const STerrainTransitionSignature* signature, const QString& sourceNamespace,
        int sourceSlot, CValidationReport* report, bool preserveAuthoredPages = false);
    int importWorldBoundaryTile(const QImage& pixels, qint32 runtimeType,
        const STerrainTransitionSignature& signature, CValidationReport* report = nullptr);

    bool addAuthoredLibraryTransition(
        const STerrainTransitionSignature& signature, const QImage& pixels,
        const QString& sourceLabel = QString(),
        CValidationReport* report = nullptr,
        const QString& sourceNamespace = QString(),
        int sourceSlot = -1);
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
        qint32 preferredPaintMaterial = 0,
        qint32 preferredRuntimeType = -1,
        bool requirePreferredRuntimeType = false);

    quint16 resolve(const STerrainTransitionSignature& signature,
                    int cellX, int cellY,
                    QMap<int, quint64>* workingUsage,
                    const QSet<int>& protectedSlots,
                    bool* generated = nullptr,
                    CValidationReport* report = nullptr,
                    qint32 preferredPaintMaterial = 0,
                    qint32 preferredRuntimeType = -1,
                    bool requirePreferredRuntimeType = false);

private:
    int realize(bool generated, int sourceIndex,
                QMap<int, quint64>* workingUsage,
                const QSet<int>& protectedSlots,
                CValidationReport* report);
    void installCatalogVariants(bool generated, int sourceIndex,
                                int workingSlot);
    bool hasSelectableAuthoredCandidate(
        const STerrainTransitionSignature& signature,
        qint32 runtimeTerrainType, qint32 preferredRouteOwnerMaterial) const;

    CTerrainTransitionCatalog m_authoredCatalog;
    CTerrainTransitionCatalog m_workingCatalog;
    QVector<QImage> m_authoredImages;
    QVector<QImage> m_workingImages;
    QVector<qint32> m_workingTileTypes;
    int m_authoredSlotCount{0};
    QSharedPointer<CTerrainVirtualAtlas> m_resolver;
    QMap<QPair<int, int>, int> m_virtualToWorking;
    QMap<int, QPair<int, int>> m_workingToVirtual;
    // Contiguous runtime flipbook copies retain the exact source working-slot
    // identity without pretending to be a new authored/generated provenance.
    QMap<int, int> m_animationCopySources;
    QMap<int, STerrainWorkingTileIdentity> m_foreignPhysicalTiles;
    QSet<int> m_changedSlots;
    int m_capacityLimit{0};
    bool m_preserveNativePages{false};
};

#endif // TERRAIN_VIRTUAL_ATLAS_H

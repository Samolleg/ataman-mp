#ifndef TERRAIN_ATLAS_PROFILE_H
#define TERRAIN_ATLAS_PROFILE_H

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

#include "terrain_atlas_analyzer.h"

class CValidationReport;
class QFileInfo;
struct STerrainAtlasFinalization;

struct STerrainAtlasSlotUsage
{
    qint64 landCount{0};
    qint64 overlayCount{0};
    qint64 landBelowOverlayCount{0};
};

enum class ETerrainAtlasUnderwaterRole
{
    Surface,
    UnderwaterOnly,
    Mixed
};

enum class ETerrainAtlasProfileSource
{
    None,
    BuiltInDefault,
    LocalOverride,
    GeneratedDraft
};

struct STerrainAtlasProfileSource
{
    ETerrainAtlasProfileSource kind{ETerrainAtlasProfileSource::None};
    QString path;
};

// A paint material is a visually distinct game-wide variant backed by at least
// one real four-identical-corner tile. Variants share a palette theme (for
// example Grass) but retain independent vertex identities (for example Grass /
// Dark and Grass / Fresh).
struct STerrainGlobalMaterialSource
{
    QString mprNamespace;
    QByteArray atlasIdentity;
    QByteArray atlasPixelSha256;
    int textureCount{0};
    int atlasSlotCount{0};
    int uniformSlot{-1};
    QString variantName;
};

struct STerrainGlobalMaterialFamily
{
    qint32 id{0};
    QString name;
    QString theme;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    qint32 runtimeTerrainType{-1};
    // Automatic painting authority: every audited four-corner-uniform slot.
    QVector<STerrainGlobalMaterialSource> sources;
    // Display authority is independent and may intentionally choose a
    // different authored tile than the uniform paint pool.
    QVector<STerrainGlobalMaterialSource> thumbnailSources;
};

struct STerrainGlobalTransitionSource
{
    STerrainTransitionSignature signature;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    QString mprNamespace;
    // Semantic identity groups alternate reviewed pixel revisions. Physical
    // identity binds this source to one exact decoded atlas revision.
    QByteArray atlasIdentity;
    QByteArray atlasPixelSha256;
    int textureCount{0};
    int atlasSlotCount{0};
    int slot{-1};
};

struct STerrainAtlasProfileFamily
{
    qint32 id{0};
    QString name;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    bool underwater{false};
    bool landAllowed{true};
    bool overlayAllowed{false};
    ETerrainAtlasUnderwaterRole underwaterRole{ETerrainAtlasUnderwaterRole::Surface};
    bool ambiguous{false};
    bool paintSelectable{true};
    qint32 tileType{-1};
    int thumbnailSlot{-1};
    QVector<int> memberSlots;

    bool allows(ETerrainAtlasLayer candidate) const
    {
        return candidate == ETerrainAtlasLayer::Land ? landAllowed : overlayAllowed;
    }
};

struct STerrainAtlasProfileSlot
{
    int slot{-1};
    STerrainTransitionSignature signature;
    bool assigned{true};
    bool ambiguous{false};
    // Optional provenance for a compacted portable atlas slot. Several
    // byte-identical authored source slots may alias one retained physical
    // slot without losing their original identities after save/reopen.
    QString sourceNamespace;
    QVector<int> sourceSlotAliases;
    QByteArray sourcePixelSha256;
    // Exact canonical-generation route owner. Zero means authored or legacy
    // route-neutral provenance; it is never inferred from slot order.
    qint32 routeOwnerMaterial{0};
};

// A profile is always scoped to both an MPR namespace and the exact decoded
// atlas pixels. Structurally valid inferred entries are immediately usable;
// unresolved entries remain unassigned until a user fixes or excludes them.
struct STerrainAtlasProfile
{
    int schemaVersion{10};
    QString mprNamespace;
    QByteArray atlasPixelSha256;
    int atlasSlotCount{0};
    // Physical MMP pages owned by this exact atlas binding. Legacy profiles
    // derive it from atlasSlotCount; embedded profiles declare it explicitly.
    int textureCount{0};
    QString provenance;
    // Optional portable hint to the one RES archive that owns every page of a
    // published atlas. The decoded pixel hash remains the authority.
    QString resourceArchiveHint;
    bool approved{false};
    QVector<STerrainAtlasProfileFamily> families;
    QVector<STerrainAtlasProfileSlot> slotDefinitions;

    QJsonObject toJson() const;
    QByteArray toBytes(bool compact = true) const;
    static bool fromJson(const QJsonObject& object, STerrainAtlasProfile* profile,
                         CValidationReport* report = nullptr);
    static bool fromBytes(const QByteArray& bytes,
                          STerrainAtlasProfile* profile,
                          CValidationReport* report = nullptr,
                          const QString& sourceName = QString());
    bool save(const QString& path, CValidationReport* report = nullptr) const;
    static bool load(const QString& path, STerrainAtlasProfile* profile,
                     CValidationReport* report = nullptr);
    bool normalizeMembership(CValidationReport* report = nullptr);
    bool mergeFamilies(const QVector<qint32>& familyIds, const QString& mergedName,
                       CValidationReport* report = nullptr);
    bool splitFamily(qint32 familyId, const QVector<int>& selectedSlots,
                     const QString& newName,
                     CValidationReport* report = nullptr);
    bool assignSlotToFamily(int slot, qint32 familyId,
                            CValidationReport* report = nullptr);
    bool replaceSlotFamily(int slot, qint32 sourceFamilyId, qint32 targetFamilyId,
                           CValidationReport* report = nullptr);
    bool setSlotCornerFamily(int slot, int corner, qint32 familyId,
                             CValidationReport* report = nullptr);
    bool unassignSlot(int slot, CValidationReport* report = nullptr);
    bool setFamilyThumbnail(qint32 familyId, int slot,
                            CValidationReport* report = nullptr);
    int assignedSlotCount() const;
    bool validateBinding(const QString& currentNamespace,
                         const QByteArray& currentAtlasPixelSha256,
                         int currentSlotCount,
                         CValidationReport* report = nullptr) const;
    STerrainAtlasAnalysis usableAnalysis(CValidationReport* report = nullptr) const;
    STerrainAtlasAnalysis approvedAnalysis(CValidationReport* report = nullptr) const;
};

class CTerrainAtlasProfileStore
{
public:
    static QString embeddedMemberName(const QString& mprNamespace);
    static QString workingAtlasProvenance();
    static bool isWorkingAtlasProfile(const STerrainAtlasProfile& profile);
    static STerrainAtlasProfile portableFinalizedProfile(
        const STerrainAtlasFinalization& finalization,
        const QVector<QImage>& decodedPhysicalTiles,
        const STerrainAtlasProfile& sourceProfile,
        ETerrainAtlasLayer layer,
        const QMap<qint32, QString>& materialNames = {},
        const QMap<qint32, qint32>& materialRuntimeTypes = {},
        CValidationReport* report = nullptr);
    static STerrainAtlasProfile workingAtlasProfile(
        const QString& mprNamespace,
        const QVector<QImage>& decodedPhysicalTiles,
        const CTerrainTransitionCatalog& workingCatalog,
        const STerrainAtlasProfile& sourceProfile,
        const QVector<STerrainGlobalMaterialFamily>& globalFamilies,
        CValidationReport* report = nullptr);
    static QString profilePath(const QString& mprNamespace,
                               const QString& rootDirectory = QString());
    // Portable draft/finalized profiles describe one exact MPR + physical
    // atlas pair.  Key them by the MPR filename as well as the runtime
    // namespace so two maps that intentionally reuse one published identity
    // cannot overwrite each other's editor-side semantic contract.
    static QString profilePathForMpr(
        const QString& mprNamespace, const QFileInfo& mprFile);
    static bool save(const STerrainAtlasProfile& profile,
                     const QString& rootDirectory = QString(),
                     CValidationReport* report = nullptr);
    static bool saveForMpr(const STerrainAtlasProfile& profile,
                           const QFileInfo& mprFile,
                           CValidationReport* report = nullptr);
    static bool load(const QString& mprNamespace, STerrainAtlasProfile* profile,
                     const QString& rootDirectory = QString(),
                     CValidationReport* report = nullptr);
    static bool loadForMpr(const QString& mprNamespace,
                           const QFileInfo& mprFile,
                           STerrainAtlasProfile* profile,
                           QString* sourcePath = nullptr,
                           bool* usedLegacyNamespacePath = nullptr,
                           CValidationReport* report = nullptr);
    static bool loadExplicitOverride(
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        int atlasSlotCount,
        STerrainAtlasProfile* profile,
        const QString& rootDirectory = QString(),
        QString* sourcePath = nullptr,
        CValidationReport* report = nullptr);
    static bool loadExplicitOverrideForMpr(
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        int atlasSlotCount,
        const QFileInfo& mprFile,
        STerrainAtlasProfile* profile,
        QString* sourcePath = nullptr,
        bool* usedLegacyNamespacePath = nullptr,
        CValidationReport* report = nullptr);
    static bool removeLocalOverride(
        const QString& mprNamespace,
        const QString& rootDirectory = QString(),
        CValidationReport* report = nullptr);
};

class CTerrainAtlasProfileInference
{
public:
    static QString currentProvenance();
    static bool isCompatibleInferenceProvenance(const QString& provenance);
    static QByteArray pixelSha256(const QVector<QImage>& images);
    static STerrainAtlasProfile fromAuthoredAnalysis(
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        int atlasSlotCount,
        const STerrainAtlasAnalysis& analysis,
        CValidationReport* report = nullptr);
    static STerrainAtlasProfile propose(
        const QString& mprNamespace,
        const QVector<QImage>& tileImages,
        const QVector<qint32>& tileTypes,
        const QVector<STerrainAtlasSlotUsage>& usage,
        CValidationReport* report = nullptr);
};

class CTerrainAtlasPresetCatalog
{
public:
    static STerrainAtlasProfile exactProfile(
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        int atlasSlotCount,
        CValidationReport* report = nullptr);
    static int presetCount(CValidationReport* report = nullptr);
    static QString materialTheme(const QString& variantName);
    static qint32 globalMaterialId(const QString& variantName,
                                   ETerrainAtlasLayer layer,
                                   const QByteArray& atlasIdentity = QByteArray(),
                                   qint32 localFamilyId = 0);
    static QVector<STerrainGlobalMaterialFamily> globalUniformFamilies(
        CValidationReport* report = nullptr);
    // Resolve one palette/paint source against the exact currently loaded
    // MMP payload. The decoded cache is payload-addressed so opening another
    // map or RES with the same page member names cannot reuse stale artwork.
    static QImage loadGlobalUniformSource(
        const STerrainGlobalMaterialFamily& family,
        const QString& preferredNamespace,
        const QSize& requiredSize,
        QMap<QString, QByteArray>* encodedPageCache,
        QSet<QString>* missingPageCache,
        QMap<QString, QImage>* decodedTileCache,
        const std::function<bool(const QString&, QByteArray*)>& pageLoader,
        bool thumbnail = false,
        STerrainGlobalMaterialSource* selectedSource = nullptr,
        const std::function<bool(const QString&, int, QImage*, QString*)>&
            tileLoader = {});
    // Resolve one authored transition against the current resource payload.
    // Unlike a page-name-only cache, this remains correct when another
    // configured RES supplies different bytes for the same MMP member.
    static QImage loadGlobalTransitionSource(
        const STerrainGlobalTransitionSource& source,
        const QSize& requiredSize,
        QMap<QString, QByteArray>* encodedPageCache,
        QSet<QString>* missingPageCache,
        QMap<QString, QImage>* decodedTileCache,
        const std::function<bool(const QString&, QByteArray*)>& pageLoader);
    static QVector<STerrainGlobalTransitionSource> globalAuthoredTransitions(
        CValidationReport* report = nullptr);
    static CTerrainTransitionCatalog globalizedCatalog(
        const STerrainAtlasProfile& profile,
        const CTerrainTransitionCatalog& source,
        const QVector<STerrainGlobalMaterialFamily>* globalFamilies = nullptr);
};

QImage terrainVertexJunctionThumbnail(
    const QVector<QImage>& images, const CTerrainTransitionCatalog& catalog,
    qint32 materialId, qint32 supportId, int fallbackSlot);

#endif

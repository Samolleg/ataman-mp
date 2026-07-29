#ifndef TERRAIN_ATLAS_PROFILE_H
#define TERRAIN_ATLAS_PROFILE_H

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

#include "terrain_atlas_analyzer.h"

class CValidationReport;
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
    int textureCount{0};
    int uniformSlot{-1};
    QString variantName;
};

struct STerrainGlobalMaterialFamily
{
    qint32 id{0};
    QString name;
    QString theme;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    QVector<STerrainGlobalMaterialSource> sources;
};

struct STerrainGlobalTransitionSource
{
    STerrainTransitionSignature signature;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    QString mprNamespace;
    int textureCount{0};
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
};

// A profile is always scoped to both an MPR namespace and the exact decoded
// atlas pixels. Structurally valid inferred entries are immediately usable;
// unresolved entries remain unassigned until a user fixes or excludes them.
struct STerrainAtlasProfile
{
    int schemaVersion{7};
    QString mprNamespace;
    QByteArray atlasPixelSha256;
    int atlasSlotCount{0};
    QString provenance;
    bool approved{false};
    QVector<STerrainAtlasProfileFamily> families;
    QVector<STerrainAtlasProfileSlot> slotDefinitions;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& object, STerrainAtlasProfile* profile,
                         CValidationReport* report = nullptr);
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
    static STerrainAtlasProfile portableFinalizedProfile(
        const STerrainAtlasFinalization& finalization,
        const QVector<QImage>& decodedPhysicalTiles,
        const STerrainAtlasProfile& sourceProfile,
        ETerrainAtlasLayer layer,
        const QMap<qint32, QString>& materialNames = {},
        CValidationReport* report = nullptr);
    static QString profilePath(const QString& mprNamespace,
                               const QString& rootDirectory = QString());
    static bool save(const STerrainAtlasProfile& profile,
                     const QString& rootDirectory = QString(),
                     CValidationReport* report = nullptr);
    static bool load(const QString& mprNamespace, STerrainAtlasProfile* profile,
                     const QString& rootDirectory = QString(),
                     CValidationReport* report = nullptr);
    static bool loadExplicitOverride(
        const QString& mprNamespace,
        const QByteArray& atlasPixelSha256,
        int atlasSlotCount,
        STerrainAtlasProfile* profile,
        const QString& rootDirectory = QString(),
        QString* sourcePath = nullptr,
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
    static QVector<STerrainGlobalTransitionSource> globalAuthoredTransitions(
        CValidationReport* report = nullptr);
    static CTerrainTransitionCatalog globalizedCatalog(
        const STerrainAtlasProfile& profile,
        const CTerrainTransitionCatalog& source);
};

QImage terrainVertexJunctionThumbnail(
    const QVector<QImage>& images, const CTerrainTransitionCatalog& catalog,
    qint32 materialId, qint32 supportId, int fallbackSlot);

#endif

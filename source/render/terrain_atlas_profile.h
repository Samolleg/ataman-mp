#ifndef TERRAIN_ATLAS_PROFILE_H
#define TERRAIN_ATLAS_PROFILE_H

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "terrain_atlas_analyzer.h"

class CValidationReport;

struct STerrainAtlasSlotUsage
{
    qint64 landCount{0};
    qint64 overlayCount{0};
    qint64 landBelowOverlayCount{0};
};

struct STerrainAtlasProfileFamily
{
    qint32 id{0};
    QString name;
    ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
    bool underwater{false};
    bool ambiguous{false};
    qint32 tileType{-1};
    QVector<int> memberSlots;
};

struct STerrainAtlasProfileSlot
{
    int slot{-1};
    STerrainTransitionSignature signature;
    bool ambiguous{false};
};

// A profile is always scoped to both an MPR namespace and the exact decoded
// atlas pixels. Inferred profiles are proposals only; semantic painting must
// remain disabled until a human has reviewed and approved one.
struct STerrainAtlasProfile
{
    int schemaVersion{1};
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
    bool validateBinding(const QString& currentNamespace,
                         const QByteArray& currentAtlasPixelSha256,
                         int currentSlotCount,
                         CValidationReport* report = nullptr) const;
    STerrainAtlasAnalysis approvedAnalysis(CValidationReport* report = nullptr) const;
};

class CTerrainAtlasProfileStore
{
public:
    static QString profilePath(const QString& mprNamespace,
                               const QString& rootDirectory = QString());
    static bool save(const STerrainAtlasProfile& profile,
                     const QString& rootDirectory = QString(),
                     CValidationReport* report = nullptr);
    static bool load(const QString& mprNamespace, STerrainAtlasProfile* profile,
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

#endif

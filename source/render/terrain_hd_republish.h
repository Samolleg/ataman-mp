#ifndef TERRAIN_HD_REPUBLISH_H
#define TERRAIN_HD_REPUBLISH_H

#include "terrain_atlas_profile.h"
#include "res_archive_index.h"
#include "res_archive_document.h"
#include "mpr_document.h"
#include <QJsonObject>
#include <QSharedPointer>
#include <QCache>
#include "terrain_hd_progress.h"
#include "terrain_hd_provenance.h"

// Cross-namespace evidence index. Approximate matches require a calibrated
// error bound, structural agreement and a margin over a different HD result.
class CTerrainHdSources
{
public:
    struct Source { int archive{-1}; QString page; int slot{-1}; int size{0}; int rotation{0}; };
    explicit CTerrainHdSources(const QVector<STerrainConfirmedHdProvenance>& rules = terrainConfirmedHdProvenance()) : m_confirmed(rules) {}
    bool open(const QStringList& archives, QString* error, CTerrainHdProgress* progress = nullptr,
              const QString& cacheDirectory = {});
    // Explicitly deferred until preparation has succeeded and cancellation is
    // no longer available. open/rebuild/cancel never create cache or data files.
    bool saveCache() const;
    bool cacheHit() const { return m_cacheHit; }
    bool match(const QImage& published, const STerrainAtlasProfileSlot& provenance,
               Source* source, QString* reason, QJsonObject* evidence = nullptr,
               const STerrainAtlasProfileFamily* family = nullptr) const;
    // Called only with a decoded, exact-bound stock witness and verified donor.
    bool addExactSdWitness(const QImage& sd, const Source& hd, const QByteArray& expectedHdSha);
    bool matchNativeHd(const QImage& high, const QImage& mip128, Source* source) const;
    QImage image(const Source& source, int size, QString* error = nullptr) const;
    QByteArray payload(const Source& source, QString* error = nullptr) const;
    bool current(QString* error) const;
    bool find(const QString& nameSpace, int slot, Source* source) const;
    int sourceCount() const { return m_sources.size(); }
    QJsonObject calibration() const { return m_calibration; }
private:
    QVector<STerrainConfirmedHdProvenance> m_confirmed;
    QHash<QByteArray,QVector<int>> m_exactSdAliases;
    bool loadCache();
    QByteArray cacheBytes() const;
    QString m_cachePath;
    QByteArray m_cacheKey;
    bool m_cacheHit{false};
    CTerrainHdProgress* m_progress{nullptr}; // borrowed for this preparation only
    struct Descriptor { int source; int rotation; int reduction; QVector<float> mean; };
    QVector<QSharedPointer<CResArchiveIndex>> m_archives;
    QVector<Source> m_sources;
    QHash<QByteArray,QVector<int>> m_byPixels;
    QHash<QByteArray,int> m_byNativePixels;
    QHash<QString,int> m_byIdentity;
    QHash<QByteArray,QStringList> m_sdOnly;
    QHash<QString,STerrainAtlasProfile> m_origins;
    mutable QHash<int,QByteArray> m_hdHashes;
    mutable QMap<QString,QByteArray> m_payloadCache;
    mutable QCache<QString,QImage> m_imageCache{32768}; // KiB; decoded recipe inputs
    QVector<Descriptor> m_descriptors;
    mutable QHash<QByteArray,QJsonObject> m_matchCache;
    QJsonObject m_calibration;
    double m_mseLimit{36.0};
};

struct STerrainHdRepublishResult
{
    bool success{false};
    bool cancelled{false};
    QString error;
    STerrainAtlasProfile profile;
    QVector<QImage> before, after;
    QVector<QByteArray> pages;
    QByteArray mprBytes;
    QVector<SResArchiveNewMember> publishedMembers;
    QJsonObject receipt;
    STerrainAtlasProfile originalProfile;
    QMap<int,int> slotRemap;
};

STerrainHdRepublishResult rebuildTerrainAtlasHd(
    const STerrainAtlasProfile& profile, const QVector<QByteArray>& authoredPages,
    const CTerrainHdSources& sources, bool upgradePartialPages = true,
    const QVector<SMprAnimatedTileRecord>& animations = {},
    const QByteArray& previousPublication = {}, CTerrainHdProgress* progress = nullptr);

// Conservative garbage collection: incomplete inventories never authorize removal.
bool buildTerrainHdCleanup(const CResArchiveDocument& archive, const QString& oldNamespace,
    const QString& newNamespace, const QString& replacedMap, const QStringList& mapRoots,
    QByteArray* candidate, QJsonObject* receipt, QString* error, CTerrainHdProgress* progress = nullptr);
bool commitTerrainHdCleanup(const QString& archivePath, const QByteArray& expected,
    const QByteArray& candidate, const QJsonObject& receipt, QString* error);

class CMprDocument;
bool prepareTerrainHdPublication(const CMprDocument& map,
    const QByteArray& oldSemantic, STerrainHdRepublishResult* result, CTerrainHdProgress* progress = nullptr);

int runTerrainHdRepublish(const QString& fixturePath);

#endif

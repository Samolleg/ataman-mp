#ifndef TERRAIN_ATLAS_COMPACT_H
#define TERRAIN_ATLAS_COMPACT_H
#include "mpr_document.h"
#include "terrain_atlas_profile.h"
#include "res_archive_document.h"
#include <atomic>
class CResArchiveIndex;

bool terrainAtlasPageLimit(int pages, QString* error);
bool terrainAtlasHasHash(const QString& name);
QStringList terrainAtlasMapDirectories(const QString& map, const QStringList& archives, const QStringList& mapRoots = {});
struct STerrainPackCleanupRow {
    QString name; int pages{0}; qint64 bytes{0}; bool removable{false}; QString usage;
};
struct STerrainPackCleanupPlan {
    QString error; QVector<STerrainPackCleanupRow> rows; QStringList members;
    QMap<QString,QByteArray> inventory; QByteArray archiveSha256;
    qint64 before{0},after{0};
};
STerrainPackCleanupPlan prepareTerrainPackCleanup(const QString& archive, const QStringList& directories);
bool commitTerrainPackCleanup(const QString& archive, const QStringList& directories,
    const STerrainPackCleanupPlan& plan, QString* error, const QString& backupRoot = QString());
QString retireTerrainAtlasAfterSave(const QString& archive, const STerrainAtlasProfile& oldProfile,
    const QString& map, const QStringList& directories, const QString& backupRoot = QString());
void terrainAtlasCopySourceIdentities(STerrainAtlasProfile* profile, const class CTerrainWorkingAtlas& atlas);

struct STerrainAtlasCompactPlan {
    QString error;
    bool noOp{false};
    // Preserve the exact source member, including its absence, for the commit
    // conflict guard. A loaded profile may have been migrated in memory.
    bool sourceProfilePresent{true};
    QByteArray sourceProfileBytes;
    QVector<QByteArray> sourcePageSha256;
    QByteArray mprBytes;
    STerrainAtlasProfile profile;
    QVector<QByteArray> pages;
    QVector<QImage> images;
    QVector<SResArchiveNewMember> members;
    QMap<int,int> remap;
    int oldPages{0}, oldSlots{0}, retainedSlots{0};
    qint64 oldDxt{0}, newDxt{0}, oldRgba{0}, newRgba{0};
    qint64 archiveBefore{0}, archiveEstimatedAfter{0};
    QString retirementReason;
};
bool terrainCompactProfileMatchesPublished(const STerrainAtlasProfile& profile,
    const QByteArray& publishedBytes);
// Pure preparation: no disk writes, no global resource lookup or repair.
// An explicit empty publishedProfileBytes means no embedded source profile.
// nullptr retains the canonical-profile expectation of in-memory callers.
STerrainAtlasCompactPlan prepareTerrainAtlasCompact(const CMprDocument& source,
    const STerrainAtlasProfile& profile, const QVector<QByteArray>& pages,
    const QByteArray& semanticBytes, const QFileInfo& mpr,
    const std::atomic_bool* cancelled = nullptr, bool forcePublication = false,
    const QVector<QImage>* decodedSource = nullptr,
    const QByteArray* publishedProfileBytes = nullptr);
bool compactTerrainPublication(QByteArray* mprBytes, STerrainAtlasProfile* profile,
    QVector<SResArchiveNewMember>* members, QVector<QImage>* images,
    const QFileInfo& map, QString* error);
// Reuse the published identity only when all page bytes, profile meaning and
// semantic fields match. Rebind the candidate MPR to that exact publication.
bool reuseTerrainPublication(const CResArchiveIndex& archive,
    const STerrainAtlasProfile& previous, QByteArray* mprBytes,
    STerrainAtlasProfile* profile, QVector<SResArchiveNewMember>* members,
    const QFileInfo& map);
// Rebuild only explicitly named owned members; prove all other payloads/times.
bool buildTerrainArchiveWithout(const CResArchiveDocument& source,
    const QStringList& names, QByteArray* output, QString* error);
// Fail closed: hash-owned portable namespace and complete readable map inventory.
QStringList terrainCompactRetiredMembers(const CResArchiveDocument& archive,
    const STerrainAtlasProfile& profile, const QString& currentMap,
    const QStringList& mapDirectories, QMap<QString,QByteArray>* inventory,
    QString* reason);
bool terrainCompactInventoryUnchanged(const QMap<QString,QByteArray>& inventory,
    const QStringList& directories, const QString& currentMap);
bool estimateTerrainCompactPublication(STerrainAtlasCompactPlan* plan,
    const STerrainAtlasProfile& oldProfile,const QString& mapPath,
    const QString& archivePath,const QStringList& mapDirectories);
struct STerrainAtlasCompactCommit { bool success{false}; QString error, cleanup; qint64 archiveBefore{0},archiveAfter{0}; };
STerrainAtlasCompactCommit commitTerrainAtlasCompact(const CMprDocument& source,
    const STerrainAtlasProfile& oldProfile, const STerrainAtlasCompactPlan& plan,
    const QString& mapPath, const QString& archivePath, const QStringList& inventoryDirs,
    const QString& backupRoot = QString(), const QString& recoveryRoot = QString());
#endif

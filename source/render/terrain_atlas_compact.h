#ifndef TERRAIN_ATLAS_COMPACT_H
#define TERRAIN_ATLAS_COMPACT_H
#include "mpr_document.h"
#include "terrain_atlas_profile.h"
#include "res_archive_document.h"
#include <atomic>

struct STerrainAtlasCompactPlan {
    QString error;
    bool noOp{false};
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
// Pure preparation: no disk writes, no global resource lookup or repair.
STerrainAtlasCompactPlan prepareTerrainAtlasCompact(const CMprDocument& source,
    const STerrainAtlasProfile& profile, const QVector<QByteArray>& pages,
    const QByteArray& semanticBytes, const QFileInfo& mpr,
    const std::atomic_bool* cancelled = nullptr);
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

#ifndef TERRAIN_BASE_HD_H
#define TERRAIN_BASE_HD_H
#include "terrain_virtual_atlas.h"
#include "terrain_atlas_profile.h"
#include <atomic>

struct SBaseHdPlan {
    QString error;
    QSharedPointer<CTerrainWorkingAtlas> atlas;
    STerrainTileCommand command;
    int eligibleCells{0};
    int unmatchedCells{0};
    int seamRejectedCells{0};
    int importedVariants{0};
    int changedSources{0};
    QStringList unmatchedSources;
    int forecastPages{0}; qint64 forecastDxt{0},forecastRgba{0};
};
SBaseHdPlan prepareBaseTerrainHd(const QString& library,
    const CTerrainWorkingAtlas& atlas, const CTerrainEditModel& terrain,
    const STerrainAtlasProfile& profile,
    const QVector<SMprAnimatedTileRecord>& animations, quint32 seed,
    const std::atomic_bool* cancelled = nullptr,
    const QVector<QByteArray>& sourcePages = {});
#endif

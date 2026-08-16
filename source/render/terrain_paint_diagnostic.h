#ifndef TERRAIN_PAINT_DIAGNOSTIC_H
#define TERRAIN_PAINT_DIAGNOSTIC_H

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

#include "terrain_atlas_profile.h"
#include "terrain_virtual_atlas.h"

QJsonObject terrainWorkingPhysicalIdentityJson(
    const STerrainWorkingTileIdentity& identity,
    const QString& currentMapNamespace,
    const QVector<STerrainGlobalMaterialFamily>& materialFamilies,
    const QMap<qint32, STerrainGlobalMaterialSource>& materialSources,
    const QMap<qint32, QByteArray>& materialPixelSha256);

QJsonObject terrainAuthoredPhysicalIdentityJson(
    int currentMapSlot, const QByteArray& physicalPixelSha256,
    const QString& currentMapNamespace, qint32 runtimeTerrainType,
    const STerrainAtlasProfileSlot* profileSlot = nullptr);

#endif // TERRAIN_PAINT_DIAGNOSTIC_H

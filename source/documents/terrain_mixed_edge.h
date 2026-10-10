#ifndef TERRAIN_MIXED_EDGE_H
#define TERRAIN_MIXED_EDGE_H

#include "terrain_virtual_atlas.h"
#include "terrain_recipe_bake.h"

// Private staging only; the caller owns committing model+atlas and uploading
// the preview together. No file writes and no alternative publication path.
class CTerrainMixedEdge
{
public:
    static STerrainTileCommand resolve(const CTerrainEditModel& model,
        CTerrainWorkingAtlas* stagedAtlas, const QSet<int>& affectedCells,
        const QMap<qint32, qint32>& materialTypes,
        const QVector<SMprAnimatedTileRecord>& animations,
        const QPoint& worldCellOrigin, qint32 paintedMaterial,
        quint32 uniformSeed, bool randomizeUniformRotation,
        CValidationReport* report, int slotLimit = 4096);
};

#endif

#ifndef TERRAIN_MIXED_EDGE_H
#define TERRAIN_MIXED_EDGE_H

#include "terrain_virtual_atlas.h"
#include "terrain_recipe_bake.h"

// Private staging only; the caller owns committing model+atlas and uploading
// the preview together. No file writes and no alternative publication path.
class CTerrainMixedEdge
{
public:
    // Freeze the reviewed SD ownership bank for deterministic HD regeneration.
    // This is the same selection/extraction path used by normal B2 painting.
    static bool buildStockBrushes(const QVector<QImage>& images,
        const QMap<qint32,QVector<int>>& uniforms,
        const QMap<QPair<qint32,qint32>,QVector<int>>& stock,
        const QMap<int,STerrainTransitionSignature>& sourceSignatures,
        const QSet<QPair<qint32,qint32>>& pairs,
        QMap<QPair<qint32,qint32>,STerrainRecipeBrush>* output,
        CValidationReport* report = nullptr);
    static STerrainTileCommand resolve(const CTerrainEditModel& model,
        CTerrainWorkingAtlas* stagedAtlas, const QSet<int>& affectedCells,
        const QMap<qint32, qint32>& materialTypes,
        const QVector<SMprAnimatedTileRecord>& animations,
        const QPoint& worldCellOrigin, qint32 paintedMaterial,
        quint32 uniformSeed, bool randomizeUniformRotation,
        CValidationReport* report, int slotLimit = 4096);
};

#endif

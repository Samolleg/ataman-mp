#ifndef TERRAIN_RECIPE_BAKE_H
#define TERRAIN_RECIPE_BAKE_H

#include <QImage>
#include <QMap>
#include <QStringList>
#include <QVector>
#include "terrain_edit_model.h"

// Offline RGB land bake only. No atlas publication, packed references, UI,
// overlay alpha, animation, compression or mutation of the supplied field.
struct STerrainRecipeMaterial
{
    qint32 materialId{0};
    qint32 runtimeTerrainType{-1};
    // Full, opaque, square profile tiles, in reviewed physical-slot order.
    QVector<QImage> uniformTiles;
};

struct STerrainRecipeBrush
{
    qint32 firstMaterial{0}; // ascending semantic IDs
    qint32 secondMaterial{0};
    // Unblurred RGB ownership estimate, along-major 512 x 65, [0,1].
    // Normal coordinates -32..32; 0 belongs to first, 1 to second.
    QVector<float> ownership;
};

struct STerrainRecipeEdge
{
    qint32 firstMaterial{0};
    qint32 secondMaterial{0};
    int state{0}; // sole supported state: midpoint crossing, fixed pair phase
};

struct STerrainRecipe
{
    STerrainTransitionSignature signature;
    // NW->NE, NE->SE, SW->SE, NW->SW, in positive local grid axes.
    STerrainRecipeEdge edges[4];
    int variant{0};
    qint32 runtimeTerrainType{-1};
    bool supported{true};
};

struct STerrainRecipeBudget
{
    quint64 recipeSlots{0};
    quint64 uniqueSlots{0};
    quint64 reservedSlots{0};
    quint64 requiredSlots{0};
    quint64 pages{0};
    bool exceeds2048{false};
    bool exceeds4096{false};
    bool exceeds8192{false};
    // False on refused plans: uniqueSlots is then the conservative recipe
    // count, without claiming a completed pixel-deduplication measurement.
    bool pixelDedupComplete{true};
};

struct STerrainRecipeBakeOptions
{
    int tileSize{128};
    int variants{4};
    // Incremental editing must not renumber labels when the group grows.
    // Offline balanced assignment remains the default for existing callers.
    bool stableCoordinateVariants{false};
    QPoint origin;
    quint64 reservedSlots{0}; // pinned other-layer/animation slots, supplied by caller
    quint64 slotLimit{4096};
    // Explicit legacy stage-A top-two preview for comparison only. Marks 3/4
    // nodes unsupported and success=false; default uses supported B1 composition.
    bool diagnosticJunctionPreview{false};
    // MVP's exact predecessor-preserving mode. Requires one reviewed class
    // per cell in preferredRuntimeTypes; ordinary generation uses majority/tie.
    bool preserveReviewedCellRuntimeTypes{false};
};

struct STerrainRecipeBakeInput
{
    int tileWidth{0};
    int tileHeight{0};
    // Modern grid row order. Cell (x,y): NW=(x,y), NE=(x+1,y),
    // SE=(x+1,y+1), SW=(x,y+1). QImage rows are reversed (north-up).
    QVector<qint32> vertexMaterials;
    QVector<STerrainRecipeMaterial> materials;
    QVector<STerrainRecipeBrush> brushes;
    // Empty or one entry per cell; used only for equal maximum class coverage.
    // Count corners by runtime class (aliases accumulate). A 1+1+1+1 tie
    // requires a preferred class/material among those tied; no implicit ID tie.
    // A three-way maximum tie is impossible with four equally weighted corners.
    QVector<qint32> preferredMaterials;
    QVector<qint32> preferredRuntimeTypes;
};

struct STerrainRecipeBakedTile
{
    QImage pixels; // opaque RGBA8888; hash excludes scanline padding
    QByteArray pixelSha256;
    qint32 runtimeTerrainType{-1};
};

struct STerrainRecipeBakeResult
{
    bool success{false};
    bool previewOnly{false};
    QStringList diagnostics;
    QVector<int> unsupportedCells;
    QVector<STerrainRecipe> recipes;
    QVector<STerrainRecipeBakedTile> tiles;
    QVector<int> tileByCell;
    STerrainRecipeBudget budget;
};

class CTerrainRecipeBaker
{
public:
    static STerrainRecipeBakeResult bake(const STerrainRecipeBakeInput& input,
        const STerrainRecipeBakeOptions& options = STerrainRecipeBakeOptions());
    static STerrainRecipeBudget budget(quint64 recipeSlots, quint64 uniqueSlots,
                                      quint64 reservedSlots = 0);
    // RGB projection against uniform profile pixels. This estimates ownership,
    // not original authoring alpha. All images must be opaque and same-sized.
    static bool estimateStockOwnership(const QImage& transition,
        const QImage& firstUniform, const QImage& secondUniform,
        QVector<float>* ownership, double* rgbRmse, QString* error = nullptr);
    // Unroll a profile tile's coarse two-material contour into an unblurred
    // ownership strip. Supports straight, 1/3 and alternating 2/2 topologies;
    // rejects poor RGB fits/corner evidence/pure sides. Caller can pool strips
    // in reviewed slot order with makeBrush(). Inputs contain no stock assets.
    static bool extractStockStrip(const QImage& transition,
        const QImage& firstUniform, const QImage& secondUniform,
        qint32 firstMaterial, qint32 secondMaterial,
        const STerrainTransitionSignature& physicalSignature,
        QVector<float>* strip, QString* error = nullptr);
    // Assemble runtime-extracted, along-major 65-wide strips into one periodic
    // bank. No embedded stock masks. Canonical input order is the caller's duty.
    static bool makeBrush(qint32 firstMaterial, qint32 secondMaterial,
        const QVector<QVector<float>>& strips, STerrainRecipeBrush* brush,
        QString* error = nullptr);
};

#endif

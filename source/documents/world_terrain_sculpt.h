#ifndef WORLD_TERRAIN_SCULPT_H
#define WORLD_TERRAIN_SCULPT_H
#include <QPointF>
#include <functional>
#include <QSharedPointer>
#include "mpr_document.h"
#include "terrain_edit_model.h"

enum class EWorldSculptMode { Raise, Lower, Flatten, Smooth, BoundaryFill, Noise };
struct SWorldTerrainTarget
{
    QString id;
    QPointF origin;
    const CTerrainEditModel* model{nullptr};
    bool editable{false};
};
class CTerrainWorkingAtlas;
struct STerrainMobHeightState;
struct SWorldTerrainPaintState
{
    CTerrainEditModel terrain;
    QSharedPointer<CTerrainWorkingAtlas> atlas;
    QVector<SMprMaterialRecord> materials;
};
struct SWorldTerrainPaintChange
{
    SWorldTerrainPaintState before,after;
};
struct SWorldTerrainStroke
{
    QString error;
    int localUndoIndex{-1};
    int protectedBoundaryVertices{0}; // Per-stamp count; repeated stamps may revisit a vertex.
    QMap<QString, STerrainHeightCommand> maps;
    QMap<QString, SOverlaySurfaceCommand> overlays;
    QMap<QString, STerrainTileCommand> tiles;
    QMap<QString, SWorldTerrainPaintChange> paint;
    // Optional editor companion; canonical MPR algorithms never inspect it.
    QSharedPointer<STerrainMobHeightState> mobHeightState;
    bool isEmpty() const { return maps.isEmpty() && overlays.isEmpty() && tiles.isEmpty() && paint.isEmpty(); }
    void append(const SWorldTerrainStroke& stamp);
};
// All inputs are immutable for the entire stamp. Values are world heights,
// not raw uint16 samples from one arbitrary map's height scale.
SWorldTerrainStroke makeWorldTerrainStamp(
    const QVector<SWorldTerrainTarget>& targets, const STerrainBrush& worldBrush,
    EWorldSculptMode mode, double worldDelta, double worldFlatten, double strength, quint32 noiseSeed=0,
    const std::function<double(double,double)>& influence = {},
    const std::function<double(double,double,double)>& transform = {});
#endif

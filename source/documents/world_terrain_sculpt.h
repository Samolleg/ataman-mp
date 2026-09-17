#ifndef WORLD_TERRAIN_SCULPT_H
#define WORLD_TERRAIN_SCULPT_H
#include <QPointF>
#include "terrain_edit_model.h"

enum class EWorldSculptMode { Raise, Lower, Flatten, Smooth, BoundaryFill };
struct SWorldTerrainTarget
{
    QString id;
    QPointF origin;
    const CTerrainEditModel* model{nullptr};
    bool editable{false};
};
struct SWorldTerrainStroke
{
    QMap<QString, STerrainHeightCommand> maps;
    bool isEmpty() const { return maps.isEmpty(); }
    void append(const SWorldTerrainStroke& stamp);
};
// All inputs are immutable for the entire stamp. Values are world heights,
// not raw uint16 samples from one arbitrary map's height scale.
SWorldTerrainStroke makeWorldTerrainStamp(
    const QVector<SWorldTerrainTarget>& targets, const STerrainBrush& worldBrush,
    EWorldSculptMode mode, double worldDelta, double worldFlatten, double strength);
#endif

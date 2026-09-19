#ifndef NEIGHBOR_MAP_LAYOUT_H
#define NEIGHBOR_MAP_LAYOUT_H

#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "map_catalog.h"
#include "world_map_layout.h"

struct SNeighborMapPlacement
{
    QString zoneId;
    QString mprPath;
    QString mobPath;
    QPointF worldOrigin;
    QPointF origin;
    QSizeF size;
    int routeDistance{0};
    bool fixedAnchor{false};
};

struct SNeighborMapLayoutResult
{
    QVector<SNeighborMapPlacement> placements;
    QStringList messages;
    QPointF activeWorldOrigin;
    QString componentAnchorZoneId;
    bool hasActiveWorldOrigin{false};
};

class CNeighborMapLayout
{
public:
    static SNeighborMapLayoutResult build(
        const QVector<SMapCatalogEntry>& topology,
        const QVector<SWorldMapZoneOrigin>& worldOrigins,
        const QString& profileId, const QString& activeAllodId,
        const QString& activeZoneId, int maximumNeighbors = -1);
};

#endif // NEIGHBOR_MAP_LAYOUT_H

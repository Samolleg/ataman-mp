#ifndef NEIGHBOR_MAP_LAYOUT_H
#define NEIGHBOR_MAP_LAYOUT_H

#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "map_catalog.h"

struct SNeighborMapPlacement
{
    QString zoneId;
    QString mprPath;
    QPointF origin;
    QSizeF size;
    int routeDistance{0};
};

struct SNeighborMapLayoutResult
{
    QVector<SNeighborMapPlacement> placements;
    QStringList messages;
};

class CNeighborMapLayout
{
public:
    static SNeighborMapLayoutResult build(
        const QVector<SMapCatalogEntry>& topology,
        const QString& activeZoneId, int maximumNeighbors = 5);
};

#endif // NEIGHBOR_MAP_LAYOUT_H

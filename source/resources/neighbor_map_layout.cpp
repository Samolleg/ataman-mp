#include "neighbor_map_layout.h"

#include <QHash>
#include <QMap>
#include <QPair>
#include <QQueue>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
enum class EBoundary { Invalid, Left, Right, Bottom, Top };

struct SDirectedOffset
{
    QString destination;
    QPointF delta;
};

struct SOffsetAccumulator
{
    QPointF total;
    int count{0};
};

QString key(const QString& value)
{
    return value.toCaseFolded();
}

QSizeF worldSize(const SMapCatalogEntry& entry)
{
    return QSizeF(double(entry.sectorWidth) * 32.0,
                  double(entry.sectorHeight) * 32.0);
}

bool renderable(const SMapCatalogEntry& entry)
{
    return !entry.mprPath.isEmpty()
        && entry.sectorWidth > 0 && entry.sectorHeight > 0;
}

const SMapTravelZone* exitWithDeploy(const SMapCatalogEntry& entry, int exitId)
{
    for(const SMapTravelZone& route : entry.travelZones)
        if(route.exitId == exitId && route.hasDeployRectangle
           && !route.deployRectangle.normalized().isEmpty())
            return &route;
    return nullptr;
}

EBoundary boundaryFor(const QRectF& rectangle, const QSizeF& size)
{
    const QPointF center = rectangle.normalized().center();
    const QVector<QPair<double, EBoundary>> distances = {
        {std::abs(center.x()), EBoundary::Left},
        {std::abs(size.width() - center.x()), EBoundary::Right},
        {std::abs(center.y()), EBoundary::Bottom},
        {std::abs(size.height() - center.y()), EBoundary::Top}
    };
    QPair<double, EBoundary> nearest = distances.first();
    double second = std::numeric_limits<double>::max();
    for(const QPair<double, EBoundary>& candidate : distances)
    {
        if(candidate.first < nearest.first)
        {
            second = nearest.first;
            nearest = candidate;
        }
        else if(candidate.second != nearest.second && candidate.first < second)
            second = candidate.first;
    }
    if(nearest.first > 32.0 || second - nearest.first < 0.5)
        return EBoundary::Invalid;
    return nearest.second;
}

bool opposite(EBoundary left, EBoundary right)
{
    return (left == EBoundary::Left && right == EBoundary::Right)
        || (left == EBoundary::Right && right == EBoundary::Left)
        || (left == EBoundary::Bottom && right == EBoundary::Top)
        || (left == EBoundary::Top && right == EBoundary::Bottom);
}

bool pairedOffset(const SMapCatalogEntry& source, int sourceExit,
                  const SMapCatalogEntry& destination, int destinationExit,
                  QPointF* delta)
{
    const SMapTravelZone* sourceRoute = exitWithDeploy(source, sourceExit);
    const SMapTravelZone* destinationRoute =
        exitWithDeploy(destination, destinationExit);
    if(!sourceRoute || !destinationRoute || !delta)
        return false;
    const QSizeF sourceSize = worldSize(source);
    const QSizeF destinationSize = worldSize(destination);
    const EBoundary sourceBoundary = boundaryFor(
        sourceRoute->deployRectangle, sourceSize);
    const EBoundary destinationBoundary = boundaryFor(
        destinationRoute->deployRectangle, destinationSize);
    if(sourceBoundary == EBoundary::Invalid
       || !opposite(sourceBoundary, destinationBoundary))
        return false;

    const QPointF sourceCenter = sourceRoute->deployRectangle.center();
    const QPointF destinationCenter = destinationRoute->deployRectangle.center();
    if(sourceBoundary == EBoundary::Right)
        *delta = QPointF(sourceSize.width(),
                         sourceCenter.y() - destinationCenter.y());
    else if(sourceBoundary == EBoundary::Left)
        *delta = QPointF(-destinationSize.width(),
                         sourceCenter.y() - destinationCenter.y());
    else if(sourceBoundary == EBoundary::Top)
        *delta = QPointF(sourceCenter.x() - destinationCenter.x(),
                         sourceSize.height());
    else
        *delta = QPointF(sourceCenter.x() - destinationCenter.x(),
                         -destinationSize.height());
    return true;
}

double rectangleDistance(const QRectF& left, const QRectF& right)
{
    const double dx = qMax(0.0, qMax(left.left() - right.right(),
                                     right.left() - left.right()));
    const double dy = qMax(0.0, qMax(left.top() - right.bottom(),
                                     right.top() - left.bottom()));
    return std::sqrt(dx * dx + dy * dy);
}
}

SNeighborMapLayoutResult CNeighborMapLayout::build(
    const QVector<SMapCatalogEntry>& topology,
    const QString& activeZoneId, int maximumNeighbors)
{
    SNeighborMapLayoutResult result;
    if(maximumNeighbors <= 0 || activeZoneId.isEmpty())
        return result;

    QHash<QString, int> indices;
    for(int index = 0; index < topology.size(); ++index)
        if(!topology.at(index).zoneId.isEmpty())
            indices.insert(key(topology.at(index).zoneId), index);
    const int activeIndex = indices.value(key(activeZoneId), -1);
    if(activeIndex < 0 || !renderable(topology.at(activeIndex)))
    {
        result.messages.append(QStringLiteral(
            "The active map has no renderable map.txt topology entry."));
        return result;
    }

    QMap<QString, SOffsetAccumulator> accumulated;
    const auto addRelation = [&](const SMapCatalogEntry& source, int sourceExit,
                                 const SMapCatalogEntry& destination,
                                 int destinationExit) {
        QPointF delta;
        if(!pairedOffset(source, sourceExit, destination, destinationExit,
                         &delta))
            return;
        const QString sourceKey = key(source.zoneId);
        const QString destinationKey = key(destination.zoneId);
        SOffsetAccumulator& forward = accumulated[
            sourceKey + QLatin1Char('|') + destinationKey];
        forward.total += delta;
        ++forward.count;
        SOffsetAccumulator& reverse = accumulated[
            destinationKey + QLatin1Char('|') + sourceKey];
        reverse.total -= delta;
        ++reverse.count;
    };

    for(const SMapCatalogEntry& source : topology)
    {
        if(!renderable(source))
            continue;
        for(const SMapTravelZone& sourceRoute : source.travelZones)
        {
            const int targetIndex = indices.value(
                key(sourceRoute.destinationZoneId), -1);
            if(targetIndex < 0)
                continue;
            const SMapCatalogEntry& target = topology.at(targetIndex);
            if(renderable(target))
            {
                addRelation(source, sourceRoute.exitId, target,
                            sourceRoute.destinationExitId);
                continue;
            }

            bool reverseConfirmed = false;
            for(const SMapTravelZone& edgeRoute : target.travelZones)
                if(edgeRoute.exitId == sourceRoute.destinationExitId
                   && key(edgeRoute.destinationZoneId) == key(source.zoneId)
                   && edgeRoute.destinationExitId == sourceRoute.exitId)
                {
                    reverseConfirmed = true;
                    break;
                }
            if(!reverseConfirmed)
                continue;
            for(const SMapTravelZone& edgeRoute : target.travelZones)
            {
                if(edgeRoute.exitId == sourceRoute.destinationExitId)
                    continue;
                const int destinationIndex = indices.value(
                    key(edgeRoute.destinationZoneId), -1);
                if(destinationIndex < 0)
                    continue;
                const SMapCatalogEntry& destination =
                    topology.at(destinationIndex);
                if(renderable(destination))
                    addRelation(source, sourceRoute.exitId, destination,
                                edgeRoute.destinationExitId);
            }
        }
    }

    QMap<QString, QVector<SDirectedOffset>> graph;
    for(auto it = accumulated.constBegin(); it != accumulated.constEnd(); ++it)
    {
        if(it.value().count <= 0)
            continue;
        const int separator = it.key().indexOf(QLatin1Char('|'));
        if(separator <= 0)
            continue;
        SDirectedOffset edge;
        edge.destination = it.key().mid(separator + 1);
        edge.delta = it.value().total / double(it.value().count);
        graph[it.key().left(separator)].append(edge);
    }
    for(auto it = graph.begin(); it != graph.end(); ++it)
        std::sort(it.value().begin(), it.value().end(),
                  [](const SDirectedOffset& left,
                     const SDirectedOffset& right) {
            return left.destination < right.destination;
        });

    const QString activeKey = key(activeZoneId);
    QHash<QString, QPointF> origins;
    QHash<QString, int> depths;
    QQueue<QString> pending;
    origins.insert(activeKey, QPointF());
    depths.insert(activeKey, 0);
    pending.enqueue(activeKey);
    const int maximumRouteDepth = 3;
    while(!pending.isEmpty())
    {
        const QString sourceKey = pending.dequeue();
        const int depth = depths.value(sourceKey);
        if(depth >= maximumRouteDepth)
            continue;
        for(const SDirectedOffset& edge : graph.value(sourceKey))
        {
            if(origins.contains(edge.destination))
                continue;
            origins.insert(edge.destination,
                           origins.value(sourceKey) + edge.delta);
            depths.insert(edge.destination, depth + 1);
            pending.enqueue(edge.destination);
        }
    }

    const SMapCatalogEntry& active = topology.at(activeIndex);
    const QRectF activeBounds(QPointF(), worldSize(active));
    struct SCandidate
    {
        SNeighborMapPlacement placement;
        double distance{0.0};
    };
    QVector<SCandidate> candidates;
    for(auto it = origins.constBegin(); it != origins.constEnd(); ++it)
    {
        if(it.key() == activeKey)
            continue;
        const int index = indices.value(it.key(), -1);
        if(index < 0 || !renderable(topology.at(index)))
            continue;
        const SMapCatalogEntry& entry = topology.at(index);
        SCandidate candidate;
        candidate.placement.zoneId = entry.zoneId;
        candidate.placement.mprPath = entry.mprPath;
        candidate.placement.origin = it.value();
        candidate.placement.size = worldSize(entry);
        candidate.placement.routeDistance = depths.value(it.key());
        candidate.distance = rectangleDistance(
            activeBounds, QRectF(it.value(), candidate.placement.size));
        candidates.append(candidate);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const SCandidate& left, const SCandidate& right) {
        if(std::abs(left.distance - right.distance) > 0.001)
            return left.distance < right.distance;
        if(left.placement.routeDistance != right.placement.routeDistance)
            return left.placement.routeDistance < right.placement.routeDistance;
        return left.placement.zoneId.toCaseFolded()
            < right.placement.zoneId.toCaseFolded();
    });
    const int count = qMin(maximumNeighbors, candidates.size());
    for(int index = 0; index < count; ++index)
        result.placements.append(candidates.at(index).placement);
    if(result.placements.isEmpty())
        result.messages.append(QStringLiteral(
            "No neighboring maps had an unambiguous opposite-edge deploy pair."));
    return result;
}

#include "world_map_layout.h"

#include <QHash>
#include <QMap>
#include <QPair>
#include <QQueue>
#include <QRectF>
#include <QSet>

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
    return value.trimmed().toCaseFolded();
}

QString relationKey(const QString& source, const QString& destination)
{
    return source + QChar(0x1f) + destination;
}

QSizeF worldSize(const SMapCatalogEntry& entry)
{
    return QSizeF(double(entry.sectorWidth) * 32.0,
                  double(entry.sectorHeight) * 32.0);
}

bool layoutable(const SMapCatalogEntry& entry)
{
    return !entry.zoneId.trimmed().isEmpty()
        && !entry.allodId.trimmed().isEmpty()
        && !entry.mprName.trimmed().isEmpty()
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

bool entryLess(const SMapCatalogEntry* left, const SMapCatalogEntry* right)
{
    const QString leftZone = key(left ? left->zoneId : QString());
    const QString rightZone = key(right ? right->zoneId : QString());
    if(leftZone != rightZone)
        return leftZone < rightZone;
    const QString leftMpr = key(left ? left->mprName : QString());
    const QString rightMpr = key(right ? right->mprName : QString());
    if(leftMpr != rightMpr)
        return leftMpr < rightMpr;
    if(left->sectorWidth != right->sectorWidth)
        return left->sectorWidth < right->sectorWidth;
    if(left->sectorHeight != right->sectorHeight)
        return left->sectorHeight < right->sectorHeight;
    return key(left->zoneType) < key(right->zoneType);
}

QMap<QString, const SMapCatalogEntry*> entriesForAllod(
    const QVector<SMapCatalogEntry>& topology, const QString& allodKey,
    QStringList* diagnostics)
{
    QVector<const SMapCatalogEntry*> candidates;
    for(const SMapCatalogEntry& entry : topology)
        if(key(entry.allodId) == allodKey && !key(entry.zoneId).isEmpty())
            candidates.append(&entry);
    std::sort(candidates.begin(), candidates.end(), entryLess);

    QMap<QString, const SMapCatalogEntry*> result;
    for(const SMapCatalogEntry* entry : candidates)
    {
        const QString zoneKey = key(entry->zoneId);
        if(result.contains(zoneKey))
        {
            if(diagnostics)
                diagnostics->append(QStringLiteral(
                    "Duplicate zone identity was ignored profile-allod:%1 zone:%2")
                    .arg(allodKey, zoneKey));
            continue;
        }
        result.insert(zoneKey, entry);
    }
    return result;
}

QMap<QString, QVector<SDirectedOffset>> buildGraph(
    const QMap<QString, const SMapCatalogEntry*>& entries)
{
    QMap<QString, SOffsetAccumulator> accumulated;
    const auto addRelation = [&](const SMapCatalogEntry& source,
                                 int sourceExit,
                                 const SMapCatalogEntry& destination,
                                 int destinationExit) {
        if(!layoutable(source) || !layoutable(destination))
            return;
        QPointF delta;
        if(!pairedOffset(source, sourceExit, destination, destinationExit,
                         &delta))
            return;
        const QString sourceKey = key(source.zoneId);
        const QString destinationKey = key(destination.zoneId);
        SOffsetAccumulator& forward = accumulated[
            relationKey(sourceKey, destinationKey)];
        forward.total += delta;
        ++forward.count;
        SOffsetAccumulator& reverse = accumulated[
            relationKey(destinationKey, sourceKey)];
        reverse.total -= delta;
        ++reverse.count;
    };

    for(auto sourceIt = entries.constBegin(); sourceIt != entries.constEnd();
        ++sourceIt)
    {
        const SMapCatalogEntry& source = *sourceIt.value();
        if(!layoutable(source))
            continue;
        for(const SMapTravelZone& sourceRoute : source.travelZones)
        {
            const SMapCatalogEntry* target = entries.value(
                key(sourceRoute.destinationZoneId), nullptr);
            if(!target)
                continue;
            if(layoutable(*target))
            {
                addRelation(source, sourceRoute.exitId, *target,
                            sourceRoute.destinationExitId);
                continue;
            }

            bool reverseConfirmed = false;
            for(const SMapTravelZone& edgeRoute : target->travelZones)
                if(edgeRoute.exitId == sourceRoute.destinationExitId
                   && key(edgeRoute.destinationZoneId) == key(source.zoneId)
                   && edgeRoute.destinationExitId == sourceRoute.exitId)
                {
                    reverseConfirmed = true;
                    break;
                }
            if(!reverseConfirmed)
                continue;
            for(const SMapTravelZone& edgeRoute : target->travelZones)
            {
                if(edgeRoute.exitId == sourceRoute.destinationExitId)
                    continue;
                const SMapCatalogEntry* destination = entries.value(
                    key(edgeRoute.destinationZoneId), nullptr);
                if(destination && layoutable(*destination))
                    addRelation(source, sourceRoute.exitId, *destination,
                                edgeRoute.destinationExitId);
            }
        }
    }

    QMap<QString, QVector<SDirectedOffset>> graph;
    const QChar separator(0x1f);
    for(auto it = accumulated.constBegin(); it != accumulated.constEnd(); ++it)
    {
        if(it.value().count <= 0)
            continue;
        const int split = it.key().indexOf(separator);
        if(split <= 0)
            continue;
        SDirectedOffset edge;
        edge.destination = it.key().mid(split + 1);
        edge.delta = it.value().total / double(it.value().count);
        graph[it.key().left(split)].append(edge);
    }
    for(auto it = graph.begin(); it != graph.end(); ++it)
        std::sort(it.value().begin(), it.value().end(),
                  [](const SDirectedOffset& left,
                     const SDirectedOffset& right) {
            return left.destination < right.destination;
        });
    return graph;
}

QString canonicalAnchorMprIdentity(const QString& allodKey)
{
    if(allodKey == QStringLiteral("gipat"))
        return QStringLiteral("zone1");
    if(allodKey == QStringLiteral("ingos"))
        return QStringLiteral("zone11");
    if(allodKey == QStringLiteral("suslanger"))
        return QStringLiteral("zone18");
    return QString();
}

QString canonicalAnchorKey(
    const QMap<QString, const SMapCatalogEntry*>& entries,
    const QString& allodKey)
{
    const QString expectedMpr = canonicalAnchorMprIdentity(allodKey);
    if(expectedMpr.isEmpty())
        return QString();
    for(auto it = entries.constBegin(); it != entries.constEnd(); ++it)
        if(key(it.value()->mprName) == expectedMpr)
            return it.key();
    return QString();
}
}

SWorldMapInferenceResult CWorldMapLayout::infer(
    const QString& profileId,
    const QVector<SMapCatalogEntry>& topology)
{
    SWorldMapInferenceResult result;
    if(key(profileId).isEmpty())
    {
        result.diagnostics.append(QStringLiteral(
            "World-map inference requires a stable resource profile identity."));
        return result;
    }

    QMap<QString, QString> allods;
    for(const SMapCatalogEntry& entry : topology)
        if(layoutable(entry))
            allods.insert(key(entry.allodId), entry.allodId.trimmed());

    for(auto allodIt = allods.constBegin(); allodIt != allods.constEnd();
        ++allodIt)
    {
        const QMap<QString, const SMapCatalogEntry*> entries = entriesForAllod(
            topology, allodIt.key(), &result.diagnostics);
        const QMap<QString, QVector<SDirectedOffset>> graph = buildGraph(entries);
        QSet<QString> visited;

        // Stock EI allods have a meaningful first map. Starting their main
        // connected component from that map makes the absolute coordinate
        // system explicit instead of depending on lexical zone-id ordering.
        // Disconnected auxiliary maps still receive deterministic lexical
        // anchors of their own.
        QStringList anchorKeys = entries.keys();
        const QString stockAnchorKey = canonicalAnchorKey(entries,
                                                           allodIt.key());
        if(!stockAnchorKey.isEmpty())
        {
            anchorKeys.removeAll(stockAnchorKey);
            anchorKeys.prepend(stockAnchorKey);
        }

        for(const QString& candidateAnchorKey : anchorKeys)
        {
            const auto anchorIt = entries.constFind(candidateAnchorKey);
            if(anchorIt == entries.constEnd())
                continue;
            if(visited.contains(anchorIt.key()) || !layoutable(*anchorIt.value()))
                continue;
            const QString anchorKey = anchorIt.key();
            const QString anchorZoneId = anchorIt.value()->zoneId.trimmed();
            QMap<QString, QPointF> origins;
            QQueue<QString> pending;
            origins.insert(anchorKey, QPointF());
            visited.insert(anchorKey);
            pending.enqueue(anchorKey);
            while(!pending.isEmpty())
            {
                const QString sourceKey = pending.dequeue();
                for(const SDirectedOffset& edge : graph.value(sourceKey))
                {
                    const SMapCatalogEntry* destination = entries.value(
                        edge.destination, nullptr);
                    if(!destination || !layoutable(*destination)
                       || origins.contains(edge.destination))
                        continue;
                    origins.insert(edge.destination,
                                   origins.value(sourceKey) + edge.delta);
                    visited.insert(edge.destination);
                    pending.enqueue(edge.destination);
                }
            }

            for(auto originIt = origins.constBegin();
                originIt != origins.constEnd(); ++originIt)
            {
                const SMapCatalogEntry* entry = entries.value(
                    originIt.key(), nullptr);
                if(!entry)
                    continue;
                SWorldMapZoneOrigin zone;
                zone.profileId = profileId.trimmed();
                zone.allodId = allodIt.value();
                zone.zoneId = entry->zoneId.trimmed();
                zone.mprIdentity = entry->mprName.trimmed();
                zone.origin = originIt.value();
                zone.provenance = EWorldMapOriginProvenance::Inferred;
                zone.componentAnchorZoneId = anchorZoneId;
                result.zones.append(zone);
            }

            for(auto sourceIt = origins.constBegin();
                sourceIt != origins.constEnd(); ++sourceIt)
                for(const SDirectedOffset& edge : graph.value(sourceIt.key()))
                {
                    if(sourceIt.key() >= edge.destination
                       || !origins.contains(edge.destination))
                        continue;
                    const QPointF residual = origins.value(edge.destination)
                        - (sourceIt.value() + edge.delta);
                    if(std::hypot(residual.x(), residual.y()) > 0.01)
                        result.diagnostics.append(QStringLiteral(
                            "Inconsistent deploy loop profile:%1 allod:%2 component:%3 edge:%4-%5 residual:%6,%7")
                            .arg(profileId.trimmed(), allodIt.value(),
                                 anchorZoneId, sourceIt.key(), edge.destination)
                            .arg(residual.x(), 0, 'f', 3)
                            .arg(residual.y(), 0, 'f', 3));
                }
        }
    }

    std::sort(result.zones.begin(), result.zones.end(),
              [](const SWorldMapZoneOrigin& left,
                 const SWorldMapZoneOrigin& right) {
        const QString leftAllod = key(left.allodId);
        const QString rightAllod = key(right.allodId);
        if(leftAllod != rightAllod)
            return leftAllod < rightAllod;
        return key(left.zoneId) < key(right.zoneId);
    });
    return result;
}

const SWorldMapZoneOrigin* CWorldMapLayout::zone(
    const QVector<SWorldMapZoneOrigin>& zones,
    const QString& profileId, const QString& allodId,
    const QString& zoneId)
{
    for(const SWorldMapZoneOrigin& candidate : zones)
        if(key(candidate.profileId) == key(profileId)
           && key(candidate.allodId) == key(allodId)
           && key(candidate.zoneId) == key(zoneId))
            return &candidate;
    return nullptr;
}

bool CWorldMapLayout::originFor(
    const QVector<SWorldMapZoneOrigin>& zones,
    const QString& profileId, const QString& allodId,
    const QString& zoneId, QPointF* origin)
{
    const SWorldMapZoneOrigin* found = zone(
        zones, profileId, allodId, zoneId);
    if(!found || !origin)
        return false;
    *origin = found->origin;
    return true;
}

QPointF CWorldMapLayout::toWorld(const QPointF& mapOrigin,
                                const QPointF& localPosition)
{
    return mapOrigin + localPosition;
}

QPointF CWorldMapLayout::toLocal(const QPointF& mapOrigin,
                                const QPointF& worldPosition)
{
    return worldPosition - mapOrigin;
}

QString worldMapOriginProvenanceName(EWorldMapOriginProvenance provenance)
{
    return provenance == EWorldMapOriginProvenance::Manual
        ? QStringLiteral("manual") : QStringLiteral("inferred");
}

bool parseWorldMapOriginProvenance(
    const QString& name, EWorldMapOriginProvenance* provenance)
{
    if(!provenance)
        return false;
    if(name == QStringLiteral("inferred"))
        *provenance = EWorldMapOriginProvenance::Inferred;
    else if(name == QStringLiteral("manual"))
        *provenance = EWorldMapOriginProvenance::Manual;
    else
        return false;
    return true;
}

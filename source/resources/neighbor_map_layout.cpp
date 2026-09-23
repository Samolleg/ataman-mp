#include "neighbor_map_layout.h"

#include <QRectF>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {
QString key(const QString& value)
{
    return value.trimmed().toCaseFolded();
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

const SMapCatalogEntry* catalogEntry(
    const QVector<SMapCatalogEntry>& topology,
    const QString& allodId, const QString& zoneId)
{
    for(const SMapCatalogEntry& entry : topology)
        if(key(entry.allodId) == key(allodId)
           && key(entry.zoneId) == key(zoneId))
            return &entry;
    return nullptr;
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

bool CNeighborMapLayout::touchesActiveMap(
    const QSizeF& activeSize, const SNeighborMapPlacement& placement)
{
    if(activeSize.width() <= 0 || activeSize.height() <= 0
       || placement.size.width() <= 0 || placement.size.height() <= 0)
        return false;
    // World-map origins can be snapped to quarter units. Accept a small
    // rounding gap, but do not load objects from another part of the world.
    return rectangleDistance(QRectF(QPointF(), activeSize),
                             QRectF(placement.origin, placement.size)) <= 0.5;
}

SNeighborMapLayoutResult CNeighborMapLayout::build(
    const QVector<SMapCatalogEntry>& topology,
    const QVector<SWorldMapZoneOrigin>& worldOrigins,
    const QString& profileId, const QString& activeAllodId,
    const QString& activeZoneId, int maximumNeighbors)
{
    SNeighborMapLayoutResult result;
    if(activeZoneId.trimmed().isEmpty() || activeAllodId.trimmed().isEmpty())
        return result;

    const SMapCatalogEntry* active = catalogEntry(
        topology, activeAllodId, activeZoneId);
    if(!active || !renderable(*active))
    {
        result.messages.append(QStringLiteral(
            "The active map has no renderable world-layout entry."));
        return result;
    }
    const SWorldMapZoneOrigin* activeWorld = CWorldMapLayout::zone(
        worldOrigins, profileId, activeAllodId, activeZoneId);
    if(!activeWorld
       || key(activeWorld->mprIdentity) != key(active->mprName))
    {
        result.messages.append(QStringLiteral(
            "The active map has no matching stored world-map identity."));
        return result;
    }
    result.activeWorldOrigin = activeWorld->origin;
    result.componentAnchorZoneId = activeWorld->componentAnchorZoneId;
    result.hasActiveWorldOrigin = true;
    if(maximumNeighbors == 0)
        return result;

    const QRectF activeBounds(QPointF(), worldSize(*active));
    struct SCandidate
    {
        SNeighborMapPlacement placement;
        double distance{0.0};
    };
    QVector<SCandidate> candidates;
    const auto physicalKey=[](const QString& path) {
        const QFileInfo info(path);
        return (info.exists()?info.canonicalFilePath():QDir::cleanPath(path)).toCaseFolded();
    };
    QSet<QString> physicalMaps{physicalKey(active->mprPath)};
    for(const SWorldMapZoneOrigin& world : worldOrigins)
    {
        if(key(world.profileId) != key(profileId)
           || key(world.allodId) != key(activeAllodId)
           || key(world.zoneId) == key(activeZoneId))
            continue;
        const SMapCatalogEntry* entry = catalogEntry(
            topology, activeAllodId, world.zoneId);
        if(!entry || !renderable(*entry))
            continue;
        if(key(world.mprIdentity) != key(entry->mprName))
        {
            result.messages.append(QStringLiteral(
                "A stored world-map coordinate was ignored because its MPR identity no longer matches its world resource: %1")
                .arg(world.zoneId));
            continue;
        }

        const QString physical = physicalKey(entry->mprPath);
        if(physicalMaps.contains(physical))
        {
            result.messages.append(QStringLiteral("Duplicate MPR reference retained in JSON but not rendered twice: %1").arg(world.zoneId));
            continue;
        }
        physicalMaps.insert(physical);
        SCandidate candidate;
        candidate.placement.zoneId = entry->zoneId;
        candidate.placement.mprPath = entry->mprPath;
        candidate.placement.mobPath = entry->mobPath;
        candidate.placement.worldOrigin = world.origin;
        candidate.placement.origin = world.origin - activeWorld->origin;
        candidate.placement.size = worldSize(*entry);
        candidate.placement.fixedAnchor =
            key(world.zoneId) == key(world.componentAnchorZoneId);
        candidate.distance = rectangleDistance(
            activeBounds, QRectF(candidate.placement.origin,
                                 candidate.placement.size));
        candidates.append(candidate);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const SCandidate& left, const SCandidate& right) {
        if(std::abs(left.distance - right.distance) > 0.001)
            return left.distance < right.distance;
        return left.placement.zoneId.toCaseFolded()
            < right.placement.zoneId.toCaseFolded();
    });
    const int count = maximumNeighbors < 0 ? candidates.size() : qMin(maximumNeighbors, candidates.size());
    for(int index = 0; index < count; ++index)
        result.placements.append(candidates.at(index).placement);
    if(result.placements.isEmpty())
        result.messages.append(QStringLiteral(
            "No neighboring maps share the active map's stored world component."));
    return result;
}

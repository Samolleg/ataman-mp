#include "map_spawn_planner.h"

#include <QtMath>
#include <limits>

namespace {
double obstacleClearance(const QPointF& candidate,
                         const QVector<SMapSpawnObstacle>& obstacles)
{
    double clearance = std::numeric_limits<double>::infinity();
    for(const SMapSpawnObstacle& obstacle : obstacles)
    {
        const double dx = candidate.x() - obstacle.position.x();
        const double dy = candidate.y() - obstacle.position.y();
        clearance = qMin(clearance,
                         qSqrt(dx * dx + dy * dy) - qMax(0.0, obstacle.radius));
    }
    return clearance;
}

double boundaryClearance(const QPointF& candidate, const QRectF& rectangle)
{
    return qMin(qMin(candidate.x() - rectangle.left(),
                     rectangle.right() - candidate.x()),
                qMin(candidate.y() - rectangle.top(),
                     rectangle.bottom() - candidate.y()));
}
}

SMapSpawnPlan CMapSpawnPlanner::choose(const QVector<SMapTravelZone>& travelZones,
                                       const QVector<SMapSpawnObstacle>& obstacles,
                                       double playerRadius,
                                       int gridResolution)
{
    SMapSpawnPlan best;
    best.reason = "No travel zone has enough unobstructed space for the test player.";
    playerRadius = qMax(0.1, playerRadius);
    gridResolution = qBound(3, gridResolution, 31);

    for(const SMapTravelZone& travelZone : travelZones)
    {
        if(!travelZone.hasDeployRectangle || travelZone.deployRectangle.isEmpty())
            continue;
        const QRectF rectangle = travelZone.deployRectangle.normalized();
        if(rectangle.width() < playerRadius * 2.0 ||
           rectangle.height() < playerRadius * 2.0)
            continue;

        QVector<QPointF> candidates;
        candidates.append(rectangle.center());
        for(int y = 0; y < gridResolution; ++y)
        {
            const double fy = double(y) / double(gridResolution - 1);
            const double py = rectangle.top() + playerRadius
                    + fy * (rectangle.height() - playerRadius * 2.0);
            for(int x = 0; x < gridResolution; ++x)
            {
                const double fx = double(x) / double(gridResolution - 1);
                const double px = rectangle.left() + playerRadius
                        + fx * (rectangle.width() - playerRadius * 2.0);
                const QPointF point(px, py);
                if(point != rectangle.center())
                    candidates.append(point);
            }
        }

        for(const QPointF& candidate : candidates)
        {
            const double clearance = qMin(boundaryClearance(candidate, rectangle),
                                           obstacleClearance(candidate, obstacles));
            if(clearance + 0.0001 < playerRadius)
                continue;
            if(!best.valid || clearance > best.clearance + 0.0001)
            {
                best.valid = true;
                best.exitId = travelZone.exitId;
                best.position = candidate;
                best.viewAngle = travelZone.viewAngle;
                best.hasViewAngle = travelZone.hasViewAngle;
                best.clearance = clearance;
                best.reason.clear();
            }
        }
    }
    return best;
}

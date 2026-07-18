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
    SMapSpawnPlan failure;
    failure.reason = "No map-table travel entrance has a deploy rectangle.";
    playerRadius = qMax(0.1, playerRadius);
    gridResolution = qBound(3, gridResolution, 31);
    SMapSpawnPlan bestEffort;

    for(const SMapTravelZone& travelZone : travelZones)
    {
        if(!travelZone.hasDeployRectangle || travelZone.deployRectangle.isEmpty())
            continue;
        const QRectF rectangle = travelZone.deployRectangle.normalized();

        const QPointF handPlacedCenter = rectangle.center();
        const double centerClearance = qMin(boundaryClearance(handPlacedCenter, rectangle),
                                            obstacleClearance(handPlacedCenter, obstacles));
        if(!bestEffort.valid || centerClearance > bestEffort.clearance)
        {
            bestEffort.valid = true;
            bestEffort.exitId = travelZone.exitId;
            bestEffort.position = handPlacedCenter;
            bestEffort.viewAngle = travelZone.viewAngle;
            bestEffort.hasViewAngle = travelZone.hasViewAngle;
            bestEffort.clearance = centerClearance;
            bestEffort.reason = "No fully clear point was found; using the map author's hand-placed entrance and allowing the game to deploy the player.";
        }
        if(rectangle.width() < playerRadius * 2.0 ||
           rectangle.height() < playerRadius * 2.0)
            continue;
        if(centerClearance + 0.0001 >= playerRadius)
        {
            SMapSpawnPlan preferred;
            preferred.valid = true;
            preferred.unobstructed = true;
            preferred.exitId = travelZone.exitId;
            preferred.position = handPlacedCenter;
            preferred.viewAngle = travelZone.viewAngle;
            preferred.hasViewAngle = travelZone.hasViewAngle;
            preferred.clearance = centerClearance;
            preferred.reason = "Using the hand-placed center of the first clear map-table entrance.";
            return preferred;
        }

        SMapSpawnPlan nearby;
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
                const double clearance = qMin(boundaryClearance(point, rectangle),
                                              obstacleClearance(point, obstacles));
                if(clearance + 0.0001 < playerRadius)
                    continue;
                if(!nearby.valid || clearance > nearby.clearance + 0.0001)
                {
                    nearby.valid = true;
                    nearby.unobstructed = true;
                    nearby.exitId = travelZone.exitId;
                    nearby.position = point;
                    nearby.viewAngle = travelZone.viewAngle;
                    nearby.hasViewAngle = travelZone.hasViewAngle;
                    nearby.clearance = clearance;
                    nearby.reason = "The hand-placed entrance center is blocked; using the nearest safe point inside the same entrance.";
                }
            }
        }
        if(nearby.valid)
            return nearby;
    }
    return bestEffort.valid ? bestEffort : failure;
}

#ifndef MAP_SPAWN_PLANNER_H
#define MAP_SPAWN_PLANNER_H

#include <QPointF>
#include <QVector>

#include "map_catalog.h"

struct SMapSpawnObstacle
{
    QPointF position;
    double radius{1.0};
};

struct SMapSpawnPlan
{
    bool valid{false};
    bool unobstructed{false};
    int exitId{0};
    QPointF position;
    double viewAngle{0.0};
    bool hasViewAngle{false};
    double clearance{0.0};
    QString reason;
};

class CMapSpawnPlanner
{
public:
    static SMapSpawnPlan choose(const QVector<SMapTravelZone>& travelZones,
                                const QVector<SMapSpawnObstacle>& obstacles,
                                double playerRadius = 1.0,
                                int gridResolution = 7);
};

#endif // MAP_SPAWN_PLANNER_H

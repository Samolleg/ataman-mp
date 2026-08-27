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
    QPointF requestedPosition;
    bool custom{false};
    bool adjusted{false};
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
    static SMapSpawnPlan chooseEntrance(const SMapTravelZone& travelZone,
                                        const QVector<SMapSpawnObstacle>& obstacles,
                                        double playerRadius = 1.0,
                                        int gridResolution = 7);
    static SMapSpawnPlan chooseCustom(const QPointF& requested,
                                      int bootstrapExitId,
                                      const QVector<SMapSpawnObstacle>& obstacles,
                                      double playerRadius = 1.0,
                                      double searchRadius = 8.0,
                                      int radialSteps = 8);
    static bool canPreserveCustomSpawnAcrossDocumentReplacement(
        const SMapSpawnPlan& spawn,
        const QString& previousZoneId,
        const QString& currentZoneId,
        bool terrainPointValid);
};

#endif // MAP_SPAWN_PLANNER_H

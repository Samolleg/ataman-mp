#include <QtTest>

#include "map_spawn_planner.h"

class MapSpawnPlannerTests : public QObject
{
    Q_OBJECT

private slots:
    void avoidsBlockedCenter();
    void rejectsFullyBlockedTravelZones();
    void fallsBackToAnotherTravelZone();
};

static SMapTravelZone zone(int id, const QRectF& rectangle)
{
    SMapTravelZone value;
    value.exitId = id;
    value.deployRectangle = rectangle;
    value.hasDeployRectangle = true;
    return value;
}

void MapSpawnPlannerTests::avoidsBlockedCenter()
{
    const SMapTravelZone travel = zone(2, QRectF(0.0, 0.0, 10.0, 10.0));
    SMapSpawnObstacle obstacle;
    obstacle.position = QPointF(5.0, 5.0);
    obstacle.radius = 2.0;

    const SMapSpawnPlan plan = CMapSpawnPlanner::choose({travel}, {obstacle});
    QVERIFY2(plan.valid, qPrintable(plan.reason));
    QCOMPARE(plan.exitId, 2);
    QVERIFY(plan.position != travel.spawnPoint());
    const double dx = plan.position.x() - obstacle.position.x();
    const double dy = plan.position.y() - obstacle.position.y();
    QVERIFY(qSqrt(dx * dx + dy * dy) - obstacle.radius >= 1.0);
}

void MapSpawnPlannerTests::rejectsFullyBlockedTravelZones()
{
    const SMapTravelZone travel = zone(1, QRectF(0.0, 0.0, 3.0, 3.0));
    SMapSpawnObstacle obstacle;
    obstacle.position = QPointF(1.5, 1.5);
    obstacle.radius = 10.0;
    const SMapSpawnPlan plan = CMapSpawnPlanner::choose({travel}, {obstacle});
    QVERIFY(!plan.valid);
    QVERIFY(!plan.reason.isEmpty());
}

void MapSpawnPlannerTests::fallsBackToAnotherTravelZone()
{
    const SMapTravelZone blocked = zone(1, QRectF(0.0, 0.0, 4.0, 4.0));
    const SMapTravelZone free = zone(4, QRectF(20.0, 20.0, 8.0, 8.0));
    SMapSpawnObstacle obstacle;
    obstacle.position = blocked.spawnPoint();
    obstacle.radius = 20.0;
    const SMapSpawnPlan plan = CMapSpawnPlanner::choose({blocked, free}, {obstacle});
    QVERIFY(plan.valid);
    QCOMPARE(plan.exitId, 4);
}

QTEST_APPLESS_MAIN(MapSpawnPlannerTests)
#include "map_spawn_planner_tests.moc"

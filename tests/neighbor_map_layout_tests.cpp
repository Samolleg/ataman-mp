#include <QtTest>

#include "map_catalog.h"
#include "neighbor_map_layout.h"
#include "world_map_layout.h"

class NeighborMapLayoutTests : public QObject
{
    Q_OBJECT

private slots:
    void placesNearestZone7BackgroundsFromPairedEntrances();
    void rejectsNonOppositeDeployEdges();
    void worldOriginsDoNotDependOnActiveZone();
    void doesNotCombineDifferentAllods();
};

void NeighborMapLayoutTests::placesNearestZone7BackgroundsFromPairedEntrances()
{
    const QString table = QString::fromUtf8(
        "#zone gz1g gipat game\n#res\nzone1 zone1\n#maps\n11 9\na b\n"
        "#exit 4\ngz1g_gz3g 1\n#deploy\n340 195 350 209\n"
        "#zone gz1g_gz3g gipat edge\n#exit 1\ngz1g 4\n#exit 2\ngz3g 1\n"
        "#zone gz3g gipat game\n#res\nzone3obr zone3obr\n#maps\n11 10\na b\n"
        "#exit 1\ngz1g_gz3g 2\n#deploy\n20 130 26 147\n"
        "#exit 3\ngz3g_gz6g 1\n#deploy\n328 14 336 26\n"
        "#exit 4\ngz3g_gz7g 1\n#deploy\n219 2 229 15\n"
        "#zone gz3g_gz6g gipat edge\n#exit 1\ngz3g 3\n#exit 2\ngz6g 1\n"
        "#zone gz3g_gz7g gipat edge\n#exit 1\ngz3g 4\n#exit 2\ngz7g 1\n"
        "#zone gz6g gipat game\n#res\nzone6 zone6\n#maps\n8 12\na b\n"
        "#exit 1\ngz3g_gz6g 2\n#deploy\n10 25 20 45\n"
        "#exit 2\ngz6g_gz7g 1\n#deploy\n40 10 53 15\n"
        "#zone gz6g_gz7g gipat edge\n#exit 1\ngz6g 2\n#exit 2\ngz7g 2\n"
        "#zone gz7g gipat game\n#res\nzone7 zone7\n#maps\n16 10\na b\n"
        "#exit 1\ngz3g_gz7g 2\n#deploy\n152 300 161 305\n"
        "#exit 2\ngz6g_gz7g 2\n#deploy\n295 300 305 305\n"
        "#exit 3\ngz7g_gz9g 1\n#deploy\n499 235 510 246\n"
        "#exit 4\ngz7g_gz9g2 1\n#deploy\n499 173 510 178\n"
        "#exit 5\ngz7g_gz8g 1\n#deploy\n502 83 510 91\n"
        "#zone gz7g_gz9g gipat edge\n#exit 1\ngz7g 3\n#exit 2\ngz9g 2\n"
        "#zone gz7g_gz9g2 gipat edge\n#exit 1\ngz7g 4\n#exit 2\ngz9g 3\n"
        "#zone gz7g_gz8g gipat edge\n#exit 1\ngz7g 5\n#exit 2\ngz8g 1\n"
        "#zone gz8g gipat game\n#res\nzone8 zone8\n#maps\n11 11\na b\n"
        "#exit 1\ngz7g_gz8g 2\n#deploy\n9 232 15 238\n"
        "#zone gz9g gipat game\n#res\nzone9 zone9\n#maps\n14 10\na b\n"
        "#exit 2\ngz7g_gz9g 2\n#deploy\n10 148 15 155\n"
        "#exit 3\ngz7g_gz9g2 2\n#deploy\n10 90 15 100\n");

    QVector<SMapCatalogEntry> topology = CMapCatalog::parseMapTopology(table);
    for(SMapCatalogEntry& entry : topology)
        if(!entry.mprName.isEmpty())
        {
            entry.mprPath = entry.mprName + QStringLiteral(".mpr");
            entry.mobPath = entry.mobName + QStringLiteral(".mob");
        }

    const SWorldMapInferenceResult world = CWorldMapLayout::infer(
        QStringLiteral("test-profile"), topology);
    // The persisted model keeps the complete six-map component. The limit of
    // five applies only when selecting background landscapes for this view.
    QCOMPARE(world.zones.size(), 6);
    const SNeighborMapLayoutResult result = CNeighborMapLayout::build(
        topology, world.zones, QStringLiteral("test-profile"),
        QStringLiteral("gipat"), QStringLiteral("gz7g"), 5);

    QCOMPARE(result.placements.size(), 5);
    QMap<QString, QPointF> origins;
    QMap<QString, QString> mobPaths;
    for(const SNeighborMapPlacement& placement : result.placements)
    {
        origins.insert(placement.zoneId, placement.origin);
        mobPaths.insert(placement.zoneId, placement.mobPath);
    }
    QCOMPARE(origins.keys().toSet(),
             QSet<QString>({QStringLiteral("gz1g"), QStringLiteral("gz3g"),
                            QStringLiteral("gz6g"), QStringLiteral("gz8g"),
                            QStringLiteral("gz9g")}));
    QVERIFY(origins.value(QStringLiteral("gz1g")).x() < 0.0);
    QVERIFY(origins.value(QStringLiteral("gz3g")).y() >= 320.0);
    QVERIFY(origins.value(QStringLiteral("gz6g")).y() >= 320.0);
    QVERIFY(origins.value(QStringLiteral("gz8g")).x() >= 512.0);
    QVERIFY(origins.value(QStringLiteral("gz9g")).x() >= 512.0);
    QCOMPARE(mobPaths.value(QStringLiteral("gz3g")),
             QStringLiteral("zone3obr.mob"));
}

void NeighborMapLayoutTests::rejectsNonOppositeDeployEdges()
{
    const QString table = QString::fromUtf8(
        "#zone a allod game\n#res\na a\n#maps\n4 4\na b\n"
        "#exit 1\nedge 1\n#deploy\n0 32 4 40\n"
        "#zone edge allod edge\n#exit 1\na 1\n#exit 2\nb 1\n"
        "#zone b allod game\n#res\nb b\n#maps\n4 4\na b\n"
        "#exit 1\nedge 2\n#deploy\n0 64 4 72\n");
    QVector<SMapCatalogEntry> topology = CMapCatalog::parseMapTopology(table);
    for(SMapCatalogEntry& entry : topology)
        if(!entry.mprName.isEmpty())
            entry.mprPath = entry.mprName + QStringLiteral(".mpr");

    const SWorldMapInferenceResult world = CWorldMapLayout::infer(
        QStringLiteral("test-profile"), topology);
    const SNeighborMapLayoutResult result = CNeighborMapLayout::build(
        topology, world.zones, QStringLiteral("test-profile"),
        QStringLiteral("allod"), QStringLiteral("a"), 5);

    QVERIFY(result.placements.isEmpty());
    QVERIFY(!result.messages.isEmpty());
}

void NeighborMapLayoutTests::worldOriginsDoNotDependOnActiveZone()
{
    const QString table = QString::fromUtf8(
        "#zone a island game\n#res\na a\n#maps\n4 4\na b\n"
        "#exit 1\nb 1\n#deploy\n124 48 128 64\n"
        "#zone b island game\n#res\nb b\n#maps\n4 4\na b\n"
        "#exit 1\na 1\n#deploy\n0 48 4 64\n"
        "#exit 2\nc 1\n#deploy\n124 48 128 64\n"
        "#zone c island game\n#res\nc c\n#maps\n4 4\na b\n"
        "#exit 1\nb 2\n#deploy\n0 48 4 64\n");
    QVector<SMapCatalogEntry> topology = CMapCatalog::parseMapTopology(table);
    for(SMapCatalogEntry& entry : topology)
        if(!entry.mprName.isEmpty())
            entry.mprPath = entry.mprName + QStringLiteral(".mpr");

    const SWorldMapInferenceResult world = CWorldMapLayout::infer(
        QStringLiteral("test-profile"), topology);
    const SNeighborMapLayoutResult fromA = CNeighborMapLayout::build(
        topology, world.zones, QStringLiteral("test-profile"),
        QStringLiteral("island"), QStringLiteral("a"), 5);
    const SNeighborMapLayoutResult fromC = CNeighborMapLayout::build(
        topology, world.zones, QStringLiteral("test-profile"),
        QStringLiteral("island"), QStringLiteral("c"), 5);
    QPointF bFromA;
    QPointF bFromC;
    bool anchorFixedFromC = false;
    for(const SNeighborMapPlacement& placement : fromA.placements)
        if(placement.zoneId == QStringLiteral("b"))
            bFromA = placement.worldOrigin;
    for(const SNeighborMapPlacement& placement : fromC.placements)
    {
        if(placement.zoneId == QStringLiteral("b"))
            bFromC = placement.worldOrigin;
        if(placement.zoneId == QStringLiteral("a"))
            anchorFixedFromC = placement.fixedAnchor;
    }

    // A persisted world layout must assign B one absolute origin regardless
    // of which map happens to be active. Expected: B remains at (128, 0).
    QCOMPARE(bFromA, QPointF(128.0, 0.0));
    QCOMPARE(bFromC, QPointF(128.0, 0.0));
    QCOMPARE(fromC.componentAnchorZoneId, QStringLiteral("a"));
    QVERIFY(anchorFixedFromC);
}

void NeighborMapLayoutTests::doesNotCombineDifferentAllods()
{
    const QString table = QString::fromUtf8(
        "#zone a first game\n#res\na a\n#maps\n4 4\na b\n"
        "#exit 1\nb 1\n#deploy\n124 48 128 64\n"
        "#zone b second game\n#res\nb b\n#maps\n4 4\na b\n"
        "#exit 1\na 1\n#deploy\n0 48 4 64\n");
    QVector<SMapCatalogEntry> topology = CMapCatalog::parseMapTopology(table);
    for(SMapCatalogEntry& entry : topology)
        if(!entry.mprName.isEmpty())
            entry.mprPath = entry.mprName + QStringLiteral(".mpr");

    const SWorldMapInferenceResult world = CWorldMapLayout::infer(
        QStringLiteral("test-profile"), topology);
    const SNeighborMapLayoutResult result = CNeighborMapLayout::build(
        topology, world.zones, QStringLiteral("test-profile"),
        QStringLiteral("first"), QStringLiteral("a"), 5);

    // Cross-allod travel remains navigation metadata only and must never be a
    // geometric relation in the world layout.
    QVERIFY(result.placements.isEmpty());
}

QTEST_APPLESS_MAIN(NeighborMapLayoutTests)
#include "neighbor_map_layout_tests.moc"

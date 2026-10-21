#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>

#include "map_catalog.h"

class MapCatalogTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesResourcePairsOnly();
    void acceptsWhitespaceAndCase();
    void parsesTravelZoneSpawnData();
    void parsesSkyEnvironment();
    void parsesCompleteMapsIdentityAndRepeatedRoutes();
    void topologyRetainsTravelOnlyZones();
    void identifiesBriefingZonesFromCatalogMetadata();
    void loadsIndexedMapWithCaseAndRootPriority();
};

void MapCatalogTests::loadsIndexedMapWithCaseAndRootPriority()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());
    const auto first=dir.filePath("first"),second=dir.filePath("second");
    QVERIFY(QDir().mkpath(first));QVERIFY(QDir().mkpath(second));
    const QByteArray map="#zone gz1g gipat game\n#res\nzone1 zone1\n";
    QByteArray bytes(16,'\0');bytes+=map;
    const auto offset=quint32(bytes.size());
    QDataStream table(&bytes,QIODevice::Append);table.setByteOrder(QDataStream::LittleEndian);
    const QByteArray name="MAP.TXT";
    table<<quint32(-1)<<quint32(map.size())<<quint32(16)<<quint32(0)<<quint16(name.size())<<quint32(0);
    bytes+=name;QDataStream header(&bytes,QIODevice::WriteOnly);header.setByteOrder(QDataStream::LittleEndian);
    header<<quint32(0x019CE23C)<<quint32(1)<<offset<<quint32(name.size());
    QFile archive(dir.filePath("TeXtS.ReS"));QVERIFY(archive.open(QIODevice::WriteOnly));archive.write(bytes);archive.close();
    for(const auto& root:{first,second})for(const auto& filename:{"ZONE1.MPR","Zone1.Mob"}) {
        QFile file(QDir(root).filePath(filename));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");
    }
    const auto result=CMapCatalog::load({dir.path()},{first,second});
    QCOMPARE(result.entries.size(),1);QVERIFY(result.entries.first().isComplete());
    QCOMPARE(result.entries.first().mprPath,QDir(first).filePath("ZONE1.MPR"));
    QCOMPARE(result.entries.first().mobPath,QDir(first).filePath("Zone1.Mob"));
    QVERIFY(QFile::remove(QDir(first).filePath("ZONE1.MPR")));
    QCOMPARE(CMapCatalog::load({dir.path()},{first,second}).entries.first().mprPath,QDir(second).filePath("ZONE1.MPR"));
}

void MapCatalogTests::parsesResourcePairsOnly()
{
    const QString table = QString::fromUtf8(
        "#zone zone1 gz1g gipat game\r\n"
        "#res\r\n"
        "zone1 zone1\r\n"
        "#maps\r\n"
        "#zone briefing brief service\r\n"
        "#text\r\n"
        "ignored\r\n"
        "#zone zone2 gz2g gipat game\r\n"
        "#res\r\n"
        "zone2_land zone2_units\r\n");

    const QVector<SMapCatalogEntry> entries = CMapCatalog::parseMapTable(table, "texts.res");
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).zoneId, QString("zone1"));
    QCOMPARE(entries.at(0).mprName, QString("zone1"));
    QCOMPARE(entries.at(0).mobName, QString("zone1"));
    QCOMPARE(entries.at(0).allodId, QString("gz1g"));
    QCOMPARE(entries.at(0).sourceArchive, QString("texts.res"));
    QCOMPARE(entries.at(1).mprName, QString("zone2_land"));
    QCOMPARE(entries.at(1).mobName, QString("zone2_units"));
}

void MapCatalogTests::acceptsWhitespaceAndCase()
{
    const QVector<SMapCatalogEntry> entries = CMapCatalog::parseMapTable(
        "  #ZONE   zone18   gz18   jmv  \n #RES \n zone18   zone18  \n");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().zoneType, QString("jmv"));
}

void MapCatalogTests::parsesTravelZoneSpawnData()
{
    const QVector<SMapCatalogEntry> entries = CMapCatalog::parseMapTable(
        "#zone gz1g gipat game\n"
        "#res\nzone1 zone1\n"
        "#exit 2\nbz1g 1\n"
        "#deploy\n184 101 190 108\n"
        "#remove\n181 108 193 120\n"
        "#view\n135.2\n"
        "#exit 3\nnone 0\n");

    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().travelZones.size(), 2);
    const SMapTravelZone& spawn = entries.first().travelZones.first();
    QCOMPARE(spawn.exitId, 1);
    QCOMPARE(spawn.destinationZoneId, QString("bz1g"));
    QCOMPARE(spawn.destinationExitId, 1);
    QVERIFY(spawn.hasDeployRectangle);
    QCOMPARE(spawn.spawnPoint(), QPointF(187.0, 104.5));
    QVERIFY(spawn.hasViewAngle);
    QCOMPARE(spawn.viewAngle, 135.2);
    QVERIFY(!entries.first().travelZones.at(1).hasDeployRectangle);
}

void MapCatalogTests::parsesSkyEnvironment()
{
    const QVector<SMapCatalogEntry> entries = CMapCatalog::parseMapTable(
        "#zone gz5g gipat game\n"
        "#res\nzone5_1 zone5_1\n"
        "#sky\ncave\n"
        "#zone gz1g gipat game\n"
        "#res\nzone1 zone1\n"
        "#sky\nnormal\n");

    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).skyType, QString("cave"));
    QCOMPARE(entries.at(1).skyType, QString("normal"));
}

void MapCatalogTests::parsesCompleteMapsIdentityAndRepeatedRoutes()
{
    const QVector<SMapCatalogEntry> entries = CMapCatalog::parseMapTable(
        "#zone gz1g gipat game\n#res\nzone1 zone1\n"
        "#maps\n11 9\nzone1map zone1quest\n"
        "#exit 1\nbz1g 2\n#deploy\n1 2 3 4\n"
        "#exit 1\nbz1g 2\n#deploy\n5 6 7 8\n");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().sectorWidth, 11);
    QCOMPARE(entries.first().sectorHeight, 9);
    QCOMPARE(entries.first().minimapTexture, QString("zone1map"));
    QCOMPARE(entries.first().objectivesTexture, QString("zone1quest"));
    QCOMPARE(entries.first().travelZones.size(), 2);
    QCOMPARE(entries.first().travelZones.at(0).spawnPoint(), QPointF(2, 3));
    QCOMPARE(entries.first().travelZones.at(1).spawnPoint(), QPointF(6, 7));
    QCOMPARE(mapTravelRouteKey(entries.first().travelZones, 0),
             QString("1|bz1g|2|0"));
    QCOMPARE(mapTravelRouteKey(entries.first().travelZones, 1),
             QString("2|bz1g|2|0"));
}

void MapCatalogTests::topologyRetainsTravelOnlyZones()
{
    const QString table = QString::fromUtf8(
        "#zone gz7g gipat game\n#res\nzone7 zone7\n#maps\n16 10\na b\n"
        "#exit 1\ngz3g_gz7g 2\n#deploy\n152 300 161 305\n"
        "#zone gz3g_gz7g gipat edge\n"
        "#exit 1\ngz3g 4\n#exit 2\ngz7g 1\n");

    const QVector<SMapCatalogEntry> renderable =
        CMapCatalog::parseMapTable(table);
    const QVector<SMapCatalogEntry> topology =
        CMapCatalog::parseMapTopology(table);

    QCOMPARE(renderable.size(), 1);
    QCOMPARE(topology.size(), 2);
    QCOMPARE(topology.at(1).zoneId, QStringLiteral("gz3g_gz7g"));
    QVERIFY(topology.at(1).mprName.isEmpty());
    QCOMPARE(topology.at(1).travelZones.size(), 2);
}

void MapCatalogTests::identifiesBriefingZonesFromCatalogMetadata()
{
    SMapCatalogEntry briefing;
    briefing.zoneType = QStringLiteral("BrIeF");
    QVERIFY(briefing.isBriefing());

    SMapCatalogEntry gameplay;
    gameplay.zoneType = QStringLiteral("game");
    QVERIFY(!gameplay.isBriefing());
}

QTEST_APPLESS_MAIN(MapCatalogTests)
#include "map_catalog_tests.moc"

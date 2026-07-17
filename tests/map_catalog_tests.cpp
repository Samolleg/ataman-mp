#include <QtTest>

#include "map_catalog.h"

class MapCatalogTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesResourcePairsOnly();
    void acceptsWhitespaceAndCase();
};

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

QTEST_APPLESS_MAIN(MapCatalogTests)
#include "map_catalog_tests.moc"

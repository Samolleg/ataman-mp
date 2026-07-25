#include <QtTest>
#include "map_table_change_report.h"

class MapTableChangeReportTests : public QObject
{
    Q_OBJECT
private slots:
    void separatesChanges();
    void entranceOnlyDoesNotReload();
    void reportsRegistration();
};

void MapTableChangeReportTests::separatesChanges()
{
    const QByteArray before = "#zone gz1g gipat game\r\n#res\r\nzone1 zone1\r\n#maps\r\n11 9\r\nzone1map zone1quest\r\n";
    const QByteArray after = "#zone gz1g gipat game\r\n#res\r\nzone1b zone1\r\n#maps\r\n11 9\r\nzone1new zone1quest\r\n";
    const auto result = analyzeMapTableChange(before, after);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.affectedZoneIds(), QStringList({"gz1g"}));
    QVERIFY(result.categories.contains("resources"));
    QVERIFY(result.categories.contains("minimap/objectives"));
    QVERIFY(result.changesPhysicalResources("GZ1G"));
}

void MapTableChangeReportTests::entranceOnlyDoesNotReload()
{
    const QByteArray before = "#zone gz1g gipat game\n#res\nzone1 zone1\n#exit 1\nbz1g 1\n#deploy\n1 2 3 4\n";
    QByteArray after = before; after.replace("1 2 3 4", "5 6 7 8");
    const auto result = analyzeMapTableChange(before, after);
    QCOMPARE(result.categories, QStringList({"entrances"}));
    QVERIFY(!result.changesPhysicalResources("gz1g"));
}

void MapTableChangeReportTests::reportsRegistration()
{
    const QByteArray before = "#zone one gipat game\n#res\none one\n";
    const QByteArray after = before + "#zone two gipat game\n#res\ntwo two\n";
    const auto result = analyzeMapTableChange(before, after);
    QVERIFY(result.affectedZoneIds().contains("two"));
    QVERIFY(result.changesPhysicalResources("two"));
}

QTEST_APPLESS_MAIN(MapTableChangeReportTests)
#include "map_table_change_report_tests.moc"

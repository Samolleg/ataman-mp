#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "hot_reload_protocol.h"

class HotReloadProtocolTests : public QObject
{
    Q_OBJECT
private slots:
    void acceptsOwnedVersionedPayload();
    void rejectsLiveOrEscapingPaths();
    void roundTripsBridgeControlFile();
};

void HotReloadProtocolTests::acceptsOwnedVersionedPayload()
{
    QTemporaryDir run;
    const QString mpr = run.filePath("reloads/1/map.mpr");
    const QString mob = run.filePath("reloads/1/map.mob");
    QVERIFY(QDir().mkpath(QFileInfo(mpr).absolutePath()));
    for(const QString& path : {mpr, mob}) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); }
    SMapHotReloadRequest request;
    request.sequence = 1;
    request.runId = "run";
    request.profileId = "jamais-vu";
    request.zoneId = "gz1g";
    request.entranceId = 1;
    request.mprPath = mpr;
    request.mobPath = mob;
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    QStringList errors;
    QVERIFY2(validateMapHotReloadRequest(request, run.path(), &errors), qPrintable(errors.join('\n')));
    SMapHotReloadRequest parsed;
    QVERIFY(mapHotReloadRequestFromJson(mapHotReloadRequestToJson(request), &parsed, &errors));
    QCOMPARE(parsed.sequence, request.sequence);
}

void HotReloadProtocolTests::rejectsLiveOrEscapingPaths()
{
    QTemporaryDir run;
    SMapHotReloadRequest request;
    request.sequence = 1;
    request.runId = "run";
    request.profileId = "jamais-vu";
    request.zoneId = "gz1g";
    request.entranceId = 1;
    request.mprPath = "D:/Evil Islands/Maps/zone1.mpr";
    request.mobPath = "D:/Evil Islands/Maps/zone1.mob";
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    QStringList errors;
    QVERIFY(!validateMapHotReloadRequest(request, run.path(), &errors));
    QVERIFY(!errors.isEmpty());
}

void HotReloadProtocolTests::roundTripsBridgeControlFile()
{
    SMapHotReloadRequest request;
    request.sequence = 19;
    request.runId = "run-19";
    request.profileId = "jamais-vu";
    request.zoneId = "gz10g";
    request.entranceId = 4;
    request.mprPath = "C:/test runs/run-19/reloads/19/map.mpr";
    request.mobPath = "C:/test runs/run-19/reloads/19/map.mob";
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    request.bootstrapMobPath = "C:/test runs/run-19/reloads/19/AutoRunSpellAddon123.mob";
    request.bootstrapMobSha256 = QString(64, 'c');
    request.preservePosition = true;

    SMapHotReloadRequest parsed;
    QStringList errors;
    QVERIFY(mapHotReloadRequestFromControlBytes(
        mapHotReloadRequestToControlBytes(request), &parsed, &errors));
    QCOMPARE(parsed.sequence, request.sequence);
    QCOMPARE(parsed.runId, request.runId);
    QCOMPARE(parsed.zoneId, request.zoneId);
    QCOMPARE(parsed.entranceId, request.entranceId);
    QCOMPARE(parsed.mprPath, request.mprPath);
    QCOMPARE(parsed.mobPath, request.mobPath);
    QCOMPARE(parsed.bootstrapMobPath, request.bootstrapMobPath);
    QCOMPARE(parsed.preservePosition, request.preservePosition);
}

QTEST_APPLESS_MAIN(HotReloadProtocolTests)
#include "hot_reload_protocol_tests.moc"

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "hot_reload_protocol.h"

class HotReloadProtocolTests : public QObject
{
    Q_OBJECT
private slots:
    void acceptsOwnedVersionedPayload();
    void acceptsOwnedGeneratedMinimapResource();
    void rejectsLiveOrEscapingPaths();
    void roundTripsBridgeControlFile();
};

void HotReloadProtocolTests::acceptsOwnedVersionedPayload()
{
    QTemporaryDir run;
    const QString mpr = run.filePath("reloads/1/map.mpr");
    const QString mob = run.filePath("reloads/1/map.mob");
    const QString saves = run.filePath("generations/000001/saves");
    const QString database = run.filePath("generations/000001/database.res");
    QVERIFY(QDir().mkpath(QFileInfo(mpr).absolutePath()));
    QVERIFY(QDir().mkpath(saves));
    for(const QString& path : {mpr, mob, database}) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); }
    SMapHotReloadRequest request;
    request.sequence = 1;
    request.generation = 1;
    request.runId = "run";
    request.profileId = "jamais-vu";
    request.sourceZoneId = "gz1g";
    request.bootstrapMprName = "zone1";
    request.bootstrapMobName = "zone1";
    request.zoneId = "gz1g";
    request.entranceId = 1;
    request.mprPath = mpr;
    request.mobPath = mob;
    request.savePath = saves;
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    request.databasePath = database;
    request.databaseSha256 = QString(64, 'd');
    QStringList errors;
    QVERIFY2(validateMapHotReloadRequest(request, run.path(), &errors), qPrintable(errors.join('\n')));
    SMapHotReloadRequest parsed;
    QVERIFY(mapHotReloadRequestFromJson(mapHotReloadRequestToJson(request), &parsed, &errors));
    QCOMPARE(parsed.sequence, request.sequence);
}

void HotReloadProtocolTests::acceptsOwnedGeneratedMinimapResource()
{
    QTemporaryDir run;
    const QString root = run.filePath("generations/000002");
    const QString mpr = QDir(root).filePath("map.mpr");
    const QString mob = QDir(root).filePath("map.mob");
    const QString minimap = QDir(root).filePath("minimap.res");
    const QString saves = QDir(root).filePath("saves");
    const QString database = QDir(root).filePath("database.res");
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(saves));
    for(const QString& path : {mpr, mob, minimap, database})
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("fixture") == 7);
    }
    SMapHotReloadRequest request;
    request.sequence = 2;
    request.generation = 2;
    request.runId = "run";
    request.profileId = "jamais-vu";
    request.sourceZoneId = "gz1g";
    request.bootstrapMprName = "zone1";
    request.bootstrapMobName = "zone1";
    request.zoneId = "gz1g";
    request.entranceId = 1;
    request.mprPath = mpr;
    request.mobPath = mob;
    request.savePath = saves;
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    request.minimapTextureName = "eimmap_0123456789ab";
    request.minimapResourcePath = minimap;
    request.minimapResourceSha256 = QString(64, 'c');
    request.databasePath = database;
    request.databaseSha256 = QString(64, 'd');
    QStringList errors;
    QVERIFY2(validateMapHotReloadRequest(request, run.path(), &errors),
             qPrintable(errors.join('\n')));
    request.minimapTextureName = "unsafe/name";
    errors.clear();
    QVERIFY(!validateMapHotReloadRequest(request, run.path(), &errors));
}

void HotReloadProtocolTests::rejectsLiveOrEscapingPaths()
{
    QTemporaryDir run;
    SMapHotReloadRequest request;
    request.sequence = 1;
    request.generation = 1;
    request.runId = "run";
    request.profileId = "jamais-vu";
    request.zoneId = "gz1g";
    request.entranceId = 1;
    request.mprPath = "D:/Evil Islands/Maps/zone1.mpr";
    request.mobPath = "D:/Evil Islands/Maps/zone1.mob";
    request.savePath = "D:/Evil Islands/saves";
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    request.databasePath = "D:/Evil Islands/Res/database.res";
    request.databaseSha256 = QString(64, 'd');
    QStringList errors;
    QVERIFY(!validateMapHotReloadRequest(request, run.path(), &errors));
    QVERIFY(!errors.isEmpty());
}

void HotReloadProtocolTests::roundTripsBridgeControlFile()
{
    SMapHotReloadRequest request;
    request.sequence = 19;
    request.generation = 7;
    request.runtimeStateSequence = 12;
    request.runId = "run-19";
    request.profileId = "jamais-vu";
    request.sourceZoneId = "gz10g";
    request.bootstrapMprName = "zone10";
    request.bootstrapMobName = "zone10";
    request.zoneId = "gz10g";
    request.entranceId = 4;
    request.mprPath = "C:/test runs/run-19/reloads/19/map.mpr";
    request.mobPath = "C:/test runs/run-19/reloads/19/map.mob";
    request.mprSha256 = QString(64, 'a');
    request.mobSha256 = QString(64, 'b');
    request.bootstrapMobPath = "C:/test runs/run-19/reloads/19/AutoRunSpellAddon123.mob";
    request.bootstrapMobSha256 = QString(64, 'c');
    request.minimapTextureName = "eimmap_0123456789ab";
    request.minimapResourcePath = "C:/test runs/run-19/reloads/19/minimap.res";
    request.minimapResourceSha256 = QString(64, 'd');
    request.databasePath = "C:/test runs/run-19/reloads/19/database.res";
    request.databaseSha256 = QString(64, 'e');
    request.savePath = "C:/test runs/run-19/generations/000007/saves";
    request.preservePosition = true;
    request.graphless = true;
    request.syntheticMapIdentity = true;
    request.mapSizeX = 7;
    request.mapSizeY = 5;
    request.spawnX = 180.25;
    request.spawnY = 96.5;

    SMapHotReloadRequest parsed;
    QStringList errors;
    QVERIFY(mapHotReloadRequestFromControlBytes(
        mapHotReloadRequestToControlBytes(request), &parsed, &errors));
    QCOMPARE(parsed.sequence, request.sequence);
    QCOMPARE(parsed.generation, request.generation);
    QCOMPARE(parsed.runtimeStateSequence, request.runtimeStateSequence);
    QCOMPARE(parsed.runId, request.runId);
    QCOMPARE(parsed.zoneId, request.zoneId);
    QCOMPARE(parsed.bootstrapMprName, request.bootstrapMprName);
    QCOMPARE(parsed.bootstrapMobName, request.bootstrapMobName);
    QCOMPARE(parsed.entranceId, request.entranceId);
    QCOMPARE(parsed.mprPath, request.mprPath);
    QCOMPARE(parsed.mobPath, request.mobPath);
    QCOMPARE(parsed.bootstrapMobPath, request.bootstrapMobPath);
    QCOMPARE(parsed.minimapTextureName, request.minimapTextureName);
    QCOMPARE(parsed.minimapResourcePath, request.minimapResourcePath);
    QCOMPARE(parsed.minimapResourceSha256, request.minimapResourceSha256);
    QCOMPARE(parsed.databasePath, request.databasePath);
    QCOMPARE(parsed.databaseSha256, request.databaseSha256);
    QCOMPARE(parsed.savePath, request.savePath);
    QCOMPARE(parsed.preservePosition, request.preservePosition);
    QCOMPARE(parsed.graphless, request.graphless);
    QCOMPARE(parsed.syntheticMapIdentity, request.syntheticMapIdentity);
    QCOMPARE(parsed.mapSizeX, request.mapSizeX);
    QCOMPARE(parsed.mapSizeY, request.mapSizeY);
    QCOMPARE(parsed.spawnX, request.spawnX);
    QCOMPARE(parsed.spawnY, request.spawnY);
}

QTEST_APPLESS_MAIN(HotReloadProtocolTests)
#include "hot_reload_protocol_tests.moc"

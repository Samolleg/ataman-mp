#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "map_catalog.h"
#include "world_map_layout.h"
#include "world_map_layout_document.h"

namespace {
QString twoZoneTable(const QString& allodId = QStringLiteral("island"),
                     int sourceWidth = 4)
{
    const int sourceRight = sourceWidth * 32;
    return QString::fromLatin1(
        "#zone b %1 game\n#res\nb b\n#maps\n4 4\na b\n"
        "#exit 1\na 1\n#deploy\n0 48 4 64\n"
        "#zone a %1 game\n#res\na a\n#maps\n%2 4\na b\n"
        "#exit 1\nb 1\n#deploy\n%3 48 %4 64\n")
        .arg(allodId).arg(sourceWidth).arg(sourceRight - 4).arg(sourceRight);
}

QString threeZoneTable()
{
    return QString::fromLatin1(
        "#zone c island game\n#res\nc c\n#maps\n4 4\na b\n"
        "#exit 1\nb 2\n#deploy\n0 48 4 64\n"
        "#zone a island game\n#res\na a\n#maps\n4 4\na b\n"
        "#exit 1\nb 1\n#deploy\n124 48 128 64\n"
        "#zone b island game\n#res\nb b\n#maps\n4 4\na b\n"
        "#exit 1\na 1\n#deploy\n0 48 4 64\n"
        "#exit 2\nc 1\n#deploy\n124 48 128 64\n");
}

QString canonicalStockAllodAnchorTable()
{
    return QString::fromLatin1(
        "#zone gz10g gipat game\n#res\nzone10 zone10\n#maps\n4 4\na b\n"
        "#exit 1\ngz1g 1\n#deploy\n0 48 4 64\n"
        "#zone gz1g gipat game\n#res\nzone1 zone1\n#maps\n4 4\na b\n"
        "#exit 1\ngz10g 1\n#deploy\n124 48 128 64\n"
        "#zone gz12k ingos game\n#res\nzone12 zone12\n#maps\n4 4\na b\n"
        "#exit 1\ngz11k 1\n#deploy\n0 48 4 64\n"
        "#zone gz11k ingos game\n#res\nzone11 zone11\n#maps\n4 4\na b\n"
        "#exit 1\ngz12k 1\n#deploy\n124 48 128 64\n"
        "#zone gz19h suslanger game\n#res\nzone19 zone19\n#maps\n4 4\na b\n"
        "#exit 1\ngz18h 1\n#deploy\n0 48 4 64\n"
        "#zone gz18h suslanger game\n#res\nzone18 zone18\n#maps\n4 4\na b\n"
        "#exit 1\ngz19h 1\n#deploy\n124 48 128 64\n");
}

QVector<SMapCatalogEntry> topologyFrom(const QString& table)
{
    QVector<SMapCatalogEntry> topology = CMapCatalog::parseMapTopology(
        table, QStringLiteral("C:/private/game/texts.res"));
    for(SMapCatalogEntry& entry : topology)
    {
        if(entry.mprName.isEmpty())
            continue;
        entry.mprPath = QStringLiteral("C:/private/game/maps/")
            + entry.mprName + QStringLiteral(".mpr");
        entry.mobPath = QStringLiteral("C:/private/game/maps/")
            + entry.mobName + QStringLiteral(".mob");
    }
    return topology;
}

QPointF requiredOrigin(const QVector<SWorldMapZoneOrigin>& zones,
                       const QString& profileId, const QString& allodId,
                       const QString& zoneId)
{
    QPointF origin;
    const bool found = CWorldMapLayout::originFor(
        zones, profileId, allodId, zoneId, &origin);
    Q_ASSERT(found);
    return origin;
}

const SWorldMapZoneOrigin* requiredZone(
    const QVector<SWorldMapZoneOrigin>& zones, const QString& profileId,
    const QString& allodId, const QString& zoneId)
{
    const SWorldMapZoneOrigin* zone = CWorldMapLayout::zone(
        zones, profileId, allodId, zoneId);
    Q_ASSERT(zone);
    return zone;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QByteArray();
    return file.readAll();
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}
}

class WorldMapLayoutDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void legacyWorldUsesOrderedRoots()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path=dir.filePath("world-map-layouts.json");
        const QByteArray legacy=R"({"schemaVersion":1,"layoutAlgorithmVersion":1,"coordinateSystem":"mpr-local-xy","profiles":[{"profileId":"p","allods":[{"allodId":"island","zones":[{"zoneId":"old","mprIdentity":"terrain","origin":{"x":31,"y":42},"provenance":"manual","componentAnchorZoneId":"old"}]}]}]})";
        QVERIFY(writeFile(path,legacy));
        for(const QString& root : {QStringLiteral("Maps"),QStringLiteral("Base"),QStringLiteral("Maps/backup")}) {
            QVERIFY(QDir().mkpath(dir.filePath(root)));
            QVERIFY(writeFile(dir.filePath(root+"/terrain.mpr"),"fixture"));
            QVERIFY(writeFile(dir.filePath(root+"/terrain.mob"),"fixture"));
        }
        CWorldMapLayoutDocument doc(path);
        auto result=doc.loadEditorWorld("p",{dir.filePath("Maps"),dir.filePath("Base")});
        QCOMPARE(result.zones.size(),1);
        QCOMPARE(result.zones[0].mprPath,QStringLiteral("Maps/terrain.mpr"));
        QCOMPARE(result.zones[0].mobPath,QStringLiteral("Maps/terrain.mob"));
        QCOMPARE(result.zones[0].origin,QPointF(31,42));
        QVERIFY(result.diagnostics.isEmpty());
        QCOMPARE(readFile(path),legacy); // Resolution alone never migrates user data.
        doc.reload();
        result=doc.loadEditorWorld("p",{dir.filePath("Base"),dir.filePath("Maps")});
        QCOMPARE(result.zones[0].mprPath,QStringLiteral("Base/terrain.mpr"));
        QCOMPARE(result.zones[0].mobPath,QStringLiteral("Base/terrain.mob"));
        QVERIFY(QFile::remove(dir.filePath("Base/terrain.mpr")));
        doc.reload();
        result=doc.loadEditorWorld("p",{dir.filePath("Base"),dir.filePath("Maps")});
        QCOMPARE(result.zones[0].mprPath,QStringLiteral("Maps/terrain.mpr"));
        QCOMPARE(readFile(path),legacy);
    }
    void installedWorldReadOnlyCheck()
    {
        const QString path=qEnvironmentVariable("EI_WORLD_LAYOUT_CHECK");
        if(path.isEmpty()) QSKIP("Optional installed profile read-only check");
        const QByteArray before=readFile(path);
        CWorldMapLayoutDocument doc(path);
        const auto roots=qEnvironmentVariable("EI_WORLD_ROOTS_CHECK").split(';',QString::SkipEmptyParts);
        const auto result=doc.loadEditorWorld("jamais-vu",roots);
        QVERIFY(!result.storageBlocked); QVERIFY(!result.zones.isEmpty());
        int found=0, missing=0;
        for(const auto& zone : result.zones) {
            QString expected;
            for(const auto& root : roots) {
                for(const auto& file : QDir(root).entryInfoList(QDir::Files))
                    if(file.fileName().compare(zone.mprIdentity+".mpr",Qt::CaseInsensitive)==0)
                        { expected=file.canonicalFilePath(); break; }
                if(!expected.isEmpty()) break;
            }
            if(expected.isEmpty()) {
                ++missing; QVERIFY(zone.mprPath.isEmpty());
                qInfo().noquote() << "Absent on disk:" << zone.mprIdentity;
                continue;
            }
            ++found;
            QCOMPARE(QFileInfo(doc.absoluteResourcePath(zone.mprPath)).canonicalFilePath(),expected);
            qInfo().noquote() << zone.mprIdentity << zone.mprPath << zone.mobPath;
        }
        QVERIFY(found>1);
        QCOMPARE(result.diagnostics.size(),missing);
        qInfo() << "Resolved:" << found << "Actually absent:" << missing;
        QCOMPARE(readFile(path),before);
    }

    void editorWorldIsJsonOnlyAndDetectsConcurrentChanges()
    {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString path=dir.filePath("world-map-layouts.json");
        const auto makeFile=[&](const QString& name){QFile file(dir.filePath(name));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");};
        makeFile("a.mpr");makeFile("b.mpr");
        CWorldMapLayoutDocument doc(path);
        QVERIFY(doc.loadEditorWorld("profile",{}).zones.isEmpty());
        QVERIFY(!QFileInfo::exists(path)); // No map.txt inference or automatic membership.
        SWorldMapZoneOrigin a;a.profileId="profile";a.allodId="world";a.zoneId="a";
        a.componentAnchorZoneId="a";a.mprIdentity="a";a.mprPath="a.mpr";a.sectorWidth=4;a.sectorHeight=3;
        auto b=a;b.zoneId="b";b.mprIdentity="b";b.mprPath="b.mpr";b.origin=QPointF(128,0);
        QString error;QVERIFY2(doc.addEditorMaps({a,b},&error),qPrintable(error));
        CWorldMapLayoutDocument reopened(path);
        const auto loaded=reopened.loadEditorWorld("profile",{});
        QCOMPARE(loaded.zones.size(),2);QCOMPARE(loaded.zones[1].origin,b.origin);
        QVERIFY(reopened.loadEditorWorld("other-profile",{}).zones.isEmpty());
        auto duplicate=b;duplicate.zoneId="duplicate";
        QVERIFY(!doc.addEditorMaps({duplicate},&error));
        auto edited=b;edited.displayName="New label";edited.origin=QPointF(200,5);
        QVERIFY(doc.replaceEditorMap(edited,&error));
        auto invalid=edited;invalid.mprPath="missing.mpr";
        QVERIFY(!doc.replaceEditorMap(invalid,&error));
        QVERIFY(doc.setZoneOrigin("profile","world","b",QPointF(300,20),&error));
        QVERIFY(!reopened.setZoneOrigin("profile","world","b",QPointF(999,999),&error));
        reopened.reload();QCOMPARE(reopened.loadEditorWorld("profile",{}).zones[1].origin,QPointF(300,20));
        QVERIFY(reopened.removeEditorMap("profile","world","a",&error));
        CWorldMapLayoutDocument afterRemoval(path);const auto retained=afterRemoval.loadEditorWorld("profile",{});
        QCOMPARE(retained.zones.size(),1);QCOMPARE(retained.zones[0].componentAnchorZoneId,QStringLiteral("b"));
        QCOMPARE(retained.zones[0].origin,QPointF(300,20));
        QVERIFY(QFileInfo::exists(dir.filePath("a.mpr")));QVERIFY(QFileInfo::exists(dir.filePath("b.mpr")));
        QVERIFY(QFile::remove(dir.filePath("b.mpr")));
        afterRemoval.reload();QCOMPARE(afterRemoval.loadEditorWorld("profile",{}).zones.size(),1);
    }
    void legacyEditorWorldResolvesFilesWithoutMapTableAndBacksUp()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QString path=dir.filePath("world-map-layouts.json");
        const QByteArray legacy=R"({"schemaVersion":1,"layoutAlgorithmVersion":1,"coordinateSystem":"mpr-local-xy","profiles":[{"profileId":"p","allods":[{"allodId":"island","zones":[{"zoneId":"old","mprIdentity":"terrain","origin":{"x":31,"y":42},"provenance":"manual","componentAnchorZoneId":"old"}]}]}]})";
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(legacy);file.close();
        QFile terrain(dir.filePath("terrain.mpr"));QVERIFY(terrain.open(QIODevice::WriteOnly));terrain.write("fixture");terrain.close();
        CWorldMapLayoutDocument doc(path);const auto result=doc.loadEditorWorld("p",{dir.path()});
        QCOMPARE(result.zones.size(),1);QCOMPARE(result.zones[0].origin,QPointF(31,42));
        QCOMPARE(result.zones[0].mprPath,QStringLiteral("terrain.mpr"));
        QVERIFY(result.zones[0].mobPath.isEmpty());
        QString error;QVERIFY2(doc.setZoneOrigin("p","island","old",QPointF(50,60),&error),qPrintable(error));
        QFile backup(path+".before-v2.bak");QVERIFY(backup.open(QIODevice::ReadOnly));QCOMPARE(backup.readAll(),legacy);
        CWorldMapLayoutDocument reopen(path);QCOMPARE(reopen.loadEditorWorld("p",{}).zones[0].origin,QPointF(50,60));
    }
    void seedsDeterministicWorldOriginsFromMapTable();
    void separatesProfilesAndAllods();
    void persistsAndReloads();
    void preservesExistingCoordinatesWhenCatalogChanges();
    void addsNewZoneWithoutMovingSavedZones();
    void corruptAndUnsupportedDocumentsRemainUntouched();
    void serializedDocumentContainsNoAbsolutePaths();
    void manualResetApisDoNotRequireUiEditing();
    void usesCanonicalStockAllodAnchors();
    void worldTransformRoundTripPreservesVisiblePoint();
    void profileDirectoryMigrationAndSwitching();
    void existingProfileFileWinsOverLegacy();
    void profileMigrationFailsWithoutOverwritingData();
};

void WorldMapLayoutDocumentTests::profileDirectoryMigrationAndSwitching()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString legacyPath = dir.filePath("legacy.json");
    const auto topology = topologyFrom(twoZoneTable());
    CWorldMapLayoutDocument legacy(legacyPath);
    QVERIFY(legacy.loadAndUpdate("jamais-vu", topology).persisted);
    QVERIFY(legacy.loadAndUpdate("deja-vu", topology).persisted);
    QVERIFY(legacy.setZoneOrigin("jamais-vu", "island", "a", QPointF(123, 456)));
    QVERIFY(legacy.setZoneOrigin("deja-vu", "island", "a", QPointF(-50, 9)));
    const QByteArray original = readFile(legacyPath);
    CWorldMapLayoutDocument document;
    const QString jmvRoot = dir.filePath("MODS/JamaisVu135");
    document.setProfileDirectory(jmvRoot, "jamais-vu", legacyPath);
    QCOMPARE(document.path(), jmvRoot + "/world-map-layouts.json");
    auto loaded = document.loadAndUpdate("jamais-vu", topology);
    QVERIFY(loaded.persisted);
    QCOMPARE(requiredOrigin(loaded.zones, "jamais-vu", "island", "a"), QPointF(123, 456));
    QVERIFY(!readFile(document.path()).contains("deja-vu"));
    QVERIFY(document.setZoneOrigin("jamais-vu", "island", "a", QPointF(777, 88)));
    const QByteArray jmvSaved = readFile(document.path());
    document.setProfileDirectory(dir.filePath("MODS/DejaVu"), "deja-vu", legacyPath);
    loaded = document.loadAndUpdate("deja-vu", topology);
    QVERIFY(loaded.persisted);
    QCOMPARE(requiredOrigin(loaded.zones, "deja-vu", "island", "a"), QPointF(-50, 9));
    QVERIFY(!readFile(document.path()).contains("jamais-vu"));
    document.setProfileDirectory(jmvRoot, "jamais-vu", legacyPath);
    loaded = document.loadAndUpdate("jamais-vu", topology);
    QCOMPARE(requiredOrigin(loaded.zones, "jamais-vu", "island", "a"), QPointF(777, 88));
    QCOMPARE(readFile(document.path()), jmvSaved);
    document.setProfileDirectory(dir.filePath("BaseGame"), "base", legacyPath);
    QVERIFY(document.loadAndUpdate("base", topology).persisted);
    QCOMPARE(document.path(), dir.filePath("BaseGame/world-map-layouts.json"));
    QCOMPARE(readFile(legacyPath), original);
}

void WorldMapLayoutDocumentTests::existingProfileFileWinsOverLegacy()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto topology = topologyFrom(twoZoneTable());
    const QString path = dir.filePath("world-map-layouts.json");
    CWorldMapLayoutDocument existing(path);
    QVERIFY(existing.loadAndUpdate("jamais-vu", topology).persisted);
    QVERIFY(existing.loadAndUpdate("deja-vu", topology).persisted);
    QVERIFY(existing.setZoneOrigin("jamais-vu", "island", "a", QPointF(99, 55)));
    const QByteArray original = readFile(path);
    const QString badLegacy = dir.filePath("bad-legacy.json");
    QVERIFY(writeFile(badLegacy, "corrupt"));
    CWorldMapLayoutDocument document;
    document.setProfileDirectory(dir.path(), "jamais-vu", badLegacy);
    const auto loaded = document.loadAndUpdate("jamais-vu", topology);
    QVERIFY(loaded.persisted);
    QCOMPARE(requiredOrigin(loaded.zones, "jamais-vu", "island", "a"), QPointF(99, 55));
    QCOMPARE(readFile(path), original); // Including the other profile's data.
    QVERIFY(document.setZoneOrigin("jamais-vu", "island", "a", QPointF(100, 56)));
    QVERIFY(readFile(path).contains("deja-vu"));
}

void WorldMapLayoutDocumentTests::profileMigrationFailsWithoutOverwritingData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto topology = topologyFrom(twoZoneTable());
    const QString legacyPath = dir.filePath("legacy.json");
    QVERIFY(writeFile(legacyPath, "broken legacy"));
    CWorldMapLayoutDocument document;
    document.setProfileDirectory(dir.filePath("new"), "jamais-vu", legacyPath);
    QVERIFY(document.loadAndUpdate("jamais-vu", topology).storageBlocked);
    QVERIFY(!QFileInfo::exists(document.path()));
    QCOMPARE(readFile(legacyPath), QByteArray("broken legacy"));
    const QString destination = dir.filePath("world-map-layouts.json");
    QVERIFY(writeFile(destination, "broken destination"));
    document.setProfileDirectory(dir.path(), "jamais-vu", legacyPath);
    QVERIFY(document.loadAndUpdate("jamais-vu", topology).storageBlocked);
    QVERIFY(!document.setZoneOrigin("jamais-vu", "island", "a", QPointF(0, 0)));
    QCOMPARE(readFile(destination), QByteArray("broken destination"));
    document.setProfileDirectory(QString(), "jamais-vu", legacyPath);
    QVERIFY(document.loadAndUpdate("jamais-vu", topology).storageBlocked);
    QVERIFY(document.path().isEmpty());
    document.setProfileDirectory(dir.filePath("race"), "jamais-vu", QString());
    document.loadAndUpdate("jamais-vu", {});
    QVERIFY(QDir().mkpath(dir.filePath("race")));
    QVERIFY(writeFile(document.path(), "new external destination"));
    const auto raced = document.loadAndUpdate("jamais-vu", topology);
    QVERIFY(!raced.persisted);
    QVERIFY(!raced.warning.isEmpty());
    QCOMPARE(readFile(document.path()), QByteArray("new external destination"));
}

void WorldMapLayoutDocumentTests::seedsDeterministicWorldOriginsFromMapTable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    CWorldMapLayoutDocument document(path);

    const SWorldMapLayoutUpdateResult result = document.loadAndUpdate(
        QStringLiteral("profile-one"), topologyFrom(twoZoneTable()));

    QVERIFY2(result.persisted, qPrintable(result.warning));
    QVERIFY(!result.storageBlocked);
    QCOMPARE(requiredOrigin(result.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("a")),
             QPointF(0.0, 0.0));
    QCOMPARE(requiredOrigin(result.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(128.0, 0.0));
    QCOMPARE(requiredZone(result.zones, QStringLiteral("profile-one"),
                          QStringLiteral("island"), QStringLiteral("b"))
                 ->componentAnchorZoneId,
             QStringLiteral("a"));
}

void WorldMapLayoutDocumentTests::separatesProfilesAndAllods()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    QVector<SMapCatalogEntry> topology = topologyFrom(twoZoneTable());
    topology += topologyFrom(twoZoneTable(QStringLiteral("other-island"), 5));

    CWorldMapLayoutDocument document(path);
    const SWorldMapLayoutUpdateResult first = document.loadAndUpdate(
        QStringLiteral("profile-one"), topology);
    const SWorldMapLayoutUpdateResult second = document.loadAndUpdate(
        QStringLiteral("profile-two"), topologyFrom(twoZoneTable(
            QStringLiteral("island"), 5)));

    QVERIFY2(first.persisted, qPrintable(first.warning));
    QVERIFY2(second.persisted, qPrintable(second.warning));
    QCOMPARE(requiredOrigin(first.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(128.0, 0.0));
    QCOMPARE(requiredOrigin(first.zones, QStringLiteral("profile-one"),
                            QStringLiteral("other-island"), QStringLiteral("b")),
             QPointF(160.0, 0.0));
    QCOMPARE(requiredOrigin(second.zones, QStringLiteral("profile-two"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(160.0, 0.0));
}

void WorldMapLayoutDocumentTests::persistsAndReloads()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    const QVector<SMapCatalogEntry> topology = topologyFrom(twoZoneTable());
    CWorldMapLayoutDocument first(path);
    const SWorldMapLayoutUpdateResult saved = first.loadAndUpdate(
        QStringLiteral("profile-one"), topology);
    QVERIFY2(saved.persisted, qPrintable(saved.warning));
    QVERIFY(QFileInfo::exists(path));

    CWorldMapLayoutDocument reopened(path);
    const SWorldMapLayoutUpdateResult loaded = reopened.loadAndUpdate(
        QStringLiteral("profile-one"), topology);

    QVERIFY2(loaded.persisted, qPrintable(loaded.warning));
    QCOMPARE(requiredOrigin(loaded.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("a")),
             QPointF(0.0, 0.0));
    QCOMPARE(requiredOrigin(loaded.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(128.0, 0.0));
}

void WorldMapLayoutDocumentTests::preservesExistingCoordinatesWhenCatalogChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    CWorldMapLayoutDocument first(path);
    QVERIFY(first.loadAndUpdate(QStringLiteral("profile-one"),
                                topologyFrom(twoZoneTable())).persisted);

    CWorldMapLayoutDocument reopened(path);
    const SWorldMapLayoutUpdateResult changed = reopened.loadAndUpdate(
        QStringLiteral("profile-one"), topologyFrom(twoZoneTable(
            QStringLiteral("island"), 5)));

    QVERIFY2(changed.persisted, qPrintable(changed.warning));
    QCOMPARE(requiredOrigin(changed.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("a")),
             QPointF(0.0, 0.0));
    QCOMPARE(requiredOrigin(changed.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(128.0, 0.0));
}

void WorldMapLayoutDocumentTests::addsNewZoneWithoutMovingSavedZones()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    CWorldMapLayoutDocument first(path);
    QVERIFY(first.loadAndUpdate(QStringLiteral("profile-one"),
                                topologyFrom(twoZoneTable())).persisted);

    CWorldMapLayoutDocument reopened(path);
    const SWorldMapLayoutUpdateResult expanded = reopened.loadAndUpdate(
        QStringLiteral("profile-one"), topologyFrom(threeZoneTable()));

    QVERIFY2(expanded.persisted, qPrintable(expanded.warning));
    QCOMPARE(requiredOrigin(expanded.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("a")),
             QPointF(0.0, 0.0));
    QCOMPARE(requiredOrigin(expanded.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(128.0, 0.0));
    QCOMPARE(requiredOrigin(expanded.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("c")),
             QPointF(256.0, 0.0));
}

void WorldMapLayoutDocumentTests::corruptAndUnsupportedDocumentsRemainUntouched()
{
    const QList<QByteArray> invalidDocuments = {
        QByteArray("{not-json"),
        QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 2}})
            .toJson(QJsonDocument::Compact),
        QByteArrayLiteral(
            "{\"schemaVersion\":1.5,\"layoutAlgorithmVersion\":1,"
            "\"coordinateSystem\":\"mpr-local-xy\",\"profiles\":[]}"),
        QByteArrayLiteral(
            "{\"schemaVersion\":1,\"layoutAlgorithmVersion\":1,"
            "\"coordinateSystem\":\"mpr-local-xy\",\"profiles\":["
            "{\"profileId\":\"profile-one\",\"allods\":[]},"
            "{\"profileId\":\"PROFILE-ONE\",\"allods\":[]}]}"),
        QByteArrayLiteral(
            "{\"schemaVersion\":1,\"layoutAlgorithmVersion\":1,"
            "\"coordinateSystem\":\"mpr-local-xy\",\"profiles\":[{"
            "\"profileId\":\"profile-one\",\"allods\":[{"
            "\"allodId\":\"island\",\"zones\":[{\"zoneId\":\"a\","
            "\"mprIdentity\":\"a\",\"origin\":{\"x\":0,\"y\":0},"
            "\"provenance\":\"inferred\","
            "\"componentAnchorZoneId\":\"missing\"}]}]}]}")
    };
    for(const QByteArray& invalid : invalidDocuments)
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(
            QStringLiteral("world-layouts.json"));
        QVERIFY(writeFile(path, invalid));
        CWorldMapLayoutDocument document(path);

        const SWorldMapLayoutUpdateResult result = document.loadAndUpdate(
            QStringLiteral("profile-one"), topologyFrom(twoZoneTable()));

        QVERIFY(result.storageBlocked);
        QVERIFY(!result.persisted);
        QVERIFY(!result.warning.isEmpty());
        QCOMPARE(readFile(path), invalid);
        QCOMPARE(requiredOrigin(result.zones, QStringLiteral("profile-one"),
                                QStringLiteral("island"), QStringLiteral("b")),
                 QPointF(128.0, 0.0));
    }
}

void WorldMapLayoutDocumentTests::serializedDocumentContainsNoAbsolutePaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    CWorldMapLayoutDocument document(path);
    const SWorldMapLayoutUpdateResult result = document.loadAndUpdate(
        QStringLiteral("profile-one"), topologyFrom(twoZoneTable()));
    QVERIFY2(result.persisted, qPrintable(result.warning));

    const QByteArray bytes = readFile(path);
    QVERIFY(!bytes.isEmpty());
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes);
    QVERIFY(parsed.isObject());
    const QJsonObject root = parsed.object();
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 2);
    QCOMPARE(root.value(QStringLiteral("layoutAlgorithmVersion")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("coordinateSystem")).toString(),
             QStringLiteral("mpr-local-xy"));
    QCOMPARE(root.value(QStringLiteral("profiles")).toArray().size(), 1);
    QVERIFY(!bytes.contains("C:/private"));
    QVERIFY(!bytes.contains("C:\\\\private"));
    QVERIFY(!bytes.contains("texts.res"));
    QVERIFY(!bytes.contains(".mpr"));
    QVERIFY(!bytes.contains(".mob"));
    QVERIFY(!bytes.contains("sourcePath"));
    QVERIFY(!bytes.contains("archivePath"));
    QVERIFY(bytes.contains("mprIdentity"));
    QVERIFY(bytes.contains("profile-one"));
}

void WorldMapLayoutDocumentTests::manualResetApisDoNotRequireUiEditing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("world-layouts.json"));
    const QVector<SMapCatalogEntry> topology = topologyFrom(twoZoneTable());
    CWorldMapLayoutDocument document(path);
    QVERIFY(document.loadAndUpdate(QStringLiteral("profile-one"), topology)
                .persisted);
    QString error;
    QVERIFY2(document.setZoneOrigin(
                 QStringLiteral("profile-one"), QStringLiteral("island"),
                 QStringLiteral("b"), QPointF(900.0, 25.0), &error),
             qPrintable(error));

    CWorldMapLayoutDocument reopened(path);
    SWorldMapLayoutUpdateResult changed = reopened.loadAndUpdate(
        QStringLiteral("profile-one"), topology);
    QCOMPARE(requiredOrigin(changed.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(900.0, 25.0));
    QCOMPARE(requiredZone(changed.zones, QStringLiteral("profile-one"),
                          QStringLiteral("island"), QStringLiteral("b"))
                 ->provenance,
             EWorldMapOriginProvenance::Manual);

    QVERIFY2(reopened.resetZoneToInferred(
                 QStringLiteral("profile-one"), QStringLiteral("island"),
                 QStringLiteral("b"), topology, &error), qPrintable(error));
    CWorldMapLayoutDocument resetReload(path);
    changed = resetReload.loadAndUpdate(QStringLiteral("profile-one"), topology);
    QCOMPARE(requiredOrigin(changed.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("b")),
             QPointF(128.0, 0.0));
    QCOMPARE(requiredZone(changed.zones, QStringLiteral("profile-one"),
                          QStringLiteral("island"), QStringLiteral("b"))
                 ->provenance,
             EWorldMapOriginProvenance::Inferred);

    QVERIFY2(resetReload.setZoneOrigin(
                 QStringLiteral("profile-one"), QStringLiteral("island"),
                 QStringLiteral("a"), QPointF(50.0, 50.0), &error),
             qPrintable(error));
    QVERIFY2(resetReload.resetAllodLayout(
                 QStringLiteral("profile-one"), QStringLiteral("island"),
                 topology, &error), qPrintable(error));
    CWorldMapLayoutDocument allodReload(path);
    changed = allodReload.loadAndUpdate(QStringLiteral("profile-one"), topology);
    QCOMPARE(requiredOrigin(changed.zones, QStringLiteral("profile-one"),
                            QStringLiteral("island"), QStringLiteral("a")),
             QPointF(0.0, 0.0));
}

void WorldMapLayoutDocumentTests::usesCanonicalStockAllodAnchors()
{
    const SWorldMapInferenceResult inferred = CWorldMapLayout::infer(
        QStringLiteral("jamais-vu"),
        topologyFrom(canonicalStockAllodAnchorTable()));

    const struct {
        const char* allodId;
        const char* zoneId;
    } expected[] = {
        {"gipat", "gz1g"},
        {"ingos", "gz11k"},
        {"suslanger", "gz18h"}
    };
    for(const auto& anchor : expected)
    {
        const SWorldMapZoneOrigin* zone = requiredZone(
            inferred.zones, QStringLiteral("jamais-vu"),
            QString::fromLatin1(anchor.allodId),
            QString::fromLatin1(anchor.zoneId));
        QCOMPARE(zone->origin, QPointF(0.0, 0.0));
        QCOMPARE(zone->componentAnchorZoneId,
                 QString::fromLatin1(anchor.zoneId));
    }
}

void WorldMapLayoutDocumentTests::worldTransformRoundTripPreservesVisiblePoint()
{
    const QPointF originA(1200.5, -300.25);
    const QPointF originB(1456.5, -172.25);
    const QPointF localA(90.25, 64.75);

    const QPointF world = CWorldMapLayout::toWorld(originA, localA);
    const QPointF localB = CWorldMapLayout::toLocal(originB, world);
    const QPointF backWorld = CWorldMapLayout::toWorld(originB, localB);
    const QPointF backOnA = CWorldMapLayout::toLocal(originA, backWorld);

    QCOMPARE(world, QPointF(1290.75, -235.5));
    QCOMPARE(localB, QPointF(-165.75, -63.25));
    QCOMPARE(backOnA, localA);
}

QTEST_APPLESS_MAIN(WorldMapLayoutDocumentTests)
#include "world_map_layout_document_tests.moc"

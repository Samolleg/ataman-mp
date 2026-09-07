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
};

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
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);
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

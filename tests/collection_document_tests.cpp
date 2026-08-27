#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "collection_document.h"

class CollectionDocumentTests : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsOrderedMobs();
    void resolvesMapRootRelativeReferences();
    void resolvesPackagedMprWithDefaultZoneMob();
    void reportsAllMissingReferences();
    void comparesEditableCollectionContent();
};

void CollectionDocumentTests::roundTripsOrderedMobs()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("zone.collection");
    SEnduranceCollection source;
    source.name = "Zone test";
    source.profileId = "jamais-vu";
    source.mapZoneId = "gz1g";
    source.mprReference = "zone1.mpr";
    source.mobReferences << "zone1.mob" << "Autozone1wolves.mob";
    QString error;
    QVERIFY2(CCollectionDocument::save(path, source, &error), qPrintable(error));
    SEnduranceCollection loaded;
    QVERIFY2(CCollectionDocument::load(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.name, source.name);
    QCOMPARE(loaded.mapZoneId, source.mapZoneId);
    QCOMPARE(loaded.mobReferences, source.mobReferences);
}

void CollectionDocumentTests::resolvesMapRootRelativeReferences()
{
    QTemporaryDir directory;
    QDir root(directory.path());
    QVERIFY(root.mkpath("Maps"));
    for(const QString& name : {QString("Zone1.MPR"), QString("zone1.mob")})
    {
        QFile file(root.filePath("Maps/" + name));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    SEnduranceCollection source;
    source.sourcePath = root.filePath("collections/test.collection");
    source.mprReference = "zone1.mpr";
    source.mobReferences << "ZONE1.MOB";
    const SResolvedCollection resolved = CCollectionDocument::resolve(
        source, QVector<QString>() << root.filePath("Maps"));
    QVERIFY(resolved.isValid());
    QCOMPARE(QFileInfo(resolved.mprPath).fileName().toLower(), QString("zone1.mpr"));
}

void CollectionDocumentTests::resolvesPackagedMprWithDefaultZoneMob()
{
    QTemporaryDir directory;
    QDir root(directory.path());
    QVERIFY(root.mkpath("package/collections"));
    QVERIFY(root.mkpath("package/runtime-atlas-probes/page-256"));
    QVERIFY(root.mkpath("jmv/Maps"));
    const QString mprPath = root.filePath(
        "package/runtime-atlas-probes/page-256/zone18-expanded.mpr");
    const QString mobPath = root.filePath("jmv/Maps/zone18.mob");
    for(const QString& path : {mprPath, mobPath})
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    SEnduranceCollection source;
    source.sourcePath = root.filePath(
        "package/collections/zone18-expanded-16384-slots.collection");
    source.profileId = "jamais-vu";
    source.mprReference =
        "../runtime-atlas-probes/page-256/zone18-expanded.mpr";
    source.mobReferences << "zone18.mob";
    const SResolvedCollection resolved = CCollectionDocument::resolve(
        source, QVector<QString>() << root.filePath("jmv/Maps"));
    QVERIFY2(resolved.isValid(), qPrintable(resolved.errors.join('\n')));
    QCOMPARE(QFileInfo(resolved.mprPath).absoluteFilePath(),
             QFileInfo(mprPath).absoluteFilePath());
    QCOMPARE(resolved.mobPaths.size(), 1);
    QCOMPARE(QFileInfo(resolved.mobPaths.front()).absoluteFilePath(),
             QFileInfo(mobPath).absoluteFilePath());
}

void CollectionDocumentTests::reportsAllMissingReferences()
{
    SEnduranceCollection source;
    source.sourcePath = "C:/none/test.collection";
    source.mprReference = "missing.mpr";
    source.mobReferences << "first.mob" << "second.mob";
    const SResolvedCollection resolved = CCollectionDocument::resolve(source, {});
    QCOMPARE(resolved.errors.size(), 3);
}

void CollectionDocumentTests::comparesEditableCollectionContent()
{
    SEnduranceCollection left;
    left.name = "Particle control";
    left.profileId = "jamais-vu";
    left.mprReference = "../test-scenes/control.mpr";
    left.mobReferences << "../test-scenes/control.mob";
    left.sourcePath = "C:/first/collection.collection";
    SEnduranceCollection right = left;
    right.sourcePath = "D:/copied/collection.collection";
    right.mprReference = "..\\test-scenes\\control.mpr";
    QVERIFY(CCollectionDocument::equivalent(left, right));

    right.mapZoneId = "gz1g";
    QVERIFY(!CCollectionDocument::equivalent(left, right));
    right = left;
    right.mobReferences.append("../test-scenes/extra.mob");
    QVERIFY(!CCollectionDocument::equivalent(left, right));
}

QTEST_APPLESS_MAIN(CollectionDocumentTests)
#include "collection_document_tests.moc"

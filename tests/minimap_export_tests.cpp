#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "minimap_bake.h"
#include "minimap_catalog_registration.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"
#include "res_archive_document.h"
#include "res_texture_export.h"

namespace {
QImage solid(const QSize& size, const QColor& color)
{ QImage image(size, QImage::Format_ARGB32); image.fill(color); return image; }

}

class MinimapExportTests : public QObject
{
    Q_OBJECT
private slots:
    void bakes128PixelsPerSectorAndExcludesDynamic();
    void encodesInspectable256Mmp();
    void createsAddsReplacesAndRefusesCollision();
    void validationFailureDoesNotPublish();
    void registersCatalogOnlyAfterExportAndPreservesObjectives();
};

void MinimapExportTests::bakes128PixelsPerSectorAndExcludesDynamic()
{
    const QSize size(256, 128);
    QVector<SMinimapBakeLayer> layers{
        {EMinimapBakeLayerKind::Terrain, "terrain", solid(size, Qt::green)},
        {EMinimapBakeLayerKind::StaticGeometry, "primary.mob", solid(size, QColor(255, 0, 0, 128))},
        {EMinimapBakeLayerKind::DynamicObject, "unit", solid(size, Qt::blue)}};
    const auto first = bakeMinimapLayers(2, 1, layers);
    const auto second = bakeMinimapLayers(2, 1, layers);
    QVERIFY2(first.success, qPrintable(first.error));
    QCOMPARE(first.reviewImage.size(), size);
    QCOMPARE(first.gameImage.size(), QSize(256, 256));
    QCOMPARE(first.reviewImage, second.reviewImage);
    QVERIFY(first.reviewImage.pixelColor(0, 0).blue() == 0);
    QVERIFY(!first.incomplete);

    layers.removeAt(1);
    const auto mprOnly = bakeMinimapLayers(2, 1, layers);
    QVERIFY(mprOnly.success);
    QVERIFY(mprOnly.incomplete);
}

void MinimapExportTests::encodesInspectable256Mmp()
{
    QImage image = solid(QSize(256, 256), QColor(12, 34, 56, 255));
    QString diagnostic;
    const QByteArray bytes = ei::encodeMmp8888(image, &diagnostic);
    const auto inspection = ei::inspectMmp(bytes);
    QVERIFY2(inspection.valid, qPrintable(inspection.diagnostic));
    QCOMPARE(inspection.formatName, QString("8888"));
    QCOMPARE(inspection.baseImage, image);
    QCOMPARE(bytes, ei::encodeMmp8888(image));
}

void MinimapExportTests::createsAddsReplacesAndRefusesCollision()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString archivePath = directory.filePath("textures.res");
    SResTextureExportRequest request;
    request.destination = QFileInfo(archivePath);
    request.memberName = "zone_new_map";
    request.mmpPayload = ei::encodeMmp8888(solid(QSize(256, 256), Qt::red));
    request.backupRootOverride = directory.filePath("backups");
    auto created = exportMmpToRes(request);
    QVERIFY2(created.success, qPrintable(created.phase + ": " + created.error));
    QVERIFY(created.createdArchive);

    request.memberName = "second_map.mmp";
    request.mmpPayload = ei::encodeMmp8888(solid(QSize(256, 256), Qt::green));
    auto added = exportMmpToRes(request);
    QVERIFY2(added.success, qPrintable(added.phase + ": " + added.error));
    QVERIFY(!added.backupPath.isEmpty());

    request.memberName = "zone_new_map";
    request.mmpPayload = ei::encodeMmp8888(solid(QSize(256, 256), Qt::blue));
    const QByteArray beforeRefusal = [] (const QString& path) {
        QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll();
    }(archivePath);
    auto refused = exportMmpToRes(request);
    QVERIFY(!refused.success);
    QFile unchanged(archivePath); QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), beforeRefusal);
    unchanged.close();

    request.conflictPolicy = EResTextureConflictPolicy::Replace;
    auto replaced = exportMmpToRes(request);
    QVERIFY2(replaced.success, qPrintable(replaced.phase + ": " + replaced.error));
    QVERIFY(replaced.replacedMember);
    QFile finalFile(archivePath); QVERIFY(finalFile.open(QIODevice::ReadOnly));
    CResArchiveDocument finalArchive;
    QVERIFY(finalArchive.loadBytes(finalFile.readAll(), archivePath));
    QCOMPARE(finalArchive.memberPayload("second_map.mmp"),
             ei::encodeMmp8888(solid(QSize(256, 256), Qt::green)));
    QCOMPARE(finalArchive.memberPayload("zone_new_map.mmp"), request.mmpPayload);
}

void MinimapExportTests::validationFailureDoesNotPublish()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("new.res");
    SResTextureExportRequest request;
    request.destination = QFileInfo(path);
    request.memberName = "test";
    request.mmpPayload = ei::encodeMmp8888(solid(QSize(256, 256), Qt::black));
    request.simulateValidationFailure = true;
    const auto result = exportMmpToRes(request);
    QVERIFY(!result.success);
    QVERIFY(!QFileInfo::exists(path));
}

void MinimapExportTests::registersCatalogOnlyAfterExportAndPreservesObjectives()
{
    const QByteArray mapTable =
        "#zone gz1g gipat game\r\n"
        "#res\r\nzone1 zone1\r\n"
        "#maps\r\n11 9\r\noldmap zone1quest\r\n"
        "#unknown\r\nauthored bytes\r\n";
    SMinimapCatalogRegistrationRequest request;
    request.mapTableBytes = mapTable;
    request.zoneId = "gz1g";
    request.textureName = "newmap";
    request.registrationConfirmed = true;

    auto beforeExport = prepareMinimapCatalogRegistration(request);
    QVERIFY(!beforeExport.success);
    QCOMPARE(beforeExport.updatedMapTableBytes, mapTable);

    request.textureExportSucceeded = true;
    request.registrationConfirmed = false;
    auto unconfirmed = prepareMinimapCatalogRegistration(request);
    QVERIFY(!unconfirmed.success);
    QCOMPARE(unconfirmed.updatedMapTableBytes, mapTable);

    request.registrationConfirmed = true;
    auto registered = prepareMinimapCatalogRegistration(request);
    QVERIFY2(registered.success, qPrintable(registered.error));
    QCOMPARE(registered.preservedObjectivesTexture, QString("zone1quest"));
    QVERIFY(registered.updatedMapTableBytes.contains("newmap zone1quest\r\n"));
    QVERIFY(registered.updatedMapTableBytes.contains("#unknown\r\nauthored bytes\r\n"));
    QVERIFY(!registered.updatedMapTableBytes.contains("oldmap zone1quest"));
}

QTEST_APPLESS_MAIN(MinimapExportTests)
#include "minimap_export_tests.moc"

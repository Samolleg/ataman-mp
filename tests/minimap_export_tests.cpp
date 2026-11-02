#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>
#include <QThreadPool>

#include "minimap_bake.h"
#include "minimap_catalog_registration.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"
#include "res_archive_document.h"
#include "res_texture_export.h"
#include "synthetic_map_table_archive.h"
#include "process_edit_lock.h"
#include "file_write_activity.h"
#include <QLockFile>

namespace {
QImage solid(const QSize& size, const QColor& color)
{ QImage image(size, QImage::Format_ARGB32); image.fill(color); return image; }

QByteArray emptyArchive()
{
    QByteArray empty(16, '\0');
    QDataStream header(&empty, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(0x019ce23c) << quint32(0) << quint32(16) << quint32(0);
    return empty;
}

QByteArray archiveWithMapTable(const QByteArray& mapTable)
{
    CResArchiveDocument baseline;
    baseline.loadBytes(emptyArchive(), "empty.res");
    QByteArray result;
    baseline.buildWithAppendedMember("map.txt", mapTable, 0, &result);
    return result;
}

}

class MinimapExportTests : public QObject
{
    Q_OBJECT
private slots:
    void cleanupTestCase() { QVERIFY(QThreadPool::globalInstance()->waitForDone(10000)); }
    void bakes128PixelsPerSectorAndExcludesDynamic();
    void normalizesOrientationAndFramesWholeMpr();
    void composesTerrainOverlayAndStaticInOrder();
    void encodesInspectable256Mmp();
    void encodesDeterministicJmvDxt1Profile();
    void preservesHighResolutionAspectPadding();
    void gradesShadingAndColorDeterministically();
    void extraShadingPreservesWeatherColor();
    void createsAddsReplacesAndRefusesCollision();
    void appendValidationMatchesByNameAfterHashTableReorder();
    void validationFailureDoesNotPublish();
    void validationFailurePreservesExistingArchive();
    void registersCatalogOnlyAfterExportAndPreservesObjectives();
    void stagesReviewedTextureInRunOwnedArchive();
    void stagesAppliedTextureBesideTerrainAndReplacesGenerationCleanly();
    void stagesTemporaryCatalogReferenceWithoutObjectivesMutation();
    void temporaryRuntimeNameAvoidsInstalledTextureShadowing();
    void resourceAndSaveLocksRefuseExport();
    void externalChangesAndAppearingDestinationArePreserved();
    void atomicFailureAndNoOpPreserveArchive();
};

void MinimapExportTests::resourceAndSaveLocksRefuseExport()
{
    QTemporaryDir directory;const auto path=directory.filePath("locked.res");
    SResTextureExportRequest request;request.destination=QFileInfo(path);request.memberName="seed";
    request.backupRootOverride=directory.filePath("backups");
    request.mmpPayload=ei::encodeMmp8888(solid(QSize(64,64),Qt::red));
    QVERIFY(exportMmpToRes(request).success);
    QFile before(path);QVERIFY(before.open(QIODevice::ReadOnly));const auto baseline=before.readAll();before.close();
    request.memberName="second";
    for(const QString& lockPath:{CProcessEditLock::lockFilePath(path,false),CProcessEditLock::identity(path)+".mapper-save.lock"}) {
        QLockFile lock(lockPath);lock.setStaleLockTime(0);QVERIFY(lock.tryLock(0));
        QVERIFY(!exportMmpToRes(request).success);
        QFile disk(path);QVERIFY(disk.open(QIODevice::ReadOnly));QCOMPARE(disk.readAll(),baseline);
        QCOMPARE(CFileWriteActivity::active(),0);
    }
    QVERIFY(exportMmpToRes(request).success);
}

void MinimapExportTests::externalChangesAndAppearingDestinationArePreserved()
{
    for(bool existed:{false,true}) {
        QTemporaryDir directory;const auto path=directory.filePath("race.res");
        SResTextureExportRequest request;request.destination=QFileInfo(path);request.memberName="seed";
        request.backupRootOverride=directory.filePath("backups");
        request.mmpPayload=ei::encodeMmp8888(solid(QSize(64,64),Qt::red));
        if(existed)QVERIFY(exportMmpToRes(request).success);
        const auto foreign=archiveWithMapTable("#owner\nforeign update\n");
        bool called=false,active=false,written=false;
        request.memberName="second";
        request.beforeCommitForTest=[&](){
            called=true;active=CFileWriteActivity::active()>0;
            QFile disk(path);written=disk.open(QIODevice::WriteOnly)
                &&disk.write(foreign)==foreign.size();
        };
        QVERIFY(!exportMmpToRes(request).success);
        QVERIFY(called&&active&&written);
        QFile disk(path);QVERIFY(disk.open(QIODevice::ReadOnly));QCOMPARE(disk.readAll(),foreign);
        QCOMPARE(CFileWriteActivity::active(),0);
    }
}

void MinimapExportTests::atomicFailureAndNoOpPreserveArchive()
{
    QTemporaryDir directory;const auto path=directory.filePath("atomic.res");
    SResTextureExportRequest request;request.destination=QFileInfo(path);request.memberName="seed";
    request.backupRootOverride=directory.filePath("backups");
    request.mmpPayload=ei::encodeMmp8888(solid(QSize(64,64),Qt::red));
    request.simulateAtomicCommitFailure=true;
    QVERIFY(!exportMmpToRes(request).success);QVERIFY(!QFileInfo::exists(path));
    request.simulateAtomicCommitFailure=false;
    const auto first=exportMmpToRes(request);QVERIFY(first.success);
    QFile before(path);QVERIFY(before.open(QIODevice::ReadOnly));const auto baseline=before.readAll();before.close();
    request.conflictPolicy=EResTextureConflictPolicy::Replace;
    const auto noOp=exportMmpToRes(request);QVERIFY(noOp.success);QCOMPARE(noOp.newArchiveSha256,first.newArchiveSha256);
    request.memberName="second";request.simulateAtomicCommitFailure=true;
    const auto failed=exportMmpToRes(request);QVERIFY(!failed.success);QVERIFY(!failed.backupPath.isEmpty());
    QFile disk(path),backup(failed.backupPath);QVERIFY(disk.open(QIODevice::ReadOnly));QCOMPARE(disk.readAll(),baseline);
    QVERIFY(backup.open(QIODevice::ReadOnly));QCOMPARE(backup.readAll(),baseline);
}

void MinimapExportTests::normalizesOrientationAndFramesWholeMpr()
{
    QCOMPARE(minimapWorldToReviewPixel(QPointF(0, 0), 2, 1), QPointF(0, 0));
    QCOMPARE(minimapWorldToReviewPixel(QPointF(64, 32), 2, 1), QPointF(256, 128));
    QCOMPARE(minimapGameContentRect(11, 9), QRect(0, 0, 256, 209));
    QCOMPARE(minimapGameContentRect(9, 11), QRect(0, 0, 209, 256));

    QImage source(128, 128, QImage::Format_ARGB32);
    source.fill(Qt::black);
    source.setPixelColor(0, 0, Qt::red); // renderer top edge is world north
    SMinimapBakeOptions options;
    options.sourceOrientation = EMinimapCaptureOrientation::PositiveYUp;
    const auto result = bakeMinimapLayers(1, 1,
        {{EMinimapBakeLayerKind::Terrain, "terrain", source}}, options);
    QVERIFY(result.success);
    QCOMPARE(result.reviewImage.pixelColor(0, 127), QColor(Qt::red));
    QVERIFY(result.incomplete);
}

void MinimapExportTests::composesTerrainOverlayAndStaticInOrder()
{
    const QSize size(128, 128);
    QImage overlay = solid(size, Qt::transparent);
    overlay.setPixelColor(5, 6, QColor(0, 0, 255, 255));
    QImage geometry = solid(size, Qt::transparent);
    geometry.setPixelColor(5, 6, QColor(255, 0, 0, 255));
    SMinimapBakeOptions options;
    options.sourceOrientation = EMinimapCaptureOrientation::PositiveYDown;
    const auto result = bakeMinimapLayers(1, 1, {
        {EMinimapBakeLayerKind::Terrain, "terrain", solid(size, Qt::green)},
        {EMinimapBakeLayerKind::Overlay, "overlays", overlay},
        {EMinimapBakeLayerKind::StaticGeometry, "collection mobs", geometry}}, options);
    QVERIFY(result.success);
    QCOMPARE(result.reviewImage.pixelColor(5, 6), QColor(Qt::red));
    QVERIFY(!result.incomplete);
}

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
    QCOMPARE(first.gameContentRect, QRect(0, 0, 256, 128));
    QCOMPARE(first.gameImage.pixelColor(0, 200), QColor(Qt::black));
    QCOMPARE(first.gameImage.pixelColor(0, 0),
             first.reviewImage.pixelColor(0, 0));
    QCOMPARE(first.gameImage.pixelColor(255, 127),
             first.reviewImage.pixelColor(255, 127));
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

void MinimapExportTests::encodesDeterministicJmvDxt1Profile()
{
    QImage image(1024, 1024, QImage::Format_ARGB32);
    for(int y = 0; y < image.height(); ++y)
        for(int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x / 4, y / 4, (x + y) / 8));
    QString diagnostic;
    const QByteArray first = ei::encodeMmpDxt1(image, &diagnostic);
    const QByteArray second = ei::encodeMmpDxt1(image, &diagnostic);
    QCOMPARE(first, second);
    const auto inspection = ei::inspectMmp(first);
    QVERIFY2(inspection.valid, qPrintable(inspection.diagnostic));
    QCOMPARE(inspection.formatName, QString("DXT1"));
    QCOMPARE(inspection.baseImage.size(), QSize(1024, 1024));
    QVERIFY(first.size() < image.sizeInBytes());
    QCOMPARE(first.left(76).toHex(), QByteArray(
        "4d4d50000004000000040000010000004458543104000000008000000f000000"
        "01000000007c00000a00000005000000e003000005000000050000001f000000"
        "000000000500000000000000"));
}

void MinimapExportTests::preservesHighResolutionAspectPadding()
{
    SMinimapBakeOptions options;
    options.sourceOrientation = EMinimapCaptureOrientation::PositiveYDown;
    options.textureSize = 1024;
    const auto result = bakeMinimapLayers(11, 9,
        {{EMinimapBakeLayerKind::Terrain, "terrain",
          solid(QSize(1408, 1152), QColor(40, 80, 120))}}, options);
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.gameImage.size(), QSize(1024, 1024));
    QCOMPARE(result.gameContentRect, QRect(0, 0, 1024, 838));
    QCOMPARE(result.gameImage.pixelColor(500, 900), QColor(Qt::black));
}

void MinimapExportTests::gradesShadingAndColorDeterministically()
{
    const QImage unlit = solid(QSize(4, 4), QColor(160, 100, 40));
    const QImage shaded = solid(QSize(4, 4), QColor(80, 50, 20));
    SMinimapGradeSettings settings{0, 100, 100, 100};
    QCOMPARE(gradeMinimapImage(unlit, shaded, settings).pixelColor(0, 0),
             QColor(160, 100, 40));
    settings.shadingPercent = 100;
    QCOMPARE(gradeMinimapImage(unlit, shaded, settings).pixelColor(0, 0),
             QColor(80, 50, 20));
    settings.shadingPercent = 150;
    QCOMPARE(gradeMinimapImage(unlit, shaded, settings).pixelColor(0, 0),
             QColor(57, 35, 14));
    settings = {0, 100, 100, 100, 120};
    QCOMPARE(gradeMinimapImage(unlit, shaded, settings).pixelColor(0, 0),
             QColor(40, 160, 100));
    settings = {50, 0, 100, 100};
    const QColor desaturated = gradeMinimapImage(unlit, shaded, settings)
        .pixelColor(0, 0);
    QCOMPARE(desaturated.red(), desaturated.green());
    QCOMPARE(desaturated.green(), desaturated.blue());
    QCOMPARE(gradeMinimapImage(unlit, shaded, settings),
             gradeMinimapImage(unlit, shaded, settings));
}

void MinimapExportTests::extraShadingPreservesWeatherColor()
{
    const QImage raw = solid(QSize(2, 2), QColor(160, 160, 160, 173));
    const QImage lit = solid(QSize(2, 2), QColor(85, 61, 54, 173));
    SMinimapGradeSettings settings(100, 100, 100, 100, 0);
    QCOMPARE(gradeMinimapImage(raw, lit, settings), lit);
    QColor previous = lit.pixelColor(0, 0);
    for(int shade : {101, 128, 150})
    {
        settings.shadingPercent = shade;
        const QColor color = gradeMinimapImage(raw, lit, settings).pixelColor(0, 0);
        QVERIFY(color.red() <= previous.red());
        QVERIFY(color.green() <= previous.green());
        QVERIFY(color.blue() <= previous.blue());
        // One common attenuation keeps the RGB ratios within byte rounding.
        QVERIFY(qAbs(color.red() * 54 - color.blue() * 85) <= 85);
        QVERIFY(qAbs(color.green() * 54 - color.blue() * 61) <= 61);
        QVERIFY(color.blue() > 20); // old RGB extrapolation gave only 1 at150.
        QCOMPARE(color.alpha(), 173);
        previous = color;
    }
    settings.shadingPercent = 150;
    const auto black = solid(QSize(2, 2), QColor(0, 0, 0, 173));
    QCOMPARE(gradeMinimapImage(raw, black, settings), black);
    QCOMPARE(gradeMinimapImage(black, black, settings), black);
    QCOMPARE(gradeMinimapImage(black, lit, settings), lit);
}

void MinimapExportTests::appendValidationMatchesByNameAfterHashTableReorder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("reordered.res");
    SResTextureExportRequest request;
    request.destination = QFileInfo(path);
    request.backupRootOverride = directory.filePath("backups");
    request.mmpPayload = ei::encodeMmp8888(
        solid(QSize(256, 256), QColor(70, 80, 90)));
    request.memberName = "seed_member";
    QVERIFY(exportMmpToRes(request).success);

    bool observedReorder = false;
    for(int attempt = 0; attempt < 24 && !observedReorder; ++attempt)
    {
        QFile beforeFile(path);
        QVERIFY(beforeFile.open(QIODevice::ReadOnly));
        CResArchiveDocument before;
        QVERIFY(before.loadBytes(beforeFile.readAll(), path));
        beforeFile.close();
        QStringList beforeOrder;
        for(const SResArchiveMember& member : before.members())
            beforeOrder.append(member.name);

        request.memberName = QString("hash_shift_%1").arg(attempt);
        const SResTextureExportResult added = exportMmpToRes(request);
        QVERIFY2(added.success, qPrintable(added.phase + ": " + added.error));
        QFile afterFile(path);
        QVERIFY(afterFile.open(QIODevice::ReadOnly));
        CResArchiveDocument after;
        QVERIFY(after.loadBytes(afterFile.readAll(), path));
        QStringList afterPrefix;
        for(int index = 0; index < beforeOrder.size(); ++index)
            afterPrefix.append(after.members().at(index).name);
        observedReorder = beforeOrder != afterPrefix;
    }
    QVERIFY2(observedReorder,
             "Synthetic append sequence did not exercise a RES hash-table reorder.");
}

void MinimapExportTests::validationFailurePreservesExistingArchive()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("existing.res");
    SResTextureExportRequest request;
    request.destination = QFileInfo(path);
    request.memberName = "first";
    request.mmpPayload = ei::encodeMmp8888(solid(QSize(256, 256), Qt::red));
    QVERIFY(exportMmpToRes(request).success);
    QFile beforeFile(path);
    QVERIFY(beforeFile.open(QIODevice::ReadOnly));
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();

    request.memberName = "second";
    request.mmpPayload = ei::encodeMmp8888(solid(QSize(256, 256), Qt::blue));
    request.simulateValidationFailure = true;
    QVERIFY(!exportMmpToRes(request).success);
    QFile afterFile(path);
    QVERIFY(afterFile.open(QIODevice::ReadOnly));
    QCOMPARE(afterFile.readAll(), before);
}

void MinimapExportTests::stagesReviewedTextureInRunOwnedArchive()
{
    const QByteArray mmp = ei::encodeMmp8888(
        solid(QSize(256, 256), QColor(17, 33, 65)));
    QByteArray archiveBytes;
    QStringList errors;
    QVERIFY2(buildSyntheticMapTextureArchive(
        "reviewed_zone_map", mmp, &archiveBytes, &errors),
        qPrintable(errors.join('\n')));
    CResArchiveDocument archive;
    QVERIFY2(archive.loadBytes(archiveBytes, "temporary-minimap.res", &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(archive.members().size(), 1);
    QCOMPARE(archive.members().first().name, QString("reviewed_zone_map.mmp"));
    QCOMPARE(archive.memberPayload("REVIEWED_ZONE_MAP.MMP"), mmp);

    QByteArray repeated;
    QVERIFY(buildSyntheticMapTextureArchive(
        "reviewed_zone_map", mmp, &repeated, &errors));
    QCOMPARE(repeated, archiveBytes);
}

void MinimapExportTests::stagesAppliedTextureBesideTerrainAndReplacesGenerationCleanly()
{
    QByteArray terrainArchive;
    QStringList errors;
    CResArchiveDocument empty;
    QVERIFY(empty.loadBytes(emptyArchive(), "terrain-seed.res", &errors));
    QVector<SResArchiveNewMember> terrainPages;
    for(int page = 0; page < 9; ++page)
    {
        SResArchiveNewMember member;
        member.name = QString("runtime%1.mmp")
            .arg(page, 3, 10, QLatin1Char('0'));
        member.payload = QString("authored-terrain-page-%1")
            .arg(page).toLatin1();
        terrainPages.append(member);
    }
    QVERIFY2(empty.buildWithAppendedMembers(
                 terrainPages, &terrainArchive, &errors),
             qPrintable(errors.join('\n')));

    const QByteArray firstMmp = ei::encodeMmp8888(
        solid(QSize(256, 256), QColor(17, 33, 65)));
    const QString firstName = temporaryMinimapRuntimeTextureName(
        firstMmp, "run-with-terrain");
    CResArchiveDocument crashContract;
    QVERIFY(crashContract.loadBytes(
        terrainArchive, "nine-page-expanded-terrain.res", &errors));
    QCOMPARE(crashContract.members().size(), 9);
    QVERIFY(crashContract.memberPayload(firstName + ".mmp").isEmpty());
    QByteArray firstStaged;
    QVERIFY2(stageSyntheticMapTextureArchive(
                 terrainArchive, "rebased-terrain.res", firstName, firstMmp,
                 &firstStaged, &errors),
             qPrintable(errors.join('\n')));

    CResArchiveDocument firstReopened;
    QVERIFY2(firstReopened.loadBytes(firstStaged, "first-staged.res", &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(firstReopened.members().size(), 10);
    for(const SResArchiveNewMember& terrainPage : terrainPages)
        QCOMPARE(firstReopened.memberPayload(terrainPage.name),
                 terrainPage.payload);
    QCOMPARE(firstReopened.memberPayload(firstName + ".mmp"), firstMmp);

    const QByteArray mapTable =
        "#zone gz1g gipat game\r\n"
        "#res\r\nruntime runtime\r\n"
        "#maps\r\n2 1\r\nauthoredmap authoredquest\r\n";
    const QByteArray mapArchive = archiveWithMapTable(mapTable);
    QByteArray firstMapArchive;
    QVERIFY2(stageTemporaryMinimapReference(
                 mapArchive, "test-current-texts.res", "gz1g", firstName,
                 &firstMapArchive, &errors),
             qPrintable(errors.join('\n')));
    CResArchiveDocument firstMapReopened;
    QVERIFY(firstMapReopened.loadBytes(
        firstMapArchive, "first-map-table.res", &errors));
    QVERIFY(firstMapReopened.memberPayload("map.txt").contains(
        firstName.toLatin1() + " authoredquest\r\n"));

    // Applying the same reviewed generation is deterministic and idempotent.
    QByteArray repeated;
    QVERIFY2(stageSyntheticMapTextureArchive(
                 firstStaged, "first-staged.res", firstName, firstMmp,
                 &repeated, &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(repeated, firstStaged);

    // Regeneration starts from the same clean terrain candidate.  The new
    // package contains the new identity and never retains the prior preview.
    const QByteArray secondMmp = ei::encodeMmp8888(
        solid(QSize(256, 256), QColor(71, 19, 143)));
    const QString secondName = temporaryMinimapRuntimeTextureName(
        secondMmp, "run-with-terrain");
    QVERIFY(firstName != secondName);
    const QByteArray firstBeforeCollision = firstStaged;
    QByteArray refusedCollision("must-be-cleared");
    errors.clear();
    QVERIFY(!stageSyntheticMapTextureArchive(
        firstStaged, "first-staged.res", firstName, secondMmp,
        &refusedCollision, &errors));
    QVERIFY(refusedCollision.isEmpty());
    QCOMPARE(firstStaged, firstBeforeCollision);
    QVERIFY(!errors.isEmpty());
    errors.clear();
    QByteArray secondStaged;
    QVERIFY2(stageSyntheticMapTextureArchive(
                 terrainArchive, "rebased-terrain.res", secondName, secondMmp,
                 &secondStaged, &errors),
             qPrintable(errors.join('\n')));
    CResArchiveDocument secondReopened;
    QVERIFY2(secondReopened.loadBytes(
                 secondStaged, "second-staged.res", &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(secondReopened.members().size(), 10);
    for(const SResArchiveNewMember& terrainPage : terrainPages)
        QCOMPARE(secondReopened.memberPayload(terrainPage.name),
                 terrainPage.payload);
    QCOMPARE(secondReopened.memberPayload(secondName + ".mmp"), secondMmp);
    QVERIFY(secondReopened.memberPayload(firstName + ".mmp").isEmpty());
    QByteArray secondMapArchive;
    QVERIFY2(stageTemporaryMinimapReference(
                 mapArchive, "test-current-texts.res", "gz1g", secondName,
                 &secondMapArchive, &errors),
             qPrintable(errors.join('\n')));
    CResArchiveDocument secondMapReopened;
    QVERIFY(secondMapReopened.loadBytes(
        secondMapArchive, "second-map-table.res", &errors));
    QVERIFY(secondMapReopened.memberPayload("map.txt").contains(
        secondName.toLatin1() + " authoredquest\r\n"));
    QVERIFY(!secondMapReopened.memberPayload("map.txt").contains(
        firstName.toLatin1()));

    // Every transaction is derived in memory; the terrain candidate remains
    // byte-identical for ordinary save/reopen and retry paths.
    CResArchiveDocument sourceReopened;
    QVERIFY(sourceReopened.loadBytes(terrainArchive, "source-reopen.res", &errors));
    QCOMPARE(sourceReopened.members().size(), 9);
    for(const SResArchiveNewMember& terrainPage : terrainPages)
        QCOMPARE(sourceReopened.memberPayload(terrainPage.name),
                 terrainPage.payload);
}

void MinimapExportTests::stagesTemporaryCatalogReferenceWithoutObjectivesMutation()
{
    const QByteArray mapTable =
        "#zone gz1g gipat game\r\n"
        "#res\r\nzone1 zone1\r\n"
        "#maps\r\n2 1\r\noldmap authoredquest\r\n";
    const QByteArray source = archiveWithMapTable(mapTable);
    QByteArray staged;
    QStringList errors;
    QVERIFY2(stageTemporaryMinimapReference(
        source, "private-texts.res", "gz1g", "reviewedmap", &staged, &errors),
        qPrintable(errors.join('\n')));
    QVERIFY(staged != source);
    CResArchiveDocument reopened;
    QVERIFY(reopened.loadBytes(staged, "private-texts-reopen.res", &errors));
    const QByteArray updated = reopened.memberPayload("map.txt");
    QVERIFY(updated.contains("reviewedmap authoredquest\r\n"));
    QVERIFY(!updated.contains("oldmap authoredquest"));

    // This helper consumes only an in-memory run-owned candidate.
    QCOMPARE(archiveWithMapTable(mapTable), source);
}

void MinimapExportTests::temporaryRuntimeNameAvoidsInstalledTextureShadowing()
{
    const QByteArray mmp = ei::encodeMmp8888(
        solid(QSize(256, 256), QColor(31, 63, 127)));
    const QString first = temporaryMinimapRuntimeTextureName(mmp, "run-a");
    const QString repeated = temporaryMinimapRuntimeTextureName(mmp, "run-a");
    const QString nextRun = temporaryMinimapRuntimeTextureName(mmp, "run-b");
    QVERIFY(first.startsWith("eimmap_"));
    QCOMPARE(first, repeated);
    QVERIFY(first != nextRun);
    QVERIFY(!first.contains("zone1map", Qt::CaseInsensitive));
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

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir store;
    if(qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))
        qputenv("EI_MAPPER_TEST_LOCK_ROOT",store.path().toUtf8());
    MinimapExportTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "minimap_export_tests.moc"

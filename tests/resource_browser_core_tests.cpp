#include <QtTest>

#include "res_archive_index.h"
#include "resource_catalog.h"
#include "resource_model_preview.h"
#include "model_gallery_thumbnail.h"
#include "mmp_inspector.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QElapsedTimer>
#include <QTemporaryDir>

namespace {
QByteArray archiveBytes(const QVector<QPair<QByteArray, QByteArray>>& input)
{
    struct Member { QByteArray name; QByteArray payload; quint32 offset{0}; };
    QVector<Member> members;
    QByteArray bytes(16, '\0');
    QByteArray names;
    for(const auto& value : input)
    {
        Member member;
        member.name = value.first;
        member.payload = value.second;
        bytes.append(QByteArray((16 - bytes.size() % 16) % 16, '\0'));
        member.offset = quint32(bytes.size());
        bytes.append(member.payload);
        members.append(member);
    }
    bytes.append(QByteArray((16 - bytes.size() % 16) % 16, '\0'));
    const quint32 tableOffset = quint32(bytes.size());
    QByteArray tableBytes;
    QDataStream table(&tableBytes, QIODevice::WriteOnly);
    table.setByteOrder(QDataStream::LittleEndian);
    for(int index = 0; index < members.size(); ++index)
    {
        const Member& member = members.at(index);
        table << quint32(-1) << quint32(member.payload.size()) << member.offset
              << quint32(0x12340000 + index) << quint16(member.name.size())
              << quint32(names.size());
        names.append(member.name);
    }
    bytes.append(tableBytes);
    bytes.append(names);
    QDataStream header(&bytes, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(0x019CE23C) << quint32(members.size()) << tableOffset
           << quint32(names.size());
    return bytes;
}

QString writeArchive(QTemporaryDir& directory, const QString& name,
                     const QVector<QPair<QByteArray, QByteArray>>& members)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(archiveBytes(members)) < 0)
        return QString();
    return path;
}

QByteArray sha(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
}

class ResourceBrowserCoreTests : public QObject
{
    Q_OBJECT
private slots:
    void galleryThumbnailRendersTextureAndBoundsSafely();
    void galleryThumbnailReadOnlyModelProbe();
    void fileIndexIsMetadataOnlyAndReadsLazily();
    void sparse765MiBArchiveIndexesWithoutPayloadRead();
    void staleArchiveRejectsLazyRead();
    void catalogPreservesRuntimePrecedenceAndOverrideChain();
    void catalogFiltersOneArchiveExactly();
    void mmpWidgetPreviewUsesEditorOrientation();
    void fig8CpuPreviewRendersGeometry();
    void modelSceneDefaultsToAboveGroundCamera();
    void interactiveModelViewKeepsStableFramingForPanAndZoom();
    void cameraOrbitAndModelRotationAreDistinctTransforms();
    void modelPartDefaultsKeepHumanoidAnatomyAndCreatureAssemblies();
    void largeHumanoidAssembliesReserveDetailForBaseAnatomy();
    void textSnifferAcceptsExtensionlessCp1251AndRejectsBinary();
    void animationFallbackInfersSelectableActionLength();
    void transactionSupportsReplaceAddRenameDelete();
    void transactionPreservesSourceAndUnchangedPayloadOffsets();
    void transactionCanAtomicallyReplaceItsIndexedSource();
    void staleTransactionLeavesDestinationUntouched();
    void transactionRejectsCaseAndCp1251Collisions();
    void representativeArchivesIndexReadOnly();
};

void ResourceBrowserCoreTests::galleryThumbnailRendersTextureAndBoundsSafely()
{
    ei::SResourceModelScene scene; scene.valid=true;
    ei::SResourceModelPart part; part.name="building";
    ei::SResourceModelTriangle triangle;
    triangle.vertices[0].position=QVector3D(-1,0,0);
    triangle.vertices[1].position=QVector3D(1,0,0);
    triangle.vertices[2].position=QVector3D(0,0,2);
    for(auto& vertex:triangle.vertices) vertex.uv=QVector2D(0.25f,0.25f);
    part.triangles.append(triangle); scene.parts.append(part);
    QImage texture(4,4,QImage::Format_ARGB32); texture.fill(QColor(255,0,0));
    const QImage image=ei::renderModelGalleryThumbnail(scene,texture,"building",1,QSize(192,132));
    QCOMPARE(image.size(),QSize(192,132)); int red=0;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
        if(qRed(image.pixel(x,y))>100 && qGreen(image.pixel(x,y))==0) ++red;
    QVERIFY(red>100); QVERIFY(red<image.width()*image.height());
    texture.fill(Qt::transparent);
    const QImage transparent=ei::renderModelGalleryThumbnail(scene,texture,"building");
    QCOMPARE(transparent.pixel(96,66),qRgb(48,51,56));
    QVERIFY(ei::renderModelGalleryThumbnail({},texture,"missing").isNull());
    QCOMPARE(ei::renderModelGalleryThumbnail(scene,{},"building",1,QSize(9999,9999)).size(),QSize(512,512));
}

void ResourceBrowserCoreTests::fileIndexIsMetadataOnlyAndReadsLazily()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray large(8 * 1024 * 1024, 'x');
    const QString path = writeArchive(directory, "large.res",
        {{"large.bin", large}, {"note.txt", "hello"}});
    QVERIFY(!path.isEmpty());

    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    QCOMPARE(index.memberCount(), 2);
    QCOMPARE(index.retainedPayloadBytes(), qint64(0));
    QCOMPARE(index.member(0).dataSize, quint32(large.size()));
    QCOMPARE(index.readMemberPrefix(0, 32, &error), QByteArray(32, 'x'));
    QCOMPARE(index.retainedPayloadBytes(), qint64(0));
    QCOMPARE(index.memberSha256(1, &error), sha("hello"));
}

void ResourceBrowserCoreTests::sparse765MiBArchiveIndexesWithoutPayloadRead()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("jmv-sized.res");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const quint32 payloadOffset = 16;
    const quint32 payloadSize = 765u * 1024u * 1024u - 64u;
    const quint32 tableOffset = payloadOffset + payloadSize;
    QByteArray header(16, '\0');
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setByteOrder(QDataStream::LittleEndian);
    headerStream << quint32(0x019CE23C) << quint32(1) << tableOffset << quint32(8);
    QCOMPARE(file.write(header), qint64(16));
    QVERIFY(file.seek(tableOffset));
    QByteArray table;
    QDataStream tableStream(&table, QIODevice::WriteOnly);
    tableStream.setByteOrder(QDataStream::LittleEndian);
    tableStream << quint32(-1) << payloadSize << payloadOffset << quint32(0)
                << quint16(8) << quint32(0);
    QCOMPARE(file.write(table), qint64(22));
    QCOMPARE(file.write("huge.bin", 8), qint64(8));
    file.close();

    QElapsedTimer timer;
    timer.start();
    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString("indexing took %1 ms").arg(timer.elapsed())));
    QCOMPARE(index.sourceSize(), qint64(tableOffset) + 30);
    QCOMPARE(index.retainedPayloadBytes(), qint64(0));
    QCOMPARE(index.member(0).dataSize, payloadSize);
}

void ResourceBrowserCoreTests::staleArchiveRejectsLazyRead()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "stale.res", {{"one.bin", "old"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY(index.open(path, &error));
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::Append));
    QCOMPARE(changed.write("changed"), qint64(7));
    changed.close();
    QVERIFY(index.readMember(0, &error).isNull());
    QVERIFY(error.contains("changed", Qt::CaseInsensitive));
}

void ResourceBrowserCoreTests::catalogPreservesRuntimePrecedenceAndOverrideChain()
{
    QTemporaryDir directory;
    const QString high = writeArchive(directory, "high.res",
        {{"shared.mmp", "high"}, {"only-high.txt", "a"}});
    const QString low = writeArchive(directory, "low.res",
        {{"SHARED.MMP", "low"}, {"only-low.lua", "b"}});
    CResourceCatalog catalog;
    QStringList errors;
    QVERIFY2(catalog.build({high, low}, &errors), qPrintable(errors.join('\n')));
    const QVector<SResourceCatalogEntry> shared = catalog.providers("shared.mmp");
    QCOMPARE(shared.size(), 2);
    QVERIFY(shared.at(0).winning);
    QVERIFY(!shared.at(1).winning);
    QCOMPARE(shared.at(0).archivePath, high);
    QCOMPARE(shared.at(1).archivePath, low);
    QCOMPARE(catalog.entries().size(), 4);
}

void ResourceBrowserCoreTests::catalogFiltersOneArchiveExactly()
{
    QTemporaryDir directory;
    const QString first = writeArchive(directory, "first.res", {{"a.fig", "a"}, {"b.mmp", "b"}});
    const QString second = writeArchive(directory, "second.res", {{"c.anm", "c"}});
    CResourceCatalog catalog;
    QVERIFY(catalog.build({first, second}));
    const QVector<SResourceCatalogEntry> filtered = catalog.entriesForArchive(first);
    QCOMPARE(filtered.size(), 2);
    for(const SResourceCatalogEntry& entry : filtered)
        QCOMPARE(QFileInfo(entry.archivePath).absoluteFilePath(), QFileInfo(first).absoluteFilePath());
}

void ResourceBrowserCoreTests::mmpWidgetPreviewUsesEditorOrientation()
{
    QImage decoded(1, 2, QImage::Format_ARGB32);
    decoded.setPixelColor(0, 0, Qt::red);
    decoded.setPixelColor(0, 1, Qt::blue);
    const QImage preview = ei::mmpEditorPreviewImage(decoded);
    QCOMPARE(preview.pixelColor(0, 0), QColor(Qt::blue));
    QCOMPARE(preview.pixelColor(0, 1), QColor(Qt::red));
}

void ResourceBrowserCoreTests::fig8CpuPreviewRendersGeometry()
{
    QByteArray fig;
    QDataStream stream(&fig, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0x38474946) << quint32(1) << quint32(1) << quint32(3)
           << quint32(3) << quint32(3) << quint32(0) << quint32(0)
           << quint32(0) << quint32(0);
    for(int block = 0; block < 3; ++block)
        for(int morph = 0; morph < 8; ++morph)
            stream << 0.0f << 0.0f << 0.0f;
    for(int morph = 0; morph < 8; ++morph) stream << 1.0f;
    const QVector<QVector3D> vertices = {QVector3D(-1, 0, 0), QVector3D(1, 0, 0),
                                         QVector3D(0, 0, 1), QVector3D(0, 0, 0)};
    for(int axis = 0; axis < 3; ++axis)
        for(int morph = 0; morph < 8; ++morph)
            for(const QVector3D& vertex : vertices) stream << vertex[axis];
    for(int axis = 0; axis < 4; ++axis)
        for(int lane = 0; lane < 4; ++lane) stream << (axis == 2 ? 1.0f : 0.0f);
    for(int uv = 0; uv < 3; ++uv) stream << 0.0f << 0.0f;
    stream << quint16(0) << quint16(1) << quint16(2);
    stream << quint16(0) << quint16(0) << quint16(0)
           << quint16(1) << quint16(0) << quint16(1)
           << quint16(2) << quint16(0) << quint16(2);
    const ei::SResourceModelPreview preview = ei::previewFig8(fig, QSize(320, 240));
    QVERIFY2(preview.valid, qPrintable(preview.diagnostics.join('\n')));
    QCOMPARE(preview.triangleCount, 1);
    QVERIFY(!preview.image.isNull());
}

void ResourceBrowserCoreTests::modelSceneDefaultsToAboveGroundCamera()
{
    QCOMPARE(ei::resourceModelDefaultAzimuthDegrees(), 0.0f);
    QCOMPARE(ei::resourceModelDefaultElevationDegrees(), 0.0f);
}

void ResourceBrowserCoreTests::interactiveModelViewKeepsStableFramingForPanAndZoom()
{
    ei::SResourceModelScene scene; scene.valid = true;
    ei::SResourceModelPart part; part.name = "bd";
    ei::SResourceModelTriangle triangle;
    triangle.vertices[0].position = QVector3D(-1, 0, 0);
    triangle.vertices[1].position = QVector3D(1, 0, 0);
    triangle.vertices[2].position = QVector3D(0, 0, 2);
    part.triangles.append(triangle); scene.parts.append(part);
    ei::SResourceModelView normal;
    const QImage first = ei::renderResourceModelScene(scene, normal, QSize(400, 300));
    normal.zoom = 2.0f;
    const QImage zoomed = ei::renderResourceModelScene(scene, normal, QSize(400, 300));
    normal.pan = QVector3D(0.25f, 0, 0);
    const QImage panned = ei::renderResourceModelScene(scene, normal, QSize(400, 300));
    const auto foreground = [](const QImage& image) {
        QRect bounds;
        const QRgb background = image.pixel(0, 0);
        for(int y = 0; y < image.height(); ++y)
            for(int x = 0; x < image.width(); ++x)
                if(image.pixel(x, y) != background) bounds |= QRect(x, y, 1, 1);
        return bounds;
    };
    QVERIFY(foreground(zoomed).width() > foreground(first).width() * 1.5);
    QVERIFY(foreground(panned).center().x() > foreground(zoomed).center().x() + 20);
}

void ResourceBrowserCoreTests::cameraOrbitAndModelRotationAreDistinctTransforms()
{
    ei::SResourceModelScene scene; scene.valid = true;
    ei::SResourceModelPart part; part.name = "asymmetric";
    for(const QVector<QVector3D>& points : {
        QVector<QVector3D>{QVector3D(-2, 0, 0), QVector3D(1, 1, 0), QVector3D(0, 0, 3)},
        QVector<QVector3D>{QVector3D(0, 0, 3), QVector3D(1, 1, 0), QVector3D(0, 2, 1)}})
    {
        ei::SResourceModelTriangle triangle;
        for(int index = 0; index < 3; ++index) triangle.vertices[index].position = points[index];
        part.triangles.append(triangle);
    }
    scene.parts.append(part);
    ei::SResourceModelView model; model.modelYawDegrees = 35; model.modelPitchDegrees = 20;
    ei::SResourceModelView camera; camera.azimuthDegrees = 35; camera.elevationDegrees = 20;
    QVERIFY(ei::renderResourceModelScene(scene, model, QSize(400, 300))
        != ei::renderResourceModelScene(scene, camera, QSize(400, 300)));
}

void ResourceBrowserCoreTests::modelPartDefaultsKeepHumanoidAnatomyAndCreatureAssemblies()
{
    ei::SResourceModelScene humanoid;
    for(const QString& name : {QStringLiteral("bd"), QStringLiteral("lh1.skin00"),
                                QStringLiteral("ll2.armor00"), QStringLiteral("rh3.sword08")})
    {
        ei::SResourceModelPart part; part.name = name; humanoid.parts.append(part);
    }
    const QSet<QString> humanVisible = ei::defaultVisibleModelParts(humanoid, "unhuma");
    QVERIFY(humanVisible.contains("bd"));
    QVERIFY(!humanVisible.contains("lh1.skin00"));
    QVERIFY(!humanVisible.contains("ll2.armor00"));
    QVERIFY(!humanVisible.contains("rh3.sword08"));

    ei::SResourceModelScene creature;
    for(const QString& name : {QStringLiteral("bd"), QStringLiteral("leftwing"),
                                QStringLiteral("weapon"), QStringLiteral("tail")})
    {
        ei::SResourceModelPart part; part.name = name; creature.parts.append(part);
    }
    const QSet<QString> creatureVisible = ei::defaultVisibleModelParts(creature, "unmodg");
    QCOMPARE(creatureVisible.size(), creature.parts.size());
}

void ResourceBrowserCoreTests::largeHumanoidAssembliesReserveDetailForBaseAnatomy()
{
    const int bodyBudget = ei::resourceModelPreviewTriangleBudget("bd", 4704, 752, 24116);
    const int armorBudget = ei::resourceModelPreviewTriangleBudget("bd.armor24", 4704, 752, 24116);
    QCOMPARE(bodyBudget, 4704);
    QVERIFY(armorBudget >= 250);
    QVERIFY(bodyBudget > armorBudget * 5);
    QCOMPARE(ei::resourceModelPreviewTriangleBudget("leftwing", 4000, 20, 0), 2500);
    QVERIFY(ei::isHumanoidAssemblyStem("unhuli"));
}

void ResourceBrowserCoreTests::textSnifferAcceptsExtensionlessCp1251AndRejectsBinary()
{
    const QByteArray cp1251 = QByteArray::fromHex("cff0e8e2e5f220ece8f0210d0a");
    QVERIFY(ei::resourcePayloadLooksText(cp1251));
    QVERIFY(!ei::resourcePayloadLooksText(QByteArray("A\0B\1C", 5)));
}

void ResourceBrowserCoreTests::animationFallbackInfersSelectableActionLength()
{
    SAnimationClip clip;
    clip.logicalLength = 0;
    SAnimationChannel channel;
    channel.rotations.resize(17);
    channel.translations.resize(9);
    clip.channels.insert("bd", channel);
    QCOMPARE(ei::inferredAnimationLogicalLength(clip), 16);
}

void ResourceBrowserCoreTests::transactionSupportsReplaceAddRenameDelete()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "edit.res",
        {{"keep.bin", "keep"}, {"replace.txt", "before"}, {"rename.lua", "lua"},
         {"delete.bin", "gone"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    CResArchiveTransaction transaction(index);
    QVERIFY(transaction.replace("replace.txt", sha("before"), "after", &error));
    QVERIFY(transaction.add("new.txt", "new", 7, &error));
    QVERIFY(transaction.rename("rename.lua", "renamed.lua", &error));
    QVERIFY(transaction.remove("delete.bin", &error));
    const QString output = directory.filePath("candidate.res");
    QVERIFY2(transaction.writeCandidate(output, &error), qPrintable(error));

    CResArchiveIndex reopened;
    QVERIFY2(reopened.open(output, &error), qPrintable(error));
    QCOMPARE(reopened.readMember("keep.bin", &error), QByteArray("keep"));
    QCOMPARE(reopened.readMember("replace.txt", &error), QByteArray("after"));
    QCOMPARE(reopened.readMember("new.txt", &error), QByteArray("new"));
    QCOMPARE(reopened.readMember("renamed.lua", &error), QByteArray("lua"));
    QVERIFY(reopened.memberIndex("delete.bin") < 0);
}

void ResourceBrowserCoreTests::transactionPreservesSourceAndUnchangedPayloadOffsets()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "source.res",
        {{"unchanged.bin", "unchanged-payload"}, {"replace.txt", "before"}});
    QFile source(path);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = source.readAll();
    source.close();

    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    const quint32 unchangedOffset = index.member(index.memberIndex("unchanged.bin")).dataOffset;
    CResArchiveTransaction transaction(index);
    QVERIFY(transaction.replace("replace.txt", sha("before"), "after", &error));
    const QString output = directory.filePath("candidate.res");
    QVERIFY2(transaction.writeCandidate(output, &error), qPrintable(error));

    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), originalBytes);
    CResArchiveIndex reopened;
    QVERIFY2(reopened.open(output, &error), qPrintable(error));
    QCOMPARE(reopened.member(reopened.memberIndex("unchanged.bin")).dataOffset, unchangedOffset);
    QCOMPARE(reopened.readMember("unchanged.bin", &error), QByteArray("unchanged-payload"));
}

void ResourceBrowserCoreTests::transactionCanAtomicallyReplaceItsIndexedSource()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "loaded.res",
        {{"keep.bin", "keep"}, {"remove.bin", "remove"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    CResArchiveTransaction transaction(index);
    QVERIFY2(transaction.remove("remove.bin", &error), qPrintable(error));

    // A loaded mod archive is a supported publication target. Publication must
    // be atomic, semantically reopenable, and leave the retained member intact.
    QVERIFY2(transaction.writeCandidate(path, &error), qPrintable(error));
    CResArchiveIndex reopened;
    QVERIFY2(reopened.open(path, &error), qPrintable(error));
    QCOMPARE(reopened.readMember("keep.bin", &error), QByteArray("keep"));
    QCOMPARE(reopened.memberIndex("remove.bin"), -1);
}

void ResourceBrowserCoreTests::staleTransactionLeavesDestinationUntouched()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "source.res", {{"replace.txt", "before"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    CResArchiveTransaction transaction(index);
    QVERIFY(transaction.replace("replace.txt", sha("before"), "after", &error));

    QFile source(path);
    QVERIFY(source.open(QIODevice::Append));
    QCOMPARE(source.write("stale"), qint64(5));
    source.close();
    const QString destination = directory.filePath("existing.res");
    QFile existing(destination);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("destination-must-survive"), qint64(24));
    existing.close();

    QVERIFY(!transaction.writeCandidate(destination, &error));
    QVERIFY(error.contains("changed", Qt::CaseInsensitive)
            || error.contains("stale", Qt::CaseInsensitive));
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("destination-must-survive"));
}

void ResourceBrowserCoreTests::transactionRejectsCaseAndCp1251Collisions()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "edit.res", {{"Name.TXT", "one"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY(index.open(path, &error));
    CResArchiveTransaction transaction(index);
    QVERIFY(!transaction.add("name.txt", "two", 0, &error));
    QVERIFY(error.contains("collision", Qt::CaseInsensitive));
    error.clear();
    QVERIFY(!transaction.add(QString::fromUtf8("日本語.txt"), "two", 0, &error));
    QVERIFY(error.contains("CP1251", Qt::CaseInsensitive));
}

void ResourceBrowserCoreTests::galleryThumbnailReadOnlyModelProbe()
{
    const QString paths = qEnvironmentVariable("EI_GALLERY_PROBE_ARCHIVES");
    if(paths.isEmpty()) QSKIP("No read-only model probe archives configured.");
    CResourceCatalog catalog;
    QStringList errors;
    QVERIFY2(catalog.build(paths.split(';').toVector(), &errors), qPrintable(errors.join('\n')));
    const auto read = [&catalog](const QString& name) {
        const auto entries = catalog.providers(name);
        return entries.isEmpty() ? QByteArray() : catalog.archive(entries.first().archivePriority)->readMember(entries.first().memberIndex);
    };
    const QString model = qEnvironmentVariable("EI_GALLERY_PROBE_MODEL", "stbuho45");
    const auto scene = ei::buildModScene(read(model + ".mod"), read(model + ".bon"));
    QVERIFY2(scene.valid, qPrintable(scene.diagnostics.join('\n')));
    const auto texture = ei::inspectMmp(read(qEnvironmentVariable("EI_GALLERY_PROBE_TEXTURE", "hadoganveranda00") + ".mmp"));
    QVERIFY2(texture.valid, qPrintable(texture.diagnostic));
    const QImage image = ei::renderModelGalleryThumbnail(scene, texture.baseImage, model);
    QVERIFY(!image.isNull());
    int pixels = 0;
    for(int y=0; y<image.height(); ++y) for(int x=0; x<image.width(); ++x)
        if(image.pixel(x,y) != qRgb(48,51,56)) ++pixels;
    QVERIFY(pixels > 100);
    const QString output = qEnvironmentVariable("EI_GALLERY_PROBE_OUTPUT");
    if(!output.isEmpty()) QVERIFY(image.save(output));
    qInfo() << "read-only model thumbnail" << model << "parts" << scene.parts.size() << "paintedPixels" << pixels;
}

void ResourceBrowserCoreTests::representativeArchivesIndexReadOnly()
{
    const QString configured = qEnvironmentVariable("EI_RES_READONLY_PROBES");
    if(configured.isEmpty()) QSKIP("No read-only representative archive paths configured.");
    for(const QString& path : configured.split(';', QString::SkipEmptyParts))
    {
        QElapsedTimer timer;
        timer.start();
        CResArchiveIndex index;
        QString error;
        QVERIFY2(index.open(path, &error), qPrintable(path + ": " + error));
        QCOMPARE(index.retainedPayloadBytes(), qint64(0));
        qInfo().noquote() << QString("readonly-res-index path=%1 size=%2 members=%3 elapsedMs=%4 retainedPayloadBytes=0")
            .arg(path).arg(index.sourceSize()).arg(index.memberCount()).arg(timer.elapsed());
    }
}

QTEST_APPLESS_MAIN(ResourceBrowserCoreTests)
#include "resource_browser_core_tests.moc"

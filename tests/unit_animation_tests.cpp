#include <QtTest>
#include <QDataStream>
#include <QTemporaryDir>
#include <QRandomGenerator>
#include <QThread>
#include "unit_animation.h"
#include "profile_visual_provider.h"
#include "unit_visual_preparation.h"
#include "persistent_visual_cache.h"

namespace {

struct TestAction { QByteArray name; quint32 number; quint64 packed; quint32 probability; quint32 length; };

QByteArray adb(const QVector<TestAction>& actions)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out.writeRawData("ADB\0", 4); out << quint32(actions.size());
    out.writeRawData(QByteArray(24 + 12, '\0').constData(), 36);
    for(const TestAction& action : actions)
    {
        QByteArray name = action.name.leftJustified(16, '\0', true);
        out.writeRawData(name.constData(), 16);
        out << action.number << action.packed << action.probability << action.length << float(0);
        for(int i = 0; i < 12; ++i) out << quint32(0);
    }
    return bytes;
}

quint64 idlePacked(quint16 weaponMask, quint8 state = 0)
{
    return quint64(weaponMask) | (quint64(state) << 15) | (quint64(6) << 18) | (quint64(2) << 30);
}

QByteArray res(const QMap<QByteArray, QByteArray>& members)
{
    QByteArray names, payload;
    struct Entry { quint32 size, offset; quint16 nameLength; quint32 nameOffset; };
    QVector<Entry> entries;
    const quint32 tableOffset = 16;
    quint32 dataOffset = tableOffset + quint32(members.size()) * 22;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        Entry e{quint32(it.value().size()), dataOffset + quint32(payload.size()),
                quint16(it.key().size()), quint32(names.size())};
        entries.append(e); names += it.key(); payload += it.value();
    }
    dataOffset += quint32(names.size());
    for(int i = 0; i < entries.size(); ++i) entries[i].offset += quint32(names.size());
    QByteArray result;
    QDataStream out(&result, QIODevice::WriteOnly); out.setByteOrder(QDataStream::LittleEndian);
    out << quint32(0x019CE23C) << quint32(entries.size()) << tableOffset << quint32(names.size());
    for(const Entry& e : entries)
        out << quint32(0) << e.size << e.offset << quint32(0) << e.nameLength << e.nameOffset;
    out.writeRawData(names.constData(), names.size()); out.writeRawData(payload.constData(), payload.size());
    return result;
}

QByteArray channel()
{
    QByteArray bytes; QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian); out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << quint32(3);
    out << float(1) << float(0) << float(0) << float(0);
    out << float(1) << float(0) << float(0) << float(0);
    out << float(1) << float(0) << float(0) << float(0);
    out << quint32(3);
    out << float(0) << float(0) << float(0);
    out << float(2) << float(0) << float(0);
    out << float(0) << float(0) << float(0);
    return bytes;
}

QByteArray morphChannel()
{
    QByteArray bytes = channel();
    QDataStream out(&bytes, QIODevice::Append);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << quint32(3) << quint32(1);
    out << float(0) << float(0) << float(0);
    out << float(2) << float(0) << float(0);
    out << float(0) << float(0) << float(0);
    return bytes;
}

}

class UnitAnimationTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesAndSelectsDeterministically();
    void providersSeparateBaseAndJmvMasks();
    void jmvSelectsStandingModelSpecificIdles();
    void jmvTitanGripUsesOrdinaryDualWieldIdle();
    void parsesNestedClipAndClosingSample();
    void anchorsRootTranslationToBindPose();
    void interpolatesMorphClosingSample();
    void filtersPoseToRenderedSkeleton();
    void propagatesPoseToNumberedEquipment();
    void inheritsAttachmentRotationOnlyOnce();
    void preservesChildBindOffsetAndIgnoresChildTranslation();
    void preMultipliesChildRotationInEiOrder();
    void appliesDistanceTiers();
    void boundsColdVisualResolutionDuringCameraMovement();
    void resolvesAppearancePreparationWithoutSceneObjects();
    void cachesPreparedAppearanceByContentIdentity();
    void sharesDatabaseAppearancePreparationAcrossEquivalentUnits();
    void groupsAllEquivalentUnitsAcrossDistanceOrderedWork();
    void keepsDistinctAssembliesInSeparatePreparationGroups();
    void boundsAppearancePreparationWorkerCount();
    void keepsAppearanceProgressVisibleBetweenWorkerBatches();
    void pumpsIncompleteIdleVisualPreparation();
    void retainsAppearanceBatchOwnershipUntilResultsAreConsumed();
    void continuesVisualFinalizationWhileCameraMoves();
    void ignoresIdleVisualProgressBeforeWorkStarts();
    void keepsAppearanceCacheBesideEditor();
    void rebuildsIncompatibleAppearanceCacheAutomatically();
    void cachesParsedAnimationAndRecoversFromCorruption();
    void roundTripsComposedAtlasAsGpuReadyRgba8();
    void roundTripsFigureAssemblyAndUnknownPartNames();
    void retainsRecentlyUsedVisualsAcrossMapReplacement();
    void enforcesCombinedPersistentVisualCacheLimit();
    void removesSlowerDeprecatedAssemblyCache();
    void malformedAndUnknownInputsFailStatic();
};

void UnitAnimationTests::retainsRecentlyUsedVisualsAcrossMapReplacement()
{
    ei::CVisualCacheLru cache;
    cache.touch(QStringLiteral("gz18h-a"), 40);
    cache.touch(QStringLiteral("gz18h-b"), 40);
    cache.touch(QStringLiteral("gz11"), 30);
    QCOMPARE(cache.trim(120), QStringList());

    cache.touch(QStringLiteral("gz18h-a"), 40);
    cache.touch(QStringLiteral("gz19"), 30);
    QCOMPARE(cache.trim(120), QStringList() << QStringLiteral("gz18h-b"));
    QCOMPARE(cache.oldestFirst(), QStringList()
             << QStringLiteral("gz11") << QStringLiteral("gz18h-a")
             << QStringLiteral("gz19"));
    QCOMPARE(cache.totalBytes(), qint64(100));

    cache.clear();
    QCOMPARE(cache.totalBytes(), qint64(0));
    QVERIFY(cache.oldestFirst().isEmpty());
}

void UnitAnimationTests::enforcesCombinedPersistentVisualCacheLimit()
{
    QTemporaryDir application;
    QVERIFY(application.isValid());
    QByteArray noisy(700 * 1024, Qt::Uninitialized);
    QRandomGenerator random(0x45494d50u);
    for(int index = 0; index < noisy.size(); ++index)
        noisy[index] = char(random.generate() & 0xffu);
    const QString atlases = ei::visualCacheDirectoryForApplication(
        application.path(), QStringLiteral("character-atlases"));
    const QString assemblies = ei::visualCacheDirectoryForApplication(
        application.path(), QStringLiteral("character-assemblies"));
    ei::setVisualCacheLimitMiB(1);
    QVERIFY(ei::writeVisualCachePayload(atlases, QStringLiteral("old"), 77, noisy));
    QThread::msleep(20);
    const QString plans = ei::visualCacheDirectoryForApplication(
        application.path(), QStringLiteral("unit-appearances"));
    QVERIFY(QDir().mkpath(plans));
    const QString planPath = QDir(plans).filePath(QStringLiteral("plan.json"));
    QFile plan(planPath);
    QVERIFY(plan.open(QIODevice::WriteOnly));
    QCOMPARE(plan.write(noisy), qint64(noisy.size()));
    plan.close();
    ei::recordVisualCacheFileWrite(plans, 0, QFileInfo(planPath).size());
    QVERIFY(!QFileInfo(QDir(atlases).filePath(QStringLiteral("old.cache"))).exists());
    QVERIFY(QFileInfo(planPath).exists());
    QThread::msleep(20);
    QVERIFY(ei::writeVisualCachePayload(assemblies, QStringLiteral("new"), 77, noisy));
    QVERIFY(!QFileInfo(planPath).exists());
    QVERIFY(QFileInfo(QDir(assemblies).filePath(QStringLiteral("new.cache"))).exists());

    ei::setVisualCacheLimitMiB(0);
    QVERIFY(ei::writeVisualCachePayload(atlases, QStringLiteral("unlimited"), 77,
                                        noisy + noisy));
    QVERIFY(QFileInfo(QDir(atlases).filePath(QStringLiteral("unlimited.cache"))).exists());
    ei::setVisualCacheLimitMiB(4096);
}

void UnitAnimationTests::removesSlowerDeprecatedAssemblyCache()
{
    QTemporaryDir application;
    QVERIFY(application.isValid());
    const QString directory = ei::visualCacheDirectoryForApplication(
        application.path(), QStringLiteral("character-assemblies"));
    QVERIFY(QDir().mkpath(directory));
    QFile stale(QDir(directory).filePath(QStringLiteral("stale.cache")));
    QVERIFY(stale.open(QIODevice::WriteOnly));
    QVERIFY(stale.write("obsolete") > 0);
    stale.close();
    QVERIFY(ei::removeDeprecatedVisualAssemblyCacheForApplication(application.path()));
    QVERIFY(!QDir(directory).exists());
}

void UnitAnimationTests::cachesParsedAnimationAndRecoversFromCorruption()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    const QByteArray archive = res({{"idle", res({{"body", channel()}})}});
    bool hit = true;
    const SAnimationClip cold = loadOrParseCachedAnimationClip(
        archive, QStringLiteral("idle"), 3, cache.path(), &hit);
    QVERIFY(cold.valid());
    QVERIFY(!hit);
    const SAnimationClip warm = loadOrParseCachedAnimationClip(
        archive, QStringLiteral("idle"), 3, cache.path(), &hit);
    QVERIFY(hit);
    QCOMPARE(warm.channels.size(), cold.channels.size());
    QCOMPARE(warm.channels.value(QStringLiteral("body")).translations.size(), 3);

    const QStringList entries = QDir(cache.path()).entryList(
        QStringList() << QStringLiteral("*.cache"), QDir::Files);
    QCOMPARE(entries.size(), 1);
    QFile corrupt(QDir(cache.path()).filePath(entries.first()));
    QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    corrupt.write("broken");
    corrupt.close();
    const SAnimationClip recovered = loadOrParseCachedAnimationClip(
        archive, QStringLiteral("idle"), 3, cache.path(), &hit);
    QVERIFY(recovered.valid());
    QVERIFY(!hit);
}

void UnitAnimationTests::roundTripsComposedAtlasAsGpuReadyRgba8()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    QImage source(2, 2, QImage::Format_RGBA64);
    source.setPixelColor(0, 0, QColor::fromRgba64(1, 2, 3, 4));
    source.setPixelColor(1, 0, QColor::fromRgba64(65535, 32768, 12, 50000));
    source.setPixelColor(0, 1, QColor::fromRgba64(100, 200, 300, 400));
    source.setPixelColor(1, 1, QColor::fromRgba64(65534, 65533, 65532, 65531));
    QVERIFY(ei::writeCachedVisualImage(cache.path(), QStringLiteral("atlas"), source));
    QImage restored;
    QVERIFY(ei::readCachedVisualImage(cache.path(), QStringLiteral("atlas"), &restored));
    QCOMPARE(restored.format(), QImage::Format_RGBA8888);
    QCOMPARE(restored.size(), source.size());
    const QImage expected = source.convertToFormat(QImage::Format_RGBA8888);
    for(int y = 0; y < source.height(); ++y)
        for(int x = 0; x < source.width(); ++x)
            QCOMPARE(restored.pixelColor(x, y), expected.pixelColor(x, y));
}

void UnitAnimationTests::roundTripsFigureAssemblyAndUnknownPartNames()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    ei::SCachedVisualPart source;
    source.name = QStringLiteral("future_unknown_part");
    source.parentName = QStringLiteral("bd");
    source.restOffset = QVector3D(1.25f, -2.5f, 3.75f);
    source.textureNumber = 2;
    QVector3D position(1, 2, 3), normal(0, 0, 1);
    QVector2D uv(0.25f, 0.75f);
    source.vertices.append(SVertexData(position, normal, uv));
    source.sourceVertexIndices.append(42);
    QVERIFY(ei::writeCachedFigureAssembly(cache.path(), QStringLiteral("assembly"), {source}));
    QVector<ei::SCachedVisualPart> restored;
    QVERIFY(ei::readCachedFigureAssembly(cache.path(), QStringLiteral("assembly"), &restored));
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().name, source.name);
    QCOMPARE(restored.first().parentName, source.parentName);
    QCOMPARE(restored.first().textureNumber, 2);
    QCOMPARE(restored.first().vertices.size(), 1);
    QCOMPARE(restored.first().vertices.first().position, position);
    QCOMPARE(restored.first().vertices.first().texCoord, uv);
    QCOMPARE(restored.first().sourceVertexIndices, QVector<int>() << 42);
}

void UnitAnimationTests::pumpsIncompleteIdleVisualPreparation()
{
    // Exact reported stalled state: the viewport became idle with fifteen
    // characters left. It must keep requesting preparation frames.
    QVERIFY(ei::shouldPumpUnitVisualPreparation(true, 402, 387));
    QVERIFY(!ei::shouldPumpUnitVisualPreparation(true, 402, 402));
    QVERIFY(!ei::shouldPumpUnitVisualPreparation(false, 402, 387));
}

void UnitAnimationTests::retainsAppearanceBatchOwnershipUntilResultsAreConsumed()
{
    QVERIFY(ei::canStartUnitAppearanceBatch(false, false));
    QVERIFY(!ei::canStartUnitAppearanceBatch(false, true));
    // Exact race: the future finished, but the queued result consumer has not
    // run. isRunning() alone previously allowed the future to be replaced.
    QVERIFY(!ei::canStartUnitAppearanceBatch(true, false));
}

void UnitAnimationTests::continuesVisualFinalizationWhileCameraMoves()
{
    // Heavy-map regression: panning previously reduced the finalization limit
    // to zero, leaving the visible progress job stuck until navigation ended.
    QCOMPARE(ei::unitVisualFinalizationLimit(true), 1);
    QCOMPARE(ei::unitVisualFinalizationLimit(false), 8);
}

void UnitAnimationTests::ignoresIdleVisualProgressBeforeWorkStarts()
{
    // Exact load regression: 0 == 0 previously logged once per event-pumped
    // frame, producing multi-megabyte logs and turning a ~4 second MOB load
    // into a 30-40 second load.
    QVERIFY(!ei::shouldLogUnitVisualProgress(0, 0, -1));
    QVERIFY(ei::shouldLogUnitVisualProgress(402, 0, -1));
    QVERIFY(!ei::shouldLogUnitVisualProgress(402, 31, 0));
    QVERIFY(ei::shouldLogUnitVisualProgress(402, 32, 0));
    QVERIFY(ei::shouldLogUnitVisualProgress(402, 402, 384));
}

void UnitAnimationTests::keepsAppearanceCacheBesideEditor()
{
    const QString root = QDir::cleanPath(QStringLiteral("C:/portable/EI Mapper Endurance"));
    QCOMPARE(QDir::cleanPath(unitAppearancePreparationCacheDirectoryForApplication(root)),
             QDir(root).filePath(QStringLiteral("cache/unit-appearances")));
    QVERIFY(unitAppearancePreparationCacheDirectoryForApplication(QString()).isEmpty());
}

void UnitAnimationTests::rebuildsIncompatibleAppearanceCacheAutomatically()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    QFile staleManifest(QDir(cache.path()).filePath(QStringLiteral("cache-manifest.json")));
    QVERIFY(staleManifest.open(QIODevice::WriteOnly));
    staleManifest.write("{\"schema\":999}");
    staleManifest.close();
    QFile stalePayload(QDir(cache.path()).filePath(QStringLiteral("stale-entry.bin")));
    QVERIFY(stalePayload.open(QIODevice::WriteOnly));
    stalePayload.write("stale");
    stalePayload.close();

    SUnitAppearancePreparationTask task;
    task.unitKey = 7;
    task.providerId = QStringLiteral("jmv.v1");
    task.request.model = QStringLiteral("unhuma");
    task.request.primaryTexture = QStringLiteral("unhumaskin_01_x4");
    task.request.bodyParts = QStringList() << QStringLiteral("hd") << QStringLiteral("bd");
    task.request.dataSource = EUnitVisualDataSource::EmbeddedMob;
    const QVector<SUnitAppearancePreparationResult> result
        = resolveUnitAppearanceTasks({task}, cache.path());

    QCOMPARE(result.size(), 1);
    QVERIFY(result.first().providerResult.success);
    QVERIFY(!QFileInfo::exists(stalePayload.fileName()));
    QFile rebuiltManifest(QDir(cache.path()).filePath(QStringLiteral("cache-manifest.json")));
    QVERIFY(rebuiltManifest.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(rebuiltManifest.readAll()).object()
                 .value(QStringLiteral("schema")).toInt(), 1);
}

void UnitAnimationTests::parsesAndSelectsDeterministically()
{
    const auto actions = parseAdbActions(adb({{"late", 7, idlePacked(0), 100, 2},
                                              {"winner", 3, idlePacked(0), 100, 2}}));
    QCOMPARE(actions.size(), 2);
    SAdbAction selected; QVERIFY(selectDefaultIdleAction(actions, -1, false, &selected));
    QCOMPARE(selected.name, QString("winner"));
}

void UnitAnimationTests::providersSeparateBaseAndJmvMasks()
{
    QSharedPointer<ei::SProfileDatabaseSnapshot> db(new ei::SProfileDatabaseSnapshot);
    db->actionTables.insert("unhuma.adb", adb({{"cidle", 1, idlePacked(quint16(1 << 10), 2), 100, 2}}));
    ei::SItemDatabaseRecord weapon; weapon.name = "great"; weapon.category = "weapons"; weapon.typeId = 10;
    db->items.insert("great", weapon);
    ProfileVisualProviderService providers;
    SUnitAnimationRequest request; request.model = "unhuma"; request.weapons = QStringList() << "great";
    QJsonObject output;
    QVERIFY(!providers.animationResolver("base.standard.v1", db)->resolve(request, &output).success);
    QVERIFY(providers.animationResolver("jmv.v1", db)->resolve(request, &output).success);
    QCOMPARE(output.value("actionName").toString(), QString("cidle"));
}

void UnitAnimationTests::jmvSelectsStandingModelSpecificIdles()
{
    const QVector<SAdbAction> normal = parseAdbActions(adb({
        {"crest", 1, idlePacked(0, 1), 100, 10},
        {"cidle", 2, idlePacked(127, 2), 100, 10}
    }));
    SAdbAction selected;
    QVERIFY(selectJmvDefaultIdleAction(normal, "unmogo", -1, &selected));
    QCOMPARE(selected.name, QString("cidle"));

    const QVector<SAdbAction> lizard = parseAdbActions(adb({
        {"cidle01", 1, idlePacked(0, 2), 100, 10},
        {"cidle02", 2, idlePacked(0, 2), 50, 10}
    }));
    QVERIFY(selectJmvDefaultIdleAction(lizard, "unmoli", -1, &selected));
    QCOMPARE(selected.name, QString("cidle01"));

    const QVector<SAdbAction> twoHand = parseAdbActions(adb({
        {"cidle01", 1, idlePacked(121, 2), 100, 10},
        {"2hscidle01", 2, idlePacked(31, 2), 100, 10}
    }));
    QVERIFY(selectJmvDefaultIdleAction(twoHand, "unhufe", 12, &selected));
    QCOMPARE(selected.name, QString("2hscidle01"));
}

void UnitAnimationTests::jmvTitanGripUsesOrdinaryDualWieldIdle()
{
    QSharedPointer<ei::SProfileDatabaseSnapshot> db(new ei::SProfileDatabaseSnapshot);
    db->jmvUnitPerkTableStatus = ei::EJmvUnitPerkTableStatus::Valid;
    db->jmvStrongManByPrototype.insert("titan", 1);
    db->actionTables.insert("unhuma.adb", adb({
        {"cidle01", 80, idlePacked(121, 2), 100, 65},
        {"2hacidle01", 179, idlePacked(543, 2), 100, 65},
        {"2hscidle01", 180, idlePacked(31, 2), 100, 65}
    }));
    ei::SItemDatabaseRecord sword;
    sword.name = "great sword"; sword.category = "weapons"; sword.typeId = 12;
    db->items.insert(sword.name, sword);
    ei::SItemDatabaseRecord axe;
    axe.name = "great axe"; axe.category = "weapons"; axe.typeId = 13;
    db->items.insert(axe.name, axe);
    ei::SItemDatabaseRecord shield;
    shield.name = "shield"; shield.category = "weapons"; shield.typeId = 9;
    db->items.insert(shield.name, shield);

    ProfileVisualProviderService providers;
    const auto resolver = providers.animationResolver("jmv.v1", db);
    SUnitAnimationRequest request;
    request.prototype = "Titan";
    request.model = "unhuma";
    QJsonObject output;

    request.weapons = QStringList() << "great sword" << "shield";
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("actionName").toString(), QString("cidle01"));

    request.weapons = QStringList() << "great sword" << "great axe";
    output = QJsonObject();
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("actionName").toString(), QString("cidle01"));

    request.prototype = "Ordinary";
    output = QJsonObject();
    QVERIFY(resolver->resolve(request, &output).success);
    QCOMPARE(output.value("actionName").toString(), QString("2hscidle01"));
}

void UnitAnimationTests::anchorsRootTranslationToBindPose()
{
    SAnimationClip clip;
    clip.logicalLength = 2;
    SAnimationChannel channel;
    channel.component = "root";
    channel.rotations << QQuaternion() << QQuaternion() << QQuaternion();
    channel.translations << QVector3D(10, 0, 0) << QVector3D(12, 0, 0) << QVector3D(10, 0, 0);
    clip.channels.insert("root", channel);
    SAnimationSkeleton skeleton;
    skeleton.restOffsets.insert("root", QVector3D(5, 0, 0));
    const auto first = propagateAnimationHierarchy(interpolateAnimationPose(clip, 0), skeleton);
    QCOMPARE(first.value("root").column(3).x(), 0.0f);
    const auto moved = propagateAnimationHierarchy(interpolateAnimationPose(clip, 1), skeleton);
    QCOMPARE(moved.value("root").column(3).x(), 2.0f);
}

void UnitAnimationTests::interpolatesMorphClosingSample()
{
    const SAnimationClip clip = parseNestedAnimationClip(
        res({{"idle", res({{"wing", morphChannel()}})}}), "idle", 2);
    QVERIFY2(clip.valid(), qPrintable(clip.diagnostics.join(';')));
    const SAnimationLocalPose pose = interpolateAnimationPose(clip, 1.5);
    QCOMPARE(pose.morphDeltas.value("wing").size(), 1);
    QCOMPARE(pose.morphDeltas.value("wing").first().x(), 1.0f);
}

void UnitAnimationTests::filtersPoseToRenderedSkeleton()
{
    SAnimationClip clip;
    clip.logicalLength = 2;
    SAnimationChannel visible;
    visible.component = "bd";
    visible.rotations << QQuaternion() << QQuaternion();
    visible.translations << QVector3D() << QVector3D(1, 0, 0);
    clip.channels.insert(visible.component, visible);
    SAnimationChannel hidden = visible;
    hidden.component = "unused.armor99";
    clip.channels.insert(hidden.component, hidden);
    SAnimationSkeleton skeleton;
    skeleton.restOffsets.insert("bd", QVector3D());

    const SAnimationLocalPose pose = interpolateAnimationPose(clip, 0.5, &skeleton);
    QVERIFY(pose.transforms.contains("bd"));
    QVERIFY(!pose.transforms.contains("unused.armor99"));
}

void UnitAnimationTests::parsesNestedClipAndClosingSample()
{
    const QByteArray archive = res({{"idle", res({{"bd", channel()}})}});
    const SAnimationClip clip = parseNestedAnimationClip(archive, "idle", 2);
    QVERIFY2(clip.valid(), qPrintable(clip.diagnostics.join(';')));
    const auto pose = interpolateAnimationPose(clip, 1.5);
    QCOMPARE(pose.transforms.value("bd").column(3).x(), 1.0f);
}

void UnitAnimationTests::propagatesPoseToNumberedEquipment()
{
    SAnimationSkeleton skeleton;
    skeleton.restOffsets.insert("rh3.sword12", QVector3D(2, 0, 0));
    SAnimationLocalPose pose; QMatrix4x4 animated; animated.translate(3, 0, 0);
    pose.transforms.insert("rh3.sword00", animated);
    const auto transforms = propagateAnimationHierarchy(pose, skeleton);
    QCOMPARE(transforms.value("rh3.sword12").column(3).x(), 3.0f);
}

void UnitAnimationTests::inheritsAttachmentRotationOnlyOnce()
{
    SAnimationSkeleton skeleton;
    skeleton.restOffsets.insert("lh2", QVector3D());
    skeleton.restOffsets.insert("lh2.shield00", QVector3D());
    skeleton.restOffsets.insert("lh2.shield04", QVector3D());
    skeleton.parents.insert("lh2.shield00", "lh2");
    skeleton.parents.insert("lh2.shield04", "lh2.shield00");
    SAnimationLocalPose pose;
    QMatrix4x4 attachment;
    attachment.rotate(90, 0, 0, 1);
    pose.transforms.insert("lh2.shield00", attachment);

    const auto transforms = propagateAnimationHierarchy(pose, skeleton);
    const QVector3D rotated = transforms.value("lh2.shield04") * QVector3D(1, 0, 0);
    QVERIFY(qAbs(rotated.x()) < 0.0001f);
    QVERIFY(qAbs(rotated.y() - 1.0f) < 0.0001f);
}

void UnitAnimationTests::preservesChildBindOffsetAndIgnoresChildTranslation()
{
    SAnimationSkeleton skeleton;
    skeleton.restOffsets.insert("root", QVector3D());
    skeleton.restOffsets.insert("child", QVector3D(2, 0, 0));
    skeleton.parents.insert("child", "root");
    SAnimationLocalPose pose;
    QMatrix4x4 root;
    root.rotate(90, 0, 0, 1);
    pose.transforms.insert("root", root);
    QMatrix4x4 child;
    child.translate(99, 0, 0);
    pose.transforms.insert("child", child);
    const auto transforms = propagateAnimationHierarchy(pose, skeleton);
    const QVector3D animatedJoint = transforms.value("child") * QVector3D(2, 0, 0);
    QVERIFY(qAbs(animatedJoint.x()) < 0.0001f);
    QVERIFY(qAbs(animatedJoint.y() - 2.0f) < 0.0001f);
}

void UnitAnimationTests::preMultipliesChildRotationInEiOrder()
{
    SAnimationSkeleton skeleton;
    skeleton.restOffsets.insert("root", QVector3D());
    skeleton.restOffsets.insert("child", QVector3D());
    skeleton.parents.insert("child", "root");
    const QQuaternion parentRotation = QQuaternion::fromAxisAndAngle(0, 0, 1, 90);
    const QQuaternion childRotation = QQuaternion::fromAxisAndAngle(1, 0, 0, 90);
    SAnimationLocalPose pose;
    QMatrix4x4 parent; parent.rotate(parentRotation); pose.transforms.insert("root", parent);
    QMatrix4x4 child; child.rotate(childRotation); pose.transforms.insert("child", child);
    const auto transforms = propagateAnimationHierarchy(pose, skeleton);
    const QVector3D actual = transforms.value("child") * QVector3D(0, 1, 0);
    const QVector3D expected = (childRotation * parentRotation).rotatedVector(QVector3D(0, 1, 0));
    QVERIFY((actual - expected).length() < 0.0001f);
}

void UnitAnimationTests::appliesDistanceTiers()
{
    QCOMPARE(animationDistanceTier(false, false, 0), EAnimationDistanceTier::Skip);
    QCOMPARE(animationDistanceTier(true, true, 999), EAnimationDistanceTier::Full);
    QCOMPARE(animationDistanceTier(true, false, 100), EAnimationDistanceTier::Full);
    QCOMPARE(animationDistanceTier(true, false, 101), EAnimationDistanceTier::HalfRate);
    QCOMPARE(animationDistanceTier(true, false, 301), EAnimationDistanceTier::LowRate);
    QCOMPARE(quantizedAnimationFrame(EAnimationDistanceTier::HalfRate, 100, 10, 0), 0.0);
    QCOMPARE(quantizedAnimationFrame(EAnimationDistanceTier::LowRate, 100, 10, 0), 0.0);
    QCOMPARE(quantizedAnimationFrame(EAnimationDistanceTier::LowRate, 300, 10, 0), 4.0);
}

void UnitAnimationTests::boundsColdVisualResolutionDuringCameraMovement()
{
    AnimationFrameContext& frame = AnimationFrameContext::instance();
    frame.beginFrame(QVector3D(), 0, true, true);
    QVERIFY(frame.cameraMoving());
    QVERIFY(!frame.claimColdVisualResolution());

    frame.beginFrame(QVector3D(), 16, true, false);
    QVERIFY(!frame.cameraMoving());
    QVERIFY(frame.claimColdVisualResolution());
    QVERIFY(!frame.claimColdVisualResolution());

    frame.beginFrame(QVector3D(), 32, true, false);
    QVERIFY(frame.claimColdVisualResolution());
}

void UnitAnimationTests::resolvesAppearancePreparationWithoutSceneObjects()
{
    SUnitAppearancePreparationTask task;
    task.unitKey = 42;
    task.databaseGeneration = 7;
    task.visualRevision = 3;
    task.providerId = "jmv.v1";
    task.request.model = "unhuma";
    task.request.primaryTexture = "unhumaskin_01_x4";
    task.request.bodyParts = QStringList() << "hd" << "bd";
    task.request.dataSource = EUnitVisualDataSource::EmbeddedMob;
    const QVector<SUnitAppearancePreparationResult> results
        = resolveUnitAppearanceTasks({task});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().unitKey, quintptr(42));
    QCOMPARE(results.first().databaseGeneration, quint64(7));
    QCOMPARE(results.first().visualRevision, quint64(3));
    QVERIFY(results.first().providerResult.success);
    QCOMPARE(results.first().appearance.value("model").toString(), QString("unhuma"));
}

void UnitAnimationTests::cachesPreparedAppearanceByContentIdentity()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    QSharedPointer<ei::SProfileDatabaseSnapshot> database(new ei::SProfileDatabaseSnapshot);
    database->cacheKey = QStringLiteral("database-content-a");
    SUnitAppearancePreparationTask task;
    task.unitKey = 1;
    task.databaseGeneration = 1;
    task.visualRevision = 1;
    task.providerId = QStringLiteral("jmv.v1");
    task.database = database;
    task.request.model = QStringLiteral("unhuma");
    task.request.primaryTexture = QStringLiteral("unhumaskin_01_x4");
    task.request.bodyParts = QStringList() << QStringLiteral("hd") << QStringLiteral("bd");
    task.request.dataSource = EUnitVisualDataSource::EmbeddedMob;

    const QVector<SUnitAppearancePreparationResult> first
        = resolveUnitAppearanceTasks({task}, cache.path());
    QCOMPARE(first.size(), 1);
    QVERIFY(first.first().providerResult.success);
    QVERIFY(!first.first().cacheHit);

    task.unitKey = 2;
    task.databaseGeneration = 99;
    task.visualRevision = 8;
    const QVector<SUnitAppearancePreparationResult> second
        = resolveUnitAppearanceTasks({task}, cache.path());
    QCOMPARE(second.size(), 1);
    QVERIFY(second.first().cacheHit);
    QCOMPARE(second.first().unitKey, quintptr(2));
    QCOMPARE(second.first().databaseGeneration, quint64(99));
    QCOMPARE(second.first().appearance, first.first().appearance);

    database->cacheKey = QStringLiteral("database-content-b");
    const QVector<SUnitAppearancePreparationResult> changedDatabase
        = resolveUnitAppearanceTasks({task}, cache.path());
    QCOMPARE(changedDatabase.size(), 1);
    QVERIFY(!changedDatabase.first().cacheHit);

    task.request.primaryTexture = QStringLiteral("unhumaskin_02_x4");
    const QVector<SUnitAppearancePreparationResult> changedUnit
        = resolveUnitAppearanceTasks({task}, cache.path());
    QCOMPARE(changedUnit.size(), 1);
    QVERIFY(!changedUnit.first().cacheHit);
}

void UnitAnimationTests::sharesDatabaseAppearancePreparationAcrossEquivalentUnits()
{
    QTemporaryDir cache;
    QVERIFY(cache.isValid());
    QSharedPointer<ei::SProfileDatabaseSnapshot> database(new ei::SProfileDatabaseSnapshot);
    database->cacheKey = QStringLiteral("database-content-shared");
    ei::SRaceModelDatabaseRecord race;
    race.name = QStringLiteral("Human Male");
    race.model = QStringLiteral("unhuma");
    race.primaryTextures << QStringLiteral("unhumaskin_00");
    database->raceModels.insert(race.name.toLower(), race);
    ei::SMonsterDatabaseRecord monster;
    monster.name = QStringLiteral("997569");
    monster.baseRace = race.name;
    monster.skinIndex = 0;
    database->monsters.insert(monster.name.toLower(), monster);

    SUnitAppearancePreparationTask first;
    first.unitKey = 101;
    first.databaseGeneration = 7;
    first.visualRevision = 3;
    first.providerId = QStringLiteral("jmv.v1");
    first.database = database;
    first.request.profileId = QStringLiteral("jmv");
    first.request.runtimeManifestId = QStringLiteral("jmv-runtime");
    first.request.prototype = QStringLiteral("997569");
    first.request.parentTemplate = QStringLiteral("stale-parent-a");
    first.request.model = QStringLiteral("stale-model-a");
    first.request.primaryTexture = QStringLiteral("stale-texture-a");
    first.request.bodyParts = QStringList() << QStringLiteral("stale-part-a");
    first.request.dataSource = EUnitVisualDataSource::ProfileDatabase;

    SUnitAppearancePreparationTask second = first;
    second.unitKey = 202;
    second.databaseGeneration = 8;
    second.visualRevision = 4;

    QCOMPARE(unitAppearancePreparationCacheKey(first),
             unitAppearancePreparationCacheKey(second));
    const QVector<SUnitAppearancePreparationResult> shared
        = resolveUnitAppearanceTasks({first, second}, QStringLiteral(""));
    QCOMPARE(shared.size(), 2);
    QVERIFY(shared.at(0).providerResult.success);
    QCOMPARE(shared.at(1).appearance, shared.at(0).appearance);
    QVERIFY(shared.at(1).sharedPreparation);
    QCOMPARE(shared.at(1).unitKey, quintptr(202));
    QCOMPARE(shared.at(1).databaseGeneration, quint64(8));
    QCOMPARE(shared.at(1).visualRevision, quint64(4));
    QVERIFY(shared.at(0).providerExecuted);
    QVERIFY(!shared.at(0).cacheHit);
    QVERIFY(!shared.at(0).sharedPreparation);
    QVERIFY(!shared.at(1).providerExecuted);
    QVERIFY(!shared.at(1).cacheHit);

    second.request.model = QStringLiteral("different-authored-assembly");
    QVERIFY(unitAppearancePreparationCacheKey(first)
            != unitAppearancePreparationCacheKey(second));
}

void UnitAnimationTests::groupsAllEquivalentUnitsAcrossDistanceOrderedWork()
{
    QVector<SUnitAppearancePreparationTask> ordered;
    for(int index = 0; index < 32; ++index)
    {
        SUnitAppearancePreparationTask peasant;
        peasant.unitKey = quintptr(1000 + index);
        peasant.providerId = QStringLiteral("jmv.v1");
        peasant.request.prototype = QStringLiteral("Peasant");
        peasant.request.model = QStringLiteral("unhuma");
        peasant.request.primaryTexture = QStringLiteral("unhumaskin_00_x4");
        ordered.append(peasant);

        SUnitAppearancePreparationTask unique = peasant;
        unique.unitKey = quintptr(2000 + index);
        unique.request.prototype = QStringLiteral("Unique %1").arg(index);
        ordered.append(unique);
    }

    const QVector<SUnitAppearancePreparationTask> selected
        = selectUnitAppearancePreparationBatch(ordered, 16);
    int peasants = 0;
    QSet<QString> uniqueKeys;
    for(const SUnitAppearancePreparationTask& task : selected)
    {
        if(task.request.prototype == QStringLiteral("Peasant"))
            ++peasants;
        uniqueKeys.insert(unitAppearancePreparationCacheKey(task));
    }
    QCOMPARE(peasants, 32);
    QCOMPARE(uniqueKeys.size(), 16);
    QCOMPARE(selected.size(), 47);
}

void UnitAnimationTests::keepsDistinctAssembliesInSeparatePreparationGroups()
{
    SUnitAppearancePreparationTask ordinary;
    ordinary.unitKey = 1;
    ordinary.providerId = QStringLiteral("jmv.v1");
    ordinary.request.prototype = QStringLiteral("Shared prototype");
    ordinary.request.model = QStringLiteral("unhuma");
    ordinary.request.primaryTexture = QStringLiteral("skin");
    SUnitAppearancePreparationTask authored = ordinary;
    authored.unitKey = 2;
    authored.request.model = QStringLiteral("special-authored-assembly");

    const QVector<SUnitAppearancePreparationTask> selected
        = selectUnitAppearancePreparationBatch({ordinary, authored}, 1);
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().unitKey, quintptr(1));
}

void UnitAnimationTests::boundsAppearancePreparationWorkerCount()
{
    QCOMPARE(unitAppearancePreparationWorkerLimit(0), 1);
    QCOMPARE(unitAppearancePreparationWorkerLimit(1), 1);
    QCOMPARE(unitAppearancePreparationWorkerLimit(2), 2);
    QCOMPARE(unitAppearancePreparationWorkerLimit(8), 4);
    QCOMPARE(unitAppearancePreparationWorkerLimit(64), 4);
}

void UnitAnimationTests::keepsAppearanceProgressVisibleBetweenWorkerBatches()
{
    QVERIFY(ei::shouldShowUnitAppearanceProgress(true, 402, 16));
    QVERIFY(ei::shouldShowUnitAppearanceProgress(true, 402, 173));
    QVERIFY(!ei::shouldShowUnitAppearanceProgress(false, 402, 173));
    QVERIFY(!ei::shouldShowUnitAppearanceProgress(true, 402, 402));
    QCOMPARE(ei::completedUnitVisualCount(402, 402), 0);
    QCOMPARE(ei::completedUnitVisualCount(402, 401), 1);
    QCOMPARE(ei::completedUnitVisualCount(402, 0), 402);
}

void UnitAnimationTests::malformedAndUnknownInputsFailStatic()
{
    QStringList diagnostics;
    QVERIFY(parseAdbActions(QByteArray("bad"), &diagnostics).isEmpty());
    QVERIFY(!diagnostics.isEmpty());
    const SAnimationClip clip = parseNestedAnimationClip(res({{"other", QByteArray("bad")}}), "idle", 2);
    QVERIFY(!clip.valid());
    QString providerDiagnostic;
    ProfileVisualProviderService providers;
    QVERIFY(providers.animationResolver("missing.mod.v1", {}, &providerDiagnostic).isNull());
    QVERIFY(providerDiagnostic.contains("no fallback", Qt::CaseInsensitive));
}

QTEST_APPLESS_MAIN(UnitAnimationTests)
#include "unit_animation_tests.moc"

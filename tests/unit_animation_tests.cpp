#include <QtTest>
#include <QDataStream>
#include <QTemporaryDir>
#include "unit_animation.h"
#include "profile_visual_provider.h"
#include "unit_visual_preparation.h"

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
    void malformedAndUnknownInputsFailStatic();
};

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

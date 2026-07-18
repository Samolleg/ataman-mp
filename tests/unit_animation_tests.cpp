#include <QtTest>
#include <QDataStream>
#include "unit_animation.h"
#include "profile_visual_provider.h"

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

}

class UnitAnimationTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesAndSelectsDeterministically();
    void providersSeparateBaseAndJmvMasks();
    void parsesNestedClipAndClosingSample();
    void propagatesPoseToNumberedEquipment();
    void preservesChildBindOffsetAndIgnoresChildTranslation();
    void appliesDistanceTiers();
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
    db->actionTables.insert("unhuma.adb", adb({{"extended", 1, idlePacked(quint16(1 << 10)), 100, 2}}));
    ei::SItemDatabaseRecord weapon; weapon.name = "great"; weapon.category = "weapons"; weapon.typeId = 10;
    db->items.insert("great", weapon);
    ProfileVisualProviderService providers;
    SUnitAnimationRequest request; request.model = "unhuma"; request.weapons = QStringList() << "great";
    QJsonObject output;
    QVERIFY(!providers.animationResolver("base.standard.v1", db)->resolve(request, &output).success);
    QVERIFY(providers.animationResolver("jmv.v1", db)->resolve(request, &output).success);
    QCOMPARE(output.value("actionName").toString(), QString("extended"));
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
    QCOMPARE(transforms.value("rh3.sword12").column(3).x(), 1.0f);
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

void UnitAnimationTests::appliesDistanceTiers()
{
    QCOMPARE(animationDistanceTier(false, false, 0), EAnimationDistanceTier::Skip);
    QCOMPARE(animationDistanceTier(true, true, 999), EAnimationDistanceTier::Full);
    QCOMPARE(animationDistanceTier(true, false, 100), EAnimationDistanceTier::Full);
    QCOMPARE(animationDistanceTier(true, false, 101), EAnimationDistanceTier::HalfRate);
    QCOMPARE(animationDistanceTier(true, false, 301), EAnimationDistanceTier::FirstPose);
    QCOMPARE(quantizedAnimationFrame(EAnimationDistanceTier::HalfRate, 100, 10, 0), 0.0);
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

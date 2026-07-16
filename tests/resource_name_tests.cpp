#include <QtTest>

#include "../resource_name.h"
#include "../unit_appearance.h"

#include <QFile>

class ResourceNameTests : public QObject
{
    Q_OBJECT

private slots:
    void preservesLiteralName();
    void resolvesSkeletonX4Name();
    void fallsBackWithinMissingSkeletonFamily();
    void keepsModelFamiliesInSeparateCacheEntries();
    void acceptsMmpExtensionWithoutDuplicatingIt();
    void buildsCompleteTerrainAtlasNames();
    void resolvesDatabaseHumanoidSkin();
    void ignoresNonHumanoidDatabaseSkin();
    void resolvesLiveBaseDatabaseWhenProvided();
};

namespace {

void appendDword(QByteArray& data, quint32 value)
{
    data.append(char(value & 0xff));
    data.append(char((value >> 8) & 0xff));
    data.append(char((value >> 16) & 0xff));
    data.append(char((value >> 24) & 0xff));
}

void appendStringField(QByteArray& data, quint8 field, const QByteArray& value)
{
    const QByteArray terminated = value + '\0';
    data.append(char(field));
    data.append(char(terminated.size() << 1));
    data.append(terminated);
}

QByteArray monsterRecord(const QByteArray& name, const QByteArray& race, quint32 skin)
{
    QByteArray payload;
    appendStringField(payload, 0, name);
    appendStringField(payload, 1, race);
    payload.append(char(2));
    payload.append(char(8));
    appendDword(payload, quint32(-1));
    payload.append(char(3));
    payload.append(char(8));
    appendDword(payload, skin);

    QByteArray result;
    result.append(char(1));
    appendDword(result, quint32((payload.size() << 1) | 1));
    result.append(payload);
    return result;
}

}

void ResourceNameTests::preservesLiteralName()
{
    QCOMPARE(ei::textureResourceCandidates("stone04", "nast7"), QStringList({"stone04.mmp"}));
}

void ResourceNameTests::resolvesSkeletonX4Name()
{
    const QStringList candidates = ei::textureResourceCandidates("unskskin_02_x4", "unhusk");
    QCOMPARE(candidates, QStringList({
        "unskskin_02_x4.mmp",
        "unhuskskin_02.x4.mmp",
        "unskskin_02.x4.mmp",
        "unhuskskin_01.x4.mmp",
        "unhuskskin_00.x4.mmp"
    }));
}

void ResourceNameTests::fallsBackWithinMissingSkeletonFamily()
{
    const QStringList candidates = ei::textureResourceCandidates("unskskin_04_x4", "unhusk");
    QCOMPARE(candidates.at(0), QString("unskskin_04_x4.mmp"));
    QCOMPARE(candidates.at(1), QString("unhuskskin_04.x4.mmp"));
    QCOMPARE(candidates.at(2), QString("unskskin_04.x4.mmp"));
    QCOMPARE(candidates.at(3), QString("unhuskskin_03.x4.mmp"));
}

void ResourceNameTests::keepsModelFamiliesInSeparateCacheEntries()
{
    QCOMPARE(ei::textureCacheKey("unmaskin_02_x4", "unhuma"), QString("unhu|unmaskin_02_x4"));
    QCOMPARE(ei::textureCacheKey("unmaskin_02_x4", "unorma"), QString("unor|unmaskin_02_x4"));
    QVERIFY(ei::textureCacheKey("unmaskin_02_x4", "unhuma")
            != ei::textureCacheKey("unmaskin_02_x4", "unorma"));
}

void ResourceNameTests::acceptsMmpExtensionWithoutDuplicatingIt()
{
    QCOMPARE(ei::textureResourceCandidates("Skeleton00.MMP"), QStringList({"skeleton00.mmp"}));
}

void ResourceNameTests::buildsCompleteTerrainAtlasNames()
{
    QCOMPARE(ei::terrainTextureResourceNames("Zone10.MP", 8), QStringList({
        "zone10000.mmp", "zone10001.mmp", "zone10002.mmp", "zone10003.mmp",
        "zone10004.mmp", "zone10005.mmp", "zone10006.mmp", "zone10007.mmp"
    }));
    QVERIFY(ei::terrainTextureResourceNames("zone10", 0).isEmpty());
}

void ResourceNameTests::resolvesDatabaseHumanoidSkin()
{
    QByteArray units = monsterRecord("OrcMaleF3", "Orc Male", 0);
    units += monsterRecord("Human Gipath Brigand Boots F1", "Human Male", 4);
    const QMap<QString, int> skins = ei::unitPrototypeSkinIndices(units);
    QCOMPARE(skins.value("orcmalef3"), 0);
    QCOMPARE(skins.value("human gipath brigand boots f1"), 4);
    QCOMPARE(ei::humanoidTextureResourceName(skins, "OrcMaleF3", "unorma"),
             QString("unormaskin_00.mmp"));
    QCOMPARE(ei::humanoidTextureResourceName(skins, "Human Gipath Brigand Boots F1", "unhuma"),
             QString("unhumaskin_04.mmp"));
}

void ResourceNameTests::ignoresNonHumanoidDatabaseSkin()
{
    const QMap<QString, int> skins = ei::unitPrototypeSkinIndices(
                monsterRecord("WolfWhiteF4", "Wolf", 1));
    QVERIFY(ei::humanoidTextureResourceName(skins, "WolfWhiteF4", "unanwiwo").isEmpty());
    QVERIFY(ei::humanoidTextureResourceName(skins, "Missing", "unhuma").isEmpty());
}

void ResourceNameTests::resolvesLiveBaseDatabaseWhenProvided()
{
    const QString path = QString::fromLocal8Bit(qgetenv("EI_TEST_UNITS_UDB"));
    if(path.isEmpty())
        return;
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QMap<QString, int> skins = ei::unitPrototypeSkinIndices(file.readAll());
    const QString prototype = QString::fromLocal8Bit(qgetenv("EI_TEST_UNIT_PROTOTYPE"));
    const QString expected = QString::fromLocal8Bit(qgetenv("EI_TEST_UNIT_TEXTURE"));
    if(!prototype.isEmpty() && !expected.isEmpty())
    {
        QCOMPARE(ei::humanoidTextureResourceName(skins, prototype, "unorma"), expected);
        return;
    }
    QCOMPARE(ei::humanoidTextureResourceName(skins, "OrcMaleF3", "unorma"),
             QString("unormaskin_00.mmp"));
    QCOMPARE(ei::humanoidTextureResourceName(skins, "Human Gipath Brigand Boots F1", "unhuma"),
             QString("unhumaskin_04.mmp"));
}

QTEST_APPLESS_MAIN(ResourceNameTests)

#include "resource_name_tests.moc"

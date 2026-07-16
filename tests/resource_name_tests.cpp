#include <QtTest>

#include "../resource_name.h"

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
};

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

QTEST_APPLESS_MAIN(ResourceNameTests)

#include "resource_name_tests.moc"

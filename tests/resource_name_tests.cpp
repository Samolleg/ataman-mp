#include <QtTest>

#include "resource_name.h"
#include "mmp_codec.h"
#include "unit_appearance.h"

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
    void derivesEquipmentAtlasUvScale();
    void resolvesDatabaseHumanoidSkin();
    void ignoresNonHumanoidDatabaseSkin();
    void resolvesLiveBaseDatabaseWhenProvided();
    void decodesSparsePnt3WithTrailingTransparency();
    void decodesLargePnt3Run();
    void rejectsNonAlignedPnt3Run();
    void rejectsOutOfBoundsPnt3Run();
    void rejectsUnalignedPnt3Payload();
    void preservesPnt3RowMajorOrientation();
    void decodesDirectDraw5551DescriptorBeforePixels();
    void compositesTransparentArmorOverBodyAtlas();
    void compositesPremultipliedPnt3LikeTheGame();
    void keepsFaceAndHairOnSkinAtlas();
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

void ResourceNameTests::derivesEquipmentAtlasUvScale()
{
    QCOMPARE(ei::textureUvScale("UnhumaHL_05.bl.2.x2.mmp"), 2.0f);
    // The suffix is not a universal multiplier: JMV body/skin x4 atlases use
    // their stored FIG UVs directly. Corpus and runtime evidence only establish
    // the packed helmet/weapon/shield x2 transform.
    QCOMPARE(ei::textureUvScale("UnhumaPL_05.bl.0.x4.mmp"), 1.0f);
    QCOMPARE(ei::textureUvScale("unskskin_02_x4"), 1.0f);
    QCOMPARE(ei::textureUvScale("unhumaskin_04.mmp"), 1.0f);
    // Packed humanoid dclub02 occupies one atlas quadrant; the otherwise
    // identical integral ogre club02 has already-expanded full-atlas UVs.
    QCOMPARE(ei::textureUvScaleForBounds("UnhumaTH_02.mr.0.x2.mmp",
        0.009657f, 0.492210f, 0.509374f, 0.993647f), 2.0f);
    QCOMPARE(ei::textureUvScaleForBounds("UnhumaTH_02.mr.0.x2.mmp",
        0.019314f, 0.984419f, 0.018749f, 0.987295f), 1.0f);
}

void ResourceNameTests::decodesSparsePnt3WithTrailingTransparency()
{
    QByteArray compressed;
    appendDword(compressed, 16); // leading transparent bytes
    compressed.append("\x11\x22\x33\x44", 4); // final literal BGRA pixel
    QByteArray decoded;
    QString diagnostic;
    QVERIFY2(ei::decodePnt3(compressed, 24, &decoded, &diagnostic), qPrintable(diagnostic));
    QCOMPARE(decoded.size(), 24);
    QCOMPARE(decoded.mid(16, 4), QByteArray("\x11\x22\x33\x44", 4));
    QCOMPARE(decoded.left(16), QByteArray(16, '\0'));
    QCOMPARE(decoded.right(4), QByteArray(4, '\0'));
}

void ResourceNameTests::rejectsOutOfBoundsPnt3Run()
{
    QByteArray compressed;
    appendDword(compressed, 32);
    QByteArray decoded;
    QString diagnostic;
    QVERIFY(!ei::decodePnt3(compressed, 16, &decoded, &diagnostic));
    QVERIFY(!diagnostic.isEmpty());
    QVERIFY(decoded.isEmpty());
}

void ResourceNameTests::decodesLargePnt3Run()
{
    QByteArray compressed;
    appendDword(compressed, 3245872); // real ID 203 plate run, above rejected heuristic
    compressed.append("\x11\x22\x33\xff", 4);
    QByteArray decoded;
    QString diagnostic;
    QVERIFY2(ei::decodePnt3(compressed, 3245876, &decoded, &diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(decoded.left(3245872), QByteArray(3245872, '\0'));
    QCOMPARE(decoded.right(4), QByteArray("\x11\x22\x33\xff", 4));
}

void ResourceNameTests::rejectsNonAlignedPnt3Run()
{
    QByteArray compressed;
    appendDword(compressed, 20);
    QByteArray decoded;
    QString diagnostic;
    QVERIFY(!ei::decodePnt3(compressed, 24, &decoded, &diagnostic));
    QVERIFY(diagnostic.contains("four-pixel aligned"));
}

void ResourceNameTests::rejectsUnalignedPnt3Payload()
{
    QByteArray decoded;
    QString diagnostic;
    QVERIFY(!ei::decodePnt3(QByteArray("abc", 3), 16, &decoded, &diagnostic));
    QVERIFY(!diagnostic.isEmpty());
    QVERIFY(decoded.isEmpty());
}

void ResourceNameTests::preservesPnt3RowMajorOrientation()
{
    QByteArray compressed;
    compressed.append("\x1e\x14\x0a\xff", 4);
    compressed.append("\x3c\x32\x28\xff", 4);
    compressed.append("\x5a\x50\x46\xff", 4);
    compressed.append("\x78\x6e\x64\xff", 4);
    QImage image;
    QString diagnostic;
    QVERIFY2(ei::decodePnt3Image(compressed, 2, 2, &image, &diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(image.pixelColor(0, 0), QColor(10, 20, 30, 255));
    QCOMPARE(image.pixelColor(1, 0), QColor(40, 50, 60, 255));
    QCOMPARE(image.pixelColor(0, 1), QColor(70, 80, 90, 255));
    QCOMPARE(image.pixelColor(1, 1), QColor(100, 110, 120, 255));
}

void ResourceNameTests::decodesDirectDraw5551DescriptorBeforePixels()
{
    QByteArray data;
    appendDword(data, 16);
    appendDword(data, 0x8000); appendDword(data, 15); appendDword(data, 1);
    appendDword(data, 0x7c00); appendDword(data, 10); appendDword(data, 5);
    appendDword(data, 0x03e0); appendDword(data, 5); appendDword(data, 5);
    appendDword(data, 0x001f); appendDword(data, 0); appendDword(data, 5);
    appendDword(data, 0); // reserved dword completing the 56-byte descriptor
    data.append(char(0x00)); data.append(char(0xfc)); // opaque red
    data.append(char(0x1f)); data.append(char(0x00)); // transparent blue
    QImage image;
    QString diagnostic;
    QVERIFY2(ei::decodeDirectDrawImage(data, 2, 1, &image, &diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0, 255));
    QCOMPARE(image.pixelColor(1, 0), QColor(0, 0, 255, 0));
}

void ResourceNameTests::compositesTransparentArmorOverBodyAtlas()
{
    QImage body(2, 1, QImage::Format_ARGB32);
    body.fill(QColor(200, 100, 50, 255));
    QImage armor(2, 1, QImage::Format_ARGB32);
    armor.fill(Qt::transparent);
    armor.setPixelColor(1, 0, QColor(20, 40, 200, 255));
    QImage composite;
    QString diagnostic;
    QVERIFY2(ei::compositeTextureLayers({body, armor}, &composite, &diagnostic),
             qPrintable(diagnostic));
    // The original MMX blend uses (destination * (255 - alpha)) >> 8,
    // including for a literal transparent pixel.
    const uchar* untouched = composite.constScanLine(0);
    QCOMPARE(untouched[0], uchar(49));
    QCOMPARE(untouched[1], uchar(99));
    QCOMPARE(untouched[2], uchar(199));
    QCOMPARE(untouched[3], uchar(254));
    QCOMPARE(composite.pixelColor(1, 0), armor.pixelColor(1, 0));
}

void ResourceNameTests::compositesPremultipliedPnt3LikeTheGame()
{
    QImage body(1, 1, QImage::Format_ARGB32_Premultiplied);
    body.fill(QColor(80, 40, 20, 255));
    QImage armor(1, 1, QImage::Format_ARGB32_Premultiplied);
    // A=128, premultiplied BGRA=(25, 50, 100, 128).
    uchar* pixel = armor.scanLine(0);
    pixel[0] = 25;
    pixel[1] = 50;
    pixel[2] = 100;
    pixel[3] = 128;
    QImage composite;
    QString diagnostic;
    QVERIFY2(ei::compositeTextureLayers({body, armor}, &composite, &diagnostic),
             qPrintable(diagnostic));
    const uchar* result = composite.constScanLine(0);
    QCOMPARE(result[0], uchar(34));  // 25 + ((20 * 127) >> 8)
    QCOMPARE(result[1], uchar(69));  // 50 + ((40 * 127) >> 8)
    QCOMPARE(result[2], uchar(139)); // 100 + ((80 * 127) >> 8)
    QCOMPARE(result[3], uchar(254)); // 128 + ((255 * 127) >> 8)
}

void ResourceNameTests::keepsFaceAndHairOnSkinAtlas()
{
    QVERIFY(ei::usesUnlayeredHumanoidTexture("hd"));
    QVERIFY(ei::usesUnlayeredHumanoidTexture("HR.03"));
    QVERIFY(!ei::usesUnlayeredHumanoidTexture("hd.armor05"));
    QVERIFY(!ei::usesUnlayeredHumanoidTexture("bd.armor05"));
}

QTEST_APPLESS_MAIN(ResourceNameTests)

#include "resource_name_tests.moc"

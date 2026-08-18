#include <QtTest>

#include "resource_name.h"
#include "mmp_codec.h"
#include "unit_appearance.h"

#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>

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
    void normalizesStandaloneQuickItemAtlasCell();
    void resolvesDatabaseHumanoidSkin();
    void ignoresNonHumanoidDatabaseSkin();
    void resolvesLiveBaseDatabaseWhenProvided();
    void decodesSparsePnt3WithTrailingTransparency();
    void decodesLargePnt3Run();
    void preservesZeroAlphaPnt3PixelsInsideLiteralBlock();
    void rejectsNonAlignedPnt3Run();
    void rejectsOutOfBoundsPnt3Run();
    void rejectsUnalignedPnt3Payload();
    void preservesPnt3RowMajorOrientation();
    void decodesDirectDraw5551DescriptorBeforePixels();
    void decodesDxt1Transparency();
    void decodesDxt3ExplicitAlpha();
    void decodesDxt5InterpolatedAlpha();
    void rejectsTruncatedDxtMip();
    void expandedTerrainDxtDecodeStaysWithinInteractiveBudget();
    void compositesTransparentArmorOverBodyAtlas();
    void compositesPremultipliedPnt3LikeTheGame();
    void keepsFaceAndHairOnSkinAtlas();
    void cachesResourceMetadataUntilExplicitRefresh();
    void honorsConfigIniResourcePrecedence();
};

void ResourceNameTests::honorsConfigIniResourcePrecedence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkpath("mod/res"));
    QVERIFY(root.mkpath("base/Res"));
    const QString modRoot = root.filePath("mod");
    const QString modResources = root.filePath("mod/res");
    const QString baseResources = root.filePath("base/Res");
    for(const QString& path : {
            QDir(modResources).filePath("figures.res"),
            QDir(modResources).filePath("HDPack_Mapobjects.res"),
            QDir(modResources).filePath("unlisted.res"),
            QDir(baseResources).filePath("figures.res")})
    {
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        archive.write("fixture");
    }
    QFile config(QDir(modRoot).filePath("config.ini"));
    QVERIFY(config.open(QIODevice::WriteOnly));
    config.write("[MOD]\r\nTitle=test\r\n[RES]\r\n"
                 "figures.res=res\\HDPack_Mapobjects.res\r\n"
                 "figures.res=res\\figures.res\r\n");
    config.close();

    const QVector<QString> sources = ei::orderedResourceSources(
        {modResources, baseResources});
    QCOMPARE(sources.size(), 4);
    QCOMPARE(QFileInfo(sources.at(0)).fileName(), QString("HDPack_Mapobjects.res"));
    QCOMPARE(QFileInfo(sources.at(1)).fileName(), QString("figures.res"));
    QCOMPARE(QFileInfo(sources.at(2)).fileName(), QString("unlisted.res"));
    QCOMPARE(QFileInfo(sources.at(3)).absoluteFilePath(),
             QFileInfo(QDir(baseResources).filePath("figures.res")).absoluteFilePath());
}

void ResourceNameTests::cachesResourceMetadataUntilExplicitRefresh()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("figures.res"));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("first");
    source.close();

    const QVector<QString> sources{path};
    ei::invalidateResourceSourceFingerprintCache();
    const QString first = ei::cachedResourceSourceFingerprint(sources);

    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Append));
    source.write("-changed");
    source.close();
    // A profile generation is immutable until the user/resource refresh path
    // explicitly invalidates it; repeated object loads must remain O(1).
    QCOMPARE(ei::cachedResourceSourceFingerprint(sources), first);

    ei::invalidateResourceSourceFingerprintCache();
    QVERIFY(ei::cachedResourceSourceFingerprint(sources) != first);
}

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

void ResourceNameTests::normalizesStandaloneQuickItemAtlasCell()
{
    // Installed initqi41item: selector 8, authored cell (0, 3).
    const auto transform=ei::standaloneAtlasCellUvTransform(
        0.0846407711f,0.1644984931f,0.7677384615f,0.9929272532f);
    QVERIFY(transform.normalizedAtlasCell);
    QCOMPARE(transform.scaleU,4.0f);
    QCOMPARE(transform.scaleV,4.0f);
    QCOMPARE(transform.offsetU,0.0f);
    QCOMPARE(transform.offsetV,-3.0f);
    QVERIFY(qAbs(0.0846407711f*transform.scaleU+transform.offsetU
                 -0.3385630844f)<0.00001f);
    QVERIFY(qAbs(0.7677384615f*transform.scaleV+transform.offsetV
                 -0.070953846f)<0.00001f);

    // Multi-cell equipment UVs are not a standalone quick-item atlas cell.
    QVERIFY(!ei::standaloneAtlasCellUvTransform(
        0.01f,0.75f,0.02f,0.98f).normalizedAtlasCell);
}

void ResourceNameTests::decodesSparsePnt3WithTrailingTransparency()
{
    QByteArray compressed;
    appendDword(compressed, 16); // leading transparent bytes
    compressed.append("\x11\x22\x33\x44", 4);
    compressed.append("\x21\x32\x43\x54", 4);
    compressed.append("\x31\x42\x53\x64", 4);
    compressed.append("\x41\x52\x63\x74", 4); // one four-pixel literal block
    QByteArray decoded;
    QString diagnostic;
    QVERIFY2(ei::decodePnt3(compressed, 48, &decoded, &diagnostic), qPrintable(diagnostic));
    QCOMPARE(decoded.size(), 48);
    QCOMPARE(decoded.mid(16, 4), QByteArray("\x11\x22\x33\x44", 4));
    QCOMPARE(decoded.left(16), QByteArray(16, '\0'));
    QCOMPARE(decoded.right(16), QByteArray(16, '\0'));
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
    appendDword(compressed, 3245872);
    compressed.append(QByteArray(16, '\xff'));
    QByteArray decoded;
    QString diagnostic;
    QVERIFY2(ei::decodePnt3(compressed, 3245888, &decoded, &diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(decoded.left(3245872), QByteArray(3245872, '\0'));
    QCOMPARE(decoded.right(16), QByteArray(16, '\xff'));
}

void ResourceNameTests::preservesZeroAlphaPnt3PixelsInsideLiteralBlock()
{
    QByteArray compressed;
    compressed.append("\x11\x22\x33\xff", 4); // block control: literal
    appendDword(compressed, 3947840);             // alpha zero, block pixel 2
    compressed.append("\x44\x55\x66\x80", 4);
    appendDword(compressed, 5461080);             // alpha zero, block pixel 4
    QByteArray decoded;
    QString diagnostic;
    QVERIFY2(ei::decodePnt3(compressed, 16, &decoded, &diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(decoded, compressed);
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

void ResourceNameTests::decodesDxt1Transparency()
{
    QByteArray block;
    block.append(char(0x00)); block.append(char(0x00)); // black 565
    block.append(char(0x00)); block.append(char(0xf8)); // red 565; color0 <= color1
    appendDword(block, 3u); // first pixel uses transparent entry
    QImage image;
    QString diagnostic;
    QVERIFY2(ei::decodeDxtImage(block, 4, 4, ei::DxtFormat::Dxt1,
                               &image, &diagnostic), qPrintable(diagnostic));
    QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
    QCOMPARE(image.pixelColor(1, 0), QColor(0, 0, 0, 255));
}

void ResourceNameTests::decodesDxt3ExplicitAlpha()
{
    QByteArray block(8, '\0');
    block[0] = char(0x0f); // alpha 255, then alpha 0
    block.append(char(0x00)); block.append(char(0xf8));
    block.append(char(0x00)); block.append(char(0xf8));
    appendDword(block, 0u);
    QImage image;
    QString diagnostic;
    QVERIFY2(ei::decodeDxtImage(block, 4, 4, ei::DxtFormat::Dxt3,
                               &image, &diagnostic), qPrintable(diagnostic));
    QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0, 255));
    QCOMPARE(image.pixelColor(1, 0).alpha(), 0);
}

void ResourceNameTests::decodesDxt5InterpolatedAlpha()
{
    QByteArray block;
    block.append(char(255)); block.append(char(0));
    block.append(char(2)); block.append(QByteArray(5, '\0')); // first alpha index = 2
    block.append(char(0x00)); block.append(char(0xf8));
    block.append(char(0x00)); block.append(char(0xf8));
    appendDword(block, 0u);
    QImage image;
    QString diagnostic;
    QVERIFY2(ei::decodeDxtImage(block, 4, 4, ei::DxtFormat::Dxt5,
                               &image, &diagnostic), qPrintable(diagnostic));
    QCOMPARE(image.pixelColor(0, 0).alpha(), 218);
    QCOMPARE(image.pixelColor(1, 0).alpha(), 255);
}

void ResourceNameTests::rejectsTruncatedDxtMip()
{
    QImage image;
    QString diagnostic;
    QVERIFY(!ei::decodeDxtImage(QByteArray(7, '\0'), 4, 4,
                                ei::DxtFormat::Dxt1, &image, &diagnostic));
    QVERIFY(!diagnostic.isEmpty());
}

void ResourceNameTests::expandedTerrainDxtDecodeStaysWithinInteractiveBudget()
{
    // 16 physical pages are the 1,024-slot case reported as unusably slow.
    // This isolates the formerly per-pixel QColor/QImage dispatch path while
    // keeping the test independent of an installed resource configuration.
    const int dimension = 1024;
    QByteArray dxt((dimension / 4) * (dimension / 4) * 8, '\0');
    for(int offset = 0; offset < dxt.size(); offset += 8)
    {
        dxt[offset] = char(0x1f);
        dxt[offset + 1] = char(0x7c);
        dxt[offset + 2] = char(0xe0);
        dxt[offset + 3] = char(0x03);
        dxt[offset + 4] = char((offset / 8) & 0xff);
    }
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray firstHash;
    for(int page = 0; page < 16; ++page)
    {
        QImage decoded;
        QString diagnostic;
        QVERIFY2(ei::decodeDxtImage(dxt, dimension, dimension,
            ei::DxtFormat::Dxt1, &decoded, &diagnostic), qPrintable(diagnostic));
        if(page == 0)
            firstHash = QByteArray::number(decoded.cacheKey());
        QCOMPARE(decoded.size(), QSize(dimension, dimension));
    }
    QVERIFY(!firstHash.isEmpty());
    QVERIFY2(elapsed.elapsed() < 8000,
        qPrintable(QStringLiteral("16 terrain pages took %1 ms to decode")
            .arg(elapsed.elapsed())));
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

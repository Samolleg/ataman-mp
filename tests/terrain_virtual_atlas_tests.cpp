#include <QtTest>
#include <QtEndian>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTransform>

#include "endurance_document.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "terrain_virtual_atlas.h"

namespace {
STerrainTransitionSignature signature(qint32 nw,qint32 ne,qint32 se,qint32 sw)
{
    STerrainTransitionSignature value;
    value.northWest=nw; value.northEast=ne;
    value.southEast=se; value.southWest=sw;
    return value;
}

QImage solid(QRgb color)
{
    QImage image(8,8,QImage::Format_RGBA8888);
    image.fill(QColor::fromRgba(color));
    return image;
}

struct Fixture
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;

    Fixture(bool duplicatePixels=false)
    {
        images << solid(qRgba(220,20,20,255))
               << solid(qRgba(20,220,20,255))
               << solid(qRgba(20,20,220,255))
               << solid(duplicatePixels?qRgba(20,20,220,255):qRgba(230,220,20,255));
        for(int material=1;material<=4;++material)
        {
            STerrainTransitionVariant variant;
            variant.signature=signature(material,material,material,material);
            SMprPackedTile packed;
            packed.tileIndex=quint8(material-1);
            variant.packedTile=packed.encode();
            catalog.addVariant(variant);
        }
    }
};

QByteArray portableMpr(bool withInactiveOverlay = false)
{
    QByteArray mp;
    QDataStream stream(&mp,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream<<quint32(0xCE4AF672)<<float(100.0f)<<quint32(1)<<quint32(1)
          <<quint32(4)<<quint32(64)<<quint32(4)<<quint32(8)
          <<quint16(0)<<quint32(0);
    stream<<qint32(1)<<qint32(2)<<qint32(3)<<qint32(4);
    QByteArray sector;
    QDataStream sec(&sector,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec<<quint32(0xCF4BF774)<<quint8(withInactiveOverlay ? 3 : 0);
    sector.append(QByteArray(33*33*8*(withInactiveOverlay ? 2 : 1),'\0'));
    sector.append(QByteArray(16*16*2,'\0'));
    if(withInactiveOverlay)
    {
        sector.append(QByteArray(16*16*2, char(0xff)));
        sector.append(QByteArray(16*16*2, char(0xff)));
    }
    QByteArray empty(16,'\0');
    qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument archive;QStringList errors;
    if(!archive.loadBytes(empty,"empty",&errors))return {};
    QByteArray first;
    if(!archive.buildWithAppendedMember("CaveSet.MP",mp,0,&first,&errors)
       ||!archive.loadBytes(first,"first",&errors))return {};
    QByteArray second;
    return archive.buildWithAppendedMember("CaveSet000000.SEC",sector,0,&second,&errors)
        ?second:QByteArray();
}

STerrainTransitionSignature finalizedSignature(
    const STerrainAtlasFinalization& finalization, quint16 packed, bool* found)
{
    const SMprPackedTile decoded = SMprPackedTile::decode(packed);
    const int slot = decoded.atlasSlot();
    if(slot < 0 || slot >= finalization.tiles.size())
    {
        if(found) *found = false;
        return {};
    }
    if(found) *found = true;
    return CTerrainTransitionCatalog::rotated(
        finalization.tiles.at(slot).canonicalSignature, -decoded.rotation);
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

void applyVisualPatterns(QVector<QImage>* images)
{
    if(!images) return;
    const QColor light[] = {QColor(210,210,205), QColor(65,145,55),
                            QColor(180,115,35), QColor(55,95,165)};
    const QColor dark[] = {QColor(85,90,95), QColor(18,65,24),
                           QColor(90,45,12), QColor(18,35,80)};
    for(int material = 0; material < images->size() && material < 4; ++material)
        for(int y = 0; y < 8; ++y) for(int x = 0; x < 8; ++x)
        {
            const bool mark = material == 0 ? ((x / 2 + y / 2) & 1)
                : material == 1 ? (x % 3 == 0)
                : material == 2 ? ((x + y) % 4 == 0)
                : ((x - y + 16) % 5 == 0);
            (*images)[material].setPixelColor(x, y, mark ? light[material] : dark[material]);
        }
}

QImage renderFinalizedPreview(const STerrainAtlasFinalization& finalized,
                              int width, int height)
{
    if(finalized.tiles.isEmpty() || finalized.packedCells.size() != width * height)
        return {};
    const int tileSize = finalized.tiles.front().pixels.width();
    QImage canvas(width * tileSize, height * tileSize, QImage::Format_RGBA8888);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for(int y = 0; y < height; ++y) for(int x = 0; x < width; ++x)
    {
        const SMprPackedTile packed = SMprPackedTile::decode(
            finalized.packedCells.at(y * width + x));
        if(packed.atlasSlot() < 0 || packed.atlasSlot() >= finalized.tiles.size())
            continue;
        QImage tile = finalized.tiles.at(packed.atlasSlot()).pixels
            .mirrored(false, true);
        if(packed.rotation)
            tile = tile.transformed(QTransform().rotate(-90.0 * packed.rotation),
                                    Qt::FastTransformation);
        painter.drawImage(x * tileSize, y * tileSize, tile);
    }
    painter.end();
    return canvas.scaled(canvas.size() * 4, Qt::IgnoreAspectRatio,
                         Qt::FastTransformation);
}

bool writeVisualScene(const QString& root, const QString& relative,
                      const QByteArray& mpr, const QByteArray& resources,
                      const STerrainAtlasFinalization& finalized,
                      int width, int height, const QString& description)
{
    const QDir output(root);
    const QString scenePath = output.filePath(QStringLiteral("visual-scenes/") + relative);
    if(!QDir().mkpath(scenePath)) return false;
    const QDir scene(scenePath);
    const QImage preview = renderFinalizedPreview(finalized, width, height);
    const QByteArray review = QString(
        "AUTOMATED STRUCTURE: pass\nVISUAL REVIEW: unreviewed\n"
        "Review categories: acceptable | no-blend | tile-mismatch | wrong-rotation\n\n%1\n")
        .arg(description).toUtf8();
    return writeBytes(scene.filePath("scene.mpr"), mpr)
        && writeBytes(scene.filePath("scene-atlas.res"), resources)
        && !preview.isNull() && preview.save(scene.filePath("preview.png"), "PNG")
        && writeBytes(scene.filePath("VISUAL-REVIEW.txt"), review);
}
}

class TerrainVirtualAtlasTests:public QObject
{
    Q_OBJECT
private slots:
    void virtualFieldPaintIsUnboundedByAtlas();
    void exactAuthoredTileIsReused();
    void authoredGlobalBlendIsImportedBeforeGeneration();
    void generatesOneThreeTuple();
    void generatesTwoTwoTuple();
    void generatesFourFamilyTupleWithOrientation();
    void generatedEdgesUseOnlyTheirTwoCornerMaterials();
    void generatedTransitionRetainsSourceTextureIdentity();
    void generatedTransitionBoundaryIsOrganicRatherThanQuadrants();
    void materialNamesSelectExplicitBlendStyles();
    void materialBlendStylesProvideFourDistinctVariants();
    void generatedNeighbourEdgesArePixelContinuous();
    void rotationsAndIdenticalPixelsAreDeduplicated();
    void stableHashesAndNamespaceAreDeterministic();
    void mapWideUsageAndUnusedCompaction();
    void finalizesAtValidatedRuntimeCeilingBeyond512();
    void capacityRefusalNeverSubstitutes();
    void rawPageCompositionPreservesEveryUntouchedSlot();
    void pageOrientationMipmapsAlphaAndReopen();
    void namespacesAreIsolated();
    void exactOnlyNoOpPreservesPixels();
    void portablePackageRemapsNamespaceAndReopens();
    void workingPreviewPackagePreservesAuthoredPagesAndAppendsExtensions();
    void virtualPaintPreviewAndUndoPreserveDesiredIdentity();
    void preservedLayerSlotsArePinnedAndRemapped();
    void inactiveOverlaySentinelSurvivesLandFinalization();
    void gradedAlphaIsRefusedWithoutFlattening();
    void dxt1TerrainPagesMatchShippedMipContract();
    void dxt1AtlasTileDirectDecodeMatchesFullPageOrientation();
    void workingAtlasGeneratesImmediatelyAndReusesTuple();
    void expanded4096AtlasRetainsWorkingExtensionCapacity();
    void workingAtlasNeverReplacesAuthoredPixels();
    void repeatedGeneratedStampsNeverMutateAuthoredAtlas();
    void workingAtlasProtectsUndoSlotsAndRefusesCapacity();
    void externalUniformFamilyPaintsAndBlendsWithoutAuthoredSlot();
    void selfPaintPreservesExactPackedTileAndRotation();
    void parallelStripesAndBaseCrossResolveExactly();
    void nestedCircleMaterialPairingsResolveExactly();
    void generatedPaintMatrixSavesReopensAndMatchesVertexOracle();
};

void TerrainVirtualAtlasTests::virtualFieldPaintIsUnboundedByAtlas()
{
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(3,3,1));
    STerrainTileBrush brush;
    brush.centerX=1.5f; brush.centerY=1.5f; brush.radius=2.0f;
    QCOMPARE(field.paint(brush,99),12);
    QCOMPARE(field.material(1,1),qint32(99));
}

void TerrainVirtualAtlasTests::exactAuthoredTileIsReused()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images,7);
    const STerrainVirtualTile tile=atlas.resolve(signature(2,2,2,2),4,9);
    QVERIFY(tile.authored);
    QCOMPARE(SMprPackedTile::decode(tile.authoredPacked).atlasSlot(),1);
    QCOMPARE(atlas.generatedTiles().size(),0);
}

void TerrainVirtualAtlasTests::authoredGlobalBlendIsImportedBeforeGeneration()
{
    Fixture fixture;
    const STerrainTransitionSignature requested = signature(1, 2, 2, 1);
    QImage authoredBlend(8, 8, QImage::Format_RGBA8888);
    for(int y = 0; y < authoredBlend.height(); ++y)
        for(int x = 0; x < authoredBlend.width(); ++x)
            authoredBlend.setPixelColor(x, y, (x + y) & 1
                ? QColor(17, 83, 149) : QColor(221, 137, 29));
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 19u);
    CValidationReport report;
    QVERIFY2(atlas.addAuthoredLibraryTransition(
        requested, authoredBlend, QStringLiteral("stock-jmv-test"), &report),
        qPrintable(report.toText()));
    const STerrainVirtualTile resolved = atlas.resolve(requested, 4, 7, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(!resolved.authored);
    QVERIFY(resolved.importedAuthored);
    QVERIFY(resolved.generatedIndex >= 0);
    QCOMPARE(atlas.generatedTiles().at(resolved.generatedIndex).pixels,
             authoredBlend);
    QVERIFY(atlas.generatedTiles().at(resolved.generatedIndex).authored);
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    QVERIFY(field.setMaterial(1, 0, 2));
    QVERIFY(field.setMaterial(1, 1, 2));
    QVERIFY(field.setMaterial(0, 1, 1));
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("AuthoredLibrary"), 512, {1, 2, 3, 4}, &report);
    QVERIFY2(finalized.success, qPrintable(finalized.error));
    bool keptImportedPixels = false;
    for(const STerrainFinalizedTile& tile : finalized.tiles)
        keptImportedPixels |= tile.authored && tile.sourceSlot < 0
            && tile.pixels == authoredBlend;
    QVERIFY(keptImportedPixels);
}

void TerrainVirtualAtlasTests::generatesOneThreeTuple()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,2,2),0,0);
    QVERIFY(!tile.authored);
    QVERIFY(tile.generatedIndex>=0);
    QCOMPARE(atlas.generatedTiles().size(),1);
}

void TerrainVirtualAtlasTests::generatesTwoTwoTuple()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    QVERIFY(atlas.resolve(signature(1,1,2,2),0,0).generatedIndex>=0);
}

void TerrainVirtualAtlasTests::generatesFourFamilyTupleWithOrientation()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,3,4),0,0);
    const QImage image=atlas.generatedTiles().at(tile.generatedIndex).pixels;
    // Grid Y is mirrored once at the image boundary: SW,NW,NE,SE become
    // preview TL,BL,BR,TR respectively.
    QCOMPARE(image.pixelColor(0,0),fixture.images[3].pixelColor(0,0));
    QCOMPARE(image.pixelColor(7,0),fixture.images[2].pixelColor(7,0));
    QCOMPARE(image.pixelColor(7,7),fixture.images[1].pixelColor(7,7));
    QCOMPARE(image.pixelColor(0,7),fixture.images[0].pixelColor(0,7));
}

void TerrainVirtualAtlasTests::generatedEdgesUseOnlyTheirTwoCornerMaterials()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,3,4),0,0);
    const QImage image=atlas.generatedTiles().at(tile.generatedIndex).pixels;
    const QColor top=image.pixelColor(3,0);
    QVERIFY(top.red()>190 && top.green()>180 && top.blue()<60);
    const QColor bottom=image.pixelColor(3,7);
    QVERIFY(bottom.red()>190 && bottom.green()<70 && bottom.blue()<40);
}

void TerrainVirtualAtlasTests::generatedTransitionRetainsSourceTextureIdentity()
{
    Fixture fixture;
    for(int material = 0; material < 2; ++material)
        for(int y = 0; y < 8; ++y) for(int x = 0; x < 8; ++x)
        {
            const bool light = ((x + y) & 1) == 0;
            fixture.images[material].setPixelColor(x, y, material == 0
                ? QColor(light ? 245 : 105, 25, 20, 255)
                : QColor(20, light ? 235 : 95, 30, 255));
        }
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const STerrainVirtualTile tile = atlas.resolve(signature(1,1,2,2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(tile.generatedIndex).pixels;
    int sourceLike = 0;
    int genuinelyMixed = 0;
    for(int y = 0; y < 8; ++y) for(int x = 0; x < 8; ++x)
    {
        const QColor actual = generated.pixelColor(x, y);
        const auto distance = [&actual](const QColor& source) {
            return qAbs(actual.red() - source.red())
                + qAbs(actual.green() - source.green())
                + qAbs(actual.blue() - source.blue());
        };
        const int nearest = qMin(distance(fixture.images[0].pixelColor(x,y)),
                                 distance(fixture.images[1].pixelColor(x,y)));
        sourceLike += nearest <= 20;
        genuinelyMixed += nearest > 20;
    }
    // Ordinary bilinear cross-fading makes most of the tile a blurry average.
    // A generated transition must instead retain a source texture on at least
    // three quarters of its pixels, with only a narrow visible join.
    QVERIFY(sourceLike >= 48);
    QVERIFY(genuinelyMixed > 0);
}

void TerrainVirtualAtlasTests::generatedTransitionBoundaryIsOrganicRatherThanQuadrants()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 73);
    const STerrainVirtualTile tile = atlas.resolve(signature(1, 1, 2, 2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(tile.generatedIndex).pixels;
    QSet<int> crossingRows;
    for(int x = 1; x + 1 < generated.width(); ++x)
    {
        int crossing = -1;
        const bool startsGreen = generated.pixelColor(x, 0).green()
                               > generated.pixelColor(x, 0).red();
        for(int y = 1; y < generated.height(); ++y)
            if((generated.pixelColor(x, y).green()
                > generated.pixelColor(x, y).red()) != startsGreen)
            {
                crossing = y;
                break;
            }
        if(crossing >= 0)
            crossingRows.insert(crossing);
    }
    // A straight bilinear/quadrant mask crosses every column on one row.
    // Generated game-art transitions must visibly meander through the tile.
    QVERIFY2(crossingRows.size() >= 5,
             "Generated transition is still a straight quadrant cut");
}

void TerrainVirtualAtlasTests::materialNamesSelectExplicitBlendStyles()
{
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Grass / Dark"),
             ETerrainBlendStyle::Grass);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Ground / Dirt"),
             ETerrainBlendStyle::Soil);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Ground / Gravel"),
             ETerrainBlendStyle::Gravel);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Ground / Water"),
             ETerrainBlendStyle::Water);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Ice / Cracked"),
             ETerrainBlendStyle::Ice);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Lava"),
             ETerrainBlendStyle::Lava);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Paving / Radial"),
             ETerrainBlendStyle::Paving);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Slabs / Carved"),
             ETerrainBlendStyle::Paving);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Rock / Snow"),
             ETerrainBlendStyle::Snow);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Stone / Weathered"),
             ETerrainBlendStyle::Stone);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Rubble / Red"),
             ETerrainBlendStyle::Gravel);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Sand / Gravel"),
             ETerrainBlendStyle::Sand);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Snow / Common"),
             ETerrainBlendStyle::Snow);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Swamp"),
             ETerrainBlendStyle::Swamp);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Water / Waves"),
             ETerrainBlendStyle::Water);
    QCOMPARE(CTerrainVirtualAtlas::blendStyleForMaterialName("Future material"),
             ETerrainBlendStyle::Organic);
}

void TerrainVirtualAtlasTests::materialBlendStylesProvideFourDistinctVariants()
{
    const QVector<ETerrainBlendStyle> styles = {
        ETerrainBlendStyle::Grass, ETerrainBlendStyle::Soil,
        ETerrainBlendStyle::Ice, ETerrainBlendStyle::Lava,
        ETerrainBlendStyle::Paving, ETerrainBlendStyle::Stone,
        ETerrainBlendStyle::Gravel, ETerrainBlendStyle::Sand,
        ETerrainBlendStyle::Snow, ETerrainBlendStyle::Swamp,
        ETerrainBlendStyle::Water, ETerrainBlendStyle::Organic
    };
    QSet<QByteArray> styleFingerprints;
    for(ETerrainBlendStyle style : styles)
    {
        Fixture fixture;
        for(QImage& image : fixture.images)
            image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
        QMap<qint32, ETerrainBlendStyle> descriptors;
        descriptors.insert(1, style);
        descriptors.insert(2, ETerrainBlendStyle::Soil);
        CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x51a7u,
                                   {}, descriptors);
        for(int y = 0; y < 16 && atlas.generatedTiles().size() < 4; ++y)
            for(int x = 0; x < 16 && atlas.generatedTiles().size() < 4; ++x)
                QVERIFY(atlas.resolve(signature(1,2,2,2), x, y).generatedIndex >= 0);
        QCOMPARE(atlas.generatedTiles().size(),
                 CTerrainVirtualAtlas::blendVariationCount());
        QList<QByteArray> hashes;
        for(const STerrainFinalizedTile& tile : atlas.generatedTiles())
            hashes.append(tile.pixelSha256);
        std::sort(hashes.begin(), hashes.end());
        QByteArray fingerprint;
        for(const QByteArray& hash : hashes) fingerprint += hash + '\n';
        QVERIFY(!styleFingerprints.contains(fingerprint));
        styleFingerprints.insert(fingerprint);
    }
    QCOMPARE(styleFingerprints.size(), styles.size());
}

void TerrainVirtualAtlasTests::generatedNeighbourEdgesArePixelContinuous()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2, 1, 1));
    QVERIFY(field.setMaterial(0, 0, 1));
    QVERIFY(field.setMaterial(1, 0, 2));
    QVERIFY(field.setMaterial(2, 0, 1));
    QVERIFY(field.setMaterial(0, 1, 4));
    QVERIFY(field.setMaterial(1, 1, 3));
    QVERIFY(field.setMaterial(2, 1, 4));
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const auto finalized = atlas.finalize(field, "PixelSeam", 512, {1,2,3,4});
    QVERIFY2(finalized.success, qPrintable(finalized.error));
    const QImage preview = renderFinalizedPreview(finalized, 2, 1);
    QVERIFY(!preview.isNull());
    const int seamLeft = preview.width() / 2 - 1;
    const int seamRight = preview.width() / 2;
    for(int y = 0; y < preview.height(); ++y)
        QCOMPARE(preview.pixelColor(seamLeft, y), preview.pixelColor(seamRight, y));
}

void TerrainVirtualAtlasTests::rotationsAndIdenticalPixelsAreDeduplicated()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainTransitionSignature base=signature(1,2,3,4);
    const STerrainVirtualTile first=atlas.resolve(base,0,0);
    const STerrainVirtualTile rotated=atlas.resolve(
        CTerrainTransitionCatalog::rotated(base,1),0,0);
    QCOMPARE(first.generatedIndex,rotated.generatedIndex);
    QVERIFY(first.generatedRotation!=rotated.generatedRotation);

    Fixture duplicate(true);
    CTerrainVirtualAtlas duplicateAtlas(duplicate.catalog,duplicate.images);
    const int a=duplicateAtlas.resolve(signature(1,2,3,3),0,0).generatedIndex;
    const int b=duplicateAtlas.resolve(signature(1,2,4,4),0,0).generatedIndex;
    QCOMPARE(a,b);
}

void TerrainVirtualAtlasTests::stableHashesAndNamespaceAreDeterministic()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    field.setMaterial(1,0,2); field.setMaterial(1,1,3); field.setMaterial(0,1,4);
    CTerrainVirtualAtlas first(fixture.catalog,fixture.images,42);
    CTerrainVirtualAtlas second(fixture.catalog,fixture.images,42);
    const STerrainAtlasFinalization a=first.finalize(field,"Zone2",512);
    const STerrainAtlasFinalization b=second.finalize(field,"Zone2",512);
    QVERIFY2(a.success,qPrintable(a.error));
    QCOMPARE(a.stableSha256,b.stableSha256);
    QCOMPARE(a.isolatedNamespace,b.isolatedNamespace);
    QCOMPARE(a.pageMmpPayloads,b.pageMmpPayloads);
}

void TerrainVirtualAtlasTests::mapWideUsageAndUnusedCompaction()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2,2,1));
    field.setMaterial(1,1,2);
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone2",512,{10,20,30,40});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.usage.cellCount,quint64(4));
    QVERIFY(result.requiredSlots<fixture.images.size());
    QCOMPARE(result.packedCells.size(),4);
    QCOMPARE(result.tileTypes.size(),result.requiredSlots);
}

void TerrainVirtualAtlasTests::finalizesAtValidatedRuntimeCeilingBeyond512()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    QVector<qint32> tileTypes;
    QVector<quint16> pinned;
    constexpr int requiredSlots = 513;
    images.reserve(requiredSlots);
    tileTypes.reserve(requiredSlots);
    pinned.reserve(requiredSlots);
    for(int slot = 0; slot < requiredSlots; ++slot)
    {
        const qint32 material = slot + 1;
        QImage image(8, 8, QImage::Format_RGBA8888);
        image.fill(QColor(slot & 255, (slot >> 8) & 255,
                          (slot * 37) & 255, 255));
        images.append(image);
        tileTypes.append(slot);
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot % 64);
        packed.atlasIndex = quint8(slot / 64);
        STerrainTransitionVariant variant;
        variant.signature = signature(material, material, material, material);
        variant.packedTile = packed.encode();
        catalog.addVariant(variant);
        pinned.append(variant.packedTile);
    }
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    CTerrainVirtualAtlas atlas(catalog, images);
    const STerrainAtlasFinalization result = atlas.finalize(
        field, "ExpandedValidated", kValidatedShippedTerrainAtlasSlotLimit,
        tileTypes, nullptr, pinned);
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.capacityLimit, kValidatedShippedTerrainAtlasSlotLimit);
    QCOMPARE(result.requiredSlots, requiredSlots);
    QCOMPARE(result.textureCount, 9);
}

void TerrainVirtualAtlasTests::capacityRefusalNeverSubstitutes()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2,1,1));
    field.setMaterial(1,0,2);
    field.setMaterial(2,0,3);
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone15",1);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains("exceeding"));
    QVERIFY(result.error.contains("No material identity was substituted"));
    QVERIFY(result.packedCells.isEmpty());
}

void TerrainVirtualAtlasTests::rawPageCompositionPreservesEveryUntouchedSlot()
{
    QVector<QImage> tiles;
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage tile(8, 8, QImage::Format_RGBA8888);
        for(int y = 0; y < 8; ++y) for(int x = 0; x < 8; ++x)
            tile.setPixelColor(x, y, QColor(slot * 3 % 256,
                slot * 5 % 256, (x * 29 + y * 17) % 256, 255));
        tiles.append(tile);
    }
    const QImage original = CTerrainVirtualAtlas::composePage(tiles, 0, 8);
    QCOMPARE(original.size(), QSize(64, 64));
    for(int slot = 0; slot < 64; ++slot)
    {
        const int destinationX = (slot % 8) * 8;
        const int destinationY = (7 - slot / 8) * 8;
        QCOMPARE(original.copy(destinationX, destinationY, 8, 8), tiles.at(slot));
    }

    QImage replacement = tiles.at(37);
    replacement.fill(QColor(255, 0, 255, 255));
    tiles[37] = replacement;
    const QImage changed = CTerrainVirtualAtlas::composePage(tiles, 0, 8);
    for(int slot = 0; slot < 64; ++slot)
    {
        const int destinationX = (slot % 8) * 8;
        const int destinationY = (7 - slot / 8) * 8;
        const QImage before = original.copy(destinationX, destinationY, 8, 8);
        const QImage after = changed.copy(destinationX, destinationY, 8, 8);
        if(slot == 37) QVERIFY(before != after);
        else QCOMPARE(after, before);
    }
}

void TerrainVirtualAtlasTests::pageOrientationMipmapsAlphaAndReopen()
{
    Fixture fixture;
    fixture.images[0].setPixelColor(0,0,QColor(255,0,0,0));
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone1",512);
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.pages.size(),1);
    QCOMPARE(result.pages[0].pixelColor(0,56).alpha(),0);
    const ei::SMmpInspection reopened=ei::inspectMmp(result.pageMmpPayloads[0]);
    QVERIFY2(reopened.valid,qPrintable(reopened.diagnostic));
    QCOMPARE(reopened.formatName,QString("DXT1"));
    QVERIFY(reopened.authoredMipSizes.size()>1);
    QCOMPARE(reopened.baseImage.pixelColor(0,56).alpha(),0);
}

void TerrainVirtualAtlasTests::workingPreviewPackagePreservesAuthoredPagesAndAppendsExtensions()
{
    CMprDocument source;
    CValidationReport report;
    QVERIFY2(source.loadBytes(portableMpr(), "working-source.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(source, &report), qPrintable(report.toText()));
    terrain.enableWorkingAtlasSlots(5 * 64);
    bool available = false;
    const quint16 before = terrain.packedTile(
        0, 0, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    SMprPackedTile extension;
    extension.atlasIndex = 4;
    extension.tileIndex = 0;
    STerrainTileCommand command;
    STerrainTileDelta delta;
    delta.globalX = 0;
    delta.globalY = 0;
    delta.layer = ETerrainTileLayer::Land;
    delta.before = before;
    delta.after = extension.encode();
    command.deltas.append(delta);
    QVERIFY2(terrain.apply(command, true, &report), qPrintable(report.toText()));
    CMprEditTransaction transaction(QStringLiteral(
        "Reference generated working-atlas extension"));
    QVERIFY2(terrain.appendSectorEdits(&transaction, &report),
             qPrintable(report.toText()));
    QByteArray paintedBytes;
    QVERIFY2(source.applyTransaction(transaction, &paintedBytes, &report),
             qPrintable(report.toText()));
    CMprDocument paintedSource;
    QVERIFY2(paintedSource.loadBytes(
                 paintedBytes, "working-painted-source.mpr", &report),
             qPrintable(report.toText()));
    QVector<QImage> tiles(5 * 64, solid(qRgba(40, 80, 120, 255)));
    QVector<QByteArray> authoredPages;
    for(int page = 0; page < 4; ++page)
    {
        tiles[page * 64].fill(QColor(30 + page * 20, 60, 90, 255));
        QString diagnostic;
        const QByteArray payload = ei::encodeMmpDxt1(
            CTerrainVirtualAtlas::composePage(tiles, page * 64, 8),
            true, &diagnostic);
        QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
        authoredPages.append(payload);
    }
    tiles[4 * 64].fill(QColor(220, 30, 170, 255));
    const STerrainPortableAtlasPackage package =
        CTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedSource, tiles, authoredPages, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes, "working-result.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().textureCount, quint32(5));
    QCOMPARE(reopened.mapInfo().tileCount, quint32(320));
    CTerrainEditModel reopenedTerrain;
    QVERIFY2(reopenedTerrain.load(reopened, &report), qPrintable(report.toText()));
    const quint16 reopenedPacked = reopenedTerrain.packedTile(
        0, 0, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    QCOMPARE(SMprPackedTile::decode(reopenedPacked).atlasSlot(), 256);
    CResArchiveDocument resources;
    QStringList errors;
    QVERIFY2(resources.loadBytes(package.resourceArchiveBytes,
                                 "working-result.res", &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(package.resourceMembers.size(), 5);
    for(int page = 0; page < authoredPages.size(); ++page)
        QCOMPARE(resources.memberPayload(package.resourceMembers.at(page)),
                 authoredPages.at(page));
    const ei::SMmpInspection appended = ei::inspectMmp(
        resources.memberPayload(package.resourceMembers.last()));
    QVERIFY2(appended.valid, qPrintable(appended.diagnostic));
}

void TerrainVirtualAtlasTests::namespacesAreIsolated()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    CTerrainVirtualAtlas a(fixture.catalog,fixture.images);
    CTerrainVirtualAtlas b(fixture.catalog,fixture.images);
    const auto zone2=a.finalize(field,"Zone2",512);
    const auto gz2g=b.finalize(field,"Gz2G",512);
    QVERIFY(zone2.success&&gz2g.success);
    QVERIFY(zone2.isolatedNamespace!=gz2g.isolatedNamespace);
    QVERIFY(zone2.isolatedNamespace.startsWith("zone2_eime_"));
}

void TerrainVirtualAtlasTests::exactOnlyNoOpPreservesPixels()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2,2,1));
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const auto result=atlas.finalize(field,"Zone1",512,{77,88,99,111});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.requiredSlots,1);
    QCOMPARE(result.tiles[0].pixels,fixture.images[0]);
    QCOMPARE(result.tileTypes,QVector<qint32>({77}));
    QCOMPARE(atlas.generatedTiles().size(),0);
}

void TerrainVirtualAtlasTests::portablePackageRemapsNamespaceAndReopens()
{
    CMprDocument source;CValidationReport report;
    QVERIFY(source.loadBytes(portableMpr(),"portable-source.mpr",&report));
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16,16,1));
    field.setMaterial(8,8,2);
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const auto finalization=atlas.finalize(field,"CaveSet",512,{1,2,3,4},&report);
    QVERIFY2(finalization.success,qPrintable(finalization.error));
    const auto package=CTerrainVirtualAtlas::buildPortablePackage(
        source,finalization,ETerrainTileLayer::Land,&report);
    QVERIFY2(package.success,qPrintable(package.error+"\n"+report.toText()));
    CMprDocument reopened;
    QVERIFY(reopened.loadBytes(package.mprBytes,"portable-result.mpr",&report));
    QCOMPARE(reopened.mapInfo().innerNamespace,finalization.isolatedNamespace);
    QCOMPARE(reopened.mapInfo().textureCount,quint32(finalization.textureCount));
    QCOMPARE(reopened.mapInfo().tileCount,quint32(finalization.requiredSlots));
    CResArchiveDocument resources;QStringList errors;
    QVERIFY(resources.loadBytes(package.resourceArchiveBytes,"portable-atlas.res",&errors));
    QCOMPARE(package.resourceMembers.size(),finalization.textureCount);
    QVERIFY(!resources.memberPayload(package.resourceMembers.front()).isEmpty());
}

void TerrainVirtualAtlasTests::virtualPaintPreviewAndUndoPreserveDesiredIdentity()
{
    Fixture fixture;
    STerrainTransitionVariant authored;
    authored.signature=signature(1,1,2,1);
    SMprPackedTile authoredPacked;authoredPacked.tileIndex=10;
    authored.packedTile=authoredPacked.encode();
    QVERIFY(fixture.catalog.addVariant(authored));
    QByteArray payload;
    QDataStream sec(&payload,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec<<quint32(0xCF4BF774)<<quint8(0);
    payload.append(QByteArray(33*33*8,'\0'));
    payload.append(QByteArray(16*16*2,'\0'));
    STerrainSectorSource source;
    source.memberName="CaveSet000000.SEC";source.sectorX=0;source.sectorY=0;
    source.type=0;source.payload=payload;source.landTileOffset=5+33*33*8;
    CTerrainEditModel model;CValidationReport report;
    QVERIFY(model.loadSources(1,1,100.0f,{source},&report,1,
                              QVector<qint32>(64,1),0));
    STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=1;
    const STerrainTileCommand command=model.makeVirtualMaterialPaint(
        8,8,brush,ETerrainTileLayer::Land,2,fixture.catalog,99,&report);
    QVERIFY2(!command.isEmpty(),qPrintable(report.toText()));
    QCOMPARE(command.virtualVertices.size(),5);
    QCOMPARE(command.semanticAudit.solverMode,QString("virtual-unblended-preview"));
    QVERIFY(!command.deltas.isEmpty());
    SMprPackedTile pureSelected;pureSelected.tileIndex=1;
    for(const STerrainTileDelta& delta:command.deltas)
        QCOMPARE(delta.after,pureSelected.encode());
    QVERIFY(model.apply(command,true,&report));
    CTerrainVirtualMaterialField painted;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&painted));
    for(const auto& touched:command.virtualVertices)
        QCOMPARE(painted.material(touched.globalX,touched.globalY),qint32(2));
    QCOMPARE(painted.material(6,8),qint32(1));
    QVector<QImage> workingImages = fixture.images;
    while(workingImages.size() < 64) workingImages.append(fixture.images.front());
    CTerrainWorkingAtlas working(fixture.catalog, workingImages, 99, 128);
    model.enableWorkingAtlasSlots(128);
    QMap<int,quint64> usage;
    for(int slot = 0; slot < 128; ++slot) usage.insert(slot, 0);
    QSet<int> protectedSlots({0,1,2,3,10});
    QSet<int> affected;
    for(const auto& vertex : command.virtualVertices)
        for(int y : {vertex.globalY-1, vertex.globalY})
            for(int x : {vertex.globalX-1, vertex.globalX})
                if(x >= 0 && x < painted.tileWidth() && y >= 0 && y < painted.tileHeight())
                    affected.insert(y * painted.tileWidth() + x);
    CValidationReport releaseReport;
    for(int cell : affected)
    {
        const int x = cell % painted.tileWidth();
        const int y = cell / painted.tileWidth();
        bool generated = false;
        const quint16 packed = working.resolve(painted.cellSignature(x,y), x, y,
                                               &usage, protectedSlots, &generated,
                                               &releaseReport);
        if(!releaseReport.hasErrors())
            ++usage[SMprPackedTile::decode(packed).atlasSlot()];
    }
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    const STerrainTileCommand resolved=model.makeVirtualStrokeResolution(
        command,working.catalog(),99,&releaseReport);
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    QCOMPARE(resolved.semanticAudit.solverMode,QString("virtual-release-exact"));
    QVERIFY(!resolved.deltas.isEmpty());
    QVERIFY(model.apply(resolved,true,&report));
    CTerrainVirtualMaterialField afterResolution;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&afterResolution));
    for(const auto& touched:command.virtualVertices)
        QCOMPARE(afterResolution.material(touched.globalX,touched.globalY),qint32(2));
    QVERIFY(model.apply(resolved,false,&report));
    QVERIFY(model.apply(command,false,&report));
    CTerrainVirtualMaterialField undone;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&undone));
    QCOMPARE(undone.material(8,8),qint32(1));
}

void TerrainVirtualAtlasTests::externalUniformFamilyPaintsAndBlendsWithoutAuthoredSlot()
{
    Fixture fixture;
    const qint32 externalId = 9001;
    fixture.catalog.declareMaterial(externalId);
    QMap<qint32, QImage> external;
    external.insert(externalId, solid(qRgba(170, 170, 165, 255)));
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 17, external);
    CValidationReport report;
    const STerrainVirtualTile pure = atlas.resolve(
        signature(externalId, externalId, externalId, externalId), 3, 4, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(!pure.authored);
    QVERIFY(pure.generatedIndex >= 0);
    const STerrainVirtualTile mixed = atlas.resolve(
        signature(1, externalId, externalId, 1), 3, 4, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(mixed.generatedIndex >= 0);

    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(2, 2, 1));
    QVERIFY(field.setMaterial(1, 1, externalId));
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, "GlobalFamily", 64, {1, 1, 1, 1}, &report);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    // The four incident cells retain one exact canonical tuple but may select
    // multiple deterministic artistic variants. The separately resolved pure
    // source remains cached and is not counted because the field has no
    // all-external cell.
    QVERIFY(finalized.usage.generatedTiles.size() >= 1);
    QVERIFY(finalized.usage.generatedTiles.size()
            <= CTerrainVirtualAtlas::blendVariationCount());
    QVERIFY(atlas.generatedTiles().size() >= 2);
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool found = false;
            QCOMPARE(finalizedSignature(finalized,
                         finalized.packedCells.at(y * field.tileWidth() + x),
                         &found), field.cellSignature(x, y));
            QVERIFY(found);
        }
}

void TerrainVirtualAtlasTests::preservedLayerSlotsArePinnedAndRemapped()
{
    Fixture fixture;CTerrainVirtualMaterialField field;QVERIFY(field.resize(1,1,1));
    SMprPackedTile pinned;pinned.tileIndex=3;pinned.rotation=2;
    CTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    const auto result=atlas.finalize(field,"Zone2",512,{10,20,30,40},nullptr,{pinned.encode()});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.requiredSlots,2);
    QVERIFY(result.authoredSlotRemap.contains(0));
    QVERIFY(result.authoredSlotRemap.contains(3));
    QVERIFY(result.authoredSlotRemap.value(0)!=result.authoredSlotRemap.value(3));
}

void TerrainVirtualAtlasTests::inactiveOverlaySentinelSurvivesLandFinalization()
{
    CMprDocument source;
    CValidationReport report;
    QVERIFY2(source.loadBytes(portableMpr(true), "inactive-overlay.mpr", &report),
             qPrintable(report.toText()));
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    QVERIFY(field.setMaterial(8, 8, 2));
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const auto finalized = atlas.finalize(field, "InactiveOverlay", 512,
                                           {1,2,3,4}, &report);
    QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
    const auto package = CTerrainVirtualAtlas::buildPortablePackage(
        source, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success, qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes, "inactive-overlay-result.mpr", &report),
             qPrintable(report.toText()));
    QVERIFY(reopened.sectors().front().hasOverlay);
    const SMprSectorInfo& sector = reopened.sectors().front();
    const QByteArray payload = reopened.members().at(sector.memberIndex).payload;
    QCOMPARE(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(
        payload.constData() + int(sector.overlayTileOffset))), quint16(0xffff));
    QCOMPARE(sector.overlayMaterialIndices.front(), qint16(-1));
}

void TerrainVirtualAtlasTests::gradedAlphaIsRefusedWithoutFlattening()
{
    QImage image(8,8,QImage::Format_ARGB32);image.fill(QColor(10,20,30,128));
    QString diagnostic;
    QVERIFY(ei::encodeMmpDxt1(image,true,&diagnostic).isEmpty());
    QVERIFY(diagnostic.contains("graded alpha"));
}

void TerrainVirtualAtlasTests::dxt1TerrainPagesMatchShippedMipContract()
{
    QImage image(2048, 2048, QImage::Format_ARGB32);
    image.fill(QColor(35, 95, 45, 255));
    QString diagnostic;
    const QByteArray encoded = ei::encodeMmpDxt1(image, true, &diagnostic);
    QVERIFY2(!encoded.isEmpty(), qPrintable(diagnostic));
    QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(
                 encoded.constData() + 12)), quint32(12));
    QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(
                 encoded.constData() + 20)), quint32(4));
    const ei::SMmpInspection inspected = ei::inspectMmp(encoded);
    QVERIFY2(inspected.valid, qPrintable(inspected.diagnostic));
    QCOMPARE(inspected.authoredMipSizes.front(), QSize(2048, 2048));
    QCOMPARE(inspected.authoredMipSizes.back(), QSize(1, 1));
    QCOMPARE(inspected.authoredMipSizes.size(), 12);
    QCOMPARE(encoded.size(), 2796292);
}

void TerrainVirtualAtlasTests::dxt1AtlasTileDirectDecodeMatchesFullPageOrientation()
{
    QVector<QImage> tiles;
    tiles.reserve(64);
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage tile(16, 16, QImage::Format_RGBA8888);
        for(int y = 0; y < tile.height(); ++y)
            for(int x = 0; x < tile.width(); ++x)
                tile.setPixelColor(x, y, QColor(
                    (slot * 19 + x * 11) & 255,
                    (slot * 31 + y * 7) & 255,
                    (slot * 47 + x * 3 + y * 5) & 255));
        tiles.append(tile);
    }
    QString diagnostic;
    const QByteArray payload = ei::encodeMmpDxt1(
        CTerrainVirtualAtlas::composePage(tiles, 0, 16), true, &diagnostic);
    QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
    const ei::SMmpInspection full = ei::inspectMmp(payload);
    QVERIFY2(full.valid, qPrintable(full.diagnostic));
    const QImage editorPage = full.baseImage.mirrored(false, true);
    for(int slot : {0, 7, 8, 27, 56, 63})
    {
        QImage direct;
        QVERIFY2(ei::decodeMmpDxt1AtlasTile(
                     payload, slot, &direct, &diagnostic),
                 qPrintable(diagnostic));
        const QImage legacy = editorPage.copy(
            (slot % 8) * 16, (slot / 8) * 16, 16, 16).mirrored(false, true);
        QCOMPARE(direct, legacy);
    }
}

void TerrainVirtualAtlasTests::workingAtlasGeneratesImmediatelyAndReusesTuple()
{
    Fixture fixture;
    CTerrainWorkingAtlas working(fixture.catalog,fixture.images,17,8);
    QVERIFY2(working.isValid(),qPrintable(working.error()));
    QMap<int,quint64> usage;
    usage[0]=4;usage[1]=4;usage[2]=4;usage[3]=0;
    QSet<int> protectedSlots({0,1,2});
    CValidationReport report;bool generated=false;
    const STerrainTransitionSignature missing=signature(1,2,2,2);
    const quint16 first=working.resolve(missing,7,8,&usage,protectedSlots,
                                        &generated,&report);
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    QVERIFY(generated);
    QCOMPARE(SMprPackedTile::decode(first).atlasSlot(),4);
    QVERIFY(working.takeChangedSlots().contains(4));
    usage[4]=1;
    const quint16 reused=working.resolve(missing,7,8,&usage,protectedSlots,
                                         &generated,&report);
    QCOMPARE(reused,first);
    QCOMPARE(working.generatedTileCount(),1);
    QVERIFY(working.takeChangedSlots().isEmpty());
    bool decoded=false;
    QCOMPARE(working.catalog().gridSignatureForPacked(reused,&decoded),missing);
    QVERIFY(decoded);
}

void TerrainVirtualAtlasTests::expanded4096AtlasRetainsWorkingExtensionCapacity()
{
    CTerrainTransitionCatalog catalog;
    STerrainTransitionVariant uniform;
    uniform.signature = signature(1, 1, 1, 1);
    uniform.packedTile = 0;
    catalog.addVariant(uniform);
    QVector<QImage> authored(4096, solid(qRgba(20, 120, 40, 255)));
    QMap<qint32, QImage> external;
    external.insert(2, solid(qRgba(150, 70, 30, 255)));
    CTerrainWorkingAtlas working(catalog, authored, 91u,
        kMprPackedTerrainAtlasSlotLimit, external);
    QCOMPARE(working.capacityLimit(), kMprPackedTerrainAtlasSlotLimit);
    QMap<int, quint64> usage;
    for(int slot = 0; slot < authored.size(); ++slot)
        usage.insert(slot, 1);
    CValidationReport report;
    const quint16 packed = working.resolve(signature(1, 2, 2, 2),
        0, 0, &usage, {}, nullptr, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(), 4096);
    QCOMPARE(working.activeTextureCount(), 65);
}

void TerrainVirtualAtlasTests::workingAtlasNeverReplacesAuthoredPixels()
{
    Fixture fixture;
    const QVector<QImage> original = fixture.images;
    CTerrainWorkingAtlas working(fixture.catalog, fixture.images, 17, 8);
    QMap<int, quint64> usage;
    for(int slot = 0; slot < 8; ++slot) usage[slot] = 0;
    CValidationReport report;
    bool generated = false;
    const quint16 packed = working.resolve(signature(1, 2, 3, 4), 0, 0,
                                            &usage, {}, &generated, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(generated);
    QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(), original.size());
    const QVector<QImage> active = working.activeTileImages();
    for(int slot = 0; slot < original.size(); ++slot)
        QCOMPARE(active.at(slot), original.at(slot));
}

void TerrainVirtualAtlasTests::repeatedGeneratedStampsNeverMutateAuthoredAtlas()
{
    Fixture fixture;
    QVector<QImage> authored;
    authored.reserve(512);
    for(int slot = 0; slot < 512; ++slot)
        authored.append(fixture.images.at(slot % fixture.images.size()));
    QMap<qint32, QImage> external;
    for(int material = 5; material <= 6; ++material)
    {
        QImage image(8, 8, QImage::Format_RGBA8888);
        for(int y = 0; y < image.height(); ++y)
            for(int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y, QColor(
                    (material * 31 + x * 19 + y * 7) & 255,
                    (material * 47 + x * 5 + y * 23) & 255,
                    (material * 59 + x * 13 + y * 11) & 255));
        external.insert(material, image);
    }
    CTerrainWorkingAtlas working(fixture.catalog, authored, 97, 1024, external);
    QMap<int, quint64> usage;
    for(int slot = 0; slot < 1024; ++slot) usage[slot] = 0;
    CValidationReport report;
    int realized = 0;
    for(int nw = 1; nw <= 6 && realized < 300; ++nw)
        for(int ne = 1; ne <= 6 && realized < 300; ++ne)
            for(int se = 1; se <= 6 && realized < 300; ++se)
                for(int sw = 1; sw <= 6 && realized < 300; ++sw)
                {
                    bool generated = false;
                    const quint16 packed = working.resolve(
                        signature(nw, ne, se, sw), nw * 37 + se,
                        ne * 41 + sw, &usage, {}, &generated, &report);
                    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
                    const int slot = SMprPackedTile::decode(packed).atlasSlot();
                    QVERIFY(slot >= 0);
                    usage[slot] += 1;
                    realized = working.generatedTileCount();
                }
    QVERIFY2(realized >= 300, "Stress fixture did not create enough exact tiles");
    const QVector<QImage> active = working.activeTileImages();
    QVERIFY(active.size() > authored.size());
    for(int slot = 0; slot < authored.size(); ++slot)
        QCOMPARE(active.at(slot), authored.at(slot));
}

void TerrainVirtualAtlasTests::workingAtlasProtectsUndoSlotsAndRefusesCapacity()
{
    Fixture fixture;
    CTerrainWorkingAtlas working(fixture.catalog,fixture.images,3,4);
    QMap<int,quint64> usage;
    usage[0]=usage[1]=usage[2]=usage[3]=0;
    const QSet<int> allProtected({0,1,2,3});
    CValidationReport report;bool generated=false;
    QCOMPARE(working.resolve(signature(1,2,3,4),0,0,&usage,allProtected,
                             &generated,&report),quint16(0));
    QVERIFY(report.hasErrors());
    QVERIFY(report.toText().contains("undo history"));
    QVERIFY(report.toText().contains("No identity was substituted"));
}

void TerrainVirtualAtlasTests::selfPaintPreservesExactPackedTileAndRotation()
{
    const QVector<ETerrainTileBrushShape> shapes = {
        ETerrainTileBrushShape::Circle, ETerrainTileBrushShape::Square};
    for(qint32 material = 1; material <= 4; ++material)
        for(ETerrainTileBrushShape shape : shapes)
            for(float radius : {1.0f, 3.0f})
            {
                CTerrainTransitionCatalog catalog;
                SMprPackedTile originalPacked;
                originalPacked.tileIndex = quint8(8 + material);
                originalPacked.rotation = quint8((material + int(radius)) % 4);
                STerrainTransitionVariant variant;
                variant.signature = signature(material, material, material, material);
                variant.packedTile = originalPacked.encode();
                QVERIFY(catalog.addVariant(variant));

                QByteArray payload;
                QDataStream sec(&payload, QIODevice::WriteOnly);
                sec.setByteOrder(QDataStream::LittleEndian);
                sec << quint32(0xCF4BF774) << quint8(0);
                payload.append(QByteArray(33 * 33 * 8, '\0'));
                const int tileOffset = payload.size();
                payload.append(QByteArray(16 * 16 * 2, '\0'));
                for(int cell = 0; cell < 16 * 16; ++cell)
                    qToLittleEndian<quint16>(originalPacked.encode(),
                        reinterpret_cast<uchar*>(payload.data() + tileOffset + cell * 2));
                STerrainSectorSource source;
                source.memberName = "Self000000.SEC";
                source.sectorX = source.sectorY = 0;
                source.type = 0;
                source.payload = payload;
                source.landTileOffset = tileOffset;
                CTerrainEditModel model;
                CValidationReport report;
                QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report,
                                          1, QVector<qint32>(64, 1), 0));
                STerrainTileBrush brush;
                brush.centerX = brush.centerY = 8.0f;
                brush.radius = radius;
                brush.shape = shape;
                const STerrainTileCommand command = model.makeVirtualMaterialPaint(
                    8, 8, brush, ETerrainTileLayer::Land, material,
                    catalog, 0x2468u, &report);
                QVERIFY2(command.isEmpty(), qPrintable(report.toText()));
                for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
                {
                    bool available = false;
                    QCOMPARE(model.packedTile(x, y, ETerrainTileLayer::Land,
                                              &available), originalPacked.encode());
                    QVERIFY(available);
                }
            }
}

void TerrainVirtualAtlasTests::parallelStripesAndBaseCrossResolveExactly()
{
    Fixture fixture;
    const QString outputPath = QString::fromLocal8Bit(
        qgetenv("EI_MAPPER_ATLAS_TEST_OUTPUT"));
    if(!outputPath.isEmpty()) applyVisualPatterns(&fixture.images);
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    for(int stripe = 0; stripe < 3; ++stripe)
        for(int y = 2 + stripe * 5; y <= 4 + stripe * 5; ++y)
            for(int x = 2; x <= 14; ++x)
                QVERIFY(field.setMaterial(x, y, stripe + 2));
    for(int y = 1; y <= 15; ++y)
        for(int x = 7; x <= 9; ++x)
            QVERIFY(field.setMaterial(x, y, 1));
    QVector<QPoint> diagonalBase;
    for(int y = 2; y <= 13; ++y)
    {
        const int centerX = y;
        for(int offset = -1; offset <= 1; ++offset)
        {
            const QPoint point(centerX + offset, y);
            diagonalBase.append(point);
            QVERIFY(field.setMaterial(point.x(), point.y(), 1));
        }
    }

    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x314159u);
    CValidationReport report;
    const auto finalized = atlas.finalize(field, "StripeCross", 512,
                                           {1,2,3,4}, &report);
    QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
    for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
    {
        bool decoded = false;
        QCOMPARE(finalizedSignature(finalized,
            finalized.packedCells.at(y * 16 + x), &decoded), field.cellSignature(x, y));
        QVERIFY(decoded);
    }
    for(int y = 1; y <= 15; ++y)
        for(int x = 7; x <= 9; ++x)
            QCOMPARE(field.material(x, y), qint32(1));
    for(const QPoint& point : diagonalBase)
        QCOMPARE(field.material(point.x(), point.y()), qint32(1));

    CMprDocument source;
    QVERIFY(source.loadBytes(portableMpr(), "stripe-source.mpr", &report));
    const auto package = CTerrainVirtualAtlas::buildPortablePackage(
        source, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success, qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY(reopened.loadBytes(package.mprBytes, "stripe-cross.mpr", &report));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(reopened, &report));
    for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
    {
        bool available = false;
        QCOMPARE(terrain.packedTile(x, y, ETerrainTileLayer::Land, &available),
                 finalized.packedCells.at(y * 16 + x));
        QVERIFY(available);
    }
    if(!outputPath.isEmpty())
        QVERIFY(writeVisualScene(outputPath, "compositions/parallel-stripes-base-cross",
            package.mprBytes, package.resourceArchiveBytes, finalized, 16, 16,
            "Three parallel size-3 material stripes crossed by perpendicular and 45-degree size-3 base-material strokes."));
}

void TerrainVirtualAtlasTests::nestedCircleMaterialPairingsResolveExactly()
{
    Fixture fixture;
    const QString outputPath = QString::fromLocal8Bit(
        qgetenv("EI_MAPPER_ATLAS_TEST_OUTPUT"));
    if(!outputPath.isEmpty()) applyVisualPatterns(&fixture.images);
    int scenarios = 0;
    for(qint32 outer = 1; outer <= 4; ++outer)
        for(qint32 inner = 1; inner <= 4; ++inner)
        {
            if(inner == outer) continue;
            CTerrainVirtualMaterialField field;
            QVERIFY(field.resize(16, 16, 1));
            STerrainTileBrush outerBrush;
            outerBrush.centerX = outerBrush.centerY = 8.0f;
            outerBrush.radius = 3.0f;
            outerBrush.shape = ETerrainTileBrushShape::Circle;
            const int outerChanges = field.paint(outerBrush, outer);
            if(outer == 1) QCOMPARE(outerChanges, 0);
            else QVERIFY(outerChanges > 0);
            STerrainTileBrush innerBrush = outerBrush;
            innerBrush.radius = 1.0f;
            QVERIFY(field.paint(innerBrush, inner) > 0);

            CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x271828u);
            CValidationReport report;
            const auto finalized = atlas.finalize(field,
                QString("Nested-%1-%2").arg(outer).arg(inner), 512,
                {1,2,3,4}, &report);
            QVERIFY2(finalized.success,
                qPrintable(finalized.error + "\n" + report.toText()));
            for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
            {
                bool decoded = false;
                QCOMPARE(finalizedSignature(finalized,
                    finalized.packedCells.at(y * 16 + x), &decoded),
                    field.cellSignature(x, y));
                QVERIFY(decoded);
            }
            CMprDocument source;
            QVERIFY(source.loadBytes(portableMpr(), "nested-source.mpr", &report));
            const auto package = CTerrainVirtualAtlas::buildPortablePackage(
                source, finalized, ETerrainTileLayer::Land, &report);
            QVERIFY2(package.success,
                qPrintable(package.error + "\n" + report.toText()));
            CMprDocument reopened;
            QVERIFY(reopened.loadBytes(package.mprBytes,
                QString("nested-%1-%2.mpr").arg(outer).arg(inner), &report));
            CTerrainEditModel terrain;
            QVERIFY(terrain.load(reopened, &report));
            for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
            {
                bool available = false;
                QCOMPARE(terrain.packedTile(x, y, ETerrainTileLayer::Land,
                                            &available),
                         finalized.packedCells.at(y * 16 + x));
                QVERIFY(available);
            }
            if(!outputPath.isEmpty())
                QVERIFY(writeVisualScene(outputPath,
                    QString("compositions/nested-circles/outer-%1-inner-%2")
                        .arg(outer).arg(inner),
                    package.mprBytes, package.resourceArchiveBytes,
                    finalized, 16, 16,
                    QString("Radius-3 material %1 circle with radius-1 material %2 center.")
                        .arg(outer).arg(inner)));
            CTerrainVirtualAtlas repeated(fixture.catalog, fixture.images, 0x271828u);
            const auto again = repeated.finalize(field,
                QString("Nested-%1-%2").arg(outer).arg(inner), 512, {1,2,3,4});
            QCOMPARE(again.stableSha256, finalized.stableSha256);
            QCOMPARE(again.pageMmpPayloads, finalized.pageMmpPayloads);
            ++scenarios;
        }
    QCOMPARE(scenarios, 12);
}

void TerrainVirtualAtlasTests::generatedPaintMatrixSavesReopensAndMatchesVertexOracle()
{
    struct Scenario { QString name; qint32 background; qint32 brush; QVector<QPoint> touched; };
    QVector<Scenario> scenarios;
    const QVector<QString> shapes = {"square", "horizontal", "vertical", "diagonal-nw-se", "diagonal-ne-sw"};
    for(qint32 background = 1; background <= 4; ++background)
        for(qint32 brush = 1; brush <= 4; ++brush)
        {
            for(const QString& shape : shapes)
            {
                Scenario scenario;
                scenario.name = QString("pair-%1-over-%2-%3").arg(brush).arg(background).arg(shape);
                scenario.background = background;
                scenario.brush = brush;
                if(shape == "square")
                    for(int y = 7; y <= 9; ++y) for(int x = 7; x <= 9; ++x)
                        scenario.touched.append(QPoint(x, y));
                else
                    for(int offset = -3; offset <= 3; ++offset)
                    {
                        if(shape == "horizontal") scenario.touched.append(QPoint(8 + offset, 8));
                        else if(shape == "vertical") scenario.touched.append(QPoint(8, 8 + offset));
                        else if(shape == "diagonal-nw-se") scenario.touched.append(QPoint(8 + offset, 8 + offset));
                        else scenario.touched.append(QPoint(8 + offset, 8 - offset));
                    }
                scenarios.append(scenario);
            }
        }
    Scenario threeFamily{"junction-three-family", 1, 2,
        {QPoint(8,8), QPoint(9,8), QPoint(8,9)}};
    scenarios.append(threeFamily);
    Scenario fourFamily{"junction-four-family", 1, 2,
        {QPoint(8,8), QPoint(9,8), QPoint(9,9), QPoint(8,9)}};
    scenarios.append(fourFamily);

    Fixture fixture;
    CMprDocument source;
    CValidationReport sourceReport;
    QVERIFY2(source.loadBytes(portableMpr(), "matrix-source.mpr", &sourceReport),
             qPrintable(sourceReport.toText()));
    const QString outputPath = QString::fromLocal8Bit(qgetenv("EI_MAPPER_ATLAS_TEST_OUTPUT"));
    if(!outputPath.isEmpty()) QVERIFY(QDir().mkpath(outputPath));
    if(!outputPath.isEmpty()) applyVisualPatterns(&fixture.images);
    QStringList rawResults;
    int authoredCells = 0;
    int generatedCells = 0;

    for(const Scenario& scenario : scenarios)
    {
        CTerrainVirtualMaterialField field;
        QVERIFY(field.resize(16, 16, scenario.background));
        for(const QPoint& point : scenario.touched)
            QVERIFY(field.setMaterial(point.x(), point.y(), scenario.brush));
        if(scenario.name == "junction-three-family")
        {
            QVERIFY(field.setMaterial(8,8,2));
            QVERIFY(field.setMaterial(9,8,3));
            QVERIFY(field.setMaterial(8,9,2));
        }
        else if(scenario.name == "junction-four-family")
        {
            QVERIFY(field.setMaterial(8,8,1));
            QVERIFY(field.setMaterial(9,8,2));
            QVERIFY(field.setMaterial(9,9,3));
            QVERIFY(field.setMaterial(8,9,4));
        }
        for(const QPoint& point : scenario.touched)
            QVERIFY(field.material(point.x(), point.y()) != 0);

        CValidationReport report;
        CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x13579u);
        const auto finalized = atlas.finalize(field, "MatrixMap", 512,
                                               {1,2,3,4}, &report);
        QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
        const auto package = CTerrainVirtualAtlas::buildPortablePackage(
            source, finalized, ETerrainTileLayer::Land, &report);
        QVERIFY2(package.success, qPrintable(package.error + "\n" + report.toText()));

        for(int y = 0; y < 16; ++y)
            for(int x = 0; x < 16; ++x)
            {
                const int cell = y * 16 + x;
                bool decoded = false;
                const auto actual = finalizedSignature(
                    finalized, finalized.packedCells.at(cell), &decoded);
                QVERIFY(decoded);
                QCOMPARE(actual, field.cellSignature(x, y));
                const SMprPackedTile packed = SMprPackedTile::decode(
                    finalized.packedCells.at(cell));
                if(finalized.tiles.at(packed.atlasSlot()).authored) ++authoredCells;
                else ++generatedCells;
                if(x + 1 < 16)
                {
                    const auto east = finalizedSignature(
                        finalized, finalized.packedCells.at(cell + 1), &decoded);
                    QCOMPARE(actual.northEast, east.northWest);
                    QCOMPARE(actual.southEast, east.southWest);
                }
                if(y + 1 < 16)
                {
                    const auto south = finalizedSignature(
                        finalized, finalized.packedCells.at(cell + 16), &decoded);
                    QCOMPARE(actual.southWest, south.northWest);
                    QCOMPARE(actual.southEast, south.northEast);
                }
            }

        CMprDocument reopened;
        QVERIFY2(reopened.loadBytes(package.mprBytes, scenario.name + ".mpr", &report),
                 qPrintable(report.toText()));
        CTerrainEditModel reopenedTerrain;
        QVERIFY2(reopenedTerrain.load(reopened, &report), qPrintable(report.toText()));
        for(int y = 0; y < 16; ++y) for(int x = 0; x < 16; ++x)
        {
            bool available = false;
            const quint16 packed = reopenedTerrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            QCOMPARE(packed, finalized.packedCells.at(y * 16 + x));
            bool decoded = false;
            QCOMPARE(finalizedSignature(finalized, packed, &decoded),
                     field.cellSignature(x, y));
            QVERIFY(decoded);
        }

        CTerrainVirtualAtlas repeatAtlas(fixture.catalog, fixture.images, 0x13579u);
        const auto repeat = repeatAtlas.finalize(field, "MatrixMap", 512, {1,2,3,4});
        QCOMPARE(repeat.stableSha256, finalized.stableSha256);
        QCOMPARE(repeat.packedCells, finalized.packedCells);
        QCOMPARE(repeat.pageMmpPayloads, finalized.pageMmpPayloads);
        rawResults.append(QString("STRUCTURAL-PASS %1 hash=%2 slots=%3 generated=%4 visual=unreviewed")
            .arg(scenario.name, QString::fromLatin1(finalized.stableSha256))
            .arg(finalized.requiredSlots).arg(finalized.usage.generatedTiles.size()));
        if(!outputPath.isEmpty())
        {
            QVERIFY(writeVisualScene(outputPath,
                QString("background-%1/%2").arg(scenario.background).arg(scenario.name),
                package.mprBytes, package.resourceArchiveBytes, finalized, 16, 16,
                QString("Background material %1; brush material %2; scenario %3.")
                    .arg(scenario.background).arg(scenario.brush).arg(scenario.name)));
        }
    }
    QCOMPARE(scenarios.size(), 82);
    QVERIFY(authoredCells > 0);
    QVERIFY(generatedCells > 0);
    rawResults.prepend(QString("STRUCTURAL-PASS scenarios=%1 authoredCells=%2 generatedCells=%3 visualAcceptance=0")
        .arg(scenarios.size()).arg(authoredCells).arg(generatedCells));
    if(!outputPath.isEmpty())
    {
        QVERIFY(writeBytes(QDir(outputPath).filePath("MODERN_ATLAS_AUTOTEST_RESULTS.txt"),
                           rawResults.join('\n').append('\n').toUtf8()));
        QVERIFY(writeBytes(QDir(outputPath).filePath("README.md"), QByteArray(
            "# Modern atlas visual scenes\n\n"
            "Each scenario folder contains a complete synthetic `scene.mpr`, its full "
            "`scene-atlas.res`, a directly viewable `preview.png`, and a visual-review card.\n\n"
            "Automated save/reopen and semantic-corner equality are structural evidence only. "
            "They do not constitute visual acceptance. Every image begins as `unreviewed`; mark "
            "it `acceptable`, `no-blend`, `tile-mismatch`, or `wrong-rotation`.\n\n"
            "The procedural textures expose blur and orientation without distributing game art. "
            "Real-map visual fixtures must be generated separately from explicitly authorized "
            "copied source maps and atlas resources.\n")));
    }
}

QTEST_GUILESS_MAIN(TerrainVirtualAtlasTests)
#include "terrain_virtual_atlas_tests.moc"

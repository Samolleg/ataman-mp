#include <QtTest>
#include <QtEndian>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QTransform>
#include <cmath>

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

QVector<qint32> reviewedFixtureTileTypes(int count)
{
    QVector<qint32> types;
    types.reserve(count);
    for(int slot = 0; slot < count; ++slot)
        types.append(slot % 4);
    return types;
}

QMap<qint32, qint32> reviewedFixtureMaterialTypes(
    const CTerrainTransitionCatalog& catalog,
    const QMap<qint32, QImage>& external = {})
{
    QMap<qint32, qint32> types;
    const QVector<qint32> materials = catalog.materialIds();
    for(int index = 0; index < materials.size(); ++index)
        types.insert(materials.at(index), index % 4);
    for(auto source = external.constBegin(); source != external.constEnd(); ++source)
        if(!types.contains(source.key()))
            types.insert(source.key(), qAbs(source.key()) % 4);
    return types;
}

// Synthetic atlas tests use an explicit reviewed class for every synthetic
// material. Production construction remains strict and has no implicit type-0
// fallback.
class CTestTerrainVirtualAtlas : public CTerrainVirtualAtlas
{
public:
    CTestTerrainVirtualAtlas(
        const CTerrainTransitionCatalog& catalog,
        const QVector<QImage>& authoredTiles,
        quint32 distributionSeed = 0,
        const QMap<qint32, QImage>& externalMaterialSources = {},
        const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles = {},
        bool generatedTilesOpaque = true,
        const QVector<qint32>& sourceTileTypes = {},
        const QMap<qint32, qint32>& materialRuntimeTypes = {})
        : CTerrainVirtualAtlas(
              catalog, authoredTiles, distributionSeed,
              externalMaterialSources, materialBlendStyles,
              generatedTilesOpaque,
              sourceTileTypes.isEmpty()
                  ? reviewedFixtureTileTypes(authoredTiles.size())
                  : sourceTileTypes,
              materialRuntimeTypes.isEmpty()
                  ? reviewedFixtureMaterialTypes(catalog, externalMaterialSources)
                  : materialRuntimeTypes),
          m_testSourceTypes(sourceTileTypes.isEmpty()
              ? reviewedFixtureTileTypes(authoredTiles.size())
              : sourceTileTypes),
          m_testRuntimeTypes(materialRuntimeTypes.isEmpty()
              ? reviewedFixtureMaterialTypes(catalog, externalMaterialSources)
              : materialRuntimeTypes)
    {
    }

    STerrainVirtualTile resolve(
        const STerrainTransitionSignature& requested, int cellX, int cellY,
        CValidationReport* report = nullptr,
        qint32 preferredPaintMaterial = 0,
        qint32 preferredRuntimeType = -1)
    {
        return CTerrainVirtualAtlas::resolve(
            requested, cellX, cellY, report,
            preferredPaintMaterial != 0
                ? preferredPaintMaterial : requested.northWest,
            preferredRuntimeType);
    }

    STerrainAtlasFinalization finalize(
        const CTerrainVirtualMaterialField& field,
        const QString& sourceNamespace,
        int capacityLimit = kValidatedShippedTerrainAtlasSlotLimit,
        const QVector<qint32>& sourceTileTypes = {},
        CValidationReport* report = nullptr,
        const QVector<quint16>& pinnedPackedTiles = {},
        const QVector<qint32>& preferredCellTypes = {})
    {
        QVector<qint32> cellTypes = preferredCellTypes;
        if(cellTypes.isEmpty())
            for(int y = 0; y < field.tileHeight(); ++y)
                for(int x = 0; x < field.tileWidth(); ++x)
                {
                    bool found = false;
                    const STerrainTransitionSignature cell =
                        field.cellSignature(x, y, &found);
                    cellTypes.append(found
                        ? m_testRuntimeTypes.value(cell.northWest, -1) : -1);
                }
        return CTerrainVirtualAtlas::finalize(
            field, sourceNamespace, capacityLimit,
            sourceTileTypes.isEmpty() ? m_testSourceTypes : sourceTileTypes,
            report, pinnedPackedTiles, cellTypes);
    }

private:
    QVector<qint32> m_testSourceTypes;
    QMap<qint32, qint32> m_testRuntimeTypes;
};

class CTestTerrainWorkingAtlas : public CTerrainWorkingAtlas
{
public:
    CTestTerrainWorkingAtlas(
        const CTerrainTransitionCatalog& catalog,
        const QVector<QImage>& authoredTiles,
        quint32 distributionSeed = 0,
        int capacityLimit = 512,
        const QMap<qint32, QImage>& externalMaterialSources = {},
        const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles = {},
        const QVector<qint32>& sourceTileTypes = {},
        const QMap<qint32, qint32>& materialRuntimeTypes = {})
        : CTerrainWorkingAtlas(
              catalog, authoredTiles, distributionSeed, capacityLimit,
              externalMaterialSources, materialBlendStyles,
              sourceTileTypes.isEmpty()
                  ? reviewedFixtureTileTypes(authoredTiles.size())
                  : sourceTileTypes,
              materialRuntimeTypes.isEmpty()
                  ? reviewedFixtureMaterialTypes(catalog, externalMaterialSources)
                  : materialRuntimeTypes)
    {
    }


    quint16 resolve(const STerrainTransitionSignature& requested,
                    int cellX, int cellY,
                    QMap<int, quint64>* workingUsage,
                    const QSet<int>& protectedSlots,
                    bool* generated = nullptr,
                    CValidationReport* report = nullptr,
                    qint32 preferredPaintMaterial = 0)
    {
        return CTerrainWorkingAtlas::resolve(
            requested, cellX, cellY, workingUsage, protectedSlots,
            generated, report,
            preferredPaintMaterial != 0
                ? preferredPaintMaterial : requested.northWest);
    }
};

template<typename TAtlas>
void seedReviewedStockMasks(TAtlas& atlas, const Fixture& fixture)
{
    const QVector<STerrainTransitionSignature> patterns = {
        signature(3, 4, 4, 4), signature(3, 3, 4, 4),
        signature(3, 4, 3, 4)
    };
    for(int patternIndex = 0; patternIndex < patterns.size(); ++patternIndex)
    {
        const STerrainTransitionSignature preview =
            CTerrainTransitionCatalog::mirroredGridY(patterns.at(patternIndex));
        const QVector<qint32> ids = {preview.northWest, preview.northEast,
            preview.southEast, preview.southWest};
        const int size = fixture.images.front().width();
        QImage transition(size, size, QImage::Format_RGBA8888);
        for(int y = 0; y < size; ++y) for(int x = 0; x < size; ++x)
        {
            const double u = size == 1 ? 0.0 : double(x) / double(size - 1);
            const double v = size == 1 ? 0.0 : double(y) / double(size - 1);
            const double weights[4] = {(1-u)*(1-v), u*(1-v), u*v, (1-u)*v};
            double owner = 0.0;
            for(int corner = 0; corner < 4; ++corner)
                if(ids.at(corner) == 3) owner += weights[corner];
            const double authoredContour = 0.5
                + 0.10 * std::sin((u * 3.0 + v * 1.5) * 6.28318530718)
                + 0.05 * std::sin((v * 7.0 - u * 2.0) * 6.28318530718);
            transition.setPixelColor(x, y, owner >= authoredContour
                ? fixture.images.at(2).pixelColor(x, y)
                : fixture.images.at(3).pixelColor(x, y));
        }
        QVERIFY(atlas.addStockBlendMaskTemplate(patterns.at(patternIndex),
            transition, QStringLiteral("reviewed-stock-fixture:%1")
                .arg(patternIndex)));
    }
}

template<typename TAtlas>
void seedReviewedPairMasks(TAtlas& atlas, qint32 owner, qint32 other,
                           const QImage& ownerPixels,
                           const QImage& otherPixels)
{
    const QVector<STerrainTransitionSignature> patterns = {
        signature(owner, other, other, other),
        signature(owner, owner, other, other),
        signature(owner, other, owner, other)
    };
    for(int patternIndex = 0; patternIndex < patterns.size(); ++patternIndex)
    {
        const STerrainTransitionSignature preview =
            CTerrainTransitionCatalog::mirroredGridY(patterns.at(patternIndex));
        const QVector<qint32> ids = {preview.northWest, preview.northEast,
            preview.southEast, preview.southWest};
        QImage transition(ownerPixels.size(), QImage::Format_RGBA8888);
        for(int y = 0; y < transition.height(); ++y)
            for(int x = 0; x < transition.width(); ++x)
            {
                const double u = transition.width() == 1 ? 0.0
                    : double(x) / double(transition.width() - 1);
                const double v = transition.height() == 1 ? 0.0
                    : double(y) / double(transition.height() - 1);
                const double weights[4] = {(1-u)*(1-v), u*(1-v),
                                            u*v, (1-u)*v};
                double amount = 0.0;
                for(int corner = 0; corner < 4; ++corner)
                    if(ids.at(corner) == owner) amount += weights[corner];
                const double contour = 0.5
                    + 0.12 * std::sin((u * 4.0 + v * 1.25) * 6.28318530718);
                transition.setPixelColor(x, y, amount >= contour
                    ? ownerPixels.pixelColor(x, y)
                    : otherPixels.pixelColor(x, y));
            }
        QVERIFY(atlas.addStockBlendMaskTemplate(patterns.at(patternIndex),
            transition, QStringLiteral("reviewed-stock-pair:%1")
                .arg(patternIndex)));
    }
}

QByteArray portableMpr(bool withInactiveOverlay = false,
                       bool withAnimatedRange = false)
{
    QByteArray mp;
    QDataStream stream(&mp,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream<<quint32(0xCE4AF672)<<float(100.0f)<<quint32(1)<<quint32(1)
          <<quint32(4)<<quint32(64)<<quint32(4)<<quint32(8)
          <<quint16(0)<<quint32(withAnimatedRange ? 1 : 0);
    stream<<qint32(1)<<qint32(2)<<qint32(3)<<qint32(4);
    if(withAnimatedRange)
        stream<<quint16(0)<<quint16(4);
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
    void missingStockMaskNeverFallsBackToProceduralArtwork();
    void authoredMapMasksAreAvailableBeforeFirstStroke();
    void selectedStockMasksAreReviewableAndDeterministic();
    void generatesOneThreeTuple();
    void generatesTwoTwoTuple();
    void generatesFourFamilyTupleWithOrientation();
    void generatedEdgesUseOnlyTheirTwoCornerMaterials();
    void generatedTransitionRetainsSourceTextureIdentity();
    void generatedTransitionBoundaryIsOrganicRatherThanQuadrants();
    void generatedLandBlendCannotPunchTransparentHoles();
    void generatedMultiFamilyBlendHasNoZeroWeightHoles();
    void generatedOverlayBlendPreservesAuthoredAlpha();
    void stockMaskTransferRemovesPixelSizedIslandsAndPinholes();
    void stockMaskTransferRemovesLargeDetachedInsideCornerBlocks();
    void stockMaskTransferPreservesSubstantialAuthoredFragments();
    void stockMaskTransferPreservesNativeEdgeDetail();
    void degenerateStockMasksAreRejected();
    void reviewedBadPavingCutIsNotSelected();
    void materialNamesSelectExplicitBlendStyles();
    void materialBlendStylesProvideBoundedDistinctVariants();
    void generatedNeighbourEdgesArePixelContinuous();
    void rotationsAndIdenticalPixelsAreDeduplicated();
    void generatedTerrainTypeUsesMajorityAcrossRotation();
    void generatedTerrainTypeTieUsesPaintedMaterialAndTypedDedup();
    void unknownGeneratedTerrainTypeIsRefused();
    void workingAtlasCarriesGeneratedTerrainType();
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
    void workingPreviewPackageStagesSeveralGeneratedPages();
    void virtualPaintPreviewAndUndoPreserveDesiredIdentity();
    void overlayVirtualPaintAllowsSparseInactiveField();
    void uniformStrokeUsesOneAuthoredVariationAcrossItsInterior();
    void preservedLayerSlotsArePinnedAndRemapped();
    void inactiveOverlaySentinelSurvivesLandFinalization();
    void gradedAlphaIsRefusedWithoutFlattening();
    void dxt1TerrainPagesMatchShippedMipContract();
    void dxt1AtlasTileDirectDecodeMatchesFullPageOrientation();
    void dxt1AtlasTileCacheReadsEachPageOnce();
    void workingAtlasGeneratesImmediatelyAndReusesTuple();
    void expanded4096AtlasRetainsWorkingExtensionCapacity();
    void workingAtlasNeverReplacesAuthoredPixels();
    void repeatedGeneratedStampsNeverMutateAuthoredAtlas();
    void workingAtlasProtectsUndoSlotsAndRefusesCapacity();
    void externalUniformFamilyPaintsAndBlendsWithoutAuthoredSlot();
    void selfPaintPreservesExactPackedTileAndRotation();
    void parallelStripesAndBaseCrossResolveExactly();
    void nestedCircleMaterialPairingsResolveExactly();
    void circularPaintsKeepEveryBrushIdentityInsideAndAtOuterEdge();
    void generatedSlotsNeverInheritCompactedAnimationRanges();
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images,7);
    seedReviewedStockMasks(atlas, fixture);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 19u);
    seedReviewedStockMasks(atlas, fixture);
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

void TerrainVirtualAtlasTests::missingStockMaskNeverFallsBackToProceduralArtwork()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 19u);
    CValidationReport report;
    const STerrainVirtualTile unresolved = atlas.resolve(
        signature(1, 2, 2, 2), 0, 0, &report);
    QCOMPARE(unresolved.generatedIndex, -1);
    QVERIFY(report.hasErrors());
    QVERIFY(report.toText().contains("No reviewed stock JMV mask"));
    QVERIFY(atlas.generatedTiles().isEmpty());
}

void TerrainVirtualAtlasTests::authoredMapMasksAreAvailableBeforeFirstStroke()
{
    Fixture fixture;
    const QVector<STerrainTransitionSignature> stockPatterns = {
        signature(3, 4, 4, 4), signature(3, 3, 4, 4),
        signature(3, 4, 3, 4)
    };
    for(const STerrainTransitionSignature& pattern : stockPatterns)
    {
        const STerrainTransitionSignature preview =
            CTerrainTransitionCatalog::mirroredGridY(pattern);
        const QVector<qint32> ids = {preview.northWest, preview.northEast,
            preview.southEast, preview.southWest};
        QImage pixels(8, 8, QImage::Format_RGBA8888);
        for(int y = 0; y < pixels.height(); ++y)
            for(int x = 0; x < pixels.width(); ++x)
            {
                const double u = double(x) / double(pixels.width() - 1);
                const double v = double(y) / double(pixels.height() - 1);
                const double weights[4] = {(1-u)*(1-v), u*(1-v),
                                            u*v, (1-u)*v};
                double owner = 0.0;
                for(int corner = 0; corner < 4; ++corner)
                    if(ids.at(corner) == 3) owner += weights[corner];
                pixels.setPixelColor(x, y, owner >= 0.5
                    ? fixture.images.at(2).pixelColor(x, y)
                    : fixture.images.at(3).pixelColor(x, y));
            }
        const int slot = fixture.images.size();
        fixture.images.append(pixels);
        STerrainTransitionVariant variant;
        variant.signature = pattern;
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(fixture.catalog.addVariant(variant));
    }

    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 19u);
    for(const STerrainTransitionSignature& requested : {
            signature(1, 2, 2, 2), signature(1, 1, 2, 2),
            signature(1, 2, 1, 2)})
    {
        QVERIFY(atlas.stockBlendMaskCountFor(requested) > 0);
        CValidationReport report;
        const STerrainVirtualTile resolved = atlas.resolve(
            requested, 0, 0, &report);
        QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
        QVERIFY(resolved.generatedIndex >= 0);
    }
}

void TerrainVirtualAtlasTests::selectedStockMasksAreReviewableAndDeterministic()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 19u);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainTransitionSignature requested = signature(1, 2, 2, 2);
    QString firstSource;
    QString secondSource;
    const QImage first = atlas.reviewBlendMask(
        requested, 1, 0, &firstSource);
    const QImage repeated = atlas.reviewBlendMask(
        requested, 1, 0, &secondSource);
    QVERIFY(!first.isNull());
    QCOMPARE(first, repeated);
    QCOMPARE(firstSource, secondSource);
    QVERIFY(firstSource.startsWith(QStringLiteral("reviewed-stock-fixture:")));
    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(requested);
    const QPoint points[4] = {{0, 0}, {first.width() - 1, 0},
        {first.width() - 1, first.height() - 1}, {0, first.height() - 1}};
    const qint32 corners[4] = {preview.northWest, preview.northEast,
        preview.southEast, preview.southWest};
    for(int corner = 0; corner < 4; ++corner)
        QCOMPARE(first.pixelColor(points[corner]).value(),
                 corners[corner] == 1 ? 255 : 0);
    QVERIFY(atlas.reviewBlendMask(signature(1, 2, 3, 2), 1).isNull());
}

void TerrainVirtualAtlasTests::generatesOneThreeTuple()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,2,2),0,0);
    QVERIFY(!tile.authored);
    QVERIFY(tile.generatedIndex>=0);
    QCOMPARE(atlas.generatedTiles().size(),1);
}

void TerrainVirtualAtlasTests::generatesTwoTwoTuple()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    QVERIFY(atlas.resolve(signature(1,1,2,2),0,0).generatedIndex>=0);
}

void TerrainVirtualAtlasTests::generatesFourFamilyTupleWithOrientation()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,3,4),0,0);
    const QImage image=atlas.generatedTiles().at(tile.generatedIndex).pixels;
    const QColor top=image.pixelColor(3,0);
    // The top edge contains only its yellow/blue endpoint materials, but the
    // transition is broad enough to be visible instead of a 90-degree cut.
    QVERIFY(top.red()>100 && top.green()>100 && top.blue()>60);
    const QColor bottom=image.pixelColor(3,7);
    // The opposite edge contains only red/green and therefore no blue leak.
    QVERIFY(bottom.red()>100 && bottom.green()>60 && bottom.blue()<40);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    seedReviewedStockMasks(atlas, fixture);
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
    // Preserve the authored source texture over most of the tile, while
    // retaining a substantial (not one- or two-pixel) transition band.
    QVERIFY(sourceLike >= 48);
    QVERIFY(genuinelyMixed >= 8);
    QVERIFY(genuinelyMixed <= 16);
}

void TerrainVirtualAtlasTests::generatedTransitionBoundaryIsOrganicRatherThanQuadrants()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 73);
    seedReviewedStockMasks(atlas, fixture);
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

void TerrainVirtualAtlasTests::generatedLandBlendCannotPunchTransparentHoles()
{
    Fixture fixture;
    for(int y = 0; y < 8; ++y)
        for(int x = 0; x < 8; ++x)
        {
            fixture.images[0].setPixelColor(x, y, ((x + y) % 3)
                ? QColor(210, 35, 20, 255) : QColor(0, 0, 0, 0));
            fixture.images[1].setPixelColor(x, y, ((x * 3 + y) % 4)
                ? QColor(30, 190, 45, 255) : QColor(0, 0, 0, 0));
        }
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainVirtualTile tile = atlas.resolve(
        signature(1, 1, 2, 2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(
        tile.generatedIndex).pixels;
    for(int y = 0; y < generated.height(); ++y)
        for(int x = 0; x < generated.width(); ++x)
        {
            const QColor pixel = generated.pixelColor(x, y);
            QCOMPARE(pixel.alpha(), 255);
            // Both uniform sources have low blue. A sky-cyan hole or an
            // out-of-family channel can therefore never be synthesized.
            QVERIFY(pixel.blue() <= 45);
            QVERIFY(pixel.red() >= 30 && pixel.red() <= 210);
            QVERIFY(pixel.green() >= 35 && pixel.green() <= 190);
        }
}

void TerrainVirtualAtlasTests::generatedMultiFamilyBlendHasNoZeroWeightHoles()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainVirtualTile tile = atlas.resolve(
        signature(1, 2, 3, 4), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(
        tile.generatedIndex).pixels;
    for(int y = 0; y < generated.height(); ++y)
        for(int x = 0; x < generated.width(); ++x)
        {
            const QColor pixel = generated.pixelColor(x, y);
            QCOMPARE(pixel.alpha(), 255);
            QVERIFY(pixel.red() + pixel.green() + pixel.blue() > 40);
        }
}

void TerrainVirtualAtlasTests::generatedOverlayBlendPreservesAuthoredAlpha()
{
    Fixture fixture;
    fixture.images[0].fill(QColor(210, 35, 20, 64));
    fixture.images[1].fill(QColor(30, 190, 45, 192));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0,
        QMap<qint32, QImage>(), QMap<qint32, ETerrainBlendStyle>(), false);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainVirtualTile tile = atlas.resolve(
        signature(1, 1, 2, 2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(
        tile.generatedIndex).pixels;
    int translucent = 0;
    for(int y = 0; y < generated.height(); ++y)
        for(int x = 0; x < generated.width(); ++x)
        {
            const int alpha = generated.pixelColor(x, y).alpha();
            QVERIFY(alpha >= 64 && alpha <= 192);
            translucent += alpha < 255;
        }
    QCOMPARE(translucent, generated.width() * generated.height());
}

void TerrainVirtualAtlasTests::stockMaskTransferRemovesPixelSizedIslandsAndPinholes()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    QImage noisy(64, 64, QImage::Format_RGBA8888);
    for(int y = 0; y < 64; ++y)
        for(int x = 0; x < 64; ++x)
            noisy.setPixelColor(x, y, x < 32
                ? fixture.images[0].pixelColor(x, y)
                : fixture.images[1].pixelColor(x, y));
    // These deliberately model the 1-2 pixel grain reported in generated
    // masks. Neither has enough area to survive the transferred silhouette.
    noisy.setPixelColor(8, 31, fixture.images[1].pixelColor(8, 31));
    noisy.setPixelColor(54, 30, fixture.images[0].pixelColor(54, 30));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 1, 2, 2),
        noisy, QStringLiteral("grain-regression")));
    const STerrainVirtualTile tile = atlas.resolve(
        signature(1, 1, 2, 2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(
        tile.generatedIndex).pixels;
    QVERIFY(generated.pixelColor(8, 31).red()
            > generated.pixelColor(8, 31).green());
    QVERIFY(generated.pixelColor(54, 30).green()
            > generated.pixelColor(54, 30).red());
}

void TerrainVirtualAtlasTests::stockMaskTransferRemovesLargeDetachedInsideCornerBlocks()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    QImage malformed(64, 64, QImage::Format_RGBA8888);
    malformed.fill(fixture.images[1].pixelColor(0, 0));
    // Grid Y mirroring places this signature's owner at preview SW.
    for(int y = 34; y < 64; ++y)
        for(int x = 0; x < 30; ++x)
            malformed.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
    // This sizeable detached owner block models the square/cross artifact in
    // a concave circle boundary. It is too large for an area-only filter but
    // is invalid because no requested owner corner reaches it.
    for(int y = 10; y < 24; ++y)
        for(int x = 34; x < 48; ++x)
            malformed.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 2, 2, 2),
        malformed, QStringLiteral("detached-inside-corner-regression")));
    const STerrainVirtualTile tile = atlas.resolve(
        signature(1, 2, 2, 2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(
        tile.generatedIndex).pixels;
    QVERIFY2(generated.pixelColor(40, 16).green()
             > generated.pixelColor(40, 16).red(),
             "Detached inside-corner owner block survived mask transfer");
}

void TerrainVirtualAtlasTests::stockMaskTransferPreservesSubstantialAuthoredFragments()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    QImage authored(64, 64, QImage::Format_RGBA8888);
    authored.fill(fixture.images[1].pixelColor(0, 0));
    for(int y = 34; y < 64; ++y)
        for(int x = 0; x < 30; ++x)
            authored.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
    // Stock ice chips, grass strands and loose gravel are often detached from
    // the principal corner-owned mass. This is deliberate authored shape, not
    // the one-pixel DXT grain removed by the neighbouring regression.
    for(int y = 18; y < 24; ++y)
        for(int x = 38; x < 44; ++x)
            authored.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 2, 2, 2),
        authored, QStringLiteral("authored-fragment-regression")));
    const QImage mask = atlas.reviewBlendMask(signature(1, 2, 2, 2), 1);
    QVERIFY(!mask.isNull());
    QVERIFY2(mask.pixelColor(40, 20).value() >= 128,
             "Substantial authored material fragment was erased");
}

void TerrainVirtualAtlasTests::stockMaskTransferPreservesNativeEdgeDetail()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(256, 256, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    QImage authored(256, 256, QImage::Format_RGBA8888);
    for(int y = 0; y < authored.height(); ++y)
        for(int x = 0; x < authored.width(); ++x)
        {
            const int serratedEdge = 112 + ((y / 4) % 2) * 12;
            authored.setPixelColor(x, y, x < serratedEdge
                ? fixture.images[0].pixelColor(x, y)
                : fixture.images[1].pixelColor(x, y));
        }
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 1, 2, 2),
        authored, QStringLiteral("native-detail-regression")));
    const QImage mask = atlas.reviewBlendMask(signature(1, 1, 2, 2), 1);
    QVERIFY(!mask.isNull());
    int blurredPixels = 0;
    for(int y = 0; y < mask.height(); ++y)
        for(int x = 0; x < mask.width(); ++x)
        {
            const int value = mask.pixelColor(x, y).value();
            blurredPixels += value > 8 && value < 247;
        }
    QVERIFY2(blurredPixels < 512,
             "Full-resolution stock mask was softened like an upscaled thumbnail");
}

void TerrainVirtualAtlasTests::degenerateStockMasksAreRejected()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    QImage allOwner(64, 64, QImage::Format_RGBA8888);
    allOwner.fill(fixture.images[0].pixelColor(0, 0));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 1, 2, 2),
        allOwner, QStringLiteral("all-white-regression")));
    QCOMPARE(atlas.stockBlendMaskCountFor(signature(1, 1, 2, 2)), 0);
}

void TerrainVirtualAtlasTests::reviewedBadPavingCutIsNotSelected()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Paving}, {2, ETerrainBlendStyle::Paving}
    };
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0, {}, styles);
    auto transition = [&](bool organic) {
        QImage image(64, 64, QImage::Format_RGBA8888);
        for(int y = 0; y < 64; ++y)
            for(int x = 0; x < 64; ++x)
            {
                const int edge = organic ? 28 + int(7.0 * std::sin(y * 0.31)) : 32;
                image.setPixelColor(x, y, x < edge
                    ? fixture.images[0].pixelColor(x, y)
                    : fixture.images[1].pixelColor(x, y));
            }
        return image;
    };
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 1, 2, 2),
        transition(false), QStringLiteral("Zone10:114")));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 1, 2, 2),
        transition(true), QStringLiteral("Zone10:200")));
    QString provenance;
    QVERIFY(!atlas.reviewBlendMask(signature(1, 1, 2, 2), 1, 0,
                                   &provenance).isNull());
    QCOMPARE(provenance, QStringLiteral("Zone10:200"));
}

void TerrainVirtualAtlasTests::materialNamesSelectExplicitBlendStyles()
{
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Grass / Dark"),
             ETerrainBlendStyle::Grass);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Ground / Dirt"),
             ETerrainBlendStyle::Soil);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Ground / Gravel"),
             ETerrainBlendStyle::Gravel);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Ground / Water"),
             ETerrainBlendStyle::Water);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Ice / Cracked"),
             ETerrainBlendStyle::Ice);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Lava"),
             ETerrainBlendStyle::Lava);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Paving / Radial"),
             ETerrainBlendStyle::Paving);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Slabs / Carved"),
             ETerrainBlendStyle::Paving);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Rock / Snow"),
             ETerrainBlendStyle::Snow);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Stone / Weathered"),
             ETerrainBlendStyle::Stone);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Rubble / Red"),
             ETerrainBlendStyle::Gravel);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Sand / Gravel"),
             ETerrainBlendStyle::Sand);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Snow / Common"),
             ETerrainBlendStyle::Snow);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Swamp"),
             ETerrainBlendStyle::Swamp);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Water / Waves"),
             ETerrainBlendStyle::Water);
    QCOMPARE(CTestTerrainVirtualAtlas::blendStyleForMaterialName("Future material"),
             ETerrainBlendStyle::Organic);
}

void TerrainVirtualAtlasTests::materialBlendStylesProvideBoundedDistinctVariants()
{
    QCOMPARE(CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,1,1,1)), 1);
    QCOMPARE(CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,1,1,2)), 2);
    QCOMPARE(CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,1,2,2)), 2);
    QCOMPARE(CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,2,1,2)), 1);
    QCOMPARE(CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,2,2,3)), 1);
    QCOMPARE(CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,2,3,4)), 1);

    Fixture boundedFixture;
    CTestTerrainVirtualAtlas bounded(boundedFixture.catalog,
                                 boundedFixture.images, 0x51a7u);
    seedReviewedStockMasks(bounded, boundedFixture);
    for(int y = 0; y < 20; ++y) for(int x = 0; x < 20; ++x)
        QVERIFY(bounded.resolve(signature(1,2,2,2), x, y).generatedIndex >= 0);
    QVERIFY(bounded.generatedTiles().size() >= 1);
    QVERIFY(bounded.generatedTiles().size()
            <= CTestTerrainVirtualAtlas::blendVariationLimit(signature(1,2,2,2)));
    for(const STerrainFinalizedTile& tile : bounded.generatedTiles())
    {
        QVERIFY(tile.stockDerivedBlend);
        QVERIFY(tile.blendProvenance.startsWith("reviewed-stock-fixture:"));
    }

    Fixture fixture;
    CTestTerrainVirtualAtlas threeWay(fixture.catalog, fixture.images, 0x31u);
    CTestTerrainVirtualAtlas fourWay(fixture.catalog, fixture.images, 0x41u);
    seedReviewedStockMasks(threeWay, fixture);
    seedReviewedStockMasks(fourWay, fixture);
    for(int y = 0; y < 16; ++y)
        for(int x = 0; x < 16; ++x)
        {
            QVERIFY(threeWay.resolve(signature(1,2,2,3), x, y)
                        .generatedIndex >= 0);
            QVERIFY(fourWay.resolve(signature(1,2,3,4), x, y)
                        .generatedIndex >= 0);
        }
    QCOMPARE(threeWay.generatedTiles().size(), 1);
    QCOMPARE(fourWay.generatedTiles().size(), 1);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    seedReviewedStockMasks(atlas, fixture);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainTransitionSignature base=signature(1,2,3,4);
    const STerrainVirtualTile first=atlas.resolve(base,0,0,nullptr,1);
    const STerrainVirtualTile rotated=atlas.resolve(
        CTerrainTransitionCatalog::rotated(base,1),0,0,nullptr,1);
    QCOMPARE(first.generatedIndex,rotated.generatedIndex);
    QVERIFY(first.generatedRotation!=rotated.generatedRotation);

    Fixture duplicate(true);
    CTestTerrainVirtualAtlas duplicateAtlas(duplicate.catalog,duplicate.images);
    seedReviewedStockMasks(duplicateAtlas, duplicate);
    const int a=duplicateAtlas.resolve(signature(1,2,3,3),0,0).generatedIndex;
    const int b=duplicateAtlas.resolve(signature(1,2,4,4),0,0).generatedIndex;
    QCOMPARE(a,b);
}

void TerrainVirtualAtlasTests::generatedTerrainTypeUsesMajorityAcrossRotation()
{
    Fixture fixture;
    const QVector<qint32> sourceTypes = {2, 7, 7, 9};
    const QMap<qint32, qint32> materialTypes = {
        {1, 2}, {2, 7}, {3, 7}, {4, 9}};
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 17, {}, {},
                               true, sourceTypes, materialTypes);
    seedReviewedPairMasks(atlas, 1, 2, fixture.images.at(0),
                          fixture.images.at(1));
    const STerrainTransitionSignature requested = signature(1, 2, 2, 2);
    CValidationReport report;
    const STerrainVirtualTile first = atlas.resolve(
        requested, 3, 5, &report, 1);
    const STerrainVirtualTile rotated = atlas.resolve(
        CTerrainTransitionCatalog::rotated(requested, 1), 3, 5,
        &report, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(first.runtimeTerrainType, qint32(7));
    QCOMPARE(rotated.runtimeTerrainType, qint32(7));
    QCOMPARE(first.generatedIndex, rotated.generatedIndex);
}

void TerrainVirtualAtlasTests::generatedTerrainTypeTieUsesPaintedMaterialAndTypedDedup()
{
    Fixture fixture(true);
    const QVector<qint32> sourceTypes = {5, 6, 6, 6};
    const QMap<qint32, qint32> materialTypes = {
        {1, 5}, {2, 6}, {3, 6}, {4, 6}};
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 23, {}, {},
                               true, sourceTypes, materialTypes);
    seedReviewedPairMasks(atlas, 1, 2, fixture.images.at(0),
                          fixture.images.at(1));
    const STerrainTransitionSignature tied = signature(1, 1, 2, 2);
    CValidationReport report;
    const STerrainVirtualTile paintedOne = atlas.resolve(
        tied, 4, 9, &report, 1);
    const STerrainVirtualTile paintedTwo = atlas.resolve(
        tied, 4, 9, &report, 2);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(paintedOne.runtimeTerrainType, qint32(5));
    QCOMPARE(paintedTwo.runtimeTerrainType, qint32(6));
    QVERIFY(paintedOne.generatedIndex != paintedTwo.generatedIndex);
    QCOMPARE(atlas.generatedTiles().at(paintedOne.generatedIndex).pixels,
             atlas.generatedTiles().at(paintedTwo.generatedIndex).pixels);
}

void TerrainVirtualAtlasTests::unknownGeneratedTerrainTypeIsRefused()
{
    Fixture fixture;
    CTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    seedReviewedPairMasks(atlas, 1, 2, fixture.images.at(0),
                          fixture.images.at(1));
    CValidationReport report;
    const STerrainVirtualTile unresolved = atlas.resolve(
        signature(1, 2, 2, 2), 0, 0, &report, 1);
    QCOMPARE(unresolved.generatedIndex, -1);
    QVERIFY(report.hasErrors());
    QVERIFY(report.toText().contains(QStringLiteral("no reviewed runtime terrain type"),
                                     Qt::CaseInsensitive));
}

void TerrainVirtualAtlasTests::workingAtlasCarriesGeneratedTerrainType()
{
    Fixture fixture;
    const QVector<qint32> sourceTypes = {2, 7, 7, 9};
    const QMap<qint32, qint32> materialTypes = {
        {1, 2}, {2, 7}, {3, 7}, {4, 9}};
    CTerrainWorkingAtlas working(fixture.catalog, fixture.images, 29, 16,
                                 {}, {}, sourceTypes, materialTypes);
    seedReviewedPairMasks(working, 1, 2, fixture.images.at(0),
                          fixture.images.at(1));
    QMap<int, quint64> usage;
    CValidationReport report;
    const quint16 packed = working.resolve(
        signature(1, 2, 2, 2), 1, 2, &usage, {}, nullptr, &report, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    const int slot = SMprPackedTile::decode(packed).atlasSlot();
    QVERIFY(slot >= fixture.images.size());
    QCOMPARE(working.runtimeTerrainTypeForSlot(slot), qint32(7));
    QCOMPARE(working.activeTileTypes().at(slot), qint32(7));
}

void TerrainVirtualAtlasTests::stableHashesAndNamespaceAreDeterministic()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    field.setMaterial(1,0,2); field.setMaterial(1,1,3); field.setMaterial(0,1,4);
    CTestTerrainVirtualAtlas first(fixture.catalog,fixture.images,42);
    CTestTerrainVirtualAtlas second(fixture.catalog,fixture.images,42);
    seedReviewedStockMasks(first, fixture);
    seedReviewedStockMasks(second, fixture);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainAtlasFinalization result=atlas.finalize(field,"Zone2",512,{10,20,30,40});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.usage.cellCount,quint64(4));
    // No authored source slot is used by this field. The dense result may
    // still contain two artistic variants of its one canonical 1/3 tuple,
    // but all four original authored slots must have been evicted.
    QVERIFY(result.authoredSlotRemap.isEmpty());
    QVERIFY(result.requiredSlots <= 2);
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
        tileTypes.append(slot % 16);
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
    CTestTerrainVirtualAtlas atlas(catalog, images);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
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
    const QImage original = CTestTerrainVirtualAtlas::composePage(tiles, 0, 8);
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
    const QImage changed = CTestTerrainVirtualAtlas::composePage(tiles, 0, 8);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
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
            CTestTerrainVirtualAtlas::composePage(tiles, page * 64, 8),
            true, &diagnostic);
        QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
        authoredPages.append(payload);
    }
    tiles[4 * 64].fill(QColor(220, 30, 170, 255));
    QVector<qint32> tileTypes(tiles.size(), 0);
    tileTypes[4 * 64] = 7;
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedSource, tiles, tileTypes, authoredPages, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes, "working-result.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().textureCount, quint32(5));
    QCOMPARE(reopened.mapInfo().tileCount, quint32(320));
    QCOMPARE(reopened.mapInfo().tileTypes.at(4 * 64), qint32(7));
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

void TerrainVirtualAtlasTests::workingPreviewPackageStagesSeveralGeneratedPages()
{
    CMprDocument source;
    CValidationReport report;
    QVERIFY2(source.loadBytes(portableMpr(), "working-multipage-source.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(source, &report), qPrintable(report.toText()));

    constexpr int authoredPages = 4;
    constexpr int generatedPages = 4;
    constexpr int totalPages = authoredPages + generatedPages;
    terrain.enableWorkingAtlasSlots(totalPages * 64);
    STerrainTileCommand command;
    for(int cell = 0; cell < generatedPages * 64; ++cell)
    {
        const int x = cell % 16;
        const int y = cell / 16;
        bool available = false;
        const quint16 before = terrain.packedTile(
            x, y, ETerrainTileLayer::Land, &available);
        QVERIFY(available);
        SMprPackedTile extension;
        const int slot = authoredPages * 64 + cell;
        extension.atlasIndex = quint8(slot / 64);
        extension.tileIndex = quint8(slot % 64);
        STerrainTileDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.layer = ETerrainTileLayer::Land;
        delta.before = before;
        delta.after = extension.encode();
        command.deltas.append(delta);
    }
    QVERIFY2(terrain.apply(command, true, &report), qPrintable(report.toText()));
    CMprEditTransaction transaction(QStringLiteral(
        "Reference several generated working-atlas pages"));
    QVERIFY2(terrain.appendSectorEdits(&transaction, &report),
             qPrintable(report.toText()));
    QByteArray paintedBytes;
    QVERIFY2(source.applyTransaction(transaction, &paintedBytes, &report),
             qPrintable(report.toText()));
    CMprDocument paintedSource;
    QVERIFY2(paintedSource.loadBytes(
                 paintedBytes, "working-multipage-painted.mpr", &report),
             qPrintable(report.toText()));

    QVector<QImage> tiles(totalPages * 64);
    for(int slot = 0; slot < tiles.size(); ++slot)
    {
        QImage tile(8, 8, QImage::Format_RGBA8888);
        for(int y = 0; y < tile.height(); ++y)
            for(int x = 0; x < tile.width(); ++x)
                tile.setPixelColor(x, y, QColor(
                    (slot * 13 + x * 29) & 255,
                    (slot * 31 + y * 17) & 255,
                    (slot * 47 + x * 7 + y * 11) & 255,
                    255));
        tiles[slot] = tile;
    }
    QVector<QByteArray> authoredPayloads;
    for(int page = 0; page < authoredPages; ++page)
    {
        QString diagnostic;
        const QByteArray payload = ei::encodeMmpDxt1(
            CTestTerrainVirtualAtlas::composePage(tiles, page * 64, 8),
            true, &diagnostic);
        QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
        authoredPayloads.append(payload);
    }
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedSource, tiles,
            QVector<qint32>(tiles.size(), qint32(2)),
            authoredPayloads, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + "\n" + report.toText()));
    QCOMPARE(package.resourceMembers.size(), totalPages);

    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
                                "working-multipage-result.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().textureCount, quint32(totalPages));
    QCOMPARE(reopened.mapInfo().tileCount, quint32(totalPages * 64));
    CTerrainEditModel reopenedTerrain;
    QVERIFY2(reopenedTerrain.load(reopened, &report), qPrintable(report.toText()));
    for(int cell = 0; cell < generatedPages * 64; ++cell)
    {
        bool available = false;
        const quint16 packed = reopenedTerrain.packedTile(
            cell % 16, cell / 16, ETerrainTileLayer::Land, &available);
        QVERIFY(available);
        QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(),
                 authoredPages * 64 + cell);
    }

    CResArchiveDocument resources;
    QStringList errors;
    QVERIFY2(resources.loadBytes(package.resourceArchiveBytes,
                                 "working-multipage-result.res", &errors),
             qPrintable(errors.join('\n')));
    for(int page = 0; page < authoredPages; ++page)
        QCOMPARE(resources.memberPayload(package.resourceMembers.at(page)),
                 authoredPayloads.at(page));
    for(int page = authoredPages; page < totalPages; ++page)
    {
        const ei::SMmpInspection appended = ei::inspectMmp(
            resources.memberPayload(package.resourceMembers.at(page)));
        QVERIFY2(appended.valid, qPrintable(appended.diagnostic));
    }
}

void TerrainVirtualAtlasTests::namespacesAreIsolated()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1,1,1));
    CTestTerrainVirtualAtlas a(fixture.catalog,fixture.images);
    CTestTerrainVirtualAtlas b(fixture.catalog,fixture.images);
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const auto result=atlas.finalize(field,"Zone1",512,{7,8,9,11});
    QVERIFY2(result.success,qPrintable(result.error));
    QCOMPARE(result.requiredSlots,1);
    QCOMPARE(result.tiles[0].pixels,fixture.images[0]);
    QCOMPARE(result.tileTypes,QVector<qint32>({7}));
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const auto finalization=atlas.finalize(field,"CaveSet",512,{1,2,3,4},&report);
    QVERIFY2(finalization.success,qPrintable(finalization.error));
    const auto package=CTestTerrainVirtualAtlas::buildPortablePackage(
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
    CTestTerrainWorkingAtlas working(fixture.catalog, workingImages, 99, 128);
    seedReviewedStockMasks(working, fixture);
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

void TerrainVirtualAtlasTests::overlayVirtualPaintAllowsSparseInactiveField()
{
    Fixture fixture;
    CMprDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(portableMpr(true), "sparse-overlay.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel model;
    QVERIFY2(model.load(document, &report), qPrintable(report.toText()));

    STerrainTileBrush activeArea;
    activeArea.centerX = 8.0f;
    activeArea.centerY = 8.0f;
    activeArea.radius = 3.0f;
    activeArea.shape = ETerrainTileBrushShape::Circle;
    const SOverlaySurfaceCommand activation = model.makeOverlayActivation(
        activeArea, 0, 0, 0, 0, &report);
    QVERIFY2(!activation.isEmpty(), qPrintable(report.toText()));
    QVERIFY2(model.apply(activation, true, &report), qPrintable(report.toText()));

    STerrainTileBrush paint;
    paint.centerX = 8.0f;
    paint.centerY = 8.0f;
    paint.radius = 1.0f;
    paint.shape = ETerrainTileBrushShape::Circle;
    const STerrainTileCommand command = model.makeVirtualMaterialPaint(
        8, 8, paint, ETerrainTileLayer::Overlay, 2,
        fixture.catalog, 71, &report);
    QVERIFY2(!command.isEmpty(), qPrintable(report.toText()));
    QVERIFY2(model.apply(command, true, &report), qPrintable(report.toText()));

    CTerrainVirtualMaterialField strictField;
    QVERIFY(!model.copyVirtualMaterialField(
        ETerrainTileLayer::Overlay, &strictField));
    CTerrainVirtualMaterialField sparseField;
    QVERIFY(model.copyVirtualMaterialField(
        ETerrainTileLayer::Overlay, &sparseField, false));
    for(const auto& vertex : command.virtualVertices)
    {
        for(int y : {vertex.globalY - 1, vertex.globalY})
            for(int x : {vertex.globalX - 1, vertex.globalX})
            {
                if(x < 0 || y < 0 || x >= sparseField.tileWidth()
                   || y >= sparseField.tileHeight()) continue;
                bool active = false;
                model.packedTile(x, y, ETerrainTileLayer::Overlay, &active);
                if(!active) continue;
                bool found = false;
                sparseField.cellSignature(x, y, &found);
                QVERIFY2(found, qPrintable(QString(
                    "active overlay cell %1,%2 lost its virtual material identity")
                    .arg(x).arg(y)));
            }
    }
}

void TerrainVirtualAtlasTests::uniformStrokeUsesOneAuthoredVariationAcrossItsInterior()
{
    Fixture fixture;
    int nextSlot = 4;
    // Give material 2 a second legitimate uniform variation, then make every
    // binary edge/corner tuple exact so release is testing distribution rather
    // than generation availability.
    STerrainTransitionVariant secondUniform;
    secondUniform.signature = signature(2, 2, 2, 2);
    SMprPackedTile secondPacked;
    secondPacked.tileIndex = quint8(nextSlot++);
    secondUniform.packedTile = secondPacked.encode();
    QVERIFY(fixture.catalog.addVariant(secondUniform));
    for(int bits = 1; bits < 15; ++bits)
    {
        const auto value = [bits](int bit) { return (bits & (1 << bit)) ? 2 : 1; };
        STerrainTransitionVariant variant;
        variant.signature = signature(value(0), value(1), value(2), value(3));
        SMprPackedTile packed;
        packed.tileIndex = quint8(nextSlot++);
        variant.packedTile = packed.encode();
        fixture.catalog.addVariant(variant);
    }

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = "CaveSet000000.SEC";
    source.sectorX = 0; source.sectorY = 0; source.type = 0;
    source.payload = payload; source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));
    STerrainTileBrush brush;
    brush.centerX = 8; brush.centerY = 8; brush.radius = 4;
    const quint32 strokeSeed = 0x98761234u;
    const STerrainTileCommand preview = model.makeVirtualMaterialPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2,
        fixture.catalog, strokeSeed, &report);
    QVERIFY2(!preview.isEmpty(), qPrintable(report.toText()));
    QVERIFY(model.apply(preview, true, &report));
    const STerrainTileCommand resolution = model.makeVirtualStrokeResolution(
        preview, fixture.catalog, strokeSeed, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(model.apply(resolution, true, &report));

    CTerrainVirtualMaterialField field;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land, &field));
    QSet<quint16> interiorTiles;
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool found = false;
            const STerrainTransitionSignature exact = field.cellSignature(x, y, &found);
            if(!found || !(exact == signature(2, 2, 2, 2))) continue;
            bool available = false;
            interiorTiles.insert(model.packedTile(
                x, y, ETerrainTileLayer::Land, &available));
            QVERIFY(available);
        }
    QVERIFY(interiorTiles.size() > 0);
    QCOMPARE(interiorTiles.size(), 1);
}

void TerrainVirtualAtlasTests::externalUniformFamilyPaintsAndBlendsWithoutAuthoredSlot()
{
    Fixture fixture;
    const qint32 externalId = 9001;
    fixture.catalog.declareMaterial(externalId);
    QMap<qint32, QImage> external;
    external.insert(externalId, solid(qRgba(170, 170, 165, 255)));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 17, external);
    seedReviewedStockMasks(atlas, fixture);
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
            <= CTestTerrainVirtualAtlas::blendVariationCount());
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const auto result=atlas.finalize(field,"Zone2",512,{10,11,12,13},nullptr,{pinned.encode()});
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
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const auto finalized = atlas.finalize(field, "InactiveOverlay", 512,
                                           {1,2,3,4}, &report);
    QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
    const auto package = CTestTerrainVirtualAtlas::buildPortablePackage(
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
        CTestTerrainVirtualAtlas::composePage(tiles, 0, 16), true, &diagnostic);
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

void TerrainVirtualAtlasTests::dxt1AtlasTileCacheReadsEachPageOnce()
{
    QVector<QImage> tiles;
    for(int slot = 0; slot < 64; ++slot)
        tiles.append(solid(qRgba(slot * 3, slot * 2, slot, 255)));
    QString diagnostic;
    const QByteArray payload = ei::encodeMmpDxt1(
        CTestTerrainVirtualAtlas::composePage(tiles, 0, 8), true, &diagnostic);
    QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
    QMap<QString, QByteArray> payloads;
    QSet<QString> missing;
    int availableReads = 0;
    int missingReads = 0;
    const ei::MmpPayloadLoader loader = [&](const QString& page,
                                            QByteArray* bytes) {
        if(page == QStringLiteral("available.mmp"))
        {
            ++availableReads;
            *bytes = payload;
            return true;
        }
        ++missingReads;
        return false;
    };
    QImage first, second, absent;
    QVERIFY(ei::decodeMmpDxt1AtlasTileCached(
        "AVAILABLE.MMP", 0, &payloads, &missing, loader, &first, &diagnostic));
    QVERIFY(ei::decodeMmpDxt1AtlasTileCached(
        "available.mmp", 63, &payloads, &missing, loader, &second, &diagnostic));
    QVERIFY(!ei::decodeMmpDxt1AtlasTileCached(
        "missing.mmp", 0, &payloads, &missing, loader, &absent, &diagnostic));
    QVERIFY(!ei::decodeMmpDxt1AtlasTileCached(
        "MISSING.MMP", 1, &payloads, &missing, loader, &absent, &diagnostic));
    QCOMPARE(availableReads, 1);
    QCOMPARE(missingReads, 1);
    QCOMPARE(payloads.size(), 1);
    QCOMPARE(missing.size(), 1);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
}

void TerrainVirtualAtlasTests::workingAtlasGeneratesImmediatelyAndReusesTuple()
{
    Fixture fixture;
    CTestTerrainWorkingAtlas working(fixture.catalog,fixture.images,17,8);
    seedReviewedStockMasks(working, fixture);
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
    CTestTerrainWorkingAtlas working(catalog, authored, 91u,
        kMprPackedTerrainAtlasSlotLimit, external);
    seedReviewedPairMasks(working, 1, 2, authored.front(), external.value(2));
    QCOMPARE(working.capacityLimit(), kMprPackedTerrainAtlasSlotLimit);
    QMap<int, quint64> usage;
    QSet<int> protectedSlots;
    for(int slot = 0; slot < authored.size(); ++slot)
    {
        usage.insert(slot, 1);
        protectedSlots.insert(slot);
    }
    CValidationReport report;
    const quint16 packed = working.resolve(signature(1, 2, 2, 2),
        0, 0, &usage, protectedSlots, nullptr, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(), 4096);
    QCOMPARE(working.activeTextureCount(), 65);
}

void TerrainVirtualAtlasTests::workingAtlasNeverReplacesAuthoredPixels()
{
    Fixture fixture;
    const QVector<QImage> original = fixture.images;
    CTestTerrainWorkingAtlas working(fixture.catalog, fixture.images, 17, 8);
    seedReviewedStockMasks(working, fixture);
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
    CTestTerrainWorkingAtlas working(fixture.catalog, authored, 97, 1024, external);
    seedReviewedStockMasks(working, fixture);
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
    CTestTerrainWorkingAtlas working(fixture.catalog,fixture.images,3,4);
    seedReviewedStockMasks(working, fixture);
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

    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x314159u);
    seedReviewedStockMasks(atlas, fixture);
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
    const auto package = CTestTerrainVirtualAtlas::buildPortablePackage(
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

            CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x271828u);
            seedReviewedStockMasks(atlas, fixture);
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
            const auto package = CTestTerrainVirtualAtlas::buildPortablePackage(
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
            CTestTerrainVirtualAtlas repeated(fixture.catalog, fixture.images, 0x271828u);
            seedReviewedStockMasks(repeated, fixture);
            const auto again = repeated.finalize(field,
                QString("Nested-%1-%2").arg(outer).arg(inner), 512, {1,2,3,4});
            QCOMPARE(again.stableSha256, finalized.stableSha256);
            QCOMPARE(again.pageMmpPayloads, finalized.pageMmpPayloads);
            ++scenarios;
        }
    QCOMPARE(scenarios, 12);
}

void TerrainVirtualAtlasTests::circularPaintsKeepEveryBrushIdentityInsideAndAtOuterEdge()
{
    Fixture fixture;
    for(qint32 background = 1; background <= 4; ++background)
        for(qint32 brushMaterial = 1; brushMaterial <= 4; ++brushMaterial)
            for(float radius : {3.0f, 4.0f})
            {
                CTerrainVirtualMaterialField field;
                QVERIFY(field.resize(16, 16, background));
                STerrainTileBrush brush;
                brush.centerX = brush.centerY = 8.0f;
                brush.radius = radius;
                brush.shape = ETerrainTileBrushShape::Circle;
                field.paint(brush, brushMaterial);
                CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images,
                                            0xc1cc1e00u);
                seedReviewedStockMasks(atlas, fixture);
                CValidationReport report;
                const STerrainAtlasFinalization finalized = atlas.finalize(
                    field, QStringLiteral("CircleBrushOracle"), 512,
                    {1,2,3,4}, &report);
                QVERIFY2(finalized.success,
                    qPrintable(finalized.error + "\n" + report.toText()));
                for(int y = 0; y < 16; ++y)
                    for(int x = 0; x < 16; ++x)
                    {
                        bool found = false;
                        const STerrainTransitionSignature actual =
                            finalizedSignature(finalized,
                                finalized.packedCells.at(y * 16 + x), &found);
                        QVERIFY(found);
                        QCOMPARE(actual, field.cellSignature(x, y));
                    }
                for(int y = 0; y <= 16; ++y)
                    for(int x = 0; x <= 16; ++x)
                    {
                        const int dx = x - 8, dy = y - 8;
                        const bool touched = dx * dx + dy * dy
                            <= int(radius * radius + 0.0001f);
                        QCOMPARE(field.material(x, y),
                            touched ? brushMaterial : background);
                    }
            }
}

void TerrainVirtualAtlasTests::generatedSlotsNeverInheritCompactedAnimationRanges()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    QVERIFY(field.setMaterial(1, 0, 2));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0xadda11u);
    seedReviewedStockMasks(atlas, fixture);
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("AnimatedSource"), 512, {1,2,3,4}, &report);
    QVERIFY2(finalized.success, qPrintable(finalized.error));
    CMprDocument source;
    QVERIFY2(source.loadBytes(portableMpr(false, true),
        QStringLiteral("animated-source.mpr"), &report),
        qPrintable(report.toText()));
    QCOMPARE(source.mapInfo().animatedTileCount, quint32(1));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            source, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
        qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
        QStringLiteral("animated-finalized.mpr"), &report),
        qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().animatedTileCount, quint32(0));
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
        CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x13579u);
        seedReviewedStockMasks(atlas, fixture);
        const auto finalized = atlas.finalize(field, "MatrixMap", 512,
                                               {1,2,3,4}, &report);
        QVERIFY2(finalized.success, qPrintable(finalized.error + "\n" + report.toText()));
        const auto package = CTestTerrainVirtualAtlas::buildPortablePackage(
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

        CTestTerrainVirtualAtlas repeatAtlas(fixture.catalog, fixture.images, 0x13579u);
        seedReviewedStockMasks(repeatAtlas, fixture);
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

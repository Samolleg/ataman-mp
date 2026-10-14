#include <QtTest>
#include <QCryptographicHash>
#include <QtEndian>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QTransform>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>

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

QByteArray grayscalePixelHash(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y = 0; y < image.height(); ++y)
        hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)),
                     image.width());
    return hash.result().toHex();
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
        const QVector<qint32>& preferredCellTypes = {},
        const QVector<quint16>& preferredPackedCells = {},
        const QVector<qint32>& preferredPaintMaterials = {},
        const CTerrainTransitionCatalog* authoredDecodeCatalog = nullptr,
        const QVector<STerrainAtlasAnimationRange>& authoredAnimations = {})
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
            report, pinnedPackedTiles, cellTypes, preferredPackedCells,
            preferredPaintMaterials, authoredDecodeCatalog, {},
            authoredAnimations);
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
                    qint32 preferredPaintMaterial = 0,
                    qint32 preferredRuntimeType = -1)
    {
        return CTerrainWorkingAtlas::resolve(
            requested, cellX, cellY, workingUsage, protectedSlots,
            generated, report,
            preferredPaintMaterial != 0
                ? preferredPaintMaterial : requested.northWest,
            preferredRuntimeType);
    }
};

class CReusedWorkingAtlasStorage
{
public:
    ~CReusedWorkingAtlasStorage() { reset(); }

    CTestTerrainWorkingAtlas* construct(
        const CTerrainTransitionCatalog& catalog,
        const QVector<QImage>& images,
        quint32 distributionSeed = 0,
        int capacityLimit = 512)
    {
        reset();
        m_value = new (&m_storage) CTestTerrainWorkingAtlas(
            catalog, images, distributionSeed, capacityLimit);
        return m_value;
    }

    void reset()
    {
        if(!m_value)
            return;
        m_value->~CTestTerrainWorkingAtlas();
        m_value = nullptr;
    }

private:
    typename std::aligned_storage<sizeof(CTestTerrainWorkingAtlas),
                                  alignof(CTestTerrainWorkingAtlas)>::type m_storage;
    CTestTerrainWorkingAtlas* m_value{nullptr};
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
                       bool withAnimatedRange = false,
                       quint16 landPacked = 0)
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
    QByteArray landTiles(16 * 16 * 2, '\0');
    for(int cell = 0; cell < 16 * 16; ++cell)
        qToLittleEndian<quint16>(landPacked,
            reinterpret_cast<uchar*>(landTiles.data() + cell * 2));
    sector.append(landTiles);
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

QByteArray portableMprWithActiveOverlay(quint16 landPacked,
                                        quint16 overlayPacked)
{
    QByteArray mp;
    QDataStream stream(&mp, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0xCE4AF672) << float(100.0f) << quint32(1) << quint32(1)
           << quint32(4) << quint32(64) << quint32(4) << quint32(8)
           << quint16(1) << quint32(0);
    mp.append(QByteArray(44, '\0'));
    stream.device()->seek(mp.size());
    stream << qint32(1) << qint32(2) << qint32(3) << qint32(4);

    QByteArray sector;
    QDataStream sec(&sector, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(3);
    sector.append(QByteArray(33 * 33 * 8 * 2, '\0'));
    QByteArray landTiles(16 * 16 * 2, '\0');
    QByteArray overlayTiles(16 * 16 * 2, '\0');
    QByteArray overlayMaterials(16 * 16 * 2, '\0');
    for(int cell = 0; cell < 16 * 16; ++cell)
    {
        qToLittleEndian<quint16>(landPacked,
            reinterpret_cast<uchar*>(landTiles.data() + cell * 2));
        qToLittleEndian<quint16>(overlayPacked,
            reinterpret_cast<uchar*>(overlayTiles.data() + cell * 2));
        qToLittleEndian<qint16>(0,
            reinterpret_cast<uchar*>(overlayMaterials.data() + cell * 2));
    }
    sector.append(landTiles);
    sector.append(overlayTiles);
    sector.append(overlayMaterials);

    QByteArray empty(16, '\0');
    qToLittleEndian<quint32>(0x019CE23C,
        reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,
        reinterpret_cast<uchar*>(empty.data() + 8));
    CResArchiveDocument archive;
    QStringList errors;
    if(!archive.loadBytes(empty, "empty", &errors))
        return {};
    QByteArray first;
    if(!archive.buildWithAppendedMember("CaveSet.MP", mp, 0, &first, &errors)
       || !archive.loadBytes(first, "first", &errors))
        return {};
    QByteArray second;
    return archive.buildWithAppendedMember(
               "CaveSet000000.SEC", sector, 0, &second, &errors)
        ? second : QByteArray();
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
    void oppositeBaselineOwnerDoesNotBlockStroke_data();
    void oppositeBaselineOwnerDoesNotBlockStroke();
    void virtualFieldPaintIsUnboundedByAtlas();
    void exactAuthoredTileIsReused();
    void rejectedPaintArtworkIsPreservedByExactFinalization();
    void paintReferenceImportsAuthorPixelsWithoutRebaking();
    void authoredGlobalBlendIsImportedBeforeGeneration();
    void workingAtlasReportsLiveImportedAndGeneratedProvenance();
    void mirroredGeneratedBlendRemainsPaintable_data();
    void mirroredGeneratedBlendRemainsPaintable();
    void mirroredVertexStrokeResolvesAndUndoesExactly();
    void workingAtlasKeepsCurrentMapExactVariantAheadOfImportedAliases();
    void workingAtlasKeepsMatchingRuntimeImportWhenLocalTypeDiffers();
    void workingAtlasAcceptsLazyMaterialSourcesWithoutResettingState();
    void nonUniformResolveUsesAuthoredRotationOrGeneratesExact();
    void canonicalMasksAreAvailableWithoutStockTemplates();
    void contextFreeResolutionUsesReviewedMaterialMask();
    void lavaUsesOnlyExplicitR123ReviewedMaskRoutes();
    void authoredMapTransitionsDoNotPopulateObsoleteMaskPool();
    void canonicalMasksMeetQuarterCoverageAndCornerContract();
    void canonicalMasksAreExactComplementsAndRotationCovariant();
    void canonicalMaskVariationsAreDeterministicAndMacroDistinct();
    void canonicalAdjacentAndDiagonalTopologiesStayAnchored();
    void canonicalMasksHaveNoUnanchoredIslandsOrWideFeather();
    void approvedIceInsideMasksDriveRuntimeGeneration();
    void approvedReviewedMasksDriveRuntimeGeneration();
    void effectiveReviewMaskMatchesGeneratedPurePixels();
    void finalizationCarriesPaintMaterialIntoReviewedMaskSelection();
    void reviewedTopologyNeverFallsBackToUnapprovedVariation();
    void generatedReviewOutcomeUsesRuntimeBlendGenerator();
    void generatesOneThreeTuple();
    void generatesTwoTwoTuple();
    void generatesFourFamilyTupleWithOrientation();
    void multiFamilyJunctionsUseApprovedAuthoredOwnershipInputs();
    void generatedEdgesUseOnlyTheirTwoCornerMaterials();
    void generatedEdgesHaveSolidEndpointPlateaus();
    void generatedTransitionRetainsSourceTextureIdentity();
    void generatedTransitionBoundaryIsOrganicRatherThanQuadrants();
    void generatedLandBlendCannotPunchTransparentHoles();
    void generatedMultiFamilyBlendHasNoZeroWeightHoles();
    void generatedOverlayBlendPreservesAuthoredAlpha();
    void linearLightReviewChangesOnlyMixedPixelsAndPreservesAlpha();
    void stockPixelNoiseCannotAlterCanonicalFallback();
    void stockDetachedBlocksCannotAlterCanonicalFallback();
    void stockFragmentsCannotAlterCanonicalFallback();
    void stockSerrationCannotAlterCanonicalFallback();
    void degenerateStockMasksAreRejected();
    void softStockLeakCannotAlterCanonicalFallback();
    void longStockReachCannotAlterCanonicalFallback();
    void legacyTargetStyleRejectionCannotAlterCanonicalFallback();
    void legacyPavingRejectionCannotAlterCanonicalFallback();
    void materialNamesSelectExplicitBlendStyles();
    void paintedMaterialOwnsGeneratedMaskFamily();
    void materialBlendStylesProvideBoundedDistinctVariants();
    void generatedNeighbourEdgesArePixelContinuous();
    void rotationsDeduplicateButSemanticAliasesRemainDistinct();
    void generatedTerrainTypeUsesMajorityAcrossRotation();
    void generatedTerrainTypeTieUsesPaintedMaterialAndTypedDedup();
    void unknownGeneratedTerrainTypeIsRefused();
    void workingAtlasCarriesGeneratedTerrainType();
    void stagedResolveUsesPredecessorTypeOverAuthoredAndSemanticMajority();
    void stagingCopyOwnsCatalogAfterSourceLifetimeEnds();
    void stableHashesAndNamespaceAreDeterministic();
    void mapWideUsageAndUnusedCompaction();
    void exactAuthoredDuplicatesAreRemappedBeforeCapacity();
    void exactAuthoredDuplicatesUseCrossLayerDecodeCatalog();
    void exactAuthoredDuplicatesWithDifferentRuntimeTypesRemainDistinct();
    void exactAuthoredDuplicateConflictFailsClosed();
    void exactAuthoredDuplicateRemapPreservesPackedRotationsAfterReopen();
    void finalizesAtValidatedRuntimeCeilingBeyond512();
    void capacityRefusalNeverSubstitutes();
    void rawPageCompositionPreservesEveryUntouchedSlot();
    void asymmetricAtlasGridPreservesEveryLogicalSlotPixel();
    void pageOrientationMipmapsAlphaAndReopen();
    void namespacesAreIsolated();
    void identityBaselineRequiresExactSharedCornerTopology();
    void exactOnlyNoOpPreservesPixels();
    void portablePackageRemapsNamespaceAndReopens();
    void portablePackageStagesLiveSculptBeforeAtlasFinalization();
    void workingPreviewPackagePreservesAuthoredPagesAndAppendsExtensions();
    void workingPreviewPackagePublishesReusedAuthoredTailPixels();
    void workingPreviewPackageDoesNotSerializePhysicalPagePadding();
    void workingPreviewPackageStagesSeveralGeneratedPages();
    void virtualPaintPreviewAndUndoPreserveDesiredIdentity();
    void virtualTilePaintTargetsFourCellCorners();
    void mapResolutionRepairsWrongPhysicalBlendAndUndoesExactly();
    void mapResolutionPreservesRuntimeTypeForTiedGeneratedBlends();
    void mapResolutionPreservesMissingTypedCandidateInPartialMode();
    void connectedRouteOwnershipUsesSemanticDeltaAcrossAllTopologies();
    void connectedRouteOwnershipMirrorUsesReversePaintIntent();
    void connectedRouteOwnershipScopesPlanningToRepairCandidates();
    void routeOwnerFiltersOppositeGeneratedExactCandidates();
    void ambiguousAndMultiMaterialRouteComponentsArePreserved();
    void mapResolutionNormalizesRouteOwnerAndIsIdempotent();
    void mapResolutionReachesFixedPointInOneCommand();
    void mapResolutionPrefersAuthoritativeVirtualField();
    void mapResolutionDoesNotChooseAWinningSideForTiedBorders();
    void recoveredMapResolutionRepairsDecodedContextStampsLikeAnyBlend();
    void recoveredMapResolutionRepairsDecodedWrongBlend();
    void recoveredMapResolutionExcludesTheCellBeingJudged();
    void classicAutoStrokeRefusesMissingBlendAndRollsBackExactly();
    void classicSemanticCommandsEstablishVirtualField();
    void contextualClassicVirtualFieldTracksAcceptedVertices();
    void overlayVirtualPaintAllowsSparseInactiveField();
    void collapsedUniformBaselineRecoversExactRotatedBlend();
    void overlappingVirtualPaintDoesNotReplaceUnchangedEdgeBlendWithUniformPreview();
    void uniformStrokeDistributesPureVariantsAndRotationsDeterministically();
    void preservedLayerSlotsArePinnedAndRemapped();
    void combinedLandAndOverlayPublicationPreservesBothExactFields();
    void reopenedDraftCanInitializeAndPublishWithoutNewPaint();
    void inactiveOverlaySentinelSurvivesLandFinalization();
    void gradedAlphaIsRefusedWithoutFlattening();
    void dxt1TerrainPagesMatchShippedMipContract();
    void dxt1AtlasTileDirectDecodeMatchesFullPageOrientation();
    void dxt1AtlasTileCacheReadsEachPageOnce();
    void workingAtlasGeneratesImmediatelyAndReusesTuple();
    void workingAtlasStagingInjectedFailureLeavesLiveStateUntouched();
    void workingAtlasStagingRealizationThenEmptyLeavesLiveStateUntouched();
    void expanded4096AtlasRetainsWorkingExtensionCapacity();
    void workingAtlasNeverReplacesAuthoredPixels();
    void workingAtlasReusesSafeLogicalTailPaddingBeforeExtendingPages();
    void repeatedGeneratedStampsNeverMutateAuthoredAtlas();
    void workingAtlasProtectsUndoSlotsAndRefusesCapacity();
    void manualTilePaintRejectsUntypedWorkingSlot();
    void completeWorkingTypeTableMakesEveryManualVariantPaintable();
    void externalUniformFamilyPaintsAndBlendsWithoutAuthoredSlot();
    void selfPaintPreservesExactPackedTileAndRotation();
    void parallelStripesAndBaseCrossResolveExactly();
    void nestedCircleMaterialPairingsResolveExactly();
    void circularPaintsKeepEveryBrushIdentityInsideAndAtOuterEdge();
    void workingAtlasRepacksInterleavedUniformAnimationFramesAtomically();
    void workingAtlasKeepsFlipbooksInsideOnePhysicalPage();
    void finalizationPreservesAndRebasesAuthoredAnimationRanges();
    void finalizationKeepsFlipbooksInsideOnePhysicalPage();
    void finalizationCompactsAnimationBlocksWithoutSourcePageDebris();
    void generatedPaintMatrixSavesReopensAndMatchesVertexOracle();
};

void TerrainVirtualAtlasTests::asymmetricAtlasGridPreservesEveryLogicalSlotPixel()
{
    QVector<QImage> tiles;
    tiles.reserve(9 * 64);
    for(int slot = 0; slot < 9 * 64; ++slot)
    {
        QImage tile(4, 4, QImage::Format_RGBA8888);
        tile.fill(QColor::fromRgb((slot * 17) & 255,
                                  (slot * 43) & 255,
                                  (slot * 97) & 255));
        tiles.append(tile);
    }
    const QImage grid = CTerrainVirtualAtlas::composeAtlasGrid(tiles, 9, 3);
    QCOMPARE(grid.size(), QSize(96, 96));
    for(int slot = 0; slot < tiles.size(); ++slot)
    {
        const int page = slot / 64;
        const int local = slot % 64;
        const int x = (page % 3) * 32 + (local % 8) * 4 + 2;
        const int y = (page / 3) * 32 + (7 - local / 8) * 4 + 2;
        QCOMPARE(grid.pixelColor(x, y), tiles.at(slot).pixelColor(2, 2));
    }
    QVERIFY(CTerrainVirtualAtlas::composeAtlasGrid(tiles, 9, 10).isNull());
    QVERIFY(CTerrainVirtualAtlas::composeAtlasGrid(tiles, 8, 8).isNull());
    QVector<QImage> malformed = tiles;
    malformed[109] = QImage(3, 4, QImage::Format_RGBA8888);
    QVERIFY(CTerrainVirtualAtlas::composeAtlasGrid(malformed, 9, 3).isNull());
}

void TerrainVirtualAtlasTests::approvedIceInsideMasksDriveRuntimeGeneration()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << QImage(256, 256, QImage::Format_RGBA8888)
           << QImage(256, 256, QImage::Format_RGBA8888);
    images[0].fill(QColor(226, 235, 248));
    images[1].fill(QColor(42, 104, 36));
    for(int material = 1; material <= 2; ++material)
    {
        STerrainTransitionVariant uniform;
        uniform.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(material - 1);
        uniform.packedTile = packed.encode();
        catalog.addVariant(uniform);
    }
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Ice}, {2, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x63u, {}, styles);
    const STerrainTransitionSignature inside = signature(1, 1, 1, 2);
    const QByteArray expectedHashes[2] = {
        QByteArray("47f439119039111046f1aa7bc40b4d404dab03fdf47934707cdb5c39667d15c1"),
        QByteArray("4f0f7e118e4539bc6c5500a1d6dff4e31799be47d15bc6f964fe0ddb23a6bebe")
    };
    const char* expectedSources[2] = {"bz11k:478", "bz11k:438"};
    for(int variation = 0; variation < 2; ++variation)
    {
        QString provenance;
        const QImage mask = atlas.reviewBlendMask(
            inside, 1, variation, &provenance);
        QVERIFY(!mask.isNull());
        QCOMPARE(mask.size(), QSize(256, 256));
        // Review assets are approved in logical grid coordinates; the runtime
        // compositor consumes the vertically mirrored preview-coordinate mask.
        QCOMPARE(grayscalePixelHash(mask.mirrored(false, true)),
                 expectedHashes[variation]);
        QCOMPARE(provenance, QStringLiteral(
            "canonical-generated:ice-fracture:inside-3of4-v%1:"
            "r76-approved-adjustment:%2")
                .arg(variation + 1)
                .arg(QString::fromLatin1(expectedSources[variation])));

        QString resultProvenance;
        CValidationReport report;
        const QImage generated = atlas.reviewGeneratedBlend(
            inside, variation, &resultProvenance, &report);
        QVERIFY2(!generated.isNull(), qPrintable(report.toText()));
        QCOMPARE(resultProvenance, provenance);
        QImage expected(generated.size(), QImage::Format_RGBA8888);
        for(int y = 0; y < expected.height(); ++y)
            for(int x = 0; x < expected.width(); ++x)
            {
                double owner = qBound(0.0,
                    (mask.pixelColor(x, y).redF() - 0.38) / 0.24, 1.0);
                owner = owner * owner * (3.0 - 2.0 * owner);
                const QColor a = images[0].pixelColor(x, y);
                const QColor b = images[1].pixelColor(x, y);
                expected.setPixelColor(x, y, QColor(
                    qRound(a.red() * owner + b.red() * (1.0 - owner)),
                    qRound(a.green() * owner + b.green() * (1.0 - owner)),
                    qRound(a.blue() * owner + b.blue() * (1.0 - owner)), 255));
            }
        // Runtime applies its established narrow ownership remap above, then
        // replaces the outermost texel row/column with the shared-edge
        // continuity contract. Every interior texel must still derive from the
        // approved mask; corners must resolve to their semantic source.
        QCOMPARE(generated.copy(1, 1, generated.width() - 2,
                                generated.height() - 2),
                 expected.copy(1, 1, expected.width() - 2,
                               expected.height() - 2));
        // Generated atlas pixels are in preview coordinates: logical SW is
        // preview NW at this boundary.
        QCOMPARE(generated.pixelColor(0, 0), images[1].pixelColor(0, 0));
        QCOMPARE(generated.pixelColor(generated.width() - 1, 0),
                 images[0].pixelColor(generated.width() - 1, 0));
        QCOMPARE(generated.pixelColor(generated.width() - 1,
                                      generated.height() - 1),
                 images[0].pixelColor(generated.width() - 1,
                                      generated.height() - 1));
        QCOMPARE(generated.pixelColor(0, generated.height() - 1),
                 images[0].pixelColor(0, generated.height() - 1));

        bool runtimeMatched = false;
        for(int y = 0; y < 16 && !runtimeMatched; ++y)
            for(int x = 0; x < 16 && !runtimeMatched; ++x)
            {
                CValidationReport runtimeReport;
                const STerrainVirtualTile tile = atlas.resolve(
                    inside, x, y, &runtimeReport);
                QVERIFY2(tile.generatedIndex >= 0,
                         qPrintable(runtimeReport.toText()));
                const STerrainFinalizedTile& realized =
                    atlas.generatedTiles().at(tile.generatedIndex);
                runtimeMatched = realized.pixels == generated
                    && realized.blendProvenance == provenance;
            }
        QVERIFY2(runtimeMatched,
                 "Resolver did not realize the approved R76 variation");

        const STerrainTransitionSignature rotated =
            CTerrainTransitionCatalog::rotated(inside, 1);
        const QImage rotatedMask = atlas.reviewBlendMask(
            rotated, 1, variation);
        QCOMPARE(rotatedMask, mask.transformed(
            QTransform().rotate(-90.0), Qt::FastTransformation));
        QImage inverse = mask;
        inverse.invertPixels();
        QCOMPARE(atlas.reviewBlendMask(inside, 2, variation), inverse);
    }
}

void TerrainVirtualAtlasTests::approvedReviewedMasksDriveRuntimeGeneration()
{
    const QImage r75ResourceProbe(QStringLiteral(
        ":/terrain_blend_masks/terrain_blend_masks/"
        "soil-outer-1of4-v2-r75-authored.png"));
    QVERIFY(!r75ResourceProbe.isNull());
    QCOMPARE(grayscalePixelHash(r75ResourceProbe), QByteArray(
        "5a930baa567911ceb215d95863acf75ccc455b5378cd4d5ba055f6229c215c38"));

    struct ReviewedCase
    {
        ETerrainBlendStyle style;
        STerrainTransitionSignature signature;
        int variation;
        const char* pixelSha256;
        const char* provenance;
    };
    const ReviewedCase cases[] = {
        {ETerrainBlendStyle::Grass, signature(1, 2, 2, 2), 0,
         "7161801ae53a63a4968abc3a39eb6f9971a5c3786d168c9c7644d5a95f442876",
         "canonical-generated:grass-strands:outer-1of4-v1:r44-approved"},
        {ETerrainBlendStyle::Grass, signature(1, 1, 2, 2), 0,
         "be0f21c2e9d01ce56b4003411e63ed70026820725701d460b1466711f2277bca",
         "canonical-generated:grass-strands:edge-2of4-v1:r44-approved"},
        {ETerrainBlendStyle::Grass, signature(1, 2, 1, 2), 0,
         "39a4bd49aa72525cb955d44e797235cf102d549ea5b1aec995fa6095dc457945",
         "canonical-generated:grass-strands:diagonal-2of4:r44-approved"},
        {ETerrainBlendStyle::Soil, signature(1, 2, 2, 2), 1,
         "5a930baa567911ceb215d95863acf75ccc455b5378cd4d5ba055f6229c215c38",
         "canonical-generated:soil-mottle:outer-1of4-v2:r75-approved-authored-restoration:Zone7:297"},
        {ETerrainBlendStyle::Soil, signature(1, 1, 2, 2), 1,
         "25c8b5c55dc820868669e56c788c555757e5930746e9ced4bea2c380d9961c9f",
         "canonical-generated:soil-mottle:edge-2of4-v2:r75-approved-authored-restoration:bz16h:114"},
        {ETerrainBlendStyle::Soil, signature(1, 1, 1, 2), 0,
         "71374bd31017e599c3216106552307f922c68c57f3ee40bc99b0e1ee8820d4a2",
         "canonical-generated:soil-mottle:inside-3of4-v1:r75-approved-authored-restoration:bz4g:191"},
        {ETerrainBlendStyle::Stone, signature(1, 2, 2, 2), 0,
         "2c107b3db7f37268082b952e2038419aa4b8371a28d28abbbb8cbd89a832505e",
         "canonical-generated:stone-fracture:outer-1of4-v1:r44-approved"},
        {ETerrainBlendStyle::Stone, signature(1, 1, 2, 2), 0,
         "3555e3e95a387d04d219e71cd0de3e8bde69d4bf369cc566f007ee66e83a7090",
         "canonical-generated:stone-fracture:edge-2of4-v1:r44-approved"},
        {ETerrainBlendStyle::Stone, signature(1, 2, 1, 2), 0,
         "eea78b03896e90d1137cc79f7991f17912248ab5480e55415bc5e0fcd502a002",
         "canonical-generated:stone-fracture:diagonal-2of4:r44-approved"},
        {ETerrainBlendStyle::Sand, signature(1, 2, 2, 2), 0,
         "142edf134ed25289299f4771e83d8b5782cbc2d74e66f57f03a3c6850b2bc0e5",
         "canonical-generated:sand-grain:outer-1of4-v1:r76-approved-adjustment:bz5g:496"},
        {ETerrainBlendStyle::Sand, signature(1, 2, 2, 2), 1,
         "3c7f026631344e1688f4bf782806608526bc9b1486adb6517d352b8bbad46539",
         "canonical-generated:sand-grain:outer-1of4-v2:r77-approved-nonrectilinear-adjustment:bz5g:467"},
        {ETerrainBlendStyle::Sand, signature(1, 1, 2, 2), 1,
         "0c142f61025b54f5b2147ac9961e9b9ad280120c0313a5842c87d00ebf5ccc3f",
         "canonical-generated:sand-grain:edge-2of4-v2:r75-approved-authored-restoration:bz16h:161"},
        {ETerrainBlendStyle::Sand, signature(1, 1, 1, 2), 0,
         "cd04494e556162822d919e0b8a2c7289d61024dc54267f83185aa592dddf9e20",
         "canonical-generated:sand-grain:inside-3of4-v1:r75-approved-authored-restoration:Zone7:297"},
        {ETerrainBlendStyle::Sand, signature(1, 1, 1, 2), 1,
         "9ad8602bc5085b0ab4226e4544837988e8911784fc9312d3021eefbad1db312f",
         "canonical-generated:sand-grain:inside-3of4-v2:r76-approved-adjustment:bz5g:180"},
        {ETerrainBlendStyle::Snow, signature(1, 2, 2, 2), 0,
         "301dff7dd30938a8da53e1b9ebae5d427a5c6bd2f7a82798f932bc3a43558cd6",
         "canonical-generated:snow-clumps:outer-1of4-v1:r76-approved-adjustment:bz10k:224"},
        {ETerrainBlendStyle::Snow, signature(1, 2, 2, 2), 1,
         "236e3fe45a9e41de68b3d88e1a621632eeaea2ec337107f375620a7427e8dcec",
         "canonical-generated:snow-clumps:outer-1of4-v2:r76-approved-adjustment:bz8k:236"},
        {ETerrainBlendStyle::Snow, signature(1, 1, 2, 2), 0,
         "143f5b7599608c91420618753aa2641bf64f56e974fcfe59caff4b77818fd293",
         "canonical-generated:snow-clumps:edge-2of4-v1:r75-approved-authored-restoration:bz10k:156"},
        {ETerrainBlendStyle::Snow, signature(1, 1, 2, 2), 1,
         "1803cc86631d8fe57a485c1eef8b785f329af90d184d03fa77258117d5bdf4b6",
         "canonical-generated:snow-clumps:edge-2of4-v2:r77-approved-nonrectilinear-adjustment:bz10k:158"},
        {ETerrainBlendStyle::Snow, signature(1, 2, 1, 2), 0,
         "3547b045e1160533ae609fb353f67efd60eb711a9c68eb9a441b6a21e8e220b6",
         "canonical-generated:snow-clumps:diagonal-2of4:r76-approved-adjustment:bz10k:242"},
        {ETerrainBlendStyle::Snow, signature(1, 1, 1, 2), 0,
         "c371c06523916c840b69f5ffb2e2703a12b68dbb3bdb2640b4386be4f57fcdaf",
         "canonical-generated:snow-clumps:inside-3of4-v1:r76-approved-adjustment:bz10k:224"},
        {ETerrainBlendStyle::Snow, signature(1, 1, 1, 2), 1,
         "6915fd79c4628e0c8661c9f4bf54017f88eb27201b1fb8df6ad4fb88a5ba03de",
         "canonical-generated:snow-clumps:inside-3of4-v2:r76-approved-adjustment:bz10k:217"},
        {ETerrainBlendStyle::Ice, signature(1, 2, 2, 2), 0,
         "5c7697948e158f687404c59ca440b6a15b3367f1ad4621fcb55153df2a8f34de",
         "canonical-generated:ice-fracture:outer-1of4-v1:r76-approved-adjustment:bz11k:406"},
        {ETerrainBlendStyle::Ice, signature(1, 2, 2, 2), 1,
         "2864da37b32d311f477f3378d4ff0592c4d0bce972dbd4dcf033c9fdfc00ab0b",
         "canonical-generated:ice-fracture:outer-1of4-v2:r76-approved-adjustment:bz11k:405"},
        {ETerrainBlendStyle::Ice, signature(1, 1, 2, 2), 0,
         "1ba9c2e33295d6f400f577a7615a8100963be76de753705f421ea27be436e7a7",
         "canonical-generated:ice-fracture:edge-2of4-v1:r76-approved-adjustment:bz11k:401"},
        {ETerrainBlendStyle::Ice, signature(1, 1, 2, 2), 1,
         "e872626e4618432db888ca6dd329c6d457ee20e85fd5361d6d3295fe23720cfc",
         "canonical-generated:ice-fracture:edge-2of4-v2:r76-approved-adjustment:bz11k:364"},
        {ETerrainBlendStyle::Ice, signature(1, 2, 1, 2), 0,
         "dbb729aca28b033be78a2f603d95b5f7ee3ad9936b28ddc9de6a3dc0eb73ffc0",
         "canonical-generated:ice-fracture:diagonal-2of4:r76-approved-adjustment:bz11k:373"},
        {ETerrainBlendStyle::Grass, signature(1, 1, 1, 2), 0,
         "f6d66a4adbf2b207a56e71052009801a4d3d4f254ab98f73b38ddfdaabb97d57",
         "canonical-generated:grass-strands:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Stone, signature(1, 1, 1, 2), 0,
         "6daedacfdf19739a485e004812d12edfae348e876fffa3153521b5e9379040ea",
         "canonical-generated:stone-fracture:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Gravel, signature(1, 2, 2, 2), 0,
         "9ffc86db49fcf58605e69c366ed6469a657246f39800a1f91bd87d484c9bbcb6",
         "canonical-generated:gravel-chunks:outer-1of4-v1:r75-approved-authored-restoration:bz13h:213"},
        {ETerrainBlendStyle::Gravel, signature(1, 1, 2, 2), 0,
         "af124c29a708ac3a4f9160e3c7f894aeea0de4e41ea22b7b8b49a95618cb68be",
         "canonical-generated:gravel-chunks:edge-2of4-v1:r71-approved"},
        {ETerrainBlendStyle::Gravel, signature(1, 2, 1, 2), 0,
         "71bfa4e9f7f7ad04ea5c774a36811c0d31ac549a82eb15df0ec87d8d0504c9d9",
         "canonical-generated:gravel-chunks:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Gravel, signature(1, 1, 1, 2), 0,
         "1048f7a93ae4f3704ef205afb3804cf7aa37671237eb5a2e4bcb17eb2f94f6ed",
         "canonical-generated:gravel-chunks:inside-3of4-v1:r75-approved-authored-restoration:bz5g:496"},
        {ETerrainBlendStyle::Gravel, signature(1, 1, 1, 2), 1,
         "c20c0e1e5941773e71a529794e146f58cc2331bc592ad8205479395ed5d1f0b0",
         "canonical-generated:gravel-chunks:inside-3of4-v2:r76-approved-adjustment:bz5g:423"},
        {ETerrainBlendStyle::Paving, signature(1, 2, 2, 2), 0,
         "f8f6c3247ad468c6e8558111f4e6b8fe96221076402882aa90d4ace40d67a21c",
         "canonical-generated:paving-courses:outer-1of4-v1:r71-approved"},
        {ETerrainBlendStyle::Paving, signature(1, 2, 2, 2), 1,
         "7f2af62ff745000591d72a3ea4229b7485c59967a18c7ce9cb7b6308051a7250",
         "canonical-generated:paving-courses:outer-1of4-v2:r77-approved-nonrectilinear-adjustment:Zone10:509"},
        {ETerrainBlendStyle::Paving, signature(1, 1, 2, 2), 0,
         "ab9b12faecc9c4494c7eeaeea9eee3553df20c4150bf8945cc1847c6b3734868",
         "canonical-generated:paving-courses:edge-2of4-v1:r71-approved"},
        {ETerrainBlendStyle::Paving, signature(1, 1, 2, 2), 1,
         "ff6415e2a059b7f0f4becb6b7655a6a8adb3b199241084d7cffef91a883236ce",
         "canonical-generated:paving-courses:edge-2of4-v2:r77-approved-nonrectilinear-adjustment:Zone10:218"},
        {ETerrainBlendStyle::Paving, signature(1, 2, 1, 2), 0,
         "18a869617280149a2cf28df72b0a9cc6431a8f47cc3e98ea739a9d8f1932a200",
         "canonical-generated:paving-courses:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Paving, signature(1, 1, 1, 2), 0,
         "f91bd0998972534cfe74f55a0491549a944dc336e40deea2079c67392f57298f",
         "canonical-generated:paving-courses:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Sand, signature(1, 2, 1, 2), 0,
         "b0d911e4e6a0516ca1f4f43e9fe6096a2ca5f017173cdd267c59bf1e91f6ddb1",
         "canonical-generated:sand-grain:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Water, signature(1, 2, 2, 2), 0,
         "8bbcc4762db352ace282d7f98e179d34430b498c3c042774d0a85114a2db7394",
         "canonical-generated:water-ripples:outer-1of4-v1:r75-approved-authored-restoration:Zone3Obr:235"},
        {ETerrainBlendStyle::Water, signature(1, 1, 2, 2), 0,
         "d6d712616251dfc2b424d60852e67d12ce9366d327d42f8319a7a04824b677f3",
         "canonical-generated:water-ripples:edge-2of4-v1:r75-approved-authored-restoration:Zone3Obr:249"},
        {ETerrainBlendStyle::Water, signature(1, 2, 1, 2), 0,
         "376b90b14ac98d1b29bb2a405ce361fad3a6d2ef317145514e600e80fbb073e3",
         "canonical-generated:water-ripples:diagonal-2of4:r75-approved-authored-restoration:Zone1:351"},
        {ETerrainBlendStyle::Water, signature(1, 1, 1, 2), 0,
         "5838ab682cd53a8c16606f9b133f7c237bb8c2179d8e0e1737a49730b5503ddd",
         "canonical-generated:water-ripples:inside-3of4-v1:r75-approved-authored-restoration:Zone3Obr:435"},
        {ETerrainBlendStyle::Grass, signature(1, 2, 2, 2), 1,
         "f982aad28868aede6379e489a24668576831f1b7ac967e1cb044f4b31d55de80",
         "canonical-generated:grass-strands:outer-1of4-v2:r75-approved-authored-restoration:Zone3Obr:435"},
        {ETerrainBlendStyle::Grass, signature(1, 1, 2, 2), 1,
         "3b7e22c38268056ff48737f8af84c03e5492cc4f1b0ce89d6b945cdb707e0a66",
         "canonical-generated:grass-strands:edge-2of4-v2:r75-approved-authored-restoration:bz3g:184"},
        {ETerrainBlendStyle::Grass, signature(1, 1, 1, 2), 1,
         "e82bd23fe502ef031e9c4001d567e1ce4a3243b879968a68cace5a6a2e9c7e52",
         "canonical-generated:grass-strands:inside-3of4-v2:r75-approved-authored-restoration:Zone5_1:110"},
        {ETerrainBlendStyle::Gravel, signature(1, 2, 2, 2), 1,
         "33f15a7716cbf5f87716a98c9b35d8e2e74c64367098acf21e6586fe4e41373f",
         "canonical-generated:gravel-chunks:outer-1of4-v2:r75-approved-authored-restoration:bz13h:214"},
        {ETerrainBlendStyle::Gravel, signature(1, 1, 2, 2), 1,
         "dadcb6ed1c915fbccf78111c74dd174db6b284d72cb968c81d4277067e7277f3",
         "canonical-generated:gravel-chunks:edge-2of4-v2:r75-approved-authored-restoration:bz13h:414"},
        {ETerrainBlendStyle::Paving, signature(1, 1, 1, 2), 1,
         "686d6c02ac891b0721870a433f307bd44988015d22509235e5ce822e49ec0ee1",
         "canonical-generated:paving-courses:inside-3of4-v2:r75-approved-authored-restoration:Zone10:499"},
        {ETerrainBlendStyle::Sand, signature(1, 1, 2, 2), 0,
         "0f17756cfea98c8725e05d379e888249f111efa1ca0a570e56fecbd68ed23c99",
         "canonical-generated:sand-grain:edge-2of4-v1:r75-approved-authored-restoration:bz16h:155"},
        {ETerrainBlendStyle::Soil, signature(1, 2, 2, 2), 0,
         "f92b2531c420b89ef3a9a706444f501eef19b65217b9122f6d820cf96cf7b664",
         "canonical-generated:soil-mottle:outer-1of4-v1:r75-approved-authored-restoration:bz4g:191"},
        {ETerrainBlendStyle::Soil, signature(1, 1, 2, 2), 0,
         "14e1ba8841bd685d938c23534ffe90cc6bf9a9aba7c3caae37d6ff2223762fa3",
         "canonical-generated:soil-mottle:edge-2of4-v1:r75-approved-authored-restoration:bz16h:111"},
        {ETerrainBlendStyle::Soil, signature(1, 2, 1, 2), 0,
         "c406fc01d3c31ce9bd4742ddd024fcfeabaf7c9e78b9d2251bce8efff9db6e5e",
         "canonical-generated:soil-mottle:diagonal-2of4:r75-approved-authored-restoration:Zone1:353"},
        {ETerrainBlendStyle::Soil, signature(1, 1, 1, 2), 1,
         "0afae4e6b53b3238788e53955995a92407076a640eaa617bb1a064fa6d769566",
         "canonical-generated:soil-mottle:inside-3of4-v2:r75-approved-authored-restoration:bz3g:97"},
        {ETerrainBlendStyle::Stone, signature(1, 2, 2, 2), 1,
         "c008228b70109b4d02fa93988c565e4a0e2f4978335ee4a2570ab23a2074fca6",
         "canonical-generated:stone-fracture:outer-1of4-v2:r75-approved-authored-restoration:bz10k:216"},
        {ETerrainBlendStyle::Stone, signature(1, 1, 1, 2), 1,
         "871752a42a283fd144f49ec38c24573a32dca4e6c61a3acf9ad8cea8dd55288f",
         "canonical-generated:stone-fracture:inside-3of4-v2:r76-approved-adjustment:bz10k:143"},
        {ETerrainBlendStyle::Water, signature(1, 2, 2, 2), 1,
         "617f5f3ee4231049108dec6334028667652a5604f8a7b2f53f207861c1f9d14b",
         "canonical-generated:water-ripples:outer-1of4-v2:r75-approved-authored-restoration:Zone3Obr:166"},
        {ETerrainBlendStyle::Water, signature(1, 1, 2, 2), 1,
         "a88fffed2e8f74b49a7c090cc9caaa8389a5ce92b517f2a8ee6bb19ca4f9150f",
         "canonical-generated:water-ripples:edge-2of4-v2:r75-approved-authored-restoration:Zone3Obr:496"},
        {ETerrainBlendStyle::Water, signature(1, 1, 1, 2), 1,
         "d0060c4b54bdc2e5fd9d27aa65ab80e7761fbe481a5cbd5fb1ca1b0449f65666",
         "canonical-generated:water-ripples:inside-3of4-v2:r75-approved-authored-restoration:Zone1:223"}
    };

    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << QImage(256, 256, QImage::Format_RGBA8888)
           << QImage(256, 256, QImage::Format_RGBA8888);
    images[0].fill(QColor(35, 90, 155));
    images[1].fill(QColor(185, 145, 75));
    for(int material = 1; material <= 2; ++material)
    {
        STerrainTransitionVariant uniform;
        uniform.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(material - 1);
        uniform.packedTile = packed.encode();
        catalog.addVariant(uniform);
    }

    for(const ReviewedCase& item : cases)
    {
        const QMap<qint32, ETerrainBlendStyle> styles = {
            {1, item.style}, {2, ETerrainBlendStyle::Organic}
        };
        CTestTerrainVirtualAtlas atlas(catalog, images, 0x71u, {}, styles);
        QString provenance;
        const QImage mask = atlas.reviewBlendMask(
            item.signature, 1, item.variation, &provenance, 1);
        QVERIFY2(!mask.isNull(), item.provenance);
        QCOMPARE(grayscalePixelHash(mask.mirrored(false, true)),
                 QByteArray(item.pixelSha256));
        QCOMPARE(provenance, QString::fromLatin1(item.provenance));

        QString resultProvenance;
        CValidationReport report;
        const QImage result = atlas.reviewGeneratedBlend(
            item.signature, item.variation, &resultProvenance, &report, 1);
        QVERIFY2(!result.isNull(), qPrintable(report.toText()));
        QCOMPARE(resultProvenance, provenance);

        const STerrainTransitionSignature rotated =
            CTerrainTransitionCatalog::rotated(item.signature, 1);
        const QImage rotatedMask = atlas.reviewBlendMask(
            rotated, 1, item.variation, nullptr, 1);
        QCOMPARE(rotatedMask, mask.transformed(
            QTransform().rotate(-90.0), Qt::FastTransformation));
        QImage inverse = mask;
        inverse.invertPixels();
        QCOMPARE(atlas.reviewBlendMask(
            item.signature, 2, item.variation, nullptr, 1), inverse);
    }

}

void TerrainVirtualAtlasTests::effectiveReviewMaskMatchesGeneratedPurePixels()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << QImage(256, 256, QImage::Format_RGBA8888)
           << QImage(256, 256, QImage::Format_RGBA8888);
    images[0].fill(QColor(226, 235, 248));
    images[1].fill(QColor(42, 104, 36));
    for(int material = 1; material <= 2; ++material)
    {
        STerrainTransitionVariant uniform;
        uniform.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(material - 1);
        uniform.packedTile = packed.encode();
        catalog.addVariant(uniform);
    }
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Ice}, {2, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x73u, {}, styles);
    const STerrainTransitionSignature requested = signature(1, 1, 1, 2);
    const QImage raw = atlas.reviewBlendMask(requested, 1, 0, nullptr, 1);
    QString maskProvenance;
    const QImage effective = atlas.reviewEffectiveBlendMask(
        requested, 1, 0, &maskProvenance, 1);
    QString resultProvenance;
    CValidationReport report;
    const QImage generated = atlas.reviewGeneratedBlend(
        requested, 0, &resultProvenance, &report, 1);
    QVERIFY2(!raw.isNull() && !effective.isNull() && !generated.isNull(),
             qPrintable(report.toText()));
    QVERIFY(raw != effective);
    QCOMPARE(maskProvenance, resultProvenance);
    int purePixels = 0;
    for(int y = 0; y < effective.height(); ++y)
        for(int x = 0; x < effective.width(); ++x)
        {
            const int value = effective.pixelColor(x, y).red();
            if(value != 0 && value != 255) continue;
            ++purePixels;
            const QColor expected = value == 255
                ? images[0].pixelColor(x, y)
                : images[1].pixelColor(x, y);
            QCOMPARE(generated.pixelColor(x, y), expected);
        }
    QVERIFY(purePixels > 0);
}

void TerrainVirtualAtlasTests::finalizationCarriesPaintMaterialIntoReviewedMaskSelection()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << solid(qRgb(90, 75, 55)) << solid(qRgb(35, 110, 45));
    for(int material = 1; material <= 2; ++material)
    {
        STerrainTransitionVariant uniform;
        uniform.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(material - 1);
        uniform.packedTile = packed.encode();
        catalog.addVariant(uniform);
    }
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Gravel}, {2, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x72u, {}, styles);
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1));
    field.setMaterial(0, 0, 1);
    field.setMaterial(1, 0, 2);
    field.setMaterial(1, 1, 2);
    field.setMaterial(0, 1, 2);

    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("FinalizationPaintOwner"), 512, {}, &report,
        {}, {}, {}, {1});
    QVERIFY2(finalized.success, qPrintable(report.toText()));
    QCOMPARE(finalized.tiles.size(), 1);
    QVERIFY2(finalized.tiles.front().blendProvenance.contains(
                 QStringLiteral(":r75-approved-authored-restoration:")),
             qPrintable(finalized.tiles.front().blendProvenance));
}

void TerrainVirtualAtlasTests::reviewedTopologyNeverFallsBackToUnapprovedVariation()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << solid(qRgb(90, 75, 55)) << solid(qRgb(35, 110, 45));
    for(int material = 1; material <= 2; ++material)
    {
        STerrainTransitionVariant uniform;
        uniform.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(material - 1);
        uniform.packedTile = packed.encode();
        catalog.addVariant(uniform);
    }
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Gravel}, {2, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x72u, {}, styles);
    const STerrainTransitionSignature outer = signature(1, 2, 2, 2);
    QSet<QString> provenance;
    for(int y = 0; y < 16; ++y)
        for(int x = 0; x < 16; ++x)
        {
            CValidationReport report;
            const STerrainVirtualTile tile = atlas.resolve(
                outer, x, y, &report, 1);
            QVERIFY2(tile.generatedIndex >= 0, qPrintable(report.toText()));
            provenance.insert(atlas.generatedTiles().at(
                tile.generatedIndex).blendProvenance);
        }
    QCOMPARE(provenance, QSet<QString>({
        QStringLiteral(
            "canonical-generated:gravel-chunks:outer-1of4-v1:"
            "r75-approved-authored-restoration:bz13h:213"),
        QStringLiteral(
            "canonical-generated:gravel-chunks:outer-1of4-v2:"
            "r75-approved-authored-restoration:bz13h:214")
    }));
}

void TerrainVirtualAtlasTests::virtualFieldPaintIsUnboundedByAtlas()
{
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(3,3,1));
    STerrainTileBrush brush;
    brush.centerX=1.5f; brush.centerY=1.5f; brush.radius=2.0f;
    QCOMPARE(field.paint(brush,99),12);
    QCOMPARE(field.material(1,1),qint32(99));
}

void TerrainVirtualAtlasTests::rejectedPaintArtworkIsPreservedByExactFinalization()
{
    Fixture fixture;
    const auto mixed=signature(1,2,2,1);
    fixture.images<<solid(qRgba(20,40,60,255))<<solid(qRgba(60,40,20,255));
    for(int slot:{4,5})for(int rotation=0;rotation<4;++rotation) {
        STerrainTransitionVariant variant;
        variant.signature=CTerrainTransitionCatalog::rotated(mixed,-rotation);
        SMprPackedTile packed;packed.tileIndex=slot;packed.rotation=rotation;
        variant.packedTile=packed.encode();variant.paintSelectable=slot!=4;
        QVERIFY(fixture.catalog.addVariant(variant));
    }
    CValidationReport report;bool found=false;
    const auto desired=fixture.catalog.gridSignatureForPacked(4,&found);QVERIFY(found);
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images,23);
    const auto fresh=atlas.resolve(desired,0,0,&report,1);
    QVERIFY(fresh.authored);QCOMPARE(SMprPackedTile::decode(fresh.authoredPacked).atlasSlot(),5);
    CTerrainVirtualMaterialField field;QVERIFY(field.resize(1,1,1));
    field.setMaterial(0,0,desired.northWest);field.setMaterial(1,0,desired.northEast);
    field.setMaterial(1,1,desired.southEast);field.setMaterial(0,1,desired.southWest);
    const auto finalized=atlas.CTerrainVirtualAtlas::finalize(field,"preserved-rejected",64,
        QVector<qint32>(6,0),&report,{}, {},{4},{},&fixture.catalog,{true});
    QVERIFY2(finalized.success,qPrintable(report.toText()));
    const int output=SMprPackedTile::decode(finalized.packedCells[0]).atlasSlot();
    QCOMPARE(finalized.tiles[output].pixels,fixture.images[4]);
    QVERIFY(!finalized.tiles[output].paintSelectable);
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
}

void TerrainVirtualAtlasTests::paintReferenceImportsAuthorPixelsWithoutRebaking()
{
    Fixture fixture;
    const auto mixed=signature(1,2,2,1);
    fixture.images.append(solid(qRgba(20,40,60,255)));
    STerrainTransitionVariant old;old.signature=mixed;old.packedTile=4;
    QVERIFY(fixture.catalog.addVariant(old));
    const auto reference=QByteArray(64,'e');
    fixture.catalog.setPaintReference(reference,false);
    bool found=false;
    QCOMPARE(fixture.catalog.gridSignatureForPacked(4,&found),mixed);QVERIFY(found);
    fixture.catalog.selectForPaint(mixed,0,0,23,&found);QVERIFY(!found);
    auto legacy=fixture.catalog;legacy.setPaintReference({},true);
    QCOMPARE(legacy.selectForPaint(mixed,0,0,23,&found),quint16(4));QVERIFY(found);

    const QMap<qint32,qint32> runtime{{1,0},{2,0},{3,0},{4,0}};
    CTerrainWorkingAtlas working(fixture.catalog,fixture.images,23,64,{}, {},
        QVector<qint32>(5,0),runtime);
    QVERIFY(working.isValid());CValidationReport report;
    const QImage author=solid(qRgba(84,122,166,255));
    QVERIFY(working.addAuthoredLibraryTransition(mixed,author,"author tile",&report,"native",7));
    QMap<int,quint64> usage;const QSet<int> protectedSlots{0,1,2,3,4};
    QVERIFY(working.realizeAuthoredLibraryVariants(mixed,&usage,protectedSlots,&report,1,0,true));
    const auto packed=working.resolve(mixed,3,7,&usage,protectedSlots,nullptr,&report,1,0,true);
    const int slot=SMprPackedTile::decode(packed).atlasSlot();
    QCOMPARE(working.activeTileImages().at(slot),author);
    STerrainWorkingTileIdentity identity;QVERIFY(working.tileIdentityForSlot(slot,&identity));
    QCOMPARE(identity.sourceNamespace,QString("native"));
    QVERIFY(!identity.canonicalGeneratedBlend);
    const auto fingerprint=working.stateFingerprint();
    QCOMPARE(working.resolve(mixed,3,7,&usage,protectedSlots,nullptr,&report,1,0,true),packed);
    QCOMPARE(working.stateFingerprint(),fingerprint);
    const auto staged=working.createStagingCopy();
    QCOMPARE(staged->catalog().paintReferenceSha256(),reference);
    QVERIFY(working.canCommitStaging(*staged));
    for(int i=0;i<fixture.images.size();++i)QCOMPARE(working.activeTileImages().at(i),fixture.images.at(i));
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));

    CTestTerrainVirtualAtlas retained(fixture.catalog,fixture.images,23);
    CTerrainVirtualMaterialField field;QVERIFY(field.resize(1,1,1));
    field.setMaterial(0,0,1);field.setMaterial(1,0,2);
    field.setMaterial(1,1,2);field.setMaterial(0,1,1);
    const auto final=retained.CTerrainVirtualAtlas::finalize(field,"reference-policy",64,
        QVector<qint32>(5,0),&report,{}, {},{4},{},&fixture.catalog,{true});
    QVERIFY2(final.success,qPrintable(report.toText()));
    QCOMPARE(final.paintReferenceSha256,reference);
    const auto& output=final.tiles[SMprPackedTile::decode(final.packedCells[0]).atlasSlot()];
    QCOMPARE(output.pixels,fixture.images[4]);QVERIFY(output.paintSelectable);
    QVERIFY(!output.referencePaintSelectable);
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

void TerrainVirtualAtlasTests::workingAtlasReportsLiveImportedAndGeneratedProvenance()
{
    Fixture fixture;
    CTestTerrainWorkingAtlas working(
        fixture.catalog, fixture.images, 47u, 32);
    QVERIFY(working.isValid());
    QCOMPARE(working.tileOriginForSlot(0),
             ETerrainWorkingTileOrigin::AuthoredMap);
    QCOMPARE(working.tileOriginForSlot(31),
             ETerrainWorkingTileOrigin::Unknown);

    QMap<int, quint64> usage;
    for(int slot = 0; slot < fixture.images.size(); ++slot)
        usage.insert(slot, 1);

    const STerrainTransitionSignature importedSignature =
        signature(1, 2, 2, 2);
    QImage importedPixels(8, 8, QImage::Format_RGBA8888);
    importedPixels.fill(QColor(117, 83, 41));
    QVERIFY(working.addAuthoredLibraryTransition(
        importedSignature, importedPixels,
        QStringLiteral("ZoneReview slot 137"), nullptr,
        QStringLiteral("ZoneReview"), 137));
    CValidationReport report;
    QVERIFY(working.realizeAuthoredLibraryVariants(
        importedSignature, &usage, {}, &report, 1));
    bool generated = true;
    const quint16 importedPacked = working.resolve(
        importedSignature, 3, 5, &usage, {}, &generated, &report, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(generated); // Imported artwork lives in the extension namespace.

    STerrainWorkingTileIdentity importedIdentity;
    QVERIFY(working.tileIdentityForSlot(
        SMprPackedTile::decode(importedPacked).atlasSlot(),
        &importedIdentity));
    QCOMPARE(importedIdentity.origin,
             ETerrainWorkingTileOrigin::ImportedAuthored);
    QCOMPARE(importedIdentity.blendProvenance,
             QStringLiteral("ZoneReview slot 137"));
    QCOMPARE(importedIdentity.sourceNamespace,
             QStringLiteral("ZoneReview"));
    QCOMPARE(importedIdentity.sourceSlot, 137);
    QVERIFY(!importedIdentity.physicalPixelSha256.isEmpty());
    QVERIFY(importedIdentity.stockDerivedBlend);
    QVERIFY(!importedIdentity.canonicalGeneratedBlend);
    const int importedSlot = SMprPackedTile::decode(importedPacked).atlasSlot();
    QCOMPARE(working.tileOriginForSlot(importedSlot),
             ETerrainWorkingTileOrigin::ImportedAuthored);
    ++usage[importedSlot];

    seedReviewedStockMasks(working, fixture);
    const STerrainTransitionSignature generatedSignature =
        signature(1, 2, 3, 4);
    const quint16 generatedPacked = working.resolve(
        generatedSignature, 8, 9, &usage, {}, &generated, &report, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(generated);
    STerrainWorkingTileIdentity generatedIdentity;
    QVERIFY(working.tileIdentityForSlot(
        SMprPackedTile::decode(generatedPacked).atlasSlot(),
        &generatedIdentity));
    QCOMPARE(generatedIdentity.origin,
             ETerrainWorkingTileOrigin::CanonicalGenerated);
    QVERIFY(generatedIdentity.blendProvenance.startsWith(
        QStringLiteral("canonical-generated:")));
    QVERIFY(!generatedIdentity.physicalPixelSha256.isEmpty());
    QVERIFY(!generatedIdentity.stockDerivedBlend);
    QVERIFY(generatedIdentity.canonicalGeneratedBlend);

    // The UI finalizer rebuilds from the flattened working images/catalog.
    // Before provenance restoration that makes extension artwork look like
    // retained map-authored slots, producing a false zero-added preview.
    STerrainAtlasFinalization flattened;
    flattened.success = true;
    flattened.tiles.resize(3);
    for(STerrainFinalizedTile& tile : flattened.tiles)
        tile.authored = true;
    const int generatedSlot = SMprPackedTile::decode(generatedPacked).atlasSlot();
    QCOMPARE(working.tileOriginForSlot(generatedSlot),
             ETerrainWorkingTileOrigin::CanonicalGenerated);
    flattened.authoredSlotRemap.insert(0, 0);
    flattened.authoredSlotRemap.insert(importedSlot, 1);
    flattened.authoredSlotRemap.insert(generatedSlot, 2);
    STerrainWorkingFinalizationOrigins origins;
    QVERIFY2(working.restoreFinalizedProvenance(
                 &flattened, &origins, QStringLiteral("CaveSet"),
                 QByteArray("material-field-sha"), {}, &report),
             qPrintable(report.toText()));
    QCOMPARE(origins.retainedMapOutputs, QSet<int>({0}));
    QCOMPARE(origins.importedAuthoredOutputs, QSet<int>({1}));
    QCOMPARE(origins.canonicalGeneratedOutputs, QSet<int>({2}));
    QVERIFY(flattened.tiles.at(0).authored);
    QCOMPARE(flattened.tiles.at(0).sourceNamespace,
             QStringLiteral("CaveSet"));
    QVERIFY(flattened.tiles.at(1).authored);
    QCOMPARE(flattened.tiles.at(1).sourceNamespace,
             QStringLiteral("ZoneReview"));
    QCOMPARE(flattened.tiles.at(1).sourceSlot, 137);
    QCOMPARE(flattened.tiles.at(1).canonicalSignature,
             importedIdentity.canonicalSignature);
    QVERIFY(!flattened.tiles.at(2).authored);
    QCOMPARE(flattened.tiles.at(2).canonicalSignature,
             generatedIdentity.canonicalSignature);
    QVERIFY(flattened.tiles.at(2).canonicalGeneratedBlend);
    QVERIFY(flattened.tiles.at(2).blendProvenance.startsWith(
        QStringLiteral("canonical-generated:")));
    QCOMPARE(flattened.stableSha256.size(), 64);
    QVERIFY(QRegularExpression(QStringLiteral("^caveset_[0-9a-f]{12}$"))
                .match(flattened.isolatedNamespace).hasMatch());
}

void TerrainVirtualAtlasTests::workingAtlasKeepsCurrentMapExactVariantAheadOfImportedAliases()
{
    Fixture fixture;
    CTestTerrainWorkingAtlas working(
        fixture.catalog, fixture.images, 0x31415926u, 16);
    const STerrainTransitionSignature exact = signature(2, 2, 2, 2);
    CValidationReport report;
    QVERIFY(working.addAuthoredLibraryTransition(
        exact, solid(qRgba(241, 37, 173, 255)),
        QStringLiteral("unrelated-global-alias"), &report,
        QStringLiteral("OtherMap"), 326));

    QMap<int, quint64> usage;
    for(int slot = 0; slot < working.capacityLimit(); ++slot)
        usage.insert(slot, 0);
    QVERIFY2(working.realizeAuthoredLibraryVariants(
        exact, &usage, {}, &report, 2), qPrintable(report.toText()));

    bool importedRealized = false;
    for(int slot = fixture.images.size(); slot < working.activeTileCount(); ++slot)
        importedRealized |= working.tileOriginForSlot(slot)
            == ETerrainWorkingTileOrigin::ImportedAuthored;
    QVERIFY(importedRealized);

    // Importing a global alias for some other missing tuple must not make that
    // artwork compete with an exact same-runtime tile already authored by the
    // current map. This is the post-merge Zone1 slot 26 regression.
    for(int x = 0; x < 128; ++x)
    {
        bool found = false;
        const quint16 packed = working.catalog().selectForPaint(
            exact, x, 11, 0x31415926u, &found);
        QVERIFY(found);
        QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(), 1);
    }
}

void TerrainVirtualAtlasTests::workingAtlasKeepsMatchingRuntimeImportWhenLocalTypeDiffers()
{
    Fixture fixture;
    const QVector<qint32> sourceTypes = {0, 0, 2, 3};
    const QMap<qint32, qint32> materialTypes = {
        {1, 0}, {2, 1}, {3, 2}, {4, 3}};
    CTestTerrainWorkingAtlas working(
        fixture.catalog, fixture.images, 0x31415926u, 16, {}, {},
        sourceTypes, materialTypes);
    const STerrainTransitionSignature exact = signature(2, 2, 2, 2);
    CValidationReport report;
    QVERIFY(working.addAuthoredLibraryTransition(
        exact, solid(qRgba(241, 37, 173, 255)),
        QStringLiteral("matching-runtime-global"), &report,
        QStringLiteral("OtherMap"), 326));

    QMap<int, quint64> usage;
    for(int slot = 0; slot < working.capacityLimit(); ++slot)
        usage.insert(slot, 0);
    QVERIFY2(working.realizeAuthoredLibraryVariants(
        exact, &usage, {}, &report, 2, 1, true),
        qPrintable(report.toText()));

    bool importedCandidate = false;
    for(const STerrainTransitionVariant& variant :
            working.catalog().paintVariants(exact))
    {
        const int slot = SMprPackedTile::decode(variant.packedTile).atlasSlot();
        importedCandidate |= slot >= fixture.images.size();
    }
    QVERIFY2(importedCandidate,
             "A matching-runtime import was hidden by a wrong-runtime local tile");
}

void TerrainVirtualAtlasTests::workingAtlasAcceptsLazyMaterialSourcesWithoutResettingState()
{
    Fixture fixture;
    CTerrainTransitionCatalog catalog = fixture.catalog;
    catalog.declareMaterial(9);
    QMap<qint32, qint32> runtimeTypes = reviewedFixtureMaterialTypes(catalog);
    runtimeTypes.insert(9, 1);
    CTestTerrainWorkingAtlas atlas(
        catalog, fixture.images, 0x10203040u, 64, {}, {},
        reviewedFixtureTileTypes(fixture.images.size()), runtimeTypes);
    QVERIFY(atlas.isValid());
    QVERIFY(atlas.hasMaterialSource(1));

    const QByteArray before = atlas.stateFingerprint();
    CValidationReport report;
    QMap<int, quint64> usage;
    QSet<int> protectedSlots;
    const STerrainTransitionSignature requested = signature(1, 9, 9, 1);
    atlas.resolve(requested, 3, 4, &usage, protectedSlots,
                  nullptr, &report, 9, 1);
    QVERIFY(report.hasErrors());
    QCOMPARE(atlas.stateFingerprint(), before);

    report = CValidationReport();
    QVERIFY(atlas.addExternalMaterialSource(9, solid(qRgba(90, 70, 40, 255))));
    // A foreign source with the same logical id must not replace the opened
    // map's authoritative uniform artwork.
    QVERIFY(atlas.addExternalMaterialSource(1, solid(qRgba(1, 1, 1, 255))));
    QCOMPARE(atlas.stateFingerprint(), before);
    bool generated = false;
    const quint16 packed = atlas.resolve(
        requested, 3, 4, &usage, protectedSlots,
        &generated, &report, 9, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(generated);
    const int generatedSlot = SMprPackedTile::decode(packed).atlasSlot();
    QVERIFY(generatedSlot >= fixture.images.size());

    CTestTerrainWorkingAtlas baseline(
        catalog, fixture.images, 0x10203040u, 64, {}, {},
        reviewedFixtureTileTypes(fixture.images.size()), runtimeTypes);
    QVERIFY(baseline.addExternalMaterialSource(
        9, solid(qRgba(90, 70, 40, 255))));
    CValidationReport baselineReport;
    QMap<int, quint64> baselineUsage;
    const quint16 baselinePacked = baseline.resolve(
        requested, 3, 4, &baselineUsage, protectedSlots,
        nullptr, &baselineReport, 9, 1);
    QVERIFY2(!baselineReport.hasErrors(), qPrintable(baselineReport.toText()));
    QCOMPARE(atlas.activeTileImages().at(generatedSlot),
             baseline.activeTileImages().at(
                 SMprPackedTile::decode(baselinePacked).atlasSlot()));

    const STerrainTransitionSignature pureForeign = signature(9, 9, 9, 9);
    atlas.resolve(pureForeign, 9, 4, &usage, protectedSlots,
                  nullptr, &report, 9, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    for(int y = 0; y < field.vertexHeight(); ++y)
        for(int x = 8; x < field.vertexWidth(); ++x)
            QVERIFY(field.setMaterial(x, y, 9));
    CTestTerrainVirtualAtlas publisher(
        atlas.catalog(), atlas.activeTileImages(), 0x10203040u, {}, {}, true,
        atlas.activeTileTypes(), runtimeTypes);
    STerrainAtlasFinalization finalized = publisher.finalize(
        field, QStringLiteral("LazyForeign"), 64,
        atlas.activeTileTypes(), &report);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    STerrainWorkingFinalizationOrigins origins;
    QVERIFY2(atlas.restoreFinalizedProvenance(
                 &finalized, &origins, QStringLiteral("LazyForeign"),
                 field.stableSha256(), {}, &report),
             qPrintable(report.toText()));
    QVERIFY(!origins.canonicalGeneratedOutputs.isEmpty());

    CMprDocument source;
    QVERIFY(source.loadBytes(portableMpr(), "lazy-source.mpr", &report));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            source, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n") + report.toText()));
    CMprDocument reopened;
    QVERIFY(reopened.loadBytes(package.mprBytes, "lazy-result.mpr", &report));
    CTerrainEditModel reopenedTerrain;
    QVERIFY(reopenedTerrain.load(reopened, &report));
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool available = false;
            const quint16 reopenedPacked = reopenedTerrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            bool decoded = false;
            QCOMPARE(finalizedSignature(
                finalized, reopenedPacked, &decoded),
                field.cellSignature(x, y));
            QVERIFY(decoded);
        }
}

void TerrainVirtualAtlasTests::nonUniformResolveUsesAuthoredRotationOrGeneratesExact()
{
    Fixture fixture;
    const STerrainTransitionSignature authored = signature(1, 2, 3, 4);
    const int authoredSlot = fixture.images.size();
    fixture.images.append(solid(qRgba(117, 83, 191, 255)));
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        STerrainTransitionVariant variant;
        variant.signature = CTerrainTransitionCatalog::rotated(
            authored, -rotation);
        SMprPackedTile packed;
        packed.tileIndex = quint8(authoredSlot);
        packed.rotation = quint8(rotation);
        variant.packedTile = packed.encode();
        QVERIFY(fixture.catalog.addVariant(variant));
    }

    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x45584143u);
    const STerrainVirtualTile rotated = atlas.resolve(
        CTerrainTransitionCatalog::rotated(authored, -1), 6, 9);
    QVERIFY(rotated.authored);
    QCOMPARE(SMprPackedTile::decode(rotated.authoredPacked).atlasSlot(),
             authoredSlot);
    QCOMPARE(SMprPackedTile::decode(rotated.authoredPacked).rotation,
             quint8(1));
    QCOMPARE(atlas.generatedTiles().size(), 0);

    // No authored tile has this exact half/half signature. Uniform artwork for
    // both materials exists, but it is not an acceptable blend substitute;
    // the virtual resolver must synthesize the exact requested tuple.
    const STerrainTransitionSignature missingBlend = signature(1, 1, 2, 2);
    CValidationReport report;
    const STerrainVirtualTile generated = atlas.resolve(
        missingBlend, 11, 13, &report, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(!generated.authored);
    QVERIFY(!generated.importedAuthored);
    QVERIFY(generated.generatedIndex >= 0);
    const STerrainFinalizedTile generatedTile =
        atlas.generatedTiles().at(generated.generatedIndex);
    QVERIFY(generatedTile.canonicalGeneratedBlend);
    const STerrainTransitionSignature canonical =
        generatedTile.canonicalSignature;
    QVERIFY(!(canonical.northWest == canonical.northEast
              && canonical.northWest == canonical.southEast
              && canonical.northWest == canonical.southWest));

    const STerrainVirtualTile uniform = atlas.resolve(
        signature(2, 2, 2, 2), 11, 13, &report, 2);
    QVERIFY(uniform.authored);
    QCOMPARE(SMprPackedTile::decode(uniform.authoredPacked).atlasSlot(), 1);
}

void TerrainVirtualAtlasTests::generatedReviewOutcomeUsesRuntimeBlendGenerator()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0x6248u);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainTransitionSignature requested = signature(1, 2, 2, 2);
    QVector<QImage> reviewed;
    for(int variation = 0;
        variation < CTerrainVirtualAtlas::blendVariationLimit(requested);
        ++variation)
    {
        QString provenance;
        CValidationReport report;
        const QImage result = atlas.reviewGeneratedBlend(
            requested, variation, &provenance, &report);
        QVERIFY2(!result.isNull(), qPrintable(report.toText()));
        QVERIFY(!provenance.isEmpty());
        reviewed.append(result);
    }
    bool matchedRuntime = false;
    for(int y = 0; y < 16 && !matchedRuntime; ++y)
        for(int x = 0; x < 16 && !matchedRuntime; ++x)
        {
            CValidationReport report;
            const STerrainVirtualTile tile = atlas.resolve(
                requested, x, y, &report);
            QVERIFY2(tile.generatedIndex >= 0, qPrintable(report.toText()));
            matchedRuntime = reviewed.contains(
                atlas.generatedTiles().at(tile.generatedIndex).pixels);
        }
    QVERIFY(matchedRuntime);
}

void TerrainVirtualAtlasTests::canonicalMasksAreAvailableWithoutStockTemplates()
{
    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 19u);
    CValidationReport report;
    const STerrainVirtualTile resolved = atlas.resolve(
        signature(1, 2, 2, 2), 0, 0, &report);
    QVERIFY2(resolved.generatedIndex >= 0, qPrintable(report.toText()));
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(atlas.generatedTiles().size(), 1);
    QString provenance;
    const QImage mask = atlas.reviewBlendMask(
        signature(1, 2, 2, 2), 1, 0, &provenance);
    QVERIFY(!mask.isNull());
    QVERIFY(provenance.startsWith(QStringLiteral("canonical-generated:")));
}

void TerrainVirtualAtlasTests::contextFreeResolutionUsesReviewedMaterialMask()
{
    struct ReviewedCase
    {
        STerrainTransitionSignature signature;
        qint32 ownerMaterial;
        ETerrainBlendStyle ownerStyle;
        const char* expectedProvenance;
    };
    const ReviewedCase cases[] = {
        {signature(2, 1, 1, 1), 1, ETerrainBlendStyle::Soil, ""},
        {signature(2, 1, 1, 2), 2, ETerrainBlendStyle::Stone,
         "canonical-generated:stone-fracture:edge-2of4-v2:r155-approved-from-edge-v1"}
    };

    for(const ReviewedCase& item : cases)
    {
        Fixture fixture;
        QMap<qint32, ETerrainBlendStyle> styles;
        styles.insert(item.ownerMaterial, item.ownerStyle);
        styles.insert(item.ownerMaterial == 1 ? 2 : 1,
                      ETerrainBlendStyle::Grass);
        CTestTerrainVirtualAtlas atlas(
            fixture.catalog, fixture.images, 0x52455657u, {}, styles);
        QVERIFY(atlas.isValid());

        CValidationReport report;
        const STerrainVirtualTile resolved = atlas.resolve(
            item.signature, 0, 0, &report, item.ownerMaterial);
        QVERIFY2(resolved.generatedIndex >= 0, qPrintable(report.toText()));
        const QString provenance = atlas.generatedTiles().at(
            resolved.generatedIndex).blendProvenance;
        if(item.expectedProvenance[0] != '\0')
            QCOMPARE(provenance, QString::fromLatin1(item.expectedProvenance));
        QVERIFY2(provenance.contains(QStringLiteral("-approved")),
                 qPrintable(QStringLiteral(
                     "Context-free production resolution used an unreviewed mathematical mask: %1")
                     .arg(provenance)));
    }
}

void TerrainVirtualAtlasTests::lavaUsesOnlyExplicitR123ReviewedMaskRoutes()
{
    struct LavaCase
    {
        STerrainTransitionSignature signature;
        const char* pixelSha256;
        const char* provenance;
    };
    const LavaCase cases[] = {
        {signature(1, 2, 2, 2),
         "8bbcc4762db352ace282d7f98e179d34430b498c3c042774d0a85114a2db7394",
         "canonical-generated:lava-flow:outer-1of4-v1:r123-approved-liquid-flow"},
        {signature(1, 1, 2, 2),
         "1ba9c2e33295d6f400f577a7615a8100963be76de753705f421ea27be436e7a7",
         "canonical-generated:lava-flow:edge-2of4-v1:r123-approved-fracture-flow"},
        {signature(1, 2, 1, 2),
         "dbb729aca28b033be78a2f603d95b5f7ee3ad9936b28ddc9de6a3dc0eb73ffc0",
         "canonical-generated:lava-flow:diagonal-2of4:r123-approved-fracture-flow"},
        {signature(1, 1, 1, 2),
         "47f439119039111046f1aa7bc40b4d404dab03fdf47934707cdb5c39667d15c1",
         "canonical-generated:lava-flow:inside-3of4-v1:r123-approved-fracture-flow"}
    };

    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << QImage(256, 256, QImage::Format_RGBA8888)
           << QImage(256, 256, QImage::Format_RGBA8888);
    images[0].fill(QColor(210, 55, 20));
    images[1].fill(QColor(100, 85, 65));
    for(int material = 1; material <= 2; ++material)
    {
        STerrainTransitionVariant uniform;
        uniform.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(material - 1);
        uniform.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(uniform));
    }
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Lava}, {2, ETerrainBlendStyle::Organic}
    };
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x123u, {}, styles);
    for(const LavaCase& item : cases)
    {
        QString provenance;
        const QImage mask = atlas.reviewBlendMask(
            item.signature, 1, 0, &provenance, 1);
        QVERIFY2(!mask.isNull(), item.provenance);
        QCOMPARE(grayscalePixelHash(mask.mirrored(false, true)),
                 QByteArray(item.pixelSha256));
        QCOMPARE(provenance, QString::fromLatin1(item.provenance));

        const STerrainTransitionSignature rotated =
            CTerrainTransitionCatalog::rotated(item.signature, 1);
        QCOMPARE(atlas.reviewBlendMask(rotated, 1, 0, nullptr, 1),
                 mask.transformed(QTransform().rotate(-90.0),
                                  Qt::FastTransformation));
    }
}

void TerrainVirtualAtlasTests::authoredMapTransitionsDoNotPopulateObsoleteMaskPool()
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
        QCOMPARE(atlas.stockBlendMaskCountFor(requested), 0);
        CValidationReport report;
        const STerrainVirtualTile resolved = atlas.resolve(
            requested, 0, 0, &report);
        QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
        QVERIFY(resolved.generatedIndex >= 0);
    }
    for(const STerrainTransitionSignature& stock : stockPatterns)
    {
        CValidationReport report;
        const STerrainVirtualTile resolved = atlas.resolve(
            stock, 0, 0, &report);
        QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
        QVERIFY(resolved.authored);
    }
}

void TerrainVirtualAtlasTests::canonicalMasksMeetQuarterCoverageAndCornerContract()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QVector<ETerrainBlendStyle> styles = {
        ETerrainBlendStyle::Organic, ETerrainBlendStyle::Soil,
        ETerrainBlendStyle::Grass, ETerrainBlendStyle::Sand,
        ETerrainBlendStyle::Gravel, ETerrainBlendStyle::Stone,
        ETerrainBlendStyle::Paving, ETerrainBlendStyle::Snow,
        ETerrainBlendStyle::Ice, ETerrainBlendStyle::Water,
        ETerrainBlendStyle::Swamp, ETerrainBlendStyle::Lava
    };
    const QVector<STerrainTransitionSignature> patterns = {
        signature(1, 2, 2, 2), signature(1, 1, 2, 2),
        signature(1, 2, 1, 2)
    };
    for(ETerrainBlendStyle style : styles)
    {
        const QMap<qint32, ETerrainBlendStyle> materialStyles = {
            {1, style}, {2, ETerrainBlendStyle::Organic}
        };
        CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images,
            19u, {}, materialStyles);
        for(const STerrainTransitionSignature& requested : patterns)
        {
            const STerrainTransitionSignature preview =
                CTerrainTransitionCatalog::mirroredGridY(requested);
            const qint32 previewCorners[4] = {preview.northWest,
                preview.northEast, preview.southEast, preview.southWest};
            for(qint32 owner : {qint32(1), qint32(2)})
                for(int variation = 0;
                    variation < CTerrainVirtualAtlas::blendVariationLimit(requested);
                    ++variation)
                {
                    const QImage mask = atlas.reviewBlendMask(
                        requested, owner, variation);
                    QVERIFY2(!mask.isNull(), qPrintable(QString(
                        "Missing canonical %1 mask").arg(
                            CTerrainVirtualAtlas::blendStyleName(style))));
                    const int half = mask.width() / 2;
                    const int patch = qMax(2, mask.width() / 16);
                    const QPoint origins[4] = {{0, 0}, {half, 0},
                        {half, half}, {0, half}};
                    for(int corner = 0; corner < 4; ++corner)
                    {
                        int strongOwner = 0;
                        int strongOriginal = 0;
                        int total = 0;
                        for(int y = origins[corner].y();
                            y < origins[corner].y() + half; ++y)
                            for(int x = origins[corner].x();
                                x < origins[corner].x() + half; ++x)
                            {
                                const int value = mask.pixelColor(x, y).red();
                                strongOwner += value >= 204;
                                strongOriginal += value <= 51;
                                ++total;
                            }
                        const bool affected = previewCorners[corner] == owner;
                        const int strong = affected ? strongOwner : strongOriginal;
                        QVERIFY2(strong * 100 >= total * 70,
                            qPrintable(QString("%1 corner %2 has only %3% strong coverage")
                                .arg(CTerrainVirtualAtlas::blendStyleName(style))
                                .arg(corner).arg(qRound(100.0 * strong / total))));
                        if(!affected)
                            QVERIFY2(strongOwner * 100 <= total * 30,
                                "Protected quarter exceeds 30% strong invasion");

                        const bool right = corner == 1 || corner == 2;
                        const bool bottom = corner == 2 || corner == 3;
                        for(int py = 0; py < patch; ++py)
                            for(int px = 0; px < patch; ++px)
                            {
                                const int x = right ? mask.width() - 1 - px : px;
                                const int y = bottom ? mask.height() - 1 - py : py;
                                const int value = mask.pixelColor(x, y).red();
                                QVERIFY2(affected ? value == 255 : value == 0,
                                    "Corner safety patch is not solid");
                            }
                    }
                }
        }
    }
}

void TerrainVirtualAtlasTests::canonicalMasksAreExactComplementsAndRotationCovariant()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Grass}, {2, ETerrainBlendStyle::Stone}
    };
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 73u, {}, styles);
    for(const STerrainTransitionSignature& requested : {
            signature(1, 2, 2, 2), signature(1, 1, 2, 2),
            signature(1, 2, 1, 2)})
        for(int variation = 0;
            variation < CTerrainVirtualAtlas::blendVariationLimit(requested);
            ++variation)
        {
            const QImage owner = atlas.reviewBlendMask(
                requested, 1, variation);
            const QImage support = atlas.reviewBlendMask(
                requested, 2, variation);
            QVERIFY(!owner.isNull());
            QCOMPARE(owner.size(), support.size());
            for(int y = 0; y < owner.height(); ++y)
                for(int x = 0; x < owner.width(); ++x)
                    QCOMPARE(owner.pixelColor(x, y).red()
                             + support.pixelColor(x, y).red(), 255);

            const STerrainTransitionSignature rotated =
                CTerrainTransitionCatalog::rotated(requested, 1);
            const QImage rotatedMask = atlas.reviewBlendMask(
                rotated, 1, variation);
            const QImage expected = owner.transformed(
                QTransform().rotate(-90.0), Qt::FastTransformation);
            QCOMPARE(rotatedMask, expected);
        }

    for(const STerrainTransitionSignature& requested : {
            signature(1, 2, 2, 2), signature(1, 1, 2, 2)})
    {
        const QImage first = atlas.reviewBlendMask(requested, 1, 0);
        const QImage second = atlas.reviewBlendMask(requested, 1, 1);
        for(int index = 0; index < first.width(); ++index)
        {
            QCOMPARE(first.pixelColor(index, 0), second.pixelColor(index, 0));
            QCOMPARE(first.pixelColor(index, first.height() - 1),
                     second.pixelColor(index, second.height() - 1));
            QCOMPARE(first.pixelColor(0, index), second.pixelColor(0, index));
            QCOMPARE(first.pixelColor(first.width() - 1, index),
                     second.pixelColor(second.width() - 1, index));
        }
    }

    QString highOwnerProvenance;
    QVERIFY(!atlas.reviewBlendMask(
        signature(2, 1, 1, 1), 2, 0, &highOwnerProvenance).isNull());
    QVERIFY(highOwnerProvenance.endsWith(
        QStringLiteral(":outer-1of4-v1")));
}

void TerrainVirtualAtlasTests::canonicalMaskVariationsAreDeterministicAndMacroDistinct()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Grass}, {2, ETerrainBlendStyle::Organic}
    };
    CTestTerrainVirtualAtlas first(fixture.catalog, fixture.images, 19u, {}, styles);
    CTestTerrainVirtualAtlas repeated(fixture.catalog, fixture.images, 999u, {}, styles);
    const STerrainTransitionSignature requested = signature(1, 2, 2, 2);
    const QImage v1 = first.reviewBlendMask(requested, 1, 0);
    const QImage v2 = first.reviewBlendMask(requested, 1, 1);
    QCOMPARE(v1, repeated.reviewBlendMask(requested, 1, 0));
    QCOMPARE(v2, repeated.reviewBlendMask(requested, 1, 1));
    QVERIFY(v1 != v2);
    int macroDifferent = 0;
    int isolatedDifferent = 0;
    for(int y = 0; y < v1.height(); ++y)
        for(int x = 0; x < v1.width(); ++x)
        {
            if(qAbs(v1.pixelColor(x, y).red()
                    - v2.pixelColor(x, y).red()) < 64)
                continue;
            ++macroDifferent;
            bool hasNeighbour = false;
            for(int dy = -1; dy <= 1; ++dy)
                for(int dx = -1; dx <= 1; ++dx)
                    if((dx || dy) && x + dx >= 0 && x + dx < v1.width()
                       && y + dy >= 0 && y + dy < v1.height()
                       && qAbs(v1.pixelColor(x + dx, y + dy).red()
                               - v2.pixelColor(x + dx, y + dy).red()) >= 64)
                        hasNeighbour = true;
            isolatedDifferent += !hasNeighbour;
        }
    QVERIFY2(macroDifferent >= v1.width() * v1.height() / 40,
             "Variations differ only cosmetically, not at macro scale");
    QVERIFY2(isolatedDifferent * 20 <= qMax(1, macroDifferent),
             "Variation difference is dominated by isolated pixel noise");
}

void TerrainVirtualAtlasTests::canonicalAdjacentAndDiagonalTopologiesStayAnchored()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const QImage adjacent = atlas.reviewBlendMask(signature(1, 1, 2, 2), 1, 0);
    const QImage diagonal = atlas.reviewBlendMask(signature(1, 2, 1, 2), 1, 0);
    QVERIFY(!adjacent.isNull());
    QVERIFY(!diagonal.isNull());

    // A continuous adjacent-side mask must own the complete path between its
    // two requested corner patches. The diagonal mask must instead preserve
    // the two foreign corners and never become a three-corner source.
    const STerrainTransitionSignature adjacentPreview =
        CTerrainTransitionCatalog::mirroredGridY(signature(1, 1, 2, 2));
    const bool ownsTop = adjacentPreview.northWest == 1;
    const int adjacentY = ownsTop ? 0 : adjacent.height() - 1;
    for(int x = 0; x < adjacent.width(); ++x)
        QVERIFY2(adjacent.pixelColor(x, adjacentY).red() >= 204,
                 "Adjacent mask does not connect its two corner anchors");

    const STerrainTransitionSignature diagonalPreview =
        CTerrainTransitionCatalog::mirroredGridY(signature(1, 2, 1, 2));
    const qint32 diagonalCorners[4] = {diagonalPreview.northWest,
        diagonalPreview.northEast, diagonalPreview.southEast,
        diagonalPreview.southWest};
    const QPoint points[4] = {{0, 0}, {diagonal.width() - 1, 0},
        {diagonal.width() - 1, diagonal.height() - 1},
        {0, diagonal.height() - 1}};
    int ownedCorners = 0;
    for(int corner = 0; corner < 4; ++corner)
    {
        const bool expected = diagonalCorners[corner] == 1;
        const bool actual = diagonal.pixelColor(points[corner]).red() >= 204;
        QCOMPARE(actual, expected);
        ownedCorners += actual;
    }
    QCOMPARE(ownedCorners, 2);
    QVERIFY(atlas.reviewBlendMask(signature(1, 2, 3, 2), 1).isNull());
}

void TerrainVirtualAtlasTests::canonicalMasksHaveNoUnanchoredIslandsOrWideFeather()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QVector<ETerrainBlendStyle> styles = {
        ETerrainBlendStyle::Organic, ETerrainBlendStyle::Soil,
        ETerrainBlendStyle::Grass, ETerrainBlendStyle::Sand,
        ETerrainBlendStyle::Gravel, ETerrainBlendStyle::Stone,
        ETerrainBlendStyle::Paving, ETerrainBlendStyle::Snow,
        ETerrainBlendStyle::Ice, ETerrainBlendStyle::Water,
        ETerrainBlendStyle::Swamp, ETerrainBlendStyle::Lava
    };
    const QVector<STerrainTransitionSignature> patterns = {
        signature(1, 2, 2, 2), signature(1, 1, 2, 2),
        signature(1, 2, 1, 2)
    };
    for(ETerrainBlendStyle style : styles)
    {
        const QMap<qint32, ETerrainBlendStyle> materialStyles = {
            {1, style}, {2, ETerrainBlendStyle::Organic}
        };
        CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images,
            19u, {}, materialStyles);
        for(const STerrainTransitionSignature& requested : patterns)
            for(int variation = 0;
                variation < CTerrainVirtualAtlas::blendVariationLimit(requested);
                ++variation)
            {
                const QImage mask = atlas.reviewBlendMask(
                    requested, 1, variation);
                QVERIFY(!mask.isNull());
                const STerrainTransitionSignature preview =
                    CTerrainTransitionCatalog::mirroredGridY(requested);
                const qint32 expectedCorners[4] = {preview.northWest,
                    preview.northEast, preview.southEast, preview.southWest};
                const QPoint points[4] = {{0, 0}, {mask.width() - 1, 0},
                    {mask.width() - 1, mask.height() - 1},
                    {0, mask.height() - 1}};
                int boundary = 0;
                int feather = 0;
                for(int y = 0; y < mask.height(); ++y)
                    for(int x = 0; x < mask.width(); ++x)
                    {
                        const int value = mask.pixelColor(x, y).red();
                        feather += value >= 52 && value <= 203;
                        const bool owner = value >= 128;
                        if(x > 0)
                            boundary += owner
                                != (mask.pixelColor(x - 1, y).red() >= 128);
                        if(y > 0)
                            boundary += owner
                                != (mask.pixelColor(x, y - 1).red() >= 128);
                    }
                const double effectiveWidth = double(feather)
                    / double(qMax(1, boundary));
                QVERIFY2(effectiveWidth >= 0.5,
                    "Canonical boundary has no visible feather band");
                QVERIFY2(effectiveWidth <= mask.width() / 10.0,
                    "Canonical boundary feather is too broad");
                QVERIFY2(boundary <= mask.width() * 14,
                    "Canonical boundary is dominated by high-frequency noise");

                for(int polarity = 0; polarity < 2; ++polarity)
                {
                    QVector<quint8> visited(mask.width() * mask.height(), 0);
                    int components = 0;
                    for(int start = 0; start < visited.size(); ++start)
                    {
                        const int sx = start % mask.width();
                        const int sy = start / mask.width();
                        const bool matches = (mask.pixelColor(sx, sy).red() >= 128)
                            == bool(polarity);
                        if(visited[start] || !matches) continue;
                        ++components;
                        QVector<int> queue;
                        queue.append(start);
                        visited[start] = 1;
                        bool anchored = false;
                        for(int cursor = 0; cursor < queue.size(); ++cursor)
                        {
                            const int index = queue.at(cursor);
                            const int x = index % mask.width();
                            const int y = index / mask.width();
                            for(int corner = 0; corner < 4; ++corner)
                                anchored = anchored
                                    || (points[corner] == QPoint(x, y)
                                        && ((expectedCorners[corner] == 1)
                                            == bool(polarity)));
                            const int neighbours[4] = {index - 1, index + 1,
                                index - mask.width(), index + mask.width()};
                            for(int direction = 0; direction < 4; ++direction)
                            {
                                if((direction == 0 && x == 0)
                                   || (direction == 1 && x + 1 == mask.width())
                                   || (direction == 2 && y == 0)
                                   || (direction == 3 && y + 1 == mask.height()))
                                    continue;
                                const int next = neighbours[direction];
                                if(!visited[next]
                                   && ((mask.pixelColor(next % mask.width(),
                                        next / mask.width()).red() >= 128)
                                       == bool(polarity)))
                                {
                                    visited[next] = 1;
                                    queue.append(next);
                                }
                            }
                        }
                        QVERIFY2(anchored,
                            "Canonical mask contains an unanchored island or hole");
                    }
                    const bool diagonal = requested.northWest
                            == requested.southEast
                        && requested.northEast == requested.southWest;
                    if(diagonal)
                        QCOMPARE(components, 2);
                    else
                        QCOMPARE(components, 1);
                }
            }
    }
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

void TerrainVirtualAtlasTests::multiFamilyJunctionsUseApprovedAuthoredOwnershipInputs()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(256, 256, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QMap<qint32, ETerrainBlendStyle> styleSets[] = {
        {{1, ETerrainBlendStyle::Grass},
         {2, ETerrainBlendStyle::Paving},
         {3, ETerrainBlendStyle::Snow},
         {4, ETerrainBlendStyle::Water}},
        {{1, ETerrainBlendStyle::Soil},
         {2, ETerrainBlendStyle::Stone},
         {3, ETerrainBlendStyle::Sand},
         {4, ETerrainBlendStyle::Gravel}}
    };
    const STerrainTransitionSignature requests[] = {
        signature(1, 2, 3, 4),
        signature(1, 1, 2, 3),
        signature(1, 2, 1, 3)
    };
    const QPoint gridCorners[4] = {
        {0, 0}, {255, 0}, {255, 255}, {0, 255}
    };
    for(const QMap<qint32, ETerrainBlendStyle>& styles : styleSets)
    {
      CTestTerrainVirtualAtlas atlas(
          fixture.catalog, fixture.images, 0x4a4d5631u, {}, styles);
      for(const STerrainTransitionSignature& base : requests)
        for(int rotation = 0; rotation < 4; ++rotation)
        {
        const STerrainTransitionSignature requested =
            CTerrainTransitionCatalog::rotated(base, rotation);
        QSet<qint32> materials;
        materials << requested.northWest << requested.northEast
                  << requested.southEast << requested.southWest;
        QString generatedProvenance;
        CValidationReport generatedReport;
        const QImage generated = atlas.reviewGeneratedBlend(
            requested, 0, &generatedProvenance, &generatedReport);
        QVERIFY2(!generated.isNull(), qPrintable(generatedReport.toText()));
        QVERIFY2(!generatedReport.hasErrors(),
                 qPrintable(generatedReport.toText()));
        for(qint32 material : materials)
        {
            QString provenance;
            QImage mask = atlas.reviewGeneratedJunctionOwnershipMask(
                requested, material, 0, &provenance);
            QVERIFY(!mask.isNull());
            QCOMPARE(mask.size(), QSize(256, 256));
            QVERIFY2(provenance.contains(QStringLiteral("-approved")),
                     qPrintable(provenance));
            QVERIFY2(generatedProvenance.contains(provenance),
                     qPrintable(generatedProvenance));

            STerrainTransitionSignature collapsed = requested;
            qint32* values[4] = {
                &collapsed.northWest, &collapsed.northEast,
                &collapsed.southEast, &collapsed.southWest};
            for(qint32* value : values)
                if(*value != material) *value = 99;
            QString expectedProvenance;
            const QImage expected = atlas.reviewBlendMask(
                collapsed, material, 0, &expectedProvenance, material);
            QVERIFY(!expected.isNull());
            QCOMPARE(mask, expected);
            QCOMPARE(provenance, expectedProvenance);

            // Cross into the questionnaire's visual grid layout once before
            // checking NW,NE,SE,SW semantic ownership.
            mask = mask.mirrored(false, true);
            const qint32 gridValues[4] = {
                requested.northWest, requested.northEast,
                requested.southEast, requested.southWest};
            for(int corner = 0; corner < 4; ++corner)
                QCOMPARE(mask.pixelColor(gridCorners[corner]).red(),
                         gridValues[corner] == material ? 255 : 0);
        }
        }
      QVERIFY(atlas.reviewGeneratedJunctionOwnershipMask(
          signature(1, 1, 2, 2), 1).isNull());
    }
}

void TerrainVirtualAtlasTests::generatedEdgesUseOnlyTheirTwoCornerMaterials()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainVirtualTile tile=atlas.resolve(signature(1,2,3,4),0,0);
    const QImage image=atlas.generatedTiles().at(tile.generatedIndex).pixels;
    const QColor top=image.pixelColor(image.width() / 2,0);
    // The top edge contains only its yellow/blue endpoint materials, but the
    // transition is broad enough to be visible instead of a 90-degree cut.
    QVERIFY(top.red()>100 && top.green()>100 && top.blue()>60);
    const QColor bottom=image.pixelColor(image.width() / 2,
                                         image.height() - 1);
    // The opposite edge contains only red/green and therefore no blue leak.
    QVERIFY(bottom.red()>100 && bottom.green()>60 && bottom.blue()<40);
}

void TerrainVirtualAtlasTests::generatedEdgesHaveSolidEndpointPlateaus()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const STerrainTransitionSignature requested = signature(1, 2, 3, 4);
    const STerrainVirtualTile tile = atlas.resolve(requested, 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(tile.generatedIndex).pixels;
    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(requested);
    const auto source = [&fixture](qint32 material, int x, int y) {
        return fixture.images.at(material - 1).pixelColor(x, y);
    };
    for(int index = 0; index < generated.width(); ++index)
    {
        const double position = double(index) / double(generated.width() - 1);
        if(position <= 0.35)
        {
            QCOMPARE(generated.pixelColor(index, 0),
                     source(preview.northWest, index, 0));
            QCOMPARE(generated.pixelColor(index, generated.height() - 1),
                     source(preview.southWest, index, generated.height() - 1));
            QCOMPARE(generated.pixelColor(0, index),
                     source(preview.northWest, 0, index));
            QCOMPARE(generated.pixelColor(generated.width() - 1, index),
                     source(preview.northEast, generated.width() - 1, index));
        }
        if(position >= 0.65)
        {
            QCOMPARE(generated.pixelColor(index, 0),
                     source(preview.northEast, index, 0));
            QCOMPARE(generated.pixelColor(index, generated.height() - 1),
                     source(preview.southEast, index, generated.height() - 1));
            QCOMPARE(generated.pixelColor(0, index),
                     source(preview.southWest, 0, index));
            QCOMPARE(generated.pixelColor(generated.width() - 1, index),
                     source(preview.southEast, generated.width() - 1, index));
        }
    }
}

void TerrainVirtualAtlasTests::generatedTransitionRetainsSourceTextureIdentity()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    for(int material = 0; material < 2; ++material)
        for(int y = 0; y < 64; ++y) for(int x = 0; x < 64; ++x)
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
    for(int y = 0; y < 64; ++y) for(int x = 0; x < 64; ++x)
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
    QVERIFY(sourceLike >= 64 * 64 * 3 / 4);
    QVERIFY2(genuinelyMixed >= 32,
             qPrintable(QString("Only %1 genuinely mixed pixels")
                 .arg(genuinelyMixed)));
    QVERIFY(genuinelyMixed <= 64 * 64 / 4);
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

void TerrainVirtualAtlasTests::linearLightReviewChangesOnlyMixedPixelsAndPreservesAlpha()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(0, 0, 0, 72));
    fixture.images[1].fill(QColor(255, 255, 255, 196));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0, {}, {}, false);
    const STerrainTransitionSignature requested = signature(1, 1, 2, 2);
    QString encodedProvenance;
    QString linearProvenance;
    const QImage encoded = atlas.reviewGeneratedBlend(
        requested, 0, &encodedProvenance);
    const QImage linear = atlas.reviewGeneratedBlendLinearLight(
        requested, 0, &linearProvenance);
    QVERIFY(!encoded.isNull());
    QCOMPARE(linear.size(), encoded.size());
    QCOMPARE(linearProvenance, encodedProvenance);
    QCOMPARE(linear, atlas.reviewGeneratedBlendLinearLight(requested, 0));
    int changed = 0;
    for(int y = 0; y < encoded.height(); ++y)
        for(int x = 0; x < encoded.width(); ++x)
        {
            const QColor a = encoded.pixelColor(x, y);
            const QColor b = linear.pixelColor(x, y);
            QCOMPARE(b.alpha(), a.alpha());
            QVERIFY(b.red() >= 0 && b.red() <= 255);
            QVERIFY(b.green() >= 0 && b.green() <= 255);
            QVERIFY(b.blue() >= 0 && b.blue() <= 255);
            if(a == b) continue;
            ++changed;
            QVERIFY(a != fixture.images[0].pixelColor(x, y));
            QVERIFY(a != fixture.images[1].pixelColor(x, y));
            QVERIFY(b.red() >= a.red());
            QVERIFY(b.green() >= a.green());
            QVERIFY(b.blue() >= a.blue());
        }
    QVERIFY(changed > 0);
    QCOMPARE(encoded.pixelColor(0, 0), fixture.images[1].pixelColor(0, 0));
    QCOMPARE(linear.pixelColor(0, 0), encoded.pixelColor(0, 0));
    QCOMPARE(encoded.pixelColor(encoded.width() - 1, encoded.height() - 1),
             fixture.images[0].pixelColor(0, 0));
    QCOMPARE(linear.pixelColor(linear.width() - 1, linear.height() - 1),
             encoded.pixelColor(encoded.width() - 1, encoded.height() - 1));
    const QImage ownership = atlas.reviewBlendMask(requested, 1, 0);
    QVERIFY(!ownership.isNull());
    for(int y = 0; y < ownership.height(); ++y)
        for(int x = 0; x < ownership.width(); ++x)
        {
            const int value = ownership.pixelColor(x, y).red();
            if(value != 0 && value != 255) continue;
            const QColor expected = value == 255
                ? fixture.images[0].pixelColor(x, y)
                : fixture.images[1].pixelColor(x, y);
            QCOMPARE(encoded.pixelColor(x, y), expected);
            QCOMPARE(linear.pixelColor(x, y), expected);
        }
}

void TerrainVirtualAtlasTests::stockPixelNoiseCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const QImage expected = atlas.reviewGeneratedBlend(
        signature(1, 1, 2, 2), 0);
    QImage noisy(64, 64, QImage::Format_RGBA8888);
    for(int y = 0; y < 64; ++y)
        for(int x = 0; x < 64; ++x)
            noisy.setPixelColor(x, y, x < 32
                ? fixture.images[0].pixelColor(x, y)
                : fixture.images[1].pixelColor(x, y));
    // These deliberately model the 1-2 pixel grain reported in the former
    // stock-transfer fallback. Calibration inputs cannot alter the canonical
    // generated mask or its composed result.
    noisy.setPixelColor(8, 31, fixture.images[1].pixelColor(8, 31));
    noisy.setPixelColor(54, 30, fixture.images[0].pixelColor(54, 30));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 1, 2, 2),
        noisy, QStringLiteral("grain-regression")));
    const STerrainVirtualTile tile = atlas.resolve(
        signature(1, 1, 2, 2), 0, 0);
    QVERIFY(tile.generatedIndex >= 0);
    const QImage generated = atlas.generatedTiles().at(
        tile.generatedIndex).pixels;
    QCOMPARE(generated, expected);
}

void TerrainVirtualAtlasTests::stockDetachedBlocksCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const QImage expected = atlas.reviewGeneratedBlend(
        signature(1, 2, 2, 2), 0);
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
    QCOMPARE(generated, expected);
}

void TerrainVirtualAtlasTests::stockFragmentsCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const QImage expected = atlas.reviewBlendMask(
        signature(1, 2, 2, 2), 1);
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
    QCOMPARE(mask, expected);
}

void TerrainVirtualAtlasTests::stockSerrationCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(256, 256, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const QImage expected = atlas.reviewBlendMask(
        signature(1, 1, 2, 2), 1);
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
    QCOMPARE(mask, expected);
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

void TerrainVirtualAtlasTests::softStockLeakCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Grass}, {2, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0, {}, styles);
    const auto mixed = [](const QColor& owner, const QColor& other, double amount) {
        return QColor(qRound(owner.red() * amount + other.red() * (1.0 - amount)),
                      qRound(owner.green() * amount + other.green() * (1.0 - amount)),
                      qRound(owner.blue() * amount + other.blue() * (1.0 - amount)),
                      255);
    };
    QImage leaking(64, 64, QImage::Format_RGBA8888);
    QImage contained(64, 64, QImage::Format_RGBA8888);
    leaking.fill(fixture.images[1].pixelColor(0, 0));
    contained.fill(fixture.images[1].pixelColor(0, 0));
    // Grid-Y mirroring puts owner 1 at preview SW. Both candidates have a
    // valid substantial island there, but the first also carries a 45%-owner
    // strand across the opposite (right) edge. It remains visibly transferred
    // after blending while staying below the old binary 128 leak threshold.
    for(int y = 34; y < 64; ++y)
        for(int x = 0; x < 30 - (63 - y) / 2; ++x)
        {
            leaking.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
            contained.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
        }
    for(int x = 18; x < 64; ++x)
        for(int y = 48; y < 54; ++y)
            leaking.setPixelColor(x, y, mixed(
                fixture.images[0].pixelColor(x, y),
                fixture.images[1].pixelColor(x, y), 0.45));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 2, 2, 2),
        leaking, QStringLiteral("a-soft-foreign-edge-leak")));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 2, 2, 2),
        contained, QStringLiteral("z-contained-grass-island")));
    QString provenance;
    QVERIFY(!atlas.reviewBlendMask(signature(1, 2, 2, 2), 1, 0,
                                   &provenance).isNull());
    QCOMPARE(provenance, QStringLiteral(
        "canonical-generated:grass-strands:outer-1of4-v1"));
}

void TerrainVirtualAtlasTests::longStockReachCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    fixture.images[0].fill(QColor(230, 20, 20, 255));
    fixture.images[1].fill(QColor(20, 220, 20, 255));
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Grass}, {2, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0, {}, styles);
    QImage reaching(64, 64, QImage::Format_RGBA8888);
    QImage contained(64, 64, QImage::Format_RGBA8888);
    reaching.fill(fixture.images[1].pixelColor(0, 0));
    contained.fill(fixture.images[1].pixelColor(0, 0));
    for(int y = 42; y < 64; ++y)
        for(int x = 0; x < 29; ++x)
        {
            reaching.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
            contained.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
        }
    // The malformed winter-grass style reaches more than three quarters of
    // the adjacent bottom edge without touching the formally foreign edge.
    for(int y = 59; y < 64; ++y)
        for(int x = 29; x < 52; ++x)
            reaching.setPixelColor(x, y, fixture.images[0].pixelColor(x, y));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 2, 2, 2),
        reaching, QStringLiteral("a-reaching-winter-grass")));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(1, 2, 2, 2),
        contained, QStringLiteral("z-contained-grass")));
    QString provenance;
    QVERIFY(!atlas.reviewBlendMask(signature(1, 2, 2, 2), 1, 0,
                                   &provenance).isNull());
    QCOMPARE(provenance, QStringLiteral(
        "canonical-generated:grass-strands:outer-1of4-v1"));
}

void TerrainVirtualAtlasTests::legacyTargetStyleRejectionCannotAlterCanonicalFallback()
{
    Fixture fixture;
    for(QImage& image : fixture.images)
        image = image.scaled(64, 64, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Ice}, {2, ETerrainBlendStyle::Ice},
        {3, ETerrainBlendStyle::Snow}, {4, ETerrainBlendStyle::Grass}
    };
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0, {}, styles);
    auto transition = [&](int phase) {
        QImage image(64, 64, QImage::Format_RGBA8888);
        for(int y = 0; y < 64; ++y)
            for(int x = 0; x < 64; ++x)
            {
                const int edge = 25 + int(6.0 * std::sin((y + phase) * 0.27));
                image.setPixelColor(x, y, x < edge
                    ? fixture.images[2].pixelColor(x, y)
                    : fixture.images[3].pixelColor(x, y));
            }
        return image;
    };
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(3, 3, 4, 4),
        transition(0), QStringLiteral("Zone6_2:296")));
    QVERIFY(atlas.addStockBlendMaskTemplate(signature(3, 3, 4, 4),
        transition(7), QStringLiteral("zz-reviewed-ice-transfer")));
    QString provenance;
    QVERIFY(!atlas.reviewBlendMask(signature(1, 1, 2, 2), 1, 0,
                                   &provenance).isNull());
    QCOMPARE(provenance, QStringLiteral(
        "canonical-generated:ice-fracture:edge-2of4-v1"));
}

void TerrainVirtualAtlasTests::legacyPavingRejectionCannotAlterCanonicalFallback()
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
    QCOMPARE(provenance, QStringLiteral(
        "canonical-generated:paving-courses:edge-2of4-v1"));
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

void TerrainVirtualAtlasTests::paintedMaterialOwnsGeneratedMaskFamily()
{
    Fixture fixture;
    QMap<qint32, ETerrainBlendStyle> styles;
    styles.insert(1, ETerrainBlendStyle::Soil);
    styles.insert(2, ETerrainBlendStyle::Grass);
    CTestTerrainVirtualAtlas atlas(
        fixture.catalog, fixture.images, 0x6d61736bu, {}, styles);
    const STerrainTransitionSignature requested = signature(1, 1, 1, 2);

    QString reviewProvenance;
    const QImage reviewed = atlas.reviewBlendMask(
        requested, 1, 0, &reviewProvenance, 1);
    QVERIFY(!reviewed.isNull());
    QCOMPARE(reviewProvenance,
             QStringLiteral(
                 "canonical-generated:soil-mottle:inside-3of4-v1:"
                 "r75-approved-authored-restoration:bz4g:191"));

    const STerrainVirtualTile resolved = atlas.resolve(
        requested, 7, 11, nullptr, 1);
    QVERIFY(!resolved.authored);
    QVERIFY(resolved.generatedIndex >= 0);
    QVERIFY(atlas.generatedTiles().at(resolved.generatedIndex).blendProvenance
                .startsWith(QStringLiteral(
                    "canonical-generated:soil-mottle:inside-3of4-v")));
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
        QVERIFY(!tile.stockDerivedBlend);
        QVERIFY(tile.canonicalGeneratedBlend);
        QVERIFY(tile.blendProvenance.startsWith("canonical-generated:"));
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
    for(QImage& image : fixture.images)
        image = image.scaled(256, 256, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation);
    const QVector<ETerrainBlendStyle> styleSets[] = {
        {ETerrainBlendStyle::Grass, ETerrainBlendStyle::Paving,
         ETerrainBlendStyle::Snow, ETerrainBlendStyle::Water},
        {ETerrainBlendStyle::Soil, ETerrainBlendStyle::Stone,
         ETerrainBlendStyle::Sand, ETerrainBlendStyle::Gravel}
    };
    // This 3x3 vertex pattern gives the four cells a four-family junction,
    // a three-family adjacent repeat, and two three-family diagonal repeats.
    // Rotating the style-to-material assignment exercises every approved mask
    // family on every connection without rotating pixels independently of the
    // semantic corner assignment.
    const qint32 vertices[3][3] = {
        {1, 2, 2}, {4, 3, 4}, {4, 1, 3}
    };
    for(const QVector<ETerrainBlendStyle>& styleSet : styleSets)
      for(int styleRotation = 0; styleRotation < 4; ++styleRotation)
      {
        QMap<qint32, ETerrainBlendStyle> styles;
        for(int material = 1; material <= 4; ++material)
            styles.insert(material,
                styleSet.at((material - 1 + styleRotation) % 4));
        CTerrainVirtualMaterialField field;
        QVERIFY(field.resize(2, 2, 1));
        for(int y = 0; y < 3; ++y)
            for(int x = 0; x < 3; ++x)
                QVERIFY(field.setMaterial(x, y, vertices[y][x]));
        CTestTerrainVirtualAtlas atlas(
            fixture.catalog, fixture.images, 0x45494844u, {}, styles);
        CValidationReport report;
        const auto finalized = atlas.finalize(
            field, "EiHdAllConnections", 512, {1, 2, 3, 4}, &report);
        QVERIFY2(finalized.success,
                 qPrintable(finalized.error + "\n" + report.toText()));
        for(const STerrainFinalizedTile& tile : finalized.tiles)
          if(tile.canonicalGeneratedBlend)
          {
            const QStringList components = tile.blendProvenance.split(
                QStringLiteral("; "), QString::SkipEmptyParts);
            QVERIFY2(!components.isEmpty(),
                     qPrintable(tile.blendProvenance));
            for(const QString& component : components)
                QVERIFY2(component.contains(QStringLiteral("-approved")),
                         qPrintable(tile.blendProvenance));
          }
        const QImage preview = renderFinalizedPreview(finalized, 2, 2);
        QVERIFY(!preview.isNull());
        const int tileSize = preview.width() / 2;
        QCOMPARE(preview.height(), tileSize * 2);
        for(int y = 0; y < preview.height(); ++y)
            QCOMPARE(preview.pixelColor(tileSize - 1, y),
                     preview.pixelColor(tileSize, y));
        for(int x = 0; x < preview.width(); ++x)
            QCOMPARE(preview.pixelColor(x, tileSize - 1),
                     preview.pixelColor(x, tileSize));
      }
}

void TerrainVirtualAtlasTests::rotationsDeduplicateButSemanticAliasesRemainDistinct()
{
    // A local authored tile is indexed under all four paint-space signatures;
    // a rotated request must reuse its physical slot rather than synthesize.
    CTerrainTransitionCatalog localAuthoredCatalog;
    const STerrainTransitionSignature authoredBase = signature(1, 2, 3, 4);
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        STerrainTransitionVariant variant;
        variant.signature = CTerrainTransitionCatalog::rotated(
            authoredBase, -rotation);
        SMprPackedTile packed;
        packed.tileIndex = 7;
        packed.rotation = quint8(rotation);
        variant.packedTile = packed.encode();
        QVERIFY(localAuthoredCatalog.addVariant(variant));
    }
    bool localFound = false;
    const SMprPackedTile localRotated = SMprPackedTile::decode(
        localAuthoredCatalog.selectForPaint(
            CTerrainTransitionCatalog::rotated(authoredBase, -1),
            0, 0, 0, &localFound));
    QVERIFY(localFound);
    QCOMPARE(localRotated.atlasSlot(), 7);
    QCOMPARE(localRotated.rotation, quint8(1));

    Fixture fixture;
    CTestTerrainVirtualAtlas atlas(fixture.catalog,fixture.images);
    seedReviewedStockMasks(atlas, fixture);
    const STerrainTransitionSignature base=signature(1,2,3,4);
    const STerrainVirtualTile first=atlas.resolve(base,0,0,nullptr,1);
    const STerrainVirtualTile rotated=atlas.resolve(
        CTerrainTransitionCatalog::rotated(base,1),0,0,nullptr,1);
    QCOMPARE(first.generatedIndex,rotated.generatedIndex);
    QVERIFY(first.generatedRotation!=rotated.generatedRotation);

    // Imported reviewed artwork follows the same rule: one working physical
    // tile is shared by four catalog rotations and is never regenerated.
    CTestTerrainVirtualAtlas importedAtlas(fixture.catalog, fixture.images);
    CValidationReport importedReport;
    QVERIFY(importedAtlas.addAuthoredLibraryTransition(
        authoredBase, solid(qRgba(80, 100, 120, 255)),
        QStringLiteral("rotated-authored-fixture"), &importedReport));
    const STerrainVirtualTile importedBase = importedAtlas.resolve(
        authoredBase, 0, 0, &importedReport, 1);
    const STerrainVirtualTile importedRotated = importedAtlas.resolve(
        CTerrainTransitionCatalog::rotated(authoredBase, -1),
        0, 0, &importedReport, 1);
    QVERIFY2(!importedReport.hasErrors(), qPrintable(importedReport.toText()));
    QVERIFY(importedBase.importedAuthored);
    QVERIFY(importedRotated.importedAuthored);
    QCOMPARE(importedBase.generatedIndex, importedRotated.generatedIndex);
    QCOMPARE(importedBase.generatedRotation, quint8(0));
    QCOMPARE(importedRotated.generatedRotation, quint8(1));

    // A second physical authored tile with the same exact corners is another
    // legitimate visual variant, not a duplicate to collapse. Fixed inputs
    // remain deterministic while a sufficiently wide grid reaches both.
    QVERIFY(importedAtlas.addAuthoredLibraryTransition(
        authoredBase, solid(qRgba(130, 75, 45, 255)),
        QStringLiteral("second-authored-fixture"), &importedReport));
    QSet<int> importedPhysicalVariants;
    for(int x = 0; x < 64; ++x)
    {
        const STerrainVirtualTile selected = importedAtlas.resolve(
            authoredBase, x, 7, &importedReport, 1);
        QVERIFY(selected.importedAuthored);
        importedPhysicalVariants.insert(selected.generatedIndex);
        QCOMPARE(selected.generatedIndex, importedAtlas.resolve(
            authoredBase, x, 7, &importedReport, 1).generatedIndex);
    }
    QCOMPARE(importedPhysicalVariants.size(), 2);

    CTestTerrainWorkingAtlas importedWorking(
        fixture.catalog, fixture.images, 0x31415926u, 16);
    QVERIFY(importedWorking.addAuthoredLibraryTransition(
        authoredBase, solid(qRgba(80, 100, 120, 255)),
        QStringLiteral("working-authored-a"), &importedReport));
    QVERIFY(importedWorking.addAuthoredLibraryTransition(
        authoredBase, solid(qRgba(130, 75, 45, 255)),
        QStringLiteral("working-authored-b"), &importedReport));
    QMap<int, quint64> importedUsage;
    for(int slot = 0; slot < importedWorking.capacityLimit(); ++slot)
        importedUsage.insert(slot, 0);
    QVERIFY(importedWorking.realizeAuthoredLibraryVariants(
        authoredBase, &importedUsage, {}, &importedReport, 1));
    QSet<int> realizedPhysicalVariants;
    for(int x = 0; x < 64; ++x)
    {
        bool found = false;
        const quint16 packed = importedWorking.catalog().selectForPaint(
            authoredBase, x, 9, 0x31415926u, &found);
        QVERIFY(found);
        realizedPhysicalVariants.insert(
            SMprPackedTile::decode(packed).atlasSlot());
    }
    QCOMPARE(realizedPhysicalVariants.size(), 2);

    Fixture duplicate(true);
    const QVector<qint32> duplicateTypes = {0, 1, 2, 2};
    const QMap<qint32, qint32> duplicateMaterialTypes = {
        {1, 0}, {2, 1}, {3, 2}, {4, 2}};
    CTestTerrainVirtualAtlas duplicateAtlas(
        duplicate.catalog, duplicate.images, 0, {}, {}, true,
        duplicateTypes, duplicateMaterialTypes);
    CValidationReport duplicateReport;
    const QImage sharedPixels = duplicate.images.at(0);
    QVERIFY(duplicateAtlas.addAuthoredLibraryTransition(
        signature(1,3,3,3), sharedPixels, QStringLiteral("semantic-a"),
        &duplicateReport));
    QVERIFY(duplicateAtlas.addAuthoredLibraryTransition(
        signature(1,4,4,4), sharedPixels, QStringLiteral("semantic-b"),
        &duplicateReport));
    const int a=duplicateAtlas.resolve(
        signature(1,3,3,3),0,0,&duplicateReport,3).generatedIndex;
    const int b=duplicateAtlas.resolve(
        signature(1,4,4,4),0,0,&duplicateReport,4).generatedIndex;
    QVERIFY2(!duplicateReport.hasErrors(), qPrintable(duplicateReport.toText()));
    QVERIFY(a >= 0);
    QVERIFY(b >= 0);
    // Identical artwork cannot share a physical slot when its semantic corner
    // identity differs: the portable profile has one signature per slot, so
    // sharing here silently substitutes one brush after reopen.
    QVERIFY(a != b);
    QCOMPARE(duplicateAtlas.generatedTiles().at(a).pixels,
             duplicateAtlas.generatedTiles().at(b).pixels);
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
    bool routeFound = false;
    const STerrainTransitionVariant routed =
        working.catalog().variantForPacked(packed, &routeFound);
    QVERIFY(routeFound);
    QCOMPARE(routed.routeOwnerMaterial, qint32(1));
    QVERIFY(routed.canonicalGeneratedRoute);

    CTestTerrainVirtualAtlas finalizer(fixture.catalog, fixture.images);
    seedReviewedPairMasks(finalizer, 1, 2, fixture.images.at(0),
                          fixture.images.at(1));
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    QVERIFY(field.setMaterial(1, 0, 2));
    QVERIFY(field.setMaterial(1, 1, 2));
    const STerrainAtlasFinalization finalized = finalizer.finalize(
        field, QStringLiteral("RouteOwnerFinalized"), 512,
        sourceTypes, &report, {}, {}, {}, {2});
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n")
                        + report.toText()));
    QCOMPARE(finalized.packedCells.size(), 1);
    const int finalizedSlot = SMprPackedTile::decode(
        finalized.packedCells.front()).atlasSlot();
    QCOMPARE(finalized.tiles.at(finalizedSlot).routeOwnerMaterial,
             qint32(2));
}

void TerrainVirtualAtlasTests::stagedResolveUsesPredecessorTypeOverAuthoredAndSemanticMajority()
{
    // This reproduces the first Zone2 Shift+R failure without a live map or
    // renderer: the exact authored non-uniform slot is type 0, while the
    // physical predecessor is type 2.  Semantic material 2 owns three
    // corners and is type 1, so an implementation must not simply fall back
    // to its unique semantic-majority type either.
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    images << solid(qRgba(28, 28, 28, 255))
           << solid(qRgba(214, 72, 54, 255))
           << solid(qRgba(53, 156, 75, 255))
           << solid(qRgba(116, 102, 175, 255));
    const auto makeVariant = [](
        const STerrainTransitionSignature& value, int slot) {
        STerrainTransitionVariant variant;
        variant.signature = value;
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        return variant;
    };
    const STerrainTransitionVariant predecessorVariant =
        makeVariant(signature(1, 1, 1, 1), 1);
    QVERIFY(catalog.addVariant(predecessorVariant));
    const quint16 predecessorPacked = predecessorVariant.packedTile;
    QVERIFY(catalog.addVariant(makeVariant(signature(2, 2, 2, 2), 2)));
    const STerrainTransitionSignature requested = signature(1, 2, 2, 2);
    const STerrainTransitionVariant authoredVariant = makeVariant(requested, 3);
    QVERIFY(catalog.addVariant(authoredVariant));
    const quint16 authoredPacked = authoredVariant.packedTile;
    const QVector<qint32> sourceTypes = {0, 2, 1, 0};
    const QMap<qint32, qint32> materialTypes = {{1, 2}, {2, 1}};
    CTestTerrainWorkingAtlas live(catalog, images, 0x5a6f6e65u, 8,
                                  {}, {}, sourceTypes, materialTypes);
    seedReviewedPairMasks(live, 1, 2, images.at(1), images.at(2));
    QVERIFY2(live.isValid(), qPrintable(live.error()));
    QCOMPARE(live.runtimeTerrainTypeForSlot(
                 SMprPackedTile::decode(authoredPacked).atlasSlot()),
             qint32(0));

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    QVector<qint32> modelTypes(64, 0);
    modelTypes[1] = 2;
    modelTypes[2] = 1;
    modelTypes[3] = 0;
    CTerrainEditModel model;
    CValidationReport modelReport;
    QVERIFY2(model.loadSources(1, 1, 100.0f, {source}, &modelReport, 1,
                               modelTypes, 0),
             qPrintable(modelReport.toText()));
    STerrainTileCommand predecessor;
    STerrainTileDelta predecessorDelta;
    predecessorDelta.globalX = 7;
    predecessorDelta.globalY = 8;
    predecessorDelta.layer = ETerrainTileLayer::Land;
    predecessorDelta.before = 0;
    predecessorDelta.after = predecessorPacked;
    predecessor.deltas.append(predecessorDelta);
    QVERIFY2(model.apply(predecessor, true, &modelReport),
             qPrintable(modelReport.toText()));
    bool predecessorTypeFound = false;
    const qint32 predecessorType = model.tileType(
        model.packedTile(7, 8, ETerrainTileLayer::Land),
        &predecessorTypeFound);
    QVERIFY(predecessorTypeFound);
    QCOMPARE(predecessorType, qint32(2));

    bool authoredFound = false;
    const quint16 untypedAuthored = live.catalog().selectForPaint(
        requested, 7, 8, 0x5a6f6e65u, &authoredFound);
    QVERIFY(authoredFound);
    QCOMPARE(untypedAuthored, authoredPacked);
    QCOMPARE(live.runtimeTerrainTypeForSlot(
                 SMprPackedTile::decode(untypedAuthored).atlasSlot()),
             qint32(0));

    QSharedPointer<CTerrainWorkingAtlas> staged = live.createStagingCopy();
    QVERIFY(staged);
    QMap<int, quint64> usage;
    CValidationReport resolveReport;
    bool generated = false;
    const quint16 resolved = staged->resolve(
        requested, 7, 8, &usage, {}, &generated, &resolveReport,
        0, predecessorType, true);
    QVERIFY2(!resolveReport.hasErrors(), qPrintable(resolveReport.toText()));
    const int resolvedSlot = SMprPackedTile::decode(resolved).atlasSlot();
    QCOMPARE(staged->runtimeTerrainTypeForSlot(resolvedSlot), predecessorType);
    QVERIFY(generated);
    QVERIFY(resolvedSlot >= images.size());
    QCOMPARE(live.generatedTileCount(), 0);
    QCOMPARE(live.runtimeTerrainTypeForSlot(
                 SMprPackedTile::decode(authoredPacked).atlasSlot()),
             qint32(0));
}

void TerrainVirtualAtlasTests::stagingCopyOwnsCatalogAfterSourceLifetimeEnds()
{
    Fixture original;
    CReusedWorkingAtlasStorage storage;
    CTestTerrainWorkingAtlas* live = storage.construct(
        original.catalog, original.images, 0x51a9u, 8);
    QSharedPointer<CTerrainWorkingAtlas> staged = live->createStagingCopy();
    QVERIFY(staged);

    // Reuse the exact source-object storage with a conflicting catalog. A true
    // staging copy must retain original material 2 -> slot 1 rather than
    // following this replacement object's material 2 -> slot 3 mapping.
    storage.reset();
    CTerrainTransitionCatalog replacementCatalog;
    STerrainTransitionVariant replacement;
    replacement.signature = signature(2, 2, 2, 2);
    SMprPackedTile replacementPacked;
    replacementPacked.tileIndex = 3;
    replacement.packedTile = replacementPacked.encode();
    QVERIFY(replacementCatalog.addVariant(replacement));
    storage.construct(replacementCatalog, original.images, 0x8e17u, 8);

    QMap<int, quint64> usage;
    bool generated = true;
    CValidationReport report;
    const quint16 resolved = staged->resolve(
        signature(2, 2, 2, 2), 4, 5, &usage, {}, &generated, &report,
        2, reviewedFixtureTileTypes(original.images.size()).at(1), true);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(SMprPackedTile::decode(resolved).atlasSlot(), 1);
    QVERIFY(!generated);
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

void TerrainVirtualAtlasTests::exactAuthoredDuplicatesAreRemappedBeforeCapacity()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images = {
        solid(qRgba(40, 90, 150, 255)),
        solid(qRgba(40, 90, 150, 255)),
        solid(qRgba(180, 110, 35, 255)),
    };
    for(int slot = 0; slot < images.size(); ++slot)
    {
        STerrainTransitionVariant variant;
        const qint32 material = slot < 2 ? 1 : 2;
        variant.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    SMprPackedTile duplicate;
    duplicate.tileIndex = 1;
    SMprPackedTile unique;
    unique.tileIndex = 2;
    SMprPackedTile primary;
    primary.tileIndex = 0;
    CTestTerrainVirtualAtlas atlas(
        catalog, images, 0, {}, {}, true, {4, 4, 7}, {{1, 4}, {2, 7}});
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("ExactDuplicateCapacity"), 2, {4, 4, 7},
        &report, {duplicate.encode(), unique.encode()}, {}, {primary.encode()});
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    QCOMPARE(finalized.requiredSlots, 2);
    QCOMPARE(finalized.authoredSlotRemap.value(0),
             finalized.authoredSlotRemap.value(1));
    QVERIFY(finalized.authoredSlotRemap.value(0)
            != finalized.authoredSlotRemap.value(2));
    const STerrainFinalizedTile& retained = finalized.tiles.at(
        finalized.authoredSlotRemap.value(0));
    QCOMPARE(retained.sourceNamespace,
             QStringLiteral("ExactDuplicateCapacity"));
    QCOMPARE(retained.sourceSlotAliases, QVector<int>({0, 1}));
}

void TerrainVirtualAtlasTests::exactAuthoredDuplicatesUseCrossLayerDecodeCatalog()
{
    CTerrainTransitionCatalog landCatalog;
    CTerrainTransitionCatalog decodeCatalog;
    const QImage shared = solid(qRgba(40, 90, 150, 255));
    QVector<QImage> images = {
        shared,
        shared,
        solid(qRgba(180, 110, 35, 255)),
    };
    for(int slot = 0; slot < images.size(); ++slot)
    {
        STerrainTransitionVariant variant;
        const qint32 material = slot < 2 ? 2 : 1;
        variant.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(decodeCatalog.addVariant(variant));
        if(slot == 2)
            QVERIFY(landCatalog.addVariant(variant));
    }

    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    SMprPackedTile firstOverlay;
    firstOverlay.tileIndex = 0;
    SMprPackedTile secondOverlay;
    secondOverlay.tileIndex = 1;
    CTestTerrainVirtualAtlas atlas(
        landCatalog, images, 0, {}, {}, true,
        {14, 14, 1}, {{1, 1}});
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("CrossLayerDuplicate"), 2,
        {14, 14, 1}, &report,
        {firstOverlay.encode(), secondOverlay.encode()}, {}, {}, {},
        &decodeCatalog);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n")
                        + report.toText()));
    QCOMPARE(finalized.requiredSlots, 2);
    QCOMPARE(finalized.authoredSlotRemap.value(0),
             finalized.authoredSlotRemap.value(1));
    const STerrainFinalizedTile& retained = finalized.tiles.at(
        finalized.authoredSlotRemap.value(0));
    QCOMPARE(retained.sourceSlotAliases, QVector<int>({0, 1}));
    QCOMPARE(retained.runtimeTerrainType, qint32(14));
    QCOMPARE(retained.canonicalSignature, signature(2, 2, 2, 2));
}

void TerrainVirtualAtlasTests::exactAuthoredDuplicatesWithDifferentRuntimeTypesRemainDistinct()
{
    CTerrainTransitionCatalog catalog;
    const QImage shared = solid(qRgba(135, 75, 55, 255));
    QVector<QImage> images = {shared, shared};
    for(int slot = 0; slot < images.size(); ++slot)
    {
        STerrainTransitionVariant variant;
        variant.signature = signature(1, 1, 1, 1);
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    SMprPackedTile stoneAlias;
    stoneAlias.tileIndex = 0;
    SMprPackedTile groundAlias;
    groundAlias.tileIndex = 1;
    CTestTerrainVirtualAtlas atlas(
        catalog, images, 0, {}, {}, true, {2, 1}, {{1, 2}});
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("RuntimeTypedDuplicate"), 2, {2, 1},
        &report, {stoneAlias.encode(), groundAlias.encode()});
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n")
                        + report.toText()));
    QCOMPARE(finalized.requiredSlots, 2);
    QVERIFY(finalized.authoredSlotRemap.value(0)
            != finalized.authoredSlotRemap.value(1));
    QCOMPARE(finalized.tiles.at(finalized.authoredSlotRemap.value(0))
                 .runtimeTerrainType,
             qint32(2));
    QCOMPARE(finalized.tiles.at(finalized.authoredSlotRemap.value(1))
                 .runtimeTerrainType,
             qint32(1));
    QCOMPARE(finalized.tiles.at(finalized.authoredSlotRemap.value(0))
                 .sourceSlotAliases,
             QVector<int>({0}));
    QCOMPARE(finalized.tiles.at(finalized.authoredSlotRemap.value(1))
                 .sourceSlotAliases,
             QVector<int>({1}));
}

void TerrainVirtualAtlasTests::exactAuthoredDuplicateConflictFailsClosed()
{
    CTerrainTransitionCatalog catalog;
    const QImage shared = solid(qRgba(75, 115, 155, 255));
    QVector<QImage> images = {shared, shared};
    for(int slot = 0; slot < images.size(); ++slot)
    {
        STerrainTransitionVariant variant;
        const qint32 material = slot + 1;
        variant.signature = signature(material, material, material, material);
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 1));
    SMprPackedTile conflict;
    conflict.tileIndex = 1;
    CTestTerrainVirtualAtlas atlas(
        catalog, images, 0, {}, {}, true, {4, 4}, {{1, 4}, {2, 4}});
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("ExactDuplicateConflict"), 2, {4, 4},
        &report, {conflict.encode()});
    QVERIFY(!finalized.success);
    QVERIFY(report.hasErrors());
    QVERIFY(finalized.error.contains(QStringLiteral("conflict"),
                                     Qt::CaseInsensitive));
}

void TerrainVirtualAtlasTests::exactAuthoredDuplicateRemapPreservesPackedRotationsAfterReopen()
{
    CTerrainTransitionCatalog catalog;
    const STerrainTransitionSignature uniform = signature(1, 1, 1, 1);
    const QImage shared = solid(qRgba(55, 125, 80, 255));
    QVector<QImage> images = {shared, shared};
    for(int slot = 0; slot < images.size(); ++slot)
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            STerrainTransitionVariant variant;
            variant.signature = uniform;
            SMprPackedTile packed;
            packed.tileIndex = quint8(slot);
            packed.rotation = quint8(rotation);
            variant.packedTile = packed.encode();
            QVERIFY(catalog.addVariant(variant));
        }
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    QVector<quint16> preferred;
    preferred.reserve(16 * 16);
    for(int y = 0; y < 16; ++y)
        for(int x = 0; x < 16; ++x)
        {
            SMprPackedTile packed;
            packed.tileIndex = quint8((x + y) & 1);
            packed.rotation = quint8((x + y * 3) & 3);
            preferred.append(packed.encode());
        }
    CTestTerrainVirtualAtlas atlas(
        catalog, images, 0, {}, {}, true, {6, 6}, {{1, 6}});
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("ExactDuplicateRoundTrip"), 64, {6, 6},
        &report, {}, {}, preferred);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    QCOMPARE(finalized.requiredSlots, 1);
    QCOMPARE(finalized.authoredSlotRemap.value(0),
             finalized.authoredSlotRemap.value(1));
    QCOMPARE(finalized.tiles.at(0).sourceSlotAliases,
             QVector<int>({0, 1}));
    for(int cell = 0; cell < preferred.size(); ++cell)
    {
        const SMprPackedTile before = SMprPackedTile::decode(preferred.at(cell));
        const SMprPackedTile after = SMprPackedTile::decode(
            finalized.packedCells.at(cell));
        QCOMPARE(after.atlasSlot(), 0);
        QCOMPARE(after.rotation, before.rotation);
    }

    CMprDocument source;
    QVERIFY2(source.loadBytes(portableMpr(), QStringLiteral("duplicate-source.mpr"),
                              &report), qPrintable(report.toText()));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            source, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n") + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
                                QStringLiteral("duplicate-reopened.mpr"), &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(reopened, &report), qPrintable(report.toText()));
    for(int y = 0; y < 16; ++y)
        for(int x = 0; x < 16; ++x)
        {
            bool available = false;
            QCOMPARE(terrain.packedTile(x, y, ETerrainTileLayer::Land,
                                        &available),
                     finalized.packedCells.at(y * 16 + x));
            QVERIFY(available);
        }

    // The inactive layer is also remapped through the duplicate alias map.
    // Its packed rotation is composed independently of the active grid.
    SMprPackedTile pinnedLand;
    pinnedLand.tileIndex = 1;
    pinnedLand.rotation = 3;
    CMprDocument layeredSource;
    QVERIFY2(layeredSource.loadBytes(
        portableMpr(true, false, pinnedLand.encode()),
        QStringLiteral("duplicate-layered-source.mpr"), &report),
        qPrintable(report.toText()));
    const STerrainPortableAtlasPackage layeredPackage =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            layeredSource, finalized, ETerrainTileLayer::Overlay, &report);
    QVERIFY2(layeredPackage.success,
             qPrintable(layeredPackage.error + QStringLiteral("\n")
                        + report.toText()));
    CMprDocument layeredReopened;
    QVERIFY2(layeredReopened.loadBytes(
        layeredPackage.mprBytes, QStringLiteral("duplicate-layered-result.mpr"),
        &report), qPrintable(report.toText()));
    CTerrainEditModel layeredTerrain;
    QVERIFY2(layeredTerrain.load(layeredReopened, &report),
             qPrintable(report.toText()));
    bool landAvailable = false;
    const SMprPackedTile remappedLand = SMprPackedTile::decode(
        layeredTerrain.packedTile(0, 0, ETerrainTileLayer::Land,
                                  &landAvailable));
    QVERIFY(landAvailable);
    QCOMPARE(remappedLand.atlasSlot(), 0);
    QCOMPARE(remappedLand.rotation, quint8(3));
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
    QVector<SMprMaterialRecord> liveMaterials =
        paintedSource.mapInfo().materials;
    if(liveMaterials.isEmpty())
        liveMaterials.append(SMprMaterialRecord());
    liveMaterials.front().red = 0.875f;
    QVector<SMprAnimatedTileRecord> liveAnimations;
    SMprAnimatedTileRecord generatedFlipbook;
    generatedFlipbook.tileIndex = 4 * 64;
    generatedFlipbook.frameCount = 2;
    liveAnimations.append(generatedFlipbook);
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedSource, tiles, tileTypes, authoredPages,
            liveMaterials, liveAnimations, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes, "working-result.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().textureCount, quint32(5));
    QCOMPARE(reopened.mapInfo().tileCount, quint32(320));
    QCOMPARE(reopened.mapInfo().tileTypes.at(4 * 64), qint32(7));
    QCOMPARE(reopened.mapInfo().materials, liveMaterials);
    QCOMPARE(reopened.mapInfo().animatedTiles, liveAnimations);
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

void TerrainVirtualAtlasTests::workingPreviewPackagePublishesReusedAuthoredTailPixels()
{
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(portableMpr(), "partial-tail-base.mpr",
                                &report), qPrintable(report.toText()));
    const SMprMapInfo map = baseline.mapInfo();
    QVERIFY(map.textureCount > 0);
    QVERIFY(map.tileCount >= 4);

    const QByteArray originalMp = baseline.memberPayload(map.memberName);
    const int tileTypeOffset = 38 + int(map.materialCount) * 44;
    const int oldTailOffset = tileTypeOffset + int(map.tileCount) * 4;
    constexpr int authoredTextureCount = 9;
    constexpr int partialTileCount = 520;
    constexpr int reusedSlot = 534; // page 8, local 22: R173 regression.
    QByteArray partialMp = originalMp.left(tileTypeOffset);
    for(int slot = 0; slot < partialTileCount; ++slot)
    {
        char bytes[4];
        qToLittleEndian<qint32>(2, reinterpret_cast<uchar*>(bytes));
        partialMp.append(bytes, 4);
    }
    partialMp.append(originalMp.mid(oldTailOffset));
    qToLittleEndian<quint32>(quint32(authoredTextureCount),
        reinterpret_cast<uchar*>(partialMp.data() + 16));
    qToLittleEndian<quint32>(quint32(partialTileCount),
        reinterpret_cast<uchar*>(partialMp.data() + 24));
    CMprEditTransaction transaction(QStringLiteral(
        "Create partial final authored page"));
    QByteArray originalSha;
    for(const SMprArchiveMember& member : baseline.members())
        if(member.name.compare(map.memberName, Qt::CaseInsensitive) == 0)
            originalSha = member.sha256;
    transaction.replaceMemberPayload(map.memberName, originalSha, partialMp);
    QByteArray partialBytes;
    QVERIFY2(baseline.applyTransaction(transaction, &partialBytes, &report),
             qPrintable(report.toText()));
    CMprDocument partial;
    QVERIFY2(partial.loadBytes(partialBytes, "partial-tail.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(partial.mapInfo().tileCount, quint32(partialTileCount));

    QVector<QByteArray> authoredPages;
    QVector<QImage> activeTiles;
    for(int page = 0; page < authoredTextureCount; ++page)
    {
        const QImage sourcePage = CTestTerrainVirtualAtlas::composePage(
            QVector<QImage>(64, solid(qRgba(20 + page, 40, 60, 255))),
            0, 8);
        QString diagnostic;
        const QByteArray payload = ei::encodeMmpDxt1(
            sourcePage, true, &diagnostic);
        QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
        authoredPages.append(payload);
        for(int localSlot = 0; localSlot < 64; ++localSlot)
        {
            QImage tile;
            QVERIFY2(ei::decodeMmpDxt1AtlasTile(
                         payload, localSlot, &tile, &diagnostic),
                     qPrintable(diagnostic));
            activeTiles.append(tile.convertToFormat(QImage::Format_RGBA8888));
        }
    }
    activeTiles[reusedSlot].fill(QColor(220, 30, 170, 255));
    QVector<qint32> types(reusedSlot + 1, 2);
    QString expectedDiagnostic;
    const QByteArray expectedStagedPage = ei::encodeMmpDxt1(
        CTestTerrainVirtualAtlas::composePage(
            activeTiles, (reusedSlot / 64) * 64, 8),
        true, &expectedDiagnostic);
    QVERIFY2(!expectedStagedPage.isEmpty(), qPrintable(expectedDiagnostic));
    QImage expectedPhysicalSlot;
    QVERIFY2(ei::decodeMmpDxt1AtlasTile(
                 expectedStagedPage, reusedSlot % 64, &expectedPhysicalSlot,
                 &expectedDiagnostic), qPrintable(expectedDiagnostic));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            partial, activeTiles, types, authoredPages, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n")
                        + report.toText()));
    QCOMPARE(package.resourceMembers.size(), authoredTextureCount);
    CMprDocument reopenedMpr;
    QVERIFY2(reopenedMpr.loadBytes(package.mprBytes,
                                   "slot534-reopened.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopenedMpr.mapInfo().textureCount,
             quint32(authoredTextureCount));
    QCOMPARE(reopenedMpr.mapInfo().tileCount, quint32(reusedSlot + 1));

    CResArchiveDocument resources;
    QStringList errors;
    QVERIFY2(resources.loadBytes(package.resourceArchiveBytes,
                                 "partial-tail.res", &errors),
             qPrintable(errors.join('\n')));
    const int partialPage = reusedSlot / 64;
    QVERIFY(resources.memberPayload(package.resourceMembers.at(partialPage))
            != authoredPages.at(partialPage));
    QImage reopened;
    QString diagnostic;
    QVERIFY2(ei::decodeMmpDxt1AtlasTile(
                 resources.memberPayload(package.resourceMembers.at(partialPage)),
                 reusedSlot % 64, &reopened, &diagnostic),
             qPrintable(diagnostic));
    const QColor center = reopened.pixelColor(
        reopened.width() / 2, reopened.height() / 2);
    QVERIFY(center.red() > 150);
    QVERIFY(center.blue() > 100);
    QVERIFY(center.alpha() > 0);
    QCOMPARE(reopened.convertToFormat(QImage::Format_RGBA8888),
             expectedPhysicalSlot.convertToFormat(QImage::Format_RGBA8888));

    // The smallest mip is one shared DXT block for the complete page. It must
    // come from the rebuilt page as well; otherwise the generated tail slot is
    // correct close up but fades back to authored transparent padding at
    // distance.
    const QByteArray reopenedPartialPage = resources.memberPayload(
        package.resourceMembers.at(partialPage));
    QCOMPARE(reopenedPartialPage.right(8), expectedStagedPage.right(8));
    QVERIFY(reopenedPartialPage.right(8)
            != authoredPages.at(partialPage).right(8));

    // Slots 512..519 are authored neighbours on the same partial page. Their
    // compressed blocks and therefore decoded RGBA must remain exact.
    for(int slot = partialPage * 64; slot < partialTileCount; ++slot)
    {
        QImage before;
        QImage after;
        QVERIFY(ei::decodeMmpDxt1AtlasTile(
            authoredPages.at(partialPage), slot % 64, &before, &diagnostic));
        QVERIFY(ei::decodeMmpDxt1AtlasTile(
            resources.memberPayload(package.resourceMembers.at(partialPage)),
            slot % 64, &after, &diagnostic));
        QCOMPARE(after.convertToFormat(QImage::Format_RGBA8888),
                 before.convertToFormat(QImage::Format_RGBA8888));
    }

    QVector<QByteArray> reopenedPages;
    QVector<QImage> reopenedTiles;
    for(int page = 0; page < authoredTextureCount; ++page)
    {
        const QByteArray pageBytes = resources.memberPayload(
            package.resourceMembers.at(page));
        reopenedPages.append(pageBytes);
        for(int localSlot = 0; localSlot < 64; ++localSlot)
        {
            QImage tile;
            QVERIFY(ei::decodeMmpDxt1AtlasTile(
                pageBytes, localSlot, &tile, &diagnostic));
            reopenedTiles.append(tile.convertToFormat(QImage::Format_RGBA8888));
        }
    }
    const STerrainPortableAtlasPackage resaved =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            reopenedMpr, reopenedTiles, types, reopenedPages, &report);
    QVERIFY2(resaved.success,
             qPrintable(resaved.error + QStringLiteral("\n")
                        + report.toText()));
    CResArchiveDocument resavedArchive;
    QVERIFY(resavedArchive.loadBytes(resaved.resourceArchiveBytes,
                                     "slot534-resaved.res", &errors));
    for(int page = 0; page < authoredTextureCount; ++page)
        QCOMPARE(resavedArchive.memberPayload(resaved.resourceMembers.at(page)),
                 reopenedPages.at(page));
}

void TerrainVirtualAtlasTests::workingPreviewPackageDoesNotSerializePhysicalPagePadding()
{
    CMprDocument source;
    CValidationReport report;
    QVERIFY2(source.loadBytes(portableMpr(), "working-padding-source.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(source, &report), qPrintable(report.toText()));

    constexpr int physicalPages = 10;
    constexpr int logicalSlots = 608;
    terrain.enableWorkingAtlasSlots(physicalPages * 64);
    bool available = false;
    const quint16 before = terrain.packedTile(
        0, 0, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    SMprPackedTile extension;
    extension.atlasIndex = quint8((logicalSlots - 1) / 64);
    extension.tileIndex = quint8((logicalSlots - 1) % 64);
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
        "Reference the last logical slot before physical page padding"));
    QVERIFY2(terrain.appendSectorEdits(&transaction, &report),
             qPrintable(report.toText()));
    QByteArray paintedBytes;
    QVERIFY2(source.applyTransaction(transaction, &paintedBytes, &report),
             qPrintable(report.toText()));
    CMprDocument paintedSource;
    QVERIFY2(paintedSource.loadBytes(
                 paintedBytes, "working-padding-painted.mpr", &report),
             qPrintable(report.toText()));

    QVector<QImage> tiles(physicalPages * 64,
                          solid(qRgba(40, 80, 120, 255)));
    QVector<QByteArray> authoredPayloads;
    for(int page = 0; page < 4; ++page)
    {
        QString diagnostic;
        const QByteArray payload = ei::encodeMmpDxt1(
            CTestTerrainVirtualAtlas::composePage(tiles, page * 64, 8),
            true, &diagnostic);
        QVERIFY2(!payload.isEmpty(), qPrintable(diagnostic));
        authoredPayloads.append(payload);
    }
    QVector<qint32> logicalTypes(logicalSlots, qint32(2));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedSource, tiles, logicalTypes, authoredPayloads, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + "\n" + report.toText()));
    QCOMPARE(package.resourceMembers.size(), physicalPages);

    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
                                "working-padding-result.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().textureCount, quint32(physicalPages));
    QCOMPARE(reopened.mapInfo().tileCount, quint32(logicalSlots));
    QCOMPARE(reopened.mapInfo().tileTypes.size(), logicalSlots);
    QCOMPARE(reopened.mapInfo().tileTypes.last(), qint32(2));

    logicalTypes[logicalSlots - 1] = -1;
    CValidationReport invalidReport;
    const STerrainPortableAtlasPackage invalid =
        CTestTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedSource, tiles, logicalTypes, authoredPayloads,
            &invalidReport);
    QVERIFY(!invalid.success);
    QVERIFY(invalid.error.contains(QStringLiteral("slot 607")));
    QVERIFY(invalidReport.hasErrors());
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
    QVERIFY(QRegularExpression(QStringLiteral("^zone2_[0-9a-f]{12}$"))
                .match(zone2.isolatedNamespace).hasMatch());

    const auto repeated = a.finalize(
        field, QStringLiteral("zone2_eime_111111111111_eime_222222222222"),
        512);
    QVERIFY(repeated.success);
    QVERIFY(QRegularExpression(QStringLiteral("^zone2_[0-9a-f]{12}$"))
                .match(repeated.isolatedNamespace).hasMatch());
    QVERIFY(!repeated.isolatedNamespace.contains(QStringLiteral("eime")));
}

void TerrainVirtualAtlasTests::identityBaselineRequiresExactSharedCornerTopology()
{
    Fixture fixture;
    const auto makeSource = [](quint16 packed) {
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint32(0xCF4BF774) << quint8(0);
        payload.append(QByteArray(33 * 33 * 8, '\0'));
        QByteArray tiles(16 * 16 * 2, '\0');
        for(int y = 0; y < 16; ++y)
            for(int x = 0; x < 16; ++x)
                qToLittleEndian<quint16>(packed,
                    reinterpret_cast<uchar*>(tiles.data()
                        + (y * 16 + x) * 2));
        payload.append(tiles);
        STerrainSectorSource source;
        source.memberName = QStringLiteral("zone2000000.sec");
        source.sectorX = 0;
        source.sectorY = 0;
        source.type = 0;
        source.payload = payload;
        source.landTileOffset = 5 + 33 * 33 * 8;
        return source;
    };
    SMprPackedTile pureOne;
    pureOne.tileIndex = 0;
    SMprPackedTile pureTwo;
    pureTwo.tileIndex = 1;

    CTerrainEditModel exactModel;
    CValidationReport exactReport;
    QVERIFY(exactModel.loadSources(1, 1, 100.0f,
        {makeSource(pureOne.encode())}, &exactReport,
        1, QVector<qint32>(64, 1), 0));
    QVERIFY2(exactModel.initializeVirtualMaterialField(
        ETerrainTileLayer::Land, fixture.catalog, &exactReport),
        qPrintable(exactReport.toText()));
    CTerrainVirtualMaterialField exactField;
    QVERIFY(exactModel.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &exactField));

    // Legacy drafts can omit a border sector while still retaining a usable
    // rectangular terrain model. Recovery must close the no-vote vertex area
    // instead of claiming success with an incomplete modern paint field.
    STerrainSectorSource incompleteEdgeSource = makeSource(pureOne.encode());
    SMprPackedTile unknownEdgeTile;
    unknownEdgeTile.tileIndex = 63;
    qToLittleEndian<quint16>(unknownEdgeTile.encode(),
        reinterpret_cast<uchar*>(incompleteEdgeSource.payload.data()
            + incompleteEdgeSource.landTileOffset
            + (15 * 16 + 15) * 2));
    CTerrainEditModel incompleteEdgeModel;
    CValidationReport incompleteEdgeReport;
    QVERIFY(incompleteEdgeModel.loadSources(1, 1, 100.0f,
        {incompleteEdgeSource}, &incompleteEdgeReport,
        1, QVector<qint32>(64, 1), 0));
    QVERIFY2(incompleteEdgeModel.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, fixture.catalog,
                 &incompleteEdgeReport,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(incompleteEdgeReport.toText()));
    CTerrainVirtualMaterialField completedEdgeField;
    QVERIFY2(incompleteEdgeModel.copyVirtualMaterialField(
                 ETerrainTileLayer::Land, &completedEdgeField),
             qPrintable(incompleteEdgeReport.toText()));
    QCOMPARE(completedEdgeField.material(
                 completedEdgeField.tileWidth(),
                 completedEdgeField.tileHeight()), qint32(1));

    STerrainTransitionVariant contradictory;
    contradictory.signature = signature(1, 2, 1, 1);
    SMprPackedTile contradictoryPacked;
    contradictoryPacked.tileIndex = 4;
    contradictory.packedTile = contradictoryPacked.encode();
    QVERIFY(fixture.catalog.addVariant(contradictory));
    CTerrainEditModel contradictoryModel;
    CValidationReport contradictoryReport;
    QVERIFY(contradictoryModel.loadSources(1, 1, 100.0f,
        {makeSource(contradictoryPacked.encode())},
        &contradictoryReport, 1, QVector<qint32>(64, 1), 0));
    QVERIFY(!contradictoryModel.initializeVirtualMaterialField(
        ETerrainTileLayer::Land, fixture.catalog, &contradictoryReport));
    QVERIFY(contradictoryReport.hasErrors());
    CValidationReport recoveryReport;
    QVERIFY2(contradictoryModel.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, fixture.catalog, &recoveryReport,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(recoveryReport.toText()));
    CTerrainVirtualMaterialField recovered;
    QVERIFY(contradictoryModel.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &recovered));
    QCOMPARE(recovered.cellSignature(0, 0), signature(1, 1, 1, 1));

    QVector<QImage> sourceImages = fixture.images;
    sourceImages.append(solid(qRgba(90, 80, 70, 255)));
    const QVector<qint32> sourceTypes = reviewedFixtureTileTypes(5);
    CTerrainVirtualAtlas atlas(
        fixture.catalog, sourceImages, 0x44524146u, {}, {}, true,
        sourceTypes, reviewedFixtureMaterialTypes(fixture.catalog));
    const QVector<quint16> preferredCells(16 * 16,
                                          contradictoryPacked.encode());
    const QVector<qint32> preferredTypes(16 * 16, sourceTypes.at(4));
    const QVector<bool> preserveCells(16 * 16, true);
    const STerrainAtlasFinalization preserved = atlas.finalize(
        recovered, QStringLiteral("ContradictoryDraft"), 512,
        sourceTypes, &recoveryReport, {}, preferredTypes, preferredCells, {},
        &fixture.catalog, preserveCells);
    QVERIFY2(preserved.success,
             qPrintable(preserved.error + QStringLiteral("\n")
                        + recoveryReport.toText()));
    QCOMPARE(preserved.requiredSlots, 1);
    QCOMPARE(preserved.tiles.front().canonicalSignature,
             contradictory.signature);
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

void TerrainVirtualAtlasTests::portablePackageStagesLiveSculptBeforeAtlasFinalization()
{
    CMprDocument source;
    CValidationReport report;
    QVERIFY2(source.loadBytes(portableMpr(), "sculpt-source.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel edited;
    QVERIFY2(edited.load(source, &report), qPrintable(report.toText()));
    STerrainBrush point;
    point.centerX = 8.0f;
    point.centerY = 8.0f;
    point.radius = 0.0f;
    point.falloff = ETerrainBrushFalloff::Constant;
    QVERIFY2(edited.apply(edited.makeRaiseLower(point, 100.0f), true, &report),
             qPrintable(report.toText()));
    QCOMPARE(edited.height(8, 8), quint16(100));

    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("SculptedZone"), 512, {1, 2, 3, 4}, &report);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));

    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            source, edited, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n") + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes, "sculpt-result.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel reopenedTerrain;
    QVERIFY2(reopenedTerrain.load(reopened, &report), qPrintable(report.toText()));
    QCOMPARE(reopenedTerrain.height(8, 8), quint16(100));
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
    QVERIFY(command.virtualVertices.isEmpty());
    QCOMPARE(command.virtualFields.size(),1);
    QCOMPARE(command.semanticAudit.paintedVertices.size(),5);
    QCOMPARE(command.semanticAudit.solverMode,QString("virtual-unblended-preview"));
    QVERIFY(!command.deltas.isEmpty());
    SMprPackedTile pureSelected;pureSelected.tileIndex=1;
    for(const STerrainTileDelta& delta:command.deltas)
        QCOMPARE(delta.after,pureSelected.encode());
    QVERIFY(model.apply(command,true,&report));
    CTerrainVirtualMaterialField painted;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&painted));
    for(const QPoint& touched:command.semanticAudit.paintedVertices)
        QCOMPARE(painted.material(touched.x(),touched.y()),qint32(2));
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
    for(const QPoint& vertex : command.semanticAudit.paintedVertices)
        for(int y : {vertex.y()-1, vertex.y()})
            for(int x : {vertex.x()-1, vertex.x()})
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
                                               &releaseReport, 2);
        if(!releaseReport.hasErrors())
            ++usage[SMprPackedTile::decode(packed).atlasSlot()];
    }
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    const STerrainTileCommand resolved=model.makeVirtualStrokeResolution(
        command,working.catalog(),99,&releaseReport,false,2);
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    QCOMPARE(resolved.semanticAudit.solverMode,QString("virtual-release-exact"));
    QVERIFY(!resolved.deltas.isEmpty());
    for(const STerrainTileDelta& delta : resolved.deltas)
    {
        bool routeFound = false;
        const STerrainTransitionVariant route =
            working.catalog().variantForPacked(delta.after, &routeFound);
        QVERIFY(routeFound);
        QSet<qint32> materials;
        for(qint32 material : {delta.requestedSignature.northWest,
                               delta.requestedSignature.northEast,
                               delta.requestedSignature.southEast,
                               delta.requestedSignature.southWest})
            materials.insert(material);
        QCOMPARE(route.routeOwnerMaterial,
                 materials.size() == 2 ? qint32(2) : qint32(0));
    }
    QVERIFY(model.apply(resolved,true,&report));
    CTerrainVirtualMaterialField afterResolution;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&afterResolution));
    for(const QPoint& touched:command.semanticAudit.paintedVertices)
        QCOMPARE(afterResolution.material(touched.x(),touched.y()),qint32(2));
    QVERIFY(model.apply(resolved,false,&report));
    QVERIFY(model.apply(command,false,&report));
    QVector<qint32> undoneCurrent;
    QVector<qint32> undoneBaseline;
    QVERIFY(model.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land,&undoneCurrent,&undoneBaseline));
    QVERIFY(undoneCurrent.isEmpty());
    QVERIFY(undoneBaseline.isEmpty());
}

void TerrainVirtualAtlasTests::virtualTilePaintTargetsFourCellCorners()
{
    Fixture fixture;
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
    STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=0;
    const STerrainTileCommand command=model.makeVirtualTileMaterialPaint(
        brush,ETerrainTileLayer::Land,2,fixture.catalog,0x54494c45u,&report);
    QVERIFY2(!command.isEmpty(),qPrintable(report.toText()));
    QVERIFY(command.virtualVertices.isEmpty());
    QCOMPARE(command.virtualFields.size(),1);
    QCOMPARE(command.semanticAudit.paintedVertices.size(),4);
    QSet<QPair<int,int>> points;
    for(const QPoint& point:command.semanticAudit.paintedVertices)
        points.insert(qMakePair(point.x(),point.y()));
    QSet<QPair<int,int>> expected;
    expected << qMakePair(8,8) << qMakePair(9,8)
             << qMakePair(9,9) << qMakePair(8,9);
    QCOMPARE(points,expected);
    QCOMPARE(command.deltas.size(), 1);
    QCOMPARE(command.deltas.front().globalX, 8);
    QCOMPARE(command.deltas.front().globalY, 8);
}

void TerrainVirtualAtlasTests::mapResolutionRepairsWrongPhysicalBlendAndUndoesExactly()
{
    Fixture fixture;
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
    QVERIFY(model.initializeVirtualMaterialField(ETerrainTileLayer::Land,
        fixture.catalog,&report,ETerrainMaterialFieldRecovery::RecoverDeterministically));
    STerrainTileCommand damage;
    STerrainTileDelta wrong;wrong.globalX=8;wrong.globalY=8;
    wrong.layer=ETerrainTileLayer::Land;wrong.before=0;wrong.after=1;
    damage.deltas.append(wrong);
    QVERIFY(model.apply(damage,true,&report));
    const STerrainTileCommand repair=model.makeVirtualMapResolution(
        ETerrainTileLayer::Land,fixture.catalog,0x4d415000u,&report,false);
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    QCOMPARE(repair.deltas.size(),1);
    QCOMPARE(repair.deltas.front().globalX,8);
    QCOMPARE(repair.deltas.front().globalY,8);
    QCOMPARE(repair.deltas.front().before,quint16(1));
    QCOMPARE(repair.deltas.front().after,quint16(0));
    QVERIFY(model.apply(repair,true,&report));
    QCOMPARE(model.packedTile(8,8,ETerrainTileLayer::Land),quint16(0));
    QVERIFY(model.apply(repair,false,&report));
    QCOMPARE(model.packedTile(8,8,ETerrainTileLayer::Land),quint16(1));
}

void TerrainVirtualAtlasTests::connectedRouteOwnershipUsesSemanticDeltaAcrossAllTopologies()
{
    const qint32 water = 10;
    const qint32 lava = 20;
    const int tileWidth = 4;
    const int tileHeight = 3;
    const QVector<qint32> current = {
        water, water, water, water, water,
        water, lava,  lava,  water, lava,
        water, lava,  water, lava,  water,
        water, water, water, water, water
    };
    const QVector<qint32> baseline(current.size(), water);
    QVector<STerrainTransitionSignature> signatures;
    for(int y = 0; y < tileHeight; ++y)
        for(int x = 0; x < tileWidth; ++x)
        {
            const int nw = y * (tileWidth + 1) + x;
            signatures.append(signature(
                current.at(nw), current.at(nw + 1),
                current.at(nw + tileWidth + 2),
                current.at(nw + tileWidth + 1)));
        }
    const STerrainRouteOwnershipPlan plan =
        planConnectedTerrainRouteOwnership(
            tileWidth, tileHeight, signatures,
            QVector<bool>(tileWidth * tileHeight, true),
            current, baseline,
            QVector<qint32>(tileWidth * tileHeight, 0));

    QCOMPARE(plan.components.size(), 1);
    QSet<int> observedTopologies;
    int ownedCells = 0;
    for(const STerrainRouteOwnershipComponent& component : plan.components)
    {
        if(component.ownerMaterial != lava)
            continue;
        QCOMPARE(component.evidence,
                 ETerrainRouteOwnershipEvidence::SemanticDelta);
        for(int cell : component.cellIndexes)
        {
            QCOMPARE(plan.ownerByCell.value(cell), lava);
            const STerrainTransitionSignature value = signatures.at(cell);
            QMap<qint32, int> counts;
            for(qint32 material : {value.northWest, value.northEast,
                                   value.southEast, value.southWest})
                ++counts[material];
            if(counts.size() != 2)
                continue;
            const int lavaCount = counts.value(lava);
            if(lavaCount == 1)
                observedTopologies.insert(1); // outer 1/4
            else if(lavaCount == 3)
                observedTopologies.insert(3); // inside 3/4
            else if(value.northWest == value.southEast
                    && value.northEast == value.southWest)
                observedTopologies.insert(22); // diagonal 2/4
            else
                observedTopologies.insert(2); // adjacent 2/4
            ++ownedCells;
        }
    }
    QVERIFY(ownedCells > 0);
    QCOMPARE(observedTopologies, QSet<int>({1, 2, 3, 22}));
    QVERIFY(plan.preservedCells.isEmpty());

    QVector<qint32> reopenedOwners(tileWidth * tileHeight, 0);
    for(auto owner = plan.ownerByCell.constBegin();
        owner != plan.ownerByCell.constEnd(); ++owner)
        reopenedOwners[owner.key()] = lava;
    const STerrainRouteOwnershipPlan reopenedPlan =
        planConnectedTerrainRouteOwnership(
            tileWidth, tileHeight, signatures,
            QVector<bool>(tileWidth * tileHeight, true),
            current, current, reopenedOwners);
    QCOMPARE(reopenedPlan.ownerByCell, plan.ownerByCell);
    QCOMPARE(reopenedPlan.components.size(), 1);
    QCOMPARE(reopenedPlan.components.front().evidence,
             ETerrainRouteOwnershipEvidence::GeneratedProvenance);
}

void TerrainVirtualAtlasTests::connectedRouteOwnershipMirrorUsesReversePaintIntent()
{
    const qint32 water = 10;
    const qint32 lava = 20;
    const int tileWidth = 4;
    const int tileHeight = 3;
    const QVector<qint32> current = {
        lava, lava,  lava,  lava, lava,
        lava, water, water, lava, water,
        lava, water, lava,  water, lava,
        lava, lava,  lava,  lava, lava
    };
    const QVector<qint32> baseline(current.size(), lava);
    QVector<STerrainTransitionSignature> signatures;
    for(int y = 0; y < tileHeight; ++y)
        for(int x = 0; x < tileWidth; ++x)
        {
            const int nw = y * (tileWidth + 1) + x;
            signatures.append(signature(
                current.at(nw), current.at(nw + 1),
                current.at(nw + tileWidth + 2),
                current.at(nw + tileWidth + 1)));
        }
    const STerrainRouteOwnershipPlan plan =
        planConnectedTerrainRouteOwnership(
            tileWidth, tileHeight, signatures,
            QVector<bool>(tileWidth * tileHeight, true),
            current, baseline,
            QVector<qint32>(tileWidth * tileHeight, 0));

    QVERIFY(!plan.ownerByCell.isEmpty());
    for(qint32 owner : plan.ownerByCell)
        QCOMPARE(owner, water);
    QVERIFY(plan.preservedCells.isEmpty());
}

void TerrainVirtualAtlasTests::connectedRouteOwnershipScopesPlanningToRepairCandidates()
{
    // Zone-sized regression shape: many unrelated columns contain valid
    // pre-existing transitions, while Resolve has exactly one candidate.
    // Planning that candidate must traverse its complete connected outline,
    // but it must not classify every unrelated transition on the map.
    const int tileWidth = 128;
    const int tileHeight = 96;
    const int cellCount = tileWidth * tileHeight;
    const STerrainTransitionSignature vertical = signature(1, 2, 2, 1);
    const QVector<STerrainTransitionSignature> signatures(
        cellCount, vertical);
    const QVector<bool> available(cellCount, true);
    const QVector<qint32> unchanged(
        (tileWidth + 1) * (tileHeight + 1), 1);
    const QVector<qint32> provenance(cellCount, 1);
    QVector<bool> repairCandidates(cellCount, false);
    const int candidateX = 63;
    const int candidateY = 48;
    repairCandidates[candidateY * tileWidth + candidateX] = true;

    const STerrainRouteOwnershipPlan plan =
        planConnectedTerrainRouteOwnership(
            tileWidth, tileHeight, signatures, available,
            unchanged, unchanged, provenance, repairCandidates);

    QCOMPARE(plan.components.size(), 1);
    QCOMPARE(plan.components.front().cellIndexes.size(), tileHeight);
    QCOMPARE(plan.ownerByCell.size(), tileHeight);
    for(int y = 0; y < tileHeight; ++y)
        QCOMPARE(plan.ownerByCell.value(y * tileWidth + candidateX),
                 qint32(1));
    QVERIFY2(!plan.ownerByCell.contains(candidateY * tileWidth),
             "An unrelated exact transition was pulled into Resolve ownership planning");
}

void TerrainVirtualAtlasTests::mirroredGeneratedBlendRemainsPaintable_data()
{
    QTest::addColumn<bool>("mirrored");
    QTest::addColumn<int>("materials");
    for(bool mirrored : {false, true})for(int materials : {3, 4})
        QTest::newRow(qPrintable(QString("mirror%1-materials%2").arg(mirrored).arg(materials)))
            << mirrored << materials;
}

void TerrainVirtualAtlasTests::mirroredGeneratedBlendRemainsPaintable()
{
    QFETCH(bool, mirrored);
    QFETCH(int, materials);
    Fixture fixture;
    fixture.catalog.setMirrorPaintGridY(mirrored);
    const auto wanted=signature(1, 2, 3, materials==4?4:3);
    // Only the reflected chiral blend is authored. A reflection of a
    // three/four-material tuple cannot be obtained by rotating this artwork.
    const auto authoredGrid=CTerrainTransitionCatalog::mirroredGridY(wanted);
    const auto physical=mirrored
        ? CTerrainTransitionCatalog::mirroredGridY(authoredGrid):authoredGrid;
    fixture.images.append(solid(qRgba(80,80,80,255)));
    for(int rotation=0;rotation<4;++rotation) {
        SMprPackedTile packed;packed.tileIndex=4;packed.rotation=quint8(rotation);
        STerrainTransitionVariant variant;variant.packedTile=packed.encode();
        variant.signature=CTerrainTransitionCatalog::rotated(physical,-rotation);
        QVERIFY(fixture.catalog.addVariant(variant));
    }
    bool found=true;
    fixture.catalog.selectForPaint(wanted,3,5,23,1,&found);
    QVERIFY(!found);
    const auto oldImages=fixture.images;
    CTestTerrainWorkingAtlas working(fixture.catalog,fixture.images,23,64,{}, {},
        QVector<qint32>(5,0),{{1,0},{2,0},{3,0},{4,0}});
    QVERIFY(working.isValid());
    QMap<int,quint64> usage;
    bool generated=false;CValidationReport report;
    const auto packed=working.resolve(wanted,3,5,&usage,{},&generated,&report,1);
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    QVERIFY(generated);
    QVERIFY(SMprPackedTile::decode(packed).atlasSlot()>=5);
    working.catalog().selectForPaint(wanted,3,5,23,1,&found);
    QVERIFY2(found,"Resolve reported success but did not register the exact generated blend for painting");
    bool decoded=false;
    QCOMPARE(working.catalog().gridSignatureForPacked(packed,&decoded),wanted);
    QVERIFY(decoded);
    for(int slot=0;slot<oldImages.size();++slot)
        QCOMPARE(working.activeTileImages().at(slot),oldImages.at(slot));
    for(int rotation=0;rotation<4;++rotation) {
        const auto rotated=CTerrainTransitionCatalog::rotated(wanted,rotation);
        const auto selected=working.catalog().selectForPaint(rotated,7,9,23,1,&found);
        QVERIFY(found);
        QCOMPARE(working.catalog().gridSignatureForPacked(selected,&decoded),rotated);
        QVERIFY(decoded);
    }
}

void TerrainVirtualAtlasTests::mirroredVertexStrokeResolvesAndUndoesExactly()
{
    Fixture fixture;
    fixture.catalog.setMirrorPaintGridY(true);
    const auto wanted=signature(1,2,3,3);
    fixture.images.append(solid(qRgba(80,80,80,255)));
    for(int rotation=0;rotation<4;++rotation) {
        SMprPackedTile packed;packed.tileIndex=4;packed.rotation=quint8(rotation);
        STerrainTransitionVariant variant;variant.packedTile=packed.encode();
        // Physical wanted means authored reflected wanted in the paint grid.
        variant.signature=CTerrainTransitionCatalog::rotated(wanted,-rotation);
        QVERIFY(fixture.catalog.addVariant(variant));
    }
    CTerrainWorkingAtlas live(fixture.catalog,fixture.images,23,64,{}, {},
        QVector<qint32>(5,0),{{1,0},{2,0},{3,0},{4,0}});
    QVERIFY(live.isValid());
    auto staged=live.createStagingCopy();
    QByteArray payload;QDataStream sec(&payload,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec<<quint32(0xCF4BF774)<<quint8(0);
    payload.append(QByteArray(33*33*8,'\0'));
    payload.append(QByteArray(16*16*2,'\0'));
    STerrainSectorSource source;source.memberName="CaveSet000000.SEC";
    source.sectorX=0;source.sectorY=0;source.type=0;
    source.payload=payload;source.landTileOffset=5+33*33*8;
    CTerrainEditModel model;CValidationReport report;
    QVERIFY(model.loadSources(1,1,100.0f,{source},&report,1,QVector<qint32>(64,0),0));
    QVector<qint32> values(17*17,1);
    values[9*17+7]=3;values[9*17+8]=3;
    QVERIFY(model.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,values,values));
    const auto before=model.sectorPayload(0,0);
    CTerrainVirtualMaterialField beforeField;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&beforeField));
    STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=0;
    const auto preview=model.makeVirtualMaterialPaint(8,8,brush,
        ETerrainTileLayer::Land,2,staged->catalog(),23,&report);
    QVERIFY2(!preview.isEmpty()&&!report.hasErrors(),qPrintable(report.toText()));
    QVERIFY(model.apply(preview,true,&report));
    CTerrainVirtualMaterialField field;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&field));
    bool found=false;QCOMPARE(field.cellSignature(7,8,&found),wanted);QVERIFY(found);
    QMap<int,quint64> usage;
    for(int y:{7,8})for(int x:{7,8}) {
        const auto realized=staged->resolve(field.cellSignature(x,y,&found),x,y,&usage,{},nullptr,&report,2);
        ++usage[SMprPackedTile::decode(realized).atlasSlot()];
        QVERIFY(found);
        QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    }
    QVERIFY(model.installWorkingAtlasSlotTypes(staged->activeTileTypes()));
    CValidationReport release;
    const auto resolved=model.makeVirtualStrokeResolution(preview,staged->catalog(),23,&release,false,2);
    QVERIFY2(!release.hasErrors(),qPrintable(release.toText()));
    QVERIFY(!resolved.isEmpty());
    QVERIFY(model.apply(resolved,true,&report));
    for(int y:{7,8})for(int x:{7,8}) {
        const auto packed=model.packedTile(x,y,ETerrainTileLayer::Land,&found);QVERIFY(found);
        QCOMPARE(staged->catalog().gridSignatureForPacked(packed,&found),field.cellSignature(x,y));
        QVERIFY(found);
    }
    QVERIFY(model.apply(resolved,false,&report));
    QVERIFY(model.apply(preview,false,&report));
    QCOMPARE(model.sectorPayload(0,0),before);
    CTerrainVirtualMaterialField undone;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&undone));
    QCOMPARE(undone.stableSha256(),beforeField.stableSha256());
    // Planning and cancelling never publishes the staging atlas into live.
    QCOMPARE(live.activeTileCount(),5);
    QCOMPARE(live.changedSlots().size(),0);
    QCOMPARE(live.authoredTileImages(),fixture.images);
}

void TerrainVirtualAtlasTests::routeOwnerFiltersOppositeGeneratedExactCandidates()
{
    const qint32 water = 10;
    const qint32 lava = 20;
    CTerrainTransitionCatalog catalog;
    const STerrainTransitionSignature requested =
        signature(water, lava, lava, water);
    STerrainTransitionVariant waterRoute;
    waterRoute.signature = requested;
    waterRoute.packedTile = quint16(7);
    waterRoute.routeOwnerMaterial = 10;
    QVERIFY(catalog.addVariant(waterRoute));
    STerrainTransitionVariant lavaRoute = waterRoute;
    lavaRoute.packedTile = quint16(8);
    lavaRoute.routeOwnerMaterial = 20;
    QVERIFY(catalog.addVariant(lavaRoute));

    for(int x = 0; x < 32; ++x)
        for(int y = 0; y < 32; ++y)
        {
            bool found = false;
            QCOMPARE(catalog.selectForPaint(
                         requested, x, y, 0x524f5554u, 20, &found),
                     lavaRoute.packedTile);
            QVERIFY(found);
            QCOMPARE(catalog.selectForPaint(
                         requested, x, y, 0x524f5554u, 10, &found),
                     waterRoute.packedTile);
            QVERIFY(found);
        }

    CTerrainTransitionCatalog legacyGeneratedOnly;
    STerrainTransitionVariant legacyGenerated = waterRoute;
    legacyGenerated.packedTile = quint16(9);
    legacyGenerated.routeOwnerMaterial = 0;
    legacyGenerated.canonicalGeneratedRoute = true;
    QVERIFY(legacyGeneratedOnly.addVariant(legacyGenerated));
    bool found = true;
    legacyGeneratedOnly.selectForPaint(
        requested, 0, 0, 0x524f5554u, lava, &found);
    QVERIFY(!found);

    CTerrainTransitionCatalog authoredPrecedence;
    STerrainTransitionVariant authored = legacyGenerated;
    authored.packedTile = quint16(6);
    authored.canonicalGeneratedRoute = false;
    QVERIFY(authoredPrecedence.addVariant(authored));
    QVERIFY(authoredPrecedence.addVariant(waterRoute));
    QCOMPARE(authoredPrecedence.selectForPaint(
                 requested, 0, 0, 0x524f5554u, lava, &found),
             authored.packedTile);
    QVERIFY(found);

    CTerrainTransitionCatalog generatedUniformCatalog;
    STerrainTransitionVariant generatedUniform;
    generatedUniform.signature = signature(lava, lava, lava, lava);
    generatedUniform.packedTile = quint16(12);
    generatedUniform.canonicalGeneratedRoute = true;
    QVERIFY(generatedUniformCatalog.addVariant(generatedUniform));
    QCOMPARE(generatedUniformCatalog.selectForPaint(
                 generatedUniform.signature, 0, 0, 0x524f5554u,
                 lava, &found),
             generatedUniform.packedTile);
    QVERIFY(found);
}

void TerrainVirtualAtlasTests::ambiguousAndMultiMaterialRouteComponentsArePreserved()
{
    const int tileWidth = 3;
    const int tileHeight = 1;
    const QVector<STerrainTransitionSignature> signatures = {
        signature(1, 2, 2, 1),
        signature(1, 2, 3, 1),
        signature(1, 2, 2, 1)
    };
    const QVector<qint32> current = {1, 2, 1, 2,
                                     1, 2, 3, 2};
    const QVector<qint32> baseline = {2, 1, 1, 2,
                                      2, 1, 3, 2};
    const STerrainRouteOwnershipPlan junctionPlan =
        planConnectedTerrainRouteOwnership(
            tileWidth, tileHeight, signatures,
            QVector<bool>(tileWidth * tileHeight, true),
            current, baseline, QVector<qint32>({1, 0, 1}));
    QVERIFY(junctionPlan.ownerByCell.isEmpty());
    QVERIFY(junctionPlan.preservedCells.contains(0));
    QVERIFY(junctionPlan.preservedCells.contains(2));
    bool junctionReason = false;
    for(const STerrainRouteOwnershipComponent& component :
            junctionPlan.components)
        junctionReason |= component.ambiguity
            == ETerrainRouteOwnershipAmbiguity::MultiMaterialJunction;
    QVERIFY(junctionReason);

    const QVector<STerrainTransitionSignature> twoMaterial = {
        signature(1, 1, 2, 2), signature(1, 2, 2, 2)};
    const QVector<qint32> unchanged = {1, 1, 2,
                                       2, 2, 2};
    const STerrainRouteOwnershipPlan mixedProvenance =
        planConnectedTerrainRouteOwnership(
            2, 1, twoMaterial, QVector<bool>(2, true),
            unchanged, unchanged, QVector<qint32>({1, 2}));
    QVERIFY(mixedProvenance.ownerByCell.isEmpty());
    QCOMPARE(mixedProvenance.preservedCells, QSet<int>({0, 1}));
}

void TerrainVirtualAtlasTests::mapResolutionNormalizesRouteOwnerAndIsIdempotent()
{
    Fixture fixture;
    const qint32 water = 10;
    const qint32 lava = 20;
    const STerrainTransitionSignature requested =
        signature(water, lava, lava, water);

    STerrainTransitionVariant waterRoute;
    waterRoute.signature = requested;
    SMprPackedTile waterPacked;
    waterPacked.tileIndex = 10;
    waterRoute.packedTile = waterPacked.encode();
    // Simulate the pre-fix in-memory R8 hybrid: generated artwork was exact
    // but carried no owner. Semantic-delta evidence must be able to replace
    // it without treating it as authored route-neutral artwork.
    waterRoute.routeOwnerMaterial = 0;
    waterRoute.canonicalGeneratedRoute = true;
    QVERIFY(fixture.catalog.addVariant(waterRoute));

    STerrainTransitionVariant lavaRoute = waterRoute;
    SMprPackedTile lavaPacked;
    lavaPacked.tileIndex = 11;
    lavaRoute.packedTile = lavaPacked.encode();
    lavaRoute.routeOwnerMaterial = lava;
    QVERIFY(fixture.catalog.addVariant(lavaRoute));

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));

    const int cellX = 8;
    const int cellY = 8;
    const int cellKey = cellY * 16 + cellX;
    STerrainTileCommand damage;
    STerrainTileDelta wrong;
    wrong.globalX = cellX;
    wrong.globalY = cellY;
    wrong.layer = ETerrainTileLayer::Land;
    wrong.before = model.packedTile(cellX, cellY,
                                    ETerrainTileLayer::Land);
    wrong.after = waterRoute.packedTile;
    damage.deltas.append(wrong);
    QVERIFY(model.apply(damage, true, &report));

    CTerrainVirtualMaterialField repairField;
    QVERIFY(repairField.resize(16, 16, water));
    QVERIFY(repairField.setMaterial(cellX + 1, cellY, lava));
    QVERIFY(repairField.setMaterial(cellX + 1, cellY + 1, lava));
    QCOMPARE(repairField.cellSignature(cellX, cellY), requested);
    const QMap<int, qint32> owners = {{cellKey, lava}};
    const QSet<int> force = {cellKey};
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x524f5554u, &report,
        false, &repairField, false, false, &owners, &force, nullptr);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(repair.deltas.size(), 1);
    QCOMPARE(repair.deltas.front().before, waterRoute.packedTile);
    QCOMPARE(repair.deltas.front().after, lavaRoute.packedTile);

    QVERIFY(model.apply(repair, true, &report));
    QCOMPARE(model.packedTile(cellX, cellY, ETerrainTileLayer::Land),
             lavaRoute.packedTile);
    QVERIFY(model.apply(repair, false, &report));
    QCOMPARE(model.packedTile(cellX, cellY, ETerrainTileLayer::Land),
             waterRoute.packedTile);
    QVERIFY(model.apply(repair, true, &report));
    QCOMPARE(model.packedTile(cellX, cellY, ETerrainTileLayer::Land),
             lavaRoute.packedTile);

    const STerrainTileCommand second = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x524f5554u, &report,
        false, &repairField, false, false, &owners, &force, nullptr);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(second.deltas.isEmpty());
}

void TerrainVirtualAtlasTests::mapResolutionPreservesRuntimeTypeForTiedGeneratedBlends()
{
    Fixture fixture;
    const QVector<qint32> authoredTypes = {5, 6, 5, 6};
    const QMap<qint32, qint32> materialTypes = {
        {1, 5}, {2, 6}, {3, 5}, {4, 6}};
    CTerrainWorkingAtlas working(
        fixture.catalog, fixture.images, 0x54595045u, 16, {}, {},
        authoredTypes, materialTypes);
    seedReviewedPairMasks(working, 1, 2, fixture.images.at(0),
                          fixture.images.at(1));

    // A 2/2 tuple has no majority runtime terrain type. Resolve Missing Blends
    // must therefore retain the current physical slot's reviewed type rather
    // than collapsing both cells onto the first generated signature match.
    const STerrainTransitionSignature tied = signature(1, 1, 2, 2);
    QMap<int, quint64> usage;
    CValidationReport prepareReport;
    bool generatedFive = false;
    const quint16 packedFive = working.resolve(
        tied, 3, 3, &usage, {}, &generatedFive, &prepareReport, 1, 5);
    // The map resolver keeps every newly realized slot reserved while it
    // prepares a batch. Mirror that here so realizing the type-6 tuple cannot
    // reuse the just-prepared type-5 physical slot.
    usage.insert(SMprPackedTile::decode(packedFive).atlasSlot(), 1);
    bool generatedSix = false;
    const quint16 packedSix = working.resolve(
        tied, 12, 12, &usage, {}, &generatedSix, &prepareReport, 2, 6);
    QVERIFY2(!prepareReport.hasErrors(), qPrintable(prepareReport.toText()));
    QVERIFY(generatedFive);
    QVERIFY(generatedSix);
    const int slotFive = SMprPackedTile::decode(packedFive).atlasSlot();
    const int slotSix = SMprPackedTile::decode(packedSix).atlasSlot();
    QVERIFY(slotFive != slotSix);
    QCOMPARE(working.runtimeTerrainTypeForSlot(slotFive), qint32(5));
    QCOMPARE(working.runtimeTerrainTypeForSlot(slotSix), qint32(6));
    QSet<qint32> preparedTypes;
    for(const STerrainTransitionVariant& variant :
        working.catalog().paintVariants(tied))
    {
        const qint32 type = working.runtimeTerrainTypeForSlot(
            SMprPackedTile::decode(variant.packedTile).atlasSlot());
        if(type >= 0)
            preparedTypes.insert(type);
    }
    QSet<qint32> expectedPreparedTypes;
    expectedPreparedTypes << 5 << 6;
    QCOMPARE(preparedTypes, expectedPreparedTypes);
    bool untypedFound = false;
    const quint16 oldUntypedChoice = working.catalog().selectForPaint(
        tied, 12, 12, 0x54595045u ^ 0x6c616e64u, &untypedFound);
    QVERIFY(untypedFound);
    QCOMPARE(working.runtimeTerrainTypeForSlot(
                 SMprPackedTile::decode(oldUntypedChoice).atlasSlot()),
             qint32(5));
    // The former signature-only map resolver made precisely this type-5 choice
    // for the type-6 predecessor below. The exact resolver must instead filter
    // candidates to the predecessor's runtime type.

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    QVector<qint32> modelTypes(64, 5);
    modelTypes[1] = 6;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              modelTypes, 0));
    model.enableWorkingAtlasSlots(working.capacityLimit());
    QVERIFY(model.setWorkingAtlasSlotType(slotFive, 5));
    QVERIFY(model.setWorkingAtlasSlotType(slotSix, 6));

    // Preserve different reviewed predecessor types at the two damaged cells.
    STerrainTileCommand damage;
    STerrainTileDelta typeSixBefore;
    typeSixBefore.globalX = 12;
    typeSixBefore.globalY = 12;
    typeSixBefore.layer = ETerrainTileLayer::Land;
    typeSixBefore.before = 0;
    typeSixBefore.after = 1;
    damage.deltas.append(typeSixBefore);
    QVERIFY(model.apply(damage, true, &report));

    CTerrainVirtualMaterialField repairField;
    const qint32 unset = std::numeric_limits<qint32>::min();
    QVERIFY(repairField.resize(model.tileCountX(), model.tileCountY(), unset));
    const auto setTiedCell = [&repairField](int x, int y) {
        QVERIFY(repairField.setMaterial(x, y, 1));
        QVERIFY(repairField.setMaterial(x + 1, y, 1));
        QVERIFY(repairField.setMaterial(x + 1, y + 1, 2));
        QVERIFY(repairField.setMaterial(x, y + 1, 2));
    };
    setTiedCell(3, 3);
    setTiedCell(12, 12);

    CValidationReport repairReport;
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, working.catalog(), 0x54595045u,
        &repairReport, false, &repairField, true, true);
    QVERIFY2(!repairReport.hasErrors(), qPrintable(repairReport.toText()));
    QCOMPARE(repair.deltas.size(), 2);
    QMap<QPair<int, int>, qint32> expectedTypes;
    expectedTypes.insert(qMakePair(3, 3), 5);
    expectedTypes.insert(qMakePair(12, 12), 6);
    QMap<QPair<int, int>, quint16> repairedPacked;
    for(const STerrainTileDelta& delta : repair.deltas)
    {
        const QPair<int, int> point = qMakePair(delta.globalX, delta.globalY);
        QVERIFY(expectedTypes.contains(point));
        repairedPacked.insert(point, delta.after);
        bool typeFound = false;
        QCOMPARE(model.tileType(delta.after, &typeFound),
                 expectedTypes.value(point));
        QVERIFY(typeFound);
    }

    QVERIFY(model.apply(repair, true, &report));
    QCOMPARE(model.packedTile(3, 3, ETerrainTileLayer::Land),
             repairedPacked.value(qMakePair(3, 3)));
    QCOMPARE(model.packedTile(12, 12, ETerrainTileLayer::Land),
             repairedPacked.value(qMakePair(12, 12)));
    QCOMPARE(model.tileType(model.packedTile(3, 3, ETerrainTileLayer::Land)),
             qint32(5));
    QCOMPARE(model.tileType(model.packedTile(12, 12, ETerrainTileLayer::Land)),
             qint32(6));

    CValidationReport settledReport;
    QVERIFY(model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, working.catalog(), 0x54595045u,
        &settledReport, false, &repairField, true, true).isEmpty());
    QVERIFY2(!settledReport.hasErrors(), qPrintable(settledReport.toText()));

    QVERIFY(model.apply(repair, false, &report));
    QCOMPARE(model.packedTile(3, 3, ETerrainTileLayer::Land), quint16(0));
    QCOMPARE(model.packedTile(12, 12, ETerrainTileLayer::Land), quint16(1));
}

void TerrainVirtualAtlasTests::mapResolutionPreservesMissingTypedCandidateInPartialMode()
{
    CTerrainTransitionCatalog catalog;
    STerrainTransitionVariant uniformOne;
    uniformOne.signature = signature(1, 1, 1, 1);
    uniformOne.packedTile = 0;
    QVERIFY(catalog.addVariant(uniformOne));
    STerrainTransitionVariant uniformTwo;
    uniformTwo.signature = signature(2, 2, 2, 2);
    uniformTwo.packedTile = 1;
    QVERIFY(catalog.addVariant(uniformTwo));
    STerrainTransitionVariant tied;
    tied.signature = signature(1, 1, 2, 2);
    tied.packedTile = 2;
    QVERIFY(catalog.addVariant(tied));

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    QVector<qint32> modelTypes(64, 5);
    modelTypes[1] = 6;
    modelTypes[2] = 5;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              modelTypes, 0));
    STerrainTileCommand damage;
    STerrainTileDelta wrong;
    wrong.globalX = 8;
    wrong.globalY = 8;
    wrong.layer = ETerrainTileLayer::Land;
    wrong.before = 0;
    wrong.after = 1;
    damage.deltas.append(wrong);
    QVERIFY(model.apply(damage, true, &report));

    CTerrainVirtualMaterialField repairField;
    const qint32 unset = std::numeric_limits<qint32>::min();
    QVERIFY(repairField.resize(16, 16, unset));
    QVERIFY(repairField.setMaterial(8, 8, 1));
    QVERIFY(repairField.setMaterial(9, 8, 1));
    QVERIFY(repairField.setMaterial(9, 9, 2));
    QVERIFY(repairField.setMaterial(8, 9, 2));
    CValidationReport partialReport;
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, catalog, 0x50415254u,
        &partialReport, false, &repairField, true, true);
    QVERIFY2(!partialReport.hasErrors(), qPrintable(partialReport.toText()));
    QVERIFY(repair.deltas.isEmpty());
    QCOMPARE(model.packedTile(8, 8, ETerrainTileLayer::Land), quint16(1));
    bool focusedWarning = false;
    for(const SValidationFinding& finding : partialReport.findings())
        focusedWarning |= finding.code == QStringLiteral(
            "terrain.virtual_map_runtime_type_skipped");
    QVERIFY2(focusedWarning,
             "A partial map Resolve silently omitted its unavailable typed tuple");
}

void TerrainVirtualAtlasTests::mapResolutionReachesFixedPointInOneCommand()
{
    Fixture fixture;
    STerrainTransitionVariant vertical;
    vertical.signature = signature(1, 2, 2, 1);
    vertical.packedTile = 4;
    QVERIFY(fixture.catalog.addVariant(vertical));
    STerrainTransitionVariant damaged;
    damaged.signature = signature(1, 2, 1, 2);
    damaged.packedTile = 5;
    QVERIFY(fixture.catalog.addVariant(damaged));

    const ETerrainTileLayer layers[] = {
        ETerrainTileLayer::Land, ETerrainTileLayer::Overlay};
    for(const ETerrainTileLayer layer : layers)
    {
        const bool overlay = layer == ETerrainTileLayer::Overlay;
        const int vertexBlockSize = 33 * 33 * 8;
        const int tileBlockSize = 16 * 16 * 2;
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint32(0xCF4BF774) << quint8(overlay ? 3 : 0);
        payload.append(QByteArray(vertexBlockSize, '\0'));
        if(overlay)
            payload.append(QByteArray(vertexBlockSize, '\0'));
        payload.append(QByteArray(tileBlockSize, '\0'));
        if(overlay)
        {
            payload.append(QByteArray(tileBlockSize, '\0'));
            // Material zero keeps every overlay tile active.
            payload.append(QByteArray(tileBlockSize, '\0'));
        }
        STerrainSectorSource source;
        source.memberName = QStringLiteral("CaveSet000000.SEC");
        source.sectorX = 0;
        source.sectorY = 0;
        source.type = overlay ? 3 : 0;
        source.payload = payload;
        source.overlayVertexOffset = overlay ? 5 + vertexBlockSize : 0;
        source.landTileOffset = 5 + vertexBlockSize
            + (overlay ? vertexBlockSize : 0);
        source.overlayTileOffset = overlay
            ? source.landTileOffset + tileBlockSize : 0;
        source.overlayMaterialOffset = overlay
            ? source.overlayTileOffset + tileBlockSize : 0;
        CTerrainEditModel model;
        CValidationReport report;
        QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                                  QVector<qint32>(64, 1), overlay ? 1 : 0));

        // This is the pre-connected-route fixed-point contract shared by
        // Ground and Overlay: a coherent vertical 1/2 boundary is present
        // everywhere except for one consecutive damaged chain. One bounded
        // map-resolution command repairs exactly that chain, owns one exact
        // undo/redo delta set, and the next invocation is a no-op.
        STerrainTileCommand setup;
        for(int y = 0; y < 16; ++y)
            for(int x = 8; x < 16; ++x)
            {
                STerrainTileDelta delta;
                delta.globalX = x;
                delta.globalY = y;
                delta.layer = layer;
                delta.before = 0;
                delta.after = x == 8 ? 4 : 1;
                setup.deltas.append(delta);
            }
        QVERIFY(model.apply(setup, true, &report));
        STerrainTileCommand corrupt;
        for(int y = 3; y <= 12; ++y)
        {
            STerrainTileDelta delta;
            delta.globalX = 8;
            delta.globalY = y;
            delta.layer = layer;
            delta.before = 4;
            delta.after = 5;
            corrupt.deltas.append(delta);
        }
        QVERIFY(model.apply(corrupt, true, &report));

        CTerrainVirtualMaterialField sparseRecovery;
        QVERIFY(sparseRecovery.resize(model.tileCountX(), model.tileCountY()));
        for(int y = 0; y <= sparseRecovery.tileHeight(); ++y)
            for(int x = 0; x <= sparseRecovery.tileWidth(); ++x)
                sparseRecovery.setMaterial(
                    x, y, std::numeric_limits<qint32>::min());
        CValidationReport repairReport;
        const STerrainTileCommand repair = model.makeVirtualMapResolution(
            layer, fixture.catalog, 0x46495850u,
            &repairReport, false, &sparseRecovery, true, false);
        QVERIFY2(!repairReport.hasErrors(), qPrintable(repairReport.toText()));
        QCOMPARE(repair.deltas.size(), 10);
        QSet<QPair<int, int>> repaired;
        for(const STerrainTileDelta& delta : repair.deltas)
        {
            QCOMPARE(delta.layer, layer);
            QCOMPARE(delta.after, quint16(4));
            repaired.insert(qMakePair(delta.globalX, delta.globalY));
        }
        QSet<QPair<int, int>> expected;
        for(int y = 3; y <= 12; ++y)
            expected.insert(qMakePair(8, y));
        QCOMPARE(repaired, expected);

        QVERIFY(model.apply(repair, true, &report));
        for(int y = 3; y <= 12; ++y)
            QCOMPARE(model.packedTile(8, y, layer), quint16(4));
        QVERIFY(model.apply(repair, false, &report));
        for(int y = 3; y <= 12; ++y)
            QCOMPARE(model.packedTile(8, y, layer), quint16(5));
        QVERIFY(model.apply(repair, true, &report));
        for(int y = 3; y <= 12; ++y)
            QCOMPARE(model.packedTile(8, y, layer), quint16(4));

        CValidationReport settledReport;
        const STerrainTileCommand settled = model.makeVirtualMapResolution(
            layer, fixture.catalog, 0x46495850u,
            &settledReport, false, &sparseRecovery, true, false);
        QVERIFY2(!settledReport.hasErrors(), qPrintable(settledReport.toText()));
        QVERIFY2(settled.isEmpty(),
                 "Resolve Missing Blends must be idempotent after one command");
    }
}

void TerrainVirtualAtlasTests::mapResolutionPrefersAuthoritativeVirtualField()
{
    Fixture fixture;
    STerrainTransitionVariant vertical;
    vertical.signature = signature(1, 2, 2, 1);
    vertical.packedTile = 4;
    QVERIFY(fixture.catalog.addVariant(vertical));
    STerrainTransitionVariant inverse;
    inverse.signature = signature(2, 1, 1, 2);
    inverse.packedTile = 5;
    QVERIFY(fixture.catalog.addVariant(inverse));

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));

    STerrainTileCommand setup;
    for(int y = 0; y < 16; ++y)
        for(int x = 8; x < 16; ++x)
        {
            STerrainTileDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.layer = ETerrainTileLayer::Land;
            delta.before = 0;
            delta.after = x == 8 ? 4 : 1;
            setup.deltas.append(delta);
        }
    QVERIFY(model.apply(setup, true, &report));
    QVERIFY2(model.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, fixture.catalog, &report),
             qPrintable(report.toText()));

    STerrainTileCommand damage;
    STerrainTileDelta erased;
    erased.globalX = 8;
    erased.globalY = 8;
    erased.layer = ETerrainTileLayer::Land;
    erased.before = 4;
    erased.after = 0;
    damage.deltas.append(erased);
    QVERIFY(model.apply(damage, true, &report));

    // The conservative field and physical neighbours describe the inverse
    // boundary. The model-owned virtual field was captured before corruption
    // and is therefore the only authoritative semantic evidence.
    CTerrainVirtualMaterialField recoveredButWrong;
    QVERIFY(recoveredButWrong.resize(model.tileCountX(), model.tileCountY()));
    for(int y = 0; y <= recoveredButWrong.tileHeight(); ++y)
        for(int x = 0; x <= recoveredButWrong.tileWidth(); ++x)
            recoveredButWrong.setMaterial(x, y, x <= 8 ? 2 : 1);
    CValidationReport repairReport;
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x41555448u,
        &repairReport, false, &recoveredButWrong, true, true);
    QVERIFY2(!repairReport.hasErrors(), qPrintable(repairReport.toText()));
    QCOMPARE(repair.deltas.size(), 1);
    QCOMPARE(repair.deltas.front().globalX, 8);
    QCOMPARE(repair.deltas.front().globalY, 8);
    QCOMPARE(repair.deltas.front().after, quint16(4));
    QVERIFY(model.apply(repair, true, &report));

    CValidationReport settledReport;
    QVERIFY(model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x41555448u,
        &settledReport, false, &recoveredButWrong, true, true).isEmpty());
    QVERIFY2(!settledReport.hasErrors(), qPrintable(settledReport.toText()));
}

void TerrainVirtualAtlasTests::mapResolutionDoesNotChooseAWinningSideForTiedBorders()
{
    Fixture fixture;
    STerrainTransitionVariant vertical;
    vertical.signature = signature(1, 2, 2, 1);
    vertical.packedTile = 4;
    QVERIFY(fixture.catalog.addVariant(vertical));

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));

    STerrainTileCommand hardBorder;
    for(int y = 0; y < 16; ++y)
        for(int x = 8; x < 16; ++x)
        {
            STerrainTileDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.layer = ETerrainTileLayer::Land;
            delta.before = 0;
            delta.after = 1;
            hardBorder.deltas.append(delta);
        }
    QVERIFY(model.apply(hardBorder, true, &report));
    CTerrainVirtualMaterialField repairField;
    QVERIFY(model.recoverUnambiguousVirtualMaterialField(
        ETerrainTileLayer::Land, fixture.catalog, &repairField, &report));
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x54494544u, &report,
        false, &repairField, true);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY2(repair.isEmpty(),
             "Map resolution arbitrarily moved a tied hard border to the lower material id");
    for(int y = 0; y < 16; ++y)
        for(int x = 0; x < 16; ++x)
            QCOMPARE(model.packedTile(x, y, ETerrainTileLayer::Land),
                     quint16(x < 8 ? 0 : 1));
}

void TerrainVirtualAtlasTests::recoveredMapResolutionRepairsDecodedContextStampsLikeAnyBlend()
{
    Fixture fixture;
    STerrainTransitionVariant stamp;
    stamp.signature = signature(1, 2, 2, 2);
    stamp.packedTile = 4;
    QVERIFY(fixture.catalog.addVariant(stamp));

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));

    STerrainTileCommand stampCommand;
    STerrainTileDelta stampDelta;
    stampDelta.globalX = 8;
    stampDelta.globalY = 8;
    stampDelta.layer = ETerrainTileLayer::Land;
    stampDelta.before = 0;
    stampDelta.after = 4;
    stampCommand.deltas.append(stampDelta);
    QVERIFY(model.apply(stampCommand, true, &report));

    CTerrainVirtualMaterialField recoveredLikeField;
    QVERIFY(recoveredLikeField.resize(model.tileCountX(), model.tileCountY()));
    for(int y = 0; y <= recoveredLikeField.tileHeight(); ++y)
        for(int x = 0; x <= recoveredLikeField.tileWidth(); ++x)
            recoveredLikeField.setMaterial(x, y, 2);
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x5354414du, &report,
        false, &recoveredLikeField, true, true);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY2(!repair.isEmpty(),
             "A decoded contextual stamp received special repair immunity");
    bool repairedStamp = false;
    for(const STerrainTileDelta& delta : repair.deltas)
        if(delta.globalX == 8 && delta.globalY == 8)
        {
            repairedStamp = true;
            QCOMPARE(delta.before, quint16(4));
            QCOMPARE(delta.after, quint16(1));
        }
    QVERIFY(repairedStamp);
    QVERIFY(model.apply(repair, true, &report));
    QCOMPARE(model.packedTile(8, 8, ETerrainTileLayer::Land), quint16(1));
}

void TerrainVirtualAtlasTests::recoveredMapResolutionRepairsDecodedWrongBlend()
{
    Fixture fixture;
    STerrainTransitionVariant vertical;
    vertical.signature = signature(1, 2, 2, 1);
    vertical.packedTile = 4;
    QVERIFY(fixture.catalog.addVariant(vertical));

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));

    CTerrainVirtualMaterialField recoveredField;
    QVERIFY(recoveredField.resize(model.tileCountX(), model.tileCountY()));
    for(int y = 0; y <= recoveredField.tileHeight(); ++y)
        for(int x = 0; x <= recoveredField.tileWidth(); ++x)
            recoveredField.setMaterial(x, y, x <= 8 ? 1 : 2);

    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x52455052u, &report,
        false, &recoveredField, true, true);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY2(!repair.isEmpty(),
             "Recovered map repair skipped a decoded uniform where a blend was required");
    bool repairedCell = false;
    for(const STerrainTileDelta& delta : repair.deltas)
        if(delta.globalX == 8 && delta.after == 4)
            repairedCell = true;
    QVERIFY(repairedCell);
}

void TerrainVirtualAtlasTests::recoveredMapResolutionExcludesTheCellBeingJudged()
{
    Fixture fixture;
    STerrainTransitionVariant expectedBoundary;
    expectedBoundary.signature = signature(1, 2, 2, 1);
    expectedBoundary.packedTile = 4;
    QVERIFY(fixture.catalog.addVariant(expectedBoundary));
    STerrainTransitionVariant wrongBoundary;
    wrongBoundary.signature = signature(1, 1, 2, 2);
    wrongBoundary.packedTile = 5;
    QVERIFY(fixture.catalog.addVariant(wrongBoundary));

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));

    STerrainTileCommand boundary;
    for(int y = 0; y < 16; ++y)
        for(int x = 8; x < 16; ++x)
        {
            STerrainTileDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.layer = ETerrainTileLayer::Land;
            delta.before = 0;
            delta.after = x == 8 ? 4 : 1;
            boundary.deltas.append(delta);
        }
    QVERIFY(model.apply(boundary, true, &report));

    STerrainTileCommand damage;
    STerrainTileDelta wrong;
    wrong.globalX = 8;
    wrong.globalY = 8;
    wrong.layer = ETerrainTileLayer::Land;
    wrong.before = 4;
    wrong.after = 5;
    damage.deltas.append(wrong);
    QVERIFY(model.apply(damage, true, &report));

    CTerrainVirtualMaterialField recovered;
    QVERIFY(model.recoverUnambiguousVirtualMaterialField(
        ETerrainTileLayer::Land, fixture.catalog, &recovered, &report));
    bool contaminatedConsensusFound = true;
    recovered.cellSignature(8, 8, &contaminatedConsensusFound);
    QVERIFY2(!contaminatedConsensusFound,
             "The damaged cell should poison the old all-cell consensus fixture");

    STerrainTransitionSignature inferred;
    QVERIFY(model.inferCellSignatureFromNeighbors(
        8, 8, ETerrainTileLayer::Land, fixture.catalog, &inferred));
    QCOMPARE(inferred, expectedBoundary.signature);

    CValidationReport repairReport;
    const STerrainTileCommand repair = model.makeVirtualMapResolution(
        ETerrainTileLayer::Land, fixture.catalog, 0x4c4f4f55u,
        &repairReport, false, &recovered, true, false);
    QVERIFY2(!repairReport.hasErrors(), qPrintable(repairReport.toText()));
    QCOMPARE(repair.deltas.size(), 1);
    QCOMPARE(repair.deltas.front().globalX, 8);
    QCOMPARE(repair.deltas.front().globalY, 8);
    QCOMPARE(repair.deltas.front().before, quint16(5));
    QCOMPARE(repair.deltas.front().after, quint16(4));
}

void TerrainVirtualAtlasTests::classicAutoStrokeRefusesMissingBlendAndRollsBackExactly()
{
    Fixture fixture;
    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;

    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(64, 1), 0));
    QVERIFY2(model.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, fixture.catalog, &report),
             qPrintable(report.toText()));
    CTerrainVirtualMaterialField baselineField;
    QVERIFY(model.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &baselineField));
    bool baselinePackedAvailable = false;
    const quint16 baselinePacked = model.packedTile(
        8, 8, ETerrainTileLayer::Land, &baselinePackedAvailable);
    QVERIFY(baselinePackedAvailable);

    STerrainTileBrush brush;
    brush.centerX = 8;
    brush.centerY = 8;
    brush.radius = 1;
    const STerrainTileCommand preview = model.makeVirtualMaterialPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2,
        fixture.catalog, 0x434C4153u, &report);
    QVERIFY2(!preview.isEmpty(), qPrintable(report.toText()));
    QVERIFY(model.apply(preview, true, &report));

    CValidationReport releaseReport;
    const STerrainTileCommand resolution = model.makeVirtualStrokeResolution(
        preview, fixture.catalog, 0x434C4153u, &releaseReport);
    QVERIFY(releaseReport.hasErrors());
    QVERIFY(releaseReport.toText().contains(
        QStringLiteral("terrain.virtual_release_unresolved")));

    // Classic Auto never asks the modern resolver to synthesize the missing
    // tuple. Its whole-stroke rollback restores both the semantic field and
    // the exact packed preview values.
    QVERIFY(model.apply(preview, false, &report));
    CTerrainVirtualMaterialField restoredField;
    QVERIFY(model.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &restoredField));
    QCOMPARE(restoredField.stableSha256(), baselineField.stableSha256());
    bool restoredPackedAvailable = false;
    QCOMPARE(model.packedTile(8, 8, ETerrainTileLayer::Land,
                              &restoredPackedAvailable), baselinePacked);
    QVERIFY(restoredPackedAvailable);
    QVERIFY(resolution.deltas.isEmpty());
}

void TerrainVirtualAtlasTests::classicSemanticCommandsEstablishVirtualField()
{
    const auto sourceForPacked = [](quint16 packed) {
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint32(0xCF4BF774) << quint8(0);
        payload.append(QByteArray(33 * 33 * 8, '\0'));
        QByteArray tiles(16 * 16 * 2, '\0');
        for(int cell = 0; cell < 16 * 16; ++cell)
            qToLittleEndian<quint16>(packed,
                reinterpret_cast<uchar*>(tiles.data() + cell * 2));
        payload.append(tiles);
        STerrainSectorSource source;
        source.memberName = QStringLiteral("CaveSet000000.SEC");
        source.sectorX = 0;
        source.sectorY = 0;
        source.type = 0;
        source.payload = payload;
        source.landTileOffset = 5 + 33 * 33 * 8;
        return source;
    };

    // A complete binary 1/2 catalog lets each Classic entry point resolve
    // exact physical tiles. The independent semantic field must nevertheless
    // be initialized and changed through the same reversible command.
    CTerrainTransitionCatalog catalog;
    for(int bits = 0; bits < 16; ++bits)
    {
        const auto material = [bits](int corner) {
            return (bits & (1 << corner)) ? qint32(2) : qint32(1);
        };
        STerrainTransitionVariant variant;
        variant.signature = signature(
            material(0), material(1), material(2), material(3));
        SMprPackedTile packed;
        packed.tileIndex = quint8(bits);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 0.0f;
    brush.shape = ETerrainTileBrushShape::Square;

    CTerrainEditModel tileModel;
    CValidationReport tileReport;
    QVERIFY(tileModel.loadSources(1, 1, 100.0f, {sourceForPacked(0)},
                                  &tileReport, 1, QVector<qint32>(64, 1), 0));
    const STerrainTileCommand tile = tileModel.makeSemanticTilePaint(
        brush, ETerrainTileLayer::Land, 2, catalog, 0x54494c45u, &tileReport);
    QVERIFY2(!tileReport.hasErrors(), qPrintable(tileReport.toText()));
    QCOMPARE(tile.virtualVertices.size(), 4);
    // Construction establishes a deterministic pre-command baseline, but the
    // selected identity exists only in the reversible command until Apply.
    bool beforeApplyFound = false;
    QCOMPARE(tileModel.virtualMaterialAt(
                 ETerrainTileLayer::Land, 8, 8, &beforeApplyFound), qint32(1));
    QVERIFY(beforeApplyFound);
    QVERIFY(tileModel.apply(tile, true, &tileReport));
    for(const QPoint& vertex : {QPoint(8, 8), QPoint(9, 8),
                                QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(tileModel.virtualMaterialAt(
                     ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(2));
        QVERIFY(found);
    }
    QVERIFY(tileModel.apply(tile, false, &tileReport));
    bool afterCancelFound = false;
    QCOMPARE(tileModel.virtualMaterialAt(
                 ETerrainTileLayer::Land, 8, 8, &afterCancelFound), qint32(1));
    QVERIFY(afterCancelFound);

    CTerrainEditModel strokeModel;
    CValidationReport strokeReport;
    QVERIFY(strokeModel.loadSources(1, 1, 100.0f, {sourceForPacked(0)},
                                    &strokeReport, 1, QVector<qint32>(64, 1), 0));
    const STerrainTileCommand stroke = strokeModel.makeSemanticVertexStrokePaint(
        {QPoint(8, 8)}, {QPoint(8, 8)}, ETerrainTileLayer::Land, 2,
        catalog, 0x5354524fu, &strokeReport);
    QVERIFY2(!strokeReport.hasErrors(), qPrintable(strokeReport.toText()));
    QCOMPARE(stroke.virtualVertices.size(), 1);
    QCOMPARE(stroke.virtualVertices.front().globalX, 8);
    QCOMPARE(stroke.virtualVertices.front().globalY, 8);
    QVERIFY(strokeModel.apply(stroke, true, &strokeReport));
    bool strokeFound = false;
    QCOMPARE(strokeModel.virtualMaterialAt(
                 ETerrainTileLayer::Land, 8, 8, &strokeFound), qint32(2));
    QVERIFY(strokeFound);

    // The corner convenience entry point delegates to the vertex resolver;
    // keep its semantic field behavior covered independently of the direct
    // vertex call used by contextual palette cards.
    CTerrainEditModel cornerModel;
    CValidationReport cornerReport;
    QVERIFY(cornerModel.loadSources(1, 1, 100.0f, {sourceForPacked(0)},
                                    &cornerReport, 1, QVector<qint32>(64, 1), 0));
    const STerrainTileCommand corner = cornerModel.makeSemanticCornerMaterialPaint(
        8, 8, 8, 8, ETerrainTileLayer::Land, 2, catalog,
        0x434f524eu, &cornerReport);
    QVERIFY2(!cornerReport.hasErrors(), qPrintable(cornerReport.toText()));
    QCOMPARE(corner.virtualVertices.size(), 1);
    QVERIFY(cornerModel.apply(corner, true, &cornerReport));
    bool cornerFound = false;
    QCOMPARE(cornerModel.virtualMaterialAt(
                 ETerrainTileLayer::Land, 8, 8, &cornerFound), qint32(2));
    QVERIFY(cornerFound);
}

void TerrainVirtualAtlasTests::contextualClassicVirtualFieldTracksAcceptedVertices()
{
    const auto sourceForPacked = [](quint16 packed) {
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint32(0xCF4BF774) << quint8(0);
        payload.append(QByteArray(33 * 33 * 8, '\0'));
        QByteArray tiles(16 * 16 * 2, '\0');
        for(int cell = 0; cell < 16 * 16; ++cell)
            qToLittleEndian<quint16>(packed,
                reinterpret_cast<uchar*>(tiles.data() + cell * 2));
        payload.append(tiles);
        STerrainSectorSource source;
        source.memberName = QStringLiteral("CaveSet000000.SEC");
        source.sectorX = 0;
        source.sectorY = 0;
        source.type = 0;
        source.payload = payload;
        source.landTileOffset = 5 + 33 * 33 * 8;
        return source;
    };

    CTerrainTransitionCatalog catalog;
    for(int bits = 0; bits < 16; ++bits)
    {
        const auto material = [bits](int corner) {
            return (bits & (1 << corner)) ? qint32(2) : qint32(3);
        };
        STerrainTransitionVariant variant;
        variant.signature = signature(
            material(0), material(1), material(2), material(3));
        SMprPackedTile packed;
        packed.tileIndex = quint8(bits);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    STerrainTransitionVariant incompatibleBackground;
    incompatibleBackground.signature = signature(1, 1, 1, 1);
    SMprPackedTile incompatiblePacked;
    incompatiblePacked.tileIndex = 20;
    incompatibleBackground.packedTile = incompatiblePacked.encode();
    QVERIFY(catalog.addVariant(incompatibleBackground));

    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 1.0f;
    brush.shape = ETerrainTileBrushShape::Circle;

    // With safe mode off, unrelated terrain still accepts the selected
    // contextual anchor. Missing multi-corner combinations must not smuggle
    // the skipped radius vertices into the virtual field.
    CTerrainEditModel incompatible;
    CValidationReport incompatibleReport;
    QVERIFY(incompatible.loadSources(1, 1, 100.0f,
                                     {sourceForPacked(incompatiblePacked.encode())},
                                     &incompatibleReport, 1,
                                     QVector<qint32>(64, 1), 0));
    const STerrainTileCommand anchorOnly = incompatible.makeSemanticVertexPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2, catalog, 0x414e4348u,
        &incompatibleReport, 3, ETerrainSemanticPaintPolicy::AllowLocalHardEdges);
    QVERIFY2(!incompatibleReport.hasErrors(),
             qPrintable(incompatibleReport.toText()));
    QSet<QPair<int, int>> acceptedAnchor;
    for(const QPoint& point : anchorOnly.semanticAudit.paintedVertices)
        acceptedAnchor.insert(qMakePair(point.x(), point.y()));
    QSet<QPair<int, int>> expectedAnchor;
    expectedAnchor.insert(qMakePair(8, 8));
    QCOMPARE(acceptedAnchor, expectedAnchor);
    QSet<QPair<int, int>> virtualAnchor;
    for(const auto& delta : anchorOnly.virtualVertices)
        virtualAnchor.insert(qMakePair(delta.globalX, delta.globalY));
    QCOMPARE(virtualAnchor, expectedAnchor);
    QVERIFY(incompatible.apply(anchorOnly, true, &incompatibleReport));
    bool anchorFound = false;
    QCOMPARE(incompatible.virtualMaterialAt(
                 ETerrainTileLayer::Land, 8, 8, &anchorFound), qint32(2));
    QVERIFY(anchorFound);

    // On the card's authored support, the same contextual brush may retain
    // every exact connected stamp. Their accepted semantic vertices, not the
    // raw brush footprint, are the virtual-field transaction.
    CTerrainEditModel compatible;
    CValidationReport compatibleReport;
    QVERIFY(compatible.loadSources(1, 1, 100.0f, {sourceForPacked(0)},
                                   &compatibleReport, 1,
                                   QVector<qint32>(64, 1), 0));
    const STerrainTileCommand connected = compatible.makeSemanticVertexPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2, catalog, 0x434f4e4eu,
        &compatibleReport, 3, ETerrainSemanticPaintPolicy::AllowLocalHardEdges);
    QVERIFY2(!compatibleReport.hasErrors(), qPrintable(compatibleReport.toText()));
    QSet<QPair<int, int>> expectedConnected;
    expectedConnected << qMakePair(8, 8) << qMakePair(8, 7)
                      << qMakePair(7, 8) << qMakePair(9, 8)
                      << qMakePair(8, 9);
    QSet<QPair<int, int>> acceptedConnected;
    for(const QPoint& point : connected.semanticAudit.paintedVertices)
        acceptedConnected.insert(qMakePair(point.x(), point.y()));
    QCOMPARE(acceptedConnected, expectedConnected);
    QSet<QPair<int, int>> virtualConnected;
    for(const auto& delta : connected.virtualVertices)
        virtualConnected.insert(qMakePair(delta.globalX, delta.globalY));
    QCOMPARE(virtualConnected, expectedConnected);
    QVERIFY(compatible.apply(connected, true, &compatibleReport));
    for(const QPair<int, int>& point : expectedConnected)
    {
        bool found = false;
        QCOMPARE(compatible.virtualMaterialAt(
                     ETerrainTileLayer::Land, point.first, point.second, &found),
                 qint32(2));
        QVERIFY(found);
    }
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

    // Saving an Overlay-only Modern edit must not require the deliberately
    // sparse Overlay field to become map-wide.  Rebuild the complete Ground
    // field, pin every active Overlay reference, and prove that publication
    // preserves both the painted water and inactive-overlay sentinels.
    QVERIFY2(model.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, fixture.catalog, &report,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(report.toText()));
    CTerrainVirtualMaterialField landField;
    QVERIFY(model.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &landField, true));
    QVector<quint16> pinnedOverlay;
    for(int y = 0; y < model.tileCountY(); ++y)
        for(int x = 0; x < model.tileCountX(); ++x)
        {
            bool available = false;
            const quint16 packed = model.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available);
            if(available && packed != 0xffffu)
                pinnedOverlay.append(packed);
        }
    QVERIFY(!pinnedOverlay.isEmpty());

    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const STerrainAtlasFinalization finalized = atlas.finalize(
        landField, "SparseOverlaySave", 512, {1, 2, 3, 4}, &report,
        pinnedOverlay);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n")
                        + report.toText()));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            document, model, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n")
                        + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
                                "sparse-overlay-result.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel published;
    QVERIFY2(published.load(reopened, &report), qPrintable(report.toText()));
    bool available = false;
    const quint16 painted = published.packedTile(
        8, 8, ETerrainTileLayer::Overlay, &available);
    QVERIFY(available);
    bool found = false;
    QCOMPARE(finalizedSignature(finalized, painted, &found),
             signature(2, 2, 2, 2));
    QVERIFY(found);
    published.packedTile(0, 0, ETerrainTileLayer::Overlay, &available);
    QVERIFY(!available);
}

void TerrainVirtualAtlasTests::collapsedUniformBaselineRecoversExactRotatedBlend()
{
    CTerrainTransitionCatalog catalog;
    const auto add = [&catalog](const STerrainTransitionSignature& value,
                                quint16 packed) {
        STerrainTransitionVariant variant;
        variant.signature = value;
        variant.packedTile = packed;
        return catalog.addVariant(variant);
    };
    QVERIFY(add(signature(1, 1, 1, 1), 0));
    QVERIFY(add(signature(2, 2, 2, 2), 1));
    const STerrainTransitionSignature expected[4] = {
        signature(2, 1, 1, 1), signature(1, 2, 1, 1),
        signature(1, 1, 2, 1), signature(1, 1, 1, 2)};
    for(int corner = 0; corner < 4; ++corner)
    {
        SMprPackedTile packed;
        packed.tileIndex = 2;
        packed.rotation = quint8(corner);
        QVERIFY(add(expected[corner], packed.encode()));
    }

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = "CaveSet000000.SEC";
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;
    const QPoint requestedVertex[4] = {
        QPoint(8, 8), QPoint(9, 8), QPoint(9, 9), QPoint(8, 9)};

    for(int corner = 0; corner < 4; ++corner)
    {
        CTerrainEditModel model;
        CValidationReport report;
        QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                                  QVector<qint32>(16, 1), 0));
        STerrainTileCommand collapsed;
        STerrainTileDelta delta;
        delta.globalX = 8;
        delta.globalY = 8;
        delta.layer = ETerrainTileLayer::Land;
        delta.before = 0;
        delta.after = 1;
        collapsed.deltas.append(delta);
        QVERIFY(model.apply(collapsed, true, &report));

        STerrainTileBrush brush;
        brush.centerX = requestedVertex[corner].x();
        brush.centerY = requestedVertex[corner].y();
        brush.radius = 0.0f;
        const quint32 seed = 0x434F4E53u + quint32(corner);
        const STerrainTileCommand preview = model.makeVirtualMaterialPaint(
            8, 8, brush, ETerrainTileLayer::Land, 2, catalog,
            seed, &report, false);
        QVERIFY2(!preview.isEmpty(), qPrintable(report.toText()));
        QVERIFY(model.apply(preview, true, &report));
        const STerrainTileCommand resolution = model.makeVirtualStrokeResolution(
            preview, catalog, seed, &report, false);
        QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
        QVERIFY(model.apply(resolution, true, &report));

        bool available = false;
        const quint16 packed = model.packedTile(
            8, 8, ETerrainTileLayer::Land, &available);
        QVERIFY(available);
        bool decoded = false;
        QCOMPARE(catalog.gridSignatureForPacked(packed, &decoded),
                 expected[corner]);
        QVERIFY(decoded);
        QVERIFY(!(expected[corner] == signature(2, 2, 2, 2)));
    }
}

void TerrainVirtualAtlasTests::overlappingVirtualPaintDoesNotReplaceUnchangedEdgeBlendWithUniformPreview()
{
    CTerrainTransitionCatalog catalog;
    for(int bits = 0; bits < 16; ++bits)
    {
        const auto material = [bits](int corner) {
            return (bits & (1 << corner)) ? qint32(2) : qint32(1);
        };
        STerrainTransitionVariant variant;
        variant.signature = signature(
            material(0), material(1), material(2), material(3));
        SMprPackedTile packed;
        packed.tileIndex = quint8(bits);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    QByteArray payload;
    QDataStream sec(&payload, QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);
    sec << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;

    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 1,
                              QVector<qint32>(16, 1), 0));

    STerrainTileBrush firstBrush;
    firstBrush.centerX = 8.0f;
    firstBrush.centerY = 8.0f;
    firstBrush.radius = 1.0f;
    firstBrush.shape = ETerrainTileBrushShape::Circle;
    const STerrainTileCommand firstPreview = model.makeVirtualMaterialPaint(
        8, 8, firstBrush, ETerrainTileLayer::Land, 2,
        catalog, 0x11112222u, &report, false);
    QVERIFY2(!firstPreview.isEmpty(), qPrintable(report.toText()));
    QVERIFY(model.apply(firstPreview, true, &report));
    const STerrainTileCommand firstResolution = model.makeVirtualStrokeResolution(
        firstPreview, catalog, 0x11112222u, &report, false);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(model.apply(firstResolution, true, &report));

    CTerrainVirtualMaterialField beforeOverlap;
    QVERIFY(model.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &beforeOverlap));
    bool expectedFound = false;
    const STerrainTransitionSignature expected =
        beforeOverlap.cellSignature(7, 7, &expectedFound);
    QVERIFY(expectedFound);
    QVERIFY(!(expected == signature(2, 2, 2, 2)));

    // This overlapping stamp changes vertices to the east, but cell 7,7 is
    // included only because an already-painted vertex lies inside the brush.
    // Its semantic tuple does not change and must never receive the uniform
    // live-preview tile that release resolution intentionally ignores.
    STerrainTileBrush overlapBrush = firstBrush;
    overlapBrush.centerX = 9.0f;
    const STerrainTileCommand overlapPreview = model.makeVirtualMaterialPaint(
        9, 8, overlapBrush, ETerrainTileLayer::Land, 2,
        catalog, 0x33334444u, &report, false);
    QVERIFY2(!overlapPreview.isEmpty(), qPrintable(report.toText()));
    QVERIFY(!overlapPreview.virtualVertices.isEmpty());
    QVERIFY(model.apply(overlapPreview, true, &report));

    bool available = false;
    quint16 targetPacked = model.packedTile(
        7, 7, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    bool decoded = false;
    QCOMPARE(catalog.gridSignatureForPacked(targetPacked, &decoded), expected);
    QVERIFY(decoded);

    const STerrainTileCommand overlapResolution = model.makeVirtualStrokeResolution(
        overlapPreview, catalog, 0x33334444u, &report, false);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(model.apply(overlapResolution, true, &report));
    targetPacked = model.packedTile(
        7, 7, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    QCOMPARE(catalog.gridSignatureForPacked(targetPacked, &decoded), expected);
    QVERIFY(decoded);
}

void TerrainVirtualAtlasTests::uniformStrokeDistributesPureVariantsAndRotationsDeterministically()
{
    Fixture fixture;
    int nextSlot = 4;
    // Give material 2 a second legitimate uniform variation, then make every
    // binary edge/corner tuple exact so release is testing distribution rather
    // than generation availability.
    const int secondUniformSlot = nextSlot++;
    fixture.images.append(solid(qRgba(75, 145, 215, 255)));
    for(int slot : {1, secondUniformSlot})
        for(int rotation = slot == 1 ? 1 : 0; rotation < 4; ++rotation)
        {
            STerrainTransitionVariant uniform;
            uniform.signature = signature(2, 2, 2, 2);
            SMprPackedTile packed;
            packed.tileIndex = quint8(slot);
            packed.rotation = quint8(rotation);
            uniform.packedTile = packed.encode();
            QVERIFY(fixture.catalog.addVariant(uniform));
        }
    for(int bits = 1; bits < 15; ++bits)
    {
        const auto value = [bits](int bit) { return (bits & (1 << bit)) ? 2 : 1; };
        STerrainTransitionVariant variant;
        variant.signature = signature(value(0), value(1), value(2), value(3));
        SMprPackedTile packed;
        packed.tileIndex = quint8(nextSlot++);
        variant.packedTile = packed.encode();
        fixture.catalog.addVariant(variant);
        fixture.images.append(solid(qRgba(
            30 + (bits * 11) % 190,
            45 + (bits * 17) % 180,
            60 + (bits * 23) % 170, 255)));
    }
    const int decodeOnlyUniformSlot = nextSlot++;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        STerrainTransitionVariant excluded;
        excluded.signature = signature(2, 2, 2, 2);
        SMprPackedTile packed;
        packed.tileIndex = quint8(decodeOnlyUniformSlot);
        packed.rotation = quint8(rotation);
        excluded.packedTile = packed.encode();
        excluded.paintSelectable = false;
        QVERIFY(fixture.catalog.addVariant(excluded));
    }
    QMap<int, int> balancedSlots;
    QSet<int> balancedRotations;
    for(int y = 0; y < 32; ++y)
        for(int x = 0; x < 32; ++x)
        {
            bool found = false;
            const SMprPackedTile selected = SMprPackedTile::decode(
                fixture.catalog.selectForPaint(
                    signature(2, 2, 2, 2), x, y, 0x11223344u, &found));
            QVERIFY(found);
            QVERIFY(selected.atlasSlot() != decodeOnlyUniformSlot);
            ++balancedSlots[selected.atlasSlot()];
            balancedRotations.insert(selected.rotation);
        }
    QCOMPARE(balancedSlots.size(), 2);
    QVERIFY(qAbs(balancedSlots.value(1)
                 - balancedSlots.value(secondUniformSlot)) <= 32);
    QCOMPARE(balancedRotations.size(), 4);
    bool decodeOnlyFound = false;
    SMprPackedTile decodeOnlyPacked;
    decodeOnlyPacked.tileIndex = quint8(decodeOnlyUniformSlot);
    QCOMPARE(fixture.catalog.gridSignatureForPacked(
                 decodeOnlyPacked.encode(), &decodeOnlyFound),
             signature(2, 2, 2, 2));
    QVERIFY(decodeOnlyFound);

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
    const auto packedField = [](const CTerrainEditModel& editModel) {
        QVector<quint16> values;
        values.reserve(16 * 16);
        for(int y = 0; y < 16; ++y)
            for(int x = 0; x < 16; ++x)
            {
                bool available = false;
                values.append(editModel.packedTile(
                    x, y, ETerrainTileLayer::Land, &available));
                if(!available)
                    return QVector<quint16>();
            }
        return values;
    };
    const QVector<quint16> initialPacked = packedField(model);
    QCOMPARE(initialPacked.size(), 16 * 16);
    STerrainTileBrush brush;
    brush.centerX = 8; brush.centerY = 8; brush.radius = 4;
    const quint32 strokeSeed = 0x98761234u;
    const STerrainTileCommand preview = model.makeVirtualMaterialPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2,
        fixture.catalog, strokeSeed, &report, true);
    QVERIFY2(!preview.isEmpty(), qPrintable(report.toText()));
    QVERIFY(model.apply(preview, true, &report));
    const QVector<quint16> previewPacked = packedField(model);
    const STerrainTileCommand resolution = model.makeVirtualStrokeResolution(
        preview, fixture.catalog, strokeSeed, &report, true);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(model.apply(resolution, true, &report));
    const QVector<quint16> finalPacked = packedField(model);
    QVERIFY(model.virtualMaterialCellChanged(
        ETerrainTileLayer::Land, 8, 8));
    QVERIFY(!model.virtualMaterialCellChanged(
        ETerrainTileLayer::Land, 0, 0));

    CTerrainVirtualMaterialField field;
    QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land, &field));
    QSet<int> interiorSlots;
    QSet<int> interiorRotations;
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool found = false;
            const STerrainTransitionSignature exact = field.cellSignature(x, y, &found);
            if(!found || !(exact == signature(2, 2, 2, 2))) continue;
            bool available = false;
            const quint16 value = model.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            const SMprPackedTile packed = SMprPackedTile::decode(value);
            interiorSlots.insert(packed.atlasSlot());
            interiorRotations.insert(packed.rotation);
            bool decoded = false;
            QCOMPARE(fixture.catalog.gridSignatureForPacked(value, &decoded),
                     signature(2, 2, 2, 2));
            QVERIFY(decoded);
        }
    QVERIFY(interiorSlots.size() > 1);
    QVERIFY(interiorRotations.size() > 1);

    // Command history must restore the exact packed field, including the
    // chosen physical uniform variation and its quarter-turn.
    QVERIFY(model.apply(resolution, false, &report));
    QCOMPARE(packedField(model), previewPacked);
    QVERIFY(model.apply(preview, false, &report));
    QCOMPARE(packedField(model), initialPacked);
    QVERIFY(model.apply(preview, true, &report));
    QVERIFY(model.apply(resolution, true, &report));
    QCOMPARE(packedField(model), finalPacked);

    // The same source, coordinates and seed reproduce the complete packed
    // field. Disabling rotation variation may affect uniform rotations only;
    // exact edge/blend selections must remain byte-identical.
    CTerrainEditModel repeated;
    CValidationReport repeatedReport;
    QVERIFY(repeated.loadSources(1, 1, 100.0f, {source}, &repeatedReport, 1,
                                 QVector<qint32>(64, 1), 0));
    const STerrainTileCommand repeatedPreview = repeated.makeVirtualMaterialPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2,
        fixture.catalog, strokeSeed, &repeatedReport, true);
    QVERIFY(repeated.apply(repeatedPreview, true, &repeatedReport));
    const STerrainTileCommand repeatedResolution = repeated.makeVirtualStrokeResolution(
        repeatedPreview, fixture.catalog, strokeSeed, &repeatedReport, true);
    QVERIFY(repeated.apply(repeatedResolution, true, &repeatedReport));
    QVERIFY2(!repeatedReport.hasErrors(), qPrintable(repeatedReport.toText()));
    QCOMPARE(packedField(repeated), finalPacked);

    CTerrainEditModel fixedRotation;
    CValidationReport fixedReport;
    QVERIFY(fixedRotation.loadSources(1, 1, 100.0f, {source}, &fixedReport, 1,
                                     QVector<qint32>(64, 1), 0));
    const STerrainTileCommand fixedPreview = fixedRotation.makeVirtualMaterialPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2,
        fixture.catalog, strokeSeed, &fixedReport, false);
    QVERIFY(fixedRotation.apply(fixedPreview, true, &fixedReport));
    const STerrainTileCommand fixedResolution = fixedRotation.makeVirtualStrokeResolution(
        fixedPreview, fixture.catalog, strokeSeed, &fixedReport, false);
    QVERIFY(fixedRotation.apply(fixedResolution, true, &fixedReport));
    QVERIFY2(!fixedReport.hasErrors(), qPrintable(fixedReport.toText()));
    const QVector<quint16> fixedPacked = packedField(fixedRotation);
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool found = false;
            const STerrainTransitionSignature exact = field.cellSignature(x, y, &found);
            if(found && !(exact == signature(2, 2, 2, 2)))
                QCOMPARE(finalPacked.at(y * 16 + x), fixedPacked.at(y * 16 + x));
        }

    // A later user-initiated stroke over the same semantic material is a
    // deliberate reroll. It captures new exact packed values without changing
    // the virtual corner field; undo/redo replays those values byte-for-byte.
    const quint32 rerollSeed = 0x13572468u;
    const STerrainTileCommand reroll = model.makeVirtualMaterialPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2,
        fixture.catalog, rerollSeed, &report, true);
    QVERIFY(reroll.virtualVertices.isEmpty());
    QVERIFY2(!reroll.deltas.isEmpty(), qPrintable(report.toText()));
    QVERIFY(model.apply(reroll, true, &report));
    const QVector<quint16> rerolledPacked = packedField(model);
    QVERIFY(rerolledPacked != finalPacked);
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool available = false;
            const quint16 value = model.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            bool decoded = false;
            QCOMPARE(fixture.catalog.gridSignatureForPacked(value, &decoded),
                     field.cellSignature(x, y));
            QVERIFY(decoded);
        }
    QVERIFY(model.apply(reroll, false, &report));
    QCOMPARE(packedField(model), finalPacked);
    QVERIFY(model.apply(reroll, true, &report));
    QCOMPARE(packedField(model), rerolledPacked);

    // Final atlas compaction must preserve the chosen physical artwork and
    // rotation, even though output slot numbers are remapped densely.
    CTestTerrainVirtualAtlas preservingAtlas(
        fixture.catalog, fixture.images, 0x24681357u);
    const STerrainAtlasFinalization preserved = preservingAtlas.finalize(
        field, QStringLiteral("UniformReroll"), 64, {}, &report, {}, {},
        rerolledPacked);
    QVERIFY2(preserved.success,
             qPrintable(preserved.error + QStringLiteral("\n") + report.toText()));
    QCOMPARE(preserved.packedCells.size(), rerolledPacked.size());
    for(int index = 0; index < rerolledPacked.size(); ++index)
    {
        const SMprPackedTile before = SMprPackedTile::decode(
            rerolledPacked.at(index));
        const SMprPackedTile after = SMprPackedTile::decode(
            preserved.packedCells.at(index));
        QCOMPARE(after.rotation, before.rotation);
        QCOMPARE(preserved.tiles.at(after.atlasSlot()).sourceSlot,
                 before.atlasSlot());
    }

    // Reopening the serialized sector payload preserves every packed bit.
    STerrainSectorSource reopenedSource = source;
    reopenedSource.payload = model.sectorPayload(0, 0);
    QVERIFY(!reopenedSource.payload.isEmpty());
    CTerrainEditModel reopened;
    CValidationReport reopenReport;
    QVERIFY(reopened.loadSources(1, 1, 100.0f, {reopenedSource}, &reopenReport, 1,
                                 QVector<qint32>(64, 1), 0));
    QVERIFY2(!reopenReport.hasErrors(), qPrintable(reopenReport.toText()));
    QCOMPARE(packedField(reopened), rerolledPacked);
    QCOMPARE(reopened.sectorPayload(0, 0), reopenedSource.payload);
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

void TerrainVirtualAtlasTests::combinedLandAndOverlayPublicationPreservesBothExactFields()
{
    Fixture fixture;
    CValidationReport report;
    SMprPackedTile sourceLand;
    sourceLand.tileIndex = 0;
    SMprPackedTile sourceOverlay;
    sourceOverlay.tileIndex = 3;
    CMprDocument source;
    QVERIFY2(source.loadBytes(portableMprWithActiveOverlay(
                 sourceLand.encode(), sourceOverlay.encode()),
                 "combined-source.mpr", &report), qPrintable(report.toText()));

    CTerrainEditModel edited;
    QVERIFY2(edited.load(source, &report), qPrintable(report.toText()));
    SMprPackedTile paintedLand;
    paintedLand.tileIndex = 1;
    SMprPackedTile paintedOverlay;
    paintedOverlay.tileIndex = 2;
    STerrainTileCommand command;
    STerrainTileDelta landDelta;
    landDelta.globalX = 0;
    landDelta.globalY = 0;
    landDelta.layer = ETerrainTileLayer::Land;
    landDelta.before = sourceLand.encode();
    landDelta.after = paintedLand.encode();
    command.deltas.append(landDelta);
    STerrainTileDelta overlayDelta;
    overlayDelta.globalX = 0;
    overlayDelta.globalY = 0;
    overlayDelta.layer = ETerrainTileLayer::Overlay;
    overlayDelta.before = sourceOverlay.encode();
    overlayDelta.after = paintedOverlay.encode();
    command.deltas.append(overlayDelta);
    QVERIFY2(edited.apply(command, true, &report), qPrintable(report.toText()));

    CTerrainVirtualMaterialField landField;
    QVERIFY(landField.resize(16, 16, 2));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const STerrainAtlasFinalization finalized = atlas.finalize(
        landField, "CombinedLayers", 512, {1, 2, 3, 4}, &report,
        {paintedOverlay.encode(), sourceOverlay.encode()});
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));

    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            source, edited, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n") + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes, "combined-result.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel result;
    QVERIFY2(result.load(reopened, &report), qPrintable(report.toText()));

    bool available = false;
    const quint16 land = result.packedTile(
        0, 0, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    bool found = false;
    QCOMPARE(finalizedSignature(finalized, land, &found), signature(2, 2, 2, 2));
    QVERIFY(found);
    const quint16 overlay = result.packedTile(
        0, 0, ETerrainTileLayer::Overlay, &available);
    QVERIFY(available);
    QCOMPARE(finalizedSignature(finalized, overlay, &found), signature(3, 3, 3, 3));
    QVERIFY(found);
    const quint16 untouchedOverlay = result.packedTile(
        1, 0, ETerrainTileLayer::Overlay, &available);
    QVERIFY(available);
    QCOMPARE(finalizedSignature(finalized, untouchedOverlay, &found),
             signature(4, 4, 4, 4));
    QVERIFY(found);
}

void TerrainVirtualAtlasTests::reopenedDraftCanInitializeAndPublishWithoutNewPaint()
{
    Fixture fixture;
    CValidationReport report;
    SMprPackedTile landSource;
    landSource.tileIndex = 0;
    SMprPackedTile overlaySource;
    overlaySource.tileIndex = 3;
    CMprDocument reopenedDraft;
    QVERIFY2(reopenedDraft.loadBytes(portableMprWithActiveOverlay(
                 landSource.encode(), overlaySource.encode()),
                 "reopened-working-draft.mpr", &report),
             qPrintable(report.toText()));

    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(reopenedDraft, &report), qPrintable(report.toText()));
    QVERIFY(!terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land));
    QVERIFY2(terrain.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, fixture.catalog, &report),
             qPrintable(report.toText()));
    CTerrainVirtualMaterialField field;
    QVERIFY(terrain.copyVirtualMaterialField(ETerrainTileLayer::Land, &field));

    QVector<quint16> pinnedOverlay;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const quint16 packed = terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available);
            if(available && packed != 0xffffu
               && !pinnedOverlay.contains(packed))
                pinnedOverlay.append(packed);
        }
    QVERIFY(!pinnedOverlay.isEmpty());

    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images);
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, "ReopenedDraft", 512, {1, 2, 3, 4}, &report,
        pinnedOverlay);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n")
                        + report.toText()));
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            reopenedDraft, terrain, finalized, ETerrainTileLayer::Land,
            &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n")
                        + report.toText()));

    CMprDocument published;
    QVERIFY2(published.loadBytes(package.mprBytes, "published-draft.mpr",
                                 &report), qPrintable(report.toText()));
    CTerrainEditModel publishedTerrain;
    QVERIFY2(publishedTerrain.load(published, &report),
             qPrintable(report.toText()));
    bool available = false;
    bool found = false;
    const quint16 publishedLand = publishedTerrain.packedTile(
        0, 0, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    QCOMPARE(finalizedSignature(finalized, publishedLand, &found),
             signature(1, 1, 1, 1));
    QVERIFY(found);
    const quint16 publishedOverlay = publishedTerrain.packedTile(
        0, 0, ETerrainTileLayer::Overlay, &available);
    QVERIFY(available);
    QCOMPARE(finalizedSignature(finalized, publishedOverlay, &found),
             signature(4, 4, 4, 4));
    QVERIFY(found);
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
    const STerrainTransitionVariant generatedRoute =
        working.catalog().variantForPacked(reused, &decoded);
    QVERIFY(decoded);
    QCOMPARE(generatedRoute.routeOwnerMaterial, qint32(1));
    QVERIFY(generatedRoute.canonicalGeneratedRoute);
}

void TerrainVirtualAtlasTests::workingAtlasStagingInjectedFailureLeavesLiveStateUntouched()
{
    Fixture fixture;
    CTestTerrainWorkingAtlas live(fixture.catalog, fixture.images, 17, 8);
    seedReviewedStockMasks(live, fixture);
    const int generatedBefore = live.generatedTileCount();
    const int catalogBefore = live.catalogVariantCount();
    const int mappingsBefore = live.workingMappingCount();
    const QVector<QImage> imagesBefore = live.activeTileImages();
    const QVector<qint32> typesBefore = live.activeTileTypes();
    const QSet<int> changedBefore = live.changedSlots();
    const QByteArray fingerprintBefore = live.stateFingerprint();
    CMprDocument source;
    CValidationReport modelReport;
    QVERIFY2(source.loadBytes(portableMpr(), "staging-upload-failure.mpr", &modelReport),
             qPrintable(modelReport.toText()));
    CTerrainEditModel liveModel;
    QVERIFY2(liveModel.load(source, &modelReport),
             qPrintable(modelReport.toText()));
    liveModel.enableWorkingAtlasSlots(live.capacityLimit());
    SMprPackedTile stagedTile;
    stagedTile.tileIndex = quint8(fixture.images.size());
    const quint16 stagedPacked = stagedTile.encode();
    bool liveTypeFound = false;
    liveModel.tileType(stagedPacked, &liveTypeFound);
    QVERIFY(!liveTypeFound);

    QSharedPointer<CTerrainWorkingAtlas> staged = live.createStagingCopy();
    QVERIFY(staged);
    QMap<int, quint64> usage;
    for(int slot = 0; slot < staged->capacityLimit(); ++slot)
        usage.insert(slot, slot < fixture.images.size() ? 1 : 0);
    CValidationReport report;
    QVERIFY2(staged->resolve(signature(1, 2, 2, 2), 7, 8, &usage, {},
                              nullptr, &report) != 0,
             qPrintable(report.toText()));
    QVERIFY(staged->generatedTileCount() > generatedBefore);
    QVERIFY(!staged->changedSlots().isEmpty());
    CTerrainEditModel stagedModel = liveModel;
    const int stagedSlot = SMprPackedTile::decode(
        staged->catalog().selectForPaint(signature(1, 2, 2, 2),
                                         7, 8, 17)).atlasSlot();
    QVERIFY(stagedModel.setWorkingAtlasSlotType(
        stagedSlot, staged->runtimeTerrainTypeForSlot(stagedSlot)));

    // Inject the final upload failure by rejecting the staged candidate.  A
    // failed Resolve must not publish any of the realization side effects.
    const bool uploadSucceeded = false;
    QVERIFY(!uploadSucceeded);
    staged.clear();

    QCOMPARE(live.generatedTileCount(), generatedBefore);
    QCOMPARE(live.catalogVariantCount(), catalogBefore);
    QCOMPARE(live.workingMappingCount(), mappingsBefore);
    QCOMPARE(live.activeTileImages(), imagesBefore);
    QCOMPARE(live.activeTileTypes(), typesBefore);
    QCOMPARE(live.changedSlots(), changedBefore);
    QCOMPARE(live.stateFingerprint(), fingerprintBefore);
    liveModel.tileType(stagedPacked, &liveTypeFound);
    QVERIFY(!liveTypeFound);
}

void TerrainVirtualAtlasTests::workingAtlasStagingRealizationThenEmptyLeavesLiveStateUntouched()
{
    Fixture fixture;
    CTestTerrainWorkingAtlas live(fixture.catalog, fixture.images, 29, 8);
    seedReviewedStockMasks(live, fixture);
    const int generatedBefore = live.generatedTileCount();
    const int catalogBefore = live.catalogVariantCount();
    const int mappingsBefore = live.workingMappingCount();
    const QVector<QImage> imagesBefore = live.activeTileImages();
    const QVector<qint32> typesBefore = live.activeTileTypes();
    const QSet<int> changedBefore = live.changedSlots();
    const QByteArray fingerprintBefore = live.stateFingerprint();
    CMprDocument source;
    CValidationReport modelReport;
    QVERIFY2(source.loadBytes(portableMpr(), "staging-empty-command.mpr", &modelReport),
             qPrintable(modelReport.toText()));
    CTerrainEditModel liveModel;
    QVERIFY2(liveModel.load(source, &modelReport),
             qPrintable(modelReport.toText()));
    liveModel.enableWorkingAtlasSlots(live.capacityLimit());
    SMprPackedTile stagedTile;
    stagedTile.tileIndex = quint8(fixture.images.size());
    const quint16 stagedPacked = stagedTile.encode();
    bool liveTypeFound = false;
    liveModel.tileType(stagedPacked, &liveTypeFound);
    QVERIFY(!liveTypeFound);

    QSharedPointer<CTerrainWorkingAtlas> staged = live.createStagingCopy();
    QVERIFY(staged);
    QMap<int, quint64> usage;
    for(int slot = 0; slot < staged->capacityLimit(); ++slot)
        usage.insert(slot, slot < fixture.images.size() ? 1 : 0);
    CValidationReport report;
    QVERIFY2(staged->resolve(signature(1, 2, 2, 2), 3, 4, &usage, {},
                              nullptr, &report) != 0,
             qPrintable(report.toText()));
    QVERIFY(staged->generatedTileCount() > generatedBefore);
    CTerrainEditModel stagedModel = liveModel;
    const int stagedSlot = SMprPackedTile::decode(
        staged->catalog().selectForPaint(signature(1, 2, 2, 2),
                                         3, 4, 29)).atlasSlot();
    QVERIFY(stagedModel.setWorkingAtlasSlotType(
        stagedSlot, staged->runtimeTerrainTypeForSlot(stagedSlot)));

    // The full semantic map command can still be empty after the cache was
    // realized.  Treat that as an abort, not as a cache publication.
    const STerrainTileCommand finalCommand;
    QVERIFY(finalCommand.isEmpty());
    staged.clear();

    QCOMPARE(live.generatedTileCount(), generatedBefore);
    QCOMPARE(live.catalogVariantCount(), catalogBefore);
    QCOMPARE(live.workingMappingCount(), mappingsBefore);
    QCOMPARE(live.activeTileImages(), imagesBefore);
    QCOMPARE(live.activeTileTypes(), typesBefore);
    QCOMPARE(live.changedSlots(), changedBefore);
    QCOMPARE(live.stateFingerprint(), fingerprintBefore);
    liveModel.tileType(stagedPacked, &liveTypeFound);
    QVERIFY(!liveTypeFound);
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

void TerrainVirtualAtlasTests::workingAtlasReusesSafeLogicalTailPaddingBeforeExtendingPages()
{
    Fixture fixture;
    QVector<QImage> physicalPage;
    physicalPage.reserve(64);
    for(int slot = 0; slot < 64; ++slot)
        physicalPage.append(fixture.images.at(slot % fixture.images.size()));
    QVector<qint32> logicalTypes;
    logicalTypes.reserve(61);
    for(int slot = 0; slot < 61; ++slot)
        logicalTypes.append(slot % 4);

    CTestTerrainWorkingAtlas working(
        fixture.catalog, physicalPage, 47, 128, {}, {}, logicalTypes);
    seedReviewedStockMasks(working, fixture);
    QMap<int, quint64> usage;
    CValidationReport report;
    bool generated = false;
    const quint16 packed = working.resolve(signature(1, 2, 3, 4), 3, 7,
                                            &usage, {}, &generated, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(generated);

    // Slots 61..63 are the unused tail of the authored physical MMP page.
    // They have no authored logical identity and are reusable when they are
    // unreferenced and not protected by undo history.
    QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(), 61);
    QCOMPARE(working.activeTextureCount(), 1);
    STerrainWorkingTileIdentity identity;
    QVERIFY(working.tileIdentityForSlot(61, &identity));
    QCOMPARE(identity.runtimeTerrainType, working.activeTileTypes().at(61));
    const QVector<QByteArray> manualIdentities =
        working.manualAtlasPhysicalIdentities();
    QCOMPARE(manualIdentities.size(), 64);
    QVERIFY(!manualIdentities.at(61).isEmpty());
    QVERIFY(manualIdentities.at(62).isEmpty());
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

void TerrainVirtualAtlasTests::manualTilePaintRejectsUntypedWorkingSlot()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;

    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 2,
                              QVector<qint32>(64, 1), 0));
    model.enableWorkingAtlasSlots(128);
    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 0.0f;

    QVERIFY(!model.makeTilePaint(
        brush, ETerrainTileLayer::Land, 0, 1, 0).isEmpty());
    QVERIFY(model.makeTilePaint(
        brush, ETerrainTileLayer::Land, 1, 0, 0).isEmpty());
    CTerrainTransitionCatalog catalog;
    QVERIFY(model.makeTilePaint(
        brush, ETerrainTileLayer::Land, 1, 0, 0, catalog, &report).isEmpty());
}

void TerrainVirtualAtlasTests::completeWorkingTypeTableMakesEveryManualVariantPaintable()
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    payload.append(QByteArray(33 * 33 * 8, '\0'));
    payload.append(QByteArray(16 * 16 * 2, '\0'));
    STerrainSectorSource source;
    source.memberName = QStringLiteral("CaveSet000000.SEC");
    source.sectorX = 0;
    source.sectorY = 0;
    source.type = 0;
    source.payload = payload;
    source.landTileOffset = 5 + 33 * 33 * 8;

    CTerrainEditModel model;
    CValidationReport report;
    QVERIFY(model.loadSources(1, 1, 100.0f, {source}, &report, 2,
                              QVector<qint32>(64, 1), 0));
    model.enableWorkingAtlasSlots(128);
    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 0.0f;

    // A family import may realize several Manual-visible variants even when
    // the Modern stroke selects only one of them.
    QVERIFY(model.makeTilePaint(
        brush, ETerrainTileLayer::Land, 1, 1, 0).isEmpty());
    QVector<qint32> activeTypes(67, 1);
    activeTypes[64] = 4;
    activeTypes[65] = 5;
    activeTypes[66] = 6;
    QVERIFY(model.canInstallWorkingAtlasSlotTypes(activeTypes));
    QVERIFY(model.installWorkingAtlasSlotTypes(activeTypes));
    QVERIFY(!model.makeTilePaint(
        brush, ETerrainTileLayer::Land, 1, 1, 0).isEmpty());
    QVERIFY(!model.makeTilePaint(
        brush, ETerrainTileLayer::Land, 1, 2, 0).isEmpty());

    QVector<qint32> invalid = activeTypes;
    invalid[65] = -1;
    QVERIFY(!model.canInstallWorkingAtlasSlotTypes(invalid));
    QVERIFY(!model.installWorkingAtlasSlotTypes(invalid));
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

void TerrainVirtualAtlasTests::workingAtlasRepacksInterleavedUniformAnimationFramesAtomically()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    const QVector<STerrainTransitionSignature> signatures = {
        signature(7, 7, 7, 7), signature(7, 8, 8, 7),
        signature(7, 7, 7, 7), signature(8, 7, 7, 8),
        signature(7, 7, 7, 7), signature(8, 8, 8, 8)
    };
    for(int slot = 0; slot < signatures.size(); ++slot)
    {
        images.append(solid(qRgba(20 + slot * 25, 40, 70, 255)));
        STerrainTransitionVariant variant;
        variant.signature = signatures.at(slot);
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    const QVector<qint32> types(signatures.size(), 3);
    CTestTerrainWorkingAtlas live(catalog, images, 0x7766u, 16,
                                  {}, {}, types);
    const QByteArray before = live.stateFingerprint();
    QSharedPointer<CTerrainWorkingAtlas> staged = live.createStagingCopy();
    SMprAnimatedTileRecord record;
    record.tileIndex = 0;
    record.frameCount = 3;
    QVector<SMprAnimatedTileRecord> normalized;
    QMap<int, int> remap;
    CValidationReport report;
    QVERIFY2(staged->normalizeAnimationRanges({record}, &normalized,
        &remap, &report), qPrintable(report.toText()));
    QCOMPARE(live.stateFingerprint(), before);
    QCOMPARE(normalized.size(), 1);
    QCOMPARE(normalized.front().tileIndex, quint16(6));
    QCOMPARE(normalized.front().frameCount, quint16(3));
    QCOMPARE(remap.value(0), 6);
    QCOMPARE(remap.value(2), 6);
    QCOMPARE(remap.value(4), 6);
    QCOMPARE(remap.size(), 3);
    const QMap<int, int> activationRemap = remap;
    QVERIFY(live.commitStaging(*staged));
    for(int slot = 6; slot < 9; ++slot)
    {
        STerrainWorkingTileIdentity identity;
        QVERIFY(live.tileIdentityForSlot(slot, &identity));
        QCOMPARE(identity.canonicalSignature, signature(7, 7, 7, 7));
        QCOMPARE(identity.runtimeTerrainType, qint32(3));
    }

    QSharedPointer<CTerrainWorkingAtlas> contiguous = live.createStagingCopy();
    record.tileIndex = 6;
    QVERIFY2(contiguous->normalizeAnimationRanges({record}, &normalized,
        &remap, &report), qPrintable(report.toText()));
    QCOMPARE(normalized.front().tileIndex, quint16(6));
    QCOMPARE(remap.value(6), 6);
    QCOMPARE(remap.value(7), 6);
    QCOMPARE(remap.value(8), 6);
    QCOMPARE(remap.size(), 3);

    SMprPackedTile activating;
    activating.tileIndex = 0;
    activating.rotation = 3;
    SMprPackedTile authoredPhase;
    authoredPhase.tileIndex = 4;
    authoredPhase.rotation = 2;
    CMprDocument document;
    QVERIFY2(document.loadBytes(portableMprWithActiveOverlay(
        activating.encode(), authoredPhase.encode()),
        QStringLiteral("animation-remap.mpr"), &report),
        qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    terrain.enableWorkingAtlasSlots(live.capacityLimit());
    QVERIFY(terrain.installWorkingAtlasSlotTypes(live.activeTileTypes()));
    const STerrainTileCommand remapCommand = terrain.makeAtlasSlotRemap(
        activationRemap);
    QCOMPARE(remapCommand.deltas.size(), 16 * 16 * 2);
    QVERIFY(terrain.apply(remapCommand, true, &report));
    for(ETerrainTileLayer layer : {ETerrainTileLayer::Land,
                                   ETerrainTileLayer::Overlay})
    {
        bool available = false;
        const SMprPackedTile moved = SMprPackedTile::decode(
            terrain.packedTile(0, 0, layer, &available));
        QVERIFY(available);
        QCOMPARE(moved.atlasSlot(), 6);
        QCOMPARE(moved.rotation, layer == ETerrainTileLayer::Land
            ? quint8(3) : quint8(2));
    }
    QVERIFY(terrain.apply(remapCommand, false, &report));
    QCOMPARE(terrain.packedTile(0, 0, ETerrainTileLayer::Land),
             activating.encode());
    QCOMPARE(terrain.packedTile(0, 0, ETerrainTileLayer::Overlay),
             authoredPhase.encode());

    // A declared range without enough same-signature/runtime frames must not
    // partially permute the live atlas or silently animate blend tiles.
    QSharedPointer<CTerrainWorkingAtlas> invalid = live.createStagingCopy();
    record.frameCount = 7;
    const QByteArray invalidBefore = invalid->stateFingerprint();
    QVERIFY(!invalid->normalizeAnimationRanges({record}, &normalized,
        &remap, &report));
    QCOMPARE(invalid->stateFingerprint(), invalidBefore);
}

void TerrainVirtualAtlasTests::workingAtlasKeepsFlipbooksInsideOnePhysicalPage()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    const STerrainTransitionSignature water = signature(7, 7, 7, 7);
    for(int slot = 0; slot < 100; ++slot)
    {
        images.append(solid(qRgba(20 + slot, 80, 110, 255)));
        STerrainTransitionVariant variant;
        variant.signature = water;
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot % 64);
        packed.atlasIndex = quint8(slot / 64);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    const QVector<qint32> types(100, 6);
    CTestTerrainWorkingAtlas live(catalog, images, 0x50432u, 192,
                                  {}, {}, types, {{7, 6}});
    const QByteArray before = live.stateFingerprint();
    QSharedPointer<CTerrainWorkingAtlas> staged = live.createStagingCopy();
    SMprAnimatedTileRecord record;
    record.tileIndex = 56;
    record.frameCount = 32;
    QVector<SMprAnimatedTileRecord> normalized;
    QMap<int, int> remap;
    CValidationReport report;
    QVERIFY2(staged->normalizeAnimationRanges({record}, &normalized,
        &remap, &report), qPrintable(report.toText()));
    QCOMPARE(live.stateFingerprint(), before);
    QCOMPARE(normalized.size(), 1);
    QCOMPARE(normalized.front().tileIndex, quint16(128));
    QCOMPARE(normalized.front().frameCount, quint16(32));
    QVERIFY(int(normalized.front().tileIndex % 64)
            + int(normalized.front().frameCount) <= 64);
    for(int source = 56; source < 88; ++source)
        QCOMPARE(remap.value(source), 128);
    for(int destination = 128; destination < 160; ++destination)
    {
        STerrainWorkingTileIdentity identity;
        QVERIFY(staged->tileIdentityForSlot(destination, &identity));
        QCOMPARE(identity.canonicalSignature, water);
        QCOMPARE(identity.runtimeTerrainType, qint32(6));
    }

    QSharedPointer<CTerrainWorkingAtlas> tooLong = live.createStagingCopy();
    record.tileIndex = 0;
    record.frameCount = 65;
    const QByteArray tooLongBefore = tooLong->stateFingerprint();
    QVERIFY(!tooLong->normalizeAnimationRanges({record}, &normalized,
        &remap, &report));
    QCOMPARE(tooLong->stateFingerprint(), tooLongBefore);
}

void TerrainVirtualAtlasTests::finalizationPreservesAndRebasesAuthoredAnimationRanges()
{
    Fixture fixture;
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(16, 16, 1));
    QVERIFY(field.setMaterial(1, 0, 2));
    CTestTerrainVirtualAtlas atlas(fixture.catalog, fixture.images, 0xadda11u);
    seedReviewedStockMasks(atlas, fixture);
    CValidationReport report;
    CMprDocument source;
    QVERIFY2(source.loadBytes(portableMprWithActiveOverlay(0, 0),
        QStringLiteral("animated-source.mpr"), &report),
        qPrintable(report.toText()));
    QVector<STerrainAtlasAnimationRange> animationRanges;
    STerrainAtlasAnimationRange liveAnimation;
    liveAnimation.tileIndex = 0;
    liveAnimation.frameCount = 4;
    animationRanges.append(liveAnimation);
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("AnimatedSource"), 512, {1,2,3,4}, &report,
        {}, {}, {}, {}, nullptr, animationRanges);
    QVERIFY2(finalized.success,
        qPrintable(finalized.error + "\n" + report.toText()));
    CTerrainEditModel editedTerrain;
    QVERIFY2(editedTerrain.load(source, &report), qPrintable(report.toText()));
    QVector<SMprMaterialRecord> liveMaterials = source.mapInfo().materials;
    QVERIFY(!liveMaterials.isEmpty());
    liveMaterials.front().red = 0.875f;
    const STerrainPortableAtlasPackage package =
        CTestTerrainVirtualAtlas::buildPortablePackage(
            source, editedTerrain, liveMaterials, finalized,
            ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
        qPrintable(package.error + "\n" + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
        QStringLiteral("animated-finalized.mpr"), &report),
        qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().animatedTileCount, quint32(1));
    QCOMPARE(reopened.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(reopened.mapInfo().animatedTiles.front().frameCount, quint16(4));
    QCOMPARE(reopened.mapInfo().materials, liveMaterials);
    const int remappedStart = reopened.mapInfo().animatedTiles.front().tileIndex;
    QVERIFY(remappedStart >= 0);
    QVERIFY(remappedStart + 4 <= finalized.tiles.size());
    for(int phase = 0; phase < 4; ++phase)
    {
        const STerrainFinalizedTile& frame = finalized.tiles.at(
            remappedStart + phase);
        QVERIFY(frame.authored);
        QVERIFY2(frame.sourceSlotAliases.contains(phase),
                 "Every source flipbook frame must survive physical compaction in order");
    }

    QVector<STerrainAtlasAnimationRange> incomplete = animationRanges;
    incomplete.front().tileIndex = 2;
    incomplete.front().frameCount = 3;
    CValidationReport invalidReport;
    const STerrainAtlasFinalization invalid = atlas.finalize(
        field, QStringLiteral("AnimatedSource"), 512, {1,2,3,4},
        &invalidReport, {}, {}, {}, {}, nullptr, incomplete);
    QVERIFY(!invalid.success);
    QVERIFY2(invalid.error.contains("animated", Qt::CaseInsensitive),
             qPrintable(invalid.error));

    QVector<STerrainAtlasAnimationRange> overlapping;
    STerrainAtlasAnimationRange tail;
    tail.tileIndex = 2;
    tail.frameCount = 2;
    overlapping.append(tail);
    STerrainAtlasAnimationRange whole;
    whole.tileIndex = 0;
    whole.frameCount = 4;
    overlapping.append(whole);
    CValidationReport overlapReport;
    const STerrainAtlasFinalization overlapFinalized = atlas.finalize(
        field, QStringLiteral("AnimatedSource"), 512, {1,2,3,4},
        &overlapReport, {}, {}, {}, {}, nullptr, overlapping);
    QVERIFY2(overlapFinalized.success,
        qPrintable(overlapFinalized.error + "\n" + overlapReport.toText()));
    QCOMPARE(overlapFinalized.animations.size(), 2);
    QCOMPARE(int(overlapFinalized.animations.at(0).tileIndex),
             int(overlapFinalized.animations.at(1).tileIndex) + 2);
}

void TerrainVirtualAtlasTests::finalizationKeepsFlipbooksInsideOnePhysicalPage()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    const STerrainTransitionSignature water = signature(7, 7, 7, 7);
    for(int slot = 0; slot < 104; ++slot)
    {
        images.append(solid(qRgba(20 + slot, 80, 110, 255)));
        STerrainTransitionVariant variant;
        variant.signature = water;
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot % 64);
        packed.atlasIndex = quint8(slot / 64);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    const QVector<qint32> types(104, 6);
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x50464u,
        {}, {}, true, types, {{7, 6}});
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 7));
    STerrainAtlasAnimationRange first;
    first.tileIndex = 0;
    first.frameCount = 40;
    STerrainAtlasAnimationRange second;
    second.tileIndex = 64;
    second.frameCount = 40;
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("PageLocalFlipbooks"), 192, types, &report,
        {}, {}, {}, {}, nullptr, {first, second});
    QVERIFY2(finalized.success,
        qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    QCOMPARE(finalized.animations.size(), 2);
    for(const STerrainAtlasAnimationRange& animation : finalized.animations)
        QVERIFY(int(animation.tileIndex % 64) + int(animation.frameCount) <= 64);
    QCOMPARE(finalized.animations.at(0).tileIndex, quint16(0));
    QCOMPARE(finalized.animations.at(1).tileIndex, quint16(64));

    STerrainAtlasAnimationRange crossing;
    crossing.tileIndex = 56;
    crossing.frameCount = 32;
    CValidationReport invalidReport;
    const STerrainAtlasFinalization invalid = atlas.finalize(
        field, QStringLiteral("CrossingFlipbook"), 192, types,
        &invalidReport, {}, {}, {}, {}, nullptr, {crossing});
    QVERIFY(!invalid.success);
    QVERIFY2(invalid.error.contains("page", Qt::CaseInsensitive),
             qPrintable(invalid.error));
}

void TerrainVirtualAtlasTests::finalizationCompactsAnimationBlocksWithoutSourcePageDebris()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> images;
    const STerrainTransitionSignature water = signature(7, 7, 7, 7);
    for(int slot = 0; slot < 64; ++slot)
    {
        images.append(solid(qRgba(20 + slot, 80, 110, 255)));
        STerrainTransitionVariant variant;
        variant.signature = water;
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    const QVector<qint32> types(64, 6);
    CTestTerrainVirtualAtlas atlas(catalog, images, 0x50432u,
        {}, {}, true, types, {{7, 6}});
    CTerrainVirtualMaterialField field;
    QVERIFY(field.resize(1, 1, 7));
    STerrainAtlasAnimationRange animation;
    animation.tileIndex = 28;
    animation.frameCount = 32;
    SMprPackedTile selected;
    selected.tileIndex = 28;
    CValidationReport report;
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, QStringLiteral("CompactFlipbook"), 128, types, &report,
        {}, {6}, {selected.encode()}, {7}, &catalog, {animation});
    QVERIFY2(finalized.success,
        qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    QCOMPARE(finalized.animations.size(), 1);
    QCOMPARE(finalized.animations.front().tileIndex, quint16(0));
    QCOMPARE(finalized.animations.front().frameCount, quint16(32));
    QCOMPARE(finalized.requiredSlots, 32);
    for(int phase = 0; phase < 32; ++phase)
        QVERIFY(finalized.tiles.at(phase).sourceSlotAliases.contains(28 + phase));
    for(int obsolete = 0; obsolete < 28; ++obsolete)
        QVERIFY2(!finalized.authoredSlotRemap.contains(obsolete),
                 "Unused source-page prefix survived clean flipbook compaction");
    for(int obsolete = 60; obsolete < 64; ++obsolete)
        QVERIFY2(!finalized.authoredSlotRemap.contains(obsolete),
                 "Unused source-page suffix survived clean flipbook compaction");
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

void TerrainVirtualAtlasTests::oppositeBaselineOwnerDoesNotBlockStroke_data()
{
    QTest::addColumn<bool>("mirrored");
    QTest::addColumn<int>("paint");
    QTest::addColumn<bool>("legacyOwner");
    for(bool mirror:{false,true})for(int material:{1,2})for(bool legacy:{false,true})
        QTest::newRow(qPrintable(QString("mirror%1-paint%2-legacy%3").arg(mirror).arg(material).arg(legacy)))
            <<mirror<<material<<legacy;
}

void TerrainVirtualAtlasTests::oppositeBaselineOwnerDoesNotBlockStroke()
{
    QFETCH(bool,mirrored);QFETCH(int,paint);QFETCH(bool,legacyOwner);
    const int background=3-paint;
    Fixture fixture;fixture.catalog.setMirrorPaintGridY(mirrored);
    const auto wanted=signature(paint,background,background,background);
    const auto physical=mirrored?CTerrainTransitionCatalog::mirroredGridY(wanted):wanted;
    fixture.images.append(solid(qRgba(80,80,80,255)));
    for(int rotation=0;rotation<4;++rotation) {
        SMprPackedTile packed;packed.tileIndex=4;packed.rotation=quint8(rotation);
        STerrainTransitionVariant v;v.packedTile=packed.encode();
        v.signature=CTerrainTransitionCatalog::rotated(physical,-rotation);
        v.canonicalGeneratedRoute=true;v.routeOwnerMaterial=legacyOwner?0:background;
        QVERIFY(fixture.catalog.addVariant(v));
    }
    CTerrainWorkingAtlas working(fixture.catalog,fixture.images,23,64,{}, {},
        QVector<qint32>(5,0),{{1,0},{2,0},{3,0},{4,0}});
    QVERIFY(working.isValid());
    QByteArray payload;QDataStream sec(&payload,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);sec<<quint32(0xCF4BF774)<<quint8(0);
    payload.append(QByteArray(33*33*8,'\0'));payload.append(QByteArray(16*16*2,'\0'));
    STerrainSectorSource source;source.memberName="CaveSet000000.SEC";
    source.sectorX=0;source.sectorY=0;source.type=0;source.payload=payload;source.landTileOffset=5+33*33*8;
    CTerrainEditModel model;CValidationReport report;
    QVERIFY(model.loadSources(1,1,100.0f,{source},&report,1,QVector<qint32>(64,0),0));
    QVector<qint32> values(17*17,background);
    QVERIFY(model.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,values,values));
    const auto before=model.sectorPayload(0,0);
    CTerrainVirtualMaterialField beforeField;QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&beforeField));
    STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=0;
    const auto preview=model.makeVirtualMaterialPaint(8,8,brush,ETerrainTileLayer::Land,paint,working.catalog(),23,&report);
    QVERIFY(!preview.isEmpty());QVERIFY(model.apply(preview,true,&report));
    CTerrainVirtualMaterialField field;QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&field));
    QMap<int,quint64> usage;
    for(int y:{7,8})for(int x:{7,8}) {
        const auto packed=working.resolve(field.cellSignature(x,y),x,y,&usage,{},nullptr,&report,paint);
        ++usage[SMprPackedTile::decode(packed).atlasSlot()];
    }
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    QVERIFY(model.installWorkingAtlasSlotTypes(working.activeTileTypes()));
    const auto resolution=model.makeVirtualStrokeResolution(preview,working.catalog(),23,&report,false,paint);
    QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
    QVERIFY(model.apply(resolution,true,&report));
    for(int y:{7,8})for(int x:{7,8}) {
        bool found=false;const auto packed=model.packedTile(x,y,ETerrainTileLayer::Land,&found);QVERIFY(found);
        QCOMPARE(working.catalog().gridSignatureForPacked(packed,&found),field.cellSignature(x,y));QVERIFY(found);
        QCOMPARE(working.catalog().variantForPacked(packed).routeOwnerMaterial,qint32(paint));
    }
    QVERIFY(model.apply(resolution,false,&report));QVERIFY(model.apply(preview,false,&report));
    QCOMPARE(model.sectorPayload(0,0),before);
    CTerrainVirtualMaterialField undone;QVERIFY(model.copyVirtualMaterialField(ETerrainTileLayer::Land,&undone));
    QCOMPARE(undone.stableSha256(),beforeField.stableSha256());
    for(int slot=0;slot<fixture.images.size();++slot)QCOMPARE(working.activeTileImages().at(slot),fixture.images.at(slot));
}

QTEST_GUILESS_MAIN(TerrainVirtualAtlasTests)
#include "terrain_virtual_atlas_tests.moc"

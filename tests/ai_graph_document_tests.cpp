#include <QtTest>
#include <QDataStream>
#include <QtEndian>
#include <QUndoCommand>

#include "ai_graph_document.h"
#include "ai_graph_preview_builder.h"
#include "render/ai_graph_preview_invalidation.h"
#include "../source/core/map_object_placement_policy.h"

namespace {
class TestAiGraphUndoCommand : public QUndoCommand,
                               public IAiGraphPreviewInvalidatingCommand
{
public:
    TestAiGraphUndoCommand(bool invalidates, QUndoCommand* parent = nullptr)
        : QUndoCommand(parent), m_invalidates(invalidates) {}
    bool invalidatesAiGraphPreview() const override { return m_invalidates; }
private:
    bool m_invalidates;
};

QByteArray syntheticGraph(int width, int height, bool trailing = false)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << qint32(width) << qint32(height);
    for(int layer = 0; layer < CAiGraphDocument::LayerCount; ++layer)
    {
        for(int y = 0; y < height; ++y)
        {
            for(int x = 0; x < width; ++x)
            {
                const int cell = y * width + x;
                for(int direction = 0; direction < CAiGraphDocument::DirectionCount; ++direction)
                    stream << qint16(layer * 1000 + cell * 10 + direction - 1);
            }
            for(int x = 0; x < width; ++x)
            {
                const int cell = y * width + x;
                stream << quint8(((layer + cell) & 7) << 4 | ((layer + cell + 1) & 7));
            }
            for(int x = 0; x < width; ++x)
            {
                const int cell = y * width + x;
                stream << quint16(0x8000 + layer * 100 + cell);
            }
        }
    }
    if(trailing)
        data.append("opaque-tail", 11);
    return data;
}

void putU16(QByteArray* data, int offset, quint16 value)
{
    qToLittleEndian<quint16>(value, reinterpret_cast<uchar*>(data->data() + offset));
}

void putU32(QByteArray* data, int offset, quint32 value)
{
    qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(data->data() + offset));
}

QByteArray syntheticTraversalRegistry()
{
    QByteArray data(67, 0);
    putU32(&data, 0, 0x45ab3efb);
    putU16(&data, 4, 1);
    putU16(&data, 6, 0xffff);
    putU32(&data, 8, 12);
    putU16(&data, 12, 2);
    putU16(&data, 14, 1);
    data[16] = '4';
    putU16(&data, 17, 0xffff); putU32(&data, 19, 29);
    putU16(&data, 23, 0xffff); putU32(&data, 25, 41);
    data[41] = 0; putU16(&data, 42, 5); data.replace(44, 5, "Speed");
    putU32(&data, 49, 700);
    data[53] = 0; putU16(&data, 54, 7); data.replace(56, 7, "CostMul");
    putU32(&data, 63, 50);
    return data;
}

SAiGraphCollisionProxy boxVolume(float minimumX, float minimumY, float minimumZ,
                                 float maximumX, float maximumY, float maximumZ,
                                 EAiGraphObjectPassability passability =
                                     EAiGraphObjectPassability::Blocking)
{
    SAiGraphCollisionProxy volume;
    volume.footprint = {QVector2D(minimumX, minimumY), QVector2D(maximumX, minimumY),
                        QVector2D(maximumX, maximumY), QVector2D(minimumX, maximumY)};
    volume.minimumZ = minimumZ;
    volume.maximumZ = maximumZ;
    volume.boxCorners = {
        QVector3D(minimumX, minimumY, minimumZ),
        QVector3D(maximumX, minimumY, minimumZ),
        QVector3D(minimumX, maximumY, minimumZ),
        QVector3D(maximumX, maximumY, minimumZ),
        QVector3D(minimumX, minimumY, maximumZ),
        QVector3D(maximumX, minimumY, maximumZ),
        QVector3D(minimumX, maximumY, maximumZ),
        QVector3D(maximumX, maximumY, maximumZ)};
    volume.passability = passability;
    volume.authored = true;
    return volume;
}
}

class AiGraphDocumentTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesKindMajorPlanesAndSignedValues();
    void comparesLoadedAndRegeneratedNavigationPlanes();
    void rejectsNavigationComparisonWithDifferentDimensions();
    void resolvesTerrainRelativeStaticObjectPlacement();
    void preservesNoOpBytesIncludingTrailingData();
    void rejectsMalformedDimensionsAndTruncation();
    void exposesStableLayerAndDirectionDefinitions();
    void initializesDecodedPreviewWithSerializableBytes();
    void generatedGraphMeetsRuntimeStagingContract();
    void regenerationRespectsFenceAndVerticalClearanceProfiles();
    void regenerationRejectsGameScaleTerrainCliffs();
    void regenerationUsesRuntimeHeightRounding();
    void regenerationUsesRuntimeBridgeHeightRounding();
    void quantizesWaterBeforeOverlayTerrainSelection();
    void usesRuntimeElevationMultiplierArithmetic();
    void regenerationUsesRuntimeCrossingTraversal();
    void regenerationRasterizesAuthoredDegenerateCollisionSlab();
    void regenerationMergesRuntimeObjectBodyParts();
    void regenerationMergesRuntimeBridgeBodyParts();
    void regenerationKeepsEasyFacesForObjectMerge();
    void regenerationKeepsBlockingLowerFacesForObjectMerge();
    void collisionMergeKeepsMostRestrictiveContributingPart();
    void classifiesNearHorizontalBridgeFaceBeforeWorldTranslation();
    void regenerationMatchesRuntimeWideSlopeMask();
    void regenerationAppliesWaterClearanceToEveryProfile();
    void regenerationUsesBridgeSurfaceAndObjectSemantics();
    void regenerationRejectsSideOnlyBridgeFringeCells();
    void regenerationRejectsBridgeIntervalsDegenerateAfterQuantization();
    void regenerationKeepsNarrowBridgeCorridorInCoarseGraph();
    void regeneratedHumanProjectionUsesPassableBridgeSurface();
    void regenerationPreservesAuthoredNarrowObjectOpening();
    void regenerationRasterizesSlopedBridgeHeightsPerTile();
    void regenerationHonorsConfiguredTerrainTraversal();
    void regenerationUsesRuntimeRouteWeightScale();
    void regenerationIndexesCollisionCoverageOnce();
    void regenerationHandlesPopulatedMapScale();
    void parsesProfileTerrainTraversalRegistry();
    void sourceSwitchOwnsRegenerationLifecycle();
    void invalidationClassifiesOnlyGraphBuilderInputs();
    void invalidationTraversesUndoMacros();
    void staticObjectNidInvalidationIsScoped();
};

void AiGraphDocumentTests::resolvesTerrainRelativeStaticObjectPlacement()
{
    const QVector3D stored(80.0f, 42.0f, 3.25f);
    const QVector3D terrain(80.0f, 42.0f, 11.5f);
    const QVector3D resolved = ei::resolveTerrainRelativeMapObjectPosition(
        eWorldObject, stored, terrain);
    QCOMPARE(resolved, QVector3D(80.0f, 42.0f, 14.75f));

    QVERIFY(ei::usesTerrainRelativeMapObjectHeight(eWorldObject));
    QVERIFY(ei::usesTerrainRelativeMapObjectHeight(eTorch));
    QVERIFY(ei::usesTerrainRelativeMapObjectHeight(eLever));
    QVERIFY(!ei::usesTerrainRelativeMapObjectHeight(eUnit));
    QVERIFY(!ei::usesTerrainRelativeMapObjectHeight(eParticle));

    QVERIFY(ei::contributesStaticNavigationCollision(eWorldObject));
    QVERIFY(ei::contributesStaticNavigationCollision(eTorch));
    QVERIFY(ei::contributesStaticNavigationCollision(eLever));
    QVERIFY(!ei::contributesStaticNavigationCollision(eMagicTrap));
    QVERIFY(!ei::contributesStaticNavigationCollision(eUnit));
    QCOMPARE(ei::runtimeAiTileCoordinate(82.0f), 164);
    QCOMPARE(ei::runtimeAiTileCoordinate(82.5f), 164);
    const QQuaternion nonUnitRotation(0.8f, 0.1f, 0.2f, -0.3f);
    const QVector3D runtimeRotated = ei::runtimeNavigationRotateVector(
        nonUnitRotation, QVector3D(2.0f, -1.0f, 0.5f));
    QVERIFY(qAbs(runtimeRotated.x() - 1.09f) < 0.00001f);
    QVERIFY(qAbs(runtimeRotated.y() + 1.82f) < 0.00001f);
    QVERIFY(qAbs(runtimeRotated.z() + 0.35f) < 0.00001f);
    QVERIFY((runtimeRotated
             - nonUnitRotation.rotatedVector(QVector3D(2.0f, -1.0f, 0.5f)))
            .length() > 0.01f);
    const float maximumTerrainHeight = 36.70589f;
    const float sampled = 151.4f * maximumTerrainHeight / 511.0f;
    const float shift = ei::runtimeNavigationObjectAnchorShift(
        0.25f, 11.0f, sampled, maximumTerrainHeight);
    const float expected = 151.0f * maximumTerrainHeight / 511.0f
        + 0.25f - 11.0f;
    QVERIFY(qAbs(shift - expected) < 0.0001f);
}

void AiGraphDocumentTests::parsesKindMajorPlanesAndSignedValues()
{
    CAiGraphDocument document;
    QString error;
    QVERIFY2(document.parse(syntheticGraph(2, 2), &error), qPrintable(error));
    QCOMPARE(document.width(), 2);
    QCOMPARE(document.height(), 2);
    QCOMPARE(document.weight(0, 0, 0, 0), qint16(-1));
    QCOMPARE(document.weight(3, 1, 0, 7), qint16(3016));
    QCOMPARE(document.localX(2, 1, 0), 4);
    QCOMPARE(document.localY(2, 1, 0), 3);
    QCOMPARE(document.component(7, 1, 0), quint16(0x8000 + 701));
    QCOMPARE(document.weight(4, 0, 1, 6), qint16(4025));
    QCOMPARE(document.component(5, 1, 1), quint16(0x8000 + 503));
}

void AiGraphDocumentTests::preservesNoOpBytesIncludingTrailingData()
{
    const QByteArray source = syntheticGraph(1, 2, true);
    CAiGraphDocument document;
    QString error;
    QStringList warnings;
    QVERIFY2(document.parse(source, &error, &warnings), qPrintable(error));
    QCOMPARE(document.serializeNoOp(), source);
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.front().contains(QStringLiteral("11")));
}

void AiGraphDocumentTests::rejectsMalformedDimensionsAndTruncation()
{
    CAiGraphDocument document;
    QString error;
    QByteArray invalid = syntheticGraph(1, 1);
    invalid[0] = 0;
    invalid[1] = 0;
    invalid[2] = 0;
    invalid[3] = 0;
    QVERIFY(!document.parse(invalid, &error));
    QVERIFY(error.contains(QStringLiteral("dimensions")));

    QByteArray truncated = syntheticGraph(2, 2);
    truncated.chop(1);
    QVERIFY(!document.parse(truncated, &error));
    QVERIFY(error.contains(QStringLiteral("truncated")));
}

void AiGraphDocumentTests::exposesStableLayerAndDirectionDefinitions()
{
    QCOMPARE(CAiGraphDocument::directionDelta(0), QPoint(0, -1));
    QCOMPARE(CAiGraphDocument::directionDelta(4), QPoint(0, 1));
    QCOMPARE(CAiGraphDocument::directionDelta(6), QPoint(1, 0));
    QCOMPARE(CAiGraphDocument::layerLabel(3), QStringLiteral("Narrow / human-sized"));
    QCOMPARE(CAiGraphDocument::canonicalHeight(3), 1.6);
}

void AiGraphDocumentTests::initializesDecodedPreviewWithSerializableBytes()
{
    QVector<SAiGraphLayer> layers(CAiGraphDocument::LayerCount);
    for(SAiGraphLayer& layer : layers)
    {
        layer.weights.fill(qint16(-1), CAiGraphDocument::DirectionCount);
        layer.packedPositions.fill(char(0x33), 1);
        layer.components.fill(quint16(1), 1);
    }
    CAiGraphDocument document;
    QString error;
    QVERIFY2(document.initializePreview(1, 1, layers, &error), qPrintable(error));
    QVERIFY(document.isValid());
    QVERIFY(document.rawBytes().isEmpty());
    const QByteArray serialized = document.serialize(&error);
    QVERIFY2(!serialized.isEmpty(), qPrintable(error));
    QCOMPARE(serialized.size(), 8 + 152);
    CAiGraphDocument reopened;
    QVERIFY2(reopened.parse(serialized, &error), qPrintable(error));
    QCOMPARE(reopened.width(), 1);
    QCOMPARE(reopened.height(), 1);
    QCOMPARE(reopened.weight(0, 0, 0, 0), qint16(-1));
    QCOMPARE(reopened.packedPosition(0, 0, 0), quint8(0x33));
    QCOMPARE(reopened.component(0, 0, 0), quint16(1));
    QCOMPARE(reopened.serialize(&error), serialized);
}

void AiGraphDocumentTests::generatedGraphMeetsRuntimeStagingContract()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 4;
    input.terrainHeights.fill(0.0f, 32 * 32);
    input.collisionProxies.append(boxVolume(
        4.0f, 4.0f, 0.0f, 8.0f, 8.0f, 10.0f));
    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QString error;
    QVERIFY2(result.document.validateRuntimeCompatibility(4, 4, &error),
             qPrintable(error));
    QCOMPARE(result.document.packedPosition(3, 1, 1), quint8(0x33));

    const QByteArray bytes = result.document.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    CAiGraphDocument reopened;
    QVERIFY2(reopened.parse(bytes, &error), qPrintable(error));
    QVERIFY2(reopened.validateRuntimeCompatibility(4, 4, &error),
             qPrintable(error));

    CAiGraphDocument structurallyParseable;
    QVERIFY(structurallyParseable.parse(syntheticGraph(2, 2), &error));
    QVERIFY(!structurallyParseable.validateRuntimeCompatibility(2, 2, &error));
}

void AiGraphDocumentTests::regenerationRespectsFenceAndVerticalClearanceProfiles()
{
    SAiGraphPreviewBuildInput flat;
    flat.graphWidth = 4;
    flat.graphHeight = 3;
    flat.terrainHeights.fill(0.0f, 4 * 8 * 3 * 8);
    const SAiGraphPreviewBuildResult open = CAiGraphPreviewBuilder::build(flat);
    QVERIFY2(open.valid, qPrintable(open.error));
    QVERIFY(open.document.weight(0, 1, 1, 6) >= 0);
    QVERIFY(open.document.weight(3, 1, 1, 6) >= 0);

    SAiGraphCollisionProxy fence = boxVolume(7.8f, 0.0f, 0.0f,
                                             8.2f, 12.0f, 2.5f);
    fence.objectId = 568;
    SAiGraphPreviewBuildInput fenced = flat;
    fenced.collisionProxies.append(fence);
    const SAiGraphPreviewBuildResult blocked = CAiGraphPreviewBuilder::build(fenced);
    QVERIFY2(blocked.valid, qPrintable(blocked.error));
    QCOMPARE(blocked.collisionProxyCount, 1);
    QCOMPARE(blocked.document.weight(0, 1, 1, 6), qint16(-1));
    QCOMPARE(blocked.document.weight(3, 1, 1, 6), qint16(-1));

    SAiGraphCollisionProxy overhead = boxVolume(7.8f, 0.0f, 2.0f,
                                                8.2f, 12.0f, 3.0f);
    SAiGraphPreviewBuildInput clearance = flat;
    clearance.collisionProxies.append(overhead);
    const SAiGraphPreviewBuildResult vertical = CAiGraphPreviewBuilder::build(clearance);
    QVERIFY2(vertical.valid, qPrintable(vertical.error));
    QVERIFY(vertical.document.weight(3, 1, 1, 6) >= 0);
    QCOMPARE(vertical.document.weight(6, 1, 1, 6), qint16(-1));
}

void AiGraphDocumentTests::regenerationRejectsGameScaleTerrainCliffs()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);

    // A 0.5 m rise over one 0.5 m AI tile is too steep for ordinary units,
    // but remains below the levitating profile's terrain-gradient limit.
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 16; x < aiWidth; ++x)
            input.terrainHeights[y * aiWidth + x] = 0.5f;
    const SAiGraphPreviewBuildResult walkCliff = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(walkCliff.valid, qPrintable(walkCliff.error));
    QVERIFY(walkCliff.document.weight(0, 1, 1, 6) >= 0);
    QCOMPARE(walkCliff.document.weight(3, 1, 1, 6), qint16(-1));

    // A 0.9 m rise crosses the levitating limit as well.
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 16; x < aiWidth; ++x)
            input.terrainHeights[y * aiWidth + x] = 0.9f;
    const SAiGraphPreviewBuildResult flyCliff = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(flyCliff.valid, qPrintable(flyCliff.error));
    QCOMPARE(flyCliff.document.weight(0, 1, 1, 6), qint16(-1));
    QCOMPARE(flyCliff.document.weight(3, 1, 1, 6), qint16(-1));
}

void AiGraphDocumentTests::regenerationUsesBridgeSurfaceAndObjectSemantics()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(0.0f, aiWidth * aiHeight);
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 15; x <= 17; ++x)
            input.waterDepths[y * aiWidth + x] = 6.0f;

    const SAiGraphPreviewBuildResult water = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(water.valid, qPrintable(water.error));
    QCOMPARE(water.document.weight(3, 1, 1, 6), qint16(-1));

    // Runtime raster coordinates refer to half-metre tile centers. Extend the
    // synthetic deck through the center of water tile 17 (x = 8.75 m).
    SAiGraphCollisionProxy bridge = boxVolume(
        7.4f, 0.0f, 0.0f, 9.1f, 12.0f, 0.2f,
        EAiGraphObjectPassability::Bridge);
    input.collisionProxies.append(bridge);
    const SAiGraphPreviewBuildResult bridged = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(bridged.valid, qPrintable(bridged.error));
    QVERIFY(bridged.bridgeAiTiles > 0);
    QCOMPARE(bridged.authoredCollisionProxyCount, 1);
    for(int layer = 0; layer < CAiGraphDocument::LayerCount; ++layer)
        QVERIFY2(bridged.document.weight(layer, 1, 1, 6) >= 0,
                 qPrintable(QStringLiteral("bridge layer %1").arg(layer)));

    SAiGraphPreviewBuildInput easyInput = input;
    easyInput.waterDepths.fill(0.0f, aiWidth * aiHeight);
    easyInput.collisionProxies.clear();
    easyInput.collisionProxies.append(boxVolume(
        7.8f, 0.0f, 0.0f, 8.2f, 12.0f, 4.0f,
        EAiGraphObjectPassability::Easy));
    const SAiGraphPreviewBuildResult easy = CAiGraphPreviewBuilder::build(easyInput);
    QVERIFY2(easy.valid, qPrintable(easy.error));
    for(int layer = 0; layer < CAiGraphDocument::LayerCount; ++layer)
        QVERIFY(easy.document.weight(layer, 1, 1, 6) >= 0);

    easyInput.collisionProxies[0].passability = EAiGraphObjectPassability::Ignored;
    const SAiGraphPreviewBuildResult ignored = CAiGraphPreviewBuilder::build(easyInput);
    QVERIFY2(ignored.valid, qPrintable(ignored.error));
    for(int layer = 0; layer < CAiGraphDocument::LayerCount; ++layer)
        QVERIFY(ignored.document.weight(layer, 1, 1, 6) >= 0);
}

void AiGraphDocumentTests::regenerationRejectsSideOnlyBridgeFringeCells()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 3;
    input.graphHeight = 2;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(6.0f, aiWidth * aiHeight);
    input.collisionProxies.append(boxVolume(
        0.0f, 0.0f, 0.0f, 8.0f, 1.0f, 1.0f,
        EAiGraphObjectPassability::Bridge));

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    // The upward face covers 16x2 half-metre cells. Samples from the vertical
    // end faces land on the next raster coordinate, but they do not describe a
    // walkable deck and must not extend the bridge.
    QCOMPARE(result.bridgeAiTiles, 32);
}

void AiGraphDocumentTests::comparesLoadedAndRegeneratedNavigationPlanes()
{
    const QByteArray loadedBytes = syntheticGraph(1, 1);
    QByteArray regeneratedBytes = loadedBytes;
    putU16(&regeneratedBytes, 8, 9);          // loaded closed, regenerated open
    putU16(&regeneratedBytes, 10, 0xffff);    // loaded open, regenerated closed
    putU16(&regeneratedBytes, 12, 77);        // shared link, changed weight
    regeneratedBytes[24] = char(quint8(regeneratedBytes.at(24)) ^ 0x01);

    CAiGraphDocument loaded;
    CAiGraphDocument regenerated;
    QString error;
    QVERIFY2(loaded.parse(loadedBytes, &error), qPrintable(error));
    QVERIFY2(regenerated.parse(regeneratedBytes, &error), qPrintable(error));

    const SAiGraphDifference difference = loaded.compareNavigation(regenerated);
    QVERIFY2(difference.comparable, qPrintable(difference.error));
    QCOMPARE(difference.layers.size(), CAiGraphDocument::LayerCount);
    QCOMPARE(difference.layers.at(0).missingLinks, qint64(1));
    QCOMPARE(difference.layers.at(0).extraLinks, qint64(1));
    QCOMPARE(difference.layers.at(0).changedWeights, qint64(1));
    QCOMPARE(difference.layers.at(0).changedPositions, qint64(1));
    QVERIFY(!difference.identical());
    const SAiGraphLayerDifference total = difference.total();
    QCOMPARE(total.missingLinks, qint64(1));
    QCOMPARE(total.extraLinks, qint64(1));
    QCOMPARE(total.changedWeights, qint64(1));
    QCOMPARE(total.changedPositions, qint64(1));

    const SAiGraphDifference identical = loaded.compareNavigation(loaded);
    QVERIFY(identical.comparable);
    QVERIFY(identical.identical());
}

void AiGraphDocumentTests::rejectsNavigationComparisonWithDifferentDimensions()
{
    CAiGraphDocument loaded;
    CAiGraphDocument regenerated;
    QString error;
    QVERIFY2(loaded.parse(syntheticGraph(1, 1), &error), qPrintable(error));
    QVERIFY2(regenerated.parse(syntheticGraph(2, 1), &error), qPrintable(error));
    const SAiGraphDifference difference = loaded.compareNavigation(regenerated);
    QVERIFY(!difference.comparable);
    QVERIFY(difference.error.contains(QStringLiteral("dimensions differ")));
}

void AiGraphDocumentTests::regenerationRejectsBridgeIntervalsDegenerateAfterQuantization()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 3;
    input.graphHeight = 3;
    input.maximumTerrainHeight = 20.0f;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(6.0f, aiWidth * aiHeight);
    // Both authored faces land at height unit 51. The runtime discards this
    // zero-height placement record instead of turning it into a walkable deck.
    input.collisionProxies.append(boxVolume(
        4.0f, 4.0f, 2.000f, 8.0f, 8.0f, 2.010f,
        EAiGraphObjectPassability::Bridge));

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.bridgeAiTiles, 0);
    QCOMPARE(result.document.weight(3, 1, 1, 6), qint16(-1));
}

void AiGraphDocumentTests::regenerationKeepsNarrowBridgeCorridorInCoarseGraph()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 3;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(6.0f, aiWidth * aiHeight);
    input.collisionProxies.append(boxVolume(
        0.0f, 5.0f, 1.8f, 12.0f, 7.0f, 2.0f,
        EAiGraphObjectPassability::Bridge));

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QVERIFY(result.document.weight(3, 0, 1, 6) >= 0);
    QVERIFY(result.document.weight(3, 1, 1, 6) >= 0);
    const int middleLocalY = result.document.localY(3, 1, 1);
    QVERIFY(middleLocalY >= 2 && middleLocalY <= 6);
}

void AiGraphDocumentTests::regeneratedHumanProjectionUsesPassableBridgeSurface()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 3;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(6.0f, aiWidth * aiHeight);
    input.collisionProxies.append(boxVolume(
        0.0f, 5.0f, 1.8f, 12.0f, 7.0f, 2.0f,
        EAiGraphObjectPassability::Bridge));

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QVector3D projected;
    QVERIFY(result.projectHumanPosition(QPointF(6.0f, 6.0f), &projected, 2.0f));
    QVERIFY(projected.z() > 1.8f);
    QVERIFY(!result.projectHumanPosition(QPointF(6.0f, 0.5f), &projected, 1.0f));
}

void AiGraphDocumentTests::regenerationPreservesAuthoredNarrowObjectOpening()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 3;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.collisionProxies.append(boxVolume(5.5f, 0.0f, 0.0f, 6.5f, 5.0f, 3.0f));
    input.collisionProxies.append(boxVolume(5.5f, 7.0f, 0.0f, 6.5f, 12.0f, 3.0f));

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QVector3D projected;
    QVERIFY(result.projectHumanPosition(QPointF(6.0f, 6.0f), &projected, 0.5f));
    QVERIFY(qAbs(projected.y() - 6.0f) <= 0.5f);
}

void AiGraphDocumentTests::regenerationRasterizesSlopedBridgeHeightsPerTile()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(6.0f, aiWidth * aiHeight);

    SAiGraphCollisionProxy ramp = boxVolume(
        0.0f, 0.0f, 0.0f, 4.0f, 8.0f, 1.1f,
        EAiGraphObjectPassability::Bridge);
    ramp.boxCorners = {
        QVector3D(0.0f, 0.0f, 0.0f), QVector3D(4.0f, 0.0f, 1.0f),
        QVector3D(0.0f, 8.0f, 0.0f), QVector3D(4.0f, 8.0f, 1.0f),
        QVector3D(0.0f, 0.0f, 0.1f), QVector3D(4.0f, 0.0f, 1.1f),
        QVector3D(0.0f, 8.0f, 0.1f), QVector3D(4.0f, 8.0f, 1.1f)
    };
    input.collisionProxies.append(ramp);
    ramp.footprint = {QVector2D(4.0f, 0.0f), QVector2D(8.0f, 0.0f),
                      QVector2D(8.0f, 8.0f), QVector2D(4.0f, 8.0f)};
    ramp.minimumZ = 1.0f;
    ramp.maximumZ = 2.1f;
    ramp.boxCorners = {
        QVector3D(4.0f, 0.0f, 1.0f), QVector3D(8.0f, 0.0f, 2.0f),
        QVector3D(4.0f, 8.0f, 1.0f), QVector3D(8.0f, 8.0f, 2.0f),
        QVector3D(4.0f, 0.0f, 1.1f), QVector3D(8.0f, 0.0f, 2.1f),
        QVector3D(4.0f, 8.0f, 1.1f), QVector3D(8.0f, 8.0f, 2.1f)
    };
    input.collisionProxies.append(ramp);
    const SAiGraphPreviewBuildResult perTile = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(perTile.valid, qPrintable(perTile.error));
    QVERIFY2(perTile.document.weight(3, 0, 0, 6) >= 0,
             qPrintable(QStringLiteral("weight=%1 p0=%2 p1=%3 bridges=%4")
                 .arg(perTile.document.weight(3, 0, 0, 6))
                 .arg(perTile.document.packedPosition(3, 0, 0))
                 .arg(perTile.document.packedPosition(3, 1, 0))
                 .arg(perTile.bridgeAiTiles)));

    // A single maximum for the whole ramp creates a false cliff at its low end.
    input.collisionProxies[0].boxCorners.clear();
    input.collisionProxies[1].boxCorners.clear();
    const SAiGraphPreviewBuildResult uniform = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(uniform.valid, qPrintable(uniform.error));
    QCOMPARE(uniform.document.weight(3, 0, 0, 6), qint16(-1));
}

void AiGraphDocumentTests::regenerationAppliesWaterClearanceToEveryProfile()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(0.0f, aiWidth * aiHeight);
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 15; x <= 17; ++x)
            input.waterDepths[y * aiWidth + x] = 0.1f;

    const SAiGraphPreviewBuildResult shallow = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(shallow.valid, qPrintable(shallow.error));
    QCOMPARE(shallow.document.weight(1, 1, 1, 6), qint16(-1));
    QVERIFY(shallow.document.weight(0, 1, 1, 6) >= 0);
    QVERIFY(shallow.document.weight(2, 1, 1, 6) >= 0);

    for(int y = 0; y < aiHeight; ++y)
        for(int x = 15; x <= 17; ++x)
            input.waterDepths[y * aiWidth + x] = 6.0f;
    const SAiGraphPreviewBuildResult deep = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(deep.valid, qPrintable(deep.error));
    QVERIFY(deep.document.weight(0, 1, 1, 6) >= 0);
    for(int layer = 1; layer < CAiGraphDocument::LayerCount; ++layer)
        QCOMPARE(deep.document.weight(layer, 1, 1, 6), qint16(-1));
}

void AiGraphDocumentTests::regenerationHonorsConfiguredTerrainTraversal()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 3;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.terrainTypes.fill(0, aiWidth * aiHeight);
    input.terrainTraversal.resize(16);
    input.terrainTraversal[0].speed = 1024;
    input.terrainTraversal[0].cost = 1;
    input.terrainTraversal[4].speed = -1;
    input.terrainTraversal[4].cost = 1;
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 15; x <= 17; ++x)
            input.terrainTypes[y * aiWidth + x] = 4;
    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    for(int layer = 0; layer < CAiGraphDocument::LayerCount; ++layer)
        QVERIFY2(result.document.weight(layer, 1, 1, 6) >= 0,
                 qPrintable(QStringLiteral("layer=%1 weight=%2")
                     .arg(layer).arg(result.document.weight(layer, 1, 1, 6))));

    input.terrainTraversal[4].cost = 71;
    const SAiGraphPreviewBuildResult blocked = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(blocked.valid, qPrintable(blocked.error));
    QVERIFY(blocked.document.weight(0, 1, 1, 6) >= 0);
    for(int layer = 1; layer < CAiGraphDocument::LayerCount; ++layer)
        QCOMPARE(blocked.document.weight(layer, 1, 1, 6), qint16(-1));
}

void AiGraphDocumentTests::regenerationMatchesRuntimeWideSlopeMask()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 4;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);

    // A ground-only cliff at x=15/16 clears those slope tiles and one shifted
    // tile to their right in the wide layers. It does not symmetrically dilate
    // onto x=14.
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 16; x < aiWidth; ++x)
            input.terrainHeights[y * aiWidth + x] = 0.5f;

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.layerPassability.size(), int(CAiGraphDocument::LayerCount));
    for(int layer = 5; layer <= 6; ++layer)
    {
        QVERIFY(result.layerPassability.at(layer).at(8 * aiWidth + 14) > 0);
        QCOMPARE(result.layerPassability.at(layer).at(8 * aiWidth + 15), quint8(0));
        QCOMPARE(result.layerPassability.at(layer).at(8 * aiWidth + 17), quint8(0));
    }
}

void AiGraphDocumentTests::regenerationUsesRuntimeHeightRounding()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    const int aiCount = input.graphWidth * input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const float halfStep = 2.5f * input.maximumTerrainHeight / 511.0f;
    input.terrainHeights.fill(halfStep, aiCount);
    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    const float expected = 2.0f * input.maximumTerrainHeight / 511.0f;
    QVERIFY(qAbs(result.navigationSurfaceHeights.front() - expected) < 0.0001f);

    // Preserve the runtime's float operation order. Reassociating this as
    // (height * 511) / maximumTerrainHeight rounds this Zone 9 sample to 110
    // instead of the runtime's 109 and changes coarse representatives/weights.
    input.terrainHeights.fill(4.285714149475098f, aiCount);
    const SAiGraphPreviewBuildResult zone9Sensitive =
        CAiGraphPreviewBuilder::build(input);
    QVERIFY2(zone9Sensitive.valid, qPrintable(zone9Sensitive.error));
    const float zone9Expected = 109.0f * input.maximumTerrainHeight / 511.0f;
    QVERIFY(qAbs(zone9Sensitive.navigationSurfaceHeights.front()
                 - zone9Expected) < 0.0001f);
}

void AiGraphDocumentTests::regenerationUsesRuntimeBridgeHeightRounding()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    input.maximumTerrainHeight = 29.18197250366211f;
    const int aiWidth = input.graphWidth
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.collisionProxies.append(boxVolume(
        1.0f, 1.0f, 8.0f, 2.0f, 2.0f, 9.476203918457031f,
        EAiGraphObjectPassability::Bridge));

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    const int index = 2 * aiWidth + 2;
    const float expected = 166.0f * input.maximumTerrainHeight / 511.0f;
    QVERIFY(qAbs(result.navigationSurfaceHeights.at(index) - expected) < 0.0001f);
}

void AiGraphDocumentTests::quantizesWaterBeforeOverlayTerrainSelection()
{
    const float maximumTerrainHeight = 29.18197250366211f;
    const float oneStep = maximumTerrainHeight / 511.0f;
    QVERIFY(oneStep * 0.25f > 0.0f);
    QCOMPARE(CAiGraphPreviewBuilder::quantizedWaterDepth(
        oneStep * 0.25f, maximumTerrainHeight), 0);
    QCOMPARE(CAiGraphPreviewBuilder::quantizedWaterDepth(
        oneStep * 0.75f, maximumTerrainHeight), 1);
}

void AiGraphDocumentTests::usesRuntimeElevationMultiplierArithmetic()
{
    const float maximumTerrainHeight = 36.70588684082031f;
    const int expected[] = {
        764, 820, 885, 951, 1004, 1024,
        1066, 1186, 1370, 1596, 1840
    };
    for(int delta = -5; delta <= 5; ++delta)
        QCOMPARE(ai_graph_internal::elevationMultiplier(
            delta, maximumTerrainHeight, false), expected[delta + 5]);
}

void AiGraphDocumentTests::regenerationUsesRuntimeCrossingTraversal()
{
    const int aiWidth = 9, aiHeight = 9;
    QVector<quint8> quality(aiWidth * aiHeight, 14);
    QVector<int> edgeCosts(aiWidth * aiHeight
                           * CAiGraphDocument::DirectionCount, -1);
    QVector<bool> crossing = ai_graph_internal::graphCellCrossingCandidates(
        quality, edgeCosts, aiWidth, aiHeight, 0, 0);
    QVERIFY(!crossing.at(3 * 8 + 3));

    int east = -1, west = -1;
    for(int direction = 0; direction < CAiGraphDocument::DirectionCount; ++direction)
    {
        if(CAiGraphDocument::directionDelta(direction) == QPoint(1, 0)) east = direction;
        if(CAiGraphDocument::directionDelta(direction) == QPoint(-1, 0)) west = direction;
    }
    QVERIFY(east >= 0 && west >= 0);
    for(int x = 0; x < aiWidth - 1; ++x)
    {
        edgeCosts[(3 * aiWidth + x) * CAiGraphDocument::DirectionCount + east] = 1024;
        edgeCosts[(3 * aiWidth + x + 1) * CAiGraphDocument::DirectionCount + west] = 1024;
    }
    crossing = ai_graph_internal::graphCellCrossingCandidates(
        quality, edgeCosts, aiWidth, aiHeight, 0, 0);
    QVERIFY(crossing.at(3 * 8 + 3));
}

void AiGraphDocumentTests::regenerationMergesRuntimeObjectBodyParts()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    const int aiWidth = input.graphWidth
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    SAiGraphCollisionProxy lower = boxVolume(
        1.0f, 1.0f, 0.0f, 3.0f, 3.0f, 1.0f,
        EAiGraphObjectPassability::Hard);
    lower.objectId = 42;
    SAiGraphCollisionProxy upper = boxVolume(
        1.0f, 1.0f, 0.5f, 3.0f, 3.0f, 1.5f,
        EAiGraphObjectPassability::Hard);
    upper.objectId = 42;
    input.collisionProxies = {lower, upper};
    const SAiGraphPreviewBuildResult merged = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(merged.valid, qPrintable(merged.error));

    input.collisionProxies[1].objectId = 43;
    const SAiGraphPreviewBuildResult separate = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(separate.valid, qPrintable(separate.error));
    const int index = 3 * aiWidth + 3;
    QVERIFY(merged.layerPassability.at(7).at(index)
            > separate.layerPassability.at(7).at(index));
}

void AiGraphDocumentTests::regenerationMergesRuntimeBridgeBodyParts()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    input.maximumTerrainHeight = 20.0f;
    const int aiWidth = input.graphWidth
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    input.waterDepths.fill(6.0f, aiWidth * aiHeight);

    SAiGraphCollisionProxy complete = boxVolume(
        2.0f, 2.0f, 1.0f, 6.0f, 6.0f, 2.0f,
        EAiGraphObjectPassability::Bridge);
    complete.objectId = 42;
    SAiGraphCollisionProxy upperOnly = boxVolume(
        2.0f, 2.0f, 1.0f, 6.0f, 6.0f, 3.0f,
        EAiGraphObjectPassability::Bridge);
    upperOnly.objectId = 42;
    upperOnly.upperFaceMask = 0x3f;
    input.collisionProxies = {complete, upperOnly};
    const SAiGraphPreviewBuildResult merged = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(merged.valid, qPrintable(merged.error));

    input.collisionProxies[1].objectId = 43;
    const SAiGraphPreviewBuildResult separate = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(separate.valid, qPrintable(separate.error));
    const int index = 6 * aiWidth + 6;
    QVERIFY(merged.navigationSurfaceHeights.at(index)
            > separate.navigationSurfaceHeights.at(index));
}

void AiGraphDocumentTests::regenerationRasterizesAuthoredDegenerateCollisionSlab()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    const int aiWidth = input.graphWidth
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);

    SAiGraphCollisionProxy slab = boxVolume(
        1.0f, 1.0f, 0.0f, 3.0f, 1.0f, 2.0f,
        EAiGraphObjectPassability::Blocking);
    slab.footprint = {QVector2D(1.0f, 1.0f), QVector2D(3.0f, 1.0f)};
    slab.objectId = 99;
    input.collisionProxies.append(slab);

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QVERIFY(result.collisionCoverageTests > 0);
    QCOMPARE(result.layerPassability.at(0).at(2 * aiWidth + 3), quint8(0));
}

void AiGraphDocumentTests::regenerationKeepsEasyFacesForObjectMerge()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    const int aiWidth = input.graphWidth
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);

    SAiGraphCollisionProxy trunk = boxVolume(
        1.0f, 1.0f, 0.0f, 3.0f, 3.0f, 0.2f,
        EAiGraphObjectPassability::Blocking);
    trunk.objectId = 42;
    SAiGraphCollisionProxy crown = boxVolume(
        1.0f, 1.0f, 0.2f, 3.0f, 3.0f, 2.0f,
        EAiGraphObjectPassability::Easy);
    crown.objectId = 42;
    input.collisionProxies = {trunk, crown};
    const SAiGraphPreviewBuildResult merged = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(merged.valid, qPrintable(merged.error));

    input.collisionProxies[1].objectId = 43;
    const SAiGraphPreviewBuildResult separate = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(separate.valid, qPrintable(separate.error));
    const int index = 3 * aiWidth + 3;
    QCOMPARE(merged.layerPassability.at(3).at(index), quint8(0));
    QVERIFY(separate.layerPassability.at(3).at(index) > 0);
}

void AiGraphDocumentTests::regenerationKeepsBlockingLowerFacesForObjectMerge()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 2;
    input.graphHeight = 2;
    input.maximumTerrainHeight = 20.0f;
    const int aiWidth = input.graphWidth
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight
        * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);

    SAiGraphCollisionProxy hard = boxVolume(
        2.0f, 2.0f, 1.0f, 6.0f, 6.0f, 3.0f,
        EAiGraphObjectPassability::Hard);
    hard.objectId = 42;
    SAiGraphCollisionProxy blockingLower = boxVolume(
        2.0f, 2.0f, 0.0f, 6.0f, 6.0f, 2.0f,
        EAiGraphObjectPassability::Blocking);
    blockingLower.objectId = 42;
    // A nonzero mask with no authored face bits classifies every sampled face
    // as a lower face, reproducing a sibling part that contributes only the
    // lower half of the object's combined collision interval at this tile.
    blockingLower.upperFaceMask = 0x40;
    input.collisionProxies = {blockingLower, hard};

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.layerPassability.at(7).at(4 * aiWidth + 4), quint8(0));
}

void AiGraphDocumentTests::collisionMergeKeepsMostRestrictiveContributingPart()
{
    QVERIFY(ai_graph_internal::collisionSampleTakesOwnership(
        false, EAiGraphObjectPassability::Blocking,
        true, EAiGraphObjectPassability::Easy));
    QVERIFY(!ai_graph_internal::collisionSampleTakesOwnership(
        true, EAiGraphObjectPassability::Easy,
        false, EAiGraphObjectPassability::Blocking));
    QVERIFY(ai_graph_internal::collisionSampleTakesOwnership(
        true, EAiGraphObjectPassability::Easy,
        true, EAiGraphObjectPassability::Blocking));
}

void AiGraphDocumentTests::classifiesNearHorizontalBridgeFaceBeforeWorldTranslation()
{
    const QQuaternion rotation(0.437479198f, 0.238110855f,
                               0.121323623f, -0.858601570f);
    const quint8 upper = ei::runtimeNavigationUpperFaceMask(rotation);
    QCOMPARE(upper, quint8((1u << 1) | (1u << 3) | (1u << 5)));

    // This authored bridge is almost exactly edge-on. Corner-plane averages
    // collapse its third and sixth faces to the same float, while the runtime's
    // transformed canonical normal retains the small positive sign on face 3.
    const QQuaternion edgeOn(-0.162391290f, 0.328814894f,
                             0.057505954f, 0.928548813f);
    QCOMPARE(ei::runtimeNavigationUpperFaceMask(edgeOn),
             quint8((1u << 2) | (1u << 3) | (1u << 4)));

    QVector3D first;
    QVector3D opposite;
    ei::runtimeNavigationBounds(
        QVector3D(0.0f, 0.0f, 2.81452703f),
        QVector3D(0.07852548f, 0.07852536f, -2.87352133f),
        QVector3D(-0.07852548f, -0.07852536f, 2.87352133f),
        QVector3D(), &first, &opposite);
    QVERIFY(first.x() > opposite.x());
    QVERIFY(first.y() > opposite.y());
    QVERIFY(first.z() < opposite.z());
}

void AiGraphDocumentTests::regenerationUsesRuntimeRouteWeightScale()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 4;
    input.graphHeight = 4;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);

    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    for(int layer = 0; layer < CAiGraphDocument::LayerCount; ++layer)
    {
        QCOMPARE(result.document.weight(layer, 1, 1, 6), qint16(64));
        QCOMPARE(result.document.weight(layer, 1, 1, 5), qint16(90));
    }
}

void AiGraphDocumentTests::regenerationIndexesCollisionCoverageOnce()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 8;
    input.graphHeight = 8;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    for(int index = 0; index < 200; ++index)
    {
        const float x = float((index * 7) % 50) * 0.5f;
        const float y = float((index * 11) % 50) * 0.5f;
        input.collisionProxies.append(boxVolume(
            x, y, 0.0f, x + 0.4f, y + 0.4f, 2.0f));
    }
    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.collisionProxyCount, input.collisionProxies.size());
    QVERIFY(result.collisionCoverageTests > 0);
    // The legacy all-proxy/all-tile/eight-layer loop would execute more than
    // six million polygon tests here. Bounded one-time indexing stays local.
    QVERIFY2(result.collisionCoverageTests < 5000,
             qPrintable(QString::number(result.collisionCoverageTests)));
}

void AiGraphDocumentTests::regenerationHandlesPopulatedMapScale()
{
    SAiGraphPreviewBuildInput input;
    input.graphWidth = 88;
    input.graphHeight = 72;
    const int aiWidth = input.graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input.terrainHeights.fill(0.0f, aiWidth * aiHeight);
    for(int index = 0; index < 2266; ++index)
    {
        const float x = float((index * 37) % (aiWidth - 4)) * 0.5f;
        const float y = float((index * 53) % (aiHeight - 4)) * 0.5f;
        input.collisionProxies.append(boxVolume(
            x, y, 0.0f, x + 0.8f, y + 0.8f, 2.0f));
    }
    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.document.width(), input.graphWidth);
    QCOMPARE(result.document.height(), input.graphHeight);
    QCOMPARE(result.collisionProxyCount, input.collisionProxies.size());
    QVERIFY2(result.collisionCoverageTests < 100000,
             qPrintable(QString::number(result.collisionCoverageTests)));
    QVERIFY(result.routeSearchEarlyExits > 0);
    const qint64 exhaustiveSearchBound = qint64(input.graphWidth)
        * input.graphHeight * CAiGraphDocument::LayerCount * 625;
    QVERIFY2(result.routeQueueSettledTiles < exhaustiveSearchBound * 4 / 5,
             qPrintable(QString::number(result.routeQueueSettledTiles)));
}

void AiGraphDocumentTests::parsesProfileTerrainTraversalRegistry()
{
    QString error;
    const QVector<SAiTerrainTraversal> traversal =
        CAiGraphPreviewBuilder::parseTerrainTraversalRegistry(
            syntheticTraversalRegistry(), &error);
    QVERIFY2(!traversal.isEmpty(), qPrintable(error));
    QCOMPARE(traversal.size(), 16);
    QCOMPARE(traversal.at(4).speed, 700);
    QCOMPARE(traversal.at(4).cost, 50);
    QCOMPARE(traversal.at(3).speed, -1);
}

void AiGraphDocumentTests::sourceSwitchOwnsRegenerationLifecycle()
{
    const SAiGraphPreviewSourceTransition loaded =
        aiGraphPreviewSourceTransition(true, true);
    QVERIFY(!loaded.enableAutomaticRegeneration);
    QVERIFY(loaded.cancelPendingRegeneration);
    QVERIFY(!loaded.queueRegeneration);

    const SAiGraphPreviewSourceTransition freshGenerated =
        aiGraphPreviewSourceTransition(false, true);
    QVERIFY(freshGenerated.enableAutomaticRegeneration);
    QVERIFY(!freshGenerated.cancelPendingRegeneration);
    QVERIFY(!freshGenerated.queueRegeneration);

    const SAiGraphPreviewSourceTransition staleGenerated =
        aiGraphPreviewSourceTransition(false, false);
    QVERIFY(staleGenerated.enableAutomaticRegeneration);
    QVERIFY(!staleGenerated.cancelPendingRegeneration);
    QVERIFY(staleGenerated.queueRegeneration);
}

void AiGraphDocumentTests::invalidationClassifiesOnlyGraphBuilderInputs()
{
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eWorldObject, eObjParam_POSITION));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eTorch, eObjParam_ROTATION_Z));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eLever, eObjParam_COMPLECTION_X));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eWorldObject, eObjParam_TEMPLATE));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eWorldObject, eObjParam_BODYPARTS));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eWorldObject, eObjParam_NID));

    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eUnit, eObjParam_POSITION));
    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eMagicTrap, eObjParam_ROTATION));
    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eWorldObject, eObjParam_COMMENTS));
    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eLever, eObjParam_LEVER_CUR_STATE));
    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eLever, eObjParam_LEVER_RECALC_GRAPH));
}

void AiGraphDocumentTests::invalidationTraversesUndoMacros()
{
    QUndoCommand unrelated;
    new TestAiGraphUndoCommand(false, &unrelated);
    QVERIFY(!aiGraphUndoCommandInvalidatesPreview(&unrelated));

    QUndoCommand mixed;
    new TestAiGraphUndoCommand(false, &mixed);
    QUndoCommand* nested = new QUndoCommand(&mixed);
    new TestAiGraphUndoCommand(true, nested);
    QVERIFY(aiGraphUndoCommandInvalidatesPreview(&mixed));
    QVERIFY(!aiGraphUndoCommandInvalidatesPreview(nullptr));
}

void AiGraphDocumentTests::staticObjectNidInvalidationIsScoped()
{
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eWorldObject, eObjParam_NID));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eTorch, eObjParam_NID));
    QVERIFY(aiGraphObjectPropertyInvalidatesPreview(
        eLever, eObjParam_NID));
    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eUnit, eObjParam_NID));
    QVERIFY(!aiGraphObjectPropertyInvalidatesPreview(
        eMagicTrap, eObjParam_NID));
}

QTEST_APPLESS_MAIN(AiGraphDocumentTests)
#include "ai_graph_document_tests.moc"

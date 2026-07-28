#include <QtTest>
#include <QDataStream>
#include <QtEndian>

#include "ai_graph_document.h"
#include "ai_graph_preview_builder.h"
#include "../source/core/map_object_placement_policy.h"

namespace {
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
    void resolvesTerrainRelativeStaticObjectPlacement();
    void preservesNoOpBytesIncludingTrailingData();
    void rejectsMalformedDimensionsAndTruncation();
    void exposesStableLayerAndDirectionDefinitions();
    void initializesDecodedPreviewWithSerializableBytes();
    void generatedGraphMeetsRuntimeStagingContract();
    void regenerationRespectsFenceAndVerticalClearanceProfiles();
    void regenerationRejectsGameScaleTerrainCliffs();
    void regenerationAppliesWaterClearanceToEveryProfile();
    void regenerationUsesBridgeSurfaceAndObjectSemantics();
    void regenerationRejectsSideOnlyBridgeFringeCells();
    void regenerationKeepsNarrowBridgeCorridorInCoarseGraph();
    void regeneratedHumanProjectionUsesPassableBridgeSurface();
    void regenerationPreservesAuthoredNarrowObjectOpening();
    void regenerationRasterizesSlopedBridgeHeightsPerTile();
    void regenerationHonorsConfiguredTerrainTraversal();
    void regenerationUsesRuntimeRouteWeightScale();
    void regenerationIndexesCollisionCoverageOnce();
    void regenerationHandlesPopulatedMapScale();
    void parsesProfileTerrainTraversalRegistry();
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

    SAiGraphCollisionProxy bridge = boxVolume(
        7.4f, 0.0f, -0.2f, 8.6f, 12.0f, 0.0f,
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
    QVERIFY(result.document.weight(0, 1, 1, 6) >= 0);
    for(int layer = 1; layer < CAiGraphDocument::LayerCount; ++layer)
        QCOMPARE(result.document.weight(layer, 1, 1, 6), qint16(-1));
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

QTEST_APPLESS_MAIN(AiGraphDocumentTests)
#include "ai_graph_document_tests.moc"

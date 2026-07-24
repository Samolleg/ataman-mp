#include <QtTest>
#include <QDataStream>

#include "ai_graph_document.h"
#include "ai_graph_preview_builder.h"

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
}

class AiGraphDocumentTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesKindMajorPlanesAndSignedValues();
    void preservesNoOpBytesIncludingTrailingData();
    void rejectsMalformedDimensionsAndTruncation();
    void exposesStableLayerAndDirectionDefinitions();
    void initializesDecodedPreviewWithoutSerializableBytes();
    void regenerationRespectsFenceAndVerticalClearanceProfiles();
    void regenerationRejectsGameScaleTerrainCliffs();
};

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

void AiGraphDocumentTests::initializesDecodedPreviewWithoutSerializableBytes()
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
    QVERIFY(document.serializeNoOp().isEmpty());
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

    SAiGraphCollisionProxy fence;
    fence.minimum = QVector3D(7.8f, 0.0f, 0.0f);
    fence.maximum = QVector3D(8.2f, 12.0f, 2.5f);
    fence.objectId = 568;
    SAiGraphPreviewBuildInput fenced = flat;
    fenced.collisionProxies.append(fence);
    const SAiGraphPreviewBuildResult blocked = CAiGraphPreviewBuilder::build(fenced);
    QVERIFY2(blocked.valid, qPrintable(blocked.error));
    QCOMPARE(blocked.collisionProxyCount, 1);
    QCOMPARE(blocked.document.weight(0, 1, 1, 6), qint16(-1));
    QCOMPARE(blocked.document.weight(3, 1, 1, 6), qint16(-1));

    SAiGraphCollisionProxy overhead = fence;
    overhead.minimum.setZ(2.0f);
    overhead.maximum.setZ(3.0f);
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

QTEST_APPLESS_MAIN(AiGraphDocumentTests)
#include "ai_graph_document_tests.moc"

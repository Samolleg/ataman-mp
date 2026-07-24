#include <QtTest>
#include <QDataStream>

#include "ai_graph_document.h"

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

QTEST_APPLESS_MAIN(AiGraphDocumentTests)
#include "ai_graph_document_tests.moc"

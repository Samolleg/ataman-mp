#include <QtTest>
#include <QtEndian>

#include "mmp_inspector.h"

namespace {
void putDword(QByteArray* bytes, int offset, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar*>(bytes->data() + offset));
}

QByteArray header(int width, int height, int mipValue, quint32 format)
{
    QByteArray bytes(76, '\0');
    putDword(&bytes, 0, 0x00504d4d);
    putDword(&bytes, 4, quint32(width));
    putDword(&bytes, 8, quint32(height));
    putDword(&bytes, 12, quint32(mipValue));
    putDword(&bytes, 16, format);
    return bytes;
}
}

class MmpInspectorTests : public QObject
{
    Q_OBJECT
private slots:
    void inspectsDxtMipChainAndPixels();
    void treatsPnt3HeaderValueAsPayloadSize();
    void rejectsTruncatedMipChain();
    void rejectsUnboundedMipCounts();
    void dimensionBoundSupportsAllDxtFormats();
};

void MmpInspectorTests::inspectsDxtMipChainAndPixels()
{
    QByteArray bytes = header(4, 4, 1, 0x31545844);
    QByteArray block(8, '\0');
    block[0] = char(0x00);
    block[1] = char(0xf8); // RGB565 red
    bytes += block;
    const ei::SMmpInspection result = ei::inspectMmp(bytes);
    QVERIFY2(result.valid, qPrintable(result.diagnostic));
    QCOMPARE(result.formatName, QString("DXT1"));
    QCOMPARE(result.authoredMipSizes, QVector<QSize>({QSize(4, 4)}));
    QCOMPARE(result.baseImage.pixelColor(0, 0), QColor(255, 0, 0, 255));
    QCOMPARE(result.sha256.size(), 64);
}

void MmpInspectorTests::treatsPnt3HeaderValueAsPayloadSize()
{
    QByteArray literal(16, '\0');
    literal[0] = char(0x20);
    literal[1] = char(0x40);
    literal[2] = char(0x80);
    literal[3] = char(0xff);
    QByteArray bytes = header(2, 2, literal.size(), 0x33544e50) + literal;
    const ei::SMmpInspection result = ei::inspectMmp(bytes);
    QVERIFY2(result.valid, qPrintable(result.diagnostic));
    QCOMPARE(result.formatName, QString("PNT3"));
    QCOMPARE(result.headerMipValue, 16);
    QCOMPARE(result.authoredMipSizes.size(), 1);
    QCOMPARE(result.baseImage.pixelColor(0, 0), QColor(128, 64, 32, 255));
}

void MmpInspectorTests::rejectsTruncatedMipChain()
{
    const ei::SMmpInspection result = ei::inspectMmp(
        header(8, 8, 3, 0x31545844) + QByteArray(8, '\0'));
    QVERIFY(!result.valid);
    QVERIFY(result.diagnostic.contains("truncated"));
}

void MmpInspectorTests::rejectsUnboundedMipCounts()
{
    for(int payloadBytes : {0, 8})
    for(quint32 count : {0u, 2u, 1000000u, 0xffffffffu})
    {
        auto bytes=header(1,1,1,0x31545844)+QByteArray(payloadBytes,'\0');
        putDword(&bytes,12,count);
        const auto result=ei::inspectMmp(bytes);
        QVERIFY(!result.valid);
        QVERIFY(result.authoredMipSizes.isEmpty());
        QVERIFY(!result.diagnostic.isEmpty());
    }
}

void MmpInspectorTests::dimensionBoundSupportsAllDxtFormats()
{
    for(quint32 format : {0x31545844u,0x32545844u,0x33545844u,0x34545844u,0x35545844u})
    {
        const int blockBytes=format==0x31545844u?8:16;
        auto bytes=header(8,4,4,format)+QByteArray(5*blockBytes,'\0');
        const auto result=ei::inspectMmp(bytes);
        QVERIFY2(result.valid,qPrintable(result.diagnostic));
        QCOMPARE(result.authoredMipSizes,QVector<QSize>({{8,4},{4,2},{2,1},{1,1}}));
        QVERIFY(!ei::inspectMmp(bytes.left(bytes.size()-1)).valid);
        putDword(&bytes,12,5);
        QVERIFY(!ei::inspectMmp(bytes+QByteArray(blockBytes,'\0')).valid);
        putDword(&bytes,12,1);
        QVERIFY(ei::inspectMmp(bytes.left(76+2*blockBytes)).valid);
    }
    const auto rectangle=header(3,5,3,0x31545844)+QByteArray(4*8,'\0');
    QVERIFY(ei::inspectMmp(rectangle).valid);
}

QTEST_APPLESS_MAIN(MmpInspectorTests)
#include "mmp_inspector_tests.moc"

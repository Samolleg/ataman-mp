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

QTEST_APPLESS_MAIN(MmpInspectorTests)
#include "mmp_inspector_tests.moc"

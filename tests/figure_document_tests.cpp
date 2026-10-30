#include <QtTest>
#include <QtEndian>
#include "figure.h"

namespace {
QByteArray figureBytes(bool empty = false) {
    QByteArray bytes;
    QDataStream out(&bytes,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << quint32(0x38474946);
    for(int n : {empty?0:1,empty?0:1,empty?0:1,empty?0:3,empty?0:1,0,0,0,0}) out<<qint32(n);
    for(int i=0;i<80;++i)out<<float(0);
    if(!empty) {
        for(int i=0;i<96+16+2;++i)out<<float(1);
        for(int i=0;i<6;++i)out<<quint16(0);
    }
    return bytes;
}
void dword(QByteArray& bytes,int offset,quint32 value) {
    qToLittleEndian(value,reinterpret_cast<uchar*>(bytes.data()+offset));
}
bool load(ei::CFigure& figure,const QByteArray& bytes) {
    QDataStream in(bytes);in.setByteOrder(QDataStream::LittleEndian);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);
    return figure.readData(in);
}
int vertices(ei::CFigure& figure) {
    QVector<CPart*> parts;QVector3D complexion(.5f,.5f,.5f);QList<QString> visible;
    figure.getVertexData(parts,complexion,visible,false);
    int count=0;for(auto* part:parts)count+=part->vertData().size();qDeleteAll(parts);return count;
}
}
class FigureDocumentTests:public QObject {
    Q_OBJECT
private slots:
    void validAndRepeatedRead() {
        ei::CFigure figure;QVERIFY(load(figure,figureBytes()));QVERIFY(figure.completeCaptureGeometry());
        QCOMPARE(vertices(figure),3);QVERIFY(load(figure,figureBytes()));QCOMPARE(vertices(figure),3);
    }
    void invalidIndexNeverReachesGeometry() {
        auto bytes=figureBytes();qToLittleEndian<quint16>(65535,reinterpret_cast<uchar*>(bytes.data()+816));
        ei::CFigure figure;QVERIFY(!load(figure,bytes));QVERIFY(!figure.completeCaptureGeometry());
        QCOMPARE(vertices(figure),0);
        QVERIFY(load(figure,figureBytes()));QCOMPARE(vertices(figure),3);
    }
    void rejectsTruncationAndCountsBeforeAllocation() {
        ei::CFigure figure;const auto valid=figureBytes();
        for(int length:{0,4,39,40,359,827}){QVERIFY(!load(figure,valid.left(length)));QCOMPARE(vertices(figure),0);}
        for(int field:{4,8,12,16,20,24}) for(quint32 value:{0xffffffffu,0x7fffffffu}) {
            auto bytes=valid;dword(bytes,field,value);QVERIFY(!load(figure,bytes));QCOMPARE(vertices(figure),0);
        }
    }
    void rejectsInvalidComponentsAndNonfiniteGeometry() {
        for(int field:{822,824,826}) {
            auto bytes=figureBytes();qToLittleEndian<quint16>(65535,reinterpret_cast<uchar*>(bytes.data()+field));
            ei::CFigure figure;QVERIFY(!load(figure,bytes));QCOMPARE(vertices(figure),0);
        }
        auto bytes=figureBytes();dword(bytes,360,0x7f800000u);
        ei::CFigure figure;QVERIFY(!load(figure,bytes));QCOMPARE(vertices(figure),0);
    }
    void emptyTransformRootKeepsChildren() {
        ei::CFigure root,child;QVERIFY(load(root,figureBytes(true)));QVERIFY(root.completeCaptureGeometry());
        QCOMPARE(vertices(root),0);QVERIFY(load(child,figureBytes()));root.addChild(&child);
        QVERIFY(root.completeCaptureGeometry());QCOMPARE(vertices(root),3);
    }
};
QTEST_APPLESS_MAIN(FigureDocumentTests)
#include "figure_document_tests.moc"

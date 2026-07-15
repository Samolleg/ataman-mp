#include <QtTest>
#include <QTemporaryFile>

#include "endurance_document.h"

class EnduranceDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void unchangedMobIsByteIdentical();
    void duplicateMobIdsAreWarningsOnly();
    void unchangedMprIsByteIdentical();
};

void EnduranceDocumentTests::unchangedMobIsByteIdentical()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QByteArray bytes(32, '\0');
    bytes.append("NewObject - 42");
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    CValidationReport report;
    QVERIFY(document.load(QFileInfo(file.fileName()), &report));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!document.isDirty());
}

void EnduranceDocumentTests::duplicateMobIdsAreWarningsOnly()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QByteArray bytes(32, '\0');
    bytes.append("NewObject - 1000100\0NewObject - 1000100\0NewObject - 7");
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    CValidationReport report;
    QVERIFY(document.load(QFileInfo(file.fileName()), &report));
    QCOMPARE(document.duplicateIds().size(), 1);
    QCOMPARE(document.duplicateIds().front(), quint32(1000100));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!report.hasErrors());
}

void EnduranceDocumentTests::unchangedMprIsByteIdentical()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x019CE23C) << quint32(0) << quint32(16) << quint32(0);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMprDocument document;
    CValidationReport report;
    QVERIFY(document.load(QFileInfo(file.fileName()), &report));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!document.isDirty());
}

QTEST_APPLESS_MAIN(EnduranceDocumentTests)
#include "endurance_document_tests.moc"

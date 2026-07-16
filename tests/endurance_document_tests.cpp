#include <QtTest>
#include <QTemporaryFile>
#include <QTemporaryDir>

#include "endurance_document.h"
#include "staged_save_validator.h"

namespace {
QByteArray section(quint32 type, const QByteArray& payload = QByteArray())
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << type << quint32(8 + payload.size());
    bytes.append(payload);
    return bytes;
}

QByteArray u32(quint32 value)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << value;
    return bytes;
}

QByteArray plot(float x, float y, float z)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << x << y << z;
    return bytes;
}

QByteArray object(quint32 nid, const QByteArray& name = "test", float x = 0.0f)
{
    QByteArray payload;
    payload += section(0xB002, u32(nid));
    payload += section(0xB003, u32(1));
    payload += section(0xB004, name);
    payload += section(0xB009, plot(x, 2.0f, 3.0f));
    return section(0xB001, payload);
}

QByteArray validMob(const QByteArray& rootChild = QByteArray())
{
    return section(0xA000, rootChild);
}

QByteArray emptyMpr()
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x019CE23C) << quint32(0) << quint32(16) << quint32(0);
    return bytes;
}
}

class EnduranceDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void unchangedMobIsByteIdentical();
    void duplicateMobIdsAreWarningsOnly();
    void unknownLeafIsPreservedAsOpaque();
    void malformedMobRootIsRejected();
    void semanticDiffClassifiesMovementAndAddition();
    void transactionPatchesOnlyAuthorizedLeaf();
    void transactionPreservesEmptyContainers();
    void projectionDeltaPreservesRawSourceStructure();
    void projectionDeltaRejectsStructuralEdits();
    void projectionDeltaRejectsMissingSourceRecord();
    void projectionDeltaRejectsCanonicalizedSourceLeaf();
    void transactionRejectsStaleProvenance();
    void cp1251EncodingIsStrict();
    void unchangedMprIsByteIdentical();
    void malformedMprTableIsRejected();
    void stagedMobIsReopenedWithoutCreatingDestination();
};

void EnduranceDocumentTests::unchangedMobIsByteIdentical()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray bytes = validMob(section(0x12345678, "opaque"));
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
    const QByteArray bytes = validMob(section(0xB000,
        object(1000100, "first") + object(1000100, "second") + object(7, "third")));
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

void EnduranceDocumentTests::unknownLeafIsPreservedAsOpaque()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray unknown = section(0xDEADBEEF, "uninterpreted payload");
    const QByteArray bytes = validMob(unknown);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    CValidationReport report;
    QVERIFY(document.load(QFileInfo(file.fileName()), &report));
    QCOMPARE(document.sections().front().children.size(), 1);
    const SMobSection& leaf = document.sections().front().children.front();
    QVERIFY(leaf.opaque);
    QCOMPARE(leaf.rawBytes, unknown);
    QCOMPARE(document.serialize(), bytes);
}

void EnduranceDocumentTests::malformedMobRootIsRejected()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QByteArray bytes = validMob(section(0x12345678, "payload"));
    bytes[4] = 8; // declared root length no longer matches the file size
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    CValidationReport report;
    QVERIFY(!document.load(QFileInfo(file.fileName()), &report));
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::semanticDiffClassifiesMovementAndAddition()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile baselineFile(dir.filePath("baseline.mob"));
    QFile candidateFile(dir.filePath("candidate.mob"));
    QVERIFY(baselineFile.open(QIODevice::WriteOnly));
    baselineFile.write(validMob(section(0xB000, object(42, "boar", 1.0f))));
    baselineFile.close();
    QVERIFY(candidateFile.open(QIODevice::WriteOnly));
    candidateFile.write(validMob(section(0xB000,
        object(42, "boar", 5.0f) + object(43, "human", 2.0f))));
    candidateFile.close();

    CMobDocument baseline;
    CMobDocument candidate;
    QVERIFY(baseline.load(QFileInfo(baselineFile.fileName())));
    QVERIFY(candidate.load(QFileInfo(candidateFile.fileName())));
    const CMobSemanticDiff diff = CMobSemanticDiff::compare(baseline, candidate);
    int moved = 0;
    int added = 0;
    for(const SMobDiffEntry& entry : diff.entries())
    {
        moved += entry.kind == EMobDiffKind::Moved ? 1 : 0;
        added += entry.kind == EMobDiffKind::Added ? 1 : 0;
    }
    QVERIFY2(moved == 1, qPrintable(diff.toText()));
    QCOMPARE(added, 1);
}

void EnduranceDocumentTests::transactionPatchesOnlyAuthorizedLeaf()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray untouched = section(0x11111111, "untouched");
    const QByteArray editable = section(0x22222222, "before");
    const QByteArray bytes = validMob(untouched + editable);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    QVERIFY(document.load(QFileInfo(file.fileName())));
    const SMobSection& leaf = document.sections().front().children.at(1);
    CMobEditTransaction transaction("test leaf edit");
    transaction.replaceLeafPayload(leaf.path, leaf.sha256, "after-value");
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(document.applyTransaction(transaction, &candidate, &report), qPrintable(report.toText()));
    QCOMPARE(candidate, validMob(untouched + section(0x22222222, "after-value")));
    QVERIFY(report.toText().contains("independently verified"));

    QTemporaryFile candidateFile;
    QVERIFY(candidateFile.open());
    candidateFile.write(candidate);
    candidateFile.flush();
    CMobDocument reparsed;
    QVERIFY(reparsed.load(QFileInfo(candidateFile.fileName())));
}

void EnduranceDocumentTests::transactionPreservesEmptyContainers()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray emptyDirectory = section(0xE000);
    const QByteArray emptyDirectoryElements = section(0xE001);
    const QByteArray editable = section(0x22222222, "before");
    const QByteArray bytes = validMob(emptyDirectory + emptyDirectoryElements + editable);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    QVERIFY(document.load(QFileInfo(file.fileName())));
    const SMobSection& leaf = document.sections().front().children.at(2);
    CMobEditTransaction transaction("edit beside empty structural sections");
    transaction.replaceLeafPayload(leaf.path, leaf.sha256, "after");
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(document.applyTransaction(transaction, &candidate, &report), qPrintable(report.toText()));
    QCOMPARE(candidate, validMob(emptyDirectory + emptyDirectoryElements +
                                 section(0x22222222, "after")));
}

void EnduranceDocumentTests::projectionDeltaPreservesRawSourceStructure()
{
    const QByteArray emptyDirectory = section(0xE000);
    const QByteArray unknown = section(0xDEADBEEF, "opaque-source-data");
    const QByteArray sourceBytes = validMob(emptyDirectory + unknown +
                                            section(0x22222222, "clean-canonical"));
    const QByteArray cleanBytes = validMob(section(0x22222222, "clean-canonical"));
    const QByteArray editedBytes = validMob(section(0x22222222, "edited-value"));

    CMobDocument source;
    CMobDocument clean;
    CMobDocument edited;
    QVERIFY(source.loadBytes(sourceBytes, "source.mob"));
    QVERIFY(clean.loadBytes(cleanBytes, "clean-projection.mob"));
    QVERIFY(edited.loadBytes(editedBytes, "edited-projection.mob"));

    CMobEditTransaction transaction("shadow projection delta");
    CValidationReport report;
    QVERIFY2(source.deriveLeafTransaction(clean, edited, &transaction, &report),
             qPrintable(report.toText()));
    QCOMPARE(transaction.edits().size(), 1);

    QByteArray candidate;
    QVERIFY2(source.applyTransaction(transaction, &candidate, &report),
             qPrintable(report.toText()));
    QCOMPARE(candidate, validMob(emptyDirectory + unknown +
                                 section(0x22222222, "edited-value")));
}

void EnduranceDocumentTests::projectionDeltaRejectsStructuralEdits()
{
    const QByteArray sourceBytes = validMob(section(0xB000, object(42, "boar")));
    const QByteArray editedBytes = validMob(section(0xB000,
        object(42, "boar") + object(43, "wolf")));
    CMobDocument source;
    CMobDocument clean;
    CMobDocument edited;
    QVERIFY(source.loadBytes(sourceBytes, "source.mob"));
    QVERIFY(clean.loadBytes(sourceBytes, "clean-projection.mob"));
    QVERIFY(edited.loadBytes(editedBytes, "edited-projection.mob"));

    CMobEditTransaction transaction("unsupported structural delta");
    CValidationReport report;
    QVERIFY(!source.deriveLeafTransaction(clean, edited, &transaction, &report));
    QVERIFY(transaction.isEmpty());
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::projectionDeltaRejectsMissingSourceRecord()
{
    CMobDocument source;
    CMobDocument clean;
    CMobDocument edited;
    QVERIFY(source.loadBytes(validMob(section(0x11111111, "source")), "source.mob"));
    QVERIFY(clean.loadBytes(validMob(section(0x22222222, "clean")), "clean-projection.mob"));
    QVERIFY(edited.loadBytes(validMob(section(0x22222222, "edited")), "edited-projection.mob"));

    CMobEditTransaction transaction("missing source provenance");
    CValidationReport report;
    QVERIFY(!source.deriveLeafTransaction(clean, edited, &transaction, &report));
    QVERIFY(transaction.isEmpty());
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::projectionDeltaRejectsCanonicalizedSourceLeaf()
{
    CMobDocument source;
    CMobDocument clean;
    CMobDocument edited;
    QVERIFY(source.loadBytes(validMob(section(0x22222222, "raw-source")), "source.mob"));
    QVERIFY(clean.loadBytes(validMob(section(0x22222222, "legacy-clean")), "clean-projection.mob"));
    QVERIFY(edited.loadBytes(validMob(section(0x22222222, "edited")), "edited-projection.mob"));

    CMobEditTransaction transaction("canonicalized source must not be guessed");
    CValidationReport report;
    QVERIFY(!source.deriveLeafTransaction(clean, edited, &transaction, &report));
    QVERIFY(transaction.isEmpty());
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::transactionRejectsStaleProvenance()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray bytes = validMob(section(0x22222222, "before"));
    file.write(bytes);
    file.flush();

    CMobDocument document;
    QVERIFY(document.load(QFileInfo(file.fileName())));
    const SMobSection& leaf = document.sections().front().children.front();
    CMobEditTransaction transaction("stale edit");
    transaction.replaceLeafPayload(leaf.path, QByteArray(32, '\x7f'), "after");
    QByteArray candidate;
    CValidationReport report;
    QVERIFY(!document.applyTransaction(transaction, &candidate, &report));
    QVERIFY(candidate.isEmpty());
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::cp1251EncodingIsStrict()
{
    QByteArray encoded;
    QString error;
    QVERIFY(CMobDocument::encodeCp1251Strict(QString::fromUtf8("Привет"), &encoded, &error));
    QVERIFY(!encoded.isEmpty());
    QVERIFY(!CMobDocument::encodeCp1251Strict(QString::fromUtf8("狼"), &encoded, &error));
    QVERIFY(!error.isEmpty());
}

void EnduranceDocumentTests::unchangedMprIsByteIdentical()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray bytes = emptyMpr();
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMprDocument document;
    CValidationReport report;
    QVERIFY(document.load(QFileInfo(file.fileName()), &report));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!document.isDirty());
}

void EnduranceDocumentTests::malformedMprTableIsRejected()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QByteArray bytes = emptyMpr();
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x019CE23C) << quint32(1) << quint32(16) << quint32(0);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMprDocument document;
    CValidationReport report;
    QVERIFY(!document.load(QFileInfo(file.fileName()), &report));
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::stagedMobIsReopenedWithoutCreatingDestination()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QFileInfo destination(dir.filePath("test.mob"));
    const QByteArray bytes = validMob(section(0x12345678, "payload!"));
    const SStagedSaveValidation result = validateStagedSave(bytes, destination);
    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.size, qint64(bytes.size()));
    QVERIFY(!destination.exists());
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0);
}

QTEST_APPLESS_MAIN(EnduranceDocumentTests)
#include "endurance_document_tests.moc"

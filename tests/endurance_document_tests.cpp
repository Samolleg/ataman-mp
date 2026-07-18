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

QByteArray lookPoint(float x)
{
    return section(0xBBBE0000,
        section(0xBBBE0001, plot(x, 2.0f, 3.0f)) +
        section(0xBBBE0002, u32(0)) +
        section(0xBBBE0003, u32(0)) +
        section(0xBBBE0004, QByteArray(1, '\0')));
}

QByteArray patrolPoint(float x, const QByteArray& looks = QByteArray())
{
    return section(0xBBBD0000,
        section(0xBBBD0001, plot(x, 2.0f, 3.0f)) + looks);
}

QByteArray validMob(const QByteArray& rootChild = QByteArray())
{
    return section(0xA000, rootChild);
}

QByteArray mapEdCountedArray(quint32 type, quint32 count, const QByteArray& elements)
{
    QByteArray bytes = section(type, u32(count) + elements);
    const quint32 shortLength = quint32(bytes.size() - 4);
    bytes[4] = char(shortLength & 0xFF);
    bytes[5] = char((shortLength >> 8) & 0xFF);
    bytes[6] = char((shortLength >> 16) & 0xFF);
    bytes[7] = char((shortLength >> 24) & 0xFF);
    return bytes;
}

QByteArray stringArray(quint32 type, const QStringList& values)
{
    QByteArray data = u32(quint32(values.size()));
    for(const QString& value : values)
        data += section(type, value.toLatin1());
    return section(type, data);
}

QByteArray encryptedScript(const QByteArray& script, quint32 key = 0x12345678)
{
    QByteArray encrypted = u32(key);
    for(char character : script)
    {
        quint32 temporaryKey = ((((key * 13) << 4) + key) << 8) - key;
        key += (temporaryKey << 2) + 2531011;
        temporaryKey = key >> 16;
        encrypted.append(char(character ^ char(temporaryKey)));
    }
    return section(0xACCEECCB, encrypted);
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
    void legacyMapEdTrapArrayLengthsArePreserved();
    void semanticAnomaliesAreWarningsOnly();
    void semanticDiffClassifiesMovementAndAddition();
    void semanticDiffPairsIdenticalDuplicatesByOccurrence();
    void semanticDiffReportsReadableScriptChanges();
    void embeddedScriptReplacementPreservesOtherLeaves();
    void transactionPatchesOnlyAuthorizedLeaf();
    void transactionPreservesEmptyContainers();
    void objectSubtreeDuplicateAndDeleteAreExact();
    void objectSubtreeRejectsStaleProvenance();
    void objectSequenceRebuildIsExactAndOrdered();
    void copiedObjectSequenceAcceptsOnlyIntendedLeafEdits();
    void patrolAndLookPointStructureIsReconciledExactly();
    void projectionDeltaPreservesRawSourceStructure();
    void projectionDeltaRejectsStructuralEdits();
    void projectionDeltaRejectsMissingSourceRecord();
    void projectionDeltaRejectsCanonicalizedSourceLeaf();
    void projectionSaveCandidateIsProvenanceChecked();
    void topologyPreservingLeafEditsAreAuthorizedTogether();
    void projectionSaveCandidateRejectsStructuralEdit();
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

void EnduranceDocumentTests::legacyMapEdTrapArrayLengthsArePreserved()
{
    const QByteArray areas = mapEdCountedArray(
        0xBBAB0003, 2, plot(83.988685f, 53.219536f, 4.0f) +
                            plot(86.446106f, 58.720936f, 5.0f));
    const QByteArray targets = mapEdCountedArray(
        0xBBAB0004, 1, plot(84.295219f, 55.768169f, 0.0f).left(8));
    const QByteArray bytes = validMob(section(
        0xB000, section(0xBBAB0000, areas + targets)));

    CMobDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(bytes, "maped-trap-arrays.mob", &report),
             qPrintable(report.toText()));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!report.hasErrors());
    int compatibilityWarnings = 0;
    for(const SValidationFinding& finding : report.findings())
    {
        if(finding.code == "mob.compat.maped_array_length")
            ++compatibilityWarnings;
    }
    QCOMPARE(compatibilityWarnings, 2);
}

void EnduranceDocumentTests::semanticAnomaliesAreWarningsOnly()
{
    const QByteArray emptyTargets = section(0xBBAB0004, u32(0));
    const QByteArray overlappingParts = stringArray(
        0xB00D, QStringList() << "hd.armor00" << "hd.armor01" << "bd");
    const QByteArray unitObject = section(
        0xB001, section(0xB002, u32(42)) + section(0xBBBB0000, QByteArray()) +
                    overlappingParts);
    const QByteArray bytes = validMob(
        section(0xB000, unitObject) + section(0xBBAB0000, emptyTargets));

    CMobDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(bytes, "semantic-warnings.mob", &report),
             qPrintable(report.toText()));
    QVERIFY(!report.hasErrors());
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(report.toText().contains("mob.semantic.empty_trap_targets"));
    QVERIFY(report.toText().contains("mob.semantic.overlapping_unit_parts"));
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

void EnduranceDocumentTests::semanticDiffPairsIdenticalDuplicatesByOccurrence()
{
    const QByteArray duplicate = object(1000100, "duplicate");
    CMobDocument baseline;
    CMobDocument candidate;
    QVERIFY(baseline.loadBytes(validMob(section(0xB000,
        duplicate + duplicate + object(9, "unique"))), "baseline.mob"));
    QVERIFY(candidate.loadBytes(validMob(section(0xB000,
        object(9, "unique") + duplicate + duplicate)), "candidate.mob"));

    const CMobSemanticDiff diff = CMobSemanticDiff::compare(baseline, candidate);
    int unchanged = 0;
    for(const SMobDiffEntry& entry : diff.entries())
    {
        QCOMPARE(entry.kind, EMobDiffKind::Unchanged);
        QVERIFY(entry.baselineIndex >= 0);
        QVERIFY(entry.candidateIndex >= 0);
        ++unchanged;
    }
    QCOMPARE(unchanged, 3);
}

void EnduranceDocumentTests::semanticDiffReportsReadableScriptChanges()
{
    CMobDocument baseline;
    CMobDocument candidate;
    QVERIFY(baseline.loadBytes(validMob(encryptedScript("first()\nold_value()\nlast()")),
                               "baseline.mob"));
    QVERIFY(candidate.loadBytes(validMob(encryptedScript("first()\nnew_value()\nlast()")),
                                "candidate.mob"));

    const CMobSemanticDiff diff = CMobSemanticDiff::compare(baseline, candidate);
    QCOMPARE(diff.documentEntries().size(), 1);
    const SMobDocumentDiffEntry& script = diff.documentEntries().front();
    QCOMPARE(script.kind, QString("modified"));
    QCOMPARE(script.category, QString("script"));
    QVERIFY(script.details.contains("-2: old_value()"));
    QVERIFY(script.details.contains("+2: new_value()"));
    QVERIFY(diff.toText().contains("MODIFIED script"));
    const QJsonArray entries = diff.toJson().value("documentEntries").toArray();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.at(0).toObject().value("category").toString(), QString("script"));
}

void EnduranceDocumentTests::embeddedScriptReplacementPreservesOtherLeaves()
{
    const QByteArray original = validMob(
        encryptedScript("WorldScript(ConsoleString(\"before\"))") +
        section(0x12345678, "opaque-data"));
    CMobDocument document;
    CValidationReport report;
    QVERIFY(document.loadBytes(original, "script-source.mob", &report));

    QByteArray candidate;
    QVERIFY2(document.replaceEmbeddedScript(
        "WorldScript(ConsoleString(\"after\"))", &candidate, &report),
        qPrintable(report.toText()));
    CMobDocument changed;
    QVERIFY(changed.loadBytes(candidate, "script-candidate.mob", &report));
    const CMobSemanticDiff diff = CMobSemanticDiff::compare(document, changed);
    QCOMPARE(diff.documentEntries().size(), 1);
    QCOMPARE(diff.documentEntries().front().category, QString("script"));
    QVERIFY(diff.documentEntries().front().details.contains("after"));
    QVERIFY(candidate.contains("opaque-data"));
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

void EnduranceDocumentTests::objectSubtreeDuplicateAndDeleteAreExact()
{
    const QByteArray emptyDirectory = section(0xE000);
    const QByteArray firstObject = object(42, "boar");
    const QByteArray secondObject = object(43, "wolf");
    const QByteArray sourceBytes = validMob(emptyDirectory +
        section(0xB000, firstObject + secondObject));
    CMobDocument source;
    QVERIFY(source.loadBytes(sourceBytes, "source.mob"));
    QCOMPARE(source.objects().size(), 2);

    const SMobObjectSnapshot first = source.objects().front();
    QByteArray duplicated;
    CValidationReport duplicateReport;
    QVERIFY2(source.duplicateObjectSubtree(first.sectionPath, first.subtreeSha256,
                                           &duplicated, &duplicateReport),
             qPrintable(duplicateReport.toText()));
    QCOMPARE(duplicated, validMob(emptyDirectory +
        section(0xB000, firstObject + firstObject + secondObject)));
    CMobDocument duplicateDocument;
    QVERIFY(duplicateDocument.loadBytes(duplicated, "duplicated.mob"));
    QCOMPARE(duplicateDocument.objects().size(), 3);
    QCOMPARE(duplicateDocument.objects().at(0).subtreeSha256,
             duplicateDocument.objects().at(1).subtreeSha256);

    QByteArray removed;
    CValidationReport removeReport;
    QVERIFY2(source.removeObjectSubtree(first.sectionPath, first.subtreeSha256,
                                        &removed, &removeReport),
             qPrintable(removeReport.toText()));
    QCOMPARE(removed, validMob(emptyDirectory + section(0xB000, secondObject)));
    CMobDocument removedDocument;
    QVERIFY(removedDocument.loadBytes(removed, "removed.mob"));
    QCOMPARE(removedDocument.objects().size(), 1);
    QCOMPARE(removedDocument.objects().front().nid, quint32(43));
}

void EnduranceDocumentTests::objectSubtreeRejectsStaleProvenance()
{
    CMobDocument source;
    QVERIFY(source.loadBytes(validMob(section(0xB000, object(42, "boar"))),
                             "source.mob"));
    QByteArray output("must be cleared");
    CValidationReport report;
    QVERIFY(!source.duplicateObjectSubtree(source.objects().front().sectionPath,
                                           QByteArray(32, '\x7f'), &output, &report));
    QVERIFY(output.isEmpty());
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::objectSequenceRebuildIsExactAndOrdered()
{
    const QByteArray firstBytes = object(10, "first");
    const QByteArray secondBytes = object(20, "second");
    CMobDocument source;
    CValidationReport report;
    QVERIFY2(source.loadBytes(validMob(section(0xB000, firstBytes + secondBytes)),
                              "sequence-source.mob", &report),
             qPrintable(report.toText()));
    QCOMPARE(source.objects().size(), 2);
    const SMobObjectSnapshot first = source.objects().at(0);
    const SMobObjectSnapshot second = source.objects().at(1);

    QByteArray rebuiltBytes;
    QVERIFY2(source.rebuildObjectSequence(
                 QStringList() << second.sectionPath << first.sectionPath << first.sectionPath,
                 QVector<QByteArray>() << second.subtreeSha256
                                       << first.subtreeSha256
                                       << first.subtreeSha256,
                 &rebuiltBytes, &report), qPrintable(report.toText()));
    CMobDocument rebuilt;
    QVERIFY(rebuilt.loadBytes(rebuiltBytes, "sequence-output.mob", &report));
    QCOMPARE(rebuilt.objects().size(), 3);
    QCOMPARE(rebuilt.objects().at(0).nid, quint32(20));
    QCOMPARE(rebuilt.objects().at(1).nid, quint32(10));
    QCOMPARE(rebuilt.objects().at(2).nid, quint32(10));
    QCOMPARE(rebuilt.objects().at(0).subtreeSha256, second.subtreeSha256);
    QCOMPARE(rebuilt.objects().at(1).subtreeSha256, first.subtreeSha256);
    QCOMPARE(rebuilt.objects().at(2).subtreeSha256, first.subtreeSha256);

    QVERIFY(!source.rebuildObjectSequence(
                QStringList() << first.sectionPath,
                QVector<QByteArray>() << QByteArray(32, '\x7f'),
                &rebuiltBytes, &report));
    QVERIFY(rebuiltBytes.isEmpty());
}

void EnduranceDocumentTests::copiedObjectSequenceAcceptsOnlyIntendedLeafEdits()
{
    const QByteArray unknown = section(0xDEADBEEF, "opaque-object-data");
    const QByteArray cleanObject = object(42, "wolf", 1.0f);
    QByteArray rawPayload = cleanObject.mid(8);
    rawPayload += unknown;
    const QByteArray rawObject = section(0xB001, rawPayload);
    const QByteArray sourceBytes = validMob(section(0xB000, rawObject));
    const QByteArray cleanBytes = validMob(section(0xB000, cleanObject));
    const QByteArray editedBytes = validMob(section(0xB000,
        cleanObject + object(43, "wolf", 10.0f)));

    CMobDocument source;
    CMobDocument clean;
    CValidationReport report;
    QVERIFY(source.loadBytes(sourceBytes, "copy-source.mob", &report));
    QVERIFY(clean.loadBytes(cleanBytes, "copy-clean.mob", &report));
    QByteArray structuralSource;
    QByteArray structuralClean;
    const SMobObjectSnapshot raw = source.objects().front();
    const SMobObjectSnapshot projected = clean.objects().front();
    QVERIFY(source.rebuildObjectSequence(
        QStringList() << raw.sectionPath << raw.sectionPath,
        QVector<QByteArray>() << raw.subtreeSha256 << raw.subtreeSha256,
        &structuralSource, &report));
    QVERIFY(clean.rebuildObjectSequence(
        QStringList() << projected.sectionPath << projected.sectionPath,
        QVector<QByteArray>() << projected.subtreeSha256 << projected.subtreeSha256,
        &structuralClean, &report));

    CMobEditTransaction transaction;
    QByteArray candidate;
    QVERIFY2(CMobDocument::buildProjectionTransactionCandidate(
        structuralSource, "copy-structural-source.mob", structuralClean,
        editedBytes, &transaction, &candidate, &report), qPrintable(report.toText()));
    QVERIFY(transaction.validatedChangedSaveClass());
    QCOMPARE(transaction.edits().size(), 2);

    QByteArray editedRawPayload = object(43, "wolf", 10.0f).mid(8);
    editedRawPayload += unknown;
    const QByteArray expected = validMob(section(0xB000,
        rawObject + section(0xB001, editedRawPayload)));
    QCOMPARE(candidate, expected);
}

void EnduranceDocumentTests::patrolAndLookPointStructureIsReconciledExactly()
{
    const QByteArray fixedLogic = section(0xBBBC0002, QByteArray(1, '\1'));
    const QByteArray first = patrolPoint(1.0f, lookPoint(1.5f));
    const QByteArray sourceFirst = section(0xBBBD0000,
        section(0xBBBD0001, plot(1.0f, 2.0f, 3.0f)) +
        section(0xDEAD0001, "point-opaque") + lookPoint(1.5f));
    const QByteArray editedFirst = patrolPoint(1.0f,
        lookPoint(1.5f) + lookPoint(1.8f));
    const QByteArray second = patrolPoint(2.0f);
    const QByteArray added = patrolPoint(1.25f, lookPoint(1.75f));
    const QByteArray sourceObject = section(0xB001,
        section(0xB002, u32(42)) + section(0xB003, u32(1)) +
        section(0xDEADBEEF, "preserve-me") +
        section(0xBBBC0000, fixedLogic + sourceFirst + second));
    const QByteArray cleanObject = section(0xB001,
        section(0xB002, u32(42)) + section(0xB003, u32(1)) +
        section(0xBBBC0000, fixedLogic + first + second));
    const QByteArray editedObject = section(0xB001,
        section(0xB002, u32(42)) + section(0xB003, u32(1)) +
        section(0xBBBC0000, fixedLogic + editedFirst + added));

    CMobDocument source;
    CMobDocument clean;
    CMobDocument edited;
    CValidationReport report;
    QVERIFY(source.loadBytes(validMob(section(0xB000, sourceObject)),
                             "logic-source.mob", &report));
    QVERIFY(clean.loadBytes(validMob(section(0xB000, cleanObject)),
                            "logic-clean.mob", &report));
    QVERIFY(edited.loadBytes(validMob(section(0xB000, editedObject)),
                             "logic-edited.mob", &report));
    QByteArray structuralSource;
    QByteArray structuralClean;
    QVERIFY2(source.reconcileLogicPointStructure(clean, edited, &structuralSource, &report),
             qPrintable(report.toText()));
    QVERIFY2(clean.reconcileLogicPointStructure(clean, edited, &structuralClean, &report),
             qPrintable(report.toText()));
    CMobEditTransaction transaction;
    QByteArray candidate;
    QVERIFY2(CMobDocument::buildProjectionTransactionCandidate(
        structuralSource, "logic-structural-source.mob", structuralClean,
        edited.originalBytes(), &transaction, &candidate, &report),
        qPrintable(report.toText()));
    const QByteArray expectedObject = section(0xB001,
        section(0xB002, u32(42)) + section(0xB003, u32(1)) +
        section(0xDEADBEEF, "preserve-me") +
        section(0xBBBC0000, fixedLogic +
            section(0xBBBD0000,
                section(0xBBBD0001, plot(1.0f, 2.0f, 3.0f)) +
                section(0xDEAD0001, "point-opaque") +
                lookPoint(1.5f) + lookPoint(1.8f)) + added));
    const QByteArray expectedBytes = validMob(section(0xB000, expectedObject));
    int mismatch = 0;
    while(mismatch < candidate.size() && mismatch < expectedBytes.size() &&
          candidate.at(mismatch) == expectedBytes.at(mismatch))
        ++mismatch;
    QVERIFY2(candidate == expectedBytes,
             qPrintable(QString("logic candidate mismatch offset=%1 actual=%2 expected=%3")
                .arg(mismatch)
                .arg(QString::fromLatin1(candidate.mid(mismatch, 48).toHex()))
                .arg(QString::fromLatin1(expectedBytes.mid(mismatch, 48).toHex()))));
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

void EnduranceDocumentTests::projectionSaveCandidateIsProvenanceChecked()
{
    const QByteArray emptyDirectory = section(0xE000);
    const QByteArray sourceBytes = validMob(emptyDirectory + section(0xB011, QByteArray(1, '\x04')));
    const QByteArray cleanBytes = validMob(section(0xB011, QByteArray(1, '\x04')));
    const QByteArray editedBytes = validMob(section(0xB011, QByteArray(1, '\x0c')));
    CMobEditTransaction transaction;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(CMobDocument::buildProjectionTransactionCandidate(
                 sourceBytes, "source.mob", cleanBytes, editedBytes,
                 &transaction, &candidate, &report), qPrintable(report.toText()));
    QCOMPARE(transaction.edits().size(), 1);
    QCOMPARE(transaction.edits().front().sectionType, quint32(0xB011));
    QCOMPARE(transaction.edits().front().sourcePayload, QByteArray(1, '\x04'));
    QCOMPARE(transaction.edits().front().replacementPayload, QByteArray(1, '\x0c'));
    QString saveClass;
    QVERIFY(transaction.validatedChangedSaveClass(&saveClass));
    QCOMPARE(saveClass, QString("RAW_LEAF_EDITS"));
    QCOMPARE(candidate, validMob(emptyDirectory + section(0xB011, QByteArray(1, '\x0c'))));

    CMobEditTransaction unvalidated("unvalidated class");
    unvalidated.replaceLeafPayload("/OTHER[0]", QByteArray(31, '\0'), "text", 0xB004);
    QVERIFY(!unvalidated.validatedChangedSaveClass());
}

void EnduranceDocumentTests::topologyPreservingLeafEditsAreAuthorizedTogether()
{
    const QByteArray sourceStats = u32(5) + u32(1234) + u32(85);
    const QByteArray editedStats = u32(5) + u32(1234) + u32(120);
    const QByteArray sourcePosition = plot(1.0f, 2.0f, 3.0f);
    const QByteArray editedPosition = plot(10.0f, 20.0f, 30.0f);
    const QByteArray sourceBytes = validMob(
        section(0xB011, QByteArray(1, '\x04')) +
        section(0xBBAC0006, sourceStats) +
        section(0xB009, sourcePosition));
    const QByteArray editedBytes = validMob(
        section(0xB011, QByteArray(1, '\x0c')) +
        section(0xBBAC0006, editedStats) +
        section(0xB009, editedPosition));
    CMobEditTransaction transaction;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(CMobDocument::buildProjectionTransactionCandidate(
                 sourceBytes, "lever-source.mob", sourceBytes, editedBytes,
                 &transaction, &candidate, &report), qPrintable(report.toText()));
    QCOMPARE(transaction.edits().size(), 3);
    QCOMPARE(transaction.edits().at(0).sourcePayload, QByteArray(1, '\x04'));
    QCOMPARE(transaction.edits().at(1).sourcePayload, sourceStats);
    QCOMPARE(transaction.edits().at(2).sourcePayload, sourcePosition);
    QString saveClass;
    QVERIFY(transaction.validatedChangedSaveClass(&saveClass));
    QCOMPARE(saveClass, QString("RAW_LEAF_EDITS"));
    QCOMPARE(candidate, editedBytes);

    CMobEditTransaction unchanged("not an edit");
    unchanged.replaceLeafPayload("/OBJ_NAME[0]", QByteArray(32, '\0'),
                                 "wolf", 0xB004, "wolf");
    QVERIFY(!unchanged.validatedChangedSaveClass());

    CMobEditTransaction missingProvenance("missing provenance");
    missingProvenance.replaceLeafPayload("/OBJ_NAME[0]", QByteArray(31, '\0'),
                                         "boar", 0xB004, "wolf");
    QVERIFY(!missingProvenance.validatedChangedSaveClass());
}

void EnduranceDocumentTests::projectionSaveCandidateRejectsStructuralEdit()
{
    const QByteArray sourceBytes = validMob(section(0xB000, object(42, "boar")));
    const QByteArray editedBytes = validMob(section(0xB000,
        object(42, "boar") + object(43, "wolf")));
    CMobEditTransaction transaction;
    QByteArray candidate("must be cleared");
    CValidationReport report;
    QVERIFY(!CMobDocument::buildProjectionTransactionCandidate(
                sourceBytes, "source.mob", sourceBytes, editedBytes,
                &transaction, &candidate, &report));
    QVERIFY(transaction.isEmpty());
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

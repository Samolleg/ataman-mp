#include <QtTest>
#include <QCryptographicHash>
#include <QSet>
#include <QTemporaryFile>
#include <QTemporaryDir>

#include "endurance_document.h"
#include "landscape_edit_policy.h"
#include "operation_history_policy.h"
#include "terrain_edit_model.h"
#include "terrain_atlas_analyzer.h"
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

QByteArray syntheticMp()
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0xCE4AF672) << float(100.0f)
           << quint32(1) << quint32(1)
           << quint32(2) << quint32(512)
           << quint32(3) << quint32(64)
           << quint16(2) << quint32(0);
    for(int material = 0; material < 2; ++material)
    {
        stream << qint32(3)
               << float(0.2f + material * 0.1f) << float(0.3f) << float(0.6f)
               << float(0.7f) << float(0.0f) << float(0.5f)
               << float(0.0f) << float(0.0f) << float(0.0f) << float(0.0f);
    }
    stream << qint32(1) << qint32(2) << qint32(2);
    return bytes;
}

QByteArray syntheticOverlaySector()
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(3);
    bytes.append(QByteArray(33 * 33 * 8 * 2, '\0'));
    bytes.append(QByteArray(16 * 16 * 2, '\0'));
    QDataStream overlayTiles(&bytes, QIODevice::Append);
    overlayTiles.setByteOrder(QDataStream::LittleEndian);
    for(int tile = 0; tile < 16 * 16; ++tile)
        overlayTiles << quint16((tile % 3) == 0 ? 0xFFFF : 0);
    QDataStream materials(&bytes, QIODevice::Append);
    materials.setByteOrder(QDataStream::LittleEndian);
    for(int tile = 0; tile < 16 * 16; ++tile)
        materials << qint16((tile % 3) == 0 ? -1 : 1);
    return bytes;
}

QByteArray syntheticMpr()
{
    struct Member
    {
        QByteArray name;
        QByteArray payload;
        quint32 timestamp;
        quint32 offset;
    };
    QVector<Member> members;
    members.append({QByteArray("unknown.bin"), QByteArray("opaque-unknown-payload"), 0x10203040, 0});
    members.append({QByteArray("CaveSet.MP"), syntheticMp(), 0x50607080, 0});
    members.append({QByteArray("CaveSet000000.SEC"), syntheticOverlaySector(), 0x90A0B0C0, 0});

    QByteArray bytes(16, '\0');
    for(int index = 0; index < members.size(); ++index)
    {
        members[index].offset = quint32(bytes.size());
        bytes.append(members.at(index).payload);
        const int padding = (16 - (bytes.size() % 16)) % 16;
        bytes.append(QByteArray(padding, char(0xA0 + index)));
    }
    const quint32 tableOffset = quint32(bytes.size());
    QByteArray names;
    QDataStream table(&bytes, QIODevice::Append);
    table.setByteOrder(QDataStream::LittleEndian);
    for(const Member& member : members)
    {
        table << quint32(-1) << quint32(member.payload.size()) << member.offset
              << member.timestamp << quint16(member.name.size()) << quint32(names.size());
        names.append(member.name);
    }
    bytes.append(names);
    bytes.append("preserved-trailer");

    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setByteOrder(QDataStream::LittleEndian);
    headerStream << quint32(0x019CE23C) << quint32(members.size())
                 << tableOffset << quint32(names.size());
    bytes.replace(0, header.size(), header);
    return bytes;
}

QByteArray syntheticLandSector(quint16 height)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xCF4BF774) << quint8(0);
    for(int y = 0; y < 33; ++y)
        for(int x = 0; x < 33; ++x)
            stream << qint8(0) << qint8(0) << height
                   << quint32((1000u << 22) | (1000u << 11) | 1000u);
    bytes.append(QByteArray(16 * 16 * 2, '\0'));
    return bytes;
}

quint32 sectorPackedNormal(const QByteArray& sector, int x, int y)
{
    const int offset = 5 + (y * 33 + x) * 8 + 4;
    QDataStream stream(sector.mid(offset, 4));
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 value = 0;
    stream >> value;
    return value;
}

quint16 sectorHeight(const QByteArray& sector, int x, int y)
{
    const int offset = 5 + (y * 33 + x) * 8 + 2;
    QDataStream stream(sector.mid(offset, 2));
    stream.setByteOrder(QDataStream::LittleEndian);
    quint16 value = 0;
    stream >> value;
    return value;
}

QVector<STerrainSectorSource> twoSectorTerrain(quint16 height)
{
    QVector<STerrainSectorSource> sources;
    for(int x = 0; x < 2; ++x)
    {
        STerrainSectorSource source;
        source.memberName = QString("test%1000.sec").arg(x, 3, 10, QChar('0'));
        source.sectorX = x;
        source.sectorY = 0;
        source.payload = syntheticLandSector(height);
        source.expectedPayloadSha256 = QCryptographicHash::hash(
            source.payload, QCryptographicHash::Sha256).toHex();
        sources.append(source);
    }
    return sources;
}

const SMobSection* firstSectionOfType(const QVector<SMobSection>& sections, quint32 type)
{
    for(const SMobSection& candidate : sections)
    {
        if(candidate.type == type)
            return &candidate;
        if(const SMobSection* nested = firstSectionOfType(candidate.children, type))
            return nested;
    }
    return nullptr;
}
}

class EnduranceDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void persistentPaintOperationSurvivesHistoryNavigation();
    void landscapeLayerChangesResetBrushSelection();
    void unchangedMobIsByteIdentical();
    void duplicateMobIdsAreWarningsOnly();
    void unknownLeafIsPreservedAsOpaque();
    void suppliedOpaqueChestCanPatchKnownLeafSafely();
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
    void crossMobObjectSequencePreservesDonorRawSubtree();
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
    void syntheticMprPreservesArchiveNamespaceAndInactiveSentinel();
    void mprSectorPatchIsProvenanceCheckedAndLocal();
    void mprLengthChangingAndStaleTransactionsFailClosed();
    void failedStagedMprSaveLeavesNoDestination();
    void unchangedExistingFileComparisonIsReadOnly();
    void canonicalTerrainSeamsAreEditedAndUndoneTogether();
    void terrainBrushToolsAreDeterministicAndFailClosed();
    void terrainHeightmapImportIsDeterministicAndSeamAware();
    void terrainDiagnosticsArePreApplyAndPolicyExplicit();
    void terrainTilePaintingIsUndoableLocalizedAndNamespaceSafe();
    void zone1NamespaceTemplateSeparatesLandOverlayAndVariants();
    void zone1NamespaceTemplateMatchesPrivateFixture();
    void terrainHeightTransactionReopensAndPreservesOverlay();
    void conflictingTerrainSeamIsPreservedAndNotEditable();
    void malformedMprTableIsRejected();
    void stagedMobIsReopenedWithoutCreatingDestination();
};

void EnduranceDocumentTests::persistentPaintOperationSurvivesHistoryNavigation()
{
    QVERIFY(operationAllowsHistoryNavigation(eOperationMethodSelect));
    QVERIFY(operationAllowsHistoryNavigation(eOperationMethodTileBrush));
    QVERIFY(!operationAllowsHistoryNavigation(eOperationMethodMoveAxis));
    QVERIFY(!operationAllowsHistoryNavigation(eOperationMethodRotateAxis));
    QVERIFY(!operationAllowsHistoryNavigation(eOperationMethodScaleAxis));
}

void EnduranceDocumentTests::landscapeLayerChangesResetBrushSelection()
{
    QVERIFY(!landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::Sculpt, ELandscapeEditMode::Sculpt));
    QVERIFY(!landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::TilePaint, ELandscapeEditMode::TilePaint));
    QVERIFY(!landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::OverlayPaint, ELandscapeEditMode::OverlayPaint));
    QVERIFY(landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::Sculpt, ELandscapeEditMode::TilePaint));
    QVERIFY(landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::Sculpt, ELandscapeEditMode::OverlayPaint));
    QVERIFY(landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::TilePaint, ELandscapeEditMode::OverlayPaint));
    QVERIFY(landscapeModeChangeRequiresBrushReset(
        ELandscapeEditMode::OverlayPaint, ELandscapeEditMode::TilePaint));
}

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
    int opaqueWarnings = 0;
    for(const SValidationFinding& finding : report.findings())
    {
        if(finding.code != "mob.opaque_unknown")
            continue;
        ++opaqueWarnings;
        QVERIFY(finding.message.contains("0xdeadbeef", Qt::CaseInsensitive));
        QVERIFY(finding.message.contains("offset 8"));
    }
    QCOMPARE(opaqueWarnings, 1);
    QVERIFY(!report.hasErrors());
}

void EnduranceDocumentTests::suppliedOpaqueChestCanPatchKnownLeafSafely()
{
    const QString fixture = qEnvironmentVariable("ENDURANCE_CHEST_FIXTURE");
    if(fixture.isEmpty())
        QSKIP("ENDURANCE_CHEST_FIXTURE was not provided");
    CMobDocument source;
    CValidationReport openReport;
    QVERIFY(source.load(QFileInfo(fixture), &openReport));
    const QByteArray original = source.serialize();
    QCOMPARE(original.size(), 1532);
    const SMobSection* opaque = firstSectionOfType(source.sections(), 0xACCEECCC);
    QVERIFY(opaque);
    QCOMPARE(opaque->offset, quint64(1166));
    QCOMPARE(opaque->declaredLength, quint32(12));
    QCOMPARE(opaque->rawBytes.mid(8), QByteArray(4, '\0'));
    const QByteArray opaqueBytes = opaque->rawBytes;

    const SMobSection* nid = firstSectionOfType(source.sections(), 0xB002);
    QVERIFY(nid);
    quint32 oldNid = 0;
    QDataStream nidStream(nid->rawBytes.mid(8));
    nidStream.setByteOrder(QDataStream::LittleEndian);
    nidStream >> oldNid;
    CMobEditTransaction transaction("opaque fixture known-leaf edit");
    transaction.replaceLeafPayload(nid->path, nid->sha256, u32(oldNid + 1), nid->type,
                                   nid->rawBytes.mid(8));
    QByteArray candidate;
    CValidationReport transactionReport;
    QVERIFY(source.applyTransaction(transaction, &candidate, &transactionReport));
    QVERIFY(candidate != original);
    CMobDocument reopened;
    QVERIFY(reopened.loadBytes(candidate, "opaque-chest-candidate.mob", &transactionReport));
    const SMobSection* candidateOpaque = firstSectionOfType(reopened.sections(), 0xACCEECCC);
    QVERIFY(candidateOpaque);
    QCOMPARE(candidateOpaque->rawBytes, opaqueBytes);
    QCOMPARE(source.serialize(), original);
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

void EnduranceDocumentTests::crossMobObjectSequencePreservesDonorRawSubtree()
{
    const QByteArray targetObject = object(10, "target", 1.0f);
    const QByteArray donorOpaque = section(0xDEADBEEF, "donor-opaque-data");
    const QByteArray donorClean = object(20, "tree", 2.0f);
    const QByteArray donorRaw = section(0xB001, donorClean.mid(8) + donorOpaque);

    CMobDocument target;
    CMobDocument donor;
    CMobDocument donorProjection;
    CValidationReport report;
    QVERIFY(target.loadBytes(validMob(section(0xB000, targetObject)),
                             "target.mob", &report));
    QVERIFY(donor.loadBytes(validMob(section(0xB000, donorRaw)),
                            "donor.mob", &report));
    QVERIFY(donorProjection.loadBytes(validMob(section(0xB000, donorClean)),
                                      "donor-clean.mob", &report));

    QByteArray targetRaw;
    QByteArray donorSourceRaw;
    QByteArray donorCleanRaw;
    const SMobObjectSnapshot targetSnapshot = target.objects().front();
    const SMobObjectSnapshot donorSnapshot = donor.objects().front();
    const SMobObjectSnapshot donorCleanSnapshot = donorProjection.objects().front();
    QVERIFY(target.objectSubtreeBytes(targetSnapshot.sectionPath,
                                      targetSnapshot.subtreeSha256, &targetRaw));
    QVERIFY(donor.objectSubtreeBytes(donorSnapshot.sectionPath,
                                     donorSnapshot.subtreeSha256, &donorSourceRaw));
    QVERIFY(donorProjection.objectSubtreeBytes(donorCleanSnapshot.sectionPath,
                                               donorCleanSnapshot.subtreeSha256,
                                               &donorCleanRaw));

    QByteArray structuralSource;
    QVERIFY2(target.rebuildObjectSequenceFromRaw(
        QVector<QByteArray>() << targetRaw << donorSourceRaw,
        QVector<QByteArray>() << targetSnapshot.subtreeSha256
                              << donorSnapshot.subtreeSha256,
        &structuralSource, &report), qPrintable(report.toText()));
    CMobDocument rebuilt;
    QVERIFY(rebuilt.loadBytes(structuralSource, "cross-mob-output.mob", &report));
    QCOMPARE(rebuilt.objects().size(), 2);
    QCOMPARE(rebuilt.objects().at(1).subtreeSha256, donorSnapshot.subtreeSha256);
    QVERIFY(structuralSource.contains("donor-opaque-data"));

    QByteArray rejected;
    QVERIFY(!target.rebuildObjectSequenceFromRaw(
        QVector<QByteArray>() << targetRaw << donorCleanRaw,
        QVector<QByteArray>() << targetSnapshot.subtreeSha256
                              << donorSnapshot.subtreeSha256,
        &rejected, &report));
    QVERIFY(rejected.isEmpty());
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

void EnduranceDocumentTests::syntheticMprPreservesArchiveNamespaceAndInactiveSentinel()
{
    const QByteArray bytes = syntheticMpr();
    CMprDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(bytes, "DifferentOuterName.mpr", &report),
             qPrintable(report.toText()));
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(document.serialize(), bytes);
    QCOMPARE(document.members().size(), 3);
    QCOMPARE(document.members().at(0).name, QString("unknown.bin"));
    QCOMPARE(document.members().at(1).name, QString("CaveSet.MP"));
    QCOMPARE(document.members().at(2).name, QString("CaveSet000000.SEC"));
    QCOMPARE(document.members().at(0).lastWriteTime, quint32(0x10203040));
    QCOMPARE(document.mapInfo().innerNamespace, QString("CaveSet"));
    QCOMPARE(document.mapInfo().textureCount, quint32(2));
    QCOMPARE(document.mapInfo().tileTypes, QVector<qint32>({1, 2, 2}));
    bool foundTileType = false;
    QCOMPARE(document.mapInfo().tileType(1, &foundTileType), qint32(2));
    QVERIFY(foundTileType);
    QCOMPARE(document.mapInfo().tileType(64, &foundTileType), qint32(0));
    QVERIFY(!foundTileType);
    SMprPackedTile packedTile;
    packedTile.tileIndex = 37;
    packedTile.atlasIndex = 129;
    packedTile.rotation = 3;
    const quint16 encodedTile = packedTile.encode();
    const SMprPackedTile decodedTile = SMprPackedTile::decode(encodedTile);
    QCOMPARE(decodedTile.tileIndex, quint8(37));
    QCOMPARE(decodedTile.atlasIndex, quint8(129));
    QCOMPARE(decodedTile.rotation, quint8(3));
    QCOMPARE(decodedTile.atlasSlot(), 129 * 64 + 37);
    QCOMPARE(document.sectors().size(), 1);
    QVERIFY(document.sectors().front().hasOverlay);
    QCOMPARE(document.sectors().front().overlayMaterialIndices.size(), 256);
    QCOMPARE(document.sectors().front().overlayMaterialIndices.at(0), qint16(-1));
    QCOMPARE(document.sectors().front().overlayMaterialIndices.at(1), qint16(1));
    for(const SValidationFinding& finding : report.findings())
        QVERIFY(finding.code != "mpr.atlas_namespace");
}

void EnduranceDocumentTests::mprSectorPatchIsProvenanceCheckedAndLocal()
{
    const QByteArray bytes = syntheticMpr();
    CMprDocument baseline;
    CValidationReport baselineReport;
    QVERIFY2(baseline.loadBytes(bytes, "baseline.mpr", &baselineReport),
             qPrintable(baselineReport.toText()));
    const SMprArchiveMember sector = baseline.members().at(2);
    QByteArray replacement = sector.payload;
    const int landTileOffset = int(baseline.sectors().front().landTileOffset);
    replacement[landTileOffset + 1] = char(quint8(replacement.at(landTileOffset + 1)) ^ 0x40);

    CMprEditTransaction transaction("rotate one synthetic land tile");
    transaction.replaceMemberPayload(sector.name, sector.sha256, replacement);
    QByteArray candidateBytes;
    CValidationReport transactionReport;
    QVERIFY2(baseline.applyTransaction(transaction, &candidateBytes, &transactionReport),
             qPrintable(transactionReport.toText()));
    QCOMPARE(candidateBytes.size(), bytes.size());
    int changedByteCount = 0;
    for(int index = 0; index < bytes.size(); ++index)
        changedByteCount += bytes.at(index) != candidateBytes.at(index);
    QCOMPARE(changedByteCount, 1);

    CMprDocument candidate;
    CValidationReport candidateReport;
    QVERIFY2(candidate.loadBytes(candidateBytes, "candidate.mpr", &candidateReport),
             qPrintable(candidateReport.toText()));
    QCOMPARE(candidate.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    QCOMPARE(candidate.memberPayload("caveset.mp"), baseline.memberPayload("CaveSet.MP"));
    for(int index = 0; index < baseline.members().size(); ++index)
    {
        QCOMPARE(candidate.members().at(index).encodedName, baseline.members().at(index).encodedName);
        QCOMPARE(candidate.members().at(index).lastWriteTime, baseline.members().at(index).lastWriteTime);
        QCOMPARE(candidate.members().at(index).dataOffset, baseline.members().at(index).dataOffset);
    }
    const CMprSemanticDiff diff = CMprSemanticDiff::compare(baseline, candidate);
    QCOMPARE(diff.entries().size(), 1);
    QCOMPARE(diff.entries().front().kind, QString("modified"));
    QCOMPARE(diff.entries().front().memberName, sector.name);
}

void EnduranceDocumentTests::mprLengthChangingAndStaleTransactionsFailClosed()
{
    CMprDocument document;
    CValidationReport loadReport;
    QVERIFY(document.loadBytes(syntheticMpr(), "synthetic.mpr", &loadReport));
    const SMprArchiveMember sector = document.members().at(2);

    CMprEditTransaction lengthChange("unproven overlay structure change");
    lengthChange.replaceMemberPayload(sector.name, sector.sha256, sector.payload + QByteArray("x"));
    QByteArray output("must be cleared");
    CValidationReport lengthReport;
    QVERIFY(!document.applyTransaction(lengthChange, &output, &lengthReport));
    QVERIFY(output.isEmpty());
    QVERIFY(lengthReport.hasErrors());

    CMprEditTransaction stale("stale edit");
    stale.replaceMemberPayload(sector.name, QByteArray(64, '0'), sector.payload);
    output = "must be cleared";
    CValidationReport staleReport;
    QVERIFY(!document.applyTransaction(stale, &output, &staleReport));
    QVERIFY(output.isEmpty());
    QVERIFY(staleReport.hasErrors());
}

void EnduranceDocumentTests::failedStagedMprSaveLeavesNoDestination()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QFileInfo destination(dir.filePath("missing/subdirectory/test.mpr"));
    const SStagedSaveValidation result = validateStagedSave(syntheticMpr(), destination);
    QVERIFY(!result.success);
    QVERIFY(!destination.exists());
}

void EnduranceDocumentTests::unchangedExistingFileComparisonIsReadOnly()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray bytes("mpr-no-op-probe");
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();
    const QFileInfo before(file.fileName());

    const SExistingFileComparison equal = compareExistingFile(before, bytes);
    QVERIFY(equal.exists);
    QVERIFY(equal.readable);
    QVERIFY(equal.equal);
    QCOMPARE(QFileInfo(file.fileName()).lastModified(), before.lastModified());

    const SExistingFileComparison different = compareExistingFile(before, "other");
    QVERIFY(different.exists);
    QVERIFY(different.readable);
    QVERIFY(!different.equal);
    QCOMPARE(QFileInfo(file.fileName()).lastModified(), before.lastModified());
}

void EnduranceDocumentTests::canonicalTerrainSeamsAreEditedAndUndoneTogether()
{
    CTerrainEditModel terrain;
    CValidationReport loadReport;
    const QVector<STerrainSectorSource> sources = twoSectorTerrain(1000);
    QVERIFY2(terrain.loadSources(2, 1, 100.0f, sources, &loadReport),
             qPrintable(loadReport.toText()));
    QCOMPARE(terrain.vertexCountX(), 65);
    QCOMPARE(terrain.vertexCountY(), 33);
    QVERIFY(!terrain.hasSeamConflicts());

    STerrainBrush brush;
    brush.centerX = 32.0f;
    brush.centerY = 10.0f;
    brush.radius = 0.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const STerrainHeightCommand raise = terrain.makeRaiseLower(brush, 250.0f);
    QCOMPARE(raise.deltas.size(), 1);
    CTerrainEditHistory history;
    QVERIFY(history.execute(&terrain, raise));
    QCOMPARE(terrain.height(32, 10), quint16(1250));
    QCOMPARE(sectorHeight(terrain.sectorPayload(0, 0), 32, 10), quint16(1250));
    QCOMPARE(sectorHeight(terrain.sectorPayload(1, 0), 0, 10), quint16(1250));
    QCOMPARE(sectorPackedNormal(terrain.sectorPayload(0, 0), 32, 10),
             sectorPackedNormal(terrain.sectorPayload(1, 0), 0, 10));
    QVERIFY(sectorPackedNormal(terrain.sectorPayload(0, 0), 31, 10)
            != sectorPackedNormal(sources.at(0).payload, 31, 10));
    QCOMPARE(history.undoCount(), 1);

    CMprEditTransaction firstTransaction("height candidate");
    CValidationReport firstSaveReport;
    QVERIFY2(terrain.appendSectorEdits(&firstTransaction, &firstSaveReport),
             qPrintable(firstSaveReport.toText()));
    QCOMPARE(firstTransaction.edits().size(), 2);
    QCOMPARE(sectorPackedNormal(firstTransaction.edits().at(0).replacementPayload, 32, 10),
             sectorPackedNormal(firstTransaction.edits().at(1).replacementPayload, 0, 10));
    QCOMPARE(sectorPackedNormal(firstTransaction.edits().at(0).replacementPayload, 5, 5),
             sectorPackedNormal(sources.at(0).payload, 5, 5));

    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.height(32, 10), quint16(1000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(0, 0), 32, 10), quint16(1000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(1, 0), 0, 10), quint16(1000));
    QCOMPARE(terrain.sectorPayload(0, 0), sources.at(0).payload);
    QCOMPARE(terrain.sectorPayload(1, 0), sources.at(1).payload);
    QVERIFY(!terrain.isDirty());
    CMprEditTransaction undoneTransaction("undone height candidate");
    QVERIFY(terrain.appendSectorEdits(&undoneTransaction));
    QVERIFY(undoneTransaction.isEmpty());
    QVERIFY(history.redo(&terrain));
    QCOMPARE(terrain.height(32, 10), quint16(1250));
    QCOMPARE(sectorPackedNormal(terrain.sectorPayload(0, 0), 32, 10),
             sectorPackedNormal(terrain.sectorPayload(1, 0), 0, 10));

    CMprEditTransaction redoneTransaction("redone height candidate");
    CValidationReport redoneSaveReport;
    QVERIFY2(terrain.appendSectorEdits(&redoneTransaction, &redoneSaveReport),
             qPrintable(redoneSaveReport.toText()));
    QCOMPARE(redoneTransaction.edits().size(), firstTransaction.edits().size());
    for(int index = 0; index < firstTransaction.edits().size(); ++index)
        QCOMPARE(redoneTransaction.edits().at(index).replacementPayload,
                 firstTransaction.edits().at(index).replacementPayload);
}

void EnduranceDocumentTests::terrainBrushToolsAreDeterministicAndFailClosed()
{
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(2, 1, 100.0f, twoSectorTerrain(1000)));
    CTerrainEditHistory history;

    STerrainBrush point;
    point.centerX = 5.0f;
    point.centerY = 5.0f;
    point.radius = 0.0f;
    point.falloff = ETerrainBrushFalloff::Constant;
    QVERIFY(history.execute(&terrain, terrain.makeFlatten(point, 3000, 1.0f)));
    QCOMPARE(terrain.height(5, 5), quint16(3000));

    STerrainBrush smooth = point;
    smooth.radius = 1.0f;
    QVERIFY(history.execute(&terrain, terrain.makeSmooth(smooth, 1.0f)));
    QVERIFY(terrain.height(5, 5) < 3000);
    QVERIFY(terrain.height(5, 5) > 1000);

    const STerrainHeightCommand ramp = terrain.makeRamp(
        10.0f, 10.0f, 2000, 20.0f, 10.0f, 4000,
        0.0f, 1.0f, ETerrainBrushFalloff::Constant);
    QVERIFY(!ramp.isEmpty());
    QVERIFY(history.execute(&terrain, ramp));
    QCOMPARE(terrain.height(10, 10), quint16(2000));
    QCOMPARE(terrain.height(20, 10), quint16(4000));
    QCOMPARE(terrain.height(15, 10), quint16(3000));

    CValidationReport staleReport;
    QVERIFY(!terrain.apply(ramp, true, &staleReport));
    QVERIFY(staleReport.hasErrors());
}

void EnduranceDocumentTests::terrainHeightmapImportIsDeterministicAndSeamAware()
{
    const QVector<STerrainSectorSource> sources = twoSectorTerrain(1000);
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(2, 1, 100.0f, sources));
    STerrainHeightmapGrid grid;
    grid.width = 2;
    grid.height = 2;
    grid.samples = {0.0f, 1.0f, 0.0f, 1.0f};
    STerrainHeightmapImportSettings settings;
    settings.minimumRawHeight = 1000.0f;
    settings.maximumRawHeight = 5000.0f;
    const STerrainHeightCommand stretched = terrain.makeHeightmapImport(grid, settings);
    QVERIFY(!stretched.isEmpty());
    int seamDeltaCount = 0;
    for(const STerrainHeightDelta& delta : stretched.deltas)
    {
        if(delta.globalX == 32 && delta.globalY == 0)
        {
            QCOMPARE(delta.after, quint16(3000));
            ++seamDeltaCount;
        }
    }
    QCOMPARE(seamDeltaCount, 1);
    CTerrainEditHistory history;
    QVERIFY(history.execute(&terrain, stretched));
    QCOMPARE(terrain.height(32, 0), quint16(3000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(0, 0), 32, 0), quint16(3000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(1, 0), 0, 0), quint16(3000));
    QVERIFY(sectorPackedNormal(terrain.sectorPayload(0, 0), 10, 10)
            != sectorPackedNormal(sources.at(0).payload, 10, 10));
    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.height(32, 0), quint16(1000));
    QCOMPARE(terrain.sectorPayload(0, 0), sources.at(0).payload);
    QCOMPARE(terrain.sectorPayload(1, 0), sources.at(1).payload);
    QVERIFY(history.redo(&terrain));
    QCOMPARE(terrain.height(64, 32), quint16(5000));

    CTerrainEditModel placedTerrain;
    QVERIFY(placedTerrain.loadSources(2, 1, 100.0f, twoSectorTerrain(1000)));
    grid.width = 3;
    grid.height = 1;
    grid.samples = {0.0f, 0.5f, 1.0f};
    settings.placement = ETerrainHeightmapPlacement::OneSamplePerVertex;
    settings.destinationX = 31;
    settings.destinationY = 0;
    settings.minimumRawHeight = 1000.0f;
    settings.maximumRawHeight = 3000.0f;
    const STerrainHeightCommand placed = placedTerrain.makeHeightmapImport(grid, settings);
    QCOMPARE(placed.deltas.size(), 2);
    QCOMPARE(placed.deltas.front().globalX, 32);
    QCOMPARE(placed.deltas.front().after, quint16(2000));
    QVERIFY(placedTerrain.apply(placed, true));
    QCOMPARE(placedTerrain.height(33, 0), quint16(3000));

    settings.destinationX = -1;
    CTerrainEditModel croppedTerrain;
    QVERIFY(croppedTerrain.loadSources(1, 1, 100.0f,
                                       twoSectorTerrain(1000).mid(0, 1)));
    const STerrainHeightCommand croppedLoaded =
        croppedTerrain.makeHeightmapImport(grid, settings);
    QCOMPARE(croppedLoaded.deltas.size(), 2);
    QCOMPARE(croppedLoaded.deltas.front().globalX, 0);
    QCOMPARE(croppedLoaded.deltas.front().after, quint16(2000));

    STerrainHeightmapGrid saturatedGrid;
    saturatedGrid.width = 1;
    saturatedGrid.height = 1;
    saturatedGrid.samples = {1.0f};
    settings.destinationX = 10;
    settings.maximumRawHeight = 70000.0f;
    const STerrainHeightCommand saturated =
        croppedTerrain.makeHeightmapImport(saturatedGrid, settings);
    QVERIFY(saturated.requiresMaxZDecision());
    QVERIFY(!croppedTerrain.apply(saturated, true));

    saturatedGrid.samples = {1.1f};
    CValidationReport invalidReport;
    QVERIFY(croppedTerrain.makeHeightmapImport(
        saturatedGrid, settings, &invalidReport).isEmpty());
    QVERIFY(invalidReport.hasErrors());
}

void EnduranceDocumentTests::terrainDiagnosticsArePreApplyAndPolicyExplicit()
{
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(1, 1, 100.0f, twoSectorTerrain(1000).mid(0, 1)));
    STerrainBrush brush;
    brush.centerX = 10.0f;
    brush.centerY = 10.0f;
    brush.radius = 0.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const STerrainHeightCommand saturated = terrain.makeRaiseLower(brush, 70000.0f);
    QCOMPARE(saturated.deltas.size(), 1);
    QVERIFY(saturated.requiresMaxZDecision());

    STerrainObjectHeightProbe object;
    object.identity = "synthetic object 7";
    object.positionX = 10.0f;
    object.positionY = 10.0f;
    object.positionZ = float(1000) * 100.0f / 65535.0f;
    STerrainDiagnosticPolicy policy;
    policy.steepSlopeDegrees = 10.0f;
    policy.objectVerticalDelta = 0.5f;
    CValidationReport report;
    const STerrainEditDiagnostics diagnostics = terrain.diagnose(
        saturated, QVector<STerrainObjectHeightProbe>() << object, policy, &report);
    QVERIFY(diagnostics.valid);
    QVERIFY(diagnostics.requiresMaxZDecision);
    QVERIFY(diagnostics.navigationGraphMayBeStale);
    QVERIFY(diagnostics.steepSlopeRisk);
    QCOMPARE(diagnostics.changedVertexCount, 1);
    QCOMPARE(diagnostics.sharedSeamVertexCount, 0);
    QCOMPARE(diagnostics.affectedSectorCount, 1);
    QCOMPARE(diagnostics.maximumAfter, quint16(65535));
    QCOMPARE(diagnostics.objectFindings.size(), 1);
    QVERIFY(diagnostics.objectFindings.front().exceedsThreshold);
    QVERIFY(diagnostics.objectFindings.front().terrainDelta > 90.0f);
    QCOMPARE(terrain.height(10, 10), quint16(1000));
    bool sawMaxZ = false;
    bool sawNavigation = false;
    bool sawSlope = false;
    bool sawObject = false;
    for(const SValidationFinding& finding : report.findings())
    {
        sawMaxZ |= finding.code == "terrain.maxz_saturation";
        sawNavigation |= finding.code == "terrain.navigation_stale";
        sawSlope |= finding.code == "terrain.slope_risk";
        sawObject |= finding.code == "terrain.mob_height_risk";
    }
    QVERIFY(sawMaxZ);
    QVERIFY(sawNavigation);
    QVERIFY(sawSlope);
    QVERIFY(sawObject);

    const STerrainEditDiagnostics withoutThresholds = terrain.diagnose(
        saturated, QVector<STerrainObjectHeightProbe>() << object);
    QVERIFY(withoutThresholds.valid);
    QVERIFY(!withoutThresholds.steepSlopeRisk);
    QCOMPARE(withoutThresholds.objectFindings.size(), 1);
    QVERIFY(!withoutThresholds.objectFindings.front().exceedsThreshold);

    CTerrainEditModel seamTerrain;
    QVERIFY(seamTerrain.loadSources(2, 1, 100.0f, twoSectorTerrain(1000)));
    brush.centerX = 32.0f;
    const STerrainHeightCommand seam = seamTerrain.makeRaiseLower(brush, 1000.0f);
    const STerrainEditDiagnostics seamDiagnostics = seamTerrain.diagnose(seam);
    QVERIFY(seamDiagnostics.valid);
    QVERIFY(!seamDiagnostics.requiresMaxZDecision);
    QCOMPARE(seamDiagnostics.sharedSeamVertexCount, 1);
    QCOMPARE(seamDiagnostics.affectedSectorCount, 2);

    STerrainHeightCommand duplicate = seam;
    duplicate.deltas.append(seam.deltas.front());
    CValidationReport duplicateReport;
    QVERIFY(!seamTerrain.apply(duplicate, true, &duplicateReport));
    QVERIFY(duplicateReport.hasErrors());

    CValidationReport blockedReport;
    QVERIFY(!terrain.apply(saturated, true, &blockedReport));
    QVERIFY(blockedReport.hasErrors());
    QCOMPARE(terrain.height(10, 10), quint16(1000));
    QVERIFY(terrain.apply(saturated, true, nullptr, true));
    CValidationReport staleReport;
    QVERIFY(!terrain.diagnose(saturated, QVector<STerrainObjectHeightProbe>(),
                              policy, &staleReport).valid);
    QVERIFY(staleReport.hasErrors());
}

void EnduranceDocumentTests::terrainTilePaintingIsUndoableLocalizedAndNamespaceSafe()
{
    STerrainTransitionSignature signature;
    signature.northWest = 1;
    signature.northEast = 2;
    signature.southEast = 3;
    signature.southWest = 4;
    const STerrainTransitionSignature rotated =
        CTerrainTransitionCatalog::rotated(signature, 1);
    QCOMPARE(rotated.northWest, qint32(4));
    QCOMPARE(rotated.northEast, qint32(1));
    QCOMPARE(rotated.southEast, qint32(2));
    QCOMPARE(rotated.southWest, qint32(3));
    QCOMPARE(CTerrainTransitionCatalog::rotated(rotated, -1), signature);
    CTerrainTransitionCatalog catalog;
    STerrainTransitionVariant firstVariant;
    firstVariant.signature = signature;
    firstVariant.packedTile = 1;
    STerrainTransitionVariant secondVariant = firstVariant;
    secondVariant.packedTile = 2;
    secondVariant.distributionWeight = 3;
    QVERIFY(catalog.addVariant(firstVariant));
    QVERIFY(catalog.addVariant(secondVariant));
    QVERIFY(!catalog.addVariant(firstVariant));
    QSet<quint16> distributed;
    for(int x = 0; x < 64; ++x)
    {
        bool found = false;
        const quint16 selected = catalog.select(signature, x, 7, 12345, &found);
        QVERIFY(found);
        QCOMPARE(selected, catalog.select(signature, x, 7, 12345));
        distributed.insert(selected);
    }
    QCOMPARE(distributed, QSet<quint16>({1, 2}));
    bool foundTransition = true;
    STerrainTransitionSignature missingSignature;
    QCOMPARE(catalog.select(missingSignature, 0, 0, 0, &foundTransition), quint16(0));
    QVERIFY(!foundTransition);

    CMprDocument baseline;
    CValidationReport baselineReport;
    QVERIFY(baseline.loadBytes(syntheticMpr(), "tile-paint-baseline.mpr", &baselineReport));
    QVERIFY(!baselineReport.hasErrors());

    CTerrainEditModel terrain;
    CValidationReport loadReport;
    QVERIFY(terrain.load(baseline, &loadReport));
    QVERIFY(!loadReport.hasErrors());
    QCOMPARE(terrain.tileCountX(), 16);
    QCOMPARE(terrain.tileCountY(), 16);
    bool available = false;
    QCOMPARE(terrain.packedTile(0, 0, ETerrainTileLayer::Land, &available), quint16(0));
    QVERIFY(available);
    QCOMPARE(terrain.tileType(quint16(1), &available), qint32(2));
    QVERIFY(available);

    STerrainTileBrush area;
    area.centerX = 5.0f;
    area.centerY = 5.0f;
    area.radius = 1.0f;
    area.shape = ETerrainTileBrushShape::Circle;
    QCOMPARE(terrain.makeTilePaint(area, ETerrainTileLayer::Land,
                                   0, 1, 0).deltas.size(), 5);
    area.shape = ETerrainTileBrushShape::Square;
    QCOMPARE(terrain.makeTilePaint(area, ETerrainTileLayer::Land,
                                   0, 1, 0).deltas.size(), 9);

    CTerrainTransitionCatalog semanticCatalog;
    const QVector<int> semanticMasks({0, 15, 12, 9, 3, 6, 1, 2, 4, 8,
                                      7, 11, 13, 14});
    for(int variantIndex = 0; variantIndex < semanticMasks.size(); ++variantIndex)
    {
        const int mask = semanticMasks.at(variantIndex);
        STerrainTransitionVariant visualVariant;
        visualVariant.signature.northWest = (mask & 1) ? 2 : 1;
        visualVariant.signature.northEast = (mask & 2) ? 2 : 1;
        visualVariant.signature.southEast = (mask & 4) ? 2 : 1;
        visualVariant.signature.southWest = (mask & 8) ? 2 : 1;
        if(variantIndex == 0)
            visualVariant.packedTile = 0;
        else if(variantIndex == 1)
            visualVariant.packedTile = quint16(1 << 14);
        else if(variantIndex < 6)
            visualVariant.packedTile = quint16(1 | ((variantIndex - 2) << 14));
        else if(variantIndex < 10)
            visualVariant.packedTile = quint16(2 | ((variantIndex - 6) << 14));
        else
            visualVariant.packedTile = quint16(3 | ((variantIndex - 10) << 14));
        QVERIFY(semanticCatalog.addVariant(visualVariant));
    }
    QVERIFY(semanticCatalog.hasMaterial(1));
    QVERIFY(semanticCatalog.hasMaterial(2));
    QVERIFY(!semanticCatalog.hasMaterial(99));
    bool semanticFound = false;
    const STerrainTransitionSignature decoded =
        semanticCatalog.signatureForPacked(quint16(1 << 14), &semanticFound);
    QVERIFY(semanticFound);
    QCOMPARE(decoded.northWest, qint32(2));

    STerrainTileBrush semanticPoint;
    semanticPoint.centerX = 8.0f;
    semanticPoint.centerY = 8.0f;
    semanticPoint.radius = 0.0f;
    CValidationReport semanticReport;
    const STerrainTileCommand semantic = terrain.makeSemanticTilePaint(
        semanticPoint, ETerrainTileLayer::Land, 2, semanticCatalog,
        0x53454D41u, &semanticReport);
    QVERIFY2(!semantic.isEmpty(), qPrintable(semanticReport.toText()));
    QVERIFY(semantic.deltas.size() <= 9);
    bool changedCenter = false;
    for(const STerrainTileDelta& delta : semantic.deltas)
    {
        QVERIFY(qAbs(delta.globalX - 8) <= 1);
        QVERIFY(qAbs(delta.globalY - 8) <= 1);
        QVERIFY(delta.hasSemanticRequest);
        QCOMPARE(delta.brushCenterX, 8);
        QCOMPARE(delta.brushCenterY, 8);
        QCOMPARE(delta.requestedMaterialId, qint32(2));
        bool requestedFound = false;
        const STerrainTransitionSignature selectedSignature =
            semanticCatalog.signatureForPacked(delta.after, &requestedFound);
        QVERIFY(requestedFound);
        QCOMPARE(delta.requestedSignature.northWest, selectedSignature.northWest);
        QCOMPARE(delta.requestedSignature.northEast, selectedSignature.northEast);
        QCOMPARE(delta.requestedSignature.southEast, selectedSignature.southEast);
        QCOMPARE(delta.requestedSignature.southWest, selectedSignature.southWest);
        if(delta.globalX == 8 && delta.globalY == 8)
        {
            bool centerFound = false;
            const STerrainTransitionSignature centerSignature =
                semanticCatalog.signatureForPacked(delta.after, &centerFound);
            QVERIFY(centerFound);
            QCOMPARE(centerSignature.northWest, qint32(2));
            QCOMPARE(centerSignature.northEast, qint32(2));
            QCOMPARE(centerSignature.southEast, qint32(2));
            QCOMPARE(centerSignature.southWest, qint32(2));
            changedCenter = true;
        }
    }
    QVERIFY(changedCenter);
    const STerrainTileCommand repeatedSemantic = terrain.makeSemanticTilePaint(
        semanticPoint, ETerrainTileLayer::Land, 2, semanticCatalog,
        0x53454D41u);
    QCOMPARE(repeatedSemantic.deltas.size(), semantic.deltas.size());
    for(int index = 0; index < semantic.deltas.size(); ++index)
    {
        QCOMPARE(repeatedSemantic.deltas.at(index).globalX, semantic.deltas.at(index).globalX);
        QCOMPARE(repeatedSemantic.deltas.at(index).globalY, semantic.deltas.at(index).globalY);
        QCOMPARE(repeatedSemantic.deltas.at(index).before, semantic.deltas.at(index).before);
        QCOMPARE(repeatedSemantic.deltas.at(index).after, semantic.deltas.at(index).after);
    }
    QVERIFY(terrain.makeSemanticTilePaint(
        semanticPoint, ETerrainTileLayer::Land, 99, semanticCatalog,
        0x53454D41u).isEmpty());
    QVERIFY(terrain.apply(semantic, true));
    QVERIFY(terrain.makeSemanticTilePaint(
        semanticPoint, ETerrainTileLayer::Land, 2, semanticCatalog,
        0x53454D41u).isEmpty());
    QVERIFY(terrain.apply(semantic, false));

    STerrainTileBrush point;
    point.centerX = 0.0f;
    point.centerY = 0.0f;
    point.radius = 0.0f;
    const STerrainTileCommand land = terrain.makeTilePaint(
        point, ETerrainTileLayer::Land, 0, 1, 3);
    QCOMPARE(land.deltas.size(), 1);
    STerrainTileCommand duplicateLand = land;
    duplicateLand.deltas.append(land.deltas.front());
    CValidationReport duplicateReport;
    QVERIFY(!terrain.apply(duplicateLand, true, &duplicateReport));
    QVERIFY(duplicateReport.hasErrors());
    CTerrainTileEditHistory history;
    QVERIFY(history.execute(&terrain, land));
    QCOMPARE(SMprPackedTile::decode(
        terrain.packedTile(0, 0, ETerrainTileLayer::Land)).rotation, quint8(3));
    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.packedTile(0, 0, ETerrainTileLayer::Land), quint16(0));
    QVERIFY(history.redo(&terrain));
    CValidationReport staleTileReport;
    QVERIFY(!terrain.apply(land, true, &staleTileReport));
    QVERIFY(staleTileReport.hasErrors());

    point.centerX = 1.0f;
    const STerrainTileCommand overlay = terrain.makeTilePaint(
        point, ETerrainTileLayer::Overlay, 0, 1, 1);
    QCOMPARE(overlay.deltas.size(), 1);
    QVERIFY(terrain.apply(overlay, true));
    point.centerX = 0.0f;
    QVERIFY(terrain.makeTilePaint(point, ETerrainTileLayer::Overlay,
                                  0, 1, 1).isEmpty());

    STerrainTileCommand inactive;
    STerrainTileDelta inactiveDelta;
    inactiveDelta.globalX = 0;
    inactiveDelta.globalY = 0;
    inactiveDelta.layer = ETerrainTileLayer::Overlay;
    inactiveDelta.before = quint16(0xFFFF);
    inactiveDelta.after = quint16(1);
    inactive.deltas.append(inactiveDelta);
    CValidationReport inactiveReport;
    QVERIFY(!terrain.apply(inactive, true, &inactiveReport));
    QVERIFY(inactiveReport.hasErrors());

    STerrainTileCommand outsideNamespace;
    STerrainTileDelta invalidDelta;
    invalidDelta.globalX = 2;
    invalidDelta.globalY = 0;
    invalidDelta.before = terrain.packedTile(2, 0, ETerrainTileLayer::Land);
    SMprPackedTile invalidSelection;
    invalidSelection.atlasIndex = 1;
    invalidDelta.after = invalidSelection.encode();
    outsideNamespace.deltas.append(invalidDelta);
    CValidationReport namespaceReport;
    QVERIFY(!terrain.apply(outsideNamespace, true, &namespaceReport));
    QVERIFY(namespaceReport.hasErrors());

    CMprEditTransaction transaction("paint synthetic land and overlay tiles");
    CValidationReport editReport;
    QVERIFY(terrain.appendSectorEdits(&transaction, &editReport));
    QCOMPARE(transaction.edits().size(), 1);
    QByteArray candidateBytes;
    QVERIFY(baseline.applyTransaction(transaction, &candidateBytes, &editReport));
    CMprDocument candidate;
    CValidationReport candidateReport;
    QVERIFY(candidate.loadBytes(candidateBytes, "tile-paint-candidate.mpr", &candidateReport));
    QVERIFY(!candidateReport.hasErrors());
    QCOMPARE(CMprSemanticDiff::compare(baseline, candidate).entries().size(), 1);

    const SMprSectorInfo& sector = baseline.sectors().front();
    const QByteArray original = baseline.members().at(sector.memberIndex).payload;
    QByteArray localized = candidate.members().at(candidate.sectors().front().memberIndex).payload;
    localized.replace(int(sector.landTileOffset), 2,
                      original.mid(int(sector.landTileOffset), 2));
    localized.replace(int(sector.overlayTileOffset + 2), 2,
                      original.mid(int(sector.overlayTileOffset + 2), 2));
    QCOMPARE(localized, original);

    CTerrainEditModel reopened;
    QVERIFY(reopened.load(candidate));
    QCOMPARE(reopened.packedTile(0, 0, ETerrainTileLayer::Land), land.deltas.front().after);
    QCOMPARE(reopened.packedTile(1, 0, ETerrainTileLayer::Overlay),
             overlay.deltas.front().after);

    STerrainBrush heightPoint;
    heightPoint.centerX = 10.0f;
    heightPoint.centerY = 10.0f;
    heightPoint.radius = 0.0f;
    heightPoint.falloff = ETerrainBrushFalloff::Constant;
    QVERIFY(terrain.apply(terrain.makeRaiseLower(heightPoint, 100.0f), true));
    CMprEditTransaction combined("combine height and tile edits");
    QVERIFY(terrain.appendSectorEdits(&combined));
    QCOMPARE(combined.edits().size(), 1);
    QByteArray combinedBytes;
    QVERIFY(baseline.applyTransaction(combined, &combinedBytes));
    CMprDocument combinedCandidate;
    QVERIFY(combinedCandidate.loadBytes(combinedBytes, "combined-height-tile.mpr"));
    CTerrainEditModel combinedReopened;
    QVERIFY(combinedReopened.load(combinedCandidate));
    QCOMPARE(combinedReopened.height(10, 10), quint16(100));
    QCOMPARE(combinedReopened.packedTile(0, 0, ETerrainTileLayer::Land),
             land.deltas.front().after);
    QCOMPARE(combinedReopened.packedTile(1, 0, ETerrainTileLayer::Overlay),
             overlay.deltas.front().after);
}

void EnduranceDocumentTests::zone1NamespaceTemplateSeparatesLandOverlayAndVariants()
{
    CValidationReport report;
    const STerrainAtlasAnalysis analysis =
        CTerrainAtlasAnalyzer::analyze("zone1", 512, &report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    QCOMPARE(analysis.templateName, QString("Zone1 stock slot layout"));
    QCOMPARE(analysis.materials.size(), 9);
    QCOMPARE(analysis.describedSlots, 512);
    int landMaterials = 0;
    int overlayMaterials = 0;
    for(const STerrainAtlasMaterial& material : analysis.materials)
    {
        if(material.layer == ETerrainAtlasLayer::Land)
            ++landMaterials;
        else
            ++overlayMaterials;
    }
    QCOMPARE(landMaterials, 7);
    QCOMPARE(overlayMaterials, 2);
    QCOMPARE(analysis.materials.at(1).name, QString("Ground / common"));
    QCOMPARE(analysis.materials.at(2).name, QString("Grass / dark"));
    QCOMPARE(analysis.materials.at(3).name, QString("Grass / common"));
    QCOMPARE(std::count_if(analysis.materials.constBegin(), analysis.materials.constEnd(),
                           [](const STerrainAtlasMaterial& material) {
        return material.underwater;
    }), 2);

    SMprPackedTile packed;
    packed.tileIndex = 50;
    bool found = false;
    const STerrainTransitionSignature signature =
        analysis.landCatalog.signatureForPacked(packed.encode(), &found);
    QVERIFY(found);
    QCOMPARE(signature.northWest, 4);
    QCOMPARE(signature.northEast, 4);
    QCOMPARE(signature.southEast, 3);
    QCOMPARE(signature.southWest, 3);
    packed.tileIndex = 16;
    analysis.landCatalog.signatureForPacked(packed.encode(), &found);
    QVERIFY(!found);
    analysis.overlayCatalog.signatureForPacked(packed.encode(), &found);
    QVERIFY(found);
    const auto stillWater = std::find_if(
        analysis.materials.constBegin(), analysis.materials.constEnd(),
        [](const STerrainAtlasMaterial& material) { return material.id == 6; });
    QVERIFY(stillWater != analysis.materials.constEnd());
    QCOMPARE(stillWater->representativeSlot, 16);

    const QVector<qint32> selectableLand({2, 3, 4, 7, 9});
    for(qint32 target : selectableLand)
    {
        for(qint32 background : selectableLand)
        {
            const QVector<qint32> path =
                analysis.landCatalog.shortestMaterialPath(target, background);
            QVERIFY2(!path.isEmpty(), qPrintable(QString("No route %1 -> %2")
                .arg(target).arg(background)));
            QCOMPARE(path.front(), target);
            QCOMPARE(path.back(), background);
            for(qint32 routedMaterial : path)
                QVERIFY(selectableLand.contains(routedMaterial));
            for(int edge = 1; edge < path.size(); ++edge)
            {
                for(int mask = 0; mask < 16; ++mask)
                {
                    if(mask == 5 || mask == 10)
                        continue;
                    STerrainTransitionSignature routed;
                    routed.northWest = (mask & 1) ? path.at(edge - 1) : path.at(edge);
                    routed.northEast = (mask & 2) ? path.at(edge - 1) : path.at(edge);
                    routed.southEast = (mask & 4) ? path.at(edge - 1) : path.at(edge);
                    routed.southWest = (mask & 8) ? path.at(edge - 1) : path.at(edge);
                    bool routedFound = false;
                    analysis.landCatalog.select(routed, 0, 0, 0, &routedFound);
                    QVERIFY2(routedFound, qPrintable(QString("Missing routed mask %1 on %2 -> %3")
                        .arg(mask).arg(path.at(edge - 1)).arg(path.at(edge))));
                }
            }
        }
    }
    QCOMPARE(analysis.overlayCatalog.shortestMaterialPath(5, 6),
             QVector<qint32>({5, 6}));
    QCOMPARE(analysis.overlayCatalog.shortestMaterialPath(6, 5),
             QVector<qint32>({6, 5}));

    STerrainTransitionSignature sideEdge;
    sideEdge.northWest = 4;
    sideEdge.northEast = 2;
    sideEdge.southEast = 2;
    sideEdge.southWest = 4;
    bool normalFound = false;
    bool paintFound = false;
    const SMprPackedTile normalSide = SMprPackedTile::decode(
        analysis.landCatalog.select(sideEdge, 100, 100, 12345u, &normalFound));
    const SMprPackedTile paintedSide = SMprPackedTile::decode(
        analysis.landCatalog.selectForPaint(sideEdge, 100, 100, 12345u, &paintFound));
    QVERIFY(normalFound);
    QVERIFY(paintFound);
    QCOMPARE(paintedSide.atlasSlot(), normalSide.atlasSlot());
    QCOMPARE(paintedSide.rotation, normalSide.rotation);

    // Manual R14 evidence plus the renderer's row-zero geometry establish one
    // conversion for every shape: catalog image Y is the reverse of terrain
    // grid Y. This fixture-independent nine-cell contract fails under every
    // rejected half-edge-only rotation workaround.
    for(int dy = -1; dy <= 1; ++dy)
    {
        for(int dx = -1; dx <= 1; ++dx)
        {
            STerrainTransitionSignature stamp;
            stamp.northWest = (dx >= 0 && dy >= 0) ? 2 : 4;
            stamp.northEast = (dx <= 0 && dy >= 0) ? 2 : 4;
            stamp.southEast = (dx <= 0 && dy <= 0) ? 2 : 4;
            stamp.southWest = (dx >= 0 && dy <= 0) ? 2 : 4;
            bool authoredFound = false;
            bool paintedFound = false;
            analysis.landCatalog.select(stamp, 100 + dx, 100 + dy,
                                        12345u, &authoredFound);
            const quint16 paintedPacked = analysis.landCatalog.selectForPaint(
                stamp, 100 + dx, 100 + dy, 12345u, &paintedFound);
            QVERIFY(authoredFound);
            QVERIFY(paintedFound);
            bool decodedFound = false;
            const STerrainTransitionSignature decoded =
                analysis.landCatalog.signatureForPacked(paintedPacked, &decodedFound);
            QVERIFY(decodedFound);
            QCOMPARE(decoded.northWest, stamp.southWest);
            QCOMPARE(decoded.northEast, stamp.southEast);
            QCOMPARE(decoded.southEast, stamp.northEast);
            QCOMPARE(decoded.southWest, stamp.northWest);

            // Apply the independently specified renderer grid-Y conversion:
            // the visible result must equal the solver request at all corners.
            STerrainTransitionSignature visible;
            visible.northWest = decoded.southWest;
            visible.northEast = decoded.southEast;
            visible.southEast = decoded.northEast;
            visible.southWest = decoded.northWest;
            QCOMPARE(visible.northWest, stamp.northWest);
            QCOMPARE(visible.northEast, stamp.northEast);
            QCOMPARE(visible.southEast, stamp.southEast);
            QCOMPARE(visible.southWest, stamp.southWest);
        }
    }

    CValidationReport unknownReport;
    QVERIFY(!CTerrainAtlasAnalyzer::analyze("custom", 512, &unknownReport).usable);
    QVERIFY(unknownReport.hasErrors());
    CValidationReport countReport;
    QVERIFY(!CTerrainAtlasAnalyzer::analyze("zone1", 448, &countReport).usable);
    QVERIFY(countReport.hasErrors());
}

void EnduranceDocumentTests::zone1NamespaceTemplateMatchesPrivateFixture()
{
    const QString fixture = qEnvironmentVariable("ENDURANCE_ZONE1_MPR_FIXTURE");
    if(fixture.isEmpty())
        QSKIP("Set ENDURANCE_ZONE1_MPR_FIXTURE to a copied private Zone1 MPR.");
    CMprDocument document;
    CValidationReport openReport;
    QVERIFY2(document.load(QFileInfo(fixture), &openReport), qPrintable(openReport.toText()));
    QCOMPARE(document.mapInfo().innerNamespace.toLower(), QString("zone1"));
    CTerrainEditModel terrain;
    CValidationReport terrainReport;
    QVERIFY2(terrain.load(document, &terrainReport), qPrintable(terrainReport.toText()));
    CValidationReport templateReport;
    const STerrainAtlasAnalysis analysis = CTerrainAtlasAnalyzer::analyze(
        document.mapInfo().innerNamespace, document.mapInfo().tileTypes.size(), &templateReport);
    QVERIFY2(analysis.usable, qPrintable(templateReport.toText()));

    int decoded = 0;
    for(const ETerrainTileLayer layer : {ETerrainTileLayer::Land, ETerrainTileLayer::Overlay})
    {
        const CTerrainTransitionCatalog& catalog = analysis.catalog(layer);
        for(int y = 0; y < terrain.tileCountY(); ++y)
        {
            for(int x = 0; x < terrain.tileCountX(); ++x)
            {
                bool available = false;
                const quint16 packed = terrain.packedTile(x, y, layer, &available);
                if(!available)
                    continue;
                bool found = false;
                catalog.signatureForPacked(packed, &found);
                QVERIFY2(found, qPrintable(QString("Undescribed %1 tile at %2,%3: 0x%4")
                    .arg(layer == ETerrainTileLayer::Land ? "land" : "overlay")
                    .arg(x).arg(y).arg(packed, 4, 16, QLatin1Char('0'))));
                ++decoded;
            }
        }
    }
    QVERIFY(decoded > 0);

    QMap<qint32, qint64> uniformLandCounts;
    QMap<qint32, qint64> uniformLandWithOverlayCounts;
    for(int y = 0; y < terrain.tileCountY(); ++y)
    {
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const quint16 landPacked = terrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            if(!available)
                continue;
            bool found = false;
            const STerrainTransitionSignature signature =
                analysis.landCatalog.signatureForPacked(landPacked, &found);
            if(!found || signature.northWest != signature.northEast
               || signature.northWest != signature.southEast
               || signature.northWest != signature.southWest)
                continue;
            ++uniformLandCounts[signature.northWest];
            terrain.packedTile(x, y, ETerrainTileLayer::Overlay, &available);
            if(available)
                ++uniformLandWithOverlayCounts[signature.northWest];
        }
    }
    // The two source families named ground/darkwater and ground/water are
    // underwater bases in the copied Zone1 fixture, not ordinary land paint:
    // every uniform occurrence sits beneath an active overlay.
    QVERIFY(uniformLandCounts.value(1) > 0);
    QCOMPARE(uniformLandWithOverlayCounts.value(1), uniformLandCounts.value(1));
    QVERIFY(uniformLandCounts.value(8) > 0);
    QCOMPARE(uniformLandWithOverlayCounts.value(8), uniformLandCounts.value(8));

    // Correct atlas flip and packed-rotation interpretation makes the shipped
    // map an almost perfectly coherent corner lattice. Preserve the few
    // authored exceptions, but reject the thousands of disagreements produced
    // by the former image-space interpretation.
    qint64 disagreements = 0;
    qint64 comparisons = 0;
    for(int y = 0; y < terrain.tileCountY(); ++y)
    {
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool found = false;
            const STerrainTransitionSignature here =
                analysis.landCatalog.signatureForPacked(terrain.packedTile(
                    x, y, ETerrainTileLayer::Land), &found);
            QVERIFY(found);
            if(x + 1 < terrain.tileCountX())
            {
                const STerrainTransitionSignature east =
                    analysis.landCatalog.signatureForPacked(terrain.packedTile(
                        x + 1, y, ETerrainTileLayer::Land), &found);
                QVERIFY(found);
                disagreements += here.northEast != east.northWest;
                disagreements += here.southEast != east.southWest;
                comparisons += 2;
            }
            if(y + 1 < terrain.tileCountY())
            {
                const STerrainTransitionSignature north =
                    analysis.landCatalog.signatureForPacked(terrain.packedTile(
                        x, y + 1, ETerrainTileLayer::Land), &found);
                QVERIFY(found);
                disagreements += here.northWest != north.southWest;
                disagreements += here.northEast != north.southEast;
                comparisons += 2;
            }
        }
    }
    QVERIFY(comparisons > 100000);
    QVERIFY(disagreements <= 100);

    QSet<qint32> paintableMaterials;
    STerrainTileBrush brush;
    brush.radius = 0.0f;
    brush.shape = ETerrainTileBrushShape::Square;
    for(const STerrainAtlasMaterial& material : analysis.materials)
    {
        const ETerrainTileLayer layer = material.layer == ETerrainAtlasLayer::Land
            ? ETerrainTileLayer::Land : ETerrainTileLayer::Overlay;
        for(int y = 0; y < terrain.tileCountY() && !paintableMaterials.contains(material.id); ++y)
        {
            for(int x = 0; x < terrain.tileCountX(); ++x)
            {
                brush.centerX = float(x);
                brush.centerY = float(y);
                CValidationReport paintReport;
                const STerrainTileCommand command = terrain.makeSemanticTilePaint(
                    brush, layer, material.id, analysis.catalog(layer), 12345u, &paintReport);
                if(!command.isEmpty())
                {
                    paintableMaterials.insert(material.id);
                    break;
                }
            }
        }
    }
    QCOMPARE(paintableMaterials.size(), analysis.materials.size());

    // Coordinates captured from the R9 manual refusal log. Every selectable
    // land family must now either produce a routed stamp or be an exact no-op;
    // none may fail because a direct pair is absent from the atlas.
    const QVector<QPoint> refusalProbes({QPoint(97, 76), QPoint(95, 74),
                                         QPoint(102, 97)});
    brush.radius = 0.0f; // UI brush size 1: one painted core tile.
    QSet<qint32> changedMaterials;
    for(const STerrainAtlasMaterial& material : analysis.materials)
    {
        if(material.layer != ETerrainAtlasLayer::Land || material.underwater)
            continue;
        for(const QPoint& probe : refusalProbes)
        {
            brush.centerX = float(probe.x());
            brush.centerY = float(probe.y());
            CValidationReport paintReport;
            const STerrainTileCommand paint = terrain.makeSemanticTilePaint(
                brush, ETerrainTileLayer::Land, material.id,
                analysis.landCatalog, 12345u, &paintReport);
            QVERIFY2(!paintReport.hasErrors(), qPrintable(QString("Material %1 at %2,%3:\n%4")
                .arg(material.id).arg(probe.x()).arg(probe.y())
                .arg(paintReport.toText())));
            if(!paint.deltas.isEmpty())
            {
                changedMaterials.insert(material.id);
                for(const STerrainTileDelta& delta : paint.deltas)
                {
                    QVERIFY(qAbs(delta.globalX - probe.x()) <= 2);
                    QVERIFY(qAbs(delta.globalY - probe.y()) <= 2);
                }
            }
        }
    }
    // At most the family already covering all probes may legitimately be a
    // no-op. This catches the R9 failure mode where only one family painted.
    QVERIFY(changedMaterials.size() >= 4);
}

void EnduranceDocumentTests::terrainHeightTransactionReopensAndPreservesOverlay()
{
    const QByteArray bytes = syntheticMpr();
    CMprDocument baseline;
    CValidationReport baselineReport;
    QVERIFY2(baseline.loadBytes(bytes, "overlay-height-baseline.mpr", &baselineReport),
             qPrintable(baselineReport.toText()));
    QCOMPARE(baseline.sectors().size(), 1);
    QVERIFY(baseline.sectors().front().hasOverlay);

    CTerrainEditModel terrain;
    CValidationReport terrainReport;
    QVERIFY2(terrain.load(baseline, &terrainReport), qPrintable(terrainReport.toText()));
    STerrainBrush brush;
    brush.centerX = 10.0f;
    brush.centerY = 10.0f;
    brush.radius = 0.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    CTerrainEditHistory history;
    QVERIFY(history.execute(&terrain, terrain.makeRaiseLower(brush, 2000.0f)));

    CMprEditTransaction transaction("raise one overlay-sector land vertex");
    CValidationReport editReport;
    QVERIFY2(terrain.appendSectorEdits(&transaction, &editReport),
             qPrintable(editReport.toText()));
    QCOMPARE(transaction.edits().size(), 1);
    const SMprMemberEdit edit = transaction.edits().front();
    const QByteArray originalSector = baseline.memberPayload(edit.memberName);
    QCOMPARE(edit.replacementPayload.size(), originalSector.size());
    const int overlayOffset = int(baseline.sectors().front().overlayVertexOffset);
    QVERIFY(overlayOffset > 0);
    QCOMPARE(edit.replacementPayload.mid(overlayOffset), originalSector.mid(overlayOffset));
    QCOMPARE(sectorHeight(edit.replacementPayload, 10, 10), quint16(2000));
    QCOMPARE(sectorPackedNormal(edit.replacementPayload, 2, 2),
             sectorPackedNormal(originalSector, 2, 2));
    QSet<int> allowedChangedBytes;
    const int changedVertexOffset = 5 + (10 * 33 + 10) * 8;
    allowedChangedBytes.insert(changedVertexOffset + 2);
    allowedChangedBytes.insert(changedVertexOffset + 3);
    for(int y = 9; y <= 11; ++y)
    {
        for(int x = 9; x <= 11; ++x)
        {
            const int normalOffset = 5 + (y * 33 + x) * 8 + 4;
            for(int byte = 0; byte < 4; ++byte)
                allowedChangedBytes.insert(normalOffset + byte);
        }
    }
    for(int offset = 0; offset < originalSector.size(); ++offset)
    {
        if(!allowedChangedBytes.contains(offset))
            QCOMPARE(edit.replacementPayload.at(offset), originalSector.at(offset));
    }

    QByteArray candidateBytes;
    CValidationReport transactionReport;
    QVERIFY2(baseline.applyTransaction(transaction, &candidateBytes, &transactionReport),
             qPrintable(transactionReport.toText()));
    CMprDocument candidate;
    CValidationReport candidateReport;
    QVERIFY2(candidate.loadBytes(candidateBytes, "overlay-height-candidate.mpr", &candidateReport),
             qPrintable(candidateReport.toText()));
    QCOMPARE(candidate.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    QCOMPARE(candidate.memberPayload("caveset.mp"), baseline.memberPayload("caveset.mp"));
    QCOMPARE(candidate.memberPayload(edit.memberName), edit.replacementPayload);
    const CMprSemanticDiff diff = CMprSemanticDiff::compare(baseline, candidate);
    QCOMPARE(diff.entries().size(), 1);
    QCOMPARE(diff.entries().front().memberName, edit.memberName);
}

void EnduranceDocumentTests::conflictingTerrainSeamIsPreservedAndNotEditable()
{
    QVector<STerrainSectorSource> sources = twoSectorTerrain(1000);
    QByteArray right = sources[1].payload;
    const int offset = 5 + (10 * 33) * 8 + 2;
    right[offset] = char(0xD0);
    right[offset + 1] = char(0x07); // 2000, little endian
    sources[1].payload = right;

    CTerrainEditModel terrain;
    CValidationReport loadReport;
    QVERIFY(terrain.loadSources(2, 1, 100.0f, sources, &loadReport));
    QVERIFY(terrain.hasSeamConflicts());
    QCOMPARE(sectorHeight(terrain.sectorPayload(0, 0), 32, 10), quint16(1000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(1, 0), 0, 10), quint16(2000));

    STerrainBrush brush;
    brush.centerX = 32.0f;
    brush.centerY = 10.0f;
    brush.radius = 0.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const STerrainHeightCommand command = terrain.makeRaiseLower(brush, 100.0f);
    CValidationReport editReport;
    QVERIFY(!terrain.apply(command, true, &editReport));
    QCOMPARE(sectorHeight(terrain.sectorPayload(0, 0), 32, 10), quint16(1000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(1, 0), 0, 10), quint16(2000));

    brush.centerX = 31.0f;
    const STerrainHeightCommand adjacent = terrain.makeRaiseLower(brush, 100.0f);
    QVERIFY(!terrain.apply(adjacent, true));
    CMprEditTransaction transaction("normal touches conflicting seam");
    CValidationReport serializationReport;
    QVERIFY(terrain.appendSectorEdits(&transaction, &serializationReport));
    QVERIFY(transaction.isEmpty());
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

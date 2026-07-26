#include <QtTest>
#include <QCryptographicHash>
#include <QSet>
#include <QTemporaryFile>
#include <QTemporaryDir>

#include <limits>

#include "endurance_document.h"
#include "landscape_edit_policy.h"
#include "operation_history_policy.h"
#include "terrain_edit_model.h"
#include "terrain_atlas_analyzer.h"
#include "terrain_atlas_profile.h"
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

QByteArray syntheticMp(quint32 sectorCountX = 1, quint32 sectorCountY = 1)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0xCE4AF672) << float(100.0f)
           << sectorCountX << sectorCountY
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

QByteArray syntheticMpr(const QByteArray& sectorPayload = syntheticOverlaySector())
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
    members.append({QByteArray("CaveSet000000.SEC"), sectorPayload, 0x90A0B0C0, 0});

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

QByteArray syntheticTwoSectorMpr(const QByteArray& leftSector,
                                 const QByteArray& rightSector)
{
    struct Member
    {
        QByteArray name;
        QByteArray payload;
        quint32 timestamp;
        quint32 offset;
    };
    QVector<Member> members;
    members.append({QByteArray("CaveSet.MP"), syntheticMp(2, 1), 0x10203040, 0});
    members.append({QByteArray("CaveSet000000.SEC"), leftSector, 0x50607080, 0});
    members.append({QByteArray("CaveSet001000.SEC"), rightSector, 0x90A0B0C0, 0});

    QByteArray bytes(16, '\0');
    for(int index = 0; index < members.size(); ++index)
    {
        members[index].offset = quint32(bytes.size());
        bytes.append(members.at(index).payload);
        const int padding = (16 - (bytes.size() % 16)) % 16;
        bytes.append(QByteArray(padding, char(0xC0 + index)));
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
    void persistentPaintOperationSurvivesDocumentTransition();
    void landscapeLayerChangesResetBrushSelection();
    void landscapeWheelDirectionUsesAvailableDelta();
    void sculptCadenceIsIndependentOfSpatialInterpolation();
    void unchangedMobIsByteIdentical();
    void duplicateMobIdsAreWarningsOnly();
    void vulcansmokeIsACriticalNonBlockingFinding();
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
    void worldObjectToLeverPreservesRawChildren();
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
    void mprMaxZPatchIsProvenanceCheckedAndLocal();
    void mprVerticalScaleTransactionReopensWithDerivedNormals();
    void mprLengthChangingTransactionsRelocateAndStaleFailClosed();
    void failedStagedMprSaveLeavesNoDestination();
    void stagedMprTransactionProvesExactSemanticScopeWithoutDestination();
    void mprSaveCoordinatorBacksUpAndAtomicallyCommits();
    void mprSaveCoordinatorCommitsLengthChangingOverlay();
    void mprSaveCoordinatorReplacesSourceAndRebasesTerrain();
    void mprSaveCoordinatorRejectsSourceRace();
    void mprSaveCoordinatorFailuresLeaveDestinationUntouched();
    void privateMprSaveCoordinatorCorpusUsesCopiesOnly();
    void unchangedExistingFileComparisonIsReadOnly();
    void canonicalTerrainSeamsAreEditedAndUndoneTogether();
    void terrainBrushToolsAreDeterministicAndFailClosed();
    void terrainHeightmapImportIsDeterministicAndSeamAware();
    void globalTerrainHeightOperationsKeepDimensionsAndSeams();
    void terrainVerticalScalePoliciesAreUndoableAndExplicit();
    void globalTerrainHeightOperationsRejectInvalidRanges();
    void terrainDiagnosticsArePreApplyAndPolicyExplicit();
    void terrainTilePaintingIsUndoableLocalizedAndNamespaceSafe();
    void terrainTileStrokeCompositionKeepsFirstAndFinalValues();
    void terrainHeightStrokeCompositionKeepsFirstAndFinalValues();
    void zone1NamespaceTemplateSeparatesLandOverlayAndVariants();
    void semanticAtlasProposalIsDeterministicAndApprovalGated();
    void semanticAtlasTransitionsCannotTransitivelyMergeMaterialAnchors();
    void semanticAtlasTransitionsCrossTileTypeMetadata();
    void authoredAtlasProfileMatchesZone1Catalog();
    void semanticAtlasProfileRejectsBindingMismatchAndAmbiguity();
    void semanticAtlasReviewEditsAndStoreAreDeterministic();
    void zone1NamespaceTemplateMatchesPrivateFixture();
    void terrainHeightTransactionReopensAndPreservesOverlay();
    void overlayActivationDeactivationIsLosslessAndUndoable();
    void overlaySculptChangesOnlyActiveOverlayGeometry();
    void overlaySculptSynchronizesSectorSeam();
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

void EnduranceDocumentTests::persistentPaintOperationSurvivesDocumentTransition()
{
    QVERIFY(!operationRequiresCancellationForDocumentTransition(
        eOperationMethodSelect));
    QVERIFY(!operationRequiresCancellationForDocumentTransition(
        eOperationMethodTileBrush));
    QVERIFY(operationRequiresCancellationForDocumentTransition(
        eOperationMethodMoveAxis));
    QVERIFY(operationRequiresCancellationForDocumentTransition(
        eOperationMethodRotateAxis));
    QVERIFY(operationRequiresCancellationForDocumentTransition(
        eOperationMethodScaleAxis));
}

void EnduranceDocumentTests::landscapeWheelDirectionUsesAvailableDelta()
{
    QCOMPARE(landscapeWheelStep(120, 120, 0), 1);
    QCOMPARE(landscapeWheelStep(-120, -120, 0), -1);
    QCOMPARE(landscapeWheelStep(0, 120, 0), 1);
    QCOMPARE(landscapeWheelStep(0, -120, 0), -1);
    QCOMPARE(landscapeWheelStep(0, 0, 7), 1);
    QCOMPARE(landscapeWheelStep(0, 0, -7), -1);
    QCOMPARE(landscapeWheelStep(0, 0, 0), 0);
}

void EnduranceDocumentTests::sculptCadenceIsIndependentOfSpatialInterpolation()
{
    const float oneStepFraction = landscapeSculptSubstepFraction(1);
    const float eightStepFraction = landscapeSculptSubstepFraction(8);
    QCOMPARE(oneStepFraction, 1.0f);
    QVERIFY(qAbs(eightStepFraction * 8.0f - oneStepFraction) < 0.000001f);

    const float stepSize = 5000.0f;
    const float strength = 0.35f;
    const float stationaryRaw = stepSize * strength * oneStepFraction;
    float movingRaw = 0.0f;
    for(int step = 0; step < 8; ++step)
        movingRaw += stepSize * strength * eightStepFraction;
    QVERIFY(qAbs(stationaryRaw - 1750.0f) < 0.0001f);
    QVERIFY(qAbs(movingRaw - stationaryRaw) < 0.0001f);

    const float oneStampBlend = landscapeSculptEffectiveStrength(strength, 1.0f);
    float subdividedRemaining = 1.0f;
    for(int step = 0; step < 8; ++step)
        subdividedRemaining *= 1.0f - landscapeSculptEffectiveStrength(
            strength, eightStepFraction);
    QVERIFY(qAbs(oneStampBlend - (1.0f - subdividedRemaining)) < 0.00001f);
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

void EnduranceDocumentTests::vulcansmokeIsACriticalNonBlockingFinding()
{
    const QByteArray bytes = validMob(section(0xDD01,
        section(0xDD06, u32(0x2005u))));
    CMobDocument document;
    CValidationReport report;
    QVERIFY(document.loadBytes(bytes, "vulcansmoke-control.mob", &report));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!report.hasErrors());

    int criticalFindings = 0;
    for(const SValidationFinding& finding : report.findings())
    {
        if(finding.code != "mob.semantic.vulcansmoke_runtime_crash")
            continue;
        ++criticalFindings;
        QCOMPARE(finding.severity, EValidationSeverity::Critical);
        QVERIFY(finding.message.contains("8197"));
        QVERIFY(finding.message.contains("terminate", Qt::CaseInsensitive));
    }
    QCOMPARE(criticalFindings, 1);
    QVERIFY(report.toText().contains("CRITICAL mob.semantic.vulcansmoke_runtime_crash"));
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

void EnduranceDocumentTests::worldObjectToLeverPreservesRawChildren()
{
    const QByteArray opaque = section(0xDEADBEEF, QByteArray::fromHex("001122334455"));
    const QByteArray source = section(0xB001,
        section(0xB002, u32(1234)) + section(0xB003, u32(54)) +
        section(0xB004, "converted") + opaque);
    CMobDocument::SLeverConversionValues defaults;
    QByteArray converted;
    CValidationReport conversionReport;
    QVERIFY(CMobDocument::convertWorldObjectSubtreeToLever(
        source, defaults, &converted, &conversionReport));

    QDataStream header(converted);
    header.setByteOrder(QDataStream::LittleEndian);
    quint32 type = 0;
    quint32 length = 0;
    header >> type >> length;
    QCOMPARE(type, quint32(0xBBAC0000));
    QCOMPARE(length, quint32(converted.size()));
    QVERIFY(converted.contains(opaque));
    QVERIFY(converted.endsWith(opaque));

    CMobDocument document;
    CValidationReport parseReport;
    QVERIFY(document.loadBytes(validMob(section(0xB000, converted)),
                               "converted-lever.mob", &parseReport));
    QCOMPARE(document.objects().size(), 1);
    QCOMPARE(document.objects().front().nid, quint32(1234));
    QCOMPARE(document.objects().front().objectType, quint32(60));

    QByteArray rejected;
    QVERIFY(!CMobDocument::convertWorldObjectSubtreeToLever(
        object(1234), defaults, &rejected, nullptr));

    for(const quint32 sourceType : {quint32(53), quint32(55)})
    {
        const QByteArray scenery = section(0xB001,
            section(0xB002, u32(2000 + sourceType)) +
            section(0xB003, u32(sourceType)) +
            section(0xB004, "scenery"));
        QByteArray sceneryLever;
        CValidationReport sceneryReport;
        QVERIFY2(CMobDocument::convertWorldObjectSubtreeToLever(
                     scenery, defaults, &sceneryLever, &sceneryReport),
                 qPrintable(sceneryReport.toText()));

        CMobDocument sceneryDocument;
        QVERIFY(sceneryDocument.loadBytes(
            validMob(section(0xB000, sceneryLever)), "converted-scenery.mob"));
        QCOMPARE(sceneryDocument.objects().size(), 1);
        QCOMPARE(sceneryDocument.objects().front().objectType, quint32(60));
    }

    const QByteArray unsupported = section(0xB001,
        section(0xB002, u32(9999)) + section(0xB003, u32(51)) +
        section(0xB004, "unsupported"));
    CValidationReport unsupportedReport;
    QVERIFY(!CMobDocument::convertWorldObjectSubtreeToLever(
        unsupported, defaults, &rejected, &unsupportedReport));
    QVERIFY(unsupportedReport.toText().contains("OBJTYPE 51"));
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

void EnduranceDocumentTests::mprMaxZPatchIsProvenanceCheckedAndLocal()
{
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(), "maxz-baseline.mpr", &loadReport),
             qPrintable(loadReport.toText()));
    const float originalMaxZ = baseline.mapInfo().maxZ;

    CMprEditTransaction noOp("no-op maxZ");
    QVERIFY(baseline.appendMaxZEdit(originalMaxZ, &noOp));
    QVERIFY(noOp.isEmpty());

    CMprEditTransaction transaction("change synthetic maxZ");
    CValidationReport editReport;
    QVERIFY2(baseline.appendMaxZEdit(originalMaxZ * 1.5f, &transaction, &editReport),
             qPrintable(editReport.toText()));
    QCOMPARE(transaction.edits().size(), 1);
    QCOMPARE(transaction.edits().front().memberName, baseline.mapInfo().memberName);

    QByteArray candidateBytes;
    QVERIFY2(baseline.applyTransaction(transaction, &candidateBytes, &editReport),
             qPrintable(editReport.toText()));
    QCOMPARE(candidateBytes.size(), baseline.originalBytes().size());

    CMprDocument candidate;
    CValidationReport reopenReport;
    QVERIFY2(candidate.loadBytes(candidateBytes, "maxz-candidate.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(candidate.mapInfo().maxZ, originalMaxZ * 1.5f);
    QCOMPARE(candidate.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    QCOMPARE(candidate.memberPayload(baseline.sectors().front().memberName),
             baseline.memberPayload(baseline.sectors().front().memberName));
    const CMprSemanticDiff diff = CMprSemanticDiff::compare(baseline, candidate);
    QCOMPARE(diff.entries().size(), 1);
    QCOMPARE(diff.entries().front().memberName, baseline.mapInfo().memberName);

    CMprEditTransaction invalid("invalid maxZ");
    CValidationReport invalidReport;
    QVERIFY(!baseline.appendMaxZEdit(0.0f, &invalid, &invalidReport));
    QVERIFY(invalid.isEmpty());
    QVERIFY(invalidReport.hasErrors());
    QVERIFY(!baseline.appendMaxZEdit(-1.0f, &invalid));
    QVERIFY(!baseline.appendMaxZEdit(std::numeric_limits<float>::infinity(), &invalid));
    QVERIFY(!baseline.appendMaxZEdit(std::numeric_limits<float>::quiet_NaN(), &invalid));
    QVERIFY(!baseline.appendMaxZEdit(originalMaxZ * 2.0f, nullptr));
}

void EnduranceDocumentTests::mprVerticalScaleTransactionReopensWithDerivedNormals()
{
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(syntheticMpr(), "scale-baseline.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(baseline, &report), qPrintable(report.toText()));
    const STerrainVerticalScaleCommand scale = terrain.makeVerticalScale(
        baseline.mapInfo().maxZ * 1.25f,
        ETerrainVerticalScalePolicy::ScaleTerrain);
    QVERIFY(scale.valid);
    QVERIFY(terrain.apply(scale, true, &report));

    CMprEditTransaction transaction("scale maxZ and rebuild derived normals");
    QVERIFY2(terrain.appendSectorEdits(&transaction, &report), qPrintable(report.toText()));
    QVERIFY2(baseline.appendMaxZEdit(terrain.maxZ(), &transaction, &report),
             qPrintable(report.toText()));
    QCOMPARE(transaction.edits().size(), 2);

    QByteArray candidateBytes;
    QVERIFY2(baseline.applyTransaction(transaction, &candidateBytes, &report),
             qPrintable(report.toText()));
    CMprDocument candidate;
    CValidationReport reopenReport;
    QVERIFY2(candidate.loadBytes(candidateBytes, "scale-candidate.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(candidate.mapInfo().maxZ, scale.afterMaxZ);
    QCOMPARE(candidate.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    const CMprSemanticDiff diff = CMprSemanticDiff::compare(baseline, candidate);
    QCOMPARE(diff.entries().size(), 2);
    QVERIFY(std::any_of(diff.entries().constBegin(), diff.entries().constEnd(),
        [&baseline](const SMprDocumentDiffEntry& entry) {
            return entry.memberName == baseline.mapInfo().memberName;
        }));
    QVERIFY(std::any_of(diff.entries().constBegin(), diff.entries().constEnd(),
        [&baseline](const SMprDocumentDiffEntry& entry) {
            return entry.memberName == baseline.sectors().front().memberName;
        }));
}

void EnduranceDocumentTests::mprLengthChangingTransactionsRelocateAndStaleFailClosed()
{
    CMprDocument document;
    CValidationReport loadReport;
    QVERIFY(document.loadBytes(syntheticMpr(), "synthetic.mpr", &loadReport));
    const SMprArchiveMember sector = document.members().at(2);

    CMprEditTransaction lengthChange("proven append-only overlay structure change");
    lengthChange.replaceMemberPayload(sector.name, sector.sha256, sector.payload + QByteArray("x"));
    QByteArray output;
    CValidationReport lengthReport;
    QVERIFY2(document.applyTransaction(lengthChange, &output, &lengthReport),
             qPrintable(lengthReport.toText()));
    CMprDocument relocated;
    CValidationReport relocatedReport;
    QVERIFY2(relocated.loadBytes(output, "relocated.mpr", &relocatedReport),
             qPrintable(relocatedReport.toText()));
    QCOMPARE(relocated.memberPayload(sector.name), sector.payload + QByteArray("x"));
    QCOMPARE(relocated.memberPayload("unknown.bin"), document.memberPayload("unknown.bin"));
    QCOMPARE(relocated.memberPayload("caveset.mp"), document.memberPayload("caveset.mp"));
    QCOMPARE(relocated.members().at(2).nextIndex, sector.nextIndex);
    QCOMPARE(relocated.members().at(2).lastWriteTime, sector.lastWriteTime);
    QCOMPARE(relocated.members().at(2).encodedName, sector.encodedName);
    QVERIFY(relocated.members().at(2).dataOffset != sector.dataOffset);
    QVERIFY(output.size() > document.originalBytes().size());

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

void EnduranceDocumentTests::stagedMprTransactionProvesExactSemanticScopeWithoutDestination()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QFileInfo destination(dir.filePath("changed.mpr"));

    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(), "staged-baseline.mpr", &loadReport),
             qPrintable(loadReport.toText()));
    const SMprArchiveMember sector = baseline.members().at(2);
    QByteArray replacement = sector.payload;
    replacement[replacement.size() - 1] = char(replacement.back() ^ 0x01);
    CMprEditTransaction transaction("change one synthetic SEC byte");
    transaction.replaceMemberPayload(sector.name, sector.sha256, replacement);

    const SStagedMprTransactionValidation result =
        validateStagedMprTransaction(baseline, transaction, destination);
    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY(!result.candidateBytes.isEmpty());
    QCOMPARE(result.size, qint64(baseline.originalBytes().size()));
    QCOMPARE(result.changedMembers, QStringList() << sector.name);
    QVERIFY(result.semanticDiff.contains(sector.name));
    QVERIFY(!destination.exists());
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0);

    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.loadBytes(result.candidateBytes, "reopened-stage.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    QCOMPARE(reopened.memberPayload(sector.name), replacement);

    CMprEditTransaction stale("stale staged edit");
    stale.replaceMemberPayload(sector.name, QByteArray(64, '0'), replacement);
    const SStagedMprTransactionValidation refused =
        validateStagedMprTransaction(baseline, stale, destination);
    QVERIFY(!refused.success);
    QVERIFY(refused.candidateBytes.isEmpty());
    QVERIFY(!destination.exists());
}

void EnduranceDocumentTests::mprSaveCoordinatorBacksUpAndAtomicallyCommits()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray original = syntheticMpr();
    const QString sourcePath = dir.filePath("source.mpr");
    const QString destinationPath = dir.filePath("destination.mpr");
    for(const QString& path : {sourcePath, destinationPath})
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
    }

    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.load(QFileInfo(sourcePath), &loadReport),
             qPrintable(loadReport.toText()));
    const SMprArchiveMember sector = baseline.members().at(2);
    QByteArray replacement = sector.payload;
    replacement[replacement.size() - 1] = char(replacement.back() ^ 0x01);
    CMprEditTransaction transaction("coordinated synthetic change");
    transaction.replaceMemberPayload(sector.name, sector.sha256, replacement);

    SMprSaveCommitRequest request;
    request.source = QFileInfo(sourcePath);
    request.destination = QFileInfo(destinationPath);
    request.backupRootOverride = dir.filePath("backups");
    const SMprSaveCommitResult result =
        commitStagedMprTransaction(baseline, transaction, request);
    QVERIFY2(result.success, qPrintable(result.phase + ": " + result.error));
    QCOMPARE(result.phase, QString("complete"));
    QVERIFY(!result.backupPath.isEmpty());

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), original);
    QFile backup(result.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);
    QFile destination(destinationPath);
    QVERIFY(destination.open(QIODevice::ReadOnly));
    const QByteArray committed = destination.readAll();
    QVERIFY(committed != original);
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(
                 committed, QCryptographicHash::Sha256).toHex()),
             result.candidateSha256);
}

void EnduranceDocumentTests::mprSaveCoordinatorCommitsLengthChangingOverlay()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray original = syntheticMpr(syntheticLandSector(900));
    const QString sourcePath = dir.filePath("source.mpr");
    const QString destinationPath = dir.filePath("destination.mpr");
    for(const QString& path : {sourcePath, destinationPath})
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
    }
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.load(QFileInfo(sourcePath), &report), qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));
    STerrainTileBrush point;
    point.centerX = 4.0f;
    point.centerY = 5.0f;
    point.radius = 0.0f;
    const SOverlaySurfaceCommand activation = terrain.makeOverlayActivation(
        point, 1, 0, 1, 0, &report);
    QVERIFY2(!activation.isEmpty(), qPrintable(report.toText()));
    QVERIFY(terrain.apply(activation, true, &report));
    CMprEditTransaction transaction("commit structural overlay activation");
    QVERIFY(terrain.appendSectorEdits(&transaction, &report));

    SMprSaveCommitRequest request;
    request.source = QFileInfo(sourcePath);
    request.destination = QFileInfo(destinationPath);
    request.backupRootOverride = dir.filePath("backups");
    const SMprSaveCommitResult result =
        commitStagedMprTransaction(baseline, transaction, request);
    QVERIFY2(result.success, qPrintable(result.phase + ": " + result.error));
    QCOMPARE(result.changedMembers,
             QStringList({QStringLiteral("CaveSet000000.SEC")}));
    QFile backup(result.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);
    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.load(QFileInfo(destinationPath), &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.sectors().front().type, quint8(3));
    CTerrainEditModel rebound;
    QVERIFY(rebound.load(reopened));
    bool available = false;
    QCOMPARE(rebound.overlayMaterialIndex(4, 5, &available), qint16(1));
    QVERIFY(available);
}

void EnduranceDocumentTests::mprSaveCoordinatorReplacesSourceAndRebasesTerrain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray original = syntheticMpr();
    const QString sourcePath = dir.filePath("source.mpr");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(original), qint64(original.size()));
    source.close();

    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.load(QFileInfo(sourcePath), &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(baseline, &report), qPrintable(report.toText()));
    STerrainBrush brush;
    brush.centerX = 1.0f;
    brush.centerY = 1.0f;
    brush.radius = 0.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const STerrainHeightCommand edit = terrain.makeRaiseLower(brush, 1.0f);
    QVERIFY(!edit.isEmpty());
    QVERIFY(terrain.apply(edit, true, &report));
    QVERIFY(terrain.isDirty());

    CMprEditTransaction transaction("replace the open synthetic MPR");
    QVERIFY2(terrain.appendSectorEdits(&transaction, &report),
             qPrintable(report.toText()));
    QVERIFY(!transaction.isEmpty());

    SMprSaveCommitRequest request;
    request.source = QFileInfo(sourcePath);
    request.destination = QFileInfo(sourcePath);
    request.backupRootOverride = dir.filePath("backups");
    const SMprSaveCommitResult result =
        commitStagedMprTransaction(baseline, transaction, request);
    QVERIFY2(result.success, qPrintable(result.phase + ": " + result.error));
    QCOMPARE(result.phase, QString("complete"));
    QVERIFY(!result.backupPath.isEmpty());

    QFile backup(result.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray committedBytes = source.readAll();
    QVERIFY(committedBytes != original);
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(
                 committedBytes, QCryptographicHash::Sha256).toHex()),
             result.candidateSha256);

    CMprDocument reboundDocument;
    CValidationReport reboundReport;
    QVERIFY2(reboundDocument.load(QFileInfo(sourcePath), &reboundReport),
             qPrintable(reboundReport.toText()));
    CTerrainEditModel reboundTerrain;
    QVERIFY2(reboundTerrain.load(reboundDocument, &reboundReport),
             qPrintable(reboundReport.toText()));
    QVERIFY(!reboundTerrain.isDirty());
    QVERIFY(reboundTerrain.apply(edit, false, &reboundReport));
    QVERIFY(reboundTerrain.isDirty());
    QVERIFY(reboundTerrain.apply(edit, true, &reboundReport));
    QVERIFY(!reboundTerrain.isDirty());
}

void EnduranceDocumentTests::mprSaveCoordinatorRejectsSourceRace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray original = syntheticMpr();
    const QString sourcePath = dir.filePath("source.mpr");
    const QString destinationPath = dir.filePath("destination.mpr");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(original), qint64(original.size()));
    source.close();
    QFile destination(destinationPath);
    QVERIFY(destination.open(QIODevice::WriteOnly));
    QCOMPARE(destination.write(original), qint64(original.size()));
    destination.close();

    CMprDocument baseline;
    CValidationReport report;
    QVERIFY(baseline.load(QFileInfo(sourcePath), &report));
    const SMprArchiveMember sector = baseline.members().at(2);
    QByteArray replacement = sector.payload;
    replacement[replacement.size() - 1] = char(replacement.back() ^ 0x01);
    CMprEditTransaction transaction("source race");
    transaction.replaceMemberPayload(sector.name, sector.sha256, replacement);

    QVERIFY(source.open(QIODevice::Append));
    QCOMPARE(source.write("external-change"), qint64(15));
    source.close();
    SMprSaveCommitRequest request;
    request.source = QFileInfo(sourcePath);
    request.destination = QFileInfo(destinationPath);
    request.backupRootOverride = dir.filePath("backups");
    const SMprSaveCommitResult result =
        commitStagedMprTransaction(baseline, transaction, request);
    QVERIFY(!result.success);
    QCOMPARE(result.phase, QString("source-check"));
    QVERIFY(destination.open(QIODevice::ReadOnly));
    QCOMPARE(destination.readAll(), original);

}

void EnduranceDocumentTests::mprSaveCoordinatorFailuresLeaveDestinationUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray original = syntheticMpr();
    const QString sourcePath = dir.filePath("source.mpr");
    const QString destinationPath = dir.filePath("destination.mpr");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(original), qint64(original.size()));
    source.close();
    QFile destination(destinationPath);
    QVERIFY(destination.open(QIODevice::WriteOnly));
    QCOMPARE(destination.write(original), qint64(original.size()));
    destination.close();

    CMprDocument baseline;
    CValidationReport report;
    QVERIFY(baseline.load(QFileInfo(sourcePath), &report));
    const SMprArchiveMember sector = baseline.members().at(2);
    QByteArray replacement = sector.payload;
    replacement[replacement.size() - 1] = char(replacement.back() ^ 0x01);
    CMprEditTransaction transaction("failure preservation");
    transaction.replaceMemberPayload(sector.name, sector.sha256, replacement);

    const QString blockerPath = dir.filePath("backup-root-blocker");
    QFile blocker(blockerPath);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    QCOMPARE(blocker.write("x"), qint64(1));
    blocker.close();
    SMprSaveCommitRequest backupFailure;
    backupFailure.source = QFileInfo(sourcePath);
    backupFailure.destination = QFileInfo(destinationPath);
    backupFailure.backupRootOverride = QDir(blockerPath).filePath("impossible");
    const SMprSaveCommitResult backupResult =
        commitStagedMprTransaction(baseline, transaction, backupFailure);
    QVERIFY(!backupResult.success);
    QCOMPARE(backupResult.phase, QString("backup"));
    QVERIFY(destination.open(QIODevice::ReadOnly));
    QCOMPARE(destination.readAll(), original);
    destination.close();

    SMprSaveCommitRequest atomicFailure;
    atomicFailure.source = QFileInfo(sourcePath);
    atomicFailure.destination = QFileInfo(destinationPath);
    atomicFailure.backupRootOverride = dir.filePath("backups");
    atomicFailure.simulateAtomicCommitFailure = true;
    const SMprSaveCommitResult atomicResult =
        commitStagedMprTransaction(baseline, transaction, atomicFailure);
    QVERIFY(!atomicResult.success);
    QCOMPARE(atomicResult.phase, QString("atomic-replacement"));
    QVERIFY(!atomicResult.backupPath.isEmpty());
    QVERIFY(destination.open(QIODevice::ReadOnly));
    QCOMPARE(destination.readAll(), original);

    const QString samePath = dir.filePath("same-path-failure.mpr");
    QFile samePathFile(samePath);
    QVERIFY(samePathFile.open(QIODevice::WriteOnly));
    QCOMPARE(samePathFile.write(original), qint64(original.size()));
    samePathFile.close();
    CMprDocument samePathBaseline;
    CValidationReport samePathReport;
    QVERIFY(samePathBaseline.load(QFileInfo(samePath), &samePathReport));
    const SMprArchiveMember samePathSector = samePathBaseline.members().at(2);
    QByteArray samePathReplacement = samePathSector.payload;
    samePathReplacement[samePathReplacement.size() - 1] =
        char(samePathReplacement.back() ^ 0x01);
    CMprEditTransaction samePathTransaction("same-path atomic failure");
    samePathTransaction.replaceMemberPayload(samePathSector.name,
        samePathSector.sha256, samePathReplacement);
    SMprSaveCommitRequest samePathFailure;
    samePathFailure.source = QFileInfo(samePath);
    samePathFailure.destination = QFileInfo(samePath);
    samePathFailure.backupRootOverride = dir.filePath("same-path-backups");
    samePathFailure.simulateAtomicCommitFailure = true;
    const SMprSaveCommitResult samePathResult = commitStagedMprTransaction(
        samePathBaseline, samePathTransaction, samePathFailure);
    QVERIFY(!samePathResult.success);
    QCOMPARE(samePathResult.phase, QString("atomic-replacement"));
    QVERIFY(!samePathResult.backupPath.isEmpty());
    QVERIFY(samePathFile.open(QIODevice::ReadOnly));
    QCOMPARE(samePathFile.readAll(), original);
}

void EnduranceDocumentTests::privateMprSaveCoordinatorCorpusUsesCopiesOnly()
{
    const QString fixtureList =
        qEnvironmentVariable("ENDURANCE_MPR_SAVE_FIXTURES");
    if(fixtureList.isEmpty())
        QSKIP("Set ENDURANCE_MPR_SAVE_FIXTURES to a path-list of copied private MPR fixtures.");
    const QStringList fixtures = fixtureList.split(QDir::listSeparator(),
                                                    QString::SkipEmptyParts);
    QVERIFY(!fixtures.isEmpty());

    for(const QString& fixturePath : fixtures)
    {
        const QFileInfo fixture(fixturePath);
        QVERIFY2(fixture.exists(), qPrintable(fixture.absoluteFilePath()));
        CMprDocument fixtureDocument;
        CValidationReport fixtureReport;
        QVERIFY2(fixtureDocument.load(fixture, &fixtureReport),
                 qPrintable(fixture.fileName() + ": " + fixtureReport.toText()));
        CTerrainEditModel terrain;
        CValidationReport terrainReport;
        QVERIFY2(terrain.load(fixtureDocument, &terrainReport),
                 qPrintable(fixture.fileName() + ": " + terrainReport.toText()));

        STerrainBrush brush;
        brush.centerX = 1.0f;
        brush.centerY = 1.0f;
        brush.radius = 0.0f;
        brush.falloff = ETerrainBrushFalloff::Constant;
        const float delta = terrain.height(1, 1) < 65535 ? 1.0f : -1.0f;
        const STerrainHeightCommand edit = terrain.makeRaiseLower(brush, delta);
        QVERIFY2(!edit.isEmpty(), qPrintable(fixture.fileName()));
        QVERIFY(terrain.apply(edit, true));
        CMprEditTransaction transaction("private copied-fixture vertex edit");
        CValidationReport transactionReport;
        QVERIFY2(terrain.appendSectorEdits(&transaction, &transactionReport),
                 qPrintable(fixture.fileName() + ": " + transactionReport.toText()));
        QVERIFY(!transaction.isEmpty());

        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString sourcePath = temp.filePath("source.mpr");
        const QString destinationPath = temp.filePath("candidate.mpr");
        QVERIFY(QFile::copy(fixture.absoluteFilePath(), sourcePath));
        QVERIFY(QFile::copy(fixture.absoluteFilePath(), destinationPath));
        CMprDocument baseline;
        CValidationReport baselineReport;
        QVERIFY2(baseline.load(QFileInfo(sourcePath), &baselineReport),
                 qPrintable(fixture.fileName() + ": " + baselineReport.toText()));

        SMprSaveCommitRequest request;
        request.source = QFileInfo(sourcePath);
        request.destination = QFileInfo(destinationPath);
        request.backupRootOverride = temp.filePath("backups");
        const SMprSaveCommitResult result =
            commitStagedMprTransaction(baseline, transaction, request);
        QVERIFY2(result.success,
                 qPrintable(fixture.fileName() + " " + result.phase + ": " + result.error));
        QCOMPARE(result.changedMembers.size(), transaction.edits().size());

        CMprDocument reopened;
        CValidationReport reopenReport;
        QVERIFY2(reopened.load(QFileInfo(destinationPath), &reopenReport),
                 qPrintable(fixture.fileName() + ": " + reopenReport.toText()));
        const CMprSemanticDiff diff = CMprSemanticDiff::compare(baseline, reopened);
        QCOMPARE(diff.entries().size(), transaction.edits().size());
        QVERIFY(QFileInfo(result.backupPath).exists());
    }
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

    // Fresh reopen alone is insufficient: a RES/MPR can parse while two SECs
    // disagree about their shared land edge. Staging must reject a newly split
    // seam before the candidate can replace or be saved beside the source.
    CMprDocument seamBaseline;
    CValidationReport seamBaselineReport;
    QVERIFY2(seamBaseline.loadBytes(syntheticTwoSectorMpr(
                sources.at(0).payload, sources.at(1).payload),
            "synthetic-two-sector.mpr", &seamBaselineReport),
        qPrintable(seamBaselineReport.toText()));
    QByteArray brokenRight = sources.at(1).payload;
    const int splitHeightOffset = 5 + (10 * 33) * 8 + 2;
    brokenRight[splitHeightOffset] = char(0);
    brokenRight[splitHeightOffset + 1] = char(0);
    const SMprSectorInfo rightInfo = seamBaseline.sectors().at(1);
    const SMprArchiveMember rightMember = seamBaseline.members().at(
        rightInfo.memberIndex);
    CMprEditTransaction splitSeam("reject split shared SEC seam");
    splitSeam.replaceMemberPayload(rightMember.name, rightMember.sha256,
                                   brokenRight);
    QTemporaryDir seamStage;
    QVERIFY(seamStage.isValid());
    const SStagedMprTransactionValidation rejected =
        validateStagedMprTransaction(seamBaseline, splitSeam,
            QFileInfo(seamStage.filePath("split-seam.mpr")));
    QVERIFY(!rejected.success);
    QVERIFY(rejected.error.contains("conflicting shared SEC land vertices"));
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

void EnduranceDocumentTests::globalTerrainHeightOperationsKeepDimensionsAndSeams()
{
    const QVector<STerrainSectorSource> sources = twoSectorTerrain(1000);
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(2, 1, 100.0f, sources));
    const int vertexCountX = terrain.vertexCountX();
    const int vertexCountY = terrain.vertexCountY();
    const int uniqueVertexCount = vertexCountX * vertexCountY;
    QCOMPARE(vertexCountX, 65);
    QCOMPARE(vertexCountY, 33);

    STerrainBrush point;
    point.radius = 0.0f;
    point.falloff = ETerrainBrushFalloff::Constant;
    point.centerX = 32.0f;
    point.centerY = 10.0f;
    QVERIFY(terrain.apply(terrain.makeFlatten(point, 2000, 1.0f), true));
    point.centerX = 64.0f;
    QVERIFY(terrain.apply(terrain.makeFlatten(point, 3000, 1.0f), true));

    const STerrainHeightRange before = terrain.heightRange();
    QCOMPARE(before.vertexCount, uniqueVertexCount);
    QCOMPARE(before.minimumRaw, quint16(1000));
    QCOMPARE(before.maximumRaw, quint16(3000));
    QCOMPARE(before.minimumWorld, 1000.0f * 100.0f / 65535.0f);
    QCOMPARE(before.maximumWorld, 3000.0f * 100.0f / 65535.0f);

    const STerrainHeightCommand normalize =
        terrain.makeGlobalNormalize(10000, 30000);
    QCOMPARE(normalize.deltas.size(), uniqueVertexCount);
    int seamDeltaCount = 0;
    for(const STerrainHeightDelta& delta : normalize.deltas)
        if(delta.globalX == 32 && delta.globalY == 10)
        {
            QCOMPARE(delta.after, quint16(20000));
            ++seamDeltaCount;
        }
    QCOMPARE(seamDeltaCount, 1);

    CTerrainEditHistory history;
    QVERIFY(history.execute(&terrain, normalize));
    QCOMPARE(terrain.vertexCountX(), vertexCountX);
    QCOMPARE(terrain.vertexCountY(), vertexCountY);
    QCOMPARE(terrain.height(32, 10), quint16(20000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(0, 0), 32, 10), quint16(20000));
    QCOMPARE(sectorHeight(terrain.sectorPayload(1, 0), 0, 10), quint16(20000));
    QCOMPARE(terrain.heightRange().minimumRaw, quint16(10000));
    QCOMPARE(terrain.heightRange().maximumRaw, quint16(30000));
    QVERIFY(terrain.makeGlobalNormalize(10000, 30000).isEmpty());

    const STerrainHeightCommand clamp = terrain.makeGlobalClamp(15000, 25000);
    QVERIFY(!clamp.isEmpty());
    QVERIFY(history.execute(&terrain, clamp));
    QCOMPARE(terrain.heightRange().minimumRaw, quint16(15000));
    QCOMPARE(terrain.heightRange().maximumRaw, quint16(25000));
    QVERIFY(terrain.makeGlobalClamp(15000, 25000).isEmpty());

    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.heightRange().minimumRaw, quint16(10000));
    QCOMPARE(terrain.heightRange().maximumRaw, quint16(30000));
    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.height(32, 10), quint16(2000));
    QCOMPARE(terrain.height(64, 10), quint16(3000));
    QVERIFY(history.redo(&terrain));
    QVERIFY(history.redo(&terrain));
    QCOMPARE(terrain.vertexCountX() * terrain.vertexCountY(), uniqueVertexCount);

    const STerrainHeightCommand flatten = terrain.makeGlobalFlatten(12345);
    QVERIFY(!flatten.isEmpty());
    QVERIFY(terrain.apply(flatten, true));
    QCOMPARE(terrain.heightRange().minimumRaw, quint16(12345));
    QCOMPARE(terrain.heightRange().maximumRaw, quint16(12345));
    QVERIFY(terrain.makeGlobalFlatten(12345).isEmpty());
}

void EnduranceDocumentTests::terrainVerticalScalePoliciesAreUndoableAndExplicit()
{
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(1, 1, 100.0f,
                                twoSectorTerrain(1000).mid(0, 1)));
    const float originalWorld = terrain.heightRange().minimumWorld;

    const STerrainVerticalScaleCommand scale = terrain.makeVerticalScale(
        200.0f, ETerrainVerticalScalePolicy::ScaleTerrain);
    QVERIFY(scale.valid);
    QVERIFY(scale.heights.isEmpty());
    QVERIFY(terrain.apply(scale, true));
    QCOMPARE(terrain.maxZ(), 200.0f);
    QCOMPARE(terrain.height(0, 0), quint16(1000));
    QCOMPARE(terrain.heightRange().minimumWorld, originalWorld * 2.0f);
    QVERIFY(terrain.isDirty());
    QVERIFY(terrain.apply(scale, false));
    QCOMPARE(terrain.maxZ(), 100.0f);
    QCOMPARE(terrain.height(0, 0), quint16(1000));
    QVERIFY(!terrain.isDirty());

    const STerrainVerticalScaleCommand preserve = terrain.makeVerticalScale(
        150.0f, ETerrainVerticalScalePolicy::PreserveWorldHeights);
    QVERIFY(preserve.valid);
    QCOMPARE(preserve.heights.deltas.size(), terrain.vertexCountX() * terrain.vertexCountY());
    QCOMPARE(preserve.heights.deltas.front().after, quint16(667));
    QVERIFY(preserve.roundedVertexCount > 0);
    QVERIFY(preserve.maximumWorldError > 0.0f);
    QVERIFY(terrain.apply(preserve, true));
    QCOMPARE(terrain.maxZ(), 150.0f);
    QCOMPARE(terrain.height(0, 0), quint16(667));
    QVERIFY(qAbs(terrain.heightRange().minimumWorld - originalWorld)
            <= preserve.maximumWorldError + 1.0e-6f);
    QVERIFY(terrain.apply(preserve, false));
    QCOMPARE(terrain.maxZ(), 100.0f);
    QCOMPARE(terrain.height(0, 0), quint16(1000));
    QVERIFY(!terrain.isDirty());

    CValidationReport refusedReport;
    const STerrainVerticalScaleCommand refused = terrain.makeVerticalScale(
        1.0f, ETerrainVerticalScalePolicy::PreserveWorldHeights,
        false, &refusedReport);
    QVERIFY(!refused.valid);
    QVERIFY(refused.clampedVertexCount > 0);
    QVERIFY(refusedReport.hasErrors());
    const STerrainVerticalScaleCommand accepted = terrain.makeVerticalScale(
        1.0f, ETerrainVerticalScalePolicy::PreserveWorldHeights, true);
    QVERIFY(accepted.valid);
    QVERIFY(accepted.clampedVertexCount > 0);
    QVERIFY(terrain.apply(accepted, true));
    QCOMPARE(terrain.heightRange().maximumRaw, quint16(65535));
    QVERIFY(terrain.apply(accepted, false));
    QVERIFY(!terrain.isDirty());

    CValidationReport tinyReport;
    const STerrainVerticalScaleCommand tiny = terrain.makeVerticalScale(
        std::numeric_limits<float>::min(),
        ETerrainVerticalScalePolicy::PreserveWorldHeights,
        false, &tinyReport);
    QVERIFY(!tiny.valid);
    QCOMPARE(tiny.clampedVertexCount,
             terrain.vertexCountX() * terrain.vertexCountY());
    QVERIFY(tinyReport.hasErrors());
}

void EnduranceDocumentTests::globalTerrainHeightOperationsRejectInvalidRanges()
{
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(1, 1, 100.0f, twoSectorTerrain(1000).mid(0, 1)));

    CValidationReport clampReport;
    QVERIFY(terrain.makeGlobalClamp(2000, 1000, &clampReport).isEmpty());
    QVERIFY(clampReport.hasErrors());

    CValidationReport normalizeRangeReport;
    QVERIFY(terrain.makeGlobalNormalize(2000, 1000,
                                        &normalizeRangeReport).isEmpty());
    QVERIFY(normalizeRangeReport.hasErrors());

    CValidationReport flatReport;
    QVERIFY(terrain.makeGlobalNormalize(1000, 5000, &flatReport).isEmpty());
    QVERIFY(flatReport.hasErrors());

    CValidationReport nonFiniteReport;
    QVERIFY(terrain.makeGlobalOffset(
        std::numeric_limits<float>::quiet_NaN(), &nonFiniteReport).isEmpty());
    QVERIFY(nonFiniteReport.hasErrors());
    QVERIFY(terrain.makeGlobalOffset(0.0f).isEmpty());

    const STerrainHeightCommand saturated = terrain.makeGlobalOffset(70000.0f);
    QCOMPARE(saturated.deltas.size(), terrain.vertexCountX() * terrain.vertexCountY());
    QVERIFY(saturated.requiresMaxZDecision());
    CValidationReport blockedReport;
    QVERIFY(!terrain.apply(saturated, true, &blockedReport));
    QVERIFY(blockedReport.hasErrors());
    QCOMPARE(terrain.heightRange().minimumRaw, quint16(1000));
    QVERIFY(terrain.apply(saturated, true, nullptr, true));
    QCOMPARE(terrain.heightRange().minimumRaw, quint16(65535));
    QCOMPARE(terrain.heightRange().maximumRaw, quint16(65535));
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

    // Adjacent stamps of the same material must merge into one shared-corner
    // field. The selected material on the second perimeter is continuation,
    // not a zero-radius background family.
    STerrainTileBrush firstAdjacent = semanticPoint;
    firstAdjacent.centerX = 6.0f;
    STerrainTileBrush secondAdjacent = semanticPoint;
    secondAdjacent.centerX = 8.0f;
    const STerrainTileCommand firstMerge = terrain.makeSemanticTilePaint(
        firstAdjacent, ETerrainTileLayer::Land, 2, semanticCatalog,
        0x53454D41u);
    QVERIFY(!firstMerge.isEmpty());
    QVERIFY(terrain.apply(firstMerge, true));
    const STerrainTileCommand secondMerge = terrain.makeSemanticTilePaint(
        secondAdjacent, ETerrainTileLayer::Land, 2, semanticCatalog,
        0x53454D41u);
    QVERIFY(secondMerge.deltas.size() > 1);
    QVERIFY(terrain.apply(secondMerge, true));
    for(int y = 0; y < terrain.tileCountY(); ++y)
    {
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool hereFound = false;
            const STerrainTransitionSignature here =
                semanticCatalog.gridSignatureForPacked(
                    terrain.packedTile(x, y, ETerrainTileLayer::Land),
                    &hereFound);
            QVERIFY(hereFound);
            if(x + 1 < terrain.tileCountX())
            {
                bool eastFound = false;
                const STerrainTransitionSignature east =
                    semanticCatalog.gridSignatureForPacked(
                        terrain.packedTile(x + 1, y, ETerrainTileLayer::Land),
                        &eastFound);
                QVERIFY(eastFound);
                QCOMPARE(here.northEast, east.northWest);
                QCOMPARE(here.southEast, east.southWest);
            }
            if(y + 1 < terrain.tileCountY())
            {
                bool nextRowFound = false;
                const STerrainTransitionSignature nextRow =
                    semanticCatalog.gridSignatureForPacked(
                        terrain.packedTile(x, y + 1, ETerrainTileLayer::Land),
                        &nextRowFound);
                QVERIFY(nextRowFound);
                QCOMPARE(here.southWest, nextRow.northWest);
                QCOMPARE(here.southEast, nextRow.northEast);
            }
        }
    }
    QVERIFY(terrain.apply(secondMerge, false));
    QVERIFY(terrain.apply(firstMerge, false));

    // Fixed atlases cannot express every boundary. The modern brush contract
    // is still deterministic: paint every core cell, mutate at most one ring,
    // and leave a hard seam when no safe authored transition exists.
    CTerrainTransitionCatalog hardEdgeCatalog;
    STerrainTransitionVariant hardCommon;
    hardCommon.signature.northWest = hardCommon.signature.northEast = 1;
    hardCommon.signature.southEast = hardCommon.signature.southWest = 1;
    hardCommon.packedTile = 0;
    QVERIFY(hardEdgeCatalog.addVariant(hardCommon));
    STerrainTransitionVariant hardSelected;
    hardSelected.signature.northWest = hardSelected.signature.northEast = 2;
    hardSelected.signature.southEast = hardSelected.signature.southWest = 2;
    hardSelected.packedTile = 2;
    QVERIFY(hardEdgeCatalog.addVariant(hardSelected));

    CTerrainEditModel hardEdgeTerrain;
    QVERIFY(hardEdgeTerrain.load(baseline));
    STerrainTileBrush boundedBrush;
    boundedBrush.centerX = 8.0f;
    boundedBrush.centerY = 8.0f;
    boundedBrush.radius = 1.0f;
    boundedBrush.shape = ETerrainTileBrushShape::Circle;
    CValidationReport hardEdgeReport;
    const STerrainTileCommand hardEdgePaint = hardEdgeTerrain.makeSemanticTilePaint(
        boundedBrush, ETerrainTileLayer::Land, 2, hardEdgeCatalog,
        0x424F554Eu, &hardEdgeReport);
    QCOMPARE(hardEdgePaint.semanticAudit.solverMode,
             QStringLiteral("bounded-hard-edge"));
    QCOMPARE(hardEdgePaint.semanticAudit.paintedCoreCells.size(), 5);
    QCOMPARE(hardEdgePaint.deltas.size(), 5);
    QVERIFY(!hardEdgeReport.hasErrors());
    QVERIFY(hardEdgeReport.toText().contains("terrain.semantic_hard_edge"));
    for(const STerrainTileDelta& delta : hardEdgePaint.deltas)
    {
        const int dx = qAbs(delta.globalX - 8);
        const int dy = qAbs(delta.globalY - 8);
        QVERIFY(dx + dy <= 1);
        bool signatureFound = false;
        const STerrainTransitionSignature after =
            hardEdgeCatalog.gridSignatureForPacked(delta.after, &signatureFound);
        QVERIFY(signatureFound);
        QCOMPARE(after.northWest, qint32(2));
        QCOMPARE(after.northEast, qint32(2));
        QCOMPARE(after.southEast, qint32(2));
        QCOMPARE(after.southWest, qint32(2));
    }
    QVERIFY(hardEdgeTerrain.apply(hardEdgePaint, true));
    CValidationReport repeatedHardEdgeReport;
    QVERIFY(hardEdgeTerrain.makeSemanticTilePaint(
        boundedBrush, ETerrainTileLayer::Land, 2, hardEdgeCatalog,
        0x424F554Eu, &repeatedHardEdgeReport).isEmpty());
    QVERIFY(!repeatedHardEdgeReport.hasErrors());
    QVERIFY(hardEdgeTerrain.apply(hardEdgePaint, false));

    // A valid material must repair an unknown packed value inside the brush,
    // while an unknown value in the transition ring remains untouched. This
    // is the recovery contract for legacy/hand-authored or damaged cells.
    CTerrainEditModel recoveryTerrain;
    QVERIFY(recoveryTerrain.load(baseline));
    STerrainTileBrush unknownPoint = boundedBrush;
    unknownPoint.radius = 0.0f;
    const quint16 unknownPacked = quint16(1 << 14); // valid slot/rotation, absent from catalog
    STerrainTileCommand unknownCoreSetup = recoveryTerrain.makeTilePaint(
        unknownPoint, ETerrainTileLayer::Land, 0, 0, 1);
    QCOMPARE(unknownCoreSetup.deltas.size(), 1);
    QVERIFY(recoveryTerrain.apply(unknownCoreSetup, true));
    unknownPoint.centerX = 9.0f;
    STerrainTileCommand unknownRingSetup = recoveryTerrain.makeTilePaint(
        unknownPoint, ETerrainTileLayer::Land, 0, 0, 1);
    QCOMPARE(unknownRingSetup.deltas.size(), 1);
    QVERIFY(recoveryTerrain.apply(unknownRingSetup, true));
    QCOMPARE(recoveryTerrain.packedTile(
        8, 8, ETerrainTileLayer::Land), unknownPacked);
    QCOMPARE(recoveryTerrain.packedTile(
        9, 8, ETerrainTileLayer::Land), unknownPacked);
    bool unknownIsAuthored = false;
    hardEdgeCatalog.gridSignatureForPacked(unknownPacked, &unknownIsAuthored);
    QVERIFY(!unknownIsAuthored);
    unknownPoint.centerX = 8.0f;
    CValidationReport recoveryReport;
    const STerrainTileCommand recoveryPaint = recoveryTerrain.makeSemanticTilePaint(
        unknownPoint, ETerrainTileLayer::Land, 2, hardEdgeCatalog,
        0x5245434Fu, &recoveryReport);
    QVERIFY2(!recoveryPaint.isEmpty(), qPrintable(recoveryReport.toText()));
    QVERIFY(!recoveryReport.hasErrors());
    QVERIFY(recoveryReport.toText().contains("terrain.semantic_unknown_recovery"));
    QVERIFY(recoveryTerrain.apply(recoveryPaint, true));
    bool recoveredFound = false;
    const STerrainTransitionSignature recovered =
        hardEdgeCatalog.gridSignatureForPacked(recoveryTerrain.packedTile(
            8, 8, ETerrainTileLayer::Land), &recoveredFound);
    QVERIFY(recoveredFound);
    QCOMPARE(recovered.northWest, qint32(2));
    QCOMPARE(recoveryTerrain.packedTile(
        9, 8, ETerrainTileLayer::Land), unknownPacked);

    // A one-corner approximation is accepted only when it remains anchored to
    // the painted core, contains no new family, and every existing tile at the
    // changed outer vertex already supports the approximated material.
    CTerrainTransitionCatalog nearestCatalog;
    QVERIFY(nearestCatalog.addVariant(hardCommon));
    QVERIFY(nearestCatalog.addVariant(hardSelected));
    STerrainTransitionSignature threeSelected;
    threeSelected.northWest = 1;
    threeSelected.northEast = 2;
    threeSelected.southEast = 2;
    threeSelected.southWest = 2;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        STerrainTransitionVariant transition;
        transition.signature = CTerrainTransitionCatalog::rotated(
            threeSelected, rotation);
        transition.packedTile = quint16(1 | (rotation << 14));
        QVERIFY(nearestCatalog.addVariant(transition));
    }
    CTerrainEditModel nearestTerrain;
    QVERIFY(nearestTerrain.load(baseline));
    STerrainTileBrush setupNearest;
    setupNearest.radius = 0.0f;
    setupNearest.shape = ETerrainTileBrushShape::Circle;
    setupNearest.centerX = 8.0f;
    setupNearest.centerY = 6.0f;
    QVERIFY(nearestTerrain.apply(nearestTerrain.makeTilePaint(
        setupNearest, ETerrainTileLayer::Land, 0, 1, 0), true));
    setupNearest.centerX = 9.0f;
    QVERIFY(nearestTerrain.apply(nearestTerrain.makeTilePaint(
        setupNearest, ETerrainTileLayer::Land, 0, 1, 0), true));
    setupNearest.centerY = 7.0f;
    QVERIFY(nearestTerrain.apply(nearestTerrain.makeTilePaint(
        setupNearest, ETerrainTileLayer::Land, 0, 1, 1), true));
    boundedBrush.radius = 0.0f;
    CValidationReport nearestReport;
    const STerrainTileCommand nearestPaint = nearestTerrain.makeSemanticTilePaint(
        boundedBrush, ETerrainTileLayer::Land, 2, nearestCatalog,
        0x424F554Eu, &nearestReport);
    QVERIFY(!nearestPaint.isEmpty());
    QVERIFY(!nearestReport.hasErrors());
    QVERIFY(nearestReport.toText().contains("terrain.semantic_nearest"));
    QVERIFY(nearestReport.toText().contains("terrain.semantic_hard_edge"));
    QSet<int> nearestChanged;
    for(const STerrainTileDelta& delta : nearestPaint.deltas)
    {
        QVERIFY(qAbs(delta.globalX - 8) <= 1);
        QVERIFY(qAbs(delta.globalY - 8) <= 1);
        const int relativeX = delta.globalX - 8;
        const int relativeY = delta.globalY - 8;
        nearestChanged.insert((relativeY + 1) * 3 + relativeX + 1);
        bool signatureFound = false;
        const STerrainTransitionSignature after =
            nearestCatalog.gridSignatureForPacked(delta.after, &signatureFound);
        QVERIFY(signatureFound);
        const qint32 families[4] = {after.northWest, after.northEast,
                                    after.southEast, after.southWest};
        for(qint32 family : families)
            QVERIFY(family == 1 || family == 2);
    }
    QCOMPARE(nearestChanged, QSet<int>({4, 1}));
    QVERIFY(nearestTerrain.apply(nearestPaint, true));
    bool approximatedFound = false;
    bool northWestSupportFound = false;
    bool northEastSupportFound = false;
    bool eastSupportFound = false;
    const STerrainTransitionSignature approximated =
        nearestCatalog.gridSignatureForPacked(nearestTerrain.packedTile(
            8, 7, ETerrainTileLayer::Land), &approximatedFound);
    const STerrainTransitionSignature northWestSupport =
        nearestCatalog.gridSignatureForPacked(nearestTerrain.packedTile(
            8, 6, ETerrainTileLayer::Land), &northWestSupportFound);
    const STerrainTransitionSignature northEastSupport =
        nearestCatalog.gridSignatureForPacked(nearestTerrain.packedTile(
            9, 6, ETerrainTileLayer::Land), &northEastSupportFound);
    const STerrainTransitionSignature eastSupport =
        nearestCatalog.gridSignatureForPacked(nearestTerrain.packedTile(
            9, 7, ETerrainTileLayer::Land), &eastSupportFound);
    QVERIFY(approximatedFound);
    QVERIFY(northWestSupportFound);
    QVERIFY(northEastSupportFound);
    QVERIFY(eastSupportFound);
    QCOMPARE(approximated.northEast, northWestSupport.southEast);
    QCOMPARE(approximated.northEast, northEastSupport.southWest);
    QCOMPARE(approximated.northEast, eastSupport.northWest);
    QVERIFY(nearestTerrain.makeSemanticTilePaint(
        boundedBrush, ETerrainTileLayer::Land, 2, nearestCatalog,
        0x424F554Eu).isEmpty());

    // The same approximation against a uniform neighborhood would contradict
    // the three other tiles at its outer vertex. It must become a hard edge,
    // not the two seam disagreements captured in the Dark/Lime diagnostic.
    CTerrainEditModel unsupportedNearestTerrain;
    QVERIFY(unsupportedNearestTerrain.load(baseline));
    CValidationReport unsupportedNearestReport;
    const STerrainTileCommand unsupportedNearest =
        unsupportedNearestTerrain.makeSemanticTilePaint(
            boundedBrush, ETerrainTileLayer::Land, 2, nearestCatalog,
            0x424F554Eu, &unsupportedNearestReport);
    QCOMPARE(unsupportedNearest.deltas.size(), 1);
    QCOMPARE(unsupportedNearest.deltas.front().globalX, 8);
    QCOMPARE(unsupportedNearest.deltas.front().globalY, 8);
    QVERIFY(!unsupportedNearestReport.toText().contains("terrain.semantic_nearest"));
    QVERIFY(unsupportedNearestReport.toText().contains("terrain.semantic_hard_edge"));

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
    // Active overlay records may currently reference a land-family atlas slot.
    // A combined authored catalog must be able to decode that pre-state and
    // replace it with an overlay family without requiring land-family paint
    // selection to be enabled in the UI.
    const STerrainTileCommand semanticOverlay = terrain.makeSemanticTilePaint(
        point, ETerrainTileLayer::Overlay, 2, semanticCatalog,
        0x4F564552u);
    QVERIFY(!semanticOverlay.isEmpty());
    QVERIFY(std::any_of(semanticOverlay.deltas.constBegin(),
                        semanticOverlay.deltas.constEnd(),
                        [](const STerrainTileDelta& delta) {
        return delta.globalX == 1 && delta.globalY == 0;
    }));

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
    analysis.overlayCatalogWithLandFamilies().signatureForPacked(
        packed.encode(), &found);
    QVERIFY(found);
    packed.tileIndex = 50;
    analysis.overlayCatalogWithLandFamilies().signatureForPacked(
        packed.encode(), &found);
    QVERIFY(found);
    QCOMPARE(analysis.overlayCatalogWithLandFamilies().size(),
             analysis.landCatalog.size() + analysis.overlayCatalog.size());
    QCOMPARE(analysis.overlayCatalogWithLandFamilies().materialIds().size(),
             analysis.landCatalog.materialIds().size()
                 + analysis.overlayCatalog.materialIds().size());
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
    for(const STerrainTransitionSignature& authored :
        analysis.landCatalog.gridSignatures())
    {
        QSet<qint32> families;
        families.insert(authored.northWest);
        families.insert(authored.northEast);
        families.insert(authored.southEast);
        families.insert(authored.southWest);
        QVERIFY2(families.size() <= 3,
                 "Zone1 unexpectedly contains a four-family land tile");
    }
    bool hasThreeDarkOneLime = false;
    bool hasOneDarkThreeLime = false;
    for(const STerrainTransitionSignature& authored :
        analysis.landCatalog.gridSignatures())
    {
        const qint32 corners[] = {authored.northWest, authored.northEast,
                                  authored.southEast, authored.southWest};
        int dark = 0;
        int lime = 0;
        for(qint32 corner : corners)
        {
            dark += corner == 3 ? 1 : 0;
            lime += corner == 7 ? 1 : 0;
        }
        hasThreeDarkOneLime |= dark == 3 && lime == 1;
        hasOneDarkThreeLime |= dark == 1 && lime == 3;
    }
    QVERIFY(!hasThreeDarkOneLime);
    QVERIFY(hasOneDarkThreeLime);
    QMap<qint32, int> existingFamilyPreference;
    existingFamilyPreference.insert(4, 20);
    QCOMPARE(analysis.landCatalog.shortestMaterialPath(7, 3, existingFamilyPreference),
             QVector<qint32>({7, 4, 3}));

    // The stock namespace authors real three-family junction tiles. Keep
    // these public metadata checks separate from the private map replay so a
    // regression cannot silently reduce semantic painting to pairwise bands.
    QVector<STerrainTransitionSignature> threeFamilyJunctions;
    STerrainTransitionSignature groundDarkCommon;
    groundDarkCommon.northWest = 3;
    groundDarkCommon.northEast = 2;
    groundDarkCommon.southEast = 4;
    groundDarkCommon.southWest = 4;
    threeFamilyJunctions.append(groundDarkCommon);
    STerrainTransitionSignature stoneDarkCommon;
    stoneDarkCommon.northWest = 7;
    stoneDarkCommon.northEast = 3;
    stoneDarkCommon.southEast = 4;
    stoneDarkCommon.southWest = 4;
    threeFamilyJunctions.append(stoneDarkCommon);
    for(const STerrainTransitionSignature& junction : threeFamilyJunctions)
    {
        bool junctionFound = false;
        const quint16 packedJunction = analysis.landCatalog.selectForPaint(
            junction, 100, 100, 12345u, &junctionFound);
        QVERIFY(junctionFound);
        bool decodedJunction = false;
        QCOMPARE(analysis.landCatalog.gridSignatureForPacked(
                     packedJunction, &decodedJunction), junction);
        QVERIFY(decodedJunction);
    }

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

void EnduranceDocumentTests::semanticAtlasProposalIsDeterministicAndApprovalGated()
{
    const auto tile = [](const QColor& nw, const QColor& ne,
                         const QColor& se, const QColor& sw) {
        QImage image(18, 18, QImage::Format_RGBA8888);
        for(int y = 0; y < image.height(); ++y)
            for(int x = 0; x < image.width(); ++x)
            {
                const bool east = x >= image.width() / 2;
                const bool south = y >= image.height() / 2;
                image.setPixelColor(x, y, south ? (east ? se : sw)
                                                 : (east ? ne : nw));
            }
        return image;
    };
    const QColor soil(110, 70, 40), grass(35, 125, 45);
    const QColor water(25, 65, 150), submerged(55, 50, 40);
    QVector<QImage> images;
    images << tile(soil, soil, soil, soil)
           << tile(soil, grass, grass, soil)
           << tile(grass, grass, grass, grass)
           << tile(water, water, water, water)
           << tile(submerged, submerged, submerged, submerged);
    const QVector<qint32> tileTypes({1, 1, 1, 6, 4});
    QVector<STerrainAtlasSlotUsage> usage(images.size());
    usage[0].landCount = 40;
    usage[1].landCount = 12;
    usage[2].landCount = 25;
    usage[3].overlayCount = 30;
    usage[4].landCount = 30;
    usage[4].landBelowOverlayCount = 30;

    CValidationReport firstReport;
    const STerrainAtlasProfile first = CTerrainAtlasProfileInference::propose(
        "synthetic_forest", images, tileTypes, usage, &firstReport);
    const STerrainAtlasProfile second = CTerrainAtlasProfileInference::propose(
        "synthetic_forest", images, tileTypes, usage);
    QCOMPARE(first.toJson(), second.toJson());
    QCOMPARE(first.atlasSlotCount, images.size());
    QCOMPARE(first.atlasPixelSha256.size(), 64);
    QVERIFY(!first.approved);
    QCOMPARE(first.slotDefinitions.size(), images.size());

    const STerrainAtlasProfileSlot transition = first.slotDefinitions.at(1);
    QCOMPARE(transition.signature.northWest, transition.signature.southWest);
    QCOMPARE(transition.signature.northEast, transition.signature.southEast);
    QVERIFY(transition.signature.northWest != transition.signature.northEast);
    int overlayFamilies = 0, underwaterFamilies = 0;
    for(const STerrainAtlasProfileFamily& family : first.families)
    {
        overlayFamilies += family.layer == ETerrainAtlasLayer::Overlay;
        underwaterFamilies += family.underwater;
        QVERIFY(!family.ambiguous);
    }
    QCOMPARE(overlayFamilies, 1);
    QCOMPARE(underwaterFamilies, 1);

    CValidationReport gateReport;
    QVERIFY(!first.approvedAnalysis(&gateReport).usable);
    QVERIFY(gateReport.hasErrors());

    STerrainAtlasProfile approved = first;
    approved.approved = true;
    CValidationReport analysisReport;
    const STerrainAtlasAnalysis analysis = approved.approvedAnalysis(&analysisReport);
    QVERIFY2(analysis.usable, qPrintable(analysisReport.toText()));
    bool found = false;
    const quint16 packed = analysis.landCatalog.selectForPaint(
        transition.signature, 3, 7, 99, &found);
    QVERIFY(found);
    bool decoded = false;
    const STerrainTransitionSignature decodedSignature =
        analysis.landCatalog.gridSignatureForPacked(packed, &decoded);
    QVERIFY(decoded);
    QCOMPARE(decodedSignature, transition.signature);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("semantic-atlas-profile.json");
    CValidationReport saveReport;
    QVERIFY2(approved.save(path, &saveReport), qPrintable(saveReport.toText()));
    STerrainAtlasProfile reopened;
    CValidationReport loadReport;
    QVERIFY2(STerrainAtlasProfile::load(path, &reopened, &loadReport),
             qPrintable(loadReport.toText()));
    QCOMPARE(reopened.toJson(), approved.toJson());
    const QByteArray json = QJsonDocument(reopened.toJson()).toJson();
    QVERIFY(!json.contains(".mpr"));
    QVERIFY(!json.contains(".mmp"));
    QVERIFY(!json.contains("Maps\\"));
}

void EnduranceDocumentTests::semanticAtlasProfileRejectsBindingMismatchAndAmbiguity()
{
    QImage image(12, 12, QImage::Format_RGBA8888);
    image.fill(QColor(80, 100, 60));
    QVector<STerrainAtlasSlotUsage> usage(1);
    usage[0].landCount = 5;
    usage[0].overlayCount = 5;
    CValidationReport proposalReport;
    STerrainAtlasProfile profile = CTerrainAtlasProfileInference::propose(
        "synthetic_mixed", QVector<QImage>({image}), QVector<qint32>({1}),
        usage, &proposalReport);
    QVERIFY(!profile.families.isEmpty());
    QVERIFY(profile.families.front().ambiguous);
    QVERIFY(proposalReport.toText().contains("both land and overlay"));

    profile.approved = true;
    CValidationReport ambiguousReport;
    QVERIFY(!profile.approvedAnalysis(&ambiguousReport).usable);
    QVERIFY(ambiguousReport.hasErrors());

    CValidationReport namespaceReport;
    QVERIFY(!profile.validateBinding("another_namespace", profile.atlasPixelSha256,
                                     1, &namespaceReport));
    QVERIFY(namespaceReport.toText().contains("another MPR namespace"));
    CValidationReport hashReport;
    QVERIFY(!profile.validateBinding(profile.mprNamespace, QByteArray(64, '0'),
                                     1, &hashReport));
    QVERIFY(hashReport.toText().contains("pixels no longer match"));
    CValidationReport countReport;
    QVERIFY(!profile.validateBinding(profile.mprNamespace, profile.atlasPixelSha256,
                                     2, &countReport));
    QVERIFY(countReport.toText().contains("slot count"));
}

void EnduranceDocumentTests::semanticAtlasReviewEditsAndStoreAreDeterministic()
{
    STerrainAtlasProfile profile;
    profile.mprNamespace = "synthetic/review namespace";
    profile.atlasPixelSha256 = QByteArray(64, 'a');
    profile.atlasSlotCount = 3;
    profile.provenance = "synthetic-test";
    STerrainAtlasProfileFamily soil;
    soil.id = 1; soil.name = "Soil";
    STerrainAtlasProfileFamily grass;
    grass.id = 2; grass.name = "Grass";
    profile.families << soil << grass;
    const auto slot = [](int index, qint32 nw, qint32 ne, qint32 se, qint32 sw) {
        STerrainAtlasProfileSlot value;
        value.slot = index;
        value.signature.northWest = nw;
        value.signature.northEast = ne;
        value.signature.southEast = se;
        value.signature.southWest = sw;
        return value;
    };
    profile.slotDefinitions << slot(0, 1, 1, 1, 1)
                            << slot(1, 1, 2, 2, 1)
                            << slot(2, 2, 2, 2, 2);
    CValidationReport normalizeReport;
    QVERIFY2(profile.normalizeMembership(&normalizeReport),
             qPrintable(normalizeReport.toText()));
    QCOMPARE(profile.families.at(0).memberSlots, QVector<int>({0, 1}));
    QCOMPARE(profile.families.at(1).memberSlots, QVector<int>({1, 2}));

    CValidationReport splitReport;
    QVERIFY2(profile.splitFamily(2, QVector<int>({2}), "Meadow", &splitReport),
             qPrintable(splitReport.toText()));
    QCOMPARE(profile.families.size(), 3);
    QCOMPARE(profile.slotDefinitions.at(2).signature.northWest, qint32(3));
    QVERIFY(profile.families.at(1).ambiguous);
    QVERIFY(profile.families.at(2).ambiguous);
    QVERIFY(!profile.approved);

    CValidationReport badMergeReport;
    profile.families[2].layer = ETerrainAtlasLayer::Overlay;
    QVERIFY(!profile.mergeFamilies(QVector<qint32>({1, 3}), "Invalid",
                                   &badMergeReport));
    QVERIFY(badMergeReport.toText().contains("Land and overlay"));
    profile.families[2].layer = ETerrainAtlasLayer::Land;
    CValidationReport mergeReport;
    QVERIFY2(profile.mergeFamilies(QVector<qint32>({1, 3}), "Soil and meadow",
                                   &mergeReport), qPrintable(mergeReport.toText()));
    QCOMPARE(profile.families.size(), 2);
    QCOMPARE(profile.slotDefinitions.at(2).signature.northWest, qint32(1));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = CTerrainAtlasProfileStore::profilePath(
        profile.mprNamespace, directory.path());
    QVERIFY(path.contains("synthetic_review_namespace-"));
    QVERIFY(path.endsWith(".semantic-atlas.json"));
    QVERIFY(CTerrainAtlasProfileStore::profilePath(
        "synthetic review namespace", directory.path()) != path);
    CValidationReport saveReport;
    QVERIFY2(CTerrainAtlasProfileStore::save(profile, directory.path(), &saveReport),
             qPrintable(saveReport.toText()));
    STerrainAtlasProfile loaded;
    CValidationReport loadReport;
    QVERIFY2(CTerrainAtlasProfileStore::load(profile.mprNamespace, &loaded,
        directory.path(), &loadReport), qPrintable(loadReport.toText()));
    QCOMPARE(loaded.toJson(), profile.toJson());

    CValidationReport staleReport;
    QVERIFY(!loaded.validateBinding(profile.mprNamespace, QByteArray(64, 'b'),
                                    profile.atlasSlotCount, &staleReport));
    QVERIFY(staleReport.toText().contains("pixels no longer match"));
}

void EnduranceDocumentTests::terrainTileStrokeCompositionKeepsFirstAndFinalValues()
{
    const auto delta = [](int x, int y, ETerrainTileLayer layer,
                          quint16 before, quint16 after) {
        STerrainTileDelta value;
        value.globalX = x;
        value.globalY = y;
        value.layer = layer;
        value.before = before;
        value.after = after;
        return value;
    };
    STerrainTileCommand stroke;
    STerrainTileCommand first;
    first.deltas << delta(4, 5, ETerrainTileLayer::Land, 10, 11)
                 << delta(8, 9, ETerrainTileLayer::Land, 20, 21);
    STerrainTileCommand second;
    second.deltas << delta(4, 5, ETerrainTileLayer::Land, 11, 12)
                  << delta(8, 9, ETerrainTileLayer::Land, 21, 20)
                  << delta(4, 5, ETerrainTileLayer::Overlay, 30, 31);

    appendTerrainTileCommand(&stroke, first);
    appendTerrainTileCommand(&stroke, second);

    QCOMPARE(stroke.deltas.size(), 2);
    QCOMPARE(stroke.deltas.at(0).globalX, 4);
    QCOMPARE(stroke.deltas.at(0).globalY, 5);
    QCOMPARE(stroke.deltas.at(0).layer, ETerrainTileLayer::Land);
    QCOMPARE(stroke.deltas.at(0).before, quint16(10));
    QCOMPARE(stroke.deltas.at(0).after, quint16(12));
    QCOMPARE(stroke.deltas.at(1).layer, ETerrainTileLayer::Overlay);
    QCOMPARE(stroke.deltas.at(1).before, quint16(30));
    QCOMPARE(stroke.deltas.at(1).after, quint16(31));
}

void EnduranceDocumentTests::terrainHeightStrokeCompositionKeepsFirstAndFinalValues()
{
    const auto delta = [](int x, int y, quint16 before, quint16 after,
                          bool clamped = false) {
        STerrainHeightDelta value;
        value.globalX = x;
        value.globalY = y;
        value.before = before;
        value.after = after;
        value.clamped = clamped;
        return value;
    };
    STerrainHeightCommand stroke;
    STerrainHeightCommand first;
    first.deltas << delta(4, 5, 100, 110)
                 << delta(8, 9, 200, 210);
    STerrainHeightCommand second;
    second.deltas << delta(4, 5, 110, 120, true)
                  << delta(8, 9, 210, 200)
                  << delta(12, 13, 300, 310);

    appendTerrainHeightCommand(&stroke, first);
    appendTerrainHeightCommand(&stroke, second);

    QCOMPARE(stroke.deltas.size(), 2);
    QCOMPARE(stroke.deltas.at(0).globalX, 4);
    QCOMPARE(stroke.deltas.at(0).globalY, 5);
    QCOMPARE(stroke.deltas.at(0).before, quint16(100));
    QCOMPARE(stroke.deltas.at(0).after, quint16(120));
    QVERIFY(stroke.deltas.at(0).clamped);
    QCOMPARE(stroke.deltas.at(1).globalX, 12);
    QCOMPARE(stroke.deltas.at(1).globalY, 13);
    QCOMPARE(stroke.deltas.at(1).before, quint16(300));
    QCOMPARE(stroke.deltas.at(1).after, quint16(310));
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
                    // Pairwise routes require at most two cells; an authored
                    // multi-family junction may use one additional constrained
                    // ring while keeping the painted core at size one.
                    QVERIFY(qAbs(delta.globalX - probe.x()) <= 3);
                    QVERIFY(qAbs(delta.globalY - probe.y()) <= 3);
                }
            }
        }
    }
    // At most the family already covering all probes may legitimately be a
    // no-op. This catches the R9 failure mode where only one family painted.
    QVERIFY(changedMaterials.size() >= 4);

    // R22 diagnostic 1: a direct Lime/Irregular click must not expand merely
    // because Common grass is visible beyond the existing radius-1 island.
    CTerrainEditModel compactIsland;
    QVERIFY(compactIsland.load(document));
    STerrainTileBrush compactBrush;
    compactBrush.centerX = 101.0f;
    compactBrush.centerY = 32.0f;
    compactBrush.radius = 1.0f;
    compactBrush.shape = ETerrainTileBrushShape::Circle;
    CValidationReport compactIrregularReport;
    const STerrainTileCommand compactIrregular = compactIsland.makeSemanticTilePaint(
        compactBrush, ETerrainTileLayer::Land, 9,
        analysis.landCatalog, 12345u, &compactIrregularReport);
    QVERIFY2(!compactIrregularReport.hasErrors(),
             qPrintable(compactIrregularReport.toText()));
    QVERIFY(!compactIrregular.isEmpty());
    QVERIFY(compactIsland.apply(compactIrregular, true));
    compactBrush.radius = 0.0f;
    CValidationReport compactLimeReport;
    const STerrainTileCommand compactLime = compactIsland.makeSemanticTilePaint(
        compactBrush, ETerrainTileLayer::Land, 7,
        analysis.landCatalog, 12345u, &compactLimeReport);
    QVERIFY2(!compactLimeReport.hasErrors(), qPrintable(compactLimeReport.toText()));
    QVERIFY(!compactLime.isEmpty());
    QCOMPARE(compactLime.semanticAudit.junctionPadding, 1);
    int commonToIrregular = 0;
    QSet<int> countedCompactCorners;
    for(const STerrainTileDelta& delta : compactLime.deltas)
    {
        bool beforeFound = false;
        bool afterFound = false;
        const STerrainTransitionSignature before =
            analysis.landCatalog.gridSignatureForPacked(delta.before, &beforeFound);
        const STerrainTransitionSignature after =
            analysis.landCatalog.gridSignatureForPacked(delta.after, &afterFound);
        QVERIFY(beforeFound);
        QVERIFY(afterFound);
        const qint32 beforeValues[4] = {before.northWest, before.northEast,
                                        before.southEast, before.southWest};
        const qint32 afterValues[4] = {after.northWest, after.northEast,
                                       after.southEast, after.southWest};
        const int keys[4] = {
            delta.globalY * (compactIsland.tileCountX() + 1) + delta.globalX,
            delta.globalY * (compactIsland.tileCountX() + 1) + delta.globalX + 1,
            (delta.globalY + 1) * (compactIsland.tileCountX() + 1) + delta.globalX + 1,
            (delta.globalY + 1) * (compactIsland.tileCountX() + 1) + delta.globalX
        };
        for(int corner = 0; corner < 4; ++corner)
        {
            if(countedCompactCorners.contains(keys[corner]))
                continue;
            countedCompactCorners.insert(keys[corner]);
            commonToIrregular += beforeValues[corner] == 4
                && afterValues[corner] == 9;
        }
    }
    QCOMPARE(commonToIrregular, 0);
    QVERIFY(compactIsland.apply(compactLime, true));
    CValidationReport repeatedLimeReport;
    const STerrainTileCommand repeatedLime = compactIsland.makeSemanticTilePaint(
        compactBrush, ETerrainTileLayer::Land, 7,
        analysis.landCatalog, 12345u, &repeatedLimeReport);
    QVERIFY2(!repeatedLimeReport.hasErrors(), qPrintable(repeatedLimeReport.toText()));
    QVERIFY(repeatedLime.isEmpty());

    // R22 diagnostic 2 minimized to the 5x5 state around the refused click.
    // The atlas already contains direct Dark/Lime signatures in this exact
    // neighborhood, so route discovery must not force Ground as an
    // intermediary and break the continuous Dark patch.
    CTerrainEditModel directObserved;
    QVERIFY(directObserved.load(document));
    const QVector<quint16> directObservedState({
        32861,32877,32877,32883,32887,
        49261,32817,16742,410,411,
        49262,49201,16794,16386,2,
        49268,49,33126,33178,33179,
        49269,16433,49,16742,410
    });
    STerrainTileCommand directObservedSetup;
    for(int localY = 0; localY < 5; ++localY)
    {
        for(int localX = 0; localX < 5; ++localX)
        {
            const int x = 95 + localX;
            const int y = 31 + localY;
            const quint16 after = directObservedState.at(localY * 5 + localX);
            const quint16 before = directObserved.packedTile(
                x, y, ETerrainTileLayer::Land);
            if(before == after)
                continue;
            STerrainTileDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.layer = ETerrainTileLayer::Land;
            delta.before = before;
            delta.after = after;
            directObservedSetup.deltas.append(delta);
        }
    }
    QVERIFY(directObserved.apply(directObservedSetup, true));
    STerrainTileBrush directObservedBrush;
    directObservedBrush.centerX = 97.0f;
    directObservedBrush.centerY = 33.0f;
    directObservedBrush.radius = 0.0f;
    directObservedBrush.shape = ETerrainTileBrushShape::Circle;
    CValidationReport directObservedReport;
    const STerrainTileCommand directDark = directObserved.makeSemanticTilePaint(
        directObservedBrush, ETerrainTileLayer::Land, 3,
        analysis.landCatalog, 12345u, &directObservedReport);
    QVERIFY2(!directObservedReport.hasErrors(), qPrintable(directObservedReport.toText()));
    QVERIFY(!directDark.isEmpty());
    QCOMPARE(directDark.semanticAudit.materialPath, QVector<qint32>({3, 7}));
    QCOMPARE(directDark.semanticAudit.junctionPadding, 0);

    // Replay the Ground/common path captured by the user's R16 diagnostic on
    // this exact copied Zone1 fixture. Consecutive samples belong to nearby
    // strokes and must merge into one corner-consistent region instead of
    // treating already-painted Ground/common as the new background family.
    CTerrainEditModel mergedTerrain;
    CValidationReport mergedLoadReport;
    QVERIFY2(mergedTerrain.load(document, &mergedLoadReport),
             qPrintable(mergedLoadReport.toText()));
    const QVector<QPoint> capturedCenters({
        QPoint(103, 40), QPoint(107, 41), QPoint(106, 40), QPoint(105, 40),
        QPoint(104, 40), QPoint(104, 39), QPoint(103, 41), QPoint(104, 41),
        QPoint(105, 41), QPoint(105, 40), QPoint(106, 40), QPoint(106, 39),
        QPoint(107, 39), QPoint(108, 38), QPoint(107, 40), QPoint(105, 40),
        QPoint(113, 46), QPoint(113, 42)
    });
    QSet<int> changedCells;
    for(int sample = 0; sample < capturedCenters.size(); ++sample)
    {
        const QPoint center = capturedCenters.at(sample);
        brush.centerX = float(center.x());
        brush.centerY = float(center.y());
        brush.radius = sample >= 16 ? 1.0f : 0.0f;
        brush.shape = ETerrainTileBrushShape::Circle;
        CValidationReport mergeReport;
        const STerrainTileCommand paint = mergedTerrain.makeSemanticTilePaint(
            brush, ETerrainTileLayer::Land, 2, analysis.landCatalog,
            12345u, &mergeReport);
        QVERIFY2(!mergeReport.hasErrors(), qPrintable(QString("Captured sample %1 at %2,%3:\n%4")
            .arg(sample).arg(center.x()).arg(center.y()).arg(mergeReport.toText())));
        for(const STerrainTileDelta& delta : paint.deltas)
            changedCells.insert(delta.globalY * mergedTerrain.tileCountX() + delta.globalX);
        QVERIFY2(mergedTerrain.apply(paint, true, &mergeReport),
                 qPrintable(mergeReport.toText()));
    }
    QVERIFY(changedCells.size() > 20);

    const auto gridSignature = [&](int x, int y, bool* found) {
        return analysis.landCatalog.gridSignatureForPacked(mergedTerrain.packedTile(
            x, y, ETerrainTileLayer::Land), found);
    };
    for(int index : changedCells)
    {
        const int x = index % mergedTerrain.tileCountX();
        const int y = index / mergedTerrain.tileCountX();
        bool found = false;
        const STerrainTransitionSignature here = gridSignature(x, y, &found);
        QVERIFY(found);
        if(x + 1 < mergedTerrain.tileCountX())
        {
            const STerrainTransitionSignature east = gridSignature(x + 1, y, &found);
            QVERIFY(found);
            QCOMPARE(here.northEast, east.northWest);
            QCOMPARE(here.southEast, east.southWest);
        }
        if(y + 1 < mergedTerrain.tileCountY())
        {
            const STerrainTransitionSignature south = gridSignature(x, y + 1, &found);
            QVERIFY(found);
            QCOMPARE(here.southWest, south.northWest);
            QCOMPARE(here.southEast, south.northEast);
        }
    }

    // R17 version-3 evidence: lay down the captured Ground/common region,
    // then introduce Grass/dark at its boundary with untouched Grass/common.
    // The atlas contains authored 2+3+4 junctions; a two-family-only solve
    // must not erase family 4 or leave a shared-edge disagreement.
    CTerrainEditModel threeWayTerrain;
    CValidationReport threeWayLoadReport;
    QVERIFY2(threeWayTerrain.load(document, &threeWayLoadReport),
             qPrintable(threeWayLoadReport.toText()));
    const QVector<QPoint> groundCenters({
        QPoint(102, 40), QPoint(103, 40), QPoint(105, 40), QPoint(108, 40),
        QPoint(106, 40), QPoint(103, 39), QPoint(103, 38), QPoint(102, 38),
        QPoint(101, 38), QPoint(101, 37), QPoint(100, 38), QPoint(99, 38),
        QPoint(99, 39), QPoint(100, 40), QPoint(102, 41), QPoint(102, 42),
        QPoint(101, 42), QPoint(100, 42), QPoint(98, 39), QPoint(98, 40)
    });
    QSet<int> threeWayChanged;
    const auto paintSample = [&](const QPoint& center, qint32 material) {
        STerrainTileBrush sampleBrush;
        sampleBrush.centerX = float(center.x());
        sampleBrush.centerY = float(center.y());
        sampleBrush.radius = 0.0f;
        sampleBrush.shape = ETerrainTileBrushShape::Circle;
        CValidationReport sampleReport;
        const STerrainTileCommand command = threeWayTerrain.makeSemanticTilePaint(
            sampleBrush, ETerrainTileLayer::Land, material,
            analysis.landCatalog, 12345u, &sampleReport);
        QVERIFY2(!sampleReport.hasErrors(), qPrintable(sampleReport.toText()));
        for(const STerrainTileDelta& delta : command.deltas)
            threeWayChanged.insert(delta.globalY * threeWayTerrain.tileCountX()
                                   + delta.globalX);
        QVERIFY2(threeWayTerrain.apply(command, true, &sampleReport),
                 qPrintable(sampleReport.toText()));
    };
    for(const QPoint& center : groundCenters)
        paintSample(center, 2);
    paintSample(QPoint(100, 40), 3);

    for(int index : threeWayChanged)
    {
        const int x = index % threeWayTerrain.tileCountX();
        const int y = index / threeWayTerrain.tileCountX();
        bool found = false;
        const STerrainTransitionSignature here =
            analysis.landCatalog.gridSignatureForPacked(threeWayTerrain.packedTile(
                x, y, ETerrainTileLayer::Land), &found);
        QVERIFY(found);
        if(x + 1 < threeWayTerrain.tileCountX())
        {
            const STerrainTransitionSignature east =
                analysis.landCatalog.gridSignatureForPacked(threeWayTerrain.packedTile(
                    x + 1, y, ETerrainTileLayer::Land), &found);
            QVERIFY(found);
            QCOMPARE(here.northEast, east.northWest);
            QCOMPARE(here.southEast, east.southWest);
        }
        if(y + 1 < threeWayTerrain.tileCountY())
        {
            const STerrainTransitionSignature south =
                analysis.landCatalog.gridSignatureForPacked(threeWayTerrain.packedTile(
                    x, y + 1, ETerrainTileLayer::Land), &found);
            QVERIFY(found);
            QCOMPARE(here.southWest, south.northWest);
            QCOMPARE(here.southEast, south.northEast);
        }
    }
    const QVector<QPoint> darkGrassCenters({
        QPoint(100, 38), QPoint(99, 38), QPoint(99, 37), QPoint(99, 36),
        QPoint(99, 39), QPoint(101, 35), QPoint(100, 41)
    });
    for(const QPoint& center : darkGrassCenters)
        paintSample(center, 3);
    paintSample(QPoint(100, 39), 7);

    for(int index : threeWayChanged)
    {
        const int x = index % threeWayTerrain.tileCountX();
        const int y = index / threeWayTerrain.tileCountX();
        bool found = false;
        const STerrainTransitionSignature here =
            analysis.landCatalog.gridSignatureForPacked(threeWayTerrain.packedTile(
                x, y, ETerrainTileLayer::Land), &found);
        QVERIFY(found);
        if(x + 1 < threeWayTerrain.tileCountX())
        {
            const STerrainTransitionSignature east =
                analysis.landCatalog.gridSignatureForPacked(threeWayTerrain.packedTile(
                    x + 1, y, ETerrainTileLayer::Land), &found);
            QVERIFY(found);
            QCOMPARE(here.northEast, east.northWest);
            QCOMPARE(here.southEast, east.southWest);
        }
        if(y + 1 < threeWayTerrain.tileCountY())
        {
            const STerrainTransitionSignature south =
                analysis.landCatalog.gridSignatureForPacked(threeWayTerrain.packedTile(
                    x, y + 1, ETerrainTileLayer::Land), &found);
            QVERIFY(found);
            QCOMPARE(here.southWest, south.northWest);
            QCOMPARE(here.southEast, south.northEast);
        }
    }

    const auto applyJunctionSample = [&](CTerrainEditModel& model,
                                         QSet<int>& changed,
                                         const QPoint& center,
                                         qint32 material) {
        STerrainTileBrush sampleBrush;
        sampleBrush.centerX = float(center.x());
        sampleBrush.centerY = float(center.y());
        sampleBrush.radius = 0.0f;
        sampleBrush.shape = ETerrainTileBrushShape::Circle;
        CValidationReport sampleReport;
        const STerrainTileCommand sample = model.makeSemanticTilePaint(
            sampleBrush, ETerrainTileLayer::Land, material,
            analysis.landCatalog, 12345u, &sampleReport);
        QVERIFY2(!sampleReport.hasErrors(), qPrintable(sampleReport.toText()));
        QVERIFY(sample.semanticAudit.valid);
        QCOMPARE(sample.semanticAudit.requestedMaterialId, material);
        QCOMPARE(sample.semanticAudit.paintedCoreCells,
                 QVector<QPoint>({center}));
        QVERIFY(!sample.semanticAudit.affectedCells.isEmpty());
        QVERIFY(sample.semanticAudit.solverMode != QStringLiteral("not-run"));
        for(const STerrainTileDelta& delta : sample.deltas)
            changed.insert(delta.globalY * model.tileCountX() + delta.globalX);
        QVERIFY2(model.apply(sample, true, &sampleReport),
                 qPrintable(sampleReport.toText()));
    };
    const auto verifyJunctionSeams = [&](const CTerrainEditModel& model,
                                         const QSet<int>& changed) {
        for(int index : changed)
        {
            const int x = index % model.tileCountX();
            const int y = index / model.tileCountX();
            bool found = false;
            const STerrainTransitionSignature here =
                analysis.landCatalog.gridSignatureForPacked(model.packedTile(
                    x, y, ETerrainTileLayer::Land), &found);
            QVERIFY(found);
            if(x + 1 < model.tileCountX())
            {
                const STerrainTransitionSignature east =
                    analysis.landCatalog.gridSignatureForPacked(model.packedTile(
                        x + 1, y, ETerrainTileLayer::Land), &found);
                QVERIFY(found);
                QCOMPARE(here.northEast, east.northWest);
                QCOMPARE(here.southEast, east.southWest);
            }
            if(y + 1 < model.tileCountY())
            {
                const STerrainTransitionSignature south =
                    analysis.landCatalog.gridSignatureForPacked(model.packedTile(
                        x, y + 1, ETerrainTileLayer::Land), &found);
                QVERIFY(found);
                QCOMPARE(here.southWest, south.northWest);
                QCOMPARE(here.southEast, south.northEast);
            }
        }
    };

    // A dark patch followed by adjacent irregular grass, both surrounded by
    // common grass, must select an authored 3/4/9 junction instead of erasing
    // either minority family merely because the resulting edges are valid.
    CTerrainEditModel preservedMinorityJunction;
    QVERIFY(preservedMinorityJunction.load(document));
    QSet<int> preservedMinorityChanged;
    applyJunctionSample(preservedMinorityJunction, preservedMinorityChanged,
                        QPoint(101, 38), 3);
    bool darkPresentBeforeIrregular = false;
    for(int index : preservedMinorityChanged)
    {
        bool found = false;
        const STerrainTransitionSignature signature =
            analysis.landCatalog.gridSignatureForPacked(
                preservedMinorityJunction.packedTile(
                    index % preservedMinorityJunction.tileCountX(),
                    index / preservedMinorityJunction.tileCountX(),
                    ETerrainTileLayer::Land), &found);
        QVERIFY(found);
        darkPresentBeforeIrregular |= signature.northWest == 3 || signature.northEast == 3
            || signature.southEast == 3 || signature.southWest == 3;
    }
    QVERIFY(darkPresentBeforeIrregular);
    applyJunctionSample(preservedMinorityJunction, preservedMinorityChanged,
                        QPoint(102, 38), 9);
    verifyJunctionSeams(preservedMinorityJunction, preservedMinorityChanged);
    bool foundDarkIrregularCommonJunction = false;
    QStringList observedMinoritySignatures;
    for(int index : preservedMinorityChanged)
    {
        bool found = false;
        const STerrainTransitionSignature signature =
            analysis.landCatalog.gridSignatureForPacked(
                preservedMinorityJunction.packedTile(
                    index % preservedMinorityJunction.tileCountX(),
                    index / preservedMinorityJunction.tileCountX(),
                    ETerrainTileLayer::Land), &found);
        QVERIFY(found);
        const QSet<qint32> families({signature.northWest, signature.northEast,
                                     signature.southEast, signature.southWest});
        observedMinoritySignatures.append(QString("%1,%2,%3,%4")
            .arg(signature.northWest).arg(signature.northEast)
            .arg(signature.southEast).arg(signature.southWest));
        if(families.contains(3) && families.contains(4) && families.contains(9))
            foundDarkIrregularCommonJunction = true;
    }
    QVERIFY2(foundDarkIrregularCommonJunction,
             qPrintable(observedMinoritySignatures.join(" | ")));

    const auto applyRadiusSample = [&](CTerrainEditModel& model,
                                        const QPoint& center,
                                        float radius,
                                        qint32 material) -> STerrainTileCommand {
        STerrainTileBrush sampleBrush;
        sampleBrush.centerX = float(center.x());
        sampleBrush.centerY = float(center.y());
        sampleBrush.radius = radius;
        sampleBrush.shape = ETerrainTileBrushShape::Circle;
        CValidationReport sampleReport;
        const STerrainTileCommand sample = model.makeSemanticTilePaint(
            sampleBrush, ETerrainTileLayer::Land, material,
            analysis.landCatalog, 12345u, &sampleReport);
        if(sampleReport.hasErrors() || sample.isEmpty()
           || !model.apply(sample, true, &sampleReport))
            return STerrainTileCommand();
        return sample;
    };
    const auto materialCornerCount = [&](const CTerrainEditModel& model,
                                          const QRect& area,
                                          qint32 material) {
        int count = 0;
        for(int y = area.top(); y <= area.bottom(); ++y)
        {
            for(int x = area.left(); x <= area.right(); ++x)
            {
                bool found = false;
                const STerrainTransitionSignature signature =
                    analysis.landCatalog.gridSignatureForPacked(model.packedTile(
                        x, y, ETerrainTileLayer::Land), &found);
                if(!found)
                    continue;
                count += signature.northWest == material;
                count += signature.northEast == material;
                count += signature.southEast == material;
                count += signature.southWest == material;
            }
        }
        return count;
    };

    // R22 diagnostic 3: the two later strokes do not overlap the Irregular
    // core. Lime and then Ground may retile their contacted boundaries, but
    // neither may consume the established Irregular ownership or make the
    // preceding Lime island retreat.
    CTerrainEditModel preservedSequenceThree;
    QVERIFY(preservedSequenceThree.load(document));
    QVERIFY(!applyRadiusSample(preservedSequenceThree,
                               QPoint(101, 34), 0.0f, 9).isEmpty());
    const QRect sequenceThreeArea(97, 28, 9, 11);
    const int irregularAfterPaint = materialCornerCount(
        preservedSequenceThree, sequenceThreeArea, 9);
    QVERIFY(!applyRadiusSample(preservedSequenceThree,
                               QPoint(101, 32), 0.0f, 7).isEmpty());
    const int irregularAfterLime = materialCornerCount(
        preservedSequenceThree, sequenceThreeArea, 9);
    QVERIFY(irregularAfterLime >= irregularAfterPaint);
    const int limeBeforeGround = materialCornerCount(
        preservedSequenceThree, sequenceThreeArea, 7);
    STerrainTileBrush sequenceGroundBrush;
    sequenceGroundBrush.centerX = 100.0f;
    sequenceGroundBrush.centerY = 33.0f;
    sequenceGroundBrush.radius = 0.0f;
    sequenceGroundBrush.shape = ETerrainTileBrushShape::Circle;
    CValidationReport sequenceGroundReport;
    const STerrainTileCommand sequenceGround =
        preservedSequenceThree.makeSemanticTilePaint(
            sequenceGroundBrush, ETerrainTileLayer::Land, 2,
            analysis.landCatalog, 12345u, &sequenceGroundReport);
    QVERIFY(sequenceGround.isEmpty());
    QVERIFY(sequenceGroundReport.hasErrors());
    QCOMPARE(sequenceGroundReport.findings().front().code,
             QStringLiteral("terrain.semantic_core_unsupported"));
    QVERIFY(materialCornerCount(preservedSequenceThree,
                                sequenceThreeArea, 9) >= irregularAfterLime);
    QVERIFY(materialCornerCount(preservedSequenceThree,
                                sequenceThreeArea, 7) >= limeBeforeGround);

    // R22 diagnostics 4/5: once a Lime boundary is established, nearby Ground
    // strokes must not consume isolated Lime half-corners outside their core.
    CTerrainEditModel stableLimeBoundary;
    QVERIFY(stableLimeBoundary.load(document));
    const QVector<QPoint> limeBoundaryPoints({QPoint(101, 32), QPoint(100, 32),
                                               QPoint(99, 32), QPoint(99, 31),
                                               QPoint(99, 30), QPoint(100, 30),
                                               QPoint(101, 30)});
    for(const QPoint& point : limeBoundaryPoints)
        QVERIFY(!applyRadiusSample(stableLimeBoundary, point, 0.0f, 7).isEmpty());
    const QRect stableLimeArea(95, 25, 11, 14);
    int stableLimeCount = materialCornerCount(
        stableLimeBoundary, stableLimeArea, 7);
    const QVector<QPoint> nearbyGroundPoints({QPoint(100, 34), QPoint(101, 34),
                                               QPoint(99, 34), QPoint(99, 28),
                                               QPoint(100, 28), QPoint(101, 28)});
    for(const QPoint& point : nearbyGroundPoints)
    {
        QVERIFY(!applyRadiusSample(stableLimeBoundary, point, 0.0f, 2).isEmpty());
        const int afterGround = materialCornerCount(
            stableLimeBoundary, stableLimeArea, 7);
        QVERIFY(afterGround >= stableLimeCount);
        stableLimeCount = afterGround;
    }

    // Lime inside a Dark-grass patch surrounded by Common grass has two
    // equally short authored routes. Reuse Common (7/4/3), never introduce
    // absent Ground (7/2/3) merely because material 2 sorts first.
    CTerrainEditModel limeInsideDark;
    QVERIFY(limeInsideDark.load(document));
    QSet<int> limeInsideDarkChanged;
    for(int y = 45; y <= 47; ++y)
        for(int x = 114; x <= 116; ++x)
            applyJunctionSample(limeInsideDark, limeInsideDarkChanged, QPoint(x, y), 3);
    const QRect limeDarkArea(110, 41, 11, 11);
    const int groundBeforeLime = materialCornerCount(limeInsideDark, limeDarkArea, 2);
    const STerrainTileCommand limeCommand =
        applyRadiusSample(limeInsideDark, QPoint(115, 46), 1.0f, 7);
    QVERIFY(!limeCommand.isEmpty());
    QCOMPARE(limeCommand.semanticAudit.materialPath,
             QVector<qint32>({7, 4, 3}));
    QCOMPARE(materialCornerCount(limeInsideDark, limeDarkArea, 2), groundBeforeLime);
    QVERIFY(materialCornerCount(limeInsideDark, limeDarkArea, 3) > 0);

    // The former preservation anchors could reject this ordinary nested edit
    // before search began. Ground must be paintable inside Irregular grass
    // surrounded by Lime without deleting the command as an impossible CSP.
    CTerrainEditModel nestedGround;
    QVERIFY(nestedGround.load(document));
    QVERIFY(!applyRadiusSample(nestedGround, QPoint(109, 40), 2.0f, 7).isEmpty());
    QVERIFY(!applyRadiusSample(nestedGround, QPoint(109, 40), 1.0f, 9).isEmpty());
    const STerrainTileCommand nestedGroundCommand =
        applyRadiusSample(nestedGround, QPoint(109, 40), 0.0f, 2);
    QVERIFY(!nestedGroundCommand.isEmpty());

    // R21 diagnostic 4 minimized to the exact 7x7 pre-stroke neighborhood.
    // Painting Ground/common at 103,37 previously changed the unrelated
    // Common-grass corner at 105,39 into Stone/lime solely because Lime was
    // present nearby. Local support must break that otherwise valid CSP tie.
    struct SLocalPackedTile { int x; int y; quint16 packed; };
    const QVector<SLocalPackedTile> diagnosticFourState({
        {100,34,5},{101,34,32775},{102,34,49155},{103,34,32772},{104,34,5},{105,34,32855},{106,34,32881},
        {100,35,49159},{101,35,32775},{102,35,49158},{103,35,7},{104,35,16388},{105,35,49272},{106,35,16512},
        {100,36,7},{101,36,16389},{102,36,7},{103,36,49157},{104,36,6},{105,36,49241},{106,36,84},
        {100,37,49157},{101,37,16387},{102,37,16387},{103,37,16390},{104,37,16387},{105,37,7},{106,37,32771},
        {100,38,16391},{101,38,4},{102,38,32853},{103,38,32882},{104,38,16467},{105,38,16390},{106,38,49155},
        {100,39,32771},{101,39,6},{102,39,49262},{103,39,32817},{104,39,16493},{105,39,6},{106,39,49156},
        {100,40,16508},{101,40,185},{102,40,49651},{103,40,16432},{104,40,16501},{105,40,49155},{106,40,16391}
    });
    CTerrainEditModel diagnosticFour;
    QVERIFY(diagnosticFour.load(document));
    STerrainTileCommand diagnosticFourSetup;
    for(const SLocalPackedTile& tile : diagnosticFourState)
    {
        const quint16 before = diagnosticFour.packedTile(
            tile.x, tile.y, ETerrainTileLayer::Land);
        if(before == tile.packed)
            continue;
        STerrainTileDelta delta;
        delta.globalX = tile.x;
        delta.globalY = tile.y;
        delta.layer = ETerrainTileLayer::Land;
        delta.before = before;
        delta.after = tile.packed;
        diagnosticFourSetup.deltas.append(delta);
    }
    QVERIFY(diagnosticFour.apply(diagnosticFourSetup, true));
    STerrainTileBrush diagnosticFourBrush;
    diagnosticFourBrush.centerX = 103.0f;
    diagnosticFourBrush.centerY = 37.0f;
    diagnosticFourBrush.radius = 0.0f;
    diagnosticFourBrush.shape = ETerrainTileBrushShape::Circle;
    CValidationReport diagnosticFourReport;
    const STerrainTileCommand diagnosticFourPaint = diagnosticFour.makeSemanticTilePaint(
        diagnosticFourBrush, ETerrainTileLayer::Land, 2,
        analysis.landCatalog, 12345u, &diagnosticFourReport);
    QVERIFY2(!diagnosticFourReport.hasErrors(),
             qPrintable(diagnosticFourReport.toText()));
    QVERIFY(!diagnosticFourPaint.isEmpty());
    int commonToLimeCorners = 0;
    QSet<int> countedDiagnosticFourCorners;
    for(const STerrainTileDelta& delta : diagnosticFourPaint.deltas)
    {
        bool beforeFound = false;
        bool afterFound = false;
        const STerrainTransitionSignature before =
            analysis.landCatalog.gridSignatureForPacked(delta.before, &beforeFound);
        const STerrainTransitionSignature after =
            analysis.landCatalog.gridSignatureForPacked(delta.after, &afterFound);
        QVERIFY(beforeFound);
        QVERIFY(afterFound);
        const qint32 beforeValues[4] = {before.northWest, before.northEast,
                                        before.southEast, before.southWest};
        const qint32 afterValues[4] = {after.northWest, after.northEast,
                                       after.southEast, after.southWest};
        const int cornerKeys[4] = {
            delta.globalY * (diagnosticFour.tileCountX() + 1) + delta.globalX,
            delta.globalY * (diagnosticFour.tileCountX() + 1) + delta.globalX + 1,
            (delta.globalY + 1) * (diagnosticFour.tileCountX() + 1) + delta.globalX + 1,
            (delta.globalY + 1) * (diagnosticFour.tileCountX() + 1) + delta.globalX
        };
        for(int corner = 0; corner < 4; ++corner)
        {
            if(countedDiagnosticFourCorners.contains(cornerKeys[corner]))
                continue;
            countedDiagnosticFourCorners.insert(cornerKeys[corner]);
            commonToLimeCorners += beforeValues[corner] == 4 && afterValues[corner] == 7;
        }
    }
    QCOMPARE(commonToLimeCorners, 0);

    const auto diagnosticConversionCount = [&](const STerrainTileCommand& paint,
                                                 qint32 fromMaterial,
                                                 qint32 toMaterial) {
        int count = 0;
        QSet<int> counted;
        for(const STerrainTileDelta& delta : paint.deltas)
        {
            bool beforeFound = false;
            bool afterFound = false;
            const STerrainTransitionSignature before =
                analysis.landCatalog.gridSignatureForPacked(delta.before, &beforeFound);
            const STerrainTransitionSignature after =
                analysis.landCatalog.gridSignatureForPacked(delta.after, &afterFound);
            if(!beforeFound || !afterFound)
                return -1;
            const qint32 beforeValues[4] = {before.northWest, before.northEast,
                                            before.southEast, before.southWest};
            const qint32 afterValues[4] = {after.northWest, after.northEast,
                                           after.southEast, after.southWest};
            const int cornerKeys[4] = {
                delta.globalY * (diagnosticFour.tileCountX() + 1) + delta.globalX,
                delta.globalY * (diagnosticFour.tileCountX() + 1) + delta.globalX + 1,
                (delta.globalY + 1) * (diagnosticFour.tileCountX() + 1) + delta.globalX + 1,
                (delta.globalY + 1) * (diagnosticFour.tileCountX() + 1) + delta.globalX
            };
            for(int corner = 0; corner < 4; ++corner)
            {
                if(counted.contains(cornerKeys[corner]))
                    continue;
                counted.insert(cornerKeys[corner]);
                count += beforeValues[corner] == fromMaterial
                    && afterValues[corner] == toMaterial;
            }
        }
        return count;
    };
    const auto replayLocalDiagnostic = [&](const QVector<SLocalPackedTile>& state,
                                            const QPoint& center,
                                            qint32 material) {
        CTerrainEditModel model;
        if(!model.load(document))
            return STerrainTileCommand();
        STerrainTileCommand setup;
        for(const SLocalPackedTile& tile : state)
        {
            const quint16 before = model.packedTile(tile.x, tile.y,
                                                    ETerrainTileLayer::Land);
            if(before == tile.packed)
                continue;
            STerrainTileDelta delta;
            delta.globalX = tile.x;
            delta.globalY = tile.y;
            delta.layer = ETerrainTileLayer::Land;
            delta.before = before;
            delta.after = tile.packed;
            setup.deltas.append(delta);
        }
        if(!model.apply(setup, true))
            return STerrainTileCommand();
        STerrainTileBrush sampleBrush;
        sampleBrush.centerX = float(center.x());
        sampleBrush.centerY = float(center.y());
        sampleBrush.radius = 0.0f;
        sampleBrush.shape = ETerrainTileBrushShape::Circle;
        CValidationReport sampleReport;
        return model.makeSemanticTilePaint(sampleBrush, ETerrainTileLayer::Land,
                                           material, analysis.landCatalog,
                                           12345u, &sampleReport);
    };

    const QVector<SLocalPackedTile> diagnosticOneState({
        {95,36,32894},{96,36,49276},{97,36,49155},{98,36,7},{99,36,3},{100,36,7},{101,36,16389},
        {95,37,4},{96,37,49156},{97,37,32945},{98,37,32999},{99,37,32992},{100,37,32998},{101,37,16834},
        {95,38,49156},{96,38,16388},{97,38,49380},{98,38,33112},{99,38,33268},{100,38,33258},{101,38,16884},
        {95,39,49157},{96,39,5},{97,39,49597},{98,39,49496},{99,39,49642},{100,39,16433},{101,39,16874},
        {95,40,16389},{96,40,3},{97,40,49590},{98,40,344},{99,40,49652},{100,40,490},{101,40,500},
        {95,41,32772},{96,41,49155},{97,41,33218},{98,41,16728},{99,41,344},{100,41,49496},{101,41,33112},
        {95,42,32774},{96,42,32774},{97,42,49330},{98,42,438},{99,42,445},{100,42,439},{101,42,445}
    });
    const STerrainTileCommand diagnosticOnePaint = replayLocalDiagnostic(
        diagnosticOneState, QPoint(98, 39), 7);
    QVERIFY(!diagnosticOnePaint.isEmpty());
    QCOMPARE(diagnosticConversionCount(diagnosticOnePaint, 4, 7), 0);

    const QVector<SLocalPackedTile> diagnosticTwoState({
        {96,33,5},{97,33,6},{98,33,32774},{99,33,16391},{100,33,32774},{101,33,32771},{102,33,16387},{103,33,5},{104,33,5},
        {96,34,16390},{97,34,7},{98,34,49155},{99,34,5},{100,34,5},{101,34,32775},{102,34,49155},{103,34,32772},{104,34,5},
        {96,35,126},{97,35,49157},{98,35,3},{99,35,32773},{100,35,49159},{101,35,32775},{102,35,49158},{103,35,7},{104,35,16388},
        {96,36,49276},{97,36,49155},{98,36,7},{99,36,32852},{100,36,32876},{101,36,16470},{102,36,7},{103,36,49157},{104,36,6},
        {96,37,49156},{97,37,49155},{98,37,32854},{99,37,49287},{100,37,49201},{101,37,32897},{102,37,16468},{103,37,16390},{104,37,16387},
        {96,38,16388},{97,38,32852},{98,38,49658},{99,38,411},{100,38,410},{101,38,411},{102,38,33274},{103,38,16466},{104,38,32771},
        {96,39,5},{97,39,49267},{98,39,16795},{99,39,32770},{100,39,16386},{101,39,2},{102,39,49563},{103,39,16500},{104,39,32775},
        {96,40,3},{97,40,49238},{98,40,506},{99,40,33179},{100,40,33178},{101,40,33179},{102,40,16890},{103,40,88},{104,40,5},
        {96,41,49155},{97,41,49155},{98,41,49232},{99,41,139},{100,41,49200},{101,41,16521},{102,41,90},{103,41,7},{104,41,5}
    });
    const STerrainTileCommand diagnosticTwoPaint = replayLocalDiagnostic(
        diagnosticTwoState, QPoint(100, 37), 3);
    QVERIFY(!diagnosticTwoPaint.isEmpty());
    QCOMPARE(diagnosticConversionCount(diagnosticTwoPaint, 3, 4), 0);

    const QVector<SLocalPackedTile> diagnosticThreeState({
        {99,35,32773},{100,35,49159},{101,35,32775},{102,35,49158},{103,35,7},{104,35,16388},{105,35,49272},
        {99,36,3},{100,36,7},{101,36,16389},{102,36,7},{103,36,49157},{104,36,6},{105,36,49241},
        {99,37,32992},{100,37,32998},{101,37,16834},{102,37,33208},{103,37,32886},{104,37,16476},{105,37,7},
        {99,38,49496},{100,38,470},{101,38,16854},{102,38,49642},{103,38,49201},{104,38,16497},{105,38,16390},
        {99,39,33112},{100,39,16854},{101,39,470},{102,39,49642},{103,39,32817},{104,39,16493},{105,39,6},
        {99,40,16728},{100,40,33112},{101,40,49496},{102,40,49642},{103,40,16432},{104,40,16501},{105,40,49155},
        {99,41,33258},{100,41,33258},{101,41,33258},{102,41,16703},{103,41,119},{104,41,80},{105,41,16387}
    });
    const STerrainTileCommand diagnosticThreePaint = replayLocalDiagnostic(
        diagnosticThreeState, QPoint(102, 38), 2);
    QVERIFY(!diagnosticThreePaint.isEmpty());
    QCOMPARE(diagnosticConversionCount(diagnosticThreePaint, 9, 7), 0);

    // R18 follow-up diagnostic cases: irregular grass must coexist with
    // Ground/common and Grass/dark through Grass/common, then Stone/lime must
    // join that dark/irregular boundary without dropping a shared corner.
    CTerrainEditModel irregularJunction;
    QVERIFY(irregularJunction.load(document));
    QSet<int> irregularChanged;
    applyJunctionSample(irregularJunction, irregularChanged, QPoint(101, 40), 2);
    applyJunctionSample(irregularJunction, irregularChanged, QPoint(101, 38), 3);
    applyJunctionSample(irregularJunction, irregularChanged, QPoint(101, 36), 9);
    applyJunctionSample(irregularJunction, irregularChanged, QPoint(99, 37), 7);
    verifyJunctionSeams(irregularJunction, irregularChanged);

    // Dark grass placed between and slightly below two Ground/common circles
    // must keep both boundaries coherent, using authored junction tiles or a
    // Grass/common intermediary where the exact corner pattern is absent.
    CTerrainEditModel groundSandwich;
    QVERIFY(groundSandwich.load(document));
    QSet<int> sandwichChanged;
    applyJunctionSample(groundSandwich, sandwichChanged, QPoint(103, 47), 2);
    applyJunctionSample(groundSandwich, sandwichChanged, QPoint(103, 44), 2);
    const QVector<QPoint> sandwichDarkCenters({
        QPoint(103, 46), QPoint(103, 45), QPoint(102, 46),
        QPoint(101, 45), QPoint(101, 46)
    });
    for(const QPoint& center : sandwichDarkCenters)
        applyJunctionSample(groundSandwich, sandwichChanged, center, 3);
    verifyJunctionSeams(groundSandwich, sandwichChanged);
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

void EnduranceDocumentTests::overlayActivationDeactivationIsLosslessAndUndoable()
{
    QByteArray landSector = syntheticLandSector(1200);
    landSector.append("preserved-sec-tail");
    CMprDocument baseline;
    CValidationReport baselineReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(landSector),
                                "overlay-activation-baseline.mpr", &baselineReport),
             qPrintable(baselineReport.toText()));
    QCOMPARE(baseline.sectors().front().type, quint8(0));

    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));

    // The first multi-cell stroke that upgrades a type-0 SEC must populate
    // every requested cell using the new layout offsets. No second click may
    // be required to repair stale pre-upgrade offsets.
    CTerrainEditModel firstStrokeTerrain;
    QVERIFY(firstStrokeTerrain.load(baseline));
    STerrainTileBrush firstStroke;
    firstStroke.centerX = 8.0f;
    firstStroke.centerY = 8.0f;
    firstStroke.radius = 1.0f;
    firstStroke.shape = ETerrainTileBrushShape::Circle;
    CValidationReport firstStrokeReport;
    const SOverlaySurfaceCommand firstStrokeActivation =
        firstStrokeTerrain.makeOverlayActivation(
            firstStroke, 1, 0, 1, 2, &firstStrokeReport);
    QVERIFY2(!firstStrokeActivation.isEmpty(),
             qPrintable(firstStrokeReport.toText()));
    QVERIFY(firstStrokeTerrain.apply(firstStrokeActivation, true,
                                     &firstStrokeReport));
    int activeFirstStrokeCells = 0;
    for(int y = 7; y <= 9; ++y)
        for(int x = 7; x <= 9; ++x)
        {
            bool active = false;
            const qint16 material = firstStrokeTerrain.overlayMaterialIndex(
                x, y, &active);
            const bool expected = qAbs(x - 8) + qAbs(y - 8) <= 1;
            QCOMPARE(active, expected);
            if(expected)
            {
                QCOMPARE(material, qint16(1));
                ++activeFirstStrokeCells;
            }
        }
    QCOMPARE(activeFirstStrokeCells, 5);
    // Upgrading coverage is allowed to append an overlay vertex/tile/material
    // layout, but the complete land vertex block is immutable. A historical
    // multi-cell activation reused stale type-0 offsets after the first cell
    // and wrote later overlay records into the SEC header/land vertices.
    QCOMPARE(firstStrokeActivation.sectors.front().afterPayload.mid(
                 5, 33 * 33 * 8),
             landSector.mid(5, 33 * 33 * 8));
    QVERIFY(firstStrokeTerrain.makeOverlayActivation(
        firstStroke, 1, 0, 1, 2).isEmpty());

    // Defense in depth: even a structurally sized overlay command must not be
    // serializable if it detached land bytes from the canonical lattice.
    SOverlaySurfaceCommand corruptLand;
    corruptLand.description = QStringLiteral("synthetic corrupt overlay payload");
    SOverlaySectorDelta corruptDelta;
    corruptDelta.sectorX = 0;
    corruptDelta.sectorY = 0;
    corruptDelta.beforePayload = firstStrokeTerrain.sectorPayload(0, 0);
    corruptDelta.afterPayload = corruptDelta.beforePayload;
    const int corruptLandHeight = 5 + (4 * 33 + 4) * 8 + 2;
    corruptDelta.afterPayload[corruptLandHeight] = char(
        quint8(corruptDelta.afterPayload.at(corruptLandHeight)) ^ quint8(0x5A));
    corruptLand.sectors.append(corruptDelta);
    QVERIFY(firstStrokeTerrain.apply(corruptLand, true, &firstStrokeReport));
    CMprEditTransaction corruptTransaction("reject detached overlay land bytes");
    CValidationReport corruptSaveReport;
    QVERIFY(!firstStrokeTerrain.appendSectorEdits(
        &corruptTransaction, &corruptSaveReport));
    QVERIFY(corruptSaveReport.toText().contains("terrain.land_vertex_integrity"));
    QVERIFY(firstStrokeTerrain.apply(corruptLand, false, &firstStrokeReport));

    STerrainTileBrush point;
    point.centerX = 2.0f;
    point.centerY = 3.0f;
    point.radius = 0.0f;
    CValidationReport activationReport;
    const SOverlaySurfaceCommand activation = terrain.makeOverlayActivation(
        point, 1, 0, 1, 2, &activationReport);
    QVERIFY2(!activation.isEmpty(), qPrintable(activationReport.toText()));
    QCOMPARE(activation.sectors.size(), 1);
    QCOMPARE(activation.sectors.front().beforePayload, landSector);
    QCOMPARE(activation.sectors.front().afterPayload.size(),
             landSector.size() + 33 * 33 * 8 + 2 * 16 * 16 * 2);
    QVERIFY(activation.sectors.front().afterPayload.endsWith("preserved-sec-tail"));

    COverlaySurfaceEditHistory history;
    QVERIFY(history.execute(&terrain, activation));
    bool available = false;
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(1));
    QVERIFY(available);
    const quint16 activatedTile = terrain.packedTile(
        2, 3, ETerrainTileLayer::Overlay, &available);
    QVERIFY(available);
    QCOMPARE(SMprPackedTile::decode(activatedTile).rotation, quint8(2));
    const QByteArray activatedPayload = terrain.sectorPayload(0, 0);
    const int activatedOverlayVertex = 5 + 33 * 33 * 8 + (6 * 33 + 4) * 8 + 2;
    QDataStream activatedHeightStream(activatedPayload.mid(activatedOverlayVertex, 2));
    activatedHeightStream.setByteOrder(QDataStream::LittleEndian);
    quint16 activatedHeight = 0;
    activatedHeightStream >> activatedHeight;
    QCOMPARE(activatedHeight, quint16(1214));
    QCOMPARE(terrain.overlayMaterialIndex(3, 3, &available), qint16(-1));
    QVERIFY(!available);
    QVERIFY(terrain.makeOverlayActivation(point, 1, 0, 1, 2).isEmpty());
    const SOverlaySurfaceCommand materialChange = terrain.makeOverlayActivation(
        point, 0, 0, 1, 2);
    QVERIFY(!materialChange.isEmpty());
    QVERIFY(terrain.apply(materialChange, true));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(0));
    QVERIFY(available);
    QVERIFY(terrain.apply(materialChange, false));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(1));

    CMprEditTransaction transaction("activate one synthetic overlay cell");
    QVERIFY(terrain.appendSectorEdits(&transaction));
    QByteArray candidateBytes;
    QVERIFY(baseline.applyTransaction(transaction, &candidateBytes));
    CMprDocument candidate;
    CValidationReport candidateReport;
    QVERIFY2(candidate.loadBytes(candidateBytes, "overlay-activation-candidate.mpr",
                                 &candidateReport), qPrintable(candidateReport.toText()));
    QCOMPARE(candidate.sectors().front().type, quint8(3));
    QCOMPARE(candidate.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    QCOMPARE(candidate.memberPayload("caveset.mp"), baseline.memberPayload("caveset.mp"));
    QVERIFY(candidate.memberPayload("caveset000000.sec").endsWith("preserved-sec-tail"));
    QTemporaryDir stagedDirectory;
    QVERIFY(stagedDirectory.isValid());
    const SStagedMprTransactionValidation staged = validateStagedMprTransaction(
        baseline, transaction, QFileInfo(stagedDirectory.filePath("overlay-created.mpr")));
    QVERIFY2(staged.success, qPrintable(staged.error));
    QCOMPARE(staged.changedMembers, QStringList({QStringLiteral("CaveSet000000.SEC")}));
    CTerrainEditModel reopened;
    QVERIFY(reopened.load(candidate));
    QCOMPARE(reopened.overlayMaterialIndex(2, 3, &available), qint16(1));
    QVERIFY(available);

    const SOverlaySurfaceCommand deactivation = terrain.makeOverlayDeactivation(point);
    QVERIFY(!deactivation.isEmpty());
    QVERIFY(history.execute(&terrain, deactivation));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(-1));
    QVERIFY(!available);
    const QByteArray activePayload = deactivation.sectors.front().beforePayload;
    QByteArray deactivatedPayload = deactivation.sectors.front().afterPayload;
    const int materialOffset = 5 + 2 * 33 * 33 * 8 + 2 * 16 * 16 * 2
        + (3 * 16 + 2) * 2;
    deactivatedPayload.replace(materialOffset, 2, activePayload.mid(materialOffset, 2));
    QCOMPARE(deactivatedPayload, activePayload);

    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(1));
    QVERIFY(available);
    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.sectorPayload(0, 0), landSector);
    QVERIFY(!terrain.isDirty());
    QVERIFY(history.redo(&terrain));
    QVERIFY(history.redo(&terrain));

    SOverlaySurfaceCommand stale = activation;
    CValidationReport staleReport;
    const QByteArray beforeRejected = terrain.sectorPayload(0, 0);
    QVERIFY(!terrain.apply(stale, true, &staleReport));
    QVERIFY(staleReport.hasErrors());
    QCOMPARE(terrain.sectorPayload(0, 0), beforeRejected);

    // Re-adding a cell removed during the current edit session must initialize
    // from current land, not resurrect arbitrary source overlay geometry.
    QByteArray raisedSource = activation.sectors.front().afterPayload;
    const int raisedOverlayVertex = 5 + 33 * 33 * 8 + (6 * 33 + 4) * 8 + 2;
    raisedSource[raisedOverlayVertex] = char(0x60);
    raisedSource[raisedOverlayVertex + 1] = char(0xEA); // 60000
    CMprDocument dormantBaseline;
    CValidationReport dormantReport;
    QVERIFY2(dormantBaseline.loadBytes(syntheticMpr(raisedSource),
                                       "overlay-dormant-source.mpr", &dormantReport),
             qPrintable(dormantReport.toText()));
    CTerrainEditModel dormantTerrain;
    QVERIFY(dormantTerrain.load(dormantBaseline));
    const SOverlaySurfaceCommand removeDormant =
        dormantTerrain.makeOverlayDeactivation(point, &dormantReport);
    QVERIFY(dormantTerrain.apply(removeDormant, true, &dormantReport));
    const SOverlaySurfaceCommand recreateDormant = dormantTerrain.makeOverlayActivation(
        point, 1, 0, 1, 2, &dormantReport);
    QVERIFY2(!recreateDormant.isEmpty(), qPrintable(dormantReport.toText()));
    QDataStream recreatedHeightStream(
        recreateDormant.sectors.front().afterPayload.mid(raisedOverlayVertex, 2));
    recreatedHeightStream.setByteOrder(QDataStream::LittleEndian);
    quint16 recreatedHeight = 0;
    recreatedHeightStream >> recreatedHeight;
    QCOMPARE(recreatedHeight, quint16(1214));

    // A maxed land vertex cannot receive positive raw clearance. Activation
    // stays local and succeeds by clamping only the new overlay surface.
    CMprDocument maxedBaseline;
    CValidationReport maxedReport;
    QVERIFY(maxedBaseline.loadBytes(syntheticMpr(syntheticLandSector(65535)),
                                    "overlay-maxz.mpr", &maxedReport));
    CTerrainEditModel maxedTerrain;
    QVERIFY(maxedTerrain.load(maxedBaseline));
    const SOverlaySurfaceCommand maxedActivation = maxedTerrain.makeOverlayActivation(
        point, 1, 0, 1, 0, &maxedReport);
    QVERIFY2(!maxedActivation.isEmpty(), qPrintable(maxedReport.toText()));
    QVERIFY(!maxedReport.hasErrors());
    QCOMPARE(maxedActivation.heightClampedVertexCount, 9);
    QDataStream maxedHeightStream(
        maxedActivation.sectors.front().afterPayload.mid(activatedOverlayVertex, 2));
    maxedHeightStream.setByteOrder(QDataStream::LittleEndian);
    quint16 maxedHeight = 0;
    maxedHeightStream >> maxedHeight;
    QCOMPARE(maxedHeight, quint16(65535));
}

void EnduranceDocumentTests::semanticAtlasTransitionsCannotTransitivelyMergeMaterialAnchors()
{
    const auto tile = [](const QColor& west, const QColor& east) {
        QImage image(18, 18, QImage::Format_RGBA8888);
        for(int y = 0; y < image.height(); ++y)
            for(int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y, x < image.width() / 2 ? west : east);
        return image;
    };
    const QColor dark(20, 20, 20), step1(40, 40, 40);
    const QColor step2(60, 60, 60), light(80, 80, 80);
    QVector<QImage> images;
    images << tile(dark, dark) << tile(dark, step1)
           << tile(step1, step2) << tile(step2, light)
           << tile(light, light);
    QVector<STerrainAtlasSlotUsage> usage(images.size());
    for(STerrainAtlasSlotUsage& item : usage)
        item.landCount = 1;
    const STerrainAtlasProfile profile = CTerrainAtlasProfileInference::propose(
        "synthetic_chain", images, QVector<qint32>(images.size(), 1), usage);
    const qint32 darkFamily = profile.slotDefinitions.front().signature.northWest;
    const qint32 lightFamily = profile.slotDefinitions.back().signature.northWest;
    QVERIFY(darkFamily > 0);
    QVERIFY(lightFamily > 0);
    QVERIFY(darkFamily != lightFamily);
    QCOMPARE(profile.families.size(), 2);
}

void EnduranceDocumentTests::semanticAtlasTransitionsCrossTileTypeMetadata()
{
    const auto tile = [](const QColor& west, const QColor& east) {
        QImage image(18, 18, QImage::Format_RGBA8888);
        for(int y = 0; y < image.height(); ++y)
            for(int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y, x < image.width() / 2 ? west : east);
        return image;
    };
    const QColor grass(20, 110, 30);
    const QColor stone(175, 175, 165);
    QVector<QImage> images({tile(grass, grass), tile(stone, stone),
                            tile(grass, stone)});
    QVector<qint32> tileTypes({0, 1, 6});
    QVector<STerrainAtlasSlotUsage> usage(images.size());
    for(STerrainAtlasSlotUsage& item : usage)
        item.landCount = 1;
    const STerrainAtlasProfile profile = CTerrainAtlasProfileInference::propose(
        "cross_type", images, tileTypes, usage);
    QCOMPARE(profile.provenance, CTerrainAtlasProfileInference::currentProvenance());
    QCOMPARE(profile.families.size(), 2);
    const STerrainTransitionSignature transition = profile.slotDefinitions.at(2).signature;
    QVERIFY(transition.northWest != transition.northEast);
    QCOMPARE(transition.northWest, transition.southWest);
    QCOMPARE(transition.northEast, transition.southEast);
    QVERIFY(CTerrainAtlasProfileInference::isCompatibleInferenceProvenance(
        profile.provenance));
    QVERIFY(!CTerrainAtlasProfileInference::isCompatibleInferenceProvenance(
        "decoded-atlas-pixels+mpr-tile-types+observed-map-usage-v1"));
    QVERIFY(CTerrainAtlasProfileInference::isCompatibleInferenceProvenance(
        "user-authored-custom-profile"));
}

void EnduranceDocumentTests::authoredAtlasProfileMatchesZone1Catalog()
{
    CValidationReport report;
    const STerrainAtlasAnalysis analysis = CTerrainAtlasAnalyzer::analyze(
        "zone1", 512, &report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    const STerrainAtlasProfile profile =
        CTerrainAtlasProfileInference::fromAuthoredAnalysis(
            "zone1", QByteArray("synthetic-hash"), 512, analysis, &report);
    QVERIFY2(profile.approved, qPrintable(report.toText()));
    QCOMPARE(profile.slotDefinitions.size(), 512);
    QVERIFY(profile.provenance.startsWith("authored-template:"));
    for(int slot : {0, 134, 511})
    {
        SMprPackedTile packed;
        packed.atlasIndex = quint8(slot / 64);
        packed.tileIndex = quint8(slot % 64);
        bool landFound = false, overlayFound = false;
        const STerrainTransitionSignature land =
            analysis.landCatalog.gridSignatureForPacked(packed.encode(), &landFound);
        const STerrainTransitionSignature overlay =
            analysis.overlayCatalog.gridSignatureForPacked(packed.encode(), &overlayFound);
        QCOMPARE(landFound, !overlayFound);
        const STerrainTransitionSignature expected = landFound ? land : overlay;
        const STerrainTransitionSignature actual = profile.slotDefinitions.at(slot).signature;
        QCOMPARE(actual.northWest, expected.northWest);
        QCOMPARE(actual.northEast, expected.northEast);
        QCOMPARE(actual.southEast, expected.southEast);
        QCOMPARE(actual.southWest, expected.southWest);
    }
    QVERIFY(std::any_of(profile.families.cbegin(), profile.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return family.name == QStringLiteral("Stone / lime");
        }));
}

void EnduranceDocumentTests::overlaySculptChangesOnlyActiveOverlayGeometry()
{
    const QByteArray landSector = syntheticLandSector(1200);
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(syntheticMpr(landSector),
                                "overlay-sculpt-baseline.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));
    STerrainTileBrush cell;
    cell.centerX = 2.0f;
    cell.centerY = 3.0f;
    cell.radius = 0.0f;
    const SOverlaySurfaceCommand activation = terrain.makeOverlayActivation(
        cell, 1, 0, 1, 0, &report);
    QVERIFY2(!activation.isEmpty(), qPrintable(report.toText()));
    QVERIFY(terrain.apply(activation, true, &report));
    const QByteArray activePayload = terrain.sectorPayload(0, 0);

    STerrainBrush brush;
    brush.centerX = 5.0f;
    brush.centerY = 7.0f;
    brush.radius = 1.5f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const SOverlaySurfaceCommand sculpt = terrain.makeOverlayRaiseLower(
        brush, 100.0f, &report);
    QVERIFY2(!sculpt.isEmpty(), qPrintable(report.toText()));
    QCOMPARE(sculpt.sectors.size(), 1);
    const QByteArray sculpted = sculpt.sectors.front().afterPayload;
    const int landVertexEnd = 5 + 33 * 33 * 8;
    QCOMPARE(sculpted.left(landVertexEnd), activePayload.left(landVertexEnd));
    const int overlayTileOffset = 5 + 2 * 33 * 33 * 8 + 16 * 16 * 2;
    QCOMPARE(sculpted.mid(overlayTileOffset), activePayload.mid(overlayTileOffset));
    QVERIFY(sculpted.mid(5 + 33 * 33 * 8, 33 * 33 * 8)
            != activePayload.mid(5 + 33 * 33 * 8, 33 * 33 * 8));

    COverlaySurfaceEditHistory history;
    QVERIFY(history.execute(&terrain, sculpt, &report));
    QVERIFY(terrain.sectorPayload(0, 0) != activePayload);
    QVERIFY(history.undo(&terrain, &report));
    QCOMPARE(terrain.sectorPayload(0, 0), activePayload);
    QVERIFY(history.redo(&terrain, &report));
    QCOMPARE(terrain.sectorPayload(0, 0), sculpted);

    STerrainBrush inactive;
    inactive.centerX = 20.0f;
    inactive.centerY = 20.0f;
    inactive.radius = 1.0f;
    QVERIFY(terrain.makeOverlayRaiseLower(inactive, 100.0f, &report).isEmpty());
}

void EnduranceDocumentTests::overlaySculptSynchronizesSectorSeam()
{
    QVector<STerrainSectorSource> sources = twoSectorTerrain(1000);
    for(STerrainSectorSource& source : sources)
        source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel terrain;
    QVector<qint32> tileTypes(64, 0);
    CValidationReport report;
    QVERIFY2(terrain.loadSources(2, 1, 100.0f, sources, &report,
                                 1, tileTypes, 2), qPrintable(report.toText()));
    STerrainTileBrush cells;
    cells.centerX = 15.5f;
    cells.centerY = 3.0f;
    cells.radius = 0.6f;
    cells.shape = ETerrainTileBrushShape::Square;
    const SOverlaySurfaceCommand activation = terrain.makeOverlayActivation(
        cells, 1, 0, 1, 0, &report);
    QVERIFY2(!activation.isEmpty(), qPrintable(report.toText()));
    QCOMPARE(activation.sectors.size(), 2);
    QVERIFY(terrain.apply(activation, true, &report));

    STerrainBrush brush;
    brush.centerX = 32.0f;
    brush.centerY = 7.0f;
    brush.radius = 1.5f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const SOverlaySurfaceCommand sculpt = terrain.makeOverlayRaiseLower(
        brush, 200.0f, &report);
    QVERIFY2(!sculpt.isEmpty(), qPrintable(report.toText()));
    QCOMPARE(sculpt.sectors.size(), 2);
    QVERIFY(terrain.apply(sculpt, true, &report));
    const QByteArray left = terrain.sectorPayload(0, 0);
    const QByteArray right = terrain.sectorPayload(1, 0);
    const int overlayBase = 5 + 33 * 33 * 8;
    const int leftVertex = overlayBase + (7 * 33 + 32) * 8;
    const int rightVertex = overlayBase + (7 * 33) * 8;
    QCOMPARE(left.mid(leftVertex, 8), right.mid(rightVertex, 8));
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

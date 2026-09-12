#include <QtTest>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSet>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QtEndian>

#include <limits>

#include "endurance_document.h"
#include "ai_graph_document.h"
#include "ai_graph_persistence.h"
#include "landscape_edit_policy.h"
#include "landscape_operation_policy.h"
#include "terrain_atlas_shape_policy.h"
#include "terrain_save_flow_policy.h"
#include "terrain_catalog_load_policy.h"
#include "mmp_codec.h"
#include "operation_history_policy.h"
#include "overlay_material_animation.h"
#include "tile_brush_stroke_policy.h"
#include "portable_diagnostic_writer.h"
#include "res_archive_document.h"
#include "terrain_paint_diagnostic.h"
#include "terrain_edit_model.h"
#include "terrain_semantic_field_store.h"
#include "terrain_tile_information_policy.h"
#include "terrain_atlas_analyzer.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_profile.h"
#include "terrain_atlas_recovery_store.h"
#include "terrain_virtual_atlas.h"
#include "tile.h"
#include "staged_save_validator.h"
#include "backup_manager.h"

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

QByteArray mainWindowSource()
{
    const QString relativePath = QStringLiteral("source/app/main_window.cpp");
    QDir directory(QDir::currentPath());
    for(int depth = 0; depth < 8; ++depth)
    {
        QFile source(directory.filePath(relativePath));
        if(source.open(QIODevice::ReadOnly))
            return source.readAll();
        if(!directory.cdUp())
            break;
    }
    return QByteArray();
}

QByteArray viewSource()
{
    const QString relativePath = QStringLiteral("source/render/view.cpp");
    QDir directory(QDir::currentPath());
    for(int depth = 0; depth < 8; ++depth)
    {
        QFile source(directory.filePath(relativePath));
        if(source.open(QIODevice::ReadOnly))
            return source.readAll();
        if(!directory.cdUp())
            break;
    }
    return QByteArray();
}

QByteArray repositorySource(const QString& relativePath)
{
    QDir directory(QDir::currentPath());
    for(int depth = 0; depth < 8; ++depth)
    {
        QFile source(directory.filePath(relativePath));
        if(source.open(QIODevice::ReadOnly))
            return source.readAll();
        if(!directory.cdUp())
            break;
    }
    return QByteArray();
}

QByteArray emptyMpr()
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x019CE23C) << quint32(0) << quint32(16) << quint32(0);
    return bytes;
}

QByteArray syntheticMpData(quint32 sectorCountX, quint32 sectorCountY,
                           int tileTypeCount)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0xCE4AF672) << float(100.0f)
           << sectorCountX << sectorCountY
           << quint32(2) << quint32(512)
           << quint32(tileTypeCount) << quint32(64)
           << quint16(2) << quint32(0);
    for(int material = 0; material < 2; ++material)
    {
        stream << qint32(3)
               << float(0.2f + material * 0.1f) << float(0.3f) << float(0.6f)
               << float(0.7f) << float(0.0f) << float(0.5f)
               << float(0.0f) << float(0.0f) << float(0.0f) << float(0.0f);
    }
    for(int tile = 0; tile < tileTypeCount; ++tile)
        stream << qint32(tile == 0 ? 1 : 2);
    return bytes;
}

QByteArray syntheticMp(int tileTypeCount = 3)
{
    return syntheticMpData(1, 1, tileTypeCount);
}

QByteArray syntheticMp(quint32 sectorCountX, quint32 sectorCountY)
{
    return syntheticMpData(sectorCountX, sectorCountY, 3);
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

QByteArray syntheticMpr(
    const QByteArray& sectorPayload = syntheticOverlaySector(),
    const QByteArray& mpPayload = syntheticMp())
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
    members.append({QByteArray("CaveSet.MP"), mpPayload, 0x50607080, 0});
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

void setSyntheticLandVertex(QByteArray* sector, int x, int y,
                            qint8 xOffset, qint8 yOffset, quint16 height)
{
    Q_ASSERT(sector);
    Q_ASSERT(x >= 0 && x < 33 && y >= 0 && y < 33);
    const int offset = 5 + (y * 33 + x) * 8;
    (*sector)[offset] = char(xOffset);
    (*sector)[offset + 1] = char(yOffset);
    uchar encoded[2];
    qToLittleEndian<quint16>(height, encoded);
    (*sector)[offset + 2] = char(encoded[0]);
    (*sector)[offset + 3] = char(encoded[1]);
}

quint32 testReadU32(const QByteArray& bytes, int offset)
{
    QDataStream stream(bytes.mid(offset, 4));
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 value = 0;
    stream >> value;
    return value;
}

QByteArray runtimeMprLookup(const QByteArray& archive, const QString& requested)
{
    const quint32 count = testReadU32(archive, 4);
    const quint32 tableOffset = testReadU32(archive, 8);
    const quint32 namesLength = testReadU32(archive, 12);
    if(!count || tableOffset + count * 22 + namesLength > quint32(archive.size()))
        return {};
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    const QByteArray wanted = codec->fromUnicode(requested.toLower());
    quint32 sum = 0;
    for(const unsigned char byte : wanted)
        sum += byte;
    quint32 slot = sum % count;
    QSet<quint32> visited;
    while(slot != quint32(-1) && slot < count && !visited.contains(slot))
    {
        visited.insert(slot);
        const int entry = int(tableOffset + slot * 22);
        const quint32 next = testReadU32(archive, entry);
        const quint32 size = testReadU32(archive, entry + 4);
        const quint32 offset = testReadU32(archive, entry + 8);
        QDataStream nameStream(archive.mid(entry + 16, 2));
        nameStream.setByteOrder(QDataStream::LittleEndian);
        quint16 nameLength = 0;
        nameStream >> nameLength;
        const quint32 nameOffset = testReadU32(archive, entry + 18);
        const QByteArray encoded = archive.mid(
            int(tableOffset + count * 22 + nameOffset), nameLength);
        if(codec->toUnicode(encoded).compare(requested, Qt::CaseInsensitive) == 0)
            return archive.mid(int(offset), int(size));
        slot = next;
    }
    return {};
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

quint16 overlaySectorHeight(const QByteArray& sector, int x, int y)
{
    const int offset = 5 + 33 * 33 * 8 + (y * 33 + x) * 8 + 2;
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

QByteArray syntheticValidAiGraph(quint8 localPosition, quint16 component)
{
    QVector<SAiGraphLayer> layers(CAiGraphDocument::LayerCount);
    for(SAiGraphLayer& layer : layers)
    {
        layer.weights.fill(qint16(-1), CAiGraphDocument::DirectionCount);
        layer.packedPositions = QByteArray(1, char(localPosition));
        layer.components.fill(component, 1);
    }
    CAiGraphDocument graph;
    QString error;
    if(!graph.initializePreview(1, 1, layers, &error))
        return QByteArray();
    return graph.serialize(&error);
}
}

class EnduranceDocumentTests : public QObject
{
    Q_OBJECT

private slots:
    void terrainTileInformationPersistsWithoutBlinking();
    void persistentPaintOperationSurvivesHistoryNavigation();
    void tileBrushFirstValidSampleOwnsUndoStroke();
    void tileBrushViewportReleasesPolicyAfterFinishAndCancel();
    void persistentPaintOperationSurvivesDocumentTransition();
    void landscapeLayerChangesResetBrushSelection();
    void landscapeWheelDirectionUsesAvailableDelta();
    void sculptCadenceIsIndependentOfSpatialInterpolation();
    void portableDiagnosticCanReplaceExistingReportInPlace();
    void terrainDiagnosticIdentityIsAuthoritativeAndShared();
    void paintIssueReportCapturesEveryAvailableLayer();
    void paintIssueMarkingKeepsCameraNavigation();
    void partialFinalAtlasPageIsAValidLiveBaseline();
    void modernSaveRoutesThroughPublicationExactlyOnce();
    void unchangedMobIsByteIdentical();
    void authoredZeroNidCanOpenAndSaveLosslessly();
    void mobGraphRemovalPreservesAllOtherTopLevelBytes();
    void mobGraphReplacementAndInsertionAreExact();
    void mobGraphOwnershipReplacementClosesRaceCheckBeforeCommit();
    void aiGraphPersistenceUsesExactMprBasenameAndReportsShadowing();
    void aiGraphPersistenceConfirmationNamesSourceAndNonUndoableSave();
    void aiGraphPreviewSelectionNamesActualSource();
    void aiGraphPreviewSelectsExactExternalUnlessPrimaryEmbeddedShadowsIt();
    void aiGraphLoadedSourceStopsRegeneratedLifecycle();
    void aiGraphUndoInvalidationUsesNarrowInputPolicy();
    void aiGraphResetIdsInvalidatesStaticObjectGrouping();
    void aiGraphFileCommitIsValidatedBackedUpAndAtomic();
    void aiGraphOwnershipSwitchValidationNeverRewritesExternalFile();
    void existingGraphComponentReuseIsWarningNotBlocker();
    void generatedGraphComponentReuseIsRecoloredAutomatically();
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
    void ownedRootProjectionAddsDiplomacyAndScriptLosslessly();
    void ownedRootProjectionRejectsUnknownStructuralEdit();
    void transactionRejectsStaleProvenance();
    void cp1251EncodingIsStrict();
    void unchangedMprIsByteIdentical();
    void mprCanvasResizeCropsAndRenamesRetainedSectorLosslessly();
    void mprCanvasResizeExpandsBoundaryWithRuntimeReachableMembers();
    void mprCanvasResizeSupportsTileOffsetsWithoutSeams();
    void mprCanvasResizeClosedEdgeTapersAndDeactivatesOverlay();
    void mprCanvasResizeAcceptsStructural32By32Ceiling();
    void mprCanvasResizeRejectsInvalidOrEmptyIntersection();
    void mprCanvasResizeCommitIsSaveAsOnlyAndAtomic();
    void newMprFromTemplateIsDeterministicSeamSafeAndRuntimeReachable();
    void newMprRejectsInventedNamespaceAndInvalidDimensions();
    void generatedMprCommitIsValidatedBackedUpAndAtomic();
    void mprRecoveryAutosaveUsesValidatedCandidateAndLeavesSourceUntouched();
    void syntheticMprPreservesArchiveNamespaceAndInactiveSentinel();
    void mprSectorPatchIsProvenanceCheckedAndLocal();
    void mprMaxZPatchIsProvenanceCheckedAndLocal();
    void mprMaterialRecordsBindEveryStoredFieldExactly();
    void mprMaterialAndAnimationEditIsLosslessAndSlotStable();
    void mprMaterialAndAnimationEditRejectsInvalidValues();
    void mprVerticalScaleTransactionReopensWithDerivedNormals();
    void mprLengthChangingTransactionsRelocateAndStaleFailClosed();
    void failedStagedMprSaveLeavesNoDestination();
    void stagedMprTransactionProvesExactSemanticScopeWithoutDestination();
    void zeroChangeModernDraftUsesValidatedOriginalMpr();
    void mprSaveCoordinatorBacksUpAndAtomicallyCommits();
    void mprSaveCoordinatorCommitsLengthChangingOverlay();
    void mprSaveCoordinatorReplacesSourceAndRebasesTerrain();
    void mprSaveCoordinatorRejectsSourceRace();
    void mprSaveCoordinatorFailuresLeaveDestinationUntouched();
    void completeMprCandidateCommitsWithBackupAndReopens();
    void privateMprSaveCoordinatorCorpusUsesCopiesOnly();
    void unchangedExistingFileComparisonIsReadOnly();
    void canonicalTerrainSeamsAreEditedAndUndoneTogether();
    void terrainBrushToolsAreDeterministicAndFailClosed();
    void terrainAndOverlayNoiseBrushesAreSeededAndUndoable();
    void terrainHeightmapImportIsDeterministicAndSeamAware();
    void globalTerrainHeightOperationsKeepDimensionsAndSeams();
    void terrainVerticalScalePoliciesAreUndoableAndExplicit();
    void globalTerrainHeightOperationsRejectInvalidRanges();
    void terrainDiagnosticsArePreApplyAndPolicyExplicit();
    void appliedTerrainDiagnosticsMatchPreApplyAndKeepProjectionLocal();
    void terrainSurfaceSamplingMatchesRuntimeCellSelection();
    void terrainTilePaintingIsUndoableLocalizedAndNamespaceSafe();
    void manualTilePaintRebasesDecodedSemanticsAndInvalidatesDisputes();
    void manualTilePaintOnLegacyMapStaysPhysicalUntilModernStarts();
    void firstModernStrokeOwnsFieldInitializationAndManualUndo();
    void refusedFirstModernStrokeLeavesFieldlessStateUntouched();
    void firstResolvePreservesFreshManualTileSemanticCorners();
    void laterManualTileWinsSharedCornersAndUndoRestoresPriorValues();
    void manualTileStrokeStoresOnlyTouchedSemanticVertices();
    void terrainSemanticFieldRoundTripsWithoutPlacementHistory();
    void terrainSemanticFieldRejectsWrongMprOrAtlasBinding();
    void mapReferencedUnassignedAtlasSlotsExcludeUnusedAtlasCapacity();
    void restoredTerrainFamiliesAreMarkedUsedBeforeFirstPaint();
    void terrainSaveRouteDoesNotHashArtworkPerMapCell();
    void terrainTileStrokeCompositionKeepsFirstAndFinalValues();
    void interruptedTerrainStrokeRejectsUnownedStamp();
    void workingAtlasPublicationPreservesActiveStrokeTransaction();
    void terrainStrokePublishesAtlasAndSectorAsOneFrame();
    void resolveUsesPreRouteBoundedPathForGroundAndOverlay();
    void workingAtlasPreviewPublishesOnlyChangedPhysicalTiles();
    void resolveMissingBlendsStagesDeterministicLoadBaseline();
    void terrainHeightStrokeCompositionKeepsFirstAndFinalValues();
    void zone1NamespaceTemplateSeparatesLandOverlayAndVariants();
    void semanticAtlasProposalIsDeterministicAndApprovalGated();
    void semanticAtlasTransitionsCannotTransitivelyMergeMaterialAnchors();
    void semanticAtlasInferenceAssignsExactRepeatedPhysicalSlots();
    void semanticAtlasPerceptualFamiliesSeparateHueAndWhiteTexture();
    void semanticAtlasInferenceUsesRendererOrientationAndPreservesCornerFamilies();
    void semanticAtlasTransitionsCrossTileTypeMetadata();
    void authoredAtlasProfileMatchesZone1Catalog();
    void authoredPresetCatalogCoversKnownAtlasesAndMatchesZone1();
    void authoredPresetCatalogAllEntriesCompile();
    void globalMaterialVariantsUseOnlyUniformAnchorsAndShareThemeGroups();
    void globalMaterialBrushSourceIsPayloadCoherentAndPrefersLocalUniform();
    void globalTerrainSourcesUseOnlyApprovedHdBindings();
    void globalTerrainHydrationRequiresExactPresetAtlasBinding();
    void directTerrainTileDecoderReadsOnlyRequestedTileBand();
    void stockInputsCannotOverrideCanonicalOneCornerMask();
    void workingAtlasRealizesExactTransitionsBeyond512Slots();
    void globalAuthoredTransitionLibraryIndexesRealStockBlends();
    void multiRowTerrainAtlasKeepsAuthoredPagesInPlace();
    void zone2TransitionOnlyGrassMapsToGlobalUniformFamily();
    void authoredPresetSeparatesZone18ReviewedLayers();
    void materialAtlasStoreLoadsOnlyExplicitLocalOverrides();
    void portableMprProfilesDoNotCollideForSharedResourceIdentity();
    void finalizedProfileBindsPostDxtPhysicalPagesAndPadding();
    void workingAtlasProfileBindsExtensionSignaturesForDraftReopen();
    void embeddedTerrainAtlasProfileRoundTripsInsidePublishedRes();
    void prevalidatedEmbeddedAtlasBindingMatchesOnlyExactLoadedAtlas();
    void indexedTerrainArchiveMemberReadSkipsUnrelatedPayloads();
    void terrainAtlasRecoveryStoreDeduplicatesExactPayloads();
    void publishedAtlasArchiveBindingSelectsCoherentProfileSource();
    void externalPublishedAtlasArchiveBindingProbe();
    void installedHdAtlasAllowlistProbe();
    void externalLegacyAdjacentAtlasProfileCollisionProbe();
    void externalReopenedDraftTopologyProbe();
    void externalModernDraftAdoptionPairProbe();
    void externalOverlayAnimationMappingProbe();
    void externalOverlaySurfaceMappingProbe();
    void externalZone11OverlayRegridProbe();
    void externalZone1TestoOverlayMappingProbe();
    void externalZone1TestOverlays3MappingProbe();
    void externalZone1TestOverlays4NoAnimMappingProbe();
    void externalZone1TestOverlays5RuntimeFlipbookProbe();
    void externalZone1TestOverlays6DraftFinalizationProbe();
    void modernDraftAdoptionStagesPairBeforeLiveMutation();
    void semanticAtlasProfileRejectsBindingMismatchAndAmbiguity();
    void semanticAtlasReviewEditsAndStoreAreDeterministic();
    void semanticAtlasPartialCatalogRejectsCrossLayerCornersInline();
    void cornerOnlyMaterialPaintUsesAuthoredContextAndStaysLocal();
    void vertexMaterialBrushPaintsCircleAndConstrainsCornerOnlyFamilies();
    void semanticVertexPaintNeverUsesUniformForBlendFallback();
    void classicAutoStrokeUsesAuthoredResolverAndRerollsExactVariants();
    void classicAutoLivePreviewUsesCommittedPackedOutcome();
    void abandonedTerrainPreviewRollsBackBeforeNextStroke();
    void classicAutoSafeTileSkipsUnsupportedCoreInsteadOfHardSeam();
    void manualPaintShowsCellFootprintAndLiftedTilePreview();
    void zone1NamespaceTemplateMatchesPrivateFixture();
    void zone1R16SquareOverlayBrushCoversDisplayedCells();
    void modernSquareTileStampResolvesAlreadySemanticCoreCells();
    void terrainHeightTransactionReopensAndPreservesOverlay();
    void overlayActivationDeactivationIsLosslessAndUndoable();
    void overlayActivationDistributesUniformVariantsAndRotations();
    void modernUniformOnlyPaintNeverSelectsBlendTiles();
    void overlayActivationMixedBrushPaintsSelectedTileAndMaterial();
    void overlaySculptChangesOnlyActiveOverlayGeometry();
    void overlaySculptSynchronizesSectorSeam();
    void conflictingTerrainSeamIsPreservedAndNotEditable();
    void deferredLandscapeVisibilityStopsBeforeViewTeardown();
    void floatingResourceBrowserClosesBeforeViewTeardown();
    void terrainCatalogWaitsForVisibleLandscapeFrame();
    void malformedMprTableIsRejected();
    void stagedMobIsReopenedWithoutCreatingDestination();
    void emptyGeneratedMobCanAcceptFirstRawObject();
    void generatedMobCommitIsValidatedBackedUpAndAtomic();
};

void EnduranceDocumentTests::terrainTileInformationPersistsWithoutBlinking()
{
    CTerrainTileInformationPolicy policy;
    QCOMPARE(policy.rightPress(QPoint(12, 8)),
             ETerrainTileInformationAction::Show);
    QCOMPARE(policy.mouseMove(QPoint(12, 8), true),
             ETerrainTileInformationAction::None);
    QCOMPARE(policy.mouseMove(QPoint(13, 8), true),
             ETerrainTileInformationAction::Show);
    QCOMPARE(policy.cell(), QPoint(13, 8));
    QCOMPARE(policy.mouseMove(QPoint(13, 8), true),
             ETerrainTileInformationAction::None);
    QCOMPARE(policy.rightRelease(), ETerrainTileInformationAction::None);
    QVERIFY(policy.visible());
    QVERIFY(!policy.rightHeld());
    QCOMPARE(policy.cell(), QPoint(13, 8));
    QCOMPARE(policy.mouseMove(QPoint(14, 8), false),
             ETerrainTileInformationAction::None);
    QVERIFY(policy.visible());
    QCOMPARE(policy.cancel(), ETerrainTileInformationAction::Hide);
    QVERIFY(!policy.visible());
}

void EnduranceDocumentTests::portableDiagnosticCanReplaceExistingReportInPlace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath("paint-issues.json");
    QString error;
    QVERIFY2(ei::writePortableDiagnostic(path, QByteArray("first"), &error),
             qPrintable(error));
    const QDateTime firstModified = QFileInfo(path).lastModified();
    QTest::qWait(20);
    QVERIFY2(ei::writePortableDiagnostic(path, QByteArray("second-report"), &error),
             qPrintable(error));
    QFile report(path);
    QVERIFY(report.open(QIODevice::ReadOnly));
    QCOMPARE(report.readAll(), QByteArray("second-report"));
    QVERIFY2(QFileInfo(path).lastModified() > firstModified,
             "Replacing changed diagnostic content did not update its timestamp");
}

void EnduranceDocumentTests::terrainDiagnosticIdentityIsAuthoritativeAndShared()
{
    STerrainWorkingTileIdentity physical;
    physical.origin = ETerrainWorkingTileOrigin::ImportedAuthored;
    physical.workingSlot = 47;
    physical.sourceSlot = 3;
    physical.sourceSlotAliases = {3, 9};
    physical.sourceNamespace = QStringLiteral("global-source");
    physical.physicalPixelSha256 = QByteArray("physical-hash");
    physical.runtimeTerrainType = 6;
    physical.canonicalSignature.northWest = 11;
    physical.canonicalSignature.northEast = 12;
    physical.canonicalSignature.southEast = 13;
    physical.canonicalSignature.southWest = 14;

    const QJsonObject exported = terrainWorkingPhysicalIdentityJson(
        physical, QStringLiteral("current-map"), {}, {}, {});
    const QJsonObject issueReport = terrainWorkingPhysicalIdentityJson(
        physical, QStringLiteral("current-map"), {}, {}, {});
    QCOMPARE(exported, issueReport);
    QCOMPARE(exported.value(QStringLiteral("origin")).toString(),
             QStringLiteral("authored-global"));
    QCOMPARE(exported.value(QStringLiteral("workingSlot")).toInt(), 47);
    QCOMPARE(exported.value(QStringLiteral("sourceNamespace")).toString(),
             QStringLiteral("global-source"));
    QCOMPARE(exported.value(QStringLiteral("sourceSlot")).toInt(), 3);
    const QJsonArray aliases =
        exported.value(QStringLiteral("sourceAliases")).toArray();
    QCOMPARE(aliases.size(), 2);
    QCOMPARE(aliases.at(1).toObject()
                 .value(QStringLiteral("sourceSlot")).toInt(), 9);
    QVERIFY2(exported.value(QStringLiteral("sourceSlot")).toInt()
                 != exported.value(QStringLiteral("workingSlot")).toInt(),
             "Source identity was inferred from the recyclable working slot");
    QCOMPARE(exported.value(QStringLiteral("runtimeTerrainType")).toInt(), 6);
    QCOMPARE(exported.value(QStringLiteral("canonicalGridCorners")).toObject()
                 .value(QStringLiteral("x0y1")).toInt(), 14);

    STerrainWorkingTileIdentity generated = physical;
    generated.origin = ETerrainWorkingTileOrigin::CanonicalGenerated;
    generated.sourceSlot = -1;
    generated.sourceSlotAliases.clear();
    generated.sourceNamespace.clear();
    generated.canonicalGeneratedBlend = true;
    STerrainGlobalMaterialFamily family;
    family.id = 11;
    family.name = QStringLiteral("Grass / Dark");
    STerrainGlobalMaterialSource source;
    source.mprNamespace = QStringLiteral("zone-source");
    source.uniformSlot = 18;
    source.atlasIdentity = QByteArray::fromHex("aabb");
    const QJsonObject generatedJson = terrainWorkingPhysicalIdentityJson(
        generated, QStringLiteral("current-map"), {family}, {{11, source}},
        {{11, QByteArray("uniform-pixel-hash")}});
    QCOMPARE(generatedJson.value(QStringLiteral("origin")).toString(),
             QStringLiteral("canonical-generated"));
    const QJsonArray inputs = generatedJson
        .value(QStringLiteral("inputUniformSources")).toArray();
    QCOMPARE(inputs.size(), 1);
    QCOMPARE(inputs.at(0).toObject()
                 .value(QStringLiteral("sourceSlot")).toInt(), 18);
    QCOMPARE(inputs.at(0).toObject()
                 .value(QStringLiteral("physicalPixelSha256")).toString(),
             QStringLiteral("uniform-pixel-hash"));
}

void EnduranceDocumentTests::persistentPaintOperationSurvivesHistoryNavigation()
{
    QVERIFY(operationAllowsHistoryNavigation(eOperationMethodSelect));
    QVERIFY(operationAllowsHistoryNavigation(eOperationMethodTileBrush));
    QVERIFY(!operationAllowsHistoryNavigation(eOperationMethodMoveAxis));
    QVERIFY(!operationAllowsHistoryNavigation(eOperationMethodRotateAxis));
    QVERIFY(!operationAllowsHistoryNavigation(eOperationMethodScaleAxis));
}

void EnduranceDocumentTests::tileBrushFirstValidSampleOwnsUndoStroke()
{
    CTileBrushStrokePolicy policy;
    QVERIFY(!policy.ensureForValidSample(false, true));
    QVERIFY(!policy.isActive());

    // Regression: the press misses empty overlay space, then a move reaches an
    // existing surface. That first valid move must open the transaction.
    QVERIFY(policy.ensureForValidSample(true, true));
    QVERIFY(policy.isActive());
    QVERIFY(!policy.ensureForValidSample(true, true));
    QVERIFY(policy.finish());
    QVERIFY(!policy.isActive());

    // A valid release after a missed press is also a complete one-sample
    // stroke. With no history owner, no mutation may start.
    QVERIFY(policy.ensureForValidSample(true, true));
    QVERIFY(policy.finish());
    QVERIFY(!policy.ensureForValidSample(true, false));
    QVERIFY(!policy.isActive());
}

void EnduranceDocumentTests::tileBrushViewportReleasesPolicyAfterFinishAndCancel()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");

    const int finishStart = source.indexOf("void CView::endTileBrushGroup()");
    const int cancelStart = source.indexOf("void CView::cancelTileBrushGroup()",
                                           finishStart);
    const int cancelEnd = source.indexOf("void CView::", cancelStart + 1);
    QVERIFY2(finishStart >= 0 && cancelStart > finishStart
                 && cancelEnd > cancelStart,
             "Terrain stroke finish/cancel implementations were not found");

    const QByteArray finish = source.mid(finishStart,
                                          cancelStart - finishStart);
    const QByteArray cancel = source.mid(cancelStart,
                                          cancelEnd - cancelStart);
    const int finishPolicy = finish.indexOf(
        "m_tileBrushStrokePolicy.finish()");
    const int finishResolution = finish.indexOf(
        "m_tileStrokeFinishing = true;");
    const int finishUndo = finish.lastIndexOf("m_pUndoStack->push(");
    const int finishPalette = finish.indexOf(
        "refreshWorkingTerrainPalettes();", finishUndo);
    QCOMPARE(finish.count("m_tileBrushStrokePolicy.finish()"), 1);
    QVERIFY2(finishResolution >= 0 && finishUndo > finishResolution
                 && finishPolicy > finishUndo
                 && finishPalette > finishPolicy,
             "Completing a viewport tile stroke leaves its policy active and blocks the next stroke");
    const int cancelFinishingGuard = cancel.indexOf(
        "if(m_tileStrokeFinishing)");
    const int cancelRollback = cancel.indexOf(
        "applyTerrainTilePreview(m_tileStrokeCommand, false)");
    const int cancelPolicy = cancel.indexOf(
        "m_tileBrushStrokePolicy.finish()");
    const int cancelPalette = cancel.indexOf(
        "refreshWorkingTerrainPalettes();", cancelPolicy);
    QCOMPARE(cancel.count("m_tileBrushStrokePolicy.finish()"), 1);
    QVERIFY2(cancelFinishingGuard >= 0
                 && cancelRollback > cancelFinishingGuard
                 && cancelPolicy > cancelRollback
                 && cancelPalette > cancelPolicy,
             "Cancelling a viewport tile stroke leaves its policy active and blocks the next stroke");
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

void EnduranceDocumentTests::mobGraphRemovalPreservesAllOtherTopLevelBytes()
{
    const QByteArray root = validMob(section(0xB000, object(42, "bridge")));
    const QByteArray graph = section(0x31415926, QByteArray(64, '\x5a'));
    CMobDocument document;
    CValidationReport loadReport;
    QVERIFY2(document.loadBytes(root + graph, "graph-source.mob", &loadReport),
             qPrintable(loadReport.toText()));
    QVERIFY(document.hasEmbeddedAiGraph());
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(document.removeEmbeddedAiGraph(&candidate, &report),
             qPrintable(report.toText()));
    QCOMPARE(candidate, root);
    CMobDocument reopened;
    QVERIFY(reopened.loadBytes(candidate, "graphless-result.mob", &report));
    QVERIFY(!reopened.hasEmbeddedAiGraph());
    QVERIFY(!reopened.hasSection(0x1E00)); // VSS_SECTION is deliberately absent.
}

void EnduranceDocumentTests::mobGraphReplacementAndInsertionAreExact()
{
    const QByteArray root = validMob(section(0xB000, object(42, "bridge")));
    const QByteArray oldGraph(64, '\x2a');
    const QByteArray newGraph(96, '\x5b');

    CMobDocument withGraph;
    CValidationReport report;
    QVERIFY2(withGraph.loadBytes(root + section(0x31415926, oldGraph),
                                 "graph-source.mob", &report),
             qPrintable(report.toText()));
    QByteArray replaced;
    QVERIFY2(withGraph.replaceEmbeddedAiGraph(newGraph, &replaced, &report),
             qPrintable(report.toText()));
    QCOMPARE(replaced, root + section(0x31415926, newGraph));

    CMobDocument reopenedReplacement;
    QVERIFY(reopenedReplacement.loadBytes(replaced, "graph-replaced.mob", &report));
    QByteArray extracted;
    QVERIFY(reopenedReplacement.embeddedAiGraph(&extracted, &report));
    QCOMPARE(extracted, newGraph);

    CMobDocument graphless;
    QVERIFY(graphless.loadBytes(root, "graphless-source.mob", &report));
    QByteArray inserted;
    QVERIFY2(graphless.replaceEmbeddedAiGraph(newGraph, &inserted, &report),
             qPrintable(report.toText()));
    QCOMPARE(inserted, root + section(0x31415926, newGraph));
}

void EnduranceDocumentTests::mobGraphOwnershipReplacementClosesRaceCheckBeforeCommit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray graph = section(0x31415926, QByteArray(64, '\x5a'));
    const QByteArray baseline = validMob(section(0xB000, object(42, "bridge")))
        + graph;

    CMobDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(baseline, "embedded-source.mob", &report),
             qPrintable(report.toText()));
    QByteArray graphless;
    QVERIFY2(document.removeEmbeddedAiGraph(&graphless, &report),
             qPrintable(report.toText()));

    const QString path = directory.filePath(QStringLiteral("zone6.mob"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(baseline), qint64(baseline.size()));
    file.close();

    QString error;
    QVERIFY2(atomicallyReplaceVerifiedFile(QFileInfo(path), baseline, graphless,
                                           &error),
             qPrintable(error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), graphless);
    file.close();

    QVERIFY(!atomicallyReplaceVerifiedFile(QFileInfo(path), baseline, baseline,
                                            &error));
    QVERIFY(error.contains(QStringLiteral("changed"), Qt::CaseInsensitive));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), graphless);
}

void EnduranceDocumentTests::aiGraphPersistenceUsesExactMprBasenameAndReportsShadowing()
{
    const QFileInfo zone6("C:/maps/zone6.mpr");
    const QFileInfo frozen("C:/maps/zone6_2.mpr");
    QCOMPARE(aiGraphRuntimeSiblingPath(zone6), QString("C:/maps/zone6.grp"));
    QCOMPARE(aiGraphRuntimeSiblingPath(frozen), QString("C:/maps/zone6_2.grp"));
    QVERIFY(aiGraphEmbeddedSourceShadowsExternal(true));
    QVERIFY(!aiGraphEmbeddedSourceShadowsExternal(false));

    const QString warning = aiGraphSharedMobWarning(
        QStringLiteral("zone6.mob"),
        QStringList() << "gz6g / zone6" << "gz6f / zone6_2");
    QVERIFY(warning.contains("zone6.mob"));
    QVERIFY(warning.contains("zone6_2"));
    QVERIFY(warning.contains("both", Qt::CaseInsensitive));
}

void EnduranceDocumentTests::aiGraphPersistenceConfirmationNamesSourceAndNonUndoableSave()
{
    const QString source = QStringLiteral("External: zone6.grp");
    const QString notice = aiGraphPersistenceActionNotice(source);
    QVERIFY(notice.contains(source));
    QVERIFY(notice.contains(QStringLiteral("uses exactly this selected graph")));
    QVERIFY(notice.contains(QStringLiteral("save the primary MOB immediately")));
    QVERIFY(notice.contains(QStringLiteral("create a backup")));
    QVERIFY(notice.contains(QStringLiteral("cannot be reversed")));
    QVERIFY(notice.contains(QStringLiteral("Edit > Undo")));
}

void EnduranceDocumentTests::aiGraphPreviewSelectionNamesActualSource()
{
    QCOMPARE(aiGraphPreviewSelectionLabel(
                 true, QStringLiteral("Embedded: zone1.mob")),
             QStringLiteral("Preview graph: Embedded"));
    QCOMPARE(aiGraphPreviewSelectionLabel(
                 true, QStringLiteral("External: zone1.grp")),
             QStringLiteral("Preview graph: External"));
    QCOMPARE(aiGraphPreviewSelectionLabel(
                 false, QStringLiteral("Embedded: zone1.mob")),
             QStringLiteral("Preview graph: Generated"));
}

void EnduranceDocumentTests::aiGraphPreviewSelectsExactExternalUnlessPrimaryEmbeddedShadowsIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray externalGraph = syntheticValidAiGraph(0x31, 3);
    const QByteArray embeddedGraph = syntheticValidAiGraph(0x42, 4);
    QFile external(directory.filePath("zone6_2.grp"));
    QVERIFY(external.open(QIODevice::WriteOnly));
    QCOMPARE(external.write(externalGraph), qint64(externalGraph.size()));
    external.close();
    QFile wrong(directory.filePath("zone6.grp"));
    QVERIFY(wrong.open(QIODevice::WriteOnly));
    QCOMPARE(wrong.write(embeddedGraph), qint64(embeddedGraph.size()));
    wrong.close();

    const QFileInfo mpr(directory.filePath("zone6_2.mpr"));
    const SAiGraphPreviewSource selected = selectAiGraphPreviewSource(
        QByteArray(), QStringLiteral("secondary.mob"), mpr);
    QCOMPARE(selected.bytes, externalGraph);
    QCOMPARE(selected.path, QDir::cleanPath(directory.filePath("zone6_2.grp")));
    QVERIFY(selected.label.contains("zone6_2.grp"));
    QVERIFY(!selected.embedded);

    const SAiGraphPreviewSource shadowed = selectAiGraphPreviewSource(
        embeddedGraph, QStringLiteral("zone6.mob"), mpr);
    QCOMPARE(shadowed.bytes, embeddedGraph);
    QVERIFY(shadowed.embedded);
    QVERIFY(shadowed.path.isEmpty());
    QVERIFY(shadowed.label.contains("zone6.mob"));
}

void EnduranceDocumentTests::aiGraphLoadedSourceStopsRegeneratedLifecycle()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");

    const int widgetStart = source.indexOf("void CView::initAiGraphPreviewWidget()");
    const int widgetEnd = source.indexOf("void CView::syncAiGraphPreviewWidget()",
                                         widgetStart);
    QVERIFY2(widgetStart >= 0 && widgetEnd > widgetStart,
             "AI graph preview widget implementation was not found");
    const QByteArray widget = source.mid(widgetStart, widgetEnd - widgetStart);
    const int loadedBranch = widget.indexOf("m_aiGraphPreviewUsesLoadedSource = true;");
    QVERIFY2(loadedBranch >= 0,
             "Loaded AI graph source-selection branch was not found");
    QVERIFY2(widget.indexOf("stopAiGraphPreviewAutoRegeneration();", loadedBranch) > loadedBranch,
             "Switching back to the loaded graph leaves regenerated-mode work active");
}

void EnduranceDocumentTests::aiGraphUndoInvalidationUsesNarrowInputPolicy()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");

    const int historyStart = source.indexOf(
        "connect(m_pUndoStack, &QUndoStack::indexChanged");
    const int historyEnd = source.indexOf("m_lastUndoStackIndex = index;",
                                          historyStart);
    QVERIFY2(historyStart >= 0 && historyEnd > historyStart,
             "Undo-stack dirty-state callback was not found");
    const QByteArray history = source.mid(historyStart, historyEnd - historyStart);
    QVERIFY2(history.contains("aiGraphUndoCommandInvalidatesPreview(command)"),
             "Undo history still invalidates the AI graph for unrelated commands");
    QVERIFY2(!history.contains(
                 "dynamic_cast<const CTerrainTilePreviewCommand*>(command) == nullptr"),
             "Terrain tile undo/redo is still excluded from AI graph invalidation");
}

void EnduranceDocumentTests::aiGraphResetIdsInvalidatesStaticObjectGrouping()
{
    const QByteArray header = repositorySource(
        QStringLiteral("source/core/undo.h"));
    const QByteArray implementation = repositorySource(
        QStringLiteral("source/core/undo.cpp"));
    QVERIFY2(!header.isEmpty() && !implementation.isEmpty(),
             "Could not locate undo sources from the test worktree");

    const int classStart = header.indexOf("class CResetIdCommand:");
    const int classEnd = header.indexOf("class CBrushTileCommand", classStart);
    QVERIFY2(classStart >= 0 && classEnd > classStart,
             "Reset IDs undo command declaration was not found");
    const QByteArray declaration = header.mid(classStart, classEnd - classStart);
    QVERIFY(declaration.contains("IAiGraphPreviewInvalidatingCommand"));
    QVERIFY(declaration.contains("invalidatesAiGraphPreview() const override"));

    const int methodStart = implementation.indexOf(
        "bool CResetIdCommand::invalidatesAiGraphPreview() const");
    const int methodEnd = implementation.indexOf(
        "void CResetIdCommand::undo()", methodStart);
    QVERIFY2(methodStart >= 0 && methodEnd > methodStart,
             "Reset IDs invalidation implementation was not found");
    const QByteArray method = implementation.mid(methodStart,
                                                  methodEnd - methodStart);
    QVERIFY(method.contains("contributesStaticNavigationCollision"));
    QVERIFY(method.contains("m_reconnectId.contains(node->mapId())"));
    QVERIFY(method.contains("m_reconnectId.key(node->mapId(), 0)"));
}

void EnduranceDocumentTests::aiGraphFileCommitIsValidatedBackedUpAndAtomic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original = syntheticValidAiGraph(0x11, 1);
    const QByteArray replacement = syntheticValidAiGraph(0x22, 2);
    QVERIFY(!original.isEmpty());
    QVERIFY(!replacement.isEmpty());
    QVERIFY(original != replacement);

    const QFileInfo destination(directory.filePath("zone6_2.grp"));
    QFile file(destination.absoluteFilePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), qint64(original.size()));
    file.close();

    SAiGraphFileCommitRequest request;
    request.destination = destination;
    request.graphBytes = replacement;
    request.backupRootOverride = directory.filePath("backups");
    const SAiGraphFileCommitResult committed = commitValidatedAiGraphFile(request);
    QVERIFY2(committed.success, qPrintable(committed.error));
    QVERIFY(!committed.noOp);
    QVERIFY(QFileInfo::exists(committed.backupPath));
    QFile reopened(destination.absoluteFilePath());
    QVERIFY(reopened.open(QIODevice::ReadOnly));
    QCOMPARE(reopened.readAll(), replacement);

    request.graphBytes = original;
    request.simulateAtomicCommitFailure = true;
    const SAiGraphFileCommitResult failed = commitValidatedAiGraphFile(request);
    QVERIFY(!failed.success);
    QCOMPARE(failed.phase, QString("atomic-replacement"));
    QFile unchanged(destination.absoluteFilePath());
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), replacement);

    SAiGraphFileCommitRequest invalid;
    invalid.destination = QFileInfo(directory.filePath("invalid.grp"));
    invalid.graphBytes = QByteArray("not-a-graph");
    const SAiGraphFileCommitResult refused = commitValidatedAiGraphFile(invalid);
    QVERIFY(!refused.success);
    QVERIFY(!invalid.destination.exists());
}

void EnduranceDocumentTests::aiGraphOwnershipSwitchValidationNeverRewritesExternalFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString graphPath = directory.filePath("zone6.grp");
    const QByteArray graphBytes = syntheticValidAiGraph(0x35, 7);
    QFile graph(graphPath);
    QVERIFY(graph.open(QIODevice::WriteOnly));
    QCOMPARE(graph.write(graphBytes), qint64(graphBytes.size()));
    graph.close();

    QString error;
    QVERIFY2(validateExistingAiGraphFile(QFileInfo(graphPath), 1, 1,
                                         nullptr, &error),
             qPrintable(error));
    QVERIFY(graph.open(QIODevice::ReadOnly));
    QCOMPARE(graph.readAll(), graphBytes);
    graph.close();

    const QString missingPath = directory.filePath("missing.grp");
    QVERIFY(!validateExistingAiGraphFile(QFileInfo(missingPath), 1, 1,
                                          nullptr, &error));
    QVERIFY(!QFileInfo::exists(missingPath));
    QVERIFY(graph.open(QIODevice::ReadOnly));
    QCOMPARE(graph.readAll(), graphBytes);
}

void EnduranceDocumentTests::existingGraphComponentReuseIsWarningNotBlocker()
{
    QVector<SAiGraphLayer> layers(CAiGraphDocument::LayerCount);
    for(SAiGraphLayer& layer : layers)
    {
        layer.weights.fill(qint16(-1), 2 * CAiGraphDocument::DirectionCount);
        layer.packedPositions = QByteArray(2, char(0x11));
        layer.components.fill(1, 2);
    }
    CAiGraphDocument graph;
    QString error;
    QVERIFY2(graph.initializePreview(2, 1, layers, &error), qPrintable(error));
    QVERIFY(!graph.validateRuntimeCompatibility(2, 1, &error));
    QVERIFY(error.contains(QStringLiteral("disconnected"), Qt::CaseInsensitive));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("legacy.grp"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray bytes = graph.serialize(&error);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();

    QStringList warnings;
    QVERIFY2(validateExistingAiGraphFile(QFileInfo(path), 2, 1,
                                         &warnings, &error), qPrintable(error));
    QVERIFY(!warnings.isEmpty());
    QVERIFY(warnings.join(QStringLiteral("\n")).contains(
        QStringLiteral("long-distance"), Qt::CaseInsensitive));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void EnduranceDocumentTests::generatedGraphComponentReuseIsRecoloredAutomatically()
{
    QVector<SAiGraphLayer> layers(CAiGraphDocument::LayerCount);
    for(SAiGraphLayer& layer : layers)
    {
        layer.weights.fill(qint16(-1), 2 * CAiGraphDocument::DirectionCount);
        layer.packedPositions = QByteArray(2, char(0x11));
        layer.components.fill(1, 2);
    }
    CAiGraphDocument graph;
    QString error;
    QVERIFY2(graph.initializePreview(2, 1, layers, &error), qPrintable(error));
    QVERIFY(!graph.validateRuntimeCompatibility(2, 1, &error));

    QVERIFY2(graph.recolorConnectedComponents(&error), qPrintable(error));
    QVERIFY2(graph.validateRuntimeCompatibility(2, 1, &error), qPrintable(error));
    QVERIFY(graph.component(0, 0, 0) != graph.component(0, 1, 0));

    const QByteArray bytes = graph.serialize(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    CAiGraphDocument reopened;
    QVERIFY2(reopened.parse(bytes, &error), qPrintable(error));
    QVERIFY2(reopened.validateRuntimeCompatibility(2, 1, &error), qPrintable(error));
}

void EnduranceDocumentTests::paintIssueReportCapturesEveryAvailableLayer()
{
    const STerrainPaintIssueCapturePlan fromLand =
        terrainPaintIssueCapturePlan(ELandscapeEditMode::TilePaint);
    QVERIFY(fromLand.land);
    QVERIFY(fromLand.overlay);

    const STerrainPaintIssueCapturePlan fromOverlay =
        terrainPaintIssueCapturePlan(ELandscapeEditMode::OverlayPaint);
    QVERIFY(fromOverlay.land);
    QVERIFY(fromOverlay.overlay);
}

void EnduranceDocumentTests::paintIssueMarkingKeepsCameraNavigation()
{
    QVERIFY(terrainPaintIssueMarkerConsumesMouseMove(false));
    QVERIFY(!terrainPaintIssueMarkerConsumesMouseMove(true));
}

void EnduranceDocumentTests::partialFinalAtlasPageIsAValidLiveBaseline()
{
    // R125 produced 473 logical tiles in eight physical 64-tile pages.  The
    // remaining 39 images are page padding, not missing MP tile types.
    QVERIFY(terrainAtlasLogicalCountsFitPhysicalPages(8, 473, 512, 473));
    QVERIFY(terrainAtlasLogicalCountsFitPhysicalPages(8, 512, 512, 512));
    QVERIFY(!terrainAtlasLogicalCountsFitPhysicalPages(8, 513, 512, 513));
    QVERIFY(!terrainAtlasLogicalCountsFitPhysicalPages(8, 473, 512, 512));
    QVERIFY(!terrainAtlasLogicalCountsFitPhysicalPages(8, 448, 512, 448));
}

void EnduranceDocumentTests::modernSaveRoutesThroughPublicationExactlyOnce()
{
    QCOMPARE(terrainMprSaveRoute(false, false, false, false, false),
             ETerrainMprSaveRoute::PlainMpr);
    QCOMPARE(terrainMprSaveRoute(true, false, false, false, false),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSaveRoute(false, true, false, false, false),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSaveRoute(false, false, true, false, false),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSaveRoute(false, false, false, true, false),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSaveRoute(false, false, false, false, true),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSaveRoute(false, false, false, false, false, true),
             ETerrainMprSaveRoute::SaveEditableModernDraft);
    QCOMPARE(terrainMprSaveRoute(false, false, false, false, false, true,
                                 true),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSaveRoute(true, false, false, false, false, true),
             ETerrainMprSaveRoute::PublishModernAtlas);
    QCOMPARE(terrainMprSavePresentationFor(
                 ETerrainMprSaveRoute::PlainMpr),
             ETerrainMprSavePresentation::PlainMpr);
    QCOMPARE(terrainMprSavePresentationFor(
                 ETerrainMprSaveRoute::SaveEditableModernDraft),
             ETerrainMprSavePresentation::EditableModernDraft);
    QCOMPARE(terrainMprSavePresentationFor(
                 ETerrainMprSaveRoute::PublishModernAtlas),
             ETerrainMprSavePresentation::PendingModernPublication);
    QVERIFY(terrainPublicationWouldOverwriteWorkingDraft(
        QStringLiteral("C:/Maps/zone1test.mpr"),
        QStringLiteral("C:/Maps/zone1test.mpr"), true));
    QVERIFY(!terrainPublicationWouldOverwriteWorkingDraft(
        QStringLiteral("C:/Maps/zone1test.mpr"),
        QStringLiteral("C:/Maps/zone1.mpr"), true));
    QVERIFY(!terrainPublicationWouldOverwriteWorkingDraft(
        QStringLiteral("C:/Maps/zone1test.mpr"),
        QStringLiteral("C:/Maps/zone1test.mpr"), false));
    QVERIFY(terrainMprUsesAdjacentModernAtlas(
        QStringLiteral("C:/Maps/zone3obr test.mpr"),
        QStringLiteral("C:/Maps/zone3obr test-atlas.res")));
    QVERIFY(!terrainMprUsesAdjacentModernAtlas(
        QStringLiteral("C:/Maps/zone3obr test.mpr"),
        QStringLiteral("C:/Res/textures.res")));
    QVERIFY(terrainSaveAsNeedsModernDraftConfirmation(
        true, true, false));
    QVERIFY(terrainSaveAsNeedsModernDraftConfirmation(
        true, false, true));
    QVERIFY(!terrainSaveAsNeedsModernDraftConfirmation(
        false, true, true));
    QVERIFY(!terrainSaveAsNeedsModernDraftConfirmation(
        true, false, false));

    const QByteArray source = viewSource();
    const QByteArray mainSource = mainWindowSource();
    QVERIFY2(mainSource.contains(
                 "void MainWindow::on_actionSave_landscape_MPR_as_triggered()"),
             "Could not locate the Save MPR As action wiring");
    QVERIFY2(mainSource.contains("m_pView->saveLandAs();"),
             "Save MPR As no longer reaches the distinct destination flow");
    QVERIFY2(mainSource.contains("Save MPR Draft"),
             "An editable draft does not identify ordinary Save as a draft save");
    QVERIFY2(mainSource.contains("Finalize Draft MPR As..."),
             "An editable draft does not identify Save As as publication");
    QVERIFY2(mainSource.contains("Finalize and Save MPR"),
             "A normal MPR with atlas changes does not identify Save as finalization");
    QVERIFY2(mainSource.contains("Finalize MPR As..."),
             "A normal MPR with atlas changes does not identify Save As as finalization");
    const int saveStart = source.indexOf("void CView::saveLand()");
    const int saveEnd = source.indexOf("void CView::saveLandAs()", saveStart);
    QVERIFY(saveStart >= 0 && saveEnd > saveStart);
    const QByteArray save = source.mid(saveStart, saveEnd - saveStart);
    QVERIFY2(save.contains("ETerrainMprSaveRoute::SaveEditableModernDraft"),
             "Ordinary Save no longer preserves a persisted editable draft");
    QVERIFY2(save.contains("m_pLand->filePath(), true, true"),
             "Ordinary Save no longer commits the paired draft atlas");
    const int saveAsStart = source.indexOf("void CView::saveLandAs()");
    const int saveAsEnd = source.indexOf(
        "void CView::saveModernDraftAs()", saveAsStart);
    QVERIFY(saveAsStart >= 0 && saveAsEnd > saveAsStart);
    const QByteArray saveAs = source.mid(
        saveAsStart, saveAsEnd - saveAsStart);
    QVERIFY2(saveAs.contains(
                 "terrainSaveAsNeedsModernDraftConfirmation"),
             "Save As no longer detects a working-atlas pair requirement");
    QVERIFY2(saveAs.contains("Modern Draft pair required")
             && saveAs.contains("Save as Modern Draft")
             && saveAs.contains("Finalize and Save As"),
             "Save As no longer offers both paired-draft and final publication routes");
    QVERIFY2(saveAs.contains("clickedButton() == saveDraft")
             && saveAs.contains("fileName, true, true")
             && saveAs.contains("clickedButton() == finalizeAndSaveAs")
             && saveAs.contains("m_requestedTerrainMprDestination = fileName"),
             "Save As no longer routes the selected destination to the explicit draft/finalize choice");

    const int finalizeStart = source.indexOf(
        "void CView::finalizeVirtualTerrainAtlas()");
    const int finalizeEnd = source.indexOf(
        "void CView::refreshMprSettingsDock()", finalizeStart);
    QVERIFY(finalizeStart >= 0 && finalizeEnd > finalizeStart);
    const QByteArray finalize = source.mid(
        finalizeStart, finalizeEnd - finalizeStart);
    QVERIFY2(finalize.contains(
                 "terrainPublicationWouldOverwriteWorkingDraft("),
             "Publication can again rewrite a draft MPR without its sidecar");
    QVERIFY2(finalize.contains(
                 "const QFileInfo mprDestination = preselectedMprDestination;"),
             "Publication no longer preserves the MPR selected by Save As");
    QVERIFY2(source.contains("Save MPR As selected current:%1 destination:%2"),
             "Save MPR As no longer records its exact source and destination");
    const int publicationPreflight = finalize.indexOf(
        "terrainPublicationPreflightRequested");
    const int publicationAuthorized = finalize.indexOf(
        "Modern terrain publication authorized mpr:%1 res:%2");
    const int publicationRead = finalize.indexOf("QFile sourceTextures(texturesPath)");
    const int publicationCommit = finalize.indexOf(
        "commitStagedResPublishedMembers");
    QVERIFY2(publicationRead >= 0 && publicationPreflight > publicationRead
             && publicationAuthorized > publicationPreflight
             && publicationCommit > publicationAuthorized,
             "Publication does not defer the owned-runtime/watcher boundary until immediately before archive mutation");
    QVERIFY2(finalize.contains("createTexturesBackup ? tr(")
             && finalize.contains(
                 "Backing up and atomically replacing textures.res...")
             && finalize.contains("Atomically replacing textures.res..."),
             "The no-backup path still claims that it is creating a backup");
    QVERIFY2(source.contains("Modern terrain publication succeeded mpr:%1 res:%2"),
             "Publication no longer records the exact committed MPR/RES pair after both commits succeed");
    QVERIFY2(mainSource.contains("Stop Test Current and continue")
             && mainSource.contains("m_mapTestLauncher->stop()")
             && mainSource.contains("m_mapTestLauncher->isRunning()"),
             "The publication preflight no longer offers to stop and waits for the owned test runtime");
    const QByteArray browserSource = repositorySource(
        QStringLiteral("source/ui/resource_browser.cpp"));
    const QByteArray browserHeader = repositorySource(
        QStringLiteral("source/ui/resource_browser.h"));
    QVERIFY2(browserHeader.contains("beginInternalPublication")
             && browserHeader.contains("finishInternalPublication"),
             "Terrain publication cannot suspend and complete the Resource Browser's watcher transaction");
    QVERIFY2(browserSource.contains("m_archiveWatcher->removePath")
             && browserSource.contains("beginLoad(m_loadedSources"),
             "A self-authored RES still prompts as an external change instead of refreshing directly");

    const int prewarmStart = source.indexOf(
        "void CView::prewarmModernTerrainFamily(");
    const int prewarmEnd = source.indexOf(
        "bool CView::adoptPrewarmedTerrainFamily(", prewarmStart);
    QVERIFY(prewarmStart >= 0 && prewarmEnd > prewarmStart);
    const QByteArray prewarm = source.mid(prewarmStart, prewarmEnd - prewarmStart);
    const int candidateLoop = prewarm.indexOf(
        "for(const STerrainGlobalTransitionSource& source :");
    const int exactOnlyGate = prewarm.indexOf("if(!exactAvailable)");
    QVERIFY2(candidateLoop >= 0
             && (exactOnlyGate < 0 || candidateLoop < exactOnlyGate),
             "A previously realized uniform still prevents the rest of its physical family from being staged before blends");

    QVERIFY(terrainMprSaveActionEnabled(true, true, false));
    QVERIFY(terrainMprSaveActionEnabled(true, false, true));
    QVERIFY(!terrainMprSaveActionEnabled(true, false, false));
    QVERIFY(!terrainMprSaveActionEnabled(false, true, true));
    QVERIFY(terrainChangedSaveNeedsWorkingAtlasPackage(
        true, true, false, false));
    QVERIFY(!terrainChangedSaveNeedsWorkingAtlasPackage(
        true, false, false, false));
    QVERIFY(terrainChangedSaveNeedsWorkingAtlasPackage(
        false, false, true, false));
    QVERIFY(terrainChangedSaveNeedsWorkingAtlasPackage(
        false, false, false, true));
    QVERIFY(!terrainChangedSaveNeedsWorkingAtlasPackage(
        false, true, false, false));
    QVERIFY(terrainChangedSaveTransactionIsUsable(
        true, false, true, true, true));
    QVERIFY(!terrainChangedSaveTransactionIsUsable(
        true, false, true, true, false));
    QVERIFY(!terrainChangedSaveTransactionIsUsable(
        true, false, true, false, true));
    QVERIFY(!terrainChangedSaveTransactionIsUsable(
        false, false, true, true, true));
    QVERIFY(!terrainChangedSaveTransactionIsUsable(
        true, true, true, true, true));
    QVERIFY(terrainChangedSaveTransactionIsUsable(
        true, false, false, false, false));

    QVERIFY2(source.contains("Save MPR as modern draft..."),
             "The portable draft action still uses the old ambiguous label");
    QVERIFY2(source.contains(
                 "Create a new content identity (recommended)"),
             "The safest publication identity choice is not identified");
    QVERIFY2(source.contains(
                 "safest export option because it avoids conflicts with existing map atlases"),
             "The new-identity recommendation does not explain its safety benefit");

    // R179: atlas artwork can change while the MPR remains within its
    // original page count. Test Current must package those working bytes based
    // on cell provenance, not on whether another physical page was allocated.
    QVERIFY(terrainMapTestNeedsWorkingAtlasPackage(
        true, true, false, 8, 8));
    QVERIFY(terrainMapTestNeedsWorkingAtlasPackage(
        true, false, true, 8, 8));
    QVERIFY(terrainMapTestNeedsWorkingAtlasPackage(
        true, false, false, 9, 8));
    QVERIFY(!terrainMapTestNeedsWorkingAtlasPackage(
        true, false, false, 8, 8));
    QVERIFY(!terrainMapTestNeedsWorkingAtlasPackage(
        false, true, true, 9, 8));
    const int candidateStart = source.indexOf(
        "bool CView::buildValidatedLandscapeCandidate(");
    const int candidateEnd = source.indexOf(
        "bool CView::saveChangedLandscapeDocument(", candidateStart);
    QVERIFY(candidateStart >= 0 && candidateEnd > candidateStart);
    const QByteArray candidate = source.mid(
        candidateStart, candidateEnd - candidateStart);
    QVERIFY2(candidate.contains("terrainMapTestNeedsWorkingAtlasPackage("),
             "Test Current still decides atlas staging from page growth alone");
    QVERIFY2(candidate.contains("stageWorkingAtlas && !terrainAtlasResource"),
             "An MPR-only candidate can still omit its required unsaved working atlas");
    QVERIFY2(candidate.contains("mpr.test.atlas.destination"),
             "The omitted working-atlas failure is not reported distinctly");

    const STerrainPublicationLayerPlan landOnly =
        terrainPublicationLayerPlan(true, false);
    QVERIFY(landOnly.valid);
    QCOMPARE(landOnly.primary, ETerrainPublicationPrimaryLayer::Land);
    QVERIFY(landOnly.publishLand);
    QVERIFY(!landOnly.publishOverlay);

    const STerrainPublicationLayerPlan overlayOnly =
        terrainPublicationLayerPlan(false, true);
    QVERIFY(overlayOnly.valid);
    // Overlay material fields are sparse wherever no overlay geometry exists.
    // Publication must therefore rebuild the complete Ground field and pin /
    // remap the edited Overlay cells, just like the combined-layer path.
    QCOMPARE(overlayOnly.primary, ETerrainPublicationPrimaryLayer::Land);
    QVERIFY(!overlayOnly.publishLand);
    QVERIFY(overlayOnly.publishOverlay);

    // One Save publishes both modern-painted layers. Ground is the canonical
    // rebuild and the staged Overlay grid is pinned/remapped into the same
    // physical atlas; there is no second destination prompt or second Save.
    const STerrainPublicationLayerPlan combined =
        terrainPublicationLayerPlan(true, true);
    QVERIFY(combined.valid);
    QCOMPARE(combined.primary, ETerrainPublicationPrimaryLayer::Land);
    QVERIFY(combined.publishLand);
    QVERIFY(combined.publishOverlay);

    QTemporaryDir resources;
    QVERIFY(resources.isValid());
    const QString missing = QDir(resources.path()).filePath("missing");
    const QString modRes = QDir(resources.path()).filePath("mod/res");
    QVERIFY(QDir().mkpath(modRes));
    const QString mprPath = QDir(resources.path()).filePath("maps/zone2.mpr");
    const QString target = terrainPublicationInitialResourcePath(
        {missing, modRes}, mprPath);
    QCOMPARE(QDir::cleanPath(target),
             QDir::cleanPath(QDir(modRes).filePath("textures.res")));
    QVERIFY(terrainPublicationPathInResourceRoots(target, {missing, modRes}));
    QVERIFY(!terrainPublicationPathInResourceRoots(
        QDir(resources.path()).filePath("external/textures.res"),
        {missing, modRes}));

    const QString publishedRes = QDir(modRes).filePath("published.res");
    QFile publishedArchive(publishedRes);
    QVERIFY(publishedArchive.open(QIODevice::WriteOnly));
    QCOMPARE(publishedArchive.write("RES", 3), qint64(3));
    publishedArchive.close();
    const STerrainPublicationResourceChoicePlan normalSave =
        terrainPublicationResourceChoicePlan(
            {missing, modRes}, mprPath, publishedRes, true, false);
    QCOMPARE(QDir::cleanPath(normalSave.initialPath),
             QDir::cleanPath(publishedRes));
    // Even an exactly bound published RES is only the chooser suggestion. A
    // modern save must never mutate an archive without explicit confirmation.
    QVERIFY(normalSave.requiresExplicitChoice);

    const QString outsideMaps = QDir(resources.path()).filePath("maps");
    QVERIFY(QDir().mkpath(outsideMaps));
    const QString outsideSidecar = QDir(outsideMaps).filePath(
        "zone1test2-atlas.res");
    QFile outsideArchive(outsideSidecar);
    QVERIFY(outsideArchive.open(QIODevice::WriteOnly));
    QCOMPARE(outsideArchive.write("RES", 3), qint64(3));
    outsideArchive.close();
    const STerrainPublicationResourceChoicePlan recoveredPublishedSave =
        terrainPublicationResourceChoicePlan(
            {missing, modRes}, mprPath, outsideSidecar, true, false);
    QVERIFY(recoveredPublishedSave.requiresExplicitChoice);
    QCOMPARE(QDir::cleanPath(recoveredPublishedSave.initialPath),
             QDir::cleanPath(QDir(modRes).filePath("textures.res")));

    const QString draftSidecar = QDir(resources.path()).filePath(
        "maps/zone2 test-atlas.res");
    const STerrainPublicationResourceChoicePlan draftSaveAs =
        terrainPublicationResourceChoicePlan(
            {missing, modRes}, mprPath, draftSidecar, false, true);
    QVERIFY(draftSaveAs.requiresExplicitChoice);
    QCOMPARE(QDir::cleanPath(draftSaveAs.initialPath),
             QDir::cleanPath(QDir(modRes).filePath("textures.res")));

    const QString maps = outsideMaps;
    QVERIFY(QDir().mkpath(maps));
    const QString mapTestMpr = QDir(maps).filePath("zone1.mpr");
    const QString differentlyNamedSidecar = QDir(maps).filePath(
        "zone1test2-atlas.res");
    QFile sidecar(differentlyNamedSidecar);
    QVERIFY(sidecar.open(QIODevice::WriteOnly));
    QCOMPARE(sidecar.write("RES", 3), qint64(3));
    sidecar.close();
    // The editor has already hash-validated this exact active archive. Test
    // Current must use that binding rather than guessing from the MPR basename.
    QCOMPARE(QDir::cleanPath(terrainMapTestResourcePath(
                 differentlyNamedSidecar, mapTestMpr)),
             QDir::cleanPath(differentlyNamedSidecar));

    const QString conventionalSidecar = QDir(maps).filePath("zone1-atlas.res");
    QFile conventional(conventionalSidecar);
    QVERIFY(conventional.open(QIODevice::WriteOnly));
    QCOMPARE(conventional.write("RES", 3), qint64(3));
    conventional.close();
    QCOMPARE(QDir::cleanPath(terrainMapTestResourcePath(QString(), mapTestMpr)),
             QDir::cleanPath(conventionalSidecar));
}

void EnduranceDocumentTests::zeroChangeModernDraftUsesValidatedOriginalMpr()
{
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(),
                                QStringLiteral("zero-change-draft-source.mpr"),
                                &loadReport),
             qPrintable(loadReport.toText()));

    CMprEditTransaction transaction(
        QStringLiteral("export unchanged portable modern draft"));
    QVERIFY(transaction.isEmpty());

    QTemporaryDir destinationDirectory;
    QVERIFY(destinationDirectory.isValid());
    const QFileInfo destination(
        destinationDirectory.filePath(QStringLiteral("portable-draft.mpr")));
    const SStagedMprTransactionValidation staged =
        validateStagedMprTransaction(baseline, transaction, destination);

    QVERIFY2(staged.success, qPrintable(staged.error));
    QCOMPARE(staged.candidateBytes, baseline.originalBytes());
    QVERIFY(staged.changedMembers.isEmpty());
    QVERIFY(staged.semanticDiff.isEmpty());
    QVERIFY(!destination.exists());

    const QByteArray source = viewSource();
    const int saveStart = source.indexOf(
        "bool CView::saveChangedLandscapeDocument(");
    const int saveEnd = source.indexOf(
        "QByteArray CView::terrainPaintIssueReport", saveStart);
    QVERIFY(saveStart >= 0);
    const QByteArray save = source.mid(
        saveStart, saveEnd > saveStart ? saveEnd - saveStart : -1);
    QVERIFY2(save.contains("terrainChangedSaveTransactionIsUsable("),
             "The view still rejects an unchanged draft before staged validation");
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

void EnduranceDocumentTests::authoredZeroNidCanOpenAndSaveLosslessly()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    const QByteArray bytes = validMob(section(0xB000, object(0, "legacy-zero")));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.flush();

    CMobDocument document;
    CValidationReport report;
    QVERIFY(document.load(QFileInfo(file.fileName()), &report));
    QCOMPARE(document.objects().size(), 1);
    QVERIFY(document.objects().first().hasNid);
    QCOMPARE(document.objects().first().nid, quint32(0));
    QCOMPARE(document.serialize(), bytes);
    QVERIFY(!document.isDirty());
    QVERIFY(!report.hasErrors());
    int reservedFindings = 0;
    for(const SValidationFinding& finding : report.findings())
    {
        if(finding.code != "nid.zero_reserved")
            continue;
        ++reservedFindings;
        QCOMPARE(finding.severity, EValidationSeverity::Critical);
        QVERIFY(finding.location.contains("OBJECT"));
        QVERIFY(finding.message.contains("Object \"legacy-zero\""));
        QVERIFY(finding.message.contains("NID 0 is invalid for map objects"));
        QVERIFY(finding.message.contains("may cause the game to crash"));
    }
    QCOMPARE(reservedFindings, 1);
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
        0xB00D, QStringList() << "hd.armor00" << "hd.armor01" << "bd"
                              << "hd.armor00" << "future.extension");
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
    QVERIFY(report.toText().contains("mob.semantic.duplicate_bodypart_token"));
    QCOMPARE(document.objects().size(), 1);
    QCOMPARE(document.objects().first().bodyParts,
             QStringList() << "hd.armor00" << "hd.armor01" << "bd"
                           << "hd.armor00" << "future.extension");
    QCOMPARE(document.serialize(), bytes);
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

void EnduranceDocumentTests::ownedRootProjectionAddsDiplomacyAndScriptLosslessly()
{
    const QByteArray unknownBefore = section(0xDEADBEEF, "opaque-before");
    const QByteArray unknownAfter = section(0xFEEDC0DE, "opaque-after");
    const QByteArray objects = section(0xB000, object(42, "trap"));
    const QByteArray trailingGraph = section(0x31415926,
                                             syntheticValidAiGraph(0x31, 7));
    const QByteArray sourceBytes = validMob(unknownBefore + objects + unknownAfter)
        + trailingGraph;
    const QByteArray cleanBytes = validMob(objects) + trailingGraph;
    const QByteArray diplomacy = section(0xDDDDDDD1,
        section(0xDDDDDDD2, QByteArray(32 * 32 * int(sizeof(quint32)), '\0')) +
        stringArray(0xDDDDDDD3, QStringList() << "Neutral"));
    const QByteArray script = encryptedScript("function main() end");
    const QByteArray editedBytes = validMob(script + diplomacy + objects) + trailingGraph;

    CMobEditTransaction transaction;
    QByteArray candidate;
    QString saveClass;
    CValidationReport report;
    QVERIFY2(CMobDocument::buildOwnedRootProjectionCandidate(
        sourceBytes, "owned-root-source.mob", cleanBytes, editedBytes,
        &transaction, &candidate, &saveClass, &report), qPrintable(report.toText()));
    QCOMPARE(saveClass, QString("OWNED_ROOT_STRUCTURE"));
    QCOMPARE(candidate, validMob(unknownBefore + script + diplomacy + objects + unknownAfter)
                            + trailingGraph);

    CMobDocument reopened;
    QVERIFY2(reopened.loadBytes(candidate, "owned-root-candidate.mob", &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.objects().size(), 1);
    QCOMPARE(reopened.objects().front().nid, quint32(42));
}

void EnduranceDocumentTests::ownedRootProjectionRejectsUnknownStructuralEdit()
{
    const QByteArray sourceBytes = validMob(section(0xB000, object(42, "trap")));
    const QByteArray editedBytes = validMob(section(0xCAFEBABE, "invented") +
                                             section(0xB000, object(42, "trap")));
    CMobEditTransaction transaction;
    QByteArray candidate("must-clear");
    QString saveClass;
    CValidationReport report;
    QVERIFY(!CMobDocument::buildOwnedRootProjectionCandidate(
        sourceBytes, "owned-root-source.mob", sourceBytes, editedBytes,
        &transaction, &candidate, &saveClass, &report));
    QVERIFY(candidate.isEmpty());
    QVERIFY(report.hasErrors());
}

void EnduranceDocumentTests::mprCanvasResizeCropsAndRenamesRetainedSectorLosslessly()
{
    const QByteArray left = syntheticLandSector(1200);
    const QByteArray right = syntheticLandSector(42000);
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticTwoSectorMpr(left, right),
                                "crop-source.mpr", &loadReport),
             qPrintable(loadReport.toText()));

    SMprCanvasResizeRequest request;
    request.sectorCountX = 1;
    request.sectorCountY = 1;
    request.oldTileOffsetX = -16;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(baseline.buildResizedCanvas(request, &candidate, &report),
             qPrintable(report.toText()));
    QVERIFY(candidate != baseline.originalBytes());

    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.loadBytes(candidate, "crop-result.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.mapInfo().sectorCountX, quint32(1));
    QCOMPARE(reopened.mapInfo().sectorCountY, quint32(1));
    QCOMPARE(reopened.memberPayload("CaveSet000000.SEC"), right);
    QVERIFY(reopened.memberPayload("CaveSet001000.SEC").isEmpty());
}

void EnduranceDocumentTests::mprCanvasResizeExpandsBoundaryWithRuntimeReachableMembers()
{
    const QByteArray source = syntheticLandSector(17321);
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(source), "expand-source.mpr", &loadReport),
             qPrintable(loadReport.toText()));

    SMprCanvasResizeRequest request;
    request.sectorCountX = 3;
    request.sectorCountY = 2;
    request.oldTileOffsetX = 16;
    request.oldTileOffsetY = 16;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(baseline.buildResizedCanvas(request, &candidate, &report),
             qPrintable(report.toText()));

    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.loadBytes(candidate, "expand-result.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.mapInfo().sectorCountX, quint32(3));
    QCOMPARE(reopened.mapInfo().sectorCountY, quint32(2));
    QCOMPARE(reopened.memberPayload("unknown.bin"), QByteArray("opaque-unknown-payload"));
    QCOMPARE(reopened.memberPayload("CaveSet001001.SEC"), source);
    QCOMPARE(sectorHeight(reopened.memberPayload("CaveSet000000.SEC"), 0, 0),
             quint16(17321));

    for(const SMprArchiveMember& member : reopened.members())
        QCOMPARE(runtimeMprLookup(candidate, member.name), member.payload);
}

void EnduranceDocumentTests::mprCanvasResizeSupportsTileOffsetsWithoutSeams()
{
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticTwoSectorMpr(
                                    syntheticLandSector(1200),
                                    syntheticLandSector(42000)),
                                "tile-offset-source.mpr", &loadReport),
             qPrintable(loadReport.toText()));

    SMprCanvasResizeRequest request;
    request.sectorCountX = 3;
    request.sectorCountY = 1;
    request.oldTileOffsetX = 3;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(baseline.buildResizedCanvas(request, &candidate, &report),
             qPrintable(report.toText()));

    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.loadBytes(candidate, "tile-offset-result.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(sectorHeight(reopened.memberPayload("CaveSet000000.SEC"), 6, 10),
             quint16(1200));
    QCOMPARE(sectorHeight(reopened.memberPayload("CaveSet001000.SEC"), 6, 10),
             quint16(42000));
    QCOMPARE(sectorHeight(reopened.memberPayload("CaveSet000000.SEC"), 32, 10),
             sectorHeight(reopened.memberPayload("CaveSet001000.SEC"), 0, 10));
}

void EnduranceDocumentTests::mprCanvasResizeClosedEdgeTapersAndDeactivatesOverlay()
{
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(), "taper-source.mpr", &loadReport),
             qPrintable(loadReport.toText()));

    SMprCanvasResizeRequest request;
    request.sectorCountX = 1;
    request.sectorCountY = 1;
    request.closeEdges = true;
    request.edgeTaperTiles = 2;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(baseline.buildResizedCanvas(request, &candidate, &report),
             qPrintable(report.toText()));

    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.loadBytes(candidate, "taper-result.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.sectors().size(), 1);
    const SMprSectorInfo& sector = reopened.sectors().first();
    QVERIFY(sector.hasOverlay);
    QCOMPARE(sector.overlayMaterialIndices.at(1), qint16(-1));
    QVERIFY(sector.overlayMaterialIndices.at(5 * 16 + 5) >= 0);
}

void EnduranceDocumentTests::mprCanvasResizeAcceptsStructural32By32Ceiling()
{
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY2(baseline.loadBytes(syntheticMpr(syntheticLandSector(500)),
                                "maximum-source.mpr", &loadReport),
             qPrintable(loadReport.toText()));
    SMprCanvasResizeRequest request;
    request.sectorCountX = 32;
    request.sectorCountY = 32;
    QByteArray candidate;
    CValidationReport report;
    QVERIFY2(baseline.buildResizedCanvas(request, &candidate, &report),
             qPrintable(report.toText()));
    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.loadBytes(candidate, "maximum-result.mpr", &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.sectors().size(), 32 * 32);
    QCOMPARE(reopened.mapInfo().sectorCountX * 8, quint32(256));
    QCOMPARE(reopened.mapInfo().sectorCountY * 8, quint32(256));
}

void EnduranceDocumentTests::mprCanvasResizeRejectsInvalidOrEmptyIntersection()
{
    CMprDocument baseline;
    CValidationReport loadReport;
    QVERIFY(baseline.loadBytes(syntheticMpr(syntheticLandSector(500)),
                               "refusal-source.mpr", &loadReport));
    QByteArray candidate("must-clear");
    CValidationReport tooLargeReport;
    SMprCanvasResizeRequest tooLarge;
    tooLarge.sectorCountX = 33;
    tooLarge.sectorCountY = 1;
    QVERIFY(!baseline.buildResizedCanvas(tooLarge, &candidate, &tooLargeReport));
    QVERIFY(candidate.isEmpty());
    QVERIFY(tooLargeReport.hasErrors());

    CValidationReport emptyReport;
    SMprCanvasResizeRequest empty;
    empty.sectorCountX = 1;
    empty.sectorCountY = 1;
    empty.oldTileOffsetX = 32;
    QVERIFY(!baseline.buildResizedCanvas(empty, &candidate, &emptyReport));
    QVERIFY(candidate.isEmpty());
    QVERIFY(emptyReport.hasErrors());
}

void EnduranceDocumentTests::mprCanvasResizeCommitIsSaveAsOnlyAndAtomic()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QFileInfo source(root.filePath("source.mpr"));
    QFile sourceFile(source.absoluteFilePath());
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    const QByteArray sourceBytes = syntheticMpr(syntheticLandSector(1000));
    QCOMPARE(sourceFile.write(sourceBytes), qint64(sourceBytes.size()));
    sourceFile.close();
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY(baseline.load(source, &report));

    SMprCanvasResizeRequest resize;
    resize.sectorCountX = 2;
    resize.sectorCountY = 1;
    SMprSaveCommitRequest overwriteSource;
    overwriteSource.source = source;
    overwriteSource.destination = source;
    overwriteSource.backupRootOverride = root.filePath("backups");
    const SMprSaveCommitResult refused = commitStagedMprCanvasResize(
        baseline, baseline, resize, overwriteSource);
    QVERIFY(!refused.success);
    QFile unchanged(source.absoluteFilePath());
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), sourceBytes);

    const QFileInfo destination(root.filePath("resized.mpr"));
    SMprSaveCommitRequest commit;
    commit.source = source;
    commit.destination = destination;
    commit.backupRootOverride = root.filePath("backups");
    const SMprSaveCommitResult result = commitStagedMprCanvasResize(
        baseline, baseline, resize, commit);
    QVERIFY2(result.success, qPrintable(result.error));
    QVERIFY(destination.exists());
    CMprDocument reopened;
    CValidationReport reopenReport;
    QVERIFY2(reopened.load(destination, &reopenReport), qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.mapInfo().sectorCountX, quint32(2));
    QCOMPARE(reopened.mapInfo().sectorCountY, quint32(1));
}

void EnduranceDocumentTests::newMprFromTemplateIsDeterministicSeamSafeAndRuntimeReachable()
{
    CMprDocument terrainTemplate;
    CValidationReport templateReport;
    QVERIFY2(terrainTemplate.loadBytes(syntheticMpr(), "template.mpr", &templateReport),
             qPrintable(templateReport.toText()));
    SMprCreationRequest request;
    request.innerNamespace = QStringLiteral("NewLand");
    request.sectorCountX = 2;
    request.sectorCountY = 2;
    request.maxZ = 250.0f;
    request.flatRawHeight = 23456;
    SMprPackedTile initialTile;
    initialTile.tileIndex = 1;
    initialTile.atlasIndex = 0;
    initialTile.rotation = 2;
    request.initialPackedTile = initialTile.encode();
    request.closeEdges = true;
    request.edgeTaperTiles = 2;
    QByteArray first;
    QByteArray second;
    CValidationReport firstReport;
    CValidationReport secondReport;
    QVERIFY2(terrainTemplate.buildNewCanvas(request, &first, &firstReport),
             qPrintable(firstReport.toText()));
    QVERIFY2(terrainTemplate.buildNewCanvas(request, &second, &secondReport),
             qPrintable(secondReport.toText()));
    QCOMPARE(first, second);

    CMprDocument created;
    CValidationReport createdReport;
    QVERIFY2(created.loadBytes(first, "created.mpr", &createdReport),
             qPrintable(createdReport.toText()));
    QCOMPARE(created.mapInfo().innerNamespace, QStringLiteral("NewLand"));
    QCOMPARE(created.mapInfo().sectorCountX, quint32(2));
    QCOMPARE(created.mapInfo().sectorCountY, quint32(2));
    QCOMPARE(created.mapInfo().maxZ, 250.0f);
    QCOMPARE(created.mapInfo().textureCount, terrainTemplate.mapInfo().textureCount);
    QCOMPARE(created.mapInfo().materialCount, terrainTemplate.mapInfo().materialCount);
    QCOMPARE(created.mapInfo().tileTypes, terrainTemplate.mapInfo().tileTypes);
    QCOMPARE(created.sectors().size(), 4);
    CTerrainEditModel terrain;
    CValidationReport terrainReport;
    QVERIFY2(terrain.load(created, &terrainReport), qPrintable(terrainReport.toText()));
    QVERIFY(!terrain.hasSeamConflicts());
    QCOMPARE(sectorHeight(created.memberPayload("NewLand000000.SEC"), 32, 20),
             sectorHeight(created.memberPayload("NewLand001000.SEC"), 0, 20));
    QVERIFY(terrain.height(0, 0) < request.flatRawHeight);
    QCOMPARE(terrain.height(32, 32), request.flatRawHeight);
    for(const SMprArchiveMember& member : created.members())
        QCOMPARE(runtimeMprLookup(first, member.name), member.payload);

    SMprCreationRequest heightmapRequest = request;
    heightmapRequest.closeEdges = false;
    heightmapRequest.heightmapWidth = 2;
    heightmapRequest.heightmapHeight = 2;
    heightmapRequest.heightmapSamples = {0.0f, 1.0f, 0.25f, 0.75f};
    heightmapRequest.heightmapMinimumRaw = 1000.0f;
    heightmapRequest.heightmapMaximumRaw = 5000.0f;
    QByteArray heightmapped;
    CValidationReport heightmapReport;
    QVERIFY2(terrainTemplate.buildNewCanvas(heightmapRequest, &heightmapped,
                                             &heightmapReport),
             qPrintable(heightmapReport.toText()));
    CMprDocument heightmappedDocument;
    QVERIFY(heightmappedDocument.loadBytes(heightmapped, "heightmapped.mpr",
                                            &heightmapReport));
    CTerrainEditModel heightmappedTerrain;
    QVERIFY(heightmappedTerrain.load(heightmappedDocument, &heightmapReport));
    QCOMPARE(heightmappedTerrain.height(0, 0), quint16(1000));
    QCOMPARE(heightmappedTerrain.height(64, 0), quint16(5000));
    QCOMPARE(heightmappedTerrain.height(0, 64), quint16(2000));
    QCOMPARE(heightmappedTerrain.height(64, 64), quint16(4000));
    QVERIFY(!heightmappedTerrain.hasSeamConflicts());

    heightmapRequest.heightmapMinimumRaw = 5000.0f;
    heightmapRequest.heightmapMaximumRaw = 1000.0f;
    QByteArray invertedHeightmapped;
    CValidationReport invertedHeightmapReport;
    QVERIFY2(terrainTemplate.buildNewCanvas(heightmapRequest,
                                             &invertedHeightmapped,
                                             &invertedHeightmapReport),
             qPrintable(invertedHeightmapReport.toText()));
    CMprDocument invertedHeightmappedDocument;
    QVERIFY(invertedHeightmappedDocument.loadBytes(invertedHeightmapped,
        "inverted-heightmapped.mpr", &invertedHeightmapReport));
    CTerrainEditModel invertedHeightmappedTerrain;
    QVERIFY(invertedHeightmappedTerrain.load(invertedHeightmappedDocument,
                                              &invertedHeightmapReport));
    QCOMPARE(invertedHeightmappedTerrain.height(0, 0), quint16(5000));
    QCOMPARE(invertedHeightmappedTerrain.height(64, 0), quint16(1000));
    QCOMPARE(invertedHeightmappedTerrain.height(0, 64), quint16(4000));
    QCOMPARE(invertedHeightmappedTerrain.height(64, 64), quint16(2000));
    QVERIFY(!invertedHeightmappedTerrain.hasSeamConflicts());

    SMprCreationRequest overlayRequest = request;
    overlayRequest.closeEdges = false;
    overlayRequest.createOverlayPlane = true;
    overlayRequest.overlayRawHeight = 42000;
    overlayRequest.overlayPackedTile = initialTile.encode();
    overlayRequest.overlayMaterialIndex = 1;
    overlayRequest.taperOverlayEdges = true;
    overlayRequest.overlayEdgeTaperTiles = 2;
    QByteArray overlaid;
    CValidationReport overlayReport;
    QVERIFY2(terrainTemplate.buildNewCanvas(overlayRequest, &overlaid,
                                             &overlayReport),
             qPrintable(overlayReport.toText()));
    CMprDocument overlaidDocument;
    QVERIFY2(overlaidDocument.loadBytes(overlaid, "overlaid.mpr", &overlayReport),
             qPrintable(overlayReport.toText()));
    QCOMPARE(overlaidDocument.sectors().size(), 4);
    for(const SMprSectorInfo& sector : overlaidDocument.sectors())
        QVERIFY(sector.hasOverlay);
    CTerrainEditModel overlaidTerrain;
    QVERIFY2(overlaidTerrain.load(overlaidDocument, &overlayReport),
             qPrintable(overlayReport.toText()));
    QVERIFY(!overlaidTerrain.hasSeamConflicts());
    bool overlayAvailable = false;
    QCOMPARE(overlaidTerrain.overlayMaterialIndex(10, 10, &overlayAvailable),
             qint16(1));
    QVERIFY(overlayAvailable);
    QCOMPARE(overlaidTerrain.packedTile(10, 10,
        ETerrainTileLayer::Overlay, &overlayAvailable), initialTile.encode());
    QVERIFY(overlayAvailable);
    QCOMPARE(overlaySectorHeight(
        overlaidDocument.memberPayload("NewLand000000.SEC"), 16, 16),
        quint16(42000));
    QCOMPARE(overlaySectorHeight(
        overlaidDocument.memberPayload("NewLand000000.SEC"), 0, 0),
        quint16(0));
    QCOMPARE(overlaySectorHeight(
        overlaidDocument.memberPayload("NewLand000000.SEC"), 32, 20),
        overlaySectorHeight(
            overlaidDocument.memberPayload("NewLand001000.SEC"), 0, 20));
}

void EnduranceDocumentTests::newMprRejectsInventedNamespaceAndInvalidDimensions()
{
    CMprDocument terrainTemplate;
    CValidationReport templateReport;
    QVERIFY(terrainTemplate.loadBytes(syntheticMpr(), "template.mpr", &templateReport));
    QByteArray candidate("must-clear");
    SMprCreationRequest invalid;
    invalid.innerNamespace = QStringLiteral("bad/name");
    CValidationReport namespaceReport;
    QVERIFY(!terrainTemplate.buildNewCanvas(invalid, &candidate, &namespaceReport));
    QVERIFY(candidate.isEmpty());
    QVERIFY(namespaceReport.hasErrors());
    invalid.innerNamespace = QStringLiteral("valid");
    invalid.sectorCountX = 33;
    CValidationReport dimensionReport;
    QVERIFY(!terrainTemplate.buildNewCanvas(invalid, &candidate, &dimensionReport));
    QVERIFY(candidate.isEmpty());
}

void EnduranceDocumentTests::generatedMprCommitIsValidatedBackedUpAndAtomic()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    CMprDocument terrainTemplate;
    CValidationReport report;
    QVERIFY(terrainTemplate.loadBytes(syntheticMpr(), "template.mpr", &report));
    SMprCreationRequest request;
    request.innerNamespace = QStringLiteral("Created");
    QByteArray candidate;
    QVERIFY2(terrainTemplate.buildNewCanvas(request, &candidate, &report),
             qPrintable(report.toText()));
    const QFileInfo destination(directory.filePath("created.mpr"));
    SMprSaveCommitRequest commit;
    commit.destination = destination;
    commit.backupRootOverride = directory.filePath("backups");
    const SMprSaveCommitResult created = commitGeneratedMpr(candidate, commit);
    QVERIFY2(created.success, qPrintable(created.error));
    QVERIFY(created.backupPath.isEmpty());
    QFile result(destination.absoluteFilePath());
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), candidate);
    result.close();

    QByteArray replacement = candidate;
    replacement.append('x');
    commit.simulateAtomicCommitFailure = true;
    const SMprSaveCommitResult failed = commitGeneratedMpr(replacement, commit);
    QVERIFY(!failed.success);
    QFile unchanged(destination.absoluteFilePath());
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), candidate);
}

void EnduranceDocumentTests::mprRecoveryAutosaveUsesValidatedCandidateAndLeavesSourceUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original = syntheticMpr(syntheticLandSector(1000));
    const QFileInfo source(directory.filePath("source.mpr"));
    QFile sourceFile(source.absoluteFilePath());
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(sourceFile.write(original), qint64(original.size()));
    sourceFile.close();
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY(baseline.load(source, &report));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline, &report));
    const STerrainHeightCommand flatten = terrain.makeGlobalFlatten(2222);
    QVERIFY(terrain.apply(flatten, true, &report, true));
    CMprEditTransaction transaction(QStringLiteral("autosave terrain"));
    QVERIFY(terrain.appendSectorEdits(&transaction, &report));
    QByteArray candidate;
    QVERIFY(baseline.applyTransaction(transaction, &candidate, &report));
    QVERIFY(candidate != original);
    const SBackupResult snapshot = CBackupManager(
        directory.filePath("backups")).snapshotAutosave(source, candidate);
    QVERIFY2(snapshot.success, qPrintable(snapshot.error));
    QFile autosave(snapshot.backupPath);
    QVERIFY(autosave.open(QIODevice::ReadOnly));
    QCOMPARE(autosave.readAll(), candidate);
    QFile untouched(source.absoluteFilePath());
    QVERIFY(untouched.open(QIODevice::ReadOnly));
    QCOMPARE(untouched.readAll(), original);
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

void EnduranceDocumentTests::terrainAndOverlayNoiseBrushesAreSeededAndUndoable()
{
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(2, 1, 100.0f, twoSectorTerrain(30000)));
    STerrainBrush brush;
    brush.centerX = 32.0f;
    brush.centerY = 12.0f;
    brush.radius = 4.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;

    const STerrainHeightCommand first = terrain.makeNoise(
        brush, 600.0f, 0x13572468u, true);
    const STerrainHeightCommand same = terrain.makeNoise(
        brush, 600.0f, 0x13572468u, true);
    const STerrainHeightCommand other = terrain.makeNoise(
        brush, 600.0f, 0x24681357u, true);
    QVERIFY(!first.isEmpty());
    QCOMPARE(first.deltas.size(), same.deltas.size());
    bool raised = false;
    bool lowered = false;
    bool seedChangedPattern = first.deltas.size() != other.deltas.size();
    for(int index = 0; index < first.deltas.size(); ++index)
    {
        QCOMPARE(first.deltas.at(index).globalX, same.deltas.at(index).globalX);
        QCOMPARE(first.deltas.at(index).globalY, same.deltas.at(index).globalY);
        QCOMPARE(first.deltas.at(index).after, same.deltas.at(index).after);
        raised = raised || first.deltas.at(index).after > first.deltas.at(index).before;
        lowered = lowered || first.deltas.at(index).after < first.deltas.at(index).before;
        if(index < other.deltas.size())
            seedChangedPattern = seedChangedPattern
                || first.deltas.at(index).after != other.deltas.at(index).after;
    }
    QVERIFY(raised);
    QVERIFY(lowered);
    QVERIFY(seedChangedPattern);

    const QByteArray before = terrain.sectorPayload(0, 0);
    CTerrainEditHistory history;
    QVERIFY(history.execute(&terrain, first));
    const QByteArray after = terrain.sectorPayload(0, 0);
    QVERIFY(after != before);
    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.sectorPayload(0, 0), before);
    QVERIFY(history.redo(&terrain));
    QCOMPARE(terrain.sectorPayload(0, 0), after);

    const QByteArray landSector = syntheticLandSector(30000);
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(syntheticMpr(landSector),
                                "overlay-noise-baseline.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel overlay;
    QVERIFY(overlay.load(baseline));
    STerrainTileBrush cell;
    cell.centerX = 2.0f;
    cell.centerY = 3.0f;
    cell.radius = 0.0f;
    const SOverlaySurfaceCommand activation = overlay.makeOverlayActivation(
        cell, 1, 0, 1, 0, &report);
    QVERIFY(overlay.apply(activation, true, &report));
    brush.centerX = 5.0f;
    brush.centerY = 7.0f;
    brush.radius = 1.5f;
    const SOverlaySurfaceCommand overlayFirst = overlay.makeOverlayNoise(
        brush, 600.0f, 0x13572468u, &report, true);
    const SOverlaySurfaceCommand overlaySame = overlay.makeOverlayNoise(
        brush, 600.0f, 0x13572468u, &report, true);
    const SOverlaySurfaceCommand overlayOther = overlay.makeOverlayNoise(
        brush, 600.0f, 0x24681357u, &report, true);
    QVERIFY2(!overlayFirst.isEmpty(), qPrintable(report.toText()));
    QCOMPARE(overlayFirst.sectors.front().afterPayload,
             overlaySame.sectors.front().afterPayload);
    QVERIFY(overlayFirst.sectors.front().afterPayload
            != overlayOther.sectors.front().afterPayload);
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

void EnduranceDocumentTests::terrainSurfaceSamplingMatchesRuntimeCellSelection()
{
    CTerrainEditModel flat;
    QVERIFY(flat.loadSources(1, 1, 100.0f,
                             twoSectorTerrain(32767).mid(0, 1)));
    bool available = false;
    QCOMPARE(flat.terrainSurfaceHeight(4.25f, 7.75f, &available), 50.0f);
    QVERIFY(available);
    const float navigationHeight = flat.navigationHeightSample(
        0, 0, ETerrainTileLayer::Land, &available);
    QVERIFY(available);
    QVERIFY(qAbs(navigationHeight - 32767.0f * 100.0f / 65534.0f) < 0.0001f);

    // Runtime graph generation scales each source vertex first, combines the
    // two quarter-weighted terms, and adds the half-weighted term last. These
    // captured boundary cases round in opposite directions if interpolation
    // is performed in raw-height space or in a different addition order.
    const float runtimeMaximum = 50.00000762939453f;
    const auto quantizedNavigationHeight = [runtimeMaximum](float world) {
        const float scaled = world * 511.0f / runtimeMaximum;
        return int(std::nearbyint(double(scaled)));
    };
    QByteArray roundsUpSector = syntheticLandSector(0);
    setSyntheticLandVertex(&roundsUpSector, 0, 0, 0, 0, 14163);
    setSyntheticLandVertex(&roundsUpSector, 1, 0, 0, 0, 13617);
    setSyntheticLandVertex(&roundsUpSector, 0, 1, 0, 0, 14229);
    STerrainSectorSource roundsUpSource;
    roundsUpSource.sectorX = 0;
    roundsUpSource.sectorY = 0;
    roundsUpSource.memberName = "rounds-up.SEC";
    roundsUpSource.payload = roundsUpSector;
    CTerrainEditModel roundsUp;
    QVERIFY(roundsUp.loadSources(1, 1, runtimeMaximum,
                                 QVector<STerrainSectorSource>() << roundsUpSource));
    QCOMPARE(quantizedNavigationHeight(roundsUp.navigationHeightSample(
                 0, 0, ETerrainTileLayer::Land, &available)), 110);
    QVERIFY(available);

    QByteArray roundsDownSector = syntheticLandSector(0);
    setSyntheticLandVertex(&roundsDownSector, 0, 0, 0, 0, 14090);
    setSyntheticLandVertex(&roundsDownSector, 1, 0, 0, 0, 14136);
    setSyntheticLandVertex(&roundsDownSector, 1, 1, 0, 0, 13810);
    STerrainSectorSource roundsDownSource;
    roundsDownSource.sectorX = 0;
    roundsDownSource.sectorY = 0;
    roundsDownSource.memberName = "rounds-down.SEC";
    roundsDownSource.payload = roundsDownSector;
    CTerrainEditModel roundsDown;
    QVERIFY(roundsDown.loadSources(1, 1, runtimeMaximum,
                                   QVector<STerrainSectorSource>() << roundsDownSource));
    QCOMPARE(quantizedNavigationHeight(roundsDown.navigationHeightSample(
                 1, 0, ETerrainTileLayer::Land, &available)), 109);
    QVERIFY(available);

    QByteArray triangleSector = syntheticLandSector(0);
    setSyntheticLandVertex(&triangleSector, 1, 0, 0, 0, 65534);
    STerrainSectorSource triangleSource;
    triangleSource.sectorX = 0;
    triangleSource.sectorY = 0;
    triangleSource.memberName = "triangle.SEC";
    triangleSource.payload = triangleSector;
    CTerrainEditModel triangle;
    QVERIFY(triangle.loadSources(1, 1, 100.0f,
                                 QVector<STerrainSectorSource>() << triangleSource));
    const float triangleHeight = triangle.terrainSurfaceHeight(0.75f, 0.25f,
                                                                &available);
    QVERIFY(available);
    QVERIFY(qAbs(triangleHeight - 50.0f) < 0.0001f);

    // Nearest-cell selection initially starts in cell 1 for this point. The
    // runtime displaced-cell correction must walk back into cell 0 instead of
    // extrapolating the elevated neighboring cell's triangle.
    QByteArray correctedSector = syntheticLandSector(0);
    setSyntheticLandVertex(&correctedSector, 2, 1, 0, 0, 65534);
    setSyntheticLandVertex(&correctedSector, 1, 2, 0, 0, 65534);
    setSyntheticLandVertex(&correctedSector, 2, 2, 0, 0, 65534);
    STerrainSectorSource correctedSource;
    correctedSource.sectorX = 0;
    correctedSource.sectorY = 0;
    correctedSource.memberName = "corrected.SEC";
    correctedSource.payload = correctedSector;
    CTerrainEditModel corrected;
    QVERIFY(corrected.loadSources(1, 1, 100.0f,
                                  QVector<STerrainSectorSource>() << correctedSource));
    QCOMPARE(corrected.terrainSurfaceHeight(0.6f, 0.6f, &available), 0.0f);
    QVERIFY(available);

    // Client terrain queries clamp to the final complete cell.
    QCOMPARE(flat.terrainSurfaceHeight(-20.0f, 200.0f, &available), 50.0f);
    QVERIFY(available);
}

void EnduranceDocumentTests::mprMaterialRecordsBindEveryStoredFieldExactly()
{
    CMprDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(syntheticMpr(), "material-fields.mpr", &report),
             qPrintable(report.toText()));
    QCOMPARE(document.mapInfo().materials.size(), 2);
    const SMprMaterialRecord first = document.mapInfo().materials.at(0);
    QCOMPARE(first.type, qint32(3));
    QCOMPARE(first.red, 0.2f);
    QCOMPARE(first.green, 0.3f);
    QCOMPARE(first.blue, 0.6f);
    QCOMPARE(first.opacity, 0.7f);
    QCOMPARE(first.selfIllumination, 0.0f);
    QCOMPARE(first.waveMultiplier, 0.5f);
    QCOMPARE(first.warpSpeed, 0.0f);
    QCOMPARE(first.reserved1, 0.0f);
    QCOMPARE(first.reserved2, 0.0f);
    QCOMPARE(first.reserved3, 0.0f);
}

void EnduranceDocumentTests::mprMaterialAndAnimationEditIsLosslessAndSlotStable()
{
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(syntheticMpr(), "material-edit.mpr", &report),
             qPrintable(report.toText()));

    QVector<SMprMaterialRecord> materials = baseline.mapInfo().materials;
    materials[1].type = 1; // Texture identity must not force water/emission behavior.
    materials[1].red = 0.75f;
    materials[1].green = 0.25f;
    materials[1].blue = 0.125f;
    materials[1].opacity = 0.625f;
    materials[1].selfIllumination = 0.8f;
    materials[1].waveMultiplier = 0.4f;
    materials[1].warpSpeed = 0.125f;
    QVector<SMprAnimatedTileRecord> animations;
    SMprAnimatedTileRecord animation;
    animation.tileIndex = 1;
    animation.frameCount = 2;
    animations.append(animation);

    CMprEditTransaction transaction("typed material edit");
    QVERIFY2(baseline.appendMapParametersEdit(baseline.mapInfo().maxZ,
                                               materials, animations,
                                               &transaction, &report),
             qPrintable(report.toText()));
    QCOMPARE(transaction.edits().size(), 1);
    QByteArray candidate;
    QVERIFY2(baseline.applyTransaction(transaction, &candidate, &report),
             qPrintable(report.toText()));

    CMprDocument reopened;
    CValidationReport reopenedReport;
    QVERIFY2(reopened.loadBytes(candidate, "material-edit-reopen.mpr", &reopenedReport),
             qPrintable(reopenedReport.toText()));
    QCOMPARE(reopened.mapInfo().materials, materials);
    QCOMPARE(reopened.mapInfo().animatedTiles, animations);
    QCOMPARE(reopened.mapInfo().tileTypes, baseline.mapInfo().tileTypes);
    QCOMPARE(reopened.memberPayload("unknown.bin"), baseline.memberPayload("unknown.bin"));
    QCOMPARE(reopened.memberPayload("CaveSet000000.SEC"),
             baseline.memberPayload("CaveSet000000.SEC"));

    // Both active sample tiles still reference shared material slot 1. Editing
    // that table record must not rewrite texture slots or per-cell ownership.
    const SMprSectorInfo sector = reopened.sectors().front();
    QCOMPARE(sector.overlayMaterialIndices.at(1), qint16(1));
    QCOMPARE(sector.overlayMaterialIndices.at(2), qint16(1));
}

void EnduranceDocumentTests::mprMaterialAndAnimationEditRejectsInvalidValues()
{
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY(baseline.loadBytes(syntheticMpr(), "material-invalid.mpr", &report));
    QVector<SMprMaterialRecord> materials = baseline.mapInfo().materials;
    QVector<SMprAnimatedTileRecord> animations;
    CMprEditTransaction transaction("invalid material edit");

    materials[0].opacity = 1.1f;
    QVERIFY(!baseline.appendMapParametersEdit(baseline.mapInfo().maxZ,
                                               materials, animations,
                                               &transaction, &report));
    materials = baseline.mapInfo().materials;
    SMprAnimatedTileRecord invalidAnimation;
    invalidAnimation.tileIndex = 2;
    invalidAnimation.frameCount = 2;
    animations.append(invalidAnimation);
    QVERIFY(!baseline.appendMapParametersEdit(baseline.mapInfo().maxZ,
                                               materials, animations,
                                               &transaction, &report));

    CMprDocument wideAtlas;
    CValidationReport wideReport;
    QVERIFY2(wideAtlas.loadBytes(
        syntheticMpr(syntheticOverlaySector(), syntheticMp(128)),
        "animation-page-invalid.mpr", &wideReport),
        qPrintable(wideReport.toText()));
    SMprAnimatedTileRecord crossingPage;
    crossingPage.tileIndex = 56;
    crossingPage.frameCount = 32;
    CMprEditTransaction crossingTransaction("crossing animation page");
    QVERIFY(!wideAtlas.appendMapParametersEdit(
        wideAtlas.mapInfo().maxZ, wideAtlas.mapInfo().materials,
        {crossingPage}, &crossingTransaction, &wideReport));
    QVERIFY(crossingTransaction.isEmpty());
    QVERIFY(wideReport.toText().contains("mpr.animation_page"));
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

    const STerrainHeightCommand saturatedBrush = terrain.makeRaiseLower(
        brush, -70000.0f, true);
    QCOMPARE(saturatedBrush.deltas.size(), 1);
    QVERIFY(!saturatedBrush.requiresMaxZDecision());
    QCOMPARE(saturatedBrush.deltas.front().after, quint16(0));

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

void EnduranceDocumentTests::appliedTerrainDiagnosticsMatchPreApplyAndKeepProjectionLocal()
{
    CTerrainEditModel terrain;
    QVERIFY(terrain.loadSources(1, 1, 100.0f, twoSectorTerrain(1000).mid(0, 1)));
    STerrainBrush brush;
    brush.centerX = 10.0f;
    brush.centerY = 12.0f;
    brush.radius = 2.0f;
    brush.falloff = ETerrainBrushFalloff::Constant;
    const STerrainHeightCommand command = terrain.makeRaiseLower(brush, 5000.0f);
    QVERIFY(!command.isEmpty());

    STerrainDiagnosticPolicy policy;
    policy.steepSlopeDegrees = 10.0f;
    const STerrainEditDiagnostics before = terrain.diagnose(
        command, QVector<STerrainObjectHeightProbe>(), policy);
    QVERIFY(before.valid);
    QVERIFY(terrain.apply(command, true));
    const STerrainEditDiagnostics applied = terrain.diagnoseApplied(
        command, QVector<STerrainObjectHeightProbe>(), policy);
    QVERIFY(applied.valid);
    QCOMPARE(applied.changedVertexCount, before.changedVertexCount);
    QCOMPARE(applied.affectedSectorCount, before.affectedSectorCount);
    QCOMPARE(applied.maximumSlopeBeforeDegrees, before.maximumSlopeBeforeDegrees);
    QCOMPARE(applied.maximumSlopeAfterDegrees, before.maximumSlopeAfterDegrees);
    QCOMPARE(applied.steepSlopeRisk, before.steepSlopeRisk);

    const STerrainHeightInfluenceBounds influence =
        terrainHeightInfluenceBounds(command, terrain.vertexCountX(),
                                     terrain.vertexCountY());
    QVERIFY(influence.valid);
    QVERIFY(influence.contains(10.0f, 12.0f));
    QVERIFY(influence.contains(-1.0f, 12.0f));
    QVERIFY(!influence.contains(100.0f, 12.0f));
    QVERIFY(!terrainHeightInfluenceBounds(STerrainHeightCommand(),
                                          terrain.vertexCountX(),
                                          terrain.vertexCountY()).valid);
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
        analysis.landCatalog.gridSignatureForPacked(packed.encode(), &found);
    QVERIFY(found);
    QCOMPARE(signature.northWest, 3);
    QCOMPARE(signature.northEast, 3);
    QCOMPARE(signature.southEast, 4);
    QCOMPARE(signature.southWest, 4);
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

    // The shared catalog now stores renderer-grid signatures directly. This
    // fixture-independent nine-cell contract prevents either the authored or
    // inferred path from reintroducing an internal Y reflection.
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
            QCOMPARE(decoded.northWest, stamp.northWest);
            QCOMPARE(decoded.northEast, stamp.northEast);
            QCOMPARE(decoded.southEast, stamp.southEast);
            QCOMPARE(decoded.southWest, stamp.southWest);
            bool gridFound = false;
            const STerrainTransitionSignature visible =
                analysis.landCatalog.gridSignatureForPacked(
                    paintedPacked, &gridFound);
            QVERIFY(gridFound);
            QCOMPARE(visible, stamp);
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
        QVERIFY(!family.name.contains("Proposed", Qt::CaseInsensitive));
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
    profile.families[2].landAllowed = false;
    profile.families[2].overlayAllowed = true;
    QVERIFY(!profile.mergeFamilies(QVector<qint32>({1, 3}), "Invalid",
                                   &badMergeReport));
    QVERIFY(badMergeReport.toText().contains("Land and overlay"));
    profile.families[2].layer = ETerrainAtlasLayer::Land;
    profile.families[2].landAllowed = true;
    profile.families[2].overlayAllowed = false;
    CValidationReport mergeReport;
    QVERIFY2(profile.mergeFamilies(QVector<qint32>({1, 3}), "Soil and meadow",
                                   &mergeReport), qPrintable(mergeReport.toText()));
    QCOMPARE(profile.families.size(), 2);
    QCOMPARE(profile.slotDefinitions.at(2).signature.northWest, qint32(1));

    profile.resourceArchiveHint =
        QStringLiteral("../res/HDPack_Terrain.res");
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

void EnduranceDocumentTests::semanticAtlasPartialCatalogRejectsCrossLayerCornersInline()
{
    STerrainAtlasProfile profile;
    profile.mprNamespace = "synthetic_partial";
    profile.atlasPixelSha256 = QByteArray(64, 'c');
    profile.atlasSlotCount = 3;
    profile.provenance = CTerrainAtlasProfileInference::currentProvenance();
    STerrainAtlasProfileFamily stone;
    stone.id = 1; stone.name = "Stone";
    STerrainAtlasProfileFamily moss;
    moss.id = 2; moss.name = "Moss";
    STerrainAtlasProfileFamily water;
    water.id = 3; water.name = "Water";
    water.layer = ETerrainAtlasLayer::Overlay;
    water.landAllowed = false;
    water.overlayAllowed = true;
    profile.families << stone << moss << water;
    const auto definition = [](int index, qint32 nw, qint32 ne,
                               qint32 se, qint32 sw, bool assigned) {
        STerrainAtlasProfileSlot value;
        value.slot = index;
        value.signature.northWest = nw;
        value.signature.northEast = ne;
        value.signature.southEast = se;
        value.signature.southWest = sw;
        value.assigned = assigned;
        value.ambiguous = !assigned;
        return value;
    };
    profile.slotDefinitions
        << definition(0, 1, 1, 1, 1, true)
        << definition(1, 3, 3, 3, 3, true)
        << definition(2, 1, 3, 3, 1, false);
    CValidationReport normalizeReport;
    QVERIFY2(profile.normalizeMembership(&normalizeReport),
             qPrintable(normalizeReport.toText()));
    QCOMPARE(profile.assignedSlotCount(), 2);

    CValidationReport usableReport;
    const STerrainAtlasAnalysis partial = profile.usableAnalysis(&usableReport);
    QVERIFY2(partial.usable, qPrintable(usableReport.toText()));
    QCOMPARE(partial.describedSlots, 2);

    CValidationReport assignReport;
    QVERIFY2(profile.assignSlotToFamily(2, 1, &assignReport),
             qPrintable(assignReport.toText()));
    QCOMPARE(profile.assignedSlotCount(), 3);
    QVERIFY(profile.families.at(0).memberSlots.contains(2));
    QVERIFY(!profile.families.at(2).memberSlots.contains(2));
    const STerrainTransitionSignature before = profile.slotDefinitions.at(2).signature;
    CValidationReport crossLayerReport;
    QVERIFY(!profile.setSlotCornerFamily(2, 1, 3, &crossLayerReport));
    QVERIFY(crossLayerReport.toText().contains("mix ground and overlay"));
    QCOMPARE(profile.slotDefinitions.at(2).signature.northEast, before.northEast);
    QVERIFY(profile.slotDefinitions.at(2).assigned);

    CValidationReport cornerReport;
    QVERIFY2(profile.setSlotCornerFamily(2, 1, 2, &cornerReport),
             qPrintable(cornerReport.toText()));
    QCOMPARE(profile.slotDefinitions.at(2).signature.northEast, qint32(2));
    QVERIFY(!profile.slotDefinitions.at(2).ambiguous);
    QVERIFY(profile.families.at(1).memberSlots.contains(2));

    profile.families[0].ambiguous = true;
    profile.approved = true;
    CValidationReport confirmationReport;
    QVERIFY(!profile.approvedAnalysis(&confirmationReport).usable);
    QVERIFY(confirmationReport.toText().contains("Confirm material"));
    profile.families[0].ambiguous = false;
    profile.approved = false;

    CValidationReport thumbnailReport;
    QVERIFY2(profile.setFamilyThumbnail(1, 0, &thumbnailReport),
             qPrintable(thumbnailReport.toText()));
    QCOMPARE(profile.families.at(0).thumbnailSlot, 0);
    const QJsonObject json = profile.toJson();
    STerrainAtlasProfile reopened;
    CValidationReport reopenReport;
    QVERIFY2(STerrainAtlasProfile::fromJson(json, &reopened, &reopenReport),
             qPrintable(reopenReport.toText()));
    QCOMPARE(reopened.toJson(), json);
    QCOMPARE(reopened.families.at(0).thumbnailSlot, 0);
}

void EnduranceDocumentTests::cornerOnlyMaterialPaintUsesAuthoredContextAndStaysLocal()
{
    CMprDocument document;
    CValidationReport loadReport;
    QVERIFY2(document.loadBytes(syntheticMpr(syntheticLandSector(1200)),
                                "corner-detail.mpr", &loadReport),
             qPrintable(loadReport.toText()));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    const auto add = [&catalog](qint32 nw, qint32 ne, qint32 se, qint32 sw,
                                quint16 packed) {
        STerrainTransitionVariant variant;
        variant.signature.northWest = nw;
        variant.signature.northEast = ne;
        variant.signature.southEast = se;
        variant.signature.southWest = sw;
        variant.packedTile = packed;
        return catalog.addVariant(variant);
    };
    QVERIFY(add(1, 1, 1, 1, 0));
    QVERIFY(add(3, 3, 3, 3, 1));
    const quint16 pureFourPacked = quint16(1 << 14);
    QVERIFY(add(4, 4, 4, 4, pureFourPacked));
    STerrainTransitionSignature detailOnGround;
    detailOnGround.northWest = 2;
    detailOnGround.northEast = 3;
    detailOnGround.southEast = 3;
    detailOnGround.southWest = 3;
    STerrainTransitionSignature detailOnCommon;
    detailOnCommon.northWest = 2;
    detailOnCommon.northEast = 1;
    detailOnCommon.southEast = 1;
    detailOnCommon.southWest = 1;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        const STerrainTransitionSignature onGround =
            CTerrainTransitionCatalog::rotated(detailOnGround, rotation);
        QVERIFY(add(onGround.northWest, onGround.northEast,
                    onGround.southEast, onGround.southWest,
                    quint16(2 | (rotation << 14))));
        const STerrainTransitionSignature onCommon =
            CTerrainTransitionCatalog::rotated(detailOnCommon, rotation);
        QVERIFY(add(onCommon.northWest, onCommon.northEast,
                    onCommon.southEast, onCommon.southWest,
                    quint16(4 | (rotation << 14))));
    }
    STerrainTransitionSignature unavailableOnFour;
    unavailableOnFour.northWest = 2;
    unavailableOnFour.northEast = 4;
    unavailableOnFour.southEast = 4;
    unavailableOnFour.southWest = 4;
    bool unavailableFound = true;
    catalog.selectForPaint(unavailableOnFour, 8, 8, 0x434F524Eu,
                           &unavailableFound);
    QVERIFY(!unavailableFound);

    // Put family 3 in all four cells sharing vertex (8, 8). Contextual corner
    // paint must choose the family-2-on-family-3 authored rotations, not stamp
    // one frozen thumbnail orientation.
    for(const QPoint& cell : QVector<QPoint>({QPoint(8, 8), QPoint(7, 8),
                                               QPoint(7, 7), QPoint(8, 7)}))
    {
        STerrainTileBrush setup;
        setup.centerX = cell.x();
        setup.centerY = cell.y();
        setup.radius = 0.0f;
        QVERIFY(terrain.apply(terrain.makeTilePaint(
            setup, ETerrainTileLayer::Land, 0, 1, 0), true));
    }
    CValidationReport contextualReport;
    const STerrainTileCommand contextual =
        terrain.makeSemanticCornerMaterialPaint(
            8, 8, 8, 8, ETerrainTileLayer::Land, 2, catalog,
            0x434F524Eu, &contextualReport);
    QVERIFY2(!contextualReport.hasErrors(), qPrintable(contextualReport.toText()));
    QCOMPARE(contextual.deltas.size(), 4);
    const QVector<QPoint> expectedCells({QPoint(8, 8), QPoint(7, 8),
                                         QPoint(7, 7), QPoint(8, 7)});
    const QSet<int> expectedCellKeys({8 * 16 + 8, 8 * 16 + 7,
                                      7 * 16 + 7, 7 * 16 + 8});
    QSet<int> changedCellKeys;
    for(const STerrainTileDelta& delta : contextual.deltas)
    {
        changedCellKeys.insert(delta.globalY * 16 + delta.globalX);
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        const QVector<qint32> corners({signature.northWest,
                                      signature.northEast,
                                      signature.southEast,
                                      signature.southWest});
        QCOMPARE(corners.count(2), 1);
        QCOMPARE(corners.count(3), 3);
    }
    QCOMPARE(changedCellKeys, expectedCellKeys);
    QVERIFY(terrain.apply(contextual, true));

    // If the exact clicked combination does not exist, the clicked tile still
    // changes to the closest authored corner signature. Adjacent cells never
    // receive this approximation, so the incomplete atlas cannot spread it.
    CTerrainEditModel fallbackTerrain;
    QVERIFY(fallbackTerrain.load(document));
    for(const QPoint& cell : expectedCells)
    {
        STerrainTileBrush setup;
        setup.centerX = cell.x();
        setup.centerY = cell.y();
        setup.radius = 0.0f;
        QVERIFY(fallbackTerrain.apply(fallbackTerrain.makeTilePaint(
            setup, ETerrainTileLayer::Land, 0, 0, 1), true));
        QCOMPARE(fallbackTerrain.packedTile(
            cell.x(), cell.y(), ETerrainTileLayer::Land), pureFourPacked);
    }
    CValidationReport fallbackReport;
    const STerrainTileCommand fallback =
        fallbackTerrain.makeSemanticCornerMaterialPaint(
            8, 8, 8, 8, ETerrainTileLayer::Land, 2, catalog,
            0x434F524Eu, &fallbackReport);
    QVERIFY2(!fallbackReport.hasErrors(), qPrintable(fallbackReport.toText()));
    QVERIFY2(fallbackReport.toText().contains("terrain.semantic_vertex_fallback"),
             qPrintable(fallbackReport.toText()));
    QCOMPARE(fallback.deltas.size(), 4);
    QSet<int> fallbackCells;
    for(const STerrainTileDelta& delta : fallback.deltas)
    {
        fallbackCells.insert(delta.globalY * 16 + delta.globalX);
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        const QVector<qint32> corners({signature.northWest,
                                      signature.northEast,
                                      signature.southEast,
                                      signature.southWest});
        QCOMPARE(corners.count(2), 1);
    }
    QCOMPARE(fallbackCells, expectedCellKeys);

    // A palette card for a corner-only family carries the support shown by
    // its four-tile thumbnail. Painting that card must place the exact motif
    // even when the terrain underneath contains an unrelated material; a
    // nearest-family fallback would produce the checkerboard seen with sparse
    // real atlases such as Zone 2's two-slot Ground family.
    STerrainTileBrush contextualBrush;
    contextualBrush.centerX = 8.0f;
    contextualBrush.centerY = 8.0f;
    contextualBrush.radius = 0.0f;
    CValidationReport explicitSupportReport;
    const STerrainTileCommand explicitSupport =
        fallbackTerrain.makeSemanticVertexPaint(
            8, 8, contextualBrush, ETerrainTileLayer::Land, 2, catalog,
            0x434F524Eu, &explicitSupportReport, 3);
    QVERIFY2(!explicitSupportReport.hasErrors(),
             qPrintable(explicitSupportReport.toText()));
    QVERIFY2(!explicitSupportReport.toText().contains(
                 "terrain.semantic_vertex_fallback"),
             qPrintable(explicitSupportReport.toText()));
    QCOMPARE(explicitSupport.semanticAudit.solverMode,
             QStringLiteral("vertex-context-stamp"));
    QCOMPARE(explicitSupport.semanticAudit.backgroundMaterialId, qint32(3));
    QCOMPARE(explicitSupport.deltas.size(), 4);
    for(const STerrainTileDelta& delta : explicitSupport.deltas)
    {
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        const QVector<qint32> corners({signature.northWest,
                                      signature.northEast,
                                      signature.southEast,
                                      signature.southWest});
        QCOMPARE(corners.count(2), 1);
        QCOMPARE(corners.count(3), 3);
    }
    QVERIFY(fallbackTerrain.apply(explicitSupport, true));
    CTerrainEditModel safeWrongSupportTerrain;
    QVERIFY(safeWrongSupportTerrain.load(document));
    for(const QPoint& cell : expectedCells)
    {
        STerrainTileBrush setup;
        setup.centerX = cell.x();
        setup.centerY = cell.y();
        setup.radius = 0.0f;
        QVERIFY(safeWrongSupportTerrain.apply(
            safeWrongSupportTerrain.makeTilePaint(
                setup, ETerrainTileLayer::Land, 0, 0, 1), true));
    }
    CValidationReport safeWrongSupportReport;
    const STerrainTileCommand safeWrongSupport =
        safeWrongSupportTerrain.makeSemanticVertexPaint(
            8, 8, contextualBrush, ETerrainTileLayer::Land, 2, catalog,
            0x53414645u, &safeWrongSupportReport, 3,
            ETerrainSemanticPaintPolicy::ExactExistingOnly);
    QVERIFY2(safeWrongSupport.isEmpty(),
             qPrintable(safeWrongSupportReport.toText()));
    QVERIFY2(safeWrongSupportReport.hasErrors(),
             qPrintable(safeWrongSupportReport.toText()));

    contextualBrush.centerX = 9.0f;
    contextualBrush.centerY = 8.0f;
    CValidationReport overlapReport;
    const STerrainTileCommand overlap = fallbackTerrain.makeSemanticVertexPaint(
        9, 8, contextualBrush, ETerrainTileLayer::Land, 2, catalog,
        0x434F524Eu, &overlapReport, 3);
    QVERIFY2(!overlap.isEmpty(), qPrintable(overlapReport.toText()));
    QVERIFY2(!overlapReport.hasErrors(), qPrintable(overlapReport.toText()));
    QVERIFY2(overlapReport.toText().contains(
                 "terrain.semantic_vertex_fallback"),
             qPrintable(overlapReport.toText()));
    QCOMPARE(overlap.semanticAudit.paintedVertices,
             QVector<QPoint>({QPoint(9, 8)}));
    QVERIFY(overlap.deltas.size() <= 4);
    for(const STerrainTileDelta& delta : overlap.deltas)
    {
        QVERIFY(delta.globalX == 8 || delta.globalX == 9);
        QVERIFY(delta.globalY == 7 || delta.globalY == 8);
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        const int corner = delta.globalX == 8
            ? (delta.globalY == 7 ? 2 : 1)
            : (delta.globalY == 7 ? 3 : 0);
        const QVector<qint32> corners({signature.northWest,
                                      signature.northEast,
                                      signature.southEast,
                                      signature.southWest});
        QCOMPARE(corners.at(corner), qint32(2));
    }

    STerrainTransitionSignature detailEdgeOnGround;
    detailEdgeOnGround.northWest = 2;
    detailEdgeOnGround.northEast = 2;
    detailEdgeOnGround.southEast = 3;
    detailEdgeOnGround.southWest = 3;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        const STerrainTransitionSignature edgeOnGround =
            CTerrainTransitionCatalog::rotated(detailEdgeOnGround, rotation);
        QVERIFY(add(edgeOnGround.northWest, edgeOnGround.northEast,
                    edgeOnGround.southEast, edgeOnGround.southWest,
                    quint16(7 | (rotation << 14))));
    }

    // Safe contextual stamps must still connect to another compatible stamp
    // vertically. The guard above should reject only unrelated support
    // materials, not the material/support pair already authored by the card.
    CTerrainEditModel safeVerticalTerrain;
    QVERIFY(safeVerticalTerrain.load(document));
    for(const QPoint& cell : QVector<QPoint>({QPoint(7, 7), QPoint(8, 7),
                                               QPoint(7, 8), QPoint(8, 8),
                                               QPoint(7, 9), QPoint(8, 9)}))
    {
        STerrainTileBrush setup;
        setup.centerX = cell.x();
        setup.centerY = cell.y();
        setup.radius = 0.0f;
        QVERIFY(safeVerticalTerrain.apply(
            safeVerticalTerrain.makeTilePaint(
                setup, ETerrainTileLayer::Land, 0, 1, 0), true));
    }
    contextualBrush.centerX = 8.0f;
    contextualBrush.centerY = 8.0f;
    CValidationReport safeFirstStampReport;
    const STerrainTileCommand safeFirstStamp =
        safeVerticalTerrain.makeSemanticVertexPaint(
            8, 8, contextualBrush, ETerrainTileLayer::Land, 2, catalog,
            0x53414645u, &safeFirstStampReport, 3,
            ETerrainSemanticPaintPolicy::ExactExistingOnly);
    QVERIFY2(!safeFirstStamp.isEmpty(),
             qPrintable(safeFirstStampReport.toText()));
    QVERIFY2(!safeFirstStampReport.hasErrors(),
             qPrintable(safeFirstStampReport.toText()));
    QVERIFY(safeVerticalTerrain.apply(safeFirstStamp, true));
    contextualBrush.centerY = 9.0f;
    CValidationReport safeVerticalReport;
    const STerrainTileCommand safeVertical =
        safeVerticalTerrain.makeSemanticVertexPaint(
            8, 9, contextualBrush, ETerrainTileLayer::Land, 2, catalog,
            0x53414645u, &safeVerticalReport, 3,
            ETerrainSemanticPaintPolicy::ExactExistingOnly);
    QVERIFY2(!safeVertical.isEmpty(), qPrintable(safeVerticalReport.toText()));
    QVERIFY2(!safeVerticalReport.hasErrors(),
             qPrintable(safeVerticalReport.toText()));
    QCOMPARE(safeVertical.semanticAudit.solverMode,
             QStringLiteral("vertex-context-stamp"));

    // The inverse 1-on-3 relationship is also a complete vertex motif. A
    // material that has ordinary pure tiles (family 4) must still be able to
    // paint its explicit contextual card on a corner-only support (family 2).
    STerrainTransitionSignature stoneOnDetail;
    stoneOnDetail.northWest = 4;
    stoneOnDetail.northEast = stoneOnDetail.southEast =
        stoneOnDetail.southWest = 2;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        const STerrainTransitionSignature rotated =
            CTerrainTransitionCatalog::rotated(stoneOnDetail, rotation);
        QVERIFY(add(rotated.northWest, rotated.northEast,
                    rotated.southEast, rotated.southWest,
                    quint16(6 | (rotation << 14))));
    }
    CTerrainEditModel inverseTerrain;
    QVERIFY(inverseTerrain.load(document));
    contextualBrush.centerX = 8.0f;
    CValidationReport inverseReport;
    const STerrainTileCommand inverse = inverseTerrain.makeSemanticVertexPaint(
        8, 8, contextualBrush, ETerrainTileLayer::Land, 4, catalog,
        0x494E5652u, &inverseReport, 2);
    QVERIFY2(!inverseReport.hasErrors(), qPrintable(inverseReport.toText()));
    QCOMPARE(inverse.semanticAudit.solverMode,
             QStringLiteral("vertex-context-stamp"));
    QCOMPARE(inverse.deltas.size(), 4);
    for(const STerrainTileDelta& delta : inverse.deltas)
    {
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        const QVector<qint32> corners({signature.northWest,
                                      signature.northEast,
                                      signature.southEast,
                                      signature.southWest});
        QCOMPARE(corners.count(4), 1);
        QCOMPARE(corners.count(2), 3);
    }
}

void EnduranceDocumentTests::vertexMaterialBrushPaintsCircleAndConstrainsCornerOnlyFamilies()
{
    CMprDocument document;
    CValidationReport loadReport;
    QVERIFY2(document.loadBytes(syntheticMpr(
                                    syntheticLandSector(1200), syntheticMp(16)),
                                "vertex-circle.mpr", &loadReport),
             qPrintable(loadReport.toText()));

    CTerrainTransitionCatalog complete;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        variant.packedTile = quint16(mask);
        QVERIFY(complete.addVariant(variant));
    }
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));
    STerrainTileBrush circle;
    circle.centerX = 8.0f;
    circle.centerY = 8.0f;
    circle.radius = 1.0f;
    circle.shape = ETerrainTileBrushShape::Circle;
    CValidationReport circleReport;
    const STerrainTileCommand paint = terrain.makeSemanticVertexPaint(
        8, 8, circle, ETerrainTileLayer::Land, 2, complete,
        0x56455254u, &circleReport);
    QVERIFY2(!circleReport.hasErrors(), qPrintable(circleReport.toText()));
    QCOMPARE(paint.semanticAudit.solverMode, QStringLiteral("vertex-exact"));
    QCOMPARE(paint.semanticAudit.paintedVertices.size(), 5);
    QCOMPARE(paint.deltas.size(), 12);
    QVERIFY(terrain.apply(paint, true));
    for(int y = 5; y <= 10; ++y)
    {
        for(int x = 5; x <= 10; ++x)
        {
            bool hereFound = false;
            const STerrainTransitionSignature here = complete.gridSignatureForPacked(
                terrain.packedTile(x, y, ETerrainTileLayer::Land), &hereFound);
            QVERIFY(hereFound);
            if(x + 1 < terrain.tileCountX())
            {
                bool eastFound = false;
                const STerrainTransitionSignature east = complete.gridSignatureForPacked(
                    terrain.packedTile(x + 1, y, ETerrainTileLayer::Land), &eastFound);
                QVERIFY(eastFound);
                QCOMPARE(here.northEast, east.northWest);
                QCOMPARE(here.southEast, east.southWest);
            }
            if(y + 1 < terrain.tileCountY())
            {
                bool southFound = false;
                const STerrainTransitionSignature south = complete.gridSignatureForPacked(
                    terrain.packedTile(x, y + 1, ETerrainTileLayer::Land), &southFound);
                QVERIFY(southFound);
                QCOMPARE(here.southWest, south.northWest);
                QCOMPARE(here.southEast, south.northEast);
            }
        }
    }

    // A vertex on a sector boundary is still one semantic paint target. Its
    // four incident cells must be resolved together even though serialization
    // later writes those cells back to two different SEC members.
    QVector<STerrainSectorSource> seamSources = twoSectorTerrain(1200);
    for(STerrainSectorSource& source : seamSources)
        source.landTileOffset = 5 + 33 * 33 * 8;
    CTerrainEditModel seamTerrain;
    QVector<qint32> seamTileTypes(16, 1);
    CValidationReport seamLoadReport;
    QVERIFY2(seamTerrain.loadSources(2, 1, 100.0f, seamSources,
                                     &seamLoadReport, 1, seamTileTypes, 2),
             qPrintable(seamLoadReport.toText()));
    STerrainTileBrush seamVertex = circle;
    seamVertex.centerX = 16.0f;
    seamVertex.centerY = 8.0f;
    seamVertex.radius = 0.0f;
    CValidationReport seamPaintReport;
    const STerrainTileCommand seamPaint = seamTerrain.makeSemanticVertexPaint(
        15, 8, seamVertex, ETerrainTileLayer::Land, 2, complete,
        0x5345414Du, &seamPaintReport);
    QVERIFY2(!seamPaintReport.hasErrors(), qPrintable(seamPaintReport.toText()));
    QCOMPARE(seamPaint.semanticAudit.paintedVertices,
             QVector<QPoint>({QPoint(16, 8)}));
    QCOMPARE(seamPaint.deltas.size(), 4);
    QSet<int> seamCells;
    for(const STerrainTileDelta& delta : seamPaint.deltas)
        seamCells.insert(delta.globalY * seamTerrain.tileCountX() + delta.globalX);
    QCOMPARE(seamCells,
             QSet<int>({7 * 32 + 15, 7 * 32 + 16,
                        8 * 32 + 15, 8 * 32 + 16}));
    QVERIFY(seamTerrain.apply(seamPaint, true));
    QVERIFY(seamTerrain.sectorPayload(0, 0) != seamSources.at(0).payload);
    QVERIFY(seamTerrain.sectorPayload(1, 0) != seamSources.at(1).payload);

    // A family with only isolated one-corner signatures cannot fill all five
    // vertices of this circle. The center remains mandatory and resolves all
    // four incident cells; unsupported surrounding vertices are skipped.
    CTerrainTransitionCatalog detail;
    STerrainTransitionVariant common;
    common.signature.northWest = common.signature.northEast = 1;
    common.signature.southEast = common.signature.southWest = 1;
    common.packedTile = 0;
    QVERIFY(detail.addVariant(common));
    STerrainTransitionSignature oneCorner;
    oneCorner.northWest = 3;
    oneCorner.northEast = oneCorner.southEast = oneCorner.southWest = 1;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        STerrainTransitionVariant variant;
        variant.signature = CTerrainTransitionCatalog::rotated(oneCorner, rotation);
        variant.packedTile = quint16(1 | (rotation << 14));
        QVERIFY(detail.addVariant(variant));
    }
    CTerrainEditModel detailTerrain;
    QVERIFY(detailTerrain.load(document));
    CValidationReport detailReport;
    const STerrainTileCommand detailPaint = detailTerrain.makeSemanticVertexPaint(
        8, 8, circle, ETerrainTileLayer::Land, 3, detail,
        0x56455254u, &detailReport, 1);
    QVERIFY2(!detailReport.hasErrors(), qPrintable(detailReport.toText()));
    QCOMPARE(detailPaint.semanticAudit.solverMode,
             QStringLiteral("vertex-context-stamp"));
    QCOMPARE(detailPaint.semanticAudit.paintedVertices,
             QVector<QPoint>({QPoint(8, 8)}));
    QCOMPARE(detailPaint.deltas.size(), 4);
    QVERIFY(detailReport.toText().contains("terrain.semantic_vertex_detail_limit"));

    // Overlay texture painting uses the same vertex model but only resolves
    // incident cells whose overlay surface is active.
    CMprDocument overlayDocument;
    QVERIFY(overlayDocument.loadBytes(syntheticMpr(), "overlay-vertex.mpr"));
    CTerrainEditModel overlayTerrain;
    QVERIFY(overlayTerrain.load(overlayDocument));
    CTerrainTransitionCatalog overlayCatalog;
    QVERIFY(overlayCatalog.addVariant(common));
    STerrainTransitionSignature overlayCorner = oneCorner;
    overlayCorner.northWest = 2;
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        STerrainTransitionVariant variant;
        variant.signature = CTerrainTransitionCatalog::rotated(
            overlayCorner, rotation);
        variant.packedTile = quint16(1 | (rotation << 14));
        QVERIFY(overlayCatalog.addVariant(variant));
    }
    STerrainTileBrush oneVertex = circle;
    oneVertex.radius = 0.0f;
    CValidationReport overlayReport;
    const STerrainTileCommand overlayPaint = overlayTerrain.makeSemanticVertexPaint(
        8, 8, oneVertex, ETerrainTileLayer::Overlay, 2, overlayCatalog,
        0x4F564552u, &overlayReport);
    QVERIFY2(!overlayReport.hasErrors(), qPrintable(overlayReport.toText()));
    QCOMPARE(overlayPaint.semanticAudit.paintedVertices,
             QVector<QPoint>({QPoint(8, 8)}));
    QCOMPARE(overlayPaint.deltas.size(), 2);
    for(const STerrainTileDelta& delta : overlayPaint.deltas)
        QCOMPARE((delta.globalY * 16 + delta.globalX) % 3 == 0, false);

    // The authored Zone 1 baseline must resolve an ordinary circular vertex
    // brush without approximation. Ground/common and Grass/common provide the
    // known-good two-family mask vocabulary used by the original maps.
    CValidationReport zone1Report;
    const STerrainAtlasAnalysis zone1 =
        CTerrainAtlasAnalyzer::analyze("zone1", 512, &zone1Report);
    QVERIFY2(zone1.usable, qPrintable(zone1Report.toText()));
    const auto materialById = [&zone1](qint32 id) {
        return std::find_if(zone1.materials.cbegin(), zone1.materials.cend(),
            [id](const STerrainAtlasMaterial& material) {
                return material.id == id;
            });
    };
    const auto ground = materialById(2);
    const auto grass = materialById(4);
    QVERIFY(ground != zone1.materials.cend());
    QVERIFY(grass != zone1.materials.cend());
    CMprDocument zone1Document;
    QVERIFY(zone1Document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(512)), "zone1-vertex.mpr"));
    CTerrainEditModel zone1Terrain;
    QVERIFY(zone1Terrain.load(zone1Document));
    STerrainTileBrush fill;
    fill.centerX = fill.centerY = 7.5f;
    fill.radius = 16.0f;
    fill.shape = ETerrainTileBrushShape::Square;
    const SMprPackedTile grassTile = SMprPackedTile::decode(
        quint16(grass->representativeSlot & 0x3F)
        | quint16((grass->representativeSlot / 64) << 6));
    QVERIFY(zone1Terrain.apply(zone1Terrain.makeTilePaint(
        fill, ETerrainTileLayer::Land, grassTile.atlasIndex,
        grassTile.tileIndex, 0), true));
    STerrainTileBrush zone1Circle = circle;
    zone1Circle.radius = 2.0f;
    CValidationReport zone1PaintReport;
    const STerrainTileCommand zone1Paint = zone1Terrain.makeSemanticVertexPaint(
        8, 8, zone1Circle, ETerrainTileLayer::Land, ground->id,
        zone1.landCatalog, 0x5A4F4E31u, &zone1PaintReport);
    QVERIFY2(!zone1PaintReport.hasErrors(), qPrintable(zone1PaintReport.toText()));
    QCOMPARE(zone1Paint.semanticAudit.solverMode, QStringLiteral("vertex-exact"));
    QCOMPARE(zone1Paint.semanticAudit.paintedVertices.size(), 13);
    QVERIFY(!zone1Paint.isEmpty());
}

void EnduranceDocumentTests::semanticVertexPaintNeverUsesUniformForBlendFallback()
{
    CTerrainTransitionCatalog catalog;
    const auto add = [&catalog](const STerrainTransitionSignature& signature,
                                quint16 packed) {
        STerrainTransitionVariant variant;
        variant.signature = signature;
        variant.packedTile = packed;
        return catalog.addVariant(variant);
    };
    STerrainTransitionSignature background;
    background.northWest = background.northEast = 1;
    background.southEast = background.southWest = 1;
    STerrainTransitionSignature uniformPaint;
    uniformPaint.northWest = uniformPaint.northEast = 2;
    uniformPaint.southEast = uniformPaint.southWest = 2;
    QVERIFY(add(background, 0));
    QVERIFY(add(uniformPaint, 1));

    // The exact requested tuple is absent. Every rotated authored alternative
    // remains a blend, while the uniform tile is a tempting lower-cost hard
    // edge. Modern painting must never substitute that uniform for a blend.
    STerrainTransitionSignature authored;
    authored.northWest = 2;
    authored.northEast = 3;
    authored.southEast = authored.southWest = 1;
    for(int rotation = 0; rotation < 4; ++rotation)
        QVERIFY(add(CTerrainTransitionCatalog::rotated(authored, rotation),
                    quint16(2 + rotation)));

    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(16)),
        "immediate-never-uniform-for-blend.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));
    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 0.0f;
    CValidationReport report;
    const STerrainTileCommand command = terrain.makeSemanticVertexPaint(
        8, 8, brush, ETerrainTileLayer::Land, 2, catalog,
        0x4E4F554Eu, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(!command.isEmpty());
    for(const STerrainTileDelta& delta : command.deltas)
    {
        bool found = false;
        const STerrainTransitionSignature actual =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        const bool uniform = actual.northWest == 2
            && actual.northEast == 2 && actual.southEast == 2
            && actual.southWest == 2;
        QVERIFY2(!uniform,
            "Legacy immediate resolver substituted uniform artwork for a blend");
    }
}

void EnduranceDocumentTests::manualTilePaintRebasesDecodedSemanticsAndInvalidatesDisputes()
{
    CMprDocument baseline;
    QVERIFY(baseline.loadBytes(syntheticMpr(), "manual-semantic-rebase.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));

    CTerrainTransitionCatalog catalog;
    const auto packed = [](quint8 tileIndex, quint8 rotation) {
        SMprPackedTile tile;
        tile.tileIndex = tileIndex;
        tile.rotation = rotation;
        return tile.encode();
    };
    STerrainTransitionVariant base;
    base.signature.northWest = base.signature.northEast =
        base.signature.southEast = base.signature.southWest = 1;
    base.packedTile = 0;
    QVERIFY(catalog.addVariant(base));
    STerrainTransitionVariant modern = base;
    modern.signature.northWest = modern.signature.northEast =
        modern.signature.southEast = modern.signature.southWest = 2;
    modern.packedTile = quint16(1 << 14);
    QVERIFY(catalog.addVariant(modern));
    STerrainTransitionVariant manualUniform = base;
    manualUniform.packedTile = packed(1, 3);
    QVERIFY(catalog.addVariant(manualUniform));
    STerrainTransitionVariant manualTransition = base;
    manualTransition.signature.northWest = 2;
    manualTransition.signature.northEast = 1;
    manualTransition.signature.southEast = 1;
    manualTransition.signature.southWest = 1;
    manualTransition.packedTile = packed(2, 0);

    STerrainTileBrush cell;
    cell.centerX = 8.0f;
    cell.centerY = 8.0f;
    cell.radius = 0.0f;
    const STerrainTileCommand semantic = terrain.makeSemanticTilePaint(
        cell, ETerrainTileLayer::Land, 2, catalog, 0x4d414e55u);
    QVERIFY(!semantic.isEmpty());
    QVERIFY(terrain.apply(semantic, true));
    QVERIFY(terrain.virtualMaterialCellChanged(
        ETerrainTileLayer::Land, 8, 8));
    const quint16 semanticPacked = terrain.packedTile(
        8, 8, ETerrainTileLayer::Land);

    // This reproduces the bug: Manual changes a physical cell but leaves the
    // previously authored Modern field at material 2, so a later resolver
    // treats the stale field as authoritative and puts the old tile back.
    CValidationReport manualReport;
    const STerrainTileCommand manual = terrain.makeTilePaint(
        cell, ETerrainTileLayer::Land, 0, 1, 3, catalog, &manualReport);
    QVERIFY2(!manualReport.hasErrors(), qPrintable(manualReport.toText()));
    QVERIFY(!manual.isEmpty());
    QVERIFY(terrain.apply(manual, true));
    const quint16 manualPacked = manual.deltas.front().after;
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             manualPacked);
    for(const QPoint& vertex : {QPoint(8, 8), QPoint(9, 8),
                                 QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(1));
        QVERIFY(found);
    }
    QVERIFY(terrain.apply(manual, false));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             semanticPacked);
    for(const QPoint& vertex : {QPoint(8, 8), QPoint(9, 8),
                                 QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(2));
        QVERIFY(found);
    }
    QVERIFY(terrain.apply(manual, true));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             manualPacked);
    for(const QPoint& vertex : {QPoint(8, 8), QPoint(9, 8),
                                 QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(1));
        QVERIFY(found);
    }
    STerrainTransitionSignature replacement;
    QVERIFY(!terrain.missingBlendRepairSignature(
        8, 8, ETerrainTileLayer::Land, catalog, nullptr, true,
        &replacement));
    QVERIFY(!terrain.virtualMaterialCellChanged(
        ETerrainTileLayer::Land, 8, 8));

    // The Manual palette intentionally includes every physical slot. Once the
    // preceding Manual edit returns the material field to its baseline, an
    // unassigned physical tile remains a plain Manual edit: it neither creates
    // a semantic field nor receives hidden ownership/preservation metadata.
    CValidationReport unassignedReport;
    const STerrainTileCommand unassigned = terrain.makeTilePaint(
        cell, ETerrainTileLayer::Land, 0, 2, 1, catalog, &unassignedReport);
    QVERIFY2(!unassigned.isEmpty(), qPrintable(unassignedReport.toText()));
    QVERIFY(!unassignedReport.hasErrors());
    QVERIFY(terrain.apply(unassigned, true));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             packed(2, 1));
    // Keep the rest of the map's field intact. Only this unassigned artwork's
    // four touched vertices become unknown, so later Modern/Resolve can route
    // the real referenced slot to assignment without rebuilding unrelated
    // terrain or carrying a complete field snapshot in this command.
    CTerrainVirtualMaterialField partialManualField;
    QVERIFY(terrain.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &partialManualField, false));
    for(const QPoint& vertex : {QPoint(8, 8), QPoint(9, 8),
                                 QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = true;
        terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found);
        QVERIFY(!found);
    }
    bool unaffectedFound = false;
    terrain.virtualMaterialAt(
        ETerrainTileLayer::Land, 0, 0, &unaffectedFound);
    QVERIFY(unaffectedFound);
    const QMap<int, QPoint> unresolved =
        terrain.referencedUnassignedAtlasSlots(
            ETerrainTileLayer::Land, catalog);
    QCOMPARE(unresolved.size(), 1);
    QCOMPARE(unresolved.value(2), QPoint(8, 8));

    // Undo/redo is still entirely physical and exact.
    QVERIFY(terrain.apply(unassigned, false));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             manualPacked);
    QVERIFY(terrain.apply(unassigned, true));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             packed(2, 1));
    QVERIFY(terrain.apply(unassigned, false));

    // Re-enter Modern away from the Manual cell. This reconstructs semantics
    // from the post-Manual packed map and leaves a genuinely active field, so
    // a subsequent decoded Manual blend can be rebased without stale state.
    STerrainTileBrush farCell;
    farCell.centerX = 0.0f;
    farCell.centerY = 0.0f;
    farCell.radius = 0.0f;
    const STerrainTileCommand farModern = terrain.makeSemanticTilePaint(
        farCell, ETerrainTileLayer::Land, 2, catalog, 0x4641524du);
    QVERIFY(!farModern.isEmpty());
    QVERIFY(terrain.apply(farModern, true));
    QVERIFY(terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land));

    // A decoded non-uniform Manual tile is an explicit physical/corner edit.
    // Its exact selected corner tuple wins at the four touched vertices; the
    // surrounding incident cells must not outvote it or turn it into an
    // unset/disputed value. This stores only the final semantic values, not
    // Manual ownership or placement order.
    QVERIFY(catalog.addVariant(manualTransition));
    CValidationReport mixedReport;
    const STerrainTileCommand mixed = terrain.makeTilePaint(
        cell, ETerrainTileLayer::Land, 0, 2, 0, catalog, &mixedReport);
    QVERIFY2(!mixedReport.hasErrors(), qPrintable(mixedReport.toText()));
    QVERIFY(!mixed.isEmpty());
    QVERIFY(terrain.apply(mixed, true));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             manualTransition.packedTile);
    bool northWestFound = false;
    QCOMPARE(terrain.virtualMaterialAt(
        ETerrainTileLayer::Land, 8, 8, &northWestFound), qint32(2));
    QVERIFY(northWestFound);
    for(const QPoint& vertex : {QPoint(9, 8), QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(1));
        QVERIFY(found);
    }
    QVERIFY(!terrain.missingBlendRepairSignature(
        8, 8, ETerrainTileLayer::Land, catalog, nullptr, true,
        &replacement));
    QVERIFY(terrain.apply(mixed, false));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             manualPacked);
    for(const QPoint& vertex : {QPoint(8, 8), QPoint(9, 8),
                                 QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(1));
        QVERIFY(found);
    }
    QVERIFY(terrain.apply(mixed, true));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land),
             manualTransition.packedTile);
    northWestFound = false;
    QCOMPARE(terrain.virtualMaterialAt(
        ETerrainTileLayer::Land, 8, 8, &northWestFound), qint32(2));
    QVERIFY(northWestFound);
    for(const QPoint& vertex : {QPoint(9, 8), QPoint(9, 9), QPoint(8, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(1));
        QVERIFY(found);
    }

    // Keep the view integration in the regression contract. Manual must use
    // the catalog-aware command above, but it must not run Modern's automatic
    // end-of-stroke exact-blend pass. Later Modern operations treat its
    // physical/semantic state generically, without any Manual-origin privilege.
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int setTileStart = source.indexOf(
        "void CView::setTile(QVector3D posOnLand, bool bLand)");
    const int setTileEnd = source.indexOf(
        "void CView::setTile(QMap<STileLocation, STileInfo>& arrTileData)",
        setTileStart);
    QVERIFY2(setTileStart >= 0 && setTileEnd > setTileStart,
             "Manual terrain paint entry point was not found");
    const QByteArray setTile = source.mid(setTileStart,
                                          setTileEnd - setTileStart);
    QVERIFY2(setTile.contains(
        "selected.rotation, semanticCatalog, &report"),
        "Manual paint bypasses the catalog-aware semantic rebase command");
    const int releaseStart = source.indexOf("void CView::endTileBrushGroup()");
    const int releaseEnd = source.indexOf("void CView::cancelTileBrushGroup()",
                                          releaseStart);
    QVERIFY2(releaseStart >= 0 && releaseEnd > releaseStart,
             "Terrain stroke release implementation was not found");
    const QByteArray release = source.mid(releaseStart,
                                          releaseEnd - releaseStart);
    const int manualGuard = release.indexOf(
        "!= int(ETerrainPaintPaletteMode::Manual)");
    const int virtualRelease = release.indexOf(
        "makeVirtualStrokeResolution(");
    QVERIFY2(manualGuard >= 0 && virtualRelease > manualGuard,
             "Manual paint is incorrectly passed through Modern stroke release");
}

void EnduranceDocumentTests::manualTilePaintOnLegacyMapStaysPhysicalUntilModernStarts()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(32)),
        "manual-legacy-physical-only.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        SMprPackedTile packed;
        packed.tileIndex = quint8(mask);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    QVERIFY(!terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land));
    CValidationReport baselineReport;
    QVERIFY(terrain.initializeVirtualMaterialField(
        ETerrainTileLayer::Land, catalog, &baselineReport,
        ETerrainMaterialFieldRecovery::RecoverDeterministically));
    QVERIFY2(!baselineReport.hasErrors(),
             qPrintable(baselineReport.toText()));
    CTerrainVirtualMaterialField inactiveCache;
    QVERIFY(terrain.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &inactiveCache, true));
    QVERIFY(!terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land));

    STerrainTileBrush wholeMap;
    wholeMap.centerX = 8.0f;
    wholeMap.centerY = 8.0f;
    wholeMap.radius = 32.0f;
    wholeMap.shape = ETerrainTileBrushShape::Square;
    CValidationReport manualReport;
    const STerrainTileCommand manual = terrain.makeTilePaint(
        wholeMap, ETerrainTileLayer::Land, 0, 15, 0,
        catalog, &manualReport);
    QVERIFY2(!manualReport.hasErrors(), qPrintable(manualReport.toText()));
    QVERIFY(!manual.isEmpty());
    QVERIFY2(manual.virtualFields.isEmpty(),
             "Manual paint stored a complete map-sized field snapshot");
    QVERIFY2(!manual.virtualVertices.isEmpty(),
             "Manual paint did not record its exact semantic corner values");
    QVERIFY(terrain.apply(manual, true));
    QVERIFY(!terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land));
    CTerrainVirtualMaterialField manualField;
    QVERIFY(terrain.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &manualField, true));
    for(int y = 0; y <= terrain.tileCountY(); ++y)
        for(int x = 0; x <= terrain.tileCountX(); ++x)
            QCOMPARE(manualField.material(x, y), qint32(2));
    QVERIFY(!terrain.virtualMaterialCellChanged(
        ETerrainTileLayer::Land, 8, 8));
    QCOMPARE(terrainMprSaveRoute(
        false,
        terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land),
        terrain.virtualMaterialFieldActive(ETerrainTileLayer::Overlay),
        false, false), ETerrainMprSaveRoute::PlainMpr);

    STerrainTileBrush oneCell;
    oneCell.centerX = 8.0f;
    oneCell.centerY = 8.0f;
    oneCell.radius = 0.0f;
    CValidationReport modernReport;
    const STerrainTileCommand modern = terrain.makeVirtualTileMaterialPaint(
        oneCell, ETerrainTileLayer::Land, 1, catalog,
        0x4d4f4445u, &modernReport);
    QVERIFY2(!modernReport.hasErrors(), qPrintable(modernReport.toText()));
    QVERIFY2(!modern.isEmpty(),
             "Modern paint could not continue after physical-only Manual paint");
    CTerrainVirtualMaterialField recovered;
    QVERIFY(terrain.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &recovered, true));
    QCOMPARE(recovered.material(0, 0), qint32(2));
}

void EnduranceDocumentTests::firstModernStrokeOwnsFieldInitializationAndManualUndo()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(32)),
        "first-modern-field-transaction.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        SMprPackedTile packed;
        packed.tileIndex = quint8(mask);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    STerrainTileBrush manualCell;
    manualCell.centerX = 8.0f;
    manualCell.centerY = 8.0f;
    manualCell.radius = 0.0f;
    manualCell.shape = ETerrainTileBrushShape::Square;
    const STerrainTileCommand manual = terrain.makeTilePaint(
        manualCell, ETerrainTileLayer::Land, 0, 15, 0, catalog);
    QVERIFY(!manual.isEmpty());
    QCOMPARE(manual.pendingVirtualVertices.size(), 4);
    QVERIFY(terrain.apply(manual, true));

    QVector<qint32> fieldlessCurrent;
    QVector<qint32> fieldlessBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &fieldlessCurrent, &fieldlessBaseline));
    QVERIFY(fieldlessCurrent.isEmpty());
    QVERIFY(fieldlessBaseline.isEmpty());

    STerrainTileBrush modernCell = manualCell;
    modernCell.centerX = 10.0f;
    CValidationReport modernReport;
    STerrainTileCommand modern = terrain.makeVirtualTileMaterialPaint(
        modernCell, ETerrainTileLayer::Land, 2, catalog,
        0x46495253u, &modernReport);
    QVERIFY2(!modernReport.hasErrors(), qPrintable(modernReport.toText()));
    QVERIFY(!modern.isEmpty());

    // Merely planning the first Modern stamp must not create hidden live
    // state. The complete recovery belongs to the same undo transaction as
    // the visible stamp, including when a preceding Manual command is still
    // represented by sparse fieldless corners.
    QVector<qint32> plannedCurrent;
    QVector<qint32> plannedBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &plannedCurrent, &plannedBaseline));
    QCOMPARE(plannedCurrent, fieldlessCurrent);
    QCOMPARE(plannedBaseline, fieldlessBaseline);
    QCOMPARE(modern.virtualFields.size(), 1);
    QVERIFY(modern.virtualVertices.isEmpty());
    QCOMPARE(modern.virtualFields.front().beforeCurrent, fieldlessCurrent);
    QCOMPARE(modern.virtualFields.front().beforeBaseline, fieldlessBaseline);

    QVERIFY(terrain.apply(modern, true));
    STerrainTileBrush secondModernCell = modernCell;
    secondModernCell.centerX = 11.0f;
    const STerrainTileCommand secondModern =
        terrain.makeVirtualTileMaterialPaint(
            secondModernCell, ETerrainTileLayer::Land, 2, catalog,
            0x46495253u, &modernReport);
    QVERIFY2(!modernReport.hasErrors(), qPrintable(modernReport.toText()));
    QVERIFY(!secondModern.isEmpty());
    QVERIFY(!secondModern.virtualVertices.isEmpty());
    QVERIFY(secondModern.virtualFields.isEmpty());
    QVERIFY(terrain.apply(secondModern, true));
    appendTerrainTileCommand(&modern, secondModern);
    QCOMPARE(modern.virtualFields.size(), 1);
    QVERIFY(modern.virtualVertices.isEmpty());
    CValidationReport releaseReport;
    const STerrainTileCommand release = terrain.makeVirtualStrokeResolution(
        modern, catalog, 0x46495253u, &releaseReport);
    QVERIFY2(!releaseReport.hasErrors(), qPrintable(releaseReport.toText()));
    QVERIFY2(!release.isEmpty(),
             "The first transactional Modern stamp lost its release ring");
    QVERIFY(terrain.apply(release, true));
    appendTerrainTileCommand(&modern, release);

    QVector<qint32> paintedCurrent;
    QVector<qint32> paintedBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &paintedCurrent, &paintedBaseline));
    const quint16 paintedManual = terrain.packedTile(
        8, 8, ETerrainTileLayer::Land);
    const quint16 paintedModern = terrain.packedTile(
        10, 8, ETerrainTileLayer::Land);

    QVERIFY2(terrain.apply(modern, false),
             "Undo of the first Modern transaction was refused");
    QVector<qint32> undoneCurrent;
    QVector<qint32> undoneBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &undoneCurrent, &undoneBaseline));
    QCOMPARE(undoneCurrent, fieldlessCurrent);
    QCOMPARE(undoneBaseline, fieldlessBaseline);
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land), quint16(15));

    QVERIFY2(terrain.apply(manual, false),
             "The preceding fieldless Manual command could not be undone");
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land), quint16(0));
    QVERIFY(terrain.apply(manual, true));
    QVERIFY(terrain.apply(modern, true));
    QCOMPARE(terrain.packedTile(8, 8, ETerrainTileLayer::Land), paintedManual);
    QCOMPARE(terrain.packedTile(10, 8, ETerrainTileLayer::Land), paintedModern);
    QVector<qint32> redoneCurrent;
    QVector<qint32> redoneBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &redoneCurrent, &redoneBaseline));
    QCOMPARE(redoneCurrent, paintedCurrent);
    QCOMPARE(redoneBaseline, paintedBaseline);

    // The viewport now collapses the first uniform preview and its exact
    // release resolution before touching the live model. Model that boundary
    // with the normal 7x7 square tile brush: one apply must establish the
    // complete semantic field and every core cell, never the partial first
    // stamp that previously became visible until the gesture was repeated.
    CTerrainEditModel atomicStamp;
    QVERIFY(atomicStamp.load(document));
    QVector<quint16> atomicBefore;
    for(int y = 0; y < atomicStamp.tileCountY(); ++y)
        for(int x = 0; x < atomicStamp.tileCountX(); ++x)
            atomicBefore.append(atomicStamp.packedTile(
                x, y, ETerrainTileLayer::Land));
    STerrainTileBrush sevenBySeven;
    sevenBySeven.centerX = 8.0f;
    sevenBySeven.centerY = 8.0f;
    sevenBySeven.radius = 3.0f;
    sevenBySeven.shape = ETerrainTileBrushShape::Square;
    CValidationReport atomicReport;
    STerrainTileCommand atomic = atomicStamp.makeVirtualTileMaterialPaint(
        sevenBySeven, ETerrainTileLayer::Land, 2, catalog,
        0x41544f4du, &atomicReport, true);
    QVERIFY2(!atomicReport.hasErrors(), qPrintable(atomicReport.toText()));
    QCOMPARE(atomic.semanticAudit.paintedCoreCells.size(), 49);
    CTerrainEditModel atomicPlan = atomicStamp;
    QVERIFY2(atomicPlan.apply(atomic, true, &atomicReport),
             qPrintable(atomicReport.toText()));
    const STerrainTileCommand atomicResolution =
        atomicPlan.makeVirtualStrokeResolution(
            atomic, catalog, 0x41544f4du, &atomicReport, true, 2);
    QVERIFY2(!atomicReport.hasErrors(), qPrintable(atomicReport.toText()));
    appendTerrainTileCommand(&atomic, atomicResolution);

    CTerrainVirtualMaterialField beforeAtomicField;
    QVERIFY(!atomicStamp.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &beforeAtomicField, false));
    QVERIFY2(atomicStamp.apply(atomic, true, &atomicReport),
             qPrintable(atomicReport.toText()));
    for(int y = 5; y <= 11; ++y)
        for(int x = 5; x <= 11; ++x)
        {
            bool available = false;
            const quint16 packed = atomicStamp.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            bool decoded = false;
            const STerrainTransitionSignature exact =
                catalog.gridSignatureForPacked(packed, &decoded);
            QVERIFY(decoded);
            QCOMPARE(exact.northWest, qint32(2));
            QCOMPARE(exact.northEast, qint32(2));
            QCOMPARE(exact.southEast, qint32(2));
            QCOMPARE(exact.southWest, qint32(2));
        }
    QVERIFY2(atomicStamp.apply(atomic, false, &atomicReport),
             qPrintable(atomicReport.toText()));
    QVector<quint16> atomicUndone;
    for(int y = 0; y < atomicStamp.tileCountY(); ++y)
        for(int x = 0; x < atomicStamp.tileCountX(); ++x)
            atomicUndone.append(atomicStamp.packedTile(
                x, y, ETerrainTileLayer::Land));
    QCOMPARE(atomicUndone, atomicBefore);
    QVERIFY(!atomicStamp.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &beforeAtomicField, false));
}

void EnduranceDocumentTests::refusedFirstModernStrokeLeavesFieldlessStateUntouched()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(32)),
        "refused-first-modern-fieldless.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        SMprPackedTile packed;
        packed.tileIndex = quint8(mask);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }
    // The semantic name is known, but no honest uniform artwork exists for
    // it. Planning therefore reaches the post-recovery resolver and refuses.
    catalog.declareMaterial(3);

    STerrainTileBrush cell;
    cell.centerX = 8.0f;
    cell.centerY = 8.0f;
    cell.radius = 0.0f;
    cell.shape = ETerrainTileBrushShape::Square;
    CValidationReport report;
    const STerrainTileCommand refused = terrain.makeVirtualTileMaterialPaint(
        cell, ETerrainTileLayer::Land, 3, catalog, 0x52454655u, &report);
    QVERIFY(refused.isEmpty());
    QVERIFY(report.hasErrors());

    QVector<qint32> current;
    QVector<qint32> baseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &current, &baseline));
    QVERIFY2(current.isEmpty() && baseline.isEmpty(),
             "A refused first Modern stamp left an unowned semantic field");
}

void EnduranceDocumentTests::firstResolvePreservesFreshManualTileSemanticCorners()
{
    // Zone 2 regression: slot 68 is stamped at 38,13 before any Modern
    // operation has created a shared-corner field.  The first Shift+R must
    // retain that explicit Manual tuple and resolve the neighbouring cells
    // outwards.  Previously, deterministic recovery counted the Manual cell
    // as just one vote, so its pre-existing neighbours overwrote all four
    // corners; a second Manual stamp then behaved differently only because
    // the first Resolve had created the field.
    CTerrainTransitionCatalog catalog;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        SMprPackedTile packed;
        packed.atlasIndex = mask == 15 ? 1 : 0;
        packed.tileIndex = quint8(mask == 15 ? 4 : mask);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    QVector<STerrainSectorSource> sources;
    for(int sectorX = 0; sectorX < 3; ++sectorX)
    {
        STerrainSectorSource source;
        source.memberName = QStringLiteral("first-manual-%1.sec")
            .arg(sectorX);
        source.sectorX = sectorX;
        source.sectorY = 0;
        source.type = 0;
        source.payload = syntheticLandSector(1200);
        source.landTileOffset = 5 + 33 * 33 * 8;
        source.expectedPayloadSha256 = QCryptographicHash::hash(
            source.payload, QCryptographicHash::Sha256).toHex();
        sources.append(source);
    }

    CTerrainEditModel terrain;
    CValidationReport setupReport;
    QVERIFY2(terrain.loadSources(3, 1, 100.0f, sources, &setupReport, 2,
                                 QVector<qint32>(128, 0), 0),
             qPrintable(setupReport.toText()));
    QCOMPARE(terrain.tileCountX(), 48);
    QCOMPARE(terrain.tileCountY(), 16);
    const CTerrainEditModel beforeManual = terrain;

    STerrainTileBrush manualCell;
    manualCell.centerX = 38.0f;
    manualCell.centerY = 13.0f;
    manualCell.radius = 0.0f;
    manualCell.shape = ETerrainTileBrushShape::Square;
    const STerrainTileCommand manual = terrain.makeTilePaint(
        manualCell, ETerrainTileLayer::Land, 1, 4, 0, catalog);
    QVERIFY(!manual.isEmpty());
    QCOMPARE(manual.pendingVirtualVertices.size(), 4);
    QVERIFY(terrain.apply(manual, true));

    bool available = false;
    QCOMPARE(terrain.packedTile(38, 13, ETerrainTileLayer::Land,
                                &available), quint16(68));
    QVERIFY(available);
    QVERIFY(!terrain.virtualMaterialFieldActive(ETerrainTileLayer::Land));

    CTerrainEditModel staged = terrain;
    CValidationReport recoveryReport;
    QVERIFY2(staged.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, catalog, &recoveryReport,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(recoveryReport.toText()));
    QVERIFY2(!recoveryReport.hasErrors(), qPrintable(recoveryReport.toText()));

    // This is the first incorrect value in the old path: each Manual corner
    // was reset to the surrounding material before the resolver planned any
    // physical repair.
    for(const QPoint& vertex : {QPoint(38, 13), QPoint(39, 13),
                                 QPoint(39, 14), QPoint(38, 14)})
    {
        bool found = false;
        QCOMPARE(staged.virtualMaterialAt(ETerrainTileLayer::Land,
                                          vertex.x(), vertex.y(), &found),
                 qint32(2));
        QVERIFY(found);
    }
    QVector<qint32> recoveredCurrent;
    QVector<qint32> recoveredBaseline;
    QVERIFY(staged.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &recoveredCurrent, &recoveredBaseline));
    const int recoveredVertexWidth = staged.tileCountX() + 1;
    for(const QPoint& vertex : {QPoint(38, 13), QPoint(39, 13),
                                 QPoint(39, 14), QPoint(38, 14)})
    {
        const int index = vertex.y() * recoveredVertexWidth + vertex.x();
        QCOMPARE(recoveredCurrent.at(index), qint32(2));
        QCOMPARE(recoveredBaseline.at(index), qint32(1));
    }

    CTerrainVirtualMaterialField repairField;
    QVERIFY(staged.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &repairField, true));
    const STerrainTileCommand firstResolve = staged.makeVirtualMapResolution(
        ETerrainTileLayer::Land, catalog, 0x534852u, &recoveryReport, false,
        &repairField, true, true);
    QVERIFY2(!recoveryReport.hasErrors(), qPrintable(recoveryReport.toText()));

    bool changedManualCell = false;
    bool expandedOutward = false;
    for(const STerrainTileDelta& delta : firstResolve.deltas)
    {
        if(delta.globalX == 38 && delta.globalY == 13)
            changedManualCell = true;
        if((delta.globalX == 37 || delta.globalX == 39)
           && delta.globalY == 13)
            expandedOutward = true;
    }
    QVERIFY(!changedManualCell);
    QVERIFY(expandedOutward);

    // Match CView's atomic Resolve command: it establishes the recovered
    // complete field together with the physical repair. Undo must then expose
    // the exact fieldless Manual command again, including its pending tuple.
    STerrainTileCommand resolveTransaction = firstResolve;
    QVERIFY(staged.apply(firstResolve, true));
    STerrainTileCommand::SVirtualFieldDelta fieldDelta;
    fieldDelta.layer = ETerrainTileLayer::Land;
    fieldDelta.vertexWidth = terrain.tileCountX() + 1;
    fieldDelta.vertexHeight = terrain.tileCountY() + 1;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &fieldDelta.beforeCurrent,
        &fieldDelta.beforeBaseline));
    QVERIFY(staged.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &fieldDelta.afterCurrent,
        &fieldDelta.afterBaseline));
    resolveTransaction.virtualFields.append(fieldDelta);
    QVERIFY(terrain.apply(resolveTransaction, true));
    QVERIFY(terrain.apply(resolveTransaction, false));
    QVERIFY(terrain.apply(manual, false));
    QVERIFY(terrain.apply(manual, true));

    // A second Manual placement on an already initialized field was the old
    // workaround. The first fieldless Manual placement must now produce the
    // identical physical Resolve plan, rather than collapsing inward first.
    CTerrainEditModel postFieldManual = beforeManual;
    CValidationReport postFieldReport;
    QVERIFY2(postFieldManual.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, catalog, &postFieldReport,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(postFieldReport.toText()));
    const STerrainTileCommand repeatedManual = postFieldManual.makeTilePaint(
        manualCell, ETerrainTileLayer::Land, 1, 4, 0, catalog);
    QVERIFY(!repeatedManual.isEmpty());
    QVERIFY(repeatedManual.pendingVirtualVertices.isEmpty());
    QVERIFY(postFieldManual.apply(repeatedManual, true));
    CTerrainVirtualMaterialField postFieldRepair;
    QVERIFY(postFieldManual.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &postFieldRepair, true));
    const STerrainTileCommand postFieldResolve =
        postFieldManual.makeVirtualMapResolution(
            ETerrainTileLayer::Land, catalog, 0x534852u, &postFieldReport,
            false, &postFieldRepair, true, true);
    QVERIFY2(!postFieldReport.hasErrors(), qPrintable(postFieldReport.toText()));
    QCOMPARE(firstResolve.deltas.size(), postFieldResolve.deltas.size());
    for(int index = 0; index < firstResolve.deltas.size(); ++index)
    {
        const STerrainTileDelta& first = firstResolve.deltas.at(index);
        const STerrainTileDelta& repeated = postFieldResolve.deltas.at(index);
        QCOMPARE(first.globalX, repeated.globalX);
        QCOMPARE(first.globalY, repeated.globalY);
        QCOMPARE(first.layer, repeated.layer);
        QCOMPARE(first.before, repeated.before);
        QCOMPARE(first.after, repeated.after);
        QCOMPARE(first.requestedSignature.northWest,
                 repeated.requestedSignature.northWest);
        QCOMPARE(first.requestedSignature.northEast,
                 repeated.requestedSignature.northEast);
        QCOMPARE(first.requestedSignature.southEast,
                 repeated.requestedSignature.southEast);
        QCOMPARE(first.requestedSignature.southWest,
                 repeated.requestedSignature.southWest);
    }
}

void EnduranceDocumentTests::laterManualTileWinsSharedCornersAndUndoRestoresPriorValues()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(),
                               "manual-shared-corner-order.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    for(qint32 material = 1; material <= 3; ++material)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = variant.signature.northEast =
            variant.signature.southEast = variant.signature.southWest =
                material;
        variant.packedTile = quint16(material - 1);
        QVERIFY(catalog.addVariant(variant));
    }

    // A legacy map with no persisted field must stay a physical-only Manual
    // edit. Reconstructing the complete map here was the multi-second click
    // stall and also produced a command whose expected field never existed in
    // the live model.
    CTerrainEditModel legacyTerrain;
    QVERIFY(legacyTerrain.load(document));
    STerrainTileBrush legacyCell;
    legacyCell.centerX = 8.0f;
    legacyCell.centerY = 8.0f;
    legacyCell.radius = 0.0f;
    const STerrainTileCommand legacyManual = legacyTerrain.makeTilePaint(
        legacyCell, ETerrainTileLayer::Land, 0, 1, 0, catalog);
    QVERIFY(!legacyManual.isEmpty());
    QVERIFY(legacyManual.virtualFields.isEmpty());
    QVERIFY(legacyManual.virtualVertices.isEmpty());
    QVERIFY(legacyTerrain.apply(legacyManual, true));
    QVERIFY(legacyTerrain.apply(legacyManual, false));

    CValidationReport baselineReport;
    QVERIFY(terrain.initializeVirtualMaterialField(
        ETerrainTileLayer::Land, catalog, &baselineReport,
        ETerrainMaterialFieldRecovery::RecoverDeterministically));
    QVERIFY2(!baselineReport.hasErrors(),
             qPrintable(baselineReport.toText()));

    STerrainTileBrush firstCell;
    firstCell.centerX = 8.0f;
    firstCell.centerY = 8.0f;
    firstCell.radius = 0.0f;
    const STerrainTileCommand first = terrain.makeTilePaint(
        firstCell, ETerrainTileLayer::Land, 0, 1, 0, catalog);
    QVERIFY(!first.isEmpty());
    QVERIFY(terrain.apply(first, true));

    STerrainTileBrush secondCell = firstCell;
    secondCell.centerX = 9.0f;
    const STerrainTileCommand second = terrain.makeTilePaint(
        secondCell, ETerrainTileLayer::Land, 0, 2, 0, catalog);
    QVERIFY(!second.isEmpty());
    QVERIFY(terrain.apply(second, true));

    // The later explicit cell supplies both vertices on the shared edge.
    // This is only the final semantic field: there is no timestamp, owner, or
    // placement sequence to persist.
    for(const QPoint& vertex : {QPoint(9, 8), QPoint(9, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(3));
        QVERIFY(found);
    }

    QVERIFY(terrain.apply(second, false));
    for(const QPoint& vertex : {QPoint(9, 8), QPoint(9, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(2));
        QVERIFY(found);
    }
    QVERIFY(terrain.apply(second, true));
    for(const QPoint& vertex : {QPoint(9, 8), QPoint(9, 9)})
    {
        bool found = false;
        QCOMPARE(terrain.virtualMaterialAt(
            ETerrainTileLayer::Land, vertex.x(), vertex.y(), &found),
                 qint32(3));
        QVERIFY(found);
    }
}

void EnduranceDocumentTests::manualTileStrokeStoresOnlyTouchedSemanticVertices()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(),
                               "manual-sparse-semantic-stroke.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    for(qint32 material = 1; material <= 3; ++material)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = variant.signature.northEast =
            variant.signature.southEast = variant.signature.southWest =
                material;
        variant.packedTile = quint16(material - 1);
        QVERIFY(catalog.addVariant(variant));
    }
    CValidationReport baselineReport;
    QVERIFY(terrain.initializeVirtualMaterialField(
        ETerrainTileLayer::Land, catalog, &baselineReport,
        ETerrainMaterialFieldRecovery::RecoverDeterministically));
    QVERIFY2(!baselineReport.hasErrors(),
             qPrintable(baselineReport.toText()));

    QVector<qint32> originalCurrent;
    QVector<qint32> originalBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &originalCurrent, &originalBaseline));

    STerrainTileCommand stroke;
    STerrainTileBrush cell;
    cell.centerX = 8.0f;
    cell.centerY = 8.0f;
    cell.radius = 0.0f;
    const STerrainTileCommand first = terrain.makeTilePaint(
        cell, ETerrainTileLayer::Land, 0, 1, 0, catalog);
    QVERIFY(!first.isEmpty());
    QVERIFY2(first.virtualFields.isEmpty(),
             "A one-cell Manual stamp stored a complete map-sized field");
    QCOMPARE(first.virtualVertices.size(), 4);
    QVERIFY(terrain.apply(first, true));
    appendTerrainTileCommand(&stroke, first);

    cell.centerX = 9.0f;
    const STerrainTileCommand second = terrain.makeTilePaint(
        cell, ETerrainTileLayer::Land, 0, 2, 0, catalog);
    QVERIFY(!second.isEmpty());
    QVERIFY2(second.virtualFields.isEmpty(),
             "A later Manual stamp stored a complete map-sized field");
    QCOMPARE(second.virtualVertices.size(), 4);
    QVERIFY(terrain.apply(second, true));
    appendTerrainTileCommand(&stroke, second);

    // Adjacent cells touch six unique vertices. The composed undo command
    // must stay proportional to that footprint, not to the complete map.
    QVERIFY(stroke.virtualFields.isEmpty());
    QCOMPARE(stroke.virtualVertices.size(), 6);
    QVector<qint32> paintedCurrent;
    QVector<qint32> paintedBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &paintedCurrent, &paintedBaseline));

    QVERIFY(terrain.apply(stroke, false));
    QVector<qint32> undoneCurrent;
    QVector<qint32> undoneBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &undoneCurrent, &undoneBaseline));
    QCOMPARE(undoneCurrent, originalCurrent);
    QCOMPARE(undoneBaseline, originalBaseline);

    QVERIFY(terrain.apply(stroke, true));
    QVector<qint32> redoneCurrent;
    QVector<qint32> redoneBaseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &redoneCurrent, &redoneBaseline));
    QCOMPARE(redoneCurrent, paintedCurrent);
    QCOMPARE(redoneBaseline, paintedBaseline);
}

void EnduranceDocumentTests::mapReferencedUnassignedAtlasSlotsExcludeUnusedAtlasCapacity()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(16)),
        "map-referenced-unassigned-slot.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    STerrainTransitionVariant assigned;
    assigned.signature.northWest = assigned.signature.northEast =
        assigned.signature.southEast = assigned.signature.southWest = 1;
    assigned.packedTile = 0;
    QVERIFY(catalog.addVariant(assigned));

    // Fifteen other physical slots exist in the synthetic atlas but are not
    // referenced by the map. They must not be reported merely because the
    // profile does not assign every atlas position.
    QVERIFY(terrain.referencedUnassignedAtlasSlots(
        ETerrainTileLayer::Land, catalog).isEmpty());

    STerrainTileBrush cell;
    cell.centerX = 8.0f;
    cell.centerY = 8.0f;
    cell.radius = 0.0f;
    const STerrainTileCommand manual = terrain.makeTilePaint(
        cell, ETerrainTileLayer::Land, 0, 2, 1);
    QVERIFY(!manual.isEmpty());
    QVERIFY(terrain.apply(manual, true));
    const QMap<int, QPoint> unresolved =
        terrain.referencedUnassignedAtlasSlots(
            ETerrainTileLayer::Land, catalog);
    QCOMPARE(unresolved.size(), 1);
    QCOMPARE(unresolved.value(2), QPoint(8, 8));

    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int setTileStart = source.indexOf(
        "void CView::setTile(QVector3D posOnLand, bool bLand)");
    const int setTileEnd = source.indexOf(
        "void CView::setTile(QMap<STileLocation, STileInfo>& arrTileData)",
        setTileStart);
    QVERIFY(setTileStart >= 0 && setTileEnd > setTileStart);
    const QByteArray setTile = source.mid(setTileStart,
                                          setTileEnd - setTileStart);
    const int assignmentGate = setTile.indexOf(
        "ensureModernTerrainArtworkAssigned(layer, semanticCatalog)");
    const int modernPaint = setTile.indexOf(
        "makeVirtualTileMaterialPaint(", assignmentGate);
    QVERIFY2(assignmentGate >= 0 && modernPaint > assignmentGate,
             "Modern paint can initialize semantics before checking live unassigned artwork");
    QVERIFY2(source.contains("reviewTerrainAtlasProfileAtSlot"),
             "Live unassigned artwork is not routed to the Material atlas assignment UI");
    QVERIFY2(source.contains("tileImages.at(slot).isNull()"),
             "The Modern assignment gate does not distinguish real artwork from null atlas padding");
    const int resolveStart = source.indexOf(
        "void CView::resolveMissingTerrainBlends()");
    const int resolveEnd = source.indexOf(
        "void CView::beginTileBrushGroup()",
        resolveStart);
    QVERIFY(resolveStart >= 0 && resolveEnd > resolveStart);
    const QByteArray resolve = source.mid(resolveStart,
                                           resolveEnd - resolveStart);
    QVERIFY2(resolve.contains("ensureModernTerrainArtworkAssigned("),
             "Resolve Missing Blends can silently process map-referenced unassigned artwork");
}

void EnduranceDocumentTests::classicAutoStrokeUsesAuthoredResolverAndRerollsExactVariants()
{
    CTerrainTransitionCatalog catalog;
    const auto add = [&catalog](const STerrainTransitionSignature& signature,
                                quint16 packed) {
        STerrainTransitionVariant variant;
        variant.signature = signature;
        variant.packedTile = packed;
        return catalog.addVariant(variant);
    };
    STerrainTransitionSignature background;
    background.northWest = background.northEast = 1;
    background.southEast = background.southWest = 1;
    STerrainTransitionSignature selected;
    selected.northWest = selected.northEast = 2;
    selected.southEast = selected.southWest = 2;
    STerrainTransitionSignature authored;
    authored.northWest = 2;
    authored.northEast = authored.southEast = authored.southWest = 1;
    QVERIFY(add(background, 0));
    QVERIFY(add(selected, 1));
    QVERIFY(add(selected, 2));
    for(int rotation = 0; rotation < 4; ++rotation)
        QVERIFY(add(CTerrainTransitionCatalog::rotated(authored, rotation),
                    quint16(3 | (rotation << 14))));

    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(16)),
        "classic-auto-authored-stroke.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    const QVector<QPoint> requested({QPoint(8, 8)});
    CValidationReport firstReport;
    const STerrainTileCommand first = terrain.makeSemanticVertexStrokePaint(
        requested, requested, ETerrainTileLayer::Land, 2, catalog,
        0x434C4153u, &firstReport);
    QVERIFY2(!firstReport.hasErrors(), qPrintable(firstReport.toText()));
    QVERIFY(!first.isEmpty());
    for(const STerrainTileDelta& delta : first.deltas)
    {
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &found);
        QVERIFY(found);
        QVERIFY2(!(signature.northWest == 2 && signature.northEast == 2
                   && signature.southEast == 2 && signature.southWest == 2),
                 "Classic Auto substituted a uniform tile for an authored blend");
    }
    QVERIFY(terrain.apply(first, true));

    STerrainTileCommand uniformSetup;
    for(const QPoint& cell : {QPoint(7, 7), QPoint(8, 7),
                              QPoint(7, 8), QPoint(8, 8)})
    {
        STerrainTileDelta delta;
        delta.globalX = cell.x();
        delta.globalY = cell.y();
        delta.layer = ETerrainTileLayer::Land;
        delta.before = terrain.packedTile(
            cell.x(), cell.y(), ETerrainTileLayer::Land);
        delta.after = 1;
        if(delta.before != delta.after)
            uniformSetup.deltas.append(delta);
    }
    QVERIFY(terrain.apply(uniformSetup, true));

    // A later committed stroke over an already-selected semantic region must
    // run exact candidate selection again instead of being collapsed solely
    // because its corner identities are unchanged.
    bool rerolled = false;
    for(quint32 seed = 1; seed < 128 && !rerolled; ++seed)
    {
        CValidationReport repaintReport;
        const STerrainTileCommand repaint =
            terrain.makeSemanticVertexStrokePaint(
                requested, requested, ETerrainTileLayer::Land, 2, catalog,
                seed, &repaintReport);
        QVERIFY2(!repaintReport.hasErrors(),
                 qPrintable(repaintReport.toText()));
        rerolled = !repaint.isEmpty();
    }
    QVERIFY2(rerolled,
             "Classic Auto collapsed every same-semantic repaint to a no-op");

    CTerrainTransitionCatalog missingBlend;
    STerrainTransitionVariant backgroundOnly;
    backgroundOnly.signature = background;
    backgroundOnly.packedTile = 0;
    QVERIFY(missingBlend.addVariant(backgroundOnly));
    STerrainTransitionVariant uniformOnly;
    uniformOnly.signature = selected;
    uniformOnly.packedTile = 1;
    QVERIFY(missingBlend.addVariant(uniformOnly));
    STerrainTransitionVariant alternateUniform = uniformOnly;
    alternateUniform.packedTile = 2;
    QVERIFY(missingBlend.addVariant(alternateUniform));
    CTerrainEditModel fallbackTerrain;
    QVERIFY(fallbackTerrain.load(document));
    CValidationReport fallbackReport;
    const STerrainTileCommand fallback =
        fallbackTerrain.makeSemanticVertexStrokePaint(
            requested, requested, ETerrainTileLayer::Land, 2,
            missingBlend, 0x53545243u, &fallbackReport);
    QVERIFY2(!fallback.isEmpty(), qPrintable(fallbackReport.toText()));
    QVERIFY(!fallbackReport.hasErrors());
    QVERIFY(fallbackReport.toText().contains(
        QStringLiteral("terrain.semantic_stroke_hard_edge")));
    QCOMPARE(fallback.semanticAudit.solverMode,
             QStringLiteral("vertex-stroke-local-hard-edge"));

    CTerrainEditModel safeTerrain;
    QVERIFY(safeTerrain.load(document));
    CValidationReport safeReport;
    const STerrainTileCommand safe = safeTerrain.makeSemanticVertexStrokePaint(
        requested, requested, ETerrainTileLayer::Land, 2, missingBlend,
        0x53414645u, &safeReport,
        ETerrainSemanticPaintPolicy::ExactExistingOnly);
    QVERIFY2(!safeReport.hasErrors(), qPrintable(safeReport.toText()));
    QVERIFY(safe.isEmpty());
    QVERIFY(safeReport.toText().contains(
        QStringLiteral("terrain.semantic_stroke_exact_only_skip")));

    // Safe mode is partial rather than atomic: a distant unsupported edge
    // must not discard an independent exact same-material reroll elsewhere in
    // the same stroke. It also must never fill the unsupported edge with the
    // selected uniform artwork.
    CTerrainEditModel partialTerrain;
    QVERIFY(partialTerrain.load(document));
    STerrainTileCommand selectedIsland;
    for(const QPoint& cell : {QPoint(7, 7), QPoint(8, 7),
                              QPoint(7, 8), QPoint(8, 8)})
    {
        STerrainTileDelta delta;
        delta.globalX = cell.x();
        delta.globalY = cell.y();
        delta.layer = ETerrainTileLayer::Land;
        delta.before = partialTerrain.packedTile(
            cell.x(), cell.y(), ETerrainTileLayer::Land);
        delta.after = 1;
        selectedIsland.deltas.append(delta);
    }
    QVERIFY(partialTerrain.apply(selectedIsland, true));
    const QVector<QPoint> partialRequest({QPoint(8, 8), QPoint(12, 12)});
    bool partialApplied = false;
    for(quint32 seed = 1; seed < 128 && !partialApplied; ++seed)
    {
        CValidationReport partialReport;
        const STerrainTileCommand partial =
            partialTerrain.makeSemanticVertexStrokePaint(
                partialRequest, partialRequest, ETerrainTileLayer::Land, 2,
                missingBlend, seed, &partialReport,
                ETerrainSemanticPaintPolicy::ExactExistingOnly);
        QVERIFY2(!partialReport.hasErrors(), qPrintable(partialReport.toText()));
        QVERIFY(partialReport.toText().contains(
            QStringLiteral("terrain.semantic_stroke_exact_only_skip")));
        if(partial.isEmpty())
            continue;
        partialApplied = true;
        for(const STerrainTileDelta& delta : partial.deltas)
        {
            QVERIFY(delta.globalX >= 7 && delta.globalX <= 8);
            QVERIFY(delta.globalY >= 7 && delta.globalY <= 8);
            bool decoded = false;
            const STerrainTransitionSignature actual =
                missingBlend.gridSignatureForPacked(delta.after, &decoded);
            QVERIFY(decoded);
            QCOMPARE(actual, selected);
        }
    }
    QVERIFY2(partialApplied,
             "Safe Classic Auto discarded every valid part of a mixed-validity stroke");
}

void EnduranceDocumentTests::classicAutoLivePreviewUsesCommittedPackedOutcome()
{
    CTerrainTransitionCatalog catalog;
    const auto add = [&catalog](const STerrainTransitionSignature& signature,
                                quint16 packed) {
        STerrainTransitionVariant variant;
        variant.signature = signature;
        variant.packedTile = packed;
        return catalog.addVariant(variant);
    };
    STerrainTransitionSignature background;
    background.northWest = background.northEast = 1;
    background.southEast = background.southWest = 1;
    STerrainTransitionSignature selected;
    selected.northWest = selected.northEast = 2;
    selected.southEast = selected.southWest = 2;
    STerrainTransitionSignature blend;
    blend.northWest = 2;
    blend.northEast = blend.southEast = blend.southWest = 1;
    QVERIFY(add(background, 0));
    QVERIFY(add(selected, 1));
    for(int rotation = 0; rotation < 4; ++rotation)
        QVERIFY(add(CTerrainTransitionCatalog::rotated(blend, rotation),
                    quint16(2 | (rotation << 14))));

    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(16)),
        "classic-auto-live-preview-parity.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));
    const QVector<QPoint> vertices({QPoint(8, 8)});
    CValidationReport report;
    const STerrainTileCommand preview = terrain.makeSemanticVertexStrokePaint(
        vertices, vertices, ETerrainTileLayer::Land, 2, catalog,
        0x50525657u, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(!preview.isEmpty());

    // The live transaction must retain this exact packed command for release;
    // rebuilding the preview as uniform physical tiles is visibly different.
    for(const STerrainTileDelta& delta : preview.deltas)
    {
        bool decoded = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(delta.after, &decoded);
        QVERIFY(decoded);
        QVERIFY(!(signature == selected));
    }
    QVERIFY(terrain.apply(preview, true));
    for(const STerrainTileDelta& delta : preview.deltas)
        QCOMPARE(terrain.packedTile(delta.globalX, delta.globalY, delta.layer),
                 delta.after);

    // Release promotes the already-visible command; undo and redo replay its
    // captured packed values without resolving or rerolling candidates.
    QVERIFY(terrain.apply(preview, false));
    for(const STerrainTileDelta& delta : preview.deltas)
        QCOMPARE(terrain.packedTile(delta.globalX, delta.globalY, delta.layer),
                 delta.before);
    QVERIFY(terrain.apply(preview, true));
    for(const STerrainTileDelta& delta : preview.deltas)
        QCOMPARE(terrain.packedTile(delta.globalX, delta.globalY, delta.layer),
                 delta.after);
}

void EnduranceDocumentTests::abandonedTerrainPreviewRollsBackBeforeNextStroke()
{
    CTerrainTransitionCatalog catalog;
    STerrainTransitionSignature background;
    background.northWest = background.northEast = 1;
    background.southEast = background.southWest = 1;
    STerrainTransitionVariant backgroundVariant;
    backgroundVariant.signature = background;
    backgroundVariant.packedTile = 0;
    QVERIFY(catalog.addVariant(backgroundVariant));
    STerrainTransitionSignature selected;
    selected.northWest = selected.northEast = 2;
    selected.southEast = selected.southWest = 2;
    STerrainTransitionVariant selectedVariant;
    selectedVariant.signature = selected;
    selectedVariant.packedTile = 1;
    QVERIFY(catalog.addVariant(selectedVariant));

    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(16)),
        "abandoned-terrain-preview-rollback.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));
    const QVector<QPoint> abandonedVertices({QPoint(8, 8)});
    CValidationReport abandonedReport;
    const STerrainTileCommand abandoned =
        terrain.makeSemanticVertexStrokePaint(
            abandonedVertices, abandonedVertices, ETerrainTileLayer::Land,
            2, catalog, 0x4142414Eu, &abandonedReport);
    QVERIFY2(!abandonedReport.hasErrors(),
             qPrintable(abandonedReport.toText()));
    QVERIFY(!abandoned.isEmpty());
    QVERIFY(terrain.apply(abandoned, true));

    // Application/window deactivation cancels instead of committing. The next
    // press starts from the exact pre-preview state and owns only its changes.
    QVERIFY(terrain.apply(abandoned, false));
    const QVector<QPoint> nextVertices({QPoint(12, 12)});
    CValidationReport nextReport;
    const STerrainTileCommand next = terrain.makeSemanticVertexStrokePaint(
        nextVertices, nextVertices, ETerrainTileLayer::Land, 2, catalog,
        0x4E455854u, &nextReport);
    QVERIFY2(!nextReport.hasErrors(), qPrintable(nextReport.toText()));
    QVERIFY(!next.isEmpty());
    QVERIFY(terrain.apply(next, true));
    for(const STerrainTileDelta& delta : abandoned.deltas)
    {
        if(qAbs(delta.globalX - 12) <= 1 && qAbs(delta.globalY - 12) <= 1)
            continue;
        QCOMPARE(terrain.packedTile(delta.globalX, delta.globalY, delta.layer),
                 delta.before);
    }
    QVERIFY(terrain.apply(next, false));
    for(const STerrainTileDelta& delta : next.deltas)
        QCOMPARE(terrain.packedTile(delta.globalX, delta.globalY, delta.layer),
                 delta.before);
    QVERIFY(terrain.apply(next, true));
    for(const STerrainTileDelta& delta : next.deltas)
        QCOMPARE(terrain.packedTile(delta.globalX, delta.globalY, delta.layer),
                 delta.after);
}

void EnduranceDocumentTests::interruptedTerrainStrokeRejectsUnownedStamp()
{
    // A desktop capture/modal interruption can cancel the active preview while
    // Qt still delivers one old mouse move or release. That input is not a new
    // gesture and therefore cannot be permitted to mutate the terrain model.
    const auto ownsTileStamp = [](bool strokeActive, bool strokeFinishing) {
        return strokeActive && !strokeFinishing;
    };
    QVERIFY(ownsTileStamp(true, false));
    QVERIFY(!ownsTileStamp(false, false));
    QVERIFY(!ownsTileStamp(true, true));

    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int methodStart = source.indexOf(
        "void CView::setTile(QVector3D posOnLand, bool bLand)");
    const int methodEnd = source.indexOf(
        "void CView::setTile(QMap<STileLocation, STileInfo>& arrTileData)",
        methodStart);
    QVERIFY2(methodStart >= 0 && methodEnd > methodStart,
             "Terrain paint entry point was not found");
    const QByteArray method = source.mid(methodStart, methodEnd - methodStart);
    const int ownershipGate = method.indexOf(
        "if(!m_tilePaintStrokeActive || m_tileStrokeFinishing)");
    const int ownershipReturn = method.indexOf("return;", ownershipGate);
    const int footprintResolution = method.indexOf(
        "resolveTerrainPaintFootprint");
    const int previewMutation = method.indexOf(
        "applyTerrainTilePreview(command, true)");
    QVERIFY2(ownershipGate >= 0 && ownershipReturn > ownershipGate,
             "A stale terrain paint event can enter setTile without owning an active stroke");
    QVERIFY2(ownershipGate < footprintResolution
             && ownershipGate < previewMutation,
             "Terrain paint ownership is checked after preview/model work has begun");
}

void EnduranceDocumentTests::workingAtlasPublicationPreservesActiveStrokeTransaction()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int refreshStart = source.indexOf(
        "void CView::refreshWorkingTerrainPalettes()");
    const int refreshEnd = source.indexOf(
        "void CView::refreshTerrainFamilyUsage()", refreshStart);
    QVERIFY(refreshStart >= 0 && refreshEnd > refreshStart);
    const QByteArray refresh = source.mid(refreshStart,
                                           refreshEnd - refreshStart);
    const int activeGate = refresh.indexOf(
        "if(m_tilePaintStrokeActive || m_tileStrokeFinishing)");
    const int pending = refresh.indexOf(
        "m_workingAtlasPaletteRefreshPending = true", activeGate);
    const int paletteMutation = refresh.indexOf(
        "refreshClassicAutoPalette()", activeGate);
    QVERIFY2(activeGate >= 0 && pending > activeGate
                 && paletteMutation > pending,
             "Working-atlas palette mutation can still cancel its active stroke");

    const int syncStart = source.indexOf(
        "bool CView::syncWorkingTerrainAtlas(");
    const int syncEnd = source.indexOf(
        "void CView::finalizeVirtualTerrainAtlas()", syncStart);
    QVERIFY(syncStart >= 0 && syncEnd > syncStart);
    const QByteArray sync = source.mid(syncStart, syncEnd - syncStart);
    QVERIFY2(sync.contains("installWorkingAtlasSlotTypes(activeTypes)"),
             "Working-atlas publication exposes variants before the MPR model receives their runtime types");
    QVERIFY2(sync.contains("refreshWorkingTerrainPalettes();"),
             "Working-atlas publication bypasses the transaction-aware palette gate");
    QVERIFY2(!sync.contains("refreshClassicAutoPalette();"),
             "Working-atlas sync still mutates the Classic palette directly");

    const int releaseStart = source.indexOf("void CView::endTileBrushGroup()");
    const int releaseEnd = source.indexOf("void CView::cancelTileBrushGroup()",
                                          releaseStart);
    QVERIFY(releaseStart >= 0 && releaseEnd > releaseStart);
    const QByteArray release = source.mid(releaseStart,
                                           releaseEnd - releaseStart);
    const int released = release.indexOf("m_tilePaintStrokeActive = false;");
    const int undoOwned = release.indexOf("m_pUndoStack->push(", released);
    const int flushed = release.indexOf("refreshWorkingTerrainPalettes();",
                                        released);
    QVERIFY2(released >= 0 && undoOwned > released && flushed > undoOwned,
             "Deferred palette refresh can run before the completed stroke owns its undo entry");
}

void EnduranceDocumentTests::terrainStrokePublishesAtlasAndSectorAsOneFrame()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int releaseStart = source.indexOf("void CView::endTileBrushGroup()");
    const int releaseEnd = source.indexOf("void CView::cancelTileBrushGroup()",
                                          releaseStart);
    QVERIFY(releaseStart >= 0 && releaseEnd > releaseStart);
    const QByteArray release = source.mid(releaseStart,
                                           releaseEnd - releaseStart);
    const int resolution = release.indexOf("makeVirtualStrokeResolution(");
    const int applyResolution = release.indexOf(
        "resolutionApplied = applyTerrainTilePreview(", resolution);
    const int publishAtlas = release.indexOf(
        "syncWorkingTerrainAtlas(", resolution);
    QVERIFY2(resolution >= 0 && applyResolution > resolution
                 && publishAtlas > applyResolution,
             "Terrain release can publish a new atlas and present a frame before the matching sector buffers are rebuilt");
    const QByteArray atomicWindow = release.mid(
        applyResolution, publishAtlas - applyResolution);
    QVERIFY2(!atomicWindow.contains("processEvents("),
             "Terrain release yields a visible frame between sector and atlas publication");

    const int paintStart = source.indexOf(
        "void CView::setTile(QVector3D posOnLand, bool bLand)");
    const int paintEnd = source.indexOf(
        "void CView::setTile(QMap<STileLocation, STileInfo>& arrTileData)",
        paintStart);
    QVERIFY(paintStart >= 0 && paintEnd > paintStart);
    const QByteArray paint = source.mid(paintStart, paintEnd - paintStart);
    const int exactStampPreparation = paint.indexOf(
        "prepareExactModernTerrainStamp(");
    const int firstPaintApply = paint.indexOf(
        "applyTerrainTilePreview(command, true)");
    const int firstPaintPublish = paint.lastIndexOf(
        "syncWorkingTerrainAtlas(");
    QVERIFY2(exactStampPreparation >= 0
                 && firstPaintApply > exactStampPreparation
                 && firstPaintPublish > firstPaintApply,
             "Modern paint still publishes its temporary uniform preview before exact blend resolution, exposing an incomplete first stamp for one frame");
    QVERIFY2(firstPaintApply >= 0 && firstPaintPublish > firstPaintApply,
             "A direct Modern stamp can publish its atlas before the matching cell/VBO update");
    const QByteArray paintAtomicWindow = paint.mid(
        firstPaintApply, firstPaintPublish - firstPaintApply);
    QVERIFY2(!paintAtomicWindow.contains("processEvents("),
             "A direct Modern stamp yields a visible frame between cell and atlas publication");

    const int syncStart = source.indexOf(
        "bool CView::syncWorkingTerrainAtlas(");
    const int syncEnd = source.indexOf(
        "void CView::finalizeVirtualTerrainAtlas()", syncStart);
    QVERIFY(syncStart >= 0 && syncEnd > syncStart);
    const QByteArray sync = source.mid(syncStart, syncEnd - syncStart);
    QVERIFY2(sync.contains("presentCompletedTerrainPublication"),
             "A completed live terrain atlas/cell transaction is only queued for a later frame, allowing the compositor to expose the offscreen upload interval");
}

void EnduranceDocumentTests::resolveUsesPreRouteBoundedPathForGroundAndOverlay()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int resolveStart = source.indexOf(
        "void CView::resolveMissingTerrainBlends()");
    const int resolveEnd = source.indexOf(
        "void CView::beginTileBrushGroup()", resolveStart);
    QVERIFY(resolveStart >= 0 && resolveEnd > resolveStart);
    const QByteArray resolve = source.mid(resolveStart,
                                           resolveEnd - resolveStart);
    QVERIFY2(!resolve.contains("planConnectedTerrainRouteOwnership("),
             "Shift+R still invokes the connected-route planner instead of the bounded pre-route repair path");
    QVERIFY2(!resolve.contains("routeOwnershipEvidenceCells")
                 && !resolve.contains("routeOwners[")
                 && !resolve.contains("forceRouteSelection[")
                 && !resolve.contains("preserveRouteComponents["),
             "Shift+R still builds or applies map-wide route-ownership state");
    QVERIFY2(!resolve.contains("terrain.resolve_route_ownership_preserved"),
             "Ordinary Shift+R can still emit connected-route ambiguity warnings");
    QVERIFY2(resolve.contains("for(ETerrainTileLayer layer : layers)"),
             "Ground and Overlay no longer share the same bounded repair path");
    QVERIFY2(resolve.contains("missingBlendRepairSignature("),
             "Shift+R no longer limits tuple discovery to cells whose packed artwork disagrees with the recovered semantic field");
    const int mapResolution = resolve.indexOf("makeVirtualMapResolution(");
    QVERIFY2(mapResolution >= 0,
             "Shift+R no longer resolves its bounded repair candidates");
    const QByteArray mapResolutionCall = resolve.mid(mapResolution, 700);
    QVERIFY2(!mapResolutionCall.contains("routeOwners")
                 && !mapResolutionCall.contains("forceRouteSelection")
                 && !mapResolutionCall.contains("preserveRouteComponents"),
             "Shift+R still passes connected-route overrides into ordinary map resolution");
}

void EnduranceDocumentTests::workingAtlasPreviewPublishesOnlyChangedPhysicalTiles()
{
    const QByteArray view = viewSource();
    const QByteArray landscape = repositorySource(
        QStringLiteral("source/render/landscape.cpp"));
    const QByteArray header = repositorySource(
        QStringLiteral("source/render/landscape.h"));
    QVERIFY2(!view.isEmpty() && !landscape.isEmpty() && !header.isEmpty(),
             "Could not locate terrain preview publication sources");
    QVERIFY2(header.contains("const QSet<int>& changedSlots"),
             "The landscape preview API cannot receive physical change ownership");
    QVERIFY2(view.contains("activeImages, atlas->activeTextureCount(), changed"),
             "Working-atlas publication still drops its changed-slot set");
    QVERIFY2(landscape.contains("glTexSubImage2D"),
             "Unchanged atlas layouts still rebuild every physical page");
    QVERIFY2(landscape.contains("glCopyTexSubImage2D"),
             "The first extension regrid still re-uploads all authored pages from CPU images");
    QVERIFY2(landscape.contains("GL_FRAMEBUFFER_BINDING")
             && landscape.contains("GL_TEXTURE_BINDING_2D"),
             "The incremental preview path does not preserve caller GL state");
    QVERIFY2(landscape.contains("previewTerrainAtlasFullUpload"),
             "Incremental publication has no fail-closed full-upload fallback");
    const int incrementalStart = landscape.indexOf(
        "bool CLandscape::previewTerrainAtlasIncremental(");
    const int regridStart = landscape.indexOf(
        "bool CLandscape::previewTerrainAtlasRegridCopy(", incrementalStart);
    QVERIFY(incrementalStart >= 0 && regridStart > incrementalStart);
    const QByteArray incremental = landscape.mid(
        incrementalStart, regridStart - incrementalStart);
    QVERIFY2(!incremental.contains("QOpenGLTexture* candidate")
                 && !incremental.contains("glBindFramebuffer"),
             "A same-layout stamp still replaces the atlas texture or touches an offscreen framebuffer");
    QVERIFY2(incremental.contains(
                 "const GLuint previewTextureId = m_previewAtlasTexture->textureId()")
                 && incremental.contains("uploadChangedSlots(tileImages)")
                 && incremental.contains("uploadChangedSlots(rollbackTileImages)"),
             "Same-layout publication no longer keeps a stable texture identity with explicit changed-slot rollback pixels");
    const int failedUpload = incremental.indexOf("if(!installed)");
    const int rollback = incremental.indexOf(
        "rollbackInstalled = rollbackChangedSlots()", failedUpload);
    const int restoreTexture = incremental.indexOf(
        "gl->glBindTexture(GL_TEXTURE_2D, GLuint(priorTexture))", rollback);
    const int failureReturn = incremental.indexOf("return false", restoreTexture);
    QVERIFY2(failedUpload >= 0 && rollback > failedUpload
                 && restoreTexture > rollback && failureReturn > restoreTexture,
             "Incremental GL failure no longer restores the prior changed-slot pixels, mipmaps, and caller binding before rejection");
    QVERIFY2(incremental.contains("glGenerateMipmap(GL_TEXTURE_2D)")
                 && incremental.contains("framebufferTouched:false"),
             "Same-layout publication no longer refreshes mipmaps or truthfully records that it left framebuffer ownership untouched");
    const int stateCapture = landscape.indexOf(
        "gl->glGetIntegerv(GL_TEXTURE_BINDING_2D", regridStart);
    const int regridAllocation = landscape.indexOf(
        "QOpenGLTexture* candidate", regridStart);
    QVERIFY2(stateCapture >= 0 && regridAllocation > stateCapture,
             "Regrid candidate allocation occurs before caller GL state is captured");

    const int prewarmStart = view.indexOf(
        "void CView::prewarmModernTerrainFamily(");
    const int adoptStart = view.indexOf(
        "bool CView::adoptPrewarmedTerrainFamily(", prewarmStart);
    const int nextMethod = view.indexOf(
        "void CView::refreshClassicAutoPalette()", adoptStart);
    QVERIFY(prewarmStart >= 0 && adoptStart > prewarmStart
            && nextMethod > adoptStart);
    const QByteArray prewarm = view.mid(prewarmStart,
                                        adoptStart - prewarmStart);
    const QByteArray adopt = view.mid(adoptStart, nextMethod - adoptStart);
    QVERIFY2(!prewarm.contains("syncWorkingTerrainAtlas("),
             "Selection prewarm exposes uncommitted slots in renderer/picker");
    QVERIFY2(prewarm.contains("QtConcurrent::run(")
             && prewarm.contains("staged->atlas->resolve("),
             "Exact-family realization still blocks the GUI event after selection");
    const int authoredImport = prewarm.indexOf(
        "addAuthoredLibraryTransition(");
    const int authoredRealize = prewarm.indexOf(
        "realizeAuthoredLibraryVariants(", authoredImport);
    const int canonicalResolve = prewarm.indexOf(
        "staged->atlas->resolve(", authoredRealize);
    QVERIFY2(authoredImport >= 0 && authoredRealize > authoredImport
             && canonicalResolve > authoredRealize,
             "Worker prewarm bypasses approved-HD authored exact candidates before canonical generation");
    QVERIFY2(prewarm.contains("guiMs:%4 workerMs:%5"),
             "Prewarm telemetry does not separate GUI snapshot and worker time");
    const int liveCommit = adopt.indexOf("commitStaging(*staged->atlas)");
    QVERIFY2(liveCommit >= 0
             && !adopt.contains("syncWorkingTerrainAtlas("),
             "Prepared family still publishes its atlas before the matching first paint cells exist");
}

void EnduranceDocumentTests::resolveMissingBlendsStagesDeterministicLoadBaseline()
{
    // Zone 2 regression shape: an initially decoded hard border has no
    // model-owned virtual material field after load.  Conservative recovery
    // cannot decide its disputed vertices, while the deterministic field
    // seeded by the first semantic paint resolves the same cells.
    CTerrainTransitionCatalog catalog;
    int nextSlot = 2;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        if(mask == 0)
            variant.packedTile = 0;
        else if(mask == 15)
            variant.packedTile = 1;
        else if(mask == 6)
            variant.packedTile = 4; // 1/2/2/1 vertical transition
        else
        {
            while(nextSlot == 4)
                ++nextSlot;
            variant.packedTile = quint16(nextSlot++);
        }
        QVERIFY(catalog.addVariant(variant));
    }

    QVector<STerrainSectorSource> sources;
    for(int sectorY = 0; sectorY < 2; ++sectorY)
        for(int sectorX = 0; sectorX < 2; ++sectorX)
        {
            STerrainSectorSource source;
            source.memberName = QStringLiteral("load-baseline-%1-%2.sec")
                .arg(sectorX).arg(sectorY);
            source.sectorX = sectorX;
            source.sectorY = sectorY;
            source.type = 0;
            source.payload = syntheticLandSector(1200);
            source.landTileOffset = 5 + 33 * 33 * 8;
            source.expectedPayloadSha256 = QCryptographicHash::hash(
                source.payload, QCryptographicHash::Sha256).toHex();
            sources.append(source);
        }

    CValidationReport setupReport;
    CTerrainEditModel loaded;
    QVERIFY2(loaded.loadSources(2, 2, 100.0f, sources, &setupReport, 1,
                                QVector<qint32>(64, 1), 0),
             qPrintable(setupReport.toText()));
    QCOMPARE(loaded.tileCountX(), 32);
    QCOMPARE(loaded.tileCountY(), 32);

    // Physical slot 0 is uniform material 1.  Build a material-2 side at
    // x=28, then corrupt the reported second cell back to material 1.
    STerrainTileCommand border;
    for(int y = 0; y < loaded.tileCountY(); ++y)
        for(int x = 28; x < loaded.tileCountX(); ++x)
        {
            STerrainTileDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.layer = ETerrainTileLayer::Land;
            delta.before = 0;
            delta.after = 1;
            border.deltas.append(delta);
        }
    QVERIFY2(loaded.apply(border, true, &setupReport),
             qPrintable(setupReport.toText()));
    STerrainTileCommand corruption;
    STerrainTileDelta corrupt;
    corrupt.globalX = 29;
    corrupt.globalY = 17;
    corrupt.layer = ETerrainTileLayer::Land;
    corrupt.before = 1;
    corrupt.after = 0;
    corruption.deltas.append(corrupt);
    QVERIFY2(loaded.apply(corruption, true, &setupReport),
             qPrintable(setupReport.toText()));

    const auto targetAfter = [](const STerrainTileCommand& command,
                                int x, int y, quint16* after) {
        for(const STerrainTileDelta& delta : command.deltas)
            if(delta.layer == ETerrainTileLayer::Land
               && delta.globalX == x && delta.globalY == y)
            {
                if(after)
                    *after = delta.after;
                return true;
            }
        return false;
    };

    // This is the pre-fix first-Shift+R path: no loaded virtual field means
    // only the partial unambiguous recovery is available.
    CTerrainVirtualMaterialField conservativeField;
    QVERIFY(!loaded.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &conservativeField, true));
    CValidationReport conservativeReport;
    QVERIFY2(loaded.recoverUnambiguousVirtualMaterialField(
                 ETerrainTileLayer::Land, catalog, &conservativeField,
                 &conservativeReport),
             qPrintable(conservativeReport.toText()));
    const STerrainTileCommand conservative = loaded.makeVirtualMapResolution(
        ETerrainTileLayer::Land, catalog, 0x53484946u, &conservativeReport,
        false, &conservativeField, true, false);
    QVERIFY2(!conservativeReport.hasErrors(),
             qPrintable(conservativeReport.toText()));
    quint16 ignored = 0;
    QVERIFY2(!targetAfter(conservative, 28, 17, &ignored)
             && !targetAfter(conservative, 29, 17, &ignored),
             "The fixture no longer distinguishes a fresh load from a first semantic paint");

    // The fixed first-Shift+R path seeds only its staged copy.  It must plan
    // the same exact target cells as a map whose first Modern paint has
    // already initialized the field.
    CTerrainEditModel staged = loaded;
    CValidationReport stagedReport;
    QVERIFY2(staged.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, catalog, &stagedReport,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(stagedReport.toText()));
    CTerrainVirtualMaterialField stagedField;
    QVERIFY(staged.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &stagedField, true));
    const STerrainTileCommand firstShiftR = staged.makeVirtualMapResolution(
        ETerrainTileLayer::Land, catalog, 0x53484946u, &stagedReport, false,
        &stagedField, true, true);
    QVERIFY2(!stagedReport.hasErrors(), qPrintable(stagedReport.toText()));
    quint16 first28 = 0;
    quint16 first29 = 0;
    QVERIFY(targetAfter(firstShiftR, 28, 17, &first28));
    QVERIFY(targetAfter(firstShiftR, 29, 17, &first29));
    QCOMPARE(first28, quint16(4));
    QCOMPARE(first29, quint16(1));

    CTerrainEditModel afterPaint = loaded;
    STerrainTileBrush firstPaintBrush;
    firstPaintBrush.centerX = 0.0f;
    firstPaintBrush.centerY = 0.0f;
    firstPaintBrush.radius = 0.0f;
    firstPaintBrush.shape = ETerrainTileBrushShape::Square;
    CValidationReport paintReport;
    const STerrainTileCommand firstPaint = afterPaint.makeVirtualMaterialPaint(
        0, 0, firstPaintBrush, ETerrainTileLayer::Land, 2, catalog,
        0x53484946u, &paintReport);
    QVERIFY2(!paintReport.hasErrors(), qPrintable(paintReport.toText()));
    QVERIFY(!firstPaint.isEmpty());
    QVERIFY2(afterPaint.apply(firstPaint, true, &paintReport),
             qPrintable(paintReport.toText()));
    CTerrainVirtualMaterialField afterPaintField;
    QVERIFY(afterPaint.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &afterPaintField, true));
    const STerrainTileCommand postPaintShiftR =
        afterPaint.makeVirtualMapResolution(
            ETerrainTileLayer::Land, catalog, 0x53484946u, &paintReport,
            false, &afterPaintField, true, true);
    QVERIFY2(!paintReport.hasErrors(), qPrintable(paintReport.toText()));
    quint16 painted28 = 0;
    quint16 painted29 = 0;
    QVERIFY(targetAfter(postPaintShiftR, 28, 17, &painted28));
    QVERIFY(targetAfter(postPaintShiftR, 29, 17, &painted29));
    QCOMPARE(painted28, first28);
    QCOMPARE(painted29, first29);

    // Abandoning the staged work must leave the freshly loaded model exactly
    // as it was: no virtual field and no physical repair are leaked.
    CTerrainVirtualMaterialField liveField;
    QVERIFY(!loaded.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &liveField, false));
    QCOMPARE(loaded.packedTile(28, 17, ETerrainTileLayer::Land), quint16(1));
    QCOMPARE(loaded.packedTile(29, 17, ETerrainTileLayer::Land), quint16(0));

    // This target does not link CView, so make the production ordering part
    // of the regression contract: the staged deterministic seed must exist
    // before Shift+R copies/scans the repair field, and live state must not be
    // assigned until after the plan is complete.
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int methodStart = source.indexOf(
        "void CView::resolveMissingTerrainBlends()");
    const int methodEnd = source.indexOf(
        "void CView::beginTileBrushGroup()", methodStart);
    QVERIFY2(methodStart >= 0 && methodEnd > methodStart,
             "Resolve Missing Blends implementation was not found");
    const QByteArray method = source.mid(methodStart, methodEnd - methodStart);
    const int stageConstruction = method.indexOf("SWorkingTerrainResolveStage staged;");
    const int stageCopy = method.indexOf("staged.terrainEdit = m_terrainEdit;");
    const int deterministicSeed = method.indexOf(
        "staged.terrainEdit.initializeVirtualMaterialField(");
    const int repairFieldCopy = method.indexOf(
        "staged.terrainEdit.copyVirtualMaterialField(");
    const int resolution = method.indexOf(
        "staged.terrainEdit.makeVirtualMapResolution(");
    const int liveCommit = method.indexOf("m_terrainEdit = staged.terrainEdit;");
    QVERIFY2(stageConstruction >= 0 && stageCopy > stageConstruction
             && deterministicSeed > stageCopy
             && repairFieldCopy > deterministicSeed
             && resolution > repairFieldCopy && liveCommit > resolution,
             "First Shift+R does not seed its staged deterministic field before planning and committing");
    QVERIFY2(!method.contains("m_terrainEdit.initializeVirtualMaterialField("),
             "Resolve Missing Blends initialized the live model before its staged command committed");
}

void EnduranceDocumentTests::classicAutoSafeTileSkipsUnsupportedCoreInsteadOfHardSeam()
{
    CTerrainTransitionCatalog catalog;
    STerrainTransitionSignature background;
    background.northWest = background.northEast = 1;
    background.southEast = background.southWest = 1;
    STerrainTransitionVariant backgroundVariant;
    backgroundVariant.signature = background;
    backgroundVariant.packedTile = 0;
    QVERIFY(catalog.addVariant(backgroundVariant));
    STerrainTransitionSignature selected;
    selected.northWest = selected.northEast = 2;
    selected.southEast = selected.southWest = 2;
    STerrainTransitionVariant selectedVariant;
    selectedVariant.signature = selected;
    selectedVariant.packedTile = 1;
    QVERIFY(catalog.addVariant(selectedVariant));

    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(16)),
        "classic-auto-safe-tile.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));
    QVector<quint16> before;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
            before.append(terrain.packedTile(x, y, ETerrainTileLayer::Land));

    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 0.0f;
    brush.shape = ETerrainTileBrushShape::Square;
    CValidationReport report;
    const STerrainTileCommand command = terrain.makeSemanticTilePaint(
        brush, ETerrainTileLayer::Land, 2, catalog, 0x53414654u, &report,
        ETerrainSemanticPaintPolicy::ExactExistingOnly);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY2(command.isEmpty(),
             "Safe Tile painted a uniform core despite absent exact boundary blends");
    QVERIFY(command.semanticAudit.paintedCoreCells.isEmpty());
    QVERIFY(report.toText().contains(
        QStringLiteral("terrain.semantic_tile_exact_only_skip")));
    int offset = 0;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
            QCOMPARE(terrain.packedTile(x, y, ETerrainTileLayer::Land),
                     before.at(offset++));
}

void EnduranceDocumentTests::manualPaintShowsCellFootprintAndLiftedTilePreview()
{
    QVERIFY(terrainPaintOutlineEnabled(true, true));
    QCOMPARE(terrainPaintOutlineWorldCenter(4, false), 8.0f);
    QCOMPARE(terrainPaintOutlineWorldCenter(4, true), 9.0f);
    QVERIFY(terrainTilePreviewElevation() > 0.0f);

    // A palette update raised synchronously from held-RMB sampling must not
    // replace the CTileBrush whose mouse handler is still executing.
    QVERIFY(!landscapeOperationNeedsTransition(
        EButtonOpTilebrush, eOperationMethodTileBrush));
    QVERIFY(landscapeOperationNeedsTransition(
        EButtonOpTilebrush, eOperationMethodSelect));
    // Other same-tool requests retain their existing transition semantics.
    QVERIFY(landscapeOperationNeedsTransition(
        EButtonOpSelect, eOperationMethodSelect));

    // Tile-target semantic painting retains its Manual-style cell footprint
    // even when the selected family has no optional physical preview tile.
    QVERIFY(terrainPaintCellOutlineVisible(true, false));
    QVERIFY(!terrainPaintCellOutlineVisible(false, true));

    // Hover, lifted preview, and commit retain one immutable logical snapshot.
    // Changing the next brush size must not retroactively resize or re-anchor
    // the footprint already resolved for the current pointer event.
    const QPoint cellAnchor(4, 7);
    const QPoint vertexAnchor(5, 8);
    const STerrainPaintFootprintSnapshot manual =
        terrainPaintFootprintSnapshot(
            cellAnchor, vertexAnchor, true, 1.0f, true);
    QCOMPARE(manual.anchor, cellAnchor);
    QCOMPARE(manual.cells().size(), 9);
    QVERIFY(manual.contains(QPoint(5, 8)));

    const STerrainPaintFootprintSnapshot modernVertex =
        terrainPaintFootprintSnapshot(
            cellAnchor, vertexAnchor, false, 1.0f, false);
    QCOMPARE(modernVertex.anchor, vertexAnchor);
    QCOMPARE(modernVertex.cells().size(), 5);

    const STerrainPaintFootprintSnapshot resized =
        terrainPaintFootprintSnapshot(
            cellAnchor, vertexAnchor, true, 2.0f, true);
    QCOMPARE(resized.cells().size(), 25);
    QCOMPARE(manual.cells().size(), 9);
    QVERIFY(terrainPaintCommitMatchesDisplayedFootprint(
        true, manual, manual));
    QVERIFY(!terrainPaintCommitMatchesDisplayedFootprint(
        true, manual, resized));
    const STerrainPaintFootprintSnapshot nextTile =
        terrainPaintFootprintSnapshot(
            QPoint(5, 7), QPoint(6, 8), true, 1.0f, true);
    QVERIFY(!terrainPaintCommitMatchesDisplayedFootprint(
        true, manual, nextTile));
    QVERIFY(!terrainPaintCommitMatchesDisplayedFootprint(
        false, manual, manual));

    // Held-RMB sampling is keyed to the logical terrain vicinity rather than
    // raw mouse events. Layer and palette changes intentionally invalidate it.
    const QPoint sample(12, 17);
    QVERIFY(terrainContinuousSampleShouldUpdate(
        false, QPoint(), -1, -1, sample, 0, 1));
    QVERIFY(!terrainContinuousSampleShouldUpdate(
        true, sample, 0, 1, sample, 0, 1));
    QVERIFY(terrainContinuousSampleShouldUpdate(
        true, sample, 0, 1, QPoint(13, 17), 0, 1));
    QVERIFY(terrainContinuousSampleShouldUpdate(
        true, sample, 0, 1, sample, 1, 1));
    QVERIFY(terrainContinuousSampleShouldUpdate(
        true, sample, 0, 1, sample, 0, 2));
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
    first.semanticAudit.valid = true;
    first.semanticAudit.requestedMaterialId = 12345;
    first.semanticAudit.solverMode = QStringLiteral("virtual-unblended-preview");
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
    QVERIFY(stroke.semanticAudit.valid);
    QCOMPARE(stroke.semanticAudit.requestedMaterialId, qint32(12345));
    QCOMPARE(stroke.semanticAudit.solverMode,
             QStringLiteral("virtual-unblended-preview"));
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

void EnduranceDocumentTests::zone1R16SquareOverlayBrushCoversDisplayedCells()
{
    const QString fixture = qEnvironmentVariable("ENDURANCE_ZONE1_R16_MPR_FIXTURE");
    if(fixture.isEmpty())
        QSKIP("Set ENDURANCE_ZONE1_R16_MPR_FIXTURE to the read-only Zone1 MPR used by the R16 repro.");
    QFile source(fixture);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex(),
             QByteArray("9d0ac909872898f0e72c4eec8acaedb9866bb1c18b4ee2ebe2b854790982dfa4"));

    CMprDocument document;
    CValidationReport report;
    QVERIFY2(document.load(QFileInfo(fixture), &report), qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));

    CValidationReport profileReport;
    const STerrainAtlasProfile profile = CTerrainAtlasPresetCatalog::exactProfile(
        QStringLiteral("zone1"),
        QByteArrayLiteral("35cc3b90985888b5c5206f8c5980321beb46f0be7b1e96b4a3467677ac2a4665"),
        512, &profileReport);
    QVERIFY2(profile.approved, qPrintable(profileReport.toText()));
    STerrainAtlasAnalysis analysis = profile.approvedAnalysis(&profileReport);
    QVERIFY2(analysis.usable, qPrintable(profileReport.toText()));
    const QVector<STerrainGlobalMaterialFamily> globalFamilies =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&profileReport);
    analysis.landCatalog = CTerrainAtlasPresetCatalog::globalizedCatalog(
        profile, analysis.landCatalog, &globalFamilies);
    analysis.overlayCatalog = CTerrainAtlasPresetCatalog::globalizedCatalog(
        profile, analysis.overlayCatalog, &globalFamilies);
    analysis.overlayCompatibleCatalog =
        CTerrainAtlasPresetCatalog::globalizedCatalog(
            profile, analysis.overlayCompatibleCatalog, &globalFamilies);
    CTerrainTransitionCatalog catalog =
        analysis.overlayCatalogWithLandFamilies();

    const qint32 lavaOrange = 2039464354;
    terrain.enableWorkingAtlasSlots(1024);
    STerrainTransitionVariant lavaUniform;
    lavaUniform.signature.northWest = lavaUniform.signature.northEast =
        lavaUniform.signature.southEast = lavaUniform.signature.southWest =
            lavaOrange;
    SMprPackedTile lavaUniformPacked;
    lavaUniformPacked.atlasIndex = 8;
    lavaUniformPacked.tileIndex = 0;
    lavaUniform.packedTile = lavaUniformPacked.encode();
    QVERIFY(catalog.addVariant(lavaUniform));

    // Exact R16 stroke regions recovered from its resolve journal. A square
    // Tile target displays and commits all 49 cells. Reproduce the complete
    // first-use Modern transaction against the real recovered Overlay field;
    // merely proving that the cells exist missed the sparse first stamp.
    const QVector<QPoint> centers({QPoint(8, 44), QPoint(8, 52)});
    for(const QPoint& center : centers)
    {
        CTerrainEditModel painted = terrain;
        CValidationReport paintReport;
        QVERIFY2(painted.initializeVirtualMaterialField(
            ETerrainTileLayer::Overlay, catalog, &paintReport,
            ETerrainMaterialFieldRecovery::RecoverDeterministically),
            qPrintable(paintReport.toText()));
        int availableCells = 0;
        for(int y = center.y() - 3; y <= center.y() + 3; ++y)
            for(int x = center.x() - 3; x <= center.x() + 3; ++x)
            {
                bool available = false;
                terrain.packedTile(x, y, ETerrainTileLayer::Overlay, &available);
                availableCells += available ? 1 : 0;
            }
        QCOMPARE(availableCells, 49);

        STerrainTileBrush brush;
        brush.centerX = center.x();
        brush.centerY = center.y();
        brush.radius = 3.0f;
        brush.shape = ETerrainTileBrushShape::Square;
        STerrainTileCommand stamp = painted.makeVirtualTileMaterialPaint(
            brush, ETerrainTileLayer::Overlay, lavaOrange, catalog,
            0x52313746u, &paintReport, true);
        QVERIFY2(!paintReport.hasErrors(), qPrintable(paintReport.toText()));
        QCOMPARE(stamp.semanticAudit.paintedCoreCells.size(), 49);
        QSet<int> directlyPaintedCore;
        for(const STerrainTileDelta& delta : stamp.deltas)
        {
            if(delta.globalX >= center.x() - 3
               && delta.globalX <= center.x() + 3
               && delta.globalY >= center.y() - 3
               && delta.globalY <= center.y() + 3)
                directlyPaintedCore.insert(
                    delta.globalY * painted.tileCountX() + delta.globalX);
        }
        QCOMPARE(directlyPaintedCore.size(), 49);
        QVERIFY2(painted.apply(stamp, true, &paintReport),
                 qPrintable(paintReport.toText()));
        for(int y = center.y() - 3; y <= center.y() + 3; ++y)
            for(int x = center.x() - 3; x <= center.x() + 3; ++x)
            {
                bool available = false;
                const quint16 packed = painted.packedTile(
                    x, y, ETerrainTileLayer::Overlay, &available);
                QVERIFY(available);
                bool decoded = false;
                const STerrainTransitionSignature exact =
                    catalog.gridSignatureForPacked(packed, &decoded);
                QVERIFY(decoded);
                QCOMPARE(exact.northWest, lavaOrange);
                QCOMPARE(exact.northEast, lavaOrange);
                QCOMPARE(exact.southEast, lavaOrange);
                QCOMPARE(exact.southWest, lavaOrange);
            }
    }
}

void EnduranceDocumentTests::modernSquareTileStampResolvesAlreadySemanticCoreCells()
{
    CMprDocument document;
    QVERIFY(document.loadBytes(syntheticMpr(
        syntheticLandSector(1200), syntheticMp(32)),
        "modern-square-semantic-core.mpr"));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(document));

    CTerrainTransitionCatalog catalog;
    for(int mask = 0; mask < 16; ++mask)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = (mask & 1) ? 2 : 1;
        variant.signature.northEast = (mask & 2) ? 2 : 1;
        variant.signature.southEast = (mask & 4) ? 2 : 1;
        variant.signature.southWest = (mask & 8) ? 2 : 1;
        SMprPackedTile packed;
        packed.tileIndex = quint8(mask);
        variant.packedTile = packed.encode();
        QVERIFY(catalog.addVariant(variant));
    }

    CValidationReport report;
    QVERIFY2(terrain.initializeVirtualMaterialField(
        ETerrainTileLayer::Land, catalog, &report,
        ETerrainMaterialFieldRecovery::RecoverDeterministically),
        qPrintable(report.toText()));
    QVector<qint32> current;
    QVector<qint32> baseline;
    QVERIFY(terrain.copyVirtualMaterialFieldState(
        ETerrainTileLayer::Land, &current, &baseline));
    const int vertexWidth = terrain.tileCountX() + 1;
    for(int y = 7; y <= 10; ++y)
        for(int x = 7; x <= 10; ++x)
        {
            current[y * vertexWidth + x] = 2;
            baseline[y * vertexWidth + x] = 2;
        }
    QVERIFY2(terrain.restoreVirtualMaterialFieldState(
        ETerrainTileLayer::Land, current, baseline, &report),
        qPrintable(report.toText()));

    // Reproduce the R16 first-stamp mismatch: recovery already describes an
    // inner part of the requested square as the selected material, while its
    // packed artwork still belongs to the old material. One Modern tile stamp
    // must make the complete displayed 7x7 core physically and semantically
    // consistent without relying on a later map-wide Shift+R repair.
    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 3.0f;
    brush.shape = ETerrainTileBrushShape::Square;
    STerrainTileCommand stamp = terrain.makeVirtualTileMaterialPaint(
        brush, ETerrainTileLayer::Land, 2, catalog,
        0x52313653u, &report, true);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(stamp.semanticAudit.paintedCoreCells.size(), 49);
    QSet<int> directlyPaintedCore;
    for(const STerrainTileDelta& delta : stamp.deltas)
        if(delta.globalX >= 5 && delta.globalX <= 11
           && delta.globalY >= 5 && delta.globalY <= 11)
            directlyPaintedCore.insert(
                delta.globalY * terrain.tileCountX() + delta.globalX);
    QCOMPARE(directlyPaintedCore.size(), 49);

    CTerrainEditModel planned = terrain;
    QVERIFY2(planned.apply(stamp, true, &report), qPrintable(report.toText()));
    const STerrainTileCommand resolution = planned.makeVirtualStrokeResolution(
        stamp, catalog, 0x52313653u, &report, true, 2);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    appendTerrainTileCommand(&stamp, resolution);
    QVERIFY2(terrain.apply(stamp, true, &report), qPrintable(report.toText()));

    for(int y = 5; y <= 11; ++y)
        for(int x = 5; x <= 11; ++x)
        {
            bool available = false;
            const quint16 packed = terrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            QVERIFY(available);
            bool decoded = false;
            const STerrainTransitionSignature exact =
                catalog.gridSignatureForPacked(packed, &decoded);
            QVERIFY(decoded);
            QCOMPARE(exact.northWest, qint32(2));
            QCOMPARE(exact.northEast, qint32(2));
            QCOMPARE(exact.southEast, qint32(2));
            QCOMPARE(exact.southWest, qint32(2));
        }
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
    QVERIFY(firstStrokeTerrain.makeOverlayActivation(
        firstStroke, 1, 0, 1, 2).isEmpty());

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
    const SOverlaySurfaceCommand brushChange = terrain.makeOverlayActivation(
        point, 0, 0, 0, 1);
    QVERIFY(!brushChange.isEmpty());
    QVERIFY(history.execute(&terrain, brushChange));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(0));
    SMprPackedTile changedTile;
    changedTile.atlasIndex = 0;
    changedTile.tileIndex = 0;
    changedTile.rotation = 1;
    QCOMPARE(terrain.packedTile(2, 3, ETerrainTileLayer::Overlay),
             changedTile.encode());
    QVERIFY(available);
    QVERIFY(history.undo(&terrain));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3, &available), qint16(1));
    QCOMPARE(terrain.packedTile(2, 3, ETerrainTileLayer::Overlay),
             activatedTile);

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

void EnduranceDocumentTests::overlayActivationDistributesUniformVariantsAndRotations()
{
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(syntheticMpr(syntheticLandSector(1200)),
                                "overlay-variant-stamp.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));

    CTerrainTransitionCatalog catalog;
    STerrainTransitionSignature uniform;
    uniform.northWest = uniform.northEast = uniform.southEast
        = uniform.southWest = 7;
    for(int slot = 0; slot < 2; ++slot)
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.tileIndex = quint8(slot);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = uniform;
            variant.packedTile = packed.encode();
            QVERIFY(catalog.addVariant(variant));
        }

    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 2.0f;
    brush.shape = ETerrainTileBrushShape::Square;
    const SOverlaySurfaceCommand command = terrain.makeOverlayActivation(
        brush, 1, 0, 0, 0, &report, &catalog, uniform, 0x5a17u, true);
    QVERIFY2(!command.isEmpty(), qPrintable(report.toText()));
    QVERIFY(terrain.apply(command, true, &report));

    QSet<quint16> selected;
    for(int y = 6; y <= 10; ++y)
        for(int x = 6; x <= 10; ++x)
            selected.insert(terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay));
    QVERIFY2(selected.size() > 1,
             "Add Overlay Surface stamped one packed tile/rotation over the whole brush");
    for(quint16 packed : selected)
    {
        bool decoded = false;
        QCOMPARE(catalog.gridSignatureForPacked(packed, &decoded), uniform);
        QVERIFY(decoded);
    }
}

void EnduranceDocumentTests::modernUniformOnlyPaintNeverSelectsBlendTiles()
{
    CMprDocument baseline;
    CValidationReport report;
    QVERIFY2(baseline.loadBytes(syntheticMpr(syntheticLandSector(1200)),
                                "modern-uniform-only.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));

    CTerrainTransitionCatalog catalog;
    STerrainTransitionSignature uniform;
    uniform.northWest = uniform.northEast = uniform.southEast
        = uniform.southWest = 7;
    for(int slot = 0; slot < 2; ++slot)
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.tileIndex = quint8(slot);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = uniform;
            variant.packedTile = packed.encode();
            QVERIFY(catalog.addVariant(variant));
        }
    STerrainTransitionSignature blend = uniform;
    blend.northWest = 3;
    SMprPackedTile blendPacked;
    blendPacked.tileIndex = 2;
    STerrainTransitionVariant blendVariant;
    blendVariant.signature = blend;
    blendVariant.packedTile = blendPacked.encode();
    QVERIFY(catalog.addVariant(blendVariant));

    STerrainTileBrush brush;
    brush.centerX = 8.0f;
    brush.centerY = 8.0f;
    brush.radius = 2.0f;
    brush.shape = ETerrainTileBrushShape::Square;
    const STerrainTileCommand command = terrain.makeUniformTilePaint(
        brush, ETerrainTileLayer::Land, uniform, catalog, 0x9173u, true,
        &report);
    QVERIFY2(!command.isEmpty(), qPrintable(report.toText()));
    QVERIFY(terrain.apply(command, true, &report));
    QSet<quint16> painted;
    for(int y = 6; y <= 10; ++y)
        for(int x = 6; x <= 10; ++x)
        {
            const quint16 packed = terrain.packedTile(
                x, y, ETerrainTileLayer::Land);
            painted.insert(packed);
            bool decoded = false;
            QCOMPARE(catalog.gridSignatureForPacked(packed, &decoded), uniform);
            QVERIFY(decoded);
        }
    QVERIFY(painted.size() > 1);
    QVERIFY(terrain.apply(command, false, &report));
    QVERIFY(terrain.apply(command, true, &report));
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

void EnduranceDocumentTests::semanticAtlasPerceptualFamiliesSeparateHueAndWhiteTexture()
{
    const auto solid = [](const QColor& color) {
        QImage image(24, 24, QImage::Format_RGBA8888);
        image.fill(color);
        return image;
    };
    const auto texture = [](const QColor& first, const QColor& second, int scale) {
        QImage image(24, 24, QImage::Format_RGBA8888);
        for(int y = 0; y < image.height(); ++y)
            for(int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y,
                    (((x / scale) + (y / scale)) & 1) ? first : second);
        return image;
    };
    QVector<QImage> images({
        solid(QColor(25, 80, 30)),    // dark grass
        solid(QColor(40, 125, 45)),   // normal grass
        solid(QColor(80, 170, 55)),   // bright grass
        solid(QColor(135, 125, 105)), // stone
        solid(QColor(180, 175, 145)), // limestone
        texture(QColor(226, 238, 246), QColor(248, 246, 242), 2), // coarse snow
        texture(QColor(226, 238, 246), QColor(248, 246, 242), 1)  // fine ice
    });
    QVector<STerrainAtlasSlotUsage> usage(images.size());
    for(STerrainAtlasSlotUsage& item : usage) item.landCount = 1;
    const STerrainAtlasProfile profile = CTerrainAtlasProfileInference::propose(
        "synthetic_hue_and_white", images,
        QVector<qint32>(images.size(), 1), usage);
    QCOMPARE(profile.families.size(), images.size());
    QCOMPARE(profile.assignedSlotCount(), images.size());
    QSet<qint32> ids;
    for(const STerrainAtlasProfileSlot& definition : profile.slotDefinitions)
    {
        QVERIFY(definition.assigned);
        QVERIFY(!definition.ambiguous);
        QVERIFY(definition.signature.northWest == definition.signature.northEast);
        QVERIFY(definition.signature.northWest == definition.signature.southEast);
        QVERIFY(definition.signature.northWest == definition.signature.southWest);
        ids.insert(definition.signature.northWest);
    }
    QCOMPARE(ids.size(), images.size());
}

void EnduranceDocumentTests::semanticAtlasInferenceUsesRendererOrientationAndPreservesCornerFamilies()
{
    const QColor ground(25, 80, 25), yellowDetail(185, 155, 35);
    QImage uniform(24, 24, QImage::Format_RGBA8888);
    uniform.fill(ground);
    QImage transition = uniform;
    for(int y = transition.height() / 2; y < transition.height(); ++y)
        for(int x = 0; x < transition.width() / 2; ++x)
            transition.setPixelColor(x, y, yellowDetail);
    QVector<STerrainAtlasSlotUsage> usage(2);
    usage[0].landCount = 20;
    usage[1].landCount = 4;
    CValidationReport report;
    const STerrainAtlasProfile profile = CTerrainAtlasProfileInference::propose(
        "synthetic_corner_detail", QVector<QImage>({uniform, transition}),
        QVector<qint32>({0, 0}), usage, &report);
    QCOMPARE(profile.families.size(), 2);
    QCOMPARE(profile.assignedSlotCount(), 2);
    const qint32 groundId = profile.slotDefinitions.at(0).signature.northWest;
    const STerrainTransitionSignature signature = profile.slotDefinitions.at(1).signature;
    // Inference stores exactly what the user sees in the tile preview.
    QCOMPARE(signature.northWest, groundId);
    QCOMPARE(signature.northEast, groundId);
    QCOMPARE(signature.southEast, groundId);
    QVERIFY(signature.southWest != groundId);
    QVERIFY(report.toText().contains("corner material"));

    CValidationReport analysisReport;
    const STerrainAtlasAnalysis analysis = profile.usableAnalysis(&analysisReport);
    QVERIFY2(analysis.usable, qPrintable(analysisReport.toText()));
    QCOMPARE(analysis.materials.size(), 2);
    bool gridFound = false;
    SMprPackedTile packed;
    packed.tileIndex = 1;
    const STerrainTransitionSignature grid = analysis.landCatalog.gridSignatureForPacked(
        packed.encode(), &gridFound);
    QVERIFY(gridFound);
    // The paint grid has the opposite vertical axis. The visible SW detail is
    // therefore paint-grid NW, with exactly one conversion at compilation.
    QVERIFY(grid.northWest != groundId);
    QCOMPARE(grid.northEast, groundId);
    QCOMPARE(grid.southEast, groundId);
    QCOMPARE(grid.southWest, groundId);
    packed.rotation = 1;
    const STerrainTransitionSignature rotatedGrid =
        analysis.landCatalog.gridSignatureForPacked(packed.encode(), &gridFound);
    QVERIFY(gridFound);
    const STerrainTransitionSignature paintBase =
        CTerrainTransitionCatalog::mirroredGridY(signature);
    const STerrainTransitionSignature expectedRotated =
        CTerrainTransitionCatalog::rotated(paintBase, -1);
    QCOMPARE(rotatedGrid.northWest, expectedRotated.northWest);
    QCOMPARE(rotatedGrid.northEast, expectedRotated.northEast);
    QCOMPARE(rotatedGrid.southEast, expectedRotated.southEast);
    QCOMPARE(rotatedGrid.southWest, expectedRotated.southWest);

    // Pixel-inferred legacy profiles already stored visible preview corners.
    QJsonObject legacyJson = profile.toJson();
    legacyJson.insert(QStringLiteral("schemaVersion"), 3);
    legacyJson.remove(QStringLiteral("cornerSpace"));
    STerrainAtlasProfile migrated;
    CValidationReport migrationReport;
    QVERIFY2(STerrainAtlasProfile::fromJson(legacyJson, &migrated,
                                            &migrationReport),
             qPrintable(migrationReport.toText()));
    QCOMPARE(migrated.schemaVersion, 10);
    QCOMPARE(migrated.slotDefinitions.at(1).signature.northWest, signature.northWest);
    QCOMPARE(migrated.slotDefinitions.at(1).signature.southWest, signature.southWest);
    const auto detail = std::find_if(analysis.materials.cbegin(), analysis.materials.cend(),
        [groundId](const STerrainAtlasMaterial& material) {
            return material.id != groundId;
        });
    QVERIFY(detail != analysis.materials.cend());
    QVERIFY(detail->cornerOnly);
    QCOMPARE(detail->pureVariantCount, 0);
    QCOMPARE(detail->representativeSlot, 1);

    // Packed rotation has one renderer-defined mapping for every atlas. Check
    // all turns from a deliberately asymmetric preview signature so a mirror
    // or chirality error cannot hide behind a uniform tile.
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        packed.rotation = quint8(rotation);
        const STerrainTransitionSignature actual =
            analysis.landCatalog.gridSignatureForPacked(packed.encode(), &gridFound);
        QVERIFY(gridFound);
        const STerrainTransitionSignature expected =
            CTerrainTransitionCatalog::rotated(paintBase, -rotation);
        QCOMPARE(actual.northWest, expected.northWest);
        QCOMPARE(actual.northEast, expected.northEast);
        QCOMPARE(actual.southEast, expected.southEast);
        QCOMPARE(actual.southWest, expected.southWest);
    }

    const QImage thumbnail = terrainVertexJunctionThumbnail(
        QVector<QImage>({uniform, transition}), analysis.landCatalog,
        detail->id, groundId, detail->representativeSlot);
    QCOMPARE(thumbnail.size(), QSize(48, 48));
    // Every incident tile must put the detail into the shared center vertex.
    for(const QPoint& point : {QPoint(22, 22), QPoint(25, 22),
                               QPoint(25, 25), QPoint(22, 25)})
        QCOMPARE(thumbnail.pixelColor(point), yellowDetail);
    // The four exterior corners remain the supporting material. A mirrored
    // or opposite-sign image rotation makes at least one of these eight pixel
    // assertions fail and reproduces the incoherent Zone1 palette thumbnail.
    for(const QPoint& point : {QPoint(2, 2), QPoint(45, 2),
                               QPoint(45, 45), QPoint(2, 45)})
        QCOMPARE(thumbnail.pixelColor(point), ground);
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
        "decoded-atlas-perceptual-texture+mpr-tile-types+observed-map-usage-v10-calibrated-packed-rotation"));
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
    QCOMPARE(profile.assignedSlotCount(), 512);
    QVERIFY(profile.provenance.startsWith("authored-template:"));
    // Tile 50 is visibly Grass/common on its north edge and Grass/dark on
    // its south edge. This explicit asymmetric fixture catches the exact UI
    // inversion reported against the Material Atlas preview.
    const STerrainTransitionSignature tile50 =
        profile.slotDefinitions.at(50).signature;
    QCOMPARE(tile50.northWest, qint32(4));
    QCOMPARE(tile50.northEast, qint32(4));
    QCOMPARE(tile50.southEast, qint32(3));
    QCOMPARE(tile50.southWest, qint32(3));
    CValidationReport roundTripReport;
    const STerrainAtlasAnalysis roundTrip = profile.approvedAnalysis(&roundTripReport);
    QVERIFY2(roundTrip.usable, qPrintable(roundTripReport.toText()));
    QCOMPARE(roundTrip.describedSlots, analysis.describedSlots);
    QCOMPARE(roundTrip.materials.size(), analysis.materials.size());
    // The authored table is only a profile seed. Every slot and packed
    // rotation must compile identically through the shared profile path used
    // by inferred atlases such as Zone2 and Zone5_1.
    for(int slot = 0; slot < 512; ++slot)
    {
        const STerrainTransitionSignature actual = profile.slotDefinitions.at(slot).signature;
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.atlasIndex = quint8(slot / 64);
            packed.tileIndex = quint8(slot % 64);
            packed.rotation = quint8(rotation);
            bool authoredLand = false, authoredOverlay = false;
            bool compiledLand = false, compiledOverlay = false;
            const STerrainTransitionSignature authoredLandSignature =
                analysis.landCatalog.gridSignatureForPacked(
                    packed.encode(), &authoredLand);
            const STerrainTransitionSignature authoredOverlaySignature =
                analysis.overlayCatalog.gridSignatureForPacked(
                    packed.encode(), &authoredOverlay);
            const STerrainTransitionSignature compiledLandSignature =
                roundTrip.landCatalog.gridSignatureForPacked(
                    packed.encode(), &compiledLand);
            const STerrainTransitionSignature compiledOverlaySignature =
                roundTrip.overlayCatalog.gridSignatureForPacked(
                    packed.encode(), &compiledOverlay);
            QCOMPARE(authoredLand, compiledLand);
            QCOMPARE(authoredOverlay, compiledOverlay);
            QCOMPARE(authoredLand, !authoredOverlay);
            const STerrainTransitionSignature authored = authoredLand
                ? authoredLandSignature : authoredOverlaySignature;
            const STerrainTransitionSignature compiled = compiledLand
                ? compiledLandSignature : compiledOverlaySignature;
            const STerrainTransitionSignature paintBase =
                CTerrainTransitionCatalog::mirroredGridY(actual);
            const STerrainTransitionSignature rotated =
                CTerrainTransitionCatalog::rotated(paintBase, -rotation);
            QCOMPARE(authored.northWest, compiled.northWest);
            QCOMPARE(authored.northEast, compiled.northEast);
            QCOMPARE(authored.southEast, compiled.southEast);
            QCOMPARE(authored.southWest, compiled.southWest);
            QCOMPARE(compiled.northWest, rotated.northWest);
            QCOMPARE(compiled.northEast, rotated.northEast);
            QCOMPARE(compiled.southEast, rotated.southEast);
            QCOMPARE(compiled.southWest, rotated.southWest);
        }
    }
    // Authored profiles saved before preview-space schema 6 stored paint-grid
    // corners. Migration must reflect them once while retaining user metadata.
    QJsonObject legacyAuthored = profile.toJson();
    legacyAuthored.insert(QStringLiteral("atlasPixelSha256"),
                          QString(64, QLatin1Char('a')));
    legacyAuthored.insert(QStringLiteral("schemaVersion"), 5);
    legacyAuthored.insert(QStringLiteral("cornerSpace"),
                          QStringLiteral("renderer-grid"));
    QJsonArray legacySlots = legacyAuthored.value(QStringLiteral("slots")).toArray();
    for(int index = 0; index < legacySlots.size(); ++index)
    {
        QJsonObject slot = legacySlots.at(index).toObject();
        const QJsonObject visual =
            slot.value(QStringLiteral("signature")).toObject();
        QJsonObject paint;
        paint.insert(QStringLiteral("northWest"),
                     visual.value(QStringLiteral("southWest")));
        paint.insert(QStringLiteral("northEast"),
                     visual.value(QStringLiteral("southEast")));
        paint.insert(QStringLiteral("southEast"),
                     visual.value(QStringLiteral("northEast")));
        paint.insert(QStringLiteral("southWest"),
                     visual.value(QStringLiteral("northWest")));
        slot.insert(QStringLiteral("signature"), paint);
        legacySlots[index] = slot;
    }
    legacyAuthored.insert(QStringLiteral("slots"), legacySlots);
    STerrainAtlasProfile migratedAuthored;
    CValidationReport migrationReport;
    QVERIFY2(STerrainAtlasProfile::fromJson(legacyAuthored, &migratedAuthored,
                                            &migrationReport),
             qPrintable(migrationReport.toText()));
    QCOMPARE(migratedAuthored.schemaVersion, 10);
    QCOMPARE(migratedAuthored.slotDefinitions.at(50).signature.northWest,
             qint32(4));
    QCOMPARE(migratedAuthored.slotDefinitions.at(50).signature.southWest,
             qint32(3));
    QVERIFY(std::any_of(profile.families.cbegin(), profile.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return family.name == QStringLiteral("Stone / lime");
        }));
}

void EnduranceDocumentTests::authoredPresetCatalogCoversKnownAtlasesAndMatchesZone1()
{
    CValidationReport report;
    QCOMPARE(CTerrainAtlasPresetCatalog::presetCount(&report), 38);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    const QByteArray hdHash =
        "35cc3b90985888b5c5206f8c5980321beb46f0be7b1e96b4a3467677ac2a4665";
    const STerrainAtlasProfile profile = CTerrainAtlasPresetCatalog::exactProfile(
        "zone1", hdHash, 512, &report);
    QVERIFY2(profile.approved, qPrintable(report.toText()));
    QCOMPARE(profile.provenance,
             QStringLiteral("built-in-material-atlas-v2"));
    QCOMPARE(profile.assignedSlotCount(), 512);
    QCOMPARE(profile.families.size(), 9);
    QCOMPARE(std::count_if(profile.families.cbegin(), profile.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return !family.paintSelectable;
        }), 0);
    const auto stoneLime = std::find_if(profile.families.cbegin(), profile.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return family.name == QStringLiteral("Stone / Lime");
        });
    QVERIFY(stoneLime != profile.families.cend());
    QVERIFY(stoneLime->paintSelectable);
    QCOMPARE(profile.slotDefinitions.at(50).signature.northWest, qint32(4));
    QCOMPARE(profile.slotDefinitions.at(50).signature.northEast, qint32(4));
    QCOMPARE(profile.slotDefinitions.at(50).signature.southEast, qint32(3));
    QCOMPARE(profile.slotDefinitions.at(50).signature.southWest, qint32(3));
    const STerrainAtlasAnalysis compiled = profile.approvedAnalysis(&report);
    QVERIFY2(compiled.usable, qPrintable(report.toText()));
    QCOMPARE(compiled.describedSlots, 512);

    const STerrainAtlasProfile mismatch = CTerrainAtlasPresetCatalog::exactProfile(
        "zone1", QByteArray(64, '0'), 512, &report);
    QVERIFY(mismatch.slotDefinitions.isEmpty());
    const STerrainAtlasProfile wrongNamespace = CTerrainAtlasPresetCatalog::exactProfile(
        "zone2", hdHash, 512, &report);
    QVERIFY(wrongNamespace.slotDefinitions.isEmpty());

    struct CapturedBinding { const char* nameSpace; const char* hash; int slotCount; };
    const CapturedBinding captured[] = {
        {"Zone2", "ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612", 512},
        {"Zone3Obr", "33d297edc193f88356c766772a84f5c6db27cf6cd9831d241cdafa9497cb5b4a", 512},
        {"Zone5_1", "2908a9617277f4e0ff663bf032325941fc57879af573c918e13f249e4de54635", 192}
    };
    for(const auto& item : captured)
    {
        CValidationReport capturedReport;
        const STerrainAtlasProfile capturedProfile =
            CTerrainAtlasPresetCatalog::exactProfile(
                QString::fromLatin1(item.nameSpace), QByteArray(item.hash),
                item.slotCount, &capturedReport);
        QVERIFY2(capturedProfile.approved,
                 qPrintable(QString::fromLatin1(item.nameSpace)
                    + QLatin1Char('\n') + capturedReport.toText()));
        QCOMPARE(capturedProfile.assignedSlotCount(), item.slotCount);
    }

    CValidationReport zone2Report;
    const STerrainAtlasProfile zone2 = CTerrainAtlasPresetCatalog::exactProfile(
        "Zone2",
        "ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612",
        512, &zone2Report);
    QVERIFY2(zone2.approved, qPrintable(zone2Report.toText()));
    const auto zone2Family = [&zone2](const QString& name) {
        return std::find_if(zone2.families.cbegin(), zone2.families.cend(),
            [&name](const STerrainAtlasProfileFamily& family) {
                return family.name == name;
            });
    };
    const auto lime = zone2Family(QStringLiteral("Stone / Lime"));
    const auto pink = zone2Family(QStringLiteral("Stone / Pink"));
    const auto mossy = zone2Family(QStringLiteral("Stone / Mossy"));
    const auto irregular = zone2Family(QStringLiteral("Grass / Irregular"));
    const auto dirt = zone2Family(QStringLiteral("Ground / Dirt"));
    QVERIFY(lime != zone2.families.cend());
    QVERIFY(pink != zone2.families.cend());
    QVERIFY(mossy != zone2.families.cend());
    QVERIFY(irregular != zone2.families.cend());
    QVERIFY(dirt != zone2.families.cend());
    // R117 supplies the reviewed one-on-three authored stamp that makes this
    // exact Zone2 identity structurally paintable through the global uniform
    // source pool.
    QVERIFY(dirt->paintSelectable);
    for(const QString& removed : {
            QStringLiteral("Ground / Light"),
            QStringLiteral("Grass / Winter normal"),
            QStringLiteral("Ground / Common"),
            QStringLiteral("Grass / Winter faded"),
            QStringLiteral("Ground / Trampled"),
            QStringLiteral("Grass / Irregular dark"),
            QStringLiteral("Ground / Dry")})
        QVERIFY2(zone2Family(removed) == zone2.families.cend(), qPrintable(removed));
    QCOMPARE(irregular->thumbnailSlot, 117);
    QCOMPARE(zone2.slotDefinitions.at(117).signature.southWest, irregular->id);
    QCOMPARE(zone2.slotDefinitions.at(393).signature.northWest, lime->id);
    QCOMPARE(zone2.slotDefinitions.at(416).signature.southWest, pink->id);
    QCOMPARE(zone2.slotDefinitions.at(285).signature.northWest, mossy->id);
    QCOMPARE(zone2.slotDefinitions.at(448).signature.southEast, dirt->id);

    CValidationReport zone8Report;
    const STerrainAtlasProfile zone8 = CTerrainAtlasPresetCatalog::exactProfile(
        "Zone8",
        "4ad67630a39a00a6d5fb1d421e7873c76faa64707a4c5565744a84476dfdc35e",
        512, &zone8Report);
    QVERIFY2(zone8.approved, qPrintable(zone8Report.toText()));
    const STerrainTransitionSignature reviewedSand426 =
        zone8.slotDefinitions.at(426).signature;
    QCOMPARE(reviewedSand426.northWest, qint32(7));
    QCOMPARE(reviewedSand426.northEast, qint32(7));
    QCOMPARE(reviewedSand426.southEast, qint32(12));
    QCOMPARE(reviewedSand426.southWest, qint32(11));
    const STerrainTransitionSignature reviewedSand121 =
        zone8.slotDefinitions.at(121).signature;
    QCOMPARE(reviewedSand121.northWest, qint32(7));
    QCOMPARE(reviewedSand121.northEast, qint32(7));
    QCOMPARE(reviewedSand121.southEast, qint32(7));
    QCOMPARE(reviewedSand121.southWest, qint32(12));

    CValidationReport bz3gReport;
    const STerrainAtlasProfile bz3g = CTerrainAtlasPresetCatalog::exactProfile(
        "bz3g",
        "6752a5b4cefd3aedbd6889516b424f4e13afafe288e99406b500404aed62b014",
        512, &bz3gReport);
    QVERIFY2(bz3g.approved, qPrintable(bz3gReport.toText()));
    QCOMPARE(bz3g.families.size(), 15);
    const auto bz3gFamily = [&bz3g](qint32 id) {
        return std::find_if(bz3g.families.cbegin(), bz3g.families.cend(),
            [id](const STerrainAtlasProfileFamily& family) {
                return family.id == id;
            });
    };
    const auto darkGround = bz3gFamily(6);
    QVERIFY(darkGround != bz3g.families.cend());
    QCOMPARE(darkGround->name, QStringLiteral("Ground / Dead"));
    QCOMPARE(darkGround->tileType, qint32(1));
    QCOMPARE(darkGround->thumbnailSlot, 7);
    for(const qint32 removedId : {7, 13, 15, 16, 18, 22, 23})
        QCOMPARE(bz3gFamily(removedId), bz3g.families.cend());
    // R114 explicitly reclassifies this reviewed corner from Ground / Dead
    // to Grass / Sparse dark; bz3g does not propagate to byte-distinct Zone9.
    QCOMPARE(bz3g.slotDefinitions.at(108).signature.northWest, qint32(5));
    QCOMPARE(bz3g.slotDefinitions.at(298).signature.northWest, qint32(2));
    QCOMPARE(bz3g.slotDefinitions.at(450).signature.northEast, qint32(5));
    QCOMPARE(bz3g.slotDefinitions.at(450).signature.southEast, qint32(2));
    const STerrainTransitionSignature reviewed416 =
        bz3g.slotDefinitions.at(416).signature;
    QCOMPARE(reviewed416.northWest, qint32(14));
    QCOMPARE(reviewed416.northEast, qint32(12));
    QCOMPARE(reviewed416.southEast, qint32(2));
    QCOMPARE(reviewed416.southWest, qint32(3));
    const STerrainTransitionSignature reviewed445 =
        bz3g.slotDefinitions.at(445).signature;
    QCOMPARE(reviewed445.northWest, qint32(3));
    QCOMPARE(reviewed445.northEast, qint32(6));
    QCOMPARE(reviewed445.southEast, qint32(3));
    QCOMPARE(reviewed445.southWest, qint32(6));
    const STerrainTransitionSignature reviewed468 =
        bz3g.slotDefinitions.at(468).signature;
    QCOMPARE(reviewed468.northWest, qint32(6));
    QCOMPARE(reviewed468.northEast, qint32(3));
    QCOMPARE(reviewed468.southEast, qint32(6));
    QCOMPARE(reviewed468.southWest, qint32(6));
    const STerrainTransitionSignature reviewed163 =
        bz3g.slotDefinitions.at(163).signature;
    QCOMPARE(reviewed163.northWest, qint32(3));
    QCOMPARE(reviewed163.northEast, qint32(6));
    QCOMPARE(reviewed163.southEast, qint32(6));
    QCOMPARE(reviewed163.southWest, qint32(6));
    const STerrainTransitionSignature reviewed198 =
        bz3g.slotDefinitions.at(198).signature;
    QCOMPARE(reviewed198.northWest, qint32(6));
    QCOMPARE(reviewed198.northEast, qint32(6));
    QCOMPARE(reviewed198.southEast, qint32(3));
    QCOMPARE(reviewed198.southWest, qint32(6));
    const STerrainTransitionSignature reviewed297 =
        bz3g.slotDefinitions.at(297).signature;
    QCOMPARE(reviewed297.northWest, qint32(3));
    QCOMPARE(reviewed297.northEast, qint32(5));
    QCOMPARE(reviewed297.southEast, qint32(3));
    QCOMPARE(reviewed297.southWest, qint32(6));

    CValidationReport zone9Report;
    const STerrainAtlasProfile zone9 = CTerrainAtlasPresetCatalog::exactProfile(
        "zone9",
        "bd5f43f33363351807dd6069ab1eb861fc1b66177500dc0d29710474253d77fe",
        512, &zone9Report);
    QVERIFY2(zone9.approved, qPrintable(zone9Report.toText()));
    QCOMPARE(zone9.families.size(), bz3g.families.size());
    QCOMPARE(zone9.slotDefinitions.at(450).signature,
             bz3g.slotDefinitions.at(450).signature);
    QCOMPARE(zone9.slotDefinitions.at(163).signature, reviewed163);
    QCOMPARE(zone9.slotDefinitions.at(198).signature, reviewed198);
    QCOMPARE(zone9.slotDefinitions.at(297).signature, reviewed297);
    const STerrainTransitionSignature reviewed476 =
        zone9.slotDefinitions.at(476).signature;
    QCOMPARE(reviewed476.northWest, qint32(3));
    QCOMPARE(reviewed476.northEast, qint32(5));
    QCOMPARE(reviewed476.southEast, qint32(6));
    QCOMPARE(reviewed476.southWest, qint32(6));

    CValidationReport zone6Report;
    const STerrainAtlasProfile zone6 = CTerrainAtlasPresetCatalog::exactProfile(
        "Zone6",
        "4aff5893f635e48e0c5ec005172e81e7e69584d9a0da8b4c285d16031b3cc799",
        512, &zone6Report);
    QVERIFY2(zone6.approved, qPrintable(zone6Report.toText()));
    const auto zone6Dead = std::find_if(
        zone6.families.cbegin(), zone6.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return family.id == 5;
        });
    QVERIFY(zone6Dead != zone6.families.cend());
    QCOMPARE(zone6Dead->name, QStringLiteral("Ground / Dead"));

    CValidationReport zone62Report;
    const STerrainAtlasProfile zone62 = CTerrainAtlasPresetCatalog::exactProfile(
        "Zone6_2",
        "916e18f19c3b648d9951af2e9930f3fa7e9d67c97fd44bf09d2f5ceebeee6f56",
        512, &zone62Report);
    QVERIFY2(zone62.approved, qPrintable(zone62Report.toText()));
    const auto zone62Dead = std::find_if(
        zone62.families.cbegin(), zone62.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return family.id == 5;
        });
    QVERIFY(zone62Dead != zone62.families.cend());
    QCOMPARE(zone62Dead->name, QStringLiteral("Ground / Dead"));

    CValidationReport snowRockReport;
    const STerrainAtlasProfile snowRock =
        CTerrainAtlasPresetCatalog::exactProfile(
            "bz11k",
            "bd1873929c5ba9d3406d8be7d642dfa37fcb42a1a48edc914d5dd97610ee00d2",
            512, &snowRockReport);
    QVERIFY2(snowRock.approved, qPrintable(snowRockReport.toText()));
    const STerrainTransitionSignature reviewed462 =
        snowRock.slotDefinitions.at(462).signature;
    QCOMPARE(reviewed462.northWest, qint32(6));
    QCOMPARE(reviewed462.northEast, qint32(16));
    QCOMPARE(reviewed462.southEast, qint32(16));
    QCOMPARE(reviewed462.southWest, qint32(16));
    for(const int slot : {218, 219})
    {
        const STerrainTransitionSignature irregularSand =
            snowRock.slotDefinitions.at(slot).signature;
        QCOMPARE(irregularSand.northWest, qint32(20));
        QCOMPARE(irregularSand.northEast, qint32(20));
        QCOMPARE(irregularSand.southEast, qint32(20));
        QCOMPARE(irregularSand.southWest, qint32(20));
    }
    QVERIFY(std::any_of(snowRock.families.cbegin(), snowRock.families.cend(),
        [](const STerrainAtlasProfileFamily& family) {
            return family.name == QStringLiteral("Sand / Irregular");
        }));
    const STerrainTransitionSignature reviewed220 =
        snowRock.slotDefinitions.at(220).signature;
    QCOMPARE(reviewed220.northWest, qint32(20));
    QCOMPARE(reviewed220.northEast, qint32(20));
    QCOMPARE(reviewed220.southEast, qint32(20));
    QCOMPARE(reviewed220.southWest, qint32(20));
    for(const int slot : {194, 195, 196})
    {
        const STerrainTransitionSignature signature =
            snowRock.slotDefinitions.at(slot).signature;
        QCOMPARE(signature.northEast, qint32(20));
        QCOMPARE(signature.southEast, qint32(20));
        QCOMPARE(signature.southWest, qint32(20));
    }
    for(const int slot : {374, 375, 392})
    {
        const STerrainTransitionSignature signature =
            snowRock.slotDefinitions.at(slot).signature;
        QCOMPARE(signature.southEast, qint32(20));
        QCOMPARE(signature.southWest, qint32(20));
    }
    for(const int slot : {384, 385})
    {
        const STerrainTransitionSignature signature =
            snowRock.slotDefinitions.at(slot).signature;
        QCOMPARE(signature.northEast, qint32(20));
        QCOMPARE(signature.southEast, qint32(20));
        QCOMPARE(signature.southWest, qint32(20));
    }
    for(const int slot : {292, 394, 395, 459, 507})
        QCOMPARE(snowRock.slotDefinitions.at(slot).signature.southWest,
                 qint32(20));
    for(const int slot : {396, 397})
        QCOMPARE(snowRock.slotDefinitions.at(slot).signature.southEast,
                 qint32(20));
    QCOMPARE(snowRock.slotDefinitions.at(396).signature.southWest,
             qint32(20));
    const STerrainTransitionSignature reviewed508 =
        snowRock.slotDefinitions.at(508).signature;
    QCOMPARE(reviewed508.northEast, qint32(20));
    QCOMPARE(reviewed508.southWest, qint32(20));

    CValidationReport zone7Report;
    const STerrainAtlasProfile zone7 = CTerrainAtlasPresetCatalog::exactProfile(
        "Zone7",
        "dacf1b6653cee1f4afc908b1ff3fe219784f4b4a1ff8d38fd46efb38aa9cd897",
        512, &zone7Report);
    QVERIFY2(zone7.approved, qPrintable(zone7Report.toText()));
    QCOMPARE(zone7.slotDefinitions.at(480).signature.southWest, qint32(17));
}

void EnduranceDocumentTests::semanticAtlasInferenceAssignsExactRepeatedPhysicalSlots()
{
    QImage grass(24, 24, QImage::Format_RGBA8888);
    grass.fill(QColor(30, 105, 35));
    QImage stone(24, 24, QImage::Format_RGBA8888);
    stone.fill(QColor(125, 120, 108));
    QImage transition = grass;
    for(int y = 0; y < transition.height(); ++y)
        for(int x = transition.width() / 2; x < transition.width(); ++x)
            transition.setPixelColor(x, y, stone.pixelColor(x, y));

    const QVector<QImage> cycle({grass, stone, transition});
    QVector<QImage> images;
    QVector<qint32> tileTypes;
    QVector<STerrainAtlasSlotUsage> usage;
    images.reserve(1024);
    tileTypes.fill(0, 1024);
    usage.fill(STerrainAtlasSlotUsage(), 1024);
    for(int slot = 0; slot < 1024; ++slot)
    {
        images.append(cycle.at(slot % cycle.size()));
        usage[slot].landCount = 1;
    }
    CValidationReport report;
    const STerrainAtlasProfile profile = CTerrainAtlasProfileInference::propose(
        "repeated_stock_pages", images, tileTypes, usage, &report);
    QCOMPARE(profile.atlasSlotCount, 1024);
    QCOMPARE(profile.assignedSlotCount(), 1024);
    QCOMPARE(profile.slotDefinitions.size(), 1024);
    for(const STerrainAtlasProfileSlot& slot : profile.slotDefinitions)
        QVERIFY(slot.assigned);
}

void EnduranceDocumentTests::authoredPresetSeparatesZone18ReviewedLayers()
{
    CValidationReport report;
    const STerrainAtlasProfile profile = CTerrainAtlasPresetCatalog::exactProfile(
        "Zone18",
        "8628607116ecfbc4fedd1c20e97690b3c55088d433c9719d6e95f650cfd73e81",
        512, &report);
    QVERIFY2(profile.approved, qPrintable(report.toText()));
    QCOMPARE(profile.assignedSlotCount(), 512);
    int sharedFamilies = 0;
    int reviewedLandOnlyFamilies = 0;
    const QStringList reviewedLandOnly = {
        QStringLiteral("Sand / Dark"),
        QStringLiteral("Stone / Orange"),
        QStringLiteral("Stone / Weathered")
    };
    for(const STerrainAtlasProfileFamily& family : profile.families)
    {
        sharedFamilies += family.landAllowed && family.overlayAllowed;
        if(reviewedLandOnly.contains(family.name))
        {
            QVERIFY(family.landAllowed);
            QVERIFY(!family.overlayAllowed);
            ++reviewedLandOnlyFamilies;
        }
    }
    QCOMPARE(sharedFamilies, 1);
    QCOMPARE(reviewedLandOnlyFamilies, 3);
    STerrainAtlasProfile reopened;
    QVERIFY2(STerrainAtlasProfile::fromJson(profile.toJson(), &reopened, &report),
             qPrintable(report.toText()));
    int reopenedSharedFamilies = 0;
    for(const STerrainAtlasProfileFamily& family : reopened.families)
        reopenedSharedFamilies += family.landAllowed && family.overlayAllowed;
    QCOMPARE(reopenedSharedFamilies, 1);
    QCOMPARE(reopened.schemaVersion, 10);
    const STerrainAtlasAnalysis analysis = profile.approvedAnalysis(&report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    bool sharedSlot = false;
    for(int slot = 0; slot < 512; ++slot)
    {
        SMprPackedTile packed;
        packed.atlasIndex = quint8(slot / 64);
        packed.tileIndex = quint8(slot % 64);
        bool land = false, overlay = false;
        analysis.landCatalog.gridSignatureForPacked(packed.encode(), &land);
        analysis.overlayCatalog.gridSignatureForPacked(packed.encode(), &overlay);
        sharedSlot |= land && overlay;
    }
    QVERIFY(!sharedSlot);
}

void EnduranceDocumentTests::completeMprCandidateCommitsWithBackupAndReopens()
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
    CValidationReport report;
    QVERIFY2(baseline.load(QFileInfo(sourcePath), &report),
             qPrintable(report.toText()));
    const SMprArchiveMember sector = baseline.members().at(2);
    QByteArray replacement = sector.payload;
    replacement[replacement.size() - 1] =
        char(replacement.back() ^ 0x01);
    CMprEditTransaction transaction("complete candidate");
    transaction.replaceMemberPayload(sector.name, sector.sha256, replacement);
    QByteArray candidate;
    QVERIFY2(baseline.applyTransaction(transaction, &candidate, &report),
             qPrintable(report.toText()));

    SMprBytesSaveCommitRequest request;
    request.source = QFileInfo(sourcePath);
    request.destination = QFileInfo(destinationPath);
    request.expectedSourceBytes = original;
    request.candidateBytes = candidate;
    request.backupRootOverride = dir.filePath("backups");
    const SMprBytesSaveCommitResult result =
        commitValidatedMprBytes(request);
    QVERIFY2(result.success, qPrintable(result.phase + ": " + result.error));
    QVERIFY(!result.backupPath.isEmpty());
    QFile backup(result.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);
    QFile destination(destinationPath);
    QVERIFY(destination.open(QIODevice::ReadOnly));
    QCOMPARE(destination.readAll(), candidate);
    CMprDocument reopened;
    QVERIFY2(reopened.load(QFileInfo(destinationPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.memberPayload(sector.name), replacement);
}

void EnduranceDocumentTests::authoredPresetCatalogAllEntriesCompile()
{
    QFile resource(QStringLiteral(":/terrain_atlas_presets.json"));
    QVERIFY(resource.open(QIODevice::ReadOnly));
    const QJsonObject catalog = QJsonDocument::fromJson(resource.readAll()).object();
    QCOMPARE(catalog.value(QStringLiteral("provenance")).toString(),
             QStringLiteral("built-in-material-atlas-v2"));
    QVERIFY(!catalog.contains(QStringLiteral("attribution")));
    const QJsonArray presets = catalog.value(QStringLiteral("presets")).toArray();
    QCOMPARE(presets.size(), 38);
    int totalSlots = 0, bindings = 0, totalFamilies = 0,
        transitionSupportFamilies = 0;
    QHash<QString, QByteArray> identityByBinding;
    for(const QJsonValue& value : presets)
    {
        const QJsonObject preset = value.toObject();
        const QString nameSpace = preset.value(QStringLiteral("namespace")).toString();
        const int slotCount = preset.value(QStringLiteral("slotCount")).toInt();
        const QJsonArray presetBindings = preset.value(QStringLiteral("bindings")).toArray();
        QVERIFY2(!presetBindings.isEmpty(), qPrintable(nameSpace));
        QJsonArray familyIdentity;
        for(const QJsonValue& familyValue : preset.value(QStringLiteral("families")).toArray())
        {
            const QJsonObject family = familyValue.toObject();
            QJsonObject identity;
            identity.insert(QStringLiteral("id"), family.value(QStringLiteral("id")));
            identity.insert(QStringLiteral("name"), family.value(QStringLiteral("name")));
            identity.insert(QStringLiteral("thumbnailSlot"),
                            family.value(QStringLiteral("thumbnailSlot")));
            identity.insert(QStringLiteral("paintSelectable"),
                            family.value(QStringLiteral("paintSelectable")));
            familyIdentity.append(identity);
        }
        QJsonObject atlasIdentity;
        atlasIdentity.insert(QStringLiteral("signaturesBase64"),
                             preset.value(QStringLiteral("signaturesBase64")));
        atlasIdentity.insert(QStringLiteral("families"), familyIdentity);
        const QByteArray encodedIdentity = QJsonDocument(atlasIdentity).toJson(
            QJsonDocument::Compact);
        for(const QJsonValue& bindingValue : presetBindings)
        {
            const QJsonObject binding = bindingValue.toObject();
            const QString bindingKey = binding.value(QStringLiteral("variant")).toString()
                + QLatin1Char(':')
                + binding.value(QStringLiteral("pixelSha256")).toString()
                + QLatin1Char(':') + QString::number(slotCount);
            if(identityByBinding.contains(bindingKey))
                QCOMPARE(identityByBinding.value(bindingKey), encodedIdentity);
            else
                identityByBinding.insert(bindingKey, encodedIdentity);
        }
        const QByteArray hash = presetBindings.first().toObject()
            .value(QStringLiteral("pixelSha256")).toString().toLatin1();
        QCOMPARE(hash.size(), 64);
        CValidationReport report;
        const STerrainAtlasProfile profile = CTerrainAtlasPresetCatalog::exactProfile(
            nameSpace, hash, slotCount, &report);
        QVERIFY2(profile.approved, qPrintable(nameSpace + QLatin1Char('\n') + report.toText()));
        QCOMPARE(profile.assignedSlotCount(), slotCount);
        for(const STerrainAtlasProfileFamily& family : profile.families)
        {
            QVERIFY2(!family.name.startsWith(QStringLiteral("Land material")),
                     qPrintable(nameSpace));
            QVERIFY(!family.ambiguous);
            bool expressibleBrush = false;
            for(const STerrainAtlasProfileSlot& definition : profile.slotDefinitions)
            {
                const qint32 corners[] = {definition.signature.northWest,
                                          definition.signature.northEast,
                                          definition.signature.southEast,
                                          definition.signature.southWest};
                int count = 0;
                QSet<qint32> others;
                for(qint32 id : corners)
                {
                    if(id == family.id) ++count;
                    else others.insert(id);
                }
                expressibleBrush |= count == 4 || (count == 1 && others.size() == 1);
            }
            // R106 removes dead records and normalizes brush authority: a
            // family is selectable exactly when it has a uniform tile or a
            // one-on-three authored stamp.
            QVERIFY2(family.paintSelectable == expressibleBrush,
                     qPrintable(nameSpace + QStringLiteral(": ") + family.name));
            transitionSupportFamilies += !family.paintSelectable;
        }
        for(const QJsonValue& familyValue : preset.value(QStringLiteral("families")).toArray())
            QVERIFY(!familyValue.toObject().contains(QStringLiteral("key")));
        const STerrainAtlasAnalysis analysis = profile.approvedAnalysis(&report);
        QVERIFY2(analysis.usable, qPrintable(nameSpace + QLatin1Char('\n') + report.toText()));
        QCOMPARE(analysis.describedSlots, slotCount);
        totalSlots += slotCount;
        bindings += presetBindings.size();
        totalFamilies += profile.families.size();
    }
    QCOMPARE(totalSlots, 16576);
    QCOMPARE(bindings, 58);
    // The synchronized R162 authority keeps every currently referenced
    // family record and its reviewed paintability classification.
    QCOMPARE(totalFamilies, 415);
    // A referenced family remains transition-only when it has neither a
    // uniform tile nor a one-on-three authored stamp.
    QCOMPARE(transitionSupportFamilies, 24);
}

void EnduranceDocumentTests::globalMaterialVariantsUseOnlyUniformAnchorsAndShareThemeGroups()
{
    CValidationReport report;
    const QVector<STerrainGlobalMaterialFamily> families =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(families.size() >= 10);
    QSet<qint32> identities;
    qint32 darkGrassId = 0;
    qint32 commonGrassId = 0;
    qint32 freshGrassId = 0;
    int darkGrassSources = 0;
    QSet<int> bz11kIceSlots;
    QSet<int> zone14IceSlots;
    bool sawBz11kIceThumbnail = false;
    bool sawZone14IceThumbnail = false;
    int grassVariantCount = 0;
    int landFamilyCount = 0;
    QSet<int> blendStyles;
    for(const STerrainGlobalMaterialFamily& family : families)
    {
        QVERIFY(family.id > 255);
        QVERIFY(!identities.contains(family.id));
        identities.insert(family.id);
        QVERIFY(!family.theme.isEmpty());
        QVERIFY(!family.sources.isEmpty());
        const ETerrainBlendStyle blendStyle =
            CTerrainVirtualAtlas::blendStyleForMaterialName(family.name);
        QVERIFY2(blendStyle != ETerrainBlendStyle::Organic,
                 qPrintable(QStringLiteral("Unclassified stock JMV material: %1")
                            .arg(family.name)));
        blendStyles.insert(int(blendStyle));
        if(family.layer == ETerrainAtlasLayer::Land) ++landFamilyCount;
        for(const STerrainGlobalMaterialSource& source : family.sources)
        {
            QVERIFY(source.uniformSlot >= 0);
            QVERIFY(source.textureCount > source.uniformSlot / 64);
            QVERIFY(source.atlasSlotCount > source.uniformSlot);
            QVERIFY(source.atlasSlotCount <= source.textureCount * 64);
            QCOMPARE(source.atlasPixelSha256.size(), 64);
            QVERIFY(!source.variantName.isEmpty());
            QVERIFY(!source.atlasIdentity.isEmpty());
            QCOMPARE(source.variantName, family.name);
            QCOMPARE(CTerrainAtlasPresetCatalog::materialTheme(
                         source.variantName), family.theme);
            // Reviewed transition-support classifications must never leak
            // into the global brush palette as purported uniform sources.
            QVERIFY(!(source.variantName == QStringLiteral("Stone / Grayish")
                      && source.mprNamespace == QStringLiteral("Zone8")));
            QVERIFY(!(source.variantName == QStringLiteral("Ground / Dirt")
                      && source.mprNamespace == QStringLiteral("zone9")));
            QVERIFY(!(source.variantName == QStringLiteral("Ground / Dry")
                      && (source.mprNamespace == QStringLiteral("bz5g")
                          || source.mprNamespace == QStringLiteral("bz6g")
                          || source.mprNamespace == QStringLiteral("Zone8"))));
            if(source.variantName == QStringLiteral("Ice / Common")
               && source.mprNamespace == QStringLiteral("bz11k"))
                bz11kIceSlots.insert(source.uniformSlot);
            if(source.variantName == QStringLiteral("Ice / Common")
               && source.mprNamespace == QStringLiteral("Zone14"))
                zone14IceSlots.insert(source.uniformSlot);
        }
        if(family.name == QStringLiteral("Ice / Common"))
            for(const STerrainGlobalMaterialSource& source : family.thumbnailSources)
            {
                sawBz11kIceThumbnail |= source.mprNamespace
                    == QStringLiteral("bz11k") && source.uniformSlot == 4;
                sawZone14IceThumbnail |= source.mprNamespace
                    == QStringLiteral("Zone14") && source.uniformSlot == 4;
            }
        for(const STerrainGlobalMaterialSource& thumbnail : family.thumbnailSources)
        {
            bool isReviewedUniform = false;
            for(const STerrainGlobalMaterialSource& source : family.sources)
                isReviewedUniform |= source.mprNamespace.compare(
                    thumbnail.mprNamespace, Qt::CaseInsensitive) == 0
                    && source.uniformSlot == thumbnail.uniformSlot;
            QVERIFY2(isReviewedUniform,
                     qPrintable(QStringLiteral("Mixed palette thumbnail leaked into %1 from %2:%3")
                         .arg(family.name, thumbnail.mprNamespace)
                         .arg(thumbnail.uniformSlot)));
        }
        if(family.layer == ETerrainAtlasLayer::Land
           && family.theme == QStringLiteral("Grass")) ++grassVariantCount;
        if(family.layer == ETerrainAtlasLayer::Land
           && family.name == QStringLiteral("Grass / Dark"))
        {
            QCOMPARE(darkGrassId, qint32(0));
            darkGrassId = family.id;
            darkGrassSources = family.sources.size();
        }
        if(family.layer == ETerrainAtlasLayer::Land
           && family.name == QStringLiteral("Grass / Common"))
            commonGrassId = family.id;
        if(family.layer == ETerrainAtlasLayer::Land
           && family.name == QStringLiteral("Grass / Fresh"))
            freshGrassId = family.id;
    }
    QVERIFY(darkGrassId > 255);
    QVERIFY(commonGrassId > 255);
    QVERIFY(freshGrassId > 255);
    QVERIFY(darkGrassSources >= 5);
    QVERIFY(grassVariantCount >= 4);
    QVERIFY(darkGrassId != commonGrassId);
    QVERIFY(darkGrassId != freshGrassId);
    QVERIFY(commonGrassId != freshGrassId);
    QCOMPARE(bz11kIceSlots.size(), 23);
    QCOMPARE(zone14IceSlots.size(), 23);
    QVERIFY(bz11kIceSlots.contains(3));
    QVERIFY(bz11kIceSlots.contains(4));
    QVERIFY(bz11kIceSlots.contains(342));
    QCOMPARE(bz11kIceSlots, zone14IceSlots);
    QVERIFY(sawBz11kIceThumbnail);
    QVERIFY(sawZone14IceThumbnail);
    // This is the reviewed Family/Subfamily palette, after collapsing atlas
    // variants and excluding transition-only/mixed imagery.
    // Two remaining reviewed names currently have no visually verified pure
    // anchor. Keep them as transition support rather than leaking a mixed
    // thumbnail into the brush palette.
    // R102 applies the explicit brush-inventory merges and names while leaving
    // false-uniform and probable-family leads out of the paint palette. R112
    // adds the reviewed Zone10 Slabs / Faces irregular uniform brush family.
    QCOMPARE(landFamilyCount, 81);
    // Every currently shipped paintable JMV class has an intentional visual
    // language; Organic remains only the forward-compatible fallback.
    QCOMPARE(blendStyles.size(), 11);
}

void EnduranceDocumentTests::globalMaterialBrushSourceIsPayloadCoherentAndPrefersLocalUniform()
{
    const auto page = [](const QColor& first, const QColor& second) {
        QVector<QImage> tiles;
        for(int slot = 0; slot < 64; ++slot)
        {
            QImage tile(8, 8, QImage::Format_RGBA8888);
            tile.fill(slot == 0 ? first : second);
            tiles.append(tile);
        }
        QString diagnostic;
        return ei::encodeMmpDxt1(
            CTerrainVirtualAtlas::composePage(tiles, 0, 8), true,
            &diagnostic);
    };
    const QByteArray foreignPage = page(Qt::red, Qt::yellow);
    const QByteArray preferredPage = page(Qt::blue, Qt::green);
    QVERIFY(!foreignPage.isEmpty());
    QVERIFY(!preferredPage.isEmpty());

    STerrainGlobalMaterialSource foreign;
    foreign.mprNamespace = QStringLiteral("foreign");
    foreign.textureCount = 1;
    foreign.uniformSlot = 0;
    foreign.variantName = QStringLiteral("Ground / Test");
    STerrainGlobalMaterialSource preferred = foreign;
    preferred.mprNamespace = QStringLiteral("preferred");
    preferred.uniformSlot = 1;
    STerrainGlobalMaterialFamily family;
    family.name = foreign.variantName;
    STerrainGlobalMaterialSource preferredFirst = preferred;
    preferredFirst.uniformSlot = 0;
    family.sources = {foreign, preferredFirst, preferred};
    // The reviewed local thumbnail is also the stable compositor base. This
    // keeps a corrected icon and the actual generated blend on one artwork;
    // same-material interiors still vary through the authored catalog.
    family.thumbnailSources = {preferred};

    QMap<QString, QByteArray> encodedPages;
    QSet<QString> missingPages;
    QMap<QString, QImage> decodedTiles;
    STerrainGlobalMaterialSource selected;
    const QImage thumbnail =
        CTerrainAtlasPresetCatalog::loadGlobalUniformSource(
            family, QStringLiteral("preferred"), QSize(8, 8),
            &encodedPages, &missingPages, &decodedTiles,
            [&](const QString& name, QByteArray* payload) {
                if(name == QStringLiteral("preferred000.mmp"))
                    *payload = preferredPage;
                else if(name == QStringLiteral("foreign000.mmp"))
                    *payload = foreignPage;
                else
                    return false;
                return true;
            }, true, &selected);
    QVERIFY(!thumbnail.isNull());
    QCOMPARE(selected.mprNamespace, QStringLiteral("preferred"));
    QCOMPARE(selected.uniformSlot, 1);
    QImage expectedPreferred;
    QString diagnostic;
    QVERIFY(ei::decodeMmpDxt1AtlasTile(
        preferredPage, 1, &expectedPreferred, &diagnostic));
    QCOMPARE(thumbnail, expectedPreferred.convertToFormat(
        QImage::Format_RGBA8888));

    encodedPages.clear();
    missingPages.clear();
    STerrainGlobalMaterialSource paintedSource;
    const QImage paintedBase =
        CTerrainAtlasPresetCatalog::loadGlobalUniformSource(
            family, QStringLiteral("preferred"), QSize(8, 8),
            &encodedPages, &missingPages, &decodedTiles,
            [&](const QString& name, QByteArray* payload) {
                if(name == QStringLiteral("preferred000.mmp"))
                    *payload = preferredPage;
                else if(name == QStringLiteral("foreign000.mmp"))
                    *payload = foreignPage;
                else
                    return false;
                return true;
            }, false, &paintedSource);
    QVERIFY(!paintedBase.isNull());
    QCOMPARE(paintedSource.mprNamespace, QStringLiteral("preferred"));
    QCOMPARE(paintedSource.uniformSlot, 1);
    QCOMPARE(paintedBase, expectedPreferred.convertToFormat(
        QImage::Format_RGBA8888));

    // The map-load path must not hydrate complete 2048x2048 pages merely to
    // obtain palette/source tiles. Prove the direct indexed-tile callback is
    // authoritative when supplied and the full-page fallback remains unused.
    int fullPageLoads = 0;
    int directTileLoads = 0;
    encodedPages.clear();
    missingPages.clear();
    const QImage directBase =
        CTerrainAtlasPresetCatalog::loadGlobalUniformSource(
            family, QStringLiteral("preferred"), QSize(8, 8),
            &encodedPages, &missingPages, &decodedTiles,
            [&](const QString&, QByteArray*) {
                ++fullPageLoads;
                return false;
            }, false, &paintedSource,
            [&](const QString& name, int logicalSlot, QImage* tile,
                QString* sourceIdentity) {
                ++directTileLoads;
                const QByteArray* payload = nullptr;
                if(name == QStringLiteral("preferred000.mmp"))
                    payload = &preferredPage;
                else if(name == QStringLiteral("foreign000.mmp"))
                    payload = &foreignPage;
                if(!payload)
                    return false;
                if(sourceIdentity)
                    *sourceIdentity = name + QStringLiteral("|test-resource");
                return ei::decodeMmpDxt1AtlasTile(
                    *payload, logicalSlot, tile, &diagnostic);
            });
    QVERIFY(!directBase.isNull());
    QCOMPARE(directBase, expectedPreferred.convertToFormat(
        QImage::Format_RGBA8888));
    QCOMPARE(fullPageLoads, 0);
    QVERIFY(directTileLoads > 0);

    // Reusing one decoded cache across two maps used to return the first map's
    // tile solely because the RES member name and slot matched. The current
    // encoded payload is now part of the key, so the second map cannot inherit
    // a stale uniform or blend image from the first one.
    STerrainGlobalMaterialFamily sameNamed;
    sameNamed.name = QStringLiteral("Ground / Same named page");
    STerrainGlobalMaterialSource sameNamedSource = foreign;
    sameNamedSource.mprNamespace = QStringLiteral("shared");
    sameNamed.sources = {sameNamedSource};
    const auto loadShared = [&](const QByteArray& currentPage) {
        QMap<QString, QByteArray> currentEncoded;
        QSet<QString> currentMissing;
        return CTerrainAtlasPresetCatalog::loadGlobalUniformSource(
            sameNamed, QStringLiteral("shared"), QSize(8, 8),
            &currentEncoded, &currentMissing, &decodedTiles,
            [&](const QString& name, QByteArray* payload) {
                if(name != QStringLiteral("shared000.mmp"))
                    return false;
                *payload = currentPage;
                return true;
            });
    };
    const QImage first = loadShared(foreignPage);
    const QImage second = loadShared(preferredPage);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QVERIFY(first != second);
    QImage expectedSecond;
    QVERIFY(ei::decodeMmpDxt1AtlasTile(
        preferredPage, 0, &expectedSecond, &diagnostic));
    QCOMPARE(second, expectedSecond.convertToFormat(
        QImage::Format_RGBA8888));

    // Authored-transition imports used to keep a page-name-only cache across
    // map switches. Prove that the same member and slot resolve from the
    // current payload rather than returning the previous map's artwork.
    STerrainGlobalTransitionSource transition;
    transition.mprNamespace = QStringLiteral("shared");
    transition.textureCount = 1;
    transition.slot = 0;
    QMap<QString, QByteArray> transitionPages;
    QSet<QString> transitionMissing;
    QMap<QString, QImage> transitionTiles;
    QByteArray currentTransitionPage = foreignPage;
    const auto loadTransition = [&]() {
        return CTerrainAtlasPresetCatalog::loadGlobalTransitionSource(
            transition, QSize(8, 8), &transitionPages,
            &transitionMissing, &transitionTiles,
            [&](const QString& name, QByteArray* payload) {
                if(name != QStringLiteral("shared000.mmp"))
                    return false;
                *payload = currentTransitionPage;
                return true;
            });
    };
    const QImage firstTransition = loadTransition();
    currentTransitionPage = preferredPage;
    const QImage secondTransition = loadTransition();
    QVERIFY(!firstTransition.isNull());
    QVERIFY(!secondTransition.isNull());
    QVERIFY(firstTransition != secondTransition);
    QCOMPARE(secondTransition, expectedSecond.convertToFormat(
        QImage::Format_RGBA8888));
}

void EnduranceDocumentTests::globalTerrainSourcesUseOnlyApprovedHdBindings()
{
    const QByteArray zone1Repaint = "a44f78f46933dfba29125ec6867c3541acb6be56d93083afa6a6ce9616b5f997";
    QVERIFY2(!CTerrainAtlasPresetCatalog::exactProfile("zone1", zone1Repaint, 512)
                  .slotDefinitions.isEmpty(), "User-approved Zone1 repaint is not paintable");
    QVERIFY(CTerrainAtlasPresetCatalog::exactProfile("zone1", QByteArray(64, 'f'), 512)
                .slotDefinitions.isEmpty());
    QVERIFY(CTerrainAtlasPresetCatalog::exactProfile("zone1", zone1Repaint, 511)
                .slotDefinitions.isEmpty());
    QFile resource(QStringLiteral(":/terrain_atlas_presets.json"));
    QVERIFY(resource.open(QIODevice::ReadOnly));
    const QJsonObject catalog = QJsonDocument::fromJson(
        resource.readAll()).object();
    QMap<QString, QSet<QByteArray>> hdByNamespace;
    QMap<QString, QSet<QByteArray>> nonHdByNamespace;
    for(const QJsonValue& presetValue : catalog.value(
            QStringLiteral("presets")).toArray())
    {
        const QJsonObject preset = presetValue.toObject();
        const QString name = preset.value(QStringLiteral("namespace"))
            .toString().toCaseFolded();
        for(const QJsonValue& bindingValue : preset.value(
                QStringLiteral("bindings")).toArray())
        {
            const QJsonObject binding = bindingValue.toObject();
            const QByteArray hash = binding.value(
                QStringLiteral("pixelSha256")).toString()
                .toLatin1().toLower();
            if(binding.value(QStringLiteral("variant")).toString()
                    == QStringLiteral("hd"))
                hdByNamespace[name].insert(hash);
            else
                nonHdByNamespace[name].insert(hash);
        }
    }

    CValidationReport report;
    const QVector<STerrainGlobalMaterialFamily> families =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    int materialSources = 0;
    bool repaintMaterialSource = false;
    for(const STerrainGlobalMaterialFamily& family : families)
        for(const QVector<STerrainGlobalMaterialSource>* list :
                {&family.sources, &family.thumbnailSources})
            for(const STerrainGlobalMaterialSource& source : *list)
            {
                ++materialSources;
                repaintMaterialSource |= source.mprNamespace.compare("zone1", Qt::CaseInsensitive) == 0
                    && source.atlasPixelSha256 == zone1Repaint;
                const QString name = source.mprNamespace.toCaseFolded();
                QVERIFY2(hdByNamespace.value(name).contains(
                             source.atlasPixelSha256),
                         qPrintable(QStringLiteral(
                             "Global Modern material source is not an approved HD binding: %1 %2")
                             .arg(source.mprNamespace,
                                  QString::fromLatin1(
                                      source.atlasPixelSha256))));
                QSet<QByteArray> originalOnly = nonHdByNamespace.value(name);
                originalOnly.subtract(hdByNamespace.value(name));
                QVERIFY2(!originalOnly.contains(source.atlasPixelSha256),
                         "Original-tier artwork can enter HD Modern generation");
            }
    QVERIFY(materialSources > 0);
    QVERIFY(repaintMaterialSource);

    int transitionSources = 0;
    bool repaintTransitionSource = false;
    for(const STerrainGlobalTransitionSource& source :
            CTerrainAtlasPresetCatalog::globalAuthoredTransitions(&report))
    {
        ++transitionSources;
        repaintTransitionSource |= source.mprNamespace.compare("zone1", Qt::CaseInsensitive) == 0
            && source.atlasPixelSha256 == zone1Repaint;
        const QString name = source.mprNamespace.toCaseFolded();
        QVERIFY2(hdByNamespace.value(name).contains(
                     source.atlasPixelSha256),
                 qPrintable(QStringLiteral(
                     "Global authored transition is not an approved HD binding: %1:%2 %3")
                     .arg(source.mprNamespace).arg(source.slot)
                     .arg(QString::fromLatin1(source.atlasPixelSha256))));
        QSet<QByteArray> originalOnly = nonHdByNamespace.value(name);
        originalOnly.subtract(hdByNamespace.value(name));
        QVERIFY2(!originalOnly.contains(source.atlasPixelSha256),
                 "Original-tier authored transition can enter HD Modern painting");
    }
    QVERIFY(transitionSources > 0);
    QVERIFY(repaintTransitionSource);
}

void EnduranceDocumentTests::globalTerrainHydrationRequiresExactPresetAtlasBinding()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(), "Could not locate source/render/view.cpp");
    const int start = source.indexOf(
        "QImage loadBoundTerrainAtlasTile(");
    const int end = source.indexOf("QVector4D aiGraphLayerColor", start);
    QVERIFY(start >= 0 && end > start);
    const QByteArray loader = source.mid(start, end - start);

    // A resource member name is not an atlas identity: different configured
    // RES archives can contain the same Zone1 page names with unrelated
    // pixels. Global semantic signatures may only be paired with an archive
    // whose complete decoded atlas matches an approved preset binding.
    QVERIFY2(loader.contains("QImage loadBoundGlobalUniformSource("),
             "Uniform material sources bypass the bound-atlas loader");
    QVERIFY2(loader.contains("QImage loadGlobalTransitionTile("),
             "Global transitions bypass the bound-atlas loader");
    QVERIFY2(loader.contains("findExactArchive("),
             "Global terrain pixels bypass exact preset-atlas binding");
    QVERIFY2(!loader.contains("catalogTerrainMmpPayload("),
             "Global terrain pixels still use a name-only resource lookup");
    QVERIFY2(!loader.contains("decodedAtlasTiles"),
             "Global palette hydration again retains complete decoded atlases");
    QVERIFY2(loader.contains("readArchiveMembers("),
             "Bound global sources no longer decode only their requested page");

    const int rebuildStart = source.indexOf(
        "void CView::rebuildTerrainAutoCatalog()");
    const int rebuildEnd = source.indexOf(
        "void CView::refreshClassicAutoPalette()", rebuildStart);
    QVERIFY(rebuildStart >= 0 && rebuildEnd > rebuildStart);
    const QByteArray rebuild = source.mid(
        rebuildStart, rebuildEnd - rebuildStart);
    QVERIFY2(!rebuild.contains("catalogTerrainMmpTile("),
             "Palette/compositor uniforms still use name-only tile lookup");
    QVERIFY2(!rebuild.contains("catalogTerrainMmpPayload("),
             "Palette/compositor uniforms still use name-only page lookup");
    QVERIFY2(!rebuild.contains("m_terrainGlobalBoundAtlasCache.clear()"),
             "Map switches again discard verified exact atlas bindings");
    QVERIFY2(!rebuild.contains("for(const STerrainGlobalMaterialFamily& family : m_terrainGlobalFamilies)\n        {\n            STerrainGlobalMaterialSource selectedSource;\n            const QImage source = loadBoundGlobalUniformSource("),
             "Palette construction again verifies every foreign atlas synchronously");
    QVERIFY2(rebuild.contains("currentMapGlobalUniformSource("),
             "Palette construction no longer reuses already-loaded current-map material artwork");
    QVERIFY2(rebuild.contains("cachedGlobalUniformSource(")
             && rebuild.contains("m_terrainGlobalUniformTileCache"),
             "A second map load discards already-verified reviewed-HD thumbnails and republishes an empty palette while decoding them again");
    QVERIFY2(!rebuild.contains("const QImage source = loadBoundGlobalUniformSource("),
             "Palette construction again performs foreign exact-archive verification eagerly");
    QVERIFY2(rebuild.contains("startDeferredGlobalTerrainPaletteHydration("),
             "Foreign reviewed-HD thumbnails are not hydrated after the visible map frame");

    QVERIFY2(source.contains("m_terrainGlobalHydrationPool->setMaxThreadCount(2)"),
             "Foreign terrain hydration is not kept on a bounded worker pool");
    const int hydrationStart = source.indexOf(
        "void CView::startDeferredGlobalTerrainPaletteHydration(");
    const int hydrationEnd = source.indexOf(
        "void CView::applyDeferredGlobalTerrainPaletteHydration(", hydrationStart);
    QVERIFY2(hydrationStart >= 0 && hydrationEnd > hydrationStart,
             "Deferred foreign terrain hydration implementation was not found");
    const QByteArray hydration = source.mid(
        hydrationStart, hydrationEnd - hydrationStart);
    QVERIFY2(hydration.contains("QtConcurrent::run(")
             && hydration.contains("m_terrainGlobalHydrationPool, [task]()"),
             "Each foreign terrain binding is not verified off the UI thread");
    QVERIFY2(source.contains("applyDeferredGlobalTerrainPaletteHydration("),
             "Deferred exact-binding results are never published back to the palettes");

    const int adoptStart = source.indexOf(
        "bool CView::adoptPrewarmedTerrainFamily(");
    const int adoptEnd = source.indexOf(
        "void CView::refreshClassicAutoPalette()", adoptStart);
    QVERIFY(adoptStart >= 0 && adoptEnd > adoptStart);
    const QByteArray adopt = source.mid(adoptStart, adoptEnd - adoptStart);
    QVERIFY2(adopt.contains("mergeTerrainHydrationCaches("),
             "Adopting an older family-prewarm snapshot still replaces thumbnail caches populated by newer background hydration");
    QVERIFY2(!adopt.contains(
                 "m_terrainGlobalUniformTileCache = staged->uniformTileCache"),
             "Family-prewarm adoption can erase every already-hydrated brush thumbnail");

    const int finalizeStart = source.indexOf(
        "void CView::finalizeVirtualTerrainAtlas()");
    const int finalizeEnd = source.indexOf(
        "void CView::onChangeCursorTile(QPixmap ico)", finalizeStart);
    QVERIFY(finalizeStart >= 0 && finalizeEnd > finalizeStart);
    const QByteArray finalize = source.mid(
        finalizeStart, finalizeEnd - finalizeStart);
    QVERIFY2(finalize.contains("terrain.finalize_unrealized_signature"),
             "Publication can reach an under-sourced generation path instead of failing closed");

    const int refreshStart = source.indexOf(
        "void CView::refreshAfterResourceSetup()");
    const int refreshEnd = source.indexOf(
        "void CView::updateWindow()", refreshStart);
    QVERIFY(refreshStart >= 0 && refreshEnd > refreshStart);
    const QByteArray refresh = source.mid(
        refreshStart, refreshEnd - refreshStart);
    QVERIFY2(refresh.contains("m_terrainGlobalBoundAtlasCache.clear()"),
             "Resource-profile changes no longer invalidate exact bindings");
}

void EnduranceDocumentTests::directTerrainTileDecoderReadsOnlyRequestedTileBand()
{
    QVector<QImage> tiles;
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage tile(32, 32, QImage::Format_RGBA8888);
        tile.fill(QColor::fromHsv((slot * 37) % 360, 180, 80 + slot * 2));
        tiles.append(tile);
    }
    QString diagnostic;
    const QByteArray page = ei::encodeMmpDxt1(
        CTerrainVirtualAtlas::composePage(tiles, 0, 32), true,
        &diagnostic);
    QVERIFY2(!page.isEmpty(), qPrintable(diagnostic));

    class CountingBuffer final : public QBuffer
    {
    public:
        explicit CountingBuffer(QByteArray* data) : QBuffer(data) {}
        qint64 bytesRead{0};

    protected:
        qint64 readData(char* data, qint64 maxSize) override
        {
            const qint64 count = QBuffer::readData(data, maxSize);
            if(count > 0)
                bytesRead += count;
            return count;
        }
    };

    QByteArray wrapped = QByteArrayLiteral("prefix") + page
        + QByteArrayLiteral("suffix");
    CountingBuffer device(&wrapped);
    QVERIFY(device.open(QIODevice::ReadOnly));
    QImage direct;
    QVERIFY2(ei::decodeMmpDxt1AtlasTile(
        &device, 6, page.size(), 37, &direct, &diagnostic),
        qPrintable(diagnostic));
    QImage expected;
    QVERIFY2(ei::decodeMmpDxt1AtlasTile(
        page, 37, &expected, &diagnostic), qPrintable(diagnostic));
    QCOMPARE(direct, expected);
    QVERIFY2(device.bytesRead < page.size() / 4,
        qPrintable(QStringLiteral("Direct tile decode read %1 of %2 bytes")
            .arg(device.bytesRead).arg(page.size())));
}

void EnduranceDocumentTests::stockInputsCannotOverrideCanonicalOneCornerMask()
{
    CTerrainTransitionCatalog catalog;
    QVector<QImage> tiles;
    const QColor ownerColor(220, 40, 30);
    const QColor otherColor(25, 65, 210);
    for(int material = 1; material <= 2; ++material)
    {
        QImage image(32, 32, QImage::Format_RGBA8888);
        image.fill(material == 1 ? ownerColor : otherColor);
        tiles.append(image);
        STerrainTransitionVariant pure;
        pure.signature.northWest = pure.signature.northEast =
            pure.signature.southEast = pure.signature.southWest = material;
        pure.packedTile = quint16(material - 1);
        QVERIFY(catalog.addVariant(pure));
    }
    const QMap<qint32, ETerrainBlendStyle> styles = {
        {1, ETerrainBlendStyle::Grass}, {2, ETerrainBlendStyle::Soil}};
    CTerrainVirtualAtlas atlas(catalog, tiles, 17u, {}, styles);
    QVERIFY2(atlas.isValid(), qPrintable(atlas.error()));

    STerrainTransitionSignature oneCorner;
    oneCorner.northWest = 1;
    oneCorner.northEast = oneCorner.southEast = oneCorner.southWest = 2;
    QString baselineProvenance;
    const QImage baseline = atlas.reviewBlendMask(
        oneCorner, 1, 0, &baselineProvenance);
    QVERIFY(!baseline.isNull());
    QCOMPARE(baselineProvenance,
        QStringLiteral("canonical-generated:grass-strands:outer-1of4-v1"));
    const auto transition = [&](bool leaky) {
        QImage pixels(32, 32, QImage::Format_RGBA8888);
        for(int y = 0; y < pixels.height(); ++y)
            for(int x = 0; x < pixels.width(); ++x)
            {
                const int fromBottom = pixels.height() - 1 - y;
                const bool owner = leaky
                    ? ((y == pixels.height() - 1 && x < 31)
                       || (x == 0 && fromBottom < 31)
                       || (x + fromBottom < 10))
                    : (x + fromBottom < 18);
                pixels.setPixelColor(x, y, owner ? ownerColor : otherColor);
            }
        return pixels;
    };
    QVERIFY(atlas.addStockBlendMaskTemplate(
        oneCorner, transition(true), QStringLiteral("aaa-leaky-mask")));
    QVERIFY(atlas.addStockBlendMaskTemplate(
        oneCorner, transition(false), QStringLiteral("zzz-contained-mask")));

    QString provenance;
    const QImage selected = atlas.reviewBlendMask(
        oneCorner, 1, 0, &provenance);
    QVERIFY(!selected.isNull());
    QCOMPARE(selected, baseline);
    QCOMPARE(provenance, baselineProvenance);
    int foreignRight = 0;
    int foreignTop = 0;
    for(int index = 0; index < selected.height(); ++index)
        foreignRight += selected.pixelColor(selected.width() - 1, index).red()
            >= 128;
    for(int index = 0; index < selected.width(); ++index)
        foreignTop += selected.pixelColor(index, 0).red()
            >= 128;
    QCOMPARE(foreignRight, 0);
    QCOMPARE(foreignTop, 0);
}

void EnduranceDocumentTests::workingAtlasRealizesExactTransitionsBeyond512Slots()
{
    CTerrainTransitionCatalog authored;
    STerrainTransitionVariant uniform;
    uniform.signature.northWest = uniform.signature.northEast =
        uniform.signature.southEast = uniform.signature.southWest = 1;
    uniform.packedTile = 0;
    QVERIFY(authored.addVariant(uniform));

    QVector<QImage> tiles;
    tiles.reserve(512);
    for(int slot = 0; slot < 512; ++slot)
    {
        QImage image(16, 16, QImage::Format_RGBA8888);
        image.fill(QColor(30 + slot % 30, 90, 35));
        tiles.append(image);
    }
    QMap<qint32, QImage> sources;
    for(int material = 1; material <= 4; ++material)
    {
        QImage source(16, 16, QImage::Format_RGBA8888);
        source.fill(QColor(material * 45, 30 + material * 20, 180));
        sources.insert(material, source);
    }
    QVector<qint32> tileTypes(tiles.size(), 0);
    const QMap<qint32, qint32> materialTypes = {
        {1, 1}, {2, 2}, {3, 3}, {4, 4}};
    CTerrainWorkingAtlas working(authored, tiles, 77u, 1024, sources, {},
                                 tileTypes, materialTypes);
    QImage reviewedMaskPixels(16, 16, QImage::Format_RGBA8888);
    for(int y = 0; y < reviewedMaskPixels.height(); ++y)
        for(int x = 0; x < reviewedMaskPixels.width(); ++x)
            reviewedMaskPixels.setPixelColor(x, y,
                x * x + (reviewedMaskPixels.height() - 1 - y)
                    * (reviewedMaskPixels.height() - 1 - y) < 100
                    ? sources.value(1).pixelColor(x, y)
                    : sources.value(2).pixelColor(x, y));
    STerrainTransitionSignature reviewedOneCorner;
    reviewedOneCorner.northWest = 1;
    reviewedOneCorner.northEast = reviewedOneCorner.southEast =
        reviewedOneCorner.southWest = 2;
    QVERIFY(working.addStockBlendMaskTemplate(reviewedOneCorner,
        reviewedMaskPixels, QStringLiteral("reviewed-stock-endurance-fixture")));
    QVERIFY(working.isValid());
    QCOMPARE(working.capacityLimit(), 1024);
    QCOMPARE(working.activeTextureCount(), 8);

    QMap<int, quint64> usage;
    for(int slot = 0; slot < 512; ++slot)
        usage.insert(slot, 1);
    STerrainTransitionSignature fourFamily;
    fourFamily.northWest = 1;
    fourFamily.northEast = 2;
    fourFamily.southEast = 3;
    fourFamily.southWest = 4;
    CValidationReport report;
    bool generated = false;
    const quint16 packed = working.resolve(
        fourFamily, 10, 20, &usage, {}, &generated, &report, 1);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(generated);
    QCOMPARE(SMprPackedTile::decode(packed).atlasSlot(), 512);
    QCOMPARE(working.activeTextureCount(), 9);
    QCOMPARE(working.activeTileImages().size(), 9 * 64);
    QCOMPARE(working.activeTileCount(), 513);
    QCOMPARE(working.activeTileTypes().size(), 513);
    QVERIFY(working.activeTileTypes().last() >= 0);
}

void EnduranceDocumentTests::globalAuthoredTransitionLibraryIndexesRealStockBlends()
{
    CValidationReport report;
    const QVector<STerrainGlobalTransitionSource> transitions =
        CTerrainAtlasPresetCatalog::globalAuthoredTransitions(&report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(!transitions.isEmpty());
    int mixed = 0;
    QSet<QString> namespaces;
    for(const STerrainGlobalTransitionSource& source : transitions)
    {
        QVERIFY(!source.mprNamespace.isEmpty());
        QCOMPARE(source.atlasPixelSha256.size(), 64);
        QVERIFY(source.textureCount > 0);
        QVERIFY(source.atlasSlotCount > 0);
        QVERIFY(source.slot >= 0);
        QVERIFY(source.slot < source.atlasSlotCount);
        QVERIFY(source.atlasSlotCount <= source.textureCount * 64);
        const qint32 corners[] = {source.signature.northWest,
                                  source.signature.northEast,
                                  source.signature.southEast,
                                  source.signature.southWest};
        for(qint32 corner : corners)
            QVERIFY(corner > 255);
        mixed += corners[0] != corners[1] || corners[0] != corners[2]
            || corners[0] != corners[3];
        namespaces.insert(source.mprNamespace.toCaseFolded());
    }
    QVERIFY2(mixed > 0, "The global authored library indexed no blended tiles.");
    QVERIFY(namespaces.size() > 1);
}

void EnduranceDocumentTests::multiRowTerrainAtlasKeepsAuthoredPagesInPlace()
{
    QCOMPARE(CTile::preferredAtlasColumns(8), 8);
    QCOMPARE(CTile::preferredAtlasColumns(9), 3);
    QCOMPARE(CTile::preferredAtlasColumns(32), 6);
    QCOMPARE(CTile::preferredAtlasColumns(64), 8);
    QCOMPARE(CTile::preferredAtlasColumns(128), 12);
    QCOMPARE(CTile::preferredAtlasColumns(256), 16);
    QCOMPARE(CTile::preferredAtlasColumns(257), 17);
    QCOMPARE(CTile::atlasPageRowOrigin(0, 8, 8), 0.0f);
    QCOMPARE(CTile::atlasPageRowOrigin(7, 8, 8), 0.0f);
    QCOMPARE(CTile::atlasPageRowOrigin(0, 9, 3), 0.0f);
    QVERIFY(qFuzzyCompare(CTile::atlasPageRowOrigin(3, 9, 3), 1.0f / 3.0f));
    QVERIFY(qFuzzyCompare(CTile::atlasPageRowOrigin(8, 9, 3), 2.0f / 3.0f));
}

void EnduranceDocumentTests::zone2TransitionOnlyGrassMapsToGlobalUniformFamily()
{
    CValidationReport report;
    const STerrainAtlasProfile profile = CTerrainAtlasPresetCatalog::exactProfile(
        QStringLiteral("Zone2"),
        QByteArrayLiteral("ed0bc69bd4abbb8f9f90fab44574a24b0b806521b6bc36501eea7c5add258612"),
        512, &report);
    QVERIFY2(profile.approved, qPrintable(report.toText()));
    const STerrainAtlasAnalysis local = profile.approvedAnalysis(&report);
    QVERIFY2(local.usable, qPrintable(report.toText()));
    const CTerrainTransitionCatalog global =
        CTerrainAtlasPresetCatalog::globalizedCatalog(
            profile, local.landCatalog);
    qint32 grassId = 0;
    bool sawTransitionOnlyIrregular = false;
    for(int slot = 0; slot < 512; ++slot)
    {
        SMprPackedTile packed;
        packed.atlasIndex = quint8(slot / 64);
        packed.tileIndex = quint8(slot % 64);
        bool localFound = false;
        const STerrainTransitionSignature before =
            local.landCatalog.gridSignatureForPacked(packed.encode(), &localFound);
        if(!localFound) continue;
        const QVector<qint32> localCorners = {
            before.northWest, before.northEast,
            before.southEast, before.southWest};
        if(!localCorners.contains(9)) continue;
        sawTransitionOnlyIrregular = true;
        bool globalFound = false;
        const STerrainTransitionSignature after =
            global.gridSignatureForPacked(packed.encode(), &globalFound);
        QVERIFY(globalFound);
        const QVector<qint32> globalCorners = {
            after.northWest, after.northEast,
            after.southEast, after.southWest};
        QVERIFY(!globalCorners.contains(9));
        for(int corner = 0; corner < localCorners.size(); ++corner)
            if(localCorners.at(corner) == 9)
            {
                if(grassId == 0) grassId = globalCorners.at(corner);
                QCOMPARE(globalCorners.at(corner), grassId);
            }
        for(qint32 corner : globalCorners) QVERIFY(corner > 255);
    }
    QVERIFY(sawTransitionOnlyIrregular);
    QVERIFY(grassId > 255);
    const QVector<STerrainGlobalMaterialFamily> materials =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    const auto grassSource = std::find_if(materials.cbegin(), materials.cend(),
        [grassId](const STerrainGlobalMaterialFamily& material) {
            return material.id == grassId && !material.sources.isEmpty();
        });
    QVERIFY(grassSource != materials.cend());
    QVERIFY(global.hasMaterial(grassId));
}

void EnduranceDocumentTests::materialAtlasStoreLoadsOnlyExplicitLocalOverrides()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray hash =
        "35cc3b90985888b5c5206f8c5980321beb46f0be7b1e96b4a3467677ac2a4665";
    CValidationReport presetReport;
    STerrainAtlasProfile profile = CTerrainAtlasPresetCatalog::exactProfile(
        "zone1", hash, 512, &presetReport);
    QVERIFY2(profile.approved, qPrintable(presetReport.toText()));
    QVERIFY(CTerrainAtlasProfileStore::save(profile, root.path(), &presetReport));

    STerrainAtlasProfile loaded;
    QString sourcePath;
    CValidationReport defaultReport;
    QVERIFY(!CTerrainAtlasProfileStore::loadExplicitOverride(
        "zone1", hash, 512, &loaded, root.path(), &sourcePath,
        &defaultReport));
    QVERIFY(defaultReport.toText().contains(
        QStringLiteral("only an explicitly edited local profile")));

    profile.provenance = QStringLiteral("portable-exact-stock-atlas-clone-v1");
    CValidationReport portableSaveReport;
    QVERIFY2(CTerrainAtlasProfileStore::save(
                 profile, root.path(), &portableSaveReport),
             qPrintable(portableSaveReport.toText()));
    CValidationReport portableLoadReport;
    QVERIFY2(CTerrainAtlasProfileStore::loadExplicitOverride(
                 "zone1", hash, 512, &loaded, root.path(), &sourcePath,
                 &portableLoadReport), qPrintable(portableLoadReport.toText()));
    QCOMPARE(loaded.provenance,
             QStringLiteral("portable-exact-stock-atlas-clone-v1"));

    profile.provenance = QStringLiteral("portable-finalized-modern-atlas-v1");
    profile.mprNamespace = QStringLiteral("zone1_eime_finalized");
    QVERIFY2(CTerrainAtlasProfileStore::save(
                 profile, root.path(), &portableSaveReport),
             qPrintable(portableSaveReport.toText()));
    QVERIFY2(CTerrainAtlasProfileStore::loadExplicitOverride(
                 profile.mprNamespace, hash, 512, &loaded, root.path(),
                 &sourcePath, &portableLoadReport),
             qPrintable(portableLoadReport.toText()));
    QCOMPARE(loaded.assignedSlotCount(), profile.assignedSlotCount());

    profile.provenance = QStringLiteral("user-authored-custom-profile");
    profile.mprNamespace = QStringLiteral("zone1");
    profile.families.front().name = QStringLiteral("User ground name");
    CValidationReport saveReport;
    QVERIFY2(CTerrainAtlasProfileStore::save(profile, root.path(), &saveReport),
             qPrintable(saveReport.toText()));
    CValidationReport overrideReport;
    QVERIFY2(CTerrainAtlasProfileStore::loadExplicitOverride(
        "ZONE1", hash, 512, &loaded, root.path(), &sourcePath,
        &overrideReport), qPrintable(overrideReport.toText()));
    QCOMPARE(loaded.families.front().name, QStringLiteral("User ground name"));
    QCOMPARE(sourcePath, CTerrainAtlasProfileStore::profilePath(
        "ZONE1", root.path()));
    QVERIFY(CTerrainAtlasProfileStore::profilePath("zone1").startsWith(
        QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("terrain-atlas-profiles/"))));

    CValidationReport mismatchReport;
    QVERIFY(!CTerrainAtlasProfileStore::loadExplicitOverride(
        "zone1", QByteArray(64, '0'), 512, &loaded, root.path(),
        &sourcePath, &mismatchReport));
    QVERIFY(mismatchReport.hasErrors());

    CValidationReport removeReport;
    QVERIFY2(CTerrainAtlasProfileStore::removeLocalOverride(
        "zone1", root.path(), &removeReport), qPrintable(removeReport.toText()));
    QVERIFY(!QFileInfo::exists(CTerrainAtlasProfileStore::profilePath(
        "zone1", root.path())));
    QVERIFY(CTerrainAtlasProfileStore::removeLocalOverride(
        "zone1", root.path(), &removeReport));
}

void EnduranceDocumentTests::portableMprProfilesDoNotCollideForSharedResourceIdentity()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QFileInfo draftMpr(root.filePath(QStringLiteral("zone3obr test.mpr")));
    const QFileInfo publishedMpr(root.filePath(QStringLiteral("zone3obr.mpr")));

    const auto profile = [](char hashByte, const QString& provenance) {
        STerrainAtlasProfile value;
        value.mprNamespace = QStringLiteral("zone3obr");
        value.atlasPixelSha256 = QByteArray(64, hashByte);
        value.atlasSlotCount = 1;
        value.provenance = provenance;
        value.approved = true;
        STerrainAtlasProfileFamily family;
        family.id = 1;
        family.name = QStringLiteral("Ground / Common");
        family.thumbnailSlot = 0;
        family.paintSelectable = true;
        STerrainAtlasProfileSlot slot;
        slot.slot = 0;
        slot.assigned = true;
        slot.signature.northWest = slot.signature.northEast =
            slot.signature.southEast = slot.signature.southWest = 1;
        value.families.append(family);
        value.slotDefinitions.append(slot);
        value.normalizeMembership();
        return value;
    };
    const STerrainAtlasProfile draft = profile(
        'a', CTerrainAtlasProfileStore::workingAtlasProvenance());
    const STerrainAtlasProfile published = profile(
        'b', QStringLiteral("portable-finalized-modern-atlas-v1"));
    CValidationReport report;
    QVERIFY2(CTerrainAtlasProfileStore::saveForMpr(
                 draft, draftMpr, &report), qPrintable(report.toText()));
    QVERIFY2(CTerrainAtlasProfileStore::saveForMpr(
                 published, publishedMpr, &report),
             qPrintable(report.toText()));

    const QString draftPath = CTerrainAtlasProfileStore::profilePathForMpr(
        draft.mprNamespace, draftMpr);
    const QString publishedPath = CTerrainAtlasProfileStore::profilePathForMpr(
        published.mprNamespace, publishedMpr);
    QVERIFY(draftPath != publishedPath);
    QVERIFY(QFileInfo::exists(draftPath));
    QVERIFY(QFileInfo::exists(publishedPath));
    QVERIFY(!QFileInfo::exists(CTerrainAtlasProfileStore::profilePath(
        draft.mprNamespace, root.path())));

    STerrainAtlasProfile loaded;
    QString sourcePath;
    bool legacy = true;
    QVERIFY2(CTerrainAtlasProfileStore::loadExplicitOverrideForMpr(
                 draft.mprNamespace, draft.atlasPixelSha256,
                 draft.atlasSlotCount, draftMpr, &loaded, &sourcePath,
                 &legacy, &report), qPrintable(report.toText()));
    QCOMPARE(loaded.provenance,
             CTerrainAtlasProfileStore::workingAtlasProvenance());
    QCOMPARE(sourcePath, draftPath);
    QVERIFY(!legacy);
    QVERIFY2(CTerrainAtlasProfileStore::loadExplicitOverrideForMpr(
                 published.mprNamespace, published.atlasPixelSha256,
                 published.atlasSlotCount, publishedMpr, &loaded,
                 &sourcePath, &legacy, &report),
             qPrintable(report.toText()));
    QCOMPARE(loaded.provenance,
             QStringLiteral("portable-finalized-modern-atlas-v1"));
    QCOMPARE(sourcePath, publishedPath);
    QVERIFY(!legacy);

    // Old packages remain readable when no map-specific profile exists. The
    // caller can distinguish this namespace-only fallback and must not let a
    // mismatching legacy JSON block a complete sibling draft atlas.
    const QFileInfo legacyMpr(root.filePath(QStringLiteral("legacy copy.mpr")));
    QVERIFY2(CTerrainAtlasProfileStore::save(draft, root.path(), &report),
             qPrintable(report.toText()));
    QVERIFY2(CTerrainAtlasProfileStore::loadForMpr(
                 draft.mprNamespace, legacyMpr, &loaded, &sourcePath,
                 &legacy, &report), qPrintable(report.toText()));
    QVERIFY(legacy);
    QCOMPARE(sourcePath, CTerrainAtlasProfileStore::profilePath(
        draft.mprNamespace, root.path()));
}

void EnduranceDocumentTests::finalizedProfileBindsPostDxtPhysicalPagesAndPadding()
{
    STerrainAtlasProfile source;
    source.mprNamespace = QStringLiteral("source");
    for(int slot = 0; slot < 3; ++slot)
    {
        STerrainAtlasProfileSlot definition;
        definition.slot = slot;
        definition.signature.northWest = slot + 1;
        definition.signature.northEast = slot + 1;
        definition.signature.southEast = slot + 1;
        definition.signature.southWest = slot + 1;
        source.slotDefinitions.append(definition);
        STerrainAtlasProfileFamily family;
        family.id = slot + 1;
        family.name = QStringLiteral("Family %1").arg(slot + 1);
        family.thumbnailSlot = slot;
        source.families.append(family);
    }
    QVERIFY(source.normalizeMembership());

    STerrainAtlasFinalization finalized;
    finalized.success = true;
    finalized.isolatedNamespace = QStringLiteral("physical_profile_regression");
    finalized.textureCount = 1;
    QVector<QImage> active;
    for(int slot = 0; slot < 3; ++slot)
    {
        QImage pixels(8, 8, QImage::Format_RGBA8888);
        pixels.fill(QColor(30 + slot * 70, 90 + slot * 20, 160 - slot * 30));
        active.append(pixels);
        STerrainFinalizedTile tile;
        tile.sourceSlot = slot;
        tile.authored = true;
        tile.pixels = pixels;
        tile.pixelSha256 = QByteArray(64, char('a' + slot));
        tile.sourceNamespace = QStringLiteral("source");
        tile.sourceSlotAliases.append(slot);
        if(slot == 0)
            tile.sourceSlotAliases.append(17);
        tile.canonicalSignature = source.slotDefinitions.at(slot).signature;
        finalized.tiles.append(tile);
    }
    QImage foreignPixels(8, 8, QImage::Format_RGBA8888);
    foreignPixels.fill(QColor(210, 80, 35));
    active.append(foreignPixels);
    STerrainFinalizedTile foreignTile;
    foreignTile.sourceSlot = 1; // Deliberately collides with a local slot.
    foreignTile.authored = true;
    foreignTile.pixels = foreignPixels;
    foreignTile.pixelSha256 = QByteArray(64, 'f');
    foreignTile.sourceNamespace = QStringLiteral("foreign-zone");
    foreignTile.sourceSlotAliases.append(1);
    foreignTile.canonicalSignature.northWest = 1;
    foreignTile.canonicalSignature.northEast = 2;
    foreignTile.canonicalSignature.southEast = 2;
    foreignTile.canonicalSignature.southWest = 1;
    foreignTile.routeOwnerMaterial = 2;
    finalized.tiles.append(foreignTile);
    QString diagnostic;
    finalized.pageMmpPayloads.append(ei::encodeMmpDxt1(
        CTerrainVirtualAtlas::composePage(active, 0, 8), true, &diagnostic));
    QVERIFY2(!finalized.pageMmpPayloads.front().isEmpty(), qPrintable(diagnostic));

    QVector<QImage> physical;
    CValidationReport report;
    QVERIFY2(CTerrainVirtualAtlas::decodePhysicalTileImages(
                 finalized, &physical, &report), qPrintable(report.toText()));
    QCOMPARE(physical.size(), 64);
    QMap<qint32, qint32> runtimeTypes;
    runtimeTypes.insert(1, 4);
    runtimeTypes.insert(2, 7);
    runtimeTypes.insert(3, 9);
    const STerrainAtlasProfile portable =
        CTerrainAtlasProfileStore::portableFinalizedProfile(
            finalized, physical, source, ETerrainAtlasLayer::Land, {},
            runtimeTypes, &report);
    QCOMPARE(portable.atlasSlotCount, 64);
    QCOMPARE(portable.slotDefinitions.size(), 64);
    QCOMPARE(portable.assignedSlotCount(), 4);
    QCOMPARE(portable.slotDefinitions.at(3).signature,
             CTerrainTransitionCatalog::mirroredGridY(
                 foreignTile.canonicalSignature));
    QCOMPARE(portable.slotDefinitions.at(3).sourceNamespace,
             QStringLiteral("foreign-zone"));
    QCOMPARE(portable.slotDefinitions.at(3).routeOwnerMaterial, qint32(2));
    QMap<qint32, qint32> portableTypes;
    for(const STerrainAtlasProfileFamily& family : portable.families)
        portableTypes.insert(family.id, family.tileType);
    QCOMPARE(portableTypes.value(1), 4);
    QCOMPARE(portableTypes.value(2), 7);
    QCOMPARE(portableTypes.value(3), 9);
    QVERIFY(portable.validateBinding(
        finalized.isolatedNamespace,
        CTerrainAtlasProfileInference::pixelSha256(physical), 64, &report));

    QVector<QImage> logical;
    for(const STerrainFinalizedTile& tile : finalized.tiles)
        logical.append(tile.pixels);
    CValidationReport oldBindingReport;
    QVERIFY(!portable.validateBinding(
        finalized.isolatedNamespace,
        CTerrainAtlasProfileInference::pixelSha256(logical), logical.size(),
        &oldBindingReport));
    QVERIFY(oldBindingReport.hasErrors());

    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY2(CTerrainAtlasProfileStore::save(portable, root.path(), &report),
             qPrintable(report.toText()));
    STerrainAtlasProfile reopened;
    QVERIFY2(CTerrainAtlasProfileStore::loadExplicitOverride(
                 finalized.isolatedNamespace, portable.atlasPixelSha256, 64,
                 &reopened, root.path(), nullptr, &report),
             qPrintable(report.toText()));
    QCOMPARE(reopened.assignedSlotCount(), 4);
    QCOMPARE(reopened.schemaVersion, 10);
    QCOMPARE(reopened.slotDefinitions.at(0).sourceNamespace,
             QStringLiteral("source"));
    QCOMPARE(reopened.slotDefinitions.at(0).sourceSlotAliases,
             QVector<int>({0, 17}));
    QCOMPARE(reopened.slotDefinitions.at(0).sourcePixelSha256,
             QByteArray(64, 'a'));
    QCOMPARE(reopened.slotDefinitions.at(3).routeOwnerMaterial, qint32(2));
    const STerrainAtlasAnalysis reopenedAnalysis =
        reopened.usableAnalysis(&report);
    QVERIFY2(reopenedAnalysis.usable, qPrintable(report.toText()));
    bool reopenedRouteFound = false;
    SMprPackedTile reopenedRoutePacked;
    reopenedRoutePacked.tileIndex = quint8(
        reopened.slotDefinitions.at(3).slot % 64);
    reopenedRoutePacked.atlasIndex = quint8(
        reopened.slotDefinitions.at(3).slot / 64);
    const STerrainTransitionVariant reopenedRoute =
        reopenedAnalysis.landCatalog.variantForPacked(
            reopenedRoutePacked.encode(),
            &reopenedRouteFound);
    QVERIFY(reopenedRouteFound);
    QCOMPARE(reopenedRoute.routeOwnerMaterial, qint32(2));

    STerrainAtlasFinalization incomplete = finalized;
    incomplete.tiles[3].canonicalSignature = STerrainTransitionSignature();
    CValidationReport incompleteReport;
    const STerrainAtlasProfile refused =
        CTerrainAtlasProfileStore::portableFinalizedProfile(
            incomplete, physical, source, ETerrainAtlasLayer::Land, {},
            {}, &incompleteReport);
    QCOMPARE(refused.assignedSlotCount(), 3);
    QVERIFY(incompleteReport.hasErrors());
    QVERIFY(incompleteReport.toText().contains(
        QStringLiteral("logical slot 3")));
}

void EnduranceDocumentTests::workingAtlasProfileBindsExtensionSignaturesForDraftReopen()
{
    const qint32 grass = CTerrainAtlasPresetCatalog::globalMaterialId(
        QStringLiteral("Grass / Common"), ETerrainAtlasLayer::Land);
    const qint32 stone = CTerrainAtlasPresetCatalog::globalMaterialId(
        QStringLiteral("Stone / Gray"), ETerrainAtlasLayer::Land);
    CTerrainTransitionCatalog catalog;
    catalog.setMirrorPaintGridY(true);
    STerrainTransitionSignature grid;
    grid.northWest = grass;
    grid.northEast = stone;
    grid.southEast = grass;
    grid.southWest = grass;
    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(grid);
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        SMprPackedTile packed;
        packed.tileIndex = 0;
        packed.atlasIndex = 1;
        packed.rotation = quint8(rotation);
        STerrainTransitionVariant variant;
        // This synthetic catalog uses the same explicit mirror boundary as
        // the live working catalog. gridSignatureForPacked() must therefore
        // recover `grid` regardless of the catalog's internal corner space.
        variant.signature = CTerrainTransitionCatalog::rotated(
            preview, -rotation);
        variant.packedTile = packed.encode();
        variant.routeOwnerMaterial = stone;
        QVERIFY(catalog.addVariant(variant));
    }
    QVector<QImage> tiles(128, QImage(8, 8, QImage::Format_RGBA8888));
    for(int slot = 0; slot < tiles.size(); ++slot)
        tiles[slot].fill(QColor(slot & 255, (slot * 3) & 255,
                                (slot * 7) & 255, 255));
    STerrainGlobalMaterialFamily grassFamily;
    grassFamily.id = grass;
    grassFamily.name = QStringLiteral("Grass / Common");
    grassFamily.layer = ETerrainAtlasLayer::Land;
    grassFamily.runtimeTerrainType = 0;
    STerrainGlobalMaterialFamily stoneFamily;
    stoneFamily.id = stone;
    stoneFamily.name = QStringLiteral("Stone / Gray");
    stoneFamily.layer = ETerrainAtlasLayer::Land;
    stoneFamily.runtimeTerrainType = 2;
    CValidationReport report;
    const STerrainAtlasProfile profile =
        CTerrainAtlasProfileStore::workingAtlasProfile(
            QStringLiteral("zone_draft"), tiles, catalog,
            STerrainAtlasProfile(),
            QVector<STerrainGlobalMaterialFamily>()
                << grassFamily << stoneFamily, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(profile.atlasSlotCount, 128);
    QCOMPARE(profile.assignedSlotCount(), 1);
    QCOMPARE(profile.slotDefinitions.at(64).signature, preview);
    QCOMPARE(profile.slotDefinitions.at(64).routeOwnerMaterial, stone);
    QCOMPARE(profile.provenance,
             CTerrainAtlasProfileStore::workingAtlasProvenance());
    QVERIFY(profile.validateBinding(
        QStringLiteral("zone_draft"),
        CTerrainAtlasProfileInference::pixelSha256(tiles), 128, &report));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(CTerrainAtlasProfileStore::save(
        profile, directory.path(), &report));
    STerrainAtlasProfile reopenedProfile;
    QVERIFY(CTerrainAtlasProfileStore::loadExplicitOverride(
        QStringLiteral("zone_draft"),
        CTerrainAtlasProfileInference::pixelSha256(tiles), 128,
        &reopenedProfile, directory.path(), nullptr, &report));
    QCOMPARE(reopenedProfile.provenance,
             CTerrainAtlasProfileStore::workingAtlasProvenance());
    QCOMPARE(reopenedProfile.slotDefinitions.at(64).routeOwnerMaterial,
             stone);
    const STerrainAtlasAnalysis analysis = profile.usableAnalysis(&report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    bool found = false;
    SMprPackedTile packed;
    packed.tileIndex = 0;
    packed.atlasIndex = 1;
    packed.rotation = 0;
    QCOMPARE(analysis.landCatalog.gridSignatureForPacked(
                 packed.encode(), &found), grid);
    QVERIFY(found);
    QCOMPARE(analysis.landCatalog.variantForPacked(
                 packed.encode(), &found).routeOwnerMaterial, stone);
    QVERIFY(found);

    // The real regression needs a second boundary: Save a draft from a map
    // whose portable profile has already been reopened. That compiled catalog
    // stores paint-grid tuples directly (mirror=false). The next draft profile
    // must still serialize preview-image corners and reopen to the same grid.
    const STerrainAtlasProfile redraft =
        CTerrainAtlasProfileStore::workingAtlasProfile(
            QStringLiteral("zone_redraft"), tiles,
            analysis.overlayCompatibleCatalog, reopenedProfile,
            QVector<STerrainGlobalMaterialFamily>()
                << grassFamily << stoneFamily, &report);
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QCOMPARE(redraft.slotDefinitions.at(64).signature, preview);
    const STerrainAtlasAnalysis redraftAnalysis = redraft.usableAnalysis(&report);
    QVERIFY2(redraftAnalysis.usable, qPrintable(report.toText()));
    QCOMPARE(redraftAnalysis.landCatalog.gridSignatureForPacked(
                 packed.encode(), &found), grid);
    QVERIFY(found);
}

void EnduranceDocumentTests::embeddedTerrainAtlasProfileRoundTripsInsidePublishedRes()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVector<QImage> tiles;
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage tile(16, 16, QImage::Format_RGBA8888);
        tile.fill(QColor((slot * 17) & 255, (slot * 31) & 255,
                         (slot * 47) & 255, 255));
        tiles.append(tile);
    }
    QString diagnostic;
    const QByteArray page = ei::encodeMmpDxt1(
        CTerrainVirtualAtlas::composePage(tiles, 0, 16), true, &diagnostic);
    QVERIFY2(!page.isEmpty(), qPrintable(diagnostic));

    // Bind the profile to the decoded, compressed artwork that the game will
    // actually read rather than to the pre-DXT source pixels.
    QVector<QImage> decoded;
    for(int slot = 0; slot < 64; ++slot)
    {
        QImage tile;
        QVERIFY2(ei::decodeMmpDxt1AtlasTile(page, slot, &tile, &diagnostic),
                 qPrintable(diagnostic));
        decoded.append(tile.convertToFormat(QImage::Format_RGBA8888));
    }
    STerrainAtlasProfile profile;
    profile.mprNamespace = QStringLiteral("zone embedded");
    profile.atlasPixelSha256 =
        CTerrainAtlasProfileInference::pixelSha256(decoded);
    profile.atlasSlotCount = 64;
    profile.textureCount = 1;
    profile.provenance = QStringLiteral("portable-finalized-modern-atlas-v1");
    profile.approved = true;
    STerrainAtlasProfileFamily family;
    family.id = 1;
    family.name = QStringLiteral("Ground / Common");
    family.thumbnailSlot = 0;
    family.tileType = 1;
    profile.families.append(family);
    for(int slot = 0; slot < 64; ++slot)
    {
        STerrainAtlasProfileSlot definition;
        definition.slot = slot;
        definition.assigned = true;
        definition.signature.northWest = 1;
        definition.signature.northEast = 1;
        definition.signature.southEast = 1;
        definition.signature.southWest = 1;
        profile.slotDefinitions.append(definition);
    }
    CValidationReport report;
    QVERIFY2(profile.normalizeMembership(&report), qPrintable(report.toText()));

    CResArchiveDocument empty;
    QStringList errors;
    QVERIFY(empty.loadBytes(emptyMpr(), QStringLiteral("empty.res"), &errors));
    SResArchiveNewMember unrelated;
    unrelated.name = QStringLiteral("unrelated.bin");
    unrelated.payload = QByteArray("preserve-me");
    unrelated.lastWriteTime = 17;
    QByteArray baselineBytes;
    QVERIFY2(empty.buildWithAppendedMembers({unrelated}, &baselineBytes, &errors),
             qPrintable(errors.join(QStringLiteral("\n"))));
    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(baselineBytes, QStringLiteral("baseline.res"),
                               &errors));
    QVector<SResArchiveNewMember> published;
    SResArchiveNewMember pageMember;
    pageMember.name = QStringLiteral("zone embedded000.mmp");
    pageMember.payload = page;
    pageMember.lastWriteTime = 23;
    published.append(pageMember);
    SResArchiveNewMember profileMember;
    profileMember.name = CTerrainAtlasProfileStore::embeddedMemberName(
        profile.mprNamespace);
    profileMember.payload = profile.toBytes();
    profileMember.lastWriteTime = 23;
    published.append(profileMember);
    QByteArray resultBytes;
    QVERIFY2(baseline.buildWithPublishedMembers(
                 published, false, &resultBytes, &errors),
             qPrintable(errors.join(QStringLiteral("\n"))));
    const QString archivePath = root.filePath(QStringLiteral("textures.res"));
    QFile archiveFile(archivePath);
    QVERIFY(archiveFile.open(QIODevice::WriteOnly));
    QCOMPARE(archiveFile.write(resultBytes), qint64(resultBytes.size()));
    archiveFile.close();

    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {archivePath}, profile.mprNamespace);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    QCOMPARE(binding.profile.mprNamespace, profile.mprNamespace);
    QCOMPARE(binding.profile.textureCount, 1);
    QCOMPARE(binding.profile.atlasPixelSha256, profile.atlasPixelSha256);
    QCOMPARE(binding.profile.slotDefinitions.size(), 64);
    QCOMPARE(binding.decodedAtlasTiles.size(), 64);
    for(int slot = 0; slot < decoded.size(); ++slot)
        QCOMPARE(binding.decodedAtlasTiles.at(slot), decoded.at(slot));
    QVERIFY(binding.canReuseDecodedAtlasTiles(
        archivePath, profile.mprNamespace, 64));
    CResArchiveDocument reopened;
    QVERIFY(reopened.loadBytes(resultBytes, archivePath, &errors));
    QCOMPARE(reopened.memberPayload(QStringLiteral("unrelated.bin")),
             QByteArray("preserve-me"));

    STerrainAtlasRecoverySnapshot recovery;
    recovery.resourceIdentity = profile.mprNamespace;
    recovery.atlasPixelSha256 = profile.atlasPixelSha256;
    recovery.profileBytes = profile.toBytes();
    recovery.mprPath = root.filePath(QStringLiteral("zone embedded.mpr"));
    recovery.resPath = archivePath;
    recovery.mprSha256 = QByteArray(64, 'a');
    recovery.runtimeTerrainTypes.fill(1, 64);
    STerrainAtlasRecoveryPage recoveryPage;
    recoveryPage.name = pageMember.name;
    recoveryPage.payload = pageMember.payload;
    recovery.pages.append(recoveryPage);
    QByteArray recoveryArchiveBytes;
    QString recoveryError;
    QVERIFY2(CTerrainAtlasRecoveryStore(root.filePath(
                 QStringLiteral("recovery"))).buildStandaloneArchive(
                 recovery, &recoveryArchiveBytes, &recoveryError),
             qPrintable(recoveryError));
    const QString recoveryArchivePath = root.filePath(
        QStringLiteral("zone embedded-atlas.res"));
    QFile recoveryArchive(recoveryArchivePath);
    QVERIFY(recoveryArchive.open(QIODevice::WriteOnly));
    QCOMPARE(recoveryArchive.write(recoveryArchiveBytes),
             qint64(recoveryArchiveBytes.size()));
    recoveryArchive.close();
    const STerrainAtlasEmbeddedBindingResult recoveredBinding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {recoveryArchivePath}, profile.mprNamespace);
    QVERIFY2(recoveredBinding.success(), qPrintable(recoveredBinding.error));
    QCOMPARE(recoveredBinding.profile.atlasPixelSha256,
             profile.atlasPixelSha256);
}

void EnduranceDocumentTests::prevalidatedEmbeddedAtlasBindingMatchesOnlyExactLoadedAtlas()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString archivePath = root.filePath(QStringLiteral("textures.res"));
    QFile archive(archivePath);
    QVERIFY(archive.open(QIODevice::WriteOnly));
    archive.close();

    STerrainAtlasEmbeddedBindingResult binding;
    binding.archivePath = archivePath;
    binding.profileMemberName = QStringLiteral("ei_terrain_atlas_zone2.json");
    binding.decodedAtlasPixelSha256 = QByteArray(64, 'a');
    binding.profile.mprNamespace = QStringLiteral("zone2");
    binding.profile.atlasPixelSha256 = binding.decodedAtlasPixelSha256;
    binding.profile.atlasSlotCount = 576;
    binding.decodedAtlasTiles.fill(
        QImage(4, 4, QImage::Format_RGBA8888), 576);
    binding.archiveSize = QFileInfo(archivePath).size();
    binding.archiveLastModifiedMs = QFileInfo(archivePath)
        .lastModified().toMSecsSinceEpoch();

    QVERIFY(binding.matchesLoadedAtlas(
        archivePath, QStringLiteral("Zone2"),
        binding.decodedAtlasPixelSha256, 576));
    QVERIFY(binding.canReuseDecodedAtlasTiles(
        archivePath, QStringLiteral("Zone2"), 576));
    QVERIFY(!binding.matchesLoadedAtlas(
        root.filePath(QStringLiteral("other.res")),
        QStringLiteral("zone2"), binding.decodedAtlasPixelSha256, 576));
    QVERIFY(!binding.matchesLoadedAtlas(
        archivePath, QStringLiteral("zone1"),
        binding.decodedAtlasPixelSha256, 576));
    QVERIFY(!binding.matchesLoadedAtlas(
        archivePath, QStringLiteral("zone2"),
        binding.decodedAtlasPixelSha256, 512));

    binding.decodedAtlasPixelSha256 = QByteArray(64, 'b');
    QVERIFY(!binding.matchesLoadedAtlas(
        archivePath, QStringLiteral("zone2"),
        binding.profile.atlasPixelSha256, 576));

    binding.decodedAtlasPixelSha256 = binding.profile.atlasPixelSha256;
    QFile changedArchive(archivePath);
    QVERIFY(changedArchive.open(QIODevice::Append));
    QCOMPARE(changedArchive.write("changed"), qint64(7));
    changedArchive.close();
    QVERIFY(!binding.canReuseDecodedAtlasTiles(
        archivePath, QStringLiteral("zone2"), 576));
}

void EnduranceDocumentTests::indexedTerrainArchiveMemberReadSkipsUnrelatedPayloads()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    CResArchiveDocument empty;
    QStringList errors;
    QVERIFY(empty.loadBytes(emptyMpr(), QStringLiteral("empty.res"), &errors));

    const QByteArray semanticPayload("{\"semantic\":\"exact\"}");
    const QByteArray profilePayload("{\"profile\":\"exact\"}");
    SResArchiveNewMember decoy;
    decoy.name = QStringLiteral("large-unrelated-atlas-page.mmp");
    decoy.payload = QByteArray(8 * 1024 * 1024, char(0x5a));
    SResArchiveNewMember semantics;
    semantics.name = QStringLiteral("ei_terrain_semantics_zone2.json");
    semantics.payload = semanticPayload;
    SResArchiveNewMember profile;
    profile.name = QStringLiteral("ei_terrain_atlas_zone2.json");
    profile.payload = profilePayload;

    QByteArray archiveBytes;
    QVERIFY2(empty.buildWithAppendedMembers(
                 {decoy, semantics, profile}, &archiveBytes, &errors),
             qPrintable(errors.join(QStringLiteral("\n"))));
    const QString archivePath = root.filePath(QStringLiteral("terrain.res"));
    QFile archiveFile(archivePath);
    QVERIFY(archiveFile.open(QIODevice::WriteOnly));
    QCOMPARE(archiveFile.write(archiveBytes), qint64(archiveBytes.size()));
    archiveFile.close();

    QMap<QString, QByteArray> payloads;
    qint64 bytesRead = 0;
    QString error;
    QVERIFY2(CTerrainAtlasArchiveBindingResolver::readArchiveMembers(
                 archivePath,
                 {semantics.name, profile.name},
                 &payloads, &bytesRead, &error),
             qPrintable(error));
    QCOMPARE(payloads.value(semantics.name), semanticPayload);
    QCOMPARE(payloads.value(profile.name), profilePayload);
    QVERIFY(!payloads.contains(decoy.name));
    QVERIFY(bytesRead > 0);
    QVERIFY2(bytesRead < archiveBytes.size() / 100,
             qPrintable(QStringLiteral(
                 "Indexed read consumed %1 of %2 archive bytes")
                 .arg(bytesRead).arg(archiveBytes.size())));
}

void EnduranceDocumentTests::terrainAtlasRecoveryStoreDeduplicatesExactPayloads()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    STerrainAtlasRecoverySnapshot snapshot;
    snapshot.resourceIdentity = QStringLiteral("zone2_deadbeef");
    snapshot.atlasPixelSha256 = QByteArray(64, 'a');
    snapshot.profileBytes = QByteArray("{\"exact\":true}");
    snapshot.mprPath = QStringLiteral("C:/Maps/zone2.mpr");
    snapshot.resPath = QStringLiteral("C:/Res/textures.res");
    snapshot.mprSha256 = QByteArray(64, 'b');
    snapshot.runtimeTerrainTypes = {1, 2, 2, 7};
    STerrainAtlasRecoveryPage pageZero;
    pageZero.name = QStringLiteral("zone2_deadbeef000.mmp");
    pageZero.payload = QByteArray("exact-page-zero");
    snapshot.pages.append(pageZero);
    STerrainAtlasRecoveryPage pageOne;
    pageOne.name = QStringLiteral("zone2_deadbeef001.mmp");
    pageOne.payload = QByteArray("exact-page-one");
    snapshot.pages.append(pageOne);
    STerrainAtlasRecoveryPage semanticMember;
    semanticMember.name = QStringLiteral(
        "ei_terrain_semantics_zone2_deadbeef.json");
    semanticMember.payload = QByteArray("{\"field\":\"exact\"}");
    snapshot.metadataMembers.append(semanticMember);

    CTerrainAtlasRecoveryStore store(root.path());
    const STerrainAtlasRecoveryResult first = store.saveSnapshot(snapshot);
    QVERIFY2(first.success, qPrintable(first.error));
    QVERIFY(QFileInfo::exists(first.manifestPath));
    const STerrainAtlasRecoveryResult second = store.saveSnapshot(snapshot);
    QVERIFY2(second.success, qPrintable(second.error));
    QVERIFY(second.skipped);
    QCOMPARE(second.manifestPath, first.manifestPath);

    const QDir objectDirectory(store.objectDirectory());
    // One profile, two pages, and one editor semantic-field payload. The
    // second snapshot must reuse all four content-addressed objects.
    QCOMPARE(objectDirectory.entryList(QDir::Files).size(), 4);
    STerrainAtlasRecoverySnapshot recovered;
    QString error;
    QVERIFY2(store.findExact(snapshot.resourceIdentity,
                             snapshot.atlasPixelSha256,
                             &recovered, &error), qPrintable(error));
    QCOMPARE(recovered.profileBytes, snapshot.profileBytes);
    QCOMPARE(recovered.runtimeTerrainTypes, snapshot.runtimeTerrainTypes);
    QCOMPARE(recovered.pages.size(), snapshot.pages.size());
    for(int index = 0; index < snapshot.pages.size(); ++index)
    {
        QCOMPARE(recovered.pages.at(index).name, snapshot.pages.at(index).name);
        QCOMPARE(recovered.pages.at(index).payload,
                 snapshot.pages.at(index).payload);
    }
    QCOMPARE(recovered.metadataMembers.size(), 1);
    QCOMPARE(recovered.metadataMembers.front().name, semanticMember.name);
    QCOMPARE(recovered.metadataMembers.front().payload,
             semanticMember.payload);
    STerrainAtlasRecoverySnapshot recoveredFromMpr;
    QVERIFY2(store.findForMpr(snapshot.resourceIdentity,
                              snapshot.mprSha256,
                              &recoveredFromMpr, &error),
             qPrintable(error));
    QCOMPARE(recoveredFromMpr.atlasPixelSha256,
             snapshot.atlasPixelSha256);
    QCOMPARE(recoveredFromMpr.profileBytes, snapshot.profileBytes);
    QCOMPARE(recoveredFromMpr.pages.size(), snapshot.pages.size());
}

void EnduranceDocumentTests::publishedAtlasArchiveBindingSelectsCoherentProfileSource()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVector<QImage> wrongTiles;
    QVector<QImage> expectedTiles;
    for(int slot = 0; slot < 128; ++slot)
    {
        QImage wrong(16, 16, QImage::Format_RGBA8888);
        wrong.fill(Qt::white);
        wrongTiles.append(wrong);
        QImage expected(16, 16, QImage::Format_RGBA8888);
        expected.fill(Qt::black);
        expectedTiles.append(expected);
    }
    const auto archiveBytes = [](const QVector<QImage>& tiles) {
        QByteArray empty = emptyMpr();
        CResArchiveDocument archive;
        QStringList errors;
        if(!archive.loadBytes(empty, QStringLiteral("empty.res"), &errors))
            return QByteArray();
        QVector<SResArchiveNewMember> pages;
        for(int page = 0; page < 2; ++page)
        {
            QString diagnostic;
            SResArchiveNewMember member;
            member.name = QStringLiteral("zone4%1.mmp")
                .arg(page, 3, 10, QLatin1Char('0'));
            member.payload = ei::encodeMmpDxt1(
                CTerrainVirtualAtlas::composePage(tiles, page * 64, 16),
                true, &diagnostic);
            if(member.payload.isEmpty())
                return QByteArray();
            pages.append(member);
        }
        QByteArray result;
        if(!archive.buildWithAppendedMembers(pages, &result, &errors))
            return QByteArray();
        return result;
    };
    const QString wrongPath = root.filePath(
        QStringLiteral("HDPack_Terrain - Copy.res"));
    const QString expectedPath = root.filePath(
        QStringLiteral("HDPack_Terrain.res"));
    for(const QPair<QString, QByteArray>& file : {
         qMakePair(wrongPath, archiveBytes(wrongTiles)),
         qMakePair(expectedPath, archiveBytes(expectedTiles))})
    {
        QVERIFY(!file.second.isEmpty());
        QFile output(file.first);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(file.second), qint64(file.second.size()));
    }
    const STerrainAtlasArchiveBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findExactArchive(
            {wrongPath, expectedPath}, QStringLiteral("zone4"), 2, 128,
            CTerrainAtlasProfileInference::pixelSha256(expectedTiles),
            wrongPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    QCOMPARE(QFileInfo(binding.archivePath).canonicalFilePath(),
             QFileInfo(expectedPath).canonicalFilePath());
    QCOMPARE(binding.decodedAtlasPixelSha256,
             CTerrainAtlasProfileInference::pixelSha256(expectedTiles));
}

void EnduranceDocumentTests::installedHdAtlasAllowlistProbe()
{
    const QString archive = qEnvironmentVariable("EI_TEST_HD_ATLAS_ARCHIVE");
    if(archive.isEmpty()) QSKIP("Set EI_TEST_HD_ATLAS_ARCHIVE for read-only HD catalog coverage");
    QFile resource(":/terrain_atlas_presets.json");
    QVERIFY(resource.open(QIODevice::ReadOnly));
    const auto presets = QJsonDocument::fromJson(resource.readAll()).object().value("presets").toArray();
    QJsonArray measured;
    QMap<QString, QByteArray> actualByNamespace;
    QStringList missing;
    for(const auto& value : presets)
    {
        const auto preset = value.toObject();
        const QString name = preset.value("namespace").toString();
        const int slotCount = preset.value("slotCount").toInt();
        // A deliberately unknown expected hash leaves the native decoder's
        // measured complete-atlas identities available without weakening binding.
        const auto probe = CTerrainAtlasArchiveBindingResolver::findExactArchive(
            {archive}, name, (slotCount + 63) / 64, slotCount, QByteArray(64, '0'));
        QVERIFY2(!probe.completeArchivePixelSha256.isEmpty(), qPrintable(name + ": " + probe.error));
        const QByteArray actual = probe.completeArchivePixelSha256.first();
        actualByNamespace.insert(name.toCaseFolded(), actual);
        QJsonObject row;
        row.insert("namespace", name); row.insert("slotCount", slotCount);
        row.insert("pixelSha256", QString::fromLatin1(actual));
        measured.append(row);
        if(CTerrainAtlasPresetCatalog::exactProfile(name, actual, slotCount).slotDefinitions.isEmpty())
            missing.append(name);
    }
    const QString outputPath = qEnvironmentVariable("EI_TEST_HD_ATLAS_REPORT");
    if(!outputPath.isEmpty())
    {
        QFile output(outputPath);
        QVERIFY(output.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(measured).toJson();
        QCOMPARE(output.write(bytes), qint64(bytes.size()));
    }
    qInfo() << "Native decoded HD atlas namespaces:" << measured.size() << "not approved:" << missing.size();
    QVERIFY2(missing.isEmpty(), qPrintable("HD atlases missing exact approval: " + missing.join(", ")));
    CValidationReport report;
    const auto families = CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    int available = 0;
    for(const auto& family : families)
    {
        bool found = false;
        for(const auto& source : family.sources)
            found |= actualByNamespace.value(source.mprNamespace.toCaseFolded()) == source.atlasPixelSha256;
        QVERIFY2(found, qPrintable("No exact HD uniform source for " + family.name));
        ++available;
    }
    QSet<QString> transitionNamespaces;
    for(const auto& source : CTerrainAtlasPresetCatalog::globalAuthoredTransitions(&report))
        if(actualByNamespace.value(source.mprNamespace.toCaseFolded()) == source.atlasPixelSha256)
            transitionNamespaces.insert(source.mprNamespace.toCaseFolded());
    QCOMPARE(transitionNamespaces.size(), measured.size());
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    qInfo() << "All global material families have exact HD sources:" << available;
    const QString mapDirectory = qEnvironmentVariable("EI_TEST_HD_MAP_DIRECTORY");
    if(!mapDirectory.isEmpty())
    {
        const auto maps = QDir(mapDirectory).entryInfoList({"*.mpr"}, QDir::Files);
        QVERIFY(!maps.isEmpty());
        for(const auto& file : maps)
        {
            CMprDocument map;
            CValidationReport mapReport;
            QVERIFY2(map.load(file, &mapReport), qPrintable(mapReport.toText()));
            const QString name = map.mapInfo().innerNamespace;
            const auto profile = CTerrainAtlasPresetCatalog::exactProfile(
                name, actualByNamespace.value(name.toCaseFolded()),
                int(map.mapInfo().textureCount) * 64);
            QVERIFY2(!profile.slotDefinitions.isEmpty(), qPrintable("Uncovered map atlas: " + file.fileName() + " -> " + name));
        }
        qInfo() << "Installed MPR maps with exact approved HD atlas:" << maps.size();
    }
}

void EnduranceDocumentTests::externalPublishedAtlasArchiveBindingProbe()
{
    const QString mprPath =
        qEnvironmentVariable("EI_MAPPER_REAL_PUBLISHED_MPR");
    const QString profilePath =
        qEnvironmentVariable("EI_MAPPER_REAL_PUBLISHED_PROFILE");
    const QStringList archives = qEnvironmentVariable(
        "EI_MAPPER_REAL_RESOURCE_ARCHIVES").split(QDir::listSeparator(),
                                                   QString::SkipEmptyParts);
    if(mprPath.isEmpty() || profilePath.isEmpty() || archives.isEmpty())
        QSKIP("Set EI_MAPPER_REAL_PUBLISHED_MPR, EI_MAPPER_REAL_PUBLISHED_PROFILE, and EI_MAPPER_REAL_RESOURCE_ARCHIVES for the read-only binding probe");
    CValidationReport report;
    CMprDocument document;
    STerrainAtlasProfile profile;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QVERIFY2(STerrainAtlasProfile::load(profilePath, &profile, &report),
             qPrintable(report.toText()));
    const STerrainAtlasArchiveBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findExactArchive(
            archives, document.mapInfo().innerNamespace,
            int(document.mapInfo().textureCount), profile.atlasSlotCount,
            profile.atlasPixelSha256);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    QCOMPARE(binding.decodedAtlasPixelSha256, profile.atlasPixelSha256);
    const QString expected =
        qEnvironmentVariable("EI_MAPPER_REAL_PUBLISHED_ARCHIVE");
    if(!expected.isEmpty())
        QCOMPARE(QFileInfo(binding.archivePath).canonicalFilePath(),
                 QFileInfo(expected).canonicalFilePath());
}

void EnduranceDocumentTests::externalModernDraftAdoptionPairProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_REAL_DRAFT_ADOPTION_MPR");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_REAL_DRAFT_ADOPTION_ATLAS");
    if(mprPath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set EI_MAPPER_REAL_DRAFT_ADOPTION_MPR and EI_MAPPER_REAL_DRAFT_ADOPTION_ATLAS for the read-only adoption-pair probe");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    const QByteArray mprSha256 = QCryptographicHash::hash(
        document.originalBytes(), QCryptographicHash::Sha256).toHex();
    const QByteArray expectedMprSha256 = qEnvironmentVariable(
        "EI_MAPPER_REAL_DRAFT_ADOPTION_MPR_SHA256").toLatin1().toLower();
    if(!expectedMprSha256.isEmpty())
        QCOMPARE(mprSha256, expectedMprSha256);

    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {atlasPath}, document.mapInfo().innerNamespace, atlasPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    const int physicalSlots = int(document.mapInfo().textureCount) * 64;
    QCOMPARE(binding.decodedAtlasTiles.size(), physicalSlots);
    QVERIFY2(binding.canReuseDecodedAtlasTiles(
                 atlasPath, document.mapInfo().innerNamespace, physicalSlots,
                 &report), qPrintable(report.toText()));
    QVERIFY2(binding.profile.validateBinding(
                 document.mapInfo().innerNamespace,
                 binding.decodedAtlasPixelSha256, physicalSlots, &report),
             qPrintable(report.toText()));
    const QByteArray expectedAtlasSha256 = qEnvironmentVariable(
        "EI_MAPPER_REAL_DRAFT_ADOPTION_ATLAS_PIXEL_SHA256")
        .toLatin1().toLower();
    if(!expectedAtlasSha256.isEmpty())
        QCOMPARE(binding.decodedAtlasPixelSha256.toLower(),
                 expectedAtlasSha256);
    for(int slot = 0; slot < binding.decodedAtlasTiles.size(); ++slot)
    {
        const QImage& tile = binding.decodedAtlasTiles.at(slot);
        QVERIFY2(!tile.isNull(), qPrintable(QStringLiteral(
            "physical atlas slot %1 (page %2 local %3) is null")
            .arg(slot).arg(slot / 64).arg(slot % 64)));
        QCOMPARE(tile.size(), binding.decodedAtlasTiles.front().size());
    }

    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QSet<int> referencedSlots;
    QSet<int> landReferencedSlots;
    QMap<int, QString> firstLandCell;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
            for(ETerrainTileLayer layer : {ETerrainTileLayer::Land,
                                           ETerrainTileLayer::Overlay})
            {
                bool available = false;
                const quint16 packed = terrain.packedTile(x, y, layer,
                                                          &available);
                if(!available || packed == 0xffffu)
                    continue;
                const int slot = SMprPackedTile::decode(packed).atlasSlot();
                QVERIFY2(slot >= 0 && slot < binding.decodedAtlasTiles.size(),
                         qPrintable(QStringLiteral(
                             "cell %1,%2 references missing physical slot %3 (page %4 local %5)")
                             .arg(x).arg(y).arg(slot).arg(slot / 64)
                             .arg(slot % 64)));
                QVERIFY(!binding.decodedAtlasTiles.at(slot).isNull());
                referencedSlots.insert(slot);
                if(layer == ETerrainTileLayer::Land)
                {
                    landReferencedSlots.insert(slot);
                    if(!firstLandCell.contains(slot))
                        firstLandCell.insert(slot, QStringLiteral("%1,%2")
                                             .arg(x).arg(y));
                }
            }

    // A decoded QImage can be non-null while containing only transparent
    // padding. Such a referenced land slot reveals the cyan clear colour as a
    // hole, so assert actual physical coverage rather than decoder success.
    QList<int> orderedLandSlots = landReferencedSlots.values();
    std::sort(orderedLandSlots.begin(), orderedLandSlots.end());
    QStringList blankLandSlots;
    for(int slot : orderedLandSlots)
    {
        const QImage tile = binding.decodedAtlasTiles.at(slot)
            .convertToFormat(QImage::Format_RGBA8888);
        bool hasVisiblePixel = false;
        bool allClearCyan = true;
        for(int y = 0; y < tile.height(); ++y)
        {
            const uchar* row = tile.constScanLine(y);
            for(int x = 0; x < tile.width(); ++x)
            {
                const uchar* pixel = row + x * 4;
                hasVisiblePixel = hasVisiblePixel || pixel[3] != 0;
                allClearCyan = allClearCyan
                    && pixel[0] == 46 && pixel[1] == 181
                    && pixel[2] == 217;
            }
        }
        if(!hasVisiblePixel)
            blankLandSlots.append(QStringLiteral(
                "%1(page %2/local %3, first cell %4)")
                .arg(slot).arg(slot / 64).arg(slot % 64)
                .arg(firstLandCell.value(slot)));
        QVERIFY2(!allClearCyan,
                 qPrintable(QStringLiteral(
                     "referenced land slot %1 (page %2 local %3) is the renderer clear colour")
                     .arg(slot).arg(slot / 64).arg(slot % 64)));
    }
    QVERIFY2(blankLandSlots.isEmpty(),
             qPrintable(QStringLiteral(
                 "referenced fully-transparent land slots: %1")
                 .arg(blankLandSlots.join(QStringLiteral(", ")))));

    const QString semanticName = STerrainSemanticFieldDocument::memberName(
        document.mapInfo().innerNamespace, QFileInfo(mprPath));
    QMap<QString, QByteArray> members;
    QString memberError;
    QVERIFY2(CTerrainAtlasArchiveBindingResolver::readArchiveMembers(
                 atlasPath, {semanticName}, &members, nullptr, &memberError),
             qPrintable(memberError));
    qInfo().noquote() << QStringLiteral(
        "draft adoption pair mprSha256:%1 atlasPixelSha256:%2 pages:%3 physicalSlots:%4 referencedSlots:%5 landReferencedSlots:%6 maxReferencedSlot:%7 slot68Sha256:%8 semanticMember:%9")
        .arg(QString::fromLatin1(mprSha256),
             QString::fromLatin1(binding.decodedAtlasPixelSha256))
        .arg(document.mapInfo().textureCount).arg(physicalSlots)
        .arg(referencedSlots.size())
        .arg(landReferencedSlots.size())
        .arg(referencedSlots.isEmpty() ? -1 : *std::max_element(
                 referencedSlots.constBegin(), referencedSlots.constEnd()))
        .arg(physicalSlots > 68 ? QString::fromLatin1(
                 CTerrainAtlasProfileInference::pixelSha256(
                     {binding.decodedAtlasTiles.at(68)}))
             : QStringLiteral("missing"))
        .arg(members.value(semanticName).isEmpty()
                 ? QStringLiteral("absent") : QStringLiteral("present"));
}

void EnduranceDocumentTests::externalOverlayAnimationMappingProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_OVERLAY_ANIMATION_REPRO_MPR");
    if(mprPath.isEmpty())
        QSKIP("Set EI_MAPPER_OVERLAY_ANIMATION_REPRO_MPR to the read-only overlay animation repro MPR");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("16EC786E18381B822AE73012A134969256394D08A443D2F1D6E01E93A8BEC36A"));

    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(document.mapInfo().animatedTiles.front().tileIndex, quint16(512));
    QCOMPARE(document.mapInfo().animatedTiles.front().frameCount, quint16(24));
    SAnimTile animation{};
    animation.tileIndex = 512;
    animation.nPhase = 24;
    const QVector<SAnimTile> animations{animation};

    struct ExpectedCell
    {
        QPoint cell;
        quint16 packed;
        int slot;
        int rotation;
        bool animated;
    };
    const QVector<ExpectedCell> cells{
        {{31, 11}, 49669, 517, 3, true},
        {{31, 10}, 506, 506, 0, false},
        {{30, 12}, 16896, 512, 1, true},
        {{29, 12}, 16896, 512, 1, true},
        // Controls on both sides prove that every authored physical phase in
        // the declared contiguous range participates in the same animation.
        {{30, 11}, 16896, 512, 1, true},
        {{32, 11}, 49669, 517, 3, true},
        {{28, 13}, 16896, 512, 1, true},
        {{28, 12}, 33298, 530, 2, true}
    };
    for(const ExpectedCell& expected : cells)
    {
        bool available = false;
        const quint16 packed = terrain.packedTile(
            expected.cell.x(), expected.cell.y(),
            ETerrainTileLayer::Overlay, &available);
        QVERIFY(available);
        QCOMPARE(packed, expected.packed);
        const SMprPackedTile decoded = SMprPackedTile::decode(packed);
        QCOMPARE(decoded.atlasSlot(), expected.slot);
        QCOMPARE(int(decoded.rotation), expected.rotation);
        QCOMPARE(terrain.overlayMaterialIndex(
                     expected.cell.x(), expected.cell.y()), qint16(0));
        QCOMPARE(ei::overlayTileAnimation(
                     expected.slot, &animations,
                     quint32(expected.cell.y() * terrain.tileCountX()
                             + expected.cell.x())).animated,
                 expected.animated);
    }
}

void EnduranceDocumentTests::externalOverlaySurfaceMappingProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_OVERLAY_SURFACE_REPRO_MPR");
    if(mprPath.isEmpty())
        QSKIP("Set EI_MAPPER_OVERLAY_SURFACE_REPRO_MPR to the read-only overlay-surface repro MPR");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("B6CBBED55918B430FD04D3C4B6D16EE00FCFADA17AB4503D673A7E523CE0F22A"));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));

    QCOMPARE(terrain.tileCountX(), 176);
    QCOMPARE(terrain.tileCountY(), 144);
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(document.mapInfo().animatedTiles.front().tileIndex, quint16(508));
    QCOMPARE(document.mapInfo().animatedTiles.front().frameCount, quint16(31));
    SAnimTile animation{};
    animation.tileIndex = 508;
    animation.nPhase = 31;
    const QVector<SAnimTile> animations{animation};
    struct ExpectedCell
    {
        QPoint cell;
        qint16 material;
        int slot;
        int rotation;
        bool animated;
        int startPhase;
    };
    const QVector<ExpectedCell> cells{
        {{5, 0}, 2, 508, 1, true, 0},
        {{13, 0}, 2, 517, 0, true, 9},
        {{8, 8}, 2, 538, 0, true, 30},
        {{67, 8}, 2, 488, 2, false, 0},
        {{70, 35}, 1, 489, 1, false, 0},
        {{71, 41}, 1, 488, 0, false, 0},
        {{132, 47}, 1, 487, 0, false, 0}
    };
    for(const ExpectedCell& expected : cells)
    {
        bool available = false;
        const qint16 material = terrain.overlayMaterialIndex(
            expected.cell.x(), expected.cell.y(), &available);
        QVERIFY(available);
        QCOMPARE(material, expected.material);
        const SMprPackedTile packed = SMprPackedTile::decode(
            terrain.packedTile(expected.cell.x(), expected.cell.y(),
                               ETerrainTileLayer::Overlay));
        QCOMPARE(packed.atlasSlot(), expected.slot);
        QCOMPARE(int(packed.rotation), expected.rotation);
        const ei::SOverlayTileAnimation resolved =
            ei::overlayTileAnimation(expected.slot, &animations);
        QCOMPARE(resolved.animated, expected.animated);
        QCOMPARE(resolved.startPhase, expected.startPhase);
    }
}

void EnduranceDocumentTests::overlayActivationMixedBrushPaintsSelectedTileAndMaterial()
{
    CValidationReport report;
    CMprDocument baseline;
    QVERIFY2(baseline.loadBytes(syntheticMpr(syntheticLandSector(1200)),
                                "overlay-mixed-activation.mpr", &report),
             qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY(terrain.load(baseline));
    COverlaySurfaceEditHistory history;

    STerrainTileBrush point;
    point.centerX = 2.0f;
    point.centerY = 3.0f;
    point.radius = 0.0f;
    const SOverlaySurfaceCommand initial = terrain.makeOverlayActivation(
        point, 1, 0, 1, 2, &report);
    QVERIFY2(history.execute(&terrain, initial, &report),
             qPrintable(report.toText()));
    const quint16 originalActiveTile = terrain.packedTile(
        2, 3, ETerrainTileLayer::Overlay);

    STerrainTileBrush mixed;
    mixed.centerX = 2.5f;
    mixed.centerY = 3.0f;
    mixed.radius = 0.6f;
    mixed.shape = ETerrainTileBrushShape::Square;
    SMprPackedTile selected;
    selected.atlasIndex = 0;
    selected.tileIndex = 0;
    selected.rotation = 3;
    const SOverlaySurfaceCommand repaint = terrain.makeOverlayActivation(
        mixed, 0, selected.atlasIndex, selected.tileIndex,
        selected.rotation, &report);
    QVERIFY2(!repaint.isEmpty(), qPrintable(report.toText()));
    QVERIFY2(history.execute(&terrain, repaint, &report),
             qPrintable(report.toText()));

    QCOMPARE(terrain.overlayMaterialIndex(2, 3), qint16(0));
    QCOMPARE(terrain.packedTile(2, 3, ETerrainTileLayer::Overlay),
             selected.encode());
    QCOMPARE(terrain.overlayMaterialIndex(3, 3), qint16(0));
    QCOMPARE(terrain.packedTile(3, 3, ETerrainTileLayer::Overlay),
             selected.encode());

    QVERIFY(history.undo(&terrain, &report));
    QCOMPARE(terrain.overlayMaterialIndex(2, 3), qint16(1));
    QCOMPARE(terrain.packedTile(2, 3, ETerrainTileLayer::Overlay),
             originalActiveTile);
    bool available = true;
    QCOMPARE(terrain.overlayMaterialIndex(3, 3, &available), qint16(-1));
    QVERIFY(!available);
    QVERIFY(history.redo(&terrain, &report));
    QCOMPARE(terrain.packedTile(2, 3, ETerrainTileLayer::Overlay),
             selected.encode());
    QCOMPARE(terrain.packedTile(3, 3, ETerrainTileLayer::Overlay),
             selected.encode());
}

void EnduranceDocumentTests::externalZone1TestOverlays3MappingProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS3_REPRO_MPR");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS3_REPRO_ATLAS");
    if(mprPath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set EI_MAPPER_ZONE1TESTOVERLAYS3_REPRO_MPR and EI_MAPPER_ZONE1TESTOVERLAYS3_REPRO_ATLAS to the read-only finalized pair");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("E3DEC573826DDE3F4E073730E1A1E9FD8595D6B83E10933DC2B457B17218D133"));
    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(atlasFile.readAll(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("9DE556E0C59C6E915605B2FE6B76DBC97B40B04EBEFA643A24B3E00515101520"));

    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {atlasPath}, document.mapInfo().innerNamespace, atlasPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    QCOMPARE(terrain.tileCountX(), 176);
    QCOMPARE(terrain.tileCountY(), 144);
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(document.mapInfo().animatedTiles.front().tileIndex, quint16(506));
    QCOMPARE(document.mapInfo().animatedTiles.front().frameCount, quint16(30));
    QCOMPARE(binding.decodedAtlasTiles.size(), 576);
    QCOMPARE(binding.decodedAtlasPixelSha256,
             QByteArrayLiteral("322a867799ad36f7a91eca26332b30ae16f8f5d8726b36444ed63fb0c5f567ef"));

    qInfo() << "R5 animation records";
    for(const SMprAnimatedTileRecord& record : document.mapInfo().animatedTiles)
        qInfo() << record.tileIndex << record.frameCount;
    qInfo() << "R5 dimensions/materials/atlas"
            << terrain.tileCountX() << terrain.tileCountY()
            << document.mapInfo().materials.size()
            << binding.decodedAtlasTiles.size()
            << binding.decodedAtlasPixelSha256;
    for(int slot = 504; slot <= 535; ++slot)
    {
        const QImage image = binding.decodedAtlasTiles.at(slot)
            .convertToFormat(QImage::Format_RGBA8888);
        quint64 red = 0, green = 0, blue = 0, alpha = 0;
        for(int y = 0; y < image.height(); ++y)
        {
            const uchar* row = image.constScanLine(y);
            for(int x = 0; x < image.width(); ++x)
            {
                red += row[x * 4];
                green += row[x * 4 + 1];
                blue += row[x * 4 + 2];
                alpha += row[x * 4 + 3];
            }
        }
        const quint64 pixels = quint64(image.width()) * image.height();
        qInfo() << "R5 frame" << slot
                << "rgba" << red / pixels << green / pixels
                << blue / pixels << alpha / pixels
                << CTerrainAtlasProfileInference::pixelSha256({image});
    }

    QVector<SAnimTile> animations;
    for(const SMprAnimatedTileRecord& record : document.mapInfo().animatedTiles)
    {
        SAnimTile animation{};
        animation.tileIndex = record.tileIndex;
        animation.nPhase = record.frameCount;
        animations.append(animation);
    }
    struct ExpectedCell
    {
        QPoint cell;
        qint16 material;
        quint16 raw;
        int slot;
        int rotation;
        bool animated;
        int startPhase;
    };
    const QVector<ExpectedCell> cells{
        {{13, 4}, 2, 506, 506, 0, true, 0},
        {{12, 14}, 2, 513, 513, 0, true, 7},
        {{3, 12}, 2, 49686, 534, 3, true, 28},
        {{12, 4}, 2, 521, 521, 0, true, 15},
        {{14, 4}, 2, 16888, 504, 1, false, 0},
        {{11, 14}, 2, 49670, 518, 3, true, 12},
        {{13, 14}, 2, 16888, 504, 1, false, 0},
        {{2, 12}, 2, 515, 515, 0, true, 9},
        {{4, 12}, 2, 49677, 525, 3, true, 19}
    };
    for(const ExpectedCell& expected : cells)
    {
        const QPoint cell = expected.cell;
        bool materialAvailable = false;
        bool packedAvailable = false;
        const qint16 material = terrain.overlayMaterialIndex(
            cell.x(), cell.y(), &materialAvailable);
        const quint16 raw = terrain.packedTile(
            cell.x(), cell.y(), ETerrainTileLayer::Overlay, &packedAvailable);
        const SMprPackedTile packed = SMprPackedTile::decode(raw);
        const ei::SOverlayTileAnimation animation = ei::overlayTileAnimation(
            packed.atlasSlot(), &animations,
            quint32(cell.y() * terrain.tileCountX() + cell.x()));
        QVERIFY(materialAvailable);
        QVERIFY(packedAvailable);
        QCOMPARE(material, expected.material);
        QCOMPARE(raw, expected.raw);
        QCOMPARE(packed.atlasSlot(), expected.slot);
        QCOMPARE(int(packed.rotation), expected.rotation);
        QCOMPARE(animation.animated, expected.animated);
        QCOMPARE(animation.startPhase, expected.startPhase);
        QByteArray pixels;
        if(packed.atlasSlot() >= 0
           && packed.atlasSlot() < binding.decodedAtlasTiles.size())
            pixels = CTerrainAtlasProfileInference::pixelSha256(
                {binding.decodedAtlasTiles.at(packed.atlasSlot())});
        const QByteArray sector = terrain.sectorPayload(
            cell.x() / 16, cell.y() / 16);
        int minimumRawClearance = std::numeric_limits<int>::max();
        int maximumRawClearance = std::numeric_limits<int>::min();
        for(int vertexY = (cell.y() % 16) * 2;
            vertexY <= (cell.y() % 16) * 2 + 2; ++vertexY)
            for(int vertexX = (cell.x() % 16) * 2;
                vertexX <= (cell.x() % 16) * 2 + 2; ++vertexX)
            {
                const int vertex = vertexY * 33 + vertexX;
                const int landOffset = 5 + vertex * 8 + 2;
                const int overlayOffset = 5 + 33 * 33 * 8 + vertex * 8 + 2;
                const int landHeight = qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar*>(sector.constData() + landOffset));
                const int overlayHeight = qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar*>(sector.constData() + overlayOffset));
                minimumRawClearance = qMin(minimumRawClearance,
                                            overlayHeight - landHeight);
                maximumRawClearance = qMax(maximumRawClearance,
                                            overlayHeight - landHeight);
            }
        qInfo() << "R5 cell" << cell
                << "available" << materialAvailable << packedAvailable
                << "material/raw/slot/rotation"
                << material << raw << packed.atlasSlot() << packed.rotation
                << "animation" << animation.animated
                << animation.startPhase << animation.frameCount
                << "raw-clearance" << minimumRawClearance
                << maximumRawClearance
                << "pixels" << pixels;
    }
}

void EnduranceDocumentTests::externalZone1TestOverlays4NoAnimMappingProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS4_REPRO_MPR");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS4_REPRO_ATLAS");
    if(mprPath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set the R6 read-only finalized MPR/atlas pair environment variables");
    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("B0D594F6ADB16DB162F06AE740ABAEE3BDC2FF7E751943EB56D44D4040F51943"));
    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(atlasFile.readAll(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("B9494F0901D70D26ACF89592B05AFCA0A6E45B9E485F62DB045C34FE56147627"));
    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {atlasPath}, document.mapInfo().innerNamespace, atlasPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));

    QMap<int, int> overlaySlots;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const quint16 raw = terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available);
            if(available)
                ++overlaySlots[SMprPackedTile::decode(raw).atlasSlot()];
        }
    qInfo() << "R6 exact fixture atlas/animations/overlay-slots"
            << binding.decodedAtlasTiles.size()
            << document.mapInfo().animatedTiles.size()
            << overlaySlots;
    for(const SMprAnimatedTileRecord& record : document.mapInfo().animatedTiles)
        qInfo() << "R6 animation" << record.tileIndex << record.frameCount;
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(document.mapInfo().animatedTiles.front().tileIndex, quint16(504));
    QCOMPARE(document.mapInfo().animatedTiles.front().frameCount, quint16(32));
    const QSet<int> expectedFrameMemberBases{507, 510, 511, 516, 517, 532};
    QSet<int> actualFrameMemberBases;
    QMap<int, int> frameMemberMaterials;
    for(auto slot = overlaySlots.constBegin(); slot != overlaySlots.constEnd(); ++slot)
        if(slot.key() >= 504 && slot.key() < 536)
            actualFrameMemberBases.insert(slot.key());
    QCOMPARE(actualFrameMemberBases, expectedFrameMemberBases);
    QCOMPARE(overlaySlots.value(504), 0);
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool packedAvailable = false;
            const int slot = SMprPackedTile::decode(terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &packedAvailable)).atlasSlot();
            if(!packedAvailable || slot < 504 || slot >= 536)
                continue;
            bool materialAvailable = false;
            const qint16 material = terrain.overlayMaterialIndex(
                x, y, &materialAvailable);
            QVERIFY(materialAvailable);
            ++frameMemberMaterials[material];
        }
    qInfo() << "R6 frame-member surface materials" << frameMemberMaterials;
    QCOMPARE(std::accumulate(frameMemberMaterials.cbegin(),
                             frameMemberMaterials.cend(), 0), 150);
    for(auto item = frameMemberMaterials.constBegin();
        item != frameMemberMaterials.constEnd(); ++item)
    {
        QVERIFY(item.key() >= 0 && item.key() < document.mapInfo().materials.size());
        const SMprMaterialRecord& material =
            document.mapInfo().materials.at(item.key());
        qInfo() << "R6 surface material" << item.key() << "cells" << item.value()
                << "type" << material.type << "opacity" << material.opacity
                << "selfIllumination" << material.selfIllumination
                << "waveMultiplier" << material.waveMultiplier
                << "warpSpeed" << material.warpSpeed
                << "reserved" << material.reserved1 << material.reserved2
                << material.reserved3;
    }
    QMap<int, int> exactStartRemap;
    for(int slot = 504; slot < 536; ++slot)
        exactStartRemap.insert(slot, 504);
    const STerrainTileCommand normalizeToRuntimeStart =
        terrain.makeAtlasSlotRemap(exactStartRemap);
    QCOMPARE(normalizeToRuntimeStart.deltas.size(), 150);
    QVERIFY2(terrain.apply(normalizeToRuntimeStart, true, &report),
             qPrintable(report.toText()));
    int normalizedStartCells = 0;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const int slot = SMprPackedTile::decode(terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available)).atlasSlot();
            if(available && slot == 504)
                ++normalizedStartCells;
        }
    QCOMPARE(normalizedStartCells, 150);
    QVERIFY2(terrain.apply(normalizeToRuntimeStart, false, &report),
             qPrintable(report.toText()));
    QCOMPARE(SMprPackedTile::decode(terrain.packedTile(
                 normalizeToRuntimeStart.deltas.front().globalX,
                 normalizeToRuntimeStart.deltas.front().globalY,
                 normalizeToRuntimeStart.deltas.front().layer)).atlasSlot(),
             SMprPackedTile::decode(
                 normalizeToRuntimeStart.deltas.front().before).atlasSlot());
    const qint32 frameRuntimeType = document.mapInfo().tileTypes.at(504);
    for(int slot = 504; slot < 536; ++slot)
    {
        QCOMPARE(document.mapInfo().tileTypes.at(slot), frameRuntimeType);
        qInfo() << "R6 frame" << slot << "runtimeType" << frameRuntimeType
                << "pixels" << CTerrainAtlasProfileInference::pixelSha256(
                       {binding.decodedAtlasTiles.at(slot)});
    }
    QVERIFY(!overlaySlots.isEmpty());
    for(auto slot = overlaySlots.constBegin(); slot != overlaySlots.constEnd(); ++slot)
        QVERIFY(slot.key() >= 0 && slot.key() < binding.decodedAtlasTiles.size());
}

void EnduranceDocumentTests::externalZone1TestOverlays5RuntimeFlipbookProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS5_REPRO_MPR");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS5_REPRO_ATLAS");
    if(mprPath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set the R7 read-only finalized MPR/atlas pair environment variables");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("105E70A6C0B043D2C64975062C1504C7C4EDB6003679859A82628CE379A7A2D2"));
    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(atlasFile.readAll(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("748C0AF0B132B00D66EF4120EE8EC91C1DBC1A9CC872815297A22B0E748FA10C"));

    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {atlasPath}, document.mapInfo().innerNamespace, atlasPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    const SMprAnimatedTileRecord record =
        document.mapInfo().animatedTiles.front();
    const int start = int(record.tileIndex);
    const int count = int(record.frameCount);
    QVERIFY(count >= 2);
    QVERIFY(start >= 0 && start + count <= binding.decodedAtlasTiles.size());
    QVERIFY(start + count <= document.mapInfo().tileTypes.size());
    QVERIFY2((start % 64) + count > 64,
             "The exact regression fixture no longer crosses its physical MMP page");

    int exactStartCells = 0;
    QMap<int, int> referencedFrameSlots;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const int slot = SMprPackedTile::decode(terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available)).atlasSlot();
            if(!available)
                continue;
            if(slot == start)
                ++exactStartCells;
            if(slot >= start && slot < start + count)
                ++referencedFrameSlots[slot];
        }
    QVERIFY2(exactStartCells > 0,
             "No overlay cell references the runtime animation start slot");
    QCOMPARE(referencedFrameSlots.size(), 1);
    QVERIFY(referencedFrameSlots.contains(start));

    const qint32 runtimeType = document.mapInfo().tileTypes.at(start);
    const STerrainAtlasProfileSlot* firstDefinition = nullptr;
    for(const STerrainAtlasProfileSlot& definition : binding.profile.slotDefinitions)
        if(definition.slot == start)
            firstDefinition = &definition;
    QVERIFY(firstDefinition);
    const STerrainTransitionSignature signature = firstDefinition->signature;
    QSet<QByteArray> frameHashes;
    for(int slot = start; slot < start + count; ++slot)
    {
        QCOMPARE(document.mapInfo().tileTypes.at(slot), runtimeType);
        const STerrainAtlasProfileSlot* definition = nullptr;
        for(const STerrainAtlasProfileSlot& candidate : binding.profile.slotDefinitions)
            if(candidate.slot == slot)
                definition = &candidate;
        QVERIFY(definition);
        QCOMPARE(definition->signature, signature);
        const QImage frame = binding.decodedAtlasTiles.at(slot)
            .convertToFormat(QImage::Format_RGBA8888);
        const QByteArray hash = CTerrainAtlasProfileInference::pixelSha256({frame});
        frameHashes.insert(hash);
        quint64 red = 0, green = 0, blue = 0, alpha = 0;
        for(int y = 0; y < frame.height(); ++y)
        {
            const uchar* row = frame.constScanLine(y);
            for(int x = 0; x < frame.width(); ++x)
            {
                red += row[x * 4]; green += row[x * 4 + 1];
                blue += row[x * 4 + 2]; alpha += row[x * 4 + 3];
            }
        }
        const quint64 pixels = quint64(frame.width()) * frame.height();
        qInfo() << "R7 flipbook frame" << slot
                << "runtimeType" << runtimeType
                << "signature" << definition->signature.northWest
                << definition->signature.northEast
                << definition->signature.southEast
                << definition->signature.southWest
                << "meanRgba" << red / pixels << green / pixels
                << blue / pixels << alpha / pixels
                << "sha256" << hash;
    }
    QCOMPARE(frameHashes.size(), count);

    const STerrainAtlasAnalysis analysis =
        binding.profile.usableAnalysis(&report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    QMap<qint32, qint32> materialRuntimeTypes;
    for(const STerrainAtlasProfileFamily& family : binding.profile.families)
        materialRuntimeTypes.insert(family.id, family.tileType);
    CTerrainWorkingAtlas working(
        analysis.overlayCompatibleCatalog, binding.decodedAtlasTiles,
        0x50432u, kMprPackedTerrainAtlasSlotLimit, {}, {},
        document.mapInfo().tileTypes, materialRuntimeTypes);
    QSharedPointer<CTerrainWorkingAtlas> staged = working.createStagingCopy();
    QVector<SMprAnimatedTileRecord> normalized;
    QMap<int, int> remap;
    CValidationReport normalizeReport;
    QVERIFY2(staged->normalizeAnimationRanges(
        {record}, &normalized, &remap, &normalizeReport),
        qPrintable(normalizeReport.toText()));
    QCOMPARE(normalized.size(), 1);
    QCOMPARE(normalized.front().tileIndex, quint16(539));
    QCOMPARE(normalized.front().frameCount, record.frameCount);
    QVERIFY2((int(normalized.front().tileIndex) % 64)
                 + int(normalized.front().frameCount) <= 64,
             "The exact runtime flipbook still crosses an MMP page after normalization");
    QVERIFY(normalized.front().tileIndex != record.tileIndex);
    QCOMPARE(remap.value(start), int(normalized.front().tileIndex));
    QCOMPARE(staged->activeTileImages().size(), 9 * 64);
    for(int phase = 0; phase < count; ++phase)
    {
        STerrainWorkingTileIdentity identity;
        QVERIFY(staged->tileIdentityForSlot(
            int(normalized.front().tileIndex) + phase, &identity));
        QCOMPARE(identity.sourceSlot, start + phase);
        STerrainWorkingTileIdentity sourceIdentity;
        QVERIFY(working.tileIdentityForSlot(start + phase, &sourceIdentity));
        QCOMPARE(identity.physicalPixelSha256,
                 sourceIdentity.physicalPixelSha256);
    }
    qInfo() << "R7 flipbook record/start references"
            << start << count << exactStartCells << referencedFrameSlots
            << "atlasSlots" << binding.decodedAtlasTiles.size();
}

void EnduranceDocumentTests::restoredTerrainFamiliesAreMarkedUsedBeforeFirstPaint()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int start = source.indexOf("bool CView::ensureTerrainAutoCatalogReady()");
    const int end = source.indexOf("void CView::unloadLand", start);
    QVERIFY(start >= 0 && end > start);
    const QByteArray load = source.mid(start, end - start);
    const int catalog = load.indexOf("rebuildTerrainAutoCatalog();");
    const int restore = load.indexOf("restoreTerrainSemanticFieldFromArchive(",
                                     catalog);
    const int usage = load.indexOf("refreshTerrainFamilyUsage();", restore);
    QVERIFY2(catalog >= 0 && restore > catalog && usage > restore,
             "Loaded semantic fields are restored after the only usage scan, so every family appears unused until the first paint");

    const int refreshStart = source.indexOf(
        "void CView::refreshTerrainFamilyUsage()");
    const int refreshEnd = source.indexOf(
        "QVector<STerrainAtlasSlotUsage> CView::terrainAtlasUsage() const",
        refreshStart);
    QVERIFY(refreshStart >= 0 && refreshEnd > refreshStart);
    const QByteArray refresh = source.mid(
        refreshStart, refreshEnd - refreshStart);
    const int backingLand = refresh.indexOf("m_modernLandPaintChoices");
    const int backingUnderwater = refresh.indexOf(
        "m_modernUnderwaterPaintChoices");
    const int backingOverlay = refresh.indexOf("m_modernOverlayPaintChoices");
    const int widgetPublish = refresh.indexOf("setUsedTerrainFamilies");
    QVERIFY2(backingLand >= 0 && backingUnderwater > backingLand
             && backingOverlay > backingUnderwater
             && widgetPublish > backingOverlay,
             "Usage is updated only in the current widget copy; deferred thumbnail publication later restores every backing choice to unused");
}

void EnduranceDocumentTests::externalZone1TestOverlays6DraftFinalizationProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS6_REPRO_MPR");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTOVERLAYS6_REPRO_ATLAS");
    if(mprPath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set the R9 read-only draft MPR/atlas pair environment variables");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("093B61A6C49B0893FF1261136F771D18D3113AC430C22FCDBB2813F90D325BB7"));
    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(atlasFile.readAll(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("F5B3DA7BE5398E6226DAB3476FF7B0C4BFBE21205EC66DEAC442B766ABA1BE7C"));

    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {atlasPath}, document.mapInfo().innerNamespace, atlasPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    QCOMPARE(document.mapInfo().animatedTiles.size(), 2);
    QCOMPARE(document.mapInfo().animatedTiles.at(0).tileIndex, quint16(540));
    QCOMPARE(document.mapInfo().animatedTiles.at(0).frameCount, quint16(32));
    QCOMPARE(document.mapInfo().animatedTiles.at(1).tileIndex, quint16(714));
    QCOMPARE(document.mapInfo().animatedTiles.at(1).frameCount, quint16(19));
    const STerrainAtlasAnalysis analysis = binding.profile.usableAnalysis(&report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QVERIFY2(terrain.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, analysis.landCatalog, &report,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(report.toText()));
    CTerrainVirtualMaterialField field;
    QVERIFY(terrain.copyVirtualMaterialField(ETerrainTileLayer::Land, &field));

    QVector<quint16> pinnedOverlay;
    QVector<qint32> placedTypes;
    QVector<quint16> placedPacked;
    QVector<bool> preservePlaced;
    QSet<int> referencedSlots;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const quint16 overlay = terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available);
            if(available && overlay != 0xffffu)
            {
                pinnedOverlay.append(overlay);
                referencedSlots.insert(SMprPackedTile::decode(overlay).atlasSlot());
            }
            const quint16 land = terrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available);
            bool typeFound = false;
            placedTypes.append(available
                ? terrain.tileType(land, &typeFound) : -1);
            placedPacked.append(available ? land : quint16(0));
            preservePlaced.append(true);
            if(available)
                referencedSlots.insert(SMprPackedTile::decode(land).atlasSlot());
        }
    QStringList referencedObsolete;
    for(int obsolete = 504; obsolete < 536; ++obsolete)
        if(referencedSlots.contains(obsolete))
            referencedObsolete.append(QString::number(obsolete));
    qInfo() << "R9 exact draft referenced pre-repack slots"
            << referencedObsolete;

    QMap<qint32, qint32> materialRuntimeTypes;
    for(const STerrainAtlasProfileFamily& family : binding.profile.families)
        materialRuntimeTypes.insert(family.id, family.tileType);
    CTerrainVirtualAtlas atlas(
        analysis.landCatalog, binding.decodedAtlasTiles, 0x50432u,
        {}, {}, true, document.mapInfo().tileTypes, materialRuntimeTypes);
    QVector<STerrainAtlasAnimationRange> animations;
    for(const SMprAnimatedTileRecord& source : document.mapInfo().animatedTiles)
    {
        STerrainAtlasAnimationRange animation;
        animation.tileIndex = source.tileIndex;
        animation.frameCount = source.frameCount;
        animations.append(animation);
    }
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, document.mapInfo().innerNamespace,
        kMprPackedTerrainAtlasSlotLimit, document.mapInfo().tileTypes,
        &report, pinnedOverlay, placedTypes, placedPacked, {},
        &analysis.overlayCompatibleCatalog, preservePlaced, animations);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n") + report.toText()));
    QCOMPARE(finalized.animations.size(), 2);
    QCOMPARE(finalized.animations.at(0).tileIndex, quint16(0));
    QCOMPARE(finalized.animations.at(0).frameCount, quint16(32));
    QCOMPARE(finalized.animations.at(1).tileIndex, quint16(32));
    QCOMPARE(finalized.animations.at(1).frameCount, quint16(19));
    for(int obsolete = 504; obsolete < 536; ++obsolete)
        QVERIFY2(!finalized.authoredSlotRemap.contains(obsolete),
                 "Clean finalization retained an obsolete Emerald source-frame copy");
    for(const STerrainAtlasAnimationRange& animation : finalized.animations)
    {
        QVERIFY(int(animation.tileIndex % 64) + int(animation.frameCount) <= 64);
        const STerrainTransitionSignature signature =
            finalized.tiles.at(animation.tileIndex).canonicalSignature;
        QVERIFY(signature.northWest == signature.northEast
                && signature.northWest == signature.southEast
                && signature.northWest == signature.southWest);
        for(int phase = 1; phase < animation.frameCount; ++phase)
            QCOMPARE(finalized.tiles.at(animation.tileIndex + phase)
                         .canonicalSignature,
                     signature);
    }
}

void EnduranceDocumentTests::externalZone1TestoOverlayMappingProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTO_REPRO_MPR");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE1TESTO_REPRO_ATLAS");
    if(mprPath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set EI_MAPPER_ZONE1TESTO_REPRO_MPR and EI_MAPPER_ZONE1TESTO_REPRO_ATLAS to the read-only finalized pair");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("1830D1017B8F6C92E8F6D1BA85627529CEB40889E63E6EE179B5A7922202FCA7"));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QCOMPARE(terrain.tileCountX(), 176);
    QCOMPARE(terrain.tileCountY(), 144);
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(document.mapInfo().animatedTiles.front().tileIndex, quint16(504));
    QCOMPARE(document.mapInfo().animatedTiles.front().frameCount, quint16(31));
    QVERIFY(document.mapInfo().materials.size() > 3);
    const SMprMaterialRecord& waveMaterial = document.mapInfo().materials.at(0);
    const SMprMaterialRecord& stillMaterial = document.mapInfo().materials.at(3);
    QCOMPARE(waveMaterial.type, qint32(3));
    QVERIFY(qAbs(waveMaterial.opacity - 0.6f) < 0.0001f);
    QVERIFY(qAbs(waveMaterial.waveMultiplier - 0.8f) < 0.0001f);
    QCOMPARE(stillMaterial.type, qint32(1));
    QVERIFY(qAbs(stillMaterial.opacity - 1.0f) < 0.0001f);
    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(atlasFile.readAll(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("1DB36BC96750434BB21FBE2BAD84DD6182226A87FA5BB8537ABE69A9F86C265A"));
    const STerrainAtlasEmbeddedBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {atlasPath}, document.mapInfo().innerNamespace, atlasPath);
    QVERIFY2(binding.success(), qPrintable(binding.error));
    QCOMPARE(binding.decodedAtlasTiles.size(), 576);
    QCOMPARE(binding.decodedAtlasPixelSha256,
             QByteArrayLiteral("6799d8c4aaf7a40c005afedcf94130264505dcdf7200abe3cf94a5875480bb7e"));
    QCOMPARE(binding.profile.slotDefinitions.size(), 576);
    QCOMPARE(binding.profile.provenance,
             QStringLiteral("portable-working-modern-atlas-v1"));
    QSet<QByteArray> physicalFrameHashes;
    for(int slot = 504; slot <= 535; ++slot)
    {
        const QImage image = binding.decodedAtlasTiles.at(slot)
            .convertToFormat(QImage::Format_RGBA8888);
        quint64 red = 0, green = 0, blue = 0, alpha = 0;
        for(int y = 0; y < image.height(); ++y)
        {
            const uchar* row = image.constScanLine(y);
            for(int x = 0; x < image.width(); ++x)
            {
                red += row[x * 4]; green += row[x * 4 + 1];
                blue += row[x * 4 + 2]; alpha += row[x * 4 + 3];
            }
        }
        const quint64 pixels = quint64(image.width()) * image.height();
        physicalFrameHashes.insert(
            CTerrainAtlasProfileInference::pixelSha256({image}));
        const auto definition = std::find_if(
            binding.profile.slotDefinitions.constBegin(),
            binding.profile.slotDefinitions.constEnd(),
            [slot](const STerrainAtlasProfileSlot& value)
            { return value.slot == slot; });
        QVERIFY(definition != binding.profile.slotDefinitions.constEnd());
        QCOMPARE(definition->signature.northWest, qint32(1278168451));
        QCOMPARE(definition->signature.northEast, qint32(1278168451));
        QCOMPARE(definition->signature.southEast, qint32(1278168451));
        QCOMPARE(definition->signature.southWest, qint32(1278168451));
        QVERIFY(red / pixels >= 44 && red / pixels <= 53);
        QVERIFY(green / pixels >= 88 && green / pixels <= 100);
        QVERIFY(blue / pixels >= 78 && blue / pixels <= 90);
        QCOMPARE(alpha / pixels, quint64(255));
    }
    QCOMPARE(physicalFrameHashes.size(), 32);
    QVector<SAnimTile> animations;
    for(const SMprAnimatedTileRecord& record : document.mapInfo().animatedTiles)
    {
        SAnimTile animation{};
        animation.tileIndex = record.tileIndex;
        animation.nPhase = record.frameCount;
        animations.append(animation);
    }
    struct ExpectedCell
    {
        QPoint cell;
        qint16 material;
        quint16 packed;
        int slot;
        int rotation;
        bool animated;
        int startPhase;
    };
    const QVector<ExpectedCell> cells{
        {{11, 13}, 0, 16888, 504, 1, true, 0},
        {{10, 13}, 3, 16905, 521, 1, true, 17},
        {{8, 13}, 3, 16899, 515, 1, true, 11},
        {{8, 14}, 3, 49684, 532, 3, true, 28},
        {{9, 13}, 3, 49687, 535, 3, false, 0},
        {{9, 14}, 3, 49687, 535, 3, false, 0},
        {{10, 14}, 0, 49687, 535, 3, false, 0},
        {{11, 14}, 0, 16888, 504, 1, true, 0},
        {{7, 13}, 3, 16899, 515, 1, true, 11},
        {{7, 14}, 3, 49684, 532, 3, true, 28},
        {{12, 13}, 0, 33293, 525, 2, true, 21},
        {{12, 14}, 0, 49685, 533, 3, true, 29}
    };
    for(const ExpectedCell& expected : cells)
    {
        bool materialAvailable = false;
        bool packedAvailable = false;
        const qint16 material = terrain.overlayMaterialIndex(
            expected.cell.x(), expected.cell.y(), &materialAvailable);
        const quint16 raw = terrain.packedTile(
            expected.cell.x(), expected.cell.y(),
            ETerrainTileLayer::Overlay, &packedAvailable);
        const SMprPackedTile packed = SMprPackedTile::decode(raw);
        const ei::SOverlayTileAnimation resolved = ei::overlayTileAnimation(
            packed.atlasSlot(), &animations,
            quint32(expected.cell.y() * terrain.tileCountX()
                    + expected.cell.x()));
        QVERIFY(materialAvailable);
        QVERIFY(packedAvailable);
        QCOMPARE(material, expected.material);
        QCOMPARE(raw, expected.packed);
        QCOMPARE(packed.atlasSlot(), expected.slot);
        QCOMPARE(int(packed.rotation), expected.rotation);
        QCOMPARE(resolved.animated, expected.animated);
        QCOMPARE(resolved.startPhase, expected.startPhase);
    }
}

void EnduranceDocumentTests::externalZone11OverlayRegridProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_ZONE11_REPRO_MPR");
    if(mprPath.isEmpty())
        QSKIP("Set EI_MAPPER_ZONE11_REPRO_MPR to the read-only Zone11 MPR");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QCOMPARE(QCryptographicHash::hash(document.originalBytes(),
                 QCryptographicHash::Sha256).toHex().toUpper(),
             QByteArrayLiteral("427414848D89887DFB648E98634A92451B9D887A2C8912C25171E8DA1F4D9A3B"));
    QCOMPARE(document.mapInfo().textureCount, quint32(8));
    QCOMPARE(document.mapInfo().animatedTiles.size(), 1);
    QCOMPARE(document.mapInfo().animatedTiles.front().tileIndex, quint16(78));
    QCOMPARE(document.mapInfo().animatedTiles.front().frameCount, quint16(32));

    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    SAnimTile animation{};
    animation.tileIndex = 78;
    animation.nPhase = 32;
    const QVector<SAnimTile> animations{animation};
    QSet<int> overlaySlots;
    int animatedCells = 0;
    int nonAnimatedOverlayCells = 0;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool materialAvailable = false;
            const qint16 material = terrain.overlayMaterialIndex(
                x, y, &materialAvailable);
            if(!materialAvailable || material < 0)
                continue;
            bool packedAvailable = false;
            const quint16 packed = terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &packedAvailable);
            QVERIFY(packedAvailable);
            const int slot = SMprPackedTile::decode(packed).atlasSlot();
            overlaySlots.insert(slot);
            const ei::SOverlayTileAnimation resolved =
                ei::overlayTileAnimation(slot, &animations,
                    quint32(y * terrain.tileCountX() + x));
            if(resolved.animated)
                ++animatedCells;
            else
                ++nonAnimatedOverlayCells;
            for(int phase = 0; phase < (resolved.animated ? 32 : 1); ++phase)
            {
                const int renderedSlot = resolved.animated
                    ? 78 + ((resolved.startPhase + phase) % 32) : slot;
                const QVector2D oldOrigin = CTile::atlasTileOrigin(
                    renderedSlot, 8, 8);
                const QVector2D newOrigin = CTile::atlasTileOrigin(
                    renderedSlot, 9, 3);
                const int oldPixelX = qRound(oldOrigin.x() * 16384.0f);
                const int oldPixelY = qRound(oldOrigin.y() * 2048.0f);
                const int newPixelX = qRound(newOrigin.x() * 6144.0f);
                const int newPixelY = qRound(newOrigin.y() * 6144.0f);
                QCOMPARE(oldPixelX % 2048, newPixelX % 2048);
                QCOMPARE(oldPixelY % 2048, newPixelY % 2048);
            }
        }
    int landControlCells = 0;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            const int slot = SMprPackedTile::decode(terrain.packedTile(
                x, y, ETerrainTileLayer::Land)).atlasSlot();
            if(slot >= 78 && slot <= 109)
                continue;
            ++landControlCells;
            const QVector2D oldOrigin = CTile::atlasTileOrigin(slot, 8, 8);
            const QVector2D newOrigin = CTile::atlasTileOrigin(slot, 9, 3);
            QCOMPARE(qRound(oldOrigin.x() * 16384.0f) % 2048,
                     qRound(newOrigin.x() * 6144.0f) % 2048);
            QCOMPARE(qRound(oldOrigin.y() * 2048.0f) % 2048,
                     qRound(newOrigin.y() * 6144.0f) % 2048);
        }
    QVERIFY(animatedCells > 0);
    QCOMPARE(nonAnimatedOverlayCells, 0);
    QVERIFY(landControlCells > 0);
    QVERIFY(overlaySlots.contains(78));
    qInfo() << "Zone11 overlay regrid probe"
            << "tiles" << terrain.tileCountX() << terrain.tileCountY()
            << "activeSlots" << overlaySlots.size()
            << "animatedCells" << animatedCells
            << "landControlCells" << landControlCells;
}

void EnduranceDocumentTests::modernDraftAdoptionStagesPairBeforeLiveMutation()
{
    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(), "Could not locate source/render/view.cpp");
    const int start = source.indexOf("bool CView::adoptCompleteTerrainBaseline(");
    const int end = source.indexOf("bool CView::buildTerrainSemanticFieldMember(",
                                   start);
    QVERIFY(start >= 0 && end > start);
    const QByteArray adoption = source.mid(start, end - start);
    const int binding = adoption.indexOf("findEmbeddedArchive(");
    const int semantic = adoption.indexOf(
        "restoreTerrainSemanticFieldFromArchive(");
    const int firstLiveMutation = adoption.indexOf(
        "m_pLand->previewCanonicalSector(");
    QVERIFY2(binding >= 0 && semantic > binding
                 && firstLiveMutation > semantic,
             "Draft pair/profile/semantic preflight must finish before live renderer mutation");
    QVERIFY2(adoption.contains("bool requireSemanticMember"),
             "Adoption no longer distinguishes an absent optional semantic field");

    const int draftCall = source.indexOf(
        "physicalDraftTiles, atlasPath, draftProfile,");
    QVERIFY(draftCall >= 0);
    const QByteArray draftCallText = source.mid(draftCall, 180);
    QVERIFY2(draftCallText.contains("!semanticFieldMember.name.isEmpty()"),
             "Draft adoption again requires a semantic member that Save did not publish");
}

void EnduranceDocumentTests::externalLegacyAdjacentAtlasProfileCollisionProbe()
{
    const QString mprPath = qEnvironmentVariable(
        "EI_MAPPER_REAL_LEGACY_COLLISION_MPR");
    const QString profilePath = qEnvironmentVariable(
        "EI_MAPPER_REAL_LEGACY_COLLISION_PROFILE");
    const QString atlasPath = qEnvironmentVariable(
        "EI_MAPPER_REAL_LEGACY_COLLISION_ATLAS");
    if(mprPath.isEmpty() || profilePath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set EI_MAPPER_REAL_LEGACY_COLLISION_MPR, EI_MAPPER_REAL_LEGACY_COLLISION_PROFILE, and EI_MAPPER_REAL_LEGACY_COLLISION_ATLAS for the read-only collision probe");
    CValidationReport report;
    CMprDocument document;
    STerrainAtlasProfile profile;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    QVERIFY2(STerrainAtlasProfile::load(profilePath, &profile, &report),
             qPrintable(report.toText()));
    QCOMPARE(profile.provenance,
             QStringLiteral("portable-finalized-modern-atlas-v1"));
    QCOMPARE(QFileInfo(profilePath).canonicalFilePath(),
             QFileInfo(CTerrainAtlasProfileStore::profilePath(
                 document.mapInfo().innerNamespace,
                 QFileInfo(mprPath).absolutePath())).canonicalFilePath());
    QVERIFY(!QFileInfo::exists(
        CTerrainAtlasProfileStore::profilePathForMpr(
            document.mapInfo().innerNamespace, QFileInfo(mprPath))));
    const STerrainAtlasArchiveBindingResult binding =
        CTerrainAtlasArchiveBindingResolver::findExactArchive(
            {atlasPath}, document.mapInfo().innerNamespace,
            int(document.mapInfo().textureCount), profile.atlasSlotCount,
            profile.atlasPixelSha256, atlasPath);
    QVERIFY(!binding.success());
    QVERIFY2(binding.hasCompleteArchive(atlasPath), qPrintable(binding.error));
    const QByteArray actual = binding.completeArchivePixelSha256.value(
        QFileInfo(atlasPath).absoluteFilePath());
    QVERIFY(!actual.isEmpty());
    QVERIFY(actual.toLower() != profile.atlasPixelSha256.toLower());

    // The complete sibling atlas is still sufficient to rebuild an editable
    // working field. A stale legacy JSON must not turn a finalized draft into
    // a view-only map merely because another MPR overwrote that shared file.
    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    CResArchiveDocument atlasArchive;
    QStringList archiveErrors;
    QVERIFY2(atlasArchive.loadBytes(atlasFile.readAll(), atlasPath,
                                    &archiveErrors),
             qPrintable(archiveErrors.join(QStringLiteral("\n"))));
    const int physicalSlotCount = document.mapInfo().tileTypes.size();
    QVERIFY(physicalSlotCount > 0);
    QVERIFY(physicalSlotCount <= int(document.mapInfo().textureCount) * 64);
    QVector<QImage> physicalTiles;
    physicalTiles.reserve(physicalSlotCount);
    for(int slot = 0; slot < physicalSlotCount; ++slot)
    {
        const QString memberName = QStringLiteral("%1%2.mmp")
            .arg(document.mapInfo().innerNamespace.toLower())
            .arg(slot / 64, 3, 10, QLatin1Char('0'));
        const QByteArray pageBytes = atlasArchive.memberPayload(memberName);
        QVERIFY2(!pageBytes.isEmpty(), qPrintable(memberName));
        QImage tile;
        QString diagnostic;
        QVERIFY2(ei::decodeMmpDxt1AtlasTile(
                     pageBytes, slot % 64, &tile, &diagnostic),
                 qPrintable(diagnostic));
        physicalTiles.append(tile.convertToFormat(
            QImage::Format_RGBA8888));
    }
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QVector<STerrainAtlasSlotUsage> usage(physicalSlotCount);
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const int landSlot = SMprPackedTile::decode(terrain.packedTile(
                x, y, ETerrainTileLayer::Land, &available)).atlasSlot();
            if(available && landSlot >= 0 && landSlot < usage.size())
                ++usage[landSlot].landCount;
            const int overlaySlot = SMprPackedTile::decode(terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available)).atlasSlot();
            if(available && overlaySlot >= 0 && overlaySlot < usage.size())
                ++usage[overlaySlot].overlayCount;
        }
    CValidationReport inferenceReport;
    const STerrainAtlasProfile inferred = CTerrainAtlasProfileInference::propose(
        document.mapInfo().innerNamespace, physicalTiles,
        document.mapInfo().tileTypes, usage, &inferenceReport);
    const STerrainAtlasAnalysis inferredAnalysis = inferred.usableAnalysis(
        &inferenceReport);
    QVERIFY2(inferredAnalysis.usable, qPrintable(inferenceReport.toText()));
    QVERIFY2(terrain.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, inferredAnalysis.landCatalog,
                 &inferenceReport,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(inferenceReport.toText()));
    CTerrainVirtualMaterialField field;
    QVERIFY(terrain.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &field, false));
    int unsetVertexCount = 0;
    const qint32 unsetMaterial = std::numeric_limits<qint32>::min();
    for(int vertexY = 0; vertexY <= field.tileHeight(); ++vertexY)
        for(int vertexX = 0; vertexX <= field.tileWidth(); ++vertexX)
            if(field.material(vertexX, vertexY) == unsetMaterial)
                ++unsetVertexCount;
    QVERIFY2(unsetVertexCount == 0,
             qPrintable(QStringLiteral("Recovered field retained %1 unset vertices")
                 .arg(unsetVertexCount)));
    const int centerX = qMax(0, field.tileWidth() / 2);
    const int centerY = qMax(0, field.tileHeight() / 2);
    bool signatureFound = false;
    const STerrainTransitionSignature current = field.cellSignature(
        centerX, centerY, &signatureFound);
    QVERIFY(signatureFound);
    qint32 replacement = 0;
    for(const STerrainAtlasMaterial& material : inferredAnalysis.materials)
        if(material.layer == ETerrainAtlasLayer::Land
           && material.id != current.northWest
           && material.id != current.northEast
           && material.id != current.southEast
           && material.id != current.southWest)
        {
            replacement = material.id;
            break;
        }
    QVERIFY(replacement != 0);
    STerrainTileBrush brush;
    brush.centerX = float(centerX);
    brush.centerY = float(centerY);
    brush.radius = 0.0f;
    brush.shape = ETerrainTileBrushShape::Square;
    CValidationReport paintReport;
    const STerrainTileCommand command = terrain.makeSemanticTilePaint(
        brush, ETerrainTileLayer::Land, replacement,
        inferredAnalysis.landCatalog, 0x52313336u, &paintReport);
    QVERIFY2(!paintReport.hasErrors(), qPrintable(paintReport.toText()));
    QVERIFY(!command.isEmpty());
}

void EnduranceDocumentTests::externalReopenedDraftTopologyProbe()
{
    const QString mprPath = qEnvironmentVariable("EI_MAPPER_REAL_DRAFT_MPR");
    const QString profilePath =
        qEnvironmentVariable("EI_MAPPER_REAL_DRAFT_PROFILE");
    const QString atlasPath =
        qEnvironmentVariable("EI_MAPPER_REAL_DRAFT_ATLAS");
    if(mprPath.isEmpty() || profilePath.isEmpty() || atlasPath.isEmpty())
        QSKIP("Set EI_MAPPER_REAL_DRAFT_MPR, EI_MAPPER_REAL_DRAFT_PROFILE, and EI_MAPPER_REAL_DRAFT_ATLAS for the read-only integration probe");

    CValidationReport report;
    CMprDocument document;
    QVERIFY2(document.load(QFileInfo(mprPath), &report),
             qPrintable(report.toText()));
    STerrainAtlasProfile profile;
    QVERIFY2(STerrainAtlasProfile::load(profilePath, &profile, &report),
             qPrintable(report.toText()));
    const STerrainAtlasAnalysis analysis = profile.usableAnalysis(&report);
    QVERIFY2(analysis.usable, qPrintable(report.toText()));
    CTerrainEditModel terrain;
    QVERIFY2(terrain.load(document, &report), qPrintable(report.toText()));
    QVERIFY2(terrain.initializeVirtualMaterialField(
                 ETerrainTileLayer::Land, analysis.landCatalog, &report,
                 ETerrainMaterialFieldRecovery::RecoverDeterministically),
             qPrintable(report.toText()));
    CTerrainVirtualMaterialField field;
    QVERIFY(terrain.copyVirtualMaterialField(
        ETerrainTileLayer::Land, &field));

    QFile atlasFile(atlasPath);
    QVERIFY(atlasFile.open(QIODevice::ReadOnly));
    CResArchiveDocument atlasArchive;
    QStringList archiveErrors;
    QVERIFY2(atlasArchive.loadBytes(atlasFile.readAll(), atlasPath,
                                    &archiveErrors),
             qPrintable(archiveErrors.join(QStringLiteral("\n"))));
    QVector<QImage> physicalTiles;
    const int textureCount = (profile.atlasSlotCount + 63) / 64;
    for(int page = 0; page < textureCount; ++page)
    {
        const QString memberName = QStringLiteral("%1%2.mmp")
            .arg(document.mapInfo().innerNamespace.toLower())
            .arg(page, 3, 10, QLatin1Char('0'));
        const QByteArray pageBytes = atlasArchive.memberPayload(memberName);
        QVERIFY2(!pageBytes.isEmpty(), qPrintable(memberName));
        for(int localSlot = 0; localSlot < 64; ++localSlot)
        {
            QImage tile;
            QString diagnostic;
            QVERIFY2(ei::decodeMmpDxt1AtlasTile(
                         pageBytes, localSlot, &tile, &diagnostic),
                     qPrintable(diagnostic));
            physicalTiles.append(tile.convertToFormat(
                QImage::Format_RGBA8888));
        }
    }
    QCOMPARE(physicalTiles.size(), textureCount * 64);

    QMap<qint32, qint32> materialRuntimeTypes;
    QMap<qint32, QString> materialNames;
    for(const STerrainAtlasProfileFamily& family : profile.families)
    {
        materialRuntimeTypes.insert(family.id, family.tileType);
        materialNames.insert(family.id, family.name);
    }
    const QVector<qint32> physicalTypes = document.mapInfo().tileTypes;
    CTerrainVirtualAtlas atlas(
        analysis.landCatalog, physicalTiles, 0x52313332u, {}, {}, true,
        physicalTypes, materialRuntimeTypes);
    QVERIFY2(atlas.isValid(), qPrintable(atlas.error()));

    QVector<quint16> pinnedOverlay;
    QVector<qint32> placedCellTypes;
    QVector<quint16> placedPackedCells;
    QVector<bool> preservePlacedCells;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool landAvailable = false;
            const quint16 landPacked = terrain.packedTile(
                x, y, ETerrainTileLayer::Land, &landAvailable);
            bool typeFound = false;
            const qint32 type = landAvailable
                ? terrain.tileType(landPacked, &typeFound) : -1;
            placedCellTypes.append(typeFound ? type : -1);
            placedPackedCells.append(landAvailable ? landPacked : quint16(0));
            preservePlacedCells.append(true);

            bool overlayAvailable = false;
            const quint16 overlayPacked = terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &overlayAvailable);
            if(overlayAvailable && overlayPacked != 0xffffu)
                pinnedOverlay.append(overlayPacked);
        }
    STerrainAtlasFinalization finalized = atlas.finalize(
        field, document.mapInfo().innerNamespace,
        kMprPackedTerrainAtlasSlotLimit, physicalTypes, &report,
        pinnedOverlay, placedCellTypes, placedPackedCells, {},
        &analysis.overlayCompatibleCatalog, preservePlacedCells);
    QVERIFY2(finalized.success,
             qPrintable(finalized.error + QStringLiteral("\n")
                        + report.toText()));
    CTerrainWorkingAtlas workingAtlas(
        analysis.overlayCompatibleCatalog, physicalTiles, 0x52313332u,
        kMprPackedTerrainAtlasSlotLimit, {}, {}, physicalTypes,
        materialRuntimeTypes);
    STerrainWorkingFinalizationOrigins origins;
    QVERIFY2(workingAtlas.restoreFinalizedProvenance(
                 &finalized, &origins,
                 document.mapInfo().innerNamespace, field.stableSha256(),
                 pinnedOverlay, &report), qPrintable(report.toText()));
    QVector<QImage> finalizedPhysicalTiles;
    QVERIFY2(CTerrainVirtualAtlas::decodePhysicalTileImages(
                 finalized, &finalizedPhysicalTiles, &report),
             qPrintable(report.toText()));
    const STerrainAtlasProfile publishedProfile =
        CTerrainAtlasProfileStore::portableFinalizedProfile(
            finalized, finalizedPhysicalTiles, profile,
            ETerrainAtlasLayer::Land, materialNames, materialRuntimeTypes,
            &report);
    QCOMPARE(publishedProfile.assignedSlotCount(), finalized.requiredSlots);
    const STerrainAtlasAnalysis publishedAnalysis =
        publishedProfile.usableAnalysis(&report);
    QVERIFY2(publishedAnalysis.usable, qPrintable(report.toText()));
    const STerrainPortableAtlasPackage package =
        CTerrainVirtualAtlas::buildPortablePackage(
            document, terrain, finalized, ETerrainTileLayer::Land, &report);
    QVERIFY2(package.success,
             qPrintable(package.error + QStringLiteral("\n")
                        + report.toText()));
    CMprDocument reopened;
    QVERIFY2(reopened.loadBytes(package.mprBytes,
                                QStringLiteral("zone3obr-published-probe.mpr"),
                                &report), qPrintable(report.toText()));
    QCOMPARE(reopened.mapInfo().innerNamespace,
             finalized.isolatedNamespace);
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
    QVERIFY(!sculpt.affectsTerrainShadowField);
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

    // Shipped maps can retain two active overlay sheets that touch the same
    // SEC-boundary coordinate with different heights or offsets. Sculpt the
    // sheet beneath the brush instead of rejecting the complete stamp or
    // silently welding the unrelated sheet to it.
    SOverlaySurfaceCommand introduceConflict;
    introduceConflict.description = QStringLiteral("synthetic overlay-sheet conflict");
    SOverlaySectorDelta conflictDelta;
    conflictDelta.sectorX = 1;
    conflictDelta.sectorY = 0;
    conflictDelta.beforePayload = terrain.sectorPayload(1, 0);
    conflictDelta.afterPayload = conflictDelta.beforePayload;
    const quint16 rightBefore = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(conflictDelta.afterPayload.constData()
                                       + rightVertex + 2));
    qToLittleEndian<quint16>(quint16(rightBefore + 1000),
        reinterpret_cast<uchar*>(conflictDelta.afterPayload.data()
                                 + rightVertex + 2));
    introduceConflict.sectors.append(conflictDelta);
    QVERIFY(terrain.apply(introduceConflict, true, &report));

    brush.centerX = 31.5f; // Prefer the left connected sheet at the boundary.
    CValidationReport conflictSculptReport;
    const SOverlaySurfaceCommand conflictSculpt = terrain.makeOverlayRaiseLower(
        brush, 200.0f, &conflictSculptReport, true);
    QVERIFY2(!conflictSculpt.isEmpty(), qPrintable(conflictSculptReport.toText()));
    QVERIFY2(!conflictSculptReport.hasErrors(),
             qPrintable(conflictSculptReport.toText()));
    QVERIFY(terrain.apply(conflictSculpt, true, &conflictSculptReport));
    const QByteArray conflictLeft = terrain.sectorPayload(0, 0);
    const QByteArray conflictRight = terrain.sectorPayload(1, 0);
    const quint16 leftAfter = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(conflictLeft.constData()
                                       + leftVertex + 2));
    const quint16 rightAfter = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(conflictRight.constData()
                                       + rightVertex + 2));
    QCOMPARE(leftAfter, quint16(rightBefore + 200));
    QCOMPARE(rightAfter, quint16(rightBefore + 1000));
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

void EnduranceDocumentTests::deferredLandscapeVisibilityStopsBeforeViewTeardown()
{
    const QByteArray source = mainWindowSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/app/main_window.cpp from the test worktree");

    const int applyStart = source.indexOf("const auto applyLandscapeVisibility =");
    const int dockConnection = source.indexOf("connect(m_landscapeToolsDock",
                                               applyStart);
    QVERIFY2(applyStart >= 0 && dockConnection > applyStart,
             "Landscape Tools visibility callback was not found");
    const QByteArray apply = source.mid(applyStart, dockConnection - applyStart);

    // The callback must operate through a QObject lifetime tracker rather than
    // reaching through MainWindow's raw view pointer after shutdown has started.
    QVERIFY2(apply.contains("if(!view)") && apply.contains("return;"),
             "Landscape visibility application has no live-view guard");
    QVERIFY2(!apply.contains("m_pView->"),
             "Landscape visibility application still dereferences m_pView directly");
    QVERIFY(apply.contains("view->setDrawLand(true);"));
    QVERIFY(apply.contains("view->setDrawWater(true);"));
    QVERIFY(apply.contains("view->setLandscapeEditingEnabled(visible);"));

    const int deferredTimer = source.indexOf("QTimer::singleShot(0, this",
                                              dockConnection);
    QVERIFY2(deferredTimer > dockConnection,
             "Landscape hidden transition is no longer deferred");
    const int viewCapture = source.indexOf("QPointer<CView> view(m_pView)",
                                            dockConnection);
    QVERIFY2(viewCapture >= dockConnection && viewCapture < deferredTimer,
             "Deferred Landscape Tools transition does not capture CView lifetime");
    const int deferredEnd = source.indexOf("});", deferredTimer);
    QVERIFY(deferredEnd > deferredTimer);
    const QByteArray deferred = source.mid(deferredTimer, deferredEnd - deferredTimer);
    QVERIFY2(deferred.contains("!view"),
             "Deferred Landscape Tools callback can still run after CView deletion");
    QVERIFY(deferred.contains("visibilityGeneration != m_landscapeVisibilityGeneration"));

    const int closeEvent = source.indexOf("void MainWindow::closeEvent");
    const int deleteView = source.indexOf("delete m_pView;", closeEvent);
    const int invalidation = source.indexOf("++m_landscapeVisibilityGeneration;",
                                            closeEvent);
    QVERIFY2(closeEvent >= 0 && deleteView > closeEvent,
             "MainWindow shutdown view deletion was not found");
    QVERIFY2(invalidation >= closeEvent && invalidation < deleteView,
             "Shutdown does not invalidate a queued Landscape Tools transition before CView deletion");

    // The production callback uses these two independent gates: a still-live
    // QObject and the generation that was current when the hidden transition
    // was queued. The document target does not link the full OpenGL MainWindow,
    // but it can still prove that QPointer clears on teardown and either gate
    // denies the pending transition.
    const auto queuedTransitionMayApply = [](quint64 expectedGeneration,
                                             quint64 activeGeneration,
                                             const QPointer<QObject>& view)
    {
        return expectedGeneration == activeGeneration && view;
    };
    quint64 currentGeneration = 1;
    QPointer<QObject> liveView(new QObject);
    const quint64 liveGeneration = currentGeneration;
    QVERIFY(queuedTransitionMayApply(liveGeneration, currentGeneration, liveView));

    QPointer<QObject> destroyedView(new QObject);
    const quint64 destroyedViewGeneration = currentGeneration;
    delete destroyedView.data();
    QVERIFY(destroyedView.isNull());
    QVERIFY(!queuedTransitionMayApply(destroyedViewGeneration,
                                      currentGeneration, destroyedView));

    QPointer<QObject> invalidatedView(new QObject);
    const quint64 invalidatedGeneration = currentGeneration;
    ++currentGeneration;
    QVERIFY(!queuedTransitionMayApply(invalidatedGeneration,
                                      currentGeneration, invalidatedView));
    delete liveView.data();
    delete invalidatedView.data();
}

void EnduranceDocumentTests::floatingResourceBrowserClosesBeforeViewTeardown()
{
    const QByteArray source = mainWindowSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/app/main_window.cpp from the test worktree");

    const int closeEvent = source.indexOf("void MainWindow::closeEvent");
    const int secondarySweep = source.indexOf("ei::closeSecondaryWindows(this);",
                                               closeEvent);
    const int deleteView = source.indexOf("delete m_pView;", closeEvent);
    QVERIFY2(closeEvent >= 0 && secondarySweep > closeEvent && deleteView > secondarySweep,
             "MainWindow shutdown ordering was not found");

    const QByteArray earlyShutdown = source.mid(closeEvent,
                                                 secondarySweep - closeEvent);
    QVERIFY2(earlyShutdown.contains(
                 "ei::closeFloatingDockHost(m_resourceBrowserDock);"),
             "Resource Browser floating host is not explicitly closed before the generic "
             "top-level sweep and synchronous viewport teardown");
}

void EnduranceDocumentTests::terrainCatalogWaitsForVisibleLandscapeFrame()
{
    QCOMPARE(terrainCatalogLoadAction(
                 true, false, false, false, false),
             ETerrainCatalogLoadAction::None);
    QCOMPARE(terrainCatalogLoadAction(
                 true, false, false, true, false),
             ETerrainCatalogLoadAction::QueueAfterVisibleFrame);
    QCOMPARE(terrainCatalogLoadAction(
                 true, true, false, true, false),
             ETerrainCatalogLoadAction::None);
    QCOMPARE(terrainCatalogLoadAction(
                 true, false, false, false, true),
             ETerrainCatalogLoadAction::BuildSynchronously);
    QCOMPARE(terrainCatalogLoadAction(
                 false, false, false, true, true),
             ETerrainCatalogLoadAction::None);
    QCOMPARE(terrainCatalogLoadAction(
                 true, false, true, true, true),
             ETerrainCatalogLoadAction::None);

    const QByteArray source = viewSource();
    QVERIFY2(!source.isEmpty(),
             "Could not locate source/render/view.cpp from the test worktree");
    const int loadStart = source.indexOf("void CView::loadLandscape");
    const int deferStart = source.indexOf(
        "void CView::deferTerrainAutoCatalogBuild", loadStart);
    QVERIFY(loadStart >= 0 && deferStart > loadStart);
    const QByteArray load = source.mid(loadStart, deferStart - loadStart);
    QVERIFY2(load.contains("deferTerrainAutoCatalogBuild(filePath)"),
             "MPR loading no longer marks paint-only catalog work as deferred");
    QVERIFY2(!load.contains("rebuildTerrainAutoCatalog()"),
             "MPR loading again blocks the first landscape frame on paint catalog construction");
    QVERIFY2(!load.contains("restoreTerrainSemanticFieldFromArchive("),
             "MPR loading again restores paint-only semantic state before the first landscape frame");
    QVERIFY2(!load.contains("m_terrainEdit.load("),
             "MPR loading again constructs canonical paint state before the first landscape frame");
    QVERIFY2(!load.contains("m_pTileForm->adoptTileImages("),
             "MPR loading again populates the tile picker before the first landscape frame");
    QVERIFY2(!load.contains("m_pTileForm->fillTable("),
             "MPR loading again decodes the tile picker before the first landscape frame");

    const int paintStart = source.indexOf("void CView::paintGL");
    const int shutdownStart = source.indexOf(
        "bool CView::prepareForApplicationShutdown", paintStart);
    QVERIFY(paintStart >= 0 && shutdownStart > paintStart);
    const QByteArray paint = source.mid(paintStart, shutdownStart - paintStart);
    int draw = paint.indexOf("draw();");
    if(draw < 0)
        draw = paint.indexOf(
            "draw(traceSceneFrame ? &frameTelemetry : nullptr);");
    const int overlays = paint.indexOf("drawTestOverlaysOpenGl();", draw);
    const int queue = paint.indexOf("queueDeferredTerrainAutoCatalogBuild();",
                                    overlays);
    QVERIFY2(draw >= 0 && overlays > draw && queue > overlays,
             "Paint catalog work is not queued after the complete visible landscape frame");

    const int manualSelectionStart = source.indexOf(
        "void CView::ensureManualTileSelection");
    const int readStateStart = source.indexOf(
        "void CView::updateReadState", manualSelectionStart);
    QVERIFY(manualSelectionStart >= 0 && readStateStart > manualSelectionStart);
    const QByteArray manualSelection = source.mid(
        manualSelectionStart, readStateStart - manualSelectionStart);
    const int pendingGate = manualSelection.indexOf(
        "if(m_terrainAutoCatalogPending)");
    const int retainedSelection = manualSelection.indexOf(
        "m_pTileForm->getSelectedTile");
    QVERIFY2(pendingGate >= 0 && retainedSelection > pendingGate,
             "A retained Manual selection can force paint catalog hydration before the first landscape frame");

    const int previewStart = source.indexOf("void CView::updatePreviewTile");
    const int nextMethod = source.indexOf(
        "void CView::addTileRotation", previewStart);
    QVERIFY(previewStart >= 0 && nextMethod > previewStart);
    const QByteArray preview = source.mid(previewStart,
                                          nextMethod - previewStart);
    const int previewPendingGate = preview.indexOf(
        "if(m_terrainAutoCatalogPending)");
    const int previewEnsure = preview.indexOf(
        "ensureTerrainAutoCatalogReady()", previewPendingGate);
    QVERIFY2(previewPendingGate >= 0 && previewEnsure > previewPendingGate,
             "Queued hover traffic can force paint catalog hydration before the first landscape frame");
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

void EnduranceDocumentTests::terrainSemanticFieldRoundTripsWithoutPlacementHistory()
{
    STerrainSemanticFieldDocument source;
    source.mprNamespace = QStringLiteral("zone2");
    source.mprSha256 = QCryptographicHash::hash(
        QByteArrayLiteral("mpr-candidate"), QCryptographicHash::Sha256);
    source.atlasPixelSha256 = QCryptographicHash::hash(
        QByteArrayLiteral("atlas-pixels"), QCryptographicHash::Sha256);
    source.profileSha256 = QCryptographicHash::hash(
        QByteArrayLiteral("semantic-profile"), QCryptographicHash::Sha256);
    source.vertexWidth = 3;
    source.vertexHeight = 2;
    source.land = {0x40000101, 0x40000101, -1,
                   0x40000102, 0x40000103, 0x40000103};
    // An absent layer remains absent. Reload must not manufacture a complete
    // all-unset field that would suppress deterministic initialization later.
    source.overlay.clear();

    CValidationReport writeReport;
    const QByteArray bytes = source.toBytes(&writeReport);
    QVERIFY2(!bytes.isEmpty(), qPrintable(writeReport.toText()));
    QVERIFY(!bytes.contains("placement"));
    QVERIFY(!bytes.contains("owner"));
    QVERIFY(!bytes.contains("timestamp"));

    STerrainSemanticFieldDocument reopened;
    CValidationReport readReport;
    QVERIFY2(STerrainSemanticFieldDocument::fromBytes(
        bytes, &reopened, &readReport, QStringLiteral("test.res:field")),
        qPrintable(readReport.toText()));
    QCOMPARE(reopened.mprNamespace, source.mprNamespace);
    QCOMPARE(reopened.mprSha256, source.mprSha256);
    QCOMPARE(reopened.atlasPixelSha256, source.atlasPixelSha256);
    QCOMPARE(reopened.profileSha256, source.profileSha256);
    QCOMPARE(reopened.vertexWidth, source.vertexWidth);
    QCOMPARE(reopened.vertexHeight, source.vertexHeight);
    QCOMPARE(reopened.land, source.land);
    QCOMPARE(reopened.overlay, source.overlay);
    QVERIFY(reopened.validateBinding(
        source.mprNamespace, source.mprSha256, source.atlasPixelSha256,
        source.profileSha256,
        source.vertexWidth, source.vertexHeight, &readReport));

    const QString first = STerrainSemanticFieldDocument::memberName(
        source.mprNamespace, QFileInfo(QStringLiteral("C:/Maps/zone2 edit.mpr")));
    const QString second = STerrainSemanticFieldDocument::memberName(
        source.mprNamespace, QFileInfo(QStringLiteral("D:/Elsewhere/zone2 edit.mpr")));
    QCOMPARE(first, second);
    QVERIFY(first.startsWith(QStringLiteral("ei_terrain_semantics_zone2_")));
    QVERIFY(first.endsWith(QStringLiteral(".json")));
}

void EnduranceDocumentTests::terrainSaveRouteDoesNotHashArtworkPerMapCell()
{
    const QByteArray source = viewSource();
    QVERIFY(!source.isEmpty());
    const int methodStart = source.indexOf(
        "bool CView::terrainLayerUsesWorkingExtension(");
    const int methodEnd = source.indexOf(
        "bool CView::realizeWorkingTerrainSignature(", methodStart);
    QVERIFY(methodStart >= 0);
    QVERIFY(methodEnd > methodStart);
    const QByteArray method = source.mid(methodStart, methodEnd - methodStart);
    QVERIFY2(method.contains("tileOriginForSlot("),
             "The save-route cell scan no longer uses the constant-time origin lookup");
    QVERIFY2(!method.contains("tileIdentityForSlot("),
             "The save-route cell scan hashes full tile artwork for each MPR cell");
}

void EnduranceDocumentTests::terrainSemanticFieldRejectsWrongMprOrAtlasBinding()
{
    STerrainSemanticFieldDocument source;
    source.mprNamespace = QStringLiteral("zone2");
    source.mprSha256 = QCryptographicHash::hash(
        QByteArrayLiteral("mpr-a"), QCryptographicHash::Sha256);
    source.atlasPixelSha256 = QCryptographicHash::hash(
        QByteArrayLiteral("atlas-a"), QCryptographicHash::Sha256);
    source.profileSha256 = QCryptographicHash::hash(
        QByteArrayLiteral("profile-a"), QCryptographicHash::Sha256);
    source.vertexWidth = 2;
    source.vertexHeight = 2;
    source.land = {1, 2, 3, 4};
    source.overlay = {-1, -1, -1, -1};

    CValidationReport report;
    QVERIFY(!source.validateBinding(
        source.mprNamespace,
        QCryptographicHash::hash(QByteArrayLiteral("mpr-b"),
                                 QCryptographicHash::Sha256),
        source.atlasPixelSha256, source.profileSha256, 2, 2, &report));
    QVERIFY(report.hasErrors());

    QByteArray bytes = source.toBytes();
    QVERIFY(!bytes.isEmpty());
    QJsonObject corruptedRoot = QJsonDocument::fromJson(bytes).object();
    QJsonObject corruptedLand = corruptedRoot.value(
        QStringLiteral("land")).toObject();
    corruptedLand.insert(QStringLiteral("sha256"),
                         QString(64, QLatin1Char('0')));
    corruptedRoot.insert(QStringLiteral("land"), corruptedLand);
    bytes = QJsonDocument(corruptedRoot).toJson(QJsonDocument::Compact);
    STerrainSemanticFieldDocument corrupt;
    CValidationReport corruptReport;
    QVERIFY(!STerrainSemanticFieldDocument::fromBytes(
        bytes, &corrupt, &corruptReport, QStringLiteral("corrupt")));
    QVERIFY(corruptReport.hasErrors());
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

void EnduranceDocumentTests::emptyGeneratedMobCanAcceptFirstRawObject()
{
    const QByteArray range = section(0xFF02,
        section(0xFF05, u32(1000)) + section(0xFF06, u32(1999)));
    const QByteArray unknownTopLevel = section(0x31415926, QByteArray(32, '\x5a'));
    const QByteArray legacyEmpty = validMob(
        section(0xD000) + section(0xFF00, range)) + unknownTopLevel;

    CMobDocument document;
    CValidationReport report;
    QVERIFY2(document.loadBytes(legacyEmpty, "legacy-empty-generated.mob", &report),
             qPrintable(report.toText()));
    QVERIFY(document.objects().isEmpty());
    QVERIFY(!document.hasSection(0xB000));

    QByteArray upgraded;
    QVERIFY2(document.ensureEmptyObjectSection(&upgraded, &report),
             qPrintable(report.toText()));
    QCOMPARE(upgraded, validMob(section(0xD000) + section(0xFF00, range)
                                + section(0xB000)) + unknownTopLevel);

    CMobDocument reopened;
    QVERIFY2(reopened.loadBytes(upgraded, "upgraded-empty-generated.mob", &report),
             qPrintable(report.toText()));
    QVERIFY(reopened.hasSection(0xB000));
    QCOMPARE(reopened.objects().size(), 0);

    const QByteArray donor = object(334, "tree");
    const QByteArray donorHash = QCryptographicHash::hash(
        donor, QCryptographicHash::Sha256);
    QByteArray withFirstObject;
    QVERIFY2(reopened.rebuildObjectSequenceFromRaw(
        QVector<QByteArray>() << donor, QVector<QByteArray>() << donorHash,
        &withFirstObject, &report), qPrintable(report.toText()));
    CMobDocument finalDocument;
    QVERIFY2(finalDocument.loadBytes(withFirstObject, "first-object.mob", &report),
             qPrintable(report.toText()));
    QCOMPARE(finalDocument.objects().size(), 1);
    QCOMPARE(finalDocument.objects().first().nid, quint32(334));
    QCOMPARE(withFirstObject.right(unknownTopLevel.size()), unknownTopLevel);
}

void EnduranceDocumentTests::generatedMobCommitIsValidatedBackedUpAndAtomic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QFileInfo destination(dir.filePath("new-autozone.mob"));
    const QFileInfo backupRoot(dir.filePath("backups"));

    const QByteArray range = section(0xFF02,
        section(0xFF05, u32(1000)) + section(0xFF06, u32(1999)));
    const QByteArray incompleteCandidate = validMob(
        section(0xD000) + section(0xFF00, range));
    const QByteArray candidate = validMob(
        section(0xD000) + section(0xFF00, range) + section(0xB000));

    SMobSaveCommitRequest request;
    request.destination = destination;
    request.backupRootOverride = backupRoot.absoluteFilePath();
    const SMobSaveCommitResult incomplete = commitGeneratedMob(
        incompleteCandidate, request);
    QVERIFY(!incomplete.success);
    QVERIFY(incomplete.error.contains(QStringLiteral("OBJECT_SECTION")));
    QVERIFY(!destination.exists());
    const SMobSaveCommitResult created = commitGeneratedMob(candidate, request);
    QVERIFY2(created.success, qPrintable(created.error));
    QCOMPARE(created.phase, QStringLiteral("complete"));
    QVERIFY(created.backupPath.isEmpty());

    CMobDocument reopened;
    CValidationReport report;
    QVERIFY(reopened.load(destination, &report));
    QVERIFY2(!report.hasErrors(), qPrintable(report.toText()));
    QVERIFY(reopened.isQuestMob());
    QCOMPARE(reopened.objects().size(), 0);
    QVERIFY(reopened.hasSection(0xB000));
    QCOMPARE(reopened.idRanges().size(), 1);
    QVERIFY(!reopened.hasEmbeddedAiGraph());

    const QByteArray replacement = validMob(
        section(0xD000) + section(0xFF00,
            section(0xFF02, section(0xFF05, u32(2000))
                              + section(0xFF06, u32(2999))))
            + section(0xB000));
    const SMobSaveCommitResult replaced = commitGeneratedMob(replacement, request);
    QVERIFY2(replaced.success, qPrintable(replaced.error));
    QVERIFY(!replaced.backupPath.isEmpty());
    QVERIFY(QFileInfo::exists(replaced.backupPath));

#ifdef QT_TESTLIB_LIB
    request.simulateAtomicCommitFailure = true;
    QFile current(destination.absoluteFilePath());
    QVERIFY(current.open(QIODevice::ReadOnly));
    const QByteArray committedBeforeFailure = current.readAll();
    current.close();
    const SMobSaveCommitResult failed = commitGeneratedMob(candidate, request);
    QVERIFY(!failed.success);
    QFile after(destination.absoluteFilePath());
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), committedBeforeFailure);
#endif
}

QTEST_APPLESS_MAIN(EnduranceDocumentTests)
#include "endurance_document_tests.moc"

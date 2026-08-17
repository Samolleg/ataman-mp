#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

#include "terrain_resolve_audit.h"

namespace {
STerrainTileDelta delta(int x, int y, ETerrainTileLayer layer,
                        quint16 before, quint16 after)
{
    STerrainTileDelta result;
    result.globalX = x;
    result.globalY = y;
    result.layer = layer;
    result.before = before;
    result.after = after;
    result.hasSemanticRequest = true;
    result.requestedSignature.northWest = 1;
    result.requestedSignature.northEast = 2;
    result.requestedSignature.southEast = 2;
    result.requestedSignature.southWest = 1;
    return result;
}

STerrainResolveAuditSession session(quint64 actionId)
{
    STerrainResolveAuditSession result;
    result.actionId = actionId;
    result.trigger = ETerrainResolveAuditTrigger::ShortcutShiftR;
    result.outcome = QStringLiteral("committed");
    result.mprNamespace = QStringLiteral("zone2");
    result.sourceMprSha256 = QByteArrayLiteral("source-mpr-sha");
    result.atlasPixelSha256 = QByteArrayLiteral("atlas-pixel-sha");
    result.landFieldBefore.present = false;
    result.landFieldAfter.present = true;
    result.landFieldAfter.complete = true;
    result.landFieldAfter.sha256 = QByteArrayLiteral("land-after");
    result.overlayFieldBefore.present = true;
    result.overlayFieldBefore.complete = true;
    result.overlayFieldBefore.sha256 = QByteArrayLiteral("overlay-before");
    result.overlayFieldAfter = result.overlayFieldBefore;
    result.stagedTupleCount = 2;
    result.stagedCellCount = 3;
    result.generatedTupleCount = 1;
    result.skippedCellCount = 4;
    result.undoCommandId = QStringLiteral("resolve-undo-%1").arg(actionId);
    result.undoCommandDescription = QStringLiteral("Resolve missing terrain blends");
    result.undoStackIndexBefore = 4;
    result.undoStackCountBefore = 5;
    result.undoStackIndexAfter = 5;
    result.undoStackCountAfter = 6;
    result.packedDeltas << delta(8, 9, ETerrainTileLayer::Land, 0x0010, 0x0020)
                        << delta(1, 2, ETerrainTileLayer::Overlay, 0x0003, 0x0004)
                        << delta(2, 2, ETerrainTileLayer::Land, 0x0005, 0x0006);
    STerrainResolveRuntimeDecision runtime;
    runtime.signature = result.packedDeltas.front().requestedSignature;
    runtime.predecessorRuntimeType = 3;
    runtime.selectedRuntimeType = 3;
    runtime.selectedWorkingSlot = 44;
    runtime.origin = QStringLiteral("canonical-generated");
    runtime.blendProvenance = QStringLiteral("canonical-generated:mask");
    runtime.cellCount = 2;
    result.runtimeDecisions.append(runtime);
    STerrainResolveSkippedReason skipped;
    skipped.reason = QStringLiteral("ambiguous_shared_vertex");
    skipped.count = 4;
    skipped.sampleCells << QPoint(9, 9) << QPoint(10, 9) << QPoint(11, 9);
    result.skippedReasons.append(skipped);
    return result;
}
}

class TerrainResolveAuditTests : public QObject
{
    Q_OBJECT

private slots:
    void sessionJsonIsBoundedAndContainsRequiredResolverEvidence();
    void journalRetentionKeepsNewestTerminalSessions();
};

void TerrainResolveAuditTests::sessionJsonIsBoundedAndContainsRequiredResolverEvidence()
{
    const STerrainResolveAuditSession value = session(17);
    const QJsonObject json = terrainResolveAuditSessionJson(value, 2, 2);

    QCOMPARE(json.value(QStringLiteral("schema")).toString(),
             QStringLiteral("ei-mapper-endurance-terrain-resolve-session"));
    QCOMPARE(json.value(QStringLiteral("actionId")).toString(),
             QStringLiteral("17"));
    QCOMPARE(json.value(QStringLiteral("trigger")).toString(),
             QStringLiteral("shortcut-shift-r"));
    QCOMPARE(json.value(QStringLiteral("outcome")).toString(),
             QStringLiteral("committed"));
    const QJsonObject fields = json.value(QStringLiteral("semanticFields")).toObject();
    QVERIFY(!fields.value(QStringLiteral("landBefore")).toObject()
                 .value(QStringLiteral("present")).toBool());
    QCOMPARE(fields.value(QStringLiteral("landAfter")).toObject()
                 .value(QStringLiteral("sha256")).toString(),
             QStringLiteral("land-after"));
    QCOMPARE(json.value(QStringLiteral("stagedTupleCount")).toInt(), 2);
    QCOMPARE(json.value(QStringLiteral("changedCellCount")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("skippedCellCount")).toInt(), 4);
    QVERIFY(!json.value(QStringLiteral("packedDeltaSha256")).toString().isEmpty());
    QCOMPARE(json.value(QStringLiteral("packedDeltaExamples")).toArray().size(), 2);
    QVERIFY(json.value(QStringLiteral("packedDeltaExamplesTruncated")).toBool());
    QCOMPARE(json.value(QStringLiteral("runtimeTypeDecisions")).toArray()
                 .at(0).toObject().value(QStringLiteral("predecessorRuntimeType"))
                 .toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("runtimeTypeDecisions")).toArray()
                 .at(0).toObject().value(QStringLiteral("origin")).toString(),
             QStringLiteral("canonical-generated"));
    const QJsonObject skipped = json.value(QStringLiteral("skippedReasons"))
        .toArray().at(0).toObject();
    QCOMPARE(skipped.value(QStringLiteral("count")).toInt(), 4);
    QCOMPARE(skipped.value(QStringLiteral("sampleCells")).toArray().size(), 2);
    QCOMPARE(json.value(QStringLiteral("undo")).toObject()
                 .value(QStringLiteral("commandId")).toString(),
             QStringLiteral("resolve-undo-17"));
    QCOMPARE(json.value(QStringLiteral("undo")).toObject()
                 .value(QStringLiteral("description")).toString(),
             QStringLiteral("Resolve missing terrain blends"));

    STerrainResolveAuditSession reordered = value;
    std::reverse(reordered.packedDeltas.begin(), reordered.packedDeltas.end());
    QCOMPARE(terrainResolvePackedDeltaSha256(value.packedDeltas),
             terrainResolvePackedDeltaSha256(reordered.packedDeltas));

    STerrainResolveAuditSession bounded = value;
    for(int index = 0; index < 128; ++index)
        bounded.runtimeDecisions.append(value.runtimeDecisions.front());
    for(int index = 0; index < 65; ++index)
        bounded.validationFindings.append(QStringLiteral("finding-%1").arg(index));
    const QJsonObject boundedJson = terrainResolveAuditSessionJson(bounded, 2, 2);
    QCOMPARE(boundedJson.value(QStringLiteral("runtimeTypeDecisionCount")).toInt(),
             129);
    QCOMPARE(boundedJson.value(QStringLiteral("runtimeTypeDecisions"))
                 .toArray().size(), 2);
    QVERIFY(boundedJson.value(
        QStringLiteral("runtimeTypeDecisionsTruncated")).toBool());
    QCOMPARE(boundedJson.value(QStringLiteral("validationFindingCount")).toInt(),
             65);
    QCOMPARE(boundedJson.value(QStringLiteral("validationFindings"))
                 .toArray().size(), 64);
    QVERIFY(boundedJson.value(
        QStringLiteral("validationFindingsTruncated")).toBool());
}

void TerrainResolveAuditTests::journalRetentionKeepsNewestTerminalSessions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("resolve-sessions.json"));
    QString error;
    QVERIFY2(appendTerrainResolveAuditSession(path, session(1), 2, 8, 2, &error),
             qPrintable(error));
    QVERIFY2(appendTerrainResolveAuditSession(path, session(2), 2, 8, 2, &error),
             qPrintable(error));
    QVERIFY2(appendTerrainResolveAuditSession(path, session(3), 2, 8, 2, &error),
             qPrintable(error));

    QFile journal(path);
    QVERIFY(journal.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(journal.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("schema")).toString(),
             QStringLiteral("ei-mapper-endurance-terrain-resolve-journal"));
    const QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();
    QCOMPARE(sessions.size(), 2);
    QCOMPARE(sessions.at(0).toObject().value(QStringLiteral("actionId")).toString(),
             QStringLiteral("2"));
    QCOMPARE(sessions.at(1).toObject().value(QStringLiteral("actionId")).toString(),
             QStringLiteral("3"));
}

QTEST_MAIN(TerrainResolveAuditTests)
#include "terrain_resolve_audit_tests.moc"

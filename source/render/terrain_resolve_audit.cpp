#include "terrain_resolve_audit.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegExp>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>

#include "mpr_document.h"

namespace {
int layerOrder(ETerrainTileLayer layer)
{
    return layer == ETerrainTileLayer::Land ? 0 : 1;
}

bool deltaLess(const STerrainTileDelta& left, const STerrainTileDelta& right)
{
    if(layerOrder(left.layer) != layerOrder(right.layer))
        return layerOrder(left.layer) < layerOrder(right.layer);
    if(left.globalY != right.globalY)
        return left.globalY < right.globalY;
    if(left.globalX != right.globalX)
        return left.globalX < right.globalX;
    if(left.before != right.before)
        return left.before < right.before;
    return left.after < right.after;
}

QVector<STerrainTileDelta> canonicalDeltas(const QVector<STerrainTileDelta>& deltas)
{
    QVector<STerrainTileDelta> result = deltas;
    std::sort(result.begin(), result.end(), deltaLess);
    return result;
}

void addInt(QCryptographicHash* hash, qint32 value)
{
    char bytes[sizeof(value)];
    qToLittleEndian<qint32>(value, reinterpret_cast<uchar*>(bytes));
    hash->addData(bytes, sizeof(bytes));
}

QJsonObject signatureJson(const STerrainTransitionSignature& signature)
{
    QJsonObject result;
    result.insert(QStringLiteral("northWest"), signature.northWest);
    result.insert(QStringLiteral("northEast"), signature.northEast);
    result.insert(QStringLiteral("southEast"), signature.southEast);
    result.insert(QStringLiteral("southWest"), signature.southWest);
    return result;
}

QJsonObject packedJson(quint16 packed)
{
    const SMprPackedTile decoded = SMprPackedTile::decode(packed);
    QJsonObject result;
    result.insert(QStringLiteral("value"), int(packed));
    result.insert(QStringLiteral("slot"), decoded.atlasSlot());
    result.insert(QStringLiteral("rotation"), int(decoded.rotation));
    return result;
}

QJsonObject deltaJson(const STerrainTileDelta& delta)
{
    QJsonObject result;
    result.insert(QStringLiteral("x"), delta.globalX);
    result.insert(QStringLiteral("y"), delta.globalY);
    result.insert(QStringLiteral("layer"),
                  delta.layer == ETerrainTileLayer::Land
                      ? QStringLiteral("land") : QStringLiteral("overlay"));
    result.insert(QStringLiteral("before"), packedJson(delta.before));
    result.insert(QStringLiteral("after"), packedJson(delta.after));
    if(delta.hasSemanticRequest)
        result.insert(QStringLiteral("requestedCorners"),
                      signatureJson(delta.requestedSignature));
    return result;
}

QJsonObject fieldJson(const STerrainResolveSemanticFieldAudit& field)
{
    QJsonObject result;
    result.insert(QStringLiteral("present"), field.present);
    result.insert(QStringLiteral("complete"), field.complete);
    result.insert(QStringLiteral("active"), field.active);
    result.insert(QStringLiteral("sha256"), QString::fromLatin1(field.sha256));
    return result;
}

QJsonObject pointJson(const QPoint& point)
{
    QJsonObject result;
    result.insert(QStringLiteral("x"), point.x());
    result.insert(QStringLiteral("y"), point.y());
    return result;
}
}

QString terrainResolveAuditTriggerName(ETerrainResolveAuditTrigger trigger)
{
    switch(trigger)
    {
    case ETerrainResolveAuditTrigger::Button:
        return QStringLiteral("button");
    case ETerrainResolveAuditTrigger::ShortcutShiftR:
        return QStringLiteral("shortcut-shift-r");
    default:
        return QStringLiteral("programmatic");
    }
}

QByteArray terrainResolvePackedDeltaSha256(const QVector<STerrainTileDelta>& deltas)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("ei-mapper-endurance-terrain-resolve-packed-deltas-v1");
    const QVector<STerrainTileDelta> ordered = canonicalDeltas(deltas);
    addInt(&hash, ordered.size());
    for(const STerrainTileDelta& delta : ordered)
    {
        addInt(&hash, layerOrder(delta.layer));
        addInt(&hash, delta.globalX);
        addInt(&hash, delta.globalY);
        addInt(&hash, delta.before);
        addInt(&hash, delta.after);
        addInt(&hash, delta.hasSemanticRequest ? 1 : 0);
        if(delta.hasSemanticRequest)
        {
            addInt(&hash, delta.requestedSignature.northWest);
            addInt(&hash, delta.requestedSignature.northEast);
            addInt(&hash, delta.requestedSignature.southEast);
            addInt(&hash, delta.requestedSignature.southWest);
        }
    }
    return hash.result().toHex();
}

QJsonObject terrainResolveAuditSessionJson(const STerrainResolveAuditSession& session,
                                           int packedDeltaLimit,
                                           int skippedSampleLimit)
{
    const int boundedDeltaLimit = qMax(0, packedDeltaLimit);
    const int boundedSkippedLimit = qMax(0, skippedSampleLimit);
    QJsonObject result;
    result.insert(QStringLiteral("schema"),
                  QStringLiteral("ei-mapper-endurance-terrain-resolve-session"));
    result.insert(QStringLiteral("version"), 1);
    result.insert(QStringLiteral("actionId"), QString::number(session.actionId));
    result.insert(QStringLiteral("trigger"), terrainResolveAuditTriggerName(session.trigger));
    result.insert(QStringLiteral("outcome"), session.outcome);
    result.insert(QStringLiteral("startedUtc"),
                  session.startedUtc.isValid()
                      ? session.startedUtc.toUTC().toString(Qt::ISODateWithMs) : QString());
    result.insert(QStringLiteral("endedUtc"),
                  session.endedUtc.isValid()
                      ? session.endedUtc.toUTC().toString(Qt::ISODateWithMs) : QString());
    result.insert(QStringLiteral("mprNamespace"), session.mprNamespace);
    result.insert(QStringLiteral("sourceMprSha256"),
                  QString::fromLatin1(session.sourceMprSha256));
    result.insert(QStringLiteral("atlasPixelSha256"),
                  QString::fromLatin1(session.atlasPixelSha256));

    QJsonObject fields;
    fields.insert(QStringLiteral("landBefore"), fieldJson(session.landFieldBefore));
    fields.insert(QStringLiteral("landAfter"), fieldJson(session.landFieldAfter));
    fields.insert(QStringLiteral("overlayBefore"), fieldJson(session.overlayFieldBefore));
    fields.insert(QStringLiteral("overlayAfter"), fieldJson(session.overlayFieldAfter));
    result.insert(QStringLiteral("semanticFields"), fields);

    result.insert(QStringLiteral("stagedTupleCount"), session.stagedTupleCount);
    result.insert(QStringLiteral("stagedCellCount"), session.stagedCellCount);
    result.insert(QStringLiteral("changedCellCount"), session.packedDeltas.size());
    result.insert(QStringLiteral("generatedTupleCount"), session.generatedTupleCount);
    result.insert(QStringLiteral("skippedCellCount"), session.skippedCellCount);
    result.insert(QStringLiteral("packedDeltaSha256"),
                  QString::fromLatin1(terrainResolvePackedDeltaSha256(
                      session.packedDeltas)));
    const QVector<STerrainTileDelta> ordered = canonicalDeltas(session.packedDeltas);
    QJsonArray examples;
    for(int index = 0; index < ordered.size() && index < boundedDeltaLimit; ++index)
        examples.append(deltaJson(ordered.at(index)));
    result.insert(QStringLiteral("packedDeltaExamples"), examples);
    result.insert(QStringLiteral("packedDeltaExamplesTruncated"),
                  ordered.size() > boundedDeltaLimit);

    const int boundedRuntimeLimit = qMin(128, boundedDeltaLimit);
    QJsonArray runtimeDecisions;
    for(int index = 0; index < session.runtimeDecisions.size()
        && index < boundedRuntimeLimit; ++index)
    {
        const STerrainResolveRuntimeDecision& decision =
            session.runtimeDecisions.at(index);
        QJsonObject item;
        item.insert(QStringLiteral("requestedCorners"), signatureJson(decision.signature));
        item.insert(QStringLiteral("predecessorRuntimeType"),
                    decision.predecessorRuntimeType);
        item.insert(QStringLiteral("selectedRuntimeType"), decision.selectedRuntimeType);
        item.insert(QStringLiteral("selectedWorkingSlot"), decision.selectedWorkingSlot);
        item.insert(QStringLiteral("origin"), decision.origin);
        item.insert(QStringLiteral("blendProvenance"), decision.blendProvenance);
        item.insert(QStringLiteral("cellCount"), decision.cellCount);
        runtimeDecisions.append(item);
    }
    result.insert(QStringLiteral("runtimeTypeDecisions"), runtimeDecisions);
    result.insert(QStringLiteral("runtimeTypeDecisionCount"),
                  session.runtimeDecisions.size());
    result.insert(QStringLiteral("runtimeTypeDecisionsTruncated"),
                  session.runtimeDecisions.size() > boundedRuntimeLimit);

    const int boundedSkippedReasonLimit = 64;
    QJsonArray skippedReasons;
    for(int reasonIndex = 0; reasonIndex < session.skippedReasons.size()
        && reasonIndex < boundedSkippedReasonLimit; ++reasonIndex)
    {
        const STerrainResolveSkippedReason& skipped =
            session.skippedReasons.at(reasonIndex);
        QJsonObject item;
        item.insert(QStringLiteral("reason"), skipped.reason);
        item.insert(QStringLiteral("count"), skipped.count);
        QJsonArray samples;
        for(int index = 0; index < skipped.sampleCells.size()
            && index < boundedSkippedLimit; ++index)
            samples.append(pointJson(skipped.sampleCells.at(index)));
        item.insert(QStringLiteral("sampleCells"), samples);
        item.insert(QStringLiteral("samplesTruncated"),
                    skipped.sampleCells.size() > boundedSkippedLimit);
        skippedReasons.append(item);
    }
    result.insert(QStringLiteral("skippedReasons"), skippedReasons);
    result.insert(QStringLiteral("skippedReasonCount"),
                  session.skippedReasons.size());
    result.insert(QStringLiteral("skippedReasonsTruncated"),
                  session.skippedReasons.size() > boundedSkippedReasonLimit);

    QJsonObject undo;
    undo.insert(QStringLiteral("commandId"), session.undoCommandId);
    undo.insert(QStringLiteral("description"), session.undoCommandDescription);
    undo.insert(QStringLiteral("stackIndexBefore"), session.undoStackIndexBefore);
    undo.insert(QStringLiteral("stackCountBefore"), session.undoStackCountBefore);
    undo.insert(QStringLiteral("stackIndexAfter"), session.undoStackIndexAfter);
    undo.insert(QStringLiteral("stackCountAfter"), session.undoStackCountAfter);
    undo.insert(QStringLiteral("historyCleared"), session.undoHistoryCleared);
    result.insert(QStringLiteral("undo"), undo);

    const int boundedFindingLimit = 64;
    QJsonArray findings;
    for(int index = 0; index < session.validationFindings.size()
        && index < boundedFindingLimit; ++index)
        findings.append(session.validationFindings.at(index));
    result.insert(QStringLiteral("validationFindings"), findings);
    result.insert(QStringLiteral("validationFindingCount"),
                  session.validationFindings.size());
    result.insert(QStringLiteral("validationFindingsTruncated"),
                  session.validationFindings.size() > boundedFindingLimit);
    return result;
}

QString terrainResolveAuditJournalPath(const QString& diagnosticsRoot,
                                       const QString& mprNamespace)
{
    QString safeName = mprNamespace.toLower();
    safeName.replace(QRegExp(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
    if(safeName.isEmpty())
        safeName = QStringLiteral("terrain");
    return QDir(diagnosticsRoot).filePath(
        safeName + QStringLiteral("-resolve-sessions.json"));
}

bool appendTerrainResolveAuditSession(const QString& path,
                                      const STerrainResolveAuditSession& session,
                                      int retainedSessionCount,
                                      int packedDeltaLimit,
                                      int skippedSampleLimit,
                                      QString* error)
{
    const QFileInfo info(path);
    if(!QDir().mkpath(info.absolutePath()))
    {
        if(error)
            *error = QStringLiteral("Could not create the resolver diagnostic folder");
        return false;
    }

    QJsonObject root;
    QFile existing(path);
    if(existing.open(QIODevice::ReadOnly))
    {
        const QJsonDocument parsed = QJsonDocument::fromJson(existing.readAll());
        if(parsed.isObject())
            root = parsed.object();
        // QSaveFile commits by replacing the destination.  Windows does not
        // permit that replacement while this diagnostic reader still holds a
        // handle on the previous journal.
        existing.close();
    }
    if(root.value(QStringLiteral("schema")).toString()
           != QStringLiteral("ei-mapper-endurance-terrain-resolve-journal"))
        root = QJsonObject();

    QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();
    sessions.append(terrainResolveAuditSessionJson(
        session, packedDeltaLimit, skippedSampleLimit));
    const int retained = qMax(1, retainedSessionCount);
    while(sessions.size() > retained)
        sessions.removeAt(0);
    root.insert(QStringLiteral("schema"),
                QStringLiteral("ei-mapper-endurance-terrain-resolve-journal"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("sessions"), sessions);

    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly))
    {
        if(error)
            *error = output.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if(output.write(bytes) != bytes.size() || !output.commit())
    {
        if(error)
            *error = output.errorString();
        return false;
    }
    if(error)
        error->clear();
    return true;
}

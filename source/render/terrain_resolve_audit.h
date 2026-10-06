#ifndef TERRAIN_RESOLVE_AUDIT_H
#define TERRAIN_RESOLVE_AUDIT_H

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QPoint>
#include <QMap>
#include <QString>
#include <QVector>

#include "terrain_edit_model.h"

enum class ETerrainResolveAuditTrigger
{
    Button,
    ShortcutShiftR,
    Programmatic
};

struct STerrainResolveSemanticFieldAudit
{
    bool present{false};
    bool complete{false};
    bool active{false};
    QByteArray sha256;
};

struct STerrainResolveRuntimeDecision
{
    STerrainTransitionSignature signature;
    qint32 predecessorRuntimeType{-1};
    qint32 selectedRuntimeType{-1};
    int selectedWorkingSlot{-1};
    qint32 routeOwnerMaterial{0};
    QString origin;
    QString blendProvenance;
    int cellCount{0};
};

struct STerrainResolveSkippedReason
{
    QString reason;
    int count{0};
    QVector<QPoint> sampleCells;
};

// A resolver session is one user-visible Shift+R action. It deliberately
// keeps only session-level state; detailed packed deltas are bounded when the
// journal is serialized so large maps cannot flood workflow diagnostics.
struct STerrainResolveAuditSession
{
    quint64 actionId{0};
    ETerrainResolveAuditTrigger trigger{ETerrainResolveAuditTrigger::Programmatic};
    QString outcome;
    QDateTime startedUtc;
    QDateTime endedUtc;
    QString mprNamespace;
    QByteArray sourceMprSha256;
    QByteArray atlasPixelSha256;
    STerrainResolveSemanticFieldAudit landFieldBefore;
    STerrainResolveSemanticFieldAudit landFieldAfter;
    STerrainResolveSemanticFieldAudit overlayFieldBefore;
    STerrainResolveSemanticFieldAudit overlayFieldAfter;
    int stagedTupleCount{0};
    int stagedCellCount{0};
    int generatedTupleCount{0};
    int skippedCellCount{0};
    QVector<STerrainTileDelta> packedDeltas;
    QMap<QString,QVector<STerrainTileDelta>> worldPackedDeltas;
    QVector<STerrainResolveRuntimeDecision> runtimeDecisions;
    QVector<STerrainResolveSkippedReason> skippedReasons;
    QString undoCommandId;
    QString undoCommandDescription;
    int undoStackIndexBefore{-1};
    int undoStackCountBefore{-1};
    int undoStackIndexAfter{-1};
    int undoStackCountAfter{-1};
    bool undoHistoryCleared{false};
    QStringList validationFindings;
};

QString terrainResolveAuditTriggerName(ETerrainResolveAuditTrigger trigger);
QByteArray terrainResolvePackedDeltaSha256(const QVector<STerrainTileDelta>& deltas);
int terrainResolveChangedCellCount(const STerrainResolveAuditSession& session);
QByteArray terrainResolveSessionDeltaSha256(const STerrainResolveAuditSession& session);
QJsonObject terrainResolveAuditSessionJson(const STerrainResolveAuditSession& session,
                                           int packedDeltaLimit = 256,
                                           int skippedSampleLimit = 32);
QString terrainResolveAuditJournalPath(const QString& diagnosticsRoot,
                                       const QString& mprNamespace);
bool appendTerrainResolveAuditSession(const QString& path,
                                      const STerrainResolveAuditSession& session,
                                      int retainedSessionCount = 32,
                                      int packedDeltaLimit = 256,
                                      int skippedSampleLimit = 32,
                                      QString* error = nullptr);

#endif // TERRAIN_RESOLVE_AUDIT_H

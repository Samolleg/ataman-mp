#ifndef AI_GRAPH_PERSISTENCE_H
#define AI_GRAPH_PERSISTENCE_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>

QString aiGraphRuntimeSiblingPath(const QFileInfo& activeMpr);
bool aiGraphEmbeddedSourceShadowsExternal(bool primaryMobHasEmbeddedGraph);
QString aiGraphSharedMobWarning(const QString& mobFileName,
                                const QStringList& mapOwners);

struct SAiGraphPreviewSource
{
    QByteArray bytes;
    QString label;
    QString path;
    bool embedded{false};
};

// Runtime ownership is explicit: only the primary MOB's embedded graph may
// shadow the exact same-basename sibling GRP. Secondary MOBs are not inputs.
SAiGraphPreviewSource selectAiGraphPreviewSource(
    const QByteArray& primaryEmbeddedGraph,
    const QString& primaryMobFileName,
    const QFileInfo& activeMpr);

struct SAiGraphFileCommitRequest
{
    QFileInfo destination;
    QByteArray graphBytes;
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
#endif
};

struct SAiGraphFileCommitResult
{
    bool success{false};
    bool noOp{false};
    QString phase;
    QString error;
    QString sha256;
    QString backupPath;
};

SAiGraphFileCommitResult commitValidatedAiGraphFile(
    const SAiGraphFileCommitRequest& request);

#endif // AI_GRAPH_PERSISTENCE_H

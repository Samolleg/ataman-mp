#ifndef RES_SAVE_COORDINATOR_H
#define RES_SAVE_COORDINATOR_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>

class CResArchiveDocument;

struct SResMemberSaveRequest
{
    QFileInfo source;
    QFileInfo destination;
    QString memberName;
    QByteArray expectedMemberSha256;
    QByteArray replacementPayload;
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
#endif
};

struct SResMemberSaveResult
{
    bool success{false};
    bool noOp{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
    QStringList validationNotes;
};

SResMemberSaveResult commitStagedResMemberReplacement(
    const CResArchiveDocument& baseline, const SResMemberSaveRequest& request);

#endif // RES_SAVE_COORDINATOR_H

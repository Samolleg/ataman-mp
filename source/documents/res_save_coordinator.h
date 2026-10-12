#ifndef RES_SAVE_COORDINATOR_H
#define RES_SAVE_COORDINATOR_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "res_archive_document.h"

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
    bool committed{false};
    int commitAttempts{0};
    bool noOp{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
    QStringList validationNotes;
};

struct SResPublishedMembersSaveRequest
{
    QFileInfo source;
    QFileInfo destination;
    QVector<SResArchiveNewMember> published;
    bool allowReplaceExisting{false};
    bool createBackup{true};
    QString backupRootOverride;
    // Called from the worker thread with a short phase label and overall
    // progress percentage. UI callers must marshal it to their GUI thread.
    std::function<void(const QString&, int)> progress;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
    QString interruptionMarkerForTest;
    bool pauseAfterPartialWriteForTest{false};
#endif
};

struct SResPublishedMembersSaveResult
{
    bool success{false};
    bool committed{false};
    int commitAttempts{0};
    bool noOp{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
    QStringList validationNotes;
};

struct SResArchiveBytesSaveRequest
{
    QFileInfo destination;
    QByteArray candidateBytes;
    bool createBackup{true};
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
#endif
};

struct SResArchiveBytesSaveResult
{
    bool success{false};
    bool committed{false};
    int commitAttempts{0};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
};

SResMemberSaveResult commitStagedResMemberReplacement(
    const CResArchiveDocument& baseline, const SResMemberSaveRequest& request);

// Publishes a coherent page set through a staged, freshly reopened, race-checked
// and atomic RES transaction. The caller deliberately controls whether an
// existing destination is backed up; disabling the backup never weakens the
// staging, race, or post-commit validation gates.
SResPublishedMembersSaveResult commitStagedResPublishedMembers(
    const CResArchiveDocument& baseline,
    const SResPublishedMembersSaveRequest& request);

SResArchiveBytesSaveResult commitValidatedResArchiveBytes(
    const SResArchiveBytesSaveRequest& request);

#endif // RES_SAVE_COORDINATOR_H

#ifndef RES_SAVE_COORDINATOR_H
#define RES_SAVE_COORDINATOR_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>

#include "res_archive_document.h"
#include "res_archive_index.h"

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
    std::function<void()> beforeCommitForTest;
#endif
};

struct SResMemberSaveResult
{
    bool success{false};
    bool noOp{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QByteArray previousDestinationSha256;
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
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
    std::function<void()> beforeCommitForTest;
#endif
};

struct SResPublishedMembersSaveResult
{
    bool success{false};
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
    QByteArray expectedSourceBytes;
    bool createBackup{true};
    bool requireAbsent{false};
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
    std::function<void()> beforeCommitForTest;
#endif
};

struct SResArchiveBytesSaveResult
{
    bool success{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
};

SResMemberSaveResult commitStagedResMemberReplacement(
    const CResArchiveDocument& baseline, const SResMemberSaveRequest& request);

// Reads only the edited member; rebases unrelated source changes and supports
// in-place saves and Save As through bounded-memory, guarded atomic I/O.
SResMemberSaveResult commitStreamedResMemberReplacement(const SResMemberSaveRequest& request);

// Publishes a coherent page set through a staged, freshly reopened, race-checked
// and atomic RES transaction. The caller deliberately controls whether an
// existing destination is backed up; disabling the backup never weakens the
// staging, race, or post-commit validation gates.
SResPublishedMembersSaveResult commitStagedResPublishedMembers(
    const CResArchiveDocument& baseline,
    const SResPublishedMembersSaveRequest& request);

SResArchiveBytesSaveResult commitValidatedResArchiveBytes(
    const SResArchiveBytesSaveRequest& request);

// File-backed publication/removal. Only changed member payloads are retained.
SResPublishedMembersSaveResult commitStreamedResMembers(
    const CResArchiveIndex& source,
    const SResPublishedMembersSaveRequest& request,
    const QStringList& removed = {}, const QByteArray& expectedSourceSha256 = {},
    const std::function<bool()>& inventoryGuard = {});

#endif // RES_SAVE_COORDINATOR_H

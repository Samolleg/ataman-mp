#ifndef STAGED_SAVE_VALIDATOR_H
#define STAGED_SAVE_VALIDATOR_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>

class CMprDocument;
class CMprEditTransaction;
struct SMprCanvasResizeRequest;

struct SStagedSaveValidation
{
    bool success{false};
    QString error;
    QString sha256;
    qint64 size{0};
};

struct SExistingFileComparison
{
    bool exists{false};
    bool readable{false};
    bool equal{false};
    QString error;
};

struct SStagedMprTransactionValidation
{
    bool success{false};
    QString error;
    QString sha256;
    qint64 size{0};
    QByteArray candidateBytes;
    QStringList changedMembers;
    QString semanticDiff;
};

struct SMprSaveCommitRequest
{
    QFileInfo source;
    QFileInfo destination;
    // Empty in production. Tests use an isolated backup root so no user
    // backup state is touched.
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
#endif
};

struct SMprSaveCommitResult
{
    bool success{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
    QStringList changedMembers;
    QString semanticDiff;
};

struct SMobSaveCommitRequest
{
    QFileInfo destination;
    QString backupRootOverride;
#ifdef QT_TESTLIB_LIB
    bool simulateAtomicCommitFailure{false};
#endif
};

struct SMobSaveCommitResult
{
    bool success{false};
    QString phase;
    QString error;
    QString candidateSha256;
    QString backupPath;
};

SExistingFileComparison compareExistingFile(const QFileInfo& file,
                                             const QByteArray& expectedBytes);

SStagedSaveValidation validateStagedSave(const QByteArray& data,
                                         const QFileInfo& destination);

// Replaces an existing file only when its bytes still match the caller's
// baseline. The race-check handle is closed before QSaveFile commits so the
// replacement is valid on Windows. Callers remain responsible for creating a
// verified backup before invoking this helper.
bool atomicallyReplaceVerifiedFile(const QFileInfo& destination,
                                   const QByteArray& expectedBytes,
                                   const QByteArray& candidateBytes,
                                   QString* error = nullptr);

// Builds and validates a changed MPR candidate without creating or replacing
// the destination. Replacement, backup, and source-race checks remain a later
// explicit commit step.
SStagedMprTransactionValidation validateStagedMprTransaction(
    const CMprDocument& baseline, const CMprEditTransaction& transaction,
    const QFileInfo& destination);

// Commits a previously representable MPR transaction through the complete
// race-check, stage, backup, atomic replacement, and post-commit reopen
// contract. Source and destination may be the same path.
SMprSaveCommitResult commitStagedMprTransaction(
    const CMprDocument& baseline, const CMprEditTransaction& transaction,
    const SMprSaveCommitRequest& request);

// Structural canvas saves always target a separate MPR. diskBaseline proves
// the open source has not changed; resizeBaseline may include the editor's
// already-validated unsaved terrain transaction.
SMprSaveCommitResult commitStagedMprCanvasResize(
    const CMprDocument& diskBaseline, const CMprDocument& resizeBaseline,
    const SMprCanvasResizeRequest& resize,
    const SMprSaveCommitRequest& request);

// Publishes a freshly generated, already deterministic MPR. There is no source
// race to check, but an existing destination is still backed up and guarded
// against change before atomic replacement.
SMprSaveCommitResult commitGeneratedMpr(
    const QByteArray& candidateBytes,
    const SMprSaveCommitRequest& request);

// Publishes a freshly generated canonical MOB. This path is intentionally
// separate from changed-MOB saving because a new document has no raw baseline
// whose unknown sections can be preserved.
SMobSaveCommitResult commitGeneratedMob(
    const QByteArray& candidateBytes,
    const SMobSaveCommitRequest& request);

#endif // STAGED_SAVE_VALIDATOR_H

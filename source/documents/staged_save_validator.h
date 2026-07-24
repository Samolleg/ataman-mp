#ifndef STAGED_SAVE_VALIDATOR_H
#define STAGED_SAVE_VALIDATOR_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QStringList>

class CMprDocument;
class CMprEditTransaction;

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

SExistingFileComparison compareExistingFile(const QFileInfo& file,
                                             const QByteArray& expectedBytes);

SStagedSaveValidation validateStagedSave(const QByteArray& data,
                                         const QFileInfo& destination);

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

#endif // STAGED_SAVE_VALIDATOR_H

#include "res_save_coordinator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>

#include "backup_manager.h"
#include "res_archive_document.h"

namespace {
bool readFile(const QFileInfo& info, QByteArray* bytes, QString* error)
{
    if(bytes)
        bytes->clear();
    const QFileInfo fresh(info.absoluteFilePath());
    if(!fresh.exists() || !fresh.isFile())
    {
        if(error) *error = QString("File does not exist: %1").arg(fresh.absoluteFilePath());
        return false;
    }
    QFile file(fresh.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error) *error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    if(file.error() != QFileDevice::NoError)
    {
        if(error) *error = file.errorString();
        return false;
    }
    if(bytes) *bytes = data;
    return true;
}

bool destinationMatches(const QFileInfo& destination, bool existed,
                        const QByteArray& expected, QString* error)
{
    const QFileInfo fresh(destination.absoluteFilePath());
    if(!existed)
    {
        if(!fresh.exists())
            return true;
        if(error) *error = "The destination appeared while the RES save was staged.";
        return false;
    }
    QByteArray current;
    if(!readFile(fresh, &current, error))
        return false;
    if(current == expected)
        return true;
    if(error) *error = "The destination changed while the RES save was staged.";
    return false;
}
}

SResMemberSaveResult commitStagedResMemberReplacement(
    const CResArchiveDocument& baseline, const SResMemberSaveRequest& request)
{
    SResMemberSaveResult result;
    result.phase = "source-check";
    if(request.source.absoluteFilePath().isEmpty() ||
       request.destination.absoluteFilePath().isEmpty() || request.memberName.isEmpty())
    {
        result.error = "Source, destination, and RES member name are required.";
        return result;
    }
    QByteArray sourceBytes;
    if(!readFile(request.source, &sourceBytes, &result.error))
        return result;
    if(sourceBytes != baseline.originalBytes())
    {
        result.error = "The source RES changed on disk. Reopen it before saving.";
        return result;
    }

    const QFileInfo destination(request.destination.absoluteFilePath());
    const bool destinationExisted = destination.exists();
    QByteArray destinationBytes;
    if(destinationExisted && !readFile(destination, &destinationBytes, &result.error))
    {
        result.phase = "destination-check";
        return result;
    }

    result.phase = "serialization";
    QByteArray candidateBytes;
    if(!baseline.replaceMember(request.memberName, request.expectedMemberSha256,
                               request.replacementPayload, &candidateBytes,
                               &result.validationNotes))
    {
        result.error = result.validationNotes.join('\n');
        return result;
    }
    result.candidateSha256 = QString::fromLatin1(
        QCryptographicHash::hash(candidateBytes, QCryptographicHash::Sha256).toHex());
    if(candidateBytes == destinationBytes && destinationExisted)
    {
        result.success = true;
        result.noOp = true;
        result.phase = "no-op";
        return result;
    }

    result.phase = "staging";
    const QDir destinationDir(destination.absolutePath());
    if(!destinationDir.exists())
    {
        result.error = "Destination directory does not exist: " + destinationDir.absolutePath();
        return result;
    }
    QTemporaryFile stage(destinationDir.filePath(".EIMapperEndurance-res-stage-XXXXXX.res"));
    stage.setAutoRemove(true);
    if(!stage.open() || stage.write(candidateBytes) != candidateBytes.size() || !stage.flush())
    {
        result.error = "Could not write the staged RES file: " + stage.errorString();
        return result;
    }
    const QString stagePath = stage.fileName();
    stage.close();
    QByteArray reopenedBytes;
    if(!readFile(QFileInfo(stagePath), &reopenedBytes, &result.error) ||
       reopenedBytes != candidateBytes)
    {
        if(result.error.isEmpty())
            result.error = "The staged RES bytes changed on fresh reopen.";
        return result;
    }
    CResArchiveDocument reopened;
    QStringList reopenErrors;
    if(!reopened.loadBytes(reopenedBytes, stagePath, &reopenErrors) ||
       !CResArchiveDocument::validateReplacement(
           baseline, reopened, request.memberName, request.replacementPayload, &reopenErrors))
    {
        result.validationNotes.append(reopenErrors);
        result.error = "Fresh RES validation failed:\n" + reopenErrors.join('\n');
        return result;
    }

    result.phase = "pre-backup-race-check";
    QByteArray currentSource;
    if(!readFile(request.source, &currentSource, &result.error) || currentSource != sourceBytes)
    {
        if(result.error.isEmpty()) result.error = "The source RES changed during staging.";
        return result;
    }
    if(!destinationMatches(destination, destinationExisted, destinationBytes, &result.error))
        return result;

    if(destinationExisted)
    {
        result.phase = "backup";
        const SBackupResult backup =
            CBackupManager(request.backupRootOverride).snapshotBeforeSave(destination);
        if(!backup.success)
        {
            result.error = backup.error;
            return result;
        }
        const QString expectedHash = QString::fromLatin1(
            QCryptographicHash::hash(destinationBytes, QCryptographicHash::Sha256).toHex());
        if(backup.sha256.compare(expectedHash, Qt::CaseInsensitive) != 0)
        {
            result.error = "The RES backup hash did not match the destination.";
            return result;
        }
        result.backupPath = backup.backupPath;
    }

    result.phase = "pre-commit-race-check";
    if(!readFile(request.source, &currentSource, &result.error) || currentSource != sourceBytes)
    {
        if(result.error.isEmpty()) result.error = "The source RES changed before commit.";
        return result;
    }
    if(!destinationMatches(destination, destinationExisted, destinationBytes, &result.error))
        return result;

#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = "atomic-replacement";
        result.error = "Simulated atomic replacement failure.";
        return result;
    }
#endif

    result.phase = "atomic-replacement";
    QSaveFile output(destination.absoluteFilePath());
    if(!output.open(QIODevice::WriteOnly))
    {
        result.error = output.errorString();
        return result;
    }
    if(output.write(candidateBytes) != candidateBytes.size())
    {
        result.error = output.errorString();
        output.cancelWriting();
        return result;
    }
    if(!output.commit())
    {
        result.error = output.errorString();
        return result;
    }
    result.success = true;
    result.phase = "complete";
    return result;
}

SResPublishedMembersSaveResult commitStagedResPublishedMembers(
    const CResArchiveDocument& baseline,
    const SResPublishedMembersSaveRequest& request)
{
    SResPublishedMembersSaveResult result;
    result.phase = QStringLiteral("source-check");
    if(request.source.absoluteFilePath().isEmpty()
       || request.destination.absoluteFilePath().isEmpty()
       || request.published.isEmpty())
    {
        result.error = QStringLiteral(
            "Source, destination, and one or more published RES members are required.");
        return result;
    }

    QByteArray sourceBytes;
    if(!readFile(request.source, &sourceBytes, &result.error))
        return result;
    if(sourceBytes != baseline.originalBytes())
    {
        result.error = QStringLiteral(
            "The source RES changed on disk. Reopen it before publishing.");
        return result;
    }

    const QFileInfo destination(request.destination.absoluteFilePath());
    const bool destinationExisted = destination.exists();
    QByteArray destinationBytes;
    if(destinationExisted
       && !readFile(destination, &destinationBytes, &result.error))
    {
        result.phase = QStringLiteral("destination-check");
        return result;
    }

    result.phase = QStringLiteral("serialization");
    QByteArray candidateBytes;
    if(!baseline.buildWithPublishedMembers(
           request.published, request.allowReplaceExisting,
           &candidateBytes, &result.validationNotes))
    {
        result.error = result.validationNotes.join(QLatin1Char('\n'));
        return result;
    }
    result.candidateSha256 = QString::fromLatin1(
        QCryptographicHash::hash(candidateBytes,
                                 QCryptographicHash::Sha256).toHex());
    if(destinationExisted && candidateBytes == destinationBytes)
    {
        result.success = true;
        result.noOp = true;
        result.phase = QStringLiteral("no-op");
        return result;
    }

    result.phase = QStringLiteral("staging");
    const QDir destinationDir(destination.absolutePath());
    if(!destinationDir.exists())
    {
        result.error = QStringLiteral("Destination directory does not exist: ")
            + destinationDir.absolutePath();
        return result;
    }
    QTemporaryFile stage(destinationDir.filePath(
        QStringLiteral(".EIMapperEndurance-res-publish-stage-XXXXXX.res")));
    stage.setAutoRemove(true);
    if(!stage.open() || stage.write(candidateBytes) != candidateBytes.size()
       || !stage.flush())
    {
        result.error = QStringLiteral("Could not write the staged RES file: ")
            + stage.errorString();
        return result;
    }
    const QString stagePath = stage.fileName();
    stage.close();
    QByteArray reopenedBytes;
    if(!readFile(QFileInfo(stagePath), &reopenedBytes, &result.error)
       || reopenedBytes != candidateBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "The staged RES bytes changed on fresh reopen.");
        return result;
    }
    CResArchiveDocument reopened;
    QStringList reopenErrors;
    if(!reopened.loadBytes(reopenedBytes, stagePath, &reopenErrors))
    {
        result.validationNotes.append(reopenErrors);
        result.error = QStringLiteral("Fresh RES validation failed:\n")
            + reopenErrors.join(QLatin1Char('\n'));
        return result;
    }
    for(const SResArchiveNewMember& member : request.published)
        if(reopened.memberPayload(member.name) != member.payload)
        {
            result.error = QStringLiteral(
                "Fresh RES validation changed published member %1.")
                .arg(member.name);
            return result;
        }

    result.phase = QStringLiteral("pre-backup-race-check");
    QByteArray currentSource;
    if(!readFile(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "The source RES changed during publication staging.");
        return result;
    }
    if(!destinationMatches(destination, destinationExisted,
                           destinationBytes, &result.error))
        return result;

    if(destinationExisted && request.createBackup)
    {
        result.phase = QStringLiteral("backup");
        const SBackupResult backup = CBackupManager(
            request.backupRootOverride).snapshotBeforeSave(destination);
        if(!backup.success)
        {
            result.error = backup.error;
            return result;
        }
        const QString expectedHash = QString::fromLatin1(
            QCryptographicHash::hash(destinationBytes,
                                     QCryptographicHash::Sha256).toHex());
        if(backup.sha256.compare(expectedHash, Qt::CaseInsensitive) != 0)
        {
            result.error = QStringLiteral(
                "The RES backup hash did not match the destination.");
            return result;
        }
        result.backupPath = backup.backupPath;
    }

    result.phase = QStringLiteral("pre-commit-race-check");
    if(!readFile(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "The source RES changed before publication commit.");
        return result;
    }
    if(!destinationMatches(destination, destinationExisted,
                           destinationBytes, &result.error))
        return result;

#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = QStringLiteral("atomic-replacement");
        result.error = QStringLiteral("Simulated atomic replacement failure.");
        return result;
    }
#endif

    result.phase = QStringLiteral("atomic-replacement");
    QSaveFile output(destination.absoluteFilePath());
    if(!output.open(QIODevice::WriteOnly))
    {
        result.error = output.errorString();
        return result;
    }
    if(output.write(candidateBytes) != candidateBytes.size())
    {
        result.error = output.errorString();
        output.cancelWriting();
        return result;
    }
    if(!output.commit())
    {
        result.error = output.errorString();
        return result;
    }

    result.phase = QStringLiteral("post-commit-validation");
    QByteArray committedBytes;
    if(!readFile(destination, &committedBytes, &result.error)
       || committedBytes != candidateBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "The committed RES bytes differ from the validated candidate.");
        return result;
    }
    CResArchiveDocument committed;
    QStringList committedErrors;
    if(!committed.loadBytes(committedBytes, destination.absoluteFilePath(),
                            &committedErrors))
    {
        result.validationNotes.append(committedErrors);
        result.error = QStringLiteral(
            "The committed RES failed a fresh reopen:\n")
            + committedErrors.join(QLatin1Char('\n'));
        return result;
    }
    for(const SResArchiveNewMember& member : request.published)
        if(committed.memberPayload(member.name) != member.payload)
        {
            result.error = QStringLiteral(
                "The committed RES changed published member %1.")
                .arg(member.name);
            return result;
        }

    result.phase = QStringLiteral("complete");
    result.success = true;
    return result;
}

SResArchiveBytesSaveResult commitValidatedResArchiveBytes(
    const SResArchiveBytesSaveRequest& request)
{
    SResArchiveBytesSaveResult result;
    result.phase = QStringLiteral("validation");
    if(request.destination.absoluteFilePath().isEmpty()
       || request.candidateBytes.isEmpty())
    {
        result.error = QStringLiteral(
            "Destination and complete RES candidate bytes are required.");
        return result;
    }
    CResArchiveDocument candidate;
    QStringList errors;
    if(!candidate.loadBytes(request.candidateBytes,
                            QStringLiteral("complete-candidate.res"), &errors))
    {
        result.error = errors.join(QLatin1Char('\n'));
        return result;
    }
    result.candidateSha256 = QString::fromLatin1(
        QCryptographicHash::hash(request.candidateBytes,
                                 QCryptographicHash::Sha256).toHex());
    const QFileInfo destination(request.destination.absoluteFilePath());
    if(!QDir(destination.absolutePath()).exists())
    {
        result.error = QStringLiteral("Destination directory does not exist: ")
            + destination.absolutePath();
        return result;
    }
    const bool existed = destination.exists();
    QByteArray original;
    if(existed && !readFile(destination, &original, &result.error))
        return result;
    if(existed && original == request.candidateBytes)
    {
        result.phase = QStringLiteral("no-op");
        result.success = true;
        return result;
    }
    if(existed && request.createBackup)
    {
        result.phase = QStringLiteral("backup");
        const SBackupResult backup = CBackupManager(
            request.backupRootOverride).snapshotBeforeSave(destination);
        if(!backup.success)
        {
            result.error = backup.error;
            return result;
        }
        result.backupPath = backup.backupPath;
    }
    if(!destinationMatches(destination, existed, original, &result.error))
    {
        result.phase = QStringLiteral("race-check");
        return result;
    }
#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = QStringLiteral("atomic-replacement");
        result.error = QStringLiteral("Simulated atomic replacement failure.");
        return result;
    }
#endif
    result.phase = QStringLiteral("atomic-replacement");
    QSaveFile output(destination.absoluteFilePath());
    if(!output.open(QIODevice::WriteOnly)
       || output.write(request.candidateBytes) != request.candidateBytes.size()
       || !output.commit())
    {
        result.error = output.errorString();
        return result;
    }
    QByteArray reopenedBytes;
    CResArchiveDocument reopened;
    errors.clear();
    if(!readFile(destination, &reopenedBytes, &result.error)
       || reopenedBytes != request.candidateBytes
       || !reopened.loadBytes(reopenedBytes,
                              destination.absoluteFilePath(), &errors))
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral("Committed RES failed exact reopen: ")
                + errors.join(QLatin1Char('\n'));
        result.phase = QStringLiteral("post-commit-validation");
        return result;
    }
    result.phase = QStringLiteral("complete");
    result.success = true;
    return result;
}

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

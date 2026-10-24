#include "res_save_coordinator.h"
#include "process_edit_lock.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>

#include "backup_manager.h"
#include "file_write_activity.h"
#include <QLockFile>
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
    CFileWriteActivity writing;
    QVector<SBackupResult> backupLeases;
    SResMemberSaveResult result;
    auto resourceLease=CProcessEditLock::acquire(request.destination.absoluteFilePath(),&result.error,false);if(!resourceLease)return result;
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
        CResArchiveDocument fresh;
        if(fresh.loadBytes(sourceBytes,request.source.absoluteFilePath())
           &&fresh.memberPayload(request.memberName)==baseline.memberPayload(request.memberName))
            return commitStagedResMemberReplacement(fresh,request);
        result.error = "The source RES member changed on disk. Reopen it before saving.";
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
        backupLeases.append(backup);
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
    CBackupManager(request.backupRootOverride).scheduleCleanup();
    return result;
}

SResPublishedMembersSaveResult commitStagedResPublishedMembers(
    const CResArchiveDocument& baseline,
    const SResPublishedMembersSaveRequest& request)
{
    CFileWriteActivity writing;
    QVector<SBackupResult> backupLeases;
    SResPublishedMembersSaveResult result;
    auto resourceLease=CProcessEditLock::acquire(request.destination.absoluteFilePath(),&result.error,false);if(!resourceLease)return result;
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
        CResArchiveDocument fresh;bool compatible=fresh.loadBytes(sourceBytes,request.source.absoluteFilePath());
        for(const auto& member:request.published)compatible &= (fresh.uniqueMemberIndex(member.name)>=0)==(baseline.uniqueMemberIndex(member.name)>=0)
            &&fresh.memberPayload(member.name)==baseline.memberPayload(member.name);
        if(compatible)return commitStagedResPublishedMembers(fresh,request);
        result.error = QStringLiteral("The source RES members changed on disk. Reopen before publishing.");
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
        backupLeases.append(backup);
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
    CBackupManager(request.backupRootOverride).scheduleCleanup();
    return result;
}

SResArchiveBytesSaveResult commitValidatedResArchiveBytes(
    const SResArchiveBytesSaveRequest& request)
{
    CFileWriteActivity writing;
    QVector<SBackupResult> backupLeases;
    SResArchiveBytesSaveResult result;
    auto resourceLease=CProcessEditLock::acquire(request.destination.absoluteFilePath(),&result.error,false);if(!resourceLease)return result;
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
    if(!request.expectedSourceBytes.isEmpty() && original != request.expectedSourceBytes)
    {
        result.phase = QStringLiteral("source-check");
        result.error = QStringLiteral("The RES changed since candidate preparation.");
        return result;
    }
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
        backupLeases.append(backup);
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
    CBackupManager(request.backupRootOverride).scheduleCleanup();
    return result;
}
SResPublishedMembersSaveResult commitStreamedResMembers(
    const CResArchiveIndex& source, const SResPublishedMembersSaveRequest& request,
    const QStringList& removed, const QByteArray& expectedSourceSha256,
    const std::function<bool()>& inventoryGuard)
{
    CFileWriteActivity writing;
    QVector<SBackupResult> backupLeases;
    SResPublishedMembersSaveResult r;r.phase="prepare";
    auto resourceLease=CProcessEditLock::acquire(request.destination.absoluteFilePath(),&r.error,false);if(!resourceLease)return r;
    const QString path=request.destination.absoluteFilePath();
    QLockFile lock(path+".mapper-save.lock");
    if(!lock.tryLock(0)){r.error="Another save owns this RES.";return r;}
    if(!source.sourceIsCurrent(&r.error))return r;
    CResArchiveTransaction tx(source);bool changed=!removed.isEmpty();
    for(const auto& name:removed)if(!tx.remove(name,&r.error))return r;
    for(const auto& m:request.published){
        const int i=source.memberIndex(m.name);
        if(i==-2){r.error="Ambiguous publication member.";return r;}
        if(i>=0){
            if(source.readMember(i,&r.error)==m.payload)continue;
            if(!request.allowReplaceExisting){r.error="Published RES member collision: "+m.name;return r;}
            if(!tx.replace(m.name,{},m.payload,&r.error))return r;
        }else if(!tx.add(m.name,m.payload,m.lastWriteTime,&r.error))return r;
        changed=true;
    }
    if(!changed){r.success=r.noOp=true;r.phase="no-op";return r;}
    const auto before=resFileSha256(source.path(),&r.error);
    if(before.isEmpty()||(!expectedSourceSha256.isEmpty()&&before!=expectedSourceSha256)){
        r.error="RES changed since preparation.";return r;
    }
    if(QFileInfo(source.path())!=QFileInfo(path)){r.error="Streamed save requires the indexed destination.";return r;}
    r.phase="staging";
    const auto guard=[&](){
        r.phase="backup";
        if(request.createBackup){const auto b=CBackupManager(request.backupRootOverride).snapshotBeforeSave(QFileInfo(path));
            if(!b.success){r.error=b.error;return false;}
            r.backupPath=b.backupPath;backupLeases.append(b);
            if(b.sha256.toLatin1()!=before||resFileSha256(b.backupPath,&r.error)!=before){r.error="RES backup differs.";return false;}}
        r.phase="race-check";
        if(resFileSha256(path,&r.error)!=before||(inventoryGuard&&!inventoryGuard())){r.error="RES or map inventory changed.";return false;}
#ifdef QT_TESTLIB_LIB
        if(request.simulateAtomicCommitFailure){r.error="Simulated atomic replacement failure.";return false;}
#endif
        r.phase="atomic-replacement";return true;
    };
    QByteArray candidateHash;
    if(!tx.writeCandidate(path,&r.error,guard,&candidateHash)){
        r.candidateSha256=QString::fromLatin1(candidateHash);
        if(r.phase=="atomic-replacement"&&!r.backupPath.isEmpty()){
            QString restoreError;const auto current=resFileSha256(path,&restoreError);
            if(current==candidateHash)replaceResFromFile(path,r.backupPath,current,&restoreError);
        }
        return r;
    }
    r.phase="post-commit-validation";
    CResArchiveIndex reopened;
    if(!reopened.open(path,&r.error))return r;
    for(const auto& m:request.published)if(reopened.readMember(m.name,&r.error)!=m.payload){r.error="Published RES payload differs.";return r;}
    for(const auto& name:removed)if(reopened.memberIndex(name)!=-1){r.error="Removed RES member remains.";return r;}
    r.candidateSha256=QString::fromLatin1(candidateHash);
    r.success=!r.candidateSha256.isEmpty();r.phase="complete";if(r.success)CBackupManager(request.backupRootOverride).scheduleCleanup();return r;
}

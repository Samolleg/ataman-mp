#include "res_save_coordinator.h"
#include "../resources/res_read_lease.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#ifdef QT_TESTLIB_LIB
#include <QThread>
#endif

#include "backup_manager.h"
#include "../core/log.h"
#include "res_archive_document.h"

namespace {
bool readFile(const QFileInfo& info, QByteArray* bytes, QString* error)
{
    if(bytes)
        bytes->clear();
    const QFileInfo fresh(info.absoluteFilePath());
    if(!fresh.exists() || !fresh.isFile())
    {
        if(error) *error = QString("missing: %1").arg(fresh.absoluteFilePath());
        return false;
    }
    resIo::ReadLease lease(fresh.absoluteFilePath(), error);
    if(!lease.acquired()) return false;
    QFile file(fresh.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error) *error = QString("open: %1; Qt file error %2: %3").arg(fresh.absoluteFilePath()).arg(int(file.error())).arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    if(file.error() != QFileDevice::NoError || data.size() != file.size())
    {
        if(error) *error = QString("read: %1; Qt file error %2: %3").arg(fresh.absoluteFilePath()).arg(int(file.error())).arg(file.errorString());
        return false;
    }
    if(bytes) *bytes = data;
    return true;
}

bool destinationMatches(const QFileInfo& destination, bool existed,
                        const QByteArray& expected, QString* error)
{
    return resIo::matches(destination.absoluteFilePath(), expected, existed, error);
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
        result.error = "mismatch: The source RES changed on disk. Reopen it before saving.";
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
            result.error = "mismatch: The staged RES bytes changed on fresh reopen.";
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

    result.phase = "reader-drain";
    resIo::PublicationLease publication(destination.absoluteFilePath(), &result.error);
    if(!publication.acquired()) return result;
    result.phase = "pre-backup-race-check";
    QByteArray currentSource;
    if(!readFile(request.source, &currentSource, &result.error) || currentSource != sourceBytes)
    {
        if(result.error.isEmpty()) result.error = "mismatch: The source RES changed during staging.";
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
        if(result.error.isEmpty()) result.error = "mismatch: The source RES changed before commit.";
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
    output.setDirectWriteFallback(false);
    if(!output.open(QIODevice::WriteOnly))
    {
        result.error = QString("open/write: Qt file error %1: %2").arg(int(output.error())).arg(output.errorString());
        return result;
    }
    if(output.write(candidateBytes) != candidateBytes.size())
    {
        result.error = QString("open/write: Qt file error %1: %2").arg(int(output.error())).arg(output.errorString());
        output.cancelWriting();
        return result;
    }
    if(!resIo::commitWithRetry(output, destination.absoluteFilePath(), candidateBytes,
        [&](QString* error) {
            QByteArray current;
            if(!readFile(request.source, &current, error)) return false;
            if(current != sourceBytes) {
                if(error) *error = "mismatch: source RES changed before commit/retry";
                return false;
            }
            return destinationMatches(destination,destinationExisted,destinationBytes,error);
        }, &result.error, &result.commitAttempts)) return result;
    result.committed = true;
    result.phase = "post-commit-validation";
    if(!resIo::matches(destination.absoluteFilePath(),candidateBytes,true,&result.error)) return result;
    result.success = true;
    result.phase = "complete";
    return result;
}

SResPublishedMembersSaveResult commitStagedResPublishedMembers(
    const CResArchiveDocument& baseline,
    const SResPublishedMembersSaveRequest& request)
{
    SResPublishedMembersSaveResult result;
    QElapsedTimer phaseTimer;
    const auto progress = [&request](const QString& phase, int value) {
        if(request.progress)
            request.progress(phase, qBound(0, value, 100));
    };
    const auto reportPhase = [&phaseTimer](const QString& phase) {
        ei::log(eLogInfo, QStringLiteral("RES publication phase:%1 elapsedMs:%2 thread:worker")
            .arg(phase).arg(phaseTimer.elapsed()));
    };
    result.phase = QStringLiteral("source-check");
    if(request.source.absoluteFilePath().isEmpty()
       || request.destination.absoluteFilePath().isEmpty()
       || request.published.isEmpty())
    {
        result.error = QStringLiteral(
            "Source, destination, and one or more published RES members are required.");
        return result;
    }

    progress(QStringLiteral("Reading source HDPack"), 2);
    phaseTimer.start();
    QByteArray sourceBytes;
    if(!readFile(request.source, &sourceBytes, &result.error))
        return result;
    reportPhase(QStringLiteral("source-read"));
    if(sourceBytes != baseline.originalBytes())
    {
        result.error = QStringLiteral(
            "mismatch: The source RES changed on disk. Reopen it before publishing.");
        return result;
    }
    // The disk read has proved the immutable baseline. Keep its shared bytes
    // for all later race checks instead of retaining another complete RES copy.
    sourceBytes = baseline.originalBytes();

    const QFileInfo destination(request.destination.absoluteFilePath());
    const bool destinationExisted = destination.exists();
    QByteArray destinationBytes;
    phaseTimer.restart();
    const bool sameDestination = resIo::pathKey(request.source.absoluteFilePath())
        == resIo::pathKey(destination.absoluteFilePath());
    if(destinationExisted && sameDestination)
        destinationBytes = sourceBytes;
    else if(destinationExisted
       && !readFile(destination, &destinationBytes, &result.error))
    {
        result.phase = QStringLiteral("destination-check");
        return result;
    }
    reportPhase(QStringLiteral("destination-read"));

    result.phase = QStringLiteral("serialization");
    progress(QStringLiteral("Building and validating HDPack candidate"), 5);
    phaseTimer.restart();
    QByteArray candidateBytes;
    if(!baseline.buildWithPublishedMembers(
           request.published, request.allowReplaceExisting,
           &candidateBytes, &result.validationNotes,
           [&progress](int value) {
               progress(QStringLiteral("Building HDPack members"),
                        5 + (value * 35) / 100);
           }))
    {
        result.error = result.validationNotes.join(QLatin1Char('\n'));
        return result;
    }
    reportPhase(QStringLiteral("candidate-build-and-member-validation"));
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
    progress(QStringLiteral("Staging complete HDPack"), 42);
    phaseTimer.restart();
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
    reportPhase(QStringLiteral("stage-write-and-flush"));
    const QString stagePath = stage.fileName();
    stage.close();
    phaseTimer.restart();
    QByteArray reopenedBytes;
    if(!readFile(QFileInfo(stagePath), &reopenedBytes, &result.error)
       || reopenedBytes != candidateBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "mismatch: The staged RES bytes changed on fresh reopen.");
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
    reportPhase(QStringLiteral("staged-reopen-and-validation"));
    for(const SResArchiveNewMember& member : request.published)
        if(reopened.memberPayload(member.name) != member.payload)
        {
            result.error = QStringLiteral(
                "Fresh RES validation changed published member %1.")
                .arg(member.name);
            return result;
        }
    // The staged candidate has passed fresh parse and exact member validation.
    // No later step uses this document; release its whole-archive byte buffers
    // before backup and post-commit parsing allocate their own verification data.
    reopened = CResArchiveDocument();
    reopenedBytes.clear();

    result.phase = QStringLiteral("reader-drain");
    progress(QStringLiteral("Waiting for RES readers"), 55);
    phaseTimer.restart();
    resIo::PublicationLease publication(destination.absoluteFilePath(), &result.error);
    if(!publication.acquired()) return result;
    reportPhase(QStringLiteral("reader-drain"));
    result.phase = QStringLiteral("pre-backup-race-check");
    QByteArray currentSource;
    if(!readFile(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "mismatch: The source RES changed during publication staging.");
        return result;
    }
    if(!destinationMatches(destination, destinationExisted,
                           destinationBytes, &result.error))
        return result;
    currentSource.clear();

    if(destinationExisted && request.createBackup)
    {
        result.phase = QStringLiteral("backup");
        progress(QStringLiteral("Creating verified HDPack backup"), 60);
        phaseTimer.restart();
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
        reportPhase(QStringLiteral("backup-copy-and-hash"));
    }

    result.phase = QStringLiteral("pre-commit-race-check");
    if(!readFile(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "mismatch: The source RES changed before publication commit.");
        return result;
    }
    if(!destinationMatches(destination, destinationExisted,
                           destinationBytes, &result.error))
        return result;
    currentSource.clear();

#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = QStringLiteral("atomic-replacement");
        result.error = QStringLiteral("Simulated atomic replacement failure.");
        return result;
    }
#endif

    result.phase = QStringLiteral("atomic-replacement");
    progress(QStringLiteral("Writing and atomically replacing HDPack"), 65);
    phaseTimer.restart();
    QSaveFile output(destination.absoluteFilePath());
    output.setDirectWriteFallback(false);
    if(!output.open(QIODevice::WriteOnly))
    {
        result.error = QString("open/write: Qt file error %1: %2").arg(int(output.error())).arg(output.errorString());
        return result;
    }
    const qint64 writeChunk = 4 * 1024 * 1024;
    qint64 written = 0;
    while(written < candidateBytes.size())
    {
        const qint64 count = qMin(writeChunk,
            qint64(candidateBytes.size()) - written);
        if(output.write(candidateBytes.constData() + written, count) != count)
        {
            result.error = QString("open/write: Qt file error %1: %2").arg(int(output.error())).arg(output.errorString());
            output.cancelWriting();
            return result;
        }
        written += count;
        progress(QStringLiteral("Writing HDPack (%1%)")
                     .arg(65 + int((written * 20) / qMax<qint64>(1, candidateBytes.size()))),
                 65 + int((written * 20) / qMax<qint64>(1, candidateBytes.size())));
#ifdef QT_TESTLIB_LIB
        if(request.pauseAfterPartialWriteForTest && written >= 1)
        {
            QFile marker(request.interruptionMarkerForTest);
            if(marker.open(QIODevice::WriteOnly))
                marker.write("partial-stage-written");
            for(;;) QThread::msleep(100);
        }
#endif
    }
    if(!output.flush())
    {
        result.error = QString("flush: Qt file error %1: %2").arg(int(output.error())).arg(output.errorString());
        output.cancelWriting();
        return result;
    }
    reportPhase(QStringLiteral("candidate-write-and-flush"));
    phaseTimer.restart();
    if(!resIo::commitWithRetry(output, destination.absoluteFilePath(), candidateBytes,
        [&](QString* error) {
            QByteArray current;
            if(!readFile(request.source, &current, error)) return false;
            if(current != sourceBytes) {
                if(error) *error = "mismatch: source RES changed before commit/retry";
                return false;
            }
            return destinationMatches(destination,destinationExisted,destinationBytes,error);
        }, &result.error, &result.commitAttempts)) return result;
    reportPhase(QStringLiteral("atomic-replace-and-durable-commit"));
    result.committed = true;

    result.phase = QStringLiteral("post-commit-validation");
    progress(QStringLiteral("Reopening and verifying HDPack"), 90);
    phaseTimer.restart();
    QByteArray committedBytes;
    if(!readFile(destination, &committedBytes, &result.error)
       || committedBytes != candidateBytes)
    {
        if(result.error.isEmpty())
            result.error = QStringLiteral(
                "mismatch: The committed RES bytes differ from the validated candidate.");
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
    reportPhase(QStringLiteral("post-commit-reopen-and-hashes"));

    result.phase = QStringLiteral("complete");
    result.success = true;
    progress(QStringLiteral("HDPack publication complete"), 100);
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
    result.phase = QStringLiteral("reader-drain");
    resIo::PublicationLease publication(destination.absoluteFilePath(), &result.error);
    if(!publication.acquired()) return result;
    result.phase = QStringLiteral("destination-check");
    const bool existed = QFileInfo(destination.absoluteFilePath()).exists();
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
    output.setDirectWriteFallback(false);
    if(!output.open(QIODevice::WriteOnly)
       || output.write(request.candidateBytes) != request.candidateBytes.size()
       || !resIo::commitWithRetry(output,destination.absoluteFilePath(),request.candidateBytes,
            [&](QString* error){return destinationMatches(destination,existed,original,error);},
            &result.error,&result.commitAttempts))
    {
        if(result.error.isEmpty()) result.error = output.errorString();
        return result;
    }
    result.committed = true;
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

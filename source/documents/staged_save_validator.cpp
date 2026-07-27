#include "staged_save_validator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>

#include "backup_manager.h"
#include "endurance_document.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"

namespace {
bool readFileBytes(const QFileInfo& info, QByteArray* bytes, QString* error)
{
    if(bytes)
        bytes->clear();
    const QFileInfo fresh(info.absoluteFilePath());
    if(!fresh.exists() || !fresh.isFile())
    {
        if(error)
            *error = QString("File does not exist or is not a regular file: %1")
                .arg(fresh.absoluteFilePath());
        return false;
    }
    QFile file(fresh.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    if(file.error() != QFileDevice::NoError)
    {
        if(error)
            *error = file.errorString();
        return false;
    }
    if(bytes)
        *bytes = data;
    return true;
}

bool destinationStillMatches(const QFileInfo& destination, bool existed,
                             const QByteArray& expected, QString* error)
{
    const QFileInfo fresh(destination.absoluteFilePath());
    if(!existed)
    {
        if(!fresh.exists())
            return true;
        if(error)
            *error = "The destination appeared after save validation began.";
        return false;
    }
    QByteArray current;
    if(!readFileBytes(fresh, &current, error))
        return false;
    if(current == expected)
        return true;
    if(error)
        *error = "The destination changed after save validation began.";
    return false;
}
}

SExistingFileComparison compareExistingFile(const QFileInfo& file,
                                             const QByteArray& expectedBytes)
{
    SExistingFileComparison result;
    result.exists = file.exists();
    if(!result.exists)
        return result;
    QFile input(file.absoluteFilePath());
    if(!input.open(QIODevice::ReadOnly))
    {
        result.error = input.errorString();
        return result;
    }
    const QByteArray bytes = input.readAll();
    if(input.error() != QFileDevice::NoError)
    {
        result.error = input.errorString();
        return result;
    }
    result.readable = true;
    result.equal = bytes == expectedBytes;
    return result;
}

SStagedSaveValidation validateStagedSave(const QByteArray& data,
                                         const QFileInfo& destination)
{
    SStagedSaveValidation result;
    result.size = data.size();
    result.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());

    const QString suffix = destination.suffix().toLower();
    if(suffix != "mob" && suffix != "mpr")
    {
        result.error = "The destination must have a .mob or .mpr extension.";
        return result;
    }

    const QDir destinationDir(destination.absolutePath());
    if(!destinationDir.exists())
    {
        result.error = "The destination directory does not exist: " + destinationDir.absolutePath();
        return result;
    }

    QTemporaryFile stage(destinationDir.filePath(
        QString(".EIMapperEndurance-stage-XXXXXX.%1").arg(suffix)));
    stage.setAutoRemove(true);
    if(!stage.open())
    {
        result.error = "Could not create the staged file: " + stage.errorString();
        return result;
    }
    if(stage.write(data) != data.size() || !stage.flush())
    {
        result.error = "Could not write the complete staged file: " + stage.errorString();
        return result;
    }
    const QFileInfo stagedInfo(stage.fileName());
    stage.close();

    CValidationReport report;
    QByteArray reopened;
    QByteArray reserialized;
    bool loaded = false;
    if(suffix == "mob")
    {
        CMobDocument document;
        loaded = document.load(stagedInfo, &report);
        reopened = document.originalBytes();
        reserialized = document.serialize();
    }
    else
    {
        CMprDocument document;
        loaded = document.load(stagedInfo, &report);
        reopened = document.originalBytes();
        reserialized = document.serialize();
    }

    if(!loaded || report.hasErrors())
    {
        result.error = "Fresh validation rejected the staged file:\n" + report.toText().trimmed();
        return result;
    }
    if(reopened != data || reserialized != data)
    {
        result.error = "The staged file changed when it was reopened and serialized.";
        return result;
    }

    result.success = true;
    return result;
}

SStagedMprTransactionValidation validateStagedMprTransaction(
    const CMprDocument& baseline, const CMprEditTransaction& transaction,
    const QFileInfo& destination)
{
    SStagedMprTransactionValidation result;
    CValidationReport transactionReport;
    if(!baseline.applyTransaction(transaction, &result.candidateBytes,
                                  &transactionReport))
    {
        result.error = "The MPR transaction was refused:\n"
            + transactionReport.toText().trimmed();
        result.candidateBytes.clear();
        return result;
    }

    const SStagedSaveValidation staged =
        validateStagedSave(result.candidateBytes, destination);
    result.sha256 = staged.sha256;
    result.size = staged.size;
    if(!staged.success)
    {
        result.error = staged.error;
        result.candidateBytes.clear();
        return result;
    }

    CMprDocument candidate;
    CValidationReport candidateReport;
    if(!candidate.loadBytes(result.candidateBytes,
                            QStringLiteral("validated-staged-candidate.mpr"),
                            &candidateReport)
       || candidateReport.hasErrors())
    {
        result.error = "The staged candidate failed semantic reopen:\n"
            + candidateReport.toText().trimmed();
        result.candidateBytes.clear();
        return result;
    }

    // A changed MPR must not introduce contradictory copies of a shared land
    // vertex. The archive/member parser can reopen such a file successfully,
    // but the renderer will split the terrain mesh and later sculpt commands
    // must refuse the ambiguous vertex. Preserve pre-existing source conflicts
    // losslessly, while rejecting any newly introduced conflict.
    CTerrainEditModel baselineTerrain;
    CTerrainEditModel candidateTerrain;
    CValidationReport baselineTerrainReport;
    CValidationReport candidateTerrainReport;
    if(!baselineTerrain.load(baseline, &baselineTerrainReport)
       || !candidateTerrain.load(candidate, &candidateTerrainReport))
    {
        result.error = "The staged candidate failed canonical terrain validation:\n"
            + candidateTerrainReport.toText().trimmed();
        result.candidateBytes.clear();
        return result;
    }
    if(!baselineTerrain.hasSeamConflicts() && candidateTerrain.hasSeamConflicts())
    {
        result.error = QStringLiteral(
            "The staged MPR introduced conflicting shared SEC land vertices. "
            "Saving was blocked to prevent terrain holes or seams.");
        result.candidateBytes.clear();
        return result;
    }

    QSet<QString> allowedChanges;
    QSet<QString> requiredChanges;
    for(const SMprMemberEdit& edit : transaction.edits())
    {
        const QString key = edit.memberName.toCaseFolded();
        allowedChanges.insert(key);
        if(baseline.memberPayload(edit.memberName) != edit.replacementPayload)
            requiredChanges.insert(key);
    }

    const CMprSemanticDiff diff = CMprSemanticDiff::compare(baseline, candidate);
    result.semanticDiff = diff.toText();
    QSet<QString> observedChanges;
    for(const SMprDocumentDiffEntry& entry : diff.entries())
    {
        const QString key = entry.memberName.toCaseFolded();
        if(entry.kind != QStringLiteral("modified") || !allowedChanges.contains(key))
        {
            result.error = QString("The staged MPR semantic diff exceeded the transaction scope: %1")
                .arg(result.semanticDiff);
            result.candidateBytes.clear();
            return result;
        }
        observedChanges.insert(key);
        result.changedMembers.append(entry.memberName);
    }
    if(observedChanges != requiredChanges)
    {
        result.error = QStringLiteral(
            "The staged MPR semantic diff did not match the requested changed members.");
        result.candidateBytes.clear();
        return result;
    }

    result.success = true;
    return result;
}

SMprSaveCommitResult commitStagedMprTransaction(
    const CMprDocument& baseline, const CMprEditTransaction& transaction,
    const SMprSaveCommitRequest& request)
{
    SMprSaveCommitResult result;
    result.phase = QStringLiteral("source-check");
    if(request.source.absoluteFilePath().isEmpty()
       || request.destination.absoluteFilePath().isEmpty())
    {
        result.error = "Source and destination paths are required.";
        return result;
    }

    QByteArray sourceBytes;
    if(!readFileBytes(request.source, &sourceBytes, &result.error))
        return result;
    if(sourceBytes != baseline.originalBytes())
    {
        result.error = "The loaded source MPR changed on disk. Reopen it before saving.";
        return result;
    }

    const QFileInfo initialDestination(request.destination.absoluteFilePath());
    const bool destinationExisted = initialDestination.exists();
    QByteArray destinationBytes;
    if(destinationExisted
       && !readFileBytes(initialDestination, &destinationBytes, &result.error))
    {
        result.phase = QStringLiteral("destination-check");
        return result;
    }

    result.phase = QStringLiteral("staging");
    const SStagedMprTransactionValidation staged =
        validateStagedMprTransaction(baseline, transaction, request.destination);
    result.candidateSha256 = staged.sha256;
    result.changedMembers = staged.changedMembers;
    result.semanticDiff = staged.semanticDiff;
    if(!staged.success)
    {
        result.error = staged.error;
        return result;
    }

    result.phase = QStringLiteral("pre-backup-race-check");
    QByteArray currentSource;
    if(!readFileBytes(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = "The source MPR changed while the candidate was staged.";
        return result;
    }
    if(!destinationStillMatches(request.destination, destinationExisted,
                                destinationBytes, &result.error))
        return result;

    if(destinationExisted)
    {
        result.phase = QStringLiteral("backup");
        const SBackupResult backup =
            CBackupManager(request.backupRootOverride).snapshotBeforeSave(initialDestination);
        if(!backup.success)
        {
            result.error = backup.error;
            return result;
        }
        const QString destinationSha256 = QString::fromLatin1(
            QCryptographicHash::hash(destinationBytes, QCryptographicHash::Sha256).toHex());
        if(backup.sha256.compare(destinationSha256, Qt::CaseInsensitive) != 0)
        {
            result.error = "The pre-save backup hash did not match the destination.";
            return result;
        }
        result.backupPath = backup.backupPath;
    }

    result.phase = QStringLiteral("pre-commit-race-check");
    if(!readFileBytes(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = "The source MPR changed before atomic replacement.";
        return result;
    }
    if(!destinationStillMatches(request.destination, destinationExisted,
                                destinationBytes, &result.error))
        return result;

#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = QStringLiteral("atomic-replacement");
        result.error = "Simulated atomic replacement failure.";
        return result;
    }
#endif

    result.phase = QStringLiteral("atomic-replacement");
    QSaveFile output(request.destination.absoluteFilePath());
    if(!output.open(QIODevice::WriteOnly))
    {
        result.error = output.errorString();
        return result;
    }
    if(output.write(staged.candidateBytes) != staged.candidateBytes.size())
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
    if(!readFileBytes(request.destination, &committedBytes, &result.error))
        return result;
    if(committedBytes != staged.candidateBytes)
    {
        result.error = "The committed MPR bytes do not match the validated candidate. "
            "The pre-save backup has been retained for recovery.";
        return result;
    }
    CMprDocument committed;
    CValidationReport committedReport;
    if(!committed.loadBytes(committedBytes,
                            request.destination.absoluteFilePath(),
                            &committedReport)
       || committedReport.hasErrors())
    {
        result.error = "The committed MPR failed a fresh reopen. "
            "The pre-save backup has been retained for recovery:\n"
            + committedReport.toText().trimmed();
        return result;
    }

    result.phase = QStringLiteral("complete");
    result.success = true;
    return result;
}

SMprSaveCommitResult commitStagedMprCanvasResize(
    const CMprDocument& diskBaseline, const CMprDocument& resizeBaseline,
    const SMprCanvasResizeRequest& resize,
    const SMprSaveCommitRequest& request)
{
    SMprSaveCommitResult result;
    result.phase = QStringLiteral("source-check");
    if(request.source.absoluteFilePath().isEmpty()
       || request.destination.absoluteFilePath().isEmpty())
    {
        result.error = "Source and destination paths are required.";
        return result;
    }
    if(QDir::cleanPath(request.source.absoluteFilePath()).compare(
           QDir::cleanPath(request.destination.absoluteFilePath()),
           Qt::CaseInsensitive) == 0)
    {
        result.error = "Canvas resize is Save As-only and cannot overwrite the open MPR.";
        return result;
    }

    QByteArray sourceBytes;
    if(!readFileBytes(request.source, &sourceBytes, &result.error))
        return result;
    if(sourceBytes != diskBaseline.originalBytes())
    {
        result.error = "The loaded source MPR changed on disk. Reopen it before resizing.";
        return result;
    }

    const QFileInfo initialDestination(request.destination.absoluteFilePath());
    const bool destinationExisted = initialDestination.exists();
    QByteArray destinationBytes;
    if(destinationExisted
       && !readFileBytes(initialDestination, &destinationBytes, &result.error))
    {
        result.phase = QStringLiteral("destination-check");
        return result;
    }

    result.phase = QStringLiteral("staging");
    QByteArray candidateBytes;
    CValidationReport resizeReport;
    if(!resizeBaseline.buildResizedCanvas(resize, &candidateBytes, &resizeReport)
       || resizeReport.hasErrors())
    {
        result.error = "The MPR canvas resize was refused:\n" + resizeReport.toText().trimmed();
        return result;
    }
    const SStagedSaveValidation staged = validateStagedSave(
        candidateBytes, request.destination);
    result.candidateSha256 = staged.sha256;
    if(!staged.success)
    {
        result.error = staged.error;
        return result;
    }

    CMprDocument candidate;
    CValidationReport candidateReport;
    CTerrainEditModel baselineTerrain;
    CTerrainEditModel candidateTerrain;
    CValidationReport baselineTerrainReport;
    CValidationReport candidateTerrainReport;
    if(!candidate.loadBytes(candidateBytes, QStringLiteral("resized-stage.mpr"),
                            &candidateReport)
       || candidateReport.hasErrors()
       || !baselineTerrain.load(resizeBaseline, &baselineTerrainReport)
       || !candidateTerrain.load(candidate, &candidateTerrainReport))
    {
        result.error = "The resized MPR failed canonical terrain validation:\n"
            + candidateReport.toText().trimmed() + "\n"
            + candidateTerrainReport.toText().trimmed();
        return result;
    }
    if(!baselineTerrain.hasSeamConflicts() && candidateTerrain.hasSeamConflicts())
    {
        result.error = "The resized MPR introduced conflicting shared SEC vertices.";
        return result;
    }
    result.changedMembers.append(resizeBaseline.mapInfo().memberName);
    result.semanticDiff = QString("canvas %1 x %2 -> %3 x %4; old canvas offset %5, %6")
        .arg(resizeBaseline.mapInfo().sectorCountX)
        .arg(resizeBaseline.mapInfo().sectorCountY)
        .arg(resize.sectorCountX).arg(resize.sectorCountY)
        .arg(resize.oldSectorOffsetX).arg(resize.oldSectorOffsetY);

    result.phase = QStringLiteral("pre-backup-race-check");
    QByteArray currentSource;
    if(!readFileBytes(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = "The source MPR changed while the resized candidate was staged.";
        return result;
    }
    if(!destinationStillMatches(request.destination, destinationExisted,
                                destinationBytes, &result.error))
        return result;

    if(destinationExisted)
    {
        result.phase = QStringLiteral("backup");
        const SBackupResult backup =
            CBackupManager(request.backupRootOverride).snapshotBeforeSave(initialDestination);
        if(!backup.success)
        {
            result.error = backup.error;
            return result;
        }
        const QString destinationSha256 = QString::fromLatin1(
            QCryptographicHash::hash(destinationBytes, QCryptographicHash::Sha256).toHex());
        if(backup.sha256.compare(destinationSha256, Qt::CaseInsensitive) != 0)
        {
            result.error = "The pre-save backup hash did not match the resize destination.";
            return result;
        }
        result.backupPath = backup.backupPath;
    }

    result.phase = QStringLiteral("pre-commit-race-check");
    if(!readFileBytes(request.source, &currentSource, &result.error)
       || currentSource != sourceBytes)
    {
        if(result.error.isEmpty())
            result.error = "The source MPR changed before atomic replacement.";
        return result;
    }
    if(!destinationStillMatches(request.destination, destinationExisted,
                                destinationBytes, &result.error))
        return result;

#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = QStringLiteral("atomic-replacement");
        result.error = "Simulated atomic replacement failure.";
        return result;
    }
#endif

    result.phase = QStringLiteral("atomic-replacement");
    QSaveFile output(request.destination.absoluteFilePath());
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
    if(!readFileBytes(request.destination, &committedBytes, &result.error))
        return result;
    if(committedBytes != candidateBytes)
    {
        result.error = "The committed resized MPR does not match the validated candidate.";
        return result;
    }
    CMprDocument committed;
    CValidationReport committedReport;
    if(!committed.loadBytes(committedBytes, request.destination.absoluteFilePath(),
                            &committedReport) || committedReport.hasErrors())
    {
        result.error = "The committed resized MPR failed a fresh reopen:\n"
            + committedReport.toText().trimmed();
        return result;
    }
    result.phase = QStringLiteral("complete");
    result.success = true;
    return result;
}

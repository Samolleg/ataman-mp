#include "staged_save_validator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryFile>

#include "endurance_document.h"
#include "mpr_document.h"

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

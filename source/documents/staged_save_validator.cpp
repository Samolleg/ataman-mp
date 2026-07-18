#include "staged_save_validator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryFile>

#include "endurance_document.h"
#include "mpr_document.h"

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

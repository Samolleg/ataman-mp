#include "backup_manager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include "log.h"

namespace {
QString sanitizedName(const QString& name)
{
    QString result = name;
    result.replace(QRegExp("[^A-Za-z0-9._-]"), "_");
    return result.isEmpty() ? QString("document") : result;
}

bool writeAtomically(const QString& path, const QByteArray& data, QString& error)
{
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly))
    {
        error = output.errorString();
        return false;
    }
    if(output.write(data) != data.size())
    {
        error = output.errorString();
        output.cancelWriting();
        return false;
    }
    if(!output.commit())
    {
        error = output.errorString();
        return false;
    }
    return true;
}
}

CBackupManager::CBackupManager(const QString& rootOverride)
    : m_rootPath(rootOverride.isEmpty() ? defaultRootPath() : QDir::cleanPath(rootOverride))
{
}

QString CBackupManager::defaultRootPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("EI Mapper Endurance/backups");
}

QString CBackupManager::sourceDirectory(const QFileInfo& source) const
{
    QString identity = source.absoluteFilePath();
#ifdef Q_OS_WIN
    identity = identity.toLower();
#endif
    const QString pathHash = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(m_rootPath).filePath(pathHash);
}

SBackupResult CBackupManager::snapshotOnOpen(const QFileInfo& source) const
{
    return createSnapshot(source, "open", true);
}

SBackupResult CBackupManager::snapshotBeforeSave(const QFileInfo& source) const
{
    return createSnapshot(source, "pre-save", false);
}

SBackupResult CBackupManager::snapshotAutosave(const QFileInfo& source,
                                               const QByteArray& candidate) const
{
    SBackupResult result;
    if(!source.exists() || !source.isFile() || candidate.isEmpty())
    {
        result.error = QString("Cannot autosave an unavailable or empty document: %1")
            .arg(source.absoluteFilePath());
        return result;
    }
    QFile current(source.absoluteFilePath());
    if(!current.open(QIODevice::ReadOnly))
    {
        result.error = QString("Cannot read autosave source: %1").arg(current.errorString());
        return result;
    }
    const QByteArray sourceData = current.readAll();
    const QString sourceHash = QCryptographicHash::hash(sourceData,
        QCryptographicHash::Sha256).toHex();
    result.sha256 = QCryptographicHash::hash(candidate,
        QCryptographicHash::Sha256).toHex();
    if(result.sha256 == sourceHash)
    {
        result.success = true;
        result.skipped = true;
        return result;
    }

    const QString directoryPath = sourceDirectory(source);
    if(!QDir().mkpath(directoryPath))
    {
        result.error = QString("Cannot create autosave directory: %1").arg(directoryPath);
        return result;
    }
    QDir directory(directoryPath);
    const QFileInfoList existing = directory.entryInfoList(
        QStringList() << "*-autosave-*.json", QDir::Files, QDir::Time);
    if(!existing.isEmpty())
    {
        QFile metadataFile(existing.first().absoluteFilePath());
        if(metadataFile.open(QIODevice::ReadOnly))
        {
            const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
            if(metadata.value("candidateSha256").toString() == result.sha256)
            {
                result.success = true;
                result.skipped = true;
                result.backupPath = metadata.value("backupPath").toString();
                return result;
            }
        }
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
    const QString baseName = QString("%1-autosave-%2-%3")
        .arg(stamp, result.sha256.left(16), sanitizedName(source.fileName()));
    const QString autosavePath = directory.filePath(baseName);
    QString error;
    if(!writeAtomically(autosavePath, candidate, error))
    {
        result.error = QString("Cannot write autosave: %1").arg(error);
        return result;
    }
    QJsonObject metadata;
    metadata.insert("schema", 1);
    metadata.insert("event", "autosave");
    metadata.insert("createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    metadata.insert("sourcePath", source.absoluteFilePath());
    metadata.insert("sourceSha256", sourceHash);
    metadata.insert("candidateSha256", result.sha256);
    metadata.insert("backupPath", autosavePath);
    if(!writeAtomically(autosavePath + ".json",
                        QJsonDocument(metadata).toJson(QJsonDocument::Indented), error))
    {
        QFile::remove(autosavePath);
        result.error = QString("Cannot write autosave metadata: %1").arg(error);
        return result;
    }
    result.success = true;
    result.backupPath = autosavePath;
    const QFileInfoList generations = directory.entryInfoList(
        QStringList() << "*-autosave-*", QDir::Files, QDir::Time);
    int retainedPayloads = 0;
    for(const QFileInfo& generation : generations)
    {
        if(generation.fileName().endsWith(".json", Qt::CaseInsensitive))
            continue;
        if(++retainedPayloads > m_maxAutosaves)
        {
            QFile::remove(generation.absoluteFilePath());
            QFile::remove(generation.absoluteFilePath() + ".json");
        }
    }
    ei::log(eLogInfo, QString("Autosave generation created source:%1 candidateSha256:%2 path:%3")
        .arg(source.absoluteFilePath(), result.sha256, autosavePath));
    return result;
}

QVector<SRecoverableAutosave> CBackupManager::recoverableAutosaves()
{
    QVector<SRecoverableAutosave> result;
    QDirIterator iterator(defaultRootPath(), QStringList() << "*-autosave-*.json",
                          QDir::Files, QDirIterator::Subdirectories);
    while(iterator.hasNext())
    {
        QFile metadataFile(iterator.next());
        if(!metadataFile.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
        const QString sourcePath = metadata.value("sourcePath").toString();
        const QString autosavePath = metadata.value("backupPath").toString();
        if(sourcePath.isEmpty() || autosavePath.isEmpty() || !QFileInfo::exists(autosavePath))
            continue;
        QFile source(sourcePath);
        if(!source.open(QIODevice::ReadOnly))
            continue;
        const QString currentHash = QCryptographicHash::hash(source.readAll(),
            QCryptographicHash::Sha256).toHex();
        if(currentHash != metadata.value("sourceSha256").toString())
            continue;
        SRecoverableAutosave recovery;
        recovery.sourcePath = sourcePath;
        recovery.autosavePath = autosavePath;
        recovery.createdUtc = metadata.value("createdUtc").toString();
        recovery.candidateSha256 = metadata.value("candidateSha256").toString();
        result.append(recovery);
    }
    return result;
}

SBackupResult CBackupManager::createSnapshot(const QFileInfo& source, const QString& event, bool deduplicate) const
{
    SBackupResult result;
    if(!source.exists() || !source.isFile())
    {
        result.error = QString("Backup source does not exist: %1").arg(source.absoluteFilePath());
        return result;
    }

    QFile input(source.absoluteFilePath());
    if(!input.open(QIODevice::ReadOnly))
    {
        result.error = QString("Cannot read backup source: %1").arg(input.errorString());
        return result;
    }
    const QByteArray data = input.readAll();
    if(input.error() != QFile::NoError)
    {
        result.error = QString("Cannot finish reading backup source: %1").arg(input.errorString());
        return result;
    }

    result.sha256 = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    const QString directoryPath = sourceDirectory(source);
    if(!QDir().mkpath(directoryPath))
    {
        result.error = QString("Cannot create backup directory: %1").arg(directoryPath);
        return result;
    }

    const QString safeName = sanitizedName(source.fileName());
    QString baseName;
    if(deduplicate)
        baseName = QString("open-%1-%2").arg(result.sha256, safeName);
    else
        baseName = QString("%1-%2-%3-%4")
            .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz"), event, safeName, result.sha256.left(16));

    QString backupPath = QDir(directoryPath).filePath(baseName + ".backup");
    if(deduplicate && QFileInfo::exists(backupPath))
    {
        result.success = true;
        result.skipped = true;
        result.backupPath = backupPath;
        ei::log(eLogInfo, QString("Backup snapshot already present event:%1 source:%2 sha256:%3 path:%4")
            .arg(event, source.absoluteFilePath(), result.sha256, backupPath));
        return result;
    }
    if(!deduplicate)
    {
        int suffix = 1;
        while(QFileInfo::exists(backupPath))
            backupPath = QDir(directoryPath).filePath(QString("%1-%2.backup").arg(baseName).arg(suffix++));
    }

    QString error;
    if(!writeAtomically(backupPath, data, error))
    {
        result.error = QString("Cannot write backup: %1").arg(error);
        return result;
    }

    QJsonObject metadata;
    metadata.insert("schema", 1);
    metadata.insert("event", event);
    metadata.insert("createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    metadata.insert("sourcePath", source.absoluteFilePath());
    metadata.insert("sourceSize", double(data.size()));
    metadata.insert("sourceSha256", result.sha256);
    metadata.insert("backupFile", QFileInfo(backupPath).fileName());
    const QString metadataPath = backupPath + ".json";
    if(!writeAtomically(metadataPath, QJsonDocument(metadata).toJson(QJsonDocument::Indented), error))
    {
        QFile::remove(backupPath);
        result.error = QString("Cannot write backup metadata: %1").arg(error);
        return result;
    }

    result.success = true;
    result.backupPath = backupPath;
    ei::log(eLogInfo, QString("Backup created event:%1 source:%2 size:%3 sha256:%4 path:%5")
        .arg(event, source.absoluteFilePath()).arg(data.size()).arg(result.sha256, backupPath));
    if(!deduplicate)
        enforceRetention(directoryPath);
    return result;
}

void CBackupManager::enforceRetention(const QString& directoryPath) const
{
    QDir directory(directoryPath);
    const QFileInfoList timestamped = directory.entryInfoList(
        QStringList() << "*-pre-save-*.backup", QDir::Files, QDir::Time);
    for(int index = m_maxTimestampedBackups; index < timestamped.size(); ++index)
    {
        const QString backupPath = timestamped.at(index).absoluteFilePath();
        if(QFile::remove(backupPath))
            QFile::remove(backupPath + ".json");
        else
            ei::log(eLogWarning, QString("Cannot prune old backup:%1").arg(backupPath));
    }
}

bool CBackupManager::openBackupFolder(QString* error)
{
    const QString root = defaultRootPath();
    if(!QDir().mkpath(root))
    {
        if(error)
            *error = QString("Cannot create backup directory: %1").arg(root);
        return false;
    }
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(root)))
    {
        if(error)
            *error = QString("Cannot open backup directory: %1").arg(root);
        return false;
    }
    return true;
}

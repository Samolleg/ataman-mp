#include "backup_manager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
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

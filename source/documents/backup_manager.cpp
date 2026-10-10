#include "backup_manager.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegExp>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

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

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString sessionJournalPath(const QString& root)
{
    return QDir(root).filePath("editor-session.json");
}

QString notificationJournalPath(const QString& root)
{
    return QDir(root).filePath("recovery-notifications.json");
}

QString recoveryNotificationKey(const QString& sourcePath,
                                const QString& candidateSha256,
                                const QString& createdUtc)
{
    QString normalized = QDir::cleanPath(sourcePath);
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return QCryptographicHash::hash(
        QString("%1\n%2\n%3").arg(normalized, candidateSha256, createdUtc).toUtf8(),
        QCryptographicHash::Sha256).toHex();
}

QSet<QString> acknowledgedRecoveryKeys(const QString& root)
{
    QSet<QString> keys;
    const QJsonArray entries = readJsonObject(notificationJournalPath(root))
        .value("keys").toArray();
    for(const QJsonValue& entry : entries)
        if(!entry.toString().isEmpty())
            keys.insert(entry.toString());
    return keys;
}

QString activeSessionId(const QString& root)
{
    const QJsonObject journal = readJsonObject(sessionJournalPath(root));
    return journal.value("state").toString() == "active"
        ? journal.value("sessionId").toString() : QString();
}
}

CBackupManager::CBackupManager(const QString& rootOverride)
    : m_rootPath(rootOverride.isEmpty() ? defaultRootPath() : QDir::cleanPath(rootOverride))
{
}

QString CBackupManager::defaultRootPath()
{
    // Native save tests supply a process-owned root before entering main.
    // Invalid test configuration must never fall back to the user's backups.
    const QString overrideRoot = qEnvironmentVariable("EI_MAPPER_TEST_BACKUP_ROOT");
    if(!overrideRoot.isEmpty())
    {
        if(!QDir::isAbsolutePath(overrideRoot))
            qFatal("EI_MAPPER_TEST_BACKUP_ROOT must be an absolute path");
        return QDir::cleanPath(overrideRoot);
    }

    // Built-in checks also save documents. Qt's shared qttest directory is
    // not private to this process, so test mode alone is not sufficient.
    bool testRun = QStandardPaths::isTestModeEnabled();
    if(QCoreApplication::instance())
        for(const QString& argument : QCoreApplication::arguments().mid(1))
        {
            const QString option = argument.section(QLatin1Char('='), 0, 0);
            testRun = testRun || (option.startsWith(QStringLiteral("--"))
                                 && option.endsWith(QStringLiteral("-check")));
        }
    if(testRun)
    {
        static QTemporaryDir temporary(QDir::tempPath()
            + QStringLiteral("/ei-mapper-test-backups-XXXXXX"));
        if(!temporary.isValid())
            qFatal("Cannot create an isolated test backup directory");
        return temporary.filePath(QStringLiteral("backups"));
    }

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
    const QString sessionId = activeSessionId(m_rootPath);
    if(!sessionId.isEmpty())
        metadata.insert("sessionId", sessionId);
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
    return CBackupManager().recoverableAutosavesForRoot();
}

QVector<SRecoverableAutosave> CBackupManager::recoverableAutosavesForRoot() const
{
    QVector<SRecoverableAutosave> result;
    QDirIterator iterator(m_rootPath, QStringList() << "*-autosave-*.json",
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
        recovery.sessionId = metadata.value("sessionId").toString();
        recovery.notificationKey = recoveryNotificationKey(
            recovery.sourcePath, recovery.candidateSha256, recovery.createdUtc);
        result.append(recovery);
    }
    return result;
}

bool CBackupManager::beginEditorSession(QVector<SRecoverableAutosave>* recoveries,
                                        QString* error) const
{
    if(recoveries)
        recoveries->clear();
    if(!QDir().mkpath(m_rootPath))
    {
        if(error)
            *error = QString("Cannot create backup session directory: %1").arg(m_rootPath);
        return false;
    }

    const QString journalPath = sessionJournalPath(m_rootPath);
    const QJsonObject previous = readJsonObject(journalPath);
    const bool previousUnclean = previous.value("state").toString() == "active";
    const QString previousSessionId = previous.value("sessionId").toString();
    QSet<QString> pendingKeys;
    for(const QJsonValue& value : previous.value("pendingRecoveryKeys").toArray())
        if(!value.toString().isEmpty())
            pendingKeys.insert(value.toString());

    QVector<SRecoverableAutosave> newlyRecoverable;
    if(previousUnclean)
    {
        const QSet<QString> acknowledged = acknowledgedRecoveryKeys(m_rootPath);
        QDateTime previousStarted = QDateTime::fromString(
            previous.value("startedUtc").toString(), Qt::ISODateWithMs);
        if(!previousStarted.isValid())
            previousStarted = QDateTime::fromString(
                previous.value("startedUtc").toString(), Qt::ISODate);
        for(const SRecoverableAutosave& recovery : recoverableAutosavesForRoot())
        {
            bool belongsToUncleanSession = pendingKeys.contains(recovery.notificationKey)
                || (!previousSessionId.isEmpty()
                    && recovery.sessionId == previousSessionId);
            // Autosaves made by builds predating session IDs can still be
            // recovered, but only when their timestamp falls inside the
            // unfinished session rather than merely existing in the folder.
            if(!belongsToUncleanSession && recovery.sessionId.isEmpty()
               && previousStarted.isValid())
            {
                QDateTime created = QDateTime::fromString(
                    recovery.createdUtc, Qt::ISODateWithMs);
                if(!created.isValid())
                    created = QDateTime::fromString(recovery.createdUtc, Qt::ISODate);
                belongsToUncleanSession = created.isValid() && created >= previousStarted;
            }
            if(belongsToUncleanSession
               && !acknowledged.contains(recovery.notificationKey))
                newlyRecoverable.append(recovery);
        }
    }

    QJsonArray carriedKeys;
    for(const SRecoverableAutosave& recovery : newlyRecoverable)
        carriedKeys.append(recovery.notificationKey);
    QJsonObject current;
    current.insert("schema", 1);
    current.insert("state", "active");
    current.insert("sessionId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    current.insert("startedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    current.insert("pendingRecoveryKeys", carriedKeys);
    QString writeError;
    if(!writeAtomically(journalPath,
                        QJsonDocument(current).toJson(QJsonDocument::Indented), writeError))
    {
        if(error)
            *error = QString("Cannot write editor backup session journal: %1").arg(writeError);
        return false;
    }
    if(recoveries)
        *recoveries = newlyRecoverable;
    return true;
}

bool CBackupManager::acknowledgeRecoveries(const QStringList& notificationKeys,
                                           QString* error) const
{
    if(notificationKeys.isEmpty())
        return true;
    QSet<QString> acknowledged = acknowledgedRecoveryKeys(m_rootPath);
    for(const QString& key : notificationKeys)
        if(!key.isEmpty())
            acknowledged.insert(key);
    QStringList orderedKeys = acknowledged.values();
    orderedKeys.sort();
    QJsonArray entries;
    for(const QString& key : orderedKeys)
        entries.append(key);
    QJsonObject notifications;
    notifications.insert("schema", 1);
    notifications.insert("updatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    notifications.insert("keys", entries);
    QString writeError;
    if(!writeAtomically(notificationJournalPath(m_rootPath),
                        QJsonDocument(notifications).toJson(QJsonDocument::Indented), writeError))
    {
        if(error)
            *error = QString("Cannot acknowledge recovery notification: %1").arg(writeError);
        return false;
    }

    QJsonObject session = readJsonObject(sessionJournalPath(m_rootPath));
    if(session.value("state").toString() == "active")
    {
        QSet<QString> acknowledgedNow;
        for(const QString& key : notificationKeys)
            acknowledgedNow.insert(key);
        QJsonArray remaining;
        for(const QJsonValue& value : session.value("pendingRecoveryKeys").toArray())
            if(!acknowledgedNow.contains(value.toString()))
                remaining.append(value);
        session.insert("pendingRecoveryKeys", remaining);
        if(!writeAtomically(sessionJournalPath(m_rootPath),
                            QJsonDocument(session).toJson(QJsonDocument::Indented), writeError))
        {
            if(error)
                *error = QString("Cannot update editor backup session journal: %1").arg(writeError);
            return false;
        }
    }
    return true;
}

bool CBackupManager::finishEditorSession(QString* error) const
{
    QJsonObject session = readJsonObject(sessionJournalPath(m_rootPath));
    if(session.isEmpty() || session.value("state").toString() != "active")
        return true;
    session.insert("state", "clean");
    session.insert("closedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    session.insert("pendingRecoveryKeys", QJsonArray());
    QString writeError;
    if(!writeAtomically(sessionJournalPath(m_rootPath),
                        QJsonDocument(session).toJson(QJsonDocument::Indented), writeError))
    {
        if(error)
            *error = QString("Cannot finish editor backup session journal: %1").arg(writeError);
        return false;
    }
    return true;
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
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);
    QByteArray chunk;
    while(!(chunk = input.read(4 * 1024 * 1024)).isEmpty())
        sourceHash.addData(chunk);
    if(input.error() != QFile::NoError)
    {
        result.error = QString("Cannot finish reading backup source: %1").arg(input.errorString());
        return result;
    }

    result.sha256 = sourceHash.result().toHex();
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

    input.close();
    if(!input.open(QIODevice::ReadOnly))
    {
        result.error = QString("Cannot reopen backup source: %1").arg(input.errorString());
        return result;
    }
    QSaveFile backupOutput(backupPath);
    if(!backupOutput.open(QIODevice::WriteOnly))
    {
        result.error = QString("Cannot create backup: %1").arg(backupOutput.errorString());
        return result;
    }
    while(!(chunk = input.read(4 * 1024 * 1024)).isEmpty())
    {
        if(backupOutput.write(chunk) != chunk.size())
        {
            backupOutput.cancelWriting();
            result.error = QString("Cannot write backup: %1").arg(backupOutput.errorString());
            return result;
        }
    }
    if(input.error() != QFile::NoError)
    {
        backupOutput.cancelWriting();
        result.error = QString("Cannot finish reading backup source: %1").arg(input.errorString());
        return result;
    }
    if(!backupOutput.commit())
    {
        result.error = QString("Cannot publish backup: %1").arg(backupOutput.errorString());
        return result;
    }

    QString error;
    QJsonObject metadata;
    metadata.insert("schema", 1);
    metadata.insert("event", event);
    metadata.insert("createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    metadata.insert("sourcePath", source.absoluteFilePath());
    metadata.insert("sourceSize", double(source.size()));
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
        .arg(event, source.absoluteFilePath()).arg(source.size()).arg(result.sha256, backupPath));
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

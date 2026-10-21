#include "backup_manager.h"

#include <QCryptographicHash>
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
#include <QUrl>
#include <QUuid>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <map>
#include <QSettings>
#include <QElapsedTimer>
#include <QtConcurrent>
#include "file_write_activity.h"

#include "log.h"
#include "res_archive_index.h"

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
    m_maxTimestampedBackups = qMax(1, QSettings().value("backups/generations", 3).toInt());
    m_minimumAgeDays = qMax(3, QSettings().value("backups/minimumAgeDays", 3).toInt());
}

QString CBackupManager::defaultRootPath()
{
    const QString isolated=qEnvironmentVariable("EI_MAPPER_BACKUP_ROOT");
    if(!isolated.isEmpty()&&QDir::isAbsolutePath(isolated))return QDir::cleanPath(isolated);
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
                                               const QByteArray& candidate,
                                               const QByteArray& terrainAtlas,
                                               const QString& expectedSourceSha256) const
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
    if(!expectedSourceSha256.isEmpty() && sourceHash != expectedSourceSha256)
    {
        result.error = QStringLiteral("The source changed while recovery was being prepared; this stale generation was not published.");
        return result;
    }
    result.sha256 = QCryptographicHash::hash(candidate,
        QCryptographicHash::Sha256).toHex();
    const QString atlasHash = terrainAtlas.isEmpty() ? QString()
        : QString::fromLatin1(QCryptographicHash::hash(terrainAtlas,
            QCryptographicHash::Sha256).toHex());
    if(!terrainAtlas.isEmpty() && source.suffix().compare("mpr", Qt::CaseInsensitive) != 0)
    {
        result.error = QStringLiteral("A paired terrain atlas requires an MPR document.");
        return result;
    }
    if(result.sha256 == sourceHash && terrainAtlas.isEmpty())
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
            if(metadata.value("candidateSha256").toString() == result.sha256
               && metadata.value("sourceSha256").toString() == sourceHash
               && metadata.value("terrainAtlasSha256").toString() == atlasHash
               && validateAutosave(QFileInfo(metadata.value("backupPath").toString())))
            {
                result.success = true;
                result.skipped = true;
                result.backupPath = metadata.value("backupPath").toString();
                return result;
            }
        }
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
    const QString generationHash = QCryptographicHash::hash(
        (result.sha256 + atlasHash).toLatin1(), QCryptographicHash::Sha256).toHex();
    const QString baseName = QString("%1-autosave-%2-%3")
        .arg(stamp, generationHash.left(16), sanitizedName(source.fileName()));
    const QString autosavePath = directory.filePath(baseName);
    const QString atlasPath = directory.filePath(
        QFileInfo(autosavePath).completeBaseName() + "-atlas.res");
    QString error;
    if(!terrainAtlas.isEmpty() && !writeAtomically(atlasPath, terrainAtlas, error))
    {
        result.error = QString("Cannot write autosave terrain atlas: %1").arg(error);
        return result;
    }
    if(!writeAtomically(autosavePath, candidate, error))
    {
        if(!terrainAtlas.isEmpty()) QFile::remove(atlasPath);
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
    if(!terrainAtlas.isEmpty())
    {
        metadata.insert("terrainAtlasPath", atlasPath);
        metadata.insert("terrainAtlasSha256", atlasHash);
    }
    const QString sessionId = activeSessionId(m_rootPath);
    if(!sessionId.isEmpty())
        metadata.insert("sessionId", sessionId);
    if(!writeAtomically(autosavePath + ".json",
                        QJsonDocument(metadata).toJson(QJsonDocument::Indented), error))
    {
        QFile::remove(autosavePath);
        if(!terrainAtlas.isEmpty()) QFile::remove(atlasPath);
        result.error = QString("Cannot write autosave metadata: %1").arg(error);
        return result;
    }
    result.success = true;
    result.backupPath = autosavePath;
    const QFileInfoList generations = directory.entryInfoList(
        QStringList() << "*-autosave-*.json", QDir::Files, QDir::Time);
    for(int index = m_maxAutosaves; index < generations.size(); ++index)
    {
        const QString payload = generations.at(index).absoluteFilePath().chopped(5);
        const QJsonObject old = readJsonObject(generations.at(index).absoluteFilePath());
        const QString oldAtlas = QFileInfo(payload).dir().filePath(
            QFileInfo(payload).completeBaseName() + "-atlas.res");
        // Invalidate the generation before removing either payload.
        if(QFile::remove(generations.at(index).absoluteFilePath()))
        {
            QFile::remove(payload);
            if(old.value("terrainAtlasPath").toString() == oldAtlas)
                QFile::remove(oldAtlas);
        }
    }
    ei::log(eLogInfo, QString("Autosave generation created source:%1 candidateSha256:%2 path:%3")
        .arg(source.absoluteFilePath(), result.sha256, autosavePath));
    return result;
}

bool CBackupManager::validateAutosave(const QFileInfo& payload, QString* error)
{
    const auto reject = [error]() {
        if(error) *error = QStringLiteral("The autosave generation is incomplete or its MPR/atlas checksum does not match. Keep the MPR, matching -atlas.res and .json together.");
        return false;
    };
    const QJsonObject metadata = readJsonObject(payload.absoluteFilePath() + ".json");
    if(metadata.value("event").toString() != "autosave"
       || QFileInfo(metadata.value("backupPath").toString()).fileName()
            != payload.fileName()) return reject();
    const auto exact = [](const QString& path, const QString& hash) {
        QFile input(path);
        return hash.size() == 64 && input.open(QIODevice::ReadOnly)
            && QCryptographicHash::hash(input.readAll(), QCryptographicHash::Sha256).toHex() == hash;
    };
    if(!exact(payload.absoluteFilePath(), metadata.value("candidateSha256").toString()))
        return reject();
    const QString atlasPath = metadata.value("terrainAtlasPath").toString();
    const QString atlasHash = metadata.value("terrainAtlasSha256").toString();
    if(!atlasPath.isEmpty() || !atlasHash.isEmpty())
    {
        const QString expected = payload.dir().filePath(payload.completeBaseName() + "-atlas.res");
        if(QFileInfo(atlasPath).fileName() != QFileInfo(expected).fileName()
           || !exact(expected, atlasHash)) return reject();
    }
    return true;
}

QVector<SRecoverableAutosave> CBackupManager::recoverableAutosaves()
{
    return CBackupManager().recoverableAutosavesForRoot();
}

QVector<SRecoverableAutosave> CBackupManager::recoverableAutosavesForRoot(const std::atomic_bool* cancelled) const
{
    QVector<SRecoverableAutosave> result;
    QMap<QString,QString> sourceHashes;
    QDirIterator iterator(m_rootPath, QStringList() << "*-autosave-*.json",
                          QDir::Files, QDirIterator::Subdirectories);
    while(iterator.hasNext())
    {
        if(cancelled && cancelled->load())return {};
        QFile metadataFile(iterator.next());
        if(!metadataFile.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
        const QString sourcePath = metadata.value("sourcePath").toString();
        const QString autosavePath = metadata.value("backupPath").toString();
        if(sourcePath.isEmpty() || autosavePath.isEmpty()
           || !validateAutosave(QFileInfo(autosavePath)))
            continue;
        if(!sourceHashes.contains(sourcePath)) {
            QFile source(sourcePath);
            if(!source.open(QIODevice::ReadOnly))continue;
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if(!hash.addData(&source))continue;
            sourceHashes.insert(sourcePath,QString::fromLatin1(hash.result().toHex()));
        }
        const QString currentHash=sourceHashes.value(sourcePath);
        if(currentHash != metadata.value("sourceSha256").toString())
            continue;
        SRecoverableAutosave recovery;
        recovery.sourcePath = sourcePath;
        recovery.autosavePath = autosavePath;
        recovery.terrainAtlasPath = metadata.value("terrainAtlasPath").toString();
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
                                        QString* error,QJsonObject* deferredScan) const
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
    const auto newlyRecoverable=deferredScan ? QVector<SRecoverableAutosave>() : discoverSessionRecoveries(previous);
    QJsonArray carriedKeys=deferredScan?previous.value("pendingRecoveryKeys").toArray():QJsonArray();
    for(const SRecoverableAutosave& recovery : newlyRecoverable)
        carriedKeys.append(recovery.notificationKey);
    QJsonObject current;
    current.insert("schema", 1);
    current.insert("state", "active");
    current.insert("sessionId", QUuid::createUuid().toString(QUuid::WithoutBraces));
    current.insert("startedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    current.insert("pendingRecoveryKeys", carriedKeys);
    if(deferredScan) {
        auto sessions=previous.value("recoveryScanSessions").toArray();
        if(previous.value("state").toString()=="active") {
            QJsonObject interrupted;interrupted["sessionId"]=previous.value("sessionId");
            interrupted["startedUtc"]=previous.value("startedUtc");sessions.append(interrupted);
        }
        current["recoveryScanSessions"]=sessions;
        *deferredScan=previous;
        deferredScan->insert("scanOwnerSessionId",current.value("sessionId"));
    }
    QString writeError;
    if(!writeAtomically(journalPath,QJsonDocument(current).toJson(QJsonDocument::Indented),writeError)) {
        if(error)*error=QString("Cannot write editor backup session journal: %1").arg(writeError);
        return false;
    }
    if(recoveries)*recoveries=newlyRecoverable;
    return true;
}

QVector<SRecoverableAutosave> CBackupManager::discoverSessionRecoveries(const QJsonObject& previous,const std::atomic_bool* cancelled) const
{
    const bool previousUnclean = previous.value("state").toString() == "active";
    auto sessions=previous.value("recoveryScanSessions").toArray();
    if(previousUnclean) sessions.append(previous);
    QSet<QString> pendingKeys;
    for(const QJsonValue& value : previous.value("pendingRecoveryKeys").toArray())
        if(!value.toString().isEmpty())
            pendingKeys.insert(value.toString());

    QVector<SRecoverableAutosave> newlyRecoverable;
    if(!sessions.isEmpty() || !pendingKeys.isEmpty())
    {
        const QSet<QString> acknowledged = acknowledgedRecoveryKeys(m_rootPath);
        for(const SRecoverableAutosave& recovery : recoverableAutosavesForRoot(cancelled))
        {
            bool belongsToUncleanSession = pendingKeys.contains(recovery.notificationKey);
            for(const auto& session:sessions) {
                const auto state=session.toObject();
                const auto previousSessionId=state.value("sessionId").toString();
                QDateTime previousStarted=QDateTime::fromString(state.value("startedUtc").toString(),Qt::ISODateWithMs);
                if(!previousStarted.isValid())previousStarted=QDateTime::fromString(state.value("startedUtc").toString(),Qt::ISODate);
                belongsToUncleanSession |= !previousSessionId.isEmpty() && recovery.sessionId==previousSessionId;
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
            }
            if(belongsToUncleanSession
               && !acknowledged.contains(recovery.notificationKey))
                newlyRecoverable.append(recovery);
        }
    }

    return newlyRecoverable;
}

bool CBackupManager::completeRecoveryScan(const QJsonObject& scan,const QStringList& keys,QString* error) const
{
    const auto path=sessionJournalPath(m_rootPath);auto current=readJsonObject(path);
    if(current.value("sessionId")!=scan.value("scanOwnerSessionId")) {
        if(error)*error="Recovery scan belongs to a different editor session.";return false;
    }
    auto pending=current.value("pendingRecoveryKeys").toArray();
    for(const auto& key:keys)if(!pending.contains(key))pending.append(key);
    current["pendingRecoveryKeys"]=pending;
    current.remove("recoveryScanSessions");
    QString writeError;
    if(!writeAtomically(path,QJsonDocument(current).toJson(),writeError)) {
        if(error)*error=writeError;return false;
    }
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

    CFileWriteActivity writing;
    const QString directoryPath = sourceDirectory(source);
    if(!QDir().mkpath(directoryPath)) { result.error="Cannot create backup directory."; return result; }
    QLockFile directoryLock(QDir(directoryPath).filePath("snapshots.lock"));
    if(!directoryLock.tryLock(5000)) { result.error="Another backup operation owns this source."; return result; }
    QFile input(source.absoluteFilePath());
    if(!input.open(QIODevice::ReadOnly))
    {
        result.error = QString("Cannot read backup source: %1").arg(input.errorString());
        return result;
    }
    QString hashError;
    result.sha256=QString::fromLatin1(resFileSha256(source.absoluteFilePath(),&hashError));
    if(result.sha256.isEmpty()){result.error="Cannot hash backup source: "+hashError;return result;}
    QByteArray chunk;


    const auto pin = [&result](const QString& path) {
        static QMutex mutex;
        static std::map<QString,std::weak_ptr<QLockFile>> leases;
        QMutexLocker guard(&mutex);
        auto lease=leases[path].lock();
        if(!lease) {
            lease=std::make_shared<QLockFile>(path+".in-use");
            if(!lease->tryLock(0))return false;
            leases[path]=lease;
        }
        result.lease = lease;
        return true;
    };
    if(event == "pre-save" && source.suffix().compare("res",Qt::CaseInsensitive)==0
       && source.size()>200LL*1024*1024) {
        const auto previous=QDir(directoryPath).entryInfoList({"*.backup"},QDir::Files|QDir::NoSymLinks,QDir::Name|QDir::Reversed);
        for(const auto& file:previous) {
            const auto metadata=readJsonObject(file.absoluteFilePath()+".json");
            if(metadata.value("sourceSha256").toString()!=result.sha256
               || metadata.value("sourceSize").toDouble()!=double(source.size())) continue;
            if(!pin(file.absoluteFilePath())) continue;
            if(resFileSha256(file.absoluteFilePath())!=result.sha256.toLatin1()) { result.lease.reset();continue; }
            result.success=result.skipped=true;result.backupPath=file.absoluteFilePath();
            ei::log(eLogInfo,"Unchanged large RES backup reused: "+result.backupPath);
            return result;
        }
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
        if(!pin(backupPath)) {result.error="Another editor is using the open snapshot.";return result;}
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
    metadata.insert("sourceMtimeMs", double(source.lastModified().toMSecsSinceEpoch()));
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
    if(!pin(backupPath)) { result.success=false; result.error="Cannot protect the operation backup."; }
    return result;
}

qint64 CBackupManager::enforceRetention(const QString& directoryPath, const QSet<QString>& pins, bool dryRun) const
{
    QLockFile lock(QDir(directoryPath).filePath("snapshots.lock"));
    if(!lock.tryLock(0)) return 0;
    const auto files=QDir(directoryPath).entryInfoList({"*.backup"},QDir::Files|QDir::NoSymLinks,QDir::Time);
    const auto cutoff=QDateTime::currentDateTimeUtc().addDays(-m_minimumAgeDays);
    qint64 freed=0;
    // Unknown/incomplete metadata is preserved. Autosaves and terrain recovery
    // live outside this file set and retain their existing recovery contracts.
    for(int i=m_maxTimestampedBackups;i<files.size();++i) {
        const auto& file=files[i];const auto path=file.absoluteFilePath();
        if(file.lastModified().toUTC()>cutoff || pins.contains(QDir::cleanPath(path).toCaseFolded())) continue;
        const auto metadata=readJsonObject(path+".json");
        const QString event=metadata.value("event").toString();
        if((event!="pre-save"&&event!="open") || metadata.value("backupFile").toString()!=file.fileName()) continue;
        if(sourceDirectory(QFileInfo(metadata.value("sourcePath").toString()))!=directoryPath) continue;
        QLockFile operation(path+".in-use");
        if(!operation.tryLock(0)) continue;
        if(dryRun || QFile::remove(path)) {
            freed+=file.size();if(!dryRun)QFile::remove(path+".json");
        }
    }
    return freed;
}

qint64 CBackupManager::collectBackups(bool dryRun) const
{
    QSet<QString> pins;
    QDir journals(QDir(m_rootPath).filePath("map-save-recovery"));
    for(const auto& file:journals.entryInfoList({"*.json"},QDir::Files)) {
        const auto journal=readJsonObject(file.absoluteFilePath());
        if(!journal.value("files").isArray()) return 0; // fail closed on unknown recovery
        for(const auto& value:journal.value("files").toArray()) {
            if(!value.isObject())return 0;
            const QString path=value.toObject().value("backup").toString();
            if(value.toObject().value("existed").toBool() && path.isEmpty())return 0;
            if(!path.isEmpty())pins.insert(QDir::cleanPath(path).toCaseFolded());
        }
    }
    qint64 freed=0;
    for(const auto& directory:QDir(m_rootPath).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks))
        if(QRegExp("[0-9a-f]{64}").exactMatch(directory.fileName()))
            freed+=enforceRetention(directory.absoluteFilePath(),pins,dryRun);
    return freed;
}

void CBackupManager::scheduleCleanup() const
{
    const auto manager=*this;
    QtConcurrent::run([manager]() {
        QElapsedTimer timer;timer.start();
        const auto bytes=manager.collectBackups();
        ei::log(eLogInfo,QString("Backup retention complete generations:%1 minimumAgeDays:%2 freedBytes:%3 elapsedMs:%4")
                .arg(manager.m_maxTimestampedBackups).arg(manager.m_minimumAgeDays).arg(bytes).arg(timer.elapsed()));
    });
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

#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <QSet>
#include <QJsonObject>
#include <atomic>

struct SRecoverableAutosave
{
    QString sourcePath;
    QString autosavePath;
    QString terrainAtlasPath;
    QString createdUtc;
    QString candidateSha256;
    QString sessionId;
    QString notificationKey;
};

struct SBackupResult
{
    bool success{false};
    bool skipped{false};
    QString backupPath;
    QString sha256;
    QString error;
    std::shared_ptr<void> lease; // held through the complete save transaction
};

class CBackupManager
{
public:
    explicit CBackupManager(const QString& rootOverride = QString());

    SBackupResult snapshotOnOpen(const QFileInfo& source) const;
    SBackupResult snapshotBeforeSave(const QFileInfo& source) const;
    SBackupResult snapshotAutosave(const QFileInfo& source, const QByteArray& candidate,
                                  const QByteArray& terrainAtlas = QByteArray(),
                                  const QString& expectedSourceSha256 = QString()) const;
    // The JSON commit record is published last; an incomplete pair is never
    // advertised or opened as a recoverable MPR.
    static bool validateAutosave(const QFileInfo& payload, QString* error = nullptr);

    QString rootPath() const { return m_rootPath; }
    static QString defaultRootPath();
    qint64 collectBackups(bool dryRun = false) const;
    void scheduleCleanup() const;
    static bool openBackupFolder(QString* error = nullptr);
    static QVector<SRecoverableAutosave> recoverableAutosaves();
    bool beginEditorSession(QVector<SRecoverableAutosave>* recoveries,
                            QString* error = nullptr,
                            QJsonObject* deferredScan = nullptr) const;
    QVector<SRecoverableAutosave> discoverSessionRecoveries(const QJsonObject& previous,
        const std::atomic_bool* cancelled = nullptr) const;
    bool completeRecoveryScan(const QJsonObject& scan, const QStringList& keys,
                              QString* error = nullptr) const;
    bool acknowledgeRecoveries(const QStringList& notificationKeys,
                               QString* error = nullptr) const;
    bool finishEditorSession(QString* error = nullptr) const;

private:
    SBackupResult createSnapshot(const QFileInfo& source, const QString& event, bool deduplicate) const;
    QString sourceDirectory(const QFileInfo& source) const;
    QVector<SRecoverableAutosave> recoverableAutosavesForRoot(const std::atomic_bool* cancelled = nullptr) const;
    qint64 enforceRetention(const QString& directoryPath, const QSet<QString>& pins, bool dryRun) const;

private:
    QString m_rootPath;
    int m_maxTimestampedBackups{3};
    int m_minimumAgeDays{3};
    int m_maxAutosaves{20};
};

#endif // BACKUP_MANAGER_H

#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>

struct SRecoverableAutosave
{
    QString sourcePath;
    QString autosavePath;
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
};

class CBackupManager
{
public:
    explicit CBackupManager(const QString& rootOverride = QString());

    SBackupResult snapshotOnOpen(const QFileInfo& source) const;
    SBackupResult snapshotBeforeSave(const QFileInfo& source) const;
    SBackupResult snapshotAutosave(const QFileInfo& source, const QByteArray& candidate) const;
    SBackupResult snapshotAutosaveWithCompanion(
        const QFileInfo& source, const QByteArray& candidate,
        const QString& companionName, const QByteArray& companionCandidate) const;

    QString rootPath() const { return m_rootPath; }
    static QString defaultRootPath();
    static bool openBackupFolder(QString* error = nullptr);
    static QVector<SRecoverableAutosave> recoverableAutosaves();
    bool beginEditorSession(QVector<SRecoverableAutosave>* recoveries,
                            QString* error = nullptr) const;
    bool acknowledgeRecoveries(const QStringList& notificationKeys,
                               QString* error = nullptr) const;
    bool finishEditorSession(QString* error = nullptr) const;

private:
    SBackupResult createSnapshot(const QFileInfo& source, const QString& event, bool deduplicate) const;
    SBackupResult snapshotAutosaveImpl(
        const QFileInfo& source, const QByteArray& candidate,
        const QString& companionName, const QByteArray& companionCandidate) const;
    QString sourceDirectory(const QFileInfo& source) const;
    QVector<SRecoverableAutosave> recoverableAutosavesForRoot() const;
    void enforceRetention(const QString& directoryPath) const;

private:
    QString m_rootPath;
    int m_maxTimestampedBackups{50};
    int m_maxAutosaves{20};
};

#endif // BACKUP_MANAGER_H

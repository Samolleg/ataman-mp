#ifndef BACKUP_MANAGER_H
#define BACKUP_MANAGER_H

#include <QFileInfo>
#include <QString>

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

    QString rootPath() const { return m_rootPath; }
    static QString defaultRootPath();
    static bool openBackupFolder(QString* error = nullptr);

private:
    SBackupResult createSnapshot(const QFileInfo& source, const QString& event, bool deduplicate) const;
    QString sourceDirectory(const QFileInfo& source) const;
    void enforceRetention(const QString& directoryPath) const;

private:
    QString m_rootPath;
    int m_maxTimestampedBackups{50};
};

#endif // BACKUP_MANAGER_H

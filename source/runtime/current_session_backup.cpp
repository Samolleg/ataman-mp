#include "current_session_backup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace {
QString fileSha256(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

bool isSafeRelativePath(const QString& path)
{
    return !path.isEmpty() && path != ".." && !path.startsWith("../") &&
           !QDir::isAbsolutePath(path);
}

bool isSafeCurrentSessionPath(const QString& path)
{
    const QFileInfo info(path);
    return info.isAbsolute() && info.fileName().compare("current", Qt::CaseInsensitive) == 0 &&
           QFileInfo(info.absolutePath()).fileName().compare("saves", Qt::CaseInsensitive) == 0;
}

bool isContainedBy(const QString& child, const QString& parent)
{
    const QString cleanChild = QDir::cleanPath(QFileInfo(child).absoluteFilePath());
    QString cleanParent = QDir::cleanPath(QFileInfo(parent).absoluteFilePath());
    if(!cleanParent.endsWith('/'))
        cleanParent += '/';
    return cleanChild.startsWith(cleanParent, Qt::CaseInsensitive);
}

bool copyDirectorySnapshot(const QString& sourceRoot, const QString& backupRoot,
                           QJsonArray* files, QJsonArray* directories, QString* error)
{
    if(!QDir().mkpath(backupRoot))
    {
        if(error) *error = QString("Could not create current-session backup directory: %1").arg(backupRoot);
        return false;
    }
    const QDir source(sourceRoot);
    QDirIterator iterator(sourceRoot,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while(iterator.hasNext())
    {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = QDir::cleanPath(source.relativeFilePath(info.absoluteFilePath()));
        if(!isSafeRelativePath(relative) || info.isSymLink())
        {
            if(error) *error = QString("Unsafe link or path in JMV saves/current: %1").arg(info.absoluteFilePath());
            return false;
        }
        const QString backupPath = QDir(backupRoot).filePath(relative);
        if(info.isDir())
        {
            if(!QDir().mkpath(backupPath))
            {
                if(error) *error = QString("Could not back up current-session directory: %1").arg(relative);
                return false;
            }
            directories->append(relative);
            continue;
        }
        if(!info.isFile() || !QDir().mkpath(QFileInfo(backupPath).absolutePath()) ||
           !QFile::copy(info.absoluteFilePath(), backupPath))
        {
            if(error) *error = QString("Could not back up current-session file: %1").arg(relative);
            return false;
        }
        const QString hash = fileSha256(info.absoluteFilePath());
        if(hash.isEmpty() || fileSha256(backupPath).compare(hash, Qt::CaseInsensitive) != 0)
        {
            if(error) *error = QString("Current-session backup hash mismatch: %1").arg(relative);
            return false;
        }
        QJsonObject entry;
        entry.insert("path", relative);
        entry.insert("sha256", hash);
        entry.insert("size", double(info.size()));
        files->append(entry);
    }
    return true;
}
}

bool snapshotCurrentSession(const QString& sourcePath, const QString& runRoot,
                            QJsonObject* snapshot, QString* error)
{
    if(!snapshot || !isSafeCurrentSessionPath(sourcePath))
    {
        if(error) *error = QString("Unsafe JMV current-session path: %1").arg(sourcePath);
        return false;
    }
    const QFileInfo sourceInfo(sourcePath);
    if(sourceInfo.exists() && (!sourceInfo.isDir() || sourceInfo.isSymLink()))
    {
        if(error) *error = QString("JMV saves/current is not a normal directory: %1").arg(sourcePath);
        return false;
    }
    const QString backupPath = QDir(runRoot).filePath("runtime-state/saves-current");
    if(!isContainedBy(backupPath, runRoot))
    {
        if(error) *error = "Current-session backup escaped the disposable run directory.";
        return false;
    }
    snapshot->insert("path", sourceInfo.absoluteFilePath());
    snapshot->insert("backupPath", QFileInfo(backupPath).absoluteFilePath());
    snapshot->insert("runRoot", QFileInfo(runRoot).absoluteFilePath());
    snapshot->insert("originalExisted", sourceInfo.exists());
    QJsonArray files;
    QJsonArray directories;
    if(sourceInfo.exists() &&
       !copyDirectorySnapshot(sourceInfo.absoluteFilePath(), backupPath,
                              &files, &directories, error))
        return false;
    snapshot->insert("files", files);
    snapshot->insert("directories", directories);
    return true;
}

bool restoreCurrentSession(const QJsonObject& snapshot, QStringList* messages)
{
    const QString path = snapshot.value("path").toString();
    const QString backupPath = snapshot.value("backupPath").toString();
    const QString runRoot = snapshot.value("runRoot").toString();
    if(path.isEmpty())
        return true;
    if(!isSafeCurrentSessionPath(path) || backupPath.isEmpty() || runRoot.isEmpty() ||
       !isContainedBy(backupPath, runRoot))
    {
        messages->append("Refused unsafe JMV current-session recovery path.");
        return false;
    }
    const bool originalExisted = snapshot.value("originalExisted").toBool();
    const QJsonArray files = snapshot.value("files").toArray();
    for(const QJsonValue& value : files)
    {
        const QJsonObject entry = value.toObject();
        const QString relative = QDir::cleanPath(entry.value("path").toString());
        const QString expectedHash = entry.value("sha256").toString();
        const QString backupFile = QDir(backupPath).filePath(relative);
        if(!isSafeRelativePath(relative) || expectedHash.isEmpty() ||
           !isContainedBy(backupFile, backupPath) ||
           fileSha256(backupFile).compare(expectedHash, Qt::CaseInsensitive) != 0)
        {
            messages->append(QString("Current-session recovery backup failed validation: %1").arg(relative));
            return false;
        }
    }
    const QFileInfo currentInfo(path);
    if(currentInfo.exists() && (!currentInfo.isDir() || currentInfo.isSymLink()))
    {
        messages->append(QString("Refused to replace non-directory JMV current-session path: %1").arg(path));
        return false;
    }
    if(currentInfo.exists() && !QDir(path).removeRecursively())
    {
        messages->append(QString("Could not clear disposable JMV current-session directory: %1").arg(path));
        return false;
    }
    if(!originalExisted)
    {
        messages->append(QString("Removed disposable JMV current-session directory: %1").arg(path));
        return true;
    }
    if(!QDir().mkpath(path))
    {
        messages->append(QString("Could not recreate JMV current-session directory: %1").arg(path));
        return false;
    }
    for(const QJsonValue& value : snapshot.value("directories").toArray())
    {
        const QString relative = QDir::cleanPath(value.toString());
        if(!isSafeRelativePath(relative) || !QDir().mkpath(QDir(path).filePath(relative)))
        {
            messages->append(QString("Could not restore current-session directory: %1").arg(relative));
            return false;
        }
    }
    for(const QJsonValue& value : files)
    {
        const QJsonObject entry = value.toObject();
        const QString relative = QDir::cleanPath(entry.value("path").toString());
        const QString source = QDir(backupPath).filePath(relative);
        const QString destination = QDir(path).filePath(relative);
        if(!QDir().mkpath(QFileInfo(destination).absolutePath()) ||
           !QFile::copy(source, destination) ||
           fileSha256(destination).compare(entry.value("sha256").toString(), Qt::CaseInsensitive) != 0)
        {
            messages->append(QString("Could not restore current-session file: %1").arg(relative));
            return false;
        }
    }
    messages->append(QString("Restored JMV current-session directory: %1").arg(path));
    return true;
}

#include "map_save_batch.h"
#include "backup_manager.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QLockFile>
#include <QSet>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QUuid>
#include <memory>
#include <vector>

namespace {
bool matches(const SMapSaveFile& file, const QByteArray& bytes, bool existed)
{
    if(!existed) return !QFileInfo::exists(file.path);
    QFile disk(file.path);
    return disk.open(QIODevice::ReadOnly) && disk.readAll()==bytes
        && disk.error()==QFileDevice::NoError;
}
}
SMapSaveBatchResult commitMapSaveBatch(const QVector<SMapSaveFile>& input,
                                     const QString& backupRoot, int failCommitForTest)
{
    SMapSaveBatchResult result;
    QVector<SMapSaveFile> files;
    QSet<QString> paths;
    for(auto file:input) {
        file.path=QFileInfo(file.path).absoluteFilePath();
        const QString key=file.path.toCaseFolded();
        if(paths.contains(key)) {result.error="Duplicate save destination: "+file.path;return result;}
        paths.insert(key);
        if(!matches(file,file.before,file.existed)) {
            result.error="File changed outside the editor: "+file.path;return result;
        }
        if(!file.existed || file.before!=file.after) files.append(file);
    }
    std::vector<std::unique_ptr<QLockFile>> locks;
    std::vector<std::unique_ptr<QSaveFile>> staged;
    for(const auto& file:files) {
        std::unique_ptr<QLockFile> lock(new QLockFile(file.path+".mapper-save.lock"));
        if(!lock->tryLock(0)) {result.error="Another save is using: "+file.path;return result;}
        locks.push_back(std::move(lock));
        std::unique_ptr<QSaveFile> stage(new QSaveFile(file.path));
        stage->setDirectWriteFallback(false);
        if(!stage->open(QIODevice::WriteOnly) || stage->write(file.after)!=file.after.size()
           || !stage->flush()) {result.error="Cannot stage: "+file.path;return result;}
        staged.push_back(std::move(stage));
    }
    CBackupManager manager(backupRoot);
    QJsonArray recoveryFiles;
    for(const auto& file:files) {
        if(!matches(file,file.before,file.existed)) {
            result.error="File changed while staging: "+file.path;return result;
        }
        QString backupPath;
        if(file.existed) {
            const auto backup=manager.snapshotBeforeSave(QFileInfo(file.path));
            if(!backup.success) {result.error=backup.error;return result;}
            QFile saved(backup.backupPath);
            if(!saved.open(QIODevice::ReadOnly) || saved.readAll()!=file.before) {
                result.error="Backup verification failed: "+file.path;return result;
            }
            result.backups.append(backup.backupPath);
            backupPath=backup.backupPath;
        }
        recoveryFiles.append(QJsonObject{{"path",file.path},{"existed",file.existed},{"backup",backupPath},
            {"beforeSha256",QString::fromLatin1(QCryptographicHash::hash(file.before,QCryptographicHash::Sha256).toHex())},
            {"afterSha256",QString::fromLatin1(QCryptographicHash::hash(file.after,QCryptographicHash::Sha256).toHex())}});
    }
    if(files.isEmpty()){result.success=true;return result;}
    const QString recoveryDir=QDir(manager.rootPath()).filePath("map-save-recovery");
    const QString recoveryPath=QDir(recoveryDir).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    const QByteArray recovery=QJsonDocument(QJsonObject{{"files",recoveryFiles}}).toJson();
    QSaveFile journal(recoveryPath);
    if(!QDir().mkpath(recoveryDir) || !journal.open(QIODevice::WriteOnly)
       || journal.write(recovery)!=recovery.size() || !journal.commit()){
        result.error="Cannot create save recovery journal: "+recoveryPath;return result;}
    int committed=0;
    for(int i=0;i<files.size();++i) {
        if(!matches(files[i],files[i].before,files[i].existed)
           || i==failCommitForTest || !staged[size_t(i)]->commit()) {
            result.error="Save failed or source changed: "+files[i].path;break;
        }
        ++committed;
        if(!matches(files[i],files[i].after,true)) {
            result.error="Saved file verification failed: "+files[i].path;break;
        }
    }
    if(result.error.isEmpty()) {QFile::remove(recoveryPath);result.success=true;return result;}
    for(int i=committed-1;i>=0;--i) {
        const auto& file=files[i];
        bool restored=false;
        if(matches(file,file.after,true)) {
            if(!file.existed) restored=QFile::remove(file.path);
            else {
                QSaveFile restore(file.path);
                restore.setDirectWriteFallback(false);
                restored=restore.open(QIODevice::WriteOnly)
                    && restore.write(file.before)==file.before.size() && restore.commit();
            }
        }
        restored=restored && matches(file,file.before,file.existed);
        if(!restored) {result.rollbackComplete=false;result.error+="\nRestore required: "+file.path;}
    }
    if(result.rollbackComplete)QFile::remove(recoveryPath);
    else result.error+="\nRecovery journal: "+recoveryPath;
    if(!result.backups.isEmpty())result.error+="\nBackups:\n"+result.backups.join('\n');
    return result;
}

#include "mpr_document.h"
#include "endurance_document.h"
#include "map_save_batch.h"
#include "navigation_save_events.h"
#include "process_edit_lock.h"
#include "backup_manager.h"
#include "file_write_activity.h"
#include "res_archive_index.h"
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
    if(!file.afterFile.isEmpty())return resFileSha256(file.path)==((&bytes==&file.before)?file.beforeSha256:file.afterSha256);
    QFile disk(file.path);
    return disk.open(QIODevice::ReadOnly) && disk.readAll()==bytes
        && disk.error()==QFileDevice::NoError;
}
}
SMapSaveBatchResult commitMapSaveBatch(const QVector<SMapSaveFile>& input,
                                     const QString& backupRoot, int failCommitForTest,
                                     const std::function<void()>& onCommitted)
{
    CFileWriteActivity writing;
    QVector<SBackupResult> backupLeases;
    SMapSaveBatchResult result;
    QVector<QSharedPointer<CProcessEditLock>> editLeases;
    for(const auto& file:input) {
        const auto suffix=QFileInfo(file.path).suffix().toLower();
        if(suffix=="mpr"||suffix=="mob"||suffix=="res") {
            auto lease=CProcessEditLock::acquire(file.path,&result.error,suffix!="res");
            if(!lease)return result;editLeases.append(lease);
        }
    }
    QVector<SMapSaveFile> files;
    QSet<QString> paths;
    for(auto file:input) {
        file.path=QFileInfo(file.path).absoluteFilePath();
        if(QFileInfo(file.path).suffix().compare("mpr",Qt::CaseInsensitive)==0){
            CMprDocument map;CValidationReport report;
            if(!map.loadBytes(file.after,file.path,&report)||report.hasErrors()){
                result.error=report.toText();return result;}
            if(map.mapInfo().textureCount>126){result.error=QString::fromUtf8("%1: атлас %2 страниц, предел126. Сожмите атлас или сократите используемые варианты/материалы. Файлы не изменены.")
                .arg(file.path).arg(map.mapInfo().textureCount);return result;}}
        const QString key=file.path.toCaseFolded();
        if(paths.contains(key)) {result.error="Duplicate save destination: "+file.path;return result;}
        paths.insert(key);
        if(!matches(file,file.before,file.existed)) {
            result.error="File changed outside the editor: "+file.path;return result;
        }
        if(!file.existed || (!file.afterFile.isEmpty()?file.beforeSha256!=file.afterSha256:file.before!=file.after)) files.append(file);
    }
    std::vector<std::unique_ptr<QLockFile>> locks;
    std::vector<std::unique_ptr<QSaveFile>> staged;
    for(const auto& file:files) {
        std::unique_ptr<QLockFile> lock(new QLockFile(file.path+".mapper-save.lock"));
        if(!lock->tryLock(0)) {result.error="Another save is using: "+file.path;return result;}
        locks.push_back(std::move(lock));
        std::unique_ptr<QSaveFile> stage(new QSaveFile(file.path));
        stage->setDirectWriteFallback(false);
        bool stagedOk=stage->open(QIODevice::WriteOnly);
        if(stagedOk && !file.afterFile.isEmpty()){
            QFile input(file.afterFile);stagedOk=input.open(QIODevice::ReadOnly)&&resFileSha256(file.afterFile)==file.afterSha256;
            while(stagedOk&&!input.atEnd()){auto b=input.read(4*1024*1024);stagedOk=!b.isEmpty()&&stage->write(b)==b.size();}
        }else if(stagedOk)stagedOk=stage->write(file.after)==file.after.size();
        if(!stagedOk || !stage->flush()) {result.error="Cannot stage: "+file.path+"\n"+stage->errorString();return result;}
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
            if((!file.afterFile.isEmpty()?resFileSha256(backup.backupPath)!=file.beforeSha256:(!saved.open(QIODevice::ReadOnly)||saved.readAll()!=file.before))) {
                result.error="Backup verification failed: "+file.path;return result;
            }
            backupLeases.append(backup);
            result.backups.append(backup.backupPath);
            backupPath=backup.backupPath;
        }
        recoveryFiles.append(QJsonObject{{"path",file.path},{"existed",file.existed},{"backup",backupPath},
            {"beforeSha256",QString::fromLatin1((file.afterFile.isEmpty()?QCryptographicHash::hash(file.before,QCryptographicHash::Sha256).toHex():file.beforeSha256))},
            {"afterSha256",QString::fromLatin1((file.afterFile.isEmpty()?QCryptographicHash::hash(file.after,QCryptographicHash::Sha256).toHex():file.afterSha256))}});
    }
    if(files.isEmpty()){result.success=true;if(onCommitted)onCommitted();return result;}
    const QString recoveryDir=QDir(manager.rootPath()).filePath("map-save-recovery");
    const QString recoveryPath=QDir(recoveryDir).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    const QByteArray recovery=QJsonDocument(QJsonObject{{"files",recoveryFiles}}).toJson();
    QSaveFile journal(recoveryPath);
    if(!QDir().mkpath(recoveryDir) || !journal.open(QIODevice::WriteOnly)
       || journal.write(recovery)!=recovery.size() || !journal.commit()){
        result.error="Cannot create save recovery journal: "+recoveryPath;return result;}
    int committed=0;
    for(int i=0;i<files.size();++i) {
        if(!matches(files[i],files[i].before,files[i].existed)) {
            result.error="Source changed or could not be read before commit: "+files[i].path;break;
        }
        if(i==failCommitForTest) {result.error="Injected commit failure: "+files[i].path;break;}
        if(!staged[size_t(i)]->commit()) {
            result.error=QString("Atomic replacement failed: %1\nQt file error %2: %3")
                .arg(files[i].path).arg(int(staged[size_t(i)]->error()))
                .arg(staged[size_t(i)]->errorString());break;
        }
        ++committed;
        if(!matches(files[i],files[i].after,true)) {
            result.error="Saved file verification failed: "+files[i].path;break;
        }
    }
    if(result.error.isEmpty()) {QFile::remove(recoveryPath);result.success=true;manager.scheduleCleanup();
        if(onCommitted)onCommitted();
        for(const auto& file:files)if(file.afterFile.isEmpty())
            CNavigationSaveEvents::committed(file.path,file.before,file.after);
        return result;}
    for(int i=committed-1;i>=0;--i) {
        const auto& file=files[i];
        bool restored=false;
        if(matches(file,file.after,true)) {
            if(!file.existed) restored=QFile::remove(file.path);
            else if(!file.afterFile.isEmpty()){
                const auto entry=recoveryFiles[i].toObject();QString error;
                restored=replaceResFromFile(file.path,entry.value("backup").toString(),file.afterSha256,&error);
            }
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

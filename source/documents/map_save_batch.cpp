#include "map_save_batch.h"
#include "backup_manager.h"
#include "../resources/res_read_lease.h"
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
bool matches(const SMapSaveFile& file, const QByteArray& bytes, bool existed, QString* error)
{
    return resIo::matches(file.path, bytes, existed, error);
}
}
SMapSaveBatchResult commitMapSaveBatch(const QVector<SMapSaveFile>& input,
                                     const QString& backupRoot, int failCommitForTest,
                                     const std::function<void()>& onCommitted)
{
    SMapSaveBatchResult result;
    const auto finish = [&]() {
        if(!result.success) result.error = "Phase: " + result.phase + "\n" + result.error;
        return result;
    };
    QVector<SMapSaveFile> files;
    QSet<QString> paths;
    // Deterministic ordering prevents cycles across overlapping batches. Drain
    // before checking any before-bytes; keep admission closed through rollback.
    QStringList destinations;
    for(const auto& file : input) destinations.append(resIo::pathKey(file.path));
    destinations.removeDuplicates(); destinations.sort();
    std::vector<std::unique_ptr<resIo::PublicationLease>> publications;
    result.phase = "reader-drain";
    QElapsedTimer drainTimer; drainTimer.start();
    for(const auto& path : destinations) {
        std::unique_ptr<resIo::PublicationLease> lease(new resIo::PublicationLease(path, &result.error, int(qMax<qint64>(0,5000-drainTimer.elapsed()))));
        if(!lease->acquired()) return finish();
        publications.push_back(std::move(lease));
    }
    result.phase = "before-check";
    for(auto file:input) {
        file.path=QFileInfo(file.path).absoluteFilePath();
        const QString key=resIo::pathKey(file.path);
        if(paths.contains(key)) {result.error="Duplicate save destination: "+file.path;return finish();}
        paths.insert(key);
        if(!matches(file,file.before,file.existed,&result.error)) {
            return finish();
        }
        if(!file.existed || file.before!=file.after) files.append(file);
    }
    std::vector<std::unique_ptr<QLockFile>> locks;
    std::vector<std::unique_ptr<QSaveFile>> staged;
    result.phase = "staging";
    for(const auto& file:files) {
        std::unique_ptr<QLockFile> lock(new QLockFile(file.path+".mapper-save.lock"));
        if(!lock->tryLock(0)) {result.error="Another save is using: "+file.path;return finish();}
        locks.push_back(std::move(lock));
        std::unique_ptr<QSaveFile> stage(new QSaveFile(file.path));
        stage->setDirectWriteFallback(false);
        if(!stage->open(QIODevice::WriteOnly) || stage->write(file.after)!=file.after.size()
           || !stage->flush()) {result.error="Cannot stage: "+file.path+"\n"+stage->errorString();return finish();}
        staged.push_back(std::move(stage));
    }
    result.phase = "backup";
    CBackupManager manager(backupRoot);
    QJsonArray recoveryFiles;
    for(const auto& file:files) {
        if(!matches(file,file.before,file.existed,&result.error)) {
            return finish();
        }
        QString backupPath;
        if(file.existed) {
            const auto backup=manager.snapshotBeforeSave(QFileInfo(file.path));
            if(!backup.success) {result.error=backup.error;return finish();}
            if(!resIo::matches(backup.backupPath,file.before,true,&result.error)) {
                return finish();
            }
            result.backups.append(backup.backupPath);
            backupPath=backup.backupPath;
        }
        recoveryFiles.append(QJsonObject{{"path",file.path},{"existed",file.existed},{"backup",backupPath},
            {"beforeSha256",QString::fromLatin1(QCryptographicHash::hash(file.before,QCryptographicHash::Sha256).toHex())},
            {"afterSha256",QString::fromLatin1(QCryptographicHash::hash(file.after,QCryptographicHash::Sha256).toHex())}});
    }
    if(files.isEmpty()){result.success=true;result.phase="complete";publications.clear();if(onCommitted)onCommitted();return finish();}
    const QString recoveryDir=QDir(manager.rootPath()).filePath("map-save-recovery");
    const QString recoveryPath=QDir(recoveryDir).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
    const QByteArray recovery=QJsonDocument(QJsonObject{{"files",recoveryFiles}}).toJson();
    result.phase = "recovery-journal";
    QSaveFile journal(recoveryPath);
    journal.setDirectWriteFallback(false);
    if(!QDir().mkpath(recoveryDir) || !journal.open(QIODevice::WriteOnly)
       || journal.write(recovery)!=recovery.size() || !journal.commit()){
        result.error="Cannot create save recovery journal: "+recoveryPath+"\n"+journal.errorString();return finish();}
    int committed=0;
    for(int i=0;i<files.size();++i) {
        result.phase = "pre-commit-check";
        result.failedPath = files[i].path;
        if(!matches(files[i],files[i].before,files[i].existed,&result.error)) {
            break;
        }
        if(i==failCommitForTest) {result.error="Injected commit failure: "+files[i].path;break;}
        result.phase = "commit";
        int attempts = 0;
        if(!resIo::commitWithRetry(*staged[size_t(i)], files[i].path, files[i].after,
            [&](QString* error) { return matches(files[i],files[i].before,files[i].existed,error); },
            &result.error, &attempts)) {
            result.commitAttempts += attempts; break;
        }
        result.commitAttempts += attempts;
        ++committed;
        result.committedPaths.append(files[i].path);
        result.phase = "post-commit-verification";
        if(!matches(files[i],files[i].after,true,&result.error)) {
            break;
        }
    }
    if(result.error.isEmpty()) {QFile::remove(recoveryPath);result.success=true;result.phase="complete";result.failedPath.clear();publications.clear();
        if(onCommitted)onCommitted();return finish();}
    for(int i=committed-1;i>=0;--i) {
        const auto& file=files[i];
        bool restored=false;
        QString rollbackError;
        if(matches(file,file.after,true,&rollbackError)) {
            if(!file.existed) restored=QFile::remove(file.path);
            else {
                QSaveFile restore(file.path);
                restore.setDirectWriteFallback(false);
                restored=restore.open(QIODevice::WriteOnly)
                    && restore.write(file.before)==file.before.size()
                    && resIo::commitWithRetry(restore,file.path,file.before,
                        [&](QString* error){return matches(file,file.after,true,error);},&rollbackError);
                if(!restored && rollbackError.isEmpty()) rollbackError=restore.errorString();
            }
        }
        restored=restored && matches(file,file.before,file.existed,&rollbackError);
        if(!restored) {result.rollbackComplete=false;result.error+="\nRestore required (committed but unverified/unrestored): "+file.path+"\n"+rollbackError;}
    }
    if(result.rollbackComplete)QFile::remove(recoveryPath);
    else result.error+="\nRecovery journal: "+recoveryPath;
    if(!result.backups.isEmpty())result.error+="\nBackups:\n"+result.backups.join('\n');
    return finish();
}

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
#include <QCoreApplication>
#include <QDirIterator>
#include <QRegularExpression>
#include "log.h"
#include "res_write_audit.h"

#ifdef EI_SAVE_INTEGRITY_TEST
extern void mapSaveReplacedForTest(int);
extern int mapSaveFailureForTest();
#endif
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
#ifdef EI_SAVE_INTEGRITY_TEST
    if(failCommitForTest<0)failCommitForTest=mapSaveFailureForTest();
#endif
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
    QLockFile journalLease(recoveryPath+".lease");journalLease.setStaleLockTime(0);
    if(!QDir().mkpath(recoveryDir)||!journalLease.tryLock(0)){result.error="Cannot lease recovery journal: "+recoveryPath;return result;}
    const QByteArray recovery=QJsonDocument(QJsonObject{{"schema",2},{"pid",double(QCoreApplication::applicationPid())},
        {"exe",QCoreApplication::applicationFilePath()},{"version",QCoreApplication::applicationVersion()},{"files",recoveryFiles}}).toJson();
    QSaveFile journal(recoveryPath);
    if(!QDir().mkpath(recoveryDir) || !journal.open(QIODevice::WriteOnly)
       || journal.write(recovery)!=recovery.size() || !journal.commit()){
        result.error="Cannot create save recovery journal: "+recoveryPath;return result;}
    QVector<SResWriteAudit> audits;
    for(int i=0;i<files.size();++i)audits.append(beginResWriteAudit(files[i].path,
        recoveryFiles[i].toObject()["beforeSha256"].toString().toLatin1()));
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
        finishResWriteAudit(audits[i],recoveryFiles[i].toObject()["afterSha256"].toString().toLatin1(),"map-save-batch");
        // Isolated production-code fault harness: no environment-controlled
        // hook exists in the shipped executable.
#ifdef EI_SAVE_INTEGRITY_TEST
        mapSaveReplacedForTest(i);
#endif
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
        const auto rollbackAudit=beginResWriteAudit(file.path,recoveryFiles[i].toObject()["afterSha256"].toString().toLatin1());
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
        if(restored)finishResWriteAudit(rollbackAudit,recoveryFiles[i].toObject()["beforeSha256"].toString().toLatin1(),"map-save-live-rollback");
        if(!restored) {result.rollbackComplete=false;result.error+="\nRestore required: "+file.path;}
    }
    if(result.rollbackComplete)QFile::remove(recoveryPath);
    else result.error+="\nRecovery journal: "+recoveryPath;
    if(!result.backups.isEmpty())result.error+="\nBackups:\n"+result.backups.join('\n');
    return result;
}

namespace {
struct RecoveryFile {
    QString path,backup;QByteArray before,after,current;bool existed{false},present{false};
};
bool recoveryState(QVector<RecoveryFile>* files,QString* error)
{
    for(auto& file:*files){
        file.present=QFileInfo::exists(file.path);
        file.current=file.present?resFileSha256(file.path):QByteArray();
        const bool before=file.existed?(file.present&&!file.current.isEmpty()&&file.current==file.before):!file.present;
        const bool after=file.present&&!file.current.isEmpty()&&file.current==file.after;
        if(!before&&!after){*error="External change/unreadable file; entire family untouched: "+file.path;return false;}
    }
    return true;
}
bool recoveryBefore(const RecoveryFile& f){return f.existed?f.current==f.before:!f.present;}
bool recoveryAfter(const RecoveryFile& f){return f.present&&f.current==f.after;}
}
QStringList recoverMapSaveBatches(const QString& backupStore,bool headless,const std::function<bool(const QString&)>& consent)
{
    QStringList warnings;
    if(!QDir(backupStore).exists())return warnings;
    QStringList stores{QDir(backupStore).filePath("map-save-recovery")};
    const QDir processes(QDir(backupStore).filePath("processes"));
    for(const auto& slot:processes.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks))
        if(QRegularExpression("^[0-9]+$").match(slot.fileName()).hasMatch())stores.append(QDir(slot.absoluteFilePath()).filePath("map-save-recovery"));
    QStringList journals;
    for(const auto& store:stores)for(const auto& file:QDir(store).entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks))journals.append(file.absoluteFilePath());
    for(const auto& journalPath:journals){
        const QFileInfo journalInfo(journalPath);
        const auto warn=[&](const QString& text){const auto message="Map-save recovery: "+text+"; journal: "+journalPath;warnings.append(message);ei::log(eLogWarning,message);};
        if(headless){warn("Pending save journal; headless recovery is read-only. Open the windowed mapper to review");continue;}
        // Respect the existing process-slot lifetime lease, including old
        // releases which predate per-journal leases. PID reuse cannot unlock it.
        auto slot=journalInfo.dir();slot.cdUp();
        std::unique_ptr<QLockFile> slotLease;
        if(slot.dirName().contains(QRegularExpression("^[0-9]+$"))&&QFileInfo(slot.absolutePath()).dir().dirName()=="processes"){
            auto store=slot;store.cdUp();store.cdUp();
            slotLease.reset(new QLockFile(store.filePath("process-"+slot.dirName()+".lock")));
            slotLease->setStaleLockTime(0);
            if(!slotLease->tryLock(0)){warn("Live or unavailable process slot; left unchanged");continue;}
        }
        // Headless is strictly read-only, including journal/lock files. It
        // reports pending journals without hashes or stale-lock reclamation.
        QLockFile lease(journalPath+".lease");lease.setStaleLockTime(0);
        if(!lease.tryLock(0)){warn("Live or unavailable journal lease; left unchanged");continue;}
        QFile journal(journalPath);QJsonParseError parse;
        if(!journal.open(QIODevice::ReadOnly)){warn("Cannot read journal");continue;}
        const auto document=QJsonDocument::fromJson(journal.readAll(),&parse);journal.close();const auto object=document.object();
        if(parse.error!=QJsonParseError::NoError||!object["files"].isArray()||object["files"].toArray().isEmpty()){
            warn("Malformed or unknown journal; retained");continue;}
        QVector<RecoveryFile> files;QSet<QString> paths;bool valid=true;
        const QRegularExpression sha("^[0-9a-f]{64}$");
        for(const auto& value:object["files"].toArray()){
            const auto row=value.toObject();RecoveryFile f;f.path=row["path"].toString();f.backup=row["backup"].toString();
            f.existed=row["existed"].toBool();f.before=row["beforeSha256"].toString().toLatin1();f.after=row["afterSha256"].toString().toLatin1();
            if(!value.isObject()||!row["existed"].isBool()||!QDir::isAbsolutePath(f.path)||QFileInfo(f.path).isSymLink()
                ||!sha.match(QString::fromLatin1(f.before)).hasMatch()||!sha.match(QString::fromLatin1(f.after)).hasMatch()
                ||(f.existed&&(!QDir::isAbsolutePath(f.backup)||QFileInfo(f.backup).isSymLink()))||paths.contains(CProcessEditLock::identity(f.path))){valid=false;break;}
            paths.insert(CProcessEditLock::identity(f.path));files.append(f);
        }
        if(!valid){warn("Invalid paths/hashes or duplicate destinations; retained");continue;}
        // Legacy root journals have no demonstrable lifetime owner. Never use
        // them to replace files even with an old backup that still exists.
        const bool ownershipKnown=bool(slotLease)||object["schema"].toInt()==2;
        if(!ownershipKnown){warn("Legacy journal without a lifetime lease; manual review required, no replacement");continue;}
        QString error;QVector<QSharedPointer<CProcessEditLock>> edits;
        std::vector<std::unique_ptr<QLockFile>> saveLocks;
        for(const auto& f:files){
            const auto suffix=QFileInfo(f.path).suffix().toLower();
            if(suffix=="mpr"||suffix=="mob"||suffix=="res"){
                auto edit=CProcessEditLock::acquire(f.path,&error,suffix!="res");if(!edit){valid=false;break;}edits.append(edit);}
            std::unique_ptr<QLockFile> lock(new QLockFile(f.path+".mapper-save.lock"));lock->setStaleLockTime(0);
            if(!lock->tryLock(0)){valid=false;error="Another save owns "+f.path;break;}saveLocks.push_back(std::move(lock));
        }
        if(!valid||!recoveryState(&files,&error)){warn(error);continue;}
        bool allBefore=true,allAfter=true;for(const auto& f:files){allBefore&=recoveryBefore(f);allAfter&=recoveryAfter(f);}
        if(allBefore||allAfter){
            if(QFile::remove(journalPath))ei::log(eLogInfo,"Map-save recovery: coherent family, journal closed: "+journalPath);
            else warn("Coherent family; could not close journal");continue;}
        for(const auto& f:files)if(f.existed&&resFileSha256(f.backup,&error)!=f.before){valid=false;error="Before backup is missing or does not match SHA: "+f.backup;break;}
        if(!valid){warn(error);continue;}
        QStringList labels;for(const auto& f:files)labels.append(f.path+(recoveryBefore(f)?QString::fromUtf8(" (уже прежнее состояние)"):QString::fromUtf8(" (будет возвращён к прежнему состоянию)")));
        const auto question=QString::fromUtf8("Найдено прерванное сохранение карты. Часть файлов уже заменена, часть осталась прежней.\n\n%1\n\nВернуть всё семейство к состоянию до этого сохранения из проверенных резервных копий? Перед откатом будет сохранена копия текущих файлов. При отказе файлы и журнал останутся для следующего запуска.").arg(labels.join('\n'));
        if(!consent||!consent(question)){warn("Owner declined rollback; files and journal retained");continue;}
        // Consent can take arbitrarily long; revalidate the whole family and
        // every backup before any destination replacement.
        if(!recoveryState(&files,&error)){warn(error);continue;}
        for(const auto& f:files)if(f.existed&&resFileSha256(f.backup,&error)!=f.before){valid=false;error="Backup changed: "+f.backup;break;}
        if(!valid){warn(error);continue;}
        const auto currentDir=journalPath+".current-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        if(!QDir().mkpath(currentDir)){warn("Cannot create current-state backup");continue;}
        QJsonArray currentSnapshots;
        for(int i=0;i<files.size();++i){const auto& f=files[i];const auto copy=QDir(currentDir).filePath(QString::number(i)+".backup");
            if(f.present&&(!QFile::copy(f.path,copy)||resFileSha256(copy)!=f.current)){valid=false;error="Cannot verify current-state backup: "+f.path;break;}
            currentSnapshots.append(QJsonObject{{"path",f.path},{"present",f.present},{"sha256",QString::fromLatin1(f.current)},{"backup",f.present?copy:QString()}});
        }
        QSaveFile receipt(QDir(currentDir).filePath("current.json"));const auto bytes=QJsonDocument(currentSnapshots).toJson();
        if(!valid||!receipt.open(QIODevice::WriteOnly)||receipt.write(bytes)!=bytes.size()||!receipt.commit()||!recoveryState(&files,&error)){
            warn("Current-state backup/revalidation failed: "+error);continue;}
        for(auto& f:files){if(recoveryBefore(f))continue;
            // Check all siblings before every replacement to detect an external
            // writer as early as possible, preserving unknown bytes everywhere.
            if(!recoveryState(&files,&error)){valid=false;break;}
            if(f.existed){if(!replaceResFromFile(f.path,f.backup,f.current,&error)){valid=false;break;}}
            else if(!QFile::remove(f.path)){error="Cannot remove newly created file: "+f.path;valid=false;break;}
        }
        if(valid&&recoveryState(&files,&error)){
            for(const auto& f:files)valid&=recoveryBefore(f);
        }
        if(valid&&QFile::remove(journalPath))ei::log(eLogInfo,"Map-save recovery: before family restored; current-state backup: "+currentDir);
        else warn("Recovery incomplete; journal and backups retained: "+error);
    }
    return warnings;
}

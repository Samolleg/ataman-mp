// EI-SAVE-PIPELINE-210: link against the exact Release objects, excluding main.o.
#include <QtCore>
#include <QtEndian>
#include "res_save_coordinator.h"
#include "terrain_atlas_recovery_store.h"
#include "terrain_atlas_compact.h"
#include "res_file.h"
#include "log.h"
#include "map_save_batch.h"
#include "terrain_edit_model.h"
#include "endurance_document.h"
#include <windows.h>
#include <psapi.h>

namespace {
QString root;QTextStream out(stdout);int checks=0;
void require(bool ok,const QString& what){if(!ok)throw what;++checks;out<<"PASS "<<what<<"\n";out.flush();}
void write(const QString& p,const QByteArray& b){QFile f(p);require(f.open(QIODevice::WriteOnly)&&f.write(b)==b.size(),"write fixture "+QFileInfo(p).fileName());}
QByteArray sha(const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
void transaction(){
    const QString path=root+"/transaction.res";QByteArray seed(16,0);qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(seed.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(seed.data()+8));
    CResArchiveDocument d;QStringList errors;require(d.loadBytes(seed,"empty",&errors),"empty fixture");
    SResArchiveNewMember a;a.name="retained.bin";a.payload=QByteArray(8*1024*1024,'a');a.lastWriteTime=1234567;
    SResArchiveNewMember b;b.name="changed.bin";b.payload="old";b.lastWriteTime=7654321;
    SResArchiveNewMember c;c.name="removed.bin";c.payload="remove";c.lastWriteTime=111;
    QByteArray bytes;require(d.buildWithAppendedMembers({a,b,c},&bytes,&errors),"synthetic game RES");write(path,bytes);
    CResArchiveIndex index;QString error;require(index.open(path,&error),"index fixture");
    SResPublishedMembersSaveRequest request;request.source=request.destination=QFileInfo(path);request.backupRootOverride=root+"/transaction-backup";request.allowReplaceExisting=true;
    b.payload="new Russian payload: "+QString::fromUtf8("Русский текст").toUtf8();c.name="added.bin";c.payload="added";request.published={b,c};
    auto result=commitStreamedResMembers(index,request,{"removed.bin"},sha(bytes),[]{return false;});
    require(!result.success&&resFileSha256(path)==sha(bytes),"interrupted before atomic commit retains original bytes");
    result=commitStreamedResMembers(index,request,{"removed.bin"},sha(bytes));
    require(result.success,"streamed add/replace/delete: "+result.error);
    CResArchiveIndex reopened;require(reopened.open(path,&error),"fresh file-backed reopen");
    require(reopened.readMember(a.name)==a.payload&&reopened.member(reopened.memberIndex(a.name)).lastWriteTime==a.lastWriteTime,"unchanged payload/timestamp exact");
    require(reopened.readMember(b.name)==b.payload&&reopened.member(reopened.memberIndex(b.name)).lastWriteTime==b.lastWriteTime,"replacement payload exact and source timestamp retained");
    require(reopened.memberIndex("removed.bin")==-1&&reopened.readMember(c.name)==c.payload,"add/delete exact");
    CResFile game(path);require(game.bufferOfFiles().value(a.name)==a.payload&&game.bufferOfFiles().value(b.name)==b.payload,"legacy game RES reader sees exact payloads");
    const auto mtime=QFileInfo(path).lastModified();result=commitStreamedResMembers(reopened,request);
    require(result.success&&result.noOp&&QFileInfo(path).lastModified()==mtime,"identical publication does not rewrite RES");
    CResArchiveTransaction cohortTx(reopened);
    require(cohortTx.add("cohort.bin","cohort",777,&error)&&cohortTx.writeCandidate(root+"/cohort-candidate.res",&error),"file-backed cohort candidate");
    SMapSaveFile res;res.path=path;res.afterFile=root+"/cohort-candidate.res";
    res.beforeSha256=resFileSha256(path);res.afterSha256=resFileSha256(res.afterFile);
    SMapSaveFile text;text.path=root+"/cohort.txt";text.before="old";text.after="new";write(text.path,text.before);
    auto batch=commitMapSaveBatch({res,text},root+"/cohort-backup",1);
    require(!batch.success&&batch.rollbackComplete&&resFileSha256(path)==res.beforeSha256,"cohort failure restores streamed RES from verified backup");
    batch=commitMapSaveBatch({res,text},root+"/cohort-backup");
    require(batch.success&&resFileSha256(path)==res.afterSha256,"cohort publishes file-backed RES and companion together");
    const QString rollback=root+"/transient-rollback.res";QFile::remove(rollback);
    require(QFile::copy(path,rollback),"transient rollback fixture");
    CResArchiveIndex noBackup;require(noBackup.open(path,&error),"no-backup publication source");
    request.createBackup=false;b.payload="without durable backup";request.published={b};
    const auto streamed=commitStreamedResMembers(noBackup,request);
    require(streamed.success&&streamed.backupPath.isEmpty(),"explicit no-backup publication remains streamed");
    require(replaceResFromFile(path,rollback,streamed.candidateSha256.toLatin1(),&error)&&resFileSha256(path)==res.afterSha256,"file-backed transient publication rollback");
}
void recovery(){
    const QString storeRoot=root+"/generations-"+QUuid::createUuid().toString(QUuid::WithoutBraces);CTerrainAtlasRecoveryStore store(storeRoot);
    STerrainAtlasRecoverySnapshot s;s.mprPath=root+"/owned-map.mpr";s.resPath=root+"/owned.res";s.atlasPixelSha256=sha("pixels");s.profileBytes="profile";
    QStringList manifests;
    for(int i=0;i<9;++i){s.resourceIdentity=QString("owned_%1").arg(i,12,16,QLatin1Char('0'));s.mprSha256=sha(QByteArray::number(i));STerrainAtlasRecoveryPage p;p.name="page.mmp";p.payload="page"+QByteArray::number(i);s.pages={p};
        auto r=store.saveSnapshot(s);require(r.success,"save recovery generation "+QString::number(i));manifests<<r.manifestPath;
        QFile f(r.manifestPath);require(f.open(QIODevice::ReadOnly),"read recovery fixture");auto j=QJsonDocument::fromJson(f.readAll()).object();f.close();
        j["createdUtc"]=QString("2026-10-20T00:00:%1.000Z").arg(i,2,10,QLatin1Char('0'));write(r.manifestPath,QJsonDocument(j).toJson());
    }
    // A different map with the same basename has its own budget.
    s.mprPath=root+"/other/owned-map.mpr";s.resourceIdentity="other_aaaaaaaaaaaa";auto other=store.saveSnapshot(s);require(other.success,"separate map path budget");
    QStringList legacy;
    s.mprPath=root+"/missing-legacy-map.mpr";
    for(int i=0;i<6;++i){s.resourceIdentity=QString("legacy_%1").arg(i,12,16,QLatin1Char('0'));s.mprSha256=sha("legacy"+QByteArray::number(i));auto r=store.saveSnapshot(s);require(r.success,"legacy generation without known saved MPR");legacy<<r.manifestPath;}
    store.setOpenDocument(root+"/owned-map.mpr",sha("0"));
    // Persist last-successful pin without scheduling a racing test cleanup.
    const QString mapKey=QDir::cleanPath(QFileInfo(root+"/owned-map.mpr").absoluteFilePath()).toCaseFolded();
    write(storeRoot+"/last-saved.json",QJsonDocument(QJsonObject{{mapKey,QString::fromLatin1(sha("1"))}}).toJson());
    auto dry=store.collectGarbage(5,true);require(dry.success&&dry.manifests==2&&dry.objects==2,"N=5 plus current and last-successful pins, exact orphan count");
    auto cleaned=store.collectGarbage(5);require(cleaned.success&&cleaned.bytes==dry.bytes,"dry-run equals actual recovery release");
    require(QFileInfo::exists(manifests[0])&&QFileInfo::exists(manifests[1])&&!QFileInfo::exists(manifests[2])&&!QFileInfo::exists(manifests[3])&&QFileInfo::exists(other.manifestPath),"protected generations and other map retained");
    for(const auto& p:legacy)require(QFileInfo::exists(p),"unknown legacy last-successful generation retained conservatively");
    auto again=store.collectGarbage(5);require(again.success&&!again.bytes&&!again.objects&&!again.manifests,"recovery cleanup idempotent");
    // Unknown/corrupt manifest fails closed before any deletion.
    write(storeRoot+"/manifests/bad.json","{");auto bad=store.collectGarbage(1);require(!bad.success&&!bad.bytes,"unreadable manifest prevents deletion");
    store.closeDocument(root+"/owned-map.mpr");
}
void cleanup(){
    auto path=root+"/current/res/HDPack_Terrain.res";const auto before=QFileInfo(path).lastModified();QElapsedTimer time;time.start();
    auto p=prepareTerrainPackCleanup(path,{root+"/current/Maps",root+"/current/BaseMaps"});const auto prepare=time.elapsed();
    require(p.error.isEmpty()&&p.members.isEmpty()&&p.archiveSha256.isEmpty()&&p.before==p.after,"cleanup inventory-only empty plan");QString error;time.restart();
    require(commitTerrainPackCleanup(path,{root+"/current/Maps",root+"/current/BaseMaps"},p,&error)&&QFileInfo(path).lastModified()==before,"cleanup no-op does not touch pack");
    out<<"METRIC cleanup prepareMs="<<prepare<<" commitMs="<<time.elapsed()<<" maps="<<p.inventory.size()<<"\n";
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);out.setCodec("UTF-8");root=qEnvironmentVariable("EI_SAVE_TEST_ROOT");
    if(!QFileInfo(root).fileName().startsWith("build-tests-save-210"))return 2;QDir::setCurrent(root);qputenv("EI_MAPPER_BACKUP_ROOT",(root+"/checks-backup").toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/check-settings");
    try{const QString mode=argc>1?QString::fromLocal8Bit(argv[1]):"checks";
        if(mode=="interrupt"){
            CResArchiveIndex a;QString e;require(a.open(root+"/interrupt.res",&e),"interruption source indexed");CResArchiveTransaction tx(a);require(tx.add("new.bin",QByteArray(16*1024*1024,'z'),123,&e),"interruption edit");
            require(tx.writeCandidate(root+"/interrupt.res",&e),e);
        }else if(mode=="retirement"){
            CMprDocument original;CValidationReport report;CResArchiveIndex archive;QString error;STerrainAtlasProfile profile;
            require(original.load(QFileInfo(root+"/zone3obr-pristine.mpr"),&report)&&archive.open(root+"/pristine.res",&error),"retirement source pair");
            require(STerrainAtlasProfile::fromBytes(archive.readMember(CTerrainAtlasProfileStore::embeddedMemberName(original.mapInfo().innerNamespace)),&profile,&report),"retirement source profile");
            QElapsedTimer time;time.start();const auto result=retireTerrainAtlasAfterSave(root+"/current/res/HDPack_Terrain.res",profile,root+"/current/Maps/zone3obr.mpr",{root+"/current/Maps",root+"/current/BaseMaps"},root+"/retirement-backup");const auto elapsed=time.elapsed();
            require(result.startsWith("Removed"),"retirement: "+result);PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem));
            out<<"METRIC retirement ms="<<elapsed<<" peakCommitMiB="<<mem.PeakPagefileUsage/1048576.<<"\n";
        }else if(mode=="transaction")transaction();
        else if(mode=="small-pair"){
            CMprDocument before,after;CValidationReport report;CTerrainEditModel a,b;
            require(before.load(QFileInfo(root+"/basegipat-pristine.mpr"),&report)&&after.load(QFileInfo(root+"/current/Maps/basegipat.mpr"),&report)
                &&a.load(before,&report)&&b.load(after,&report),"reopen original and saved small MPR");
            require(b.height(20,20)==a.height(20,20)+32,"small map height edit persisted");
            require(before.mapInfo().innerNamespace==after.mapInfo().innerNamespace&&resFileSha256(root+"/pristine.res")==resFileSha256(root+"/current/res/HDPack_Terrain.res"),"height-only Save preserved namespace and entire RES hash");
            STerrainAtlasRecoverySnapshot recovery;QString error;
            require(CTerrainAtlasRecoveryStore(root+"/backups/terrain-atlases").findForMpr(after.mapInfo().innerNamespace,sha(after.originalBytes()),&recovery,&error),"height-only Save has exact durable recovery generation: "+error);
        }else if(mode=="cleanup-only")cleanup();
        else if(mode=="recovery-synthetic")recovery();
        else if(mode=="recovery-copy"||mode=="recovery-copy-final"){
            CTerrainAtlasRecoveryStore store(root+(mode=="recovery-copy-final"?"/recovery-copy-final":"/recovery-copy"));auto dry=store.collectGarbage(5,true);require(dry.success,"real recovery dry-run: "+dry.error);
            auto actual=store.collectGarbage(5);require(actual.success&&actual.bytes==dry.bytes&&actual.manifests==dry.manifests&&actual.objects==dry.objects,"real recovery cleanup matches plan");
            out<<"METRIC recovery bytes="<<actual.bytes<<" manifests="<<actual.manifests<<" objects="<<actual.objects<<"\n";
        }else{transaction();recovery();cleanup();ei::log(eLogInfo,QString::fromUtf8("Проверка журнала: сохранение карты"));
            QFile log(CLogger::getInstance()->logFilePath());require(log.open(QIODevice::ReadOnly),"read UTF-8 log");auto bytes=log.readAll();require(bytes.startsWith(QByteArray::fromHex("efbbbf"))&&QString::fromUtf8(bytes).contains(QString::fromUtf8("Проверка журнала: сохранение карты")),"UTF-8 BOM and Russian text readable");}
        out<<"PASS total="<<checks<<"\n";return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\n";return 1;}
}

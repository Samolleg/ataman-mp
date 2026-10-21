#include "terrain_atlas_recovery_store.h"
#include "log.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <algorithm>
#include <map>
#include <memory>

namespace {
QMutex mutex;
struct Lease { std::unique_ptr<QLockFile> lock; QJsonObject maps; };
std::map<QString,Lease> leases;
QString key(const QString& map){return QDir::cleanPath(QFileInfo(map).absoluteFilePath()).toCaseFolded();}
QJsonObject json(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(f.readAll()).object():QJsonObject();}
bool write(const QString& p,const QJsonObject& j){QSaveFile f(p);const auto b=QJsonDocument(j).toJson();return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit();}
QString leasePath(const QString& root){return QDir(root).filePath(QString("open-%1.json").arg(QCoreApplication::applicationPid()));}
void updateLease(const QString& root,const QString& map,const QByteArray& hash){
    QMutexLocker guard(&mutex);QDir().mkpath(root);auto& lease=leases[root];const auto path=leasePath(root);
    if(!lease.lock){lease.lock.reset(new QLockFile(path+".lock"));if(!lease.lock->tryLock(0))return;}
    if(hash.isEmpty())lease.maps.remove(key(map));else lease.maps[key(map)]=QString::fromLatin1(hash);
    write(path,lease.maps);
}
}

void CTerrainAtlasRecoveryStore::setOpenDocument(const QString& map,const QByteArray& hash) const {
    QDir().mkpath(m_rootPath);QLockFile lock(QDir(m_rootPath).filePath("store.lock"));
    if(lock.tryLock(5000))updateLease(m_rootPath,map,hash);
}
void CTerrainAtlasRecoveryStore::closeDocument(const QString& map) const {setOpenDocument(map,{});}
void CTerrainAtlasRecoveryStore::markSaved(const QString& map,const QByteArray& hash) const {
    setOpenDocument(map,hash);
    QLockFile lock(QDir(m_rootPath).filePath("store.lock"));if(!lock.tryLock(5000))return;
    auto pins=json(QDir(m_rootPath).filePath("last-saved.json"));pins[key(map)]=QString::fromLatin1(hash);
    write(QDir(m_rootPath).filePath("last-saved.json"),pins);
    lock.unlock();scheduleCleanup();
}
void CTerrainAtlasRecoveryStore::scheduleCleanup() const {
    const int count=qMax(1,QSettings().value("recovery/terrainAtlasGenerations",5).toInt());
    const QString root=m_rootPath;
    QtConcurrent::run([root,count](){
        QElapsedTimer timer;timer.start();
        const auto r=CTerrainAtlasRecoveryStore(root).collectGarbage(count);
        if(r.success)ei::log(eLogInfo,QString("Terrain recovery cleanup: generations=%1 manifests=%2 objects=%3 freedBytes=%4 elapsedMs:%5")
            .arg(count).arg(r.manifests).arg(r.objects).arg(r.bytes).arg(timer.elapsed()));
        else ei::log(eLogWarning,"Terrain recovery cleanup deferred: "+r.error);
    });
}

STerrainAtlasRecoveryCleanup CTerrainAtlasRecoveryStore::collectGarbage(int generations,bool dryRun) const {
    STerrainAtlasRecoveryCleanup r;
    if(!QDir(m_rootPath).exists()){r.success=true;return r;}
    QLockFile lock(QDir(m_rootPath).filePath("store.lock"));if(!lock.tryLock(0)){r.error="Recovery store busy; cleanup deferred.";return r;}
    const auto fail=[&](const QString& e){r.error=e;return r;};
    for(const auto& path:{m_rootPath,QDir(m_rootPath).filePath("manifests"),objectDirectory()})
        if(QFileInfo(path).isSymLink())return fail("Linked recovery root; retained.");
    QMap<QString,QSet<QString>> pins;
    const auto pin=[&](const QJsonObject& j){for(auto it=j.begin();it!=j.end();++it)pins[it.key()].insert(it.value().toString());};
    {
        QMutexLocker guard(&mutex);
        auto own=leases.find(m_rootPath);if(own!=leases.end())pin(own->second.maps);
    }
    // A failed lease JSON update must not expose another process's open map.
    // Defer GC while another editor holds a lease, even if its JSON is missing.
    for(const auto& f:QDir(m_rootPath).entryInfoList({"open-*.json.lock"},QDir::Files)){
        if(f.absoluteFilePath()==leasePath(m_rootPath)+".lock")continue;
        QLockFile other(f.absoluteFilePath());
        if(!other.tryLock(0))return fail("Another editor has open recovery generations; cleanup deferred.");
    }
    const QString saved=QDir(m_rootPath).filePath("last-saved.json");
    if(QFileInfo::exists(saved)){auto j=json(saved);if(j.isEmpty())return fail("Unreadable last-saved pins; retained.");pin(j);}
    for(const auto& f:QDir(m_rootPath).entryInfoList({"open-*.json"},QDir::Files)){
        if(f.isSymLink())return fail("Linked recovery lease; retained.");
        QLockFile lease(f.absoluteFilePath()+".lock");
        // Own lease is held by this process; other live processes also hold theirs.
        if(lease.tryLock(0)){if(!dryRun)QFile::remove(f.absoluteFilePath());}
        else {
            QFile input(f.absoluteFilePath());
            if(!input.open(QIODevice::ReadOnly))return fail("Unreadable open-document pins; retained.");
            const auto document=QJsonDocument::fromJson(input.readAll());
            if(!document.isObject())return fail("Unreadable open-document pins; retained.");
            pin(document.object());
        }
    }
    struct Generation {QString path,map,stamp;QJsonObject data;};
    QVector<Generation> all;QMap<QString,QVector<int>> maps;
    QDirIterator it(QDir(m_rootPath).filePath("manifests"),QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(it.hasNext()){
        it.next();const auto f=it.fileInfo();if(f.isSymLink())return fail("Linked recovery manifest; retained.");if(f.isDir())continue;
        if(f.suffix()!="json")continue;auto j=json(f.absoluteFilePath());
        if(j.value("schemaVersion").toInt()!=1||j.value("mprPath").toString().isEmpty()
           ||!j.value("pages").isArray()||j.value("createdUtc").toString().isEmpty())return fail("Unrecognized recovery manifest; retained.");
        Generation g{f.absoluteFilePath(),key(j.value("mprPath").toString()),j.value("createdUtc").toString(),j};
        // Legacy stores have no last-saved journal yet. The actual saved MPR
        // protects its exact generation even after many interrupted publications.
        if(!maps.contains(g.map)){
            QFile map(j.value("mprPath").toString());QCryptographicHash h(QCryptographicHash::Sha256);
            if(map.open(QIODevice::ReadOnly)&&h.addData(&map))pins[g.map].insert(QString::fromLatin1(h.result().toHex()));
        }
        maps[g.map]<<all.size();all<<g;
    }
    QSet<int> keep;
    for(auto group:maps){std::sort(group.begin(),group.end(),[&](int a,int b){return all[a].stamp==all[b].stamp?all[a].path>all[b].path:all[a].stamp>all[b].stamp;});
        bool knownSaved=false;
        for(int i:group)knownSaved|=pins.value(all[i].map).contains(all[i].data.value("mprSha256").toString());
        // Legacy manifests predate the last-successful journal. If the saved
        // MPR cannot identify any generation, never guess which one succeeded.
        if(!knownSaved){for(int i:group)keep.insert(i);continue;}
        for(int n=0;n<group.size();++n){int i=group[n];if(n<qMax(1,generations)||pins.value(all[i].map).contains(all[i].data.value("mprSha256").toString()))keep.insert(i);}}
    QSet<QString> referenced;const QRegularExpression hash("^[0-9a-f]{64}$");
    // Validate references from ALL manifests before deleting anything. Unknown data fails closed.
    for(int i=0;i<all.size();++i){QStringList refs{all[i].data.value("profileObjectSha256").toString()};
        for(const auto& array:{all[i].data.value("pages").toArray(),all[i].data.value("metadataMembers").toArray()})
            for(const auto& v:array)refs<<v.toObject().value("sha256").toString();
        for(const auto& h:refs){if(!hash.match(h).hasMatch())return fail("Invalid recovery object reference; retained.");if(keep.contains(i))referenced.insert(h);}}
    QStringList removeManifests,removeObjects;
    for(int i=0;i<all.size();++i)if(!keep.contains(i))removeManifests<<all[i].path;
    for(const auto& f:QDir(objectDirectory()).entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot)){
        if(f.isSymLink()||f.isDir())return fail("Linked or nested recovery object; retained.");
        if(f.suffix()=="blob"&&hash.match(f.completeBaseName()).hasMatch()&&!referenced.contains(f.completeBaseName()))removeObjects<<f.absoluteFilePath();}
    // Invalidate generations first; never remove their objects if any manifest deletion fails.
    for(const auto& p:removeManifests){const auto bytes=QFileInfo(p).size();if(!dryRun&&!QFile::remove(p))return fail("Cannot remove old recovery manifest.");r.bytes+=bytes;++r.manifests;}
    for(const auto& p:removeObjects){const auto bytes=QFileInfo(p).size();if(!dryRun&&!QFile::remove(p))return fail("Cannot remove unreferenced recovery object.");r.bytes+=bytes;++r.objects;}
    r.success=true;return r;
}

#include "world_terrain_session.h"
#include "endurance_document.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

QString CWorldTerrainSession::key(const QString& path)
{
    const QFileInfo file(path);
    const QString canonical=file.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty()?file.absoluteFilePath():canonical).toLower();
}
bool CWorldTerrainSession::load(const QString& path, QString* error)
{
    const QString id=key(path);
    if(documents.contains(id)) return true;
    if(QFileInfo::exists(path+".world-sculpt.json"))
    { *error=QStringLiteral("Interrupted terrain save requires recovery: ")+path+".world-sculpt.json"; return false; }
    QSharedPointer<SWorldTerrainDocument> doc(new SWorldTerrainDocument);
    doc->editLease=CProcessEditLock::acquire(path,error);if(!doc->editLease)return false;
    doc->path=QFileInfo(path).absoluteFilePath();
    CValidationReport report;
    if(!doc->baseline.load(QFileInfo(path),&report) || report.hasErrors()
       || !doc->terrain.load(doc->baseline,&report) || report.hasErrors())
    { *error=report.toText(); return false; }
    doc->materials=doc->baseline.mapInfo().materials;
    documents.insert(id,doc);
    return true;
}
bool CWorldTerrainSession::apply(const SWorldTerrainStroke& stroke,bool forward,QString* error)
{
    if(!stroke.error.isEmpty()) { *error=stroke.error;return false; }
    QMap<QString,CTerrainEditModel> staged;
    for(auto it=stroke.maps.cbegin();it!=stroke.maps.cend();++it)
    {
        if(!documents.contains(it.key())) { *error="A stroke document is unavailable."; return false; }
        staged[it.key()]=documents[it.key()]->terrain;
        CValidationReport report;
        if(!staged[it.key()].apply(it.value(),forward,&report))
        { *error=report.toText(); return false; }
    }
    for(auto it=stroke.overlays.cbegin();it!=stroke.overlays.cend();++it) {
        if(!documents.contains(it.key())){*error="A stroke document is unavailable.";return false;}
        if(!staged.contains(it.key()))staged[it.key()]=documents[it.key()]->terrain;
        CValidationReport report;
        if(!staged[it.key()].apply(it.value(),forward,&report)){*error=report.toText();return false;}
    }
    for(auto it=stroke.tiles.cbegin();it!=stroke.tiles.cend();++it) {
        if(!documents.contains(it.key())){*error="A stroke document is unavailable.";return false;}
        if(!staged.contains(it.key()))staged[it.key()]=documents[it.key()]->terrain;
        CValidationReport report;
        if(!staged[it.key()].apply(it.value(),forward,&report)){*error=report.toText();return false;}
    }
    for(auto it=stroke.paint.cbegin();it!=stroke.paint.cend();++it) {
        if(!documents.contains(it.key()) || staged.contains(it.key())) {
            *error="Missing or mixed snapshot stroke document.";return false;
        }
        const auto doc=documents[it.key()];
        const auto& expected=forward?it.value().before:it.value().after;
        const auto& target=forward?it.value().after:it.value().before;
        if(doc->materials!=expected.materials) {*error="Material table changed outside the world stroke.";return false;}
        if(doc->terrain.maxZ()!=expected.terrain.maxZ()) {*error="Terrain scale changed outside the world stroke.";return false;}
        for(const auto& sector:doc->baseline.sectors())
            if(doc->terrain.sectorPayload(sector.sectorX,sector.sectorY)!=expected.terrain.sectorPayload(sector.sectorX,sector.sectorY)) {
                *error="Terrain changed outside the world stroke.";return false;
            }
        for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
            QVector<qint32> current,baseline,other,otherBaseline;
            doc->terrain.copyVirtualMaterialFieldState(layer,&current,&baseline);
            expected.terrain.copyVirtualMaterialFieldState(layer,&other,&otherBaseline);
            if(current!=other || baseline!=otherBaseline){*error="Material field changed outside the world stroke.";return false;}
        }
        staged.insert(it.key(),target.terrain);
    }
    for(auto it=staged.cbegin();it!=staged.cend();++it) documents[it.key()]->terrain=it.value();
    for(auto it=stroke.paint.cbegin();it!=stroke.paint.cend();++it) {
        const auto& target=forward?it.value().after:it.value().before;
        documents[it.key()]->atlas=target.atlas;
        documents[it.key()]->materials=target.materials;
    }
    return true;
}
bool CWorldTerrainSession::rebaseSavedDocument(const QString& id,const CMprDocument& baseline,QString* error)
{
    if(!documents.contains(id)){*error="Missing saved world document.";return false;}
    QSharedPointer<SWorldTerrainDocument> doc(new SWorldTerrainDocument(*documents[id]));
    const auto saved=doc->terrain;auto history=m_history;
    CTerrainEditModel disk;CValidationReport report;
    if(!disk.load(baseline,&report)){*error=report.toText();return false;}
    if(!doc->terrain.rebaseSavedBaseline(disk,saved)){*error="Saved terrain layout changed.";return false;}
    for(auto& stroke:history)if(stroke.paint.contains(id)) {
        auto& paint=stroke.paint[id];
        if(!paint.before.terrain.rebaseSavedBaseline(disk,saved)
           || !paint.after.terrain.rebaseSavedBaseline(disk,saved)){
            *error="World history cannot adopt the saved terrain layout.";return false;
        }
    }
    doc->baseline=baseline;documents[id]=doc;m_history=history;return true;
}

QStringList CWorldTerrainSession::dirtyPaths() const
{
    QStringList result;
    for(const auto& doc:documents) if(doc->terrain.isDirty() || doc->materials!=doc->baseline.mapInfo().materials) result.append(doc->path);
    return result;
}
void CWorldTerrainSession::record(const SWorldTerrainStroke& stroke)
{
    if(stroke.isEmpty()) return;
    m_history.resize(m_index); m_history.append(stroke); ++m_index;
}
const SWorldTerrainStroke* CWorldTerrainSession::nextCommand(bool forward) const
{
    if(forward ? !canRedo() : !canUndo()) return nullptr;
    return &m_history[forward?m_index:m_index-1];
}
bool CWorldTerrainSession::undo(QString* error)
{
    if(!canUndo() || !apply(m_history[m_index-1],false,error)) return false;
    --m_index; return true;
}
bool CWorldTerrainSession::redo(QString* error)
{
    if(!canRedo() || !apply(m_history[m_index],true,error)) return false;
    ++m_index; return true;
}

namespace {
QByteArray read(const QString& path)
{ QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
QString hash(const QByteArray& bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); }
bool write(const QString& path,const QByteArray& bytes)
{
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size() && f.commit();
}
bool valid(const QByteArray& bytes,const QString& path)
{
    CMprDocument doc; CTerrainEditModel terrain; CValidationReport report;
    return doc.loadBytes(bytes,path,&report) && !report.hasErrors()
        && terrain.load(doc,&report) && !report.hasErrors();
}
}
bool recoverWorldTerrainCohort(const QString& marker,QString* error)
{
    const QByteArray manifest=read(marker);
    const QJsonDocument json=QJsonDocument::fromJson(manifest);
    const QJsonObject root=json.object();
    const QJsonArray entries=root.value("files").toArray();
    if(root.value("schema").toInt()!=1 || entries.isEmpty())
    { *error="Invalid world terrain recovery manifest."; return false; }
    QVector<QSharedPointer<QLockFile>> locks;
    QSet<QString> targets;
    for(const auto& entry:entries)
    {
        const QJsonObject obj=entry.toObject();
        const QString path=obj.value("path").toString();
        const QString identity=CWorldTerrainSession::key(path);
        if(!QFileInfo(path).isAbsolute() || targets.contains(identity)
           || QFileInfo(path).suffix().compare("mpr",Qt::CaseInsensitive)!=0)
        { *error="Invalid or duplicate recovery target.";return false; }
        targets.insert(identity);
        QSharedPointer<QLockFile> lock(new QLockFile(path+".world-sculpt.lock"));
        if(!lock->tryLock(0)) { *error="A recovery target is locked: "+path; return false; }
        locks.append(lock);
        const QString current=hash(read(path));
        const QByteArray backup=read(obj.value("backup").toString());
        if((current!=obj.value("before").toString() && current!=obj.value("after").toString())
           || hash(backup)!=obj.value("before").toString() || !valid(backup,path))
        { *error="Recovery identity mismatch; no files restored: "+path; return false; }
    }
    for(const auto& entry:entries)
    {
        const QJsonObject obj=entry.toObject();
        const QString path=obj.value("path").toString();
        const QByteArray backup=read(obj.value("backup").toString());
        const QString current=hash(read(path));
        if(hash(backup)!=obj.value("before").toString()
           || (current!=obj.value("before").toString() && current!=obj.value("after").toString()))
        { *error="Recovery interrupted by an external change; keep backups and markers: "+path;return false; }
        if(!write(path,backup) || read(path)!=backup)
        { *error="Recovery incomplete; keep manifest and backups: "+path; return false; }
    }
    for(const auto& entry:entries)
    {
        const QString p=entry.toObject().value("path").toString()+".world-sculpt.json";
        if(QFileInfo::exists(p) && (read(p)!=manifest || !QFile::remove(p)))
        { *error="Restored files, but recovery marker changed or cannot be removed: "+p; return false; }
    }
    return true;
}
SWorldTerrainSaveResult saveWorldTerrainCohort(const QVector<SWorldTerrainSaveItem>& items,int failBeforePublish)
{
    SWorldTerrainSaveResult result;
    if(items.isEmpty()) { result.success=true; return result; }
    QVector<QSharedPointer<QLockFile>> locks;
    QSet<QString> paths;
    for(const auto& item:items)
    {
        const QString id=CWorldTerrainSession::key(item.path);
        if(paths.contains(id) || QFileInfo::exists(item.path+".world-sculpt.json"))
        { result.error="Duplicate target or pending recovery: "+item.path; return result; }
        paths.insert(id);
        QSharedPointer<QLockFile> lock(new QLockFile(item.path+".world-sculpt.lock"));
        if(!lock->tryLock(0)) { result.error="Save target locked: "+item.path; return result; }
        locks.append(lock);
        if(read(item.path)!=item.original || !valid(item.original,item.path) || !valid(item.candidate,item.path))
        { result.error="Source changed or candidate validation failed: "+item.path; return result; }
    }
    const QString transaction=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonArray entries;
    for(const auto& item:items)
    {
        const QString backup=item.path+".world-sculpt-"+transaction+".bak";
        if(!write(backup,item.original) || read(backup)!=item.original)
        { result.error="Backup failed; no MPR replaced: "+backup; return result; }
        result.backups.append(backup);
        QJsonObject obj;
        obj.insert("path",QFileInfo(item.path).absoluteFilePath()); obj.insert("backup",backup);
        obj.insert("before",hash(item.original)); obj.insert("after",hash(item.candidate));
        entries.append(obj);
    }
    QJsonObject root; root.insert("schema",1); root.insert("files",entries);
    const QByteArray manifest=QJsonDocument(root).toJson();
    QStringList markers;
    for(const auto& item:items)
    {
        const QString marker=item.path+".world-sculpt.json";
        if(!write(marker,manifest))
        {
            for(const auto& created:markers) if(read(created)==manifest) QFile::remove(created);
            result.error="Recovery journal failed; no MPR replaced: "+marker; return result;
        }
        markers.append(marker);
    }
    result.recoveryMarker=markers.first();
    for(int i=0;i<items.size();++i)
    {
        const auto& item=items[i];
        if(i==failBeforePublish || read(item.path)!=item.original
           || !write(item.path,item.candidate) || read(item.path)!=item.candidate)
        {
            locks.clear();
            QString recoveryError;
            const bool restored=recoverWorldTerrainCohort(markers.first(),&recoveryError);
            result.error=restored?QStringLiteral("Save failed; every MPR restored. Unsaved edits remain in memory."):
                QStringLiteral("Save failed; recovery required: ")+recoveryError;
            if(restored) result.recoveryMarker.clear();
            return result;
        }
    }
    for(const auto& marker:markers)
        if(read(marker)!=manifest || !QFile::remove(marker))
        { result.recoveryMarker=marker;result.error="MPRs written, recovery marker cleanup failed: "+marker; return result; }
    result.recoveryMarker.clear(); result.success=true; return result;
}

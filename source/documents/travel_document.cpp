#include "travel_document.h"
#include "res_archive_document.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <cmath>

namespace {
bool loadArchive(const QString& path,CResArchiveDocument* archive,QString* error)
{
    QFile file(path);QStringList errors;
    if(!file.open(QIODevice::ReadOnly)) {if(error)*error=file.errorString();return false;}
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError || !archive->loadBytes(bytes,path,&errors)) {
        if(error)*error=errors.join('\n');return false;
    }
    return true;
}
bool validRect(const QRectF& r)
{
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width())
        && std::isfinite(r.height()) && r.width()>0 && r.height()>0;
}
}
bool CTravelDocument::open(const QString& path,QString* error)
{
    CResArchiveDocument archive;
    if(!loadArchive(path,&archive,error)) return false;
    const int member=archive.uniqueMemberIndex("map.txt");
    if(member<0) {if(error)*error="A unique map.txt is required in texts.res.";return false;}
    CMapTableDocument table;QStringList errors;
    const auto bytes=archive.members()[member].payload;
    if(!table.loadBytes(bytes,&errors)) {if(error)*error=errors.join('\n');return false;}
    m_path=QFileInfo(path).absoluteFilePath();m_member=archive.members()[member].name;
    m_baseline=bytes;m_table=table;return true;
}
bool CTravelDocument::restore(const QByteArray& bytes,QString* error)
{
    CMapTableDocument table;QStringList errors;
    if(!table.loadBytes(bytes,&errors)) {if(error)*error=errors.join('\n');return false;}
    m_table=table;return true;
}
int CTravelDocument::uniqueZone(const QString& id) const
{
    int result=-1;
    for(int i=0;i<m_table.zones().size();++i)
        if(m_table.zones()[i].zoneId.compare(id,Qt::CaseInsensitive)==0) {
            if(result>=0)return -1;result=i;
        }
    return result;
}
bool CTravelDocument::replaceZone(int index,const SMapTableZone& zone,QString* error)
{
    for(const auto& exit:zone.exits)
        if((exit.hasDeployRectangle&&!validRect(exit.deployRectangle))
           ||(exit.hasRemoveRectangle&&!validRect(exit.removeRectangle))) {
            if(error)*error="Area coordinates must be finite and its size positive.";return false;
        }
    const auto result=m_table.replaceZoneAt(index,zone);
    if(!result.success && error)*error=result.toText();
    return result.success;
}
bool CTravelDocument::appendZone(const SMapTableZone& zone,QString* error)
{
    const auto result=m_table.appendZone(zone);
    if(!result.success && error)*error=result.toText();
    return result.success;
}
bool CTravelDocument::prepareSave(SMapSaveFile* file,QString* error) const
{
    CResArchiveDocument archive;
    if(!loadArchive(m_path,&archive,error))return false;
    // Other RES members may have been saved meanwhile. Preserve them, but do
    // not overwrite another editor's changes to the shared map table.
    if(archive.uniqueMemberIndex(m_member)<0 || archive.memberPayload(m_member)!=m_baseline) {
        if(error)*error="map.txt changed outside this editor. Saving was cancelled.";return false;
    }
    const auto validation=m_table.validateAllZones();
    if(!validation.success) {if(error)*error=validation.toText();return false;}
    CMapTableDocument baseline;baseline.loadBytes(m_baseline);
    for(const auto& zone:m_table.zones())for(const auto& exit:zone.exits) {
        bool connectionChanged=true;
        for(const auto& oldZone:baseline.zones())if(oldZone.zoneId==zone.zoneId)
            for(const auto& oldExit:oldZone.exits)if(oldExit.exitId==exit.exitId
                && oldExit.destinationZoneId==exit.destinationZoneId
                && oldExit.destinationExitId==exit.destinationExitId)connectionChanged=false;
        if(!connectionChanged || exit.destinationZoneId.compare("none",Qt::CaseInsensitive)==0)continue;
        bool found=false;
        for(const auto& target:m_table.zones())
            if(target.zoneId.compare(exit.destinationZoneId,Qt::CaseInsensitive)==0)
                for(const auto& entry:target.exits)if(entry.exitId==exit.destinationExitId
                    && (target.zoneType.compare("edge",Qt::CaseInsensitive)==0 || entry.hasDeployRectangle))found=true;
        if(!found){if(error)*error=QString("%1:%2: target %3:%4 does not exist or has no party deployment area.")
            .arg(zone.zoneId).arg(exit.exitId).arg(exit.destinationZoneId).arg(exit.destinationExitId);return false;}
    }
    QByteArray bytes;QStringList errors;
    if(!archive.replaceMember(m_member,QCryptographicHash::hash(m_baseline,QCryptographicHash::Sha256).toHex(),
                              snapshot(),&bytes,&errors)) {
        if(error)*error=errors.join('\n');return false;
    }
    CResArchiveDocument reopened;
    if(!reopened.loadBytes(bytes,m_path,&errors)
       ||!CResArchiveDocument::validateReplacement(archive,reopened,m_member,snapshot(),&errors)) {
        if(error)*error=errors.join('\n');return false;
    }
    file->path=m_path;file->before=archive.originalBytes();file->after=bytes;file->existed=true;
    return true;
}

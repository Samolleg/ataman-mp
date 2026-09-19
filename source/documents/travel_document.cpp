#include "travel_document.h"
#include "res_archive_document.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <cmath>
#include <QSet>
#include <limits>

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
bool CTravelDocument::createPairedRoute(const STravelRoute& r,QString* error)
{
    const auto fail=[error](const QString& message){if(error)*error=message;return false;};
    const int a=uniqueZone(r.sourceZone),b=uniqueZone(r.targetZone);
    if(a<0 || b<0 || a==b)return fail(QStringLiteral("Нужны две разные однозначные карты."));
    auto first=m_table.zones()[a],second=m_table.zones()[b];
    if(first.zoneType!="game" || second.zoneType!="game" || first.allodId.compare(second.allodId,Qt::CaseInsensitive)!=0)
        return fail(QStringLiteral("Маршрут создаётся между игровыми картами одного аллода."));
    for(const auto& zone:m_table.zones())if(zone.zoneId.compare(r.edgeZone,Qt::CaseInsensitive)==0)
        return fail(QStringLiteral("ID связующей зоны уже занят. Существующая зона не перезаписывается."));
    if(!validRect(r.sourceDeploy)||!validRect(r.sourceRemove)||!validRect(r.targetDeploy)||!validRect(r.targetRemove))
        return fail(QStringLiteral("Задайте четыре области с конечными координатами и положительными размерами."));
    if(!std::isfinite(r.worldMapPosition.x())||!std::isfinite(r.worldMapPosition.y())||!std::isfinite(r.worldMapPosition.z()))
        return fail(QStringLiteral("Нужна конечная позиция на игровой глобальной карте."));
    const auto endpoint=[&](SMapTableZone& zone,int id,const QRectF& deploy,const QRectF& remove,int edgeExit)->int {
        int index=-1;QSet<int> ids;
        for(int i=0;i<zone.exits.size();++i){
            const int existing=zone.exits[i].exitId;
            if(ids.contains(existing))return -1;
            ids.insert(existing);if(existing==id)index=i;
        }
        if(id<0 || (id>0 && index<0))return -1;
        if(id==0){
            id=1;while(ids.contains(id) && id<std::numeric_limits<int>::max())++id;
            if(ids.contains(id))return -1;
            SMapTableExit entry;entry.exitId=id;zone.exits.append(entry);index=zone.exits.size()-1;
        }
        auto& entry=zone.exits[index];entry.destinationZoneId=r.edgeZone;entry.destinationExitId=edgeExit;
        entry.hasDeployRectangle=entry.hasRemoveRectangle=true;
        entry.deployRectangle=deploy;entry.removeRectangle=remove;return id;
    };
    const int aid=endpoint(first,r.sourceExit,r.sourceDeploy,r.sourceRemove,1);
    const int bid=endpoint(second,r.targetExit,r.targetDeploy,r.targetRemove,2);
    if(aid<0 || bid<0)return fail(QStringLiteral("Номер конца маршрута отсутствует или неоднозначен."));
    SMapTableZone edge;edge.zoneId=r.edgeZone;edge.allodId=first.allodId;edge.zoneType="edge";
    SMapTableExit left;left.exitId=1;left.destinationZoneId=first.zoneId;left.destinationExitId=aid;
    SMapTableExit right;right.exitId=2;right.destinationZoneId=second.zoneId;right.destinationExitId=bid;
    edge.exits={left,right};
    // Build all three records on a private document. No partial mutation on failure.
    CTravelDocument candidate=*this;
    if(!candidate.replaceZone(a,first,error)||!candidate.replaceZone(b,second,error)||!candidate.appendZone(edge,error))return false;
    const int edgeIndex=candidate.uniqueZone(edge.zoneId);
    auto raw=candidate.table().zones()[edgeIndex].rawBlock;
    const int header=raw.indexOf("#zone "),lineEnd=raw.indexOf('\n',header);
    if(header<0 || lineEnd<0)return fail(QStringLiteral("Не удалось сформировать связующую зону."));
    raw.insert(lineEnd+1,QString("#position\n%1 %2 %3\n")
        .arg(r.worldMapPosition.x(),0,'g',9).arg(r.worldMapPosition.y(),0,'g',9).arg(r.worldMapPosition.z(),0,'g',9));
    const auto positioned=candidate.m_table.replaceRawZoneSpanAt(edgeIndex,raw);
    if(!positioned.success)return fail(positioned.toText());
    const auto& zones=candidate.table().zones();
    const auto matching=[&](int zone,int id,const QString& target,int targetId){
        int count=0;for(const auto& exit:zones[zone].exits)
            if(exit.exitId==id && exit.destinationZoneId==target && exit.destinationExitId==targetId)++count;
        return count==1;
    };
    if(!matching(a,aid,edge.zoneId,1)||!matching(b,bid,edge.zoneId,2)
       ||!matching(edgeIndex,1,first.zoneId,aid)||!matching(edgeIndex,2,second.zoneId,bid))
        return fail(QStringLiteral("Проверка парных связей не пройдена."));
    m_table=candidate.m_table;return true;
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

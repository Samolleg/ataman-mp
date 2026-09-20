#include "travel_document.h"
#include "res_archive_document.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <cmath>
#include <QSet>
#include <QRegularExpression>
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
bool CTravelDocument::deleteExit(int zoneIndex,int exitIndex,QString* error)
{
    const auto zones=m_table.zones();
    if(zoneIndex<0 || zoneIndex>=zones.size() || exitIndex<0 || exitIndex>=zones[zoneIndex].exits.size()) {
        if(error)*error=QStringLiteral("Запись для удаления не найдена.");return false;
    }
    const auto zoneId=zones[zoneIndex].zoneId;
    if(uniqueZone(zoneId)!=zoneIndex) {
        if(error)*error=QStringLiteral("Имя зоны неоднозначно; удаление отменено.");return false;
    }
    const int ordinal=exitIndex+1;
    CMapTableDocument candidate=m_table;
    // Disconnect incoming links before deleting the target. Keep all other
    // records and geometry; the table migrates ordinals after removal.
    for(int z=0;z<zones.size();++z) {
        auto zone=candidate.zones()[z];bool changed=false;
        for(auto& entry:zone.exits)
            if(entry.destinationZoneId.compare(zoneId,Qt::CaseInsensitive)==0 && entry.destinationExitId==ordinal) {
                entry.destinationZoneId="none";entry.destinationExitId=0;changed=true;
            }
        if(changed) {
            const auto result=candidate.replaceZoneAt(z,zone);
            if(!result.success){if(error)*error=result.toText();return false;}
        }
    }
    auto zone=candidate.zones()[zoneIndex];zone.exits.removeAt(exitIndex);
    const auto result=candidate.replaceZoneAt(zoneIndex,zone);
    if(!result.success){if(error)*error=result.toText();return false;}
    m_table=candidate;return true;
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
        if(!found){if(error)*error=QStringLiteral("Переход %1:%2 направлен в %3:%4, но там нет записи с областью появления. Создайте область появления у записи №%4 на карте %3 либо выберите другой существующий вход. Для полного парного маршрута используйте «Создать маршрут между картами…».")
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

// Change only the global marker: links, rectangles and unrelated source text survive.
bool CTravelDocument::setZonePosition(int index,const QVector3D& position,QString* error)
{
    const auto fail=[error](const QString& text){if(error)*error=text;return false;};
    if(index<0 || index>=m_table.zones().size() || !std::isfinite(position.x())
        || !std::isfinite(position.y()) || !std::isfinite(position.z()))
        return fail(QStringLiteral("Неверная зона или координаты метки."));
    QString raw=m_table.zones()[index].rawBlock;
    const QRegularExpression tag(QStringLiteral(R"((?:^|\n)[ \t]*#position[^\r\n]*\r?\n)"));
    auto matches=tag.globalMatch(raw);
    if(!matches.hasNext())return fail(QStringLiteral("У зоны отсутствует #position."));
    const auto match=matches.next();
    if(matches.hasNext())return fail(QStringLiteral("У зоны несколько #position; положение неоднозначно."));
    const int start=match.capturedEnd();
    int end=raw.indexOf('\n',start);if(end<0)end=raw.size();
    if(end>start && raw[end-1]=='\r')--end;
    const QRegularExpression values(QStringLiteral(R"(^([ \t]*)([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+))"));
    const auto old=values.match(raw.mid(start,end-start));
    if(!old.hasMatch())return fail(QStringLiteral("Не удалось прочитать #position."));
    for(int i=2;i<=4;++i){bool ok=false;const double v=old.captured(i).toDouble(&ok);if(!ok||!std::isfinite(v))return fail(QStringLiteral("Неверное значение #position."));}
    const int valueStart=start+old.capturedStart(2);
    raw.replace(valueStart,old.capturedEnd(4)-old.capturedStart(2),QString("%1 %2 %3")
        .arg(position.x(),0,'g',9).arg(position.y(),0,'g',9).arg(position.z(),0,'g',9));
    const auto result=m_table.replaceRawZoneSpanAt(index,raw);
    if(!result.success && error)*error=result.toText();
    return result.success;
}

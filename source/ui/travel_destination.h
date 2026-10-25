#pragma once
#include "map_table_document.h"
#include <QMap>
#include <QFileInfo>

// Presentation of the existing map-table links; never rewrites their identity.
namespace travel {
inline int zoneIndex(const QVector<SMapTableZone>& zones,const QString& id) {
    int result=-1;
    for(int i=0;i<zones.size();++i)if(zones[i].zoneId.compare(id,Qt::CaseInsensitive)==0) {
        if(result>=0)return -1;result=i;
    }
    return result;
}
inline int entryIndex(const SMapTableZone& zone,int id) {
    int result=-1;for(int i=0;i<zone.exits.size();++i)if(zone.exits[i].exitId==id) {
        if(result>=0)return -1;result=i;
    }return result;
}
inline bool edge(const SMapTableZone& zone){return zone.zoneType.compare("edge",Qt::CaseInsensitive)==0;}
inline QString name(const SMapTableZone& zone,const QMap<QString,QString>& names={}) {
    QString title=names.value(zone.zoneId);
    QString fileName=QFileInfo(zone.mprName).completeBaseName();
    if(fileName.startsWith("zone",Qt::CaseInsensitive))fileName[0]='Z';
    if(!title.isEmpty() && !fileName.isEmpty() && title.compare(fileName,Qt::CaseInsensitive)!=0)title+=" / "+fileName;
    if(title.isEmpty())title=fileName;
    if(title.isEmpty())title=zone.zoneId;
    return title.compare(zone.zoneId,Qt::CaseInsensitive)==0?zone.zoneId:title+" ("+zone.zoneId+")";
}
struct Destination {
    int zone{-1},entry{-1},route{-1},nearEnd{-1},farEnd{-1};
    QString error;
};
inline QString compactName(const SMapTableZone& zone,const QMap<QString,QString>& names,bool withFile=false) {
    QString title=names.value(zone.zoneId);
    QString file=QFileInfo(zone.mprName).completeBaseName();
    if(file.startsWith("zone",Qt::CaseInsensitive))file[0]='Z';
    if(title.isEmpty())return file.isEmpty()?zone.zoneId:file;
    if(withFile && !file.isEmpty() && title.compare(file,Qt::CaseInsensitive)!=0)title+=" ("+file+")";
    return title;
}
inline QString compactTarget(const QVector<SMapTableZone>& zones,const Destination& d,const QMap<QString,QString>& names,bool withFile=false) {
    if(d.zone<0 || d.entry<0)return QStringLiteral("назначение не задано");
    return QStringLiteral("%1, вход %2").arg(compactName(zones[d.zone],names,withFile)).arg(zones[d.zone].exits[d.entry].exitId);
}
inline Destination resolve(const QVector<SMapTableZone>& zones,const QString& id,int entry) {
    Destination d;d.zone=zoneIndex(zones,id);
    if(d.zone<0){d.error=QStringLiteral("Целевая зона отсутствует или неоднозначна");return d;}
    d.entry=entryIndex(zones[d.zone],entry);
    if(d.entry<0){d.error=QStringLiteral("Вход не существует");return d;}
    if(edge(zones[d.zone])) {
        d.route=d.zone;d.nearEnd=d.entry;
        if(zones[d.zone].exits.size()!=2){d.error=QStringLiteral("У маршрута должно быть два конца; используйте расширенные поля");return d;}
        d.farEnd=1-d.nearEnd;
        const auto& endpoint=zones[d.route].exits[d.farEnd];
        d.zone=zoneIndex(zones,endpoint.destinationZoneId);
        d.entry=d.zone<0?-1:entryIndex(zones[d.zone],endpoint.destinationExitId);
        if(d.zone<0 || d.entry<0){d.error=QStringLiteral("Вход не существует на дальнем конце маршрута");return d;}
        if(edge(zones[d.zone])){d.error=QStringLiteral("Дальний конец ведёт в другой маршрут; используйте расширенные поля");return d;}
    }
    if(!zones[d.zone].exits[d.entry].hasDeployRectangle)d.error=QStringLiteral("Нет области появления на целевой карте");
    return d;
}
inline QString entryDetails(const SMapTableExit& e,const QString& backZone=QString(),int backEntry=0) {
    QString text=QStringLiteral("№%1 — %2; %3").arg(e.exitId)
        .arg(e.hasDeployRectangle?QStringLiteral("появление: X %1, Y %2, %3 × %4")
             .arg(e.deployRectangle.x()).arg(e.deployRectangle.y()).arg(e.deployRectangle.width()).arg(e.deployRectangle.height())
             :QStringLiteral("нет области появления"))
        .arg(e.destinationZoneId.compare("none",Qt::CaseInsensitive)!=0 && e.hasEffectiveRemove()
             ?QStringLiteral("переход → %1:%2").arg(e.destinationZoneId).arg(e.destinationExitId):QStringLiteral("нет исходящего перехода"));
    if(!backZone.isEmpty())text+=QStringLiteral("; обратный переход: %1").arg(e.hasEffectiveRemove()
        && e.destinationZoneId.compare(backZone,Qt::CaseInsensitive)==0 && e.destinationExitId==backEntry
        ?QStringLiteral("есть"):QStringLiteral("нет"));
    return text;
}
inline bool pointsTo(const SMapTableExit& e,const SMapTableZone& z,int id) {
    return e.destinationZoneId.compare(z.zoneId,Qt::CaseInsensitive)==0 && e.destinationExitId==id;
}
inline bool reciprocal(const QVector<SMapTableZone>& zones,int source,const SMapTableExit& e,const Destination& d) {
    if(!d.error.isEmpty() || source<0 || !e.hasDeployRectangle)return false;
    const auto& back=zones[d.zone].exits[d.entry];
    if(!back.hasEffectiveRemove())return false;
    if(d.route<0)return pointsTo(back,zones[source],e.exitId);
    return pointsTo(zones[d.route].exits[d.nearEnd],zones[source],e.exitId)
        && pointsTo(back,zones[d.route],zones[d.route].exits[d.farEnd].exitId);
}
inline QString targetLabel(const QVector<SMapTableZone>& zones,const Destination& d,const QMap<QString,QString>& names={}) {
    if(d.zone<0 || d.entry<0)return d.error;
    QString text=QStringLiteral("%1, вход №%2").arg(name(zones[d.zone],names)).arg(zones[d.zone].exits[d.entry].exitId);
    if(d.route>=0)text+=QStringLiteral(" (через маршрут %1)").arg(zones[d.route].zoneId);
    return text;
}
}

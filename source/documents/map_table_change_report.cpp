#include "map_table_change_report.h"
#include "map_table_document.h"

namespace {
void unique(QStringList* values, const QString& value)
{ if(values && !values->contains(value)) values->append(value); }
}

QStringList SMapTableChangeAnalysis::affectedZoneIds() const
{
    QStringList result;
    for(const SMapTableSemanticChange& zone : zones) unique(&result, zone.zoneId);
    return result;
}

bool SMapTableChangeAnalysis::changesPhysicalResources(const QString& zoneId) const
{
    for(const SMapTableSemanticChange& zone : zones)
        if(zone.zoneId.compare(zoneId, Qt::CaseInsensitive) == 0
           && (zone.categories.contains("resources")
               || zone.categories.contains("registration"))) return true;
    return false;
}

SMapTableChangeAnalysis analyzeMapTableChange(const QByteArray& before,
                                              const QByteArray& after)
{
    SMapTableChangeAnalysis result;
    CMapTableDocument oldDoc, newDoc;
    QStringList errors;
    if(!oldDoc.loadBytes(before, &errors))
    { result.error = "Previous map.txt could not be parsed: " + errors.join(" | "); return result; }
    errors.clear();
    if(!newDoc.loadBytes(after, &errors))
    { result.error = "Updated map.txt could not be parsed: " + errors.join(" | "); return result; }
    const auto& oldZones = oldDoc.zones();
    const auto& newZones = newDoc.zones();
    for(int index = 0; index < qMax(oldZones.size(), newZones.size()); ++index)
    {
        SMapTableSemanticChange change;
        if(index >= oldZones.size() || index >= newZones.size())
        {
            const SMapTableZone& zone = index < newZones.size() ? newZones.at(index) : oldZones.at(index);
            change.zoneId = zone.zoneId;
            change.categories.append("registration");
        }
        else
        {
            const SMapTableZone& oldZone = oldZones.at(index);
            const SMapTableZone& newZone = newZones.at(index);
            change.zoneId = newZone.zoneId.isEmpty() ? oldZone.zoneId : newZone.zoneId;
            if(oldZone.zoneId != newZone.zoneId || oldZone.allodId != newZone.allodId
               || oldZone.zoneType != newZone.zoneType) change.categories.append("zone metadata");
            if(oldZone.mprName != newZone.mprName || oldZone.mobName != newZone.mobName)
                change.categories.append("resources");
            if(oldZone.maps != newZone.maps) change.categories.append("minimap/objectives");
            if(oldZone.exits != newZone.exits) change.categories.append("entrances");
            if(oldZone.rawBlock != newZone.rawBlock && change.categories.isEmpty())
                change.categories.append("comments/unknown metadata");
        }
        if(!change.categories.isEmpty())
        {
            for(const QString& category : change.categories) unique(&result.categories, category);
            result.zones.append(change);
        }
    }
    if(before != after && result.zones.isEmpty()) result.categories.append("global comments/metadata");
    result.valid = true;
    return result;
}

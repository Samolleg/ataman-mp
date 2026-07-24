#include "synthetic_map_table_archive.h"

#include <QCryptographicHash>
#include <QRectF>

#include "map_table_document.h"
#include "res_archive_document.h"

namespace {
void addError(QStringList* errors, const QString& text)
{
    if(errors)
        errors->append(text);
}
}

bool buildSyntheticMapTableArchive(const QByteArray& sourceArchive,
                                   const QString& sourceName,
                                   const QString& zoneId,
                                   const QString& allodId,
                                   const QString& mapName,
                                   const QPointF& spawn,
                                   QByteArray* outputArchive,
                                   QStringList* errors)
{
    if(outputArchive)
        outputArchive->clear();
    if(sourceArchive.isEmpty() || zoneId.isEmpty() || allodId.isEmpty()
       || mapName.isEmpty() || !outputArchive)
    {
        addError(errors, "Synthetic map-table staging received incomplete input.");
        return false;
    }

    CResArchiveDocument baseline;
    QStringList localErrors;
    if(!baseline.loadBytes(sourceArchive, sourceName, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    const QByteArray mapBytes = baseline.memberPayload("map.txt");
    if(mapBytes.isEmpty())
    {
        addError(errors, "The selected texts.res has no non-empty map.txt member.");
        return false;
    }

    CMapTableDocument table;
    if(!table.loadBytes(mapBytes, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    SMapTableZone zone;
    zone.zoneId = zoneId;
    zone.allodId = allodId;
    zone.zoneType = "game";
    zone.mprName = mapName;
    zone.mobName = mapName;
    // Deliberately omit #maps. This keeps both UI texture names empty. The
    // bridge sets only the two numeric terrain dimensions after lookup.
    SMapTableExit exit;
    exit.exitId = 1;
    exit.destinationZoneId = "none";
    exit.destinationExitId = 0;
    exit.hasDeployRectangle = true;
    exit.hasRemoveRectangle = true;
    const qreal radius = 0.75;
    exit.deployRectangle = QRectF(spawn.x() - radius, spawn.y() - radius,
                                  radius * 2.0, radius * 2.0);
    exit.removeRectangle = exit.deployRectangle;
    zone.exits.append(exit);
    const SMapTableEditResult append = table.appendZone(zone);
    if(!append.success)
    {
        if(errors) errors->append(append.errors);
        return false;
    }
    const QByteArray replacement = table.serialize();
    const QByteArray expectedMapHash = QCryptographicHash::hash(
        mapBytes, QCryptographicHash::Sha256).toHex();
    QByteArray staged;
    if(!baseline.replaceMember("map.txt", expectedMapHash, replacement,
                               &staged, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    CResArchiveDocument candidate;
    if(!candidate.loadBytes(staged, sourceName + " [staged]", &localErrors)
       || !CResArchiveDocument::validateReplacement(
           baseline, candidate, "map.txt", replacement, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    CMapTableDocument reopened;
    if(!reopened.loadBytes(candidate.memberPayload("map.txt"), &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    int matches = 0;
    for(const SMapTableZone& item : reopened.zones())
    {
        if(item.zoneId.compare(zoneId, Qt::CaseInsensitive) != 0)
            continue;
        ++matches;
        if(item.mprName.compare(mapName, Qt::CaseInsensitive) != 0
           || item.mobName.compare(mapName, Qt::CaseInsensitive) != 0
           || item.maps.present || item.exits.size() != 1)
            addError(&localErrors, "The staged synthetic zone failed semantic reopen validation.");
    }
    if(matches != 1)
        addError(&localErrors, "The staged map table does not contain exactly one synthetic zone.");
    if(!localErrors.isEmpty())
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    *outputArchive = staged;
    return true;
}

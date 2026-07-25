#include "minimap_catalog_registration.h"

#include "map_table_document.h"

SMinimapCatalogRegistrationResult prepareMinimapCatalogRegistration(
    const SMinimapCatalogRegistrationRequest& request)
{
    SMinimapCatalogRegistrationResult result;
    result.updatedMapTableBytes = request.mapTableBytes;
    if(!request.textureExportSucceeded)
    {
        result.error = "Catalog registration is unavailable until texture export succeeds.";
        return result;
    }
    if(!request.registrationConfirmed)
    {
        result.error = "Catalog registration requires explicit confirmation.";
        return result;
    }
    const QString zoneId = request.zoneId.trimmed();
    const QString textureName = request.textureName.trimmed();
    if(zoneId.isEmpty() || textureName.isEmpty())
    {
        result.error = "Zone and minimap texture names are required.";
        return result;
    }

    CMapTableDocument document;
    QStringList errors;
    if(!document.loadBytes(request.mapTableBytes, &errors))
    {
        result.error = "map.txt could not be parsed: " + errors.join(" | ");
        return result;
    }
    int zoneIndex = -1;
    for(int index = 0; index < document.zones().size(); ++index)
    {
        if(document.zones().at(index).zoneId.compare(zoneId, Qt::CaseInsensitive) != 0)
            continue;
        if(zoneIndex >= 0)
        {
            result.error = "Catalog registration is ambiguous because the zone ID is repeated.";
            return result;
        }
        zoneIndex = index;
    }
    if(zoneIndex < 0)
    {
        result.error = "The requested zone is not present in map.txt.";
        return result;
    }

    SMapTableZone replacement = document.zones().at(zoneIndex);
    if(!replacement.maps.present)
    {
        result.error = "The requested zone has no #maps metadata to update.";
        return result;
    }
    result.preservedObjectivesTexture = replacement.maps.objectivesTexture;
    if(replacement.maps.minimapTexture == textureName)
    {
        result.success = true;
        result.noOp = true;
        return result;
    }
    replacement.maps.minimapTexture = textureName;
    const SMapTableEditResult edit = document.replaceZoneAt(zoneIndex, replacement);
    if(!edit.success)
    {
        result.error = edit.errors.join(" | ");
        return result;
    }
    result.updatedMapTableBytes = document.serialize();
    result.changedFields = edit.changedFields;
    result.success = true;
    return result;
}

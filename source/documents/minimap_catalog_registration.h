#ifndef MINIMAP_CATALOG_REGISTRATION_H
#define MINIMAP_CATALOG_REGISTRATION_H

#include <QByteArray>
#include <QString>
#include <QStringList>

struct SMinimapCatalogRegistrationRequest
{
    QByteArray mapTableBytes;
    QString zoneId;
    QString textureName;
    bool textureExportSucceeded{false};
    bool registrationConfirmed{false};
};

struct SMinimapCatalogRegistrationResult
{
    bool success{false};
    bool noOp{false};
    QByteArray updatedMapTableBytes;
    QString preservedObjectivesTexture;
    QStringList changedFields;
    QString error;
};

// Prepares a lossless map.txt update after, and only after, the texture RES
// transaction has succeeded. Committing map.txt remains a separate explicit
// verified-save action so a texture export never silently edits the catalog.
SMinimapCatalogRegistrationResult prepareMinimapCatalogRegistration(
    const SMinimapCatalogRegistrationRequest& request);

#endif

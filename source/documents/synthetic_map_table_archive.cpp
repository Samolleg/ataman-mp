#include "synthetic_map_table_archive.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRectF>

#include "map_table_document.h"
#include "minimap_catalog_registration.h"
#include "mmp_inspector.h"
#include "res_archive_document.h"

namespace {
const char kPlaceholderTextureName[] = "enduranceplaceholder";

void addError(QStringList* errors, const QString& text)
{
    if(errors)
        errors->append(text);
}

QByteArray checkerboardMmp()
{
    constexpr int width = 256;
    constexpr int height = 256;
    QByteArray pixels;
    pixels.reserve(width * height * 4);
    for(int y = 0; y < height; ++y)
    {
        for(int x = 0; x < width; ++x)
        {
            const bool white = ((x / 16) + (y / 16)) % 2 == 0;
            const char value = char(white ? 0xFF : 0xB0);
            pixels.append(value); // B
            pixels.append(value); // G
            pixels.append(value); // R
            pixels.append(char(0xFF));
        }
    }
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x00504D4D) << quint32(width) << quint32(height)
           << quint32(1) << quint32(0x00008888) << quint32(32)
           << quint32(0xFF000000) << quint32(24) << quint32(8)
           << quint32(0x00FF0000) << quint32(16) << quint32(8)
           << quint32(0x0000FF00) << quint32(8) << quint32(8)
           << quint32(0x000000FF) << quint32(0) << quint32(8)
           << quint32(0);
    result.append(pixels);
    return result;
}
}

QString syntheticMapPlaceholderTextureName()
{
    return QString::fromLatin1(kPlaceholderTextureName);
}

QString temporaryMinimapRuntimeTextureName(const QByteArray& mmpPayload,
                                           const QString& runId)
{
    if(mmpPayload.isEmpty() || runId.trimmed().isEmpty())
        return QString();
    const QByteArray identity = runId.toUtf8() + QByteArray(1, '\0') + mmpPayload;
    return QStringLiteral("eimmap_") + QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256)
            .toHex().left(12));
}

bool buildSyntheticMapPlaceholderArchive(QByteArray* outputArchive,
                                         QStringList* errors)
{
    return buildSyntheticMapTextureArchive(syntheticMapPlaceholderTextureName(),
                                           checkerboardMmp(), outputArchive, errors);
}

bool buildSyntheticMapTextureArchive(const QString& textureName,
                                     const QByteArray& mmpPayload,
                                     QByteArray* outputArchive,
                                     QStringList* errors)
{
    if(outputArchive)
        outputArchive->clear();
    QString normalizedName = textureName.trimmed();
    if(normalizedName.endsWith(".mmp", Qt::CaseInsensitive))
        normalizedName.chop(4);
    const ei::SMmpInspection inspection = ei::inspectMmp(mmpPayload);
    if(!outputArchive || normalizedName.isEmpty() || normalizedName.contains('/')
       || normalizedName.contains('\\') || !inspection.valid
        || inspection.width != inspection.height || inspection.width < 64
        || inspection.width > 2048
        || (inspection.width & (inspection.width - 1)) != 0)
    {
        addError(errors, inspection.valid
            ? "A safe texture name, square power-of-two MMP from 64 to 2048 pixels, and output RES are required."
            : "Temporary minimap MMP validation failed: " + inspection.diagnostic);
        return false;
    }
    QByteArray emptyArchive(16, '\0');
    QDataStream header(&emptyArchive, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(0x019CE23C) << quint32(0) << quint32(16) << quint32(0);
    CResArchiveDocument baseline;
    QStringList localErrors;
    if(!baseline.loadBytes(emptyArchive, "Endurance temporary placeholder", &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    const QString memberName = normalizedName + ".mmp";
    if(!baseline.buildWithAppendedMember(memberName, mmpPayload, 0,
                                         outputArchive, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    CResArchiveDocument candidate;
    if(!candidate.loadBytes(*outputArchive, "Endurance temporary placeholder [reopen]",
                            &localErrors)
       || candidate.members().size() != 1
       || candidate.memberPayload(memberName) != mmpPayload)
    {
        if(errors) errors->append(localErrors);
        addError(errors, "The standalone placeholder RES failed semantic reopen validation.");
        return false;
    }
    return true;
}

bool buildSyntheticMapTableArchive(const QByteArray& sourceArchive,
                                   const QString& sourceName,
                                   const QString& zoneId,
                                   const QString& allodId,
                                   const QString& mapName,
                                   int mapSizeX,
                                   int mapSizeY,
                                   const QPointF& spawn,
                                   QByteArray* outputArchive,
                                   QStringList* errors)
{
    if(outputArchive)
        outputArchive->clear();
    if(sourceArchive.isEmpty() || zoneId.isEmpty() || allodId.isEmpty()
       || mapName.isEmpty() || mapSizeX <= 0 || mapSizeY <= 0
       || mapSizeX > 1024 || mapSizeY > 1024 || !outputArchive)
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
    // A gameplay-zone mission screen constructs its minimap immediately and
    // expects both a valid map size and a successfully loaded texture. The
    // bridge registers a standalone, run-owned RES containing this neutral
    // placeholder before invoking the New Game bootstrap.
    zone.maps.present = true;
    zone.maps.sectorWidth = mapSizeX;
    zone.maps.sectorHeight = mapSizeY;
    zone.maps.minimapTexture = syntheticMapPlaceholderTextureName();
    zone.maps.objectivesTexture = syntheticMapPlaceholderTextureName();
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
           || !item.maps.present
           || item.maps.sectorWidth != mapSizeX
           || item.maps.sectorHeight != mapSizeY
           || item.maps.minimapTexture.compare(syntheticMapPlaceholderTextureName(), Qt::CaseInsensitive) != 0
           || item.maps.objectivesTexture.compare(syntheticMapPlaceholderTextureName(), Qt::CaseInsensitive) != 0
           || item.exits.size() != 1)
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

bool stageTemporaryMinimapReference(const QByteArray& stagedMapTableArchive,
                                    const QString& sourceName,
                                    const QString& zoneId,
                                    const QString& textureName,
                                    QByteArray* outputArchive,
                                    QStringList* errors)
{
    if(outputArchive)
        outputArchive->clear();
    CResArchiveDocument stagedDocument;
    QStringList localErrors;
    if(!outputArchive || !stagedDocument.loadBytes(
           stagedMapTableArchive, sourceName, &localErrors))
    {
        if(errors) errors->append(localErrors);
        addError(errors, "Could not reopen the private map table for minimap preview.");
        return false;
    }
    SMinimapCatalogRegistrationRequest registration;
    registration.mapTableBytes = stagedDocument.memberPayload("map.txt");
    registration.zoneId = zoneId;
    registration.textureName = textureName;
    registration.textureExportSucceeded = true;
    registration.registrationConfirmed = true;
    const SMinimapCatalogRegistrationResult prepared =
        prepareMinimapCatalogRegistration(registration);
    const QByteArray expectedHash = QCryptographicHash::hash(
        registration.mapTableBytes, QCryptographicHash::Sha256).toHex();
    QByteArray candidate;
    if(!prepared.success
       || !stagedDocument.replaceMember("map.txt", expectedHash,
           prepared.updatedMapTableBytes, &candidate, &localErrors))
    {
        if(errors) errors->append(localErrors);
        addError(errors, prepared.success
            ? "Could not stage the private minimap map.txt replacement."
            : prepared.error);
        return false;
    }
    CResArchiveDocument reopened;
    if(!reopened.loadBytes(candidate, sourceName + " [reopen]", &localErrors)
       || reopened.memberPayload("map.txt") != prepared.updatedMapTableBytes)
    {
        if(errors) errors->append(localErrors);
        addError(errors, "The private minimap map-table staging failed fresh reopen validation.");
        return false;
    }
    *outputArchive = candidate;
    return true;
}

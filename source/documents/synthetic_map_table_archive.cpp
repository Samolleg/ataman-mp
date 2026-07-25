#include "synthetic_map_table_archive.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRectF>

#include "map_table_document.h"
#include "res_archive_document.h"

namespace {
// Keep the original menus.res member table and hash chains intact. The shipped
// game accepts the append-only replacement format already used for map.txt,
// while a completely rebuilt menus.res table proved unstable at runtime.
// mainmenu00 is only the transient main-menu background and can safely serve
// as the private test run's synthetic minimap/objective alias.
const char kPlaceholderTextureName[] = "mainmenu00";

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
           << quint32(pixels.size()) << quint32(0x33544E50) << quint32(32);
    for(int index = 0; index < 13; ++index)
        stream << quint32(0);
    result.append(pixels);
    return result;
}
}

QString syntheticMapPlaceholderTextureName()
{
    return QString::fromLatin1(kPlaceholderTextureName);
}

bool buildSyntheticMapPlaceholderArchive(const QByteArray& sourceArchive,
                                         const QString& sourceName,
                                         QByteArray* outputArchive,
                                         QStringList* errors)
{
    CResArchiveDocument baseline;
    QStringList localErrors;
    if(!baseline.loadBytes(sourceArchive, sourceName, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    const QString memberName = syntheticMapPlaceholderTextureName() + ".mmp";
    const QByteArray original = baseline.memberPayload(memberName);
    if(original.isEmpty())
    {
        addError(errors, QString("The placeholder source member %1 is missing.")
                          .arg(memberName));
        return false;
    }
    const QByteArray expectedHash = QCryptographicHash::hash(
        original, QCryptographicHash::Sha256).toHex();
    const QByteArray checker = checkerboardMmp();
    if(!baseline.replaceMember(memberName, expectedHash, checker,
                               outputArchive, &localErrors))
    {
        if(errors) errors->append(localErrors);
        return false;
    }
    CResArchiveDocument candidate;
    if(!candidate.loadBytes(*outputArchive, sourceName + " [placeholder]",
                            &localErrors)
       || !CResArchiveDocument::validateReplacement(
           baseline, candidate, memberName, checker, &localErrors))
    {
        if(errors) errors->append(localErrors);
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
    // expects both a valid map size and a successfully loaded texture. A neutral
    // generated checker replaces an existing texture only in a run-local,
    // append-only menus.res derivative. The objectives screen uses the same
    // owned placeholder until generated map captures are available.
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

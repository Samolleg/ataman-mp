#include "terrain_atlas_profile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "endurance_document.h"

namespace {
const QJsonArray& presetArray(CValidationReport* report)
{
    static QJsonArray presets;
    static bool loaded = false;
    if(loaded) return presets;
    loaded = true;
    QFile file(QStringLiteral(":/terrain_atlas_presets.json"));
    if(!file.open(QIODevice::ReadOnly))
    {
        if(report) report->add("terrain.atlas_preset_resource",
            EValidationSeverity::Error,
            "The built-in Material atlas preset catalog could not be opened");
        return presets;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject root = document.object();
    if(error.error != QJsonParseError::NoError
       || root.value(QStringLiteral("schemaVersion")).toInt() != 1
       || !root.value(QStringLiteral("presets")).isArray())
    {
        if(report) report->add("terrain.atlas_preset_schema",
            EValidationSeverity::Error,
            "The built-in Material atlas preset catalog is invalid",
            error.errorString());
        return presets;
    }
    presets = root.value(QStringLiteral("presets")).toArray();
    return presets;
}

ETerrainAtlasUnderwaterRole parseUnderwaterRole(const QString& value)
{
    if(value == QStringLiteral("underwater-only"))
        return ETerrainAtlasUnderwaterRole::UnderwaterOnly;
    if(value == QStringLiteral("mixed"))
        return ETerrainAtlasUnderwaterRole::Mixed;
    return ETerrainAtlasUnderwaterRole::Surface;
}
}

int CTerrainAtlasPresetCatalog::presetCount(CValidationReport* report)
{
    return presetArray(report).size();
}

STerrainAtlasProfile CTerrainAtlasPresetCatalog::exactProfile(
    const QString& mprNamespace, const QByteArray& atlasPixelSha256,
    int atlasSlotCount, CValidationReport* report)
{
    const QString requestedNamespace = mprNamespace.trimmed();
    const QByteArray requestedHash = atlasPixelSha256.toLower();
    for(const QJsonValue& value : presetArray(report))
    {
        const QJsonObject preset = value.toObject();
        if(preset.value(QStringLiteral("namespace")).toString().compare(
               requestedNamespace, Qt::CaseInsensitive) != 0
           || preset.value(QStringLiteral("slotCount")).toInt() != atlasSlotCount)
            continue;
        bool bindingMatches = false;
        for(const QJsonValue& bindingValue : preset.value(QStringLiteral("bindings")).toArray())
            bindingMatches |= bindingValue.toObject().value(
                QStringLiteral("pixelSha256")).toString().toLatin1().toLower() == requestedHash;
        if(!bindingMatches)
            return {};

        const QByteArray signatures = QByteArray::fromBase64(
            preset.value(QStringLiteral("signaturesBase64")).toString().toLatin1());
        if(signatures.size() != atlasSlotCount * 4)
        {
            if(report) report->add("terrain.atlas_preset_signatures",
                EValidationSeverity::Error,
                "The matching Material atlas preset has an invalid corner table",
                requestedNamespace);
            return {};
        }
        STerrainAtlasProfile profile;
        profile.schemaVersion = 7;
        profile.mprNamespace = requestedNamespace;
        profile.atlasPixelSha256 = requestedHash;
        profile.atlasSlotCount = atlasSlotCount;
        profile.provenance = QStringLiteral("built-in-material-atlas-v2");
        profile.approved = true;
        for(const QJsonValue& familyValue : preset.value(QStringLiteral("families")).toArray())
        {
            const QJsonObject item = familyValue.toObject();
            STerrainAtlasProfileFamily family;
            family.id = item.value(QStringLiteral("id")).toInt();
            family.name = item.value(QStringLiteral("name")).toString();
            const QJsonArray layers = item.value(QStringLiteral("allowedLayers")).toArray();
            // Defaults are land-only for legacy profiles, so clear land when
            // this authored family is explicitly overlay-only.
            if(!layers.isEmpty())
            {
                family.landAllowed = false;
                family.overlayAllowed = false;
                for(const QJsonValue& layer : layers)
                {
                    family.landAllowed |= layer.toString() == QStringLiteral("land");
                    family.overlayAllowed |= layer.toString() == QStringLiteral("overlay");
                }
            }
            family.layer = !family.landAllowed && family.overlayAllowed
                ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
            family.underwaterRole = parseUnderwaterRole(
                item.value(QStringLiteral("underwaterRole")).toString());
            family.underwater = family.underwaterRole
                == ETerrainAtlasUnderwaterRole::UnderwaterOnly;
            family.tileType = item.value(QStringLiteral("tileType")).toInt(-1);
            family.thumbnailSlot = item.value(QStringLiteral("thumbnailSlot")).toInt(-1);
            family.paintSelectable = item.value(QStringLiteral("paintSelectable")).toBool(true);
            profile.families.append(family);
        }
        for(int slot = 0; slot < atlasSlotCount; ++slot)
        {
            STerrainAtlasProfileSlot definition;
            definition.slot = slot;
            definition.signature.northWest = quint8(signatures.at(slot * 4));
            definition.signature.northEast = quint8(signatures.at(slot * 4 + 1));
            definition.signature.southEast = quint8(signatures.at(slot * 4 + 2));
            definition.signature.southWest = quint8(signatures.at(slot * 4 + 3));
            definition.assigned = true;
            definition.ambiguous = false;
            profile.slotDefinitions.append(definition);
        }
        if(!profile.normalizeMembership(report))
            return {};
        return profile;
    }
    return {};
}

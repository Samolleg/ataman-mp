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

QString CTerrainAtlasPresetCatalog::materialTheme(const QString& variantName)
{
    const QString trimmed = variantName.trimmed();
    const int separator = trimmed.indexOf(QLatin1Char('/'));
    return (separator < 0 ? trimmed : trimmed.left(separator)).trimmed();
}

qint32 CTerrainAtlasPresetCatalog::globalMaterialId(
    const QString& variantName, ETerrainAtlasLayer layer)
{
    const QByteArray key = (layer == ETerrainAtlasLayer::Land
        ? QByteArrayLiteral("land:") : QByteArrayLiteral("overlay:"))
        + variantName.trimmed().toCaseFolded().toUtf8();
    quint32 hash = 2166136261u;
    for(char byte : key)
    {
        hash ^= quint8(byte);
        hash *= 16777619u;
    }
    // Keep global family IDs positive and far away from legacy atlas-local
    // one-byte IDs. Zero remains the invalid/unavailable sentinel.
    return qint32(0x40000000u | (hash & 0x3fffffffu));
}

QVector<STerrainGlobalMaterialFamily>
CTerrainAtlasPresetCatalog::globalUniformFamilies(CValidationReport* report)
{
    QMap<QString, STerrainGlobalMaterialFamily> grouped;
    QMap<qint32, QString> identityKeys;
    for(const QJsonValue& value : presetArray(report))
    {
        const QJsonObject preset = value.toObject();
        const QString mprNamespace = preset.value(QStringLiteral("namespace")).toString();
        const int slotCount = preset.value(QStringLiteral("slotCount")).toInt();
        const QByteArray signatures = QByteArray::fromBase64(
            preset.value(QStringLiteral("signaturesBase64")).toString().toLatin1());
        if(mprNamespace.isEmpty() || slotCount <= 0
           || signatures.size() != slotCount * 4)
            continue;
        for(const QJsonValue& familyValue : preset.value(
                QStringLiteral("families")).toArray())
        {
            const QJsonObject familyObject = familyValue.toObject();
            const int localId = familyObject.value(QStringLiteral("id")).toInt(-1);
            const QString variantName = familyObject.value(
                QStringLiteral("name")).toString();
            int uniformSlot = -1;
            for(int slot = 0; slot < slotCount; ++slot)
            {
                const int offset = slot * 4;
                if(quint8(signatures.at(offset)) == localId
                   && quint8(signatures.at(offset + 1)) == localId
                   && quint8(signatures.at(offset + 2)) == localId
                   && quint8(signatures.at(offset + 3)) == localId)
                {
                    uniformSlot = slot;
                    break;
                }
            }
            if(uniformSlot < 0)
                continue; // transition evidence is not a paint family
            const QString theme = materialTheme(variantName);
            if(theme.isEmpty() || variantName.trimmed().isEmpty())
                continue;
            const QJsonArray layers = familyObject.value(
                QStringLiteral("allowedLayers")).toArray();
            for(const QJsonValue& layerValue : layers)
            {
                const ETerrainAtlasLayer layer =
                    layerValue.toString() == QStringLiteral("overlay")
                    ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
                const QString key = QString(layer == ETerrainAtlasLayer::Land
                    ? QStringLiteral("L:%1") : QStringLiteral("O:%1"))
                    .arg(variantName.trimmed().toCaseFolded());
                STerrainGlobalMaterialFamily& global = grouped[key];
                if(global.name.isEmpty())
                {
                    global.name = variantName.trimmed();
                    global.theme = theme;
                    global.layer = layer;
                    global.id = globalMaterialId(global.name, layer);
                    const QString previous = identityKeys.value(global.id);
                    if(!previous.isEmpty() && previous != key)
                    {
                        if(report) report->add("terrain.global_family_hash_collision",
                            EValidationSeverity::Error,
                            "Two global terrain material families have the same deterministic identity",
                            previous + QStringLiteral(" / ") + key);
                        continue;
                    }
                    identityKeys.insert(global.id, key);
                }
                bool duplicate = false;
                for(const STerrainGlobalMaterialSource& source : global.sources)
                    duplicate |= source.mprNamespace.compare(
                        mprNamespace, Qt::CaseInsensitive) == 0
                        && source.uniformSlot == uniformSlot;
                if(duplicate)
                    continue;
                STerrainGlobalMaterialSource source;
                source.mprNamespace = mprNamespace;
                source.textureCount = (slotCount + 63) / 64;
                source.uniformSlot = uniformSlot;
                source.variantName = variantName;
                global.sources.append(source);
            }
        }
    }
    QVector<STerrainGlobalMaterialFamily> result = grouped.values().toVector();
    std::sort(result.begin(), result.end(), [](
        const STerrainGlobalMaterialFamily& left,
        const STerrainGlobalMaterialFamily& right) {
        if(left.layer != right.layer)
            return left.layer == ETerrainAtlasLayer::Land;
        const int themeOrder = left.theme.localeAwareCompare(right.theme);
        if(themeOrder != 0) return themeOrder < 0;
        return left.name.localeAwareCompare(right.name) < 0;
    });
    return result;
}

CTerrainTransitionCatalog CTerrainAtlasPresetCatalog::globalizedCatalog(
    const STerrainAtlasProfile& profile,
    const CTerrainTransitionCatalog& source)
{
    QMap<qint32, qint32> globalByLocal;
    for(const STerrainAtlasProfileFamily& family : profile.families)
        globalByLocal.insert(family.id,
            globalMaterialId(family.name, family.layer));
    CTerrainTransitionCatalog result;
    result.setMirrorPaintGridY(source.paintGridMirrored());
    for(int slot = 0; slot < profile.atlasSlotCount; ++slot)
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.tileIndex = quint8(slot % 64);
            packed.atlasIndex = quint8(slot / 64);
            packed.rotation = quint8(rotation);
            bool found = false;
            STerrainTransitionSignature signature = source.signatureForPacked(
                packed.encode(), &found);
            if(!found) continue;
            qint32* values[4] = {&signature.northWest, &signature.northEast,
                                &signature.southEast, &signature.southWest};
            bool mapped = true;
            for(qint32* value : values)
            {
                const auto replacement = globalByLocal.constFind(*value);
                if(replacement == globalByLocal.constEnd())
                {
                    mapped = false;
                    break;
                }
                *value = replacement.value();
            }
            if(!mapped) continue;
            STerrainTransitionVariant variant;
            variant.signature = signature;
            variant.packedTile = packed.encode();
            result.addVariant(variant);
        }
    return result;
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

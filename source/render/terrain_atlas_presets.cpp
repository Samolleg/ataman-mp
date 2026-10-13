#include "terrain_atlas_profile.h"
#include "terrain_hd_provenance.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "endurance_document.h"
#include "mmp_codec.h"
#include "resource_name.h"

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

QByteArray presetAtlasIdentity(const QJsonObject& preset)
{
    // Bindings are alternate pixel revisions of one reviewed semantic atlas.
    // Identify the semantic atlas itself so aliases group together, while two
    // unrelated atlases that happen to share a page hash can never be merged.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(preset.value(QStringLiteral("signaturesBase64"))
                     .toString().toLatin1());
    hash.addData(QJsonDocument(preset.value(QStringLiteral("families"))
                                   .toArray()).toJson(QJsonDocument::Compact));
    return hash.result().toHex();
}

QJsonObject matchingPreset(const STerrainAtlasProfile& profile)
{
    for(const QJsonValue& value : presetArray(nullptr))
    {
        const QJsonObject preset = value.toObject();
        if(preset.value(QStringLiteral("namespace")).toString().compare(
               profile.mprNamespace, Qt::CaseInsensitive) != 0)
            continue;
        for(const QJsonValue& bindingValue : preset.value(
                QStringLiteral("bindings")).toArray())
            if(bindingValue.toObject().value(QStringLiteral("pixelSha256"))
                   .toString().toLatin1().toLower()
               == profile.atlasPixelSha256.toLower())
                return preset;
    }
    return {};
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

QString CTerrainAtlasPresetCatalog::reviewedMaterialName(const QString& name,int reviewedClass)
{
    for(const auto& rule:terrainConfirmedHdProvenance())
        if(name==rule.material&&reviewedClass==rule.reviewedClass)
            return name+QStringLiteral(" [class %1]").arg(reviewedClass);
    return name;
}

bool CTerrainAtlasPresetCatalog::validateConfirmedSource(const STerrainGlobalMaterialSource& source,
    const QImage& nativeHd,const std::function<bool(const QString&,QByteArray*)>& loader)
{
    if(source.confirmedSdPixelSha256.isEmpty())return true;
    if(nativeHd.size()!=QSize(256,256)||terrainProvenancePixelSha(nativeHd)!=source.confirmedHdPixelSha256||!loader)return false;
    QByteArray bytes;QImage sd,currentHd;
    const QString hdPage=QStringLiteral("%1%2.mmp").arg(source.mprNamespace.toLower()).arg(source.uniformSlot/64,3,10,QLatin1Char('0'));
    return source.uniformSlot>=0 && loader(hdPage,&bytes)
        &&ei::decodeMmpDxt1AtlasTile(bytes,source.uniformSlot%64,&currentHd)
        &&terrainProvenancePixelSha(currentHd)==source.confirmedHdPixelSha256
        &&loader(source.confirmedSdPage,&bytes)
        &&ei::decodeMmpDxt1AtlasTile(bytes,source.confirmedSdSlot,&sd)
        &&sd.size()==QSize(128,128)&&terrainProvenancePixelSha(sd)==source.confirmedSdPixelSha256;
}

qint32 CTerrainAtlasPresetCatalog::globalMaterialId(
    const QString& variantName, ETerrainAtlasLayer layer,
    const QByteArray& atlasIdentity, qint32 localFamilyId)
{
    Q_UNUSED(atlasIdentity);
    Q_UNUSED(localFamilyId);
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
        const QByteArray atlasIdentity = presetAtlasIdentity(preset);
        const QString mprNamespace = preset.value(QStringLiteral("namespace")).toString();
        const int slotCount = preset.value(QStringLiteral("slotCount")).toInt();
        QVector<QByteArray> approvedPixelBindings;
        for(const QJsonValue& bindingValue : preset.value(
                QStringLiteral("bindings")).toArray())
        {
            const QJsonObject binding = bindingValue.toObject();
            // Global Modern painting is one reviewed visual revision. Mixing
            // an original atlas anchor with an HD current map can create a
            // technically exact semantic transition whose artwork is visibly
            // unrelated. Classic/Manual remain map-local; cross-atlas Modern
            // sources are restricted to the reviewed HD binding.
            if(binding.value(QStringLiteral("variant")).toString()
                   != QStringLiteral("hd"))
                continue;
            const QByteArray pixelSha256 = binding.value(
                QStringLiteral("pixelSha256")).toString().toLatin1().toLower();
            if(pixelSha256.size() == 64
               && !approvedPixelBindings.contains(pixelSha256))
                approvedPixelBindings.append(pixelSha256);
        }
        const QByteArray signatures = QByteArray::fromBase64(
            preset.value(QStringLiteral("signaturesBase64")).toString().toLatin1());
        if(mprNamespace.isEmpty() || slotCount <= 0
           || signatures.size() != slotCount * 4
           || approvedPixelBindings.isEmpty())
            continue;
        for(const QJsonValue& familyValue : preset.value(
                QStringLiteral("families")).toArray())
        {
            const QJsonObject familyObject = familyValue.toObject();
            const int localId = familyObject.value(QStringLiteral("id")).toInt(-1);
            const QString variantName = reviewedMaterialName(familyObject.value(
                QStringLiteral("name")).toString(),familyObject.value(QStringLiteral("tileType")).toInt(-1));
            if(!familyObject.value(QStringLiteral("paintSelectable")).toBool(true))
                continue;
            const int thumbnailSlot = familyObject.value(
                QStringLiteral("thumbnailSlot")).toInt(-1);
            const qint32 runtimeTerrainType = familyObject.value(
                QStringLiteral("tileType")).toInt(-1);
            QVector<int> uniformSlots;
            for(int slot = 0; slot < slotCount; ++slot)
            {
                const int offset = slot * 4;
                if(quint8(signatures.at(offset)) == localId
                   && quint8(signatures.at(offset + 1)) == localId
                   && quint8(signatures.at(offset + 2)) == localId
                   && quint8(signatures.at(offset + 3)) == localId)
                    uniformSlots.append(slot);
            }
            // The audited semantic corner table is paint authority. A family
            // with no genuine four-corner-uniform tile remains contextual;
            // thumbnailSlot is display metadata and never grants authority.
            if(uniformSlots.isEmpty())
                continue;
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
                    global.id = globalMaterialId(global.name, layer,
                                                 atlasIdentity, localId);
                    global.runtimeTerrainType = runtimeTerrainType;
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
                else if(global.runtimeTerrainType != runtimeTerrainType)
                {
                    if(report) report->add(
                        "terrain.global_family_runtime_type_conflict",
                        EValidationSeverity::Warning,
                        "Excluded a visual variation whose runtime terrain type disagrees with its paint material",
                        QStringLiteral("%1: kept %2, excluded %3 from %4 slot %5")
                            .arg(global.name)
                            .arg(global.runtimeTerrainType)
                            .arg(runtimeTerrainType)
                            .arg(mprNamespace)
                            .arg(uniformSlots.front()));
                    // A brush has one reviewed gameplay identity. Retaining a
                    // visually similar source with a different MP class would
                    // make traversal/ambience vary randomly between strokes.
                    continue;
                }
                const auto appendSource = [&](int slot,
                                              QVector<STerrainGlobalMaterialSource>* target) {
                    for(const QByteArray& pixelSha256 : approvedPixelBindings)
                    {
                        bool duplicate = false;
                        for(const STerrainGlobalMaterialSource& existing : *target)
                            duplicate |= existing.mprNamespace.compare(
                                   mprNamespace, Qt::CaseInsensitive) == 0
                                && existing.uniformSlot == slot
                                && existing.atlasPixelSha256 == pixelSha256;
                        if(duplicate)
                            continue;
                        STerrainGlobalMaterialSource source;
                        source.mprNamespace = mprNamespace;
                        source.atlasIdentity = atlasIdentity;
                        source.atlasPixelSha256 = pixelSha256;
                        source.textureCount = (slotCount + 63) / 64;
                        source.atlasSlotCount = slotCount;
                        source.uniformSlot = slot;
                        source.variantName = variantName;
                        target->append(source);
                    }
                };
                for(int uniformSlot : uniformSlots)
                    appendSource(uniformSlot, &global.sources);
                if(thumbnailSlot >= 0 && thumbnailSlot < slotCount)
                {
                    const int offset = thumbnailSlot * 4;
                    bool uniformFamily = true;
                    for(int corner = 0; corner < 4; ++corner)
                        uniformFamily &= quint8(signatures.at(offset + corner))
                            == localId;
                    // The catalog thumbnail is useful review metadata even
                    // for a contextual one-on-three family, but the paint
                    // palette icon must never show that mixed transition as
                    // though it were a uniform brush. If the preferred slot
                    // is mixed, loadGlobalUniformSource falls back to one of
                    // the audited four-corner sources collected above.
                    if(uniformFamily)
                        appendSource(thumbnailSlot, &global.thumbnailSources);
                }
            }
        }
    }
    for(const auto& rule:terrainConfirmedHdProvenance()) {
        STerrainGlobalMaterialSource hd;bool found=false;
        for(const auto& family:grouped)for(const auto& candidate:family.sources)
            if(candidate.mprNamespace==rule.hdNamespace&&candidate.uniformSlot==rule.hdSlot){hd=candidate;found=true;break;}
        if(!found)continue;
        for(auto& family:grouped) {
            if(family.name!=reviewedMaterialName(rule.material,rule.reviewedClass)
               ||family.runtimeTerrainType!=rule.reviewedClass||family.layer!=rule.layer)continue;
            QVector<STerrainGlobalMaterialSource> aliases;
            for(const auto& sd:family.sources) {
                auto alias=hd;alias.variantName=family.name;
                alias.confirmedSdPage=QStringLiteral("%1%2.mmp").arg(sd.mprNamespace.toLower()).arg(sd.uniformSlot/64,3,10,QLatin1Char('0'));
                alias.confirmedSdSlot=sd.uniformSlot%64;
                alias.confirmedSdPixelSha256=rule.sdPixelSha256;alias.confirmedHdPixelSha256=rule.hdPixelSha256;
                aliases.append(alias);
            }
            family.sources=aliases;family.thumbnailSources=aliases;
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

QImage CTerrainAtlasPresetCatalog::loadGlobalUniformSource(
    const STerrainGlobalMaterialFamily& family,
    const QString& preferredNamespace,
    const QSize& requiredSize,
    QMap<QString, QByteArray>* encodedPageCache,
    QSet<QString>* missingPageCache,
    QMap<QString, QImage>* decodedTileCache,
    const std::function<bool(const QString&, QByteArray*)>& pageLoader,
    bool thumbnail,
    STerrainGlobalMaterialSource* selectedSource,
    const std::function<bool(const QString&, int, QImage*, QString*)>&
        tileLoader)
{
    if(!encodedPageCache || !missingPageCache || !decodedTileCache
       || (!pageLoader && !tileLoader))
        return QImage();

    QVector<STerrainGlobalMaterialSource> sources;
    const auto appendUnique = [&sources](
        const STerrainGlobalMaterialSource& source) {
        for(const STerrainGlobalMaterialSource& existing : sources)
            if(existing.mprNamespace.compare(
                   source.mprNamespace, Qt::CaseInsensitive) == 0
               && existing.uniformSlot == source.uniformSlot
               && existing.confirmedSdPage == source.confirmedSdPage
               && existing.confirmedSdSlot == source.confirmedSdSlot)
                return;
        sources.append(source);
    };
    const auto appendMatching = [&](
        const QVector<STerrainGlobalMaterialSource>& candidates,
        bool preferred) {
        for(const STerrainGlobalMaterialSource& source : candidates)
            if((source.mprNamespace.compare(
                    preferredNamespace, Qt::CaseInsensitive) == 0)
               == preferred)
                appendUnique(source);
    };
    Q_UNUSED(thumbnail);
    // A generated blend needs one stable full-tile base even though uniform
    // interiors may later distribute across every exact authored variant.
    // Use the reviewed, structurally uniform thumbnail source as that base
    // for both the icon and compositor. Previously the icon used this source
    // while painting silently took the first same-family slot, so a corrected
    // brush preview could still paint with a visibly mixed legacy variant.
    // Local artwork remains preferred over a foreign thumbnail.
    appendMatching(family.thumbnailSources, true);
    appendMatching(family.sources, true);
    appendMatching(family.thumbnailSources, false);
    appendMatching(family.sources, false);

    QImage fallback;
    STerrainGlobalMaterialSource fallbackSource;
    for(const STerrainGlobalMaterialSource& source : sources)
    {
        const QStringList names = ei::terrainTextureResourceNames(
            source.mprNamespace, source.textureCount);
        const int pageIndex = source.uniformSlot / 64;
        if(pageIndex < 0 || pageIndex >= names.size())
            continue;
        const QString pageName = names.at(pageIndex).toCaseFolded();
        if(tileLoader)
        {
            QImage tile;
            QString sourceIdentity;
            if(!tileLoader(pageName, source.uniformSlot % 64,
                           &tile, &sourceIdentity)
               || tile.isNull())
                continue;
            tile = tile.convertToFormat(QImage::Format_RGBA8888);
            if(!validateConfirmedSource(source,tile,pageLoader))continue;
            if(!source.confirmedSdPixelSha256.isEmpty() || requiredSize.isEmpty() || tile.size() == requiredSize)
            {
                if(selectedSource) *selectedSource = source;
                return tile;
            }
            if(fallback.isNull())
            {
                fallback = tile;
                fallbackSource = source;
            }
            continue;
        }
        if(missingPageCache->contains(pageName))
            continue;
        auto encoded = encodedPageCache->find(pageName);
        if(encoded == encodedPageCache->end())
        {
            QByteArray payload;
            if(!pageLoader(pageName, &payload) || payload.isEmpty())
            {
                missingPageCache->insert(pageName);
                continue;
            }
            encoded = encodedPageCache->insert(pageName, payload);
        }
        const QByteArray payloadSha256 = QCryptographicHash::hash(
            encoded.value(), QCryptographicHash::Sha256).toHex();
        const QString tileCacheKey = QStringLiteral("%1|%2|%3|%4x%5")
            .arg(pageName, QString::fromLatin1(payloadSha256))
            .arg(source.uniformSlot % 64)
            .arg(requiredSize.width()).arg(requiredSize.height());
        if(source.confirmedSdPixelSha256.isEmpty() && decodedTileCache->contains(tileCacheKey))
        {
            if(selectedSource) *selectedSource = source;
            return decodedTileCache->value(tileCacheKey);
        }
        QImage tile;
        QString diagnostic;
        if(!ei::decodeMmpDxt1AtlasTile(
               encoded.value(), source.uniformSlot % 64,
               &tile, &diagnostic))
            continue;
        tile = tile.convertToFormat(QImage::Format_RGBA8888);
        if(!validateConfirmedSource(source,tile,pageLoader))continue;
        if(!source.confirmedSdPixelSha256.isEmpty() || requiredSize.isEmpty() || tile.size() == requiredSize)
        {
            decodedTileCache->insert(tileCacheKey, tile);
            if(selectedSource) *selectedSource = source;
            return tile;
        }
        if(fallback.isNull())
        {
            fallback = tile;
            fallbackSource = source;
        }
    }
    if(fallback.isNull())
        return QImage();
    if(selectedSource) *selectedSource = fallbackSource;
    return fallback.scaled(requiredSize, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
}

QImage CTerrainAtlasPresetCatalog::loadGlobalTransitionSource(
    const STerrainGlobalTransitionSource& source,
    const QSize& requiredSize,
    QMap<QString, QByteArray>* encodedPageCache,
    QSet<QString>* missingPageCache,
    QMap<QString, QImage>* decodedTileCache,
    const std::function<bool(const QString&, QByteArray*)>& pageLoader)
{
    if(!encodedPageCache || !missingPageCache || !decodedTileCache
       || !pageLoader)
        return QImage();
    const QStringList names = ei::terrainTextureResourceNames(
        source.mprNamespace, source.textureCount);
    const int pageIndex = source.slot / 64;
    if(pageIndex < 0 || pageIndex >= names.size())
        return QImage();
    const QString pageName = names.at(pageIndex).toCaseFolded();
    QByteArray payload;
    // Refresh the resource lookup before consulting decoded pixels. A
    // persistent page-name cache can otherwise keep bytes from a previously
    // opened map/resource profile and import unrelated artwork into a stroke.
    if(!pageLoader(pageName, &payload) || payload.isEmpty())
    {
        missingPageCache->insert(pageName);
        return QImage();
    }
    missingPageCache->remove(pageName);
    const QByteArray payloadSha256 = QCryptographicHash::hash(
        payload, QCryptographicHash::Sha256).toHex();
    const QString payloadKey = pageName + QLatin1Char('|')
        + QString::fromLatin1(payloadSha256);
    encodedPageCache->insert(payloadKey, payload);
    const int localSlot = source.slot % 64;
    const QString tileCacheKey = QStringLiteral("%1|%2|%3|%4x%5")
        .arg(pageName, QString::fromLatin1(payloadSha256))
        .arg(localSlot)
        .arg(requiredSize.width()).arg(requiredSize.height());
    const auto cached = decodedTileCache->constFind(tileCacheKey);
    if(cached != decodedTileCache->constEnd())
        return cached.value();
    QImage tile;
    QString diagnostic;
    if(!ei::decodeMmpDxt1AtlasTile(payload, localSlot, &tile, &diagnostic))
        return QImage();
    tile = tile.convertToFormat(QImage::Format_RGBA8888);
    if(!requiredSize.isEmpty() && tile.size() != requiredSize)
        tile = tile.scaled(requiredSize, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
    decodedTileCache->insert(tileCacheKey, tile);
    return tile;
}

QVector<STerrainGlobalTransitionSource>
CTerrainAtlasPresetCatalog::globalAuthoredTransitions(CValidationReport* report)
{
    QVector<STerrainGlobalTransitionSource> result;
    for(const QJsonValue& value : presetArray(report))
    {
        const QJsonObject preset = value.toObject();
        const QByteArray atlasIdentity = presetAtlasIdentity(preset);
        const QString mprNamespace = preset.value(QStringLiteral("namespace")).toString();
        const int slotCount = preset.value(QStringLiteral("slotCount")).toInt();
        QVector<QByteArray> approvedPixelBindings;
        for(const QJsonValue& bindingValue : preset.value(
                QStringLiteral("bindings")).toArray())
        {
            const QJsonObject binding = bindingValue.toObject();
            if(binding.value(QStringLiteral("variant")).toString()
                   != QStringLiteral("hd"))
                continue;
            const QByteArray pixelSha256 = binding.value(
                QStringLiteral("pixelSha256")).toString().toLatin1().toLower();
            if(pixelSha256.size() == 64
               && !approvedPixelBindings.contains(pixelSha256))
                approvedPixelBindings.append(pixelSha256);
        }
        const QByteArray signatures = QByteArray::fromBase64(
            preset.value(QStringLiteral("signaturesBase64")).toString().toLatin1());
        if(mprNamespace.isEmpty() || slotCount <= 0
           || signatures.size() != slotCount * 4
           || approvedPixelBindings.isEmpty())
            continue;
        QMap<QPair<int, int>, qint32> globalByLocalAndLayer;
        for(const QJsonValue& familyValue : preset.value(
                QStringLiteral("families")).toArray())
        {
            const QJsonObject family = familyValue.toObject();
            const int localId = family.value(QStringLiteral("id")).toInt(-1);
            const QString name = family.value(QStringLiteral("name")).toString().trimmed();
            if(localId < 0 || name.isEmpty())
                continue;
            for(const QJsonValue& layerValue : family.value(
                    QStringLiteral("allowedLayers")).toArray())
            {
                const ETerrainAtlasLayer layer = layerValue.toString()
                    == QStringLiteral("overlay")
                    ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
                globalByLocalAndLayer.insert(qMakePair(localId, int(layer)),
                    globalMaterialId(name, layer, atlasIdentity, localId));
            }
        }
        for(int slot = 0; slot < slotCount; ++slot)
            for(ETerrainAtlasLayer layer : {ETerrainAtlasLayer::Land,
                                            ETerrainAtlasLayer::Overlay})
            {
                qint32 corners[4] = {};
                bool mapped = true;
                for(int corner = 0; corner < 4; ++corner)
                {
                    const int localId = quint8(signatures.at(slot * 4 + corner));
                    const auto found = globalByLocalAndLayer.constFind(
                        qMakePair(localId, int(layer)));
                    if(found == globalByLocalAndLayer.constEnd())
                    {
                        mapped = false;
                        break;
                    }
                    corners[corner] = found.value();
                }
                if(!mapped)
                    continue;
                STerrainTransitionSignature previewSignature;
                previewSignature.northWest = corners[0];
                previewSignature.northEast = corners[1];
                previewSignature.southEast = corners[2];
                previewSignature.southWest = corners[3];
                for(const QByteArray& pixelSha256 : approvedPixelBindings)
                {
                    STerrainGlobalTransitionSource source;
                    source.signature = CTerrainTransitionCatalog::mirroredGridY(
                        previewSignature);
                    source.layer = layer;
                    source.mprNamespace = mprNamespace;
                    source.atlasIdentity = atlasIdentity;
                    source.atlasPixelSha256 = pixelSha256;
                    source.textureCount = (slotCount + 63) / 64;
                    source.atlasSlotCount = slotCount;
                    source.slot = slot;
                    result.append(source);
                }
                break;
            }
    }
    return result;
}

CTerrainTransitionCatalog CTerrainAtlasPresetCatalog::globalizedCatalog(
    const STerrainAtlasProfile& profile,
    const CTerrainTransitionCatalog& source,
    const QVector<STerrainGlobalMaterialFamily>* globalFamilies)
{
    const QJsonObject preset = matchingPreset(profile);
    const QByteArray atlasIdentity = presetAtlasIdentity(preset);
    QMap<qint32, qint32> globalByLocal;
    QVector<STerrainGlobalMaterialFamily> resolvedGlobalFamilies;
    for(const STerrainAtlasProfileFamily& family : profile.families)
    {
        STerrainTransitionSignature pure;
        pure.northWest = pure.northEast = pure.southEast = pure.southWest = family.id;
        bool hasUniform = false;
        source.selectForPaint(pure, 0, 0, 0, &hasUniform);
        qint32 globalId = globalMaterialId(family.name, family.layer,
                                          atlasIdentity, family.id);
        if(!hasUniform)
        {
            // A transition-only local identity may borrow only one explicit
            // full-tile variant with the same reviewed name. Never merge all
            // same-named uniform sources into one brush identity.
            if(!globalFamilies && resolvedGlobalFamilies.isEmpty())
                resolvedGlobalFamilies = globalUniformFamilies(nullptr);
            const QVector<STerrainGlobalMaterialFamily>& candidates =
                globalFamilies ? *globalFamilies : resolvedGlobalFamilies;
            QByteArray bestIdentity;
            for(const STerrainGlobalMaterialFamily& candidate : candidates)
                if(candidate.layer == family.layer
                   && !candidate.sources.isEmpty()
                   && candidate.sources.front().variantName.compare(
                       family.name, Qt::CaseInsensitive) == 0
                   && (bestIdentity.isEmpty()
                       || candidate.sources.front().atlasIdentity < bestIdentity))
                {
                    bestIdentity = candidate.sources.front().atlasIdentity;
                    globalId = candidate.id;
                }
        }
        globalByLocal.insert(family.id, globalId);
    }
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
            const STerrainTransitionVariant sourceVariant =
                source.variantForPacked(packed.encode(), &found);
            if(!found) continue;
            STerrainTransitionSignature signature = sourceVariant.signature;
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
            STerrainTransitionVariant variant = sourceVariant;
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
        profile.schemaVersion = 10;
        profile.mprNamespace = requestedNamespace;
        profile.atlasPixelSha256 = requestedHash;
        profile.atlasSlotCount = atlasSlotCount;
        profile.textureCount = (atlasSlotCount + 63) / 64;
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

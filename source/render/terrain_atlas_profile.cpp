#include "terrain_atlas_profile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QRegularExpression>
#include <algorithm>

#include "endurance_document.h"

namespace {
QString layerName(ETerrainAtlasLayer layer)
{
    return layer == ETerrainAtlasLayer::Land ? QStringLiteral("land")
                                              : QStringLiteral("overlay");
}

bool parseLayer(const QString& text, ETerrainAtlasLayer* layer)
{
    if(text == QStringLiteral("land"))
        *layer = ETerrainAtlasLayer::Land;
    else if(text == QStringLiteral("overlay"))
        *layer = ETerrainAtlasLayer::Overlay;
    else
        return false;
    return true;
}

QJsonObject signatureJson(const STerrainTransitionSignature& signature)
{
    QJsonObject result;
    result.insert(QStringLiteral("northWest"), signature.northWest);
    result.insert(QStringLiteral("northEast"), signature.northEast);
    result.insert(QStringLiteral("southEast"), signature.southEast);
    result.insert(QStringLiteral("southWest"), signature.southWest);
    return result;
}

bool parseSignature(const QJsonObject& object, STerrainTransitionSignature* signature)
{
    const QStringList keys = {QStringLiteral("northWest"), QStringLiteral("northEast"),
                              QStringLiteral("southEast"), QStringLiteral("southWest")};
    for(const QString& key : keys)
        if(!object.value(key).isDouble())
            return false;
    signature->northWest = object.value(keys.at(0)).toInt();
    signature->northEast = object.value(keys.at(1)).toInt();
    signature->southEast = object.value(keys.at(2)).toInt();
    signature->southWest = object.value(keys.at(3)).toInt();
    return true;
}

struct SCornerColor
{
    SCornerColor(int red = 0, int green = 0, int blue = 0)
        : r(red), g(green), b(blue) {}
    int r;
    int g;
    int b;
};

SCornerColor cornerColor(const QImage& source, int corner)
{
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    const int sample = qMax(1, qMin(image.width(), image.height()) / 6);
    const int x0 = (corner == 1 || corner == 2) ? image.width() - sample : 0;
    const int y0 = (corner >= 2) ? image.height() - sample : 0;
    qint64 red = 0, green = 0, blue = 0, weight = 0;
    for(int y = y0; y < y0 + sample; ++y)
        for(int x = x0; x < x0 + sample; ++x)
        {
            const QRgb pixel = image.pixel(x, y);
            const int alpha = qAlpha(pixel);
            red += qRed(pixel) * alpha;
            green += qGreen(pixel) * alpha;
            blue += qBlue(pixel) * alpha;
            weight += alpha;
        }
    if(weight == 0)
        return {};
    return {int(red / weight), int(green / weight), int(blue / weight)};
}

int colorDistance(const SCornerColor& lhs, const SCornerColor& rhs)
{
    const int dr = lhs.r - rhs.r, dg = lhs.g - rhs.g, db = lhs.b - rhs.b;
    return dr * dr + dg * dg + db * db;
}

bool uniform(const STerrainTransitionSignature& signature)
{
    return signature.northWest == signature.northEast
        && signature.northWest == signature.southEast
        && signature.northWest == signature.southWest;
}
}

QJsonObject STerrainAtlasProfile::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);
    root.insert(QStringLiteral("mprNamespace"), mprNamespace);
    root.insert(QStringLiteral("atlasPixelSha256"), QString::fromLatin1(atlasPixelSha256));
    root.insert(QStringLiteral("atlasSlotCount"), atlasSlotCount);
    root.insert(QStringLiteral("provenance"), provenance);
    root.insert(QStringLiteral("approved"), approved);
    QJsonArray familyArray;
    for(const STerrainAtlasProfileFamily& family : families)
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), family.id);
        object.insert(QStringLiteral("name"), family.name);
        object.insert(QStringLiteral("layer"), layerName(family.layer));
        object.insert(QStringLiteral("underwater"), family.underwater);
        object.insert(QStringLiteral("ambiguous"), family.ambiguous);
        object.insert(QStringLiteral("tileType"), family.tileType);
        QJsonArray slotsArray;
        for(int slot : family.memberSlots) slotsArray.append(slot);
        object.insert(QStringLiteral("slots"), slotsArray);
        familyArray.append(object);
    }
    root.insert(QStringLiteral("families"), familyArray);
    QJsonArray slotArray;
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        QJsonObject object;
        object.insert(QStringLiteral("slot"), slot.slot);
        object.insert(QStringLiteral("signature"), signatureJson(slot.signature));
        object.insert(QStringLiteral("ambiguous"), slot.ambiguous);
        slotArray.append(object);
    }
    root.insert(QStringLiteral("slots"), slotArray);
    return root;
}

bool STerrainAtlasProfile::fromJson(const QJsonObject& object,
                                    STerrainAtlasProfile* profile,
                                    CValidationReport* report)
{
    if(!profile || object.value(QStringLiteral("schemaVersion")).toInt() != 1
       || !object.value(QStringLiteral("families")).isArray()
       || !object.value(QStringLiteral("slots")).isArray())
    {
        if(report) report->add("terrain.atlas_profile_schema", EValidationSeverity::Error,
                               "The semantic atlas profile schema is missing or unsupported");
        return false;
    }
    STerrainAtlasProfile parsed;
    parsed.mprNamespace = object.value(QStringLiteral("mprNamespace")).toString();
    parsed.atlasPixelSha256 = object.value(QStringLiteral("atlasPixelSha256")).toString().toLatin1();
    parsed.atlasSlotCount = object.value(QStringLiteral("atlasSlotCount")).toInt(-1);
    parsed.provenance = object.value(QStringLiteral("provenance")).toString();
    parsed.approved = object.value(QStringLiteral("approved")).toBool(false);
    if(parsed.mprNamespace.trimmed().isEmpty() || parsed.atlasPixelSha256.size() != 64
       || parsed.atlasSlotCount < 1)
    {
        if(report) report->add("terrain.atlas_profile_binding", EValidationSeverity::Error,
                               "The semantic atlas profile has an incomplete namespace/hash binding");
        return false;
    }
    QSet<qint32> familyIds;
    for(const QJsonValue& value : object.value(QStringLiteral("families")).toArray())
    {
        const QJsonObject item = value.toObject();
        STerrainAtlasProfileFamily family;
        family.id = item.value(QStringLiteral("id")).toInt();
        family.name = item.value(QStringLiteral("name")).toString();
        if(family.id <= 0 || family.name.trimmed().isEmpty()
           || familyIds.contains(family.id)
           || !parseLayer(item.value(QStringLiteral("layer")).toString(), &family.layer))
        {
            if(report) report->add("terrain.atlas_profile_family", EValidationSeverity::Error,
                                   "The semantic atlas profile contains an invalid material family");
            return false;
        }
        familyIds.insert(family.id);
        family.underwater = item.value(QStringLiteral("underwater")).toBool();
        family.ambiguous = item.value(QStringLiteral("ambiguous")).toBool();
        family.tileType = item.value(QStringLiteral("tileType")).toInt(-1);
        for(const QJsonValue& slot : item.value(QStringLiteral("slots")).toArray())
            family.memberSlots.append(slot.toInt(-1));
        parsed.families.append(family);
    }
    QSet<int> slotIds;
    for(const QJsonValue& value : object.value(QStringLiteral("slots")).toArray())
    {
        const QJsonObject item = value.toObject();
        STerrainAtlasProfileSlot slot;
        slot.slot = item.value(QStringLiteral("slot")).toInt(-1);
        slot.ambiguous = item.value(QStringLiteral("ambiguous")).toBool();
        if(slot.slot < 0 || slot.slot >= parsed.atlasSlotCount || slotIds.contains(slot.slot)
           || !parseSignature(item.value(QStringLiteral("signature")).toObject(), &slot.signature)
           || !familyIds.contains(slot.signature.northWest)
           || !familyIds.contains(slot.signature.northEast)
           || !familyIds.contains(slot.signature.southEast)
           || !familyIds.contains(slot.signature.southWest))
        {
            if(report) report->add("terrain.atlas_profile_slot", EValidationSeverity::Error,
                                   "The semantic atlas profile contains an invalid slot signature");
            return false;
        }
        slotIds.insert(slot.slot);
        parsed.slotDefinitions.append(slot);
    }
    if(parsed.slotDefinitions.size() != parsed.atlasSlotCount)
    {
        if(report) report->add("terrain.atlas_profile_coverage", EValidationSeverity::Error,
                               "The semantic atlas profile does not describe every atlas slot");
        return false;
    }
    if(!parsed.normalizeMembership(report))
        return false;
    *profile = parsed;
    return true;
}

bool STerrainAtlasProfile::save(const QString& path, CValidationReport* report) const
{
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)
       || file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)) < 0
       || !file.commit())
    {
        if(report) report->add("terrain.atlas_profile_write", EValidationSeverity::Error,
                               "Could not atomically save the semantic atlas profile", path);
        return false;
    }
    return true;
}

bool STerrainAtlasProfile::load(const QString& path, STerrainAtlasProfile* profile,
                                CValidationReport* report)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(report) report->add("terrain.atlas_profile_read", EValidationSeverity::Error,
                               "Could not read the semantic atlas profile", path);
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if(error.error != QJsonParseError::NoError || !document.isObject())
    {
        if(report) report->add("terrain.atlas_profile_json", EValidationSeverity::Error,
                               "The semantic atlas profile is not valid JSON", path);
        return false;
    }
    return fromJson(document.object(), profile, report);
}

bool STerrainAtlasProfile::normalizeMembership(CValidationReport* report)
{
    QMap<qint32, int> familyIndex;
    for(int index = 0; index < families.size(); ++index)
    {
        if(families.at(index).id <= 0 || familyIndex.contains(families.at(index).id))
        {
            if(report) report->add("terrain.atlas_profile_family_id", EValidationSeverity::Error,
                                   "Material family identifiers must be unique positive integers");
            return false;
        }
        familyIndex.insert(families.at(index).id, index);
        families[index].memberSlots.clear();
    }
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                              slot.signature.southEast, slot.signature.southWest};
        for(qint32 id : ids)
        {
            if(!familyIndex.contains(id))
            {
                if(report) report->add("terrain.atlas_profile_orphan_family", EValidationSeverity::Error,
                                       "A slot signature refers to a missing material family",
                                       QString::number(slot.slot));
                return false;
            }
            QVector<int>& memberSlots = families[familyIndex.value(id)].memberSlots;
            if(!memberSlots.contains(slot.slot)) memberSlots.append(slot.slot);
        }
    }
    for(STerrainAtlasProfileFamily& family : families)
        std::sort(family.memberSlots.begin(), family.memberSlots.end());
    std::sort(families.begin(), families.end(), [](const STerrainAtlasProfileFamily& a,
                                                    const STerrainAtlasProfileFamily& b) {
        return a.id < b.id;
    });
    return true;
}

bool STerrainAtlasProfile::mergeFamilies(const QVector<qint32>& requestedIds,
                                         const QString& mergedName,
                                         CValidationReport* report)
{
    QSet<qint32> ids;
    for(qint32 id : requestedIds) ids.insert(id);
    if(ids.size() < 2 || mergedName.trimmed().isEmpty())
    {
        if(report) report->add("terrain.atlas_profile_merge", EValidationSeverity::Error,
                               "Select at least two families and provide a merged name");
        return false;
    }
    QVector<int> indices;
    for(int index = 0; index < families.size(); ++index)
        if(ids.contains(families.at(index).id)) indices.append(index);
    if(indices.size() != ids.size())
    {
        if(report) report->add("terrain.atlas_profile_merge_missing", EValidationSeverity::Error,
                               "A selected material family no longer exists");
        return false;
    }
    const ETerrainAtlasLayer layer = families.at(indices.front()).layer;
    for(int index : indices)
        if(families.at(index).layer != layer)
        {
            if(report) report->add("terrain.atlas_profile_merge_layer", EValidationSeverity::Error,
                                   "Land and overlay families cannot be merged");
            return false;
        }
    const qint32 targetId = *std::min_element(ids.constBegin(), ids.constEnd());
    for(STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        qint32* corners[] = {&slot.signature.northWest, &slot.signature.northEast,
                             &slot.signature.southEast, &slot.signature.southWest};
        for(qint32* corner : corners) if(ids.contains(*corner)) *corner = targetId;
    }
    for(int index = families.size() - 1; index >= 0; --index)
        if(ids.contains(families.at(index).id) && families.at(index).id != targetId)
            families.removeAt(index);
    for(STerrainAtlasProfileFamily& family : families)
        if(family.id == targetId)
        {
            family.name = mergedName.trimmed();
            family.ambiguous = true;
            break;
        }
    approved = false;
    return normalizeMembership(report);
}

bool STerrainAtlasProfile::splitFamily(qint32 familyId,
                                       const QVector<int>& selectedSlots,
                                       const QString& newName,
                                       CValidationReport* report)
{
    QSet<int> requested;
    for(int slot : selectedSlots) requested.insert(slot);
    int familyIndex = -1;
    qint32 maximumId = 0;
    for(int index = 0; index < families.size(); ++index)
    {
        maximumId = qMax(maximumId, families.at(index).id);
        if(families.at(index).id == familyId) familyIndex = index;
    }
    if(familyIndex < 0 || requested.isEmpty() || newName.trimmed().isEmpty())
    {
        if(report) report->add("terrain.atlas_profile_split", EValidationSeverity::Error,
                               "Select a family, at least one of its slots, and provide a split name");
        return false;
    }
    QSet<int> existing;
    for(int slot : families.at(familyIndex).memberSlots) existing.insert(slot);
    if(requested == existing || !std::all_of(requested.constBegin(), requested.constEnd(),
                                              [&existing](int slot) { return existing.contains(slot); }))
    {
        if(report) report->add("terrain.atlas_profile_split_scope", EValidationSeverity::Error,
                               "A split must move a proper subset of the selected family's slots");
        return false;
    }
    STerrainAtlasProfileFamily split = families.at(familyIndex);
    split.id = maximumId + 1;
    split.name = newName.trimmed();
    split.ambiguous = true;
    split.memberSlots.clear();
    families[familyIndex].ambiguous = true;
    families.append(split);
    for(STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        if(!requested.contains(slot.slot)) continue;
        qint32* corners[] = {&slot.signature.northWest, &slot.signature.northEast,
                             &slot.signature.southEast, &slot.signature.southWest};
        for(qint32* corner : corners) if(*corner == familyId) *corner = split.id;
    }
    approved = false;
    return normalizeMembership(report);
}

QString CTerrainAtlasProfileStore::profilePath(const QString& mprNamespace,
                                                const QString& rootDirectory)
{
    QString safe = mprNamespace.trimmed().toLower();
    safe.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("_"));
    if(safe.isEmpty()) safe = QStringLiteral("unnamed");
    const QByteArray namespaceSuffix = QCryptographicHash::hash(
        mprNamespace.trimmed().toLower().toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    const QString root = rootDirectory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        : rootDirectory;
    return QDir(root).filePath(QStringLiteral("terrain-atlas-profiles/%1-%2.semantic-atlas.json")
        .arg(safe, QString::fromLatin1(namespaceSuffix)));
}

bool CTerrainAtlasProfileStore::save(const STerrainAtlasProfile& profile,
                                     const QString& rootDirectory,
                                     CValidationReport* report)
{
    const QString path = profilePath(profile.mprNamespace, rootDirectory);
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if(report) report->add("terrain.atlas_profile_directory", EValidationSeverity::Error,
                               "Could not create the semantic atlas profile directory",
                               QFileInfo(path).absolutePath());
        return false;
    }
    return profile.save(path, report);
}

bool CTerrainAtlasProfileStore::load(const QString& mprNamespace,
                                     STerrainAtlasProfile* profile,
                                     const QString& rootDirectory,
                                     CValidationReport* report)
{
    return STerrainAtlasProfile::load(profilePath(mprNamespace, rootDirectory), profile, report);
}

bool STerrainAtlasProfile::validateBinding(const QString& currentNamespace,
                                           const QByteArray& currentHash,
                                           int currentSlotCount,
                                           CValidationReport* report) const
{
    bool valid = true;
    if(mprNamespace.compare(currentNamespace, Qt::CaseInsensitive) != 0)
    {
        valid = false;
        if(report) report->add("terrain.atlas_profile_namespace_mismatch", EValidationSeverity::Error,
                               "The approved semantic atlas profile belongs to another MPR namespace",
                               currentNamespace);
    }
    if(atlasPixelSha256.toLower() != currentHash.toLower())
    {
        valid = false;
        if(report) report->add("terrain.atlas_profile_hash_mismatch", EValidationSeverity::Error,
                               "The decoded atlas pixels no longer match the approved semantic profile");
    }
    if(atlasSlotCount != currentSlotCount)
    {
        valid = false;
        if(report) report->add("terrain.atlas_profile_slot_mismatch", EValidationSeverity::Error,
                               "The atlas slot count no longer matches the approved semantic profile");
    }
    return valid;
}

STerrainAtlasAnalysis STerrainAtlasProfile::approvedAnalysis(CValidationReport* report) const
{
    STerrainAtlasAnalysis result;
    if(!approved)
    {
        if(report) report->add("terrain.atlas_profile_unapproved", EValidationSeverity::Error,
                               "Semantic painting is gated until this atlas profile is approved");
        return result;
    }
    for(const STerrainAtlasProfileFamily& family : families)
        if(family.ambiguous)
        {
            if(report) report->add("terrain.atlas_profile_ambiguous", EValidationSeverity::Error,
                                   "An approved atlas profile still contains an ambiguous material family",
                                   family.name);
            return result;
        }
    QMap<qint32, STerrainAtlasProfileFamily> byId;
    for(const STerrainAtlasProfileFamily& family : families)
    {
        if(family.name.trimmed().isEmpty())
        {
            if(report) report->add("terrain.atlas_profile_family_name", EValidationSeverity::Error,
                                   "Every reviewed material family requires a name",
                                   QString::number(family.id));
            return result;
        }
        byId.insert(family.id, family);
    }
    result.templateName = QStringLiteral("Reviewed semantic profile for %1").arg(mprNamespace);
    result.expectedSlots = atlasSlotCount;
    result.landCatalog.setMirrorPaintGridY(true);
    result.overlayCatalog.setMirrorPaintGridY(true);
    result.overlayCompatibleCatalog.setMirrorPaintGridY(true);
    QMap<qint32, int> pureCounts, representatives;
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        if(slot.ambiguous)
        {
            if(report) report->add("terrain.atlas_profile_ambiguous_slot", EValidationSeverity::Error,
                                   "An approved atlas profile still contains an ambiguous slot",
                                   QString::number(slot.slot));
            return STerrainAtlasAnalysis();
        }
        const ETerrainAtlasLayer layer = byId.value(slot.signature.northWest).layer;
        const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                              slot.signature.southEast, slot.signature.southWest};
        for(qint32 id : ids)
            if(!byId.contains(id) || byId.value(id).layer != layer)
            {
                if(report) report->add("terrain.atlas_profile_layer_mix", EValidationSeverity::Error,
                                       "A reviewed slot mixes land and overlay material families",
                                       QString::number(slot.slot));
                return STerrainAtlasAnalysis();
            }
        CTerrainTransitionCatalog* catalog = layer == ETerrainAtlasLayer::Land
            ? &result.landCatalog : &result.overlayCatalog;
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.atlasIndex = quint8(slot.slot / 64);
            packed.tileIndex = quint8(slot.slot % 64);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = CTerrainTransitionCatalog::rotated(slot.signature, rotation);
            variant.packedTile = packed.encode();
            if(!catalog->addVariant(variant, report)
               || !result.overlayCompatibleCatalog.addVariant(variant, report))
                return STerrainAtlasAnalysis();
        }
        if(uniform(slot.signature))
        {
            ++pureCounts[slot.signature.northWest];
            if(!representatives.contains(slot.signature.northWest))
                representatives.insert(slot.signature.northWest, slot.slot);
        }
        ++result.describedSlots;
    }
    for(const STerrainAtlasProfileFamily& family : families)
    {
        if(!pureCounts.contains(family.id))
        {
            if(report) report->add("terrain.atlas_profile_missing_uniform", EValidationSeverity::Error,
                                   "A reviewed material family has no uniform atlas tile and cannot be painted safely",
                                   family.name);
            return STerrainAtlasAnalysis();
        }
        STerrainAtlasMaterial material;
        material.id = family.id;
        material.name = family.name;
        material.layer = family.layer;
        material.underwater = family.underwater;
        material.representativeSlot = representatives.value(family.id, -1);
        material.pureVariantCount = pureCounts.value(family.id);
        result.materials.append(material);
    }
    result.usable = !result.materials.isEmpty() && result.describedSlots == result.expectedSlots;
    return result;
}

QByteArray CTerrainAtlasProfileInference::pixelSha256(const QVector<QImage>& images)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(images.size())); hash.addData("\n", 1);
    for(const QImage& source : images)
    {
        const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
        hash.addData(QByteArray::number(image.width())); hash.addData("x", 1);
        hash.addData(QByteArray::number(image.height())); hash.addData("\n", 1);
        for(int y = 0; y < image.height(); ++y)
            hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4);
    }
    return hash.result().toHex();
}

STerrainAtlasProfile CTerrainAtlasProfileInference::propose(
    const QString& mprNamespace, const QVector<QImage>& images,
    const QVector<qint32>& tileTypes, const QVector<STerrainAtlasSlotUsage>& usage,
    CValidationReport* report)
{
    STerrainAtlasProfile result;
    result.mprNamespace = mprNamespace.trimmed();
    result.atlasPixelSha256 = pixelSha256(images);
    result.atlasSlotCount = images.size();
    result.provenance = QStringLiteral("decoded-atlas-pixels+mpr-tile-types+observed-map-usage-v1");
    if(result.mprNamespace.isEmpty() || images.isEmpty() || tileTypes.size() != images.size()
       || usage.size() != images.size())
    {
        if(report) report->add("terrain.atlas_inference_input", EValidationSeverity::Error,
                               "Atlas inference requires one image, MPR tile category, and usage record per slot");
        return result;
    }
    struct SNode { int slot; int corner; qint32 type; ETerrainAtlasLayer layer; bool ambiguous; SCornerColor color; };
    QVector<SNode> nodes;
    for(int slot = 0; slot < images.size(); ++slot)
    {
        const bool land = usage.at(slot).landCount > 0;
        const bool overlay = usage.at(slot).overlayCount > 0;
        const bool ambiguous = land == overlay; // both used, or unused
        const ETerrainAtlasLayer layer = overlay && !land
            ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
        if(ambiguous && report)
            report->add("terrain.atlas_inference_ambiguous_layer", EValidationSeverity::Warning,
                        land ? "The atlas slot is used on both land and overlay surfaces"
                             : "The atlas slot is unused; its proposed layer requires review",
                        QString::number(slot));
        for(int corner = 0; corner < 4; ++corner)
            nodes.append({slot, corner, tileTypes.at(slot), layer, ambiguous,
                          cornerColor(images.at(slot), corner)});
    }
    QVector<int> parent(nodes.size());
    for(int i = 0; i < parent.size(); ++i) parent[i] = i;
    const auto root = [&parent](int value) {
        int current = value;
        while(parent[current] != current) current = parent[current];
        return current;
    };
    struct SEdge { int distance; int a; int b; };
    QVector<SEdge> edges;
    const int threshold = 28 * 28 * 3;
    for(int a = 0; a < nodes.size(); ++a)
        for(int b = a + 1; b < nodes.size(); ++b)
            if(nodes.at(a).type == nodes.at(b).type && nodes.at(a).layer == nodes.at(b).layer)
            {
                const int distance = colorDistance(nodes.at(a).color, nodes.at(b).color);
                if(distance <= threshold) edges.append({distance, a, b});
            }
    std::sort(edges.begin(), edges.end(), [](const SEdge& a, const SEdge& b) {
        if(a.distance != b.distance) return a.distance < b.distance;
        if(a.a != b.a) return a.a < b.a;
        return a.b < b.b;
    });
    for(const SEdge& edge : edges)
    {
        const int a = root(edge.a), b = root(edge.b);
        if(a != b) parent[qMax(a, b)] = qMin(a, b);
    }
    QMap<int, qint32> familyForRoot;
    for(int i = 0; i < nodes.size(); ++i)
    {
        const int nodeRoot = root(i);
        if(!familyForRoot.contains(nodeRoot))
        {
            STerrainAtlasProfileFamily family;
            family.id = result.families.size() + 1;
            family.name = QStringLiteral("Proposed %1 family %2")
                .arg(layerName(nodes.at(i).layer)).arg(family.id);
            family.layer = nodes.at(i).layer;
            family.tileType = nodes.at(i).type;
            familyForRoot.insert(nodeRoot, family.id);
            result.families.append(family);
        }
        STerrainAtlasProfileFamily& family = result.families[familyForRoot.value(nodeRoot) - 1];
        family.ambiguous |= nodes.at(i).ambiguous;
        if(!family.memberSlots.contains(nodes.at(i).slot)) family.memberSlots.append(nodes.at(i).slot);
    }
    for(STerrainAtlasProfileFamily& family : result.families)
    {
        std::sort(family.memberSlots.begin(), family.memberSlots.end());
        qint64 landUses = 0, belowOverlay = 0;
        for(int slot : family.memberSlots)
        {
            landUses += usage.at(slot).landCount;
            belowOverlay += usage.at(slot).landBelowOverlayCount;
        }
        family.underwater = family.layer == ETerrainAtlasLayer::Land
            && landUses > 0 && belowOverlay == landUses;
        if(family.layer == ETerrainAtlasLayer::Land && belowOverlay > 0 && belowOverlay < landUses)
        {
            family.ambiguous = true;
            if(report) report->add("terrain.atlas_inference_ambiguous_underwater", EValidationSeverity::Warning,
                                   "A proposed land family is used both below and outside overlays",
                                   family.name);
        }
    }
    for(int slot = 0; slot < images.size(); ++slot)
    {
        STerrainAtlasProfileSlot proposed;
        proposed.slot = slot;
        const int base = slot * 4;
        proposed.signature.northWest = familyForRoot.value(root(base));
        proposed.signature.northEast = familyForRoot.value(root(base + 1));
        proposed.signature.southEast = familyForRoot.value(root(base + 2));
        proposed.signature.southWest = familyForRoot.value(root(base + 3));
        proposed.ambiguous = nodes.at(base).ambiguous;
        result.slotDefinitions.append(proposed);
    }
    // A family pair with no authored mixed slot cannot be blended safely.
    for(int a = 0; a < result.families.size(); ++a)
        for(int b = a + 1; b < result.families.size(); ++b)
        {
            const auto& first = result.families.at(a);
            const auto& second = result.families.at(b);
            if(first.layer != second.layer) continue;
            bool transition = false;
            for(const STerrainAtlasProfileSlot& slot : result.slotDefinitions)
            {
                const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                                      slot.signature.southEast, slot.signature.southWest};
                bool hasFirst = false, hasSecond = false;
                for(qint32 id : ids) { hasFirst |= id == first.id; hasSecond |= id == second.id; }
                transition |= hasFirst && hasSecond;
            }
            if(!transition && report)
                report->add("terrain.atlas_inference_missing_transition", EValidationSeverity::Warning,
                            "No mixed transition slot was inferred between proposed material families",
                            QStringLiteral("%1 / %2").arg(first.name, second.name));
        }
    return result;
}

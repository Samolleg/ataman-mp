#include "terrain_atlas_profile.h"

#include "terrain_virtual_atlas.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QRegularExpression>
#include <QTransform>
#include <algorithm>
#include <cmath>

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
    SCornerColor(int red = 0, int green = 0, int blue = 0,
                 int redSpread = 0, int greenSpread = 0, int blueSpread = 0,
                 int saturation = 0, int edge = 0, int roughness = 0)
        : r(red), g(green), b(blue), rs(redSpread), gs(greenSpread), bs(blueSpread),
          sat(saturation), edgeEnergy(edge), roughnessEnergy(roughness) {}
    int r;
    int g;
    int b;
    int rs;
    int gs;
    int bs;
    int sat;
    int edgeEnergy;
    int roughnessEnergy;
};

SCornerColor cornerColor(const QImage& source, int corner)
{
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    const int sample = qMax(1, qMin(image.width(), image.height()) / 6);
    const int x0 = (corner == 1 || corner == 2) ? image.width() - sample : 0;
    const int y0 = (corner >= 2) ? image.height() - sample : 0;
    qint64 red = 0, green = 0, blue = 0, weight = 0;
    qint64 redSquared = 0, greenSquared = 0, blueSquared = 0;
    qint64 saturation = 0;
    for(int y = y0; y < y0 + sample; ++y)
        for(int x = x0; x < x0 + sample; ++x)
        {
            const QRgb pixel = image.pixel(x, y);
            const int alpha = qAlpha(pixel);
            red += qRed(pixel) * alpha;
            green += qGreen(pixel) * alpha;
            blue += qBlue(pixel) * alpha;
            redSquared += qint64(qRed(pixel)) * qRed(pixel) * alpha;
            greenSquared += qint64(qGreen(pixel)) * qGreen(pixel) * alpha;
            blueSquared += qint64(qBlue(pixel)) * qBlue(pixel) * alpha;
            saturation += (qMax(qRed(pixel), qMax(qGreen(pixel), qBlue(pixel)))
                         - qMin(qRed(pixel), qMin(qGreen(pixel), qBlue(pixel)))) * alpha;
            weight += alpha;
        }
    if(weight == 0)
        return {};
    const int meanRed = int(red / weight);
    const int meanGreen = int(green / weight);
    const int meanBlue = int(blue / weight);
    const auto spread = [weight](qint64 squared, int mean) {
        return int(std::sqrt(qMax(0.0, double(squared) / double(weight)
                                      - double(mean * mean))));
    };
    const auto luminance = [&image](int x, int y) {
        const QRgb pixel = image.pixel(x, y);
        return (54 * qRed(pixel) + 183 * qGreen(pixel) + 19 * qBlue(pixel)) / 256;
    };
    qint64 edgeTotal = 0, edgeCount = 0, roughnessTotal = 0, roughnessCount = 0;
    for(int y = y0; y < y0 + sample; ++y)
        for(int x = x0; x < x0 + sample; ++x)
        {
            const int center = luminance(x, y);
            if(x > x0) { edgeTotal += qAbs(center - luminance(x - 1, y)); ++edgeCount; }
            if(y > y0) { edgeTotal += qAbs(center - luminance(x, y - 1)); ++edgeCount; }
            if(x > x0 && x + 1 < x0 + sample && y > y0 && y + 1 < y0 + sample)
            {
                roughnessTotal += qAbs(4 * center - luminance(x - 1, y)
                    - luminance(x + 1, y) - luminance(x, y - 1)
                    - luminance(x, y + 1));
                ++roughnessCount;
            }
        }
    return {meanRed, meanGreen, meanBlue,
            spread(redSquared, meanRed), spread(greenSquared, meanGreen),
            spread(blueSquared, meanBlue), int(saturation / weight),
            edgeCount ? int(edgeTotal / edgeCount) : 0,
            roughnessCount ? int(roughnessTotal / roughnessCount) : 0};
}

QString underwaterRoleName(ETerrainAtlasUnderwaterRole role)
{
    if(role == ETerrainAtlasUnderwaterRole::UnderwaterOnly)
        return QStringLiteral("underwater-only");
    if(role == ETerrainAtlasUnderwaterRole::Mixed)
        return QStringLiteral("mixed");
    return QStringLiteral("surface");
}

ETerrainAtlasUnderwaterRole parseUnderwaterRole(const QString& value, bool legacyUnderwater)
{
    if(value == QStringLiteral("underwater-only") || legacyUnderwater)
        return ETerrainAtlasUnderwaterRole::UnderwaterOnly;
    if(value == QStringLiteral("mixed"))
        return ETerrainAtlasUnderwaterRole::Mixed;
    return ETerrainAtlasUnderwaterRole::Surface;
}
}

QImage terrainVertexJunctionThumbnail(
    const QVector<QImage>& images, const CTerrainTransitionCatalog& catalog,
    qint32 materialId, qint32 supportId, int fallbackSlot)
{
    if(images.isEmpty())
        return QImage();
    const int sourceSize = qMax(1, qMin(images.front().width(), images.front().height()));
    QImage result(sourceSize * 2, sourceSize * 2, QImage::Format_RGBA8888);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    const auto drawCell = [&](int cellX, int cellY, int selectedCorner) {
        STerrainTransitionSignature visualSignature;
        visualSignature.northWest = visualSignature.northEast = supportId;
        visualSignature.southEast = visualSignature.southWest = supportId;
        if(selectedCorner == 0) visualSignature.northWest = materialId;
        if(selectedCorner == 1) visualSignature.northEast = materialId;
        if(selectedCorner == 2) visualSignature.southEast = materialId;
        if(selectedCorner == 3) visualSignature.southWest = materialId;
        const STerrainTransitionSignature paintSignature =
            CTerrainTransitionCatalog::mirroredGridY(visualSignature);
        bool found = false;
        const quint16 packedValue = catalog.selectForPaint(
            paintSignature, cellX, cellY, 0x54484D42u, &found);
        const SMprPackedTile packed = SMprPackedTile::decode(packedValue);
        const int slot = found ? packed.atlasSlot() : fallbackSlot;
        if(slot < 0 || slot >= images.size())
            return;
        QImage tile = images.at(slot);
        if(found && packed.rotation != 0)
        {
            QTransform rotation;
            rotation.rotate(90.0 * packed.rotation);
            tile = tile.transformed(rotation, Qt::SmoothTransformation);
        }
        painter.drawImage(QRect(cellX * sourceSize, cellY * sourceSize,
                                sourceSize, sourceSize), tile);
    };
    if(supportId > 0 && supportId != materialId)
    {
        drawCell(0, 0, 2);
        drawCell(1, 0, 3);
        drawCell(1, 1, 0);
        drawCell(0, 1, 1);
    }
    else
    {
        for(int y = 0; y < 2; ++y)
            for(int x = 0; x < 2; ++x)
                drawCell(x, y, -1);
    }
    painter.end();
    return result;
}

namespace {

int colorDistance(const SCornerColor& lhs, const SCornerColor& rhs)
{
    const auto lab = [](const SCornerColor& color) {
        auto linear = [](double channel) {
            channel /= 255.0;
            return channel <= 0.04045 ? channel / 12.92
                                      : std::pow((channel + 0.055) / 1.055, 2.4);
        };
        const double red = linear(color.r), green = linear(color.g), blue = linear(color.b);
        const double x = (red * 0.4124 + green * 0.3576 + blue * 0.1805) / 0.95047;
        const double y = (red * 0.2126 + green * 0.7152 + blue * 0.0722);
        const double z = (red * 0.0193 + green * 0.1192 + blue * 0.9505) / 1.08883;
        auto curve = [](double value) {
            return value > 0.008856 ? std::cbrt(value) : 7.787 * value + 16.0 / 116.0;
        };
        return QVector<double>({116.0 * curve(y) - 16.0,
                                500.0 * (curve(x) - curve(y)),
                                200.0 * (curve(y) - curve(z))});
    };
    const QVector<double> left = lab(lhs), right = lab(rhs);
    const double dl = left[0] - right[0], da = left[1] - right[1],
                 db = left[2] - right[2];
    const int drs = lhs.rs - rhs.rs, dgs = lhs.gs - rhs.gs,
              dbs = lhs.bs - rhs.bs;
    const int dsat = lhs.sat - rhs.sat;
    const int dedge = lhs.edgeEnergy - rhs.edgeEnergy;
    const int drough = lhs.roughnessEnergy - rhs.roughnessEnergy;
    return qRound((dl * dl + da * da + db * db) * 100.0)
        + 24 * (drs * drs + dgs * dgs + dbs * dbs)
        + 4 * dsat * dsat + 32 * dedge * dedge + 16 * drough * drough;
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
    root.insert(QStringLiteral("schemaVersion"), 8);
    root.insert(QStringLiteral("cornerSpace"),
                QStringLiteral("tile-preview-nw-ne-se-sw-v1"));
    root.insert(QStringLiteral("mprNamespace"), mprNamespace);
    root.insert(QStringLiteral("atlasPixelSha256"), QString::fromLatin1(atlasPixelSha256));
    root.insert(QStringLiteral("atlasSlotCount"), atlasSlotCount);
    root.insert(QStringLiteral("provenance"), provenance);
    if(!resourceArchiveHint.trimmed().isEmpty())
        root.insert(QStringLiteral("resourceArchiveHint"),
                    QDir::fromNativeSeparators(resourceArchiveHint.trimmed()));
    root.insert(QStringLiteral("approved"), approved);
    QJsonArray familyArray;
    for(const STerrainAtlasProfileFamily& family : families)
    {
        QJsonObject object;
        object.insert(QStringLiteral("id"), family.id);
        object.insert(QStringLiteral("name"), family.name);
        object.insert(QStringLiteral("layer"), layerName(family.layer));
        object.insert(QStringLiteral("underwater"), family.underwater);
        QJsonArray allowedLayers;
        if(family.landAllowed) allowedLayers.append(QStringLiteral("land"));
        if(family.overlayAllowed) allowedLayers.append(QStringLiteral("overlay"));
        object.insert(QStringLiteral("allowedLayers"), allowedLayers);
        object.insert(QStringLiteral("underwaterRole"),
                      underwaterRoleName(family.underwaterRole));
        object.insert(QStringLiteral("ambiguous"), family.ambiguous);
        object.insert(QStringLiteral("paintSelectable"), family.paintSelectable);
        object.insert(QStringLiteral("tileType"), family.tileType);
        object.insert(QStringLiteral("thumbnailSlot"), family.thumbnailSlot);
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
        object.insert(QStringLiteral("assigned"), slot.assigned);
        object.insert(QStringLiteral("ambiguous"), slot.ambiguous);
        if(!slot.sourceSlotAliases.isEmpty())
        {
            object.insert(QStringLiteral("sourceNamespace"),
                          slot.sourceNamespace);
            QJsonArray aliases;
            for(int sourceSlot : slot.sourceSlotAliases)
                aliases.append(sourceSlot);
            object.insert(QStringLiteral("sourceSlots"), aliases);
            object.insert(QStringLiteral("sourcePixelSha256"),
                          QString::fromLatin1(slot.sourcePixelSha256));
        }
        slotArray.append(object);
    }
    root.insert(QStringLiteral("slots"), slotArray);
    return root;
}

bool STerrainAtlasProfile::fromJson(const QJsonObject& object,
                                    STerrainAtlasProfile* profile,
                                    CValidationReport* report)
{
    const int schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt();
    if(!profile || (schemaVersion < 1 || schemaVersion > 8)
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
    parsed.resourceArchiveHint = QDir::fromNativeSeparators(
        object.value(QStringLiteral("resourceArchiveHint")).toString().trimmed());
    parsed.approved = object.value(QStringLiteral("approved")).toBool(false);
    if(schemaVersion >= 6
       && object.value(QStringLiteral("cornerSpace")).toString()
            != QStringLiteral("tile-preview-nw-ne-se-sw-v1"))
    {
        if(report) report->add("terrain.atlas_profile_corner_space",
                               EValidationSeverity::Error,
                               "The semantic atlas profile uses an unsupported corner coordinate space");
        return false;
    }
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
        family.landAllowed = family.layer == ETerrainAtlasLayer::Land;
        family.overlayAllowed = family.layer == ETerrainAtlasLayer::Overlay;
        if(schemaVersion >= 7 && item.value(QStringLiteral("allowedLayers")).isArray())
        {
            family.landAllowed = false;
            family.overlayAllowed = false;
            for(const QJsonValue& layer : item.value(QStringLiteral("allowedLayers")).toArray())
            {
                family.landAllowed |= layer.toString() == QStringLiteral("land");
                family.overlayAllowed |= layer.toString() == QStringLiteral("overlay");
            }
            if(!family.landAllowed && !family.overlayAllowed)
            {
                if(report) report->add("terrain.atlas_profile_family_layers",
                    EValidationSeverity::Error,
                    "A material family must allow Ground, Overlay, or both",
                    family.name);
                return false;
            }
        }
        family.underwaterRole = parseUnderwaterRole(
            item.value(QStringLiteral("underwaterRole")).toString(), family.underwater);
        family.underwater = family.underwaterRole
            == ETerrainAtlasUnderwaterRole::UnderwaterOnly;
        family.ambiguous = item.value(QStringLiteral("ambiguous")).toBool();
        family.paintSelectable = schemaVersion >= 3
            ? item.value(QStringLiteral("paintSelectable")).toBool(true) : true;
        family.tileType = item.value(QStringLiteral("tileType")).toInt(-1);
        family.thumbnailSlot = schemaVersion >= 2
            ? item.value(QStringLiteral("thumbnailSlot")).toInt(-1) : -1;
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
        slot.assigned = schemaVersion >= 2
            ? item.value(QStringLiteral("assigned")).toBool(!slot.ambiguous)
            : !slot.ambiguous;
        if(schemaVersion >= 8
           && item.value(QStringLiteral("sourceSlots")).isArray())
        {
            slot.sourceNamespace = item.value(
                QStringLiteral("sourceNamespace")).toString().trimmed();
            slot.sourcePixelSha256 = item.value(
                QStringLiteral("sourcePixelSha256")).toString().toLatin1();
            QSet<int> aliases;
            for(const QJsonValue& aliasValue : item.value(
                    QStringLiteral("sourceSlots")).toArray())
            {
                const int alias = aliasValue.toInt(-1);
                if(alias < 0 || aliases.contains(alias))
                {
                    if(report) report->add(
                        "terrain.atlas_profile_source_alias",
                        EValidationSeverity::Error,
                        "A finalized atlas provenance alias is invalid or duplicated");
                    return false;
                }
                aliases.insert(alias);
                slot.sourceSlotAliases.append(alias);
            }
            const bool hashValid = QRegularExpression(
                QStringLiteral("^[0-9a-fA-F]{64}$")).match(
                    QString::fromLatin1(slot.sourcePixelSha256)).hasMatch();
            if(slot.sourceNamespace.isEmpty()
               || slot.sourceSlotAliases.isEmpty() || !hashValid)
            {
                if(report) report->add(
                    "terrain.atlas_profile_source_alias",
                    EValidationSeverity::Error,
                    "A finalized atlas provenance alias is incomplete");
                return false;
            }
        }
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
    // Profiles produced by pixel inference already store the visible corners
    // of CTileForm's preview images. Authored-template profiles through schema
    // 5 instead stored the paint-grid signature, whose vertical axis is the
    // opposite of the preview. Convert that known provenance exactly once;
    // do not guess for third-party legacy profiles that lack a declared space.
    if(schemaVersion < 6
       && parsed.provenance.startsWith(QStringLiteral("authored-template:")))
    {
        for(STerrainAtlasProfileSlot& slot : parsed.slotDefinitions)
            slot.signature = CTerrainTransitionCatalog::mirroredGridY(slot.signature);
    }
    else if(schemaVersion >= 4 && schemaVersion < 6
            && !parsed.provenance.startsWith(QStringLiteral("decoded-atlas-"))
            && !parsed.provenance.startsWith(
                QStringLiteral("decoded-atlas-perceptual-texture+")))
    {
        if(report) report->add("terrain.atlas_profile_corner_space",
                               EValidationSeverity::Error,
                               "This legacy custom profile does not declare whether its corners follow the tile preview or the paint grid");
        return false;
    }
    if(!parsed.normalizeMembership(report))
        return false;
    parsed.schemaVersion = 8;
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
        if(!families.at(index).landAllowed && !families.at(index).overlayAllowed)
        {
            if(report) report->add("terrain.atlas_profile_family_layers",
                EValidationSeverity::Error,
                "A material family must allow Ground, Overlay, or both",
                families.at(index).name);
            return false;
        }
        familyIndex.insert(families.at(index).id, index);
        families[index].memberSlots.clear();
    }
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        if(!slot.assigned)
            continue;
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
    bool allowLand = true, allowOverlay = true;
    for(int index : indices)
    {
        allowLand &= families.at(index).landAllowed;
        allowOverlay &= families.at(index).overlayAllowed;
    }
    if(!allowLand && !allowOverlay)
    {
        if(report) report->add("terrain.atlas_profile_merge_layer", EValidationSeverity::Error,
                               "Land and overlay families cannot be merged because they do not share a compatible section");
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
            family.landAllowed = allowLand;
            family.overlayAllowed = allowOverlay;
            family.layer = !allowLand && allowOverlay
                ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
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
    split.thumbnailSlot = -1;
    split.memberSlots.clear();
    if(requested.contains(families.at(familyIndex).thumbnailSlot))
        families[familyIndex].thumbnailSlot = -1;
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

bool STerrainAtlasProfile::assignSlotToFamily(int slotId, qint32 familyId,
                                              CValidationReport* report)
{
    bool familyFound = false;
    for(const STerrainAtlasProfileFamily& family : families)
        familyFound |= family.id == familyId;
    if(!familyFound)
    {
        if(report) report->add("terrain.atlas_profile_assign_family",
            EValidationSeverity::Error, "The target material does not exist");
        return false;
    }
    for(STerrainAtlasProfileSlot& slot : slotDefinitions)
        if(slot.slot == slotId)
        {
            slot.signature.northWest = familyId;
            slot.signature.northEast = familyId;
            slot.signature.southEast = familyId;
            slot.signature.southWest = familyId;
            slot.assigned = true;
            slot.ambiguous = false;
            approved = false;
            return normalizeMembership(report);
        }
    if(report) report->add("terrain.atlas_profile_assign_slot",
        EValidationSeverity::Error, "The atlas tile no longer exists",
        QString::number(slotId));
    return false;
}

bool STerrainAtlasProfile::replaceSlotFamily(int slotId, qint32 sourceFamilyId,
                                             qint32 targetFamilyId,
                                             CValidationReport* report)
{
    QMap<qint32, QPair<bool, bool>> layers;
    for(const STerrainAtlasProfileFamily& family : families)
        layers.insert(family.id, qMakePair(family.landAllowed, family.overlayAllowed));
    if(!layers.contains(targetFamilyId))
    {
        if(report) report->add("terrain.atlas_profile_replace_family",
            EValidationSeverity::Error, "The target material does not exist");
        return false;
    }
    for(STerrainAtlasProfileSlot& slot : slotDefinitions)
        if(slot.slot == slotId)
        {
            if(sourceFamilyId <= 0)
                return assignSlotToFamily(slotId, targetFamilyId, report);
            STerrainTransitionSignature replacement = slot.signature;
            qint32* corners[] = {&replacement.northWest, &replacement.northEast,
                                 &replacement.southEast, &replacement.southWest};
            bool changed = false;
            for(qint32* corner : corners)
                if(*corner == sourceFamilyId)
                {
                    *corner = targetFamilyId;
                    changed = true;
                }
            if(!changed)
            {
                if(report) report->add("terrain.atlas_profile_replace_source",
                    EValidationSeverity::Error,
                    "The atlas tile does not reference the dragged material",
                    QString::number(slotId));
                return false;
            }
            const qint32 ids[] = {replacement.northWest, replacement.northEast,
                                  replacement.southEast, replacement.southWest};
            bool allowLand = true, allowOverlay = true;
            for(qint32 id : ids)
            {
                if(!layers.contains(id))
                {
                    if(report) report->add("terrain.atlas_profile_replace_layer",
                        EValidationSeverity::Error,
                        "A terrain tile cannot combine ground and overlay materials",
                        QString::number(slotId));
                    return false;
                }
                allowLand &= layers.value(id).first;
                allowOverlay &= layers.value(id).second;
            }
            if(!allowLand && !allowOverlay)
            {
                if(report) report->add("terrain.atlas_profile_replace_layer",
                    EValidationSeverity::Error,
                    "These materials do not share a compatible Ground or Overlay section",
                    QString::number(slotId));
                return false;
            }
            slot.signature = replacement;
            slot.assigned = true;
            slot.ambiguous = false;
            approved = false;
            return normalizeMembership(report);
        }
    if(report) report->add("terrain.atlas_profile_replace_slot",
        EValidationSeverity::Error, "The atlas tile no longer exists",
        QString::number(slotId));
    return false;
}

bool STerrainAtlasProfile::setSlotCornerFamily(int slotId, int cornerIndex,
                                               qint32 familyId,
                                               CValidationReport* report)
{
    QMap<qint32, QPair<bool, bool>> layers;
    for(const STerrainAtlasProfileFamily& family : families)
        layers.insert(family.id, qMakePair(family.landAllowed, family.overlayAllowed));
    if(cornerIndex < 0 || cornerIndex > 3 || !layers.contains(familyId))
    {
        if(report) report->add("terrain.atlas_profile_corner",
            EValidationSeverity::Error, "Choose an existing material and tile corner");
        return false;
    }
    for(STerrainAtlasProfileSlot& slot : slotDefinitions)
        if(slot.slot == slotId)
        {
            STerrainTransitionSignature replacement = slot.signature;
            qint32* corners[] = {&replacement.northWest, &replacement.northEast,
                                 &replacement.southEast, &replacement.southWest};
            *corners[cornerIndex] = familyId;
            const qint32 ids[] = {replacement.northWest, replacement.northEast,
                                  replacement.southEast, replacement.southWest};
            bool allowLand = true, allowOverlay = true;
            for(qint32 id : ids)
            {
                if(!layers.contains(id))
                {
                    if(report) report->add("terrain.atlas_profile_corner_layer",
                        EValidationSeverity::Error,
                        "This corner would mix ground and overlay materials. Choose a material from the same section, or move the whole tile first.",
                        QString::number(slotId));
                    return false;
                }
                allowLand &= layers.value(id).first;
                allowOverlay &= layers.value(id).second;
            }
            if(!allowLand && !allowOverlay)
            {
                if(report) report->add("terrain.atlas_profile_corner_layer",
                    EValidationSeverity::Error,
                    "This corner would mix ground and overlay materials without a common section. Choose a compatible material.",
                    QString::number(slotId));
                return false;
            }
            slot.signature = replacement;
            slot.assigned = true;
            slot.ambiguous = false;
            approved = false;
            return normalizeMembership(report);
        }
    if(report) report->add("terrain.atlas_profile_corner_slot",
        EValidationSeverity::Error, "The atlas tile no longer exists",
        QString::number(slotId));
    return false;
}

bool STerrainAtlasProfile::unassignSlot(int slotId, CValidationReport* report)
{
    for(STerrainAtlasProfileSlot& slot : slotDefinitions)
        if(slot.slot == slotId)
        {
            slot.assigned = false;
            slot.ambiguous = true;
            approved = false;
            return normalizeMembership(report);
        }
    if(report) report->add("terrain.atlas_profile_unassign_slot",
        EValidationSeverity::Error, "The atlas tile no longer exists",
        QString::number(slotId));
    return false;
}

bool STerrainAtlasProfile::setFamilyThumbnail(qint32 familyId, int slotId,
                                              CValidationReport* report)
{
    STerrainAtlasProfileFamily* target = nullptr;
    for(STerrainAtlasProfileFamily& family : families)
        if(family.id == familyId) target = &family;
    const STerrainAtlasProfileSlot* slot = nullptr;
    for(const STerrainAtlasProfileSlot& candidate : slotDefinitions)
        if(candidate.slot == slotId) slot = &candidate;
    const bool referencesFamily = slot && (
        slot->signature.northWest == familyId
        || slot->signature.northEast == familyId
        || slot->signature.southEast == familyId
        || slot->signature.southWest == familyId);
    if(!target || !slot || !slot->assigned || !referencesFamily)
    {
        if(report) report->add("terrain.atlas_profile_thumbnail",
            EValidationSeverity::Error,
            "A palette thumbnail must be an assigned tile that contains that material",
            QString::number(slotId));
        return false;
    }
    target->thumbnailSlot = slotId;
    approved = false;
    return true;
}

int STerrainAtlasProfile::assignedSlotCount() const
{
    int count = 0;
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
        count += slot.assigned;
    return count;
}

QString CTerrainAtlasProfileStore::workingAtlasProvenance()
{
    return QStringLiteral("portable-working-modern-atlas-v1");
}

bool CTerrainAtlasProfileStore::isWorkingAtlasProfile(
    const STerrainAtlasProfile& profile)
{
    return profile.provenance == workingAtlasProvenance();
}

STerrainAtlasProfile CTerrainAtlasProfileStore::portableFinalizedProfile(
    const STerrainAtlasFinalization& finalization,
    const QVector<QImage>& decodedPhysicalTiles,
    const STerrainAtlasProfile& sourceProfile, ETerrainAtlasLayer layer,
    const QMap<qint32, QString>& materialNames,
    const QMap<qint32, qint32>& materialRuntimeTypes,
    CValidationReport* report)
{
    STerrainAtlasProfile profile;
    profile.mprNamespace = finalization.isolatedNamespace;
    profile.atlasPixelSha256 =
        CTerrainAtlasProfileInference::pixelSha256(decodedPhysicalTiles);
    profile.atlasSlotCount = decodedPhysicalTiles.size();
    profile.provenance = QStringLiteral("portable-finalized-modern-atlas-v1");
    profile.approved = true;

    QMap<qint32, STerrainAtlasProfileFamily> sourceFamilies;
    for(const STerrainAtlasProfileFamily& family : sourceProfile.families)
        sourceFamilies.insert(family.id, family);

    QSet<qint32> materialIds;
    for(int slot = 0; slot < decodedPhysicalTiles.size(); ++slot)
    {
        STerrainAtlasProfileSlot definition;
        definition.slot = slot;
        definition.assigned = slot < finalization.tiles.size();
        definition.ambiguous = false;
        if(definition.assigned)
        {
            const STerrainFinalizedTile& tile = finalization.tiles.at(slot);
            // Finalization has resolved and compacted the complete working
            // catalog. Its canonical grid signature is authoritative for
            // retained, imported, and generated logical output slots alike.
            definition.signature = CTerrainTransitionCatalog::mirroredGridY(
                tile.canonicalSignature);
            definition.assigned = definition.signature.northWest > 0
                && definition.signature.northEast > 0
                && definition.signature.southEast > 0
                && definition.signature.southWest > 0;
            if(!definition.assigned && report)
                report->add(
                    QStringLiteral("terrain.finalized_profile_signature"),
                    EValidationSeverity::Error,
                    QStringLiteral("A logical finalized atlas tile has no complete semantic signature."),
                    QStringLiteral("logical slot %1").arg(slot));
            QVector<int> aliases = tile.sourceSlotAliases;
            if(aliases.isEmpty() && tile.sourceSlot >= 0)
                aliases.append(tile.sourceSlot);
            const QString aliasNamespace = tile.sourceNamespace.isEmpty()
                ? sourceProfile.mprNamespace : tile.sourceNamespace;
            if(tile.authored && !aliasNamespace.trimmed().isEmpty()
               && !aliases.isEmpty() && tile.pixelSha256.size() == 64)
            {
                definition.sourceNamespace = aliasNamespace;
                definition.sourceSlotAliases = aliases;
                definition.sourcePixelSha256 = tile.pixelSha256;
            }
            if(definition.assigned)
                for(qint32 id : {definition.signature.northWest,
                                  definition.signature.northEast,
                                  definition.signature.southEast,
                                  definition.signature.southWest})
                    materialIds.insert(id);
        }
        profile.slotDefinitions.append(definition);
    }

    // JSON profiles intentionally describe every physical slot. Unassigned
    // page padding still needs a syntactically valid family signature so it
    // can round-trip through the strict parser; assigned=false keeps it out of
    // every paint catalog and usage count.
    if(!materialIds.isEmpty())
    {
        const qint32 paddingFamily = *materialIds.constBegin();
        for(STerrainAtlasProfileSlot& definition : profile.slotDefinitions)
            if(!definition.assigned)
            {
                definition.signature.northWest = paddingFamily;
                definition.signature.northEast = paddingFamily;
                definition.signature.southEast = paddingFamily;
                definition.signature.southWest = paddingFamily;
            }
    }

    for(qint32 id : materialIds)
    {
        STerrainAtlasProfileFamily family;
        if(sourceFamilies.contains(id)) family = sourceFamilies.value(id);
        family.id = id;
        family.name = materialNames.value(id, family.name);
        if(family.name.trimmed().isEmpty())
            family.name = QStringLiteral("Material %1").arg(id);
        if(!sourceFamilies.contains(id))
        {
            family.layer = layer;
            family.landAllowed = layer == ETerrainAtlasLayer::Land;
            family.overlayAllowed = layer == ETerrainAtlasLayer::Overlay;
        }
        if(materialRuntimeTypes.contains(id))
            family.tileType = materialRuntimeTypes.value(id);
        family.memberSlots.clear();
        family.thumbnailSlot = -1;
        for(const STerrainAtlasProfileSlot& definition : profile.slotDefinitions)
            if(definition.assigned
               && definition.signature.northWest == id
               && definition.signature.northEast == id
               && definition.signature.southEast == id
               && definition.signature.southWest == id)
            {
                family.thumbnailSlot = definition.slot;
                break;
            }
        profile.families.append(family);
    }
    profile.normalizeMembership(report);
    return profile;
}

STerrainAtlasProfile CTerrainAtlasProfileStore::workingAtlasProfile(
    const QString& mprNamespace,
    const QVector<QImage>& decodedPhysicalTiles,
    const CTerrainTransitionCatalog& workingCatalog,
    const STerrainAtlasProfile& sourceProfile,
    const QVector<STerrainGlobalMaterialFamily>& globalFamilies,
    CValidationReport* report)
{
    STerrainAtlasProfile profile;
    profile.mprNamespace = mprNamespace.trimmed();
    profile.atlasPixelSha256 =
        CTerrainAtlasProfileInference::pixelSha256(decodedPhysicalTiles);
    profile.atlasSlotCount = decodedPhysicalTiles.size();
    profile.provenance = workingAtlasProvenance();
    profile.approved = true;

    QMap<qint32, STerrainAtlasProfileFamily> families;
    for(const STerrainAtlasProfileFamily& local : sourceProfile.families)
    {
        STerrainAtlasProfileFamily family = local;
        family.id = CTerrainAtlasPresetCatalog::globalMaterialId(
            local.name, local.layer);
        family.memberSlots.clear();
        family.thumbnailSlot = -1;
        families.insert(family.id, family);
    }
    for(const STerrainGlobalMaterialFamily& global : globalFamilies)
    {
        STerrainAtlasProfileFamily family = families.value(global.id);
        family.id = global.id;
        family.name = global.name;
        family.layer = global.layer;
        family.landAllowed = global.layer == ETerrainAtlasLayer::Land;
        family.overlayAllowed = global.layer == ETerrainAtlasLayer::Overlay;
        family.paintSelectable = true;
        family.tileType = global.runtimeTerrainType;
        family.memberSlots.clear();
        family.thumbnailSlot = -1;
        families.insert(family.id, family);
    }

    QSet<qint32> usedMaterials;
    for(int slot = 0; slot < decodedPhysicalTiles.size(); ++slot)
    {
        SMprPackedTile packed;
        packed.atlasIndex = quint8(slot / 64);
        packed.tileIndex = quint8(slot % 64);
        bool found = false;
        STerrainAtlasProfileSlot definition;
        definition.slot = slot;
        const STerrainTransitionSignature gridSignature =
            workingCatalog.gridSignatureForPacked(packed.encode(), &found);
        // Profile schema v8 stores the corners visible in the atlas preview,
        // while a working catalog exposes the recovered terrain-grid corners.
        // This conversion must not depend on the catalog's internal mirror
        // flag. In particular, a catalog reopened from a portable finalized
        // profile has mirror=false; writing its grid tuple directly made the
        // next draft reopen reflect every non-symmetric tile a second time.
        definition.signature = found
            ? CTerrainTransitionCatalog::mirroredGridY(gridSignature)
            : STerrainTransitionSignature();
        definition.assigned = found;
        definition.ambiguous = false;
        if(found)
            for(qint32 id : {definition.signature.northWest,
                              definition.signature.northEast,
                              definition.signature.southEast,
                              definition.signature.southWest})
            {
                usedMaterials.insert(id);
                if(!families.contains(id))
                {
                    STerrainAtlasProfileFamily family;
                    family.id = id;
                    family.name = QStringLiteral("Material %1").arg(id);
                    family.paintSelectable = false;
                    families.insert(id, family);
                }
            }
        profile.slotDefinitions.append(definition);
    }
    if(!usedMaterials.isEmpty())
    {
        const qint32 paddingFamily = *usedMaterials.constBegin();
        for(STerrainAtlasProfileSlot& definition : profile.slotDefinitions)
            if(!definition.assigned)
            {
                definition.signature.northWest = paddingFamily;
                definition.signature.northEast = paddingFamily;
                definition.signature.southEast = paddingFamily;
                definition.signature.southWest = paddingFamily;
            }
    }
    for(qint32 id : usedMaterials)
    {
        STerrainAtlasProfileFamily family = families.value(id);
        if(family.name.trimmed().isEmpty())
            family.name = QStringLiteral("Material %1").arg(id);
        profile.families.append(family);
    }
    profile.normalizeMembership(report);
    return profile;
}

QString CTerrainAtlasProfileStore::profilePath(const QString& mprNamespace,
                                                const QString& rootDirectory)
{
    QString safe = mprNamespace.trimmed().toLower();
    safe.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("_"));
    if(safe.isEmpty()) safe = QStringLiteral("unnamed");
    const QByteArray namespaceSuffix = QCryptographicHash::hash(
        mprNamespace.trimmed().toLower().toUtf8(), QCryptographicHash::Sha256).toHex().left(12);
    // Endurance is distributed as a portable editor. Keep authored atlas
    // overrides with that editor instance so tester builds and installations
    // cannot silently inherit profiles from a shared AppData location.
    const QString root = rootDirectory.isEmpty()
        ? QCoreApplication::applicationDirPath() : rootDirectory;
    return QDir(root).filePath(QStringLiteral("terrain-atlas-profiles/%1-%2.semantic-atlas.json")
        .arg(safe, QString::fromLatin1(namespaceSuffix)));
}

QString CTerrainAtlasProfileStore::profilePathForMpr(
    const QString& mprNamespace, const QFileInfo& mprFile)
{
    QString safe = mprFile.completeBaseName().trimmed().toLower();
    safe.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")),
                 QStringLiteral("_"));
    if(safe.isEmpty()) safe = QStringLiteral("unnamed-map");
    const QByteArray key = mprNamespace.trimmed().toLower().toUtf8()
        + QByteArrayLiteral("\n")
        + mprFile.fileName().trimmed().toLower().toUtf8();
    const QByteArray suffix = QCryptographicHash::hash(
        key, QCryptographicHash::Sha256).toHex().left(12);
    return mprFile.dir().filePath(
        QStringLiteral("terrain-atlas-profiles/%1-%2.semantic-atlas.json")
            .arg(safe, QString::fromLatin1(suffix)));
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

bool CTerrainAtlasProfileStore::saveForMpr(
    const STerrainAtlasProfile& profile, const QFileInfo& mprFile,
    CValidationReport* report)
{
    const QString path = profilePathForMpr(profile.mprNamespace, mprFile);
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if(report) report->add("terrain.atlas_profile_directory",
            EValidationSeverity::Error,
            "Could not create the portable semantic atlas profile directory",
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

bool CTerrainAtlasProfileStore::loadForMpr(
    const QString& mprNamespace, const QFileInfo& mprFile,
    STerrainAtlasProfile* profile, QString* sourcePath,
    bool* usedLegacyNamespacePath, CValidationReport* report)
{
    if(sourcePath) sourcePath->clear();
    if(usedLegacyNamespacePath) *usedLegacyNamespacePath = false;
    const QString exactPath = profilePathForMpr(mprNamespace, mprFile);
    if(QFileInfo::exists(exactPath))
    {
        if(!STerrainAtlasProfile::load(exactPath, profile, report))
            return false;
        if(sourcePath) *sourcePath = exactPath;
        return true;
    }
    const QString legacyPath = profilePath(
        mprNamespace, mprFile.absolutePath());
    if(!QFileInfo::exists(legacyPath)
       || !STerrainAtlasProfile::load(legacyPath, profile, report))
        return false;
    if(sourcePath) *sourcePath = legacyPath;
    if(usedLegacyNamespacePath) *usedLegacyNamespacePath = true;
    return true;
}

namespace {
bool isExplicitTerrainAtlasOverride(const STerrainAtlasProfile& loaded)
{
    return loaded.provenance == QStringLiteral("user-authored-custom-profile")
        || loaded.provenance
            == QStringLiteral("portable-exact-stock-atlas-clone-v1")
        || loaded.provenance
            == QStringLiteral("portable-finalized-modern-atlas-v1")
        || CTerrainAtlasProfileStore::isWorkingAtlasProfile(loaded);
}

bool validateExplicitTerrainAtlasOverride(
    const STerrainAtlasProfile& loaded, const QString& path,
    const QString& mprNamespace, const QByteArray& atlasPixelSha256,
    int atlasSlotCount, STerrainAtlasProfile* profile,
    QString* sourcePath, CValidationReport* report)
{
    if(!profile || !isExplicitTerrainAtlasOverride(loaded))
    {
        if(report && !isExplicitTerrainAtlasOverride(loaded))
            report->add("terrain.atlas_profile_not_explicit_override",
                EValidationSeverity::Warning,
                "A stored generated/default profile was ignored; only an explicitly edited local profile or an exact-bound portable stock, working, or finalized atlas may override the built-in Material atlas",
                path);
        return false;
    }
    if(!loaded.validateBinding(mprNamespace, atlasPixelSha256,
                               atlasSlotCount, report))
        return false;
    *profile = loaded;
    if(sourcePath) *sourcePath = path;
    return true;
}
}

bool CTerrainAtlasProfileStore::loadExplicitOverride(
    const QString& mprNamespace, const QByteArray& atlasPixelSha256,
    int atlasSlotCount, STerrainAtlasProfile* profile,
    const QString& rootDirectory, QString* sourcePath,
    CValidationReport* report)
{
    const QString path = profilePath(mprNamespace, rootDirectory);
    if(!QFileInfo::exists(path) || !profile)
        return false;
    STerrainAtlasProfile loaded;
    if(!STerrainAtlasProfile::load(path, &loaded, report))
        return false;
    return validateExplicitTerrainAtlasOverride(
        loaded, path, mprNamespace, atlasPixelSha256, atlasSlotCount,
        profile, sourcePath, report);
}

bool CTerrainAtlasProfileStore::loadExplicitOverrideForMpr(
    const QString& mprNamespace, const QByteArray& atlasPixelSha256,
    int atlasSlotCount, const QFileInfo& mprFile,
    STerrainAtlasProfile* profile, QString* sourcePath,
    bool* usedLegacyNamespacePath, CValidationReport* report)
{
    STerrainAtlasProfile loaded;
    QString path;
    if(!loadForMpr(mprNamespace, mprFile, &loaded, &path,
                   usedLegacyNamespacePath, report))
        return false;
    return validateExplicitTerrainAtlasOverride(
        loaded, path, mprNamespace, atlasPixelSha256, atlasSlotCount,
        profile, sourcePath, report);
}

bool CTerrainAtlasProfileStore::removeLocalOverride(
    const QString& mprNamespace, const QString& rootDirectory,
    CValidationReport* report)
{
    const QString path = profilePath(mprNamespace, rootDirectory);
    if(!QFileInfo::exists(path))
        return true;
    if(QFile::remove(path))
        return true;
    if(report) report->add("terrain.atlas_profile_remove",
        EValidationSeverity::Error,
        "Could not remove the local Material atlas override", path);
    return false;
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
                               "This material atlas profile belongs to another MPR namespace",
                               currentNamespace);
    }
    if(atlasPixelSha256.toLower() != currentHash.toLower())
    {
        valid = false;
        if(report) report->add("terrain.atlas_profile_hash_mismatch", EValidationSeverity::Error,
                               "The decoded atlas pixels no longer match this material atlas profile");
    }
    if(atlasSlotCount != currentSlotCount)
    {
        valid = false;
        if(report) report->add("terrain.atlas_profile_slot_mismatch", EValidationSeverity::Error,
                               "The atlas slot count no longer matches this material atlas profile");
    }
    return valid;
}

STerrainAtlasAnalysis STerrainAtlasProfile::approvedAnalysis(CValidationReport* report) const
{
    if(!approved)
    {
        if(report) report->add("terrain.atlas_profile_unapproved", EValidationSeverity::Error,
                                "This material atlas profile has not been explicitly approved. Its safe assignments remain usable for painting.");
        return STerrainAtlasAnalysis();
    }
    if(assignedSlotCount() == 0)
    {
        if(report) report->add("terrain.atlas_profile_no_safe_assignments",
            EValidationSeverity::Error,
            "This profile has no structurally safe tile assignments to apply");
        return STerrainAtlasAnalysis();
    }
    QSet<qint32> usedFamilies;
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
        if(slot.assigned && !slot.ambiguous)
        {
            usedFamilies.insert(slot.signature.northWest);
            usedFamilies.insert(slot.signature.northEast);
            usedFamilies.insert(slot.signature.southEast);
            usedFamilies.insert(slot.signature.southWest);
        }
    for(const STerrainAtlasProfileFamily& family : families)
        if(family.ambiguous && usedFamilies.contains(family.id))
        {
            if(report) report->add("terrain.atlas_profile_ambiguous", EValidationSeverity::Error,
                                    "This active material still needs confirmation. Select it, verify its name and Ground, Underwater, or Overlay section, then choose Confirm material.",
                                    family.name);
            return STerrainAtlasAnalysis();
        }
    return usableAnalysis(report);
}

STerrainAtlasAnalysis STerrainAtlasProfile::usableAnalysis(CValidationReport* report) const
{
    STerrainAtlasAnalysis result;
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
    result.templateName = QStringLiteral("Material atlas profile for %1").arg(mprNamespace);
    result.expectedSlots = atlasSlotCount;
    QMap<qint32, int> pureCounts, representatives;
    QMap<qint32, QSet<int>> uniformSlots;
    for(const STerrainAtlasProfileSlot& slot : slotDefinitions)
    {
        if(!slot.assigned)
            continue;
        if(slot.ambiguous)
        {
            if(report) report->add("terrain.atlas_profile_ambiguous_slot", EValidationSeverity::Warning,
                                   "An unresolved atlas tile is excluded from automatic painting",
                                   QString::number(slot.slot));
            continue;
        }
        const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                              slot.signature.southEast, slot.signature.southWest};
        bool allowLand = true;
        bool allowOverlay = true;
        for(qint32 id : ids)
            if(!byId.contains(id))
            {
                if(report) report->add("terrain.atlas_profile_layer_mix", EValidationSeverity::Error,
                                       "This tile references a material that no longer exists. Move it to Unassigned or repair its four corners.",
                                       QString::number(slot.slot));
                return STerrainAtlasAnalysis();
            }
            else
            {
                allowLand &= byId.value(id).landAllowed;
                allowOverlay &= byId.value(id).overlayAllowed;
            }
        if(!allowLand && !allowOverlay)
        {
            if(report) report->add("terrain.atlas_profile_layer_mix", EValidationSeverity::Error,
                "This tile combines materials that do not share Ground or Overlay usage. Choose compatible materials for all four corners.",
                QString::number(slot.slot));
            return STerrainAtlasAnalysis();
        }
        // Profile signatures are the corners visible in the tile preview.
        // Terrain grid row zero is the opposite vertical side, so painting
        // uses one explicit reflection at this boundary. The renderer's
        // packed quarter-turn then has the opposite chirality in paint space:
        // M(R(v)) == R(-v)(M(v)). This rule is global, never atlas-specific.
        const STerrainTransitionSignature paintBase =
            CTerrainTransitionCatalog::mirroredGridY(slot.signature);
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.atlasIndex = quint8(slot.slot / 64);
            packed.tileIndex = quint8(slot.slot % 64);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = CTerrainTransitionCatalog::rotated(
                paintBase, -rotation);
            variant.packedTile = packed.encode();
            if((allowLand && !result.landCatalog.addVariant(variant, report))
               || (allowOverlay && !result.overlayCatalog.addVariant(variant, report))
               || !result.overlayCompatibleCatalog.addVariant(variant, report))
                return STerrainAtlasAnalysis();
        }
        if(uniform(slot.signature))
        {
            ++pureCounts[slot.signature.northWest];
            uniformSlots[slot.signature.northWest].insert(slot.slot);
            if(!representatives.contains(slot.signature.northWest)
               || slot.slot < representatives.value(slot.signature.northWest))
                representatives.insert(slot.signature.northWest, slot.slot);
        }
        ++result.describedSlots;
    }
    for(const STerrainAtlasProfileFamily& family : families)
    {
        if(!family.paintSelectable)
            continue;
        STerrainAtlasMaterial material;
        material.id = family.id;
        material.name = family.name;
        material.underwater = family.underwaterRole
            == ETerrainAtlasUnderwaterRole::UnderwaterOnly;
        material.cornerOnly = !pureCounts.contains(family.id);
        if(material.cornerOnly)
        {
            material.representativeSlot = family.thumbnailSlot >= 0
                && family.memberSlots.contains(family.thumbnailSlot)
                    ? family.thumbnailSlot
                    : (family.memberSlots.isEmpty() ? -1 : family.memberSlots.front());
            material.thumbnailSlot = material.representativeSlot;
            if(report) report->add("terrain.atlas_profile_corner_only_material",
                EValidationSeverity::Warning,
                "This material has no uniform tile and paints contextually at terrain corners",
                family.name);
        }
        else
        {
            material.representativeSlot = representatives.value(family.id, -1);
            material.thumbnailSlot = family.thumbnailSlot >= 0
                && family.memberSlots.contains(family.thumbnailSlot)
                    ? family.thumbnailSlot : material.representativeSlot;
        }
        material.pureVariantCount = pureCounts.value(family.id);
        if(material.representativeSlot >= 0)
        {
            if(family.landAllowed)
            {
                material.layer = ETerrainAtlasLayer::Land;
                result.materials.append(material);
            }
            if(family.overlayAllowed)
            {
                material.layer = ETerrainAtlasLayer::Overlay;
                material.underwater = false;
                result.materials.append(material);
            }
        }
    }
    result.usable = !result.materials.isEmpty() && result.describedSlots > 0;
    if(!result.usable && report)
        report->add("terrain.atlas_profile_no_safe_materials",
            EValidationSeverity::Error,
            "No structurally valid material assignments are available yet. Assign at least one atlas tile to a material before using this profile.");
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

QString CTerrainAtlasProfileInference::currentProvenance()
{
    return QStringLiteral(
        "decoded-atlas-perceptual-texture+mpr-tile-types+observed-map-usage-v11-preview-corners");
}

bool CTerrainAtlasProfileInference::isCompatibleInferenceProvenance(
    const QString& provenance)
{
    const bool generated = provenance.startsWith(QStringLiteral("decoded-atlas-pixels+"))
        || provenance.startsWith(QStringLiteral("decoded-atlas-perceptual-texture+"));
    return !generated || provenance == currentProvenance()
        || provenance == QStringLiteral(
            "decoded-atlas-perceptual-texture+mpr-tile-types+observed-map-usage-v10-calibrated-packed-rotation");
}

STerrainAtlasProfile CTerrainAtlasProfileInference::fromAuthoredAnalysis(
    const QString& mprNamespace, const QByteArray& atlasPixelSha256,
    int atlasSlotCount, const STerrainAtlasAnalysis& analysis,
    CValidationReport* report)
{
    STerrainAtlasProfile profile;
    profile.mprNamespace = mprNamespace;
    profile.atlasPixelSha256 = atlasPixelSha256;
    profile.atlasSlotCount = atlasSlotCount;
    profile.provenance = QStringLiteral("authored-template:%1").arg(analysis.templateName);
    profile.approved = analysis.usable;

    QMap<qint32, STerrainAtlasProfileFamily> families;
    for(const STerrainAtlasMaterial& material : analysis.materials)
    {
        STerrainAtlasProfileFamily family;
        family.id = material.id;
        family.name = material.name;
        family.layer = material.layer;
        family.underwater = material.underwater;
        family.landAllowed = material.layer == ETerrainAtlasLayer::Land;
        family.overlayAllowed = material.layer == ETerrainAtlasLayer::Overlay;
        family.underwaterRole = material.underwater
            ? ETerrainAtlasUnderwaterRole::UnderwaterOnly
            : ETerrainAtlasUnderwaterRole::Surface;
        families.insert(family.id, family);
    }
    for(int slot = 0; slot < atlasSlotCount; ++slot)
    {
        SMprPackedTile packed;
        packed.atlasIndex = quint8(slot / 64);
        packed.tileIndex = quint8(slot % 64);
        bool landFound = false;
        bool overlayFound = false;
        const STerrainTransitionSignature land =
            analysis.landCatalog.gridSignatureForPacked(packed.encode(), &landFound);
        const STerrainTransitionSignature overlay =
            analysis.overlayCatalog.gridSignatureForPacked(packed.encode(), &overlayFound);
        if(landFound == overlayFound)
        {
            if(report) report->add("terrain.atlas_authored_slot", EValidationSeverity::Error,
                "Authored atlas slot must belong to exactly one surface layer",
                QString::number(slot));
            profile.approved = false;
            continue;
        }
        STerrainAtlasProfileSlot definition;
        definition.slot = slot;
        // The authored catalog is the known-good paint-grid baseline. Profiles
        // and the review UI store the corresponding visible preview corners.
        definition.signature = CTerrainTransitionCatalog::mirroredGridY(
            landFound ? land : overlay);
        profile.slotDefinitions.append(definition);
        const qint32 ids[] = {definition.signature.northWest,
                              definition.signature.northEast,
                              definition.signature.southEast,
                              definition.signature.southWest};
        for(qint32 id : ids)
        {
            if(!families.contains(id))
            {
                STerrainAtlasProfileFamily family;
                family.id = id;
                family.name = QStringLiteral("Internal %1 family %2")
                    .arg(landFound ? QStringLiteral("land")
                                   : QStringLiteral("overlay"))
                    .arg(id);
                family.layer = landFound ? ETerrainAtlasLayer::Land
                                         : ETerrainAtlasLayer::Overlay;
                family.landAllowed = landFound;
                family.overlayAllowed = overlayFound;
                family.paintSelectable = false;
                families.insert(id, family);
            }
        }
    }
    profile.families = families.values().toVector();
    profile.normalizeMembership(report);
    return profile;
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
    result.provenance = currentProvenance();
    if(result.mprNamespace.isEmpty() || images.isEmpty() || tileTypes.size() != images.size()
       || usage.size() != images.size())
    {
        if(report) report->add("terrain.atlas_inference_input", EValidationSeverity::Error,
                               "Atlas inference requires one image, MPR tile category, and usage record per slot");
        return result;
    }
    struct SSlotEvidence
    {
        qint32 type{-1};
        ETerrainAtlasLayer layer{ETerrainAtlasLayer::Land};
        bool layerKnown{false};
        bool layerAmbiguous{false};
        bool pure{false};
        SCornerColor corners[4];
    };
    QVector<SSlotEvidence> evidence(images.size());
    // Family creation is deliberately stricter than corner assignment. CIE
    // Lab keeps grass/stone hue differences visible, while local colour spread
    // separates low-saturation textures such as snow and ice.
    const int familyThreshold = 6 * 6 * 100;
    const int pureTileThreshold = 6 * 6 * 100;
    const int ambiguityMargin = 3 * 3 * 100;
    const int cornerNoveltyThreshold = 12 * 12 * 100;
    for(int slot = 0; slot < images.size(); ++slot)
    {
        SSlotEvidence& item = evidence[slot];
        item.type = tileTypes.at(slot);
        const bool land = usage.at(slot).landCount > 0;
        const bool overlay = usage.at(slot).overlayCount > 0;
        item.layerKnown = land != overlay;
        item.layerAmbiguous = land && overlay;
        item.layer = overlay && !land ? ETerrainAtlasLayer::Overlay
                                     : ETerrainAtlasLayer::Land;
        int maximumCornerDistance = 0;
        for(int corner = 0; corner < 4; ++corner)
            item.corners[corner] = cornerColor(images.at(slot), corner);
        for(int a = 0; a < 4; ++a)
            for(int b = a + 1; b < 4; ++b)
                maximumCornerDistance = qMax(maximumCornerDistance,
                    colorDistance(item.corners[a], item.corners[b]));
        item.pure = maximumCornerDistance <= pureTileThreshold;
        if(item.layerAmbiguous && report)
            report->add("terrain.atlas_inference_ambiguous_layer", EValidationSeverity::Warning,
                        "The atlas slot is used on both land and overlay surfaces",
                        QString::number(slot));
    }

    // Families are anchored only by uniform, observed material tiles. Transition
    // corners may join an anchor, but never merge two anchors through a chain of
    // intermediate colours.
    struct SFamilyEvidence
    {
        QVector<SCornerColor> anchors;
        SCornerColor centroid;
    };
    QVector<SFamilyEvidence> familyEvidence;
    const auto appendFamily = [&result, &familyEvidence](qint32 type,
            ETerrainAtlasLayer layer, const SCornerColor& color, bool ambiguous) {
        STerrainAtlasProfileFamily family;
        family.id = result.families.size() + 1;
        family.name = layer == ETerrainAtlasLayer::Land
            ? QStringLiteral("Land material %1").arg(family.id)
            : QStringLiteral("Overlay material %1").arg(family.id);
        family.layer = layer;
        family.landAllowed = layer == ETerrainAtlasLayer::Land;
        family.overlayAllowed = layer == ETerrainAtlasLayer::Overlay;
        family.tileType = type;
        family.ambiguous = ambiguous;
        result.families.append(family);
        SFamilyEvidence detail;
        detail.anchors.append(color);
        detail.centroid = color;
        familyEvidence.append(detail);
        return family.id;
    };
    const auto addAnchor = [&result, &familyEvidence](int index, qint32 type,
            const SCornerColor& color) {
        SFamilyEvidence& detail = familyEvidence[index];
        if(result.families[index].tileType != type)
            result.families[index].tileType = -1;
        detail.anchors.append(color);
        qint64 red = 0, green = 0, blue = 0;
        qint64 redSpread = 0, greenSpread = 0, blueSpread = 0;
        qint64 saturation = 0, edge = 0, roughness = 0;
        for(const SCornerColor& anchor : detail.anchors)
        {
            red += anchor.r; green += anchor.g; blue += anchor.b;
            redSpread += anchor.rs; greenSpread += anchor.gs; blueSpread += anchor.bs;
            saturation += anchor.sat;
            edge += anchor.edgeEnergy;
            roughness += anchor.roughnessEnergy;
        }
        detail.centroid = SCornerColor(int(red / detail.anchors.size()),
                                      int(green / detail.anchors.size()),
                                      int(blue / detail.anchors.size()),
                                      int(redSpread / detail.anchors.size()),
                                      int(greenSpread / detail.anchors.size()),
                                      int(blueSpread / detail.anchors.size()),
                                      int(saturation / detail.anchors.size()),
                                      int(edge / detail.anchors.size()),
                                      int(roughness / detail.anchors.size()));
    };
    const auto matchingFamily = [&result, &familyEvidence, familyThreshold,
                                 ambiguityMargin](ETerrainAtlasLayer layer,
            const SCornerColor& color,
            bool requireCompleteLink, bool* confident) {
        int best = -1, bestDistance = 0, secondDistance = 0;
        for(int index = 0; index < result.families.size(); ++index)
        {
            const STerrainAtlasProfileFamily& family = result.families.at(index);
            if(family.layer != layer)
                continue;
            bool completeLink = true;
            if(requireCompleteLink)
                for(const SCornerColor& anchor : familyEvidence.at(index).anchors)
                    completeLink &= colorDistance(color, anchor) <= familyThreshold;
            if(!completeLink)
                continue;
            const int distance = colorDistance(color, familyEvidence.at(index).centroid);
            if(best < 0 || distance < bestDistance)
            {
                secondDistance = best < 0 ? 0 : bestDistance;
                best = index;
                bestDistance = distance;
            }
            else if(secondDistance == 0 || distance < secondDistance)
                secondDistance = distance;
        }
        if(confident)
            *confident = best >= 0
                && (secondDistance == 0 || secondDistance - bestDistance > ambiguityMargin);
        return best;
    };

    // Known-layer pure tiles establish the material identities.
    for(int slot = 0; slot < evidence.size(); ++slot)
    {
        const SSlotEvidence& item = evidence.at(slot);
        if(!item.pure || !item.layerKnown)
            continue;
        int family = matchingFamily(item.layer, item.corners[0], true, nullptr);
        if(family < 0)
        {
            appendFamily(item.type, item.layer, item.corners[0], false);
            family = result.families.size() - 1;
        }
        else
            addAnchor(family, item.type, item.corners[0]);
    }

    // If a layer has no observed uniform tile, retain a reviewable fallback
    // instead of silently inventing its identity.
    for(int slot = 0; slot < evidence.size(); ++slot)
    {
        const SSlotEvidence& item = evidence.at(slot);
        if(!item.pure || item.layerKnown)
            continue;
        int best = -1, bestDistance = 0;
        for(int layerValue = int(ETerrainAtlasLayer::Land);
            layerValue <= int(ETerrainAtlasLayer::Overlay); ++layerValue)
        {
            bool confident = false;
            const int candidate = matchingFamily(
                ETerrainAtlasLayer(layerValue), item.corners[0], true, &confident);
            if(candidate >= 0)
            {
                const int distance = colorDistance(item.corners[0],
                    familyEvidence.at(candidate).centroid);
                if(best < 0 || distance < bestDistance)
                {
                    best = candidate;
                    bestDistance = distance;
                }
            }
        }
        if(best < 0)
        {
            appendFamily(item.type, item.layer, item.corners[0], true);
            if(report) report->add("terrain.atlas_inference_unanchored_material",
                                   EValidationSeverity::Warning,
                                   "An unused uniform material has no observed layer anchor",
                                   QString::number(slot));
        }
        else
            addAnchor(best, item.type, item.corners[0]);
    }

    for(int slot = 0; slot < evidence.size(); ++slot)
    {
        const SSlotEvidence& item = evidence.at(slot);
        STerrainAtlasProfileSlot proposed;
        proposed.slot = slot;
        ETerrainAtlasLayer inferredLayer = item.layer;
        bool inferredLayerKnown = item.layerKnown;
        if(!inferredLayerKnown && !item.layerAmbiguous)
        {
            qint64 layerScores[2] = {0, 0};
            bool layerAvailable[2] = {false, false};
            for(int layerValue = 0; layerValue < 2; ++layerValue)
                for(int corner = 0; corner < 4; ++corner)
                {
                    int best = -1;
                    for(int family = 0; family < result.families.size(); ++family)
                    {
                        if(int(result.families.at(family).layer) != layerValue) continue;
                        const int distance = colorDistance(item.corners[corner],
                            familyEvidence.at(family).centroid);
                        if(best < 0 || distance < best) best = distance;
                    }
                    if(best >= 0)
                    {
                        layerAvailable[layerValue] = true;
                        layerScores[layerValue] += best;
                    }
                }
            if(layerAvailable[0] != layerAvailable[1])
            {
                inferredLayer = layerAvailable[1] ? ETerrainAtlasLayer::Overlay
                                                  : ETerrainAtlasLayer::Land;
                inferredLayerKnown = true;
            }
            else if(layerAvailable[0] && layerAvailable[1]
                    && std::abs(layerScores[0] - layerScores[1]) > ambiguityMargin * 4)
            {
                inferredLayer = layerScores[1] < layerScores[0]
                    ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
                inferredLayerKnown = true;
            }
        }
        proposed.ambiguous = item.layerAmbiguous || !inferredLayerKnown;
        // CTileForm supplies renderer-oriented tile images. Profiles retain
        // that order so review labels, previews, JSON, diagnostics, and the
        // authored Zone 1 template all use one corner coordinate space.
        qint32* signatureCorners[] = {&proposed.signature.northWest,
                                     &proposed.signature.northEast,
                                     &proposed.signature.southEast,
                                     &proposed.signature.southWest};
        for(int corner = 0; corner < 4; ++corner)
        {
            int best = -1, bestDistance = 0, secondDistance = 0;
            for(int family = 0; family < result.families.size(); ++family)
            {
                const STerrainAtlasProfileFamily& candidate = result.families.at(family);
                if(inferredLayerKnown && candidate.layer != inferredLayer)
                    continue;
                const int distance = colorDistance(item.corners[corner],
                    familyEvidence.at(family).centroid);
                if(best < 0 || distance < bestDistance)
                {
                    secondDistance = best < 0 ? 0 : bestDistance;
                    best = family;
                    bestDistance = distance;
                }
                else if(secondDistance == 0 || distance < secondDistance)
                    secondDistance = distance;
            }
            if(best < 0)
            {
                appendFamily(item.type, inferredLayer, item.corners[corner], true);
                best = result.families.size() - 1;
            }
            else if(inferredLayerKnown && bestDistance > cornerNoveltyThreshold)
            {
                appendFamily(item.type, inferredLayer, item.corners[corner], false);
                best = result.families.size() - 1;
                bestDistance = 0;
                secondDistance = 0;
                if(report)
                    report->add("terrain.atlas_inference_corner_only_material",
                        EValidationSeverity::Warning,
                        "A visually distinct corner material has no uniform four-corner tile; it was preserved as its own palette family",
                        QString::number(slot));
            }
            const bool confident = best >= 0
                && (secondDistance == 0 || secondDistance - bestDistance > ambiguityMargin);
            proposed.ambiguous |= !confident;
            *signatureCorners[corner] = result.families.at(best).id;
        }
        if(item.pure && !item.layerKnown)
        {
            const qint32 familyId = proposed.signature.northWest;
            for(const STerrainAtlasProfileFamily& family : result.families)
                if(family.id == familyId)
                    proposed.ambiguous |= family.ambiguous;
        }
        const qint32 proposedIds[] = {proposed.signature.northWest,
                                      proposed.signature.northEast,
                                      proposed.signature.southEast,
                                      proposed.signature.southWest};
        ETerrainAtlasLayer proposedLayer = ETerrainAtlasLayer::Land;
        bool proposedLayerKnown = false;
        bool sameLayer = true;
        for(qint32 id : proposedIds)
            for(const STerrainAtlasProfileFamily& family : result.families)
                if(family.id == id)
                {
                    if(!proposedLayerKnown)
                    {
                        proposedLayer = family.layer;
                        proposedLayerKnown = true;
                    }
                    else
                        sameLayer &= family.layer == proposedLayer;
                    break;
                }
        proposed.assigned = !proposed.ambiguous && proposedLayerKnown && sameLayer;
        if(!sameLayer)
            proposed.ambiguous = true;
        if(proposed.ambiguous && report)
            report->add("terrain.atlas_inference_ambiguous_slot", EValidationSeverity::Warning,
                        sameLayer
                            ? "This unusual atlas tile was left unassigned"
                            : "This atlas tile matched both ground and overlay materials and was left unassigned",
                        QString::number(slot));
        result.slotDefinitions.append(proposed);
    }

    CValidationReport membershipReport;
    if(!result.normalizeMembership(&membershipReport) && report)
        for(const SValidationFinding& finding : membershipReport.findings())
            report->add(finding.code, finding.severity, finding.message, finding.location);
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
        family.underwaterRole = family.underwater
            ? ETerrainAtlasUnderwaterRole::UnderwaterOnly
            : (belowOverlay > 0 ? ETerrainAtlasUnderwaterRole::Mixed
                                : ETerrainAtlasUnderwaterRole::Surface);
        if(family.layer == ETerrainAtlasLayer::Land && belowOverlay > 0 && belowOverlay < landUses)
        {
            family.ambiguous = true;
            if(report) report->add("terrain.atlas_inference_ambiguous_underwater", EValidationSeverity::Warning,
                                   "A proposed land family is used both below and outside overlays",
                                   family.name);
        }
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

#include "terrain_edit_model.h"

#include <QCryptographicHash>
#include <QSet>
#include <QtEndian>

#include <cmath>
#include <limits>

#include "endurance_document.h"
#include "mpr_document.h"

namespace {
const int kSectorVertices = 33;
const int kSectorStride = 32;
const int kVertexSize = 8;
const int kLandVertexOffset = 5;
const int kSectorTiles = 16;
const int kSectorTileCount = kSectorTiles * kSectorTiles;
const int kTileSize = 2;
const quint32 kSectorSignature = 0xCF4BF774;

quint16 readHeight(const QByteArray& payload, int localX, int localY)
{
    const int offset = kLandVertexOffset + (localY * kSectorVertices + localX) * kVertexSize + 2;
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(payload.constData() + offset));
}

void writeHeight(QByteArray& payload, int localX, int localY, quint16 height)
{
    const int offset = kLandVertexOffset + (localY * kSectorVertices + localX) * kVertexSize + 2;
    uchar encoded[2];
    qToLittleEndian<quint16>(height, encoded);
    payload[offset] = char(encoded[0]);
    payload[offset + 1] = char(encoded[1]);
}

qint8 readOffset(const QByteArray& payload, int localX, int localY, int component)
{
    const int offset = kLandVertexOffset + (localY * kSectorVertices + localX) * kVertexSize
        + component;
    return qint8(payload.at(offset));
}

void writePackedNormal(QByteArray& payload, int localX, int localY, quint32 packed)
{
    const int offset = kLandVertexOffset + (localY * kSectorVertices + localX) * kVertexSize + 4;
    uchar encoded[4];
    qToLittleEndian<quint32>(packed, encoded);
    for(int byte = 0; byte < 4; ++byte)
        payload[offset + byte] = char(encoded[byte]);
}

quint32 readPackedNormal(const QByteArray& payload, int localX, int localY)
{
    const int offset = kLandVertexOffset + (localY * kSectorVertices + localX) * kVertexSize + 4;
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(payload.constData() + offset));
}

quint16 readPackedTile(const QByteArray& payload, quint64 baseOffset, int localTile)
{
    const quint64 offset = baseOffset + quint64(localTile) * kTileSize;
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(payload.constData() + offset));
}

void writePackedTile(QByteArray& payload, quint64 baseOffset, int localTile, quint16 packed)
{
    const quint64 offset = baseOffset + quint64(localTile) * kTileSize;
    uchar encoded[2];
    qToLittleEndian<quint16>(packed, encoded);
    payload[int(offset)] = char(encoded[0]);
    payload[int(offset) + 1] = char(encoded[1]);
}

qint16 readOverlayMaterial(const QByteArray& payload, quint64 baseOffset, int localTile)
{
    return qint16(readPackedTile(payload, baseOffset, localTile));
}

struct SVector3
{
    SVector3(double valueX = 0.0, double valueY = 0.0, double valueZ = 0.0)
        : x(valueX), y(valueY), z(valueZ) {}
    double x;
    double y;
    double z;
};

SVector3 subtract(const SVector3& left, const SVector3& right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

SVector3 cross(const SVector3& left, const SVector3& right)
{
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

bool normalize(SVector3* value)
{
    const double length = std::sqrt(value->x * value->x + value->y * value->y
                                    + value->z * value->z);
    if(length <= 0.000000001)
        return false;
    value->x /= length;
    value->y /= length;
    value->z /= length;
    return true;
}

quint16 clampedHeight(float value, bool* wasClamped = nullptr)
{
    if(wasClamped)
        *wasClamped = value < 0.0f || value > 65535.0f;
    if(value <= 0.0f)
        return 0;
    if(value >= 65535.0f)
        return 65535;
    return quint16(std::lround(value));
}
}

STerrainTransitionSignature CTerrainTransitionCatalog::rotated(
    const STerrainTransitionSignature& signature, int clockwiseQuarterTurns)
{
    STerrainTransitionSignature result = signature;
    int turns = clockwiseQuarterTurns % 4;
    if(turns < 0)
        turns += 4;
    while(turns-- > 0)
    {
        const STerrainTransitionSignature before = result;
        result.northWest = before.southWest;
        result.northEast = before.northWest;
        result.southEast = before.northEast;
        result.southWest = before.southEast;
    }
    return result;
}

bool CTerrainTransitionCatalog::addVariant(const STerrainTransitionVariant& variant,
                                           CValidationReport* report)
{
    if(variant.distributionWeight <= 0)
    {
        if(report)
            report->add("terrain.transition_weight", EValidationSeverity::Error,
                        "Transition variants require a positive distribution weight");
        return false;
    }
    if(m_packedLookup.contains(variant.packedTile))
    {
        if(report)
            report->add("terrain.transition_duplicate", EValidationSeverity::Error,
                        "Transition catalog assigns the same packed tile more than once");
        return false;
    }
    const int index = m_variants.size();
    m_variants.append(variant);
    m_packedLookup.insert(variant.packedTile, index);
    m_signatureLookup[signatureKey(variant.signature)].append(index);
    m_materialIds.insert(variant.signature.northWest);
    m_materialIds.insert(variant.signature.northEast);
    m_materialIds.insert(variant.signature.southEast);
    m_materialIds.insert(variant.signature.southWest);
    return true;
}

QByteArray CTerrainTransitionCatalog::signatureKey(
    const STerrainTransitionSignature& signature)
{
    QByteArray key(16, '\0');
    qToLittleEndian<qint32>(signature.northWest,
        reinterpret_cast<uchar*>(key.data()));
    qToLittleEndian<qint32>(signature.northEast,
        reinterpret_cast<uchar*>(key.data() + 4));
    qToLittleEndian<qint32>(signature.southEast,
        reinterpret_cast<uchar*>(key.data() + 8));
    qToLittleEndian<qint32>(signature.southWest,
        reinterpret_cast<uchar*>(key.data() + 12));
    return key;
}

quint16 CTerrainTransitionCatalog::select(
    const STerrainTransitionSignature& signature, int globalX, int globalY,
    quint32 distributionSeed, bool* found) const
{
    const QVector<int> candidates = m_signatureLookup.value(signatureKey(signature));
    quint64 totalWeight = 0;
    for(int index : candidates)
        totalWeight += quint64(m_variants.at(index).distributionWeight);
    if(totalWeight == 0)
    {
        if(found)
            *found = false;
        return 0;
    }

    quint32 hash = 2166136261u;
    const auto mix = [&hash](quint32 value) {
        for(int byte = 0; byte < 4; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 0xFFu;
            hash *= 16777619u;
        }
    };
    mix(distributionSeed);
    mix(quint32(globalX));
    mix(quint32(globalY));
    mix(quint32(signature.northWest));
    mix(quint32(signature.northEast));
    mix(quint32(signature.southEast));
    mix(quint32(signature.southWest));
    quint64 selection = quint64(hash) % totalWeight;
    for(int index : candidates)
    {
        const STerrainTransitionVariant& variant = m_variants.at(index);
        if(selection < quint64(variant.distributionWeight))
        {
            if(found)
                *found = true;
            return variant.packedTile;
        }
        selection -= quint64(variant.distributionWeight);
    }
    if(found)
        *found = false;
    return 0;
}

quint16 CTerrainTransitionCatalog::selectForPaint(
    const STerrainTransitionSignature& signature, int globalX, int globalY,
    quint32 distributionSeed, bool* found) const
{
    bool selected = false;
    quint16 packed = select(signature, globalX, globalY, distributionSeed, &selected);
    const bool horizontalHalf = signature.northWest == signature.southWest
        && signature.northEast == signature.southEast
        && signature.northWest != signature.northEast;
    const bool verticalHalf = signature.northWest == signature.northEast
        && signature.southWest == signature.southEast
        && signature.northWest != signature.southWest;
    if(selected && m_swapPaintHalfTurns && (horizontalHalf || verticalHalf))
    {
        SMprPackedTile tile = SMprPackedTile::decode(packed);
        if(tile.rotation == 1 || tile.rotation == 3)
        {
            tile.rotation = quint8(4 - tile.rotation);
            packed = tile.encode();
        }
    }
    if(found)
        *found = selected;
    return packed;
}

STerrainTransitionSignature CTerrainTransitionCatalog::signatureForPacked(
    quint16 packedTile, bool* found) const
{
    const auto existing = m_packedLookup.constFind(packedTile);
    if(existing != m_packedLookup.constEnd())
    {
        if(found)
            *found = true;
        return m_variants.at(existing.value()).signature;
    }
    if(found)
        *found = false;
    return STerrainTransitionSignature();
}

bool CTerrainTransitionCatalog::hasMaterial(qint32 materialId) const
{
    return m_materialIds.contains(materialId);
}

QVector<qint32> CTerrainTransitionCatalog::materialIds() const
{
    QVector<qint32> result = m_materialIds.values().toVector();
    std::sort(result.begin(), result.end());
    return result;
}

QVector<qint32> CTerrainTransitionCatalog::shortestMaterialPath(
    qint32 fromMaterial, qint32 toMaterial) const
{
    if(!hasMaterial(fromMaterial) || !hasMaterial(toMaterial))
        return {};
    if(fromMaterial == toMaterial)
        return {fromMaterial};

    const QVector<qint32> ids = materialIds();
    QMap<qint32, QVector<qint32>> neighbors;
    for(qint32 left : ids)
    {
        for(qint32 right : ids)
        {
            if(left == right)
                continue;
            bool completeBoundary = true;
            for(int mask = 0; mask < 16; ++mask)
            {
                // Alternating diagonal corners are not generated by a solid
                // brush boundary and many stock atlases intentionally omit them.
                if(mask == 5 || mask == 10)
                    continue;
                STerrainTransitionSignature signature;
                signature.northWest = (mask & 1) ? left : right;
                signature.northEast = (mask & 2) ? left : right;
                signature.southEast = (mask & 4) ? left : right;
                signature.southWest = (mask & 8) ? left : right;
                if(!m_signatureLookup.contains(signatureKey(signature)))
                {
                    completeBoundary = false;
                    break;
                }
            }
            if(completeBoundary)
                neighbors[left].append(right);
        }
    }

    QVector<QVector<qint32>> queue;
    queue.append(QVector<qint32>({fromMaterial}));
    QSet<qint32> visited;
    visited.insert(fromMaterial);
    for(int head = 0; head < queue.size(); ++head)
    {
        const QVector<qint32> path = queue.at(head);
        const qint32 tail = path.back();
        const QVector<qint32> next = neighbors.value(tail);
        for(qint32 candidate : next)
        {
            if(visited.contains(candidate))
                continue;
            QVector<qint32> extended = path;
            extended.append(candidate);
            if(candidate == toMaterial)
                return extended;
            visited.insert(candidate);
            queue.append(extended);
        }
    }
    return {};
}

bool CTerrainEditModel::load(const CMprDocument& document, CValidationReport* report)
{
    if(!document.mapInfo().valid)
    {
        if(report)
            report->add("terrain.mp", EValidationSeverity::Error,
                        "A parsed MP namespace is required before terrain editing");
        return false;
    }
    QVector<STerrainSectorSource> sources;
    for(const SMprSectorInfo& sector : document.sectors())
    {
        if(sector.type != 0 && sector.type != 3)
            continue;
        const SMprArchiveMember& member = document.members().at(sector.memberIndex);
        STerrainSectorSource source;
        source.memberName = member.name;
        source.expectedPayloadSha256 = member.sha256;
        source.sectorX = sector.sectorX;
        source.sectorY = sector.sectorY;
        source.type = sector.type;
        source.landTileOffset = sector.landTileOffset;
        source.overlayTileOffset = sector.overlayTileOffset;
        source.overlayMaterialOffset = sector.overlayMaterialOffset;
        source.payload = member.payload;
        sources.append(source);
    }
    return loadSources(int(document.mapInfo().sectorCountX),
                       int(document.mapInfo().sectorCountY), document.mapInfo().maxZ,
                       sources, report, int(document.mapInfo().textureCount),
                       document.mapInfo().tileTypes);
}

bool CTerrainEditModel::loadSources(int sectorCountX, int sectorCountY, float maxZ,
                                    const QVector<STerrainSectorSource>& sources,
                                    CValidationReport* report, int textureCount,
                                    const QVector<qint32>& tileTypes)
{
    m_sectorCountX = 0;
    m_sectorCountY = 0;
    m_vertexCountX = 0;
    m_vertexCountY = 0;
    m_maxZ = 0.0f;
    m_textureCount = 0;
    m_tileTypes.clear();
    m_heights.clear();
    m_originalHeights.clear();
    m_xOffsets.clear();
    m_yOffsets.clear();
    m_refs.clear();
    m_conflictedVertices.clear();
    m_sectors.clear();
    m_sectorLookup.clear();
    if(sectorCountX <= 0 || sectorCountY <= 0 || !std::isfinite(maxZ) || maxZ <= 0.0f)
    {
        if(report)
            report->add("terrain.dimensions", EValidationSeverity::Error,
                        "Terrain sector dimensions and maxZ must be positive");
        return false;
    }

    const int minimumPayload = kLandVertexOffset
        + kSectorVertices * kSectorVertices * kVertexSize;
    for(const STerrainSectorSource& source : sources)
    {
        if(source.sectorX < 0 || source.sectorX >= sectorCountX ||
           source.sectorY < 0 || source.sectorY >= sectorCountY ||
           source.payload.size() < minimumPayload ||
           qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(source.payload.constData()))
               != kSectorSignature)
        {
            if(report)
                report->add("terrain.sector", EValidationSeverity::Error,
                            "Terrain sector source is outside the map or has a truncated vertex lattice",
                            source.memberName);
            return false;
        }
        if(textureCount > 0)
        {
            const quint64 landEnd = source.landTileOffset
                + quint64(kSectorTileCount) * kTileSize;
            const quint64 overlayEnd = source.overlayMaterialOffset
                + quint64(kSectorTileCount) * kTileSize;
            if(source.landTileOffset == 0 || landEnd > quint64(source.payload.size())
               || (source.type == 3 && (source.overlayTileOffset == 0
                   || source.overlayMaterialOffset == 0
                   || overlayEnd > quint64(source.payload.size()))))
            {
                if(report)
                    report->add("terrain.tile_arrays", EValidationSeverity::Error,
                                "Terrain sector has truncated or missing tile arrays",
                                source.memberName);
                return false;
            }
        }
        const QPair<int, int> key(source.sectorX, source.sectorY);
        if(m_sectorLookup.contains(key))
        {
            if(report)
                report->add("terrain.sector_duplicate", EValidationSeverity::Error,
                            "Terrain sector coordinates occur more than once", source.memberName);
            return false;
        }
        SSector sector;
        sector.source = source;
        sector.currentPayload = source.payload;
        m_sectorLookup.insert(key, m_sectors.size());
        m_sectors.append(sector);
    }
    if(m_sectors.size() != sectorCountX * sectorCountY)
    {
        if(report)
            report->add("terrain.sector_count", EValidationSeverity::Error,
                        QString("Expected %1 sectors but received %2")
                            .arg(sectorCountX * sectorCountY).arg(m_sectors.size()));
        return false;
    }

    m_sectorCountX = sectorCountX;
    m_sectorCountY = sectorCountY;
    m_maxZ = maxZ;
    m_textureCount = qMax(0, textureCount);
    m_tileTypes = tileTypes;
    m_vertexCountX = sectorCountX * kSectorStride + 1;
    m_vertexCountY = sectorCountY * kSectorStride + 1;
    const int globalCount = m_vertexCountX * m_vertexCountY;
    m_heights.fill(0, globalCount);
    m_xOffsets.fill(0, globalCount);
    m_yOffsets.fill(0, globalCount);
    m_refs.resize(globalCount);
    m_conflictedVertices.fill(false, globalCount);
    QVector<bool> initialized(globalCount, false);

    for(int sectorY = 0; sectorY < sectorCountY; ++sectorY)
    {
        for(int sectorX = 0; sectorX < sectorCountX; ++sectorX)
        {
            const int sectorIndex = m_sectorLookup.value(QPair<int, int>(sectorX, sectorY), -1);
            if(sectorIndex < 0)
                return false;
            const QByteArray& payload = m_sectors.at(sectorIndex).currentPayload;
            for(int localY = 0; localY < kSectorVertices; ++localY)
            {
                for(int localX = 0; localX < kSectorVertices; ++localX)
                {
                    const int globalX = sectorX * kSectorStride + localX;
                    const int globalY = sectorY * kSectorStride + localY;
                    const int index = globalIndex(globalX, globalY);
                    const quint16 value = readHeight(payload, localX, localY);
                    const qint8 xOffset = readOffset(payload, localX, localY, 0);
                    const qint8 yOffset = readOffset(payload, localX, localY, 1);
                    if(!initialized.at(index))
                    {
                        initialized[index] = true;
                        m_heights[index] = value;
                        m_xOffsets[index] = xOffset;
                        m_yOffsets[index] = yOffset;
                    }
                    else if(m_heights.at(index) != value || m_xOffsets.at(index) != xOffset
                            || m_yOffsets.at(index) != yOffset)
                    {
                        m_conflictedVertices[index] = true;
                        if(report)
                            report->add("terrain.seam_conflict", EValidationSeverity::Warning,
                                QString("Shared vertex %1,%2 has conflicting geometry across sectors; editing it is disabled")
                                    .arg(globalX).arg(globalY), m_sectors.at(sectorIndex).source.memberName);
                    }
                    SVertexRef ref;
                    ref.sectorIndex = sectorIndex;
                    ref.localX = localX;
                    ref.localY = localY;
                    m_refs[index].append(ref);
                }
            }
        }
    }
    m_originalHeights = m_heights;
    return true;
}

int CTerrainEditModel::globalIndex(int x, int y) const
{
    return y * m_vertexCountX + x;
}

quint16 CTerrainEditModel::height(int globalX, int globalY) const
{
    if(globalX < 0 || globalX >= m_vertexCountX ||
       globalY < 0 || globalY >= m_vertexCountY)
        return 0;
    return m_heights.at(globalIndex(globalX, globalY));
}

bool CTerrainEditModel::isDirty() const
{
    for(const SSector& sector : m_sectors)
        if(sector.dirty)
            return true;
    return false;
}

bool CTerrainEditModel::tileLocation(int globalX, int globalY, int* sectorIndex,
                                     int* localTile, CValidationReport* report) const
{
    if(globalX < 0 || globalX >= tileCountX() ||
       globalY < 0 || globalY >= tileCountY())
    {
        if(report)
            report->add("terrain.tile_bounds", EValidationSeverity::Error,
                        "Tile command references a cell outside the terrain",
                        QString("%1,%2").arg(globalX).arg(globalY));
        return false;
    }
    const int sectorX = globalX / kSectorTiles;
    const int sectorY = globalY / kSectorTiles;
    const int index = m_sectorLookup.value(QPair<int, int>(sectorX, sectorY), -1);
    if(index < 0)
        return false;
    if(sectorIndex)
        *sectorIndex = index;
    if(localTile)
        *localTile = (globalY % kSectorTiles) * kSectorTiles
            + (globalX % kSectorTiles);
    return true;
}

bool CTerrainEditModel::overlayTileIsActive(const SSector& sector, int localTile) const
{
    return sector.source.type == 3 && sector.source.overlayMaterialOffset > 0
        && readOverlayMaterial(sector.currentPayload,
                               sector.source.overlayMaterialOffset, localTile) != -1;
}

quint16 CTerrainEditModel::packedTile(int globalX, int globalY,
                                      ETerrainTileLayer layer, bool* available) const
{
    int sectorIndex = -1;
    int localTile = -1;
    if(!tileLocation(globalX, globalY, &sectorIndex, &localTile))
    {
        if(available)
            *available = false;
        return 0;
    }
    const SSector& sector = m_sectors.at(sectorIndex);
    const bool present = layer == ETerrainTileLayer::Land
        ? sector.source.landTileOffset > 0 : overlayTileIsActive(sector, localTile);
    if(available)
        *available = present;
    if(!present)
        return 0;
    const quint64 offset = layer == ETerrainTileLayer::Land
        ? sector.source.landTileOffset : sector.source.overlayTileOffset;
    return readPackedTile(sector.currentPayload, offset, localTile);
}

qint32 CTerrainEditModel::tileType(quint16 packed, bool* found) const
{
    const int slot = SMprPackedTile::decode(packed).atlasSlot();
    const bool present = slot >= 0 && slot < m_tileTypes.size();
    if(found)
        *found = present;
    return present ? m_tileTypes.at(slot) : 0;
}

bool CTerrainEditModel::hasSeamConflicts() const
{
    for(bool conflicted : m_conflictedVertices)
        if(conflicted)
            return true;
    return false;
}

float CTerrainEditModel::brushWeight(float distance, float radius,
                                     ETerrainBrushFalloff falloff) const
{
    if(distance > radius || radius < 0.0f)
        return 0.0f;
    if(falloff == ETerrainBrushFalloff::Constant || radius == 0.0f)
        return 1.0f;
    const float linear = qBound(0.0f, 1.0f - distance / radius, 1.0f);
    if(falloff == ETerrainBrushFalloff::Linear)
        return linear;
    return linear * linear * (3.0f - 2.0f * linear);
}

STerrainHeightCommand CTerrainEditModel::makeRaiseLower(
    const STerrainBrush& brush, float rawHeightDelta) const
{
    STerrainHeightCommand command;
    command.description = rawHeightDelta >= 0.0f ? "Raise terrain" : "Lower terrain";
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const float dx = float(x) - brush.centerX;
            const float dy = float(y) - brush.centerY;
            const float weight = brushWeight(std::sqrt(dx * dx + dy * dy),
                                             brush.radius, brush.falloff);
            if(weight <= 0.0f)
                continue;
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = height(x, y);
            delta.after = clampedHeight(float(delta.before) + rawHeightDelta * weight,
                                        &delta.clamped);
            if(delta.before != delta.after)
                command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeBlendedCommand(
    const QString& description, const STerrainBrush& brush,
    const QVector<float>& targets, float strength) const
{
    STerrainHeightCommand command;
    command.description = description;
    strength = qBound(0.0f, strength, 1.0f);
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const float dx = float(x) - brush.centerX;
            const float dy = float(y) - brush.centerY;
            const float weight = brushWeight(std::sqrt(dx * dx + dy * dy),
                                             brush.radius, brush.falloff) * strength;
            if(weight <= 0.0f)
                continue;
            const int index = globalIndex(x, y);
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = m_heights.at(index);
            delta.after = clampedHeight(float(delta.before)
                + (targets.at(index) - float(delta.before)) * weight, &delta.clamped);
            if(delta.before != delta.after)
                command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeFlatten(
    const STerrainBrush& brush, quint16 targetHeight, float strength) const
{
    QVector<float> targets(m_heights.size(), float(targetHeight));
    return makeBlendedCommand("Flatten terrain", brush, targets, strength);
}

STerrainHeightCommand CTerrainEditModel::makeSmooth(
    const STerrainBrush& brush, float strength) const
{
    QVector<float> targets(m_heights.size(), 0.0f);
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            quint64 total = 0;
            int count = 0;
            for(int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for(int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    const int sampleX = x + offsetX;
                    const int sampleY = y + offsetY;
                    if(sampleX < 0 || sampleX >= m_vertexCountX ||
                       sampleY < 0 || sampleY >= m_vertexCountY)
                        continue;
                    total += height(sampleX, sampleY);
                    ++count;
                }
            }
            targets[globalIndex(x, y)] = float(total) / float(count);
        }
    }
    return makeBlendedCommand("Smooth terrain", brush, targets, strength);
}

STerrainHeightCommand CTerrainEditModel::makeRamp(
    float startX, float startY, quint16 startHeight,
    float endX, float endY, quint16 endHeight,
    float radius, float strength, ETerrainBrushFalloff falloff) const
{
    STerrainHeightCommand command;
    command.description = "Ramp terrain";
    const float axisX = endX - startX;
    const float axisY = endY - startY;
    const float axisLengthSquared = axisX * axisX + axisY * axisY;
    if(axisLengthSquared <= 0.0f || radius < 0.0f)
        return command;
    strength = qBound(0.0f, strength, 1.0f);
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const float relativeX = float(x) - startX;
            const float relativeY = float(y) - startY;
            const float t = qBound(0.0f,
                (relativeX * axisX + relativeY * axisY) / axisLengthSquared, 1.0f);
            const float nearestX = startX + t * axisX;
            const float nearestY = startY + t * axisY;
            const float dx = float(x) - nearestX;
            const float dy = float(y) - nearestY;
            const float weight = brushWeight(std::sqrt(dx * dx + dy * dy), radius, falloff)
                * strength;
            if(weight <= 0.0f)
                continue;
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = height(x, y);
            const float target = float(startHeight)
                + (float(endHeight) - float(startHeight)) * t;
            delta.after = clampedHeight(float(delta.before)
                + (target - float(delta.before)) * weight, &delta.clamped);
            if(delta.before != delta.after)
                command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeHeightmapImport(
    const STerrainHeightmapGrid& grid,
    const STerrainHeightmapImportSettings& settings,
    CValidationReport* report) const
{
    STerrainHeightCommand command;
    command.description = QStringLiteral("Import heightmap");
    const qint64 expectedSamples = qint64(grid.width) * qint64(grid.height);
    if(grid.width <= 0 || grid.height <= 0 || expectedSamples != grid.samples.size())
    {
        if(report)
            report->add("terrain.heightmap_dimensions", EValidationSeverity::Error,
                        "Heightmap dimensions do not match its normalized sample grid");
        return command;
    }
    if(!std::isfinite(settings.minimumRawHeight)
       || !std::isfinite(settings.maximumRawHeight)
       || !std::isfinite(settings.strength)
       || settings.strength < 0.0f || settings.strength > 1.0f)
    {
        if(report)
            report->add("terrain.heightmap_settings", EValidationSeverity::Error,
                        "Heightmap range and strength must be finite; strength must be between zero and one");
        return command;
    }
    for(float sample : grid.samples)
    {
        if(!std::isfinite(sample) || sample < 0.0f || sample > 1.0f)
        {
            if(report)
                report->add("terrain.heightmap_sample", EValidationSeverity::Error,
                            "Heightmap samples must be finite normalized values from zero to one");
            return command;
        }
    }
    if(settings.strength == 0.0f)
        return command;

    const auto targetHeight = [&settings](float sample) {
        return settings.minimumRawHeight
            + (settings.maximumRawHeight - settings.minimumRawHeight) * sample;
    };
    const auto appendDelta = [this, &command, &settings, &targetHeight](
        int x, int y, float sample) {
        if(x < 0 || x >= m_vertexCountX || y < 0 || y >= m_vertexCountY)
            return;
        STerrainHeightDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.before = height(x, y);
        const float blended = float(delta.before)
            + (targetHeight(sample) - float(delta.before)) * settings.strength;
        delta.after = clampedHeight(blended, &delta.clamped);
        if(delta.before != delta.after)
            command.deltas.append(delta);
    };

    if(settings.placement == ETerrainHeightmapPlacement::OneSamplePerVertex)
    {
        for(int sourceY = 0; sourceY < grid.height; ++sourceY)
            for(int sourceX = 0; sourceX < grid.width; ++sourceX)
                appendDelta(settings.destinationX + sourceX,
                            settings.destinationY + sourceY,
                            grid.samples.at(sourceY * grid.width + sourceX));
        return command;
    }

    const auto sampleGrid = [&grid](float sourceX, float sourceY) {
        const int x0 = qBound(0, int(std::floor(sourceX)), grid.width - 1);
        const int y0 = qBound(0, int(std::floor(sourceY)), grid.height - 1);
        const int x1 = qMin(x0 + 1, grid.width - 1);
        const int y1 = qMin(y0 + 1, grid.height - 1);
        const float fractionX = sourceX - float(x0);
        const float fractionY = sourceY - float(y0);
        const float bottom = grid.samples.at(y0 * grid.width + x0) * (1.0f - fractionX)
            + grid.samples.at(y0 * grid.width + x1) * fractionX;
        const float top = grid.samples.at(y1 * grid.width + x0) * (1.0f - fractionX)
            + grid.samples.at(y1 * grid.width + x1) * fractionX;
        return bottom * (1.0f - fractionY) + top * fractionY;
    };
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        const float sourceY = m_vertexCountY > 1
            ? float(y) * float(grid.height - 1) / float(m_vertexCountY - 1) : 0.0f;
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const float sourceX = m_vertexCountX > 1
                ? float(x) * float(grid.width - 1) / float(m_vertexCountX - 1) : 0.0f;
            appendDelta(x, y, sampleGrid(sourceX, sourceY));
        }
    }
    return command;
}

STerrainEditDiagnostics CTerrainEditModel::diagnose(
    const STerrainHeightCommand& command,
    const QVector<STerrainObjectHeightProbe>& objects,
    const STerrainDiagnosticPolicy& policy,
    CValidationReport* report) const
{
    STerrainEditDiagnostics result;
    if(command.isEmpty())
    {
        result.valid = true;
        return result;
    }

    QMap<int, quint16> afterHeights;
    QSet<int> affectedSectors;
    bool firstDelta = true;
    for(const STerrainHeightDelta& delta : command.deltas)
    {
        if(delta.globalX < 0 || delta.globalX >= m_vertexCountX ||
           delta.globalY < 0 || delta.globalY >= m_vertexCountY)
        {
            if(report)
                report->add("terrain.diagnostic_bounds", EValidationSeverity::Error,
                            "Height diagnostic references a vertex outside the terrain");
            return result;
        }
        const int index = globalIndex(delta.globalX, delta.globalY);
        if(afterHeights.contains(index))
        {
            if(report)
                report->add("terrain.diagnostic_duplicate", EValidationSeverity::Error,
                            "Height diagnostic contains the same vertex more than once",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return result;
        }
        if(m_conflictedVertices.at(index) || m_heights.at(index) != delta.before)
        {
            if(report)
                report->add("terrain.diagnostic_provenance", EValidationSeverity::Error,
                            "Height diagnostic no longer matches editable canonical terrain",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return result;
        }
        afterHeights.insert(index, delta.after);
        for(const SVertexRef& ref : m_refs.at(index))
            affectedSectors.insert(ref.sectorIndex);
        if(m_refs.at(index).size() > 1)
            ++result.sharedSeamVertexCount;
        const float worldDelta = (float(delta.after) - float(delta.before))
            * m_maxZ / 65535.0f;
        if(firstDelta)
        {
            result.minimumBefore = result.maximumBefore = delta.before;
            result.minimumAfter = result.maximumAfter = delta.after;
            result.minimumWorldDelta = result.maximumWorldDelta = worldDelta;
            firstDelta = false;
        }
        else
        {
            result.minimumBefore = qMin(result.minimumBefore, delta.before);
            result.maximumBefore = qMax(result.maximumBefore, delta.before);
            result.minimumAfter = qMin(result.minimumAfter, delta.after);
            result.maximumAfter = qMax(result.maximumAfter, delta.after);
            result.minimumWorldDelta = qMin(result.minimumWorldDelta, worldDelta);
            result.maximumWorldDelta = qMax(result.maximumWorldDelta, worldDelta);
        }
    }

    result.valid = true;
    result.requiresMaxZDecision = command.requiresMaxZDecision();
    result.navigationGraphMayBeStale = true;
    result.changedVertexCount = afterHeights.size();
    result.affectedSectorCount = affectedSectors.size();

    const auto rawHeight = [this, &afterHeights](int index, bool after) {
        return after && afterHeights.contains(index)
            ? afterHeights.value(index) : m_heights.at(index);
    };
    const auto position = [this, &rawHeight](int index, bool after) {
        const int x = index % m_vertexCountX;
        const int y = index / m_vertexCountX;
        return SVector3(double(x) + double(m_xOffsets.at(index)) / 254.0,
                        double(y) + double(m_yOffsets.at(index)) / 254.0,
                        double(rawHeight(index, after)) * double(m_maxZ) / 65535.0);
    };
    QSet<quint64> measuredEdges;
    for(auto changed = afterHeights.cbegin(); changed != afterHeights.cend(); ++changed)
    {
        const int center = changed.key();
        const int centerX = center % m_vertexCountX;
        const int centerY = center / m_vertexCountX;
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                if(offsetX == 0 && offsetY == 0)
                    continue;
                const int x = centerX + offsetX;
                const int y = centerY + offsetY;
                if(x < 0 || x >= m_vertexCountX || y < 0 || y >= m_vertexCountY)
                    continue;
                const int neighbor = globalIndex(x, y);
                const quint32 low = quint32(qMin(center, neighbor));
                const quint32 high = quint32(qMax(center, neighbor));
                const quint64 edge = (quint64(low) << 32) | high;
                if(measuredEdges.contains(edge))
                    continue;
                measuredEdges.insert(edge);
                for(int state = 0; state < 2; ++state)
                {
                    const SVector3 first = position(center, state == 1);
                    const SVector3 second = position(neighbor, state == 1);
                    const double horizontal = std::sqrt(
                        (second.x - first.x) * (second.x - first.x)
                        + (second.y - first.y) * (second.y - first.y));
                    const float degrees = horizontal > 0.000000001
                        ? float(std::atan2(std::fabs(second.z - first.z), horizontal)
                                * 57.29577951308232)
                        : 90.0f;
                    if(state == 0)
                        result.maximumSlopeBeforeDegrees = qMax(
                            result.maximumSlopeBeforeDegrees, degrees);
                    else
                        result.maximumSlopeAfterDegrees = qMax(
                            result.maximumSlopeAfterDegrees, degrees);
                }
            }
        }
    }
    result.steepSlopeRisk = policy.steepSlopeDegrees >= 0.0f
        && result.maximumSlopeAfterDegrees > policy.steepSlopeDegrees
        && result.maximumSlopeAfterDegrees > result.maximumSlopeBeforeDegrees + 0.0001f;

    const auto sampledTerrainZ = [this, &rawHeight](float x, float y, bool after,
                                                     float* value) {
        if(x < 0.0f || y < 0.0f || x > float(m_vertexCountX - 1)
           || y > float(m_vertexCountY - 1))
            return false;
        const int x0 = qBound(0, int(std::floor(x)), m_vertexCountX - 1);
        const int y0 = qBound(0, int(std::floor(y)), m_vertexCountY - 1);
        const int x1 = qMin(x0 + 1, m_vertexCountX - 1);
        const int y1 = qMin(y0 + 1, m_vertexCountY - 1);
        const float fractionX = x - float(x0);
        const float fractionY = y - float(y0);
        const float bottom = float(rawHeight(globalIndex(x0, y0), after)) * (1.0f - fractionX)
            + float(rawHeight(globalIndex(x1, y0), after)) * fractionX;
        const float top = float(rawHeight(globalIndex(x0, y1), after)) * (1.0f - fractionX)
            + float(rawHeight(globalIndex(x1, y1), after)) * fractionX;
        *value = (bottom * (1.0f - fractionY) + top * fractionY) * m_maxZ / 65535.0f;
        return true;
    };
    for(const STerrainObjectHeightProbe& object : objects)
    {
        float beforeZ = 0.0f;
        float afterZ = 0.0f;
        if(!sampledTerrainZ(object.positionX, object.positionY, false, &beforeZ)
           || !sampledTerrainZ(object.positionX, object.positionY, true, &afterZ))
            continue;
        const float delta = afterZ - beforeZ;
        if(std::fabs(delta) <= 0.000001f)
            continue;
        STerrainObjectHeightFinding finding;
        finding.identity = object.identity;
        finding.objectZ = object.positionZ;
        finding.terrainBeforeZ = beforeZ;
        finding.terrainAfterZ = afterZ;
        finding.terrainDelta = delta;
        finding.clearanceBefore = object.positionZ - beforeZ;
        finding.clearanceAfter = object.positionZ - afterZ;
        finding.exceedsThreshold = policy.objectVerticalDelta >= 0.0f
            && std::fabs(delta) > policy.objectVerticalDelta;
        result.objectFindings.append(finding);
    }

    if(report && result.requiresMaxZDecision)
        report->add("terrain.maxz_saturation", EValidationSeverity::Warning,
                    "The requested edit exceeded the raw height range; reduce it or make an explicit maxZ decision before applying");
    if(report)
        report->add("terrain.navigation_stale", EValidationSeverity::Warning,
                    "Terrain geometry changed; existing AI navigation data may require regeneration");
    if(report && result.steepSlopeRisk)
        report->add("terrain.slope_risk", EValidationSeverity::Warning,
                    QString("The edit increases local slope to %1 degrees, above the caller-selected %2-degree threshold")
                        .arg(result.maximumSlopeAfterDegrees, 0, 'f', 2)
                        .arg(policy.steepSlopeDegrees, 0, 'f', 2));
    if(report)
    {
        for(const STerrainObjectHeightFinding& finding : result.objectFindings)
        {
            if(finding.exceedsThreshold)
                report->add("terrain.mob_height_risk", EValidationSeverity::Warning,
                            QString("Terrain under the object moves by %1 world units, above the caller-selected %2-unit threshold")
                                .arg(finding.terrainDelta, 0, 'f', 3)
                                .arg(policy.objectVerticalDelta, 0, 'f', 3),
                            finding.identity);
        }
    }
    return result;
}

bool CTerrainEditModel::apply(const STerrainHeightCommand& command, bool forward,
                              CValidationReport* report, bool allowHeightClamping)
{
    if(forward && command.requiresMaxZDecision() && !allowHeightClamping)
    {
        if(report)
            report->add("terrain.maxz_decision", EValidationSeverity::Error,
                        "The command reaches the raw height limit and requires an explicit maxZ/clamping decision");
        return false;
    }
    QSet<int> commandVertices;
    QSet<int> affectedNormals;
    for(const STerrainHeightDelta& delta : command.deltas)
    {
        if(delta.globalX < 0 || delta.globalX >= m_vertexCountX ||
           delta.globalY < 0 || delta.globalY >= m_vertexCountY)
        {
            if(report)
                report->add("terrain.command_bounds", EValidationSeverity::Error,
                            "Height command references a vertex outside the terrain");
            return false;
        }
        const int index = globalIndex(delta.globalX, delta.globalY);
        if(commandVertices.contains(index))
        {
            if(report)
                report->add("terrain.command_duplicate", EValidationSeverity::Error,
                            "Height command references the same vertex more than once",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        commandVertices.insert(index);
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const int x = delta.globalX + offsetX;
                const int y = delta.globalY + offsetY;
                if(x >= 0 && x < m_vertexCountX && y >= 0 && y < m_vertexCountY)
                    affectedNormals.insert(globalIndex(x, y));
            }
        }
        if(m_conflictedVertices.at(index))
        {
            if(report)
                report->add("terrain.command_seam", EValidationSeverity::Error,
                            "Height command touches a pre-existing conflicting sector seam",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        const quint16 expected = forward ? delta.before : delta.after;
        if(m_heights.at(index) != expected)
        {
            if(report)
                report->add("terrain.command_provenance", EValidationSeverity::Error,
                            "Height command no longer matches the canonical lattice",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
    }

    for(int index : affectedNormals)
    {
        const int x = index % m_vertexCountX;
        const int y = index / m_vertexCountX;
        if(!normalNeighborhoodIsEditable(x, y))
        {
            if(report)
                report->add("terrain.normal_seam", EValidationSeverity::Error,
                            "An affected preview normal depends on a pre-existing conflicting seam",
                            QString("%1,%2").arg(x).arg(y));
            return false;
        }
    }

    for(const STerrainHeightDelta& delta : command.deltas)
    {
        const int index = globalIndex(delta.globalX, delta.globalY);
        const quint16 value = forward ? delta.after : delta.before;
        m_heights[index] = value;
        for(const SVertexRef& ref : m_refs.at(index))
        {
            SSector& sector = m_sectors[ref.sectorIndex];
            writeHeight(sector.currentPayload, ref.localX, ref.localY, value);
        }
    }


    // The renderer consumes normals directly from the current SEC payload. Keep
    // them synchronized for live sculpt/import previews instead of waiting for
    // staged serialization. When an undo restores an untouched neighbourhood,
    // restore each source normal byte-for-byte; fixture normals are authoritative
    // and are not silently normalized by the editor.
    for(int index : affectedNormals)
    {
        const int x = index % m_vertexCountX;
        const int y = index / m_vertexCountX;
        bool sourceGeometry = true;
        for(int offsetY = -1; offsetY <= 1 && sourceGeometry; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const int sampleX = x + offsetX;
                const int sampleY = y + offsetY;
                if(sampleX < 0 || sampleX >= m_vertexCountX
                   || sampleY < 0 || sampleY >= m_vertexCountY)
                    continue;
                const int sample = globalIndex(sampleX, sampleY);
                if(m_heights.at(sample) != m_originalHeights.at(sample))
                {
                    sourceGeometry = false;
                    break;
                }
            }
        }
        const quint32 rebuilt = sourceGeometry ? 0u : packedNormal(x, y);
        for(const SVertexRef& ref : m_refs.at(index))
        {
            SSector& sector = m_sectors[ref.sectorIndex];
            const quint32 value = sourceGeometry
                ? readPackedNormal(sector.source.payload, ref.localX, ref.localY)
                : rebuilt;
            writePackedNormal(sector.currentPayload, ref.localX, ref.localY, value);
        }
    }
    for(SSector& sector : m_sectors)
        sector.dirty = sector.currentPayload != sector.source.payload;
    return true;
}

STerrainTileCommand CTerrainEditModel::makeTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    quint8 atlasIndex, quint8 tileIndex, quint8 rotation) const
{
    STerrainTileCommand command;
    command.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint terrain tiles") : QStringLiteral("Paint overlay tiles");
    if(brush.radius < 0.0f || tileIndex >= 64 || rotation >= 4
       || atlasIndex >= m_textureCount)
        return command;
    SMprPackedTile selected;
    selected.atlasIndex = atlasIndex;
    selected.tileIndex = tileIndex;
    selected.rotation = rotation;
    if(selected.atlasSlot() >= m_tileTypes.size())
        return command;
    const quint16 after = selected.encode();
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(tileCountX() - 1,
                              int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(tileCountY() - 1,
                              int(std::floor(brush.centerY + brush.radius)));
    for(int y = minimumY; y <= maximumY; ++y)
    {
        for(int x = minimumX; x <= maximumX; ++x)
        {
            const float dx = std::fabs(float(x) - brush.centerX);
            const float dy = std::fabs(float(y) - brush.centerY);
            const bool inside = brush.shape == ETerrainTileBrushShape::Square
                ? qMax(dx, dy) <= brush.radius
                : std::sqrt(dx * dx + dy * dy) <= brush.radius;
            if(!inside)
                continue;
            bool available = false;
            const quint16 before = packedTile(x, y, layer, &available);
            if(!available || before == after)
                continue;
            STerrainTileDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.layer = layer;
            delta.before = before;
            delta.after = after;
            command.deltas.append(delta);
        }
    }
    return command;
}

STerrainTileCommand CTerrainEditModel::makeSemanticTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, CValidationReport* report) const
{
    STerrainTileCommand command;
    command.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint terrain material")
        : QStringLiteral("Paint overlay material");
    if(brush.radius < 0.0f || !catalog.hasMaterial(materialId))
    {
        if(report)
            report->add("terrain.semantic_type", EValidationSeverity::Error,
                        "The selected material is absent from the registered namespace template");
        return command;
    }

    const int width = tileCountX();
    const int height = tileCountY();
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(width - 1, int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(height - 1, int(std::floor(brush.centerY + brush.radius)));
    QSet<int> painted;
    for(int y = minimumY; y <= maximumY; ++y)
    {
        for(int x = minimumX; x <= maximumX; ++x)
        {
            const float dx = std::fabs(float(x) - brush.centerX);
            const float dy = std::fabs(float(y) - brush.centerY);
            const bool inside = brush.shape == ETerrainTileBrushShape::Square
                ? qMax(dx, dy) <= brush.radius
                : std::sqrt(dx * dx + dy * dy) <= brush.radius;
            if(!inside)
                continue;
            bool available = false;
            packedTile(x, y, layer, &available);
            if(!available)
                continue;
            painted.insert(y * width + x);
        }
    }
    if(painted.isEmpty())
    {
        if(report)
            report->add(layer == ETerrainTileLayer::Overlay
                            ? "terrain.overlay_inactive" : "terrain.semantic_empty",
                        EValidationSeverity::Warning,
                        layer == ETerrainTileLayer::Overlay
                            ? "The brush did not cover an active overlay cell"
                            : "The brush did not cover an editable terrain tile");
        return command;
    }

    // Choose the dominant uniform family immediately around the stroke. Stock
    // atlases do not contain every direct family pairing, so route through the
    // shortest sequence of authored boundary-compatible families when needed.
    QMap<qint32, int> backgroundCounts;
    QSet<int> perimeter;
    for(int index : painted)
    {
        const int x = index % width;
        const int y = index / width;
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const int sampleX = x + offsetX;
                const int sampleY = y + offsetY;
                if(sampleX < 0 || sampleX >= width || sampleY < 0 || sampleY >= height)
                    continue;
                const int sampleIndex = sampleY * width + sampleX;
                if(!painted.contains(sampleIndex))
                    perimeter.insert(sampleIndex);
            }
        }
    }
    if(perimeter.isEmpty())
        perimeter = painted;
    for(int index : perimeter)
    {
        const int x = index % width;
        const int y = index / width;
        bool available = false;
        const quint16 current = packedTile(x, y, layer, &available);
        bool found = false;
        const STerrainTransitionSignature signature = available
            ? catalog.signatureForPacked(current, &found)
            : STerrainTransitionSignature();
        if(!found)
            continue;
        const qint32 corners[4] = {signature.northWest, signature.northEast,
                                   signature.southEast, signature.southWest};
        for(qint32 candidate : corners)
        {
            STerrainTransitionSignature pure;
            pure.northWest = pure.northEast = pure.southEast = pure.southWest = candidate;
            bool pureFound = false;
            catalog.select(pure, 0, 0, 0, &pureFound);
            if(pureFound)
                backgroundCounts[candidate] += 1;
        }
    }
    qint32 backgroundMaterial = materialId;
    int bestCount = -1;
    for(auto entry = backgroundCounts.constBegin(); entry != backgroundCounts.constEnd(); ++entry)
    {
        if(entry.value() > bestCount)
        {
            backgroundMaterial = entry.key();
            bestCount = entry.value();
        }
    }
    const QVector<qint32> materialPath =
        catalog.shortestMaterialPath(materialId, backgroundMaterial);
    if(materialPath.isEmpty())
    {
        if(report)
            report->add("terrain.semantic_route", EValidationSeverity::Error,
                        "The namespace template has no complete transition route between materials",
                        QString("%1 -> %2").arg(materialId).arg(backgroundMaterial));
        return command;
    }

    const int transitionRadius = qMax(0, materialPath.size() - 1);
    QSet<int> affected;
    QMap<int, int> cornerDistances;
    for(int index : painted)
    {
        const int x = index % width;
        const int y = index / width;
        for(int offsetY = -transitionRadius; offsetY <= transitionRadius; ++offsetY)
        {
            for(int offsetX = -transitionRadius; offsetX <= transitionRadius; ++offsetX)
            {
                const int affectedX = x + offsetX;
                const int affectedY = y + offsetY;
                bool available = false;
                if(affectedX >= 0 && affectedX < width
                   && affectedY >= 0 && affectedY < height)
                    packedTile(affectedX, affectedY, layer, &available);
                if(available)
                    affected.insert(affectedY * width + affectedX);
            }
        }
        for(int cornerY = y - transitionRadius; cornerY <= y + 1 + transitionRadius; ++cornerY)
        {
            for(int cornerX = x - transitionRadius; cornerX <= x + 1 + transitionRadius; ++cornerX)
            {
                if(cornerX < 0 || cornerX > width || cornerY < 0 || cornerY > height)
                    continue;
                const int deltaX = qMax(qMax(x - cornerX, 0), cornerX - (x + 1));
                const int deltaY = qMax(qMax(y - cornerY, 0), cornerY - (y + 1));
                const int distance = qMax(deltaX, deltaY);
                const int key = cornerY * (width + 1) + cornerX;
                if(!cornerDistances.contains(key) || distance < cornerDistances.value(key))
                    cornerDistances.insert(key, distance);
            }
        }
    }

    // Decode only the routed footprint. Existing shipped maps may contain
    // abstract corner mismatches, so the generated patch is canonicalized
    // locally while all cells outside this bounded route remain untouched.
    QMap<int, STerrainTransitionSignature> existingSignatures;
    QMap<int, quint16> existingTiles;
    for(int index : affected)
    {
        const int x = index % width;
        const int y = index / width;
        bool available = false;
        const quint16 current = packedTile(x, y, layer, &available);
        bool found = false;
        const STerrainTransitionSignature signature = catalog.signatureForPacked(current, &found);
        if(!available || !found)
        {
            if(report)
                report->add("terrain.semantic_catalog", EValidationSeverity::Error,
                            "An existing tile is absent from the registered namespace template",
                            QString("%1,%2 packed:0x%3").arg(x).arg(y)
                                .arg(current, 4, 16, QLatin1Char('0')));
            return STerrainTileCommand();
        }
        existingSignatures.insert(index, signature);
        existingTiles.insert(index, current);
    }

    const auto routedMaterial = [&](int cornerX, int cornerY) -> qint32
    {
        const int distance = cornerDistances.value(
            cornerY * (width + 1) + cornerX, transitionRadius);
        return materialPath.at(qMin(distance, materialPath.size() - 1));
    };

    for(int index : affected)
    {
        const int x = index % width;
        const int y = index / width;
        STerrainTransitionSignature desired;
        desired.northWest = routedMaterial(x, y);
        desired.northEast = routedMaterial(x + 1, y);
        desired.southEast = routedMaterial(x + 1, y + 1);
        desired.southWest = routedMaterial(x, y + 1);
        const quint16 before = existingTiles.value(index);
        if(desired == existingSignatures.value(index))
            continue;
        bool found = false;
        const quint16 after = catalog.selectForPaint(
            desired, x, y, distributionSeed, &found);
        if(!found)
        {
            if(report)
                report->add("terrain.semantic_transition", EValidationSeverity::Error,
                            "The namespace template has no tile for a required local corner transition",
                            QString("%1,%2 corners:%3,%4,%5,%6")
                                .arg(x).arg(y).arg(desired.northWest).arg(desired.northEast)
                                .arg(desired.southEast).arg(desired.southWest));
            return STerrainTileCommand();
        }
        STerrainTileDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.layer = layer;
        delta.before = before;
        delta.after = after;
        command.deltas.append(delta);
    }
    return command;
}

bool CTerrainEditModel::apply(const STerrainTileCommand& command, bool forward,
                              CValidationReport* report)
{
    QSet<int> commandTiles;
    struct SResolvedTile
    {
        int sectorIndex;
        int localTile;
        quint64 offset;
    };
    QVector<SResolvedTile> resolved;
    resolved.reserve(command.deltas.size());
    for(const STerrainTileDelta& delta : command.deltas)
    {
        int sectorIndex = -1;
        int localTile = -1;
        if(!tileLocation(delta.globalX, delta.globalY, &sectorIndex, &localTile, report))
            return false;
        const int key = ((delta.layer == ETerrainTileLayer::Overlay) ? 1 : 0)
            * tileCountX() * tileCountY() + delta.globalY * tileCountX() + delta.globalX;
        if(commandTiles.contains(key))
        {
            if(report)
                report->add("terrain.tile_duplicate", EValidationSeverity::Error,
                            "Tile command references the same layer cell more than once",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        commandTiles.insert(key);
        const SSector& sector = m_sectors.at(sectorIndex);
        if(delta.layer == ETerrainTileLayer::Overlay
           && !overlayTileIsActive(sector, localTile))
        {
            if(report)
                report->add("terrain.overlay_inactive", EValidationSeverity::Error,
                            "Tile painting cannot create an overlay surface implicitly",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        const quint64 offset = delta.layer == ETerrainTileLayer::Land
            ? sector.source.landTileOffset : sector.source.overlayTileOffset;
        if(offset == 0)
            return false;
        const quint16 expected = forward ? delta.before : delta.after;
        if(readPackedTile(sector.currentPayload, offset, localTile) != expected)
        {
            if(report)
                report->add("terrain.tile_provenance", EValidationSeverity::Error,
                            "Tile command no longer matches the canonical sector payload",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        if(forward)
        {
            const SMprPackedTile selected = SMprPackedTile::decode(delta.after);
            if(selected.atlasIndex >= m_textureCount
               || selected.atlasSlot() >= m_tileTypes.size())
            {
                if(report)
                    report->add("terrain.tile_namespace", EValidationSeverity::Error,
                                "Tile command selects a slot outside the MPR atlas namespace",
                                QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
                return false;
            }
        }
        resolved.append({sectorIndex, localTile, offset});
    }

    for(int index = 0; index < command.deltas.size(); ++index)
    {
        const STerrainTileDelta& delta = command.deltas.at(index);
        const SResolvedTile& target = resolved.at(index);
        SSector& sector = m_sectors[target.sectorIndex];
        writePackedTile(sector.currentPayload, target.offset, target.localTile,
                        forward ? delta.after : delta.before);
        sector.dirty = sector.currentPayload != sector.source.payload;
    }
    return true;
}

bool CTerrainEditModel::appendSectorEdits(CMprEditTransaction* transaction,
                                          CValidationReport* report) const
{
    if(!transaction)
    {
        if(report)
            report->add("terrain.transaction", EValidationSeverity::Error,
                        "No MPR transaction was supplied");
        return false;
    }
    QSet<int> changedVertices;
    for(int index = 0; index < m_heights.size(); ++index)
        if(m_heights.at(index) != m_originalHeights.at(index))
            changedVertices.insert(index);
    QSet<int> affectedNormals;
    for(int index : changedVertices)
    {
        const int centerX = index % m_vertexCountX;
        const int centerY = index / m_vertexCountX;
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const int x = centerX + offsetX;
                const int y = centerY + offsetY;
                if(x >= 0 && x < m_vertexCountX && y >= 0 && y < m_vertexCountY)
                    affectedNormals.insert(globalIndex(x, y));
            }
        }
    }

    QVector<QByteArray> replacements;
    replacements.reserve(m_sectors.size());
    for(const SSector& sector : m_sectors)
        replacements.append(sector.currentPayload);
    for(int index : affectedNormals)
    {
        const int x = index % m_vertexCountX;
        const int y = index / m_vertexCountX;
        if(!normalNeighborhoodIsEditable(x, y))
        {
            if(report)
                report->add("terrain.normal_seam", EValidationSeverity::Error,
                            "An affected normal depends on a pre-existing conflicting seam",
                            QString("%1,%2").arg(x).arg(y));
            return false;
        }
        const quint32 packed = packedNormal(x, y);
        for(const SVertexRef& ref : m_refs.at(index))
            writePackedNormal(replacements[ref.sectorIndex], ref.localX, ref.localY, packed);
    }

    int editedSectorCount = 0;
    for(int index = 0; index < m_sectors.size(); ++index)
    {
        const SSector& sector = m_sectors.at(index);
        if(replacements.at(index) == sector.source.payload)
            continue;
        transaction->replaceMemberPayload(sector.source.memberName,
                                          sector.source.expectedPayloadSha256,
                                          replacements.at(index));
        ++editedSectorCount;
    }
    if(report)
        report->add("terrain.transaction", EValidationSeverity::Info,
                    QString("Prepared %1 sector patch(es) for %2 changed height vertex/vertices, %3 affected normal(s), and localized tile edits")
                        .arg(editedSectorCount).arg(changedVertices.size())
                        .arg(affectedNormals.size()));
    return true;
}

bool CTerrainEditModel::normalNeighborhoodIsEditable(int globalX, int globalY) const
{
    for(int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        for(int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            const int x = globalX + offsetX;
            const int y = globalY + offsetY;
            if(x < 0 || x >= m_vertexCountX || y < 0 || y >= m_vertexCountY)
                continue;
            if(m_conflictedVertices.at(globalIndex(x, y)))
                return false;
        }
    }
    return true;
}

quint32 CTerrainEditModel::packedNormal(int globalX, int globalY) const
{
    // Copied Base/JMV fixtures match a unit-weight average of the normalized
    // faces in the eight-neighbour fan. Horizontal jitter uses the renderer's
    // observed signed-offset scale; the stored maxZ converts raw height to the
    // same world-space slope used when the sector is drawn.
    const auto position = [this](int x, int y) {
        const int index = globalIndex(x, y);
        SVector3 value;
        value.x = double(x) + double(m_xOffsets.at(index)) / 254.0;
        value.y = double(y) + double(m_yOffsets.at(index)) / 254.0;
        value.z = double(m_heights.at(index)) * double(m_maxZ) / 65535.0;
        return value;
    };
    static const int ring[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    };
    const SVector3 center = position(globalX, globalY);
    SVector3 normal;
    for(int index = 0; index < 8; ++index)
    {
        const int next = (index + 1) % 8;
        const int firstX = globalX + ring[index][0];
        const int firstY = globalY + ring[index][1];
        const int secondX = globalX + ring[next][0];
        const int secondY = globalY + ring[next][1];
        if(firstX < 0 || firstX >= m_vertexCountX || firstY < 0 || firstY >= m_vertexCountY ||
           secondX < 0 || secondX >= m_vertexCountX || secondY < 0 || secondY >= m_vertexCountY)
            continue;
        SVector3 face = cross(subtract(position(firstX, firstY), center),
                              subtract(position(secondX, secondY), center));
        if(face.z < 0.0)
        {
            face.x = -face.x;
            face.y = -face.y;
            face.z = -face.z;
        }
        if(!normalize(&face))
            continue;
        normal.x += face.x;
        normal.y += face.y;
        normal.z += face.z;
    }
    if(!normalize(&normal))
        normal = {0.0, 0.0, 1.0};
    const quint32 packedX = quint32(qBound(0, int(normal.x * 1000.0 + 1000.0), 2047));
    const quint32 packedY = quint32(qBound(0, int(normal.y * 1000.0 + 1000.0), 2047));
    const quint32 packedZ = quint32(qBound(0, int(normal.z * 1000.0), 1023));
    return (packedZ << 22) | (packedX << 11) | packedY;
}

QByteArray CTerrainEditModel::sectorPayload(int sectorX, int sectorY) const
{
    const int index = m_sectorLookup.value(QPair<int, int>(sectorX, sectorY), -1);
    return index >= 0 ? m_sectors.at(index).currentPayload : QByteArray();
}

bool CTerrainTileEditHistory::execute(CTerrainEditModel* model,
                                      const STerrainTileCommand& command,
                                      CValidationReport* report)
{
    if(!model || command.isEmpty() || !model->apply(command, true, report))
        return false;
    m_undo.append(command);
    m_redo.clear();
    return true;
}

bool CTerrainTileEditHistory::undo(CTerrainEditModel* model,
                                   CValidationReport* report)
{
    if(!model || m_undo.isEmpty())
        return false;
    const STerrainTileCommand command = m_undo.back();
    if(!model->apply(command, false, report))
        return false;
    m_undo.removeLast();
    m_redo.append(command);
    return true;
}

bool CTerrainTileEditHistory::redo(CTerrainEditModel* model,
                                   CValidationReport* report)
{
    if(!model || m_redo.isEmpty())
        return false;
    const STerrainTileCommand command = m_redo.back();
    if(!model->apply(command, true, report))
        return false;
    m_redo.removeLast();
    m_undo.append(command);
    return true;
}

void CTerrainTileEditHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
}

bool CTerrainEditHistory::execute(CTerrainEditModel* model,
                                  const STerrainHeightCommand& command,
                                  CValidationReport* report, bool allowHeightClamping)
{
    if(!model || command.isEmpty()
       || !model->apply(command, true, report, allowHeightClamping))
        return false;
    m_undo.append(command);
    m_redo.clear();
    return true;
}

bool CTerrainEditHistory::undo(CTerrainEditModel* model, CValidationReport* report)
{
    if(!model || m_undo.isEmpty())
        return false;
    const STerrainHeightCommand command = m_undo.back();
    if(!model->apply(command, false, report))
        return false;
    m_undo.removeLast();
    m_redo.append(command);
    return true;
}

bool CTerrainEditHistory::redo(CTerrainEditModel* model, CValidationReport* report,
                               bool allowHeightClamping)
{
    if(!model || m_redo.isEmpty())
        return false;
    const STerrainHeightCommand command = m_redo.back();
    if(!model->apply(command, true, report, allowHeightClamping))
        return false;
    m_redo.removeLast();
    m_undo.append(command);
    return true;
}

void CTerrainEditHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
}

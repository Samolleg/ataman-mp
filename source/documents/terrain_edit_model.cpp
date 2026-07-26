#include "terrain_edit_model.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QSet>
#include <QtEndian>

#include <cmath>
#include <functional>
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
const int kVertexBlockSize = kSectorVertices * kSectorVertices * kVertexSize;
const int kTileBlockSize = kSectorTileCount * kTileSize;
const int kLandSectorSize = kLandVertexOffset + kVertexBlockSize + kTileBlockSize;
const int kOverlaySectorSize = kLandVertexOffset + 2 * kVertexBlockSize
    + 3 * kTileBlockSize;
const quint32 kSectorSignature = 0xCF4BF774;
const double kNewOverlayClearanceWorld = 0.02;

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

quint16 readVertexHeight(const QByteArray& payload, quint64 baseOffset,
                         int localX, int localY)
{
    const quint64 offset = baseOffset
        + quint64(localY * kSectorVertices + localX) * kVertexSize + 2;
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(
        payload.constData() + offset));
}

void writeVertexHeight(QByteArray& payload, quint64 baseOffset,
                       int localX, int localY, quint16 height)
{
    const quint64 offset = baseOffset
        + quint64(localY * kSectorVertices + localX) * kVertexSize + 2;
    uchar encoded[2];
    qToLittleEndian<quint16>(height, encoded);
    payload[int(offset)] = char(encoded[0]);
    payload[int(offset) + 1] = char(encoded[1]);
}

qint8 readVertexOffset(const QByteArray& payload, quint64 baseOffset,
                       int localX, int localY, int component)
{
    const quint64 offset = baseOffset
        + quint64(localY * kSectorVertices + localX) * kVertexSize + component;
    return qint8(payload.at(int(offset)));
}

void writeVertexNormal(QByteArray& payload, quint64 baseOffset,
                       int localX, int localY, quint32 packed)
{
    const quint64 offset = baseOffset
        + quint64(localY * kSectorVertices + localX) * kVertexSize + 4;
    uchar encoded[4];
    qToLittleEndian<quint32>(packed, encoded);
    for(int byte = 0; byte < 4; ++byte)
        payload[int(offset) + byte] = char(encoded[byte]);
}

QByteArray readVertexBytes(const QByteArray& payload, quint64 baseOffset,
                           int localX, int localY)
{
    const quint64 offset = baseOffset
        + quint64(localY * kSectorVertices + localX) * kVertexSize;
    return payload.mid(int(offset), kVertexSize);
}

void writeVertexBytes(QByteArray& payload, quint64 baseOffset,
                      int localX, int localY, const QByteArray& bytes)
{
    const quint64 offset = baseOffset
        + quint64(localY * kSectorVertices + localX) * kVertexSize;
    payload.replace(int(offset), kVertexSize, bytes.constData(), kVertexSize);
}

void writeOverlayMaterial(QByteArray& payload, quint64 baseOffset,
                          int localTile, qint16 material)
{
    writePackedTile(payload, baseOffset, localTile, quint16(material));
}

bool tileInsideBrush(const STerrainTileBrush& brush, int x, int y)
{
    const float dx = std::fabs(float(x) - brush.centerX);
    const float dy = std::fabs(float(y) - brush.centerY);
    return brush.shape == ETerrainTileBrushShape::Square
        ? qMax(dx, dy) <= brush.radius
        : std::sqrt(dx * dx + dy * dy) <= brush.radius;
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

void appendTerrainTileCommand(STerrainTileCommand* stroke,
                              const STerrainTileCommand& stamp)
{
    if(!stroke)
        return;
    for(const STerrainTileDelta& delta : stamp.deltas)
    {
        int existingIndex = -1;
        for(int index = 0; index < stroke->deltas.size(); ++index)
        {
            const STerrainTileDelta& existing = stroke->deltas.at(index);
            if(existing.globalX == delta.globalX
               && existing.globalY == delta.globalY
               && existing.layer == delta.layer)
            {
                existingIndex = index;
                break;
            }
        }
        if(existingIndex < 0)
            stroke->deltas.append(delta);
        else
        {
            STerrainTileDelta& existing = stroke->deltas[existingIndex];
            existing.after = delta.after;
            existing.diagnosticStrokeId = delta.diagnosticStrokeId;
            if(existing.before == existing.after)
                stroke->deltas.remove(existingIndex);
        }
    }
}

void appendTerrainHeightCommand(STerrainHeightCommand* stroke,
                                const STerrainHeightCommand& stamp)
{
    if(!stroke)
        return;
    for(const STerrainHeightDelta& delta : stamp.deltas)
    {
        int existingIndex = -1;
        for(int index = 0; index < stroke->deltas.size(); ++index)
        {
            const STerrainHeightDelta& existing = stroke->deltas.at(index);
            if(existing.globalX == delta.globalX
               && existing.globalY == delta.globalY)
            {
                existingIndex = index;
                break;
            }
        }
        if(existingIndex < 0)
            stroke->deltas.append(delta);
        else
        {
            STerrainHeightDelta& existing = stroke->deltas[existingIndex];
            existing.after = delta.after;
            existing.clamped = existing.clamped || delta.clamped;
            if(existing.before == existing.after)
                stroke->deltas.remove(existingIndex);
        }
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

STerrainTransitionSignature CTerrainTransitionCatalog::mirroredGridY(
    const STerrainTransitionSignature& signature)
{
    STerrainTransitionSignature result;
    result.northWest = signature.southWest;
    result.northEast = signature.southEast;
    result.southEast = signature.northEast;
    result.southWest = signature.northWest;
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
    const STerrainTransitionSignature lookup = m_mirrorPaintGridY
        ? mirroredGridY(signature) : signature;
    return select(lookup, globalX, globalY, distributionSeed, found);
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

STerrainTransitionSignature CTerrainTransitionCatalog::gridSignatureForPacked(
    quint16 packedTile, bool* found) const
{
    bool selected = false;
    const STerrainTransitionSignature catalog =
        signatureForPacked(packedTile, &selected);
    if(found)
        *found = selected;
    if(!selected)
        return STerrainTransitionSignature();
    return m_mirrorPaintGridY ? mirroredGridY(catalog) : catalog;
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

QVector<STerrainTransitionSignature> CTerrainTransitionCatalog::gridSignatures() const
{
    QVector<STerrainTransitionSignature> result;
    QSet<QByteArray> seen;
    for(const STerrainTransitionVariant& variant : m_variants)
    {
        const STerrainTransitionSignature signature = m_mirrorPaintGridY
            ? mirroredGridY(variant.signature) : variant.signature;
        const QByteArray key = signatureKey(signature);
        if(seen.contains(key))
            continue;
        seen.insert(key);
        result.append(signature);
    }
    return result;
}

QVector<qint32> CTerrainTransitionCatalog::shortestMaterialPath(
    qint32 fromMaterial, qint32 toMaterial,
    const QMap<qint32, int>& preferredMaterials) const
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
        QVector<qint32> next = neighbors.value(tail);
        std::sort(next.begin(), next.end(), [&](qint32 left, qint32 right) {
            const int leftPreference = preferredMaterials.value(left);
            const int rightPreference = preferredMaterials.value(right);
            return leftPreference != rightPreference
                ? leftPreference > rightPreference : left < right;
        });
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
        source.overlayVertexOffset = sector.overlayVertexOffset;
        source.landTileOffset = sector.landTileOffset;
        source.overlayTileOffset = sector.overlayTileOffset;
        source.overlayMaterialOffset = sector.overlayMaterialOffset;
        source.payload = member.payload;
        sources.append(source);
    }
    return loadSources(int(document.mapInfo().sectorCountX),
                       int(document.mapInfo().sectorCountY), document.mapInfo().maxZ,
                       sources, report, int(document.mapInfo().textureCount),
                       document.mapInfo().tileTypes,
                       int(document.mapInfo().materialCount));
}

bool CTerrainEditModel::loadSources(int sectorCountX, int sectorCountY, float maxZ,
                                    const QVector<STerrainSectorSource>& sources,
                                    CValidationReport* report, int textureCount,
                                    const QVector<qint32>& tileTypes,
                                    int materialCount)
{
    m_sectorCountX = 0;
    m_sectorCountY = 0;
    m_vertexCountX = 0;
    m_vertexCountY = 0;
    m_maxZ = 0.0f;
    m_originalMaxZ = 0.0f;
    m_textureCount = 0;
    m_materialCount = 0;
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
    m_originalMaxZ = maxZ;
    m_textureCount = qMax(0, textureCount);
    m_materialCount = qMax(0, materialCount);
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

STerrainHeightRange CTerrainEditModel::heightRange() const
{
    STerrainHeightRange range;
    range.vertexCount = m_heights.size();
    range.worldUnitsPerRawStep = m_maxZ / 65535.0f;
    if(m_heights.isEmpty())
        return range;
    const auto bounds = std::minmax_element(m_heights.constBegin(), m_heights.constEnd());
    range.minimumRaw = *bounds.first;
    range.maximumRaw = *bounds.second;
    range.minimumWorld = float(range.minimumRaw) * range.worldUnitsPerRawStep;
    range.maximumWorld = float(range.maximumRaw) * range.worldUnitsPerRawStep;
    return range;
}

bool CTerrainEditModel::isDirty() const
{
    if(m_maxZ != m_originalMaxZ)
        return true;
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

bool CTerrainEditModel::refreshSectorLayout(SSector* sector,
                                            CValidationReport* report) const
{
    if(!sector || sector->currentPayload.size() < 5
       || qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(
              sector->currentPayload.constData())) != kSectorSignature)
        return false;
    const quint8 type = quint8(sector->currentPayload.at(4));
    if(type != 0 && type != 3)
    {
        if(report)
            report->add("terrain.overlay_sector_type", EValidationSeverity::Error,
                        "Overlay editing supports only ordinary and overlay SEC members",
                        sector->source.memberName);
        return false;
    }
    const int required = type == 3 ? kOverlaySectorSize : kLandSectorSize;
    if(sector->currentPayload.size() < required)
    {
        if(report)
            report->add("terrain.overlay_sector_size", EValidationSeverity::Error,
                        "Overlay edit produced a truncated SEC payload",
                        sector->source.memberName);
        return false;
    }
    sector->source.type = type;
    sector->source.overlayVertexOffset = type == 3
        ? kLandVertexOffset + kVertexBlockSize : 0;
    sector->source.landTileOffset = kLandVertexOffset + kVertexBlockSize
        + (type == 3 ? kVertexBlockSize : 0);
    sector->source.overlayTileOffset = type == 3
        ? sector->source.landTileOffset + kTileBlockSize : 0;
    sector->source.overlayMaterialOffset = type == 3
        ? sector->source.overlayTileOffset + kTileBlockSize : 0;
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

qint16 CTerrainEditModel::overlayMaterialIndex(int globalTileX, int globalTileY,
                                               bool* available) const
{
    int sectorIndex = -1;
    int localTile = -1;
    if(!tileLocation(globalTileX, globalTileY, &sectorIndex, &localTile))
    {
        if(available)
            *available = false;
        return -1;
    }
    const SSector& sector = m_sectors.at(sectorIndex);
    const bool present = overlayTileIsActive(sector, localTile);
    if(available)
        *available = present;
    return present ? readOverlayMaterial(sector.currentPayload,
                                         sector.source.overlayMaterialOffset,
                                         localTile) : qint16(-1);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayActivation(
    const STerrainTileBrush& brush, qint16 materialIndex,
    quint8 atlasIndex, quint8 tileIndex, quint8 rotation,
    CValidationReport* report) const
{
    SOverlaySurfaceCommand command;
    command.description = QStringLiteral("Activate overlay surface");
    SMprPackedTile selected;
    selected.atlasIndex = atlasIndex;
    selected.tileIndex = tileIndex;
    selected.rotation = rotation;
    if(brush.radius < 0.0f || materialIndex < 0 || tileIndex >= 64 || rotation >= 4
       || atlasIndex >= m_textureCount || selected.atlasSlot() >= m_tileTypes.size()
       || (m_materialCount > 0 && materialIndex >= m_materialCount))
    {
        if(report)
            report->add("terrain.overlay_activation", EValidationSeverity::Error,
                        "Overlay activation requires an existing material and atlas tile");
        return command;
    }

    struct SActivatedCell
    {
        SActivatedCell() {}
        SActivatedCell(int sector, int tile, int x, int y)
            : sectorIndex(sector), localTile(tile), globalX(x), globalY(y) {}
        int sectorIndex{-1};
        int localTile{-1};
        int globalX{0};
        int globalY{0};
    };
    QMap<int, QByteArray> replacements;
    const auto replacementFor = [this, &replacements](int sectorIndex)
        -> QByteArray& {
        auto it = replacements.find(sectorIndex);
        if(it == replacements.end())
            it = replacements.insert(sectorIndex,
                                     m_sectors.at(sectorIndex).currentPayload);
        return it.value();
    };
    QVector<SActivatedCell> activatedCells;
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(tileCountX() - 1,
                              int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(tileCountY() - 1,
                              int(std::floor(brush.centerY + brush.radius)));
    for(int y = minimumY; y <= maximumY; ++y)
        for(int x = minimumX; x <= maximumX; ++x)
        {
            if(!tileInsideBrush(brush, x, y))
                continue;
            int sectorIndex = -1;
            int localTile = -1;
            if(!tileLocation(x, y, &sectorIndex, &localTile, report))
                return SOverlaySurfaceCommand();
            const SSector& sector = m_sectors.at(sectorIndex);
            QByteArray& payload = replacementFor(sectorIndex);
            quint64 overlayTileOffset = sector.source.overlayTileOffset;
            quint64 overlayMaterialOffset = sector.source.overlayMaterialOffset;
            if(quint8(payload.at(4)) == 0)
            {
                const QByteArray landVertices = payload.mid(kLandVertexOffset, kVertexBlockSize);
                const QByteArray landTiles = payload.mid(
                    kLandVertexOffset + kVertexBlockSize, kTileBlockSize);
                const QByteArray trailing = payload.mid(kLandSectorSize);
                QByteArray upgraded;
                upgraded.reserve(payload.size() + kVertexBlockSize + 2 * kTileBlockSize);
                upgraded.append(payload.left(kLandVertexOffset));
                upgraded[4] = char(3);
                // New overlay geometry starts as an exact copy of land. Only
                // explicitly activated cells become visible; inactive records
                // retain the native -1 sentinel.
                upgraded.append(landVertices);
                upgraded.append(landVertices);
                upgraded.append(landTiles);
                upgraded.append(QByteArray(kTileBlockSize, char(0xFF)));
                upgraded.append(QByteArray(kTileBlockSize, char(0xFF)));
                upgraded.append(trailing);
                payload = upgraded;
                overlayTileOffset = kLandVertexOffset + 2 * kVertexBlockSize
                    + kTileBlockSize;
                overlayMaterialOffset = overlayTileOffset + kTileBlockSize;
            }
            const bool wasActive = readOverlayMaterial(
                payload, overlayMaterialOffset, localTile) >= 0;
            if(readPackedTile(payload, overlayTileOffset, localTile) == selected.encode()
               && readOverlayMaterial(payload, overlayMaterialOffset, localTile)
                    == materialIndex)
                continue;
            writePackedTile(payload, overlayTileOffset, localTile, selected.encode());
            writeOverlayMaterial(payload, overlayMaterialOffset, localTile, materialIndex);
            if(!wasActive)
                activatedCells.append({sectorIndex, localTile, x, y});
        }

    // An inactive overlay record is not evidence of a usable surface plane.
    // Shipped maps may retain arbitrary/buried vertex data outside active
    // liquid cells. Initialize genuinely new cells from current land geometry,
    // with the smallest practical world-space clearance, while preserving an
    // active neighbour. Re-adding a removed cell is a new authoring action:
    // undo restores the old bytes exactly, while activation must not resurrect
    // dormant/buried geometry from the original SEC.
    QSet<int> activatedVertices;
    for(const SActivatedCell& cell : activatedCells)
    {
        for(int vertexY = cell.globalY * 2;
            vertexY <= cell.globalY * 2 + 2; ++vertexY)
            for(int vertexX = cell.globalX * 2;
                vertexX <= cell.globalX * 2 + 2; ++vertexX)
            {
                const int key = globalIndex(vertexX, vertexY);
                activatedVertices.insert(key);
            }
    }
    const auto sectorIndicesForVertex = [&](int globalX, int globalY) {
        QVector<int> indices;
        QVector<int> sectorXs({globalX / kSectorStride});
        QVector<int> sectorYs({globalY / kSectorStride});
        if(globalX > 0 && globalX % kSectorStride == 0)
            sectorXs.prepend(globalX / kSectorStride - 1);
        if(globalY > 0 && globalY % kSectorStride == 0)
            sectorYs.prepend(globalY / kSectorStride - 1);
        for(int sectorY : sectorYs)
            for(int sectorX : sectorXs)
            {
                const int index = m_sectorLookup.value(
                    QPair<int, int>(sectorX, sectorY), -1);
                if(index >= 0)
                    indices.append(index);
            }
        return indices;
    };
    const auto vertexTouchesActiveCell = [&](const QByteArray& payload,
                                              int localX, int localY) {
        if(payload.size() < kOverlaySectorSize || quint8(payload.at(4)) != 3)
            return false;
        QVector<int> tileXs({qMin(kSectorTiles - 1, localX / 2)});
        QVector<int> tileYs({qMin(kSectorTiles - 1, localY / 2)});
        if(localX > 0 && localX < kSectorStride && localX % 2 == 0)
            tileXs.prepend(localX / 2 - 1);
        if(localY > 0 && localY < kSectorStride && localY % 2 == 0)
            tileYs.prepend(localY / 2 - 1);
        const quint64 materialOffset = kLandVertexOffset + 2 * kVertexBlockSize
            + 2 * kTileBlockSize;
        for(int tileY : tileYs)
            for(int tileX : tileXs)
                if(readOverlayMaterial(payload, materialOffset,
                                       tileY * kSectorTiles + tileX) >= 0)
                    return true;
        return false;
    };
    const int rawClearance = m_maxZ > 0.0f
        ? qMax(1, int(std::ceil(kNewOverlayClearanceWorld * 65535.0
                               / double(m_maxZ))))
        : 1;
    for(int key : activatedVertices)
    {
        if(key < 0 || key >= m_conflictedVertices.size()
           || m_conflictedVertices.at(key))
        {
            if(report)
                report->add("terrain.overlay_activation_seam",
                    EValidationSeverity::Error,
                    "Overlay activation touches a conflicting land seam",
                    QString("%1,%2").arg(key % m_vertexCountX)
                        .arg(key / m_vertexCountX));
            return SOverlaySurfaceCommand();
        }
        const int globalX = key % m_vertexCountX;
        const int globalY = key / m_vertexCountX;
        const QVector<int> indices = sectorIndicesForVertex(globalX, globalY);
        QByteArray desired;
        bool activeSourceFound = false;
        for(int sectorIndex : indices)
        {
            const SSector& sector = m_sectors.at(sectorIndex);
            const int localX = globalX - sector.source.sectorX * kSectorStride;
            const int localY = globalY - sector.source.sectorY * kSectorStride;
            if(sector.source.type == 3
               && vertexTouchesActiveCell(sector.currentPayload, localX, localY))
            {
                const QByteArray candidate = readVertexBytes(
                    sector.currentPayload, sector.source.overlayVertexOffset,
                    localX, localY);
                if(activeSourceFound && candidate.left(4) != desired.left(4))
                {
                    if(report)
                        report->add("terrain.overlay_activation_seam",
                            EValidationSeverity::Error,
                            "Overlay activation found conflicting active surface vertices",
                            QString("%1,%2").arg(globalX).arg(globalY));
                    return SOverlaySurfaceCommand();
                }
                desired = candidate;
                activeSourceFound = true;
            }
        }
        if(!activeSourceFound)
        {
            if(indices.isEmpty())
                continue;
            const SSector& sector = m_sectors.at(indices.front());
            const int localX = globalX - sector.source.sectorX * kSectorStride;
            const int localY = globalY - sector.source.sectorY * kSectorStride;
            desired = readVertexBytes(sector.currentPayload,
                                      kLandVertexOffset, localX, localY);
            const quint16 landHeight = readVertexHeight(
                sector.currentPayload, kLandVertexOffset, localX, localY);
            const bool clamped = landHeight > 65535 - rawClearance;
            if(clamped)
                ++command.heightClampedVertexCount;
            uchar encoded[2];
            qToLittleEndian<quint16>(clamped
                ? quint16(65535) : quint16(landHeight + rawClearance), encoded);
            desired[2] = char(encoded[0]);
            desired[3] = char(encoded[1]);
        }
        for(int sectorIndex : indices)
        {
            const SSector& sector = m_sectors.at(sectorIndex);
            QByteArray& payload = replacementFor(sectorIndex);
            if(payload.size() < kOverlaySectorSize || quint8(payload.at(4)) != 3)
                continue;
            const int localX = globalX - sector.source.sectorX * kSectorStride;
            const int localY = globalY - sector.source.sectorY * kSectorStride;
            writeVertexBytes(payload, kLandVertexOffset + kVertexBlockSize,
                             localX, localY, desired);
        }
    }

    for(auto it = replacements.constBegin(); it != replacements.constEnd(); ++it)
    {
        const SSector& sector = m_sectors.at(it.key());
        if(it.value() == sector.currentPayload)
            continue;
        SOverlaySectorDelta delta;
        delta.sectorX = sector.source.sectorX;
        delta.sectorY = sector.source.sectorY;
        delta.beforePayload = sector.currentPayload;
        delta.afterPayload = it.value();
        command.sectors.append(delta);
    }
    if(command.heightClampedVertexCount > 0 && report)
        report->add("terrain.overlay_activation_range",
            EValidationSeverity::Warning,
            QString("%1 new overlay vertex/vertices were clamped to the current MPR maxZ; lower the land or increase maxZ if the surface overlaps it")
                .arg(command.heightClampedVertexCount));
    return command;
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayDeactivation(
    const STerrainTileBrush& brush, CValidationReport* report) const
{
    SOverlaySurfaceCommand command;
    command.description = QStringLiteral("Deactivate overlay surface");
    if(brush.radius < 0.0f)
        return command;
    QMap<int, QByteArray> replacements;
    const auto replacementFor = [this, &replacements](int sectorIndex)
        -> QByteArray& {
        auto it = replacements.find(sectorIndex);
        if(it == replacements.end())
            it = replacements.insert(sectorIndex,
                                     m_sectors.at(sectorIndex).currentPayload);
        return it.value();
    };
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(tileCountX() - 1,
                              int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(tileCountY() - 1,
                              int(std::floor(brush.centerY + brush.radius)));
    for(int y = minimumY; y <= maximumY; ++y)
        for(int x = minimumX; x <= maximumX; ++x)
        {
            if(!tileInsideBrush(brush, x, y))
                continue;
            int sectorIndex = -1;
            int localTile = -1;
            if(!tileLocation(x, y, &sectorIndex, &localTile, report))
                return SOverlaySurfaceCommand();
            const SSector& sector = m_sectors.at(sectorIndex);
            if(sector.source.type != 3)
                continue;
            const auto existing = replacements.constFind(sectorIndex);
            const QByteArray& current = existing == replacements.constEnd()
                ? sector.currentPayload : existing.value();
            if(readOverlayMaterial(current, sector.source.overlayMaterialOffset,
                                   localTile) == -1)
                continue;
            QByteArray& payload = replacementFor(sectorIndex);
            // Deactivation changes only the proven native sentinel. Tile and
            // vertex bytes remain available for exact undo or later reuse.
            writeOverlayMaterial(payload, sector.source.overlayMaterialOffset,
                                 localTile, -1);
        }
    for(auto it = replacements.constBegin(); it != replacements.constEnd(); ++it)
    {
        const SSector& sector = m_sectors.at(it.key());
        SOverlaySectorDelta delta;
        delta.sectorX = sector.source.sectorX;
        delta.sectorY = sector.source.sectorY;
        delta.beforePayload = sector.currentPayload;
        delta.afterPayload = it.value();
        command.sectors.append(delta);
    }
    return command;
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayRaiseLower(
    const STerrainBrush& brush, float rawHeightDelta,
    CValidationReport* report) const
{
    return makeOverlayHeightEdit(QStringLiteral("Sculpt overlay height"), brush,
                                 0, rawHeightDelta, 1.0f, report);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayFlatten(
    const STerrainBrush& brush, quint16 targetHeight, float strength,
    CValidationReport* report) const
{
    return makeOverlayHeightEdit(QStringLiteral("Flatten overlay height"), brush,
                                 1, float(targetHeight), strength, report);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlaySmooth(
    const STerrainBrush& brush, float strength,
    CValidationReport* report) const
{
    return makeOverlayHeightEdit(QStringLiteral("Smooth overlay height"), brush,
                                 2, 0.0f, strength, report);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayHeightEdit(
    const QString& description, const STerrainBrush& brush,
    int operation, float value, float strength,
    CValidationReport* report) const
{
    SOverlaySurfaceCommand command;
    command.description = description;
    if(brush.radius <= 0.0f || !std::isfinite(value) || !std::isfinite(strength)
       || strength < 0.0f || strength > 1.0f || operation < 0 || operation > 2)
    {
        if(report)
            report->add("terrain.overlay_sculpt_input", EValidationSeverity::Error,
                        "Overlay sculpt parameters are outside the supported range");
        return command;
    }

    struct SOverlayRef
    {
        int sectorIndex;
        int localX;
        int localY;
        quint16 height;
        qint8 xOffset;
        qint8 yOffset;
    };
    QMap<int, QVector<SOverlayRef>> refs;
    QMap<int, quint16> heights;
    QMap<int, qint8> xOffsets;
    QMap<int, qint8> yOffsets;
    QSet<int> activeVertices;
    QMap<int, QSet<int>> activeRefSectors;
    const int minimumX = qMax(0, int(std::floor(brush.centerX - brush.radius)) - 1);
    const int maximumX = qMin(m_vertexCountX - 1,
                              int(std::ceil(brush.centerX + brush.radius)) + 1);
    const int minimumY = qMax(0, int(std::floor(brush.centerY - brush.radius)) - 1);
    const int maximumY = qMin(m_vertexCountY - 1,
                              int(std::ceil(brush.centerY + brush.radius)) + 1);
    const int minimumSectorX = minimumX / kSectorStride;
    const int maximumSectorX = maximumX / kSectorStride;
    const int minimumSectorY = minimumY / kSectorStride;
    const int maximumSectorY = maximumY / kSectorStride;
    for(int sectorY = minimumSectorY; sectorY <= maximumSectorY; ++sectorY)
        for(int sectorX = minimumSectorX; sectorX <= maximumSectorX; ++sectorX)
    {
        const int sectorIndex = m_sectorLookup.value(
            QPair<int, int>(sectorX, sectorY), -1);
        if(sectorIndex < 0)
            continue;
        const SSector& sector = m_sectors.at(sectorIndex);
        if(sector.source.type != 3 || sector.source.overlayVertexOffset == 0)
            continue;
        const int localMinimumX = qMax(0, minimumX - sectorX * kSectorStride);
        const int localMaximumX = qMin(kSectorStride,
            maximumX - sectorX * kSectorStride);
        const int localMinimumY = qMax(0, minimumY - sectorY * kSectorStride);
        const int localMaximumY = qMin(kSectorStride,
            maximumY - sectorY * kSectorStride);
        for(int localY = localMinimumY; localY <= localMaximumY; ++localY)
        {
            for(int localX = localMinimumX; localX <= localMaximumX; ++localX)
            {
                const int globalX = sector.source.sectorX * kSectorStride + localX;
                const int globalY = sector.source.sectorY * kSectorStride + localY;
                const int key = globalIndex(globalX, globalY);
                const quint16 height = readVertexHeight(
                    sector.currentPayload, sector.source.overlayVertexOffset,
                    localX, localY);
                const qint8 xOffset = readVertexOffset(
                    sector.currentPayload, sector.source.overlayVertexOffset,
                    localX, localY, 0);
                const qint8 yOffset = readVertexOffset(
                    sector.currentPayload, sector.source.overlayVertexOffset,
                    localX, localY, 1);
                refs[key].append({sectorIndex, localX, localY,
                                  height, xOffset, yOffset});
            }
        }
        for(int tileY = 0; tileY < kSectorTiles; ++tileY)
        {
            for(int tileX = 0; tileX < kSectorTiles; ++tileX)
            {
                const int localTile = tileY * kSectorTiles + tileX;
                if(!overlayTileIsActive(sector, localTile))
                    continue;
                for(int vertexY = tileY * 2; vertexY <= tileY * 2 + 2; ++vertexY)
                    for(int vertexX = tileX * 2; vertexX <= tileX * 2 + 2; ++vertexX)
                    {
                        const int globalX = sector.source.sectorX * kSectorStride + vertexX;
                        const int globalY = sector.source.sectorY * kSectorStride + vertexY;
                        if(globalX < minimumX || globalX > maximumX
                           || globalY < minimumY || globalY > maximumY)
                            continue;
                        const int key = globalIndex(globalX, globalY);
                        activeVertices.insert(key);
                        activeRefSectors[key].insert(sectorIndex);
                    }
            }
        }
    }
    for(auto it = refs.constBegin(); it != refs.constEnd(); ++it)
    {
        const int key = it.key();
        const QVector<SOverlayRef>& vertexRefs = it.value();
        if(vertexRefs.isEmpty())
            continue;
        const QSet<int> activeOwners = activeRefSectors.value(key);
        const SOverlayRef* selected = nullptr;
        for(const SOverlayRef& ref : vertexRefs)
        {
            if(!activeOwners.contains(ref.sectorIndex))
                continue;
            if(!selected)
                selected = &ref;
            else if(selected->height != ref.height
                    || selected->xOffset != ref.xOffset
                    || selected->yOffset != ref.yOffset)
            {
                if(report)
                    report->add("terrain.overlay_seam", EValidationSeverity::Error,
                        "Overlay sculpting found conflicting active surface vertices",
                        QString("%1,%2").arg(key % m_vertexCountX)
                            .arg(key / m_vertexCountX));
                return command;
            }
        }
        if(!selected)
            selected = &vertexRefs.front();
        heights.insert(key, selected->height);
        xOffsets.insert(key, selected->xOffset);
        yOffsets.insert(key, selected->yOffset);
    }

    QMap<int, quint16> afterHeights = heights;
    QSet<int> changedVertices;
    for(int key : activeVertices)
    {
        if(!heights.contains(key))
            continue;
        const int x = key % m_vertexCountX;
        const int y = key / m_vertexCountX;
        const float dx = float(x) - brush.centerX;
        const float dy = float(y) - brush.centerY;
        const float weight = brushWeight(std::sqrt(dx * dx + dy * dy),
                                         brush.radius, brush.falloff);
        if(weight <= 0.0f)
            continue;
        const float before = float(heights.value(key));
        float target = before;
        if(operation == 0)
            target = before + value * weight;
        else if(operation == 1)
            target = before + (value - before) * strength * weight;
        else
        {
            float sum = 0.0f;
            int samples = 0;
            for(int oy = -1; oy <= 1; ++oy)
                for(int ox = -1; ox <= 1; ++ox)
                {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if(nx < 0 || ny < 0 || nx >= m_vertexCountX || ny >= m_vertexCountY)
                        continue;
                    const int neighbor = globalIndex(nx, ny);
                    if(!heights.contains(neighbor))
                        continue;
                    sum += heights.value(neighbor);
                    ++samples;
                }
            if(samples > 0)
                target = before + (sum / float(samples) - before) * strength * weight;
        }
        bool clamped = false;
        const quint16 after = clampedHeight(target, &clamped);
        if(clamped)
        {
            if(report)
                report->add("terrain.overlay_sculpt_range", EValidationSeverity::Error,
                            "Overlay sculpting would clamp the raw MPR height range",
                            QString("%1,%2").arg(x).arg(y));
            return SOverlaySurfaceCommand();
        }
        if(after != heights.value(key))
        {
            afterHeights.insert(key, after);
            changedVertices.insert(key);
        }
    }
    if(changedVertices.isEmpty())
        return command;

    QMap<int, QByteArray> replacements;
    const auto replacementFor = [&](int sectorIndex) {
        return replacements.value(sectorIndex,
                                  m_sectors.at(sectorIndex).currentPayload);
    };
    for(int key : changedVertices)
        for(const SOverlayRef& ref : refs.value(key))
        {
            QByteArray payload = replacementFor(ref.sectorIndex);
            writeVertexHeight(payload,
                m_sectors.at(ref.sectorIndex).source.overlayVertexOffset,
                ref.localX, ref.localY, afterHeights.value(key));
            replacements.insert(ref.sectorIndex, payload);
        }

    QSet<int> affectedNormals;
    for(int changed : changedVertices)
    {
        const int centerX = changed % m_vertexCountX;
        const int centerY = changed / m_vertexCountX;
        for(int oy = -1; oy <= 1; ++oy)
            for(int ox = -1; ox <= 1; ++ox)
            {
                const int x = centerX + ox;
                const int y = centerY + oy;
                if(x < 0 || y < 0 || x >= m_vertexCountX || y >= m_vertexCountY)
                    continue;
                const int key = globalIndex(x, y);
                if(activeVertices.contains(key) && refs.contains(key))
                    affectedNormals.insert(key);
            }
    }
    const auto position = [&](int x, int y) {
        const int key = globalIndex(x, y);
        return SVector3(double(x) + double(xOffsets.value(key)) / 254.0,
                        double(y) + double(yOffsets.value(key)) / 254.0,
                        double(afterHeights.value(key)) * double(m_maxZ) / 65535.0);
    };
    static const int ring[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    };
    for(int key : affectedNormals)
    {
        const int x = key % m_vertexCountX;
        const int y = key / m_vertexCountX;
        const SVector3 center = position(x, y);
        SVector3 normal;
        for(int edge = 0; edge < 8; ++edge)
        {
            const int next = (edge + 1) % 8;
            const int ax = x + ring[edge][0];
            const int ay = y + ring[edge][1];
            const int bx = x + ring[next][0];
            const int by = y + ring[next][1];
            if(ax < 0 || ay < 0 || bx < 0 || by < 0
               || ax >= m_vertexCountX || ay >= m_vertexCountY
               || bx >= m_vertexCountX || by >= m_vertexCountY
               || !afterHeights.contains(globalIndex(ax, ay))
               || !afterHeights.contains(globalIndex(bx, by)))
                continue;
            SVector3 face = cross(subtract(position(ax, ay), center),
                                  subtract(position(bx, by), center));
            if(face.z < 0.0)
                face = {-face.x, -face.y, -face.z};
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
        const quint32 packed = (packedZ << 22) | (packedX << 11) | packedY;
        for(const SOverlayRef& ref : refs.value(key))
        {
            QByteArray payload = replacementFor(ref.sectorIndex);
            writeVertexNormal(payload,
                m_sectors.at(ref.sectorIndex).source.overlayVertexOffset,
                ref.localX, ref.localY, packed);
            replacements.insert(ref.sectorIndex, payload);
        }
    }

    for(auto it = replacements.constBegin(); it != replacements.constEnd(); ++it)
    {
        const int index = it.key();
        if(it.value() == m_sectors.at(index).currentPayload)
            continue;
        SOverlaySectorDelta delta;
        delta.sectorX = m_sectors.at(index).source.sectorX;
        delta.sectorY = m_sectors.at(index).source.sectorY;
        delta.beforePayload = m_sectors.at(index).currentPayload;
        delta.afterPayload = it.value();
        command.sectors.append(delta);
    }
    return command;
}

float CTerrainEditModel::navigationHeightSample(int aiTileX, int aiTileY,
                                                ETerrainTileLayer layer,
                                                bool* available) const
{
    if(available)
        *available = false;
    if(aiTileX < 0 || aiTileY < 0
       || aiTileX >= (m_vertexCountX - 1) * 2
       || aiTileY >= (m_vertexCountY - 1) * 2)
        return 0.0f;

    const int quadX = aiTileX / 2;
    const int quadY = aiTileY / 2;
    quint16 h00 = 0;
    quint16 h10 = 0;
    quint16 h01 = 0;
    quint16 h11 = 0;
    if(layer == ETerrainTileLayer::Land)
    {
        h00 = height(quadX, quadY);
        h10 = height(quadX + 1, quadY);
        h01 = height(quadX, quadY + 1);
        h11 = height(quadX + 1, quadY + 1);
    }
    else
    {
        const int globalTileX = aiTileX / 4;
        const int globalTileY = aiTileY / 4;
        int sectorIndex = -1;
        int localTile = -1;
        if(!tileLocation(globalTileX, globalTileY, &sectorIndex, &localTile))
            return 0.0f;
        const SSector& sector = m_sectors.at(sectorIndex);
        if(!overlayTileIsActive(sector, localTile) || sector.source.overlayVertexOffset == 0)
            return 0.0f;
        const int localQuadX = quadX - sector.source.sectorX * kSectorStride;
        const int localQuadY = quadY - sector.source.sectorY * kSectorStride;
        if(localQuadX < 0 || localQuadY < 0
           || localQuadX >= kSectorStride || localQuadY >= kSectorStride)
            return 0.0f;
        const auto readOverlayHeight = [&](int x, int y)
        {
            const quint64 offset = sector.source.overlayVertexOffset
                + quint64(y * kSectorVertices + x) * kVertexSize + 2;
            return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(
                sector.currentPayload.constData() + offset));
        };
        h00 = readOverlayHeight(localQuadX, localQuadY);
        h10 = readOverlayHeight(localQuadX + 1, localQuadY);
        h01 = readOverlayHeight(localQuadX, localQuadY + 1);
        h11 = readOverlayHeight(localQuadX + 1, localQuadY + 1);
    }

    const bool right = (aiTileX & 1) != 0;
    const bool bottom = (aiTileY & 1) != 0;
    float raw = 0.0f;
    if(!right && !bottom)
        raw = 0.5f * h00 + 0.25f * h10 + 0.25f * h01;
    else if(right && !bottom)
        raw = 0.5f * h10 + 0.25f * h00 + 0.25f * h11;
    else if(!right && bottom)
        raw = 0.5f * h01 + 0.25f * h00 + 0.25f * h11;
    else
        raw = 0.5f * h11 + 0.25f * h10 + 0.25f * h01;
    if(available)
        *available = true;
    return raw * m_maxZ / 65535.0f;
}

STerrainVerticalScaleCommand CTerrainEditModel::makeVerticalScale(
    float newMaxZ, ETerrainVerticalScalePolicy policy, bool allowClamping,
    CValidationReport* report) const
{
    STerrainVerticalScaleCommand command;
    command.policy = policy;
    command.beforeMaxZ = m_maxZ;
    command.afterMaxZ = newMaxZ;
    command.description = policy == ETerrainVerticalScalePolicy::ScaleTerrain
        ? QStringLiteral("Scale terrain vertical range")
        : QStringLiteral("Preserve world heights while changing vertical range");
    if(!std::isfinite(newMaxZ) || newMaxZ <= 0.0f || m_heights.isEmpty())
    {
        if(report)
            report->add("terrain.maxz_value", EValidationSeverity::Error,
                        "The new maxZ must be finite and greater than zero");
        return command;
    }
    if(newMaxZ == m_maxZ || policy == ETerrainVerticalScalePolicy::ScaleTerrain)
    {
        command.valid = true;
        return command;
    }

    command.heights.description = command.description;
    for(int index = 0; index < m_heights.size(); ++index)
    {
        const quint16 before = m_heights.at(index);
        const double exact = double(before) * double(m_maxZ) / double(newMaxZ);
        // qRound64 has undefined results outside qint64's representable range.
        // Values above the packed terrain ceiling are clamped anyway, so use a
        // small out-of-range sentinel without converting an enormous/inf value.
        const qint64 rounded = !std::isfinite(exact) || exact > 65535.0
            ? qint64(65536) : qRound64(exact);
        const qint64 bounded = qBound<qint64>(0, rounded, 65535);
        const bool clamped = bounded != rounded;
        if(clamped)
            ++command.clampedVertexCount;
        if(std::fabs(exact - double(rounded)) > 1.0e-9)
            ++command.roundedVertexCount;
        const quint16 after = quint16(bounded);
        const double beforeWorld = double(before) * double(m_maxZ) / 65535.0;
        const double afterWorld = double(after) * double(newMaxZ) / 65535.0;
        command.maximumWorldError = qMax(command.maximumWorldError,
            float(std::fabs(afterWorld - beforeWorld)));
        if(after == before)
            continue;
        STerrainHeightDelta delta;
        delta.globalX = index % m_vertexCountX;
        delta.globalY = index / m_vertexCountX;
        delta.before = before;
        delta.after = after;
        delta.clamped = clamped;
        command.heights.deltas.append(delta);
    }
    if(command.clampedVertexCount > 0 && !allowClamping)
    {
        if(report)
            report->add("terrain.maxz_clamp", EValidationSeverity::Error,
                        QString("Preserving world heights would clamp %1 vertex/vertices; explicit clamp acceptance is required")
                            .arg(command.clampedVertexCount));
        return command;
    }
    command.valid = true;
    return command;
}

bool CTerrainEditModel::apply(const STerrainVerticalScaleCommand& command,
                              bool forward, CValidationReport* report)
{
    if(!command.valid)
    {
        if(report)
            report->add("terrain.maxz_command", EValidationSeverity::Error,
                        "The vertical-scale command is invalid");
        return false;
    }
    const float expected = forward ? command.beforeMaxZ : command.afterMaxZ;
    const float target = forward ? command.afterMaxZ : command.beforeMaxZ;
    if(m_maxZ != expected)
    {
        if(report)
            report->add("terrain.maxz_provenance", EValidationSeverity::Error,
                        "The current maxZ no longer matches the command baseline");
        return false;
    }
    if(!command.heights.isEmpty()
       && !apply(command.heights, forward, report, true))
        return false;
    m_maxZ = target;
    return true;
}

STerrainHeightCommand CTerrainEditModel::makeGlobalOffset(
    float rawHeightDelta, CValidationReport* report) const
{
    STerrainHeightCommand command;
    command.description = QStringLiteral("Offset all terrain heights");
    if(!std::isfinite(rawHeightDelta))
    {
        if(report)
            report->add("terrain.global_offset", EValidationSeverity::Error,
                        "The global terrain offset must be finite");
        return command;
    }
    if(rawHeightDelta == 0.0f)
        return command;
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = height(x, y);
            delta.after = clampedHeight(float(delta.before) + rawHeightDelta,
                                        &delta.clamped);
            if(delta.before != delta.after)
                command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeGlobalFlatten(quint16 targetHeight) const
{
    STerrainHeightCommand command;
    command.description = QStringLiteral("Flatten all terrain");
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const quint16 before = height(x, y);
            if(before == targetHeight)
                continue;
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = before;
            delta.after = targetHeight;
            command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeGlobalClamp(
    quint16 minimumHeight, quint16 maximumHeight, CValidationReport* report) const
{
    STerrainHeightCommand command;
    command.description = QStringLiteral("Clamp all terrain heights");
    if(minimumHeight > maximumHeight)
    {
        if(report)
            report->add("terrain.global_clamp_range", EValidationSeverity::Error,
                        "The terrain clamp minimum must not exceed its maximum");
        return command;
    }
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const quint16 before = height(x, y);
            const quint16 after = qBound(minimumHeight, before, maximumHeight);
            if(before == after)
                continue;
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = before;
            delta.after = after;
            command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeGlobalNormalize(
    quint16 outputMinimum, quint16 outputMaximum, CValidationReport* report) const
{
    STerrainHeightCommand command;
    command.description = QStringLiteral("Normalize all terrain heights");
    if(outputMinimum > outputMaximum)
    {
        if(report)
            report->add("terrain.global_normalize_range", EValidationSeverity::Error,
                        "The terrain normalize minimum must not exceed its maximum");
        return command;
    }
    const STerrainHeightRange input = heightRange();
    if(input.vertexCount == 0)
        return command;
    if(input.minimumRaw == input.maximumRaw)
    {
        if(report)
            report->add("terrain.global_normalize_flat", EValidationSeverity::Error,
                        "A flat landscape has no height range to normalize; use Flatten instead");
        return command;
    }
    const double inputSpan = double(input.maximumRaw) - double(input.minimumRaw);
    const double outputSpan = double(outputMaximum) - double(outputMinimum);
    for(int y = 0; y < m_vertexCountY; ++y)
    {
        for(int x = 0; x < m_vertexCountX; ++x)
        {
            const quint16 before = height(x, y);
            const double normalized = (double(before) - double(input.minimumRaw)) / inputSpan;
            const quint16 after = quint16(qBound<qint64>(0,
                qRound64(double(outputMinimum) + outputSpan * normalized), 65535));
            if(before == after)
                continue;
            STerrainHeightDelta delta;
            delta.globalX = x;
            delta.globalY = y;
            delta.before = before;
            delta.after = after;
            command.deltas.append(delta);
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

STerrainTileCommand CTerrainEditModel::makeBoundedSemanticTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, CValidationReport* report) const
{
    STerrainTileCommand command;
    command.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint terrain material")
        : QStringLiteral("Paint overlay material");
    command.semanticAudit.valid = true;
    command.semanticAudit.brush = brush;
    command.semanticAudit.layer = layer;
    command.semanticAudit.requestedMaterialId = materialId;
    command.semanticAudit.junctionPadding = 0;
    command.semanticAudit.solverLimit = 0;
    if(brush.radius < 0.0f || !catalog.hasMaterial(materialId))
    {
        if(report)
            report->add("terrain.semantic_type", EValidationSeverity::Error,
                        "The selected material is absent from the registered namespace template");
        return command;
    }

    const int width = tileCountX();
    const int height = tileCountY();
    QSet<int> painted;
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(width - 1, int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(height - 1, int(std::floor(brush.centerY + brush.radius)));
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
            if(available)
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

    QVector<int> orderedPainted = painted.values().toVector();
    std::sort(orderedPainted.begin(), orderedPainted.end());
    for(int index : orderedPainted)
        command.semanticAudit.paintedCoreCells.append(QPoint(index % width, index / width));

    STerrainTransitionSignature pure;
    pure.northWest = pure.northEast = pure.southEast = pure.southWest = materialId;
    bool pureAvailable = false;
    catalog.selectForPaint(pure, 0, 0, distributionSeed, &pureAvailable);
    if(!pureAvailable)
    {
        if(report)
            report->add("terrain.semantic_core_unsupported", EValidationSeverity::Error,
                        "The atlas has no uniform tile for the requested paint material",
                        QString::number(materialId));
        return command;
    }

    // The only mutable cells outside the brush are the immediate eight-neighbor
    // ring. Missing transitions never enlarge this footprint.
    QSet<int> boundary;
    for(int index : painted)
    {
        const int x = index % width;
        const int y = index / width;
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const int candidateX = x + offsetX;
                const int candidateY = y + offsetY;
                if(candidateX < 0 || candidateX >= width
                   || candidateY < 0 || candidateY >= height)
                    continue;
                const int candidate = candidateY * width + candidateX;
                if(painted.contains(candidate))
                    continue;
                bool available = false;
                packedTile(candidateX, candidateY, layer, &available);
                if(available)
                    boundary.insert(candidate);
            }
        }
    }
    QVector<int> orderedBoundary = boundary.values().toVector();
    std::sort(orderedBoundary.begin(), orderedBoundary.end());
    for(int index : orderedPainted)
        command.semanticAudit.affectedCells.append(QPoint(index % width, index / width));
    for(int index : orderedBoundary)
        command.semanticAudit.affectedCells.append(QPoint(index % width, index / width));

    QSet<int> coreCorners;
    for(int index : painted)
    {
        const int x = index % width;
        const int y = index / width;
        coreCorners.insert(y * (width + 1) + x);
        coreCorners.insert(y * (width + 1) + x + 1);
        coreCorners.insert((y + 1) * (width + 1) + x + 1);
        coreCorners.insert((y + 1) * (width + 1) + x);
    }

    QMap<int, STerrainTransitionSignature> existingSignatures;
    QMap<int, quint16> existingTiles;
    QMap<qint32, int> backgroundCounts;
    const auto decodeCell = [&](int index) -> bool {
        const int x = index % width;
        const int y = index / width;
        bool available = false;
        const quint16 packed = packedTile(x, y, layer, &available);
        bool found = false;
        const STerrainTransitionSignature signature = available
            ? catalog.gridSignatureForPacked(packed, &found)
            : STerrainTransitionSignature();
        if(!available || !found)
        {
            if(report)
                report->add("terrain.semantic_catalog", EValidationSeverity::Error,
                            "An editable tile is absent from the registered namespace template",
                            QString("%1,%2 packed:0x%3").arg(x).arg(y)
                                .arg(packed, 4, 16, QLatin1Char('0')));
            return false;
        }
        existingTiles.insert(index, packed);
        existingSignatures.insert(index, signature);
        return true;
    };
    for(int index : orderedPainted)
        if(!decodeCell(index))
            return command;
    for(int index : orderedBoundary)
    {
        if(!decodeCell(index))
            return command;
        const STerrainTransitionSignature signature = existingSignatures.value(index);
        const qint32 values[4] = {signature.northWest, signature.northEast,
                                  signature.southEast, signature.southWest};
        for(qint32 value : values)
            if(value != materialId)
                backgroundCounts[value] += 1;
    }
    qint32 backgroundMaterial = materialId;
    int backgroundSupport = -1;
    for(auto entry = backgroundCounts.constBegin(); entry != backgroundCounts.constEnd(); ++entry)
    {
        if(entry.value() > backgroundSupport)
        {
            backgroundMaterial = entry.key();
            backgroundSupport = entry.value();
        }
    }
    command.semanticAudit.backgroundMaterialId = backgroundMaterial;
    command.semanticAudit.materialPath = backgroundMaterial == materialId
        ? QVector<qint32>({materialId})
        : QVector<qint32>({materialId, backgroundMaterial});

    const auto appendDelta = [&](int index, quint16 after,
                                 const STerrainTransitionSignature& requested) {
        const quint16 before = existingTiles.value(index);
        if(before == after)
            return;
        STerrainTileDelta delta;
        delta.globalX = index % width;
        delta.globalY = index / width;
        delta.layer = layer;
        delta.before = before;
        delta.after = after;
        delta.hasSemanticRequest = true;
        delta.brushCenterX = qRound(brush.centerX);
        delta.brushCenterY = qRound(brush.centerY);
        delta.requestedMaterialId = materialId;
        delta.requestedSignature = requested;
        command.deltas.append(delta);
    };

    // Core coverage is non-negotiable. Atlas limitations may affect only the
    // transition ring, never whether the user's requested cells get painted.
    for(int index : orderedPainted)
    {
        const STerrainTransitionSignature existing = existingSignatures.value(index);
        if(existing == pure)
            continue;
        const int x = index % width;
        const int y = index / width;
        bool found = false;
        const quint16 after = catalog.selectForPaint(
            pure, x, y, distributionSeed, &found);
        if(!found)
        {
            if(report)
                report->add("terrain.semantic_core_unsupported", EValidationSeverity::Error,
                            "The atlas stopped resolving a uniform requested paint tile",
                            QString("%1,%2").arg(x).arg(y));
            command.deltas.clear();
            return command;
        }
        appendDelta(index, after, pure);
    }

    const QVector<STerrainTransitionSignature> authored = catalog.gridSignatures();
    const auto valuesOf = [](const STerrainTransitionSignature& signature) {
        return QVector<qint32>({signature.northWest, signature.northEast,
                                signature.southEast, signature.southWest});
    };
    const auto lexicallyEarlier = [](const STerrainTransitionSignature& left,
                                     const STerrainTransitionSignature& right) {
        if(left.northWest != right.northWest)
            return left.northWest < right.northWest;
        if(left.northEast != right.northEast)
            return left.northEast < right.northEast;
        if(left.southEast != right.southEast)
            return left.southEast < right.southEast;
        return left.southWest < right.southWest;
    };
    int nearestCount = 0;
    int hardEdgeCount = 0;
    int candidateChecks = 0;
    const auto outerVertexSupports = [&](int vertexKey, int sourceX,
                                         int sourceY, qint32 material) {
        const int vertexX = vertexKey % (width + 1);
        const int vertexY = vertexKey / (width + 1);
        const int cellX[4] = {vertexX - 1, vertexX,
                              vertexX - 1, vertexX};
        const int cellY[4] = {vertexY - 1, vertexY - 1,
                              vertexY, vertexY};
        // Corresponding corner in NW, NE, SE, SW order for the four cells.
        const int cellCorner[4] = {2, 3, 1, 0};
        for(int neighbor = 0; neighbor < 4; ++neighbor)
        {
            const int neighborX = cellX[neighbor];
            const int neighborY = cellY[neighbor];
            if(neighborX < 0 || neighborX >= width
               || neighborY < 0 || neighborY >= height
               || (neighborX == sourceX && neighborY == sourceY))
                continue;
            bool available = false;
            const quint16 packed = packedTile(
                neighborX, neighborY, layer, &available);
            if(!available)
                continue;
            bool found = false;
            const STerrainTransitionSignature signature =
                catalog.gridSignatureForPacked(packed, &found);
            if(!found)
                return false;
            const qint32 values[4] = {signature.northWest,
                                      signature.northEast,
                                      signature.southEast,
                                      signature.southWest};
            if(values[cellCorner[neighbor]] != material)
                return false;
        }
        return true;
    };
    for(int index : orderedBoundary)
    {
        const int x = index % width;
        const int y = index / width;
        const int cornerKeys[4] = {y * (width + 1) + x,
                                   y * (width + 1) + x + 1,
                                   (y + 1) * (width + 1) + x + 1,
                                   (y + 1) * (width + 1) + x};
        const STerrainTransitionSignature existing = existingSignatures.value(index);
        const QVector<qint32> existingValues = valuesOf(existing);
        QVector<qint32> desiredValues = existingValues;
        int targetCornerCount = 0;
        for(int corner = 0; corner < 4; ++corner)
        {
            if(coreCorners.contains(cornerKeys[corner]))
            {
                desiredValues[corner] = materialId;
                ++targetCornerCount;
            }
        }
        if(targetCornerCount == 0)
            continue;
        STerrainTransitionSignature desired;
        desired.northWest = desiredValues.at(0);
        desired.northEast = desiredValues.at(1);
        desired.southEast = desiredValues.at(2);
        desired.southWest = desiredValues.at(3);
        if(desired == existing)
            continue;

        bool exactFound = false;
        quint16 after = catalog.selectForPaint(
            desired, x, y, distributionSeed, &exactFound);
        STerrainTransitionSignature selected = desired;
        if(!exactFound)
        {
            // Nearest candidates remain topologically anchored to the core and
            // may use only families already present in this exact boundary tile.
            QSet<qint32> allowedMaterials;
            allowedMaterials.insert(materialId);
            for(qint32 value : existingValues)
                allowedMaterials.insert(value);
            int bestCost = std::numeric_limits<int>::max();
            int bestMismatchCount = std::numeric_limits<int>::max();
            bool nearestFound = false;
            STerrainTransitionSignature nearest;
            for(const STerrainTransitionSignature& candidate : authored)
            {
                ++candidateChecks;
                const QVector<qint32> candidateValues = valuesOf(candidate);
                bool valid = true;
                bool retainsBackground = false;
                int mismatchCount = 0;
                int cost = 0;
                for(int corner = 0; corner < 4; ++corner)
                {
                    const qint32 value = candidateValues.at(corner);
                    if(!allowedMaterials.contains(value)
                       || (coreCorners.contains(cornerKeys[corner]) && value != materialId))
                    {
                        valid = false;
                        break;
                    }
                    if(!coreCorners.contains(cornerKeys[corner]) && value != materialId)
                        retainsBackground = true;
                    if(value != desiredValues.at(corner))
                    {
                        // A one-corner approximation is safe only when every
                        // existing tile sharing that outer vertex already
                        // agrees with it. Otherwise the approximation would
                        // manufacture a visible seam disagreement.
                        if(!outerVertexSupports(cornerKeys[corner], x, y, value))
                        {
                            valid = false;
                            break;
                        }
                        ++mismatchCount;
                        cost += 16;
                    }
                    if(value != existingValues.at(corner))
                        cost += 2;
                }
                // More than one wrong corner is too dissimilar: keep a hard
                // seam instead of turning the transition ring into extra paint.
                if(!valid || !retainsBackground || mismatchCount > 1)
                    continue;
                if(!nearestFound || cost < bestCost
                   || (cost == bestCost && mismatchCount < bestMismatchCount)
                   || (cost == bestCost && mismatchCount == bestMismatchCount
                       && lexicallyEarlier(candidate, nearest)))
                {
                    bestCost = cost;
                    bestMismatchCount = mismatchCount;
                    nearest = candidate;
                    nearestFound = true;
                }
            }
            if(nearestFound)
            {
                bool packedFound = false;
                after = catalog.selectForPaint(
                    nearest, x, y, distributionSeed, &packedFound);
                if(packedFound)
                {
                    selected = nearest;
                    ++nearestCount;
                }
                else
                    nearestFound = false;
            }
            if(!nearestFound)
            {
                ++hardEdgeCount;
                continue;
            }
        }
        if(!(selected == existing))
            appendDelta(index, after, selected);
    }

    command.semanticAudit.solverNodes = candidateChecks;
    command.semanticAudit.solverMode = hardEdgeCount > 0
        ? QStringLiteral("bounded-hard-edge")
        : nearestCount > 0
            ? QStringLiteral("bounded-nearest")
            : QStringLiteral("bounded-exact");
    if(nearestCount > 0 && report)
        report->add("terrain.semantic_nearest", EValidationSeverity::Warning,
                    "Some boundary tiles used a constrained nearest authored transition",
                    QString::number(nearestCount));
    if(hardEdgeCount > 0 && report)
        report->add("terrain.semantic_hard_edge", EValidationSeverity::Warning,
                    "Some unsupported boundary transitions were left as hard seams",
                    QString::number(hardEdgeCount));
    return command;
}

STerrainTileCommand CTerrainEditModel::makeSemanticTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, CValidationReport* report) const
{
    return makeBoundedSemanticTilePaint(
        brush, layer, materialId, catalog, distributionSeed, report);
#if 0
    CValidationReport compactReport;
    STerrainTileCommand compact = makeSemanticTilePaintAttempt(
        brush, layer, materialId, catalog, distributionSeed, 0, true,
        &compactReport);
    bool retryExpanded = false;
    bool hasOtherError = false;
    for(const SValidationFinding& finding : compactReport.findings())
    {
        if(finding.severity != EValidationSeverity::Error)
            continue;
        if(finding.code == QStringLiteral("terrain.semantic_multifamily"))
            retryExpanded = true;
        else
            hasOtherError = true;
    }
    if(!compact.isEmpty() || !retryExpanded || hasOtherError)
    {
        if(report)
            for(const SValidationFinding& finding : compactReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        return compact;
    }

    CValidationReport completeRouteReport;
    STerrainTileCommand completeRoute = makeSemanticTilePaintAttempt(
        brush, layer, materialId, catalog, distributionSeed, 0, false,
        &completeRouteReport);
    if(!completeRoute.isEmpty() || !completeRouteReport.hasErrors())
    {
        if(report)
            for(const SValidationFinding& finding : completeRouteReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        return completeRoute;
    }

    // Most strokes must remain within the route's natural footprint. Only an
    // actually unsatisfied compact CSP may borrow one additional constrained
    // ring for an authored multi-family junction.
    CValidationReport expandedReport;
    STerrainTileCommand expanded = makeSemanticTilePaintAttempt(
        brush, layer, materialId, catalog, distributionSeed, 1, false,
        &expandedReport);
    if(report)
        for(const SValidationFinding& finding : expandedReport.findings())
            report->add(finding.code, finding.severity,
                        finding.message, finding.location);
    return expanded;
#endif
}

#if 0
STerrainTileCommand CTerrainEditModel::makeSemanticTilePaintAttempt(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, int junctionPadding,
    bool allowObservedDirectRoute,
    CValidationReport* report) const
{
    STerrainTileCommand command;
    command.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint terrain material")
        : QStringLiteral("Paint overlay material");
    command.semanticAudit.valid = true;
    command.semanticAudit.brush = brush;
    command.semanticAudit.layer = layer;
    command.semanticAudit.requestedMaterialId = materialId;
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
    for(int index : painted)
        command.semanticAudit.paintedCoreCells.append(QPoint(index % width, index / width));

    // Choose the dominant uniform family immediately around the stroke. Stock
    // atlases do not contain every direct family pairing, so route through the
    // shortest sequence of authored boundary-compatible families when needed.
    QMap<qint32, int> backgroundCounts;
    QSet<QPair<qint32, qint32>> observedDirectPairs;
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
            ? catalog.gridSignatureForPacked(current, &found)
            : STerrainTransitionSignature();
        if(!found)
            continue;
        const qint32 corners[4] = {signature.northWest, signature.northEast,
                                   signature.southEast, signature.southWest};
        QSet<qint32> present;
        for(qint32 candidate : corners)
        {
            present.insert(candidate);
            STerrainTransitionSignature pure;
            pure.northWest = pure.northEast = pure.southEast = pure.southWest = candidate;
            bool pureFound = false;
            catalog.select(pure, 0, 0, 0, &pureFound);
            if(pureFound)
                backgroundCounts[candidate] += 1;
        }
        if(present.size() == 2)
        {
            QVector<qint32> pair = present.values().toVector();
            std::sort(pair.begin(), pair.end());
            observedDirectPairs.insert(qMakePair(pair.at(0), pair.at(1)));
        }
    }
    qint32 backgroundMaterial = materialId;
    int bestCount = -1;
    // Existing selected material at the perimeter means this stroke is
    // continuing an earlier painted region; it is not the family into which
    // the new boundary must blend. Prefer any authored non-target family and
    // fall back to the target only when the entire perimeter is already target.
    for(auto entry = backgroundCounts.constBegin(); entry != backgroundCounts.constEnd(); ++entry)
    {
        if(entry.key() == materialId)
            continue;
        if(entry.value() > bestCount)
        {
            backgroundMaterial = entry.key();
            bestCount = entry.value();
        }
    }
    QVector<qint32> materialPath;
    const QPair<qint32, qint32> requestedPair = qMakePair(
        qMin(materialId, backgroundMaterial), qMax(materialId, backgroundMaterial));
    if(allowObservedDirectRoute && observedDirectPairs.contains(requestedPair))
        materialPath = {materialId, backgroundMaterial};
    else
        materialPath = catalog.shortestMaterialPath(
            materialId, backgroundMaterial, backgroundCounts);
    command.semanticAudit.backgroundMaterialId = backgroundMaterial;
    command.semanticAudit.materialPath = materialPath;
    if(materialPath.isEmpty())
    {
        if(report)
            report->add("terrain.semantic_route", EValidationSeverity::Error,
                        "The namespace template has no complete transition route between materials",
                        QString("%1 -> %2").arg(materialId).arg(backgroundMaterial));
        return command;
    }

    // The authored material route determines the compact footprint. The public
    // entry point retries with one additional constrained ring only after this
    // compact attempt proves unsatisfiable.
    command.semanticAudit.junctionPadding = junctionPadding;
    const int transitionRadius = qMax(0, materialPath.size() - 1) + junctionPadding;
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
    for(int index : affected)
        command.semanticAudit.affectedCells.append(QPoint(index % width, index / width));

    // Decode only the routed footprint. Existing shipped maps may contain
    // abstract corner mismatches, so the generated patch is canonicalized
    // locally while all cells outside this bounded route remain untouched.
    QMap<int, STerrainTransitionSignature> existingSignatures;
    QMap<int, quint16> existingTiles;
    QSet<int> context = affected;
    QSet<int> affectedCorners;
    QSet<int> paintedCorners;
    for(int index : affected)
    {
        const int x = index % width;
        const int y = index / width;
        affectedCorners.insert(y * (width + 1) + x);
        affectedCorners.insert(y * (width + 1) + x + 1);
        affectedCorners.insert((y + 1) * (width + 1) + x);
        affectedCorners.insert((y + 1) * (width + 1) + x + 1);
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                const int contextX = x + offsetX;
                const int contextY = y + offsetY;
                if(contextX >= 0 && contextX < width
                   && contextY >= 0 && contextY < height)
                    context.insert(contextY * width + contextX);
            }
        }
    }
    for(int index : painted)
    {
        const int x = index % width;
        const int y = index / width;
        paintedCorners.insert(y * (width + 1) + x);
        paintedCorners.insert(y * (width + 1) + x + 1);
        paintedCorners.insert((y + 1) * (width + 1) + x);
        paintedCorners.insert((y + 1) * (width + 1) + x + 1);
    }
    QMap<int, STerrainTransitionSignature> contextSignatures;
    for(int index : context)
    {
        const int x = index % width;
        const int y = index / width;
        bool available = false;
        const quint16 current = packedTile(x, y, layer, &available);
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(current, &found);
        if(!available || !found)
        {
            if(affected.contains(index) && report)
                report->add("terrain.semantic_catalog", EValidationSeverity::Error,
                            "An existing tile is absent from the registered namespace template",
                            QString("%1,%2 packed:0x%3").arg(x).arg(y)
                                .arg(current, 4, 16, QLatin1Char('0')));
            if(affected.contains(index))
                return command;
            continue;
        }
        contextSignatures.insert(index, signature);
        if(affected.contains(index))
        {
            existingSignatures.insert(index, signature);
            existingTiles.insert(index, current);
        }
    }

    // Preserve the semantic identity already visible inside the mutable
    // footprint. A merely edge-valid first solution can otherwise replace a
    // nearby minority family with the dominant background even when the atlas
    // contains an authored three-family junction.
    QMap<int, QMap<qint32, int>> existingCornerCounts;
    for(auto entry = existingSignatures.constBegin();
        entry != existingSignatures.constEnd(); ++entry)
    {
        const int x = entry.key() % width;
        const int y = entry.key() / width;
        const STerrainTransitionSignature& signature = entry.value();
        const qint32 values[4] = {signature.northWest, signature.northEast,
                                  signature.southEast, signature.southWest};
        const int keys[4] = {y * (width + 1) + x,
                             y * (width + 1) + x + 1,
                             (y + 1) * (width + 1) + x + 1,
                             (y + 1) * (width + 1) + x};
        for(int corner = 0; corner < 4; ++corner)
            existingCornerCounts[keys[corner]][values[corner]] += 1;
    }
    QMap<int, qint32> existingCornerMaterials;
    QMap<qint32, int> footprintMaterialSupport;
    for(auto corner = existingCornerCounts.constBegin();
        corner != existingCornerCounts.constEnd(); ++corner)
    {
        qint32 selected = 0;
        int selectedCount = -1;
        for(auto candidate = corner.value().constBegin();
            candidate != corner.value().constEnd(); ++candidate)
        {
            if(candidate.value() > selectedCount)
            {
                selected = candidate.key();
                selectedCount = candidate.value();
            }
        }
        if(selectedCount >= 0)
        {
            existingCornerMaterials.insert(corner.key(), selected);
            footprintMaterialSupport[selected] += 1;
        }
    }

    // Corners shared with an untouched halo cell are hard boundary
    // constraints. Preserving that cell's authored corner allows the new
    // route to form an atlas-authored three-or-more-family junction instead
    // of finishing the selected/background route with a third-family seam.
    QMap<int, QMap<qint32, int>> boundaryCounts;
    for(auto entry = contextSignatures.constBegin(); entry != contextSignatures.constEnd(); ++entry)
    {
        if(affected.contains(entry.key()))
            continue;
        const int x = entry.key() % width;
        const int y = entry.key() / width;
        const STerrainTransitionSignature& signature = entry.value();
        const qint32 materials[4] = {signature.northWest, signature.northEast,
                                     signature.southEast, signature.southWest};
        const int cornerKeys[4] = {
            y * (width + 1) + x,
            y * (width + 1) + x + 1,
            (y + 1) * (width + 1) + x + 1,
            (y + 1) * (width + 1) + x
        };
        for(int corner = 0; corner < 4; ++corner)
        {
            if(affectedCorners.contains(cornerKeys[corner])
               && !paintedCorners.contains(cornerKeys[corner]))
                boundaryCounts[cornerKeys[corner]][materials[corner]] += 1;
        }
    }
    QMap<int, qint32> boundaryMaterials;
    bool contradictoryBoundary = false;
    for(auto corner = boundaryCounts.constBegin(); corner != boundaryCounts.constEnd(); ++corner)
    {
        contradictoryBoundary |= corner.value().size() > 1;
        qint32 selectedMaterial = 0;
        int selectedCount = -1;
        for(auto candidate = corner.value().constBegin();
            candidate != corner.value().constEnd(); ++candidate)
        {
            if(candidate.value() > selectedCount)
            {
                selectedMaterial = candidate.key();
                selectedCount = candidate.value();
            }
        }
        if(selectedCount >= 0)
            boundaryMaterials.insert(corner.key(), selectedMaterial);
    }

    // Merge with nearby strokes by seeding the route distance from the current
    // decoded corner lattice. Minimum distance deliberately favors the selected
    // material where shipped neighboring tiles disagree at a shared corner.
    for(auto entry = contextSignatures.constBegin(); entry != contextSignatures.constEnd(); ++entry)
    {
        const int x = entry.key() % width;
        const int y = entry.key() / width;
        const STerrainTransitionSignature& signature = entry.value();
        const qint32 materials[4] = {signature.northWest, signature.northEast,
                                     signature.southEast, signature.southWest};
        const int cornerX[4] = {x, x + 1, x + 1, x};
        const int cornerY[4] = {y, y, y + 1, y + 1};
        for(int corner = 0; corner < 4; ++corner)
        {
            const int pathDistance = materialPath.indexOf(materials[corner]);
            if(pathDistance < 0)
                continue;
            const int key = cornerY[corner] * (width + 1) + cornerX[corner];
            if(!cornerDistances.contains(key)
               || pathDistance < cornerDistances.value(key))
                cornerDistances.insert(key, pathDistance);
        }
    }

    const auto pairwiseMaterial = [&](int key) -> qint32
    {
        const int distance = cornerDistances.value(key, transitionRadius);
        return materialPath.at(qMin(distance, materialPath.size() - 1));
    };
    QMap<int, qint32> pairwiseCorners;
    QMap<int, qint32> paintedDesiredCorners;
    for(int key : paintedCorners)
    {
        const qint32 existing = existingCornerMaterials.value(key, materialId);
        paintedDesiredCorners.insert(key,
            materialPath.contains(existing) ? materialId : existing);
    }
    QMap<int, qint32> preferredCorners;
    for(int key : affectedCorners)
    {
        pairwiseCorners.insert(key, pairwiseMaterial(key));
        preferredCorners.insert(key,
            paintedCorners.contains(key) ? paintedDesiredCorners.value(key)
            : boundaryMaterials.contains(key) ? boundaryMaterials.value(key)
            : existingCornerMaterials.value(key, pairwiseCorners.value(key)));
    }

    // Painted core ownership and untouched halo edges are fixed. All other
    // local corners may move between authored materials so the complete patch
    // can use valid atlas signatures without breaking a shared edge.
    QMap<int, qint32> fixedCorners = boundaryMaterials;
    for(int key : paintedCorners)
        fixedCorners.insert(key, paintedDesiredCorners.value(key));

    const auto signatureAt = [&](const QMap<int, qint32>& corners,
                                 int x, int y) {
        STerrainTransitionSignature signature;
        signature.northWest = corners.value(y * (width + 1) + x);
        signature.northEast = corners.value(y * (width + 1) + x + 1);
        signature.southEast = corners.value((y + 1) * (width + 1) + x + 1);
        signature.southWest = corners.value((y + 1) * (width + 1) + x);
        return signature;
    };

    for(int index : painted)
    {
        const int x = index % width;
        const int y = index / width;
        bool coreFound = false;
        const STerrainTransitionSignature core = signatureAt(
            paintedDesiredCorners, x, y);
        catalog.selectForPaint(core, x, y, distributionSeed, &coreFound);
        if(!coreFound)
        {
            if(report)
                report->add("terrain.semantic_core_unsupported",
                            EValidationSeverity::Error,
                            "The atlas has no authored tile for the requested core blend",
                            QString("%1,%2 corners:%3,%4,%5,%6")
                                .arg(x).arg(y).arg(core.northWest).arg(core.northEast)
                                .arg(core.southEast).arg(core.southWest));
            return command;
        }
    }

    QMap<int, qint32> solvedCorners = preferredCorners;
    bool preferredAvailable = true;
    for(int index : affected)
    {
        const int x = index % width;
        const int y = index / width;
        bool found = false;
        catalog.selectForPaint(signatureAt(preferredCorners, x, y),
                               x, y, distributionSeed, &found);
        if(!found)
        {
            preferredAvailable = false;
            break;
        }
    }

    if(!preferredAvailable)
    {
        command.semanticAudit.solverMode = QStringLiteral("junction-csp");
        const auto materialAllowedAtCorner = [&](qint32 candidate, int key) {
            if(fixedCorners.contains(key))
                return fixedCorners.value(key) == candidate;
            const int pathIndex = materialPath.indexOf(candidate);
            if(pathIndex >= 0)
            {
                const int expectedDistance = qMin(
                    cornerDistances.value(key, transitionRadius), materialPath.size() - 1);
                if(qAbs(pathIndex - expectedDistance) <= 1)
                    return true;
            }

            // A third/fourth family may participate only beside a corner where
            // it already exists. Context membership alone must never permit a
            // material to teleport across the whole solver footprint.
            const int cornerX = key % (width + 1);
            const int cornerY = key / (width + 1);
            for(int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for(int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    const int sampleX = cornerX + offsetX;
                    const int sampleY = cornerY + offsetY;
                    if(sampleX < 0 || sampleX > width || sampleY < 0 || sampleY > height)
                        continue;
                    const int sampleKey = sampleY * (width + 1) + sampleX;
                    if(existingCornerCounts.value(sampleKey).contains(candidate))
                        return true;
                }
            }
            return false;
        };

        const QVector<STerrainTransitionSignature> authored = catalog.gridSignatures();
        QMap<int, QVector<STerrainTransitionSignature>> candidates;
        const auto cornerValues = [&](const STerrainTransitionSignature& signature) {
            return QVector<qint32>({signature.northWest, signature.northEast,
                                    signature.southEast, signature.southWest});
        };
        const auto cornerKeys = [&](int x, int y) {
            return QVector<int>({y * (width + 1) + x,
                                 y * (width + 1) + x + 1,
                                 (y + 1) * (width + 1) + x + 1,
                                 (y + 1) * (width + 1) + x});
        };
        QMap<int, QMap<qint32, int>> localSupportByCorner;
        for(int key : affectedCorners)
        {
            const int cornerX = key % (width + 1);
            const int cornerY = key / (width + 1);
            for(int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for(int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    const int sampleX = cornerX + offsetX;
                    const int sampleY = cornerY + offsetY;
                    if(sampleX < 0 || sampleX > width
                       || sampleY < 0 || sampleY > height)
                        continue;
                    const int sampleKey = sampleY * (width + 1) + sampleX;
                    const QMap<qint32, int>& sample = existingCornerCounts.value(sampleKey);
                    for(auto material = sample.constBegin(); material != sample.constEnd(); ++material)
                        localSupportByCorner[key][material.key()] += material.value();
                }
            }
        }
        const auto cornerAssignmentCost = [&](int key, qint32 candidate) {
            if(fixedCorners.contains(key))
                return 0;
            int cost = candidate != preferredCorners.value(key, candidate);
            if(!existingCornerMaterials.contains(key))
                return cost;
            const qint32 existing = existingCornerMaterials.value(key);
            if(candidate == existing)
                return cost;
            const int preservationWeight = existing != materialId ? 8 : 2;
            cost += preservationWeight;
            cost += qMax(0, localSupportByCorner.value(key).value(existing)
                             - localSupportByCorner.value(key).value(candidate));
            if(junctionPadding > 0
               && materialPath.contains(existing) != materialPath.contains(candidate))
            {
                // Crossing between the active route and an unrelated family
                // is a last resort, not an ordinary equal-cost tie. Preserve
                // existing third-family islands most strongly. If the route
                // must retreat, favor the family with greater footprint-wide
                // support so a local island shrinks rather than squares off.
                cost += 100000;
                if(!materialPath.contains(existing))
                    cost += 50000;
                else
                    cost += 1000 * qMax(0,
                        footprintMaterialSupport.value(existing)
                            - footprintMaterialSupport.value(candidate));
            }
            return cost;
        };
        bool constraintCandidateMissing = false;
        for(int index : affected)
        {
            const int x = index % width;
            const int y = index / width;
            const QVector<int> keys = cornerKeys(x, y);
            QVector<STerrainTransitionSignature>& cellCandidates = candidates[index];
            for(const STerrainTransitionSignature& signature : authored)
            {
                const QVector<qint32> values = cornerValues(signature);
                bool compatible = true;
                for(int corner = 0; corner < 4; ++corner)
                {
                    if(!materialAllowedAtCorner(values.at(corner), keys.at(corner)))
                    {
                        compatible = false;
                        break;
                    }
                }
                if(compatible)
                    cellCandidates.append(signature);
            }
            std::sort(cellCandidates.begin(), cellCandidates.end(),
                [&](const STerrainTransitionSignature& left,
                    const STerrainTransitionSignature& right) {
                    const QVector<qint32> leftValues = cornerValues(left);
                    const QVector<qint32> rightValues = cornerValues(right);
                    int leftDifference = 0;
                    int rightDifference = 0;
                    for(int corner = 0; corner < 4; ++corner)
                    {
                        const int key = keys.at(corner);
                        leftDifference += cornerAssignmentCost(key, leftValues.at(corner));
                        rightDifference += cornerAssignmentCost(key, rightValues.at(corner));
                    }
                    if(leftDifference != rightDifference)
                        return leftDifference < rightDifference;
                    if(left.northWest != right.northWest)
                        return left.northWest < right.northWest;
                    if(left.northEast != right.northEast)
                        return left.northEast < right.northEast;
                    if(left.southEast != right.southEast)
                        return left.southEast < right.southEast;
                    return left.southWest < right.southWest;
                });
            if(cellCandidates.isEmpty())
            {
                constraintCandidateMissing = true;
                break;
            }
        }

        QMap<int, qint32> assignments = fixedCorners;
        QSet<int> remaining = affected;
        int searchNodes = 0;
        const int searchLimit = 100000;
        command.semanticAudit.solverLimit = searchLimit;
        int bestScore = std::numeric_limits<int>::max();
        QMap<int, qint32> bestAssignments;
        std::function<void(int)> solve = [&](int score) {
            if(remaining.isEmpty())
            {
                if(score < bestScore)
                {
                    bestScore = score;
                    bestAssignments = assignments;
                }
                return;
            }
            if(++searchNodes > searchLimit)
                return;
            if(score >= bestScore)
                return;

            int selectedCell = -1;
            QVector<STerrainTransitionSignature> selectedCandidates;
            for(int index : remaining)
            {
                const int x = index % width;
                const int y = index / width;
                const QVector<int> keys = cornerKeys(x, y);
                QVector<STerrainTransitionSignature> compatible;
                for(const STerrainTransitionSignature& signature : candidates.value(index))
                {
                    const QVector<qint32> values = cornerValues(signature);
                    bool matches = true;
                    for(int corner = 0; corner < 4; ++corner)
                    {
                        if(assignments.contains(keys.at(corner))
                           && assignments.value(keys.at(corner)) != values.at(corner))
                        {
                            matches = false;
                            break;
                        }
                    }
                    if(matches)
                        compatible.append(signature);
                }
                if(compatible.isEmpty())
                    return;
                if(selectedCell < 0 || compatible.size() < selectedCandidates.size())
                {
                    selectedCell = index;
                    selectedCandidates = compatible;
                    if(selectedCandidates.size() == 1)
                        break;
                }
            }

            remaining.remove(selectedCell);
            const int selectedX = selectedCell % width;
            const int selectedY = selectedCell / width;
            const QVector<int> keys = cornerKeys(selectedX, selectedY);
            for(const STerrainTransitionSignature& signature : selectedCandidates)
            {
                const QVector<qint32> values = cornerValues(signature);
                QVector<int> inserted;
                int addedCost = 0;
                for(int corner = 0; corner < 4; ++corner)
                {
                    if(!assignments.contains(keys.at(corner)))
                    {
                        assignments.insert(keys.at(corner), values.at(corner));
                        inserted.append(keys.at(corner));
                        addedCost += cornerAssignmentCost(keys.at(corner),
                                                          values.at(corner));
                    }
                }
                solve(score + addedCost);
                for(int key : inserted)
                    assignments.remove(key);
            }
            remaining.insert(selectedCell);
        };

        if(!constraintCandidateMissing)
            solve(0);
        const bool solved = !bestAssignments.isEmpty();
        if(!solved)
        {
            command.semanticAudit.solverNodes = searchNodes;
            if(contradictoryBoundary)
            {
                // Preserve established behavior only for rare authored legacy
                // neighborhoods whose outer boundary already disagrees with
                // itself. Never describe this as a solved junction.
                solvedCorners = pairwiseCorners;
                command.semanticAudit.solverMode =
                    QStringLiteral("legacy-contradictory-boundary-fallback");
            }
            else
            {
                if(report)
                    report->add("terrain.semantic_multifamily", EValidationSeverity::Error,
                                searchNodes > searchLimit
                                    ? "The bounded local terrain-junction search exceeded its safety limit"
                                    : "The atlas cannot satisfy the complete local terrain-junction boundary",
                                QString("%1 affected cells, %2 search nodes, route %3")
                                    .arg(affected.size()).arg(searchNodes)
                                    .arg([&]() {
                                        QStringList ids;
                                        for(qint32 id : materialPath)
                                            ids.append(QString::number(id));
                                        return ids.join(QStringLiteral(" -> "));
                                    }()));
                return command;
            }
        }
        else
        {
            solvedCorners = bestAssignments;
            command.semanticAudit.solverNodes = searchNodes;
        }
    }
    else
        command.semanticAudit.solverMode = QStringLiteral("preferred-direct");

    const auto routedMaterial = [&](int cornerX, int cornerY) -> qint32
    {
        return solvedCorners.value(cornerY * (width + 1) + cornerX,
                                   materialId);
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
            return command;
        }
        STerrainTileDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.layer = layer;
        delta.before = before;
        delta.after = after;
        delta.hasSemanticRequest = true;
        delta.brushCenterX = qRound(brush.centerX);
        delta.brushCenterY = qRound(brush.centerY);
        delta.requestedMaterialId = materialId;
        delta.requestedSignature = desired;
        command.deltas.append(delta);
    }
    return command;
}
#endif

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

bool CTerrainEditModel::apply(const SOverlaySurfaceCommand& command, bool forward,
                              CValidationReport* report)
{
    QSet<int> seen;
    QVector<int> indices;
    indices.reserve(command.sectors.size());
    for(const SOverlaySectorDelta& delta : command.sectors)
    {
        const int index = m_sectorLookup.value(
            QPair<int, int>(delta.sectorX, delta.sectorY), -1);
        if(index < 0 || seen.contains(index))
        {
            if(report)
                report->add("terrain.overlay_sector", EValidationSeverity::Error,
                            index < 0 ? "Overlay command references a missing sector"
                                      : "Overlay command references a sector more than once",
                            QString("%1,%2").arg(delta.sectorX).arg(delta.sectorY));
            return false;
        }
        seen.insert(index);
        const QByteArray& expected = forward ? delta.beforePayload : delta.afterPayload;
        if(m_sectors.at(index).currentPayload != expected)
        {
            if(report)
                report->add("terrain.overlay_provenance", EValidationSeverity::Error,
                            "Overlay command no longer matches the canonical SEC payload",
                            m_sectors.at(index).source.memberName);
            return false;
        }
        const QByteArray& replacement = forward ? delta.afterPayload : delta.beforePayload;
        const bool headerValid = replacement.size() >= 5
            && qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(
                   replacement.constData())) == kSectorSignature;
        const quint8 type = headerValid ? quint8(replacement.at(4)) : quint8(255);
        const int required = type == 3 ? kOverlaySectorSize : kLandSectorSize;
        if(!headerValid || (type != 0 && type != 3) || replacement.size() < required)
        {
            if(report)
                report->add("terrain.overlay_payload", EValidationSeverity::Error,
                            "Overlay command contains an invalid replacement SEC payload",
                            m_sectors.at(index).source.memberName);
            return false;
        }
        indices.append(index);
    }
    for(int commandIndex = 0; commandIndex < command.sectors.size(); ++commandIndex)
    {
        SSector& sector = m_sectors[indices.at(commandIndex)];
        const SOverlaySectorDelta& delta = command.sectors.at(commandIndex);
        sector.currentPayload = forward ? delta.afterPayload : delta.beforePayload;
        if(!refreshSectorLayout(&sector, report))
            return false;
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
    if(m_maxZ != m_originalMaxZ)
        for(int index = 0; index < m_heights.size(); ++index)
            affectedNormals.insert(index);
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

bool COverlaySurfaceEditHistory::execute(CTerrainEditModel* model,
                                         const SOverlaySurfaceCommand& command,
                                         CValidationReport* report)
{
    if(!model || command.isEmpty() || !model->apply(command, true, report))
        return false;
    m_undo.append(command);
    m_redo.clear();
    return true;
}

bool COverlaySurfaceEditHistory::undo(CTerrainEditModel* model,
                                      CValidationReport* report)
{
    if(!model || m_undo.isEmpty())
        return false;
    const SOverlaySurfaceCommand command = m_undo.back();
    if(!model->apply(command, false, report))
        return false;
    m_undo.removeLast();
    m_redo.append(command);
    return true;
}

bool COverlaySurfaceEditHistory::redo(CTerrainEditModel* model,
                                      CValidationReport* report)
{
    if(!model || m_redo.isEmpty())
        return false;
    const SOverlaySurfaceCommand command = m_redo.back();
    if(!model->apply(command, true, report))
        return false;
    m_redo.removeLast();
    m_undo.append(command);
    return true;
}

void COverlaySurfaceEditHistory::clear()
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

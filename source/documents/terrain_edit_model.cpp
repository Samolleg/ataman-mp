#include "terrain_edit_model.h"
#include "terrain_boundary_fill.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QSet>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <limits>

#include "endurance_document.h"
#include "mpr_document.h"
#include "terrain_virtual_atlas.h"

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

float terrainNoiseSample(quint32 noiseSeed, int x, int y)
{
    // Stable signed coordinate noise: one seed owns a complete sculpt stroke,
    // so repeated stamps strengthen one pattern instead of rerolling it.
    quint32 value = noiseSeed ^ (quint32(x) * 0x9E3779B9u)
        ^ (quint32(y) * 0x85EBCA6Bu);
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return (float(value & 0x00FFFFFFu) / 8388607.5f) - 1.0f;
}

quint32 terrainPaintLayerSeed(quint32 strokeSeed, ETerrainTileLayer layer)
{
    return strokeSeed ^ (layer == ETerrainTileLayer::Land
        ? 0x6c616e64u : 0x6f766572u);
}

quint16 selectUniformInteriorTile(
    const CTerrainTransitionCatalog& catalog,
    const STerrainTransitionSignature& uniformSignature,
    int cellX, int cellY, quint32 distributionSeed,
    bool randomizeRotation, bool* found,
    const QSet<int>* allowedPhysicalSlots = nullptr)
{
    bool selected = false;
    const quint16 value = allowedPhysicalSlots
        ? catalog.selectForPaint(uniformSignature, cellX, cellY,
            distributionSeed, *allowedPhysicalSlots, &selected)
        : catalog.selectForPaint(uniformSignature, cellX, cellY,
            distributionSeed, &selected);
    if(found)
        *found = selected;
    if(!selected)
        return 0;
    if(randomizeRotation)
        return value;
    return catalog.lowestPaintRotationForSlot(
        uniformSignature, value, found);
}

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

STerrainRouteOwnershipPlan planConnectedTerrainRouteOwnership(
    int tileWidth, int tileHeight,
    const QVector<STerrainTransitionSignature>& currentSignatures,
    const QVector<bool>& availableCells,
    const QVector<qint32>& currentVertexMaterials,
    const QVector<qint32>& baselineVertexMaterials,
    const QVector<qint32>& exactGeneratedRouteOwners,
    const QVector<bool>& repairCandidateCells)
{
    STerrainRouteOwnershipPlan plan;
    const int cellCount = tileWidth * tileHeight;
    const int vertexWidth = tileWidth + 1;
    const int vertexCount = vertexWidth * (tileHeight + 1);
    if(tileWidth <= 0 || tileHeight <= 0
       || currentSignatures.size() != cellCount
       || availableCells.size() != cellCount
       || (!repairCandidateCells.isEmpty()
           && repairCandidateCells.size() != cellCount))
        return plan;
    const bool scopedToRepairCandidates =
        repairCandidateCells.size() == cellCount;

    struct SRouteCell
    {
        bool transition{false};
        bool junction{false};
        qint32 first{0};
        qint32 second{0};
    };
    QVector<SRouteCell> cells(cellCount);
    for(int cell = 0; cell < cellCount; ++cell)
    {
        if(!availableCells.at(cell))
            continue;
        QSet<qint32> materials;
        const STerrainTransitionSignature& value = currentSignatures.at(cell);
        for(qint32 material : {value.northWest, value.northEast,
                               value.southEast, value.southWest})
            if(material > 0)
                materials.insert(material);
        if(materials.size() > 2)
        {
            cells[cell].junction = true;
            continue;
        }
        if(materials.size() != 2)
            continue;
        QVector<qint32> ordered = materials.values().toVector();
        std::sort(ordered.begin(), ordered.end());
        cells[cell].transition = true;
        cells[cell].first = ordered.at(0);
        cells[cell].second = ordered.at(1);
    }

    const auto samePair = [&cells](int first, int second) {
        return cells.at(first).transition && cells.at(second).transition
            && cells.at(first).first == cells.at(second).first
            && cells.at(first).second == cells.at(second).second;
    };
    const auto connectedAcrossMixedEdge = [
        &currentSignatures, &cells, &samePair,
        tileWidth](int first, int second) {
        if(!samePair(first, second))
            return false;
        const int firstX = first % tileWidth;
        const int firstY = first / tileWidth;
        const int secondX = second % tileWidth;
        const int secondY = second / tileWidth;
        qint32 firstA = 0, firstB = 0, secondA = 0, secondB = 0;
        const STerrainTransitionSignature& a = currentSignatures.at(first);
        const STerrainTransitionSignature& b = currentSignatures.at(second);
        if(secondX == firstX + 1 && secondY == firstY)
        {
            firstA = a.northEast; firstB = a.southEast;
            secondA = b.northWest; secondB = b.southWest;
        }
        else if(secondX == firstX - 1 && secondY == firstY)
        {
            firstA = a.northWest; firstB = a.southWest;
            secondA = b.northEast; secondB = b.southEast;
        }
        else if(secondY == firstY + 1 && secondX == firstX)
        {
            firstA = a.southWest; firstB = a.southEast;
            secondA = b.northWest; secondB = b.northEast;
        }
        else if(secondY == firstY - 1 && secondX == firstX)
        {
            firstA = a.northWest; firstB = a.northEast;
            secondA = b.southWest; secondB = b.southEast;
        }
        else
            return false;
        return firstA == secondA && firstB == secondB
            && firstA != firstB
            && (firstA == cells.at(first).first
                || firstA == cells.at(first).second)
            && (firstB == cells.at(first).first
                || firstB == cells.at(first).second);
    };

    QVector<bool> visited(cellCount, false);
    const bool deltaFieldsAvailable =
        currentVertexMaterials.size() == vertexCount
        && baselineVertexMaterials.size() == vertexCount;
    const bool provenanceAvailable =
        exactGeneratedRouteOwners.size() == cellCount;
    for(int seed = 0; seed < cellCount; ++seed)
    {
        if(visited.at(seed) || !cells.at(seed).transition
           || (scopedToRepairCandidates
               && !repairCandidateCells.at(seed)))
            continue;
        STerrainRouteOwnershipComponent component;
        component.firstMaterial = cells.at(seed).first;
        component.secondMaterial = cells.at(seed).second;
        QVector<int> frontier;
        frontier.append(seed);
        visited[seed] = true;
        while(!frontier.isEmpty())
        {
            const int cell = frontier.takeLast();
            component.cellIndexes.append(cell);
            const int x = cell % tileWidth;
            const int y = cell / tileWidth;
            const int neighbors[4] = {
                x > 0 ? cell - 1 : -1,
                x + 1 < tileWidth ? cell + 1 : -1,
                y > 0 ? cell - tileWidth : -1,
                y + 1 < tileHeight ? cell + tileWidth : -1};
            for(int neighbor : neighbors)
                if(neighbor >= 0 && !visited.at(neighbor)
                   && connectedAcrossMixedEdge(cell, neighbor))
                {
                    visited[neighbor] = true;
                    frontier.append(neighbor);
                }
        }
        std::sort(component.cellIndexes.begin(), component.cellIndexes.end());

        bool touchesJunction = false;
        for(int cell : component.cellIndexes)
        {
            const int x = cell % tileWidth;
            const int y = cell / tileWidth;
            for(int neighborY = y - 1; neighborY <= y + 1; ++neighborY)
                for(int neighborX = x - 1; neighborX <= x + 1; ++neighborX)
                    if(neighborX >= 0 && neighborX < tileWidth
                       && neighborY >= 0 && neighborY < tileHeight
                       && cells.at(neighborY * tileWidth + neighborX).junction)
                        touchesJunction = true;
        }
        if(touchesJunction)
            component.ambiguity =
                ETerrainRouteOwnershipAmbiguity::MultiMaterialJunction;
        else
        {
            QSet<int> incidentVertices;
            for(int cell : component.cellIndexes)
            {
                const int x = cell % tileWidth;
                const int y = cell / tileWidth;
                const int northWest = y * vertexWidth + x;
                incidentVertices.insert(northWest);
                incidentVertices.insert(northWest + 1);
                incidentVertices.insert(northWest + vertexWidth);
                incidentVertices.insert(northWest + vertexWidth + 1);
            }
            QSet<qint32> deltaOwners;
            bool invalidDelta = false;
            if(deltaFieldsAvailable)
                for(int vertex : incidentVertices)
                {
                    const qint32 current = currentVertexMaterials.at(vertex);
                    const qint32 baseline = baselineVertexMaterials.at(vertex);
                    if(current == baseline)
                        continue;
                    const bool currentInPair = current == component.firstMaterial
                        || current == component.secondMaterial;
                    const bool baselineInPair = baseline == component.firstMaterial
                        || baseline == component.secondMaterial;
                    if(!currentInPair || !baselineInPair)
                    {
                        invalidDelta = true;
                        continue;
                    }
                    deltaOwners.insert(current);
                }
            if(invalidDelta || deltaOwners.size() > 1)
                component.ambiguity =
                    ETerrainRouteOwnershipAmbiguity::MixedSemanticDelta;
            else if(deltaOwners.size() == 1)
            {
                component.ownerMaterial = *deltaOwners.constBegin();
                component.evidence =
                    ETerrainRouteOwnershipEvidence::SemanticDelta;
            }
            else
            {
                QSet<qint32> provenanceOwners;
                bool missingProvenance = !provenanceAvailable;
                if(provenanceAvailable)
                    for(int cell : component.cellIndexes)
                    {
                        const qint32 owner = exactGeneratedRouteOwners.at(cell);
                        if(owner != component.firstMaterial
                           && owner != component.secondMaterial)
                            missingProvenance = true;
                        else
                            provenanceOwners.insert(owner);
                    }
                if(missingProvenance || provenanceOwners.isEmpty())
                    component.ambiguity =
                        ETerrainRouteOwnershipAmbiguity::MissingEvidence;
                else if(provenanceOwners.size() > 1)
                    component.ambiguity =
                        ETerrainRouteOwnershipAmbiguity::MixedGeneratedProvenance;
                else
                {
                    component.ownerMaterial = *provenanceOwners.constBegin();
                    component.evidence =
                        ETerrainRouteOwnershipEvidence::GeneratedProvenance;
                }
            }
        }

        const int componentIndex = plan.components.size();
        for(int cell : component.cellIndexes)
            plan.componentByCell.insert(cell, componentIndex);
        if(component.ownerMaterial > 0
           && component.ambiguity
                == ETerrainRouteOwnershipAmbiguity::None)
            for(int cell : component.cellIndexes)
                plan.ownerByCell.insert(cell, component.ownerMaterial);
        else
            for(int cell : component.cellIndexes)
                plan.preservedCells.insert(cell);
        plan.components.append(component);
    }
    return plan;
}

void appendTerrainTileCommand(STerrainTileCommand* stroke,
                              const STerrainTileCommand& stamp)
{
    if(!stroke)
        return;
    // The release resolver needs the brush identity for equal-coverage
    // runtime terrain-type ties. Preserve it while composing incremental
    // preview stamps into the one undoable stroke; previously only tile and
    // vertex deltas survived, so the release path saw the default material 0
    // and refused otherwise valid 2/2 transitions.
    if(stamp.semanticAudit.valid)
    {
        if(!stroke->semanticAudit.valid)
            stroke->semanticAudit = stamp.semanticAudit;
        else
        {
            stroke->semanticAudit.requestedMaterialId =
                stamp.semanticAudit.requestedMaterialId;
            stroke->semanticAudit.brush = stamp.semanticAudit.brush;
            stroke->semanticAudit.layer = stamp.semanticAudit.layer;
            stroke->semanticAudit.solverMode = stamp.semanticAudit.solverMode;
            stroke->semanticAudit.paintedCoreCells +=
                stamp.semanticAudit.paintedCoreCells;
            stroke->semanticAudit.paintedVertices +=
                stamp.semanticAudit.paintedVertices;
            stroke->semanticAudit.affectedCells +=
                stamp.semanticAudit.affectedCells;
        }
    }
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
    for(const STerrainTileCommand::SVirtualVertexDelta& delta:stamp.virtualVertices)
    {
        int fieldIndex = -1;
        for(int index = 0; index < stroke->virtualFields.size(); ++index)
            if(stroke->virtualFields.at(index).layer == delta.layer)
            {
                fieldIndex = index;
                break;
            }
        if(fieldIndex >= 0)
        {
            auto& field = stroke->virtualFields[fieldIndex];
            const int key = delta.globalY * field.vertexWidth + delta.globalX;
            if(key >= 0 && key < field.afterCurrent.size()
               && field.afterCurrent.at(key) == delta.before)
            {
                field.afterCurrent[key] = delta.after;
                if(delta.updatesBaseline
                   && key < field.afterBaseline.size()
                   && field.afterBaseline.at(key) == delta.beforeBaseline)
                    field.afterBaseline[key] = delta.afterBaseline;
            }
            continue;
        }
        int existingIndex=-1;
        for(int index=0;index<stroke->virtualVertices.size();++index)
        {
            const auto& existing=stroke->virtualVertices.at(index);
            if(existing.globalX==delta.globalX&&existing.globalY==delta.globalY
               &&existing.layer==delta.layer){existingIndex=index;break;}
        }
        if(existingIndex<0)stroke->virtualVertices.append(delta);
        else
        {
            auto& existing=stroke->virtualVertices[existingIndex];
            existing.after=delta.after;
            if(delta.updatesBaseline)
            {
                if(!existing.updatesBaseline)
                    existing.beforeBaseline = delta.beforeBaseline;
                existing.updatesBaseline = true;
                existing.afterBaseline = delta.afterBaseline;
            }
            if(existing.before==existing.after
               && (!existing.updatesBaseline
                   || existing.beforeBaseline==existing.afterBaseline))
                stroke->virtualVertices.remove(existingIndex);
        }
    }
    for(const STerrainTileCommand::SPendingVirtualVertexDelta& delta
        : stamp.pendingVirtualVertices)
    {
        int existingIndex = -1;
        for(int index = 0; index < stroke->pendingVirtualVertices.size(); ++index)
        {
            const auto& existing = stroke->pendingVirtualVertices.at(index);
            if(existing.globalX == delta.globalX
               && existing.globalY == delta.globalY
               && existing.layer == delta.layer)
            {
                existingIndex = index;
                break;
            }
        }
        if(existingIndex < 0)
            stroke->pendingVirtualVertices.append(delta);
        else
        {
            auto& existing = stroke->pendingVirtualVertices[existingIndex];
            existing.after = delta.after;
            if(existing.before == existing.after)
                stroke->pendingVirtualVertices.remove(existingIndex);
        }
    }
    for(const STerrainTileCommand::SVirtualFieldDelta& delta : stamp.virtualFields)
    {
        int existingIndex = -1;
        for(int index = 0; index < stroke->virtualFields.size(); ++index)
            if(stroke->virtualFields.at(index).layer == delta.layer)
            {
                existingIndex = index;
                break;
            }
        if(existingIndex < 0)
        {
            STerrainTileCommand::SVirtualFieldDelta composed = delta;
            // A mode switch normally closes the live stroke.  Still make
            // composition deterministic if a full-field command follows
            // per-vertex edits on the same layer: reconstruct the original
            // field and let the complete snapshot own the transaction.
            for(int index = stroke->virtualVertices.size() - 1; index >= 0; --index)
            {
                const auto& vertex = stroke->virtualVertices.at(index);
                if(vertex.layer != delta.layer)
                    continue;
                const int key = vertex.globalY * delta.vertexWidth
                    + vertex.globalX;
                if(key >= 0 && key < composed.beforeCurrent.size())
                    composed.beforeCurrent[key] = vertex.before;
                if(vertex.updatesBaseline && key >= 0
                   && key < composed.beforeBaseline.size())
                    composed.beforeBaseline[key] = vertex.beforeBaseline;
                stroke->virtualVertices.remove(index);
            }
            stroke->virtualFields.append(composed);
        }
        else
        {
            auto& existing = stroke->virtualFields[existingIndex];
            existing.afterCurrent = delta.afterCurrent;
            existing.afterBaseline = delta.afterBaseline;
            if(existing.beforeCurrent == existing.afterCurrent
               && existing.beforeBaseline == existing.afterBaseline)
                stroke->virtualFields.remove(existingIndex);
        }
    }
}

void appendOverlaySurfaceCommand(SOverlaySurfaceCommand* stroke,
                                 const SOverlaySurfaceCommand& stamp)
{
    if(!stroke)
        return;
    if(stroke->description.isEmpty())
        stroke->description = stamp.description;
    stroke->heightClampedVertexCount += stamp.heightClampedVertexCount;
    stroke->affectsTerrainShadowField = stroke->affectsTerrainShadowField
        || stamp.affectsTerrainShadowField;
    for(const SOverlaySectorDelta& delta : stamp.sectors)
    {
        bool merged = false;
        for(SOverlaySectorDelta& existing : stroke->sectors)
        {
            if(existing.sectorX != delta.sectorX || existing.sectorY != delta.sectorY)
                continue;
            existing.afterPayload = delta.afterPayload;
            merged = true;
            break;
        }
        if(!merged)
            stroke->sectors.append(delta);
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

STerrainHeightInfluenceBounds terrainHeightInfluenceBounds(
    const STerrainHeightCommand& command, int vertexCountX, int vertexCountY)
{
    STerrainHeightInfluenceBounds bounds;
    if(command.deltas.isEmpty() || vertexCountX < 2 || vertexCountY < 2)
        return bounds;
    int minimumX = command.deltas.front().globalX;
    int minimumY = command.deltas.front().globalY;
    int maximumX = minimumX;
    int maximumY = minimumY;
    for(const STerrainHeightDelta& delta : command.deltas)
    {
        minimumX = qMin(minimumX, delta.globalX);
        minimumY = qMin(minimumY, delta.globalY);
        maximumX = qMax(maximumX, delta.globalX);
        maximumY = qMax(maximumY, delta.globalY);
    }
    // Surface sampling may walk neighbouring displaced cells before finding
    // the owning triangle. This conservative halo keeps that search correct
    // without reprojecting every object in every loaded MOB.
    constexpr float projectionSearchHalo = 12.0f;
    bounds.valid = true;
    bounds.minimumX = float(minimumX) - projectionSearchHalo;
    bounds.minimumY = float(minimumY) - projectionSearchHalo;
    bounds.maximumX = float(maximumX) + projectionSearchHalo;
    bounds.maximumY = float(maximumY) + projectionSearchHalo;
    bounds.terrainMaximumX = float(vertexCountX - 2);
    bounds.terrainMaximumY = float(vertexCountY - 2);
    return bounds;
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
    QSet<qint32> routeMaterials;
    for(qint32 material : {variant.signature.northWest,
                           variant.signature.northEast,
                           variant.signature.southEast,
                           variant.signature.southWest})
        routeMaterials.insert(material);
    if(variant.routeOwnerMaterial < 0
       || (variant.routeOwnerMaterial > 0
           && (routeMaterials.size() != 2
               || !routeMaterials.contains(variant.routeOwnerMaterial))))
    {
        if(report)
            report->add("terrain.transition_route_owner",
                EValidationSeverity::Error,
                "A generated transition route owner must be one of exactly two semantic corner materials");
        return false;
    }
    if(m_packedLookup.contains(variant.packedTile))
    {
        if(report)
            report->add("terrain.transition_duplicate", EValidationSeverity::Error,
                        "Transition catalog assigns the same packed tile more than once");
        return false;
    }
    const int physicalSlot = SMprPackedTile::decode(
        variant.packedTile).atlasSlot();
    SMprPackedTile physical = SMprPackedTile::decode(variant.packedTile);
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        physical.rotation = quint8(rotation);
        const auto existingPacked = m_packedLookup.constFind(physical.encode());
        if(existingPacked != m_packedLookup.constEnd()
           && m_variants.at(existingPacked.value()).routeOwnerMaterial
                != variant.routeOwnerMaterial)
        {
            if(report)
                report->add("terrain.transition_route_owner",
                    EValidationSeverity::Error,
                    "Every rotation of one physical terrain tile must carry the same route owner provenance");
            return false;
        }
        if(existingPacked != m_packedLookup.constEnd()
           && m_variants.at(existingPacked.value()).canonicalGeneratedRoute
                != variant.canonicalGeneratedRoute)
        {
            if(report)
                report->add("terrain.transition_route_owner",
                    EValidationSeverity::Error,
                    "Every rotation of one physical terrain tile must carry the same generated-route provenance class");
            return false;
        }
    }
    const QVector<int> sameSignature = m_signatureLookup.value(
        signatureKey(variant.signature));
    for(int existingIndex : sameSignature)
    {
        const STerrainTransitionVariant& existing = m_variants.at(existingIndex);
        if(SMprPackedTile::decode(existing.packedTile).atlasSlot() == physicalSlot
           && existing.distributionWeight != variant.distributionWeight)
        {
            if(report)
                report->add("terrain.transition_physical_weight",
                    EValidationSeverity::Error,
                    "Every valid rotation of one physical terrain tile must use the same distribution weight");
            return false;
        }
        if(SMprPackedTile::decode(existing.packedTile).atlasSlot() == physicalSlot
           && existing.routeOwnerMaterial != variant.routeOwnerMaterial)
        {
            if(report)
                report->add("terrain.transition_route_owner",
                    EValidationSeverity::Error,
                    "Every rotation of one physical terrain tile must carry the same route owner provenance");
            return false;
        }
        if(SMprPackedTile::decode(existing.packedTile).atlasSlot() == physicalSlot
           && existing.canonicalGeneratedRoute
                != variant.canonicalGeneratedRoute)
        {
            if(report)
                report->add("terrain.transition_route_owner",
                    EValidationSeverity::Error,
                    "Every rotation of one physical terrain tile must carry the same generated-route provenance class");
            return false;
        }
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
    return selectCandidates(signature, globalX, globalY, distributionSeed,
                            false, nullptr, 0, found);
}

quint16 CTerrainTransitionCatalog::selectCandidates(
    const STerrainTransitionSignature& signature, int globalX, int globalY,
    quint32 distributionSeed, bool paintOnly,
    const QSet<int>* allowedPhysicalSlots,
    qint32 preferredRouteOwnerMaterial, bool* found) const
{
    const QVector<int> indexed = m_signatureLookup.value(signatureKey(signature));
    QSet<qint32> requestedMaterials;
    for(qint32 material : {signature.northWest, signature.northEast,
                           signature.southEast, signature.southWest})
        requestedMaterials.insert(material);
    const bool applyRouteOwner = preferredRouteOwnerMaterial > 0
        && requestedMaterials.size() == 2
        && requestedMaterials.contains(preferredRouteOwnerMaterial);
    QMap<int, QVector<int>> byPhysicalSlot;
    for(int index : indexed)
    {
        const STerrainTransitionVariant& variant = m_variants.at(index);
        if(paintOnly && !variant.paintSelectable)
            continue;
        const int physicalSlot = SMprPackedTile::decode(
            variant.packedTile).atlasSlot();
        if(allowedPhysicalSlots && !allowedPhysicalSlots->contains(physicalSlot))
            continue;
        // Authored artwork is route-neutral and retains precedence. Explicit
        // generated provenance is eligible only for its own connected-region
        // owner, so an opposite cached route cannot displace the requested
        // one merely because it has the same semantic corners and MP type.
        if(applyRouteOwner
           && ((variant.routeOwnerMaterial > 0
                && variant.routeOwnerMaterial
                    != preferredRouteOwnerMaterial)
               || (variant.canonicalGeneratedRoute
                   && variant.routeOwnerMaterial
                        != preferredRouteOwnerMaterial)))
            continue;
        byPhysicalSlot[physicalSlot].append(index);
    }
    quint64 totalWeight = 0;
    for(auto group = byPhysicalSlot.constBegin();
        group != byPhysicalSlot.constEnd(); ++group)
        if(!group.value().isEmpty())
            totalWeight += quint64(m_variants.at(
                group.value().front()).distributionWeight);
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
    QVector<int> selectedRotations;
    for(auto group = byPhysicalSlot.constBegin();
        group != byPhysicalSlot.constEnd(); ++group)
    {
        const int weight = m_variants.at(
            group.value().front()).distributionWeight;
        if(selection < quint64(weight))
        {
            selectedRotations = group.value();
            break;
        }
        selection -= quint64(weight);
    }
    if(!selectedRotations.isEmpty())
    {
        // Rotation is a separate deterministic choice. A physical slot with
        // fewer explicitly valid rotations must not become less likely.
        quint32 rotationHash = hash ^ 0x9e3779b9u;
        rotationHash ^= rotationHash >> 16;
        rotationHash *= 0x85ebca6bu;
        rotationHash ^= rotationHash >> 13;
        const int index = selectedRotations.at(
            int(rotationHash % quint32(selectedRotations.size())));
        if(found)
            *found = true;
        return m_variants.at(index).packedTile;
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
    return selectCandidates(lookup, globalX, globalY, distributionSeed,
                             true, nullptr, 0, found);
}

quint16 CTerrainTransitionCatalog::selectForPaint(
    const STerrainTransitionSignature& signature, int globalX, int globalY,
    quint32 distributionSeed, qint32 preferredRouteOwnerMaterial,
    bool* found) const
{
    const STerrainTransitionSignature lookup = m_mirrorPaintGridY
        ? mirroredGridY(signature) : signature;
    return selectCandidates(lookup, globalX, globalY, distributionSeed,
                             true, nullptr,
                             preferredRouteOwnerMaterial, found);
}

quint16 CTerrainTransitionCatalog::selectForPaint(
    const STerrainTransitionSignature& signature, int globalX, int globalY,
    quint32 distributionSeed, const QSet<int>& allowedPhysicalSlots,
    bool* found) const
{
    const STerrainTransitionSignature lookup = m_mirrorPaintGridY
        ? mirroredGridY(signature) : signature;
    return selectCandidates(lookup, globalX, globalY, distributionSeed,
                             true, &allowedPhysicalSlots, 0, found);
}

quint16 CTerrainTransitionCatalog::selectForPaint(
    const STerrainTransitionSignature& signature, int globalX, int globalY,
    quint32 distributionSeed, const QSet<int>& allowedPhysicalSlots,
    qint32 preferredRouteOwnerMaterial, bool* found) const
{
    const STerrainTransitionSignature lookup = m_mirrorPaintGridY
        ? mirroredGridY(signature) : signature;
    return selectCandidates(lookup, globalX, globalY, distributionSeed,
                             true, &allowedPhysicalSlots,
                             preferredRouteOwnerMaterial, found);
}

quint16 CTerrainTransitionCatalog::lowestPaintRotationForSlot(
    const STerrainTransitionSignature& signature, quint16 selectedPacked,
    bool* found) const
{
    const STerrainTransitionSignature lookup = m_mirrorPaintGridY
        ? mirroredGridY(signature) : signature;
    const int physicalSlot = SMprPackedTile::decode(
        selectedPacked).atlasSlot();
    int bestRotation = 4;
    quint16 best = 0;
    for(int index : m_signatureLookup.value(signatureKey(lookup)))
    {
        const STerrainTransitionVariant& variant = m_variants.at(index);
        const SMprPackedTile packed = SMprPackedTile::decode(variant.packedTile);
        if(variant.paintSelectable && packed.atlasSlot() == physicalSlot
           && packed.rotation < bestRotation)
        {
            bestRotation = packed.rotation;
            best = variant.packedTile;
        }
    }
    if(found)
        *found = bestRotation < 4;
    return best;
}

STerrainTransitionVariant CTerrainTransitionCatalog::variantForPacked(
    quint16 packedTile, bool* found) const
{
    const auto existing = m_packedLookup.constFind(packedTile);
    if(existing != m_packedLookup.constEnd())
    {
        if(found)
            *found = true;
        return m_variants.at(existing.value());
    }
    if(found)
        *found = false;
    return STerrainTransitionVariant();
}

QVector<STerrainTransitionVariant> CTerrainTransitionCatalog::paintVariants(
    const STerrainTransitionSignature& signature) const
{
    const STerrainTransitionSignature lookup = m_mirrorPaintGridY
        ? mirroredGridY(signature) : signature;
    QVector<STerrainTransitionVariant> result;
    for(int index : m_signatureLookup.value(signatureKey(lookup)))
        if(m_variants.at(index).paintSelectable)
            result.append(m_variants.at(index));
    std::sort(result.begin(), result.end(), [](
        const STerrainTransitionVariant& left,
        const STerrainTransitionVariant& right) {
        return left.packedTile < right.packedTile;
    });
    return result;
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

void CTerrainTransitionCatalog::removeAtlasSlot(int atlasSlot)
{
    QVector<STerrainTransitionVariant> retained;
    retained.reserve(m_variants.size());
    for(const STerrainTransitionVariant& variant : m_variants)
        if(SMprPackedTile::decode(variant.packedTile).atlasSlot() != atlasSlot)
            retained.append(variant);
    m_variants.clear();
    m_packedLookup.clear();
    m_signatureLookup.clear();
    // Material identities belong to the authoring catalog, not to whichever
    // physical cache slots happen to be resident. Keep m_materialIds intact.
    for(const STerrainTransitionVariant& variant : retained)
    {
        const int index = m_variants.size();
        m_variants.append(variant);
        m_packedLookup.insert(variant.packedTile, index);
        m_signatureLookup[signatureKey(variant.signature)].append(index);
    }
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
    m_virtualLandMaterials.clear();
    m_virtualOverlayMaterials.clear();
    m_originalVirtualLandMaterials.clear();
    m_originalVirtualOverlayMaterials.clear();
    m_pendingVirtualLandMaterials.clear();
    m_pendingVirtualOverlayMaterials.clear();
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

void CTerrainEditModel::enableWorkingAtlasSlots(int slotCount)
{
    const int bounded = qBound(0, slotCount, 16384);
    if(bounded <= m_tileTypes.size())
        return;
    const int previous = m_tileTypes.size();
    m_tileTypes.resize(bounded);
    for(int slot = previous; slot < bounded; ++slot)
        m_tileTypes[slot] = -1;
    m_textureCount = qMax(m_textureCount, (bounded + 63) / 64);
}

bool CTerrainEditModel::canInstallWorkingAtlasSlotTypes(
    const QVector<qint32>& runtimeTerrainTypes) const
{
    if(runtimeTerrainTypes.isEmpty()
       || runtimeTerrainTypes.size() > m_tileTypes.size())
        return false;
    for(qint32 runtimeTerrainType : runtimeTerrainTypes)
        if(runtimeTerrainType < 0 || runtimeTerrainType >= 16)
            return false;
    return true;
}

bool CTerrainEditModel::installWorkingAtlasSlotTypes(
    const QVector<qint32>& runtimeTerrainTypes)
{
    // Validate the complete table before changing any entry. A generated
    // family can realize several physical variants at once; publishing only
    // the selected slot leaves its siblings visible in Manual mode but
    // unpaintable until a save/reopen rebuilds MP metadata.
    if(!canInstallWorkingAtlasSlotTypes(runtimeTerrainTypes))
        return false;
    for(int slot = 0; slot < runtimeTerrainTypes.size(); ++slot)
        m_tileTypes[slot] = runtimeTerrainTypes.at(slot);
    return true;
}

bool CTerrainEditModel::setWorkingAtlasSlotType(
    int slot, qint32 runtimeTerrainType)
{
    if(slot < 0 || slot >= m_tileTypes.size()
       || runtimeTerrainType < 0 || runtimeTerrainType >= 16)
        return false;
    m_tileTypes[slot] = runtimeTerrainType;
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
    if(m_maxZ != m_originalMaxZ || m_virtualLandMaterials!=m_originalVirtualLandMaterials
       || m_virtualOverlayMaterials!=m_originalVirtualOverlayMaterials)
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
    const bool present = slot >= 0 && slot < m_tileTypes.size()
        && m_tileTypes.at(slot) >= 0 && m_tileTypes.at(slot) < 16;
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
    const STerrainBrush& brush, float rawHeightDelta,
    bool allowSaturation) const
{
    STerrainHeightCommand command;
    command.description = rawHeightDelta >= 0.0f ? "Raise terrain" : "Lower terrain";
    if(brush.radius < 0.0f || m_vertexCountX <= 0 || m_vertexCountY <= 0)
        return command;
    const int minX = qMax(0, int(std::floor(brush.centerX - brush.radius)));
    const int maxX = qMin(m_vertexCountX - 1,
        int(std::ceil(brush.centerX + brush.radius)));
    const int minY = qMax(0, int(std::floor(brush.centerY - brush.radius)));
    const int maxY = qMin(m_vertexCountY - 1,
        int(std::ceil(brush.centerY + brush.radius)));
    for(int y = minY; y <= maxY; ++y)
    {
        for(int x = minX; x <= maxX; ++x)
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
                                        allowSaturation ? nullptr : &delta.clamped);
            if(delta.before != delta.after)
                command.deltas.append(delta);
        }
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeNoise(
    const STerrainBrush& brush, float rawHeightRange,
    quint32 noiseSeed, bool allowSaturation) const
{
    STerrainHeightCommand command;
    command.description = QStringLiteral("Noise terrain");
    if(brush.radius < 0.0f || !std::isfinite(rawHeightRange)
       || rawHeightRange < 0.0f || m_vertexCountX <= 0 || m_vertexCountY <= 0)
        return command;
    const int minX = qMax(0, int(std::floor(brush.centerX - brush.radius)));
    const int maxX = qMin(m_vertexCountX - 1,
        int(std::ceil(brush.centerX + brush.radius)));
    const int minY = qMax(0, int(std::floor(brush.centerY - brush.radius)));
    const int maxY = qMin(m_vertexCountY - 1,
        int(std::ceil(brush.centerY + brush.radius)));
    for(int y = minY; y <= maxY; ++y)
        for(int x = minX; x <= maxX; ++x)
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
            delta.after = clampedHeight(float(delta.before)
                + rawHeightRange * weight * terrainNoiseSample(noiseSeed, x, y),
                allowSaturation ? nullptr : &delta.clamped);
            if(delta.before != delta.after)
                command.deltas.append(delta);
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
    if(brush.radius < 0.0f || m_vertexCountX <= 0 || m_vertexCountY <= 0)
        return command;
    const int minX = qMax(0, int(std::floor(brush.centerX - brush.radius)));
    const int maxX = qMin(m_vertexCountX - 1,
        int(std::ceil(brush.centerX + brush.radius)));
    const int minY = qMax(0, int(std::floor(brush.centerY - brush.radius)));
    const int maxY = qMin(m_vertexCountY - 1,
        int(std::ceil(brush.centerY + brush.radius)));
    for(int y = minY; y <= maxY; ++y)
    {
        for(int x = minX; x <= maxX; ++x)
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

STerrainHeightCommand CTerrainEditModel::makeBoundaryFill(
    const STerrainBrush& brush, float strength) const
{
    STerrainHeightCommand command;
    command.description = "Fill terrain from boundary";
    if(!std::isfinite(strength) || strength<=0) return command;
    const auto targets=TerrainBoundaryFill::grid(brush.centerX,brush.centerY,brush.radius,
        m_vertexCountX,m_vertexCountY,[this](int x,int y,double* value) {
            *value=height(x,y);return true;
        });
    for(auto it=targets.cbegin();it!=targets.cend();++it) {
        const int x=it.key().first,y=it.key().second;
        const double weight=brushWeight(std::hypot(x-brush.centerX,y-brush.centerY),
            brush.radius,brush.falloff)*qBound(0.0f,strength,1.0f);
        STerrainHeightDelta delta;delta.globalX=x;delta.globalY=y;delta.before=height(x,y);
        delta.after=clampedHeight(delta.before+(it.value()-delta.before)*weight);
        if(delta.before!=delta.after) command.deltas.append(delta);
    }
    return command;
}

STerrainHeightCommand CTerrainEditModel::makeSmooth(
    const STerrainBrush& brush, float strength) const
{
    QVector<float> targets(m_heights.size(), 0.0f);
    if(brush.radius < 0.0f || m_vertexCountX <= 0 || m_vertexCountY <= 0)
        return STerrainHeightCommand();
    const int minX = qMax(0, int(std::floor(brush.centerX - brush.radius)));
    const int maxX = qMin(m_vertexCountX - 1,
        int(std::ceil(brush.centerX + brush.radius)));
    const int minY = qMax(0, int(std::floor(brush.centerY - brush.radius)));
    const int maxY = qMin(m_vertexCountY - 1,
        int(std::ceil(brush.centerY + brush.radius)));
    for(int y = minY; y <= maxY; ++y)
    {
        for(int x = minX; x <= maxX; ++x)
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
    CValidationReport* report,
    const CTerrainTransitionCatalog* uniformCatalog,
    const STerrainTransitionSignature& uniformSignature,
    quint32 distributionSeed, bool randomizeUniformRotation,
    qint32 requiredRuntimeType) const
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
    QSet<int> allowedUniformSlots;
    if(uniformCatalog && requiredRuntimeType >= 0)
        for(const STerrainTransitionVariant& variant :
                uniformCatalog->paintVariants(uniformSignature))
        {
            bool typeFound = false;
            const qint32 type = tileType(variant.packedTile, &typeFound);
            if(typeFound && type == requiredRuntimeType)
                allowedUniformSlots.insert(SMprPackedTile::decode(
                    variant.packedTile).atlasSlot());
        }
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
            QByteArray& payload = replacementFor(sectorIndex);
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
            }
            // Earlier cells in this command may already have upgraded this
            // detached payload. sector.source is reparsed only by apply(), so
            // derive offsets from the replacement layout, not stale type-0
            // metadata.
            const quint64 overlayTileOffset = kLandVertexOffset
                + 2 * kVertexBlockSize + kTileBlockSize;
            const quint64 overlayMaterialOffset = overlayTileOffset
                + kTileBlockSize;
            const bool wasActive = readOverlayMaterial(
                payload, overlayMaterialOffset, localTile) >= 0;
            quint16 selectedForCell = selected.encode();
            if(uniformCatalog)
            {
                bool variantFound = false;
                selectedForCell = selectUniformInteriorTile(
                    *uniformCatalog, uniformSignature, x, y,
                    terrainPaintLayerSeed(distributionSeed,
                                          ETerrainTileLayer::Overlay),
                    randomizeUniformRotation, &variantFound,
                    requiredRuntimeType >= 0 ? &allowedUniformSlots : nullptr);
                const int selectedSlot = SMprPackedTile::decode(
                    selectedForCell).atlasSlot();
                if(!variantFound || selectedSlot < 0
                   || selectedSlot >= m_tileTypes.size())
                {
                    if(report)
                        report->add("terrain.overlay_activation_variant",
                            EValidationSeverity::Error,
                            "Add Overlay Surface could not select an exact typed uniform brush variant");
                    return SOverlaySurfaceCommand();
                }
            }
            if(readPackedTile(payload, overlayTileOffset, localTile) == selectedForCell
               && readOverlayMaterial(payload, overlayMaterialOffset, localTile)
                    == materialIndex)
                continue;
            // Add Surface is the atomic selected-brush authoring operation:
            // it writes both the packed atlas tile and MPR surface material.
            // Existing geometry/heights stay intact; only genuinely inactive
            // cells need their overlay vertices initialized below.
            writePackedTile(payload, overlayTileOffset, localTile,
                            selectedForCell);
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
    command.affectsTerrainShadowField = !activatedCells.isEmpty();
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
    command.affectsTerrainShadowField = !command.sectors.isEmpty();
    return command;
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayRaiseLower(
    const STerrainBrush& brush, float rawHeightDelta,
    CValidationReport* report, bool allowSaturation) const
{
    return makeOverlayHeightEdit(QStringLiteral("Sculpt overlay height"), brush,
                                 0, rawHeightDelta, 1.0f, report,
                                 allowSaturation);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayFlatten(
    const STerrainBrush& brush, quint16 targetHeight, float strength,
    CValidationReport* report) const
{
    return makeOverlayHeightEdit(QStringLiteral("Flatten overlay height"), brush,
                                 1, float(targetHeight), strength, report);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayBoundaryFill(
    const STerrainBrush& brush, float strength, CValidationReport* report) const
{
    return makeOverlayHeightEdit(QStringLiteral("Fill overlay from boundary"), brush,
                                 4, 0.0f, strength, report);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlaySmooth(
    const STerrainBrush& brush, float strength,
    CValidationReport* report) const
{
    return makeOverlayHeightEdit(QStringLiteral("Smooth overlay height"), brush,
                                 2, 0.0f, strength, report);
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayNoise(
    const STerrainBrush& brush, float rawHeightRange, quint32 noiseSeed,
    CValidationReport* report, bool allowSaturation) const
{
    return makeOverlayHeightEdit(QStringLiteral("Noise overlay height"), brush,
                                 3, rawHeightRange, 1.0f, report,
                                 allowSaturation, noiseSeed);
}

SOverlaySurfaceSnapshot CTerrainEditModel::overlaySurfaceSnapshot() const
{
    SOverlaySurfaceSnapshot snapshot;
    snapshot.sectors.reserve(m_sectors.size());
    for(const SSector& sector : m_sectors)
    {
        SOverlaySectorSnapshot state;
        state.sectorX = sector.source.sectorX;
        state.sectorY = sector.source.sectorY;
        // QByteArray's implicit sharing keeps a complete stroke baseline
        // cheap; only edited sector payloads detach from these source bytes.
        state.payload = sector.currentPayload;
        snapshot.sectors.append(state);
    }
    return snapshot;
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlaySurfaceCommand(
    const SOverlaySurfaceSnapshot& before, const QString& description,
    CValidationReport* report, bool affectsTerrainShadowField) const
{
    SOverlaySurfaceCommand command;
    command.description = description;
    command.affectsTerrainShadowField = affectsTerrainShadowField;
    QSet<QPair<int, int>> seen;
    for(const SOverlaySectorSnapshot& state : before.sectors)
    {
        const QPair<int, int> key(state.sectorX, state.sectorY);
        const int index = m_sectorLookup.value(key, -1);
        if(index < 0 || seen.contains(key))
        {
            if(report)
                report->add("terrain.overlay_snapshot", EValidationSeverity::Error,
                            index < 0
                                ? "Overlay stroke snapshot references a missing sector"
                                : "Overlay stroke snapshot contains a sector more than once",
                            QString("%1,%2").arg(state.sectorX).arg(state.sectorY));
            return SOverlaySurfaceCommand();
        }
        seen.insert(key);
        const QByteArray& current = m_sectors.at(index).currentPayload;
        if(current == state.payload)
            continue;
        SOverlaySectorDelta delta;
        delta.sectorX = state.sectorX;
        delta.sectorY = state.sectorY;
        delta.beforePayload = state.payload;
        delta.afterPayload = current;
        command.sectors.append(delta);
    }
    if(seen.size() != m_sectors.size())
    {
        if(report)
            report->add("terrain.overlay_snapshot", EValidationSeverity::Error,
                        "Overlay stroke snapshot does not cover the current terrain sector set");
        return SOverlaySurfaceCommand();
    }
    return command;
}

SOverlaySurfaceCommand CTerrainEditModel::makeOverlayHeightEdit(
    const QString& description, const STerrainBrush& brush,
    int operation, float value, float strength,
    CValidationReport* report, bool allowSaturation,
    quint32 noiseSeed) const
{
    SOverlaySurfaceCommand command;
    command.description = description;
    if(brush.radius <= 0.0f || !std::isfinite(value) || !std::isfinite(strength)
       || strength < 0.0f || strength > 1.0f || operation < 0 || operation > 4)
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
    QMap<int, QVector<SOverlayRef>> selectedRefs;
    const int preferredSectorIndex = m_sectorLookup.value(
        QPair<int, int>(qBound(0, int(std::floor(brush.centerX / kSectorStride)),
                               m_sectorCountX - 1),
                        qBound(0, int(std::floor(brush.centerY / kSectorStride)),
                               m_sectorCountY - 1)), -1);
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
        // Only overlay cells whose 3x3 vertex footprint intersects the brush
        // neighborhood can own a selected vertex. The previous implementation
        // rescanned every one of the sector's 256 cells on every sculpt stamp,
        // even for a radius-1 brush.
        const int localMinimumTileX = qMax(0,
            int(std::ceil((localMinimumX - 2) / 2.0)));
        const int localMaximumTileX = qMin(kSectorTiles - 1,
            int(std::floor(localMaximumX / 2.0)));
        const int localMinimumTileY = qMax(0,
            int(std::ceil((localMinimumY - 2) / 2.0)));
        const int localMaximumTileY = qMin(kSectorTiles - 1,
            int(std::floor(localMaximumY / 2.0)));
        for(int tileY = localMinimumTileY; tileY <= localMaximumTileY; ++tileY)
        {
            for(int tileX = localMinimumTileX; tileX <= localMaximumTileX; ++tileX)
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
            if(!selected || ref.sectorIndex == preferredSectorIndex)
                selected = &ref;
            if(ref.sectorIndex == preferredSectorIndex)
                break;
        }
        if(!selected)
            continue;
        QVector<SOverlayRef> owners;
        for(const SOverlayRef& ref : vertexRefs)
            if(activeOwners.contains(ref.sectorIndex)
               && ref.height == selected->height
               && ref.xOffset == selected->xOffset
               && ref.yOffset == selected->yOffset)
                owners.append(ref);
        if(owners.isEmpty())
            continue;
        selectedRefs.insert(key, owners);
        heights.insert(key, selected->height);
        xOffsets.insert(key, selected->xOffset);
        yOffsets.insert(key, selected->yOffset);
    }

    QMap<QPair<int,int>,double> boundaryTargets;
    if(operation==4)
        boundaryTargets=TerrainBoundaryFill::grid(brush.centerX,brush.centerY,brush.radius,
            m_vertexCountX,m_vertexCountY,[&](int x,int y,double* value) {
                const auto it=heights.constFind(globalIndex(x,y));
                if(it==heights.cend()) return false;
                *value=it.value();return true;
            });
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
        else if(operation == 2)
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
        else if(operation==4)
        {
            const auto it=boundaryTargets.constFind({x,y});
            if(it==boundaryTargets.cend()) continue;
            target=before+(it.value()-before)*strength*weight;
        }
        else
            target = before + value * weight * terrainNoiseSample(noiseSeed, x, y);
        bool clamped = false;
        const quint16 after = clampedHeight(target,
            allowSaturation ? nullptr : &clamped);
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
    const auto replacementFor = [this, &replacements](int sectorIndex)
        -> QByteArray& {
        auto it = replacements.find(sectorIndex);
        if(it == replacements.end())
            it = replacements.insert(sectorIndex,
                                     m_sectors.at(sectorIndex).currentPayload);
        return it.value();
    };
    for(int key : changedVertices)
        for(const SOverlayRef& ref : selectedRefs.value(key))
        {
            QByteArray& payload = replacementFor(ref.sectorIndex);
            writeVertexHeight(payload,
                m_sectors.at(ref.sectorIndex).source.overlayVertexOffset,
                ref.localX, ref.localY, afterHeights.value(key));
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
                if(activeVertices.contains(key) && selectedRefs.contains(key))
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
        for(const SOverlayRef& ref : selectedRefs.value(key))
        {
            QByteArray& payload = replacementFor(ref.sectorIndex);
            writeVertexNormal(payload,
                m_sectors.at(ref.sectorIndex).source.overlayVertexOffset,
                ref.localX, ref.localY, packed);
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
                                                bool* available,
                                                bool requireActiveOverlay) const
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
        if(sector.source.overlayVertexOffset == 0
           || (requireActiveOverlay && !overlayTileIsActive(sector, localTile)))
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
    const float z00 = float(h00) * m_maxZ / 65534.0f;
    const float z10 = float(h10) * m_maxZ / 65534.0f;
    const float z01 = float(h01) * m_maxZ / 65534.0f;
    const float z11 = float(h11) * m_maxZ / 65534.0f;
    float world = 0.0f;
    if(!right && !bottom)
        world = z01 / 4.0f + z10 / 4.0f + z00 / 2.0f;
    else if(right && !bottom)
        world = z00 / 4.0f + z11 / 4.0f + z10 / 2.0f;
    else if(!right && bottom)
        world = z00 / 4.0f + z11 / 4.0f + z01 / 2.0f;
    else
        world = z01 / 4.0f + z10 / 4.0f + z11 / 2.0f;
    if(available)
        *available = true;
    return world;
}

float CTerrainEditModel::terrainSurfaceHeight(float worldX, float worldY,
                                              bool* available) const
{
    if(available)
        *available = false;
    if(m_vertexCountX < 2 || m_vertexCountY < 2 || m_heights.isEmpty()
       || !std::isfinite(worldX) || !std::isfinite(worldY))
        return 0.0f;

    const float x = qBound(0.0f, worldX, float(m_vertexCountX - 2));
    const float y = qBound(0.0f, worldY, float(m_vertexCountY - 2));
    int cellX = int(std::nearbyint(double(x)));
    int cellY = int(std::nearbyint(double(y)));

    struct SurfaceVertex
    {
        double x{0.0};
        double y{0.0};
        double z{0.0};
    };
    const auto vertex = [this](int vx, int vy, SurfaceVertex* out) {
        if(!out || vx < 0 || vy < 0 || vx >= m_vertexCountX || vy >= m_vertexCountY)
            return false;
        const int index = globalIndex(vx, vy);
        out->x = double(vx) + double(m_xOffsets.at(index)) / 252.0;
        out->y = double(vy) + double(m_yOffsets.at(index)) / 252.0;
        out->z = double(m_heights.at(index)) * double(m_maxZ) / 65534.0;
        return true;
    };
    const auto orientation = [](const SurfaceVertex& start,
                                const SurfaceVertex& end,
                                double pointX, double pointY) {
        return (end.x - start.x) * (pointY - start.y)
            - (end.y - start.y) * (pointX - start.x);
    };
    const auto planeHeight = [](double sampleX, double sampleY,
                                const SurfaceVertex& first,
                                const SurfaceVertex& second,
                                const SurfaceVertex& third) {
        const double e1 = second.x - first.x;
        const double e2 = second.y - first.y;
        const double e4 = third.x - first.x;
        const double e5 = third.y - first.y;
        const double c = e1 * e5 - e2 * e4;
        if(std::fabs(c) < 0.01)
            return (first.z + second.z + third.z) / 3.0;
        const double e3 = second.z - first.z;
        const double e6 = third.z - first.z;
        const double a = e2 * e6 - e3 * e5;
        const double b = e3 * e4 - e1 * e6;
        const double d = -(a * first.x + b * first.y + c * first.z);
        return -(a * sampleX + b * sampleY + d) / c;
    };

    for(int attempt = 0; attempt < 10; ++attempt)
    {
        SurfaceVertex upperLeft;
        SurfaceVertex upperRight;
        SurfaceVertex lowerLeft;
        SurfaceVertex lowerRight;
        if(!vertex(cellX, cellY, &upperLeft)
           || !vertex(cellX + 1, cellY + 1, &lowerRight))
            return 0.0f;
        const bool hasUpperRight = vertex(cellX + 1, cellY, &upperRight);
        const bool hasLowerLeft = vertex(cellX, cellY + 1, &lowerLeft);
        if(hasUpperRight && !hasLowerLeft)
        {
            if(available)
                *available = true;
            return float(planeHeight(x, y, upperLeft, upperRight, lowerRight));
        }
        if(!hasUpperRight && hasLowerLeft)
        {
            if(available)
                *available = true;
            return float(planeHeight(x, y, upperLeft, lowerRight, lowerLeft));
        }
        if(!hasUpperRight || !hasLowerLeft)
            return 0.0f;

        if(orientation(upperRight, lowerRight, upperLeft.x, upperLeft.y) <= 0.0
           || orientation(lowerLeft, upperLeft, lowerRight.x, lowerRight.y) <= 0.0)
        {
            if(available)
                *available = true;
            return float((lowerLeft.z + lowerRight.z) * 0.5);
        }

        const double diagonalSide = (double(x) - upperLeft.x)
            * (lowerRight.y - upperLeft.y)
            - (double(y) - upperLeft.y) * (lowerRight.x - upperLeft.x);
        if(diagonalSide > 0.0)
        {
            if(attempt < 9)
            {
                if(orientation(upperRight, upperLeft, x, y) > 0.0)
                {
                    --cellY;
                    continue;
                }
                if(orientation(upperRight, lowerRight, x, y) < 0.0)
                {
                    ++cellX;
                    continue;
                }
            }
            if(available)
                *available = true;
            return float(planeHeight(x, y, upperLeft, upperRight, lowerRight));
        }

        if(attempt < 9)
        {
            if(orientation(lowerLeft, upperLeft, x, y) < 0.0)
            {
                --cellX;
                continue;
            }
            if(orientation(lowerLeft, lowerRight, x, y) > 0.0)
            {
                ++cellY;
                continue;
            }
        }
        if(available)
            *available = true;
        return float(planeHeight(x, y, upperLeft, lowerRight, lowerLeft));
    }
    return 0.0f;
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
    return diagnoseState(command, objects, policy, report, false);
}

STerrainEditDiagnostics CTerrainEditModel::diagnoseApplied(
    const STerrainHeightCommand& command,
    const QVector<STerrainObjectHeightProbe>& objects,
    const STerrainDiagnosticPolicy& policy,
    CValidationReport* report) const
{
    return diagnoseState(command, objects, policy, report, true);
}

STerrainEditDiagnostics CTerrainEditModel::diagnoseState(
    const STerrainHeightCommand& command,
    const QVector<STerrainObjectHeightProbe>& objects,
    const STerrainDiagnosticPolicy& policy,
    CValidationReport* report, bool commandAlreadyApplied) const
{
    STerrainEditDiagnostics result;
    if(command.isEmpty())
    {
        result.valid = true;
        return result;
    }

    QMap<int, quint16> beforeHeights;
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
        const quint16 expectedCurrent = commandAlreadyApplied
            ? delta.after : delta.before;
        if(m_conflictedVertices.at(index) || m_heights.at(index) != expectedCurrent)
        {
            if(report)
                report->add("terrain.diagnostic_provenance", EValidationSeverity::Error,
                            "Height diagnostic no longer matches editable canonical terrain",
                            QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return result;
        }
        beforeHeights.insert(index, delta.before);
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

    const auto rawHeight = [this, &beforeHeights, &afterHeights,
                            commandAlreadyApplied](int index, bool after) {
        if(!after && commandAlreadyApplied && beforeHeights.contains(index))
            return beforeHeights.value(index);
        if(after && !commandAlreadyApplied && afterHeights.contains(index))
            return afterHeights.value(index);
        return m_heights.at(index);
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
    QSet<int> affectedSectors;
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
            affectedSectors.insert(ref.sectorIndex);
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
            affectedSectors.insert(ref.sectorIndex);
        }
    }
    for(int sectorIndex : affectedSectors)
    {
        SSector& sector = m_sectors[sectorIndex];
        sector.dirty = sector.currentPayload != sector.source.payload;
    }
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
    if(selected.atlasSlot() >= m_tileTypes.size()
       || m_tileTypes.at(selected.atlasSlot()) < 0
       || m_tileTypes.at(selected.atlasSlot()) >= 16)
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

STerrainTileCommand CTerrainEditModel::makeAtlasSlotRemap(
    const QMap<int, int>& slotRemap) const
{
    STerrainTileCommand command;
    command.description = QStringLiteral(
        "Repack animated terrain frames and preserve terrain references");
    if(slotRemap.isEmpty())
        return command;
    for(ETerrainTileLayer layer : {ETerrainTileLayer::Land,
                                   ETerrainTileLayer::Overlay})
        for(int y = 0; y < tileCountY(); ++y)
            for(int x = 0; x < tileCountX(); ++x)
            {
                bool available = false;
                const quint16 before = packedTile(x, y, layer, &available);
                if(!available)
                    continue;
                SMprPackedTile packed = SMprPackedTile::decode(before);
                const auto remapped = slotRemap.constFind(packed.atlasSlot());
                if(remapped == slotRemap.constEnd()
                   || remapped.value() == packed.atlasSlot()
                   || remapped.value() < 0
                   || remapped.value() >= m_tileTypes.size()
                   || m_tileTypes.at(remapped.value()) < 0
                   || m_tileTypes.at(remapped.value()) >= 16)
                    continue;
                packed.tileIndex = quint8(remapped.value() % 64);
                packed.atlasIndex = quint8(remapped.value() / 64);
                STerrainTileDelta delta;
                delta.globalX = x;
                delta.globalY = y;
                delta.layer = layer;
                delta.before = before;
                delta.after = packed.encode();
                command.deltas.append(delta);
            }
    return command;
}

STerrainTileCommand CTerrainEditModel::makeTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    quint8 atlasIndex, quint8 tileIndex, quint8 rotation,
    const CTerrainTransitionCatalog& catalog, CValidationReport* report)
{
    Q_UNUSED(report);
    STerrainTileCommand command = makeTilePaint(
        brush, layer, atlasIndex, tileIndex, rotation);
    if(brush.radius < 0.0f || tileIndex >= 64 || rotation >= 4
       || atlasIndex >= m_textureCount)
        return command;

    SMprPackedTile selected;
    selected.atlasIndex = atlasIndex;
    selected.tileIndex = tileIndex;
    selected.rotation = rotation;
    if(selected.atlasSlot() >= m_tileTypes.size()
       || m_tileTypes.at(selected.atlasSlot()) < 0
       || m_tileTypes.at(selected.atlasSlot()) >= 16)
        return command;

    const int width = tileCountX();
    const int height = tileCountY();
    const quint16 after = selected.encode();
    QMap<int, quint16> postCommandPacked;
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(width - 1,
                              int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(height - 1,
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
            packedTile(x, y, layer, &available);
            if(available)
                postCommandPacked.insert(y * width + x, after);
        }
    }
    if(postCommandPacked.isEmpty())
        return command;

    command.semanticAudit.valid = true;
    command.semanticAudit.brush = brush;
    command.semanticAudit.layer = layer;
    command.semanticAudit.requestedMaterialId = -1;
    command.semanticAudit.solverMode = QStringLiteral(
        "manual-exact-corners");
    for(auto cell = postCommandPacked.constBegin();
        cell != postCommandPacked.constEnd(); ++cell)
    {
        const QPoint point(cell.key() % width, cell.key() / width);
        command.semanticAudit.paintedCoreCells.append(point);
        command.semanticAudit.affectedCells.append(point);
    }

    const int vertexWidth = width + 1;
    const int expectedFieldSize = vertexWidth * (height + 1);
    QVector<qint32> beforeCurrent;
    QVector<qint32> beforeBaseline;
    copyVirtualMaterialFieldState(layer, &beforeCurrent, &beforeBaseline);

    bool selectedDecoded = false;
    const STerrainTransitionSignature signature =
        catalog.gridSignatureForPacked(after, &selectedDecoded);
    const qint32 unset = std::numeric_limits<qint32>::min();
    QMap<int, qint32> finalValues;
    for(auto cell = postCommandPacked.constBegin();
        cell != postCommandPacked.constEnd(); ++cell)
    {
        const int x = cell.key() % width;
        const int y = cell.key() / width;
        const int northWest = y * vertexWidth + x;
        const int northEast = northWest + 1;
        const int southWest = (y + 1) * vertexWidth + x;
        const int southEast = southWest + 1;
        finalValues[northWest] = selectedDecoded
            ? signature.northWest : unset;
        finalValues[northEast] = selectedDecoded
            ? signature.northEast : unset;
        finalValues[southEast] = selectedDecoded
            ? signature.southEast : unset;
        finalValues[southWest] = selectedDecoded
            ? signature.southWest : unset;
    }
    if(beforeCurrent.size() == expectedFieldSize
       && beforeBaseline.size() == expectedFieldSize)
    {
        // QMap iteration is cell-key order (y, then x). When several Manual
        // cells touch a vertex, the later explicit cell in that deterministic
        // order wins. Only the final values are stored; no origin or time data
        // is retained.
        for(auto value = finalValues.constBegin();
            value != finalValues.constEnd(); ++value)
        {
            const int key = value.key();
            if(beforeCurrent.at(key) == value.value()
               && beforeBaseline.at(key) == value.value())
                continue;
            STerrainTileCommand::SVirtualVertexDelta delta;
            delta.globalX = key % vertexWidth;
            delta.globalY = key / vertexWidth;
            delta.layer = layer;
            delta.before = beforeCurrent.at(key);
            delta.after = value.value();
            delta.updatesBaseline = true;
            delta.beforeBaseline = beforeBaseline.at(key);
            delta.afterBaseline = value.value();
            command.virtualVertices.append(delta);
        }
    }
    else if(beforeCurrent.isEmpty() && beforeBaseline.isEmpty())
    {
        // Stay proportional to the Manual footprint on a legacy/plain map,
        // but retain the decoded tuple for the first deterministic material
        // recovery. The model still has no complete semantic field here.
        QMap<int, qint32>& pending = layer == ETerrainTileLayer::Land
            ? m_pendingVirtualLandMaterials : m_pendingVirtualOverlayMaterials;
        for(auto value = finalValues.constBegin();
            value != finalValues.constEnd(); ++value)
        {
            const qint32 before = pending.value(value.key(), unset);
            if(before == value.value())
                continue;
            STerrainTileCommand::SPendingVirtualVertexDelta delta;
            delta.globalX = value.key() % vertexWidth;
            delta.globalY = value.key() / vertexWidth;
            delta.layer = layer;
            delta.before = before;
            delta.after = value.value();
            command.pendingVirtualVertices.append(delta);
        }
    }
    // A plain/legacy MPR has no durable semantic field yet. Manual remains a
    // direct physical edit in that state; it must not synchronously solve the
    // complete map for every clicked tile. The first later Modern/Resolve
    // operation initializes once from the post-Manual packed cells.
    return command;
}

STerrainTileCommand CTerrainEditModel::makeBoundedSemanticTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, CValidationReport* report,
    ETerrainSemanticPaintPolicy policy) const
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

    QSet<int> boundary;
    QVector<int> orderedBoundary;
    QSet<int> coreCorners;
    const auto rebuildFootprint = [&]() {
        // The only mutable cells outside the brush are the immediate
        // eight-neighbor ring. Missing transitions never enlarge this
        // footprint.
        boundary.clear();
        coreCorners.clear();
        for(int index : painted)
        {
            const int x = index % width;
            const int y = index / width;
            coreCorners.insert(y * (width + 1) + x);
            coreCorners.insert(y * (width + 1) + x + 1);
            coreCorners.insert((y + 1) * (width + 1) + x + 1);
            coreCorners.insert((y + 1) * (width + 1) + x);
            for(int offsetY = -1; offsetY <= 1; ++offsetY)
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
        orderedPainted = painted.values().toVector();
        std::sort(orderedPainted.begin(), orderedPainted.end());
        orderedBoundary = boundary.values().toVector();
        std::sort(orderedBoundary.begin(), orderedBoundary.end());
    };
    rebuildFootprint();

    QMap<int, STerrainTransitionSignature> existingSignatures;
    QMap<int, quint16> existingTiles;
    QMap<qint32, int> backgroundCounts;
    QSet<int> unknownCore;
    QSet<int> unknownBoundary;
    QString firstUnknownLocation;
    const auto decodeCell = [&](int index, bool core) -> bool {
        const int x = index % width;
        const int y = index / width;
        bool available = false;
        const quint16 packed = packedTile(x, y, layer, &available);
        bool found = false;
        const STerrainTransitionSignature signature = available
            ? catalog.gridSignatureForPacked(packed, &found)
            : STerrainTransitionSignature();
        if(!available)
        {
            if(report)
                report->add("terrain.semantic_catalog", EValidationSeverity::Error,
                            "The semantic paint footprint contains an unavailable tile",
                            QString("%1,%2 packed:0x%3").arg(x).arg(y)
                                .arg(packed, 4, 16, QLatin1Char('0')));
            return false;
        }
        existingTiles.insert(index, packed);
        if(!found)
        {
            // Existing maps can contain hand-authored, legacy, or damaged
            // packed values outside the approved atlas template. A valid
            // brush must still paint over them. Unknown core cells are
            // replaced; unknown ring cells stay frozen so recovery cannot
            // spread beyond the requested footprint.
            STerrainTransitionSignature unknown;
            unknown.northWest = unknown.northEast = -1;
            unknown.southEast = unknown.southWest = -1;
            existingSignatures.insert(index, unknown);
            if(core)
                unknownCore.insert(index);
            else
                unknownBoundary.insert(index);
            if(firstUnknownLocation.isEmpty())
                firstUnknownLocation = QString("%1,%2 packed:0x%3")
                    .arg(x).arg(y).arg(packed, 4, 16, QLatin1Char('0'));
            return true;
        }
        existingSignatures.insert(index, signature);
        return true;
    };
    for(int index : orderedPainted)
        if(!decodeCell(index, true))
            return command;
    for(int index : orderedBoundary)
    {
        if(!decodeCell(index, false))
            return command;
    }

    if(policy == ETerrainSemanticPaintPolicy::ExactExistingOnly)
    {
        QSet<int> unknownCells = unknownCore;
        unknownCells.unite(unknownBoundary);
        const int requestedCoreCount = painted.size();
        for(;;)
        {
            rebuildFootprint();
            QSet<int> blockedCore;
            for(int index : orderedBoundary)
            {
                const int x = index % width;
                const int y = index / width;
                bool supported = !unknownCells.contains(index);
                if(supported)
                {
                    const int cornerKeys[4] = {
                        y * (width + 1) + x,
                        y * (width + 1) + x + 1,
                        (y + 1) * (width + 1) + x + 1,
                        (y + 1) * (width + 1) + x};
                    const STerrainTransitionSignature existing =
                        existingSignatures.value(index);
                    QVector<qint32> desired = {
                        existing.northWest, existing.northEast,
                        existing.southEast, existing.southWest};
                    bool changed = false;
                    for(int corner = 0; corner < 4; ++corner)
                        if(coreCorners.contains(cornerKeys[corner]))
                        {
                            changed |= desired[corner] != materialId;
                            desired[corner] = materialId;
                        }
                    if(changed)
                    {
                        STerrainTransitionSignature requested;
                        requested.northWest = desired.at(0);
                        requested.northEast = desired.at(1);
                        requested.southEast = desired.at(2);
                        requested.southWest = desired.at(3);
                        bool found = false;
                        catalog.selectForPaint(requested, x, y,
                                               distributionSeed, &found);
                        supported = found;
                    }
                }
                if(supported)
                    continue;
                // Every retained core cell adjacent to this unsupported ring
                // cell contributes at least one of its requested corners.
                // Remove those cores and recompute: this preserves independent
                // exact islands instead of committing a uniform hard seam.
                for(int offsetY = -1; offsetY <= 1; ++offsetY)
                    for(int offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        const int coreX = x + offsetX;
                        const int coreY = y + offsetY;
                        const int coreIndex = coreY * width + coreX;
                        if(coreX >= 0 && coreX < width
                           && coreY >= 0 && coreY < height
                           && painted.contains(coreIndex))
                            blockedCore.insert(coreIndex);
                    }
            }
            if(blockedCore.isEmpty())
                break;
            painted.subtract(blockedCore);
            if(painted.isEmpty())
                break;
        }
        rebuildFootprint();
        unknownCore = unknownCells;
        unknownCore.intersect(painted);
        unknownBoundary = unknownCells;
        unknownBoundary.intersect(boundary);
        const int skippedCoreCount = requestedCoreCount - painted.size();
        if(skippedCoreCount > 0 && report)
            report->add("terrain.semantic_tile_exact_only_skip",
                EValidationSeverity::Warning,
                "Classic Auto Safe left tile cells requiring absent authored boundary blends unchanged",
                QString::number(skippedCoreCount));
        if(painted.isEmpty())
        {
            command.semanticAudit.paintedCoreCells.clear();
            command.semanticAudit.affectedCells.clear();
            return command;
        }
    }

    command.semanticAudit.paintedCoreCells.clear();
    command.semanticAudit.affectedCells.clear();
    for(int index : orderedPainted)
    {
        const QPoint point(index % width, index / width);
        command.semanticAudit.paintedCoreCells.append(point);
        command.semanticAudit.affectedCells.append(point);
    }
    for(int index : orderedBoundary)
        command.semanticAudit.affectedCells.append(
            QPoint(index % width, index / width));

    for(int index : orderedBoundary)
    {
        if(unknownBoundary.contains(index))
            continue;
        const STerrainTransitionSignature signature = existingSignatures.value(index);
        const qint32 values[4] = {signature.northWest, signature.northEast,
                                  signature.southEast, signature.southWest};
        for(qint32 value : values)
            if(value != materialId)
                backgroundCounts[value] += 1;
    }
    if((!unknownCore.isEmpty() || !unknownBoundary.isEmpty()) && report)
        report->add("terrain.semantic_unknown_recovery", EValidationSeverity::Warning,
                    QString("Paint recovered %1 unknown core tile(s); %2 unknown boundary tile(s) were preserved")
                        .arg(unknownCore.size()).arg(unknownBoundary.size()),
                    firstUnknownLocation);
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
        if(unknownBoundary.contains(index))
            continue;
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
            if(policy == ETerrainSemanticPaintPolicy::ExactExistingOnly)
            {
                ++hardEdgeCount;
                continue;
            }
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

bool CTerrainEditModel::appendSemanticVirtualVertices(
    STerrainTileCommand* command,
    const QVector<QPoint>& acceptedVertices,
    ETerrainTileLayer layer, qint32 materialId,
    const CTerrainTransitionCatalog& catalog,
    CValidationReport* report)
{
    if(!command)
        return false;
    if(acceptedVertices.isEmpty())
        return true;

    // Classic output is physical authored artwork, but it must not leave a
    // later Modern operation to reconstruct its meaning from that artwork.
    // Establish the field from the pre-command topology once, then carry only
    // the vertices the Classic solver actually accepted in this undoable
    // transaction. In particular, constrained contextual brushes must never
    // promote their skipped footprint vertices into semantic identity.
    if(!initializeVirtualMaterialField(
           layer, catalog, report,
           ETerrainMaterialFieldRecovery::RecoverDeterministically))
    {
        command->deltas.clear();
        command->virtualVertices.clear();
        return false;
    }
    QVector<qint32>& field = layer == ETerrainTileLayer::Land
        ? m_virtualLandMaterials : m_virtualOverlayMaterials;
    const int vertexWidth = tileCountX() + 1;
    const int expectedSize = vertexWidth * (tileCountY() + 1);
    if(field.size() != expectedSize)
    {
        if(report)
            report->add("terrain.semantic_virtual_field",
                EValidationSeverity::Error,
                "The Classic terrain command could not establish its shared-corner material field");
        command->deltas.clear();
        command->virtualVertices.clear();
        return false;
    }

    QSet<int> seen;
    for(const QPoint& vertex : acceptedVertices)
    {
        if(vertex.x() < 0 || vertex.x() > tileCountX()
           || vertex.y() < 0 || vertex.y() > tileCountY())
            continue;
        const int key = vertex.y() * vertexWidth + vertex.x();
        if(seen.contains(key))
            continue;
        seen.insert(key);
        const qint32 before = field.at(key);
        if(before == materialId)
            continue;
        STerrainTileCommand::SVirtualVertexDelta delta;
        delta.globalX = vertex.x();
        delta.globalY = vertex.y();
        delta.layer = layer;
        delta.before = before;
        delta.after = materialId;
        command->virtualVertices.append(delta);
    }
    return true;
}

STerrainTileCommand CTerrainEditModel::makeSemanticTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, CValidationReport* report,
    ETerrainSemanticPaintPolicy policy)
{
    CValidationReport localReport;
    CValidationReport* effectiveReport = report ? report : &localReport;
    STerrainTileCommand command = makeBoundedSemanticTilePaint(
        brush, layer, materialId, catalog, distributionSeed, effectiveReport,
        policy);
    if(effectiveReport->hasErrors())
        return command;
    QVector<QPoint> acceptedVertices;
    for(const QPoint& cell : command.semanticAudit.paintedCoreCells)
    {
        acceptedVertices.append(cell);
        acceptedVertices.append(cell + QPoint(1, 0));
        acceptedVertices.append(cell + QPoint(1, 1));
        acceptedVertices.append(cell + QPoint(0, 1));
    }
    appendSemanticVirtualVertices(&command, acceptedVertices, layer,
                                  materialId, catalog, effectiveReport);
    return command;
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

STerrainTileCommand CTerrainEditModel::makeUniformTilePaint(
    const STerrainTileBrush& brush, ETerrainTileLayer layer,
    const STerrainTransitionSignature& uniformSignature,
    const CTerrainTransitionCatalog& catalog, quint32 distributionSeed,
    bool randomizeUniformRotation, CValidationReport* report) const
{
    STerrainTileCommand combined;
    combined.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint uniform terrain tiles")
        : QStringLiteral("Paint uniform overlay tiles");
    CTerrainEditModel staged = *this;
    const quint32 paintSeed = terrainPaintLayerSeed(distributionSeed, layer);
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
            bool available = false;
            staged.packedTile(x, y, layer, &available);
            if(!available)
                continue;
            bool selected = false;
            const quint16 packed = selectUniformInteriorTile(
                catalog, uniformSignature, x, y, paintSeed,
                randomizeUniformRotation, &selected);
            if(!selected)
            {
                if(report)
                    report->add("terrain.uniform_only_missing",
                        EValidationSeverity::Error,
                        "Uniform-only Modern paint has no exact uniform tile for the selected family");
                return STerrainTileCommand();
            }
            const SMprPackedTile decoded = SMprPackedTile::decode(packed);
            STerrainTileBrush point;
            point.centerX = float(x);
            point.centerY = float(y);
            point.radius = 0.0f;
            const STerrainTileCommand stamp = staged.makeTilePaint(
                point, layer, decoded.atlasIndex, decoded.tileIndex,
                decoded.rotation, catalog, report);
            if(report && report->hasErrors())
                return STerrainTileCommand();
            if(stamp.isEmpty())
                continue;
            if(!staged.apply(stamp, true, report))
                return STerrainTileCommand();
            appendTerrainTileCommand(&combined, stamp);
        }
    return combined;
}

QVector<QPoint> terrainVertexStrokeLine(const QPoint& from, const QPoint& to)
{
    QVector<QPoint> result;
    const int dx = to.x() - from.x();
    const int dy = to.y() - from.y();
    const int steps = qMax(qAbs(dx), qAbs(dy));
    if(steps == 0)
    {
        result.append(from);
        return result;
    }
    result.reserve(steps + 1);
    for(int step = 0; step <= steps; ++step)
    {
        const QPoint point(
            from.x() + qRound(double(dx) * step / steps),
            from.y() + qRound(double(dy) * step / steps));
        if(result.isEmpty() || result.back() != point)
            result.append(point);
    }
    return result;
}

STerrainTileCommand CTerrainEditModel::makeSemanticVertexPaint(
    int selectedCellX, int selectedCellY,
    const STerrainTileBrush& vertexBrush, ETerrainTileLayer layer,
    qint32 materialId, const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed, CValidationReport* report,
    qint32 preferredSupportId, ETerrainSemanticPaintPolicy policy)
{
    CValidationReport localReport;
    if(!report)
        report = &localReport;
    STerrainTileCommand command;
    command.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint terrain material vertices")
        : QStringLiteral("Paint overlay material vertices");
    command.semanticAudit.valid = true;
    command.semanticAudit.brush = vertexBrush;
    command.semanticAudit.layer = layer;
    command.semanticAudit.requestedMaterialId = materialId;
    command.semanticAudit.solverMode = QStringLiteral("vertex-exact");
    if(vertexBrush.radius < 0.0f || !catalog.hasMaterial(materialId)
       || selectedCellX < 0 || selectedCellX >= tileCountX()
       || selectedCellY < 0 || selectedCellY >= tileCountY())
    {
        if(report) report->add("terrain.semantic_vertex_input",
            EValidationSeverity::Error,
            "The selected material, brush, or terrain cell is unavailable");
        return command;
    }

    const int width = tileCountX();
    const int height = tileCountY();
    const int vertexWidth = width + 1;
    const int centerX = qBound(0, qRound(vertexBrush.centerX), width);
    const int centerY = qBound(0, qRound(vertexBrush.centerY), height);
    const int centerKey = centerY * vertexWidth + centerX;
    const auto vertexKey = [vertexWidth](int x, int y) {
        return y * vertexWidth + x;
    };
    const auto valuesOf = [](const STerrainTransitionSignature& signature) {
        return QVector<qint32>({signature.northWest, signature.northEast,
                                signature.southEast, signature.southWest});
    };
    const auto signatureOf = [](const QVector<qint32>& values) {
        STerrainTransitionSignature signature;
        signature.northWest = values.at(0);
        signature.northEast = values.at(1);
        signature.southEast = values.at(2);
        signature.southWest = values.at(3);
        return signature;
    };
    const auto cellCornerKeys = [vertexKey](int x, int y) {
        return QVector<int>({vertexKey(x, y), vertexKey(x + 1, y),
                             vertexKey(x + 1, y + 1), vertexKey(x, y + 1)});
    };
    const auto cornerForVertex = [](int cellX, int cellY,
                                    int pointX, int pointY) {
        if(pointY == cellY)
            return pointX == cellX ? 0 : 1;
        return pointX == cellX ? 3 : 2;
    };

    struct SVertexRequest
    {
        SVertexRequest() : x(0), y(0), distanceSquared(0) {}
        SVertexRequest(int vertexX, int vertexY, int distance)
            : x(vertexX), y(vertexY), distanceSquared(distance) {}
        int x;
        int y;
        int distanceSquared;
    };
    QVector<SVertexRequest> requestedVertices;
    const int minimumVertexX = qMax(0,
        int(std::ceil(vertexBrush.centerX - vertexBrush.radius)));
    const int maximumVertexX = qMin(width,
        int(std::floor(vertexBrush.centerX + vertexBrush.radius)));
    const int minimumVertexY = qMax(0,
        int(std::ceil(vertexBrush.centerY - vertexBrush.radius)));
    const int maximumVertexY = qMin(height,
        int(std::floor(vertexBrush.centerY + vertexBrush.radius)));
    for(int y = minimumVertexY; y <= maximumVertexY; ++y)
    {
        for(int x = minimumVertexX; x <= maximumVertexX; ++x)
        {
            const float dx = float(x) - vertexBrush.centerX;
            const float dy = float(y) - vertexBrush.centerY;
            const bool inside = vertexBrush.shape == ETerrainTileBrushShape::Square
                ? qMax(std::fabs(dx), std::fabs(dy)) <= vertexBrush.radius
                : std::sqrt(dx * dx + dy * dy) <= vertexBrush.radius;
            if(inside)
                requestedVertices.append({x, y,
                    (x - centerX) * (x - centerX)
                        + (y - centerY) * (y - centerY)});
        }
    }
    if(requestedVertices.isEmpty())
        requestedVertices.append({centerX, centerY, 0});
    std::sort(requestedVertices.begin(), requestedVertices.end(),
        [](const SVertexRequest& left, const SVertexRequest& right) {
            if(left.distanceSquared != right.distanceSquared)
                return left.distanceSquared < right.distanceSquared;
            if(left.y != right.y)
                return left.y < right.y;
            return left.x < right.x;
        });

    QSet<int> affectedCells;
    for(const SVertexRequest& vertex : requestedVertices)
    {
        for(int y : {vertex.y - 1, vertex.y})
        {
            for(int x : {vertex.x - 1, vertex.x})
            {
                if(x < 0 || x >= width || y < 0 || y >= height)
                    continue;
                bool available = false;
                packedTile(x, y, layer, &available);
                if(available)
                    affectedCells.insert(y * width + x);
            }
        }
    }
    if(affectedCells.isEmpty())
    {
        if(report) report->add(layer == ETerrainTileLayer::Overlay
                ? "terrain.overlay_vertex_inactive"
                : "terrain.semantic_vertex_empty",
            EValidationSeverity::Warning,
            layer == ETerrainTileLayer::Overlay
                ? "The vertex brush did not touch an active overlay tile"
                : "The vertex brush did not touch editable terrain");
        return command;
    }

    QMap<int, quint16> originalTiles;
    QMap<int, STerrainTransitionSignature> scratch;
    QSet<int> unknownCells;
    for(int index : affectedCells)
    {
        const int x = index % width;
        const int y = index / width;
        bool available = false;
        const quint16 packed = packedTile(x, y, layer, &available);
        bool decoded = false;
        const STerrainTransitionSignature signature = available
            ? catalog.gridSignatureForPacked(packed, &decoded)
            : STerrainTransitionSignature();
        if(!available)
            continue;
        originalTiles.insert(index, packed);
        if(decoded)
            scratch.insert(index, signature);
        else
            unknownCells.insert(index);
    }

    STerrainTransitionSignature pure;
    pure.northWest = pure.northEast = pure.southEast = pure.southWest = materialId;
    bool pureAvailable = false;
    catalog.selectForPaint(pure, 0, 0, distributionSeed, &pureAvailable);
    const bool cornerOnly = !pureAvailable;
    const bool contextualStamp = preferredSupportId > 0
        && preferredSupportId != materialId;
    if(contextualStamp)
    {
        command.semanticAudit.backgroundMaterialId = preferredSupportId;
        command.semanticAudit.materialPath = {materialId, preferredSupportId};
    }
    const auto supportCompatibleWithContextualStamp =
        [&](const STerrainTransitionSignature& signature, int cellX,
            int cellY, int pointX, int pointY) {
            if(!contextualStamp
               || policy != ETerrainSemanticPaintPolicy::ExactExistingOnly)
                return true;
            const QVector<int> keys = cellCornerKeys(cellX, cellY);
            const QVector<qint32> values = valuesOf(signature);
            const int ownerCorner =
                cornerForVertex(cellX, cellY, pointX, pointY);
            for(int corner = 0; corner < values.size(); ++corner)
            {
                if(corner == ownerCorner)
                    continue;
                const qint32 value = values.at(corner);
                if(value != preferredSupportId && value != materialId)
                    return false;
            }
            return keys.size() == 4;
        };
    const QVector<STerrainTransitionSignature> authored = catalog.gridSignatures();
    QSet<int> acceptedVertices;
    int skippedVertices = 0;
    int fallbackCells = 0;
    int candidateChecks = 0;

    const auto chooseClosest = [&](int cellX, int cellY,
            const QVector<qint32>& desired, const QSet<int>& requiredVertices,
            STerrainTransitionSignature* selected) {
        const QVector<int> keys = cellCornerKeys(cellX, cellY);
        QSet<qint32> existingFamilies;
        for(qint32 value : desired)
            if(value >= 0)
                existingFamilies.insert(value);
        const bool requestedUniform = std::all_of(
            desired.cbegin(), desired.cend(), [materialId](qint32 value) {
                return value == materialId;
            });
        bool foundTransition = false;
        bool foundUniform = false;
        int bestTransitionCost = std::numeric_limits<int>::max();
        int bestUniformCost = std::numeric_limits<int>::max();
        STerrainTransitionSignature bestTransition;
        STerrainTransitionSignature bestUniform;
        for(const STerrainTransitionSignature& candidate : authored)
        {
            ++candidateChecks;
            const QVector<qint32> values = valuesOf(candidate);
            bool compatible = true;
            int cost = 0;
            for(int corner = 0; corner < 4; ++corner)
            {
                const bool required = requiredVertices.contains(keys.at(corner));
                if(required && values.at(corner) != materialId)
                {
                    compatible = false;
                    break;
                }
                if(desired.at(corner) >= 0
                   && values.at(corner) != desired.at(corner))
                    cost += existingFamilies.contains(values.at(corner)) ? 4 : 32;
                if(!required && values.at(corner) == materialId
                   && desired.at(corner) != materialId)
                    cost += 2;
            }
            if(!compatible)
                continue;
            const bool selectedUniform = values.at(0) == materialId
                && values.at(1) == materialId
                && values.at(2) == materialId
                && values.at(3) == materialId;
            if(selectedUniform)
            {
                // Uniform artwork is never a substitute for a requested
                // blend. The virtual production path generates the exact
                // missing tuple; this legacy helper must fail closed when it
                // cannot find a non-uniform authored alternative.
                if(requestedUniform
                   && (!foundUniform || cost < bestUniformCost))
                {
                    foundUniform = true;
                    bestUniformCost = cost;
                    bestUniform = candidate;
                }
            }
            else if(!foundTransition || cost < bestTransitionCost)
            {
                foundTransition = true;
                bestTransitionCost = cost;
                bestTransition = candidate;
            }
        }
        if(selected && (foundTransition || foundUniform))
            *selected = foundTransition ? bestTransition : bestUniform;
        return foundTransition || foundUniform;
    };

    for(const SVertexRequest& vertex : requestedVertices)
    {
        const int key = vertexKey(vertex.x, vertex.y);
        const bool centerVertex = key == centerKey;
        const bool contextualCenter = contextualStamp && centerVertex;
        struct SPlannedCell
        {
            SPlannedCell() : index(0), fallback(false) {}
            SPlannedCell(int cellIndex,
                         const STerrainTransitionSignature& cellSignature,
                         bool usedFallback)
                : index(cellIndex), signature(cellSignature),
                  fallback(usedFallback) {}
            int index;
            STerrainTransitionSignature signature;
            bool fallback;
        };
        QVector<SPlannedCell> planned;
        bool vertexExact = true;
        bool vertexPossible = true;
        bool constrainedDetailRejected = false;
        for(int y : {vertex.y - 1, vertex.y})
        {
            for(int x : {vertex.x - 1, vertex.x})
            {
                if(x < 0 || x >= width || y < 0 || y >= height)
                    continue;
                const int index = y * width + x;
                if(!originalTiles.contains(index))
                    continue;
                if(contextualCenter && scratch.contains(index)
                   && !supportCompatibleWithContextualStamp(
                       scratch.value(index), x, y, vertex.x, vertex.y))
                {
                    vertexPossible = false;
                    break;
                }
                QVector<qint32> desired = contextualCenter
                    ? QVector<qint32>({preferredSupportId, preferredSupportId,
                                      preferredSupportId, preferredSupportId})
                    : scratch.contains(index)
                        ? valuesOf(scratch.value(index))
                        : QVector<qint32>({-1, -1, -1, -1});
                if(contextualCenter && scratch.contains(index))
                {
                    const QVector<qint32> existing = valuesOf(scratch.value(index));
                    for(int existingCorner = 0; existingCorner < 4; ++existingCorner)
                        if(existing.at(existingCorner) == materialId)
                            desired[existingCorner] = materialId;
                }
                const int corner = cornerForVertex(x, y, vertex.x, vertex.y);
                desired[corner] = materialId;
                const STerrainTransitionSignature exact = signatureOf(desired);
                bool exactFound = false;
                catalog.selectForPaint(exact, x, y, distributionSeed, &exactFound);
                if(exactFound)
                {
                    planned.append({index, exact, false});
                    continue;
                }
                vertexExact = false;
                if(policy == ETerrainSemanticPaintPolicy::ExactExistingOnly)
                {
                    vertexPossible = false;
                    break;
                }
                // Transition-only families may cover a larger brush only
                // while every incident tile remains exact. Avoid an atlas-wide
                // nearest-candidate scan for a vertex that must be skipped.
                if((cornerOnly || contextualStamp) && !centerVertex)
                {
                    constrainedDetailRejected = true;
                    break;
                }
                QSet<int> required = acceptedVertices;
                required.insert(key);
                STerrainTransitionSignature closest;
                if(!chooseClosest(x, y, desired, required, &closest))
                {
                    vertexPossible = false;
                    break;
                }
                planned.append({index, closest, true});
            }
            if(!vertexPossible || constrainedDetailRejected)
                break;
        }

        if(!vertexPossible || constrainedDetailRejected
           || ((cornerOnly || contextualStamp) && !centerVertex && !vertexExact))
        {
            ++skippedVertices;
            continue;
        }
        for(const SPlannedCell& cell : planned)
        {
            scratch.insert(cell.index, cell.signature);
            fallbackCells += cell.fallback ? 1 : 0;
        }
        acceptedVertices.insert(key);
        command.semanticAudit.paintedVertices.append(QPoint(vertex.x, vertex.y));
    }

    if(!acceptedVertices.contains(centerKey))
    {
        if(report)
        {
            report->add("terrain.semantic_vertex_center",
                EValidationSeverity::Error,
                "No authored atlas combination can place this material at the selected terrain vertex");
        }
        return command;
    }

    QVector<int> orderedCells = scratch.keys().toVector();
    std::sort(orderedCells.begin(), orderedCells.end());
    for(int index : orderedCells)
    {
        const int x = index % width;
        const int y = index / width;
        const STerrainTransitionSignature selected = scratch.value(index);
        bool found = false;
        const quint16 after = catalog.selectForPaint(
            selected, x, y, distributionSeed, &found);
        if(!found)
        {
            if(report) report->add("terrain.semantic_vertex_internal",
                EValidationSeverity::Error,
                "The vertex resolver selected a signature absent from the Material atlas",
                QString("%1,%2").arg(x).arg(y));
            command.deltas.clear();
            return command;
        }
        command.semanticAudit.affectedCells.append(QPoint(x, y));
        const quint16 before = originalTiles.value(index);
        if(after == before)
            continue;
        STerrainTileDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.layer = layer;
        delta.before = before;
        delta.after = after;
        delta.hasSemanticRequest = true;
        delta.brushCenterX = centerX;
        delta.brushCenterY = centerY;
        delta.requestedMaterialId = materialId;
        delta.requestedSignature = selected;
        command.deltas.append(delta);
    }
    command.semanticAudit.solverNodes = candidateChecks;
    command.semanticAudit.solverMode = contextualStamp
        ? QStringLiteral("vertex-context-stamp")
        : fallbackCells > 0
        ? QStringLiteral("vertex-bounded-fallback")
        : skippedVertices > 0
            ? QStringLiteral("vertex-constrained-detail")
            : QStringLiteral("vertex-exact");
    if(!unknownCells.isEmpty() && report)
        report->add("terrain.semantic_vertex_unknown_recovery",
            EValidationSeverity::Warning,
            "The vertex brush replaced atlas tiles that were not described by the active profile",
            QString::number(unknownCells.size()));
    if(fallbackCells > 0 && report)
        report->add("terrain.semantic_vertex_fallback",
            EValidationSeverity::Warning,
            "The atlas lacks some requested vertex combinations; the brush used the closest local authored tiles",
            QString::number(fallbackCells));
    if(skippedVertices > 0 && report)
        report->add("terrain.semantic_vertex_detail_limit",
            EValidationSeverity::Warning,
            "This detail material cannot occupy every requested vertex because the atlas lacks the required multi-corner tiles",
            QString::number(skippedVertices));
    if(!report->hasErrors())
        appendSemanticVirtualVertices(
            &command, command.semanticAudit.paintedVertices, layer,
            materialId, catalog, report);
    return command;
}

STerrainTileCommand CTerrainEditModel::makeSemanticVertexStrokePaint(
    const QVector<QPoint>& requestedVertices,
    const QVector<QPoint>& anchorVertices,
    ETerrainTileLayer layer, qint32 materialId,
    const CTerrainTransitionCatalog& catalog, quint32 distributionSeed,
    CValidationReport* report, ETerrainSemanticPaintPolicy policy)
{
    CValidationReport localReport;
    if(!report)
        report = &localReport;
    STerrainTileCommand command;
    command.description = layer == ETerrainTileLayer::Land
        ? QStringLiteral("Paint terrain material stroke")
        : QStringLiteral("Paint overlay material stroke");
    command.semanticAudit.valid = true;
    command.semanticAudit.layer = layer;
    command.semanticAudit.requestedMaterialId = materialId;
    command.semanticAudit.solverMode = QStringLiteral("vertex-stroke-exact");

    if(!catalog.hasMaterial(materialId))
    {
        if(report) report->add("terrain.semantic_stroke_material",
            EValidationSeverity::Error,
            "The selected material is absent from the active Material atlas");
        return command;
    }

    const int width = tileCountX();
    const int height = tileCountY();
    const int vertexWidth = width + 1;
    const auto vertexKey = [vertexWidth](const QPoint& point) {
        return point.y() * vertexWidth + point.x();
    };
    QSet<int> requested;
    for(const QPoint& point : requestedVertices)
        if(point.x() >= 0 && point.x() <= width
           && point.y() >= 0 && point.y() <= height)
            requested.insert(vertexKey(point));
    Q_UNUSED(anchorVertices);
    if(requested.isEmpty())
    {
        if(report) report->add("terrain.semantic_stroke_empty",
            EValidationSeverity::Warning,
            "The stroke did not cover an editable terrain vertex");
        return command;
    }

    struct SCellState
    {
        quint16 packed{0};
        STerrainTransitionSignature original;
    };
    QMap<int, SCellState> cells;
    QSet<int> unknownCells;
    QSet<int> blockedVertices;
    for(int key : requested)
    {
        const int vertexX = key % vertexWidth;
        const int vertexY = key / vertexWidth;
        for(int cellY : {vertexY - 1, vertexY})
        {
            for(int cellX : {vertexX - 1, vertexX})
            {
                if(cellX < 0 || cellX >= width || cellY < 0 || cellY >= height)
                    continue;
                const int cellKey = cellY * width + cellX;
                bool available = false;
                const quint16 packed = packedTile(cellX, cellY, layer, &available);
                if(!available)
                    continue;
                bool decoded = false;
                const STerrainTransitionSignature signature =
                    catalog.gridSignatureForPacked(packed, &decoded);
                if(!decoded)
                {
                    unknownCells.insert(cellKey);
                    blockedVertices.insert(key);
                    continue;
                }
                SCellState state;
                state.packed = packed;
                state.original = signature;
                cells.insert(cellKey, state);
            }
        }
    }
    if(cells.isEmpty())
    {
        if(report) report->add(layer == ETerrainTileLayer::Overlay
                ? "terrain.overlay_vertex_inactive"
                : "terrain.semantic_stroke_unavailable",
            EValidationSeverity::Warning,
            layer == ETerrainTileLayer::Overlay
                ? "The stroke did not touch an active overlay surface"
                : "The stroke did not touch described terrain tiles");
        return command;
    }
    const int originallyRequested = requested.size();
    requested.subtract(blockedVertices);

    const auto corners = [](const STerrainTransitionSignature& signature) {
        return QVector<qint32>({signature.northWest, signature.northEast,
                               signature.southEast, signature.southWest});
    };
    const auto cellCornerKeys = [width, vertexWidth](int cellKey) {
        const int x = cellKey % width;
        const int y = cellKey / width;
        return QVector<int>({y * vertexWidth + x,
                             y * vertexWidth + x + 1,
                             (y + 1) * vertexWidth + x + 1,
                             (y + 1) * vertexWidth + x});
    };
    const auto signatureFromValues = [](const QVector<qint32>& values) {
        STerrainTransitionSignature result;
        result.northWest = values.at(0);
        result.northEast = values.at(1);
        result.southEast = values.at(2);
        result.southWest = values.at(3);
        return result;
    };

    QMap<int, QMap<qint32, int>> observedByVertex;
    QMap<int, SCellState> activeCells;
    for(auto entry = cells.constBegin(); entry != cells.constEnd(); ++entry)
    {
        const QVector<int> keys = cellCornerKeys(entry.key());
        bool incidentToRequest = false;
        for(int key : keys)
            incidentToRequest |= requested.contains(key);
        if(!incidentToRequest)
            continue;
        activeCells.insert(entry.key(), entry.value());
        const QVector<qint32> values = corners(entry.value().original);
        for(int corner = 0; corner < 4; ++corner)
            ++observedByVertex[keys.at(corner)][values.at(corner)];
    }
    QMap<int, STerrainTransitionSignature> resolvedSignatures;
    QSet<int> unsupportedVertices = blockedVertices;
    int solverNodes = 0;
    QSet<int> hardEdgeCells;
    QSet<int> repairedBoundaryCells;
    QSet<int> compatibleBoundaryCells;

    // Consult all four incident cells at each shared vertex. This repairs a
    // prior collapsed uniform boundary only when the surrounding authored
    // terrain provides an unambiguous material consensus.
    QMap<int, QMap<qint32, int>> contextObservedByVertex;
    for(auto entry = activeCells.constBegin(); entry != activeCells.constEnd(); ++entry)
    {
        for(int key : cellCornerKeys(entry.key()))
        {
            if(contextObservedByVertex.contains(key))
                continue;
            const int vertexX = key % vertexWidth;
            const int vertexY = key / vertexWidth;
            const QPoint incidentCells[] = {
                QPoint(vertexX, vertexY),
                QPoint(vertexX - 1, vertexY),
                QPoint(vertexX - 1, vertexY - 1),
                QPoint(vertexX, vertexY - 1)};
            const int incidentCorners[] = {0, 1, 2, 3};
            for(int index = 0; index < 4; ++index)
            {
                const QPoint cell = incidentCells[index];
                if(cell.x() < 0 || cell.x() >= width
                   || cell.y() < 0 || cell.y() >= height)
                    continue;
                bool available = false;
                const quint16 packed = packedTile(
                    cell.x(), cell.y(), layer, &available);
                if(!available)
                    continue;
                bool decoded = false;
                const STerrainTransitionSignature signature =
                    catalog.gridSignatureForPacked(packed, &decoded);
                if(!decoded)
                    continue;
                const QVector<qint32> values = corners(signature);
                ++contextObservedByVertex[key][values.at(incidentCorners[index])];
            }
            const QMap<qint32, int> observedValues =
                contextObservedByVertex.value(key);
            for(auto observed = observedValues.constBegin();
                observed != observedValues.constEnd(); ++observed)
                observedByVertex[key][observed.key()] += observed.value();
        }
    }

    const auto outsideConsensus = [&](int key, qint32 cellValue,
                                      bool* found) {
        QMap<qint32, int> counts = contextObservedByVertex.value(key);
        if(counts.contains(cellValue))
        {
            const int remaining = counts.value(cellValue) - 1;
            if(remaining > 0)
                counts[cellValue] = remaining;
            else
                counts.remove(cellValue);
        }
        qint32 best = cellValue;
        int bestCount = 0;
        bool tied = false;
        for(auto value = counts.constBegin(); value != counts.constEnd(); ++value)
        {
            if(value.value() > bestCount)
            {
                best = value.key();
                bestCount = value.value();
                tied = false;
            }
            else if(value.value() == bestCount)
                tied = true;
        }
        if(found)
            *found = bestCount >= 2 && !tied && best != cellValue;
        return best;
    };

    STerrainTransitionSignature uniform;
    uniform.northWest = uniform.northEast = materialId;
    uniform.southEast = uniform.southWest = materialId;
    bool uniformChecked = false;
    bool uniformFound = false;
    bool missingWithoutFallback = false;
    QSet<int> exactOnlyBlockedVertices;
    const QVector<STerrainTransitionSignature> authoredSignatures =
        catalog.gridSignatures();
    for(auto entry = activeCells.constBegin();
        entry != activeCells.constEnd(); ++entry)
    {
        const QVector<int> keys = cellCornerKeys(entry.key());
        const QVector<qint32> originalValues = corners(entry.value().original);
        QVector<qint32> values;
        for(int corner = 0; corner < keys.size(); ++corner)
            values.append(requested.contains(keys.at(corner))
                ? materialId : originalValues.at(corner));
        STerrainTransitionSignature exact = signatureFromValues(values);
        bool found = false;
        catalog.selectForPaint(exact, entry.key() % width,
                               entry.key() / width, distributionSeed, &found);
        ++solverNodes;

        const bool collapsedSelected =
            exact.northWest == materialId && exact.northEast == materialId
            && exact.southEast == materialId && exact.southWest == materialId;
        if(collapsedSelected || !found)
        {
            QVector<qint32> repaired = values;
            bool changed = false;
            for(int corner = 0; corner < keys.size(); ++corner)
            {
                if(requested.contains(keys.at(corner)))
                    continue;
                bool consensusFound = false;
                const qint32 consensus = outsideConsensus(
                    keys.at(corner), originalValues.at(corner),
                    &consensusFound);
                if(consensusFound)
                {
                    repaired[corner] = consensus;
                    changed = true;
                }
            }
            if(changed)
            {
                const STerrainTransitionSignature candidate =
                    signatureFromValues(repaired);
                bool candidateFound = false;
                catalog.selectForPaint(candidate, entry.key() % width,
                                       entry.key() / width, distributionSeed,
                                       &candidateFound);
                ++solverNodes;
                if(candidateFound)
                {
                    exact = candidate;
                    found = true;
                    repairedBoundaryCells.insert(entry.key());
                }
            }
        }

        const auto isSelectedUniform = [materialId](
            const STerrainTransitionSignature& signature) {
            return signature.northWest == materialId
                && signature.northEast == materialId
                && signature.southEast == materialId
                && signature.southWest == materialId;
        };
        if(!found || isSelectedUniform(exact))
        {
            bool compatibleFound = false;
            int bestScore = std::numeric_limits<int>::max();
            STerrainTransitionSignature bestCompatible;
            for(const STerrainTransitionSignature& candidate : authoredSignatures)
            {
                const QVector<qint32> candidateValues = corners(candidate);
                bool compatible = true;
                bool uniformSelected = true;
                int changedCorners = 0;
                int observedSupport = 0;
                for(int corner = 0; corner < keys.size(); ++corner)
                {
                    const int key = keys.at(corner);
                    const qint32 candidateValue = candidateValues.at(corner);
                    uniformSelected &= candidateValue == materialId;
                    if(requested.contains(key))
                    {
                        if(candidateValue != materialId)
                        {
                            compatible = false;
                            break;
                        }
                        continue;
                    }
                    if(candidateValue != originalValues.at(corner)
                       && !contextObservedByVertex.value(key).contains(candidateValue))
                    {
                        compatible = false;
                        break;
                    }
                    changedCorners += candidateValue != originalValues.at(corner);
                    observedSupport += contextObservedByVertex.value(key)
                        .value(candidateValue);
                }
                if(!compatible || uniformSelected)
                    continue;
                const int score = changedCorners * 1000 - observedSupport;
                const auto lexicallyBefore = [](
                    const STerrainTransitionSignature& left,
                    const STerrainTransitionSignature& right) {
                    if(left.northWest != right.northWest)
                        return left.northWest < right.northWest;
                    if(left.northEast != right.northEast)
                        return left.northEast < right.northEast;
                    if(left.southEast != right.southEast)
                        return left.southEast < right.southEast;
                    return left.southWest < right.southWest;
                };
                if(!compatibleFound || score < bestScore
                   || (score == bestScore
                       && lexicallyBefore(candidate, bestCompatible)))
                {
                    compatibleFound = true;
                    bestScore = score;
                    bestCompatible = candidate;
                }
            }
            solverNodes += authoredSignatures.size();
            if(compatibleFound)
            {
                exact = bestCompatible;
                found = true;
                compatibleBoundaryCells.insert(entry.key());
            }
        }
        if(found)
            resolvedSignatures.insert(entry.key(), exact);
        else
        {
            if(policy == ETerrainSemanticPaintPolicy::ExactExistingOnly)
            {
                for(int key : keys)
                    if(requested.contains(key))
                        exactOnlyBlockedVertices.insert(key);
                continue;
            }
            if(!uniformChecked)
            {
                catalog.selectForPaint(uniform, 0, 0, distributionSeed,
                                       &uniformFound);
                ++solverNodes;
                uniformChecked = true;
            }
            if(uniformFound)
            {
                resolvedSignatures.insert(entry.key(), uniform);
                hardEdgeCells.insert(entry.key());
            }
            else
            {
                missingWithoutFallback = true;
                for(int key : keys)
                    if(requested.contains(key))
                        unsupportedVertices.insert(key);
            }
        }
    }

    if(policy == ETerrainSemanticPaintPolicy::ExactExistingOnly
       && !exactOnlyBlockedVertices.isEmpty())
    {
        QSet<int> remaining = requested;
        remaining.subtract(exactOnlyBlockedVertices);
        QVector<QPoint> retryVertices;
        QVector<int> ordered = remaining.values().toVector();
        std::sort(ordered.begin(), ordered.end());
        for(int key : ordered)
            retryVertices.append(QPoint(key % vertexWidth, key / vertexWidth));
        CValidationReport retryReport;
        STerrainTileCommand retry = makeSemanticVertexStrokePaint(
            retryVertices, retryVertices, layer, materialId, catalog,
            distributionSeed, &retryReport, policy);
        retry.semanticAudit.solverMode = QStringLiteral(
            "vertex-stroke-exact-existing-partial");
        if(report)
        {
            report->add("terrain.semantic_stroke_exact_only_skip",
                EValidationSeverity::Warning,
                "Classic Auto Safe left vertices requiring absent authored blends unchanged",
                QString::number(exactOnlyBlockedVertices.size()));
            for(const SValidationFinding& finding : retryReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        }
        return retry;
    }

    if(missingWithoutFallback)
    {
        resolvedSignatures.clear();
        hardEdgeCells.clear();
        unsupportedVertices.unite(requested);
    }

    QMap<int, QSet<qint32>> resolvedVertices;
    QSet<int> verticesWithHardEdgeCells;
    bool invariantFailed = false;
    for(auto entry = resolvedSignatures.constBegin();
        entry != resolvedSignatures.constEnd(); ++entry)
    {
        const QVector<int> keys = cellCornerKeys(entry.key());
        const QVector<qint32> values = corners(entry.value());
        for(int corner = 0; corner < 4; ++corner)
        {
            const int key = keys.at(corner);
            const qint32 value = values.at(corner);
            resolvedVertices[key].insert(value);
            if(hardEdgeCells.contains(entry.key()))
                verticesWithHardEdgeCells.insert(key);
            if(value != materialId
               && !observedByVertex.value(key).contains(value))
                invariantFailed = true;
        }
    }
    for(auto entry = resolvedVertices.constBegin();
        entry != resolvedVertices.constEnd(); ++entry)
    {
        if(!requested.contains(entry.key()))
            continue;
        if(entry.value().size() <= 1)
            continue;
        if(!verticesWithHardEdgeCells.contains(entry.key())
           || !entry.value().contains(materialId))
            invariantFailed = true;
        for(qint32 value : entry.value())
            if(value != materialId
               && !observedByVertex.value(entry.key()).contains(value))
                invariantFailed = true;
    }
    if(invariantFailed)
    {
        if(report) report->add("terrain.semantic_stroke_vertex_conflict",
            EValidationSeverity::Error,
            "The stroke resolver produced conflicting or unrelated material identities at a shared terrain vertex");
        return command;
    }

    QVector<int> activeKeys = requested.values().toVector();
    std::sort(activeKeys.begin(), activeKeys.end());
    for(int key : activeKeys)
        if(!unsupportedVertices.contains(key))
            command.semanticAudit.paintedVertices.append(
                QPoint(key % vertexWidth, key / vertexWidth));
    command.semanticAudit.solverNodes = solverNodes;
    if(!hardEdgeCells.isEmpty())
        command.semanticAudit.solverMode =
            QStringLiteral("vertex-stroke-local-hard-edge");
    else if(!repairedBoundaryCells.isEmpty())
        command.semanticAudit.solverMode =
            QStringLiteral("vertex-stroke-authored-boundary-repair");
    else if(!compatibleBoundaryCells.isEmpty())
        command.semanticAudit.solverMode =
            QStringLiteral("vertex-stroke-compatible-authored");
    else if(!unsupportedVertices.isEmpty())
        command.semanticAudit.solverMode =
            QStringLiteral("vertex-stroke-unavailable");

    QVector<int> orderedCells = resolvedSignatures.keys().toVector();
    std::sort(orderedCells.begin(), orderedCells.end());
    for(int cellKey : orderedCells)
    {
        const int x = cellKey % width;
        const int y = cellKey / width;
        const STerrainTransitionSignature desired =
            resolvedSignatures.value(cellKey, cells.value(cellKey).original);
        bool found = false;
        const SCellState& cell = cells.value(cellKey);
        const quint16 after = catalog.selectForPaint(
            desired, x, y, distributionSeed, &found);
        if(!found)
        {
            if(report) report->add("terrain.semantic_stroke_internal",
                EValidationSeverity::Error,
                "The whole-stroke resolver produced a signature absent from the Material atlas",
                QString("%1,%2").arg(x).arg(y));
            command.deltas.clear();
            return command;
        }
        command.semanticAudit.affectedCells.append(QPoint(x, y));
        if(after == cell.packed)
            continue;
        STerrainTileDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.layer = layer;
        delta.before = cell.packed;
        delta.after = after;
        delta.hasSemanticRequest = true;
        delta.requestedMaterialId = materialId;
        delta.requestedSignature = desired;
        command.deltas.append(delta);
    }
    if(!unknownCells.isEmpty() && report)
        report->add("terrain.semantic_stroke_unknown",
            EValidationSeverity::Warning,
            "Some requested vertices touch atlas tiles not described by the active profile and were left unchanged",
            QString::number(unknownCells.size()));
    if(!unsupportedVertices.isEmpty() && report)
        report->add("terrain.semantic_stroke_atlas_limit",
            EValidationSeverity::Warning,
            "The Material atlas cannot represent this material across the requested adjacent vertices. Nothing was changed; use a single-vertex junction stamp or add the missing atlas transitions",
            QString("%1 of %2 vertices were not paintable")
                .arg(unsupportedVertices.size()).arg(originallyRequested));
    if(!hardEdgeCells.isEmpty() && report)
        report->add("terrain.semantic_stroke_hard_edge",
            EValidationSeverity::Warning,
            "The Material atlas is missing some boundary transitions. Exact blends were preserved and only the unresolved boundary tiles used the selected material's uniform hard-edge fallback",
            QString("%1 boundary tile(s) used hard-edge fallback")
                .arg(hardEdgeCells.size()));
    if(!report->hasErrors())
        appendSemanticVirtualVertices(
            &command, command.semanticAudit.paintedVertices, layer,
            materialId, catalog, report);
    return command;
}

STerrainTileCommand CTerrainEditModel::makeSemanticCornerMaterialPaint(
    int selectedCellX, int selectedCellY, int vertexX, int vertexY,
    ETerrainTileLayer layer, qint32 materialId,
    const CTerrainTransitionCatalog& catalog, quint32 distributionSeed,
    CValidationReport* report)
{
    STerrainTileBrush vertexBrush;
    vertexBrush.centerX = vertexX;
    vertexBrush.centerY = vertexY;
    vertexBrush.radius = 0.0f;
    return makeSemanticVertexPaint(
        selectedCellX, selectedCellY, vertexBrush, layer, materialId,
        catalog, distributionSeed, report);
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

STerrainTileCommand CTerrainEditModel::makeVirtualMaterialPaint(
    int selectedCellX,int selectedCellY,const STerrainTileBrush& brush,
    ETerrainTileLayer layer,qint32 materialId,
    const CTerrainTransitionCatalog& catalog,quint32 distributionSeed,
    CValidationReport* report,bool randomizeUniformRotation)
{
    QVector<QPoint> requested;
    const int minX=qMax(0,int(std::ceil(brush.centerX-brush.radius)));
    const int maxX=qMin(tileCountX(),int(std::floor(brush.centerX+brush.radius)));
    const int minY=qMax(0,int(std::ceil(brush.centerY-brush.radius)));
    const int maxY=qMin(tileCountY(),int(std::floor(brush.centerY+brush.radius)));
    for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x)
    {
        const float dx=float(x)-brush.centerX,dy=float(y)-brush.centerY;
        const bool inside=brush.shape==ETerrainTileBrushShape::Square
            ?qMax(std::fabs(dx),std::fabs(dy))<=brush.radius
            :std::sqrt(dx*dx+dy*dy)<=brush.radius;
        if(inside)requested.append(QPoint(x,y));
    }
    return makeVirtualMaterialPaintVertices(selectedCellX,selectedCellY,
        requested,QSet<int>(),brush,layer,materialId,catalog,distributionSeed,report,
        randomizeUniformRotation);
}

STerrainTileCommand CTerrainEditModel::makeVirtualTileMaterialPaint(
    const STerrainTileBrush& tileBrush,ETerrainTileLayer layer,
    qint32 materialId,const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed,CValidationReport* report,
    bool randomizeUniformRotation)
{
    QSet<QPair<int,int>> vertexCoordinates;
    QSet<int> previewCoreCells;
    const int minX=qMax(0,int(std::ceil(tileBrush.centerX-tileBrush.radius)));
    const int maxX=qMin(tileCountX()-1,
        int(std::floor(tileBrush.centerX+tileBrush.radius)));
    const int minY=qMax(0,int(std::ceil(tileBrush.centerY-tileBrush.radius)));
    const int maxY=qMin(tileCountY()-1,
        int(std::floor(tileBrush.centerY+tileBrush.radius)));
    for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x)
    {
        const float dx=float(x)-tileBrush.centerX;
        const float dy=float(y)-tileBrush.centerY;
        const bool inside=tileBrush.shape==ETerrainTileBrushShape::Square
            ?qMax(std::fabs(dx),std::fabs(dy))<=tileBrush.radius
            :std::sqrt(dx*dx+dy*dy)<=tileBrush.radius;
        if(!inside)continue;
        previewCoreCells.insert(y*tileCountX()+x);
        vertexCoordinates.insert(qMakePair(x,y));
        vertexCoordinates.insert(qMakePair(x+1,y));
        vertexCoordinates.insert(qMakePair(x+1,y+1));
        vertexCoordinates.insert(qMakePair(x,y+1));
    }
    QVector<QPoint> requested;
    requested.reserve(vertexCoordinates.size());
    for(const QPair<int,int>& coordinate:vertexCoordinates)
        requested.append(QPoint(coordinate.first,coordinate.second));
    std::sort(requested.begin(),requested.end(),[](const QPoint& left,
        const QPoint& right){return left.y()!=right.y()
            ?left.y()<right.y():left.x()<right.x();});
    if(requested.isEmpty())return {};
    STerrainTileCommand command=makeVirtualMaterialPaintVertices(
        qBound(0,qRound(tileBrush.centerX),tileCountX()-1),
        qBound(0,qRound(tileBrush.centerY),tileCountY()-1),requested,
        previewCoreCells,tileBrush,layer,materialId,catalog,
        distributionSeed,report,randomizeUniformRotation);
    QVector<int> sortedCore=previewCoreCells.values().toVector();
    std::sort(sortedCore.begin(),sortedCore.end());
    for(int cell:sortedCore)
        command.semanticAudit.paintedCoreCells.append(
            QPoint(cell%tileCountX(),cell/tileCountX()));
    return command;
}

STerrainTileCommand CTerrainEditModel::makeVirtualMaterialPaintVertices(
    int selectedCellX,int selectedCellY,const QVector<QPoint>& requestedVertices,
    const QSet<int>& previewCoreCells,const STerrainTileBrush& auditBrush,
    ETerrainTileLayer layer,
    qint32 materialId,const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed,CValidationReport* report,
    bool randomizeUniformRotation)
{
    STerrainTileCommand command;
    command.description=layer==ETerrainTileLayer::Land
        ?QStringLiteral("Paint virtual terrain materials")
        :QStringLiteral("Paint virtual overlay materials");
    command.semanticAudit.valid=true;command.semanticAudit.brush=auditBrush;
    command.semanticAudit.layer=layer;command.semanticAudit.requestedMaterialId=materialId;
    command.semanticAudit.solverMode=QStringLiteral("virtual-unblended-preview");
    if(auditBrush.radius<0.0f||!catalog.hasMaterial(materialId)
       ||selectedCellX<0||selectedCellX>=tileCountX()
       ||selectedCellY<0||selectedCellY>=tileCountY())
    {
        if(report)report->add("terrain.virtual_paint_input",EValidationSeverity::Error,
            "The selected virtual material, brush, or terrain cell is unavailable");
        return command;
    }
    QVector<qint32>& active=layer==ETerrainTileLayer::Land
        ?m_virtualLandMaterials:m_virtualOverlayMaterials;
    const qint32 unset=std::numeric_limits<qint32>::min();
    const int vertexWidth=tileCountX()+1;
    if(active.isEmpty())
    {
        // Planning must never establish hidden live state.  Build the first
        // Modern stamp on a copy, then replace its sparse vertex edits with
        // one complete before/after field delta.  The resulting command owns
        // recovery, visible preview, undo, and redo atomically; refusal or a
        // no-op leaves the live model exactly fieldless.
        CTerrainEditModel staged=*this;
        if(!staged.initializeVirtualMaterialField(
               layer, catalog, report,
               ETerrainMaterialFieldRecovery::RecoverDeterministically))
            return command;
        STerrainTileCommand stagedCommand=
            staged.makeVirtualMaterialPaintVertices(
                selectedCellX,selectedCellY,requestedVertices,
                previewCoreCells,auditBrush,layer,materialId,catalog,
                distributionSeed,report,randomizeUniformRotation);
        if(stagedCommand.isEmpty())
            return stagedCommand;
        if(!staged.apply(stagedCommand,true,report))
            return STerrainTileCommand();

        STerrainTileCommand::SVirtualFieldDelta fieldDelta;
        fieldDelta.layer=layer;
        fieldDelta.vertexWidth=tileCountX()+1;
        fieldDelta.vertexHeight=tileCountY()+1;
        copyVirtualMaterialFieldState(
            layer,&fieldDelta.beforeCurrent,&fieldDelta.beforeBaseline);
        staged.copyVirtualMaterialFieldState(
            layer,&fieldDelta.afterCurrent,&fieldDelta.afterBaseline);
        stagedCommand.virtualVertices.clear();
        stagedCommand.virtualFields.append(fieldDelta);
        return stagedCommand;
    }
    QVector<qint32> desired=active;
    const quint32 paintSeed=terrainPaintLayerSeed(distributionSeed,layer);
    QSet<int> affected;
    for(const QPoint& point:requestedVertices)
    {
        const int x=point.x(),y=point.y();
        if(x<0||x>tileCountX()||y<0||y>tileCountY())continue;
        bool incidentAvailable=layer==ETerrainTileLayer::Land;
        if(layer==ETerrainTileLayer::Overlay)
            for(int cellY:{y-1,y})for(int cellX:{x-1,x})
            {
                bool available=false;
                if(cellX>=0&&cellX<tileCountX()&&cellY>=0&&cellY<tileCountY())
                    packedTile(cellX,cellY,layer,&available);
                incidentAvailable|=available;
            }
        if(!incidentAvailable)continue;
        const int key=y*vertexWidth+x;
        for(int cellY:{y-1,y})for(int cellX:{x-1,x})
            if(cellX>=0&&cellX<tileCountX()&&cellY>=0&&cellY<tileCountY())
                affected.insert(cellY*tileCountX()+cellX);
        if(desired.at(key)==materialId)continue;
        STerrainTileCommand::SVirtualVertexDelta delta;
        delta.globalX=x;delta.globalY=y;delta.layer=layer;
        delta.before=desired.at(key);delta.after=materialId;
        command.virtualVertices.append(delta);desired[key]=materialId;
        command.semanticAudit.paintedVertices.append(QPoint(x,y));
    }
    if(affected.isEmpty())return command;
    STerrainTransitionSignature pure;
    pure.northWest=pure.northEast=pure.southEast=pure.southWest=materialId;
    bool pureFound=false;
    catalog.selectForPaint(pure,0,0,paintSeed,&pureFound);
    QVector<int> cells=affected.values().toVector();std::sort(cells.begin(),cells.end());
    for(int cell:cells)
    {
        const int x=cell%tileCountX(),y=cell/tileCountX();
        bool available=false;const quint16 before=packedTile(x,y,layer,&available);
        if(!available)continue;
        const int nw=y*vertexWidth+x,ne=nw+1,sw=(y+1)*vertexWidth+x,se=sw+1;
        if(desired.at(nw)==unset||desired.at(ne)==unset
           ||desired.at(se)==unset||desired.at(sw)==unset)
        {
            if(report)report->add("terrain.virtual_paint_unknown",EValidationSeverity::Error,
                "The current atlas profile cannot recover a complete vertex-material baseline",
                QString("%1,%2").arg(x).arg(y));
            command.deltas.clear();command.virtualVertices.clear();return command;
        }
        STerrainTransitionSignature exact;
        exact.northWest=desired.at(nw);exact.northEast=desired.at(ne);
        exact.southEast=desired.at(se);exact.southWest=desired.at(sw);
        STerrainTransitionSignature current;
        current.northWest=active.at(nw);current.northEast=active.at(ne);
        current.southEast=active.at(se);current.southWest=active.at(sw);
        if(!pureFound)
        {
            if(report)report->add("terrain.virtual_paint_preview",EValidationSeverity::Error,
                "This material has no uniform authored tile for an honest unblended preview");
            command.deltas.clear();command.virtualVertices.clear();return command;
        }
        bool selected = false;
        const bool exactUniform = exact.northWest == exact.northEast
            && exact.northWest == exact.southEast
            && exact.northWest == exact.southWest;
        // A cell whose semantic tuple did not change must retain its exact
        // packed blend while another part of the same overlapping stamp uses
        // a temporary uniform preview. Release resolution intentionally visits
        // only cells incident to changed vertices, so changing this collateral
        // cell here would otherwise leave a permanent uniform hole at an edge.
        // A wholly semantic-no-op stroke remains a deliberate variant reroll.
        if(!command.virtualVertices.isEmpty() && exact == current
           && !previewCoreCells.contains(cell))
        {
            command.semanticAudit.affectedCells.append(QPoint(x,y));
            continue;
        }
        // Tile targeting owns complete cells, but changing their four corner
        // vertices also makes the surrounding blend ring semantically affected.
        // Keep that ring at its exact current artwork during the drag preview;
        // release resolution will replace it with exact blends. Painting the
        // uniform preview into every incident cell made the visible footprint
        // one cell larger than the tile outline and the committed core.
        if(!command.virtualVertices.isEmpty()&&!previewCoreCells.isEmpty()
           &&!previewCoreCells.contains(cell))
        {
            command.semanticAudit.affectedCells.append(QPoint(x,y));
            continue;
        }
        const quint16 after = command.virtualVertices.isEmpty()
            ? (exactUniform
                ? selectUniformInteriorTile(catalog, exact, x, y, paintSeed,
                    randomizeUniformRotation, &selected)
                : catalog.selectForPaint(exact, x, y, paintSeed, &selected))
            : selectUniformInteriorTile(catalog, pure, x, y, paintSeed,
                randomizeUniformRotation, &selected);
        if(!selected)
        {
            if(report)report->add("terrain.virtual_paint_preview",
                EValidationSeverity::Error,
                "This material has no structurally uniform authored tile for an honest unblended preview");
            command.deltas.clear();command.virtualVertices.clear();return command;
        }
        command.semanticAudit.affectedCells.append(QPoint(x,y));
        if(after==before)continue;
        STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=layer;
        delta.before=before;delta.after=after;delta.hasSemanticRequest=true;
        delta.requestedMaterialId=materialId;delta.requestedSignature=exact;
        command.deltas.append(delta);
    }
    if(report)report->add("terrain.virtual_paint_resolve_pending",
        EValidationSeverity::Info,
        "Touched vertices are mandatory; authored exact transition resolution is deferred until stroke release");
    return command;
}

STerrainTileCommand CTerrainEditModel::makeVirtualStrokeResolution(
    const STerrainTileCommand& stroke,const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed,CValidationReport* report,
    bool randomizeUniformRotation,
    qint32 preferredRouteOwnerMaterial) const
{
    STerrainTileCommand command;
    command.description=QStringLiteral("Resolve virtual terrain stroke");
    command.semanticAudit.valid=true;
    command.semanticAudit.requestedMaterialId=stroke.semanticAudit.requestedMaterialId;
    command.semanticAudit.brush=stroke.semanticAudit.brush;
    command.semanticAudit.layer=stroke.semanticAudit.layer;
    command.semanticAudit.solverMode=QStringLiteral("virtual-release-exact");
    QVector<QPoint> changedVertices=stroke.semanticAudit.paintedVertices;
    for(const auto& vertex:stroke.virtualVertices)
        changedVertices.append(QPoint(vertex.globalX,vertex.globalY));
    if(changedVertices.isEmpty())return command;
    const ETerrainTileLayer layer=!stroke.virtualVertices.isEmpty()
        ?stroke.virtualVertices.front().layer
        :!stroke.virtualFields.isEmpty()
            ?stroke.virtualFields.front().layer:stroke.semanticAudit.layer;
    const quint32 paintSeed=terrainPaintLayerSeed(distributionSeed,layer);
    const QVector<qint32>& values=layer==ETerrainTileLayer::Land
        ?m_virtualLandMaterials:m_virtualOverlayMaterials;
    const int vertexWidth=tileCountX()+1;
    if(values.size()!=vertexWidth*(tileCountY()+1))return command;
    QSet<int> affected;
    for(const auto& vertex:stroke.virtualVertices)
    {
        if(vertex.layer!=layer)
        {
            if(report)report->add("terrain.virtual_stroke_layers",EValidationSeverity::Error,
                "A virtual material stroke cannot mix land and overlay vertices");
            return STerrainTileCommand();
        }
    }
    for(const QPoint& vertex:changedVertices)
        for(int y:{vertex.y()-1,vertex.y()})
            for(int x:{vertex.x()-1,vertex.x()})
                if(x>=0&&x<tileCountX()&&y>=0&&y<tileCountY())
                    affected.insert(y*tileCountX()+x);
    // A tile-target stamp owns every displayed core cell. Some of those cells
    // can already have the requested semantic corners after deterministic
    // recovery while still retaining stale packed artwork. They therefore
    // produce no changed vertex and used to be skipped by release resolution,
    // leaving holes in the committed square until a later map-wide Resolve.
    // Resolve the core cells directly as well as the blend ring incident to
    // genuinely changed vertices.
    for(const QPoint& cell:stroke.semanticAudit.paintedCoreCells)
        if(cell.x()>=0&&cell.x()<tileCountX()
           &&cell.y()>=0&&cell.y()<tileCountY())
            affected.insert(cell.y()*tileCountX()+cell.x());
    QVector<int> cells=affected.values().toVector();std::sort(cells.begin(),cells.end());
    int unresolved=0;
    for(int cell:cells)
    {
        const int x=cell%tileCountX(),y=cell/tileCountX();
        bool available=false;const quint16 before=packedTile(x,y,layer,&available);
        if(!available)continue;
        const int nw=y*vertexWidth+x,ne=nw+1,sw=(y+1)*vertexWidth+x,se=sw+1;
        STerrainTransitionSignature exact;
        exact.northWest=values.at(nw);exact.northEast=values.at(ne);
        exact.southEast=values.at(se);exact.southWest=values.at(sw);
        bool found=false;
        const bool uniform = exact.northWest == exact.northEast
            && exact.northWest == exact.southEast
            && exact.northWest == exact.southWest;
        const quint16 after = uniform
            ? selectUniformInteriorTile(catalog, exact, x, y,
                paintSeed, randomizeUniformRotation, &found)
            : catalog.selectForPaint(exact, x, y, paintSeed,
                preferredRouteOwnerMaterial, &found);
        if(!found){++unresolved;continue;}
        command.semanticAudit.affectedCells.append(QPoint(x,y));
        if(after==before)continue;
        STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=layer;
        delta.before=before;delta.after=after;delta.hasSemanticRequest=true;
        delta.requestedMaterialId=-1;delta.requestedSignature=exact;
        command.deltas.append(delta);
    }
    if(unresolved>0&&report)report->add("terrain.virtual_release_unresolved",
        EValidationSeverity::Error,
        QString("%1 exact transition tuple(s) were not realized in the live working atlas before release; the stroke must be refused without substituting identities").arg(unresolved));
    return command;
}

bool CTerrainEditModel::inferCellSignatureFromNeighbors(
    int cellX,int cellY,ETerrainTileLayer layer,
    const CTerrainTransitionCatalog& catalog,
    STerrainTransitionSignature* output,
    const QMap<int, STerrainTransitionSignature>* plannedSignatures) const
{
    if(!output||cellX<0||cellX>=tileCountX()
       ||cellY<0||cellY>=tileCountY())
        return false;
    qint32 inferred[4]={0,0,0,0};
    const int vertexX[4]={cellX,cellX+1,cellX+1,cellX};
    const int vertexY[4]={cellY,cellY,cellY+1,cellY+1};
    for(int targetCorner=0;targetCorner<4;++targetCorner)
    {
        QSet<qint32> observed;
        const int vx=vertexX[targetCorner],vy=vertexY[targetCorner];
        for(int otherY=vy-1;otherY<=vy;++otherY)
            for(int otherX=vx-1;otherX<=vx;++otherX)
            {
                if(otherX<0||otherX>=tileCountX()
                   ||otherY<0||otherY>=tileCountY()
                   ||(otherX==cellX&&otherY==cellY))
                    continue;
                bool available = true;
                bool decoded = false;
                STerrainTransitionSignature signature;
                const int otherKey = otherY * tileCountX() + otherX;
                if(plannedSignatures && plannedSignatures->contains(otherKey))
                {
                    signature = plannedSignatures->value(otherKey);
                    decoded = true;
                }
                else
                {
                    const quint16 packed = packedTile(
                        otherX, otherY, layer, &available);
                    if(available)
                        signature = catalog.gridSignatureForPacked(
                            packed, &decoded);
                }
                if(!available)
                    continue;
                if(!decoded)continue;
                if(otherX==vx&&otherY==vy)
                    observed.insert(signature.northWest);
                else if(otherX==vx-1&&otherY==vy)
                    observed.insert(signature.northEast);
                else if(otherX==vx-1&&otherY==vy-1)
                    observed.insert(signature.southEast);
                else if(otherX==vx&&otherY==vy-1)
                    observed.insert(signature.southWest);
            }
        if(observed.size()!=1)
            return false;
        inferred[targetCorner]=*observed.constBegin();
    }
    output->northWest=inferred[0];output->northEast=inferred[1];
    output->southEast=inferred[2];output->southWest=inferred[3];
    return true;
}

bool CTerrainEditModel::missingBlendRepairSignature(
    int cellX,int cellY,ETerrainTileLayer layer,
    const CTerrainTransitionCatalog& catalog,
    const CTerrainVirtualMaterialField* repairField,
    bool repairDecodedCells,STerrainTransitionSignature* output,
    const QMap<int, STerrainTransitionSignature>* plannedSignatures) const
{
    if(!output||cellX<0||cellX>=tileCountX()
       ||cellY<0||cellY>=tileCountY())
        return false;
    bool available = true;
    bool decoded = false;
    STerrainTransitionSignature current;
    const int cellKey = cellY * tileCountX() + cellX;
    if(plannedSignatures && plannedSignatures->contains(cellKey))
    {
        current = plannedSignatures->value(cellKey);
        decoded = true;
    }
    else
    {
        const quint16 packed = packedTile(cellX, cellY, layer, &available);
        if(available)
            current = catalog.gridSignatureForPacked(packed, &decoded);
    }
    if(!available)
        return false;

    // The model-owned field is an explicit history of Modern and Classic
    // semantic edits. A supplied repair field and neighbouring packed cells
    // are reconstruction evidence only, so neither may overwrite it.
    STerrainTransitionSignature authoritativeSignature;
    bool authoritativeFound = false;
    const QVector<qint32>& authoritativeValues =
        layer == ETerrainTileLayer::Land
            ? m_virtualLandMaterials : m_virtualOverlayMaterials;
    const int vertexWidth = tileCountX() + 1;
    const qint32 unset = std::numeric_limits<qint32>::min();
    if(authoritativeValues.size() == vertexWidth * (tileCountY() + 1))
    {
        const int northWest = cellY * vertexWidth + cellX;
        const int northEast = northWest + 1;
        const int southWest = (cellY + 1) * vertexWidth + cellX;
        const int southEast = southWest + 1;
        authoritativeFound = authoritativeValues.at(northWest) != unset
            && authoritativeValues.at(northEast) != unset
            && authoritativeValues.at(southEast) != unset
            && authoritativeValues.at(southWest) != unset;
        if(authoritativeFound)
        {
            authoritativeSignature.northWest = authoritativeValues.at(northWest);
            authoritativeSignature.northEast = authoritativeValues.at(northEast);
            authoritativeSignature.southEast = authoritativeValues.at(southEast);
            authoritativeSignature.southWest = authoritativeValues.at(southWest);
        }
    }
    if(authoritativeFound)
    {
        if(decoded && current == authoritativeSignature)
            return false;
        *output = authoritativeSignature;
        return true;
    }

    STerrainTransitionSignature recoveredSignature;
    bool recoveredFound = false;
    if(repairField)
        recoveredSignature = repairField->cellSignature(
            cellX, cellY, &recoveredFound);
    STerrainTransitionSignature neighborSignature;
    const bool neighborFound=inferCellSignatureFromNeighbors(
        cellX,cellY,layer,catalog,&neighborSignature,plannedSignatures);
    if(!decoded)
    {
        if(neighborFound)*output=neighborSignature;
        else if(recoveredFound)*output=recoveredSignature;
        else return false;
        return true;
    }
    const auto uniform=[](const STerrainTransitionSignature& signature)
    {
        return signature.northWest==signature.northEast
            &&signature.northWest==signature.southEast
            &&signature.northWest==signature.southWest;
    };
    // A recovered unanimous shared-corner tuple is stable map-wide evidence.
    // It applies equally to every decoded tile, including contextual stamps,
    // Manual placements, authored blends, and uniforms.
    if(repairDecodedCells&&recoveredFound)
    {
        if(current==recoveredSignature)
            return false;
        *output=recoveredSignature;
        return true;
    }
    if(!neighborFound||current==neighborSignature)
        return false;
    // With an ambiguous recovered vertex, do not let one locally unanimous
    // uniform tuple move a disputed material boundary. This applies to every
    // physical tile identically and carries no placement-origin privilege.
    if(uniform(neighborSignature))
        return false;
    *output=neighborSignature;
    return true;
}

STerrainTileCommand CTerrainEditModel::makeVirtualMapResolution(
    ETerrainTileLayer layer,const CTerrainTransitionCatalog& catalog,
    quint32 distributionSeed,CValidationReport* report,
    bool randomizeUniformRotation,
    const CTerrainVirtualMaterialField* repairField,
    bool allowPartialMaterialField,
    bool repairDecodedCells,
    const QMap<int, qint32>* preferredRouteOwners,
    const QSet<int>* forceExactCells,
    const QSet<int>* preserveCells) const
{
    STerrainTileCommand command;
    command.description=QStringLiteral("Resolve missing terrain blends");
    command.semanticAudit.valid=true;
    command.semanticAudit.layer=layer;
    command.semanticAudit.requestedMaterialId=-1;
    command.semanticAudit.solverMode=QStringLiteral("virtual-map-exact");
    const QVector<qint32>& values=layer==ETerrainTileLayer::Land
        ?m_virtualLandMaterials:m_virtualOverlayMaterials;
    if(repairField
       && (repairField->tileWidth()!=tileCountX()
           ||repairField->tileHeight()!=tileCountY()))
    {
        if(report)report->add("terrain.virtual_map_field",
            EValidationSeverity::Error,
            "The conservative repair field dimensions do not match this map");
        return command;
    }
    if(!repairField&&values.size()!=(tileCountX()+1)*(tileCountY()+1))
    {
        if(report)report->add("terrain.virtual_map_field",
            EValidationSeverity::Error,
            "The map has no complete shared-corner material field to resolve");
        return command;
    }
    const quint32 paintSeed=terrainPaintLayerSeed(distributionSeed,layer);
    const auto uniformSignature=[](const STerrainTransitionSignature& signature)
    {
        return signature.northWest==signature.northEast
            &&signature.northWest==signature.southEast
            &&signature.northWest==signature.southWest;
    };
    const auto selectExact = [&](const STerrainTransitionSignature& exact,
                                  int x, int y,
                                  int cellKey,
                                  qint32 requiredRuntimeType,
                                  bool* found) {
        QSet<int> allowedPhysicalSlots;
        const QSet<int>* allowedSlots = nullptr;
        if(requiredRuntimeType >= 0 && requiredRuntimeType < 16)
        {
            for(const STerrainTransitionVariant& variant :
                catalog.paintVariants(exact))
            {
                bool typeFound = false;
                const qint32 candidateType = tileType(
                    variant.packedTile, &typeFound);
                if(!typeFound || candidateType != requiredRuntimeType)
                    continue;
                allowedPhysicalSlots.insert(SMprPackedTile::decode(
                    variant.packedTile).atlasSlot());
            }
            allowedSlots = &allowedPhysicalSlots;
        }
        // Uniform-only rotation normalization is deliberately confined to a
        // uniform tuple. A non-uniform exact repair must never fall back to a
        // pure interior tile merely because its required runtime type is absent.
        if(uniformSignature(exact))
            return selectUniformInteriorTile(catalog, exact, x, y, paintSeed,
                randomizeUniformRotation, found, allowedSlots);
        const qint32 preferredOwner = preferredRouteOwners
            ? preferredRouteOwners->value(cellKey, 0) : 0;
        return allowedSlots
            ? catalog.selectForPaint(exact, x, y, paintSeed, *allowedSlots,
                preferredOwner, found)
            : catalog.selectForPaint(exact, x, y, paintSeed,
                preferredOwner, found);
    };
    const auto refuseRuntimeTypeSubstitution = [report](
        int x, int y, const STerrainTransitionSignature& exact,
        qint32 requiredRuntimeType) {
        if(report)
            report->add("terrain.virtual_map_runtime_type",
                EValidationSeverity::Error,
                QString("Resolve Missing Blends could not select the exact %1/%2/%3/%4 tuple at %5,%6 with the predecessor runtime terrain type %7; no differently typed slot was substituted.")
                    .arg(exact.northWest).arg(exact.northEast)
                    .arg(exact.southEast).arg(exact.southWest)
                    .arg(x).arg(y).arg(requiredRuntimeType));
    };

    // Neighbour recovery used to emit a first physical batch and only learn
    // from it on the next Resolve Missing Blends invocation. Keep the planned
    // semantic signatures separate from sector payloads and iterate them to a
    // fixed point before producing one reversible command. This never lets a
    // transient packed choice replace the model-owned virtual field.
    QMap<int, STerrainTransitionSignature> plannedSignatures;
    QMap<int, bool> pendingCells;
    QSet<int> unresolvedCells;
    QSet<int> runtimeTypeSkippedCells;
    for(int y = 0; y < tileCountY(); ++y)
        for(int x = 0; x < tileCountX(); ++x)
        {
            bool available = false;
            packedTile(x, y, layer, &available);
            if(available)
                pendingCells.insert(y * tileCountX() + x, true);
        }
    const auto signatureStateKey = [](const STerrainTransitionSignature& value) {
        return QByteArray::number(value.northWest) + ','
            + QByteArray::number(value.northEast) + ','
            + QByteArray::number(value.southEast) + ','
            + QByteArray::number(value.southWest);
    };
    QMap<int, QSet<QByteArray>> observedStates;
    int stateChanges = 0;
    const qint64 mapCellCount = qint64(tileCountX()) * tileCountY();
    const int maximumStateChanges = int(qMax<qint64>(1, qMin<qint64>(
        1000000, mapCellCount * 8)));
    while(!pendingCells.isEmpty())
    {
        const int key = pendingCells.constBegin().key();
        pendingCells.remove(key);
        const int x = key % tileCountX();
        const int y = key / tileCountX();
        if(preserveCells && preserveCells->contains(key))
            continue;
        bool available = false;
        const quint16 before = packedTile(x, y, layer, &available);
        if(!available)
            continue;
        STerrainTransitionSignature exact;
        if(!missingBlendRepairSignature(
               x, y, layer, catalog, repairField,
               repairDecodedCells, &exact, &plannedSignatures))
            continue;
        bool predecessorTypeFound = false;
        const qint32 predecessorType = tileType(before, &predecessorTypeFound);
        bool found = false;
        selectExact(exact, x, y, key,
                    predecessorTypeFound ? predecessorType : -1, &found);
        if(!found)
        {
            if(predecessorTypeFound)
            {
                if(allowPartialMaterialField)
                {
                    unresolvedCells.insert(key);
                    runtimeTypeSkippedCells.insert(key);
                    continue;
                }
                refuseRuntimeTypeSubstitution(x, y, exact, predecessorType);
                return STerrainTileCommand();
            }
            continue;
        }

        bool currentDecoded = false;
        STerrainTransitionSignature current;
        if(plannedSignatures.contains(key))
        {
            current = plannedSignatures.value(key);
            currentDecoded = true;
        }
        else
        {
            const quint16 packed = packedTile(x, y, layer, &available);
            if(available)
                current = catalog.gridSignatureForPacked(
                    packed, &currentDecoded);
        }
        if(currentDecoded && current == exact)
            continue;

        QSet<QByteArray>& states = observedStates[key];
        if(states.isEmpty() && currentDecoded)
            states.insert(signatureStateKey(current));
        const QByteArray nextState = signatureStateKey(exact);
        if(states.contains(nextState) || ++stateChanges > maximumStateChanges)
        {
            if(report)
                report->add("terrain.virtual_map_nonconvergent",
                    EValidationSeverity::Error,
                    "Resolve Missing Blends detected a cyclic or excessive semantic repair frontier without mutating the map");
            return command;
        }
        states.insert(nextState);

        bool originalDecoded = false;
        const quint16 originalPacked = packedTile(x, y, layer, &available);
        STerrainTransitionSignature original;
        if(available)
            original = catalog.gridSignatureForPacked(
                originalPacked, &originalDecoded);
        if(originalDecoded && original == exact)
            plannedSignatures.remove(key);
        else
            plannedSignatures.insert(key, exact);

        // A changed cell can influence only the three-by-three cell block
        // sharing one of its four vertices. Keep the frontier ordered by key
        // so equivalent maps produce the same command independent of hash
        // iteration order.
        for(int neighborY = y - 1; neighborY <= y + 1; ++neighborY)
            for(int neighborX = x - 1; neighborX <= x + 1; ++neighborX)
                if(neighborX >= 0 && neighborX < tileCountX()
                   && neighborY >= 0 && neighborY < tileCountY())
                    pendingCells.insert(
                        neighborY * tileCountX() + neighborX, true);
    }

    // Route-only normalization keeps the authoritative semantic tuple fixed
    // but re-runs exact physical selection through the component owner gate.
    // It is deliberately added after semantic frontier convergence so an
    // artwork-route correction cannot influence shared-corner recovery.
    if(forceExactCells)
        for(int key : *forceExactCells)
        {
            if(key < 0 || key >= tileCountX() * tileCountY()
               || (preserveCells && preserveCells->contains(key)))
                continue;
            const int x = key % tileCountX();
            const int y = key / tileCountX();
            bool found = false;
            STerrainTransitionSignature exact;
            if(repairField)
                exact = repairField->cellSignature(x, y, &found);
            if(!found && values.size()
                    == (tileCountX() + 1) * (tileCountY() + 1))
            {
                const int northWest = y * (tileCountX() + 1) + x;
                exact.northWest = values.at(northWest);
                exact.northEast = values.at(northWest + 1);
                exact.southWest = values.at(
                    northWest + tileCountX() + 1);
                exact.southEast = values.at(
                    northWest + tileCountX() + 2);
                found = exact.northWest > 0 && exact.northEast > 0
                    && exact.southEast > 0 && exact.southWest > 0;
            }
            if(found)
                plannedSignatures.insert(key, exact);
        }

    for(int y = 0; y < tileCountY(); ++y)
        for(int x = 0; x < tileCountX(); ++x)
        {
            bool available = false;
            const quint16 before = packedTile(x, y, layer, &available);
            if(!available)
                continue;
            const int key = y * tileCountX() + x;
            if(preserveCells && preserveCells->contains(key))
                continue;
            STerrainTransitionSignature exact;
            if(!missingBlendRepairSignature(
                   x, y, layer, catalog, repairField,
                   repairDecodedCells, &exact, &plannedSignatures))
                continue;
            bool predecessorTypeFound = false;
            const qint32 predecessorType = tileType(
                before, &predecessorTypeFound);
            bool found = false;
            selectExact(exact, x, y, key,
                        predecessorTypeFound ? predecessorType : -1, &found);
            if(!found)
            {
                if(predecessorTypeFound)
                {
                    if(allowPartialMaterialField)
                    {
                        unresolvedCells.insert(key);
                        runtimeTypeSkippedCells.insert(key);
                        continue;
                    }
                    refuseRuntimeTypeSubstitution(x, y, exact,
                                                  predecessorType);
                    return STerrainTileCommand();
                }
                unresolvedCells.insert(key);
            }
        }

    for(auto entry = plannedSignatures.constBegin();
        entry != plannedSignatures.constEnd(); ++entry)
    {
        const int key = entry.key();
        const int x = key % tileCountX();
        const int y = key / tileCountX();
        bool available = false;
        const quint16 before = packedTile(x, y, layer, &available);
        if(!available)
            continue;
        bool predecessorTypeFound = false;
        const qint32 predecessorType = tileType(before, &predecessorTypeFound);
        bool found = false;
        const quint16 after = selectExact(entry.value(), x, y, key,
            predecessorTypeFound ? predecessorType : -1, &found);
        if(!found)
        {
            if(predecessorTypeFound)
            {
                if(allowPartialMaterialField)
                {
                    unresolvedCells.insert(key);
                    runtimeTypeSkippedCells.insert(key);
                    continue;
                }
                refuseRuntimeTypeSubstitution(x, y, entry.value(),
                                              predecessorType);
                return STerrainTileCommand();
            }
            unresolvedCells.insert(key);
            continue;
        }
        command.semanticAudit.affectedCells.append(QPoint(x, y));
        if(after == before)
            continue;
        STerrainTileDelta delta;
        delta.globalX = x;
        delta.globalY = y;
        delta.layer = layer;
        delta.before = before;
        delta.after = after;
        delta.hasSemanticRequest = true;
        delta.requestedMaterialId = -1;
        delta.requestedSignature = entry.value();
        command.deltas.append(delta);
    }
    if(!runtimeTypeSkippedCells.isEmpty() && report)
        report->add("terrain.virtual_map_runtime_type_skipped",
            EValidationSeverity::Warning,
            QString("%1 map cell(s) were preserved because no exact blend exists with their predecessor runtime terrain type")
                .arg(runtimeTypeSkippedCells.size()));
    QSet<int> ambiguousCells = unresolvedCells;
    ambiguousCells.subtract(runtimeTypeSkippedCells);
    if(!ambiguousCells.isEmpty()&&report)report->add(
        allowPartialMaterialField
            ? "terrain.virtual_map_ambiguous_skipped"
            : "terrain.virtual_map_unresolved",
        allowPartialMaterialField
            ? EValidationSeverity::Warning : EValidationSeverity::Error,
        allowPartialMaterialField
            ? QString("%1 ambiguous map cell(s) were preserved rather than moving a tied material boundary")
                  .arg(ambiguousCells.size())
            : QString("%1 map cell(s) could not be resolved to an exact physical blend")
                  .arg(ambiguousCells.size()));
    return command;
}

bool CTerrainEditModel::virtualMaterialFieldActive(ETerrainTileLayer layer) const
{
    const QVector<qint32>& current=layer==ETerrainTileLayer::Land
        ?m_virtualLandMaterials:m_virtualOverlayMaterials;
    const QVector<qint32>& baseline=layer==ETerrainTileLayer::Land
        ?m_originalVirtualLandMaterials:m_originalVirtualOverlayMaterials;
    return !current.isEmpty()&&current!=baseline;
}

QMap<int, QPoint> CTerrainEditModel::referencedUnassignedAtlasSlots(
    ETerrainTileLayer layer, const CTerrainTransitionCatalog& catalog) const
{
    QMap<int, QPoint> result;
    for(int y = 0; y < tileCountY(); ++y)
        for(int x = 0; x < tileCountX(); ++x)
        {
            bool available = false;
            const quint16 packed = packedTile(x, y, layer, &available);
            if(!available)
                continue;
            bool decoded = false;
            catalog.gridSignatureForPacked(packed, &decoded);
            if(decoded)
                continue;
            const int slot = SMprPackedTile::decode(packed).atlasSlot();
            if(!result.contains(slot))
                result.insert(slot, QPoint(x, y));
        }
    return result;
}

qint32 CTerrainEditModel::virtualMaterialAt(ETerrainTileLayer layer,
    int vertexX,int vertexY,bool* found) const
{
    const QVector<qint32>& values=layer==ETerrainTileLayer::Land
        ?m_virtualLandMaterials:m_virtualOverlayMaterials;
    const int width=tileCountX()+1;
    const bool valid=vertexX>=0&&vertexX<=tileCountX()
        &&vertexY>=0&&vertexY<=tileCountY()
        &&values.size()==width*(tileCountY()+1)
        &&values.at(vertexY*width+vertexX)
            !=std::numeric_limits<qint32>::min();
    if(found)*found=valid;
    return valid?values.at(vertexY*width+vertexX):-1;
}

bool CTerrainEditModel::virtualMaterialCellChanged(
    ETerrainTileLayer layer, int cellX, int cellY) const
{
    if(cellX < 0 || cellX >= tileCountX()
       || cellY < 0 || cellY >= tileCountY())
        return true;
    const QVector<qint32>& current = layer == ETerrainTileLayer::Land
        ? m_virtualLandMaterials : m_virtualOverlayMaterials;
    const QVector<qint32>& baseline = layer == ETerrainTileLayer::Land
        ? m_originalVirtualLandMaterials : m_originalVirtualOverlayMaterials;
    const int vertexWidth = tileCountX() + 1;
    const int expected = vertexWidth * (tileCountY() + 1);
    if(current.size() != expected || baseline.size() != expected)
        return true;
    const int northWest = cellY * vertexWidth + cellX;
    const int northEast = northWest + 1;
    const int southWest = (cellY + 1) * vertexWidth + cellX;
    const int southEast = southWest + 1;
    return current.at(northWest) != baseline.at(northWest)
        || current.at(northEast) != baseline.at(northEast)
        || current.at(southEast) != baseline.at(southEast)
        || current.at(southWest) != baseline.at(southWest);
}

bool CTerrainEditModel::apply(const STerrainTileCommand& command, bool forward,
                              CValidationReport* report)
{
    QSet<int> commandFields;
    const int expectedFieldSize = (tileCountX() + 1) * (tileCountY() + 1);
    for(const STerrainTileCommand::SVirtualFieldDelta& delta
        : command.virtualFields)
    {
        const int layerKey = delta.layer == ETerrainTileLayer::Overlay ? 1 : 0;
        QVector<qint32>& current = delta.layer == ETerrainTileLayer::Land
            ? m_virtualLandMaterials : m_virtualOverlayMaterials;
        QVector<qint32>& baseline = delta.layer == ETerrainTileLayer::Land
            ? m_originalVirtualLandMaterials : m_originalVirtualOverlayMaterials;
        const QVector<qint32>& expectedCurrent = forward
            ? delta.beforeCurrent : delta.afterCurrent;
        const QVector<qint32>& expectedBaseline = forward
            ? delta.beforeBaseline : delta.afterBaseline;
        const bool dimensionsValid = delta.vertexWidth == tileCountX() + 1
            && delta.vertexHeight == tileCountY() + 1;
        const auto validFieldSize = [expectedFieldSize](const QVector<qint32>& value) {
            return value.isEmpty() || value.size() == expectedFieldSize;
        };
        if(commandFields.contains(layerKey) || !dimensionsValid
           || !validFieldSize(delta.beforeCurrent)
           || !validFieldSize(delta.afterCurrent)
           || !validFieldSize(delta.beforeBaseline)
           || !validFieldSize(delta.afterBaseline)
           || current != expectedCurrent || baseline != expectedBaseline)
        {
            if(report)
                report->add("terrain.virtual_field_provenance",
                    EValidationSeverity::Error,
                    "The virtual material command no longer matches its complete field state",
                    delta.layer == ETerrainTileLayer::Land
                        ? QStringLiteral("land") : QStringLiteral("overlay"));
            return false;
        }
        commandFields.insert(layerKey);
    }
    QSet<int> commandPendingVertices;
    const qint32 unset = std::numeric_limits<qint32>::min();
    for(const STerrainTileCommand::SPendingVirtualVertexDelta& delta
        : command.pendingVirtualVertices)
    {
        if(delta.globalX < 0 || delta.globalX > tileCountX()
           || delta.globalY < 0 || delta.globalY > tileCountY())
            return false;
        const int key = delta.globalY * (tileCountX() + 1) + delta.globalX;
        const int layerKey = (delta.layer == ETerrainTileLayer::Overlay ? 1 : 0)
            * (tileCountX() + 1) * (tileCountY() + 1) + key;
        const QVector<qint32>& values = delta.layer == ETerrainTileLayer::Land
            ? m_virtualLandMaterials : m_virtualOverlayMaterials;
        const QVector<qint32>& baseline = delta.layer == ETerrainTileLayer::Land
            ? m_originalVirtualLandMaterials : m_originalVirtualOverlayMaterials;
        const QMap<int, qint32>& pending = delta.layer == ETerrainTileLayer::Land
            ? m_pendingVirtualLandMaterials : m_pendingVirtualOverlayMaterials;
        const qint32 expected = forward ? delta.before : delta.after;
        if(commandPendingVertices.contains(layerKey)
           || !values.isEmpty() || !baseline.isEmpty()
           || pending.value(key, unset) != expected)
        {
            if(report)
                report->add("terrain.pending_virtual_paint_provenance",
                    EValidationSeverity::Error,
                    "The fieldless Manual corner command no longer matches its pending semantic state",
                    QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        commandPendingVertices.insert(layerKey);
    }
    QSet<int> commandVertices;
    for(const STerrainTileCommand::SVirtualVertexDelta& delta:command.virtualVertices)
    {
        if(delta.globalX<0||delta.globalX>tileCountX()
           ||delta.globalY<0||delta.globalY>tileCountY())
            return false;
        QVector<qint32>& values=delta.layer==ETerrainTileLayer::Land
            ?m_virtualLandMaterials:m_virtualOverlayMaterials;
        QVector<qint32>& baseline=delta.layer==ETerrainTileLayer::Land
            ?m_originalVirtualLandMaterials:m_originalVirtualOverlayMaterials;
        const int key=delta.globalY*(tileCountX()+1)+delta.globalX;
        const int layerKey=(delta.layer==ETerrainTileLayer::Overlay?1:0)
            *(tileCountX()+1)*(tileCountY()+1)+key;
        const qint32 expected=forward?delta.before:delta.after;
        const qint32 expectedBaseline=forward
            ?delta.beforeBaseline:delta.afterBaseline;
        const int fieldLayerKey = delta.layer == ETerrainTileLayer::Overlay ? 1 : 0;
        if(commandFields.contains(fieldLayerKey)
           || commandVertices.contains(layerKey)||key>=values.size()
           ||values.at(key)!=expected
           ||(delta.updatesBaseline
              &&(key>=baseline.size()
                 ||baseline.at(key)!=expectedBaseline)))
        {
            if(report)report->add("terrain.virtual_paint_provenance",
                EValidationSeverity::Error,
                "The virtual material command no longer matches its vertex field",
                QString("%1,%2").arg(delta.globalX).arg(delta.globalY));
            return false;
        }
        commandVertices.insert(layerKey);
    }
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
    for(const STerrainTileCommand::SVirtualVertexDelta& delta:command.virtualVertices)
    {
        QVector<qint32>& values=delta.layer==ETerrainTileLayer::Land
            ?m_virtualLandMaterials:m_virtualOverlayMaterials;
        QVector<qint32>& baseline=delta.layer==ETerrainTileLayer::Land
            ?m_originalVirtualLandMaterials:m_originalVirtualOverlayMaterials;
        const int key=delta.globalY*(tileCountX()+1)+delta.globalX;
        values[key]=forward?delta.after:delta.before;
        if(delta.updatesBaseline)
            baseline[key]=forward?delta.afterBaseline:delta.beforeBaseline;
    }
    for(const STerrainTileCommand::SVirtualFieldDelta& delta
        : command.virtualFields)
    {
        QVector<qint32>& current = delta.layer == ETerrainTileLayer::Land
            ? m_virtualLandMaterials : m_virtualOverlayMaterials;
        QVector<qint32>& baseline = delta.layer == ETerrainTileLayer::Land
            ? m_originalVirtualLandMaterials : m_originalVirtualOverlayMaterials;
        current = forward ? delta.afterCurrent : delta.beforeCurrent;
        baseline = forward ? delta.afterBaseline : delta.beforeBaseline;
    }
    for(const STerrainTileCommand::SPendingVirtualVertexDelta& delta
        : command.pendingVirtualVertices)
    {
        QMap<int, qint32>& pending = delta.layer == ETerrainTileLayer::Land
            ? m_pendingVirtualLandMaterials : m_pendingVirtualOverlayMaterials;
        const int key = delta.globalY * (tileCountX() + 1) + delta.globalX;
        const qint32 value = forward ? delta.after : delta.before;
        if(value == unset)
            pending.remove(key);
        else
            pending.insert(key, value);
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
                            QString("%1 expected=%2/%3 actual=%4/%5")
                                .arg(m_sectors.at(index).source.memberName)
                                .arg(expected.size())
                                .arg(QString::fromLatin1(QCryptographicHash::hash(
                                    expected, QCryptographicHash::Sha256).toHex()))
                                .arg(m_sectors.at(index).currentPayload.size())
                                .arg(QString::fromLatin1(QCryptographicHash::hash(
                                    m_sectors.at(index).currentPayload,
                                    QCryptographicHash::Sha256).toHex())));
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

    // The canonical lattice, not a detached SEC payload, owns land geometry.
    // Overlay activation may expand a type-0 SEC to type 3, but it must never
    // alter the original land vertex block. Validate the final replacement
    // payloads against canonical heights/offsets and the expected normal
    // provenance before allowing them into a staged archive transaction.
    // This is deliberately a save gate as well as an edit-time invariant: a
    // malformed overlay command must not become a structurally valid-looking
    // MPR with split sector seams.
    for(int index = 0; index < m_heights.size(); ++index)
    {
        const int globalX = index % m_vertexCountX;
        const int globalY = index / m_vertexCountX;
        for(const SVertexRef& ref : m_refs.at(index))
        {
            const SSector& sector = m_sectors.at(ref.sectorIndex);
            const QByteArray& replacement = replacements.at(ref.sectorIndex);
            const QByteArray& original = sector.source.payload;
            const quint16 expectedHeight = m_conflictedVertices.at(index)
                ? readHeight(original, ref.localX, ref.localY)
                : m_heights.at(index);
            const qint8 expectedXOffset = m_conflictedVertices.at(index)
                ? readOffset(original, ref.localX, ref.localY, 0)
                : m_xOffsets.at(index);
            const qint8 expectedYOffset = m_conflictedVertices.at(index)
                ? readOffset(original, ref.localX, ref.localY, 1)
                : m_yOffsets.at(index);
            if(readHeight(replacement, ref.localX, ref.localY) != expectedHeight
               || readOffset(replacement, ref.localX, ref.localY, 0) != expectedXOffset
               || readOffset(replacement, ref.localX, ref.localY, 1) != expectedYOffset)
            {
                if(report)
                    report->add("terrain.land_vertex_integrity",
                        EValidationSeverity::Error,
                        "A pending SEC replacement no longer matches the canonical land vertex lattice; saving is blocked",
                        QString("%1 vertex %2,%3")
                            .arg(sector.source.memberName).arg(globalX).arg(globalY));
                return false;
            }

            const quint32 expectedNormal = affectedNormals.contains(index)
                ? packedNormal(globalX, globalY)
                : readPackedNormal(original, ref.localX, ref.localY);
            if(readPackedNormal(replacement, ref.localX, ref.localY) != expectedNormal)
            {
                if(report)
                    report->add("terrain.land_normal_integrity",
                        EValidationSeverity::Error,
                        "A pending SEC replacement changed a land normal outside the canonical terrain transaction; saving is blocked",
                        QString("%1 vertex %2,%3")
                            .arg(sector.source.memberName).arg(globalX).arg(globalY));
                return false;
            }
        }
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

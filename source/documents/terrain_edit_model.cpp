#include "terrain_edit_model.h"

#include <QCryptographicHash>
#include <QSet>
#include <QtEndian>

#include <cmath>

#include "endurance_document.h"
#include "mpr_document.h"

namespace {
const int kSectorVertices = 33;
const int kSectorStride = 32;
const int kVertexSize = 8;
const int kLandVertexOffset = 5;
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

quint16 clampedHeight(float value)
{
    return quint16(qBound(0, int(std::lround(value)), 65535));
}
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
        source.payload = member.payload;
        sources.append(source);
    }
    return loadSources(int(document.mapInfo().sectorCountX),
                       int(document.mapInfo().sectorCountY), document.mapInfo().maxZ,
                       sources, report);
}

bool CTerrainEditModel::loadSources(int sectorCountX, int sectorCountY, float maxZ,
                                    const QVector<STerrainSectorSource>& sources,
                                    CValidationReport* report)
{
    m_sectorCountX = 0;
    m_sectorCountY = 0;
    m_vertexCountX = 0;
    m_vertexCountY = 0;
    m_maxZ = 0.0f;
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
            delta.after = clampedHeight(float(delta.before) + rawHeightDelta * weight);
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
                + (targets.at(index) - float(delta.before)) * weight);
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
                + (target - float(delta.before)) * weight);
            if(delta.before != delta.after)
                command.deltas.append(delta);
        }
    }
    return command;
}

bool CTerrainEditModel::apply(const STerrainHeightCommand& command, bool forward,
                              CValidationReport* report)
{
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

    for(const STerrainHeightDelta& delta : command.deltas)
    {
        const int index = globalIndex(delta.globalX, delta.globalY);
        const quint16 value = forward ? delta.after : delta.before;
        m_heights[index] = value;
        for(const SVertexRef& ref : m_refs.at(index))
        {
            SSector& sector = m_sectors[ref.sectorIndex];
            writeHeight(sector.currentPayload, ref.localX, ref.localY, value);
            sector.dirty = sector.currentPayload != sector.source.payload;
        }
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
    if(changedVertices.isEmpty())
        return true;

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
                    QString("Prepared %1 sector patch(es) for %2 changed height vertex/vertices and %3 affected normal(s)")
                        .arg(editedSectorCount).arg(changedVertices.size()).arg(affectedNormals.size()));
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

bool CTerrainEditHistory::execute(CTerrainEditModel* model,
                                  const STerrainHeightCommand& command,
                                  CValidationReport* report)
{
    if(!model || command.isEmpty() || !model->apply(command, true, report))
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

bool CTerrainEditHistory::redo(CTerrainEditModel* model, CValidationReport* report)
{
    if(!model || m_redo.isEmpty())
        return false;
    const STerrainHeightCommand command = m_redo.back();
    if(!model->apply(command, true, report))
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

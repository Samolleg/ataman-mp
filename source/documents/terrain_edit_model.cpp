#include "terrain_edit_model.h"

#include <QCryptographicHash>
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
                       int(document.mapInfo().sectorCountY), sources, report);
}

bool CTerrainEditModel::loadSources(int sectorCountX, int sectorCountY,
                                    const QVector<STerrainSectorSource>& sources,
                                    CValidationReport* report)
{
    m_sectorCountX = 0;
    m_sectorCountY = 0;
    m_vertexCountX = 0;
    m_vertexCountY = 0;
    m_heights.clear();
    m_refs.clear();
    m_conflictedVertices.clear();
    m_sectors.clear();
    m_sectorLookup.clear();
    if(sectorCountX <= 0 || sectorCountY <= 0)
    {
        if(report)
            report->add("terrain.dimensions", EValidationSeverity::Error,
                        "Terrain sector dimensions must be positive");
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
    m_vertexCountX = sectorCountX * kSectorStride + 1;
    m_vertexCountY = sectorCountY * kSectorStride + 1;
    const int globalCount = m_vertexCountX * m_vertexCountY;
    m_heights.fill(0, globalCount);
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
                    if(!initialized.at(index))
                    {
                        initialized[index] = true;
                        m_heights[index] = value;
                    }
                    else if(m_heights.at(index) != value)
                    {
                        m_conflictedVertices[index] = true;
                        if(report)
                            report->add("terrain.seam_conflict", EValidationSeverity::Warning,
                                QString("Shared vertex %1,%2 differs across sectors; editing it is disabled")
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
    if(isDirty())
    {
        if(report)
            report->add("terrain.normals_pending", EValidationSeverity::Error,
                        "Height serialization remains disabled until affected packed normals are recalculated");
        return false;
    }
    return true;
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

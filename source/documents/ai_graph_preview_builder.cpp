#include "ai_graph_preview_builder.h"

#include <QQueue>
#include <QtEndian>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <queue>

namespace {
constexpr int LayerCount = CAiGraphDocument::LayerCount;
constexpr int DirectionCount = CAiGraphDocument::DirectionCount;
constexpr float WalkSlopeLimitDegrees = 40.0f;
constexpr float FlySlopeLimitDegrees = 60.0f;
const float ProfileHeights[LayerCount] = {2.0f, 0.4f, 1.0f, 1.6f, 2.2f, 1.7f, 3.0f, 6.0f};

const QPoint Star3[] = {QPoint(0, 0), QPoint(1, 0), QPoint(-1, 0),
                        QPoint(0, 1), QPoint(0, -1)};
const QPoint Star5[] = {QPoint(-2, 0), QPoint(-1, -1), QPoint(-1, 0), QPoint(-1, 1),
                        QPoint(0, -2), QPoint(0, -1), QPoint(0, 0), QPoint(0, 1),
                        QPoint(0, 2), QPoint(1, -1), QPoint(1, 0), QPoint(1, 1),
                        QPoint(2, 0)};
const QPoint Star7[] = {
    QPoint(-3, 0), QPoint(-2, -2), QPoint(-2, -1), QPoint(-2, 0), QPoint(-2, 1),
    QPoint(-2, 2), QPoint(-1, -2), QPoint(-1, -1), QPoint(-1, 0), QPoint(-1, 1),
    QPoint(-1, 2), QPoint(0, -3), QPoint(0, -2), QPoint(0, -1), QPoint(0, 0),
    QPoint(0, 1), QPoint(0, 2), QPoint(0, 3), QPoint(1, -2), QPoint(1, -1),
    QPoint(1, 0), QPoint(1, 1), QPoint(1, 2), QPoint(2, -2), QPoint(2, -1),
    QPoint(2, 0), QPoint(2, 1), QPoint(2, 2), QPoint(3, 0)};

int tileIndex(int x, int y, int width) { return y * width + x; }

template <size_t N>
QVector<quint8> expandImpassable(const QVector<quint8>& source, int width, int height,
                                 const QPoint (&footprint)[N])
{
    QVector<quint8> result(source.size(), 0);
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            quint8 quality = 15;
            for(const QPoint& offset : footprint)
            {
                const int sx = x + offset.x();
                const int sy = y + offset.y();
                if(sx < 0 || sy < 0 || sx >= width || sy >= height)
                {
                    quality = 0;
                    break;
                }
                quality = qMin(quality, source.at(tileIndex(sx, sy, width)));
            }
            result[tileIndex(x, y, width)] = quality;
        }
    return result;
}

void assignComponents(SAiGraphLayer* layer, int width, int height)
{
    layer->components.fill(0, width * height);
    quint32 nextComponent = 1;
    QQueue<int> queue;
    for(int start = 0; start < width * height; ++start)
    {
        if(layer->components.at(start) != 0
           || quint8(layer->packedPositions.at(start)) == 0xff)
            continue;
        const quint16 component = quint16(qMin(nextComponent++, quint32(0xffff)));
        layer->components[start] = component;
        queue.enqueue(start);
        while(!queue.isEmpty())
        {
            const int cell = queue.dequeue();
            const int cx = cell % width;
            const int cy = cell / width;
            for(int direction = 0; direction < DirectionCount; ++direction)
            {
                if(layer->weights.at(cell * DirectionCount + direction) < 0)
                    continue;
                const QPoint delta = CAiGraphDocument::directionDelta(direction);
                const int nx = cx + delta.x();
                const int ny = cy + delta.y();
                if(nx < 0 || ny < 0 || nx >= width || ny >= height)
                    continue;
                const int neighbor = tileIndex(nx, ny, width);
                if(layer->components.at(neighbor) == 0
                   && quint8(layer->packedPositions.at(neighbor)) != 0xff)
                {
                    layer->components[neighbor] = component;
                    queue.enqueue(neighbor);
                }
            }
        }
    }
}

bool crossesGraphCell(const QVector<quint8>& quality, int aiWidth, int aiHeight,
                      int graphX, int graphY, int start)
{
    const int lowX = graphX * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int lowY = graphY * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int highX = qMin(aiWidth - 1, lowX + CAiGraphPreviewBuilder::AiTilesPerGraphCell);
    const int highY = qMin(aiHeight - 1, lowY + CAiGraphPreviewBuilder::AiTilesPerGraphCell);
    QVector<bool> visited((highX - lowX + 1) * (highY - lowY + 1), false);
    const auto localIndex = [=](int x, int y)
    { return (y - lowY) * (highX - lowX + 1) + x - lowX; };
    QQueue<int> queue;
    queue.enqueue(start);
    visited[localIndex(start % aiWidth, start / aiWidth)] = true;
    bool left = false, right = false, top = false, bottom = false;
    while(!queue.isEmpty())
    {
        const int current = queue.dequeue();
        const int x = current % aiWidth, y = current / aiWidth;
        left |= x == lowX; right |= x == highX; top |= y == lowY; bottom |= y == highY;
        const QPoint cardinal[] = {QPoint(-1,0), QPoint(1,0), QPoint(0,-1), QPoint(0,1)};
        for(const QPoint& delta : cardinal)
        {
            const int nx = x + delta.x(), ny = y + delta.y();
            if(nx < lowX || nx > highX || ny < lowY || ny > highY
               || !quality.at(tileIndex(nx, ny, aiWidth)))
                continue;
            const int local = localIndex(nx, ny);
            if(!visited.at(local))
            { visited[local] = true; queue.enqueue(tileIndex(nx, ny, aiWidth)); }
        }
    }
    return (left && right) || (top && bottom);
}

void makeConsistent(SAiGraphLayer* layer, int width, int height)
{
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
            for(int direction = 0; direction < DirectionCount; ++direction)
            {
                const QPoint delta = CAiGraphDocument::directionDelta(direction);
                const int nx = x + delta.x(), ny = y + delta.y();
                if(nx < 0 || ny < 0 || nx >= width || ny >= height)
                    continue;
                int opposite = -1;
                for(int candidate = 0; candidate < DirectionCount; ++candidate)
                    if(CAiGraphDocument::directionDelta(candidate) == -delta)
                    { opposite = candidate; break; }
                if(opposite < 0)
                    continue;
                qint16& forward = layer->weights[tileIndex(x, y, width) * DirectionCount + direction];
                qint16& reverse = layer->weights[tileIndex(nx, ny, width) * DirectionCount + opposite];
                if(forward < 0 || reverse < 0)
                    forward = reverse = -1;
            }
}

quint16 readU16(const QByteArray& data, int offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + offset));
}
quint32 readU32(const QByteArray& data, int offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + offset));
}
}

QVector<SAiTerrainTraversal> CAiGraphPreviewBuilder::parseTerrainTraversalRegistry(
    const QByteArray& registry, QString* error)
{
    if(error) error->clear();
    QVector<SAiTerrainTraversal> result(16);
    for(SAiTerrainTraversal& entry : result) { entry.speed = -1; entry.cost = 1; }
    if(registry.size() < 6 || readU32(registry, 0) != 0x45ab3efb)
    {
        if(error) *error = QStringLiteral("tileDesc.reg has an invalid registry header.");
        return {};
    }
    const int sectionCount = readU16(registry, 4);
    if(sectionCount <= 0 || 6 + sectionCount * 6 > registry.size())
    {
        if(error) *error = QStringLiteral("tileDesc.reg has a truncated section table.");
        return {};
    }
    for(int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
    {
        const int sectionOffset = int(readU32(registry, 6 + sectionIndex * 6 + 2));
        if(sectionOffset < 0 || sectionOffset + 4 > registry.size())
            continue;
        const int valueCount = readU16(registry, sectionOffset);
        const int nameLength = readU16(registry, sectionOffset + 2);
        if(sectionOffset + 4 + nameLength + valueCount * 6 > registry.size())
            continue;
        bool ok = false;
        const int terrain = QString::fromLatin1(registry.mid(sectionOffset + 4, nameLength)).toInt(&ok);
        if(!ok || terrain < 0 || terrain >= result.size())
            continue;
        const int tableOffset = sectionOffset + 4 + nameLength;
        for(int valueIndex = 0; valueIndex < valueCount; ++valueIndex)
        {
            const int relative = int(readU32(registry, tableOffset + valueIndex * 6 + 2));
            const int valueOffset = sectionOffset + relative;
            if(valueOffset < 0 || valueOffset + 3 > registry.size())
                continue;
            const quint8 type = quint8(registry.at(valueOffset));
            const int valueNameLength = readU16(registry, valueOffset + 1);
            const int dataOffset = valueOffset + 3 + valueNameLength;
            if(type != 0 || dataOffset + 4 > registry.size())
                continue;
            const QString name = QString::fromLatin1(
                registry.mid(valueOffset + 3, valueNameLength));
            const int value = qint32(readU32(registry, dataOffset));
            if(name.compare(QStringLiteral("Speed"), Qt::CaseInsensitive) == 0)
                result[terrain].speed = value;
            else if(name.compare(QStringLiteral("CostMul"), Qt::CaseInsensitive) == 0)
                result[terrain].cost = value;
        }
    }
    return result;
}

SAiGraphPreviewBuildResult CAiGraphPreviewBuilder::build(const SAiGraphPreviewBuildInput& input)
{
    SAiGraphPreviewBuildResult result;
    if(input.graphWidth <= 0 || input.graphHeight <= 0
       || input.graphWidth > 256 || input.graphHeight > 256)
    { result.error = QStringLiteral("Invalid graph dimensions."); return result; }
    const int aiWidth = input.graphWidth * AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * AiTilesPerGraphCell;
    const int aiCount = aiWidth * aiHeight;
    if(input.terrainHeights.size() != aiCount)
    { result.error = QStringLiteral("Terrain snapshot dimensions do not match the graph."); return result; }
    if(!input.waterDepths.isEmpty() && input.waterDepths.size() != aiCount)
    { result.error = QStringLiteral("Water snapshot dimensions do not match the graph."); return result; }
    if(!input.terrainTypes.isEmpty() && input.terrainTypes.size() != aiCount)
    { result.error = QStringLiteral("Terrain-class snapshot dimensions do not match the graph."); return result; }

    QVector<SAiTerrainTraversal> traversal = input.terrainTraversal;
    if(traversal.size() < 16)
    {
        traversal.resize(16);
        result.warnings.append(QStringLiteral(
            "Configured terrain traversal data was unavailable; unknown terrain classes use neutral diagnostic costs."));
    }
    if(input.collisionMetadataApproximate && !input.collisionProxies.isEmpty())
        result.warnings.append(QStringLiteral(
            "Object blocking uses conservative rendered bounds; authored hard/soft/easy/danger and bridge volumes are not yet available."));

    QVector<bool> walkSlopeBlocked(aiCount, false);
    QVector<bool> flySlopeBlocked(aiCount, false);
    const float walkStep = qTan(qDegreesToRadians(WalkSlopeLimitDegrees)) * AiTileWorldSize;
    const float flyStep = qTan(qDegreesToRadians(FlySlopeLimitDegrees)) * AiTileWorldSize;
    for(int y = 1; y < aiHeight - 1; ++y)
        for(int x = 1; x < aiWidth - 1; ++x)
        {
            const int index = tileIndex(x, y, aiWidth);
            const float ground = input.terrainHeights.at(index);
            float difference = 0.0f;
            const QPoint cardinal[] = {QPoint(-1,0), QPoint(1,0), QPoint(0,-1), QPoint(0,1)};
            for(const QPoint& delta : cardinal)
                difference = qMax(difference, qAbs(input.terrainHeights.at(
                    tileIndex(x + delta.x(), y + delta.y(), aiWidth)) - ground));
            walkSlopeBlocked[index] = difference > walkStep;
            flySlopeBlocked[index] = difference > flyStep;
        }

    QVector<SAiGraphLayer> layers(LayerCount);
    for(int layerIndex = 0; layerIndex < LayerCount; ++layerIndex)
    {
        QVector<quint8> rawQuality(aiCount, 15);
        for(int y = 0; y < aiHeight; ++y)
            for(int x = 0; x < aiWidth; ++x)
            {
                const int index = tileIndex(x, y, aiWidth);
                const int terrainType = input.terrainTypes.isEmpty() ? 0 : input.terrainTypes.at(index);
                const SAiTerrainTraversal policy = terrainType >= 0 && terrainType < traversal.size()
                    ? traversal.at(terrainType) : SAiTerrainTraversal();
                int quality = (policy.speed < 0 || policy.cost > 70) ? 0
                    : qBound(1, (policy.speed * 15 + 512) / 1024
                                   - qMax(0, policy.cost - 1) / 5, 15);
                const float depth = input.waterDepths.isEmpty() ? 0.0f
                    : qMax(0.0f, input.waterDepths.at(index));
                if(depth > 0.0001f)
                {
                    if(layerIndex == 1 || (layerIndex != 0
                        && depth >= ProfileHeights[layerIndex] - 0.01f))
                        quality = 0;
                    else if(layerIndex != 0)
                        quality = qMin(quality, depth > ProfileHeights[layerIndex] * 0.5f ? 6 : 10);
                    if(layerIndex == 3)
                        ++result.waterAiTiles;
                }
                const float ground = input.terrainHeights.at(index)
                    + (layerIndex == 0 ? depth : 0.0f);
                const float top = ground + ProfileHeights[layerIndex];
                const float worldX = (x + 0.5f) * AiTileWorldSize;
                const float worldY = (y + 0.5f) * AiTileWorldSize;
                for(const SAiGraphCollisionProxy& proxy : input.collisionProxies)
                    if(worldX + AiTileWorldSize * 0.5f >= proxy.minimum.x()
                       && worldX - AiTileWorldSize * 0.5f <= proxy.maximum.x()
                       && worldY + AiTileWorldSize * 0.5f >= proxy.minimum.y()
                       && worldY - AiTileWorldSize * 0.5f <= proxy.maximum.y()
                       && proxy.maximum.z() > ground + 0.05f && proxy.minimum.z() < top)
                    { quality = 0; break; }
                rawQuality[index] = quint8(quality);
            }

        QVector<quint8> quality;
        if(layerIndex <= 4) quality = expandImpassable(rawQuality, aiWidth, aiHeight, Star3);
        else if(layerIndex <= 6) quality = expandImpassable(rawQuality, aiWidth, aiHeight, Star5);
        else quality = expandImpassable(rawQuality, aiWidth, aiHeight, Star7);

        for(int y = 0; y < aiHeight; ++y)
            for(int x = 0; x < aiWidth; ++x)
            {
                const int index = tileIndex(x, y, aiWidth);
                bool slopeBlocked = layerIndex == 0
                    ? flySlopeBlocked.at(index) : walkSlopeBlocked.at(index);
                if(!slopeBlocked && layerIndex >= 5)
                {
                    if(layerIndex <= 6)
                    {
                        for(const QPoint& offset : Star3)
                        {
                            const int sx = x + offset.x(), sy = y + offset.y();
                            if(sx >= 0 && sy >= 0 && sx < aiWidth && sy < aiHeight
                               && walkSlopeBlocked.at(tileIndex(sx, sy, aiWidth)))
                            { slopeBlocked = true; break; }
                        }
                    }
                    else
                    {
                        for(const QPoint& offset : Star5)
                        {
                            const int sx = x + offset.x(), sy = y + offset.y();
                            if(sx >= 0 && sy >= 0 && sx < aiWidth && sy < aiHeight
                               && walkSlopeBlocked.at(tileIndex(sx, sy, aiWidth)))
                            { slopeBlocked = true; break; }
                        }
                    }
                }
                if(x < 3 || y < 3 || x >= aiWidth - 3 || y >= aiHeight - 3
                   || slopeBlocked)
                    quality[index] = 0;
            }

        SAiGraphLayer& layer = layers[layerIndex];
        layer.weights.fill(qint16(-1), input.graphWidth * input.graphHeight * DirectionCount);
        layer.packedPositions.fill(char(0xff), input.graphWidth * input.graphHeight);
        QVector<int> representative(input.graphWidth * input.graphHeight, -1);
        for(int gy = 0; gy < input.graphHeight; ++gy)
            for(int gx = 0; gx < input.graphWidth; ++gx)
            {
                int best = -1;
                int bestQuality = -1;
                int bestDistance = std::numeric_limits<int>::max();
                bool bestCrosses = false;
                for(int ly = 0; ly < AiTilesPerGraphCell; ++ly)
                    for(int lx = 0; lx < AiTilesPerGraphCell; ++lx)
                    {
                        const int candidate = tileIndex(gx * 8 + lx, gy * 8 + ly, aiWidth);
                        const int candidateQuality = quality.at(candidate);
                        const int distance = (lx * 2 - 7) * (lx * 2 - 7)
                                           + (ly * 2 - 7) * (ly * 2 - 7);
                        if(candidateQuality <= 0)
                            continue;
                        const bool crosses = crossesGraphCell(
                            quality, aiWidth, aiHeight, gx, gy, candidate);
                        if((crosses && !bestCrosses)
                           || (crosses == bestCrosses && (candidateQuality > bestQuality
                               || (candidateQuality == bestQuality && distance < bestDistance))))
                        {
                            best = candidate;
                            bestQuality = candidateQuality;
                            bestDistance = distance;
                            bestCrosses = crosses;
                        }
                    }
                if(bestQuality <= 0) best = -1;
                const int graphCell = tileIndex(gx, gy, input.graphWidth);
                representative[graphCell] = best;
                if(best >= 0)
                {
                    const int localX = best % aiWidth - gx * 8;
                    const int localY = best / aiWidth - gy * 8;
                    layer.packedPositions[graphCell] = char((localY << 4) | localX);
                }
            }

        using QueueEntry = QPair<int, int>;
        for(int gy = 0; gy < input.graphHeight; ++gy)
            for(int gx = 0; gx < input.graphWidth; ++gx)
            {
                const int graphCell = tileIndex(gx, gy, input.graphWidth);
                const int start = representative.at(graphCell);
                if(start < 0) continue;
                const int lowX = qMax(0, (gx - 1) * 8), lowY = qMax(0, (gy - 1) * 8);
                const int highX = qMin(aiWidth, (gx + 2) * 8), highY = qMin(aiHeight, (gy + 2) * 8);
                const int regionWidth = highX - lowX, regionHeight = highY - lowY;
                QVector<int> distance(regionWidth * regionHeight, std::numeric_limits<int>::max());
                const auto regionIndex = [=](int x, int y) { return (y-lowY)*regionWidth + x-lowX; };
                std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
                distance[regionIndex(start % aiWidth, start / aiWidth)] = 0;
                queue.push(qMakePair(0, start));
                while(!queue.empty())
                {
                    const QueueEntry entry = queue.top(); queue.pop();
                    const int currentDistance = entry.first, current = entry.second;
                    const int cx = current % aiWidth, cy = current / aiWidth;
                    if(currentDistance != distance.at(regionIndex(cx, cy))) continue;
                    for(int direction = 0; direction < DirectionCount; ++direction)
                    {
                        const QPoint delta = CAiGraphDocument::directionDelta(direction);
                        const int nx = cx + delta.x(), ny = cy + delta.y();
                        if(nx < lowX || nx >= highX || ny < lowY || ny >= highY) continue;
                        const int next = tileIndex(nx, ny, aiWidth);
                        if(quality.at(next) == 0) continue;
                        if(delta.x() && delta.y()
                           && (!quality.at(tileIndex(nx, cy, aiWidth))
                               || !quality.at(tileIndex(cx, ny, aiWidth)))) continue;
                        const float dz = input.terrainHeights.at(next) - input.terrainHeights.at(current);
                        const int terrainCost = qMax(1, 16 - int(quality.at(next)));
                        const int planar = delta.x() && delta.y() ? 181 : 128;
                        const int elevationCost = layerIndex == 0 ? 0
                            : int(qMax(0.0f, dz) * 96.0f + qAbs(dz) * 16.0f);
                        const int candidate = currentDistance + planar * terrainCost + elevationCost;
                        const int ri = regionIndex(nx, ny);
                        if(candidate < distance.at(ri))
                        { distance[ri] = candidate; queue.push(qMakePair(candidate, next)); }
                    }
                }
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    const QPoint delta = CAiGraphDocument::directionDelta(direction);
                    const int ngx = gx + delta.x(), ngy = gy + delta.y();
                    if(ngx < 0 || ngy < 0 || ngx >= input.graphWidth || ngy >= input.graphHeight) continue;
                    const int destination = representative.at(tileIndex(ngx, ngy, input.graphWidth));
                    if(destination < 0) continue;
                    const int dx = destination % aiWidth, dy = destination / aiWidth;
                    if(dx < lowX || dx >= highX || dy < lowY || dy >= highY) continue;
                    const int cost = distance.at(regionIndex(dx, dy));
                    if(cost != std::numeric_limits<int>::max())
                        layer.weights[graphCell * DirectionCount + direction] =
                            qint16(qMin(32767, qMax(1, cost / 128)));
                }
            }
        makeConsistent(&layer, input.graphWidth, input.graphHeight);
        assignComponents(&layer, input.graphWidth, input.graphHeight);
        if(layerIndex == 3)
            result.blockedAiTiles = int(std::count(quality.cbegin(), quality.cend(), quint8(0)));
    }

    QString error;
    if(!result.document.initializePreview(input.graphWidth, input.graphHeight, layers, &error))
    { result.error = error; return result; }
    result.collisionProxyCount = input.collisionProxies.size();
    result.valid = true;
    return result;
}

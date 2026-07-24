#include "ai_graph_preview_builder.h"

#include <QQueue>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace {
constexpr int LayerCount = CAiGraphDocument::LayerCount;
constexpr int DirectionCount = CAiGraphDocument::DirectionCount;
const float ProfileRadii[LayerCount] = {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.7f, 0.7f, 1.5f};

int tileIndex(int x, int y, int width)
{
    return y * width + x;
}

bool insideExpandedProxy(const SAiGraphCollisionProxy& proxy, float x, float y,
                         float ground, float profileHeight, float radius)
{
    if(x < proxy.minimum.x() - radius || x > proxy.maximum.x() + radius
       || y < proxy.minimum.y() - radius || y > proxy.maximum.y() + radius)
        return false;
    const float top = ground + profileHeight;
    return proxy.maximum.z() > ground + 0.05f && proxy.minimum.z() < top;
}

void assignComponents(SAiGraphLayer* layer, int width, int height)
{
    if(!layer)
        return;
    layer->components.fill(0, width * height);
    quint32 nextComponent = 1;
    QQueue<int> queue;
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int start = tileIndex(x, y, width);
            if(layer->components[start] != 0
               || quint8(layer->packedPositions.at(start)) == 0xff)
                continue;
            const quint16 component = quint16(qMin(nextComponent, quint32(0xffff)));
            if(nextComponent < 0xffff)
                ++nextComponent;
            layer->components[start] = component;
            queue.enqueue(start);
            while(!queue.isEmpty())
            {
                const int cell = queue.dequeue();
                const int cx = cell % width;
                const int cy = cell / width;
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    if(layer->weights[cell * DirectionCount + direction] < 0)
                        continue;
                    const QPoint delta = CAiGraphDocument::directionDelta(direction);
                    const int nx = cx + delta.x();
                    const int ny = cy + delta.y();
                    if(nx < 0 || nx >= width || ny < 0 || ny >= height)
                        continue;
                    const int neighbor = tileIndex(nx, ny, width);
                    if(layer->components[neighbor] == 0
                       && quint8(layer->packedPositions.at(neighbor)) != 0xff)
                    {
                        layer->components[neighbor] = component;
                        queue.enqueue(neighbor);
                    }
                }
            }
        }
}
}

SAiGraphPreviewBuildResult CAiGraphPreviewBuilder::build(
    const SAiGraphPreviewBuildInput& input)
{
    SAiGraphPreviewBuildResult result;
    if(input.graphWidth <= 0 || input.graphHeight <= 0
       || input.graphWidth > 256 || input.graphHeight > 256)
    {
        result.error = QStringLiteral("Invalid graph dimensions.");
        return result;
    }
    const int aiWidth = input.graphWidth * AiTilesPerGraphCell;
    const int aiHeight = input.graphHeight * AiTilesPerGraphCell;
    if(input.terrainHeights.size() != aiWidth * aiHeight)
    {
        result.error = QStringLiteral("Terrain snapshot dimensions do not match the graph.");
        return result;
    }

    QVector<QVector<int>> proxyCandidates(aiWidth * aiHeight);
    constexpr float MaximumProfileRadius = 1.5f;
    for(int proxyIndex = 0; proxyIndex < input.collisionProxies.size(); ++proxyIndex)
    {
        const SAiGraphCollisionProxy& proxy = input.collisionProxies.at(proxyIndex);
        const int lowX = qBound(0, int(qFloor((proxy.minimum.x() - MaximumProfileRadius)
                                              / AiTileWorldSize)), aiWidth - 1);
        const int lowY = qBound(0, int(qFloor((proxy.minimum.y() - MaximumProfileRadius)
                                              / AiTileWorldSize)), aiHeight - 1);
        const int highX = qBound(0, int(qFloor((proxy.maximum.x() + MaximumProfileRadius)
                                               / AiTileWorldSize)), aiWidth - 1);
        const int highY = qBound(0, int(qFloor((proxy.maximum.y() + MaximumProfileRadius)
                                               / AiTileWorldSize)), aiHeight - 1);
        if(highX < lowX || highY < lowY)
            continue;
        for(int y = lowY; y <= highY; ++y)
            for(int x = lowX; x <= highX; ++x)
                proxyCandidates[tileIndex(x, y, aiWidth)].append(proxyIndex);
    }

    QVector<SAiGraphLayer> layers(LayerCount);
    for(int layerIndex = 0; layerIndex < LayerCount; ++layerIndex)
    {
        const float profileHeight = float(CAiGraphDocument::canonicalHeight(layerIndex));
        const float radius = ProfileRadii[layerIndex];
        QVector<bool> passable(aiWidth * aiHeight, true);
        const float maximumStep = layerIndex == 0 ? 1.5f : 0.75f;
        for(int y = 0; y < aiHeight; ++y)
            for(int x = 0; x < aiWidth; ++x)
            {
                const int index = tileIndex(x, y, aiWidth);
                if(x < 3 || y < 3 || x >= aiWidth - 3 || y >= aiHeight - 3)
                {
                    passable[index] = false;
                    continue;
                }
                const float ground = input.terrainHeights[index];
                const int dx[4] = {-1, 1, 0, 0};
                const int dy[4] = {0, 0, -1, 1};
                for(int direction = 0; direction < 4 && passable[index]; ++direction)
                {
                    const float neighbor = input.terrainHeights[
                        tileIndex(x + dx[direction], y + dy[direction], aiWidth)];
                    if(qAbs(neighbor - ground) > maximumStep)
                        passable[index] = false;
                }
                const float worldX = (x + 0.5f) * AiTileWorldSize;
                const float worldY = (y + 0.5f) * AiTileWorldSize;
                for(int proxyIndex : proxyCandidates.at(index))
                    if(passable[index] && insideExpandedProxy(
                           input.collisionProxies.at(proxyIndex), worldX, worldY,
                           ground, profileHeight, radius))
                        passable[index] = false;
            }

        SAiGraphLayer& layer = layers[layerIndex];
        layer.weights.fill(qint16(-1), input.graphWidth * input.graphHeight * DirectionCount);
        layer.packedPositions.fill(char(0xff), input.graphWidth * input.graphHeight);
        QVector<int> representative(input.graphWidth * input.graphHeight, -1);
        for(int gy = 0; gy < input.graphHeight; ++gy)
            for(int gx = 0; gx < input.graphWidth; ++gx)
            {
                int best = -1;
                int bestDistance = std::numeric_limits<int>::max();
                for(int ly = 0; ly < AiTilesPerGraphCell; ++ly)
                    for(int lx = 0; lx < AiTilesPerGraphCell; ++lx)
                    {
                        const int x = gx * AiTilesPerGraphCell + lx;
                        const int y = gy * AiTilesPerGraphCell + ly;
                        const int candidate = tileIndex(x, y, aiWidth);
                        if(!passable[candidate])
                            continue;
                        const int distance = (lx * 2 - 7) * (lx * 2 - 7)
                                           + (ly * 2 - 7) * (ly * 2 - 7);
                        if(distance < bestDistance)
                        {
                            bestDistance = distance;
                            best = candidate;
                        }
                    }
                const int graphCell = tileIndex(gx, gy, input.graphWidth);
                representative[graphCell] = best;
                if(best >= 0)
                {
                    const int localX = (best % aiWidth) - gx * AiTilesPerGraphCell;
                    const int localY = (best / aiWidth) - gy * AiTilesPerGraphCell;
                    layer.packedPositions[graphCell] = char((localY << 4) | localX);
                }
            }

        QQueue<int> queue;
        for(int gy = 0; gy < input.graphHeight; ++gy)
            for(int gx = 0; gx < input.graphWidth; ++gx)
            {
                const int graphCell = tileIndex(gx, gy, input.graphWidth);
                const int start = representative[graphCell];
                if(start < 0)
                    continue;
                const int lowX = qMax(0, (gx - 1) * AiTilesPerGraphCell);
                const int lowY = qMax(0, (gy - 1) * AiTilesPerGraphCell);
                const int highX = qMin(aiWidth, (gx + 2) * AiTilesPerGraphCell);
                const int highY = qMin(aiHeight, (gy + 2) * AiTilesPerGraphCell);
                const int regionWidth = highX - lowX;
                const int regionHeight = highY - lowY;
                QVector<qint16> distance(regionWidth * regionHeight, qint16(-1));
                const auto regionIndex = [=](int x, int y)
                {
                    return (y - lowY) * regionWidth + (x - lowX);
                };
                distance[regionIndex(start % aiWidth, start / aiWidth)] = 0;
                queue.clear();
                queue.enqueue(start);
                while(!queue.isEmpty())
                {
                    const int current = queue.dequeue();
                    const int cx = current % aiWidth;
                    const int cy = current / aiWidth;
                    const qint16 currentDistance = distance[regionIndex(cx, cy)];
                    for(int direction = 0; direction < DirectionCount; ++direction)
                    {
                        const QPoint delta = CAiGraphDocument::directionDelta(direction);
                        const int nx = cx + delta.x();
                        const int ny = cy + delta.y();
                        if(nx < lowX || nx >= highX || ny < lowY || ny >= highY
                           || !passable[tileIndex(nx, ny, aiWidth)])
                            continue;
                        if(delta.x() != 0 && delta.y() != 0
                           && (!passable[tileIndex(cx + delta.x(), cy, aiWidth)]
                               || !passable[tileIndex(cx, cy + delta.y(), aiWidth)]))
                            continue;
                        const int ri = regionIndex(nx, ny);
                        if(distance[ri] >= 0)
                            continue;
                        distance[ri] = qint16(currentDistance + 1);
                        queue.enqueue(tileIndex(nx, ny, aiWidth));
                    }
                }

                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    const QPoint delta = CAiGraphDocument::directionDelta(direction);
                    const int ngx = gx + delta.x();
                    const int ngy = gy + delta.y();
                    if(ngx < 0 || ngx >= input.graphWidth
                       || ngy < 0 || ngy >= input.graphHeight)
                        continue;
                    const int destination = representative[
                        tileIndex(ngx, ngy, input.graphWidth)];
                    if(destination < 0)
                        continue;
                    const int dx = destination % aiWidth;
                    const int dy = destination / aiWidth;
                    if(dx < lowX || dx >= highX || dy < lowY || dy >= highY)
                        continue;
                    const qint16 steps = distance[regionIndex(dx, dy)];
                    if(steps >= 0)
                        layer.weights[graphCell * DirectionCount + direction] =
                            qint16(qMin(32767, int(steps) * 128));
                }
            }
        assignComponents(&layer, input.graphWidth, input.graphHeight);
        if(layerIndex == 3)
            result.blockedAiTiles = int(std::count(passable.cbegin(), passable.cend(), false));
    }

    QString error;
    if(!result.document.initializePreview(
           input.graphWidth, input.graphHeight, layers, &error))
    {
        result.error = error;
        return result;
    }
    result.collisionProxyCount = input.collisionProxies.size();
    result.valid = true;
    return result;
}

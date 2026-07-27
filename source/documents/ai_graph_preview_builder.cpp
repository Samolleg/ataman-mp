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

float cross2d(const QVector2D& a, const QVector2D& b, const QVector2D& c)
{
    return (b.x() - a.x()) * (c.y() - a.y())
        - (b.y() - a.y()) * (c.x() - a.x());
}

bool pointInConvexPolygon(const QVector<QVector2D>& polygon, const QVector2D& point)
{
    if(polygon.size() < 3)
        return false;
    float orientation = 0.0f;
    for(int index = 0; index < polygon.size(); ++index)
    {
        const float side = cross2d(polygon.at(index),
                                   polygon.at((index + 1) % polygon.size()), point);
        if(qAbs(side) <= 1.0e-5f)
            continue;
        if(orientation == 0.0f)
            orientation = side;
        else if((orientation < 0.0f) != (side < 0.0f))
            return false;
    }
    return true;
}

bool segmentsIntersect(const QVector2D& a, const QVector2D& b,
                       const QVector2D& c, const QVector2D& d)
{
    constexpr float epsilon = 1.0e-5f;
    const float abC = cross2d(a, b, c), abD = cross2d(a, b, d);
    const float cdA = cross2d(c, d, a), cdB = cross2d(c, d, b);
    const bool properIntersection = ((abC < -epsilon && abD > epsilon)
                                     || (abC > epsilon && abD < -epsilon))
        && ((cdA < -epsilon && cdB > epsilon)
            || (cdA > epsilon && cdB < -epsilon));
    if(properIntersection)
        return true;
    const auto onSegment = [epsilon](const QVector2D& start, const QVector2D& end,
                                     const QVector2D& point) {
        return qAbs(cross2d(start, end, point)) <= epsilon
            && point.x() >= qMin(start.x(), end.x()) - epsilon
            && point.x() <= qMax(start.x(), end.x()) + epsilon
            && point.y() >= qMin(start.y(), end.y()) - epsilon
            && point.y() <= qMax(start.y(), end.y()) + epsilon;
    };
    return onSegment(a, b, c) || onSegment(a, b, d)
        || onSegment(c, d, a) || onSegment(c, d, b);
}

bool volumeCoversAiTile(const SAiGraphCollisionProxy& volume, int x, int y)
{
    if(volume.footprint.size() < 3)
        return false;
    const float left = x * CAiGraphPreviewBuilder::AiTileWorldSize;
    const float top = y * CAiGraphPreviewBuilder::AiTileWorldSize;
    const float right = left + CAiGraphPreviewBuilder::AiTileWorldSize;
    const float bottom = top + CAiGraphPreviewBuilder::AiTileWorldSize;
    float minX = volume.footprint.front().x(), maxX = minX;
    float minY = volume.footprint.front().y(), maxY = minY;
    for(const QVector2D& point : volume.footprint)
    {
        minX = qMin(minX, point.x()); maxX = qMax(maxX, point.x());
        minY = qMin(minY, point.y()); maxY = qMax(maxY, point.y());
        if(point.x() >= left && point.x() <= right
           && point.y() >= top && point.y() <= bottom)
            return true;
    }
    if(maxX < left || minX > right || maxY < top || minY > bottom)
        return false;
    const QVector2D square[] = {QVector2D(left, top), QVector2D(right, top),
                                QVector2D(right, bottom), QVector2D(left, bottom)};
    for(const QVector2D& point : square)
        if(pointInConvexPolygon(volume.footprint, point))
            return true;
    for(int edge = 0; edge < volume.footprint.size(); ++edge)
    {
        const QVector2D a = volume.footprint.at(edge);
        const QVector2D b = volume.footprint.at((edge + 1) % volume.footprint.size());
        for(int side = 0; side < 4; ++side)
            if(segmentsIntersect(a, b, square[side], square[(side + 1) % 4]))
                return true;
    }
    return false;
}

quint8 applyObstacleQuality(quint8 quality, EAiGraphObjectPassability passability,
                            float obstacleMinimum, float obstacleMaximum,
                            float ground, float ceiling)
{
    if(passability == EAiGraphObjectPassability::Ignored
       || passability == EAiGraphObjectPassability::Easy
       || passability == EAiGraphObjectPassability::Bridge
       || obstacleMinimum >= ceiling || obstacleMaximum <= ground)
        return quality;
    if(passability == EAiGraphObjectPassability::Danger)
        return qMin<quint8>(quality, 7);
    const float occupied = qMin(obstacleMaximum, ceiling)
        - qMax(obstacleMinimum, ground);
    const float fraction = qBound(0.0f, occupied / qMax(0.01f, ceiling - ground), 1.0f);
    if(passability == EAiGraphObjectPassability::Blocking)
        return fraction >= 0.25f ? 0
            : quint8(qMax(0, int(quality) - qCeil(fraction * 12.0f)));
    return quint8(qMax(1, int(quality) - qCeil(fraction * 5.0f)));
}

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

QVector<bool> graphCellCrossingCandidates(const QVector<quint8>& quality,
                                          int aiWidth, int aiHeight,
                                          int graphX, int graphY)
{
    const int lowX = graphX * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int lowY = graphY * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int highX = qMin(aiWidth - 1, lowX + CAiGraphPreviewBuilder::AiTilesPerGraphCell);
    const int highY = qMin(aiHeight - 1, lowY + CAiGraphPreviewBuilder::AiTilesPerGraphCell);
    const int localWidth = highX - lowX + 1;
    const int localHeight = highY - lowY + 1;
    QVector<int> components(localWidth * localHeight, -1);
    QVector<bool> componentCrosses;
    const auto localIndex = [=](int x, int y)
    { return (y - lowY) * localWidth + x - lowX; };
    QQueue<int> queue;
    for(int startY = lowY; startY <= highY; ++startY)
        for(int startX = lowX; startX <= highX; ++startX)
        {
            const int startLocal = localIndex(startX, startY);
            if(components.at(startLocal) >= 0
               || !quality.at(tileIndex(startX, startY, aiWidth)))
                continue;
            const int component = componentCrosses.size();
            components[startLocal] = component;
            queue.enqueue(tileIndex(startX, startY, aiWidth));
            bool left = false, right = false, top = false, bottom = false;
            while(!queue.isEmpty())
            {
                const int current = queue.dequeue();
                const int x = current % aiWidth, y = current / aiWidth;
                left |= x == lowX; right |= x == highX;
                top |= y == lowY; bottom |= y == highY;
                const QPoint cardinal[] = {
                    QPoint(-1,0), QPoint(1,0), QPoint(0,-1), QPoint(0,1)};
                for(const QPoint& delta : cardinal)
                {
                    const int nx = x + delta.x(), ny = y + delta.y();
                    if(nx < lowX || nx > highX || ny < lowY || ny > highY
                       || !quality.at(tileIndex(nx, ny, aiWidth)))
                        continue;
                    const int local = localIndex(nx, ny);
                    if(components.at(local) < 0)
                    {
                        components[local] = component;
                        queue.enqueue(tileIndex(nx, ny, aiWidth));
                    }
                }
            }
            componentCrosses.append((left && right) || (top && bottom));
        }

    QVector<bool> result(CAiGraphPreviewBuilder::AiTilesPerGraphCell
                         * CAiGraphPreviewBuilder::AiTilesPerGraphCell, false);
    for(int localY = 0; localY < CAiGraphPreviewBuilder::AiTilesPerGraphCell; ++localY)
        for(int localX = 0; localX < CAiGraphPreviewBuilder::AiTilesPerGraphCell; ++localX)
        {
            const int component = components.at(localIndex(
                lowX + localX, lowY + localY));
            if(component >= 0)
                result[localY * CAiGraphPreviewBuilder::AiTilesPerGraphCell + localX]
                    = componentCrosses.at(component);
        }
    return result;
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
            "Some objects lack authored navigation bounds; only those objects use conservative rendered-volume fallbacks."));

    QVector<float> navigationHeights = input.terrainHeights;
    QVector<float> navigationWater = input.waterDepths;
    if(navigationWater.isEmpty())
        navigationWater.fill(0.0f, aiCount);
    QVector<bool> bridgeTiles(aiCount, false);
    // Resolve polygon/tile coverage once. The earlier source-correct object
    // pass repeated the same polygon test for every profile and every AI tile
    // (and performed a second full scan for bridges). A populated map such as
    // gz1g has thousands of proxies, turning that into billions of identical
    // tests. Indexing each proxy only over its bounded tile rectangle preserves
    // the exact coverage predicate while making the eight profile passes local.
    QVector<QVector<int>> collisionProxiesByTile(aiCount);
    for(int proxyIndex = 0; proxyIndex < input.collisionProxies.size(); ++proxyIndex)
    {
        const SAiGraphCollisionProxy& volume = input.collisionProxies.at(proxyIndex);
        if(volume.authored)
            ++result.authoredCollisionProxyCount;
        if(volume.footprint.size() < 3
           || volume.passability == EAiGraphObjectPassability::Ignored
           || volume.passability == EAiGraphObjectPassability::Easy)
            continue;
        float minimumX = volume.footprint.front().x();
        float maximumX = minimumX;
        float minimumY = volume.footprint.front().y();
        float maximumY = minimumY;
        for(const QVector2D& point : volume.footprint)
        {
            minimumX = qMin(minimumX, point.x());
            maximumX = qMax(maximumX, point.x());
            minimumY = qMin(minimumY, point.y());
            maximumY = qMax(maximumY, point.y());
        }
        // Include the tile immediately before an exact boundary because the
        // original inclusive predicate treats edge contact as coverage.
        const int firstX = qBound(0, int(std::floor(
            minimumX / AiTileWorldSize)) - 1, aiWidth - 1);
        const int lastX = qBound(0, int(std::floor(
            maximumX / AiTileWorldSize)), aiWidth - 1);
        const int firstY = qBound(0, int(std::floor(
            minimumY / AiTileWorldSize)) - 1, aiHeight - 1);
        const int lastY = qBound(0, int(std::floor(
            maximumY / AiTileWorldSize)), aiHeight - 1);
        if(firstX > lastX || firstY > lastY)
            continue;
        for(int y = firstY; y <= lastY; ++y)
            for(int x = firstX; x <= lastX; ++x)
            {
                ++result.collisionCoverageTests;
                if(!volumeCoversAiTile(volume, x, y))
                    continue;
                const int index = tileIndex(x, y, aiWidth);
                if(volume.passability != EAiGraphObjectPassability::Bridge)
                    collisionProxiesByTile[index].append(proxyIndex);
                else
                {
                    navigationHeights[index] = qMax(navigationHeights.at(index), volume.maximumZ);
                    navigationWater[index] = 0.0f;
                    if(!bridgeTiles.at(index))
                        ++result.bridgeAiTiles;
                    bridgeTiles[index] = true;
                }
            }
    }

    QVector<bool> walkSlopeBlocked(aiCount, false);
    QVector<bool> flySlopeBlocked(aiCount, false);
    const float walkStep = qTan(qDegreesToRadians(WalkSlopeLimitDegrees)) * AiTileWorldSize;
    const float flyStep = qTan(qDegreesToRadians(FlySlopeLimitDegrees)) * AiTileWorldSize;
    for(int y = 1; y < aiHeight - 1; ++y)
        for(int x = 1; x < aiWidth - 1; ++x)
        {
            const int index = tileIndex(x, y, aiWidth);
            const float ground = navigationHeights.at(index);
            float difference = 0.0f;
            const QPoint cardinal[] = {QPoint(-1,0), QPoint(1,0), QPoint(0,-1), QPoint(0,1)};
            for(const QPoint& delta : cardinal)
                difference = qMax(difference, qAbs(navigationHeights.at(
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
                int quality = bridgeTiles.at(index) ? 15
                    : (policy.speed < 0 || policy.cost > 70) ? 0
                    : qBound(1, (policy.speed * 15 + 512) / 1024
                                   - qMax(0, policy.cost - 1) / 5, 15);
                const float depth = qMax(0.0f, navigationWater.at(index));
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
                const float ground = navigationHeights.at(index)
                    + (layerIndex == 0 ? depth : 0.0f);
                const float top = ground + ProfileHeights[layerIndex];
                for(const int proxyIndex : collisionProxiesByTile.at(index))
                {
                    const SAiGraphCollisionProxy& proxy =
                        input.collisionProxies.at(proxyIndex);
                    quality = applyObstacleQuality(quint8(quality), proxy.passability,
                                                   proxy.minimumZ, proxy.maximumZ,
                                                   ground + 0.05f, top);
                    if(quality <= 0)
                        break;
                }
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
                const QVector<bool> crossingCandidates =
                    graphCellCrossingCandidates(
                        quality, aiWidth, aiHeight, gx, gy);
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
                        const bool crosses = crossingCandidates.at(
                            ly * AiTilesPerGraphCell + lx);
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
                        const float dz = navigationHeights.at(next) - navigationHeights.at(current);
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

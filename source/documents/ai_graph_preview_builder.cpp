#include "ai_graph_preview_builder.h"

#include <QElapsedTimer>
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
constexpr int MaximumAiHeight = 0x1ff;
constexpr int MaximumAiDepth = 0x3e;
constexpr int MaximumObjectHeight = 1020;
const float ProfileHeights[LayerCount] = {2.0f, 0.4f, 1.0f, 1.6f, 2.2f, 1.7f, 3.0f, 6.0f};
const int TerrainRouteCosts[16] = {
    -1, 25600, 30700, 35800, 12280, 14320, 16380, 3580,
    4095, 4605, 5120, 1638, 1842, 2048, 1024, 1228};
const int TerrainIndexTable[5][16] = {
    {0,0,1,1,1,1,4,7,11,12,14,14,15,15,15,15},
    {0,0,1,1,1,1,4,7,11,12,13,13,13,13,13,13},
    {0,0,1,1,1,1,4,7,8,9,10,10,10,10,10,10},
    {0,0,1,1,1,1,4,5,6,6,6,6,6,6,6,6},
    {0,0,1,1,1,1,2,3,3,3,3,3,3,3,3,3}};

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
    return properIntersection;
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
    constexpr float halfOpenEpsilon = 1.0e-5f;
    for(const QVector2D& point : volume.footprint)
    {
        minX = qMin(minX, point.x()); maxX = qMax(maxX, point.x());
        minY = qMin(minY, point.y()); maxY = qMax(maxY, point.y());
        // AI tiles are a half-open raster. Merely touching the right/bottom
        // boundary belongs to the adjacent tile and must not thicken a wall
        // or close a narrow doorway by one extra 0.5 m sample.
        if(point.x() >= left && point.x() < right - halfOpenEpsilon
           && point.y() >= top && point.y() < bottom - halfOpenEpsilon)
            return true;
    }
    if(maxX < left || minX > right || maxY < top || minY > bottom)
        return false;
    const QVector2D square[] = {
        QVector2D(left + halfOpenEpsilon, top + halfOpenEpsilon),
        QVector2D(right - halfOpenEpsilon, top + halfOpenEpsilon),
        QVector2D(right - halfOpenEpsilon, bottom - halfOpenEpsilon),
        QVector2D(left + halfOpenEpsilon, bottom - halfOpenEpsilon)};
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

bool verticalIntervalAtPoint(const SAiGraphCollisionProxy& volume,
                             float x, float y,
                             float* minimumZ, float* maximumZ)
{
    if(volume.boxCorners.size() != 8 || !minimumZ || !maximumZ)
        return false;
    const QVector3D origin = volume.boxCorners.at(0);
    const QVector3D axisX = volume.boxCorners.at(1) - origin;
    const QVector3D axisY = volume.boxCorners.at(2) - origin;
    const QVector3D axisZ = volume.boxCorners.at(4) - origin;
    const QVector3D crossYZ = QVector3D::crossProduct(axisY, axisZ);
    const float determinant = QVector3D::dotProduct(axisX, crossYZ);
    if(qAbs(determinant) <= 1.0e-8f)
        return false;
    const QVector3D reciprocal[3] = {
        crossYZ / determinant,
        QVector3D::crossProduct(axisZ, axisX) / determinant,
        QVector3D::crossProduct(axisX, axisY) / determinant
    };
    const QVector3D atZero(x - origin.x(), y - origin.y(), -origin.z());
    float low = -std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::max();
    for(const QVector3D& coordinate : reciprocal)
    {
        const float base = QVector3D::dotProduct(atZero, coordinate);
        const float slope = coordinate.z();
        if(qAbs(slope) <= 1.0e-8f)
        {
            if(base < -1.0e-5f || base > 1.00001f)
                return false;
            continue;
        }
        const float first = -base / slope;
        const float second = (1.0f - base) / slope;
        low = qMax(low, qMin(first, second));
        high = qMin(high, qMax(first, second));
        if(low > high + 1.0e-5f)
            return false;
    }
    if(!qIsFinite(low) || !qIsFinite(high))
        return false;
    *minimumZ = low;
    *maximumZ = high;
    return true;
}

void collisionIntervalForAiTile(const SAiGraphCollisionProxy& volume,
                                int x, int y,
                                float* minimumZ, float* maximumZ)
{
    float low = std::numeric_limits<float>::max();
    float high = -std::numeric_limits<float>::max();
    bool found = false;
    const float left = float(x) * CAiGraphPreviewBuilder::AiTileWorldSize;
    const float top = float(y) * CAiGraphPreviewBuilder::AiTileWorldSize;
    const float sample[] = {0.0f, 0.5f, 1.0f};
    for(float sy : sample)
        for(float sx : sample)
        {
            float sampleLow = 0.0f, sampleHigh = 0.0f;
            if(!verticalIntervalAtPoint(
                   volume,
                   left + sx * CAiGraphPreviewBuilder::AiTileWorldSize,
                   top + sy * CAiGraphPreviewBuilder::AiTileWorldSize,
                   &sampleLow, &sampleHigh))
                continue;
            low = qMin(low, sampleLow);
            high = qMax(high, sampleHigh);
            found = true;
        }
    // Very thin volumes can cross a tile without touching the regular sample
    // lattice. Preserve their conservative coverage while using per-tile
    // intervals for ordinary authored volumes.
    *minimumZ = found ? low : volume.minimumZ;
    *maximumZ = found ? high : volume.maximumZ;
}

struct SIndexedCollisionTile
{
    int proxyIndex{-1};
    int next{-1};
    int minimumHeight{0};
    int maximumHeight{0};
};

int quantizedHeight(float worldHeight, float maximumTerrainHeight,
                    int maximumValue = MaximumAiHeight)
{
    if(!qIsFinite(worldHeight) || !qIsFinite(maximumTerrainHeight)
       || maximumTerrainHeight <= 0.0f)
        return 0;
    return qBound(0, qRound(worldHeight * float(MaximumAiHeight)
                            / maximumTerrainHeight), maximumValue);
}

int quantizedDepth(float worldDepth, float maximumTerrainHeight)
{
    const int depth = quantizedHeight(worldDepth, maximumTerrainHeight,
                                      MaximumObjectHeight);
    return depth > MaximumAiDepth ? MaximumAiDepth + 1 : depth;
}

float dequantizedHeight(int height, float maximumTerrainHeight)
{
    return float(height) * maximumTerrainHeight / float(MaximumAiHeight);
}

int elevationMultiplier(int heightDelta, float maximumTerrainHeight, bool flying)
{
    const float worldDifference = qAbs(float(heightDelta))
        * maximumTerrainHeight / float(MaximumAiHeight);
    const float angle = qRadiansToDegrees(qAtan2(worldDifference,
                                                  CAiGraphPreviewBuilder::AiTileWorldSize));
    if(angle > (flying ? FlySlopeLimitDegrees : WalkSlopeLimitDegrees))
        return -1;
    if(flying)
        return 1024;
    const float sine = qSin(qDegreesToRadians(angle));
    const float uphill = 1.0f + sine * sine;
    const float multiplier = heightDelta < 0 ? 1.0f / uphill
                                              : uphill * uphill;
    return qRound(multiplier * 1024.0f);
}

struct STilePassability
{
    int minimumHeight{0};
    int maximumHeight{1};
    int speed{1024};
    int cost{1};
    int waterUnitHeight{0};
    bool danger{false};
};

void applyWater(STilePassability* tile, int depth)
{
    if(!tile || tile->speed < 0 || depth <= 0)
        return;
    if(depth == MaximumAiDepth + 1 || depth >= tile->waterUnitHeight - 1)
        tile->speed = -1;
    else if(tile->waterUnitHeight < 1024)
    {
        if(depth > tile->waterUnitHeight / 2)
        {
            tile->cost += 30;
            tile->speed = (tile->speed * 100) >> 7;
        }
        else
            tile->speed = (tile->speed * 110) >> 7;
    }
}

void applyObstacle(STilePassability* tile,
                   EAiGraphObjectPassability passability,
                   int obstacleMinimum, int obstacleMaximum)
{
    if(!tile || passability == EAiGraphObjectPassability::Ignored
       || passability == EAiGraphObjectPassability::Easy
       || passability == EAiGraphObjectPassability::Bridge)
        return;
    if(passability == EAiGraphObjectPassability::Danger)
    {
        tile->danger = true;
        return;
    }
    if(tile->speed < 0 || obstacleMinimum > tile->maximumHeight
       || obstacleMaximum < tile->minimumHeight)
        return;
    obstacleMinimum = qMax(obstacleMinimum, tile->minimumHeight);
    obstacleMaximum = qMin(obstacleMaximum, tile->maximumHeight);
    const float fraction = float(obstacleMaximum - obstacleMinimum)
        / float(qMax(1, tile->maximumHeight - tile->minimumHeight));
    const int passabilityIndex = passability == EAiGraphObjectPassability::Blocking
        ? 0 : 1;
    static const int hardCosts[2] = {290, 20};
    static const int speedCosts[2] = {7, 2};
    tile->cost += qRound(float(hardCosts[passabilityIndex]) * fraction);
    tile->speed = qRound(float(tile->speed)
        * qExp(qLn(0.6f) * fraction * float(speedCosts[passabilityIndex])));
}

quint8 terrainClass(const STilePassability& tile)
{
    if(tile.speed < 0 || tile.cost > 70)
        return 0;
    const int speedIndex = qBound(0, (tile.speed + 50) / 102, 15);
    int cost = tile.danger ? tile.cost * 2 : tile.cost;
    int costIndex = 0;
    if(cost < 10)
        costIndex = cost < 3 ? (cost < 2 ? 0 : 1) : 2;
    else
        costIndex = cost < 35 ? 3 : 4;
    return quint8(TerrainIndexTable[costIndex][speedIndex]);
}

template <size_t N>
QVector<quint8> expandImpassable(const QVector<quint8>& source, int width, int height,
                                 const QPoint (&footprint)[N])
{
    QVector<quint8> result = source;
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            for(const QPoint& offset : footprint)
            {
                const int sx = x + offset.x();
                const int sy = y + offset.y();
                if(sx < 0 || sy < 0 || sx >= width || sy >= height
                   || source.at(tileIndex(sx, sy, width)) == 0)
                {
                    result[tileIndex(x, y, width)] = 0;
                    break;
                }
            }
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
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    const QPoint delta = CAiGraphDocument::directionDelta(direction);
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
                const qint16 consistent = qMin(forward, reverse);
                forward = consistent;
                reverse = consistent;
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

bool SAiGraphPreviewBuildResult::projectHumanPosition(
    const QPointF& requested, QVector3D* projected, float maximumDistance) const
{
    if(!projected || !valid || !document.isValid()
       || !qIsFinite(requested.x()) || !qIsFinite(requested.y())
       || !qIsFinite(maximumDistance) || maximumDistance < 0.0f)
        return false;
    const int aiWidth = document.width() * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = document.height() * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiCount = aiWidth * aiHeight;
    if(humanPassability.size() != aiCount
       || navigationSurfaceHeights.size() != aiCount)
        return false;

    const float tileSize = CAiGraphPreviewBuilder::AiTileWorldSize;
    const int radius = qCeil(maximumDistance / tileSize);
    const int centerX = qBound(0, qFloor(requested.x() / tileSize), aiWidth - 1);
    const int centerY = qBound(0, qFloor(requested.y() / tileSize), aiHeight - 1);
    const float maximumDistanceSquared = maximumDistance * maximumDistance;
    float bestDistanceSquared = std::numeric_limits<float>::max();
    int best = -1;
    for(int y = qMax(0, centerY - radius); y <= qMin(aiHeight - 1, centerY + radius); ++y)
        for(int x = qMax(0, centerX - radius); x <= qMin(aiWidth - 1, centerX + radius); ++x)
        {
            const int index = tileIndex(x, y, aiWidth);
            if(humanPassability.at(index) == 0)
                continue;
            const float worldX = (float(x) + 0.5f) * tileSize;
            const float worldY = (float(y) + 0.5f) * tileSize;
            const float dx = worldX - requested.x();
            const float dy = worldY - requested.y();
            const float distanceSquared = dx * dx + dy * dy;
            if(distanceSquared > maximumDistanceSquared
               || distanceSquared >= bestDistanceSquared)
                continue;
            bestDistanceSquared = distanceSquared;
            best = index;
        }
    if(best < 0)
        return false;
    const int bestX = best % aiWidth;
    const int bestY = best / aiWidth;
    *projected = QVector3D((float(bestX) + 0.5f) * tileSize,
                           (float(bestY) + 0.5f) * tileSize,
                           navigationSurfaceHeights.at(best));
    return true;
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
    QElapsedTimer totalTimer;
    totalTimer.start();
    if(input.graphWidth <= 0 || input.graphHeight <= 0
       || input.graphWidth > 256 || input.graphHeight > 256)
    { result.error = QStringLiteral("Invalid graph dimensions."); return result; }
    if(!qIsFinite(input.maximumTerrainHeight) || input.maximumTerrainHeight <= 0.0f)
    { result.error = QStringLiteral("Invalid terrain height range."); return result; }
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

    QVector<int> navigationHeights(aiCount, 0);
    QVector<int> navigationWater(aiCount, 0);
    QVector<quint8> navigationTypes = input.terrainTypes;
    if(navigationTypes.isEmpty())
        navigationTypes.fill(0, aiCount);
    for(int index = 0; index < aiCount; ++index)
    {
        navigationHeights[index] = quantizedHeight(
            input.terrainHeights.at(index), input.maximumTerrainHeight);
        if(!input.waterDepths.isEmpty())
            navigationWater[index] = quantizedDepth(
                qMax(0.0f, input.waterDepths.at(index)),
                input.maximumTerrainHeight);
        if(navigationWater.at(index) > 0)
            ++result.waterAiTiles;
    }
    QVector<bool> bridgeTiles(aiCount, false);
    // Rasterize object coverage and transformed vertical intervals once. A flat
    // linked index avoids allocating one QVector for every 0.5 m navigation tile.
    QElapsedTimer collisionTimer;
    collisionTimer.start();
    QVector<int> collisionHead(aiCount, -1);
    QVector<SIndexedCollisionTile> collisionTiles;
    collisionTiles.reserve(input.collisionProxies.size() * 8);
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
                float localMinimumZ = volume.minimumZ;
                float localMaximumZ = volume.maximumZ;
                collisionIntervalForAiTile(volume, x, y,
                                           &localMinimumZ, &localMaximumZ);
                if(volume.passability != EAiGraphObjectPassability::Bridge)
                {
                    SIndexedCollisionTile tile;
                    tile.proxyIndex = proxyIndex;
                    tile.next = collisionHead.at(index);
                    tile.minimumHeight = quantizedHeight(
                        localMinimumZ, input.maximumTerrainHeight,
                        MaximumObjectHeight);
                    tile.maximumHeight = quantizedHeight(
                        localMaximumZ, input.maximumTerrainHeight,
                        MaximumObjectHeight);
                    collisionHead[index] = collisionTiles.size();
                    collisionTiles.append(tile);
                }
                else
                {
                    navigationHeights[index] = qMax(navigationHeights.at(index),
                        quantizedHeight(localMaximumZ,
                                        input.maximumTerrainHeight,
                                        MaximumObjectHeight));
                    navigationWater[index] = 0;
                    navigationTypes[index] = 7;
                    if(!bridgeTiles.at(index))
                        ++result.bridgeAiTiles;
                    bridgeTiles[index] = true;
                }
            }
    }
    result.collisionRasterMilliseconds = collisionTimer.elapsed();
    result.navigationSurfaceHeights.resize(aiCount);
    result.bridgeMask.resize(aiCount);
    for(int index = 0; index < aiCount; ++index)
    {
        result.navigationSurfaceHeights[index] = dequantizedHeight(
            navigationHeights.at(index), input.maximumTerrainHeight);
        result.bridgeMask[index] = bridgeTiles.at(index) ? quint8(1) : quint8(0);
    }

    QVector<bool> walkSlopeBlocked(aiCount, false);
    QVector<bool> flySlopeBlocked(aiCount, false);
    for(int y = 1; y < aiHeight - 1; ++y)
        for(int x = 1; x < aiWidth - 1; ++x)
        {
            const int index = tileIndex(x, y, aiWidth);
            const int ground = navigationHeights.at(index);
            int difference = 0;
            const QPoint cardinal[] = {QPoint(-1,0), QPoint(1,0), QPoint(0,-1), QPoint(0,1)};
            for(const QPoint& delta : cardinal)
                difference = qMax(difference, qAbs(navigationHeights.at(
                    tileIndex(x + delta.x(), y + delta.y(), aiWidth)) - ground));
            walkSlopeBlocked[index] = elevationMultiplier(
                difference, input.maximumTerrainHeight, false) < 0;
            flySlopeBlocked[index] = elevationMultiplier(
                difference, input.maximumTerrainHeight, true) < 0;
        }

    QVector<SAiGraphLayer> layers(LayerCount);
    QElapsedTimer routeTimer;
    routeTimer.start();
    for(int layerIndex = 0; layerIndex < LayerCount; ++layerIndex)
    {
        QVector<quint8> rawQuality(aiCount, 0);
        const int unitHeight = qMax(1, quantizedHeight(
            ProfileHeights[layerIndex], input.maximumTerrainHeight,
            MaximumObjectHeight));
        for(int y = 0; y < aiHeight; ++y)
            for(int x = 0; x < aiWidth; ++x)
            {
                const int index = tileIndex(x, y, aiWidth);
                const int terrainType = navigationTypes.at(index);
                const SAiTerrainTraversal policy = terrainType >= 0 && terrainType < traversal.size()
                    ? traversal.at(terrainType) : SAiTerrainTraversal();
                STilePassability tile;
                tile.minimumHeight = navigationHeights.at(index)
                    + (layerIndex == 0 ? navigationWater.at(index) : 0);
                tile.maximumHeight = tile.minimumHeight + unitHeight;
                tile.speed = layerIndex == 0 ? 1024 : policy.speed;
                tile.cost = layerIndex == 0 ? 1 : policy.cost;
                tile.waterUnitHeight = layerIndex == 0
                    ? MaximumObjectHeight * 2 : (layerIndex == 1 ? 0 : unitHeight);
                if(layerIndex != 0)
                    applyWater(&tile, navigationWater.at(index));
                for(int collisionIndex = collisionHead.at(index);
                    collisionIndex >= 0;
                    collisionIndex = collisionTiles.at(collisionIndex).next)
                {
                    const SIndexedCollisionTile& collision =
                        collisionTiles.at(collisionIndex);
                    const SAiGraphCollisionProxy& proxy =
                        input.collisionProxies.at(collision.proxyIndex);
                    applyObstacle(&tile, proxy.passability,
                                  collision.minimumHeight,
                                  collision.maximumHeight);
                }
                rawQuality[index] = terrainClass(tile);
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

        // Edge costs depend only on the current layer's tile qualities and
        // terrain heights. Precompute them once instead of recalculating slope,
        // terrain, and diagonal multipliers in every graph-cell search.
        QVector<int> traversalEdgeCosts(aiCount * DirectionCount, -1);
        for(int y = 0; y < aiHeight; ++y)
            for(int x = 0; x < aiWidth; ++x)
            {
                const int current = tileIndex(x, y, aiWidth);
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    const QPoint delta = CAiGraphDocument::directionDelta(direction);
                    const int nx = x + delta.x(), ny = y + delta.y();
                    if(nx < 0 || ny < 0 || nx >= aiWidth || ny >= aiHeight)
                        continue;
                    const int next = tileIndex(nx, ny, aiWidth);
                    if(quality.at(next) == 0)
                        continue;
                    const int elevation = elevationMultiplier(
                        navigationHeights.at(next) - navigationHeights.at(current),
                        input.maximumTerrainHeight, layerIndex == 0);
                    int stepCost = TerrainRouteCosts[quality.at(next)];
                    if(elevation < 0 || stepCost < 0)
                        continue;
                    stepCost = (stepCost * elevation) >> 10;
                    stepCost = (stepCost * (delta.x() && delta.y() ? 1448 : 1024)) >> 10;
                    traversalEdgeCosts[current * DirectionCount + direction] = stepCost;
                }
            }

        SAiGraphLayer& layer = layers[layerIndex];
        layer.weights.fill(qint16(-1), input.graphWidth * input.graphHeight * DirectionCount);
        // A graph cell without a passable representative still needs a legal
        // in-cell position. The runtime generator uses the central (3,3)
        // fallback; 0xff points outside the 8x8 cell and is not stageable.
        layer.packedPositions.fill(char(0x33), input.graphWidth * input.graphHeight);
        QVector<int> representative(input.graphWidth * input.graphHeight, -1);
        for(int gy = 0; gy < input.graphHeight; ++gy)
            for(int gx = 0; gx < input.graphWidth; ++gx)
            {
                const QVector<bool> crossingCandidates =
                    graphCellCrossingCandidates(
                        quality, aiWidth, aiHeight, gx, gy);
                int best = tileIndex(gx * 8 + 3, gy * 8 + 3, aiWidth);
                int bestQuality = 0;
                bool foundCrossing = false;
                bool foundCrossingBridge = false;
                for(int size = 2; size <= AiTilesPerGraphCell; size += 2)
                {
                    const int offset = (AiTilesPerGraphCell - size) / 2;
                    for(int ly = offset; ly < offset + size; ++ly)
                        for(int lx = offset; lx < offset + size; ++lx)
                    {
                        const int candidate = tileIndex(gx * 8 + lx, gy * 8 + ly, aiWidth);
                        const int candidateQuality = quality.at(candidate);
                        if(candidateQuality <= 0)
                            continue;
                        const bool crosses = crossingCandidates.at(
                            ly * AiTilesPerGraphCell + lx);
                        const bool crossingBridge = crosses
                            && bridgeTiles.at(candidate);
                        if((crossingBridge && !foundCrossingBridge)
                           || (!foundCrossingBridge
                               && (candidateQuality > bestQuality || !foundCrossing)))
                        {
                            if(crosses || !foundCrossing)
                            {
                                best = candidate;
                                bestQuality = candidateQuality;
                                foundCrossing = crosses;
                                foundCrossingBridge = crossingBridge;
                            }
                        }
                    }
                }
                const int graphCell = tileIndex(gx, gy, input.graphWidth);
                representative[graphCell] = best;
                const int localX = best % aiWidth - gx * 8;
                const int localY = best / aiWidth - gy * 8;
                layer.packedPositions[graphCell] = char((localY << 4) | localX);
            }

        using QueueEntry = QPair<int, int>;
        for(int gy = 0; gy < input.graphHeight; ++gy)
            for(int gx = 0; gx < input.graphWidth; ++gx)
            {
                const int graphCell = tileIndex(gx, gy, input.graphWidth);
                const int start = representative.at(graphCell);
                const int lowX = qMax(0, (gx - 1) * 8), lowY = qMax(0, (gy - 1) * 8);
                const int highX = qMin(aiWidth, (gx + 2) * 8 + 1);
                const int highY = qMin(aiHeight, (gy + 2) * 8 + 1);
                const int regionWidth = highX - lowX, regionHeight = highY - lowY;
                QVector<int> distance(regionWidth * regionHeight, std::numeric_limits<int>::max());
                const auto regionIndex = [=](int x, int y) { return (y-lowY)*regionWidth + x-lowX; };
                QVector<quint8> destinationMask(regionWidth * regionHeight, 0);
                int destinationsRemaining = 0;
                for(int direction = 0; direction < DirectionCount; ++direction)
                {
                    const QPoint delta = CAiGraphDocument::directionDelta(direction);
                    const int ngx = gx + delta.x(), ngy = gy + delta.y();
                    if(ngx < 0 || ngy < 0
                       || ngx >= input.graphWidth || ngy >= input.graphHeight)
                        continue;
                    const int destination = representative.at(
                        tileIndex(ngx, ngy, input.graphWidth));
                    const int dx = destination % aiWidth, dy = destination / aiWidth;
                    if(dx < lowX || dx >= highX || dy < lowY || dy >= highY)
                        continue;
                    const int destinationRegion = regionIndex(dx, dy);
                    if(!destinationMask.at(destinationRegion))
                    {
                        destinationMask[destinationRegion] = 1;
                        ++destinationsRemaining;
                    }
                }
                if(destinationsRemaining == 0)
                    continue;
                std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
                distance[regionIndex(start % aiWidth, start / aiWidth)] = 0;
                queue.push(qMakePair(0, start));
                while(!queue.empty())
                {
                    const QueueEntry entry = queue.top(); queue.pop();
                    const int currentDistance = entry.first, current = entry.second;
                    const int cx = current % aiWidth, cy = current / aiWidth;
                    if(currentDistance != distance.at(regionIndex(cx, cy))) continue;
                    ++result.routeQueueSettledTiles;
                    const int currentRegion = regionIndex(cx, cy);
                    if(destinationMask.at(currentRegion))
                    {
                        destinationMask[currentRegion] = 0;
                        if(--destinationsRemaining == 0)
                        {
                            ++result.routeSearchEarlyExits;
                            break;
                        }
                    }
                    for(int direction = 0; direction < DirectionCount; ++direction)
                    {
                        const QPoint delta = CAiGraphDocument::directionDelta(direction);
                        const int nx = cx + delta.x(), ny = cy + delta.y();
                        if(nx < lowX || nx >= highX || ny < lowY || ny >= highY) continue;
                        const int next = tileIndex(nx, ny, aiWidth);
                        const int stepCost = traversalEdgeCosts.at(
                            current * DirectionCount + direction);
                        if(stepCost < 0) continue;
                        const int candidate = currentDistance + stepCost;
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
                    const int dx = destination % aiWidth, dy = destination / aiWidth;
                    if(dx < lowX || dx >= highX || dy < lowY || dy >= highY) continue;
                    const int cost = distance.at(regionIndex(dx, dy));
                    if(cost != std::numeric_limits<int>::max())
                        layer.weights[graphCell * DirectionCount + direction] =
                            qint16(qMin(32767, cost / 128));
                }
            }
        makeConsistent(&layer, input.graphWidth, input.graphHeight);
        assignComponents(&layer, input.graphWidth, input.graphHeight);
        if(layerIndex == 3)
        {
            result.blockedAiTiles = int(std::count(quality.cbegin(), quality.cend(), quint8(0)));
            result.humanPassability = quality;
        }
    }
    result.routeBuildMilliseconds = routeTimer.elapsed();

    QString error;
    if(!result.document.initializePreview(input.graphWidth, input.graphHeight, layers, &error))
    { result.error = error; return result; }
    result.collisionProxyCount = input.collisionProxies.size();
    result.buildMilliseconds = totalTimer.elapsed();
    result.valid = true;
    return result;
}

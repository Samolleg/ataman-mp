#include "overlay_material_animation.h"

#include <QtMath>

namespace
{

constexpr float kPi = 3.14159265358979323846f;
constexpr int kNoiseSide = 33;

float tableSine(float angle)
{
    const float tablePosition = angle * (512.0f / (2.0f * kPi));
    const int tableIndex = tablePosition < 0.0f
        ? -int(qFloor(-tablePosition + 0.5f))
        : int(qFloor(tablePosition + 0.5f));
    return qSin(float(tableIndex & 511) * (2.0f * kPi / 512.0f));
}

quint32 nextPreviewRandom(quint32& state)
{
    state = state * 214013u + 2531011u;
    return (state >> 16) & 0x7fffu;
}

const QVector<float>& previewNoise()
{
    static const QVector<float> values = []()
    {
        QVector<float> result(kNoiseSide * kNoiseSide, 0.0f);
        quint32 randomState = 1u;
        for(int circle = 0; circle < 40; ++circle)
        {
            const int centerX = int(nextPreviewRandom(randomState) % kNoiseSide);
            const int centerY = int(nextPreviewRandom(randomState) % kNoiseSide);
            const float radius = float(nextPreviewRandom(randomState) % 5u + 1u);
            const float radiusSquared = radius * radius;
            for(int y = 0; y < kNoiseSide; ++y)
                for(int x = 0; x < kNoiseSide; ++x)
                {
                    const float dx = float(x - centerX);
                    const float dy = float(y - centerY);
                    const float distanceSquared = dx * dx + dy * dy;
                    if(distanceSquared < radiusSquared)
                        result[y * kNoiseSide + x] +=
                            (1.0f - distanceSquared / radiusSquared) * kPi * 0.5f;
                }
        }

        for(int x = 1; x < kNoiseSide - 1; ++x)
        {
            const float average = (result[x] + result[(kNoiseSide - 1) * kNoiseSide + x]) * 0.5f;
            result[x] = average;
            result[(kNoiseSide - 1) * kNoiseSide + x] = average;
        }
        for(int y = 1; y < kNoiseSide - 1; ++y)
        {
            const float average = (result[y * kNoiseSide]
                + result[y * kNoiseSide + kNoiseSide - 1]) * 0.5f;
            result[y * kNoiseSide] = average;
            result[y * kNoiseSide + kNoiseSide - 1] = average;
        }
        const float cornerAverage = (result[0] + result[kNoiseSide - 1]
            + result[(kNoiseSide - 1) * kNoiseSide]
            + result[kNoiseSide * kNoiseSide - 1]) * 0.25f;
        result[0] = cornerAverage;
        result[kNoiseSide - 1] = cornerAverage;
        result[(kNoiseSide - 1) * kNoiseSide] = cornerAverage;
        result[kNoiseSide * kNoiseSide - 1] = cornerAverage;
        return result;
    }();
    return values;
}

}

namespace ei
{

SOverlayMaterialAnimation overlayMaterialAnimation(const SMaterial* material)
{
    SOverlayMaterialAnimation result;
    if(!material)
        return result;
    result.waveMultiplier = material->waveMultiplier;
    result.authoredWarpSpeed = material->warpSpeed;
    const int type = int(material->type);
    result.animated = type == 2 || type == 3;
    return result;
}

QVector3D terrainMaterialEmission(const SMaterial* material)
{
    if(!material || material->selfIllumination <= 0.0f)
        return QVector3D();
    const float strength = material->selfIllumination;
    return QVector3D(qBound(0.0f, material->R * strength, 1.0f),
                     qBound(0.0f, material->G * strength, 1.0f),
                     qBound(0.0f, material->B * strength, 1.0f));
}

float terrainMaterialOpacity(const SMaterial* material)
{
    return material ? qBound(0.0f, material->A, 1.0f) : 1.0f;
}

int sharedOverlayMaterialIndex(const QVector<qint16>& tileMaterialIndices,
                               int tileColumns,
                               int tileRows,
                               int gridX,
                               int gridY)
{
    if(tileColumns <= 0 || tileRows <= 0
       || tileMaterialIndices.size() < tileColumns * tileRows
       || gridX < 0 || gridY < 0
       || gridX > tileColumns * 2 || gridY > tileRows * 2)
        return -1;

    const int tileX = gridX / 2;
    const int tileY = gridY / 2;
    const bool onVerticalBoundary = (gridX & 1) == 0;
    const bool onHorizontalBoundary = (gridY & 1) == 0;
    int selected = -1;
    const auto tryTile = [&](int x, int y)
    {
        if(selected != -1 || x < 0 || x >= tileColumns
           || y < 0 || y >= tileRows)
            return;
        const int materialIndex = tileMaterialIndices.at(y * tileColumns + x);
        if(materialIndex != -1)
            selected = materialIndex;
    };

    if(onVerticalBoundary && onHorizontalBoundary)
    {
        tryTile(tileX - 1, tileY - 1);
        tryTile(tileX - 1, tileY);
        tryTile(tileX, tileY - 1);
        tryTile(tileX, tileY);
    }
    else if(onVerticalBoundary)
    {
        tryTile(tileX - 1, tileY);
        tryTile(tileX, tileY);
    }
    else if(onHorizontalBoundary)
    {
        tryTile(tileX, tileY - 1);
        tryTile(tileX, tileY);
    }
    else
        tryTile(tileX, tileY);
    return selected;
}

QVector3D terrainUnderlayEmission(const QVector3D& overlayEmission,
                                  float landHeight,
                                  float overlayHeight)
{
    return overlayHeight > landHeight ? overlayEmission : QVector3D();
}

SOverlayWindAnimation overlayWindAnimation(const QVector3D& authoredDirection,
                                            float authoredStrength,
                                            float elapsedSeconds,
                                            bool motionEnabled)
{
    SOverlayWindAnimation result;
    result.strength = qBound(0.0f, authoredStrength, 1.0f);
    result.enabled = motionEnabled && result.strength > 0.0f;
    if(!result.enabled)
        return result;

    QVector3D direction = authoredDirection;
    if(!direction.isNull())
        direction.normalize();
    const float waveLengthMultiplier = (kPi / 7.0f)
        * (qAsin(1.0f - result.strength) + kPi / 1.999f);
    result.direction = QVector2D(direction.x(), direction.y()) * waveLengthMultiplier;
    result.preciseLogicalTime = qMax(0.0f, elapsedSeconds) / kTerrainLogicalTickSeconds;
    const float logicalTick = qFloor(result.preciseLogicalTime);
    result.phase = 2.0f * kPi + logicalTick * result.strength * (-kPi / 21.0f);
    return result;
}

float overlayWindNoise(int gridX, int gridY)
{
    const int x = qBound(0, gridX, kNoiseSide - 1);
    const int y = qBound(0, gridY, kNoiseSide - 1);
    return previewNoise().at(y * kNoiseSide + x);
}

QVector3D overlayVertexDisplacement(const QVector3D& basePosition,
                                    const QVector2D& sectorGrid,
                                    float noise,
                                    const SOverlayMaterialAnimation& material,
                                    const SOverlayWindAnimation& wind)
{
    if(!wind.enabled || !material.animated || material.waveMultiplier == 0.0f)
        return QVector3D();

    const float amplitude = wind.strength * kTerrainMaximumWaveHeight
        * material.waveMultiplier;
    const float waveAngle = QVector2D::dotProduct(basePosition.toVector2D(), wind.direction)
        + wind.phase * material.waveMultiplier + noise;
    const float warpPhase = wind.preciseLogicalTime * kTerrainWarpSpeed;
    return QVector3D(
        tableSine(sectorGrid.x() * kPi * 0.25f + warpPhase) * amplitude * 3.0f,
        tableSine(sectorGrid.y() * kPi * 0.25f + warpPhase) * amplitude * 3.0f,
        tableSine(waveAngle) * amplitude * 0.25f);
}

}

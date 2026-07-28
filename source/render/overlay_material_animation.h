#ifndef OVERLAY_MATERIAL_ANIMATION_H
#define OVERLAY_MATERIAL_ANIMATION_H

#include <QVector2D>
#include <QVector3D>

#include "types.h"

namespace ei
{

constexpr float kTerrainLogicalTickSeconds = 0.055f;
constexpr float kTerrainMaximumWaveHeight = 0.3f;
constexpr float kTerrainWarpSpeed = 0.05f;

struct SOverlayMaterialAnimation
{
    float waveMultiplier{0.0f};
    float authoredWarpSpeed{0.0f};
    bool animated{false};
};

struct SOverlayWindAnimation
{
    QVector2D direction;
    float strength{0.0f};
    float phase{0.0f};
    float preciseLogicalTime{0.0f};
    bool enabled{false};
};

SOverlayMaterialAnimation overlayMaterialAnimation(const SMaterial* material);
QVector3D terrainMaterialEmission(const SMaterial* material);
SOverlayWindAnimation overlayWindAnimation(const QVector3D& authoredDirection,
                                            float authoredStrength,
                                            float elapsedSeconds,
                                            bool motionEnabled);
float overlayWindNoise(int gridX, int gridY);
QVector3D overlayVertexDisplacement(const QVector3D& basePosition,
                                    const QVector2D& sectorGrid,
                                    float noise,
                                    const SOverlayMaterialAnimation& material,
                                    const SOverlayWindAnimation& wind);

}

#endif

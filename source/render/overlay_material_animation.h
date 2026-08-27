#ifndef OVERLAY_MATERIAL_ANIMATION_H
#define OVERLAY_MATERIAL_ANIMATION_H

#include <QVector2D>
#include <QVector3D>
#include <QVector>

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

struct SOverlayTileAnimation
{
    int startSlot{0};
    int baseSlot{0};
    int frameCount{0};
    int startPhase{0};
    bool animated{false};
};

SOverlayMaterialAnimation overlayMaterialAnimation(const SMaterial* material);
SOverlayTileAnimation overlayTileAnimation(int atlasSlot,
                                           const QVector<SAnimTile>* animations,
                                           quint32 phaseSeed = 0u);
QVector3D terrainMaterialEmission(const SMaterial* material);
float terrainMaterialOpacity(const SMaterial* material);
int sharedOverlayMaterialIndex(const QVector<qint16>& tileMaterialIndices,
                               int tileColumns,
                               int tileRows,
                               int gridX,
                               int gridY);
QVector3D terrainUnderlayEmission(const QVector3D& overlayEmission,
                                  float landHeight,
                                  float overlayHeight);
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

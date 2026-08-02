#ifndef MAP_OBJECT_PLACEMENT_POLICY_H
#define MAP_OBJECT_PLACEMENT_POLICY_H

#include "types.h"

#include <QQuaternion>
#include <QVector3D>
#include <QtGlobal>
#include <cmath>

namespace ei
{

inline bool usesTerrainRelativeMapObjectHeight(ENodeType type)
{
    switch(type)
    {
    case eWorldObject:
    case eTorch:
    case eLever:
    case eMagicTrap:
        return true;
    default:
        return false;
    }
}

inline QVector3D resolveTerrainRelativeMapObjectPosition(
    ENodeType type, const QVector3D& storedPosition,
    const QVector3D& terrainSurfacePosition)
{
    // terrainSurfacePosition must contain the sampled surface height only.
    // The MOB Z is an offset from that surface and is applied exactly once.
    QVector3D resolved = terrainSurfacePosition;
    if(usesTerrainRelativeMapObjectHeight(type))
        resolved.setZ(terrainSurfacePosition.z() + storedPosition.z());
    return resolved;
}

inline bool contributesStaticNavigationCollision(ENodeType type)
{
    // Magic traps have no static figure collision in the runtime AI map. Units
    // are dynamic occupants and must not be baked into a regenerated graph.
    switch(type)
    {
    case eWorldObject:
    case eTorch:
    case eLever:
        return true;
    default:
        return false;
    }
}

inline int runtimeAiTileCoordinate(float worldCoordinate,
                                   float aiTileWorldSize = 0.5f)
{
    if(!std::isfinite(worldCoordinate) || !std::isfinite(aiTileWorldSize)
       || aiTileWorldSize <= 0.0f)
        return -1;
    return int(std::nearbyint(double(
        worldCoordinate / aiTileWorldSize - 0.5f)));
}

inline QVector3D runtimeNavigationRotateVector(
    const QQuaternion& rotation, const QVector3D& vector)
{
    // The runtime expands the stored quaternion directly into a 3x3 matrix.
    // MOB quaternions are not guaranteed to be unit length, so neither
    // normalizing nor QQuaternion::rotatedVector is equivalent here.
    const float x = rotation.x();
    const float y = rotation.y();
    const float z = rotation.z();
    const float w = rotation.scalar();
    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float wx2 = x2 * w;
    const float wy2 = y2 * w;
    const float wz2 = z2 * w;
    const float xx2 = x2 * x;
    const float xy2 = x2 * y;
    const float xz2 = z2 * x;
    const float yy2 = y2 * y;
    const float yz2 = z2 * y;
    const float zz2 = z2 * z;
    const float m00 = 1.0f - (zz2 + yy2);
    const float m01 = xy2 - wz2;
    const float m02 = xz2 + wy2;
    const float m10 = xy2 + wz2;
    const float m11 = 1.0f - (zz2 + xx2);
    const float m12 = yz2 - wx2;
    const float m20 = xz2 - wy2;
    const float m21 = yz2 + wx2;
    const float m22 = 1.0f - (yy2 + xx2);
    return QVector3D(
        float((double(m02) * double(vector.z())
               + double(m01) * double(vector.y()))
              + double(m00) * double(vector.x())),
        float((double(m12) * double(vector.z())
               + double(m10) * double(vector.x()))
              + double(m11) * double(vector.y())),
        float((double(m22) * double(vector.z())
               + double(m20) * double(vector.x()))
              + double(m21) * double(vector.y())));
}

inline quint8 runtimeNavigationUpperFaceMask(const QQuaternion& rotation)
{
    // Face ownership is decided from the six separately transformed canonical
    // normals. Deriving it from already transformed corners loses the sign on
    // nearly horizontal faces through cancellation, especially on long boxes.
    const QVector3D normals[] = {
        QVector3D(0.0f, 0.0f, -1.0f),
        QVector3D(-1.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 1.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 1.0f),
        QVector3D(1.0f, 0.0f, 0.0f),
        QVector3D(0.0f, -1.0f, 0.0f)
    };
    quint8 result = 0;
    for(int face = 0; face < 6; ++face)
        if(runtimeNavigationRotateVector(rotation, normals[face]).z() > 0.0f)
            result |= quint8(1u << face);
    return result;
}

inline void runtimeNavigationBounds(
    const QVector3D& center, const QVector3D& storedMinimum,
    const QVector3D& storedMaximum, const QVector3D& boneOffset,
    QVector3D* firstCorner, QVector3D* oppositeCorner)
{
    if(firstCorner)
        *firstCorner = center + storedMinimum + boneOffset;
    if(oppositeCorner)
        *oppositeCorner = center + storedMaximum + boneOffset;
    // Do not sort individual axes. Some interpolated FIG bounds are inverted
    // on one or two axes, and face ownership remains tied to their stored
    // orientation when the navigation raster walks the box.
}

inline float runtimeNavigationObjectAnchorShift(
    float storedObjectZ, float renderedObjectZ,
    float sampledNavigationHeight, float maximumTerrainHeight)
{
    if(!std::isfinite(storedObjectZ) || !std::isfinite(renderedObjectZ)
       || !std::isfinite(sampledNavigationHeight)
       || !std::isfinite(maximumTerrainHeight)
       || maximumTerrainHeight <= 0.0f)
        return 0.0f;
    const float scaled = sampledNavigationHeight * 511.0f
        / maximumTerrainHeight;
    const int quantized = qBound(0,
        int(std::nearbyint(double(scaled))), 511);
    const float runtimeSurface = float(quantized)
        * maximumTerrainHeight / 511.0f;
    return runtimeSurface + storedObjectZ - renderedObjectZ;
}

}

#endif // MAP_OBJECT_PLACEMENT_POLICY_H

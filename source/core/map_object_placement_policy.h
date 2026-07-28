#ifndef MAP_OBJECT_PLACEMENT_POLICY_H
#define MAP_OBJECT_PLACEMENT_POLICY_H

#include "types.h"

#include <QVector3D>

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
    const QVector3D& terrainProjectedPosition)
{
    QVector3D resolved = terrainProjectedPosition;
    if(usesTerrainRelativeMapObjectHeight(type))
        resolved.setZ(terrainProjectedPosition.z() + storedPosition.z());
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

}

#endif // MAP_OBJECT_PLACEMENT_POLICY_H

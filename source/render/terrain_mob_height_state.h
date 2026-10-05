#ifndef TERRAIN_MOB_HEIGHT_STATE_H
#define TERRAIN_MOB_HEIGHT_STATE_H

#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>

class CNode;
class CMob;

// Editor history only. MOB Z is an offset from terrain, so preserving world
// height requires a real stored-Z edit paired with the terrain command.
struct STerrainMobHeightEntry
{
    QPointer<CNode> node;
    CMob* mob{nullptr};
    float beforeOffset{0};
    float afterOffset{0};
    float beforeTerrainHeight{0};
};

struct STerrainMobHeightState
{
    QString terrainPath;
    QVector<STerrainMobHeightEntry> entries;
    QSet<CNode*> captured;
};

#endif

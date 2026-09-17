#ifndef NEIGHBOR_SCENE_DATA_H
#define NEIGHBOR_SCENE_DATA_H
#include <QMap>
#include <QJsonObject>
#include <QVector>
#include <QStringList>
#include "neighbor_map_layout.h"
class CMobDocument;
struct SNeighborSceneData
{
    SNeighborMapPlacement placement;
    QMap<QString, QByteArray> terrainMembers;
    QVector<QJsonObject> staticObjects;
    QStringList diagnostics;
    bool valid{false};
};
// Pure CPU snapshots: no QObject, singleton resource cache or GL access.
QVector<QJsonObject> neighborStaticObjects(const CMobDocument& document);
SNeighborSceneData prepareNeighborScene(const SNeighborMapPlacement& placement);
#endif

#ifndef NEIGHBOR_VISUAL_PREPARATION_H
#define NEIGHBOR_VISUAL_PREPARATION_H
#include "neighbor_scene_data.h"
#include <functional>
void prepareNeighborVisuals(SNeighborSceneData& scene, const QStringList& archives,
                           const std::function<bool()>& current);
#endif

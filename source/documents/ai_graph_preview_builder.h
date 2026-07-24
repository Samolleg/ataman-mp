#ifndef AI_GRAPH_PREVIEW_BUILDER_H
#define AI_GRAPH_PREVIEW_BUILDER_H

#include <QVector>
#include <QVector3D>

#include "ai_graph_document.h"

struct SAiGraphCollisionProxy
{
    QVector3D minimum;
    QVector3D maximum;
    quint32 objectId{0};
};

struct SAiGraphPreviewBuildInput
{
    int graphWidth{0};
    int graphHeight{0};
    QVector<float> terrainHeights;
    QVector<SAiGraphCollisionProxy> collisionProxies;
};

struct SAiGraphPreviewBuildResult
{
    bool valid{false};
    QString error;
    CAiGraphDocument document;
    int blockedAiTiles{0};
    int collisionProxyCount{0};
};

class CAiGraphPreviewBuilder
{
public:
    static constexpr int AiTilesPerGraphCell = 8;
    static constexpr float AiTileWorldSize = 0.5f;

    static SAiGraphPreviewBuildResult build(const SAiGraphPreviewBuildInput& input);
};

#endif // AI_GRAPH_PREVIEW_BUILDER_H

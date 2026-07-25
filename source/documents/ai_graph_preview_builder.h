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

struct SAiTerrainTraversal
{
    int speed{1024};
    int cost{1};
};

struct SAiGraphPreviewBuildInput
{
    int graphWidth{0};
    int graphHeight{0};
    QVector<float> terrainHeights;
    QVector<float> waterDepths;
    QVector<quint8> terrainTypes;
    QVector<SAiTerrainTraversal> terrainTraversal;
    QVector<SAiGraphCollisionProxy> collisionProxies;
    bool collisionMetadataApproximate{false};
};

struct SAiGraphPreviewBuildResult
{
    bool valid{false};
    QString error;
    CAiGraphDocument document;
    int blockedAiTiles{0};
    int collisionProxyCount{0};
    int waterAiTiles{0};
    QStringList warnings;
};

class CAiGraphPreviewBuilder
{
public:
    static constexpr int AiTilesPerGraphCell = 8;
    static constexpr float AiTileWorldSize = 0.5f;

    static SAiGraphPreviewBuildResult build(const SAiGraphPreviewBuildInput& input);
    static QVector<SAiTerrainTraversal> parseTerrainTraversalRegistry(
        const QByteArray& registry, QString* error = nullptr);
};

#endif // AI_GRAPH_PREVIEW_BUILDER_H

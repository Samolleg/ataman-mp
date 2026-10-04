#ifndef AI_GRAPH_PREVIEW_BUILDER_H
#define AI_GRAPH_PREVIEW_BUILDER_H

#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QPointF>

#include "ai_graph_document.h"

enum class EAiGraphObjectPassability : quint8
{
    Blocking,
    Hard,
    Easy,
    Danger,
    Bridge,
    Ignored
};

struct SAiGraphCollisionProxy
{
    QVector<QVector2D> footprint;
    QVector<QVector3D> boxCorners;
    quint8 upperFaceMask{0};
    float minimumZ{0.0f};
    float maximumZ{0.0f};
    EAiGraphObjectPassability passability{EAiGraphObjectPassability::Blocking};
    bool authored{false};
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
    float maximumTerrainHeight{20.0f};
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
    int authoredCollisionProxyCount{0};
    int bridgeAiTiles{0};
    int waterAiTiles{0};
    qint64 collisionCoverageTests{0};
    qint64 routeQueueSettledTiles{0};
    qint64 routeSearchEarlyExits{0};
    qint64 buildMilliseconds{0};
    qint64 collisionRasterMilliseconds{0};
    qint64 routeBuildMilliseconds{0};
    QVector<float> navigationSurfaceHeights;
    QVector<QVector<quint8>> layerPassability;
    QVector<quint8> humanPassability;
    QVector<quint8> bridgeMask;
    QStringList warnings;

    bool projectHumanPosition(const QPointF& requested, QVector3D* projected,
                              float maximumDistance = 8.0f) const;
};

namespace ai_graph_internal {
QVector<bool> graphCellCrossingCandidates(
    const QVector<quint8>& quality, const QVector<int>& traversalEdgeCosts,
    int aiWidth, int aiHeight, int graphX, int graphY);
int elevationMultiplier(int heightDelta, float maximumTerrainHeight, bool flying);
bool collisionSampleTakesOwnership(
    bool hadPending, EAiGraphObjectPassability current,
    bool candidateHasMaximum, EAiGraphObjectPassability candidate);
}

class CAiGraphPreviewBuilder
{
public:
    static constexpr int AiTilesPerGraphCell = 8;
    static constexpr float AiTileWorldSize = 0.5f;

    static SAiGraphPreviewBuildResult build(const SAiGraphPreviewBuildInput& input);
    // Restore display Z for a loaded graph without regenerating its routes.
    static QVector<float> navigationSurfaceHeights(
        const SAiGraphPreviewBuildInput& input, QString* error = nullptr);
    static int quantizedWaterDepth(float worldDepth, float maximumTerrainHeight);
    static float navigationWaterDepth(float landHeight, float overlayHeight,
                                      bool overlayActive, int overlayMaterialType);
    static QVector<SAiTerrainTraversal> parseTerrainTraversalRegistry(
        const QByteArray& registry, QString* error = nullptr);
private:
    static SAiGraphPreviewBuildResult buildInternal(
        const SAiGraphPreviewBuildInput& input, bool surfacesOnly);
};

#endif // AI_GRAPH_PREVIEW_BUILDER_H

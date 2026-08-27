#ifndef RENDER_PERFORMANCE_H
#define RENDER_PERFORMANCE_H

#include <QElapsedTimer>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QtGlobal>

struct SRenderFrameCounters
{
    quint64 nodesVisited{0};
    quint64 nodesCulled{0};
    quint64 nodesSubmitted{0};
    quint64 logicSubmissions{0};
    quint64 drawCalls{0};
    quint64 triangles{0};
    quint64 textureBinds{0};
    quint64 terrainDrawCalls{0};
    quint64 terrainSectorsTested{0};
    quint64 terrainSectorsCulled{0};
    quint64 terrainSectorsVisible{0};
    quint64 spatialCellsTested{0};
    quint64 spatialCellsVisible{0};
    quint64 spatialCandidates{0};
    quint64 spatialOverflowEntries{0};
    quint64 opaqueQueueSize{0};
    quint64 cutoutQueueSize{0};
    quint64 transparentQueueSize{0};
    quint64 batches{0};
    quint64 instances{0};
    quint64 instancedDrawCalls{0};
    quint64 sharedMeshes{0};
    quint64 gpuMemoryEstimateBytes{0};
    quint64 animationPoseEvaluations{0};
    quint64 animationPoseCacheHits{0};
    quint64 animationComponentsEvaluated{0};
    quint64 queueCollectNanoseconds{0};
    quint64 queueSortNanoseconds{0};
    quint64 queueDrawNanoseconds{0};
    quint64 shadowCandidates{0};
    quint64 shadowItems{0};
    quint64 shadowPasses{0};
    quint64 shadowStaticRebuilds{0};
    quint64 shadowDynamicRebuilds{0};
    quint64 shadowNanoseconds{0};
    quint64 terrainShadowFieldRebuilds{0};
    quint64 terrainShadowFieldNodes{0};
    quint64 terrainShadowFieldSamples{0};
    quint64 terrainShadowNanoseconds{0};
};

struct SRenderFrameMetadata
{
    int viewportWidth{0};
    int viewportHeight{0};
    QVector3D cameraPosition;
    QVector3D cameraPivot;
    QVector3D cameraRotation;
    QString profileId;
    QString mapName;
    QString contentFingerprint;
    bool benchmarkMode{false};
    bool vsyncEnabled{true};
    int frameRateCap{0};
    bool animationEnabled{true};
    bool landEnabled{true};
    bool waterEnabled{true};
    bool logicEnabled{false};
    bool helpEnabled{false};
    bool frustumCullingEnabled{true};
    int shadowQuality{0};
    int shadowAtlasResolution{0};
    bool terrainShadowsEnabled{false};
};

class RenderPerformanceContext
{
public:
    static RenderPerformanceContext& instance();
    void beginFrame(bool enabled, bool reportEnabled = true);
    void recordGpuTime(quint64 nanoseconds);
    void setFrameMetadata(const SRenderFrameMetadata& metadata) { m_metadata = metadata; }
    void endFrame();
    bool collecting() const { return m_collecting; }
    SRenderFrameCounters& counters() { return m_frame; }
    const SRenderFrameCounters& currentFrameCounters() const { return m_frame; }

private:
    void resetWindow();

    bool m_collecting{false};
    bool m_reportEnabled{false};
    SRenderFrameCounters m_frame;
    SRenderFrameCounters m_total;
    QElapsedTimer m_frameTimer;
    QElapsedTimer m_windowTimer;
    quint64 m_cpuNanoseconds{0};
    quint64 m_maxCpuNanoseconds{0};
    quint64 m_gpuNanoseconds{0};
    quint64 m_gpuSamples{0};
    quint64 m_frames{0};
    QVector<quint64> m_cpuSamples;
    QVector<quint64> m_gpuSamplesNs;
    SRenderFrameMetadata m_metadata;
};

#endif

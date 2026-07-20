#ifndef RENDER_PERFORMANCE_H
#define RENDER_PERFORMANCE_H

#include <QElapsedTimer>
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
};

class RenderPerformanceContext
{
public:
    static RenderPerformanceContext& instance();
    void beginFrame(bool enabled);
    void recordGpuTime(quint64 nanoseconds);
    void endFrame();
    bool collecting() const { return m_collecting; }
    SRenderFrameCounters& counters() { return m_frame; }

private:
    void resetWindow();

    bool m_collecting{false};
    SRenderFrameCounters m_frame;
    SRenderFrameCounters m_total;
    QElapsedTimer m_frameTimer;
    QElapsedTimer m_windowTimer;
    quint64 m_cpuNanoseconds{0};
    quint64 m_maxCpuNanoseconds{0};
    quint64 m_gpuNanoseconds{0};
    quint64 m_gpuSamples{0};
    quint64 m_frames{0};
};

#endif

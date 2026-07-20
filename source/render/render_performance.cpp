#include "render_performance.h"

#include "log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {
void addCounters(SRenderFrameCounters& target, const SRenderFrameCounters& value)
{
    target.nodesVisited += value.nodesVisited;
    target.nodesCulled += value.nodesCulled;
    target.nodesSubmitted += value.nodesSubmitted;
    target.logicSubmissions += value.logicSubmissions;
    target.drawCalls += value.drawCalls;
    target.triangles += value.triangles;
    target.textureBinds += value.textureBinds;
    target.terrainDrawCalls += value.terrainDrawCalls;
}
}

RenderPerformanceContext& RenderPerformanceContext::instance()
{
    static RenderPerformanceContext context;
    return context;
}

void RenderPerformanceContext::beginFrame(bool enabled)
{
    m_collecting = enabled;
    m_frame = SRenderFrameCounters();
    if(!enabled)
    {
        if(m_windowTimer.isValid())
            resetWindow();
        return;
    }
    if(!m_windowTimer.isValid())
        m_windowTimer.start();
    m_frameTimer.start();
}

void RenderPerformanceContext::recordGpuTime(quint64 nanoseconds)
{
    if(!m_collecting)
        return;
    m_gpuNanoseconds += nanoseconds;
    ++m_gpuSamples;
}

void RenderPerformanceContext::endFrame()
{
    if(!m_collecting)
        return;
    const quint64 cpuNanoseconds = quint64(m_frameTimer.nsecsElapsed());
    m_cpuNanoseconds += cpuNanoseconds;
    m_maxCpuNanoseconds = qMax(m_maxCpuNanoseconds, cpuNanoseconds);
    addCounters(m_total, m_frame);
    ++m_frames;
    m_collecting = false;

    const qint64 elapsedMs = m_windowTimer.elapsed();
    if(elapsedMs < 2000 || m_frames == 0)
        return;
    const double frames = double(m_frames);
    const double fps = frames * 1000.0 / double(elapsedMs);
    const double cpuMs = double(m_cpuNanoseconds) / frames / 1000000.0;
    const double maxCpuMs = double(m_maxCpuNanoseconds) / 1000000.0;
    const QString gpu = m_gpuSamples == 0 ? QString("unsupported/pending")
        : QString::number(double(m_gpuNanoseconds) / double(m_gpuSamples) / 1000000.0, 'f', 2) + " ms";
    const QString report = QString(
        "Render stats fps:%1 cpu-submit:%2 ms max:%3 ms gpu:%4 "
        "nodes:%5 culled:%6 submitted:%7 logic:%8 draws:%9 terrain:%10 triangles:%11 texture-binds:%12")
        .arg(fps, 0, 'f', 1).arg(cpuMs, 0, 'f', 2).arg(maxCpuMs, 0, 'f', 2).arg(gpu)
        .arg(double(m_total.nodesVisited) / frames, 0, 'f', 1)
        .arg(double(m_total.nodesCulled) / frames, 0, 'f', 1)
        .arg(double(m_total.nodesSubmitted) / frames, 0, 'f', 1)
        .arg(double(m_total.logicSubmissions) / frames, 0, 'f', 1)
        .arg(double(m_total.drawCalls) / frames, 0, 'f', 1)
        .arg(double(m_total.terrainDrawCalls) / frames, 0, 'f', 1)
        .arg(double(m_total.triangles) / frames, 0, 'f', 0)
        .arg(double(m_total.textureBinds) / frames, 0, 'f', 1);
    ei::log(eLogInfo, report);

    // Performance captures need to survive ordinary workflow-log filtering so
    // users can attach one small, unambiguous file after a benchmark run.
    QDir current = QDir::current();
    current.mkpath("logs");
    QFile performanceLog(current.filePath("logs/render-performance.log"));
    if(performanceLog.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        QTextStream stream(&performanceLog);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
               << ' ' << report << '\n';
    }
    resetWindow();
}

void RenderPerformanceContext::resetWindow()
{
    m_total = SRenderFrameCounters();
    m_cpuNanoseconds = 0;
    m_maxCpuNanoseconds = 0;
    m_gpuNanoseconds = 0;
    m_gpuSamples = 0;
    m_frames = 0;
    m_windowTimer.invalidate();
}

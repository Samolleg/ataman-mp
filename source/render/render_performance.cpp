#include "render_performance.h"

#include "log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QtMath>

#include <algorithm>

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
    target.terrainSectorsTested += value.terrainSectorsTested;
    target.terrainSectorsCulled += value.terrainSectorsCulled;
    target.terrainSectorsVisible += value.terrainSectorsVisible;
    target.spatialCellsTested += value.spatialCellsTested;
    target.spatialCellsVisible += value.spatialCellsVisible;
    target.spatialCandidates += value.spatialCandidates;
    target.spatialOverflowEntries += value.spatialOverflowEntries;
    target.opaqueQueueSize += value.opaqueQueueSize;
    target.cutoutQueueSize += value.cutoutQueueSize;
    target.transparentQueueSize += value.transparentQueueSize;
    target.batches += value.batches;
    target.instances += value.instances;
    target.sharedMeshes += value.sharedMeshes;
    target.gpuMemoryEstimateBytes += value.gpuMemoryEstimateBytes;
    target.animationPoseEvaluations += value.animationPoseEvaluations;
    target.animationPoseCacheHits += value.animationPoseCacheHits;
    target.animationComponentsEvaluated += value.animationComponentsEvaluated;
}

double percentileMilliseconds(QVector<quint64> samples, double percentile)
{
    if(samples.isEmpty())
        return 0.0;
    std::sort(samples.begin(), samples.end());
    const int index = qBound(0, int(qCeil(percentile * samples.size())) - 1,
                             samples.size() - 1);
    return double(samples.at(index)) / 1000000.0;
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
    m_gpuSamplesNs.append(nanoseconds);
}

void RenderPerformanceContext::endFrame()
{
    if(!m_collecting)
        return;
    const quint64 cpuNanoseconds = quint64(m_frameTimer.nsecsElapsed());
    m_cpuNanoseconds += cpuNanoseconds;
    m_maxCpuNanoseconds = qMax(m_maxCpuNanoseconds, cpuNanoseconds);
    m_cpuSamples.append(cpuNanoseconds);
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
    const double cpuP95Ms = percentileMilliseconds(m_cpuSamples, 0.95);
    const double gpuAverageMs = m_gpuSamples == 0 ? 0.0
        : double(m_gpuNanoseconds) / double(m_gpuSamples) / 1000000.0;
    const double gpuP95Ms = percentileMilliseconds(m_gpuSamplesNs, 0.95);
    const QString gpu = m_gpuSamples == 0 ? QString("unsupported/pending")
        : QString("%1 ms p95:%2 ms").arg(gpuAverageMs, 0, 'f', 2).arg(gpuP95Ms, 0, 'f', 2);
    const QString report = QString(
        "Render stats mode:%1 viewport:%2x%3 camera:%4,%5,%6 profile:%7 map:%8 content:%9 "
        "fps:%10 cpu-submit:%11 ms p95:%12 ms max:%13 ms gpu:%14 "
        "nodes:%15 culled:%16 submitted:%17 logic:%18 cells:%19/%20 candidates:%21 overflow:%22 "
        "draws:%23 terrain:%24 sectors:%25/%26/%27 triangles:%28 texture-binds:%29 "
        "queues:%30/%31/%32 batches:%33 instances:%34 shared-meshes:%35 gpu-memory:%36 "
        "animation-eval/cache/components:%37/%38/%39")
        .arg(m_metadata.benchmarkMode ? "benchmark" : "vsync")
        .arg(m_metadata.viewportWidth).arg(m_metadata.viewportHeight)
        .arg(m_metadata.cameraPosition.x(), 0, 'f', 2)
        .arg(m_metadata.cameraPosition.y(), 0, 'f', 2)
        .arg(m_metadata.cameraPosition.z(), 0, 'f', 2)
        .arg(m_metadata.profileId).arg(m_metadata.mapName).arg(m_metadata.contentFingerprint)
        .arg(fps, 0, 'f', 1).arg(cpuMs, 0, 'f', 2).arg(cpuP95Ms, 0, 'f', 2)
        .arg(maxCpuMs, 0, 'f', 2).arg(gpu)
        .arg(double(m_total.nodesVisited) / frames, 0, 'f', 1)
        .arg(double(m_total.nodesCulled) / frames, 0, 'f', 1)
        .arg(double(m_total.nodesSubmitted) / frames, 0, 'f', 1)
        .arg(double(m_total.logicSubmissions) / frames, 0, 'f', 1)
        .arg(double(m_total.spatialCellsVisible) / frames, 0, 'f', 1)
        .arg(double(m_total.spatialCellsTested) / frames, 0, 'f', 1)
        .arg(double(m_total.spatialCandidates) / frames, 0, 'f', 1)
        .arg(double(m_total.spatialOverflowEntries) / frames, 0, 'f', 1)
        .arg(double(m_total.drawCalls) / frames, 0, 'f', 1)
        .arg(double(m_total.terrainDrawCalls) / frames, 0, 'f', 1)
        .arg(double(m_total.terrainSectorsVisible) / frames, 0, 'f', 1)
        .arg(double(m_total.terrainSectorsCulled) / frames, 0, 'f', 1)
        .arg(double(m_total.terrainSectorsTested) / frames, 0, 'f', 1)
        .arg(double(m_total.triangles) / frames, 0, 'f', 0)
        .arg(double(m_total.textureBinds) / frames, 0, 'f', 1)
        .arg(double(m_total.opaqueQueueSize) / frames, 0, 'f', 1)
        .arg(double(m_total.cutoutQueueSize) / frames, 0, 'f', 1)
        .arg(double(m_total.transparentQueueSize) / frames, 0, 'f', 1)
        .arg(double(m_total.batches) / frames, 0, 'f', 1)
        .arg(double(m_total.instances) / frames, 0, 'f', 1)
        .arg(double(m_total.sharedMeshes) / frames, 0, 'f', 1)
        .arg(double(m_total.gpuMemoryEstimateBytes) / frames, 0, 'f', 0)
        .arg(double(m_total.animationPoseEvaluations) / frames, 0, 'f', 1)
        .arg(double(m_total.animationPoseCacheHits) / frames, 0, 'f', 1)
        .arg(double(m_total.animationComponentsEvaluated) / frames, 0, 'f', 1);
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
    if(m_metadata.benchmarkMode)
    {
        QJsonObject definition;
        definition.insert("profile", m_metadata.profileId);
        definition.insert("map", m_metadata.mapName);
        definition.insert("contentFingerprint", m_metadata.contentFingerprint);
        definition.insert("viewportWidth", m_metadata.viewportWidth);
        definition.insert("viewportHeight", m_metadata.viewportHeight);
        definition.insert("camera", QJsonArray({m_metadata.cameraPosition.x(),
            m_metadata.cameraPosition.y(), m_metadata.cameraPosition.z()}));
        definition.insert("cameraPivot", QJsonArray({m_metadata.cameraPivot.x(),
            m_metadata.cameraPivot.y(), m_metadata.cameraPivot.z()}));
        definition.insert("cameraRotation", QJsonArray({m_metadata.cameraRotation.x(),
            m_metadata.cameraRotation.y(), m_metadata.cameraRotation.z()}));
        QJsonObject averages;
        averages.insert("visibleTerrainSectors", double(m_total.terrainSectorsVisible) / frames);
        averages.insert("spatialCandidates", double(m_total.spatialCandidates) / frames);
        averages.insert("spatialOverflowEntries", double(m_total.spatialOverflowEntries) / frames);
        averages.insert("drawCalls", double(m_total.drawCalls) / frames);
        averages.insert("opaqueQueueSize", double(m_total.opaqueQueueSize) / frames);
        averages.insert("cutoutQueueSize", double(m_total.cutoutQueueSize) / frames);
        averages.insert("transparentQueueSize", double(m_total.transparentQueueSize) / frames);
        averages.insert("batches", double(m_total.batches) / frames);
        averages.insert("instances", double(m_total.instances) / frames);
        averages.insert("sharedMeshes", double(m_total.sharedMeshes) / frames);
        averages.insert("gpuMemoryEstimateBytes", double(m_total.gpuMemoryEstimateBytes) / frames);
        averages.insert("animationPoseEvaluations", double(m_total.animationPoseEvaluations) / frames);
        averages.insert("animationPoseCacheHits", double(m_total.animationPoseCacheHits) / frames);
        averages.insert("animationComponentsEvaluated", double(m_total.animationComponentsEvaluated) / frames);
        definition.insert("averages", averages);
        QJsonObject toggles;
        toggles.insert("animation", m_metadata.animationEnabled);
        toggles.insert("land", m_metadata.landEnabled);
        toggles.insert("water", m_metadata.waterEnabled);
        toggles.insert("logic", m_metadata.logicEnabled);
        toggles.insert("help", m_metadata.helpEnabled);
        toggles.insert("frustumCulling", m_metadata.frustumCullingEnabled);
        definition.insert("renderToggles", toggles);
        definition.insert("fps", fps);
        definition.insert("cpuAverageMs", cpuMs);
        definition.insert("cpuP95Ms", cpuP95Ms);
        definition.insert("gpuAverageMs", gpuAverageMs);
        definition.insert("gpuP95Ms", gpuP95Ms);
        definition.insert("capturedAt", QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        QSaveFile definitionFile(current.filePath("logs/render-benchmark.json"));
        if(definitionFile.open(QIODevice::WriteOnly))
        {
            definitionFile.write(QJsonDocument(definition).toJson(QJsonDocument::Indented));
            definitionFile.commit();
        }
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
    m_cpuSamples.clear();
    m_gpuSamplesNs.clear();
    m_windowTimer.invalidate();
}

#include "frame_presentation_telemetry.h"

CFramePresentationHistory::CFramePresentationHistory(int capacity)
    : m_capacity(qMax(1, capacity))
{
    m_frames.reserve(m_capacity);
}

void CFramePresentationHistory::arm()
{
    m_frames.clear();
    m_nextWrite = 0;
    m_armed = true;
}

void CFramePresentationHistory::disarm()
{
    m_armed = false;
    m_frames.clear();
    m_nextWrite = 0;
}

bool CFramePresentationHistory::append(const SFramePresentationTelemetry& frame)
{
    if(!m_armed)
        return false;
    if(m_frames.size() < m_capacity)
    {
        m_frames.append(frame);
        m_nextWrite = m_frames.size() % m_capacity;
    }
    else
    {
        m_frames[m_nextWrite] = frame;
        m_nextWrite = (m_nextWrite + 1) % m_capacity;
    }
    return true;
}

SFramePresentationMarkerSnapshot CFramePresentationHistory::snapshot()
{
    SFramePresentationMarkerSnapshot result;
    if(!m_armed || m_frames.isEmpty())
        return result;

    result.markerId = m_nextMarkerId++;
    result.frames.reserve(m_frames.size());
    if(m_frames.size() < m_capacity)
        result.frames = m_frames;
    else
        for(int offset = 0; offset < m_frames.size(); ++offset)
            result.frames.append(m_frames.at((m_nextWrite + offset) % m_capacity));
    return result;
}

EFramePresentationDiagnosis classifyFramePresentation(
    const SFramePresentationTelemetry& frame)
{
    if(!frame.sceneExpected)
        return EFramePresentationDiagnosis::NoSceneExpected;
    if(!frame.contextCurrent || !frame.surfaceValid || !frame.framebufferComplete
       || !frame.resourcesValid || !frame.cameraFinite)
        return EFramePresentationDiagnosis::InvalidContextFramebufferOrResources;
    if(!frame.defaultFramebufferBound
       || frame.framebufferAfterBind != frame.defaultFramebuffer)
        return EFramePresentationDiagnosis::WrongPresentationTarget;
    if(frame.fullEnvironment && frame.shadowsEnabled)
    {
        for(const SShadowFramebufferPassTelemetry& pass : frame.shadowPasses)
            if(pass.attempted
               && (!pass.bindSucceeded
                   || pass.framebufferAfterBind != pass.framebufferId
                   || !pass.releaseSucceeded
                   || !pass.bindDefaultSucceeded
                   || pass.framebufferAfterRestore != frame.defaultFramebuffer))
                return EFramePresentationDiagnosis::WrongPresentationTarget;
    }
    const int colorTargets[] = {
        frame.framebufferBeforeTerrainColor,
        frame.framebufferBeforeObjectFlush,
        frame.framebufferBeforeWaterOverlay,
        frame.framebufferAtPaintReturn
    };
    for(int target : colorTargets)
        if(target >= 0 && target != frame.defaultFramebuffer)
            return EFramePresentationDiagnosis::WrongPresentationTarget;
    if(!frame.landscapeVisible && !frame.overlaysVisible
       && !frame.staticObjectsVisible && !frame.dynamicObjectsVisible)
        return EFramePresentationDiagnosis::AllWorldVisibilityDisabled;

    // Total draw calls also contain sky and private shadow-atlas work. Only
    // color-pass terrain or object submissions prove that world geometry was
    // sent to the presentation target after its clear.
    const bool submitted = frame.terrainDrawCalls > 0
        || frame.nodesSubmitted > 0;
    if(submitted)
        return EFramePresentationDiagnosis::SceneSubmittedToPresentationTarget;
    if(frame.asyncUploadOrRebuildActive)
        return EFramePresentationDiagnosis::AsyncUploadOrRebuildWindow;

    const bool nodesEntirelyCulled = frame.nodesVisited > 0
        && frame.nodesCulled >= frame.nodesVisited;
    const bool terrainEntirelyCulled = frame.terrainSectorsTested > 0
        && frame.terrainSectorsCulled >= frame.terrainSectorsTested
        && frame.terrainSectorsVisible == 0;
    if(frame.frustumCullingEnabled
       && (nodesEntirelyCulled || terrainEntirelyCulled))
        return EFramePresentationDiagnosis::WholeSceneFrustumRejected;
    return EFramePresentationDiagnosis::ClearedWithoutSceneDraw;
}

QString framePresentationDiagnosisName(EFramePresentationDiagnosis diagnosis)
{
    switch(diagnosis)
    {
    case EFramePresentationDiagnosis::NoSceneExpected:
        return QStringLiteral("no-scene-expected");
    case EFramePresentationDiagnosis::SceneSubmittedToPresentationTarget:
        return QStringLiteral("scene-submitted-to-presentation-target");
    case EFramePresentationDiagnosis::ClearedWithoutSceneDraw:
        return QStringLiteral("cleared-without-scene-draw");
    case EFramePresentationDiagnosis::WrongPresentationTarget:
        return QStringLiteral("wrong-presentation-target");
    case EFramePresentationDiagnosis::AllWorldVisibilityDisabled:
        return QStringLiteral("all-world-visibility-disabled");
    case EFramePresentationDiagnosis::InvalidContextFramebufferOrResources:
        return QStringLiteral("invalid-context-framebuffer-or-resources");
    case EFramePresentationDiagnosis::WholeSceneFrustumRejected:
        return QStringLiteral("whole-scene-frustum-rejected");
    case EFramePresentationDiagnosis::AsyncUploadOrRebuildWindow:
        return QStringLiteral("async-upload-or-rebuild-window");
    }
    return QStringLiteral("unknown");
}

QString formatFramePresentationTelemetry(const SFramePresentationTelemetry& frame)
{
    QString result = QStringLiteral(
        "Scene frame telemetry id:%1 elapsed-ms:%2 diagnosis:%3 expected:%4 "
        "context:%5 surface:%6 fbo:%7/%8/%9 status:0x%10 complete:%11 "
        "resources:%12 visibility:L%13/O%14/S%15/D%16 camera:%17 "
        "culling:%18 async:%19 draws:%20 terrain-draws:%21 "
        "nodes:%22/%23/%24 sectors:%25/%26/%27")
        .arg(frame.frameId)
        .arg(frame.capturedMilliseconds)
        .arg(framePresentationDiagnosisName(classifyFramePresentation(frame)))
        .arg(frame.sceneExpected ? 1 : 0)
        .arg(frame.contextCurrent ? 1 : 0)
        .arg(frame.surfaceValid ? 1 : 0)
        .arg(frame.framebufferBeforeBind)
        .arg(frame.defaultFramebuffer)
        .arg(frame.framebufferAfterBind)
        .arg(QString::number(frame.framebufferStatus, 16))
        .arg(frame.framebufferComplete ? 1 : 0)
        .arg(frame.resourcesValid ? 1 : 0)
        .arg(frame.landscapeVisible ? 1 : 0)
        .arg(frame.overlaysVisible ? 1 : 0)
        .arg(frame.staticObjectsVisible ? 1 : 0)
        .arg(frame.dynamicObjectsVisible ? 1 : 0)
        .arg(frame.cameraFinite ? 1 : 0)
        .arg(frame.frustumCullingEnabled ? 1 : 0)
        .arg(frame.asyncUploadOrRebuildActive ? 1 : 0)
        .arg(frame.sceneDrawCalls)
        .arg(frame.terrainDrawCalls)
        .arg(frame.nodesVisited).arg(frame.nodesCulled).arg(frame.nodesSubmitted)
        .arg(frame.terrainSectorsTested).arg(frame.terrainSectorsCulled)
        .arg(frame.terrainSectorsVisible);
    const auto passText = [&frame](const QString& name,
                                  EShadowFramebufferPass pass)
    {
        const SShadowFramebufferPassTelemetry& sample =
            frame.shadowPasses[shadowFramebufferPassIndex(pass)];
        return QStringLiteral("%1:%2/%3/%4/%5/%6/%7")
            .arg(name).arg(sample.framebufferId)
            .arg(sample.bindSucceeded ? 1 : 0)
            .arg(sample.framebufferAfterBind)
            .arg(sample.releaseSucceeded ? 1 : 0)
            .arg(sample.bindDefaultSucceeded ? 1 : 0)
            .arg(sample.framebufferAfterRestore);
    };
    result += QStringLiteral(
        " full-environment:%1 shadows:%2 shadow-rebuild:0x%3 "
        "shadow-fbos:%4/%5/%6/%7/%8 shadow-passes:[%9 %10 %11 %12 %13] "
        "color-targets:%14/%15/%16/%17")
        .arg(frame.fullEnvironment ? 1 : 0)
        .arg(frame.shadowsEnabled ? 1 : 0)
        .arg(QString::number(frame.shadowRebuildMask, 16))
        .arg(frame.shadowFramebufferIds[0])
        .arg(frame.shadowFramebufferIds[1])
        .arg(frame.shadowFramebufferIds[2])
        .arg(frame.shadowFramebufferIds[3])
        .arg(frame.shadowFramebufferIds[4])
        .arg(passText(QStringLiteral("static-near"),
                      EShadowFramebufferPass::StaticNear))
        .arg(passText(QStringLiteral("static-far"),
                      EShadowFramebufferPass::StaticFar))
        .arg(passText(QStringLiteral("dynamic-near"),
                      EShadowFramebufferPass::DynamicNear))
        .arg(passText(QStringLiteral("dynamic-far"),
                      EShadowFramebufferPass::DynamicFar))
        .arg(passText(QStringLiteral("local"),
                      EShadowFramebufferPass::Local))
        .arg(frame.framebufferBeforeTerrainColor)
        .arg(frame.framebufferBeforeObjectFlush)
        .arg(frame.framebufferBeforeWaterOverlay)
        .arg(frame.framebufferAtPaintReturn);
    return result;
}

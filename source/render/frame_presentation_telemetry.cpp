#include "frame_presentation_telemetry.h"

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
    return QStringLiteral(
        "Scene frame telemetry id:%1 diagnosis:%2 expected:%3 "
        "context:%4 surface:%5 fbo:%6/%7/%8 status:0x%9 complete:%10 "
        "resources:%11 visibility:L%12/O%13/S%14/D%15 camera:%16 "
        "culling:%17 async:%18 draws:%19 terrain-draws:%20 "
        "nodes:%21/%22/%23 sectors:%24/%25/%26")
        .arg(frame.frameId)
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
}

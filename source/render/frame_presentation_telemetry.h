#ifndef FRAME_PRESENTATION_TELEMETRY_H
#define FRAME_PRESENTATION_TELEMETRY_H

#include <QString>
#include <QtGlobal>

enum class EFramePresentationDiagnosis
{
    NoSceneExpected,
    SceneSubmittedToPresentationTarget,
    ClearedWithoutSceneDraw,
    WrongPresentationTarget,
    AllWorldVisibilityDisabled,
    InvalidContextFramebufferOrResources,
    WholeSceneFrustumRejected,
    AsyncUploadOrRebuildWindow
};

struct SFramePresentationTelemetry
{
    quint64 frameId{0};
    bool sceneExpected{false};
    bool contextCurrent{false};
    bool surfaceValid{false};
    bool defaultFramebufferBound{false};
    bool framebufferComplete{false};
    bool resourcesValid{false};
    int framebufferBeforeBind{-1};
    int defaultFramebuffer{-1};
    int framebufferAfterBind{-1};
    unsigned int framebufferStatus{0};
    bool landscapeVisible{false};
    bool overlaysVisible{false};
    bool staticObjectsVisible{false};
    bool dynamicObjectsVisible{false};
    bool frustumCullingEnabled{false};
    bool cameraFinite{true};
    bool asyncUploadOrRebuildActive{false};
    quint64 sceneDrawCalls{0};
    quint64 terrainDrawCalls{0};
    quint64 nodesVisited{0};
    quint64 nodesCulled{0};
    quint64 nodesSubmitted{0};
    quint64 terrainSectorsTested{0};
    quint64 terrainSectorsCulled{0};
    quint64 terrainSectorsVisible{0};
};

EFramePresentationDiagnosis classifyFramePresentation(
    const SFramePresentationTelemetry& frame);
QString framePresentationDiagnosisName(EFramePresentationDiagnosis diagnosis);
QString formatFramePresentationTelemetry(const SFramePresentationTelemetry& frame);

#endif

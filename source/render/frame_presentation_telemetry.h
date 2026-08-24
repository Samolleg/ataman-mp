#ifndef FRAME_PRESENTATION_TELEMETRY_H
#define FRAME_PRESENTATION_TELEMETRY_H

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <array>

enum class EShadowFramebufferPass
{
    StaticNear = 0,
    StaticFar,
    DynamicNear,
    DynamicFar,
    Local,
    Count
};

constexpr int kShadowFramebufferPassCount =
    static_cast<int>(EShadowFramebufferPass::Count);

inline int shadowFramebufferPassIndex(EShadowFramebufferPass pass)
{
    return static_cast<int>(pass);
}

inline quint8 shadowFramebufferPassBit(EShadowFramebufferPass pass)
{
    return quint8(1u << unsigned(shadowFramebufferPassIndex(pass)));
}

struct SShadowFramebufferPassTelemetry
{
    bool attempted{false};
    int framebufferId{-1};
    bool bindSucceeded{false};
    int framebufferAfterBind{-1};
    bool releaseSucceeded{false};
    bool bindDefaultSucceeded{false};
    int framebufferAfterRestore{-1};
};

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
    qint64 capturedMilliseconds{0};
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
    bool fullEnvironment{false};
    bool shadowsEnabled{false};
    quint8 shadowRebuildMask{0};
    std::array<int, kShadowFramebufferPassCount> shadowFramebufferIds{{
        -1, -1, -1, -1, -1}};
    std::array<SShadowFramebufferPassTelemetry,
               kShadowFramebufferPassCount> shadowPasses{};
    int framebufferBeforeTerrainColor{-1};
    int framebufferBeforeObjectFlush{-1};
    int framebufferBeforeWaterOverlay{-1};
    int framebufferAtPaintReturn{-1};
};

struct SFramePresentationMarkerSnapshot
{
    quint64 markerId{0};
    QVector<SFramePresentationTelemetry> frames;

    bool isValid() const { return markerId > 0 && !frames.isEmpty(); }
};

class CFramePresentationHistory
{
public:
    explicit CFramePresentationHistory(int capacity = 512);

    void arm();
    void disarm();
    bool isArmed() const { return m_armed; }
    int capacity() const { return m_capacity; }
    int size() const { return m_frames.size(); }
    bool append(const SFramePresentationTelemetry& frame);
    SFramePresentationMarkerSnapshot snapshot();

private:
    int m_capacity{1};
    QVector<SFramePresentationTelemetry> m_frames;
    int m_nextWrite{0};
    bool m_armed{false};
    quint64 m_nextMarkerId{1};
};

EFramePresentationDiagnosis classifyFramePresentation(
    const SFramePresentationTelemetry& frame);
QString framePresentationDiagnosisName(EFramePresentationDiagnosis diagnosis);
QString formatFramePresentationTelemetry(const SFramePresentationTelemetry& frame);

#endif

#include <QtTest>

#include "frame_presentation_telemetry.h"

class FramePresentationTelemetryTests : public QObject
{
    Q_OBJECT

private slots:
    void classifiesHealthySubmission();
    void classifiesClearedFrameWithoutSceneDraw();
    void classifiesShadowOnlyDrawAsCleared();
    void classifiesWrongPresentationTargetAfterSubmission();
    void classifiesAllWorldVisibilityDisabled();
    void classifiesInvalidContextFramebufferOrResources();
    void classifiesWholeSceneFrustumRejection();
    void classifiesAsyncRebuildWindow();
    void classifiesShadowRestoreFailureBeforeColorPass();
    void retainsFullEnvironmentShadowLifecycleCheckpoints();
    void rollingHistoryRemainsArmedAndBoundedPastFrameLimit();
    void markerRequiresEvidenceAndRepeatedMarkersRemainDistinct();
    void disarmStopsAndClearsCapture();
};

static SFramePresentationTelemetry healthyFrame()
{
    SFramePresentationTelemetry frame;
    frame.sceneExpected = true;
    frame.contextCurrent = true;
    frame.surfaceValid = true;
    frame.defaultFramebufferBound = true;
    frame.framebufferComplete = true;
    frame.resourcesValid = true;
    frame.landscapeVisible = true;
    frame.overlaysVisible = true;
    frame.staticObjectsVisible = true;
    frame.dynamicObjectsVisible = true;
    frame.sceneDrawCalls = 12;
    frame.terrainDrawCalls = 4;
    frame.nodesSubmitted = 8;
    frame.terrainSectorsTested = 6;
    frame.terrainSectorsVisible = 4;
    return frame;
}

void FramePresentationTelemetryTests::classifiesHealthySubmission()
{
    QCOMPARE(classifyFramePresentation(healthyFrame()),
             EFramePresentationDiagnosis::SceneSubmittedToPresentationTarget);
}

void FramePresentationTelemetryTests::classifiesClearedFrameWithoutSceneDraw()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.sceneDrawCalls = 0;
    frame.terrainDrawCalls = 0;
    frame.nodesSubmitted = 0;
    frame.terrainSectorsTested = 0;
    frame.terrainSectorsVisible = 0;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::ClearedWithoutSceneDraw);
}

void FramePresentationTelemetryTests::classifiesShadowOnlyDrawAsCleared()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.sceneDrawCalls = 7;
    frame.terrainDrawCalls = 0;
    frame.nodesSubmitted = 0;
    frame.terrainSectorsTested = 0;
    frame.terrainSectorsVisible = 0;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::ClearedWithoutSceneDraw);
}

void FramePresentationTelemetryTests::classifiesWrongPresentationTargetAfterSubmission()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.defaultFramebufferBound = false;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::WrongPresentationTarget);
}

void FramePresentationTelemetryTests::classifiesAllWorldVisibilityDisabled()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.landscapeVisible = false;
    frame.overlaysVisible = false;
    frame.staticObjectsVisible = false;
    frame.dynamicObjectsVisible = false;
    frame.sceneDrawCalls = 0;
    frame.terrainDrawCalls = 0;
    frame.nodesSubmitted = 0;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::AllWorldVisibilityDisabled);
}

void FramePresentationTelemetryTests::classifiesInvalidContextFramebufferOrResources()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.framebufferComplete = false;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::InvalidContextFramebufferOrResources);
}

void FramePresentationTelemetryTests::classifiesWholeSceneFrustumRejection()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.sceneDrawCalls = 0;
    frame.terrainDrawCalls = 0;
    frame.nodesSubmitted = 0;
    frame.frustumCullingEnabled = true;
    frame.nodesVisited = 9;
    frame.nodesCulled = 9;
    frame.terrainSectorsTested = 6;
    frame.terrainSectorsCulled = 6;
    frame.terrainSectorsVisible = 0;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::WholeSceneFrustumRejected);
}

void FramePresentationTelemetryTests::classifiesAsyncRebuildWindow()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.sceneDrawCalls = 0;
    frame.terrainDrawCalls = 0;
    frame.nodesSubmitted = 0;
    frame.terrainSectorsTested = 0;
    frame.terrainSectorsVisible = 0;
    frame.asyncUploadOrRebuildActive = true;
    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::AsyncUploadOrRebuildWindow);
}

void FramePresentationTelemetryTests::classifiesShadowRestoreFailureBeforeColorPass()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.defaultFramebuffer = 0;
    frame.framebufferAfterBind = 0;
    frame.fullEnvironment = true;
    frame.shadowsEnabled = true;
    frame.shadowRebuildMask = shadowFramebufferPassBit(
        EShadowFramebufferPass::DynamicNear);
    SShadowFramebufferPassTelemetry& dynamicNear =
        frame.shadowPasses[shadowFramebufferPassIndex(
            EShadowFramebufferPass::DynamicNear)];
    dynamicNear.attempted = true;
    dynamicNear.framebufferId = 23;
    dynamicNear.bindSucceeded = true;
    dynamicNear.framebufferAfterBind = 23;
    dynamicNear.releaseSucceeded = true;
    dynamicNear.bindDefaultSucceeded = false;
    dynamicNear.framebufferAfterRestore = 23;
    frame.framebufferBeforeTerrainColor = 23;

    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::WrongPresentationTarget);
}

void FramePresentationTelemetryTests::retainsFullEnvironmentShadowLifecycleCheckpoints()
{
    SFramePresentationTelemetry frame = healthyFrame();
    frame.defaultFramebuffer = 0;
    frame.framebufferAfterBind = 0;
    frame.fullEnvironment = true;
    frame.shadowsEnabled = true;
    frame.shadowFramebufferIds = {{11, 12, 13, 14, 15}};
    frame.shadowRebuildMask = shadowFramebufferPassBit(
        EShadowFramebufferPass::DynamicNear)
        | shadowFramebufferPassBit(EShadowFramebufferPass::DynamicFar)
        | shadowFramebufferPassBit(EShadowFramebufferPass::Local);
    for(EShadowFramebufferPass pass : {
            EShadowFramebufferPass::DynamicNear,
            EShadowFramebufferPass::DynamicFar,
            EShadowFramebufferPass::Local})
    {
        SShadowFramebufferPassTelemetry& sample =
            frame.shadowPasses[shadowFramebufferPassIndex(pass)];
        sample.attempted = true;
        sample.framebufferId = frame.shadowFramebufferIds[
            shadowFramebufferPassIndex(pass)];
        sample.bindSucceeded = true;
        sample.framebufferAfterBind = sample.framebufferId;
        sample.releaseSucceeded = true;
        sample.bindDefaultSucceeded = true;
        sample.framebufferAfterRestore = 0;
    }
    frame.framebufferBeforeTerrainColor = 0;
    frame.framebufferBeforeObjectFlush = 0;
    frame.framebufferBeforeWaterOverlay = 0;
    frame.framebufferAtPaintReturn = 0;

    QCOMPARE(classifyFramePresentation(frame),
             EFramePresentationDiagnosis::SceneSubmittedToPresentationTarget);
    const QString formatted = formatFramePresentationTelemetry(frame);
    QVERIFY(formatted.contains(QStringLiteral("full-environment:1 shadows:1")));
    QVERIFY(formatted.contains(QStringLiteral("shadow-rebuild:0x1c")));
    QVERIFY(formatted.contains(QStringLiteral("shadow-fbos:11/12/13/14/15")));
    QVERIFY(formatted.contains(QStringLiteral("dynamic-near:13/1/13/1/1/0")));
    QVERIFY(formatted.contains(QStringLiteral("dynamic-far:14/1/14/1/1/0")));
    QVERIFY(formatted.contains(QStringLiteral("local:15/1/15/1/1/0")));
    QVERIFY(formatted.contains(QStringLiteral("color-targets:0/0/0/0")));
}

void FramePresentationTelemetryTests::rollingHistoryRemainsArmedAndBoundedPastFrameLimit()
{
    CFramePresentationHistory history(3);
    history.arm();
    for(quint64 frameId = 1; frameId <= 600; ++frameId)
    {
        SFramePresentationTelemetry frame = healthyFrame();
        frame.frameId = frameId;
        QVERIFY(history.append(frame));
    }

    QVERIFY(history.isArmed());
    QCOMPARE(history.size(), 3);
    const SFramePresentationMarkerSnapshot snapshot = history.snapshot();
    QVERIFY(snapshot.isValid());
    QCOMPARE(snapshot.frames.size(), 3);
    QCOMPARE(snapshot.frames.at(0).frameId, quint64(598));
    QCOMPARE(snapshot.frames.at(1).frameId, quint64(599));
    QCOMPARE(snapshot.frames.at(2).frameId, quint64(600));
}

void FramePresentationTelemetryTests::markerRequiresEvidenceAndRepeatedMarkersRemainDistinct()
{
    CFramePresentationHistory history(4);
    history.arm();
    QVERIFY(!history.snapshot().isValid());

    SFramePresentationTelemetry frame = healthyFrame();
    frame.frameId = 41;
    QVERIFY(history.append(frame));
    const SFramePresentationMarkerSnapshot first = history.snapshot();
    QVERIFY(first.isValid());

    frame.frameId = 42;
    QVERIFY(history.append(frame));
    const SFramePresentationMarkerSnapshot second = history.snapshot();
    QVERIFY(second.isValid());
    QVERIFY(first.markerId != second.markerId);
    QCOMPARE(first.frames.last().frameId, quint64(41));
    QCOMPARE(second.frames.last().frameId, quint64(42));
}

void FramePresentationTelemetryTests::disarmStopsAndClearsCapture()
{
    CFramePresentationHistory history(2);
    history.arm();
    SFramePresentationTelemetry frame = healthyFrame();
    frame.frameId = 1;
    QVERIFY(history.append(frame));
    history.disarm();
    QVERIFY(!history.isArmed());
    QCOMPARE(history.size(), 0);
    QVERIFY(!history.append(frame));
    QVERIFY(!history.snapshot().isValid());
}

QTEST_APPLESS_MAIN(FramePresentationTelemetryTests)

#include "frame_presentation_telemetry_tests.moc"

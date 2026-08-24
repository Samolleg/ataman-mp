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

QTEST_APPLESS_MAIN(FramePresentationTelemetryTests)

#include "frame_presentation_telemetry_tests.moc"

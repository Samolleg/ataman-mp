#include <QtTest>

#include <limits>

#include "camera_orbit.h"
#include "camera_telephoto.h"
#include "shadow_cascade.h"

class CameraTests : public QObject
{
    Q_OBJECT

private slots:
    void changesOrbitPivotWithoutMovingTheCurrentView();
    void frustumSliceUsesTheRenderedCameraEye();
    void mapsShadowQualityToCapabilityBoundResolution();
    void integratesMovementIndependentlyOfFrameRate();
    void advancesVsyncFramesWithoutFixedStepLag();
    void keepsMovementActiveBetweenInterpolatedSteps();
    void telephotoMultiplierContract();
    void telephotoUsesFixedJamaisVuOrientation();
    void telephotoMovesOnlyTheRenderEye();
    void telephotoScalesOnlyPerspectiveXY();
    void telephotoPreservesCentralScreenScale();
    void telephotoKeepsLogicalCullingCenter();
    void telephotoScreenWorldTransformsStayConsistent();
    void telephotoBlocksRotationButKeepsPanAndZoom();
    void telephotoExitRestoresTheExactNormalView();
};

void CameraTests::changesOrbitPivotWithoutMovingTheCurrentView()
{
    const QVector3D position(14.0f, -9.0f, 32.0f);
    const QVector3D oldPivot(0.0f, 0.0f, 0.0f);
    const float xRotation = -63.0f;
    const float zRotation = 28.0f;
    const QVector3D expectedPivot(117.0f, -42.0f, 6.5f);
    const auto viewMatrix = [xRotation, zRotation](const QVector3D& pos,
                                                   const QVector3D& pivot)
    {
        QMatrix4x4 matrix;
        matrix.translate(pivot);
        matrix.translate(-pos);
        matrix.rotate(xRotation, 1.0f, 0.0f, 0.0f);
        matrix.rotate(zRotation, 0.0f, 0.0f, 1.0f);
        matrix.translate(-pivot);
        return matrix;
    };
    const QMatrix4x4 before = viewMatrix(position, oldPivot);

    const QVector3D reanchored = ei::reanchoredCameraPosition(
        position, oldPivot, expectedPivot, xRotation, zRotation);

    const QMatrix4x4 after = viewMatrix(reanchored, expectedPivot);
    const float* left = before.constData();
    const float* right = after.constData();
    for(int element = 0; element < 16; ++element)
        QVERIFY2(qAbs(left[element] - right[element]) < 0.0001f,
                 "Changing the orbit pivot moved the current view");
}

void CameraTests::frustumSliceUsesTheRenderedCameraEye()
{
    QMatrix4x4 projection;
    projection.perspective(45.0f, 16.0f / 9.0f, 1.0f, 1000.0f);
    QMatrix4x4 view;
    view.translate(80.0f, -20.0f, -60.0f);
    view.rotate(-62.0f, 1.0f, 0.0f, 0.0f);
    view.rotate(137.0f, 0.0f, 0.0f, 1.0f);

    bool eyeOk = false;
    const QVector3D eye = ei::cameraEyeFromView(view, &eyeOk);
    QVERIFY(eyeOk);
    QVERIFY((view * QVector4D(eye, 1.0f)).toVector3D().length() < 0.0001f);

    bool cornersOk = false;
    const QVector<QVector3D> corners = ei::cameraFrustumSliceCorners(
        projection, view, 1.0f, 48.0f, &cornersOk);
    QVERIFY(cornersOk);
    QCOMPARE(corners.size(), 8);
    for(int index = 0; index < corners.size(); ++index)
    {
        const QVector4D inView = view * QVector4D(corners.at(index), 1.0f);
        const float expectedDepth = (index % 2 == 0) ? 1.0f : 48.0f;
        QVERIFY(qAbs(-inView.z() - expectedDepth) < 0.001f);
    }
}

void CameraTests::mapsShadowQualityToCapabilityBoundResolution()
{
    QCOMPARE(ei::shadowAtlasResolution(0, 16384), 0);
    QCOMPARE(ei::shadowAtlasResolution(1, 16384), 2048);
    QCOMPARE(ei::shadowAtlasResolution(2, 16384), 4096);
    QCOMPARE(ei::shadowAtlasResolution(2, 2048), 2048);
    QCOMPARE(ei::shadowAtlasResolution(1, 1024), 1024);
}

void CameraTests::integratesMovementIndependentlyOfFrameRate()
{
    auto distanceAfterOneSecond = [](int framesPerSecond) {
        float distance = 0.0f;
        for(int frame = 0; frame < framesPerSecond; ++frame)
            distance += 0.5f * ei::cameraFrameMovementScale(
                1.0 / double(framesPerSecond));
        return distance;
    };
    const float at30 = distanceAfterOneSecond(30);
    const float at60 = distanceAfterOneSecond(60);
    const float at240 = distanceAfterOneSecond(240);
    QVERIFY(qAbs(at30 - at60) < 0.0001f);
    QVERIFY(qAbs(at240 - at60) < 0.0001f);
    QCOMPARE(ei::cameraFrameMovementScale(1.0), 6.0f);
}

void CameraTests::advancesVsyncFramesWithoutFixedStepLag()
{
    const QVector<double> frameTimes = {
        1.0 / 59.0, 1.0 / 61.0, 1.0 / 60.0, 1.0 / 59.5,
        1.0 / 60.5, 1.0 / 60.0
    };
    double expected = 0.0;
    double integrated = 0.0;
    for(double frameTime : frameTimes)
    {
        expected += frameTime * 60.0;
        integrated += ei::cameraFrameMovementScale(frameTime);
        QVERIFY2(qAbs(integrated - expected) < 0.000001,
                 "VSync-sized frame deltas were quantized or delayed");
    }
}

void CameraTests::keepsMovementActiveBetweenInterpolatedSteps()
{
    QVERIFY(!ei::cameraMotionActive(false, false));
    QVERIFY(ei::cameraMotionActive(true, false));
    QVERIFY(ei::cameraMotionActive(false, true));
}

void CameraTests::telephotoMultiplierContract()
{
    QCOMPARE(ei::kTelephotoMultiplierMinimum, 1.0f);
    QCOMPARE(ei::kTelephotoMultiplierDefault, 2.0f);
    QCOMPARE(ei::kTelephotoMultiplierMaximum, 4.0f);
    QCOMPARE(ei::boundedTelephotoMultiplier(-5.0f), 1.0f);
    QCOMPARE(ei::boundedTelephotoMultiplier(2.75f), 2.75f);
    QCOMPARE(ei::boundedTelephotoMultiplier(9.0f), 4.0f);
    QCOMPARE(ei::boundedTelephotoMultiplier(
                 std::numeric_limits<float>::quiet_NaN()), 2.0f);
}

void CameraTests::telephotoUsesFixedJamaisVuOrientation()
{
    const QVector3D direction = ei::telephotoViewDirection();
    const float yaw = qRadiansToDegrees(qAtan2(direction.y(), direction.x()));
    const float pitch = qRadiansToDegrees(qAsin(-direction.z()));
    QVERIFY2(qAbs(yaw - 45.0f) < 0.0001f,
             "Telephoto yaw must be exactly 45 degrees in the editor's +X/+Y plane");
    QVERIFY2(qAbs(pitch - 35.264f) < 0.0001f,
             "Telephoto pitch must look down by 35.264 degrees");

    const QVector3D target(20.0f, -5.0f, 3.0f);
    QMatrix4x4 normalView;
    normalView.lookAt(target - direction * 30.0f, target,
                      QVector3D(0.0f, 0.0f, 1.0f));
    const ei::STelephotoCameraFrame frame = ei::telephotoCameraFrame(
        normalView, target, 2.0f);
    const QVector3D targetInView =
        (frame.view * QVector4D(target, 1.0f)).toVector3D();
    QVERIFY(qAbs(targetInView.x()) < 0.0001f);
    QVERIFY(qAbs(targetInView.y()) < 0.0001f);
    QVERIFY(qAbs(targetInView.z() + 60.0f) < 0.001f);

    // Roll 0 keeps the camera-right vector horizontal in the Z-up world.
    const QVector3D right = ei::telephotoGroundRight();
    QVERIFY(qAbs(right.z()) < 0.000001f);
    QVERIFY(qAbs(QVector3D::dotProduct(right, direction)) < 0.000001f);
}

void CameraTests::telephotoMovesOnlyTheRenderEye()
{
    const QVector3D target(100.0f, 80.0f, 12.0f);
    const QVector3D direction = ei::telephotoViewDirection();
    const float logicalDistance = 42.0f;
    QMatrix4x4 normalView;
    normalView.lookAt(target - direction * logicalDistance, target,
                      QVector3D(0.0f, 0.0f, 1.0f));

    const ei::STelephotoCameraFrame frame = ei::telephotoCameraFrame(
        normalView, target, 3.0f);
    QVERIFY((frame.target - target).length() < 0.0001f);
    QVERIFY(qAbs(frame.logicalDistance - logicalDistance) < 0.0001f);
    QVERIFY(qAbs(frame.logicalEye.distanceToPoint(target) - logicalDistance)
            < 0.0001f);
    QVERIFY(qAbs(frame.renderEye.distanceToPoint(target)
                 - logicalDistance * 3.0f) < 0.0002f);
    QVERIFY((frame.renderEye
             - (target - direction * logicalDistance * 3.0f)).length()
            < 0.0002f);
}

void CameraTests::telephotoScalesOnlyPerspectiveXY()
{
    QMatrix4x4 normalProjection;
    normalProjection.perspective(45.0f, 16.0f / 9.0f, 1.0f, 1000.0f);
    const QMatrix4x4 telephoto = ei::telephotoProjectionMatrix(
        normalProjection, 2.5f);

    for(int row = 0; row < 4; ++row)
        for(int column = 0; column < 4; ++column)
        {
            if(row == 0 && column == 0)
                QCOMPARE(telephoto(row, column),
                         normalProjection(row, column) * 2.5f);
            else if(row == 1 && column == 1)
                QCOMPARE(telephoto(row, column),
                         normalProjection(row, column) * 2.5f);
            else
                QCOMPARE(telephoto(row, column),
                         normalProjection(row, column));
        }
}

void CameraTests::telephotoPreservesCentralScreenScale()
{
    const QVector3D target(0.0f, 0.0f, 0.0f);
    const QVector3D direction = ei::telephotoViewDirection();
    const QVector3D right = ei::telephotoGroundRight();
    const float logicalDistance = 40.0f;
    QMatrix4x4 normalView;
    normalView.lookAt(target - direction * logicalDistance, target,
                      QVector3D(0.0f, 0.0f, 1.0f));
    QMatrix4x4 normalProjection;
    normalProjection.perspective(45.0f, 16.0f / 9.0f, 1.0f, 1000.0f);
    const QRect viewport(0, 0, 1920, 1080);
    const QVector3D edge = target + right * 2.0f;
    const QVector3D normalCenter = target.project(
        normalView, normalProjection, viewport);
    const QVector3D normalEdge = edge.project(
        normalView, normalProjection, viewport);

    const ei::STelephotoCameraFrame frame = ei::telephotoCameraFrame(
        normalView, target, 2.0f);
    const QMatrix4x4 telephotoProjection = ei::telephotoProjectionMatrix(
        normalProjection, 2.0f);
    const QVector3D telephotoCenter = target.project(
        frame.view, telephotoProjection, viewport);
    const QVector3D telephotoEdge = edge.project(
        frame.view, telephotoProjection, viewport);
    const float normalPixels = (normalEdge - normalCenter).length();
    const float telephotoPixels = (telephotoEdge - telephotoCenter).length();
    QVERIFY2(qAbs(normalPixels - telephotoPixels) < 0.01f,
             "A centered object changed screen size at multiplier 2.0");
}

void CameraTests::telephotoKeepsLogicalCullingCenter()
{
    const QVector3D target(64.0f, 32.0f, 4.0f);
    const QVector3D direction = ei::telephotoViewDirection();
    const QVector3D logicalEye = target - direction * 25.0f;
    QMatrix4x4 normalView;
    normalView.lookAt(logicalEye, target, QVector3D(0.0f, 0.0f, 1.0f));

    const ei::STelephotoCameraFrame frame = ei::telephotoCameraFrame(
        normalView, target, 4.0f);
    QVERIFY((frame.logicalEye - logicalEye).length() < 0.0001f);
    QVERIFY((frame.cullingCenter - logicalEye).length() < 0.0001f);
    QVERIFY(frame.renderEye.distanceToPoint(frame.cullingCenter) > 70.0f);
}

void CameraTests::telephotoScreenWorldTransformsStayConsistent()
{
    const QVector3D target(18.0f, 7.0f, 2.0f);
    const QVector3D direction = ei::telephotoViewDirection();
    QMatrix4x4 normalView;
    normalView.lookAt(target - direction * 35.0f, target,
                      QVector3D(0.0f, 0.0f, 1.0f));
    QMatrix4x4 normalProjection;
    normalProjection.perspective(45.0f, 4.0f / 3.0f, 1.0f, 1000.0f);
    const ei::STelephotoCameraFrame frame = ei::telephotoCameraFrame(
        normalView, target, 2.0f);
    const QMatrix4x4 projection = ei::telephotoProjectionMatrix(
        normalProjection, 2.0f);
    const QRect viewport(0, 0, 1280, 960);
    const QVector3D world = target + ei::telephotoGroundRight() * 3.0f
        + QVector3D(0.0f, 0.0f, 1.5f);

    // Render, selection and depth picking consume this exact pair. A projected
    // depth sample must therefore unproject back to the rendered world point.
    const QVector3D screen = world.project(frame.view, projection, viewport);
    const QVector3D unprojected = screen.unproject(
        frame.view, projection, viewport);
    QVERIFY2((unprojected - world).length() < 0.001f,
             "Telephoto render and depth-unproject matrices diverged");
}

void CameraTests::telephotoBlocksRotationButKeepsPanAndZoom()
{
    QVERIFY(ei::cameraRotationInputAllowed(false));
    QVERIFY(!ei::cameraRotationInputAllowed(true));

    const QVector3D forward = ei::telephotoGroundForward();
    const QVector3D right = ei::telephotoGroundRight();
    QVERIFY(qAbs(forward.z()) < 0.000001f);
    QVERIFY(qAbs(right.z()) < 0.000001f);
    QVERIFY(qAbs(QVector3D::dotProduct(forward, right)) < 0.000001f);

    QVector3D target(10.0f, 20.0f, 3.0f);
    QVector3D position(10.0f, 20.0f, 33.0f);
    const float distanceBeforePan = position.distanceToPoint(target);
    const QVector3D pan = forward * 4.0f + right * 2.0f;
    position += pan;
    target += pan;
    QVERIFY(qAbs(position.distanceToPoint(target) - distanceBeforePan)
            < 0.0001f);

    const QVector3D zoomed = ei::telephotoZoomedPosition(
        position, target, 5.0f, false);
    QVERIFY(qAbs(zoomed.distanceToPoint(target)
                 - (distanceBeforePan + 5.0f)) < 0.0001f);
}

void CameraTests::telephotoExitRestoresTheExactNormalView()
{
    QMatrix4x4 normalView;
    normalView.translate(17.0f, -9.0f, -31.0f);
    normalView.rotate(-63.0f, 1.0f, 0.0f, 0.0f);
    normalView.rotate(28.0f, 0.0f, 0.0f, 1.0f);
    QMatrix4x4 normalProjection;
    normalProjection.perspective(45.0f, 1.7f, 1.0f, 1000.0f);
    const QVector3D target(5.0f, 7.0f, 1.0f);

    const QMatrix4x4 active = ei::activeCameraViewMatrix(
        normalView, target, true, 2.0f);
    QVERIFY(active != normalView);
    const QMatrix4x4 restored = ei::activeCameraViewMatrix(
        normalView, target, false, 2.0f);
    const QMatrix4x4 restoredProjection = ei::activeCameraProjectionMatrix(
        normalProjection, false, 2.0f);
    for(int element = 0; element < 16; ++element)
    {
        QCOMPARE(restored.constData()[element], normalView.constData()[element]);
        QCOMPARE(restoredProjection.constData()[element],
                 normalProjection.constData()[element]);
    }
}

QTEST_APPLESS_MAIN(CameraTests)
#include "camera_tests.moc"

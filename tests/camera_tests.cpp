#include <QtTest>

#include "camera_orbit.h"
#include "shadow_cascade.h"

class CameraTests : public QObject
{
    Q_OBJECT

private slots:
    void changesOrbitPivotWithoutMovingTheCurrentView();
    void frustumSliceUsesTheRenderedCameraEye();
    void mapsShadowQualityToCapabilityBoundResolution();
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

QTEST_APPLESS_MAIN(CameraTests)
#include "camera_tests.moc"

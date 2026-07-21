#include <QtTest>

#include "camera_orbit.h"

class CameraTests : public QObject
{
    Q_OBJECT

private slots:
    void changesOrbitPivotWithoutMovingTheCurrentView();
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

QTEST_APPLESS_MAIN(CameraTests)
#include "camera_tests.moc"

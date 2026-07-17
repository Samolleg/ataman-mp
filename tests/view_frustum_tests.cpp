#include <QtTest>

#include "view_frustum.h"

class ViewFrustumTests : public QObject
{
    Q_OBJECT

private slots:
    void identityClipRejectsOutsideSpheres();
    void perspectiveClipHandlesNearAndFarObjects();
};

void ViewFrustumTests::identityClipRejectsOutsideSpheres()
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    const CViewFrustum frustum(identity);
    QVERIFY(frustum.intersectsSphere(QVector3D(0, 0, 0), 0));
    QVERIFY(!frustum.intersectsSphere(QVector3D(2, 0, 0), 0));
    QVERIFY(frustum.intersectsSphere(QVector3D(1.5f, 0, 0), 0.6f));
    QVERIFY(!frustum.intersectsSphere(QVector3D(0, 0, -2), 0.1f));
}

void ViewFrustumTests::perspectiveClipHandlesNearAndFarObjects()
{
    QMatrix4x4 projection;
    projection.perspective(45.0f, 1.0f, 1.0f, 100.0f);
    const CViewFrustum frustum(projection);
    QVERIFY(frustum.intersectsSphere(QVector3D(0, 0, -10), 1));
    QVERIFY(!frustum.intersectsSphere(QVector3D(100, 0, -10), 1));
    QVERIFY(!frustum.intersectsSphere(QVector3D(0, 0, -150), 1));
}

QTEST_APPLESS_MAIN(ViewFrustumTests)
#include "view_frustum_tests.moc"

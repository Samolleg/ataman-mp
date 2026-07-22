#include <QtTest>

#include "view_frustum.h"
#include "spatial_grid.h"

class ViewFrustumTests : public QObject
{
    Q_OBJECT

private slots:
    void identityClipRejectsOutsideSpheres();
    void perspectiveClipHandlesNearAndFarObjects();
    void aabbIntersectionUsesPositiveVertex();
    void sweptSphereRetainsOffscreenShadowCasters();
    void spatialGridReturnsVisibleEntriesInSourceOrder();
    void spatialGridInsertsBoundarySpanningEntriesIntoEachCell();
    void spatialGridPreservesOverflowEntries();
    void spatialGridTreatsInvalidBoundsAsDiagnosticOverflow();
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

void ViewFrustumTests::aabbIntersectionUsesPositiveVertex()
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    const CViewFrustum frustum(identity);
    QVERIFY(frustum.intersectsAabb(QVector3D(-0.5f, -0.5f, -0.5f),
                                   QVector3D(0.5f, 0.5f, 0.5f)));
    QVERIFY(!frustum.intersectsAabb(QVector3D(2.0f, 2.0f, 2.0f),
                                    QVector3D(3.0f, 3.0f, 3.0f)));
}

void ViewFrustumTests::sweptSphereRetainsOffscreenShadowCasters()
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    const CViewFrustum frustum(identity);
    QVERIFY(!frustum.intersectsSphere(QVector3D(2.0f, 0.0f, 0.0f), 0.1f));
    QVERIFY(frustum.intersectsSweptSphere(QVector3D(2.0f, 0.0f, 0.0f),
                                          0.1f, QVector3D(-2.0f, 0.0f, 0.0f)));
    QVERIFY(!frustum.intersectsSweptSphere(QVector3D(3.0f, 0.0f, 0.0f),
                                           0.1f, QVector3D(1.0f, 0.0f, 0.0f)));
}

void ViewFrustumTests::spatialGridReturnsVisibleEntriesInSourceOrder()
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    const CViewFrustum frustum(identity);
    CSpatialGrid<int> grid(1.0f);
    grid.insert(10, QVector3D(0.5f, 0.0f, 0.0f), 0.1f);
    grid.insert(20, QVector3D(5.0f, 0.0f, 0.0f), 0.1f);
    grid.insert(30, QVector3D(-0.5f, 0.0f, 0.0f), 0.1f);
    SSpatialQueryStats stats;
    const QVector<int> result = grid.query(&frustum, &stats);
    QCOMPARE(result, QVector<int>({10, 30}));
    QVERIFY(stats.cellsTested >= 3);
    QCOMPARE(stats.entriesTested, 2);
}

void ViewFrustumTests::spatialGridPreservesOverflowEntries()
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    const CViewFrustum frustum(identity);
    CSpatialGrid<int> grid(1.0f, 4);
    grid.insert(1, QVector3D(100.0f, 0.0f, 0.0f), 50.0f);
    grid.insert(2, QVector3D(100.0f, 0.0f, 0.0f), 0.1f, true);
    QCOMPARE(grid.overflowCount(), 2);
    QCOMPARE(grid.query(&frustum), QVector<int>({2}));
}

void ViewFrustumTests::spatialGridInsertsBoundarySpanningEntriesIntoEachCell()
{
    CSpatialGrid<int> grid(32.0f);
    grid.insert(7, QVector3D(32.0f, 32.0f, 0.0f), 1.0f);
    QCOMPARE(grid.cellCount(), 4);
    QCOMPARE(grid.query(nullptr), QVector<int>({7}));
}

void ViewFrustumTests::spatialGridTreatsInvalidBoundsAsDiagnosticOverflow()
{
    CSpatialGrid<int> grid(32.0f);
    grid.insert(9, QVector3D(qQNaN(), 0.0f, 0.0f), 1.0f);
    QCOMPARE(grid.overflowCount(), 1);
    QMatrix4x4 identity;
    identity.setToIdentity();
    const CViewFrustum frustum(identity);
    QCOMPARE(grid.query(&frustum), QVector<int>({9}));
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

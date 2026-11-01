#include <QtTest>

#include "render_material.h"
#include "render_queue.h"
#include "render_scheduling.h"
#include "render_environment.h"

class RenderQueueTests : public QObject
{
    Q_OBJECT

private slots:
    void alphaClassification();
    void rejectsUncreatedRenderTextures();
    void deterministicQueueOrder();
    void groupsOnlyCompatibleInstanceRuns();
    void filtersShadowCastersByDistance();
    void filtersShadowCastersByCascadeFootprint();
    void selectsModernAndFallbackCapabilities();
    void appliesRenderSchedulingOptions();
    void derivesEnvironmentFromWorldValues();
    void matchesNativeSunRayDirectionAndLength();
    void usesFixedNativeCaveSunRay();
    void combinesShadowCastingPointLights();
    void pointLightInfluenceIsCasterLocalAndFallsWithDistance();
};

void RenderQueueTests::alphaClassification()
{
    QImage opaque(2, 2, QImage::Format_RGBA8888);
    opaque.fill(QColor(1, 2, 3, 255));
    QCOMPARE(classifyTextureAlpha(opaque), ERenderMaterialClass::Opaque);

    QImage cutout = opaque;
    cutout.setPixelColor(0, 0, QColor(1, 2, 3, 0));
    QCOMPARE(classifyTextureAlpha(cutout), ERenderMaterialClass::Cutout);

    QImage transparent = opaque;
    transparent.setPixelColor(0, 0, QColor(1, 2, 3, 127));
    QCOMPARE(classifyTextureAlpha(transparent), ERenderMaterialClass::Transparent);
    QCOMPARE(classifyTextureAlpha(QImage()), ERenderMaterialClass::Conservative);
}

void RenderQueueTests::rejectsUncreatedRenderTextures()
{
    QOpenGLTexture uncreated(QOpenGLTexture::Target2D);
    QVERIFY(!renderTextureReady(nullptr));
    QVERIFY(!renderTextureReady(&uncreated));
}

void RenderQueueTests::deterministicQueueOrder()
{
    SRenderItem highlighted;
    highlighted.materialClass = ERenderMaterialClass::Opaque;
    highlighted.highlighted = true;
    highlighted.sequence = 5;

    SRenderItem transparentNear;
    transparentNear.materialClass = ERenderMaterialClass::Transparent;
    transparentNear.cameraDistanceSquared = 4.0f;
    transparentNear.sequence = 4;

    SRenderItem transparentFar = transparentNear;
    transparentFar.cameraDistanceSquared = 100.0f;
    transparentFar.sequence = 3;

    SRenderItem cutout;
    cutout.materialClass = ERenderMaterialClass::Cutout;
    cutout.sequence = 2;

    SRenderItem opaqueHighTexture;
    opaqueHighTexture.materialClass = ERenderMaterialClass::Opaque;
    opaqueHighTexture.texture = reinterpret_cast<QOpenGLTexture*>(quintptr(2));
    opaqueHighTexture.sequence = 1;

    SRenderItem opaqueLowTexture = opaqueHighTexture;
    opaqueLowTexture.texture = reinterpret_cast<QOpenGLTexture*>(quintptr(1));
    opaqueLowTexture.sequence = 0;

    QVector<SRenderItem> items({highlighted, transparentNear, cutout,
                                opaqueHighTexture, transparentFar, opaqueLowTexture});
    sortRenderItems(items);
    QCOMPARE(items.at(0).sequence, quint64(0));
    QCOMPARE(items.at(1).sequence, quint64(1));
    QCOMPARE(items.at(2).sequence, quint64(2));
    QCOMPARE(items.at(3).sequence, quint64(3));
    QCOMPARE(items.at(4).sequence, quint64(4));
    QCOMPARE(items.at(5).sequence, quint64(5));
}

void RenderQueueTests::groupsOnlyCompatibleInstanceRuns()
{
    SRenderItem first;
    first.materialClass = ERenderMaterialClass::Opaque;
    first.texture = reinterpret_cast<QOpenGLTexture*>(quintptr(1));
    first.meshIdentity = 7;
    first.textureUvScale = 1.0f;
    first.instanceEligible = true;
    SRenderItem second = first;
    second.sequence = 1;
    SRenderItem differentUv = first;
    differentUv.textureUvScale = 2.0f;
    SRenderItem transparent = first;
    transparent.materialClass = ERenderMaterialClass::Transparent;
    SRenderItem highlighted = first;
    highlighted.highlighted = true;
    SRenderItem dynamicCaster = first;
    dynamicCaster.dynamicShadow = true;
    SRenderItem emissive = first;
    emissive.materialEmission = QVector3D(1.0f, 1.0f, 1.0f);

    QVector<SRenderItem> compatible({first, second, differentUv});
    QCOMPARE(instanceBatchLength(compatible, 0), 2);
    QVERIFY(!shareInstanceBatch(first, differentUv));
    QVERIFY(!shareInstanceBatch(first, transparent));
    QVERIFY(!shareInstanceBatch(first, highlighted));
    QVERIFY(!shareInstanceBatch(first, dynamicCaster));
    QVERIFY(!shareInstanceBatch(first, emissive));
    SRenderItem neighbor = first;
    neighbor.backgroundScenery = true;
    QVERIFY(!shareInstanceBatch(first, neighbor)); // the border-fog uniform differs

    first.shadowOwner=101;second.shadowOwner=202;
    QVERIFY(shareInstanceBatch(first,second)); // different objects, same resolved pass state
    first.castsShadow=second.castsShadow=true;
    QVERIFY(shareInstanceBatch(first,second)); // sun/caster pass has no local exclusion
    second.localShadowSkipIndex=0;
    QVERIFY(!shareInstanceBatch(first,second));
    first.localShadowSkipIndex=0;
    QVERIFY(shareInstanceBatch(first,second));
    second.localShadowSkipIndex=1;
    QVERIFY(!shareInstanceBatch(first,second)); // different self-shadow exclusions
    second.localShadowSkipIndex=first.localShadowSkipIndex;
    second.instanceEligible=false;
    QVERIFY(!shareInstanceBatch(first,second)); // animated/morphed path stays separate
    second.instanceEligible=true;
    second.diplomacyTint=QVector4D(1,0,0,1);
    QVERIFY(!shareInstanceBatch(first,second));
    second.diplomacyTint=first.diplomacyTint;
    second.pickingColor=QVector4D(1,0,0,1);
    QVERIFY(shareInstanceBatch(first,second)); // picking color is uploaded per instance

    QVector<SRenderItem> forest;
    first.localShadowSkipIndex=-1;
    for(int i=0;i<1000;++i){auto tree=first;tree.shadowOwner=i+1;tree.sequence=i;forest.append(tree);}
    sortRenderItems(forest);
    QCOMPARE(instanceBatchLength(forest,0),1000);
}

void RenderQueueTests::filtersShadowCastersByDistance()
{
    SRenderItem item;
    item.cameraDistanceSquared = 1600.0f;
    item.cullingDistanceSquared = 16.0f;
    QVERIFY(!shadowItemWithinDistance(item, 1.0f));
    QVERIFY(shadowItemWithinDistance(item, 4.0f));
    QVERIFY(shadowItemWithinDistance(item, 8.0f));
}

void RenderQueueTests::filtersShadowCastersByCascadeFootprint()
{
    QMatrix4x4 lightSpace;
    lightSpace.ortho(-10.0f, 10.0f, -10.0f, 10.0f, -10.0f, 10.0f);

    SRenderItem inside;
    inside.shadowBoundsCenter = QVector3D(0.0f, 0.0f, 0.0f);
    inside.shadowBoundsRadius = 1.0f;
    QVERIFY(shadowItemIntersectsLightSpace(inside, lightSpace));

    SRenderItem crossing = inside;
    crossing.shadowBoundsCenter = QVector3D(10.5f, 0.0f, 0.0f);
    QVERIFY(shadowItemIntersectsLightSpace(crossing, lightSpace));

    SRenderItem outside = inside;
    outside.shadowBoundsCenter = QVector3D(12.0f, 0.0f, 0.0f);
    QVERIFY(!shadowItemIntersectsLightSpace(outside, lightSpace));
}

void RenderQueueTests::selectsModernAndFallbackCapabilities()
{
    QVERIFY(supportsInstancedRenderPath(false, 3, 3));
    QVERIFY(supportsInstancedRenderPath(false, 4, 6));
    QVERIFY(!supportsInstancedRenderPath(false, 3, 2));
    QVERIFY(!supportsInstancedRenderPath(true, 4, 6));
}

void RenderQueueTests::appliesRenderSchedulingOptions()
{
    QCOMPARE(ei::renderTimerIntervalMs(0), 0);
    QCOMPARE(ei::renderTimerIntervalMs(-1), 0);
    QCOMPARE(ei::renderTimerIntervalMs(60), 17);
    QCOMPARE(ei::renderTimerIntervalMs(144), 7);
    QCOMPARE(ei::renderTimerIntervalMs(200), 5);
    QCOMPARE(ei::renderSwapInterval(true, false), 1);
    QCOMPARE(ei::renderSwapInterval(false, false), 0);
    QCOMPARE(ei::renderSwapInterval(true, true), 0);
    QVERIFY(ei::renderVsyncFromOptions("{\"Render\":{\"renderVSync\":true}}"));
    QVERIFY(!ei::renderVsyncFromOptions("{\"Render\":{\"renderVSync\":false}}"));
    QVERIFY(ei::renderVsyncFromOptions("invalid", true));
}

void RenderQueueTests::derivesEnvironmentFromWorldValues()
{
    const ei::SRenderEnvironment noon =
        ei::renderEnvironmentForValues(12.0f, 0.25f, 0.75f);
    QCOMPARE(noon.timeHours, 12.0f);
    QCOMPARE(noon.ambientStrength, 1.0f);
    QCOMPARE(noon.sunStrength, 1.0f);
    QCOMPARE(noon.sunColor, QVector3D(1.0f, 1.0f, 1.0f));
    QCOMPARE(noon.ambientColor, QVector3D(129.0f, 135.0f, 125.0f) / 255.0f);
    QCOMPARE(noon.skyColor, QVector3D(46.0f, 181.0f, 217.0f) / 255.0f);
    QVERIFY(noon.fogStart < noon.fogEnd);
    QCOMPARE(noon.fogEnd, 620.0f);
    QVERIFY(qAbs(noon.sunDirection.x() + 0.5f) < 0.00001f);
    QVERIFY(qAbs(noon.sunDirection.y()) < 0.00001f);
    QVERIFY(qAbs(noon.sunDirection.z() + 0.8660254f) < 0.00001f);

    const ei::SRenderEnvironment midnight =
        ei::renderEnvironmentForValues(24.0f, -1.0f, 9.0f);
    QCOMPARE(midnight.timeHours, 0.0f);
    QCOMPARE(midnight.ambientStrength, 1.0f);
    QCOMPARE(midnight.sunStrength, 1.0f);
    QCOMPARE(midnight.sunColor, QVector3D(63.0f, 93.0f, 157.0f) / 255.0f);
    QCOMPARE(midnight.ambientColor, QVector3D(54.0f, 83.0f, 147.0f) / 255.0f);
    QVERIFY(midnight.sunColor.z() > midnight.sunColor.x());
    QVERIFY(qAbs(midnight.sunDirection.x() - 0.2588190f) < 0.00001f);
    QVERIFY(qAbs(midnight.sunDirection.y()) < 0.00001f);
    QVERIFY(qAbs(midnight.sunDirection.z() + 0.9659258f) < 0.00001f);

    const ei::SRenderEnvironment halfHour =
        ei::renderEnvironmentForValues(19.5f, 99.0f, -99.0f);
    const QVector3D expectedHalfHourSun =
        QVector3D(255.0f, 162.5f, 109.0f) / 255.0f;
    const QVector3D expectedHalfHourAmbient =
        QVector3D(134.0f, 106.5f, 89.5f) / 255.0f;
    QVERIFY((halfHour.sunColor - expectedHalfHourSun).length() < 0.000001f);
    QVERIFY((halfHour.ambientColor - expectedHalfHourAmbient).length() < 0.000001f);

    const ei::SRenderEnvironment ingosNoon =
        ei::renderEnvironmentForValues(12.0f, 0.0f, 0.0f, false, "ingos");
    QCOMPARE(ingosNoon.ambientColor,
             QVector3D(111.0f, 121.0f, 151.0f) / 255.0f);
    QCOMPARE(ingosNoon.skyColor,
             QVector3D(46.0f, 132.0f, 217.0f) / 255.0f);

    const ei::SRenderEnvironment cave =
        ei::renderEnvironmentForValues(10.34f, 0.25f, 0.75f, true);
    QCOMPARE(cave.fogColor, QVector3D(0.0f, 0.0f, 0.0f));
    QCOMPARE(cave.sunColor, QVector3D(161.0f, 179.0f, 170.0f) / 255.0f);
    QCOMPARE(cave.ambientColor, QVector3D(75.0f, 75.0f, 89.0f) / 255.0f);

    QCOMPARE(ei::clockValueToHours(0), 0.0f);
    QCOMPARE(ei::clockValueToHours(1230), 12.5f);
    QCOMPARE(ei::clockValueToHours(2360), 23.0f + 59.0f / 60.0f);
    QCOMPARE(ei::clockValueToHours(2400), 23.0f + 59.0f / 60.0f);
}

void RenderQueueTests::matchesNativeSunRayDirectionAndLength()
{
    const ei::SRenderEnvironment six =
        ei::renderEnvironmentForValues(6.0f, 1.0f, 1.0f);
    const ei::SRenderEnvironment noon =
        ei::renderEnvironmentForValues(12.0f, 1.0f, 1.0f);
    const ei::SRenderEnvironment eighteen =
        ei::renderEnvironmentForValues(18.0f, 1.0f, 1.0f);

    // The shipped light ray completes one clockwise XY cycle with the noon
    // ray pointing -X. Elevation is 60 degrees at noon and 37.5 degrees at
    // 06:00/18:00, so horizontal shadow length per unit height is cot(elevation).
    QVERIFY(qAbs(six.sunDirection.x()) < 0.00001f);
    QVERIFY(six.sunDirection.y() > 0.79334f);
    QVERIFY(noon.sunDirection.x() < -0.49999f);
    QVERIFY(qAbs(noon.sunDirection.y()) < 0.00001f);
    QVERIFY(qAbs(eighteen.sunDirection.x()) < 0.00001f);
    QVERIFY(eighteen.sunDirection.y() < -0.79334f);

    const auto shadowLengthPerHeight = [](const QVector3D& ray)
    {
        return qSqrt(ray.x() * ray.x() + ray.y() * ray.y()) / qAbs(ray.z());
    };
    QVERIFY(qAbs(shadowLengthPerHeight(noon.sunDirection) - 0.5773503f) < 0.00001f);
    QVERIFY(qAbs(shadowLengthPerHeight(six.sunDirection) - 1.3032254f) < 0.00001f);
    QVERIFY(qAbs(shadowLengthPerHeight(eighteen.sunDirection) - 1.3032254f) < 0.00001f);
}

void RenderQueueTests::usesFixedNativeCaveSunRay()
{
    const QVector3D nativeCaveRay(0.5f, 0.5f, -0.707106f);
    const ei::SRenderEnvironment morning =
        ei::renderEnvironmentForValues(4.0f, 1.0f, 1.0f, true);
    const ei::SRenderEnvironment evening =
        ei::renderEnvironmentForValues(18.0f, 1.0f, 1.0f, true);

    // Cave zones bypass the outdoor clock track. Their directional light ray
    // is fixed, normalized, and identical at every authored WS_TIME value.
    QVERIFY((morning.sunDirection - nativeCaveRay).length() < 0.000001f);
    QVERIFY((evening.sunDirection - nativeCaveRay).length() < 0.000001f);
    QVERIFY((morning.sunDirection - evening.sunDirection).length() < 0.000001f);
    QVERIFY(qAbs(morning.sunDirection.length() - 1.0f) < 0.000001f);
}

void RenderQueueTests::combinesShadowCastingPointLights()
{
    const QVector3D down(0.0f, 0.0f, -1.0f);
    QCOMPARE(ei::shadowProjectionDirection(down, QVector3D(1.0f, 1.0f, 1.0f),
                                            1.0f, {}, QVector3D()), down);

    ei::SScenePointLight light;
    light.position = QVector3D(-10.0f, 0.0f, 0.0f);
    light.color = QVector3D(255.0f, 255.0f, 255.0f);
    light.range = 20.0f;
    light.castsShadow = true;
    const QVector3D pointOnly = ei::shadowProjectionDirection(
        down, QVector3D(), 0.0f, {light}, QVector3D());
    QVERIFY(pointOnly.x() > 0.93f);
    QVERIFY(pointOnly.z() < -0.33f);
    QVERIFY(qAbs(pointOnly.length() - 1.0f) < 0.00001f);

    light.castsShadow = false;
    QCOMPARE(ei::shadowProjectionDirection(down, QVector3D(), 0.0f,
                                            {light}, QVector3D()), down);
}

void RenderQueueTests::pointLightInfluenceIsCasterLocalAndFallsWithDistance()
{
    ei::SScenePointLight light;
    light.position = QVector3D();
    light.color = QVector3D(1.0f, 1.0f, 1.0f);
    light.range = 10.0f;
    light.castsShadow = true;
    bool nearInfluenced = false;
    bool edgeInfluenced = false;
    bool outsideInfluenced = true;
    ei::shadowProjectionDirection(QVector3D(0.0f, 0.0f, -1.0f), QVector3D(),
        0.0f, {light}, QVector3D(1.0f, 0.0f, 0.0f), &nearInfluenced);
    ei::shadowProjectionDirection(QVector3D(0.0f, 0.0f, -1.0f), QVector3D(),
        0.0f, {light}, QVector3D(9.0f, 0.0f, 0.0f), &edgeInfluenced);
    ei::shadowProjectionDirection(QVector3D(0.0f, 0.0f, -1.0f), QVector3D(),
        0.0f, {light}, QVector3D(10.01f, 0.0f, 0.0f), &outsideInfluenced);
    QVERIFY(nearInfluenced);
    QVERIFY(edgeInfluenced);
    QVERIFY(!outsideInfluenced);
}

QTEST_APPLESS_MAIN(RenderQueueTests)
#include "render_queue_tests.moc"

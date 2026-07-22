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
    void deterministicQueueOrder();
    void groupsOnlyCompatibleInstanceRuns();
    void selectsModernAndFallbackCapabilities();
    void appliesRenderSchedulingOptions();
    void derivesEnvironmentFromWorldValues();
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

    QVector<SRenderItem> compatible({first, second, differentUv});
    QCOMPARE(instanceBatchLength(compatible, 0), 2);
    QVERIFY(!shareInstanceBatch(first, differentUv));
    QVERIFY(!shareInstanceBatch(first, transparent));
    QVERIFY(!shareInstanceBatch(first, highlighted));
    QVERIFY(!shareInstanceBatch(first, dynamicCaster));
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
    QCOMPARE(noon.ambientStrength, 0.25f);
    QCOMPARE(noon.sunStrength, 0.75f);
    QVERIFY(noon.fogStart < noon.fogEnd);
    QCOMPARE(noon.fogEnd, 620.0f);
    QVERIFY(noon.sunDirection.z() < -0.60f);
    QVERIFY(noon.sunDirection.z() > -0.64f);

    const ei::SRenderEnvironment midnight =
        ei::renderEnvironmentForValues(24.0f, -1.0f, 9.0f);
    QCOMPARE(midnight.timeHours, 0.0f);
    QCOMPARE(midnight.ambientStrength, 0.0f);
    QCOMPARE(midnight.sunStrength, 4.0f);
    QVERIFY(midnight.sunColor.z() > midnight.sunColor.x());
    QVERIFY(qAbs(midnight.sunDirection.z() - noon.sunDirection.z()) < 0.001f);
    QVERIFY((midnight.sunDirection - noon.sunDirection).length() > 1.0f);

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

QTEST_APPLESS_MAIN(RenderQueueTests)
#include "render_queue_tests.moc"

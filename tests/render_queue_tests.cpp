#include <QtTest>

#include "render_material.h"
#include "render_queue.h"

class RenderQueueTests : public QObject
{
    Q_OBJECT

private slots:
    void alphaClassification();
    void deterministicQueueOrder();
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

QTEST_APPLESS_MAIN(RenderQueueTests)
#include "render_queue_tests.moc"

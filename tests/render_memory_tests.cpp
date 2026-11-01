#include <QtTest>
#include "render_memory.h"
class RenderMemoryTests:public QObject {
    Q_OBJECT
private slots:
    void formatAndMipStorage() {
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::RGBA8_UNorm,4,4,3),quint64(84));
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::RGBA_DXT1,4,4,3),quint64(24));
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::RGBA_DXT5,5,7,1),quint64(64));
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::R8_UNorm,8,8,1),quint64(64));
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::RGBA16F,2,2,1),quint64(32));
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::RGBA8_UNorm,12288,12288,1),quint64(576)*1024*1024);
        QCOMPARE(estimatedTextureStorage(QOpenGLTexture::RGBA_DXT1,12288,12288,1),quint64(72)*1024*1024);
    }
    void sharedImagesCountOnce() {
        QImage a(16,16,QImage::Format_RGBA8888);a.fill(Qt::red);auto shared=a;
        SRenderMemoryEstimate memory;memory.image(a);memory.image(shared);
        QCOMPARE(memory.cpuImageBytes,quint64(1024));
        shared.fill(Qt::blue);memory.image(shared);QCOMPARE(memory.cpuImageBytes,quint64(2048));
        memory.image({});memory.texture(nullptr);QCOMPARE(memory.total(),quint64(2048));
    }
};
QTEST_APPLESS_MAIN(RenderMemoryTests)
#include "render_memory_tests.moc"

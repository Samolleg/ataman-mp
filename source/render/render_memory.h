#ifndef RENDER_MEMORY_H
#define RENDER_MEMORY_H
#include <QImage>
#include <QOpenGLTexture>
#include <QSet>

// Allocation-size estimates, not a driver VRAM/RSS query. Unique texture
// wrappers and implicitly shared QImages are charged once per snapshot.
inline quint64 estimatedTextureStorage(QOpenGLTexture::TextureFormat format,int width,int height,int levels)
{
    quint64 bytes=0;
    const int block=format==QOpenGLTexture::RGB_DXT1 || format==QOpenGLTexture::RGBA_DXT1 ? 8
        : format==QOpenGLTexture::RGBA_DXT3 || format==QOpenGLTexture::RGBA_DXT5 ? 16 : 0;
    const int pixel=format==QOpenGLTexture::R8_UNorm?1:format==QOpenGLTexture::RG8_UNorm?2
        :format==QOpenGLTexture::RGB8_UNorm?3:format==QOpenGLTexture::RGBA16F?8
        :format==QOpenGLTexture::RGBA32F?16:4;
    for(int level=0;level<qMax(1,levels)&&width>0&&height>0;++level) {
        bytes+=block?quint64((width+3)/4)*((height+3)/4)*block:quint64(width)*height*pixel;
        width=qMax(1,width/2);height=qMax(1,height/2);
    }
    return bytes;
}
struct SRenderMemoryEstimate
{
    quint64 gpuTextureBytes{0},gpuTerrainTextureBytes{0},gpuTerrainBufferBytes{0};
    quint64 cpuTerrainBytes{0},cpuImageBytes{0};
    QSet<const QOpenGLTexture*> textures;
    QSet<qint64> images;
    void texture(const QOpenGLTexture* value,bool terrain=false) {
        if(!value || !value->isCreated() || textures.contains(value))return;
        textures.insert(value);
        const auto bytes=estimatedTextureStorage(value->format(),value->width(),value->height(),value->mipLevels());
        gpuTextureBytes+=bytes;if(terrain)gpuTerrainTextureBytes+=bytes;
    }
    void image(const QImage& value) {
        if(value.isNull()||images.contains(value.cacheKey()))return;
        images.insert(value.cacheKey());cpuImageBytes+=quint64(value.sizeInBytes());
    }
    quint64 total() const {return gpuTextureBytes+gpuTerrainBufferBytes+cpuTerrainBytes+cpuImageBytes;}
};
#endif

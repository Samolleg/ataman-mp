#ifndef TEXTURE_PAGE_UPLOAD_H
#define TEXTURE_PAGE_UPLOAD_H
#include "legacy_projection_pulse.h"
#include "opening_trace.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QImage>
#include <functional>
#include <cmath>

// Transactional atlas publication with one page of CPU staging. The callback
// uses the existing native pixel orientation; no mirror/re-encoding is added.
inline QOpenGLTexture* uploadRgbaAtlasPages(int count,int columns,int pageSize,
                                           const std::function<QImage(int)>& pageImage)
{
    auto* context=QOpenGLContext::currentContext();
    if(!context||count<=0||columns<=0||columns>count||pageSize<=0)return nullptr;
    const qint64 rows=(qint64(count)+columns-1)/columns;
    const qint64 wide=qint64(columns)*pageSize,high=rows*pageSize;
    auto* gl=context->functions();GLint maximum=0;gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    if(wide>maximum||high>maximum)return nullptr;
    const int width=int(wide),height=int(high);
    GLint previousActive=GL_TEXTURE0,previousTexture=0,previousUnpack=4;
    gl->glGetIntegerv(GL_ACTIVE_TEXTURE,&previousActive);gl->glActiveTexture(GL_TEXTURE0);
    gl->glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousTexture);gl->glGetIntegerv(GL_UNPACK_ALIGNMENT,&previousUnpack);
    while(gl->glGetError()!=GL_NO_ERROR){}
    auto* texture=new QOpenGLTexture(QOpenGLTexture::Target2D);
    texture->setSize(width,height);texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    texture->setMipLevels(1+int(std::floor(std::log2(double(qMax(width,height))))));
    const auto allocateBegin=opening::clock();
    texture->allocateStorage(QOpenGLTexture::RGBA,QOpenGLTexture::UInt8);
    opening::span("gui.gl.atlas_allocate",allocateBegin);
    bool valid=texture->isCreated()&&gl->glGetError()==GL_NO_ERROR;
    if(valid) {
        texture->bind();gl->glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        for(int page=0;page<columns*rows&&valid;++page) {
            QImage pixels;
            const auto decodeBegin=opening::clock();
            if(page<count)pixels=pageImage(page);
            else {pixels=QImage(pageSize,pageSize,QImage::Format_RGBA8888);pixels.fill(Qt::transparent);}
            opening::span("gui.gl.atlas_page_decode",decodeBegin);
            if(ei::legacyProjectionPulse()){gl->glActiveTexture(GL_TEXTURE0);texture->bind();gl->glPixelStorei(GL_UNPACK_ALIGNMENT,4);}
            if(pixels.isNull()||pixels.size()!=QSize(pageSize,pageSize)){valid=false;break;}
            if(pixels.format()!=QImage::Format_RGBA8888)pixels=pixels.convertToFormat(QImage::Format_RGBA8888);
            if(pixels.isNull()){valid=false;break;}
            gl->glTexSubImage2D(GL_TEXTURE_2D,0,(page%columns)*pageSize,(page/columns)*pageSize,
                pageSize,pageSize,GL_RGBA,GL_UNSIGNED_BYTE,pixels.constBits());
            valid=gl->glGetError()==GL_NO_ERROR;
        }
        if(valid){const auto mipBegin=opening::clock();gl->glGenerateMipmap(GL_TEXTURE_2D);valid=gl->glGetError()==GL_NO_ERROR;opening::span("gui.gl.atlas_mipmap",mipBegin);}
    }
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT,previousUnpack);
    gl->glBindTexture(GL_TEXTURE_2D,GLuint(previousTexture));gl->glActiveTexture(GLenum(previousActive));
    if(!valid){delete texture;return nullptr;}
    return texture;
}
#endif

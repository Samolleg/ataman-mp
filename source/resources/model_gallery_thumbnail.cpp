#include "model_gallery_thumbnail.h"
#include <QtMath>
#include <cmath>
#include <limits>

namespace ei {
QImage renderGalleryTriangles(const QVector<SGalleryTriangle>& triangles, const QVector<QImage>& sourceTextures, const QSize& requested, const std::atomic_bool* cancelled)
{
    if(triangles.isEmpty())return {};
    const QSize size(qBound(32, requested.width(), 512), qBound(32, requested.height(), 512));
    QImage image(size, QImage::Format_ARGB32); image.fill(QColor(48, 51, 56));
    QVector<QImage> textures;for(const auto& source:sourceTextures)textures.append(source.convertToFormat(QImage::Format_ARGB32));
    QMatrix4x4 rotation; rotation.rotate(-62.0f, 1, 0, 0); rotation.rotate(-40.0f, 0, 0, 1);
    QVector3D minimum(1e20f,1e20f,1e20f), maximum(-1e20f,-1e20f,-1e20f);
    int count = 0;
    for(const auto& entry:triangles) for(const auto& vertex:entry.triangle.vertices)
        {
            const QVector3D point = rotation * vertex.position;
            if(!qIsFinite(point.x()) || !qIsFinite(point.y()) || !qIsFinite(point.z())) continue;
            for(int i=0;i<3;++i) { minimum[i]=qMin(minimum[i],point[i]); maximum[i]=qMax(maximum[i],point[i]); }
            ++count;
        }
    if(!count) return {};
    const float scale = 0.86f * qMin(size.width()/qMax(0.0001f,maximum.x()-minimum.x()),
        size.height()/qMax(0.0001f,maximum.y()-minimum.y()));
    const QVector3D center = (minimum+maximum)*0.5f;
    QVector<float> depth(size.width()*size.height(), -std::numeric_limits<float>::infinity());
    for(const auto& entry:triangles)
        {
            if(cancelled && cancelled->load())return {};
            const auto& triangle=entry.triangle;
            const QImage texture=textures.value(entry.textureIndex);
            QVector3D point[3]; bool finite = true;
            for(int i=0;i<3;++i)
            {
                const QVector3D rotated = rotation*triangle.vertices[i].position-center;
                point[i]=QVector3D(size.width()*0.5f+rotated.x()*scale,
                    size.height()*0.5f-rotated.y()*scale,rotated.z());
                finite = finite && qIsFinite(point[i].x()) && qIsFinite(point[i].y()) && qIsFinite(point[i].z());
            }
            if(!finite) continue;
            const float area=(point[1].y()-point[2].y())*(point[0].x()-point[2].x())
                +(point[2].x()-point[1].x())*(point[0].y()-point[2].y());
            if(qAbs(area)<0.001f) continue;
            const int left=qBound(0,int(std::floor(qMin(point[0].x(),qMin(point[1].x(),point[2].x())))),size.width()-1);
            const int right=qBound(0,int(std::ceil(qMax(point[0].x(),qMax(point[1].x(),point[2].x())))),size.width()-1);
            const int top=qBound(0,int(std::floor(qMin(point[0].y(),qMin(point[1].y(),point[2].y())))),size.height()-1);
            const int bottom=qBound(0,int(std::ceil(qMax(point[0].y(),qMax(point[1].y(),point[2].y())))),size.height()-1);
            const QVector3D normal=QVector3D::crossProduct(triangle.vertices[1].position-triangle.vertices[0].position,
                triangle.vertices[2].position-triangle.vertices[0].position).normalized();
            const float light=0.55f+0.45f*qAbs(QVector3D::dotProduct(normal,QVector3D(0.35f,-0.45f,0.82f).normalized()));
            for(int y=top;y<=bottom;++y) for(int x=left;x<=right;++x)
            {
                const float a=((point[1].y()-point[2].y())*(x+0.5f-point[2].x())
                    +(point[2].x()-point[1].x())*(y+0.5f-point[2].y()))/area;
                const float b=((point[2].y()-point[0].y())*(x+0.5f-point[2].x())
                    +(point[0].x()-point[2].x())*(y+0.5f-point[2].y()))/area;
                const float c=1-a-b;
                if(a<0 || b<0 || c<0) continue;
                const float z=a*point[0].z()+b*point[1].z()+c*point[2].z();
                if(z<=depth[y*size.width()+x]) continue;
                QRgb color=qRgb(165,174,185);
                if(!texture.isNull())
                {
                    const QVector2D uv=(a*triangle.vertices[0].uv+b*triangle.vertices[1].uv+c*triangle.vertices[2].uv)*entry.uvScale;
                    if(!qIsFinite(uv.x()) || !qIsFinite(uv.y())) continue;
                    const int tx=qBound(0,int((uv.x()-std::floor(uv.x()))*texture.width()),texture.width()-1);
                    const int ty=qBound(0,int((uv.y()-std::floor(uv.y()))*texture.height()),texture.height()-1);
                    color=texture.pixel(tx,ty);
                    if(qAlpha(color)<32) continue;
                }
                depth[y*size.width()+x]=z;
                image.setPixel(x,y,qRgb(int(qRed(color)*light),int(qGreen(color)*light),int(qBlue(color)*light)));
            }
        }
    return image;
}
QImage renderModelGalleryThumbnail(const SResourceModelScene& scene,const QImage& texture,
    const QString& model,float uvScale,const QSize& size) {
    if(!scene.valid)return {};
    const auto visible=defaultVisibleModelParts(scene,model);QVector<SGalleryTriangle> triangles;
    for(const auto& part:scene.parts)if(visible.contains(part.name.toLower()))for(const auto& triangle:part.triangles) {
        SGalleryTriangle entry;entry.triangle=triangle;entry.uvScale=uvScale;triangles.append(entry);
    }
    return renderGalleryTriangles(triangles,{texture},size);
}

}

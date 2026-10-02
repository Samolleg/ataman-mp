#ifndef MODEL_GALLERY_THUMBNAIL_H
#define MODEL_GALLERY_THUMBNAIL_H
#include "resource_model_preview.h"
#include <atomic>
namespace ei {
struct SGalleryTriangle { SResourceModelTriangle triangle; int textureIndex=0; float uvScale=1; };
QImage renderGalleryTriangles(const QVector<SGalleryTriangle>& triangles, const QVector<QImage>& textures,
    const QSize& size = QSize(192,132), const std::atomic_bool* cancelled = nullptr);
QImage renderModelGalleryThumbnail(const SResourceModelScene& scene, const QImage& texture,
    const QString& model, float uvScale = 1.0f, const QSize& size = QSize(192, 132));
}
#endif

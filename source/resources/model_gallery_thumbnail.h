#ifndef MODEL_GALLERY_THUMBNAIL_H
#define MODEL_GALLERY_THUMBNAIL_H
#include "resource_model_preview.h"
namespace ei {
QImage renderModelGalleryThumbnail(const SResourceModelScene& scene, const QImage& texture,
    const QString& model, float uvScale = 1.0f, const QSize& size = QSize(192, 132));
}
#endif

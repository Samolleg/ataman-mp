#ifndef GALLERY_THUMBNAIL_CACHE_H
#define GALLERY_THUMBNAIL_CACHE_H
#include <QImage>
#include <QString>
namespace ei {
// Bump when pixel generation, background, framing, or the on-disk layout changes.
constexpr int GalleryThumbnailFormat = 1;
constexpr qint64 GalleryThumbnailBudget = 128ll * 1024 * 1024;
QString galleryThumbnailDirectory();
QString galleryThumbnailKey(const QString& resourceNamespace, const QString& profile,
                            const QString& model, const QString& texture,
                            int format = GalleryThumbnailFormat);
bool readGalleryThumbnail(const QString& directory, const QString& key, QImage* image);
bool writeGalleryThumbnail(const QString& directory, const QString& key, const QImage& image,
                           qint64 budget = GalleryThumbnailBudget);
}
#endif

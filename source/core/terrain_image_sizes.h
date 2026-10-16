#ifndef TERRAIN_IMAGE_SIZES_H
#define TERRAIN_IMAGE_SIZES_H
#include <QImage>
#include <QVector>

// Native images remain authoritative for storage and identity. Only GPU and
// synthesis inputs use a common canvas; a small page must never downsample HD.
inline int terrainMaximumTileSize(const QVector<QImage>& images)
{
    int size = 0;
    for(const QImage& image : images) {
        if(image.isNull() || image.width() != image.height()) return 0;
        size = qMax(size, image.width());
    }
    return size;
}
inline QVector<QImage> terrainDisplayTiles(const QVector<QImage>& images)
{
    const int size = terrainMaximumTileSize(images);
    if(!size) return {};
    QVector<QImage> result = images;
    for(QImage& image : result)
        if(image.width() != size)
            image = image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return result;
}
#endif

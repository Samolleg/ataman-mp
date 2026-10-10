#ifndef TERRAIN_ATLAS_SHAPE_POLICY_H
#define TERRAIN_ATLAS_SHAPE_POLICY_H

#include <QImage>
#include <QVector>

// Native images remain authoritative for binding and persistence. Consumers
// that need a common raster size use the largest square, never the first/min.
inline QSize terrainAtlasMaximumNativeTileSize(const QVector<QImage>& images)
{
    int maximum = 0;
    for(const QImage& image : images)
    {
        if(image.isNull() || image.width() <= 0 || image.width() != image.height())
            return {};
        maximum = qMax(maximum, image.width());
    }
    return QSize(maximum, maximum);
}

inline QImage terrainAtlasRasterTile(const QImage& native, const QSize& size)
{
    return native.size() == size ? native : native.scaled(
        size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

inline bool terrainAtlasLogicalCountsFitPhysicalPages(
    int textureCount, int logicalTileCount, int physicalTileCount,
    int tileTypeCount)
{
    if(textureCount <= 0 || logicalTileCount <= 0
       || physicalTileCount != textureCount * 64
       || tileTypeCount != logicalTileCount)
        return false;
    // MMP pages are physical 8x8 containers.  The final page may contain
    // padding images that have no logical MP tile type; it must still be the
    // minimal page count for the logical atlas.
    return (logicalTileCount + 63) / 64 == textureCount;
}

#endif // TERRAIN_ATLAS_SHAPE_POLICY_H

#include "minimap_bake.h"

#include <QPainter>

SMinimapBakeResult bakeMinimapLayers(
    int sectorWidth, int sectorHeight,
    const QVector<SMinimapBakeLayer>& orderedLayers)
{
    SMinimapBakeResult result;
    if(sectorWidth <= 0 || sectorHeight <= 0 || sectorWidth > 4096 || sectorHeight > 4096)
    {
        result.error = "Positive, bounded sector dimensions are required.";
        return result;
    }
    const QSize reviewSize(sectorWidth * 128, sectorHeight * 128);
    QImage review(reviewSize, QImage::Format_ARGB32_Premultiplied);
    if(review.isNull())
    {
        result.error = "The full-resolution minimap review image could not be allocated.";
        return result;
    }
    review.fill(Qt::transparent);
    bool terrain = false;
    bool staticGeometry = false;
    QPainter painter(&review);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for(const SMinimapBakeLayer& layer : orderedLayers)
    {
        if(layer.kind == EMinimapBakeLayerKind::DynamicObject)
        {
            result.notes.append(QString("Excluded dynamic layer: %1").arg(layer.source));
            continue;
        }
        if(layer.image.size() != reviewSize)
        {
            result.error = QString("Bake layer %1 is %2x%3; expected %4x%5.")
                .arg(layer.source).arg(layer.image.width()).arg(layer.image.height())
                .arg(reviewSize.width()).arg(reviewSize.height());
            return result;
        }
        terrain = terrain || layer.kind == EMinimapBakeLayerKind::Terrain;
        staticGeometry = staticGeometry
            || layer.kind == EMinimapBakeLayerKind::StaticGeometry;
        painter.drawImage(0, 0, layer.image);
    }
    painter.end();
    if(!terrain)
    {
        result.error = "A terrain capture is required for a minimap bake.";
        return result;
    }
    result.incomplete = !staticGeometry;
    if(result.incomplete)
        result.notes.append("MPR-only bake: collection static geometry is absent.");
    result.reviewImage = review;
    result.gameImage = review.scaled(256, 256, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    result.success = !result.gameImage.isNull();
    return result;
}

#include "minimap_bake.h"

#include <QPainter>

QPointF minimapWorldToReviewPixel(const QPointF& worldPosition,
                                  int sectorWidth, int sectorHeight)
{
    if(sectorWidth <= 0 || sectorHeight <= 0)
        return QPointF();
    const double pixelsPerWorldUnit = 4.0; // 128 pixels / 32 world units.
    return QPointF(worldPosition.x() * pixelsPerWorldUnit,
                   worldPosition.y() * pixelsPerWorldUnit);
}

QRect minimapGameContentRect(int sectorWidth, int sectorHeight, int textureSize)
{
    if(sectorWidth <= 0 || sectorHeight <= 0 || textureSize <= 0)
        return QRect();
    const int largest = qMax(sectorWidth, sectorHeight);
    return QRect(0, 0,
        qMax(1, qRound(double(textureSize) * sectorWidth / largest)),
        qMax(1, qRound(double(textureSize) * sectorHeight / largest)));
}

SMinimapBakeResult bakeMinimapLayers(
    int sectorWidth, int sectorHeight,
    const QVector<SMinimapBakeLayer>& orderedLayers,
    const SMinimapBakeOptions& options)
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
        QImage normalized = layer.image;
        if(options.sourceOrientation == EMinimapCaptureOrientation::PositiveYUp)
            normalized = normalized.mirrored(false, true);
        painter.drawImage(0, 0, normalized);
    }
    painter.end();
    if(!terrain)
    {
        result.error = "A terrain capture is required for a minimap bake.";
        return result;
    }
    result.incomplete = !staticGeometry;
    if(result.incomplete)
        result.notes.append("MPR-only bake: no MOB static-geometry source is loaded.");
    result.reviewImage = review;
    result.gameContentRect = minimapGameContentRect(sectorWidth, sectorHeight);
    result.gameImage = QImage(256, 256, QImage::Format_ARGB32_Premultiplied);
    result.gameImage.fill(Qt::transparent);
    QPainter gamePainter(&result.gameImage);
    gamePainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    gamePainter.drawImage(result.gameContentRect, review);
    gamePainter.end();
    result.success = !result.gameImage.isNull();
    return result;
}

#include "minimap_bake.h"

#include <QPainter>
#include <QtMath>

QImage gradeMinimapImage(const QImage& unlitImage, const QImage& shadedImage,
                         const SMinimapGradeSettings& settings)
{
    if(shadedImage.isNull()) return QImage();
    const QImage unlit = unlitImage.size() == shadedImage.size()
        ? unlitImage.convertToFormat(QImage::Format_ARGB32)
        : shadedImage.convertToFormat(QImage::Format_ARGB32);
    const QImage shaded = shadedImage.convertToFormat(QImage::Format_ARGB32);
    QImage result(shaded.size(), QImage::Format_ARGB32);
    const double shade = qBound(0, settings.shadingPercent, 150) / 100.0;
    const double saturation = qBound(0, settings.saturationPercent, 200) / 100.0;
    const double contrast = qBound(0, settings.contrastPercent, 200) / 100.0;
    const double brightness = qBound(0, settings.brightnessPercent, 200) / 100.0;
    for(int y = 0; y < result.height(); ++y)
    {
        const QRgb* raw = reinterpret_cast<const QRgb*>(unlit.constScanLine(y));
        const QRgb* lit = reinterpret_cast<const QRgb*>(shaded.constScanLine(y));
        QRgb* output = reinterpret_cast<QRgb*>(result.scanLine(y));
        for(int x = 0; x < result.width(); ++x)
        {
            double channel[3] = {
                qRed(raw[x]) + (qRed(lit[x]) - qRed(raw[x])) * shade,
                qGreen(raw[x]) + (qGreen(lit[x]) - qGreen(raw[x])) * shade,
                qBlue(raw[x]) + (qBlue(lit[x]) - qBlue(raw[x])) * shade};
            QColor hueColor(qBound(0, qRound(channel[0]), 255),
                            qBound(0, qRound(channel[1]), 255),
                            qBound(0, qRound(channel[2]), 255));
            qreal hue = hueColor.hslHueF();
            if(hue >= 0.0 && settings.hueDegrees != 0)
            {
                hue += qBound(-180, settings.hueDegrees, 180) / 360.0;
                hue -= qFloor(hue);
                hueColor.setHslF(hue, hueColor.hslSaturationF(),
                                 hueColor.lightnessF());
                channel[0] = hueColor.red();
                channel[1] = hueColor.green();
                channel[2] = hueColor.blue();
            }
            const double luma = 0.2126 * channel[0] + 0.7152 * channel[1]
                + 0.0722 * channel[2];
            for(int component = 0; component < 3; ++component)
            {
                channel[component] = luma + (channel[component] - luma) * saturation;
                channel[component] = ((channel[component] / 255.0 - 0.5) * contrast
                                      + 0.5) * 255.0 * brightness;
                channel[component] = qBound(0.0, channel[component], 255.0);
            }
            output[x] = qRgba(qRound(channel[0]), qRound(channel[1]),
                              qRound(channel[2]), qAlpha(lit[x]));
        }
    }
    return result;
}

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
    if(options.textureSize < 64 || options.textureSize > 2048
       || (options.textureSize & (options.textureSize - 1)) != 0)
    {
        result.error = "A power-of-two minimap texture size from 64 to 2048 is required.";
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
    result.gameContentRect = minimapGameContentRect(
        sectorWidth, sectorHeight, options.textureSize);
    result.gameImage = QImage(options.textureSize, options.textureSize,
                              QImage::Format_ARGB32_Premultiplied);
    result.gameImage.fill(Qt::black);
    QPainter gamePainter(&result.gameImage);
    gamePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    gamePainter.drawImage(result.gameContentRect, review);
    gamePainter.end();
    result.success = !result.gameImage.isNull();
    return result;
}

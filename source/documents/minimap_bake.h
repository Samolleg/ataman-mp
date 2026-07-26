#ifndef MINIMAP_BAKE_H
#define MINIMAP_BAKE_H

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSizeF>
#include <QString>
#include <QVector>

enum class EMinimapBakeLayerKind { Terrain, Overlay, StaticGeometry, DynamicObject };

enum class EMinimapCaptureOrientation
{
    // The source image has world +Y toward its top edge. It is flipped into
    // the game's minimap UV convention, where world +Y advances downward.
    PositiveYUp,
    // The source already follows the game's top-left UV convention: world +X
    // points right and world +Y points down.
    PositiveYDown
};

struct SMinimapBakeLayer
{
    SMinimapBakeLayer() = default;
    SMinimapBakeLayer(EMinimapBakeLayerKind layerKind, const QString& layerSource,
                      const QImage& layerImage)
        : kind(layerKind), source(layerSource), image(layerImage) {}
    EMinimapBakeLayerKind kind{EMinimapBakeLayerKind::Terrain};
    QString source;
    QImage image;
};

struct SMinimapBakeResult
{
    bool success{false};
    bool incomplete{false};
    QString error;
    QStringList notes;
    QImage reviewImage;
    QImage gameImage;
    QRect gameContentRect;
};

struct SMinimapBakeOptions
{
    EMinimapCaptureOrientation sourceOrientation{
        EMinimapCaptureOrientation::PositiveYUp};
    int textureSize{256};
};

struct SMinimapGradeSettings
{
    SMinimapGradeSettings(int shading = 65, int saturation = 110,
                          int contrast = 100, int brightness = 106)
        : shadingPercent(shading), saturationPercent(saturation),
          contrastPercent(contrast), brightnessPercent(brightness) {}
    int shadingPercent;
    int saturationPercent;
    int contrastPercent;
    int brightnessPercent;
};

QImage gradeMinimapImage(const QImage& unlitImage, const QImage& shadedImage,
                         const SMinimapGradeSettings& settings);

// Fixed full-MPR framing used by both the renderer capture and synthetic
// orientation tests. The world rectangle starts at (0, 0), spans 32 units per
// sector, and maps to 128 review pixels per sector without cropping.
QPointF minimapWorldToReviewPixel(const QPointF& worldPosition,
                                  int sectorWidth, int sectorHeight);

// The game stores minimaps in a square texture but maps both world axes using
// the larger #maps dimension. Non-square maps therefore occupy a top-left
// active rectangle instead of being stretched to a square. Shipped maps use
// several texture sizes, so callers must retain the selected export profile.
QRect minimapGameContentRect(int sectorWidth, int sectorHeight,
                             int textureSize = 256);

SMinimapBakeResult bakeMinimapLayers(int sectorWidth, int sectorHeight,
                                     const QVector<SMinimapBakeLayer>& orderedLayers,
                                     const SMinimapBakeOptions& options = {});

#endif

#ifndef MINIMAP_BAKE_H
#define MINIMAP_BAKE_H

#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

enum class EMinimapBakeLayerKind { Terrain, Overlay, StaticGeometry, DynamicObject };

enum class EMinimapCaptureOrientation
{
    // Review images use the ordinary top-left QImage origin. World +X points
    // right and world +Y points toward the top of the image.
    PositiveYUp,
    // Accepted for renderer/readback adapters whose rows still point with
    // world +Y toward the bottom. The bake normalizes them before composition.
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
};

struct SMinimapBakeOptions
{
    EMinimapCaptureOrientation sourceOrientation{
        EMinimapCaptureOrientation::PositiveYUp};
};

// Fixed full-MPR framing used by both the renderer capture and synthetic
// orientation tests. The world rectangle starts at (0, 0), spans 32 units per
// sector, and maps to 128 review pixels per sector without cropping.
QPointF minimapWorldToReviewPixel(const QPointF& worldPosition,
                                  int sectorWidth, int sectorHeight);

SMinimapBakeResult bakeMinimapLayers(int sectorWidth, int sectorHeight,
                                     const QVector<SMinimapBakeLayer>& orderedLayers,
                                     const SMinimapBakeOptions& options = {});

#endif

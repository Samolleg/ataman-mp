#ifndef MINIMAP_BAKE_H
#define MINIMAP_BAKE_H

#include <QImage>
#include <QString>
#include <QVector>

enum class EMinimapBakeLayerKind { Terrain, Overlay, StaticGeometry, DynamicObject };

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

SMinimapBakeResult bakeMinimapLayers(int sectorWidth, int sectorHeight,
                                     const QVector<SMinimapBakeLayer>& orderedLayers);

#endif

#ifndef PREPARED_STATIC_VISUAL_H
#define PREPARED_STATIC_VISUAL_H
#include "types.h"
#include "render_material.h"
#include <QImage>
#include <QSharedPointer>
#include <QMap>

// Worker-owned values only. No GL handles, nodes or shared resource caches.
struct SPreparedStaticPart
{
    QString name;
    int textureNumber{0};
    QVector<SVertexData> vertices;
    QVector<SVertexData> distantVertices;
};
struct SPreparedStaticTexture
{
    QByteArray compressed;
    QVector<int> mipSizes;
    QImage image;
    int width{0}, height{0};
    quint32 format{0};
    ERenderMaterialClass material{ERenderMaterialClass::Conservative};
    bool valid() const { return !image.isNull() || !mipSizes.isEmpty(); }
};
struct SPreparedStaticVisual
{
    bool specializedVisual{false};
    QVector<SPreparedStaticPart> parts;
    SPreparedStaticTexture texture;
    QString key;
};
QVector<SVertexData> simplifyStaticMesh(const QVector<SVertexData>& vertices);
#endif

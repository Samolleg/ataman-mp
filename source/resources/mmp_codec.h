#ifndef MMP_CODEC_H
#define MMP_CODEC_H

#include <QByteArray>
#include <QString>
#include <QImage>
#include <QVector>

namespace ei {

// PNT3 stores premultiplied BGRA dwords separated by byte-counted transparent
// runs. Values 1..1,000,000 are run markers; zero and larger values are literal
// pixels. Shipped files may omit the final transparent run, so callers provide
// the exact decoded image size and this function pads it without reading past
// the payload.
bool decodePnt3(const QByteArray& compressed, int decodedSize,
                QByteArray* decoded, QString* diagnostic = nullptr);

// Decode a PNT3 payload into the row-major image orientation used by the
// original Direct3D renderer and EIrepack. The first decoded pixel is (0, 0);
// callers must not transpose or mirror individual armor layers.
bool decodePnt3Image(const QByteArray& compressed, int width, int height,
                     QImage* image, QString* diagnostic = nullptr);

// Decode the uncompressed DirectDraw MMP variants (5650/5551/4444/8888).
// Data begins at the 56-byte pixel descriptor after the common MMP dwords.
bool decodeDirectDrawImage(const QByteArray& descriptorAndPixels, int width, int height,
                           QImage* image, QString* diagnostic = nullptr);

// Compose the shared humanoid atlas with the original 2dintmmx.dll formula.
// PNT3 pixels are already premultiplied, so routing them through a generic
// source-over painter would premultiply translucent armor edges twice.
bool compositeTextureLayers(const QVector<QImage>& layers, QImage* composite,
                            QString* diagnostic = nullptr);

}

#endif // MMP_CODEC_H

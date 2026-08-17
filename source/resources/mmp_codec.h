#ifndef MMP_CODEC_H
#define MMP_CODEC_H

#include <QByteArray>
#include <QString>
#include <QImage>
#include <QIODevice>
#include <QMap>
#include <QSet>
#include <QVector>
#include <functional>

namespace ei {

enum class DxtFormat
{
    Dxt1,
    Dxt3,
    Dxt5
};

// Decode one authored DXT mip level. This is used only when the active GL
// implementation cannot accept EI's compressed MMP payloads (notably the
// bundled ANGLE/GLES2 renderer); native desktop GL keeps the original upload.
bool decodeDxtImage(const QByteArray& compressed, int width, int height,
                    DxtFormat format, QImage* image,
                    QString* diagnostic = nullptr);

// Decode one logical 8x8 terrain-atlas tile directly from the base DXT1 mip.
// This avoids expanding a 1024/2048-pixel page merely to build one material
// palette thumbnail. logicalSlot uses the MPR 0..63 tile ordering.
bool decodeMmpDxt1AtlasTile(const QByteArray& mmp, int logicalSlot,
                            QImage* image, QString* diagnostic = nullptr);

// Decode one logical terrain tile without reading the rest of a large RES
// member. payloadOffset/payloadSize identify the MMP member inside the device;
// only its header and the DXT block-row band containing logicalSlot are read.
bool decodeMmpDxt1AtlasTile(QIODevice* device, qint64 payloadOffset,
                            qint64 payloadSize, int logicalSlot,
                            QImage* image, QString* diagnostic = nullptr);

// Resolve one page payload at most once while a caller requests several
// palette tiles from the same authored MMP. Failed page lookups are cached as
// well, preventing every material-family fallback from repeating the same RES
// scan/read. The cache lifetime remains caller-owned and can therefore be
// bounded to one catalog rebuild.
using MmpPayloadLoader = std::function<bool(const QString&, QByteArray*)>;
bool decodeMmpDxt1AtlasTileCached(
    const QString& pageName, int logicalSlot,
    QMap<QString, QByteArray>* payloadCache,
    QSet<QString>* missingPageCache,
    const MmpPayloadLoader& loader,
    QImage* image, QString* diagnostic = nullptr);

// PNT3 stores four-pixel (16-byte) literal blocks separated by byte-counted
// transparent runs. Only the first dword of each block is a control candidate:
// non-zero with alpha zero is a run; otherwise the whole block is literal.
// Shipped files may omit the final transparent run, so callers provide the
// exact decoded image size and this function pads it without reading past the
// payload.
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

// Deterministic uncompressed 8888 writer used only after a review bake has
// passed orientation/framing validation. Pixels are emitted as BGRA matching
// the explicit masks in the 76-byte MMP descriptor.
QByteArray encodeMmp8888(const QImage& image, QString* diagnostic = nullptr);

// Deterministic terrain-page encoder. The 76-byte EI header is followed by a
// complete block-safe DXT1 mip chain. Alpha is preserved with DXT1's authored
// one-bit transparency mode; terrain pages with graded alpha must use another
// validated codec rather than being silently flattened.
QByteArray encodeMmpDxt1(const QImage& image, bool generateMipmaps = true,
                         QString* diagnostic = nullptr);

// Compose the shared humanoid atlas with the original 2dintmmx.dll formula.
// PNT3 pixels are already premultiplied, so routing them through a generic
// source-over painter would premultiply translucent armor edges twice.
bool compositeTextureLayers(const QVector<QImage>& layers, QImage* composite,
                            QString* diagnostic = nullptr);

}

#endif // MMP_CODEC_H

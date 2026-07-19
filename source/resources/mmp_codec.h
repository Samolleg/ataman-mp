#ifndef MMP_CODEC_H
#define MMP_CODEC_H

#include <QByteArray>
#include <QString>
#include <QImage>
#include <QVector>

namespace ei {

// PNT3 stores literal BGRA dwords separated by byte-counted transparent runs.
// Shipped files may omit the final transparent run, so callers provide the
// exact decoded image size and this function pads it without reading past the
// compressed payload.
bool decodePnt3(const QByteArray& compressed, int decodedSize,
                QByteArray* decoded, QString* diagnostic = nullptr);

// Source-over composition for the shared humanoid body atlas. Every input
// must have identical dimensions; transparent armor pixels reveal lower
// clothing/skin instead of making the assembled body transparent.
bool compositeTextureLayers(const QVector<QImage>& layers, QImage* composite,
                            QString* diagnostic = nullptr);

}

#endif // MMP_CODEC_H

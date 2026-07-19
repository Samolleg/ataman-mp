#ifndef MMP_CODEC_H
#define MMP_CODEC_H

#include <QByteArray>
#include <QString>

namespace ei {

// PNT3 stores literal BGRA dwords separated by byte-counted transparent runs.
// Shipped files may omit the final transparent run, so callers provide the
// exact decoded image size and this function pads it without reading past the
// compressed payload.
bool decodePnt3(const QByteArray& compressed, int decodedSize,
                QByteArray* decoded, QString* diagnostic = nullptr);

}

#endif // MMP_CODEC_H

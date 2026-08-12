#ifndef MMP_INSPECTOR_H
#define MMP_INSPECTOR_H

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

namespace ei {

struct SMmpInspection
{
    bool valid{false};
    QString diagnostic;
    QString formatName;
    quint32 format{0};
    int width{0};
    int height{0};
    int headerMipValue{0};
    QVector<QSize> authoredMipSizes;
    QByteArray sha256;
    QImage baseImage;
};

// Validates and decodes an MMP without creating an OpenGL texture. This is the
// read-only path used by minimap review and reports the PNT3 header field as a
// compressed byte count rather than inventing a mip chain for it.
SMmpInspection inspectMmp(const QByteArray& bytes);

// MMP decoders preserve game-row orientation. Conventional Qt image widgets
// display top-left rows, so previews use the same vertical adapter as other
// editor MMP/minimap review surfaces without changing payload or UV semantics.
QImage mmpEditorPreviewImage(const QImage& decodedGameRows);

}

#endif // MMP_INSPECTOR_H

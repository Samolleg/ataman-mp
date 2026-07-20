#include "mmp_codec.h"

#include <QtEndian>
#include <QPainter>
#include <limits>

namespace ei {

bool decodePnt3(const QByteArray& compressed, int decodedSize,
                QByteArray* decoded, QString* diagnostic)
{
    if(!decoded || decodedSize < 0)
    {
        if(diagnostic) *diagnostic = "Invalid PNT3 output request.";
        return false;
    }
    decoded->clear();
    decoded->reserve(decodedSize);
    int cursor = 0;
    int literalStart = 0;
    while(cursor + int(sizeof(quint32)) <= compressed.size())
    {
        const quint32 value = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(compressed.constData() + cursor));
        cursor += int(sizeof(quint32));
        // PNT3's original decoder treats zero and values above one million as
        // literal BGRA dwords. Every other value is a transparent-byte run;
        // run lengths are not required to be 16-byte aligned.
        if(value > 1000000u || value == 0u)
            continue;

        const int literalEnd = cursor - int(sizeof(quint32));
        const int literalSize = literalEnd - literalStart;
        if(literalEnd < literalStart || decoded->size() > decodedSize - literalSize)
        {
            if(diagnostic) *diagnostic = "PNT3 literal data exceeds the decoded image bounds.";
            decoded->clear();
            return false;
        }
        if(value > quint32(decodedSize - decoded->size() - literalSize))
        {
            if(diagnostic) *diagnostic = "PNT3 transparent run exceeds the decoded image bounds.";
            decoded->clear();
            return false;
        }
        decoded->append(compressed.constData() + literalStart, literalSize);
        decoded->append(QByteArray(int(value), '\0'));
        literalStart = cursor;
    }
    if(cursor != compressed.size())
    {
        if(diagnostic) *diagnostic = "PNT3 payload is not dword-aligned.";
        decoded->clear();
        return false;
    }
    if(decoded->size() > decodedSize - (cursor - literalStart))
    {
        if(diagnostic) *diagnostic = "PNT3 literal data exceeds the decoded image bounds.";
        decoded->clear();
        return false;
    }
    decoded->append(compressed.constData() + literalStart, cursor - literalStart);
    decoded->append(QByteArray(decodedSize - decoded->size(), '\0'));
    return true;
}

bool decodePnt3Image(const QByteArray& compressed, int width, int height,
                     QImage* image, QString* diagnostic)
{
    const qint64 decodedSize = qint64(width) * qint64(height) * 4;
    if(!image || width <= 0 || height <= 0 || decodedSize > std::numeric_limits<int>::max())
    {
        if(diagnostic) *diagnostic = "Invalid PNT3 image dimensions.";
        return false;
    }

    QByteArray decoded;
    if(!decodePnt3(compressed, int(decodedSize), &decoded, diagnostic))
        return false;

    QImage result(width, height, QImage::Format_RGBA8888);
    int cursor = 0;
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            result.setPixelColor(x, y, QColor(uchar(decoded[cursor + 2]),
                                               uchar(decoded[cursor + 1]),
                                               uchar(decoded[cursor]),
                                               uchar(decoded[cursor + 3])));
            cursor += 4;
        }
    *image = result;
    return true;
}

bool compositeTextureLayers(const QVector<QImage>& layers, QImage* composite,
                            QString* diagnostic)
{
    if(!composite || layers.isEmpty() || layers.first().isNull())
    {
        if(diagnostic) *diagnostic = "Texture composition requires a non-empty base layer.";
        return false;
    }
    const QSize size = layers.first().size();
    *composite = QImage(size, QImage::Format_ARGB32_Premultiplied);
    composite->fill(Qt::transparent);
    QPainter painter(composite);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for(const QImage& layer : layers)
    {
        if(layer.isNull() || layer.size() != size)
        {
            painter.end();
            composite->fill(Qt::transparent);
            if(diagnostic) *diagnostic = "Texture layers have inconsistent dimensions.";
            return false;
        }
        painter.drawImage(0, 0, layer);
    }
    painter.end();
    return true;
}

}

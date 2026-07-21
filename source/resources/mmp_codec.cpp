#include "mmp_codec.h"

#include <QtEndian>
#include <cstring>
#include <limits>

namespace ei {

namespace {
quint32 readDword(const QByteArray& data, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(data.constData() + offset));
}

uchar expandMaskedChannel(quint32 pixel, quint32 mask, quint32 shift)
{
    if(mask == 0)
        return 255;
    const quint32 maximum = mask >> shift;
    return maximum == 0 ? 0 : uchar((255u * ((pixel & mask) >> shift) + maximum / 2u) / maximum);
}
}

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
    while(cursor + int(sizeof(quint32)) <= compressed.size())
    {
        const quint32 value = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(compressed.constData() + cursor));
        cursor += int(sizeof(quint32));
        // 2dintmmx.dll's PNT3 surface unpacker identifies a transparent run
        // by a zero alpha byte and a non-zero dword. The value is a byte
        // count. Literal pixels are premultiplied BGRA, including zero. Runs
        // are consumed in four-pixel groups by the original MMX routine.
        const bool transparentRun = value != 0u && (value & 0xff000000u) == 0u;
        if(transparentRun)
        {
            if((value & 0x0fu) != 0u)
            {
                if(diagnostic) *diagnostic = "PNT3 transparent run is not four-pixel aligned.";
                decoded->clear();
                return false;
            }
            if(value > quint32(decodedSize - decoded->size()))
            {
                if(diagnostic) *diagnostic = "PNT3 transparent run exceeds the decoded image bounds.";
                decoded->clear();
                return false;
            }
            decoded->append(QByteArray(int(value), '\0'));
            continue;
        }
        if(decoded->size() > decodedSize - int(sizeof(quint32)))
        {
            if(diagnostic) *diagnostic = "PNT3 literal data exceeds the decoded image bounds.";
            decoded->clear();
            return false;
        }
        decoded->append(compressed.constData() + cursor - int(sizeof(quint32)),
                        int(sizeof(quint32)));
    }
    if(cursor != compressed.size())
    {
        if(diagnostic) *diagnostic = "PNT3 payload is not dword-aligned.";
        decoded->clear();
        return false;
    }
    if(decoded->size() > decodedSize)
    {
        if(diagnostic) *diagnostic = "PNT3 data exceeds the decoded image bounds.";
        decoded->clear();
        return false;
    }
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

    // On little-endian platforms ARGB32_Premultiplied is stored as BGRA,
    // exactly matching the bytes consumed by the game's MMX compositor.
    QImage result(width, height, QImage::Format_ARGB32_Premultiplied);
    for(int y = 0; y < height; ++y)
        memcpy(result.scanLine(y), decoded.constData() + y * width * 4,
               size_t(width) * 4u);
    *image = result;
    return true;
}

bool decodeDirectDrawImage(const QByteArray& data, int width, int height,
                           QImage* image, QString* diagnostic)
{
    constexpr int descriptorBytes = 56;
    if(!image || width <= 0 || height <= 0 || data.size() < descriptorBytes)
    {
        if(diagnostic) *diagnostic = "Invalid DirectDraw MMP image request.";
        return false;
    }
    const quint32 bitsPerPixel = readDword(data, 0);
    if(bitsPerPixel != 16 && bitsPerPixel != 32)
    {
        if(diagnostic) *diagnostic = "Unsupported DirectDraw MMP pixel size.";
        return false;
    }
    const int bytesPerPixel = int(bitsPerPixel / 8u);
    const qint64 pixelBytes = qint64(width) * qint64(height) * bytesPerPixel;
    if(pixelBytes > std::numeric_limits<int>::max()
            || data.size() < descriptorBytes + int(pixelBytes))
    {
        if(diagnostic) *diagnostic = "DirectDraw MMP pixel payload is truncated.";
        return false;
    }
    const quint32 alphaMask = readDword(data, 4);
    const quint32 alphaShift = readDword(data, 8);
    const quint32 redMask = readDword(data, 16);
    const quint32 redShift = readDword(data, 20);
    const quint32 greenMask = readDword(data, 28);
    const quint32 greenShift = readDword(data, 32);
    const quint32 blueMask = readDword(data, 40);
    const quint32 blueShift = readDword(data, 44);

    QImage result(width, height, QImage::Format_ARGB32);
    int cursor = descriptorBytes;
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const quint32 pixel = bytesPerPixel == 2
                ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + cursor))
                : readDword(data, cursor);
            cursor += bytesPerPixel;
            const int alpha = alphaMask ? expandMaskedChannel(pixel, alphaMask, alphaShift) : 255;
            result.setPixelColor(x, y, QColor(
                expandMaskedChannel(pixel, redMask, redShift),
                expandMaskedChannel(pixel, greenMask, greenShift),
                expandMaskedChannel(pixel, blueMask, blueShift), alpha));
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
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    for(const QImage& layer : layers)
    {
        if(layer.isNull() || layer.size() != size)
        {
            composite->fill(Qt::transparent);
            if(diagnostic) *diagnostic = "Texture layers have inconsistent dimensions.";
            return false;
        }
        const QImage source = layer.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for(int y = 0; y < size.height(); ++y)
        {
            const uchar* src = source.constScanLine(y);
            uchar* dst = result.scanLine(y);
            for(int byte = 0; byte < size.width() * 4; byte += 4)
            {
                const uint inverseAlpha = 255u - src[byte + 3];
                for(int channel = 0; channel < 4; ++channel)
                {
                    const uint blended = uint(src[byte + channel])
                        + ((uint(dst[byte + channel]) * inverseAlpha) >> 8);
                    dst[byte + channel] = uchar(qMin(255u, blended));
                }
            }
        }
    }
    *composite = result;
    return true;
}

}

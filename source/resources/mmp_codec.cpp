#include "mmp_codec.h"

#include <QtEndian>
#include <QtMath>
#include <QDataStream>
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

QRgb decodeRgb565(quint16 value)
{
    const int red = ((value >> 11) & 31) * 255 / 31;
    const int green = ((value >> 5) & 63) * 255 / 63;
    const int blue = (value & 31) * 255 / 31;
    return qRgba(red, green, blue, 255);
}

QRgb interpolate(QRgb first, QRgb second, int firstWeight, int secondWeight,
                  int divisor, int alpha = 255)
{
    return qRgba((qRed(first) * firstWeight + qRed(second) * secondWeight) / divisor,
                 (qGreen(first) * firstWeight + qGreen(second) * secondWeight) / divisor,
                 (qBlue(first) * firstWeight + qBlue(second) * secondWeight) / divisor,
                 alpha);
}

void decodeColorBlock(const uchar* block, bool allowDxt1Transparency,
                      QRgb colors[4], quint32* indices)
{
    const quint16 color0 = qFromLittleEndian<quint16>(block);
    const quint16 color1 = qFromLittleEndian<quint16>(block + 2);
    colors[0] = decodeRgb565(color0);
    colors[1] = decodeRgb565(color1);
    if(!allowDxt1Transparency || color0 > color1)
    {
        colors[2] = interpolate(colors[0], colors[1], 2, 1, 3);
        colors[3] = interpolate(colors[0], colors[1], 1, 2, 3);
    }
    else
    {
        colors[2] = interpolate(colors[0], colors[1], 1, 1, 2);
        colors[3] = qRgba(0, 0, 0, 0);
    }
    *indices = qFromLittleEndian<quint32>(block + 4);
}
}

bool decodeDxtImage(const QByteArray& compressed, int width, int height,
                    DxtFormat format, QImage* image, QString* diagnostic)
{
    if(!image || width <= 0 || height <= 0)
    {
        if(diagnostic) *diagnostic = "Invalid DXT image dimensions.";
        return false;
    }
    const int blockBytes = format == DxtFormat::Dxt1 ? 8 : 16;
    const qint64 blocksWide = (qint64(width) + 3) / 4;
    const qint64 blocksHigh = (qint64(height) + 3) / 4;
    const qint64 required = blocksWide * blocksHigh * blockBytes;
    if(required > std::numeric_limits<int>::max() || compressed.size() < required)
    {
        if(diagnostic) *diagnostic = "DXT mip payload is truncated.";
        return false;
    }

    QImage result(width, height, QImage::Format_ARGB32);
    const uchar* source = reinterpret_cast<const uchar*>(compressed.constData());
    for(int blockY = 0; blockY < blocksHigh; ++blockY)
        for(int blockX = 0; blockX < blocksWide; ++blockX)
        {
            const uchar* block = source + (blockY * blocksWide + blockX) * blockBytes;
            uchar alphaValues[8] = {255, 255, 255, 255, 255, 255, 255, 255};
            quint64 alphaIndices = 0;
            const uchar* colorBlock = block;
            if(format == DxtFormat::Dxt3)
            {
                alphaIndices = qFromLittleEndian<quint64>(block);
                colorBlock += 8;
            }
            else if(format == DxtFormat::Dxt5)
            {
                alphaValues[0] = block[0];
                alphaValues[1] = block[1];
                if(alphaValues[0] > alphaValues[1])
                    for(int i = 1; i <= 6; ++i)
                        alphaValues[i + 1] = uchar(((7 - i) * int(alphaValues[0])
                                                   + i * int(alphaValues[1])) / 7);
                else
                {
                    for(int i = 1; i <= 4; ++i)
                        alphaValues[i + 1] = uchar(((5 - i) * int(alphaValues[0])
                                                   + i * int(alphaValues[1])) / 5);
                    alphaValues[6] = 0;
                    alphaValues[7] = 255;
                }
                for(int byte = 0; byte < 6; ++byte)
                    alphaIndices |= quint64(block[2 + byte]) << (8 * byte);
                colorBlock += 8;
            }

            QRgb colors[4];
            quint32 colorIndices = 0;
            decodeColorBlock(colorBlock, format == DxtFormat::Dxt1,
                             colors, &colorIndices);
            for(int pixel = 0; pixel < 16; ++pixel)
            {
                const int x = blockX * 4 + pixel % 4;
                const int y = blockY * 4 + pixel / 4;
                if(x >= width || y >= height)
                    continue;
                const QRgb color = colors[(colorIndices >> (2 * pixel)) & 3u];
                int alpha = qAlpha(color);
                if(format == DxtFormat::Dxt3)
                    alpha = int((alphaIndices >> (4 * pixel)) & 0xfu) * 17;
                else if(format == DxtFormat::Dxt5)
                    alpha = alphaValues[(alphaIndices >> (3 * pixel)) & 7u];
                result.setPixel(x, y, qRgba(qRed(color), qGreen(color),
                                            qBlue(color), alpha));
            }
        }
    *image = result;
    return true;
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
    constexpr int literalBlockBytes = 4 * int(sizeof(quint32));
    while(cursor + int(sizeof(quint32)) <= compressed.size())
    {
        const quint32 value = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(compressed.constData() + cursor));
        // 2dintmmx.dll (SHA-256 7FBBF945...8423B7A) processes PNT3 at
        // 0x100018AF in four-pixel blocks. Only the first dword of a block is
        // tested: non-zero with alpha zero is a byte-counted transparent run;
        // otherwise all 16 bytes are literal premultiplied BGRA pixels. Thus
        // zero-alpha color dwords in positions 1..3 are data, not markers.
        // UnhumaPL_05 controls the large-run case; UnhumaPL_24 controls the
        // literal-block case that a per-dword parser corrupts.
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
            cursor += int(sizeof(quint32));
            continue;
        }
        if(cursor > compressed.size() - literalBlockBytes)
        {
            if(diagnostic) *diagnostic = "PNT3 literal block is truncated.";
            decoded->clear();
            return false;
        }
        if(decoded->size() > decodedSize - literalBlockBytes)
        {
            if(diagnostic) *diagnostic = "PNT3 literal data exceeds the decoded image bounds.";
            decoded->clear();
            return false;
        }
        decoded->append(compressed.constData() + cursor, literalBlockBytes);
        cursor += literalBlockBytes;
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

QByteArray encodeMmp8888(const QImage& image, QString* diagnostic)
{
    if(image.isNull() || image.width() <= 0 || image.height() <= 0)
    {
        if(diagnostic) *diagnostic = "A non-empty image is required for 8888 MMP encoding.";
        return {};
    }
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x00504d4d) << quint32(source.width()) << quint32(source.height())
           << quint32(1) << quint32(0x00008888) << quint32(32)
           << quint32(0xff000000) << quint32(24) << quint32(8)
           << quint32(0x00ff0000) << quint32(16) << quint32(8)
           << quint32(0x0000ff00) << quint32(8) << quint32(8)
           << quint32(0x000000ff) << quint32(0) << quint32(8) << quint32(0);
    for(int y = 0; y < source.height(); ++y)
        for(int x = 0; x < source.width(); ++x)
            stream << quint32(source.pixel(x, y));
    return result;
}

namespace {
quint16 rgb565(int r, int g, int b)
{
    return quint16(((r * 31 + 127) / 255) << 11
        | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}

QRgb expand565(quint16 value)
{
    const int r = (value >> 11) & 31;
    const int g = (value >> 5) & 63;
    const int b = value & 31;
    return qRgb((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                (b << 3) | (b >> 2));
}

int colorError(QRgb a, QRgb b)
{
    const int dr = qRed(a) - qRed(b);
    const int dg = qGreen(a) - qGreen(b);
    const int db = qBlue(a) - qBlue(b);
    return dr * dr + dg * dg + db * db;
}
}

QByteArray encodeMmpDxt1(const QImage& image, QString* diagnostic)
{
    if(image.isNull() || image.width() <= 0 || image.height() <= 0
       || (image.width() & 3) != 0 || (image.height() & 3) != 0)
    {
        if(diagnostic) *diagnostic = "DXT1 encoding requires non-empty dimensions divisible by four.";
        return {};
    }
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x00504d4d) << quint32(source.width())
           << quint32(source.height()) << quint32(1) << quint32(0x31545844);
    // EI's DXT reader still consumes the legacy DirectDraw RGB565 descriptor.
    // A zero-filled tail decodes in our structural inspector but is rejected
    // by the game texture path and produces an invisible minimap.
    stream << quint32(4) << quint32(0x00008000) << quint32(15)
           << quint32(1) << quint32(0x00007c00) << quint32(10)
           << quint32(5) << quint32(0x000003e0) << quint32(5)
           << quint32(5) << quint32(0x0000001f) << quint32(0)
           << quint32(5) << quint32(0);

    for(int by = 0; by < source.height(); by += 4)
        for(int bx = 0; bx < source.width(); bx += 4)
        {
            QRgb pixels[16];
            bool transparent = false;
            int opaqueCount = 0, sumR = 0, sumG = 0, sumB = 0;
            QRgb low = qRgb(0, 0, 0), high = qRgb(0, 0, 0);
            for(int py = 0; py < 4; ++py)
            {
                const QRgb* row = reinterpret_cast<const QRgb*>(source.constScanLine(by + py));
                for(int px = 0; px < 4; ++px)
                {
                    const QRgb pixel = row[bx + px];
                    pixels[py * 4 + px] = pixel;
                    if(qAlpha(pixel) < 128) { transparent = true; continue; }
                    ++opaqueCount;
                    sumR += qRed(pixel); sumG += qGreen(pixel); sumB += qBlue(pixel);
                }
            }
            if(opaqueCount > 0)
            {
                const double mean[3] = {double(sumR) / opaqueCount,
                    double(sumG) / opaqueCount, double(sumB) / opaqueCount};
                double covariance[3][3] = {};
                for(int i = 0; i < 16; ++i)
                {
                    if(qAlpha(pixels[i]) < 128) continue;
                    const double delta[3] = {qRed(pixels[i]) - mean[0],
                        qGreen(pixels[i]) - mean[1], qBlue(pixels[i]) - mean[2]};
                    for(int row = 0; row < 3; ++row)
                        for(int column = 0; column < 3; ++column)
                            covariance[row][column] += delta[row] * delta[column];
                }
                double axis[3] = {1.0, 1.0, 1.0};
                for(int iteration = 0; iteration < 4; ++iteration)
                {
                    double next[3] = {};
                    for(int row = 0; row < 3; ++row)
                        for(int column = 0; column < 3; ++column)
                            next[row] += covariance[row][column] * axis[column];
                    const double length = qSqrt(next[0]*next[0] + next[1]*next[1]
                                                + next[2]*next[2]);
                    if(length < 0.0001) break;
                    for(int channel = 0; channel < 3; ++channel)
                        axis[channel] = next[channel] / length;
                }
                double minimum = 1.0e30, maximum = -1.0e30;
                for(int i = 0; i < 16; ++i)
                {
                    if(qAlpha(pixels[i]) < 128) continue;
                    const double projection = (qRed(pixels[i]) - mean[0]) * axis[0]
                        + (qGreen(pixels[i]) - mean[1]) * axis[1]
                        + (qBlue(pixels[i]) - mean[2]) * axis[2];
                    if(projection < minimum) { minimum = projection; low = pixels[i]; }
                    if(projection > maximum) { maximum = projection; high = pixels[i]; }
                }
            }
            quint16 c0 = rgb565(qRed(high), qGreen(high), qBlue(high));
            quint16 c1 = rgb565(qRed(low), qGreen(low), qBlue(low));
            if(opaqueCount == 0) c0 = c1 = 0;
            if(transparent)
            {
                if(c0 > c1) qSwap(c0, c1);
            }
            else
            {
                if(c0 <= c1) qSwap(c0, c1);
                if(c0 == c1) c0 = c0 == 0xffff ? quint16(c0 - 1) : quint16(c0 + 1);
                if(c0 <= c1) qSwap(c0, c1);
            }
            QRgb palette[4] = {expand565(c0), expand565(c1), 0, 0};
            if(c0 > c1)
            {
                palette[2] = qRgb((2*qRed(palette[0])+qRed(palette[1]))/3,
                    (2*qGreen(palette[0])+qGreen(palette[1]))/3,
                    (2*qBlue(palette[0])+qBlue(palette[1]))/3);
                palette[3] = qRgb((qRed(palette[0])+2*qRed(palette[1]))/3,
                    (qGreen(palette[0])+2*qGreen(palette[1]))/3,
                    (qBlue(palette[0])+2*qBlue(palette[1]))/3);
            }
            else
            {
                palette[2] = qRgb((qRed(palette[0])+qRed(palette[1]))/2,
                    (qGreen(palette[0])+qGreen(palette[1]))/2,
                    (qBlue(palette[0])+qBlue(palette[1]))/2);
                palette[3] = qRgba(0, 0, 0, 0);
            }
            quint32 indices = 0;
            for(int i = 0; i < 16; ++i)
            {
                int best = transparent && qAlpha(pixels[i]) < 128 ? 3 : 0;
                int bestError = best == 3 ? 0 : std::numeric_limits<int>::max();
                const int paletteCount = c0 > c1 ? 4 : 3;
                for(int candidate = 0; candidate < paletteCount && bestError; ++candidate)
                {
                    const int error = colorError(pixels[i], palette[candidate]);
                    if(error < bestError) { bestError = error; best = candidate; }
                }
                indices |= quint32(best) << (2 * i);
            }
            stream << c0 << c1 << indices;
        }
    return result;
}

}

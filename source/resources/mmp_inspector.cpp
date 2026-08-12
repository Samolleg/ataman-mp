#include "mmp_inspector.h"

#include <QCryptographicHash>
#include <QtEndian>
#include <limits>

#include "mmp_codec.h"

namespace ei {
namespace {
constexpr quint32 kSignature = 0x00504d4d;
constexpr int kCommonHeaderSize = 76;
constexpr int kDirectDrawDescriptorOffset = 20;
constexpr quint32 kDxt1 = 0x31545844;
constexpr quint32 kDxt2 = 0x32545844;
constexpr quint32 kDxt3 = 0x33545844;
constexpr quint32 kDxt4 = 0x34545844;
constexpr quint32 kDxt5 = 0x35545844;
constexpr quint32 kPnt3 = 0x33544e50;
constexpr quint32 k5650 = 0x00005650;
constexpr quint32 k5551 = 0x00005551;
constexpr quint32 k4444 = 0x00004444;
constexpr quint32 k8888 = 0x00008888;

quint32 dword(const QByteArray& bytes, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

QString formatName(quint32 format)
{
    switch(format)
    {
    case kDxt1: return QStringLiteral("DXT1");
    case kDxt2: return QStringLiteral("DXT2");
    case kDxt3: return QStringLiteral("DXT3");
    case kDxt4: return QStringLiteral("DXT4");
    case kDxt5: return QStringLiteral("DXT5");
    case kPnt3: return QStringLiteral("PNT3");
    case k5650: return QStringLiteral("5650");
    case k5551: return QStringLiteral("5551");
    case k4444: return QStringLiteral("4444");
    case k8888: return QStringLiteral("8888");
    default: return QStringLiteral("unknown (0x%1)").arg(format, 8, 16, QLatin1Char('0'));
    }
}

bool checkedImageSize(quint32 width, quint32 height)
{
    return width > 0 && height > 0
        && width <= quint32(std::numeric_limits<int>::max())
        && height <= quint32(std::numeric_limits<int>::max())
        && quint64(width) * quint64(height) * 4u <= quint64(std::numeric_limits<int>::max());
}
}

SMmpInspection inspectMmp(const QByteArray& bytes)
{
    SMmpInspection result;
    result.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    if(bytes.size() < 20)
    {
        result.diagnostic = QStringLiteral("MMP common header is truncated.");
        return result;
    }
    if(dword(bytes, 0) != kSignature)
    {
        result.diagnostic = QStringLiteral("MMP signature is invalid.");
        return result;
    }
    const quint32 width = dword(bytes, 4);
    const quint32 height = dword(bytes, 8);
    const quint32 mipValue = dword(bytes, 12);
    result.format = dword(bytes, 16);
    result.formatName = formatName(result.format);
    if(!checkedImageSize(width, height))
    {
        result.diagnostic = QStringLiteral("MMP dimensions are invalid or unsafe.");
        return result;
    }
    result.width = int(width);
    result.height = int(height);
    result.headerMipValue = int(mipValue);

    if(result.format == kPnt3)
    {
        if(bytes.size() < kCommonHeaderSize || mipValue > quint32(bytes.size() - kCommonHeaderSize))
        {
            result.diagnostic = QStringLiteral("PNT3 compressed payload is truncated.");
            return result;
        }
        if(!decodePnt3Image(bytes.mid(kCommonHeaderSize, int(mipValue)), result.width,
                            result.height, &result.baseImage, &result.diagnostic))
            return result;
        result.authoredMipSizes.append(QSize(result.width, result.height));
    }
    else if(result.format == kDxt1 || result.format == kDxt2 || result.format == kDxt3
            || result.format == kDxt4 || result.format == kDxt5)
    {
        if(bytes.size() < kCommonHeaderSize || mipValue == 0)
        {
            result.diagnostic = QStringLiteral("DXT MMP has no complete authored mip chain.");
            return result;
        }
        const int blockBytes = result.format == kDxt1 ? 8 : 16;
        qint64 required = kCommonHeaderSize;
        int mipWidth = result.width;
        int mipHeight = result.height;
        for(quint32 level = 0; level < mipValue; ++level)
        {
            result.authoredMipSizes.append(QSize(mipWidth, mipHeight));
            required += qint64(qMax(1, (mipWidth + 3) / 4))
                * qint64(qMax(1, (mipHeight + 3) / 4)) * blockBytes;
            mipWidth = qMax(1, mipWidth / 2);
            mipHeight = qMax(1, mipHeight / 2);
        }
        if(required > bytes.size())
        {
            result.diagnostic = QStringLiteral("DXT authored mip payload is truncated.");
            return result;
        }
        const DxtFormat codec = result.format == kDxt1 ? DxtFormat::Dxt1
            : (result.format == kDxt2 || result.format == kDxt3)
                ? DxtFormat::Dxt3 : DxtFormat::Dxt5;
        const int baseSize = qMax(1, (result.width + 3) / 4)
            * qMax(1, (result.height + 3) / 4) * blockBytes;
        if(!decodeDxtImage(bytes.mid(kCommonHeaderSize, baseSize), result.width,
                           result.height, codec, &result.baseImage, &result.diagnostic))
            return result;
    }
    else if(result.format == k5650 || result.format == k5551
            || result.format == k4444 || result.format == k8888)
    {
        if(!decodeDirectDrawImage(bytes.mid(kDirectDrawDescriptorOffset), result.width,
                                  result.height, &result.baseImage, &result.diagnostic))
            return result;
        result.authoredMipSizes.append(QSize(result.width, result.height));
    }
    else
    {
        result.diagnostic = QStringLiteral("Unsupported MMP format %1.").arg(result.formatName);
        return result;
    }
    result.valid = true;
    return result;
}

QImage mmpEditorPreviewImage(const QImage& decodedGameRows)
{
    return decodedGameRows.mirrored(false, true);
}

}

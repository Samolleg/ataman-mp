#include "terrain_virtual_atlas.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <QTransform>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "endurance_document.h"
#include "mmp_codec.h"
#include "mpr_document.h"
#include "res_archive_document.h"

namespace {
QByteArray imageHash(const QImage& image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint32 width = rgba.width();
    const qint32 height = rgba.height();
    hash.addData(reinterpret_cast<const char*>(&width), sizeof(width));
    hash.addData(reinterpret_cast<const char*>(&height), sizeof(height));
    for(int y = 0; y < rgba.height(); ++y)
        hash.addData(reinterpret_cast<const char*>(rgba.constScanLine(y)),
                     rgba.bytesPerLine());
    return hash.result().toHex();
}

QVector<qint32> corners(const STerrainTransitionSignature& signature)
{
    return {signature.northWest, signature.northEast,
            signature.southEast, signature.southWest};
}

QByteArray typedPixelKey(const QByteArray& pixelHash, qint32 runtimeType,
                         const STerrainTransitionSignature& signature)
{
    QByteArray key = pixelHash;
    char bytes[4];
    qToLittleEndian<qint32>(runtimeType, reinterpret_cast<uchar*>(bytes));
    key.append(bytes, 4);
    for(qint32 material : corners(signature))
    {
        qToLittleEndian<qint32>(material, reinterpret_cast<uchar*>(bytes));
        key.append(bytes, 4);
    }
    return key;
}

bool signatureLess(const STerrainTransitionSignature& left,
                   const STerrainTransitionSignature& right)
{
    return corners(left) < corners(right);
}

quint32 terrainNoiseHash(quint32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

int terrainBlendStylePriority(ETerrainBlendStyle style)
{
    switch(style)
    {
    case ETerrainBlendStyle::Water: return 110;
    case ETerrainBlendStyle::Snow: return 100;
    case ETerrainBlendStyle::Grass: return 90;
    case ETerrainBlendStyle::Sand: return 80;
    case ETerrainBlendStyle::Gravel: return 70;
    case ETerrainBlendStyle::Lava: return 60;
    case ETerrainBlendStyle::Ice: return 50;
    case ETerrainBlendStyle::Swamp: return 40;
    case ETerrainBlendStyle::Stone: return 30;
    case ETerrainBlendStyle::Paving: return 20;
    case ETerrainBlendStyle::Soil: return 10;
    case ETerrainBlendStyle::Organic: return 0;
    }
    return 0;
}

double srgbToLinear(int channel)
{
    static const QVector<double> table = []() {
        QVector<double> values(256);
        for(int index = 0; index < values.size(); ++index)
        {
            const double value = index / 255.0;
            values[index] = value <= 0.04045 ? value / 12.92
                : std::pow((value + 0.055) / 1.055, 2.4);
        }
        return values;
    }();
    return table.at(qBound(0, channel, 255));
}

int linearToSrgb(double channel)
{
    static const QVector<quint8> table = []() {
        QVector<quint8> values(65536);
        for(int index = 0; index < values.size(); ++index)
        {
            const double channel = index / 65535.0;
            const double value = channel <= 0.0031308 ? channel * 12.92
                : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
            values[index] = quint8(qBound(0, qRound(value * 255.0), 255));
        }
        return values;
    }();
    return table.at(qBound(0, qRound(channel * 65535.0), 65535));
}

QRgb materialBlend(const QRgb samples[4], const double weights[4],
                   bool forceOpaque, bool linearLight)
{
    double alpha = 0.0;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    for(int index = 0; index < 4; ++index)
    {
        // DXT1 terrain pages can decode the transparent selector even though
        // the land renderer treats the authored texel as opaque coverage.
        // Letting that selector participate in a newly generated land blend
        // punches holes through the terrain and also biases its colour toward
        // the other family. Overlay generation can explicitly retain alpha.
        const double a = forceOpaque ? 1.0 : qAlpha(samples[index]) / 255.0;
        alpha += weights[index] * a;
        red += weights[index] * a * (linearLight
            ? srgbToLinear(qRed(samples[index])) : qRed(samples[index]));
        green += weights[index] * a * (linearLight
            ? srgbToLinear(qGreen(samples[index])) : qGreen(samples[index]));
        blue += weights[index] * a * (linearLight
            ? srgbToLinear(qBlue(samples[index])) : qBlue(samples[index]));
    }
    if(alpha <= 0.0)
        return qRgba(0, 0, 0, 0);
    return qRgba(linearLight ? linearToSrgb(red / alpha)
                             : qBound(0, qRound(red / alpha), 255),
                 linearLight ? linearToSrgb(green / alpha)
                             : qBound(0, qRound(green / alpha), 255),
                 linearLight ? linearToSrgb(blue / alpha)
                             : qBound(0, qRound(blue / alpha), 255),
                 forceOpaque ? 255
                             : qBound(0, qRound(alpha * 255.0), 255));
}

QImage cleanAnchoredMask(const QImage& source,
                         const QVector<bool>& ownerCorners)
{
    if(source.isNull()) return {};
    const int width = source.width();
    const int height = source.height();
    const int count = width * height;
    QVector<quint8> binary(count);
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
            binary[y * width + x] = source.pixelColor(x, y).red() >= 128;

    const int cornerIndices[4] = {0, width - 1, count - 1,
                                  qMax(0, count - width)};
    for(int corner = 0; corner < 4 && corner < ownerCorners.size(); ++corner)
        binary[cornerIndices[corner]] = ownerCorners.at(corner) ? 1 : 0;

    // Preserve stock-authored chips, strands and gravel clumps while removing
    // both DXT grain and the large detached square regions which caused the
    // reported inside-corner crosses. Corner-connected regions are always
    // authoritative. Detached regions survive only in the substantial-detail
    // band; this is deliberately area based, not a blanket flood-fill erase.
    const int minimumArea = qMax(3, count / 4096);
    const int maximumDetachedArea = qMax(minimumArea, count / 32);
    for(int pass = 0; pass < 2; ++pass)
    {
        QVector<quint8> visited(count, 0);
        for(int start = 0; start < count; ++start)
        {
            if(visited[start]) continue;
            const quint8 value = binary[start];
            QVector<int> component;
            component.append(start);
            visited[start] = 1;
            bool anchored = false;
            for(int cursor = 0; cursor < component.size(); ++cursor)
            {
                const int index = component.at(cursor);
                for(int corner = 0; corner < 4; ++corner)
                    anchored = anchored || index == cornerIndices[corner];
                const int x = index % width;
                const int y = index / width;
                const int neighbours[4] = {index - 1, index + 1,
                                            index - width, index + width};
                for(int direction = 0; direction < 4; ++direction)
                {
                    if((direction == 0 && x == 0)
                       || (direction == 1 && x + 1 == width)
                       || (direction == 2 && y == 0)
                       || (direction == 3 && y + 1 == height))
                        continue;
                    const int next = neighbours[direction];
                    if(!visited[next] && binary[next] == value)
                    {
                        visited[next] = 1;
                        component.append(next);
                    }
                }
            }
            if(!anchored && (component.size() < minimumArea
                             || component.size() > maximumDetachedArea))
                for(int index : component) binary[index] = !value;
        }
    }

    // Retain the stock mask's authored tonal ramp wherever topology cleanup
    // did not change ownership. Earlier code rebuilt a binary mask and then
    // feathered only a few pixels, turning broad stock blends into sharp cuts.
    QVector<quint8> cleaned(count);
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int index = y * width + x;
            const int authored = source.pixelColor(x, y).red();
            const bool authoredOwner = authored >= 128;
            cleaned[index] = authoredOwner == bool(binary[index])
                ? quint8(authored) : quint8(binary[index] ? 255 : 0);
        }

    // Stock art already carries its authored tonal ramp. Do not box-blur it:
    // at 256px the old radius-four pass visibly downrezzed crisp masks and
    // erased the small shapes the review explicitly asked us to retain.
    QImage result(source.size(), QImage::Format_Grayscale8);
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int value = cleaned[y * width + x];
            result.setPixelColor(x, y, QColor(value, value, value));
        }
    return result;
}

bool reviewedMaskRejected(const QString& source, ETerrainBlendStyle ownerStyle,
                          bool sameStyle)
{
    const auto normalizedLabel = [](QString value) {
        value.remove(QLatin1Char(' '));
        value.replace(QStringLiteral("slot"), QStringLiteral(":"),
                      Qt::CaseInsensitive);
        return value;
    };
    const QString normalizedSource = normalizedLabel(source);
    const auto matches = [&normalizedSource, &normalizedLabel](const char* suffix) {
        return normalizedSource.endsWith(
            normalizedLabel(QString::fromLatin1(suffix)), Qt::CaseInsensitive);
    };
    const auto any = [&matches](std::initializer_list<const char*> values) {
        for(const char* value : values) if(matches(value)) return true;
        return false;
    };
    // Decisions exported from the user's r31 review. A source rejected for a
    // cross-family transfer is excluded throughout that visual style. Sources
    // that were acceptable cross-family but failed same-style transfer are
    // excluded only for the latter selection pool.
    switch(ownerStyle)
    {
    case ETerrainBlendStyle::Ice:
        if(any({"bz11k:204", "Zone6_2:58", "bz11k:408",
                "Zone6_2:285", "Zone6_2:54"})) return true;
        return sameStyle && any({"Zone6_2:296", "Zone6_2:60"});
    case ETerrainBlendStyle::Sand:
        if(any({"bz4g:449", "bz5g:482", "Zone7:497", "bz4g:401",
                "Zone13:442", "bz4g:495", "bz5g:431"})) return true;
        return sameStyle && any({"bz13h:101", "Zone7:125", "Zone7:423",
                                 "Zone7:165", "bz13h:418"});
    case ETerrainBlendStyle::Gravel:
        return sameStyle && any({"Zone6:300", "Zone6:301", "Zone6:216",
                                 "bz14h:223", "Zone6:231", "Zone6:398"});
    case ETerrainBlendStyle::Stone:
        if(matches("BaseGipat:320")) return true;
        return sameStyle && any({"Bz7g:11", "Bz7g:12", "Bz7g:134",
                                 "Bz7g:217"});
    case ETerrainBlendStyle::Grass:
        if(any({"Zone7:216", "Zone7:160", "Zone7:122", "Zone7:123"}))
            return true;
        return sameStyle && any({"BaseGipat:100", "BaseGipat:101",
            "BaseGipat:128", "BaseGipat:129", "BaseGipat:384",
            "BaseGipat:192", "BaseGipat:193", "Zone6:290",
            "Zone6:291", "Zone6:420"});
    case ETerrainBlendStyle::Snow:
        if(matches("bz10k:290")) return true;
        return sameStyle && any({"bz10k:211", "bz10k:194", "Zone13:233",
                                 "bz10k:202", "bz10k:234"});
    case ETerrainBlendStyle::Paving:
        if(any({"Zone10:383", "Zone10:182", "Zone10:183"})) return true;
        return sameStyle && any({"Zone10:282", "Zone10:283", "Zone10:114",
                                 "Zone10:115", "Zone10:395"});
    default:
        return false;
    }
}

bool oneCornerMaskLeaksToForeignEdge(
    const QImage& mask, const STerrainTransitionSignature& requestedPreview,
    qint32 ownerMaterial)
{
    if(mask.isNull() || mask.width() < 4 || mask.height() < 4)
        return true;
    QVector<bool> ownerCorners;
    for(qint32 material : corners(requestedPreview))
        ownerCorners.append(material == ownerMaterial);
    int ownerCount = 0;
    for(bool owner : ownerCorners) ownerCount += owner;
    if(ownerCount != 1 && ownerCount != 3)
        return false;

    // Judge the minority island. A three-corner mask is the inverse of the
    // same one-corner topology and must obey exactly the same containment
    // rule. This catches the long L-shaped strips called out in the visual
    // review without imposing a procedural replacement silhouette.
    const bool invert = ownerCount == 3;
    int minorityCorner = -1;
    for(int corner = 0; corner < ownerCorners.size(); ++corner)
        if(ownerCorners.at(corner) != invert)
        {
            minorityCorner = corner;
            break;
        }
    if(minorityCorner < 0) return true;
    const auto minority = [&mask, invert](int x, int y) {
        const int value = mask.pixelColor(x, y).red();
        // A transferred 10-45% strand remains plainly visible in the composed
        // texture even though it does not cross the binary ownership midpoint.
        // Judge meaningful tonal influence, not only the >=128 topology.
        return invert ? value <= 231 : value >= 24;
    };
    const bool left = minorityCorner == 0 || minorityCorner == 3;
    const bool top = minorityCorner == 0 || minorityCorner == 1;
    const int foreignX = left ? mask.width() - 1 : 0;
    const int foreignY = top ? mask.height() - 1 : 0;
    int foreignVertical = 0;
    int foreignHorizontal = 0;
    for(int y = 0; y < mask.height(); ++y)
        foreignVertical += minority(foreignX, y);
    for(int x = 0; x < mask.width(); ++x)
        foreignHorizontal += minority(x, foreignY);
    const int verticalAllowance = qMax(1, mask.height() / 128);
    const int horizontalAllowance = qMax(1, mask.width() / 128);
    if(foreignVertical > verticalAllowance
       || foreignHorizontal > horizontalAllowance)
        return true;

    // A single forced black corner is insufficient: reject an island that
    // runs almost the full length of either adjacent edge and creates the
    // reported ninety-degree cut against the next tile.
    const int ownX = left ? 0 : mask.width() - 1;
    const int ownY = top ? 0 : mask.height() - 1;
    int adjacentHorizontalReach = 0;
    int adjacentVerticalReach = 0;
    for(int x = 0; x < mask.width(); ++x)
        if(minority(x, ownY))
            adjacentHorizontalReach = qMax(adjacentHorizontalReach,
                left ? x : mask.width() - 1 - x);
    for(int y = 0; y < mask.height(); ++y)
        if(minority(ownX, y))
            adjacentVerticalReach = qMax(adjacentVerticalReach,
                top ? y : mask.height() - 1 - y);
    return adjacentHorizontalReach * 4 >= mask.width() * 3
        || adjacentVerticalReach * 4 >= mask.height() * 3;
}

int maskBoundaryComplexity(const QImage& mask)
{
    int transitions = 0;
    for(int y = 0; y < mask.height(); ++y)
        for(int x = 0; x < mask.width(); ++x)
        {
            const bool owner = mask.pixelColor(x, y).red() >= 128;
            if(x > 0)
                transitions += owner
                    != (mask.pixelColor(x - 1, y).red() >= 128);
            if(y > 0)
                transitions += owner
                    != (mask.pixelColor(x, y - 1).red() >= 128);
        }
    return transitions;
}

QImage opaqueTerrainMaterialSource(const QImage& source)
{
    QImage result = source.convertToFormat(QImage::Format_RGBA8888);
    if(result.isNull()) return result;
    const int width = result.width();
    const int height = result.height();
    const int count = width * height;
    QVector<int> nearest(count, -1);
    QVector<int> queue;
    queue.reserve(count);
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int index = y * width + x;
            if(result.pixelColor(x, y).alpha() > 0)
            {
                nearest[index] = index;
                queue.append(index);
            }
        }
    // Multi-source flood fill reconstructs each DXT1 transparent selector
    // from the closest real texel of that same uniform material. This avoids
    // converting transparent decode placeholders into opaque black artifacts.
    for(int cursor = 0; cursor < queue.size(); ++cursor)
    {
        const int index = queue.at(cursor);
        const int x = index % width;
        const int y = index / width;
        const int neighbours[4] = {index - 1, index + 1,
                                    index - width, index + width};
        for(int direction = 0; direction < 4; ++direction)
        {
            if((direction == 0 && x == 0)
               || (direction == 1 && x + 1 == width)
               || (direction == 2 && y == 0)
               || (direction == 3 && y + 1 == height))
                continue;
            const int next = neighbours[direction];
            if(nearest[next] < 0)
            {
                nearest[next] = nearest[index];
                queue.append(next);
            }
        }
    }
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int sourceIndex = nearest[y * width + x];
            QColor color = sourceIndex >= 0
                ? result.pixelColor(sourceIndex % width, sourceIndex / width)
                : QColor(0, 0, 0);
            color.setAlpha(255);
            result.setPixelColor(x, y, color);
        }
    return result;
}

int transitionTopology(const STerrainTransitionSignature& signature)
{
    QMap<qint32, int> counts;
    for(qint32 id : corners(signature)) ++counts[id];
    if(counts.size() != 2) return counts.size();
    const QList<int> values = counts.values();
    if(values.contains(1)) return 21; // one/three
    return signature.northWest == signature.southEast
        && signature.northEast == signature.southWest ? 22 : 23;
}

STerrainTransitionSignature rotatePreviewClockwise(
    const STerrainTransitionSignature& value)
{
    STerrainTransitionSignature result;
    result.northWest = value.southWest;
    result.northEast = value.northWest;
    result.southEast = value.northEast;
    result.southWest = value.southEast;
    return result;
}

QImage rotateImageClockwise(const QImage& image)
{
    return image.transformed(QTransform().rotate(90.0), Qt::FastTransformation);
}

struct SCanonicalBlendMaskProfile
{
    double cornerExponent{1.6};
    double cornerExtent{0.62};
    double adjacentExtent{0.50};
    double displacement{0.035};
    double featherEdge{0.012};
    double featherInterior{0.030};
    double frequencyA{1.25};
    double frequencyB{2.15};
};

SCanonicalBlendMaskProfile canonicalBlendMaskProfile(
    ETerrainBlendStyle style)
{
    SCanonicalBlendMaskProfile result;
    switch(style)
    {
    case ETerrainBlendStyle::Grass:
        result.cornerExponent = 1.35;
        result.displacement = 0.050;
        result.featherInterior = 0.022;
        result.frequencyA = 1.85;
        result.frequencyB = 3.10;
        break;
    case ETerrainBlendStyle::Soil:
        result.cornerExponent = 1.70;
        result.displacement = 0.030;
        result.featherInterior = 0.032;
        result.frequencyA = 1.10;
        result.frequencyB = 2.05;
        break;
    case ETerrainBlendStyle::Ice:
        result.cornerExponent = 1.50;
        result.displacement = 0.042;
        result.featherEdge = 0.010;
        result.featherInterior = 0.026;
        result.frequencyA = 1.55;
        result.frequencyB = 2.75;
        break;
    case ETerrainBlendStyle::Lava:
        result.cornerExponent = 1.48;
        result.displacement = 0.048;
        result.featherInterior = 0.034;
        result.frequencyA = 1.20;
        result.frequencyB = 2.40;
        break;
    case ETerrainBlendStyle::Paving:
        result.cornerExponent = 1.90;
        result.displacement = 0.026;
        result.featherEdge = 0.010;
        result.featherInterior = 0.022;
        result.frequencyA = 1.05;
        result.frequencyB = 1.90;
        break;
    case ETerrainBlendStyle::Stone:
        result.cornerExponent = 1.55;
        result.displacement = 0.044;
        result.featherInterior = 0.026;
        result.frequencyA = 1.60;
        result.frequencyB = 2.85;
        break;
    case ETerrainBlendStyle::Gravel:
        result.cornerExponent = 1.62;
        result.displacement = 0.050;
        result.featherInterior = 0.025;
        result.frequencyA = 1.75;
        result.frequencyB = 3.00;
        break;
    case ETerrainBlendStyle::Sand:
        result.cornerExponent = 1.82;
        result.displacement = 0.034;
        result.featherEdge = 0.016;
        result.featherInterior = 0.040;
        result.frequencyA = 1.00;
        result.frequencyB = 1.85;
        break;
    case ETerrainBlendStyle::Snow:
        result.cornerExponent = 1.80;
        result.displacement = 0.028;
        result.featherEdge = 0.018;
        result.featherInterior = 0.034;
        result.frequencyA = 0.90;
        result.frequencyB = 1.70;
        break;
    case ETerrainBlendStyle::Swamp:
        result.cornerExponent = 1.68;
        result.displacement = 0.044;
        result.featherEdge = 0.018;
        result.featherInterior = 0.038;
        result.frequencyA = 1.15;
        result.frequencyB = 2.05;
        break;
    case ETerrainBlendStyle::Water:
        result.cornerExponent = 1.82;
        result.displacement = 0.034;
        result.featherEdge = 0.018;
        result.featherInterior = 0.036;
        result.frequencyA = 0.85;
        result.frequencyB = 1.65;
        break;
    case ETerrainBlendStyle::Organic:
        result.displacement = 0.055;
        break;
    }
    return result;
}

double smoothUnit(double value)
{
    value = qBound(0.0, value, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

double canonicalBoundaryNoise(double u, double v,
                              const SCanonicalBlendMaskProfile& profile,
                              ETerrainBlendStyle style, int variation)
{
    const double pi = 3.14159265358979323846;
    const double phase = double(terrainBlendStylePriority(style) + 17) * 0.071
        + double(variation + 1) * 1.137;
    const double first = std::sin(2.0 * pi
        * (profile.frequencyA * u + 0.61 * v) + phase);
    const double second = std::sin(2.0 * pi
        * (-0.43 * u + profile.frequencyB * v) + phase * 1.73);
    return first * 0.64 + second * 0.36;
}

QImage canonicalNorthWestCornerMask(const QSize& size,
                                    ETerrainBlendStyle style, int variation)
{
    if(size.width() <= 0 || size.width() != size.height()) return {};
    SCanonicalBlendMaskProfile profile = canonicalBlendMaskProfile(style);
    // The two variants change the large silhouette rather than adding fine
    // grain. The exterior crossings remain fixed because their displacement
    // envelope is exactly zero on the tile boundary.
    if(variation & 1)
    {
        profile.cornerExponent = qMax(1.25, profile.cornerExponent - 0.10);
        profile.displacement *= -0.90;
    }
    QImage result(size, QImage::Format_Grayscale8);
    const int divisor = qMax(1, size.width() - 1);
    const double pi = 3.14159265358979323846;
    for(int y = 0; y < size.height(); ++y)
        for(int x = 0; x < size.width(); ++x)
        {
            const double u = double(x) / double(divisor);
            const double v = double(y) / double(divisor);
            const double anchorEnvelope = std::sin(pi * u) * std::sin(pi * v);
            const double noise = canonicalBoundaryNoise(
                u, v, profile, style, variation);
            const double extent = profile.cornerExtent
                + profile.displacement * anchorEnvelope * noise;
            const double metric = std::pow(
                std::pow(u, profile.cornerExponent)
                    + std::pow(v, profile.cornerExponent),
                1.0 / profile.cornerExponent);
            const double feather = profile.featherEdge
                + profile.featherInterior * anchorEnvelope;
            const int value = qRound(255.0 * smoothUnit(
                (extent - metric + feather) / (2.0 * feather)));
            result.setPixelColor(x, y, QColor(value, value, value));
        }
    return result;
}

QImage canonicalNorthAdjacentMask(const QSize& size,
                                  ETerrainBlendStyle style, int variation)
{
    if(size.width() <= 0 || size.width() != size.height()) return {};
    SCanonicalBlendMaskProfile profile = canonicalBlendMaskProfile(style);
    if(variation & 1) profile.displacement *= -0.90;
    QImage result(size, QImage::Format_Grayscale8);
    const int divisor = qMax(1, size.width() - 1);
    const double pi = 3.14159265358979323846;
    for(int y = 0; y < size.height(); ++y)
        for(int x = 0; x < size.width(); ++x)
        {
            const double u = double(x) / double(divisor);
            const double v = double(y) / double(divisor);
            const double anchorEnvelope = std::sin(pi * u);
            // This odd wave is deliberately antisymmetric around the center.
            // Therefore inverse(rotation180(mask)) is byte-identical to the
            // same adjacent ownership assignment, independent of which
            // material ID happened to sort first.
            const double centered = u - 0.5;
            const double noise = 0.64 * std::sin(2.0 * pi * centered)
                + 0.36 * std::sin(4.0 * pi * centered);
            const double boundary = profile.adjacentExtent
                + profile.displacement * anchorEnvelope * noise;
            const double feather = profile.featherEdge
                + profile.featherInterior * anchorEnvelope;
            const int value = qRound(255.0 * smoothUnit(
                (boundary - v + feather) / (2.0 * feather)));
            result.setPixelColor(x, y, QColor(value, value, value));
        }
    return result;
}

QImage canonicalNorthWestSouthEastDiagonalMask(
    const QSize& size, ETerrainBlendStyle style)
{
    if(size.width() <= 0 || size.width() != size.height()) return {};
    const SCanonicalBlendMaskProfile profile = canonicalBlendMaskProfile(style);
    QImage result(size, QImage::Format_Grayscale8);
    const int divisor = qMax(1, size.width() - 1);
    const double pi = 3.14159265358979323846;
    for(int y = 0; y < size.height(); ++y)
        for(int x = 0; x < size.width(); ++x)
        {
            const double u = double(x) / double(divisor);
            const double v = double(y) / double(divisor);
            const double dx = u - 0.5;
            const double dy = v - 0.5;
            const double radius2 = dx * dx + dy * dy;
            // Every term changes sign under a quarter turn. Consequently the
            // inverse mask is exactly the rotated opposite-diagonal mask, and
            // both polarities remain two corner-rooted lobes. The quartic term
            // bends the central saddle without creating pixel-grain islands.
            const double saddle = dx * dy
                + profile.displacement * 8.0
                    * (dx * dx - dy * dy) * radius2;
            const double signedDistance = saddle
                / (std::sqrt(radius2) + 0.15);
            const double interiorEnvelope = std::sin(pi * u) * std::sin(pi * v);
            const double feather = profile.featherEdge
                + profile.featherInterior * interiorEnvelope;
            const int value = qRound(255.0 * smoothUnit(
                (signedDistance + feather) / (2.0 * feather)));
            result.setPixelColor(x, y, QColor(value, value, value));
        }
    return result;
}

QImage rotateMaskClockwise(QImage mask, int turns)
{
    turns %= 4;
    if(turns < 0) turns += 4;
    while(turns-- > 0) mask = rotateImageClockwise(mask);
    return mask;
}

void applyCanonicalExteriorProfile(QImage* mask, const bool owner[4])
{
    if(!mask || mask->isNull()) return;
    const int width = mask->width();
    const int height = mask->height();
    const auto value = [](bool firstOwner, bool secondOwner, double position) {
        if(firstOwner == secondOwner) return firstOwner ? 255 : 0;
        const double secondWeight = smoothUnit((position - 0.38) / 0.24);
        const double ownerWeight = firstOwner
            ? 1.0 - secondWeight : secondWeight;
        return qBound(0, qRound(ownerWeight * 255.0), 255);
    };
    for(int x = 0; x < width; ++x)
    {
        const double position = width == 1 ? 0.0
            : double(x) / double(width - 1);
        mask->setPixelColor(x, 0, QColor(value(owner[0], owner[1], position),
            value(owner[0], owner[1], position),
            value(owner[0], owner[1], position)));
        const int bottom = value(owner[3], owner[2], position);
        mask->setPixelColor(x, height - 1, QColor(bottom, bottom, bottom));
    }
    for(int y = 0; y < height; ++y)
    {
        const double position = height == 1 ? 0.0
            : double(y) / double(height - 1);
        const int left = value(owner[0], owner[3], position);
        const int right = value(owner[1], owner[2], position);
        mask->setPixelColor(0, y, QColor(left, left, left));
        mask->setPixelColor(width - 1, y, QColor(right, right, right));
    }
}

QString canonicalMaskTopologyName(int ownerCount, bool diagonal, int variation)
{
    if(ownerCount == 1)
        return QStringLiteral("outer-1of4-v%1").arg((variation & 1) + 1);
    if(ownerCount == 3)
        return QStringLiteral("inside-3of4-v%1").arg((variation & 1) + 1);
    if(diagonal)
        return QStringLiteral("diagonal-2of4");
    return QStringLiteral("edge-2of4-v%1").arg((variation & 1) + 1);
}

QByteArray grayscaleMaskPixelHash(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y = 0; y < image.height(); ++y)
        hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)),
                     image.width());
    return hash.result().toHex();
}

QImage approvedR63IceInsideMask(
    const QSize& size, const STerrainTransitionSignature& preview,
    qint32 ownerMaterial, qint32 reviewedStyleMaterial, int variation,
    QString* provenance)
{
    if(size.width() <= 0 || size.width() != size.height()
       || reviewedStyleMaterial == 0)
        return {};
    const QVector<qint32> ids = corners(preview);
    int ownerCount = 0;
    int supportCorner = -1;
    for(int corner = 0; corner < ids.size(); ++corner)
    {
        if(ids.at(corner) == reviewedStyleMaterial)
            ++ownerCount;
        else
            supportCorner = corner;
    }
    if(ownerCount != 3 || supportCorner < 0)
        return {};
    static const char* const resources[2] = {
        ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v1-r63.png",
        ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v2-r63.png"
    };
    static const char* const hashes[2] = {
        "1617f1c7b073aae77ad0d181368e83d68f2b013e612101c4dc3eb257625f247b",
        "53c17ae64fffa80b182002d4b9e44c54ce381c750329e689eabd65e4ae1aae8d"
    };
    const int index = variation & 1;
    QImage result(QString::fromLatin1(resources[index]));
    if(result.isNull() || result.size() != QSize(256, 256)
       || grayscaleMaskPixelHash(result) != QByteArray(hashes[index]))
        return {};
    result = result.convertToFormat(QImage::Format_Grayscale8);
    // Reviewed authority is stored in logical grid coordinates with owner at
    // NW/NE/SE and support at SW. Runtime masks use preview coordinates, whose
    // Y axis is mirrored at the grid boundary, so the same base mask has its
    // support at preview NW before any requested quarter-turn is applied.
    // Transform that exact corner assignment; never score or infer orientation.
    result = result.mirrored(false, true);
    result = rotateMaskClockwise(result, supportCorner);
    if(result.size() != size)
        result = result.scaled(size, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_Grayscale8);
    if(ownerMaterial != reviewedStyleMaterial)
        result.invertPixels();
    if(provenance)
        *provenance = QStringLiteral(
            "canonical-generated:ice-fracture:inside-3of4-v%1:r63-approved")
                .arg(index + 1);
    return result;
}

QImage canonicalGeneratedOwnershipMask(
    const QSize& size, const STerrainTransitionSignature& preview,
    qint32 ownerMaterial, ETerrainBlendStyle style, int variation,
    qint32 reviewedStyleMaterial,
    QString* provenance)
{
    const QVector<qint32> ids = corners(preview);
    bool owner[4] = {false, false, false, false};
    int ownerCount = 0;
    for(int corner = 0; corner < 4; ++corner)
    {
        owner[corner] = ids.at(corner) == ownerMaterial;
        ownerCount += owner[corner];
    }
    if(ownerCount <= 0 || ownerCount >= 4) return {};
    if(style == ETerrainBlendStyle::Ice && reviewedStyleMaterial != 0)
        return approvedR63IceInsideMask(
            size, preview, ownerMaterial, reviewedStyleMaterial,
            variation, provenance);
    const bool diagonal = ownerCount == 2
        && owner[0] == owner[2] && owner[1] == owner[3];
    QImage result;
    if(ownerCount == 1 || ownerCount == 3)
    {
        int corner = -1;
        for(int index = 0; index < 4; ++index)
            if(owner[index] == (ownerCount == 1)) { corner = index; break; }
        result = rotateMaskClockwise(
            canonicalNorthWestCornerMask(size, style, variation), corner);
        if(ownerCount == 3) result.invertPixels();
    }
    else if(diagonal)
    {
        result = canonicalNorthWestSouthEastDiagonalMask(size, style);
        if(!owner[0]) result = rotateMaskClockwise(result, 1);
    }
    else
    {
        int turns = -1;
        if(owner[0] && owner[1]) turns = 0;
        else if(owner[1] && owner[2]) turns = 1;
        else if(owner[2] && owner[3]) turns = 2;
        else if(owner[3] && owner[0]) turns = 3;
        if(turns < 0) return {};
        result = rotateMaskClockwise(
            canonicalNorthAdjacentMask(size, style, variation), turns);
    }

    // Structural safety patches are semantic, not decorative. They make each
    // requested corner visibly originate at full strength and guarantee that
    // no protected corner can be touched by feathering or silhouette noise.
    applyCanonicalExteriorProfile(&result, owner);
    const int patch = qMin(qMin(size.width(), size.height()),
        qMax(2, (size.width() + 15) / 16));
    for(int corner = 0; corner < 4; ++corner)
    {
        const bool right = corner == 1 || corner == 2;
        const bool bottom = corner == 2 || corner == 3;
        const int value = owner[corner] ? 255 : 0;
        for(int py = 0; py < patch; ++py)
            for(int px = 0; px < patch; ++px)
            {
                const int x = right ? size.width() - 1 - px : px;
                const int y = bottom ? size.height() - 1 - py : py;
                result.setPixelColor(x, y, QColor(value, value, value));
            }
    }
    if(provenance)
        *provenance = QStringLiteral("canonical-generated:%1:%2")
            .arg(CTerrainVirtualAtlas::blendStyleName(style))
            .arg(canonicalMaskTopologyName(ownerCount, diagonal, variation));
    return result;
}

bool sameOwnerPattern(const STerrainTransitionSignature& first,
                      qint32 firstOwner,
                      const STerrainTransitionSignature& second,
                      qint32 secondOwner)
{
    const QVector<qint32> a = corners(first);
    const QVector<qint32> b = corners(second);
    for(int index = 0; index < 4; ++index)
        if((a.at(index) == firstOwner) != (b.at(index) == secondOwner))
            return false;
    return true;
}
}

bool CTerrainVirtualMaterialField::resize(int tileWidth, int tileHeight,
                                          qint32 initialMaterial)
{
    if(tileWidth <= 0 || tileHeight <= 0
       || quint64(tileWidth + 1) * quint64(tileHeight + 1)
            > quint64(std::numeric_limits<int>::max()))
        return false;
    m_tileWidth = tileWidth;
    m_tileHeight = tileHeight;
    m_materials.fill(initialMaterial, (tileWidth + 1) * (tileHeight + 1));
    return true;
}

qint32 CTerrainVirtualMaterialField::material(int vertexX, int vertexY,
                                              bool* found) const
{
    const bool valid = vertexX >= 0 && vertexX <= m_tileWidth
        && vertexY >= 0 && vertexY <= m_tileHeight;
    if(found)
        *found = valid;
    return valid ? m_materials.at(vertexY * vertexWidth() + vertexX) : 0;
}

bool CTerrainVirtualMaterialField::setMaterial(int vertexX, int vertexY,
                                               qint32 value)
{
    if(vertexX < 0 || vertexX > m_tileWidth
       || vertexY < 0 || vertexY > m_tileHeight)
        return false;
    m_materials[vertexY * vertexWidth() + vertexX] = value;
    return true;
}

int CTerrainVirtualMaterialField::paint(const STerrainTileBrush& brush,
                                        qint32 value)
{
    if(brush.radius < 0.0f || m_materials.isEmpty())
        return 0;
    const int minimumX = qMax(0, int(std::ceil(brush.centerX - brush.radius)));
    const int maximumX = qMin(m_tileWidth,
        int(std::floor(brush.centerX + brush.radius)));
    const int minimumY = qMax(0, int(std::ceil(brush.centerY - brush.radius)));
    const int maximumY = qMin(m_tileHeight,
        int(std::floor(brush.centerY + brush.radius)));
    int changed = 0;
    for(int y = minimumY; y <= maximumY; ++y)
    {
        for(int x = minimumX; x <= maximumX; ++x)
        {
            const float dx = float(x) - brush.centerX;
            const float dy = float(y) - brush.centerY;
            const bool inside = brush.shape == ETerrainTileBrushShape::Square
                ? qMax(std::fabs(dx), std::fabs(dy)) <= brush.radius
                : std::sqrt(dx * dx + dy * dy) <= brush.radius;
            if(!inside)
                continue;
            const int index = y * vertexWidth() + x;
            if(m_materials.at(index) != value)
            {
                m_materials[index] = value;
                ++changed;
            }
        }
    }
    return changed;
}

STerrainTransitionSignature CTerrainVirtualMaterialField::cellSignature(
    int cellX, int cellY, bool* found) const
{
    bool valid = cellX >= 0 && cellX < m_tileWidth
        && cellY >= 0 && cellY < m_tileHeight;
    STerrainTransitionSignature result;
    if(valid)
    {
        const qint32 unset = std::numeric_limits<qint32>::min();
        const int nw = cellY * vertexWidth() + cellX;
        const int ne = nw + 1;
        const int sw = (cellY + 1) * vertexWidth() + cellX;
        const int se = sw + 1;
        valid = m_materials.at(nw) != unset && m_materials.at(ne) != unset
            && m_materials.at(se) != unset && m_materials.at(sw) != unset;
    }
    if(found) *found = valid;
    if(!valid)
        return result;
    result.northWest = material(cellX, cellY);
    result.northEast = material(cellX + 1, cellY);
    result.southEast = material(cellX + 1, cellY + 1);
    result.southWest = material(cellX, cellY + 1);
    return result;
}

QByteArray CTerrainVirtualMaterialField::stableSha256() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray dimensions;
    QDataStream stream(&dimensions, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << qint32(m_tileWidth) << qint32(m_tileHeight);
    hash.addData(dimensions);
    QByteArray value(4, '\0');
    for(qint32 materialValue : m_materials)
    {
        qToLittleEndian<qint32>(materialValue,
            reinterpret_cast<uchar*>(value.data()));
        hash.addData(value);
    }
    return hash.result().toHex();
}

bool CTerrainEditModel::copyVirtualMaterialField(
    ETerrainTileLayer layer,CTerrainVirtualMaterialField* output,
    bool requireComplete) const
{
    if(!output)return false;
    const QVector<qint32>& values=layer==ETerrainTileLayer::Land
        ?m_virtualLandMaterials:m_virtualOverlayMaterials;
    if(values.size()!=(tileCountX()+1)*(tileCountY()+1)
       ||!output->resize(tileCountX(),tileCountY()))return false;
    const qint32 unset=std::numeric_limits<qint32>::min();
    for(int y=0;y<=tileCountY();++y)for(int x=0;x<=tileCountX();++x)
    {
        const qint32 value=values.at(y*(tileCountX()+1)+x);
        if(value==unset&&requireComplete)return false;
        output->setMaterial(x,y,value);
    }
    return true;
}

ETerrainBlendStyle CTerrainVirtualAtlas::blendStyleForMaterialName(
    const QString& name)
{
    const QString normalized = name.trimmed().toCaseFolded();
    if(normalized.startsWith(QStringLiteral("grass")))
        return ETerrainBlendStyle::Grass;
    if(normalized.contains(QStringLiteral("snow")))
        return ETerrainBlendStyle::Snow;
    if(normalized.startsWith(QStringLiteral("sand")))
        return ETerrainBlendStyle::Sand;
    if(normalized.startsWith(QStringLiteral("rubble"))
       || normalized.contains(QStringLiteral("gravel")))
        return ETerrainBlendStyle::Gravel;
    if(normalized.startsWith(QStringLiteral("water"))
       || normalized.contains(QStringLiteral("/ water")))
        return ETerrainBlendStyle::Water;
    if(normalized.startsWith(QStringLiteral("ice")))
        return ETerrainBlendStyle::Ice;
    if(normalized.startsWith(QStringLiteral("lava")))
        return ETerrainBlendStyle::Lava;
    if(normalized.startsWith(QStringLiteral("swamp")))
        return ETerrainBlendStyle::Swamp;
    if(normalized.startsWith(QStringLiteral("paving"))
       || normalized.startsWith(QStringLiteral("slabs")))
        return ETerrainBlendStyle::Paving;
    if(normalized.startsWith(QStringLiteral("stone"))
       || normalized.startsWith(QStringLiteral("rock")))
        return ETerrainBlendStyle::Stone;
    if(normalized.startsWith(QStringLiteral("ground"))
       || normalized.contains(QStringLiteral("soil"))
       || normalized.contains(QStringLiteral("dirt"))
       || normalized.contains(QStringLiteral("mud")))
        return ETerrainBlendStyle::Soil;
    return ETerrainBlendStyle::Organic;
}

QString CTerrainVirtualAtlas::blendStyleName(ETerrainBlendStyle style)
{
    switch(style)
    {
    case ETerrainBlendStyle::Grass: return QStringLiteral("grass-strands");
    case ETerrainBlendStyle::Soil: return QStringLiteral("soil-mottle");
    case ETerrainBlendStyle::Ice: return QStringLiteral("ice-fracture");
    case ETerrainBlendStyle::Lava: return QStringLiteral("lava-flow");
    case ETerrainBlendStyle::Paving: return QStringLiteral("paving-courses");
    case ETerrainBlendStyle::Stone: return QStringLiteral("stone-fracture");
    case ETerrainBlendStyle::Gravel: return QStringLiteral("gravel-chunks");
    case ETerrainBlendStyle::Sand: return QStringLiteral("sand-grain");
    case ETerrainBlendStyle::Snow: return QStringLiteral("snow-clumps");
    case ETerrainBlendStyle::Swamp: return QStringLiteral("swamp-pools");
    case ETerrainBlendStyle::Water: return QStringLiteral("water-ripples");
    case ETerrainBlendStyle::Organic: return QStringLiteral("organic-fallback");
    }
    return QStringLiteral("organic-fallback");
}

CTerrainVirtualAtlas::CTerrainVirtualAtlas(
    const CTerrainTransitionCatalog& catalog,
    const QVector<QImage>& authoredTiles, quint32 distributionSeed,
    const QMap<qint32, QImage>& externalMaterialSources,
    const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles,
    bool generatedTilesOpaque, const QVector<qint32>& sourceTileTypes,
    const QMap<qint32, qint32>& materialRuntimeTypes)
    : m_catalog(catalog), m_authoredTiles(authoredTiles),
      m_sourceTileTypes(sourceTileTypes),
      m_materialRuntimeTypes(materialRuntimeTypes),
      m_materialBlendStyles(materialBlendStyles),
      m_distributionSeed(distributionSeed),
      m_generatedTilesOpaque(generatedTilesOpaque)
{
    if(authoredTiles.isEmpty())
    {
        m_error = QStringLiteral("The authored atlas is empty.");
        return;
    }
    const QSize tileSize = authoredTiles.front().size();
    if(tileSize.width() <= 0 || tileSize.width() != tileSize.height())
    {
        m_error = QStringLiteral("Terrain atlas tiles must be non-empty squares.");
        return;
    }
    for(const QImage& image : authoredTiles)
    {
        if(image.isNull() || image.size() != tileSize)
        {
            m_error = QStringLiteral("Terrain atlas tiles have inconsistent dimensions.");
            return;
        }
    }
    for(qint32 material : catalog.materialIds())
    {
        STerrainTransitionSignature pure;
        pure.northWest = pure.northEast = pure.southEast = pure.southWest = material;
        bool found = false;
        const quint16 packed = catalog.selectForPaint(
            pure, 0, 0, distributionSeed, &found);
        if(found)
        {
            const int slot = SMprPackedTile::decode(packed).atlasSlot();
            if(slot >= 0 && slot < authoredTiles.size())
            {
                m_materialSources.insert(material, m_generatedTilesOpaque
                    ? opaqueTerrainMaterialSource(authoredTiles.at(slot))
                    : authoredTiles.at(slot).convertToFormat(
                        QImage::Format_RGBA8888));
                m_materialSourceSlots.insert(material,slot);
                if(!m_materialRuntimeTypes.contains(material)
                   && slot < m_sourceTileTypes.size()
                   && m_sourceTileTypes.at(slot) >= 0
                   && m_sourceTileTypes.at(slot) < 16)
                    m_materialRuntimeTypes.insert(
                        material, m_sourceTileTypes.at(slot));
            }
        }
    }
    for(auto source = externalMaterialSources.constBegin();
        source != externalMaterialSources.constEnd(); ++source)
    {
        if(source.value().isNull() || source.value().size() != tileSize)
            continue;
        m_materialSources.insert(source.key(), m_generatedTilesOpaque
            ? opaqueTerrainMaterialSource(source.value())
            : source.value().convertToFormat(QImage::Format_RGBA8888));
        // External uniform anchors have no slot in the opened map. They remain
        // logical sources until a pure or mixed tuple is realized into the
        // working cache/final atlas.
        m_materialSourceSlots.remove(source.key());
    }
    m_valid = true;
}

int CTerrainVirtualAtlas::stockBlendMaskCountFor(
    const STerrainTransitionSignature& signature) const
{
    const int topology = transitionTopology(signature);
    int count = 0;
    for(const STerrainBlendMaskTemplate& item : m_blendMaskTemplates)
        count += transitionTopology(item.signature) == topology;
    return count;
}

QByteArray CTerrainVirtualAtlas::signatureKey(
    const STerrainTransitionSignature& signature) const
{
    QByteArray key(16, '\0');
    const QVector<qint32> values = corners(signature);
    for(int index = 0; index < values.size(); ++index)
        qToLittleEndian<qint32>(values.at(index),
            reinterpret_cast<uchar*>(key.data() + index * 4));
    return key;
}

STerrainTransitionSignature CTerrainVirtualAtlas::canonicalSignature(
    const STerrainTransitionSignature& signature, quint8* rotation) const
{
    STerrainTransitionSignature best = signature;
    quint8 bestRotation = 0;
    for(quint8 candidateRotation = 1; candidateRotation < 4; ++candidateRotation)
    {
        const STerrainTransitionSignature candidate =
            CTerrainTransitionCatalog::rotated(signature, candidateRotation);
        if(signatureLess(candidate, best))
        {
            best = candidate;
            bestRotation = candidateRotation;
        }
    }
    if(rotation)
        *rotation = bestRotation;
    return best;
}

ETerrainBlendStyle CTerrainVirtualAtlas::blendStyleForSignature(
    const STerrainTransitionSignature& signature,
    qint32 preferredPaintMaterial) const
{
    QMap<qint32, int> counts;
    for(qint32 material : corners(signature))
        ++counts[material];
    // A committed paint stroke carries the selected brush material explicitly.
    // That material owns the mask language even when it already occupies three
    // corners and the support material is the one-corner minority. Falling back
    // to corner frequency is reserved for context-free callers and old maps.
    if(preferredPaintMaterial != 0 && counts.contains(preferredPaintMaterial))
        return m_materialBlendStyles.value(
            preferredPaintMaterial, ETerrainBlendStyle::Organic);
    // R63 explicitly approves the authored-derived Ice language when Ice
    // owns three corners. Keep every other family/topology on its existing
    // path until its own review decision is applied.
    for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        if(it.value() == 3 && m_materialBlendStyles.value(
               it.key(), ETerrainBlendStyle::Organic)
               == ETerrainBlendStyle::Ice)
            return ETerrainBlendStyle::Ice;
    int selectedCount = 5;
    int selectedPriority = -1;
    ETerrainBlendStyle selected = ETerrainBlendStyle::Organic;
    for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    {
        const ETerrainBlendStyle style = m_materialBlendStyles.value(
            it.key(), ETerrainBlendStyle::Organic);
        const int priority = terrainBlendStylePriority(style);
        // A one-corner material is normally the newly entering edge and owns
        // the visual language. Balanced tuples use a stable material priority.
        if(it.value() < selectedCount
           || (it.value() == selectedCount && priority > selectedPriority))
        {
            selectedCount = it.value();
            selectedPriority = priority;
            selected = style;
        }
    }
    return selected;
}

qint32 CTerrainVirtualAtlas::reviewedMaskStyleMaterial(
    const STerrainTransitionSignature& signature,
    ETerrainBlendStyle style, qint32 preferredPaintMaterial) const
{
    if(style != ETerrainBlendStyle::Ice)
        return 0;
    QMap<qint32, int> counts;
    for(qint32 material : corners(signature))
        ++counts[material];
    if(preferredPaintMaterial != 0
       && counts.value(preferredPaintMaterial) == 3
       && m_materialBlendStyles.value(
              preferredPaintMaterial, ETerrainBlendStyle::Organic)
              == ETerrainBlendStyle::Ice)
        return preferredPaintMaterial;
    for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        if(it.value() == 3 && m_materialBlendStyles.value(
               it.key(), ETerrainBlendStyle::Organic)
               == ETerrainBlendStyle::Ice)
            return it.key();
    return 0;
}

int CTerrainVirtualAtlas::blendVariationLimit(
    const STerrainTransitionSignature& signature)
{
    const QVector<qint32> values = corners(signature);
    QSet<qint32> materials;
    for(qint32 material : values)
        materials.insert(material);
    switch(materials.size())
    {
    case 0:
    case 1: return 1;
    case 2:
    {
        int firstCount = 0;
        const qint32 first = *materials.constBegin();
        for(qint32 material : values) firstCount += material == first;
        // Across one unordered material pair this permits two variants for
        // its 1/3 tuple, two for its adjacent 2/2 tuple, and one for its
        // diagonal 2/2 tuple: five exact tiles total, with rotations reused.
        if(firstCount == 1 || firstCount == 3) return 2;
        const bool diagonal = signature.northWest == signature.southEast
            && signature.northEast == signature.southWest;
        return diagonal ? 1 : 2;
    }
    // A three-material set has three possible doubled identities. One exact
    // tile for each arrangement gives the requested three-tile total budget.
    case 3: return 1;
    default: return 1;
    }
}

int CTerrainVirtualAtlas::blendVariation(int cellX, int cellY,
                                         ETerrainBlendStyle style,
                                         int limit) const
{
    const int boundedLimit = qBound(1, limit, blendVariationCount());
    const quint32 origin = terrainNoiseHash(m_distributionSeed
        ^ quint32(terrainBlendStylePriority(style) + 1) * 0xc2b2ae35u);
    return int((origin + quint32(cellX) + quint32(cellY) * 3u)
               % quint32(boundedLimit));
}

bool CTerrainVirtualAtlas::registerBlendMaskTemplate(
    const STerrainTransitionSignature& signature, const QImage& pixels,
    const QString& sourceLabel, CValidationReport* report)
{
    QSet<qint32> materials;
    for(qint32 id : corners(signature)) materials.insert(id);
    if(materials.size() != 2)
        return true;
    QList<qint32> ordered = materials.values();
    std::sort(ordered.begin(), ordered.end());
    const qint32 owner = ordered.at(0);
    const qint32 other = ordered.at(1);
    if(!m_materialSources.contains(owner) || !m_materialSources.contains(other))
        return true;
    const QImage ownerPixels = m_materialSources.value(owner)
        .convertToFormat(QImage::Format_RGBA8888);
    const QImage otherPixels = m_materialSources.value(other)
        .convertToFormat(QImage::Format_RGBA8888);
    if(ownerPixels.size() != pixels.size() || otherPixels.size() != pixels.size())
        return true;

    const QImage transition = pixels.convertToFormat(QImage::Format_RGBA8888);
    QImage mask(pixels.size(), QImage::Format_Grayscale8);
    quint64 separability = 0;
    for(int y = 0; y < mask.height(); ++y)
        for(int x = 0; x < mask.width(); ++x)
        {
            const QColor actual = transition.pixelColor(x, y);
            const QColor a = ownerPixels.pixelColor(x, y);
            const QColor b = otherPixels.pixelColor(x, y);
            const auto distance = [&actual](const QColor& sample) {
                const qint64 dr = actual.red() - sample.red();
                const qint64 dg = actual.green() - sample.green();
                const qint64 db = actual.blue() - sample.blue();
                const qint64 da = actual.alpha() - sample.alpha();
                return quint64(dr * dr + dg * dg + db * db + da * da);
            };
            const quint64 da = distance(a);
            const quint64 db = distance(b);
            separability += quint64(qAbs(a.red() - b.red())
                + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()));
            double ownership = da + db == 0 ? 0.5
                : double(db) / double(da + db);
            // Stock transition art often contains pixels authored specifically
            // for the join. Preserve its silhouette while avoiding a broad,
            // blurry cross-fade when that ownership field is transferred.
            ownership = qBound(0.0, (ownership - 0.5) * 2.4 + 0.5, 1.0);
            ownership = ownership * ownership * (3.0 - 2.0 * ownership);
            mask.setPixelColor(x, y, QColor::fromRgbF(
                ownership, ownership, ownership));
        }
    if(separability < quint64(mask.width()) * quint64(mask.height()) * 6u)
        return true;

    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(signature);
    const QVector<qint32> previewCorners = corners(preview);
    QVector<bool> ownerCorners;
    for(qint32 material : previewCorners)
        ownerCorners.append(material == owner);
    mask = cleanAnchoredMask(mask, ownerCorners);
    const QPoint points[4] = {{0, 0}, {mask.width() - 1, 0},
        {mask.width() - 1, mask.height() - 1}, {0, mask.height() - 1}};
    for(int corner = 0; corner < 4; ++corner)
        mask.setPixelColor(points[corner], previewCorners.at(corner) == owner
            ? Qt::white : Qt::black);

    int ownedPixels = 0;
    for(int y = 0; y < mask.height(); ++y)
        for(int x = 0; x < mask.width(); ++x)
            ownedPixels += mask.pixelColor(x, y).red() >= 128;
    const double ownedFraction = double(ownedPixels)
        / double(qMax(1, mask.width() * mask.height()));
    int ownedCorners = 0;
    for(bool owned : ownerCorners) ownedCorners += owned;
    const double minimumOwned = ownedCorners == 1 ? 0.01
        : ownedCorners == 2 ? 0.05 : 0.20;
    const double maximumOwned = ownedCorners == 1 ? 0.80
        : ownedCorners == 2 ? 0.95 : 0.99;
    if(ownedFraction < minimumOwned || ownedFraction > maximumOwned)
    {
        if(report) report->add("terrain.stock_blend_mask_rejected",
            EValidationSeverity::Warning,
            "Rejected a degenerate near-solid stock transition mask",
            QStringLiteral("%1 (%2% owner)").arg(sourceLabel)
                .arg(qRound(ownedFraction * 100.0)));
        return true;
    }

    STerrainBlendMaskTemplate candidate;
    candidate.signature = signature;
    candidate.ownershipMask = mask;
    candidate.ownerMaterial = owner;
    candidate.otherMaterial = other;
    candidate.ownerStyle = m_materialBlendStyles.value(
        owner, ETerrainBlendStyle::Organic);
    candidate.otherStyle = m_materialBlendStyles.value(
        other, ETerrainBlendStyle::Organic);
    candidate.sourceLabel = sourceLabel;
    candidate.maskSha256 = imageHash(mask);
    for(const STerrainBlendMaskTemplate& existing : m_blendMaskTemplates)
        if(existing.maskSha256 == candidate.maskSha256
           && transitionTopology(existing.signature)
                == transitionTopology(candidate.signature))
            return true;
    m_blendMaskTemplates.append(candidate);
    if(report) report->add("terrain.stock_blend_mask",
        EValidationSeverity::Info,
        "Indexed a full-resolution stock JMV transition mask", sourceLabel);
    return true;
}

QImage CTerrainVirtualAtlas::alignedStockMask(
    const STerrainTransitionSignature& requestedPreview,
    qint32 ownerMaterial, qint32 otherMaterial, int variation,
    QString* provenance) const
{
    struct Candidate { int score{0}; QImage mask; QString source; QByteArray hash; };
    QVector<Candidate> matches;
    const ETerrainBlendStyle ownerStyle = m_materialBlendStyles.value(
        ownerMaterial, ETerrainBlendStyle::Organic);
    const ETerrainBlendStyle otherStyle = m_materialBlendStyles.value(
        otherMaterial, ETerrainBlendStyle::Organic);
    const int topology = transitionTopology(requestedPreview);
    for(const STerrainBlendMaskTemplate& item : m_blendMaskTemplates)
    {
        STerrainTransitionSignature itemPreview =
            CTerrainTransitionCatalog::mirroredGridY(item.signature);
        if(transitionTopology(itemPreview) != topology)
            continue;
        QImage rotated = item.ownershipMask;
        for(int turn = 0; turn < 4; ++turn)
        {
            bool inverted = false;
            bool aligned = sameOwnerPattern(itemPreview, item.ownerMaterial,
                                            requestedPreview, ownerMaterial);
            if(!aligned)
            {
                aligned = sameOwnerPattern(itemPreview, item.otherMaterial,
                                           requestedPreview, ownerMaterial);
                inverted = aligned;
            }
            if(aligned)
            {
                Candidate candidate;
                candidate.mask = rotated;
                if(inverted) candidate.mask.invertPixels();
                candidate.source = item.sourceLabel;
                candidate.hash = item.maskSha256;
                const ETerrainBlendStyle mappedOwnerStyle = inverted
                    ? item.otherStyle : item.ownerStyle;
                const ETerrainBlendStyle mappedOtherStyle = inverted
                    ? item.ownerStyle : item.otherStyle;
                // Review decisions describe how a source mask transfers into
                // the requested brush style. Keying them by the source-side
                // style let rejected snow/grass masks bypass Ice and Paving
                // reviews whenever the source pair was cross-style.
                if(reviewedMaskRejected(item.sourceLabel, ownerStyle,
                                        ownerStyle == otherStyle))
                    break;
                const bool exactStyles = mappedOwnerStyle == ownerStyle
                    && mappedOtherStyle == otherStyle;
                if(oneCornerMaskLeaksToForeignEdge(candidate.mask,
                        requestedPreview, ownerMaterial))
                    break;

                // Generated transitions share a small reviewed mask language
                // by visual material style. Do not privilege an exact
                // subfamily pair: that made Winter/Sparse/Dark grass each pick
                // unrelated masks despite all being grass. Exact authored
                // pixel tiles are still reused by the catalog before this
                // generation path is reached.
                candidate.score = exactStyles ? 1500 : 0;
                if(mappedOwnerStyle == ownerStyle) candidate.score += 700;
                if(mappedOtherStyle == otherStyle) candidate.score += 350;
                if(ownerStyle == ETerrainBlendStyle::Paving)
                {
                    // The reviewed paving masks commonly encode straight
                    // courses and radial cuts. Rock/gravel silhouettes retain
                    // substantial broken pieces and transfer much better to
                    // slabs and paving artwork.
                    if(mappedOwnerStyle == ETerrainBlendStyle::Stone
                       || mappedOwnerStyle == ETerrainBlendStyle::Gravel)
                        candidate.score += 2500;
                    else if(mappedOwnerStyle == ETerrainBlendStyle::Paving)
                        candidate.score -= 300;
                }
                if(ownerStyle == ETerrainBlendStyle::Stone
                   && mappedOwnerStyle == ETerrainBlendStyle::Gravel)
                    candidate.score += 250;
                if(ownerStyle == ETerrainBlendStyle::Ice
                   && mappedOwnerStyle == ETerrainBlendStyle::Snow)
                    candidate.score += 250;
                if(ownerStyle == ETerrainBlendStyle::Sand)
                {
                    // Prefer broad dune-like stock silhouettes over noisy
                    // cheese-hole ownership fields. This is only a ranking of
                    // real JMV masks; no synthetic sand pattern is introduced.
                    const int complexity = maskBoundaryComplexity(candidate.mask);
                    candidate.score -= qMin(640, (complexity / 64) * 64);
                }
                matches.append(candidate);
                break;
            }
            itemPreview = rotatePreviewClockwise(itemPreview);
            rotated = rotateImageClockwise(rotated);
        }
    }
    if(matches.isEmpty()) return {};
    std::sort(matches.begin(), matches.end(), [](const Candidate& a,
                                                  const Candidate& b) {
        if(a.score != b.score) return a.score > b.score;
        if(a.source != b.source) return a.source < b.source;
        return a.hash < b.hash;
    });
    const int bestScore = matches.front().score;
    int pool = 0;
    while(pool < matches.size() && matches.at(pool).score == bestScore
          && pool < blendVariationCount()) ++pool;
    Candidate selected = matches.at(variation % qMax(1, pool));
    const QPoint requestedCorners[4] = {{0, 0},
        {selected.mask.width() - 1, 0},
        {selected.mask.width() - 1, selected.mask.height() - 1},
        {0, selected.mask.height() - 1}};
    const QVector<qint32> requestedIds = corners(requestedPreview);
    for(int corner = 0; corner < 4; ++corner)
        selected.mask.setPixelColor(requestedCorners[corner],
            requestedIds.at(corner) == ownerMaterial ? Qt::white : Qt::black);
    if(provenance) *provenance = selected.source;
    return selected.mask;
}

QImage CTerrainVirtualAtlas::reviewBlendMask(
    const STerrainTransitionSignature& gridSignature,
    qint32 ownerMaterial, int variation, QString* provenance,
    qint32 preferredPaintMaterial) const
{
    if(!m_valid || m_authoredTiles.isEmpty()) return {};
    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(gridSignature);
    const QVector<qint32> ids = corners(preview);
    QSet<qint32> unique;
    for(qint32 id : ids) unique.insert(id);
    if(unique.size() != 2 || !unique.contains(ownerMaterial)) return {};
    QList<qint32> ordered = unique.values();
    std::sort(ordered.begin(), ordered.end());
    const qint32 canonicalOwner = ordered.front();
    const ETerrainBlendStyle style = blendStyleForSignature(
        gridSignature, preferredPaintMaterial);
    const qint32 reviewedStyleMaterial = reviewedMaskStyleMaterial(
        gridSignature, style, preferredPaintMaterial);
    QString canonicalProvenance;
    QImage result = canonicalGeneratedOwnershipMask(
        m_authoredTiles.front().size(), preview, canonicalOwner,
        style, qMax(0, variation), reviewedStyleMaterial,
        &canonicalProvenance);
    if(!result.isNull() && canonicalOwner != ownerMaterial)
        result.invertPixels();
    if(provenance && !result.isNull())
    {
        if(canonicalProvenance.endsWith(QStringLiteral(":r63-approved")))
        {
            *provenance = canonicalProvenance;
            return result;
        }
        int ownerCount = 0;
        for(qint32 id : ids) ownerCount += id == ownerMaterial;
        const bool diagonal = ownerCount == 2
            && ids.at(0) == ids.at(2) && ids.at(1) == ids.at(3);
        *provenance = QStringLiteral("canonical-generated:%1:%2")
            .arg(blendStyleName(style))
            .arg(canonicalMaskTopologyName(
                ownerCount, diagonal, qMax(0, variation)));
    }
    return result;
}

QImage CTerrainVirtualAtlas::generateTile(
    const STerrainTransitionSignature& gridSignature,
    ETerrainBlendStyle style, int variation,
    qint32 reviewedStyleMaterial,
    QString* provenance, CValidationReport* report, bool linearLight) const
{
    // Atlas thumbnails are vertically opposite the canonical terrain grid.
    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(gridSignature);
    const QVector<qint32> ids = corners(preview);
    for(qint32 id : ids)
    {
        if(!m_materialSources.contains(id))
        {
            if(report) report->add("terrain.virtual_atlas_source",
                EValidationSeverity::Error,
                "A generated transition requires a uniform authored source tile",
                QString::number(id));
            return {};
        }
    }
    QSet<qint32> uniqueSet;
    for(qint32 id : ids) uniqueSet.insert(id);
    QList<qint32> unique = uniqueSet.values();
    std::sort(unique.begin(), unique.end());
    QVector<QImage> ownership;
    ownership.reserve(unique.size());
    QStringList sources;
    if(unique.size() == 1)
    {
        if(provenance) *provenance = QStringLiteral("uniform-material-source");
        return m_materialSources.value(unique.front())
            .convertToFormat(QImage::Format_RGBA8888);
    }
    if(unique.size() == 2)
    {
        QString source;
        QImage owner = canonicalGeneratedOwnershipMask(
            m_authoredTiles.front().size(), preview, unique.at(0), style,
            variation, reviewedStyleMaterial, &source);
        if(owner.isNull())
        {
            if(report) report->add("terrain.canonical_blend_mask_missing",
                EValidationSeverity::Error,
                "No canonical blend mask matches this transition topology",
                QString("%1/%2 (%3)").arg(unique.at(0)).arg(unique.at(1))
                    .arg(blendStyleName(style)));
            return {};
        }
        ownership << owner;
        QImage inverse = owner;
        inverse.invertPixels();
        ownership << inverse;
        sources << source;
    }
    else
    {
        // Multi-family junctions use one canonical topology mask for every
        // material's complete corner assignment. In particular, adjacent
        // repeated corners remain one connected region instead of a union of
        // unrelated one-corner source masks.
        for(qint32 material : unique)
        {
            QString source;
            const QImage mask = canonicalGeneratedOwnershipMask(
                m_authoredTiles.front().size(), preview, material,
                m_materialBlendStyles.value(material, style), variation,
                0,
                &source);
            if(mask.isNull())
            {
                if(report) report->add("terrain.canonical_blend_mask_missing",
                    EValidationSeverity::Error,
                    "No canonical topology mask can compose a multi-family junction",
                    QString::number(material));
                return {};
            }
            sources << source;
            ownership << mask;
        }
    }
    sources.removeDuplicates();
    if(provenance) *provenance = sources.join(QStringLiteral("; "));
    const int size = m_authoredTiles.front().width();
    QImage result(size, size, QImage::Format_RGBA8888);
    for(int y = 0; y < size; ++y)
        for(int x = 0; x < size; ++x)
        {
            double weights[4] = {0.0, 0.0, 0.0, 0.0};
            double total = 0.0;
            QRgb samples[4];
            for(int corner = 0; corner < 4; ++corner)
            {
                samples[corner] = m_materialSources.value(ids.at(corner))
                    .pixelColor(x, y).rgba();
                bool first = true;
                for(int candidate = 0; candidate < 4; ++candidate)
                    if(ids.at(candidate) == ids.at(corner) && candidate < corner)
                        first = false;
                if(first)
                {
                    const int materialIndex = unique.indexOf(ids.at(corner));
                    weights[corner] = ownership.at(materialIndex)
                        .pixelColor(x, y).redF();
                    total += weights[corner];
                }
            }
            if(total <= 0.000001)
            {
                // Independently transferred one-corner masks can leave a gap
                // at a rare 3-/4-family junction. Fall back only in that gap
                // to the exact corner bilinear partition; never emit a zero-
                // weight transparent/black texel or substitute an identity.
                const double u = size == 1 ? 0.0
                    : double(x) / double(size - 1);
                const double v = size == 1 ? 0.0
                    : double(y) / double(size - 1);
                const double cornerWeights[4] = {(1-u)*(1-v), u*(1-v),
                                                  u*v, (1-u)*v};
                for(int corner = 0; corner < 4; ++corner)
                {
                    bool first = true;
                    for(int candidate = 0; candidate < corner; ++candidate)
                        first &= ids.at(candidate) != ids.at(corner);
                    if(!first) continue;
                    for(int candidate = corner; candidate < 4; ++candidate)
                        if(ids.at(candidate) == ids.at(corner))
                            weights[corner] += cornerWeights[candidate];
                    total += weights[corner];
                }
            }
            if(total > 0.0)
                for(double& weight : weights) weight /= total;

            // Independently transferred corner silhouettes may overlap at a
            // three-/four-family junction. Never average all of them into a
            // low-detail soup: retain only the strongest two, and cross-fade
            // those only inside a narrow ownership band.
            QVector<int> ranked;
            for(int corner = 0; corner < 4; ++corner)
                if(weights[corner] > 0.0) ranked.append(corner);
            std::sort(ranked.begin(), ranked.end(), [&weights](int a, int b) {
                return weights[a] > weights[b];
            });
            if(ranked.size() > 1)
            {
                const int first = ranked.at(0);
                const int second = ranked.at(1);
                const double pairTotal = weights[first] + weights[second];
                double share = pairTotal > 0.0 ? weights[first] / pairTotal : 1.0;
                share = qBound(0.0, (share - 0.38) / 0.24, 1.0);
                share = share * share * (3.0 - 2.0 * share);
                for(double& weight : weights) weight = 0.0;
                weights[first] = share;
                weights[second] = 1.0 - share;
            }
            const auto forceSharedEdge = [&](qint32 firstMaterial,
                                             qint32 secondMaterial,
                                             double position) {
                for(double& weight : weights) weight = 0.0;
                // Match the stored 8-bit ownership mask exactly. Without this
                // quantization a sub-half-byte weight can display as black in
                // the review mask yet still tint a linear-light preview by a
                // channel value at the edge of the zero/one plateaus.
                const double secondWeight = firstMaterial == secondMaterial
                    ? 0.0 : qRound(smoothUnit(
                        (position - 0.38) / 0.24) * 255.0) / 255.0;
                for(int corner = 0; corner < 4; ++corner)
                    if(ids.at(corner) == firstMaterial)
                    { weights[corner] = 1.0 - secondWeight; break; }
                for(int corner = 0; corner < 4; ++corner)
                    if(ids.at(corner) == secondMaterial)
                    { weights[corner] += secondWeight; break; }
            };
            // Interior artwork comes from the canonical generated mask. The
            // outer texel ring is resolved solely from its two endpoints using
            // a universal plateau profile. This provides solid corner patches
            // and prevents independently selected variants from opening seams.
            if(y == 0) forceSharedEdge(ids.at(0), ids.at(1),
                size == 1 ? 0.0 : double(x) / double(size - 1));
            if(y == size - 1) forceSharedEdge(ids.at(3), ids.at(2),
                size == 1 ? 0.0 : double(x) / double(size - 1));
            if(x == 0) forceSharedEdge(ids.at(0), ids.at(3),
                size == 1 ? 0.0 : double(y) / double(size - 1));
            if(x == size - 1) forceSharedEdge(ids.at(1), ids.at(2),
                size == 1 ? 0.0 : double(y) / double(size - 1));
            result.setPixelColor(x, y,
                QColor::fromRgba(materialBlend(samples, weights,
                                               m_generatedTilesOpaque,
                                               linearLight)));
        }
    return result;
}

QImage CTerrainVirtualAtlas::reviewGeneratedBlend(
    const STerrainTransitionSignature& gridSignature, int variation,
    QString* provenance, CValidationReport* report,
    qint32 preferredPaintMaterial) const
{
    const ETerrainBlendStyle style = blendStyleForSignature(
        gridSignature, preferredPaintMaterial);
    return generateTile(gridSignature, style, qMax(0, variation),
                        reviewedMaskStyleMaterial(
                            gridSignature, style, preferredPaintMaterial),
                        provenance, report);
}

QImage CTerrainVirtualAtlas::reviewGeneratedBlendLinearLight(
    const STerrainTransitionSignature& gridSignature, int variation,
    QString* provenance, CValidationReport* report,
    qint32 preferredPaintMaterial) const
{
    const ETerrainBlendStyle style = blendStyleForSignature(
        gridSignature, preferredPaintMaterial);
    return generateTile(gridSignature, style, qMax(0, variation),
                        reviewedMaskStyleMaterial(
                            gridSignature, style, preferredPaintMaterial),
                        provenance, report, true);
}

bool CTerrainVirtualAtlas::addAuthoredLibraryTransition(
    const STerrainTransitionSignature& signature, const QImage& pixels,
    const QString& sourceLabel, CValidationReport* report)
{
    if(!m_valid || pixels.isNull() || pixels.size() != m_authoredTiles.front().size())
    {
        if(report) report->add("terrain.authored_library_pixels",
            EValidationSeverity::Error,
            "An authored global transition has invalid or incompatible pixels",
            sourceLabel);
        return false;
    }
    registerBlendMaskTemplate(signature, pixels, sourceLabel, report);
    const int libraryIndex = m_authoredLibraryGeneratedIndices.size();
    if(libraryIndex >= 16384)
        return false;
    STerrainFinalizedTile imported;
    imported.canonicalSignature = signature;
    imported.pixels = pixels.convertToFormat(QImage::Format_RGBA8888);
    imported.pixelSha256 = imageHash(imported.pixels);
    imported.authored = true;
    imported.sourceSlot = -1;
    imported.blendProvenance = sourceLabel;
    imported.stockDerivedBlend = true;
    const int generatedIndex = m_generatedTiles.size();
    m_generatedTiles.append(imported);
    m_generatedByPixels.insert(typedPixelKey(imported.pixelSha256, -1,
                               imported.canonicalSignature),
                               generatedIndex);
    m_authoredLibraryGeneratedIndices.append(generatedIndex);
    for(int rotation = 0; rotation < 4; ++rotation)
    {
        SMprPackedTile packed;
        packed.tileIndex = quint8(libraryIndex % 64);
        packed.atlasIndex = quint8(libraryIndex / 64);
        packed.rotation = quint8(rotation);
        STerrainTransitionVariant variant;
        variant.signature = CTerrainTransitionCatalog::rotated(
            signature, -rotation);
        variant.packedTile = packed.encode();
        if(!m_authoredLibraryCatalog.addVariant(variant, report))
            return false;
    }
    return true;
}

QVector<STerrainVirtualTile> CTerrainVirtualAtlas::authoredLibraryVariants(
    const STerrainTransitionSignature& signature,
    qint32 preferredPaintMaterial, CValidationReport* report)
{
    QVector<STerrainVirtualTile> result;
    const qint32 runtimeType = resolvedRuntimeType(
        signature, preferredPaintMaterial, -1, report);
    if(runtimeType < 0)
        return result;
    QSet<int> seen;
    for(const STerrainTransitionVariant& variant :
        m_authoredLibraryCatalog.paintVariants(signature))
    {
        const int libraryIndex = SMprPackedTile::decode(
            variant.packedTile).atlasSlot();
        if(libraryIndex < 0
           || libraryIndex >= m_authoredLibraryGeneratedIndices.size())
            continue;
        const int generatedIndex = typedTileVariant(
            m_authoredLibraryGeneratedIndices.at(libraryIndex), runtimeType);
        if(generatedIndex < 0 || seen.contains(generatedIndex))
            continue;
        seen.insert(generatedIndex);
        STerrainVirtualTile tile;
        tile.signature = signature;
        tile.importedAuthored = true;
        tile.generatedIndex = generatedIndex;
        tile.runtimeTerrainType = runtimeType;
        result.append(tile);
    }
    return result;
}

qint32 CTerrainVirtualAtlas::resolvedRuntimeType(
    const STerrainTransitionSignature& signature,
    qint32 preferredPaintMaterial, qint32 preferredRuntimeType,
    CValidationReport* report) const
{
    QMap<qint32, int> coverage;
    for(qint32 material : corners(signature))
    {
        const qint32 type = m_materialRuntimeTypes.value(material, -1);
        if(type < 0 || type >= 16)
        {
            if(report) report->add("terrain.generated_type_unknown",
                EValidationSeverity::Error,
                "A generated terrain identity has no reviewed runtime terrain type",
                QStringLiteral("semantic material %1").arg(material));
            return -1;
        }
        ++coverage[type];
    }
    int maximum = 0;
    QVector<qint32> tied;
    for(auto it = coverage.cbegin(); it != coverage.cend(); ++it)
    {
        if(it.value() > maximum)
        {
            maximum = it.value();
            tied = {it.key()};
        }
        else if(it.value() == maximum)
            tied.append(it.key());
    }
    if(tied.size() == 1)
        return tied.front();
    qint32 tieType = preferredRuntimeType;
    if(tieType < 0 && preferredPaintMaterial != 0)
        tieType = m_materialRuntimeTypes.value(preferredPaintMaterial, -1);
    if(tied.contains(tieType))
        return tieType;
    if(report) report->add("terrain.generated_type_tie",
        EValidationSeverity::Error,
        "An equal-coverage generated transition requires the runtime type of the material currently being painted");
    return -1;
}

int CTerrainVirtualAtlas::typedTileVariant(int sourceIndex, qint32 runtimeType)
{
    if(sourceIndex < 0 || sourceIndex >= m_generatedTiles.size()
       || runtimeType < 0 || runtimeType >= 16)
        return -1;
    if(m_generatedTiles.at(sourceIndex).runtimeTerrainType == runtimeType)
        return sourceIndex;
    const QByteArray key = typedPixelKey(
        m_generatedTiles.at(sourceIndex).pixelSha256, runtimeType,
        m_generatedTiles.at(sourceIndex).canonicalSignature);
    const int existing = m_generatedByPixels.value(key, -1);
    if(existing >= 0) return existing;
    STerrainFinalizedTile variant = m_generatedTiles.at(sourceIndex);
    variant.runtimeTerrainType = runtimeType;
    const int index = m_generatedTiles.size();
    m_generatedTiles.append(variant);
    m_generatedByPixels.insert(key, index);
    return index;
}

STerrainVirtualTile CTerrainVirtualAtlas::resolve(
    const STerrainTransitionSignature& signature, int cellX, int cellY,
    CValidationReport* report, qint32 preferredPaintMaterial,
    qint32 preferredRuntimeType)
{
    STerrainVirtualTile result;
    result.signature = signature;
    if(!m_valid)
    {
        if(report) report->add("terrain.virtual_atlas_invalid",
            EValidationSeverity::Error, m_error);
        return result;
    }
    bool authored = false;
    const quint16 packed = m_catalog.selectForPaint(
        signature, cellX, cellY, m_distributionSeed, &authored);
    if(authored)
    {
        result.authored = true;
        result.authoredPacked = packed;
        const int slot = SMprPackedTile::decode(packed).atlasSlot();
        result.runtimeTerrainType = slot >= 0 && slot < m_sourceTileTypes.size()
            ? m_sourceTileTypes.at(slot) : -1;
        return result;
    }

    const qint32 runtimeType = resolvedRuntimeType(
        signature, preferredPaintMaterial, preferredRuntimeType, report);
    if(runtimeType < 0)
        return result;

    bool libraryAuthored = false;
    const quint16 libraryPacked = m_authoredLibraryCatalog.selectForPaint(
        signature, cellX, cellY, m_distributionSeed, &libraryAuthored);
    if(libraryAuthored)
    {
        const SMprPackedTile decoded = SMprPackedTile::decode(libraryPacked);
        const int libraryIndex = decoded.atlasSlot();
        if(libraryIndex >= 0
           && libraryIndex < m_authoredLibraryGeneratedIndices.size())
        {
            result.importedAuthored = true;
            result.generatedIndex = typedTileVariant(
                m_authoredLibraryGeneratedIndices.at(libraryIndex), runtimeType);
            result.generatedRotation = decoded.rotation;
            result.runtimeTerrainType = runtimeType;
            return result;
        }
    }

    quint8 rotation = 0;
    const STerrainTransitionSignature canonical =
        canonicalSignature(signature, &rotation);
    const ETerrainBlendStyle style = blendStyleForSignature(
        canonical, preferredPaintMaterial);
    const qint32 reviewedStyleMaterial = reviewedMaskStyleMaterial(
        canonical, style, preferredPaintMaterial);
    const int variation = blendVariation(
        cellX, cellY, style, blendVariationLimit(canonical));
    QByteArray key = signatureKey(canonical);
    key.append(char(terrainBlendStylePriority(style)));
    key.append(char(variation));
    char typeBytes[4];
    qToLittleEndian<qint32>(reviewedStyleMaterial,
        reinterpret_cast<uchar*>(typeBytes));
    key.append(typeBytes, 4);
    qToLittleEndian<qint32>(runtimeType,
        reinterpret_cast<uchar*>(typeBytes));
    key.append(typeBytes, 4);
    int index = m_generatedBySignature.value(key, -1);
    if(index < 0)
    {
        QString provenance;
        const QImage pixels = generateTile(
            canonical, style, variation, reviewedStyleMaterial,
            &provenance, report);
        if(pixels.isNull())
            return result;
        const QByteArray pixelsHash = imageHash(pixels);
        index = m_generatedByPixels.value(
            typedPixelKey(pixelsHash, runtimeType, canonical), -1);
        if(index < 0)
        {
            STerrainFinalizedTile generated;
            generated.canonicalSignature = canonical;
            generated.pixels = pixels;
            generated.pixelSha256 = pixelsHash;
            generated.authored = false;
            generated.blendProvenance = provenance;
            QSet<qint32> generatedMaterials;
            for(qint32 id : corners(canonical)) generatedMaterials.insert(id);
            generated.canonicalGeneratedBlend = generatedMaterials.size() > 1;
            generated.runtimeTerrainType = runtimeType;
            index = m_generatedTiles.size();
            m_generatedTiles.append(generated);
            m_generatedByPixels.insert(
                typedPixelKey(pixelsHash, runtimeType, canonical), index);
        }
        m_generatedBySignature.insert(key, index);
    }
    result.generatedIndex = index;
    result.generatedRotation = rotation;
    result.runtimeTerrainType = runtimeType;
    return result;
}

QImage CTerrainVirtualAtlas::composePage(const QVector<QImage>& tiles,
                                         int firstTile, int tileSize)
{
    QImage page(tileSize * 8, tileSize * 8, QImage::Format_RGBA8888);
    page.fill(Qt::transparent);
    for(int local = 0; local < 64 && firstTile + local < tiles.size(); ++local)
    {
        // Tile images are already stored in the MMP/OpenGL bottom-up row
        // convention. Slot row zero is the visual top of the 8x8 page, so it
        // belongs in raw page row seven. Mirroring every tile and leaving the
        // page rows in visual order flips the complete atlas at upload time.
        const QImage source = tiles.at(firstTile + local)
            .convertToFormat(QImage::Format_RGBA8888);
        const int destinationX = (local % 8) * tileSize;
        const int destinationY = (7 - local / 8) * tileSize;
        for(int y = 0; y < tileSize; ++y)
            memcpy(page.scanLine(destinationY + y) + destinationX * 4,
                   source.constScanLine(y), size_t(tileSize) * 4u);
    }
    return page;
}

bool CTerrainVirtualAtlas::decodePhysicalTileImages(
    const STerrainAtlasFinalization& finalization, QVector<QImage>* images,
    CValidationReport* report)
{
    if(!images) return false;
    images->clear();
    if(finalization.textureCount <= 0
       || finalization.pageMmpPayloads.size() != finalization.textureCount)
    {
        if(report) report->add("terrain.finalized_physical_pages",
            EValidationSeverity::Error,
            "The finalized atlas does not contain one MMP payload per physical page");
        return false;
    }
    images->reserve(finalization.textureCount * 64);
    for(int page = 0; page < finalization.textureCount; ++page)
        for(int localSlot = 0; localSlot < 64; ++localSlot)
        {
            QImage tile;
            QString diagnostic;
            if(!ei::decodeMmpDxt1AtlasTile(
                    finalization.pageMmpPayloads.at(page), localSlot,
                    &tile, &diagnostic))
            {
                if(report) report->add("terrain.finalized_physical_decode",
                    EValidationSeverity::Error,
                    "A finalized physical atlas slot could not be decoded",
                    QStringLiteral("page %1 slot %2: %3")
                        .arg(page).arg(localSlot).arg(diagnostic));
                images->clear();
                return false;
            }
            images->append(tile);
        }
    return true;
}

STerrainAtlasFinalization CTerrainVirtualAtlas::finalize(
    const CTerrainVirtualMaterialField& field, const QString& sourceNamespace,
    int capacityLimit, const QVector<qint32>& sourceTileTypes,
    CValidationReport* report,const QVector<quint16>& pinnedPackedTiles,
    const QVector<qint32>& preferredCellTypes,
    const QVector<quint16>& preferredPackedCells)
{
    STerrainAtlasFinalization result;
    result.capacityLimit = capacityLimit;
    if(!m_valid || field.tileWidth() <= 0 || field.tileHeight() <= 0
       || capacityLimit <= 0 || capacityLimit > 16384
       || (!preferredPackedCells.isEmpty()
           && preferredPackedCells.size()
                != field.tileWidth() * field.tileHeight()))
    {
        result.error = !m_valid ? m_error
            : QStringLiteral("The material field or atlas capacity is invalid.");
        return result;
    }

    struct SResolvedCell { STerrainVirtualTile tile; int x; int y; };
    QVector<SResolvedCell> cells;
    cells.reserve(field.tileWidth() * field.tileHeight());
    QMap<int, int> authoredOutput;
    QMap<int, int> generatedOutput;
    QVector<QImage> denseImages;
    for(int y = 0; y < field.tileHeight(); ++y)
    {
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            const int cellIndex = y * field.tileWidth() + x;
            const qint32 preferredType = cellIndex < preferredCellTypes.size()
                ? preferredCellTypes.at(cellIndex) : -1;
            const STerrainTransitionSignature desired =
                field.cellSignature(x, y);
            STerrainVirtualTile tile;
            bool preserved = false;
            if(!preferredPackedCells.isEmpty())
            {
                const quint16 preferredPacked = preferredPackedCells.at(cellIndex);
                bool decoded = false;
                const STerrainTransitionSignature existing =
                    m_catalog.gridSignatureForPacked(preferredPacked, &decoded);
                const int preferredSlot = SMprPackedTile::decode(
                    preferredPacked).atlasSlot();
                if(decoded && existing == desired && preferredSlot >= 0
                   && preferredSlot < m_authoredTiles.size())
                {
                    tile.signature = desired;
                    tile.authored = true;
                    tile.authoredPacked = preferredPacked;
                    tile.runtimeTerrainType = preferredSlot
                        < sourceTileTypes.size()
                        ? sourceTileTypes.at(preferredSlot) : -1;
                    preserved = true;
                }
            }
            if(!preserved)
                tile = resolve(desired, x, y, report, 0, preferredType);
            if(!tile.authored && tile.generatedIndex < 0)
            {
                result.error = QStringLiteral(
                    "An exact tile could not be authored. See validation findings for a missing "
                    "uniform material source or canonical blend topology.");
                return result;
            }
            cells.append({tile, x, y});
            ++result.usage.cellCount;
            if(tile.authored)
            {
                const int sourceSlot = SMprPackedTile::decode(tile.authoredPacked).atlasSlot();
                ++result.usage.authoredSlots[sourceSlot];
                if(!authoredOutput.contains(sourceSlot))
                {
                    if(sourceSlot < 0 || sourceSlot >= m_authoredTiles.size())
                    {
                        result.error = QStringLiteral("An authored tile points outside the source atlas.");
                        return result;
                    }
                    authoredOutput.insert(sourceSlot, denseImages.size());
                    result.authoredSlotRemap.insert(sourceSlot,denseImages.size());
                    denseImages.append(m_authoredTiles.at(sourceSlot));
                    STerrainFinalizedTile output;
                    output.authored = true;
                    output.sourceSlot = sourceSlot;
                    output.pixels = m_authoredTiles.at(sourceSlot);
                    output.pixelSha256 = imageHash(output.pixels);
                    output.runtimeTerrainType = sourceSlot < sourceTileTypes.size()
                        ? sourceTileTypes.at(sourceSlot) : -1;
                    bool found = false;
                    SMprPackedTile basePacked;
                    basePacked.tileIndex = quint8(sourceSlot % 64);
                    basePacked.atlasIndex = quint8(sourceSlot / 64);
                    output.canonicalSignature = m_catalog.gridSignatureForPacked(
                        basePacked.encode(), &found);
                    result.tiles.append(output);
                }
            }
            else
            {
                ++result.usage.generatedTiles[tile.generatedIndex];
                if(!generatedOutput.contains(tile.generatedIndex))
                {
                    generatedOutput.insert(tile.generatedIndex, denseImages.size());
                    denseImages.append(m_generatedTiles.at(tile.generatedIndex).pixels);
                    result.tiles.append(m_generatedTiles.at(tile.generatedIndex));
                }
            }
        }
    }
    for(quint16 packed:pinnedPackedTiles)
    {
        const int sourceSlot=SMprPackedTile::decode(packed).atlasSlot();
        ++result.usage.authoredSlots[sourceSlot];
        if(authoredOutput.contains(sourceSlot))continue;
        if(sourceSlot<0||sourceSlot>=m_authoredTiles.size())
        { result.error=QStringLiteral("A pinned layer tile points outside the source atlas.");return result; }
        authoredOutput.insert(sourceSlot,denseImages.size());
        result.authoredSlotRemap.insert(sourceSlot,denseImages.size());
        denseImages.append(m_authoredTiles.at(sourceSlot));
        STerrainFinalizedTile output;output.authored=true;output.sourceSlot=sourceSlot;
        output.pixels=m_authoredTiles.at(sourceSlot);output.pixelSha256=imageHash(output.pixels);
        output.runtimeTerrainType=sourceSlot<sourceTileTypes.size()
            ?sourceTileTypes.at(sourceSlot):-1;
        SMprPackedTile base;base.tileIndex=quint8(sourceSlot%64);base.atlasIndex=quint8(sourceSlot/64);
        bool found=false;output.canonicalSignature=m_catalog.gridSignatureForPacked(base.encode(),&found);
        result.tiles.append(output);
    }
    result.requiredSlots = denseImages.size();
    if(result.requiredSlots > capacityLimit)
    {
        result.error = QStringLiteral(
            "Finalization requires %1 unique tiles, exceeding the validated %2-slot ceiling. "
            "No material identity was substituted; reduce used identities or validate a higher page limit.")
            .arg(result.requiredSlots).arg(capacityLimit);
        if(report) report->add("terrain.virtual_atlas_capacity",
            EValidationSeverity::Error, result.error);
        return result;
    }

    result.packedCells.reserve(cells.size());
    for(const SResolvedCell& cell : cells)
    {
        int outputSlot = -1;
        quint8 rotation = 0;
        if(cell.tile.authored)
        {
            const SMprPackedTile source = SMprPackedTile::decode(cell.tile.authoredPacked);
            outputSlot = authoredOutput.value(source.atlasSlot());
            rotation = source.rotation;
        }
        else
        {
            outputSlot = generatedOutput.value(cell.tile.generatedIndex);
            rotation = cell.tile.generatedRotation;
        }
        SMprPackedTile packed;
        packed.tileIndex = quint8(outputSlot % 64);
        packed.atlasIndex = quint8(outputSlot / 64);
        packed.rotation = rotation;
        result.packedCells.append(packed.encode());
    }

    result.tileTypes.reserve(result.tiles.size());
    for(const STerrainFinalizedTile& tile : result.tiles)
    {
        const qint32 type = tile.runtimeTerrainType;
        if(type < 0 || type >= 16)
        {
            result.error = QStringLiteral(
                "A finalized physical atlas slot has no reviewed runtime terrain type.");
            if(report) report->add("terrain.finalized_type_unknown",
                EValidationSeverity::Error, result.error,
                tile.authored
                    ? QStringLiteral("source slot %1").arg(tile.sourceSlot)
                    : QString::fromLatin1(tile.pixelSha256));
            return result;
        }
        result.tileTypes.append(type);
    }
    const int tileSize = denseImages.front().width();
    result.textureCount = (denseImages.size() + 63) / 64;
    for(int page = 0; page < result.textureCount; ++page)
    {
        const QImage image = composePage(denseImages, page * 64, tileSize);
        QString diagnostic;
        const QByteArray mmp = ei::encodeMmpDxt1(image, true, &diagnostic);
        if(mmp.isEmpty())
        {
            result.error = QStringLiteral("Could not encode atlas page: %1").arg(diagnostic);
            return result;
        }
        result.pages.append(image);
        result.pageMmpPayloads.append(mmp);
    }

    QCryptographicHash stable(QCryptographicHash::Sha256);
    stable.addData(sourceNamespace.toUtf8().toLower());
    stable.addData(field.stableSha256());
    for(const STerrainFinalizedTile& tile : result.tiles)
    {
        stable.addData(tile.pixelSha256);
        char typeBytes[4];
        qToLittleEndian<qint32>(tile.runtimeTerrainType,
            reinterpret_cast<uchar*>(typeBytes));
        stable.addData(typeBytes, 4);
    }
    for(quint16 packed : result.packedCells)
    {
        char bytes[2];
        qToLittleEndian<quint16>(packed, reinterpret_cast<uchar*>(bytes));
        stable.addData(bytes, 2);
    }
    for(quint16 packed:pinnedPackedTiles)
    {
        char bytes[2];
        qToLittleEndian<quint16>(packed,reinterpret_cast<uchar*>(bytes));
        stable.addData(bytes,2);
    }
    result.stableSha256 = stable.result().toHex();
    const QString safeBase = sourceNamespace.toLower().replace(
        QRegularExpression(QStringLiteral("[^a-z0-9_]+")), QStringLiteral("_"));
    result.isolatedNamespace = QStringLiteral("%1_eime_%2")
        .arg(safeBase.isEmpty() ? QStringLiteral("terrain") : safeBase,
             QString::fromLatin1(result.stableSha256.left(12)));
    result.success = true;
    return result;
}

STerrainPortableAtlasPackage CTerrainVirtualAtlas::buildPortablePackage(
    const CMprDocument& source,const STerrainAtlasFinalization& finalization,
    ETerrainTileLayer layer,CValidationReport* report)
{
    STerrainPortableAtlasPackage result;
    const SMprMapInfo& map=source.mapInfo();
    const int width=int(map.sectorCountX)*16;
    const int height=int(map.sectorCountY)*16;
    if(!finalization.success||!map.valid
       ||finalization.packedCells.size()!=width*height
       ||finalization.textureCount<=0||finalization.pageMmpPayloads.size()!=finalization.textureCount){
        result.error=QStringLiteral("The finalized atlas does not match the source MPR dimensions.");
        return result;
    }
    const QByteArray originalMp=source.memberPayload(map.memberName);
    const int tileTypeOffset=38+int(map.materialCount)*44;
    const int oldTailOffset=tileTypeOffset+int(map.tileCount)*4;
    const int oldAnimationEnd=oldTailOffset+int(map.animatedTileCount)*4;
    if(tileTypeOffset<38||oldTailOffset>originalMp.size()
       ||oldAnimationEnd>originalMp.size()){
        result.error=QStringLiteral("The MP tile-type array is truncated.");return result;
    }
    QByteArray replacementMp=originalMp.left(tileTypeOffset);
    for(qint32 type:finalization.tileTypes){
        char bytes[4];qToLittleEndian<qint32>(type,reinterpret_cast<uchar*>(bytes));
        replacementMp.append(bytes,4);
    }
    // Animated-terrain records address physical atlas slots. Copying them
    // byte-for-byte after compaction makes an old animation start point at a
    // newly generated material, which then cycles through unrelated tiles in
    // game. Preserve only ranges whose complete authored frame sequence was
    // retained and remains contiguous after remapping. Generated/imported
    // exact tiles are deliberately static.
    QByteArray remappedAnimations;
    quint32 remappedAnimationCount=0;
    for(quint32 index=0;index<map.animatedTileCount;++index)
    {
        const int offset=oldTailOffset+int(index)*4;
        const quint16 oldStart=qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(originalMp.constData()+offset));
        const quint16 phaseCount=qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(originalMp.constData()+offset+2));
        bool retain=phaseCount>0;
        int newStart=-1;
        for(int phase=0;retain&&phase<int(phaseCount);++phase)
        {
            const int oldSlot=int(oldStart)+phase;
            if(!finalization.authoredSlotRemap.contains(oldSlot))
            { retain=false; break; }
            const int mapped=finalization.authoredSlotRemap.value(oldSlot);
            if(phase==0)newStart=mapped;
            else if(mapped!=newStart+phase)retain=false;
        }
        if(!retain)continue;
        char bytes[4];
        qToLittleEndian<quint16>(quint16(newStart),
            reinterpret_cast<uchar*>(bytes));
        qToLittleEndian<quint16>(phaseCount,
            reinterpret_cast<uchar*>(bytes+2));
        remappedAnimations.append(bytes,4);
        ++remappedAnimationCount;
    }
    replacementMp.append(remappedAnimations);
    replacementMp.append(originalMp.mid(oldAnimationEnd));
    qToLittleEndian<quint32>(quint32(finalization.textureCount),
        reinterpret_cast<uchar*>(replacementMp.data()+16));
    qToLittleEndian<quint32>(quint32(finalization.pages.front().width()),
        reinterpret_cast<uchar*>(replacementMp.data()+20));
    qToLittleEndian<quint32>(quint32(finalization.tileTypes.size()),
        reinterpret_cast<uchar*>(replacementMp.data()+24));
    qToLittleEndian<quint32>(quint32(finalization.tiles.front().pixels.width()),
        reinterpret_cast<uchar*>(replacementMp.data()+28));
    qToLittleEndian<quint32>(remappedAnimationCount,
        reinterpret_cast<uchar*>(replacementMp.data()+34));
    CMprEditTransaction transaction(QStringLiteral("Finalize isolated terrain atlas"));
    for(const SMprArchiveMember& member:source.members())
        if(member.name.compare(map.memberName,Qt::CaseInsensitive)==0)
            transaction.replaceMemberPayload(member.name,member.sha256,replacementMp);
    for(const SMprSectorInfo& sector:source.sectors()){
        if(layer==ETerrainTileLayer::Overlay&&!sector.hasOverlay)continue;
        const SMprArchiveMember& member=source.members().at(sector.memberIndex);
        QByteArray payload=member.payload;
        const quint64 offset=layer==ETerrainTileLayer::Land
            ?sector.landTileOffset:sector.overlayTileOffset;
        if(offset+16u*16u*2u>quint64(payload.size())){
            result.error=QStringLiteral("A sector tile array is truncated: %1").arg(member.name);
            return result;
        }
        for(int localY=0;localY<16;++localY)for(int localX=0;localX<16;++localX){
            const int globalX=sector.sectorX*16+localX;
            const int globalY=sector.sectorY*16+localY;
            const quint16 packed=finalization.packedCells.at(globalY*width+globalX);
            qToLittleEndian<quint16>(packed,reinterpret_cast<uchar*>(
                payload.data()+int(offset)+(localY*16+localX)*2));
        }
        const ETerrainTileLayer pinnedLayer=layer==ETerrainTileLayer::Land
            ?ETerrainTileLayer::Overlay:ETerrainTileLayer::Land;
        if(pinnedLayer==ETerrainTileLayer::Land||sector.hasOverlay)
        {
            const quint64 pinnedOffset=pinnedLayer==ETerrainTileLayer::Land
                ?sector.landTileOffset:sector.overlayTileOffset;
            for(int local=0;local<16*16;++local)
            {
                // Inactive overlay cells deliberately store 0xffff. It is an
                // absence sentinel, not atlas slot 16383, and must survive a
                // land-only finalization exactly.
                if(pinnedLayer==ETerrainTileLayer::Overlay
                   && local<sector.overlayMaterialIndices.size()
                   && sector.overlayMaterialIndices.at(local)==-1)
                    continue;
                const quint16 before=qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar*>(payload.constData()+int(pinnedOffset)+local*2));
                SMprPackedTile packed=SMprPackedTile::decode(before);
                if(!finalization.authoredSlotRemap.contains(packed.atlasSlot()))
                {
                    result.error=QStringLiteral(
                        "The preserved terrain layer references atlas slot %1, which was not pinned.")
                        .arg(packed.atlasSlot());
                    return result;
                }
                const int remapped=finalization.authoredSlotRemap.value(packed.atlasSlot());
                packed.tileIndex=quint8(remapped%64);packed.atlasIndex=quint8(remapped/64);
                qToLittleEndian<quint16>(packed.encode(),reinterpret_cast<uchar*>(
                    payload.data()+int(pinnedOffset)+local*2));
            }
        }
        transaction.replaceMemberPayload(member.name,member.sha256,payload);
    }
    if(!source.applyIsolatedNamespaceTransaction(transaction,
        finalization.isolatedNamespace,&result.mprBytes,report)){
        result.error=QStringLiteral("The isolated MPR transaction failed fresh reopen.");
        return result;
    }
    QByteArray empty(16,'\0');
    qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument archive;QStringList errors;
    if(!archive.loadBytes(empty,QStringLiteral("generated-atlas.res"),&errors)){
        result.error=errors.join(QStringLiteral("\n"));return result;
    }
    QVector<SResArchiveNewMember> appendedMembers;
    appendedMembers.reserve(finalization.pageMmpPayloads.size());
    for(int page=0;page<finalization.pageMmpPayloads.size();++page){
        const QString member=QStringLiteral("%1%2.mmp")
            .arg(finalization.isolatedNamespace.toLower())
            .arg(page,3,10,QLatin1Char('0'));
        SResArchiveNewMember appended;
        appended.name=member;
        appended.payload=finalization.pageMmpPayloads.at(page);
        appendedMembers.append(appended);
        result.resourceMembers.append(member);
    }
    QByteArray archiveBytes;
    if(!archive.buildWithAppendedMembers(appendedMembers,&archiveBytes,&errors)){
        result.error=errors.join(QStringLiteral("\n"));return result;
    }
    CMprDocument reopenedMpr;CValidationReport reopenReport;
    CResArchiveDocument reopenedRes;
    if(!reopenedMpr.loadBytes(result.mprBytes,QStringLiteral("portable.mpr"),&reopenReport)
       ||reopenReport.hasErrors()
       ||reopenedMpr.mapInfo().textureCount!=quint32(finalization.textureCount)
       ||reopenedMpr.mapInfo().tileCount!=quint32(finalization.tileTypes.size())
       ||!reopenedRes.loadBytes(archiveBytes,QStringLiteral("portable.res"),&errors)){
        result.error=QStringLiteral("The portable atlas package failed fresh reopen.");return result;
    }
    for(int page=0;page<result.resourceMembers.size();++page)
        if(reopenedRes.memberPayload(result.resourceMembers.at(page))
           !=finalization.pageMmpPayloads.at(page)){
            result.error=QStringLiteral("A reopened atlas page differs from the staged payload.");
            return result;
        }
    result.resourceArchiveBytes=archiveBytes;result.success=true;return result;
}

STerrainPortableAtlasPackage CTerrainVirtualAtlas::buildWorkingPreviewPackage(
    const CMprDocument& source, const QVector<QImage>& activeTiles,
    const QVector<qint32>& activeTileTypes,
    const QVector<QByteArray>& authoredPagePayloads,
    CValidationReport* report)
{
    STerrainPortableAtlasPackage result;
    const SMprMapInfo& map = source.mapInfo();
    if(!map.valid || activeTiles.isEmpty() || activeTiles.size() % 64 != 0
       || activeTiles.size() > kMprPackedTerrainAtlasSlotLimit
       || authoredPagePayloads.size() != int(map.textureCount)
       || activeTiles.size() < int(map.tileCount)
       || activeTileTypes.size() != activeTiles.size())
    {
        result.error = QStringLiteral(
            "The working terrain atlas does not match the staged MPR baseline.");
        return result;
    }
    const int textureCount = activeTiles.size() / 64;
    if(textureCount < authoredPagePayloads.size())
    {
        result.error = QStringLiteral(
            "The working terrain atlas would discard authored texture pages.");
        return result;
    }
    const QSize tileSize = activeTiles.front().size();
    if(tileSize.isEmpty())
    {
        result.error = QStringLiteral("The working terrain tiles have no pixels.");
        return result;
    }
    for(const QImage& tile : activeTiles)
        if(tile.size() != tileSize || tile.isNull())
        {
            result.error = QStringLiteral(
                "The working terrain atlas contains a missing or mismatched tile.");
            return result;
        }

    const QByteArray originalMp = source.memberPayload(map.memberName);
    const int tileTypeOffset = 38 + int(map.materialCount) * 44;
    const int oldTailOffset = tileTypeOffset + int(map.tileCount) * 4;
    if(tileTypeOffset < 38 || oldTailOffset > originalMp.size())
    {
        result.error = QStringLiteral("The staged MP tile-type array is truncated.");
        return result;
    }
    const QVector<qint32> tileTypes = activeTileTypes;
    for(int slot = 0; slot < tileTypes.size(); ++slot)
        if(tileTypes.at(slot) < 0 || tileTypes.at(slot) >= 16)
        {
            result.error = QStringLiteral(
                "Working atlas slot %1 has no reviewed runtime terrain type; staging was refused.")
                .arg(slot);
            if(report) report->add("terrain.working_type_unknown",
                EValidationSeverity::Error, result.error);
            return result;
        }
    QByteArray replacementMp = originalMp.left(tileTypeOffset);
    for(qint32 type : tileTypes)
    {
        char bytes[4];
        qToLittleEndian<qint32>(type, reinterpret_cast<uchar*>(bytes));
        replacementMp.append(bytes, 4);
    }
    replacementMp.append(originalMp.mid(oldTailOffset));
    qToLittleEndian<quint32>(quint32(textureCount),
        reinterpret_cast<uchar*>(replacementMp.data() + 16));
    qToLittleEndian<quint32>(quint32(tileSize.width() * 8),
        reinterpret_cast<uchar*>(replacementMp.data() + 20));
    qToLittleEndian<quint32>(quint32(activeTiles.size()),
        reinterpret_cast<uchar*>(replacementMp.data() + 24));
    qToLittleEndian<quint32>(quint32(tileSize.width()),
        reinterpret_cast<uchar*>(replacementMp.data() + 28));

    CMprEditTransaction transaction(QStringLiteral(
        "Stage working terrain atlas metadata"));
    QByteArray originalMpSha256;
    for(const SMprArchiveMember& member : source.members())
        if(member.name.compare(map.memberName, Qt::CaseInsensitive) == 0)
        {
            originalMpSha256 = member.sha256;
            break;
        }
    transaction.replaceMemberPayload(map.memberName, originalMpSha256,
                                     replacementMp);
    if(!source.applyTransaction(transaction, &result.mprBytes, report))
    {
        result.error = QStringLiteral(
            "The working terrain atlas metadata transaction failed.");
        return result;
    }

    QVector<QByteArray> pagePayloads = authoredPagePayloads;
    pagePayloads.reserve(textureCount);
    for(int page = authoredPagePayloads.size(); page < textureCount; ++page)
    {
        QString diagnostic;
        const QImage pixels = composePage(activeTiles, page * 64,
                                          tileSize.width());
        const QByteArray payload = ei::encodeMmpDxt1(pixels, true, &diagnostic);
        if(payload.isEmpty())
        {
            result.error = QStringLiteral(
                "Could not encode working atlas page %1: %2")
                .arg(page).arg(diagnostic);
            return result;
        }
        pagePayloads.append(payload);
    }

    QByteArray empty(16, '\0');
    qToLittleEndian<quint32>(0x019CE23C,
        reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,
        reinterpret_cast<uchar*>(empty.data() + 8));
    CResArchiveDocument archive;
    QStringList errors;
    if(!archive.loadBytes(empty, QStringLiteral("working-atlas.res"), &errors))
    {
        result.error = errors.join(QStringLiteral("\n"));
        return result;
    }
    QVector<SResArchiveNewMember> members;
    members.reserve(textureCount);
    for(int page = 0; page < textureCount; ++page)
    {
        SResArchiveNewMember member;
        member.name = QStringLiteral("%1%2.mmp")
            .arg(map.innerNamespace.toLower())
            .arg(page, 3, 10, QLatin1Char('0'));
        member.payload = pagePayloads.at(page);
        members.append(member);
        result.resourceMembers.append(member.name);
    }
    if(!archive.buildWithAppendedMembers(members,
                                          &result.resourceArchiveBytes,
                                          &errors))
    {
        result.error = errors.join(QStringLiteral("\n"));
        return result;
    }

    CMprDocument reopenedMpr;
    CValidationReport reopenReport;
    CResArchiveDocument reopenedRes;
    if(!reopenedMpr.loadBytes(result.mprBytes, QStringLiteral("working.mpr"),
                              &reopenReport)
       || reopenReport.hasErrors()
       || reopenedMpr.mapInfo().textureCount != quint32(textureCount)
       || reopenedMpr.mapInfo().tileCount != quint32(activeTiles.size())
       || !reopenedRes.loadBytes(result.resourceArchiveBytes,
                                 QStringLiteral("working.res"), &errors))
    {
        result.error = QStringLiteral(
            "The working terrain atlas package failed fresh reopen.");
        return result;
    }
    for(int page = 0; page < textureCount; ++page)
        if(reopenedRes.memberPayload(result.resourceMembers.at(page))
           != pagePayloads.at(page))
        {
            result.error = QStringLiteral(
                "A reopened working atlas page differs from its staged payload.");
            return result;
        }
    result.success = true;
    return result;
}

CTerrainWorkingAtlas::CTerrainWorkingAtlas(
    const CTerrainTransitionCatalog& authoredCatalog,
    const QVector<QImage>& authoredTiles, quint32 distributionSeed,
    int capacityLimit,
    const QMap<qint32, QImage>& externalMaterialSources,
    const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles,
    const QVector<qint32>& sourceTileTypes,
    const QMap<qint32, qint32>& materialRuntimeTypes)
    : m_authoredCatalog(authoredCatalog), m_workingCatalog(authoredCatalog),
      m_authoredImages(authoredTiles), m_workingImages(authoredTiles),
      m_workingTileTypes(sourceTileTypes),
      m_capacityLimit(qBound(authoredTiles.size(), capacityLimit,
                             kMprPackedTerrainAtlasSlotLimit))
{
    if(!authoredTiles.isEmpty() && m_workingImages.size() < m_capacityLimit)
    {
        QImage blank(authoredTiles.front().size(), QImage::Format_RGBA8888);
        blank.fill(Qt::transparent);
        m_workingImages.resize(m_capacityLimit);
        for(int slot = authoredTiles.size(); slot < m_workingImages.size(); ++slot)
            m_workingImages[slot] = blank;
    }
    m_workingTileTypes.resize(m_capacityLimit);
    for(int slot = sourceTileTypes.size(); slot < m_workingTileTypes.size(); ++slot)
        m_workingTileTypes[slot] = -1;
    m_resolver.reset(new CTerrainVirtualAtlas(
        m_authoredCatalog, m_authoredImages, distributionSeed,
        externalMaterialSources, materialBlendStyles, true,
        sourceTileTypes, materialRuntimeTypes));
    for(int slot = 0; slot < m_authoredImages.size(); ++slot)
    {
        const QPair<int, int> key(0, slot);
        m_virtualToWorking.insert(key, slot);
        m_workingToVirtual.insert(slot, key);
    }
}

bool CTerrainWorkingAtlas::isValid() const
{
    return m_capacityLimit > 0 && m_resolver && m_resolver->isValid();
}

QString CTerrainWorkingAtlas::error() const
{
    if(m_capacityLimit <= 0)
        return QStringLiteral("The working atlas has no validated physical slots.");
    return m_resolver ? m_resolver->error()
                      : QStringLiteral("The working atlas resolver is unavailable.");
}

int CTerrainWorkingAtlas::generatedTileCount() const
{
    return m_resolver ? m_resolver->generatedTiles().size() : 0;
}

bool CTerrainWorkingAtlas::addAuthoredLibraryTransition(
    const STerrainTransitionSignature& signature, const QImage& pixels,
    const QString& sourceLabel, CValidationReport* report)
{
    return m_resolver && m_resolver->addAuthoredLibraryTransition(
        signature, pixels, sourceLabel, report);
}

bool CTerrainWorkingAtlas::addStockBlendMaskTemplate(
    const STerrainTransitionSignature& signature, const QImage& pixels,
    const QString& sourceLabel, CValidationReport* report)
{
    return m_resolver && m_resolver->addStockBlendMaskTemplate(
        signature, pixels, sourceLabel, report);
}

int CTerrainWorkingAtlas::stockBlendMaskCountFor(
    const STerrainTransitionSignature& signature) const
{
    return m_resolver ? m_resolver->stockBlendMaskCountFor(signature) : 0;
}

bool CTerrainWorkingAtlas::realizeAuthoredLibraryVariants(
    const STerrainTransitionSignature& signature,
    QMap<int, quint64>* workingUsage, const QSet<int>& protectedSlots,
    CValidationReport* report, qint32 preferredPaintMaterial)
{
    if(!m_resolver || !workingUsage)
        return false;
    const QVector<STerrainVirtualTile> variants =
        m_resolver->authoredLibraryVariants(
            signature, preferredPaintMaterial, report);
    if(variants.isEmpty())
        return false;
    QSet<int> reserved = protectedSlots;
    for(const STerrainVirtualTile& variant : variants)
    {
        const int workingSlot = realize(
            true, variant.generatedIndex, workingUsage, reserved, report);
        if(workingSlot < 0)
            return false;
        reserved.insert(workingSlot);
    }
    return true;
}

int CTerrainWorkingAtlas::activeTextureCount() const
{
    const int highest = m_workingToVirtual.isEmpty()
        ? -1 : m_workingToVirtual.lastKey();
    return qMax(1, (highest + 64) / 64);
}

QVector<QImage> CTerrainWorkingAtlas::activeTileImages() const
{
    return m_workingImages.mid(0, activeTextureCount() * 64);
}

QVector<qint32> CTerrainWorkingAtlas::activeTileTypes() const
{
    return m_workingTileTypes.mid(0, activeTextureCount() * 64);
}

qint32 CTerrainWorkingAtlas::runtimeTerrainTypeForSlot(int slot) const
{
    return slot >= 0 && slot < m_workingTileTypes.size()
        ? m_workingTileTypes.at(slot) : -1;
}

QSet<int> CTerrainWorkingAtlas::takeChangedSlots()
{
    const QSet<int> result = m_changedSlots;
    m_changedSlots.clear();
    return result;
}

int CTerrainWorkingAtlas::realize(
    bool generated, int sourceIndex, QMap<int, quint64>* workingUsage,
    const QSet<int>& protectedSlots, CValidationReport* report)
{
    const QPair<int, int> key(generated ? 1 : 0, sourceIndex);
    const auto existing = m_virtualToWorking.constFind(key);
    if(existing != m_virtualToWorking.constEnd())
        return existing.value();
    if(!workingUsage)
        return -1;

    int workingSlot = -1;
    // Authored imagery is immutable for the entire editing session. Generated
    // transitions live only in the extension namespace; an apparently unused
    // stock slot may still be referenced by preserved layers, undo history, or
    // renderer state and must never be recycled under it.
    for(int slot = m_authoredImages.size(); slot < m_capacityLimit; ++slot)
    {
        if(workingUsage->value(slot) == 0 && !protectedSlots.contains(slot))
        {
            workingSlot = slot;
            break;
        }
    }
    if(workingSlot < 0)
    {
        if(report) report->add("terrain.working_atlas_capacity",
            EValidationSeverity::Error,
            QString("The stroke needs another exact transition, but all %1 validated working slots are used or protected by undo history (authored:%2 extension:%3). No identity was substituted.")
                .arg(m_capacityLimit).arg(m_authoredImages.size())
                .arg(qMax(0, m_capacityLimit - m_authoredImages.size())));
        return -1;
    }

    const auto evicted = m_workingToVirtual.constFind(workingSlot);
    if(evicted != m_workingToVirtual.constEnd())
        m_virtualToWorking.remove(evicted.value());
    m_workingToVirtual.remove(workingSlot);
    m_workingCatalog.removeAtlasSlot(workingSlot);

    const QImage pixels = generated
        ? m_resolver->generatedTiles().value(sourceIndex).pixels
        : m_authoredImages.value(sourceIndex);
    if(pixels.isNull())
    {
        if(report) report->add("terrain.working_atlas_pixels",
            EValidationSeverity::Error,
            "The exact transition has no realizable source pixels.");
        return -1;
    }
    m_workingImages[workingSlot] = pixels;
    const qint32 runtimeType = generated
        ? m_resolver->generatedTiles().value(sourceIndex).runtimeTerrainType
        : m_workingTileTypes.value(sourceIndex, -1);
    if(runtimeType < 0 || runtimeType >= 16)
    {
        if(report) report->add("terrain.working_type_unknown",
            EValidationSeverity::Error,
            "The exact transition has no reviewed runtime terrain type.");
        return -1;
    }
    m_workingTileTypes[workingSlot] = runtimeType;
    m_virtualToWorking.insert(key, workingSlot);
    m_workingToVirtual.insert(workingSlot, key);
    installCatalogVariants(generated, sourceIndex, workingSlot);
    m_changedSlots.insert(workingSlot);
    return workingSlot;
}

void CTerrainWorkingAtlas::installCatalogVariants(
    bool generated, int sourceIndex, int workingSlot)
{
    if(generated)
    {
        const STerrainTransitionSignature grid =
            m_resolver->generatedTiles().at(sourceIndex).canonicalSignature;
        const STerrainTransitionSignature base = m_workingCatalog.paintGridMirrored()
            ? CTerrainTransitionCatalog::mirroredGridY(grid) : grid;
        for(int rotation = 0; rotation < 4; ++rotation)
        {
            SMprPackedTile packed;
            packed.tileIndex = quint8(workingSlot % 64);
            packed.atlasIndex = quint8(workingSlot / 64);
            packed.rotation = quint8(rotation);
            STerrainTransitionVariant variant;
            variant.signature = CTerrainTransitionCatalog::rotated(base, -rotation);
            variant.packedTile = packed.encode();
            m_workingCatalog.addVariant(variant);
        }
        return;
    }

    for(int rotation = 0; rotation < 4; ++rotation)
    {
        SMprPackedTile source;
        source.tileIndex = quint8(sourceIndex % 64);
        source.atlasIndex = quint8(sourceIndex / 64);
        source.rotation = quint8(rotation);
        bool found = false;
        const STerrainTransitionVariant sourceVariant =
            m_authoredCatalog.variantForPacked(source.encode(), &found);
        if(!found)
            continue;
        SMprPackedTile packed;
        packed.tileIndex = quint8(workingSlot % 64);
        packed.atlasIndex = quint8(workingSlot / 64);
        packed.rotation = quint8(rotation);
        STerrainTransitionVariant variant = sourceVariant;
        variant.packedTile = packed.encode();
        m_workingCatalog.addVariant(variant);
    }
}

quint16 CTerrainWorkingAtlas::resolve(
    const STerrainTransitionSignature& signature, int cellX, int cellY,
    QMap<int, quint64>* workingUsage, const QSet<int>& protectedSlots,
    bool* generated, CValidationReport* report,
    qint32 preferredPaintMaterial)
{
    if(generated)
        *generated = false;
    if(!isValid())
    {
        if(report) report->add("terrain.working_atlas_invalid",
            EValidationSeverity::Error, error());
        return 0;
    }
    const STerrainVirtualTile tile = m_resolver->resolve(
        signature, cellX, cellY, report, preferredPaintMaterial);
    const bool isGenerated = !tile.authored;
    const int sourceIndex = tile.authored
        ? SMprPackedTile::decode(tile.authoredPacked).atlasSlot()
        : tile.generatedIndex;
    if(sourceIndex < 0)
        return 0;
    const int workingSlot = realize(isGenerated, sourceIndex,
        workingUsage, protectedSlots, report);
    if(workingSlot < 0)
        return 0;
    SMprPackedTile packed;
    packed.tileIndex = quint8(workingSlot % 64);
    packed.atlasIndex = quint8(workingSlot / 64);
    packed.rotation = tile.authored
        ? SMprPackedTile::decode(tile.authoredPacked).rotation
        : tile.generatedRotation;
    if(generated)
        *generated = isGenerated;
    return packed.encode();
}

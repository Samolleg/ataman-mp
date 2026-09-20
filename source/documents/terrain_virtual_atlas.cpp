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
                         const STerrainTransitionSignature& signature,
                         qint32 routeOwnerMaterial = 0)
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
    qToLittleEndian<qint32>(routeOwnerMaterial,
        reinterpret_cast<uchar*>(bytes));
    key.append(bytes, 4);
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

struct SReviewedCanonicalMask
{
    ETerrainBlendStyle style;
    const char* topology;
    const char* resource;
    const char* pixelSha256;
    const char* provenance;
};

const SReviewedCanonicalMask* reviewedCanonicalMask(
    ETerrainBlendStyle style, const QString& topology)
{
    // R155 full effective-mask review rejected the last live mathematical
    // stone edge variant.  Reuse the approved R44 authored stone edge profile
    // as the reviewed source; approvedReviewedMask() mirrors/rotates it to the
    // requested edge-v2 corner ownership instead of using procedural geometry.
    static const SReviewedCanonicalMask r155Masks[] = {
        {ETerrainBlendStyle::Stone, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/stone-edge-2of4-v1-r44-repaired.png",
         "3555e3e95a387d04d219e71cd0de3e8bde69d4bf369cc566f007ee66e83a7090",
         "canonical-generated:stone-fracture:edge-2of4-v2:r155-approved-from-edge-v1"}
    };
    for(const SReviewedCanonicalMask& mask : r155Masks)
        if(mask.style == style
           && topology == QString::fromLatin1(mask.topology))
            return &mask;

    // R123 explicitly reviewed Lava against the real approved-HD Red and
    // Orange uniforms. Lava has no authored transition family, so reuse only
    // the selected, already-approved ownership mattes instead of reaching the
    // obsolete procedural geometry fallback.
    static const SReviewedCanonicalMask r123Masks[] = {
        {ETerrainBlendStyle::Lava, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-outer-1of4-v1-r75-authored.png",
         "8bbcc4762db352ace282d7f98e179d34430b498c3c042774d0a85114a2db7394",
         "canonical-generated:lava-flow:outer-1of4-v1:r123-approved-liquid-flow"},
        {ETerrainBlendStyle::Lava, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-edge-2of4-v1-r76-adjusted.png",
         "1ba9c2e33295d6f400f577a7615a8100963be76de753705f421ea27be436e7a7",
         "canonical-generated:lava-flow:edge-2of4-v1:r123-approved-fracture-flow"},
        {ETerrainBlendStyle::Lava, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/ice-diagonal-2of4-r76-adjusted.png",
         "dbb729aca28b033be78a2f603d95b5f7ee3ad9936b28ddc9de6a3dc0eb73ffc0",
         "canonical-generated:lava-flow:diagonal-2of4:r123-approved-fracture-flow"},
        {ETerrainBlendStyle::Lava, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v1-r76-adjusted.png",
         "47f439119039111046f1aa7bc40b4d404dab03fdf47934707cdb5c39667d15c1",
         "canonical-generated:lava-flow:inside-3of4-v1:r123-approved-fracture-flow"}
    };
    for(const SReviewedCanonicalMask& mask : r123Masks)
        if(mask.style == style
           && topology == QString::fromLatin1(mask.topology))
            return &mask;

    static const SReviewedCanonicalMask r77Masks[] = {
        {ETerrainBlendStyle::Paving, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/paving-edge-2of4-v2-r77-adjusted.png",
         "ff6415e2a059b7f0f4becb6b7655a6a8adb3b199241084d7cffef91a883236ce",
         "canonical-generated:paving-courses:edge-2of4-v2:r77-approved-nonrectilinear-adjustment:Zone10:218"},
        {ETerrainBlendStyle::Paving, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/paving-outer-1of4-v2-r77-adjusted.png",
         "7f2af62ff745000591d72a3ea4229b7485c59967a18c7ce9cb7b6308051a7250",
         "canonical-generated:paving-courses:outer-1of4-v2:r77-approved-nonrectilinear-adjustment:Zone10:509"},
        {ETerrainBlendStyle::Sand, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/sand-outer-1of4-v2-r77-adjusted.png",
         "3c7f026631344e1688f4bf782806608526bc9b1486adb6517d352b8bbad46539",
         "canonical-generated:sand-grain:outer-1of4-v2:r77-approved-nonrectilinear-adjustment:bz5g:467"},
        {ETerrainBlendStyle::Snow, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/snow-edge-2of4-v2-r77-adjusted.png",
         "1803cc86631d8fe57a485c1eef8b785f329af90d184d03fa77258117d5bdf4b6",
         "canonical-generated:snow-clumps:edge-2of4-v2:r77-approved-nonrectilinear-adjustment:bz10k:158"}
    };
    for(const SReviewedCanonicalMask& mask : r77Masks)
        if(mask.style == style
           && topology == QString::fromLatin1(mask.topology))
            return &mask;

    static const SReviewedCanonicalMask r76Masks[] = {
        {ETerrainBlendStyle::Gravel, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-inside-3of4-v2-r76-adjusted.png",
         "c20c0e1e5941773e71a529794e146f58cc2331bc592ad8205479395ed5d1f0b0",
         "canonical-generated:gravel-chunks:inside-3of4-v2:r76-approved-adjustment:bz5g:423"},
        {ETerrainBlendStyle::Ice, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/ice-diagonal-2of4-r76-adjusted.png",
         "dbb729aca28b033be78a2f603d95b5f7ee3ad9936b28ddc9de6a3dc0eb73ffc0",
         "canonical-generated:ice-fracture:diagonal-2of4:r76-approved-adjustment:bz11k:373"},
        {ETerrainBlendStyle::Ice, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-edge-2of4-v1-r76-adjusted.png",
         "1ba9c2e33295d6f400f577a7615a8100963be76de753705f421ea27be436e7a7",
         "canonical-generated:ice-fracture:edge-2of4-v1:r76-approved-adjustment:bz11k:401"},
        {ETerrainBlendStyle::Ice, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/ice-edge-2of4-v2-r76-adjusted.png",
         "e872626e4618432db888ca6dd329c6d457ee20e85fd5361d6d3295fe23720cfc",
         "canonical-generated:ice-fracture:edge-2of4-v2:r76-approved-adjustment:bz11k:364"},
        {ETerrainBlendStyle::Ice, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v1-r76-adjusted.png",
         "47f439119039111046f1aa7bc40b4d404dab03fdf47934707cdb5c39667d15c1",
         "canonical-generated:ice-fracture:inside-3of4-v1:r76-approved-adjustment:bz11k:478"},
        {ETerrainBlendStyle::Ice, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v2-r76-adjusted.png",
         "4f0f7e118e4539bc6c5500a1d6dff4e31799be47d15bc6f964fe0ddb23a6bebe",
         "canonical-generated:ice-fracture:inside-3of4-v2:r76-approved-adjustment:bz11k:438"},
        {ETerrainBlendStyle::Ice, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-outer-1of4-v1-r76-adjusted.png",
         "5c7697948e158f687404c59ca440b6a15b3367f1ad4621fcb55153df2a8f34de",
         "canonical-generated:ice-fracture:outer-1of4-v1:r76-approved-adjustment:bz11k:406"},
        {ETerrainBlendStyle::Ice, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/ice-outer-1of4-v2-r76-adjusted.png",
         "2864da37b32d311f477f3378d4ff0592c4d0bce972dbd4dcf033c9fdfc00ab0b",
         "canonical-generated:ice-fracture:outer-1of4-v2:r76-approved-adjustment:bz11k:405"},
        {ETerrainBlendStyle::Sand, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/sand-inside-3of4-v2-r76-adjusted.png",
         "9ad8602bc5085b0ab4226e4544837988e8911784fc9312d3021eefbad1db312f",
         "canonical-generated:sand-grain:inside-3of4-v2:r76-approved-adjustment:bz5g:180"},
        {ETerrainBlendStyle::Sand, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/sand-outer-1of4-v1-r76-adjusted.png",
         "142edf134ed25289299f4771e83d8b5782cbc2d74e66f57f03a3c6850b2bc0e5",
         "canonical-generated:sand-grain:outer-1of4-v1:r76-approved-adjustment:bz5g:496"},
        {ETerrainBlendStyle::Snow, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/snow-diagonal-2of4-r76-adjusted.png",
         "3547b045e1160533ae609fb353f67efd60eb711a9c68eb9a441b6a21e8e220b6",
         "canonical-generated:snow-clumps:diagonal-2of4:r76-approved-adjustment:bz10k:242"},
        {ETerrainBlendStyle::Snow, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/snow-inside-3of4-v1-r76-adjusted.png",
         "c371c06523916c840b69f5ffb2e2703a12b68dbb3bdb2640b4386be4f57fcdaf",
         "canonical-generated:snow-clumps:inside-3of4-v1:r76-approved-adjustment:bz10k:224"},
        {ETerrainBlendStyle::Snow, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/snow-inside-3of4-v2-r76-adjusted.png",
         "6915fd79c4628e0c8661c9f4bf54017f88eb27201b1fb8df6ad4fb88a5ba03de",
         "canonical-generated:snow-clumps:inside-3of4-v2:r76-approved-adjustment:bz10k:217"},
        {ETerrainBlendStyle::Snow, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/snow-outer-1of4-v1-r76-adjusted.png",
         "301dff7dd30938a8da53e1b9ebae5d427a5c6bd2f7a82798f932bc3a43558cd6",
         "canonical-generated:snow-clumps:outer-1of4-v1:r76-approved-adjustment:bz10k:224"},
        {ETerrainBlendStyle::Snow, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/snow-outer-1of4-v2-r76-adjusted.png",
         "236e3fe45a9e41de68b3d88e1a621632eeaea2ec337107f375620a7427e8dcec",
         "canonical-generated:snow-clumps:outer-1of4-v2:r76-approved-adjustment:bz8k:236"},
        {ETerrainBlendStyle::Stone, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/stone-inside-3of4-v2-r76-adjusted.png",
         "871752a42a283fd144f49ec38c24573a32dca4e6c61a3acf9ad8cea8dd55288f",
         "canonical-generated:stone-fracture:inside-3of4-v2:r76-approved-adjustment:bz10k:143"}
    };
    for(const SReviewedCanonicalMask& mask : r76Masks)
        if(mask.style == style
           && topology == QString::fromLatin1(mask.topology))
            return &mask;

    // Explicit R75 approvals supersede older reviewed masks for the same
    // style/topology.  Keeping this authority separate makes it impossible for
    // a still-pending R75 adjustment request to enter production by accident.
    static const SReviewedCanonicalMask r75Masks[] = {
        {ETerrainBlendStyle::Grass, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/grass-edge-2of4-v2-r75-authored.png",
         "3b7e22c38268056ff48737f8af84c03e5492cc4f1b0ce89d6b945cdb707e0a66",
         "canonical-generated:grass-strands:edge-2of4-v2:r75-approved-authored-restoration:bz3g:184"},
        {ETerrainBlendStyle::Grass, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/grass-inside-3of4-v2-r75-authored.png",
         "e82bd23fe502ef031e9c4001d567e1ce4a3243b879968a68cace5a6a2e9c7e52",
         "canonical-generated:grass-strands:inside-3of4-v2:r75-approved-authored-restoration:Zone5_1:110"},
        {ETerrainBlendStyle::Grass, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/grass-outer-1of4-v2-r75-authored.png",
         "f982aad28868aede6379e489a24668576831f1b7ac967e1cb044f4b31d55de80",
         "canonical-generated:grass-strands:outer-1of4-v2:r75-approved-authored-restoration:Zone3Obr:435"},
        {ETerrainBlendStyle::Gravel, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-edge-2of4-v2-r75-authored.png",
         "dadcb6ed1c915fbccf78111c74dd174db6b284d72cb968c81d4277067e7277f3",
         "canonical-generated:gravel-chunks:edge-2of4-v2:r75-approved-authored-restoration:bz13h:414"},
        {ETerrainBlendStyle::Gravel, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-inside-3of4-v1-r75-authored.png",
         "1048f7a93ae4f3704ef205afb3804cf7aa37671237eb5a2e4bcb17eb2f94f6ed",
         "canonical-generated:gravel-chunks:inside-3of4-v1:r75-approved-authored-restoration:bz5g:496"},
        {ETerrainBlendStyle::Gravel, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-outer-1of4-v1-r75-authored.png",
         "9ffc86db49fcf58605e69c366ed6469a657246f39800a1f91bd87d484c9bbcb6",
         "canonical-generated:gravel-chunks:outer-1of4-v1:r75-approved-authored-restoration:bz13h:213"},
        {ETerrainBlendStyle::Gravel, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-outer-1of4-v2-r75-authored.png",
         "33f15a7716cbf5f87716a98c9b35d8e2e74c64367098acf21e6586fe4e41373f",
         "canonical-generated:gravel-chunks:outer-1of4-v2:r75-approved-authored-restoration:bz13h:214"},
        {ETerrainBlendStyle::Paving, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/paving-inside-3of4-v2-r75-authored.png",
         "686d6c02ac891b0721870a433f307bd44988015d22509235e5ce822e49ec0ee1",
         "canonical-generated:paving-courses:inside-3of4-v2:r75-approved-authored-restoration:Zone10:499"},
        {ETerrainBlendStyle::Sand, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/sand-edge-2of4-v1-r75-authored.png",
         "0f17756cfea98c8725e05d379e888249f111efa1ca0a570e56fecbd68ed23c99",
         "canonical-generated:sand-grain:edge-2of4-v1:r75-approved-authored-restoration:bz16h:155"},
        {ETerrainBlendStyle::Sand, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/sand-edge-2of4-v2-r75-authored.png",
         "0c142f61025b54f5b2147ac9961e9b9ad280120c0313a5842c87d00ebf5ccc3f",
         "canonical-generated:sand-grain:edge-2of4-v2:r75-approved-authored-restoration:bz16h:161"},
        {ETerrainBlendStyle::Sand, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/sand-inside-3of4-v1-r75-authored.png",
         "cd04494e556162822d919e0b8a2c7289d61024dc54267f83185aa592dddf9e20",
         "canonical-generated:sand-grain:inside-3of4-v1:r75-approved-authored-restoration:Zone7:297"},
        {ETerrainBlendStyle::Snow, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/snow-edge-2of4-v1-r75-authored.png",
         "143f5b7599608c91420618753aa2641bf64f56e974fcfe59caff4b77818fd293",
         "canonical-generated:snow-clumps:edge-2of4-v1:r75-approved-authored-restoration:bz10k:156"},
        {ETerrainBlendStyle::Soil, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/soil-diagonal-2of4-r75-authored.png",
         "c406fc01d3c31ce9bd4742ddd024fcfeabaf7c9e78b9d2251bce8efff9db6e5e",
         "canonical-generated:soil-mottle:diagonal-2of4:r75-approved-authored-restoration:Zone1:353"},
        {ETerrainBlendStyle::Soil, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/soil-edge-2of4-v1-r75-authored.png",
         "14e1ba8841bd685d938c23534ffe90cc6bf9a9aba7c3caae37d6ff2223762fa3",
         "canonical-generated:soil-mottle:edge-2of4-v1:r75-approved-authored-restoration:bz16h:111"},
        {ETerrainBlendStyle::Soil, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/soil-edge-2of4-v2-r75-authored.png",
         "25c8b5c55dc820868669e56c788c555757e5930746e9ced4bea2c380d9961c9f",
         "canonical-generated:soil-mottle:edge-2of4-v2:r75-approved-authored-restoration:bz16h:114"},
        {ETerrainBlendStyle::Soil, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/soil-inside-3of4-v1-r75-authored.png",
         "71374bd31017e599c3216106552307f922c68c57f3ee40bc99b0e1ee8820d4a2",
         "canonical-generated:soil-mottle:inside-3of4-v1:r75-approved-authored-restoration:bz4g:191"},
        {ETerrainBlendStyle::Soil, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/soil-inside-3of4-v2-r75-authored.png",
         "0afae4e6b53b3238788e53955995a92407076a640eaa617bb1a064fa6d769566",
         "canonical-generated:soil-mottle:inside-3of4-v2:r75-approved-authored-restoration:bz3g:97"},
        {ETerrainBlendStyle::Soil, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/soil-outer-1of4-v1-r75-authored.png",
         "f92b2531c420b89ef3a9a706444f501eef19b65217b9122f6d820cf96cf7b664",
         "canonical-generated:soil-mottle:outer-1of4-v1:r75-approved-authored-restoration:bz4g:191"},
        {ETerrainBlendStyle::Soil, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/soil-outer-1of4-v2-r75-authored.png",
         "5a930baa567911ceb215d95863acf75ccc455b5378cd4d5ba055f6229c215c38",
         "canonical-generated:soil-mottle:outer-1of4-v2:r75-approved-authored-restoration:Zone7:297"},
        {ETerrainBlendStyle::Stone, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/stone-outer-1of4-v2-r75-authored.png",
         "c008228b70109b4d02fa93988c565e4a0e2f4978335ee4a2570ab23a2074fca6",
         "canonical-generated:stone-fracture:outer-1of4-v2:r75-approved-authored-restoration:bz10k:216"},
        {ETerrainBlendStyle::Water, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/water-diagonal-2of4-r75-authored.png",
         "376b90b14ac98d1b29bb2a405ce361fad3a6d2ef317145514e600e80fbb073e3",
         "canonical-generated:water-ripples:diagonal-2of4:r75-approved-authored-restoration:Zone1:351"},
        {ETerrainBlendStyle::Water, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-edge-2of4-v1-r75-authored.png",
         "d6d712616251dfc2b424d60852e67d12ce9366d327d42f8319a7a04824b677f3",
         "canonical-generated:water-ripples:edge-2of4-v1:r75-approved-authored-restoration:Zone3Obr:249"},
        {ETerrainBlendStyle::Water, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/water-edge-2of4-v2-r75-authored.png",
         "a88fffed2e8f74b49a7c090cc9caaa8389a5ce92b517f2a8ee6bb19ca4f9150f",
         "canonical-generated:water-ripples:edge-2of4-v2:r75-approved-authored-restoration:Zone3Obr:496"},
        {ETerrainBlendStyle::Water, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-inside-3of4-v1-r75-authored.png",
         "5838ab682cd53a8c16606f9b133f7c237bb8c2179d8e0e1737a49730b5503ddd",
         "canonical-generated:water-ripples:inside-3of4-v1:r75-approved-authored-restoration:Zone3Obr:435"},
        {ETerrainBlendStyle::Water, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/water-inside-3of4-v2-r75-authored.png",
         "d0060c4b54bdc2e5fd9d27aa65ab80e7761fbe481a5cbd5fb1ca1b0449f65666",
         "canonical-generated:water-ripples:inside-3of4-v2:r75-approved-authored-restoration:Zone1:223"},
        {ETerrainBlendStyle::Water, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-outer-1of4-v1-r75-authored.png",
         "8bbcc4762db352ace282d7f98e179d34430b498c3c042774d0a85114a2db7394",
         "canonical-generated:water-ripples:outer-1of4-v1:r75-approved-authored-restoration:Zone3Obr:235"},
        {ETerrainBlendStyle::Water, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/water-outer-1of4-v2-r75-authored.png",
         "617f5f3ee4231049108dec6334028667652a5604f8a7b2f53f207861c1f9d14b",
         "canonical-generated:water-ripples:outer-1of4-v2:r75-approved-authored-restoration:Zone3Obr:166"}
    };
    for(const SReviewedCanonicalMask& mask : r75Masks)
        if(mask.style == style
           && topology == QString::fromLatin1(mask.topology))
            return &mask;

    static const SReviewedCanonicalMask masks[] = {
        {ETerrainBlendStyle::Grass, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/grass-outer-1of4-v1-r44-repaired.png",
         "7161801ae53a63a4968abc3a39eb6f9971a5c3786d168c9c7644d5a95f442876",
         "canonical-generated:grass-strands:outer-1of4-v1:r44-approved"},
        {ETerrainBlendStyle::Grass, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/grass-edge-2of4-v1-r44-repaired.png",
         "be0f21c2e9d01ce56b4003411e63ed70026820725701d460b1466711f2277bca",
         "canonical-generated:grass-strands:edge-2of4-v1:r44-approved"},
        {ETerrainBlendStyle::Grass, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/grass-diagonal-2of4-r44-repaired.png",
         "39a4bd49aa72525cb955d44e797235cf102d549ea5b1aec995fa6095dc457945",
         "canonical-generated:grass-strands:diagonal-2of4:r44-approved"},
        {ETerrainBlendStyle::Soil, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/soil-outer-1of4-v2-r44-repaired.png",
         "f9ea6dde4e3cd8095d79d4650aaccf0f70cae20991dccc9a5fa8fdc9a89f08df",
         "canonical-generated:soil-mottle:outer-1of4-v2:r44-approved"},
        {ETerrainBlendStyle::Soil, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/soil-edge-2of4-v2-r44-repaired.png",
         "168f98d2d6b6ab53598cb30622b784aaf7a0eadb8815968e238b2a19ec7f7bd3",
         "canonical-generated:soil-mottle:edge-2of4-v2:r44-approved"},
        {ETerrainBlendStyle::Soil, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/soil-inside-3of4-v1-r44-repaired.png",
         "1953ce4e0062f23052e59a9fc6ae6e8d9324345c2caba7110823bf624a30e998",
         "canonical-generated:soil-mottle:inside-3of4-v1:r44-approved"},
        {ETerrainBlendStyle::Stone, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/stone-outer-1of4-v1-r44-repaired.png",
         "2c107b3db7f37268082b952e2038419aa4b8371a28d28abbbb8cbd89a832505e",
         "canonical-generated:stone-fracture:outer-1of4-v1:r44-approved"},
        {ETerrainBlendStyle::Stone, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/stone-edge-2of4-v1-r44-repaired.png",
         "3555e3e95a387d04d219e71cd0de3e8bde69d4bf369cc566f007ee66e83a7090",
         "canonical-generated:stone-fracture:edge-2of4-v1:r44-approved"},
        {ETerrainBlendStyle::Stone, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/stone-diagonal-2of4-r44-repaired.png",
         "eea78b03896e90d1137cc79f7991f17912248ab5480e55415bc5e0fcd502a002",
         "canonical-generated:stone-fracture:diagonal-2of4:r44-approved"},
        {ETerrainBlendStyle::Sand, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/sand-outer-1of4-v1-r47-detailed.png",
         "01cb028bd15bec4642750ea35825423aa1b54b1cbc98275975071fb5399210b9",
         "canonical-generated:sand-grain:outer-1of4-v1:r47-approved"},
        {ETerrainBlendStyle::Sand, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/sand-edge-2of4-v2-r47-detailed.png",
         "0f0b55bce8ecb96963d467897c5dcdec71a09af343545d6350b8e965d6ea0ddb",
         "canonical-generated:sand-grain:edge-2of4-v2:r47-approved"},
        {ETerrainBlendStyle::Sand, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/sand-inside-3of4-v1-r47-detailed.png",
         "581cb3c7c4ad785da566f21eeae319ad7c2e2a24e4695f2df4658635062dad56",
         "canonical-generated:sand-grain:inside-3of4-v1:r47-approved"},
        {ETerrainBlendStyle::Snow, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/snow-outer-1of4-v1-r53-detailed.png",
         "b4eeffed78157e662b508d66eb8e675d4b98bfe55ee6d9d1135c4a2c97e40eaf",
         "canonical-generated:snow-clumps:outer-1of4-v1:r53-approved"},
        {ETerrainBlendStyle::Snow, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/snow-outer-1of4-v2-r53-detailed.png",
         "3970d79ffae6c1d3faccac4e839135864cf44879d423d4fbcf5f373e09416c72",
         "canonical-generated:snow-clumps:outer-1of4-v2:r53-approved"},
        {ETerrainBlendStyle::Snow, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/snow-edge-2of4-v1-r53-detailed.png",
         "cdc9c3f16dccdcd88cf952dbf8c239e510ff1372148eb9a47193d3d0d3972a2e",
         "canonical-generated:snow-clumps:edge-2of4-v1:r53-approved"},
        {ETerrainBlendStyle::Snow, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/snow-edge-2of4-v2-r53-detailed.png",
         "d8971e2ac0b5ee39d5b86ed174995166fc7e1b85658846fb41016e8bc568b9f8",
         "canonical-generated:snow-clumps:edge-2of4-v2:r53-approved"},
        {ETerrainBlendStyle::Snow, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/snow-diagonal-2of4-r54-detailed.png",
         "07f01530b7bd83018cc04831ba1e1a7dd7da9209d36c6e546987626188732970",
         "canonical-generated:snow-clumps:diagonal-2of4:r54-approved"},
        {ETerrainBlendStyle::Snow, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/snow-inside-3of4-v1-r54-detailed.png",
         "b576e2c9ce1b055325b715aa51a8e53aed9bccf98af9a2ed4ea007ed597e9ed3",
         "canonical-generated:snow-clumps:inside-3of4-v1:r54-approved"},
        {ETerrainBlendStyle::Snow, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/snow-inside-3of4-v2-r54-detailed.png",
         "b576e2c9ce1b055325b715aa51a8e53aed9bccf98af9a2ed4ea007ed597e9ed3",
         "canonical-generated:snow-clumps:inside-3of4-v2:r54-approved"},
        {ETerrainBlendStyle::Ice, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-outer-1of4-v1-r53-detailed.png",
         "740a2e823540ca51db9f3606d612cef30c0aac7a6cd513dfb53101e769a576ae",
         "canonical-generated:ice-fracture:outer-1of4-v1:r53-approved"},
        {ETerrainBlendStyle::Ice, "outer-1of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/ice-outer-1of4-v2-r53-detailed.png",
         "845be2dce324f8a209a2f5cc50fb869ce1c75da30b0dcce755337aa2e148225f",
         "canonical-generated:ice-fracture:outer-1of4-v2:r53-approved"},
        {ETerrainBlendStyle::Ice, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-edge-2of4-v1-r53-detailed.png",
         "3878111591a0787ca4e723dd6b0be7455ba9dcd986a3cfefc5aaf1059d3e8f48",
         "canonical-generated:ice-fracture:edge-2of4-v1:r53-approved"},
        {ETerrainBlendStyle::Ice, "edge-2of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/ice-edge-2of4-v2-r53-detailed.png",
         "14f4e15a6cf02f2f93ef09ebae3590e050a53fa5455aaaae403ceb5d6bd289f1",
         "canonical-generated:ice-fracture:edge-2of4-v2:r53-approved"},
        {ETerrainBlendStyle::Ice, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/ice-diagonal-2of4-r54-detailed.png",
         "0b00ef5bf181d9741ab982a8cd4b225338d451516274343354158f38447c9183",
         "canonical-generated:ice-fracture:diagonal-2of4:r54-approved"},
        {ETerrainBlendStyle::Ice, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v1-r63.png",
         "1617f1c7b073aae77ad0d181368e83d68f2b013e612101c4dc3eb257625f247b",
         "canonical-generated:ice-fracture:inside-3of4-v1:r63-approved"},
        {ETerrainBlendStyle::Ice, "inside-3of4-v2",
         ":/terrain_blend_masks/terrain_blend_masks/ice-inside-3of4-v2-r63.png",
         "53c17ae64fffa80b182002d4b9e44c54ce381c750329e689eabd65e4ae1aae8d",
         "canonical-generated:ice-fracture:inside-3of4-v2:r63-approved"},
        {ETerrainBlendStyle::Grass, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/grass-inside-3of4-v1-r71-repaired.png",
         "f6d66a4adbf2b207a56e71052009801a4d3d4f254ab98f73b38ddfdaabb97d57",
         "canonical-generated:grass-strands:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Stone, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/stone-inside-3of4-v1-r71-repaired.png",
         "6daedacfdf19739a485e004812d12edfae348e876fffa3153521b5e9379040ea",
         "canonical-generated:stone-fracture:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Gravel, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-outer-1of4-v1-r71-repaired.png",
         "eb23ccb05747b9e97792661c4e59e8c9968cef7373f8cbde8ece1181fdb98fd2",
         "canonical-generated:gravel-chunks:outer-1of4-v1:r71-approved"},
        {ETerrainBlendStyle::Gravel, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-edge-2of4-v1-r71-repaired.png",
         "af124c29a708ac3a4f9160e3c7f894aeea0de4e41ea22b7b8b49a95618cb68be",
         "canonical-generated:gravel-chunks:edge-2of4-v1:r71-approved"},
        {ETerrainBlendStyle::Gravel, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-diagonal-2of4-r71-repaired.png",
         "71bfa4e9f7f7ad04ea5c774a36811c0d31ac549a82eb15df0ec87d8d0504c9d9",
         "canonical-generated:gravel-chunks:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Gravel, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/gravel-inside-3of4-v1-r71-repaired.png",
         "c0e0b78765fa1615da376335c95f2d80aca1f803d28c2cda3010ae2f7dfe864d",
         "canonical-generated:gravel-chunks:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Paving, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/paving-outer-1of4-v1-r71-raw.png",
         "f8f6c3247ad468c6e8558111f4e6b8fe96221076402882aa90d4ace40d67a21c",
         "canonical-generated:paving-courses:outer-1of4-v1:r71-approved"},
        {ETerrainBlendStyle::Paving, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/paving-edge-2of4-v1-r71-raw.png",
         "ab9b12faecc9c4494c7eeaeea9eee3553df20c4150bf8945cc1847c6b3734868",
         "canonical-generated:paving-courses:edge-2of4-v1:r71-approved"},
        {ETerrainBlendStyle::Paving, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/paving-diagonal-2of4-r71-raw.png",
         "18a869617280149a2cf28df72b0a9cc6431a8f47cc3e98ea739a9d8f1932a200",
         "canonical-generated:paving-courses:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Paving, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/paving-inside-3of4-v1-r71-raw.png",
         "f91bd0998972534cfe74f55a0491549a944dc336e40deea2079c67392f57298f",
         "canonical-generated:paving-courses:inside-3of4-v1:r71-approved"},
        {ETerrainBlendStyle::Sand, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/sand-diagonal-2of4-r71-detailed.png",
         "b0d911e4e6a0516ca1f4f43e9fe6096a2ca5f017173cdd267c59bf1e91f6ddb1",
         "canonical-generated:sand-grain:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Water, "outer-1of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-outer-1of4-v1-r71-conservative.png",
         "8df2999930ab72c856d6d0744483ecb5a69bb7a04ff8e11660f64f98ab858e9b",
         "canonical-generated:water-ripples:outer-1of4-v1:r71-approved"},
        {ETerrainBlendStyle::Water, "edge-2of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-edge-2of4-v1-r71-conservative.png",
         "f6de1773292e3063bc58715182767b9cc6d74f984269c98a8267d0fcb127265b",
         "canonical-generated:water-ripples:edge-2of4-v1:r71-approved"},
        {ETerrainBlendStyle::Water, "diagonal-2of4",
         ":/terrain_blend_masks/terrain_blend_masks/water-diagonal-2of4-r71-detailed.png",
         "38f05a9ba431ff65abfa859ee6dab3d400a15d9886776a0500373bcb2a235448",
         "canonical-generated:water-ripples:diagonal-2of4:r71-approved"},
        {ETerrainBlendStyle::Water, "inside-3of4-v1",
         ":/terrain_blend_masks/terrain_blend_masks/water-inside-3of4-v1-r71-detailed.png",
         "c0231ccb71e9adf6c8f5eba7470ef994f545d0ea24fea9961571be363dba0e80",
         "canonical-generated:water-ripples:inside-3of4-v1:r71-approved"}
    };
    for(const SReviewedCanonicalMask& mask : masks)
        if(mask.style == style
           && topology == QString::fromLatin1(mask.topology))
            return &mask;
    return nullptr;
}

QVector<int> reviewedCanonicalVariations(
    const STerrainTransitionSignature& signature,
    ETerrainBlendStyle style, qint32 reviewedStyleMaterial)
{
    QVector<int> result;
    if(reviewedStyleMaterial == 0)
        return result;
    const QVector<qint32> ids = corners(signature);
    bool requested[4] = {false, false, false, false};
    int ownerCount = 0;
    for(int corner = 0; corner < ids.size(); ++corner)
    {
        requested[corner] = ids.at(corner) == reviewedStyleMaterial;
        ownerCount += requested[corner];
    }
    if(ownerCount <= 0 || ownerCount >= 4)
        return result;
    const bool diagonal = ownerCount == 2
        && requested[0] == requested[2]
        && requested[1] == requested[3];
    const int limit = diagonal ? 1 : 2;
    for(int variation = 0; variation < limit; ++variation)
        if(reviewedCanonicalMask(style, canonicalMaskTopologyName(
               ownerCount, diagonal, variation)))
            result.append(variation);
    return result;
}

void rotateCornerPatternClockwise(bool pattern[4])
{
    const bool before[4] = {
        pattern[0], pattern[1], pattern[2], pattern[3]
    };
    pattern[0] = before[3];
    pattern[1] = before[0];
    pattern[2] = before[1];
    pattern[3] = before[2];
}

QImage approvedReviewedMask(
    const QSize& size, const STerrainTransitionSignature& preview,
    qint32 ownerMaterial, ETerrainBlendStyle style,
    qint32 reviewedStyleMaterial, int variation, QString* provenance)
{
    if(size.width() <= 0 || size.width() != size.height()
       || reviewedStyleMaterial == 0)
        return {};
    const QVector<qint32> ids = corners(preview);
    bool requested[4] = {false, false, false, false};
    int ownerCount = 0;
    for(int corner = 0; corner < ids.size(); ++corner)
    {
        requested[corner] = ids.at(corner) == reviewedStyleMaterial;
        ownerCount += requested[corner];
    }
    if(ownerCount <= 0 || ownerCount >= 4)
        return {};
    const bool diagonal = ownerCount == 2
        && requested[0] == requested[2]
        && requested[1] == requested[3];
    const QString topology = canonicalMaskTopologyName(
        ownerCount, diagonal, variation);
    const SReviewedCanonicalMask* reviewed = reviewedCanonicalMask(
        style, topology);
    if(!reviewed)
        return {};
    QImage result(QString::fromLatin1(reviewed->resource));
    if(result.isNull() || result.size() != QSize(256, 256)
       || grayscaleMaskPixelHash(result)
            != QByteArray(reviewed->pixelSha256))
        return {};
    result = result.convertToFormat(QImage::Format_Grayscale8);
    // Every reviewed asset is stored in the questionnaire's fixed logical
    // corner layout. Cross the grid/preview boundary once, then rotate only
    // until the four semantic corner booleans exactly match the request.
    result = result.mirrored(false, true);
    bool base[4] = {false, false, false, false};
    if(ownerCount == 1) base[0] = true;
    else if(ownerCount == 3) base[0] = base[1] = base[2] = true;
    else if(diagonal) base[0] = base[2] = true;
    else base[0] = base[1] = true;
    const bool logical[4] = {base[0], base[1], base[2], base[3]};
    base[0] = logical[3];
    base[1] = logical[2];
    base[2] = logical[1];
    base[3] = logical[0];
    int turns = -1;
    for(int step = 0; step < 4; ++step)
    {
        // A diagonal signature repeats after 180 degrees, but its authored
        // pixels generally do not. Prefer the grid-covariant counter-clockwise
        // quarter-turn (3 preview-clockwise turns) for the opposite diagonal;
        // never let the boolean symmetry choose the unrelated +90 artwork.
        const int candidate = diagonal
            ? (step == 0 ? 0 : step == 1 ? 3 : step == 2 ? 2 : 1)
            : step;
        bool candidatePattern[4] = {
            base[0], base[1], base[2], base[3]
        };
        for(int turn = 0; turn < candidate; ++turn)
            rotateCornerPatternClockwise(candidatePattern);
        bool matches = true;
        for(int corner = 0; corner < 4; ++corner)
            matches &= candidatePattern[corner] == requested[corner];
        if(matches)
        {
            turns = candidate;
            break;
        }
    }
    if(turns < 0)
        return {};
    result = rotateMaskClockwise(result, turns);
    if(result.size() != size)
        result = result.scaled(size, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_Grayscale8);
    if(ownerMaterial != reviewedStyleMaterial)
        result.invertPixels();
    if(provenance)
        *provenance = QString::fromLatin1(reviewed->provenance);
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
    if(reviewedStyleMaterial != 0)
    {
        const QImage reviewed = approvedReviewedMask(
            size, preview, ownerMaterial, style, reviewedStyleMaterial,
            variation, provenance);
        if(!reviewed.isNull())
            return reviewed;
    }
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

void refreshFinalizationStableIdentity(
    STerrainAtlasFinalization* finalization,
    const QString& sourceNamespace,
    const QByteArray& materialFieldSha256,
    const QVector<quint16>& pinnedPackedTiles)
{
    if(!finalization)
        return;
    QCryptographicHash stable(QCryptographicHash::Sha256);
    stable.addData(sourceNamespace.toUtf8().toLower());
    stable.addData(materialFieldSha256);
    for(const STerrainFinalizedTile& tile : finalization->tiles)
    {
        stable.addData(tile.pixelSha256);
        char typeBytes[4];
        qToLittleEndian<qint32>(tile.runtimeTerrainType,
            reinterpret_cast<uchar*>(typeBytes));
        stable.addData(typeBytes, 4);
        // Keep ownerless legacy/authored finalizations byte-for-byte stable.
        // Only exact generated owner evidence extends the namespace identity.
        if(tile.routeOwnerMaterial > 0)
        {
            stable.addData("route-owner-v1", 14);
            qToLittleEndian<qint32>(tile.routeOwnerMaterial,
                reinterpret_cast<uchar*>(typeBytes));
            stable.addData(typeBytes, 4);
        }
        stable.addData(tile.sourceNamespace.toUtf8().toLower());
        for(int sourceAlias : tile.sourceSlotAliases)
        {
            char aliasBytes[4];
            qToLittleEndian<qint32>(sourceAlias,
                reinterpret_cast<uchar*>(aliasBytes));
            stable.addData(aliasBytes, 4);
        }
    }
    for(quint16 packed : finalization->packedCells)
    {
        char bytes[2];
        qToLittleEndian<quint16>(packed, reinterpret_cast<uchar*>(bytes));
        stable.addData(bytes, 2);
    }
    for(const STerrainAtlasAnimationRange& animation : finalization->animations)
    {
        char bytes[4];
        qToLittleEndian<quint16>(animation.tileIndex,
            reinterpret_cast<uchar*>(bytes));
        qToLittleEndian<quint16>(animation.frameCount,
            reinterpret_cast<uchar*>(bytes + 2));
        stable.addData(bytes, 4);
    }
    for(quint16 packed : pinnedPackedTiles)
    {
        char bytes[2];
        qToLittleEndian<quint16>(packed, reinterpret_cast<uchar*>(bytes));
        stable.addData(bytes, 2);
    }
    finalization->stableSha256 = stable.result().toHex();
    finalization->isolatedNamespace =
        CTerrainVirtualAtlas::contentResourceIdentity(
            sourceNamespace, finalization->stableSha256);
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

bool CTerrainVirtualAtlas::validateResourceIdentity(const QString& identity,
                                                    QString* error)
{
    if(error)
        error->clear();
    const QString value = identity.trimmed();
    if(value.size() < 1 || value.size() > 100)
    {
        if(error)
            *error = QStringLiteral(
                "Terrain resource identity must contain 1 to 100 characters.");
        return false;
    }
    if(!QRegularExpression(QStringLiteral("^[a-z][a-z0-9_]*$"))
            .match(value).hasMatch())
    {
        if(error)
            *error = QStringLiteral(
                "Terrain resource identity must start with a lowercase letter "
                "and use only lowercase ASCII letters, digits, and underscores.");
        return false;
    }
    return true;
}

QString CTerrainVirtualAtlas::contentResourceIdentity(
    const QString& sourceNamespace, const QByteArray& stableSha256)
{
    QString base = sourceNamespace.trimmed().toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]+")),
                 QStringLiteral("_"));
    base.replace(QRegularExpression(QStringLiteral("_+")),
                 QStringLiteral("_"));
    while(base.endsWith(QLatin1Char('_')))
        base.chop(1);
    while(base.startsWith(QLatin1Char('_')))
        base.remove(0, 1);

    // Flatten both the legacy Endurance suffix and the current content suffix.
    // Re-finalizing a published MPR must not create chained resource identities.
    const QRegularExpression legacySuffix(
        QStringLiteral("_eime_[0-9a-f]{12}$"));
    while(legacySuffix.match(base).hasMatch())
        base.remove(legacySuffix);
    const QRegularExpression contentSuffix(
        QStringLiteral("_[0-9a-f]{12}$"));
    if(contentSuffix.match(base).hasMatch())
        base.remove(contentSuffix);

    if(base.isEmpty() || !base.at(0).isLetter())
        base.prepend(QStringLiteral("terrain_"));
    const int hashLength = 12;
    const int maximumBaseLength = 100 - 1 - hashLength;
    base = base.left(maximumBaseLength);
    while(base.endsWith(QLatin1Char('_')))
        base.chop(1);
    const QByteArray contentHash = stableSha256.left(hashLength).toLower();
    const QByteArray suffix = contentHash.size() == hashLength
        ? contentHash
        : QCryptographicHash::hash(stableSha256, QCryptographicHash::Sha256)
              .toHex().left(hashLength);
    return QStringLiteral("%1_%2").arg(
        base, QString::fromLatin1(suffix));
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

bool CTerrainEditModel::copyVirtualMaterialFieldState(
    ETerrainTileLayer layer, QVector<qint32>* current,
    QVector<qint32>* baseline) const
{
    if(!current || !baseline)
        return false;
    *current = layer == ETerrainTileLayer::Land
        ? m_virtualLandMaterials : m_virtualOverlayMaterials;
    *baseline = layer == ETerrainTileLayer::Land
        ? m_originalVirtualLandMaterials : m_originalVirtualOverlayMaterials;
    return true;
}

bool CTerrainEditModel::restoreVirtualMaterialFieldState(
    ETerrainTileLayer layer, const QVector<qint32>& current,
    const QVector<qint32>& baseline, CValidationReport* report)
{
    const int expected = (tileCountX() + 1) * (tileCountY() + 1);
    const bool empty = current.isEmpty() && baseline.isEmpty();
    if(!empty && (current.size() != expected || baseline.size() != expected))
    {
        if(report)
            report->add("terrain.virtual_field_dimensions",
                EValidationSeverity::Error,
                "The stored virtual material field dimensions do not match the MPR terrain grid");
        return false;
    }
    QVector<qint32>& destinationCurrent = layer == ETerrainTileLayer::Land
        ? m_virtualLandMaterials : m_virtualOverlayMaterials;
    QVector<qint32>& destinationBaseline = layer == ETerrainTileLayer::Land
        ? m_originalVirtualLandMaterials : m_originalVirtualOverlayMaterials;
    destinationCurrent = current;
    destinationBaseline = baseline;
    return true;
}

bool CTerrainEditModel::initializeVirtualMaterialField(
    ETerrainTileLayer layer, const CTerrainTransitionCatalog& catalog,
    CValidationReport* report, ETerrainMaterialFieldRecovery recovery)
{
    QVector<qint32>& active = layer == ETerrainTileLayer::Land
        ? m_virtualLandMaterials : m_virtualOverlayMaterials;
    if(!active.isEmpty())
        return active.size() == (tileCountX() + 1) * (tileCountY() + 1);
    const int vertexWidth = tileCountX() + 1;
    QVector<QMap<qint32, int>> votes(
        vertexWidth * (tileCountY() + 1));
    for(int y = 0; y < tileCountY(); ++y)
        for(int x = 0; x < tileCountX(); ++x)
        {
            bool available = false;
            bool decoded = false;
            const quint16 packed = packedTile(x, y, layer, &available);
            if(!available && layer == ETerrainTileLayer::Overlay)
                continue;
            const STerrainTransitionSignature signature = available
                ? catalog.gridSignatureForPacked(packed, &decoded)
                : STerrainTransitionSignature();
            if(!decoded)
            {
                if(recovery == ETerrainMaterialFieldRecovery::RequireExactTopology)
                {
                    if(report)
                        report->add("terrain.virtual_identity_baseline",
                            EValidationSeverity::Error,
                            "The current atlas contains a cell that the semantic profile cannot decode",
                            QStringLiteral("%1,%2").arg(x).arg(y));
                    active.clear();
                    return false;
                }
                if(report)
                    report->add("terrain.virtual_identity_baseline",
                        EValidationSeverity::Warning,
                        "A working draft cell could not vote in deterministic material-field recovery",
                        QStringLiteral("%1,%2").arg(x).arg(y));
                continue;
            }
            const qint32 values[4] = {signature.northWest,
                signature.northEast, signature.southEast,
                signature.southWest};
            const int keys[4] = {y * vertexWidth + x,
                y * vertexWidth + x + 1,
                (y + 1) * vertexWidth + x + 1,
                (y + 1) * vertexWidth + x};
            for(int corner = 0; corner < 4; ++corner)
                ++votes[keys[corner]][values[corner]];
        }
    const qint32 unset = std::numeric_limits<qint32>::min();
    active.fill(unset, votes.size());
    for(int index = 0; index < votes.size(); ++index)
    {
        int bestCount = -1;
        qint32 best = unset;
        for(auto it = votes.at(index).constBegin();
            it != votes.at(index).constEnd(); ++it)
            if(it.value() > bestCount
               || (it.value() == bestCount && it.key() < best))
            {
                bestCount = it.value();
                best = it.key();
            }
        active[index] = best;
    }
    // A legacy draft can contain unavailable edge cells. Those cells cast no
    // corner votes, so deterministic recovery must also close the resulting
    // holes in the vertex field. Leaving an unset border vertex makes the
    // model report a successful initialization while modern painting remains
    // unusable. Propagate only from the immediately adjacent recovered field,
    // using a snapshot per pass so traversal order cannot affect the result.
    if(recovery == ETerrainMaterialFieldRecovery::RecoverDeterministically
       && layer == ETerrainTileLayer::Land)
    {
        int recoveredUnsetVertices = 0;
        for(;;)
        {
            const QVector<qint32> previous = active;
            int filledThisPass = 0;
            for(int y = 0; y <= tileCountY(); ++y)
                for(int x = 0; x <= tileCountX(); ++x)
                {
                    const int index = y * vertexWidth + x;
                    if(previous.at(index) != unset)
                        continue;
                    QMap<qint32, int> neighborVotes;
                    if(x > 0 && previous.at(index - 1) != unset)
                        ++neighborVotes[previous.at(index - 1)];
                    if(x < tileCountX()
                       && previous.at(index + 1) != unset)
                        ++neighborVotes[previous.at(index + 1)];
                    if(y > 0 && previous.at(index - vertexWidth) != unset)
                        ++neighborVotes[previous.at(index - vertexWidth)];
                    if(y < tileCountY()
                       && previous.at(index + vertexWidth) != unset)
                        ++neighborVotes[previous.at(index + vertexWidth)];
                    int bestCount = -1;
                    qint32 best = unset;
                    for(auto it = neighborVotes.constBegin();
                        it != neighborVotes.constEnd(); ++it)
                        if(it.value() > bestCount
                           || (it.value() == bestCount && it.key() < best))
                        {
                            bestCount = it.value();
                            best = it.key();
                        }
                    if(best != unset)
                    {
                        active[index] = best;
                        ++filledThisPass;
                    }
                }
            recoveredUnsetVertices += filledThisPass;
            if(filledThisPass == 0)
                break;
        }
        if(active.contains(unset))
        {
            if(report)
                report->add("terrain.virtual_identity_recovery_incomplete",
                    EValidationSeverity::Error,
                    "Deterministic material-field recovery could not assign every map vertex");
            active.clear();
            return false;
        }
        if(recoveredUnsetVertices > 0 && report)
            report->add("terrain.virtual_identity_vertices_recovered",
                EValidationSeverity::Info,
                QStringLiteral("Recovered %1 material-field vertex hole(s) adjacent to unavailable legacy cells")
                    .arg(recoveredUnsetVertices));
    }
    // This recovered field is the load/legacy baseline. Sparse Manual or
    // uniform-only Modern corners below are current-session intent and must
    // not be copied into that baseline. Route ownership is inferred from the
    // current-versus-baseline delta; assigning the baseline after applying the
    // pending map erased that evidence and made the resolver preserve the
    // freshly painted transition as "ambiguous".
    const QVector<qint32> recoveredBaseline = active;

    // Before the first Modern/Resolve operation, Manual has only its physical
    // MPR edit plus these sparse explicit corners. Recovery votes reconstruct
    // the rest of the legacy map, but must not allow the incident legacy cells
    // to erase a newly selected Manual tile. Keep the sparse state intact: a
    // Resolve undo removes the complete field and therefore needs the exact
    // pre-field Manual intent still available to its preceding undo command.
    const QMap<int, qint32>& pending = layer == ETerrainTileLayer::Land
        ? m_pendingVirtualLandMaterials : m_pendingVirtualOverlayMaterials;
    for(auto value = pending.constBegin(); value != pending.constEnd(); ++value)
        if(value.key() >= 0 && value.key() < active.size())
            active[value.key()] = value.value();
    int recoveredConflicts = 0;
    QString firstRecoveredConflict;
    for(int y = 0; y < tileCountY(); ++y)
        for(int x = 0; x < tileCountX(); ++x)
        {
            bool available = false;
            bool decoded = false;
            const quint16 packed = packedTile(x, y, layer, &available);
            if(!available && layer == ETerrainTileLayer::Overlay)
                continue;
            const STerrainTransitionSignature actual = available
                ? catalog.gridSignatureForPacked(packed, &decoded)
                : STerrainTransitionSignature();
            const int nw = y * vertexWidth + x;
            const int ne = nw + 1;
            const int sw = (y + 1) * vertexWidth + x;
            const int se = sw + 1;
            STerrainTransitionSignature recovered;
            recovered.northWest = active.at(nw);
            recovered.northEast = active.at(ne);
            recovered.southEast = active.at(se);
            recovered.southWest = active.at(sw);
            const bool same = recovered.northWest == actual.northWest
                && recovered.northEast == actual.northEast
                && recovered.southEast == actual.southEast
                && recovered.southWest == actual.southWest;
            if(!decoded || !same)
            {
                if(recovery == ETerrainMaterialFieldRecovery::RequireExactTopology)
                {
                    if(report)
                        report->add("terrain.virtual_identity_topology",
                            EValidationSeverity::Error,
                            "The current packed cells do not form one exact shared-corner semantic field; identity finalization was refused",
                            QStringLiteral("%1,%2").arg(x).arg(y));
                    active.clear();
                    return false;
                }
                ++recoveredConflicts;
                if(firstRecoveredConflict.isEmpty())
                    firstRecoveredConflict = QStringLiteral("%1,%2").arg(x).arg(y);
            }
        }
    if(recoveredConflicts > 0 && report)
        report->add("terrain.virtual_identity_topology_recovered",
            EValidationSeverity::Info,
            QStringLiteral("Recovered one deterministic working-draft material field across %1 packed topology conflict(s)")
                .arg(recoveredConflicts), firstRecoveredConflict);
    if(layer == ETerrainTileLayer::Land)
        m_originalVirtualLandMaterials = recoveredBaseline;
    else
        m_originalVirtualOverlayMaterials = recoveredBaseline;
    return true;
}

bool CTerrainEditModel::recoverUnambiguousVirtualMaterialField(
    ETerrainTileLayer layer, const CTerrainTransitionCatalog& catalog,
    CTerrainVirtualMaterialField* output, CValidationReport* report) const
{
    if(!output || !output->resize(tileCountX(), tileCountY()))
        return false;
    const int vertexWidth = tileCountX() + 1;
    QVector<QSet<qint32>> observed(
        vertexWidth * (tileCountY() + 1));
    int undecodableCells = 0;
    for(int y = 0; y < tileCountY(); ++y)
        for(int x = 0; x < tileCountX(); ++x)
        {
            bool available = false;
            bool decoded = false;
            const quint16 packed = packedTile(x, y, layer, &available);
            if(!available && layer == ETerrainTileLayer::Overlay)
                continue;
            const STerrainTransitionSignature signature = available
                ? catalog.gridSignatureForPacked(packed, &decoded)
                : STerrainTransitionSignature();
            if(!decoded)
            {
                ++undecodableCells;
                continue;
            }
            const qint32 values[4] = {signature.northWest,
                signature.northEast, signature.southEast,
                signature.southWest};
            const int keys[4] = {y * vertexWidth + x,
                y * vertexWidth + x + 1,
                (y + 1) * vertexWidth + x + 1,
                (y + 1) * vertexWidth + x};
            for(int corner = 0; corner < 4; ++corner)
                observed[keys[corner]].insert(values[corner]);
        }

    const qint32 unset = std::numeric_limits<qint32>::min();
    int ambiguousVertices = 0;
    for(int y = 0; y <= tileCountY(); ++y)
        for(int x = 0; x <= tileCountX(); ++x)
        {
            const QSet<qint32>& values = observed.at(y * vertexWidth + x);
            if(values.size() == 1)
                output->setMaterial(x, y, *values.constBegin());
            else
            {
                output->setMaterial(x, y, unset);
                ++ambiguousVertices;
            }
        }
    if((ambiguousVertices > 0 || undecodableCells > 0) && report)
        report->add("terrain.virtual_repair_ambiguous_vertices",
            EValidationSeverity::Info,
            QStringLiteral("Legacy terrain disagrees at %1 shared corner(s), and %2 cell(s) could not be decoded. Those locations were left unchanged while exact repairs continued elsewhere.")
                .arg(ambiguousVertices).arg(undecodableCells));
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
        // An opened-map uniform is authoritative for that map. External
        // reviewed sources fill only genuine gaps; they never replace local
        // artwork merely because the logical material id is shared.
        if(m_materialSources.contains(source.key()))
            continue;
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

CTerrainVirtualAtlas::CTerrainVirtualAtlas(
    const CTerrainVirtualAtlas& other,
    const CTerrainTransitionCatalog& catalog)
    : m_catalog(catalog),
      m_authoredTiles(other.m_authoredTiles),
      m_materialSources(other.m_materialSources),
      m_materialSourceSlots(other.m_materialSourceSlots),
      m_sourceTileTypes(other.m_sourceTileTypes),
      m_materialRuntimeTypes(other.m_materialRuntimeTypes),
      m_materialBlendStyles(other.m_materialBlendStyles),
      m_generatedBySignature(other.m_generatedBySignature),
      m_generatedByPixels(other.m_generatedByPixels),
      m_generatedTiles(other.m_generatedTiles),
      m_blendMaskTemplates(other.m_blendMaskTemplates),
      m_authoredLibraryCatalog(other.m_authoredLibraryCatalog),
      m_authoredLibraryGeneratedIndices(
          other.m_authoredLibraryGeneratedIndices),
      m_distributionSeed(other.m_distributionSeed),
      m_generatedTilesOpaque(other.m_generatedTilesOpaque),
      m_valid(other.m_valid),
      m_error(other.m_error)
{
}

bool CTerrainVirtualAtlas::addExternalMaterialSource(
    qint32 material, const QImage& pixels)
{
    if(m_materialSources.contains(material))
        return true;
    if(!m_valid || pixels.isNull() || m_authoredTiles.isEmpty()
       || pixels.size() != m_authoredTiles.front().size())
        return false;
    m_materialSources.insert(material, m_generatedTilesOpaque
        ? opaqueTerrainMaterialSource(pixels)
        : pixels.convertToFormat(QImage::Format_RGBA8888));
    m_materialSourceSlots.remove(material);
    return true;
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
    QMap<qint32, int> counts;
    for(qint32 material : corners(signature))
        ++counts[material];
    if(preferredPaintMaterial != 0
       && counts.contains(preferredPaintMaterial)
       && m_materialBlendStyles.value(
              preferredPaintMaterial, ETerrainBlendStyle::Organic)
              == style)
        return preferredPaintMaterial;
    // R63's Ice inside masks were explicitly approved before paint-material
    // ownership was threaded through every context-free caller. Preserve that
    // narrow fallback. Other reviewed families are activated only by the
    // selected paint material and an exact reviewed style/topology lookup.
    if(style != ETerrainBlendStyle::Ice)
        return 0;
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
        // Newer approval provenance retains the authored namespace/slot after
        // the review marker.  Approval is an explicit segment, not a suffix.
        if(canonicalProvenance.contains(QStringLiteral("-approved")))
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

QImage CTerrainVirtualAtlas::reviewEffectiveBlendMask(
    const STerrainTransitionSignature& gridSignature,
    qint32 ownerMaterial, int variation, QString* provenance,
    qint32 preferredPaintMaterial) const
{
    QImage result = reviewBlendMask(
        gridSignature, ownerMaterial, variation, provenance,
        preferredPaintMaterial);
    if(result.isNull()) return {};
    result = result.convertToFormat(QImage::Format_Grayscale8);
    for(int y = 0; y < result.height(); ++y)
        for(int x = 0; x < result.width(); ++x)
        {
            const double source = result.pixelColor(x, y).redF();
            const double weight = smoothUnit((source - 0.38) / 0.24);
            // Preserve exact 0/1 plateaus, but never label a non-zero mixed
            // runtime weight as a pure source pixel merely because its 8-bit
            // review visualization rounds to an endpoint.
            const int effective = weight <= 0.0 ? 0
                : weight >= 1.0 ? 255
                : qBound(1, qRound(weight * 255.0), 254);
            result.setPixelColor(x, y, QColor(effective, effective, effective));
        }

    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(gridSignature);
    const QVector<qint32> ids = corners(preview);
    const auto edgeOwnerValue = [&](qint32 firstMaterial,
                                    qint32 secondMaterial,
                                    double position) {
        if(firstMaterial == secondMaterial)
            return firstMaterial == ownerMaterial ? 255 : 0;
        const int secondWeight = qRound(
            smoothUnit((position - 0.38) / 0.24) * 255.0);
        if(ownerMaterial == firstMaterial) return 255 - secondWeight;
        if(ownerMaterial == secondMaterial) return secondWeight;
        return 0;
    };
    const auto setValue = [&](int x, int y, int value) {
        result.setPixelColor(x, y, QColor(value, value, value));
    };
    const int width = result.width();
    const int height = result.height();
    for(int x = 0; x < width; ++x)
    {
        const double position = width == 1
            ? 0.0 : double(x) / double(width - 1);
        setValue(x, 0, edgeOwnerValue(ids.at(0), ids.at(1), position));
        setValue(x, height - 1,
                 edgeOwnerValue(ids.at(3), ids.at(2), position));
    }
    for(int y = 0; y < height; ++y)
    {
        const double position = height == 1
            ? 0.0 : double(y) / double(height - 1);
        setValue(0, y, edgeOwnerValue(ids.at(0), ids.at(3), position));
        setValue(width - 1, y,
                 edgeOwnerValue(ids.at(1), ids.at(2), position));
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
        // Multi-family junctions use one complete topology mask for every
        // material's corner assignment. The material itself owns the reviewed
        // mask language, so the R44-R77 authored approvals remain active here
        // exactly as they are for two-family blends. This also preserves one
        // connected region for adjacent repeated corners instead of a union of
        // unrelated one-corner masks.
        for(qint32 material : unique)
        {
            QString source;
            const QImage mask = canonicalGeneratedOwnershipMask(
                m_authoredTiles.front().size(), preview, material,
                m_materialBlendStyles.value(material, style), variation,
                material,
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

QImage CTerrainVirtualAtlas::reviewGeneratedJunctionOwnershipMask(
    const STerrainTransitionSignature& gridSignature,
    qint32 ownerMaterial, int variation, QString* provenance) const
{
    if(!m_valid || m_authoredTiles.isEmpty()) return {};
    const STerrainTransitionSignature preview =
        CTerrainTransitionCatalog::mirroredGridY(gridSignature);
    const QVector<qint32> ids = corners(preview);
    QSet<qint32> unique;
    for(qint32 id : ids) unique.insert(id);
    if(unique.size() < 3 || !unique.contains(ownerMaterial)) return {};
    return canonicalGeneratedOwnershipMask(
        m_authoredTiles.front().size(), preview, ownerMaterial,
        m_materialBlendStyles.value(ownerMaterial,
            ETerrainBlendStyle::Organic), qMax(0, variation), ownerMaterial,
        provenance);
}

bool CTerrainVirtualAtlas::addAuthoredLibraryTransition(
    const STerrainTransitionSignature& signature, const QImage& pixels,
    const QString& sourceLabel, CValidationReport* report,
    const QString& sourceNamespace, int sourceSlot)
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
    imported.sourceSlot = sourceSlot;
    imported.sourceNamespace = sourceNamespace;
    if(sourceSlot >= 0)
        imported.sourceSlotAliases.append(sourceSlot);
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
    qint32 preferredPaintMaterial, qint32 preferredRuntimeType,
    CValidationReport* report, bool requirePreferredRuntimeType)
{
    QVector<STerrainVirtualTile> result;
    const qint32 runtimeType = resolvedRuntimeType(
        signature, preferredPaintMaterial, preferredRuntimeType, report,
        requirePreferredRuntimeType);
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
    CValidationReport* report, bool requirePreferredRuntimeType) const
{
    if(requirePreferredRuntimeType)
    {
        if(preferredRuntimeType >= 0 && preferredRuntimeType < 16)
            return preferredRuntimeType;
        if(report) report->add("terrain.required_runtime_type_invalid",
            EValidationSeverity::Error,
            "An exact predecessor-preserving resolution requires a reviewed runtime terrain type.");
        return -1;
    }
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
        m_generatedTiles.at(sourceIndex).canonicalSignature,
        m_generatedTiles.at(sourceIndex).routeOwnerMaterial);
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
    qint32 preferredRuntimeType, bool requirePreferredRuntimeType)
{
    STerrainVirtualTile result;
    result.signature = signature;
    if(!m_valid)
    {
        if(report) report->add("terrain.virtual_atlas_invalid",
            EValidationSeverity::Error, m_error);
        return result;
    }
    const qint32 requiredRuntimeType = requirePreferredRuntimeType
        ? resolvedRuntimeType(signature, preferredPaintMaterial,
                              preferredRuntimeType, report, true)
        : -1;
    if(requirePreferredRuntimeType && requiredRuntimeType < 0)
        return result;

    bool authored = false;
    quint16 packed = 0;
    if(requirePreferredRuntimeType)
    {
        QSet<int> allowedSlots;
        const int sourceSlotCount = qMin(m_authoredTiles.size(),
                                         m_sourceTileTypes.size());
        for(int slot = 0; slot < sourceSlotCount; ++slot)
            if(m_sourceTileTypes.at(slot) == requiredRuntimeType)
                allowedSlots.insert(slot);
        packed = m_catalog.selectForPaint(
            signature, cellX, cellY, m_distributionSeed, allowedSlots,
            preferredPaintMaterial, &authored);
    }
    else
        packed = m_catalog.selectForPaint(
            signature, cellX, cellY, m_distributionSeed,
            preferredPaintMaterial, &authored);
    if(authored)
    {
        result.authored = true;
        result.authoredPacked = packed;
        const int slot = SMprPackedTile::decode(packed).atlasSlot();
        result.runtimeTerrainType = slot >= 0 && slot < m_sourceTileTypes.size()
            ? m_sourceTileTypes.at(slot) : -1;
        return result;
    }

    const qint32 runtimeType = requirePreferredRuntimeType
        ? requiredRuntimeType : resolvedRuntimeType(
            signature, preferredPaintMaterial, preferredRuntimeType, report,
            false);
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
    QSet<qint32> generatedMaterials;
    for(qint32 id : corners(canonical))
        generatedMaterials.insert(id);
    const qint32 generatedRouteOwner = generatedMaterials.size() == 2
        && preferredPaintMaterial != 0
        && generatedMaterials.contains(preferredPaintMaterial)
            ? preferredPaintMaterial : 0;
    const ETerrainBlendStyle style = blendStyleForSignature(
        canonical, preferredPaintMaterial);
    qint32 reviewedStyleMaterial = reviewedMaskStyleMaterial(
        canonical, style, preferredPaintMaterial);
    if(reviewedStyleMaterial == 0 && preferredPaintMaterial == 0)
    {
        // Production repair/reopen paths have no active brush, but they must
        // not ignore an already reviewed authored-derived mask and fall back
        // to obsolete procedural geometry. Infer only a same-style owner that
        // actually has this canonical topology approved; otherwise retain the
        // explicit mathematical fallback so it remains auditable for review.
        QMap<qint32, int> counts;
        for(qint32 material : corners(canonical))
            ++counts[material];
        qint32 candidate = 0;
        int candidateCount = std::numeric_limits<int>::max();
        for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            if(m_materialBlendStyles.value(
                   it.key(), ETerrainBlendStyle::Organic) == style
               && (it.value() < candidateCount
                   || (it.value() == candidateCount
                       && (candidate == 0 || it.key() < candidate))))
            {
                candidate = it.key();
                candidateCount = it.value();
            }
        if(counts.size() == 2 && candidate != 0
           && !reviewedCanonicalVariations(
               canonical, style, candidate).isEmpty())
            reviewedStyleMaterial = candidate;
    }
    int variation = 0;
    const QVector<int> reviewedVariations = reviewedCanonicalVariations(
        canonical, style, reviewedStyleMaterial);
    if(reviewedVariations.isEmpty())
        variation = blendVariation(
            cellX, cellY, style, blendVariationLimit(canonical));
    else
        variation = reviewedVariations.at(blendVariation(
            cellX, cellY, style, reviewedVariations.size()));
    QByteArray key = signatureKey(canonical);
    key.append(char(terrainBlendStylePriority(style)));
    key.append(char(variation));
    char typeBytes[4];
    qToLittleEndian<qint32>(reviewedStyleMaterial,
        reinterpret_cast<uchar*>(typeBytes));
    key.append(typeBytes, 4);
    qToLittleEndian<qint32>(generatedRouteOwner,
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
            typedPixelKey(pixelsHash, runtimeType, canonical,
                          generatedRouteOwner), -1);
        if(index < 0)
        {
            STerrainFinalizedTile generated;
            generated.canonicalSignature = canonical;
            generated.pixels = pixels;
            generated.pixelSha256 = pixelsHash;
            generated.authored = false;
            generated.blendProvenance = provenance;
            generated.canonicalGeneratedBlend = generatedMaterials.size() > 1;
            generated.routeOwnerMaterial = generatedRouteOwner;
            generated.runtimeTerrainType = runtimeType;
            index = m_generatedTiles.size();
            m_generatedTiles.append(generated);
            m_generatedByPixels.insert(
                typedPixelKey(pixelsHash, runtimeType, canonical,
                              generated.routeOwnerMaterial), index);
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

QImage CTerrainVirtualAtlas::composeAtlasGrid(
    const QVector<QImage>& tiles, int textureCount, int columns)
{
    if(textureCount <= 0 || tiles.size() != textureCount * 64
       || columns <= 0 || columns > textureCount || tiles.isEmpty())
        return {};
    const int tileSize = tiles.front().width();
    if(tileSize <= 0 || tiles.front().height() != tileSize)
        return {};
    for(const QImage& tile : tiles)
        if(tile.size() != QSize(tileSize, tileSize))
            return {};
    const int pageSize = tileSize * 8;
    const int rows = (textureCount + columns - 1) / columns;
    QImage combined(pageSize * columns, pageSize * rows,
                    QImage::Format_RGBA8888);
    combined.fill(Qt::transparent);
    for(int page = 0; page < textureCount; ++page)
    {
        const QImage pageImage = composePage(tiles, page * 64, tileSize);
        if(pageImage.size() != QSize(pageSize, pageSize))
            return {};
        const int pageColumn = page % columns;
        const int pageRow = page / columns;
        for(int y = 0; y < pageSize; ++y)
            memcpy(combined.scanLine(pageRow * pageSize + y)
                       + pageColumn * pageSize * 4,
                   pageImage.constScanLine(y), size_t(pageSize) * 4u);
    }
    return combined;
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
    const QVector<quint16>& preferredPackedCells,
    const QVector<qint32>& preferredPaintMaterials,
    const CTerrainTransitionCatalog* authoredDecodeCatalog,
    const QVector<bool>& preservePreferredPackedCells,
    const QVector<STerrainAtlasAnimationRange>& animations)
{
    STerrainAtlasFinalization result;
    result.capacityLimit = capacityLimit;
    if(!m_valid || field.tileWidth() <= 0 || field.tileHeight() <= 0
       || capacityLimit <= 0 || capacityLimit > 16384
       || (!preferredPackedCells.isEmpty()
           && preferredPackedCells.size()
                != field.tileWidth() * field.tileHeight())
       || (!preferredPaintMaterials.isEmpty()
           && preferredPaintMaterials.size()
                != field.tileWidth() * field.tileHeight())
       || (!preservePreferredPackedCells.isEmpty()
           && preservePreferredPackedCells.size()
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
    // Runtime terrain type is part of physical authored identity. Some stock
    // atlases intentionally reuse identical pixels for distinct gameplay
    // terrain classes, so those aliases must not be deduplicated together.
    QMap<QByteArray, QVector<int>> exactAuthoredOutputs;
    // Flipbooks address contiguous physical atlas slots. Their phases must not
    // collapse into one another or become aliases for visually identical static
    // tiles, otherwise compaction changes both timing and unrelated terrain.
    QSet<int> animatedOutputSlots;
    const CTerrainTransitionCatalog& decodeCatalog = authoredDecodeCatalog
        ? *authoredDecodeCatalog : m_catalog;
    const auto addAuthoredOutput = [&](int sourceSlot,
                                       const QString& rangeError,
                                       bool animationPhase = false) -> bool
    {
        if(authoredOutput.contains(sourceSlot))
            return true;
        if(sourceSlot < 0 || sourceSlot >= m_authoredTiles.size())
        {
            result.error = rangeError;
            return false;
        }

        STerrainFinalizedTile candidate;
        candidate.authored = true;
        candidate.sourceSlot = sourceSlot;
        candidate.sourceNamespace = sourceNamespace;
        candidate.sourceSlotAliases.append(sourceSlot);
        candidate.pixels = m_authoredTiles.at(sourceSlot);
        candidate.pixelSha256 = imageHash(candidate.pixels);
        candidate.runtimeTerrainType = sourceSlot < sourceTileTypes.size()
            ? sourceTileTypes.at(sourceSlot) : -1;
        bool signatureFound = false;
        SMprPackedTile basePacked;
        basePacked.tileIndex = quint8(sourceSlot % 64);
        basePacked.atlasIndex = quint8(sourceSlot / 64);
        candidate.canonicalSignature = decodeCatalog.gridSignatureForPacked(
            basePacked.encode(), &signatureFound);
        bool provenanceFound = false;
        const STerrainTransitionVariant provenanceVariant =
            decodeCatalog.variantForPacked(
                basePacked.encode(), &provenanceFound);
        if(provenanceFound)
            candidate.routeOwnerMaterial =
                provenanceVariant.routeOwnerMaterial;
        const bool signatureAssigned = signatureFound
            && candidate.canonicalSignature.northWest > 0
            && candidate.canonicalSignature.northEast > 0
            && candidate.canonicalSignature.southEast > 0
            && candidate.canonicalSignature.southWest > 0;
        if(!signatureAssigned)
        {
            result.error = QStringLiteral(
                "The authored atlas source slot %1 has no complete semantic signature; "
                "finalization cannot preserve its terrain meaning.").arg(sourceSlot);
            if(report) report->add(
                "terrain.finalized_authored_signature",
                EValidationSeverity::Error, result.error,
                QStringLiteral("source slot %1").arg(sourceSlot));
            return false;
        }

        const QByteArray dedupeKey = candidate.pixelSha256
            + QByteArray(1, '\0')
            + QByteArray::number(candidate.runtimeTerrainType)
            + QByteArray(1, '\0')
            + QByteArray::number(candidate.routeOwnerMaterial);
        const QVector<int> matchingHashes = exactAuthoredOutputs.value(dedupeKey);
        for(int outputSlot : matchingHashes)
        {
            if(animationPhase || animatedOutputSlots.contains(outputSlot))
                continue;
            const STerrainFinalizedTile& retained = result.tiles.at(outputSlot);
            if(retained.pixels != candidate.pixels)
                continue; // SHA-256 collision: preserve both physical tiles.
            const bool semanticsAgree = signatureAssigned
                && retained.canonicalSignature == candidate.canonicalSignature
                && retained.runtimeTerrainType == candidate.runtimeTerrainType
                && retained.routeOwnerMaterial
                    == candidate.routeOwnerMaterial;
            if(!semanticsAgree)
            {
                result.error = QStringLiteral(
                    "Exact authored artwork conflict between source slots %1 and %2; "
                    "their semantic corners differ despite matching pixels and "
                    "runtime terrain type. "
                    "Finalization was refused pending semantic review.")
                    .arg(retained.sourceSlot).arg(sourceSlot);
                if(report) report->add(
                    "terrain.finalized_authored_duplicate_conflict",
                    EValidationSeverity::Error, result.error,
                    QStringLiteral("pixel SHA-256 %1")
                        .arg(QString::fromLatin1(candidate.pixelSha256)));
                return false;
            }

            authoredOutput.insert(sourceSlot, outputSlot);
            result.authoredSlotRemap.insert(sourceSlot, outputSlot);
            QVector<int>& aliases = result.tiles[outputSlot].sourceSlotAliases;
            if(!aliases.contains(sourceSlot))
            {
                aliases.append(sourceSlot);
                std::sort(aliases.begin(), aliases.end());
            }
            result.tiles[outputSlot].sourceSlot = aliases.front();
            return true;
        }

        const int outputSlot = denseImages.size();
        authoredOutput.insert(sourceSlot, outputSlot);
        result.authoredSlotRemap.insert(sourceSlot, outputSlot);
        denseImages.append(candidate.pixels);
        result.tiles.append(candidate);
        exactAuthoredOutputs[dedupeKey].append(outputSlot);
        if(animationPhase)
            animatedOutputSlots.insert(outputSlot);
        return true;
    };
    // Reserve only the live frame blocks before cells and pinned layers are
    // compacted. The runtime advances a flipbook's local tile index while
    // retaining its MMP texture, so every block must remain contiguous inside
    // one 64-slot page. Preserving the complete source page retained obsolete
    // pre-repack frame copies and unrelated blend tiles indefinitely. Merge
    // overlapping source ranges, pack those blocks atomically, and use only
    // otherwise-live authored slots as page alignment when two blocks cannot
    // share the same output page.
    struct SAnimationSourceBlock
    {
        int start{0};
        int end{0};
    };
    QVector<SAnimationSourceBlock> animationBlocks;
    QSet<int> animatedSourceSlots;
    const int liveSourceSlotCount = sourceTileTypes.isEmpty()
        ? m_authoredTiles.size()
        : qMin(m_authoredTiles.size(), sourceTileTypes.size());
    for(int animationIndex = 0; animationIndex < animations.size();
        ++animationIndex)
    {
        const STerrainAtlasAnimationRange& animation = animations.at(animationIndex);
        const int sourceStart = int(animation.tileIndex);
        const int physicalCount = qMax(1, int(animation.frameCount));
        if(sourceStart < 0
           || sourceStart + physicalCount > liveSourceSlotCount)
        {
            result.error = QStringLiteral(
                "Animated terrain range %1 points outside the live working atlas.")
                .arg(animationIndex);
            if(report) report->add("terrain.finalized_animation_range",
                EValidationSeverity::Error, result.error,
                QStringLiteral("start %1, frames %2")
                    .arg(sourceStart).arg(animation.frameCount));
            return result;
        }
        if(physicalCount > kTerrainAtlasSlotsPerPage
           || sourceStart % kTerrainAtlasSlotsPerPage + physicalCount
                > kTerrainAtlasSlotsPerPage)
        {
            result.error = QStringLiteral(
                "Animated terrain range %1 crosses an MMP texture page. The game advances flipbook frames only inside the starting page.")
                .arg(animationIndex);
            if(report) report->add("terrain.finalized_animation_page",
                EValidationSeverity::Error, result.error,
                QStringLiteral("start %1, frames %2")
                    .arg(sourceStart).arg(animation.frameCount));
            return result;
        }
        SAnimationSourceBlock block;
        block.start = sourceStart;
        block.end = sourceStart + physicalCount;
        animationBlocks.append(block);
        for(int sourceSlot = block.start; sourceSlot < block.end; ++sourceSlot)
            animatedSourceSlots.insert(sourceSlot);
    }
    std::sort(animationBlocks.begin(), animationBlocks.end(), [](
        const SAnimationSourceBlock& left,
        const SAnimationSourceBlock& right) {
        return left.start != right.start
            ? left.start < right.start : left.end < right.end;
    });
    QVector<SAnimationSourceBlock> mergedBlocks;
    for(const SAnimationSourceBlock& block : animationBlocks)
    {
        if(mergedBlocks.isEmpty()
           || block.start > mergedBlocks.back().end)
            mergedBlocks.append(block);
        else
            mergedBlocks.back().end = qMax(
                mergedBlocks.back().end, block.end);
    }
    for(const SAnimationSourceBlock& block : mergedBlocks)
    {
        const int blockLength = block.end - block.start;
        const int outputOffset = denseImages.size()
            % kTerrainAtlasSlotsPerPage;
        if(outputOffset + blockLength > kTerrainAtlasSlotsPerPage)
        {
            int paddingNeeded = kTerrainAtlasSlotsPerPage - outputOffset;
            for(int sourceSlot = 0;
                sourceSlot < liveSourceSlotCount && paddingNeeded > 0;
                ++sourceSlot)
            {
                if(animatedSourceSlots.contains(sourceSlot)
                   || authoredOutput.contains(sourceSlot))
                    continue;
                const int before = denseImages.size();
                if(!addAuthoredOutput(sourceSlot, QStringLiteral(
                       "An animated terrain page-alignment slot points outside the live working atlas."),
                       true))
                    return result;
                if(denseImages.size() > before)
                    --paddingNeeded;
            }
            if(paddingNeeded > 0)
            {
                result.error = QStringLiteral(
                    "Animated terrain blocks cannot be aligned without inventing atlas padding.");
                if(report) report->add(
                    "terrain.finalized_animation_alignment",
                    EValidationSeverity::Error, result.error,
                    QStringLiteral("source block %1-%2")
                        .arg(block.start).arg(block.end - 1));
                return result;
            }
        }
        for(int sourceSlot = block.start; sourceSlot < block.end; ++sourceSlot)
            if(!addAuthoredOutput(sourceSlot, QStringLiteral(
                   "An animated terrain block points outside the live working atlas."),
                   true))
                return result;
    }

    for(int animationIndex = 0; animationIndex < animations.size();
        ++animationIndex)
    {
        const STerrainAtlasAnimationRange& animation = animations.at(animationIndex);
        const int sourceStart = int(animation.tileIndex);
        const int physicalCount = qMax(1, int(animation.frameCount));
        const int outputStart = authoredOutput.value(sourceStart, -1);
        if(outputStart < 0
           || outputStart % kTerrainAtlasSlotsPerPage + physicalCount
                > kTerrainAtlasSlotsPerPage)
        {
            result.error = QStringLiteral(
                "A finalized animated terrain range does not fit inside one MMP texture page.");
            if(report) report->add("terrain.finalized_animation_page",
                EValidationSeverity::Error, result.error,
                QStringLiteral("animation %1, start %2, frames %3")
                    .arg(animationIndex).arg(outputStart)
                    .arg(animation.frameCount));
            return result;
        }
        for(int phase = 1; phase < physicalCount; ++phase)
            if(authoredOutput.value(sourceStart + phase, -1)
               != outputStart + phase)
            {
                result.error = QStringLiteral(
                    "Overlapping animated terrain ranges cannot be preserved as one contiguous physical sequence.");
                if(report) report->add("terrain.finalized_animation_contiguity",
                    EValidationSeverity::Error, result.error,
                    QStringLiteral("animation %1, phase %2")
                        .arg(animationIndex).arg(phase));
                return result;
            }
        STerrainAtlasAnimationRange remapped;
        remapped.tileIndex = quint16(outputStart);
        remapped.frameCount = animation.frameCount;
        result.animations.append(remapped);
    }
    for(int y = 0; y < field.tileHeight(); ++y)
    {
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            const int cellIndex = y * field.tileWidth() + x;
            const qint32 preferredType = cellIndex < preferredCellTypes.size()
                ? preferredCellTypes.at(cellIndex) : -1;
            const qint32 preferredPaintMaterial =
                cellIndex < preferredPaintMaterials.size()
                    ? preferredPaintMaterials.at(cellIndex) : 0;
            const STerrainTransitionSignature desired =
                field.cellSignature(x, y);
            STerrainVirtualTile tile;
            bool preserved = false;
            if(!preferredPackedCells.isEmpty())
            {
                const quint16 preferredPacked = preferredPackedCells.at(cellIndex);
                bool decoded = false;
                const STerrainTransitionSignature existing =
                    decodeCatalog.gridSignatureForPacked(preferredPacked, &decoded);
                const int preferredSlot = SMprPackedTile::decode(
                    preferredPacked).atlasSlot();
                const bool preserveExisting =
                    !preservePreferredPackedCells.isEmpty()
                    && preservePreferredPackedCells.at(cellIndex);
                if(preserveExisting && (!decoded || preferredSlot < 0
                   || preferredSlot >= m_authoredTiles.size()))
                {
                    result.error = QStringLiteral(
                        "An untouched working-draft cell cannot be decoded from its exact packed atlas identity.");
                    if(report) report->add(
                        "terrain.finalized_preserved_cell",
                        EValidationSeverity::Error, result.error,
                        QStringLiteral("%1,%2").arg(x).arg(y));
                    return result;
                }
                if(decoded && (preserveExisting || existing == desired)
                   && preferredSlot >= 0
                   && preferredSlot < m_authoredTiles.size())
                {
                    tile.signature = preserveExisting ? existing : desired;
                    tile.authored = true;
                    tile.authoredPacked = preferredPacked;
                    tile.runtimeTerrainType = preferredSlot
                        < sourceTileTypes.size()
                        ? sourceTileTypes.at(preferredSlot) : -1;
                    preserved = true;
                }
            }
            if(!preserved)
                tile = resolve(desired, x, y, report,
                               preferredPaintMaterial, preferredType);
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
                if(!addAuthoredOutput(sourceSlot, QStringLiteral(
                    "An authored tile points outside the source atlas.")))
                    return result;
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
    for(quint16 packed : pinnedPackedTiles)
    {
        const int sourceSlot = SMprPackedTile::decode(packed).atlasSlot();
        ++result.usage.authoredSlots[sourceSlot];
        if(!addAuthoredOutput(sourceSlot, QStringLiteral(
            "A pinned layer tile points outside the source atlas.")))
            return result;
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

    refreshFinalizationStableIdentity(
        &result, sourceNamespace, field.stableSha256(), pinnedPackedTiles);
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
    // Finalization pins every live flipbook phase before ordinary compaction;
    // these are the resulting physical ranges. Never infer them afterward from
    // source slot numbers or silently discard a range that happened to move.
    QByteArray remappedAnimations;
    for(int index = 0; index < finalization.animations.size(); ++index)
    {
        const STerrainAtlasAnimationRange& animation =
            finalization.animations.at(index);
        const quint64 physicalCount = qMax<quint64>(1, animation.frameCount);
        if(quint64(animation.tileIndex) + physicalCount
             > quint64(finalization.tileTypes.size()))
        {
            result.error = QStringLiteral(
                "A finalized animated terrain range points outside the compacted atlas.");
            if(report) report->add("terrain.package_animation_range",
                EValidationSeverity::Error, result.error,
                QStringLiteral("animation %1, start %2, frames %3")
                    .arg(index).arg(animation.tileIndex)
                    .arg(animation.frameCount));
            return result;
        }
        if(physicalCount > quint64(kTerrainAtlasSlotsPerPage)
           || int(animation.tileIndex % kTerrainAtlasSlotsPerPage)
                + int(physicalCount) > kTerrainAtlasSlotsPerPage)
        {
            result.error = QStringLiteral(
                "A finalized animated terrain range crosses an MMP texture page.");
            if(report) report->add("terrain.package_animation_page",
                EValidationSeverity::Error, result.error,
                QStringLiteral("animation %1, start %2, frames %3")
                    .arg(index).arg(animation.tileIndex)
                    .arg(animation.frameCount));
            return result;
        }
        char bytes[4];
        qToLittleEndian<quint16>(animation.tileIndex,
            reinterpret_cast<uchar*>(bytes));
        qToLittleEndian<quint16>(animation.frameCount,
            reinterpret_cast<uchar*>(bytes+2));
        remappedAnimations.append(bytes,4);
    }
    replacementMp.append(remappedAnimations);
    replacementMp.append(originalMp.mid(int(map.parsedSize)));
    qToLittleEndian<quint32>(quint32(finalization.textureCount),
        reinterpret_cast<uchar*>(replacementMp.data()+16));
    qToLittleEndian<quint32>(quint32(finalization.pages.front().width()),
        reinterpret_cast<uchar*>(replacementMp.data()+20));
    qToLittleEndian<quint32>(quint32(finalization.tileTypes.size()),
        reinterpret_cast<uchar*>(replacementMp.data()+24));
    qToLittleEndian<quint32>(quint32(finalization.tiles.front().pixels.width()),
        reinterpret_cast<uchar*>(replacementMp.data()+28));
    qToLittleEndian<quint32>(quint32(finalization.animations.size()),
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
    QVector<SMprAnimatedTileRecord> expectedAnimations;
    expectedAnimations.reserve(finalization.animations.size());
    for(const STerrainAtlasAnimationRange& animation : finalization.animations)
    {
        SMprAnimatedTileRecord record;
        record.tileIndex = animation.tileIndex;
        record.frameCount = animation.frameCount;
        expectedAnimations.append(record);
    }
    if(!reopenedMpr.loadBytes(result.mprBytes,QStringLiteral("portable.mpr"),&reopenReport)
       ||reopenReport.hasErrors()
       ||reopenedMpr.mapInfo().textureCount!=quint32(finalization.textureCount)
       ||reopenedMpr.mapInfo().tileCount!=quint32(finalization.tileTypes.size())
       ||reopenedMpr.mapInfo().materials!=source.mapInfo().materials
       ||reopenedMpr.mapInfo().animatedTiles!=expectedAnimations
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

STerrainPortableAtlasPackage CTerrainVirtualAtlas::buildPortablePackage(
    const CMprDocument& source, const CTerrainEditModel& editedTerrain,
    const STerrainAtlasFinalization& finalization,
    ETerrainTileLayer layer, CValidationReport* report)
{
    return buildPortablePackage(source, editedTerrain,
        source.mapInfo().materials, finalization, layer, report);
}

STerrainPortableAtlasPackage CTerrainVirtualAtlas::buildPortablePackage(
    const CMprDocument& source, const CTerrainEditModel& editedTerrain,
    const QVector<SMprMaterialRecord>& materials,
    const STerrainAtlasFinalization& finalization,
    ETerrainTileLayer layer, CValidationReport* report)
{
    STerrainPortableAtlasPackage result;
    CValidationReport stageReport;
    CMprEditTransaction transaction(QStringLiteral(
        "Stage every live terrain edit before atlas finalization"));
    if(!editedTerrain.appendSectorEdits(&transaction, &stageReport)
       || !source.appendMapParametersEdit(
              editedTerrain.maxZ(), materials,
              source.mapInfo().animatedTiles, &transaction, &stageReport)
       || stageReport.hasErrors())
    {
        if(report)
            for(const SValidationFinding& finding : stageReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        result.error = QStringLiteral(
            "The live sculpt, material, and terrain edits could not be staged before atlas finalization.");
        return result;
    }
    if(transaction.isEmpty())
        return buildPortablePackage(source, finalization, layer, report);

    QByteArray stagedBytes;
    if(!source.applyTransaction(transaction, &stagedBytes, &stageReport))
    {
        if(report)
            for(const SValidationFinding& finding : stageReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        result.error = QStringLiteral(
            "The complete live terrain edit failed its provenance-checked staging transaction.");
        return result;
    }
    CMprDocument stagedSource;
    if(!stagedSource.loadBytes(
           stagedBytes,
           QStringLiteral("edited-terrain-before-atlas-finalization.mpr"),
           &stageReport)
       || stageReport.hasErrors())
    {
        if(report)
            for(const SValidationFinding& finding : stageReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        result.error = QStringLiteral(
            "The complete live terrain edit failed fresh reopen before atlas finalization.");
        return result;
    }
    if(report)
        for(const SValidationFinding& finding : stageReport.findings())
            report->add(finding.code, finding.severity,
                        finding.message, finding.location);
    return buildPortablePackage(stagedSource, finalization, layer, report);
}

STerrainPortableAtlasPackage CTerrainVirtualAtlas::buildWorkingPreviewPackage(
    const CMprDocument& source, const QVector<QImage>& activeTiles,
    const QVector<qint32>& activeTileTypes,
    const QVector<QByteArray>& authoredPagePayloads,
    CValidationReport* report)
{
    return buildWorkingPreviewPackage(
        source, activeTiles, activeTileTypes, authoredPagePayloads,
        source.mapInfo().materials, source.mapInfo().animatedTiles, report);
}

STerrainPortableAtlasPackage CTerrainVirtualAtlas::buildWorkingPreviewPackage(
    const CMprDocument& source, const QVector<QImage>& activeTiles,
    const QVector<qint32>& activeTileTypes,
    const QVector<QByteArray>& authoredPagePayloads,
    const QVector<SMprMaterialRecord>& materials,
    const QVector<SMprAnimatedTileRecord>& animations,
    CValidationReport* report)
{
    STerrainPortableAtlasPackage result;
    const SMprMapInfo& map = source.mapInfo();
    if(!map.valid || activeTiles.isEmpty() || activeTiles.size() % 64 != 0
       || activeTiles.size() > kMprPackedTerrainAtlasSlotLimit
       || authoredPagePayloads.size() != int(map.textureCount)
       || activeTiles.size() < int(map.tileCount)
       || activeTileTypes.size() < int(map.tileCount)
       || activeTileTypes.size() > activeTiles.size()
       || materials.size() > int(std::numeric_limits<quint16>::max()))
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

    // Reuse the canonical parameter validator for every live material value.
    // Its source animation table is known to fit the persisted baseline; live
    // working-slot animations are validated below against activeTileTypes.
    CMprEditTransaction parameterValidation(QStringLiteral(
        "Validate live working-atlas materials"));
    if(!source.appendMapParametersEdit(
           map.maxZ, materials, map.animatedTiles,
           &parameterValidation, report))
    {
        result.error = QStringLiteral(
            "The live material table is invalid for the working atlas.");
        return result;
    }
    for(int index = 0; index < animations.size(); ++index)
    {
        const SMprAnimatedTileRecord& animation = animations.at(index);
        const quint64 physicalCount = qMax<quint64>(1, animation.frameCount);
        if(quint64(animation.tileIndex) + physicalCount
             > quint64(activeTileTypes.size()))
        {
            result.error = QStringLiteral(
                "Animated terrain range %1 points outside the live working atlas.")
                .arg(index);
            if(report) report->add("terrain.working_animation_range",
                EValidationSeverity::Error, result.error,
                QStringLiteral("start %1, frames %2")
                    .arg(animation.tileIndex).arg(animation.frameCount));
            return result;
        }
        if(physicalCount > quint64(kTerrainAtlasSlotsPerPage)
           || int(animation.tileIndex % kTerrainAtlasSlotsPerPage)
                + int(physicalCount) > kTerrainAtlasSlotsPerPage)
        {
            result.error = QStringLiteral(
                "Animated terrain range %1 crosses an MMP texture page. Apply the animation in the Material Editor to repack it before Test Current.")
                .arg(index);
            if(report) report->add("terrain.working_animation_page",
                EValidationSeverity::Error, result.error,
                QStringLiteral("start %1, frames %2")
                    .arg(animation.tileIndex).arg(animation.frameCount));
            return result;
        }
    }

    const QByteArray originalMp = source.memberPayload(map.memberName);
    if(originalMp.size() < 38 || map.parsedSize > quint64(originalMp.size()))
    {
        result.error = QStringLiteral("The staged MP parameter table is truncated.");
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
    QByteArray replacementMp = originalMp.left(38);
    qToLittleEndian<quint16>(quint16(materials.size()),
        reinterpret_cast<uchar*>(replacementMp.data() + 32));
    qToLittleEndian<quint32>(quint32(animations.size()),
        reinterpret_cast<uchar*>(replacementMp.data() + 34));
    {
        QDataStream stream(&replacementMp, QIODevice::WriteOnly | QIODevice::Append);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for(const SMprMaterialRecord& material : materials)
            stream << qint32(material.type)
                   << material.red << material.green << material.blue
                   << material.opacity << material.selfIllumination
                   << material.waveMultiplier << material.warpSpeed
                   << material.reserved1 << material.reserved2
                   << material.reserved3;
        for(qint32 type : tileTypes)
            stream << type;
        for(const SMprAnimatedTileRecord& animation : animations)
            stream << animation.tileIndex << animation.frameCount;
    }
    replacementMp.append(originalMp.mid(int(map.parsedSize)));
    qToLittleEndian<quint32>(quint32(textureCount),
        reinterpret_cast<uchar*>(replacementMp.data() + 16));
    qToLittleEndian<quint32>(quint32(tileSize.width() * 8),
        reinterpret_cast<uchar*>(replacementMp.data() + 20));
    qToLittleEndian<quint32>(quint32(tileTypes.size()),
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
    // The working atlas deliberately reuses the physically present but
    // logically unowned tail of the last authored MMP page. Preserve complete
    // authored pages byte-for-byte, but rebuild that one partial page when MP
    // metadata has grown into its tail. Otherwise the staged MPR references
    // the new slot while the RES still contains the old transparent padding.
    if(activeTileTypes.size() > int(map.tileCount)
       && int(map.tileCount) % 64 != 0)
    {
        const int partialPage = int(map.tileCount) / 64;
        if(partialPage < authoredPagePayloads.size())
        {
            QString diagnostic;
            const QImage pixels = composePage(
                activeTiles, partialPage * 64, tileSize.width());
            const QByteArray payload = ei::encodeMmpDxt1(
                pixels, true, &diagnostic);
            if(payload.isEmpty())
            {
                result.error = QStringLiteral(
                    "Could not encode reused authored-tail page %1: %2")
                    .arg(partialPage).arg(diagnostic);
                return result;
            }
            QSet<int> reusedLocalSlots;
            const int pageFirstSlot = partialPage * 64;
            for(int slot = int(map.tileCount);
                slot < activeTileTypes.size()
                    && slot < pageFirstSlot + 64; ++slot)
                reusedLocalSlots.insert(slot - pageFirstSlot);
            const QByteArray patched = ei::patchMmpDxt1AtlasTiles(
                authoredPagePayloads.at(partialPage), payload,
                reusedLocalSlots, &diagnostic);
            if(patched.isEmpty())
            {
                result.error = QStringLiteral(
                    "Could not patch reused authored-tail page %1: %2")
                    .arg(partialPage).arg(diagnostic);
                return result;
            }
            pagePayloads[partialPage] = patched;
        }
    }
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
       || reopenedMpr.mapInfo().tileCount != quint32(tileTypes.size())
       || reopenedMpr.mapInfo().materials != materials
       || reopenedMpr.mapInfo().animatedTiles != animations
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
      m_authoredSlotCount(sourceTileTypes.isEmpty()
          ? authoredTiles.size()
          : qMin(sourceTileTypes.size(), authoredTiles.size())),
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
    for(int slot = 0; slot < m_authoredSlotCount; ++slot)
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

QSharedPointer<CTerrainWorkingAtlas>
CTerrainWorkingAtlas::createStagingCopy() const
{
    // QSharedPointer itself would share the mutable resolver.  Resolve uses
    // that resolver to append generated and imported-library entries, so a
    // shallow copy would still leak failed/no-op planning back into the live
    // working atlas.  All other members are value containers.
    QSharedPointer<CTerrainWorkingAtlas> staged(
        new CTerrainWorkingAtlas(*this));
    if(m_resolver)
        staged->m_resolver.reset(new CTerrainVirtualAtlas(
            *m_resolver, staged->m_authoredCatalog));
    return staged;
}

bool CTerrainWorkingAtlas::canCommitStaging(
    const CTerrainWorkingAtlas& staged) const
{
    if(this == &staged)
        return true;
    // A staging copy is valid only for the atlas that created it.  Never let
    // an unrelated atlas replace the immutable authored baseline.
    if(m_capacityLimit != staged.m_capacityLimit
       || m_authoredSlotCount != staged.m_authoredSlotCount
       || m_authoredImages != staged.m_authoredImages
       || m_workingImages.size() != staged.m_workingImages.size()
       || m_workingTileTypes.size() != staged.m_workingTileTypes.size()
       || bool(m_resolver) != bool(staged.m_resolver))
        return false;

    return true;
}

bool CTerrainWorkingAtlas::commitStaging(
    const CTerrainWorkingAtlas& staged)
{
    if(!canCommitStaging(staged))
        return false;

    m_workingCatalog = staged.m_workingCatalog;
    m_workingImages = staged.m_workingImages;
    m_workingTileTypes = staged.m_workingTileTypes;
    m_virtualToWorking = staged.m_virtualToWorking;
    m_workingToVirtual = staged.m_workingToVirtual;
    m_animationCopySources = staged.m_animationCopySources;
    m_foreignPhysicalTiles = staged.m_foreignPhysicalTiles;
    m_changedSlots = staged.m_changedSlots;
    if(staged.m_resolver)
        m_resolver.reset(new CTerrainVirtualAtlas(
            *staged.m_resolver, m_authoredCatalog));
    else
        m_resolver.reset();
    return true;
}

bool CTerrainWorkingAtlas::normalizeAnimationRanges(
    const QVector<SMprAnimatedTileRecord>& animations,
    QVector<SMprAnimatedTileRecord>* normalizedAnimations,
    QMap<int, int>* slotRemap, CValidationReport* report)
{
    if(!normalizedAnimations || !slotRemap || !isValid())
        return false;
    normalizedAnimations->clear();
    slotRemap->clear();
    const auto completeUniform = [](const STerrainTransitionSignature& value) {
        return value.northWest > 0
            && value.northWest == value.northEast
            && value.northWest == value.southEast
            && value.northWest == value.southWest;
    };
    QVector<SMprAnimatedTileRecord> normalized = animations;
    QVector<QVector<int>> selectedFrames(animations.size());
    QSet<int> reservedCopies;

    // Resolve the complete plan before mutation. Working slot order is the
    // authoring/provenance order: blend identities are skipped, while exact
    // uniform identities of another runtime type can never become phases.
    for(int animationIndex = 0; animationIndex < animations.size();
        ++animationIndex)
    {
        const SMprAnimatedTileRecord& record = animations.at(animationIndex);
        const int physicalCount = qMax(1, int(record.frameCount));
        const int start = int(record.tileIndex);
        if(start < 0 || start >= activeTileCount())
        {
            if(report) report->add("terrain.working_animation_range",
                EValidationSeverity::Error,
                QStringLiteral("Animation record %1 points outside the live working atlas.")
                    .arg(animationIndex));
            return false;
        }
        if(record.frameCount == 0)
            continue;
        if(physicalCount > kTerrainAtlasSlotsPerPage)
        {
            if(report) report->add("terrain.working_animation_page",
                EValidationSeverity::Error,
                QStringLiteral("Animation record %1 needs %2 frames, but the game can advance only within one %3-slot MMP texture page. No slots were moved.")
                    .arg(animationIndex).arg(physicalCount)
                    .arg(kTerrainAtlasSlotsPerPage));
            return false;
        }

        STerrainWorkingTileIdentity startIdentity;
        if(!tileIdentityForSlot(start, &startIdentity)
           || !completeUniform(startIdentity.canonicalSignature)
           || startIdentity.runtimeTerrainType < 0
           || startIdentity.runtimeTerrainType >= 16)
        {
            if(report) report->add("terrain.working_animation_identity",
                EValidationSeverity::Error,
                QStringLiteral("Animation record %1 does not start on a complete typed uniform terrain identity.")
                    .arg(animationIndex));
            return false;
        }

        QVector<int>& frameSlots = selectedFrames[animationIndex];
        frameSlots.reserve(physicalCount);
        for(int position = start;
            position < activeTileCount() && frameSlots.size() < physicalCount;
            ++position)
        {
            STerrainWorkingTileIdentity candidate;
            if(tileIdentityForSlot(position, &candidate)
               && candidate.canonicalSignature
                    == startIdentity.canonicalSignature
               && candidate.runtimeTerrainType
                    == startIdentity.runtimeTerrainType)
                frameSlots.append(position);
        }
        if(frameSlots.size() != physicalCount)
        {
            if(report) report->add("terrain.working_animation_frames",
                EValidationSeverity::Error,
                QStringLiteral("Animation record %1 needs %2 ordered uniform frames of runtime type %3, but only %4 remain in working-atlas provenance order. No slots were moved.")
                    .arg(animationIndex).arg(physicalCount)
                    .arg(startIdentity.runtimeTerrainType)
                    .arg(frameSlots.size()));
            return false;
        }

        bool alreadyContiguous =
            start % kTerrainAtlasSlotsPerPage + physicalCount
                <= kTerrainAtlasSlotsPerPage;
        for(int phase = 0; phase < frameSlots.size(); ++phase)
            alreadyContiguous = alreadyContiguous
                && frameSlots.at(phase) == start + phase;
        int activationStart = start;
        if(!alreadyContiguous)
        {
            int copyStart = -1;
            for(int candidate = activeTileCount();
                candidate + physicalCount <= m_capacityLimit; ++candidate)
            {
                if(candidate % kTerrainAtlasSlotsPerPage + physicalCount
                     > kTerrainAtlasSlotsPerPage)
                    continue;
                bool free = true;
                for(int phase = 0; phase < physicalCount; ++phase)
                    free = free && !m_workingToVirtual.contains(candidate + phase)
                        && !reservedCopies.contains(candidate + phase);
                if(free)
                {
                    copyStart = candidate;
                    break;
                }
            }
            if(copyStart < 0
               || copyStart > int(std::numeric_limits<quint16>::max()))
            {
                if(report) report->add("terrain.working_animation_capacity",
                    EValidationSeverity::Error,
                    QStringLiteral("Animation record %1 needs %2 contiguous extension slots, but the working atlas has no safe free range. No slots were moved.")
                        .arg(animationIndex).arg(physicalCount));
                return false;
            }
            activationStart = copyStart;
            normalized[animationIndex].tileIndex = quint16(copyStart);
            for(int phase = 0; phase < physicalCount; ++phase)
                reservedCopies.insert(copyStart + phase);
        }

        // Every cell authored with one of the selected physical frames must
        // reference the exact record start. Runtime supplies its independent
        // phase; a frame-member reference is otherwise static.
        for(int phase = 0; phase < physicalCount; ++phase)
        {
            const int sourceSlot = frameSlots.at(phase);
            if(slotRemap->contains(sourceSlot)
               && slotRemap->value(sourceSlot) != activationStart)
            {
                if(report) report->add("terrain.working_animation_shared_frame",
                    EValidationSeverity::Error,
                    QStringLiteral("Two animation records share a frame slot but require different physical ranges. No slots were moved."));
                return false;
            }
            slotRemap->insert(sourceSlot, activationStart);
        }
    }

    for(int animationIndex = 0; animationIndex < animations.size();
        ++animationIndex)
    {
        const int copyStart = int(normalized.at(animationIndex).tileIndex);
        if(copyStart == int(animations.at(animationIndex).tileIndex))
            continue;
        const QVector<int>& frames = selectedFrames.at(animationIndex);
        for(int phase = 0; phase < frames.size(); ++phase)
        {
            const int sourceSlot = frames.at(phase);
            const int destination = copyStart + phase;
            m_workingImages[destination] = m_workingImages.at(sourceSlot);
            m_workingTileTypes[destination] = m_workingTileTypes.at(sourceSlot);
            const QPair<int, int> key(2, destination);
            m_workingToVirtual.insert(destination, key);
            m_virtualToWorking.insert(key, destination);
            m_animationCopySources.insert(destination, sourceSlot);
            m_changedSlots.insert(destination);

            STerrainWorkingTileIdentity identity;
            if(!tileIdentityForSlot(sourceSlot, &identity))
                return false; // resolved above; defensive only
            const STerrainTransitionSignature base =
                m_workingCatalog.paintGridMirrored()
                    ? CTerrainTransitionCatalog::mirroredGridY(
                          identity.canonicalSignature)
                    : identity.canonicalSignature;
            for(int rotation = 0; rotation < 4; ++rotation)
            {
                SMprPackedTile packed;
                packed.tileIndex = quint8(destination % 64);
                packed.atlasIndex = quint8(destination / 64);
                packed.rotation = quint8(rotation);
                STerrainTransitionVariant variant;
                variant.signature = CTerrainTransitionCatalog::rotated(
                    base, -rotation);
                variant.packedTile = packed.encode();
                variant.paintSelectable = false;
                m_workingCatalog.addVariant(variant);
            }
        }
    }
    *normalizedAnimations = normalized;
    return true;
}

QByteArray CTerrainWorkingAtlas::stateFingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto addInt = [&hash](qint32 value) {
        char bytes[sizeof(value)];
        qToLittleEndian<qint32>(value, reinterpret_cast<uchar*>(bytes));
        hash.addData(bytes, sizeof(bytes));
    };
    const auto addBytes = [&hash, &addInt](const QByteArray& value) {
        addInt(value.size());
        hash.addData(value);
    };
    const auto addSignature = [&addInt](
        const STerrainTransitionSignature& signature) {
        addInt(signature.northWest);
        addInt(signature.northEast);
        addInt(signature.southEast);
        addInt(signature.southWest);
    };

    hash.addData("terrain-working-atlas-state-v1");
    addInt(m_capacityLimit);
    addInt(m_authoredSlotCount);
    addInt(m_workingCatalog.size());
    const QVector<STerrainTransitionSignature> catalogSignatures =
        m_workingCatalog.gridSignatures();
    addInt(catalogSignatures.size());
    for(const STerrainTransitionSignature& signature : catalogSignatures)
        addSignature(signature);
    addInt(m_workingImages.size());
    for(const QImage& image : m_workingImages)
        addBytes(imageHash(image));
    addInt(m_workingTileTypes.size());
    for(qint32 type : m_workingTileTypes)
        addInt(type);
    addInt(m_virtualToWorking.size());
    for(auto mapping = m_virtualToWorking.constBegin();
        mapping != m_virtualToWorking.constEnd(); ++mapping)
    {
        addInt(mapping.key().first);
        addInt(mapping.key().second);
        addInt(mapping.value());
    }
    addInt(m_workingToVirtual.size());
    for(auto mapping = m_workingToVirtual.constBegin();
        mapping != m_workingToVirtual.constEnd(); ++mapping)
    {
        addInt(mapping.key());
        addInt(mapping.value().first);
        addInt(mapping.value().second);
    }
    addInt(m_animationCopySources.size());
    for(auto copy = m_animationCopySources.constBegin();
        copy != m_animationCopySources.constEnd(); ++copy)
    {
        addInt(copy.key());
        addInt(copy.value());
    }
    addInt(m_changedSlots.size());
    for(int slot : m_changedSlots)
        addInt(slot);
    const QVector<STerrainFinalizedTile> generated = m_resolver
        ? m_resolver->generatedTiles() : QVector<STerrainFinalizedTile>();
    addInt(generated.size());
    for(const STerrainFinalizedTile& tile : generated)
    {
        addSignature(tile.canonicalSignature);
        addBytes(tile.pixelSha256);
        addInt(tile.authored ? 1 : 0);
        addInt(tile.sourceSlot);
        addBytes(tile.sourceNamespace.toUtf8());
        addBytes(tile.blendProvenance.toUtf8());
        addInt(tile.stockDerivedBlend ? 1 : 0);
        addInt(tile.canonicalGeneratedBlend ? 1 : 0);
        addInt(tile.routeOwnerMaterial);
        addInt(tile.runtimeTerrainType);
        addInt(tile.sourceSlotAliases.size());
        for(int alias : tile.sourceSlotAliases)
            addInt(alias);
    }
    return hash.result();
}

void CTerrainWorkingAtlas::acknowledgeChangedSlots(const QSet<int>& changed)
{
    for(int slot : changed)
        m_changedSlots.remove(slot);
}

bool CTerrainWorkingAtlas::addAuthoredLibraryTransition(
    const STerrainTransitionSignature& signature, const QImage& pixels,
    const QString& sourceLabel, CValidationReport* report,
    const QString& sourceNamespace, int sourceSlot)
{
    return m_resolver && m_resolver->addAuthoredLibraryTransition(
        signature, pixels, sourceLabel, report, sourceNamespace, sourceSlot);
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
    CValidationReport* report, qint32 preferredPaintMaterial,
    qint32 preferredRuntimeType, bool requirePreferredRuntimeType)
{
    if(!m_resolver || !workingUsage)
        return false;
    const QVector<STerrainVirtualTile> variants =
        m_resolver->authoredLibraryVariants(
            signature, preferredPaintMaterial, preferredRuntimeType, report,
            requirePreferredRuntimeType);
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
    const int authoredPages = (m_authoredImages.size() + 63) / 64;
    return qMax(qMax(1, authoredPages), (activeTileCount() + 63) / 64);
}

int CTerrainWorkingAtlas::activeTileCount() const
{
    const int highest = m_workingToVirtual.isEmpty()
        ? -1 : m_workingToVirtual.lastKey();
    return qMax(m_authoredSlotCount, highest + 1);
}

QVector<QImage> CTerrainWorkingAtlas::activeTileImages() const
{
    return m_workingImages.mid(0, activeTextureCount() * 64);
}

QVector<qint32> CTerrainWorkingAtlas::activeTileTypes() const
{
    QVector<qint32> result = m_workingTileTypes.mid(0, activeTileCount());
    qint32 paddingType = 0;
    for(qint32 type : result)
        if(type >= 0 && type < 16)
        {
            paddingType = type;
            break;
        }
    // A partial authored page can leave physical positions between the old
    // logical end and the first extension slot. They remain unreferenced, but
    // MP metadata is contiguous through the highest live extension. Give only
    // those unmapped holes a deterministic valid filler; mapped slots retain
    // -1 so the package gate still fails closed on a real untyped tile.
    for(int slot = 0; slot < result.size(); ++slot)
        if((result.at(slot) < 0 || result.at(slot) >= 16)
           && !m_workingToVirtual.contains(slot))
            result[slot] = paddingType;
    return result;
}

QVector<QByteArray> CTerrainWorkingAtlas::manualAtlasPhysicalIdentities() const
{
    QVector<QByteArray> result(activeTextureCount() * 64);
    for(int slot = 0; slot < result.size(); ++slot)
    {
        STerrainWorkingTileIdentity identity;
        if(!tileIdentityForSlot(slot, &identity)
           || identity.physicalPixelSha256.isEmpty()
           || identity.runtimeTerrainType < 0
           || identity.runtimeTerrainType >= 16)
            continue;
        QByteArray selectionIdentity = identity.physicalPixelSha256;
        char typeBytes[sizeof(identity.runtimeTerrainType)];
        qToLittleEndian<qint32>(identity.runtimeTerrainType,
                                reinterpret_cast<uchar*>(typeBytes));
        selectionIdentity.append(typeBytes, sizeof(typeBytes));
        result[slot] = selectionIdentity;
    }
    return result;
}

qint32 CTerrainWorkingAtlas::runtimeTerrainTypeForSlot(int slot) const
{
    return slot >= 0 && slot < m_workingTileTypes.size()
        ? m_workingTileTypes.at(slot) : -1;
}

ETerrainWorkingTileOrigin CTerrainWorkingAtlas::tileOriginForSlot(
    int slot) const
{
    if(m_foreignPhysicalTiles.contains(slot))return ETerrainWorkingTileOrigin::ImportedAuthored;
    const auto mapped = m_workingToVirtual.constFind(slot);
    if(mapped == m_workingToVirtual.constEnd())
        return ETerrainWorkingTileOrigin::Unknown;
    if(mapped.value().first == 2)
        return tileOriginForSlot(m_animationCopySources.value(slot, -1));
    if(mapped.value().first == 0)
        return ETerrainWorkingTileOrigin::AuthoredMap;
    const int sourceIndex = mapped.value().second;
    if(!m_resolver || sourceIndex < 0
       || sourceIndex >= m_resolver->generatedTiles().size())
        return ETerrainWorkingTileOrigin::Unknown;
    return m_resolver->generatedTiles().at(sourceIndex).authored
        ? ETerrainWorkingTileOrigin::ImportedAuthored
        : ETerrainWorkingTileOrigin::CanonicalGenerated;
}

bool CTerrainWorkingAtlas::tileIdentityForSlot(
    int slot, STerrainWorkingTileIdentity* identity) const
{
    if(!identity)
        return false;
    *identity = STerrainWorkingTileIdentity();
    if(slot < 0 || slot >= m_workingImages.size()
       || m_workingImages.at(slot).isNull())
        return false;
    if(m_foreignPhysicalTiles.contains(slot)) {
        *identity=m_foreignPhysicalTiles.value(slot);return true;
    }
    const auto mapped = m_workingToVirtual.constFind(slot);
    if(mapped == m_workingToVirtual.constEnd())
        return false;

    if(mapped.value().first == 2)
    {
        const int sourceSlot = m_animationCopySources.value(slot, -1);
        STerrainWorkingTileIdentity source;
        if(sourceSlot < 0 || sourceSlot == slot
           || !tileIdentityForSlot(sourceSlot, &source))
            return false;
        *identity = source;
        identity->workingSlot = slot;
        identity->physicalPixelSha256 = imageHash(m_workingImages.at(slot));
        identity->runtimeTerrainType = runtimeTerrainTypeForSlot(slot);
        return true;
    }

    identity->workingSlot = slot;
    identity->physicalPixelSha256 = imageHash(m_workingImages.at(slot));
    identity->runtimeTerrainType = runtimeTerrainTypeForSlot(slot);
    const ETerrainWorkingTileOrigin origin = tileOriginForSlot(slot);
    const bool extensionIdentity = origin
        != ETerrainWorkingTileOrigin::AuthoredMap;
    const int sourceIndex = mapped.value().second;
    if(!extensionIdentity)
    {
        if(sourceIndex < 0 || sourceIndex >= m_authoredImages.size())
            return false;
        identity->origin = ETerrainWorkingTileOrigin::AuthoredMap;
        identity->sourceSlot = sourceIndex;
        identity->sourceSlotAliases = {sourceIndex};
        SMprPackedTile packed;
        packed.tileIndex = quint8(sourceIndex % 64);
        packed.atlasIndex = quint8(sourceIndex / 64);
        bool found = false;
        identity->canonicalSignature = m_authoredCatalog.gridSignatureForPacked(
            packed.encode(), &found);
        if(!found)
            identity->canonicalSignature = STerrainTransitionSignature();
        bool variantFound = false;
        const STerrainTransitionVariant variant =
            m_authoredCatalog.variantForPacked(
                packed.encode(), &variantFound);
        if(variantFound)
            identity->routeOwnerMaterial = variant.routeOwnerMaterial;
        return true;
    }
    if(!m_resolver || sourceIndex < 0
       || sourceIndex >= m_resolver->generatedTiles().size())
        return false;
    const STerrainFinalizedTile& source =
        m_resolver->generatedTiles().at(sourceIndex);
    identity->origin = origin;
    identity->sourceSlot = source.sourceSlot;
    identity->sourceSlotAliases = source.sourceSlotAliases;
    if(identity->sourceSlotAliases.isEmpty() && source.sourceSlot >= 0)
        identity->sourceSlotAliases.append(source.sourceSlot);
    identity->sourceNamespace = source.sourceNamespace;
    identity->canonicalSignature = source.canonicalSignature;
    identity->blendProvenance = source.blendProvenance;
    identity->stockDerivedBlend = source.stockDerivedBlend;
    identity->canonicalGeneratedBlend = source.canonicalGeneratedBlend;
    identity->routeOwnerMaterial = source.routeOwnerMaterial;
    return true;
}

bool CTerrainWorkingAtlas::restoreFinalizedProvenance(
    STerrainAtlasFinalization* finalization,
    STerrainWorkingFinalizationOrigins* origins,
    const QString& sourceNamespace,
    const QByteArray& materialFieldSha256,
    const QVector<quint16>& pinnedPackedTiles,
    CValidationReport* report) const
{
    if(!finalization || !origins || !finalization->success)
        return false;
    *origins = STerrainWorkingFinalizationOrigins();
    QMap<int, QVector<STerrainWorkingTileIdentity>> identitiesByOutput;
    for(auto remap = finalization->authoredSlotRemap.constBegin();
        remap != finalization->authoredSlotRemap.constEnd(); ++remap)
    {
        const int outputSlot = remap.value();
        STerrainWorkingTileIdentity identity;
        if(outputSlot < 0 || outputSlot >= finalization->tiles.size()
           || !tileIdentityForSlot(remap.key(), &identity))
        {
            if(report) report->add(
                QStringLiteral("terrain.finalized_working_provenance"),
                EValidationSeverity::Error,
                QStringLiteral("A retained working-atlas slot has no stable physical provenance."),
                QStringLiteral("working slot %1 -> finalized slot %2")
                    .arg(remap.key()).arg(outputSlot));
            return false;
        }
        identitiesByOutput[outputSlot].append(identity);
    }

    for(int outputSlot = 0; outputSlot < finalization->tiles.size(); ++outputSlot)
    {
        STerrainFinalizedTile& tile = finalization->tiles[outputSlot];
        const QVector<STerrainWorkingTileIdentity> identities =
            identitiesByOutput.value(outputSlot);
        QVector<STerrainWorkingTileIdentity> retained;
        QVector<STerrainWorkingTileIdentity> imported;
        QVector<STerrainWorkingTileIdentity> generated;
        for(const STerrainWorkingTileIdentity& identity : identities)
        {
            if(identity.origin == ETerrainWorkingTileOrigin::AuthoredMap)
                retained.append(identity);
            else if(identity.origin == ETerrainWorkingTileOrigin::ImportedAuthored)
                imported.append(identity);
            else if(identity.origin == ETerrainWorkingTileOrigin::CanonicalGenerated)
                generated.append(identity);
        }
        if(!identities.isEmpty())
        {
            const STerrainTransitionSignature restoredSignature =
                identities.front().canonicalSignature;
            const auto completeSignature = [](const STerrainTransitionSignature& value)
            {
                return value.northWest > 0 && value.northEast > 0
                    && value.southEast > 0 && value.southWest > 0;
            };
            bool identityAgreement = completeSignature(restoredSignature);
            for(const STerrainWorkingTileIdentity& identity : identities)
                identityAgreement = identityAgreement
                    && identity.canonicalSignature == restoredSignature;
            if(!identityAgreement)
            {
                if(report) report->add(
                    QStringLiteral("terrain.finalized_working_signature"),
                    EValidationSeverity::Error,
                    QStringLiteral("Compacted working-atlas aliases do not share one complete semantic signature."),
                    QStringLiteral("finalized slot %1").arg(outputSlot));
                return false;
            }
            tile.canonicalSignature = restoredSignature;
        }
        if(!retained.isEmpty())
        {
            tile.authored = true;
            tile.sourceNamespace = sourceNamespace;
            tile.blendProvenance.clear();
            tile.stockDerivedBlend = false;
            tile.canonicalGeneratedBlend = false;
            tile.routeOwnerMaterial = 0;
            tile.sourceSlotAliases.clear();
            for(const STerrainWorkingTileIdentity& identity : retained)
                if(identity.sourceSlot >= 0
                   && !tile.sourceSlotAliases.contains(identity.sourceSlot))
                    tile.sourceSlotAliases.append(identity.sourceSlot);
            std::sort(tile.sourceSlotAliases.begin(), tile.sourceSlotAliases.end());
            tile.sourceSlot = tile.sourceSlotAliases.isEmpty()
                ? -1 : tile.sourceSlotAliases.front();
            QSet<qint32> retainedOwners;
            bool everyRetainedIdentityHasOwner = true;
            for(const STerrainWorkingTileIdentity& identity : retained)
                if(identity.routeOwnerMaterial > 0)
                    retainedOwners.insert(identity.routeOwnerMaterial);
                else
                    everyRetainedIdentityHasOwner = false;
            if(retainedOwners.size() == 1
               && everyRetainedIdentityHasOwner)
                tile.routeOwnerMaterial = *retainedOwners.constBegin();
            origins->retainedMapOutputs.insert(outputSlot);
            continue;
        }
        if(!imported.isEmpty())
        {
            std::sort(imported.begin(), imported.end(),
                [](const STerrainWorkingTileIdentity& left,
                   const STerrainWorkingTileIdentity& right)
                {
                    const int namespaceOrder = QString::compare(
                        left.sourceNamespace, right.sourceNamespace,
                        Qt::CaseInsensitive);
                    return namespaceOrder != 0
                        ? namespaceOrder < 0 : left.sourceSlot < right.sourceSlot;
                });
            const STerrainWorkingTileIdentity& selected = imported.front();
            tile.authored = true;
            tile.sourceNamespace = selected.sourceNamespace;
            tile.sourceSlot = selected.sourceSlot;
            tile.sourceSlotAliases.clear();
            for(const STerrainWorkingTileIdentity& identity : imported)
                if(identity.sourceNamespace.compare(selected.sourceNamespace,
                       Qt::CaseInsensitive) == 0)
                    for(int alias : identity.sourceSlotAliases)
                        if(alias >= 0 && !tile.sourceSlotAliases.contains(alias))
                            tile.sourceSlotAliases.append(alias);
            if(tile.sourceSlot >= 0
               && !tile.sourceSlotAliases.contains(tile.sourceSlot))
                tile.sourceSlotAliases.append(tile.sourceSlot);
            std::sort(tile.sourceSlotAliases.begin(), tile.sourceSlotAliases.end());
            tile.blendProvenance = selected.blendProvenance;
            tile.stockDerivedBlend = selected.stockDerivedBlend;
            tile.canonicalGeneratedBlend = false;
            tile.routeOwnerMaterial = selected.routeOwnerMaterial;
            origins->importedAuthoredOutputs.insert(outputSlot);
            continue;
        }
        if(!generated.isEmpty() || !tile.authored)
        {
            if(!generated.isEmpty())
            {
                const STerrainWorkingTileIdentity& selected = generated.front();
                tile.sourceNamespace = selected.sourceNamespace;
                tile.blendProvenance = selected.blendProvenance;
                tile.stockDerivedBlend = selected.stockDerivedBlend;
                tile.canonicalGeneratedBlend = true;
                tile.routeOwnerMaterial = selected.routeOwnerMaterial;
            }
            tile.authored = false;
            tile.sourceSlot = -1;
            tile.sourceSlotAliases.clear();
            origins->canonicalGeneratedOutputs.insert(outputSlot);
            continue;
        }
        // A tile generated directly by the clean finalizer does not have a
        // working-slot remap, but already carries its complete provenance.
        if(tile.authored && !tile.sourceNamespace.isEmpty())
            origins->importedAuthoredOutputs.insert(outputSlot);
        else
            origins->retainedMapOutputs.insert(outputSlot);
    }

    // The clean finalizer initially sees every live working slot as authored.
    // Its first hash therefore describes that flattened cache identity. Once
    // the real source/generated provenance is restored, rebuild the identity
    // through the same byte contract used by finalize().
    refreshFinalizationStableIdentity(
        finalization, sourceNamespace, materialFieldSha256,
        pinnedPackedTiles);
    return true;
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
    // Logical authored slots are immutable for the editing session. The unused
    // tail of an authored physical page has no logical identity, so it is a
    // safe extension slot exactly when no layer references it and undo history
    // does not protect it. Do not skip that reusable tail merely because the
    // MMP page itself is physically complete.
    for(int slot = m_authoredSlotCount; slot < m_capacityLimit; ++slot)
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
            QString("The stroke needs another exact transition, but all %1 validated working slots are used or protected by undo history (authored logical:%2 reusable extension:%3). No identity was substituted.")
                .arg(m_capacityLimit).arg(m_authoredSlotCount)
                .arg(qMax(0, m_capacityLimit - m_authoredSlotCount)));
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
        const STerrainFinalizedTile& sourceTile =
            m_resolver->generatedTiles().at(sourceIndex);
        const STerrainTransitionSignature grid = sourceTile.canonicalSignature;
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
            variant.routeOwnerMaterial = sourceTile.authored
                ? 0 : sourceTile.routeOwnerMaterial;
            variant.canonicalGeneratedRoute = !sourceTile.authored;
            // Global-authored and canonical-generated tiles fill an exact
            // signature/runtime gap. They are not an extra variation pool
            // when this map already authored a selectable candidate for that
            // exact tuple. A different runtime class remains eligible for
            // predecessor-preserving repair.
            if(hasSelectableAuthoredCandidate(
                   variant.signature, sourceTile.runtimeTerrainType))
                continue;
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

bool CTerrainWorkingAtlas::hasSelectableAuthoredCandidate(
    const STerrainTransitionSignature& signature,
    qint32 runtimeTerrainType) const
{
    if(runtimeTerrainType < 0 || runtimeTerrainType >= 16)
        return false;
    for(const STerrainTransitionVariant& variant :
            m_authoredCatalog.paintVariants(signature))
    {
        const int slot = SMprPackedTile::decode(variant.packedTile).atlasSlot();
        if(slot < 0 || slot >= m_authoredSlotCount
           || slot >= m_authoredImages.size()
           || m_authoredImages.at(slot).isNull()
           || slot >= m_workingTileTypes.size())
            continue;
        if(m_workingTileTypes.at(slot) == runtimeTerrainType)
            return true;
    }
    return false;
}

quint16 CTerrainWorkingAtlas::resolve(
    const STerrainTransitionSignature& signature, int cellX, int cellY,
    QMap<int, quint64>* workingUsage, const QSet<int>& protectedSlots,
    bool* generated, CValidationReport* report,
    qint32 preferredPaintMaterial, qint32 preferredRuntimeType,
    bool requirePreferredRuntimeType)
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
        signature, cellX, cellY, report, preferredPaintMaterial,
        preferredRuntimeType, requirePreferredRuntimeType);
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

int CTerrainWorkingAtlas::importPhysicalTile(const QImage& pixels,qint32 runtimeType,
        const STerrainTransitionSignature* signature,const QString& sourceNamespace,
        int sourceSlot,CValidationReport* report)
{
    if(pixels.isNull() || runtimeType<0 || runtimeType>=16 || m_authoredImages.isEmpty()
       || pixels.size()!=m_authoredImages.first().size()) {
        if(report)report->add("terrain.world.physical",EValidationSeverity::Error,
            "The selected physical tile has incompatible pixels or runtime type.");return -1;
    }
    for(int slot=0;slot<activeTileCount();++slot)
        if(m_workingToVirtual.contains(slot) && m_workingTileTypes.value(slot)==runtimeType
           && m_workingImages.at(slot)==pixels) {
            SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);
            bool known=false;const auto existing=m_workingCatalog.gridSignatureForPacked(packed.encode(),&known);
            if(!signature || (known && existing==*signature))return slot;
        }
    const int slot=activeTileCount();
    if(slot>=m_capacityLimit) {
        if(report)report->add("terrain.world.capacity",EValidationSeverity::Error,"The neighboring atlas is full.");return -1;
    }
    m_workingImages[slot]=pixels;m_workingTileTypes[slot]=runtimeType;
    m_workingToVirtual.insert(slot,qMakePair(3,slot));
    STerrainWorkingTileIdentity identity;identity.origin=ETerrainWorkingTileOrigin::ImportedAuthored;
    identity.workingSlot=slot;identity.sourceNamespace=sourceNamespace;identity.sourceSlot=sourceSlot;
    identity.sourceSlotAliases={sourceSlot};identity.physicalPixelSha256=imageHash(pixels);identity.runtimeTerrainType=runtimeType;
    if(signature) {
        identity.canonicalSignature=*signature;
        const auto base=m_workingCatalog.paintGridMirrored()?CTerrainTransitionCatalog::mirroredGridY(*signature):*signature;
        for(int rotation=0;rotation<4;++rotation) {
            SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);packed.rotation=quint8(rotation);
            STerrainTransitionVariant variant;variant.packedTile=packed.encode();
            variant.signature=CTerrainTransitionCatalog::rotated(base,-rotation);m_workingCatalog.addVariant(variant,report);
        }
    }
    m_foreignPhysicalTiles.insert(slot,identity);m_changedSlots.insert(slot);return slot;
}

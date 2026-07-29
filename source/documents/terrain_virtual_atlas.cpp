#include "terrain_virtual_atlas.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
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

double terrainNoiseLattice(int x, int y, quint32 seed)
{
    const quint32 mixed = terrainNoiseHash(seed
        ^ quint32(x) * 0x9e3779b9u ^ quint32(y) * 0x85ebca6bu);
    return double(mixed & 0xffffu) / 65535.0;
}

double terrainValueNoise(double x, double y, quint32 seed)
{
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const double fx0 = x - x0;
    const double fy0 = y - y0;
    const double fx = fx0 * fx0 * (3.0 - 2.0 * fx0);
    const double fy = fy0 * fy0 * (3.0 - 2.0 * fy0);
    const double nw = terrainNoiseLattice(x0, y0, seed);
    const double ne = terrainNoiseLattice(x0 + 1, y0, seed);
    const double sw = terrainNoiseLattice(x0, y0 + 1, seed);
    const double se = terrainNoiseLattice(x0 + 1, y0 + 1, seed);
    return (nw + (ne - nw) * fx) * (1.0 - fy)
         + (sw + (se - sw) * fx) * fy;
}

quint32 transitionNoiseSeed(const QVector<qint32>& ids,
                            const QMap<qint32, QImage>& sources,
                            quint32 seed)
{
    quint32 result = seed ^ 0xa511e9b3u;
    for(qint32 id : ids)
    {
        const QByteArray hash = imageHash(sources.value(id));
        quint32 pixels = 0;
        for(int index = 0; index < 8 && index < hash.size(); ++index)
            pixels = pixels * 33u + quint8(hash.at(index));
        result = terrainNoiseHash(result ^ pixels);
    }
    return result;
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

double terrainTriangleWave(double value)
{
    const double fraction = value - std::floor(value);
    return 1.0 - 4.0 * std::fabs(fraction - 0.5);
}

void terrainBlendDisplacement(ETerrainBlendStyle style, double u, double v,
                              quint32 seed, int variation,
                              double* dx, double* dy, double* sharpness)
{
    const double tau = 6.28318530717958647692;
    const double phase = (double(seed & 0xffffu) / 65535.0
                         + double(variation) * 0.193) * tau;
    const double coarseX = terrainValueNoise(u * 2.25, v * 2.25, seed) - 0.5;
    const double coarseY = terrainValueNoise(u * 2.25, v * 2.25,
                                              seed ^ 0x68bc21ebu) - 0.5;
    const double detailX = terrainValueNoise(u * 6.5, v * 6.5,
                                              seed ^ 0x02e5be93u) - 0.5;
    const double detailY = terrainValueNoise(u * 6.5, v * 6.5,
                                              seed ^ 0x967a889bu) - 0.5;
    *sharpness = 9.0;
    switch(style)
    {
    case ETerrainBlendStyle::Grass:
    {
        const double strandsX = std::sin((v * (10.0 + variation) + u * 1.5) * tau + phase);
        const double strandsY = std::sin((u * (9.0 + variation) - v * 1.2) * tau + phase * 1.37);
        const double patches = terrainValueNoise(u * 4.0, v * 4.0,
                                                  seed ^ 0x4f1bbcdcu) - 0.5;
        *dx = coarseX * 0.30 + strandsX * 0.24 + patches * 0.38;
        *dy = coarseY * 0.30 + strandsY * 0.24 + patches * 0.32;
        *sharpness = 13.0 + std::fabs(patches) * 7.0;
        break;
    }
    case ETerrainBlendStyle::Sand:
    {
        const double grainX = terrainValueNoise(u * (18.0 + variation * 2.0),
            v * (18.0 + variation * 2.0), seed ^ 0xa0761d65u) - 0.5;
        const double grainY = terrainValueNoise(u * (20.0 + variation * 2.0),
            v * (20.0 + variation * 2.0), seed ^ 0xe7037ed1u) - 0.5;
        *dx = coarseX * 0.12 + grainX * 0.20;
        *dy = coarseY * 0.12 + grainY * 0.20;
        *sharpness = 7.5;
        break;
    }
    case ETerrainBlendStyle::Gravel:
    {
        const double chunksX = terrainValueNoise(u * (7.0 + variation),
            v * (6.0 + variation), seed ^ 0x8ebc6af1u) - 0.5;
        const double chunksY = terrainValueNoise(u * (6.0 + variation),
            v * (8.0 + variation), seed ^ 0x589965cdu) - 0.5;
        *dx = coarseX * 0.22 + chunksX * 0.64 + detailX * 0.24;
        *dy = coarseY * 0.22 + chunksY * 0.64 + detailY * 0.24;
        *sharpness = 14.0 + std::fabs(chunksX + chunksY) * 5.0;
        break;
    }
    case ETerrainBlendStyle::Snow:
    {
        const double clumpX = std::sin((u * (3.0 + variation * 0.5)
                                       + v * 2.0) * tau + phase);
        const double clumpY = std::sin((v * (3.5 + variation * 0.5)
                                       - u * 1.75) * tau + phase * 1.21);
        const double powder = terrainValueNoise(u * 5.0, v * 5.0,
                                                 seed ^ 0xd1b54a35u) - 0.5;
        *dx = coarseX * 0.28 + clumpX * 0.25 + powder * 0.25;
        *dy = coarseY * 0.28 + clumpY * 0.25 + powder * 0.25;
        *sharpness = 10.5 + (clumpX + 1.0) * 1.5;
        break;
    }
    case ETerrainBlendStyle::Stone:
    {
        const double crackX = terrainTriangleWave(u * (5.0 + variation)
                                                   + v * 1.7 + phase / tau);
        const double crackY = terrainTriangleWave(v * (5.0 + variation)
                                                   - u * 1.3 + phase / tau);
        *dx = coarseX * 0.22 + crackX * 0.38 + detailX * 0.20;
        *dy = coarseY * 0.22 + crackY * 0.38 + detailY * 0.20;
        *sharpness = 15.0 + std::fabs(crackX * crackY) * 6.0;
        break;
    }
    case ETerrainBlendStyle::Water:
    {
        const double ripple = std::sin((v * (4.0 + variation * 0.75)
                                       + u * 0.65) * tau + phase);
        const double crossRipple = std::sin((u * 2.25 - v * 3.0) * tau
                                            + phase * 1.43);
        *dx = coarseX * 0.12 + ripple * 0.18;
        *dy = coarseY * 0.12 + ripple * 0.42 + crossRipple * 0.12;
        *sharpness = 8.5;
        break;
    }
    case ETerrainBlendStyle::Ice:
    {
        const double fractureX = terrainTriangleWave(u * (7.0 + variation)
                                                      + v * 3.0 + phase / tau);
        const double fractureY = terrainTriangleWave(v * (8.0 + variation)
                                                      - u * 2.0 + phase / tau);
        *dx = coarseX * 0.16 + fractureX * 0.36 + detailX * 0.16;
        *dy = coarseY * 0.16 + fractureY * 0.36 + detailY * 0.16;
        *sharpness = 12.5;
        break;
    }
    case ETerrainBlendStyle::Paving:
    {
        const double courses = terrainTriangleWave(v * (5.0 + variation)
                                                    + phase / tau);
        const double joints = terrainTriangleWave(u * (6.0 + variation)
                                                   + (int(v * 6.0) & 1) * 0.5);
        *dx = coarseX * 0.10 + joints * 0.25;
        *dy = coarseY * 0.10 + courses * 0.25;
        *sharpness = 16.0;
        break;
    }
    case ETerrainBlendStyle::Lava:
    {
        const double flow = std::sin((u * 1.4 + v * (4.0 + variation))
                                     * tau + phase);
        const double branch = terrainValueNoise(u * 5.5, v * 5.5,
                                                 seed ^ 0x94d049bbu) - 0.5;
        *dx = coarseX * 0.32 + flow * 0.22 + branch * 0.35;
        *dy = coarseY * 0.32 + flow * 0.45 + branch * 0.25;
        *sharpness = 12.0 + std::fabs(flow) * 4.0;
        break;
    }
    case ETerrainBlendStyle::Swamp:
    {
        const double pools = terrainValueNoise(u * (3.0 + variation * 0.4),
            v * (3.0 + variation * 0.4), seed ^ 0x369dea0fu) - 0.5;
        const double reeds = std::sin((u * 8.0 + v * 1.25) * tau + phase);
        *dx = coarseX * 0.30 + pools * 0.42 + reeds * 0.12;
        *dy = coarseY * 0.30 + pools * 0.42 + reeds * 0.18;
        *sharpness = 9.5 + std::fabs(pools) * 5.0;
        break;
    }
    case ETerrainBlendStyle::Soil:
        *dx = coarseX * 0.52 + detailX * 0.24;
        *dy = coarseY * 0.52 + detailY * 0.24;
        *sharpness = 10.0;
        break;
    case ETerrainBlendStyle::Organic:
    default:
    {
        const double sweepX = std::sin(v * tau + phase)
                            + 0.45 * std::sin(v * 2.5 * tau + phase * 1.71);
        const double sweepY = std::sin(u * tau + phase * 1.31)
                            + 0.45 * std::sin(u * 2.5 * tau + phase * 0.73);
        *dx = coarseX * 0.60 + detailX * 0.20 + sweepX * 0.20;
        *dy = coarseY * 0.60 + detailY * 0.20 + sweepY * 0.20;
        break;
    }
    }
}

double terrainEdgeDisplacement(ETerrainBlendStyle style, qint32 first,
                               qint32 second, double position, quint32 seed)
{
    if(first == second)
        return 0.0;
    const bool reversed = first > second;
    if(reversed)
    {
        std::swap(first, second);
        position = 1.0 - position;
    }
    const quint32 pairSeed = terrainNoiseHash(seed);
    const double coarse = terrainValueNoise(position * 3.25, 0.375,
                                             pairSeed) - 0.5;
    const double detail = terrainValueNoise(position * 9.5, 1.875,
                                             pairSeed ^ 0x27d4eb2fu) - 0.5;
    const double tau = 6.28318530717958647692;
    double displacement = coarse * 0.20 + detail * 0.07;
    switch(style)
    {
    case ETerrainBlendStyle::Grass:
        displacement += std::sin(position * 11.0 * tau
            + double(pairSeed & 255u)) * 0.045;
        break;
    case ETerrainBlendStyle::Sand:
        displacement = coarse * 0.09 + detail * 0.025;
        break;
    case ETerrainBlendStyle::Gravel:
        displacement = coarse * 0.24 + detail * 0.11;
        break;
    case ETerrainBlendStyle::Snow:
        displacement = coarse * 0.18
            + std::sin(position * 4.0 * tau + double(pairSeed & 127u)) * 0.06;
        break;
    case ETerrainBlendStyle::Stone:
    case ETerrainBlendStyle::Paving:
    case ETerrainBlendStyle::Ice:
        displacement = coarse * 0.14
            + terrainTriangleWave(position * 6.0
                + double(pairSeed & 63u) / 63.0) * 0.045;
        break;
    case ETerrainBlendStyle::Water:
        displacement = coarse * 0.08
            + std::sin(position * 5.0 * tau + double(pairSeed & 255u)) * 0.055;
        break;
    default:
        break;
    }
    return reversed ? -displacement : displacement;
}

QRgb premultipliedBlend(const QRgb samples[4], const double weights[4])
{
    double alpha = 0.0;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    for(int index = 0; index < 4; ++index)
    {
        const double a = qAlpha(samples[index]) / 255.0;
        alpha += weights[index] * a;
        red += weights[index] * a * qRed(samples[index]);
        green += weights[index] * a * qGreen(samples[index]);
        blue += weights[index] * a * qBlue(samples[index]);
    }
    if(alpha <= 0.0)
        return qRgba(0, 0, 0, 0);
    return qRgba(qBound(0, qRound(red / alpha), 255),
                 qBound(0, qRound(green / alpha), 255),
                 qBound(0, qRound(blue / alpha), 255),
                 qBound(0, qRound(alpha * 255.0), 255));
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
    const bool valid = cellX >= 0 && cellX < m_tileWidth
        && cellY >= 0 && cellY < m_tileHeight;
    if(found)
        *found = valid;
    STerrainTransitionSignature result;
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
    ETerrainTileLayer layer,CTerrainVirtualMaterialField* output) const
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
        if(value==unset)return false;
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
    const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles)
    : m_catalog(catalog), m_authoredTiles(authoredTiles),
      m_materialBlendStyles(materialBlendStyles),
      m_distributionSeed(distributionSeed)
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
                m_materialSources.insert(material,
                    authoredTiles.at(slot).convertToFormat(QImage::Format_RGBA8888));
                m_materialSourceSlots.insert(material,slot);
            }
        }
    }
    for(auto source = externalMaterialSources.constBegin();
        source != externalMaterialSources.constEnd(); ++source)
    {
        if(source.value().isNull() || source.value().size() != tileSize)
            continue;
        m_materialSources.insert(source.key(), source.value().convertToFormat(
            QImage::Format_RGBA8888));
        // External uniform anchors have no slot in the opened map. They remain
        // logical sources until a pure or mixed tuple is realized into the
        // working cache/final atlas.
        m_materialSourceSlots.remove(source.key());
    }
    m_valid = true;
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
    const STerrainTransitionSignature& signature) const
{
    QMap<qint32, int> counts;
    for(qint32 material : corners(signature))
        ++counts[material];
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

int CTerrainVirtualAtlas::blendVariationLimit(
    const STerrainTransitionSignature& signature)
{
    QSet<qint32> materials;
    for(qint32 material : corners(signature))
        materials.insert(material);
    switch(materials.size())
    {
    case 0:
    case 1: return 1;
    case 2: return 5;
    case 3: return 3;
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

QImage CTerrainVirtualAtlas::generateTile(
    const STerrainTransitionSignature& gridSignature,
    ETerrainBlendStyle style, int variation,
    CValidationReport* report) const
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
    const int size = m_authoredTiles.front().width();
    const quint32 noiseSeed = transitionNoiseSeed(
        ids, m_materialSources, m_distributionSeed);
    const quint32 variantSeed = terrainNoiseHash(noiseSeed
        ^ quint32(variation + 1) * 0x9e3779b9u);
    struct EdgeDescriptor
    {
        qint32 first{0};
        qint32 second{0};
        ETerrainBlendStyle style{ETerrainBlendStyle::Organic};
        quint32 seed{0};
    };
    const auto makeEdge = [this](qint32 first, qint32 second) {
        EdgeDescriptor edge;
        edge.first = first;
        edge.second = second;
        const ETerrainBlendStyle firstStyle = m_materialBlendStyles.value(
            first, ETerrainBlendStyle::Organic);
        const ETerrainBlendStyle secondStyle = m_materialBlendStyles.value(
            second, ETerrainBlendStyle::Organic);
        edge.style = terrainBlendStylePriority(firstStyle)
            >= terrainBlendStylePriority(secondStyle) ? firstStyle : secondStyle;
        QVector<qint32> pair;
        if(first <= second) pair << first << second;
        else pair << second << first;
        edge.seed = transitionNoiseSeed(
            pair, m_materialSources, m_distributionSeed);
        return edge;
    };
    const EdgeDescriptor topEdge = makeEdge(ids[0], ids[1]);
    const EdgeDescriptor bottomEdge = makeEdge(ids[3], ids[2]);
    const EdgeDescriptor leftEdge = makeEdge(ids[0], ids[3]);
    const EdgeDescriptor rightEdge = makeEdge(ids[1], ids[2]);
    const auto edgeWarp = [](const EdgeDescriptor& edge, double position) {
        return terrainEdgeDisplacement(edge.style, edge.first, edge.second,
                                       position, edge.seed);
    };
    QImage result(size, size, QImage::Format_RGBA8888);
    for(int y = 0; y < size; ++y)
    {
        const double v = size == 1 ? 0.0 : double(y) / double(size - 1);
        for(int x = 0; x < size; ++x)
        {
            const double u = size == 1 ? 0.0 : double(x) / double(size - 1);
            // Warp the interior corner field with deterministic multi-scale
            // value noise. The envelope is zero on every tile edge, so two
            // neighbouring exact tuples still produce byte-identical seams,
            // while the visible meeting line meanders like authored terrain
            // instead of dividing the tile into four geometric quadrants.
            const double sinU = std::sin(u * 3.14159265358979323846);
            const double sinV = std::sin(v * 3.14159265358979323846);
            const double envelope = sinU * sinV;
            double displacementX = 0.0;
            double displacementY = 0.0;
            double sharpness = 9.0;
            terrainBlendDisplacement(style, u, v, variantSeed, variation,
                                     &displacementX, &displacementY,
                                     &sharpness);
            // Tangential edge warps are keyed only by the two endpoint
            // materials. Adjacent tiles therefore share the same irregular
            // silhouette instead of meeting at a repeated 90-degree quarter
            // cut. Interior variation remains cell-specific.
            const double horizontalEdgeWarp =
                (1.0 - v) * edgeWarp(topEdge, u)
                + v * edgeWarp(bottomEdge, u);
            const double verticalEdgeWarp =
                (1.0 - u) * edgeWarp(leftEdge, v)
                + u * edgeWarp(rightEdge, v);
            const double warpedU = qBound(0.0,
                u + envelope * displacementX + sinU * horizontalEdgeWarp, 1.0);
            const double warpedV = qBound(0.0,
                v + envelope * displacementY + sinV * verticalEdgeWarp, 1.0);
            const double bilinear[4] = {
                (1.0 - warpedU) * (1.0 - warpedV),
                warpedU * (1.0 - warpedV),
                warpedU * warpedV, (1.0 - warpedU) * warpedV
            };
            // A transition is a masked meeting of material textures, not a
            // low-frequency colour interpolation across the complete tile.
            // Aggregate duplicate corners first, then sharpen the material
            // masks so source detail is retained outside the true boundary.
            double weights[4] = {0.0, 0.0, 0.0, 0.0};
            double total = 0.0;
            QRgb samples[4];
            for(int corner = 0; corner < 4; ++corner)
            {
                samples[corner] = m_materialSources.value(ids.at(corner))
                    .pixelColor(x, y).rgba();
                bool first = true;
                double materialWeight = 0.0;
                for(int candidate = 0; candidate < 4; ++candidate)
                    if(ids.at(candidate) == ids.at(corner))
                    {
                        materialWeight += bilinear[candidate];
                        if(candidate < corner) first = false;
                    }
                if(first)
                {
                    // Style-specific sharpness is also faded out at edges so
                    // independently selected variants remain byte-seamless.
                    const double edgeSafeSharpness = 9.0
                        + envelope * (sharpness - 9.0);
                    weights[corner] = std::pow(materialWeight,
                                                edgeSafeSharpness);
                    total += weights[corner];
                }
            }
            if(total > 0.0)
                for(double& weight : weights) weight /= total;
            result.setPixelColor(x, y,
                QColor::fromRgba(premultipliedBlend(samples, weights)));
        }
    }
    return result;
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
    bool alreadyPresent = false;
    m_authoredLibraryCatalog.selectForPaint(
        signature, 0, 0, m_distributionSeed, &alreadyPresent);
    if(alreadyPresent)
        return true;
    const int libraryIndex = m_authoredLibraryGeneratedIndices.size();
    if(libraryIndex >= 16384)
        return false;
    STerrainFinalizedTile imported;
    imported.canonicalSignature = signature;
    imported.pixels = pixels.convertToFormat(QImage::Format_RGBA8888);
    imported.pixelSha256 = imageHash(imported.pixels);
    imported.authored = true;
    imported.sourceSlot = -1;
    const int generatedIndex = m_generatedTiles.size();
    m_generatedTiles.append(imported);
    m_generatedByPixels.insert(imported.pixelSha256, generatedIndex);
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

STerrainVirtualTile CTerrainVirtualAtlas::resolve(
    const STerrainTransitionSignature& signature, int cellX, int cellY,
    CValidationReport* report)
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
        return result;
    }

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
            result.generatedIndex =
                m_authoredLibraryGeneratedIndices.at(libraryIndex);
            result.generatedRotation = decoded.rotation;
            return result;
        }
    }

    quint8 rotation = 0;
    const STerrainTransitionSignature canonical =
        canonicalSignature(signature, &rotation);
    const ETerrainBlendStyle style = blendStyleForSignature(canonical);
    const int variation = blendVariation(
        cellX, cellY, style, blendVariationLimit(canonical));
    QByteArray key = signatureKey(canonical);
    key.append(char(terrainBlendStylePriority(style)));
    key.append(char(variation));
    int index = m_generatedBySignature.value(key, -1);
    if(index < 0)
    {
        const QImage pixels = generateTile(canonical, style, variation, report);
        if(pixels.isNull())
            return result;
        const QByteArray pixelsHash = imageHash(pixels);
        index = m_generatedByPixels.value(pixelsHash, -1);
        if(index < 0)
        {
            STerrainFinalizedTile generated;
            generated.canonicalSignature = canonical;
            generated.pixels = pixels;
            generated.pixelSha256 = pixelsHash;
            generated.authored = false;
            index = m_generatedTiles.size();
            m_generatedTiles.append(generated);
            m_generatedByPixels.insert(pixelsHash, index);
        }
        m_generatedBySignature.insert(key, index);
    }
    result.generatedIndex = index;
    result.generatedRotation = rotation;
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

STerrainAtlasFinalization CTerrainVirtualAtlas::finalize(
    const CTerrainVirtualMaterialField& field, const QString& sourceNamespace,
    int capacityLimit, const QVector<qint32>& sourceTileTypes,
    CValidationReport* report,const QVector<quint16>& pinnedPackedTiles)
{
    STerrainAtlasFinalization result;
    result.capacityLimit = capacityLimit;
    if(!m_valid || field.tileWidth() <= 0 || field.tileHeight() <= 0
       || capacityLimit <= 0 || capacityLimit > 16384)
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
            const STerrainVirtualTile tile = resolve(field.cellSignature(x, y), x, y, report);
            if(!tile.authored && tile.generatedIndex < 0)
            {
                result.error = QStringLiteral(
                    "An exact tile could not be authored because a material lacks a uniform source.");
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
        const int typeSource=tile.authored?tile.sourceSlot
            :m_materialSourceSlots.value(tile.canonicalSignature.northWest,-1);
        result.tileTypes.append(typeSource>=0&&typeSource<sourceTileTypes.size()
            ?sourceTileTypes.at(typeSource):qint32(0));
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
        stable.addData(tile.pixelSha256);
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
    if(tileTypeOffset<38||oldTailOffset>originalMp.size()){
        result.error=QStringLiteral("The MP tile-type array is truncated.");return result;
    }
    QByteArray replacementMp=originalMp.left(tileTypeOffset);
    for(qint32 type:finalization.tileTypes){
        char bytes[4];qToLittleEndian<qint32>(type,reinterpret_cast<uchar*>(bytes));
        replacementMp.append(bytes,4);
    }
    replacementMp.append(originalMp.mid(oldTailOffset));
    qToLittleEndian<quint32>(quint32(finalization.textureCount),
        reinterpret_cast<uchar*>(replacementMp.data()+16));
    qToLittleEndian<quint32>(quint32(finalization.pages.front().width()),
        reinterpret_cast<uchar*>(replacementMp.data()+20));
    qToLittleEndian<quint32>(quint32(finalization.tileTypes.size()),
        reinterpret_cast<uchar*>(replacementMp.data()+24));
    qToLittleEndian<quint32>(quint32(finalization.tiles.front().pixels.width()),
        reinterpret_cast<uchar*>(replacementMp.data()+28));
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
    const QVector<QByteArray>& authoredPagePayloads,
    CValidationReport* report)
{
    STerrainPortableAtlasPackage result;
    const SMprMapInfo& map = source.mapInfo();
    if(!map.valid || activeTiles.isEmpty() || activeTiles.size() % 64 != 0
       || activeTiles.size() > kMprPackedTerrainAtlasSlotLimit
       || authoredPagePayloads.size() != int(map.textureCount)
       || activeTiles.size() < int(map.tileCount))
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
    QVector<qint32> tileTypes = map.tileTypes;
    tileTypes.resize(activeTiles.size());
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
    const QMap<qint32, ETerrainBlendStyle>& materialBlendStyles)
    : m_authoredCatalog(authoredCatalog), m_workingCatalog(authoredCatalog),
      m_authoredImages(authoredTiles), m_workingImages(authoredTiles),
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
    m_resolver.reset(new CTerrainVirtualAtlas(
        m_authoredCatalog, m_authoredImages, distributionSeed,
        externalMaterialSources, materialBlendStyles));
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
        const STerrainTransitionSignature catalogSignature =
            m_authoredCatalog.signatureForPacked(source.encode(), &found);
        if(!found)
            continue;
        SMprPackedTile packed;
        packed.tileIndex = quint8(workingSlot % 64);
        packed.atlasIndex = quint8(workingSlot / 64);
        packed.rotation = quint8(rotation);
        STerrainTransitionVariant variant;
        variant.signature = catalogSignature;
        variant.packedTile = packed.encode();
        m_workingCatalog.addVariant(variant);
    }
}

quint16 CTerrainWorkingAtlas::resolve(
    const STerrainTransitionSignature& signature, int cellX, int cellY,
    QMap<int, quint64>* workingUsage, const QSet<int>& protectedSlots,
    bool* generated, CValidationReport* report)
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
        signature, cellX, cellY, report);
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

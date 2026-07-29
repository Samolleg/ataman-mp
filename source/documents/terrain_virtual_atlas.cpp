#include "terrain_virtual_atlas.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QRegularExpression>
#include <QTransform>
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
    // The opened map is itself reviewed stock JMV evidence. Index all of its
    // authored two-family transitions immediately so the first semantic
    // stroke can generate an absent tuple without waiting for an unrelated
    // exact global transition to be imported earlier in the session.
    for(int slot = 0; slot < authoredTiles.size(); ++slot)
    {
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot % 64);
        packed.atlasIndex = quint8(slot / 64);
        bool found = false;
        const STerrainTransitionSignature signature =
            catalog.gridSignatureForPacked(packed.encode(), &found);
        if(found)
            registerBlendMaskTemplate(signature, authoredTiles.at(slot),
                QStringLiteral("current-map stock slot %1").arg(slot), nullptr);
    }
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
    const QPoint points[4] = {{0, 0}, {mask.width() - 1, 0},
        {mask.width() - 1, mask.height() - 1}, {0, mask.height() - 1}};
    for(int corner = 0; corner < 4; ++corner)
        mask.setPixelColor(points[corner], previewCorners.at(corner) == owner
            ? Qt::white : Qt::black);

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
                const bool exactPair = (item.ownerMaterial == ownerMaterial
                    && item.otherMaterial == otherMaterial)
                    || (item.ownerMaterial == otherMaterial
                        && item.otherMaterial == ownerMaterial);
                const bool exactStyles = (item.ownerStyle == ownerStyle
                    && item.otherStyle == otherStyle)
                    || (item.ownerStyle == otherStyle
                        && item.otherStyle == ownerStyle);
                candidate.score = exactPair ? 10000 : exactStyles ? 1000 : 0;
                if(item.ownerStyle == ownerStyle || item.otherStyle == ownerStyle)
                    candidate.score += 200;
                if(item.ownerStyle == otherStyle || item.otherStyle == otherStyle)
                    candidate.score += 100;
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
    const Candidate& selected = matches.at(variation % qMax(1, pool));
    if(provenance) *provenance = selected.source;
    return selected.mask;
}

QImage CTerrainVirtualAtlas::generateTile(
    const STerrainTransitionSignature& gridSignature,
    ETerrainBlendStyle style, int variation,
    QString* provenance, CValidationReport* report) const
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
        QImage owner = alignedStockMask(preview, unique.at(0), unique.at(1),
                                        variation, &source);
        if(owner.isNull())
        {
            if(report) report->add("terrain.stock_blend_mask_missing",
                EValidationSeverity::Error,
                "No reviewed stock JMV mask matches this transition topology",
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
        // Multi-school junctions are composed from reviewed stock one-corner
        // transitions. Each corner retains a stock-authored silhouette; equal
        // identities are aggregated before normalization.
        for(qint32 material : unique)
        {
            QImage combined(m_authoredTiles.front().size(),
                            QImage::Format_Grayscale8);
            combined.fill(Qt::black);
            for(int corner = 0; corner < 4; ++corner)
            {
                if(ids.at(corner) != material) continue;
                qint32 support = unique.front() == material && unique.size() > 1
                    ? unique.at(1) : unique.front();
                STerrainTransitionSignature oneCorner;
                oneCorner.northWest = oneCorner.northEast =
                    oneCorner.southEast = oneCorner.southWest = support;
                if(corner == 0) oneCorner.northWest = material;
                if(corner == 1) oneCorner.northEast = material;
                if(corner == 2) oneCorner.southEast = material;
                if(corner == 3) oneCorner.southWest = material;
                QString source;
                const QImage cornerMask = alignedStockMask(
                    oneCorner, material, support, variation, &source);
                if(cornerMask.isNull())
                {
                    if(report) report->add("terrain.stock_blend_mask_missing",
                        EValidationSeverity::Error,
                        "No reviewed stock JMV one-corner mask can compose a multi-school junction",
                        QString::number(material));
                    return {};
                }
                sources << source;
                for(int y = 0; y < combined.height(); ++y)
                    for(int x = 0; x < combined.width(); ++x)
                    {
                        const int value = qMax(combined.pixelColor(x, y).red(),
                            cornerMask.pixelColor(x, y).red());
                        combined.setPixelColor(x, y, QColor(value, value, value));
                    }
            }
            ownership << combined;
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
            if(total > 0.0)
                for(double& weight : weights) weight /= total;
            const auto forceSharedEdge = [&](qint32 firstMaterial,
                                             qint32 secondMaterial,
                                             double position) {
                for(double& weight : weights) weight = 0.0;
                const qint32 selected = firstMaterial == secondMaterial
                    || position < 0.5 ? firstMaterial : secondMaterial;
                for(int corner = 0; corner < 4; ++corner)
                    if(ids.at(corner) == selected)
                    {
                        weights[corner] = 1.0;
                        break;
                    }
            };
            // Interior artwork comes from the reviewed stock mask. The outer
            // texel ring is resolved solely from its two edge endpoints so
            // independently selected interior variants cannot open a seam.
            if(y == 0) forceSharedEdge(ids.at(0), ids.at(1),
                size == 1 ? 0.0 : double(x) / double(size - 1));
            if(y == size - 1) forceSharedEdge(ids.at(3), ids.at(2),
                size == 1 ? 0.0 : double(x) / double(size - 1));
            if(x == 0) forceSharedEdge(ids.at(0), ids.at(3),
                size == 1 ? 0.0 : double(y) / double(size - 1));
            if(x == size - 1) forceSharedEdge(ids.at(1), ids.at(2),
                size == 1 ? 0.0 : double(y) / double(size - 1));
            result.setPixelColor(x, y,
                QColor::fromRgba(premultipliedBlend(samples, weights)));
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
    registerBlendMaskTemplate(signature, pixels, sourceLabel, report);
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
    imported.blendProvenance = sourceLabel;
    imported.stockDerivedBlend = true;
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
        QString provenance;
        const QImage pixels = generateTile(canonical, style, variation,
                                           &provenance, report);
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
            generated.blendProvenance = provenance;
            QSet<qint32> generatedMaterials;
            for(qint32 id : corners(canonical)) generatedMaterials.insert(id);
            generated.stockDerivedBlend = generatedMaterials.size() > 1;
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
                    "An exact tile could not be authored. See validation findings for a missing "
                    "uniform material source or reviewed stock JMV blend mask.");
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

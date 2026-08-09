#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QSet>
#include <QTextCodec>
#include <QTextStream>
#include <QtEndian>

#include <cmath>
#include <cstring>

#include "endurance_document.h"
#include "collection_document.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"
#include "mpr_document.h"
#include "res_file.h"
#include "terrain_atlas_profile.h"
#include "terrain_edit_model.h"
#include "terrain_virtual_atlas.h"

namespace {
struct Entry { qint64 offset{0}; qint64 size{0}; };
struct Archive
{
    QString path;
    QMap<QString, Entry> entries;
};

bool indexArchive(const QString& path, Archive* output)
{
    QFile file(path);
    if(!output || !file.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    SResFileHeader header;
    stream >> header;
    if(header.Signature != 0x019CE23C || header.TableSize > 10000000
       || !file.seek(header.TableOffset)) return false;
    QVector<SResHashTable> table(int(header.TableSize));
    for(SResHashTable& item : table) stream >> item;
    const QByteArray names = file.read(header.NamesLenght);
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    output->path = path;
    for(const SResHashTable& item : table)
    {
        if(quint64(item.NameOffset) + item.NameLength > quint64(names.size())
           || quint64(item.DataOffset) + item.DataSize > quint64(file.size()))
            continue;
        const QString name = codec->toUnicode(names.mid(
            int(item.NameOffset), int(item.NameLength))).toLower();
        Entry entry;
        entry.offset = item.DataOffset;
        entry.size = item.DataSize;
        output->entries.insert(name, entry);
    }
    return !output->entries.isEmpty();
}

bool readMember(const Archive& archive, const QString& name, QByteArray* bytes)
{
    const auto found = archive.entries.constFind(name.toLower());
    if(!bytes || found == archive.entries.constEnd()) return false;
    QFile file(archive.path);
    if(!file.open(QIODevice::ReadOnly) || !file.seek(found->offset)) return false;
    *bytes = file.read(found->size);
    return bytes->size() == found->size;
}

QByteArray fileSha256(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    while(!file.atEnd()) digest.addData(file.read(1024 * 1024));
    return digest.result().toHex();
}

QVector<QImage> splitAtlasPages(const QVector<QImage>& decodedPages,
                                int slotCount)
{
    QVector<QImage> tiles;
    tiles.reserve(slotCount);
    for(const QImage& decoded : decodedPages)
    {
        if(decoded.isNull() || decoded.width() != decoded.height()
           || decoded.width() % 8 != 0) return {};
        const QImage editorPage = decoded.mirrored(false, true);
        const int size = editorPage.width() / 8;
        for(int slot = 0; slot < 64 && tiles.size() < slotCount; ++slot)
            tiles.append(editorPage.copy((slot % 8) * size,
                                         (slot / 8) * size, size, size)
                .mirrored(false, true).convertToFormat(QImage::Format_RGBA8888));
    }
    return tiles.size() == slotCount ? tiles : QVector<QImage>();
}

struct Atlas
{
    bool valid{false};
    STerrainAtlasProfile profile;
    STerrainAtlasAnalysis analysis;
    QVector<QImage> tiles;
    QVector<QByteArray> pageMmpPayloads;
    QString archivePath;
    QByteArray pixelHash;
};

class StockAssets
{
public:
    bool index(const QString& jmvRoot, QString* error)
    {
        const QStringList roots = {
            QDir(jmvRoot).filePath(QStringLiteral("res")),
            QDir(jmvRoot).absoluteFilePath(QStringLiteral("../../../Res"))
        };
        for(const QString& root : roots)
        {
            QDir directory(root);
            for(const QString& fileName : directory.entryList(
                    {QStringLiteral("*.res")}, QDir::Files, QDir::Name))
            {
                Archive archive;
                if(indexArchive(directory.filePath(fileName), &archive))
                    m_archives.append(archive);
            }
        }
        if(m_archives.isEmpty() && error)
            *error = QStringLiteral("No readable stock JMV RES archives were found.");
        return !m_archives.isEmpty();
    }

    Atlas atlas(const QString& nameSpace, int slotCount, QString* error)
    {
        const QString cacheKey = nameSpace.toCaseFolded()
            + QLatin1Char(':') + QString::number(slotCount);
        if(m_cache.contains(cacheKey)) return m_cache.value(cacheKey);
        const int pages = (slotCount + 63) / 64;
        const QString prefix = nameSpace.toLower();
        for(const Archive& archive : m_archives)
        {
            QVector<QImage> decoded;
            QVector<QByteArray> pageMmpPayloads;
            bool complete = true;
            for(int page = 0; page < pages; ++page)
            {
                const QString member = QStringLiteral("%1%2.mmp")
                    .arg(prefix).arg(page, 3, 10, QLatin1Char('0'));
                QByteArray payload;
                if(!readMember(archive, member, &payload))
                {
                    complete = false;
                    break;
                }
                const ei::SMmpInspection inspected = ei::inspectMmp(payload);
                if(!inspected.valid)
                {
                    complete = false;
                    break;
                }
                pageMmpPayloads.append(payload);
                decoded.append(inspected.baseImage);
            }
            if(!complete) continue;
            const QVector<QImage> tiles = splitAtlasPages(decoded, slotCount);
            if(tiles.isEmpty()) continue;
            const QByteArray hash = CTerrainAtlasProfileInference::pixelSha256(tiles);
            CValidationReport report;
            const STerrainAtlasProfile profile =
                CTerrainAtlasPresetCatalog::exactProfile(
                    nameSpace, hash, slotCount, &report);
            if(!profile.approved) continue;
            Atlas result;
            result.profile = profile;
            result.analysis = profile.approvedAnalysis(&report);
            result.tiles = tiles;
            result.pageMmpPayloads = pageMmpPayloads;
            result.archivePath = archive.path;
            result.pixelHash = hash;
            result.valid = result.analysis.usable && !report.hasErrors();
            if(result.valid)
            {
                m_cache.insert(cacheKey, result);
                return result;
            }
        }
        if(error) *error = QStringLiteral(
            "No complete reviewed stock JMV atlas matched %1 (%2 slots).")
            .arg(nameSpace).arg(slotCount);
        return {};
    }

private:
    QVector<Archive> m_archives;
    QMap<QString, Atlas> m_cache;
};

QString slug(QString value)
{
    value = value.toLower();
    value.replace(QRegExp(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    while(value.startsWith('-')) value.remove(0, 1);
    while(value.endsWith('-')) value.chop(1);
    return value.isEmpty() ? QStringLiteral("material") : value;
}

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error)
{
    QFile file(path);
    if(file.open(QIODevice::WriteOnly | QIODevice::Truncate)
       && file.write(bytes) == bytes.size()) return true;
    if(error) *error = QStringLiteral("Could not write %1").arg(path);
    return false;
}

STerrainTransitionSignature finalizedSignature(
    const STerrainAtlasFinalization& finalized, quint16 encoded, bool* found)
{
    const SMprPackedTile packed = SMprPackedTile::decode(encoded);
    if(packed.atlasSlot() < 0 || packed.atlasSlot() >= finalized.tiles.size())
    {
        if(found) *found = false;
        return {};
    }
    if(found) *found = true;
    return CTerrainTransitionCatalog::rotated(
        finalized.tiles.at(packed.atlasSlot()).canonicalSignature,
        -packed.rotation);
}

bool sameSignature(const STerrainTransitionSignature& left,
                   const STerrainTransitionSignature& right)
{
    return left.northWest == right.northWest
        && left.northEast == right.northEast
        && left.southEast == right.southEast
        && left.southWest == right.southWest;
}

QVector<quint16> pinnedOverlay(const CTerrainEditModel& terrain)
{
    QVector<quint16> result;
    for(int y = 0; y < terrain.tileCountY(); ++y)
        for(int x = 0; x < terrain.tileCountX(); ++x)
        {
            bool available = false;
            const quint16 packed = terrain.packedTile(
                x, y, ETerrainTileLayer::Overlay, &available);
            if(available && packed != 0xffffu) result.append(packed);
        }
    return result;
}

QMap<qint32, QImage> globalSources(
    const QVector<STerrainGlobalMaterialFamily>& families,
    StockAssets* assets, const QSize& tileSize, QJsonArray* sourceManifest,
    QString* error)
{
    QMap<qint32, QImage> result;
    for(const STerrainGlobalMaterialFamily& family : families)
    {
        if(family.layer != ETerrainAtlasLayer::Land) continue;
        bool loaded = false;
        QImage fallback;
        STerrainGlobalMaterialSource chosen;
        QSize chosenSourceSize;
        QJsonArray candidateSizes;
        for(const STerrainGlobalMaterialSource& source : family.sources)
        {
            Atlas atlas = assets->atlas(source.mprNamespace,
                                        source.textureCount * 64, nullptr);
            if(!atlas.valid || source.uniformSlot < 0
               || source.uniformSlot >= atlas.tiles.size()) continue;
            const QImage image = atlas.tiles.at(source.uniformSlot);
            QJsonObject candidateSize;
            candidateSize.insert(QStringLiteral("namespace"),
                                 source.mprNamespace);
            candidateSize.insert(QStringLiteral("width"), image.width());
            candidateSize.insert(QStringLiteral("height"), image.height());
            candidateSizes.append(candidateSize);
            if(fallback.isNull())
            {
                fallback = image;
                chosen = source;
                chosenSourceSize = image.size();
            }
            if(image.size() != tileSize) continue;
            fallback = image;
            chosen = source;
            chosenSourceSize = image.size();
            loaded = true;
            break;
        }
        if(!loaded && !fallback.isNull())
        {
            fallback = fallback.scaled(tileSize, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
            loaded = true;
        }
        if(loaded)
        {
            result.insert(family.id, fallback);
            if(sourceManifest)
            {
                QJsonObject item;
                item.insert(QStringLiteral("material_id"), family.id);
                item.insert(QStringLiteral("material"), family.name);
                item.insert(QStringLiteral("source_namespace"),
                            chosen.mprNamespace);
                item.insert(QStringLiteral("reviewed_pure_anchor_slot"),
                            chosen.uniformSlot);
                item.insert(QStringLiteral("target_tile_width"), tileSize.width());
                item.insert(QStringLiteral("source_tile_width"),
                            chosenSourceSize.width());
                item.insert(QStringLiteral("resampled"),
                            chosenSourceSize != tileSize);
                item.insert(QStringLiteral("candidate_sizes"), candidateSizes);
                qint64 transparentPixels = 0;
                quint64 neighbourDelta = 0;
                quint64 neighbourSamples = 0;
                for(int y = 0; y < fallback.height(); ++y)
                    for(int x = 0; x < fallback.width(); ++x)
                    {
                        const QColor pixel = fallback.pixelColor(x, y);
                        transparentPixels += pixel.alpha() == 0;
                        if(x + 1 < fallback.width())
                        {
                            const QColor next = fallback.pixelColor(x + 1, y);
                            neighbourDelta += quint64(qAbs(pixel.red() - next.red())
                                + qAbs(pixel.green() - next.green())
                                + qAbs(pixel.blue() - next.blue()));
                            ++neighbourSamples;
                        }
                        if(y + 1 < fallback.height())
                        {
                            const QColor next = fallback.pixelColor(x, y + 1);
                            neighbourDelta += quint64(qAbs(pixel.red() - next.red())
                                + qAbs(pixel.green() - next.green())
                                + qAbs(pixel.blue() - next.blue()));
                            ++neighbourSamples;
                        }
                    }
                item.insert(QStringLiteral("transparent_pixels"),
                            double(transparentPixels));
                item.insert(QStringLiteral("transparent_fraction"),
                            fallback.isNull() ? 0.0
                            : double(transparentPixels)
                                / double(fallback.width() * fallback.height()));
                item.insert(QStringLiteral("mean_neighbour_rgb_delta"),
                            neighbourSamples == 0 ? 0.0
                            : double(neighbourDelta) / double(neighbourSamples));
                item.insert(QStringLiteral("pixel_sha256"), QString::fromLatin1(
                    QCryptographicHash::hash(
                        QByteArray(reinterpret_cast<const char*>(fallback.constBits()),
                                   fallback.byteCount()), QCryptographicHash::Sha256)
                        .toHex()));
                sourceManifest->append(item);
            }
        }
        if(!loaded)
        {
            if(error) *error = QStringLiteral(
                "No reviewed pure stock-JMV source could be loaded for %1.")
                .arg(family.name);
            return {};
        }
    }
    return result;
}

QByteArray reviewImageHash(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y = 0; y < image.height(); ++y)
        hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)),
                     image.bytesPerLine());
    return hash.result().toHex();
}

QByteArray reviewRgbImageHash(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_RGB888);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y = 0; y < image.height(); ++y)
        hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)),
                     image.width() * 3);
    return hash.result().toHex();
}

QJsonObject writeBlendMaskReview(
    const CTerrainVirtualAtlas& atlas, const QVector<qint32>& materialIds,
    const QMap<qint32, QString>& names,
    const QMap<qint32, QImage>& externalSources,
    const QString& outputDirectory, QString* error)
{
    struct Topology
    {
        Topology() = default;
        Topology(const QString& valueName, int valueVariation,
                 int valueOwnerCorners, bool valueDiagonal)
            : name(valueName), variation(valueVariation),
              ownerCorners(valueOwnerCorners), diagonal(valueDiagonal) {}
        QString name;
        int variation{0};
        int ownerCorners{1};
        bool diagonal{false};
    };
    struct SupplementalCase
    {
        SupplementalCase() = default;
        SupplementalCase(qint32 valueOwner, qint32 valueSupport,
                         const Topology& valueTopology,
                         const QString& valueScenario)
            : owner(valueOwner), support(valueSupport), topology(valueTopology),
              scenario(valueScenario) {}
        qint32 owner{0};
        qint32 support{0};
        Topology topology;
        QString scenario;
    };
    const QVector<Topology> topologies = {
        {QStringLiteral("outer-1of4-v1"), 0, 1, false},
        {QStringLiteral("outer-1of4-v2"), 1, 1, false},
        {QStringLiteral("edge-2of4-v1"), 0, 2, false},
        {QStringLiteral("edge-2of4-v2"), 1, 2, false},
        {QStringLiteral("diagonal-2of4"), 0, 2, true},
        {QStringLiteral("inside-3of4-v1"), 0, 3, false},
        {QStringLiteral("inside-3of4-v2"), 1, 3, false}
    };
    QMap<qint32, ETerrainBlendStyle> styles;
    QMap<ETerrainBlendStyle, QVector<qint32>> byStyle;
    for(qint32 id : materialIds)
    {
        const ETerrainBlendStyle style =
            CTerrainVirtualAtlas::blendStyleForMaterialName(names.value(id));
        styles.insert(id, style);
        byStyle[style].append(id);
    }
    qint32 commonGrass = -1;
    for(qint32 id : materialIds)
        if(names.value(id).compare(QStringLiteral("Grass / Common"),
                                   Qt::CaseInsensitive) == 0)
        {
            commonGrass = id;
            break;
        }
    if(commonGrass < 0 && !materialIds.isEmpty()) commonGrass = materialIds.front();

    const QString reviewDirectory = QDir(outputDirectory).filePath(
        QStringLiteral("blend-mask-review"));
    if(!QDir().mkpath(reviewDirectory))
    {
        if(error) *error = QStringLiteral("Could not create %1").arg(reviewDirectory);
        return {};
    }
    const QString uniformDirectory = QDir(reviewDirectory).filePath(
        QStringLiteral("uniform-materials"));
    if(!QDir().mkpath(uniformDirectory))
    {
        if(error) *error = QStringLiteral("Could not create %1")
            .arg(uniformDirectory);
        return {};
    }
    QMap<qint32, QString> uniformFiles;
    QMap<qint32, QString> uniformFileHashes;
    for(qint32 id : materialIds)
    {
        const QImage uniform = externalSources.value(id);
        if(uniform.isNull())
        {
            if(error) *error = QStringLiteral(
                "Missing proven four-corner uniform source for material %1")
                .arg(id);
            return {};
        }
        const QString relative = QStringLiteral("uniform-materials/%1.png")
            .arg(id);
        if(!uniform.save(QDir(reviewDirectory).filePath(relative), "PNG"))
        {
            if(error) *error = QStringLiteral("Could not write %1").arg(relative);
            return {};
        }
        uniformFiles.insert(id, relative);
        uniformFileHashes.insert(id, QString::fromLatin1(fileSha256(
            QDir(reviewDirectory).filePath(relative))));
    }
    QByteArray csv("sheet,row,column,owner_material_id,owner_material,owner_style,mask_family,"
                   "support_scenario,support_material_id,support_material,topology,"
                   "variation,provenance,mask_sha256,missing,heatmap_sheet,"
                   "heatmap_sha256,heatmap_missing,result_example_sheet,"
                   "result_example_sha256,result_example_missing,"
                   "linear_result_sheet,linear_result_sha256,linear_result_missing,owner_uniform,"
                   "owner_uniform_sha256,support_uniform,support_uniform_sha256\n");
    const auto csvText = [](QString value) {
        value.replace('"', QStringLiteral("\"\""));
        return QByteArray("\"") + value.toUtf8() + QByteArray("\"");
    };
    QJsonArray sheets;
    int totalMasks = 0;
    int missingMasks = 0;
    QMap<QString, int> familyTopologyCounts;
    QSet<QString> applicationIdentities;
    QJsonArray limitedExampleGroups;
    const int targetExamplesPerMask = 4;
    const auto familyTopologyKey = [](const QString& family,
                                      const QString& topology) {
        return family + QLatin1Char('|') + topology;
    };
    const auto applicationIdentity = [](qint32 owner, qint32 support,
                                        const QString& scenario,
                                        const QString& topology) {
        return QStringLiteral("%1|%2|%3|%4")
            .arg(owner).arg(support).arg(scenario, topology);
    };
    const int rowsPerSheet = 20;
    for(auto styleIt = byStyle.constBegin(); styleIt != byStyle.constEnd(); ++styleIt)
    {
        const ETerrainBlendStyle ownerStyle = styleIt.key();
        const QVector<qint32>& owners = styleIt.value();
        for(int page = 0; page * rowsPerSheet < owners.size(); ++page)
        {
            const int firstRow = page * rowsPerSheet;
            const int rowCount = qMin(rowsPerSheet, owners.size() - firstRow);
            const int columns = topologies.size() * 2;
            const int tileSize = 256;
            QImage sheet(columns * tileSize, rowCount * tileSize,
                         QImage::Format_RGB888);
            sheet.fill(QColor(36, 36, 36));
            QImage heatmaps(columns * tileSize, rowCount * tileSize,
                            QImage::Format_RGB888);
            heatmaps.fill(QColor(36, 36, 36));
            QImage results(columns * tileSize, rowCount * tileSize,
                           QImage::Format_RGB888);
            results.fill(QColor(36, 36, 36));
            QImage linearResults(columns * tileSize, rowCount * tileSize,
                                 QImage::Format_RGB888);
            linearResults.fill(QColor(36, 36, 36));
            QPainter painter(&sheet);
            QPainter heatmapPainter(&heatmaps);
            QPainter resultPainter(&results);
            QPainter linearResultPainter(&linearResults);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            heatmapPainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            resultPainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            linearResultPainter.setRenderHint(
                QPainter::SmoothPixmapTransform, false);
            const QString sheetName = QStringLiteral("%1-%2.png")
                .arg(slug(CTerrainVirtualAtlas::blendStyleName(ownerStyle)))
                .arg(page + 1, 2, 10, QLatin1Char('0'));
            const QString heatmapSheetName = QStringLiteral("%1-%2-heatmaps.png")
                .arg(slug(CTerrainVirtualAtlas::blendStyleName(ownerStyle)))
                .arg(page + 1, 2, 10, QLatin1Char('0'));
            const QString resultSheetName = QStringLiteral("%1-%2-results.png")
                .arg(slug(CTerrainVirtualAtlas::blendStyleName(ownerStyle)))
                .arg(page + 1, 2, 10, QLatin1Char('0'));
            const QString linearResultSheetName =
                QStringLiteral("%1-%2-results-linear.png")
                    .arg(slug(CTerrainVirtualAtlas::blendStyleName(ownerStyle)))
                    .arg(page + 1, 2, 10, QLatin1Char('0'));
            for(int localRow = 0; localRow < rowCount; ++localRow)
            {
                const qint32 owner = owners.at(firstRow + localRow);
                qint32 sameStyle = -1;
                for(qint32 candidate : owners)
                    if(candidate != owner) { sameStyle = candidate; break; }
                qint32 crossStyle = commonGrass;
                if(crossStyle == owner || styles.value(crossStyle) == ownerStyle)
                    for(qint32 candidate : materialIds)
                        if(candidate != owner && styles.value(candidate) != ownerStyle)
                        { crossStyle = candidate; break; }
                if(sameStyle < 0) sameStyle = crossStyle;
                const qint32 supports[2] = {crossStyle, sameStyle};
                const QString scenarios[2] = {
                    QStringLiteral("cross-style"), QStringLiteral("same-style")};
                for(int scenario = 0; scenario < 2; ++scenario)
                    for(int topologyIndex = 0;
                        topologyIndex < topologies.size(); ++topologyIndex)
                    {
                        const Topology& topology = topologies.at(topologyIndex);
                        const qint32 support = supports[scenario];
                        STerrainTransitionSignature signature;
                        signature.northWest = owner;
                        signature.northEast = !topology.diagonal
                            && topology.ownerCorners >= 2 ? owner : support;
                        signature.southEast = topology.diagonal
                            ? owner : (topology.ownerCorners >= 3 ? owner : support);
                        signature.southWest = support;
                        QString provenance;
                        QImage mask = atlas.reviewEffectiveBlendMask(
                            signature, owner, topology.variation,
                            &provenance, owner);
                        // Review the same paint-owned family and polarity that
                        // the generated outcome uses. Canonical material-id
                        // ordering is an internal compositor detail and must
                        // never select or rotate a different questionnaire
                        // mask.
                        const int column = scenario * topologies.size()
                            + topologyIndex;
                        if(mask.isNull())
                        {
                            ++missingMasks;
                            painter.fillRect(column * tileSize,
                                localRow * tileSize, tileSize, tileSize,
                                QColor(180, 0, 180));
                            painter.setPen(QPen(Qt::black, 12));
                            painter.drawLine(column * tileSize,
                                localRow * tileSize,
                                (column + 1) * tileSize - 1,
                                (localRow + 1) * tileSize - 1);
                            painter.drawLine((column + 1) * tileSize - 1,
                                localRow * tileSize, column * tileSize,
                                (localRow + 1) * tileSize - 1);
                        }
                        else
                        {
                            ++totalMasks;
                            painter.drawImage(column * tileSize,
                                localRow * tileSize,
                                mask.scaled(tileSize, tileSize,
                                    Qt::IgnoreAspectRatio, Qt::FastTransformation));
                        }
                        QImage heatmap;
                        if(!mask.isNull())
                        {
                            heatmap = QImage(mask.size(), QImage::Format_RGB888);
                            for(int y = 0; y < mask.height(); ++y)
                                for(int x = 0; x < mask.width(); ++x)
                                {
                                    const int value = mask.pixelColor(x, y).red();
                                    const QColor color = value >= 204
                                        ? QColor(42, 184, 92)
                                        : value <= 51 ? QColor(35, 52, 96)
                                                      : QColor(236, 154, 52);
                                    heatmap.setPixelColor(x, y, color);
                                }
                        }
                        if(heatmap.isNull())
                            heatmapPainter.fillRect(column * tileSize,
                                localRow * tileSize, tileSize, tileSize,
                                QColor(180, 0, 180));
                        else
                            heatmapPainter.drawImage(column * tileSize,
                                localRow * tileSize,
                                heatmap.scaled(tileSize, tileSize,
                                    Qt::IgnoreAspectRatio, Qt::FastTransformation));
                        CValidationReport blendReport;
                        QString resultProvenance;
                        const QImage generatedResult = atlas.reviewGeneratedBlend(
                            signature, topology.variation, &resultProvenance,
                            &blendReport, owner);
                        QString linearProvenance;
                        const QImage linearResult =
                            atlas.reviewGeneratedBlendLinearLight(
                                signature, topology.variation,
                                &linearProvenance, &blendReport, owner);
                        if(generatedResult.isNull())
                            resultPainter.fillRect(column * tileSize,
                                localRow * tileSize, tileSize, tileSize,
                                QColor(180, 0, 180));
                        else
                            resultPainter.drawImage(column * tileSize,
                                localRow * tileSize,
                                generatedResult.scaled(tileSize, tileSize,
                                    Qt::IgnoreAspectRatio, Qt::FastTransformation));
                        if(linearResult.isNull())
                            linearResultPainter.fillRect(column * tileSize,
                                localRow * tileSize, tileSize, tileSize,
                                QColor(180, 0, 180));
                        else
                            linearResultPainter.drawImage(column * tileSize,
                                localRow * tileSize,
                                linearResult.scaled(tileSize, tileSize,
                                    Qt::IgnoreAspectRatio, Qt::FastTransformation));
                        const QString reviewFamily = provenance.section(
                            QLatin1Char(':'), 1, 1);
                        const QString resultFamily = resultProvenance.section(
                            QLatin1Char(':'), 1, 1);
                        const QString linearFamily = linearProvenance.section(
                            QLatin1Char(':'), 1, 1);
                        if(!generatedResult.isNull()
                            && (resultFamily != reviewFamily
                                || linearFamily != reviewFamily))
                        {
                            if(error) *error = QStringLiteral(
                                "Mask/result family mismatch for %1 %2")
                                .arg(owner).arg(topology.name);
                            return {};
                        }
                        const QString maskFamily = reviewFamily;
                        ++familyTopologyCounts[familyTopologyKey(
                            maskFamily, topology.name)];
                        applicationIdentities.insert(applicationIdentity(
                            owner, support, scenarios[scenario], topology.name));
                        csv += csvText(sheetName) + ','
                            + QByteArray::number(localRow) + ','
                            + QByteArray::number(column) + ','
                            + QByteArray::number(owner) + ','
                            + csvText(names.value(owner)) + ','
                            + csvText(CTerrainVirtualAtlas::blendStyleName(ownerStyle)) + ','
                            + csvText(maskFamily) + ','
                            + csvText(scenarios[scenario]) + ','
                            + QByteArray::number(support) + ','
                            + csvText(names.value(support)) + ','
                            + csvText(topology.name) + ','
                            + QByteArray::number(topology.variation) + ','
                            + csvText(provenance) + ','
                            + csvText(mask.isNull() ? QString()
                                : QString::fromLatin1(reviewImageHash(mask))) + ','
                            + QByteArray::number(mask.isNull()) + ','
                            + csvText(heatmapSheetName) + ','
                            + csvText(heatmap.isNull() ? QString()
                                : QString::fromLatin1(reviewRgbImageHash(heatmap))) + ','
                            + QByteArray::number(heatmap.isNull()) + ','
                            + csvText(resultSheetName) + ','
                            + csvText(generatedResult.isNull() ? QString()
                                : QString::fromLatin1(reviewRgbImageHash(
                                    generatedResult))) + ','
                            + QByteArray::number(generatedResult.isNull()) + ','
                            + csvText(linearResultSheetName) + ','
                            + csvText(linearResult.isNull() ? QString()
                                : QString::fromLatin1(reviewRgbImageHash(
                                    linearResult))) + ','
                            + QByteArray::number(linearResult.isNull()) + ','
                            + csvText(uniformFiles.value(owner)) + ','
                            + csvText(uniformFileHashes.value(owner)) + ','
                            + csvText(uniformFiles.value(support)) + ','
                            + csvText(uniformFileHashes.value(support)) + '\n';
                    }
            }
            painter.end();
            heatmapPainter.end();
            resultPainter.end();
            linearResultPainter.end();
            if(!sheet.save(QDir(reviewDirectory).filePath(sheetName), "PNG"))
            {
                if(error) *error = QStringLiteral("Could not write mask sheet %1")
                    .arg(sheetName);
                return {};
            }
            if(!heatmaps.save(QDir(reviewDirectory).filePath(heatmapSheetName),
                              "PNG"))
            {
                if(error) *error = QStringLiteral(
                    "Could not write threshold heatmap sheet %1")
                    .arg(heatmapSheetName);
                return {};
            }
            if(!results.save(QDir(reviewDirectory).filePath(resultSheetName),
                             "PNG"))
            {
                if(error) *error = QStringLiteral(
                    "Could not write generated result sheet %1")
                    .arg(resultSheetName);
                return {};
            }
            if(!linearResults.save(QDir(reviewDirectory).filePath(
                    linearResultSheetName), "PNG"))
            {
                if(error) *error = QStringLiteral(
                    "Could not write linear-light result sheet %1")
                    .arg(linearResultSheetName);
                return {};
            }
            QJsonObject sheetEntry;
            sheetEntry.insert(QStringLiteral("file"), sheetName);
            sheetEntry.insert(QStringLiteral("heatmaps"), heatmapSheetName);
            sheetEntry.insert(QStringLiteral("generated_results"), resultSheetName);
            sheetEntry.insert(QStringLiteral("linear_light_results"),
                              linearResultSheetName);
            sheetEntry.insert(QStringLiteral("style"),
                CTerrainVirtualAtlas::blendStyleName(ownerStyle));
            sheetEntry.insert(QStringLiteral("first_row"), firstRow);
            sheetEntry.insert(QStringLiteral("rows"), rowCount);
            sheetEntry.insert(QStringLiteral("columns"), columns);
            sheets.append(sheetEntry);
        }
    }

    // The broad application matrix above is useful evidence, but a low-count
    // material family can be masked by the other style in its initially
    // selected support pair. Add only genuine missing evidence cases, up to
    // four concrete outcomes. Some atlas families have only one or two
    // distinct source/support applications for a topology, and some zones
    // have none. Record that honestly; never pad with duplicated or invented
    // examples and never reject a valid map for review-only sample scarcity.
    QVector<SupplementalCase> supplemental;
    for(auto styleIt = byStyle.constBegin(); styleIt != byStyle.constEnd(); ++styleIt)
    {
        const QString desiredFamily =
            CTerrainVirtualAtlas::blendStyleName(styleIt.key());
        for(const Topology& topology : topologies)
        {
            const QString groupKey = familyTopologyKey(
                desiredFamily, topology.name);
            const auto consider = [&](qint32 owner, qint32 support) {
                if(owner == support
                   || familyTopologyCounts.value(groupKey)
                        >= targetExamplesPerMask)
                    return;
                const QString scenario = styles.value(owner) == styles.value(support)
                    ? QStringLiteral("same-style")
                    : QStringLiteral("cross-style");
                const QString identity = applicationIdentity(
                    owner, support, scenario, topology.name);
                if(applicationIdentities.contains(identity)) return;
                STerrainTransitionSignature signature;
                signature.northWest = owner;
                signature.northEast = !topology.diagonal
                    && topology.ownerCorners >= 2 ? owner : support;
                signature.southEast = topology.diagonal
                    ? owner : (topology.ownerCorners >= 3 ? owner : support);
                signature.southWest = support;
                QString provenance;
                const QImage mask = atlas.reviewBlendMask(
                    signature, owner, topology.variation, &provenance);
                if(mask.isNull()
                   || provenance.section(QLatin1Char(':'), 1, 1)
                        != desiredFamily)
                    return;
                supplemental.append(SupplementalCase(
                    owner, support, topology, scenario));
                applicationIdentities.insert(identity);
                ++familyTopologyCounts[groupKey];
            };
            if(topology.ownerCorners >= 3)
            {
                for(qint32 support : styleIt.value())
                    for(qint32 owner : materialIds)
                        consider(owner, support);
            }
            else
            {
                for(qint32 owner : styleIt.value())
                    for(qint32 support : materialIds)
                        consider(owner, support);
            }
            const int exampleCount = familyTopologyCounts.value(groupKey);
            if(exampleCount < targetExamplesPerMask)
            {
                QJsonObject limited;
                limited.insert(QStringLiteral("mask_family"), desiredFamily);
                limited.insert(QStringLiteral("topology"), topology.name);
                limited.insert(QStringLiteral("genuine_examples"), exampleCount);
                limitedExampleGroups.append(limited);
            }
        }
    }
    if(!supplemental.isEmpty())
    {
        const int columns = 7;
        const int rows = (supplemental.size() + columns - 1) / columns;
        const int tileSize = 256;
        const QString sheetName = QStringLiteral("canonical-supplemental.png");
        const QString heatmapSheetName =
            QStringLiteral("canonical-supplemental-heatmaps.png");
        const QString resultSheetName =
            QStringLiteral("canonical-supplemental-results.png");
        const QString linearResultSheetName =
            QStringLiteral("canonical-supplemental-results-linear.png");
        QImage sheet(columns * tileSize, rows * tileSize, QImage::Format_RGB888);
        QImage heatmaps(sheet.size(), QImage::Format_RGB888);
        QImage results(sheet.size(), QImage::Format_RGB888);
        QImage linearResults(sheet.size(), QImage::Format_RGB888);
        sheet.fill(QColor(36, 36, 36));
        heatmaps.fill(QColor(36, 36, 36));
        results.fill(QColor(36, 36, 36));
        linearResults.fill(QColor(36, 36, 36));
        QPainter painter(&sheet);
        QPainter heatmapPainter(&heatmaps);
        QPainter resultPainter(&results);
        QPainter linearResultPainter(&linearResults);
        for(int index = 0; index < supplemental.size(); ++index)
        {
            const SupplementalCase& item = supplemental.at(index);
            const int row = index / columns;
            const int column = index % columns;
            STerrainTransitionSignature signature;
            signature.northWest = item.owner;
            signature.northEast = !item.topology.diagonal
                && item.topology.ownerCorners >= 2 ? item.owner : item.support;
            signature.southEast = item.topology.diagonal
                ? item.owner
                : (item.topology.ownerCorners >= 3 ? item.owner : item.support);
            signature.southWest = item.support;
            QString provenance;
            const QImage mask = atlas.reviewEffectiveBlendMask(
                signature, item.owner, item.topology.variation,
                &provenance, item.owner);
            if(mask.isNull())
            {
                if(error) *error = QStringLiteral(
                    "Supplemental canonical mask unexpectedly disappeared");
                return {};
            }
            ++totalMasks;
            painter.drawImage(column * tileSize, row * tileSize,
                mask.scaled(tileSize, tileSize, Qt::IgnoreAspectRatio,
                            Qt::FastTransformation));
            QImage heatmap(mask.size(), QImage::Format_RGB888);
            for(int y = 0; y < mask.height(); ++y)
                for(int x = 0; x < mask.width(); ++x)
                {
                    const int value = mask.pixelColor(x, y).red();
                    const QColor color = value >= 204
                        ? QColor(42, 184, 92)
                        : value <= 51 ? QColor(35, 52, 96)
                                          : QColor(236, 154, 52);
                    heatmap.setPixelColor(x, y, color);
                }
            heatmapPainter.drawImage(column * tileSize, row * tileSize,
                heatmap.scaled(tileSize, tileSize, Qt::IgnoreAspectRatio,
                               Qt::FastTransformation));
            CValidationReport blendReport;
            QString resultProvenance;
            const QImage generatedResult = atlas.reviewGeneratedBlend(
                signature, item.topology.variation, &resultProvenance,
                &blendReport, item.owner);
            QString linearProvenance;
            const QImage linearResult = atlas.reviewGeneratedBlendLinearLight(
                signature, item.topology.variation, &linearProvenance,
                &blendReport, item.owner);
            const QString maskFamily = provenance.section(
                QLatin1Char(':'), 1, 1);
            if(generatedResult.isNull() || linearResult.isNull()
               || resultProvenance.section(QLatin1Char(':'), 1, 1) != maskFamily
               || linearProvenance.section(QLatin1Char(':'), 1, 1) != maskFamily)
            {
                if(error) *error = QStringLiteral(
                    "Supplemental mask/result family mismatch for %1")
                    .arg(maskFamily);
                return {};
            }
            resultPainter.drawImage(column * tileSize, row * tileSize,
                generatedResult.scaled(tileSize, tileSize,
                    Qt::IgnoreAspectRatio, Qt::FastTransformation));
            linearResultPainter.drawImage(column * tileSize, row * tileSize,
                linearResult.scaled(tileSize, tileSize,
                    Qt::IgnoreAspectRatio, Qt::FastTransformation));
            csv += csvText(sheetName) + ',' + QByteArray::number(row) + ','
                + QByteArray::number(column) + ','
                + QByteArray::number(item.owner) + ','
                + csvText(names.value(item.owner)) + ','
                + csvText(CTerrainVirtualAtlas::blendStyleName(
                    styles.value(item.owner))) + ','
                + csvText(maskFamily) + ',' + csvText(item.scenario) + ','
                + QByteArray::number(item.support) + ','
                + csvText(names.value(item.support)) + ','
                + csvText(item.topology.name) + ','
                + QByteArray::number(item.topology.variation) + ','
                + csvText(provenance) + ','
                + csvText(QString::fromLatin1(reviewImageHash(mask))) + ",0,"
                + csvText(heatmapSheetName) + ','
                + csvText(QString::fromLatin1(reviewRgbImageHash(heatmap))) + ",0,"
                + csvText(resultSheetName) + ','
                + csvText(QString::fromLatin1(reviewRgbImageHash(
                    generatedResult))) + ",0,"
                + csvText(linearResultSheetName) + ','
                + csvText(QString::fromLatin1(reviewRgbImageHash(
                    linearResult))) + ",0,"
                + csvText(uniformFiles.value(item.owner)) + ','
                + csvText(uniformFileHashes.value(item.owner)) + ','
                + csvText(uniformFiles.value(item.support)) + ','
                + csvText(uniformFileHashes.value(item.support)) + '\n';
        }
        painter.end();
        heatmapPainter.end();
        resultPainter.end();
        linearResultPainter.end();
        if(!sheet.save(QDir(reviewDirectory).filePath(sheetName), "PNG")
           || !heatmaps.save(QDir(reviewDirectory).filePath(
                heatmapSheetName), "PNG")
           || !results.save(QDir(reviewDirectory).filePath(
                resultSheetName), "PNG")
           || !linearResults.save(QDir(reviewDirectory).filePath(
                linearResultSheetName), "PNG"))
        {
            if(error) *error = QStringLiteral(
                "Could not write supplemental canonical review sheets");
            return {};
        }
        QJsonObject sheetEntry;
        sheetEntry.insert(QStringLiteral("file"), sheetName);
        sheetEntry.insert(QStringLiteral("heatmaps"), heatmapSheetName);
        sheetEntry.insert(QStringLiteral("generated_results"), resultSheetName);
        sheetEntry.insert(QStringLiteral("linear_light_results"),
                          linearResultSheetName);
        sheetEntry.insert(QStringLiteral("style"),
                          QStringLiteral("canonical-supplemental"));
        sheetEntry.insert(QStringLiteral("first_row"), 0);
        sheetEntry.insert(QStringLiteral("rows"), rows);
        sheetEntry.insert(QStringLiteral("columns"), columns);
        sheets.append(sheetEntry);
    }
    if(!writeBytes(QDir(reviewDirectory).filePath(QStringLiteral("index.csv")),
                   csv, error)) return {};
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                    QStringLiteral("ei-canonical-blend-mask-review-v5"));
    manifest.insert(QStringLiteral("meaning"),
        QStringLiteral("White is the requested brush material; black is support. "
                       "Every cell is the effective runtime ownership after the reviewed source mask, narrow ownership remap, and shared-edge continuity profile."));
    manifest.insert(QStringLiteral("comparison_assets"),
        QStringLiteral("Each indexed cell includes full requested/support uniform tiles, "
                       "a threshold heatmap, the runtime encoded-RGB result, and a review-only linear-light result."));
    manifest.insert(QStringLiteral("material_count"), materialIds.size());
    manifest.insert(QStringLiteral("selected_masks"), totalMasks);
    manifest.insert(QStringLiteral("missing_masks"), missingMasks);
    manifest.insert(QStringLiteral("mask_family_topology_count"),
                    familyTopologyCounts.size());
    manifest.insert(QStringLiteral("target_examples_per_mask"),
                    targetExamplesPerMask);
    manifest.insert(QStringLiteral("example_selection_policy"),
                    QStringLiteral("unique-genuine-up-to-target-no-padding"));
    manifest.insert(QStringLiteral("limited_example_groups"),
                    limitedExampleGroups);
    manifest.insert(QStringLiteral("columns_per_support"), topologies.size());
    manifest.insert(QStringLiteral("support_scenarios"),
                    QJsonArray{QStringLiteral("cross-style"),
                               QStringLiteral("same-style")});
    manifest.insert(QStringLiteral("sheets"), sheets);
    manifest.insert(QStringLiteral("visual_review_status"),
                    QStringLiteral("pending-human-review"));
    if(!writeBytes(QDir(reviewDirectory).filePath(QStringLiteral("manifest.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented), error))
        return {};
    return manifest;
}

QJsonObject buildMaskReviewOnly(const Atlas& sourceAtlas,
                                StockAssets* stockAssets,
                                const QString& outputDirectory,
                                QString* error)
{
    CValidationReport report;
    const QVector<STerrainGlobalMaterialFamily> materials =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    QVector<qint32> ids;
    QMap<qint32, QString> names;
    QMap<qint32, ETerrainBlendStyle> styles;
    QMap<qint32, qint32> runtimeTypes;
    for(const STerrainGlobalMaterialFamily& material : materials)
        if(material.layer == ETerrainAtlasLayer::Land)
        {
            ids.append(material.id);
            names.insert(material.id, material.name);
            styles.insert(material.id,
                CTerrainVirtualAtlas::blendStyleForMaterialName(material.name));
            if(material.runtimeTerrainType >= 0
               && material.runtimeTerrainType < 16)
                runtimeTypes.insert(material.id, material.runtimeTerrainType);
        }
    std::sort(ids.begin(), ids.end(), [&names](qint32 left, qint32 right) {
        return names.value(left).localeAwareCompare(names.value(right)) < 0;
    });
    QJsonArray sourceManifest;
    const QMap<qint32, QImage> externalSources = globalSources(
        materials, stockAssets, sourceAtlas.tiles.front().size(),
        &sourceManifest, error);
    if(externalSources.size() != ids.size()) return {};
    const CTerrainTransitionCatalog catalog =
        CTerrainAtlasPresetCatalog::globalizedCatalog(
            sourceAtlas.profile, sourceAtlas.analysis.landCatalog);
    QVector<qint32> sourceTypes(sourceAtlas.tiles.size(), 0);
    CTerrainVirtualAtlas atlas(catalog, sourceAtlas.tiles, 0x4a4d5631u,
        externalSources, styles, true, sourceTypes, runtimeTypes);
    if(!atlas.isValid())
    {
        if(error) *error = atlas.error();
        return {};
    }
    QJsonObject manifest = writeBlendMaskReview(
        atlas, ids, names, externalSources, outputDirectory, error);
    if(manifest.isEmpty()) return {};
    manifest.insert(QStringLiteral("reviewed_pure_sources"), sourceManifest);
    const QString presetPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral(
            "../../assets/resources/terrain_atlas_presets.json"));
    const QByteArray presetHash = fileSha256(presetPath);
    if(presetHash.isEmpty())
    {
        if(error) *error = QStringLiteral(
            "Could not hash semantic preset for mask-review provenance: %1")
            .arg(QDir::cleanPath(presetPath));
        return {};
    }
    manifest.insert(QStringLiteral("semantic_preset_sha256"),
                    QString::fromLatin1(presetHash));
    if(!writeBytes(QDir(outputDirectory).filePath(
            QStringLiteral("blend-mask-review/manifest.json")),
            QJsonDocument(manifest).toJson(QJsonDocument::Indented), error))
        return {};
    return manifest;
}

QString junctionTopologyName(const STerrainTransitionSignature& signature,
                             qint32 owner)
{
    const QVector<qint32> values = {
        signature.northWest, signature.northEast,
        signature.southEast, signature.southWest};
    bool corners[4] = {false, false, false, false};
    int count = 0;
    for(int index = 0; index < 4; ++index)
    {
        corners[index] = values.at(index) == owner;
        count += corners[index];
    }
    if(count == 1) return QStringLiteral("outer-1of4-v1");
    if(count == 3) return QStringLiteral("inside-3of4-v1");
    if(count == 2 && corners[0] == corners[2]
       && corners[1] == corners[3])
        return QStringLiteral("diagonal-2of4");
    if(count == 2) return QStringLiteral("edge-2of4-v1");
    return QStringLiteral("invalid");
}

QJsonObject issueSignatureJson(const STerrainTransitionSignature& signature,
                               const QMap<qint32, QString>& names)
{
    QJsonObject result;
    const auto corner = [&names](qint32 id) {
        QJsonObject value;
        value.insert(QStringLiteral("material_id"), id);
        value.insert(QStringLiteral("material"), names.value(
            id, QStringLiteral("Unknown material %1").arg(id)));
        return value;
    };
    result.insert(QStringLiteral("northWest"), corner(signature.northWest));
    result.insert(QStringLiteral("northEast"), corner(signature.northEast));
    result.insert(QStringLiteral("southEast"), corner(signature.southEast));
    result.insert(QStringLiteral("southWest"), corner(signature.southWest));
    return result;
}

QJsonObject buildIssueJunctionMaskReview(
    const Atlas& sourceAtlas, StockAssets* stockAssets,
    const QString& issueReportPath, const QString& outputDirectory,
    QString* error)
{
    QFile reportFile(issueReportPath);
    if(!reportFile.open(QIODevice::ReadOnly))
    {
        if(error) *error = QStringLiteral("Could not read issue report %1")
            .arg(issueReportPath);
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument reportDocument = QJsonDocument::fromJson(
        reportFile.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError
       || !reportDocument.isObject())
    {
        if(error) *error = QStringLiteral("Invalid issue report %1: %2")
            .arg(issueReportPath, parseError.errorString());
        return {};
    }

    CValidationReport report;
    const QVector<STerrainGlobalMaterialFamily> materials =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    QMap<qint32, QString> names;
    QMap<qint32, ETerrainBlendStyle> styles;
    QMap<qint32, qint32> runtimeTypes;
    for(const STerrainGlobalMaterialFamily& material : materials)
        if(material.layer == ETerrainAtlasLayer::Land)
        {
            names.insert(material.id, material.name);
            styles.insert(material.id,
                CTerrainVirtualAtlas::blendStyleForMaterialName(material.name));
            if(material.runtimeTerrainType >= 0
               && material.runtimeTerrainType < 16)
                runtimeTypes.insert(material.id, material.runtimeTerrainType);
        }
    QJsonArray sourceManifest;
    const QMap<qint32, QImage> externalSources = globalSources(
        materials, stockAssets, sourceAtlas.tiles.front().size(),
        &sourceManifest, error);
    if(externalSources.size() != names.size()) return {};
    const CTerrainTransitionCatalog catalog =
        CTerrainAtlasPresetCatalog::globalizedCatalog(
            sourceAtlas.profile, sourceAtlas.analysis.landCatalog);
    QVector<qint32> sourceTypes(sourceAtlas.tiles.size(), 0);
    CTerrainVirtualAtlas atlas(catalog, sourceAtlas.tiles, 0x4a4d5631u,
        externalSources, styles, true, sourceTypes, runtimeTypes);
    if(!atlas.isValid())
    {
        if(error) *error = atlas.error();
        return {};
    }

    const QString imageDirectory = QDir(outputDirectory).filePath(
        QStringLiteral("images"));
    if(!QDir().mkpath(imageDirectory))
    {
        if(error) *error = QStringLiteral("Could not create %1")
            .arg(imageDirectory);
        return {};
    }
    QJsonArray examples;
    QMap<QString, QJsonObject> groups;
    QSet<QString> seenCells;
    QSet<QString> seenNoBlendCells;
    int uniformSignatureCells = 0;
    int twoMaterialCells = 0;
    const QJsonArray marks = reportDocument.object()
        .value(QStringLiteral("marks")).toArray();
    for(const QJsonValue& markValue : marks)
    {
        const QJsonObject mark = markValue.toObject();
        if(mark.value(QStringLiteral("category")).toString()
             != QStringLiteral("no-blend"))
            continue;
        QSet<QString> selected;
        for(const QJsonValue& selectedValue :
            mark.value(QStringLiteral("selectedCells")).toArray())
        {
            const QJsonObject cell = selectedValue.toObject();
            selected.insert(QStringLiteral("%1:%2")
                .arg(cell.value(QStringLiteral("x")).toInt())
                .arg(cell.value(QStringLiteral("y")).toInt()));
        }
        for(const QJsonValue& snapshotValue :
            mark.value(QStringLiteral("tileSnapshot")).toArray())
        {
            const QJsonObject snapshot = snapshotValue.toObject();
            const int cellX = snapshot.value(QStringLiteral("x")).toInt();
            const int cellY = snapshot.value(QStringLiteral("y")).toInt();
            const QString cellKey = QStringLiteral("%1:%2")
                .arg(cellX).arg(cellY);
            if(!selected.contains(cellKey) || seenCells.contains(cellKey))
                continue;
            const QJsonObject corners = snapshot
                .value(QStringLiteral("gridCorners")).toObject();
            STerrainTransitionSignature signature;
            signature.northWest = corners.value(QStringLiteral("x0y0")).toInt();
            signature.northEast = corners.value(QStringLiteral("x1y0")).toInt();
            signature.southEast = corners.value(QStringLiteral("x1y1")).toInt();
            signature.southWest = corners.value(QStringLiteral("x0y1")).toInt();
            QSet<qint32> unique;
            unique << signature.northWest << signature.northEast
                   << signature.southEast << signature.southWest;
            if(!seenNoBlendCells.contains(cellKey))
            {
                seenNoBlendCells.insert(cellKey);
                if(unique.size() == 1) ++uniformSignatureCells;
                else if(unique.size() == 2) ++twoMaterialCells;
            }
            if(unique.size() < 3) continue;
            seenCells.insert(cellKey);

            const QString exampleId = QStringLiteral("mark-%1-cell-%2-%3")
                .arg(mark.value(QStringLiteral("id")).toInt())
                .arg(cellX).arg(cellY);
            QString generatedProvenance;
            CValidationReport generatedReport;
            QImage generated = atlas.reviewGeneratedBlend(
                signature, 0, &generatedProvenance, &generatedReport);
            if(generated.isNull() || generatedReport.hasErrors())
            {
                if(error) *error = QStringLiteral(
                    "Could not reproduce generated junction %1: %2")
                    .arg(exampleId, generatedReport.toText());
                return {};
            }
            // Questionnaires use visual NW NE / SW SE. Cross the physical
            // atlas/engine-grid boundary exactly once for every displayed
            // runtime image and ownership mask.
            generated = generated.mirrored(false, true);
            const QString generatedFile = exampleId
                + QStringLiteral("-generated.png");
            if(!generated.save(QDir(imageDirectory).filePath(generatedFile),
                               "PNG"))
            {
                if(error) *error = QStringLiteral("Could not write %1")
                    .arg(generatedFile);
                return {};
            }

            QJsonArray components;
            QList<qint32> ordered = unique.values();
            std::sort(ordered.begin(), ordered.end());
            for(qint32 owner : ordered)
            {
                QString maskProvenance;
                QImage mask = atlas.reviewGeneratedJunctionOwnershipMask(
                    signature, owner, 0, &maskProvenance);
                if(mask.isNull())
                {
                    if(error) *error = QStringLiteral(
                        "Could not reproduce junction ownership for %1/%2")
                        .arg(exampleId).arg(owner);
                    return {};
                }
                mask = mask.mirrored(false, true);
                const QString style = CTerrainVirtualAtlas::blendStyleName(
                    styles.value(owner, ETerrainBlendStyle::Organic));
                const QString topology = junctionTopologyName(signature, owner);
                const QString groupKey = QStringLiteral("%1-%2")
                    .arg(style, topology);
                const QString componentFile = QStringLiteral("%1-%2.png")
                    .arg(exampleId, slug(names.value(owner)));
                if(!mask.save(QDir(imageDirectory).filePath(componentFile),
                              "PNG"))
                {
                    if(error) *error = QStringLiteral("Could not write %1")
                        .arg(componentFile);
                    return {};
                }
                const QString uniformFile = QStringLiteral("uniform-%1.png")
                    .arg(owner);
                const QString uniformPath = QDir(imageDirectory)
                    .filePath(uniformFile);
                if(!QFileInfo::exists(uniformPath)
                   && !externalSources.value(owner).save(uniformPath, "PNG"))
                {
                    if(error) *error = QStringLiteral("Could not write %1")
                        .arg(uniformFile);
                    return {};
                }
                QJsonObject component;
                component.insert(QStringLiteral("material_id"), owner);
                component.insert(QStringLiteral("material"), names.value(owner));
                component.insert(QStringLiteral("style"), style);
                component.insert(QStringLiteral("topology"), topology);
                component.insert(QStringLiteral("group_key"), groupKey);
                component.insert(QStringLiteral("mask_file"),
                    QStringLiteral("images/") + componentFile);
                component.insert(QStringLiteral("mask_sha256"),
                    QString::fromLatin1(reviewImageHash(mask)));
                component.insert(QStringLiteral("uniform_file"),
                    QStringLiteral("images/") + uniformFile);
                component.insert(QStringLiteral("provenance"), maskProvenance);
                components.append(component);

                QJsonObject group = groups.value(groupKey);
                if(group.isEmpty())
                {
                    group.insert(QStringLiteral("key"), groupKey);
                    group.insert(QStringLiteral("style"), style);
                    group.insert(QStringLiteral("topology"), topology);
                    group.insert(QStringLiteral("examples"), QJsonArray());
                }
                QJsonArray groupExamples = group
                    .value(QStringLiteral("examples")).toArray();
                QJsonObject groupExample = component;
                groupExample.insert(QStringLiteral("example_id"), exampleId);
                groupExample.insert(QStringLiteral("mark_id"),
                    mark.value(QStringLiteral("id")).toInt());
                groupExample.insert(QStringLiteral("cell_x"), cellX);
                groupExample.insert(QStringLiteral("cell_y"), cellY);
                groupExample.insert(QStringLiteral("generated_file"),
                    QStringLiteral("images/") + generatedFile);
                groupExample.insert(QStringLiteral("signature"),
                    issueSignatureJson(signature, names));
                groupExamples.append(groupExample);
                group.insert(QStringLiteral("examples"), groupExamples);
                groups.insert(groupKey, group);
            }
            QJsonObject example;
            example.insert(QStringLiteral("id"), exampleId);
            example.insert(QStringLiteral("mark_id"),
                mark.value(QStringLiteral("id")).toInt());
            example.insert(QStringLiteral("cell_x"), cellX);
            example.insert(QStringLiteral("cell_y"), cellY);
            example.insert(QStringLiteral("unique_materials"), unique.size());
            example.insert(QStringLiteral("signature"),
                issueSignatureJson(signature, names));
            example.insert(QStringLiteral("generated_file"),
                QStringLiteral("images/") + generatedFile);
            example.insert(QStringLiteral("generated_provenance"),
                generatedProvenance);
            example.insert(QStringLiteral("components"), components);
            examples.append(example);
        }
    }
    QJsonArray groupArray;
    for(auto group = groups.constBegin(); group != groups.constEnd(); ++group)
        groupArray.append(group.value());
    const QString presetPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral(
            "../../assets/resources/terrain_atlas_presets.json"));
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                    QStringLiteral("ei-junction-mask-issue-review-v1"));
    manifest.insert(QStringLiteral("issue_report"),
                    QFileInfo(issueReportPath).fileName());
    manifest.insert(QStringLiteral("issue_report_sha256"),
                    QString::fromLatin1(fileSha256(issueReportPath)));
    manifest.insert(QStringLiteral("semantic_preset_sha256"),
                    QString::fromLatin1(fileSha256(presetPath)));
    manifest.insert(QStringLiteral("display_corner_order"),
                    QStringLiteral("NW,NE,SW,SE"));
    manifest.insert(QStringLiteral("runtime_corner_order"),
                    QStringLiteral("NW,NE,SE,SW"));
    manifest.insert(QStringLiteral("physical_preview_mirrored_y_for_display"),
                    true);
    manifest.insert(QStringLiteral("selected_three_four_material_cells"),
                    examples.size());
    manifest.insert(QStringLiteral("selected_uniform_signature_cells"),
                    uniformSignatureCells);
    manifest.insert(QStringLiteral("selected_two_material_cells"),
                    twoMaterialCells);
    manifest.insert(QStringLiteral("mask_groups"), groupArray);
    manifest.insert(QStringLiteral("examples"), examples);
    manifest.insert(QStringLiteral("reviewed_pure_sources"), sourceManifest);
    if(!writeBytes(QDir(outputDirectory).filePath(QStringLiteral("manifest.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented),
                   error))
        return {};
    return manifest;
}

QJsonObject buildAuthoredMaskInventory(const Atlas& sourceAtlas,
                                       StockAssets* stockAssets,
                                       const QString& outputDirectory,
                                       QString* error)
{
    if(!stockAssets || sourceAtlas.tiles.isEmpty()) return {};
    CValidationReport report;
    const QVector<STerrainGlobalMaterialFamily> materials =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
    QMap<qint32, STerrainGlobalMaterialFamily> families;
    for(const STerrainGlobalMaterialFamily& material : materials)
        if(material.layer == ETerrainAtlasLayer::Land)
            families.insert(material.id, material);
    const QString imageDirectory = QDir(outputDirectory).filePath(
        QStringLiteral("images"));
    if(!QDir().mkpath(imageDirectory))
    {
        if(error) *error = QStringLiteral("Could not create %1")
            .arg(imageDirectory);
        return {};
    }
    QMap<QByteArray, QString> imageFiles;
    const auto saveImage = [&](const QImage& source, QString* relative) {
        const QImage image = source.convertToFormat(QImage::Format_RGB888);
        const QByteArray hash = reviewRgbImageHash(image);
        if(hash.isEmpty()) return false;
        if(!imageFiles.contains(hash))
        {
            const QString name = QString::fromLatin1(hash) + QStringLiteral(".png");
            if(!image.save(QDir(imageDirectory).filePath(name), "PNG"))
                return false;
            imageFiles.insert(hash, QStringLiteral("images/") + name);
        }
        if(relative) *relative = imageFiles.value(hash);
        return true;
    };
    const auto sameAtlasUniform = [&](qint32 materialId,
                                      const STerrainGlobalTransitionSource& transition,
                                      const Atlas& transitionAtlas,
                                      int* slot) -> QImage {
        const auto found = families.constFind(materialId);
        if(found == families.constEnd()) return {};
        for(const STerrainGlobalMaterialSource& source : found->sources)
            if(source.atlasIdentity == transition.atlasIdentity
               && source.mprNamespace.compare(
                    transition.mprNamespace, Qt::CaseInsensitive) == 0
               && source.uniformSlot >= 0
               && source.uniformSlot < transitionAtlas.tiles.size())
            {
                if(slot) *slot = source.uniformSlot;
                return transitionAtlas.tiles.at(source.uniformSlot);
            }
        return {};
    };
    QJsonArray candidates;
    int skippedNonPair = 0;
    int skippedMissingUniform = 0;
    int skippedWrongSize = 0;
    for(const STerrainGlobalTransitionSource& transition :
            CTerrainAtlasPresetCatalog::globalAuthoredTransitions(&report))
    {
        if(transition.layer != ETerrainAtlasLayer::Land) continue;
        const QVector<qint32> cornerIds = {
            transition.signature.northWest,
            transition.signature.northEast,
            transition.signature.southEast,
            transition.signature.southWest
        };
        QSet<qint32> uniqueSet;
        for(qint32 id : cornerIds) uniqueSet.insert(id);
        if(uniqueSet.size() != 2)
        {
            ++skippedNonPair;
            continue;
        }
        const Atlas atlas = stockAssets->atlas(
            transition.mprNamespace, transition.textureCount * 64, nullptr);
        if(!atlas.valid || transition.slot < 0
           || transition.slot >= atlas.tiles.size()
           || atlas.tiles.at(transition.slot).size()
                != sourceAtlas.tiles.front().size())
        {
            ++skippedWrongSize;
            continue;
        }
        QList<qint32> unique = uniqueSet.values();
        std::sort(unique.begin(), unique.end());
        int firstSlot = -1;
        int secondSlot = -1;
        const QImage firstUniform = sameAtlasUniform(
            unique.at(0), transition, atlas, &firstSlot);
        const QImage secondUniform = sameAtlasUniform(
            unique.at(1), transition, atlas, &secondSlot);
        if(firstUniform.isNull() || secondUniform.isNull())
        {
            ++skippedMissingUniform;
            continue;
        }
        QString transitionFile;
        QString firstUniformFile;
        QString secondUniformFile;
        if(!saveImage(atlas.tiles.at(transition.slot), &transitionFile)
           || !saveImage(firstUniform, &firstUniformFile)
           || !saveImage(secondUniform, &secondUniformFile))
        {
            if(error) *error = QStringLiteral(
                "Could not write authored-mask inventory images");
            return {};
        }
        for(qint32 owner : unique)
        {
            const qint32 support = owner == unique.at(0)
                ? unique.at(1) : unique.at(0);
            int ownerCount = 0;
            for(qint32 id : cornerIds) ownerCount += id == owner;
            const bool diagonal = ownerCount == 2
                && cornerIds.at(0) == cornerIds.at(2)
                && cornerIds.at(1) == cornerIds.at(3);
            const QString topology = ownerCount == 1
                ? QStringLiteral("outer-1of4")
                : ownerCount == 3 ? QStringLiteral("inside-3of4")
                : diagonal ? QStringLiteral("diagonal-2of4")
                           : QStringLiteral("edge-2of4");
            const STerrainGlobalMaterialFamily ownerFamily =
                families.value(owner);
            const STerrainGlobalMaterialFamily supportFamily =
                families.value(support);
            QJsonObject item;
            item.insert(QStringLiteral("source"), transition.mprNamespace
                + QLatin1Char(':') + QString::number(transition.slot));
            item.insert(QStringLiteral("source_namespace"),
                        transition.mprNamespace);
            item.insert(QStringLiteral("source_slot"), transition.slot);
            item.insert(QStringLiteral("source_atlas_sha256"),
                        QString::fromLatin1(transition.atlasIdentity.toHex()));
            item.insert(QStringLiteral("transition_file"), transitionFile);
            item.insert(QStringLiteral("transition_pixel_sha256"),
                QString::fromLatin1(reviewRgbImageHash(
                    atlas.tiles.at(transition.slot))));
            item.insert(QStringLiteral("owner_material_id"), owner);
            item.insert(QStringLiteral("owner_material"), ownerFamily.name);
            item.insert(QStringLiteral("owner_style"),
                CTerrainVirtualAtlas::blendStyleName(
                    CTerrainVirtualAtlas::blendStyleForMaterialName(
                        ownerFamily.name)));
            item.insert(QStringLiteral("owner_uniform_slot"),
                        owner == unique.at(0) ? firstSlot : secondSlot);
            item.insert(QStringLiteral("owner_uniform_file"),
                        owner == unique.at(0)
                            ? firstUniformFile : secondUniformFile);
            item.insert(QStringLiteral("support_material_id"), support);
            item.insert(QStringLiteral("support_material"), supportFamily.name);
            item.insert(QStringLiteral("support_style"),
                CTerrainVirtualAtlas::blendStyleName(
                    CTerrainVirtualAtlas::blendStyleForMaterialName(
                        supportFamily.name)));
            item.insert(QStringLiteral("support_uniform_slot"),
                        support == unique.at(0) ? firstSlot : secondSlot);
            item.insert(QStringLiteral("support_uniform_file"),
                        support == unique.at(0)
                            ? firstUniformFile : secondUniformFile);
            item.insert(QStringLiteral("topology"), topology);
            item.insert(QStringLiteral("owner_corner_count"), ownerCount);
            item.insert(QStringLiteral("diagonal"), diagonal);
            item.insert(QStringLiteral("signature_corner_order"),
                        QStringLiteral("NW,NE,SE,SW"));
            QJsonArray signature;
            QJsonArray ownerCorners;
            for(qint32 id : cornerIds)
            {
                signature.append(id);
                ownerCorners.append(id == owner);
            }
            item.insert(QStringLiteral("grid_signature"), signature);
            item.insert(QStringLiteral("owner_corners"), ownerCorners);
            candidates.append(item);
        }
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                    QStringLiteral("ei-authored-mask-source-inventory-v1"));
    manifest.insert(QStringLiteral("semantic_preset_sha256"),
        QString::fromLatin1(fileSha256(QDir(
            QCoreApplication::applicationDirPath()).absoluteFilePath(
                QStringLiteral("../../assets/resources/terrain_atlas_presets.json")))));
    manifest.insert(QStringLiteral("candidate_polarities"), candidates.size());
    manifest.insert(QStringLiteral("unique_images"), imageFiles.size());
    manifest.insert(QStringLiteral("skipped_non_pair"), skippedNonPair);
    manifest.insert(QStringLiteral("skipped_missing_same_atlas_uniform"),
                    skippedMissingUniform);
    manifest.insert(QStringLiteral("skipped_wrong_size_or_unreviewed_atlas"),
                    skippedWrongSize);
    manifest.insert(QStringLiteral("candidates"), candidates);
    if(!writeBytes(QDir(outputDirectory).filePath(
            QStringLiteral("authored-mask-source-inventory.json")),
        QJsonDocument(manifest).toJson(QJsonDocument::Indented), error))
        return {};
    return manifest;
}

struct BuiltMap
{
    bool success{false};
    QString error;
    QJsonObject manifest;
};

BuiltMap buildMap(const QString& sourcePath, const QString& nameSpace,
                  const QVector<qint32>& stampIds,
                  const QMap<qint32, QString>& names,
                  const QMap<qint32, QImage>& externalSources,
                  const Atlas& sourceAtlas, StockAssets* stockAssets,
                  const QString& outputDirectory, bool stripeLayout = false,
                  bool circleLayout = false)
{
    BuiltMap result;
    QFile sourceFile(sourcePath);
    if(!sourceFile.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Cannot open source MPR: %1").arg(sourcePath);
        return result;
    }
    const QByteArray sourceBytes = sourceFile.readAll();
    CValidationReport report;
    CMprDocument document;
    CTerrainEditModel terrain;
    if(!document.loadBytes(sourceBytes, sourcePath, &report)
       || !terrain.load(document, &report))
    {
        result.error = report.toText();
        return result;
    }
    const CTerrainTransitionCatalog catalog =
        CTerrainAtlasPresetCatalog::globalizedCatalog(
            sourceAtlas.profile, sourceAtlas.analysis.landCatalog);
    if(stampIds.isEmpty())
    {
        result.error = QStringLiteral("No paintable material identities were selected.");
        return result;
    }
    CTerrainVirtualMaterialField field;
    const qint32 base = stampIds.front();
    if(!field.resize(terrain.tileCountX(), terrain.tileCountY(), base))
    {
        result.error = QStringLiteral("The virtual field dimensions are invalid.");
        return result;
    }
    QVector<qint32> paintedMaterial(
        field.tileWidth() * field.tileHeight(), base);
    const auto markPaintedCells = [&](int vertexX, int vertexY,
                                      int vertexWidth, int vertexHeight,
                                      qint32 material) {
        for(int y = qMax(0, vertexY - 1);
            y <= qMin(field.tileHeight() - 1, vertexY + vertexHeight - 1); ++y)
            for(int x = qMax(0, vertexX - 1);
                x <= qMin(field.tileWidth() - 1, vertexX + vertexWidth - 1); ++x)
                paintedMaterial[y * field.tileWidth() + x] = material;
    };
    const int spacing = circleLayout ? 8 : 5;
    const int margin = 2;
    const int columns = qMax(1, (field.vertexWidth() - margin * 2) / spacing);
    const int stripeHeight = 12;
    const int rowSpacing = stripeLayout ? stripeHeight + 3 : spacing;
    const int rows = qMax(1,
        (field.vertexHeight() - margin * 2) / rowSpacing);
    if(columns * rows < stampIds.size())
    {
        result.error = QStringLiteral(
            "The source map cannot hold %1 non-overlapping 3x3 family stamps; capacity is %2.")
            .arg(stampIds.size()).arg(columns * rows);
        return result;
    }
    QJsonArray stamps;
    for(int index = 0; index < stampIds.size(); ++index)
    {
        const int x0 = margin + (index % columns) * spacing;
        const int y0 = margin + (index / columns) * rowSpacing;
        const int height = stripeLayout ? stripeHeight
            : (circleLayout ? 7 : 3);
        const int width = circleLayout ? 7 : 3;
        if(circleLayout)
        {
            // Every brush, including the map's base material, receives a real
            // outer edge. The base brush gets a local patch of the second
            // material first so it is not accidentally tested as a no-op.
            if(stampIds.at(index) == base && stampIds.size() > 1)
                for(int y = y0; y < y0 + 7; ++y)
                    for(int x = x0; x < x0 + 7; ++x)
                    {
                        field.setMaterial(x, y, stampIds.at(1));
                        markPaintedCells(x, y, 1, 1, stampIds.at(1));
                    }
            STerrainTileBrush brush;
            brush.centerX = float(x0 + 3);
            brush.centerY = float(y0 + 3);
            brush.radius = 3.0f;
            brush.shape = ETerrainTileBrushShape::Circle;
            field.paint(brush, stampIds.at(index));
            for(int y = y0; y < y0 + 7; ++y)
                for(int x = x0; x < x0 + 7; ++x)
                {
                    const int dx=x-(x0+3),dy=y-(y0+3);
                    if(dx*dx+dy*dy<=9)
                        markPaintedCells(x, y, 1, 1, stampIds.at(index));
                    if(dx*dx+dy*dy<=9
                       &&field.material(x,y)!=stampIds.at(index))
                    {
                        result.error=QStringLiteral(
                            "Circular brush %1 dropped a mandatory interior vertex.")
                            .arg(names.value(stampIds.at(index)));
                        return result;
                    }
                }
        }
        else
        {
            for(int y = y0; y < y0 + height; ++y)
                for(int x = x0; x < x0 + width; ++x)
                    field.setMaterial(x, y, stampIds.at(index));
            markPaintedCells(x0, y0, width, height, stampIds.at(index));
        }
        QJsonObject stamp;
        stamp.insert(QStringLiteral("material_id"), stampIds.at(index));
        stamp.insert(QStringLiteral("material"), names.value(stampIds.at(index)));
        stamp.insert(QStringLiteral("blend_style"),
            CTerrainVirtualAtlas::blendStyleName(
                CTerrainVirtualAtlas::blendStyleForMaterialName(
                    names.value(stampIds.at(index)))));
        stamp.insert(QStringLiteral("x0"), x0);
        stamp.insert(QStringLiteral("y0"), y0);
        stamp.insert(QStringLiteral("width_vertices"), width);
        stamp.insert(QStringLiteral("height_vertices"), height);
        stamp.insert(QStringLiteral("shape"),circleLayout
            ?QStringLiteral("circle-radius-3")
            :(stripeLayout?QStringLiteral("vertical-stripe")
                         :QStringLiteral("square")));
        stamps.append(stamp);
    }
    // Force genuine four-family junctions in otherwise unused lower rows.
    QJsonArray fourFamilyJunctions;
    const int junctionY = margin + rows * rowSpacing;
    if(junctionY + 1 < field.vertexHeight())
        for(int group = 0; group + 3 < stampIds.size(); group += 4)
        {
            const int x = margin + (group / 4) * 3;
            if(x + 1 >= field.vertexWidth()) break;
            field.setMaterial(x, junctionY, stampIds.at(group));
            markPaintedCells(x, junctionY, 1, 1, stampIds.at(group));
            field.setMaterial(x + 1, junctionY, stampIds.at(group + 1));
            markPaintedCells(x + 1, junctionY, 1, 1,
                             stampIds.at(group + 1));
            field.setMaterial(x + 1, junctionY + 1, stampIds.at(group + 2));
            markPaintedCells(x + 1, junctionY + 1, 1, 1,
                             stampIds.at(group + 2));
            field.setMaterial(x, junctionY + 1, stampIds.at(group + 3));
            markPaintedCells(x, junctionY + 1, 1, 1,
                             stampIds.at(group + 3));
            QJsonObject junction;
            junction.insert(QStringLiteral("cell_x"), x);
            junction.insert(QStringLiteral("cell_y"), junctionY);
            fourFamilyJunctions.append(junction);
        }

    QMap<qint32, ETerrainBlendStyle> blendStyles;
    for(auto it = names.constBegin(); it != names.constEnd(); ++it)
        blendStyles.insert(it.key(),
            CTerrainVirtualAtlas::blendStyleForMaterialName(it.value()));
    QMap<qint32, qint32> runtimeTypes;
    for(const STerrainGlobalMaterialFamily& family :
            CTerrainAtlasPresetCatalog::globalUniformFamilies(&report))
        if(family.runtimeTerrainType >= 0 && family.runtimeTerrainType < 16)
            runtimeTypes.insert(family.id, family.runtimeTerrainType);
    CTerrainVirtualAtlas atlas(catalog, sourceAtlas.tiles, 0x4a4d5631u,
                               externalSources, blendStyles, true,
                               document.mapInfo().tileTypes, runtimeTypes);
    int indexedStockTransitions = 0;
    if(stockAssets)
        for(const STerrainGlobalTransitionSource& transition :
                CTerrainAtlasPresetCatalog::globalAuthoredTransitions(&report))
        {
            if(transition.layer != ETerrainAtlasLayer::Land)
                continue;
            const Atlas stock = stockAssets->atlas(
                transition.mprNamespace, transition.textureCount * 64,
                nullptr);
            if(!stock.valid || transition.slot < 0
               || transition.slot >= stock.tiles.size()
               || stock.tiles.at(transition.slot).size()
                    != sourceAtlas.tiles.front().size())
                continue;
            const QString sourceLabel = transition.mprNamespace
                + QStringLiteral(":") + QString::number(transition.slot);
            if(atlas.addAuthoredLibraryTransition(
                   transition.signature, stock.tiles.at(transition.slot),
                   sourceLabel, &report))
            {
                ++indexedStockTransitions;
            }
        }
    const QVector<quint16> pinned = pinnedOverlay(terrain);
    QVector<qint32> preferredCellTypes;
    preferredCellTypes.reserve(paintedMaterial.size());
    for(qint32 material : paintedMaterial)
        preferredCellTypes.append(runtimeTypes.value(material, -1));
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, nameSpace, 16384, document.mapInfo().tileTypes, &report, pinned,
        preferredCellTypes, {}, paintedMaterial);
    if(!finalized.success)
    {
        result.error = finalized.error + QLatin1Char('\n') + report.toText();
        return result;
    }
    int unresolved = 0;
    QStringList unresolvedDetails;
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool found = false;
            const STerrainTransitionSignature actual = finalizedSignature(
                finalized, finalized.packedCells.at(y * field.tileWidth() + x),
                &found);
            if(!found || !sameSignature(actual, field.cellSignature(x, y)))
            {
                ++unresolved;
                if(unresolvedDetails.size() < 24)
                {
                    const STerrainTransitionSignature expected =
                        field.cellSignature(x, y);
                    unresolvedDetails << QStringLiteral(
                        "(%1,%2) expected %3/%4/%5/%6 got %7/%8/%9/%10")
                        .arg(x).arg(y)
                        .arg(expected.northWest).arg(expected.northEast)
                        .arg(expected.southEast).arg(expected.southWest)
                        .arg(actual.northWest).arg(actual.northEast)
                        .arg(actual.southEast).arg(actual.southWest);
                }
            }
        }
    if(unresolved != 0)
    {
        result.error = QStringLiteral("Finalized map contains %1 unresolved cells.\n%2\n%3")
            .arg(unresolved).arg(unresolvedDetails.join(QLatin1Char('\n')),
                                 report.toText());
        return result;
    }
    int actualReviewedMaskTiles = 0;
    for(const STerrainFinalizedTile& tile : finalized.tiles)
        actualReviewedMaskTiles += !tile.authored
            && tile.blendProvenance.contains(QStringLiteral("-approved"));
    if((stripeLayout || circleLayout) && actualReviewedMaskTiles == 0)
    {
        result.error = QStringLiteral(
            "The finalized material test map contains no generated tile with "
            "reviewed mask provenance.");
        return result;
    }
    int unapprovedTwoFamilyNonJunctionCells = 0;
    for(int cellIndex = 0; cellIndex < finalized.packedCells.size(); ++cellIndex)
    {
        const int physicalSlot = SMprPackedTile::decode(
            finalized.packedCells.at(cellIndex)).atlasSlot();
        if(physicalSlot < 0 || physicalSlot >= finalized.tiles.size())
            continue;
        const STerrainFinalizedTile& tile = finalized.tiles.at(physicalSlot);
        QSet<qint32> materials;
        materials.insert(tile.canonicalSignature.northWest);
        materials.insert(tile.canonicalSignature.northEast);
        materials.insert(tile.canonicalSignature.southEast);
        materials.insert(tile.canonicalSignature.southWest);
        if(tile.authored || materials.size() != 2
           || tile.blendProvenance.contains(QStringLiteral("-approved")))
            continue;
        const int x = cellIndex % field.tileWidth();
        const int y = cellIndex / field.tileWidth();
        bool belongsToExplicitJunction = false;
        for(const QJsonValue& value : fourFamilyJunctions)
        {
            const QJsonObject junction = value.toObject();
            belongsToExplicitJunction |= qAbs(
                x - junction.value(QStringLiteral("cell_x")).toInt()) <= 1
                && qAbs(y - junction.value(
                    QStringLiteral("cell_y")).toInt()) <= 1;
        }
        unapprovedTwoFamilyNonJunctionCells += !belongsToExplicitJunction;
    }
    if((stripeLayout || circleLayout)
       && unapprovedTwoFamilyNonJunctionCells != 0)
    {
        result.error = QStringLiteral(
            "The finalized material test map contains %1 ordinary two-family "
            "cell(s) using unapproved mask provenance.")
            .arg(unapprovedTwoFamilyNonJunctionCells);
        return result;
    }
    const STerrainPortableAtlasPackage package =
        CTerrainVirtualAtlas::buildPortablePackage(
            document, finalized, ETerrainTileLayer::Land, &report);
    if(!package.success)
    {
        result.error = package.error + QLatin1Char('\n') + report.toText();
        return result;
    }
    if(!QDir().mkpath(outputDirectory))
    {
        result.error = QStringLiteral("Could not create %1").arg(outputDirectory);
        return result;
    }
    const QJsonObject maskReview = writeBlendMaskReview(
        atlas, stampIds, names, externalSources, outputDirectory,
        &result.error);
    if(maskReview.isEmpty()) return result;
    QByteArray materialIndex(
        "material_id,material,blend_style,x0,y0,width_vertices,height_vertices,shape\n");
    for(const QJsonValue& value : stamps)
    {
        const QJsonObject stamp = value.toObject();
        const auto csv = [](QString text) {
            text.replace('"', QStringLiteral("\"\""));
            return QByteArray("\"") + text.toUtf8() + QByteArray("\"");
        };
        materialIndex += QByteArray::number(stamp.value("material_id").toInt()) + ','
            + csv(stamp.value("material").toString()) + ','
            + csv(stamp.value("blend_style").toString()) + ','
            + QByteArray::number(stamp.value("x0").toInt()) + ','
            + QByteArray::number(stamp.value("y0").toInt()) + ','
            + QByteArray::number(stamp.value("width_vertices").toInt()) + ','
            + QByteArray::number(stamp.value("height_vertices").toInt()) + ','
            + csv(stamp.value("shape").toString()) + '\n';
    }
    if(!writeBytes(QDir(outputDirectory).filePath(
           QStringLiteral("material-visual-review-index.csv")),
           materialIndex, &result.error)) return result;
    const QString baseName = finalized.isolatedNamespace;
    if(!writeBytes(QDir(outputDirectory).filePath(baseName + QStringLiteral(".mpr")),
                   package.mprBytes, &result.error)
       || !writeBytes(QDir(outputDirectory).filePath(
                         baseName + QStringLiteral("-atlas.res")),
                     package.resourceArchiveBytes, &result.error))
        return result;

    // Ship the semantic identity beside every generated visual-test MPR. The
    // profile binds to the post-DXT physical pages (including their padding),
    // exactly as a normal reopen sees them; logical active-tile hashes cannot
    // bind a page-backed MPR whose slot count is rounded to 64.
    QVector<QImage> physicalTiles;
    CValidationReport portableProfileReport;
    if(!CTerrainVirtualAtlas::decodePhysicalTileImages(
           finalized, &physicalTiles, &portableProfileReport))
    {
        result.error = portableProfileReport.toText();
        return result;
    }
    const STerrainAtlasProfile portableProfile =
        CTerrainAtlasProfileStore::portableFinalizedProfile(
            finalized, physicalTiles, sourceAtlas.profile,
            ETerrainAtlasLayer::Land, names, &portableProfileReport);
    if(portableProfile.assignedSlotCount() <= 0
       || !CTerrainAtlasProfileStore::save(
           portableProfile, outputDirectory, &portableProfileReport))
    {
        result.error = QStringLiteral(
            "Could not write the generated map's physical semantic profile.\n%1")
            .arg(portableProfileReport.toText());
        return result;
    }
    STerrainAtlasProfile reopenedProfile;
    if(!CTerrainAtlasProfileStore::loadExplicitOverride(
           baseName, portableProfile.atlasPixelSha256, physicalTiles.size(),
           &reopenedProfile, outputDirectory, nullptr, &portableProfileReport)
       || reopenedProfile.assignedSlotCount()
            != portableProfile.assignedSlotCount())
    {
        result.error = QStringLiteral(
            "The generated map's semantic profile failed exact reopen.\n%1")
            .arg(portableProfileReport.toText());
        return result;
    }

    QVector<int> generatedSlots;
    for(int slot = 0; slot < finalized.tiles.size(); ++slot)
        if(!finalized.tiles.at(slot).authored) generatedSlots.append(slot);
    if(!generatedSlots.isEmpty())
    {
        const int tileSize = finalized.tiles.front().pixels.width();
        const int columns = 8;
        const int rows = (generatedSlots.size() + columns - 1) / columns;
        QImage review(columns * tileSize, rows * tileSize,
                      QImage::Format_RGBA8888);
        review.fill(QColor(32, 32, 32));
        for(int index = 0; index < generatedSlots.size(); ++index)
        {
            const QImage tile = finalized.tiles.at(generatedSlots.at(index))
                .pixels.convertToFormat(QImage::Format_RGBA8888);
            const int destinationX = (index % columns) * tileSize;
            const int destinationY = (index / columns) * tileSize;
            for(int y = 0; y < tileSize; ++y)
                memcpy(review.scanLine(destinationY + y) + destinationX * 4,
                       tile.constScanLine(y), size_t(tileSize) * 4u);
        }
        if(!review.save(QDir(outputDirectory).filePath(
                QStringLiteral("generated-blends-review.png")), "PNG"))
        {
            result.error = QStringLiteral(
                "Could not write the native-resolution generated blend review sheet.");
            return result;
        }
    }

    int authored = 0;
    int importedStock = 0;
    QJsonArray generatedBlendSources;
    QJsonArray physicalTileIdentities;
    for(int physicalSlot = 0; physicalSlot < finalized.tiles.size(); ++physicalSlot)
    {
        const STerrainFinalizedTile& tile = finalized.tiles.at(physicalSlot);
        authored += tile.authored;
        importedStock += tile.authored && tile.sourceSlot < 0;
        QJsonObject identity;
        identity.insert(QStringLiteral("physical_slot"), physicalSlot);
        identity.insert(QStringLiteral("kind"), tile.authored
            ? (tile.sourceSlot >= 0 ? QStringLiteral("source-authored")
                                    : QStringLiteral("imported-stock"))
            : QStringLiteral("generated-exact"));
        identity.insert(QStringLiteral("source_namespace"),
                        tile.authored && tile.sourceSlot >= 0
                            ? nameSpace : QString());
        identity.insert(QStringLiteral("source_slot"), tile.sourceSlot);
        identity.insert(QStringLiteral("pixel_sha256"),
                        QString::fromLatin1(tile.pixelSha256));
        identity.insert(QStringLiteral("provenance"), tile.blendProvenance);
        identity.insert(QStringLiteral("runtime_terrain_type"),
                        tile.runtimeTerrainType);
        QJsonObject signature;
        signature.insert(QStringLiteral("northWest"),
                         tile.canonicalSignature.northWest);
        signature.insert(QStringLiteral("northEast"),
                         tile.canonicalSignature.northEast);
        signature.insert(QStringLiteral("southEast"),
                         tile.canonicalSignature.southEast);
        signature.insert(QStringLiteral("southWest"),
                         tile.canonicalSignature.southWest);
        identity.insert(QStringLiteral("canonical_signature"), signature);
        physicalTileIdentities.append(identity);
        if(!tile.authored)
        {
            QJsonObject source;
            source.insert(QStringLiteral("pixel_sha256"),
                          QString::fromLatin1(tile.pixelSha256));
            source.insert(QStringLiteral("stock_derived"),
                          tile.stockDerivedBlend);
            source.insert(QStringLiteral("canonical_generated"),
                          tile.canonicalGeneratedBlend);
            source.insert(QStringLiteral("provenance"),
                          tile.blendProvenance);
            generatedBlendSources.append(source);
        }
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                    QStringLiteral("ei-modern-atlas-real-map-v1"));
    manifest.insert(QStringLiteral("source_namespace"), nameSpace);
    manifest.insert(QStringLiteral("source_mpr"), QFileInfo(sourcePath).fileName());
    manifest.insert(QStringLiteral("isolated_namespace"), baseName);
    manifest.insert(QStringLiteral("family_stamps"), stamps);
    manifest.insert(QStringLiteral("layout"), circleLayout
        ?QStringLiteral("all-material-radius-3-circles")
        :(stripeLayout?QStringLiteral("final-material-stripes")
                     :QStringLiteral("non-overlapping-3x3-stamps")));
    manifest.insert(QStringLiteral("four_family_junctions"), fourFamilyJunctions);
    manifest.insert(QStringLiteral("paintable_materials"), stampIds.size());
    manifest.insert(QStringLiteral("authored_tiles_reused"), authored);
    manifest.insert(QStringLiteral("local_authored_tiles_reused"),
                    authored - importedStock);
    manifest.insert(QStringLiteral("global_stock_blends_reused"), importedStock);
    manifest.insert(QStringLiteral("loaded_global_stock_transition_candidates"),
                    indexedStockTransitions);
    manifest.insert(QStringLiteral("generated_exact_tiles"),
                    finalized.tiles.size() - authored);
    manifest.insert(QStringLiteral("generated_blend_sources"),
                    generatedBlendSources);
    manifest.insert(QStringLiteral("physical_tile_identities"),
                    physicalTileIdentities);
    QByteArray packedCells;
    packedCells.resize(finalized.packedCells.size() * int(sizeof(quint16)));
    for(int index = 0; index < finalized.packedCells.size(); ++index)
        qToLittleEndian<quint16>(finalized.packedCells.at(index),
            reinterpret_cast<uchar*>(packedCells.data()
                + index * int(sizeof(quint16))));
    manifest.insert(QStringLiteral("packed_cell_width"), field.tileWidth());
    manifest.insert(QStringLiteral("packed_cell_height"), field.tileHeight());
    manifest.insert(QStringLiteral("packed_cells_le16_base64"),
                    QString::fromLatin1(packedCells.toBase64()));
    manifest.insert(QStringLiteral("generated_blend_review"),
                    generatedSlots.isEmpty() ? QString()
                    : QStringLiteral("generated-blends-review.png"));
    manifest.insert(QStringLiteral("generated_blend_review_columns"), 8);
    manifest.insert(QStringLiteral("blend_mask_review"),
                    QStringLiteral("blend-mask-review/manifest.json"));
    manifest.insert(QStringLiteral("blend_mask_review_selected_masks"),
                    maskReview.value(QStringLiteral("selected_masks")));
    manifest.insert(QStringLiteral("blend_mask_review_missing_masks"),
                    maskReview.value(QStringLiteral("missing_masks")));
    manifest.insert(QStringLiteral("actual_reviewed_mask_tiles"),
                    actualReviewedMaskTiles);
    manifest.insert(QStringLiteral(
                        "unapproved_two_family_non_junction_cells"),
                    unapprovedTwoFamilyNonJunctionCells);
    manifest.insert(QStringLiteral("total_active_slots"), finalized.requiredSlots);
    manifest.insert(QStringLiteral("texture_pages"), finalized.textureCount);
    manifest.insert(QStringLiteral("capacity_limit"), finalized.capacityLimit);
    manifest.insert(QStringLiteral("unresolved_cells"), unresolved);
    manifest.insert(QStringLiteral("semantic_resolution_status"),
                    QStringLiteral("pass"));
    manifest.insert(QStringLiteral("visual_review_status"),
                    QStringLiteral("pending-human-review"));
    manifest.insert(QStringLiteral("visual_review_is_automated"), false);
    manifest.insert(QStringLiteral("visual_review_index"),
                    QStringLiteral("material-visual-review-index.csv"));
    manifest.insert(QStringLiteral("visual_review_instructions"),
        QStringLiteral("Open the map in the editor and inspect every labeled material stamp. "
                       "unresolved_cells=0 proves exact corner identity only; it does not prove "
                       "that source families, masks, pixels, or rotations look correct. Use "
                       "Mark bad tiles on map and send the generated paint-issues JSON."));
    manifest.insert(QStringLiteral("stable_sha256"),
                    QString::fromLatin1(finalized.stableSha256));
    manifest.insert(QStringLiteral("mpr_file"), baseName + QStringLiteral(".mpr"));
    manifest.insert(QStringLiteral("atlas_file"),
                    baseName + QStringLiteral("-atlas.res"));
    manifest.insert(QStringLiteral("semantic_profile"),
        QDir(outputDirectory).relativeFilePath(
            CTerrainAtlasProfileStore::profilePath(baseName, outputDirectory)));
    manifest.insert(QStringLiteral("physical_slots"), physicalTiles.size());
    manifest.insert(QStringLiteral("assigned_slots"),
                    portableProfile.assignedSlotCount());
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(
        QJsonDocument::Indented);
    if(!writeBytes(QDir(outputDirectory).filePath(QStringLiteral("manifest.json")),
                   manifestBytes, &result.error)) return result;
    result.manifest = manifest;
    result.success = true;
    return result;
}

bool writeMapCollection(const QString& collectionPath,
                        const QString& mapDirectory, BuiltMap* built,
                        const QString& name, const QString& mobReference,
                        QString* error)
{
    if(!built || !built->success)
    {
        if(error) *error = QStringLiteral("Cannot create a collection for an incomplete map.");
        return false;
    }
    const QString mprName = built->manifest.value(
        QStringLiteral("mpr_file")).toString();
    const QString mprPath = QFileInfo(
        QDir(mapDirectory).filePath(mprName)).absoluteFilePath();
    if(mprName.isEmpty() || !QFileInfo::exists(mprPath))
    {
        if(error) *error = QStringLiteral("The generated collection MPR is missing: %1")
            .arg(mprPath);
        return false;
    }
    SEnduranceCollection collection;
    collection.name = name;
    collection.profileId = QStringLiteral("jamais-vu");
    collection.mprReference = QDir(
        QFileInfo(collectionPath).absolutePath()).relativeFilePath(mprPath);
    collection.mobReferences.append(mobReference);
    if(!CCollectionDocument::save(collectionPath, collection, error))
        return false;
    built->manifest.insert(QStringLiteral("collection_file"),
        QFileInfo(collectionPath).fileName());
    built->manifest.insert(QStringLiteral("collection_mob"), mobReference);
    return writeBytes(QDir(mapDirectory).filePath(QStringLiteral("manifest.json")),
        QJsonDocument(built->manifest).toJson(QJsonDocument::Indented), error);
}

BuiltMap buildExpandedRuntimeProbe(
    const QString& sourcePath, const QString& sourceMobPath,
    const QString& nameSpace, int targetPages,
    const QVector<qint32>& materialIds,
    const QMap<qint32, QImage>& externalSources,
    const Atlas& sourceAtlas, const QString& outputDirectory,
    const QString& portableProfileRoot, int targetPageSize = 0)
{
    Q_UNUSED(materialIds);
    Q_UNUSED(externalSources);
    BuiltMap result;
    QFile sourceFile(sourcePath);
    if(!sourceFile.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Cannot open source MPR: %1").arg(sourcePath);
        return result;
    }
    CValidationReport loadReport;
    CMprDocument document;
    if(!document.loadBytes(sourceFile.readAll(), sourcePath, &loadReport))
    {
        result.error = loadReport.toText();
        return result;
    }
    if(targetPages <= 0 || targetPages > 256 || sourceAtlas.tiles.isEmpty())
    {
        result.error = QStringLiteral("Expanded runtime probe parameters are invalid.");
        return result;
    }
    const int requiredSlots = targetPages * 64;
    const int cellCount = int(document.mapInfo().sectorCountX
                              * document.mapInfo().sectorCountY * 16u * 16u);
    if(cellCount < requiredSlots)
    {
        result.error = QStringLiteral(
            "The source MPR has %1 cells but the %2-page probe needs %3 references.")
            .arg(cellCount).arg(targetPages).arg(requiredSlots);
        return result;
    }
    STerrainAtlasFinalization best;
    best.capacityLimit = requiredSlots;
    best.requiredSlots = requiredSlots;
    best.textureCount = targetPages;
    const int sourceTileSize = sourceAtlas.tiles.front().width();
    if(sourceTileSize <= 0
       || sourceAtlas.tiles.front().height() != sourceTileSize
       || (targetPageSize > 0 && (targetPageSize % 8 != 0
                                 || targetPageSize > sourceTileSize * 8)))
    {
        result.error = QStringLiteral("The source atlas tile dimensions are invalid.");
        return result;
    }
    const int pageSize = targetPageSize > 0
        ? targetPageSize : sourceTileSize * 8;
    const int tileSize = pageSize / 8;
    const bool resampled = tileSize != sourceTileSize;
    QCryptographicHash stable(QCryptographicHash::Sha256);
    QCryptographicHash semanticPixels(QCryptographicHash::Sha256);
    semanticPixels.addData(QByteArray::number(requiredSlots));
    semanticPixels.addData("\n", 1);
    stable.addData(nameSpace.toLower().toUtf8());
    stable.addData(QByteArray::number(targetPages));
    stable.addData("\npage-size:", 11);
    stable.addData(QByteArray::number(pageSize));
    best.tileTypes.reserve(requiredSlots);
    for(int slot = 0; slot < requiredSlots; ++slot)
        best.tileTypes.append(document.mapInfo().tileTypes.isEmpty() ? 0
            : document.mapInfo().tileTypes.at(
                  slot % document.mapInfo().tileTypes.size()));
    for(int slot = 0; slot < sourceAtlas.tiles.size(); ++slot)
        best.authoredSlotRemap.insert(slot, slot);
    for(int page = 0; page < targetPages; ++page)
    {
        QVector<QImage> pageTiles;
        pageTiles.reserve(64);
        for(int local = 0; local < 64; ++local)
        {
            const int slot = page * 64 + local;
            QImage pixels = sourceAtlas.tiles.at(slot % sourceAtlas.tiles.size())
                .convertToFormat(QImage::Format_RGBA8888);
            if(resampled)
                pixels = pixels.scaled(tileSize, tileSize,
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGBA8888);
            semanticPixels.addData(QByteArray::number(pixels.width()));
            semanticPixels.addData("x", 1);
            semanticPixels.addData(QByteArray::number(pixels.height()));
            semanticPixels.addData("\n", 1);
            for(int y = 0; y < pixels.height(); ++y)
                semanticPixels.addData(
                    reinterpret_cast<const char*>(pixels.constScanLine(y)),
                    pixels.width() * 4);
            // Repeat exact stock Zone18 imagery. Physical slot/page identity
            // comes from the MPR reference; changing pixels here prevents the
            // semantic catalog from recovering a complete paintable field.
            const QByteArray pixelSha256 = QCryptographicHash::hash(
                QByteArray(reinterpret_cast<const char*>(pixels.constBits()),
                           int(pixels.sizeInBytes())),
                QCryptographicHash::Sha256).toHex();
            stable.addData(pixelSha256);
            if(best.tiles.isEmpty())
            {
                STerrainFinalizedTile first;
                first.pixels = pixels;
                first.pixelSha256 = pixelSha256;
                first.authored = true;
                first.sourceSlot = 0;
                best.tiles.append(first);
            }
            pageTiles.append(pixels);
        }
        if(sourceAtlas.pageMmpPayloads.isEmpty())
        {
            result.error = QStringLiteral(
                "The source atlas has no authored MMP page payloads.");
            return result;
        }
        const QImage composed = CTerrainVirtualAtlas::composePage(
            pageTiles, 0, tileSize);
        if(best.pages.isEmpty())
            best.pages.append(composed);
        if(resampled)
        {
            QString diagnostic;
            const QByteArray payload = ei::encodeMmpDxt1(
                composed, true, &diagnostic);
            if(payload.isEmpty())
            {
                result.error = QStringLiteral(
                    "Could not encode resampled runtime page %1: %2")
                    .arg(page).arg(diagnostic);
                return result;
            }
            best.pageMmpPayloads.append(payload);
        }
        else
            best.pageMmpPayloads.append(sourceAtlas.pageMmpPayloads.at(
                page % sourceAtlas.pageMmpPayloads.size()));
    }
    best.packedCells.reserve(cellCount);
    for(int cell = 0; cell < cellCount; ++cell)
    {
        const int slot = cell % requiredSlots;
        SMprPackedTile packed;
        packed.tileIndex = quint8(slot % 64);
        packed.atlasIndex = quint8(slot / 64);
        const quint16 encoded = packed.encode();
        best.packedCells.append(encoded);
        char bytes[2];
        qToLittleEndian<quint16>(encoded, reinterpret_cast<uchar*>(bytes));
        stable.addData(bytes, 2);
    }
    best.stableSha256 = stable.result().toHex();
    best.isolatedNamespace = resampled
        ? QStringLiteral("%1_runtime_%2p_%3px_%4")
            .arg(nameSpace.toLower()).arg(targetPages).arg(pageSize)
            .arg(QString::fromLatin1(best.stableSha256.left(12)))
        : QStringLiteral("%1_runtime_%2p_%3")
            .arg(nameSpace.toLower()).arg(targetPages)
            .arg(QString::fromLatin1(best.stableSha256.left(12)));
    best.success = true;
    CValidationReport packageReport;
    const STerrainPortableAtlasPackage package =
        CTerrainVirtualAtlas::buildPortablePackage(
            document, best, ETerrainTileLayer::Land, &packageReport);
    if(!package.success)
    {
        result.error = package.error + QLatin1Char('\n') + packageReport.toText();
        return result;
    }
    if(!QDir().mkpath(outputDirectory))
    {
        result.error = QStringLiteral("Could not create %1").arg(outputDirectory);
        return result;
    }
    const QString baseName = best.isolatedNamespace;
    if(!writeBytes(QDir(outputDirectory).filePath(baseName + QStringLiteral(".mpr")),
                   package.mprBytes, &result.error)
       || !writeBytes(QDir(outputDirectory).filePath(
                         baseName + QStringLiteral("-atlas.res")),
                     package.resourceArchiveBytes, &result.error))
        return result;
    STerrainAtlasProfile portableProfile = sourceAtlas.profile;
    portableProfile.mprNamespace = baseName;
    portableProfile.atlasPixelSha256 = semanticPixels.result().toHex();
    portableProfile.atlasSlotCount = requiredSlots;
    portableProfile.provenance =
        QStringLiteral("portable-exact-stock-atlas-clone-v1");
    portableProfile.approved = true;
    portableProfile.slotDefinitions.clear();
    portableProfile.slotDefinitions.reserve(requiredSlots);
    for(int slot = 0; slot < requiredSlots; ++slot)
    {
        STerrainAtlasProfileSlot definition =
            sourceAtlas.profile.slotDefinitions.at(
                slot % sourceAtlas.profile.slotDefinitions.size());
        definition.slot = slot;
        portableProfile.slotDefinitions.append(definition);
    }
    CValidationReport portableProfileReport;
    if(!portableProfile.normalizeMembership(&portableProfileReport)
       || !portableProfile.validateBinding(
           baseName, portableProfile.atlasPixelSha256, requiredSlots,
           &portableProfileReport))
    {
        result.error = QStringLiteral(
            "Could not create the exact semantic profile for the expanded atlas.\n%1")
            .arg(portableProfileReport.toText());
        return result;
    }
    const STerrainAtlasAnalysis portableAnalysis =
        portableProfile.approvedAnalysis(&portableProfileReport);
    if(!portableAnalysis.usable
       || portableAnalysis.describedSlots != requiredSlots)
    {
        result.error = QStringLiteral(
            "The expanded semantic profile does not cover every physical slot.\n%1")
            .arg(portableProfileReport.toText());
        return result;
    }
    CMprDocument paintDocument;
    CTerrainEditModel paintTerrain;
    CValidationReport paintLoadReport;
    if(!paintDocument.loadBytes(package.mprBytes, baseName + QStringLiteral(".mpr"),
                                &paintLoadReport)
       || !paintTerrain.load(paintDocument, &paintLoadReport))
    {
        result.error = QStringLiteral(
            "The expanded paint-refusal regression map could not be reopened.\n%1")
            .arg(paintLoadReport.toText());
        return result;
    }
    STerrainTileBrush paintBrush;
    paintBrush.centerX = qMin(110, paintTerrain.tileCountX() - 1);
    paintBrush.centerY = qMin(62, paintTerrain.tileCountY() - 1);
    paintBrush.radius = 0.0f;
    bool paintAccepted = false;
    CValidationReport paintReport;
    for(const STerrainAtlasMaterial& material : portableAnalysis.materials)
    {
        if(material.layer != ETerrainAtlasLayer::Land || material.cornerOnly)
            continue;
        const STerrainTileCommand command = paintTerrain.makeVirtualMaterialPaint(
            int(paintBrush.centerX), int(paintBrush.centerY), paintBrush,
            ETerrainTileLayer::Land, material.id, portableAnalysis.landCatalog,
            0x13579bdfu, &paintReport);
        if(paintReport.hasErrors()) break;
        if(!command.virtualVertices.isEmpty())
        {
            paintAccepted = true;
            break;
        }
    }
    if(!paintAccepted || paintReport.hasErrors())
    {
        result.error = QStringLiteral(
            "Expanded-atlas brush preview was refused at the original failure coordinate.\n%1")
            .arg(paintReport.toText());
        return result;
    }
    if(!CTerrainAtlasProfileStore::save(
           portableProfile, portableProfileRoot, &portableProfileReport))
    {
        result.error = QStringLiteral(
            "Could not save the exact semantic profile for the expanded atlas.\n%1")
            .arg(portableProfileReport.toText());
        return result;
    }
    int highestReferencedSlot = -1;
    QSet<int> referencedPages;
    for(quint16 encoded : best.packedCells)
    {
        const SMprPackedTile packed = SMprPackedTile::decode(encoded);
        highestReferencedSlot = qMax(highestReferencedSlot, packed.atlasSlot());
        referencedPages.insert(packed.atlasIndex);
    }
    if(highestReferencedSlot < (targetPages - 1) * 64
       || !referencedPages.contains(targetPages - 1))
    {
        result.error = QStringLiteral("The highest atlas page is not referenced by terrain.");
        return result;
    }
    QJsonObject manifest;
    manifest.insert("schema", "ei-expanded-atlas-runtime-probe-v1");
    manifest.insert("source_mpr", QFileInfo(sourcePath).fileName());
    manifest.insert("source_mpr_sha256", QString::fromLatin1(fileSha256(sourcePath)));
    manifest.insert("source_mob", QFileInfo(sourceMobPath).fileName());
    manifest.insert("source_mob_sha256", QString::fromLatin1(fileSha256(sourceMobPath)));
    manifest.insert("workload", "installed-zone18-mpr-mob-with-drawall");
    manifest.insert("drawall_required", true);
    manifest.insert("isolated_namespace", baseName);
    manifest.insert("mpr_file", baseName + QStringLiteral(".mpr"));
    manifest.insert("atlas_file", baseName + QStringLiteral("-atlas.res"));
    manifest.insert("semantic_profile_file",
        QDir(QFileInfo(outputDirectory).absoluteFilePath()).relativeFilePath(
            QFileInfo(CTerrainAtlasProfileStore::profilePath(
                baseName, portableProfileRoot)).absoluteFilePath()));
    manifest.insert("texture_pages", best.textureCount);
    manifest.insert("active_slots", best.requiredSlots);
    const int atlasColumns = targetPages <= 8 ? targetPages
        : qMax(1, int(std::ceil(std::sqrt(double(targetPages)))));
    const int atlasRows = (targetPages + atlasColumns - 1) / atlasColumns;
    manifest.insert("page_width", pageSize);
    manifest.insert("page_height", pageSize);
    manifest.insert("source_page_width", sourceTileSize * 8);
    manifest.insert("source_page_height", sourceTileSize * 8);
    manifest.insert("atlas_columns", atlasColumns);
    manifest.insert("atlas_rows", atlasRows);
    manifest.insert("combined_width", pageSize * atlasColumns);
    manifest.insert("combined_height", pageSize * atlasRows);
    manifest.insert("combined_rgba_bytes", double(qint64(pageSize)
        * atlasColumns * qint64(pageSize) * atlasRows * 4));
    manifest.insert("probe_variant", resampled ? "512px" : "hd");
    manifest.insert("pixel_policy", resampled
        ? "stock-zone18-per-tile-resample" : "exact-stock-zone18-repeat");
    manifest.insert("page_payload_policy", resampled
        ? "deterministic-dxt1-with-authored-mips"
        : "byte-identical-stock-mmp-repeat");
    manifest.insert("resampling_policy", resampled
        ? "qt-smooth-per-tile-128-to-64-v1" : "none");
    manifest.insert("test_only_resolution_probe", resampled);
    manifest.insert("silent_production_resampling", false);
    manifest.insert("source_cycle_slots", sourceAtlas.tiles.size());
    manifest.insert("highest_referenced_slot", highestReferencedSlot);
    manifest.insert("highest_referenced_page", targetPages - 1);
    manifest.insert("referenced_page_count", referencedPages.size());
    manifest.insert("mmp_member_count", package.resourceMembers.size());
    manifest.insert("map_size_x", int(document.mapInfo().sectorCountX));
    manifest.insert("map_size_y", int(document.mapInfo().sectorCountY));
    manifest.insert("stable_sha256", QString::fromLatin1(best.stableSha256));
    manifest.insert("installed_resources_modified", false);
    if(!writeBytes(QDir(outputDirectory).filePath(QStringLiteral("manifest.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented),
                   &result.error))
        return result;
    result.manifest = manifest;
    result.success = true;
    return result;
}

BuiltMap buildPaintedExtensionRuntimeProbe(
    const QString& sourcePath, const QString& sourceMobPath,
    const Atlas& sourceAtlas, const QString& outputDirectory,
    const QString& portableProfileRoot)
{
    BuiltMap result;
    const QString controlDirectory = QDir(outputDirectory).filePath(
        QStringLiteral("base-2048-control"));
    const BuiltMap control = buildExpandedRuntimeProbe(
        sourcePath, sourceMobPath, QStringLiteral("Zone18"), 32,
        {}, {}, sourceAtlas, controlDirectory, portableProfileRoot);
    if(!control.success)
    {
        result.error = QStringLiteral("Could not build the 2048-slot paint control: ")
            + control.error;
        return result;
    }

    const QString controlMprPath = QDir(controlDirectory).filePath(
        control.manifest.value("mpr_file").toString());
    QFile controlMprFile(controlMprPath);
    if(!controlMprFile.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("Could not read the 2048-slot paint control.");
        return result;
    }
    CMprDocument controlDocument;
    CValidationReport report;
    if(!controlDocument.loadBytes(controlMprFile.readAll(), controlMprPath, &report))
    {
        result.error = report.toText();
        return result;
    }
    const QString paintedNamespace =
        control.manifest.value("isolated_namespace").toString()
        + QStringLiteral("_painted");
    CMprEditTransaction isolateTransaction(QStringLiteral(
        "Isolate painted extension regression namespace"));
    QByteArray isolatedControlBytes;
    if(!controlDocument.applyIsolatedNamespaceTransaction(
           isolateTransaction, paintedNamespace, &isolatedControlBytes,
           &report)
       || !controlDocument.loadBytes(
           isolatedControlBytes, paintedNamespace + QStringLiteral(".mpr"),
           &report))
    {
        result.error = QStringLiteral(
            "Could not isolate the painted 2048-slot control namespace.\n")
            + report.toText();
        return result;
    }

    QVector<STerrainAtlasMaterial> paintable;
    for(const STerrainAtlasMaterial& material : sourceAtlas.analysis.materials)
        if(material.layer == ETerrainAtlasLayer::Land && !material.cornerOnly
           && material.representativeSlot >= 0
           && material.representativeSlot < sourceAtlas.tiles.size())
            paintable.append(material);
    if(paintable.size() < 2)
    {
        result.error = QStringLiteral(
            "Zone18 does not expose two uniform land materials for the painted extension probe.");
        return result;
    }

    CTerrainTransitionCatalog uniformCatalog;
    QVector<QImage> uniformImages;
    for(int index = 0; index < 2; ++index)
    {
        STerrainTransitionVariant variant;
        variant.signature.northWest = variant.signature.northEast =
            variant.signature.southEast = variant.signature.southWest =
                paintable.at(index).id;
        SMprPackedTile packed;
        packed.tileIndex = quint8(index);
        variant.packedTile = packed.encode();
        uniformCatalog.addVariant(variant);
        uniformImages.append(sourceAtlas.tiles.at(
            paintable.at(index).representativeSlot));
    }
    QVector<qint32> uniformTypes;
    QMap<qint32, qint32> uniformRuntimeTypes;
    for(int index = 0; index < 2; ++index)
    {
        const int sourceSlot = paintable.at(index).representativeSlot;
        const qint32 type = sourceSlot >= 0
            && sourceSlot < controlDocument.mapInfo().tileTypes.size()
            ? controlDocument.mapInfo().tileTypes.at(sourceSlot) : -1;
        uniformTypes.append(type);
        uniformRuntimeTypes.insert(paintable.at(index).id, type);
    }
    CTerrainVirtualAtlas generator(uniformCatalog, uniformImages, 0x2048u,
        {}, {}, true, uniformTypes, uniformRuntimeTypes);
    STerrainTransitionSignature requested;
    requested.northWest = paintable.at(0).id;
    requested.northEast = requested.southEast = requested.southWest =
        paintable.at(1).id;
    const STerrainVirtualTile generated = generator.resolve(
        requested, 0, 0, &report, paintable.at(0).id);
    if(generated.authored || generated.generatedIndex < 0
       || generator.generatedTiles().at(generated.generatedIndex).pixels.isNull())
    {
        result.error = QStringLiteral(
            "The painted extension probe did not create an exact missing blend.");
        return result;
    }

    constexpr int baseSlots = 2048;
    constexpr int activeSlots = baseSlots + 64;
    constexpr int generatedSlot = baseSlots;
    CTerrainEditModel terrain;
    if(!terrain.load(controlDocument, &report))
    {
        result.error = report.toText();
        return result;
    }
    terrain.enableWorkingAtlasSlots(activeSlots);
    bool available = false;
    const quint16 before = terrain.packedTile(
        0, 0, ETerrainTileLayer::Land, &available);
    SMprPackedTile extensionPacked;
    extensionPacked.atlasIndex = quint8(generatedSlot / 64);
    extensionPacked.tileIndex = quint8(generatedSlot % 64);
    STerrainTileCommand command;
    STerrainTileDelta delta;
    delta.globalX = 0;
    delta.globalY = 0;
    delta.layer = ETerrainTileLayer::Land;
    delta.before = before;
    delta.after = extensionPacked.encode();
    command.deltas.append(delta);
    if(!available || !terrain.apply(command, true, &report))
    {
        result.error = QStringLiteral(
            "The generated extension slot could not be referenced by terrain.\n")
            + report.toText();
        return result;
    }
    CMprEditTransaction transaction(QStringLiteral(
        "Create painted 2048-slot extension runtime probe"));
    if(!terrain.appendSectorEdits(&transaction, &report))
    {
        result.error = report.toText();
        return result;
    }
    QByteArray paintedMprBytes;
    if(!controlDocument.applyTransaction(transaction, &paintedMprBytes, &report))
    {
        result.error = report.toText();
        return result;
    }
    CMprDocument paintedDocument;
    if(!paintedDocument.loadBytes(paintedMprBytes,
                                  QStringLiteral("painted-2048.mpr"), &report))
    {
        result.error = report.toText();
        return result;
    }

    QVector<QImage> activeTiles;
    activeTiles.reserve(activeSlots);
    for(int slot = 0; slot < baseSlots; ++slot)
        activeTiles.append(sourceAtlas.tiles.at(slot % sourceAtlas.tiles.size()));
    const QImage generatedPixels =
        generator.generatedTiles().at(generated.generatedIndex).pixels;
    for(int slot = baseSlots; slot < activeSlots; ++slot)
        activeTiles.append(generatedPixels);
    QVector<qint32> activeTileTypes;
    activeTileTypes.reserve(activeSlots);
    for(int slot = 0; slot < baseSlots; ++slot)
        activeTileTypes.append(controlDocument.mapInfo().tileTypes.at(
            slot % controlDocument.mapInfo().tileTypes.size()));
    for(int slot = baseSlots; slot < activeSlots; ++slot)
        activeTileTypes.append(generated.runtimeTerrainType);
    QVector<QByteArray> authoredPages;
    authoredPages.reserve(32);
    for(int page = 0; page < 32; ++page)
        authoredPages.append(sourceAtlas.pageMmpPayloads.at(
            page % sourceAtlas.pageMmpPayloads.size()));
    const STerrainPortableAtlasPackage package =
        CTerrainVirtualAtlas::buildWorkingPreviewPackage(
            paintedDocument, activeTiles, activeTileTypes,
            authoredPages, &report);
    if(!package.success)
    {
        result.error = package.error + QLatin1Char('\n') + report.toText();
        return result;
    }

    const QString controlProfilePath = QDir(controlDirectory).filePath(
        control.manifest.value("semantic_profile_file").toString());
    STerrainAtlasProfile paintedProfile;
    CValidationReport profileReport;
    if(!STerrainAtlasProfile::load(
           QFileInfo(controlProfilePath).absoluteFilePath(), &paintedProfile,
           &profileReport)
       || paintedProfile.slotDefinitions.size() != baseSlots)
    {
        result.error = QStringLiteral(
            "The 2112-slot control semantic profile could not be loaded.\n")
            + profileReport.toText();
        return result;
    }
    paintedProfile.mprNamespace = paintedNamespace;
    paintedProfile.atlasPixelSha256 =
        CTerrainAtlasProfileInference::pixelSha256(activeTiles);
    paintedProfile.atlasSlotCount = activeSlots;
    for(int slot = baseSlots; slot < activeSlots; ++slot)
    {
        STerrainAtlasProfileSlot definition;
        definition.slot = slot;
        definition.signature = requested;
        paintedProfile.slotDefinitions.append(definition);
    }
    paintedProfile.provenance =
        QStringLiteral("portable-exact-stock-atlas-clone-v1");
    paintedProfile.approved = true;
    if(!paintedProfile.normalizeMembership(&profileReport)
       || !paintedProfile.validateBinding(
           paintedProfile.mprNamespace, paintedProfile.atlasPixelSha256,
           activeSlots, &profileReport)
       || !CTerrainAtlasProfileStore::save(
           paintedProfile, portableProfileRoot, &profileReport))
    {
        result.error = QStringLiteral(
            "The painted 2112-slot semantic profile could not be bound.\n")
            + profileReport.toText();
        return result;
    }
    const STerrainAtlasAnalysis paintedAnalysis =
        paintedProfile.approvedAnalysis(&profileReport);
    CMprDocument semanticReopen;
    CTerrainEditModel semanticTerrain;
    if(!paintedAnalysis.usable
       || paintedAnalysis.describedSlots != activeSlots
       || !semanticReopen.loadBytes(
           package.mprBytes, QStringLiteral("painted-2112.mpr"),
           &profileReport)
       || !semanticTerrain.load(semanticReopen, &profileReport))
    {
        result.error = QStringLiteral(
            "The painted fixture failed its semantic reopen.\n")
            + profileReport.toText();
        return result;
    }
    STerrainTileBrush regressionBrush;
    regressionBrush.centerX = qMin(110, semanticTerrain.tileCountX() - 1);
    regressionBrush.centerY = qMin(62, semanticTerrain.tileCountY() - 1);
    regressionBrush.radius = 0.0f;
    bool brushAccepted = false;
    for(const STerrainAtlasMaterial& material : paintedAnalysis.materials)
    {
        if(material.layer != ETerrainAtlasLayer::Land || material.cornerOnly)
            continue;
        CValidationReport brushReport;
        const STerrainTileCommand brushCommand =
            semanticTerrain.makeVirtualMaterialPaint(
                int(regressionBrush.centerX), int(regressionBrush.centerY),
                regressionBrush, ETerrainTileLayer::Land, material.id,
                paintedAnalysis.landCatalog, 0x2112u, &brushReport);
        if(brushReport.hasErrors())
        {
            profileReport = brushReport;
            break;
        }
        if(!brushCommand.virtualVertices.isEmpty())
        {
            brushAccepted = true;
            break;
        }
    }
    if(!brushAccepted)
    {
        result.error = QStringLiteral(
            "The painted 2112-slot fixture still refuses a semantic brush after reopen.\n")
            + profileReport.toText();
        return result;
    }

    if(!QDir().mkpath(outputDirectory))
    {
        result.error = QStringLiteral("Could not create %1").arg(outputDirectory);
        return result;
    }
    const QString baseName = paintedNamespace;
    const QString mprName = baseName + QStringLiteral("-painted-extension.mpr");
    const QString atlasName = baseName + QStringLiteral("-painted-extension-atlas.res");
    if(!writeBytes(QDir(outputDirectory).filePath(mprName), package.mprBytes,
                   &result.error)
       || !writeBytes(QDir(outputDirectory).filePath(atlasName),
                      package.resourceArchiveBytes, &result.error))
        return result;

    QJsonObject manifest;
    manifest.insert("schema", "ei-painted-working-atlas-runtime-probe-v1");
    manifest.insert("source_mpr", QFileInfo(sourcePath).fileName());
    manifest.insert("source_mpr_sha256", QString::fromLatin1(fileSha256(sourcePath)));
    manifest.insert("source_mob", QFileInfo(sourceMobPath).fileName());
    manifest.insert("source_mob_sha256", QString::fromLatin1(fileSha256(sourceMobPath)));
    manifest.insert("workload", "installed-zone18-mpr-mob-painted-extension");
    manifest.insert("drawall_required", false);
    manifest.insert("isolated_namespace", baseName);
    manifest.insert("mpr_file", mprName);
    manifest.insert("atlas_file", atlasName);
    manifest.insert("texture_pages", 33);
    manifest.insert("active_slots", activeSlots);
    const int pageSize = sourceAtlas.tiles.front().width() * 8;
    manifest.insert("page_width", pageSize);
    manifest.insert("page_height", pageSize);
    manifest.insert("atlas_columns", 6);
    manifest.insert("atlas_rows", 6);
    manifest.insert("combined_width", pageSize * 6);
    manifest.insert("combined_height", pageSize * 6);
    manifest.insert("combined_rgba_bytes", double(qint64(pageSize)
        * 6 * qint64(pageSize) * 6 * 4));
    manifest.insert("base_authored_slots", baseSlots);
    manifest.insert("generated_exact_tiles", 1);
    manifest.insert("generated_referenced_slot", generatedSlot);
    manifest.insert("highest_referenced_page", 32);
    manifest.insert("mmp_member_count", package.resourceMembers.size());
    manifest.insert("semantic_profile_file",
        QDir(QFileInfo(outputDirectory).absoluteFilePath()).relativeFilePath(
            QFileInfo(CTerrainAtlasProfileStore::profilePath(
                paintedProfile.mprNamespace, portableProfileRoot))
                .absoluteFilePath()));
    manifest.insert("semantic_profile_slots", paintedAnalysis.describedSlots);
    manifest.insert("semantic_reopen_paint_accepted", brushAccepted);
    manifest.insert("map_size_x", int(controlDocument.mapInfo().sectorCountX));
    manifest.insert("map_size_y", int(controlDocument.mapInfo().sectorCountY));
    manifest.insert("installed_resources_modified", false);
    if(!writeBytes(QDir(outputDirectory).filePath(QStringLiteral("manifest.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented),
                   &result.error))
        return result;
    result.manifest = manifest;
    result.success = true;
    return result;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    if(application.arguments().size() < 3 || application.arguments().size() > 5)
    {
        err << "usage: modern_atlas_real_map_builder <output> <JamaisVu135-root> [--runtime-only|--runtime-lowres-only|--painted-only|--stripe-only|--mask-review-only|--authored-mask-inventory|--issue-mask-review <report.json>]\n";
        return 2;
    }
    const QString outputRoot = QDir::cleanPath(application.arguments().at(1));
    const QString jmvRoot = QDir::cleanPath(application.arguments().at(2));
    StockAssets assets;
    QString error;
    if(!assets.index(jmvRoot, &error))
    {
        err << error << '\n';
        return 3;
    }
    const bool runtimeOnly = application.arguments().contains(
        QStringLiteral("--runtime-only"));
    const bool paintedOnly = application.arguments().contains(
        QStringLiteral("--painted-only"));
    const bool runtimeLowresOnly = application.arguments().contains(
        QStringLiteral("--runtime-lowres-only"));
    const bool stripeOnly = application.arguments().contains(
        QStringLiteral("--stripe-only"));
    const bool maskReviewOnly = application.arguments().contains(
        QStringLiteral("--mask-review-only"));
    const bool authoredMaskInventory = application.arguments().contains(
        QStringLiteral("--authored-mask-inventory"));
    const int issueMaskReviewIndex = application.arguments().indexOf(
        QStringLiteral("--issue-mask-review"));
    if(issueMaskReviewIndex >= 0)
    {
        if(issueMaskReviewIndex + 1 >= application.arguments().size())
        {
            err << "--issue-mask-review requires a paint issue report path\n";
            return 2;
        }
        Atlas zone2 = assets.atlas(QStringLiteral("Zone2"), 512, &error);
        if(!zone2.valid)
        {
            err << "Zone2 issue-mask-review source unavailable: "
                << error << '\n';
            return 4;
        }
        const QJsonObject review = buildIssueJunctionMaskReview(
            zone2, &assets, application.arguments().at(issueMaskReviewIndex + 1),
            outputRoot, &error);
        if(review.isEmpty())
        {
            err << "issue-mask-review: " << error << '\n';
            return 7;
        }
        QJsonObject summary;
        summary.insert(QStringLiteral("selected_three_four_material_cells"),
            review.value(QStringLiteral("selected_three_four_material_cells")));
        summary.insert(QStringLiteral("mask_group_count"),
            review.value(QStringLiteral("mask_groups")).toArray().size());
        out << QJsonDocument(summary).toJson(QJsonDocument::Indented);
        return 0;
    }
    if(authoredMaskInventory)
    {
        Atlas zone2 = assets.atlas(QStringLiteral("Zone2"), 512, &error);
        if(!zone2.valid)
        {
            err << "Zone2 authored-mask inventory source unavailable: "
                << error << '\n';
            return 4;
        }
        const QJsonObject inventory = buildAuthoredMaskInventory(
            zone2, &assets, outputRoot, &error);
        if(inventory.isEmpty())
        {
            err << "authored-mask-inventory: " << error << '\n';
            return 7;
        }
        QJsonObject summary;
        summary.insert(QStringLiteral("candidate_polarities"),
                       inventory.value(QStringLiteral("candidate_polarities")));
        summary.insert(QStringLiteral("unique_images"),
                       inventory.value(QStringLiteral("unique_images")));
        out << QJsonDocument(summary).toJson(QJsonDocument::Indented);
        return 0;
    }
    if(maskReviewOnly)
    {
        Atlas zone2 = assets.atlas(QStringLiteral("Zone2"), 512, &error);
        if(!zone2.valid)
        {
            err << "Zone2 mask-review source unavailable: " << error << '\n';
            return 4;
        }
        const QJsonObject review = buildMaskReviewOnly(
            zone2, &assets, outputRoot, &error);
        if(review.isEmpty())
        {
            err << "mask-review-only: " << error << '\n';
            return 7;
        }
        out << QJsonDocument(review).toJson(QJsonDocument::Indented);
        return 0;
    }
    if(stripeOnly)
    {
        Atlas zone2 = assets.atlas(QStringLiteral("Zone2"), 512, &error);
        const QString sourceMpr = QDir(jmvRoot).filePath("Maps/zone2.mpr");
        if(!zone2.valid || !QFileInfo::exists(sourceMpr))
        {
            err << "Zone2 stripe source unavailable: " << error << '\n';
            return 4;
        }
        const QVector<STerrainGlobalMaterialFamily> materials =
            CTerrainAtlasPresetCatalog::globalUniformFamilies(nullptr);
        QMap<qint32, QString> names;
        QVector<qint32> ids;
        for(const STerrainGlobalMaterialFamily& material : materials)
            if(material.layer == ETerrainAtlasLayer::Land)
            {
                ids.append(material.id);
                names.insert(material.id, material.name);
            }
        std::sort(ids.begin(), ids.end(), [&names](qint32 left, qint32 right) {
            return names.value(left).localeAwareCompare(names.value(right)) < 0;
        });
        QJsonArray sourceManifest;
        const QMap<qint32, QImage> external = globalSources(
            materials, &assets, zone2.tiles.front().size(), &sourceManifest,
            &error);
        BuiltMap stripes = buildMap(
            sourceMpr, QStringLiteral("Zone2"), ids, names, external,
            zone2, &assets, outputRoot, true);
        if(!stripes.success)
        {
            err << "final-material-stripes: " << stripes.error << '\n';
            return 7;
        }
        const QString collectionPath = QDir(outputRoot).filePath(
            QStringLiteral("final-material-stripes.collection"));
        if(!writeMapCollection(collectionPath, outputRoot, &stripes,
               QStringLiteral("Final material stripes - Zone2 environment"),
               QStringLiteral("zone2.mob"), &error))
        {
            err << "final-material-stripes collection: " << error << '\n';
            return 9;
        }
        const QString circleDirectory=QDir(outputRoot).filePath(
            QStringLiteral("all-material-circles"));
        BuiltMap circles=buildMap(
            sourceMpr,QStringLiteral("Zone2"),ids,names,external,
            zone2,&assets,circleDirectory,false,true);
        if(!circles.success)
        {
            err << "all-material-circles: " << circles.error << '\n';
            return 11;
        }
        const QString circleCollectionPath=QDir(outputRoot).filePath(
            QStringLiteral("all-material-circles.collection"));
        if(!writeMapCollection(circleCollectionPath,circleDirectory,&circles,
               QStringLiteral("Every material radius-3 circles - Zone2 environment"),
               QStringLiteral("zone2.mob"),&error))
        {
            err << "all-material-circles collection: " << error << '\n';
            return 12;
        }
        circles.manifest.insert(QStringLiteral("reviewed_pure_sources"),
                                sourceManifest);
        if(!writeBytes(QDir(circleDirectory).filePath(QStringLiteral("manifest.json")),
               QJsonDocument(circles.manifest).toJson(QJsonDocument::Indented),
               &error))
        {
            err << error << '\n';
            return 13;
        }
        stripes.manifest.insert(QStringLiteral("reviewed_pure_sources"),
                                sourceManifest);
        stripes.manifest.insert(QStringLiteral("circle_test_map"),
            QDir(outputRoot).relativeFilePath(circleDirectory));
        stripes.manifest.insert(QStringLiteral("circle_test_collection"),
            QFileInfo(circleCollectionPath).fileName());
        stripes.manifest.insert(QStringLiteral("circle_test_unresolved_cells"),
            circles.manifest.value(QStringLiteral("unresolved_cells")));
        if(!writeBytes(QDir(outputRoot).filePath(QStringLiteral("manifest.json")),
                       QJsonDocument(stripes.manifest).toJson(
                           QJsonDocument::Indented), &error))
        {
            err << error << '\n';
            return 10;
        }
        out << QJsonDocument(stripes.manifest).toJson(QJsonDocument::Indented);
        return 0;
    }
    if(paintedOnly)
    {
        const QString sourceMpr = QDir(jmvRoot).filePath("Maps/zone18.mpr");
        const QString sourceMob = QDir(jmvRoot).filePath("Maps/zone18.mob");
        Atlas zone18 = assets.atlas(QStringLiteral("Zone18"), 512, &error);
        if(!zone18.valid || !QFileInfo::exists(sourceMpr)
           || !QFileInfo::exists(sourceMob))
        {
            err << "Zone18 painted runtime source unavailable: " << error << '\n';
            return 4;
        }
        const BuiltMap painted = buildPaintedExtensionRuntimeProbe(
            sourceMpr, sourceMob, zone18, outputRoot, outputRoot);
        if(!painted.success)
        {
            err << "painted-extension-2048: " << painted.error << '\n';
            return 11;
        }
        out << QJsonDocument(painted.manifest).toJson(QJsonDocument::Indented);
        return 0;
    }
    if(runtimeLowresOnly)
    {
        const QString sourceMpr = QDir(jmvRoot).filePath("Maps/zone18.mpr");
        const QString sourceMob = QDir(jmvRoot).filePath("Maps/zone18.mob");
        Atlas zone18 = assets.atlas(QStringLiteral("Zone18"), 512, &error);
        if(!zone18.valid || !QFileInfo::exists(sourceMpr)
           || !QFileInfo::exists(sourceMob))
        {
            err << "Zone18 low-resolution runtime source unavailable: "
                << error << '\n';
            return 4;
        }
        QJsonArray probes;
        const QString collectionsDirectory = QDir(outputRoot).filePath(
            QStringLiteral("collections"));
        for(int pages : {128, 256})
        {
            const QString directory = QDir(outputRoot).filePath(
                QStringLiteral("runtime-atlas-probes-512px/page-%1")
                    .arg(pages, 3, 10, QLatin1Char('0')));
            const BuiltMap probe = buildExpandedRuntimeProbe(
                sourceMpr, sourceMob, QStringLiteral("Zone18"), pages,
                {}, {}, zone18, directory, outputRoot, 512);
            if(!probe.success)
            {
                err << "runtime-lowres-probe-" << pages << ": "
                    << probe.error << '\n';
                return 8;
            }
            if(probe.manifest.value("combined_width").toInt() > 8192
               || probe.manifest.value("combined_height").toInt() > 8192)
            {
                err << "runtime-lowres-probe-" << pages
                    << ": combined texture exceeds 8192x8192\n";
                return 8;
            }
            const QString collectionName = QStringLiteral(
                "zone18-expanded-%1-slots-512px-pages.collection")
                .arg(pages * 64);
            const QString collectionPath = QDir(collectionsDirectory).filePath(
                collectionName);
            SEnduranceCollection collection;
            collection.name = QStringLiteral(
                "Zone18 expanded atlas - %1 slots - 512px test pages")
                .arg(pages * 64);
            collection.profileId = QStringLiteral("jamais-vu");
            const QString generatedMprPath = QFileInfo(
                QDir(directory).filePath(
                    probe.manifest.value("mpr_file").toString()))
                .absoluteFilePath();
            collection.mprReference = QDir(
                QFileInfo(collectionPath).absolutePath())
                .relativeFilePath(generatedMprPath);
            collection.mobReferences.append(QStringLiteral("zone18.mob"));
            if(!CCollectionDocument::save(collectionPath, collection, &error))
            {
                err << "runtime-lowres-probe-" << pages
                    << " collection: " << error << '\n';
                return 9;
            }
            QJsonObject item = probe.manifest;
            item.insert("collection_file",
                QStringLiteral("collections/") + collectionName);
            item.insert("collection_mob", "zone18.mob");
            probes.append(item);
            out << "runtime-lowres-probe-" << pages << ": slots="
                << probe.manifest.value("active_slots").toInt()
                << " combined="
                << probe.manifest.value("combined_width").toInt() << 'x'
                << probe.manifest.value("combined_height").toInt() << '\n';
            out.flush();
        }
        QJsonObject root;
        root.insert("schema", "ei-zone18-expanded-atlas-lowres-fixtures-v1");
        root.insert("asset_policy",
            "Installed JMV Zone18 imagery resampled for controlled runtime testing; live files unmodified.");
        root.insert("production_policy",
            "Test-only. The editor must never silently downsample a user atlas.");
        root.insert("runtime_atlas_probes_512px", probes);
        root.insert("source_mpr_sha256",
            QString::fromLatin1(fileSha256(sourceMpr)));
        root.insert("source_mob_sha256",
            QString::fromLatin1(fileSha256(sourceMob)));
        QDir().mkpath(outputRoot);
        if(!writeBytes(QDir(outputRoot).filePath("manifest.json"),
                       QJsonDocument(root).toJson(QJsonDocument::Indented),
                       &error))
        {
            err << error << '\n';
            return 10;
        }
        return 0;
    }
    if(runtimeOnly)
    {
        const QString sourceMpr = QDir(jmvRoot).filePath("Maps/zone18.mpr");
        const QString sourceMob = QDir(jmvRoot).filePath("Maps/zone18.mob");
        Atlas zone18 = assets.atlas(QStringLiteral("Zone18"), 512, &error);
        if(!zone18.valid || !QFileInfo::exists(sourceMpr)
           || !QFileInfo::exists(sourceMob))
        {
            err << "Zone18 runtime source unavailable: " << error << '\n';
            return 4;
        }
        QJsonArray probes;
        const QString collectionsDirectory = QDir(outputRoot).filePath(
            QStringLiteral("collections"));
        for(int pages : {16, 32, 64, 128, 256})
        {
            const QString directory = QDir(outputRoot).filePath(
                QStringLiteral("runtime-atlas-probes/page-%1")
                    .arg(pages, 2, 10, QLatin1Char('0')));
            const BuiltMap probe = buildExpandedRuntimeProbe(
                sourceMpr, sourceMob, QStringLiteral("Zone18"), pages,
                {}, {}, zone18, directory, outputRoot);
            if(!probe.success)
            {
                err << "runtime-probe-" << pages << ": " << probe.error << '\n';
                return 8;
            }
            const QString collectionName = QStringLiteral(
                "zone18-expanded-%1-slots.collection").arg(pages * 64);
            const QString collectionPath = QDir(collectionsDirectory).filePath(
                collectionName);
            SEnduranceCollection collection;
            collection.name = QStringLiteral("Zone18 expanded atlas - %1 slots")
                .arg(pages * 64);
            collection.profileId = QStringLiteral("jamais-vu");
            // Leave mapZoneId empty deliberately. The generated MPR has an
            // isolated namespace, while the collection explicitly supplies
            // the stock Zone18 environment MOB from the active JMV map root.
            const QString generatedMprPath = QFileInfo(
                QDir(directory).filePath(
                    probe.manifest.value("mpr_file").toString()))
                .absoluteFilePath();
            collection.mprReference = QDir(
                QFileInfo(collectionPath).absolutePath())
                .relativeFilePath(generatedMprPath);
            collection.mobReferences.append(QStringLiteral("zone18.mob"));
            if(!CCollectionDocument::save(collectionPath, collection, &error))
            {
                err << "runtime-probe-" << pages
                    << " collection: " << error << '\n';
                return 9;
            }
            QJsonObject probeManifest = probe.manifest;
            probeManifest.insert("collection_file",
                QStringLiteral("collections/") + collectionName);
            probeManifest.insert("collection_mob", "zone18.mob");
            probes.append(probeManifest);
            out << "runtime-probe-" << pages << ": slots="
                << probe.manifest.value("active_slots").toInt() << '\n';
            out.flush();
        }
        const QString paintedDirectory = QDir(outputRoot).filePath(
            QStringLiteral("painted-extension-probes/2048-plus-one-blend"));
        const BuiltMap painted = buildPaintedExtensionRuntimeProbe(
            sourceMpr, sourceMob, zone18, paintedDirectory, outputRoot);
        if(!painted.success)
        {
            err << "painted-extension-2048: " << painted.error << '\n';
            return 11;
        }
        const QString paintedCollectionName = QStringLiteral(
            "zone18-expanded-2048-painted-extension.collection");
        const QString paintedCollectionPath = QDir(collectionsDirectory).filePath(
            paintedCollectionName);
        SEnduranceCollection paintedCollection;
        paintedCollection.name = QStringLiteral(
            "Zone18 expanded atlas - 2048 slots + painted exact blend");
        paintedCollection.profileId = QStringLiteral("jamais-vu");
        paintedCollection.mprReference = QDir(
            QFileInfo(paintedCollectionPath).absolutePath()).relativeFilePath(
                QFileInfo(QDir(paintedDirectory).filePath(
                    painted.manifest.value("mpr_file").toString())).absoluteFilePath());
        paintedCollection.mobReferences.append(QStringLiteral("zone18.mob"));
        if(!CCollectionDocument::save(
               paintedCollectionPath, paintedCollection, &error))
        {
            err << "painted-extension-2048 collection: " << error << '\n';
            return 12;
        }
        QJsonObject paintedManifest = painted.manifest;
        paintedManifest.insert("collection_file",
            QStringLiteral("collections/") + paintedCollectionName);
        paintedManifest.insert("collection_mob", "zone18.mob");
        QJsonObject root;
        root.insert("schema", "ei-zone18-expanded-atlas-fixtures-v1");
        root.insert("asset_policy", "Installed JMV assets used directly; live files unmodified.");
        root.insert("runtime_atlas_probes", probes);
        root.insert("painted_extension_probe", paintedManifest);
        root.insert("source_mpr_sha256", QString::fromLatin1(fileSha256(sourceMpr)));
        root.insert("source_mob_sha256", QString::fromLatin1(fileSha256(sourceMob)));
        QDir().mkpath(outputRoot);
        if(!writeBytes(QDir(outputRoot).filePath("manifest.json"),
                       QJsonDocument(root).toJson(QJsonDocument::Indented), &error))
        {
            err << error << '\n';
            return 10;
        }
        return 0;
    }
    const QVector<STerrainGlobalMaterialFamily> globalFamilies =
        CTerrainAtlasPresetCatalog::globalUniformFamilies(nullptr);
    QMap<qint32, QString> globalNames;
    QVector<qint32> globalLandIds;
    for(const STerrainGlobalMaterialFamily& family : globalFamilies)
        if(family.layer == ETerrainAtlasLayer::Land)
        {
            globalLandIds.append(family.id);
            globalNames.insert(family.id, family.name);
        }
    std::sort(globalLandIds.begin(), globalLandIds.end(),
        [&globalNames](qint32 left, qint32 right) {
            return globalNames.value(left).localeAwareCompare(
                globalNames.value(right)) < 0;
        });

    struct Zone { const char* nameSpace; const char* file; int slotCount; };
    const Zone zones[] = {
        {"Zone1", "zone1.mpr", 512}, {"Zone2", "zone2.mpr", 512},
        {"Zone5_1", "zone5_1.mpr", 192}, {"Zone11", "zone11.mpr", 512},
        {"Zone16", "zone16.mpr", 256}, {"Zone17", "zone17.mpr", 256},
        {"Zone18", "zone18.mpr", 512}
    };
    Atlas zone2;
    Atlas zone18;
    QMap<qint32, QImage> zone2External;
    QJsonArray outputs;
    QJsonArray sourceManifest;
    for(const Zone& zone : zones)
    {
        Atlas sourceAtlas = assets.atlas(zone.nameSpace, zone.slotCount, &error);
        if(!sourceAtlas.valid)
        {
            err << error << '\n';
            return 4;
        }
        const bool isZone2 = QString::fromLatin1(zone.nameSpace).compare(
            QStringLiteral("Zone2"), Qt::CaseInsensitive) == 0;
        if(isZone2) zone2 = sourceAtlas;
        const bool isZone18 = QString::fromLatin1(zone.nameSpace).compare(
            QStringLiteral("Zone18"), Qt::CaseInsensitive) == 0;
        if(isZone18) zone18 = sourceAtlas;
        QJsonArray* sourcesForManifest = isZone2 ? &sourceManifest : nullptr;
        const QMap<qint32, QImage> external = globalSources(
            globalFamilies, &assets, sourceAtlas.tiles.front().size(),
            sourcesForManifest, &error);
        if(external.size() != globalLandIds.size())
        {
            err << zone.nameSpace << ": " << error << '\n';
            return 5;
        }
        if(isZone2) zone2External = external;
        const CTerrainTransitionCatalog globalCatalog =
            CTerrainAtlasPresetCatalog::globalizedCatalog(
                sourceAtlas.profile, sourceAtlas.analysis.landCatalog);
        QVector<qint32> localIds;
        for(qint32 id : globalLandIds)
            if(globalCatalog.hasMaterial(id)) localIds.append(id);
        const QString sourcePath = QDir(jmvRoot).filePath(
            QStringLiteral("Maps/") + QString::fromLatin1(zone.file));
        const QString directory = QDir(outputRoot).filePath(
            QStringLiteral("zone-set/")
            + QString::fromLatin1(zone.nameSpace).toLower());
        BuiltMap built = buildMap(sourcePath, zone.nameSpace,
            localIds, globalNames, external, sourceAtlas, &assets, directory);
        if(!built.success)
        {
            err << zone.nameSpace << ": " << built.error << '\n';
            return 6;
        }
        const QString zoneCollectionPath = QDir(outputRoot).filePath(
            QStringLiteral("zone-set-%1.collection")
                .arg(QString::fromLatin1(zone.nameSpace).toLower()));
        const QString mobReference = QString::fromLatin1(zone.file)
            .replace(QStringLiteral(".mpr"), QStringLiteral(".mob"),
                     Qt::CaseInsensitive);
        if(!writeMapCollection(zoneCollectionPath, directory, &built,
               QStringLiteral("Material stamps - %1 environment")
                   .arg(QString::fromLatin1(zone.nameSpace)),
               mobReference, &error))
        {
            err << zone.nameSpace << " collection: " << error << '\n';
            return 9;
        }
        outputs.append(built.manifest);
        out << zone.nameSpace << ": materials=" << localIds.size()
            << " slots=" << built.manifest.value("total_active_slots").toInt()
            << " generated=" << built.manifest.value("generated_exact_tiles").toInt()
            << " unresolved=0\n";
    }
    const QString fullSource = QDir(jmvRoot).filePath(QStringLiteral("Maps/zone2.mpr"));
    const QString fullDirectory = QDir(outputRoot).filePath(
        QStringLiteral("full-game-family-stamp"));
    BuiltMap full = buildMap(fullSource, QStringLiteral("Zone2"),
        globalLandIds, globalNames, zone2External, zone2, &assets,
        fullDirectory);
    if(!full.success)
    {
        err << "full-family: " << full.error << '\n';
        return 7;
    }
    const QString fullCollectionPath = QDir(outputRoot).filePath(
        QStringLiteral("full-game-family-stamp.collection"));
    if(!writeMapCollection(fullCollectionPath, fullDirectory, &full,
           QStringLiteral("Every reviewed material family - Zone2 environment"),
           QStringLiteral("zone2.mob"), &error))
    {
        err << "full-family collection: " << error << '\n';
        return 9;
    }
    outputs.append(full.manifest);
    const QString stripeDirectory = QDir(outputRoot).filePath(
        QStringLiteral("final-material-stripes"));
    BuiltMap stripes = buildMap(fullSource, QStringLiteral("Zone2"),
        globalLandIds, globalNames, zone2External, zone2, &assets,
        stripeDirectory, true);
    if(!stripes.success)
    {
        err << "final-material-stripes: " << stripes.error << '\n';
        return 7;
    }
    const QString stripeCollectionPath = QDir(outputRoot).filePath(
        QStringLiteral("final-material-stripes.collection"));
    if(!writeMapCollection(stripeCollectionPath, stripeDirectory, &stripes,
           QStringLiteral("Final material stripes - Zone2 environment"),
           QStringLiteral("zone2.mob"), &error))
    {
        err << "final-material-stripes collection: " << error << '\n';
        return 9;
    }
    outputs.append(stripes.manifest);
    const QString circleDirectory = QDir(outputRoot).filePath(
        QStringLiteral("all-material-circles"));
    BuiltMap circles = buildMap(fullSource, QStringLiteral("Zone2"),
        globalLandIds, globalNames, zone2External, zone2, &assets,
        circleDirectory, false, true);
    if(!circles.success)
    {
        err << "all-material-circles: " << circles.error << '\n';
        return 7;
    }
    const QString circleCollectionPath = QDir(outputRoot).filePath(
        QStringLiteral("all-material-circles.collection"));
    if(!writeMapCollection(circleCollectionPath, circleDirectory, &circles,
           QStringLiteral("Every material radius-3 circles - Zone2 environment"),
           QStringLiteral("zone2.mob"), &error))
    {
        err << "all-material-circles collection: " << error << '\n';
        return 9;
    }
    outputs.append(circles.manifest);
    const QString zone18Source = QDir(jmvRoot).filePath(
        QStringLiteral("Maps/zone18.mpr"));
    const QString zone18Mob = QDir(jmvRoot).filePath(
        QStringLiteral("Maps/zone18.mob"));
    if(!zone18.valid || !QFileInfo::exists(zone18Mob))
    {
        err << "Zone18 MPR/MOB or its authored atlas is unavailable.\n";
        return 8;
    }
    QJsonArray runtimeProbes;
    for(int pages : {16, 32, 64, 128, 256})
    {
        const QString probeDirectory = QDir(outputRoot).filePath(
            QStringLiteral("runtime-atlas-probes/page-%1")
                .arg(pages, 2, 10, QLatin1Char('0')));
        const BuiltMap probe = buildExpandedRuntimeProbe(
            zone18Source, zone18Mob, QStringLiteral("Zone18"), pages,
            globalLandIds, QMap<qint32, QImage>(), zone18, probeDirectory,
            outputRoot);
        if(!probe.success)
        {
            err << "runtime-probe-" << pages << ": " << probe.error << '\n';
            return 8;
        }
        runtimeProbes.append(probe.manifest);
        out << "runtime-probe-" << pages
            << ": slots=" << probe.manifest.value("active_slots").toInt()
            << " highest-page="
            << probe.manifest.value("highest_referenced_page").toInt()
            << " unresolved=0\n";
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"),
                QStringLiteral("ei-modern-atlas-real-map-set-v1"));
    root.insert(QStringLiteral("asset_policy"),
        QStringLiteral("Installed stock JMV assets used directly; separate EI-HD assets excluded."));
    root.insert(QStringLiteral("reviewed_pure_sources"), sourceManifest);
    root.insert(QStringLiteral("maps"), outputs);
    root.insert(QStringLiteral("runtime_atlas_probes"), runtimeProbes);
    root.insert(QStringLiteral("full_family_material_count"), globalLandIds.size());
    root.insert(QStringLiteral("full_family_required_slots"),
                full.manifest.value(QStringLiteral("total_active_slots")));
    root.insert(QStringLiteral("full_family_generated_exact_tiles"),
                full.manifest.value(QStringLiteral("generated_exact_tiles")));
    root.insert(QStringLiteral("stripe_test_required_slots"),
                stripes.manifest.value(QStringLiteral("total_active_slots")));
    root.insert(QStringLiteral("stripe_test_generated_exact_tiles"),
                stripes.manifest.value(QStringLiteral("generated_exact_tiles")));
    root.insert(QStringLiteral("stripe_test_global_stock_blends_reused"),
                stripes.manifest.value(QStringLiteral("global_stock_blends_reused")));
    root.insert(QStringLiteral("circle_test_required_slots"),
                circles.manifest.value(QStringLiteral("total_active_slots")));
    root.insert(QStringLiteral("circle_test_generated_exact_tiles"),
                circles.manifest.value(QStringLiteral("generated_exact_tiles")));
    root.insert(QStringLiteral("circle_test_global_stock_blends_reused"),
                circles.manifest.value(QStringLiteral("global_stock_blends_reused")));
    root.insert(QStringLiteral("unresolved_cells"), 0);
    QDir().mkpath(outputRoot);
    if(!writeBytes(QDir(outputRoot).filePath(QStringLiteral("manifest.json")),
        QJsonDocument(root).toJson(QJsonDocument::Indented), &error))
    {
        err << error << '\n';
        return 8;
    }
    const QByteArray readme = QByteArrayLiteral(
        "# Modern terrain atlas real-map tests\n\n"
        "These disposable maps use installed stock Jamais Vu terrain materials. "
        "Each MPR has an adjacent isolated `-atlas.res`; keep the pair together.\n\n"
        "`full-game-family-stamp` contains one non-overlapping 3x3 vertex stamp for "
        "every unique reviewed Family / Subfamily brush. `zone-set` contains the same "
        "Zone 1, 2, 5_1, 11, 16, and 17 source set used by semantic-atlas review. "
        "`final-material-stripes` lays out the same final brush set as long isolated "
        "stripes so stock transition reuse and generated exact blends can be compared "
        "visually. `all-material-circles` applies every final brush as an isolated "
        "radius-3 circle so transition rings and uniform interior variation can be "
        "reviewed at normal painting scale.\n\n"
        "Every visual manifest must report `unresolved_cells: 0`, which proves "
        "exact semantic corner resolution only. It is not a visual pass. Each map "
        "therefore starts with `visual_review_status: pending-human-review` and includes "
        "`material-visual-review-index.csv`. Use Landscape Tools -> Mark bad tiles on map "
        "to record wrong families, blends, or rotations as a portable diagnostic JSON. The "
        "`runtime-atlas-probes` directory contains a deterministic 8-page control "
        "plus 9-page and 16-page MPR/RES pairs whose highest page is referenced "
        "by terrain for automated Test Current Map runtime validation.\n");
    if(!writeBytes(QDir(outputRoot).filePath(QStringLiteral("README.md")),
                   readme, &error))
    {
        err << error << '\n';
        return 10;
    }
    out << "full-family: materials=" << globalLandIds.size()
        << " slots=" << full.manifest.value("total_active_slots").toInt()
        << " generated=" << full.manifest.value("generated_exact_tiles").toInt()
        << " unresolved=0\n";
    return 0;
}

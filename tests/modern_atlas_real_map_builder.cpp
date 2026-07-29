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
#include <QSet>
#include <QTextCodec>
#include <QTextStream>
#include <QtEndian>

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
        output->entries.insert(name, {item.DataOffset, item.DataSize});
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
        for(const STerrainGlobalMaterialSource& source : family.sources)
        {
            Atlas atlas = assets->atlas(source.mprNamespace,
                                        source.textureCount * 64, nullptr);
            if(!atlas.valid || source.uniformSlot < 0
               || source.uniformSlot >= atlas.tiles.size()) continue;
            const QImage image = atlas.tiles.at(source.uniformSlot);
            if(fallback.isNull())
            {
                fallback = image;
                chosen = source;
            }
            if(image.size() != tileSize) continue;
            fallback = image;
            chosen = source;
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
                  const Atlas& sourceAtlas, const QString& outputDirectory)
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
    const int spacing = 5;
    const int margin = 2;
    const int columns = qMax(1, (field.vertexWidth() - margin * 2) / spacing);
    const int rows = qMax(1, (field.vertexHeight() - margin * 2) / spacing);
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
        const int y0 = margin + (index / columns) * spacing;
        for(int y = y0; y < y0 + 3; ++y)
            for(int x = x0; x < x0 + 3; ++x)
                field.setMaterial(x, y, stampIds.at(index));
        QJsonObject stamp;
        stamp.insert(QStringLiteral("material_id"), stampIds.at(index));
        stamp.insert(QStringLiteral("material"), names.value(stampIds.at(index)));
        stamp.insert(QStringLiteral("blend_style"),
            CTerrainVirtualAtlas::blendStyleName(
                CTerrainVirtualAtlas::blendStyleForMaterialName(
                    names.value(stampIds.at(index)))));
        stamp.insert(QStringLiteral("x0"), x0);
        stamp.insert(QStringLiteral("y0"), y0);
        stamp.insert(QStringLiteral("width_vertices"), 3);
        stamp.insert(QStringLiteral("height_vertices"), 3);
        stamps.append(stamp);
    }
    // Force genuine four-family junctions in otherwise unused lower rows.
    QJsonArray fourFamilyJunctions;
    const int junctionY = margin + rows * spacing;
    if(junctionY + 1 < field.vertexHeight())
        for(int group = 0; group + 3 < stampIds.size(); group += 4)
        {
            const int x = margin + (group / 4) * 3;
            if(x + 1 >= field.vertexWidth()) break;
            field.setMaterial(x, junctionY, stampIds.at(group));
            field.setMaterial(x + 1, junctionY, stampIds.at(group + 1));
            field.setMaterial(x + 1, junctionY + 1, stampIds.at(group + 2));
            field.setMaterial(x, junctionY + 1, stampIds.at(group + 3));
            QJsonObject junction;
            junction.insert(QStringLiteral("cell_x"), x);
            junction.insert(QStringLiteral("cell_y"), junctionY);
            fourFamilyJunctions.append(junction);
        }

    QMap<qint32, ETerrainBlendStyle> blendStyles;
    for(auto it = names.constBegin(); it != names.constEnd(); ++it)
        blendStyles.insert(it.key(),
            CTerrainVirtualAtlas::blendStyleForMaterialName(it.value()));
    CTerrainVirtualAtlas atlas(catalog, sourceAtlas.tiles, 0x4a4d5631u,
                               externalSources, blendStyles);
    const QVector<quint16> pinned = pinnedOverlay(terrain);
    const STerrainAtlasFinalization finalized = atlas.finalize(
        field, nameSpace, 16384, document.mapInfo().tileTypes, &report, pinned);
    if(!finalized.success)
    {
        result.error = finalized.error + QLatin1Char('\n') + report.toText();
        return result;
    }
    int unresolved = 0;
    for(int y = 0; y < field.tileHeight(); ++y)
        for(int x = 0; x < field.tileWidth(); ++x)
        {
            bool found = false;
            const STerrainTransitionSignature actual = finalizedSignature(
                finalized, finalized.packedCells.at(y * field.tileWidth() + x),
                &found);
            if(!found || !sameSignature(actual, field.cellSignature(x, y)))
                ++unresolved;
        }
    if(unresolved != 0)
    {
        result.error = QStringLiteral("Finalized map contains %1 unresolved cells.")
            .arg(unresolved);
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
    const QString baseName = finalized.isolatedNamespace;
    if(!writeBytes(QDir(outputDirectory).filePath(baseName + QStringLiteral(".mpr")),
                   package.mprBytes, &result.error)
       || !writeBytes(QDir(outputDirectory).filePath(
                         baseName + QStringLiteral("-atlas.res")),
                     package.resourceArchiveBytes, &result.error))
        return result;

    int authored = 0;
    for(const STerrainFinalizedTile& tile : finalized.tiles)
        authored += tile.authored;
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                    QStringLiteral("ei-modern-atlas-real-map-v1"));
    manifest.insert(QStringLiteral("source_namespace"), nameSpace);
    manifest.insert(QStringLiteral("source_mpr"), QFileInfo(sourcePath).fileName());
    manifest.insert(QStringLiteral("isolated_namespace"), baseName);
    manifest.insert(QStringLiteral("family_stamps"), stamps);
    manifest.insert(QStringLiteral("four_family_junctions"), fourFamilyJunctions);
    manifest.insert(QStringLiteral("paintable_materials"), stampIds.size());
    manifest.insert(QStringLiteral("authored_tiles_reused"), authored);
    manifest.insert(QStringLiteral("generated_exact_tiles"),
                    finalized.tiles.size() - authored);
    manifest.insert(QStringLiteral("total_active_slots"), finalized.requiredSlots);
    manifest.insert(QStringLiteral("texture_pages"), finalized.textureCount);
    manifest.insert(QStringLiteral("capacity_limit"), finalized.capacityLimit);
    manifest.insert(QStringLiteral("unresolved_cells"), unresolved);
    manifest.insert(QStringLiteral("stable_sha256"),
                    QString::fromLatin1(finalized.stableSha256));
    manifest.insert(QStringLiteral("mpr_file"), baseName + QStringLiteral(".mpr"));
    manifest.insert(QStringLiteral("atlas_file"),
                    baseName + QStringLiteral("-atlas.res"));
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(
        QJsonDocument::Indented);
    if(!writeBytes(QDir(outputDirectory).filePath(QStringLiteral("manifest.json")),
                   manifestBytes, &result.error)) return result;
    result.manifest = manifest;
    result.success = true;
    return result;
}

BuiltMap buildExpandedRuntimeProbe(
    const QString& sourcePath, const QString& sourceMobPath,
    const QString& nameSpace, int targetPages,
    const QVector<qint32>& materialIds,
    const QMap<qint32, QImage>& externalSources,
    const Atlas& sourceAtlas, const QString& outputDirectory,
    const QString& portableProfileRoot)
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
    const int tileSize = sourceAtlas.tiles.front().width();
    if(tileSize <= 0 || sourceAtlas.tiles.front().height() != tileSize)
    {
        result.error = QStringLiteral("The source atlas tile dimensions are invalid.");
        return result;
    }
    QCryptographicHash stable(QCryptographicHash::Sha256);
    QCryptographicHash semanticPixels(QCryptographicHash::Sha256);
    semanticPixels.addData(QByteArray::number(requiredSlots));
    semanticPixels.addData("\n", 1);
    stable.addData(nameSpace.toLower().toUtf8());
    stable.addData(QByteArray::number(targetPages));
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
        if(best.pages.isEmpty())
            best.pages.append(CTerrainVirtualAtlas::composePage(
                pageTiles, 0, tileSize));
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
    best.isolatedNamespace = QStringLiteral("%1_runtime_%2p_%3")
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
    manifest.insert("semantic_profile_file", QDir(outputDirectory).relativeFilePath(
        CTerrainAtlasProfileStore::profilePath(baseName, portableProfileRoot)));
    manifest.insert("texture_pages", best.textureCount);
    manifest.insert("active_slots", best.requiredSlots);
    manifest.insert("pixel_policy", "exact-stock-zone18-repeat");
    manifest.insert("page_payload_policy", "byte-identical-stock-mmp-repeat");
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
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    if(application.arguments().size() < 3 || application.arguments().size() > 4)
    {
        err << "usage: modern_atlas_real_map_builder <output> <JamaisVu135-root> [--runtime-only]\n";
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
        QJsonObject root;
        root.insert("schema", "ei-zone18-expanded-atlas-fixtures-v1");
        root.insert("asset_policy", "Installed JMV assets used directly; live files unmodified.");
        root.insert("runtime_atlas_probes", probes);
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
        const BuiltMap built = buildMap(sourcePath, zone.nameSpace,
            localIds, globalNames, external, sourceAtlas, directory);
        if(!built.success)
        {
            err << zone.nameSpace << ": " << built.error << '\n';
            return 6;
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
    const BuiltMap full = buildMap(fullSource, QStringLiteral("Zone2"),
        globalLandIds, globalNames, zone2External, zone2, fullDirectory);
    if(!full.success)
    {
        err << "full-family: " << full.error << '\n';
        return 7;
    }
    outputs.append(full.manifest);
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
        "Zone 1, 2, 5_1, 11, 16, and 17 source set used by semantic-atlas review.\n\n"
        "Every visual manifest must report `unresolved_cells: 0`. The "
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

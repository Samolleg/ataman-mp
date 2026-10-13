#include "terrain_atlas_archive_binding.h"

#include <QDataStream>
#include <QCryptographicHash>
#include <QHash>
#include <QCache>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTextCodec>
#include <QTemporaryFile>
#include <QSharedPointer>
#include <QMutex>
#include <cstring>
#include <algorithm>
#include <QtEndian>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "mmp_codec.h"
#include "endurance_document.h"
#include "res_file.h"
#include "res_read_lease.h"
#include "terrain_atlas_profile.h"

namespace {
QMutex verifiedAtlasHashMutex;
QCache<QByteArray, QByteArray> verifiedAtlasHashes(128);

#ifdef Q_OS_WIN
struct SMappedTerrainPixels;
QMutex mappedTerrainMutex;
QVector<QWeakPointer<SMappedTerrainPixels>> mappedTerrainStores;
// Keep large immutable decoded atlases out of private committed heap memory.
// QImage's const-data constructor detaches on edits, even for a sole owner.
// The temporary backing is deleted by Windows on the last handle close (also
// on process termination); image cleanup keeps the mapping alive across COW
// containers and background-to-GUI handoff without a QObject owner.
struct SMappedTerrainPixels
{
    HANDLE file{INVALID_HANDLE_VALUE};
    HANDLE mapping{nullptr};
    uchar* data{nullptr};
    qint64 size{0};
    qint64 used{0};
    qint64 discarded{0};
    ~SMappedTerrainPixels()
    {
        if(data) UnmapViewOfFile(data);
        if(mapping) CloseHandle(mapping);
        if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    static QSharedPointer<SMappedTerrainPixels> create(qint64 bytes)
    {
        if(bytes < 32ll * 1024 * 1024 || bytes > 8ll * 1024 * 1024 * 1024)
            return {};
        QTemporaryFile temporary(QDir::tempPath()
            + QStringLiteral("/ataman-terrain-pixels-XXXXXX.tmp"));
        if(!temporary.open()) return {};
        const QString path = temporary.fileName();
        // QTemporaryFile::close() can retain its native temporary-file handle.
        // Remove that reservation before atomically creating our own handle;
        // CREATE_NEW cannot overwrite a file created by a competing process.
        if(!temporary.remove()) return {};
        temporary.setAutoRemove(false);
        QSharedPointer<SMappedTerrainPixels> store(new SMappedTerrainPixels);
        store->size = bytes;
        store->file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if(store->file == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER end; end.QuadPart = bytes;
        if(!SetFilePointerEx(store->file, end, nullptr, FILE_BEGIN)
           || !SetEndOfFile(store->file)) return {};
        const quint64 length = quint64(bytes);
        store->mapping = CreateFileMappingW(store->file, nullptr, PAGE_READWRITE,
            DWORD(length >> 32), DWORD(length & 0xffffffffu), nullptr);
        if(!store->mapping) return {};
        store->data = static_cast<uchar*>(MapViewOfFile(store->mapping,
            FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, SIZE_T(bytes)));
        if(store->data)
        {
            QMutexLocker lock(&mappedTerrainMutex);
            for(int i = mappedTerrainStores.size() - 1; i >= 0; --i)
                if(mappedTerrainStores.at(i).isNull()) mappedTerrainStores.removeAt(i);
            mappedTerrainStores.append(store.toWeakRef());
        }
        return store->data ? store : QSharedPointer<SMappedTerrainPixels>();
    }
    static void cleanup(void* owner)
    {
        delete static_cast<QSharedPointer<SMappedTerrainPixels>*>(owner);
    }
    QImage storeTile(const QImage& tile, const QSharedPointer<SMappedTerrainPixels>& owner)
    {
        const qint64 bytes = qint64(tile.width()) * tile.height() * 4;
        if(bytes > size - used) return tile;
        uchar* pixels = data + used;
        for(int y = 0; y < tile.height(); ++y)
            memcpy(pixels + qint64(y) * tile.width() * 4,
                   tile.constScanLine(y), size_t(tile.width()) * 4);
        used += bytes;
        return QImage(static_cast<const uchar*>(pixels), tile.width(), tile.height(),
            tile.width() * 4, QImage::Format_RGBA8888, cleanup,
            new QSharedPointer<SMappedTerrainPixels>(owner));
    }
    void discardResidentPages(bool all = false)
    {
        const qint64 begin = all ? 0 : discarded;
        if(used <= begin) return;
        // For unlocked file-backed pages Windows removes their working-set
        // residency and returns ERROR_NOT_LOCKED. Contents stay file-backed;
        // this never applies to private heap pixels or another process.
        const DWORD previousError = GetLastError();
        VirtualUnlock(data + begin, SIZE_T(used - begin));
        SetLastError(previousError);
        discarded = used;
    }
};

qint64 nativeAtlasPixelBytes(const QVector<QByteArray>& pages, int slotCount)
{
    qint64 bytes = 0;
    for(int page = 0; page < pages.size() && page * 64 < slotCount; ++page)
    {
        const QByteArray& payload = pages.at(page);
        if(payload.size() < 76) return 0;
        QDataStream stream(payload); stream.setByteOrder(QDataStream::LittleEndian);
        quint32 signature = 0, width = 0, height = 0;
        stream >> signature >> width >> height;
        if(signature != 0x00504d4d || width == 0 || width != height
           || width % 32 != 0 || width > 65536) return 0;
        bytes += qint64(qMin(64, slotCount - page * 64)) * (width / 8) * (width / 8) * 4;
    }
    return bytes;
}
#endif
uint decodedTileBucket(const QByteArray& payload, int slot, const QImage& tile)
{
    // Hash eight times fewer bytes: identical native BC1 blocks decode to the
    // same image. Exact QImage equality below remains the collision guard.
    // Different BC1 encodings of equal pixels may simply remain separate.
    if(payload.size() >= 76)
    {
        const quint32 width = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(payload.constData() + 4));
        const quint32 height = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(payload.constData() + 8));
        if(width >= 32 && height >= 32 && width % 32 == 0 && height % 32 == 0
           && width / 8 == quint32(tile.width()) && height / 8 == quint32(tile.height()))
        {
            uint bucket = qHash(width, qHash(height));
            const quint64 stride = quint64(width) * 2, bytes = width / 4;
            for(quint32 row = 0; row < height / 32; ++row)
            {
                // Keep the codec's inverted MPR-slot / raw-MMP row mapping.
                const quint64 offset = 76 + (quint64(7 - slot / 8) * (height / 32) + row) * stride
                    + quint64(slot % 8) * bytes;
                if(offset > quint64(payload.size()) || bytes > quint64(payload.size()) - offset)
                    return qHashBits(tile.constBits(), size_t(tile.sizeInBytes()));
                bucket = qHashBits(payload.constData() + offset, size_t(bytes), bucket);
            }
            return bucket;
        }
    }
    return qHashBits(tile.constBits(), size_t(tile.sizeInBytes()));
}

struct SArchiveMemberLocation
{
    quint32 offset{0};
    quint32 size{0};
};

bool archiveIndex(const QString& path,
                  QMap<QString, SArchiveMemberLocation>* members,
                  qint64* bytesRead = nullptr,
                  QSet<QString>* duplicateNames = nullptr,
                  QString* error = nullptr)
{
    if(bytesRead)
        *bytesRead = 0;
    if(duplicateNames)
        duplicateNames->clear();
    if(error)
        error->clear();
    if(!members)
        return false;
    members->clear();
    resIo::ReadLease lease(path, error);
    if(!lease.acquired()) return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = QStringLiteral("The RES archive could not be opened.");
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    SResFileHeader header;
    stream >> header;
    if(header.Signature != 0x019CE23C || header.TableSize > 10000000
       || quint64(header.TableOffset) > quint64(file.size()))
    {
        if(error)
            *error = QStringLiteral("The RES archive header is invalid.");
        return false;
    }
    if(bytesRead)
        *bytesRead += 16;
    if(!file.seek(header.TableOffset))
    {
        if(error)
            *error = QStringLiteral("The RES archive table could not be reached.");
        return false;
    }
    QVector<SResHashTable> table(int(header.TableSize));
    for(SResHashTable& entry : table)
        stream >> entry;
    if(stream.status() != QDataStream::Ok)
    {
        if(error)
            *error = QStringLiteral("The RES archive table is truncated.");
        return false;
    }
    if(bytesRead)
        *bytesRead += qint64(header.TableSize) * 22;
    const QByteArray names = file.read(header.NamesLenght);
    if(names.size() != int(header.NamesLenght))
    {
        if(error)
            *error = QStringLiteral("The RES filename buffer is truncated.");
        return false;
    }
    if(bytesRead)
        *bytesRead += names.size();
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(const SResHashTable& raw : table)
    {
        if(quint64(raw.NameOffset) + raw.NameLength > quint64(names.size())
           || quint64(raw.DataOffset) + raw.DataSize > quint64(file.size()))
            continue;
        const QString name = codec->toUnicode(names.mid(
            int(raw.NameOffset), int(raw.NameLength))).toLower();
        SArchiveMemberLocation location;
        location.offset = raw.DataOffset;
        location.size = raw.DataSize;
        if(members->contains(name) && duplicateNames)
            duplicateNames->insert(name);
        else
            members->insert(name, location);
    }
    return true;
}

QByteArray memberPayload(QFile* file,
                         const SArchiveMemberLocation& member)
{
    if(!file || !file->seek(member.offset))
        return QByteArray();
    const QByteArray payload = file->read(member.size);
    return payload.size() == int(member.size) ? payload : QByteArray();
}

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}
}

bool CTerrainAtlasArchiveBindingResolver::readArchiveMembers(
    const QString& archivePath, const QStringList& memberNames,
    QMap<QString, QByteArray>* payloads, qint64* bytesRead, QString* error)
{
    if(payloads)
        payloads->clear();
    if(bytesRead)
        *bytesRead = 0;
    if(error)
        error->clear();
    if(!payloads)
    {
        if(error)
            *error = QStringLiteral("No output map was supplied.");
        return false;
    }

    resIo::ReadLease lease(archivePath, error);
    if(!lease.acquired()) return false;
    QMap<QString, SArchiveMemberLocation> members;
    QSet<QString> duplicateNames;
    qint64 indexedBytes = 0;
    if(!archiveIndex(archivePath, &members, &indexedBytes, &duplicateNames,
                     error))
        return false;
    if(bytesRead)
        *bytesRead = indexedBytes;

    QFile file(archivePath);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = QStringLiteral("The RES archive could not be reopened.");
        return false;
    }
    for(const QString& requested : memberNames)
    {
        const QString normalized = requested.toLower();
        if(duplicateNames.contains(normalized))
        {
            if(error)
                *error = QStringLiteral("RES member %1 is ambiguous.")
                    .arg(requested);
            payloads->clear();
            return false;
        }
        const auto found = members.constFind(normalized);
        if(found == members.constEnd())
            continue;
        if(!file.seek(found->offset))
        {
            if(error)
                *error = QStringLiteral("RES member %1 could not be reached.")
                    .arg(requested);
            payloads->clear();
            return false;
        }
        const QByteArray payload = file.read(found->size);
        if(payload.size() != int(found->size))
        {
            if(error)
                *error = QStringLiteral("RES member %1 is truncated.")
                    .arg(requested);
            payloads->clear();
            return false;
        }
        payloads->insert(requested, payload);
        if(bytesRead)
            *bytesRead += payload.size();
    }
    return true;
}

void CTerrainAtlasArchiveBindingResolver::releaseDecodedImageResidency(const QVector<QImage>& images)
{
#ifdef Q_OS_WIN
    QMutexLocker lock(&mappedTerrainMutex);
    for(int index = mappedTerrainStores.size() - 1; index >= 0; --index)
    {
        const auto store = mappedTerrainStores.at(index).toStrongRef();
        if(!store) { mappedTerrainStores.removeAt(index); continue; }
        const quintptr begin = reinterpret_cast<quintptr>(store->data);
        // Capacity is immutable after registration. A different store may
        // still be decoding on a worker, so do not read its mutable used count.
        const quintptr end = begin + quintptr(store->size);
        QVector<QPair<quintptr, quintptr>> ranges;
        // Images from this store are published only after decoding finishes;
        // a concurrently decoding store cannot be present in this vector.
        for(const QImage& image : images)
        {
            const quintptr pixels = reinterpret_cast<quintptr>(image.constBits());
            if(pixels >= begin && pixels < end)
            {
                const quintptr length = quintptr(image.sizeInBytes());
                if(length <= end - pixels) ranges.append(qMakePair(pixels, pixels + length));
            }
        }
        if(ranges.isEmpty()) continue;
        std::sort(ranges.begin(), ranges.end());
        quintptr first = ranges.first().first, last = ranges.first().second;
        const DWORD previousError = GetLastError();
        for(int i = 1; i < ranges.size(); ++i)
        {
            if(ranges.at(i).first <= last) last = qMax(last, ranges.at(i).second);
            else
            {
                VirtualUnlock(reinterpret_cast<void*>(first), SIZE_T(last - first));
                first = ranges.at(i).first; last = ranges.at(i).second;
            }
        }
        VirtualUnlock(reinterpret_cast<void*>(first), SIZE_T(last - first));
        SetLastError(previousError);
    }
#else
    Q_UNUSED(images);
#endif
}

bool STerrainAtlasArchiveBindingResult::hasCompleteArchive(
    const QString& path) const
{
    const QString wanted = normalizedPath(path);
    for(const QString& complete : completeArchives)
        if(normalizedPath(complete) == wanted)
            return true;
    return false;
}

bool STerrainAtlasEmbeddedBindingResult::matchesLoadedAtlas(
    const QString& activeArchivePath,
    const QString& currentNamespace,
    const QByteArray& currentAtlasPixelSha256,
    int currentAtlasSlotCount,
    CValidationReport* report) const
{
    bool valid = success() && !profileMemberName.isEmpty();
    if(!valid)
    {
        if(report) report->add(
            "terrain.embedded_binding_incomplete",
            EValidationSeverity::Error,
            "The prevalidated embedded terrain-atlas binding is incomplete");
        return false;
    }
    if(normalizedPath(archivePath) != normalizedPath(activeArchivePath))
    {
        valid = false;
        if(report) report->add(
            "terrain.embedded_binding_archive_mismatch",
            EValidationSeverity::Error,
            "The prevalidated embedded terrain-atlas binding belongs to another RES archive");
    }
    if(decodedAtlasPixelSha256.toLower()
       != currentAtlasPixelSha256.toLower())
    {
        valid = false;
        if(report) report->add(
            "terrain.embedded_binding_pixel_mismatch",
            EValidationSeverity::Error,
            "The loaded terrain-atlas pixels no longer match the prevalidated RES archive");
    }
    return profile.validateBinding(
               currentNamespace, currentAtlasPixelSha256,
               currentAtlasSlotCount, report)
        && valid;
}

bool STerrainAtlasEmbeddedBindingResult::canReuseDecodedAtlasTiles(
    const QString& activeArchivePath,
    const QString& currentNamespace,
    int currentAtlasSlotCount,
    CValidationReport* report) const
{
    bool valid = currentAtlasSlotCount > 0
        && decodedAtlasTiles.size() == currentAtlasSlotCount
        && archiveSize >= 0 && archiveLastModifiedMs >= 0;
    const QFileInfo active(activeArchivePath);
    if(!active.isFile() || active.size() != archiveSize
       || active.lastModified().toMSecsSinceEpoch()
            != archiveLastModifiedMs)
    {
        valid = false;
        if(report) report->add(
            "terrain.embedded_binding_archive_changed",
            EValidationSeverity::Error,
            "The terrain RES changed after its exact atlas was decoded");
    }
    if(valid)
    {
        for(const QImage& tile : decodedAtlasTiles)
            if(tile.isNull() || tile.width() <= 0 || tile.width() != tile.height())
            {
                valid = false;
                break;
            }
    }
    if(!valid)
        return false;
    return matchesLoadedAtlas(
        activeArchivePath, currentNamespace, decodedAtlasPixelSha256,
        currentAtlasSlotCount, report);
}

STerrainAtlasEmbeddedBindingResult
CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
    const QStringList& candidateArchives,
    const QString& mprNamespace,
    const QString& preferredArchive)
{
    STerrainAtlasEmbeddedBindingResult result;
    if(mprNamespace.trimmed().isEmpty())
    {
        result.error = QStringLiteral(
            "The MPR has no terrain resource identity to look up.");
        return result;
    }
    QStringList ordered;
    if(QFileInfo(preferredArchive).isFile())
        ordered.append(QFileInfo(preferredArchive).absoluteFilePath());
    ordered.append(candidateArchives);
    const QString memberName =
        CTerrainAtlasProfileStore::embeddedMemberName(mprNamespace);
    QSet<QString> seen;
    QStringList rejected;
    for(const QString& candidate : ordered)
    {
        const QFileInfo archive(candidate);
        if(!archive.isFile())
            continue;
        const QString key = normalizedPath(archive.absoluteFilePath());
        if(seen.contains(key))
            continue;
        seen.insert(key);
        resIo::ReadLease lease(archive.absoluteFilePath(), &result.error);
        if(!lease.acquired()) return result;
        const quint64 generation = resIo::generation(archive.absoluteFilePath());
        QMap<QString, SArchiveMemberLocation> members;
        if(!archiveIndex(archive.absoluteFilePath(), &members))
            continue;
        const auto profileMember = members.constFind(memberName.toLower());
        if(profileMember == members.constEnd())
            continue;
        QFile file(archive.absoluteFilePath());
        if(!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray profileBytes = memberPayload(&file,
                                                       profileMember.value());
        file.close();
        lease.release();
        STerrainAtlasProfile profile;
        if(profileBytes.isEmpty()
           || !STerrainAtlasProfile::fromBytes(
               profileBytes, &profile, nullptr,
               archive.absoluteFilePath() + QStringLiteral(":") + memberName)
           || profile.mprNamespace.compare(mprNamespace,
                                           Qt::CaseInsensitive) != 0)
        {
            rejected.append(QStringLiteral("%1: invalid embedded profile")
                .arg(QDir::toNativeSeparators(archive.absoluteFilePath())));
            continue;
        }
        QVector<QImage> decodedTiles;
        const STerrainAtlasArchiveBindingResult exact = findExactArchive(
            {archive.absoluteFilePath()}, profile.mprNamespace,
            profile.textureCount, profile.atlasSlotCount,
            profile.atlasPixelSha256, archive.absoluteFilePath(),
            &decodedTiles);
        if(!exact.success())
        {
            if(exact.error.contains(QStringLiteral("reader-admission"))) {
                result.error=exact.error;
                return result;
            }
            rejected.append(QStringLiteral("%1: %2")
                .arg(QDir::toNativeSeparators(archive.absoluteFilePath()),
                     exact.error));
            continue;
        }
        if(generation != resIo::generation(archive.absoluteFilePath())) continue;
        result.archivePath = exact.archivePath;
        result.profileMemberName = memberName;
        result.profile = profile;
        result.decodedAtlasPixelSha256 = exact.decodedAtlasPixelSha256;
        result.decodedAtlasTiles = decodedTiles;
        result.archiveSize = exact.archiveSize;
        result.archiveLastModifiedMs = exact.archiveLastModifiedMs;
        return result;
    }
    result.error = QStringLiteral(
        "No configured RES archive contains a complete embedded terrain-atlas profile for this MPR identity.");
    if(!rejected.isEmpty())
        result.error += QStringLiteral("\nRejected candidates:\n%1")
            .arg(rejected.join(QStringLiteral("\n")));
    return result;
}

STerrainAtlasArchiveBindingResult
CTerrainAtlasArchiveBindingResolver::findExactArchive(
    const QStringList& candidateArchives,
    const QString& mprNamespace,
    int textureCount,
    int atlasSlotCount,
    const QByteArray& expectedAtlasPixelSha256,
    const QString& preferredArchive,
    QVector<QImage>* decodedTiles)
{
    STerrainAtlasArchiveBindingResult result;
    if(decodedTiles)
        decodedTiles->clear();
    if(mprNamespace.trimmed().isEmpty() || textureCount <= 0
       || atlasSlotCount <= 0 || atlasSlotCount > textureCount * 64
       || expectedAtlasPixelSha256.size() != 64)
    {
        result.error = QStringLiteral("The saved terrain-atlas binding is incomplete.");
        return result;
    }
    QStringList ordered;
    if(QFileInfo(preferredArchive).isFile())
        ordered.append(QFileInfo(preferredArchive).absoluteFilePath());
    ordered.append(candidateArchives);
    QSet<QString> seen;
    for(const QString& candidate : ordered)
    {
        const QFileInfo archive(candidate);
        if(!archive.isFile())
            continue;
        const QString key = normalizedPath(archive.absoluteFilePath());
        if(seen.contains(key))
            continue;
        seen.insert(key);
        resIo::ReadLease lease(archive.absoluteFilePath(), &result.error);
        if(!lease.acquired()) return result;
        const quint64 generation = resIo::generation(archive.absoluteFilePath());
        const qint64 archiveSize = QFileInfo(archive.absoluteFilePath()).size();
        const qint64 archiveLastModifiedMs =
            QFileInfo(archive.absoluteFilePath()).lastModified().toMSecsSinceEpoch();
        QMap<QString, SArchiveMemberLocation> members;
        if(!archiveIndex(archive.absoluteFilePath(), &members))
            continue;
        QVector<QByteArray> pages;
        QFile file(archive.absoluteFilePath());
        if(!file.open(QIODevice::ReadOnly))
            continue;
        bool complete = true;
        for(int page = 0; page < textureCount; ++page)
        {
            const QString name = QStringLiteral("%1%2.mmp")
                .arg(mprNamespace.toLower())
                .arg(page, 3, 10, QLatin1Char('0'));
            const auto found = members.constFind(name);
            if(found == members.constEnd())
            {
                complete = false;
                break;
            }
            const QByteArray payload = memberPayload(&file, found.value());
            if(payload.isEmpty())
            {
                complete = false;
                break;
            }
            pages.append(payload);
        }
        file.close();
        lease.release();
        if(!complete)
            continue;
        result.completeArchives.append(archive.absoluteFilePath());
        // Different reviewed bindings and aliases can request the same native
        // pages repeatedly. Cache only the proved pixel digest, keyed by every
        // compressed source byte and the exact physical layout. No pixels are
        // retained, and changed payloads invalidate even with unchanged metadata.
        QCryptographicHash sourceHash(QCryptographicHash::Sha256);
        sourceHash.addData(QByteArrayLiteral("native-atlas-pixels-v1\n"));
        sourceHash.addData(QByteArray::number(textureCount) + '\n'
                           + QByteArray::number(atlasSlotCount) + '\n');
        for(const auto& page:pages) {
            sourceHash.addData(QByteArray::number(page.size()) + '\n');
            sourceHash.addData(page);
        }
        const QByteArray sourceKey=sourceHash.result();
        QByteArray knownPixelHash;
        {
            QMutexLocker lock(&verifiedAtlasHashMutex);
            if(const auto* known=verifiedAtlasHashes.object(sourceKey))
                knownPixelHash=*known;
        }
        const auto sourceStillCurrent=[&]() {
            QFileInfo after(archive.absoluteFilePath());after.refresh();
            return generation==resIo::generation(archive.absoluteFilePath())
                &&after.isFile()&&after.size()==archiveSize
                &&after.lastModified().toMSecsSinceEpoch()==archiveLastModifiedMs;
        };
        if(!knownPixelHash.isEmpty()
           &&(!decodedTiles || knownPixelHash.toLower()!=expectedAtlasPixelSha256.toLower())) {
            if(!sourceStillCurrent())continue;
            result.completeArchivePixelSha256.insert(archive.absoluteFilePath(),knownPixelHash);
            if(knownPixelHash.toLower()!=expectedAtlasPixelSha256.toLower())continue;
            result.archivePath=archive.absoluteFilePath();
            result.decodedAtlasPixelSha256=knownPixelHash;
            result.archiveSize=archiveSize;
            result.archiveLastModifiedMs=archiveLastModifiedMs;
            return result;
        }
        QVector<QImage> tiles;
        tiles.reserve(atlasSlotCount);
        QHash<uint, QVector<QImage>> uniqueTiles;
#ifdef Q_OS_WIN
        const auto mapped = SMappedTerrainPixels::create(nativeAtlasPixelBytes(pages, atlasSlotCount));
#endif
        QCryptographicHash atlasHash(QCryptographicHash::Sha256);
        atlasHash.addData(QByteArray::number(atlasSlotCount)); atlasHash.addData("\n", 1);
        QString diagnostic;
        for(int slot = 0; slot < atlasSlotCount; ++slot)
        {
            QImage tile;
            if(!ei::decodeMmpDxt1AtlasTile(
                   pages.at(slot / 64), slot % 64, &tile, &diagnostic))
            {
                tiles.clear();
                break;
            }
            tile = tile.convertToFormat(QImage::Format_RGBA8888);
            // Same serialization as pixelSha256(), evaluated while one decoded
            // tile is already resident. Do not fault the whole mapped atlas back
            // into RAM merely to hash the final image vector a second time.
            atlasHash.addData(QByteArray::number(tile.width()) + 'x'
                              + QByteArray::number(tile.height()));
            atlasHash.addData("\n", 1);
            for(int y = 0; y < tile.height(); ++y)
                atlasHash.addData(reinterpret_cast<const char*>(tile.constScanLine(y)), tile.width() * 4);
            // Exact QImage comparison guards every bucket collision. The
            // saved atlas identity above remains SHA-256 without hashing
            // every pixel cryptographically twice just for deduplication.
            auto& candidates = uniqueTiles[decodedTileBucket(pages.at(slot / 64), slot % 64, tile)];
            bool shared = false;
            for(const QImage& candidate : candidates)
                if(candidate == tile)
                {
                    tile = candidate;
                    shared = true;
                    break;
                }
            if(!shared)
            {
#ifdef Q_OS_WIN
                if(mapped) tile = mapped->storeTile(tile, mapped);
#endif
                candidates.append(tile);
            }
            tiles.append(tile);
            // Decoding is complete for this page; do not retain all authored
            // BC1 mip payloads beside the growing native RGBA tile vector.
            if(slot % 64 == 63 || slot + 1 == atlasSlotCount)
            {
                pages[slot / 64].clear();
#ifdef Q_OS_WIN
                if(mapped) mapped->discardResidentPages();
#endif
            }
        }
        if(tiles.size() != atlasSlotCount)
            continue;
        const QByteArray actual = atlasHash.result().toHex();
#ifdef Q_OS_WIN
        if(mapped) mapped->discardResidentPages(true);
#endif
        QFileInfo archiveAfterRead(archive.absoluteFilePath());
        archiveAfterRead.refresh();
        if(generation != resIo::generation(archive.absoluteFilePath())
           || !archiveAfterRead.isFile()
           || archiveAfterRead.size() != archiveSize
           || archiveAfterRead.lastModified().toMSecsSinceEpoch()
                != archiveLastModifiedMs)
            continue;
        result.completeArchivePixelSha256.insert(
            archive.absoluteFilePath(), actual);
        {
            QMutexLocker lock(&verifiedAtlasHashMutex);
            verifiedAtlasHashes.insert(sourceKey,new QByteArray(actual));
        }
        if(actual.toLower() != expectedAtlasPixelSha256.toLower())
            continue;
        result.archivePath = archive.absoluteFilePath();
        result.decodedAtlasPixelSha256 = actual;
        result.archiveSize = archiveSize;
        result.archiveLastModifiedMs = archiveLastModifiedMs;
        if(decodedTiles)
            *decodedTiles = tiles;
        return result;
    }
    result.error = QStringLiteral(
        "No configured RES archive contains one complete terrain atlas matching the saved profile.");
    if(!result.completeArchivePixelSha256.isEmpty())
    {
        QStringList mismatches;
        for(auto found = result.completeArchivePixelSha256.constBegin();
            found != result.completeArchivePixelSha256.constEnd(); ++found)
            mismatches.append(QStringLiteral("%1 -> %2")
                .arg(QDir::toNativeSeparators(found.key()),
                     QString::fromLatin1(found.value())));
        result.error += QStringLiteral("\nComplete archive hashes:\n%1")
            .arg(mismatches.join(QStringLiteral("\n")));
    }
    return result;
}

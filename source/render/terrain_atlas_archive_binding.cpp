#include "terrain_atlas_archive_binding.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTextCodec>

#include "mmp_codec.h"
#include "endurance_document.h"
#include "res_file.h"
#include "terrain_atlas_profile.h"

namespace {
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
        const QSize tileSize = decodedAtlasTiles.front().size();
        valid = tileSize.width() > 0 && tileSize.width() == tileSize.height();
        for(const QImage& tile : decodedAtlasTiles)
            if(tile.isNull() || tile.width() != tile.height())
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
            rejected.append(QStringLiteral("%1: %2")
                .arg(QDir::toNativeSeparators(archive.absoluteFilePath()),
                     exact.error));
            continue;
        }
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
        const qint64 archiveSize = archive.size();
        const qint64 archiveLastModifiedMs =
            archive.lastModified().toMSecsSinceEpoch();
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
        if(!complete)
            continue;
        result.completeArchives.append(archive.absoluteFilePath());
        QVector<QImage> tiles;
        tiles.reserve(atlasSlotCount);
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
            tiles.append(tile.convertToFormat(QImage::Format_RGBA8888));
        }
        if(tiles.size() != atlasSlotCount)
            continue;
        const QByteArray actual =
            CTerrainAtlasProfileInference::pixelSha256(tiles);
        QFileInfo archiveAfterRead(archive.absoluteFilePath());
        archiveAfterRead.refresh();
        if(!archiveAfterRead.isFile()
           || archiveAfterRead.size() != archiveSize
           || archiveAfterRead.lastModified().toMSecsSinceEpoch()
                != archiveLastModifiedMs)
            continue;
        result.completeArchivePixelSha256.insert(
            archive.absoluteFilePath(), actual);
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

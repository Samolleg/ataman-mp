#include "terrain_atlas_archive_binding.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTextCodec>

#include "mmp_codec.h"
#include "res_file.h"
#include "terrain_atlas_profile.h"

namespace {
struct SArchiveMemberLocation
{
    quint32 offset{0};
    quint32 size{0};
};

bool archiveIndex(const QString& path,
                  QMap<QString, SArchiveMemberLocation>* members)
{
    if(!members)
        return false;
    members->clear();
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return false;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    SResFileHeader header;
    stream >> header;
    if(header.Signature != 0x019CE23C || header.TableSize > 10000000
       || quint64(header.TableOffset) > quint64(file.size()))
        return false;
    if(!file.seek(header.TableOffset))
        return false;
    QVector<SResHashTable> table(int(header.TableSize));
    for(SResHashTable& entry : table)
        stream >> entry;
    const QByteArray names = file.read(header.NamesLenght);
    if(names.size() != int(header.NamesLenght))
        return false;
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

bool STerrainAtlasArchiveBindingResult::hasCompleteArchive(
    const QString& path) const
{
    const QString wanted = normalizedPath(path);
    for(const QString& complete : completeArchives)
        if(normalizedPath(complete) == wanted)
            return true;
    return false;
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
        const STerrainAtlasArchiveBindingResult exact = findExactArchive(
            {archive.absoluteFilePath()}, profile.mprNamespace,
            profile.textureCount, profile.atlasSlotCount,
            profile.atlasPixelSha256, archive.absoluteFilePath());
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
    const QString& preferredArchive)
{
    STerrainAtlasArchiveBindingResult result;
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
        result.completeArchivePixelSha256.insert(
            archive.absoluteFilePath(), actual);
        if(actual.toLower() != expectedAtlasPixelSha256.toLower())
            continue;
        result.archivePath = archive.absoluteFilePath();
        result.decodedAtlasPixelSha256 = actual;
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

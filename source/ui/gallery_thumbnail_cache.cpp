#include "gallery_thumbnail_cache.h"
#include "persistent_visual_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
const QByteArray magic("EIGTHUMB1\n");
constexpr int width = 192, height = 132, pixelBytes = width * height * 4;
QMutex diskMutex;
QHash<QString,qint64> directoryBytes;
QHash<QString,int> directoryWrites;
bool safeDirectory(const QString& directory)
{
    if(directory.isEmpty()) return false;
    QFileInfo ancestor(QDir(directory).absolutePath());
    while(true) {
        if(ancestor.isSymLink()) return false;
        const QString parent = ancestor.dir().absolutePath();
        if(parent == ancestor.absoluteFilePath()) break;
        ancestor.setFile(parent);
    }
    return true;
}
bool validKey(const QString& key)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(key).hasMatch();
}
QString entryPath(const QString& directory, const QString& key)
{
    return QDir(directory).filePath(key + QStringLiteral(".thumb"));
}
qint64 prune(const QString& directory, qint64 budget)
{
    // Only this format's flat entries; never recurse into character-* or user folders.
    const auto entries = QDir(directory).entryInfoList({QStringLiteral("*.thumb")},
        QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for(const auto& entry : entries) if(validKey(entry.completeBaseName())) total += entry.size();
    for(const auto& entry : entries) {
        if(total <= budget) break;
        if(validKey(entry.completeBaseName()) && QFile::remove(entry.absoluteFilePath())) total -= entry.size();
    }
    return total;
}
}
namespace ei {
QString galleryThumbnailDirectory()
{
    // Same installation-relative user-cache location as character-*; avoid the
    // character-cache helper's unrelated deprecated-cache cleanup side effect.
    return visualCacheDirectoryForApplication(QCoreApplication::applicationDirPath(),
                                              QStringLiteral("gallery-thumbnails"));
}
QString galleryThumbnailKey(const QString& resourceNamespace, const QString& profile,
                            const QString& model, const QString& texture, int format)
{
    QByteArray identity; QDataStream out(&identity, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_12);
    out << QStringLiteral("gallery-cpu-argb32-le") << format << resourceNamespace << profile
        << model.toCaseFolded() << texture.toCaseFolded() << width << height
        << quint32(0xff303338) << QStringLiteral("frame86-rot62-40-uv-bounds-v1");
    // The CPU image has a fixed background; UI palette/theme does not affect it.
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
bool readGalleryThumbnail(const QString& directory, const QString& key, QImage* image)
{
    if(!image || !validKey(key) || !safeDirectory(directory)) return false;
    QMutexLocker lock(&diskMutex);
    const QString path = entryPath(directory, key);
    if(QFileInfo(path).isSymLink()) return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size() != magic.size() + 64 + 32 + pixelBytes) return false;
    const QByteArray header = file.read(magic.size() + 64 + 32);
    if(!header.startsWith(magic + key.toLatin1())) return false;
    const QByteArray payload = file.read(pixelBytes);
    if(payload.size() != pixelBytes || header.right(32) != QCryptographicHash::hash(payload, QCryptographicHash::Sha256)) return false;
    // Fixed dimensions/length prevent corrupt records from causing unbounded allocations.
    *image = QImage(reinterpret_cast<const uchar*>(payload.constData()), width, height,
                    width * 4, QImage::Format_ARGB32).copy();
    if(image->isNull()) return false;
    // Windows read-only handles cannot update timestamps. Recency is best effort
    // (a read-only cache remains readable); no payload bytes are changed.
    file.close();
    if(file.open(QIODevice::ReadWrite))
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return true;
}
bool writeGalleryThumbnail(const QString& directory, const QString& key, const QImage& image, qint64 budget)
{
    if(!validKey(key) || !safeDirectory(directory) || image.isNull() || image.size() != QSize(width, height)) return false;
    QMutexLocker lock(&diskMutex);
    if(!QDir().mkpath(directory)) return false;
    const QString path = entryPath(directory, key);
    if(QFileInfo(path).isSymLink()) return false;
    if(!directoryBytes.contains(directory))
        directoryBytes.insert(directory,prune(directory, qMax<qint64>(0,budget)));
    const qint64 previousBytes=QFileInfo(path).size();
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
    const QByteArray payload(reinterpret_cast<const char*>(pixels.constBits()), pixelBytes);
    const QByteArray record = magic + key.toLatin1() + QCryptographicHash::hash(payload, QCryptographicHash::Sha256) + payload;
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly) || file.write(record) != record.size() || !file.commit()) return false;
    directoryBytes[directory]+=record.size()-previousBytes;
    // No directory walk per thumbnail. Periodically refresh accounting for
    // another process's writes/deletions; prune immediately at our budget.
    if(directoryBytes.value(directory)>budget || ++directoryWrites[directory]>=64) {
        directoryBytes[directory]=prune(directory,qMax<qint64>(0,budget));
        directoryWrites[directory]=0;
    }
    return true;
}
}

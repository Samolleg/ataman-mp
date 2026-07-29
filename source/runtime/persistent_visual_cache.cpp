#include "persistent_visual_cache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <algorithm>

namespace {

const quint32 kContainerMagic = 0x43495645; // EIVC
const int kContainerSchema = 2;
// Final decoded/composited game textures are uploaded as RGBA8. Persist the
// GPU-ready 8-bit representation instead of the old RGBA64 cache payload,
// which doubled disk traffic and decompression work without preserving any
// additional source precision.
const quint32 kImageSchema = 2;
const quint32 kAssemblySchema = 1;
const int kMaximumPayloadBytes = 512 * 1024 * 1024;
const int kMaximumParts = 4096;
const int kMaximumVertices = 16 * 1024 * 1024;
QMutex s_cacheMutex;
QMutex s_pruneMutex;
QSet<QString> s_preparedDirectories;
QSet<QString> s_cacheRoots;
QHash<QString, qint64> s_cacheRootBytes;
int s_visualCacheLimitMiB = 4096;
bool s_deprecatedAssemblyCacheChecked = false;

qint64 configuredCacheBudget()
{
    QMutexLocker locker(&s_cacheMutex);
    return s_visualCacheLimitMiB <= 0 ? 0
        : qint64(s_visualCacheLimitMiB) * 1024ll * 1024ll;
}

bool isBudgetedCacheFile(const QFileInfo& entry)
{
    if(entry.fileName().compare(QStringLiteral("cache-manifest.json"),
                                Qt::CaseInsensitive) == 0)
        return false;
    const QString suffix = entry.suffix().toLower();
    return suffix == QStringLiteral("cache") || suffix == QStringLiteral("json");
}

qint64 cacheRootBytes(const QString& rootPath)
{
    qint64 total = 0;
    QDirIterator iterator(rootPath, QDir::Files, QDirIterator::Subdirectories);
    while(iterator.hasNext())
    {
        const QFileInfo entry(iterator.next());
        if(isBudgetedCacheFile(entry))
            total += entry.size();
    }
    return total;
}

void pruneCacheRoot(const QString& categoryPath)
{
    const qint64 budget = configuredCacheBudget();
    if(budget <= 0)
        return;
    QMutexLocker pruneLocker(&s_pruneMutex);
    const QString rootPath = QFileInfo(categoryPath).dir().absolutePath();
    QFileInfoList entries;
    QDirIterator iterator(rootPath, QDir::Files, QDirIterator::Subdirectories);
    while(iterator.hasNext())
    {
        const QFileInfo entry(iterator.next());
        if(isBudgetedCacheFile(entry))
            entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const QFileInfo& left, const QFileInfo& right)
    {
        return left.lastModified() < right.lastModified();
    });
    qint64 total = 0;
    for(const QFileInfo& entry : entries)
        total += entry.size();
    for(const QFileInfo& entry : entries)
    {
        if(total <= budget)
            break;
        const qint64 size = entry.size();
        if(QFile::remove(entry.absoluteFilePath()))
            total -= size;
    }
    QMutexLocker locker(&s_cacheMutex);
    s_cacheRootBytes.insert(QDir::cleanPath(rootPath).toCaseFolded(), total);
}

bool prepareDirectory(const QString& path)
{
    if(path.isEmpty())
        return false;
    const QString normalized = QDir::cleanPath(path).toCaseFolded();
    {
        QMutexLocker locker(&s_cacheMutex);
        if(s_preparedDirectories.contains(normalized))
            return true;
    }
    QDir directory(path);
    const QString manifestPath = directory.filePath(QStringLiteral("cache-manifest.json"));
    bool compatible = false;
    QFile manifest(manifestPath);
    if(manifest.open(QIODevice::ReadOnly))
    {
        const QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
        compatible = root.value(QStringLiteral("schema")).toInt(-1) == kContainerSchema;
    }
    if(!compatible && directory.exists() && !directory.removeRecursively())
        return false;
    if(!QDir().mkpath(path))
        return false;
    if(!compatible)
    {
        QSaveFile output(manifestPath);
        if(!output.open(QIODevice::WriteOnly))
            return false;
        QJsonObject root;
        root.insert(QStringLiteral("schema"), kContainerSchema);
        root.insert(QStringLiteral("format"), QStringLiteral("EI Mapper Endurance visual cache"));
        output.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        if(!output.commit())
            return false;
    }
    const QString rootPath = QFileInfo(path).dir().absolutePath();
    const QString normalizedRoot = QDir::cleanPath(rootPath).toCaseFolded();
    bool initializeRootSize = false;
    {
        QMutexLocker locker(&s_cacheMutex);
        s_preparedDirectories.insert(normalized);
        s_cacheRoots.insert(rootPath);
        initializeRootSize = !s_cacheRootBytes.contains(normalizedRoot);
    }
    if(initializeRootSize)
    {
        const qint64 total = cacheRootBytes(rootPath);
        QMutexLocker locker(&s_cacheMutex);
        s_cacheRootBytes.insert(normalizedRoot, total);
    }
    bool overBudget = false;
    {
        QMutexLocker locker(&s_cacheMutex);
        const qint64 budget = s_visualCacheLimitMiB <= 0 ? 0
            : qint64(s_visualCacheLimitMiB) * 1024ll * 1024ll;
        overBudget = budget > 0 && s_cacheRootBytes.value(normalizedRoot) > budget;
    }
    if(overBudget)
        pruneCacheRoot(path);
    return true;
}

QString entryPath(const QString& directory, const QString& key)
{
    return QDir(directory).filePath(key + QStringLiteral(".cache"));
}

void writeVector3(QDataStream& out, const QVector3D& value)
{
    out << value.x() << value.y() << value.z();
}

bool readVector3(QDataStream& in, QVector3D* value)
{
    float x = 0, y = 0, z = 0;
    in >> x >> y >> z;
    if(in.status() != QDataStream::Ok)
        return false;
    *value = QVector3D(x, y, z);
    return true;
}

}

namespace ei {

void setVisualCacheLimitMiB(int limitMiB)
{
    QStringList roots;
    {
        QMutexLocker locker(&s_cacheMutex);
        s_visualCacheLimitMiB = qMax(0, limitMiB);
        roots = s_cacheRoots.values();
    }
    for(const QString& root : roots)
        pruneCacheRoot(QDir(root).filePath(QStringLiteral("character-plans")));
}

int visualCacheLimitMiB()
{
    QMutexLocker locker(&s_cacheMutex);
    return s_visualCacheLimitMiB;
}

void recordVisualCacheFileWrite(const QString& directory, qint64 previousBytes,
                                qint64 currentBytes)
{
    if(directory.isEmpty())
        return;
    const QString rootPath = QFileInfo(directory).dir().absolutePath();
    const QString normalizedRoot = QDir::cleanPath(rootPath).toCaseFolded();
    bool initializeRootSize = false;
    bool overBudget = false;
    {
        QMutexLocker locker(&s_cacheMutex);
        s_cacheRoots.insert(rootPath);
        initializeRootSize = !s_cacheRootBytes.contains(normalizedRoot);
        if(!initializeRootSize)
        {
            const qint64 updated = qMax<qint64>(0, s_cacheRootBytes.value(normalizedRoot)
                - qMax<qint64>(0, previousBytes) + qMax<qint64>(0, currentBytes));
            s_cacheRootBytes.insert(normalizedRoot, updated);
            const qint64 budget = s_visualCacheLimitMiB <= 0 ? 0
                : qint64(s_visualCacheLimitMiB) * 1024ll * 1024ll;
            overBudget = budget > 0 && updated > budget;
        }
    }
    if(initializeRootSize)
    {
        const qint64 total = cacheRootBytes(rootPath);
        QMutexLocker locker(&s_cacheMutex);
        s_cacheRootBytes.insert(normalizedRoot, total);
        const qint64 budget = s_visualCacheLimitMiB <= 0 ? 0
            : qint64(s_visualCacheLimitMiB) * 1024ll * 1024ll;
        overBudget = budget > 0 && total > budget;
    }
    if(overBudget)
        pruneCacheRoot(directory);
}

void CVisualCacheLru::touch(const QString& key, qint64 costBytes)
{
    if(key.isEmpty())
        return;
    const qint64 boundedCost = qMax<qint64>(0, costBytes);
    m_totalBytes -= m_costs.value(key, 0);
    m_oldestFirst.removeAll(key);
    m_costs.insert(key, boundedCost);
    m_oldestFirst.append(key);
    m_totalBytes += boundedCost;
}

void CVisualCacheLru::touchExisting(const QString& key)
{
    if(!m_costs.contains(key))
        return;
    m_oldestFirst.removeAll(key);
    m_oldestFirst.append(key);
}

QStringList CVisualCacheLru::trim(qint64 maximumBytes)
{
    QStringList removed;
    const qint64 budget = qMax<qint64>(0, maximumBytes);
    while(m_totalBytes > budget && !m_oldestFirst.isEmpty())
    {
        const QString key = m_oldestFirst.takeFirst();
        m_totalBytes -= m_costs.take(key);
        removed.append(key);
    }
    return removed;
}

void CVisualCacheLru::remove(const QString& key)
{
    m_totalBytes -= m_costs.take(key);
    m_oldestFirst.removeAll(key);
}

void CVisualCacheLru::clear()
{
    m_costs.clear();
    m_oldestFirst.clear();
    m_totalBytes = 0;
}

QString visualCacheDirectoryForApplication(const QString& applicationDirectory,
                                           const QString& category)
{
    if(applicationDirectory.isEmpty() || category.isEmpty())
        return QString();
    return QDir(applicationDirectory).filePath(QStringLiteral("cache/") + category);
}

QString visualCacheDirectory(const QString& category)
{
    if(!QCoreApplication::instance())
        return QString();
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    bool checkDeprecated = false;
    {
        QMutexLocker locker(&s_cacheMutex);
        if(!s_deprecatedAssemblyCacheChecked)
        {
            s_deprecatedAssemblyCacheChecked = true;
            checkDeprecated = true;
        }
    }
    if(checkDeprecated)
    {
        // The retired assembly cache may contain thousands of large files.
        // Deleting it synchronously on the first character finalization would
        // make the viewport appear frozen. It is derived-only data and no
        // current reader can open it, so reclaim it on a one-shot background
        // thread and retry on the next launch if process exit interrupts it.
        QThread* cleanup = QThread::create([applicationDirectory]() {
            removeDeprecatedVisualAssemblyCacheForApplication(applicationDirectory);
        });
        QObject::connect(cleanup, &QThread::finished,
                         QCoreApplication::instance(), [cleanup]() {
            cleanup->deleteLater();
        });
        cleanup->start();
    }
    return visualCacheDirectoryForApplication(applicationDirectory, category);
}

bool removeDeprecatedVisualAssemblyCacheForApplication(const QString& applicationDirectory)
{
    if(applicationDirectory.isEmpty())
        return false;
    QDir obsolete(visualCacheDirectoryForApplication(
        applicationDirectory, QStringLiteral("character-assemblies")));
    return !obsolete.exists() || obsolete.removeRecursively();
}

QString visualCacheKey(const QByteArray& identity)
{
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

bool readVisualCachePayload(const QString& directory, const QString& key,
                            quint32 payloadSchema, QByteArray* payload)
{
    if(!payload || !prepareDirectory(directory))
        return false;
    const QString path = entryPath(directory, key);
    QFile input(path);
    if(!input.open(QIODevice::ReadOnly))
        return false;
    QDataStream stream(&input);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, schema = 0, unpackedSize = 0;
    QByteArray digest, compressed;
    stream >> magic >> schema >> unpackedSize >> digest >> compressed;
    if(stream.status() != QDataStream::Ok || magic != kContainerMagic
            || schema != payloadSchema || unpackedSize > quint32(kMaximumPayloadBytes)
            || digest.size() != 32)
    {
        input.close();
        QFile::remove(path);
        return false;
    }
    const QByteArray unpacked = qUncompress(compressed);
    if(unpacked.size() != int(unpackedSize)
            || QCryptographicHash::hash(unpacked, QCryptographicHash::Sha256) != digest)
    {
        input.close();
        QFile::remove(path);
        return false;
    }
    *payload = unpacked;
    input.close();
    QFile touch(path);
    if(touch.open(QIODevice::ReadOnly))
        touch.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return true;
}

bool writeVisualCachePayload(const QString& directory, const QString& key,
                             quint32 payloadSchema, const QByteArray& payload)
{
    if(payload.size() > kMaximumPayloadBytes || !prepareDirectory(directory))
        return false;
    const QString path = entryPath(directory, key);
    const qint64 previousSize = QFileInfo(path).size();
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kContainerMagic << payloadSchema << quint32(payload.size())
           << QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
           << qCompress(payload, 1);
    if(stream.status() != QDataStream::Ok || !output.commit())
        return false;
    recordVisualCacheFileWrite(directory, previousSize, QFileInfo(path).size());
    return true;
}

bool readCachedVisualImage(const QString& directory, const QString& key, QImage* image)
{
    QByteArray payload;
    if(!image || !readVisualCachePayload(directory, key, kImageSchema, &payload))
        return false;
    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 width = 0, height = 0;
    QByteArray pixels;
    stream >> width >> height >> pixels;
    const quint64 expected = quint64(width) * quint64(height) * 4;
    if(stream.status() != QDataStream::Ok || width == 0 || height == 0
            || width > 16384 || height > 16384 || expected != quint64(pixels.size()))
    {
        QFile::remove(entryPath(directory, key));
        return false;
    }
    QImage decoded(int(width), int(height), QImage::Format_RGBA8888);
    for(int y = 0; y < decoded.height(); ++y)
        memcpy(decoded.scanLine(y), pixels.constData() + y * decoded.width() * 4,
               size_t(decoded.width() * 4));
    *image = decoded;
    return true;
}

bool writeCachedVisualImage(const QString& directory, const QString& key, const QImage& image)
{
    if(image.isNull())
        return false;
    const QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray pixels(converted.width() * converted.height() * 4, Qt::Uninitialized);
    for(int y = 0; y < converted.height(); ++y)
        memcpy(pixels.data() + y * converted.width() * 4, converted.constScanLine(y),
               size_t(converted.width() * 4));
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(converted.width()) << quint32(converted.height()) << pixels;
    return stream.status() == QDataStream::Ok
        && writeVisualCachePayload(directory, key, kImageSchema, payload);
}

bool readCachedFigureAssembly(const QString& directory, const QString& key,
                              QVector<SCachedVisualPart>* parts)
{
    QByteArray payload;
    if(!parts || !readVisualCachePayload(directory, key, kAssemblySchema, &payload))
        return false;
    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 count = 0;
    stream >> count;
    if(count > quint32(kMaximumParts))
        return false;
    QVector<SCachedVisualPart> decoded;
    decoded.reserve(int(count));
    for(quint32 i = 0; i < count; ++i)
    {
        SCachedVisualPart part;
        qint32 textureNumber = 0;
        quint32 vertexCount = 0, sourceCount = 0;
        stream >> part.name >> part.parentName;
        if(!readVector3(stream, &part.restOffset))
            return false;
        stream >> textureNumber >> vertexCount;
        if(vertexCount > quint32(kMaximumVertices))
            return false;
        part.textureNumber = textureNumber;
        part.vertices.reserve(int(vertexCount));
        for(quint32 vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            SVertexData vertex;
            if(!readVector3(stream, &vertex.position) || !readVector3(stream, &vertex.normal))
                return false;
            float u = 0, v = 0;
            stream >> u >> v;
            vertex.texCoord = QVector2D(u, v);
            part.vertices.append(vertex);
        }
        stream >> sourceCount;
        if(sourceCount > vertexCount)
            return false;
        part.sourceVertexIndices.reserve(int(sourceCount));
        for(quint32 sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
        {
            qint32 value = 0;
            stream >> value;
            part.sourceVertexIndices.append(value);
        }
        if(stream.status() != QDataStream::Ok)
            return false;
        decoded.append(part);
    }
    if(stream.status() != QDataStream::Ok || !stream.atEnd())
        return false;
    *parts = decoded;
    return true;
}

bool writeCachedFigureAssembly(const QString& directory, const QString& key,
                               const QVector<SCachedVisualPart>& parts)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(parts.size());
    for(const SCachedVisualPart& part : parts)
    {
        stream << part.name << part.parentName;
        writeVector3(stream, part.restOffset);
        stream << qint32(part.textureNumber) << quint32(part.vertices.size());
        for(const SVertexData& vertex : part.vertices)
        {
            writeVector3(stream, vertex.position);
            writeVector3(stream, vertex.normal);
            stream << vertex.texCoord.x() << vertex.texCoord.y();
        }
        stream << quint32(part.sourceVertexIndices.size());
        for(int sourceIndex : part.sourceVertexIndices)
            stream << qint32(sourceIndex);
    }
    return stream.status() == QDataStream::Ok
        && writeVisualCachePayload(directory, key, kAssemblySchema, payload);
}

}

#include "persistent_visual_cache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

const quint32 kContainerMagic = 0x43495645; // EIVC
const int kContainerSchema = 1;
const quint32 kImageSchema = 1;
const quint32 kAssemblySchema = 1;
const int kMaximumPayloadBytes = 512 * 1024 * 1024;
const int kMaximumParts = 4096;
const int kMaximumVertices = 16 * 1024 * 1024;

bool prepareDirectory(const QString& path)
{
    if(path.isEmpty())
        return false;
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
    return visualCacheDirectoryForApplication(QCoreApplication::applicationDirPath(), category);
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
    return true;
}

bool writeVisualCachePayload(const QString& directory, const QString& key,
                             quint32 payloadSchema, const QByteArray& payload)
{
    if(payload.size() > kMaximumPayloadBytes || !prepareDirectory(directory))
        return false;
    QSaveFile output(entryPath(directory, key));
    if(!output.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kContainerMagic << payloadSchema << quint32(payload.size())
           << QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
           << qCompress(payload, 1);
    return stream.status() == QDataStream::Ok && output.commit();
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
    const quint64 expected = quint64(width) * quint64(height) * 8;
    if(stream.status() != QDataStream::Ok || width == 0 || height == 0
            || width > 16384 || height > 16384 || expected != quint64(pixels.size()))
    {
        QFile::remove(entryPath(directory, key));
        return false;
    }
    QImage decoded(int(width), int(height), QImage::Format_RGBA64);
    for(int y = 0; y < decoded.height(); ++y)
        memcpy(decoded.scanLine(y), pixels.constData() + y * decoded.width() * 8,
               size_t(decoded.width() * 8));
    *image = decoded;
    return true;
}

bool writeCachedVisualImage(const QString& directory, const QString& key, const QImage& image)
{
    if(image.isNull())
        return false;
    const QImage converted = image.convertToFormat(QImage::Format_RGBA64);
    QByteArray pixels(converted.width() * converted.height() * 8, Qt::Uninitialized);
    for(int y = 0; y < converted.height(); ++y)
        memcpy(pixels.data() + y * converted.width() * 8, converted.constScanLine(y),
               size_t(converted.width() * 8));
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

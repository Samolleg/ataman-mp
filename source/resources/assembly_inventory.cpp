#include "assembly_inventory.h"
#include "../render/figure_format.h"

#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTextCodec>
#include <QVector>

#include <cmath>
#include <limits>

#include "res_file.h"

namespace {

struct SCachedArchiveIndex
{
    qint64 size{-1};
    QDateTime modified;
    QMap<QString, SResFileEntry> entries;
};

QMutex& inventoryMutex()
{
    static QMutex mutex;
    return mutex;
}

QMap<QString, SCachedArchiveIndex>& archiveIndexes()
{
    static QMap<QString, SCachedArchiveIndex> indexes;
    return indexes;
}

QMap<QString, QStringList>& partNameCache()
{
    static QMap<QString, QStringList> cache;
    return cache;
}

struct SVerticalFigureGeometry
{
    QVector<QVector<float>> morphZ;
    QVector<float> accumulatedOffsetZ;
    QSet<int> referencedVertices;
    bool valid{false};
};

struct SHeadHairGeometry
{
    SVerticalFigureGeometry head;
    SVerticalFigureGeometry hair;
    bool valid{false};
};

QMap<QString, SHeadHairGeometry>& headHairGeometryCache()
{
    static QMap<QString, SHeadHairGeometry> cache;
    return cache;
}

bool readEntries(QDataStream& stream, qint64 streamSize,
                 QMap<QString, SResFileEntry>* entries)
{
    if(!entries || !stream.device())
        return false;
    entries->clear();
    const qint64 start = stream.device()->pos();
    SResFileHeader header;
    stream >> header;
    if(stream.status() != QDataStream::Ok || header.Signature != 0x019CE23C
       || header.TableSize > 100000 || header.TableOffset > quint64(streamSize - start))
        return false;
    const quint64 tableBytes = quint64(header.TableSize) * sizeof(SResHashTable);
    if(quint64(start) + header.TableOffset + tableBytes + header.NamesLenght
       > quint64(streamSize))
        return false;

    stream.device()->seek(start + header.TableOffset);
    QVector<SResHashTable> table(int(header.TableSize));
    for(SResHashTable& entry : table)
        stream >> entry;
    const QByteArray names = stream.device()->read(header.NamesLenght);
    if(stream.status() != QDataStream::Ok || names.size() != int(header.NamesLenght))
        return false;

    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(const SResHashTable& raw : table)
    {
        if(quint64(raw.NameOffset) + raw.NameLength > quint64(names.size())
           || quint64(raw.DataOffset) + raw.DataSize > quint64(streamSize - start))
            continue;
        SResFileEntry entry;
        entry.FileName = codec->toUnicode(
            names.mid(int(raw.NameOffset), int(raw.NameLength))).toLower();
        entry.Position = int(start + raw.DataOffset);
        entry.Size = int(raw.DataSize);
        entry.LastWriteTime = QDateTime::fromTime_t(raw.LastWriteTime);
        entries->insert(entry.FileName, entry);
    }
    return !entries->isEmpty();
}

const SCachedArchiveIndex& archiveIndex(const QString& path)
{
    const QFileInfo info(path);
    SCachedArchiveIndex& cached = archiveIndexes()[path];
    if(cached.size == info.size() && cached.modified == info.lastModified())
        return cached;
    cached = SCachedArchiveIndex();
    cached.size = info.size();
    cached.modified = info.lastModified();
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return cached;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    readEntries(stream, file.size(), &cached.entries);
    return cached;
}

bool readResource(const QStringList& sources, const QString& name, QByteArray* data)
{
    if(!data)
        return false;
    const QString folded = name.toLower();
    for(const QString& source : sources)
    {
        const SCachedArchiveIndex& index = archiveIndex(source);
        const auto found = index.entries.constFind(folded);
        if(found == index.entries.constEnd())
            continue;
        QFile file(source);
        if(!file.open(QIODevice::ReadOnly) || !file.seek(found->Position))
            continue;
        *data = file.read(found->Size);
        if(data->size() == found->Size)
            return true;
    }
    return false;
}

bool readLinkName(QDataStream& stream, QString* value, bool allowEmpty)
{
    if(!value)
        return false;
    int length = 0;
    stream >> length;
    if(stream.status() != QDataStream::Ok || length < 0 || length > 4096
       || (!allowEmpty && length == 0))
        return false;
    QByteArray raw(length, Qt::Uninitialized);
    if(length > 0 && stream.readRawData(raw.data(), length) != length)
        return false;
    while(!raw.isEmpty() && raw.endsWith('\0'))
        raw.chop(1);
    *value = QString::fromLatin1(raw).toLower();
    return allowEmpty || !value->isEmpty();
}

bool readAssemblyLinks(const QByteArray& archiveData,
                       const QMap<QString, SResFileEntry>& entries,
                       const QString& model,
                       QMap<QString, QString>* parents,
                       QStringList* orderedParts)
{
    if(!parents || !orderedParts)
        return false;
    parents->clear();
    orderedParts->clear();
    if(!entries.contains(model))
        return false;
    const SResFileEntry linkEntry = entries.value(model);
    if(linkEntry.Size <= 0)
        return false;
    const QByteArray linkData = archiveData.mid(linkEntry.Position, linkEntry.Size);
    QDataStream stream(linkData);
    stream.setByteOrder(QDataStream::LittleEndian);
    int count = 0;
    stream >> count;
    if(stream.status() != QDataStream::Ok || count <= 0 || count > 10000)
        return false;
    for(int index = 0; index < count; ++index)
    {
        QString part;
        QString parent;
        if(!readLinkName(stream, &part, false)
           || !readLinkName(stream, &parent, true))
            return false;
        parents->insert(part, parent);
        orderedParts->append(part);
    }
    return true;
}

bool readFigureVerticalGeometry(const QByteArray& data,
                                SVerticalFigureGeometry* geometry)
{
    if(!geometry)
        return false;
    *geometry = SVerticalFigureGeometry();
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 signature = 0;
    quint32 vertexBlocks = 0;
    quint32 normalBlocks = 0;
    quint32 uvCount = 0;
    quint32 indexCount = 0;
    quint32 componentCount = 0;
    quint32 morphComponentCount = 0;
    quint32 ignored = 0;
    stream >> signature >> vertexBlocks >> normalBlocks >> uvCount >> indexCount
           >> componentCount >> morphComponentCount >> ignored >> ignored >> ignored;
    const int formCount = ei::figureFormCount(signature);
    if(stream.status() != QDataStream::Ok || !formCount
       || vertexBlocks == 0 || vertexBlocks > 100000
       || normalBlocks > 100000 || uvCount > 1000000
       || indexCount > 3000000 || componentCount > 3000000
       || morphComponentCount > 3000000)
        return false;

    const qint64 headerTailBytes = qint64(formCount * 10) * qint64(sizeof(float));
    const qint64 vertexBytes = qint64(vertexBlocks) * 3 * formCount * 4 * qint64(sizeof(float));
    const qint64 normalBytes = qint64(normalBlocks) * 4 * 4 * qint64(sizeof(float));
    const qint64 uvBytes = qint64(uvCount) * 2 * qint64(sizeof(float));
    const qint64 indexBytes = qint64(indexCount) * qint64(sizeof(quint16));
    const qint64 componentBytes = qint64(componentCount) * 3 * qint64(sizeof(quint16));
    const qint64 morphBytes = qint64(morphComponentCount) * qint64(sizeof(quint32));
    const qint64 expected = 4 + 9 * qint64(sizeof(quint32)) + headerTailBytes
        + vertexBytes + normalBytes + uvBytes + indexBytes + componentBytes + morphBytes;
    if(expected > data.size() || !stream.device()->seek(4 + 9 * sizeof(quint32) + headerTailBytes))
        return false;

    geometry->morphZ.fill(QVector<float>(int(vertexBlocks * 4)), formCount);
    for(quint32 block = 0; block < vertexBlocks; ++block)
        for(int axis = 0; axis < 3; ++axis)
            for(int morph = 0; morph < formCount; ++morph)
                for(int lane = 0; lane < 4; ++lane)
                {
                    float value = 0.0f;
                    stream >> value;
                    if(stream.status() != QDataStream::Ok || !std::isfinite(value))
                        return false;
                    if(axis == 2)
                        geometry->morphZ[morph][int(block * 4 + quint32(lane))] = value;
                }

    if(!stream.device()->seek(stream.device()->pos() + normalBytes + uvBytes))
        return false;
    QVector<quint16> indices;
    indices.resize(int(indexCount));
    for(quint16& index : indices)
        stream >> index;
    QVector<quint16> componentVertices;
    componentVertices.resize(int(componentCount));
    for(quint16& vertex : componentVertices)
    {
        quint16 normal = 0;
        quint16 uv = 0;
        stream >> vertex >> normal >> uv;
    }
    if(stream.status() != QDataStream::Ok)
        return false;
    for(const quint16 componentIndex : indices)
    {
        if(componentIndex >= componentVertices.size())
            return false;
        const int vertex = int(componentVertices.at(componentIndex));
        if(vertex < 0 || vertex >= geometry->morphZ.first().size())
            return false;
        geometry->referencedVertices.insert(vertex);
    }
    if(geometry->referencedVertices.isEmpty())
        return false;
    geometry->accumulatedOffsetZ.fill(0.0f, 8);
    geometry->valid = true;
    return true;
}

bool readBonOffset(const QByteArray& archiveData,
                   const QMap<QString, SResFileEntry>& entries,
                   const QString& part,
                   QVector<float>* offsets)
{
    if(!offsets)
        return false;
    offsets->fill(0.0f, 8);
    if(!entries.contains(part))
        return true;
    const SResFileEntry entry = entries.value(part);
    if(entry.Size <= 0)
        return true;
    const QByteArray data = archiveData.mid(entry.Position, entry.Size);
    if(data.size() != 3 * int(sizeof(float)) && data.size() < 8 * 3 * int(sizeof(float)))
        return false;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    const int endpointCount = data.size() == 3 * int(sizeof(float)) ? 1 : 8;
    for(int morph = 0; morph < endpointCount; ++morph)
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        stream >> x >> y >> z;
        if(stream.status() != QDataStream::Ok || !std::isfinite(x)
           || !std::isfinite(y) || !std::isfinite(z))
            return false;
        (*offsets)[morph] = z;
    }
    if(endpointCount == 1) offsets->fill(offsets->first(), 8);
    return true;
}

bool accumulatedBonOffset(const QByteArray& bonData,
                          const QMap<QString, SResFileEntry>& bonEntries,
                          const QMap<QString, QString>& parents,
                          const QString& part,
                          QVector<float>* accumulated)
{
    if(!accumulated)
        return false;
    accumulated->fill(0.0f, 8);
    QString cursor = part;
    QSet<QString> visited;
    while(!cursor.isEmpty())
    {
        if(visited.contains(cursor) || !parents.contains(cursor))
            return false;
        visited.insert(cursor);
        QVector<float> local;
        if(!readBonOffset(bonData, bonEntries, cursor, &local))
            return false;
        for(int morph = 0; morph < 8; ++morph)
            (*accumulated)[morph] += local.at(morph);
        cursor = parents.value(cursor);
    }
    return true;
}

bool readHeadHairGeometry(const QStringList& sources, const QString& model,
                          const QString& hairPart, SHeadHairGeometry* result)
{
    if(!result)
        return false;
    *result = SHeadHairGeometry();
    QByteArray modelData;
    if(!readResource(sources, model + ".mod", &modelData))
        return false;
    QDataStream modelStream(modelData);
    modelStream.setByteOrder(QDataStream::LittleEndian);
    QMap<QString, SResFileEntry> modelEntries;
    if(!readEntries(modelStream, modelData.size(), &modelEntries))
        return false;
    QMap<QString, QString> parents;
    QStringList orderedParts;
    if(!readAssemblyLinks(modelData, modelEntries, model, &parents, &orderedParts)
       || !modelEntries.contains("hd") || !modelEntries.contains(hairPart))
        return false;
    const SResFileEntry headEntry = modelEntries.value("hd");
    const SResFileEntry hairEntry = modelEntries.value(hairPart);
    if(!readFigureVerticalGeometry(
           modelData.mid(headEntry.Position, headEntry.Size), &result->head)
       || !readFigureVerticalGeometry(
           modelData.mid(hairEntry.Position, hairEntry.Size), &result->hair))
        return false;

    QByteArray bonData;
    QMap<QString, SResFileEntry> bonEntries;
    if(readResource(sources, model + ".bon", &bonData))
    {
        QDataStream bonStream(bonData);
        bonStream.setByteOrder(QDataStream::LittleEndian);
        if(!readEntries(bonStream, bonData.size(), &bonEntries))
            return false;
    }
    if(!accumulatedBonOffset(bonData, bonEntries, parents, "hd",
                             &result->head.accumulatedOffsetZ)
       || !accumulatedBonOffset(bonData, bonEntries, parents, hairPart,
                                &result->hair.accumulatedOffsetZ))
        return false;
    result->valid = true;
    return true;
}

float interpolateMorph(const QVector<float>& values, float x, float y, float z)
{
    if(values.size() == 1) return values.first();
    const float lowerA = values.at(0) + (values.at(1) - values.at(0)) * y;
    const float lowerB = values.at(2) + (values.at(3) - values.at(2)) * y;
    const float lower = lowerA + (lowerB - lowerA) * x;
    const float upperA = values.at(4) + (values.at(5) - values.at(4)) * y;
    const float upperB = values.at(6) + (values.at(7) - values.at(6)) * y;
    const float upper = upperA + (upperB - upperA) * x;
    return lower + (upper - lower) * z;
}

bool crownHeight(const SVerticalFigureGeometry& geometry,
                 float x, float y, float z, float* height)
{
    if(!height || !geometry.valid
       || (geometry.morphZ.size() != 1 && geometry.morphZ.size() != 8)
       || geometry.accumulatedOffsetZ.size() != 8)
        return false;
    const float offset = interpolateMorph(geometry.accumulatedOffsetZ, x, y, z);
    float maximum = -std::numeric_limits<float>::infinity();
    QVector<float> vertexMorphs(geometry.morphZ.size());
    for(const int vertex : geometry.referencedVertices)
    {
        for(int morph = 0; morph < vertexMorphs.size(); ++morph)
            vertexMorphs[morph] = geometry.morphZ.at(morph).at(vertex);
        maximum = qMax(maximum, interpolateMorph(vertexMorphs, x, y, z) + offset);
    }
    if(!std::isfinite(maximum))
        return false;
    *height = maximum;
    return true;
}

QStringList readAssemblyPartNames(const QStringList& sources, const QString& model)
{
    QByteArray archiveData;
    if(!readResource(sources, model + ".mod", &archiveData))
        return {};
    QDataStream archiveStream(archiveData);
    archiveStream.setByteOrder(QDataStream::LittleEndian);
    QMap<QString, SResFileEntry> entries;
    if(!readEntries(archiveStream, archiveData.size(), &entries))
        return {};
    if(!entries.contains(model))
        return {};
    const SResFileEntry linkEntry = entries.value(model);
    if(linkEntry.Size <= 0)
        return {};

    const QByteArray linkData = archiveData.mid(linkEntry.Position, linkEntry.Size);
    QDataStream linkStream(linkData);
    linkStream.setByteOrder(QDataStream::LittleEndian);
    int linkCount = 0;
    linkStream >> linkCount;
    if(linkStream.status() != QDataStream::Ok || linkCount <= 0 || linkCount > 10000)
        return {};
    QStringList result;
    QSet<QString> seen;
    for(int index = 0; index < linkCount; ++index)
    {
        QString component;
        QString parent;
        if(!readLinkName(linkStream, &component, false)
           || !readLinkName(linkStream, &parent, true))
            return {};
        if(entries.contains(component) && !seen.contains(component))
        {
            seen.insert(component);
            result.append(component);
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

}

QStringList ei::assemblyPartNamesFromResourceSources(
    const QStringList& resourceSources,
    const QString& resourceFingerprint,
    const QString& modelName)
{
    QString model = modelName.trimmed().toLower();
    if(model.endsWith(".mod"))
        model.chop(4);
    if(model.isEmpty() || resourceSources.isEmpty())
        return {};
    const QString cacheKey = resourceFingerprint + '|' + model;
    QMutexLocker locker(&inventoryMutex());
    const auto cached = partNameCache().constFind(cacheKey);
    if(cached != partNameCache().constEnd())
        return cached.value();
    const QStringList result = readAssemblyPartNames(resourceSources, model);
    partNameCache().insert(cacheKey, result);
    return result;
}

ei::EHairCrownGeometry ei::assemblyHairCrownGeometryFromResourceSources(
    const QStringList& resourceSources,
    const QString& resourceFingerprint,
    const QString& modelName,
    const QString& hairPart,
    float complexionX,
    float complexionY,
    float complexionZ)
{
    QString model = modelName.trimmed().toLower();
    if(model.endsWith(".mod"))
        model.chop(4);
    const QString hair = hairPart.trimmed().toLower();
    if(model.isEmpty() || hair.isEmpty() || resourceSources.isEmpty()
       || !std::isfinite(complexionX) || !std::isfinite(complexionY)
       || !std::isfinite(complexionZ))
        return EHairCrownGeometry::Unavailable;

    const QString cacheKey = resourceFingerprint + '|' + model + '|' + hair;
    QMutexLocker locker(&inventoryMutex());
    auto cached = headHairGeometryCache().constFind(cacheKey);
    if(cached == headHairGeometryCache().constEnd())
    {
        SHeadHairGeometry geometry;
        readHeadHairGeometry(resourceSources, model, hair, &geometry);
        headHairGeometryCache().insert(cacheKey, geometry);
        cached = headHairGeometryCache().constFind(cacheKey);
    }
    if(cached == headHairGeometryCache().constEnd() || !cached->valid)
        return EHairCrownGeometry::Unavailable;

    float headCrown = 0.0f;
    float hairCrown = 0.0f;
    if(!crownHeight(cached->head, complexionX, complexionY, complexionZ, &headCrown)
       || !crownHeight(cached->hair, complexionX, complexionY, complexionZ, &hairCrown))
        return EHairCrownGeometry::Unavailable;
    const float scale = qMax(1.0f, qMax(std::fabs(headCrown), std::fabs(hairCrown)));
    const float numericalTolerance = 32.0f * std::numeric_limits<float>::epsilon() * scale;
    return hairCrown > headCrown + numericalTolerance
        ? EHairCrownGeometry::AboveHeadCrown
        : EHairCrownGeometry::AtOrBelowHeadCrown;
}

QString ei::assemblyHairGeometryKey(const QString& modelName,
                                    const QString& hairPart)
{
    QString model = modelName.trimmed().toLower();
    if(model.endsWith(".mod"))
        model.chop(4);
    return model + '|' + hairPart.trimmed().toLower();
}

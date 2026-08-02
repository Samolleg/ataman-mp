#include "assembly_inventory.h"

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
    auto readName = [&linkStream](QString* value, bool allowEmpty) -> bool
    {
        int length = 0;
        linkStream >> length;
        if(linkStream.status() != QDataStream::Ok || length < 0 || length > 4096
           || (!allowEmpty && length == 0))
            return false;
        QByteArray raw(length, Qt::Uninitialized);
        if(length > 0 && linkStream.readRawData(raw.data(), length) != length)
            return false;
        while(!raw.isEmpty() && raw.endsWith('\0'))
            raw.chop(1);
        *value = QString::fromLatin1(raw).toLower();
        return allowEmpty || !value->isEmpty();
    };

    QStringList result;
    QSet<QString> seen;
    for(int index = 0; index < linkCount; ++index)
    {
        QString component;
        QString parent;
        if(!readName(&component, false) || !readName(&parent, true))
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

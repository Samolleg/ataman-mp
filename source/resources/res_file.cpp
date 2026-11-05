#include "res_write_audit.h"
#include <QtConcurrent>
#include <QCryptographicHash>
#include "QDebug"
#include <QTextCodec>
#include <QSaveFile>
#include <limits>
#include "res_file.h"
#include "utils.h"

namespace {
bool rangeFits(qint64 start, quint64 offset, quint64 size, qint64 streamLength)
{
    if (start < 0 || streamLength < start)
        return false;

    const quint64 available = quint64(streamLength - start);
    return offset <= available && size <= available - offset;
}

bool readRawDataExactly(QDataStream& stream, char* data, int size)
{
    if (size < 0 || stream.status() != QDataStream::Ok)
        return false;

    return size == 0 ||
        (stream.readRawData(data, size) == size && stream.status() == QDataStream::Ok);
}
}

bool CResFile::buildFileMap(
        QMap<QString, SResFileEntry>& aEntry,
        const SResHashTable& file, qint64 offsetStream,
        qint64 streamLength, const QByteArray& nameBuf)
{
    if (!rangeFits(offsetStream, file.DataOffset, file.DataSize, streamLength))
        return false;

    const quint64 namesLength = quint64(nameBuf.size());
    if (quint64(file.NameOffset) > namesLength ||
        quint64(file.NameLength) > namesLength - quint64(file.NameOffset))
    {
        return false;
    }

    const qint64 position = offsetStream + qint64(file.DataOffset);
    if (position > std::numeric_limits<int>::max() ||
        file.DataSize > uint(std::numeric_limits<int>::max()))
    {
        return false;
    }

    const int nameOffset = int(file.NameOffset);
    const int nameLength = int(file.NameLength);
    const char* encodedName = nameLength == 0 ? "" : nameBuf.constData() + nameOffset;
    QTextCodec* codec = QTextCodec::codecForName("CP1251");

    SResFileEntry entry;
    entry.FileName = codec ? codec->toUnicode(encodedName, nameLength)
                           : QString::fromLatin1(encodedName, nameLength);
    entry.LastWriteTime = QDateTime::fromMSecsSinceEpoch(file.LastWriteTime);
    entry.Position = int(position);
    entry.Size = int(file.DataSize);
    aEntry.insert(entry.FileName, entry);

    return true;
}

bool CResFile::getFiles(QMap<QString, SResFileEntry>& aEntry, QDataStream& stream)
{
    QIODevice* device = stream.device();
    if (!device)
        return false;

    const qint64 startPos = device->pos();
    const qint64 streamLength = device->size();
    if (!rangeFits(startPos, 0, sizeof(SResFileHeader), streamLength))
        return false;

    stream >> m_header;
    if (stream.status() != QDataStream::Ok || m_header.Signature != s_signature)
        return false;

    if (m_header.TableOffset < sizeof(SResFileHeader) ||
        m_header.NamesLenght > uint(std::numeric_limits<int>::max()))
    {
        return false;
    }

    const quint64 tableBytes = quint64(m_header.TableSize) * sizeof(SResHashTable);
    const quint64 namesOffset = quint64(m_header.TableOffset) + tableBytes;
    if (!rangeFits(startPos, m_header.TableOffset, tableBytes, streamLength) ||
        !rangeFits(startPos, namesOffset, m_header.NamesLenght, streamLength))
    {
        return false;
    }

    const qint64 tablePosition = startPos + qint64(m_header.TableOffset);
    const qint64 namesPosition = startPos + qint64(namesOffset);
    QByteArray nameData(int(m_header.NamesLenght), '\0');
    if (!device->seek(namesPosition) ||
        !readRawDataExactly(stream, nameData.data(), nameData.size()))
    {
        return false;
    }

    if (!device->seek(tablePosition))
        return false;

    QMap<QString, SResFileEntry> parsedEntries;
    for (uint i(0); i<m_header.TableSize; ++i)
    {
        SResHashTable hashTable;
        stream >> hashTable;
        if (stream.status() != QDataStream::Ok ||
            (hashTable.NextIndex != uint(-1) && hashTable.NextIndex >= m_header.TableSize) ||
            !buildFileMap(parsedEntries, hashTable, startPos, streamLength, nameData))
        {
            return false;
        }
    }

    aEntry = parsedEntries;
    return true;
}

// in: aEntry, stream
bool CResFile::readFiles(const QMap<QString, SResFileEntry>& aEntry, QDataStream& stream)
{
    QIODevice* device = stream.device();
    if (!device)
        return false;

    QMap<QString, QByteArray> parsedFiles;
    for(auto it = aEntry.constBegin(); it != aEntry.constEnd(); ++it)
    {
        const SResFileEntry& entry = it.value();
        if (entry.Position < 0 || entry.Size < 0 || !device->seek(entry.Position))
            return false;

        QByteArray ba(entry.Size, 0);
        if (!readRawDataExactly(stream, ba.data(), ba.size()))
            return false;

        parsedFiles.insert(entry.FileName, ba);
    }

    m_aFiles.swap(parsedFiles);
    return true;
}

uint getEIStringHash32(QString value, uint hashTableSize = 0)
{
    if (value.isEmpty())
    {
        ei::log(eLogFatal, "Hash for empty string is incorrect");
        return -1;
    }

    uint hash = 0;
    QTextCodec* pCodec = QTextCodec::codecForName("CP1251");
    QByteArray filenameAsByte = pCodec->fromUnicode(value.toLower());
    for(auto& symb: filenameAsByte)
    {
        hash += symb;
    }

    return hashTableSize == 0
        ? hash
        : hash % hashTableSize;
}


// author: Demoth
// modyfied by: Konstvest (moved from c# to c++, export align offset)
void buildResHashTable(QMap<QString, QByteArray> entries, uint& tableOffset,
                       QVector<SResHashTable>& outHashTable, QVector<uint>& outAlignOffset, QByteArray& outName)
{
    uint hashTableSize = (uint)entries.size();
    outName.clear();
    outHashTable.resize(hashTableSize);
    for (int i = 0; i < outHashTable.size(); i++)
    {
        outHashTable[i].NextIndex = -1;
        outHashTable[i].DataOffset = 0;
    }
    QDateTime now(QDateTime::currentDateTime());
    uint unixTime = now.toTime_t();

    uint lastFreeIndex = (uint)outHashTable.size() - 1;
    for (auto& entry: entries.toStdMap())
    {
        uint index = getEIStringHash32(entry.first, hashTableSize);
        if (outHashTable[index].DataOffset != 0)
        {
            while (outHashTable[index].NextIndex != uint(-1))
                index = outHashTable[index].NextIndex;

            while (outHashTable[lastFreeIndex].DataOffset != 0)
                lastFreeIndex--;

            outHashTable[index].NextIndex = lastFreeIndex;
            index = lastFreeIndex;
            lastFreeIndex--;
        }

        outHashTable[index].LastWriteTime = unixTime;
        outHashTable[index].DataOffset    = (uint)tableOffset; tableOffset += entry.second.size();
        uint alignOffset = (16-tableOffset%16)%16;
        outAlignOffset.append(alignOffset);
        tableOffset += alignOffset;

        outHashTable[index].DataSize      = (uint)entry.second.size();
        QTextCodec* codec = QTextCodec::codecForName("CP1251");
        const QByteArray encodedName = codec->fromUnicode(entry.first);
        outHashTable[index].NameOffset    = (uint)outName.size();
        outHashTable[index].NameLength    = (uint)encodedName.size(); outName.append(encodedName);
        outHashTable[index].NextIndex = -1;
    }
}

QByteArray CResFile::generateResData()
{
    if(m_aFiles.empty())
    {
        ei::log(eLogWarning, "Can not write res-file with empty data");
    }
    SResFileHeader header{s_signature, uint(m_aFiles.size()), sizeof (SResFileHeader), 0};

    QVector<SResHashTable> aHashTable;
    QVector<uint> aAlignOffset;
    QByteArray nameData;
    buildResHashTable(m_aFiles, header.TableOffset, aHashTable, aAlignOffset, nameData);
    header.NamesLenght = nameData.size();
    QByteArray resData;
    QDataStream resStream(&resData, QIODevice::WriteOnly);
    util::formatStream(resStream);
    //write header
    resStream << header;
    //write file data
    char zeroByte = 0;

    int nOffset = 0;
    for(auto& file: m_aFiles)
    {
        resStream.writeRawData(file, file.size());
        for(uint i(0); i<aAlignOffset[nOffset]; ++i)
        {
            resStream.writeRawData(&zeroByte, 1);
        }
        ++nOffset;
    }
    //write hash table
    for(auto& hashData: aHashTable)
        resStream << hashData;
    //write name data
    resStream.writeRawData(nameData, nameData.size());
    return resData;
}

CResFile::CResFile(QString path)
{
    m_aFiles.clear();
    m_aEntry.clear();

    QFile file(path);
    if (!file.exists())
    {
        ei::log(eLogFatal, file.fileName() + " does not exists");
        return;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        ei::log(eLogFatal, file.fileName() + " Error while open res-file");
        return;
    }

    QByteArray buffer = file.readAll();
    if (file.error() != QFile::NoError)
    {
        ei::log(eLogFatal, file.fileName() + " Error while reading res-file");
        return;
    }
    file.close();

    QDataStream stream(buffer);
    util::formatStream(stream);
    QMap<QString, SResFileEntry> resEntries;
    if (!getFiles(resEntries, stream) || !readFiles(resEntries, stream))
    {
        m_aFiles.clear();
        ei::log(eLogFatal, "Invalid res-file structure");
    }
}

CResFile::CResFile(const QByteArray& data)
{
    m_aFiles.clear();
    m_aEntry.clear();
    QDataStream stream(data);
    util::formatStream(stream);
    QMap<QString, SResFileEntry> resEntries;
    if (!getFiles(resEntries, stream) || !readFiles(resEntries, stream))
    {
        m_aFiles.clear();
        qDebug() << "Invalid res-file structure";
    }
}

void CResFile::saveToFile(QString path)
{
    auto hash=QtConcurrent::run([path](){return QFileInfo::exists(path)?resFileSha256(path):QByteArray();});hash.waitForFinished();
    const auto audit=beginResWriteAudit(path,hash.result());
    QSaveFile file(path);
    try
    {
        file.open(QIODevice::WriteOnly);
        if (file.error() != QFile::NoError)
        {
            qDebug() << file.fileName() << " Error while writing res-file";
            return;
        }
        QByteArray resData = generateResData();
        file.write(resData);
        if(!file.commit())
            qDebug() << file.fileName() << " Error while committing res-file" << file.errorString();
        else {auto digest=QtConcurrent::run([resData](){return QCryptographicHash::hash(resData,QCryptographicHash::Sha256).toHex();});digest.waitForFinished();finishResWriteAudit(audit,digest.result(),"legacy-res-write");}
    }
    catch (std::exception ex)
    {
        qDebug() << ex.what();
    }
}

void CResFile::addFiledata(const QString name, const QByteArray data)
{
    m_aFiles[name] = data;
}

void CResFile::addFiledataCaseInsensitive(const QString& name, const QByteArray& data)
{
    for(auto it = m_aFiles.begin(); it != m_aFiles.end(); ++it)
    {
        if(QString::compare(it.key(), name, Qt::CaseInsensitive) == 0)
        {
            it.value() = data;
            return;
        }
    }
    m_aFiles[name] = data;
}

CResFile::~CResFile()
{
    for (auto& buf: m_aFiles.values())
    {
        buf.clear();
    }
    m_aFiles.clear();
    m_aEntry.clear();
}

#include "res_archive_index.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QtEndian>

#include <limits>

namespace {
constexpr quint32 kSignature = 0x019CE23C;
constexpr quint64 kHeaderSize = 16;
constexpr quint64 kEntrySize = 22;
constexpr quint64 kMaximumMemberCount = 10000000;
constexpr qint64 kHeaderFingerprintBytes = 4096;

void setError(QString* error, const QString& text)
{
    if(error) *error = text;
}

quint16 u16(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

quint32 u32(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

void put16(QByteArray* bytes, quint64 offset, quint16 value)
{
    qToLittleEndian<quint16>(value,
        reinterpret_cast<uchar*>(bytes->data() + offset));
}

void put32(QByteArray* bytes, quint64 offset, quint32 value)
{
    qToLittleEndian<quint32>(value,
        reinterpret_cast<uchar*>(bytes->data() + offset));
}

QByteArray sha(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QByteArray encodedCp1251(const QString& name)
{
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    if(!codec) return QByteArray();
    const QByteArray encoded = codec->fromUnicode(name);
    return codec->toUnicode(encoded) == name ? encoded : QByteArray();
}

quint32 nameHash(const QByteArray& name, quint32 tableSize)
{
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    const QByteArray normalized = codec
        ? codec->fromUnicode(codec->toUnicode(name).toLower()) : name.toLower();
    quint32 sum = 0;
    for(const unsigned char byte : normalized) sum += byte;
    return tableSize ? sum % tableSize : 0;
}
}

bool CResArchiveIndex::open(const QString& path, QString* error)
{
    m_path.clear();
    m_members.clear();
    m_byName.clear();
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        setError(error, QString("Could not open RES archive: %1").arg(file.errorString()));
        return false;
    }
    const QFileInfo info(file);
    const qint64 size = file.size();
    const QByteArray header = file.read(kHeaderSize);
    if(header.size() != qint64(kHeaderSize) || u32(header, 0) != kSignature)
    {
        setError(error, "The file is not a complete Evil Islands RES archive.");
        return false;
    }
    const quint32 count = u32(header, 4);
    const quint32 tableOffset = u32(header, 8);
    const quint32 namesLength = u32(header, 12);
    const quint64 tableLength = quint64(count) * kEntrySize;
    if(count == 0 || count > kMaximumMemberCount || tableOffset < kHeaderSize
       || quint64(tableOffset) + tableLength + namesLength > quint64(size)
       || tableLength > quint64(std::numeric_limits<int>::max())
       || namesLength > quint32(std::numeric_limits<int>::max()))
    {
        setError(error, "The RES table or filename buffer is out of bounds.");
        return false;
    }
    if(!file.seek(tableOffset))
    {
        setError(error, "Could not seek to the RES table.");
        return false;
    }
    const QByteArray table = file.read(qint64(tableLength));
    const QByteArray names = file.read(namesLength);
    if(table.size() != qint64(tableLength) || names.size() != int(namesLength))
    {
        setError(error, "The RES table or filename buffer is truncated.");
        return false;
    }
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(quint32 slot = 0; slot < count; ++slot)
    {
        const quint64 offset = quint64(slot) * kEntrySize;
        SResIndexedMember member;
        member.tableIndex = int(slot);
        member.nextIndex = u32(table, offset);
        member.dataSize = u32(table, offset + 4);
        member.dataOffset = u32(table, offset + 8);
        member.lastWriteTime = u32(table, offset + 12);
        member.nameLength = u16(table, offset + 16);
        member.nameOffset = u32(table, offset + 18);
        if((member.nextIndex != quint32(-1) && member.nextIndex >= count)
           || quint64(member.dataOffset) + member.dataSize > quint64(size)
           || quint64(member.nameOffset) + member.nameLength > quint64(names.size()))
        {
            setError(error, QString("RES member table slot %1 is out of bounds.").arg(slot));
            return false;
        }
        member.encodedName = names.mid(int(member.nameOffset), member.nameLength);
        member.name = codec ? codec->toUnicode(member.encodedName)
                            : QString::fromLatin1(member.encodedName);
        m_byName[member.name.toCaseFolded()].append(m_members.size());
        m_members.append(member);
    }
    file.seek(0);
    m_headerSha256 = sha(file.read(qMin(kHeaderFingerprintBytes, size)));
    m_path = info.absoluteFilePath();
    m_size = size;
    m_modified = info.lastModified();
    if(error) error->clear();
    return true;
}

int CResArchiveIndex::memberIndex(const QString& name) const
{
    const QVector<int> matches = m_byName.value(name.toCaseFolded());
    return matches.size() == 1 ? matches.first() : matches.isEmpty() ? -1 : -2;
}

bool CResArchiveIndex::sourceIsCurrent(QString* error) const
{
    if(!isOpen())
    {
        setError(error, "No RES archive is open.");
        return false;
    }
    QFile file(m_path);
    const QFileInfo info(m_path);
    if(info.size() != m_size || info.lastModified() != m_modified
       || !file.open(QIODevice::ReadOnly)
       || sha(file.read(qMin(kHeaderFingerprintBytes, m_size))) != m_headerSha256)
    {
        setError(error, "The RES archive changed since it was indexed.");
        return false;
    }
    return true;
}

QByteArray CResArchiveIndex::readRange(quint32 offset, quint32 size, qint64 maximumBytes,
                                       QString* error) const
{
    if(!sourceIsCurrent(error)) return QByteArray();
    QFile file(m_path);
    if(!file.open(QIODevice::ReadOnly) || !file.seek(offset))
    {
        setError(error, "Could not seek to the RES member payload.");
        return QByteArray();
    }
    const qint64 wanted = maximumBytes < 0 ? qint64(size) : qMin(qint64(size), maximumBytes);
    const QByteArray result = file.read(wanted);
    if(result.size() != wanted)
    {
        setError(error, "The RES member payload is truncated.");
        return QByteArray();
    }
    if(error) error->clear();
    return result;
}

QByteArray CResArchiveIndex::readMember(int index, QString* error) const
{
    if(index < 0 || index >= m_members.size())
    {
        setError(error, "The RES member index is invalid.");
        return QByteArray();
    }
    const auto& value = m_members.at(index);
    return readRange(value.dataOffset, value.dataSize, -1, error);
}

QByteArray CResArchiveIndex::readMember(const QString& name, QString* error) const
{
    const int index = memberIndex(name);
    if(index < 0)
    {
        setError(error, index == -2 ? "The RES member name is ambiguous."
                                    : "The RES member was not found.");
        return QByteArray();
    }
    return readMember(index, error);
}

QByteArray CResArchiveIndex::readMemberPrefix(int index, qint64 maximumBytes,
                                              QString* error) const
{
    if(index < 0 || index >= m_members.size() || maximumBytes < 0)
    {
        setError(error, "The RES member prefix request is invalid.");
        return QByteArray();
    }
    const auto& value = m_members.at(index);
    return readRange(value.dataOffset, value.dataSize, maximumBytes, error);
}

QByteArray CResArchiveIndex::memberSha256(int index, QString* error) const
{
    const QByteArray payload = readMember(index, error);
    if(payload.isNull()) return QByteArray();
    return sha(payload);
}

QByteArray CResArchiveIndex::memberSha256(const QString& name, QString* error) const
{
    const int index = memberIndex(name);
    return index < 0 ? QByteArray() : memberSha256(index, error);
}

CResArchiveTransaction::CResArchiveTransaction(const CResArchiveIndex& source)
    : m_source(source)
{
}

QString CResArchiveTransaction::finalNameFor(const QString& original) const
{
    for(auto it = m_changes.crbegin(); it != m_changes.crend(); ++it)
        if(it->originalName.compare(original, Qt::CaseInsensitive) == 0
           && it->kind == Change::Rename) return it->finalName;
    return original;
}

bool CResArchiveTransaction::isDeleted(const QString& original) const
{
    for(const Change& change : m_changes)
        if(change.kind == Change::Delete
           && change.originalName.compare(original, Qt::CaseInsensitive) == 0) return true;
    return false;
}

bool CResArchiveTransaction::validateName(const QString& name, const QString& ignoredName,
                                          QString* error) const
{
    if(name.isEmpty() || encodedCp1251(name).isEmpty())
    {
        setError(error, "RES member names must round-trip through CP1251.");
        return false;
    }
    for(const SResIndexedMember& member : m_source.members())
    {
        if(isDeleted(member.name) || member.name.compare(ignoredName, Qt::CaseInsensitive) == 0)
            continue;
        if(finalNameFor(member.name).compare(name, Qt::CaseInsensitive) == 0)
        {
            setError(error, "The RES member name has a case-insensitive collision.");
            return false;
        }
    }
    for(const Change& change : m_changes)
        if(change.kind == Change::Add
           && change.finalName.compare(name, Qt::CaseInsensitive) == 0)
        {
            setError(error, "The RES member name has a case-insensitive collision.");
            return false;
        }
    return true;
}

bool CResArchiveTransaction::replace(const QString& name, const QByteArray& expectedSha256,
                                     const QByteArray& payload, QString* error)
{
    const int index = m_source.memberIndex(name);
    if(index < 0 || isDeleted(m_source.member(index).name))
    {
        setError(error, "The replacement member is missing or ambiguous.");
        return false;
    }
    if(!expectedSha256.isEmpty() && m_source.memberSha256(index, error) != expectedSha256)
    {
        setError(error, "The replacement member changed since it was reviewed.");
        return false;
    }
    Change change;
    change.kind = Change::Replace;
    change.originalName = m_source.member(index).name;
    change.payload = payload;
    m_changes.append(change);
    return true;
}

bool CResArchiveTransaction::add(const QString& name, const QByteArray& payload,
                                 quint32 lastWriteTime, QString* error)
{
    if(!validateName(name, QString(), error)) return false;
    Change change;
    change.kind = Change::Add;
    change.finalName = name;
    change.payload = payload;
    change.lastWriteTime = lastWriteTime;
    m_changes.append(change);
    return true;
}

bool CResArchiveTransaction::rename(const QString& name, const QString& newName, QString* error)
{
    const int index = m_source.memberIndex(name);
    if(index < 0 || isDeleted(name))
    {
        setError(error, "The rename source is missing or ambiguous.");
        return false;
    }
    if(!validateName(newName, m_source.member(index).name, error)) return false;
    Change change;
    change.kind = Change::Rename;
    change.originalName = m_source.member(index).name;
    change.finalName = newName;
    m_changes.append(change);
    return true;
}

bool CResArchiveTransaction::remove(const QString& name, QString* error)
{
    const int index = m_source.memberIndex(name);
    if(index < 0 || isDeleted(name))
    {
        setError(error, "The delete source is missing or ambiguous.");
        return false;
    }
    Change change;
    change.kind = Change::Delete;
    change.originalName = m_source.member(index).name;
    m_changes.append(change);
    return true;
}

bool CResArchiveTransaction::writeCandidate(const QString& destinationPath, QString* error) const
{
    if(!m_source.sourceIsCurrent(error)) return false;
    QFile source(m_source.path());
    if(!source.open(QIODevice::ReadOnly))
    {
        setError(error, "Could not reopen the transaction source archive.");
        return false;
    }
    QByteArray staged = source.readAll();
    if(staged.size() != m_source.sourceSize())
    {
        setError(error, "Could not retain the complete transaction source archive.");
        return false;
    }
    // QSaveFile cannot atomically replace an open source on Windows.
    source.close();

    struct OutputMember { QByteArray name; quint32 size; quint32 offset; quint32 time; };
    QVector<OutputMember> outputMembers;
    for(const SResIndexedMember& member : m_source.members())
    {
        if(isDeleted(member.name)) continue;
        OutputMember output{encodedCp1251(finalNameFor(member.name)), member.dataSize,
                            member.dataOffset, member.lastWriteTime};
        for(const Change& change : m_changes)
        {
            if(change.kind != Change::Replace
               || change.originalName.compare(member.name, Qt::CaseInsensitive) != 0) continue;
            staged.append(QByteArray((16 - staged.size() % 16) % 16, '\0'));
            if(quint64(staged.size()) > std::numeric_limits<quint32>::max()
               || quint64(change.payload.size()) > std::numeric_limits<quint32>::max())
            {
                setError(error, "The RES transaction exceeds 32-bit offset or size limits.");
                return false;
            }
            output.offset = quint32(staged.size());
            output.size = quint32(change.payload.size());
            staged.append(change.payload);
        }
        outputMembers.append(output);
    }
    for(const Change& change : m_changes)
    {
        if(change.kind != Change::Add) continue;
        staged.append(QByteArray((16 - staged.size() % 16) % 16, '\0'));
        if(quint64(staged.size()) > std::numeric_limits<quint32>::max()
           || quint64(change.payload.size()) > std::numeric_limits<quint32>::max())
        {
            setError(error, "The RES transaction exceeds 32-bit offset or size limits.");
            return false;
        }
        OutputMember output{encodedCp1251(change.finalName), quint32(change.payload.size()),
                            quint32(staged.size()), change.lastWriteTime};
        staged.append(change.payload);
        outputMembers.append(output);
    }
    if(outputMembers.isEmpty())
    {
        setError(error, "A RES transaction cannot delete every member.");
        return false;
    }
    staged.append(QByteArray((16 - staged.size() % 16) % 16, '\0'));
    if(quint64(staged.size()) > std::numeric_limits<quint32>::max())
    {
        setError(error, "The RES transaction table exceeds the 32-bit offset limit.");
        return false;
    }
    const quint32 tableOffset = quint32(staged.size());
    const quint32 count = quint32(outputMembers.size());
    QVector<int> hashSlots(int(count), -1);
    QVector<quint32> next(int(count), quint32(-1));
    int freeSlot = int(count) - 1;
    for(int memberIndex = 0; memberIndex < outputMembers.size(); ++memberIndex)
    {
        int slot = int(nameHash(outputMembers.at(memberIndex).name, count));
        if(hashSlots.at(slot) >= 0)
        {
            while(next.at(slot) != quint32(-1)) slot = int(next.at(slot));
            while(freeSlot >= 0 && hashSlots.at(freeSlot) >= 0) --freeSlot;
            if(freeSlot < 0)
            {
                setError(error, "Could not allocate a RES hash collision slot.");
                return false;
            }
            next[slot] = quint32(freeSlot);
            slot = freeSlot--;
        }
        hashSlots[slot] = memberIndex;
    }
    QByteArray names;
    QByteArray table(int(quint64(count) * kEntrySize), '\0');
    for(int slot = 0; slot < hashSlots.size(); ++slot)
    {
        if(hashSlots.at(slot) < 0)
        {
            setError(error, "The generated RES hash table contains an empty slot.");
            return false;
        }
        const OutputMember& member = outputMembers.at(hashSlots.at(slot));
        const quint64 offset = quint64(slot) * kEntrySize;
        put32(&table, offset, next.at(slot));
        put32(&table, offset + 4, member.size);
        put32(&table, offset + 8, member.offset);
        put32(&table, offset + 12, member.time);
        put16(&table, offset + 16, quint16(member.name.size()));
        put32(&table, offset + 18, quint32(names.size()));
        names.append(member.name);
    }
    staged.append(table);
    staged.append(names);
    put32(&staged, 0, kSignature);
    put32(&staged, 4, count);
    put32(&staged, 8, tableOffset);
    put32(&staged, 12, quint32(names.size()));

    QTemporaryFile validationFile(QFileInfo(destinationPath).absolutePath()
        + QStringLiteral("/.endurance-res-candidate-XXXXXX.res"));
    validationFile.setAutoRemove(true);
    if(!validationFile.open() || validationFile.write(staged) != staged.size()
       || !validationFile.flush())
    {
        setError(error, "Could not stage the RES transaction candidate.");
        return false;
    }
    const QString validationPath = validationFile.fileName();
    validationFile.close();
    CResArchiveIndex reopened;
    if(!reopened.open(validationPath, error) || reopened.memberCount() != outputMembers.size())
    {
        setError(error, "The staged RES transaction failed semantic reopen validation.");
        return false;
    }
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(const OutputMember& member : outputMembers)
        if(reopened.memberIndex(codec ? codec->toUnicode(member.name)
                                     : QString::fromLatin1(member.name)) < 0)
        {
            setError(error, "The staged RES transaction lost a member name.");
            return false;
        }
    if(!m_source.sourceIsCurrent(error)) return false;

    QSaveFile destination(destinationPath);
    if(!destination.open(QIODevice::WriteOnly) || destination.write(staged) != staged.size())
    {
        destination.cancelWriting();
        setError(error, "Could not stage the validated RES transaction for publication.");
        return false;
    }
    if(!m_source.sourceIsCurrent(error))
    {
        destination.cancelWriting();
        return false;
    }
    if(!destination.commit())
    {
        setError(error, "Could not atomically publish the RES transaction candidate.");
        return false;
    }
    return true;
}

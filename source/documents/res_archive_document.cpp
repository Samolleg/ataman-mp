#include "res_archive_document.h"

#include <QCryptographicHash>
#include <QTextCodec>
#include <QtEndian>

#include <limits>

namespace {
const quint32 kResSignature = 0x019CE23C;
const quint64 kHeaderSize = 16;
const quint64 kEntrySize = 22;

bool rangeFits(const QByteArray& bytes, quint64 offset, quint64 size)
{
    return offset <= quint64(bytes.size()) && size <= quint64(bytes.size()) - offset;
}

quint16 readU16(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

quint32 readU32(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

void writeU32(QByteArray* bytes, quint64 offset, quint32 value)
{
    qToLittleEndian<quint32>(value,
        reinterpret_cast<uchar*>(bytes->data() + offset));
}

void writeU16(QByteArray* bytes, quint64 offset, quint16 value)
{
    qToLittleEndian<quint16>(value,
        reinterpret_cast<uchar*>(bytes->data() + offset));
}

quint32 resourceNameHash(const QByteArray& encodedName, quint32 tableSize)
{
    quint32 sum = 0;
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    const QByteArray normalized = codec
        ? codec->fromUnicode(codec->toUnicode(encodedName).toLower())
        : encodedName.toLower();
    for(const unsigned char byte : normalized)
        sum += byte;
    return tableSize ? sum % tableSize : 0;
}

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

void addError(QStringList* errors, const QString& text)
{
    if(errors)
        errors->append(text);
}
}

bool CResArchiveDocument::loadBytes(const QByteArray& bytes, const QString& sourceName,
                                    QStringList* errors)
{
    m_originalBytes.clear();
    m_tableBytes.clear();
    m_namesBytes.clear();
    m_members.clear();
    m_sourceName = sourceName;
    m_tableOffset = 0;
    if(errors)
        errors->clear();
    if(!rangeFits(bytes, 0, kHeaderSize))
    {
        addError(errors, "RES archive is smaller than its header.");
        return false;
    }
    if(readU32(bytes, 0) != kResSignature)
    {
        addError(errors, "Invalid RES signature.");
        return false;
    }
    const quint32 tableSize = readU32(bytes, 4);
    m_tableOffset = readU32(bytes, 8);
    const quint32 namesLength = readU32(bytes, 12);
    const quint64 tableBytes = quint64(tableSize) * kEntrySize;
    if(!rangeFits(bytes, m_tableOffset, tableBytes + namesLength))
    {
        addError(errors, "RES table or filename buffer extends outside the archive.");
        return false;
    }
    m_tableBytes = bytes.mid(int(m_tableOffset), int(tableBytes));
    m_namesBytes = bytes.mid(int(m_tableOffset + tableBytes), int(namesLength));
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(quint32 index = 0; index < tableSize; ++index)
    {
        const quint64 entryOffset = quint64(index) * kEntrySize;
        SResArchiveMember member;
        member.tableIndex = int(index);
        member.nextIndex = readU32(m_tableBytes, entryOffset);
        member.dataSize = readU32(m_tableBytes, entryOffset + 4);
        member.dataOffset = readU32(m_tableBytes, entryOffset + 8);
        member.lastWriteTime = readU32(m_tableBytes, entryOffset + 12);
        member.nameLength = readU16(m_tableBytes, entryOffset + 16);
        member.nameOffset = readU32(m_tableBytes, entryOffset + 18);
        if(member.nextIndex != quint32(-1) && member.nextIndex >= tableSize)
        {
            addError(errors, QString("RES member %1 has an invalid hash-chain index.").arg(index));
            return false;
        }
        if(!rangeFits(bytes, member.dataOffset, member.dataSize) ||
           !rangeFits(m_namesBytes, member.nameOffset, member.nameLength))
        {
            addError(errors, QString("RES member %1 has an out-of-range payload or name.").arg(index));
            return false;
        }
        member.encodedName = m_namesBytes.mid(int(member.nameOffset), member.nameLength);
        member.name = codec ? codec->toUnicode(member.encodedName)
                            : QString::fromLatin1(member.encodedName);
        member.payload = bytes.mid(int(member.dataOffset), int(member.dataSize));
        member.sha256 = digest(member.payload);
        m_members.append(member);
    }
    m_originalBytes = bytes;
    return true;
}

int CResArchiveDocument::uniqueMemberIndex(const QString& name) const
{
    int found = -1;
    for(int index = 0; index < m_members.size(); ++index)
    {
        if(m_members.at(index).name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if(found >= 0)
            return -2;
        found = index;
    }
    return found;
}

QByteArray CResArchiveDocument::memberPayload(const QString& name) const
{
    const int index = uniqueMemberIndex(name);
    return index >= 0 ? m_members.at(index).payload : QByteArray();
}

bool CResArchiveDocument::replaceMember(const QString& name,
                                        const QByteArray& expectedPayloadSha256,
                                        const QByteArray& replacementPayload,
                                        QByteArray* output, QStringList* errors) const
{
    if(output)
        output->clear();
    const int index = uniqueMemberIndex(name);
    if(index == -2)
    {
        addError(errors, QString("RES member %1 is ambiguous.").arg(name));
        return false;
    }
    if(index < 0)
    {
        addError(errors, QString("RES member %1 was not found.").arg(name));
        return false;
    }
    const SResArchiveMember& member = m_members.at(index);
    if(!expectedPayloadSha256.isEmpty() && member.sha256 != expectedPayloadSha256)
    {
        addError(errors, QString("RES member %1 changed since it was opened.").arg(member.name));
        return false;
    }
    if(member.payload == replacementPayload)
    {
        if(output)
            *output = m_originalBytes;
        return true;
    }

    QByteArray staged = m_originalBytes;
    staged.append(QByteArray((16 - (staged.size() % 16)) % 16, '\0'));
    const quint64 payloadOffset = quint64(staged.size());
    if(payloadOffset > quint64(std::numeric_limits<quint32>::max()) ||
       quint64(replacementPayload.size()) > quint64(std::numeric_limits<quint32>::max()))
    {
        addError(errors, "The staged RES payload exceeds the 32-bit archive format.");
        return false;
    }
    staged.append(replacementPayload);
    staged.append(QByteArray((16 - (staged.size() % 16)) % 16, '\0'));
    const quint64 newTableOffset = quint64(staged.size());
    if(newTableOffset > quint64(std::numeric_limits<quint32>::max()))
    {
        addError(errors, "The staged RES table offset exceeds the archive format.");
        return false;
    }
    QByteArray table = m_tableBytes;
    const quint64 entryOffset = quint64(member.tableIndex) * kEntrySize;
    writeU32(&table, entryOffset + 4, quint32(replacementPayload.size()));
    writeU32(&table, entryOffset + 8, quint32(payloadOffset));
    staged.append(table);
    staged.append(m_namesBytes);
    writeU32(&staged, 8, quint32(newTableOffset));
    if(output)
        *output = staged;
    return true;
}

bool CResArchiveDocument::buildWithAppendedMember(const QString& name,
                                                  const QByteArray& payload,
                                                  quint32 lastWriteTime,
                                                  QByteArray* output,
                                                  QStringList* errors) const
{
    if(output)
        output->clear();
    if(name.isEmpty() || payload.isEmpty() || !output)
    {
        addError(errors, "A RES member name, payload, and output are required.");
        return false;
    }
    if(uniqueMemberIndex(name) != -1)
    {
        addError(errors, QString("RES member %1 already exists or is ambiguous.").arg(name));
        return false;
    }
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    const QByteArray appendedName = codec ? codec->fromUnicode(name)
                                          : name.toLatin1();
    if(appendedName.isEmpty() || (codec && codec->toUnicode(appendedName) != name))
    {
        addError(errors, "The appended RES member name cannot round-trip through CP1251.");
        return false;
    }

    struct BuildMember
    {
        QByteArray name;
        QByteArray payload;
        quint32 time{0};
        quint32 offset{0};
        quint32 nameOffset{0};
    };
    QVector<BuildMember> members;
    members.reserve(m_members.size() + 1);
    for(const SResArchiveMember& member : m_members)
    {
        BuildMember copy;
        copy.name = member.encodedName;
        copy.payload = member.payload;
        copy.time = member.lastWriteTime;
        members.append(copy);
    }
    BuildMember appended;
    appended.name = appendedName;
    appended.payload = payload;
    appended.time = lastWriteTime;
    members.append(appended);
    const quint32 count = quint32(members.size());

    QByteArray staged(int(kHeaderSize), '\0');
    QByteArray names;
    for(BuildMember& member : members)
    {
        staged.append(QByteArray((16 - (staged.size() % 16)) % 16, '\0'));
        if(quint64(staged.size()) > quint64(std::numeric_limits<quint32>::max())
           || quint64(member.payload.size()) > quint64(std::numeric_limits<quint32>::max()))
        {
            addError(errors, "The derived RES archive exceeds its 32-bit format.");
            return false;
        }
        member.offset = quint32(staged.size());
        member.nameOffset = quint32(names.size());
        staged.append(member.payload);
        names.append(member.name);
    }
    staged.append(QByteArray((16 - (staged.size() % 16)) % 16, '\0'));
    const quint32 tableOffset = quint32(staged.size());

    QVector<int> tableSlots(int(count), -1);
    QVector<quint32> next(int(count), quint32(-1));
    int lastFree = int(count) - 1;
    for(int memberIndex = 0; memberIndex < members.size(); ++memberIndex)
    {
        int slot = int(resourceNameHash(members.at(memberIndex).name, count));
        if(tableSlots.at(slot) >= 0)
        {
            while(next.at(slot) != quint32(-1))
                slot = int(next.at(slot));
            while(lastFree >= 0 && tableSlots.at(lastFree) >= 0)
                --lastFree;
            if(lastFree < 0)
            {
                addError(errors, "The derived RES hash table has no collision slot.");
                return false;
            }
            next[slot] = quint32(lastFree);
            slot = lastFree--;
        }
        tableSlots[slot] = memberIndex;
    }

    QByteArray table(int(quint64(count) * kEntrySize), '\0');
    for(int slot = 0; slot < tableSlots.size(); ++slot)
    {
        if(tableSlots.at(slot) < 0)
        {
            addError(errors, "The derived RES hash table contains an empty slot.");
            return false;
        }
        const BuildMember& member = members.at(tableSlots.at(slot));
        const quint64 entry = quint64(slot) * kEntrySize;
        writeU32(&table, entry, next.at(slot));
        writeU32(&table, entry + 4, quint32(member.payload.size()));
        writeU32(&table, entry + 8, member.offset);
        writeU32(&table, entry + 12, member.time);
        writeU16(&table, entry + 16, quint16(member.name.size()));
        writeU32(&table, entry + 18, member.nameOffset);
    }
    staged.append(table);
    staged.append(names);
    writeU32(&staged, 0, kResSignature);
    writeU32(&staged, 4, count);
    writeU32(&staged, 8, tableOffset);
    writeU32(&staged, 12, quint32(names.size()));

    CResArchiveDocument reopened;
    QStringList reopenErrors;
    if(!reopened.loadBytes(staged, m_sourceName + " [derived]", &reopenErrors)
       || reopened.members().size() != members.size()
       || reopened.memberPayload(name) != payload)
    {
        if(errors) errors->append(reopenErrors);
        addError(errors, "The derived RES archive failed structural reopen validation.");
        return false;
    }
    for(const SResArchiveMember& original : m_members)
    {
        if(reopened.memberPayload(original.name) != original.payload)
        {
            addError(errors, QString("Derived RES changed untouched member %1.").arg(original.name));
            return false;
        }
    }
    *output = staged;
    return true;
}

bool CResArchiveDocument::validateReplacement(const CResArchiveDocument& baseline,
                                              const CResArchiveDocument& candidate,
                                              const QString& changedName,
                                              const QByteArray& expectedReplacement,
                                              QStringList* errors)
{
    if(baseline.members().size() != candidate.members().size())
        addError(errors, "RES member count changed.");
    const int count = qMin(baseline.members().size(), candidate.members().size());
    int changed = 0;
    for(int index = 0; index < count; ++index)
    {
        const SResArchiveMember& before = baseline.members().at(index);
        const SResArchiveMember& after = candidate.members().at(index);
        if(before.encodedName != after.encodedName || before.nameOffset != after.nameOffset ||
           before.nameLength != after.nameLength || before.nextIndex != after.nextIndex ||
           before.lastWriteTime != after.lastWriteTime)
            addError(errors, QString("RES metadata changed for member %1.").arg(before.name));
        if(before.name.compare(changedName, Qt::CaseInsensitive) == 0)
        {
            ++changed;
            if(after.payload != expectedReplacement)
                addError(errors, QString("Replacement payload mismatch for %1.").arg(before.name));
        }
        else if(before.payload != after.payload || before.sha256 != after.sha256)
            addError(errors, QString("Untouched RES member %1 changed.").arg(before.name));
    }
    if(changed != 1)
        addError(errors, QString("Expected exactly one changed RES member named %1.").arg(changedName));
    return !errors || errors->isEmpty();
}

#include "process_edit_lock.h"
#include "res_archive_index.h"
#include "native_sha256.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QSet>
#include <QtEndian>

#include <limits>
#include <algorithm>

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
    if(index<0||index>=m_members.size()||!sourceIsCurrent(error))return {};
    QFile f(m_path);const auto& m=m_members[index];
    if(!f.open(QIODevice::ReadOnly)||!f.seek(m.dataOffset))return {};
    QCryptographicHash h(QCryptographicHash::Sha256);qint64 remaining=m.dataSize;
    while(remaining){auto b=f.read(qMin(remaining,qint64(4*1024*1024)));if(b.isEmpty())return {};h.addData(b);remaining-=b.size();}
    return h.result().toHex();
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

bool CResArchiveTransaction::writeCandidate(const QString& destinationPath, QString* error,
    const std::function<bool()>& beforeCommit, QByteArray* candidateSha256) const
{
    if(!m_source.sourceIsCurrent(error)) return false;
    QFile source(m_source.path());
    QTemporaryFile staged(QFileInfo(destinationPath).absolutePath()+"/.mapper-res-XXXXXX");
    if(!source.open(QIODevice::ReadOnly) || !staged.open()) {
        setError(error,"Cannot open RES source/staging file."); return false;
    }
    const auto write=[&](const QByteArray& b){return staged.write(b)==b.size();};
    const auto align=[&](){return write(QByteArray((16-staged.pos()%16)%16,'\0'));};
    if(!write(QByteArray(16,'\0')))return false;
    struct OutputMember { QByteArray name; quint32 size; quint32 offset; quint32 time; int sourceIndex; QByteArray payload; bool changed; };
    QVector<OutputMember> outputMembers;
    for(const auto& member:m_source.members()) {
        if(isDeleted(member.name))continue;
        const auto finalName=finalNameFor(member.name);
        OutputMember out{finalName==member.name?member.encodedName:encodedCp1251(finalName),member.dataSize,0,member.lastWriteTime,member.tableIndex,{},false};
        for(const auto& c:m_changes)if(c.kind==Change::Replace && c.originalName.compare(member.name,Qt::CaseInsensitive)==0){
            out.payload=c.payload;out.size=c.payload.size();out.changed=true;
        }
        outputMembers<<out;
    }
    for(const auto& c:m_changes)if(c.kind==Change::Add)
        outputMembers<<OutputMember{encodedCp1251(c.finalName),quint32(c.payload.size()),0,c.lastWriteTime,-1,c.payload,true};
    if(outputMembers.isEmpty()){setError(error,"Cannot delete every RES member.");return false;}
    for(auto& out:outputMembers){
        if(!align() || quint64(staged.pos())+out.size>std::numeric_limits<quint32>::max()
           || out.name.size()>65535){setError(error,"RES offset/name limit or write error.");return false;}
        out.offset=quint32(staged.pos());
        if(out.changed){if(!write(out.payload))return false;}
        else {
            if(!source.seek(m_source.member(out.sourceIndex).dataOffset))return false;
            qint64 remaining=out.size;
            while(remaining){auto b=source.read(qMin(remaining,qint64(4*1024*1024)));
                if(b.isEmpty()||!write(b)){setError(error,"RES streaming copy failed.");return false;}remaining-=b.size();}
        }
    }
    if(!align())return false;
    const quint32 tableOffset=quint32(staged.pos());
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
    // The game walks these chains from the name's hash bucket. A table that
    // merely enumerates all names is insufficient evidence of readability.
    for(int memberIndex=0;memberIndex<outputMembers.size();++memberIndex){
        int slot=int(nameHash(outputMembers[memberIndex].name,count));QSet<int> visited;bool found=false;
        while(slot>=0&&slot<hashSlots.size()&&!visited.contains(slot)){
            visited.insert(slot);if(hashSlots[slot]==memberIndex){found=true;break;}
            slot=next[slot]==quint32(-1)?-1:int(next[slot]);
        }
        if(!found){setError(error,"RES member is unreachable through the game hash chain.");return false;}
    }
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
    QByteArray header(16,'\0');
    put32(&header,0,kSignature);put32(&header,4,count);
    put32(&header,8,tableOffset);put32(&header,12,quint32(names.size()));
    if(!write(table)||!write(names)||!staged.seek(0)||!write(header)||!staged.flush()) {
        setError(error,"Cannot finish RES staging file.");return false;
    }
    CResArchiveIndex reopened;
    if(!reopened.open(staged.fileName(),error)||reopened.memberCount()!=outputMembers.size())return false;
    // Exact changed bytes; stream-compare every unchanged payload and timestamp.
    QFile check(staged.fileName());if(!check.open(QIODevice::ReadOnly))return false;
    QTextCodec* codec=QTextCodec::codecForName("CP1251");
    for(const auto& out:outputMembers){
        const int i=reopened.memberIndex(codec->toUnicode(out.name));
        if(i<0||reopened.member(i).dataSize!=out.size||reopened.member(i).lastWriteTime!=out.time)return false;
        if(out.changed){if(reopened.readMember(i,error)!=out.payload)return false;}
        else {
            if(!check.seek(reopened.member(i).dataOffset)||!source.seek(m_source.member(out.sourceIndex).dataOffset))return false;
            qint64 remaining=out.size;
            while(remaining){qint64 n=qMin(remaining,qint64(4*1024*1024));
                auto b=source.read(n);if(b.size()!=n||check.read(n)!=b){setError(error,"Unchanged RES member differs.");return false;}remaining-=n;}
        }
    }
    check.close();source.close();
    if(!m_source.sourceIsCurrent(error))return false;
    const auto expected=resFileSha256(staged.fileName(),error);
    if(expected.isEmpty())return false;
    if(candidateSha256)*candidateSha256=expected;
    // QSaveFile has direct-write fallback disabled: interruption keeps the old file.
    QSaveFile destination(destinationPath);destination.setDirectWriteFallback(false);
    if(!destination.open(QIODevice::WriteOnly)||!staged.seek(0))return false;
    while(!staged.atEnd()){auto b=staged.read(4*1024*1024);
        if(b.isEmpty()||destination.write(b)!=b.size()){setError(error,"Cannot copy staged RES.");return false;}}
    if(!destination.flush()||!m_source.sourceIsCurrent(error)||(beforeCommit&&!beforeCommit()))return false;
    if(!destination.commit()){setError(error,destination.errorString());return false;}
    if(resFileSha256(destinationPath,error)!=expected){setError(error,"Committed RES differs from the validated staging file.");return false;}
    return true;
}

QByteArray resFileSha256(const QString& path, QString* error)
{
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){setError(error,f.errorString());return {};}
    CNativeSha256 native;const bool system=native.isAvailable();QCryptographicHash portable(QCryptographicHash::Sha256);
    while(!f.atEnd()){auto b=f.read(4*1024*1024);if(b.isEmpty()){setError(error,f.errorString());return {};}
        if(system)native.addData(b);else portable.addData(b);}
    auto digest=system?native.result():portable.result();
    if(digest.isEmpty()&&system){f.close();if(!f.open(QIODevice::ReadOnly)||!portable.addData(&f))return {};digest=portable.result();}
    return digest.toHex();
}

bool replaceResFromFile(const QString& destination,const QString& candidate,const QByteArray& expected,QString* error)
{
    auto lease=CProcessEditLock::acquire(destination,error,false);if(!lease)return false;
    const auto wanted=resFileSha256(candidate,error);if(wanted.isEmpty())return false;
    QFile input(candidate);QSaveFile output(destination);output.setDirectWriteFallback(false);
    if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))return false;
    while(!input.atEnd()){auto b=input.read(4*1024*1024);if(b.isEmpty()||output.write(b)!=b.size())return false;}
    if(resFileSha256(destination,error)!=expected){setError(error,"RES changed before atomic replacement.");return false;}
    return output.commit()&&resFileSha256(destination,error)==wanted;
}

bool CResArchiveIndex::writeMemberReplacement(const QString& destinationPath,
    const QString& name, const QByteArray& payload, QString* error,
    const std::function<bool()>& beforeCommit, QByteArray* candidateSha256) const
{
    const int changed = memberIndex(name);
    if(changed < 0 || !sourceIsCurrent(error)) {
        setError(error, "Replacement member is missing, ambiguous, or stale."); return false;
    }
    QFile source(path());
    QTemporaryFile staged(QFileInfo(destinationPath).absolutePath()+"/.mapper-member-XXXXXX");
    if(!source.open(QIODevice::ReadOnly) || !staged.open()) {
        setError(error, "Cannot open RES source/staging file."); return false;
    }
    QByteArray header = source.read(kHeaderSize);
    const qint64 tableOffset = u32(header, 8);
    const auto& target = member(changed);
    const qint64 oldEnd = qint64(target.dataOffset) + target.dataSize;
    const qint64 delta = qint64(payload.size()) - target.dataSize;
    if(tableOffset + delta < qint64(kHeaderSize)
       || quint64(tableOffset + delta) > std::numeric_limits<quint32>::max()) {
        setError(error, "Replacement exceeds the RES offset range."); return false;
    }
    QVector<QPair<quint64, quint64>> ranges;
    for(const auto& m : members()) {
        const quint64 end = quint64(m.dataOffset) + m.dataSize;
        if(m.dataOffset < kHeaderSize || end > quint64(tableOffset)) {
            setError(error, "RES payload overlaps the header or directory."); return false;
        }
        if(m.dataSize) ranges.append(qMakePair(quint64(m.dataOffset), end));
    }
    std::sort(ranges.begin(), ranges.end());
    for(int i=1;i<ranges.size();++i) if(ranges[i].first < ranges[i-1].second) {
        setError(error, "RES payloads overlap; replacement is unsafe."); return false;
    }
    if(!source.seek(tableOffset)) return false;
    QByteArray table = source.read(qint64(memberCount()) * kEntrySize);
    if(table.size() != qint64(memberCount()) * kEntrySize) return false;
    put32(&header, 8, quint32(tableOffset + delta));
    for(const auto& m : members()) {
        const quint64 offset = quint64(m.tableIndex) * kEntrySize;
        if(m.tableIndex == changed) put32(&table, offset+4, quint32(payload.size()));
        else if(m.dataOffset > target.dataOffset)
            put32(&table, offset+8, quint32(qint64(m.dataOffset)+delta));
    }
    QCryptographicHash expected(QCryptographicHash::Sha256);
    const auto write = [&](const QByteArray& bytes) {
        expected.addData(bytes);
        if(staged.write(bytes) == bytes.size()) return true;
        setError(error, staged.errorString()); return false;
    };
    const auto copy = [&](qint64 offset, qint64 count) {
        if(!source.seek(offset)) return false;
        while(count > 0) {
            const qint64 n = qMin(count, qint64(4*1024*1024));
            const auto bytes = source.read(n);
            if(bytes.size()!=n || !write(bytes)) return false;
            count -= n;
        }
        return true;
    };
    if(!write(header) || !copy(kHeaderSize, target.dataOffset-kHeaderSize)
       || !write(payload) || !copy(oldEnd, tableOffset-oldEnd) || !write(table)
       || !copy(tableOffset+table.size(), source.size()-tableOffset-table.size())
       || !staged.flush()) return false;
    const QByteArray wanted = expected.result().toHex();
    CResArchiveIndex reopened;
    if(resFileSha256(staged.fileName(), error)!=wanted
       || !reopened.open(staged.fileName(), error)
       || reopened.readMember(name, error)!=payload) {
        setError(error, "Fresh streamed replacement validation failed."); return false;
    }
    // The streamed digest covers original gaps, directory slots and names too.
    if(candidateSha256) *candidateSha256 = wanted;
    source.close();
    QSaveFile destination(destinationPath); destination.setDirectWriteFallback(false);
    if(!destination.open(QIODevice::WriteOnly) || !staged.seek(0)) {
        setError(error, destination.errorString()); return false;
    }
    while(!staged.atEnd()) {
        const auto bytes = staged.read(4*1024*1024);
        if(bytes.isEmpty() || destination.write(bytes)!=bytes.size()) {
            setError(error, "Cannot copy staged replacement."); return false;
        }
    }
    if(!destination.flush() || !sourceIsCurrent(error) || (beforeCommit && !beforeCommit())) return false;
    if(!destination.commit()) { setError(error, destination.errorString()); return false; }
    if(resFileSha256(destinationPath,error)!=wanted) {
        setError(error, "Committed RES differs from staged replacement."); return false;
    }
    return true;
}

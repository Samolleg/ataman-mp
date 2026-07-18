#include "mpr_document.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextCodec>
#include <QtEndian>

#include <algorithm>
#include <cstring>

#include "endurance_document.h"

namespace {
const quint32 kResSignature = 0x019CE23C;
const quint32 kMapSignature = 0xCE4AF672;
const quint32 kSectorSignature = 0xCF4BF774;
const quint64 kResHeaderSize = 16;
const quint64 kResEntrySize = 22;
const quint64 kMapHeaderSize = 38;
const quint64 kMaterialSize = 44;
const quint64 kSectorVertexCount = 33 * 33;
const quint64 kSectorVertexSize = 8;
const quint64 kSectorTileCount = 16 * 16;
const quint64 kSectorTileSize = 2;
const quint64 kLandSectorSize = 5 + kSectorVertexCount * kSectorVertexSize
    + kSectorTileCount * kSectorTileSize;
const quint64 kOverlaySectorSize = 5 + 2 * kSectorVertexCount * kSectorVertexSize
    + 3 * kSectorTileCount * kSectorTileSize;

QByteArray sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool rangeFits(const QByteArray& bytes, quint64 offset, quint64 size)
{
    return offset <= quint64(bytes.size()) && size <= quint64(bytes.size()) - offset;
}

quint16 readU16(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

qint16 readI16(const QByteArray& bytes, quint64 offset)
{
    return qint16(readU16(bytes, offset));
}

quint32 readU32(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

qint32 readI32(const QByteArray& bytes, quint64 offset)
{
    return qint32(readU32(bytes, offset));
}

float readF32(const QByteArray& bytes, quint64 offset)
{
    const quint32 raw = readU32(bytes, offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "Unexpected float representation");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

QString normalizedName(const QString& name)
{
    return name.toCaseFolded();
}

void copyFindings(const CValidationReport& source, CValidationReport* destination)
{
    if(!destination)
        return;
    for(const SValidationFinding& finding : source.findings())
        destination->add(finding.code, finding.severity, finding.message, finding.location);
}

QString memberKindName(EMprMemberKind kind)
{
    switch(kind)
    {
    case EMprMemberKind::MapParameters: return QStringLiteral("MP");
    case EMprMemberKind::Sector: return QStringLiteral("SEC");
    case EMprMemberKind::Unknown: break;
    }
    return QStringLiteral("unknown");
}
}

qint32 SMprMapInfo::tileType(int atlasSlot, bool* found) const
{
    const bool present = atlasSlot >= 0 && atlasSlot < tileTypes.size();
    if(found)
        *found = present;
    return present ? tileTypes.at(atlasSlot) : 0;
}

SMprPackedTile SMprPackedTile::decode(quint16 packed)
{
    SMprPackedTile tile;
    tile.tileIndex = quint8(packed & 0x3F);
    tile.atlasIndex = quint8((packed >> 6) & 0xFF);
    tile.rotation = quint8((packed >> 14) & 0x03);
    return tile;
}

quint16 SMprPackedTile::encode() const
{
    return quint16(tileIndex & 0x3F)
        | quint16(quint16(atlasIndex) << 6)
        | quint16(quint16(rotation & 0x03) << 14);
}

void CMprEditTransaction::replaceMemberPayload(
    const QString& memberName, const QByteArray& expectedPayloadSha256,
    const QByteArray& replacementPayload)
{
    SMprMemberEdit edit;
    edit.memberName = memberName;
    edit.expectedPayloadSha256 = expectedPayloadSha256;
    edit.replacementPayload = replacementPayload;
    m_edits.append(edit);
}

bool CMprDocument::load(const QFileInfo& file, CValidationReport* report)
{
    QFile input(file.filePath());
    if(!input.open(QIODevice::ReadOnly))
    {
        if(report)
            report->add("mpr.open", EValidationSeverity::Error,
                        input.errorString(), file.filePath());
        return false;
    }
    const QByteArray bytes = input.readAll();
    if(input.error() != QFile::NoError)
    {
        if(report)
            report->add("mpr.read", EValidationSeverity::Error,
                        input.errorString(), file.filePath());
        return false;
    }
    return loadBytes(bytes, file.filePath(), report);
}

bool CMprDocument::loadBytes(const QByteArray& bytes, const QString& sourceName,
                             CValidationReport* report)
{
    m_originalBytes = bytes;
    m_members.clear();
    m_mapInfo = SMprMapInfo();
    m_sectors.clear();
    m_tableOffset = 0;
    m_namesLength = 0;
    const QString source = sourceName.isEmpty() ? QStringLiteral("memory.mpr") : sourceName;

    if(bytes.size() < qint64(kResHeaderSize))
    {
        if(report)
            report->add("mpr.envelope", EValidationSeverity::Error,
                        "MPR is smaller than a RES header", source);
        return false;
    }

    const quint32 signature = readU32(bytes, 0);
    const quint32 tableSize = readU32(bytes, 4);
    m_tableOffset = readU32(bytes, 8);
    m_namesLength = readU32(bytes, 12);
    if(signature != kResSignature)
    {
        if(report)
            report->add("mpr.signature", EValidationSeverity::Error,
                        "Invalid MPR/RES signature", source);
        return false;
    }

    const quint64 tableBytes = quint64(tableSize) * kResEntrySize;
    const quint64 namesOffset = quint64(m_tableOffset) + tableBytes;
    if(m_tableOffset < kResHeaderSize ||
       !rangeFits(bytes, m_tableOffset, tableBytes) ||
       !rangeFits(bytes, namesOffset, m_namesLength))
    {
        if(report)
            report->add("mpr.table", EValidationSeverity::Error,
                        "The RES table or filename buffer extends outside the MPR file", source);
        return false;
    }

    QTextCodec* cp1251 = QTextCodec::codecForName("CP1251");
    QMap<quint32, quint32> occupiedDataRanges;
    for(quint32 index = 0; index < tableSize; ++index)
    {
        const quint64 entryOffset = quint64(m_tableOffset) + quint64(index) * kResEntrySize;
        SMprArchiveMember member;
        member.tableIndex = int(index);
        member.nextIndex = readU32(bytes, entryOffset);
        member.dataSize = readU32(bytes, entryOffset + 4);
        member.dataOffset = readU32(bytes, entryOffset + 8);
        member.lastWriteTime = readU32(bytes, entryOffset + 12);
        member.nameLength = readU16(bytes, entryOffset + 16);
        member.nameOffset = readU32(bytes, entryOffset + 18);

        const bool nextValid = member.nextIndex == quint32(-1) || member.nextIndex < tableSize;
        const bool dataValid = member.dataOffset >= kResHeaderSize &&
            quint64(member.dataOffset) + member.dataSize <= m_tableOffset;
        const bool nameValid = quint64(member.nameOffset) + member.nameLength <= m_namesLength;
        if(!nextValid || !dataValid || !nameValid)
        {
            if(report)
                report->add("mpr.entry", EValidationSeverity::Error,
                            QString("Invalid MPR resource-table entry %1").arg(index), source);
            return false;
        }

        if(member.dataSize > 0)
        {
            const auto following = occupiedDataRanges.lowerBound(member.dataOffset);
            bool overlapsPrevious = false;
            if(following != occupiedDataRanges.begin())
            {
                auto previous = following;
                --previous;
                overlapsPrevious = quint64(previous.key()) + previous.value() > member.dataOffset;
            }
            if((following != occupiedDataRanges.end() &&
                quint64(member.dataOffset) + member.dataSize > following.key()) || overlapsPrevious)
            {
                if(report)
                    report->add("mpr.member_overlap", EValidationSeverity::Error,
                                QString("Archive member %1 overlaps another payload").arg(index), source);
                return false;
            }
            occupiedDataRanges.insert(member.dataOffset, member.dataSize);
        }

        member.encodedName = bytes.mid(int(namesOffset + member.nameOffset), member.nameLength);
        member.name = cp1251 ? cp1251->toUnicode(member.encodedName)
                             : QString::fromLatin1(member.encodedName);
        member.payload = bytes.mid(member.dataOffset, member.dataSize);
        member.sha256 = sha256(member.payload);
        m_members.append(member);
    }

    QVector<int> mpMembers;
    for(int index = 0; index < m_members.size(); ++index)
    {
        if(m_members.at(index).name.endsWith(".mp", Qt::CaseInsensitive))
            mpMembers.append(index);
    }
    if(mpMembers.size() > 1)
    {
        if(report)
            report->add("mpr.mp_namespace", EValidationSeverity::Error,
                        QString("MPR contains %1 MP namespace members").arg(mpMembers.size()), source);
        return false;
    }

    if(mpMembers.size() == 1)
    {
        const int mpIndex = mpMembers.front();
        SMprArchiveMember& mpMember = m_members[mpIndex];
        mpMember.kind = EMprMemberKind::MapParameters;
        const QByteArray& mp = mpMember.payload;
        if(mp.size() < qint64(kMapHeaderSize) || readU32(mp, 0) != kMapSignature)
        {
            if(report)
                report->add("mpr.mp_header", EValidationSeverity::Error,
                            "The MP member has an invalid or truncated header", mpMember.name);
            return false;
        }

        m_mapInfo.valid = true;
        m_mapInfo.memberName = mpMember.name;
        m_mapInfo.innerNamespace = mpMember.name.left(mpMember.name.size() - 3);
        m_mapInfo.signature = readU32(mp, 0);
        m_mapInfo.maxZ = readF32(mp, 4);
        m_mapInfo.sectorCountX = readU32(mp, 8);
        m_mapInfo.sectorCountY = readU32(mp, 12);
        m_mapInfo.textureCount = readU32(mp, 16);
        m_mapInfo.textureSize = readU32(mp, 20);
        m_mapInfo.tileCount = readU32(mp, 24);
        m_mapInfo.tileSize = readU32(mp, 28);
        m_mapInfo.materialCount = readU16(mp, 32);
        m_mapInfo.animatedTileCount = readU32(mp, 34);
        m_mapInfo.parsedSize = kMapHeaderSize
            + quint64(m_mapInfo.materialCount) * kMaterialSize
            + quint64(m_mapInfo.tileCount) * 4
            + quint64(m_mapInfo.animatedTileCount) * 4;
        if(m_mapInfo.parsedSize > quint64(mp.size()))
        {
            if(report)
                report->add("mpr.mp_payload", EValidationSeverity::Error,
                            "The MP arrays extend beyond the member payload", mpMember.name);
            return false;
        }
        const quint64 tileTypeOffset = kMapHeaderSize
            + quint64(m_mapInfo.materialCount) * kMaterialSize;
        m_mapInfo.tileTypes.reserve(int(m_mapInfo.tileCount));
        for(quint32 tile = 0; tile < m_mapInfo.tileCount; ++tile)
            m_mapInfo.tileTypes.append(readI32(mp, tileTypeOffset + quint64(tile) * 4));
        if(m_mapInfo.parsedSize < quint64(mp.size()) && report)
            report->add("mpr.mp_trailing", EValidationSeverity::Warning,
                        QString("MP has %1 trailing bytes; they are preserved")
                            .arg(quint64(mp.size()) - m_mapInfo.parsedSize), mpMember.name);

        const QString escapedNamespace = QRegularExpression::escape(m_mapInfo.innerNamespace);
        const QRegularExpression sectorPattern(
            QStringLiteral("^") + escapedNamespace + QStringLiteral("(\\d{3})(\\d{3})\\.sec$"),
            QRegularExpression::CaseInsensitiveOption);
        QSet<QString> presentSectors;
        for(int index = 0; index < m_members.size(); ++index)
        {
            SMprArchiveMember& member = m_members[index];
            const QRegularExpressionMatch match = sectorPattern.match(member.name);
            if(!match.hasMatch())
                continue;
            member.kind = EMprMemberKind::Sector;
            presentSectors.insert(normalizedName(member.name));

            const QByteArray& sec = member.payload;
            if(sec.size() < 5 || readU32(sec, 0) != kSectorSignature)
            {
                if(report)
                    report->add("mpr.sec_header", EValidationSeverity::Error,
                                "Sector has an invalid or truncated header", member.name);
                return false;
            }

            SMprSectorInfo sector;
            sector.memberIndex = index;
            sector.memberName = member.name;
            sector.sectorX = match.captured(1).toInt();
            sector.sectorY = match.captured(2).toInt();
            sector.type = quint8(sec.at(4));
            sector.hasOverlay = sector.type == 3;
            if(sector.type != 0 && sector.type != 3)
            {
                if(report)
                    report->add("mpr.sec_type", EValidationSeverity::Warning,
                                QString("Unknown sector type %1 is preserved and not editable")
                                    .arg(sector.type), member.name);
                m_sectors.append(sector);
                continue;
            }

            sector.landTileOffset = 5 + kSectorVertexCount * kSectorVertexSize;
            sector.parsedSize = kLandSectorSize;
            if(sector.hasOverlay)
            {
                sector.overlayVertexOffset = sector.landTileOffset;
                sector.landTileOffset += kSectorVertexCount * kSectorVertexSize;
                sector.overlayTileOffset = sector.landTileOffset + kSectorTileCount * kSectorTileSize;
                sector.overlayMaterialOffset = sector.overlayTileOffset + kSectorTileCount * kSectorTileSize;
                sector.parsedSize = kOverlaySectorSize;
            }
            if(sector.parsedSize > quint64(sec.size()))
            {
                if(report)
                    report->add("mpr.sec_payload", EValidationSeverity::Error,
                                "Sector arrays extend beyond the member payload", member.name);
                return false;
            }
            if(sector.parsedSize < quint64(sec.size()) && report)
                report->add("mpr.sec_trailing", EValidationSeverity::Warning,
                            QString("Sector has %1 trailing bytes; they are preserved")
                                .arg(quint64(sec.size()) - sector.parsedSize), member.name);

            const auto validateTiles = [&](quint64 tileOffset, const QString& layer,
                                           const QVector<qint16>* materials)
            {
                for(quint64 tile = 0; tile < kSectorTileCount; ++tile)
                {
                    if(materials && materials->at(int(tile)) == -1)
                        continue;
                    const SMprPackedTile packed = SMprPackedTile::decode(
                        readU16(sec, tileOffset + tile * 2));
                    if(packed.atlasIndex >= m_mapInfo.textureCount && report)
                    {
                        report->add("mpr.atlas_namespace", EValidationSeverity::Warning,
                                    QString("%1 tile %2 references atlas %3, outside namespace count %4")
                                        .arg(layer).arg(tile).arg(packed.atlasIndex)
                                        .arg(m_mapInfo.textureCount),
                                    member.name);
                    }
                }
            };
            validateTiles(sector.landTileOffset, QStringLiteral("land"), nullptr);
            if(sector.hasOverlay)
            {
                sector.overlayMaterialIndices.reserve(int(kSectorTileCount));
                for(quint64 tile = 0; tile < kSectorTileCount; ++tile)
                {
                    const qint16 material = readI16(sec, sector.overlayMaterialOffset + tile * 2);
                    sector.overlayMaterialIndices.append(material);
                    if(material != -1 &&
                       (material < 0 || int(material) >= int(m_mapInfo.materialCount)) && report)
                    {
                        report->add("mpr.overlay_material", EValidationSeverity::Warning,
                                    QString("Overlay tile %1 has unknown raw material reference 0x%2; it is preserved")
                                        .arg(tile)
                                        .arg(quint16(material), 4, 16, QChar('0')),
                                    member.name);
                    }
                }
                validateTiles(sector.overlayTileOffset, QStringLiteral("overlay"),
                              &sector.overlayMaterialIndices);
            }
            m_sectors.append(sector);
        }

        for(quint32 y = 0; y < m_mapInfo.sectorCountY; ++y)
        {
            for(quint32 x = 0; x < m_mapInfo.sectorCountX; ++x)
            {
                const QString expected = QString("%1%2%3.sec")
                    .arg(m_mapInfo.innerNamespace)
                    .arg(x, 3, 10, QChar('0'))
                    .arg(y, 3, 10, QChar('0'));
                if(!presentSectors.contains(normalizedName(expected)))
                {
                    if(report)
                        report->add("mpr.sec_missing", EValidationSeverity::Error,
                                    "Expected sector member is missing", expected);
                    return false;
                }
            }
        }
    }

    if(report)
    {
        report->add("mpr.archive_coverage", EValidationSeverity::Info,
                    QString("Parsed %1 archive members while preserving all %2 bytes")
                        .arg(m_members.size()).arg(bytes.size()), source);
        report->add("mpr.lossless", EValidationSeverity::Info,
                    "Unchanged serialization is byte-identical", QFileInfo(source).fileName());
    }
    return true;
}

int CMprDocument::uniqueMemberIndex(const QString& name) const
{
    int result = -1;
    for(int index = 0; index < m_members.size(); ++index)
    {
        if(m_members.at(index).name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        if(result != -1)
            return -2;
        result = index;
    }
    return result;
}

QByteArray CMprDocument::memberPayload(const QString& name) const
{
    const int index = uniqueMemberIndex(name);
    return index >= 0 ? m_members.at(index).payload : QByteArray();
}

bool CMprDocument::applyTransaction(const CMprEditTransaction& transaction,
                                    QByteArray* output,
                                    CValidationReport* report) const
{
    if(output)
        output->clear();
    if(!output)
    {
        if(report)
            report->add("mpr.transaction_output", EValidationSeverity::Error,
                        "No output buffer was supplied");
        return false;
    }
    if(transaction.isEmpty())
    {
        *output = m_originalBytes;
        return true;
    }

    QByteArray candidate = m_originalBytes;
    QSet<int> editedIndices;
    for(const SMprMemberEdit& edit : transaction.edits())
    {
        const int index = uniqueMemberIndex(edit.memberName);
        if(index == -2)
        {
            if(report)
                report->add("mpr.transaction_ambiguous", EValidationSeverity::Error,
                            "The member name is ambiguous under case-insensitive lookup",
                            edit.memberName);
            return false;
        }
        if(index < 0)
        {
            if(report)
                report->add("mpr.transaction_missing", EValidationSeverity::Error,
                            "The archive member does not exist", edit.memberName);
            return false;
        }
        if(editedIndices.contains(index))
        {
            if(report)
                report->add("mpr.transaction_duplicate", EValidationSeverity::Error,
                            "The archive member was edited more than once", edit.memberName);
            return false;
        }
        const SMprArchiveMember& member = m_members.at(index);
        if(member.kind == EMprMemberKind::Unknown)
        {
            if(report)
                report->add("mpr.transaction_unknown", EValidationSeverity::Error,
                            "Unknown archive members are preservation-only", edit.memberName);
            return false;
        }
        if(edit.expectedPayloadSha256.toLower() != member.sha256.toLower())
        {
            if(report)
                report->add("mpr.transaction_provenance", EValidationSeverity::Error,
                            "The member payload no longer matches the expected SHA-256",
                            edit.memberName);
            return false;
        }
        if(edit.replacementPayload.size() != member.payload.size())
        {
            if(report)
                report->add("mpr.transaction_length", EValidationSeverity::Error,
                            "Length-changing MPR edits remain disabled until archive relocation is proven",
                            edit.memberName);
            return false;
        }
        candidate.replace(member.dataOffset, member.dataSize, edit.replacementPayload);
        editedIndices.insert(index);
    }

    CValidationReport reopenedReport;
    CMprDocument reopened;
    if(!reopened.loadBytes(candidate, QStringLiteral("staged-transaction.mpr"), &reopenedReport) ||
       reopenedReport.hasErrors())
    {
        copyFindings(reopenedReport, report);
        if(report)
            report->add("mpr.transaction_reopen", EValidationSeverity::Error,
                        "The patched archive failed a fresh semantic reopen");
        return false;
    }
    if(reopened.members().size() != m_members.size())
    {
        if(report)
            report->add("mpr.transaction_members", EValidationSeverity::Error,
                        "The patched archive changed the member count");
        return false;
    }
    for(int index = 0; index < m_members.size(); ++index)
    {
        const SMprArchiveMember& before = m_members.at(index);
        const SMprArchiveMember& after = reopened.members().at(index);
        const bool metadataEqual = before.tableIndex == after.tableIndex &&
            before.nextIndex == after.nextIndex &&
            before.dataSize == after.dataSize &&
            before.dataOffset == after.dataOffset &&
            before.lastWriteTime == after.lastWriteTime &&
            before.nameLength == after.nameLength &&
            before.nameOffset == after.nameOffset &&
            before.encodedName == after.encodedName;
        if(!metadataEqual || (!editedIndices.contains(index) && before.payload != after.payload))
        {
            if(report)
                report->add("mpr.transaction_locality", EValidationSeverity::Error,
                            "An untouched member or its metadata changed", before.name);
            return false;
        }
    }

    *output = candidate;
    if(report)
        report->add("mpr.transaction", EValidationSeverity::Info,
                    QString("Applied %1 provenance-checked same-length member patch(es)")
                        .arg(editedIndices.size()), transaction.description());
    return true;
}

CMprSemanticDiff CMprSemanticDiff::compare(const CMprDocument& baseline,
                                           const CMprDocument& candidate)
{
    CMprSemanticDiff diff;
    const int common = qMin(baseline.m_members.size(), candidate.m_members.size());
    for(int index = 0; index < common; ++index)
    {
        const SMprArchiveMember& before = baseline.m_members.at(index);
        const SMprArchiveMember& after = candidate.m_members.at(index);
        if(before.name != after.name || before.encodedName != after.encodedName ||
           before.dataOffset != after.dataOffset || before.dataSize != after.dataSize ||
           before.lastWriteTime != after.lastWriteTime || before.nextIndex != after.nextIndex)
        {
            SMprDocumentDiffEntry entry;
            entry.kind = "metadata";
            entry.memberName = before.name;
            entry.baselineIndex = index;
            entry.candidateIndex = index;
            entry.baselineSha256 = before.sha256;
            entry.candidateSha256 = after.sha256;
            entry.details = "Archive member name/order/metadata changed";
            diff.m_entries.append(entry);
        }
        if(before.payload != after.payload)
        {
            SMprDocumentDiffEntry entry;
            entry.kind = "modified";
            entry.memberName = before.name;
            entry.baselineIndex = index;
            entry.candidateIndex = index;
            entry.baselineSha256 = before.sha256;
            entry.candidateSha256 = after.sha256;
            entry.details = QString("%1 payload changed; size %2 -> %3")
                .arg(memberKindName(before.kind))
                .arg(before.payload.size()).arg(after.payload.size());
            diff.m_entries.append(entry);
        }
    }
    for(int index = common; index < baseline.m_members.size(); ++index)
    {
        SMprDocumentDiffEntry entry;
        entry.kind = "removed";
        entry.memberName = baseline.m_members.at(index).name;
        entry.baselineIndex = index;
        entry.baselineSha256 = baseline.m_members.at(index).sha256;
        entry.details = "Archive member removed";
        diff.m_entries.append(entry);
    }
    for(int index = common; index < candidate.m_members.size(); ++index)
    {
        SMprDocumentDiffEntry entry;
        entry.kind = "added";
        entry.memberName = candidate.m_members.at(index).name;
        entry.candidateIndex = index;
        entry.candidateSha256 = candidate.m_members.at(index).sha256;
        entry.details = "Archive member added";
        diff.m_entries.append(entry);
    }
    return diff;
}

QString CMprSemanticDiff::toText() const
{
    QStringList lines;
    for(const SMprDocumentDiffEntry& entry : m_entries)
        lines.append(QString("%1 %2: %3").arg(entry.kind, entry.memberName, entry.details));
    return lines.join('\n');
}

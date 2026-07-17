#include "endurance_document.h"

#include <QDataStream>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QTextCodec>
#include <QTextStream>
#include <QtEndian>

#include <cmath>
#include <climits>
#include <cstring>

namespace {
QString severityName(EValidationSeverity severity)
{
    switch(severity)
    {
    case EValidationSeverity::Info: return "info";
    case EValidationSeverity::Warning: return "warning";
    case EValidationSeverity::Error: return "error";
    }
    return "error";
}

bool readWholeFile(const QFileInfo& info, QByteArray& output, CValidationReport* report)
{
    QFile file(info.filePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        if(report)
            report->add("file.open", EValidationSeverity::Error, file.errorString(), info.filePath());
        return false;
    }
    output = file.readAll();
    if(output.isEmpty())
    {
        if(report)
            report->add("file.empty", EValidationSeverity::Error, "File is empty", info.filePath());
        return false;
    }
    return true;
}

quint32 readU32(const QByteArray& bytes, int offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

float readFloat32(const QByteArray& bytes, int offset)
{
    const quint32 raw = readU32(bytes, offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw), "32-bit float required");
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

void appendU32(QByteArray& bytes, quint32 value)
{
    uchar encoded[4];
    qToLittleEndian<quint32>(value, encoded);
    bytes.append(reinterpret_cast<const char*>(encoded), 4);
}

bool isMobContainer(quint32 type)
{
    static const QSet<quint32> containers = {
        0x1E00, 0x1E01, 0x1E02, 0x1E03, 0x1E0B, 0x1E0E,
        0xA000, 0xAA01, 0xABD0, 0xB000, 0xB001,
        0xBBAB0000, 0xBBAC0000, 0xBBBB0000, 0xBBBC0000,
        0xBBBD0000, 0xBBBE0000, 0xBBBF0000,
        0xCC01, 0xDD01, 0xDDDDDDD1,
        0xE000, 0xE001, 0xF000, 0xFF00, 0xFF01, 0xFF02
    };
    return containers.contains(type);
}

QString sectionTypeName(quint32 type)
{
    static const QMap<quint32, QString> names = {
        {0xA000, "OBJECT_DB_FILE"}, {0xB000, "OBJECT_SECTION"}, {0xB001, "OBJECT"},
        {0xB002, "NID"}, {0xB003, "OBJ_TYPE"}, {0xB004, "OBJ_NAME"},
        {0xB006, "OBJ_TEMPLATE"}, {0xB009, "OBJ_POSITION"},
        {0xBBBB0000, "UNIT"}, {0xBBBB0002, "UNIT_PROTOTYPE"},
        {0xAA01, "LIGHT"}, {0xCC01, "SOUND"}, {0xDD01, "PARTICL"},
        {0x31415926, "AI_GRAPH"}
    };
    const auto it = names.constFind(type);
    return it == names.constEnd() ? QString("0x%1").arg(type, 8, 16, QLatin1Char('0')).toUpper() : it.value();
}

class MobSectionParser
{
public:
    MobSectionParser(const QByteArray& bytes, CValidationReport* report, const QString& source)
        : m_bytes(bytes), m_report(report), m_source(source)
    {
    }

    bool parseRange(quint64 begin, quint64 end, const QString& parentPath,
                    QVector<SMobSection>& output, int depth)
    {
        if(depth > 128)
            return fail("mob.depth", "MOB section nesting exceeds 128 levels", parentPath);

        QMap<quint32, int> occurrences;
        quint64 cursor = begin;
        while(cursor < end)
        {
            if(end - cursor < 8)
                return fail("mob.section_header", "Section has an incomplete 8-byte header", parentPath);
            if(cursor > quint64(INT_MAX))
                return fail("mob.offset_overflow", "Section offset cannot be represented safely", parentPath);

            const quint32 type = readU32(m_bytes, int(cursor));
            const quint32 length = readU32(m_bytes, int(cursor + 4));
            const int occurrence = occurrences[type]++;
            const QString path = parentPath + "/" + sectionTypeName(type) +
                QString("[%1]").arg(occurrence);
            if(length < 8)
                return fail("mob.section_length", QString("Section declares invalid length %1").arg(length), path);
            const quint64 sectionEnd = cursor + quint64(length);
            if(sectionEnd < cursor || sectionEnd > end)
                return fail("mob.section_bounds",
                            QString("Section [%1, %2) exceeds parent boundary %3")
                                .arg(cursor).arg(sectionEnd).arg(end), path);

            SMobSection section;
            section.type = type;
            section.declaredLength = length;
            section.offset = cursor;
            section.occurrence = occurrence;
            section.path = path;
            section.rawBytes = m_bytes.mid(int(cursor), int(length));
            section.sha256 = QCryptographicHash::hash(section.rawBytes, QCryptographicHash::Sha256);
            section.container = isMobContainer(type);
            section.opaque = !section.container;
            if(section.container && length > 8)
            {
                if(!parseRange(cursor + 8, sectionEnd, path, section.children, depth + 1))
                    return false;
                section.opaque = false;
            }
            output.append(section);
            ++m_sectionCount;
            if(m_sectionCount > (m_bytes.size() / 8) + 1)
                return fail("mob.zero_progress", "Section parser exceeded the maximum possible section count", path);
            cursor = sectionEnd;
        }
        return cursor == end;
    }

private:
    bool fail(const QString& code, const QString& message, const QString& location)
    {
        if(m_report)
            m_report->add(code, EValidationSeverity::Error, message, m_source + ":" + location);
        return false;
    }

    const QByteArray& m_bytes;
    CValidationReport* m_report = nullptr;
    QString m_source;
    int m_sectionCount = 0;
};

const SMobSection* findSection(const SMobSection& root, quint32 type)
{
    if(root.type == type)
        return &root;
    for(const SMobSection& child : root.children)
        if(const SMobSection* found = findSection(child, type))
            return found;
    return nullptr;
}

QString decodeCp1251(const QByteArray& bytes)
{
    QByteArray value = bytes;
    if(value.endsWith('\0'))
        value.chop(1);
    QTextCodec* codec = QTextCodec::codecForName("Windows-1251");
    return codec ? codec->toUnicode(value) : QString::fromLatin1(value);
}

QByteArray payload(const SMobSection& section)
{
    return section.rawBytes.mid(8);
}

SMobObjectSnapshot snapshotObject(const SMobSection& section)
{
    SMobObjectSnapshot object;
    object.sectionPath = section.path;
    object.sectionType = section.type;
    object.occurrence = section.occurrence;
    object.subtreeSha256 = section.sha256;
    if(const SMobSection* nid = findSection(section, 0xB002))
    {
        const QByteArray data = payload(*nid);
        if(data.size() == 4)
        {
            object.hasNid = true;
            object.nid = readU32(data, 0);
        }
    }
    if(const SMobSection* type = findSection(section, 0xB003))
    {
        const QByteArray data = payload(*type);
        if(data.size() == 4)
        {
            object.hasObjectType = true;
            object.objectType = readU32(data, 0);
        }
    }
    if(const SMobSection* name = findSection(section, 0xB004))
        object.name = decodeCp1251(payload(*name));
    const SMobSection* prototype = findSection(section, 0xBBBB0002);
    if(!prototype)
        prototype = findSection(section, 0xB006);
    if(prototype)
        object.prototype = decodeCp1251(payload(*prototype));
    if(const SMobSection* position = findSection(section, 0xB009))
    {
        const QByteArray data = payload(*position);
        if(data.size() == 12)
        {
            object.hasPosition = true;
            object.positionX = readFloat32(data, 0);
            object.positionY = readFloat32(data, 4);
            object.positionZ = readFloat32(data, 8);
        }
    }
    return object;
}

void collectObjects(const QVector<SMobSection>& sections, QVector<SMobObjectSnapshot>& objects)
{
    for(const SMobSection& section : sections)
    {
        if(section.type == 0xB000)
        {
            for(const SMobSection& child : section.children)
                objects.append(snapshotObject(child));
        }
        else
            collectObjects(section.children, objects);
    }
}

QString diffKindName(EMobDiffKind kind)
{
    switch(kind)
    {
    case EMobDiffKind::Unchanged: return "unchanged";
    case EMobDiffKind::Added: return "added";
    case EMobDiffKind::Removed: return "removed";
    case EMobDiffKind::Modified: return "modified";
    case EMobDiffKind::Moved: return "moved";
    case EMobDiffKind::Ambiguous: return "ambiguous";
    }
    return "ambiguous";
}

QString objectIdentity(const SMobObjectSnapshot& object)
{
    return QString::number(object.sectionType) + ":" +
        (object.hasObjectType ? QString::number(object.objectType) : QString("-")) + ":" +
        object.prototype + ":" + object.name;
}

bool positionsDiffer(const SMobObjectSnapshot& a, const SMobObjectSnapshot& b)
{
    if(a.hasPosition != b.hasPosition)
        return true;
    if(!a.hasPosition)
        return false;
    return std::fabs(a.positionX - b.positionX) > 0.0001f ||
           std::fabs(a.positionY - b.positionY) > 0.0001f ||
           std::fabs(a.positionZ - b.positionZ) > 0.0001f;
}

bool emitTransactionSection(const SMobSection& section,
                            const QMap<QString, SMobLeafEdit>& edits,
                            QSet<QString>* applied, QByteArray* output,
                            CValidationReport* report)
{
    const auto editIt = edits.constFind(section.path);
    if(editIt != edits.constEnd())
    {
        if(section.container)
        {
            if(report)
                report->add("mob.transaction.container", EValidationSeverity::Error,
                            "A leaf transaction cannot replace a record container", section.path);
            return false;
        }
        if(editIt->expectedSectionSha256.isEmpty() ||
           editIt->expectedSectionSha256 != section.sha256)
        {
            if(report)
                report->add("mob.transaction.provenance", EValidationSeverity::Error,
                            "The source section hash no longer matches the edit provenance", section.path);
            return false;
        }
        const quint64 newLength = quint64(editIt->replacementPayload.size()) + 8;
        if(newLength > quint64(UINT_MAX))
        {
            if(report)
                report->add("mob.transaction.overflow", EValidationSeverity::Error,
                            "Replacement payload exceeds the MOB section length limit", section.path);
            return false;
        }
        appendU32(*output, section.type);
        appendU32(*output, quint32(newLength));
        output->append(editIt->replacementPayload);
        applied->insert(section.path);
        return true;
    }

    if(!section.container || section.children.isEmpty())
    {
        output->append(section.rawBytes);
        return true;
    }

    QByteArray childBytes;
    for(const SMobSection& child : section.children)
        if(!emitTransactionSection(child, edits, applied, &childBytes, report))
            return false;
    const quint64 newLength = quint64(childBytes.size()) + 8;
    if(newLength > quint64(UINT_MAX))
    {
        if(report)
            report->add("mob.transaction.overflow", EValidationSeverity::Error,
                        "Rebuilt container exceeds the MOB section length limit", section.path);
        return false;
    }
    appendU32(*output, section.type);
    appendU32(*output, quint32(newLength));
    output->append(childBytes);
    return true;
}

const SMobSection* findObjectChild(const QVector<SMobSection>& sections,
                                   const QString& path)
{
    for(const SMobSection& section : sections)
    {
        if(section.type == 0xB000)
            for(const SMobSection& object : section.children)
                if(object.path == path)
                    return &object;
        if(const SMobSection* found = findObjectChild(section.children, path))
            return found;
    }
    return nullptr;
}

bool emitObjectSubtreeChange(const SMobSection& section, const QString& targetPath,
                             bool duplicate, int* applications, QByteArray* output,
                             CValidationReport* report)
{
    if(section.path == targetPath)
    {
        if(++(*applications) != 1)
        {
            if(report)
                report->add("mob.subtree.duplicate_path", EValidationSeverity::Error,
                            "The object path was encountered more than once", targetPath);
            return false;
        }
        if(duplicate)
        {
            output->append(section.rawBytes);
            output->append(section.rawBytes);
        }
        return true;
    }

    if(!section.container || section.children.isEmpty())
    {
        output->append(section.rawBytes);
        return true;
    }

    QByteArray childBytes;
    for(const SMobSection& child : section.children)
        if(!emitObjectSubtreeChange(child, targetPath, duplicate, applications,
                                    &childBytes, report))
            return false;
    const quint64 newLength = quint64(childBytes.size()) + 8;
    if(newLength > quint64(UINT_MAX))
    {
        if(report)
            report->add("mob.subtree.overflow", EValidationSeverity::Error,
                        "Rebuilt container exceeds the MOB section length limit",
                        section.path);
        return false;
    }
    appendU32(*output, section.type);
    appendU32(*output, quint32(newLength));
    output->append(childBytes);
    return true;
}
}

QJsonObject SValidationFinding::toJson() const
{
    QJsonObject result;
    result.insert("code", code);
    result.insert("severity", severityName(severity));
    result.insert("message", message);
    if(!location.isEmpty())
        result.insert("location", location);
    return result;
}

void CValidationReport::add(const QString& code, EValidationSeverity severity, const QString& message, const QString& location)
{
    m_findings.append({code, severity, message, location});
}

void CMobEditTransaction::replaceLeafPayload(const QString& sectionPath,
                                             const QByteArray& expectedSectionSha256,
                                             const QByteArray& replacementPayload,
                                             quint32 sectionType)
{
    m_edits.append(SMobLeafEdit(sectionPath, expectedSectionSha256,
                                replacementPayload, sectionType));
}

bool CMobEditTransaction::validatedChangedSaveClass(QString* className) const
{
    if(className)
        className->clear();
    if(m_edits.isEmpty())
        return false;
    for(const SMobLeafEdit& edit : m_edits)
        if(edit.sectionType != 0xB011 || edit.replacementPayload.size() != 1)
            return false;
    if(className)
        *className = "OBJ_PLAYER";
    return true;
}

bool CValidationReport::hasErrors() const
{
    for(const auto& finding : m_findings)
        if(finding.severity == EValidationSeverity::Error)
            return true;
    return false;
}

QJsonArray CValidationReport::toJson() const
{
    QJsonArray result;
    for(const auto& finding : m_findings)
        result.append(finding.toJson());
    return result;
}

QString CValidationReport::toText() const
{
    QString result;
    QTextStream stream(&result);
    if(m_findings.isEmpty())
        stream << "OK: no findings\n";
    for(const auto& finding : m_findings)
    {
        stream << severityName(finding.severity).toUpper() << " " << finding.code << ": " << finding.message;
        if(!finding.location.isEmpty())
            stream << " [" << finding.location << "]";
        stream << "\n";
    }
    return result;
}

QJsonObject SMobObjectSnapshot::toJson() const
{
    QJsonObject result;
    result.insert("path", sectionPath);
    result.insert("sectionType", QString("0x%1").arg(sectionType, 8, 16, QLatin1Char('0')).toUpper());
    result.insert("occurrence", occurrence);
    if(hasNid)
        result.insert("nid", double(nid));
    if(hasObjectType)
        result.insert("objectType", double(objectType));
    if(!name.isEmpty())
        result.insert("name", name);
    if(!prototype.isEmpty())
        result.insert("prototype", prototype);
    if(hasPosition)
    {
        QJsonArray position;
        position.append(positionX);
        position.append(positionY);
        position.append(positionZ);
        result.insert("position", position);
    }
    result.insert("sha256", QString::fromLatin1(subtreeSha256.toHex()));
    return result;
}

QJsonObject SMobDiffEntry::toJson(const QVector<SMobObjectSnapshot>& baseline,
                                  const QVector<SMobObjectSnapshot>& candidate) const
{
    QJsonObject result;
    result.insert("kind", diffKindName(kind));
    result.insert("matchMethod", matchMethod);
    result.insert("confidence", confidence);
    if(baselineIndex >= 0 && baselineIndex < baseline.size())
        result.insert("baseline", baseline.at(baselineIndex).toJson());
    if(candidateIndex >= 0 && candidateIndex < candidate.size())
        result.insert("candidate", candidate.at(candidateIndex).toJson());
    return result;
}

CMobSemanticDiff CMobSemanticDiff::compare(const CMobDocument& baseline,
                                           const CMobDocument& candidate)
{
    CMobSemanticDiff result;
    result.m_baseline = baseline.objects();
    result.m_candidate = candidate.objects();
    QSet<int> matchedBaseline;
    QSet<int> matchedCandidate;

    QMultiMap<QByteArray, int> candidateHashes;
    for(int i = 0; i < result.m_candidate.size(); ++i)
        candidateHashes.insert(result.m_candidate.at(i).subtreeSha256, i);
    for(int i = 0; i < result.m_baseline.size(); ++i)
    {
        const QList<int> candidates = candidateHashes.values(result.m_baseline.at(i).subtreeSha256);
        int available = -1;
        int availableCount = 0;
        for(int index : candidates)
            if(!matchedCandidate.contains(index))
            {
                available = index;
                ++availableCount;
            }
        if(availableCount == 1)
        {
            result.m_entries.append({EMobDiffKind::Unchanged, i, available, "exact-subtree-hash", 1.0});
            matchedBaseline.insert(i);
            matchedCandidate.insert(available);
        }
    }

    QMultiMap<QString, int> baselineNids;
    QMultiMap<QString, int> candidateNids;
    for(int i = 0; i < result.m_baseline.size(); ++i)
        if(!matchedBaseline.contains(i) && result.m_baseline.at(i).hasNid)
            baselineNids.insert(QString::number(result.m_baseline.at(i).sectionType) + ":" +
                                QString::number(result.m_baseline.at(i).nid), i);
    for(int i = 0; i < result.m_candidate.size(); ++i)
        if(!matchedCandidate.contains(i) && result.m_candidate.at(i).hasNid)
            candidateNids.insert(QString::number(result.m_candidate.at(i).sectionType) + ":" +
                                 QString::number(result.m_candidate.at(i).nid), i);
    for(auto it = baselineNids.constBegin(); it != baselineNids.constEnd(); )
    {
        const QString key = it.key();
        const QList<int> left = baselineNids.values(key);
        const QList<int> right = candidateNids.values(key);
        if(left.size() == 1 && right.size() == 1)
        {
            const int baselineIndex = left.front();
            const int candidateIndex = right.front();
            const SMobObjectSnapshot& before = result.m_baseline.at(baselineIndex);
            const SMobObjectSnapshot& after = result.m_candidate.at(candidateIndex);
            const bool movedOnly = positionsDiffer(before, after) &&
                objectIdentity(before) == objectIdentity(after);
            result.m_entries.append({movedOnly ? EMobDiffKind::Moved : EMobDiffKind::Modified,
                                     baselineIndex, candidateIndex, "unique-family-nid", 0.95});
            matchedBaseline.insert(baselineIndex);
            matchedCandidate.insert(candidateIndex);
        }
        it = baselineNids.upperBound(key);
    }

    QMultiMap<QString, int> baselineIdentity;
    QMultiMap<QString, int> candidateIdentity;
    for(int i = 0; i < result.m_baseline.size(); ++i)
        if(!matchedBaseline.contains(i))
            baselineIdentity.insert(objectIdentity(result.m_baseline.at(i)), i);
    for(int i = 0; i < result.m_candidate.size(); ++i)
        if(!matchedCandidate.contains(i))
            candidateIdentity.insert(objectIdentity(result.m_candidate.at(i)), i);
    for(auto it = baselineIdentity.constBegin(); it != baselineIdentity.constEnd(); )
    {
        const QString key = it.key();
        const QList<int> left = baselineIdentity.values(key);
        const QList<int> right = candidateIdentity.values(key);
        if(left.size() == 1 && right.size() == 1)
        {
            result.m_entries.append({positionsDiffer(result.m_baseline.at(left.front()), result.m_candidate.at(right.front()))
                                         ? EMobDiffKind::Moved : EMobDiffKind::Modified,
                                     left.front(), right.front(), "unique-type-prototype-name", 0.75});
            matchedBaseline.insert(left.front());
            matchedCandidate.insert(right.front());
        }
        else if(!right.isEmpty())
        {
            for(int baselineIndex : left)
            {
                result.m_entries.append({EMobDiffKind::Ambiguous, baselineIndex, -1,
                                         "non-unique-type-prototype-name", 0.0});
                matchedBaseline.insert(baselineIndex);
            }
        }
        it = baselineIdentity.upperBound(key);
    }

    for(int i = 0; i < result.m_baseline.size(); ++i)
        if(!matchedBaseline.contains(i))
            result.m_entries.append({EMobDiffKind::Removed, i, -1, "unmatched", 1.0});
    for(int i = 0; i < result.m_candidate.size(); ++i)
        if(!matchedCandidate.contains(i))
            result.m_entries.append({EMobDiffKind::Added, -1, i, "unmatched", 1.0});
    return result;
}

QJsonObject CMobSemanticDiff::toJson() const
{
    QJsonArray entries;
    QMap<QString, int> counts;
    for(const SMobDiffEntry& entry : m_entries)
    {
        entries.append(entry.toJson(m_baseline, m_candidate));
        counts[diffKindName(entry.kind)] += 1;
    }
    QJsonObject summary;
    for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        summary.insert(it.key(), it.value());
    QJsonObject result;
    result.insert("summary", summary);
    result.insert("entries", entries);
    return result;
}

QString CMobSemanticDiff::toText() const
{
    QString output;
    QTextStream stream(&output);
    QMap<QString, int> counts;
    for(const SMobDiffEntry& entry : m_entries)
        counts[diffKindName(entry.kind)] += 1;
    stream << "MOB perspective diff:";
    for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        stream << " " << it.key() << "=" << it.value();
    stream << "\n";
    for(const SMobDiffEntry& entry : m_entries)
    {
        stream << diffKindName(entry.kind).toUpper() << " " << entry.matchMethod;
        if(entry.baselineIndex >= 0)
            stream << " baseline=" << m_baseline.at(entry.baselineIndex).sectionPath;
        if(entry.candidateIndex >= 0)
            stream << " candidate=" << m_candidate.at(entry.candidateIndex).sectionPath;
        stream << "\n";
    }
    return output;
}

static void collectLeafSections(const QVector<SMobSection>& sections,
                                QMap<QString, const SMobSection*>* leaves);
static bool verifyAuthorizedTransactionOutput(const QVector<SMobSection>& sourceSections,
                                              const QVector<SMobSection>& candidateSections,
                                              const QMap<QString, SMobLeafEdit>& edits,
                                              CValidationReport* report);

bool CMobDocument::applyTransaction(const CMobEditTransaction& transaction,
                                    QByteArray* output, CValidationReport* report) const
{
    if(!output)
        return false;
    output->clear();
    if(m_originalBytes.isEmpty() || m_sections.isEmpty())
    {
        if(report)
            report->add("mob.transaction.document", EValidationSeverity::Error,
                        "No parsed source document is available");
        return false;
    }
    if(transaction.isEmpty())
    {
        *output = m_originalBytes;
        return true;
    }

    QMap<QString, SMobLeafEdit> edits;
    for(const SMobLeafEdit& edit : transaction.edits())
    {
        if(edit.sectionPath.isEmpty() || edits.contains(edit.sectionPath))
        {
            if(report)
                report->add("mob.transaction.duplicate_path", EValidationSeverity::Error,
                            "Each transaction path must be non-empty and unique", edit.sectionPath);
            return false;
        }
        edits.insert(edit.sectionPath, edit);
    }

    QSet<QString> applied;
    for(const SMobSection& section : m_sections)
        if(!emitTransactionSection(section, edits, &applied, output, report))
        {
            output->clear();
            return false;
        }
    if(applied.size() != edits.size())
    {
        if(report)
            for(auto it = edits.constBegin(); it != edits.constEnd(); ++it)
                if(!applied.contains(it.key()))
                    report->add("mob.transaction.missing_path", EValidationSeverity::Error,
                                "The transaction path does not exist in the source document", it.key());
        output->clear();
        return false;
    }

    QVector<SMobSection> candidateSections;
    MobSectionParser candidateParser(*output, report, "transaction-output");
    if(!candidateParser.parseRange(0, quint64(output->size()), QString(), candidateSections, 0) ||
       candidateSections.isEmpty() || candidateSections.front().type != 0xA000)
    {
        output->clear();
        return false;
    }
    for(int index = 1; index < candidateSections.size(); ++index)
        if(candidateSections.at(index).type != 0x31415926)
        {
            if(report)
                report->add("mob.transaction.top_level", EValidationSeverity::Error,
                            "Transaction produced an unexpected top-level section",
                            candidateSections.at(index).path);
            output->clear();
            return false;
        }
    if(!verifyAuthorizedTransactionOutput(m_sections, candidateSections, edits, report))
    {
        output->clear();
        return false;
    }
    if(report)
        report->add("mob.transaction.authorized", EValidationSeverity::Info,
                    QString("Applied and independently verified %1 provenance-checked leaf edit(s); all other leaves were emitted byte-for-byte")
                        .arg(applied.size()), transaction.description());
    return true;
}

static bool applyObjectSubtreeChange(const CMobDocument& document,
                                     const QString& objectPath,
                                     const QByteArray& expectedSubtreeSha256,
                                     bool duplicate, QByteArray* output,
                                     CValidationReport* report)
{
    if(!output)
        return false;
    output->clear();
    const SMobSection* object = findObjectChild(document.sections(), objectPath);
    if(!object)
    {
        if(report)
            report->add("mob.subtree.missing_path", EValidationSeverity::Error,
                        "The object path is not a direct OBJECT_SECTION child",
                        objectPath);
        return false;
    }
    if(expectedSubtreeSha256.isEmpty() || object->sha256 != expectedSubtreeSha256)
    {
        if(report)
            report->add("mob.subtree.provenance", EValidationSeverity::Error,
                        "The source object subtree hash no longer matches the operation provenance",
                        objectPath);
        return false;
    }

    int applications = 0;
    for(const SMobSection& section : document.sections())
        if(!emitObjectSubtreeChange(section, objectPath, duplicate, &applications,
                                    output, report))
        {
            output->clear();
            return false;
        }
    if(applications != 1)
    {
        if(report)
            report->add("mob.subtree.application_count", EValidationSeverity::Error,
                        "The object subtree operation did not apply exactly once",
                        objectPath);
        output->clear();
        return false;
    }

    CMobDocument reparsed;
    CValidationReport localReport;
    CValidationReport* validationReport = report ? report : &localReport;
    if(!reparsed.loadBytes(*output, "subtree-transaction-output.mob", validationReport))
    {
        output->clear();
        return false;
    }
    const int expectedCount = document.objects().size() + (duplicate ? 1 : -1);
    if(reparsed.objects().size() != expectedCount)
    {
        if(report)
            report->add("mob.subtree.object_count", EValidationSeverity::Error,
                        QString("Object count is %1; expected %2")
                            .arg(reparsed.objects().size()).arg(expectedCount),
                        objectPath);
        output->clear();
        return false;
    }
    if(report)
        report->add("mob.subtree.authorized", EValidationSeverity::Info,
                    duplicate
                        ? "Duplicated one exact provenance-checked raw object subtree"
                        : "Removed one exact provenance-checked raw object subtree",
                    objectPath);
    return true;
}

bool CMobDocument::duplicateObjectSubtree(const QString& objectPath,
                                          const QByteArray& expectedSubtreeSha256,
                                          QByteArray* output,
                                          CValidationReport* report) const
{
    return applyObjectSubtreeChange(*this, objectPath, expectedSubtreeSha256,
                                    true, output, report);
}

bool CMobDocument::removeObjectSubtree(const QString& objectPath,
                                       const QByteArray& expectedSubtreeSha256,
                                       QByteArray* output,
                                       CValidationReport* report) const
{
    return applyObjectSubtreeChange(*this, objectPath, expectedSubtreeSha256,
                                    false, output, report);
}

static void collectLeafSections(const QVector<SMobSection>& sections,
                                QMap<QString, const SMobSection*>* leaves)
{
    for(const SMobSection& section : sections)
    {
        if(section.container && !section.children.isEmpty())
            collectLeafSections(section.children, leaves);
        else
            leaves->insert(section.path, &section);
    }
}

static bool verifyAuthorizedTransactionOutput(const QVector<SMobSection>& sourceSections,
                                              const QVector<SMobSection>& candidateSections,
                                              const QMap<QString, SMobLeafEdit>& edits,
                                              CValidationReport* report)
{
    QMap<QString, const SMobSection*> sourceLeaves;
    QMap<QString, const SMobSection*> candidateLeaves;
    collectLeafSections(sourceSections, &sourceLeaves);
    collectLeafSections(candidateSections, &candidateLeaves);

    if(sourceLeaves.keys() != candidateLeaves.keys())
    {
        if(report)
            report->add("mob.transaction.structure", EValidationSeverity::Error,
                        "The candidate leaf structure differs from the source document");
        return false;
    }

    for(auto it = sourceLeaves.constBegin(); it != sourceLeaves.constEnd(); ++it)
    {
        const SMobSection& source = *it.value();
        const SMobSection& candidate = *candidateLeaves.value(it.key());
        const auto edit = edits.constFind(it.key());
        if(edit == edits.constEnd())
        {
            if(source.rawBytes != candidate.rawBytes)
            {
                if(report)
                    report->add("mob.transaction.unauthorized_change", EValidationSeverity::Error,
                                "A leaf outside the accepted transaction changed", it.key());
                return false;
            }
            continue;
        }

        if(candidate.type != source.type || candidate.rawBytes.mid(8) != edit->replacementPayload)
        {
            if(report)
                report->add("mob.transaction.payload_mismatch", EValidationSeverity::Error,
                            "The candidate does not contain the authorized replacement payload", it.key());
            return false;
        }
    }
    return true;
}

bool CMobDocument::deriveLeafTransaction(const CMobDocument& cleanProjection,
                                         const CMobDocument& editedProjection,
                                         CMobEditTransaction* transaction,
                                         CValidationReport* report) const
{
    if(!transaction || !transaction->isEmpty())
    {
        if(report)
            report->add("mob.projection_transaction.output", EValidationSeverity::Error,
                        "Projection transaction output must exist and start empty");
        return false;
    }
    if(m_sections.isEmpty() || cleanProjection.m_sections.isEmpty() ||
       editedProjection.m_sections.isEmpty())
    {
        if(report)
            report->add("mob.projection_transaction.document", EValidationSeverity::Error,
                        "Source, clean projection, and edited projection must all be parsed");
        return false;
    }

    QMap<QString, const SMobSection*> sourceLeaves;
    QMap<QString, const SMobSection*> cleanLeaves;
    QMap<QString, const SMobSection*> editedLeaves;
    collectLeafSections(m_sections, &sourceLeaves);
    collectLeafSections(cleanProjection.m_sections, &cleanLeaves);
    collectLeafSections(editedProjection.m_sections, &editedLeaves);

    if(cleanLeaves.keys() != editedLeaves.keys())
    {
        if(report)
            report->add("mob.projection_transaction.structure", EValidationSeverity::Error,
                        "The UI edit changed record topology; leaf-only shadow migration is not applicable");
        return false;
    }

    QVector<SMobLeafEdit> derivedEdits;
    for(auto it = cleanLeaves.constBegin(); it != cleanLeaves.constEnd(); ++it)
    {
        const SMobSection& clean = *it.value();
        const SMobSection& edited = *editedLeaves.value(it.key());
        if(clean.type != edited.type)
        {
            if(report)
                report->add("mob.projection_transaction.type", EValidationSeverity::Error,
                            "A projection leaf changed its numeric section type", it.key());
            return false;
        }
        if(clean.rawBytes == edited.rawBytes)
            continue;

        const auto sourceIt = sourceLeaves.constFind(it.key());
        if(sourceIt == sourceLeaves.constEnd() || (*sourceIt)->type != clean.type ||
           (*sourceIt)->rawBytes != clean.rawBytes)
        {
            if(report)
                report->add("mob.projection_transaction.provenance", EValidationSeverity::Error,
                            "A changed projection leaf does not byte-match its clean source record",
                            it.key());
            return false;
        }
        derivedEdits.append(SMobLeafEdit(it.key(), (*sourceIt)->sha256,
                                         edited.rawBytes.mid(8), edited.type));
    }

    for(const SMobLeafEdit& edit : derivedEdits)
        transaction->replaceLeafPayload(edit.sectionPath, edit.expectedSectionSha256,
                                        edit.replacementPayload, edit.sectionType);

    if(report)
        report->add("mob.projection_transaction.derived", EValidationSeverity::Info,
                    QString("Derived %1 source-hash-bound leaf edit(s); structural edits remain blocked")
                        .arg(derivedEdits.size()), transaction->description());
    return true;
}

bool CMobDocument::buildProjectionTransactionCandidate(
    const QByteArray& sourceBytes, const QString& sourceName,
    const QByteArray& cleanProjectionBytes, const QByteArray& editedProjectionBytes,
    CMobEditTransaction* transaction, QByteArray* candidate,
    CValidationReport* report)
{
    if(!transaction || !candidate)
    {
        if(report)
            report->add("mob.projection_save.output", EValidationSeverity::Error,
                        "Transaction and candidate outputs are required");
        return false;
    }
    *transaction = CMobEditTransaction("dirty UI projection transaction save");
    candidate->clear();

    CMobDocument sourceDocument;
    CMobDocument cleanProjectionDocument;
    CMobDocument editedProjectionDocument;
    CMobEditTransaction derived("dirty UI projection transaction save");
    QByteArray derivedCandidate;
    if(!sourceDocument.loadBytes(sourceBytes, sourceName, report) ||
       !cleanProjectionDocument.loadBytes(cleanProjectionBytes,
                                          "clean-ui-projection.mob", report) ||
       !editedProjectionDocument.loadBytes(editedProjectionBytes,
                                           "edited-ui-projection.mob", report) ||
       !sourceDocument.deriveLeafTransaction(cleanProjectionDocument,
                                             editedProjectionDocument,
                                             &derived, report))
        return false;
    if(derived.isEmpty())
    {
        if(report)
            report->add("mob.projection_save.no_changes", EValidationSeverity::Error,
                        "The dirty projection did not produce an authorized leaf edit");
        return false;
    }
    if(!sourceDocument.applyTransaction(derived, &derivedCandidate, report))
        return false;

    *transaction = derived;
    *candidate = derivedCandidate;
    return true;
}

bool CMobDocument::encodeCp1251Strict(const QString& value, QByteArray* output, QString* error)
{
    if(!output)
        return false;
    QTextCodec* codec = QTextCodec::codecForName("Windows-1251");
    if(!codec)
    {
        if(error)
            *error = "Windows-1251 codec is unavailable";
        return false;
    }
    const QByteArray encoded = codec->fromUnicode(value);
    if(codec->toUnicode(encoded) != value)
    {
        if(error)
            *error = "Text contains characters that cannot round-trip through Windows-1251";
        return false;
    }
    *output = encoded;
    return true;
}

bool CMobDocument::load(const QFileInfo& file, CValidationReport* report)
{
    QByteArray bytes;
    if(!readWholeFile(file, bytes, report))
        return false;
    return loadBytes(bytes, file.filePath(), report);
}

bool CMobDocument::loadBytes(const QByteArray& bytes, const QString& sourceName,
                             CValidationReport* report)
{
    m_originalBytes.clear();
    m_sections.clear();
    m_objects.clear();
    m_duplicateIds.clear();
    m_originalBytes = bytes;
    const QFileInfo sourceInfo(sourceName);
    const QString sourcePath = sourceName.isEmpty() ? QString("memory.mob") : sourceName;

    if(m_originalBytes.size() < 8)
    {
        if(report)
            report->add("mob.envelope", EValidationSeverity::Error,
                        "MOB is too small to contain a valid root section", sourcePath);
        return false;
    }

    MobSectionParser parser(m_originalBytes, report, sourcePath);
    if(!parser.parseRange(0, quint64(m_originalBytes.size()), QString(), m_sections, 0))
        return false;
    if(m_sections.isEmpty() || m_sections.front().type != 0xA000)
    {
        if(report)
            report->add("mob.root", EValidationSeverity::Error,
                        "The first top-level section is not OBJECT_DB_FILE",
                        sourcePath);
        return false;
    }
    for(int index = 1; index < m_sections.size(); ++index)
    {
        if(m_sections.at(index).type != 0x31415926)
        {
            if(report)
                report->add("mob.trailing_section", EValidationSeverity::Error,
                            QString("Unexpected top-level section %1 after OBJECT_DB_FILE")
                                .arg(sectionTypeName(m_sections.at(index).type)),
                            sourcePath);
            return false;
        }
    }

    collectObjects(m_sections, m_objects);
    QMap<quint32, int> counts;
    for(const SMobObjectSnapshot& object : m_objects)
        if(object.hasNid)
            counts[object.nid] += 1;
    for(auto countIt = counts.constBegin(); countIt != counts.constEnd(); ++countIt)
    {
        if(countIt.value() > 1)
        {
            for(int n = 1; n < countIt.value(); ++n)
                m_duplicateIds.append(countIt.key());
            if(report)
                report->add("mob.duplicate_id", EValidationSeverity::Warning,
                            QString("ID %1 occurs %2 times; it was preserved").arg(countIt.key()).arg(countIt.value()),
                            sourceInfo.fileName());
        }
    }
    if(report)
    {
        report->add("mob.byte_coverage", EValidationSeverity::Info,
                    QString("Strict section parsing covered all %1 bytes and found %2 objects")
                        .arg(m_originalBytes.size()).arg(m_objects.size()), sourceInfo.fileName());
        report->add("mob.lossless", EValidationSeverity::Info,
                    "Unchanged serialization is byte-identical", sourceInfo.fileName());
    }
    return true;
}

bool CMprDocument::load(const QFileInfo& file, CValidationReport* report)
{
    m_originalBytes.clear();
    if(!readWholeFile(file, m_originalBytes, report))
        return false;
    if(m_originalBytes.size() < 16)
    {
        if(report)
            report->add("mpr.envelope", EValidationSeverity::Error, "MPR is smaller than a RES header", file.filePath());
        return false;
    }
    QDataStream stream(m_originalBytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 signature = 0;
    quint32 tableSize = 0;
    quint32 tableOffset = 0;
    quint32 namesLength = 0;
    stream >> signature >> tableSize >> tableOffset >> namesLength;
    if(signature != 0x019CE23C)
    {
        if(report)
            report->add("mpr.signature", EValidationSeverity::Error, "Invalid MPR/RES signature", file.filePath());
        return false;
    }
    const quint64 entrySize = 22;
    const quint64 tableBytes = quint64(tableSize) * entrySize;
    const quint64 namesOffset = quint64(tableOffset) + tableBytes;
    if(tableOffset < 16 || namesOffset > quint64(m_originalBytes.size()) ||
       namesOffset + namesLength > quint64(m_originalBytes.size()))
    {
        if(report)
            report->add("mpr.table", EValidationSeverity::Error,
                        "The RES table or filename buffer extends outside the MPR file",
                        file.filePath());
        return false;
    }

    if(!stream.device()->seek(tableOffset))
    {
        if(report)
            report->add("mpr.table_seek", EValidationSeverity::Error,
                        "Could not seek to the MPR resource table", file.filePath());
        return false;
    }
    for(quint32 index = 0; index < tableSize; ++index)
    {
        quint32 nextIndex = 0;
        quint32 dataSize = 0;
        quint32 dataOffset = 0;
        quint32 lastWriteTime = 0;
        quint16 nameLength = 0;
        quint32 nameOffset = 0;
        stream >> nextIndex >> dataSize >> dataOffset >> lastWriteTime >> nameLength >> nameOffset;
        Q_UNUSED(lastWriteTime);
        const bool nextIndexValid = nextIndex == quint32(-1) || nextIndex < tableSize;
        const bool dataValid = dataOffset >= 16 &&
            quint64(dataOffset) + dataSize <= quint64(tableOffset);
        const bool nameValid = quint64(nameOffset) + nameLength <= namesLength;
        if(stream.status() != QDataStream::Ok || !nextIndexValid || !dataValid || !nameValid)
        {
            if(report)
                report->add("mpr.entry", EValidationSeverity::Error,
                            QString("Invalid MPR resource-table entry %1").arg(index),
                            file.filePath());
            return false;
        }
    }
    if(report)
        report->add("mpr.lossless", EValidationSeverity::Info, "Unchanged serialization is byte-identical", file.fileName());
    return true;
}

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

#include <algorithm>
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

constexpr quint32 MobScriptSection = 0xACCEECCB;
constexpr quint32 MobOldScriptSection = 0xACCEECCA;

void collectDocumentLeaves(const QVector<SMobSection>& sections,
                           QMap<QString, const SMobSection*>* leaves,
                           bool insideObjectSection = false)
{
    for(const SMobSection& section : sections)
    {
        const bool objectSection = insideObjectSection || section.type == 0xB000;
        if(objectSection)
            continue;
        if(section.container && !section.children.isEmpty())
            collectDocumentLeaves(section.children, leaves, false);
        else
            leaves->insert(section.path, &section);
    }
}

QString decodeEncryptedScript(const SMobSection* section)
{
    if(!section)
        return QString();
    const QByteArray data = payload(*section);
    if(data.size() < 4)
        return QString();
    quint32 key = readU32(data, 0);
    QByteArray decrypted;
    decrypted.reserve(data.size() - 4);
    for(int i = 4; i < data.size(); ++i)
    {
        quint32 temporaryKey = ((((key * 13) << 4) + key) << 8) - key;
        key += (temporaryKey << 2) + 2531011;
        temporaryKey = key >> 16;
        decrypted.append(char(data.at(i) ^ char(temporaryKey)));
    }
    return decodeCp1251(decrypted);
}

QString readableLineDiff(const QString& baseline, const QString& candidate)
{
    const QStringList before = baseline.split('\n');
    const QStringList after = candidate.split('\n');
    const qint64 cells = qint64(before.size() + 1) * qint64(after.size() + 1);
    if(cells > 4000000)
    {
        int prefix = 0;
        while(prefix < before.size() && prefix < after.size() && before.at(prefix) == after.at(prefix))
            ++prefix;
        int suffix = 0;
        while(suffix < before.size() - prefix && suffix < after.size() - prefix &&
              before.at(before.size() - 1 - suffix) == after.at(after.size() - 1 - suffix))
            ++suffix;
        const int removed = before.size() - prefix - suffix;
        const int added = after.size() - prefix - suffix;
        QString output;
        QTextStream stream(&output);
        stream << "--- baseline script\n+++ candidate script\n"
               << "Large script: " << before.size() << " -> " << after.size()
               << " lines; changed block starts at line " << (prefix + 1) << "\n";
        constexpr int MaximumLines = 400;
        int emitted = 0;
        for(int index = 0; index < removed && emitted < MaximumLines; ++index, ++emitted)
            stream << "-" << (prefix + index + 1) << ": " << before.at(prefix + index) << "\n";
        for(int index = 0; index < added && emitted < MaximumLines; ++index, ++emitted)
            stream << "+" << (prefix + index + 1) << ": " << after.at(prefix + index) << "\n";
        if(removed + added > MaximumLines)
            stream << "... diff truncated after " << MaximumLines << " changed lines\n";
        return output;
    }

    const int columns = after.size() + 1;
    QVector<int> lcs((before.size() + 1) * columns, 0);
    for(int i = before.size() - 1; i >= 0; --i)
        for(int j = after.size() - 1; j >= 0; --j)
            lcs[i * columns + j] = before.at(i) == after.at(j)
                ? 1 + lcs[(i + 1) * columns + j + 1]
                : qMax(lcs[(i + 1) * columns + j], lcs[i * columns + j + 1]);

    QString output;
    QTextStream stream(&output);
    stream << "--- baseline script\n+++ candidate script\n";
    int i = 0;
    int j = 0;
    int emitted = 0;
    constexpr int MaximumLines = 400;
    while((i < before.size() || j < after.size()) && emitted < MaximumLines)
    {
        if(i < before.size() && j < after.size() && before.at(i) == after.at(j))
        {
            ++i;
            ++j;
            continue;
        }
        if(j < after.size() && (i == before.size() ||
           lcs[i * columns + j + 1] >= lcs[(i + 1) * columns + j]))
        {
            stream << "+" << (j + 1) << ": " << after.at(j) << "\n";
            ++j;
        }
        else if(i < before.size())
        {
            stream << "-" << (i + 1) << ": " << before.at(i) << "\n";
            ++i;
        }
        ++emitted;
    }
    if(i < before.size() || j < after.size())
        stream << "... diff truncated after " << MaximumLines << " changed lines\n";
    return output;
}

QString documentCategory(quint32 type)
{
    if(type == MobScriptSection)
        return "script";
    if(type == MobOldScriptSection)
        return "old-script";
    return "document-section";
}

void compareDocumentLeaves(const CMobDocument& baseline, const CMobDocument& candidate,
                           QVector<SMobDocumentDiffEntry>* entries)
{
    QMap<QString, const SMobSection*> before;
    QMap<QString, const SMobSection*> after;
    collectDocumentLeaves(baseline.sections(), &before);
    collectDocumentLeaves(candidate.sections(), &after);
    QSet<QString> paths;
    for(auto it = before.constBegin(); it != before.constEnd(); ++it)
        paths.insert(it.key());
    for(auto it = after.constBegin(); it != after.constEnd(); ++it)
        paths.insert(it.key());
    QStringList orderedPaths = paths.values();
    std::sort(orderedPaths.begin(), orderedPaths.end());
    for(const QString& path : orderedPaths)
    {
        const SMobSection* left = before.value(path, nullptr);
        const SMobSection* right = after.value(path, nullptr);
        if(left && right && left->sha256 == right->sha256)
            continue;
        SMobDocumentDiffEntry entry;
        entry.kind = !left ? "added" : (!right ? "removed" : "modified");
        entry.sectionPath = path;
        entry.sectionType = left ? left->type : right->type;
        entry.category = documentCategory(entry.sectionType);
        if(left)
            entry.baselineSha256 = left->sha256;
        if(right)
            entry.candidateSha256 = right->sha256;
        if(entry.sectionType == MobScriptSection)
            entry.details = readableLineDiff(decodeEncryptedScript(left), decodeEncryptedScript(right));
        else if(entry.sectionType == MobOldScriptSection)
            entry.details = readableLineDiff(left ? decodeCp1251(payload(*left)) : QString(),
                                             right ? decodeCp1251(payload(*right)) : QString());
        else
            entry.details = QString("Raw section changed (%1 -> %2 bytes).")
                .arg(left ? left->rawBytes.size() : 0).arg(right ? right->rawBytes.size() : 0);
        entries->append(entry);
    }
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

bool emitObjectSequence(const SMobSection& section,
                        const QVector<const SMobSection*>& objects,
                        bool* applied, QByteArray* output,
                        CValidationReport* report)
{
    if(section.type == 0xB000)
    {
        if(*applied)
        {
            if(report)
                report->add("mob.object_sequence.multiple_sections",
                            EValidationSeverity::Error,
                            "The MOB contains more than one OBJECT_SECTION",
                            section.path);
            return false;
        }
        *applied = true;
        QByteArray childBytes;
        // Every direct OBJECT_SECTION child is one object record. Different
        // profiles use more than one numeric root type, so do not assume B001.
        for(const SMobSection* object : objects)
            childBytes.append(object->rawBytes);
        const quint64 newLength = quint64(childBytes.size()) + 8;
        if(newLength > quint64(UINT_MAX))
        {
            if(report)
                report->add("mob.object_sequence.overflow", EValidationSeverity::Error,
                            "Rebuilt OBJECT_SECTION exceeds the MOB length limit",
                            section.path);
            return false;
        }
        appendU32(*output, section.type);
        appendU32(*output, quint32(newLength));
        output->append(childBytes);
        return true;
    }

    if(!section.container || section.children.isEmpty())
    {
        output->append(section.rawBytes);
        return true;
    }
    QByteArray childBytes;
    for(const SMobSection& child : section.children)
        if(!emitObjectSequence(child, objects, applied, &childBytes, report))
            return false;
    const quint64 newLength = quint64(childBytes.size()) + 8;
    if(newLength > quint64(UINT_MAX))
        return false;
    appendU32(*output, section.type);
    appendU32(*output, quint32(newLength));
    output->append(childBytes);
    return true;
}

const SMobSection* childByTypeOccurrence(const SMobSection* parent,
                                         quint32 type, int occurrence)
{
    if(!parent)
        return nullptr;
    int found = 0;
    for(const SMobSection& child : parent->children)
        if(child.type == type)
        {
            if(found == occurrence)
                return &child;
            ++found;
        }
    return nullptr;
}

QVector<const SMobSection*> childrenOfType(const SMobSection& parent, quint32 type)
{
    QVector<const SMobSection*> result;
    for(const SMobSection& child : parent.children)
        if(child.type == type)
            result.append(&child);
    return result;
}

QVector<int> exactSequenceMatches(const QVector<const SMobSection*>& clean,
                                  const QVector<const SMobSection*>& edited)
{
    const int rows = clean.size() + 1;
    const int columns = edited.size() + 1;
    QVector<int> lengths(rows * columns, 0);
    for(int i = clean.size() - 1; i >= 0; --i)
        for(int j = edited.size() - 1; j >= 0; --j)
            lengths[i * columns + j] = clean.at(i)->rawBytes == edited.at(j)->rawBytes
                ? 1 + lengths[(i + 1) * columns + j + 1]
                : qMax(lengths[(i + 1) * columns + j],
                       lengths[i * columns + j + 1]);
    QVector<int> editedToClean(edited.size(), -1);
    int i = 0;
    int j = 0;
    while(i < clean.size() && j < edited.size())
    {
        if(clean.at(i)->rawBytes == edited.at(j)->rawBytes)
        {
            editedToClean[j++] = i++;
        }
        else if(lengths[(i + 1) * columns + j] >=
                lengths[i * columns + j + 1])
            ++i;
        else
            ++j;
    }
    QSet<int> usedClean;
    for(int match : editedToClean)
        if(match >= 0)
            usedClean.insert(match);

    // A patrol container changes byte hash when a look point is added or
    // removed. Pair such records by their direct position leaf before treating
    // them as delete+add, so source-only fields remain attached to the point.
    const auto identityLeaf = [](const SMobSection* section) -> QByteArray {
        if(!section)
            return QByteArray();
        const quint32 identityType = section->type == 0xBBBD0000
            ? 0xBBBD0001 : (section->type == 0xBBBE0000 ? 0xBBBE0001 : 0);
        if(identityType == 0)
            return QByteArray();
        for(const SMobSection& child : section->children)
            if(child.type == identityType)
                return child.rawBytes;
        return QByteArray();
    };
    for(int editedIndex = 0; editedIndex < edited.size(); ++editedIndex)
    {
        if(editedToClean.at(editedIndex) >= 0)
            continue;
        const QByteArray identity = identityLeaf(edited.at(editedIndex));
        if(identity.isEmpty())
            continue;
        int uniqueMatch = -1;
        for(int cleanIndex = 0; cleanIndex < clean.size(); ++cleanIndex)
            if(!usedClean.contains(cleanIndex) &&
               identityLeaf(clean.at(cleanIndex)) == identity)
            {
                if(uniqueMatch >= 0)
                {
                    uniqueMatch = -1;
                    break;
                }
                uniqueMatch = cleanIndex;
            }
        if(uniqueMatch >= 0)
        {
            editedToClean[editedIndex] = uniqueMatch;
            usedClean.insert(uniqueMatch);
        }
    }
    if(clean.size() == edited.size())
    {
        QVector<int> remainingClean;
        QVector<int> remainingEdited;
        for(int cleanIndex = 0; cleanIndex < clean.size(); ++cleanIndex)
            if(!usedClean.contains(cleanIndex))
                remainingClean.append(cleanIndex);
        for(int editedIndex = 0; editedIndex < edited.size(); ++editedIndex)
            if(editedToClean.at(editedIndex) < 0)
                remainingEdited.append(editedIndex);
        if(remainingClean.size() == remainingEdited.size())
            for(int index = 0; index < remainingClean.size(); ++index)
                editedToClean[remainingEdited.at(index)] = remainingClean.at(index);
    }
    return editedToClean;
}

bool knownLogicPoint(const SMobSection& section)
{
    if(section.type == 0xBBBD0000)
    {
        for(const SMobSection& child : section.children)
            if(child.type != 0xBBBD0001 && child.type != 0xBBBE0000)
                return false;
        return true;
    }
    if(section.type == 0xBBBE0000)
    {
        for(const SMobSection& child : section.children)
            if(child.type < 0xBBBE0001 || child.type > 0xBBBE0004)
                return false;
        return true;
    }
    return false;
}

bool emitLogicStructure(const SMobSection& source,
                        const SMobSection* clean,
                        const SMobSection* edited,
                        QByteArray* output,
                        CValidationReport* report)
{
    if(!source.container || source.children.isEmpty() || !clean || !edited ||
       source.type != clean->type || clean->type != edited->type)
    {
        output->append(source.rawBytes);
        return true;
    }

    quint32 sequenceType = 0;
    if(source.type == 0xBBBC0000)
        sequenceType = 0xBBBD0000;
    else if(source.type == 0xBBBD0000)
        sequenceType = 0xBBBE0000;

    QByteArray childBytes;
    if(sequenceType != 0)
    {
        const QVector<const SMobSection*> sourceSequence = childrenOfType(source, sequenceType);
        const QVector<const SMobSection*> cleanSequence = childrenOfType(*clean, sequenceType);
        const QVector<const SMobSection*> editedSequence = childrenOfType(*edited, sequenceType);
        if(sourceSequence.size() != cleanSequence.size())
        {
            if(report)
                report->add("mob.logic_structure.provenance", EValidationSeverity::Error,
                            "Raw and clean logic-point counts differ", source.path);
            return false;
        }
        const QVector<int> matches = exactSequenceMatches(cleanSequence, editedSequence);
        bool inserted = false;
        for(const SMobSection& child : source.children)
        {
            if(child.type == sequenceType)
            {
                if(inserted)
                    continue;
                inserted = true;
                for(int editedIndex = 0; editedIndex < editedSequence.size(); ++editedIndex)
                {
                    const int cleanIndex = matches.at(editedIndex);
                    if(cleanIndex >= 0)
                    {
                        if(!emitLogicStructure(*sourceSequence.at(cleanIndex),
                                               cleanSequence.at(cleanIndex),
                                               editedSequence.at(editedIndex),
                                               &childBytes, report))
                            return false;
                    }
                    else
                    {
                        if(!knownLogicPoint(*editedSequence.at(editedIndex)))
                        {
                            if(report)
                                report->add("mob.logic_structure.unknown_addition",
                                            EValidationSeverity::Error,
                                            "A new patrol/look point contains an unknown field",
                                            editedSequence.at(editedIndex)->path);
                            return false;
                        }
                        childBytes.append(editedSequence.at(editedIndex)->rawBytes);
                    }
                }
                continue;
            }
            const SMobSection* cleanChild = childByTypeOccurrence(
                clean, child.type, child.occurrence);
            const SMobSection* editedChild = childByTypeOccurrence(
                edited, child.type, child.occurrence);
            if(!emitLogicStructure(child, cleanChild, editedChild, &childBytes, report))
                return false;
        }
        if(!inserted)
            for(const SMobSection* added : editedSequence)
            {
                if(!knownLogicPoint(*added))
                    return false;
                childBytes.append(added->rawBytes);
            }
    }
    else
    {
        for(const SMobSection& child : source.children)
        {
            const SMobSection* cleanChild = childByTypeOccurrence(
                clean, child.type, child.occurrence);
            const SMobSection* editedChild = childByTypeOccurrence(
                edited, child.type, child.occurrence);
            if(!emitLogicStructure(child, cleanChild, editedChild, &childBytes, report))
                return false;
        }
    }
    const quint64 newLength = quint64(childBytes.size()) + 8;
    if(newLength > quint64(UINT_MAX))
        return false;
    appendU32(*output, source.type);
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
                                             quint32 sectionType,
                                             const QByteArray& sourcePayload)
{
    m_edits.append(SMobLeafEdit(sectionPath, expectedSectionSha256,
                                replacementPayload, sectionType, sourcePayload));
}

bool CMobEditTransaction::validatedChangedSaveClass(QString* className) const
{
    if(className)
        className->clear();
    if(m_edits.isEmpty())
        return false;
    for(const SMobLeafEdit& edit : m_edits)
    {
        // The authorization boundary is structural, not a growing whitelist of
        // UI fields. deriveLeafTransaction() supplies these values only after
        // proving that the clean projection still matches the original raw
        // section at this exact path. applyTransaction() then rechecks the hash,
        // reparses the result, and verifies every untouched section byte-for-byte.
        if(edit.sectionPath.isEmpty() || edit.sectionType == 0 ||
           edit.expectedSectionSha256.size() != 32 ||
           edit.sourcePayload == edit.replacementPayload)
            return false;
    }
    if(className)
        *className = "RAW_LEAF_EDITS";
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

QJsonObject SMobDocumentDiffEntry::toJson() const
{
    QJsonObject result;
    result.insert("kind", kind);
    result.insert("category", category);
    result.insert("path", sectionPath);
    result.insert("sectionType", QString("0x%1").arg(sectionType, 8, 16, QLatin1Char('0')).toUpper());
    if(!baselineSha256.isEmpty())
        result.insert("baselineSha256", QString::fromLatin1(baselineSha256.toHex()));
    if(!candidateSha256.isEmpty())
        result.insert("candidateSha256", QString::fromLatin1(candidateSha256.toHex()));
    result.insert("details", details);
    return result;
}

CMobSemanticDiff CMobSemanticDiff::compare(const CMobDocument& baseline,
                                           const CMobDocument& candidate)
{
    CMobSemanticDiff result;
    compareDocumentLeaves(baseline, candidate, &result.m_documentEntries);
    result.m_baseline = baseline.objects();
    result.m_candidate = candidate.objects();
    QSet<int> matchedBaseline;
    QSet<int> matchedCandidate;

    QMultiMap<QByteArray, int> baselineHashes;
    QMultiMap<QByteArray, int> candidateHashes;
    for(int i = 0; i < result.m_baseline.size(); ++i)
        baselineHashes.insert(result.m_baseline.at(i).subtreeSha256, i);
    for(int i = 0; i < result.m_candidate.size(); ++i)
        candidateHashes.insert(result.m_candidate.at(i).subtreeSha256, i);
    for(auto it = baselineHashes.constBegin(); it != baselineHashes.constEnd(); )
    {
        const QByteArray hash = it.key();
        QList<int> baselineMatches = baselineHashes.values(hash);
        QList<int> candidateMatches = candidateHashes.values(hash);
        std::sort(baselineMatches.begin(), baselineMatches.end());
        std::sort(candidateMatches.begin(), candidateMatches.end());
        if(!baselineMatches.isEmpty() && baselineMatches.size() == candidateMatches.size())
        {
            for(int pairIndex = 0; pairIndex < baselineMatches.size(); ++pairIndex)
            {
                const int baselineIndex = baselineMatches.at(pairIndex);
                const int candidateIndex = candidateMatches.at(pairIndex);
                result.m_entries.append({EMobDiffKind::Unchanged, baselineIndex,
                                         candidateIndex, "exact-subtree-hash", 1.0});
                matchedBaseline.insert(baselineIndex);
                matchedCandidate.insert(candidateIndex);
            }
        }
        it = baselineHashes.upperBound(hash);
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
    QJsonArray documentEntries;
    QMap<QString, int> counts;
    for(const SMobDiffEntry& entry : m_entries)
    {
        entries.append(entry.toJson(m_baseline, m_candidate));
        counts[diffKindName(entry.kind)] += 1;
    }
    for(const SMobDocumentDiffEntry& entry : m_documentEntries)
    {
        documentEntries.append(entry.toJson());
        counts[entry.kind] += 1;
    }
    QJsonObject summary;
    for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        summary.insert(it.key(), it.value());
    QJsonObject result;
    result.insert("summary", summary);
    result.insert("entries", entries);
    result.insert("documentEntries", documentEntries);
    return result;
}

QString CMobSemanticDiff::toText() const
{
    QString output;
    QTextStream stream(&output);
    QMap<QString, int> counts;
    for(const SMobDiffEntry& entry : m_entries)
        counts[diffKindName(entry.kind)] += 1;
    for(const SMobDocumentDiffEntry& entry : m_documentEntries)
        counts[entry.kind] += 1;
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
    for(const SMobDocumentDiffEntry& entry : m_documentEntries)
    {
        stream << entry.kind.toUpper() << " " << entry.category
               << " section=" << entry.sectionPath << "\n";
        stream << entry.details;
        if(!entry.details.endsWith('\n'))
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

bool CMobDocument::replaceEmbeddedScript(const QString& script, QByteArray* output,
                                         CValidationReport* report) const
{
    if(!output)
        return false;
    output->clear();

    QMap<QString, const SMobSection*> leaves;
    collectLeafSections(m_sections, &leaves);
    const SMobSection* scriptSection = nullptr;
    for(auto it = leaves.constBegin(); it != leaves.constEnd(); ++it)
    {
        if(it.value()->type != MobScriptSection)
            continue;
        if(scriptSection)
        {
            if(report)
                report->add("mob.script.multiple", EValidationSeverity::Error,
                            "The MOB contains more than one encrypted script section; the launch bootstrap was not injected");
            return false;
        }
        scriptSection = it.value();
    }
    if(!scriptSection)
    {
        if(report)
            report->add("mob.script.missing", EValidationSeverity::Error,
                        "The MOB has no existing encrypted script leaf that can be replaced losslessly");
        return false;
    }

    const QByteArray originalPayload = payload(*scriptSection);
    if(originalPayload.size() < 4)
    {
        if(report)
            report->add("mob.script.payload", EValidationSeverity::Error,
                        "The encrypted script payload is shorter than its key",
                        scriptSection->path);
        return false;
    }
    QByteArray encoded;
    QString encodingError;
    if(!encodeCp1251Strict(script, &encoded, &encodingError))
    {
        if(report)
            report->add("mob.script.encoding", EValidationSeverity::Error,
                        encodingError, scriptSection->path);
        return false;
    }

    quint32 key = readU32(originalPayload, 0);
    QByteArray replacement;
    appendU32(replacement, key);
    replacement.reserve(4 + encoded.size());
    for(char character : encoded)
    {
        quint32 temporaryKey = ((((key * 13) << 4) + key) << 8) - key;
        key += (temporaryKey << 2) + 2531011;
        temporaryKey = key >> 16;
        replacement.append(char(character ^ char(temporaryKey)));
    }

    CMobEditTransaction transaction("Replace embedded script");
    transaction.replaceLeafPayload(scriptSection->path, scriptSection->sha256,
                                   replacement, scriptSection->type,
                                   originalPayload);
    return applyTransaction(transaction, output, report);
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

bool CMobDocument::rebuildObjectSequence(
    const QStringList& objectPaths,
    const QVector<QByteArray>& expectedSubtreeSha256,
    QByteArray* output, CValidationReport* report) const
{
    if(!output)
        return false;
    output->clear();
    if(objectPaths.size() != expectedSubtreeSha256.size())
    {
        if(report)
            report->add("mob.object_sequence.input", EValidationSeverity::Error,
                        "Object path and provenance hash counts differ");
        return false;
    }
    QVector<const SMobSection*> requested;
    requested.reserve(objectPaths.size());
    for(int index = 0; index < objectPaths.size(); ++index)
    {
        const SMobSection* object = findObjectChild(m_sections, objectPaths.at(index));
        if(!object || expectedSubtreeSha256.at(index).size() != 32 ||
           object->sha256 != expectedSubtreeSha256.at(index))
        {
            if(report)
                report->add("mob.object_sequence.provenance", EValidationSeverity::Error,
                            "An object source path or subtree hash is stale",
                            objectPaths.at(index));
            return false;
        }
        requested.append(object);
    }

    bool applied = false;
    for(const SMobSection& section : m_sections)
        if(!emitObjectSequence(section, requested, &applied, output, report))
        {
            output->clear();
            return false;
        }
    if(!applied)
    {
        if(report)
            report->add("mob.object_sequence.missing_section", EValidationSeverity::Error,
                        "The MOB has no OBJECT_SECTION to rebuild");
        output->clear();
        return false;
    }

    CMobDocument reparsed;
    if(!reparsed.loadBytes(*output, "object-sequence-output.mob", report) ||
       reparsed.objects().size() != requested.size())
    {
        if(report && !report->hasErrors())
            report->add("mob.object_sequence.count", EValidationSeverity::Error,
                        "Rebuilt object count does not match the requested sequence");
        output->clear();
        return false;
    }
    for(int index = 0; index < requested.size(); ++index)
        if(reparsed.objects().at(index).subtreeSha256 != requested.at(index)->sha256)
        {
            if(report)
                report->add("mob.object_sequence.bytes", EValidationSeverity::Error,
                            "A rebuilt object subtree was not emitted byte-for-byte",
                            objectPaths.at(index));
            output->clear();
            return false;
        }
    if(report)
        report->add("mob.object_sequence.authorized", EValidationSeverity::Info,
                    QString("Rebuilt %1 object entries from exact source subtrees")
                        .arg(requested.size()));
    return true;
}

bool CMobDocument::reconcileLogicPointStructure(
    const CMobDocument& cleanProjection,
    const CMobDocument& editedProjection,
    QByteArray* output, CValidationReport* report) const
{
    if(!output)
        return false;
    output->clear();
    if(m_sections.size() != cleanProjection.sections().size() ||
       cleanProjection.sections().size() != editedProjection.sections().size())
        return false;
    for(int index = 0; index < m_sections.size(); ++index)
        if(!emitLogicStructure(m_sections.at(index),
                               &cleanProjection.sections().at(index),
                               &editedProjection.sections().at(index),
                               output, report))
        {
            output->clear();
            return false;
        }
    CMobDocument reparsed;
    if(!reparsed.loadBytes(*output, "logic-structure-output.mob", report))
    {
        output->clear();
        return false;
    }
    return true;
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
                                         edited.rawBytes.mid(8), edited.type,
                                         clean.rawBytes.mid(8)));
    }

    for(const SMobLeafEdit& edit : derivedEdits)
        transaction->replaceLeafPayload(edit.sectionPath, edit.expectedSectionSha256,
                                        edit.replacementPayload, edit.sectionType,
                                        edit.sourcePayload);

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

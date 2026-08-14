#include "profile_database_provider_support.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {

struct SArchiveEntry { quint32 size{0}; quint32 offset{0}; quint16 nameLength{0}; quint32 nameOffset{0}; };

bool ignoredArchive(const QString& name)
{
    const QString lower = name.toLower();
    return lower.contains("backup") || lower.contains("original")
        || lower.contains("unmodified") || lower.contains("old");
}

QString sha256(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()).toUpper();
}

QString caseInsensitiveChild(const QString& directory, const QString& name,
                             QDir::Filters filters)
{
    const QDir parent(directory);
    for(const QFileInfo& entry : parent.entryInfoList(filters | QDir::Readable,
                                                       QDir::Name))
        if(entry.fileName().compare(name, Qt::CaseInsensitive) == 0)
            return entry.absoluteFilePath();
    return QString();
}

QString jmvTablesLuaPath(const QStringList& resourceDirectories)
{
    if(resourceDirectories.isEmpty())
        return QString();
    QDir modRoot(resourceDirectories.first());
    if(!modRoot.cdUp())
        return QString();
    const QString luaDirectory = caseInsensitiveChild(
        modRoot.absolutePath(), "lua", QDir::Dirs | QDir::NoDotAndDotDot);
    return luaDirectory.isEmpty() ? QString() : caseInsensitiveChild(
        luaDirectory, "tables.lua", QDir::Files);
}

QString stripLuaComments(const QString& source, bool* terminated)
{
    QString output;
    output.reserve(source.size());
    QChar quote;
    bool escaped = false;
    bool lineComment = false;
    bool blockComment = false;
    for(int index = 0; index < source.size(); ++index)
    {
        const QChar ch = source.at(index);
        const QChar next = index + 1 < source.size() ? source.at(index + 1) : QChar();
        const QChar after = index + 2 < source.size() ? source.at(index + 2) : QChar();
        const QChar fourth = index + 3 < source.size() ? source.at(index + 3) : QChar();
        if(lineComment)
        {
            if(ch == '\n' || ch == '\r')
            {
                lineComment = false;
                output.append(ch);
            }
            else
                output.append(' ');
            continue;
        }
        if(blockComment)
        {
            if(ch == ']' && next == ']')
            {
                output.append("  ");
                ++index;
                blockComment = false;
            }
            else
                output.append(ch == '\n' || ch == '\r' ? ch : QChar(' '));
            continue;
        }
        if(quote.isNull() && ch == '-' && next == '-')
        {
            if(after == '[' && fourth == '[')
            {
                output.append("    ");
                index += 3;
                blockComment = true;
            }
            else
            {
                output.append("  ");
                ++index;
                lineComment = true;
            }
            continue;
        }
        output.append(ch);
        if(!quote.isNull() && ch == '\\' && !escaped)
        {
            escaped = true;
            continue;
        }
        if((ch == '\"' || ch == '\'') && !escaped)
        {
            if(quote.isNull()) quote = ch;
            else if(quote == ch) quote = QChar();
        }
        escaped = false;
    }
    if(terminated)
        *terminated = quote.isNull() && !blockComment;
    return output;
}

int matchingBrace(const QString& text, int opening)
{
    int depth = 0;
    QChar quote;
    bool escaped = false;
    for(int index = opening; index < text.size(); ++index)
    {
        const QChar ch = text.at(index);
        if(!quote.isNull() && ch == '\\' && !escaped)
        {
            escaped = true;
            continue;
        }
        if((ch == '\"' || ch == '\'') && !escaped)
        {
            if(quote.isNull()) quote = ch;
            else if(quote == ch) quote = QChar();
        }
        escaped = false;
        if(!quote.isNull())
            continue;
        if(ch == '{') ++depth;
        else if(ch == '}' && --depth == 0) return index;
    }
    return -1;
}

QStringList splitLuaFields(const QString& row)
{
    QStringList result;
    QChar quote;
    bool escaped = false;
    int start = 0;
    for(int index = 0; index < row.size(); ++index)
    {
        const QChar ch = row.at(index);
        if(!quote.isNull() && ch == '\\' && !escaped)
        {
            escaped = true;
            continue;
        }
        if((ch == '\"' || ch == '\'') && !escaped)
        {
            if(quote.isNull()) quote = ch;
            else if(quote == ch) quote = QChar();
        }
        escaped = false;
        if(quote.isNull() && ch == ',')
        {
            result.append(row.mid(start, index - start).trimmed());
            start = index + 1;
        }
    }
    result.append(row.mid(start).trimmed());
    while(!result.isEmpty() && result.last().isEmpty())
        result.removeLast();
    return result;
}

bool parseLuaString(const QString& token, QString* value)
{
    if(token.size() < 2 || (token.front() != '\"' && token.front() != '\'')
       || token.back() != token.front())
        return false;
    QString decoded = token.mid(1, token.size() - 2);
    decoded.replace("\\\"", "\"");
    decoded.replace("\\'", "'");
    decoded.replace("\\\\", "\\");
    if(value) *value = decoded;
    return true;
}

bool parseJmvUnitPerks(const QByteArray& bytes,
                       QMap<QString, ei::SJmvUnitPerkRecord>* records,
                       QMap<QString, int>* strongMan, QString* diagnostic)
{
    if(bytes.isEmpty() || bytes.size() > 4 * 1024 * 1024)
    {
        if(diagnostic) *diagnostic = "tables.lua is empty or exceeds the 4 MiB safety limit.";
        return false;
    }
    bool commentsTerminated = false;
    QString text = stripLuaComments(QString::fromUtf8(bytes), &commentsTerminated);
    if(!commentsTerminated)
    {
        if(diagnostic) *diagnostic = "tables.lua contains an unterminated string or block comment.";
        return false;
    }
    const QRegularExpression assignment(
        "\\bUnitPerk\\s*=\\s*\\{", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = assignment.match(text);
    if(!match.hasMatch())
    {
        if(diagnostic) *diagnostic = "tables.lua has no bounded UnitPerk table assignment.";
        return false;
    }
    const int opening = match.capturedEnd() - 1;
    const int closing = matchingBrace(text, opening);
    if(closing < 0)
    {
        if(diagnostic) *diagnostic = "The UnitPerk table has unbalanced braces.";
        return false;
    }
    QMap<QString, int> parsed;
    QMap<QString, ei::SJmvUnitPerkRecord> parsedRecords;
    int cursor = opening + 1;
    while(cursor < closing)
    {
        while(cursor < closing && (text.at(cursor).isSpace() || text.at(cursor) == ','))
            ++cursor;
        if(cursor >= closing)
            break;
        if(text.at(cursor) != '{')
        {
            if(diagnostic) *diagnostic = "UnitPerk contains a non-row expression.";
            return false;
        }
        const int rowClosing = matchingBrace(text, cursor);
        if(rowClosing < 0 || rowClosing > closing)
        {
            if(diagnostic) *diagnostic = "A UnitPerk row has unbalanced braces.";
            return false;
        }
        const QStringList fields = splitLuaFields(text.mid(
            cursor + 1, rowClosing - cursor - 1));
        if(fields.size() < 44)
        {
            if(diagnostic) *diagnostic = QString(
                "A UnitPerk row has %1 fields; at least 44 are required through StrongMan/DexMan/WiseMan.")
                .arg(fields.size());
            return false;
        }
        QString prototype;
        if(!parseLuaString(fields.first(), &prototype) || prototype.trimmed().isEmpty())
        {
            if(diagnostic) *diagnostic = "A UnitPerk row has no bounded string prototype name.";
            return false;
        }
        static const QRegularExpression integer("^[+-]?\\d+$");
        for(int field = 1; field < fields.size(); ++field)
            if(!integer.match(fields.at(field)).hasMatch())
            {
                if(diagnostic) *diagnostic = QString(
                    "UnitPerk row '%1' contains a non-integer field at Lua index %2.")
                    .arg(prototype).arg(field + 1);
                return false;
            }
        bool ok = false;
        const int strong = fields.at(41).toInt(&ok); // UnitPerkType.StrongMan = 42 (Lua, 1-based).
        const QString key = prototype.trimmed().toLower();
        ei::SJmvUnitPerkRecord parsedRecord;
        parsedRecord.prototype = prototype.trimmed();
        for(int field = 1; field < fields.size(); ++field)
            parsedRecord.values.append(fields.at(field).toInt());
        if(!ok)
        {
            if(diagnostic) *diagnostic =
                QString("UnitPerk row '%1' has an invalid StrongMan value.").arg(prototype);
            return false;
        }
        parsedRecord.physicalOccurrences = parsedRecords.value(key).physicalOccurrences +
            (parsedRecords.contains(key) ? 1 : 0);
        parsed.insert(key, strong);
        parsedRecords.insert(key, parsedRecord);
        if(parsed.size() > 100000)
        {
            if(diagnostic) *diagnostic = "UnitPerk exceeds the 100000-row safety limit.";
            return false;
        }
        cursor = rowClosing + 1;
    }
    if(parsed.isEmpty())
    {
        if(diagnostic) *diagnostic = "The UnitPerk table contains no rows.";
        return false;
    }
    if(records) *records = parsedRecords;
    if(strongMan) *strongMan = parsed;
    return true;
}

bool parseJmvUnitStats(const QByteArray& bytes,
                       QMap<QString, ei::SJmvUnitStatRecord>* records,
                       bool* found, QString* diagnostic)
{
    if(found) *found = false;
    if(bytes.isEmpty() || bytes.size() > 4 * 1024 * 1024)
    {
        if(diagnostic) *diagnostic = "tables.lua is empty or exceeds the 4 MiB safety limit.";
        return false;
    }
    bool commentsTerminated = false;
    const QString text = stripLuaComments(QString::fromUtf8(bytes), &commentsTerminated);
    if(!commentsTerminated)
    {
        if(diagnostic) *diagnostic = "tables.lua contains an unterminated string or block comment.";
        return false;
    }
    const QRegularExpression assignment(
        "\\bUnitStat\\s*=\\s*\\{", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = assignment.match(text);
    if(!match.hasMatch())
    {
        if(records) records->clear();
        return true; // Older/synthetic JMV tables.lua files may legitimately omit UnitStat.
    }
    if(found) *found = true;
    const int opening = match.capturedEnd() - 1;
    const int closing = matchingBrace(text, opening);
    if(closing < 0)
    {
        if(diagnostic) *diagnostic = "The UnitStat table has unbalanced braces.";
        return false;
    }
    static const QRegularExpression number(
        "^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?$");
    QMap<QString, ei::SJmvUnitStatRecord> parsed;
    int cursor = opening + 1;
    int physicalRows = 0;
    while(cursor < closing)
    {
        while(cursor < closing && (text.at(cursor).isSpace() || text.at(cursor) == ',')) ++cursor;
        if(cursor >= closing) break;
        if(text.at(cursor) != '{')
        {
            if(diagnostic) *diagnostic = "UnitStat contains a non-row expression.";
            return false;
        }
        const int rowClosing = matchingBrace(text, cursor);
        if(rowClosing < 0 || rowClosing > closing)
        {
            if(diagnostic) *diagnostic = "A UnitStat row has unbalanced braces.";
            return false;
        }
        const QStringList fields = splitLuaFields(text.mid(cursor + 1, rowClosing - cursor - 1));
        QString prototype;
        if(fields.size() < 2 || !parseLuaString(fields.first(), &prototype)
           || prototype.trimmed().isEmpty())
        {
            if(diagnostic) *diagnostic = "A UnitStat row needs a bounded prototype name and at least one numeric value.";
            return false;
        }
        ei::SJmvUnitStatRecord record;
        record.prototype = prototype.trimmed();
        for(int field = 1; field < fields.size(); ++field)
        {
            bool ok = false;
            const double value = fields.at(field).toDouble(&ok);
            if(!ok || !number.match(fields.at(field)).hasMatch())
            {
                if(diagnostic) *diagnostic = QString(
                    "UnitStat row '%1' contains a non-numeric field at Lua index %2.")
                    .arg(prototype).arg(field + 1);
                return false;
            }
            record.values.append(value);
        }
        const QString key = record.prototype.toLower();
        record.physicalOccurrences = parsed.contains(key)
            ? parsed.value(key).physicalOccurrences + 1 : 1;
        parsed.insert(key, record); // JMV lookup semantics: the last physical row wins.
        if(++physicalRows > 100000)
        {
            if(diagnostic) *diagnostic = "UnitStat exceeds the 100000-row safety limit.";
            return false;
        }
        cursor = rowClosing + 1;
    }
    if(records) *records = parsed;
    return true;
}

QString luaString(const QString& value)
{
    QString escaped = value;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    escaped.replace('\r', "\\r");
    escaped.replace('\n', "\\n");
    escaped.replace('\t', "\\t");
    return '"' + escaped + '"';
}

QString unitPerkRow(const ei::SJmvUnitPerkRecord& record)
{
    QStringList fields;
    fields.append(luaString(record.prototype));
    for(int value : record.values)
        fields.append(QString::number(value));
    return '{' + fields.join(',') + ",}";
}

QString unitStatRow(const ei::SJmvUnitStatRecord& record)
{
    QStringList fields;
    fields.append(luaString(record.prototype));
    for(double value : record.values)
        fields.append(QString::number(value, 'g', 15));
    return '{' + fields.join(',') + ",}";
}

}

namespace ei {

QString activeDatabaseArchive(const QStringList& resourceDirectories, bool firstDirectoryOnly)
{
    const int count = firstDirectoryOnly ? qMin(1, resourceDirectories.size()) : resourceDirectories.size();
    for(int index = 0; index < count; ++index)
    {
        const QDir directory(resourceDirectories.at(index));
        for(const QFileInfo& file : directory.entryInfoList(QDir::Files, QDir::Name))
            if(file.fileName().compare("database.res", Qt::CaseInsensitive) == 0
                    && !ignoredArchive(file.fileName()))
                return file.absoluteFilePath();
    }
    return QString();
}

QString siblingArchive(const QString& databasePath, const QString& fileName)
{
    const QDir directory(QFileInfo(databasePath).absolutePath());
    for(const QFileInfo& file : directory.entryInfoList(QDir::Files, QDir::Name))
        if(file.fileName().compare(fileName, Qt::CaseInsensitive) == 0 && !ignoredArchive(file.fileName()))
            return file.absoluteFilePath();
    return QString();
}

QMap<QString, QByteArray> readDatabaseArchive(const QString& path,
                                              QVector<SDatabaseDiagnostic>* diagnostics)
{
    QMap<QString, QByteArray> result;
    QFile file(path);
    if(path.isEmpty() || !file.open(QIODevice::ReadOnly))
    {
        diagnostics->append({EDatabaseDiagnosticSeverity::Error, "database.archive.missing",
            "Required active database archive could not be opened.", path});
        return result;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 signature = 0, tableSize = 0, tableOffset = 0, namesLength = 0;
    stream >> signature >> tableSize >> tableOffset >> namesLength;
    if(signature != 0x019CE23C || tableSize > 100000 || tableOffset > quint64(file.size()))
    {
        diagnostics->append({EDatabaseDiagnosticSeverity::Error, "database.archive.invalid",
            "Database archive header is invalid or exceeds safety bounds.", path});
        return result;
    }
    if(!file.seek(tableOffset))
        return result;
    QVector<SArchiveEntry> entries;
    entries.reserve(int(tableSize));
    for(quint32 index = 0; index < tableSize; ++index)
    {
        quint32 next = 0, modified = 0;
        SArchiveEntry entry;
        stream >> next >> entry.size >> entry.offset >> modified >> entry.nameLength >> entry.nameOffset;
        entries.append(entry);
    }
    if(namesLength > quint64(file.size() - file.pos()))
    {
        diagnostics->append({EDatabaseDiagnosticSeverity::Error, "database.archive.names.invalid",
            "Database archive name table exceeds the file boundary.", path});
        return result;
    }
    const QByteArray names = file.read(namesLength);
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(const SArchiveEntry& entry : entries)
    {
        if(entry.nameOffset + entry.nameLength > quint32(names.size())
                || quint64(entry.offset) + entry.size > quint64(file.size()))
            continue;
        const QString name = codec->toUnicode(names.mid(int(entry.nameOffset), entry.nameLength));
        if(!file.seek(entry.offset))
            continue;
        const QByteArray bytes = file.read(entry.size);
        if(bytes.size() == int(entry.size))
            result.insert(name.toLower(), bytes);
    }
    return result;
}

SDatabaseProvenance databaseProvenance(const SProfileDatabaseSnapshot& snapshot,
                                       const QString& path, const QString& member,
                                       bool overlay)
{
    SDatabaseProvenance value;
    value.providerId = snapshot.providerId;
    value.profileId = snapshot.profileId;
    value.runtimeManifestId = snapshot.runtimeManifestId;
    value.sourcePath = path;
    value.memberName = member;
    value.overlay = overlay;
    QFile file(path);
    if(file.open(QIODevice::ReadOnly))
        value.sha256 = sha256(file.readAll());
    return value;
}

void applyRelationalMembers(const QMap<QString, QByteArray>& members, const QString& sourcePath,
                            SProfileDatabaseSnapshot* snapshot)
{
    const SParsedDatabaseMembers parsed = parseProfileDatabaseMembers(members, sourcePath);
    snapshot->monsters = parsed.monsters;
    snapshot->raceModels = parsed.raceModels;
    snapshot->items = parsed.items;
    snapshot->materialRecords = parsed.materialRecords;
    snapshot->materials = parsed.namesByCategory.value("materials");
    snapshot->weapons = parsed.namesByCategory.value("weapons");
    snapshot->armor = parsed.namesByCategory.value("armor");
    snapshot->quickItems = parsed.namesByCategory.value("quickItems");
    snapshot->questItems = parsed.namesByCategory.value("questItems");
    snapshot->lootItems = parsed.namesByCategory.value("lootItems");
    snapshot->spells = parsed.namesByCategory.value("spells");
    snapshot->perks = parsed.namesByCategory.value("perks");
    snapshot->diagnostics += parsed.diagnostics;
    for(const QString& member : {QString("units.udb"), QString("items.idb"),
                                 QString("spells.sdb"), QString("perks.pdb")})
        if(members.contains(member))
            snapshot->provenance.append(databaseProvenance(*snapshot, sourcePath, member, false));
}

void applyActionMembers(const QMap<QString, QByteArray>& members, const QString& sourcePath,
                        bool overlay, SProfileDatabaseSnapshot* snapshot)
{
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        if(!it.key().endsWith(".adb", Qt::CaseInsensitive))
            continue;
        const QString key = it.key().toLower();
        snapshot->actionTables.insert(key, it.value());
        const SDatabaseProvenance provenance = databaseProvenance(*snapshot, sourcePath, key, overlay);
        snapshot->actionTableProvenance.insert(key, provenance);
        snapshot->provenance.append(provenance);
    }
}

void applyJmvUnitPerkTable(const QStringList& resourceDirectories,
                           SProfileDatabaseSnapshot* snapshot)
{
    if(!snapshot)
        return;
    snapshot->jmvStrongManByPrototype.clear();
    snapshot->jmvUnitPerksByPrototype.clear();
    snapshot->jmvUnitStatsByPrototype.clear();
    snapshot->jmvUnitPerkSourcePath = jmvTablesLuaPath(resourceDirectories);
    snapshot->jmvUnitPerkSha256.clear();
    snapshot->jmvUnitPerkDiagnostic.clear();
    if(snapshot->jmvUnitPerkSourcePath.isEmpty())
    {
        snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Missing;
        snapshot->jmvUnitPerkDiagnostic =
            "The active JMV mod has no readable lua/tables.lua next to its Res directory.";
        return;
    }
    QFile file(snapshot->jmvUnitPerkSourcePath);
    if(!file.open(QIODevice::ReadOnly))
    {
        snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Missing;
        snapshot->jmvUnitPerkDiagnostic = "The active JMV lua/tables.lua could not be opened.";
        return;
    }
    const QByteArray bytes = file.read(4 * 1024 * 1024 + 1);
    snapshot->jmvUnitPerkSha256 = sha256(bytes);
    if(!parseJmvUnitPerks(bytes, &snapshot->jmvUnitPerksByPrototype,
                         &snapshot->jmvStrongManByPrototype,
                         &snapshot->jmvUnitPerkDiagnostic))
    {
        snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Malformed;
        snapshot->jmvUnitPerksByPrototype.clear();
        snapshot->jmvStrongManByPrototype.clear();
        return;
    }
    bool unitStatFound = false;
    if(!parseJmvUnitStats(bytes, &snapshot->jmvUnitStatsByPrototype,
                         &unitStatFound, &snapshot->jmvUnitPerkDiagnostic))
    {
        snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Malformed;
        snapshot->jmvUnitPerksByPrototype.clear();
        snapshot->jmvUnitStatsByPrototype.clear();
        snapshot->jmvStrongManByPrototype.clear();
        return;
    }
    snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Valid;
}

QByteArray rewriteJmvUnitPerkTable(
    const QByteArray& source,
    const QMap<QString, SJmvUnitPerkRecord>& desiredRecords,
    QString* diagnostic)
{
    QMap<QString, SJmvUnitPerkRecord> parsed;
    if(!parseJmvUnitPerks(source, &parsed, nullptr, diagnostic))
        return {};
    for(auto it = desiredRecords.constBegin(); it != desiredRecords.constEnd(); ++it)
        if(it.value().prototype.trimmed().isEmpty() || it.value().values.size() < 43)
        {
            if(diagnostic) *diagnostic = QString(
                "JMV UnitPerk row '%1' is incomplete; 43 numeric fields are required.")
                .arg(it.key());
            return {};
        }

    bool terminated = false;
    const QByteArray utf8Bom("\xEF\xBB\xBF",3);
    const bool hasBom=source.startsWith(utf8Bom);
    QString text = QString::fromUtf8(hasBom?source.mid(3):source);
    const QString stripped = stripLuaComments(text, &terminated);
    const QRegularExpression assignment(
        "\\bUnitPerk\\s*=\\s*\\{", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = assignment.match(stripped);
    if(!terminated || !match.hasMatch())
    {
        if(diagnostic) *diagnostic = "tables.lua has no safely editable UnitPerk table.";
        return {};
    }
    const int opening = match.capturedEnd() - 1;
    const int closing = matchingBrace(stripped, opening);
    if(closing < 0)
    {
        if(diagnostic) *diagnostic = "The UnitPerk table has unbalanced braces.";
        return {};
    }

    struct Replacement { int start; int length; QString value; };
    QVector<Replacement> replacements;
    QSet<QString> retained;
    QMap<QString,int> remainingOccurrences;
    int countCursor=opening+1;
    while(countCursor<closing)
    {
        while(countCursor<closing&&(stripped.at(countCursor).isSpace()||stripped.at(countCursor)==','))++countCursor;
        if(countCursor>=closing)break;const int end=matchingBrace(stripped,countCursor);if(end<0||end>closing)break;const QStringList fields=splitLuaFields(stripped.mid(countCursor+1,end-countCursor-1));QString prototype;if(!fields.isEmpty()&&parseLuaString(fields.first(),&prototype))++remainingOccurrences[prototype.trimmed().toLower()];countCursor=end+1;
    }
    int cursor = opening + 1;
    while(cursor < closing)
    {
        while(cursor < closing && (stripped.at(cursor).isSpace() || stripped.at(cursor) == ','))
            ++cursor;
        if(cursor >= closing)
            break;
        const int rowStart = cursor;
        const int rowClosing = matchingBrace(stripped, rowStart);
        if(rowClosing < 0 || rowClosing > closing)
        {
            if(diagnostic) *diagnostic = "A UnitPerk row has unbalanced braces.";
            return {};
        }
        const QStringList fields = splitLuaFields(stripped.mid(
            rowStart + 1, rowClosing - rowStart - 1));
        QString prototype;
        if(fields.isEmpty() || !parseLuaString(fields.first(), &prototype))
        {
            if(diagnostic) *diagnostic = "A UnitPerk row has no bounded string prototype name.";
            return {};
        }
        const QString key = prototype.trimmed().toLower();
        SJmvUnitPerkRecord current;current.prototype=prototype.trimmed();for(int field=1;field<fields.size();++field)current.values.append(fields.at(field).toInt());current.physicalOccurrences=remainingOccurrences.value(key);
        const bool lastOccurrence=--remainingOccurrences[key]==0;
        int after = rowClosing + 1;
        while(after < closing && stripped.at(after).isSpace()) ++after;
        if(after < closing && stripped.at(after) == ',') ++after;
        const auto desired = desiredRecords.constFind(key);
        if(desired == desiredRecords.constEnd())
            replacements.append({rowStart, after - rowStart, QString()});
        else
        {
            retained.insert(key);
            if(lastOccurrence&&(desired.value().prototype != current.prototype
               || desired.value().values != current.values))
                replacements.append({rowStart, rowClosing - rowStart + 1,
                                     unitPerkRow(desired.value())});
        }
        cursor = after;
    }

    QStringList additions;
    for(auto it = desiredRecords.constBegin(); it != desiredRecords.constEnd(); ++it)
        if(!retained.contains(it.key()))
            for(int occurrence=0;occurrence<qMax(1,it->physicalOccurrences);++occurrence)
                additions.append(unitPerkRow(it.value()) + ',');
    if(!additions.isEmpty())
    {
        const QString newline = text.contains("\r\n") ? "\r\n" : "\n";
        replacements.append({closing, 0, newline + additions.join(newline) + newline});
    }
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement& left, const Replacement& right)
              { return left.start > right.start; });
    for(const Replacement& replacement : replacements)
        text.replace(replacement.start, replacement.length, replacement.value);
    const QByteArray result = (hasBom?utf8Bom:QByteArray())+text.toUtf8();
    QMap<QString, SJmvUnitPerkRecord> verified;
    if(!parseJmvUnitPerks(result, &verified, nullptr, diagnostic)
       || verified.size() != desiredRecords.size())
    {
        if(diagnostic && diagnostic->isEmpty())
            *diagnostic = "Rewritten tables.lua did not reproduce the requested UnitPerk rows.";
        return {};
    }
    return result;
}

QByteArray rewriteJmvUnitStatTable(
    const QByteArray& source,
    const QMap<QString, SJmvUnitStatRecord>& desiredRecords,
    QString* diagnostic)
{
    QMap<QString, SJmvUnitStatRecord> parsed;
    bool found = false;
    if(!parseJmvUnitStats(source, &parsed, &found, diagnostic)) return {};
    if(!found)
    {
        if(desiredRecords.isEmpty()) return source;
        if(diagnostic) *diagnostic = "tables.lua has no safely editable UnitStat table.";
        return {};
    }
    for(auto it = desiredRecords.constBegin(); it != desiredRecords.constEnd(); ++it)
        if(it.value().prototype.trimmed().isEmpty() || it.value().values.isEmpty())
        {
            if(diagnostic) *diagnostic = QString("JMV UnitStat row '%1' is incomplete.").arg(it.key());
            return {};
        }

    const QByteArray utf8Bom("\xEF\xBB\xBF", 3);
    const bool hasBom = source.startsWith(utf8Bom);
    QString text = QString::fromUtf8(hasBom ? source.mid(3) : source);
    bool terminated = false;
    const QString stripped = stripLuaComments(text, &terminated);
    const QRegularExpression assignment(
        "\\bUnitStat\\s*=\\s*\\{", QRegularExpression::CaseInsensitiveOption);
    const auto match = assignment.match(stripped);
    if(!terminated || !match.hasMatch())
    {
        if(diagnostic) *diagnostic = "tables.lua has no safely editable UnitStat table.";
        return {};
    }
    const int opening = match.capturedEnd() - 1;
    const int closing = matchingBrace(stripped, opening);
    if(closing < 0)
    {
        if(diagnostic) *diagnostic = "The UnitStat table has unbalanced braces.";
        return {};
    }
    struct Replacement { int start; int length; QString value; };
    QVector<Replacement> replacements;
    QSet<QString> retained;
    QMap<QString, int> remaining;
    int cursor = opening + 1;
    while(cursor < closing)
    {
        while(cursor < closing && (stripped.at(cursor).isSpace() || stripped.at(cursor) == ',')) ++cursor;
        if(cursor >= closing) break;
        const int end = matchingBrace(stripped, cursor);
        if(end < 0 || end > closing) return {};
        const QStringList fields = splitLuaFields(stripped.mid(cursor + 1, end - cursor - 1));
        QString prototype;
        if(!fields.isEmpty() && parseLuaString(fields.first(), &prototype))
            ++remaining[prototype.trimmed().toLower()];
        cursor = end + 1;
    }
    cursor = opening + 1;
    while(cursor < closing)
    {
        while(cursor < closing && (stripped.at(cursor).isSpace() || stripped.at(cursor) == ',')) ++cursor;
        if(cursor >= closing) break;
        const int rowStart = cursor;
        const int rowClosing = matchingBrace(stripped, rowStart);
        if(rowClosing < 0 || rowClosing > closing) return {};
        const QStringList fields = splitLuaFields(stripped.mid(rowStart + 1, rowClosing - rowStart - 1));
        QString prototype;
        if(fields.isEmpty() || !parseLuaString(fields.first(), &prototype)) return {};
        const QString key = prototype.trimmed().toLower();
        SJmvUnitStatRecord current;
        current.prototype = prototype.trimmed();
        for(int field = 1; field < fields.size(); ++field) current.values.append(fields.at(field).toDouble());
        const bool lastOccurrence = --remaining[key] == 0;
        int after = rowClosing + 1;
        while(after < closing && stripped.at(after).isSpace()) ++after;
        if(after < closing && stripped.at(after) == ',') ++after;
        const auto desired = desiredRecords.constFind(key);
        if(desired == desiredRecords.constEnd()) replacements.append({rowStart, after - rowStart, {}});
        else
        {
            retained.insert(key);
            if(lastOccurrence && (desired->prototype != current.prototype || desired->values != current.values))
                replacements.append({rowStart, rowClosing - rowStart + 1, unitStatRow(desired.value())});
        }
        cursor = after;
    }
    QStringList additions;
    for(auto it = desiredRecords.constBegin(); it != desiredRecords.constEnd(); ++it)
        if(!retained.contains(it.key()))
            for(int occurrence = 0; occurrence < qMax(1, it->physicalOccurrences); ++occurrence)
                additions.append(unitStatRow(it.value()) + ',');
    if(!additions.isEmpty())
    {
        const QString newline = text.contains("\r\n") ? "\r\n" : "\n";
        replacements.append({closing, 0, newline + additions.join(newline) + newline});
    }
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement& left, const Replacement& right){ return left.start > right.start; });
    for(const auto& replacement : replacements)
        text.replace(replacement.start, replacement.length, replacement.value);
    const QByteArray result = (hasBom ? utf8Bom : QByteArray()) + text.toUtf8();
    QMap<QString, SJmvUnitStatRecord> verified;
    bool verifiedFound = false;
    if(!parseJmvUnitStats(result, &verified, &verifiedFound, diagnostic)
       || !verifiedFound || verified.size() != desiredRecords.size())
    {
        if(diagnostic && diagnostic->isEmpty())
            *diagnostic = "Rewritten tables.lua did not reproduce the requested UnitStat rows.";
        return {};
    }
    return result;
}

}

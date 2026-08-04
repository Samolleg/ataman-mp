#include "profile_database_provider_support.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QRegularExpression>

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

bool parseJmvUnitPerks(const QByteArray& bytes, QMap<QString, int>* strongMan,
                       QString* diagnostic)
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
        if(!ok || (parsed.contains(key) && parsed.value(key) != strong))
        {
            if(diagnostic) *diagnostic = !ok
                ? QString("UnitPerk row '%1' has an invalid StrongMan value.").arg(prototype)
                : QString("UnitPerk contains conflicting StrongMan values for duplicate prototype '%1'.")
                    .arg(prototype);
            return false;
        }
        parsed.insert(key, strong);
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
    if(strongMan) *strongMan = parsed;
    return true;
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
    if(!parseJmvUnitPerks(bytes, &snapshot->jmvStrongManByPrototype,
                         &snapshot->jmvUnitPerkDiagnostic))
    {
        snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Malformed;
        snapshot->jmvStrongManByPrototype.clear();
        return;
    }
    snapshot->jmvUnitPerkTableStatus = EJmvUnitPerkTableStatus::Valid;
}

}

#include "profile_database_provider_support.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>

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

}

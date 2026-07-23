#include "profile_database.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextCodec>

#include "../runtime/providers/base_profile_database_provider.h"
#include "../runtime/providers/jmv_profile_database_provider.h"

namespace {

quint8 u8(const QByteArray& data, int offset) { return quint8(uchar(data.at(offset))); }

quint32 u32(const QByteArray& data, int offset)
{
    return quint32(u8(data, offset)) | (quint32(u8(data, offset + 1)) << 8)
        | (quint32(u8(data, offset + 2)) << 16) | (quint32(u8(data, offset + 3)) << 24);
}

struct STag { int id{-1}; int payload{-1}; int length{0}; int end{-1}; };

bool tagAt(const QByteArray& data, int offset, int limit, STag* tag)
{
    if(offset < 0 || offset + 2 > limit || limit > data.size())
        return false;
    const int id = u8(data, offset);
    quint32 encoded = u8(data, offset + 1);
    int header = 2;
    if(encoded & 1)
    {
        if(offset + 5 > limit)
            return false;
        encoded = u32(data, offset + 1) - 1;
        header = 5;
    }
    const quint32 length = encoded / 2;
    if(length > quint32(limit - offset - header))
        return false;
    tag->id = id;
    tag->payload = offset + header;
    tag->length = int(length);
    tag->end = tag->payload + tag->length;
    return true;
}

QString text(const QByteArray& bytes)
{
    QByteArray value = bytes;
    if(value.endsWith('\0'))
        value.chop(1);
    if(value.isEmpty() || value.contains('\0'))
        return QString();
    for(char c : value)
        if(uchar(c) < 0x20 && c != '\t')
            return QString();
    return QTextCodec::codecForName("CP1251")->toUnicode(value).trimmed();
}

QString fieldString(const QMap<int, QByteArray>& fields, int id)
{
    return text(fields.value(id));
}

int fieldInt(const QMap<int, QByteArray>& fields, int id, int fallback = -1)
{
    const QByteArray value = fields.value(id);
    return value.size() == 4 ? int(qint32(u32(value, 0))) : fallback;
}

float fieldFloat(const QMap<int, QByteArray>& fields, int id, float fallback = 1.0f)
{
    const QByteArray value = fields.value(id);
    if(value.size() != 4)
        return fallback;
    QDataStream stream(value);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    float result = fallback;
    stream >> result;
    return stream.status() == QDataStream::Ok ? result : fallback;
}

void nestedStrings(const QByteArray& data, int begin, int end, QStringList* result, int depth = 0)
{
    if(depth > 8 || begin >= end)
        return;
    // A leaf may begin with bytes that also happen to form a valid tag header
    // (notably "2h ...": 0x32, 0x68).  Prefer a bounded printable leaf before
    // attempting recursive tag decoding; container payloads contain tag header
    // control bytes and therefore fail text().
    const QString directLeaf = text(data.mid(begin, end - begin));
    if(!directLeaf.isEmpty())
    {
        result->append(directLeaf);
        return;
    }
    int cursor = begin;
    bool parsed = false;
    while(cursor < end)
    {
        STag tag;
        if(!tagAt(data, cursor, end, &tag))
            break;
        parsed = true;
        const QString leaf = text(data.mid(tag.payload, tag.length));
        if(!leaf.isEmpty())
            result->append(leaf);
        else
            nestedStrings(data, tag.payload, tag.end, result, depth + 1);
        cursor = tag.end;
    }
    if(!parsed)
    {
        const QString leaf = text(data.mid(begin, end - begin));
        if(!leaf.isEmpty())
            result->append(leaf);
    }
}

QMap<int, QByteArray> recordFields(const QByteArray& data, const STag& record,
                                   QVector<ei::SDatabaseDiagnostic>* diagnostics,
                                   const QString& source)
{
    QMap<int, QByteArray> fields;
    int cursor = record.payload;
    while(cursor < record.end)
    {
        STag field;
        if(!tagAt(data, cursor, record.end, &field))
        {
            diagnostics->append({ei::EDatabaseDiagnosticSeverity::Warning,
                "database.record.truncated", "A database record contains an invalid field boundary.", source});
            break;
        }
        fields.insert(field.id, data.mid(field.payload, field.length));
        cursor = field.end;
    }
    return fields;
}

QVector<QMap<int, QByteArray>> tableRecords(const QByteArray& data, int wantedTable,
                                            QVector<ei::SDatabaseDiagnostic>* diagnostics,
                                            const QString& source)
{
    QVector<QMap<int, QByteArray>> result;
    STag root;
    if(!tagAt(data, 0, data.size(), &root))
    {
        diagnostics->append({ei::EDatabaseDiagnosticSeverity::Error,
            "database.root.invalid", "Database member has an invalid root boundary.", source});
        return result;
    }
    int cursor = root.payload;
    int tableIndex = 0;
    while(cursor < root.end)
    {
        STag table;
        if(!tagAt(data, cursor, root.end, &table))
            break;
        if(tableIndex == wantedTable)
        {
            int recordCursor = table.payload;
            while(recordCursor < table.end)
            {
                STag record;
                if(!tagAt(data, recordCursor, table.end, &record))
                    break;
                result.append(recordFields(data, record, diagnostics, source));
                recordCursor = record.end;
            }
            break;
        }
        ++tableIndex;
        cursor = table.end;
    }
    return result;
}

QStringList stringListField(const QMap<int, QByteArray>& fields, int id)
{
    QStringList encoded;
    const QByteArray value = fields.value(id);
    nestedStrings(value, 0, value.size(), &encoded);
    QStringList result;
    for(const QString& entry : encoded)
    {
        int squareDepth = 0;
        int braceDepth = 0;
        int start = 0;
        for(int index = 0; index <= entry.size(); ++index)
        {
            const QChar c = index < entry.size() ? entry.at(index) : QChar(',');
            if(c == '[') ++squareDepth;
            else if(c == ']') squareDepth = qMax(0, squareDepth - 1);
            else if(c == '{') ++braceDepth;
            else if(c == '}') braceDepth = qMax(0, braceDepth - 1);
            if(c == ',' && squareDepth == 0 && braceDepth == 0)
            {
                const QString part = entry.mid(start, index - start).trimmed();
                if(!part.isEmpty()) result.append(part);
                start = index + 1;
            }
        }
    }
    result.removeDuplicates();
    return result;
}

void addNames(const QVector<QMap<int, QByteArray>>& records, const QString& category,
              ei::SParsedDatabaseMembers* output)
{
    QStringList& names = output->namesByCategory[category];
    for(const auto& fields : records)
    {
        const QString name = fieldString(fields, 0);
        if(!name.isEmpty())
            names.append(name);
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
}

}

namespace ei {

bool SProfileDatabaseSnapshot::usable() const
{
    if(monsters.isEmpty() || raceModels.isEmpty())
        return false;
    for(const auto& diagnostic : diagnostics)
        if(diagnostic.severity == EDatabaseDiagnosticSeverity::Error)
            return false;
    return true;
}

SParsedDatabaseMembers parseProfileDatabaseMembers(const QMap<QString, QByteArray>& members,
                                                    const QString& sourcePath)
{
    SParsedDatabaseMembers output;
    QMap<QString, QByteArray> normalized;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
        normalized.insert(it.key().toLower(), it.value());

    const QByteArray units = normalized.value("units.udb");
    if(!units.isEmpty())
    {
        for(const auto& fields : tableRecords(units, 1, &output.diagnostics, sourcePath + ":units.udb"))
        {
            SRaceModelDatabaseRecord record;
            record.name = fieldString(fields, 0);
            record.model = fieldString(fields, 30);
            record.primaryTextures = stringListField(fields, 31);
            record.secondaryTextures = stringListField(fields, 32);
            if(!record.name.isEmpty())
                output.raceModels.insert(record.name.toLower(), record);
        }
        for(const auto& fields : tableRecords(units, 2, &output.diagnostics, sourcePath + ":units.udb"))
        {
            SMonsterDatabaseRecord record;
            record.name = fieldString(fields, 0);
            record.baseRace = fieldString(fields, 1);
            record.skinIndex = fieldInt(fields, 3);
            record.hairIndex = fieldInt(fields, 4);
            record.complexionX = fieldFloat(fields, 5);
            record.complexionY = fieldFloat(fields, 6);
            record.complexionZ = fieldFloat(fields, 7);
            record.items = stringListField(fields, 29);
            record.wears = stringListField(fields, 32);
            const QStringList weapons = stringListField(fields, 33);
            record.weapon = weapons.value(0);
            record.secondWeapon = fieldString(fields, 42);
            if(!record.name.isEmpty())
                output.monsters.insert(record.name.toLower(), record);
        }
    }
    else
        output.diagnostics.append({EDatabaseDiagnosticSeverity::Error, "database.units.missing",
            "The selected active database has no units.udb.", sourcePath});

    const QByteArray items = normalized.value("items.idb");
    const QStringList categories = {"materials", "weapons", "armor", "quickItems", "questItems", "lootItems"};
    if(!items.isEmpty())
    {
        for(int table = 0; table < categories.size(); ++table)
        {
            const auto records = tableRecords(items, table, &output.diagnostics, sourcePath + ":items.idb");
            addNames(records, categories.at(table), &output);
            for(const auto& fields : records)
            {
                if(table == 0)
                {
                    SMaterialDatabaseRecord material;
                    material.name = fieldString(fields, 0);
                    material.type = fieldString(fields, 1);
                    material.code = fieldString(fields, 2);
                    material.id = fieldInt(fields, 3);
                    if(!material.name.isEmpty())
                        output.materialRecords.insert(material.name.toLower(), material);
                    continue;
                }
                SItemDatabaseRecord record;
                record.name = fieldString(fields, 0);
                record.category = categories.at(table);
                record.type = fieldString(fields, 1);
                record.typeId = fieldInt(fields, 2);
                record.materialType = fieldString(fields, 3);
                record.textureType1 = fieldInt(fields, 5);
                record.textureType2 = fieldInt(fields, 6);
                if(table == 2)
                    record.wearOrder = fieldInt(fields, 24);
                if(!record.name.isEmpty())
                    output.items.insert(record.name.toLower(), record);
            }
        }
    }
    else
        output.diagnostics.append({EDatabaseDiagnosticSeverity::Error, "database.items.missing",
            "The selected active database has no items.idb.", sourcePath});

    const QByteArray spells = normalized.value("spells.sdb");
    if(!spells.isEmpty())
        addNames(tableRecords(spells, 0, &output.diagnostics, sourcePath + ":spells.sdb"), "spells", &output);
    const QByteArray perks = normalized.value("perks.pdb");
    if(!perks.isEmpty())
        addNames(tableRecords(perks, 1, &output.diagnostics, sourcePath + ":perks.pdb"), "perks", &output);
    return output;
}

bool validateItemReferenceSyntax(const QString& value, QString* diagnostic)
{
    int depth = 0;
    for(const QChar c : value)
    {
        if(c == '[') ++depth;
        else if(c == ']')
        {
            if(depth == 0)
            {
                if(diagnostic) *diagnostic = "Closing modifier bracket has no matching opening bracket.";
                return false;
            }
            --depth;
        }
    }
    if(depth != 0)
    {
        if(diagnostic) *diagnostic = "Modifier brackets are not balanced.";
        return false;
    }
    const QString base = value.section('[', 0, 0);
    const QStringList segments = base.split('.');
    bool grammarValid = !value.isEmpty() && segments.size() <= 2;
    for(const QString& segment : segments)
        grammarValid = grammarValid && !segment.trimmed().isEmpty() && segment == segment.trimmed();
    if(!value.isEmpty() && !grammarValid)
    {
        if(diagnostic) *diagnostic = "Expected prototype[.material][modifier] without whitespace.";
        return false;
    }
    return true;
}

ProfileDatabaseService::ProfileDatabaseService()
{
    registerProvider(QSharedPointer<IProfileDatabaseProvider>(new BaseProfileDatabaseProvider));
    registerProvider(QSharedPointer<IProfileDatabaseProvider>(new JmvProfileDatabaseProvider));
}

bool ProfileDatabaseService::registerProvider(const QSharedPointer<IProfileDatabaseProvider>& provider,
                                              QString* diagnostic)
{
    if(provider.isNull() || provider->providerId().trimmed().isEmpty())
    {
        if(diagnostic) *diagnostic = "Database provider is null or has no ID.";
        return false;
    }
    const QString key = provider->providerId().trimmed().toLower();
    if(m_providers.contains(key))
    {
        if(diagnostic) *diagnostic = "Database provider is already registered: " + provider->providerId();
        return false;
    }
    m_providers.insert(key, provider);
    return true;
}

QSharedPointer<const SProfileDatabaseSnapshot> ProfileDatabaseService::snapshot(
    const SProfileDatabaseLoadRequest& request)
{
    const auto provider = m_providers.value(request.providerId.trimmed().toLower());
    if(provider.isNull() || provider->interfaceVersion() != request.providerInterfaceVersion)
    {
        QSharedPointer<SProfileDatabaseSnapshot> failed(new SProfileDatabaseSnapshot);
        failed->providerId = request.providerId;
        failed->profileId = request.profileId;
        failed->runtimeManifestId = request.runtimeManifestId;
        failed->diagnostics.append({EDatabaseDiagnosticSeverity::Error, "database.provider.unavailable",
            "Unknown or incompatible database provider; database-backed features are disabled.", QString()});
        return failed;
    }
    QString key = request.providerId.toLower() + '|' + QString::number(request.providerInterfaceVersion)
        + '|' + request.profileId + '|' + request.runtimeManifestId;
    for(const QString& directory : request.resourceDirectories)
    {
        const QFileInfo info(directory);
        key += '|' + info.absoluteFilePath().toLower() + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
        const QDir dir(directory);
        for(const QFileInfo& archive : dir.entryInfoList({"*.res"}, QDir::Files, QDir::Name))
        {
            const QString name = archive.fileName().toLower();
            if(name != "database.res" && name != "databasejmv.res")
                continue;
            QFile file(archive.absoluteFilePath());
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if(file.open(QIODevice::ReadOnly))
                hash.addData(&file);
            key += ':' + name + ':' + QString::fromLatin1(hash.result().toHex());
        }
    }
    const QString hashedKey = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    if(!m_cache.contains(hashedKey))
    {
        QSharedPointer<SProfileDatabaseSnapshot> loaded = provider->load(request);
        loaded->cacheKey = hashedKey;
        m_cache.insert(hashedKey, loaded);
    }
    return m_cache.value(hashedKey);
}

void ProfileDatabaseService::clear() { m_cache.clear(); }

ProfileDatabaseContext& ProfileDatabaseContext::instance()
{
    static ProfileDatabaseContext context;
    return context;
}

void ProfileDatabaseContext::activate(const QSharedPointer<const SProfileDatabaseSnapshot>& snapshot,
                                      const QMap<QString, QString>& providerBindings)
{
    QWriteLocker locker(&m_lock);
    m_current = snapshot;
    m_providerBindings = providerBindings;
    ++m_generation;
}

QString ProfileDatabaseContext::providerForRole(const QString& role) const
{
    QReadLocker locker(&m_lock);
    return m_providerBindings.value(role);
}

QSharedPointer<const SProfileDatabaseSnapshot> ProfileDatabaseContext::current() const
{
    QReadLocker locker(&m_lock);
    return m_current;
}

quint64 ProfileDatabaseContext::generation() const
{
    QReadLocker locker(&m_lock);
    return m_generation;
}

}

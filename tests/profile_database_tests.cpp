#include <QtTest>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "profile_database.h"

namespace {

void dword(QByteArray* output, quint32 value)
{
    output->append(char(value)); output->append(char(value >> 8));
    output->append(char(value >> 16)); output->append(char(value >> 24));
}

QByteArray tag(int id, const QByteArray& payload)
{
    QByteArray result;
    result.append(char(id));
    const quint32 encoded = quint32(payload.size()) * 2;
    if(encoded <= 254)
        result.append(char(encoded));
    else
        dword(&result, encoded + 1);
    result.append(payload);
    return result;
}

QByteArray stringField(int id, const QByteArray& value) { return tag(id, value + '\0'); }
QByteArray intField(int id, qint32 value) { QByteArray bytes; dword(&bytes, quint32(value)); return tag(id, bytes); }
QByteArray record(const QByteArray& fields) { return tag(1, fields); }

QByteArray database(const QVector<QByteArray>& tables)
{
    QByteArray payload;
    for(int index = 0; index < tables.size(); ++index)
        payload += tag(index, tables.at(index));
    return tag(0, payload);
}

QMap<QString, QByteArray> relationalMembers()
{
    QByteArray race = stringField(0, "Human Male") + stringField(30, "unhuma")
        + tag(31, stringField(0, "unhumaskin_00"));
    QByteArray monster = stringField(0, "Human Test") + stringField(1, "Human Male")
        + intField(3, 2) + tag(29, stringField(0, "gold [3]"))
        + tag(32, stringField(0, "armor_leather.iron"))
        + tag(33, stringField(0, "2h sword.iron[effect{a,b}],shield.iron"));
    const QByteArray units = database({QByteArray(), record(race), record(monster), QByteArray()});

    QVector<QByteArray> itemTables;
    const QStringList names = {"iron", "2h sword", "armor_leather", "potion", "quest_key", "gold"};
    for(int index = 0; index < names.size(); ++index)
    {
        QByteArray fields = stringField(0, names.at(index).toLatin1());
        if(index == 0)
            fields += stringField(1, "Metal") + stringField(2, "ir") + intField(3, 7);
        else
            fields += stringField(1, index == 1 ? "weapon" : "item") + intField(2, index)
                + stringField(3, "metal") + intField(5, 1);
        itemTables.append(record(fields));
    }
    const QByteArray items = database(itemTables);
    const QByteArray spells = database({record(stringField(0, "fireball"))});
    const QByteArray perks = database({QByteArray(), record(stringField(0, "two_handed"))});
    return {{"units.udb", units}, {"items.idb", items}, {"spells.sdb", spells}, {"perks.pdb", perks}};
}

QByteArray resArchive(const QMap<QString, QByteArray>& members)
{
    QByteArray data;
    QByteArray names;
    struct Entry { quint32 size; quint32 offset; quint16 nameLength; quint32 nameOffset; };
    QVector<Entry> entries;
    quint32 offset = 16;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        const QByteArray name = it.key().toLatin1();
        entries.append({quint32(it.value().size()), offset, quint16(name.size()), quint32(names.size())});
        data += it.value();
        names += name;
        offset += it.value().size();
    }
    QByteArray output;
    QDataStream stream(&output, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0x019CE23C) << quint32(entries.size()) << quint32(16 + data.size())
           << quint32(names.size());
    output += data;
    QDataStream table(&output, QIODevice::Append);
    table.setByteOrder(QDataStream::LittleEndian);
    for(const Entry& entry : entries)
        table << quint32(-1) << entry.size << entry.offset << quint32(0)
              << entry.nameLength << entry.nameOffset;
    output += names;
    return output;
}

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
}

}

class ProfileDatabaseTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesStableLinksAndCurrentNames();
    void validatesBalancedItemGrammarWithoutMutation();
    void providersAreStrictAndJmvOverlaysOnlyActions();
    void unknownProviderFailsClosed();
};

void ProfileDatabaseTests::parsesStableLinksAndCurrentNames()
{
    const ei::SParsedDatabaseMembers parsed = ei::parseProfileDatabaseMembers(relationalMembers(), "memory");
    QCOMPARE(parsed.monsters.value("human test").baseRace, QString("Human Male"));
    QCOMPARE(parsed.monsters.value("human test").weapon, QString("2h sword.iron[effect{a,b}]"));
    QCOMPARE(parsed.monsters.value("human test").items, QStringList({"gold [3]"}));
    QCOMPARE(parsed.raceModels.value("human male").model, QString("unhuma"));
    QCOMPARE(parsed.items.value("2h sword").typeId, 1);
    QCOMPARE(parsed.materialRecords.value("iron").code, QString("ir"));
    QCOMPARE(parsed.materialRecords.value("iron").id, 7);
    QVERIFY(parsed.namesByCategory.value("spells").contains("fireball"));
    QVERIFY(parsed.namesByCategory.value("perks").contains("two_handed"));
}

void ProfileDatabaseTests::validatesBalancedItemGrammarWithoutMutation()
{
    QString value = "sword.iron[flame][keen]";
    QVERIFY(ei::validateItemReferenceSyntax(value));
    QCOMPARE(value, QString("sword.iron[flame][keen]"));
    QString diagnostic;
    QVERIFY(!ei::validateItemReferenceSyntax("sword.iron[flame", &diagnostic));
    QVERIFY(diagnostic.contains("balanced"));
    QVERIFY(!ei::validateItemReferenceSyntax("sword.iron]", &diagnostic));
    QVERIFY(ei::validateItemReferenceSyntax("light metal axe.bronze[keen]"));
}

void ProfileDatabaseTests::providersAreStrictAndJmvOverlaysOnlyActions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath("base"));
    QVERIFY(root.mkpath("jmv"));
    QMap<QString, QByteArray> base = relationalMembers();
    base.insert("unhuma.adb", "base-action");
    write(root.filePath("base/database.res"), resArchive(base));
    QMap<QString, QByteArray> jmv = relationalMembers();
    jmv.insert("unhuma.adb", "jmv-current-action");
    write(root.filePath("jmv/database.res"), resArchive(jmv));
    write(root.filePath("jmv/databaseJmv.res"), resArchive({{"unhuma.adb", "jmv-overlay-action"}}));

    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "base.standard.v1";
    request.profileId = "compatible-base-mod";
    request.runtimeManifestId = "test-base";
    request.resourceDirectories << root.filePath("base");
    auto baseSnapshot = service.snapshot(request);
    QVERIFY(baseSnapshot->usable());
    QCOMPARE(baseSnapshot->actionTables.value("unhuma.adb"), QByteArray("base-action"));

    request.providerId = "jmv.v1";
    request.profileId = "compatible-jmv-mod";
    request.runtimeManifestId = "test-jmv";
    request.resourceDirectories = QStringList() << root.filePath("jmv") << root.filePath("base");
    auto jmvSnapshot = service.snapshot(request);
    QVERIFY(jmvSnapshot->usable());
    QCOMPARE(jmvSnapshot->actionTables.value("unhuma.adb"), QByteArray("jmv-overlay-action"));
    QVERIFY(jmvSnapshot->actionTableProvenance.value("unhuma.adb").overlay);
    QVERIFY(baseSnapshot->cacheKey != jmvSnapshot->cacheKey);
}

void ProfileDatabaseTests::unknownProviderFailsClosed()
{
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "unknown.v1";
    const auto snapshot = service.snapshot(request);
    QVERIFY(!snapshot->usable());
    QCOMPARE(snapshot->diagnostics.first().code, QString("database.provider.unavailable"));
}

QTEST_APPLESS_MAIN(ProfileDatabaseTests)
#include "profile_database_tests.moc"

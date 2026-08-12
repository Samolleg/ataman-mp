#include <QtTest>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "profile_database.h"
#include "database_document.h"
#include "database_workbook.h"
#include "lossless_database_member.h"
#include "profile_database_provider_support.h"

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
        + intField(3, 2) + intField(9, 123) + tag(29, stringField(0, "gold [3]"))
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
    file.close();
}

}

class ProfileDatabaseTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesStableLinksAndCurrentNames();
    void validatesBalancedItemGrammarWithoutMutation();
    void providersAreStrictAndJmvOverlaysOnlyActions();
    void jmvLoadsBoundedUnitPerkOwnership();
    void installedJmvTitanGripControl();
    void unknownProviderFailsClosed();
    void authoringDocumentPreservesBaselineAndTracksTransactions();
    void authoringDocumentBuildsExactNoOpAndUndoRestoresIt();
    void authoringDocumentSavePreservesOpaqueArchiveMembers();
    void authoringDocumentRejectsIdentityAndReferenceBreakage();
    void authoringDocumentDetectsStaleSourceAndKeepsProfilesSeparate();
    void losslessMemberNoOpPreservesDuplicateOrderedAndUnknownTags();
    void losslessMemberPatchChangesOnlyRequestedOccurrence();
    void installedDatabaseMembersAreByteIdenticalOnNoOp();
    void installedDatabaseDocumentNativeRoundTrip();
    void authoringDocumentPublishesCloneAndUndoRestoresOriginal();
    void workbookImportIsOneValidatedUndoableTransaction();
    void workbookNoOpAllowsPreexistingReferenceFindings();
    void authoringDocumentApplyIsAtomicAndUndoCanRestoreSource();
    void authoringDocumentRenamesStableIdentityAndTracksAppliedState();
};

QByteArray unitPerkLua(const QString& prototype, int strongMan)
{
    QStringList fields;
    fields << QString("\"%1\"").arg(prototype);
    for(int luaIndex = 2; luaIndex <= 44; ++luaIndex)
        fields << QString::number(luaIndex == 42 ? strongMan : 0);
    return QString("\xEF\xBB\xBF\nUnitPerk = {\n{%1,},\n}\n")
        .arg(fields.join(','))
        .toUtf8();
}

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

void ProfileDatabaseTests::jmvLoadsBoundedUnitPerkOwnership()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));
    QVERIFY(root.mkpath("mod/lua"));
    write(root.filePath("mod/res/database.res"), resArchive(relationalMembers()));
    write(root.filePath("mod/res/databaseJmv.res"), resArchive({}));
    write(root.filePath("mod/lua/tables.lua"), unitPerkLua("Human Test", 1));

    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "jmv.v1";
    request.profileId = "jamais-vu";
    request.runtimeManifestId = "test-jmv";
    request.resourceDirectories << root.filePath("mod/res");
    auto snapshot = service.snapshot(request);
    QVERIFY2(snapshot->jmvUnitPerkTableStatus == ei::EJmvUnitPerkTableStatus::Valid,
             qPrintable(snapshot->jmvUnitPerkDiagnostic));
    QCOMPARE(snapshot->jmvStrongManByPrototype.value("human test"), 1);

    write(root.filePath("mod/lua/tables.lua"), "UnitPerk = { {\"Human Test\", nope} }");
    snapshot = service.snapshot(request);
    QCOMPARE(snapshot->jmvUnitPerkTableStatus, ei::EJmvUnitPerkTableStatus::Malformed);
    QVERIFY(!snapshot->jmvUnitPerkDiagnostic.isEmpty());

    QVERIFY(QFile::remove(root.filePath("mod/lua/tables.lua")));
    snapshot = service.snapshot(request);
    QCOMPARE(snapshot->jmvUnitPerkTableStatus, ei::EJmvUnitPerkTableStatus::Missing);
}

void ProfileDatabaseTests::installedJmvTitanGripControl()
{
    const QString modRoot = qEnvironmentVariable("EI_TEST_JMV_MOD_ROOT");
    if(modRoot.isEmpty())
        QSKIP("Set EI_TEST_JMV_MOD_ROOT to run the installed JMV UnitPerk control.");
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "jmv.v1";
    request.profileId = "jamais-vu";
    request.runtimeManifestId = "installed-control";
    request.resourceDirectories << QDir(modRoot).filePath("Res");
    const auto snapshot = service.snapshot(request);
    QVERIFY2(snapshot->jmvUnitPerkTableStatus == ei::EJmvUnitPerkTableStatus::Valid,
             qPrintable(snapshot->jmvUnitPerkDiagnostic));
    const QString prototype = "human hadagan guard commander f20";
    QVERIFY(snapshot->monsters.contains(prototype));
    QVERIFY(snapshot->jmvStrongManByPrototype.contains(prototype));
    QVERIFY(snapshot->jmvStrongManByPrototype.value(prototype) != 0);
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

void ProfileDatabaseTests::authoringDocumentPreservesBaselineAndTracksTransactions()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); const QString path=QDir(temporary.path()).filePath("database.res");
    auto members=relationalMembers(); members["opaque.future"]="unchanged"; write(path,resArchive(members));
    ei::ProfileDatabaseService service; ei::SProfileDatabaseLoadRequest request; request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request)); QVERIFY(document);
    QVERIFY(document->setValue("monsters","human test","skinIndex",5)); QCOMPARE(document->table("monsters")->rows.first().values.value("skinIndex").toInt(),5);
    QVERIFY(document->undo()); QCOMPARE(document->table("monsters")->rows.first().values.value("skinIndex").toInt(),2); QVERIFY(document->redo());
    QVERIFY(document->deleteRow("monsters","human test"));
    QVERIFY(document->table("monsters")->rows.first().deleted);
    QVERIFY(document->undo());
    QVERIFY(!document->table("monsters")->rows.first().deleted);
    bool foundOpaque=false;
    for(const auto& field:document->table("monsters")->fields)
        foundOpaque|=field.key=="opaque.9"&&field.opaque&&!field.writable;
    QVERIFY(foundOpaque);
    QCOMPARE(document->table("monsters")->rows.first().values.value("opaque.9").toString(),QString("7b000000"));
    QVERIFY(document->pendingChangeManifest().value("preservedRawMembers").toArray().contains("opaque.future")); QVERIFY(document->pendingChangeManifest().value("publicationSupported").toBool());
}

void ProfileDatabaseTests::authoringDocumentBuildsExactNoOpAndUndoRestoresIt()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");
    const QByteArray source=resArchive(relationalMembers());write(path,source);ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QString diagnostic;QCOMPARE(document->buildArchive(&diagnostic),source);
    QVERIFY(document->setValue("monsters","human test","skinIndex",5,&diagnostic));const QByteArray changed=document->buildArchive(&diagnostic);QVERIFY2(!changed.isEmpty(),qPrintable(diagnostic));QVERIFY(changed!=source);
    QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);QVERIFY(document->redo());QCOMPARE(document->buildArchive(&diagnostic),changed);
}

void ProfileDatabaseTests::authoringDocumentSavePreservesOpaqueArchiveMembers()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");auto members=relationalMembers();members["opaque.future"]="unchanged";write(path,resArchive(members));ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QString diagnostic;QVERIFY(document->setValue("monsters","human test","skinIndex",9,&diagnostic));const QString output=QDir(temporary.path()).filePath("published.res");QVERIFY2(document->saveArchive(output,&diagnostic),qPrintable(diagnostic));QVector<ei::SDatabaseDiagnostic> findings;const auto reopened=ei::readDatabaseArchive(output,&findings);QCOMPARE(reopened.value("opaque.future"),QByteArray("unchanged"));
}

void ProfileDatabaseTests::authoringDocumentRejectsIdentityAndReferenceBreakage()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service; ei::SProfileDatabaseLoadRequest request; request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request)); QString diagnostic;
    QCOMPARE(document->displayIdentity("monsters","human test"),QString("Human Test"));
    QVERIFY(!document->cloneRow("monsters","human test","Human Test",&diagnostic)); QVERIFY(diagnostic.contains("exists")); QVERIFY(document->setValue("monsters","human test","baseRace","missing race"));
    bool missing=false,opaque=false; for(const auto& finding:document->validate()){missing|=finding.code=="database.reference.missing"&&finding.verified;opaque|=finding.code=="database.impact.opaque"&&!finding.verified;} QVERIFY(missing);QVERIFY(opaque);
}

void ProfileDatabaseTests::authoringDocumentDetectsStaleSourceAndKeepsProfilesSeparate()
{
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); QDir root(temporary.path()); QVERIFY(root.mkpath("base"));QVERIFY(root.mkpath("jmv"));
    write(root.filePath("base/database.res"),resArchive(relationalMembers()));write(root.filePath("jmv/database.res"),resArchive(relationalMembers()));write(root.filePath("jmv/databaseJmv.res"),resArchive({}));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<root.filePath("base");auto base=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    request.providerId="jmv.v1";request.profileId="jamais-vu";request.resourceDirectories=QStringList()<<root.filePath("jmv")<<root.filePath("base");auto jmv=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QCOMPARE(base->providerId(),QString("base.standard.v1"));QCOMPARE(jmv->providerId(),QString("jmv.v1"));QVERIFY(base->sourceArchivePath()!=jmv->sourceArchivePath());
    write(root.filePath("base/database.res"),resArchive({{"changed","data"}}));QString diagnostic;QVERIFY(!base->sourceIsCurrent(&diagnostic));QVERIFY(diagnostic.contains("changed"));
}

void ProfileDatabaseTests::losslessMemberNoOpPreservesDuplicateOrderedAndUnknownTags()
{
    QByteArray fields=tag(26,QByteArray::fromHex("010203"))+tag(26,QByteArray::fromHex("aabb"))
        +tag(27,QByteArray::fromHex("1122334455667788"))+tag(250,QByteArray::fromHex("deadbeef"));
    const QByteArray bytes=database({record(fields)});QString diagnostic;
    auto document=ei::LosslessDatabaseMember::parse(bytes,&diagnostic);
    QVERIFY2(document.valid(),qPrintable(diagnostic));QCOMPARE(document.serialize(),bytes);
    const auto parsed=document.fields(0,0);QCOMPARE(parsed.size(),4);QCOMPARE(parsed[0].id,26);QCOMPARE(parsed[0].occurrence,0);
    QCOMPARE(parsed[1].id,26);QCOMPARE(parsed[1].occurrence,1);QCOMPARE(parsed[3].id,250);QCOMPARE(parsed[3].payload,QByteArray::fromHex("deadbeef"));
}

void ProfileDatabaseTests::losslessMemberPatchChangesOnlyRequestedOccurrence()
{
    QByteArray fields=tag(26,QByteArray::fromHex("010203"))+tag(26,QByteArray::fromHex("aabb"))+tag(27,QByteArray::fromHex("1122334455667788"));
    const QByteArray bytes=database({record(fields)});auto document=ei::LosslessDatabaseMember::parse(bytes);QVERIFY(document.valid());
    QVERIFY(document.replaceFieldPayload(0,0,26,1,QByteArray::fromHex("ccddeeff")));
    auto patched=ei::LosslessDatabaseMember::parse(document.serialize());QVERIFY(patched.valid());const auto result=patched.fields(0,0);
    QCOMPARE(result[0].payload,QByteArray::fromHex("010203"));QCOMPARE(result[1].payload,QByteArray::fromHex("ccddeeff"));QCOMPARE(result[2].payload,QByteArray::fromHex("1122334455667788"));
}

void ProfileDatabaseTests::installedDatabaseMembersAreByteIdenticalOnNoOp()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run installed Base/JMV no-op controls.");
    for(const QString& archive:archives)
    {
        QVector<ei::SDatabaseDiagnostic> diagnostics;const auto members=ei::readDatabaseArchive(archive,&diagnostics);QVERIFY2(!members.isEmpty(),qPrintable(archive));
        for(auto it=members.constBegin();it!=members.constEnd();++it)
        {
            if(!(it.key().endsWith(".db")||it.key().endsWith(".idb")||it.key().endsWith(".udb")||it.key().endsWith(".sdb")||it.key().endsWith(".pdb")||it.key().endsWith(".ldb")))continue;
            QString diagnostic;const auto document=ei::LosslessDatabaseMember::parse(it.value(),&diagnostic);
            if(!document.valid())continue;
            QCOMPARE(document.serialize(),it.value());
        }
    }
}

void ProfileDatabaseTests::installedDatabaseDocumentNativeRoundTrip()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run installed Base/JMV native publication controls.");
    for(const QString& archive:archives)
    {
        if(QFileInfo(archive).fileName().contains("databaseJmv",Qt::CaseInsensitive))continue;
        QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString sourcePath=QDir(temporary.path()).filePath("database.res");
        QVERIFY2(QFile::copy(archive,sourcePath),qPrintable(archive));QFile source(sourcePath);QVERIFY(source.open(QIODevice::ReadOnly));const QByteArray sourceBytes=source.readAll();source.close();
        ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="native-parity";request.resourceDirectories<<temporary.path();
        auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY2(document,qPrintable(archive));QString diagnostic;QCOMPARE(document->buildArchive(&diagnostic),sourceBytes);
        const auto* monsters=document->table("monsters");QVERIFY(monsters);bool published=false;QString rowKey;int newSkin=0;QByteArray edited;
        for(const auto& row:monsters->rows)
        {
            rowKey=row.stableKey;newSkin=row.values.value("skinIndex").toInt()+1;
            if(!document->setValue("monsters",rowKey,"skinIndex",newSkin,&diagnostic))continue;
            edited=document->buildArchive(&diagnostic);if(!edited.isEmpty()){published=true;break;}QVERIFY(document->undo());
        }
        QVERIFY2(published,qPrintable(QString("%1: %2").arg(archive,diagnostic)));QVERIFY(edited!=sourceBytes);
        const QString editedPath=QDir(temporary.path()).filePath("edited.res");write(editedPath,edited);QVector<ei::SDatabaseDiagnostic> originalDiagnostics,editedDiagnostics;const auto originalMembers=ei::readDatabaseArchive(sourcePath,&originalDiagnostics);const auto editedMembers=ei::readDatabaseArchive(editedPath,&editedDiagnostics);
        QCOMPARE(editedMembers.keys(),originalMembers.keys());for(auto it=originalMembers.constBegin();it!=originalMembers.constEnd();++it)if(it.key()!="units.udb")QCOMPARE(editedMembers.value(it.key()),it.value());QVERIFY(editedMembers.value("units.udb")!=originalMembers.value("units.udb"));
        const auto parsed=ei::parseProfileDatabaseMembers(editedMembers,editedPath);QCOMPARE(parsed.monsters.value(rowKey).skinIndex,newSkin);QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),sourceBytes);
    }
}

void ProfileDatabaseTests::authoringDocumentPublishesCloneAndUndoRestoresOriginal()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");const QByteArray source=resArchive(relationalMembers());write(path,source);ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;
    QVERIFY(document->cloneRow("monsters","human test","Human Clone",&diagnostic));QVERIFY(document->setValue("monsters","human clone","skinIndex",8,&diagnostic));const QByteArray archive=document->buildArchive(&diagnostic);QVERIFY2(!archive.isEmpty(),qPrintable(diagnostic));const QString output=QDir(temporary.path()).filePath("clone.res");write(output,archive);QVector<ei::SDatabaseDiagnostic> diagnostics;const auto parsed=ei::parseProfileDatabaseMembers(ei::readDatabaseArchive(output,&diagnostics),output);QVERIFY(parsed.monsters.contains("human clone"));QCOMPARE(parsed.monsters.value("human clone").skinIndex,8);QVERIFY(document->undo());QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
}

void ProfileDatabaseTests::workbookImportIsOneValidatedUndoableTransaction()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;const QString workbook=QDir(temporary.path()).filePath("database.xlsx");QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));ei::SDatabaseWorkbookImport imported;QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),qPrintable(diagnostic));auto tables=imported.tables;tables["monsters"].rows[0].values["skinIndex"]=11;QVERIFY(document->replaceTables(tables,"Imported database.xlsx",&diagnostic));QCOMPARE(document->table("monsters")->rows[0].values.value("skinIndex").toInt(),11);QVERIFY(document->undo());QCOMPARE(document->table("monsters")->rows[0].values.value("skinIndex").toInt(),2);
    tables=imported.tables;tables["monsters"].rows[0].values["baseRace"]="missing race";QVERIFY(!document->replaceTables(tables,"Invalid workbook",&diagnostic));QCOMPARE(document->table("monsters")->rows[0].values.value("baseRace").toString(),QString("Human Male"));
}
void ProfileDatabaseTests::workbookNoOpAllowsPreexistingReferenceFindings()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());auto members=relationalMembers();QByteArray monster=stringField(0,"Broken Existing")+stringField(1,"missing race");members["units.udb"]=database({QByteArray(),record(stringField(0,"Human Male")+stringField(30,"unhuma")),record(monster),QByteArray()});write(QDir(temporary.path()).filePath("database.res"),resArchive(members));ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QMap<QString,ei::SDatabaseTable> unchanged;for(const QString& key:document->tableKeys())unchanged[key]=*document->table(key);QString diagnostic;QVERIFY2(document->replaceTables(unchanged,"No-op workbook",&diagnostic),qPrintable(diagnostic));QVERIFY(document->undo());
}
void ProfileDatabaseTests::authoringDocumentApplyIsAtomicAndUndoCanRestoreSource()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");const QByteArray original=resArchive(relationalMembers());write(path,original);ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QString diagnostic;QVERIFY2(document->setValue("monsters","human test","skinIndex",17,&diagnostic),qPrintable(diagnostic));QVERIFY2(document->applyToSource(&diagnostic),qPrintable(diagnostic));QFile applied(path);QVERIFY(applied.open(QIODevice::ReadOnly));QVERIFY(applied.readAll()!=original);applied.close();QVERIFY(document->undo());QVERIFY2(document->applyToSource(&diagnostic),qPrintable(diagnostic));QFile restored(path);QVERIFY(restored.open(QIODevice::ReadOnly));QCOMPARE(restored.readAll(),original);
}
void ProfileDatabaseTests::authoringDocumentRenamesStableIdentityAndTracksAppliedState()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");write(path,resArchive(relationalMembers()));ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QVERIFY(!document->identityAvailable("monsters","Human Test"));QVERIFY(document->identityAvailable("monsters","Human Test Copy"));QString diagnostic;QVERIFY2(document->setValue("monsters","human test","name","Human Test Renamed",&diagnostic),qPrintable(diagnostic));QVERIFY(!document->identityAvailable("monsters","human test renamed"));QVERIFY(document->hasUnappliedChanges(&diagnostic));QVERIFY2(document->applyToSource(&diagnostic),qPrintable(diagnostic));QVERIFY(!document->hasUnappliedChanges(&diagnostic));QVERIFY(document->undo());QVERIFY(document->hasUnappliedChanges(&diagnostic));
}

QTEST_APPLESS_MAIN(ProfileDatabaseTests)
#include "profile_database_tests.moc"

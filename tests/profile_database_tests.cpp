#include <QtTest>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtGui/private/qzipreader_p.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QSignalSpy>
#include <QTableWidget>
#include <cmath>

#include "profile_database.h"
#include "database_document.h"
#include "database_workbook.h"
#include "database_schema.h"
#include "database_value_codec.h"
#include "lossless_database_member.h"
#include "res_archive_document.h"
#include "database_editor_dialog.h"
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

QByteArray longTag(int id, const QByteArray& payload)
{
    QByteArray result;
    result.append(char(id));
    dword(&result, quint32(payload.size()) * 2 + 1);
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
    QByteArray race2 = stringField(0, "Human Female") + stringField(30, "unhumf")
        + tag(31, stringField(0, "unhumfskin_00"));
    QByteArray monster = stringField(0, "Human Test") + stringField(1, "Human Male")
        + intField(3, 2) + intField(4, 1) + tag(5,QByteArray::fromHex("0000803f"))
        + tag(6,QByteArray::fromHex("0000803f")) + tag(7,QByteArray::fromHex("0000803f"))
        + intField(9, 123) + tag(29, stringField(0, "gold [3]"))
        + tag(27, tag(0,QByteArray::fromHex("0000c842"))+tag(1,QByteArray::fromHex("00000000"))
            +tag(2,QByteArray::fromHex("00001c42"))+tag(3,QByteArray::fromHex("00003442")))
        + tag(32, stringField(1, "armor_leather.iron"))
        + tag(33, stringField(0, "2h sword.iron[effect{a,b}],shield.iron"))
        + stringField(42,"2h sword.iron");
    const QByteArray sparseMonster=stringField(0,"Sparse Test")+stringField(1,"Human Male")+intField(3,0);
    const QByteArray units = database({QByteArray(), record(race)+record(race2), record(monster)+record(sparseMonster), QByteArray()});

    QVector<QByteArray> itemTables;
    const QStringList names = {"iron", "2h sword", "armor_leather", "potion", "quest_key", "gold"};
    for(int index = 0; index < names.size(); ++index)
    {
        QByteArray fields = stringField(0, names.at(index).toLatin1());
        if(index == 0)
            fields += stringField(1, "Metal") + stringField(2, "ir") + intField(3, 7);
        else
            fields += stringField(1, index == 1 ? "weapon" : "item") + intField(2, index)
                + stringField(3, "metal") + intField(5, 1) + intField(6,2);
        if(index==2)fields+=intField(24,4);
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
    void installedWearsEditPreservesStructuralTagIdentity();
    void authoringDocumentPublishesCloneAndUndoRestoresOriginal();
    void workbookImportIsOneValidatedUndoableTransaction();
    void workbookUsesEidbCompatibleListAndSharedStringSyntax();
    void workbookNoOpAllowsPreexistingReferenceFindings();
    void authoringDocumentApplyIsAtomicAndUndoCanRestoreSource();
    void authoringDocumentRenamesStableIdentityAndTracksAppliedState();
    void authoringDocumentPreservesNestedWearsTagIdentity();
    void authoringDocumentExposesCompleteRawDatabaseCoverage();
    void installedFullSchemaWorkbookRoundTrip();
    void installedAllFieldCodecIdentity();
    void installedEveryStorageTypeEditIsIsolated();
    void nestedCodecPreservesUntouchedEncodingAndOpaqueBits();
    void schemaNamesRuntimeMappedFields();
    void authoringDocumentPublishesEveryWritableFieldLosslessly_data();
    void authoringDocumentPublishesEveryWritableFieldLosslessly();
    void authoringDocumentComposesTablesSharingOneMember();
    void authoringDocumentClonesAndDeletesEveryPhysicalTable();
    void authoringDocumentRenameThenDeleteRestoresExactBaseline();
    void losslessMemberFixedSizePatchPreservesOriginalHeaders();
    void workbookImportRejectsReadOnlyAndUnpublishableChanges();
    void databaseEditorRowMenuAndWrappedFieldEditor();
    void installedDatabaseInteractiveLatency();
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

void ProfileDatabaseTests::installedWearsEditPreservesStructuralTagIdentity()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run the installed wears structure control.");
    bool foundTarget=false;
    for(const QString& archive:archives)
    {
        if(QFileInfo(archive).fileName().contains("databaseJmv",Qt::CaseInsensitive))continue;
        QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString sourcePath=QDir(temporary.path()).filePath("database.res");
        QVERIFY2(QFile::copy(archive,sourcePath),qPrintable(archive));
        ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="wears-parity";request.resourceDirectories<<temporary.path();
        auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY2(document,qPrintable(archive));
        const auto* monsters=document->table("monsters");QVERIFY(monsters);
        const ei::SDatabaseRow* target=nullptr;
        for(const auto& row:monsters->rows)if(row.stableKey=="human poselok ad f4"){target=&row;break;}
        if(!target)continue;
        foundTarget=true;
        const QStringList replacement=QStringList()<<"armor plate.mithril"<<"helmet plate.mithril";
        QString diagnostic;QVERIFY2(document->setValue("monsters",target->stableKey,"wears",replacement,&diagnostic),qPrintable(diagnostic));
        const QByteArray edited=document->buildArchive(&diagnostic);QVERIFY2(!edited.isEmpty(),qPrintable(diagnostic));
        CResArchiveDocument sourceRes,editedRes;QStringList errors;
        QFile sourceFile(sourcePath);QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        QVERIFY2(sourceRes.loadBytes(sourceFile.readAll(),sourcePath,&errors),qPrintable(errors.join('\n')));errors.clear();
        QVERIFY2(editedRes.loadBytes(edited,"memory",&errors),qPrintable(errors.join('\n')));
        const auto before=ei::LosslessDatabaseMember::parse(sourceRes.memberPayload("units.udb"),&diagnostic);
        const auto after=ei::LosslessDatabaseMember::parse(editedRes.memberPayload("units.udb"),&diagnostic);
        QVERIFY(before.valid());QVERIFY(after.valid());
        const QByteArray name=QTextCodec::codecForName("CP1251")->fromUnicode("Human Poselok AD F4")+'\0';
        const int beforeRecord=before.findRecordByFieldPayload(2,0,name),afterRecord=after.findRecordByFieldPayload(2,0,name);
        QVERIFY(beforeRecord>=0);QVERIFY(afterRecord>=0);
        QByteArray beforeWears,afterWears;
        for(const auto& field:before.fields(2,beforeRecord))if(field.id==32&&field.occurrence==0)beforeWears=field.payload;
        for(const auto& field:after.fields(2,afterRecord))if(field.id==32&&field.occurrence==0)afterWears=field.payload;
        QVERIFY(!beforeWears.isEmpty());QVERIFY(!afterWears.isEmpty());
        QCOMPARE(uchar(afterWears.at(0)),uchar(beforeWears.at(0)));
        const int afterFirstLength=uchar(afterWears.at(1))/2;
        QVERIFY(2+afterFirstLength<afterWears.size());
        QCOMPARE(uchar(afterWears.at(2+afterFirstLength)),uchar(beforeWears.at(0)));
    }
    QVERIFY2(foundTarget,"Human Poselok AD F4 was absent from every supplied database archive.");
}

void ProfileDatabaseTests::authoringDocumentPublishesCloneAndUndoRestoresOriginal()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");const QByteArray source=resArchive(relationalMembers());write(path,source);ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;
    QVERIFY(document->cloneRow("monsters","human test","Human Clone",&diagnostic));QVERIFY(document->setValue("monsters","human clone","skinIndex",8,&diagnostic));const QByteArray archive=document->buildArchive(&diagnostic);QVERIFY2(!archive.isEmpty(),qPrintable(diagnostic));const QString output=QDir(temporary.path()).filePath("clone.res");write(output,archive);QVector<ei::SDatabaseDiagnostic> diagnostics;const auto parsed=ei::parseProfileDatabaseMembers(ei::readDatabaseArchive(output,&diagnostics),output);QVERIFY(parsed.monsters.contains("human clone"));QCOMPARE(parsed.monsters.value("human clone").skinIndex,8);QVERIFY(document->undo());QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
}

void ProfileDatabaseTests::workbookImportIsOneValidatedUndoableTransaction()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;const QString workbook=QDir(temporary.path()).filePath("database.xlsx");QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));ei::SDatabaseWorkbookImport imported;QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),qPrintable(diagnostic));auto tables=imported.tables;int monsterRow=-1;for(int index=0;index<tables["monsters"].rows.size();++index)if(tables["monsters"].rows[index].stableKey=="human test")monsterRow=index;QVERIFY(monsterRow>=0);tables["monsters"].rows[monsterRow].values["skinIndex"]=11;QVERIFY2(document->replaceTables(tables,"Imported database.xlsx",&diagnostic),qPrintable(diagnostic));QCOMPARE(document->table("monsters")->rows[monsterRow].values.value("skinIndex").toInt(),11);QVERIFY(document->undo());QCOMPARE(document->table("monsters")->rows[monsterRow].values.value("skinIndex").toInt(),2);
    tables=imported.tables;tables["monsters"].rows[monsterRow].values["baseRace"]="missing race";QVERIFY(!document->replaceTables(tables,"Invalid workbook",&diagnostic));QCOMPARE(document->table("monsters")->rows[monsterRow].values.value("baseRace").toString(),QString("Human Male"));
}

void ProfileDatabaseTests::workbookUsesEidbCompatibleListAndSharedStringSyntax()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const auto* monsters=document->table("units.monsters");QVERIFY(monsters);QString monsterRow;for(const auto& row:monsters->rows)if(row.sourceStableKey=="Human Test")monsterRow=row.stableKey;QVERIFY(!monsterRow.isEmpty());QString setupDiagnostic;QVERIFY2(document->setValue("units.monsters",monsterRow,"fld.32.0",QStringLiteral("[\"armor_leather.iron\",\"helmet_plate.iron\"]"),&setupDiagnostic),qPrintable(setupDiagnostic));
    QString diagnostic;const QString workbook=QDir(temporary.path()).filePath("database.xlsx");
    QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));
    QZipReader zip(workbook);QVERIFY(zip.exists());
    const QByteArray shared=zip.fileData("xl/sharedStrings.xml");QVERIFY(!shared.isEmpty());
    QVERIFY(shared.contains("armor_leather.iron,helmet_plate.iron"));
    QVERIFY(!shared.contains("[&quot;armor_leather.iron&quot;"));
    QVERIFY(shared.contains("FLD32-0"));
    const int sheetCount=ei::workbookTableKeys(*document).size();QVERIFY(sheetCount>0);for(int index=1;index<=sheetCount;++index){const QByteArray sheet=zip.fileData(QString("xl/worksheets/sheet%1.xml").arg(index));QVERIFY2(!sheet.isEmpty(),qPrintable(QString("sheet %1 missing").arg(index)));QVERIFY2(sheet.contains("<dimension ref="),qPrintable(QString("sheet %1 lacks dimension").arg(index)));QVERIFY(!sheet.contains("inlineStr"));}
    QVERIFY2(!zip.fileData("eidbeditor/source.res").isEmpty(),"Patched EIDBEditor source archive is missing");
    QCOMPARE(zip.fileData("eidbeditor/source.res"),document->buildArchive(&diagnostic));
    QCOMPARE(zip.fileData("eidbeditor/sheets.sha256").trimmed().size(),64);
    ei::SDatabaseWorkbookImport imported;
    QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),qPrintable(diagnostic));
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

void ProfileDatabaseTests::authoringDocumentPreservesNestedWearsTagIdentity()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = QDir(temporary.path()).filePath("database.res");
    auto members=relationalMembers();
    const QByteArray untouched=longTag(7,QByteArray("helmet_plate.iron\0",18));
    const QByteArray monster=stringField(0,"Human Test")+stringField(1,"Human Male")
        +intField(3,2)+intField(4,1)+tag(5,QByteArray::fromHex("0000803f"))
        +tag(6,QByteArray::fromHex("0000803f"))+tag(7,QByteArray::fromHex("0000803f"))
        +tag(32,stringField(1,"armor_leather.iron")+untouched);
    members["units.udb"]=database({QByteArray(),record(stringField(0,"Human Male")
        +stringField(30,"unhuma")),record(monster),QByteArray()});
    write(sourcePath, resArchive(members));
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "base.standard.v1";
    request.profileId = "base";
    request.resourceDirectories << temporary.path();
    auto document = ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    QString diagnostic;
    const QString workbook=QDir(temporary.path()).filePath("database.xlsx");
    QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));
    ei::SDatabaseWorkbookImport imported;
    QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),qPrintable(diagnostic));
    auto tables=imported.tables;int monsterRow=-1;
    for(int index=0;index<tables["monsters"].rows.size();++index)
        if(tables["monsters"].rows[index].stableKey=="human test")monsterRow=index;
    QVERIFY(monsterRow>=0);
    tables["monsters"].rows[monsterRow].values["wears"]=
        QStringList()<<"armor_plate.meteoric"<<"helmet_plate.iron";
    QVERIFY2(document->replaceTables(tables,"Imported nested database edit",&diagnostic),qPrintable(diagnostic));
    const QByteArray archive = document->buildArchive(&diagnostic);
    QVERIFY2(!archive.isEmpty(), qPrintable(diagnostic));
    CResArchiveDocument res;
    QStringList errors;
    QVERIFY2(res.loadBytes(archive, "memory", &errors), qPrintable(errors.join('\n')));
    const auto member = ei::LosslessDatabaseMember::parse(res.memberPayload("units.udb"),
                                                           &diagnostic);
    QVERIFY2(member.valid(), qPrintable(diagnostic));
    const int recordIndex = member.findRecordByFieldPayload(2, 0,
                                                             stringField(0, "Human Test").mid(2));
    QVERIFY(recordIndex >= 0);
    QByteArray wearsPayload;
    for(const auto& field : member.fields(2, recordIndex))
        if(field.id == 32 && field.occurrence == 0)
            wearsPayload = field.payload;
    QVERIFY(!wearsPayload.isEmpty());
    QCOMPARE(wearsPayload,tag(1,QByteArray("armor_plate.meteoric\0",21))+untouched);
}

void ProfileDatabaseTests::authoringDocumentExposesCompleteRawDatabaseCoverage()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = QDir(temporary.path()).filePath("database.res");
    const QByteArray source = resArchive(relationalMembers());
    write(sourcePath, source);
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "base.standard.v1";
    request.profileId = "base";
    request.resourceDirectories << temporary.path();
    auto document = ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    QVERIFY(document->tableKeys().size() > 3);
    const auto* fullMonsters = document->table("units.monsters");
    QVERIFY(fullMonsters);
    QVERIFY(fullMonsters->structuralEditing);
    QCOMPARE(fullMonsters->rows.size(), 2);
    QVERIFY(fullMonsters->rows.first().values.contains("fld.32.0"));
    QString diagnostic;
    QVERIFY(document->cloneRow(fullMonsters->key, fullMonsters->rows.first().stableKey,
                               "Full Schema Clone", &diagnostic));
    QVERIFY2(!document->buildArchive(&diagnostic).isEmpty(),qPrintable(diagnostic));
    QVERIFY(document->undo());
    QCOMPARE(document->buildArchive(&diagnostic), source);
}

void ProfileDatabaseTests::installedFullSchemaWorkbookRoundTrip()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run full workbook corpus parity.");
    for(const QString& archivePath:archives)
    {
        const QFileInfo info(archivePath);QVERIFY2(info.isFile(),qPrintable(archivePath));if(info.fileName().compare("database.res",Qt::CaseInsensitive)!=0)continue;ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<info.absolutePath();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QString diagnostic;QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString workbook=QDir(temporary.path()).filePath("database.xlsx");
        QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));const QString retained=qEnvironmentVariable("EI_TEST_RETAIN_XLSX");if(!retained.isEmpty()){QFile::remove(retained);QVERIFY2(QFile::copy(workbook,retained),qPrintable(retained));}ei::SDatabaseWorkbookImport imported;QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),qPrintable(diagnostic));QVERIFY2(document->replaceTables(imported.tables,"Full schema XLSX no-op",&diagnostic),qPrintable(diagnostic));QFile source(archivePath);QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(document->buildArchive(&diagnostic),source.readAll());QVERIFY(document->undo());
        const QString eidbWorkbook=qEnvironmentVariable("EI_TEST_EIDB_WORKBOOK");if(!eidbWorkbook.isEmpty()){ei::SDatabaseWorkbookImport eidbImported;QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,eidbWorkbook,&eidbImported,&diagnostic),qPrintable(diagnostic));QVERIFY2(document->replaceTables(eidbImported.tables,"EIDB workbook no-op",&diagnostic),qPrintable(diagnostic));QFile eidbSource(archivePath);QVERIFY(eidbSource.open(QIODevice::ReadOnly));QCOMPARE(document->buildArchive(&diagnostic),eidbSource.readAll());}
        QZipReader zip(workbook);QVERIFY(zip.exists());const QStringList keys=ei::workbookTableKeys(*document);QCOMPARE(keys.size(),26);for(int index=0;index<keys.size();++index)QVERIFY(!zip.fileData(QString("xl/worksheets/sheet%1.xml").arg(index+1)).isEmpty());QVERIFY(!zip.fileData("ei-mapper/source.res").isEmpty());
    }
}

void ProfileDatabaseTests::installedAllFieldCodecIdentity()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run exhaustive codec parity.");const auto& schema=ei::DatabaseSchema::instance();QVERIFY2(schema.diagnostic().isEmpty(),qPrintable(schema.diagnostic()));
    for(const QString& archivePath:archives)
    {
        QVector<ei::SDatabaseDiagnostic> diagnostics;const auto members=ei::readDatabaseArchive(archivePath,&diagnostics);
        for(const auto& table:schema.tables())
        {
            if(!members.contains(table.memberName))continue;QString parseDiagnostic;const auto member=ei::LosslessDatabaseMember::parse(members.value(table.memberName),&parseDiagnostic);QVERIFY2(member.valid(),qPrintable(parseDiagnostic));QVERIFY(table.physicalIndex<member.tableCount());
            for(int record=0;record<member.recordCount(table.physicalIndex);++record)for(const auto& field:member.fields(table.physicalIndex,record))
            {
                const auto* descriptor=schema.field(table.family,table.blockId,field.id);if(!descriptor)continue;QString diagnostic;const QVariant value=ei::DatabaseValueCodec::decode(field.payload,*descriptor,schema,table.family,&diagnostic);QVERIFY2(diagnostic.isEmpty(),qPrintable(QString("%1 row %2 FLD%3: %4").arg(table.key).arg(record).arg(field.id).arg(diagnostic)));const QByteArray encoded=ei::DatabaseValueCodec::encode(value,field.payload,*descriptor,schema,table.family,&diagnostic);QVERIFY2(diagnostic.isEmpty(),qPrintable(diagnostic));QVERIFY2(encoded==field.payload,qPrintable(QString("%1 row %2 FLD%3 type %4 encoded=%5 source=%6").arg(table.key).arg(record).arg(field.id).arg(int(descriptor->type)).arg(QString::fromLatin1(encoded.toHex()),QString::fromLatin1(field.payload.toHex()))));
            }
        }
    }
}

void ProfileDatabaseTests::installedEveryStorageTypeEditIsIsolated()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run storage-type edit isolation.");const auto& schema=ei::DatabaseSchema::instance();QSet<int> tested;
    for(const QString& archivePath:archives)
    {
        QVector<ei::SDatabaseDiagnostic> diagnostics;const auto members=ei::readDatabaseArchive(archivePath,&diagnostics);
        for(const auto& table:schema.tables())
        {
            if(!members.contains(table.memberName))continue;const auto original=ei::LosslessDatabaseMember::parse(members.value(table.memberName));if(!original.valid())continue;
            for(int record=0;record<original.recordCount(table.physicalIndex);++record)for(const auto& field:original.fields(table.physicalIndex,record))
            {
                const auto* descriptor=schema.field(table.family,table.blockId,field.id);if(!descriptor||tested.contains(int(descriptor->type)))continue;QString diagnostic;QVariant value=ei::DatabaseValueCodec::decode(field.payload,*descriptor,schema,table.family,&diagnostic);if(!diagnostic.isEmpty())continue;QByteArray replacement=field.payload;
                if(descriptor->type==ei::EDatabaseStorageType::String||descriptor->type==ei::EDatabaseStorageType::FixedString){value=value.toString()+"_codex";replacement=ei::DatabaseValueCodec::encode(value,field.payload,*descriptor,schema,table.family,&diagnostic);}
                else if(descriptor->type==ei::EDatabaseStorageType::SignedLong||descriptor->type==ei::EDatabaseStorageType::UnsignedLong||descriptor->type==ei::EDatabaseStorageType::Byte){value=value.toLongLong()+1;replacement=ei::DatabaseValueCodec::encode(value,field.payload,*descriptor,schema,table.family,&diagnostic);}
                else if(descriptor->type==ei::EDatabaseStorageType::Float&&std::isfinite(value.toDouble())){value=value.toDouble()+0.25;replacement=ei::DatabaseValueCodec::encode(value,field.payload,*descriptor,schema,table.family,&diagnostic);}
                else if(descriptor->type==ei::EDatabaseStorageType::Hex&&!field.payload.isEmpty()){replacement=field.payload;replacement[0]=char(uchar(replacement[0])^1);}
                else if(value.type()==QVariant::String){QJsonParseError error;QJsonDocument doc=QJsonDocument::fromJson(value.toString().toUtf8(),&error);if(error.error==QJsonParseError::NoError&&doc.isArray()&&!doc.array().isEmpty()){QJsonArray array=doc.array();if(array[0].isBool())array[0]=!array[0].toBool();else if(array[0].isDouble())array[0]=array[0].toDouble()+1;else if(array[0].isString())array[0]=array[0].toString()+"_codex";else if(array[0].isObject()){QJsonObject object=array[0].toObject();if(object.contains("value")){if(object["value"].isDouble())object["value"]=object["value"].toDouble()+1;else if(object["value"].isString())object["value"]=object["value"].toString()+"_codex";}else if(object.value("fields").isArray()){QJsonArray fields=object.value("fields").toArray();if(!fields.isEmpty()){QJsonObject nested=fields[0].toObject();if(nested["value"].isDouble())nested["value"]=nested["value"].toDouble()+1;else nested["value"]=nested["value"].toString()+"_codex";fields[0]=nested;object["fields"]=fields;}}array[0]=object;}value=QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));replacement=ei::DatabaseValueCodec::encode(value,field.payload,*descriptor,schema,table.family,&diagnostic);}}
                if(replacement.isEmpty()||replacement==field.payload||!diagnostic.isEmpty())continue;auto changed=original;QVERIFY(changed.replaceFieldPayload(table.physicalIndex,record,field.id,field.occurrence,replacement,&diagnostic));const auto reparsed=ei::LosslessDatabaseMember::parse(changed.serialize(),&diagnostic);QVERIFY(reparsed.valid());for(int ti=0;ti<original.tableCount();++ti)for(int ri=0;ri<original.recordCount(ti);++ri){const auto before=original.fields(ti,ri),after=reparsed.fields(ti,ri);QCOMPARE(after.size(),before.size());for(int fi=0;fi<before.size();++fi)if(!(ti==table.physicalIndex&&ri==record&&before[fi].id==field.id&&before[fi].occurrence==field.occurrence))QCOMPARE(after[fi].encoded,before[fi].encoded);}tested.insert(int(descriptor->type));
            }
        }
    }
    const QSet<int> corpusTypes=QSet<int>()<<int(ei::EDatabaseStorageType::Hex)<<int(ei::EDatabaseStorageType::String)<<int(ei::EDatabaseStorageType::UnsignedLong)<<int(ei::EDatabaseStorageType::SignedLong)<<int(ei::EDatabaseStorageType::Float)<<int(ei::EDatabaseStorageType::Byte)<<int(ei::EDatabaseStorageType::FloatList)<<int(ei::EDatabaseStorageType::ByteList)<<int(ei::EDatabaseStorageType::StringList)<<int(ei::EDatabaseStorageType::FixedStringList)<<int(ei::EDatabaseStorageType::BitList)<<int(ei::EDatabaseStorageType::TypeList)<<int(ei::EDatabaseStorageType::AcksUniqueType)<<int(ei::EDatabaseStorageType::FixedString);
    for(int type:corpusTypes)QVERIFY2(tested.contains(type),qPrintable(QString("Installed corpus storage type %1 was not mutation-tested.").arg(type)));
}

void ProfileDatabaseTests::nestedCodecPreservesUntouchedEncodingAndOpaqueBits()
{
    const auto& schema=ei::DatabaseSchema::instance();
    QString diagnostic;
    auto editFirst=[&](ei::EDatabaseStorageType type,const QByteArray& source,
                       const std::function<void(QJsonArray*)>& edit,int modifier=0)->QByteArray
    {
        diagnostic.clear();
        ei::SDatabaseStorageField field;field.type=type;field.modifier=modifier;
        const QVariant decoded=ei::DatabaseValueCodec::decode(source,field,schema,"__lossless_test__",&diagnostic);
        QJsonDocument document=QJsonDocument::fromJson(decoded.toString().toUtf8());
        if(!diagnostic.isEmpty()||!document.isArray())return {};
        QJsonArray values=document.array();edit(&values);
        const QByteArray encoded=ei::DatabaseValueCodec::encode(
            QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact)),
            source,field,schema,"__lossless_test__",&diagnostic);
        return encoded;
    };

    const QByteArray untouchedString=longTag(9,QByteArray("beta\0",5));
    const QByteArray stringSource=tag(7,QByteArray("alpha\0",6))+untouchedString;
    QCOMPARE(editFirst(ei::EDatabaseStorageType::StringList,stringSource,
        [](QJsonArray* a){(*a)[0]="omega";}),tag(7,QByteArray("omega\0",6))+untouchedString);

    const QByteArray fixedFirst=QByteArray("one\0\0\0\0\0",8);
    const QByteArray fixedChanged=QByteArray("six\0\0\0\0\0",8);
    const QByteArray untouchedFixed=longTag(10,QByteArray("two\0\0\0\0\0",8));
    const QByteArray fixedSource=tag(8,fixedFirst)+untouchedFixed;
    QCOMPARE(editFirst(ei::EDatabaseStorageType::FixedStringList,fixedSource,
        [](QJsonArray* a){(*a)[0]="six";},8),tag(8,fixedChanged)+untouchedFixed);

    const QByteArray untouchedFloat=longTag(12,QByteArray::fromHex("00000040"));
    const QByteArray floatSource=tag(11,QByteArray::fromHex("0000803f"))+untouchedFloat;
    QCOMPARE(editFirst(ei::EDatabaseStorageType::IndexedFloatList,floatSource,
        [](QJsonArray* a){QJsonObject o=(*a)[0].toObject();o["value"]=3.0;(*a)[0]=o;}),
        tag(11,QByteArray::fromHex("00004040"))+untouchedFloat);

    const QByteArray untouchedTyped=longTag(22,QByteArray::fromHex("bb"));
    const QByteArray typedSource=tag(21,QByteArray::fromHex("aa"))+untouchedTyped;
    QCOMPARE(editFirst(ei::EDatabaseStorageType::TypeList,typedSource,
        [](QJsonArray* a){QJsonObject o=(*a)[0].toObject();o["value"]="ab";(*a)[0]=o;},99),
        tag(21,QByteArray::fromHex("ab"))+untouchedTyped);

    const QByteArray untouchedChild=longTag(32,QByteArray::fromHex("cc"));
    const QByteArray firstChildren=tag(31,QByteArray::fromHex("aa"))+untouchedChild;
    const QByteArray untouchedOuter=longTag(42,longTag(33,QByteArray::fromHex("dd")));
    const QByteArray opaqueOuter=longTag(43,QByteArray::fromHex("ff"));
    const QByteArray compoundSource=tag(41,firstChildren)+untouchedOuter+opaqueOuter;
    const QByteArray compoundExpected=tag(41,tag(31,QByteArray::fromHex("ab"))+untouchedChild)+untouchedOuter+opaqueOuter;
    for(const auto type:{ei::EDatabaseStorageType::CapsulatedTypeList,ei::EDatabaseStorageType::AcksUniqueType})
        QCOMPARE(editFirst(type,compoundSource,[](QJsonArray* a){
            QJsonObject outer=(*a)[0].toObject();QJsonArray fields=outer["fields"].toArray();
            QJsonObject child=fields[0].toObject();child["value"]="ab";fields[0]=child;
            outer["fields"]=fields;(*a)[0]=outer;
        },99),compoundExpected);

    ei::SDatabaseStorageField bits;bits.type=ei::EDatabaseStorageType::BitList;bits.modifier=3;
    const QByteArray bitSource(1,char(0xf8));
    const QVariant decoded=ei::DatabaseValueCodec::decode(bitSource,bits,schema,"__lossless_test__",&diagnostic);
    QJsonArray values=QJsonDocument::fromJson(decoded.toString().toUtf8()).array();values[0]=true;
    QCOMPARE(ei::DatabaseValueCodec::encode(
        QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact)),
        bitSource,bits,schema,"__lossless_test__",&diagnostic),QByteArray(1,char(0xf9)));
}

void ProfileDatabaseTests::schemaNamesRuntimeMappedFields()
{
    struct Expected { const char* family; int block; int field; int index; const char* name; };
    const Expected expected[] = {
        {"Items",1,12,0,"Cripple Mask"}, {"Items",2,4,0,"Material Type Index"},
        {"Items",2,22,0,"Stat Modifiers"}, {"Items",2,27,0,"Manual Skill Modifiers"},
        {"Items",2,28,0,"Automatic Skill Modifiers"}, {"Items",3,21,8,"Main Cripple Mask"},
        {"Items",3,22,8,"Additional Cripple Mask"}, {"Items",3,25,0,"Manual Skill Modifiers"},
        {"Items",3,26,0,"Automatic Skill Modifiers"}, {"Items",4,2,0,"Type ID"},
        {"Items",4,24,0,"Damage Type"}, {"Items",4,26,2,"Tame Modifier"},
        {"Items",4,27,0,"Automatic Skill Modifiers"}, {"Items",6,22,0,"Instruction Type"},
        {"Items",6,23,0,"Material Index"}, {"Levers",1,2,0,"Valid Place Count"},
        {"Perks",2,13,0,"Manual Skill ID"}, {"Prints",2,10,1,"Bloody Opacity"},
        {"Prints",2,10,2,"Bloody Lifetime"}, {"Prints",2,10,3,"Bloody Fadeout"},
        {"Units",1,3,0,"Cripple Mask"}, {"Units",1,4,0,"Hit Location Type"},
        {"Units",2,12,1,"Head Type Index"}, {"Units",2,17,1,"Left Leg Type Index"},
        {"Units",2,47,0,"Mouth Point X"}, {"Units",2,48,0,"Mouth Point Y"},
        {"Units",2,49,0,"Mouth Rectangle Size X"}, {"Units",2,50,0,"Mouth Rectangle Size Y"},
        {"Units",3,2,0,"Base Race Index"}, {"Units",3,8,0,"Sounds Data SFX Path"},
        {"Units",4,1,0,"Character Points"}, {"Acks",1,33,0,"Target Too High"},
        {"Quests",1,2,0,"Total Previous Experience"}, {"Quests",1,7,0,"Quest Places"},
        {"Quests",2,1,0,"Experience"}, {"Quests",2,9,0,"Complete Zones"}
    };
    const auto& schema=ei::DatabaseSchema::instance();
    for(const Expected& item:expected)
    {
        const auto headers=schema.headers(item.family,item.block,item.field);
        auto found=std::find_if(headers.cbegin(),headers.cend(),[&](const ei::SDatabaseStorageHeader& header){return header.index==item.index;});
        QVERIFY2(found!=headers.cend(),qPrintable(QString("Missing %1 block %2 FLD%3[%4]").arg(item.family).arg(item.block).arg(item.field).arg(item.index)));
        QCOMPARE(found->name,QString::fromLatin1(item.name));
    }
}

void ProfileDatabaseTests::authoringDocumentPublishesEveryWritableFieldLosslessly_data()
{
    QTest::addColumn<QString>("table");QTest::addColumn<QString>("row");
    QTest::addColumn<QString>("field");QTest::addColumn<QVariant>("value");
    QTest::newRow("race-name")<<"raceModels"<<"human male"<<"name"<<QVariant("Human Male Renamed");
    QTest::newRow("race-model")<<"raceModels"<<"human male"<<"model"<<QVariant("unhumb");
    QTest::newRow("monster-name")<<"monsters"<<"human test"<<"name"<<QVariant("Human Test Renamed");
    QTest::newRow("monster-race")<<"monsters"<<"human test"<<"baseRace"<<QVariant("Human Female");
    QTest::newRow("monster-skin")<<"monsters"<<"human test"<<"skinIndex"<<QVariant(7);
    QTest::newRow("monster-hair")<<"monsters"<<"human test"<<"hairIndex"<<QVariant(4);
    QTest::newRow("monster-x")<<"monsters"<<"human test"<<"complexionX"<<QVariant(1.25);
    QTest::newRow("monster-y")<<"monsters"<<"human test"<<"complexionY"<<QVariant(1.5);
    QTest::newRow("monster-z")<<"monsters"<<"human test"<<"complexionZ"<<QVariant(1.75);
    QTest::newRow("monster-items")<<"monsters"<<"human test"<<"items"<<QVariant(QStringList()<<"gold [5]");
    QTest::newRow("monster-wears")<<"monsters"<<"human test"<<"wears"<<QVariant(QStringList()<<"armor_leather.iron"<<"armor_leather.iron");
    QTest::newRow("monster-second-weapon")<<"monsters"<<"human test"<<"secondWeapon"<<QVariant("shield.iron");
    QTest::newRow("weapon-name")<<"items"<<"2h sword"<<"name"<<QVariant("2h sword renamed");
    QTest::newRow("weapon-type")<<"items"<<"2h sword"<<"type"<<QVariant("weapon2");
    QTest::newRow("weapon-type-id")<<"items"<<"2h sword"<<"typeId"<<QVariant(12);
    QTest::newRow("weapon-material")<<"items"<<"2h sword"<<"materialType"<<QVariant("metal2");
    QTest::newRow("weapon-texture-1")<<"items"<<"2h sword"<<"textureType1"<<QVariant(3);
    QTest::newRow("weapon-texture-2")<<"items"<<"2h sword"<<"textureType2"<<QVariant(4);
    QTest::newRow("armor-wear-order")<<"items"<<"armor_leather"<<"wearOrder"<<QVariant(8);
}

void ProfileDatabaseTests::authoringDocumentPublishesEveryWritableFieldLosslessly()
{
    QFETCH(QString,table);QFETCH(QString,row);QFETCH(QString,field);QFETCH(QVariant,value);
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");
    const QByteArray source=resArchive(relationalMembers());write(path,source);
    CResArchiveDocument baseline;QStringList errors;QVERIFY(baseline.loadBytes(source,"baseline",&errors));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QString diagnostic;
    QVERIFY2(document->setValue(table,row,field,value,&diagnostic),qPrintable(diagnostic));
    const QByteArray edited=document->buildArchive(&diagnostic);QVERIFY2(!edited.isEmpty(),qPrintable(diagnostic));QVERIFY(edited!=source);
    CResArchiveDocument candidate;errors.clear();QVERIFY2(candidate.loadBytes(edited,"candidate",&errors),qPrintable(errors.join('\n')));
    const QString changedMember=table=="items"?"items.idb":"units.udb";
    QCOMPARE(candidate.members().size(),baseline.members().size());
    for(const auto& member:baseline.members())if(member.name.compare(changedMember,Qt::CaseInsensitive)!=0)QCOMPARE(candidate.memberPayload(member.name),member.payload);
    const auto original=ei::LosslessDatabaseMember::parse(baseline.memberPayload(changedMember),&diagnostic);
    const auto changed=ei::LosslessDatabaseMember::parse(candidate.memberPayload(changedMember),&diagnostic);QVERIFY2(original.valid()&&changed.valid(),qPrintable(diagnostic));
    int physicalTable=table=="raceModels"?1:(table=="monsters"?2:-1),physicalField=-1;
    if(table=="items")
    {
        const auto* items=document->table("items");QVERIFY(items);for(const auto& item:items->rows)if(item.sourceStableKey.compare(row,Qt::CaseInsensitive)==0||item.stableKey==row){physicalTable=QStringList({"materials","weapons","armor","quickItems","questItems","lootItems"}).indexOf(item.values.value("category").toString());break;}
    }
    if(field=="name")physicalField=0;else if(table=="raceModels"&&field=="model")physicalField=30;
    else if(table=="monsters")physicalField=QMap<QString,int>{{"baseRace",1},{"skinIndex",3},{"hairIndex",4},{"complexionX",5},{"complexionY",6},{"complexionZ",7},{"items",29},{"wears",32},{"secondWeapon",42}}.value(field,-1);
    else if(table=="items")physicalField=QMap<QString,int>{{"type",1},{"typeId",2},{"materialType",3},{"textureType1",5},{"textureType2",6},{"wearOrder",24}}.value(field,-1);
    QVERIFY(physicalTable>=0);QVERIFY(physicalField>=0);QCOMPARE(changed.tableCount(),original.tableCount());
    int targetRecord=-1;
    for(int recordIndex=0;recordIndex<original.recordCount(physicalTable);++recordIndex)
        for(const auto& sourceField:original.fields(physicalTable,recordIndex))
            if(sourceField.id==0&&sourceField.occurrence==0
               &&QTextCodec::codecForName("CP1251")->toUnicode(sourceField.payload).remove('\0').compare(row,Qt::CaseInsensitive)==0)
                targetRecord=recordIndex;
    QVERIFY(targetRecord>=0);
    for(int tableIndex=0;tableIndex<original.tableCount();++tableIndex)
    {
        QCOMPARE(changed.recordCount(tableIndex),original.recordCount(tableIndex));
        for(int recordIndex=0;recordIndex<original.recordCount(tableIndex);++recordIndex)
        {
            const auto beforeFields=original.fields(tableIndex,recordIndex),afterFields=changed.fields(tableIndex,recordIndex);QCOMPARE(afterFields.size(),beforeFields.size());
            for(int index=0;index<beforeFields.size();++index)
            {
                QCOMPARE(afterFields.at(index).id,beforeFields.at(index).id);QCOMPARE(afterFields.at(index).occurrence,beforeFields.at(index).occurrence);
                const bool target=tableIndex==physicalTable&&recordIndex==targetRecord
                    &&beforeFields.at(index).id==physicalField&&beforeFields.at(index).occurrence==0;
                if(!target)QCOMPARE(afterFields.at(index).encoded,beforeFields.at(index).encoded);
            }
        }
    }
    QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
}

void ProfileDatabaseTests::authoringDocumentComposesTablesSharingOneMember()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");const QByteArray source=resArchive(relationalMembers());write(path,source);
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;
    QVERIFY(document->setValue("raceModels","human male","model","unhumb",&diagnostic));QVERIFY(document->setValue("monsters","human test","skinIndex",8,&diagnostic));
    const auto snapshot=document->currentSnapshot(&diagnostic);QVERIFY2(snapshot,qPrintable(diagnostic));QCOMPARE(snapshot->raceModels.value("human male").model,QString("unhumb"));QCOMPARE(snapshot->monsters.value("human test").skinIndex,8);
    QVERIFY(document->undo());QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
}

void ProfileDatabaseTests::authoringDocumentClonesAndDeletesEveryPhysicalTable()
{
    const QVector<QPair<QString,QString>> rows={{"raceModels","human male"},{"monsters","human test"},{"items","2h sword"},{"items","armor_leather"},{"items","potion"},{"items","quest_key"},{"items","gold"}};
    for(const auto& test:rows)
    {
        QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");const QByteArray source=resArchive(relationalMembers());write(path,source);
        ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;const QString clone=test.second+" clone";
        QVERIFY2(document->cloneRow(test.first,test.second,clone,&diagnostic),qPrintable(diagnostic));const auto snapshot=document->currentSnapshot(&diagnostic);QVERIFY2(snapshot,qPrintable(QString("%1/%2: %3").arg(test.first,test.second,diagnostic)));
        QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
        QVERIFY(document->deleteRow(test.first,test.second,&diagnostic));QVERIFY2(!document->buildArchive(&diagnostic).isEmpty(),qPrintable(diagnostic));QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
    }
}

void ProfileDatabaseTests::authoringDocumentRenameThenDeleteRestoresExactBaseline()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");const QByteArray source=resArchive(relationalMembers());write(path,source);
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QString diagnostic;
    QVERIFY(document->setValue("monsters","human test","name","Human Renamed",&diagnostic));QVERIFY(document->deleteRow("monsters","human renamed",&diagnostic));QVERIFY2(!document->buildArchive(&diagnostic).isEmpty(),qPrintable(diagnostic));
    QVERIFY(document->undo());QVERIFY(document->undo());QCOMPARE(document->buildArchive(&diagnostic),source);
}

void ProfileDatabaseTests::losslessMemberFixedSizePatchPreservesOriginalHeaders()
{
    QByteArray nonCanonical;nonCanonical.append(char(3));dword(&nonCanonical,9);nonCanonical+=QByteArray::fromHex("01020304");
    const QByteArray bytes=database({record(nonCanonical)});auto member=ei::LosslessDatabaseMember::parse(bytes);QVERIFY(member.valid());QVERIFY(member.replaceFieldPayload(0,0,3,0,QByteArray::fromHex("05060708")));
    const QByteArray changed=member.serialize();QCOMPARE(changed.size(),bytes.size());
    int difference=0;for(int index=0;index<bytes.size();++index)difference+=bytes.at(index)!=changed.at(index);QCOMPARE(difference,4);
}

void ProfileDatabaseTests::workbookImportRejectsReadOnlyAndUnpublishableChanges()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");write(path,resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QString diagnostic;
    QMap<QString,ei::SDatabaseTable> candidate;for(const QString& key:document->tableKeys())candidate[key]=*document->table(key);
    for(auto& row:candidate["items"].rows)if(row.stableKey=="2h sword")row.values["category"]="armor";
    QVERIFY(!document->replaceTables(candidate,"unsafe read-only import",&diagnostic));QVERIFY(diagnostic.contains("Read-only"));QVERIFY(!document->isDirty());
    candidate.clear();for(const QString& key:document->tableKeys())candidate[key]=*document->table(key);
    for(auto& row:candidate["monsters"].rows)if(row.stableKey=="human test")row.values["weapon"]="unsafe";
    QVERIFY(!document->replaceTables(candidate,"unsafe unsupported import",&diagnostic));QVERIFY(diagnostic.contains("Read-only"));QVERIFY(!document->isDirty());
    candidate.clear();for(const QString& key:document->tableKeys())candidate[key]=*document->table(key);
    for(auto& row:candidate["monsters"].rows)if(row.stableKey=="sparse test")row.values["hairIndex"]=5;
    QVERIFY(!document->replaceTables(candidate,"unsafe absent-field import",&diagnostic));QVERIFY(diagnostic.contains("does not exist")||diagnostic.contains("absent"));QVERIFY(!document->isDirty());
}

void ProfileDatabaseTests::databaseEditorRowMenuAndWrappedFieldEditor()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"), resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "base.standard.v1";
    request.profileId = "base";
    request.resourceDirectories << temporary.path();
    auto document = ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    const QString originalLongValue = QStringLiteral("[\"armor_leather.iron\",\"armor_leather.iron\",\"armor_leather.iron\",\"armor_leather.iron\",\"armor_leather.iron\"]");
    QString setupDiagnostic;
    const ei::SDatabaseTable* setupMonsters = document->table("units.monsters");
    QVERIFY(setupMonsters);
    QString setupRow;
    for(const ei::SDatabaseRow& row : setupMonsters->rows)
        if(row.stableKey.startsWith("human test#"))
            setupRow = row.stableKey;
    QVERIFY(!setupRow.isEmpty());
    QVERIFY2(document->setValue("units.monsters", setupRow, "fld.32.0",
        originalLongValue, &setupDiagnostic), qPrintable(setupDiagnostic));

    ei::DatabaseEditorDialog dialog(document);
    QSignalSpy committed(&dialog,SIGNAL(transactionCommitted(QString,QString,QString)));
    dialog.show();
    QVERIFY(dialog.selectPrototype("Human Test"));
    QTableWidget* grid = dialog.findChild<QTableWidget*>("databaseGrid");
    QVERIFY(grid);
    QVERIFY(grid->currentRow() >= 0);

    bool copiedByRealMenu = false;
    bool cloneActionAvailable = false;
    QTimer::singleShot(0, [&copiedByRealMenu, &cloneActionAvailable]() {
        QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(!menu)
        {
            QTimer::singleShot(10, [&copiedByRealMenu, &cloneActionAvailable]() {
                QMenu* delayedMenu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if(!delayedMenu)
                    return;
                QAction* delayedCopy = nullptr;
                for(QAction* action : delayedMenu->actions()) {
                    if(action->objectName() == "databaseCloneRowAction")
                        cloneActionAvailable = action->isEnabled();
                    if(action->objectName() == "databaseCopyRowIdAction")
                        delayedCopy = action;
                }
                if(delayedCopy) { delayedCopy->trigger(); copiedByRealMenu = true; delayedMenu->close(); return; }
                delayedMenu->close();
            });
            return;
        }
        QAction* copyAction = nullptr;
        for(QAction* action : menu->actions()) {
            if(action->objectName() == "databaseCloneRowAction")
                cloneActionAvailable = action->isEnabled();
            if(action->objectName() == "databaseCopyRowIdAction")
                copyAction = action;
        }
        if(copyAction) { copyAction->trigger(); copiedByRealMenu = true; menu->close(); return; }
        menu->close();
    });
    const QRect rowCell = grid->visualItemRect(grid->item(grid->currentRow(), 0));
    const QPoint local = rowCell.center();
    QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, local,
        grid->viewport()->mapToGlobal(local));
    QApplication::sendEvent(grid->viewport(), &menuEvent);
    QTRY_VERIFY_WITH_TIMEOUT(copiedByRealMenu, 1000);
    QVERIFY(cloneActionAvailable);
    QApplication::processEvents();
    QCOMPARE(QApplication::clipboard()->text(), QString("Human Test"));

    int wearsColumn = -1;
    for(int column = 0; column < grid->columnCount(); ++column)
        if(grid->horizontalHeaderItem(column)->text().contains("Wears", Qt::CaseInsensitive))
            wearsColumn = column;
    QVERIFY(wearsColumn >= 0);
    QTableWidgetItem* wearsItem = grid->item(grid->currentRow(), wearsColumn);
    QVERIFY(wearsItem);
    QVERIFY(!wearsItem->data(Qt::UserRole + 2).toString().contains('['));
    QVERIFY(wearsItem->data(Qt::UserRole + 2).toString().contains('\n'));
    QVERIFY(!wearsItem->toolTip().contains(originalLongValue));
    grid->scrollToItem(wearsItem, QAbstractItemView::PositionAtCenter);
    grid->setCurrentCell(wearsItem->row(), wearsColumn);
    QApplication::processEvents();

    bool openedWrappedEditor = false;
    const QString edited = QStringLiteral("armor_leather.iron\narmor_leather.iron\narmor_leather.iron");
    QTimer::singleShot(0, [&openedWrappedEditor, &edited]() {
        QDialog* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!editor || editor->objectName() != "databaseFieldEditorDialog")
            for(QWidget* widget : QApplication::topLevelWidgets())
                if(widget->objectName() == "databaseFieldEditorDialog")
                    editor = qobject_cast<QDialog*>(widget);
        if(!editor)
            return;
        QPlainTextEdit* text = editor->findChild<QPlainTextEdit*>("databaseFieldEditorText");
        QLabel* count = editor->findChild<QLabel*>("databaseFieldEditorCount");
        QDialogButtonBox* buttons = editor->findChild<QDialogButtonBox*>("databaseFieldEditorButtons");
        openedWrappedEditor = true;
        QVERIFY(text);
        QVERIFY(buttons);
        QVERIFY(count);
        QCOMPARE(text->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QVERIFY(editor->width() <= 760);
        if(text)
            text->setPlainText(edited);
        if(count) {
            QVERIFY(count->text().contains(QStringLiteral("3")));
            QVERIFY(count->text().contains(QStringLiteral("item"), Qt::CaseInsensitive));
        }
        if(buttons)
            buttons->button(QDialogButtonBox::Ok)->click();
    });
    const QRect wearsRect = grid->visualItemRect(wearsItem);
    QVERIFY(wearsRect.isValid());
    QMetaObject::invokeMethod(grid, "cellDoubleClicked", Qt::DirectConnection,
        Q_ARG(int, wearsItem->row()), Q_ARG(int, wearsColumn));
    QApplication::processEvents();
    QVERIFY(openedWrappedEditor);
    const ei::SDatabaseTable* monsters = document->table("units.monsters");
    QVERIFY(monsters);
    bool storedCompleteValue = false;
    for(const ei::SDatabaseRow& row : monsters->rows)
        if(row.stableKey.startsWith("human test#"))
            storedCompleteValue = row.values.value("fld.32.0").toString() == QStringLiteral("[\"armor_leather.iron\",\"armor_leather.iron\",\"armor_leather.iron\"]");
    QVERIFY(storedCompleteValue);
    QVERIFY(document->canUndo());
    QVERIFY(!committed.isEmpty());
    QCOMPARE(committed.last().at(1).toString(),QString("units.monsters"));
    QCOMPARE(committed.last().at(2).toString(),QString("Human Test"));

    int probabilityColumn=-1;
    for(int column=0;column<grid->columnCount();++column)
        if(grid->horizontalHeaderItem(column)->text().contains("Common loot Probability",Qt::CaseInsensitive))
            probabilityColumn=column;
    QVERIFY(probabilityColumn>=0);
    QTableWidgetItem* probabilityItem=grid->item(grid->currentRow(),probabilityColumn);
    QVERIFY(probabilityItem);
    const QString readableProbability=probabilityItem->data(Qt::UserRole+2).toString();
    QVERIFY(!readableProbability.contains('{'));
    QVERIFY(!readableProbability.contains('"'));
    QVERIFY(readableProbability.contains("[0]"));
    QVERIFY(readableProbability.contains("= 100"));
    bool editedProbability=false;
    QTimer::singleShot(0,[&editedProbability](){QDialog*editor=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!editor)return;QPlainTextEdit*text=editor->findChild<QPlainTextEdit*>("databaseFieldEditorText");QDialogButtonBox*buttons=editor->findChild<QDialogButtonBox*>("databaseFieldEditorButtons");if(text){QStringList lines=text->toPlainText().split('\n');for(QString&line:lines)if(line.trimmed().startsWith("[0]")){line="[0] Common loot Probability = 75";editedProbability=true;break;}text->setPlainText(lines.join('\n'));}if(buttons)buttons->button(QDialogButtonBox::Ok)->click();});
    QMetaObject::invokeMethod(grid,"cellDoubleClicked",Qt::DirectConnection,Q_ARG(int,probabilityItem->row()),Q_ARG(int,probabilityColumn));
    QVERIFY(editedProbability);
    monsters=document->table("units.monsters");QVERIFY(monsters);bool probabilityStored=false;QString storedProbability;for(const auto&row:monsters->rows)if(row.stableKey.startsWith("human test#")){storedProbability=row.values.value("fld.27.0").toString();const QJsonArray value=QJsonDocument::fromJson(storedProbability.toUtf8()).array();probabilityStored=!value.isEmpty()&&value.first().toObject().value("value").toDouble()==75.0;}QVERIFY2(probabilityStored,qPrintable(storedProbability));

    const int changesBeforeCancel = document->changes().size();
    QVERIFY(dialog.selectPrototype("Human Test"));
    QTableWidgetItem* cancelItem = grid->item(grid->currentRow(), wearsColumn);
    QVERIFY(cancelItem);
    QTimer::singleShot(0, []() {
        QDialog* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!editor)
            return;
        QPlainTextEdit* text = editor->findChild<QPlainTextEdit*>("databaseFieldEditorText");
        QDialogButtonBox* buttons = editor->findChild<QDialogButtonBox*>("databaseFieldEditorButtons");
        if(text)
            text->setPlainText("cancelled mutation");
        if(buttons)
            buttons->button(QDialogButtonBox::Cancel)->click();
    });
    QMetaObject::invokeMethod(grid, "cellDoubleClicked", Qt::DirectConnection,
        Q_ARG(int, cancelItem->row()), Q_ARG(int, wearsColumn));
    QCOMPARE(document->changes().size(), changesBeforeCancel);
}

void ProfileDatabaseTests::installedDatabaseInteractiveLatency()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run the installed interaction latency control.");
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString sourcePath=QDir(temporary.path()).filePath("database.res");QVERIFY(QFile::copy(archives.first(),sourcePath));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="latency";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const ei::SDatabaseTable* monsters=document->table("units.monsters");QVERIFY(monsters&&!monsters->rows.isEmpty());const QString rowKey=monsters->rows.first().stableKey;QString diagnostic;QElapsedTimer timer;timer.start();QVERIFY2(document->setValue("units.monsters",rowKey,"fld.3.0",monsters->rows.first().values.value("fld.3.0").toInt()+1,&diagnostic),qPrintable(diagnostic));const qint64 setValueMs=timer.restart();const auto snapshot=document->currentSnapshot(&diagnostic);const qint64 snapshotMs=timer.restart();QVERIFY2(snapshot,qPrintable(diagnostic));
    ei::DatabaseEditorDialog dialog(document);dialog.show();timer.restart();dialog.refreshFromHistory();const qint64 refreshMs=timer.elapsed();qInfo()<<"database interaction latency ms set"<<setValueMs<<"snapshot"<<snapshotMs<<"refresh"<<refreshMs<<"rows"<<monsters->rows.size();QVERIFY2(setValueMs<100,qPrintable(QString("A simple database mutation took %1 ms; expected under 100 ms.").arg(setValueMs)));QVERIFY2(snapshotMs<500,qPrintable(QString("Runtime snapshot publication took %1 ms; expected under 500 ms.").arg(snapshotMs)));QVERIFY2(refreshMs<500,qPrintable(QString("A simple database refresh took %1 ms; expected under 500 ms.").arg(refreshMs)));
}

QTEST_MAIN(ProfileDatabaseTests)
#include "profile_database_tests.moc"

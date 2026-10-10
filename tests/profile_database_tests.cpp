#include <QtTest>
#include "verified_clipboard.h"
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtGui/private/qzipreader_p.h>
#include <QtGui/private/qzipwriter_p.h>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QSignalSpy>
#include <QSettings>
#include <QScrollBar>
#include <QTableWidget>
#include <QTableView>
#include <QDockWidget>
#include <QMainWindow>
#include <QComboBox>
#include <QAbstractItemDelegate>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <cmath>

#include "profile_database.h"
#include "database_map_catalog.h"
#include "database_document.h"
#include "database_workbook.h"
#include "database_schema.h"
#include "database_value_codec.h"
#include "lossless_database_member.h"
#include "res_archive_document.h"
#include "database_editor_dialog.h"
#include "item_spell_constructor.h"
#include "game_text_catalog.h"
#include "profile_database_provider_support.h"
#include "window_lifecycle.h"

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

void replaceZipPart(const QString& path,const QString& part,const QByteArray& replacement)
{
    QZipReader reader(path);QVERIFY(reader.exists());
    QBuffer package;QVERIFY(package.open(QIODevice::WriteOnly));
    QZipWriter writer(&package);writer.setCompressionPolicy(QZipWriter::AutoCompress);
    for(const auto& info:reader.fileInfoList())
    {
        if(info.isDir)writer.addDirectory(info.filePath);
        else if(info.isFile)writer.addFile(info.filePath,
            info.filePath==part?replacement:reader.fileData(info.filePath));
    }
    reader.close();writer.close();QCOMPARE(writer.status(),QZipWriter::NoError);
    write(path,package.data());
}

}

class ProfileDatabaseTests : public QObject
{
    Q_OBJECT
private slots:
    void parsesStableLinksAndCurrentNames();
    void mapPickerIncludesDatabaseLeversWithoutMapObjects();
    void databaseMapSelectionUsesFreshDefaultsAndRejectsUnsupportedEntries();
    void installedDatabaseMapCatalogUsesActiveDefinitions();
    void validatesBalancedItemGrammarWithoutMutation();
    void providersAreStrictAndJmvOverlaysOnlyActions();
    void jmvLoadsBoundedUnitPerkOwnership();
    void jmvNewUnitStatPadsToActiveRuntimeWidth();
    void jmvExistingUnitStatBlanksSerializeAsZero();
    void jmvAbsentLuaIdentityInitializesOnlyOnManualEdit();
    void installedSuppliedJmvUnitStatStagesAtRuntimeWidth();
    void jmvMonsterLuaDataClonesUndoesAndPublishesAtomically();
    void installedJmvTitanGripControl();
    void unknownProviderFailsClosed();
    void authoringDocumentPreservesBaselineAndTracksTransactions();
    void authoringDocumentBuildsExactNoOpAndUndoRestoresIt();
    void authoringDocumentSavePreservesOpaqueArchiveMembers();
    void authoringDocumentRejectsIdentityAndReferenceBreakage();
    void identityValidationUsesRuntimeLookupKeys();
    void authoringDocumentDetectsStaleSourceAndKeepsProfilesSeparate();
    void losslessMemberNoOpPreservesDuplicateOrderedAndUnknownTags();
    void losslessMemberPatchChangesOnlyRequestedOccurrence();
    void installedDatabaseMembersAreByteIdenticalOnNoOp();
    void installedDatabaseDocumentNativeRoundTrip();
    void installedWearsEditPreservesStructuralTagIdentity();
    void authoringDocumentPublishesCloneAndUndoRestoresOriginal();
    void workbookImportIsOneValidatedUndoableTransaction();
    void workbookRoundTripsClonedRowsWithoutSourceCollision();
    void nativeDatabaseAndLuaImportsAreValidatedUndoableTransactions();
    void jmvWorkbookCarriesLinkedLuaPayload();
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
    void databaseEditorOpensKnownResourceReferences();
    void databaseEditorMarksVerifiedLeverResources();
    void databaseEditorUsesLazyModelAndNavigatesReferences();
    void databaseEditorPersistsVisualColumnControls();
    void resourceNavigationPreservesDocking();
    void databaseEditorForwardsFocusedUndoRedo();
    void installedVerifiedReferenceMappingsResolve();
    void installedDatabaseInteractiveLatency();
    void installedRaceModelSelectionAndContextLatency();
    void itemSpellConstructorParsesCanonicalAndLegacyCodes();
    void itemSpellConstructorRestrictsMaterialsAndReportsSoftLimits();
    void itemSpellConstructorCalculatesRuntimeSpellValues();
    void itemSpellConstructorAppliesJmvItemRunePenalties();
    void itemSpellConstructorDoesNotTreatAuthoredCodesAsCraftingExceptions();
    void databaseValidationReportsInvalidConstructedItems();
    void itemSpellConstructorUsesGameTextKeys();
    void itemSpellConstructorNormalizesMaterialTextKeys();
    void databaseEditorExposesConstructorEntryPoint();
    void databaseEditorUsesCleanImportAndSchemaLabels();
    void itemSpellConstructorUsesVerifiedPreviewFamilies();
    void itemSpellConstructorPreviewPoliciesAreCategoryAware();
    void itemSpellConstructorDoesNotDirtySparseJmvDocument();
    void itemSpellConstructorUsesEffectiveJmvSpellCatalog();
    void installedConstructorCatalogCoverage();
    void installedConstructorImportsAgilityStaffIron();
    void gameTextCatalogComposesProfileOverlays();
};

QByteArray unitPerkLua(const QString& prototype, int strongMan)
{
    QStringList fields;
    fields << QString("\"%1\"").arg(prototype);
    for(int luaIndex = 2; luaIndex <= 44; ++luaIndex)
        fields << QString::number(luaIndex == 42 ? strongMan : 0);
    QStringList stats;
    stats << QString("\"%1\"").arg(prototype);
    for(int luaIndex = 2; luaIndex <= 21; ++luaIndex)
        stats << (luaIndex == 2 ? QString("100")
                  : luaIndex == 3 ? QString("10.5") : QString("0"));
    return QString("\xEF\xBB\xBF\nUnitPerk = {\n{%1,},\n}\n\nUnitStat = {\n{%2,},\n}\n")
        .arg(fields.join(','), stats.join(','))
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

void ProfileDatabaseTests::mapPickerIncludesDatabaseLeversWithoutMapObjects()
{
    auto members = relationalMembers();
    members.insert("levers.ldb", database({record(stringField(0, "test_lever")
        + stringField(7, "Test switch") + tag(3, QByteArray::fromHex("00000040")))}));
    const auto parsed = ei::parseProfileDatabaseMembers(members, "memory");
    // No map, selected node, or installed data participates in this lookup.
    QCOMPARE(parsed.namesByCategory.value("levers"), QStringList({"test_lever"}));
    QCOMPARE(parsed.levers.value("test_lever").text, QString("Test switch"));
    QCOMPARE(parsed.levers.value("test_lever").scale, 2.0f);
    QVERIFY(!ei::parseProfileDatabaseMembers(relationalMembers(), "memory")
                 .namesByCategory.contains("levers"));
}

void ProfileDatabaseTests::databaseMapSelectionUsesFreshDefaultsAndRejectsUnsupportedEntries()
{
    auto members = relationalMembers();
    const QByteArray race = stringField(0, "Test race") + intField(1, 52)
        + stringField(30, "testmodel") + tag(31, stringField(0, "skin_00"));
    const QByteArray monster = stringField(0, "Test creature") + stringField(1, "Test race")
        + intField(3, 12);
    members["units.udb"] = database({{}, record(race), record(monster)});
    ei::SProfileDatabaseSnapshot snapshot;
    ei::applyRelationalMembers(members, "memory", &snapshot);
    const auto catalog = ei::databaseMapCatalog(snapshot);
    QVERIFY(!catalog.isEmpty());
    const auto& creature = catalog.first();
    QCOMPARE(creature.unitType, 52);
    QCOMPARE(creature.primaryTexture, QString("testmodelskin_12"));
    QVERIFY(creature.unavailableReason.isEmpty());
    const QJsonObject defaults{{"World object", QJsonObject{
        {"Base object", QJsonObject{{"Node type", 7}}}}}, {"Logics", QJsonArray{}}};
    QJsonObject node;
    QVERIFY(ei::applyDatabaseMapEntry(creature, defaults, &node));
    QCOMPARE(node.value("Prototype name").toString(), QString("Test creature"));
    QCOMPARE(node.value("Subtype").toInt(), 52);
    QVERIFY(!node.value("Is use mob stats?").toBool());
    QVERIFY(!defaults.contains("Prototype name"));
    QVERIFY(!node.contains("_Endurance raw provenance"));
    // Original Jun Male Hero failure shape: a JMV humanoid DB row, skin 41,
    // becomes a fresh object with usable JMV skin and explicit base anatomy.
    auto humanSnapshot=snapshot;
    humanSnapshot.profileId="jamais-vu";
    humanSnapshot.raceModels["test race"].model="unhufe";
    humanSnapshot.monsters["test creature"].skinIndex=41;
    const auto human=ei::databaseMapCatalog(humanSnapshot).first();
    QCOMPARE(human.primaryTexture,QString("unhufeskin_41_x4"));
    QJsonObject humanNode;
    QVERIFY(ei::applyDatabaseMapEntry(human,defaults,&humanNode));
    const auto humanParts=humanNode.value("World object").toObject().value("Object parts").toArray();
    QCOMPARE(humanParts.size(),16);
    QVERIFY(humanParts.contains("bd") && humanParts.contains("hr") && humanParts.contains("rl3"));
    humanSnapshot.profileId="base-game";
    QCOMPARE(ei::databaseMapCatalog(humanSnapshot).first().primaryTexture,QString("unhufeskin_41"));
    humanSnapshot.raceModels["test race"].model="unorma";
    humanSnapshot.monsters["test creature"].hairIndex=7;
    const auto orc=ei::databaseMapCatalog(humanSnapshot).first();
    QCOMPARE(orc.bodyParts.size(),16);
    QVERIFY(orc.bodyParts.contains("hr.07") && !orc.bodyParts.contains("hr"));
    humanSnapshot.monsters["test creature"].hairIndex=-1;
    QCOMPARE(ei::databaseMapCatalog(humanSnapshot).first().bodyParts.size(),15);
    QVERIFY(creature.bodyParts.isEmpty()); // Integral models retain their own assembly.
    snapshot.monsters["test creature"].complexionX = 0;
    QVERIFY(ei::databaseMapCatalog(snapshot).first().unavailableReason.isEmpty());
    ei::SLeverDatabaseRecord lever;
    lever.name = "test_lever"; lever.scale = 4;
    snapshot.levers.insert(lever.name, lever);
    const auto leverEntry = ei::databaseMapCatalog(snapshot).last();
    const QJsonObject leverDefaults{{"World object", QJsonObject{{"Base object",
        QJsonObject{{"Node type", 0x23}, {"Complection", QJsonArray{1, 1, 1}}}}}}};
    QVERIFY(ei::applyDatabaseMapEntry(leverEntry, leverDefaults, &node));
    QCOMPARE(node.value("World object").toObject().value("Base object").toObject()
             .value("Complection").toArray(), QJsonArray({1, 1, 1}));
    for(const auto& item : catalog)
        if(item.category == ei::EDatabaseMapCategory::Item)
        {
              QVERIFY(item.unavailableReason.isEmpty());
              QVERIFY(!ei::applyDatabaseMapEntry(item, defaults, &node));
              QVERIFY(node.isEmpty());
              auto resolved = item;
              resolved.model = "initwesw0weapon";
              resolved.primaryTexture = " unhumadg_03.br.1.x2.MMP ";
              const QJsonObject worldDefaults{{"Base object", QJsonObject{{"Node type", 3}}}};
              QVERIFY(ei::applyDatabaseMapEntry(resolved, worldDefaults, &node));
              QCOMPARE(node.value("Model name").toString(), resolved.model);
              QCOMPARE(node.value("Primary texture").toString(), QString("unhumadg_03.br.1.x2map"));
              QCOMPARE(node.value("Secondary texture").toString(), QString("unhumadg_03.br.1.x2map"));
              resolved.primaryTexture = "unhumadg_03.br.1.x2map.mmp";
              QVERIFY(ei::applyDatabaseMapEntry(resolved, worldDefaults, &node));
              QCOMPARE(node.value("Primary texture").toString(), QString("unhumadg_03.br.1.x2map"));
              resolved.primaryTexture = "";
              QVERIFY(!ei::applyDatabaseMapEntry(resolved, worldDefaults, &node));
              resolved.primaryTexture = "inventory_texture";
              QVERIFY(ei::applyDatabaseMapEntry(resolved, worldDefaults, &node));
              QCOMPARE(node.value("Primary texture").toString(), QString("inventory_texturemap"));
              QVERIFY(!node.contains("World object"));
        }
    snapshot.raceModels.clear();
    QVERIFY(!ei::databaseMapCatalog(snapshot).first().unavailableReason.isEmpty());
}

void ProfileDatabaseTests::installedDatabaseMapCatalogUsesActiveDefinitions()
{
    const auto archives = qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';', QString::SkipEmptyParts);
    if(archives.isEmpty()) QSKIP("Set EI_TEST_DATABASE_ARCHIVES for read-only installed catalog coverage.");
    for(const QString& archive : archives)
    {
        QVector<ei::SDatabaseDiagnostic> diagnostics;
        const auto members = ei::readDatabaseArchive(archive, &diagnostics);
        ei::SProfileDatabaseSnapshot snapshot;
        ei::applyRelationalMembers(members, "installed-test", &snapshot);
        QVERIFY(snapshot.usable());
        const auto entries = ei::databaseMapCatalog(snapshot);
        QCOMPARE(entries.size(), snapshot.monsters.size() + snapshot.items.size() + snapshot.levers.size());
        int creatures = 0, levers = 0;
        for(const auto& entry : entries)
        {
            if(entry.category == ei::EDatabaseMapCategory::Creature && entry.unavailableReason.isEmpty())
            {
                ++creatures;
                QCOMPARE(entry.unitType, snapshot.raceModels.value(
                    snapshot.monsters.value(entry.key).baseRace.toLower()).typeId);
                QVERIFY(!entry.model.isEmpty()); QVERIFY(!entry.primaryTexture.isEmpty());
            }
            if(entry.category == ei::EDatabaseMapCategory::Lever) ++levers;
        }
        QVERIFY(creatures > 0); QVERIFY(levers > 0);
        qInfo("Catalog entries: %d; creatures with creation metadata: %d; levers: %d",
              entries.size(), creatures, levers);
    }
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
    QCOMPARE(snapshot->jmvUnitStatsByPrototype.value("human test").values.value(0), 100.0);
    QCOMPARE(snapshot->jmvUnitStatsByPrototype.value("human test").values.value(1), 10.5);

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
    QVERIFY(!snapshot->jmvUnitStatsByPrototype.isEmpty());
    QFile tables(snapshot->jmvUnitPerkSourcePath);
    QVERIFY(tables.open(QIODevice::ReadOnly));
    const QByteArray source = tables.readAll();
    QString diagnostic;
    const QByteArray perks = ei::rewriteJmvUnitPerkTable(
        source, snapshot->jmvUnitPerksByPrototype, &diagnostic);
    QCOMPARE(ei::rewriteJmvUnitStatTable(
                 perks, snapshot->jmvUnitStatsByPrototype, &diagnostic), source);
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

void ProfileDatabaseTests::jmvNewUnitStatPadsToActiveRuntimeWidth()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));
    QVERIFY(root.mkpath("mod/lua"));
    write(root.filePath("mod/res/database.res"), resArchive(relationalMembers()));
    write(root.filePath("mod/res/databaseJmv.res"), resArchive({}));
    write(root.filePath("mod/lua/tables.lua"), unitPerkLua("Lua Width Template", 0));

    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "jmv.v1";
    request.profileId = "jamais-vu";
    request.runtimeManifestId = "jmv-unit-stat-width";
    request.resourceDirectories << root.filePath("mod/res");
    auto document = ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);

    const ei::SDatabaseTable* monsters = document->table("units.monsters");
    QVERIFY(monsters);
    QString sourceRow;
    for(const auto& row : monsters->rows)
        if(row.sourceStableKey.compare("Human Test", Qt::CaseInsensitive) == 0)
            sourceRow = row.stableKey;
    QVERIFY(!sourceRow.isEmpty());

    QString diagnostic;
    QVERIFY2(document->cloneRow("units.monsters", sourceRow,
                                "Human Partial Lua Clone", &diagnostic),
             qPrintable(diagnostic));
    const QString cloneRow = document->table("units.monsters")->rows.last().stableKey;
    const QVector<QPair<QString, QVariant>> entered = {
        {"jmv.stat.experience", 10000},
        {"jmv.stat.strength", 30.0},
        {"jmv.stat.dexterity", 25.0},
        {"jmv.stat.intelligence", 15.0},
        {"jmv.stat.criticalChance", 0.0}
    };
    for(const auto& value : entered)
        QVERIFY2(document->setValue("units.monsters", cloneRow,
                                    value.first, value.second, &diagnostic),
                 qPrintable(diagnostic));

    const auto staged = document->currentSnapshot(&diagnostic);
    QVERIFY2(staged, qPrintable(diagnostic));
    const auto record = staged->jmvUnitStatsByPrototype.value("human partial lua clone");
    QCOMPARE(record.values.size(), 20);
    QCOMPARE(record.values.value(0), 10000.0);
    QCOMPARE(record.values.value(4), 0.0);
    QCOMPARE(record.values.value(5), 0.0);
    for(int index = 6; index < record.values.size(); ++index)
        QCOMPARE(record.values.at(index), 0.0);
}

void ProfileDatabaseTests::jmvExistingUnitStatBlanksSerializeAsZero()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));QVERIFY(root.mkpath("mod/lua"));
    write(root.filePath("mod/res/database.res"),resArchive(relationalMembers()));
    QByteArray lua=unitPerkLua("Human Test",0);
    const int statStart=lua.indexOf("UnitStat = {");QVERIFY(statStart>=0);
    lua=lua.left(statStart)+QByteArray("UnitStat = {\n{\"Human Test\",100,10.5,},\n}\n");
    write(root.filePath("mod/lua/tables.lua"),lua);
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="jamais-vu";
    request.resourceDirectories<<root.filePath("mod/res");
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    QString diagnostic;const QByteArray normalized=document->buildTablesLua(&diagnostic);
    QVERIFY2(!normalized.isEmpty(),qPrintable(diagnostic));
    QMap<QString,ei::SJmvUnitPerkRecord> perks;QMap<QString,ei::SJmvUnitStatRecord> stats;
    QVERIFY2(ei::parseJmvUnitTables(normalized,&perks,&stats,&diagnostic),qPrintable(diagnostic));
    const auto record=stats.value("human test");
    QCOMPARE(record.values.size(),20);
    QCOMPARE(record.values.at(0),100.0);QCOMPARE(record.values.at(1),10.5);
    for(int index=2;index<record.values.size();++index)QCOMPARE(record.values.at(index),0.0);
}

void ProfileDatabaseTests::jmvAbsentLuaIdentityInitializesOnlyOnManualEdit()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));QVERIFY(root.mkpath("mod/lua"));
    write(root.filePath("mod/res/database.res"),resArchive(relationalMembers()));
    write(root.filePath("mod/res/databaseJmv.res"),resArchive({}));
    const QByteArray originalLua=unitPerkLua("Lua Template",1);
    write(root.filePath("mod/lua/tables.lua"),originalLua);
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="jamais-vu";
    request.runtimeManifestId="jmv-absent-lua-identity";
    request.resourceDirectories<<root.filePath("mod/res");
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    QString diagnostic;

    QVERIFY2(document->fieldEditable("monsters","human test","jmv.perk.health",&diagnostic),
             qPrintable(diagnostic));
    QVERIFY2(document->fieldEditable("monsters","human test","jmv.stat.strength",&diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(document->buildTablesLua(&diagnostic),originalLua);

    QVERIFY2(document->cloneRow("monsters","human test","Human No Lua Clone",&diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(document->buildTablesLua(&diagnostic),originalLua);
    QMap<QString,ei::SJmvUnitPerkRecord> perks;QMap<QString,ei::SJmvUnitStatRecord> stats;
    QVERIFY2(ei::parseJmvUnitTables(document->buildTablesLua(&diagnostic),
             &perks,&stats,&diagnostic),qPrintable(diagnostic));
    QVERIFY(!perks.contains("human no lua clone"));
    QVERIFY(!stats.contains("human no lua clone"));

    QVERIFY2(document->setValue("monsters","human no lua clone",
             "jmv.perk.health",7,&diagnostic),qPrintable(diagnostic));
    QVERIFY2(ei::parseJmvUnitTables(document->buildTablesLua(&diagnostic),
             &perks,&stats,&diagnostic),qPrintable(diagnostic));
    QCOMPARE(perks.value("human no lua clone").values.value(13),7);
    QVERIFY(!stats.contains("human no lua clone"));
    QVERIFY(document->undo());
    QCOMPARE(document->buildTablesLua(&diagnostic),originalLua);

    QVERIFY2(document->setValue("monsters","human test",
             "jmv.stat.strength",12.5,&diagnostic),qPrintable(diagnostic));
    QVERIFY2(ei::parseJmvUnitTables(document->buildTablesLua(&diagnostic),
             &perks,&stats,&diagnostic),qPrintable(diagnostic));
    QVERIFY(!perks.contains("human test"));
    QCOMPARE(stats.value("human test").values.value(1),12.5);
}

void ProfileDatabaseTests::installedSuppliedJmvUnitStatStagesAtRuntimeWidth()
{
    const QString database=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").section(';',0,0);
    const QString tables=qEnvironmentVariable("EI_TEST_JMV_TABLES_LUA");
    if(database.isEmpty()||tables.isEmpty())QSKIP("Set supplied database and tables.lua paths.");
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));QVERIFY(root.mkpath("mod/lua"));
    QVERIFY(QFile::copy(database,root.filePath("mod/res/database.res")));
    QVERIFY(QFile::copy(tables,root.filePath("mod/lua/tables.lua")));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="supplied-jmv";
    request.resourceDirectories<<root.filePath("mod/res");
    const auto loaded=service.snapshot(request);
    auto document=ei::DatabaseDocument::fromSnapshot(loaded);QVERIFY(document);
    const QString prototype="Human Poselok Peasant F01 1";
    const auto* monsters=document->table("units.monsters");QVERIFY(monsters);
    QString rowKey,lastStatField;int statWidth=0;
    for(const auto& field:monsters->fields)if(field.storageId==-3){lastStatField=field.key;++statWidth;}
    for(const auto& row:monsters->rows)if(row.values.value("fld.0.0").toString()==prototype)rowKey=row.stableKey;
    QVERIFY(!rowKey.isEmpty());QVERIFY(statWidth>5);QVERIFY(!lastStatField.isEmpty());
    QString diagnostic;QVERIFY2(document->setValue("units.monsters",rowKey,lastStatField,0.0,&diagnostic),qPrintable(diagnostic));
    const QByteArray stagedLua=document->buildTablesLua(&diagnostic);QVERIFY2(!stagedLua.isEmpty(),qPrintable(diagnostic));
    const auto snapshot=document->currentSnapshot(&diagnostic);QVERIFY2(snapshot,qPrintable(diagnostic));
    const auto record=snapshot->jmvUnitStatsByPrototype.value(prototype.toLower());
    QCOMPARE(record.values.size(),statWidth);
    QVERIFY(root.mkpath("stage"));
    QVERIFY2(document->saveBundle(root.filePath("stage/database.res"),root.filePath("stage/tables.lua"),&diagnostic),qPrintable(diagnostic));
    QFile output(root.filePath("stage/tables.lua"));QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains(prototype.toUtf8()));
    const QString retainedStage=qEnvironmentVariable("EI_TEST_JMV_STAGE_OUTPUT");
    if(!retainedStage.isEmpty())
    {
        QVERIFY(QDir().mkpath(retainedStage));
        QVERIFY2(document->saveBundle(QDir(retainedStage).filePath("database.res"),
            QDir(retainedStage).filePath("tables.lua"),&diagnostic),qPrintable(diagnostic));
    }
}

void ProfileDatabaseTests::jmvMonsterLuaDataClonesUndoesAndPublishesAtomically()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));
    QVERIFY(root.mkpath("mod/lua"));
    const QString databasePath = root.filePath("mod/res/database.res");
    const QString luaPath = root.filePath("mod/lua/tables.lua");
    const QByteArray originalDatabase = resArchive(relationalMembers());
    const QByteArray originalLua = unitPerkLua("Human Test", 1);
    write(databasePath, originalDatabase);
    write(root.filePath("mod/res/databaseJmv.res"), resArchive({}));
    write(luaPath, originalLua);

    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId = "jmv.v1";
    request.profileId = "jamais-vu";
    request.runtimeManifestId = "jmv-lua-authoring";
    request.resourceDirectories << root.filePath("mod/res");
    auto document = ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    QString diagnostic;
    QCOMPARE(document->buildTablesLua(&diagnostic), originalLua);
    QVERIFY2(document->cloneRow("monsters", "human test", "Human Concise Lua Clone",
                                &diagnostic), qPrintable(diagnostic));
    const QByteArray conciseCloneLua=document->buildTablesLua(&diagnostic);
    QVERIFY(conciseCloneLua.contains("\"Human Concise Lua Clone\""));
    QMap<QString,ei::SJmvUnitPerkRecord> concisePerks;
    QMap<QString,ei::SJmvUnitStatRecord> conciseStats;
    QVERIFY2(ei::parseJmvUnitTables(conciseCloneLua,&concisePerks,&conciseStats,&diagnostic),
             qPrintable(diagnostic));
    QVERIFY(concisePerks.contains("human test"));
    QVERIFY(conciseStats.contains("human test"));
    QVERIFY(concisePerks.contains("human concise lua clone"));
    QVERIFY(conciseStats.contains("human concise lua clone"));
    QVERIFY(document->undo());
    QCOMPARE(document->buildTablesLua(&diagnostic), originalLua);

    const ei::SDatabaseTable* monsters = document->table("units.monsters");
    QVERIFY(monsters);
    QString sourceRow;
    for(const auto& row : monsters->rows)
        if(row.sourceStableKey.compare("Human Test", Qt::CaseInsensitive) == 0)
            sourceRow = row.stableKey;
    QVERIFY(!sourceRow.isEmpty());
    bool hasHealth = false, hasStrongMan = false, hasStrengthStat = false;
    for(const auto& field : monsters->fields)
    {
        hasHealth |= field.key == "jmv.perk.health" && field.writable;
        hasStrongMan |= field.key == "jmv.perk.strongMan" && field.writable;
        if(field.key == "jmv.perk.strength")
            QCOMPARE(field.label, QString::fromUtf8("JMV Lua [13] — Perk: Strength"));
        if(field.key == "jmv.stat.strength")
        {
            hasStrengthStat = field.writable;
            QCOMPARE(field.label, QString::fromUtf8("JMV Lua [3] — Unit stat: Strength"));
            QCOMPARE(field.type, ei::EDatabaseFieldType::Decimal);
        }
    }
    QVERIFY(hasHealth);
    QVERIFY(hasStrongMan);
    QVERIFY(hasStrengthStat);

    QVERIFY2(document->cloneRow("units.monsters", sourceRow, "Human Lua Clone", &diagnostic),
             qPrintable(diagnostic));
    monsters = document->table("units.monsters");
    QString cloneRow;
    for(const auto& row : monsters->rows)
        if(row.values.value("fld.0.0").toString() == "Human Lua Clone")
        {
            cloneRow = row.stableKey;
            QCOMPARE(row.values.value("jmv.perk.strongMan").toInt(), 1);
            QCOMPARE(row.values.value("jmv.stat.strength").toDouble(), 10.5);
        }
    QVERIFY(!cloneRow.isEmpty());
    QVERIFY2(document->setValue("units.monsters", cloneRow, "jmv.perk.health", 9,
                                &diagnostic), qPrintable(diagnostic));
    QVERIFY(document->undo());
    QCOMPARE(document->table("units.monsters")->rows.last().values
             .value("jmv.perk.health").toInt(), 0);
    QVERIFY(document->undo());
    for(const auto& row : document->table("units.monsters")->rows)
        QVERIFY(row.values.value("fld.0.0").toString() != "Human Lua Clone");
    QVERIFY(document->redo());
    QVERIFY(document->redo());
    QVERIFY2(document->setValue("units.monsters", cloneRow, "jmv.stat.strength", 33.5,
                                &diagnostic), qPrintable(diagnostic));
    QVERIFY(document->undo());
    QCOMPARE(document->table("units.monsters")->rows.last().values
             .value("jmv.stat.strength").toDouble(), 10.5);
    QVERIFY(document->redo());

    QVERIFY(root.mkpath("stage"));
    const QString stagedDatabase=root.filePath("stage/database.res");
    const QString stagedLua=root.filePath("stage/tables.lua");
    QVERIFY(!document->saveArchive(stagedDatabase,&diagnostic));
    QVERIFY(diagnostic.contains("tables.lua",Qt::CaseInsensitive));
    QVERIFY2(document->saveBundle(stagedDatabase,stagedLua,&diagnostic),qPrintable(diagnostic));
    QFile stagedLuaFile(stagedLua);QVERIFY(stagedLuaFile.open(QIODevice::ReadOnly));
    QVERIFY(stagedLuaFile.readAll().contains("\"Human Lua Clone\""));

    QVERIFY2(document->applyToSource(&diagnostic), qPrintable(diagnostic));
    QFile database(databasePath), lua(luaPath);
    QVERIFY(database.open(QIODevice::ReadOnly));
    QVERIFY(lua.open(QIODevice::ReadOnly));
    const QByteArray publishedDatabase = database.readAll();
    const QByteArray publishedLua = lua.readAll();
    QVERIFY(publishedDatabase != originalDatabase);
    QVERIFY(publishedLua != originalLua);
    QVERIFY(publishedLua.contains("\"Human Lua Clone\""));

    ei::ProfileDatabaseService reopenedService;
    const auto reopened = reopenedService.snapshot(request);
    QVERIFY2(reopened->jmvUnitPerkTableStatus == ei::EJmvUnitPerkTableStatus::Valid,
             qPrintable(reopened->jmvUnitPerkDiagnostic));
    QCOMPARE(reopened->jmvStrongManByPrototype.value("human lua clone"), 1);
    QCOMPARE(reopened->jmvUnitPerksByPrototype.value("human lua clone").values.value(13), 9);
    QCOMPARE(reopened->jmvUnitStatsByPrototype.value("human lua clone").values.value(1), 33.5);

    QVERIFY(document->undo());
    write(luaPath, publishedLua + "-- external edit\n");
    const QByteArray databaseBeforeRejectedApply = publishedDatabase;
    QVERIFY(!document->applyToSource(&diagnostic));
    QVERIFY(diagnostic.contains("tables.lua", Qt::CaseInsensitive));
    database.close();
    QVERIFY(database.open(QIODevice::ReadOnly));
    QCOMPARE(database.readAll(), databaseBeforeRejectedApply);
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

void ProfileDatabaseTests::identityValidationUsesRuntimeLookupKeys()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    auto members=relationalMembers();
    const QByteArray spellA=stringField(0,"Fire A")+stringField(1,"fire");
    const QByteArray spellB=stringField(0,"Fire B")+stringField(1,"fire");
    const QByteArray templateRow=stringField(1,"fire")+tag(2,stringField(0,"e1"));
    members["spells.sdb"]=database({record(spellA)+record(spellB),QByteArray(),
                                     record(templateRow)+record(templateRow),
                                     QByteArray(),QByteArray()});
    write(QDir(temporary.path()).filePath("database.res"),resArchive(members));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const auto* prototypes=document->table("spells.spellprototypes");QVERIFY(prototypes);
    QCOMPARE(prototypes->identityFieldId,1);QVERIFY(prototypes->uniqueIdentity);
    const auto* templates=document->table("spells.s_spells");QVERIFY(templates);
    QVERIFY(!templates->uniqueIdentity);
    int prototypeDuplicates=0,templateDuplicates=0;
    for(const auto& finding:document->validate())if(finding.code=="database.identity.invalid")
    {prototypeDuplicates+=finding.tableKey=="spells.spellprototypes";templateDuplicates+=finding.tableKey=="spells.s_spells";}
    QCOMPARE(prototypeDuplicates,1);
    QCOMPARE(templateDuplicates,0);
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
        CResArchiveDocument sourceLayout,editedLayout;QStringList layoutErrors;
        QVERIFY2(sourceLayout.loadBytes(sourceBytes,sourcePath,&layoutErrors),qPrintable(layoutErrors.join('\n')));layoutErrors.clear();
        QVERIFY2(editedLayout.loadBytes(edited,"edited",&layoutErrors),qPrintable(layoutErrors.join('\n')));
        const int sourceUnitsIndex=sourceLayout.uniqueMemberIndex("units.udb"),editedUnitsIndex=editedLayout.uniqueMemberIndex("units.udb");
        QVERIFY(sourceUnitsIndex>=0);QVERIFY(editedUnitsIndex>=0);
        const auto sourceUnits=sourceLayout.members().at(sourceUnitsIndex),editedUnits=editedLayout.members().at(editedUnitsIndex);
        QCOMPARE(editedLayout.tableOffset(),quint32(qint64(sourceLayout.tableOffset())+qint64(editedUnits.dataSize)-sourceUnits.dataSize));
        QCOMPARE(editedUnits.dataOffset,sourceUnits.dataOffset);
        for(const auto& member:editedLayout.members())QVERIFY2(quint64(member.dataOffset)+member.dataSize<=editedLayout.tableOffset(),qPrintable(member.name));
        const auto originalUnits=ei::LosslessDatabaseMember::parse(sourceLayout.memberPayload("units.udb"));
        const auto publishedUnits=ei::LosslessDatabaseMember::parse(editedLayout.memberPayload("units.udb"));
        const int wolfRecord=originalUnits.findRecordByFieldPayload(2,0,QByteArray("WolfGreyF10\0",12));QVERIFY(wolfRecord>=0);
        const auto originalWolf=originalUnits.fields(2,wolfRecord),publishedWolf=publishedUnits.fields(2,wolfRecord);QCOMPARE(publishedWolf.size(),originalWolf.size());for(int field=0;field<originalWolf.size();++field)QCOMPARE(publishedWolf.at(field).encoded,originalWolf.at(field).encoded);
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

void ProfileDatabaseTests::workbookRoundTripsClonedRowsWithoutSourceCollision()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    QString diagnostic;QMap<QString,ei::SDatabaseTable> tables;
    for(const QString& key:document->tableKeys())tables.insert(key,*document->table(key));
    auto& physical=tables["units.monsters"];QVERIFY(!physical.rows.isEmpty());
    ei::SDatabaseRow clone=physical.rows.first();clone.added=true;clone.deleted=false;
    clone.values["fld.0.0"]="Human Workbook Clone";
    clone.stableKey="human workbook clone#new";physical.rows<<clone;
    QVERIFY2(document->replaceTables(tables,"Synthetic workbook clone",&diagnostic),
             qPrintable(diagnostic));
    const QString workbook=QDir(temporary.path()).filePath("database.xlsx");
    QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));
    replaceZipPart(workbook,"eidbeditor/sheets.sha256",QByteArray(64,'0'));

    ei::SDatabaseWorkbookImport imported;
    QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),
             qPrintable(diagnostic));
    int originals=0,clones=0;
    for(const auto& row:imported.tables.value("units.monsters").rows)
    {
        if(row.deleted)continue;
        const QString identity=row.values.value("fld.0.0").toString();
        originals+=identity==QStringLiteral("Human Test");
        clones+=identity==QStringLiteral("Human Workbook Clone");
    }
    QCOMPARE(originals,1);
    QCOMPARE(clones,1);
}

void ProfileDatabaseTests::nativeDatabaseAndLuaImportsAreValidatedUndoableTransactions()
{
    QTemporaryDir base;QVERIFY(base.isValid());
    const QString basePath=QDir(base.path()).filePath("database.res");
    write(basePath,resArchive(relationalMembers()));
    ei::ProfileDatabaseService baseService;ei::SProfileDatabaseLoadRequest baseRequest;
    baseRequest.providerId="base.standard.v1";baseRequest.profileId="base";baseRequest.resourceDirectories<<base.path();
    auto baseDocument=ei::DatabaseDocument::fromSnapshot(baseService.snapshot(baseRequest));QVERIFY(baseDocument);
    auto changedMembers=relationalMembers();
    changedMembers["units.udb"].replace(intField(3,2),intField(3,7));
    QString diagnostic;
    const QByteArray changedArchive=resArchive(changedMembers);
    QVERIFY2(baseDocument->importNativeBundle(changedArchive,QByteArray(),"Imported database.res",&diagnostic),qPrintable(diagnostic));
    QCOMPARE(baseDocument->table("units.monsters")->rows.first().values.value("fld.3.0").toInt(),7);
    QCOMPARE(baseDocument->buildArchive(&diagnostic),changedArchive);
    QVERIFY(baseDocument->undo());
    QCOMPARE(baseDocument->table("units.monsters")->rows.first().values.value("fld.3.0").toInt(),2);
    QVERIFY2(baseDocument->cloneRow("units.monsters",baseDocument->table("units.monsters")->rows.first().stableKey,"Native Clone",&diagnostic),qPrintable(diagnostic));
    const QByteArray clonedArchive=baseDocument->buildArchive(&diagnostic);QVERIFY2(!clonedArchive.isEmpty(),qPrintable(diagnostic));
    QVERIFY(baseDocument->undo());
    QVERIFY2(baseDocument->importNativeBundle(clonedArchive,QByteArray(),"Reimported cloned database.res",&diagnostic),qPrintable(diagnostic));
    bool foundClone=false;for(const auto& row:baseDocument->table("units.monsters")->rows)foundClone|=!row.deleted&&row.values.value("fld.0.0").toString()=="Native Clone";
    QVERIFY(foundClone);QCOMPARE(baseDocument->buildArchive(&diagnostic),clonedArchive);
    QVERIFY(baseDocument->undo());

    QTemporaryDir jmv;QVERIFY(jmv.isValid());QVERIFY(QDir(jmv.path()).mkpath("mod/Res"));QVERIFY(QDir(jmv.path()).mkpath("mod/lua"));
    write(jmv.filePath("mod/Res/database.res"),resArchive(relationalMembers()));
    write(jmv.filePath("mod/lua/tables.lua"),unitPerkLua("Human Test",1));
    ei::ProfileDatabaseService jmvService;ei::SProfileDatabaseLoadRequest jmvRequest;
    jmvRequest.providerId="jmv.v1";jmvRequest.profileId="jamais-vu";jmvRequest.resourceDirectories<<jmv.filePath("mod/Res");
    auto jmvDocument=ei::DatabaseDocument::fromSnapshot(jmvService.snapshot(jmvRequest));QVERIFY(jmvDocument);
    QVERIFY2(jmvDocument->importNativeBundle(QByteArray(),unitPerkLua("Human Test",0),"Imported tables.lua",&diagnostic),qPrintable(diagnostic));
    const auto* monsters=jmvDocument->table("units.monsters");QVERIFY(monsters);
    QString rowKey;for(const auto& row:monsters->rows)if(row.sourceStableKey=="Human Test")rowKey=row.stableKey;
    QVERIFY(!rowKey.isEmpty());
    QCOMPARE(jmvDocument->table("units.monsters")->rows.first().values.value("jmv.perk.strongMan").toInt(),0);
    QVERIFY(jmvDocument->undo());
    QCOMPARE(jmvDocument->table("units.monsters")->rows.first().values.value("jmv.perk.strongMan").toInt(),1);
}

void ProfileDatabaseTests::jmvWorkbookCarriesLinkedLuaPayload()
{
    QTemporaryDir root;QVERIFY(root.isValid());QVERIFY(QDir(root.path()).mkpath("mod/Res"));QVERIFY(QDir(root.path()).mkpath("mod/lua"));
    auto members=relationalMembers();
    members["spells.sdb"]=database({record(stringField(0,"Weaken")
        +stringField(1,"weak")+stringField(2,"acid")
        +tag(10,QByteArray::fromHex("00004041")))});
    write(root.filePath("mod/Res/database.res"),resArchive(members));
    write(root.filePath("mod/lua/tables.lua"),unitPerkLua("Human Test",1)
        +QByteArray("\nSpellAdds = { {\"weak\", 20.0}, }\n"));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="jamais-vu";request.resourceDirectories<<root.filePath("mod/Res");
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    QString diagnostic;const QString workbook=root.filePath("database.xlsx");
    QVERIFY2(ei::DatabaseWorkbook::exportXlsx(*document,workbook,&diagnostic),qPrintable(diagnostic));
    const QString retained=qEnvironmentVariable("EI_TEST_RETAIN_JMV_XLSX");
    if(!retained.isEmpty()){QFile::remove(retained);QVERIFY2(QFile::copy(workbook,retained),qPrintable(retained));}
    QCOMPARE(ei::DatabaseWorkbook::embeddedTablesLua(workbook),document->buildTablesLua(&diagnostic));
    QZipReader zip(workbook);QVERIFY(zip.exists());
    QVERIFY(zip.fileData("[Content_Types].xml").contains("Extension=\"lua\""));
    QVERIFY(zip.fileData("ei-mapper/_rels/manifest.txt.rels").contains("Target=\"tables.lua\""));
    const QByteArray shared=zip.fileData("xl/sharedStrings.xml");
    QVERIFY(shared.contains("JMV Lua [42]"));
    QVERIFY(shared.contains("JMV:jmv.perk.strongMan"));
    QByteArray monsterSheet;QString monsterSheetPath;
    int sharedIndex=-1,currentIndex=-1;QXmlStreamReader stringReader(shared);
    while(!stringReader.atEnd())
    {
        stringReader.readNext();
        if(stringReader.isStartElement()&&stringReader.name()=="si")++currentIndex;
        else if(stringReader.isStartElement()&&stringReader.name()=="t"
                &&stringReader.readElementText()==QStringLiteral("JMV:jmv.perk.strongMan"))
            sharedIndex=currentIndex;
    }
    QVERIFY(sharedIndex>=0);
    const QRegularExpression header(QStringLiteral(
        "<c r=\"([A-Z]+)2\" t=\"s\"><v>%1</v></c>").arg(sharedIndex));
    QRegularExpressionMatch headerMatch;QString sheetText;
    for(int index=1;index<=ei::workbookTableKeys(*document).size();++index)
    {
        const QString candidate=QStringLiteral("xl/worksheets/sheet%1.xml").arg(index);
        const QByteArray bytes=zip.fileData(candidate);
        const auto match=header.match(QString::fromUtf8(bytes));
        if(match.hasMatch()){monsterSheetPath=candidate;monsterSheet=bytes;headerMatch=match;break;}
    }
    zip.close();QVERIFY(!monsterSheetPath.isEmpty());sheetText=QString::fromUtf8(monsterSheet);
    const QString cell=headerMatch.captured(1)+QStringLiteral("3");
    const QRegularExpression value(QStringLiteral(
        "<c r=\"%1\"><v>[^<]*</v></c>").arg(cell));
    QVERIFY(value.match(sheetText).hasMatch());
    sheetText.replace(value,QStringLiteral("<c r=\"%1\"><v>7</v></c>").arg(cell));
    replaceZipPart(workbook,monsterSheetPath,sheetText.toUtf8());
    ei::SDatabaseWorkbookImport imported;
    QVERIFY2(ei::DatabaseWorkbook::importXlsx(*document,workbook,&imported,&diagnostic),
             qPrintable(diagnostic));
    QCOMPARE(imported.tables.value("units.monsters").rows.first()
                 .values.value("jmv.perk.strongMan").toInt(),7);
    QVERIFY2(document->replaceTablesWithLua(imported.tables,
                 ei::DatabaseWorkbook::embeddedTablesLua(workbook),
                 QStringLiteral("Imported workbook"),&diagnostic,true),qPrintable(diagnostic));
    QCOMPARE(document->table("units.monsters")->rows.first()
                 .values.value("jmv.perk.strongMan").toInt(),7);
    const auto importedSnapshot=document->currentSnapshot(&diagnostic);
    QVERIFY2(importedSnapshot,qPrintable(diagnostic));
    QCOMPARE(importedSnapshot->jmvStrongManByPrototype.value("human test"),7);
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
    QCOMPARE(zip.fileData("eidbeditor/cells.sha256").trimmed().size(),64);
    const QByteArray contentTypes=zip.fileData("[Content_Types].xml");
    QVERIFY2(contentTypes.contains("Extension=\"sha256\""),"The fingerprint part has no OPC content type");
    const QByteArray packageRelationships=zip.fileData("_rels/.rels");
    for(const QByteArray& target:{QByteArray("ei-mapper/manifest.txt"),QByteArray("ei-mapper/source.res"),QByteArray("eidbeditor/source.res"),QByteArray("eidbeditor/sheets.sha256"),QByteArray("eidbeditor/cells.sha256")})
        QVERIFY2(packageRelationships.contains("Target=\""+target+"\""),target.constData());
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
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString path=QDir(temporary.path()).filePath("database.res");write(path,resArchive(relationalMembers()));ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);QVERIFY(!document->identityAvailable("monsters","Human Test"));QVERIFY(document->identityAvailable("monsters","Human Test Copy"));QString diagnostic;QVERIFY2(document->setValue("monsters","human test","name","Human Test Renamed",&diagnostic),qPrintable(diagnostic));QVERIFY(!document->identityAvailable("monsters","human test renamed"));QVERIFY(document->hasUnappliedChanges(&diagnostic));QVERIFY2(document->applyToSource(&diagnostic),qPrintable(diagnostic));QVERIFY(!document->hasUnappliedChanges(&diagnostic));QVERIFY(document->undo());QVERIFY(document->hasUnappliedChanges(&diagnostic));QVERIFY(document->redo());QVERIFY(!document->hasUnappliedChanges(&diagnostic));
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
    QTableView* grid = dialog.findChild<QTableView*>("databaseGrid");
    QVERIFY(grid);
    QVERIFY(grid->currentIndex().isValid());

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
    const QRect rowCell = grid->visualRect(grid->model()->index(grid->currentIndex().row(),0));
    const QPoint local = rowCell.center();
    QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, local,
        grid->viewport()->mapToGlobal(local));
    QApplication::sendEvent(grid->viewport(), &menuEvent);
    QTRY_VERIFY_WITH_TIMEOUT(copiedByRealMenu, 1000);
    QVERIFY(cloneActionAvailable);
    QApplication::processEvents();
    QCOMPARE(QApplication::clipboard()->text(), QString("Human Test"));

    int wearsColumn = -1;
    for(int column = 0; column < grid->model()->columnCount(); ++column)
        if(grid->model()->headerData(column,Qt::Horizontal).toString().contains("Wears", Qt::CaseInsensitive))
            wearsColumn = column;
    QVERIFY(wearsColumn >= 0);
    QModelIndex wearsItem = grid->model()->index(grid->currentIndex().row(), wearsColumn);
    QVERIFY(wearsItem.isValid());
    QVERIFY(!wearsItem.data(Qt::UserRole + 2).toString().contains('['));
    QVERIFY(wearsItem.data(Qt::UserRole + 2).toString().contains('\n'));
    QVERIFY(!wearsItem.data(Qt::ToolTipRole).toString().contains(originalLongValue));
    grid->scrollTo(wearsItem, QAbstractItemView::PositionAtCenter);
    grid->setCurrentIndex(wearsItem);
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
    const QRect wearsRect = grid->visualRect(wearsItem);
    QVERIFY(wearsRect.isValid());
    QMetaObject::invokeMethod(grid, "doubleClicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, wearsItem));
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
    QVERIFY(dialog.selectPrototype("Human Test"));
    for(int column=0;column<grid->model()->columnCount();++column)
        if(grid->model()->headerData(column,Qt::Horizontal).toString().contains("Common loot Probability",Qt::CaseInsensitive))
            probabilityColumn=column;
    QVERIFY(probabilityColumn>=0);
    QModelIndex probabilityItem=grid->model()->index(grid->currentIndex().row(),probabilityColumn);
    QVERIFY(probabilityItem.isValid());
    const QString readableProbability=probabilityItem.data(Qt::UserRole+2).toString();
    QVERIFY(!readableProbability.contains('{'));
    QVERIFY(!readableProbability.contains('"'));
    QVERIFY(readableProbability.contains("[0]"));
    QVERIFY(readableProbability.contains("= 100"));
    bool editedProbability=false;
    QTimer::singleShot(0,[&editedProbability](){QDialog*editor=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!editor)return;QPlainTextEdit*text=editor->findChild<QPlainTextEdit*>("databaseFieldEditorText");QDialogButtonBox*buttons=editor->findChild<QDialogButtonBox*>("databaseFieldEditorButtons");if(text){QStringList lines=text->toPlainText().split('\n');for(QString&line:lines)if(line.trimmed().startsWith("[0]")){line="[0] Common loot Probability = 75";editedProbability=true;break;}text->setPlainText(lines.join('\n'));}if(buttons)buttons->button(QDialogButtonBox::Ok)->click();});
    QMetaObject::invokeMethod(grid,"doubleClicked",Qt::DirectConnection,Q_ARG(QModelIndex,probabilityItem));
    QVERIFY(editedProbability);
    monsters=document->table("units.monsters");QVERIFY(monsters);bool probabilityStored=false;QString storedProbability;for(const auto&row:monsters->rows)if(row.stableKey.startsWith("human test#")){storedProbability=row.values.value("fld.27.0").toString();const QJsonArray value=QJsonDocument::fromJson(storedProbability.toUtf8()).array();probabilityStored=!value.isEmpty()&&value.first().toObject().value("value").toDouble()==75.0;}QVERIFY2(probabilityStored,qPrintable(storedProbability));

    const int changesBeforeCancel = document->changes().size();
    QVERIFY(dialog.selectPrototype("Human Test"));
    QModelIndex cancelItem = grid->model()->index(grid->currentIndex().row(), wearsColumn);
    QVERIFY(cancelItem.isValid());
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
    QMetaObject::invokeMethod(grid, "doubleClicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, cancelItem));
    QCOMPARE(document->changes().size(), changesBeforeCancel);
}

void ProfileDatabaseTests::databaseEditorOpensKnownResourceReferences()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    ei::DatabaseEditorDialog dialog(document);dialog.show();
    QVERIFY(dialog.selectFinding("units.racemodels","Human Male"));
    QTableView* grid=dialog.findChild<QTableView*>("databaseGrid");QVERIFY(grid);
    int modelColumn=-1;
    for(int column=0;column<grid->model()->columnCount();++column)
        if(grid->model()->index(grid->currentIndex().row(),column)
               .data(Qt::UserRole+1).toString()==QStringLiteral("fld.30.0"))
            modelColumn=column;
    QVERIFY(modelColumn>=0);
    QCOMPARE(grid->model()->headerData(modelColumn,Qt::Horizontal).toString(),
             QStringLiteral("Model resource"));
    const QModelIndex model=grid->model()->index(grid->currentIndex().row(),modelColumn);
    grid->setCurrentIndex(model);grid->scrollTo(model,QAbstractItemView::PositionAtCenter);
    QSignalSpy requested(&dialog,&ei::DatabaseEditorDialog::resourceBrowserRequested);
    bool triggered=false;
    QTimer::singleShot(0,[&triggered](){
        QMenu* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(!menu)return;
        for(QAction* action:menu->actions())if(action->objectName()=="databaseOpenResourceAction")
        {action->trigger();triggered=true;return;}
        menu->close();
    });
    const QRect cell=grid->visualRect(model);QVERIFY(cell.isValid());
    QContextMenuEvent event(QContextMenuEvent::Mouse,cell.center(),
        grid->viewport()->mapToGlobal(cell.center()));
    QApplication::sendEvent(grid->viewport(),&event);
    QTRY_VERIFY_WITH_TIMEOUT(triggered,1000);
    QCOMPARE(requested.count(),1);
    QCOMPARE(requested.takeFirst().at(0).toString(),QStringLiteral("unhuma.mod"));
}

void ProfileDatabaseTests::databaseEditorMarksVerifiedLeverResources()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    auto members=relationalMembers();
    members["levers.ldb"]=database({record(stringField(0,"stst43")
        +stringField(6,"Tools\\Door.wav"))});
    write(QDir(temporary.path()).filePath("database.res"),resArchive(members));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    const auto* table=document->table("levers.leverprototypes");QVERIFY(table);
    const auto field=[table](int storageId)->const ei::SDatabaseFieldSchema* {
        for(const auto& candidate:table->fields)
            if(candidate.storageId==storageId&&candidate.storageOccurrence==0)return &candidate;
        return nullptr;
    };
    const auto* model=field(0);QVERIFY(model);QVERIFY(model->resourceReference);
    QCOMPARE(model->resourceExtension,QStringLiteral("mod"));
    const auto* sound=field(6);QVERIFY(sound);QVERIFY(sound->resourceReference);
    QVERIFY(sound->resourceExtension.isEmpty());
}

void ProfileDatabaseTests::databaseEditorUsesLazyModelAndNavigatesReferences()
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

    ei::DatabaseEditorDialog dialog(document);
    dialog.show();
    QVERIFY(dialog.selectPrototype("Human Test"));
    QTableView* grid = dialog.findChild<QTableView*>("databaseGrid");
    QVERIFY(grid);
    QVERIFY2(!qobject_cast<QTableWidget*>(grid),
             "The database grid must not eagerly allocate QTableWidgetItem objects.");
    QVERIFY(grid->model());
    QCOMPARE(grid->model()->objectName(), QString("databaseFilterModel"));

    int baseRaceColumn = -1;
    for(int column = 0; column < grid->model()->columnCount(); ++column)
        if(grid->model()->headerData(column, Qt::Horizontal).toString()
               .compare("Base Race", Qt::CaseInsensitive) == 0)
            baseRaceColumn = column;
    QVERIFY(baseRaceColumn >= 0);
    const QModelIndex reference = grid->model()->index(
        grid->currentIndex().row(), baseRaceColumn);
    QCOMPARE(reference.data(Qt::UserRole + 4).toString(),
             QString("units.racemodels"));
    grid->setCurrentIndex(reference);
    grid->edit(reference);
    QApplication::processEvents();
    QComboBox* picker = grid->findChild<QComboBox*>();
    QVERIFY(picker);
    QVERIFY(picker->isEditable());
    QVERIFY(picker->findText("Human Male", Qt::MatchFixedString) >= 0);
    QTest::keyClick(picker,Qt::Key_Escape);
    QApplication::processEvents();
    grid->setCurrentIndex(reference);
    grid->scrollTo(reference,QAbstractItemView::PositionAtCenter);
    QApplication::processEvents();

    bool openedReference = false;
    QTimer::singleShot(0, [&openedReference]() {
        QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(!menu)
            return;
        for(QAction* action : menu->actions())
            if(action->objectName() == "databaseOpenReferenceAction") {
                action->trigger();
                openedReference = true;
                return;
            }
        menu->close();
    });
    const QRect cell = grid->visualRect(reference);
    QVERIFY(cell.isValid());
    QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, cell.center(),
        grid->viewport()->mapToGlobal(cell.center()));
    QApplication::sendEvent(grid->viewport(), &menuEvent);
    QTRY_VERIFY_WITH_TIMEOUT(openedReference, 1000);
    QCOMPARE(grid->currentIndex().data(Qt::DisplayRole).toString(),
             QString("Human Male"));
}

void ProfileDatabaseTests::installedDatabaseInteractiveLatency()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run the installed interaction latency control.");
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString sourcePath=QDir(temporary.path()).filePath("database.res");QVERIFY(QFile::copy(archives.first(),sourcePath));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="latency";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const ei::SDatabaseTable* monsters=document->table("units.monsters");QVERIFY(monsters&&!monsters->rows.isEmpty());const QString rowKey=monsters->rows.first().stableKey;QString diagnostic;QElapsedTimer timer;timer.start();QVERIFY2(document->setValue("units.monsters",rowKey,"fld.3.0",monsters->rows.first().values.value("fld.3.0").toInt()+1,&diagnostic),qPrintable(diagnostic));const qint64 setValueMs=timer.restart();const auto snapshot=document->currentSnapshot(&diagnostic);const qint64 snapshotMs=timer.restart();QVERIFY2(snapshot,qPrintable(diagnostic));
    ei::DatabaseEditorDialog dialog(document);dialog.show();timer.restart();dialog.refreshFromHistory();const qint64 refreshMs=timer.elapsed();
    const QString prototype=document->displayIdentity("units.monsters",rowKey);QVERIFY(dialog.selectPrototype(prototype));
    QTableView* grid=dialog.findChild<QTableView*>("databaseGrid");QVERIFY(grid);QModelIndex editable;
    for(int column=1;column<grid->model()->columnCount()&&!editable.isValid();++column)
    {const QModelIndex candidate=grid->model()->index(grid->currentIndex().row(),column);if(candidate.flags()&Qt::ItemIsEditable)editable=candidate;}
    QVERIFY(editable.isValid());QVariant editedValue=editable.data(Qt::EditRole);bool numeric=false;const qlonglong integer=editedValue.toLongLong(&numeric);if(numeric)editedValue=integer+1;else editedValue=editedValue.toString()+QStringLiteral(" latency");
    timer.restart();QVERIFY(grid->model()->setData(editable,editedValue,Qt::EditRole));const qint64 interactiveCommitMs=timer.elapsed();
    const auto published=ei::ProfileDatabaseContext::instance().current();QVERIFY(published);QVERIFY(published->cacheKey!=snapshot->cacheKey);
    qInfo()<<"database interaction latency ms set"<<setValueMs<<"snapshot"<<snapshotMs<<"refresh"<<refreshMs<<"real model commit"<<interactiveCommitMs<<"rows"<<monsters->rows.size();QVERIFY2(setValueMs<100,qPrintable(QString("A simple database mutation took %1 ms; expected under 100 ms.").arg(setValueMs)));QVERIFY2(snapshotMs<500,qPrintable(QString("Runtime snapshot publication took %1 ms; expected under 500 ms.").arg(snapshotMs)));QVERIFY2(refreshMs<500,qPrintable(QString("A simple database refresh took %1 ms; expected under 500 ms.").arg(refreshMs)));QVERIFY2(interactiveCommitMs<150,qPrintable(QString("A real database cell commit took %1 ms; expected under 150 ms.").arg(interactiveCommitMs)));
}

void ProfileDatabaseTests::databaseEditorPersistsVisualColumnControls()
{
    const QString previousOrganization=QCoreApplication::organizationName();
    const QString previousApplication=QCoreApplication::applicationName();
    const QSettings::Format previousFormat=QSettings::defaultFormat();
    QTemporaryDir settingsDirectory;QVERIFY(settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("EI Mapper Endurance Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Database Column State"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDirectory.path());
    QSettings settings;
    settings.remove(QStringLiteral("databaseEditor/columns/base_units.monsters"));
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const int changesBefore=document->changes().size();

    const auto openColumnMenu=[]() -> QMenu* {
        QMenu* active=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(active && active->isVisible()
           && active->objectName()==QStringLiteral("databaseColumnMenu"))return active;
        for(QWidget* widget:QApplication::allWidgets())
            if(widget->isVisible()
               && widget->objectName()==QStringLiteral("databaseColumnMenu"))
                return qobject_cast<QMenu*>(widget);
        return nullptr;
    };
    auto triggerHeaderAction=[openColumnMenu](QHeaderView* header,const QString& objectName,int section) {
        bool triggered=false;
        QTimer poll;poll.setInterval(5);
        QObject::connect(&poll,&QTimer::timeout,[&triggered,objectName,&poll,openColumnMenu](){
            QMenu* menu=openColumnMenu();
            if(!menu)return;
            for(QAction* action:menu->findChildren<QAction*>())
                if(action->objectName()==objectName){action->trigger();triggered=true;poll.stop();menu->close();return;}
            menu->close();poll.stop();
        });
        QTimer timeout;timeout.setSingleShot(true);timeout.setInterval(3000);
        QObject::connect(&timeout,&QTimer::timeout,[openColumnMenu](){
            if(QMenu* menu=openColumnMenu())menu->close();
        });
        timeout.start();
        poll.start();
        const QPoint point(header->sectionViewportPosition(section)+header->sectionSize(section)/2,
                           header->height()/2);
        QMetaObject::invokeMethod(header,"customContextMenuRequested",Qt::DirectConnection,
                                  Q_ARG(QPoint,point));
        poll.stop();timeout.stop();
        return triggered;
    };
    auto clickHeaderActionStaysOpen=[openColumnMenu](QHeaderView* header,const QString& objectName,int section) {
        bool clicked=false,stayedOpen=false,hasDatabaseSource=false,hasEditorSource=false;
        QTimer poll;poll.setInterval(5);
        QObject::connect(&poll,&QTimer::timeout,[&](){
            QMenu* menu=openColumnMenu();
            if(!menu)return;
            hasDatabaseSource=menu->findChild<QMenu*>("databaseResColumnsSubmenu");
            hasEditorSource=menu->findChild<QMenu*>("databaseEditorColumnsSubmenu");
            for(QAction* action:menu->findChildren<QAction*>())if(action->objectName()==objectName)
            {
                QMenu* owner=qobject_cast<QMenu*>(action->parent());if(!owner)continue;
                clicked=true;poll.stop();
                if(owner!=menu){owner->popup(menu->mapToGlobal(QPoint(menu->width(),0)));QCoreApplication::processEvents();}
                QTest::mouseClick(owner,Qt::LeftButton,Qt::NoModifier,
                    owner->actionGeometry(action).center());
                QTimer::singleShot(0,[menu,owner,&stayedOpen](){stayedOpen=menu->isVisible();owner->close();menu->close();});return;
            }
        });
        QTimer timeout;timeout.setSingleShot(true);timeout.setInterval(3000);
        QObject::connect(&timeout,&QTimer::timeout,[openColumnMenu](){
            if(QMenu* menu=openColumnMenu())menu->close();
        });
        timeout.start();
        poll.start();const QPoint point(header->sectionViewportPosition(section)+header->sectionSize(section)/2,header->height()/2);
        QMetaObject::invokeMethod(header,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,point));
        poll.stop();timeout.stop();return clicked&&stayedOpen&&hasDatabaseSource&&hasEditorSource;
    };

    {
        ei::DatabaseEditorDialog dialog(document);dialog.show();
        QVERIFY(dialog.selectPrototype("Human Test"));
        QTableView* grid=dialog.findChild<QTableView*>("databaseGrid");QVERIFY(grid);
        QCOMPARE(grid->model()->headerData(0,Qt::Horizontal).toString(),QString("#"));
        QCOMPARE(grid->property("frozenColumnCount").toInt(),1);
        QTableView* frozen=dialog.findChild<QTableView*>("databaseFrozenGrid");QVERIFY(frozen);
        QVERIFY(!frozen->isColumnHidden(0));
        for(int column=1;column<frozen->model()->columnCount();++column)
            QVERIFY(frozen->isColumnHidden(column));
        const QString firstRowNumber=frozen->model()->index(0,0).data().toString();
        grid->setCurrentIndex(grid->model()->index(1,qMin(3,grid->model()->columnCount()-1)));
        grid->horizontalScrollBar()->setValue(grid->horizontalScrollBar()->maximum());
        QApplication::processEvents();
        QCOMPARE(grid->property("frozenColumnCount").toInt(),1);
        QCOMPARE(frozen->horizontalScrollBar()->value(),0);
        QCOMPARE(frozen->horizontalHeader()->offset(),0);
        QCOMPARE(frozen->model()->index(0,0).data().toString(),firstRowNumber);
        QVERIFY(!frozen->isColumnHidden(0));
        const int frozenScrollProbe=qMin(3,frozen->model()->columnCount()-1);
        frozen->setColumnHidden(frozenScrollProbe,false);
        frozen->scrollTo(frozen->model()->index(0,frozenScrollProbe),QAbstractItemView::PositionAtCenter);
        QApplication::processEvents();
        QCOMPARE(frozen->horizontalScrollBar()->value(),0);
        frozen->setColumnHidden(frozenScrollProbe,true);
        QVERIFY(!(grid->model()->flags(grid->model()->index(0,0))&Qt::ItemIsEditable));
        QLineEdit* filter=dialog.findChild<QLineEdit*>("databaseRowFilter");QVERIFY(filter);
        const QString rowNumber=grid->model()->index(grid->currentIndex().row(),0).data().toString();
        filter->setText(QStringLiteral("#")+rowNumber);
        QTRY_COMPARE_WITH_TIMEOUT(grid->model()->rowCount(),1,1000);
        filter->clear();QTRY_VERIFY_WITH_TIMEOUT(grid->model()->rowCount()>1,1000);

        grid->horizontalScrollBar()->setValue(0);
        grid->setAlternatingRowColors(false);
        const int currentRow=0;
        const int comparisonRow=qMin(1,grid->model()->rowCount()-1);
        const int tintColumn=qMin(2,grid->model()->columnCount()-1);
        grid->setCurrentIndex(grid->model()->index(currentRow,1));
        QApplication::processEvents();
        const QRect tintedCell=grid->visualRect(grid->model()->index(currentRow,tintColumn));
        const QRect baseCell=grid->visualRect(grid->model()->index(comparisonRow,tintColumn));
        QVERIFY(tintedCell.isValid()&&baseCell.isValid());
        const QImage painted=grid->viewport()->grab().toImage();
        const QColor tinted=painted.pixelColor(tintedCell.right()-3,tintedCell.center().y());
        const QColor base=painted.pixelColor(baseCell.right()-3,baseCell.center().y());
        QVERIFY2(tinted!=base,qPrintable(QString("Current row was not tinted: %1").arg(tinted.name())));

        QVERIFY(triggerHeaderAction(grid->horizontalHeader(),"databaseLockColumnsAction",2));
        QCOMPARE(grid->property("frozenColumnCount").toInt(),3);
        QVERIFY(!grid->wordWrap());QVERIFY(!frozen->wordWrap());
        for(int row=0;row<grid->model()->rowCount();++row)
            QCOMPARE(frozen->rowHeight(row),grid->rowHeight(row));
        QVERIFY(clickHeaderActionStaysOpen(grid->horizontalHeader(),"databaseHideAllColumns_database.res",0));
        int hiddenDatabaseColumns=0;
        for(int column=1;column<grid->model()->columnCount();++column)
            hiddenDatabaseColumns+=grid->isColumnHidden(column);
        QCOMPARE(hiddenDatabaseColumns,grid->model()->columnCount()-1);
        QVERIFY(!grid->isColumnHidden(0));
        QVERIFY(clickHeaderActionStaysOpen(grid->horizontalHeader(),"databaseShowAllColumns_database.res",0));
        QVERIFY(triggerHeaderAction(grid->horizontalHeader(),"databaseColumnVisibilityAction_3",2));
        int hidden=0;for(int column=0;column<grid->model()->columnCount();++column)
            hidden+=grid->isColumnHidden(column);
        QVERIFY(hidden>0);
        QCOMPARE(document->changes().size(),changesBefore);
    }
    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("databaseEditor/columns/base_units.monsters/frozenThrough")).toString(),
             QStringLiteral("fld.1.0"));
    {
        ei::DatabaseEditorDialog restored(document);restored.show();
        QVERIFY(restored.selectPrototype("Human Test"));
        QTableView* grid=restored.findChild<QTableView*>("databaseGrid");QVERIFY(grid);
        QCOMPARE(grid->property("frozenColumnCount").toInt(),3);
        int hidden=0;for(int column=0;column<grid->model()->columnCount();++column)
            hidden+=grid->isColumnHidden(column);
        QVERIFY(hidden>0);
        QCOMPARE(document->changes().size(),changesBefore);
    }
    settings.setValue(QStringLiteral("databaseEditor/columns/base_units.monsters/frozenThrough"),
                      QStringLiteral("removed.field.identity"));
    settings.sync();
    {
        ei::DatabaseEditorDialog staleState(document);staleState.show();
        QVERIFY(staleState.selectPrototype("Human Test"));
        QTableView* grid=staleState.findChild<QTableView*>("databaseGrid");QVERIFY(grid);
        QCOMPARE(grid->property("frozenColumnCount").toInt(),1);
    }
    settings.remove(QStringLiteral("databaseEditor/columns/base_units.monsters"));
    settings.sync();
    QCoreApplication::setOrganizationName(previousOrganization);
    QCoreApplication::setApplicationName(previousApplication);
    QSettings::setDefaultFormat(previousFormat);
}

void ProfileDatabaseTests::databaseEditorForwardsFocusedUndoRedo()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    ei::DatabaseEditorDialog dialog(document);dialog.show();dialog.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isActiveWindow(),1000);QVERIFY(dialog.selectPrototype("Human Test"));
    QTableView* grid=dialog.findChild<QTableView*>("databaseGrid");QVERIFY(grid);grid->setFocus();
    QSignalSpy undo(&dialog,&ei::DatabaseEditorDialog::undoRequested);
    QSignalSpy redo(&dialog,&ei::DatabaseEditorDialog::redoRequested);
    QTest::keySequence(grid,QKeySequence::Undo);
    QTest::keySequence(grid,QKeySequence::Redo);
    QCOMPARE(undo.count(),1);QCOMPARE(redo.count(),1);
}

void ProfileDatabaseTests::installedVerifiedReferenceMappingsResolve()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run installed reference controls.");
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString sourcePath=QDir(temporary.path()).filePath("database.res");QVERIFY(QFile::copy(archives.first(),sourcePath));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;request.providerId="base.standard.v1";request.profileId="references";request.resourceDirectories<<temporary.path();auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    int mappedFields=0;
    for(const QString& tableKey:document->tableKeys())
    {
        const auto* table=document->table(tableKey);if(!table||table->physicalTable<0)continue;
        for(const auto& field:table->fields)
        {
            if(field.referenceTable.isEmpty())
                continue;
            ++mappedFields;
            QSet<QString> targets;
            const auto* target=document->table(field.referenceTable);
            QVERIFY2(target,qPrintable(field.referenceTable));
            for(const auto& row:target->rows)
                if(!row.deleted)
                    targets.insert(document->displayIdentity(field.referenceTable,row.stableKey).trimmed().toLower());
            int populated=0,resolved=0;
            QStringList examples;
            for(const auto& row:table->rows)
            {
                if(row.deleted)
                    continue;
                const QString value=row.values.value(field.key).toString().trimmed().toLower();
                if(value.isEmpty())
                    continue;
                ++populated;
                resolved+=targets.contains(value);
                if(examples.size()<4)
                    examples.append(value);
            }
            qInfo()<<"database reference coverage"<<tableKey<<field.label<<field.referenceTable
                   <<resolved<<populated<<"values"<<examples<<"targets"<<targets.values().mid(0,4);
            QVERIFY2(populated==0||resolved*100>=populated*90,
                     qPrintable(QString("Only %1/%2 values in %3.%4 resolve to %5")
                                .arg(resolved).arg(populated).arg(tableKey,field.label,field.referenceTable)));
        }
    }
    QVERIFY(mappedFields>=1);
}

void ProfileDatabaseTests::resourceNavigationPreservesDocking()
{
    QMainWindow host;
    QDockWidget dock(&host);
    QDockWidget sibling(&host);
    QDialog source;
    dock.setObjectName(QStringLiteral("resourceBrowserDockProbe"));
    sibling.setObjectName(QStringLiteral("resourceBrowserSiblingDockProbe"));
    host.addDockWidget(Qt::RightDockWidgetArea,&dock);
    host.addDockWidget(Qt::RightDockWidgetArea,&sibling);
    host.tabifyDockWidget(&dock,&sibling);
    host.show();
    host.setGeometry(0,0,500,400);
    source.setGeometry(600,0,500,400);
    source.show();
    sibling.raise();
    QApplication::processEvents();
    QVERIFY(!dock.isFloating());
    ei::revealDockNavigationTarget(&host,&dock,&source);
    QApplication::processEvents();
    QVERIFY(host.isVisible());
    QVERIFY(dock.isVisible());
    QVERIFY(!dock.isFloating());
    QVERIFY(!dock.visibleRegion().isEmpty());
    QVERIFY(source.isVisible());
}

void ProfileDatabaseTests::installedRaceModelSelectionAndContextLatency()
{
    const QString archive=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES")
        .split(';',QString::SkipEmptyParts).value(0);
    if(archive.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run Race Models UI latency.");
    const QFileInfo source(archive);ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;request.providerId="jmv.v1";
    request.profileId="jamais-vu";request.resourceDirectories<<source.absolutePath();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const auto* table=document->table("units.racemodels");QVERIFY(table&&table->rows.size()>2);
    ei::DatabaseEditorDialog dialog(document);dialog.show();
    dialog.resize(1775,1129);
    QVERIFY(dialog.selectFinding("units.racemodels",table->rows.at(1).stableKey));
    QTableView* grid=dialog.findChild<QTableView*>("databaseGrid");QVERIFY(grid);
    const QModelIndex cell=grid->model()->index(grid->currentIndex().row(),
        qMin(3,grid->model()->columnCount()-1));QVERIFY(cell.isValid());
    QElapsedTimer timer;timer.start();grid->setCurrentIndex(cell);grid->selectRow(cell.row());
    QApplication::processEvents();const qint64 selectionMs=timer.restart();
    const QRect visual=grid->visualRect(cell);QVERIFY(visual.isValid());
    QContextMenuEvent event(QContextMenuEvent::Mouse,visual.center(),
        grid->viewport()->mapToGlobal(visual.center()));
    QApplication::sendEvent(grid->viewport(),&event);QApplication::processEvents();
    const qint64 contextMs=timer.elapsed();
    const int contextSelectionCells=grid->selectionModel()->selectedIndexes().size();
    if(QMenu* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()))menu->close();
    qint64 worstColdRowMs=0,totalColdRowsMs=0;
    const int lastRow=qMin(grid->model()->rowCount()-1,100);
    for(int row=10;row<=lastRow;row+=5)
    {
        const QModelIndex cold=grid->model()->index(row,cell.column());
        timer.restart();grid->setCurrentIndex(cold);grid->selectRow(row);
        QApplication::processEvents();const qint64 elapsed=timer.elapsed();
        worstColdRowMs=qMax(worstColdRowMs,elapsed);totalColdRowsMs+=elapsed;
    }
    const QModelIndex clicked=grid->model()->index(qMin(20,grid->model()->rowCount()-1),cell.column());
    const QPoint clickedPoint=grid->visualRect(clicked).center();
    timer.restart();QTest::mouseClick(grid->viewport(),Qt::LeftButton,Qt::NoModifier,clickedPoint);
    QApplication::processEvents();const qint64 mouseClickMs=timer.restart();
    const int scrollMaximum=grid->horizontalScrollBar()->maximum();
    for(int pass=0;pass<4;++pass)
    {
        grid->horizontalScrollBar()->setValue(pass%2?0:scrollMaximum);
        QApplication::processEvents();
    }
    const qint64 fourFullScrollsMs=timer.elapsed();
    qInfo()<<"Race Models UI latency ms selection"<<selectionMs<<"context"<<contextMs
           <<"worst cold row"<<worstColdRowMs<<"cold total"<<totalColdRowsMs
           <<"mouse click"<<mouseClickMs<<"four full scrolls"<<fourFullScrollsMs
           <<"context selection cells"<<contextSelectionCells
           <<"rows"<<table->rows.size()<<"columns"<<grid->model()->columnCount();
    QVERIFY2(selectionMs<100,qPrintable(QString("Race Models selection took %1 ms").arg(selectionMs)));
    QVERIFY2(contextMs<100,qPrintable(QString("Race Models context menu took %1 ms").arg(contextMs)));
    QCOMPARE(contextSelectionCells,1);
    QVERIFY2(worstColdRowMs<60,qPrintable(QString("Race Models cold row selection took %1 ms").arg(worstColdRowMs)));
    QVERIFY2(mouseClickMs<100,qPrintable(QString("Race Models mouse click took %1 ms").arg(mouseClickMs)));
    QVERIFY2(fourFullScrollsMs<200,qPrintable(QString("Race Models scrolling took %1 ms").arg(fourFullScrollsMs)));
}

void ProfileDatabaseTests::itemSpellConstructorParsesCanonicalAndLegacyCodes()
{
    const ei::SParsedSpellCode spell = ei::ItemSpellConstructorCatalog::parseSpellCode(
        " acid_ray { e0,e1 ; im2 } ");
    QVERIFY(spell.valid);
    QCOMPARE(spell.prototypeCode, QString("acid_ray"));
    QCOMPARE(spell.runeCodes, QStringList({"e0", "e1", "im2"}));
    QCOMPARE(ei::ItemSpellConstructorCatalog::formatSpellCode(spell.prototypeCode,
                                                              spell.runeCodes),
             QString("acid_ray{e0;e1;im2}"));

    const ei::SParsedItemCode item = ei::ItemSpellConstructorCatalog::parseItemCode(
        "vlfsh.black wolf hide[prot_fire{it;m0;im0}]");
    QVERIFY(item.valid);
    QCOMPARE(item.prototypeName, QString("vlfsh"));
    QCOMPARE(item.materialName, QString("black wolf hide"));
    QVERIFY(item.spell.valid);
    QCOMPARE(ei::ItemSpellConstructorCatalog::formatItemCode(
                 item.prototypeName, item.materialName,
                 ei::ItemSpellConstructorCatalog::formatSpellCode(
                     item.spell.prototypeCode, item.spell.runeCodes)),
             QString("vlfsh.black wolf hide[prot_fire{it;m0;im0}]"));
}

void ProfileDatabaseTests::itemSpellConstructorRestrictsMaterialsAndReportsSoftLimits()
{
    ei::ItemSpellConstructorCatalog catalog;
    ei::SConstructorMaterial wool; wool.name="wool"; wool.displayName="Wool";
    wool.type="Cloth"; wool.code="wo"; wool.mana=2.0; wool.slotCapacity=2;
    ei::SConstructorMaterial thin; thin.name="thin"; thin.displayName="Thin leather";
    thin.type="Leather"; thin.code="le"; thin.mana=3.0; thin.slotCapacity=3;
    ei::SConstructorMaterial bones; bones.name="bones"; bones.displayName="Bones";
    bones.type="Bone"; bones.code="bn"; bones.mana=100.0; bones.slotCapacity=100;
    catalog.materials << wool << thin << bones;
    ei::SConstructorItem item;
    item.name = "shirt"; item.materialType = "Leather"; item.mana = 5.0;
    item.slotCapacity = 4; item.components = 2;
    const QVector<ei::SConstructorMaterial> choices = catalog.compatibleMaterials(item);
    QCOMPARE(choices.size(), 1);
    QCOMPARE(choices.front().name, QString("thin"));

    ei::SConstructorRune rune; rune.name="effect"; rune.displayName="Effect";
    rune.code="e0"; rune.type=3; rune.complexity=3; rune.mana=7.0;
    rune.value=0.5;
    catalog.runes << rune;
    ei::SConstructorSpell spell;
    spell.code = "fire"; spell.mana = 20.0; spell.slotCapacity=2;
    spell.complexity = 20; spell.effect=10.0;
    const ei::SConstructorLimitReport limits = catalog.limits(
        item, &choices.front(), &spell, {"e0"});
    QCOMPARE(limits.itemMana, 8.0);
    QCOMPARE(limits.itemManaRegenerationPerSecond, 0.12);
    QCOMPARE(limits.itemSlots, 7);
    QCOMPARE(limits.spellMana, 27.0);
    QCOMPARE(limits.spellSlots, 3);
    QCOMPARE(limits.spellComplexity, 23);
    QCOMPARE(limits.effect, 15.0);
    QCOMPARE(limits.warnings.size(), 2);
    QVERIFY(limits.warnings.join('\n').contains("difficulty",Qt::CaseInsensitive));
}

void ProfileDatabaseTests::itemSpellConstructorCalculatesRuntimeSpellValues()
{
    ei::ItemSpellConstructorCatalog catalog;
    ei::SConstructorSpell spell;
    spell.code="blast"; spell.mana=20.0; spell.slotCapacity=2;
    spell.complexity=12; spell.range=15.0; spell.area=12.566370614359172;
    spell.effect=40.0; spell.durationSegments=45; spell.targets=2;
    auto rune=[](const char* code,int type,double mana,double value,int complexity){
        ei::SConstructorRune result;result.code=code;result.type=type;
        result.mana=mana;result.value=value;result.complexity=complexity;return result;
    };
    catalog.runes << rune("r0",0,1.0,0.5,1) << rune("a0",2,2.0,0.5,2)
                  << rune("e0",3,3.0,-0.25,3) << rune("d0",4,4.0,1.0,4)
                  << rune("m0",5,-1.0,-0.25,5);
    const auto report=catalog.spellReport(spell,{"r0","a0","e0","d0","m0"});
    QCOMPARE(report.spellMana,24.0); // 20 + flat 9 - 25% of base mana
    QCOMPARE(report.spellSlots,7);   // base slots plus one slot per rune
    QCOMPARE(report.spellComplexity,27);
    QCOMPARE(report.range,22.5);
    QVERIFY(qAbs(report.area-18.84955592153876)<0.000001);
    QVERIFY(qAbs(report.areaRadius-qSqrt(6.0))<0.000001);
    QCOMPARE(report.effect,30.0);
    QCOMPARE(report.durationSegments,90);
    QCOMPARE(report.targets,2);

    ei::ItemSpellConstructorCatalog jmvCodes;
    jmvCodes.runes << rune("pr0",4,0.0,0.5,0)
                   << rune("pp0",3,0.0,0.25,0);
    const auto jmvReport=jmvCodes.spellReport(spell,{"pr0","pp0"});
    QCOMPARE(jmvReport.durationSegments,67);
    QCOMPARE(jmvReport.effect,50.0);
    QVERIFY(qAbs(jmvReport.spellManaPerSecond-(20.0/(67.0/15.0)))<0.000001);
}

void ProfileDatabaseTests::itemSpellConstructorAppliesJmvItemRunePenalties()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath("mod/Res"));
    QVERIFY(QDir(root.path()).mkpath("mod/lua"));
    auto members=relationalMembers();
    members["spells.sdb"]=database({record(stringField(0,"Weaken")
        +stringField(1,"weak")+stringField(2,"acid")
        +tag(10,QByteArray::fromHex("00004041")))});
    write(root.filePath("mod/Res/database.res"),resArchive(members));
    write(root.filePath("mod/lua/tables.lua"),unitPerkLua("Human Test",1)
        +QByteArray("\nSpellAdds = { {\"weak\", 20.0}, }\n"));
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";
    request.profileId="jamais-vu";
    request.resourceDirectories << root.filePath("mod/Res");
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    ei::ItemSpellConstructorCatalog catalog=ei::ItemSpellConstructorCatalog::fromDocument(*document);
    QVERIFY(catalog.isJmvProfile());
    const auto* weak=catalog.findSpell("weak");QVERIFY(weak);
    QCOMPARE(weak->effect,12.0);QCOMPARE(weak->effectBonus,20.0);
    ei::SConstructorSpell spell;
    spell.code="continuous";
    spell.mana=20.0;
    spell.effect=40.0;
    spell.durationSegments=60;
    auto rune=[](const char* code){
        ei::SConstructorRune result;
        result.code=code;
        result.type=7;
        return result;
    };
    catalog.runes << rune("it") << rune("ic");

    const auto trigger = catalog.spellReport(spell,{"it"});
    QCOMPARE(trigger.effect,10.0);
    QCOMPARE(trigger.durationSegments,45);
    const auto continuous = catalog.spellReport(spell,{"ic"});
    QCOMPARE(continuous.effect,10.0);
    QCOMPARE(continuous.durationSegments,45);

    ei::SConstructorRune effectRune;effectRune.code="e3";effectRune.type=3;
    effectRune.value=1.0;catalog.runes<<effectRune;
    spell.effect=12.0;spell.effectBonus=20.0;
    const auto jmvAddedEffect=catalog.spellReport(spell,{"ic","e3","e3"});
    QCOMPARE(jmvAddedEffect.effectBase,27.0);
    QCOMPARE(jmvAddedEffect.effectBonus,20.0);
    QCOMPARE(jmvAddedEffect.effect,47.0);
}

void ProfileDatabaseTests::itemSpellConstructorDoesNotTreatAuthoredCodesAsCraftingExceptions()
{
    QTemporaryDir directory;
    const QString path=directory.filePath("database.res");
    write(path,resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="constructor-authored-material";
    request.resourceDirectories<<directory.path();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    const auto* armors=document->table("items.armors");QVERIFY(armors&&!armors->rows.isEmpty());
    QString diagnostic;
    QVERIFY2(document->setValue("items.armors",armors->rows.first().stableKey,
                                "fld.3.0","Leather",&diagnostic),qPrintable(diagnostic));
    const auto catalog=ei::ItemSpellConstructorCatalog::fromDocument(*document);
    const auto* armor=catalog.findItem("armor_leather");QVERIFY(armor);
    QVERIFY(catalog.isAuthoredMaterialReference("armor_leather","iron"));
    QCOMPARE(catalog.authoredMaterialTypeMismatches(),
             QStringList({QStringLiteral("armor_leather.iron (Leather != Metal)")}));
    const auto compatible=catalog.compatibleMaterials(*armor);
    QVERIFY(compatible.isEmpty());
}

void ProfileDatabaseTests::databaseValidationReportsInvalidConstructedItems()
{
    QTemporaryDir directory;QVERIFY(directory.isValid());
    write(directory.filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="validation-items";
    request.resourceDirectories<<directory.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const auto* armors=document->table("items.armors");QVERIFY(armors&&!armors->rows.isEmpty());
    QString diagnostic;QVERIFY2(document->setValue("items.armors",armors->rows.first().stableKey,
        "fld.3.0","Leather",&diagnostic),qPrintable(diagnostic));
    const auto findings=document->validate();bool found=false;
    for(const auto& finding:findings)if(finding.code=="database.item.material_incompatible")
    {found=true;QCOMPARE(finding.severity,ei::SDatabaseValidationFinding::Severity::Warning);
     QCOMPARE(finding.tableKey,QString("units.monsters"));QVERIFY(!finding.rowKey.isEmpty());
     QVERIFY(finding.message.contains("armor_leather.iron"));}
    QVERIFY(found);
}

void ProfileDatabaseTests::itemSpellConstructorUsesGameTextKeys()
{
    ei::ItemSpellConstructorCatalog catalog;
    ei::SConstructorMaterial material; material.name="Iron"; material.displayName="Iron";
    material.type="Metal"; material.code="ir"; catalog.materials << material;
    ei::SConstructorMaterial ogre;ogre.name="ogre black leather";
    ogre.displayName=ogre.name;catalog.materials<<ogre;
    ei::SConstructorSpell spell; spell.name="Fireball"; spell.displayName="Fireball";
    spell.code="fireball"; spell.subtype="fire"; spell.school="Fire"; spell.textureType=3;
    catalog.spells << spell;
    ei::SConstructorRune rune; rune.name="Cut Effect"; rune.displayName="Cut Effect";
    rune.code="e0"; rune.type=3; catalog.runes << rune;
    ei::SConstructorItem item; item.name="vlcsh"; item.displayName="vlcsh";
    item.category="Armor"; item.type="shirt"; item.materialType="Cloth";
    item.typeId=3; catalog.items << item;
    catalog.applyGameTexts({
        {"MATERIAL Ogre_Black_Leather",QStringLiteral("Tanned ogre leather\r\nDescription")},
        {"MATERIAL Iron", QString::fromUtf8("Железо\r\nDescription")},
        {"SPELL fireball", QString::fromUtf8("Огненный шар\r\nDescription")},
        {"MODIFIER e0", QString::fromUtf8("Усиление\r\n\r\nDescription\r\n\r\nПр\r\n")},
        {"INSTR vlcsh", QString::fromUtf8("Безрукавка\r\nDescription")}
    });
    QCOMPARE(catalog.materials.front().displayName, QString::fromUtf8("Железо"));
    QCOMPARE(catalog.spells.front().displayName, QString::fromUtf8("Огненный шар"));
    QCOMPARE(catalog.runes.front().displayName, QString::fromUtf8("Усиление"));
    QCOMPARE(catalog.runes.front().tag, QString::fromUtf8("Пр"));
    QCOMPARE(catalog.items.front().displayName, QString::fromUtf8("Безрукавка"));
}

void ProfileDatabaseTests::itemSpellConstructorNormalizesMaterialTextKeys()
{
    ei::ItemSpellConstructorCatalog catalog;
    ei::SConstructorMaterial material;material.name="ogre black leather";
    material.displayName=material.name;catalog.materials<<material;
    catalog.applyGameTexts({{"MATERIAL Ogre_Black_Leather",
                             QStringLiteral("Tanned ogre leather\r\nDescription")}});
    QCOMPARE(catalog.materials.front().displayName,QString("Tanned ogre leather"));
}

void ProfileDatabaseTests::databaseEditorExposesConstructorEntryPoint()
{
    QTemporaryDir directory;
    const QString path=directory.filePath("database.res");
    write(path,resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<directory.path();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    ei::DatabaseEditorDialog dialog(document);
    QPushButton* button=dialog.findChild<QPushButton*>("databaseItemSpellConstructorButton");
    QVERIFY(button);
    QSignalSpy requested(&dialog,&ei::DatabaseEditorDialog::itemSpellConstructorRequested);
    button->click();
    QCOMPARE(requested.count(),1);
}

void ProfileDatabaseTests::databaseEditorUsesCleanImportAndSchemaLabels()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    write(QDir(temporary.path()).filePath("database.res"),resArchive(relationalMembers()));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="base";
    request.resourceDirectories<<temporary.path();
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    ei::DatabaseEditorDialog dialog(document);
    QPushButton* importButton=nullptr;
    for(QPushButton* button:dialog.findChildren<QPushButton*>())
        if(button->menu()&&button->text()==QStringLiteral("Import…")){importButton=button;break;}
    QVERIFY(importButton);
    QCOMPARE(importButton->menu()->actions().at(0)->text(),QStringLiteral("Import XLSX…"));
    QCOMPARE(importButton->menu()->actions().at(1)->text(),QStringLiteral("Import database.res…"));
    QCOMPARE(importButton->menu()->actions().at(2)->text(),QStringLiteral("Import tables.lua…"));
    const auto weapon1=ei::DatabaseSchema::instance().headers("units",3,33);
    const auto weapon2=ei::DatabaseSchema::instance().headers("units",3,42);
    QVERIFY(!weapon1.isEmpty());QVERIFY(!weapon2.isEmpty());
    QCOMPARE(weapon1.first().name,QStringLiteral("Equipment Weapon 1"));
    QCOMPARE(weapon2.first().name,QStringLiteral("Equipment Weapon 2"));
}

void ProfileDatabaseTests::itemSpellConstructorUsesVerifiedPreviewFamilies()
{
    ei::SConstructorMaterial iron;iron.name="iron";iron.code="ir";
    ei::SConstructorItem weapon;weapon.category="Weapon";weapon.typeId=1;
    weapon.textureType1=5;weapon.textureType2=0;
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemFigureName(weapon),QString("initweax5weapon"));
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemTextureName(weapon,&iron),QString("unhumaax_05.ir.0"));
    ei::SConstructorItem armor;armor.category="Armor";armor.type="shirt";
    armor.textureType1=3;armor.textureType2=1;
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemFigureName(armor),QString("initarsh3armor"));
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemTextureName(armor,&iron),QString("unhumash_03.ir.1"));
    ei::SConstructorItem quick;quick.category="Quick Item";quick.textureType1=17;
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemFigureName(quick),QString("initqi17item"));
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemTextureName(quick,nullptr),QString("qitem0017"));
    QCOMPARE(ei::ItemSpellConstructorCatalog::itemTextureName(quick,&iron),QString("qitem0017.ir"));
    ei::ItemSpellConstructorCatalog baseCatalog;
    QCOMPARE(baseCatalog.itemTextureCandidates(quick,&iron),QStringList({"qitem0017.ir"}));
    QTemporaryDir jmvRoot;QVERIFY(jmvRoot.isValid());
    QVERIFY(QDir(jmvRoot.path()).mkpath("mod/Res"));
    QVERIFY(QDir(jmvRoot.path()).mkpath("mod/lua"));
    write(jmvRoot.filePath("mod/Res/database.res"),resArchive(relationalMembers()));
    write(jmvRoot.filePath("mod/lua/tables.lua"),unitPerkLua("Human Test",1));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="jamais-vu";
    request.resourceDirectories<<jmvRoot.filePath("mod/Res");
    const auto jmvDocument=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(jmvDocument);
    const auto jmvCatalog=ei::ItemSpellConstructorCatalog::fromDocument(*jmvDocument);
    QCOMPARE(jmvCatalog.itemTextureCandidates(quick,&iron),
             QStringList({"qitem0017.ir_x2","qitem0017.ir"}));
    ei::SConstructorSpell spell;spell.textureType=3;
    ei::ItemSpellConstructorCatalog catalog;
    QCOMPARE(catalog.spellTextureName(spell),QString("spell0003"));
    ei::SConstructorRune rune;rune.textureIndex=17;
    QCOMPARE(catalog.runeTextureCandidates(rune),QStringList({"modifier0017"}));

    QImage orientation(1,2,QImage::Format_ARGB32);
    orientation.setPixelColor(0,0,Qt::red);orientation.setPixelColor(0,1,Qt::blue);
    const QImage displayed=ei::ItemSpellConstructorCatalog::orientPreviewImage(orientation);
    QCOMPARE(displayed.pixelColor(0,0),QColor(Qt::blue));
    QCOMPARE(displayed.pixelColor(0,1),QColor(Qt::red));
    QStringList codes={"ee1","e10","e3","e2"};
    std::sort(codes.begin(),codes.end(),ei::ItemSpellConstructorCatalog::naturalCodeLess);
    QCOMPARE(codes,QStringList({"e2","e3","e10","ee1"}));
}

void ProfileDatabaseTests::itemSpellConstructorPreviewPoliciesAreCategoryAware()
{
    ei::SConstructorItem quick;quick.category="Quick Item";
    ei::SConstructorItem weapon;weapon.category="Weapon";
    // Quick-item FIGs address one authored atlas cell, while their qitem
    // material textures are standalone frames. Preserving those UVs samples
    // the transparent corner of the standalone texture.
    QVERIFY(!ei::ItemSpellConstructorCatalog::preservesStandaloneTextureUvs(quick));
    QVERIFY(!ei::ItemSpellConstructorCatalog::preservesStandaloneTextureUvs(weapon));
    QVERIFY(ei::ItemSpellConstructorCatalog::normalizesStandaloneTextureAtlasCell(quick));
    QVERIFY(!ei::ItemSpellConstructorCatalog::normalizesStandaloneTextureAtlasCell(weapon));
    QVERIFY(ei::ItemSpellConstructorCatalog::previewCategoryNeedsCameraReset({},"Weapon"));
    QVERIFY(!ei::ItemSpellConstructorCatalog::previewCategoryNeedsCameraReset("Weapon","Weapon"));
    QVERIFY(ei::ItemSpellConstructorCatalog::previewCategoryNeedsCameraReset("Weapon","Armor"));
    QVERIFY(ei::ItemSpellConstructorCatalog::previewCategoryNeedsCameraReset("Armor","Quick Item"));
}

void ProfileDatabaseTests::itemSpellConstructorDoesNotDirtySparseJmvDocument()
{
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QDir root(temporary.path());
    QVERIFY(root.mkpath("mod/res"));QVERIFY(root.mkpath("mod/lua"));
    write(root.filePath("mod/res/database.res"),resArchive(relationalMembers()));
    QByteArray lua=unitPerkLua("Human Test",0);
    const int statStart=lua.indexOf("UnitStat = {");QVERIFY(statStart>=0);
    lua=lua.left(statStart)+QByteArray("UnitStat = {\n{\"Human Test\",100,10.5,},\n}\n");
    write(root.filePath("mod/lua/tables.lua"),lua);
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="jamais-vu";
    request.resourceDirectories<<root.filePath("mod/res");
    auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    QString diagnostic;
    QVERIFY2(!document->hasUnappliedChanges(&diagnostic),qPrintable(diagnostic));
    const auto catalog=ei::ItemSpellConstructorCatalog::fromDocument(*document);
    QVERIFY(!catalog.items.isEmpty());
    QVERIFY(!document->isDirty());
    QVERIFY2(!document->hasUnappliedChanges(&diagnostic),qPrintable(diagnostic));
}

void ProfileDatabaseTests::itemSpellConstructorUsesEffectiveJmvSpellCatalog()
{
    QTemporaryDir directory;QVERIFY(directory.isValid());
    QMap<QString,QByteArray> members=relationalMembers();
    const QByteArray original=stringField(0,"Old Fire")+stringField(1,"fireball")
        +stringField(2,"fire")+intField(4,0)+intField(18,3);
    const QByteArray replacement=stringField(0,"JMV Fire")+stringField(1,"fireball")
        +stringField(2,"fire")+intField(4,0)+intField(18,27);
    members["spells.sdb"]=database({record(original)+record(replacement)});
    write(directory.filePath("database.res"),resArchive(members));
    write(directory.filePath("databaseJmv.res"),resArchive({}));
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";request.profileId="jamais-vu";
    request.resourceDirectories<<directory.path();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));QVERIFY(document);
    const auto catalog=ei::ItemSpellConstructorCatalog::fromDocument(*document);
    QVERIFY(catalog.isJmvProfile());
    QCOMPARE(catalog.spells.size(),1);
    QCOMPARE(catalog.spells.front().name,QString("JMV Fire"));
    QCOMPARE(catalog.spells.front().school,QString("Fire"));
    QCOMPARE(catalog.spells.front().recordType,0);
    QCOMPARE(catalog.spellTextureCandidates(catalog.spells.front()),
             QStringList({"spell0027.x2","spell0027"}));
    ei::SConstructorRune rune;rune.textureIndex=17;
    QCOMPARE(catalog.runeTextureCandidates(rune),
             QStringList({"modifier0017.x2","modifier0017"}));
}

void ProfileDatabaseTests::installedConstructorCatalogCoverage()
{
    const QStringList archives=qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES").split(';',QString::SkipEmptyParts);
    if(archives.isEmpty())QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run constructor corpus coverage.");
    const QFileInfo info(archives.first());QVERIFY(info.isFile());
    ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
    request.providerId="base.standard.v1";request.profileId="constructor-corpus";
    request.resourceDirectories<<info.absolutePath();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    const auto catalog=ei::ItemSpellConstructorCatalog::fromDocument(*document);
    QVERIFY2(catalog.materials.size()>=20,qPrintable(QString("materials=%1").arg(catalog.materials.size())));
    QVERIFY2(catalog.items.size()>=100,qPrintable(QString("items=%1").arg(catalog.items.size())));
    QVERIFY2(catalog.spells.size()>=20,qPrintable(QString("spells=%1").arg(catalog.spells.size())));
    QVERIFY2(catalog.runes.size()>=20,qPrintable(QString("runes=%1").arg(catalog.runes.size())));
    for(const auto& item:catalog.items)
        for(const auto& material:catalog.compatibleMaterials(item))
            QCOMPARE(material.type.compare(item.materialType,Qt::CaseInsensitive),0);
    const QStringList authoredMismatches=catalog.authoredMaterialTypeMismatches();
    qInfo()<<"authored item/material references which the runtime constructor rejects"
           <<authoredMismatches;
    if(const auto* agilityStaff=catalog.findItem(QStringLiteral("agility staff")))
    {
        bool metalAvailable=false;
        QStringList names;
        for(const auto& material:catalog.compatibleMaterials(*agilityStaff))
        {
            names<<material.name;
            metalAvailable|=material.type.compare(QStringLiteral("Metal"),Qt::CaseInsensitive)==0
                || material.type.compare(QStringLiteral("Metall"),Qt::CaseInsensitive)==0;
        }
        QVERIFY2(metalAvailable,qPrintable(QStringLiteral(
            "The authored agility staff supports metal materials; choices were: %1")
            .arg(names.join(QStringLiteral(", ")))));
    }
}

void ProfileDatabaseTests::installedConstructorImportsAgilityStaffIron()
{
    const QString archive = qEnvironmentVariable("EI_TEST_DATABASE_ARCHIVES")
                                .split(';', QString::SkipEmptyParts).value(0);
    if(archive.isEmpty())
        QSKIP("Set EI_TEST_DATABASE_ARCHIVES to run the supplied constructor import control.");
    const QFileInfo info(archive);
    QVERIFY(info.isFile());
    ei::ProfileDatabaseService service;
    ei::SProfileDatabaseLoadRequest request;
    request.providerId="jmv.v1";
    request.profileId="jamais-vu";
    request.resourceDirectories << info.absolutePath();
    const auto document=ei::DatabaseDocument::fromSnapshot(service.snapshot(request));
    QVERIFY(document);
    const auto catalog=ei::ItemSpellConstructorCatalog::fromDocument(*document);
    const auto parsed=ei::ItemSpellConstructorCatalog::parseItemCode(
        "agility staff.iron[slow{it;e1;m1;m1;im1;im1;im1}]");
    QVERIFY(parsed.valid);
    const auto* item=catalog.findItem(parsed.prototypeName);
    const auto* material=catalog.findMaterial(parsed.materialName);
    QVERIFY(item);
    QVERIFY(material);
    QCOMPARE(item->materialType.compare(material->type,Qt::CaseInsensitive),0);
    bool offered=false;
    for(const auto& candidate:catalog.compatibleMaterials(*item))
        offered|=candidate.name.compare(material->name,Qt::CaseInsensitive)==0;
    QVERIFY(offered);

    const QString overLimitCode="large potion 3[speed{e3;e3;e3;e3;e3;d3;d2;d2}]";
    const auto overLimit=ei::ItemSpellConstructorCatalog::parseItemCode(overLimitCode);
    QVERIFY(overLimit.valid);const auto* potion=catalog.findItem(overLimit.prototypeName);
    const auto* spell=catalog.findSpell(overLimit.spell.prototypeCode);QVERIFY(potion);QVERIFY(spell);
    const auto limits=catalog.limits(*potion,nullptr,spell,overLimit.spell.runeCodes);
    QVERIFY(limits.spellComplexity>limits.itemSlots);
    QString energyCode;
    for(const auto& candidate:catalog.items)
    {
        const auto materials=catalog.compatibleMaterials(candidate);
        if(materials.isEmpty())continue;
        const auto energyLimits=catalog.limits(candidate,&materials.first(),spell,overLimit.spell.runeCodes);
        if(energyLimits.spellMana>energyLimits.itemMana)
        {energyCode=ei::ItemSpellConstructorCatalog::formatItemCode(candidate.name,materials.first().name,
            ei::ItemSpellConstructorCatalog::formatSpellCode(overLimit.spell.prototypeCode,overLimit.spell.runeCodes));break;}
    }
    QVERIFY(!energyCode.isEmpty());
    const auto* monsters=document->table("units.monsters");QVERIFY(monsters);QString rowKey;
    for(const auto& row:monsters->rows)if(row.values.contains("fld.32.0")){rowKey=row.stableKey;break;}
    QVERIFY(!rowKey.isEmpty());QString diagnostic;
    QVERIFY2(document->setValue("units.monsters",rowKey,"fld.32.0",
        QStringLiteral("[\"")+overLimitCode+QStringLiteral("\",\"")+energyCode+QStringLiteral("\"]"),&diagnostic),qPrintable(diagnostic));
    QSet<QString> codes;for(const auto& finding:document->validate())codes.insert(finding.code);
    QVERIFY(codes.contains("database.item.spell_energy_exceeded"));
    QVERIFY(codes.contains("database.item.spell_difficulty_exceeded"));

    QVERIFY(document->undo());
    const auto* container=catalog.findItem("spell container");QVERIFY(container);
    const QString droppedSpell=ei::ItemSpellConstructorCatalog::formatItemCode(
        container->name,QString(),ei::ItemSpellConstructorCatalog::formatSpellCode(
            overLimit.spell.prototypeCode,overLimit.spell.runeCodes));
    QVERIFY2(document->setValue("units.monsters",rowKey,"fld.29.0",
        QStringLiteral("[\"")+droppedSpell+QStringLiteral("\"]"),&diagnostic),qPrintable(diagnostic));
    bool capacityWarning=false;
    for(const auto& finding:document->validate())
        if(finding.rowKey==rowKey&&finding.message.contains(droppedSpell)
           &&(finding.code=="database.item.spell_energy_exceeded"
              ||finding.code=="database.item.spell_difficulty_exceeded"))
            capacityWarning=true;
    QVERIFY(!capacityWarning);

    const QString tooManyRunes=QStringLiteral("spell container[speed{e3;e3;e3;e3;e3;e3;e3;e3;e3}]");
    QVERIFY2(document->setValue("units.monsters",rowKey,"fld.29.0",
        QStringLiteral("[\"")+tooManyRunes+QStringLiteral("\"]"),&diagnostic),qPrintable(diagnostic));
    bool syntaxWarning=false;
    for(const auto& finding:document->validate())
        syntaxWarning|=finding.rowKey==rowKey
            &&finding.code==QStringLiteral("database.item.syntax_invalid")
            &&finding.message.contains(tooManyRunes);
    QVERIFY(syntaxWarning);
}

void ProfileDatabaseTests::gameTextCatalogComposesProfileOverlays()
{
    QTemporaryDir directory;QVERIFY(directory.isValid());
    QDir root(directory.path());QVERIFY(root.mkpath("base"));QVERIFY(root.mkpath("jmv"));
    write(root.filePath("base/texts.res"),resArchive({
        {"SPELL fireball",QByteArray("Base Fireball\r\nDescription")},
        {"MATERIAL Iron",QByteArray("Base Iron\r\nDescription")}
    }));
    write(root.filePath("jmv/texts.res"),resArchive({
        {"SPELL fireball",QByteArray("JMV Fireball\r\nDescription")}
    }));
    const ei::SGameTextCatalog catalog=ei::loadGameTextCatalog(
        {root.filePath("jmv"),root.filePath("base")});
    QCOMPARE(catalog.entries.value("SPELL fireball"),QString("JMV Fireball\r\nDescription"));
    QCOMPARE(catalog.entries.value("MATERIAL Iron"),QString("Base Iron\r\nDescription"));
}

int main(int argc, char** argv)
{
    // This save/document suite also exercises database copy actions. Protect
    // the owner's clipboard before QApplication initializes its native backend.
    ei::TestClipboardSession clipboard(true);
    if(!clipboard.prepared()) return 2;
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    if(!clipboard.install()) return 2;
    class HiddenWidgetFilter final : public QObject
    {
        bool eventFilter(QObject* object, QEvent* event) override
        {
            if(event->type() == QEvent::Polish)
                if(QWidget* widget = qobject_cast<QWidget*>(object))
                    widget->setAttribute(Qt::WA_DontShowOnScreen);
            return false;
        }
    } hiddenWidgets;
    app.installEventFilter(&hiddenWidgets);
    ProfileDatabaseTests tests;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tests, argc, argv);
}
#include "profile_database_tests.moc"

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>

#include <algorithm>

#include "mob_family_validation.h"
#include "profile_database.h"

namespace {
QByteArray section(quint32 type, const QByteArray& payload = QByteArray())
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << type << quint32(8 + payload.size());
    bytes.append(payload);
    return bytes;
}

QByteArray u32(quint32 value)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << value;
    return bytes;
}

QByteArray byteValue(quint8 value)
{
    return QByteArray(1, char(value));
}

QByteArray stringArrayPayload(const QStringList& values)
{
    QByteArray bytes = u32(quint32(values.size()));
    for(const QString& value : values)
        bytes += section(0, value.toLatin1());
    return bytes;
}

QStringList wearableParts(bool includeHair = true)
{
    QStringList parts = {
        "bd", "hd", "hp", "lh1", "lh2", "lh3", "ll1", "ll2", "ll3",
        "rh1", "rh2", "rh3", "rl1", "rl2", "rl3"
    };
    if(includeHair)
        parts.insert(3, "hr");
    return parts;
}

QByteArray unitObject(quint32 nid, const QByteArray& name,
                      const QByteArray& prototype, const QByteArray& model,
                      bool importMode, const QStringList& bodyParts,
                      const QStringList& armor = QStringList(),
                      const QStringList& weapons = QStringList(),
                      bool includeBodyPartsRecord = true)
{
    QByteArray object = section(0xB002, u32(nid))
        + section(0xB003, u32(50))
        + section(0xB004, name)
        + section(0xB006, model);
    if(includeBodyPartsRecord)
        object += section(0xB00D, stringArrayPayload(bodyParts));
    object += section(0xBBBB0000,
        section(0xBBBB0002, prototype)
        + section(0xBBBB0008, stringArrayPayload(weapons))
        + section(0xBBBB0009, stringArrayPayload(armor))
        + section(0xBBBB000A, byteValue(importMode ? 1 : 0)));
    return section(0xB001, object);
}

QByteArray mobWithUnits(const QList<QByteArray>& units)
{
    QByteArray objects;
    for(const QByteArray& unit : units)
        objects += unit;
    return section(0xA000, section(0xC000) + section(0xB000, objects));
}

QSharedPointer<const ei::SProfileDatabaseSnapshot> wearableDatabase(
    const QString& prototype, const QString& model, const QStringList& wears,
    int hairIndex = -1, const QString& helmetPrototype = "helmet",
    int helmetGeometry = -1)
{
    QSharedPointer<ei::SProfileDatabaseSnapshot> database(new ei::SProfileDatabaseSnapshot);
    database->profileId = "test";
    database->providerId = "test.database";
    database->cacheKey = "bodypart-test";
    ei::SMonsterDatabaseRecord monster;
    monster.name = prototype;
    monster.baseRace = "Test race";
    monster.wears = wears;
    monster.hairIndex = hairIndex;
    database->monsters.insert(prototype.toLower(), monster);
    ei::SRaceModelDatabaseRecord race;
    race.name = monster.baseRace;
    race.model = model;
    database->raceModels.insert(monster.baseRace.toLower(), race);
    ei::SItemDatabaseRecord helmet;
    helmet.name = helmetPrototype;
    helmet.category = "armor";
    helmet.type = "helm";
    helmet.textureType1 = helmetGeometry;
    database->items.insert(helmet.name.toLower(), helmet);
    return database;
}

QByteArray mobWithObject(quint32 nid)
{
    const QByteArray object = section(0xB001,
        section(0xB002, u32(nid)) + section(0xB003, u32(50))
        + section(0xB004, QByteArray("unit")));
    return section(0xA000, section(0xC000) + section(0xB000, object));
}

QByteArray mobWithObjects(quint32 firstNid, quint32 secondNid)
{
    const auto object = [](quint32 nid, const QByteArray& name)
    {
        return section(0xB001,
            section(0xB002, u32(nid)) + section(0xB003, u32(50))
            + section(0xB004, name));
    };
    return section(0xA000, section(0xC000)
        + section(0xB000, object(firstNid, "first") + object(secondNid, "second")));
}

QByteArray encryptedScript(const QByteArray& script, quint32 key = 0x12345678)
{
    QByteArray encrypted = u32(key);
    for(char character : script)
    {
        quint32 temporaryKey = ((((key * 13) << 4) + key) << 8) - key;
        key += (temporaryKey << 2) + 2531011;
        temporaryKey = key >> 16;
        encrypted.append(char(character ^ char(temporaryKey)));
    }
    return section(0xACCEECCB, encrypted);
}

QByteArray mobWithScript(const QByteArray& script)
{
    return section(0xA000, encryptedScript(script));
}

QByteArray mobWithMagicTrap(bool includeDiplomacy)
{
    const QByteArray trap = section(0xBBAB0000,
        section(0xB002, u32(150)) + section(0xB004, QByteArray("test trap")));
    QByteArray payload = section(0xC000) + section(0xB000, trap);
    if(includeDiplomacy)
        payload += section(0xDDDDDDD1,
            section(0xDDDDDDD2, QByteArray(32 * 32 * int(sizeof(quint32)), '\0')));
    return section(0xA000, payload);
}

QString writeMob(const QString& directory, const QString& name, const QByteArray& bytes)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return QString();
    return QFileInfo(path).absoluteFilePath();
}
}

class MobFamilyValidationTests : public QObject
{
    Q_OBJECT
private slots:
    void workspaceCopyShadowsInstalledLogicalMob();
    void renamedLoadedPrimaryReplacesInstalledCatalogPrimary();
    void differentLogicalNamesRemainIndependent();
    void missingScriptDependencyReportsExactReferenceSource();
    void separatelyLoadedMapSetsAreNotReportedAsOneFamily();
    void multipleLoadedPrimaryMobsAreReported();
    void duplicateIdsWithinOneMobAreAuthoritativeWarnings();
    void magicTrapRequiresContainingMobDiplomacy();
    void zone3HelmetMissingHairSelectionIsReported();
    void sameHairHelmetCombinationOutsideZone3IsReported();
    void otherHelmetGeometryDoesNotReportMissingHair();
    void hairlessHelmetedTemplateDoesNotReportMissingHair();
    void unconfirmedAuthoredHairPeerDoesNotReportMissingHair();
    void wearableAllPartsMasksAreCriticalButCreatureMasksAreIgnored();
    void coreOverlapAndDuplicateFindingsPreserveExactParts();
    void importWithoutTemplateHairEvidenceDoesNotReportMissingHair();
    void databaseCreatureResolutionSuppressesWearableMaskRules();
};

void MobFamilyValidationTests::workspaceCopyShadowsInstalledLogicalMob()
{
    QTemporaryDir workspace;
    QTemporaryDir installed;
    QVERIFY(workspace.isValid());
    QVERIFY(installed.isValid());
    const QString installedPrimary = writeMob(installed.path(), "zone7.mob", mobWithObject(41));
    const QString workspacePrimary = writeMob(workspace.path(), "zone7.mob", mobWithObject(77));
    const QString supplemental = writeMob(workspace.path(), "Autozone7altar.mob", mobWithObject(77));
    QVERIFY(!installedPrimary.isEmpty() && !workspacePrimary.isEmpty() && !supplemental.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "zone7-copy";
    request.profileId = "test";
    request.seedMobPaths << workspacePrimary << supplemental;
    request.mappedPrimaryMobPath = installedPrimary;
    request.preferredMobPath = workspacePrimary;
    request.workspaceRoots << workspace.path();
    request.mapRoots << installed.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QCOMPARE(report.members.size(), 2);
    int primaryCount = 0;
    for(const SMobFamilyMember& member : report.members)
    {
        if(member.logicalName == "zone7.mob")
        {
            ++primaryCount;
            QCOMPARE(QDir::cleanPath(member.path), QDir::cleanPath(workspacePrimary));
            QVERIFY(member.shadowedPaths.contains(installedPrimary, Qt::CaseInsensitive));
        }
    }
    QCOMPARE(primaryCount, 1);
    QVERIFY(report.nidOwners.contains(77));
    QCOMPARE(report.nidOwners.value(77).size(), 2);
    QVERIFY(!report.nidOwners.contains(41));
}

void MobFamilyValidationTests::renamedLoadedPrimaryReplacesInstalledCatalogPrimary()
{
    QTemporaryDir workspace;
    QTemporaryDir installed;
    QVERIFY(workspace.isValid());
    QVERIFY(installed.isValid());
    const QString installedPrimary = writeMob(installed.path(), "zone3obr.mob",
                                               mobWithObject(3470));
    const QString testPrimary = writeMob(workspace.path(), "zone3obrtest.mob",
                                         mobWithObject(202));
    QVERIFY(!installedPrimary.isEmpty() && !testPrimary.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "gz3obr";
    request.profileId = "test";
    request.seedMobPaths << testPrimary;
    request.mappedPrimaryMobPath = installedPrimary;
    request.preferredMobPath = testPrimary;
    request.workspaceRoots << workspace.path();
    request.mapRoots << installed.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QCOMPARE(report.members.size(), 1);
    QCOMPARE(QDir::cleanPath(report.members.first().path), QDir::cleanPath(testPrimary));
    QCOMPARE(report.members.first().inclusionReason, QString("explicit seed"));
    QVERIFY(report.nidOwners.contains(202));
    QVERIFY(!report.nidOwners.contains(3470));
}

void MobFamilyValidationTests::differentLogicalNamesRemainIndependent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithObject(88);
    const QString first = writeMob(directory.path(), "first.mob", bytes);
    const QString second = writeMob(directory.path(), "second.mob", bytes);
    SMobFamilyValidationRequest request;
    request.familyLabel = "different-names";
    request.profileId = "test";
    request.seedMobPaths << first << second;
    request.workspaceRoots << directory.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    QCOMPARE(report.members.size(), 2);
    QCOMPARE(report.nidOwners.value(88).size(), 2);
}

void MobFamilyValidationTests::missingScriptDependencyReportsExactReferenceSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString primary = writeMob(directory.path(), "zone14.mob",
        mobWithScript("first()\nAddMob(\"Autozone14dm4.mob\")\nlast()"));
    QVERIFY(!primary.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "zone14";
    request.profileId = "test";
    request.seedMobPaths << primary;
    request.workspaceRoots << directory.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item) { return item.code == "family.missing_mob"; });
    QVERIFY(finding != report.findings.cend());
    QVERIFY(finding->message.contains("MOB file \"Autozone14dm4.mob\" was not found"));
    QVERIFY(finding->message.contains("zone14.mob script line 2"));
    QVERIFY(finding->message.contains("AddMob(\"Autozone14dm4.mob\")"));
    QVERIFY(finding->locations.contains(QString("Reference source: %1").arg(primary)));
    QVERIFY(finding->locations.contains(
        "Searched the active workspace and configured profile Maps sources"));
}

void MobFamilyValidationTests::separatelyLoadedMapSetsAreNotReportedAsOneFamily()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString zone4 = writeMob(directory.path(), "zone4.mob", mobWithObject(777));
    const QString zone17 = writeMob(directory.path(), "zone17.mob", mobWithObject(777));

    SMobFamilyValidationRequest request;
    request.familyLabel = "gz4g";
    request.profileId = "test";
    request.seedMobPaths << zone4 << zone17;
    request.mappedPrimaryMobPath = zone4;
    request.workspaceRoots << directory.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto mixed = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item) { return item.code == "workspace.unrelated_loaded_mobs"; });
    QVERIFY(mixed != report.findings.cend());
    QVERIFY(mixed->locations.contains(zone17));

    const auto collision = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item) { return item.code == "nid.workspace_collision"; });
    QVERIFY(collision != report.findings.cend());
    QVERIFY(collision->message.contains("separately loaded map sets"));
    QVERIFY(collision->message.contains("not one gz4g runtime family"));
}

void MobFamilyValidationTests::duplicateIdsWithinOneMobAreAuthoritativeWarnings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMob(directory.path(), "duplicate.mob",
                                  mobWithObjects(568, 568));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "duplicate-test";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "nid.duplicate_within_mob"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(finding->message.contains("NID 568 is used by 2 objects in duplicate.mob"));
    QCOMPARE(finding->locations.size(), 2);
}

void MobFamilyValidationTests::magicTrapRequiresContainingMobDiplomacy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing = writeMob(directory.path(), "autozone2newtest.mob",
                                     mobWithMagicTrap(false));
    const QString present = writeMob(directory.path(), "autozone2withdip.mob",
                                     mobWithMagicTrap(true));
    QVERIFY(!missing.isEmpty() && !present.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "trap-diplomacy";
    request.profileId = "test";
    request.seedMobPaths << missing;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "diplomacy.magic_trap_table_missing"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(finding->message.contains("autozone2newtest.mob"));
    QVERIFY(finding->message.contains("magic trap NID 150"));
    QVERIFY(finding->message.contains("MOB diplomacy table"));
    QVERIFY(finding->hasNid);
    QCOMPARE(finding->nid, quint32(150));
    QVERIFY(std::any_of(finding->locations.cbegin(), finding->locations.cend(),
        [&missing](const QString& location)
        { return location.startsWith(missing + ":") && location.contains("MAGIC_TRAP"); }));

    request.seedMobPaths = QStringList() << present;
    report = CMobFamilyValidator::validate(request);
    finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "diplomacy.magic_trap_table_missing"; });
    QVERIFY(finding == report.findings.cend());
}

void MobFamilyValidationTests::multipleLoadedPrimaryMobsAreReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = writeMob(directory.path(), "zone4.mob", mobWithObject(100));
    const QString second = writeMob(directory.path(), "zone17.mob", mobWithObject(200));
    QVERIFY(!first.isEmpty() && !second.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "mixed-workspace";
    request.profileId = "test";
    request.seedMobPaths << first << second;
    request.mappedPrimaryMobPath = first;
    request.workspaceRoots << directory.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "workspace.multiple_primary_mobs"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
    QCOMPARE(finding->locations.size(), 2);
    QVERIFY(finding->locations.contains(first));
    QVERIFY(finding->locations.contains(second));
}

void MobFamilyValidationTests::zone3HelmetMissingHairSelectionIsReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray goodBytes = mobWithUnits({unitObject(
        3470, "Boss", "Human Gipath NPC Boss F5", "unhuma", false,
        wearableParts(true), {"mhh.troll green"})});
    const QByteArray badBytes = mobWithUnits({unitObject(
        800004, "Boss-1000000004", "Human Gipath NPC Boss F5", "unhuma", false,
        wearableParts(false), {"mhh.troll green"})});
    const QString good = writeMob(directory.path(), "zone3obr.mob", goodBytes);
    const QString bad = writeMob(directory.path(), "Autozone3obradd.mob", badBytes);
    QVERIFY(!good.isEmpty() && !bad.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "zone3obr-hair-control";
    request.profileId = "jamais-vu";
    request.seedMobPaths << good << bad;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Gipath NPC Boss F5", "unhuma", {"mhh.troll green"}, 1, "mhh", 6);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVector<SMobFamilyFinding> matches;
    for(const SMobFamilyFinding& finding : report.findings)
        if(finding.code == "mob.semantic.missing_hair_selection")
            matches.append(finding);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(matches.first().hasNid);
    QCOMPARE(matches.first().nid, quint32(800004));
    QCOMPARE(matches.first().message,
             QString("NID 800004: missing 'hr' for hair 01 with helmet 06."));
    QVERIFY(!matches.first().message.contains("Boss-1000000004"));
    QVERIFY(!matches.first().message.contains("Human Gipath NPC Boss F5"));
    QVERIFY(!matches.first().message.contains("serialized parts"));
    QVERIFY(std::any_of(matches.first().locations.cbegin(), matches.first().locations.cend(),
        [&bad](const QString& location) { return location.startsWith(bad + ":"); }));

    QFile goodFile(good), badFile(bad);
    QVERIFY(goodFile.open(QIODevice::ReadOnly));
    QVERIFY(badFile.open(QIODevice::ReadOnly));
    QCOMPARE(goodFile.readAll(), goodBytes);
    QCOMPARE(badFile.readAll(), badBytes);
}

void MobFamilyValidationTests::sameHairHelmetCombinationOutsideZone3IsReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        44001, "later-map actor", "Human Open Helmet F1", "unhuma", false,
        wearableParts(false), {"mhh.troll green"})});
    const QString path = writeMob(directory.path(), "zone4.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "same-resolved-head-combination";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Open Helmet F1", "unhuma", {"mhh.troll green"}, 1, "mhh", 6);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "mob.semantic.missing_hair_selection"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
    QCOMPARE(finding->message,
             QString("NID 44001: missing 'hr' for hair 01 with helmet 06."));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void MobFamilyValidationTests::otherHelmetGeometryDoesNotReportMissingHair()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        44002, "closed helmet actor", "Human Closed Helmet F1", "unhuma", false,
        wearableParts(false), {"llh.troll green"})});
    const QString path = writeMob(directory.path(), "helmet12.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "different-head-combination";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Closed Helmet F1", "unhuma", {"llh.troll green"}, 1, "llh", 12);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.missing_hair_selection"; }));
}

void MobFamilyValidationTests::hairlessHelmetedTemplateDoesNotReportMissingHair()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({
        unitObject(800004, "hairless control", "Human Hairless F0", "unhuma", false,
                   wearableParts(true), {"helmet.iron"}),
        unitObject(800005, "hairless helmeted actor", "Human Hairless F0", "unhuma", false,
                   wearableParts(false), {"helmet.iron"})
    });
    const QString path = writeMob(directory.path(), "hairless-helmet.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "hairless-template-control";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Hairless F0", "unhuma", {"helmet.iron"}, -1);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.missing_hair_selection"; }));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void MobFamilyValidationTests::unconfirmedAuthoredHairPeerDoesNotReportMissingHair()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({
        unitObject(800005, "authored hair control", "Human Authored Hair F0", "unhuma", false,
                   wearableParts(true), {"helmet.iron"}),
        unitObject(800006, "helmeted actor", "Human Authored Hair F0", "unhuma", false,
                   wearableParts(false), {"helmet.iron"})
    });
    const QString path = writeMob(directory.path(), "authored-hair-helmet.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "authored-hair-without-peer";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Authored Hair F0", "unhuma", {"helmet.iron"}, 2);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.missing_hair_selection"; }));
}

void MobFamilyValidationTests::wearableAllPartsMasksAreCriticalButCreatureMasksAreIgnored()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({
        unitObject(10, "empty orc", "OrcMaleF3", "unorma", false, {}),
        unitObject(11, "missing human", "Human F1", "unhuma", false, {}, {}, {}, false),
        unitObject(12, "integral dragon", "DragonGreenM12", "unmodg", true, {})
    });
    const QString path = writeMob(directory.path(), "wearable-empty.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "wearable-empty";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto empty = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.wearable_all_parts_enabled"; });
    QVERIFY(empty != report.findings.cend());
    QCOMPARE(empty->severity, EMobFamilyFindingSeverity::Critical);
    QCOMPARE(empty->nid, quint32(10));
    QCOMPARE(empty->message,
             QString("NID 10: empty body-part list enables every assembly member, including overlapping alternatives."));
    const auto missing = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.wearable_bodyparts_missing"; });
    QVERIFY(missing != report.findings.cend());
    QCOMPARE(missing->severity, EMobFamilyFindingSeverity::Critical);
    QCOMPARE(missing->nid, quint32(11));
    QCOMPARE(missing->message,
             QString("NID 11: OBJBODYPARTS is missing, so every assembly member is enabled."));
    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.hasNid && finding.nid == 12
                 && finding.code.startsWith("mob.semantic.wearable_"); }));
}

void MobFamilyValidationTests::coreOverlapAndDuplicateFindingsPreserveExactParts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList parts = wearableParts(true);
    parts.removeAll("lh2");
    parts << "rh3.pike00" << "rh3.tube00" << "hd" << "future.extension";
    const QByteArray bytes = mobWithUnits({unitObject(
        6257, "DriadDarkM9-6256", "DriadDarkM15", "unhuma", false, parts)});
    const QString path = writeMob(directory.path(), "zone12.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "body-part-semantics";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto core = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.missing_core_bodyparts"; });
    QVERIFY(core != report.findings.cend());
    QCOMPARE(core->severity, EMobFamilyFindingSeverity::Warning);
    QCOMPARE(core->message, QString("NID 6257: missing standard anatomy part(s): lh2."));

    const auto overlap = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.overlapping_unit_parts"; });
    QVERIFY(overlap != report.findings.cend());
    QCOMPARE(overlap->severity, EMobFamilyFindingSeverity::Warning);
    QCOMPARE(overlap->message,
             QString("NID 6257: mutually overlapping parts are enabled for 'rh3': rh3.pike00, rh3.tube00."));

    const auto duplicate = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.duplicate_bodypart_token"; });
    QVERIFY(duplicate != report.findings.cend());
    QCOMPARE(duplicate->severity, EMobFamilyFindingSeverity::Info);
    QCOMPARE(duplicate->message,
             QString("NID 6257: body-part token 'hd' is duplicated 2 times."));
    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code.contains("unknown_bodypart"); }));
    QVERIFY(!overlap->message.contains("future.extension"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void MobFamilyValidationTests::importWithoutTemplateHairEvidenceDoesNotReportMissingHair()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({
        unitObject(21, "imported orc", "OrcF3", "unorma", true,
                   wearableParts(false), {"helmet.iron"}),
        unitObject(22, "normal orc", "OrcF3", "unorma", false,
                   wearableParts(false), {"helmet.iron"})
    });
    const QString path = writeMob(directory.path(), "import-source.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "import-source";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase("OrcF3", "unorma", {});
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.missing_hair_selection"; }));
}

void MobFamilyValidationTests::databaseCreatureResolutionSuppressesWearableMaskRules()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        31, "database creature", "DragonF0", "unmodg", false, {})});
    const QString path = writeMob(directory.path(), "database-creature.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "database-creature";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase("DragonF0", "unmodg", {});
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.hasNid && finding.nid == 31
                 && finding.code.startsWith("mob.semantic.wearable_"); }));
}

QTEST_APPLESS_MAIN(MobFamilyValidationTests)
#include "mob_family_validation_tests.moc"

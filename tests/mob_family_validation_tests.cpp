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
                      bool includeBodyPartsRecord = true,
                      const QList<QByteArray>& logicSlots = QList<QByteArray>())
{
    QByteArray object = section(0xB002, u32(nid))
        + section(0xB003, u32(50))
        + section(0xB004, name)
        + section(0xB006, model);
    if(includeBodyPartsRecord)
        object += section(0xB00D, stringArrayPayload(bodyParts));
    QByteArray unit =
        section(0xBBBB0002, prototype)
        + section(0xBBBB0008, stringArrayPayload(weapons))
        + section(0xBBBB0009, stringArrayPayload(armor))
        + section(0xBBBB000A, byteValue(importMode ? 1 : 0));
    for(const QByteArray& logic : logicSlots)
        unit += logic;
    object += section(0xBBBB0000, unit);
    return section(0xB001, object);
}

QByteArray unitLogic(quint32 behavior, bool enabled, int patrolPointCount)
{
    QByteArray payload = section(0xBBBC0003, u32(behavior))
        + section(0xBBBC0007, byteValue(enabled ? 1 : 0));
    for(int index = 0; index < patrolPointCount; ++index)
        payload += section(0xBBBD0000);
    return section(0xBBBC0000, payload);
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

QByteArray mobWithObjectAndParent(quint32 nid, quint32 parentId)
{
    const QByteArray object = section(0xB001,
        section(0xB002, u32(nid)) + section(0xB003, u32(50))
        + section(0xB004, QByteArray("unit")) + section(0xB012, u32(parentId)));
    return section(0xA000, section(0xC000) + section(0xB000, object));
}

QByteArray mobWithDiplomacy(const QVector<QVector<quint32>>& values)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    for(int row = 0; row < 32; ++row)
        for(int column = 0; column < 32; ++column)
            stream << values.value(row).value(column);
    QStringList names;
    for(int index = 0; index < 32; ++index)
        names.append(QString("Faction %1").arg(index));
    return section(0xA000, section(0xDDDDDDD1,
        section(0xDDDDDDD2, data) + section(0xDDDDDDD3, stringArrayPayload(names))));
}

QByteArray resArchive(const QMap<QString, QByteArray>& members)
{
    struct Entry
    {
        quint32 size = 0;
        quint32 offset = 0;
        quint16 nameLength = 0;
        quint32 nameOffset = 0;
    };
    QVector<Entry> entries;
    QByteArray names;
    QByteArray data(16, '\0');
    for(auto member = members.constBegin(); member != members.constEnd(); ++member)
    {
        Entry entry;
        entry.size = quint32(member.value().size());
        entry.offset = quint32(data.size());
        const QByteArray encodedName = member.key().toLatin1();
        entry.nameLength = quint16(encodedName.size());
        entry.nameOffset = quint32(names.size());
        entries.append(entry);
        names.append(encodedName);
        data.append(member.value());
        data.append(QByteArray((16 - data.size() % 16) % 16, '\0'));
    }
    const quint32 tableOffset = quint32(data.size());
    QDataStream tableStream(&data, QIODevice::Append);
    tableStream.setByteOrder(QDataStream::LittleEndian);
    for(const Entry& entry : entries)
        tableStream << quint32(0xffffffffu) << entry.size << entry.offset
                    << quint32(0) << entry.nameLength << entry.nameOffset;
    data.append(names);
    QDataStream headerStream(&data, QIODevice::ReadWrite);
    headerStream.setByteOrder(QDataStream::LittleEndian);
    headerStream << quint32(0x019CE23C) << quint32(entries.size())
                 << tableOffset << quint32(names.size());
    return data;
}

QByteArray assemblyLink(const QStringList& components)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << qint32(components.size());
    for(const QString& component : components)
    {
        const QByteArray encoded = component.toLatin1();
        stream << qint32(encoded.size());
        stream.writeRawData(encoded.constData(), encoded.size());
        stream << qint32(0);
    }
    return data;
}

QByteArray figureWithCrown(float crown)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0x38474946)
           << quint32(1) << quint32(1) << quint32(1)
           << quint32(3) << quint32(3) << quint32(0)
           << quint32(0) << quint32(0) << quint32(0);
    for(int group = 0; group < 3; ++group)
        for(int morph = 0; morph < 8; ++morph)
            stream << float(0.0f) << float(0.0f)
                   << float(group == 2 ? crown : 0.0f);
    for(int morph = 0; morph < 8; ++morph)
        stream << float(1.0f);
    for(int axis = 0; axis < 3; ++axis)
        for(int morph = 0; morph < 8; ++morph)
            for(int lane = 0; lane < 4; ++lane)
                stream << float(axis == 2 && lane < 3 ? crown : 0.0f);
    for(int value = 0; value < 16; ++value)
        stream << float(value % 4 == 2 ? 1.0f : 0.0f);
    stream << float(0.0f) << float(0.0f);
    stream << quint16(0) << quint16(1) << quint16(2);
    for(quint16 vertex = 0; vertex < 3; ++vertex)
        stream << vertex << quint16(0) << quint16(0);
    return data;
}

void setHairGeometry(SMobFamilyValidationRequest* request,
                     const QString& model, const QString& hair,
                     ei::EHairCrownGeometry geometry)
{
    request->assemblyHairGeometryByPart.insert(
        ei::assemblyHairGeometryKey(model, hair), geometry);
}
}

class MobFamilyValidationTests : public QObject
{
    Q_OBJECT
private slots:
    void authoredZeroNidIsCriticalAndBlocksRuntimePreflight();
    void positiveNidWithNoParentSentinelIsClean();
    void authoringPolicyAndReplacementSuggestionsRejectZero();
    void workspaceCopyShadowsInstalledLogicalMob();
    void renamedLoadedPrimaryReplacesInstalledCatalogPrimary();
    void differentLogicalNamesRemainIndependent();
    void missingScriptDependencyReportsExactReferenceSource();
    void separatelyLoadedMapSetsAreNotReportedAsOneFamily();
    void multipleLoadedPrimaryMobsAreReported();
    void duplicateIdsWithinOneMobAreAuthoritativeWarnings();
    void magicTrapRequiresContainingMobDiplomacy();
    void directionalAndSelfDiplomacyAreSupportedWithWarnings();
    void zone3HelmetHairFallbackIsReported();
    void orcHairRootDoesNotSelectDatabaseHair();
    void coveredDatabaseHairDoesNotReportFallback();
    void coveredSkeletonHairDoesNotReportFallback();
    void databaseEquippedLegacyAssemblyUsesSameHairRule();
    void helmetHairReplacementAvoidsMissingRootFallback();
    void unavailableNativeHairAvoidsMissingRootFallback();
    void resourceBackedAssemblyEvidenceCoversUnloadedFamilyModel();
    void hairlessHelmetedTemplateDoesNotReportMissingHair();
    void missingAssemblyEvidenceDoesNotReportMissingHair();
    void wearableAllPartsMasksAreCriticalButCreatureMasksAreIgnored();
    void coreOverlapAndDuplicateFindingsPreserveExactParts();
    void importWithoutTemplateHairEvidenceDoesNotReportMissingHair();
    void databaseCreatureResolutionSuppressesWearableMaskRules();
    void jmvUnitPerkTableFailuresAreCritical();
    void missingDatabasePrototypeIsCrashClassButDoesNotBlockRuntime();
    void patrolBehaviorWithoutPointsReportsNavigableCrashRisk();
    void disabledEmptyPatrolIsWarningAndValidRoutesAreClean();
};

void MobFamilyValidationTests::patrolBehaviorWithoutPointsReportsNavigableCrashRisk()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMob(directory.path(), "missing-patrol.mob",
        mobWithUnits({unitObject(67000001, "Broken patrol guard", "Guard", "unhuma",
            false, wearableParts(), {}, {}, true,
            {unitLogic(2, false, 0), unitLogic(0, false, 0),
             unitLogic(2, true, 0), unitLogic(0, false, 0), unitLogic(0, false, 0)})}));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "missing-patrol";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVector<SMobFamilyFinding> findings;
    for(const SMobFamilyFinding& finding : report.findings)
        if(finding.code == "unit.patrol_points_missing") findings.append(finding);
    QCOMPARE(findings.size(), 2);
    for(const SMobFamilyFinding& finding : findings)
    {
        QCOMPARE(finding.severity, EMobFamilyFindingSeverity::Critical);
        QCOMPARE(finding.category, QString("Behavior"));
        QVERIFY(finding.hasNid);
        QCOMPARE(finding.nid, quint32(67000001));
        QVERIFY(finding.locations.size() == 1);
        QVERIFY(finding.locations.first().startsWith(path + ":"));
        QVERIFY(finding.message.contains("Broken patrol guard"));
        QVERIFY(finding.message.contains("NID 67000001"));
        QVERIFY(finding.message.contains("may terminate", Qt::CaseInsensitive));
    }
    QVERIFY(findings.at(0).message.contains("Logic 0"));
    QVERIFY(findings.at(1).message.contains("Logic 2"));
}

void MobFamilyValidationTests::disabledEmptyPatrolIsWarningAndValidRoutesAreClean()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMob(directory.path(), "dormant-patrol.mob",
        mobWithUnits({unitObject(41, "Dormant patrol", "Guard", "unhuma",
            false, wearableParts(), {}, {}, true,
            {unitLogic(0, true, 0), unitLogic(2, false, 0),
             unitLogic(2, true, 1), unitLogic(1, true, 0), unitLogic(0, false, 0)})}));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "dormant-patrol";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVector<SMobFamilyFinding> findings;
    for(const SMobFamilyFinding& finding : report.findings)
        if(finding.code == "unit.patrol_points_missing") findings.append(finding);
    QCOMPARE(findings.size(), 1);
    QCOMPARE(findings.first().severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(findings.first().message.contains("Logic 1"));
    QVERIFY(findings.first().message.contains("disabled", Qt::CaseInsensitive));
    QVERIFY(findings.first().hasNid);
    QCOMPARE(findings.first().nid, quint32(41));
}

void MobFamilyValidationTests::authoredZeroNidIsCriticalAndBlocksRuntimePreflight()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMob(directory.path(), "zero.mob",
                                  mobWithObjectAndParent(0, 0));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "zero-nid";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item) { return item.code == "nid.zero_reserved"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Critical);
    QCOMPARE(finding->category, QString("Object ID"));
    QVERIFY(finding->hasNid);
    QCOMPARE(finding->nid, quint32(0));
    QCOMPARE(finding->locations.size(), 1);
    QVERIFY(finding->locations.first().startsWith(path + ":"));
    QVERIFY(finding->message.contains("Object \"unit\" in zero.mob has NID 0"));
    QVERIFY(finding->message.contains("NID 0 is invalid for map objects"));
    QVERIFY(finding->message.contains("may cause the game to crash"));
    QVERIFY(finding->message.contains("unique positive NID"));
    QVERIFY(report.hasBlockingRuntimeFindings());
    QCOMPARE(report.blockingRuntimeFindings().size(), 1);
}

void MobFamilyValidationTests::positiveNidWithNoParentSentinelIsClean()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMob(directory.path(), "positive.mob",
                                  mobWithObjectAndParent(48, 0));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "positive-nid";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item) { return item.code == "nid.zero_reserved"; }));
    QVERIFY(!report.hasBlockingRuntimeFindings());
    QVERIFY(report.nidOwners.contains(48));
    QVERIFY(!report.nidOwners.contains(0));
}

void MobFamilyValidationTests::authoringPolicyAndReplacementSuggestionsRejectZero()
{
    QVERIFY(!CMobFamilyValidator::isAuthorableObjectNid(0));
    QVERIFY(CMobFamilyValidator::isAuthorableObjectNid(1));

    CMobFamilyValidationReport report;
    SMobFamilyMember member;
    member.path = QFileInfo("zero-range.mob").absoluteFilePath();
    SMobIdRangeSnapshot range;
    range.minimum = 0;
    range.maximum = 2;
    range.effective = true;
    member.ranges.append(range);
    report.members.append(member);
    quint32 suggestion = 0;
    QVERIFY(report.suggestReplacementNid(member.path, &suggestion));
    QCOMPARE(suggestion, quint32(1));
}

void MobFamilyValidationTests::jmvUnitPerkTableFailuresAreCritical()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString prototype = "Human Test";
    const QString path = writeMob(directory.path(), "unit-perk.mob", mobWithUnits({unitObject(
        997569, "Titan guard", prototype.toLatin1(), "unhuma", false, wearableParts())}));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "jmv-unit-perk";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    auto database = qSharedPointerConstCast<ei::SProfileDatabaseSnapshot>(
        wearableDatabase(prototype, "unhuma", {}));
    database->providerId = "jmv.v1";
    database->jmvUnitPerkTableStatus = ei::EJmvUnitPerkTableStatus::Missing;
    database->jmvUnitPerkDiagnostic = "tables.lua was not found";
    request.database = database;

    CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "jmv.tables.unit_perk_unavailable"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Critical);

    database->jmvUnitPerkTableStatus = ei::EJmvUnitPerkTableStatus::Valid;
    database->jmvUnitPerkDiagnostic.clear();
    report = CMobFamilyValidator::validate(request);
    finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "jmv.tables.unit_perk_row_missing"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
    QCOMPARE(finding->category, QString("JMV"));
    QCOMPARE(finding->message,
             QString("Character '%1' doesn't have a valid tables.lua entry.").arg(prototype));
    QVERIFY(finding->message.contains(prototype));

    database->providerId = "base.standard.v1";
    database->jmvUnitPerkTableStatus = ei::EJmvUnitPerkTableStatus::NotApplicable;
    report = CMobFamilyValidator::validate(request);
    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code.startsWith("jmv.tables."); }));
}

void MobFamilyValidationTests::missingDatabasePrototypeIsCrashClassButDoesNotBlockRuntime()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missingPrototype = "Missing Runtime Prototype";
    const QString path = writeMob(directory.path(), "missing-prototype.mob", mobWithUnits({unitObject(
        997570, "Broken unit", missingPrototype.toLatin1(), "unhuma", false, wearableParts())}));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "missing-database-prototype";
    request.profileId = "base";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase("Different Prototype", "unhuma", {});

    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "database.prototype.missing"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Critical);
    QCOMPARE(finding->category, QString("Database"));
    QVERIFY(finding->message.contains(missingPrototype));
    QVERIFY(finding->message.contains("crash", Qt::CaseInsensitive));
    QVERIFY(finding->hasNid);
    QCOMPARE(finding->nid, quint32(997570));
    QVERIFY(!finding->locations.isEmpty());
    QVERIFY(finding->locations.first().startsWith(path + ":"));
    QVERIFY(!report.hasBlockingRuntimeFindings());
    QCOMPARE(report.blockingRuntimeFindings().size(), 0);
}

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

void MobFamilyValidationTests::directionalAndSelfDiplomacyAreSupportedWithWarnings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVector<QVector<quint32>> diplomacy(32, QVector<quint32>(32, 0));
    diplomacy[1][2] = 2;
    diplomacy[2][1] = 1;
    diplomacy[3][3] = 2;
    const QString path = writeMob(directory.path(), "directional.mob",
                                  mobWithDiplomacy(diplomacy));
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "directional-diplomacy";
    request.profileId = "test";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    const auto asymmetric = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "diplomacy.asymmetric_relations"; });
    QVERIFY(asymmetric != report.findings.cend());
    QCOMPARE(asymmetric->severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(asymmetric->message.contains("supported"));
    QVERIFY(asymmetric->message.contains("Faction 1"));
    QVERIFY(asymmetric->message.contains("War"));
    QVERIFY(asymmetric->message.contains("Neutral"));

    const auto self = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "diplomacy.self_relation"; });
    QVERIFY(self != report.findings.cend());
    QCOMPARE(self->severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(self->message.contains("supported"));
    QVERIFY(self->message.contains("Faction 3"));
    QVERIFY(self->message.contains("War"));
}

void MobFamilyValidationTests::zone3HelmetHairFallbackIsReported()
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
    request.assemblyPartsByModel.insert(
        "unhuma", {"hd", "hr.01", "hd.armor06"});
    setHairGeometry(&request, "unhuma", "hr.01",
                    ei::EHairCrownGeometry::AboveHeadCrown);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVector<SMobFamilyFinding> matches;
    for(const SMobFamilyFinding& finding : report.findings)
        if(finding.code == "mob.semantic.helmet_hair_fallback")
            matches.append(finding);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().severity, EMobFamilyFindingSeverity::Warning);
    QVERIFY(matches.first().hasNid);
    QCOMPARE(matches.first().nid, quint32(800004));
    QCOMPARE(matches.first().message,
             QString("NID 800004: missing 'hr' may show hair with helmet 06."));
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

void MobFamilyValidationTests::orcHairRootDoesNotSelectDatabaseHair()
{
    struct Case { quint32 nid; const char* prototype; int hair; const char* exactHair; };
    const Case cases[] = {
        {4500, "GZOrcFemaleF11", 1, "hr.01"},
        {4634, "OrcFemaleRuneEM3", 0, "hr.00"}
    };
    for(const Case& test : cases)
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray rootOnlyBytes = mobWithUnits({unitObject(
            test.nid, "helmeted orc", test.prototype, "unorfe", false,
            wearableParts(true), {"helmet.iron"})});
        const QString rootOnlyPath = writeMob(
            directory.path(), QString("%1-root.mob").arg(test.nid), rootOnlyBytes);
        QVERIFY(!rootOnlyPath.isEmpty());

        SMobFamilyValidationRequest request;
        request.familyLabel = "orc-runtime-hair-fallback";
        request.profileId = "jamais-vu";
        request.seedMobPaths << rootOnlyPath;
        request.explicitComposition = true;
        request.includeProfileWideMobs = false;
        request.database = wearableDatabase(
            test.prototype, "unorfe", {"helmet.iron"}, test.hair, "helmet", 4);
        request.assemblyPartsByModel.insert(
            "unorfe", {"hd", test.exactHair, "hd.armor04"});
        setHairGeometry(&request, "unorfe", test.exactHair,
                        ei::EHairCrownGeometry::AboveHeadCrown);
        CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

        auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
            [](const SMobFamilyFinding& item)
            { return item.code == "mob.semantic.helmet_hair_fallback"; });
        QVERIFY(finding != report.findings.cend());
        QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
        QCOMPARE(finding->message,
                 QString("NID %1: missing '%2' may show hair with helmet 04.")
                    .arg(test.nid).arg(test.exactHair));

        QStringList exactParts = wearableParts(false);
        exactParts.insert(3, test.exactHair);
        const QByteArray exactBytes = mobWithUnits({unitObject(
            test.nid, "helmeted orc", test.prototype, "unorfe", false,
            exactParts, {"helmet.iron"})});
        const QString exactPath = writeMob(
            directory.path(), QString("%1-exact.mob").arg(test.nid), exactBytes);
        QVERIFY(!exactPath.isEmpty());
        request.seedMobPaths = QStringList() << exactPath;
        report = CMobFamilyValidator::validate(request);
        QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
            [](const SMobFamilyFinding& item)
            { return item.code == "mob.semantic.helmet_hair_fallback"; }));

        QFile rootOnlyFile(rootOnlyPath), exactFile(exactPath);
        QVERIFY(rootOnlyFile.open(QIODevice::ReadOnly));
        QVERIFY(exactFile.open(QIODevice::ReadOnly));
        QCOMPARE(rootOnlyFile.readAll(), rootOnlyBytes);
        QCOMPARE(exactFile.readAll(), exactBytes);
    }
}

void MobFamilyValidationTests::coveredDatabaseHairDoesNotReportFallback()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        294, "covered human hair", "Human Gipath Brigand CB F5", "unhuma", false,
        {"bd", "hd", "hp", "hr.01", "lh1", "lh2", "lh3", "ll1", "ll2",
         "ll3", "rh1", "rh2", "rh3", "rl1", "rl2", "rl3"},
        {"helmet.iron"})});
    const QString path = writeMob(directory.path(), "zone3obr.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "covered-human-hair-control";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Gipath Brigand CB F5", "unhuma", {"helmet.iron"}, 2, "helmet", 5);
    request.assemblyPartsByModel.insert(
        "unhuma", {"hd", "hr.01", "hr.02", "hd.armor05"});
    setHairGeometry(&request, "unhuma", "hr.02",
                    ei::EHairCrownGeometry::AtOrBelowHeadCrown);

    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void MobFamilyValidationTests::coveredSkeletonHairDoesNotReportFallback()
{
    struct Case { quint32 nid; const char* file; QStringList parts; int helmet; };
    const Case cases[] = {
        {46, "zone11.mob", wearableParts(true), 1},
        {1504, "zone9.mob", wearableParts(false), 9}
    };
    for(const Case& test : cases)
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray bytes = mobWithUnits({unitObject(
            test.nid, "covered skeleton hair", "Skeleton control", "unhusk", false,
            test.parts, {"helmet.iron"})});
        const QString path = writeMob(directory.path(), test.file, bytes);
        QVERIFY(!path.isEmpty());

        SMobFamilyValidationRequest request;
        request.familyLabel = "covered-skeleton-hair-control";
        request.profileId = "jamais-vu";
        request.seedMobPaths << path;
        request.explicitComposition = true;
        request.includeProfileWideMobs = false;
        request.database = wearableDatabase(
            "Skeleton control", "unhusk", {"helmet.iron"}, 2,
            "helmet", test.helmet);
        request.assemblyPartsByModel.insert(
            "unhusk", {"hd", "hr.02",
                        QString("hd.armor%1").arg(test.helmet, 2, 10, QChar('0'))});
        setHairGeometry(&request, "unhusk", "hr.02",
                        ei::EHairCrownGeometry::AtOrBelowHeadCrown);

        const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
        QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
            [](const SMobFamilyFinding& finding)
            { return finding.code == "mob.semantic.helmet_hair_fallback"; }));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
}

void MobFamilyValidationTests::databaseEquippedLegacyAssemblyUsesSameHairRule()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        55123, "equipped undead", "Equipped Undead", "unmoud", false,
        wearableParts(false), {"helmet.iron"})});
    const QString path = writeMob(directory.path(), "equipped-undead.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "database-equipped-legacy-assembly";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Equipped Undead", "unmoud", {"helmet.iron"}, 3, "helmet", 2);
    request.assemblyPartsByModel.insert(
        "unmoud", {"hd", "hr.03", "hd.armor02"});
    setHairGeometry(&request, "unmoud", "hr.03",
                    ei::EHairCrownGeometry::AboveHeadCrown);

    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    const auto finding = std::find_if(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& item)
        { return item.code == "mob.semantic.helmet_hair_fallback"; });
    QVERIFY(finding != report.findings.cend());
    QCOMPARE(finding->severity, EMobFamilyFindingSeverity::Warning);
    QCOMPARE(finding->message,
             QString("NID 55123: missing 'hr.03' may show hair with helmet 02."));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void MobFamilyValidationTests::helmetHairReplacementAvoidsMissingRootFallback()
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
    request.assemblyPartsByModel.insert(
        "unhuma", {"hd", "hr.01", "hd.armor12", "hr.01.armor12"});
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));
}

void MobFamilyValidationTests::unavailableNativeHairAvoidsMissingRootFallback()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        44003, "assembly without authored hair", "Human Missing Hair Asset F3", "unhufe", false,
        wearableParts(false), {"llh.troll green"})});
    const QString path = writeMob(directory.path(), "missing-native-hair.mob", bytes);
    QVERIFY(!path.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "missing-native-hair";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Missing Hair Asset F3", "unhufe", {"llh.troll green"}, 3, "llh", 12);
    request.assemblyPartsByModel.insert(
        "unhufe", {"hd", "hd.armor12"});
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));
}

void MobFamilyValidationTests::resourceBackedAssemblyEvidenceCoversUnloadedFamilyModel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithUnits({unitObject(
        44004, "unloaded family actor", "Human Resource Hair F2", "unhufe", false,
        wearableParts(false), {"llh.troll green"})});
    const QString mobPath = writeMob(directory.path(), "unloaded-family.mob", bytes);
    QVERIFY(!mobPath.isEmpty());

    const QStringList components = {"hd", "hr.02", "hd.armor12"};
    QMap<QString, QByteArray> nestedMembers;
    nestedMembers.insert("unhufe", assemblyLink(components));
    nestedMembers.insert("hd", figureWithCrown(1.0f));
    nestedMembers.insert("hr.02", figureWithCrown(1.2f));
    nestedMembers.insert("hd.armor12", QByteArray("component"));
    const QByteArray sourceBytes = resArchive({{"unhufe.mod", resArchive(nestedMembers)}});
    const QString resourcePath = QDir(directory.path()).filePath("figures.res");
    QFile resourceFile(resourcePath);
    QVERIFY(resourceFile.open(QIODevice::WriteOnly));
    QCOMPARE(resourceFile.write(sourceBytes), qint64(sourceBytes.size()));
    resourceFile.close();

    SMobFamilyValidationRequest request;
    request.familyLabel = "unloaded-family-resource-evidence";
    request.profileId = "base";
    request.seedMobPaths << mobPath;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Resource Hair F2", "unhufe", {"llh.troll green"}, 2, "llh", 12);
    request.assemblyResourceSources << resourcePath;
    request.assemblyResourceFingerprint = "fixture-profile";

    QCOMPARE(int(ei::assemblyHairCrownGeometryFromResourceSources(
                     request.assemblyResourceSources,
                     request.assemblyResourceFingerprint,
                     "unhufe", "hr.02", 1.0f, 1.0f, 1.0f)),
             int(ei::EHairCrownGeometry::AboveHeadCrown));

    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    QVERIFY(std::any_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));
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
        "Human Hairless F0", "unhuma", {"helmet.iron"}, -1, "helmet", 6);
    request.assemblyPartsByModel.insert(
        "unhuma", {"hd", "hr.01", "hd.armor06"});
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void MobFamilyValidationTests::missingAssemblyEvidenceDoesNotReportMissingHair()
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
    request.familyLabel = "authored-hair-without-assembly-evidence";
    request.profileId = "jamais-vu";
    request.seedMobPaths << path;
    request.explicitComposition = true;
    request.includeProfileWideMobs = false;
    request.database = wearableDatabase(
        "Human Authored Hair F0", "unhuma", {"helmet.iron"}, 2);
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QVERIFY(std::none_of(report.findings.cbegin(), report.findings.cend(),
        [](const SMobFamilyFinding& finding)
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));
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
        { return finding.code == "mob.semantic.helmet_hair_fallback"; }));
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

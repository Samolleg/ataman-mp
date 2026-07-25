#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>

#include <algorithm>

#include "mob_family_validation.h"

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

QTEST_APPLESS_MAIN(MobFamilyValidationTests)
#include "mob_family_validation_tests.moc"

#include <QtTest>
#include <QTemporaryDir>

#include "runtime_providers.h"

class RuntimeProviderTests : public QObject
{
    Q_OBJECT

private slots:
    void builtInProvidersAreRoleSelected();
    void providerSelectionFailsClosed();
    void validJmvManifestStaysInsideRunRoot();
    void livePathsAreRejectedForExperimentalBaseProfile();
};

void RuntimeProviderTests::builtInProvidersAreRoleSelected()
{
    const ProfileProviderRegistry registry = ProfileProviderRegistry::builtIns();
    const SProfileProviderSelection base = registry.select(
        EProfileProviderRole::Database, "base.standard.v1");
    QVERIFY(base.valid);
    QCOMPARE(base.providerId, QString("base.standard.v1"));
    const SProfileProviderSelection jmv = registry.select(
        EProfileProviderRole::EquipmentLayout, "JMV.V1");
    QVERIFY(jmv.valid);
    QCOMPARE(jmv.providerId, QString("jmv.v1"));
}

void RuntimeProviderTests::providerSelectionFailsClosed()
{
    ProfileProviderRegistry registry = ProfileProviderRegistry::builtIns();
    const SProfileProviderSelection unknown = registry.select(
        EProfileProviderRole::CharacterAppearance, "future-mod.v1");
    QVERIFY(!unknown.valid);
    QVERIFY(unknown.diagnostic.contains("Unknown"));

    SProfileProviderDescriptor databaseOnly;
    databaseOnly.id = "test.database.v2";
    databaseOnly.interfaceVersion = 2;
    databaseOnly.roles.insert(EProfileProviderRole::Database);
    QVERIFY(registry.registerProvider(databaseOnly));
    QVERIFY(!registry.select(EProfileProviderRole::Database, databaseOnly.id, 1).valid);
    QVERIFY(!registry.select(EProfileProviderRole::UnitAnimation, databaseOnly.id, 2).valid);
}

void RuntimeProviderTests::validJmvManifestStaysInsideRunRoot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    SMapTestLaunchRequest request;
    request.runId = "20260716-test-1";
    request.profileId = "jamais-vu";
    request.runtimeManifestId = "jmv-development-stack-v1";
    request.runtimeComponentHashes.insert("starter-engine", "ABC");
    request.internalMapName = "endurance_test";
    request.stagedMprPath = root.filePath("maps/endurance_test.mpr");
    request.orderedStagedMobPaths << root.filePath("maps/endurance_test.mob")
                                  << root.filePath("maps/Autoendurance_test01.mob");
    request.activeMobPath = request.orderedStagedMobPaths.front();
    request.sourceSha256.insert("source.mob", "DEF");
    request.stagedSha256.insert("endurance_test.mob", "DEF");
    request.logPath = root.filePath("logs/runtime.log");
    request.resultPath = root.filePath("result.json");

    QStringList errors;
    QVERIFY2(validateMapTestLaunchRequest(request, root.path(), &errors), qPrintable(errors.join("\n")));
    QCOMPARE(mapTestLaunchRequestToJson(request).value("protocolVersion").toInt(), 1);
}

void RuntimeProviderTests::livePathsAreRejectedForExperimentalBaseProfile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    SMapTestLaunchRequest request;
    request.runId = "test";
    request.profileId = "base-game";
    request.runtimeManifestId = "base-ei-v1";
    request.runtimeComponentHashes.insert("game", "ABC");
    request.internalMapName = "zone1";
    request.stagedMprPath = "D:/Evil Islands/Maps/zone1.mpr";
    request.orderedStagedMobPaths << "D:/Evil Islands/Maps/zone1.mob";
    request.activeMobPath = request.orderedStagedMobPaths.front();
    request.sourceSha256.insert("source", "DEF");
    request.stagedSha256.insert("staged", "DEF");
    request.logPath = root.filePath("runtime.log");
    request.resultPath = root.filePath("result.json");

    QStringList errors;
    QVERIFY(!validateMapTestLaunchRequest(request, root.path(), &errors));
    QVERIFY(errors.join(" ").contains("disposable"));
}

QTEST_APPLESS_MAIN(RuntimeProviderTests)
#include "runtime_provider_tests.moc"

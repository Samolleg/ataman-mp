#include <QtTest>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include "resource_profile.h"
#include "../source/runtime/runtime_variant_registry.h"

namespace {
void createFile(const QString& path, const QByteArray& data = QByteArray("test"))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), qint64(data.size()));
}

QString createJmvInstall(QTemporaryDir* temporary)
{
    const QString root = temporary->path();
    createFile(QDir(root).filePath("game.exe"));
    createFile(QDir(root).filePath("Starter/Engine/game.exe"));
    createFile(QDir(root).filePath("Res/textures.res"));
    QDir().mkpath(QDir(root).filePath("Maps"));
    const QString mod = QDir(root).filePath("Starter/MODS/JamaisVu135");
    createFile(QDir(mod).filePath("main.lua"));
    createFile(QDir(mod).filePath("addon.dll"));
    createFile(QDir(mod).filePath("res/databaseJmv.res"));
    QDir().mkpath(QDir(mod).filePath("Maps"));
    return root;
}
}

class ResourceProfileTests : public QObject
{
    Q_OBJECT

private slots:
    void jmvPresetFindsModAndBaseResources();
    void basePresetDoesNotRequireJmv();
    void invalidGameRootIsRejected();
    void externalProfileCanBeAdded();
    void informationalRuntimeHashDoesNotInvalidateManifest();
    void builtInProfilesUseCanonicalRuntimeRegistry();
    void dejaVuDoesNotSelectNeighboringMod();
    void installedDejaVuProfile();
    void portableDejaVuUsesOnlyPortableResources();
};

void ResourceProfileTests::jmvPresetFindsModAndBaseResources()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = createJmvInstall(&temporary);
    const SResourceProfileEvaluation result = evaluateResourceProfile(root, builtInResourceProfiles()[0]);
    QVERIFY2(result.valid, qPrintable(result.messages.join('\n')));
    QCOMPARE(result.profileId, QString("jamais-vu"));
    QCOMPARE(result.resourceDirectories.size(), 2);
    QCOMPARE(result.archiveCount, 2);
    QVERIFY(result.resourceDirectories[0].endsWith("JamaisVu135/res"));
    QVERIFY(result.resourceDirectories[1].endsWith("Res"));
    QCOMPARE(result.mapDirectories.size(), 2);
    QVERIFY(result.mapDirectories[0].endsWith("JamaisVu135/Maps"));
    QVERIFY(result.mapDirectories[1].endsWith("Maps"));
    QVERIFY(result.runtimeStackComplete);
    QVERIFY(!result.runtimeStackValidated);
    QCOMPARE(result.runtimeComponents.size(), 1);
    QCOMPARE(result.runtimeComponents.first().id, QString("starter-engine"));
    QCOMPARE(profileCapabilityStatusName(result.capabilities.value("runtime.directMapLaunch").status),
             QString("experimental"));
    QCOMPARE(result.providers.value("database").id, QString("jmv.v1"));
    QCOMPARE(result.providers.value("equipmentLayout").id, QString("jmv.v1"));

    QVERIFY(QFile::remove(QDir(result.modRoot).filePath("addon.dll")));
    const SResourceProfileEvaluation withoutJmvAddon =
        evaluateResourceProfile(root, builtInResourceProfiles()[0]);
    QVERIFY2(withoutJmvAddon.valid, qPrintable(withoutJmvAddon.messages.join('\n')));
}

void ResourceProfileTests::basePresetDoesNotRequireJmv()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    createFile(QDir(root).filePath("game.exe"));
    createFile(QDir(root).filePath("Res/figures.res"));
    QDir().mkpath(QDir(root).filePath("Maps"));
    const SResourceProfileEvaluation result = evaluateResourceProfile(root, builtInResourceProfiles()[1]);
    QVERIFY2(result.valid, qPrintable(result.messages.join('\n')));
    QCOMPARE(result.resourceDirectories.size(), 1);
    QCOMPARE(result.mapDirectories.size(), 1);
    QVERIFY(result.mapDirectories[0].endsWith("Maps"));
    QVERIFY(result.runtimeStackComplete);
    QCOMPARE(result.providers.value("database").id, QString("base.standard.v1"));
    const SResourceProfile baseProfile = builtInResourceProfiles()[1];
    QCOMPARE(baseProfile.runtimeComponents.size(), 1);
    QCOMPARE(baseProfile.runtimeComponents.first().acceptedSha256.size(), 2);
    QVERIFY(baseProfile.runtimeComponents.first().acceptedSha256.contains(
        "B871D70D3876B2A5634F0920C92118BC49058B50C7D8CBE68376349F51C2E570"));
    QVERIFY(baseProfile.runtimeComponents.first().acceptedSha256.contains(
        "8ED3F9A500D84D822BD311B74B1B49D5CD16DD12D3502C27D7667957311036C1"));
}

void ResourceProfileTests::invalidGameRootIsRejected()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const SResourceProfileEvaluation result = evaluateResourceProfile(temporary.path(), builtInResourceProfiles()[0]);
    QVERIFY(!result.valid);
    QVERIFY(result.messages.join(' ').contains("game.exe"));
}

void ResourceProfileTests::externalProfileCanBeAdded()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray json = R"JSON({
        "schemaVersion": 2,
        "id": "test-mod",
        "displayName": "Test Mod",
        "resourceDirectories": ["{gameRoot}/TestRes"],
        "mapDirectories": ["{gameRoot}/TestMaps"],
        "providers": {
            "database": {"id": "base.standard.v1", "interfaceVersion": 1}
        }
    })JSON";
    createFile(QDir(temporary.path()).filePath("test-mod.json"), json);
    const QVector<SResourceProfile> profiles = loadResourceProfiles(temporary.path());
    bool found = false;
    for(const SResourceProfile& profile : profiles)
        if(profile.id == "test-mod")
            found = true;
    QVERIFY(found);
    for(const SResourceProfile& profile : profiles)
        if(profile.id == "test-mod")
            QCOMPARE(profile.providers.value("database").id, QString("base.standard.v1"));
}

void ResourceProfileTests::informationalRuntimeHashDoesNotInvalidateManifest()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QByteArray json = R"JSON({
        "schemaVersion": 2,
        "id": "mutable-runtime-test",
        "displayName": "Mutable runtime test",
        "resourceDirectories": ["{gameRoot}/TestRes"],
        "mapDirectories": ["{gameRoot}/TestMaps"],
        "runtime": {
            "manifestId": "mutable-v1",
            "components": [
                {"id":"engine", "path":"{gameRoot}/game.exe",
                 "acceptedSha256":["9F86D081884C7D659A2FEAA0C55AD015A3BF4F1B2B0B822CD15D6C15B0F00A08"]},
                {"id":"script", "path":"{gameRoot}/main.lua", "enforceHash":false,
                 "acceptedSha256":["0000000000000000000000000000000000000000000000000000000000000000"]}
            ]
        }
    })JSON";
    createFile(QDir(temporary.path()).filePath("profiles/mutable.json"), json);
    createFile(QDir(temporary.path()).filePath("game.exe"));
    createFile(QDir(temporary.path()).filePath("main.lua"), "locally edited");
    createFile(QDir(temporary.path()).filePath("TestRes/data.res"));
    QDir().mkpath(QDir(temporary.path()).filePath("TestMaps"));

    const QVector<SResourceProfile> profiles = loadResourceProfiles(
        QDir(temporary.path()).filePath("profiles"));
    SResourceProfile profile;
    for(const SResourceProfile& candidate : profiles)
        if(candidate.id == "mutable-runtime-test")
            profile = candidate;
    QVERIFY(!profile.id.isEmpty());
    const SResourceProfileEvaluation result = evaluateResourceProfile(temporary.path(), profile);
    QVERIFY2(result.valid, qPrintable(result.messages.join('\n')));
    QVERIFY(result.runtimeStackValidated);
    QCOMPARE(result.runtimeComponents.size(), 2);
    QVERIFY(!result.runtimeComponents.at(1).hashAccepted);
    QVERIFY(!result.runtimeComponents.at(1).enforceHash);
}

void ResourceProfileTests::builtInProfilesUseCanonicalRuntimeRegistry()
{
    const QVector<SResourceProfile> profiles = builtInResourceProfiles();
    QCOMPARE(profiles.size(), 3);

    QStringList expectedJmv;
    QStringList expectedBase;
    QSet<QString> uniqueHashes;
    for(std::size_t index = 0; index < ei::runtime::runtimeVariantCount(); ++index)
    {
        const ei::runtime::SRuntimeVariant& variant = ei::runtime::kRuntimeVariants[index];
        const QString hash = QString::fromLatin1(variant.sha256);
        QCOMPARE(QString::fromLatin1(variant.layoutId),
                 QString::fromLatin1(ei::runtime::kGameLayoutV1));
        QCOMPARE(hash.size(), 64);
        QVERIFY(QRegularExpression("^[0-9A-F]{64}$").match(hash).hasMatch());
        QVERIFY2(!uniqueHashes.contains(hash), qPrintable(hash));
        uniqueHashes.insert(hash);
        if(variant.profile == ei::runtime::ERuntimeProfile::Jmv)
            expectedJmv.append(hash);
        else
            expectedBase.append(hash);
    }

    QCOMPARE(profiles.at(0).runtimeComponents.size(), 1);
    QCOMPARE(profiles.at(1).runtimeComponents.size(), 1);
    QCOMPARE(profiles.at(0).runtimeComponents.first().acceptedSha256, expectedJmv);
    QCOMPARE(profiles.at(1).runtimeComponents.first().acceptedSha256, expectedBase);
    QCOMPARE(profiles.at(2).runtimeComponents.first().acceptedSha256, expectedJmv);
    QCOMPARE(expectedJmv.size(), 1);
    QCOMPARE(expectedBase.size(), 2);
}

void ResourceProfileTests::dejaVuDoesNotSelectNeighboringMod()
{
    SResourceProfile deja;
    for(const auto& profile : builtInResourceProfiles())
        if(profile.id == "deja-vu") deja = profile;
    QVERIFY2(!deja.id.isEmpty(), "DejaVu must be independently selectable");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = createJmvInstall(&temporary);
    QVERIFY(!evaluateResourceProfile(root, deja).valid);
    const QString mod = QDir(root).filePath("Starter/MODS/DejaVu");
    createFile(QDir(mod).filePath("main.lua"));
    createFile(QDir(mod).filePath("res/databaseJmv.res"));
    QDir().mkpath(QDir(mod).filePath("Maps"));
    const auto selected = evaluateResourceProfile(root, deja);
    QVERIFY2(selected.valid, qPrintable(selected.messages.join('\n')));
    QCOMPARE(selected.modRoot, mod);
    QCOMPARE(selected.resourceDirectories.first(), mod + "/res");
    QCOMPARE(selected.mapDirectories.first(), mod + "/Maps");
    QCOMPARE(selected.providers.value("database").id, QString("jmv.v1"));
    QCOMPARE(selected.providers.value("characterAppearance").id, QString("jmv.v1"));
    QVERIFY(evaluateResourceProfile(root, builtInResourceProfiles().first()).modRoot.endsWith("JamaisVu135"));
    QVERIFY(QFile::remove(QDir(root).filePath("Starter/MODS/JamaisVu135/main.lua")));
    QVERIFY(!evaluateResourceProfile(root, builtInResourceProfiles().first()).valid);
    QVERIFY(evaluateResourceProfile(root, deja).valid);
}

void ResourceProfileTests::portableDejaVuUsesOnlyPortableResources()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = temporary.path();
    createFile(root + "/Engine/game.exe");
    createFile(root + "/Base/Res/textures.res");
    QDir().mkpath(root + "/Base/Maps");
    createFile(root + "/MODS/DejaVu/main.lua");
    createFile(root + "/MODS/DejaVu/res/database.res");
    QDir().mkpath(root + "/MODS/DejaVu/Maps");
    const auto profiles = builtInResourceProfiles();
    const auto selected = evaluateResourceProfile(root, profiles[2]);
    QVERIFY2(selected.valid, qPrintable(selected.messages.join('\n')));
    QCOMPARE(selected.gameRoot, root);
    QCOMPARE(selected.modRoot, root + "/MODS/DejaVu");
    QCOMPARE(selected.resourceDirectories, QStringList({root + "/MODS/DejaVu/res", root + "/Base/Res"}));
    QCOMPARE(selected.mapDirectories, QStringList({root + "/MODS/DejaVu/Maps", root + "/Base/Maps"}));
    QCOMPARE(selected.runtimeComponents.first().path, root + "/Engine/game.exe");
    QCOMPARE(resourceGameDataRoot(root), root + "/Base");
    QCOMPARE(resourceStarterRoot(root), root);
    QCOMPARE(resourceGameExecutable(root, "deja-vu"), root + "/Engine/game.exe");
    QVERIFY(!selected.runtimeStackValidated); // arbitrary test executable is never trusted
    QVERIFY(!evaluateResourceProfile(root, profiles[0]).valid); // no JMV substitution
    QVERIFY(!evaluateResourceProfile(root, profiles[1]).valid); // no base-runtime substitution
    createFile(root + "/MODS/JamaisVu135/main.lua");
    createFile(root + "/MODS/JamaisVu135/res/database.res");
    QDir().mkpath(root + "/MODS/JamaisVu135/Maps");
    const auto jmv = evaluateResourceProfile(root, profiles[0]);
    QVERIFY(jmv.valid);
    QCOMPARE(jmv.modRoot, root + "/MODS/JamaisVu135");
    QCOMPARE(jmv.resourceDirectories.last(), root + "/Base/Res");
    QCOMPARE(evaluateResourceProfile(root, profiles[2]).modRoot, selected.modRoot);
    QVERIFY(QFile::remove(root + "/Engine/game.exe"));
    QVERIFY(!evaluateResourceProfile(root, profiles[2]).valid);
}

void ResourceProfileTests::installedDejaVuProfile()
{
    const QString root = qEnvironmentVariable("EI_TEST_DEJAVU_GAME_ROOT");
    if(root.isEmpty()) QSKIP("Optional read-only installed profile probe");
    SResourceProfile deja;
    for(const auto& profile : builtInResourceProfiles())
        if(profile.id == "deja-vu") deja = profile;
    QVERIFY(!deja.id.isEmpty());
    const auto selected = evaluateResourceProfile(root, deja);
    QVERIFY2(selected.valid, qPrintable(selected.messages.join('\n')));
    QVERIFY(selected.modRoot.endsWith("/DejaVu", Qt::CaseInsensitive));
    QVERIFY2(selected.runtimeStackValidated, qPrintable(selected.messages.join('\n')));
    QVERIFY(QFileInfo(selected.resourceDirectories.first() + "/database.res").isFile());
    qInfo() << "DejaVu archives:" << selected.archiveCount
            << "MPR files:" << QDir(selected.mapDirectories.first()).entryList({"*.mpr"}, QDir::Files).size();
}

QTEST_APPLESS_MAIN(ResourceProfileTests)
#include "resource_profile_tests.moc"

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "resource_profile.h"

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
        "schemaVersion": 1,
        "id": "test-mod",
        "displayName": "Test Mod",
        "resourceDirectories": ["{gameRoot}/TestRes"],
        "mapDirectories": ["{gameRoot}/TestMaps"]
    })JSON";
    createFile(QDir(temporary.path()).filePath("test-mod.json"), json);
    const QVector<SResourceProfile> profiles = loadResourceProfiles(temporary.path());
    bool found = false;
    for(const SResourceProfile& profile : profiles)
        if(profile.id == "test-mod")
            found = true;
    QVERIFY(found);
}

QTEST_APPLESS_MAIN(ResourceProfileTests)
#include "resource_profile_tests.moc"

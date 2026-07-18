#include <QtTest>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "jmv_map_test_launcher.h"
#include "current_session_backup.h"

class JmvMapTestLauncherTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void injectsIntoExistingWorldScript();
    void ignoresWorldScriptInStringsAndComments();
    void rejectsMalformedExistingWorldScript();
    void recognizesConfiguredJmvPluginSlot();
    void exposesTestRunsQuickAccessPath();
    void isolatesPersistentSavesByProfileInstallation();
    void preparesOwnedGodmodeAutoexec();
    void preservesExistingAutoexecForRecovery();
    void recoveryRemovesAutoexecCreatedByEndurance();
    void recoveryRestoresPreexistingAutoexec();
    void recoveryRemovesOwnedBridge();
    void recoveryRetainsChangedBridge();
    void currentSessionSnapshotRestoresExistingTree();
    void currentSessionSnapshotRemovesDisposableTreeWhenOriginallyAbsent();
    void currentSessionRestoreRejectsTamperedBackupWithoutTouchingLiveTree();
    void currentSessionSnapshotRejectsUnsafePath();
};

static QString hash(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

static void writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), qint64(data.size()));
}

static QString createRecoveryManifest(const QString& autoexecPath,
                                      const QString& backupPath,
                                      bool originalExisted,
                                      const QByteArray& original,
                                      const QByteArray& patched,
                                      const QString& bridgePath = QString(),
                                      const QByteArray& bridgeBytes = QByteArray())
{
    const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("test-runs");
    const QString run = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if(!QDir().mkpath(run))
        return QString();
    QJsonObject autoexec;
    autoexec.insert("path", autoexecPath);
    autoexec.insert("backupPath", backupPath);
    autoexec.insert("originalExisted", originalExisted);
    autoexec.insert("originalSha256", hash(original));
    autoexec.insert("patchedSha256", hash(patched));
    QJsonObject manifest;
    manifest.insert("state", "launching");
    manifest.insert("pid", 0);
    manifest.insert("autoexec", autoexec);
    if(!bridgePath.isEmpty())
    {
        QJsonObject bridge;
        bridge.insert("path", bridgePath);
        bridge.insert("sha256", hash(bridgeBytes));
        manifest.insert("bridge", bridge);
    }
    writeFile(QDir(run).filePath("manifest.json"),
              QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    return run;
}

void JmvMapTestLauncherTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("test-runs");
    QDir(root).removeRecursively();
}

void JmvMapTestLauncherTests::recognizesConfiguredJmvPluginSlot()
{
    const QByteArray expected = QByteArray::fromHex(
        "01000300444556ffff0d000000020600506c7567696e0a00706c7567696e2e646c6c");
    QVERIFY(CJmvMapTestLauncher::hasConfiguredJmvPluginSlot(expected));
    QVERIFY(!CJmvMapTestLauncher::hasConfiguredJmvPluginSlot(
                QByteArray("Plugin=EIMapperEnduranceJmvBridge.plg")));
}

void JmvMapTestLauncherTests::exposesTestRunsQuickAccessPath()
{
    const QString expected = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath("test-runs");
    QCOMPARE(QDir::cleanPath(CJmvMapTestLauncher::testRunsRootPath()),
             QDir::cleanPath(expected));
}

void JmvMapTestLauncherTests::isolatesPersistentSavesByProfileInstallation()
{
    SResourceProfileEvaluation first;
    first.profileId = "jamais-vu";
    first.modRoot = "C:/Games/Evil Islands/Starter/MODS/JamaisVu135";
    SResourceProfileEvaluation same = first;
    SResourceProfileEvaluation other = first;
    other.modRoot = "D:/Games/Evil Islands/Starter/MODS/JamaisVu135";

    const QString firstPath = CJmvMapTestLauncher::testSavesPathForProfile(first);
    QCOMPARE(CJmvMapTestLauncher::testSavesPathForProfile(same), firstPath);
    QVERIFY(CJmvMapTestLauncher::testSavesPathForProfile(other) != firstPath);
    QVERIFY(firstPath.contains("runtime-profiles"));
    QVERIFY(firstPath.endsWith("saves"));
}

void JmvMapTestLauncherTests::preparesOwnedGodmodeAutoexec()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString engine = QDir(temp.path()).filePath("Engine");
    const QString run = QDir(temp.path()).filePath("run");
    QVERIFY(QDir().mkpath(engine));
    QVERIFY(QDir().mkpath(run));
    QJsonObject manifest;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::prepareTemporaryAutoexec(
        engine, run, true, &manifest, &error), qPrintable(error));
    QFile generated(QDir(engine).filePath("autoexec.cfg"));
    QVERIFY(generated.open(QIODevice::ReadOnly));
    const QByteArray contents = generated.readAll();
    QCOMPARE(contents.count("@godmode(0,3)"), 1);
    QCOMPARE(manifest.value("autoexec").toObject().value("originalExisted").toBool(), false);
}

void JmvMapTestLauncherTests::preservesExistingAutoexecForRecovery()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString engine = QDir(temp.path()).filePath("Engine");
    const QString run = QDir(temp.path()).filePath("run");
    QVERIFY(QDir().mkpath(engine));
    QVERIFY(QDir().mkpath(run));
    const QByteArray original("fps 1\r\n");
    writeFile(QDir(engine).filePath("autoexec.cfg"), original);
    QJsonObject manifest;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::prepareTemporaryAutoexec(
        engine, run, true, &manifest, &error), qPrintable(error));
    QFile backup(QDir(run).filePath("autoexec.original.cfg"));
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);
    QCOMPARE(manifest.value("autoexec").toObject().value("originalSha256").toString(),
             hash(original));
}

void JmvMapTestLauncherTests::injectsIntoExistingWorldScript()
{
    const QString source = "WorldScript(\nConsoleString(\"original\")\n)\n";
    QString output;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::injectWorldScriptBootstrap(
                 source, "ConsoleString(\"test\")", &output, &error), qPrintable(error));
    QVERIFY(output.contains("WorldScript(\nConsoleString(\"test\")\n\nConsoleString(\"original\")"));
}

void JmvMapTestLauncherTests::ignoresWorldScriptInStringsAndComments()
{
    const QString source = "// WorldScript(ignored)\nConsoleString(\"WorldScript(ignored)\")\n";
    QString output;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::injectWorldScriptBootstrap(
                 source, "ConsoleString(\"test\")", &output, &error), qPrintable(error));
    QVERIFY(output.endsWith("WorldScript(\nConsoleString(\"test\")\n)\n"));
}

void JmvMapTestLauncherTests::rejectsMalformedExistingWorldScript()
{
    QString output;
    QString error;
    QVERIFY(!CJmvMapTestLauncher::injectWorldScriptBootstrap(
                "WorldScript broken", "ConsoleString(\"test\")", &output, &error));
    QVERIFY(!error.isEmpty());
}

void JmvMapTestLauncherTests::recoveryRemovesAutoexecCreatedByEndurance()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString autoexec = QDir(temp.path()).filePath("autoexec.cfg");
    const QByteArray patched("endurance_launch ed12345678\r\n");
    writeFile(autoexec, patched);
    const QString run = createRecoveryManifest(autoexec,
        QDir(temp.path()).filePath("autoexec.original.cfg"), false, QByteArray(), patched);
    QVERIFY(!run.isEmpty());
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY2(!QFileInfo::exists(autoexec), qPrintable(summary));
    QVERIFY(summary.contains("Removed temporary autoexec file"));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryRestoresPreexistingAutoexec()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString autoexec = QDir(temp.path()).filePath("autoexec.cfg");
    const QString backup = QDir(temp.path()).filePath("autoexec.original.cfg");
    const QByteArray original("fps 1\r\n");
    const QByteArray patched(original + "endurance_launch ed12345678\r\n");
    writeFile(autoexec, patched);
    writeFile(backup, original);
    const QString run = createRecoveryManifest(autoexec, backup, true, original, patched);
    QVERIFY(!run.isEmpty());
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QFile restored(autoexec);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), original);
    QVERIFY(summary.contains("Restored autoexec file"));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryRemovesOwnedBridge()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString bridge = QDir(temp.path()).filePath("plugin.dll");
    const QByteArray bridgeBytes("fingerprinted bridge");
    writeFile(bridge, bridgeBytes);
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray(), bridge, bridgeBytes);
    QVERIFY(!run.isEmpty());
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY2(!QFileInfo::exists(bridge), qPrintable(summary));
    QVERIFY(summary.contains("Removed temporary file"));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryRetainsChangedBridge()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString bridge = QDir(temp.path()).filePath("plugin.dll");
    const QByteArray owned("fingerprinted bridge");
    writeFile(bridge, QByteArray("externally changed"));
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray(), bridge, owned);
    QVERIFY(!run.isEmpty());
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY(QFileInfo::exists(bridge));
    QVERIFY(summary.contains("Retained changed temporary file"));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::currentSessionSnapshotRestoresExistingTree()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString current = QDir(temp.path()).filePath("saves/current");
    const QString nested = QDir(current).filePath("nested");
    const QString empty = QDir(current).filePath("empty");
    QVERIFY(QDir().mkpath(nested));
    QVERIFY(QDir().mkpath(empty));
    const QByteArray original("original session\0bytes", 22);
    writeFile(QDir(nested).filePath("state.bin"), original);

    const QString run = QDir(temp.path()).filePath("run");
    QVERIFY(QDir().mkpath(run));
    QJsonObject snapshot;
    QString error;
    QVERIFY2(snapshotCurrentSession(current, run, &snapshot, &error), qPrintable(error));

    writeFile(QDir(nested).filePath("state.bin"), QByteArray("temporary state"));
    writeFile(QDir(current).filePath("new.tmp"), QByteArray("new"));
    QVERIFY(QDir(empty).removeRecursively());

    QStringList messages;
    QVERIFY2(restoreCurrentSession(snapshot, &messages), qPrintable(messages.join('\n')));
    QFile restored(QDir(nested).filePath("state.bin"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), original);
    QVERIFY(QFileInfo::exists(empty));
    QVERIFY(!QFileInfo::exists(QDir(current).filePath("new.tmp")));
}

void JmvMapTestLauncherTests::currentSessionSnapshotRemovesDisposableTreeWhenOriginallyAbsent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString current = QDir(temp.path()).filePath("saves/current");
    const QString run = QDir(temp.path()).filePath("run");
    QVERIFY(QDir().mkpath(run));
    QJsonObject snapshot;
    QString error;
    QVERIFY2(snapshotCurrentSession(current, run, &snapshot, &error), qPrintable(error));
    QVERIFY(!snapshot.value("originalExisted").toBool());

    QVERIFY(QDir().mkpath(current));
    writeFile(QDir(current).filePath("test.sav"), QByteArray("disposable"));
    QStringList messages;
    QVERIFY2(restoreCurrentSession(snapshot, &messages), qPrintable(messages.join('\n')));
    QVERIFY(!QFileInfo::exists(current));
}

void JmvMapTestLauncherTests::currentSessionRestoreRejectsTamperedBackupWithoutTouchingLiveTree()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString current = QDir(temp.path()).filePath("saves/current");
    QVERIFY(QDir().mkpath(current));
    writeFile(QDir(current).filePath("state.bin"), QByteArray("original"));
    const QString run = QDir(temp.path()).filePath("run");
    QVERIFY(QDir().mkpath(run));
    QJsonObject snapshot;
    QString error;
    QVERIFY2(snapshotCurrentSession(current, run, &snapshot, &error), qPrintable(error));

    writeFile(QDir(current).filePath("state.bin"), QByteArray("live disposable state"));
    const QJsonObject entry = snapshot.value("files").toArray().first().toObject();
    writeFile(QDir(snapshot.value("backupPath").toString()).filePath(entry.value("path").toString()),
              QByteArray("tampered backup"));
    QStringList messages;
    QVERIFY(!restoreCurrentSession(snapshot, &messages));
    QFile live(QDir(current).filePath("state.bin"));
    QVERIFY(live.open(QIODevice::ReadOnly));
    QCOMPARE(live.readAll(), QByteArray("live disposable state"));
}

void JmvMapTestLauncherTests::currentSessionSnapshotRejectsUnsafePath()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QJsonObject snapshot;
    QString error;
    QVERIFY(!snapshotCurrentSession(QDir(temp.path()).filePath("current"),
                                    QDir(temp.path()).filePath("run"),
                                    &snapshot, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(JmvMapTestLauncherTests)
#include "jmv_map_test_launcher_tests.moc"

#include <QtTest>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "jmv_map_test_launcher.h"
#include "current_session_backup.h"
#include "endurance_document.h"

class JmvMapTestLauncherTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void injectsIntoExistingWorldScript();
    void injectsAfterLeadingWorldScriptDelay();
    void ignoresWorldScriptInStringsAndComments();
    void rejectsMalformedExistingWorldScript();
    void injectsActualAutorunRuntimeBootstrap();
    void recognizesConfiguredJmvPluginSlot();
    void exposesTestRunsQuickAccessPath();
    void createsGenerationScopedSavePaths();
    void preparesOwnedGodmodeAutoexec();
    void preservesExistingAutoexecForRecovery();
    void recoveryRemovesAutoexecCreatedByEndurance();
    void recoveryRestoresPreexistingAutoexec();
    void recoveryRemovesOwnedBridge();
    void recoveryRetainsChangedBridge();
    void recoveryRemovesDisposableSaveGenerations();
    void recoveryDoesNotTrustOrCleanLivePidOnlyJournal();
    void recoveryMonitorClearsAfterRecordedProcessExits();
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

void JmvMapTestLauncherTests::createsGenerationScopedSavePaths()
{
    const QString run = "C:/Endurance/test-runs/run-1";
    const QString initial = CJmvMapTestLauncher::generationSavesPath(run, 0);
    const QString reload = CJmvMapTestLauncher::generationSavesPath(run, 17);
    QVERIFY(QDir::fromNativeSeparators(initial).endsWith("generations/000000/saves"));
    QVERIFY(QDir::fromNativeSeparators(reload).endsWith("generations/000017/saves"));
    QVERIFY(initial != reload);
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

void JmvMapTestLauncherTests::injectsAfterLeadingWorldScriptDelay()
{
    const QString source = "WorldScript(\n  Sleep( 2 )\n  ConsoleString(\"original\")\n)\n";
    QString output;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::injectWorldScriptBootstrap(
        source, "GodMode(0,3)", &output, &error), qPrintable(error));
    const int delay = output.indexOf("Sleep( 2 )");
    const int command = output.indexOf("GodMode(0,3)");
    const int original = output.indexOf("ConsoleString(\"original\")");
    QVERIFY(delay >= 0);
    QVERIFY(command > delay);
    QVERIFY(original > command);
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

void JmvMapTestLauncherTests::injectsActualAutorunRuntimeBootstrap()
{
    const QString maps = QString::fromLocal8Bit(qgetenv("EI_TEST_JMV_MAPS"));
    if(maps.isEmpty())
        QSKIP("Set EI_TEST_JMV_MAPS to run the private-corpus autorun check.");
    const QString source = QDir(maps).filePath("AutoRunSpellAddon123.mob");
    QVERIFY(QFileInfo::exists(source));
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SMapSpawnPlan spawn;
    spawn.valid = true;
    spawn.custom = true;
    spawn.exitId = 5;
    spawn.position = QPointF(290.5, 219.2);
    QJsonObject manifest;
    QString outputPath;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::prepareGodmodeBootstrap(
        QStringList() << maps, temp.path(), spawn, 12.75, true,
        QStringList() << "Autozone1cyclopus.mob",
        &manifest, &outputPath, &error), qPrintable(error));

    CMobDocument output;
    CValidationReport report;
    QVERIFY2(output.load(QFileInfo(outputPath), &report), qPrintable(report.toText()));
    QString script;
    QVERIFY2(output.embeddedScript(&script, &report), qPrintable(report.toText()));
    const int worldScript = script.indexOf("WorldScript", 0, Qt::CaseInsensitive);
    const int marker = script.indexOf("// EI Mapper Endurance owned runtime bootstrap");
    QVERIFY(worldScript >= 0);
    QVERIFY(marker > worldScript);
    QVERIFY(script.contains("SetCPFast(GetLeader(), 290.500000, 219.200000, 0)"));
    QCOMPARE(script.count("SetCPFast(GetLeader(), 290.500000, 219.200000, 0)"), 2);
    QVERIFY(script.contains("AddMob(\"Autozone1cyclopus.mob\")"));
    QVERIFY(script.contains("GodMode(0,3)"));
    QVERIFY(!script.contains("GodMode(0,3);"));

    QFile unchanged(source);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), originalBytes);

    QTemporaryDir entranceTemp;
    QVERIFY(entranceTemp.isValid());
    SMapSpawnPlan entrance = spawn;
    entrance.custom = false;
    QJsonObject entranceManifest;
    QString entranceOutputPath;
    QVERIFY2(CJmvMapTestLauncher::prepareGodmodeBootstrap(
        QStringList() << maps, entranceTemp.path(), entrance, 12.75, true,
        QStringList(), &entranceManifest, &entranceOutputPath, &error),
        qPrintable(error));
    CMobDocument entranceOutput;
    QVERIFY2(entranceOutput.load(QFileInfo(entranceOutputPath), &report),
             qPrintable(report.toText()));
    QString entranceScript;
    QVERIFY2(entranceOutput.embeddedScript(&entranceScript, &report),
             qPrintable(report.toText()));
    QVERIFY(!entranceScript.contains("SetCPFast(GetLeader()"));
    QVERIFY(entranceScript.contains("GodMode(0,3)"));
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

void JmvMapTestLauncherTests::recoveryRemovesDisposableSaveGenerations()
{
    const QString root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath("test-runs");
    const QString run = QDir(root).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString saves = CJmvMapTestLauncher::generationSavesPath(run, 3);
    QVERIFY(QDir().mkpath(saves));
    writeFile(QDir(saves).filePath("slot.sav"), QByteArray("disposable generation"));

    QJsonObject manifest;
    manifest.insert("state", "running");
    manifest.insert("pid", 0);
    manifest.insert("runRoot", run);
    writeFile(QDir(run).filePath("manifest.json"),
              QJsonDocument(manifest).toJson(QJsonDocument::Indented));

    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY2(!QFileInfo::exists(QDir(run).filePath("generations")), qPrintable(summary));
    QVERIFY(summary.contains("Removed disposable save generations"));
    QVERIFY(QFileInfo::exists(QDir(run).filePath("manifest.json")));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryDoesNotTrustOrCleanLivePidOnlyJournal()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString bridge = QDir(temp.path()).filePath("plugin.dll");
    const QByteArray owned("legacy pid-only bridge");
    writeFile(bridge, owned);
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray(), bridge, owned);
    QVERIFY(!run.isEmpty());

    const QString manifestPath = QDir(run).filePath("manifest.json");
    QFile manifestFile(manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest.insert("pid", double(QCoreApplication::applicationPid()));
    writeFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented));

    const QString deferred = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY(deferred.isEmpty());
    QVERIFY(QFileInfo::exists(bridge));
    QString description;
    QVERIFY(CJmvMapTestLauncher::hasActiveInterruptedRun(&description));
    QVERIFY(description.contains("incomplete legacy process identity"));

    manifest.insert("pid", 0);
    writeFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    const QString cleaned = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY2(!QFileInfo::exists(bridge), qPrintable(cleaned));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryMonitorClearsAfterRecordedProcessExits()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString bridge = QDir(temp.path()).filePath("plugin.dll");
    const QByteArray owned("owned interrupted bridge");
    writeFile(bridge, owned);
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray(), bridge, owned);
    QVERIFY(!run.isEmpty());

    const QString manifestPath = QDir(run).filePath("manifest.json");
    QFile manifestFile(manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest.insert("pid", double(QCoreApplication::applicationPid()));
    writeFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented));

    CJmvMapTestLauncher launcher;
    QVERIFY(launcher.isRecoveryPending());
    QSignalSpy statusSpy(&launcher, &CJmvMapTestLauncher::recoveryStatusChanged);

    manifest.insert("pid", 0);
    writeFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    launcher.refreshInterruptedRecovery();

    QVERIFY(!launcher.isRecoveryPending());
    QVERIFY(!QFileInfo::exists(bridge));
    QCOMPARE(statusSpy.count(), 1);
    QCOMPARE(statusSpy.takeFirst().at(1).toBool(), false);
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

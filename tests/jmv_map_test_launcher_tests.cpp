#include <QtTest>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <windows.h>
#include <thread>
#endif

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
    void exposesTestRunsQuickAccessPath();
    void createsGenerationScopedSavePaths();
    void validatesExternalGraphEnvelope();
    void recognizesGraphlessBridgeProtocolMarker();
    void detectsAutoSpellMobCapability();
    void resolvesWorkspaceExternalGraph();
    void embeddedGraphShadowsExternalGraph();
    void reportsMissingOptionalGraph();
    void rejectsMalformedExternalGraph();
    void resolvesActualZone6ExternalGraph();
    void resolvesHostedGamePidForHotReload();
    void preparesOwnedGodmodeAutoexec();
    void preservesExistingAutoexecForRecovery();
    void recoveryRemovesAutoexecCreatedByEndurance();
    void recoveryRestoresPreexistingAutoexec();
    void recoveryRemovesOwnedBridge();
    void recoveryRemovesOwnedGraph();
    void recoveryRemovesOwnedPlaceholderResource();
    void recoveryRetainsChangedPlaceholderResource();
    void recoveryRetriesTransientBridgeLock();
    void recoveryRetainsChangedBridge();
    void recoveryRemovesDisposableSaveGenerations();
    void recoveryDoesNotTrustOrCleanLivePidOnlyJournal();
    void recoveryMonitorClearsAfterRecordedProcessExits();
    void currentSessionSnapshotRestoresExistingTree();
    void currentSessionSnapshotRemovesDisposableTreeWhenOriginallyAbsent();
    void currentSessionRestoreRejectsTamperedBackupWithoutTouchingLiveTree();
    void currentSessionSnapshotRejectsUnsafePath();
    void authoritativeResultAcceptsVerifiedRun();
    void authoritativeResultAcceptsLatestHotReloadGeneration();
    void authoritativeResultRejectsWrongZone();
    void analyzesPersistedAuthoritativeResult();
    void runtimeSmokeReachesGameplayReady();
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

static void appendU32(QByteArray* bytes, quint32 value)
{
    const quint32 encoded = qToLittleEndian(value);
    bytes->append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

static QByteArray minimalMob(bool embeddedGraph)
{
    QByteArray bytes;
    appendU32(&bytes, 0xA000);
    appendU32(&bytes, 8);
    if(embeddedGraph)
    {
        appendU32(&bytes, 0x31415926);
        appendU32(&bytes, 8);
    }
    return bytes;
}

static QByteArray externalGraph(quint32 width, quint32 height)
{
    QByteArray bytes;
    appendU32(&bytes, width);
    appendU32(&bytes, height);
    bytes.append(QByteArray(int(width * height * 152u), '\0'));
    return bytes;
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

void JmvMapTestLauncherTests::runtimeSmokeReachesGameplayReady()
{
    const QString gameRoot = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_GAME_ROOT");
    const QString modRoot = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_MOD_ROOT");
    const QString mprPath = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_MPR");
    const QString mobPath = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_MOB");
    if(gameRoot.isEmpty() || modRoot.isEmpty() || mprPath.isEmpty() || mobPath.isEmpty())
        QSKIP("Set EI_ENDURANCE_RUNTIME_SMOKE_* to run the owned-process gameplay smoke test.");

    auto readAll = [](const QString& path) {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    };
    const QByteArray mprData = readAll(mprPath);
    const QByteArray mobData = readAll(mobPath);
    QVERIFY2(!mprData.isEmpty(), qPrintable("Could not read smoke MPR: " + mprPath));
    QVERIFY2(!mobData.isEmpty(), qPrintable("Could not read smoke MOB: " + mobPath));

    SJmvMapTestLaunchInput input;
    input.profile.valid = true;
    input.profile.runtimeStackComplete = true;
    input.profile.runtimeStackValidated = true;
    input.profile.profileId = "jamais-vu";
    input.profile.profileName = "Jamais Vu runtime smoke";
    input.profile.gameRoot = QDir::cleanPath(gameRoot);
    input.profile.modRoot = QDir::cleanPath(modRoot);
    input.profile.mapDirectories << QDir(modRoot).filePath("Maps");
    input.profile.resourceDirectories
        << QDir(modRoot).filePath("res")
        << QDir(gameRoot).filePath("Res");
    input.profile.runtimeManifestId = "runtime-smoke";
    SRuntimeComponentEvaluation gameComponent;
    gameComponent.id = "game.exe";
    gameComponent.path = QDir(gameRoot).filePath("Starter/Engine/game.exe");
    gameComponent.exists = QFileInfo::exists(gameComponent.path);
    gameComponent.hashAccepted = true;
    gameComponent.enforceHash = true;
    gameComponent.sha256 = hash(readAll(gameComponent.path));
    input.profile.runtimeComponents.append(gameComponent);

    input.sourceMpr = QFileInfo(mprPath);
    input.sourceMob = QFileInfo(mobPath);
    input.mprData = mprData;
    input.mobData = mobData;
    input.databaseSourcePath = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_DATABASE",
        QDir(modRoot).filePath("res/database.res"));
    input.databaseData = readAll(input.databaseSourcePath);
    QVERIFY2(!input.databaseData.isEmpty(),
             qPrintable("Could not read smoke database: " + input.databaseSourcePath));
    input.mapEntry.zoneId = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_ZONE", "gz1g");
    input.mapEntry.mprName = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_MPR_NAME",
        QFileInfo(mprPath).completeBaseName());
    input.mapEntry.mobName = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_MOB_NAME",
        QFileInfo(mobPath).completeBaseName());
    input.mapEntry.sourceArchive = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_TEXTS_RES",
        QDir(modRoot).filePath("res/texts.res"));
    input.mapEntry.allodId = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_ALLOD", "gipat");
    input.spawn.valid = true;
    input.spawn.unobstructed = true;
    input.spawn.exitId = qEnvironmentVariableIntValue("EI_ENDURANCE_RUNTIME_SMOKE_EXIT");
    if(input.spawn.exitId <= 0)
        input.spawn.exitId = 1;
    bool xOk = false;
    bool yOk = false;
    const double x = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_X", "54").toDouble(&xOk);
    const double y = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_Y", "138").toDouble(&yOk);
    QVERIFY(xOk && yOk);
    input.spawn.position = QPointF(x, y);
    input.spawn.requestedPosition = input.spawn.position;
    input.syntheticMapIdentity = qEnvironmentVariableIntValue(
        "EI_ENDURANCE_RUNTIME_SMOKE_SYNTHETIC") != 0;
    input.mapSizeX = qEnvironmentVariableIntValue("EI_ENDURANCE_RUNTIME_SMOKE_SIZE_X");
    input.mapSizeY = qEnvironmentVariableIntValue("EI_ENDURANCE_RUNTIME_SMOKE_SIZE_Y");
    if(input.mapSizeX <= 0)
        input.mapSizeX = 7;
    if(input.mapSizeY <= 0)
        input.mapSizeY = 5;
    input.allowMissingAiGraph = qEnvironmentVariableIntValue(
        "EI_ENDURANCE_RUNTIME_SMOKE_ALLOW_GRAPHLESS") != 0;
    const QString graphPath = qEnvironmentVariable("EI_ENDURANCE_RUNTIME_SMOKE_GRAPH");
    if(!graphPath.isEmpty())
        input.generatedAiGraphData = readAll(graphPath);

    struct DumpStamp
    {
        qint64 size{0};
        QDateTime modified;
    };
    auto snapshotDumps = [](const QString& directory) {
        QHash<QString, DumpStamp> result;
        const QFileInfoList dumps = QDir(directory).entryInfoList(
            QStringList() << "*.dmp", QDir::Files, QDir::Name);
        for(const QFileInfo& dump : dumps)
        {
            DumpStamp stamp;
            stamp.size = dump.size();
            stamp.modified = dump.lastModified();
            result.insert(dump.absoluteFilePath().toLower(), stamp);
        }
        return result;
    };
    const QString dumpDirectory = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_DUMP_DIR",
        QDir(gameRoot).filePath("Starter/Dumps"));
    const QHash<QString, DumpStamp> dumpsBefore = snapshotDumps(dumpDirectory);

    CJmvMapTestLauncher launcher;
    QString error;
    QVERIFY2(launcher.launch(input, &error), qPrintable(error));
    QElapsedTimer timeout;
    timeout.start();
    while(timeout.elapsed() < 45000 && launcher.isRunning()
          && launcher.runtimeState() != "GameplayReady")
        QTest::qWait(50);
    const QString finalState = launcher.runtimeState();
    int dwellMs = qEnvironmentVariableIntValue("EI_ENDURANCE_RUNTIME_SMOKE_DWELL_MS");
    if(dwellMs <= 0)
        dwellMs = 15000;
    QElapsedTimer dwell;
    dwell.start();
    while(dwell.elapsed() < dwellMs && launcher.isRunning()
          && launcher.runtimeState() == "GameplayReady")
        QTest::qWait(50);
    const bool survivedDwell = launcher.isRunning()
        && launcher.runtimeState() == "GameplayReady";
    const QHash<QString, DumpStamp> dumpsAfter = snapshotDumps(dumpDirectory);
    QStringList newDumps;
    for(auto it = dumpsAfter.constBegin(); it != dumpsAfter.constEnd(); ++it)
    {
        const auto before = dumpsBefore.constFind(it.key());
        if(before == dumpsBefore.constEnd()
           || before->size != it->size || before->modified != it->modified)
            newDumps.append(it.key());
    }
    const QString runId = launcher.runId();
    launcher.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!launcher.isRunning(), 10000);
    QCOMPARE(finalState, QString("GameplayReady"));
    QVERIFY2(survivedDwell,
             "The game did not remain in GameplayReady for the required post-load dwell.");
    QVERIFY2(newDumps.isEmpty(),
             qPrintable("The game wrote a native dump after GameplayReady: "
                        + newDumps.join(", ")));
    QVERIFY2(!runId.isEmpty(), "Runtime smoke did not create a run ID.");
}

void JmvMapTestLauncherTests::validatesExternalGraphEnvelope()
{
    QString error;
    QVERIFY2(CJmvMapTestLauncher::validateExternalGraph(externalGraph(3, 2), &error),
             qPrintable(error));
    QByteArray truncated = externalGraph(3, 2);
    truncated.chop(1);
    QVERIFY(!CJmvMapTestLauncher::validateExternalGraph(truncated, &error));
    QVERIFY(error.contains("requires exactly"));
    QVERIFY(!CJmvMapTestLauncher::validateExternalGraph(externalGraph(0, 2), &error));
    QVERIFY(error.contains("invalid dimensions"));
}

void JmvMapTestLauncherTests::recognizesGraphlessBridgeProtocolMarker()
{
    const QString marker = QStringLiteral("EI_MAPPER_ENDURANCE_GRAPHLESS");
    const QByteArray encoded(reinterpret_cast<const char*>(marker.utf16()),
                             marker.size() * 2);
    QVERIFY(CJmvMapTestLauncher::bridgeSupportsGraphlessLaunch(
        QByteArray("prefix") + encoded + QByteArray("suffix")));
    QVERIFY(!CJmvMapTestLauncher::bridgeSupportsGraphlessLaunch(
        QByteArray("stale runtime bridge")));
    const QString syntheticMarker = QStringLiteral("EI_MAPPER_ENDURANCE_SYNTHETIC_MAP");
    const QByteArray syntheticEncoded(
        reinterpret_cast<const char*>(syntheticMarker.utf16()),
        syntheticMarker.size() * 2);
    QVERIFY(CJmvMapTestLauncher::bridgeSupportsSyntheticMapIdentity(
        QByteArray("prefix") + syntheticEncoded + QByteArray("suffix")));
    QVERIFY(!CJmvMapTestLauncher::bridgeSupportsSyntheticMapIdentity(
        QByteArray("stale runtime bridge")));
}

void JmvMapTestLauncherTests::detectsAutoSpellMobCapability()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString engine = QDir(temporary.path()).filePath("Starter/Engine");
    const QString mod = QDir(temporary.path()).filePath("Starter/MODS/TestMod");
    const QString maps = QDir(mod).filePath("Maps");
    QVERIFY(QDir().mkpath(engine));
    QVERIFY(QDir().mkpath(maps));

    SResourceProfileEvaluation profile;
    profile.profileId = "jamais-vu";
    profile.gameRoot = temporary.path();
    profile.modRoot = mod;
    profile.mapDirectories << maps;
    SRuntimeComponentEvaluation runtime;
    runtime.id = "starter-engine";
    runtime.path = QDir(engine).filePath("game.exe");
    profile.runtimeComponents.append(runtime);

    QString reason;
    QVERIFY(!CJmvMapTestLauncher::hasAutoSpellMob(profile, "game", false, &reason));
    QVERIFY(reason.contains("SpellAddon", Qt::CaseInsensitive));

    writeFile(QDir(engine).filePath("SpellAddonX.asi"), "asi");
    writeFile(QDir(maps).filePath("AutoRunSpellAddon123.mob"), "mob");
    QVERIFY(CJmvMapTestLauncher::hasAutoSpellMob(profile, "game", false, &reason));
    QVERIFY(reason.isEmpty());
    QVERIFY(CJmvMapTestLauncher::hasAutoSpellMob(profile, QString(), true, &reason));
    QVERIFY(!CJmvMapTestLauncher::hasAutoSpellMob(profile, "brief", false, &reason));
    QVERIFY(reason.contains("briefing", Qt::CaseInsensitive));
    QVERIFY(!CJmvMapTestLauncher::hasAutoSpellMob(profile, "edge", false, &reason));

    writeFile(QDir(mod).filePath("SPELLADDON.INI"),
              "[gameplay]\nautorunmobfile=0\n");
    QVERIFY(!CJmvMapTestLauncher::hasAutoSpellMob(profile, "game", false, &reason));
    QVERIFY(reason.contains("autorunmobfile", Qt::CaseInsensitive));

    profile.profileId = "base-game";
    QVERIFY(!CJmvMapTestLauncher::hasAutoSpellMob(profile, "game", false, &reason));
}

void JmvMapTestLauncherTests::resolvesWorkspaceExternalGraph()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString workspace = QDir(temporary.path()).filePath("workspace");
    const QString installed = QDir(temporary.path()).filePath("installed");
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(installed));
    const QString mprPath = QDir(workspace).filePath("Zone6.MPR");
    writeFile(mprPath, "mpr");
    const QByteArray workspaceGraph = externalGraph(2, 2);
    const QByteArray installedGraph = externalGraph(1, 1);
    const QString workspaceGraphPath = QDir(workspace).filePath("ZoNe6.GrP");
    writeFile(workspaceGraphPath, workspaceGraph);
    writeFile(QDir(installed).filePath("zone6.grp"), installedGraph);

    SMapGraphSource graph;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::resolveMapGraphSource(
        minimalMob(false), QFileInfo(mprPath), "zone6.mpr",
        QStringList() << installed, &graph, &error), qPrintable(error));
    QVERIFY(!graph.embedded);
    QCOMPARE(QFileInfo(graph.externalPath).absoluteFilePath(),
             QFileInfo(workspaceGraphPath).absoluteFilePath());
    QCOMPARE(graph.externalData, workspaceGraph);
    QCOMPARE(graph.sha256, hash(workspaceGraph));
}

void JmvMapTestLauncherTests::embeddedGraphShadowsExternalGraph()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mprPath = QDir(temporary.path()).filePath("zone6_2.mpr");
    const QString grpPath = QDir(temporary.path()).filePath("zone6_2.grp");
    writeFile(mprPath, "mpr");
    writeFile(grpPath, externalGraph(1, 1));

    SMapGraphSource graph;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::resolveMapGraphSource(
        minimalMob(true), QFileInfo(mprPath), "zone6_2.mpr",
        QStringList(), &graph, &error), qPrintable(error));
    QVERIFY(graph.embedded);
    QVERIFY(!graph.usesExternalGraph());
    QCOMPARE(QFileInfo(graph.shadowedExternalPath).absoluteFilePath(),
             QFileInfo(grpPath).absoluteFilePath());
}

void JmvMapTestLauncherTests::reportsMissingOptionalGraph()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mprPath = QDir(temporary.path()).filePath("newmap.mpr");
    writeFile(mprPath, "mpr");
    SMapGraphSource graph;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::resolveMapGraphSource(
        minimalMob(false), QFileInfo(mprPath), "newmap.mpr",
        QStringList(), &graph, &error), qPrintable(error));
    QVERIFY(graph.missing);
    QVERIFY(!graph.embedded);
    QVERIFY(!graph.usesExternalGraph());
}

void JmvMapTestLauncherTests::rejectsMalformedExternalGraph()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString mprPath = QDir(temporary.path()).filePath("broken.mpr");
    writeFile(mprPath, "mpr");
    QByteArray malformed = externalGraph(2, 2);
    malformed.chop(7);
    writeFile(QDir(temporary.path()).filePath("broken.grp"), malformed);
    SMapGraphSource graph;
    QString error;
    QVERIFY(!CJmvMapTestLauncher::resolveMapGraphSource(
        minimalMob(false), QFileInfo(mprPath), "broken.mpr",
        QStringList(), &graph, &error));
    QVERIFY(error.contains("external graph is invalid"));
    QVERIFY(error.contains("requires exactly"));
}

void JmvMapTestLauncherTests::resolvesActualZone6ExternalGraph()
{
    const QString maps = QString::fromLocal8Bit(qgetenv("EI_TEST_BASE_MAPS"));
    if(maps.isEmpty())
        QSKIP("Set EI_TEST_BASE_MAPS to run the installed Zone 6 graph check.");
    const QString mobPath = QDir(maps).filePath("zone6.mob");
    const QString mprPath = QDir(maps).filePath("zone6.mpr");
    QFile mob(mobPath);
    QVERIFY2(mob.open(QIODevice::ReadOnly), qPrintable(mob.errorString()));
    SMapGraphSource graph;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::resolveMapGraphSource(
        mob.readAll(), QFileInfo(mprPath), "zone6.mpr",
        QStringList() << maps, &graph, &error), qPrintable(error));
    QVERIFY(!graph.embedded);
    QCOMPARE(QFileInfo(graph.externalPath).fileName().toLower(), QString("zone6.grp"));
    QVERIFY(CJmvMapTestLauncher::validateExternalGraph(graph.externalData, &error));
}

void JmvMapTestLauncherTests::resolvesHostedGamePidForHotReload()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString pidPath = QDir(temporary.path()).filePath("game.pid");
    writeFile(pidPath, "424242\n");

    QCOMPARE(CJmvMapTestLauncher::runtimeGameProcessId(true, pidPath, 111111),
             qint64(424242));
    QCOMPARE(CJmvMapTestLauncher::runtimeGameProcessId(false, pidPath, 111111),
             qint64(111111));
    QCOMPARE(CJmvMapTestLauncher::runtimeGameProcessId(true,
             QDir(temporary.path()).filePath("missing.pid"), 111111), qint64(0));

    // alpha.29's x86 host wrote UTF-16LE without a BOM. Keep existing test-run
    // journals usable while the host protocol moves back to explicit ASCII.
    const QString legacyValue = QStringLiteral("46232\n");
    const QByteArray legacyUtf16(reinterpret_cast<const char*>(legacyValue.utf16()),
                                 legacyValue.size() * int(sizeof(ushort)));
    writeFile(pidPath, legacyUtf16);
    QCOMPARE(CJmvMapTestLauncher::runtimeGameProcessId(true, pidPath, 111111),
             qint64(46232));
}

static QJsonObject resultManifest(const QString& runRoot, const QString& sourcePath)
{
    QJsonObject session;
    session.insert("sourceZone", "zone1");
    session.insert("travelExit", 2);
    QJsonObject saves;
    saves.insert("activeGeneration", "0");
    saves.insert("testPath", QDir(runRoot).filePath("generations/000000/saves"));
    QJsonObject sources;
    sources.insert(sourcePath, hash(QByteArray("source map")));
    QJsonObject live;
    live.insert("mprPath", QDir(runRoot).filePath("removed-live.mpr"));
    live.insert("mobPath", QDir(runRoot).filePath("removed-live.mob"));
    live.insert("mprSha256", "mpr-hash");
    live.insert("mobSha256", "mob-hash");
    QJsonObject bridge;
    bridge.insert("path", QDir(runRoot).filePath("removed-bridge.plg"));
    bridge.insert("resultPath", QDir(runRoot).filePath("bridge-result.log"));
    QJsonObject manifest;
    manifest.insert("runId", "run-authoritative-1");
    manifest.insert("profileId", "jamais-vu");
    manifest.insert("runRoot", runRoot);
    manifest.insert("logPath", QDir(runRoot).filePath("runtime.log"));
    manifest.insert("sessionSettings", session);
    manifest.insert("isolatedSaves", saves);
    manifest.insert("sourceSha256", sources);
    manifest.insert("liveFiles", live);
    manifest.insert("bridge", bridge);
    return manifest;
}

static QString successfulBridgeEvidence(const QString& runRoot,
                                        const QString& zone = "zone1")
{
    return QString(
        "2026-07-21T00:00:00Z DIRECT_LAUNCH initialized New Game player zone=\"%1\" entrance_id=2 entrance_index=1 before transition\n"
        "2026-07-21T00:00:01Z DIRECT_LAUNCH redirecting normal single-player transition loader mpr=\"%2\" mob=\"%3\" context=0x1\n"
        "2026-07-21T00:00:02Z DIRECT_LAUNCH staged map loader returned result=1\n"
        "2026-07-21T00:00:02Z RUNTIME_STATE sequence=3 from=Transitioning to=GameplayReady evidence=gameplay\n")
        .arg(zone,
             QDir(runRoot).filePath("removed-live.mpr"),
             QDir(runRoot).filePath("removed-live.mob"));
}

void JmvMapTestLauncherTests::authoritativeResultAcceptsVerifiedRun()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    const QJsonObject report = CJmvMapTestLauncher::buildAuthoritativeResult(
        resultManifest(temp.path(), source), successfulBridgeEvidence(temp.path()), 0,
        int(QProcess::NormalExit), false, "Removed all owned runtime artifacts");
    QCOMPARE(report.value("actualZone").toString(), QString("zone1"));
    QCOMPARE(report.value("actualEntrance").toInt(), 2);
    QCOMPARE(report.value("reportedPlayerCount").toInt(), 1);
    QVERIFY(report.value("sourceFilesUnchanged").toBool());
    QVERIFY(report.value("cleanupComplete").toBool());
    QVERIFY(report.value("payloadHashesValidated").toBool());
    QCOMPARE(report.value("actualMprSha256").toString(), QString("mpr-hash"));
    QCOMPARE(report.value("actualMobSha256").toString(), QString("mob-hash"));
    QStringList findings;
    QVERIFY2(CJmvMapTestLauncher::validateAutomatedResult(report, &findings),
             qPrintable(findings.join('\n')));
}

void JmvMapTestLauncherTests::authoritativeResultAcceptsLatestHotReloadGeneration()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    QJsonObject manifest = resultManifest(temp.path(), source);
    const QString generationRoot = QDir(temp.path()).filePath("generations/000002");
    QJsonObject reload;
    reload.insert("sequence", "2");
    reload.insert("generation", "2");
    reload.insert("zoneId", "zone2");
    reload.insert("entranceId", 1);
    reload.insert("mprPath", QDir(generationRoot).filePath("map.mpr"));
    reload.insert("mobPath", QDir(generationRoot).filePath("map.mob"));
    reload.insert("mprSha256", "reload-mpr-hash");
    reload.insert("mobSha256", "reload-mob-hash");
    reload.insert("savePath", QDir(generationRoot).filePath("saves"));
    manifest.insert("hotReloads", QJsonArray{reload});
    const QString evidence = successfulBridgeEvidence(temp.path()) + QString(
        "HOT_RELOAD dispatch sequence=2 generation=2 runtime_state=GameplayReady zone=\"zone2\" entrance_id=1 thread=1\n"
        "DIRECT_LAUNCH initialized New Game player zone=\"zone2\" entrance_id=1 entrance_index=0 before transition\n"
        "DIRECT_LAUNCH redirecting normal single-player transition loader mpr=\"%1\" mob=\"%2\" context=0x2\n"
        "DIRECT_LAUNCH staged map loader returned result=1\n"
        "RUNTIME_STATE sequence=9 from=Transitioning to=GameplayReady evidence=gameplay\n"
        "HOT_RELOAD completed sequence=2 transition_result=1 staged_loader=observed\n")
        .arg(reload.value("mprPath").toString(), reload.value("mobPath").toString());
    const QJsonObject report = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, evidence, 0, int(QProcess::NormalExit), false,
        "Removed all owned runtime artifacts");
    QCOMPARE(report.value("actualZone").toString(), QString("zone2"));
    QCOMPARE(report.value("actualGeneration").toString(), QString("2"));
    QCOMPARE(report.value("actualMprSha256").toString(), QString("reload-mpr-hash"));
    QCOMPARE(report.value("actualMobSha256").toString(), QString("reload-mob-hash"));
    QCOMPARE(report.value("saveNamespace").toString(), reload.value("savePath").toString());
    QStringList findings;
    QVERIFY2(CJmvMapTestLauncher::validateAutomatedResult(report, &findings),
             qPrintable(findings.join('\n')));
}

void JmvMapTestLauncherTests::authoritativeResultRejectsWrongZone()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    const QJsonObject report = CJmvMapTestLauncher::buildAuthoritativeResult(
        resultManifest(temp.path(), source), successfulBridgeEvidence(temp.path(), "zone2"), 0,
        int(QProcess::NormalExit), false, QString());
    QVERIFY(!report.value("passed").toBool());
    QVERIFY(QJsonDocument(report.value("errors").toArray()).toJson()
            .contains("Requested zone zone1 but runtime reported zone2"));
}

void JmvMapTestLauncherTests::analyzesPersistedAuthoritativeResult()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    const QJsonObject expected = CJmvMapTestLauncher::buildAuthoritativeResult(
        resultManifest(temp.path(), source), successfulBridgeEvidence(temp.path()), 0,
        int(QProcess::NormalExit), false, QString());
    writeFile(QDir(temp.path()).filePath("result.json"),
              QJsonDocument(expected).toJson(QJsonDocument::Indented));
    QJsonObject actual;
    QStringList findings;
    QVERIFY2(CJmvMapTestLauncher::analyzeCompletedRun(temp.path(), &actual, &findings),
             qPrintable(findings.join('\n')));
    QCOMPARE(actual.value("runId").toString(), QString("run-authoritative-1"));
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
        true, 21.5,
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
    QVERIFY(script.contains("RunWorldTime(21.500000)"));
    QCOMPARE(script.count("RunWorldTime(21.500000)"), 2);
    QVERIFY(!script.contains("FixWorldTime("));
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
        QStringList(), false, 12.0,
        &entranceManifest, &entranceOutputPath, &error),
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

void JmvMapTestLauncherTests::recoveryRemovesOwnedGraph()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString graphPath = QDir(temp.path()).filePath("temporary-map.grp");
    const QByteArray graphBytes = externalGraph(2, 2);
    writeFile(graphPath, graphBytes);
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray());
    QVERIFY(!run.isEmpty());

    QFile manifestFile(QDir(run).filePath("manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    QJsonObject live;
    live.insert("grpPath", graphPath);
    live.insert("grpSha256", hash(graphBytes));
    manifest.insert("liveFiles", live);
    writeFile(QDir(run).filePath("manifest.json"),
              QJsonDocument(manifest).toJson(QJsonDocument::Indented));

    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY2(!QFileInfo::exists(graphPath), qPrintable(summary));
    QVERIFY(summary.contains("Removed temporary file"));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryRemovesOwnedPlaceholderResource()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString resourcePath = QDir(temp.path()).filePath("EIMapperEndurance-run.res");
    const QByteArray resourceBytes("owned standalone placeholder resource");
    writeFile(resourcePath, resourceBytes);
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray());
    QVERIFY(!run.isEmpty());
    QFile manifestFile(QDir(run).filePath("manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    QJsonObject live;
    live.insert("placeholderResPath", resourcePath);
    live.insert("placeholderResSha256", hash(resourceBytes));
    manifest.insert("liveFiles", live);
    writeFile(QDir(run).filePath("manifest.json"),
              QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY2(!QFileInfo::exists(resourcePath), qPrintable(summary));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryRetainsChangedPlaceholderResource()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString resourcePath = QDir(temp.path()).filePath("EIMapperEndurance-run.res");
    const QByteArray expected("owned standalone placeholder resource");
    writeFile(resourcePath, QByteArray("externally changed resource"));
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray());
    QVERIFY(!run.isEmpty());
    QFile manifestFile(QDir(run).filePath("manifest.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    QJsonObject live;
    live.insert("placeholderResPath", resourcePath);
    live.insert("placeholderResSha256", hash(expected));
    manifest.insert("liveFiles", live);
    writeFile(QDir(run).filePath("manifest.json"),
              QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    QVERIFY(QFileInfo::exists(resourcePath));
    QVERIFY(summary.contains("Retained changed temporary file"));
    QDir(run).removeRecursively();
}

void JmvMapTestLauncherTests::recoveryRetriesTransientBridgeLock()
{
#ifndef Q_OS_WIN
    QSKIP("The transient image-section lock regression is Windows-specific.");
#else
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString bridge = QDir(temp.path()).filePath("plugin.dll");
    const QByteArray owned("transiently locked bridge");
    writeFile(bridge, owned);
    const QString run = createRecoveryManifest(QString(), QString(), false,
        QByteArray(), QByteArray(), bridge, owned);
    QVERIFY(!run.isEmpty());

    HANDLE lock = CreateFileW(reinterpret_cast<LPCWSTR>(bridge.utf16()),
                              GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(lock != INVALID_HANDLE_VALUE);
    std::thread releaseLock([lock]()
    {
        Sleep(250);
        CloseHandle(lock);
    });
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns();
    releaseLock.join();
    QVERIFY2(!QFileInfo::exists(bridge), qPrintable(summary));
    QVERIFY(summary.contains("cleanup retries"));
    QDir(run).removeRecursively();
#endif
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

QTEST_GUILESS_MAIN(JmvMapTestLauncherTests)
#include "jmv_map_test_launcher_tests.moc"

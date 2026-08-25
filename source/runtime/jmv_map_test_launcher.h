#ifndef JMV_MAP_TEST_LAUNCHER_H
#define JMV_MAP_TEST_LAUNCHER_H

#include <QObject>
#include <QByteArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QVector>

#include "map_catalog.h"
#include "map_spawn_planner.h"
#include "resource_profile.h"

class QTimer;

struct SMapGraphSource
{
    bool embedded{false};
    bool missing{false};
    QString externalPath;
    QString shadowedExternalPath;
    QByteArray externalData;
    QString sha256;

    bool usesExternalGraph() const { return !externalPath.isEmpty(); }
};

struct SJmvSupplementalMobInput
{
    QFileInfo sourceMob;
    QByteArray mobData;
};

struct SJmvMapTestLaunchInput
{
    SResourceProfileEvaluation profile;
    QFileInfo sourceMpr;
    QFileInfo sourceMob;
    QFileInfo sourceTerrainTextureResource;
    QByteArray mprData;
    QByteArray mobData;
    // Optional complete run-owned RES containing the isolated terrain MMP
    // pages referenced by mprData. When absent, the ordinary placeholder
    // archive is staged for maps that use installed terrain resources.
    QByteArray terrainTextureResourceData;
    int expectedTerrainAtlasPages{0};
    int expectedTerrainAtlasSlots{0};
    QByteArray generatedAiGraphData;
    QString databaseSourcePath;
    QByteArray databaseData;
    QString tablesLuaSourcePath;
    QByteArray tablesLuaData;
    QVector<SJmvSupplementalMobInput> supplementalMobs;
    QString embeddedScript;
    SMapCatalogEntry mapEntry;
    SMapSpawnPlan spawn;
    double spawnZ{0.0};
    bool godmode{false};
    bool forceDrawall{false};
    bool overrideWorldTime{false};
    double worldTimeHours{12.0};
    bool borrowedMapIdentity{false};
    bool syntheticMapIdentity{false};
    int mapSizeX{0};
    int mapSizeY{0};
    bool allowMissingAiGraph{false};
    int baseProbeLevel{0};
    QByteArray temporaryMinimapMmp;
};

class CJmvMapTestLauncher : public QObject
{
    Q_OBJECT

public:
    explicit CJmvMapTestLauncher(QObject* parent = nullptr);
    ~CJmvMapTestLauncher() override;

    bool launch(const SJmvMapTestLaunchInput& input, QString* error = nullptr);
    bool hotReload(const SJmvMapTestLaunchInput& input, QString* error = nullptr);
    bool restart(const SJmvMapTestLaunchInput& input, QString* error = nullptr);
    bool isRunning() const;
    bool isHotReloadReady() const;
    bool isHotReloadPending() const { return m_pendingHotReloadSequence != 0; }
    bool isRestartPending() const { return m_restartPending; }
    bool isTerrainAtlasRestartPending() const
    { return m_terrainAtlasRestartPending; }
    bool isRecoveryPending() const { return m_recoveryPending; }
    QString runtimeState() const;
    quint64 saveGeneration() const { return m_saveGeneration; }
    QString runId() const { return m_runId; }
    QString temporaryMapName() const { return m_temporaryMapName; }
    QStringList startupResourceChanges(const SJmvMapTestLaunchInput& input) const;
    qint64 activeGameProcessId() const { return runtimeGameProcessId(); }
    bool runtimeFailureObserved() const { return m_runtimeFailureObserved; }
    QString runtimeFailureEvidence() const
    { return m_manifest.value("runtimeFailureEvidence").toString(); }
    void stop();

    static QString testRunsRootPath();
    static QString generationSavesPath(const QString& runRoot, quint64 generation);
    static QString generationPayloadFileName(quint64 generation,
                                             const QString& extension);
    static bool validateJmvGameRootPaths(const QString& selectedGameRoot,
                                         const QString& workPath,
                                         const QString& resourcesPath,
                                         const QString& cdRomPath,
                                         QString* error = nullptr);
    static bool resolveMapGraphSource(const QByteArray& primaryMobData,
                                      const QFileInfo& sourceMpr,
                                      const QString& logicalMprName,
                                      const QStringList& mapDirectories,
                                      SMapGraphSource* graph,
                                      QString* error = nullptr);
    static bool validateExternalGraph(const QByteArray& bytes,
                                      QString* error = nullptr);
    static bool bridgeSupportsGraphlessLaunch(const QByteArray& bridgeBytes);
    static bool bridgeSupportsSyntheticMapIdentity(const QByteArray& bridgeBytes);
    static bool hasAutoSpellMob(const SResourceProfileEvaluation& profile,
                                const QString& zoneType,
                                bool syntheticMapIdentity,
                                QString* reason = nullptr);
    static qint64 runtimeGameProcessId(bool usesRuntimeHost,
                                       const QString& gamePidPath,
                                       qint64 transportProcessId);
    static QStringList changedStartupResources(
        const QString& stagedDatabaseSha256,
        const QString& stagedTablesLuaSourcePath,
        const QString& stagedTablesLuaSha256,
        const SJmvMapTestLaunchInput& input);
    static bool isRuntimeFailureDialog(const QString& title,
                                       const QString& windowClass,
                                       const QString& bodyText);
    static QJsonObject buildAuthoritativeResult(const QJsonObject& manifest,
                                                const QString& bridgeResult,
                                                int exitCode,
                                                int exitStatus,
                                                bool stopRequested,
                                                const QString& cleanupMessage);
    static bool validateAutomatedResult(const QJsonObject& report,
                                        QStringList* findings = nullptr);
    static bool analyzeCompletedRun(const QString& runRoot,
                                    QJsonObject* report,
                                    QStringList* findings = nullptr);
    static bool rebaseRuntimeTerrainAtlas(const QByteArray& sourceMpr,
                                          const QByteArray& sourceAtlasRes,
                                          const QString& runtimeNamespace,
                                          int expectedPages,
                                          QByteArray* rebasedMpr,
                                          QByteArray* rebasedAtlasRes,
                                          QStringList* rebasedMmpMembers,
                                          QString* error = nullptr);
    static bool terrainAtlasHotReloadRequiresRestart(
        const QString& runningSourceAtlasSha256, int runningAtlasPages,
        int runningAtlasSlots, const QString& candidateSourceAtlasSha256,
        int candidateAtlasPages, int candidateAtlasSlots);
    static QString recoverInterruptedRuns(bool* pending = nullptr);
    static bool hasActiveInterruptedRun(QString* description = nullptr);
    static bool requestCloseActiveInterruptedRun(QString* error = nullptr);
    void refreshInterruptedRecovery();
    static bool injectWorldScriptBootstrap(const QString& script,
                                           const QString& commands,
                                           QString* output,
                                           QString* error = nullptr,
                                           const QString& immediateCommands = QString());
    static bool prepareTemporaryAutoexec(const QString& engineDirectory,
                                         const QString& runRoot,
                                         bool godmode,
                                         QJsonObject* manifest,
                                         QString* error = nullptr);
    static bool prepareRunOwnedDrawallAutoexec(const QString& sourcePath,
                                               const QString& runRoot,
                                               bool forceDrawall,
                                               QJsonObject* manifest,
                                               QString* outputPath,
                                               QString* error = nullptr);
    static bool prepareGodmodeBootstrap(const QStringList& mapDirectories,
                                        const QString& runRoot,
                                        const SMapSpawnPlan& spawn,
                                        double spawnZ,
                                        bool godmode,
                                        const QStringList& supplementalMobNames,
                                        bool overrideWorldTime,
                                        double worldTimeHours,
                                        QJsonObject* manifest,
                                        QString* outputPath,
                                        QString* error = nullptr);

signals:
    void testStarted(const QString& runId, const QString& temporaryMapName);
    void testFinished(const QString& runId, int exitCode, const QString& cleanupMessage);
    void testReportReady(const QJsonObject& report);
    void testError(const QString& message);
    void hotReloadQueued(quint64 sequence, const QString& zoneId);
    void hotReloadReady();
    void hotReloadFinished(quint64 sequence, bool success, const QString& message);
    void recoveryStatusChanged(const QString& message, bool pending);

private:
    bool stage(const SJmvMapTestLaunchInput& input, QString* error);
    bool queueTerrainAtlasRestart(const SJmvMapTestLaunchInput& input,
                                  const QString& candidateAtlasSha256,
                                  QString* error);
    bool installBridge(QString* error);
    QString cleanup();
    bool writeManifest(const QString& state, QString* error = nullptr);
    void appendRunLog(const QString& message);
    QString bridgeResultSummary() const;
    qint64 runtimeGameProcessId() const;
    quint64 latestRuntimeStateSequence() const;
    void focusGameWindow(bool maximize);

    QProcess* m_process;
    QTimer* m_hotReloadReadyTimer;
    QTimer* m_hotReloadResultTimer;
    QTimer* m_runtimeHealthTimer;
    QTimer* m_recoveryTimer;
    QString m_runId;
    QString m_temporaryMapName;
    QString m_runRoot;
    QString m_manifestPath;
    QString m_runLogPath;
    QString m_sharedRunLogPath;
    QString m_liveMprPath;
    QString m_liveMobPath;
    QString m_liveGrpPath;
    QString m_mapTableSourcePath;
    QString m_stagedMapTablePath;
    QString m_databaseSourcePath;
    QString m_stagedDatabasePath;
    QString m_tablesLuaSourcePath;
    QString m_stagedTablesLuaPath;
    QString m_livePlaceholderResourcePath;
    QString m_drawallAutoexecPath;
    QString m_bridgeSourcePath;
    QString m_bridgeLivePath;
    QString m_bridgeResultPath;
    QString m_testSavesPath;
    QString m_bridgeSha256;
    QString m_liveMprSha256;
    QString m_liveMobSha256;
    QString m_liveGrpSha256;
    QString m_mapTableSourceSha256;
    QString m_stagedMapTableSha256;
    QString m_databaseSourceSha256;
    QString m_stagedDatabaseSha256;
    QString m_tablesLuaSourceSha256;
    QString m_stagedTablesLuaSha256;
    QString m_livePlaceholderResourceSha256;
    QString m_sourceTerrainTextureResourceSha256;
    QString m_gameExePath;
    QString m_engineDirectory;
    QString m_runtimeHostPath;
    QString m_gamePidPath;
    QString m_liveSavesPath;
    QString m_hotReloadRequestPath;
    QString m_hotReloadThreadPath;
    QString m_supplementalMobDirectory;
    QString m_profileId;
    QString m_currentZoneId;
    QJsonObject m_manifest;
    quint64 m_hotReloadSequence{0};
    quint64 m_saveGeneration{0};
    quint64 m_pendingHotReloadSequence{0};
    quint64 m_pendingSaveGeneration{0};
    int m_sourceTerrainAtlasPages{0};
    int m_sourceTerrainAtlasSlots{0};
    int m_hotReloadResultPolls{0};
    bool m_cleanupComplete;
    bool m_stopRequested{false};
    bool m_processCrashObserved{false};
    bool m_runtimeFailureObserved{false};
    bool m_baseProbe;
    bool m_usesRuntimeHost{false};
    bool m_recoveryPending{false};
    bool m_terrainAtlasRestartPending{false};
    SJmvMapTestLaunchInput m_pendingTerrainAtlasRestartInput;
    bool m_lastPublishedRecoveryPending{false};
    QString m_lastPublishedRecoveryStatus;
    bool m_restartPending{false};
    SJmvMapTestLaunchInput m_restartInput;
};

#endif // JMV_MAP_TEST_LAUNCHER_H

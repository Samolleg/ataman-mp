#ifndef JMV_MAP_TEST_LAUNCHER_H
#define JMV_MAP_TEST_LAUNCHER_H

#include <QObject>
#include <QByteArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QVector>

#include "map_catalog.h"
#include "map_spawn_planner.h"
#include "resource_profile.h"

class QTimer;

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
    QByteArray mprData;
    QByteArray mobData;
    QVector<SJmvSupplementalMobInput> supplementalMobs;
    QString embeddedScript;
    SMapCatalogEntry mapEntry;
    SMapSpawnPlan spawn;
    double spawnZ{0.0};
    bool godmode{false};
    bool borrowedMapIdentity{false};
    int baseProbeLevel{0};
};

class CJmvMapTestLauncher : public QObject
{
    Q_OBJECT

public:
    explicit CJmvMapTestLauncher(QObject* parent = nullptr);
    ~CJmvMapTestLauncher() override;

    bool launch(const SJmvMapTestLaunchInput& input, QString* error = nullptr);
    bool hotReload(const SJmvMapTestLaunchInput& input, QString* error = nullptr);
    bool isRunning() const;
    bool isHotReloadReady() const;
    bool isHotReloadPending() const { return m_pendingHotReloadSequence != 0; }
    QString runId() const { return m_runId; }
    QString temporaryMapName() const { return m_temporaryMapName; }
    void stop();

    static QString testRunsRootPath();
    static QString testSavesPathForProfile(const SResourceProfileEvaluation& profile);
    static QString recoverInterruptedRuns();
    static bool injectWorldScriptBootstrap(const QString& script,
                                           const QString& commands,
                                           QString* output,
                                           QString* error = nullptr,
                                           const QString& immediateCommands = QString());
    static bool hasConfiguredJmvPluginSlot(const QByteArray& configReg);
    static bool prepareTemporaryAutoexec(const QString& engineDirectory,
                                         const QString& runRoot,
                                         bool godmode,
                                         QJsonObject* manifest,
                                         QString* error = nullptr);
    static bool prepareGodmodeBootstrap(const QStringList& mapDirectories,
                                        const QString& runRoot,
                                        const SMapSpawnPlan& spawn,
                                        double spawnZ,
                                        bool godmode,
                                        const QStringList& supplementalMobNames,
                                        QJsonObject* manifest,
                                        QString* outputPath,
                                        QString* error = nullptr);

signals:
    void testStarted(const QString& runId, const QString& temporaryMapName);
    void testFinished(const QString& runId, int exitCode, const QString& cleanupMessage);
    void testError(const QString& message);
    void hotReloadQueued(quint64 sequence, const QString& zoneId);
    void hotReloadReady();
    void hotReloadFinished(quint64 sequence, bool success, const QString& message);

private:
    bool stage(const SJmvMapTestLaunchInput& input, QString* error);
    bool installBridge(QString* error);
    QString cleanup();
    bool writeManifest(const QString& state, QString* error = nullptr);
    void appendRunLog(const QString& message);
    QString bridgeResultSummary() const;
    void focusGameWindow();

    QProcess* m_process;
    QTimer* m_hotReloadReadyTimer;
    QTimer* m_hotReloadResultTimer;
    QString m_runId;
    QString m_temporaryMapName;
    QString m_runRoot;
    QString m_manifestPath;
    QString m_runLogPath;
    QString m_liveMprPath;
    QString m_liveMobPath;
    QString m_bridgeSourcePath;
    QString m_bridgeLivePath;
    QString m_bridgeResultPath;
    QString m_testSavesPath;
    QString m_bridgeSha256;
    QString m_liveMprSha256;
    QString m_liveMobSha256;
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
    quint64 m_pendingHotReloadSequence{0};
    int m_hotReloadResultPolls{0};
    bool m_cleanupComplete;
    bool m_baseProbe;
};

#endif // JMV_MAP_TEST_LAUNCHER_H

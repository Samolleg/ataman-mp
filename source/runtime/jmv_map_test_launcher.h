#ifndef JMV_MAP_TEST_LAUNCHER_H
#define JMV_MAP_TEST_LAUNCHER_H

#include <QObject>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>

#include "map_catalog.h"
#include "map_spawn_planner.h"
#include "resource_profile.h"

struct SJmvMapTestLaunchInput
{
    SResourceProfileEvaluation profile;
    QFileInfo sourceMpr;
    QFileInfo sourceMob;
    QByteArray mprData;
    QByteArray mobData;
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
    bool isRunning() const;
    QString runId() const { return m_runId; }
    QString temporaryMapName() const { return m_temporaryMapName; }
    void stop();

    static QString testRunsRootPath();
    static QString testSavesPathForProfile(const SResourceProfileEvaluation& profile);
    static QString recoverInterruptedRuns();
    static bool injectWorldScriptBootstrap(const QString& script,
                                           const QString& commands,
                                           QString* output,
                                           QString* error = nullptr);
    static bool hasConfiguredJmvPluginSlot(const QByteArray& configReg);
    static bool prepareTemporaryAutoexec(const QString& engineDirectory,
                                         const QString& runRoot,
                                         bool godmode,
                                         QJsonObject* manifest,
                                         QString* error = nullptr);
    static bool prepareGodmodeBootstrap(const QStringList& mapDirectories,
                                        const QString& runRoot,
                                        bool godmode,
                                        QJsonObject* manifest,
                                        QString* outputPath,
                                        QString* error = nullptr);

signals:
    void testStarted(const QString& runId, const QString& temporaryMapName);
    void testFinished(const QString& runId, int exitCode, const QString& cleanupMessage);
    void testError(const QString& message);

private:
    bool stage(const SJmvMapTestLaunchInput& input, QString* error);
    bool installBridge(QString* error);
    QString cleanup();
    bool writeManifest(const QString& state, QString* error = nullptr);
    void appendRunLog(const QString& message);
    QString bridgeResultSummary() const;
    void focusGameWindow();

    QProcess* m_process;
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
    QJsonObject m_manifest;
    bool m_cleanupComplete;
    bool m_baseProbe;
};

#endif // JMV_MAP_TEST_LAUNCHER_H

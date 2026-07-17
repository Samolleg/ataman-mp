#include "jmv_map_test_launcher.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QProcessEnvironment>

#include "endurance_document.h"
#include "runtime_providers.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {
QString sha256(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString fileSha256(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

bool readFile(const QString& path, QByteArray* bytes, QString* error)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = QString("Could not read %1: %2").arg(path, file.errorString());
        return false;
    }
    *bytes = file.readAll();
    return true;
}

bool atomicWrite(const QString& path, const QByteArray& bytes, QString* error)
{
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error)
            *error = QString("Could not atomically write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool processExists(qint64 pid)
{
#ifdef Q_OS_WIN
    if(pid <= 0)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if(!process)
        return false;
    DWORD exitCode = 0;
    const bool exists = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return exists;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

bool anotherGameIsRunning()
{
#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot == INVALID_HANDLE_VALUE)
        return true;
    PROCESSENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    bool found = false;
    if(Process32FirstW(snapshot, &entry))
    {
        do
        {
            if(QString::fromWCharArray(entry.szExeFile).compare("game.exe", Qt::CaseInsensitive) == 0)
            {
                found = true;
                break;
            }
        }
        while(Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
#else
    return false;
#endif
}

QString testRunsRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath("test-runs");
}

QString normalized(const QString& path)
{
    return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool removeIfOwned(const QString& path, const QString& expectedHash, QStringList* messages)
{
    if(!QFileInfo::exists(path))
        return true;
    const QString currentHash = fileSha256(path);
    if(currentHash.compare(expectedHash, Qt::CaseInsensitive) != 0)
    {
        messages->append(QString("Retained changed temporary file: %1").arg(path));
        return false;
    }
    if(!QFile::remove(path))
    {
        messages->append(QString("Could not remove temporary file: %1").arg(path));
        return false;
    }
    messages->append(QString("Removed temporary file: %1").arg(path));
    return true;
}

bool restoreAutoexec(const QJsonObject& manifest, QStringList* messages)
{
    const QJsonObject autoexec = manifest.value("autoexec").toObject();
    const QString path = autoexec.value("path").toString();
    const QString backupPath = autoexec.value("backupPath").toString();
    const QString originalHash = autoexec.value("originalSha256").toString();
    const QString patchedHash = autoexec.value("patchedSha256").toString();
    const bool originalExisted = autoexec.value("originalExisted").toBool(true);
    if(path.isEmpty())
        return true;
    if(!originalExisted)
    {
        if(!QFileInfo::exists(path))
            return true;
        if(fileSha256(path).compare(patchedHash, Qt::CaseInsensitive) != 0)
        {
            messages->append(QString("Did not remove externally changed temporary autoexec file: %1").arg(path));
            return false;
        }
        if(!QFile::remove(path))
        {
            messages->append(QString("Could not remove temporary autoexec file: %1").arg(path));
            return false;
        }
        messages->append(QString("Removed temporary autoexec file: %1").arg(path));
        return true;
    }
    if(backupPath.isEmpty())
        return false;
    const QString currentHash = fileSha256(path);
    if(currentHash.compare(originalHash, Qt::CaseInsensitive) == 0)
        return true;
    if(currentHash.compare(patchedHash, Qt::CaseInsensitive) != 0)
    {
        messages->append(QString("Did not overwrite externally changed autoexec file: %1").arg(path));
        return false;
    }
    QByteArray backup;
    QString error;
    if(!readFile(backupPath, &backup, &error) || sha256(backup).compare(originalHash, Qt::CaseInsensitive) != 0)
    {
        messages->append(error.isEmpty() ? "Autoexec recovery backup hash mismatch." : error);
        return false;
    }
    if(!atomicWrite(path, backup, &error))
    {
        messages->append(error);
        return false;
    }
    messages->append(QString("Restored autoexec file: %1").arg(path));
    return true;
}

bool cleanupManifest(const QJsonObject& manifest, QStringList* messages)
{
    bool success = restoreAutoexec(manifest, messages);
    const QJsonObject bridge = manifest.value("bridge").toObject();
    success = removeIfOwned(bridge.value("path").toString(),
                            bridge.value("sha256").toString(), messages) && success;
    const QJsonObject live = manifest.value("liveFiles").toObject();
    success = removeIfOwned(live.value("mprPath").toString(),
                            live.value("mprSha256").toString(), messages) && success;
    success = removeIfOwned(live.value("mobPath").toString(),
                            live.value("mobSha256").toString(), messages) && success;
    return success;
}

QJsonObject runtimeHashes(const SResourceProfileEvaluation& profile)
{
    QJsonObject result;
    for(const SRuntimeComponentEvaluation& component : profile.runtimeComponents)
        result.insert(component.id, component.sha256);
    return result;
}

#ifdef Q_OS_WIN
struct FocusContext { DWORD pid; HWND window; };
BOOL CALLBACK findWindowForProcess(HWND window, LPARAM parameter)
{
    FocusContext* context = reinterpret_cast<FocusContext*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if(pid == context->pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)
    {
        context->window = window;
        return FALSE;
    }
    return TRUE;
}
#endif
}

CJmvMapTestLauncher::CJmvMapTestLauncher(QObject* parent)
    : QObject(parent), m_process(new QProcess(this)),
      m_cleanupComplete(true)
{
    connect(m_process, &QProcess::started, this, [this]()
    {
        appendRunLog(QString("game.exe started pid=%1").arg(m_process->processId()));
        m_manifest.insert("pid", double(m_process->processId()));
        writeManifest("running");
        emit testStarted(m_runId, m_temporaryMapName);
        QTimer::singleShot(1500, this, &CJmvMapTestLauncher::focusGameWindow);
        QTimer::singleShot(5000, this, &CJmvMapTestLauncher::focusGameWindow);
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status)
    {
        appendRunLog(QString("game.exe finished exitCode=%1 status=%2")
                     .arg(exitCode).arg(int(status)));
        const QString bridgeResult = bridgeResultSummary();
        appendRunLog(bridgeResult);
        QString cleanupMessage = cleanup();
        if(!cleanupMessage.isEmpty())
            cleanupMessage += "\n";
        cleanupMessage += bridgeResult;
        emit testFinished(m_runId, exitCode, cleanupMessage);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
    {
        appendRunLog(QString("game.exe process error=%1 text=%2")
                     .arg(int(error)).arg(m_process->errorString()));
        if(error == QProcess::FailedToStart)
        {
            const QString cleanupMessage = cleanup();
            emit testError(QString("JMV test game failed to start: %1\n\n%2")
                           .arg(m_process->errorString(), cleanupMessage));
        }
    });
}

CJmvMapTestLauncher::~CJmvMapTestLauncher()
{
    if(!isRunning())
        cleanup();
}

bool CJmvMapTestLauncher::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

bool CJmvMapTestLauncher::injectWorldScriptBootstrap(const QString& script,
                                                     const QString& commands,
                                                     QString* output,
                                                     QString* error)
{
    if(!output || commands.trimmed().isEmpty())
        return false;
    enum class State { Code, String, LineComment, BlockComment };
    State state = State::Code;
    for(int index = 0; index < script.size(); ++index)
    {
        const QChar current = script.at(index);
        const QChar next = index + 1 < script.size() ? script.at(index + 1) : QChar();
        if(state == State::String)
        {
            if(current == '\\')
                ++index;
            else if(current == '"')
                state = State::Code;
            continue;
        }
        if(state == State::LineComment)
        {
            if(current == '\n' || current == '\r')
                state = State::Code;
            continue;
        }
        if(state == State::BlockComment)
        {
            if(current == '*' && next == '/')
            {
                state = State::Code;
                ++index;
            }
            continue;
        }
        if(current == '"') { state = State::String; continue; }
        if(current == '/' && next == '/') { state = State::LineComment; ++index; continue; }
        if(current == '/' && next == '*') { state = State::BlockComment; ++index; continue; }

        static const QString token = "WorldScript";
        if(script.midRef(index, token.size()).compare(token, Qt::CaseInsensitive) != 0)
            continue;
        const bool leftBoundary = index == 0 || !(script.at(index - 1).isLetterOrNumber() || script.at(index - 1) == '_');
        const int tokenEnd = index + token.size();
        const bool rightBoundary = tokenEnd >= script.size() ||
                !(script.at(tokenEnd).isLetterOrNumber() || script.at(tokenEnd) == '_');
        if(!leftBoundary || !rightBoundary)
            continue;
        int open = tokenEnd;
        while(open < script.size() && script.at(open).isSpace())
            ++open;
        if(open >= script.size() || script.at(open) != '(')
        {
            if(error)
                *error = "WorldScript is present but is not followed by an opening parenthesis.";
            return false;
        }
        *output = script.left(open + 1) + "\n" + commands.trimmed() + "\n" + script.mid(open + 1);
        return true;
    }
    if(state == State::String || state == State::BlockComment)
    {
        if(error)
            *error = "The embedded script has an unterminated string or block comment.";
        return false;
    }
    *output = script;
    if(!output->isEmpty() && !output->endsWith('\n'))
        output->append('\n');
    output->append("WorldScript(\n" + commands.trimmed() + "\n)\n");
    return true;
}

bool CJmvMapTestLauncher::hasConfiguredJmvPluginSlot(const QByteArray& configReg)
{
    static const QByteArray record("\x02\x06\x00Plugin\x0a\x00plugin.dll", 21);
    return configReg.contains(record);
}

bool CJmvMapTestLauncher::launch(const SJmvMapTestLaunchInput& input, QString* error)
{
    if(isRunning())
    {
        if(error) *error = "An Endurance test game is already running.";
        return false;
    }
    if(anotherGameIsRunning())
    {
        if(error) *error = "A game.exe process is already running. Close it before starting an Endurance test.";
        return false;
    }
    m_cleanupComplete = false;
    if(!stage(input, error))
    {
        cleanup();
        return false;
    }
    if(!installBridge(error))
    {
        cleanup();
        return false;
    }
    if(!writeManifest("launching", error))
    {
        cleanup();
        return false;
    }
    appendRunLog(QString("Launching %1 workingDirectory=%2 map=%3")
                 .arg(m_gameExePath, m_engineDirectory, m_temporaryMapName));
    m_process->setProgram(m_gameExePath);
    m_process->setWorkingDirectory(m_engineDirectory);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("EI_MAPPER_ENDURANCE_MAP", m_temporaryMapName);
    environment.insert("EI_MAPPER_ENDURANCE_SOURCE_MAP", input.mapEntry.mprName.toLower());
    environment.insert("EI_MAPPER_ENDURANCE_TEMP_MPR", m_liveMprPath);
    environment.insert("EI_MAPPER_ENDURANCE_TEMP_MOB", m_liveMobPath);
    environment.insert("EI_MAPPER_ENDURANCE_BRIDGE_RESULT", m_bridgeResultPath);
    m_process->setProcessEnvironment(environment);
    m_process->start();
    return true;
}

bool CJmvMapTestLauncher::stage(const SJmvMapTestLaunchInput& input, QString* error)
{
    if(input.profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) != 0 ||
       !input.profile.valid || !input.profile.runtimeStackValidated)
    {
        if(error) *error = "The active JMV runtime stack is incomplete or does not match the validated manifest.";
        return false;
    }
    if(!input.spawn.valid || input.mprData.isEmpty() || input.mobData.isEmpty())
    {
        if(error) *error = "The loaded map pair or its unobstructed travel-zone spawn is incomplete.";
        return false;
    }
    if(input.profile.mapDirectories.isEmpty() || input.profile.modRoot.isEmpty())
    {
        if(error) *error = "The active JMV profile has no writable mod Maps directory.";
        return false;
    }
    const QString sourceMapName = input.mapEntry.mprName.toLower();
    if(!QRegularExpression("^[a-z0-9_]{1,63}$").match(sourceMapName).hasMatch())
    {
        if(error) *error = "The selected map has no safe runtime map identity for the JMV bridge.";
        return false;
    }

    const QString mapsDirectory = QDir(input.profile.modRoot).filePath("Maps");
    if(normalized(input.profile.mapDirectories.first()).compare(normalized(mapsDirectory), Qt::CaseInsensitive) != 0)
    {
        if(error) *error = "The highest-priority Maps directory is not the active JMV mod Maps directory.";
        return false;
    }
    m_gameExePath = QDir(input.profile.gameRoot).filePath("Starter/Engine/game.exe");
    m_engineDirectory = QFileInfo(m_gameExePath).absolutePath();
    m_bridgeSourcePath = QDir(QCoreApplication::applicationDirPath())
            .filePath("runtime/EIMapperEnduranceJmvBridge.plg");
    const QString modConfigPath = QDir(input.profile.modRoot).filePath("config.reg");
    QByteArray modConfig;
    if(!readFile(modConfigPath, &modConfig, error) ||
       !hasConfiguredJmvPluginSlot(modConfig))
    {
        if(error && error->isEmpty())
            *error = "The active JMV config.reg does not declare the supported DEV Plugin=plugin.dll slot.";
        return false;
    }
    // JMV's config.reg declares DEV/Plugin as plugin.dll. Starter loads that
    // exact configured file; its *.plg scan is used only when the configured
    // value names a directory. Keep the packaged provider descriptively named,
    // but install it temporarily into the already-declared, currently empty slot.
    m_bridgeLivePath = QDir(input.profile.modRoot).filePath("plugin.dll");
    if(!QFileInfo(m_gameExePath).isFile())
    {
        if(error) *error = "The validated Starter game.exe is missing.";
        return false;
    }
    if(!QFileInfo(m_bridgeSourcePath).isFile())
    {
        if(error) *error = "The packaged JMV launch bridge is missing. Reinstall this Endurance build.";
        return false;
    }
    if(QFileInfo::exists(m_bridgeLivePath))
    {
        if(error) *error = "The JMV DEV Plugin path already exists. Endurance will not replace an installed mod plugin; run recovery or inspect plugin.dll first.";
        return false;
    }

    m_runId = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-") +
            QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, '0');
    m_temporaryMapName = "ed" + m_runId.right(8).toLower();
    m_runRoot = QDir(testRunsRoot()).filePath(m_runId);
    m_manifestPath = QDir(m_runRoot).filePath("manifest.json");
    m_runLogPath = QDir(m_runRoot).filePath("runtime.log");
    m_bridgeResultPath = QDir(m_runRoot).filePath("bridge-result.log");
    m_liveMprPath = QDir(mapsDirectory).filePath(m_temporaryMapName + ".mpr");
    m_liveMobPath = QDir(mapsDirectory).filePath(m_temporaryMapName + ".mob");
    if(QFileInfo::exists(m_liveMprPath) || QFileInfo::exists(m_liveMobPath))
    {
        if(error) *error = "The randomly generated Endurance test-map name already exists; retry the launch.";
        return false;
    }
    if(!QDir().mkpath(m_runRoot))
    {
        if(error) *error = "Could not create the disposable Endurance test-run directory.";
        return false;
    }

    const QString heroName = "edhero" + m_runId.right(8).toLower();
    const QString x = QString::number(input.spawn.position.x(), 'f', 3);
    const QString y = QString::number(input.spawn.position.y(), 'f', 3);
    const QString commands = QString(
        "ConsoleString(\"Endurance test run %1\")\n"
        "AddUnitToServer(\"%2\",\"Human Hero\",\"\",%3,%4,GetZValue(%3,%4))\n"
        "SetPlayer(GetObjectByName(\"%2\"),0)\n"
        "AddUnitUnderControl(0,GetObjectByName(\"%2\"))\n"
        "RedeployParty(0)")
        .arg(m_runId, heroName, x, y);
    QString testScript;
    if(!injectWorldScriptBootstrap(input.embeddedScript, commands, &testScript, error))
        return false;

    CMobDocument sourceMob;
    CValidationReport report;
    if(!sourceMob.loadBytes(input.mobData, "test-run-source.mob", &report))
    {
        if(error) *error = "The in-memory MOB candidate failed validation:\n" + report.toText();
        return false;
    }
    QByteArray stagedMob;
    if(!sourceMob.replaceEmbeddedScript(testScript, &stagedMob, &report))
    {
        if(error) *error = "Could not inject the disposable player bootstrap losslessly:\n" + report.toText();
        return false;
    }

    const QString payloadDirectory = QDir(m_runRoot).filePath("payload");
    if(!QDir().mkpath(payloadDirectory))
    {
        if(error) *error = "Could not create the test-run payload directory.";
        return false;
    }
    const QString stagedMprPath = QDir(payloadDirectory).filePath(m_temporaryMapName + ".mpr");
    const QString stagedMobPath = QDir(payloadDirectory).filePath(m_temporaryMapName + ".mob");
    if(!atomicWrite(stagedMprPath, input.mprData, error) ||
       !atomicWrite(stagedMobPath, stagedMob, error))
        return false;
    CMprDocument mprDocument;
    if(!mprDocument.load(QFileInfo(stagedMprPath), &report))
    {
        if(error) *error = "The staged MPR failed validation:\n" + report.toText();
        return false;
    }
    CMobDocument mobDocument;
    if(!mobDocument.load(QFileInfo(stagedMobPath), &report))
    {
        if(error) *error = "The staged MOB failed validation:\n" + report.toText();
        return false;
    }

    m_liveMprSha256 = sha256(input.mprData);
    m_liveMobSha256 = sha256(stagedMob);
    QJsonObject liveFiles;
    liveFiles.insert("mprPath", m_liveMprPath);
    liveFiles.insert("mprSha256", m_liveMprSha256);
    liveFiles.insert("mobPath", m_liveMobPath);
    liveFiles.insert("mobSha256", m_liveMobSha256);
    m_manifest.insert("liveFiles", liveFiles);
    if(!atomicWrite(m_liveMprPath, input.mprData, error) ||
       !atomicWrite(m_liveMobPath, stagedMob, error))
        return false;
    if(fileSha256(m_liveMprPath) != m_liveMprSha256 || fileSha256(m_liveMobPath) != m_liveMobSha256)
    {
        if(error) *error = "A temporary live map copy failed its post-write SHA-256 check.";
        return false;
    }

    SMapTestLaunchRequest request;
    request.runId = m_runId;
    request.profileId = input.profile.profileId;
    request.runtimeManifestId = input.profile.runtimeManifestId;
    request.runtimeComponentHashes = runtimeHashes(input.profile);
    request.stagedMprPath = stagedMprPath;
    request.internalMapName = input.mapEntry.mprName;
    request.orderedStagedMobPaths << stagedMobPath;
    request.activeMobPath = stagedMobPath;
    request.sourceSha256.insert(input.sourceMpr.absoluteFilePath(),
                                fileSha256(input.sourceMpr.absoluteFilePath()));
    request.sourceSha256.insert(input.sourceMob.absoluteFilePath(),
                                fileSha256(input.sourceMob.absoluteFilePath()));
    request.stagedSha256.insert(QFileInfo(stagedMprPath).fileName(), m_liveMprSha256);
    request.stagedSha256.insert(QFileInfo(stagedMobPath).fileName(), m_liveMobSha256);
    request.sessionSettings.insert("travelExit", input.spawn.exitId);
    request.sessionSettings.insert("spawnX", input.spawn.position.x());
    request.sessionSettings.insert("spawnY", input.spawn.position.y());
    request.sessionSettings.insert("clearance", input.spawn.clearance);
    request.logPath = m_runLogPath;
    request.resultPath = QDir(m_runRoot).filePath("result.json");
    QStringList requestErrors;
    if(!validateMapTestLaunchRequest(request, m_runRoot, &requestErrors))
    {
        if(error) *error = requestErrors.join("\n");
        return false;
    }

    m_manifest = mapTestLaunchRequestToJson(request);
    m_manifest.insert("createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_manifest.insert("temporaryMapName", m_temporaryMapName);
    m_manifest.insert("liveFiles", liveFiles);
    appendRunLog(QString("Staged temporary map=%1 exit=%2 spawn=(%3,%4) clearance=%5")
                 .arg(m_temporaryMapName).arg(input.spawn.exitId)
                 .arg(x, y).arg(input.spawn.clearance, 0, 'f', 3));
    return writeManifest("staged", error);
}

bool CJmvMapTestLauncher::installBridge(QString* error)
{
    QByteArray bridge;
    if(!readFile(m_bridgeSourcePath, &bridge, error) || bridge.isEmpty())
        return false;
    m_bridgeSha256 = sha256(bridge);
    QJsonObject bridgeManifest;
    bridgeManifest.insert("sourcePath", m_bridgeSourcePath);
    bridgeManifest.insert("path", m_bridgeLivePath);
    bridgeManifest.insert("sha256", m_bridgeSha256);
    bridgeManifest.insert("resultPath", m_bridgeResultPath);
    m_manifest.insert("bridge", bridgeManifest);
    if(!atomicWrite(m_bridgeLivePath, bridge, error) ||
       fileSha256(m_bridgeLivePath).compare(m_bridgeSha256, Qt::CaseInsensitive) != 0)
    {
        if(error && error->isEmpty())
            *error = "The temporary JMV launch bridge failed its SHA-256 check.";
        return false;
    }
    appendRunLog(QString("Installed temporary JMV launch bridge sha256=%1 path=%2")
                 .arg(m_bridgeSha256, m_bridgeLivePath));
    return writeManifest("bridge-installed", error);
}

bool CJmvMapTestLauncher::writeManifest(const QString& state, QString* error)
{
    if(m_manifestPath.isEmpty())
        return false;
    m_manifest.insert("state", state);
    m_manifest.insert("updatedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return atomicWrite(m_manifestPath, QJsonDocument(m_manifest).toJson(QJsonDocument::Indented), error);
}

void CJmvMapTestLauncher::appendRunLog(const QString& message)
{
    if(m_runLogPath.isEmpty())
        return;
    QFile log(m_runLogPath);
    if(log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        QTextStream stream(&log);
        stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
               << " " << message << "\n";
    }
}

QString CJmvMapTestLauncher::bridgeResultSummary() const
{
    QFile result(m_bridgeResultPath);
    if(!result.open(QIODevice::ReadOnly | QIODevice::Text))
        return "JMV bridge produced no result log; Starter may not have loaded the temporary provider.";
    const QString text = QString::fromUtf8(result.readAll()).trimmed();
    return text.isEmpty() ?
        "JMV bridge result log was empty; the runtime did not report bridge readiness." :
        "JMV bridge result:\n" + text;
}

QString CJmvMapTestLauncher::cleanup()
{
    if(m_cleanupComplete)
        return QString();
    QStringList messages;
    const bool success = cleanupManifest(m_manifest, &messages);
    m_cleanupComplete = success;
    writeManifest(success ? "cleaned" : "cleanup-needed");
    appendRunLog(messages.join(" | "));
    return messages.join("\n");
}

void CJmvMapTestLauncher::stop()
{
    if(!isRunning())
        return;
    appendRunLog("Stop Test requested by user");
    m_process->terminate();
    QTimer::singleShot(5000, this, [this]()
    {
        if(isRunning())
        {
            appendRunLog("Graceful stop timed out; terminating the Endurance-owned process");
            m_process->kill();
        }
    });
}

void CJmvMapTestLauncher::focusGameWindow()
{
#ifdef Q_OS_WIN
    if(!isRunning())
        return;
    FocusContext context{DWORD(m_process->processId()), nullptr};
    EnumWindows(findWindowForProcess, LPARAM(&context));
    if(context.window)
    {
        ShowWindow(context.window, SW_RESTORE);
        const bool focused = SetForegroundWindow(context.window) != FALSE;
        appendRunLog(QString("Game window focus requested hwnd=0x%1 result=%2")
                     .arg(quintptr(context.window), 0, 16).arg(focused));
    }
    else
        appendRunLog("Game window not ready for focus yet");
#endif
}

QString CJmvMapTestLauncher::recoverInterruptedRuns()
{
    QStringList summary;
    const QDir root(testRunsRoot());
    if(!root.exists())
        return QString();
    for(const QFileInfo& runDirectory : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        const QString manifestPath = QDir(runDirectory.absoluteFilePath()).filePath("manifest.json");
        QFile manifestFile(manifestPath);
        if(!manifestFile.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        if(parseError.error != QJsonParseError::NoError || !document.isObject())
            continue;
        QJsonObject manifest = document.object();
        const QString state = manifest.value("state").toString();
        if(state == "cleaned")
            continue;
        const qint64 pid = qint64(manifest.value("pid").toDouble());
        if(processExists(pid))
            continue;
        QStringList messages;
        const bool success = cleanupManifest(manifest, &messages);
        manifest.insert("state", success ? "cleaned" : "cleanup-needed");
        manifest.insert("recoveredUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        QString writeError;
        atomicWrite(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented), &writeError);
        summary.append(QString("%1: %2").arg(runDirectory.fileName(), messages.join("; ")));
    }
    return summary.join("\n");
}

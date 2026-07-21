#include "jmv_map_test_launcher.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QProcessEnvironment>

#include "endurance_document.h"
#include "current_session_backup.h"
#include "hot_reload_protocol.h"
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

QString findCaseInsensitiveFile(const QStringList& directories,
                                const QString& fileName)
{
    for(const QString& directory : directories)
    {
        const QFileInfoList files = QDir(directory).entryInfoList(QDir::Files | QDir::Readable);
        for(const QFileInfo& file : files)
            if(file.fileName().compare(fileName, Qt::CaseInsensitive) == 0)
                return file.absoluteFilePath();
    }
    return QString();
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

QJsonObject processIdentity(qint64 pid, const QString& expectedImage = QString())
{
    QJsonObject result;
#ifdef Q_OS_WIN
    if(pid <= 0)
        return result;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if(!process)
        return result;
    wchar_t image[MAX_PATH * 4]{};
    DWORD imageLength = DWORD(sizeof(image) / sizeof(image[0]));
    FILETIME created{}, exited{}, kernel{}, user{};
    using QueryImageNameFn = BOOL (WINAPI*)(HANDLE, DWORD, LPWSTR, PDWORD);
    const auto queryImageName = reinterpret_cast<QueryImageNameFn>(GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW"));
    if(queryImageName && queryImageName(process, 0, image, &imageLength)
       && GetProcessTimes(process, &created, &exited, &kernel, &user))
    {
        ULARGE_INTEGER createdValue{};
        createdValue.LowPart = created.dwLowDateTime;
        createdValue.HighPart = created.dwHighDateTime;
        const QString imagePath = QDir::fromNativeSeparators(QString::fromWCharArray(image));
        result.insert("pid", double(pid));
        result.insert("imagePath", imagePath);
        result.insert("creationTime100ns", QString::number(createdValue.QuadPart));
        const QString hashPath = expectedImage.isEmpty() ? imagePath : expectedImage;
        result.insert("imageSha256", fileSha256(hashPath));
    }
    CloseHandle(process);
#else
    Q_UNUSED(pid);
    Q_UNUSED(expectedImage);
#endif
    return result;
}

bool processIdentityMatches(const QJsonObject& expected)
{
    const qint64 pid = qint64(expected.value("pid").toDouble());
    if(pid <= 0 || !processExists(pid))
        return false;
    // PID-only legacy journals are never ownership proof: a PID may have been
    // reused after a crash. They are handled conservatively by recovery until
    // the referenced process (and any game.exe) is gone.
    if(expected.value("imagePath").toString().isEmpty()
       || expected.value("creationTime100ns").toString().isEmpty())
        return false;
    const QJsonObject actual = processIdentity(pid);
    const auto clean = [](const QString& path)
    {
        return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    };
    return !actual.isEmpty()
        && clean(actual.value("imagePath").toString()).compare(
               clean(expected.value("imagePath").toString()), Qt::CaseInsensitive) == 0
        && actual.value("creationTime100ns").toString()
            == expected.value("creationTime100ns").toString()
        && (expected.value("imageSha256").toString().isEmpty()
            || actual.value("imageSha256").toString().compare(
                expected.value("imageSha256").toString(), Qt::CaseInsensitive) == 0);
}

bool threadBelongsToProcess(quint32 threadId, quint32 processId)
{
#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if(snapshot == INVALID_HANDLE_VALUE)
        return false;
    THREADENTRY32 entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    bool found = false;
    if(Thread32First(snapshot, &entry))
    {
        do
        {
            if(entry.th32ThreadID == DWORD(threadId)
               && entry.th32OwnerProcessID == DWORD(processId))
            {
                found = true;
                break;
            }
        }
        while(Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
#else
    Q_UNUSED(threadId);
    Q_UNUSED(processId);
    return false;
#endif
}

qint64 readOwnedGamePid(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    bool ok = false;
    const qint64 pid = QString::fromLatin1(file.readAll()).trimmed().toLongLong(&ok);
    return ok ? pid : 0;
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

QString saveGenerationPath(const QString& runRoot, quint64 generation)
{
    return QDir(runRoot).filePath(QString("generations/%1/saves")
        .arg(generation, 6, 10, QLatin1Char('0')));
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
    bool success = restoreCurrentSession(manifest.value("currentSessionBackup").toObject(), messages);
    success = restoreAutoexec(manifest, messages) && success;
    const QJsonObject bridge = manifest.value("bridge").toObject();
    success = removeIfOwned(bridge.value("path").toString(),
                            bridge.value("sha256").toString(), messages) && success;
    const QJsonObject live = manifest.value("liveFiles").toObject();
    success = removeIfOwned(live.value("mprPath").toString(),
                            live.value("mprSha256").toString(), messages) && success;
    success = removeIfOwned(live.value("mobPath").toString(),
                            live.value("mobSha256").toString(), messages) && success;
    const QString recordedRunRoot = manifest.value("runRoot").toString();
    const QString runRoot = recordedRunRoot.isEmpty()
        ? QString() : QFileInfo(recordedRunRoot).absoluteFilePath();
    const QString generationsPath = QDir(runRoot).filePath("generations");
    const QString cleanRoot = QDir::fromNativeSeparators(QDir::cleanPath(runRoot)) + "/";
    const QString cleanGenerations = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(generationsPath).absoluteFilePath()));
    if(!runRoot.isEmpty() && cleanGenerations.startsWith(cleanRoot, Qt::CaseInsensitive)
       && QFileInfo(generationsPath).exists())
    {
        const QFileInfo generationsInfo(generationsPath);
        if(generationsInfo.isSymLink() || !generationsInfo.isDir())
        {
            messages->append("Refused unsafe save-generation cleanup path: " + generationsPath);
            success = false;
        }
        else if(!QDir(generationsPath).removeRecursively())
        {
            messages->append("Could not remove retired save generations: " + generationsPath);
            success = false;
        }
        else
            messages->append("Removed disposable save generations: " + generationsPath);
    }
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
      m_hotReloadReadyTimer(new QTimer(this)),
      m_hotReloadResultTimer(new QTimer(this)),
      m_recoveryTimer(new QTimer(this)),
      m_cleanupComplete(true), m_baseProbe(false)
{
    m_recoveryTimer->setInterval(2000);
    connect(m_recoveryTimer, &QTimer::timeout,
            this, &CJmvMapTestLauncher::refreshInterruptedRecovery);
    QString interrupted;
    if(hasActiveInterruptedRun(&interrupted))
        m_recoveryPending = true;
    m_hotReloadReadyTimer->setInterval(500);
    connect(m_hotReloadReadyTimer, &QTimer::timeout, this, [this]()
    {
        if(isHotReloadReady())
        {
            m_hotReloadReadyTimer->stop();
            appendRunLog("JMV hot-reload transport is ready on the verified game thread");
            emit hotReloadReady();
        }
    });
    m_hotReloadResultTimer->setInterval(250);
    connect(m_hotReloadResultTimer, &QTimer::timeout, this, [this]()
    {
        if(m_pendingHotReloadSequence == 0)
        {
            m_hotReloadResultTimer->stop();
            return;
        }
        QFile result(m_bridgeResultPath);
        const QString text = result.open(QIODevice::ReadOnly | QIODevice::Text)
            ? QString::fromUtf8(result.readAll()) : QString();
        const QString completed = QString("HOT_RELOAD completed sequence=%1")
            .arg(m_pendingHotReloadSequence);
        const QString failed = QString("HOT_RELOAD failed sequence=%1")
            .arg(m_pendingHotReloadSequence);
        if(text.contains(completed) || text.contains(failed))
        {
            const quint64 sequence = m_pendingHotReloadSequence;
            const bool success = text.contains(completed);
            if(success || text.contains(QString("SAVE_GENERATION committed generation=%1")
                                         .arg(m_pendingSaveGeneration)))
            {
                m_saveGeneration = m_pendingSaveGeneration;
                m_testSavesPath = saveGenerationPath(m_runRoot, m_saveGeneration);
                QJsonObject isolated = m_manifest.value("isolatedSaves").toObject();
                isolated.insert("activeGeneration", QString::number(m_saveGeneration));
                isolated.insert("testPath", m_testSavesPath);
                QJsonArray generations = isolated.value("generations").toArray();
                for(int index = 0; index < generations.size(); ++index)
                {
                    QJsonObject generation = generations.at(index).toObject();
                    generation.insert("state",
                        generation.value("generation").toString()
                            == QString::number(m_saveGeneration) ? "active" : "retired");
                    generations[index] = generation;
                }
                isolated.insert("generations", generations);
                m_manifest.insert("isolatedSaves", isolated);
            }
            m_pendingHotReloadSequence = 0;
            m_pendingSaveGeneration = 0;
            m_hotReloadResultTimer->stop();
            const QString message = success
                ? QString("JMV hot reload %1 completed through the staged map loader.").arg(sequence)
                : QString("JMV hot reload %1 failed; inspect bridge-result.log.").arg(sequence);
            appendRunLog(message);
            writeManifest("running");
            if(success)
                QTimer::singleShot(0, this, [this]() { focusGameWindow(true); });
            emit hotReloadFinished(sequence, success, message);
            return;
        }
        if(++m_hotReloadResultPolls >= 160)
        {
            const quint64 sequence = m_pendingHotReloadSequence;
            m_pendingHotReloadSequence = 0;
            m_pendingSaveGeneration = 0;
            m_hotReloadResultTimer->stop();
            const QString message = QString(
                "JMV hot reload %1 did not report completion within 40 seconds; inspect bridge-result.log.")
                .arg(sequence);
            appendRunLog(message);
            emit hotReloadFinished(sequence, false, message);
        }
    });
    connect(m_process, &QProcess::started, this, [this]()
    {
        appendRunLog(QString("%1 started pid=%2")
                     .arg(m_usesRuntimeHost ? "Endurance runtime host" : "game.exe")
                     .arg(m_process->processId()));
        const qint64 pid = m_process->processId();
        m_manifest.insert("pid", double(pid));
        m_manifest.insert("process", processIdentity(pid));
        writeManifest("running");
        emit testStarted(m_runId, m_temporaryMapName);
        if(!m_baseProbe)
            m_hotReloadReadyTimer->start();
        QTimer::singleShot(1500, this, [this]() { focusGameWindow(false); });
        QTimer::singleShot(5000, this, [this]() { focusGameWindow(false); });
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status)
    {
        m_hotReloadReadyTimer->stop();
        m_hotReloadResultTimer->stop();
        m_pendingHotReloadSequence = 0;
        m_pendingSaveGeneration = 0;
        appendRunLog(QString("game.exe finished exitCode=%1 status=%2")
                     .arg(exitCode).arg(int(status)));
        const QString bridgeResult = bridgeResultSummary();
        appendRunLog(bridgeResult);
        const QString cleanupOnly = cleanup();
        QJsonObject report = buildAuthoritativeResult(
            m_manifest,
            bridgeResult.startsWith("Runtime bridge result:\n")
                ? bridgeResult.mid(QString("Runtime bridge result:\n").size())
                : QString(),
            exitCode, int(status), m_stopRequested, cleanupOnly);
        const QString resultPath = QDir(m_runRoot).filePath("result.json");
        QString resultError;
        if(!atomicWrite(resultPath,
                        QJsonDocument(report).toJson(QJsonDocument::Indented),
                        &resultError))
            appendRunLog("Could not persist authoritative result: " + resultError);
        m_manifest.insert("finalResultPath", resultPath);
        m_manifest.insert("finalResultPassed", report.value("passed").toBool());
        writeManifest(m_cleanupComplete ? "cleaned" : "cleanup-needed");
        appendRunLog(QString("Authoritative test result passed=%1 errors=%2 warnings=%3 path=%4")
                     .arg(report.value("passed").toBool())
                     .arg(report.value("errorCount").toInt())
                     .arg(report.value("warningCount").toInt()).arg(resultPath));
        emit testReportReady(report);
        QString cleanupMessage = cleanupOnly;
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

QString CJmvMapTestLauncher::testRunsRootPath()
{
    return testRunsRoot();
}

QString CJmvMapTestLauncher::generationSavesPath(const QString& runRoot,
                                                 quint64 generation)
{
    return saveGenerationPath(runRoot, generation);
}

QJsonObject CJmvMapTestLauncher::buildAuthoritativeResult(
    const QJsonObject& manifest, const QString& bridgeResult, int exitCode,
    int exitStatus, bool stopRequested, const QString& cleanupMessage)
{
    QJsonObject report;
    report.insert("schemaVersion", 1);
    report.insert("runId", manifest.value("runId").toString());
    report.insert("profileId", manifest.value("profileId").toString());
    report.insert("runRoot", manifest.value("runRoot").toString());
    report.insert("runLogPath", manifest.value("sharedLogPath").toString(
                      manifest.value("logPath").toString()));
    report.insert("internalRunLogPath", manifest.value("logPath").toString());
    report.insert("bridgeResultPath",
                  manifest.value("bridge").toObject().value("resultPath").toString());
    report.insert("exitCode", exitCode);
    report.insert("exitStatus", exitStatus);
    report.insert("exitReason", stopRequested ? "user-stop"
                                                : (exitStatus == int(QProcess::CrashExit)
                                                   ? "crash" : "process-exit"));
    report.insert("cleanupMessage", cleanupMessage);

    QString requestedZone = manifest.value("sessionSettings").toObject()
        .value("sourceZone").toString();
    int requestedEntrance = manifest.value("sessionSettings").toObject()
        .value("travelExit").toInt();
    quint64 requestedGeneration = manifest.value("isolatedSaves").toObject()
        .value("activeGeneration").toString().toULongLong();
    QString requestedMprHash;
    QString requestedMobHash;
    QString expectedMprPath = manifest.value("liveFiles").toObject()
        .value("mprPath").toString();
    QString expectedMobPath = manifest.value("liveFiles").toObject()
        .value("mobPath").toString();
    QString saveNamespace = manifest.value("isolatedSaves").toObject()
        .value("testPath").toString();
    const QJsonArray reloads = manifest.value("hotReloads").toArray();
    if(!reloads.isEmpty())
    {
        const QJsonObject latest = reloads.last().toObject();
        requestedZone = latest.value("zoneId").toString(requestedZone);
        requestedEntrance = latest.value("entranceId").toInt(requestedEntrance);
        requestedGeneration = latest.value("generation").toString()
            .toULongLong();
        requestedMprHash = latest.value("mprSha256").toString();
        requestedMobHash = latest.value("mobSha256").toString();
        expectedMprPath = latest.value("mprPath").toString();
        expectedMobPath = latest.value("mobPath").toString();
        saveNamespace = latest.value("savePath").toString(saveNamespace);
    }
    if(requestedMprHash.isEmpty() || requestedMobHash.isEmpty())
    {
        const QJsonObject live = manifest.value("liveFiles").toObject();
        requestedMprHash = live.value("mprSha256").toString();
        requestedMobHash = live.value("mobSha256").toString();
    }

    QString actualZone;
    int actualEntrance = 0;
    quint64 actualGeneration = 0;
    quint64 completedReloadSequence = 0;
    const QRegularExpression reloadExpression(
        "HOT_RELOAD dispatch sequence=(\\d+) generation=(\\d+).*zone=\\\"([^\\\"]+)\\\" entrance_id=(\\d+)");
    QRegularExpressionMatchIterator reloadMatches = reloadExpression.globalMatch(bridgeResult);
    while(reloadMatches.hasNext())
    {
        const QRegularExpressionMatch match = reloadMatches.next();
        const quint64 sequence = match.captured(1).toULongLong();
        if(bridgeResult.contains(QString("HOT_RELOAD completed sequence=%1").arg(sequence)))
        {
            completedReloadSequence = sequence;
            actualGeneration = match.captured(2).toULongLong();
            actualZone = match.captured(3);
            actualEntrance = match.captured(4).toInt();
        }
    }
    if(actualZone.isEmpty())
    {
        const QRegularExpression initialExpression(
            "DIRECT_LAUNCH initialized New Game player zone=\\\"([^\\\"]+)\\\" entrance_id=(\\d+)");
        QRegularExpressionMatchIterator initialMatches = initialExpression.globalMatch(bridgeResult);
        while(initialMatches.hasNext())
        {
            const QRegularExpressionMatch match = initialMatches.next();
            actualZone = match.captured(1);
            actualEntrance = match.captured(2).toInt();
        }
    }

    QString actualMprPath;
    QString actualMobPath;
    const QRegularExpression loaderExpression(
        "redirecting normal single-player transition loader mpr=\\\"([^\\\"]+)\\\" mob=\\\"([^\\\"]+)\\\"");
    QRegularExpressionMatchIterator loaderMatches = loaderExpression.globalMatch(bridgeResult);
    while(loaderMatches.hasNext())
    {
        const QRegularExpressionMatch match = loaderMatches.next();
        actualMprPath = match.captured(1);
        actualMobPath = match.captured(2);
    }

    const bool gameplayReady = bridgeResult.contains(
        QRegularExpression("RUNTIME_STATE.*to=GameplayReady"));
    const bool stagedLoaderObserved = bridgeResult.contains(
        "DIRECT_LAUNCH staged map loader returned result=1")
        || completedReloadSequence != 0;
    const bool actualMprMatchesOwnedPayload = !actualMprPath.isEmpty()
        && QFileInfo(actualMprPath).absoluteFilePath().compare(
            QFileInfo(expectedMprPath).absoluteFilePath(),
            Qt::CaseInsensitive) == 0;
    const bool actualMobMatchesOwnedPayload = !actualMobPath.isEmpty()
        && QFileInfo(actualMobPath).absoluteFilePath().compare(
            QFileInfo(expectedMobPath).absoluteFilePath(),
            Qt::CaseInsensitive) == 0;
    const bool payloadHashesValidated = stagedLoaderObserved
        && actualMprMatchesOwnedPayload && actualMobMatchesOwnedPayload
        && !requestedMprHash.isEmpty() && !requestedMobHash.isEmpty();
    const int playerInitializations = bridgeResult.count(
        "DIRECT_LAUNCH initialized New Game player");

    bool sourceFilesUnchanged = true;
    QJsonArray changedSources;
    const QJsonObject sources = manifest.value("sourceSha256").toObject();
    for(auto it = sources.begin(); it != sources.end(); ++it)
    {
        if(fileSha256(it.key()).compare(it.value().toString(), Qt::CaseInsensitive) != 0)
        {
            sourceFilesUnchanged = false;
            changedSources.append(it.key());
        }
    }

    bool cleanupComplete = true;
    QJsonArray retainedArtifacts;
    const auto checkRemoved = [&cleanupComplete, &retainedArtifacts](const QString& path)
    {
        if(!path.isEmpty() && QFileInfo::exists(path))
        {
            cleanupComplete = false;
            retainedArtifacts.append(path);
        }
    };
    const QJsonObject live = manifest.value("liveFiles").toObject();
    checkRemoved(live.value("mprPath").toString());
    checkRemoved(live.value("mobPath").toString());
    checkRemoved(manifest.value("bridge").toObject().value("path").toString());
    checkRemoved(QDir(manifest.value("runRoot").toString()).filePath("generations"));
    if(cleanupMessage.contains("Could not", Qt::CaseInsensitive)
       || cleanupMessage.contains("Refused", Qt::CaseInsensitive)
       || cleanupMessage.contains("Retained changed", Qt::CaseInsensitive))
        cleanupComplete = false;

    QJsonArray errors;
    QJsonArray warnings;
    const auto addUnique = [](QJsonArray* array, const QString& text)
    {
        if(!array || text.isEmpty()) return;
        for(const QJsonValue& existing : *array)
            if(existing.toString() == text) return;
        array->append(text);
    };
    if(bridgeResult.trimmed().isEmpty())
        addUnique(&errors, "The runtime bridge produced no result evidence.");
    if(!gameplayReady || !stagedLoaderObserved)
        addUnique(&errors, "The runtime did not prove a completed staged gameplay load.");
    if(stagedLoaderObserved && !payloadHashesValidated)
        addUnique(&errors, "The runtime loader paths could not be matched to the hash-verified staged payloads.");
    if(actualZone.isEmpty())
        addUnique(&errors, "The runtime did not report the actually initialized zone.");
    else if(!requestedZone.isEmpty()
            && actualZone.compare(requestedZone, Qt::CaseInsensitive) != 0)
        addUnique(&errors, QString("Requested zone %1 but runtime reported %2.")
                  .arg(requestedZone, actualZone));
    if(requestedEntrance > 0 && actualEntrance != requestedEntrance)
        addUnique(&errors, QString("Requested entrance %1 but runtime reported %2.")
                  .arg(requestedEntrance).arg(actualEntrance));
    if(requestedGeneration > 0 && actualGeneration > 0
       && requestedGeneration != actualGeneration)
        addUnique(&errors, QString("Requested save generation %1 but runtime committed %2.")
                  .arg(requestedGeneration).arg(actualGeneration));
    if(!sourceFilesUnchanged)
        addUnique(&errors, "One or more source map files changed during the test.");
    if(!cleanupComplete)
        addUnique(&errors, "One or more owned runtime artifacts could not be cleaned up safely.");
    if(exitStatus == int(QProcess::CrashExit) && !stopRequested)
        addUnique(&errors, "The owned game process ended with a crash status.");
    else if(exitCode != 0 && !stopRequested)
        addUnique(&warnings, QString("The game returned non-zero exit code %1.").arg(exitCode));

    const QStringList bridgeLines = bridgeResult.split('\n', QString::SkipEmptyParts);
    for(const QString& line : bridgeLines)
    {
        if(line.contains(" failed", Qt::CaseInsensitive)
           || line.contains(" refused", Qt::CaseInsensitive)
           || line.contains(" disabled", Qt::CaseInsensitive))
        {
            const int marker = line.indexOf(QRegularExpression(
                "(DIRECT_LAUNCH|HOT_RELOAD|BASE_PROBE)"));
            addUnique(&errors, marker >= 0 ? line.mid(marker).trimmed() : line.trimmed());
        }
    }

    report.insert("requestedZone", requestedZone);
    report.insert("actualZone", actualZone);
    report.insert("requestedEntrance", requestedEntrance);
    report.insert("actualEntrance", actualEntrance);
    report.insert("requestedGeneration", QString::number(requestedGeneration));
    report.insert("actualGeneration", QString::number(actualGeneration));
    report.insert("saveNamespace", saveNamespace);
    report.insert("completedReloadSequence", QString::number(completedReloadSequence));
    report.insert("mprSha256", requestedMprHash);
    report.insert("mobSha256", requestedMobHash);
    report.insert("actualMprSha256", actualMprMatchesOwnedPayload ? requestedMprHash : QString());
    report.insert("actualMobSha256", actualMobMatchesOwnedPayload ? requestedMobHash : QString());
    report.insert("actualMprPath", actualMprPath);
    report.insert("actualMobPath", actualMobPath);
    report.insert("gameplayReady", gameplayReady);
    report.insert("stagedLoaderObserved", stagedLoaderObserved);
    report.insert("payloadHashesValidated", payloadHashesValidated);
    report.insert("playerInitializationCount", playerInitializations);
    report.insert("reportedPlayerCount", playerInitializations > 0 ? 1 : -1);
    report.insert("playerCountEvidence", playerInitializations > 0
                  ? "New Game player-record initialization observed"
                  : "not observed");
    report.insert("sourceFilesUnchanged", sourceFilesUnchanged);
    report.insert("changedSources", changedSources);
    report.insert("cleanupComplete", cleanupComplete);
    report.insert("retainedArtifacts", retainedArtifacts);
    report.insert("errors", errors);
    report.insert("warnings", warnings);
    report.insert("errorCount", errors.size());
    report.insert("warningCount", warnings.size());
    report.insert("passed", errors.isEmpty());
    report.insert("completedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return report;
}

bool CJmvMapTestLauncher::validateAutomatedResult(const QJsonObject& report,
                                                  QStringList* findings)
{
    QStringList local;
    if(report.value("schemaVersion").toInt() != 1)
        local.append("Unsupported or missing test-result schema version.");
    if(report.value("runId").toString().isEmpty())
        local.append("The test result has no run ID.");
    if(!report.value("gameplayReady").toBool())
        local.append("Gameplay readiness was not proven.");
    if(!report.value("stagedLoaderObserved").toBool())
        local.append("The staged loader was not observed.");
    if(!report.value("payloadHashesValidated").toBool())
        local.append("The loaded payload hashes were not validated.");
    if(report.value("actualZone").toString().isEmpty())
        local.append("The actually loaded zone is missing.");
    if(!report.value("sourceFilesUnchanged").toBool())
        local.append("Source-file preservation failed.");
    if(!report.value("cleanupComplete").toBool())
        local.append("Runtime cleanup is incomplete.");
    for(const QJsonValue& error : report.value("errors").toArray())
        local.append(error.toString());
    local.removeDuplicates();
    if(findings) *findings = local;
    return local.isEmpty();
}

bool CJmvMapTestLauncher::analyzeCompletedRun(const QString& runRoot,
                                              QJsonObject* report,
                                              QStringList* findings)
{
    if(!report)
        return false;
    QFile resultFile(QDir(runRoot).filePath("result.json"));
    if(!resultFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if(findings) findings->append("The completed run has no readable result.json.");
        return false;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(resultFile.readAll(), &error);
    if(error.error != QJsonParseError::NoError || !document.isObject())
    {
        if(findings) findings->append("The completed run result is not valid JSON.");
        return false;
    }
    *report = document.object();
    return validateAutomatedResult(*report, findings);
}

bool CJmvMapTestLauncher::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

bool CJmvMapTestLauncher::isHotReloadReady() const
{
    if(!isRunning() || m_baseProbe || m_hotReloadThreadPath.isEmpty())
        return false;
    QFile file(m_hotReloadThreadPath);
    bool ok = false;
    quint32 threadId = 0;
    if(file.open(QIODevice::ReadOnly | QIODevice::Text))
        threadId = file.readAll().trimmed().toULong(&ok);
    const qint64 gamePid = m_usesRuntimeHost ? readOwnedGamePid(m_gamePidPath)
                                             : m_process->processId();
    return ok && threadId != 0 && gamePid > 0
        && threadBelongsToProcess(threadId, quint32(gamePid));
}

QString CJmvMapTestLauncher::runtimeState() const
{
    QFile result(m_bridgeResultPath);
    if(!result.open(QIODevice::ReadOnly | QIODevice::Text))
        return "Initializing";
    const QStringList lines = QString::fromUtf8(result.readAll()).split('\n');
    const QRegularExpression expression("RUNTIME_STATE\\s+sequence=\\d+\\s+from=\\S+\\s+to=(\\S+)");
    for(auto it = lines.crbegin(); it != lines.crend(); ++it)
    {
        const QRegularExpressionMatch match = expression.match(*it);
        if(match.hasMatch())
            return match.captured(1);
    }
    return isHotReloadReady() ? "GameplayReady" : "Initializing";
}

quint64 CJmvMapTestLauncher::latestRuntimeStateSequence() const
{
    QFile result(m_bridgeResultPath);
    if(!result.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    const QString text = QString::fromUtf8(result.readAll());
    const QRegularExpression expression("RUNTIME_STATE\\s+sequence=(\\d+)");
    QRegularExpressionMatchIterator matches = expression.globalMatch(text);
    quint64 sequence = 0;
    while(matches.hasNext())
        sequence = matches.next().captured(1).toULongLong();
    return sequence;
}

bool CJmvMapTestLauncher::injectWorldScriptBootstrap(const QString& script,
                                                     const QString& commands,
                                                     QString* output,
                                                     QString* error,
                                                     const QString& immediateCommands)
{
    if(!output || (commands.trimmed().isEmpty() && immediateCommands.trimmed().isEmpty()))
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
        // JMV's autorun MOB deliberately begins WorldScript with a short
        // Sleep while the player/party record is registered. Running
        // SetCPFast or GodMode before that point is a silent no-op. Preserve
        // the source delay and insert immediately after it when present.
        const int bodyStart = open + 1;
        int insertion = bodyStart;
        const QString body = script.mid(insertion);
        const QRegularExpression leadingDelay(
            "^\\s*Sleep\\s*\\([^\\)]*\\)",
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch delay = leadingDelay.match(body);
        if(delay.hasMatch())
            insertion += delay.capturedEnd();
        *output = script.left(bodyStart);
        if(!immediateCommands.trimmed().isEmpty())
            output->append("\n" + immediateCommands.trimmed() + "\n");
        output->append(script.mid(bodyStart, insertion - bodyStart));
        if(!commands.trimmed().isEmpty())
            output->append("\n" + commands.trimmed() + "\n");
        output->append(script.mid(insertion));
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
    QStringList generatedBody;
    if(!immediateCommands.trimmed().isEmpty())
        generatedBody.append(immediateCommands.trimmed());
    if(!commands.trimmed().isEmpty())
        generatedBody.append(commands.trimmed());
    output->append("WorldScript(\n" + generatedBody.join("\n") + "\n)\n");
    return true;
}

bool CJmvMapTestLauncher::prepareTemporaryAutoexec(const QString& engineDirectory,
                                                   const QString& runRoot,
                                                   bool godmode,
                                                   QJsonObject* manifest,
                                                   QString* error)
{
    if(!godmode)
        return true;
    if(!manifest || engineDirectory.isEmpty() || runRoot.isEmpty())
    {
        if(error) *error = "Temporary Godmode autoexec paths are incomplete.";
        return false;
    }
    const QString path = QDir(engineDirectory).filePath("autoexec.cfg");
    const QString backupPath = QDir(runRoot).filePath("autoexec.original.cfg");
    const bool existed = QFileInfo::exists(path);
    QByteArray original;
    if(existed && !readFile(path, &original, error))
        return false;
    if(existed)
    {
        if(!atomicWrite(backupPath, original, error)
           || fileSha256(backupPath).compare(sha256(original), Qt::CaseInsensitive) != 0)
        {
            if(error && error->isEmpty()) *error = "The temporary autoexec backup failed verification.";
            return false;
        }
    }
    QByteArray patched = original;
    if(!patched.isEmpty() && !patched.endsWith('\n'))
        patched.append("\r\n");
    patched.append("// EI Mapper Endurance owned test command\r\n@godmode(0,3)\r\n");
    QJsonObject autoexec;
    autoexec.insert("path", path);
    autoexec.insert("backupPath", existed ? backupPath : QString());
    autoexec.insert("originalExisted", existed);
    autoexec.insert("originalSha256", sha256(original));
    autoexec.insert("patchedSha256", sha256(patched));
    autoexec.insert("command", "@godmode(0,3)");
    // Record ownership before touching the live file so every failure path can
    // invoke the same hash-checked restoration routine.
    manifest->insert("autoexec", autoexec);
    if(!atomicWrite(path, patched, error)
       || fileSha256(path).compare(sha256(patched), Qt::CaseInsensitive) != 0)
    {
        if(error && error->isEmpty()) *error = "The temporary Godmode autoexec failed verification.";
        QStringList rollback;
        restoreAutoexec(*manifest, &rollback);
        return false;
    }
    return true;
}

bool CJmvMapTestLauncher::prepareGodmodeBootstrap(
    const QStringList& mapDirectories, const QString& runRoot,
    const SMapSpawnPlan& spawn, double spawnZ, bool godmode,
    const QStringList& supplementalMobNames,
    QJsonObject* manifest, QString* outputPath, QString* error)
{
    if(outputPath)
        outputPath->clear();
    if((!spawn.valid || !spawn.custom) && !godmode && supplementalMobNames.isEmpty())
        return true;
    if(!manifest || !outputPath || runRoot.isEmpty())
    {
        if(error) *error = "JMV runtime bootstrap paths are incomplete.";
        return false;
    }
    const QString sourcePath = findCaseInsensitiveFile(
        mapDirectories, "AutoRunSpellAddon123.mob");
    if(sourcePath.isEmpty())
    {
        if(error) *error = "JMV runtime positioning requires the profile's AutoRunSpellAddon123.mob.";
        return false;
    }
    CMobDocument document;
    CValidationReport report;
    if(!document.load(QFileInfo(sourcePath), &report))
    {
        if(error) *error = "Could not validate the JMV autorun MOB:\n" + report.toText();
        return false;
    }
    QString script;
    if(!document.embeddedScript(&script, &report))
    {
        if(error) *error = "Could not read the JMV autorun script:\n" + report.toText();
        return false;
    }
    QStringList commands;
    commands.append("// EI Mapper Endurance owned runtime bootstrap");
    // SetCPFast's final value is a height offset, not an absolute terrain Z.
    // Normal entrance starts already use the map table and must not be moved a
    // second time. Custom starts are grounded by requesting a zero offset.
    QString immediatePositionCommand;
    if(spawn.valid && spawn.custom)
    {
        immediatePositionCommand = QString("SetCPFast(GetLeader(), %1, %2, 0)")
            .arg(spawn.position.x(), 0, 'f', 6)
            .arg(spawn.position.y(), 0, 'f', 6);
        // Try before the source autorun delay so a ready player starts at the
        // requested point before the first rendered frame. Repeat after the
        // delay as the guarded fallback for runtimes that register the leader
        // asynchronously.
        commands.append(immediatePositionCommand);
    }
    for(const QString& name : supplementalMobNames)
    {
        const QFileInfo file(name);
        if(file.fileName() != name || !name.endsWith(".mob", Qt::CaseInsensitive)
           || name.contains('"') || name.contains('\\') || name.contains('/'))
        {
            if(error) *error = "A supplemental MOB bootstrap name is unsafe.";
            return false;
        }
        commands.append(QString("AddMob(\"%1\")").arg(name));
    }
    if(godmode)
        commands.append("GodMode(0,3)");
    QString bootstrappedScript;
    QString bootstrapError;
    if(!injectWorldScriptBootstrap(script, commands.join("\r\n"),
                                   &bootstrappedScript, &bootstrapError,
                                   immediatePositionCommand))
    {
        if(error) *error = "Could not inject the JMV runtime commands into WorldScript: "
            + bootstrapError;
        return false;
    }
    QByteArray patched;
    if(!document.replaceEmbeddedScript(bootstrappedScript, &patched, &report))
    {
        if(error) *error = "Could not create the lossless JMV runtime autorun MOB:\n" + report.toText();
        return false;
    }
    const QString ownedPath = QDir(runRoot).filePath("godmode/AutoRunSpellAddon123.mob");
    if(!QDir().mkpath(QFileInfo(ownedPath).absolutePath())
       || !atomicWrite(ownedPath, patched, error))
        return false;
    QJsonObject bootstrap;
    bootstrap.insert("sourcePath", sourcePath);
    bootstrap.insert("sourceSha256", fileSha256(sourcePath));
    bootstrap.insert("path", ownedPath);
    bootstrap.insert("sha256", sha256(patched));
    bootstrap.insert("commands", commands.join(" "));
    bootstrap.insert("spawnX", spawn.position.x());
    bootstrap.insert("spawnY", spawn.position.y());
    bootstrap.insert("spawnZ", spawnZ);
    bootstrap.insert("godmode", godmode);
    bootstrap.insert("runtimeSpawnHeightOffset", 0.0);
    QJsonArray explicitMobs;
    for(const QString& name : supplementalMobNames)
        explicitMobs.append(name);
    bootstrap.insert("explicitSupplementalMobs", explicitMobs);
    manifest->insert("godmodeBootstrap", bootstrap);
    *outputPath = ownedPath;
    return true;
}

bool CJmvMapTestLauncher::launch(const SJmvMapTestLaunchInput& input, QString* error)
{
    if(isRunning())
    {
        if(error) *error = "An Endurance test game is already running.";
        return false;
    }
    QString interrupted;
    if(hasActiveInterruptedRun(&interrupted))
    {
        if(error) *error = "An interrupted Endurance test game is still running. "
            "Exit that game normally; Endurance will clean it automatically before another test.\n\n"
            + interrupted;
        return false;
    }
    if(anotherGameIsRunning())
    {
        if(error) *error = "A game.exe process is already running. Close it before starting an Endurance test.";
        return false;
    }
    m_runId.clear();
    m_temporaryMapName.clear();
    m_runRoot.clear();
    m_manifestPath.clear();
    m_runLogPath.clear();
    m_sharedRunLogPath.clear();
    m_liveMprPath.clear();
    m_liveMobPath.clear();
    m_bridgeResultPath.clear();
    m_testSavesPath.clear();
    m_bridgeSha256.clear();
    m_liveMprSha256.clear();
    m_liveMobSha256.clear();
    m_runtimeHostPath.clear();
    m_gamePidPath.clear();
    m_liveSavesPath.clear();
    m_hotReloadRequestPath.clear();
    m_hotReloadThreadPath.clear();
    m_profileId.clear();
    m_currentZoneId.clear();
    m_hotReloadSequence = 0;
    m_saveGeneration = 0;
    m_pendingHotReloadSequence = 0;
    m_pendingSaveGeneration = 0;
    m_hotReloadResultPolls = 0;
    m_baseProbe = false;
    m_usesRuntimeHost = false;
    m_manifest = QJsonObject();
    m_cleanupComplete = false;
    m_stopRequested = false;
    if(!stage(input, error))
    {
        if(error && !error->isEmpty())
            appendRunLog("Preflight/staging failed: " + *error);
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
    appendRunLog(QString("Launching %1 workingDirectory=%2 map=%3 transport=%4")
                 .arg(m_gameExePath, m_engineDirectory, m_temporaryMapName,
                      m_usesRuntimeHost ? "owned-x86-injector" : "direct-process"));
    m_process->setProgram(m_usesRuntimeHost ? m_runtimeHostPath : m_gameExePath);
    if(m_usesRuntimeHost)
        m_process->setArguments(QStringList() << m_gameExePath << m_bridgeLivePath
            << m_engineDirectory << m_gamePidPath
            << QDir(m_runRoot).filePath("runtime-host.log"));
    else
        m_process->setArguments(QStringList());
    m_process->setWorkingDirectory(m_engineDirectory);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("EI_MAPPER_ENDURANCE_GAME_ROOT",
                       QDir::cleanPath(input.profile.gameRoot));
    if(!input.profile.modRoot.isEmpty())
        environment.insert("EI_MAPPER_ENDURANCE_MOD_ROOT",
                           QDir::cleanPath(input.profile.modRoot));
    environment.insert("EI_MAPPER_ENDURANCE_MAP", m_temporaryMapName);
    environment.insert("EI_MAPPER_ENDURANCE_SOURCE_ZONE", input.mapEntry.zoneId);
    environment.insert("EI_MAPPER_ENDURANCE_TARGET_EXIT", QString::number(input.spawn.exitId));
    environment.insert("EI_MAPPER_ENDURANCE_SPAWN_MODE", input.spawn.custom ? "custom" : "entrance");
    environment.insert("EI_MAPPER_ENDURANCE_SPAWN_X", QString::number(input.spawn.position.x(), 'f', 6));
    environment.insert("EI_MAPPER_ENDURANCE_SPAWN_Y", QString::number(input.spawn.position.y(), 'f', 6));
    environment.insert("EI_MAPPER_ENDURANCE_SPAWN_Z", QString::number(input.spawnZ, 'f', 6));
    environment.insert("EI_MAPPER_ENDURANCE_TEMP_MPR", m_liveMprPath);
    environment.insert("EI_MAPPER_ENDURANCE_TEMP_MOB", m_liveMobPath);
    environment.insert("EI_MAPPER_ENDURANCE_BRIDGE_RESULT", m_bridgeResultPath);
    environment.insert("EI_MAPPER_ENDURANCE_LIVE_SAVES", m_liveSavesPath);
    environment.insert("EI_MAPPER_ENDURANCE_TEST_SAVES", m_testSavesPath);
    environment.insert("EI_MAPPER_ENDURANCE_SKIP_INTROS", "1");
    environment.insert("EI_MAPPER_ENDURANCE_HOT_RELOAD_REQUEST", m_hotReloadRequestPath);
    environment.insert("EI_MAPPER_ENDURANCE_HOT_RELOAD_THREAD", m_hotReloadThreadPath);
    if(!m_supplementalMobDirectory.isEmpty())
        environment.insert("EI_MAPPER_ENDURANCE_SUPPLEMENTAL_MOB_DIR",
                           m_supplementalMobDirectory);
    const QString godmodeBootstrapPath = m_manifest.value("godmodeBootstrapPath").toString();
    if(!godmodeBootstrapPath.isEmpty())
        environment.insert("EI_MAPPER_ENDURANCE_GODMODE_MOB", godmodeBootstrapPath);
    if(m_baseProbe)
        environment.insert("EI_MAPPER_ENDURANCE_BASE_PROBE_LEVEL",
                           QString::number(input.baseProbeLevel));
    m_process->setProcessEnvironment(environment);
    m_process->start();
    return true;
}

bool CJmvMapTestLauncher::stage(const SJmvMapTestLaunchInput& input, QString* error)
{
    const bool isJmv = input.profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) == 0;
    const bool isBase = input.profile.profileId.compare("base-game", Qt::CaseInsensitive) == 0;
    m_baseProbe = isBase;
    m_usesRuntimeHost = isJmv || isBase;
    if((!isJmv && !isBase) || !input.profile.valid || !input.profile.runtimeStackValidated)
    {
        if(error) *error = "The active runtime stack is incomplete or does not match an exact manifest.";
        return false;
    }
    if(!input.spawn.valid || input.mprData.isEmpty() || input.mobData.isEmpty())
    {
        if(error) *error = "The loaded map pair or its unobstructed travel-zone spawn is incomplete.";
        return false;
    }
    if(isBase && (input.baseProbeLevel < 1 || input.baseProbeLevel > 4))
    {
        if(error) *error = "Select Base feasibility probe level 1 through 4.";
        return false;
    }
    if(input.profile.mapDirectories.isEmpty() || (isJmv && input.profile.modRoot.isEmpty()))
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
    const QString profileMapsDirectory = isJmv
        ? QDir(input.profile.modRoot).filePath("Maps")
        : input.profile.mapDirectories.first();
    if(isJmv && normalized(input.profile.mapDirectories.first()).compare(
           normalized(profileMapsDirectory), Qt::CaseInsensitive) != 0)
    {
        if(error) *error = "The highest-priority Maps directory is not the active JMV mod Maps directory.";
        return false;
    }
    m_gameExePath = isJmv
        ? QDir(input.profile.gameRoot).filePath("Starter/Engine/game.exe")
        : QDir(input.profile.gameRoot).filePath("game.exe");
    m_engineDirectory = QFileInfo(m_gameExePath).absolutePath();
    m_bridgeSourcePath = QDir(QCoreApplication::applicationDirPath())
            .filePath("runtime/EIMapperEnduranceJmvBridge.plg");
    if(!QFileInfo(m_gameExePath).isFile())
    {
        if(error) *error = "The validated profile game.exe is missing.";
        return false;
    }
    if(!QFileInfo(m_bridgeSourcePath).isFile())
    {
        if(error) *error = "The packaged Endurance launch bridge is missing. Reinstall this Endurance build.";
        return false;
    }
    m_runId = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-") +
            QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, '0');
    m_temporaryMapName = "ed" + m_runId.right(8).toLower();
    m_runRoot = QDir(testRunsRoot()).filePath(m_runId);
    m_manifestPath = QDir(m_runRoot).filePath("manifest.json");
    m_runLogPath = QDir(m_runRoot).filePath("runtime.log");
    const QString sharedLogsDirectory = QDir::current().filePath("logs");
    if(!QDir().mkpath(sharedLogsDirectory))
    {
        if(error) *error = "Could not create the editor logs directory for the test runtime log.";
        return false;
    }
    m_sharedRunLogPath = QDir(sharedLogsDirectory).filePath(
        QString("test-runtime-%1.log").arg(m_runId));
    m_bridgeResultPath = QDir(m_runRoot).filePath("bridge-result.log");
    m_testSavesPath = saveGenerationPath(m_runRoot, 0);
    m_liveSavesPath = isJmv ? QDir(input.profile.modRoot).filePath("saves")
                            : QDir(input.profile.gameRoot).filePath("saves");
    m_hotReloadRequestPath = QDir(m_runRoot).filePath("hot-reload.request");
    m_hotReloadThreadPath = QDir(m_runRoot).filePath("game-thread.id");
    m_supplementalMobDirectory = QDir(m_runRoot).filePath("supplemental-mobs");
    m_profileId = input.profile.profileId;
    m_currentZoneId = input.mapEntry.zoneId;
    m_bridgeLivePath = QDir(m_runRoot).filePath("EIMapperEnduranceRuntimeBridge.dll");
    m_runtimeHostPath = QDir(QCoreApplication::applicationDirPath())
        .filePath("runtime/EIMapperEnduranceRuntimeHost.exe");
    m_gamePidPath = QDir(m_runRoot).filePath("game.pid");
    if(!QFileInfo(m_runtimeHostPath).isFile())
    {
        if(error) *error = "The packaged x86 Endurance runtime host is missing.";
        return false;
    }
    if(isBase)
    {
        const QString liveDirectory = QDir(m_runRoot).filePath("live");
        if(!QDir().mkpath(liveDirectory))
        {
            if(error) *error = "Could not create the Base EI run-local map directory.";
            return false;
        }
        m_liveMprPath = QDir(liveDirectory).filePath(m_temporaryMapName + ".mpr");
        m_liveMobPath = QDir(liveDirectory).filePath(m_temporaryMapName + ".mob");
    }
    else
    {
        m_liveMprPath = QDir(profileMapsDirectory).filePath(m_temporaryMapName + ".mpr");
        m_liveMobPath = QDir(profileMapsDirectory).filePath(m_temporaryMapName + ".mob");
    }
    if(QFileInfo::exists(m_liveMprPath) || QFileInfo::exists(m_liveMobPath))
    {
        if(error) *error = "The randomly generated Endurance test-map name already exists; retry the launch.";
        return false;
    }
    if(!QDir().mkpath(m_runRoot))
    {
        if(error) *error = "Could not create the Endurance test-run directory.";
        return false;
    }

    // Journal every live artifact before creating it. A crash at any later
    // staging point can therefore be recovered without guessing ownership.
    m_liveMprSha256 = sha256(input.mprData);
    m_liveMobSha256 = sha256(input.mobData);
    QJsonObject liveJournal;
    liveJournal.insert("mprPath", m_liveMprPath);
    liveJournal.insert("mprSha256", m_liveMprSha256);
    liveJournal.insert("mobPath", m_liveMobPath);
    liveJournal.insert("mobSha256", m_liveMobSha256);
    QJsonObject saveJournal;
    saveJournal.insert("mode", "generation-scoped-runtime-path-redirection");
    saveJournal.insert("livePath", m_liveSavesPath);
    saveJournal.insert("activeGeneration", QString::number(0));
    saveJournal.insert("testPath", m_testSavesPath);
    QJsonArray generations;
    QJsonObject initialGeneration;
    initialGeneration.insert("generation", QString::number(0));
    initialGeneration.insert("savePath", m_testSavesPath);
    initialGeneration.insert("state", "planned");
    generations.append(initialGeneration);
    saveJournal.insert("generations", generations);
    m_manifest.insert("journalVersion", 1);
    m_manifest.insert("runId", m_runId);
    m_manifest.insert("runRoot", m_runRoot);
    m_manifest.insert("sharedLogPath", m_sharedRunLogPath);
    m_manifest.insert("createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_manifest.insert("temporaryMapName", m_temporaryMapName);
    m_manifest.insert("gameExePath", m_gameExePath);
    m_manifest.insert("gameExeSha256", fileSha256(m_gameExePath));
    m_manifest.insert("liveFiles", liveJournal);
    m_manifest.insert("isolatedSaves", saveJournal);
    if(!writeManifest("staging", error))
        return false;
    if(!QDir().mkpath(m_testSavesPath))
    {
        if(error) *error = "Could not create the initial generation save directory.";
        return false;
    }
    initialGeneration.insert("state", "active");
    generations[0] = initialGeneration;
    saveJournal.insert("generations", generations);
    m_manifest.insert("isolatedSaves", saveJournal);
    if(!writeManifest("generation-ready", error))
        return false;

    const QString x = QString::number(input.spawn.position.x(), 'f', 3);
    const QString y = QString::number(input.spawn.position.y(), 'f', 3);
    CMobDocument sourceMob;
    CValidationReport report;
    if(!sourceMob.loadBytes(input.mobData, "test-run-source.mob", &report))
    {
        if(error) *error = "The in-memory MOB candidate failed validation:\n" + report.toText();
        return false;
    }
    const QByteArray stagedMob = input.mobData;
    if(isBase && input.baseProbeLevel == 3)
    {
        QByteArray sourceMpr;
        QByteArray sourceMobBytes;
        if(!readFile(input.sourceMpr.absoluteFilePath(), &sourceMpr, error)
           || !readFile(input.sourceMob.absoluteFilePath(), &sourceMobBytes, error))
            return false;
        if(sourceMpr != input.mprData || sourceMobBytes != input.mobData)
        {
            if(error) *error = "Base probe level 3 requires byte-identical, unedited source MPR/MOB data.";
            return false;
        }
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

    QStringList stagedSupplementalPaths;
    QSet<QString> supplementalNames;
    if(!input.supplementalMobs.isEmpty() && !isJmv)
    {
        if(error) *error = "Supplemental MOB test staging is available only for JMV.";
        return false;
    }
    if(!input.supplementalMobs.isEmpty() && !QDir().mkpath(m_supplementalMobDirectory))
    {
        if(error) *error = "Could not create the owned supplemental-MOB directory.";
        return false;
    }
    for(const SJmvSupplementalMobInput& supplemental : input.supplementalMobs)
    {
        const QString name = supplemental.sourceMob.fileName();
        const QString folded = name.toCaseFolded();
        if(name.isEmpty() || !name.endsWith(".mob", Qt::CaseInsensitive)
           || name.compare(input.sourceMob.fileName(), Qt::CaseInsensitive) == 0
           || name.compare("AutoRunSpellAddon123.mob", Qt::CaseInsensitive) == 0
           || supplementalNames.contains(folded))
        {
            if(error) *error = "Supplemental MOB names must be unique, ordinary .mob filenames.";
            return false;
        }
        supplementalNames.insert(folded);
        const QString stagedPath = QDir(m_supplementalMobDirectory).filePath(name);
        if(!atomicWrite(stagedPath, supplemental.mobData, error))
            return false;
        CMobDocument supplementalDocument;
        CValidationReport supplementalReport;
        if(!supplementalDocument.load(QFileInfo(stagedPath), &supplementalReport))
        {
            if(error) *error = "A staged supplemental MOB failed validation:\n"
                + supplementalReport.toText();
            return false;
        }
        stagedSupplementalPaths.append(stagedPath);
    }

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
    request.orderedStagedMobPaths << stagedSupplementalPaths;
    request.activeMobPath = stagedMobPath;
    request.sourceSha256.insert(input.sourceMpr.absoluteFilePath(),
                                fileSha256(input.sourceMpr.absoluteFilePath()));
    request.sourceSha256.insert(input.sourceMob.absoluteFilePath(),
                                fileSha256(input.sourceMob.absoluteFilePath()));
    for(int index = 0; index < input.supplementalMobs.size(); ++index)
    {
        const SJmvSupplementalMobInput& supplemental = input.supplementalMobs.at(index);
        const QString stagedPath = stagedSupplementalPaths.at(index);
        request.sourceSha256.insert(supplemental.sourceMob.absoluteFilePath(),
                                    fileSha256(supplemental.sourceMob.absoluteFilePath()));
        request.stagedSha256.insert(QFileInfo(stagedPath).fileName(),
                                    sha256(supplemental.mobData));
    }
    request.stagedSha256.insert(QFileInfo(stagedMprPath).fileName(), m_liveMprSha256);
    request.stagedSha256.insert(QFileInfo(stagedMobPath).fileName(), m_liveMobSha256);
    request.sessionSettings.insert("travelExit", input.spawn.exitId);
    request.sessionSettings.insert("sourceZone", input.mapEntry.zoneId);
    request.sessionSettings.insert("zoneType",
                                   input.mapEntry.zoneId.startsWith("bz", Qt::CaseInsensitive)
                                   ? "briefing" : "gameplay");
    request.sessionSettings.insert("spawnX", input.spawn.position.x());
    request.sessionSettings.insert("spawnY", input.spawn.position.y());
    request.sessionSettings.insert("spawnZ", input.spawnZ);
    request.sessionSettings.insert("spawnMode", input.spawn.custom ? "custom" : "entrance");
    request.sessionSettings.insert("requestedSpawnX", input.spawn.requestedPosition.x());
    request.sessionSettings.insert("requestedSpawnY", input.spawn.requestedPosition.y());
    request.sessionSettings.insert("spawnAdjusted", input.spawn.adjusted);
    request.sessionSettings.insert("godmode", input.godmode);
    request.sessionSettings.insert("borrowedMapIdentity", input.borrowedMapIdentity);
    request.sessionSettings.insert("baseProbeLevel", input.baseProbeLevel);
    request.sessionSettings.insert("clearance", input.spawn.clearance);
    request.sessionSettings.insert("unobstructed", input.spawn.unobstructed);
    request.logPath = m_runLogPath;
    request.resultPath = QDir(m_runRoot).filePath("result.json");
    QStringList requestErrors;
    if(!validateMapTestLaunchRequest(request, m_runRoot, &requestErrors))
    {
        if(error) *error = requestErrors.join("\n");
        return false;
    }

    const QJsonObject requestManifest = mapTestLaunchRequestToJson(request);
    for(auto it = requestManifest.begin(); it != requestManifest.end(); ++it)
        m_manifest.insert(it.key(), it.value());
    m_manifest.insert("liveFiles", liveFiles);
    if(!stagedSupplementalPaths.isEmpty())
    {
        QJsonArray supplemental;
        for(const QString& path : stagedSupplementalPaths)
            supplemental.append(path);
        m_manifest.insert("supplementalMobPaths", supplemental);
    }
    QString godmodeBootstrapPath;
    QStringList supplementalMobNames;
    for(const QString& path : stagedSupplementalPaths)
        supplementalMobNames.append(QFileInfo(path).fileName());
    if(isJmv && !prepareGodmodeBootstrap(input.profile.mapDirectories, m_runRoot,
                                         input.spawn, input.spawnZ, input.godmode,
                                         supplementalMobNames, &m_manifest,
                                         &godmodeBootstrapPath, error))
        return false;
    if(!godmodeBootstrapPath.isEmpty())
        m_manifest.insert("godmodeBootstrapPath", godmodeBootstrapPath);
    appendRunLog(QString("Prepared generation-scoped isolated saves generation=0 path=%1; live saves remain untouched")
                 .arg(m_testSavesPath));
    appendRunLog(QString("Staged temporary map=%1 exit=%2 spawn=(%3,%4) clearance=%5")
                 .arg(m_temporaryMapName).arg(input.spawn.exitId)
                 .arg(x, y).arg(input.spawn.clearance, 0, 'f', 3));
    return writeManifest("staged", error);
}

bool CJmvMapTestLauncher::hotReload(const SJmvMapTestLaunchInput& input,
                                    QString* error)
{
    if(!isRunning() || m_baseProbe)
    {
        if(error) *error = "Hot reload requires an active Endurance-owned JMV test process.";
        return false;
    }
    if(!isHotReloadReady())
    {
        if(error) *error = "The initial staged map has not reported hot-reload readiness yet.";
        return false;
    }
    if(isHotReloadPending())
    {
        const QString state = runtimeState();
        if(state != "Transitioning" && state != "Initializing" && state != "Unknown")
        {
            if(error) *error = "Wait for the current hot reload to complete or time out.";
            return false;
        }
        appendRunLog(QString("Replacing deferred hot reload sequence=%1 with the newest editor request while runtime_state=%2")
                     .arg(m_pendingHotReloadSequence).arg(state));
    }
    if(input.profile.profileId.compare(m_profileId, Qt::CaseInsensitive) != 0)
    {
        if(error) *error = "The active editor profile differs from the running test profile.";
        return false;
    }
    if(!input.profile.valid || !input.profile.runtimeStackValidated
       || input.mprData.isEmpty() || input.mobData.isEmpty() || !input.spawn.valid)
    {
        if(error) *error = "The hot-reload profile, map pair, or selected entrance is incomplete.";
        return false;
    }

    const quint64 sequence = m_hotReloadSequence + 1;
    const quint64 generation = m_saveGeneration + 1;
    const QString reloadRoot = QDir(m_runRoot).filePath(
        QString("generations/%1").arg(generation, 6, 10, QLatin1Char('0')));
    const QString generationSavePath = QDir(reloadRoot).filePath("saves");
    const QString payloadMpr = QDir(reloadRoot).filePath("map.mpr");
    const QString payloadMob = QDir(reloadRoot).filePath("map.mob");
    QJsonObject isolatedSaves = m_manifest.value("isolatedSaves").toObject();
    QJsonArray saveGenerations = isolatedSaves.value("generations").toArray();
    QJsonObject stagedGeneration;
    stagedGeneration.insert("generation", QString::number(generation));
    stagedGeneration.insert("savePath", generationSavePath);
    stagedGeneration.insert("state", "planned");
    int generationIndex = -1;
    for(int index = 0; index < saveGenerations.size(); ++index)
    {
        if(saveGenerations.at(index).toObject().value("generation").toString()
           == QString::number(generation))
        {
            generationIndex = index;
            break;
        }
    }
    if(generationIndex < 0)
    {
        generationIndex = saveGenerations.size();
        saveGenerations.append(stagedGeneration);
    }
    else
        saveGenerations[generationIndex] = stagedGeneration;
    isolatedSaves.insert("generations", saveGenerations);
    m_manifest.insert("isolatedSaves", isolatedSaves);
    if(!writeManifest("reload-staging", error))
        return false;
    if(!QDir().mkpath(generationSavePath)
       || !atomicWrite(payloadMpr, input.mprData, error)
       || !atomicWrite(payloadMob, input.mobData, error))
        return false;

    CValidationReport report;
    CMprDocument mprDocument;
    CMobDocument mobDocument;
    if(!mprDocument.load(QFileInfo(payloadMpr), &report)
       || !mobDocument.load(QFileInfo(payloadMob), &report))
    {
        if(error) *error = "The staged hot-reload pair failed validation:\n" + report.toText();
        return false;
    }

    QHash<QString, QByteArray> desiredSupplemental;
    QStringList desiredSupplementalNames;
    const QString reloadSupplementalDirectory = QDir(reloadRoot).filePath("supplemental");
    if(!input.supplementalMobs.isEmpty()
       && !QDir().mkpath(reloadSupplementalDirectory))
    {
        if(error) *error = "Could not create the hot-reload supplemental-MOB directory.";
        return false;
    }
    for(const SJmvSupplementalMobInput& supplemental : input.supplementalMobs)
    {
        const QString name = supplemental.sourceMob.fileName();
        const QString folded = name.toCaseFolded();
        if(name.isEmpty() || !name.endsWith(".mob", Qt::CaseInsensitive)
           || name.compare(input.sourceMob.fileName(), Qt::CaseInsensitive) == 0
           || name.compare("AutoRunSpellAddon123.mob", Qt::CaseInsensitive) == 0
           || desiredSupplemental.contains(folded))
        {
            if(error) *error = "Hot-reload supplemental MOB names must be unique ordinary .mob filenames.";
            return false;
        }
        const QString auditPath = QDir(reloadSupplementalDirectory).filePath(name);
        if(!atomicWrite(auditPath, supplemental.mobData, error))
            return false;
        CMobDocument supplementalDocument;
        CValidationReport supplementalReport;
        if(!supplementalDocument.load(QFileInfo(auditPath), &supplementalReport))
        {
            if(error) *error = "A hot-reload supplemental MOB failed validation:\n"
                + supplementalReport.toText();
            return false;
        }
        desiredSupplemental.insert(folded, supplemental.mobData);
        desiredSupplementalNames.append(name);
    }

    QJsonObject reloadManifest;
    QString bootstrapPath;
    if(!prepareGodmodeBootstrap(input.profile.mapDirectories, reloadRoot,
                                input.spawn, input.spawnZ, input.godmode,
                                desiredSupplementalNames,
                                &reloadManifest, &bootstrapPath, error))
        return false;

    SMapHotReloadRequest request;
    request.sequence = sequence;
    request.generation = generation;
    request.runtimeStateSequence = latestRuntimeStateSequence();
    request.runId = m_runId;
    request.profileId = input.profile.profileId;
    request.sourceZoneId = m_currentZoneId;
    request.zoneId = input.mapEntry.zoneId;
    request.bootstrapMode = "automatic";
    request.entranceId = input.spawn.exitId;
    request.mprPath = payloadMpr;
    request.mobPath = payloadMob;
    request.mprSha256 = sha256(input.mprData);
    request.mobSha256 = sha256(input.mobData);
    request.bootstrapMobPath = bootstrapPath;
    request.bootstrapMobSha256 = bootstrapPath.isEmpty()
        ? QString() : fileSha256(bootstrapPath);
    request.savePath = generationSavePath;
    // Position preservation requires a verified runtime position provider.
    // Until that provider exists, use normal entrance/custom-spawn bootstrap.
    request.preservePosition = false;
    QStringList validationErrors;
    if(!validateMapHotReloadRequest(request, m_runRoot, &validationErrors))
    {
        if(error) *error = validationErrors.join('\n');
        return false;
    }

    QHash<QString, QByteArray> previousSupplemental;
    QHash<QString, QString> previousSupplementalNames;
    QDir stableSupplemental(m_supplementalMobDirectory);
    for(const QFileInfo& file : stableSupplemental.entryInfoList(
            QStringList() << "*.mob", QDir::Files | QDir::Readable, QDir::Name))
    {
        QByteArray bytes;
        if(!readFile(file.absoluteFilePath(), &bytes, error))
            return false;
        previousSupplemental.insert(file.fileName().toCaseFolded(), bytes);
        previousSupplementalNames.insert(file.fileName().toCaseFolded(), file.fileName());
    }
    const auto restoreSupplemental = [this, &previousSupplemental,
                                      &previousSupplementalNames]()
    {
        QString ignored;
        QDir directory(m_supplementalMobDirectory);
        for(const QFileInfo& file : directory.entryInfoList(
                QStringList() << "*.mob", QDir::Files, QDir::Name))
            QFile::remove(file.absoluteFilePath());
        for(auto it = previousSupplemental.cbegin(); it != previousSupplemental.cend(); ++it)
            atomicWrite(directory.filePath(previousSupplementalNames.value(it.key())),
                        it.value(), &ignored);
        writeManifest("running");
    };
    if(!QDir().mkpath(m_supplementalMobDirectory))
    {
        if(error) *error = "Could not prepare the stable supplemental-MOB directory.";
        return false;
    }
    for(const QFileInfo& file : stableSupplemental.entryInfoList(
            QStringList() << "*.mob", QDir::Files, QDir::Name))
    {
        if(!desiredSupplemental.contains(file.fileName().toCaseFolded())
           && !QFile::remove(file.absoluteFilePath()))
        {
            restoreSupplemental();
            if(error) *error = "Could not retire an owned supplemental MOB before hot reload.";
            return false;
        }
    }
    for(int index = 0; index < desiredSupplementalNames.size(); ++index)
    {
        const QString name = desiredSupplementalNames.at(index);
        const QByteArray bytes = desiredSupplemental.value(name.toCaseFolded());
        const QString path = stableSupplemental.filePath(name);
        if(!atomicWrite(path, bytes, error)
           || fileSha256(path).compare(sha256(bytes), Qt::CaseInsensitive) != 0)
        {
            restoreSupplemental();
            if(error && error->isEmpty())
                *error = "A supplemental MOB failed its hot-reload SHA-256 check.";
            return false;
        }
    }
    const QString auditPath = QDir(reloadRoot).filePath("request.json");
    if(!atomicWrite(auditPath,
                    QJsonDocument(mapHotReloadRequestToJson(request)).toJson(QJsonDocument::Indented),
                    error)
       || !atomicWrite(m_hotReloadRequestPath,
                       mapHotReloadRequestToControlBytes(request), error))
    {
        restoreSupplemental();
        return false;
    }

    QFile threadFile(m_hotReloadThreadPath);
    bool threadOk = false;
    quint32 threadId = 0;
    if(threadFile.open(QIODevice::ReadOnly | QIODevice::Text))
        threadId = threadFile.readAll().trimmed().toULong(&threadOk);
#ifdef Q_OS_WIN
    const bool ownsThread = threadOk && threadId != 0
        && threadBelongsToProcess(threadId, quint32(m_process->processId()));
    constexpr UINT kHotReloadMessage = WM_APP + 0x45E;
    if(!ownsThread || !PostThreadMessageW(DWORD(threadId), kHotReloadMessage, 0, 0))
    {
        restoreSupplemental();
        if(error) *error = "The verified JMV game thread is unavailable for hot reload.";
        return false;
    }
#else
    Q_UNUSED(threadId);
    restoreSupplemental();
    if(error) *error = "JMV hot reload is currently available only on Windows.";
    return false;
#endif

    m_hotReloadSequence = sequence;
    m_pendingHotReloadSequence = sequence;
    m_pendingSaveGeneration = generation;
    m_hotReloadResultPolls = 0;
    m_hotReloadResultTimer->start();
    m_currentZoneId = input.mapEntry.zoneId;
    QJsonArray reloads = m_manifest.value("hotReloads").toArray();
    QJsonObject audit = mapHotReloadRequestToJson(request);
    QJsonArray supplementalAudit;
    for(const QString& name : desiredSupplementalNames)
        supplementalAudit.append(stableSupplemental.filePath(name));
    audit.insert("supplementalMobPaths", supplementalAudit);
    audit.insert("auditPath", auditPath);
    audit.insert("queuedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    reloads.append(audit);
    m_manifest.insert("hotReloads", reloads);
    stagedGeneration.insert("state", "staged");
    saveGenerations[generationIndex] = stagedGeneration;
    isolatedSaves.insert("generations", saveGenerations);
    m_manifest.insert("isolatedSaves", isolatedSaves);
    writeManifest("running");
    appendRunLog(QString("Queued JMV hot reload sequence=%1 generation=%2 zone=%3 exit=%4")
                 .arg(sequence).arg(generation).arg(request.zoneId).arg(request.entranceId));
    emit hotReloadQueued(sequence, request.zoneId);
    return true;
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
    bridgeManifest.insert("transport", "owned-x86-injector");
    m_manifest.insert("bridge", bridgeManifest);
    if(!writeManifest("bridge-planned", error))
        return false;
    if(!atomicWrite(m_bridgeLivePath, bridge, error) ||
       fileSha256(m_bridgeLivePath).compare(m_bridgeSha256, Qt::CaseInsensitive) != 0)
    {
        if(error && error->isEmpty())
            *error = "The run-local Endurance launch bridge failed its SHA-256 check.";
        return false;
    }
    appendRunLog(QString("Staged run-local Endurance launch bridge sha256=%1 path=%2 transport=owned-x86-injector")
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
    const QString line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + " " + message + "\n";
    const auto append = [&line](const QString& path)
    {
        if(path.isEmpty()) return;
        QFile log(path);
        if(log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            log.write(line.toUtf8());
    };
    append(m_runLogPath);
    if(m_sharedRunLogPath.compare(m_runLogPath, Qt::CaseInsensitive) != 0)
        append(m_sharedRunLogPath);
}

QString CJmvMapTestLauncher::bridgeResultSummary() const
{
    QFile result(m_bridgeResultPath);
    if(!result.open(QIODevice::ReadOnly | QIODevice::Text))
        return "Runtime bridge produced no result log; the temporary provider may not have initialized.";
    const QString text = QString::fromUtf8(result.readAll()).trimmed();
    return text.isEmpty() ?
        "Runtime bridge result log was empty; the runtime did not report bridge readiness." :
        "Runtime bridge result:\n" + text;
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
    m_stopRequested = true;
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

void CJmvMapTestLauncher::focusGameWindow(bool maximize)
{
#ifdef Q_OS_WIN
    if(!isRunning())
        return;
    const qint64 ownedPid = m_usesRuntimeHost ? readOwnedGamePid(m_gamePidPath)
                                             : m_process->processId();
    if(ownedPid <= 0)
    {
        appendRunLog("Owned game PID is not available for focus yet");
        return;
    }
    FocusContext context{DWORD(ownedPid), nullptr};
    EnumWindows(findWindowForProcess, LPARAM(&context));
    if(context.window)
    {
        ShowWindow(context.window, maximize ? SW_MAXIMIZE : SW_RESTORE);
        BringWindowToTop(context.window);
        const bool focused = SetForegroundWindow(context.window) != FALSE;
        appendRunLog(QString("Game window focus requested hwnd=0x%1 result=%2 maximize=%3")
                     .arg(quintptr(context.window), 0, 16).arg(focused).arg(maximize));
    }
    else
        appendRunLog("Game window not ready for focus yet");
#endif
}

void CJmvMapTestLauncher::refreshInterruptedRecovery()
{
    QString message;
    QString active;
    bool pending = false;
    if(hasActiveInterruptedRun(&active))
    {
        message = active;
        pending = true;
    }
    else
        message = recoverInterruptedRuns(&pending);

    m_recoveryPending = pending;
    if(pending)
    {
        if(!m_recoveryTimer->isActive())
            m_recoveryTimer->start();
        if(message.isEmpty())
            message = "Waiting for the interrupted game process to finish exiting before cleanup.";
    }
    else
        m_recoveryTimer->stop();

    if(message != m_lastPublishedRecoveryStatus
       || pending != m_lastPublishedRecoveryPending)
    {
        m_lastPublishedRecoveryStatus = message;
        m_lastPublishedRecoveryPending = pending;
        emit recoveryStatusChanged(message, pending);
    }
}

QString CJmvMapTestLauncher::recoverInterruptedRuns(bool* pending)
{
    QStringList summary;
    bool deferred = false;
    const QDir root(testRunsRoot());
    if(!root.exists())
    {
        if(pending)
            *pending = false;
        return QString();
    }
    // The newest unfinished run normally owns the currently installed shared
    // plugin slot. Clean it first so older journals see an already-absent file
    // instead of reporting the newer bridge as an external modification.
    for(const QFileInfo& runDirectory : root.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed))
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
        QJsonObject expectedProcess = manifest.value("process").toObject();
        if(expectedProcess.isEmpty() && manifest.value("pid").toDouble() > 0)
            expectedProcess.insert("pid", manifest.value("pid"));
        if(processIdentityMatches(expectedProcess))
        {
            deferred = true;
            continue;
        }
        const qint64 recordedPid = qint64(expectedProcess.value("pid").toDouble());
        const bool incompleteIdentity = expectedProcess.value("imagePath").toString().isEmpty()
            || expectedProcess.value("creationTime100ns").toString().isEmpty();
        if((incompleteIdentity && recordedPid > 0 && processExists(recordedPid))
           || anotherGameIsRunning())
        {
            deferred = true;
            continue;
        }
        QStringList messages;
        const bool success = cleanupManifest(manifest, &messages);
        manifest.insert("state", success ? "cleaned" : "cleanup-needed");
        manifest.insert("recoveredUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        QString writeError;
        atomicWrite(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented), &writeError);
        summary.append(QString("%1: %2").arg(runDirectory.fileName(), messages.join("; ")));
    }
    if(pending)
        *pending = deferred;
    return summary.join("\n");
}

bool CJmvMapTestLauncher::requestCloseActiveInterruptedRun(QString* error)
{
#ifdef Q_OS_WIN
    const QDir root(testRunsRoot());
    for(const QFileInfo& runDirectory : root.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        QFile file(QDir(runDirectory.absoluteFilePath()).filePath("manifest.json"));
        if(!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if(!document.isObject())
            continue;
        const QJsonObject manifest = document.object();
        if(manifest.value("state").toString() == "cleaned")
            continue;
        QJsonObject expectedProcess = manifest.value("process").toObject();
        if(!processIdentityMatches(expectedProcess))
            continue;
        const qint64 pid = qint64(expectedProcess.value("pid").toDouble());
        FocusContext context{DWORD(pid), nullptr};
        EnumWindows(findWindowForProcess, LPARAM(&context));
        if(!context.window)
        {
            if(error)
                *error = QString("The verified test game (PID %1) has no closable top-level window. Close it manually; Endurance will keep monitoring it.").arg(pid);
            return false;
        }
        if(!PostMessageW(context.window, WM_CLOSE, 0, 0))
        {
            if(error)
                *error = QString("Windows refused the close request for the verified test game (PID %1). Close it manually; Endurance will keep monitoring it.").arg(pid);
            return false;
        }
        return true;
    }
    if(error)
        *error = "No still-running Endurance-owned test game could be verified.";
    return false;
#else
    if(error)
        *error = "Closing an interrupted test game is currently supported only on Windows.";
    return false;
#endif
}

bool CJmvMapTestLauncher::hasActiveInterruptedRun(QString* description)
{
    const QDir root(testRunsRoot());
    if(!root.exists())
        return false;
    for(const QFileInfo& runDirectory : root.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        QFile file(QDir(runDirectory.absoluteFilePath()).filePath("manifest.json"));
        if(!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if(!document.isObject())
            continue;
        const QJsonObject manifest = document.object();
        if(manifest.value("state").toString() == "cleaned")
            continue;
        QJsonObject expectedProcess = manifest.value("process").toObject();
        if(expectedProcess.isEmpty() && manifest.value("pid").toDouble() > 0)
            expectedProcess.insert("pid", manifest.value("pid"));
        const qint64 recordedPid = qint64(expectedProcess.value("pid").toDouble());
        const bool completeIdentity = !expectedProcess.value("imagePath").toString().isEmpty()
            && !expectedProcess.value("creationTime100ns").toString().isEmpty();
        const bool verified = completeIdentity && processIdentityMatches(expectedProcess);
        const bool conservativePending = (!completeIdentity && recordedPid > 0
                                          && processExists(recordedPid))
            || (expectedProcess.isEmpty() && anotherGameIsRunning());
        if(!verified && !conservativePending)
            continue;
        if(description)
        {
            *description = verified
                ? QString("Interrupted test run %1 is still active (verified PID %2).")
                    .arg(runDirectory.fileName()).arg(recordedPid)
                : QString("Interrupted test run %1 has incomplete legacy process identity; cleanup is deferred while its recorded process is alive.")
                    .arg(runDirectory.fileName());
        }
        return true;
    }
    return false;
}

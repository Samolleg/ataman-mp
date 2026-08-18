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
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QProcessEnvironment>
#include <QtEndian>
#include <algorithm>
#include <cmath>

#include "endurance_document.h"
#include "current_session_backup.h"
#include "hot_reload_protocol.h"
#include "res_archive_document.h"
#include "runtime_providers.h"
#include "synthetic_map_table_archive.h"

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
    QByteArray bytes = file.readAll();
    // alpha.29's x86 host used fwprintf on a binary stream and therefore
    // emitted UTF-16LE without a BOM. Accept those journals during recovery as
    // well as the ASCII protocol emitted by current hosts.
    if(bytes.size() >= 2 && bytes.at(1) == '\0')
    {
        QByteArray ascii;
        ascii.reserve((bytes.size() + 1) / 2);
        for(int index = 0; index < bytes.size(); index += 2)
        {
            if(index + 1 >= bytes.size() || bytes.at(index + 1) != '\0')
                return 0;
            ascii.append(bytes.at(index));
        }
        bytes = ascii;
    }
    bool ok = false;
    const qint64 pid = QString::fromLatin1(bytes).trimmed().toLongLong(&ok);
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
    return QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

#ifdef Q_OS_WIN
QString registry32String(HKEY root, const wchar_t* subkey, const wchar_t* valueName)
{
    HKEY key = nullptr;
    if(RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &key)
       != ERROR_SUCCESS)
        return QString();
    wchar_t value[MAX_PATH * 4]{};
    DWORD type = 0;
    DWORD size = DWORD(sizeof(value));
    const LSTATUS status = RegQueryValueExW(
        key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    if(status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return QString();
    value[(sizeof(value) / sizeof(value[0])) - 1] = L'\0';
    return QString::fromWCharArray(value);
}
#endif

QString saveGenerationPath(const QString& runRoot, quint64 generation)
{
    return QDir(runRoot).filePath(QString("generations/%1/saves")
        .arg(generation, 6, 10, QLatin1Char('0')));
}

bool removeIfOwned(const QString& path, const QString& expectedHash, QStringList* messages)
{
    // A stopped runtime host can report completion a fraction before Windows
    // releases the last image-section handle held by its game child. Keep the
    // hash guard on every attempt, but tolerate that short teardown window.
    constexpr int kRemovalAttempts = 11;
    for(int attempt = 0; attempt < kRemovalAttempts; ++attempt)
    {
        if(!QFileInfo::exists(path))
        {
            if(attempt > 0)
                messages->append(QString("Removed temporary file after %1 cleanup retries: %2")
                                 .arg(attempt).arg(path));
            return true;
        }
        const QString currentHash = fileSha256(path);
        if(currentHash.compare(expectedHash, Qt::CaseInsensitive) != 0)
        {
            messages->append(QString("Retained changed temporary file: %1").arg(path));
            return false;
        }
        if(QFile::remove(path))
        {
            messages->append(attempt == 0
                ? QString("Removed temporary file: %1").arg(path)
                : QString("Removed temporary file after %1 cleanup retries: %2")
                    .arg(attempt).arg(path));
            return true;
        }
        if(attempt + 1 < kRemovalAttempts)
            QThread::msleep(100);
    }
    messages->append(QString("Could not remove temporary file after 1 second of hash-checked retries: %1")
                     .arg(path));
    return false;
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
    success = removeIfOwned(live.value("grpPath").toString(),
                            live.value("grpSha256").toString(), messages) && success;
    success = removeIfOwned(live.value("placeholderResPath").toString(),
                            live.value("placeholderResSha256").toString(), messages) && success;
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

QString nativeWindowText(HWND window)
{
    wchar_t text[2048]{};
    GetWindowTextW(window, text, int(sizeof(text) / sizeof(text[0])));
    return QString::fromWCharArray(text);
}

struct RuntimeFailureWindowContext
{
    DWORD pid{0};
    QString summary;
};

BOOL CALLBACK collectChildWindowText(HWND window, LPARAM parameter)
{
    QString* text = reinterpret_cast<QString*>(parameter);
    const QString child = nativeWindowText(window).trimmed();
    if(!child.isEmpty())
        *text += (text->isEmpty() ? QString() : QStringLiteral(" | ")) + child;
    return TRUE;
}

BOOL CALLBACK findRuntimeFailureWindow(HWND window, LPARAM parameter)
{
    RuntimeFailureWindowContext* context =
        reinterpret_cast<RuntimeFailureWindowContext*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if(pid != context->pid || !IsWindowVisible(window))
        return TRUE;
    wchar_t className[128]{};
    GetClassNameW(window, className, int(sizeof(className) / sizeof(className[0])));
    const QString title = nativeWindowText(window).trimmed();
    QString body;
    EnumChildWindows(window, collectChildWindowText, LPARAM(&body));
    if(CJmvMapTestLauncher::isRuntimeFailureDialog(
           title, QString::fromWCharArray(className), body))
    {
        context->summary = QStringLiteral("title=\"%1\" class=\"%2\" text=\"%3\"")
            .arg(title, QString::fromWCharArray(className), body.left(512));
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
      m_runtimeHealthTimer(new QTimer(this)),
      m_recoveryTimer(new QTimer(this)),
      m_cleanupComplete(true), m_baseProbe(false)
{
    m_runtimeHealthTimer->setInterval(500);
    connect(m_runtimeHealthTimer, &QTimer::timeout, this, [this]()
    {
#ifdef Q_OS_WIN
        if(m_runtimeFailureObserved || !isRunning())
            return;
        const qint64 pid = runtimeGameProcessId();
        if(pid <= 0)
            return;
        RuntimeFailureWindowContext context;
        context.pid = DWORD(pid);
        EnumWindows(findRuntimeFailureWindow, LPARAM(&context));
        if(context.summary.isEmpty())
            return;
        m_runtimeFailureObserved = true;
        m_processCrashObserved = true;
        m_manifest.insert("runtimeFailureObserved", true);
        m_manifest.insert("runtimeFailureEvidence", context.summary);
        appendRunLog("Runtime failure dialog observed: " + context.summary);
        writeManifest("runtime-failure-observed");
        stop();
        emit testError(QStringLiteral(
            "The staged JMV runtime reported a fatal error. This run is failed; "
            "GameplayReady and an unchanged process ID cannot override a native "
            "runtime failure.\n\n%1").arg(context.summary));
#endif
    });
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
        m_runtimeHealthTimer->start();
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
        const bool restarting = m_restartPending;
        const SJmvMapTestLaunchInput genericRestartInput = m_restartInput;
        const bool terrainAtlasRestart = m_terrainAtlasRestartPending;
        const SJmvMapTestLaunchInput terrainRestartInput =
            m_pendingTerrainAtlasRestartInput;
        const QString completedRunId = m_runId;
        m_hotReloadReadyTimer->stop();
        m_hotReloadResultTimer->stop();
        m_runtimeHealthTimer->stop();
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
            exitCode, int(status), m_stopRequested && !m_processCrashObserved,
            cleanupOnly);
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
        if(!restarting && !terrainAtlasRestart)
            emit testReportReady(report);
        else
            appendRunLog(
                "Suppressing the intermediate stopped-run report while restarting the owned test process");
        QString cleanupMessage = cleanupOnly;
        if(!cleanupMessage.isEmpty())
            cleanupMessage += "\n";
        cleanupMessage += bridgeResult;
        emit testFinished(completedRunId, exitCode, cleanupMessage);
        if(restarting)
        {
            m_restartPending = false;
            m_restartInput = SJmvMapTestLaunchInput();
            QTimer::singleShot(0, this, [this, genericRestartInput]()
            {
                QString restartError;
                if(!launch(genericRestartInput, &restartError))
                    emit testError(QCoreApplication::translate(
                        "CJmvMapTestLauncher",
                        "The fresh Test Current Map process could not be started after stopping the previous process.\n\n%1")
                        .arg(restartError));
            });
        }
        else if(terrainAtlasRestart)
        {
            QTimer::singleShot(0, this, [this, terrainRestartInput]()
            {
                m_terrainAtlasRestartPending = false;
                m_pendingTerrainAtlasRestartInput = SJmvMapTestLaunchInput();
                if(!m_cleanupComplete)
                {
                    emit testError(QStringLiteral(
                        "The previous owned test process stopped, but its guarded cleanup did not complete. "
                        "The changed terrain atlas was not launched; inspect the test-run recovery status."));
                    return;
                }
                QString restartError;
                if(!launch(terrainRestartInput, &restartError))
                    emit testError(QStringLiteral(
                        "The owned test process stopped safely, but restarting it with the changed terrain atlas failed:\n\n%1")
                        .arg(restartError));
            });
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
    {
        if(error == QProcess::Crashed && !m_stopRequested)
            m_processCrashObserved = true;
        if(error == QProcess::Crashed && m_stopRequested)
            appendRunLog("Endurance-owned process terminated after Stop Test request");
        else
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

bool CJmvMapTestLauncher::terrainAtlasHotReloadRequiresRestart(
    const QString& runningSourceAtlasSha256, int runningAtlasPages,
    int runningAtlasSlots, const QString& candidateSourceAtlasSha256,
    int candidateAtlasPages, int candidateAtlasSlots)
{
    return runningSourceAtlasSha256.compare(
               candidateSourceAtlasSha256, Qt::CaseInsensitive) != 0
        || runningAtlasPages != candidateAtlasPages
        || runningAtlasSlots != candidateAtlasSlots;
}

bool CJmvMapTestLauncher::rebaseRuntimeTerrainAtlas(
    const QByteArray& sourceMpr,
    const QByteArray& sourceAtlasRes,
    const QString& runtimeNamespace,
    int expectedPages,
    QByteArray* rebasedMpr,
    QByteArray* rebasedAtlasRes,
    QStringList* rebasedMmpMembers,
    QString* error)
{
    if(!rebasedMpr || !rebasedAtlasRes || !rebasedMmpMembers
       || sourceMpr.isEmpty() || sourceAtlasRes.isEmpty()
       || expectedPages <= 0 || runtimeNamespace.isEmpty())
    {
        if(error) *error = "The runtime terrain-atlas rebase request is incomplete.";
        return false;
    }
    CMprDocument namespaceSource;
    CValidationReport namespaceReport;
    if(!namespaceSource.loadBytes(sourceMpr,
                                  QStringLiteral("runtime-atlas-source.mpr"),
                                  &namespaceReport))
    {
        if(error) *error = "The run-owned MPR could not be decoded before rebasing:\n"
            + namespaceReport.toText();
        return false;
    }
    const QString sourceNamespace = namespaceSource.mapInfo().innerNamespace;
    if(sourceNamespace.isEmpty()
       || int(namespaceSource.mapInfo().textureCount) != expectedPages)
    {
        if(error) *error = QString(
            "The run-owned MPR terrain-atlas contract differs: namespace=%1, "
            "MPR pages=%2, expected pages=%3.")
            .arg(sourceNamespace)
            .arg(namespaceSource.mapInfo().textureCount)
            .arg(expectedPages);
        return false;
    }

    QStringList archiveErrors;
    CResArchiveDocument sourceArchive;
    if(!sourceArchive.loadBytes(sourceAtlasRes,
                                QStringLiteral("run-owned-terrain-atlas.res"),
                                &archiveErrors))
    {
        if(error) *error = "The run-owned terrain atlas RES is invalid:\n"
            + archiveErrors.join('\n');
        return false;
    }
    // A published RES may legitimately contain unrelated atlas identities or
    // stale higher pages left by an older, larger version of this identity.
    // Stage only the contiguous page set named by the MPR's exact namespace
    // and declared page count. Counting every MMP member caused Test Current
    // to reject (or stage the wrong pages from) otherwise valid archives.
    QStringList sourceMembers;
    sourceMembers.reserve(expectedPages);
    for(int page = 0; page < expectedPages; ++page)
    {
        const QString expectedName = QStringLiteral("%1%2.mmp")
            .arg(sourceNamespace).arg(page, 3, 10, QLatin1Char('0'));
        bool found = false;
        for(const SResArchiveMember& member : sourceArchive.members())
        {
            if(member.name.compare(expectedName, Qt::CaseInsensitive) != 0)
                continue;
            if(found)
            {
                if(error) *error = "The run-owned terrain atlas contains duplicate page "
                    + expectedName + ".";
                return false;
            }
            sourceMembers.append(member.name);
            found = true;
        }
        if(!found)
        {
            if(error) *error = "The run-owned terrain atlas is missing required page "
                + expectedName + ".";
            return false;
        }
    }

    CMprEditTransaction namespaceTransaction(
        QStringLiteral("Rebase runtime terrain atlas namespace"));
    QByteArray runtimeMpr = sourceMpr;
    if(!namespaceSource.loadBytes(runtimeMpr,
                                  QStringLiteral("runtime-atlas-source.mpr"),
                                  &namespaceReport)
       || !namespaceSource.applyIsolatedNamespaceTransaction(
           namespaceTransaction, runtimeNamespace,
           &runtimeMpr, &namespaceReport))
    {
        if(error) *error = "The run-owned MPR could not be rebased to its temporary runtime namespace:\n"
            + namespaceReport.toText();
        return false;
    }

    QByteArray runtimeArchive(16, '\0');
    qToLittleEndian<quint32>(0x019CE23C,
        reinterpret_cast<uchar*>(runtimeArchive.data()));
    qToLittleEndian<quint32>(16,
        reinterpret_cast<uchar*>(runtimeArchive.data() + 8));
    CResArchiveDocument destinationArchive;
    if(!destinationArchive.loadBytes(runtimeArchive,
                                     QStringLiteral("runtime-atlas-empty.res"),
                                     &archiveErrors))
    {
        if(error) *error = "Could not initialize the rebased terrain atlas RES:\n"
            + archiveErrors.join('\n');
        return false;
    }
    QStringList runtimeMembers;
    QVector<SResArchiveNewMember> appendedMembers;
    appendedMembers.reserve(sourceMembers.size());
    for(int page = 0; page < sourceMembers.size(); ++page)
    {
        const QByteArray payload = sourceArchive.memberPayload(sourceMembers.at(page));
        const QString memberName = QStringLiteral("%1%2.mmp")
            .arg(runtimeNamespace).arg(page, 3, 10, QLatin1Char('0'));
        if(payload.isEmpty())
        {
            if(error) *error = "A run-owned terrain atlas page is empty.";
            return false;
        }
        SResArchiveNewMember appended;
        appended.name = memberName;
        appended.payload = payload;
        appendedMembers.append(appended);
        runtimeMembers.append(memberName);
    }
    if(!destinationArchive.buildWithAppendedMembers(
           appendedMembers, &runtimeArchive, &archiveErrors))
    {
        if(error) *error = "Could not bulk-rebase the run-owned terrain atlas pages:\n"
            + archiveErrors.join('\n');
        return false;
    }
    *rebasedMpr = runtimeMpr;
    *rebasedAtlasRes = runtimeArchive;
    *rebasedMmpMembers = runtimeMembers;
    return true;
}

QString CJmvMapTestLauncher::generationSavesPath(const QString& runRoot,
                                                 quint64 generation)
{
    return saveGenerationPath(runRoot, generation);
}

bool CJmvMapTestLauncher::validateJmvGameRootPaths(
    const QString& selectedGameRoot, const QString& workPath,
    const QString& resourcesPath, const QString& cdRomPath, QString* error)
{
    const QString selected = normalized(selectedGameRoot);
    const QString expectedResources = normalized(QDir(selected).filePath("Res"));
    const auto matches = [](const QString& actual, const QString& expected) {
        return !actual.trimmed().isEmpty()
            && normalized(actual).compare(expected, Qt::CaseInsensitive) == 0;
    };
    if(matches(workPath, selected) && matches(resourcesPath, expectedResources)
       && matches(cdRomPath, selected))
        return true;
    if(error)
    {
        *error = QCoreApplication::translate("CJmvMapTestLauncher",
            "The 32-bit Evil Islands runtime paths do not match the selected Endurance game. "
            "Launching this mixed-root configuration can load incompatible resources and report "
            "missing prototypes.\n\nSelected game root:\n%1\n\n32-bit registry paths:\n"
            "Work path: %2\nResources path: %3\nCD-ROM Path: %4\n\n"
            "Restore the runtime paths for the selected installation, then retry Test Current Map.")
            .arg(QDir::toNativeSeparators(selected),
                 workPath.isEmpty() ? QString("<not configured>")
                                    : QDir::toNativeSeparators(workPath),
                 resourcesPath.isEmpty() ? QString("<not configured>")
                                         : QDir::toNativeSeparators(resourcesPath),
                 cdRomPath.isEmpty() ? QString("<not configured>")
                                     : QDir::toNativeSeparators(cdRomPath));
    }
    return false;
}

bool CJmvMapTestLauncher::validateExternalGraph(const QByteArray& bytes,
                                                QString* error)
{
    if(bytes.size() < 8)
    {
        if(error) *error = "The external AI graph is too small to contain width and height.";
        return false;
    }
    const uchar* raw = reinterpret_cast<const uchar*>(bytes.constData());
    const quint32 width = qFromLittleEndian<quint32>(raw);
    const quint32 height = qFromLittleEndian<quint32>(raw + 4);
    if(width == 0 || height == 0 || width > 256 || height > 256)
    {
        if(error)
            *error = QString("The external AI graph has invalid dimensions %1x%2.")
                .arg(width).arg(height);
        return false;
    }
    constexpr quint64 bytesPerCoordinate = 8u * (8u * 2u + 1u + 2u);
    const quint64 expected = 8u + quint64(width) * quint64(height) * bytesPerCoordinate;
    if(expected != quint64(bytes.size()))
    {
        if(error)
            *error = QString("The external AI graph size is %1 bytes; %2x%3 requires exactly %4 bytes.")
                .arg(bytes.size()).arg(width).arg(height).arg(expected);
        return false;
    }
    return true;
}

bool CJmvMapTestLauncher::bridgeSupportsGraphlessLaunch(
    const QByteArray& bridgeBytes)
{
    const QString marker = QStringLiteral("EI_MAPPER_ENDURANCE_GRAPHLESS");
    const QByteArray utf16Marker(
        reinterpret_cast<const char*>(marker.utf16()), marker.size() * 2);
    return bridgeBytes.contains(utf16Marker);
}

bool CJmvMapTestLauncher::bridgeSupportsSyntheticMapIdentity(
    const QByteArray& bridgeBytes)
{
    const QString marker = QStringLiteral("EI_MAPPER_ENDURANCE_SYNTHETIC_MAP");
    const QByteArray utf16Marker(
        reinterpret_cast<const char*>(marker.utf16()), marker.size() * 2);
    return bridgeBytes.contains(utf16Marker);
}

bool CJmvMapTestLauncher::hasAutoSpellMob(
    const SResourceProfileEvaluation& profile,
    const QString& zoneType,
    bool syntheticMapIdentity,
    QString* reason)
{
    auto reject = [reason](const QString& message) {
        if(reason)
            *reason = message;
        return false;
    };
    if(reason)
        reason->clear();
    if(profile.profileId.compare("jamais-vu", Qt::CaseInsensitive) != 0)
        return reject(QStringLiteral("The active profile does not support Play from Here."));
    if(!syntheticMapIdentity
       && zoneType.compare("game", Qt::CaseInsensitive) != 0)
    {
        if(zoneType.compare("brief", Qt::CaseInsensitive) == 0)
            return reject(QStringLiteral("The current briefing map does not support Play from Here."));
        return reject(QStringLiteral("The current map does not support Play from Here."));
    }

    QString engineDirectory;
    for(const SRuntimeComponentEvaluation& component : profile.runtimeComponents)
    {
        if(component.id.compare("starter-engine", Qt::CaseInsensitive) == 0)
        {
            engineDirectory = QFileInfo(component.path).absolutePath();
            break;
        }
    }
    if(engineDirectory.isEmpty())
        engineDirectory = QDir(profile.gameRoot).filePath("Starter/Engine");
    if(findCaseInsensitiveFile(QStringList() << engineDirectory,
                               "SpellAddonX.asi").isEmpty())
        return reject(QStringLiteral("Play from Here requires SpellAddon's autorun-MOB feature."));

    const QString settingsPath = findCaseInsensitiveFile(
        QStringList() << profile.modRoot, "SPELLADDON.INI");
    if(!settingsPath.isEmpty())
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        if(settings.value("gameplay/autorunmobfile", 1).toInt() < 1)
            return reject(QStringLiteral("Play from Here is disabled by gameplay/autorunmobfile in SPELLADDON.INI."));
    }
    if(findCaseInsensitiveFile(profile.mapDirectories,
                               "AutoRunSpellAddon123.mob").isEmpty())
        return reject(QStringLiteral("Play from Here requires AutoRunSpellAddon123.mob in the active mod."));
    return true;
}

bool CJmvMapTestLauncher::resolveMapGraphSource(
    const QByteArray& primaryMobData,
    const QFileInfo& sourceMpr,
    const QString& logicalMprName,
    const QStringList& mapDirectories,
    SMapGraphSource* graph,
    QString* error)
{
    if(!graph)
    {
        if(error) *error = "No graph-source result destination was provided.";
        return false;
    }
    *graph = SMapGraphSource();

    CMobDocument primaryMob;
    CValidationReport report;
    if(!primaryMob.loadBytes(primaryMobData, "test-run-primary.mob", &report))
    {
        if(error) *error = "The primary MOB could not be inspected for its AI graph:\n"
            + report.toText();
        return false;
    }

    QStringList candidates;
    const QString sourceBase = sourceMpr.completeBaseName();
    const QString logicalBase = QFileInfo(logicalMprName).completeBaseName();
    const auto appendCandidate = [&candidates](const QString& directory,
                                               const QString& baseName)
    {
        if(directory.isEmpty() || baseName.isEmpty())
            return;
        const QString found = findCaseInsensitiveFile(
            QStringList() << directory, baseName + ".grp");
        if(found.isEmpty())
            return;
        for(const QString& existing : candidates)
            if(QFileInfo(existing).absoluteFilePath().compare(
                   QFileInfo(found).absoluteFilePath(), Qt::CaseInsensitive) == 0)
                return;
        candidates.append(found);
    };

    // The open workspace overlays installed profile maps. Prefer the selected
    // MPR's own basename, then its borrowed/logical map-table identity.
    appendCandidate(sourceMpr.absolutePath(), sourceBase);
    appendCandidate(sourceMpr.absolutePath(), logicalBase);
    for(const QString& directory : mapDirectories)
    {
        appendCandidate(directory, logicalBase);
        appendCandidate(directory, sourceBase);
    }

    graph->embedded = primaryMob.hasEmbeddedAiGraph();
    if(graph->embedded)
    {
        if(!candidates.isEmpty())
            graph->shadowedExternalPath = candidates.first();
        return true;
    }
    if(candidates.isEmpty())
    {
        graph->missing = true;
        return true;
    }

    graph->externalPath = candidates.first();
    if(!readFile(graph->externalPath, &graph->externalData, error))
        return false;
    QString graphError;
    if(!validateExternalGraph(graph->externalData, &graphError))
    {
        if(error)
            *error = QString("The required external graph is invalid:\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(graph->externalPath), graphError);
        return false;
    }
    graph->sha256 = sha256(graph->externalData);
    return true;
}

qint64 CJmvMapTestLauncher::runtimeGameProcessId(bool usesRuntimeHost,
                                                 const QString& gamePidPath,
                                                 qint64 transportProcessId)
{
    return usesRuntimeHost ? readOwnedGamePid(gamePidPath) : transportProcessId;
}

qint64 CJmvMapTestLauncher::runtimeGameProcessId() const
{
    return runtimeGameProcessId(m_usesRuntimeHost, m_gamePidPath,
                                m_process->processId());
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
    const bool runtimeFailureObserved = manifest.value(
        "runtimeFailureObserved").toBool();
    const QString runtimeFailureEvidence = manifest.value(
        "runtimeFailureEvidence").toString();

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
    const bool tablesLuaStaged = manifest.contains("tablesLua");
    const QJsonObject tablesLuaJournal = manifest.value("tablesLua").toObject();
    const bool tablesLuaRedirectObserved = bridgeResult.contains("TABLES_LUA_REDIRECT");
    bool tablesLuaLoadObserved = !tablesLuaStaged;
    if(tablesLuaStaged)
    {
        QFile evidence(tablesLuaJournal.value("loadEvidencePath").toString());
        tablesLuaLoadObserved = evidence.open(QIODevice::ReadOnly)
            && QString::fromLatin1(evidence.readAll()).trimmed().compare(
                tablesLuaJournal.value("stagedSha256").toString(),
                Qt::CaseInsensitive) == 0;
    }
    const QJsonObject atlasProbe = manifest.value(
        "terrainAtlasRuntimeProbe").toObject();
    const bool atlasProbeRequested = !atlasProbe.isEmpty();
    const int atlasPages = atlasProbe.value("texturePages").toInt();
    const int atlasSlots = atlasProbe.value("activeSlots").toInt();
    const QString textureProbeName = atlasProbe.value("textureProbeName").toString();
    const QString textureProbeMemberName = atlasProbe.value(
        "textureProbeMemberName").toString();
    const QString runtimeAtlasNamespace = atlasProbe.value(
        "runtimeNamespace").toString();
    const QString temporaryMapName = manifest.value("temporaryMapName").toString();
    const bool runtimeNamespaceAligned = !atlasProbeRequested
        || (!runtimeAtlasNamespace.isEmpty() && !temporaryMapName.isEmpty()
            && runtimeAtlasNamespace.compare(
                temporaryMapName, Qt::CaseInsensitive) == 0);
    const bool textureResourceRegistered = bridgeResult.contains(
        "TEMP_TEXTURE_RES registered");
    const bool textureProbeResolved = !atlasProbeRequested
        || (!textureProbeMemberName.isEmpty() && bridgeResult.contains(
            QRegularExpression(QStringLiteral(
                "TEMP_TEXTURE_RES probe resolved name=\\\"")
                + QRegularExpression::escape(textureProbeMemberName)
                + QStringLiteral("\\\""))));
    const bool textureRequestObservationArmed = !atlasProbeRequested
        || bridgeResult.contains(
            "TEMP_TEXTURE_RES armed terrain texture-request observation");
    bool allTerrainPageRequestsResolved = !atlasProbeRequested;
    int resolvedTerrainPageRequests = 0;
    if(atlasProbeRequested && atlasPages > 0 && !runtimeAtlasNamespace.isEmpty())
    {
        allTerrainPageRequestsResolved = true;
        for(int page = 0; page < atlasPages; ++page)
        {
            const QString memberName = QStringLiteral("%1%2.mmp")
                .arg(runtimeAtlasNamespace).arg(page, 3, 10, QLatin1Char('0'));
            const QRegularExpression requestPattern(
                QStringLiteral("TERRAIN_TEXTURE_REQUEST name=\\\"")
                + QRegularExpression::escape(memberName)
                + QStringLiteral("\\\" result=resolved"));
            if(bridgeResult.contains(requestPattern))
                ++resolvedTerrainPageRequests;
            else
                allTerrainPageRequestsResolved = false;
        }
    }
    const bool customAtlasContractValidated = !atlasProbeRequested
        || (atlasPages > 0 && atlasSlots > 0
            && atlasProbe.value("resourceMmpMembers").toInt() == atlasPages
            && runtimeNamespaceAligned
            && !atlasProbe.value("installedArchivesModified").toBool()
            && textureResourceRegistered && textureProbeResolved
            && textureRequestObservationArmed
            && allTerrainPageRequestsResolved
            && stagedLoaderObserved && gameplayReady);
    const bool expandedAtlasRequested = atlasProbeRequested
        && (atlasPages > 8 || atlasSlots > 512);
    const bool expandedAtlasContractValidated = !expandedAtlasRequested
        || (customAtlasContractValidated && atlasPages > 8 && atlasSlots > 512
            && atlasProbe.value("exceedsLegacyEightPages").toBool());
    const QJsonObject drawall = manifest.value("drawallStress").toObject();
    const bool drawallRequested = drawall.value("requested").toBool();
    const bool drawallAutoexecRedirected = !drawallRequested
        || bridgeResult.contains("DRAWALL_AUTOEXEC redirect");
    const QString drawallHash = drawall.value("ownedSha256").toString();
    const bool drawallAutoexecConsumed = !drawallRequested
        || (!drawallHash.isEmpty() && bridgeResult.contains(
            QRegularExpression(QStringLiteral("DRAWALL_AUTOEXEC consumed.*sha256=")
                + QRegularExpression::escape(drawallHash))));
    const bool drawallContractValidated = !drawallRequested
        || (!drawall.value("installedConfigurationModified").toBool()
            && drawall.value("command").toString() == QStringLiteral("drawall 1")
            && drawallAutoexecRedirected && drawallAutoexecConsumed);
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
    QString playerEvidenceScope = bridgeResult;
    if(completedReloadSequence != 0)
    {
        const int latestDispatch = bridgeResult.lastIndexOf(
            QString("HOT_RELOAD dispatch sequence=%1")
                .arg(completedReloadSequence));
        if(latestDispatch >= 0)
            playerEvidenceScope = bridgeResult.mid(latestDispatch);
    }
    const int playerInitializations = playerEvidenceScope.count(
        "DIRECT_LAUNCH initialized New Game player");
    const bool controlledPlayerConfirmed = playerInitializations == 1;

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
    checkRemoved(live.value("grpPath").toString());
    checkRemoved(live.value("placeholderResPath").toString());
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
    if(atlasProbeRequested && !textureResourceRegistered)
        addUnique(&errors, "The runtime did not register the run-owned terrain atlas RES.");
    if(atlasProbeRequested && !textureProbeResolved)
        addUnique(&errors, "The runtime did not resolve the highest-page texture from the run-owned terrain atlas RES.");
    if(atlasProbeRequested && !textureRequestObservationArmed)
        addUnique(&errors, "The runtime did not arm actual terrain texture-request observation.");
    if(atlasProbeRequested && !allTerrainPageRequestsResolved)
        addUnique(&errors, "The runtime workload did not request and resolve every expected run-owned atlas page.");
    if(atlasProbeRequested && !customAtlasContractValidated)
        addUnique(&errors, "The custom terrain atlas runtime contract was not proven.");
    if(expandedAtlasRequested && !expandedAtlasContractValidated)
        addUnique(&errors, "The expanded terrain atlas runtime contract was not proven.");
    if(drawallRequested && !drawallContractValidated)
        addUnique(&errors, "The run-owned Zone18 drawall staging contract was not proven.");
    if(stagedLoaderObserved && !payloadHashesValidated)
        addUnique(&errors, "The runtime loader paths could not be matched to the hash-verified staged payloads.");
    if(tablesLuaStaged && !tablesLuaLoadObserved)
        addUnique(&errors, "The staged tables.lua was not read by the JMV runtime.");
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
    if(!controlledPlayerConfirmed)
        addUnique(&errors, QString("The staged run reported %1 controlled-player "
                                  "initializations; exactly one is required.")
                  .arg(playerInitializations));
    if(!sourceFilesUnchanged)
        addUnique(&errors, "One or more source map files changed during the test.");
    if(!cleanupComplete)
        addUnique(&errors, "One or more owned runtime artifacts could not be cleaned up safely.");
    if(runtimeFailureObserved)
        addUnique(&errors, runtimeFailureEvidence.isEmpty()
            ? QStringLiteral("A native runtime failure was observed during the staged run.")
            : QStringLiteral("A native runtime failure was observed during the staged run: ")
                + runtimeFailureEvidence);
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
    report.insert("tablesLuaStaged", tablesLuaStaged);
    report.insert("tablesLuaRedirectObserved", tablesLuaRedirectObserved);
    report.insert("tablesLuaLoadObserved", tablesLuaLoadObserved);
    report.insert("terrainAtlasProbeRequested", atlasProbeRequested);
    report.insert("terrainAtlasPages", atlasPages);
    report.insert("terrainAtlasSlots", atlasSlots);
    report.insert("textureProbeName", textureProbeName);
    report.insert("textureProbeMemberName", textureProbeMemberName);
    report.insert("runtimeAtlasNamespace", runtimeAtlasNamespace);
    report.insert("runtimeNamespaceAligned", runtimeNamespaceAligned);
    report.insert("textureResourceRegistered", textureResourceRegistered);
    report.insert("textureProbeResolved", textureProbeResolved);
    report.insert("textureRequestObservationArmed", textureRequestObservationArmed);
    report.insert("resolvedTerrainPageRequests", resolvedTerrainPageRequests);
    report.insert("allTerrainPageRequestsResolved", allTerrainPageRequestsResolved);
    report.insert("customAtlasContractValidated", customAtlasContractValidated);
    report.insert("expandedAtlasRequested", expandedAtlasRequested);
    report.insert("expandedAtlasContractValidated", expandedAtlasContractValidated);
    report.insert("drawallRequested", drawallRequested);
    report.insert("drawallAutoexecRedirected", drawallAutoexecRedirected);
    report.insert("drawallAutoexecConsumed", drawallAutoexecConsumed);
    report.insert("drawallContractValidated", drawallContractValidated);
    report.insert("playerInitializationCount", playerInitializations);
    report.insert("reportedPlayerCount", playerInitializations);
    report.insert("controlledPlayerConfirmed", controlledPlayerConfirmed);
    report.insert("playerCountEvidence", controlledPlayerConfirmed
                  ? "exactly one New Game player-record initialization observed"
                  : QString("observed %1 New Game player-record initializations")
                        .arg(playerInitializations));
    report.insert("sourceFilesUnchanged", sourceFilesUnchanged);
    report.insert("changedSources", changedSources);
    report.insert("cleanupComplete", cleanupComplete);
    report.insert("runtimeFailureObserved", runtimeFailureObserved);
    report.insert("runtimeFailureEvidence", runtimeFailureEvidence);
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
    if(report.value("terrainAtlasProbeRequested").toBool()
       && !report.value("customAtlasContractValidated").toBool())
        local.append("The custom terrain atlas runtime contract was not validated.");
    if(report.value("expandedAtlasRequested").toBool()
       && !report.value("expandedAtlasContractValidated").toBool())
        local.append("The expanded terrain atlas runtime contract was not validated.");
    if(report.value("drawallRequested").toBool()
       && !report.value("drawallContractValidated").toBool())
        local.append("The run-owned Zone18 drawall staging contract was not validated.");
    if(report.value("actualZone").toString().isEmpty())
        local.append("The actually loaded zone is missing.");
    if(!report.value("controlledPlayerConfirmed").toBool())
        local.append("Exactly one controlled player was not proven.");
    if(!report.value("sourceFilesUnchanged").toBool())
        local.append("Source-file preservation failed.");
    if(!report.value("cleanupComplete").toBool())
        local.append("Runtime cleanup is incomplete.");
    if(report.value("runtimeFailureObserved").toBool())
        local.append("A native runtime failure was observed during the staged run.");
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
    const qint64 gamePid = runtimeGameProcessId();
    return ok && threadId != 0 && gamePid > 0
        && threadBelongsToProcess(threadId, quint32(gamePid));
}

QStringList CJmvMapTestLauncher::changedStartupResources(
    const QString& stagedDatabaseSha256,
    const QString& stagedTablesLuaSourcePath,
    const QString& stagedTablesLuaSha256,
    const SJmvMapTestLaunchInput& input)
{
    QStringList changed;
    if(stagedDatabaseSha256.trimmed().isEmpty()
       || sha256(input.databaseData).compare(
              stagedDatabaseSha256, Qt::CaseInsensitive) != 0)
        changed.append(QStringLiteral("database.res"));

    const bool hadStagedLua = !stagedTablesLuaSourcePath.trimmed().isEmpty()
        || !stagedTablesLuaSha256.trimmed().isEmpty();
    const bool hasInputLua = !input.tablesLuaSourcePath.trimmed().isEmpty()
        || !input.tablesLuaData.isEmpty();
    if(hadStagedLua || hasInputLua)
    {
        const bool sourceChanged = QFileInfo(stagedTablesLuaSourcePath)
            .absoluteFilePath().compare(
                QFileInfo(input.tablesLuaSourcePath).absoluteFilePath(),
                Qt::CaseInsensitive) != 0;
        const bool payloadChanged = stagedTablesLuaSha256.trimmed().isEmpty()
            || sha256(input.tablesLuaData).compare(
                stagedTablesLuaSha256, Qt::CaseInsensitive) != 0;
        if(sourceChanged || payloadChanged)
            changed.append(QStringLiteral("tables.lua"));
    }
    return changed;
}

QStringList CJmvMapTestLauncher::startupResourceChanges(
    const SJmvMapTestLaunchInput& input) const
{
    return changedStartupResources(
        m_stagedDatabaseSha256, m_tablesLuaSourcePath,
        m_stagedTablesLuaSha256, input);
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

bool CJmvMapTestLauncher::isRuntimeFailureDialog(const QString& title,
                                                 const QString& windowClass,
                                                 const QString& bodyText)
{
    if(windowClass.compare(QStringLiteral("#32770"),
                           Qt::CaseInsensitive) != 0)
        return false;
    const QString combined = (title + QLatin1Char('\n') + bodyText).toLower();
    return title.contains(QStringLiteral("Microsoft Visual C++ Runtime Library"),
                          Qt::CaseInsensitive)
        || combined.contains(QStringLiteral("runtime error"))
        || combined.contains(QStringLiteral("abnormal program termination"))
        || (combined.contains(QStringLiteral("game.exe"))
            && combined.contains(QStringLiteral("application error")));
}

bool CJmvMapTestLauncher::prepareRunOwnedDrawallAutoexec(
    const QString& sourcePath, const QString& runRoot, bool forceDrawall,
    QJsonObject* manifest, QString* outputPath, QString* error)
{
    if(outputPath)
        outputPath->clear();
    if(!forceDrawall)
        return true;
    if(!manifest || !outputPath || sourcePath.isEmpty() || runRoot.isEmpty())
    {
        if(error) *error = "Run-owned drawall autoexec paths are incomplete.";
        return false;
    }
    QByteArray original;
    if(!readFile(sourcePath, &original, error))
        return false;
    QByteArray patched = original;
    if(!patched.isEmpty() && !patched.endsWith('\n'))
        patched.append("\r\n");
    patched.append("// EI Mapper Endurance run-owned Zone18 stress command\r\n"
                   "drawall 1\r\n");
    const QString ownedPath = QDir(runRoot).filePath("drawall/2utoexec.cfg");
    if(!QDir().mkpath(QFileInfo(ownedPath).absolutePath())
       || !atomicWrite(ownedPath, patched, error)
       || fileSha256(ownedPath).compare(sha256(patched), Qt::CaseInsensitive) != 0)
    {
        if(error && error->isEmpty())
            *error = "The run-owned drawall autoexec failed verification.";
        return false;
    }
    QJsonObject drawall;
    drawall.insert("requested", true);
    drawall.insert("command", "drawall 1");
    drawall.insert("sourcePath", QFileInfo(sourcePath).absoluteFilePath());
    drawall.insert("sourceSha256", sha256(original));
    drawall.insert("ownedPath", ownedPath);
    drawall.insert("ownedSha256", sha256(patched));
    drawall.insert("installedConfigurationModified", false);
    manifest->insert("drawallStress", drawall);
    *outputPath = ownedPath;
    return true;
}

bool CJmvMapTestLauncher::prepareGodmodeBootstrap(
    const QStringList& mapDirectories, const QString& runRoot,
    const SMapSpawnPlan& spawn, double spawnZ, bool godmode,
    const QStringList& supplementalMobNames,
    bool overrideWorldTime, double worldTimeHours,
    QJsonObject* manifest, QString* outputPath, QString* error)
{
    if(outputPath)
        outputPath->clear();
    if((!spawn.valid || !spawn.custom) && !godmode && supplementalMobNames.isEmpty()
       && !overrideWorldTime)
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
    QStringList immediateCommands;
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
        immediateCommands.append(immediatePositionCommand);
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
    if(overrideWorldTime)
    {
        if(!qIsFinite(worldTimeHours))
        {
            if(error) *error = "The selected editor time of day is invalid.";
            return false;
        }
        const double normalizedHours = std::fmod(std::fmod(worldTimeHours, 24.0) + 24.0, 24.0);
        const QString runTime = QString("RunWorldTime(%1)")
            .arg(normalizedHours, 0, 'f', 6);
        // Apply before the autorun delay when the world clock is already
        // available, then repeat after it as a deterministic fallback. Unlike
        // FixWorldTime, RunWorldTime resumes normal clock progression.
        immediateCommands.append(runTime);
        commands.append(runTime);
        worldTimeHours = normalizedHours;
    }
    if(godmode)
        commands.append("GodMode(0,3)");
    QString bootstrappedScript;
    QString bootstrapError;
    if(!injectWorldScriptBootstrap(script, commands.join("\r\n"),
                                   &bootstrappedScript, &bootstrapError,
                                   immediateCommands.join("\r\n")))
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
    bootstrap.insert("overrideWorldTime", overrideWorldTime);
    bootstrap.insert("worldTimeHours", worldTimeHours);
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
    m_liveGrpPath.clear();
    m_mapTableSourcePath.clear();
    m_stagedMapTablePath.clear();
    m_databaseSourcePath.clear();
    m_stagedDatabasePath.clear();
    m_tablesLuaSourcePath.clear();
    m_stagedTablesLuaPath.clear();
    m_livePlaceholderResourcePath.clear();
    m_drawallAutoexecPath.clear();
    m_bridgeResultPath.clear();
    m_testSavesPath.clear();
    m_bridgeSha256.clear();
    m_liveMprSha256.clear();
    m_liveMobSha256.clear();
    m_liveGrpSha256.clear();
    m_mapTableSourceSha256.clear();
    m_stagedMapTableSha256.clear();
    m_databaseSourceSha256.clear();
    m_stagedDatabaseSha256.clear();
    m_tablesLuaSourceSha256.clear();
    m_stagedTablesLuaSha256.clear();
    m_livePlaceholderResourceSha256.clear();
    m_sourceTerrainTextureResourceSha256.clear();
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
    m_sourceTerrainAtlasPages = 0;
    m_sourceTerrainAtlasSlots = 0;
    m_hotReloadResultPolls = 0;
    m_baseProbe = false;
    m_usesRuntimeHost = false;
    m_manifest = QJsonObject();
    m_cleanupComplete = false;
    m_stopRequested = false;
    m_processCrashObserved = false;
    m_runtimeFailureObserved = false;
    m_terrainAtlasRestartPending = false;
    m_pendingTerrainAtlasRestartInput = SJmvMapTestLaunchInput();
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
            << QDir(m_runRoot).filePath("runtime-host.log")
            << (m_baseProbe ? "base" : "jmv"));
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
    environment.insert("EI_MAPPER_ENDURANCE_SOURCE_ZONE",
                       input.syntheticMapIdentity ? m_temporaryMapName
                                                  : input.mapEntry.zoneId);
    environment.insert("EI_MAPPER_ENDURANCE_BOOTSTRAP_ZONE", input.mapEntry.zoneId);
    environment.insert("EI_MAPPER_ENDURANCE_BOOTSTRAP_MPR", input.mapEntry.mprName);
    environment.insert("EI_MAPPER_ENDURANCE_BOOTSTRAP_MOB", input.mapEntry.mobName);
    environment.insert("EI_MAPPER_ENDURANCE_SYNTHETIC_MAP",
                       input.syntheticMapIdentity ? "1" : "0");
    environment.insert("EI_MAPPER_ENDURANCE_MAP_SIZE_X", QString::number(input.mapSizeX));
    environment.insert("EI_MAPPER_ENDURANCE_MAP_SIZE_Y", QString::number(input.mapSizeY));
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
    environment.insert("EI_MAPPER_ENDURANCE_GRAPHLESS",
                       input.allowMissingAiGraph ? "1" : "0");
    if(!m_mapTableSourcePath.isEmpty())
    {
        environment.insert("EI_MAPPER_ENDURANCE_MAP_TABLE_SOURCE",
                           m_mapTableSourcePath);
        environment.insert("EI_MAPPER_ENDURANCE_MAP_TABLE_STAGE",
                           m_stagedMapTablePath);
    }
    if(!m_databaseSourcePath.isEmpty())
    {
        environment.insert("EI_MAPPER_ENDURANCE_DATABASE_SOURCE",
                           m_databaseSourcePath);
        environment.insert("EI_MAPPER_ENDURANCE_DATABASE_STAGE",
                           m_stagedDatabasePath);
    }
    if(!m_tablesLuaSourcePath.isEmpty())
    {
        environment.insert("EI_MAPPER_ENDURANCE_TABLES_LUA_SOURCE",m_tablesLuaSourcePath);
        environment.insert("EI_MAPPER_ENDURANCE_TABLES_LUA_STAGE",m_stagedTablesLuaPath);
        const QJsonObject luaJournal=m_manifest.value("tablesLua").toObject();
        environment.insert("EI_MAPPER_ENDURANCE_TABLES_LUA_PAYLOAD",m_stagedTablesLuaPath);
        environment.insert("EI_MAPPER_ENDURANCE_TABLES_LUA_EVIDENCE",
                           luaJournal.value("loadEvidencePath").toString());
        environment.insert("EI_MAPPER_ENDURANCE_TABLES_LUA_SHA256",
                           m_stagedTablesLuaSha256);
        environment.insert("LUA_PATH",
                           QDir(m_runRoot).filePath("lua-override/?.lua")+";;");
    }
    if(!m_livePlaceholderResourcePath.isEmpty())
    {
        environment.insert("EI_MAPPER_ENDURANCE_TEMP_TEXTURE_RES",
                           m_livePlaceholderResourcePath);
        const QString textureProbeMemberName = m_manifest.value(
            "terrainAtlasRuntimeProbe").toObject().value(
                "textureProbeMemberName").toString();
        if(!textureProbeMemberName.isEmpty())
            environment.insert("EI_MAPPER_ENDURANCE_TEXTURE_PROBE_NAME",
                               textureProbeMemberName);
        environment.insert("EI_MAPPER_ENDURANCE_TEXTURE_PAGE_COUNT",
                           QString::number(input.expectedTerrainAtlasPages));
    }
    const QJsonObject drawall = m_manifest.value("drawallStress").toObject();
    if(!m_drawallAutoexecPath.isEmpty())
    {
        environment.insert("EI_MAPPER_ENDURANCE_DRAWALL_AUTOEXEC_SOURCE",
                           drawall.value("sourcePath").toString());
        environment.insert("EI_MAPPER_ENDURANCE_DRAWALL_AUTOEXEC",
                           m_drawallAutoexecPath);
        environment.insert("EI_MAPPER_ENDURANCE_DRAWALL_AUTOEXEC_SHA256",
                           drawall.value("ownedSha256").toString());
    }
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
    m_sourceTerrainTextureResourceSha256 =
        input.terrainTextureResourceData.isEmpty()
            ? QString() : sha256(input.terrainTextureResourceData);
    m_sourceTerrainAtlasPages = input.expectedTerrainAtlasPages;
    m_sourceTerrainAtlasSlots = input.expectedTerrainAtlasSlots;
    if((!isJmv && !isBase) || !input.profile.valid || !input.profile.runtimeStackValidated)
    {
        if(error) *error = "The active runtime stack is incomplete or does not match an exact manifest.";
        return false;
    }
    if(!input.spawn.valid || input.mprData.isEmpty() || input.mobData.isEmpty()
       || input.databaseSourcePath.trimmed().isEmpty() || input.databaseData.isEmpty())
    {
        if(error) *error = "The loaded map pair, active database, or its unobstructed travel-zone spawn is incomplete.";
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
    m_currentZoneId = input.syntheticMapIdentity
        ? m_temporaryMapName : input.mapEntry.zoneId;
    m_bridgeLivePath = QDir(m_runRoot).filePath("EIMapperEnduranceRuntimeBridge.dll");
    m_runtimeHostPath = QDir(QCoreApplication::applicationDirPath())
        .filePath("runtime/EIMapperEnduranceRuntimeHost.exe");
    m_gamePidPath = QDir(m_runRoot).filePath("game.pid");
    if(!QFileInfo(m_runtimeHostPath).isFile())
    {
        if(error) *error = "The packaged x86 Endurance runtime host is missing.";
        return false;
    }
    if(input.syntheticMapIdentity
       && (input.mapSizeX <= 0 || input.mapSizeY <= 0
           || input.mapSizeX > 1024 || input.mapSizeY > 1024))
    {
        if(error) *error = "Synthetic-map dimensions derived from the loaded MPR are invalid.";
        return false;
    }
    if(input.allowMissingAiGraph)
    {
        QFile bridgeFile(m_bridgeSourcePath);
        if(!bridgeFile.open(QIODevice::ReadOnly)
           || !bridgeSupportsGraphlessLaunch(bridgeFile.readAll()))
        {
            if(error)
                *error = "The packaged runtime bridge predates graphless Test Current Map support. Rebuild or reinstall this Endurance package; launching with the stale bridge could crash game.exe.";
            return false;
        }
    }
    if(input.syntheticMapIdentity)
    {
        QFile bridgeFile(m_bridgeSourcePath);
        if(!bridgeFile.open(QIODevice::ReadOnly)
           || !bridgeSupportsSyntheticMapIdentity(bridgeFile.readAll()))
        {
            if(error)
                *error = "The packaged runtime bridge predates synthetic-map Test Current Map support. Rebuild or reinstall this Endurance package.";
            return false;
        }
    }
    QString starterStoredSelection;
    QString starterRegistryAddonPath;
    QString selectedModConfig;
    if(isJmv)
    {
        selectedModConfig = findCaseInsensitiveFile(
            QStringList() << input.profile.modRoot, "config.reg");
        if(selectedModConfig.isEmpty())
        {
            if(error) *error = "The selected JMV mod has no readable Starter config.reg.";
            return false;
        }
        const QString starterRoot = QDir(input.profile.gameRoot).filePath("Starter");
        QSettings starterConfig(QDir(starterRoot).filePath("starter.config"),
                                QSettings::IniFormat);
        starterStoredSelection = starterConfig.value("Settings/ModSel").toString();
#ifdef Q_OS_WIN
        QSettings starterRegistry(
            "HKEY_CURRENT_USER\\Software\\Gipat.Ru\\EI_Starter\\Settings",
            QSettings::NativeFormat);
        starterRegistryAddonPath = starterRegistry.value("AddonPath").toString();
#endif
        const QString selectedModRoot = normalized(input.profile.modRoot);
        const QString persistedModRoot = starterRegistryAddonPath.isEmpty()
            ? QString() : normalized(starterRegistryAddonPath);
        if(persistedModRoot.isEmpty()
           || persistedModRoot.compare(selectedModRoot, Qt::CaseInsensitive) != 0)
        {
            if(error)
            {
                *error = QString(
                    "Starter is not currently configured for the selected Endurance mod.\n\n"
                    "Selected profile mod:\n%1\n\n"
                    "Starter AddonPath:\n%2\n\n"
                    "Select the intended mod in Starter, then retry Test Current Map. "
                    "Endurance will not start with a different cached mod because that can mix incompatible resources.")
                    .arg(QDir::toNativeSeparators(selectedModRoot),
                         starterRegistryAddonPath.isEmpty()
                             ? QString("<not configured>")
                             : QDir::toNativeSeparators(persistedModRoot));
            }
            return false;
        }
#ifdef Q_OS_WIN
        const wchar_t* pathSettings =
            L"Software\\Gipat.Ru\\EI_Starter\\EvilIslands\\Path Settings";
        const QString workPath = registry32String(
            HKEY_CURRENT_USER, pathSettings, L"Work path");
        const QString resourcesPath = registry32String(
            HKEY_CURRENT_USER, pathSettings, L"Resources path");
        const QString cdRomPath = registry32String(
            HKEY_CURRENT_USER, pathSettings, L"CD-ROM Path");
        if(!validateJmvGameRootPaths(input.profile.gameRoot, workPath,
                                     resourcesPath, cdRomPath, error))
            return false;
#endif
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
    SMapGraphSource graphSource;
    if(!resolveMapGraphSource(input.mobData, input.sourceMpr,
                              input.mapEntry.mprName,
                              input.profile.mapDirectories,
                              &graphSource, error))
        return false;
    if(!input.generatedAiGraphData.isEmpty())
    {
        QString graphError;
        if(!validateExternalGraph(input.generatedAiGraphData, &graphError))
        {
            if(error) *error = "The generated test AI graph is invalid: " + graphError;
            return false;
        }
        if(graphSource.usesExternalGraph())
            graphSource.shadowedExternalPath = graphSource.externalPath;
        graphSource.embedded = false;
        graphSource.missing = false;
        graphSource.externalPath = "<Endurance generated graph>";
        graphSource.externalData = input.generatedAiGraphData;
        graphSource.sha256 = sha256(input.generatedAiGraphData);
    }
    if(graphSource.missing && !input.allowMissingAiGraph)
    {
        if(error)
            *error = "The primary MOB has no embedded AI_GRAPH and no matching external .grp was found. Explicit confirmation is required before starting a graphless test.";
        return false;
    }
    if(graphSource.usesExternalGraph())
        m_liveGrpPath = QFileInfo(m_liveMprPath).dir().filePath(
            m_temporaryMapName + ".grp");
    if(QFileInfo::exists(m_liveMprPath) || QFileInfo::exists(m_liveMobPath)
       || (!m_liveGrpPath.isEmpty() && QFileInfo::exists(m_liveGrpPath)))
    {
        if(error) *error = "The randomly generated Endurance test-map name already exists; retry the launch.";
        return false;
    }
    if(!QDir().mkpath(m_runRoot))
    {
        if(error) *error = "Could not create the Endurance test-run directory.";
        return false;
    }

    m_databaseSourcePath = QFileInfo(input.databaseSourcePath).absoluteFilePath();
    QByteArray sourceDatabase;
    if(!QFileInfo(m_databaseSourcePath).isFile()
       || QFileInfo(m_databaseSourcePath).fileName().compare(
              "database.res", Qt::CaseInsensitive) != 0
       || !readFile(m_databaseSourcePath, &sourceDatabase, error))
    {
        if(error && error->isEmpty())
            *error = "The active database.res source could not be resolved for owned test staging.";
        return false;
    }
    const QString databaseDirectory = QDir(m_runRoot).filePath("database");
    m_stagedDatabasePath = QDir(databaseDirectory).filePath("database.res");
    m_databaseSourceSha256 = sha256(sourceDatabase);
    m_stagedDatabaseSha256 = sha256(input.databaseData);
    if(!QDir().mkpath(databaseDirectory)
       || !atomicWrite(m_stagedDatabasePath, input.databaseData, error)
       || fileSha256(m_stagedDatabasePath).compare(
              m_stagedDatabaseSha256, Qt::CaseInsensitive) != 0)
    {
        if(error && error->isEmpty())
            *error = "The staged active database failed its post-write SHA-256 check.";
        return false;
    }
    QJsonObject databaseJournal;
    databaseJournal.insert("policy", "read-only-exact-source-path-redirection");
    databaseJournal.insert("sourcePath", m_databaseSourcePath);
    databaseJournal.insert("sourceSha256", m_databaseSourceSha256);
    databaseJournal.insert("stagedPath", m_stagedDatabasePath);
    databaseJournal.insert("stagedSha256", m_stagedDatabaseSha256);
    databaseJournal.insert("installedArchiveModified", false);
    m_manifest.insert("database", databaseJournal);

    if(isJmv)
    {
        m_tablesLuaSourcePath=QFileInfo(input.tablesLuaSourcePath).absoluteFilePath();QByteArray sourceLua;
        if(!QFileInfo(m_tablesLuaSourcePath).isFile()||QFileInfo(m_tablesLuaSourcePath).fileName().compare("tables.lua",Qt::CaseInsensitive)!=0||!readFile(m_tablesLuaSourcePath,&sourceLua,error))
        {if(error&&error->isEmpty())*error="The active JMV tables.lua source could not be resolved for owned test staging.";return false;}
        m_stagedTablesLuaPath=QDir(databaseDirectory).filePath("tables.lua");m_tablesLuaSourceSha256=sha256(sourceLua);m_stagedTablesLuaSha256=sha256(input.tablesLuaData);
        if(input.tablesLuaData.isEmpty()||!atomicWrite(m_stagedTablesLuaPath,input.tablesLuaData,error)||fileSha256(m_stagedTablesLuaPath).compare(m_stagedTablesLuaSha256,Qt::CaseInsensitive)!=0)
        {if(error&&error->isEmpty())*error="The staged active tables.lua failed its post-write SHA-256 check.";return false;}
        const QString overrideDirectory=QDir(m_runRoot).filePath("lua-override");
        const QString overridePath=QDir(overrideDirectory).filePath("tables.lua");
        const QString evidencePath=QDir(m_runRoot).filePath("tables-lua-loaded.txt");
        const QByteArray wrapper=
            "local payload = assert(os.getenv('EI_MAPPER_ENDURANCE_TABLES_LUA_PAYLOAD'))\n"
            "local chunk = assert(loadfile(payload))\n"
            "local result = chunk()\n"
            "local evidence = assert(os.getenv('EI_MAPPER_ENDURANCE_TABLES_LUA_EVIDENCE'))\n"
            "local marker = assert(io.open(evidence, 'wb'))\n"
            "marker:write(assert(os.getenv('EI_MAPPER_ENDURANCE_TABLES_LUA_SHA256')))\n"
            "marker:close()\n"
            "return result\n";
        if(!QDir().mkpath(overrideDirectory)||!atomicWrite(overridePath,wrapper,error))
        {if(error&&error->isEmpty())*error="The run-owned tables.lua loader could not be staged.";return false;}
        QJsonObject luaJournal;luaJournal.insert("policy","pre-initialization-lua-module-override");luaJournal.insert("sourcePath",m_tablesLuaSourcePath);luaJournal.insert("sourceSha256",m_tablesLuaSourceSha256);luaJournal.insert("stagedPath",m_stagedTablesLuaPath);luaJournal.insert("stagedSha256",m_stagedTablesLuaSha256);luaJournal.insert("overridePath",overridePath);luaJournal.insert("loadEvidencePath",evidencePath);luaJournal.insert("installedLuaModified",false);m_manifest.insert("tablesLua",luaJournal);
    }

    // Journal every live artifact before creating it. A crash at any later
    // staging point can therefore be recovered without guessing ownership.
    QByteArray runtimeMprData = input.mprData;
    m_liveMobSha256 = sha256(input.mobData);
    m_liveGrpSha256 = graphSource.sha256;
    QByteArray placeholderArchive;
    QStringList placeholderErrors;
    const bool hasTemporaryMinimap = !input.temporaryMinimapMmp.isEmpty();
    const QString temporaryMinimapRuntimeName = hasTemporaryMinimap
        ? temporaryMinimapRuntimeTextureName(input.temporaryMinimapMmp, m_runId)
        : QString();
    int stagedTextureMmpMembers = 0;
    QString terrainTextureProbeName = syntheticMapPlaceholderTextureName();
    QString terrainTextureProbeMemberName = terrainTextureProbeName + ".mmp";
    if(!input.terrainTextureResourceData.isEmpty())
    {
        // The shipped terrain loader derives MP/SEC and page names from the
        // staged MPR filename. Rebase both archives to the same random owned
        // identity; merely registering pages under the finalized source
        // namespace loads geometry with untextured terrain.
        QStringList runtimeMmpMembers;
        if(!rebaseRuntimeTerrainAtlas(input.mprData,
                                      input.terrainTextureResourceData,
                                      m_temporaryMapName,
                                      input.expectedTerrainAtlasPages,
                                      &runtimeMprData,
                                      &placeholderArchive,
                                      &runtimeMmpMembers,
                                      error))
            return false;
        stagedTextureMmpMembers = runtimeMmpMembers.size();
        terrainTextureProbeMemberName = runtimeMmpMembers.constLast();
        terrainTextureProbeName = QFileInfo(
            terrainTextureProbeMemberName).completeBaseName();
    }
    else
    {
        const bool textureArchiveBuilt = hasTemporaryMinimap
            ? buildSyntheticMapTextureArchive(
                temporaryMinimapRuntimeName, input.temporaryMinimapMmp,
                &placeholderArchive, &placeholderErrors)
            : buildSyntheticMapPlaceholderArchive(
                &placeholderArchive, &placeholderErrors);
        if(!textureArchiveBuilt)
        {
            if(error) *error =
                "Could not build the run-owned placeholder texture archive:\n"
                + placeholderErrors.join('\n');
            return false;
        }
    }
    m_liveMprSha256 = sha256(runtimeMprData);
    const QString resourcesDirectory = QDir(input.profile.gameRoot).filePath("Res");
    m_livePlaceholderResourcePath = QDir(resourcesDirectory).filePath(
        "EIMapperEndurance-" + m_runId + ".res");
    m_livePlaceholderResourceSha256 = sha256(placeholderArchive);
    if(!QFileInfo(resourcesDirectory).isDir()
       || QFileInfo::exists(m_livePlaceholderResourcePath))
    {
        if(error) *error = "The run-owned placeholder RES path is unavailable or already exists.";
        return false;
    }
    QJsonObject liveJournal;
    liveJournal.insert("mprPath", m_liveMprPath);
    liveJournal.insert("mprSha256", m_liveMprSha256);
    liveJournal.insert("mobPath", m_liveMobPath);
    liveJournal.insert("mobSha256", m_liveMobSha256);
    if(graphSource.usesExternalGraph())
    {
        liveJournal.insert("grpPath", m_liveGrpPath);
        liveJournal.insert("grpSha256", m_liveGrpSha256);
    }
    liveJournal.insert("placeholderResPath", m_livePlaceholderResourcePath);
    liveJournal.insert("placeholderResSha256", m_livePlaceholderResourceSha256);
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
    if(isJmv)
    {
        QJsonObject starterSelection;
        starterSelection.insert("policy", "owned-process-addon-path-override");
        starterSelection.insert("targetModRoot", QDir::cleanPath(input.profile.modRoot));
        starterSelection.insert("targetConfig", selectedModConfig);
        starterSelection.insert("persistedModSelection", starterStoredSelection);
        starterSelection.insert("persistedAddonPath", starterRegistryAddonPath);
        starterSelection.insert("persistentConfigurationModified", false);
        m_manifest.insert("starterModSelection", starterSelection);
    }
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
    CValidationReport report;
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
    const QString stagedGrpPath = graphSource.usesExternalGraph()
        ? QDir(payloadDirectory).filePath(m_temporaryMapName + ".grp")
        : QString();
    if(!atomicWrite(stagedMprPath, runtimeMprData, error) ||
       !atomicWrite(stagedMobPath, stagedMob, error))
        return false;
    if(graphSource.usesExternalGraph()
       && !atomicWrite(stagedGrpPath, graphSource.externalData, error))
        return false;
    CMprDocument mprDocument;
    if(!mprDocument.load(QFileInfo(stagedMprPath), &report))
    {
        if(error) *error = "The staged MPR failed validation:\n" + report.toText();
        return false;
    }
    if(!input.terrainTextureResourceData.isEmpty())
    {
        const int pages = int(mprDocument.mapInfo().textureCount);
        const int activeSlots = int(mprDocument.mapInfo().tileCount);
        const QString runtimeNamespace = mprDocument.mapInfo().innerNamespace;
        if(runtimeNamespace.compare(m_temporaryMapName,
                                    Qt::CaseInsensitive) != 0
           || pages != input.expectedTerrainAtlasPages
           || activeSlots != input.expectedTerrainAtlasSlots
           || pages != stagedTextureMmpMembers)
        {
            if(error) *error = QString(
                "The staged MPR/RES terrain atlas contract differs: "
                "namespace=%1 expected namespace=%2, MPR pages=%3 slots=%4, "
                "RES pages=%5, expected pages=%6 slots=%7.")
                .arg(runtimeNamespace, m_temporaryMapName)
                .arg(pages).arg(activeSlots).arg(stagedTextureMmpMembers)
                .arg(input.expectedTerrainAtlasPages)
                .arg(input.expectedTerrainAtlasSlots);
            return false;
        }
        QJsonObject atlasProbe;
        atlasProbe.insert("mode", "run-owned-expanded-terrain-atlas");
        atlasProbe.insert("texturePages", pages);
        atlasProbe.insert("activeSlots", activeSlots);
        atlasProbe.insert("resourceMmpMembers", stagedTextureMmpMembers);
        atlasProbe.insert("resourceSha256", m_livePlaceholderResourceSha256);
        atlasProbe.insert("textureProbeName", terrainTextureProbeName);
        atlasProbe.insert("textureProbeMemberName", terrainTextureProbeMemberName);
        atlasProbe.insert("runtimeNamespace", runtimeNamespace);
        atlasProbe.insert("exceedsLegacyEightPages", pages > 8);
        atlasProbe.insert("installedArchivesModified", false);
        m_manifest.insert("terrainAtlasRuntimeProbe", atlasProbe);
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
    if(graphSource.usesExternalGraph())
    {
        liveFiles.insert("grpPath", m_liveGrpPath);
        liveFiles.insert("grpSha256", m_liveGrpSha256);
    }
    liveFiles.insert("placeholderResPath", m_livePlaceholderResourcePath);
    liveFiles.insert("placeholderResSha256", m_livePlaceholderResourceSha256);
    m_manifest.insert("liveFiles", liveFiles);
    if(!atomicWrite(m_liveMprPath, runtimeMprData, error) ||
       !atomicWrite(m_liveMobPath, stagedMob, error) ||
       !atomicWrite(m_livePlaceholderResourcePath, placeholderArchive, error))
        return false;
    if(graphSource.usesExternalGraph()
       && !atomicWrite(m_liveGrpPath, graphSource.externalData, error))
        return false;
    if(fileSha256(m_liveMprPath) != m_liveMprSha256
       || fileSha256(m_liveMobPath) != m_liveMobSha256
       || fileSha256(m_livePlaceholderResourcePath) != m_livePlaceholderResourceSha256)
    {
        if(error) *error = "A temporary live map/resource copy failed its post-write SHA-256 check.";
        return false;
    }

    // Add one private, randomly named map record before game.exe begins loading
    // texts.res. Normal mapped launches ignore it; mapless launches and later
    // mapless hot reloads use it without renaming an already constructed game
    // object. The installed archive remains byte-identical.
    if(isJmv)
    {
        m_mapTableSourcePath = QFileInfo(input.mapEntry.sourceArchive).absoluteFilePath();
        QByteArray sourceArchive;
        if(m_mapTableSourcePath.isEmpty()
           || !QFileInfo(m_mapTableSourcePath).isFile()
           || !readFile(m_mapTableSourcePath, &sourceArchive, error))
        {
            if(error && error->isEmpty())
                *error = "The active map.txt source archive could not be resolved for safe test staging.";
            return false;
        }
        QByteArray stagedArchive;
        QStringList stagingErrors;
        if(!buildSyntheticMapTableArchive(sourceArchive, m_mapTableSourcePath,
                                          m_temporaryMapName,
                                          input.mapEntry.allodId,
                                          m_temporaryMapName,
                                          input.mapSizeX,
                                          input.mapSizeY,
                                          input.spawn.position,
                                          &stagedArchive, &stagingErrors,
                                          terrainTextureProbeName))
        {
            if(error) *error = "Could not stage the private test map table:\n"
                + stagingErrors.join('\n');
            return false;
        }
        if(hasTemporaryMinimap)
        {
            QStringList minimapErrors;
            const QString previewZone = input.syntheticMapIdentity
                ? m_temporaryMapName : input.mapEntry.zoneId;
            QByteArray previewArchive;
            if(!stageTemporaryMinimapReference(
                   stagedArchive, m_mapTableSourcePath + " [minimap preview]",
                   previewZone, temporaryMinimapRuntimeName,
                   &previewArchive, &minimapErrors))
            {
                if(error) *error = "Could not stage the private minimap reference: "
                    + minimapErrors.join(" | ");
                return false;
            }
            stagedArchive = previewArchive;
        }
        const QString mapTableDirectory = QDir(m_runRoot).filePath("map-table");
        m_stagedMapTablePath = QDir(mapTableDirectory).filePath("texts.res");
        if(!QDir().mkpath(mapTableDirectory)
           || !atomicWrite(m_stagedMapTablePath, stagedArchive, error))
            return false;
        m_mapTableSourceSha256 = sha256(sourceArchive);
        m_stagedMapTableSha256 = sha256(stagedArchive);
        if(fileSha256(m_stagedMapTablePath) != m_stagedMapTableSha256)
        {
            if(error) *error = "The private staged texts.res failed its post-write hash check.";
            return false;
        }
        QJsonObject mapTableJournal;
        mapTableJournal.insert("policy", "read-only-exact-path-redirection");
        mapTableJournal.insert("sourcePath", m_mapTableSourcePath);
        mapTableJournal.insert("sourceSha256", m_mapTableSourceSha256);
        mapTableJournal.insert("stagedPath", m_stagedMapTablePath);
        mapTableJournal.insert("stagedSha256", m_stagedMapTableSha256);
        mapTableJournal.insert("syntheticZone", m_temporaryMapName);
        mapTableJournal.insert("installedArchiveModified", false);
        m_manifest.insert("syntheticMapTable", mapTableJournal);

    }
    if(graphSource.usesExternalGraph()
       && fileSha256(m_liveGrpPath).compare(m_liveGrpSha256, Qt::CaseInsensitive) != 0)
    {
        if(error) *error = "The temporary live external AI graph failed its post-write SHA-256 check.";
        return false;
    }

    SMapTestLaunchRequest request;
    request.runId = m_runId;
    request.profileId = input.profile.profileId;
    request.runtimeManifestId = input.profile.runtimeManifestId;
    request.runtimeComponentHashes = runtimeHashes(input.profile);
    request.stagedMprPath = stagedMprPath;
    request.internalMapName = input.syntheticMapIdentity
        ? m_temporaryMapName : input.mapEntry.mprName;
    request.orderedStagedMobPaths << stagedMobPath;
    request.orderedStagedMobPaths << stagedSupplementalPaths;
    request.activeMobPath = stagedMobPath;
    request.sourceSha256.insert(input.sourceMpr.absoluteFilePath(),
                                fileSha256(input.sourceMpr.absoluteFilePath()));
    request.sourceSha256.insert(input.sourceMob.absoluteFilePath(),
                                fileSha256(input.sourceMob.absoluteFilePath()));
    if(!input.terrainTextureResourceData.isEmpty()
       && input.sourceTerrainTextureResource.isFile())
        request.sourceSha256.insert(
            input.sourceTerrainTextureResource.absoluteFilePath(),
            fileSha256(input.sourceTerrainTextureResource.absoluteFilePath()));
    // A generated graph is an owned staged payload, not a source file. Do not
    // put its diagnostic pseudo-path into the source-integrity inventory.
    if(graphSource.usesExternalGraph() && QFileInfo(graphSource.externalPath).isFile())
    {
        request.sourceSha256.insert(graphSource.externalPath, graphSource.sha256);
        request.stagedSha256.insert(QFileInfo(stagedGrpPath).fileName(),
                                    graphSource.sha256);
    }
    else if(graphSource.usesExternalGraph())
    {
        request.stagedSha256.insert(QFileInfo(stagedGrpPath).fileName(),
                                    graphSource.sha256);
    }
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
    request.sessionSettings.insert("sourceZone", m_currentZoneId);
    request.sessionSettings.insert("runtimeZone", m_currentZoneId);
    request.sessionSettings.insert("syntheticMapIdentity", input.syntheticMapIdentity);
    request.sessionSettings.insert("mapSizeX", input.mapSizeX);
    request.sessionSettings.insert("mapSizeY", input.mapSizeY);
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
    request.sessionSettings.insert("forceDrawall", input.forceDrawall);
    request.sessionSettings.insert("overrideWorldTime", input.overrideWorldTime);
    request.sessionSettings.insert("worldTimeHours", input.worldTimeHours);
    request.sessionSettings.insert("borrowedMapIdentity", input.borrowedMapIdentity);
    request.sessionSettings.insert("baseProbeLevel", input.baseProbeLevel);
    request.sessionSettings.insert("clearance", input.spawn.clearance);
    request.sessionSettings.insert("unobstructed", input.spawn.unobstructed);
    if(isJmv && !prepareRunOwnedDrawallAutoexec(
           QDir(m_engineDirectory).filePath("2utoexec.cfg"), m_runRoot,
           input.forceDrawall, &m_manifest, &m_drawallAutoexecPath, error))
        return false;
    if(!m_drawallAutoexecPath.isEmpty())
    {
        const QJsonObject drawall = m_manifest.value("drawallStress").toObject();
        request.sourceSha256.insert(drawall.value("sourcePath").toString(),
                                    drawall.value("sourceSha256").toString());
        appendRunLog(QString("Staged run-owned drawall autoexec path=%1 command=drawall 1 installedModified=false")
                     .arg(QDir::toNativeSeparators(m_drawallAutoexecPath)));
    }
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
    QJsonObject graphManifest;
    graphManifest.insert("source", graphSource.missing ? "missing-allowed"
                                                        : (graphSource.embedded ? "embedded-mob" : "external-grp"));
    if(graphSource.usesExternalGraph())
    {
        graphManifest.insert("sourcePath", graphSource.externalPath);
        graphManifest.insert("sourceSha256", graphSource.sha256);
        graphManifest.insert("payloadPath", stagedGrpPath);
        graphManifest.insert("livePath", m_liveGrpPath);
    }
    if(!graphSource.shadowedExternalPath.isEmpty())
        graphManifest.insert("shadowedExternalPath", graphSource.shadowedExternalPath);
    m_manifest.insert("aiGraph", graphManifest);
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
                                         supplementalMobNames,
                                         input.overrideWorldTime, input.worldTimeHours,
                                         &m_manifest,
                                         &godmodeBootstrapPath, error))
        return false;
    if(!godmodeBootstrapPath.isEmpty())
        m_manifest.insert("godmodeBootstrapPath", godmodeBootstrapPath);
    appendRunLog(QString("Prepared generation-scoped isolated saves generation=0 path=%1; live saves remain untouched")
                 .arg(m_testSavesPath));
    appendRunLog(QString("Staged active database source=%1 sourceSha256=%2 staged=%3 stagedSha256=%4 installedModified=false")
                 .arg(QDir::toNativeSeparators(m_databaseSourcePath),
                      m_databaseSourceSha256,
                      QDir::toNativeSeparators(m_stagedDatabasePath),
                      m_stagedDatabaseSha256));
    if(isJmv)
        appendRunLog(QString("Starter mod target=%1 persistedSelection=%2 persistedAddonPath=%3 policy=owned-process-override persistentModified=false")
                     .arg(QDir::cleanPath(input.profile.modRoot), starterStoredSelection,
                          starterRegistryAddonPath));
    if(isJmv)
        appendRunLog(QString("Staged private map table source=%1 sourceSha256=%2 staged=%3 stagedSha256=%4 syntheticZone=%5 installedModified=false")
                     .arg(QDir::toNativeSeparators(m_mapTableSourcePath),
                          m_mapTableSourceSha256,
                          QDir::toNativeSeparators(m_stagedMapTablePath),
                          m_stagedMapTableSha256,
                          m_temporaryMapName));
    appendRunLog(QString("Staged standalone temporary texture RES path=%1 sha256=%2 texture=%3 source=%4 registration=shared-owned-process-bridge installedArchivesModified=false")
                 .arg(QDir::toNativeSeparators(m_livePlaceholderResourcePath),
                      m_livePlaceholderResourceSha256,
                      hasTemporaryMinimap ? temporaryMinimapRuntimeName
                                          : syntheticMapPlaceholderTextureName(),
                      hasTemporaryMinimap ? "reviewed-minimap" : "neutral-checker"));
    appendRunLog(QString("Staged standalone temporary texture RES path=%1 sha256=%2 pages=%3 slots=%4 mode=%5 registration=shared-owned-process-bridge installedArchivesModified=false")
                 .arg(QDir::toNativeSeparators(m_livePlaceholderResourcePath),
                      m_livePlaceholderResourceSha256)
                 .arg(input.expectedTerrainAtlasPages)
                 .arg(input.expectedTerrainAtlasSlots)
                 .arg(input.terrainTextureResourceData.isEmpty()
                      ? QStringLiteral("placeholder")
                      : QStringLiteral("expanded-terrain-atlas")));
    appendRunLog(QString("Staged temporary map=%1 exit=%2 spawn=(%3,%4) clearance=%5")
                 .arg(m_temporaryMapName).arg(input.spawn.exitId)
                 .arg(x, y).arg(input.spawn.clearance, 0, 'f', 3));
    if(graphSource.usesExternalGraph())
        appendRunLog(QString("Staged external AI graph source=%1 live=%2 sha256=%3")
                     .arg(QDir::toNativeSeparators(graphSource.externalPath),
                          QDir::toNativeSeparators(m_liveGrpPath), graphSource.sha256));
    else if(!graphSource.shadowedExternalPath.isEmpty())
        appendRunLog(QString("Using embedded AI_GRAPH; external graph is shadowed: %1")
                     .arg(QDir::toNativeSeparators(graphSource.shadowedExternalPath)));
    else if(graphSource.missing)
        appendRunLog("WARNING: Test Current Map is starting without an embedded AI_GRAPH or external .grp by explicit user confirmation");
    else
        appendRunLog("Using embedded AI_GRAPH from the primary MOB");
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
    if(QFileInfo(input.databaseSourcePath).absoluteFilePath().compare(
           m_databaseSourcePath, Qt::CaseInsensitive) != 0)
    {
        if(error) *error = "The active database source differs from the running test profile.";
        return false;
    }
    const QStringList startupChanges = startupResourceChanges(input);
    if(!startupChanges.isEmpty())
    {
        if(error)
            *error = QCoreApplication::translate(
                "CJmvMapTestLauncher",
                "Pending %1 changes require a fresh Test Current Map process because database and JMV Lua state are initialized once at process startup.")
                .arg(startupChanges.join(QStringLiteral(" + ")));
        return false;
    }
    if(!input.profile.valid || !input.profile.runtimeStackValidated
       || input.mprData.isEmpty() || input.mobData.isEmpty()
       || input.databaseData.isEmpty() || !input.spawn.valid)
    {
        if(error) *error = "The hot-reload profile, map pair, or selected entrance is incomplete.";
        return false;
    }

    SMapGraphSource graphSource;
    if(!resolveMapGraphSource(input.mobData, input.sourceMpr,
                              input.mapEntry.mprName,
                              input.profile.mapDirectories,
                              &graphSource, error))
        return false;
    if(!input.generatedAiGraphData.isEmpty())
    {
        QString graphError;
        if(!validateExternalGraph(input.generatedAiGraphData, &graphError))
        {
            if(error) *error = "The generated hot-reload AI graph is invalid: " + graphError;
            return false;
        }
        if(graphSource.usesExternalGraph())
            graphSource.shadowedExternalPath = graphSource.externalPath;
        graphSource.embedded = false;
        graphSource.missing = false;
        graphSource.externalPath = "<Endurance generated graph>";
        graphSource.externalData = input.generatedAiGraphData;
        graphSource.sha256 = sha256(input.generatedAiGraphData);
    }
    if(graphSource.missing && !input.allowMissingAiGraph)
    {
        if(error)
            *error = "The primary MOB has no embedded AI_GRAPH and no matching external .grp was found. Explicit confirmation is required before hot reloading a graphless map.";
        return false;
    }

    const QString candidateAtlasSha256 =
        input.terrainTextureResourceData.isEmpty()
            ? QString() : sha256(input.terrainTextureResourceData);
    if(terrainAtlasHotReloadRequiresRestart(
           m_sourceTerrainTextureResourceSha256,
           m_sourceTerrainAtlasPages, m_sourceTerrainAtlasSlots,
           candidateAtlasSha256, input.expectedTerrainAtlasPages,
           input.expectedTerrainAtlasSlots))
        return queueTerrainAtlasRestart(input, candidateAtlasSha256, error);

    const quint64 sequence = m_hotReloadSequence + 1;
    const quint64 generation = m_saveGeneration + 1;
    const QString reloadRoot = QDir(m_runRoot).filePath(
        QString("generations/%1").arg(generation, 6, 10, QLatin1Char('0')));
    const QString generationSavePath = QDir(reloadRoot).filePath("saves");
    const QString payloadMpr = QDir(reloadRoot).filePath("map.mpr");
    const QString payloadMob = QDir(reloadRoot).filePath("map.mob");
    const QString payloadGrp = graphSource.usesExternalGraph()
        ? QDir(reloadRoot).filePath("map.grp") : QString();
    const QString payloadDatabase = QDir(reloadRoot).filePath("database.res");
    const bool hasGeneratedMinimap = !input.temporaryMinimapMmp.isEmpty();
    const QString minimapTextureName = hasGeneratedMinimap
        ? temporaryMinimapRuntimeTextureName(
              input.temporaryMinimapMmp,
              m_runId + ":" + QString::number(sequence))
        : input.mapEntry.minimapTexture.trimmed();
    const QString minimapResourcePath = hasGeneratedMinimap
        ? QDir(reloadRoot).filePath("minimap.res") : QString();
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
       || !atomicWrite(payloadMob, input.mobData, error)
       || !atomicWrite(payloadDatabase, input.databaseData, error))
        return false;
    if(graphSource.usesExternalGraph()
       && !atomicWrite(payloadGrp, graphSource.externalData, error))
        return false;
    if(hasGeneratedMinimap)
    {
        QByteArray minimapArchive;
        QStringList minimapErrors;
        if(!buildSyntheticMapTextureArchive(minimapTextureName,
                                            input.temporaryMinimapMmp,
                                            &minimapArchive, &minimapErrors)
           || !atomicWrite(minimapResourcePath, minimapArchive, error)
           || fileSha256(minimapResourcePath).compare(
                  sha256(minimapArchive), Qt::CaseInsensitive) != 0)
        {
            if(error && error->isEmpty())
                *error = "The hot-reload minimap RES failed staged validation: "
                    + minimapErrors.join(" | ");
            return false;
        }
    }
    if(graphSource.usesExternalGraph()
       && fileSha256(payloadGrp).compare(graphSource.sha256, Qt::CaseInsensitive) != 0)
    {
        if(error) *error = "The hot-reload external AI graph failed its post-write SHA-256 check.";
        return false;
    }

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
                                input.overrideWorldTime, input.worldTimeHours,
                                &reloadManifest, &bootstrapPath, error))
        return false;

    SMapHotReloadRequest request;
    request.sequence = sequence;
    request.generation = generation;
    request.runtimeStateSequence = latestRuntimeStateSequence();
    request.runId = m_runId;
    request.profileId = input.profile.profileId;
    request.sourceZoneId = input.mapEntry.zoneId;
    request.bootstrapMprName = input.mapEntry.mprName;
    request.bootstrapMobName = input.mapEntry.mobName;
    request.zoneId = input.syntheticMapIdentity
        ? m_temporaryMapName : input.mapEntry.zoneId;
    request.bootstrapMode = "automatic";
    request.entranceId = input.spawn.exitId;
    request.mprPath = payloadMpr;
    request.mobPath = payloadMob;
    request.mprSha256 = sha256(input.mprData);
    request.mobSha256 = sha256(input.mobData);
    request.bootstrapMobPath = bootstrapPath;
    request.bootstrapMobSha256 = bootstrapPath.isEmpty()
        ? QString() : fileSha256(bootstrapPath);
    request.minimapTextureName = minimapTextureName;
    request.minimapResourcePath = minimapResourcePath;
    request.minimapResourceSha256 = minimapResourcePath.isEmpty()
        ? QString() : fileSha256(minimapResourcePath);
    request.databasePath = payloadDatabase;
    request.databaseSha256 = sha256(input.databaseData);
    request.savePath = generationSavePath;
    // Position preservation requires a verified runtime position provider.
    // Until that provider exists, use normal entrance/custom-spawn bootstrap.
    request.preservePosition = false;
    request.graphless = graphSource.missing;
    request.syntheticMapIdentity = input.syntheticMapIdentity;
    request.mapSizeX = input.mapSizeX;
    request.mapSizeY = input.mapSizeY;
    request.spawnX = input.spawn.position.x();
    request.spawnY = input.spawn.position.y();
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
    const qint64 gamePid = runtimeGameProcessId();
    const bool ownsThread = threadOk && threadId != 0
        && gamePid > 0
        && threadBelongsToProcess(threadId, quint32(gamePid));
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
    m_currentZoneId = request.zoneId;
    QJsonArray reloads = m_manifest.value("hotReloads").toArray();
    QJsonObject audit = mapHotReloadRequestToJson(request);
    QJsonArray supplementalAudit;
    for(const QString& name : desiredSupplementalNames)
        supplementalAudit.append(stableSupplemental.filePath(name));
    audit.insert("supplementalMobPaths", supplementalAudit);
    QJsonObject graphAudit;
    graphAudit.insert("source", graphSource.missing ? "missing-allowed"
                                                     : (graphSource.embedded ? "embedded-mob" : "external-grp"));
    if(graphSource.usesExternalGraph())
    {
        graphAudit.insert("sourcePath", graphSource.externalPath);
        graphAudit.insert("sourceSha256", graphSource.sha256);
        graphAudit.insert("stagedPath", payloadGrp);
        graphAudit.insert("stagedSha256", fileSha256(payloadGrp));
    }
    if(!graphSource.shadowedExternalPath.isEmpty())
        graphAudit.insert("shadowedExternalPath", graphSource.shadowedExternalPath);
    audit.insert("aiGraph", graphAudit);
    audit.insert("auditPath", auditPath);
    audit.insert("minimapTextureName", minimapTextureName);
    audit.insert("minimapResourcePath", minimapResourcePath);
    audit.insert("minimapResourceSha256", request.minimapResourceSha256);
    audit.insert("databasePath", request.databasePath);
    audit.insert("databaseSha256", request.databaseSha256);
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
    appendRunLog(QString("Hot reload generation=%1 minimap=%2 resource=%3")
                 .arg(generation).arg(
                     minimapTextureName.isEmpty() ? "unchanged" : minimapTextureName,
                     minimapResourcePath.isEmpty()
                         ? "active-profile" : QDir::toNativeSeparators(minimapResourcePath)));
    if(graphSource.usesExternalGraph())
        appendRunLog(QString("Hot reload generation=%1 staged external AI graph source=%2 sha256=%3")
                     .arg(generation)
                     .arg(QDir::toNativeSeparators(graphSource.externalPath),
                          graphSource.sha256));
    else if(!graphSource.shadowedExternalPath.isEmpty())
        appendRunLog(QString("Hot reload generation=%1 uses embedded AI_GRAPH; external graph is shadowed: %2")
                     .arg(generation)
                     .arg(QDir::toNativeSeparators(graphSource.shadowedExternalPath)));
    else if(graphSource.missing)
        appendRunLog(QString("WARNING: Hot reload generation=%1 is starting without an embedded AI_GRAPH or external .grp by explicit user confirmation")
                     .arg(generation));
    else
        appendRunLog(QString("Hot reload generation=%1 uses embedded AI_GRAPH from the primary MOB")
                     .arg(generation));
    emit hotReloadQueued(sequence, request.zoneId);
    return true;
}

bool CJmvMapTestLauncher::restart(const SJmvMapTestLaunchInput& input,
                                  QString* error)
{
    if(!isRunning())
        return launch(input, error);
    if(m_restartPending)
    {
        if(error)
            *error = QCoreApplication::translate(
                "CJmvMapTestLauncher",
                "A fresh Test Current Map process is already being prepared.");
        return false;
    }
    if(input.profile.profileId.compare(m_profileId, Qt::CaseInsensitive) != 0)
    {
        if(error)
            *error = QCoreApplication::translate(
                "CJmvMapTestLauncher",
                "The active editor profile differs from the running test profile.");
        return false;
    }
    m_restartInput = input;
    m_restartPending = true;
    appendRunLog(QString("Fresh process requested for startup resources: %1")
        .arg(startupResourceChanges(input).join(QStringLiteral(" + "))));
    m_stopRequested = true;
    m_process->terminate();
    QTimer::singleShot(5000, this, [this]()
    {
        if(isRunning())
        {
            appendRunLog(
                "Graceful restart stop timed out; terminating the Endurance-owned process");
            m_process->kill();
        }
    });
    return true;
}

bool CJmvMapTestLauncher::queueTerrainAtlasRestart(
    const SJmvMapTestLaunchInput& input, const QString& candidateAtlasSha256,
    QString* error)
{
    if(!isRunning())
    {
        if(error)
            *error = "The owned test process exited before its terrain atlas could be restarted.";
        return false;
    }
    if(m_terrainAtlasRestartPending)
    {
        if(error)
            *error = "A terrain-atlas test restart is already in progress.";
        return false;
    }

    m_pendingTerrainAtlasRestartInput = input;
    m_terrainAtlasRestartPending = true;
    appendRunLog(QString(
        "Terrain atlas input changed; replacing unsafe MPR-only hot reload "
        "with an owned-process restart oldSha256=%1 newSha256=%2 "
        "oldPages=%3 newPages=%4 oldSlots=%5 newSlots=%6")
        .arg(m_sourceTerrainTextureResourceSha256.isEmpty()
                 ? QStringLiteral("(none)")
                 : m_sourceTerrainTextureResourceSha256,
             candidateAtlasSha256.isEmpty()
                 ? QStringLiteral("(none)") : candidateAtlasSha256)
        .arg(m_sourceTerrainAtlasPages).arg(input.expectedTerrainAtlasPages)
        .arg(m_sourceTerrainAtlasSlots).arg(input.expectedTerrainAtlasSlots));
    m_stopRequested = true;
    m_process->terminate();
    QTimer::singleShot(5000, this, [this]()
    {
        if(isRunning() && m_terrainAtlasRestartPending)
        {
            appendRunLog(
                "Terrain-atlas restart stop timed out; terminating the Endurance-owned process");
            m_process->kill();
        }
    });
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
    if(m_restartPending)
    {
        appendRunLog("Pending fresh-process launch cancelled by Stop Test or editor shutdown");
        m_restartPending = false;
        m_restartInput = SJmvMapTestLaunchInput();
    }
    if(!isRunning())
        return;
    m_stopRequested = true;
    appendRunLog(m_runtimeFailureObserved
        ? "Stop Test initiated after detected native runtime failure"
        : "Stop Test requested by user");
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
    const qint64 ownedPid = runtimeGameProcessId();
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
        // An arbitrary game.exe is not evidence that it belongs to this
        // journal. Only the journal's still-live recorded PID may defer legacy
        // cleanup; complete journals already require path + creation time +
        // optional hash above. This prevents a normal unrelated game session
        // from blocking Test Current Map with a false ownership claim.
        if(incompleteIdentity && recordedPid > 0 && processExists(recordedPid))
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
        // A process found only by the filename game.exe is not owned by this
        // journal. Legacy recovery may wait for its recorded PID, while modern
        // journals require the complete guarded process identity above.
        const bool conservativePending = !completeIdentity && recordedPid > 0
            && processExists(recordedPid);
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

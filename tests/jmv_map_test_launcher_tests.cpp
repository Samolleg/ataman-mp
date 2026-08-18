#include <QtTest>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <thread>
#endif

#include "jmv_map_test_launcher.h"
#include "current_session_backup.h"
#include "endurance_document.h"
#include "mpr_document.h"
#include "res_archive_document.h"

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
    void rejectsMixedJmvGameRootRegistryPaths();
    void validatesExternalGraphEnvelope();
    void recognizesGraphlessBridgeProtocolMarker();
    void detectsAutoSpellMobCapability();
    void resolvesWorkspaceExternalGraph();
    void embeddedGraphShadowsExternalGraph();
    void reportsMissingOptionalGraph();
    void rejectsMalformedExternalGraph();
    void resolvesActualZone6ExternalGraph();
    void resolvesHostedGamePidForHotReload();
    void startupResourceChangesRequireFreshProcess();
    void preparesOwnedGodmodeAutoexec();
    void preparesRunOwnedDrawallAutoexec();
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
    void authoritativeResultRejectsStaleReadyWithoutControlledPlayer();
    void recognizesNativeRuntimeFailureDialogs();
    void authoritativeResultRejectsObservedFailureAfterStop();
    void authoritativeResultRequiresExpandedAtlasRegistration();
    void authoritativeResultRequiresDrawallStagingEvidence();
    void authoritativeResultAcceptsLatestHotReloadGeneration();
    void authoritativeResultRequiresStagedTablesLuaRead();
    void authoritativeResultRejectsWrongZone();
    void analyzesPersistedAuthoritativeResult();
    void rebasesRuntimeTerrainAtlasNamespace();
    void changedTerrainAtlasRequiresOwnedProcessRestart();
    void renderEvidenceRejectsTexturelessFrames();
    void memoryEvidenceSummarizesComparableSamples();
    void runtimeSmokeReachesGameplayReady();
    void runtimeSmokeRecoveryClosesOwnedRun();
};

static QString hash(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

static QByteArray launcherSource()
{
    const QString relativePath = QStringLiteral(
        "source/runtime/jmv_map_test_launcher.cpp");
    QDir directory(QDir::currentPath());
    for(int depth = 0; depth < 8; ++depth)
    {
        QFile source(directory.filePath(relativePath));
        if(source.open(QIODevice::ReadOnly))
            return source.readAll();
        if(!directory.cdUp())
            break;
    }
    return QByteArray();
}

#ifdef Q_OS_WIN
struct RuntimeWindowSearch
{
    DWORD processId{0};
    HWND window{nullptr};
    qint64 area{0};
};

static BOOL CALLBACK findRuntimeWindow(HWND window, LPARAM parameter)
{
    RuntimeWindowSearch* search = reinterpret_cast<RuntimeWindowSearch*>(parameter);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if(processId == search->processId && IsWindowVisible(window)
       && GetWindow(window, GW_OWNER) == nullptr)
    {
        RECT bounds{};
        if(GetWindowRect(window, &bounds))
        {
            const qint64 area = qint64(qMax(0L, bounds.right - bounds.left))
                * qint64(qMax(0L, bounds.bottom - bounds.top));
            if(area > search->area)
            {
                search->window = window;
                search->area = area;
            }
        }
    }
    return TRUE;
}

static HWND runtimeWindow(qint64 processId)
{
    if(processId <= 0)
        return nullptr;
    RuntimeWindowSearch search;
    search.processId = DWORD(processId);
    EnumWindows(findRuntimeWindow, LPARAM(&search));
    return search.window;
}

static bool runtimeWindowResponsive(qint64 processId)
{
    if(processId <= 0)
        return false;
    const HWND window = runtimeWindow(processId);
    if(!window || IsHungAppWindow(window))
        return false;
    DWORD_PTR ignored = 0;
    return SendMessageTimeoutW(window, WM_NULL, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &ignored) != 0;
}

static QImage captureRuntimeWindow(qint64 processId)
{
    const HWND window = runtimeWindow(processId);
    RECT bounds{};
    if(!window || !GetWindowRect(window, &bounds))
        return QImage();
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if(width <= 0 || height <= 0)
        return QImage();
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = memory ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
    const bool copied = bitmap && BitBlt(memory, 0, 0, width, height, screen,
        bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
    QImage image(width, height, QImage::Format_ARGB32);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    const bool read = copied && GetDIBits(memory, bitmap, 0, UINT(height),
        image.bits(), &info, DIB_RGB_COLORS) == height;
    if(previous)
        SelectObject(memory, previous);
    if(bitmap)
        DeleteObject(bitmap);
    if(memory)
        DeleteDC(memory);
    if(screen)
        ReleaseDC(nullptr, screen);
    return read ? image : QImage();
}

static bool hasRenderedTerrainEvidence(const QImage& frame,
                                       double* dominantRatio,
                                       int* colorBuckets)
{
    if(frame.width() < 640 || frame.height() < 480)
        return false;
    const QRect sample(frame.width() / 8, frame.height() / 8,
                       frame.width() * 3 / 4, frame.height() * 3 / 4);
    QHash<quint16, int> histogram;
    int total = 0;
    int dominant = 0;
    for(int y = sample.top(); y < sample.bottom(); y += 2)
    {
        const QRgb* row = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        for(int x = sample.left(); x < sample.right(); x += 2)
        {
            const QRgb pixel = row[x];
            const quint16 key = quint16((qRed(pixel) >> 3) << 10)
                | quint16((qGreen(pixel) >> 3) << 5)
                | quint16(qBlue(pixel) >> 3);
            const int count = ++histogram[key];
            dominant = qMax(dominant, count);
            ++total;
        }
    }
    const double ratio = total > 0 ? double(dominant) / double(total) : 1.0;
    if(dominantRatio)
        *dominantRatio = ratio;
    if(colorBuckets)
        *colorBuckets = histogram.size();
    // A missing terrain atlas leaves most of the world as one flat cyan clear
    // color (or a black capture). Authored JMV terrain is both non-dominant and
    // chromatically varied even before lighting and UI are considered.
    return histogram.size() >= 64 && ratio < 0.35;
}

struct RuntimeMemorySample
{
    qint64 elapsedMs{0};
    QString phase;
    quint64 workingSetBytes{0};
    quint64 reportedPeakWorkingSetBytes{0};
    quint64 privateBytes{0};
    quint64 pagefileBytes{0};
    quint64 committedAddressBytes{0};
    quint64 reservedAddressBytes{0};
    quint64 freeAddressBytes{0};
    quint64 largestFreeRegionBytes{0};
    quint64 mappedAddressBytes{0};
    quint64 imageAddressBytes{0};
    quint64 privateAddressBytes{0};

    RuntimeMemorySample() = default;
    RuntimeMemorySample(qint64 elapsed, const QString& samplePhase,
                        quint64 workingSet, quint64 reportedPeak,
                        quint64 privateWorking, quint64 pagefile,
                        quint64 committed, quint64 reserved, quint64 free,
                        quint64 largestFree, quint64 mapped, quint64 image,
                        quint64 privateAddress)
        : elapsedMs(elapsed), phase(samplePhase),
          workingSetBytes(workingSet),
          reportedPeakWorkingSetBytes(reportedPeak),
          privateBytes(privateWorking), pagefileBytes(pagefile),
          committedAddressBytes(committed), reservedAddressBytes(reserved),
          freeAddressBytes(free), largestFreeRegionBytes(largestFree),
          mappedAddressBytes(mapped), imageAddressBytes(image),
          privateAddressBytes(privateAddress)
    {
    }
};

static bool sampleRuntimeMemory(qint64 processId, qint64 elapsedMs,
                                const QString& phase,
                                RuntimeMemorySample* sample)
{
    if(!sample || processId <= 0)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                 FALSE, DWORD(processId));
    if(!process)
        return false;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    const bool ok = GetProcessMemoryInfo(process,
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        DWORD(sizeof(counters))) != FALSE;
    if(!ok)
    {
        CloseHandle(process);
        return false;
    }
    sample->elapsedMs = elapsedMs;
    sample->phase = phase;
    sample->workingSetBytes = quint64(counters.WorkingSetSize);
    sample->reportedPeakWorkingSetBytes = quint64(counters.PeakWorkingSetSize);
    sample->privateBytes = quint64(counters.PrivateUsage);
    sample->pagefileBytes = quint64(counters.PagefileUsage);
    // game.exe is a 32-bit process. Walk its complete 4-GiB user address map so
    // atlas tests distinguish physical-memory growth from VA fragmentation.
    const quint64 addressLimit = quint64(1) << 32;
    quint64 address = 0;
    while(address < addressLimit)
    {
        MEMORY_BASIC_INFORMATION region{};
        const SIZE_T queried = VirtualQueryEx(process,
            reinterpret_cast<LPCVOID>(quintptr(address)), &region, sizeof(region));
        if(queried == 0)
            break;
        const quint64 bytes = qMin<quint64>(quint64(region.RegionSize),
                                            addressLimit - address);
        if(region.State == MEM_FREE)
        {
            sample->freeAddressBytes += bytes;
            sample->largestFreeRegionBytes = qMax(
                sample->largestFreeRegionBytes, bytes);
        }
        else if(region.State == MEM_RESERVE)
            sample->reservedAddressBytes += bytes;
        else if(region.State == MEM_COMMIT)
        {
            sample->committedAddressBytes += bytes;
            if(region.Type == MEM_MAPPED) sample->mappedAddressBytes += bytes;
            else if(region.Type == MEM_IMAGE) sample->imageAddressBytes += bytes;
            else if(region.Type == MEM_PRIVATE) sample->privateAddressBytes += bytes;
        }
        const quint64 next = quint64(reinterpret_cast<quintptr>(region.BaseAddress))
            + quint64(region.RegionSize);
        if(next <= address)
            break;
        address = next;
    }
    CloseHandle(process);
    return true;
}

static QJsonObject summarizeRuntimeMemory(
    const QVector<RuntimeMemorySample>& samples, int atlasPages, int atlasSlots)
{
    QJsonObject result;
    result.insert("schema", "ei-runtime-memory-evidence-v2");
    result.insert("source", "GetProcessMemoryInfo + VirtualQueryEx (32-bit address space)");
    result.insert("atlasPages", atlasPages);
    result.insert("atlasSlots", atlasSlots);
    result.insert("sampleCount", samples.size());
    quint64 observedPeakWorkingSet = 0;
    quint64 reportedPeakWorkingSet = 0;
    quint64 peakPrivate = 0;
    quint64 peakPagefile = 0;
    quint64 peakCommittedAddress = 0;
    quint64 peakMappedAddress = 0;
    quint64 peakPrivateAddress = 0;
    quint64 minimumFreeAddress = quint64(-1);
    quint64 minimumLargestFreeRegion = quint64(-1);
    const RuntimeMemorySample* gameplayReady = nullptr;
    QJsonArray serialized;
    for(const RuntimeMemorySample& sample : samples)
    {
        observedPeakWorkingSet = qMax(observedPeakWorkingSet,
                                      sample.workingSetBytes);
        reportedPeakWorkingSet = qMax(reportedPeakWorkingSet,
                                      sample.reportedPeakWorkingSetBytes);
        peakPrivate = qMax(peakPrivate, sample.privateBytes);
        peakPagefile = qMax(peakPagefile, sample.pagefileBytes);
        peakCommittedAddress = qMax(peakCommittedAddress, sample.committedAddressBytes);
        peakMappedAddress = qMax(peakMappedAddress, sample.mappedAddressBytes);
        peakPrivateAddress = qMax(peakPrivateAddress, sample.privateAddressBytes);
        minimumFreeAddress = qMin(minimumFreeAddress, sample.freeAddressBytes);
        minimumLargestFreeRegion = qMin(minimumLargestFreeRegion,
                                        sample.largestFreeRegionBytes);
        if(!gameplayReady && sample.phase != QStringLiteral("loading"))
            gameplayReady = &sample;
        QJsonObject item;
        item.insert("elapsedMs", sample.elapsedMs);
        item.insert("phase", sample.phase);
        item.insert("workingSetBytes", qint64(sample.workingSetBytes));
        item.insert("privateBytes", qint64(sample.privateBytes));
        item.insert("pagefileBytes", qint64(sample.pagefileBytes));
        item.insert("committedAddressBytes", qint64(sample.committedAddressBytes));
        item.insert("reservedAddressBytes", qint64(sample.reservedAddressBytes));
        item.insert("freeAddressBytes", qint64(sample.freeAddressBytes));
        item.insert("largestFreeRegionBytes", qint64(sample.largestFreeRegionBytes));
        item.insert("mappedAddressBytes", qint64(sample.mappedAddressBytes));
        item.insert("imageAddressBytes", qint64(sample.imageAddressBytes));
        item.insert("privateAddressBytes", qint64(sample.privateAddressBytes));
        serialized.append(item);
    }
    result.insert("samples", serialized);
    result.insert("observedPeakWorkingSetBytes", qint64(observedPeakWorkingSet));
    result.insert("reportedPeakWorkingSetBytes", qint64(reportedPeakWorkingSet));
    result.insert("peakPrivateBytes", qint64(peakPrivate));
    result.insert("peakPagefileBytes", qint64(peakPagefile));
    result.insert("peakCommittedAddressBytes", qint64(peakCommittedAddress));
    result.insert("peakMappedAddressBytes", qint64(peakMappedAddress));
    result.insert("peakPrivateAddressBytes", qint64(peakPrivateAddress));
    result.insert("minimumFreeAddressBytes", qint64(samples.isEmpty() ? 0 : minimumFreeAddress));
    result.insert("minimumLargestFreeRegionBytes",
                  qint64(samples.isEmpty() ? 0 : minimumLargestFreeRegion));
    if(gameplayReady)
    {
        result.insert("gameplayReadyWorkingSetBytes",
                      qint64(gameplayReady->workingSetBytes));
        result.insert("gameplayReadyPrivateBytes",
                      qint64(gameplayReady->privateBytes));
        result.insert("gameplayReadyCommittedAddressBytes",
                      qint64(gameplayReady->committedAddressBytes));
        result.insert("gameplayReadyMappedAddressBytes",
                      qint64(gameplayReady->mappedAddressBytes));
    }
    if(!samples.isEmpty())
    {
        const RuntimeMemorySample& final = samples.back();
        result.insert("finalWorkingSetBytes", qint64(final.workingSetBytes));
        result.insert("finalPrivateBytes", qint64(final.privateBytes));
        result.insert("finalCommittedAddressBytes",
                      qint64(final.committedAddressBytes));
        result.insert("finalMappedAddressBytes", qint64(final.mappedAddressBytes));
        result.insert("finalFreeAddressBytes", qint64(final.freeAddressBytes));
        result.insert("finalLargestFreeRegionBytes",
                      qint64(final.largestFreeRegionBytes));
        result.insert("durationMs", final.elapsedMs);
    }
    return result;
}
#endif

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
    const QStringList arguments = QCoreApplication::arguments();
    const bool runtimeInvocation = arguments.contains(
        QStringLiteral("runtimeSmokeReachesGameplayReady"));
    const bool recoveryInvocation = arguments.contains(
        QStringLiteral("runtimeSmokeRecoveryClosesOwnedRun"));
    if(runtimeInvocation)
    {
        bool pending = false;
        const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns(&pending);
        QVERIFY2(!pending, qPrintable(QStringLiteral(
            "A previous owned runtime run is still active; recovery must finish first. %1")
            .arg(summary)));
    }
    else if(!recoveryInvocation)
        QDir(root).removeRecursively();
}

static QByteArray terrainPageArchive(int pages, const QString& sourceNamespace)
{
    QByteArray bytes(16, '\0');
    qToLittleEndian<quint32>(0x019CE23C,
        reinterpret_cast<uchar*>(bytes.data()));
    qToLittleEndian<quint32>(16,
        reinterpret_cast<uchar*>(bytes.data() + 8));
    CResArchiveDocument archive;
    QStringList errors;
    if(!archive.loadBytes(bytes, QStringLiteral("empty.res"), &errors))
        return QByteArray();
    for(int page = 0; page < pages; ++page)
    {
        const QString name = QStringLiteral("%1%2.mmp")
            .arg(sourceNamespace).arg(page, 3, 10, QLatin1Char('0'));
        const QByteArray payload = QStringLiteral("authored-page-%1")
            .arg(page).toLatin1();
        QByteArray next;
        if(!archive.buildWithAppendedMember(name, payload, 0, &next, &errors)
           || !archive.loadBytes(next, QStringLiteral("pages.res"), &errors))
            return QByteArray();
        bytes = next;
    }
    return bytes;
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

void JmvMapTestLauncherTests::startupResourceChangesRequireFreshProcess()
{
    SJmvMapTestLaunchInput input;
    input.databaseData = "database baseline";
    input.tablesLuaSourcePath = "D:/Evil Islands/Starter/Mods/JamaisVu135/lua/tables.lua";
    input.tablesLuaData = "lua baseline";

    QStringList reasons = CJmvMapTestLauncher::changedStartupResources(
        hash(input.databaseData), input.tablesLuaSourcePath,
        hash(input.tablesLuaData), input);
    QVERIFY(reasons.isEmpty());

    input.databaseData = "database with cloned prototype";
    reasons = CJmvMapTestLauncher::changedStartupResources(
        hash("database baseline"), input.tablesLuaSourcePath,
        hash(input.tablesLuaData), input);
    QCOMPARE(reasons, QStringList{QStringLiteral("database.res")});

    input.databaseData = "database baseline";
    input.tablesLuaData = "lua with edited unit stats";
    reasons = CJmvMapTestLauncher::changedStartupResources(
        hash(input.databaseData), input.tablesLuaSourcePath,
        hash("lua baseline"), input);
    QCOMPARE(reasons, QStringList{QStringLiteral("tables.lua")});

    input.databaseData = "database with edited HP";
    reasons = CJmvMapTestLauncher::changedStartupResources(
        hash("database baseline"), input.tablesLuaSourcePath,
        hash("lua baseline"), input);
    QCOMPARE(reasons, (QStringList{QStringLiteral("database.res"),
                                  QStringLiteral("tables.lua")}));
}

void JmvMapTestLauncherTests::rejectsMixedJmvGameRootRegistryPaths()
{
    QString error;
    QVERIFY(!CJmvMapTestLauncher::validateJmvGameRootPaths(
        "D:/Evil Islands",
        "C:/Users/test/Documents/EI Renderer/work/runtime-clone/",
        "C:/Users/test/Documents/EI Renderer/work/runtime-clone/Res/",
        "C:/Users/test/Documents/EI Renderer/work/runtime-clone/",
        &error));
    QVERIFY(error.contains("runtime-clone"));
    QVERIFY(error.contains("D:\\Evil Islands"));

    error.clear();
    QVERIFY2(CJmvMapTestLauncher::validateJmvGameRootPaths(
        "D:/Evil Islands", "D:/Evil Islands/", "D:/Evil Islands/Res/",
        "D:/Evil Islands/", &error), qPrintable(error));
}

void JmvMapTestLauncherTests::rebasesRuntimeTerrainAtlasNamespace()
{
    const QString fixture = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../../tests/fixtures/runtime/particle-flat-7x5.mpr");
    QVERIFY2(QFileInfo::exists(fixture),
             qPrintable("The checked runtime MPR fixture is missing: " + fixture));
    QFile sourceFile(fixture);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    const QByteArray sourceMpr = sourceFile.readAll();
    CMprDocument sourceDocument;
    CValidationReport sourceReport;
    QVERIFY2(sourceDocument.loadBytes(sourceMpr, fixture, &sourceReport),
             qPrintable(sourceReport.toText()));
    const int pages = qMax(1, int(sourceDocument.mapInfo().textureCount));
    const QString sourceNamespace = sourceDocument.mapInfo().innerNamespace;
    const QByteArray sourceRes = terrainPageArchive(pages, sourceNamespace);
    QVERIFY(!sourceRes.isEmpty());

    const QString runtimeNamespace = QStringLiteral("ed12ab34");
    QByteArray runtimeMpr;
    QByteArray runtimeRes;
    QStringList runtimeMembers;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::rebaseRuntimeTerrainAtlas(
                 sourceMpr, sourceRes, runtimeNamespace, pages,
                 &runtimeMpr, &runtimeRes, &runtimeMembers, &error),
             qPrintable(error));
    QCOMPARE(runtimeMembers.size(), pages);
    for(int page = 0; page < pages; ++page)
        QCOMPARE(runtimeMembers.at(page), QStringLiteral("%1%2.mmp")
                 .arg(runtimeNamespace).arg(page, 3, 10, QLatin1Char('0')));

    CMprDocument reopenedMpr;
    CValidationReport reopenedReport;
    QVERIFY2(reopenedMpr.loadBytes(runtimeMpr, "rebased.mpr", &reopenedReport),
             qPrintable(reopenedReport.toText()));
    QCOMPARE(reopenedMpr.mapInfo().innerNamespace, runtimeNamespace);
    CResArchiveDocument reopenedRes;
    QStringList archiveErrors;
    QVERIFY2(reopenedRes.loadBytes(runtimeRes, "rebased.res", &archiveErrors),
             qPrintable(archiveErrors.join('\n')));
    QCOMPARE(reopenedRes.members().size(), pages);
    for(int page = 0; page < pages; ++page)
        QCOMPARE(reopenedRes.memberPayload(runtimeMembers.at(page)),
                 QStringLiteral("authored-page-%1").arg(page).toLatin1());
    if(sourceNamespace.compare(runtimeNamespace, Qt::CaseInsensitive) != 0)
        QVERIFY(reopenedRes.memberPayload(QStringLiteral("%1%2.mmp")
            .arg(sourceNamespace).arg(0, 3, 10, QLatin1Char('0'))).isEmpty());

    // Test Current must select the MPR-declared contiguous identity from a
    // general-purpose RES. Unrelated identities and stale higher pages are not
    // part of this MPR and must neither block nor enter the rebased archive.
    CResArchiveDocument archiveWithExtras;
    QStringList extraErrors;
    QVERIFY2(archiveWithExtras.loadBytes(sourceRes, "source-with-extras.res",
                                         &extraErrors),
             qPrintable(extraErrors.join('\n')));
    QByteArray withStalePage;
    const QString staleName = QStringLiteral("%1%2.mmp")
        .arg(sourceNamespace).arg(pages, 3, 10, QLatin1Char('0'));
    QVERIFY2(archiveWithExtras.buildWithAppendedMember(
                 staleName, QByteArray("stale-higher-page"), 0,
                 &withStalePage, &extraErrors),
             qPrintable(extraErrors.join('\n')));
    QVERIFY2(archiveWithExtras.loadBytes(withStalePage,
                                         "source-with-stale-page.res",
                                         &extraErrors),
             qPrintable(extraErrors.join('\n')));
    QByteArray withUnrelatedIdentity;
    QVERIFY2(archiveWithExtras.buildWithAppendedMember(
                 QStringLiteral("other000.mmp"), QByteArray("unrelated-page"), 0,
                 &withUnrelatedIdentity, &extraErrors),
             qPrintable(extraErrors.join('\n')));
    QByteArray extraRebasedMpr;
    QByteArray extraRebasedRes;
    QStringList extraRebasedMembers;
    QString extraRebaseError;
    QVERIFY2(CJmvMapTestLauncher::rebaseRuntimeTerrainAtlas(
                 sourceMpr, withUnrelatedIdentity, QStringLiteral("ed12ef56"),
                 pages, &extraRebasedMpr, &extraRebasedRes,
                 &extraRebasedMembers, &extraRebaseError),
             qPrintable(extraRebaseError));
    QCOMPARE(extraRebasedMembers.size(), pages);
    CResArchiveDocument extrasReopened;
    QVERIFY2(extrasReopened.loadBytes(extraRebasedRes,
                                      "rebased-with-extras.res", &extraErrors),
             qPrintable(extraErrors.join('\n')));
    QCOMPARE(extrasReopened.members().size(), pages);
    QVERIFY(extrasReopened.memberPayload(staleName).isEmpty());
    QVERIFY(extrasReopened.memberPayload(QStringLiteral("other000.mmp")).isEmpty());

    // Optional read-only probe for a reported published pair. This runs the
    // exact same namespace rebase used by Test Current without launching the
    // editor or game and without copying or modifying the source files.
    const QString externalMprPath = qEnvironmentVariable(
        "ENDURANCE_PUBLISHED_MPR_FIXTURE");
    const QString externalResPath = qEnvironmentVariable(
        "ENDURANCE_PUBLISHED_TERRAIN_RES_FIXTURE");
    if(!externalMprPath.isEmpty() || !externalResPath.isEmpty())
    {
        QVERIFY2(QFileInfo::exists(externalMprPath),
                 qPrintable("Published MPR fixture is missing: " + externalMprPath));
        QVERIFY2(QFileInfo::exists(externalResPath),
                 qPrintable("Published terrain RES fixture is missing: " + externalResPath));
        QFile externalMprFile(externalMprPath);
        QFile externalResFile(externalResPath);
        QVERIFY(externalMprFile.open(QIODevice::ReadOnly));
        QVERIFY(externalResFile.open(QIODevice::ReadOnly));
        const QByteArray externalMpr = externalMprFile.readAll();
        const QByteArray externalRes = externalResFile.readAll();
        CMprDocument externalDocument;
        CValidationReport externalReport;
        QVERIFY2(externalDocument.loadBytes(
                     externalMpr, externalMprPath, &externalReport),
                 qPrintable(externalReport.toText()));
        QByteArray rebasedMpr;
        QByteArray rebasedRes;
        QStringList rebasedMembers;
        QString externalError;
        QVERIFY2(CJmvMapTestLauncher::rebaseRuntimeTerrainAtlas(
                     externalMpr, externalRes, QStringLiteral("ed137abc"),
                     int(externalDocument.mapInfo().textureCount),
                     &rebasedMpr, &rebasedRes, &rebasedMembers, &externalError),
                 qPrintable(externalError));
        QCOMPARE(rebasedMembers.size(),
                 int(externalDocument.mapInfo().textureCount));
        QVERIFY(!rebasedMpr.isEmpty());
        QVERIFY(!rebasedRes.isEmpty());
    }
}

void JmvMapTestLauncherTests::changedTerrainAtlasRequiresOwnedProcessRestart()
{
    const QByteArray atlasA = QByteArrayLiteral("atlas-a");
    const QByteArray atlasB = QByteArrayLiteral("atlas-b");
    const QString hashA = hash(atlasA);
    const QString hashB = hash(atlasB);

    QVERIFY(!CJmvMapTestLauncher::terrainAtlasHotReloadRequiresRestart(
        hashA, 8, 512, hashA, 8, 512));
    QVERIFY(CJmvMapTestLauncher::terrainAtlasHotReloadRequiresRestart(
        hashA, 8, 512, hashB, 8, 512));
    QVERIFY(CJmvMapTestLauncher::terrainAtlasHotReloadRequiresRestart(
        hashA, 8, 512, hashA, 9, 513));
    QVERIFY(CJmvMapTestLauncher::terrainAtlasHotReloadRequiresRestart(
        QString(), 0, 0, hashA, 8, 512));
    QVERIFY(CJmvMapTestLauncher::terrainAtlasHotReloadRequiresRestart(
        hashA, 8, 512, QString(), 0, 0));

    const QByteArray source = launcherSource();
    const int hotReloadStart = source.indexOf(
        "bool CJmvMapTestLauncher::hotReload(");
    const int hotReloadEnd = source.indexOf(
        "bool CJmvMapTestLauncher::queueTerrainAtlasRestart(",
        hotReloadStart);
    QVERIFY(hotReloadStart >= 0 && hotReloadEnd > hotReloadStart);
    const QByteArray hotReload = source.mid(
        hotReloadStart, hotReloadEnd - hotReloadStart);
    QVERIFY2(hotReload.contains(
                 "terrainAtlasHotReloadRequiresRestart("),
             "Hot reload no longer checks whether terrain atlas bytes changed");
    QVERIFY2(hotReload.contains("queueTerrainAtlasRestart("),
             "Changed terrain atlas bytes no longer force an owned-process restart");
}

void JmvMapTestLauncherTests::renderEvidenceRejectsTexturelessFrames()
{
#ifdef Q_OS_WIN
    QImage cyan(1280, 720, QImage::Format_ARGB32);
    cyan.fill(qRgb(91, 177, 188));
    for(int y = 260; y < 560; ++y)
        for(int x = 450; x < 850; ++x)
            if(((x / 37) + (y / 29)) % 5 == 0)
                cyan.setPixel(x, y, qRgb(22, 52, 55));
    double ratio = 0.0;
    int buckets = 0;
    QVERIFY(!hasRenderedTerrainEvidence(cyan, &ratio, &buckets));
    QVERIFY(ratio > 0.35);

    QImage black(1280, 720, QImage::Format_ARGB32);
    black.fill(Qt::black);
    QVERIFY(!hasRenderedTerrainEvidence(black, &ratio, &buckets));

    QImage authored(1280, 720, QImage::Format_ARGB32);
    for(int y = 0; y < authored.height(); ++y)
        for(int x = 0; x < authored.width(); ++x)
        {
            const quint32 noise = quint32(x * 1103515245u)
                ^ quint32(y * 2654435761u) ^ quint32((x * y) << 3);
            authored.setPixel(x, y, qRgb(
                35 + int(noise & 127),
                45 + int((noise >> 8) & 127),
                25 + int((noise >> 16) & 127)));
        }
    QVERIFY(hasRenderedTerrainEvidence(authored, &ratio, &buckets));
    QVERIFY(ratio < 0.35);
    QVERIFY(buckets >= 64);
#else
    QSKIP("Runtime frame evidence is implemented for the shipped Windows game.");
#endif
}

void JmvMapTestLauncherTests::memoryEvidenceSummarizesComparableSamples()
{
#ifdef Q_OS_WIN
    QVector<RuntimeMemorySample> samples;
    samples.append({100, QStringLiteral("loading"), 100, 120, 80, 80,
                    300, 40, 700, 400, 50, 30, 220});
    samples.append({500, QStringLiteral("gameplay-ready"), 180, 200, 140, 140,
                    420, 50, 530, 260, 90, 40, 290});
    samples.append({900, QStringLiteral("dwell"), 170, 210, 160, 160,
                    460, 50, 490, 220, 110, 40, 310});
    const QJsonObject evidence = summarizeRuntimeMemory(samples, 9, 576);
    QCOMPARE(evidence.value("schema").toString(),
             QString("ei-runtime-memory-evidence-v2"));
    QCOMPARE(evidence.value("atlasPages").toInt(), 9);
    QCOMPARE(evidence.value("atlasSlots").toInt(), 576);
    QCOMPARE(evidence.value("sampleCount").toInt(), 3);
    QCOMPARE(evidence.value("observedPeakWorkingSetBytes").toVariant().toULongLong(),
             quint64(180));
    QCOMPARE(evidence.value("reportedPeakWorkingSetBytes").toVariant().toULongLong(),
             quint64(210));
    QCOMPARE(evidence.value("peakPrivateBytes").toVariant().toULongLong(),
             quint64(160));
    QCOMPARE(evidence.value("gameplayReadyPrivateBytes").toVariant().toULongLong(),
             quint64(140));
    QCOMPARE(evidence.value("finalPrivateBytes").toVariant().toULongLong(),
             quint64(160));
    QCOMPARE(evidence.value("peakCommittedAddressBytes").toVariant().toULongLong(),
             quint64(460));
    QCOMPARE(evidence.value("peakMappedAddressBytes").toVariant().toULongLong(),
             quint64(110));
    QCOMPARE(evidence.value("minimumLargestFreeRegionBytes").toVariant().toULongLong(),
             quint64(220));
    QCOMPARE(evidence.value("samples").toArray().size(), 3);
#else
    QSKIP("Runtime memory evidence is implemented for the shipped Windows game.");
#endif
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
    input.databaseData = readAll(qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_DATABASE_DATA",input.databaseSourcePath));
    QVERIFY2(!input.databaseData.isEmpty(),
             qPrintable("Could not read smoke database: " + input.databaseSourcePath));
    input.tablesLuaSourcePath = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_TABLES_LUA",
        QDir(modRoot).filePath("lua/tables.lua"));
    input.tablesLuaData = readAll(qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_TABLES_LUA_DATA",input.tablesLuaSourcePath));
    QVERIFY2(!input.tablesLuaData.isEmpty(),
             qPrintable("Could not read smoke tables.lua: " + input.tablesLuaSourcePath));
    // Make the staged payload observably different without changing Lua
    // semantics. GameplayReady plus the redirect/hash evidence then proves a
    // pending Lua document was loaded, rather than a byte-identical copy.
    input.tablesLuaData.append("\r\n-- EI Mapper Endurance staged-Lua smoke marker\r\n");
    const QString textureResPath = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_TEXTURE_RES");
    if(!textureResPath.isEmpty())
    {
        input.sourceTerrainTextureResource = QFileInfo(textureResPath);
        input.terrainTextureResourceData = readAll(textureResPath);
        input.expectedTerrainAtlasPages = qEnvironmentVariableIntValue(
            "EI_ENDURANCE_RUNTIME_SMOKE_ATLAS_PAGES");
        input.expectedTerrainAtlasSlots = qEnvironmentVariableIntValue(
            "EI_ENDURANCE_RUNTIME_SMOKE_ATLAS_SLOTS");
        QVERIFY2(!input.terrainTextureResourceData.isEmpty(),
                 qPrintable("Could not read smoke terrain atlas RES: "
                            + textureResPath));
        QVERIFY(input.expectedTerrainAtlasPages > 0);
        QVERIFY(input.expectedTerrainAtlasSlots > 0);
    }
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
    input.forceDrawall = qEnvironmentVariableIntValue(
        "EI_ENDURANCE_RUNTIME_SMOKE_DRAWALL") != 0;
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
    const QString externalMemoryEvidencePath = qEnvironmentVariable(
        "EI_ENDURANCE_RUNTIME_SMOKE_MEMORY_EVIDENCE");

    CJmvMapTestLauncher launcher;
    QString error;
    QVERIFY2(launcher.launch(input, &error), qPrintable(error));
    const QString runId = launcher.runId();
    const QString runRoot = QDir(CJmvMapTestLauncher::testRunsRootPath())
        .filePath(runId);
#ifdef Q_OS_WIN
    QElapsedTimer memoryClock;
    memoryClock.start();
    QVector<RuntimeMemorySample> memorySamples;
    qint64 lastMemorySampleMs = -1000;
    qint64 lastMemoryPersistMs = -1000;
    const auto persistMemory = [&](bool force) {
        const qint64 elapsed = memoryClock.elapsed();
        if(!force && elapsed - lastMemoryPersistMs < 1000)
            return;
        const QByteArray bytes = QJsonDocument(summarizeRuntimeMemory(
            memorySamples, input.expectedTerrainAtlasPages,
            input.expectedTerrainAtlasSlots)).toJson(QJsonDocument::Indented);
        const auto writeSnapshot = [&bytes](const QString& path) {
            if(path.isEmpty()) return true;
            QSaveFile file(path);
            return file.open(QIODevice::WriteOnly)
                && file.write(bytes) == bytes.size() && file.commit();
        };
        writeSnapshot(QDir(runRoot).filePath("runtime-memory-evidence.json"));
        writeSnapshot(externalMemoryEvidencePath);
        lastMemoryPersistMs = elapsed;
    };
    const auto collectMemory = [&](const QString& phase, bool force = false) {
        const qint64 elapsed = memoryClock.elapsed();
        if(!force && elapsed - lastMemorySampleMs < 250)
            return;
        RuntimeMemorySample sample;
        if(sampleRuntimeMemory(launcher.activeGameProcessId(), elapsed,
                               phase, &sample))
        {
            memorySamples.append(sample);
            lastMemorySampleMs = elapsed;
            persistMemory(force);
        }
    };
#endif
    QElapsedTimer timeout;
    timeout.start();
    while(timeout.elapsed() < 45000 && launcher.isRunning()
          && launcher.runtimeState() != "GameplayReady")
    {
#ifdef Q_OS_WIN
        collectMemory(QStringLiteral("loading"));
#endif
        QTest::qWait(50);
    }
    const QString finalState = launcher.runtimeState();
#ifdef Q_OS_WIN
    collectMemory(QStringLiteral("gameplay-ready"), true);
#endif
    int dwellMs = qEnvironmentVariableIntValue("EI_ENDURANCE_RUNTIME_SMOKE_DWELL_MS");
    if(dwellMs <= 0)
        dwellMs = 15000;
    QElapsedTimer dwell;
    dwell.start();
    bool responsiveDuringDwell = true;
    int consecutiveHungSamples = 0;
    while(dwell.elapsed() < dwellMs && launcher.isRunning()
          && launcher.runtimeState() == "GameplayReady")
    {
#ifdef Q_OS_WIN
        collectMemory(QStringLiteral("dwell"));
        if(runtimeWindowResponsive(launcher.activeGameProcessId()))
            consecutiveHungSamples = 0;
        else if(++consecutiveHungSamples >= 3)
        {
            responsiveDuringDwell = false;
            break;
        }
#endif
        QTest::qWait(250);
    }
    const bool survivedDwell = launcher.isRunning()
        && launcher.runtimeState() == "GameplayReady"
        && responsiveDuringDwell;
    QVERIFY2(!launcher.runtimeFailureObserved(),
             qPrintable(QStringLiteral("A native runtime failure was observed: %1")
                        .arg(launcher.runtimeFailureEvidence())));
    bool renderedTerrain = true;
    double dominantColorRatio = 0.0;
    int renderedColorBuckets = 0;
#ifdef Q_OS_WIN
    collectMemory(QStringLiteral("capture"), true);
    const HWND evidenceWindow = runtimeWindow(launcher.activeGameProcessId());
    if(evidenceWindow && IsIconic(evidenceWindow))
    {
        ShowWindowAsync(evidenceWindow, SW_RESTORE);
        SetForegroundWindow(evidenceWindow);
        QTest::qWait(1500);
        collectMemory(QStringLiteral("capture-restored"), true);
    }
    const QImage runtimeFrame = captureRuntimeWindow(launcher.activeGameProcessId());
    renderedTerrain = hasRenderedTerrainEvidence(runtimeFrame,
        &dominantColorRatio, &renderedColorBuckets);
    QVERIFY2(!runtimeFrame.isNull(),
             "Could not capture the owned game window for terrain-render validation.");
    const QString runtimeFramePath = QDir(runRoot).filePath("runtime-frame.png");
    QVERIFY2(runtimeFrame.save(runtimeFramePath),
             "Could not preserve the terrain-render validation frame.");
    QJsonObject frameEvidence;
    frameEvidence.insert("schema", "ei-runtime-terrain-frame-evidence-v1");
    frameEvidence.insert("framePath", runtimeFramePath);
    frameEvidence.insert("frameSha256", hash([&runtimeFramePath]() {
        QFile frame(runtimeFramePath);
        return frame.open(QIODevice::ReadOnly) ? frame.readAll() : QByteArray();
    }()));
    frameEvidence.insert("width", runtimeFrame.width());
    frameEvidence.insert("height", runtimeFrame.height());
    frameEvidence.insert("dominantQuantizedColorRatio", dominantColorRatio);
    frameEvidence.insert("quantizedColorBuckets", renderedColorBuckets);
    frameEvidence.insert("renderedTerrainEvidence", renderedTerrain);
    const QString frameEvidencePath = QDir(runRoot).filePath(
        "runtime-frame-evidence.json");
    QSaveFile frameEvidenceFile(frameEvidencePath);
    QVERIFY(frameEvidenceFile.open(QIODevice::WriteOnly));
    const QByteArray frameEvidenceBytes = QJsonDocument(frameEvidence).toJson(
        QJsonDocument::Indented);
    QCOMPARE(frameEvidenceFile.write(frameEvidenceBytes),
             qint64(frameEvidenceBytes.size()));
    QVERIFY(frameEvidenceFile.commit());
    qInfo().noquote() << "RUNTIME_FRAME_EVIDENCE"
                      << QDir::toNativeSeparators(frameEvidencePath)
                      << "rendered=" << renderedTerrain
                      << "dominant=" << dominantColorRatio
                      << "buckets=" << renderedColorBuckets;

    QVERIFY2(!memorySamples.isEmpty(),
             "Could not collect owned game process memory evidence.");
    const QJsonObject memoryEvidence = summarizeRuntimeMemory(
        memorySamples, input.expectedTerrainAtlasPages,
        input.expectedTerrainAtlasSlots);
    const QByteArray memoryEvidenceBytes = QJsonDocument(memoryEvidence).toJson(
        QJsonDocument::Indented);
    const auto writeMemoryEvidence = [&memoryEvidenceBytes](const QString& path) {
        if(path.isEmpty())
            return true;
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly)
            && file.write(memoryEvidenceBytes) == memoryEvidenceBytes.size()
            && file.commit();
    };
    const QString runMemoryEvidencePath = QDir(runRoot).filePath(
        "runtime-memory-evidence.json");
    QVERIFY2(writeMemoryEvidence(runMemoryEvidencePath),
             "Could not preserve run-local memory evidence.");
    QVERIFY2(writeMemoryEvidence(externalMemoryEvidencePath),
             "Could not preserve requested external memory evidence.");
    const double mib = 1024.0 * 1024.0;
    qInfo().noquote() << "RUNTIME_MEMORY_EVIDENCE"
                      << QDir::toNativeSeparators(externalMemoryEvidencePath.isEmpty()
                          ? runMemoryEvidencePath : externalMemoryEvidencePath)
                      << "samples=" << memoryEvidence.value("sampleCount").toInt()
                      << "peakWorkingSetMiB="
                      << memoryEvidence.value("observedPeakWorkingSetBytes")
                             .toVariant().toULongLong() / mib
                      << "reportedPeakWorkingSetMiB="
                      << memoryEvidence.value("reportedPeakWorkingSetBytes")
                             .toVariant().toULongLong() / mib
                      << "peakPrivateMiB="
                      << memoryEvidence.value("peakPrivateBytes")
                             .toVariant().toULongLong() / mib
                      << "finalPrivateMiB="
                      << memoryEvidence.value("finalPrivateBytes")
                             .toVariant().toULongLong() / mib;
#endif
    const QHash<QString, DumpStamp> dumpsAfter = snapshotDumps(dumpDirectory);
    QStringList newDumps;
    for(auto it = dumpsAfter.constBegin(); it != dumpsAfter.constEnd(); ++it)
    {
        const auto before = dumpsBefore.constFind(it.key());
        if(before == dumpsBefore.constEnd()
           || before->size != it->size || before->modified != it->modified)
            newDumps.append(it.key());
    }
    launcher.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!launcher.isRunning(), 10000);
    QCOMPARE(finalState, QString("GameplayReady"));
    QVERIFY2(survivedDwell,
             responsiveDuringDwell
             ? "The game did not remain in GameplayReady for the required post-load dwell."
             : "The game window stopped responding during the required post-load dwell.");
    QVERIFY2(newDumps.isEmpty(),
             qPrintable("The game wrote a native dump after GameplayReady: "
                        + newDumps.join(", ")));
    QVERIFY2(renderedTerrain,
             qPrintable(QString("The loaded map has no credible rendered-terrain evidence "
                                "(dominant quantized color=%1%, buckets=%2).")
                        .arg(dominantColorRatio * 100.0, 0, 'f', 1)
                        .arg(renderedColorBuckets)));
    QVERIFY2(!runId.isEmpty(), "Runtime smoke did not create a run ID.");
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(
        QDir(runRoot).filePath("result.json")), 10000);
    QJsonObject authoritative;
    QStringList findings;
    QVERIFY2(CJmvMapTestLauncher::analyzeCompletedRun(
                 runRoot, &authoritative, &findings),
             qPrintable(findings.join('\n')));
    qInfo().noquote() << "RUNTIME_AUTHORITATIVE_EVIDENCE"
                      << "passed=" << authoritative.value("passed").toBool()
                      << "pages=" << authoritative.value("terrainAtlasPages").toInt()
                      << "slots=" << authoritative.value("terrainAtlasSlots").toInt()
                      << "playerCount=" << authoritative.value("reportedPlayerCount").toInt()
                      << "zone=" << authoritative.value("actualZone").toString()
                      << "runId=" << authoritative.value("runId").toString();
    if(!input.terrainTextureResourceData.isEmpty())
    {
        QCOMPARE(authoritative.value("terrainAtlasPages").toInt(),
                 input.expectedTerrainAtlasPages);
        QCOMPARE(authoritative.value("terrainAtlasSlots").toInt(),
                 input.expectedTerrainAtlasSlots);
        QVERIFY(authoritative.value("textureResourceRegistered").toBool());
        QVERIFY(authoritative.value("textureProbeResolved").toBool());
        QVERIFY(authoritative.value("customAtlasContractValidated").toBool());
        if(input.expectedTerrainAtlasPages > 8
           || input.expectedTerrainAtlasSlots > 512)
            QVERIFY(authoritative.value("expandedAtlasContractValidated").toBool());
    }
    if(input.forceDrawall)
    {
        QVERIFY(authoritative.value("drawallAutoexecRedirected").toBool());
        QVERIFY(authoritative.value("drawallAutoexecConsumed").toBool());
        QVERIFY(authoritative.value("drawallContractValidated").toBool());
    }
}

void JmvMapTestLauncherTests::runtimeSmokeRecoveryClosesOwnedRun()
{
    QString description;
    if(!CJmvMapTestLauncher::hasActiveInterruptedRun(&description))
    {
        bool pending = false;
        const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns(&pending);
        QVERIFY2(!pending, qPrintable(summary));
        return;
    }
    QString error;
    QVERIFY2(CJmvMapTestLauncher::requestCloseActiveInterruptedRun(&error),
             qPrintable(error + QLatin1Char('\n') + description));
    QElapsedTimer timeout;
    timeout.start();
    while(timeout.elapsed() < 15000
          && CJmvMapTestLauncher::hasActiveInterruptedRun(nullptr))
        QTest::qWait(100);
    bool pending = false;
    const QString summary = CJmvMapTestLauncher::recoverInterruptedRuns(&pending);
    QVERIFY2(!pending, qPrintable(summary));
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
    manifest.insert("temporaryMapName", "ed12ab34");
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

void JmvMapTestLauncherTests::authoritativeResultRejectsStaleReadyWithoutControlledPlayer()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    QString stale = successfulBridgeEvidence(temp.path());
    stale.remove(QRegularExpression(
        "[^\\n]*DIRECT_LAUNCH initialized New Game player[^\\n]*\\n"));
    const QJsonObject report = CJmvMapTestLauncher::buildAuthoritativeResult(
        resultManifest(temp.path(), source), stale, 0,
        int(QProcess::NormalExit), false, "Removed all owned runtime artifacts");
    QVERIFY(!report.value("passed").toBool());
    QCOMPARE(report.value("reportedPlayerCount").toInt(), 0);
    QVERIFY(!report.value("controlledPlayerConfirmed").toBool());
    QStringList findings;
    QVERIFY(!CJmvMapTestLauncher::validateAutomatedResult(report, &findings));
    QVERIFY(findings.join('\n').contains("controlled player", Qt::CaseInsensitive));
}

void JmvMapTestLauncherTests::recognizesNativeRuntimeFailureDialogs()
{
    QVERIFY(CJmvMapTestLauncher::isRuntimeFailureDialog(
        "Microsoft Visual C++ Runtime Library", "#32770",
        "Runtime Error! Program: D:\\Evil Islands\\Starter\\Engine\\game.exe "
        "abnormal program termination"));
    QVERIFY(CJmvMapTestLauncher::isRuntimeFailureDialog(
        "game.exe - Application Error", "#32770", "The application was unable to start"));
    QVERIFY(!CJmvMapTestLauncher::isRuntimeFailureDialog(
        "Microsoft Visual C++ Runtime Library", "JMV Main Window", "Runtime Error!"));
    QVERIFY(!CJmvMapTestLauncher::isRuntimeFailureDialog(
        "Evil Islands", "#32770", "Choose a rendering mode"));
}

void JmvMapTestLauncherTests::authoritativeResultRejectsObservedFailureAfterStop()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    QJsonObject manifest = resultManifest(temp.path(), source);
    manifest.insert("runtimeFailureObserved", true);
    manifest.insert("runtimeFailureEvidence",
                    "Microsoft Visual C++ Runtime Library: abnormal program termination");
    const QJsonObject report = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, successfulBridgeEvidence(temp.path()), 62097,
        int(QProcess::CrashExit), true, "Removed all owned runtime artifacts");
    QVERIFY(!report.value("passed").toBool());
    QVERIFY(report.value("runtimeFailureObserved").toBool());
    QVERIFY(QJsonDocument(report.value("errors").toArray()).toJson()
            .contains("native runtime failure"));
    QStringList findings;
    QVERIFY(!CJmvMapTestLauncher::validateAutomatedResult(report, &findings));
}

void JmvMapTestLauncherTests::authoritativeResultRequiresExpandedAtlasRegistration()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    QJsonObject manifest = resultManifest(temp.path(), source);
    QJsonObject probe;
    probe.insert("mode", "run-owned-expanded-terrain-atlas");
    probe.insert("texturePages", 9);
    probe.insert("activeSlots", 576);
    probe.insert("resourceMmpMembers", 9);
    probe.insert("textureProbeName", "ed12ab34008");
    probe.insert("textureProbeMemberName", "ed12ab34008.mmp");
    probe.insert("runtimeNamespace", "ed12ab34");
    probe.insert("exceedsLegacyEightPages", true);
    probe.insert("installedArchivesModified", false);
    manifest.insert("terrainAtlasRuntimeProbe", probe);

    const QString baseEvidence = successfulBridgeEvidence(temp.path());
    QJsonObject rejected = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, baseEvidence, 0, int(QProcess::NormalExit), false,
        "Removed all owned runtime artifacts");
    QVERIFY(!rejected.value("passed").toBool());
    QVERIFY(!rejected.value("expandedAtlasContractValidated").toBool());

    const QJsonObject registeredOnly = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, QString("TEMP_TEXTURE_RES registered path=probe.res profile=jmv "
                          "installed_archives_modified=false\n") + baseEvidence,
        0, int(QProcess::NormalExit), false,
        "Removed all owned runtime artifacts");
    QVERIFY(!registeredOnly.value("passed").toBool());
    QVERIFY(!registeredOnly.value("textureProbeResolved").toBool());

    QString completeTextureEvidence =
        "TEMP_TEXTURE_RES probe resolved name=\"ed12ab34008.mmp\" "
        "width=1024 height=1024 source=run-owned\n"
        "TEMP_TEXTURE_RES armed terrain texture-request observation\n"
        "TEMP_TEXTURE_RES registered path=probe.res profile=jmv "
        "installed_archives_modified=false\n";
    for(int page = 0; page < 9; ++page)
        completeTextureEvidence += QString(
            "TERRAIN_TEXTURE_REQUEST name=\"ed12ab34%1.mmp\" "
            "result=resolved width=1024 height=1024\n")
            .arg(page, 3, 10, QLatin1Char('0'));
    const QJsonObject accepted = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, completeTextureEvidence + baseEvidence,
        0, int(QProcess::NormalExit), false,
        "Removed all owned runtime artifacts");
    QCOMPARE(accepted.value("terrainAtlasPages").toInt(), 9);
    QCOMPARE(accepted.value("terrainAtlasSlots").toInt(), 576);
    QVERIFY(accepted.value("textureResourceRegistered").toBool());
    QVERIFY(accepted.value("textureProbeResolved").toBool());
    QCOMPARE(accepted.value("resolvedTerrainPageRequests").toInt(), 9);
    QVERIFY(accepted.value("allTerrainPageRequestsResolved").toBool());
    QVERIFY(accepted.value("expandedAtlasContractValidated").toBool());
    QStringList findings;
    QVERIFY2(CJmvMapTestLauncher::validateAutomatedResult(accepted, &findings),
             qPrintable(findings.join('\n')));
}

void JmvMapTestLauncherTests::authoritativeResultRequiresDrawallStagingEvidence()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    QJsonObject manifest = resultManifest(temp.path(), source);
    QJsonObject drawall;
    drawall.insert("requested", true);
    drawall.insert("command", "drawall 1");
    drawall.insert("installedConfigurationModified", false);
    drawall.insert("ownedPath", QDir(temp.path()).filePath("2utoexec.cfg"));
    drawall.insert("ownedSha256", "drawall-owned-hash");
    manifest.insert("drawallStress", drawall);

    const QString baseEvidence = successfulBridgeEvidence(temp.path());
    const QJsonObject rejected = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, baseEvidence, 0, int(QProcess::NormalExit), false,
        "Removed all owned runtime artifacts");
    QVERIFY(!rejected.value("passed").toBool());
    QVERIFY(!rejected.value("drawallContractValidated").toBool());

    const QString drawallEvidence =
        "DRAWALL_AUTOEXEC redirect from=\"D:\\\\Evil Islands\\\\Starter\\\\Engine\\\\2utoexec.cfg\" "
        "to=\"C:\\\\owned\\\\2utoexec.cfg\" command=\"drawall 1\" installed_modified=false\n"
        "DRAWALL_AUTOEXEC consumed bytes=91 sha256=drawall-owned-hash\n";
    const QJsonObject accepted = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, drawallEvidence + baseEvidence, 0, int(QProcess::NormalExit), false,
        "Removed all owned runtime artifacts");
    QVERIFY(accepted.value("drawallRequested").toBool());
    QVERIFY(accepted.value("drawallAutoexecRedirected").toBool());
    QVERIFY(accepted.value("drawallAutoexecConsumed").toBool());
    QVERIFY(accepted.value("drawallContractValidated").toBool());
    QVERIFY(accepted.value("passed").toBool());
}

void JmvMapTestLauncherTests::preparesRunOwnedDrawallAutoexec()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("installed/2utoexec.cfg");
    QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
    const QByteArray original("thingamabob\r\nfps 0\r\n");
    writeFile(source, original);
    const QString runRoot = QDir(temp.path()).filePath("run");
    QVERIFY(QDir().mkpath(runRoot));
    QJsonObject manifest;
    QString owned;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::prepareRunOwnedDrawallAutoexec(
                 source, runRoot, true, &manifest, &owned, &error),
             qPrintable(error));
    QFile unchanged(source);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
    QFile staged(owned);
    QVERIFY(staged.open(QIODevice::ReadOnly));
    const QByteArray stagedBytes = staged.readAll();
    QVERIFY(stagedBytes.startsWith(original));
    QVERIFY(stagedBytes.contains("drawall 1"));
    const QJsonObject drawall = manifest.value("drawallStress").toObject();
    QVERIFY(drawall.value("requested").toBool());
    QCOMPARE(drawall.value("command").toString(), QString("drawall 1"));
    QCOMPARE(drawall.value("sourceSha256").toString(), hash(original));
    QCOMPARE(drawall.value("ownedSha256").toString(), hash(stagedBytes));
    QVERIFY(!drawall.value("installedConfigurationModified").toBool());
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

void JmvMapTestLauncherTests::authoritativeResultRequiresStagedTablesLuaRead()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = QDir(temp.path()).filePath("source.mob");
    writeFile(source, QByteArray("source map"));
    QJsonObject manifest = resultManifest(temp.path(), source);
    QJsonObject tablesLua;
    tablesLua.insert("sourcePath", "D:/JMV/lua/tables.lua");
    tablesLua.insert("stagedPath", QDir(temp.path()).filePath("database/tables.lua"));
    tablesLua.insert("stagedSha256", QStringLiteral("pending-lua-hash"));
    tablesLua.insert("loadEvidencePath", QDir(temp.path()).filePath("tables-lua-loaded.txt"));
    manifest.insert("tablesLua", tablesLua);

    const QJsonObject missing = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, successfulBridgeEvidence(temp.path()), 0,
        int(QProcess::NormalExit), false, QString());
    QVERIFY(!missing.value("passed").toBool());
    QVERIFY(!missing.value("tablesLuaLoadObserved").toBool());
    QVERIFY(QJsonDocument(missing.value("errors").toArray()).toJson()
            .contains("staged tables.lua was not read"));

    writeFile(tablesLua.value("loadEvidencePath").toString(), QByteArray("pending-lua-hash\n"));
    const QString observed = successfulBridgeEvidence(temp.path());
    const QJsonObject accepted = CJmvMapTestLauncher::buildAuthoritativeResult(
        manifest, observed, 0, int(QProcess::NormalExit), false, QString());
    QVERIFY(accepted.value("tablesLuaLoadObserved").toBool());
    QVERIFY(accepted.value("passed").toBool());
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

#include "main_window.h"
#include <QApplication>
#include <QProgressBar>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QIcon>
#include <QCommandLineParser>
#include <QDialog>
#include <QMessageBox>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QDir>
#include <QTabWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QLabel>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QUndoStack>
#include <QClipboard>
#include <QMimeData>
#include <QMenu>
#include <QPushButton>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QSlider>
#include <QScrollArea>
#include <QFormLayout>
#include <QEventLoop>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QRegularExpression>
#include <QWidget>
#include <QVariant>
#include <QSettings>
#include <QSharedPointer>
#include <functional>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGraphicsView>
#include <QMainWindow>
#include <QDockWidget>
#include <QTabBar>
#include <QThread>
#include <cstdio>
#include <cstring>

#include "endurance_document.h"
#include "log.h"
#include "resource_profile.h"
#include "resourcemanager.h"
#include "layout_components/tablemanager.h"
#include "mob/mob_parameters.h"
#include "core/undo.h"
#include "layout_components/tree_view.h"
#include "layout_components/viewport_widget.h"
#include "ui/landscape_tools.h"
#include "ui/bodypartedit_form.h"
#include "ui/validation_panel_layout.h"
#include "ui/message_log_panel.h"
#include "ui/dock_hidden_tab.h"
#include "ui/camera_telephoto_controls.h"
#include "camera_telephoto.h"
#include "ui/viewport_minimap_widget.h"
#include "ui/minimap_export_dialog.h"
#include "ui/resource_setup_dialog.h"
#include "ui/resource_browser.h"
#include "ui/item_spell_constructor_dialog.h"
#include "objects/object_base.h"
#include "ui/tile_form.h"
#include "window_lifecycle.h"
#include "settings.h"
#include "endurance_ui_scale.h"
#include "localization.h"
#include "jmv_map_test_launcher.h"
#include "view.h"
#include "preview.h"
#include "render_scheduling.h"
#include "landscape_operation_policy.h"
#include "utils.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace {
QByteArray resourceNavigationArchiveBytes()
{
    struct Member { QByteArray name; QByteArray payload; quint32 offset; };
    QVector<Member> members;
    members << Member{QByteArray("z-target.mod"),QByteArray("model"),0}
            << Member{QByteArray("a-unrelated.wav"),QByteArray("audio"),0};
    for(int index=0;index<1000;++index)
        members<<Member{QString("bulk-%1.wav").arg(index,4,10,QLatin1Char('0')).toLatin1(),QByteArray("x"),0};
    QByteArray bytes(16,'\0'),names;
    for(Member& member:members)
    {
        bytes.append(QByteArray((16-bytes.size()%16)%16,'\0'));
        member.offset=quint32(bytes.size());bytes.append(member.payload);
    }
    bytes.append(QByteArray((16-bytes.size()%16)%16,'\0'));
    const quint32 tableOffset=quint32(bytes.size());QByteArray tableBytes;
    QDataStream table(&tableBytes,QIODevice::WriteOnly);table.setByteOrder(QDataStream::LittleEndian);
    for(int index=0;index<members.size();++index)
    {
        const Member& member=members.at(index);
        table<<quint32(-1)<<quint32(member.payload.size())<<member.offset
             <<quint32(0x12340000+index)<<quint16(member.name.size())<<quint32(names.size());
        names.append(member.name);
    }
    bytes.append(tableBytes);bytes.append(names);QDataStream header(&bytes,QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);header<<quint32(0x019CE23C)
          <<quint32(members.size())<<tableOffset<<quint32(names.size());return bytes;
}

bool resourceNavigationExactTargetCheck()
{
    QTemporaryDir directory;if(!directory.isValid())return false;
    const QString archive=directory.filePath(QStringLiteral("navigation.res"));
    QFile file(archive);const QByteArray bytes=resourceNavigationArchiveBytes();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return false;
    file.close();CResourceBrowser browser;
    QTreeWidget* tree=browser.findChild<QTreeWidget*>(QStringLiteral("resourceContentView"));
    browser.setActiveProfileSources({archive},QStringLiteral("test"));
    QElapsedTimer wait;wait.start();
    while(tree&&tree->topLevelItemCount()<2&&wait.elapsed()<3000)
    {QApplication::processEvents();QThread::msleep(5);}
    if(!tree)return false;
    tree->sortItems(0,Qt::DescendingOrder);
    QElapsedTimer navigation;navigation.start();
    const bool selected=browser.selectResourceReference(QStringLiteral("z-target.mod"));
    const qint64 navigationMs=navigation.elapsed();
    return selected&&navigationMs<100&&tree->topLevelItemCount()<=2
        &&tree->currentItem()&&tree->currentItem()->text(0)==QStringLiteral("z-target.mod");
}

class CMousePressProbe final: public QWidget
{
public:
    int presses = 0;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        ++presses;
        event->accept();
    }
};

class CPaintProbe final: public QWidget
{
public:
    int paints{0};

protected:
    void paintEvent(QPaintEvent* event) override
    {
        ++paints;
        QWidget::paintEvent(event);
    }
};
QtMessageHandler g_previousQtMessageHandler = nullptr;

void enduranceQtMessageHandler(QtMsgType type,
                               const QMessageLogContext& context,
                               const QString& message)
{
    const QString category = QString::fromLatin1(
        context.category ? context.category : "");
    // Qt 5.12's Windows platform/accessibility plugins can repeatedly emit
    // non-actionable warnings during otherwise successful editor operation.
    // Keep application/default-category diagnostics visible and suppress only
    // those known framework categories.
    if((type == QtDebugMsg || type == QtInfoMsg || type == QtWarningMsg)
       && (category.startsWith(QStringLiteral("qt.qpa."))
           || category.startsWith(QStringLiteral("qt.accessibility"))))
        return;
    if(g_previousQtMessageHandler)
        g_previousQtMessageHandler(type, context, message);
    else
    {
        const QByteArray encoded = message.toLocal8Bit();
        std::fprintf(stderr, "%s\n", encoded.constData());
        std::fflush(stderr);
    }
}

class CStartupSplash final: public QWidget
{
public:
    CStartupSplash():
        QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint |
                Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                Qt::WindowMinimizeButtonHint),
        m_art(QStringLiteral(":/endurance_splash.png")),
        m_status(tr("Initializing editor..."))
    {
        setWindowTitle(tr("EI Mapper: Endurance Edition"));
        setWindowIcon(QIcon(QStringLiteral(":/endurance_gate.png")));
        setFixedSize(960, 400);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void showCentered()
    {
        if(QScreen* screen = QGuiApplication::primaryScreen())
        {
            qApp->setProperty("enduranceStartupScreenGeometry",
                              screen->availableGeometry());
            move(screen->availableGeometry().center() - rect().center());
        }
        show();
        raise();
        activateWindow();
    }

    void setStatus(const QString& status)
    {
        m_status = status;
        update();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QWidget::mousePressEvent(event);
        // Treat the splash like the application's taskbar surface: restore and
        // raise every already-visible Endurance top-level window, then leave
        // the active modal dialog (first-run setup or its confirmation) on top.
        raise();
        activateWindow();
        QWidget* modal = QApplication::activeModalWidget();
        const auto windows = QApplication::topLevelWidgets();
        for(QWidget* window : windows)
        {
            if(!window || window == this || window == modal
               || (!window->isVisible() && !window->isMinimized()))
                continue;
            if(window->isMinimized())
                window->showNormal();
            window->raise();
        }
        if(modal)
        {
            if(modal->isMinimized())
                modal->showNormal();
            modal->raise();
            modal->activateWindow();
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if(!m_art.isNull())
            painter.drawPixmap(rect(), m_art);
        else
            painter.fillRect(rect(), QColor(20, 20, 20));

        painter.fillRect(QRect(0, 0, 520, 150), QColor(0, 0, 0, 168));
        painter.fillRect(QRect(0, height() - 48, width(), 48), QColor(0, 0, 0, 190));

        QFont titleFont = painter.font();
        titleFont.setPointSize(28);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(Qt::white);
        painter.drawText(QRect(30, 22, 470, 48), Qt::AlignLeft | Qt::AlignVCenter,
                         tr("EI Mapper"));

        QFont subtitleFont = painter.font();
        subtitleFont.setPointSize(17);
        subtitleFont.setBold(false);
        painter.setFont(subtitleFont);
        painter.setPen(QColor(218, 190, 130));
        painter.drawText(QRect(32, 70, 468, 34), Qt::AlignLeft | Qt::AlignVCenter,
                         tr("Endurance Edition"));

        QFont detailFont = painter.font();
        detailFont.setPointSize(10);
        painter.setFont(detailFont);
        painter.setPen(QColor(220, 220, 220));
        painter.drawText(QRect(33, 108, 465, 24), Qt::AlignLeft | Qt::AlignVCenter,
                         tr("Version %1").arg(QCoreApplication::applicationVersion()));

        QFont statusFont = painter.font();
        statusFont.setPointSize(10);
        painter.setFont(statusFont);
        painter.setPen(Qt::white);
        painter.drawText(QRect(24, height() - 48, width() - 48, 48),
                         Qt::AlignLeft | Qt::AlignVCenter, m_status);
    }

private:
    QPixmap m_art;
    QString m_status;
};

class CEnduranceStyle final: public QProxyStyle
{
public:
    explicit CEnduranceStyle(QStyle* baseStyle): QProxyStyle(baseStyle) {}

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override
    {
        // Fusion's etched disabled text paints a bright offset copy beneath
        // the glyph. On a dark menu that looks like black text with a white
        // shadow instead of a disabled item. Draw the palette's grey once.
        if(hint == QStyle::SH_EtchDisabledText)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

class CCounterUndoCommand final: public QUndoCommand
{
public:
    CCounterUndoCommand(int* value, int delta): m_value(value), m_delta(delta) {}
    void undo() override { if(m_value) *m_value -= m_delta; }
    void redo() override { if(m_value) *m_value += m_delta; }

private:
    int* m_value;
    int m_delta;
};

const char* requestedOpenGlBackend(int argc, char* argv[])
{
    for(int i = 1; i < argc; ++i)
    {
        if(std::strcmp(argv[i], "--angle-opengl") == 0)
            return "angle";
        if(std::strcmp(argv[i], "--desktop-opengl") == 0)
            return "desktop";
    }
    return "automatic";
}

bool renderBenchmarkRequested(int argc, char* argv[])
{
    for(int i = 1; i < argc; ++i)
        if(std::strcmp(argv[i], "--render-benchmark") == 0)
            return true;
    return false;
}

bool automatedWorkspaceSmokeRequested(int argc, char* argv[])
{
    for(int i = 1; i < argc; ++i)
        if(std::strcmp(argv[i], "--workspace-load-smoke") == 0
           || std::strcmp(argv[i], "--workspace-visual-smoke") == 0
           || std::strcmp(argv[i], "--workspace-visual-sequence") == 0)
            return true;
    return false;
}

bool persistedRenderVsync()
{
    QFile options("endurance-options.json");
    if(!options.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;
    return ei::renderVsyncFromOptions(options.readAll(), true);
}
#ifdef Q_OS_WIN
const char* accessViolationOperation(const EXCEPTION_RECORD* record)
{
    if(!record || record->NumberParameters < 2)
        return "unknown";
    switch(record->ExceptionInformation[0])
    {
    case 0: return "read";
    case 1: return "write";
    case 8: return "execute";
    default: return "unknown";
    }
}

LONG WINAPI enduranceUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
    const EXCEPTION_RECORD* exceptionRecord = exceptionInfo ? exceptionInfo->ExceptionRecord : nullptr;
    const DWORD code = exceptionRecord ? exceptionRecord->ExceptionCode : 0;
    const ULONG_PTR address = exceptionRecord
            ? reinterpret_cast<ULONG_PTR>(exceptionRecord->ExceptionAddress) : 0;
    const ULONG_PTR target = exceptionRecord && exceptionRecord->NumberParameters >= 2
            ? exceptionRecord->ExceptionInformation[1] : 0;

    MEMORY_BASIC_INFORMATION memoryInfo = {};
    const bool foundAllocation = address != 0
            && VirtualQuery(reinterpret_cast<LPCVOID>(address), &memoryInfo, sizeof(memoryInfo)) == sizeof(memoryInfo);
    const HMODULE faultModule = foundAllocation ? static_cast<HMODULE>(memoryInfo.AllocationBase) : nullptr;
    const ULONG_PTR moduleBase = reinterpret_cast<ULONG_PTR>(faultModule);
    wchar_t modulePathW[MAX_PATH] = L"unknown";
    if(faultModule)
        GetModuleFileNameW(faultModule, modulePathW, MAX_PATH);
    const wchar_t* moduleNameW = modulePathW;
    for(const wchar_t* cursor = modulePathW; *cursor; ++cursor)
    {
        if(*cursor == L'\\' || *cursor == L'/')
            moduleNameW = cursor + 1;
    }
    char moduleName[4 * MAX_PATH] = "unknown";
    WideCharToMultiByte(CP_UTF8, 0, moduleNameW, -1, moduleName, int(sizeof(moduleName)), nullptr, nullptr);
    SYSTEMTIME now;
    GetLocalTime(&now);

    CreateDirectoryW(L"logs", nullptr);
    wchar_t dumpPath[256];
    std::swprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]),
            L"logs\\crash-%04u%02u%02u-%02u%02u%02u-p%lu-t%lu.dmp",
            unsigned(now.wYear), unsigned(now.wMonth), unsigned(now.wDay),
            unsigned(now.wHour), unsigned(now.wMinute), unsigned(now.wSecond),
            static_cast<unsigned long>(GetCurrentProcessId()), static_cast<unsigned long>(GetCurrentThreadId()));
    bool dumpWritten = false;
    DWORD dumpError = ERROR_SUCCESS;
    HANDLE dumpFile = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(dumpFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION dumpException = {};
        dumpException.ThreadId = GetCurrentThreadId();
        dumpException.ExceptionPointers = exceptionInfo;
        dumpException.ClientPointers = FALSE;
        dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                MiniDumpNormal, exceptionInfo ? &dumpException : nullptr, nullptr, nullptr) != FALSE;
        if(!dumpWritten)
            dumpError = GetLastError();
        FlushFileBuffers(dumpFile);
        CloseHandle(dumpFile);
    }
    else
        dumpError = GetLastError();

    char registers[1024] = "registers=unavailable";
    if(exceptionInfo && exceptionInfo->ContextRecord)
    {
#if defined(_M_X64) || defined(__x86_64__)
        const CONTEXT* context = exceptionInfo->ContextRecord;
        std::snprintf(registers, sizeof(registers),
                "rip=0x%llx rsp=0x%llx rbp=0x%llx rax=0x%llx rbx=0x%llx rcx=0x%llx rdx=0x%llx",
                static_cast<unsigned long long>(context->Rip), static_cast<unsigned long long>(context->Rsp),
                static_cast<unsigned long long>(context->Rbp), static_cast<unsigned long long>(context->Rax),
                static_cast<unsigned long long>(context->Rbx), static_cast<unsigned long long>(context->Rcx),
                static_cast<unsigned long long>(context->Rdx));
#elif defined(_M_IX86) || defined(__i386__)
        const CONTEXT* context = exceptionInfo->ContextRecord;
        std::snprintf(registers, sizeof(registers),
                "eip=0x%lx esp=0x%lx ebp=0x%lx eax=0x%lx ebx=0x%lx ecx=0x%lx edx=0x%lx",
                static_cast<unsigned long>(context->Eip), static_cast<unsigned long>(context->Esp),
                static_cast<unsigned long>(context->Ebp), static_cast<unsigned long>(context->Eax),
                static_cast<unsigned long>(context->Ebx), static_cast<unsigned long>(context->Ecx),
                static_cast<unsigned long>(context->Edx));
#endif
    }

    char record[4096];
    const int length = std::snprintf(record, sizeof(record),
            "[%02u.%02u.%04u %02u:%02u:%02u] {CRASH} code=0x%08lx address=0x%p module=%s module_base=0x%p module_offset=0x%llx access=%s target=0x%p thread=%lu context=%s dump=%s dump_error=%lu %s\r\n",
            unsigned(now.wDay), unsigned(now.wMonth), unsigned(now.wYear),
            unsigned(now.wHour), unsigned(now.wMinute), unsigned(now.wSecond),
            static_cast<unsigned long>(code), reinterpret_cast<void*>(address), moduleName,
            reinterpret_cast<void*>(moduleBase),
            static_cast<unsigned long long>(moduleBase && address >= moduleBase ? address - moduleBase : 0),
            code == EXCEPTION_ACCESS_VIOLATION ? accessViolationOperation(exceptionRecord) : "n/a",
            reinterpret_cast<void*>(target), static_cast<unsigned long>(GetCurrentThreadId()),
            ei::crashContext(), dumpWritten ? "written" : "failed", static_cast<unsigned long>(dumpError), registers);

    HANDLE logFile = CreateFileW(L"logs\\workflow.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(logFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        const DWORD safeLength = DWORD(length > 0 ? qMin(length, int(sizeof(record) - 1)) : 0);
        WriteFile(logFile, record, safeLength, &written, nullptr);
        FlushFileBuffers(logFile);
        CloseHandle(logFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
}

namespace {
int runHeadlessCommand(QApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("EI Mapper: Endurance Edition validation tools");
    parser.addHelpOption();
    // QCommandLineParser's built-in version option uses a QMessageBox for a
    // Windows GUI executable. Keep version probes non-interactive so automated
    // builds never leave an editor-name/version window for the user to close.
    QCommandLineOption versionOption(QStringList() << "v" << "version",
                                     "Print the application version without opening a window.");
    parser.addOption(versionOption);
    QCommandLineOption validateOption("validate", "Validate a MOB or MPR without changing it.", "file");
    QCommandLineOption roundtripOption("roundtrip-check", "Verify byte-identical unchanged serialization.", "file");
    QCommandLineOption jsonOption("json", "Emit machine-readable JSON.");
    QCommandLineOption profileLoadOption("profile-load", "Discover the recommended resource profile for a game installation.", "game-root");
    QCommandLineOption mobDiffOption("mob-diff", "Compare a baseline MOB with another MOB.", "baseline");
    QCommandLineOption uiWidgetCheckOption("ui-widget-check", "Run non-interactive asset-picker regression checks.");
    QCommandLineOption resourceNavigationCheckOption("resource-navigation-check",
        "Run the sorted Resource Browser reference-navigation regression check.");
    QCommandLineOption testRunReportOption("test-run-report",
        "Validate the authoritative result of a completed Endurance map-test run.",
        "run-root");
    QCommandLineOption againstOption("against", "Candidate MOB used with --mob-diff.", "candidate");
    QCommandLineOption angleOpenGlOption("angle-opengl", "Use Qt's ANGLE/OpenGL ES implementation (diagnostic GUI mode).");
    QCommandLineOption desktopOpenGlOption("desktop-opengl", "Use the system desktop OpenGL implementation (diagnostic GUI mode).");
    QCommandLineOption renderBenchmarkOption("render-benchmark",
        "Disable VSync and run the viewport continuously for renderer throughput capture.");
    QCommandLineOption workspaceLoadSmokeOption("workspace-load-smoke",
        "Open positional MPR/MOB documents, log foreground load timings, then exit.");
    QCommandLineOption workspaceVisualSmokeOption("workspace-visual-smoke",
        "Open positional documents and exit after all background unit visuals resolve.");
    QCommandLineOption workspaceVisualSequenceOption("workspace-visual-sequence",
        "Open semicolon-separated MPR|MOB stages in one process and exit after each stage resolves.",
        "stages");
    QCommandLineOption terrainStampRegressionOption("terrain-stamp-regression",
        "Run one real 6x6 Modern Lava-on-Water overlay viewport stamp, frame-capture it, validate undo/redo, then exit.");
    QCommandLineOption terrainStampRegressionResultOption(
        "terrain-stamp-regression-result",
        "Write --terrain-stamp-regression JSON evidence to this file.",
        "file");
    parser.addOption(validateOption);
    parser.addOption(roundtripOption);
    parser.addOption(jsonOption);
    parser.addOption(profileLoadOption);
    parser.addOption(mobDiffOption);
    parser.addOption(uiWidgetCheckOption);
    parser.addOption(resourceNavigationCheckOption);
    parser.addOption(testRunReportOption);
    parser.addOption(againstOption);
    parser.addOption(angleOpenGlOption);
    parser.addOption(desktopOpenGlOption);
    parser.addOption(renderBenchmarkOption);
    parser.addOption(workspaceLoadSmokeOption);
    parser.addOption(workspaceVisualSmokeOption);
    parser.addOption(workspaceVisualSequenceOption);
    parser.addOption(terrainStampRegressionOption);
    parser.addOption(terrainStampRegressionResultOption);
    parser.addPositionalArgument("documents", "MPR and MOB documents to open in order.", "[documents...]");
    parser.process(app);

    // This remains a normal GUI initialization/load path rather than a
    // synthetic parser benchmark. The startup callback exits only after every
    // positional document has passed through MainWindow/CView.
    if(parser.isSet(workspaceLoadSmokeOption))
        app.setProperty("workspaceLoadSmoke", true);
    if(parser.isSet(workspaceVisualSmokeOption))
        app.setProperty("workspaceVisualSmoke", true);
    if(parser.isSet(workspaceVisualSequenceOption))
    {
        app.setProperty("workspaceVisualSmoke", true);
        app.setProperty("workspaceVisualSequence", parser.value(workspaceVisualSequenceOption));
    }
    if(parser.isSet(terrainStampRegressionOption))
    {
        app.setProperty("terrainStampRegression", true);
        app.setProperty("terrainStampRegressionResult",
                        parser.value(terrainStampRegressionResultOption));
    }

    if(parser.isSet(versionOption))
    {
        QTextStream output(stdout);
        output << QCoreApplication::applicationName() << " "
               << QCoreApplication::applicationVersion() << "\n";
        output.flush();
        return 0;
    }

    const bool validate = parser.isSet(validateOption);
    const bool roundtrip = parser.isSet(roundtripOption);
    const bool profileLoad = parser.isSet(profileLoadOption);
    const bool mobDiff = parser.isSet(mobDiffOption);
    const bool uiWidgetCheck = parser.isSet(uiWidgetCheckOption);
    const bool resourceNavigationCheck = parser.isSet(resourceNavigationCheckOption);
    const bool testRunReport = parser.isSet(testRunReportOption);
    if(!validate && !roundtrip && !profileLoad && !mobDiff && !uiWidgetCheck
       && !resourceNavigationCheck
       && !testRunReport)
        return -1;
    if((validate ? 1 : 0) + (roundtrip ? 1 : 0) + (profileLoad ? 1 : 0)
            + (mobDiff ? 1 : 0) + (uiWidgetCheck ? 1 : 0)
            + (resourceNavigationCheck ? 1 : 0)
            + (testRunReport ? 1 : 0) > 1)
    {
        QTextStream(stderr) << "Choose one headless command.\n";
        return 2;
    }

    if(resourceNavigationCheck)
    {
        app.setQuitOnLastWindowClosed(false);
        const bool passed=resourceNavigationExactTargetCheck();
        QTextStream(passed?stdout:stderr)
            <<(passed?"PASS":"FAIL")<<": sorted resource reference selected exact target\n";
        return passed?0:1;
    }

    if(testRunReport)
    {
        QJsonObject report;
        QStringList findings;
        const bool passed = CJmvMapTestLauncher::analyzeCompletedRun(
            parser.value(testRunReportOption), &report, &findings);
        QTextStream output(passed ? stdout : stderr);
        if(parser.isSet(jsonOption))
        {
            QJsonObject result = report;
            QJsonArray findingArray;
            for(const QString& finding : findings)
                findingArray.append(finding);
            result.insert("automatedFindings", findingArray);
            result.insert("automatedPassed", passed);
            output << QJsonDocument(result).toJson(QJsonDocument::Indented);
        }
        else
        {
            output << (passed ? "PASS" : "FAIL")
                   << ": completed map-test run "
                   << report.value("runId").toString() << "\n";
            output << "requested=" << report.value("requestedZone").toString()
                   << " entrance=" << report.value("requestedEntrance").toInt()
                   << " actual=" << report.value("actualZone").toString()
                   << " entrance=" << report.value("actualEntrance").toInt()
                   << " generation=" << report.value("actualGeneration").toString()
                   << "\n";
            for(const QString& finding : findings)
                output << "- " << finding << "\n";
        }
        output.flush();
        return passed ? 0 : 1;
    }

    if(uiWidgetCheck)
    {
        app.setQuitOnLastWindowClosed(false);
        CTreeView behaviorFilterTree;
        CObjectBase behaviorFilterNode;
        auto behaviorOwner = new QTreeWidgetItem(&behaviorFilterTree);
        behaviorOwner->setText(0, QStringLiteral("Unit owner"));
        auto behaviorLook = new CTreeObject(behaviorOwner, &behaviorFilterNode);
        behaviorLook->setText(0, QStringLiteral("Look point 1"));
        behaviorLook->setData(0, CTreeView::BehaviorItemRole, true);
        behaviorLook->setData(0, CTreeView::FilterTextRole,
                              QStringLiteral("Look point 1"));
        behaviorFilterTree.setFilterText(QStringLiteral("look"));
        behaviorFilterTree.applyFilter();
        const bool behaviorFilterUsesCachedText = !behaviorLook->isHidden();

        // Reproduce typing immediately before a hierarchy rebuild. The pending
        // debounce must be cancelled before the old rows are destroyed.
        behaviorFilterTree.setFilterText(QStringLiteral("look"));
        behaviorFilterTree.prepareForRebuild();
        behaviorFilterTree.clear();
        auto rebuiltLook = new QTreeWidgetItem(&behaviorFilterTree);
        rebuiltLook->setText(0, QStringLiteral("Look point 2"));
        rebuiltLook->setData(0, CTreeView::BehaviorItemRole, true);
        rebuiltLook->setData(0, CTreeView::FilterTextRole,
                             QStringLiteral("Look point 2"));
        behaviorFilterTree.applyFilter();
        const bool behaviorFilterRebuildSafe = !rebuiltLook->isHidden();

        CUnit* virtualLogicOwner = reinterpret_cast<CUnit*>(quintptr(1));
        auto virtualLogicRow = new QTreeWidgetItem(&behaviorFilterTree);
        virtualLogicRow->setData(0, Qt::UserRole + 10, 4);
        virtualLogicRow->setData(0, Qt::UserRole + 11,
            QVariant::fromValue<qulonglong>(
                reinterpret_cast<qulonglong>(virtualLogicOwner)));
        behaviorFilterTree.focusLogicSlot(virtualLogicOwner, 4);
        const bool virtualLogicFocusWorks = behaviorFilterTree.currentItem()
                == virtualLogicRow && virtualLogicRow->isSelected()
                && behaviorFilterTree.focusedLogicUnit() == virtualLogicOwner
                && behaviorFilterTree.focusedLogicSlot() == 4;
        behaviorFilterTree.clear();
        auto rebuiltLogicRow = new QTreeWidgetItem(&behaviorFilterTree);
        rebuiltLogicRow->setData(0, Qt::UserRole + 10, 4);
        rebuiltLogicRow->setData(0, Qt::UserRole + 11,
            QVariant::fromValue<qulonglong>(
                reinterpret_cast<qulonglong>(virtualLogicOwner)));
        behaviorFilterTree.focusLogicSlot(
            behaviorFilterTree.focusedLogicUnit(),
            behaviorFilterTree.focusedLogicSlot());
        const bool virtualLogicFocusSurvivesRebuild =
                behaviorFilterTree.currentItem() == rebuiltLogicRow
                && rebuiltLogicRow->isSelected();

        QList<QString>& figures = CObjectList::getInstance()->figureList();
        figures = {"alpha", "hammer", "Harpy", "echo", "house"};
        QSharedPointer<IPropertyBase> value(new propStr(eObjParam_TEMPLATE, "original_model"));
        CComboDynItem combo(value);
        int appliedChanges = 0;
        QObject::connect(&combo, &CComboDynItem::onParamChange,
                         [&appliedChanges](const QSharedPointer<IPropertyBase>&) { ++appliedChanges; });
        combo.setEditText("h");
        combo._onFilterTextChanged("h");

        QStringList completions;
        QAbstractItemModel* completionModel = combo.completer()->completionModel();
        for(int row = 0; completionModel && row < completionModel->rowCount(); ++row)
            completions.append(completionModel->index(row, 0).data().toString());

        bool prefixesOnly = !completions.isEmpty();
        for(const QString& completion : completions)
            prefixesOnly = prefixesOnly && completion.startsWith("h", Qt::CaseInsensitive);
        const bool valueStable = combo.currentText() == "h"
                && combo.value()->toString() == "original_model" && appliedChanges == 0;
        QFocusEvent focusOut(QEvent::FocusOut, Qt::OtherFocusReason);
        QApplication::sendEvent(combo.lineEdit(), &focusOut);
        const bool focusCancel = combo.currentText() == "original_model"
                && combo.value()->toString() == "original_model" && appliedChanges == 0;
        combo.setEditText("house");
        combo._onFilterTextChanged("house");
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(combo.lineEdit(), &escape);
        const bool escapeCancel = combo.currentText() == "original_model"
                && combo.value()->toString() == "original_model" && appliedChanges == 0;
        combo.setEditText("custom_model");
        combo._onEditingFinished();
        const bool enterCommit = combo.value()->toString() == "custom_model" && appliedChanges == 1;
        combo._onChange("house");
        const bool clickCommit = combo.value()->toString() == "house" && appliedChanges == 2;

        QSharedPointer<IPropertyBase> prototypeValue(
            new propStr(eObjParam_UNIT_PROTOTYPE, "Human Test"));
        QTableWidget prototypeTable;
        CTableManager prototypeManager(&prototypeTable);
        QString routedPrototype;
        QObject::connect(&prototypeManager,
                         &CTableManager::openDatabasePrototypeRequested,
                         [&routedPrototype](const QString& value) {
            routedPrototype = value;
        });
        prototypeManager.setNewData(
            QList<QSharedPointer<IPropertyBase>>() << prototypeValue);
        CComboDynItem* routedCombo = qobject_cast<CComboDynItem*>(
            prototypeTable.cellWidget(0, 1));
        QAction* prototypeOpenAction = nullptr;
        bool prototypeMenuAccepted = false;
        if(routedCombo)
        {
            QTimer::singleShot(0, [&prototypeOpenAction]()
            {
                for(QWidget* widget : QApplication::topLevelWidgets())
                    if(QMenu* menu = qobject_cast<QMenu*>(widget))
                        if(QAction* action = menu->findChild<QAction*>(
                               QStringLiteral("openDatabasePrototypeAction")))
                        {
                            prototypeOpenAction = action;
                            action->trigger();
                            menu->close();
                            return;
                        }
            });
            QContextMenuEvent prototypeMenuEvent(QContextMenuEvent::Mouse,
                QPoint(2, 2), routedCombo->lineEdit()->mapToGlobal(QPoint(2, 2)));
            QApplication::sendEvent(routedCombo->lineEdit(), &prototypeMenuEvent);
            prototypeMenuAccepted = prototypeMenuEvent.isAccepted();
        }
        QApplication::processEvents();
        const bool databasePrototypeMenuEmissionWorks =
            prototypeMenuAccepted && prototypeOpenAction && routedCombo
            && routedPrototype == QStringLiteral("Human Test");

        QSharedPointer<IPropertyBase> textureValue(new propStr(eObjParam_PRIM_TXTR, "original_texture"));
        CComboDynItem textureCombo(textureValue);
        QMimeData* incompatibleMime = new QMimeData;
        incompatibleMime->setText("model_value");
        incompatibleMime->setData("application/x-ei-mapper-property-type",
                                  QByteArray::number(int(eObjParam_TEMPLATE)));
        QGuiApplication::clipboard()->setMimeData(incompatibleMime);
        QKeyEvent pasteIncompatible(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(textureCombo.lineEdit(), &pasteIncompatible);
        const bool incompatiblePasteBlocked = textureCombo.currentText() == "original_texture"
                && textureCombo.value()->toString() == "original_texture";

        QGuiApplication::clipboard()->setText("custom_texture");
        textureCombo.lineEdit()->selectAll();
        QKeyEvent pastePlainText(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(textureCombo.lineEdit(), &pastePlainText);
        const bool compatiblePastePreview = textureCombo.currentText() == "custom_texture"
                && textureCombo.value()->toString() == "original_texture";
        textureCombo._onEditingFinished();
        const bool compatiblePasteCommit = textureCombo.value()->toString() == "custom_texture";

        const auto rowForType = [](QTableWidget& table, EObjParam type)
        {
            for(int row = 0; row < table.rowCount(); ++row)
            {
                QTableWidgetItem* label = table.item(row, 0);
                if(label && label->data(Qt::UserRole).toInt() == int(type))
                    return row;
            }
            return -1;
        };
        ei::STrapTargetSummary targetSummary;
        targetSummary.available = true;
        targetSummary.playerGroup = 3;
        targetSummary.groups = {4, 12};
        targetSummary.labels = QStringList()
            << QStringLiteral("4 (Guards)")
            << QStringLiteral("12 (Orcs)");
        targetSummary.display = QStringLiteral(
            "Player group 3 → 4 (Guards), 12 (Orcs)");
        QList<QSharedPointer<IPropertyBase>> trapTargetProperties;
        trapTargetProperties << QSharedPointer<IPropertyBase>(new propTrapTargets(
            eObjParam_TRAP_EFFECTIVE_TARGETS, targetSummary));
        QTableWidget trapTargetPropertyTable;
        CTableManager trapTargetManager(&trapTargetPropertyTable);
        trapTargetManager.setNewData(trapTargetProperties, true);
        const int trapTargetRow = rowForType(
            trapTargetPropertyTable, eObjParam_TRAP_EFFECTIVE_TARGETS);
        CTrapTargetItem* trapTargetButton = trapTargetRow >= 0
            ? qobject_cast<CTrapTargetItem*>(
                trapTargetPropertyTable.cellWidget(trapTargetRow, 1))
            : nullptr;
        if(trapTargetButton) trapTargetButton->click();
        QApplication::processEvents();
        QDialog* trapTargetDialog = nullptr;
        for(QWidget* widget : QApplication::topLevelWidgets())
            if(widget->objectName() == QStringLiteral("trapTargetDialog"))
                trapTargetDialog = qobject_cast<QDialog*>(widget);
        QTableWidget* trapTargetTable = trapTargetDialog
            ? trapTargetDialog->findChild<QTableWidget*>(
                QStringLiteral("trapTargetTable")) : nullptr;
        const bool trapTargetsUseReadOnlyTable = trapTargetRow >= 0
            && trapTargetPropertyTable.item(trapTargetRow, 0)->text()
                == QStringLiteral("Will attack")
            && trapTargetButton
            && !trapTargetPropertyTable.cellWidget(trapTargetRow, 1)
                ->findChild<QLineEdit*>()
            && trapTargetTable && trapTargetTable->rowCount() == 1
            && trapTargetTable->columnCount() == 2
            && trapTargetTable->horizontalHeaderItem(0)->text()
                == QStringLiteral("4 (Guards)")
            && trapTargetTable->horizontalHeaderItem(1)->text()
                == QStringLiteral("12 (Orcs)")
            && trapTargetTable->verticalHeaderItem(0)->text()
                == QStringLiteral("Will attack")
            && trapTargetTable->editTriggers() == QAbstractItemView::NoEditTriggers;
        if(trapTargetDialog) trapTargetDialog->close();

        CMob diplomacyMob;
        diplomacyMob.generateDiplomacyTable();
        diplomacyMob.setRanges(true, QVector<SRange>() << SRange(0, 100));
        diplomacyMob.diplomacyField()[0][1] = 0;
        diplomacyMob.diplomacyField()[0][2] = 1;
        diplomacyMob.diplomacyField()[0][3] = 2;
        CMobParameters diplomacyParameters(nullptr, &diplomacyMob, nullptr);
        QTableWidget* diplomacyTable = nullptr;
        for(QTableWidget* candidate : diplomacyParameters.findChildren<QTableWidget*>())
            if(candidate->rowCount() == 32 && candidate->columnCount() == 32)
                diplomacyTable = candidate;
        const bool diplomacyValuesExplainMeaning = diplomacyTable
            && diplomacyTable->item(0, 1)->text() == QStringLiteral("0")
            && diplomacyTable->item(0, 2)->text() == QStringLiteral("1")
            && diplomacyTable->item(0, 3)->text() == QStringLiteral("2")
            && diplomacyTable->item(0, 1)->toolTip().contains(QStringLiteral("Ally"))
            && diplomacyTable->item(0, 2)->toolTip().contains(QStringLiteral("Neutral"))
            && diplomacyTable->item(0, 3)->toolTip().contains(QStringLiteral("hostile"));
        const bool diplomacyDiagonalUsesRelationColor = diplomacyTable
            && diplomacyTable->item(0, 0)->background().color() == QColor(Qt::darkGreen);
        if(diplomacyTable)
            diplomacyTable->setCurrentCell(7, 8);
        QApplication::processEvents();
        bool diplomacySelectionCrossHighlights = diplomacyTable
            && diplomacyTable->selectedItems().size() == 63;
        for(int index = 0; diplomacySelectionCrossHighlights && index < 32; ++index)
            diplomacySelectionCrossHighlights = diplomacyTable->item(7, index)->isSelected()
                && diplomacyTable->item(index, 8)->isSelected();
        bool diplomacyHeadersBoldSelection = false;
        QVector<QVector<uint>>& commandDiplomacy = diplomacyMob.diplomacyField();
        commandDiplomacy[4][5] = 2;
        commandDiplomacy[5][4] = 1;
        CChangeDiplomacyTableItem oneWayCommand(&diplomacyMob, 4, 5, false);
        oneWayCommand.redo();
        const bool oneWayRedoPreservesReverse = commandDiplomacy[4][5] == 0
            && commandDiplomacy[5][4] == 1;
        oneWayCommand.undo();
        const bool oneWayUndoPreservesReverse = commandDiplomacy[4][5] == 2
            && commandDiplomacy[5][4] == 1;
        commandDiplomacy[6][6] = 0;
        CChangeDiplomacyTableItem selfCommand(&diplomacyMob, 6, 6, true);
        selfCommand.redo();
        const bool selfRelationEditable = commandDiplomacy[6][6] == 1;
        selfCommand.undo();
        const bool selfRelationUndoable = commandDiplomacy[6][6] == 0;

        QSettings diplomacySettings;
        const QString diplomacySizeKey = QStringLiteral("ui/diplomacyWindowSizeV4");
        const bool hadDiplomacySize = diplomacySettings.contains(diplomacySizeKey);
        const QVariant previousDiplomacySize = diplomacySettings.value(diplomacySizeKey);
        diplomacySettings.remove(diplomacySizeKey);
        QMetaObject::invokeMethod(&diplomacyParameters,
                                  "on_diplomacyButton_clicked");
        QApplication::processEvents();
        if(diplomacyTable)
        {
            diplomacyTable->setCurrentCell(7, 9);
            diplomacyTable->setCurrentCell(7, 8);
        }
        QApplication::processEvents();
        diplomacyHeadersBoldSelection = diplomacyTable
            && diplomacyTable->horizontalHeaderItem(8)
            && diplomacyTable->verticalHeaderItem(7)
            && diplomacyTable->horizontalHeaderItem(8)->font().bold()
            && diplomacyTable->verticalHeaderItem(7)->font().bold()
            && !diplomacyTable->horizontalHeaderItem(7)->font().bold()
            && !diplomacyTable->verticalHeaderItem(8)->font().bold();
        QWidget* diplomacyWindow = diplomacyParameters.findChild<QWidget*>(
            QStringLiteral("diplomacyWindow"));
        QCheckBox* symmetricDiplomacy = diplomacyWindow
            ? diplomacyWindow->findChild<QCheckBox*>() : nullptr;
        const bool diplomacyWindowAutoFitsAndResizable = diplomacyWindow
            && diplomacyWindow->isVisible()
            && diplomacyWindow->minimumSize() != diplomacyWindow->maximumSize()
            && diplomacyWindow->size().width() > 400
            && diplomacyWindow->size().height() > 300
            && diplomacyTable->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded
            && diplomacyTable->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded;
        QScreen* diplomacyScreen = diplomacyWindow
            ? QGuiApplication::screenAt(diplomacyWindow->frameGeometry().center()) : nullptr;
        if(!diplomacyScreen)
            diplomacyScreen = QGuiApplication::primaryScreen();
        const QSize diplomacyAvailable = diplomacyScreen
            ? diplomacyScreen->availableGeometry().size() : QSize(1280, 800);
        const int diplomacyTableContentWidth = diplomacyTable
            ? diplomacyTable->verticalHeader()->width()
              + diplomacyTable->horizontalHeader()->length()
              + diplomacyTable->frameWidth() * 2 + 4 : 0;
        const int diplomacyTableContentHeight = diplomacyTable
            ? diplomacyTable->horizontalHeader()->height()
              + diplomacyTable->verticalHeader()->length()
              + diplomacyTable->frameWidth() * 2 + 4 : 0;
        const bool diplomacyFirstOpenFitsContent = diplomacyWindow
            && diplomacyWindow->width() >= qMin(
                diplomacyTableContentWidth, qRound(diplomacyAvailable.width() * 0.95))
            && diplomacyWindow->height() >= qMin(
                diplomacyTableContentHeight, qRound(diplomacyAvailable.height() * 0.95));
        const bool diplomacyResizeUsesFixedSections = diplomacyTable
            && diplomacyTable->horizontalHeader()->sectionResizeMode(0)
                == QHeaderView::Fixed
            && diplomacyTable->verticalHeader()->sectionResizeMode(0)
                == QHeaderView::Fixed;
        const bool diplomacyDirectionControlVisible = symmetricDiplomacy
            && symmetricDiplomacy->text()
                == QStringLiteral("Apply changes in both directions");
        const QSize testDiplomacySize(733, 511);
        if(diplomacyWindow)
        {
            diplomacyWindow->resize(testDiplomacySize);
            diplomacyWindow->close();
        }
        QMetaObject::invokeMethod(&diplomacyParameters,
                                  "on_diplomacyButton_clicked");
        QApplication::processEvents();
        const bool diplomacyWindowSizePersists = diplomacyWindow
            && diplomacyWindow->size() == testDiplomacySize;
        bool diplomacyResizeRepaintCoalesced = false;
        bool diplomacyResizeRepaintResumes = false;
        if(diplomacyWindow)
        {
            diplomacyWindow->setProperty("skipDiplomacySizePersistence", true);
            diplomacyWindow->resize(testDiplomacySize + QSize(17, 11));
            diplomacyResizeRepaintCoalesced = !diplomacyTable->updatesEnabled();
            QEventLoop resizeWait;
            QTimer::singleShot(100, &resizeWait, &QEventLoop::quit);
            resizeWait.exec();
            diplomacyResizeRepaintResumes = diplomacyTable->updatesEnabled();
            diplomacyWindow->close();
        }
        if(hadDiplomacySize)
            diplomacySettings.setValue(diplomacySizeKey, previousDiplomacySize);
        else
            diplomacySettings.remove(diplomacySizeKey);
        QList<QSharedPointer<IPropertyBase>> staticChoiceProperties;
        staticChoiceProperties
            << QSharedPointer<IPropertyBase>(new propUint(eObjParam_TYPE, 50))
            << QSharedPointer<IPropertyBase>(new propChar(eObjParam_AGRESSION_MODE, 0))
            << QSharedPointer<IPropertyBase>(new propChar(eObjParam_LOGIC_ALARM_CONDITION, 0));
        QTableWidget staticChoiceTable;
        CTableManager staticChoiceManager(&staticChoiceTable);
        staticChoiceManager.setNewData(staticChoiceProperties);
        const int unitCategoryRow = rowForType(staticChoiceTable, eObjParam_TYPE);
        const int aggressionRow = rowForType(staticChoiceTable, eObjParam_AGRESSION_MODE);
        const int alarmConditionRow = rowForType(
            staticChoiceTable, eObjParam_LOGIC_ALARM_CONDITION);
        CComboStItem* unitCategoryCombo = qobject_cast<CComboStItem*>(
            staticChoiceTable.cellWidget(unitCategoryRow, 1));
        CComboStItem* aggressionCombo = qobject_cast<CComboStItem*>(
            staticChoiceTable.cellWidget(aggressionRow, 1));
        CComboStItem* alarmConditionCombo = qobject_cast<CComboStItem*>(
            staticChoiceTable.cellWidget(alarmConditionRow, 1));
        const int attackIndex = aggressionCombo
            ? aggressionCombo->findText(QStringLiteral("Attack")) : -1;
        const bool staticChoiceItemDataStored = aggressionCombo
            && attackIndex >= 0
            && aggressionCombo->itemData(attackIndex, Qt::ToolTipRole).toString()
                == QStringLiteral("Attacks enemies on sight.");
        const QStringList expectedAlarmTooltips = {
            QStringLiteral("Raises the configured alarm when the unit sees an enemy."),
            QStringLiteral("Raises the configured alarm when the unit is attacked."),
            QStringLiteral("Raises the configured alarm when the unit becomes suspicious.")};
        bool alarmConditionItemTooltipsWork = alarmConditionCombo
            && alarmConditionCombo->count() == expectedAlarmTooltips.size();
        for(int index = 0; alarmConditionItemTooltipsWork
                && index < expectedAlarmTooltips.size(); ++index)
            alarmConditionItemTooltipsWork =
                alarmConditionCombo->itemData(index, Qt::ToolTipRole).toString()
                    == expectedAlarmTooltips.at(index);
        const bool staticChoicePopupTooltipClear = aggressionCombo
            && aggressionCombo->view()->toolTip().isEmpty()
            && aggressionCombo->view()->viewport()->toolTip().isEmpty();
        const bool staticChoiceItemTooltipsWork = staticChoiceItemDataStored
            && staticChoicePopupTooltipClear;
        const bool unitCategoryIdsVisible = unitCategoryCombo
            && unitCategoryCombo->findText(QStringLiteral("Human (50)")) >= 0
            && unitCategoryCombo->findText(QStringLiteral("Animal (51)")) >= 0
            && unitCategoryCombo->findText(QStringLiteral("Monster (52)")) >= 0;
        const bool helperFigureCacheIdentityDistinct =
            ei::editorHelperFigureIdentity(QString(),
                reinterpret_cast<const ei::CFigure*>(quintptr(1)))
            != ei::editorHelperFigureIdentity(QString(),
                reinterpret_cast<const ei::CFigure*>(quintptr(2)));
        QList<QSharedPointer<IPropertyBase>> sourceProperties;
        sourceProperties << QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE, "copied_model"))
                         << QSharedPointer<IPropertyBase>(new propStr(eObjParam_PRIM_TXTR, "copied_texture"));
        QTableWidget sourcePropertyTable;
        CTableManager sourcePropertyManager(&sourcePropertyTable);
        sourcePropertyManager.setNewData(sourceProperties);
        const int sourceModelRow = rowForType(sourcePropertyTable, eObjParam_TEMPLATE);
        const int sourceTextureRow = rowForType(sourcePropertyTable, eObjParam_PRIM_TXTR);
        sourcePropertyTable.item(sourceModelRow, 0)->setSelected(true);
        sourcePropertyTable.item(sourceTextureRow, 0)->setSelected(true);
        QKeyEvent copySelectedProperties(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(&sourcePropertyTable, &copySelectedProperties);

        QList<QSharedPointer<IPropertyBase>> destinationProperties;
        destinationProperties << QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE, "old_model"))
                              << QSharedPointer<IPropertyBase>(new propStr(eObjParam_PRIM_TXTR, "old_texture"));
        QTableWidget destinationPropertyTable;
        CTableManager destinationPropertyManager(&destinationPropertyTable);
        destinationPropertyManager.setNewData(destinationProperties);
        const int destinationModelRow = rowForType(destinationPropertyTable, eObjParam_TEMPLATE);
        const int destinationTextureRow = rowForType(destinationPropertyTable, eObjParam_PRIM_TXTR);
        destinationPropertyTable.item(destinationModelRow, 0)->setSelected(true);
        destinationPropertyTable.item(destinationTextureRow, 0)->setSelected(true);
        QList<QSharedPointer<IPropertyBase>> pastedPropertyBundle;
        QObject::connect(&destinationPropertyManager, &CTableManager::onUpdateProperties,
                         [&pastedPropertyBundle](const QList<QSharedPointer<IPropertyBase>>& values)
        {
            pastedPropertyBundle = values;
        });
        QKeyEvent pasteSelectedProperties(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&destinationPropertyTable, &pasteSelectedProperties);
        const bool multiPropertyPaste = pastedPropertyBundle.size() == 2
                && pastedPropertyBundle.at(0)->type() == eObjParam_TEMPLATE
                && pastedPropertyBundle.at(0)->toString() == "copied_model"
                && pastedPropertyBundle.at(1)->type() == eObjParam_PRIM_TXTR
                && pastedPropertyBundle.at(1)->toString() == "copied_texture";

        destinationPropertyTable.clearSelection();
        destinationPropertyTable.item(destinationTextureRow, 0)->setSelected(true);
        pastedPropertyBundle.clear();
        QKeyEvent rejectMismatchedBundle(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&destinationPropertyTable, &rejectMismatchedBundle);
        const bool mismatchedPropertyPasteBlocked = pastedPropertyBundle.isEmpty();

        sourcePropertyTable.clearSelection();
        sourcePropertyTable.item(sourceModelRow, 0)->setSelected(true);
        QKeyEvent copyModelProperty(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(&sourcePropertyTable, &copyModelProperty);
        QSharedPointer<IPropertyBase> immediateModelPaste;
        QObject::connect(&destinationPropertyManager, &CTableManager::onUpdateProperty,
                         [&immediateModelPaste](const QSharedPointer<IPropertyBase>& value)
        {
            immediateModelPaste.reset(value->clone());
        });
        CComboDynItem* destinationModel = qobject_cast<CComboDynItem*>(
                    destinationPropertyTable.cellWidget(destinationModelRow, 1));
        destinationModel->lineEdit()->selectAll();
        QKeyEvent pasteModelField(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(destinationModel->lineEdit(), &pasteModelField);
        const bool fieldPastePreview = immediateModelPaste.isNull()
                && destinationModel->currentText() == "copied_model";
        destinationModel->_onEditingFinished();
        const bool fieldPasteCommit = !immediateModelPaste.isNull()
                && immediateModelPaste->type() == eObjParam_TEMPLATE
                && immediateModelPaste->toString() == "copied_model";

        QKeyEvent copyModelField(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(sourcePropertyTable.cellWidget(sourceModelRow, 1), &copyModelField);
        const bool fieldCopyAvailable = QGuiApplication::clipboard()->text() == "copied_model";

        QList<QSharedPointer<IPropertyBase>> colorSource;
        colorSource << QSharedPointer<IPropertyBase>(new prop3D(eObjParam_LIGHT_COLOR, 0.1f, 0.2f, 0.3f));
        QTableWidget colorSourceTable;
        CTableManager colorSourceManager(&colorSourceTable);
        colorSourceManager.setNewData(colorSource);
        colorSourceTable.item(0, 0)->setSelected(true);
        QKeyEvent copyColor(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(&colorSourceTable, &copyColor);
        QList<QSharedPointer<IPropertyBase>> colorDestination;
        colorDestination << QSharedPointer<IPropertyBase>(new prop3D(eObjParam_LIGHT_COLOR, 0.8f, 0.8f, 0.8f));
        QTableWidget colorDestinationTable;
        CTableManager colorDestinationManager(&colorDestinationTable);
        colorDestinationManager.setNewData(colorDestination);
        colorDestinationTable.item(0, 0)->setSelected(true);
        QSharedPointer<IPropertyBase> pastedColor;
        QObject::connect(&colorDestinationManager, &CTableManager::onUpdateProperty,
                         [&pastedColor](const QSharedPointer<IPropertyBase>& value)
        {
            pastedColor.reset(value->clone());
        });
        QKeyEvent pasteColor(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&colorDestinationTable, &pasteColor);
        const prop3D* colorValue = dynamic_cast<prop3D*>(pastedColor.get());
        const bool nonTextPropertyPaste = colorValue
                && qFuzzyCompare(colorValue->value().x(), 0.1f)
                && qFuzzyCompare(colorValue->value().y(), 0.2f)
                && qFuzzyCompare(colorValue->value().z(), 0.3f);

        // A single component of a compound vector is ordinary numeric data:
        // it can be copied into another numeric field, with range validation
        // and nearest-integer rounding when the destination is integral.
        QList<QSharedPointer<IPropertyBase>> vectorComponents;
        vectorComponents << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_X, 1.25f))
                         << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_Y, 2.5f))
                         << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_Z, 3.6f));
        QTableWidget vectorComponentTable;
        CTableManager vectorComponentManager(&vectorComponentTable);
        vectorComponentManager.setNewData(vectorComponents);
        QToolButton* xComponentPlate = nullptr;
        QToolButton* yComponentPlate = nullptr;
        QToolButton* zComponentPlate = nullptr;
        const QList<QToolButton*> componentPlates = vectorComponentTable.findChildren<QToolButton*>();
        for(QToolButton* component : componentPlates)
        {
            if(component->text() == "X:") xComponentPlate = component;
            if(component->text() == "Y:") yComponentPlate = component;
            if(component->text() == "Z:") zComponentPlate = component;
        }
        vectorComponentTable.item(0, 0)->setSelected(true);
        if(xComponentPlate)
            xComponentPlate->click();
        const bool individualComponentSelection = xComponentPlate && yComponentPlate
                && zComponentPlate && xComponentPlate->isChecked()
                && !yComponentPlate->isChecked() && !zComponentPlate->isChecked()
                && vectorComponentTable.selectedItems().isEmpty();
        if(zComponentPlate)
        {
            zComponentPlate->click();
            QKeyEvent copyZComponent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
            QApplication::sendEvent(zComponentPlate, &copyZComponent);
        }
        const bool selectedComponentCopy = zComponentPlate && zComponentPlate->isChecked()
                && QGuiApplication::clipboard()->text() == "3.6";
        QList<QSharedPointer<IPropertyBase>> integerDestination;
        integerDestination << QSharedPointer<IPropertyBase>(new propUint(eObjParam_RANGE, 0));
        QTableWidget integerDestinationTable;
        CTableManager integerDestinationManager(&integerDestinationTable);
        integerDestinationManager.setNewData(integerDestination);
        QSharedPointer<IPropertyBase> roundedComponentPaste;
        QObject::connect(&integerDestinationManager, &CTableManager::onUpdateProperty,
                         [&roundedComponentPaste](const QSharedPointer<IPropertyBase>& value)
        {
            roundedComponentPaste.reset(value->clone());
        });
        integerDestinationTable.item(0, 0)->setSelected(true);
        QKeyEvent pasteZComponent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&integerDestinationTable, &pasteZComponent);
        const propUint* roundedInteger = dynamic_cast<propUint*>(roundedComponentPaste.get());
        const bool numericComponentPaste = zComponentPlate && roundedInteger
                && roundedInteger->value() == 4u;
        roundedComponentPaste.clear();
        integerDestinationManager.setReadOnly(true);
        QKeyEvent pasteLockedComponent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&integerDestinationTable, &pasteLockedComponent);
        const bool lockedNumericPasteBlocked = roundedComponentPaste.isNull();

        QSharedPointer<IPropertyBase> componentPlateChange;
        QObject::connect(&vectorComponentManager, &CTableManager::onUpdateProperty,
                         [&componentPlateChange](const QSharedPointer<IPropertyBase>& value)
        {
            componentPlateChange = QSharedPointer<IPropertyBase>(value->clone());
        });
        QMimeData* componentText = new QMimeData;
        componentText->setText("8.75");
        QGuiApplication::clipboard()->setMimeData(componentText);
        if(xComponentPlate)
        {
            xComponentPlate->click();
            QKeyEvent pasteXComponent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
            QApplication::sendEvent(xComponentPlate, &pasteXComponent);
        }
        const propFloat* changedX = dynamic_cast<propFloat*>(componentPlateChange.get());
        const bool componentPlatePaste = changedX
                && changedX->type() == eObjParam_POSITION_X
                && qAbs(changedX->value() - 8.75f) < 0.0001f;

        componentPlateChange.clear();
        componentText = new QMimeData;
        componentText->setText("6.25");
        QGuiApplication::clipboard()->setMimeData(componentText);
        bool componentContextShowsOnlyComponent = false;
        bool componentContextGestureKeepsTableClear = false;
        if(yComponentPlate)
        {
            QTimer::singleShot(0, [&componentContextShowsOnlyComponent,
                                   xComponentPlate, yComponentPlate,
                                   &vectorComponentTable]()
            {
                QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if(!menu)
                    return;
                componentContextShowsOnlyComponent = yComponentPlate->isChecked()
                        && !xComponentPlate->isChecked()
                        && vectorComponentTable.selectedItems().isEmpty()
                        && !vectorComponentTable.currentIndex().isValid();
                for(QAction* action : menu->actions())
                    if(action->text().contains("Paste", Qt::CaseInsensitive))
                    {
                        action->trigger();
                        menu->close();
                        return;
                    }
                menu->close();
            });
            QMouseEvent contextPress(QEvent::MouseButtonPress, QPointF(2, 2),
                                     Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(yComponentPlate, &contextPress);
            QMouseEvent contextRelease(QEvent::MouseButtonRelease, QPointF(2, 2),
                                       Qt::RightButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(yComponentPlate, &contextRelease);
            componentContextGestureKeepsTableClear = contextPress.isAccepted()
                    && contextRelease.isAccepted()
                    && vectorComponentTable.selectedItems().isEmpty()
                    && !vectorComponentTable.currentIndex().isValid();
        }
        const propFloat* changedY = dynamic_cast<propFloat*>(componentPlateChange.get());
        const bool componentContextPaste = changedY
                && changedY->type() == eObjParam_POSITION_Y
                && qAbs(changedY->value() - 6.25f) < 0.0001f;
        const bool componentContextPreservesSelection = xComponentPlate
                && xComponentPlate->isChecked()
                && yComponentPlate && !yComponentPlate->isChecked()
                && vectorComponentTable.selectedItems().isEmpty()
                && !vectorComponentTable.currentIndex().isValid();
        const bool componentContextUsesManualDispatch = yComponentPlate
                && yComponentPlate->contextMenuPolicy() == Qt::PreventContextMenu;
        const bool componentContextFocusIsChildOwned = yComponentPlate
                && yComponentPlate->focusPolicy() == Qt::StrongFocus
                && yComponentPlate->parentWidget()
                && yComponentPlate->parentWidget()->focusPolicy() == Qt::NoFocus;

        QList<QSharedPointer<IPropertyBase>> exclusiveComponents;
        exclusiveComponents
                << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_X, 1.0f))
                << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_Y, 2.0f))
                << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_Z, 3.0f))
                << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_ROTATION_X, 4.0f))
                << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_ROTATION_Y, 5.0f))
                << QSharedPointer<IPropertyBase>(new propFloat(eObjParam_ROTATION_Z, 6.0f))
                << QSharedPointer<IPropertyBase>(new propStr(eObjParam_COMMENTS, "normal row"));
        QTableWidget exclusiveComponentTable;
        CTableManager exclusiveComponentManager(&exclusiveComponentTable);
        exclusiveComponentManager.setNewData(exclusiveComponents);
        QToolButton* positionXPlate = nullptr;
        QToolButton* rotationYPlate = nullptr;
        bool componentHitTargetsExpanded = true;
        bool componentLayoutHasNoDeadSpace = true;
        const QList<QToolButton*> exclusivePlates =
                exclusiveComponentTable.findChildren<QToolButton*>();
        for(QToolButton* plate : exclusivePlates)
        {
            const int componentType = plate->property("enduranceComponentType").toInt();
            if(componentType == int(eObjParam_POSITION_X)) positionXPlate = plate;
            if(componentType == int(eObjParam_ROTATION_Y)) rotationYPlate = plate;
            componentHitTargetsExpanded = componentHitTargetsExpanded
                    && plate->minimumWidth() >= 32 && plate->minimumHeight() >= 22;
            QLayout* componentLayout = plate->parentWidget()->layout();
            const QMargins margins = componentLayout
                    ? componentLayout->contentsMargins() : QMargins();
            componentLayoutHasNoDeadSpace = componentLayoutHasNoDeadSpace
                    && componentLayout && componentLayout->spacing() == 0
                    && margins.left() == 0 && margins.top() == 0
                    && margins.right() == 0 && margins.bottom() == 0;
        }
        if(positionXPlate) positionXPlate->click();
        if(rotationYPlate) rotationYPlate->click();
        const bool componentSelectionTableExclusive = positionXPlate && rotationYPlate
                && !positionXPlate->isChecked() && rotationYPlate->isChecked();
        if(exclusiveComponentTable.rowCount() > 0)
            exclusiveComponentTable.item(exclusiveComponentTable.rowCount() - 1, 0)->setSelected(true);
        bool componentSelectionClearsForProperty = true;
        for(QToolButton* plate : exclusivePlates)
            componentSelectionClearsForProperty = componentSelectionClearsForProperty
                    && !plate->isChecked();

        QSharedPointer<IPropertyBase> partialValue(new propStr(eObjParam_COMMENTS, "abcdef"));
        CValueItem partialEditor(partialValue);
        partialEditor.setSelection(2, 2);
        QKeyEvent copyPartialText(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(&partialEditor, &copyPartialText);
        const bool partialCopyExact = QGuiApplication::clipboard()->text() == "cd"
                && !QGuiApplication::clipboard()->mimeData()->hasFormat(
                    "application/x-ei-mapper-property-type");
        QSharedPointer<IPropertyBase> partialDestinationValue(
                    new propStr(eObjParam_COMMENTS, "123456"));
        CValueItem partialDestination(partialDestinationValue);
        int partialChanges = 0;
        QObject::connect(&partialDestination, &CValueItem::onParamChange,
                         [&partialChanges](const QSharedPointer<IPropertyBase>&) { ++partialChanges; });
        partialDestination.setSelection(2, 2);
        QKeyEvent pastePartialText(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&partialDestination, &pastePartialText);
        const bool partialPasteExact = partialDestination.text() == "12cd56"
                && partialChanges == 0;
        partialDestination.onTextChangeEnd();
        const bool partialPasteCommit = partialDestination.value()->toString() == "12cd56"
                && partialChanges == 1;

        QList<QSharedPointer<IPropertyBase>> scrollingProperties;
        for(int index = 0; index < 40; ++index)
            scrollingProperties << QSharedPointer<IPropertyBase>(
                new propStr(eObjParam_COMMENTS, QString("comment-%1").arg(index)));
        QTableWidget scrollingTable;
        scrollingTable.resize(320, 120);
        scrollingTable.setAttribute(Qt::WA_DontShowOnScreen);
        scrollingTable.show();
        CTableManager scrollingManager(&scrollingTable);
        scrollingManager.setNewData(scrollingProperties);
        QApplication::processEvents();
        const int wantedScroll = qMax(1, scrollingTable.verticalScrollBar()->maximum() / 2);
        scrollingTable.verticalScrollBar()->setValue(wantedScroll);
        scrollingManager.setNewData(scrollingProperties);
        QApplication::processEvents();
        const bool propertyScrollPreserved = scrollingTable.verticalScrollBar()->value()
                == qMin(wantedScroll, scrollingTable.verticalScrollBar()->maximum());

        QMap<QString, QSharedPointer<propBool>> mixedParts;
        mixedParts.insert("head", QSharedPointer<propBool>(new propBool(eObjParam_BODYPARTS)));
        mixedParts.insert("body", QSharedPointer<propBool>(new propBool(eObjParam_BODYPARTS, true)));
        QList<QSharedPointer<IPropertyBase>> mixedModelProperties;
        mixedModelProperties << QSharedPointer<IPropertyBase>(new propStr(eObjParam_TEMPLATE))
                             << QSharedPointer<IPropertyBase>(new propBodyPart(eObjParam_BODYPARTS,
                                                                              mixedParts));
        QTableWidget mixedModelTable;
        CTableManager mixedModelManager(&mixedModelTable);
        mixedModelManager.setNewData(mixedModelProperties);
        const bool mixedModelPartsVisible = rowForType(mixedModelTable,
                                                       eObjParam_BODYPARTS) >= 0;

        QMap<QString, QSharedPointer<propBool>> serializedPartMap;
        serializedPartMap.insert("bd", QSharedPointer<propBool>(
            new propBool(eObjParam_BODYPARTS, true)));
        serializedPartMap.insert("hr", QSharedPointer<propBool>(
            new propBool(eObjParam_BODYPARTS, true)));
        serializedPartMap.insert("future_part", QSharedPointer<propBool>(
            new propBool(eObjParam_BODYPARTS, true)));
        serializedPartMap.insert("optional_part", QSharedPointer<propBool>(
            new propBool(eObjParam_BODYPARTS, false)));
        const QStringList exactSerializedParts = {
            "bd", "hr", "future_part", "future_part"
        };
        const QSet<QString> recognizedPartOptions = {
            "bd", "hr", "optional_part"
        };
        QSharedPointer<IPropertyBase> serializedPartsProperty(new propBodyPart(
            eObjParam_BODYPARTS, serializedPartMap, exactSerializedParts,
            recognizedPartOptions));
        CBodyPartEditForm bodyPartForm;
        bodyPartForm.setPartData(serializedPartsProperty);
        QTableWidget* bodyPartTable = bodyPartForm.findChild<QTableWidget*>("tablePart");
        QLineEdit* bodyPartFilter = bodyPartForm.findChild<QLineEdit*>("lineFilter");
        const bool serializedUnknownPartsVisible = bodyPartTable
            && bodyPartTable->findItems("hr", Qt::MatchExactly).size() == 1
            && bodyPartTable->findItems("future_part", Qt::MatchExactly).size() == 1;
        if(bodyPartFilter) bodyPartFilter->setText("hr");
        bool bodyPartFilterDisplayOnly = bodyPartTable && bodyPartFilter;
        if(bodyPartTable)
            for(int row = 0; row < bodyPartTable->rowCount(); ++row)
                bodyPartFilterDisplayOnly = bodyPartFilterDisplayOnly
                    && (bodyPartTable->item(row, 0)->text() == "hr"
                        ? !bodyPartTable->isRowHidden(row)
                        : bodyPartTable->isRowHidden(row));
        auto filterShowsOnly = [&](const QString& filter,
                                   const QStringList& visibleParts) {
            if(!bodyPartTable || !bodyPartFilter) return false;
            bodyPartFilter->setText(filter);
            for(int row = 0; row < bodyPartTable->rowCount(); ++row)
            {
                const QString part = bodyPartTable->item(row, 0)->text();
                if(bodyPartTable->isRowHidden(row) == visibleParts.contains(part))
                    return false;
            }
            return true;
        };
        const QStringList checkedParts = {"bd", "hr", "future_part"};
        const QStringList uncheckedParts = {"optional_part"};
        const bool bodyPartStateFilterAliases =
            filterShowsOnly("checked", checkedParts)
            && filterShowsOnly("true", checkedParts)
            && filterShowsOnly("on", checkedParts)
            && filterShowsOnly("enabled", checkedParts)
            && filterShowsOnly("unchecked", uncheckedParts)
            && filterShowsOnly("false", uncheckedParts)
            && filterShowsOnly("off", uncheckedParts)
            && filterShowsOnly("disabled", uncheckedParts);
        bool bodyPartStateTextHidden = bodyPartTable != nullptr;
        bool bodyPartColumnsResizable = bodyPartTable != nullptr;
        if(bodyPartTable)
        {
            for(int row = 0; row < bodyPartTable->rowCount(); ++row)
                bodyPartStateTextHidden = bodyPartStateTextHidden
                    && bodyPartTable->item(row, 1)
                    && bodyPartTable->item(row, 1)->text().isEmpty();
            for(int column = 0; column < bodyPartTable->columnCount(); ++column)
                bodyPartColumnsResizable = bodyPartColumnsResizable
                    && bodyPartTable->horizontalHeader()->sectionResizeMode(column)
                        == QHeaderView::Interactive;
        }
        QTreeWidget validationTree;
        validationTree.setColumnCount(4);
        ei::configureValidationTreeHeader(validationTree.header());
        bool validationColumnsResizable = true;
        for(int column = 0; column < validationTree.columnCount(); ++column)
            validationColumnsResizable = validationColumnsResizable
                && validationTree.header()->sectionResizeMode(column)
                    == QHeaderView::Interactive;
        const bool bodyPartSourceFiltering =
            filterShowsOnly("model option", {"optional_part"})
            && filterShowsOnly("unknown", {"future_part"});
        bodyPartFilter->clear();
        bodyPartTable->sortItems(0, Qt::DescendingOrder);
        const bool bodyPartColumnSorting = bodyPartTable->isSortingEnabled()
            && bodyPartTable->horizontalHeader()->sectionsClickable()
            && bodyPartTable->item(0, 0)->text() == "optional_part";
        bool bodyPartMultiSelectionToggle = false;
        if(bodyPartTable)
        {
            QTableWidgetItem* bodyItem = bodyPartTable->findItems(
                "bd", Qt::MatchExactly).value(0, nullptr);
            QTableWidgetItem* hairItem = bodyPartTable->findItems(
                "hr", Qt::MatchExactly).value(0, nullptr);
            if(bodyItem && hairItem)
            {
                bodyPartTable->selectionModel()->clearSelection();
                bodyPartTable->selectionModel()->select(
                    bodyPartTable->model()->index(bodyItem->row(), 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
                bodyPartTable->selectionModel()->select(
                    bodyPartTable->model()->index(hairItem->row(), 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
                QCheckBox* bodyCheck = qobject_cast<QCheckBox*>(
                    bodyPartTable->cellWidget(bodyItem->row(), 1));
                QCheckBox* hairCheck = qobject_cast<QCheckBox*>(
                    bodyPartTable->cellWidget(hairItem->row(), 1));
                if(bodyCheck && hairCheck)
                {
                    const bool checkBoxesDistinct = bodyCheck != hairCheck;
                    const bool selectionInitiallyMultiple =
                        bodyPartTable->selectionModel()->selectedRows().size() == 2;
                    const bool bothInitiallyChecked =
                        bodyCheck->checkState() == Qt::Checked
                        && hairCheck->checkState() == Qt::Checked;
                    // A real table-cell click can update the view selection
                    // before the embedded checkbox receives its mouse press.
                    // Reproduce that ordering instead of dispatching only to
                    // the checkbox, which used to hide the regression.
                    bodyPartTable->selectionModel()->select(
                        bodyPartTable->model()->index(bodyItem->row(), 0),
                        QItemSelectionModel::ClearAndSelect
                            | QItemSelectionModel::Rows);
                    const bool selectionCollapsedBeforeCheckBox =
                        bodyPartTable->selectionModel()->selectedRows().size() == 1;
                    const QPointF clickPoint(bodyCheck->rect().center());
                    QMouseEvent checkPress(QEvent::MouseButtonPress, clickPoint,
                                           Qt::LeftButton, Qt::LeftButton,
                                           Qt::NoModifier);
                    QApplication::sendEvent(bodyCheck, &checkPress);
                    QMouseEvent checkRelease(QEvent::MouseButtonRelease, clickPoint,
                                             Qt::LeftButton, Qt::NoButton,
                                             Qt::NoModifier);
                    QApplication::sendEvent(bodyCheck, &checkRelease);
                    bodyPartMultiSelectionToggle =
                        checkBoxesDistinct
                        && selectionInitiallyMultiple
                        && bothInitiallyChecked
                        && selectionCollapsedBeforeCheckBox
                        && bodyPartTable->selectionMode() == QAbstractItemView::ExtendedSelection
                        && bodyPartTable->selectionBehavior() == QAbstractItemView::SelectRows
                        && bodyPartTable->selectionModel()->selectedRows().size() == 2
                        && bodyCheck->checkState() == Qt::Unchecked
                        && hairCheck->checkState() == Qt::Unchecked;
                    QTableWidgetItem* optionalItem = bodyPartTable->findItems(
                        "optional_part", Qt::MatchExactly).value(0, nullptr);
                    QCheckBox* optionalCheck = optionalItem
                        ? qobject_cast<QCheckBox*>(bodyPartTable->cellWidget(
                              optionalItem->row(), 1))
                        : nullptr;
                    if(optionalItem && optionalCheck)
                    {
                        bodyPartTable->selectionModel()->select(
                            bodyPartTable->model()->index(optionalItem->row(), 0),
                            QItemSelectionModel::ClearAndSelect
                                | QItemSelectionModel::Rows);
                        const QPointF optionalPoint(optionalCheck->rect().center());
                        QMouseEvent optionalPress(QEvent::MouseButtonPress,
                                                  optionalPoint, Qt::LeftButton,
                                                  Qt::LeftButton, Qt::NoModifier);
                        QApplication::sendEvent(optionalCheck, &optionalPress);
                        QMouseEvent optionalRelease(QEvent::MouseButtonRelease,
                                                    optionalPoint, Qt::LeftButton,
                                                    Qt::NoButton, Qt::NoModifier);
                        QApplication::sendEvent(optionalCheck, &optionalRelease);
                        bodyPartMultiSelectionToggle = bodyPartMultiSelectionToggle
                            && bodyPartTable->selectionModel()->selectedRows().size() == 1
                            && optionalCheck->checkState() == Qt::Checked
                            && bodyCheck->checkState() == Qt::Unchecked
                            && hairCheck->checkState() == Qt::Unchecked;
                    }
                    else
                    {
                        bodyPartMultiSelectionToggle = false;
                    }
                }
            }
        }
        const bool humanoidHairOptionAvailable =
            util::knownBodyPartOptions("unhufe").contains("hr")
            && util::knownBodyPartOptions("unhuma.mod").contains("hr")
            && !util::knownBodyPartOptions("unwolf").contains("hr");
        QSettings bodyPartSettings;
        const QString bodyPartSizeKey = "ui/bodyPartEditorSize";
        const bool hadBodyPartSize = bodyPartSettings.contains(bodyPartSizeKey);
        const QVariant previousBodyPartSize = bodyPartSettings.value(bodyPartSizeKey);
        const QSize testBodyPartSize(612, 734);
        bodyPartForm.resize(testBodyPartSize);
        bodyPartForm.close();
        CBodyPartEditForm restoredBodyPartForm;
        const bool bodyPartWindowSizePersists =
            restoredBodyPartForm.size() == testBodyPartSize;
        if(hadBodyPartSize)
            bodyPartSettings.setValue(bodyPartSizeKey, previousBodyPartSize);
        else
            bodyPartSettings.remove(bodyPartSizeKey);
        const propBodyPart* serializedParts =
            dynamic_cast<const propBodyPart*>(serializedPartsProperty.get());
        const bool bodyPartNoOpPreservesExactArray = serializedParts
            && serializedParts->valueAppliedTo(exactSerializedParts)
                == exactSerializedParts;
        propBodyPart reorderedParts(eObjParam_BODYPARTS, serializedPartMap,
            QStringList{"hr", "bd", "future_part", "future_part"});
        const bool bodyPartOrderParticipatesInEquality = serializedParts
            && !serializedParts->isEqual(&reorderedParts);
        QMap<QString, QSharedPointer<propBool>> changedPartMap;
        for(const QString& part : QStringList{"bd", "hr", "future_part"})
            changedPartMap.insert(part, QSharedPointer<propBool>(
                new propBool(eObjParam_BODYPARTS, true)));
        propBodyPart changedParts(eObjParam_BODYPARTS, changedPartMap,
                                  exactSerializedParts);
        changedParts.value().value("bd")->resetValue(false);
        const QStringList changedSerializedParts =
            changedParts.valueAppliedTo(exactSerializedParts);
        const bool bodyPartDeltaPreservesHiddenAndDuplicates =
            !changedSerializedParts.contains("bd")
            && changedSerializedParts.contains("hr")
            && changedSerializedParts.count("future_part") == 2;

        QList<QSharedPointer<IPropertyBase>> bodyPartCopySource;
        bodyPartCopySource << QSharedPointer<IPropertyBase>(
                                  new propStr(eObjParam_TEMPLATE, "unhufe"))
                           << serializedPartsProperty;
        QTableWidget bodyPartCopySourceTable;
        CTableManager bodyPartCopySourceManager(&bodyPartCopySourceTable);
        bodyPartCopySourceManager.setNewData(bodyPartCopySource);
        const int bodyPartCopySourceRow = rowForType(
            bodyPartCopySourceTable, eObjParam_BODYPARTS);
        if(bodyPartCopySourceRow >= 0)
            bodyPartCopySourceTable.item(bodyPartCopySourceRow, 0)->setSelected(true);
        QKeyEvent copyBodyParts(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(&bodyPartCopySourceTable, &copyBodyParts);
        QMap<QString, QSharedPointer<propBool>> destinationPartMap;
        destinationPartMap.insert("bd", QSharedPointer<propBool>(
            new propBool(eObjParam_BODYPARTS, true)));
        destinationPartMap.insert("hr", QSharedPointer<propBool>(
            new propBool(eObjParam_BODYPARTS, false)));
        QList<QSharedPointer<IPropertyBase>> bodyPartCopyDestination;
        bodyPartCopyDestination << QSharedPointer<IPropertyBase>(
                                       new propStr(eObjParam_TEMPLATE, "unhufe"))
                                << QSharedPointer<IPropertyBase>(new propBodyPart(
                                       eObjParam_BODYPARTS, destinationPartMap,
                                       QStringList{"bd"}, QSet<QString>{"bd", "hr"}));
        QTableWidget bodyPartCopyDestinationTable;
        CTableManager bodyPartCopyDestinationManager(&bodyPartCopyDestinationTable);
        bodyPartCopyDestinationManager.setNewData(bodyPartCopyDestination);
        const int bodyPartCopyDestinationRow = rowForType(
            bodyPartCopyDestinationTable, eObjParam_BODYPARTS);
        if(bodyPartCopyDestinationRow >= 0)
            bodyPartCopyDestinationTable.item(bodyPartCopyDestinationRow, 0)
                ->setSelected(true);
        QSharedPointer<IPropertyBase> pastedBodyParts;
        QObject::connect(&bodyPartCopyDestinationManager,
                         &CTableManager::onUpdateProperty,
                         [&pastedBodyParts](const QSharedPointer<IPropertyBase>& value)
        {
            pastedBodyParts.reset(value->clone());
        });
        QKeyEvent pasteBodyParts(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
        QApplication::sendEvent(&bodyPartCopyDestinationTable, &pasteBodyParts);
        const propBodyPart* pastedBodyPartValue =
            dynamic_cast<const propBodyPart*>(pastedBodyParts.get());
        const bool bodyPartPropertyPasteIsExact = bodyPartCopySourceRow >= 0
            && bodyPartCopyDestinationRow >= 0 && pastedBodyPartValue
            && pastedBodyPartValue->valueAppliedTo(QStringList{"bd"})
                == exactSerializedParts;

        QTemporaryDir landscapeSettingsDirectory;
        if(landscapeSettingsDirectory.isValid())
        {
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                               landscapeSettingsDirectory.path());
            QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                               landscapeSettingsDirectory.path());
        }
        CLandscapeTools landscapeTools;
        const bool minimumPaintBrushIsSingleTile = qFuzzyIsNull(
            landscapeTools.paintRadius());
        landscapeTools.setPaintTerrainTypes(
            {{2, "Ground"}}, {{1, "Underwater ground"}}, {{5, "Swamp"}});
        landscapeTools.activateMode(ELandscapeEditMode::TilePaint, false);
        const bool underwaterFamiliesVisibleByDefault =
            landscapeTools.underwaterFamiliesVisible()
            && landscapeTools.selectPaintTerrainType(1);
        landscapeTools.setUnderwaterFamiliesVisible(false);
        const bool underwaterFamiliesRemainVisible =
            landscapeTools.underwaterFamiliesVisible()
            && landscapeTools.selectPaintTerrainType(1);
        const bool landscapeLayersVisibleByDefault =
            landscapeTools.mainTerrainVisible() && landscapeTools.overlaysVisible();
        landscapeTools.setTerrainVisibility(false, true);
        const bool landscapeVisibilityControlsWork =
            !landscapeTools.mainTerrainVisible() && landscapeTools.overlaysVisible();
        landscapeTools.activateMode(ELandscapeEditMode::OverlaySurface, false);
        const bool overlaySurfaceToolSeparate = landscapeTools.overlaySurfaceEnabled()
            && landscapeTools.overlaySurfaceOperation()
                == EOverlaySurfaceOperation::Activate
            && !landscapeTools.manualTileSelectionEnabled()
            && landscapeTools.selectPaintTerrainType(5);
        const bool overlaySurfaceAdmissionFollowsPickedLayer =
            CView::terrainPaintPickUsesLandLayer(true, true)
            && CView::terrainPaintPickUsesLandLayer(false, true)
            && !CView::terrainPaintPickUsesLandLayer(false, false);
        QCheckBox* overlaySlotLabels = landscapeTools.findChild<QCheckBox*>(
            QStringLiteral("showOverlayMaterialSlotLabels"));
        const bool overlaySlotLabelsAvailableInOverlaySurface = overlaySlotLabels
            && !overlaySlotLabels->isHidden()
            && !landscapeTools.overlayMaterialSlotLabelsVisible();
        landscapeTools.activateMode(ELandscapeEditMode::OverlayPaint, false);
        landscapeTools.setOverlayMaterialSlotLabelsVisible(true);
        const bool overlaySlotLabelsAvailableInOverlayPaint = overlaySlotLabels
            && !overlaySlotLabels->isHidden()
            && landscapeTools.overlayMaterialSlotLabelsVisible();
        landscapeTools.activateMode(ELandscapeEditMode::TilePaint, false);
        const bool overlaySlotLabelsHiddenOutsideOverlayModes = overlaySlotLabels
            && overlaySlotLabels->isHidden();
        bool landModeSelectionValid = false;
        const bool programmaticModeRefreshesMaterials =
            !landscapeTools.selectPaintTerrainType(5)
            && landscapeTools.paintTerrainType(&landModeSelectionValid) == 1
            && landModeSelectionValid;
        landscapeTools.activateMode(ELandscapeEditMode::Sculpt, false);
        QSpinBox* flattenTarget = landscapeTools.findChild<QSpinBox*>(
            QStringLiteral("landscapeFlattenTarget"));
        QComboBox* sculptFalloff = landscapeTools.findChild<QComboBox*>(
            QStringLiteral("landscapeSculptFalloff"));
        QComboBox* sculptTool = landscapeTools.findChild<QComboBox*>(
            QStringLiteral("landscapeSculptTool"));
        QDoubleSpinBox* raiseLowerStep = landscapeTools.findChild<QDoubleSpinBox*>(
            QStringLiteral("landscapeRaiseLowerStep"));
        const ELandscapeSculptTool initialSculptTool = landscapeTools.sculptTool();
        const quint16 initialFlattenHeight = landscapeTools.flattenHeight();
        landscapeTools.setSculptTool(ELandscapeSculptTool::Raise);
        const bool sculptRaiseShowsFalloff = flattenTarget && sculptFalloff
            && flattenTarget->isHidden() && !sculptFalloff->isHidden();
        landscapeTools.setSculptTool(ELandscapeSculptTool::Flatten);
        const bool sculptFlattenShowsTarget = flattenTarget && sculptFalloff
            && !flattenTarget->isHidden() && !sculptFalloff->isHidden();
        const bool sculptToolOrderMatchesShortcuts = sculptTool
            && sculptTool->count() == 5
            && sculptTool->itemData(0).toInt() == int(ELandscapeSculptTool::Raise)
            && sculptTool->itemData(1).toInt() == int(ELandscapeSculptTool::Lower)
            && sculptTool->itemData(2).toInt() == int(ELandscapeSculptTool::Smooth)
            && sculptTool->itemData(3).toInt() == int(ELandscapeSculptTool::Flatten)
            && sculptTool->itemData(4).toInt() == int(ELandscapeSculptTool::Noise);
        landscapeTools.setFlattenHeight(43210);
        const bool flattenHeightCanBeSampled = landscapeTools.flattenHeight() == 43210;
        if(raiseLowerStep)
            raiseLowerStep->setValue(500.0);
        const float priorDelta = landscapeTools.rawDelta();
        landscapeTools.adjustRawDelta(1);
        const bool sculptDeltaWheelAdjustmentWorks =
            raiseLowerStep
            && qFuzzyCompare(landscapeTools.rawDelta() + 1.0f,
                             priorDelta + 101.0f);
        const quint16 priorFlattenHeight = landscapeTools.flattenHeight();
        landscapeTools.adjustFlattenHeight(1);
        const bool flattenTargetWheelAdjustmentWorks =
            landscapeTools.flattenHeight() == priorFlattenHeight + 100;
        bool mprSettingsRequested = false;
        QObject::connect(&landscapeTools, &CLandscapeTools::openMprSettingsRequested,
                         [&mprSettingsRequested]() { mprSettingsRequested = true; });
        if(QPushButton* button = landscapeTools.findChild<QPushButton*>(
               QStringLiteral("openMprSettingsFromLandscape")))
            button->click();
        const bool landscapeMprSettingsButtonWorks = mprSettingsRequested;
        landscapeTools.adjustRawDelta(-1);
        landscapeTools.setFlattenHeight(initialFlattenHeight);
        landscapeTools.setSculptTool(initialSculptTool);
        QSettings minimapSizeSettings;
        const QVariant savedLegacyMinimapSize = minimapSizeSettings.value(
            "ui/viewportMinimapSizeLevel");
        const QVariant savedMinimapSizeV2 = minimapSizeSettings.value(
            "ui/viewportMinimapSizeLevelV2");
        minimapSizeSettings.setValue("ui/viewportMinimapSizeLevel", 2);
        // A previous run may have persisted the largest V2 size. New editor
        // sessions must still begin with the compact viewport minimap.
        minimapSizeSettings.setValue("ui/viewportMinimapSizeLevelV2", 2);
        CViewportMinimapWidget viewportMinimap;
        QImage generatedMinimap(256, 256, QImage::Format_ARGB32);
        generatedMinimap.fill(Qt::darkGreen);
        QImage embeddedMinimap(256, 256, QImage::Format_ARGB32);
        embeddedMinimap.fill(Qt::darkBlue);
        viewportMinimap.setMapDimensions(11, 9);
        viewportMinimap.setGeneratedPreview(generatedMinimap, false, "generated");
        viewportMinimap.setEmbeddedPreview("zone1map", embeddedMinimap,
                                           "C:/fixtures/textures.res");
        viewportMinimap.preferGenerated(false);
        const bool embeddedMinimapDefaultsWhenAvailable =
            !viewportMinimap.usesGeneratedPreview();
        viewportMinimap.preferGenerated(true);
        const bool generatedMinimapCanBeSelected =
            viewportMinimap.usesGeneratedPreview()
            && viewportMinimap.hasGeneratedPreview()
            && viewportMinimap.hasEmbeddedPreview();
        QToolButton* minimapSmaller = viewportMinimap.findChild<QToolButton*>(
            "viewportMinimapSmaller");
        QToolButton* minimapLarger = viewportMinimap.findChild<QToolButton*>(
            "viewportMinimapLarger");
        const int originalMinimapSizeLevel = viewportMinimap.sizeLevel();
        const bool viewportMinimapStartsAtSmallest =
            originalMinimapSizeLevel == 0
            && viewportMinimap.size() == QSize(390, 430);
        while(minimapSmaller && minimapSmaller->isEnabled())
            minimapSmaller->click();
        const QSize minimapSmallSize = viewportMinimap.size();
        if(minimapLarger) minimapLarger->click();
        const QSize minimapMediumSize = viewportMinimap.size();
        if(minimapLarger) minimapLarger->click();
        const QSize minimapLargeSize = viewportMinimap.size();
        const bool viewportMinimapHasThreeSizes = minimapSmaller && minimapLarger
            && minimapSmallSize == QSize(390, 430)
            && minimapMediumSize == QSize(520, 560)
            && minimapLargeSize == QSize(910, 980)
            && !minimapLarger->isEnabled();
        QToolButton* minimapExpand = viewportMinimap.findChild<QToolButton*>(
            "viewportMinimapExpand");
        QLabel* minimapTitle = viewportMinimap.findChild<QLabel*>(
            "viewportMinimapTitle");
        QLabel* minimapImage = viewportMinimap.findChild<QLabel*>(
            "viewportMinimapImage");
        if(minimapExpand) minimapExpand->click();
        const bool viewportMinimapCollapsesToTitle = minimapExpand
            && minimapTitle && minimapImage
            && !minimapTitle->isHidden() && minimapImage->isHidden()
            && viewportMinimap.size() == QSize(116, 38);
        QMouseEvent minimapBackgroundPress(QEvent::MouseButtonPress,
                                            QPointF(2, 2), Qt::LeftButton,
                                            Qt::LeftButton, Qt::NoModifier);
        minimapBackgroundPress.setAccepted(false);
        QApplication::sendEvent(&viewportMinimap, &minimapBackgroundPress);
        QMouseEvent minimapChildPress(QEvent::MouseButtonPress,
                                     QPointF(2, 2), Qt::LeftButton,
                                     Qt::LeftButton, Qt::NoModifier);
        minimapChildPress.setAccepted(false);
        if(minimapTitle)
            QApplication::sendEvent(minimapTitle, &minimapChildPress);
        const bool viewportMinimapBlocksClickThrough =
            minimapBackgroundPress.isAccepted() && minimapTitle
            && minimapChildPress.isAccepted();
        if(minimapExpand) minimapExpand->click();
        viewportMinimap.layout()->activate();
        QVector<QPointF> minimapDragPoints;
        QObject::connect(&viewportMinimap,
                         &CViewportMinimapWidget::cameraMoveRequested,
                         [&minimapDragPoints](const QPointF& point) {
            minimapDragPoints.append(point);
        });
        viewportMinimap.moveCameraFromNormalizedPoint(QPointF(0.25, 0.25));
        viewportMinimap.moveCameraFromNormalizedPoint(QPointF(0.50, 0.50));
        viewportMinimap.moveCameraFromNormalizedPoint(QPointF(0.75, 0.75));
        const bool viewportMinimapDragNavigationWorks =
            minimapDragPoints.size() >= 3
            && minimapDragPoints.first().x() < minimapDragPoints.last().x()
            && minimapDragPoints.first().y() > minimapDragPoints.last().y();
        QToolButton* minimapRefresh = viewportMinimap.findChild<QToolButton*>(
            "viewportMinimapRefresh");
        QToolButton* minimapLock = viewportMinimap.findChild<QToolButton*>(
            "viewportMinimapLock");
        bool minimapRefreshRequested = false;
        bool minimapLockRequested = false;
        QObject::connect(&viewportMinimap,
                         &CViewportMinimapWidget::regenerationRequested,
                         [&minimapRefreshRequested]() { minimapRefreshRequested = true; });
        QObject::connect(&viewportMinimap,
                         &CViewportMinimapWidget::generationLockRequested,
                         [&minimapLockRequested](bool locked) {
            minimapLockRequested = locked;
        });
        viewportMinimap.preferGenerated(false);
        if(minimapRefresh) minimapRefresh->click();
        viewportMinimap.setGeneratedPreview(generatedMinimap, false,
                                            "regenerated");
        const bool minimapRefreshSelectsGenerated =
            viewportMinimap.usesGeneratedPreview();
        if(minimapLock) minimapLock->click();
        viewportMinimap.setGenerationLocked(true);
        const bool minimapRefreshDisabledWhenLocked = minimapRefresh
            && !minimapRefresh->isEnabled();
        const bool minimapLockCheckedWhenLocked = minimapLock
            && minimapLock->isChecked();
        const bool minimapLockSignalWhenLocked = minimapLockRequested;
        QImage minimapSurface(viewportMinimap.size(), QImage::Format_ARGB32);
        minimapSurface.fill(Qt::transparent);
        viewportMinimap.render(&minimapSurface);
        const bool minimapOverlayPaintContract =
            viewportMinimap.testAttribute(Qt::WA_OpaquePaintEvent)
            && viewportMinimap.testAttribute(Qt::WA_StyledBackground)
            && minimapSurface.pixelColor(1, 1).alpha() == 255
            && minimapSurface.pixelColor(minimapSurface.width() - 2,
                                         minimapSurface.height() - 2).alpha() == 255;
        const bool viewportMinimapLockControlsWork = minimapRefresh && minimapLock
            && minimapRefreshRequested && minimapLockSignalWhenLocked
            && minimapRefreshDisabledWhenLocked && minimapLockCheckedWhenLocked
            && minimapOverlayPaintContract;
        if(minimapLock) minimapLock->click();
        viewportMinimap.setGenerationLocked(false);
        const bool viewportMinimapUnlockWorks = !minimapLockRequested
            && minimapRefresh->isEnabled() && !minimapLock->isChecked();
        if(savedLegacyMinimapSize.isValid())
            minimapSizeSettings.setValue("ui/viewportMinimapSizeLevel",
                                         savedLegacyMinimapSize);
        else
            minimapSizeSettings.remove("ui/viewportMinimapSizeLevel");
        if(savedMinimapSizeV2.isValid())
            minimapSizeSettings.setValue("ui/viewportMinimapSizeLevelV2",
                                         savedMinimapSizeV2);
        else
            minimapSizeSettings.remove("ui/viewportMinimapSizeLevelV2");
        SMinimapSceneCaptureResult exportCapture;
        exportCapture.bake.success = true;
        QImage gradeFixture(16, 16, QImage::Format_ARGB32);
        gradeFixture.fill(Qt::darkGreen);
        exportCapture.bake.gameImage = gradeFixture;
        exportCapture.bake.reviewImage = gradeFixture;
        exportCapture.unlitGameImage = gradeFixture;
        exportCapture.sectorWidth = 11;
        exportCapture.sectorHeight = 9;
        exportCapture.bake.gameContentRect = minimapGameContentRect(11, 9, 16);
        exportCapture.exportProfileDescription = "synthetic";
        CMinimapExportDialog* minimapExport = new CMinimapExportDialog(
            exportCapture, gradeFixture, "embeddedmap", "syntheticmap", QString(),
            SMinimapGradeSettings(), false);
        minimapExport->setAttribute(Qt::WA_DeleteOnClose, false);
        QSlider* shadingSlider = minimapExport->findChild<QSlider*>(
            "minimapShadingSlider");
        QSpinBox* shadingValue = minimapExport->findChild<QSpinBox*>(
            "minimapShadingValue");
        QSlider* hueSlider = minimapExport->findChild<QSlider*>(
            "minimapHueSlider");
        QSpinBox* hueValue = minimapExport->findChild<QSpinBox*>(
            "minimapHueValue");
        QPushButton* resetGrade = minimapExport->findChild<QPushButton*>(
            "minimapResetGrade");
        QPushButton* actualPreview = minimapExport->findChild<QPushButton*>(
            "minimapActualPreview");
        QPushButton* fitPreview = minimapExport->findChild<QPushButton*>(
            "minimapFitPreview");
        QPushButton* linkViews = minimapExport->findChild<QPushButton*>(
            "minimapLinkViews");
        QGraphicsView* generatedPreview = minimapExport->findChild<QGraphicsView*>(
            "minimapExportPreview");
        QGraphicsView* embeddedPreview = minimapExport->findChild<QGraphicsView*>(
            "minimapEmbeddedPreview");
        const auto normalizedViewCenter = [](QGraphicsView* view) {
            if(!view || view->sceneRect().isEmpty()) return QPointF();
            const QRectF rect = view->sceneRect();
            const QPointF center = view->mapToScene(view->viewport()->rect().center());
            return QPointF((center.x() - rect.left()) / rect.width(),
                           (center.y() - rect.top()) / rect.height());
        };
        if(actualPreview) actualPreview->click();
        const QTransform previewTransform = generatedPreview
            ? generatedPreview->transform() : QTransform();
        const QPointF previewCenter = normalizedViewCenter(generatedPreview);
        if(shadingValue) shadingValue->setValue(137);
        if(hueSlider) hueSlider->setValue(-73);
        QPushButton* applyLock = minimapExport->findChild<QPushButton*>(
            "minimapApplyLock");
        const Qt::WindowFlags exportFlags = minimapExport->windowFlags();
        const bool minimapExportIsModeless = !minimapExport->isModal()
            && minimapExport->parentWidget() == nullptr
            && (exportFlags & Qt::WindowMinMaxButtonsHint)
            && (exportFlags & Qt::WindowCloseButtonHint)
            && !minimapExport->windowIcon().isNull()
            && applyLock && applyLock->minimumWidth() >= 180;
        const bool minimapTypedGradeControlsWork = shadingSlider && shadingValue
            && hueSlider && hueValue && shadingSlider->maximum() == 150
            && shadingSlider->value() == 137 && shadingValue->value() == 137
            && hueSlider->minimum() == -180 && hueSlider->maximum() == 180
            && hueSlider->value() == -73 && hueValue->value() == -73;
        const bool minimapPreviewCropAndViewPersist = generatedPreview
            && generatedPreview->sceneRect().size() == QSizeF(16, 13)
            && generatedPreview->transform() == previewTransform
            && QLineF(normalizedViewCenter(generatedPreview), previewCenter).length()
                < 0.001;
        if(generatedPreview)
        {
            generatedPreview->scale(100.0, 100.0);
            generatedPreview->horizontalScrollBar()->setValue(
                generatedPreview->horizontalScrollBar()->maximum() / 3);
            generatedPreview->verticalScrollBar()->setValue(
                generatedPreview->verticalScrollBar()->maximum() * 2 / 3);
        }
        const bool minimapViewsLinkByDefault = linkViews && linkViews->isChecked()
            && generatedPreview && embeddedPreview
            && generatedPreview->transform() == embeddedPreview->transform()
            && QLineF(normalizedViewCenter(generatedPreview),
                      normalizedViewCenter(embeddedPreview)).length() < 0.02;
        if(fitPreview) fitPreview->click();
        const QTransform fittedTransform = generatedPreview
            ? generatedPreview->transform() : QTransform();
        const QPointF fittedCenter = normalizedViewCenter(generatedPreview);
        if(shadingValue) shadingValue->setValue(111);
        if(fitPreview) fitPreview->click();
        const bool minimapFitDoesNotDrift = fitPreview && generatedPreview
            && generatedPreview->transform() == fittedTransform
            && QLineF(normalizedViewCenter(generatedPreview), fittedCenter).length()
                < 0.001;
        if(resetGrade) resetGrade->click();
        const SMinimapGradeSettings defaultGrade;
        const bool minimapGradeResetWorks = resetGrade
            && shadingValue->value() == defaultGrade.shadingPercent
            && hueValue->value() == defaultGrade.hueDegrees;
        delete minimapExport;
        while(minimapSmaller
              && viewportMinimap.sizeLevel() > originalMinimapSizeLevel)
            minimapSmaller->click();
        STerrainPaintMaterialChoice irregularOnDark;
        irregularOnDark.id = 9;
        irregularOnDark.name = "Irregular — on Dark";
        irregularOnDark.cornerOnly = true;
        STerrainPaintMaterialChoice irregularOnStone = irregularOnDark;
        irregularOnStone.name = "Irregular — on Stone";
        landscapeTools.setPaintMaterials(
            {irregularOnDark, irregularOnStone}, {}, {}, 2, 2);
        landscapeTools.activateMode(ELandscapeEditMode::TilePaint, false);
        int irregularButtons = 0;
        for(QToolButton* button : landscapeTools.findChildren<QToolButton*>())
            irregularButtons += button->property("terrainMaterialId").toInt() == 9;
        bool irregularSelected = false;
        const bool uniqueMaterialPaletteCards = irregularButtons == 1
            && landscapeTools.selectPaintTerrainType(9)
            && landscapeTools.paintTerrainType(&irregularSelected) == 9
            && irregularSelected;
        STerrainPaintMaterialChoice darkGrass;
        darkGrass.id = 101;
        darkGrass.name = "Grass / Dark";
        darkGrass.familyName = "Grass";
        STerrainPaintMaterialChoice freshGrass = darkGrass;
        freshGrass.id = 102;
        freshGrass.name = "Grass / Fresh";
        landscapeTools.setPaintMaterials(
            {darkGrass, freshGrass}, {}, {}, 2, 2);
        int grassVariantButtons = 0;
        int grassFamilyHeadings = 0;
        for(QToolButton* button : landscapeTools.findChildren<QToolButton*>())
            grassVariantButtons += button->property("terrainMaterialId").toInt() == 101
                || button->property("terrainMaterialId").toInt() == 102;
        for(QLabel* label : landscapeTools.findChildren<QLabel*>())
            grassFamilyHeadings += label->text() == QStringLiteral("Grass");
        const bool materialVariantsRemainDistinctAndGrouped =
            grassVariantButtons == 2 && grassFamilyHeadings == 1
            && landscapeTools.selectPaintTerrainType(101)
            && landscapeTools.selectPaintTerrainType(102);
        landscapeTools.resize(320, 900);
        landscapeTools.show();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QScrollArea* groundMaterialScroll =
            landscapeTools.findChild<QScrollArea*>("groundMaterialScroll");
        QScrollArea* overlayMaterialScroll =
            landscapeTools.findChild<QScrollArea*>("overlayMaterialScroll");
        QTabWidget* materialPaletteTabs =
            landscapeTools.findChild<QTabWidget*>("materialPaletteTabs");
        QTabWidget* modernMaterialLayerTabs =
            landscapeTools.findChild<QTabWidget*>("modernMaterialLayerTabs");
        QTabWidget* classicMaterialLayerTabs =
            landscapeTools.findChild<QTabWidget*>("classicMaterialLayerTabs");
        QLineEdit* terrainPaintFilter =
            landscapeTools.findChild<QLineEdit*>("terrainPaintFilter");
        QListWidget* manualAtlasTileList =
            landscapeTools.findChild<QListWidget*>("manualAtlasTileList");
        QSlider* terrainPaintRadius =
            landscapeTools.findChild<QSlider*>("terrainPaintRadius");
        if(materialPaletteTabs)
            materialPaletteTabs->setCurrentIndex(1);
        const bool threeTerrainPaintModesAvailable = materialPaletteTabs
            && materialPaletteTabs->count() == 3
            && materialPaletteTabs->tabText(0) == QStringLiteral("Modern (Z)")
            && materialPaletteTabs->tabText(1) == QStringLiteral("Classic Auto (X)")
            && materialPaletteTabs->tabText(2) == QStringLiteral("Manual (C)")
            && modernMaterialLayerTabs && classicMaterialLayerTabs
            && terrainPaintFilter && manualAtlasTileList;
        landscapeTools.setClassicAutoMaterials(
            {darkGrass, freshGrass}, {}, {});
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QToolButton* classicDarkButton = nullptr;
        QToolButton* classicFreshButton = nullptr;
        if(classicMaterialLayerTabs)
            for(QToolButton* button :
                classicMaterialLayerTabs->findChildren<QToolButton*>())
            {
                const int id = button->property("terrainMaterialId").toInt();
                if(id == 101) classicDarkButton = button;
                if(id == 102) classicFreshButton = button;
            }
        const bool classicPaletteStartsInMultipleColumns =
            classicDarkButton && classicFreshButton
            && classicDarkButton->geometry().top()
                == classicFreshButton->geometry().top()
            && classicDarkButton->geometry().left()
                != classicFreshButton->geometry().left();
        const int paletteBottom = materialPaletteTabs
            ? materialPaletteTabs->mapTo(
                  &landscapeTools, materialPaletteTabs->rect().bottomLeft()).y()
            : -1;
        const int radiusTop = terrainPaintRadius
            ? terrainPaintRadius->mapTo(
                  &landscapeTools, terrainPaintRadius->rect().topLeft()).y()
            : -1;
        const bool classicPaletteFirstOpenDoesNotOverlapControls =
            materialPaletteTabs && terrainPaintRadius
            && paletteBottom < radiusTop
            && classicPaletteStartsInMultipleColumns;
        QToolButton* circlePaintShape =
            landscapeTools.findChild<QToolButton*>("circlePaintShape");
        QToolButton* squarePaintShape =
            landscapeTools.findChild<QToolButton*>("squarePaintShape");
        QToolButton* tilePaintTarget =
            landscapeTools.findChild<QToolButton*>("tilePaintTarget");
        QToolButton* vertexPaintTarget =
            landscapeTools.findChild<QToolButton*>("vertexPaintTarget");
        QCheckBox* classicAutoSafe =
            landscapeTools.findChild<QCheckBox*>("classicAutoSafeMode");
        QPushButton* resolveMissingBlends =
            landscapeTools.findChild<QPushButton*>("resolveMissingBlendsButton");
        STerrainPaintMaterialChoice irregularStamp = irregularOnDark;
        irregularStamp.supportId = 3;
        irregularStamp.contextualStamp = true;
        irregularStamp.familyName = QStringLiteral("Grass");
        landscapeTools.setClassicAutoMaterials({irregularStamp}, {}, {});
        landscapeTools.setPaintPaletteMode(
            ETerrainPaintPaletteMode::ClassicAuto);
        if(vertexPaintTarget) vertexPaintTarget->setChecked(true);
        QToolButton* contextualStampButton = nullptr;
        for(QToolButton* button : landscapeTools.findChildren<QToolButton*>())
            if(button->property("terrainContextualStamp").toBool()
               && button->property("terrainMaterialId").toInt() == 9
               && button->property("terrainSupportMaterialId").toInt() == 3)
            {
                contextualStampButton = button;
                break;
            }
        bool contextualSelected = false;
        const bool contextualStampVisibleForVertex = contextualStampButton
            && contextualStampButton->isVisible()
            && landscapeTools.selectPaintTerrainType(9)
            && landscapeTools.paintTerrainType(&contextualSelected) == 9
            && contextualSelected
            && landscapeTools.paintTerrainSupport() == 3;
        if(tilePaintTarget) tilePaintTarget->setChecked(true);
        bool tileStampSelected = true;
        landscapeTools.paintTerrainType(&tileStampSelected);
        const bool contextualStampHiddenForTile = contextualStampButton
            && !contextualStampButton->isVisible() && !tileStampSelected;
        if(classicAutoSafe) classicAutoSafe->setChecked(true);
        const bool tilePaintTargetKeepsShapeChoice = squarePaintShape
            && circlePaintShape && squarePaintShape->isEnabled()
            && circlePaintShape->isEnabled();
        const bool terrainPaintBetaControlsAreExplicit = circlePaintShape
            && squarePaintShape && tilePaintTarget && vertexPaintTarget
            && classicAutoSafe && resolveMissingBlends
            && circlePaintShape->styleSheet().contains(
                QStringLiteral("QToolButton:checked"))
            && tilePaintTarget->isChecked()
            && classicAutoSafe->isChecked()
            && tilePaintTargetKeepsShapeChoice;
        landscapeTools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        if(materialPaletteTabs)
            materialPaletteTabs->setCurrentIndex(0);
        QVector<QImage> manualTiles(2, QImage(16, 16,
                                             QImage::Format_RGBA8888));
        manualTiles[0].fill(Qt::red);
        manualTiles[1].fill(Qt::green);
        landscapeTools.setManualAtlasTiles(
            manualTiles, {1, 2},
            {QStringLiteral("Grass / Dark"),
             QStringLiteral("Stone / Lime")});
        terrainPaintFilter->setText(QStringLiteral("grass dark"));
        const bool sharedTerrainPaintFilterWorks = manualAtlasTileList
            && manualAtlasTileList->count() == 2
            && !manualAtlasTileList->item(0)->isHidden()
            && manualAtlasTileList->item(1)->isHidden();
        const bool manualAtlasGridUsesTooltips = manualAtlasTileList
            && manualAtlasTileList->viewMode() == QListView::IconMode
            && manualAtlasTileList->flow() == QListView::LeftToRight
            && manualAtlasTileList->isWrapping()
            && manualAtlasTileList->item(0)->text().isEmpty()
            && manualAtlasTileList->item(0)->toolTip().contains(
                QStringLiteral("Grass / Dark"));
        terrainPaintFilter->clear();
        const QList<QToolButton*> paletteButtons =
            landscapeTools.findChildren<QToolButton*>();
        const bool materialPaletteScrollsAndFillsWidth = groundMaterialScroll
            && overlayMaterialScroll && groundMaterialScroll->widgetResizable()
            && overlayMaterialScroll->widgetResizable()
            && groundMaterialScroll->horizontalScrollBarPolicy()
                == Qt::ScrollBarAlwaysOff
            && overlayMaterialScroll->horizontalScrollBarPolicy()
                == Qt::ScrollBarAlwaysOff
            && groundMaterialScroll->widget()->width()
                >= groundMaterialScroll->viewport()->width()
            && std::all_of(paletteButtons.cbegin(), paletteButtons.cend(),
                [](const QToolButton* button) {
                    return !button->property("terrainMaterialId").isValid()
                        || button->sizePolicy().horizontalPolicy()
                            == QSizePolicy::Expanding;
                });
        const bool landscapeToolsAreCompactAndPaletteFillsHeight =
            landscapeTools.minimumSizeHint().width() <= 320
            && landscapeTools.width() <= 320
            && materialPaletteTabs && materialPaletteTabs->height() >= 100
            // One full compact palette row remains usable after moving the
            // former prose help into tab tooltips. Font metrics can make this
            // 38 px in the retained headless Qt 5.12 configuration.
            && groundMaterialScroll && groundMaterialScroll->height() >= 38;
        landscapeTools.hide();
        QLabel* atlasSourceStatus =
            landscapeTools.findChild<QLabel*>("materialAtlasSourceStatus");
        landscapeTools.setPaintMaterials(
            {irregularOnDark}, {}, {}, 512, 512, QString(),
            QStringLiteral("Default atlas"));
        const bool defaultAtlasSourceVisible = atlasSourceStatus
            && atlasSourceStatus->text().contains(QStringLiteral("Default atlas"));
        const QString localAtlasPath = QStringLiteral(
            "C:/test/profiles/zone2.semantic-atlas.json");
        landscapeTools.setPaintMaterials(
            {irregularOnDark}, {}, {}, 512, 512, QString(),
            QStringLiteral("Local override: zone2.semantic-atlas.json"),
            localAtlasPath);
        const bool localAtlasSourceVisible = atlasSourceStatus
            && atlasSourceStatus->text().contains(QStringLiteral("Local override"))
            && atlasSourceStatus->toolTip().contains(
                QDir::toNativeSeparators(localAtlasPath));
        QLabel* generatedAtlasStatus =
            landscapeTools.findChild<QLabel*>("generatedAtlasTileCount");
        landscapeTools.setGeneratedAtlasTileCount(7);
        const bool generatedAtlasCountVisible = generatedAtlasStatus
            && generatedAtlasStatus->text() == QStringLiteral(
                "512 original + 7 added exact = 519 active slots");
        QProgressBar* atlasCapacity =
            landscapeTools.findChild<QProgressBar*>("terrainAtlasCapacity");
        QLabel* atlasCapacityWarning = landscapeTools.findChild<QLabel*>(
            "terrainAtlasCapacityWarning");
        const bool expandedAtlasCapacityIsYellow = atlasCapacity
            && atlasCapacity->value() == 519
            && atlasCapacity->styleSheet().contains(QStringLiteral("#ebbe23"))
            && atlasCapacityWarning && atlasCapacityWarning->text().isEmpty();
        landscapeTools.setPaintMaterials(
            {irregularOnDark}, {}, {}, 4096, 4096);
        landscapeTools.setGeneratedAtlasTileCount(1);
        const bool unstableAtlasCapacityIsRedButInformational = atlasCapacity
            && atlasCapacity->value() == 4097
            && atlasCapacity->styleSheet().contains(QStringLiteral("#cd3737"))
            && atlasCapacityWarning
            && atlasCapacityWarning->text().contains(
                QStringLiteral("Staging remains available"));
        bool semanticAtlasReviewRequested = false;
        QObject::connect(&landscapeTools, &CLandscapeTools::atlasProfileReviewRequested,
                         [&semanticAtlasReviewRequested]() {
            semanticAtlasReviewRequested = true;
        });
        QPushButton* materialAtlasButton =
            landscapeTools.findChild<QPushButton*>("materialAtlasButton");
        const bool materialAtlasButtonAvailable = materialAtlasButton != nullptr;
        if(materialAtlasButton)
            materialAtlasButton->click();
        const bool materialAtlasActionWorks = materialAtlasButtonAvailable
            && semanticAtlasReviewRequested;
        bool modernDraftRequested = false;
        QObject::connect(&landscapeTools,
                         &CLandscapeTools::saveModernDraftRequested,
                         [&modernDraftRequested]() {
            modernDraftRequested = true;
        });
        QPushButton* modernDraftButton =
            landscapeTools.findChild<QPushButton*>("saveModernDraftButton");
        const bool modernDraftActionWorks = modernDraftButton
            && modernDraftButton->text().contains(QStringLiteral("draft"),
                                                   Qt::CaseInsensitive);
        if(modernDraftButton)
            modernDraftButton->click();
        const QList<QPushButton*> landscapePushButtons =
            landscapeTools.findChildren<QPushButton*>();
        const bool redundantFinalizeActionRemoved = std::none_of(
            landscapePushButtons.cbegin(), landscapePushButtons.cend(),
            [](const QPushButton* button) {
                return button->text().contains(
                    QStringLiteral("Finalize map-specific"));
            });
        QComboBox* paintIssueCategory =
            landscapeTools.findChild<QComboBox*>("paintIssueCategory");
        QToolButton* markPaintIssueButton =
            landscapeTools.findChild<QToolButton*>("markPaintIssueButton");
        const bool paintIssueMarkerAvailable = paintIssueCategory
            && paintIssueCategory->count() == 3
            && paintIssueCategory->itemData(0).toString() == "no-blend"
            && paintIssueCategory->itemText(0) == "No / wrong blend"
            && paintIssueCategory->itemData(1).toString() == "tile-mismatch"
            && paintIssueCategory->itemData(2).toString() == "wrong-rotation"
            && markPaintIssueButton && markPaintIssueButton->isCheckable();
        if(markPaintIssueButton) markPaintIssueButton->setChecked(true);
        bool materialSelectedWhileMarking = false;
        const bool paintIssueMarkerActivates = paintIssueMarkerAvailable
            && landscapeTools.paintIssueMarkingEnabled()
            && landscapeTools.paintIssueCategory() == "no-blend"
            && landscapeTools.paintTerrainType(&materialSelectedWhileMarking) == 0
            && !materialSelectedWhileMarking
            && landscapeTools.editingEnabled()
            && markPaintIssueButton->text().contains("MARKING");
        bool materialSelectedAfterMarker = false;
        int markerExitStateSignals = 0;
        int markerExitMaterialSignals = 0;
        QObject::connect(&landscapeTools, &CLandscapeTools::editingStateChanged,
                         [&markerExitStateSignals]() { ++markerExitStateSignals; });
        QObject::connect(&landscapeTools, &CLandscapeTools::paintTerrainTypeChanged,
                         [&markerExitMaterialSignals](qint32 material) {
            if(material == 9)
                ++markerExitMaterialSignals;
        });
        const bool paintIssueMarkerMutuallyExclusive =
            landscapeTools.selectPaintTerrainType(9)
            && !landscapeTools.paintIssueMarkingEnabled()
            && landscapeTools.paintTerrainType(&materialSelectedAfterMarker) == 9
            && materialSelectedAfterMarker
            && landscapeTools.editingEnabled()
            && markerExitStateSignals > 0
            && markerExitMaterialSignals == 1;
        const bool paintIssueTemporarySelectPreservesEditing =
            !landscapeOperationDisablesEditing(
                EButtonOpSelect, true, true)
            && landscapeOperationDisablesEditing(
                EButtonOpSelect, true, false)
             && !landscapeOperationDisablesEditing(
                 EButtonOpTilebrush, true, false);

        QWidget primaryWindow;
        QLabel existingWindowChild(&primaryWindow);
        bool settingsTabsUsable = false;
        bool settingsShortcutsPersist = false;
        bool autosaveSettingsPersist = false;
        bool autosaveLiveReconfigure = false;
        bool cameraSpeedSettingsPersist = false;
        bool telephotoControlsContract = false;
        bool telephotoSettingsPersist = false;
        bool telephotoEnabledSessionOnly = false;
        bool uiScaleSettingsPersist = false;
        bool uiScaleLiveApply = false;
        bool uiScaleFontApplied = false;
        bool uiScaleExistingWindowApplied = false;
        bool settingsApplyKeepsOpen = false;
        bool settingsSaveCloseWorks = false;
        bool settingsButtonLayoutWorks = false;
        bool settingsApplySignal = false;
        bool languageSettingsPersist = false;
        bool languageRestartPromptShown = false;
        bool russianCatalogLoads = false;
        bool qtStandardDialogsTranslate = false;
        bool russianFallbackWorks = false;
        bool validationTemplatesTranslate = false;
        bool russianLayoutsFit = false;
        bool renderOptionsRegistered = false;
        bool terrainShadowToggleApplies = false;
        {
            CCameraTelephotoControls controls;
            QCheckBox* enabled = controls.findChild<QCheckBox*>(
                "telephotoCameraEnabled");
            QDoubleSpinBox* multiplier = controls.findChild<QDoubleSpinBox*>(
                "telephotoCameraMultiplier");
            QComboBox* direction = controls.findChild<QComboBox*>(
                "telephotoCameraDirection");
            bool enabledSignal = false;
            bool directionSignal = false;
            const bool defaultDirection = controls.telephotoDirection()
                == int(ei::ETelephotoDirection::SouthwestToNortheast);
            QObject::connect(&controls,
                &CCameraTelephotoControls::telephotoEnabledChanged,
                [&enabledSignal](bool value) { enabledSignal = value; });
            QObject::connect(&controls,
                &CCameraTelephotoControls::telephotoDirectionChanged,
                [&directionSignal](int value)
                {
                    directionSignal = value
                        == int(ei::ETelephotoDirection::NortheastToSouthwest);
                });
            if(enabled)
                enabled->setChecked(true);
            if(direction)
                direction->setCurrentIndex(2);
            telephotoControlsContract = enabled && multiplier && direction
                && enabledSignal && controls.telephotoEnabled()
                && qAbs(multiplier->minimum() - 1.0) < 0.001
                && qAbs(multiplier->maximum() - 4.0) < 0.001
                && qAbs(multiplier->value() - 2.0) < 0.001
                && direction->count() == 4 && defaultDirection
                && directionSignal
                && controls.telephotoDirection()
                    == int(ei::ETelephotoDirection::NortheastToSouthwest);
            CCameraTelephotoControls nextSessionControls;
            telephotoEnabledSessionOnly = !nextSessionControls.telephotoEnabled()
                && nextSessionControls.telephotoDirection()
                    == int(ei::kTelephotoDirectionDefault);
        }
        {
            const QString previousDirectory = QDir::currentPath();
            QTemporaryDir settingsDirectory;
            if(settingsDirectory.isValid())
                QDir::setCurrent(settingsDirectory.path());
            {
                CSettings settingsWindow(&primaryWindow);
                settingsWindow.setAttribute(Qt::WA_DontShowOnScreen);
                settingsWindow.show();
                QObject::connect(&settingsWindow, &CSettings::settingsApplied,
                                 [&settingsApplySignal]() { settingsApplySignal = true; });
                QObject::connect(&settingsWindow, &CSettings::autosaveSettingsApplied,
                    [&autosaveLiveReconfigure](bool enabled, int intervalMinutes)
                    {
                        autosaveLiveReconfigure = enabled && intervalMinutes == 1;
                    });
                QObject::connect(&settingsWindow, &CSettings::uiScaleApplied,
                    [&uiScaleLiveApply](int percent)
                    {
                        uiScaleLiveApply = percent == 150;
                    });
                QTabWidget* settingsTabs = settingsWindow.findChild<QTabWidget*>("tabWidget");
                if(settingsTabs && settingsTabs->count() > 1)
                {
                    settingsTabs->setCurrentIndex(1);
                    settingsTabsUsable = settingsWindow.windowModality() == Qt::WindowModal
                            && settingsTabs->isEnabled() && settingsTabs->currentIndex() == 1;
                }
                QKeySequenceEdit* historyShortcut = settingsWindow.findChild<QKeySequenceEdit*>("shortcutHistory");
                QKeySequenceEdit* duplicateShortcut = settingsWindow.findChild<QKeySequenceEdit*>("shortcutDuplicateObjects");
                QKeySequenceEdit* stopTestShortcut = settingsWindow.findChild<QKeySequenceEdit*>("shortcutStopMapTest");
                QCheckBox* autosaveEnabled = settingsWindow.findChild<QCheckBox*>("autosaveEnabled");
                QLineEdit* autosaveInterval = settingsWindow.findChild<QLineEdit*>("autosaveIntervalMinutes");
                QComboBox* cameraWheelGesture = settingsWindow.findChild<QComboBox*>(
                    "cameraSpeedWheelGesture");
                QComboBox* language = settingsWindow.findChild<QComboBox*>("language");
                QSlider* uiScale = settingsWindow.findChild<QSlider*>("uiScalePercent");
                QLabel* uiScaleValue = settingsWindow.findChild<QLabel*>(
                    "uiScalePercentValue");
                QPushButton* saveCloseButton = settingsWindow.findChild<QPushButton*>(
                    "buttonSaveClose");
                QPushButton* applyButton = settingsWindow.findChild<QPushButton*>(
                    "buttonApply");
                COptBool* animateUnits = dynamic_cast<COptBool*>(
                    settingsWindow.opt(eOptSetRender, "animateUnits"));
                COptBool* performanceCounters = dynamic_cast<COptBool*>(
                    settingsWindow.opt(eOptSetRender, "renderPerformanceCounters"));
                COptBool* renderVsync = dynamic_cast<COptBool*>(
                    settingsWindow.opt(eOptSetRender, "renderVSync"));
                COptInt* renderFpsCap = dynamic_cast<COptInt*>(
                    settingsWindow.opt(eOptSetRender, "renderFpsCap"));
                COptInt* renderLightingMode = dynamic_cast<COptInt*>(
                    settingsWindow.opt(eOptSetRender, "renderLightingMode"));
                COptBool* renderMipmapping = dynamic_cast<COptBool*>(
                    settingsWindow.opt(eOptSetRender, "renderTextureMipmapping"));
                COptBool* renderTerrainShadows = dynamic_cast<COptBool*>(
                    settingsWindow.opt(eOptSetRender, "renderTerrainShadows"));
                COptDouble* telephotoMultiplierOption = dynamic_cast<COptDouble*>(
                    settingsWindow.opt(eOptSetGeneral, "telephotoCameraMultiplier"));
                COptInt* telephotoDirectionOption = dynamic_cast<COptInt*>(
                    settingsWindow.opt(eOptSetGeneral, "telephotoCameraDirection"));
                QCheckBox* renderTerrainShadowsCheck =
                    settingsWindow.findChild<QCheckBox*>("renderTerrainShadows");
                renderOptionsRegistered = animateUnits && animateUnits->value()
                        && performanceCounters && !performanceCounters->value()
                        && renderVsync && renderVsync->value()
                        && renderFpsCap && renderFpsCap->value() == 0
                        && renderLightingMode && renderLightingMode->value() == 2
                        && renderMipmapping && renderMipmapping->value()
                        && renderTerrainShadows && renderTerrainShadows->value()
                        && settingsWindow.findChild<QCheckBox*>("renderVSync")
                        && settingsWindow.findChild<QCheckBox*>("renderTextureMipmapping")
                        && renderTerrainShadowsCheck
                        && settingsWindow.findChild<QLineEdit*>("renderFpsCap");
                if(telephotoMultiplierOption)
                    telephotoMultiplierOption->setValue(3.4);
                if(telephotoDirectionOption)
                    telephotoDirectionOption->setValue(
                        int(ei::ETelephotoDirection::NorthwestToSoutheast));
                if(historyShortcut && duplicateShortcut && stopTestShortcut)
                {
                    historyShortcut->setKeySequence(QKeySequence("Alt+L"));
                    if(autosaveEnabled)
                        autosaveEnabled->setChecked(true);
                    if(autosaveInterval)
                        autosaveInterval->setText("1");
                    if(cameraWheelGesture)
                        cameraWheelGesture->setCurrentIndex(2);
                    if(language)
                        language->setCurrentIndex(language->findData("ru"));
                    if(uiScale)
                        uiScale->setValue(150);
                    if(renderTerrainShadowsCheck)
                        renderTerrainShadowsCheck->setChecked(false);
                    QTimer restartPromptDismissTimer;
                    QObject::connect(&restartPromptDismissTimer, &QTimer::timeout,
                        [&languageRestartPromptShown]()
                    {
                        for(QWidget* widget : QApplication::topLevelWidgets())
                        {
                            QMessageBox* prompt = qobject_cast<QMessageBox*>(widget);
                            if(!prompt)
                                continue;
                            const QString title = QCoreApplication::translate(
                                "CSettings", "Restart required");
                            const QString body = QCoreApplication::translate(
                                "CSettings",
                                "Restart EI Mapper to apply the new interface language.");
                            if(prompt->windowTitle() == title && prompt->text() == body)
                            {
                                languageRestartPromptShown = true;
                            }
                            prompt->accept();
                        }
                    });
                    restartPromptDismissTimer.start(10);
                    QMetaObject::invokeMethod(&settingsWindow, "on_buttonApply_clicked", Qt::DirectConnection);
                    restartPromptDismissTimer.stop();
                    settingsApplyKeepsOpen = settingsWindow.isVisible();
                    terrainShadowToggleApplies = renderTerrainShadows
                        && !renderTerrainShadows->value();
                    COptString* saved = dynamic_cast<COptString*>(
                        settingsWindow.opt(eOptSetKeyBinding, "shortcutHistory"));
                    settingsShortcutsPersist = saved && saved->value() == "Alt+L"
                            && duplicateShortcut->keySequence() == QKeySequence("Ctrl+D")
                            && stopTestShortcut->keySequence() == QKeySequence("Shift+Esc");
                    COptBool* savedAutosaveEnabled = dynamic_cast<COptBool*>(
                        settingsWindow.opt(eOptSetGeneral, "autosaveEnabled"));
                    COptInt* savedAutosaveInterval = dynamic_cast<COptInt*>(
                        settingsWindow.opt(eOptSetGeneral, "autosaveIntervalMinutes"));
                    autosaveSettingsPersist = savedAutosaveEnabled && savedAutosaveEnabled->value()
                            && savedAutosaveInterval && savedAutosaveInterval->value() == 1;
                    COptString* savedCameraGesture = dynamic_cast<COptString*>(
                        settingsWindow.opt(eOptSetKeyBinding, "cameraSpeedWheelGesture"));
                    COptString* savedLanguage = dynamic_cast<COptString*>(
                        settingsWindow.opt(eOptSetGeneral, "language"));
                    languageSettingsPersist = language && language->count() == 2
                        && language->itemData(0).toString() == "en"
                        && language->itemData(1).toString() == "ru"
                        && savedLanguage && savedLanguage->value() == "ru"
                        && ei::persistedApplicationLanguage() == "ru";
                    COptDouble* savedCameraMultiplier = dynamic_cast<COptDouble*>(
                        settingsWindow.opt(eOptSetGeneral, "cameraSpeedMultiplier"));
                    cameraSpeedSettingsPersist = cameraWheelGesture
                            && savedCameraGesture && savedCameraGesture->value() == "Shift+Wheel"
                            && savedCameraMultiplier
                            && qAbs(savedCameraMultiplier->value() - 1.0) < 0.001;
                    COptInt* savedUiScale = dynamic_cast<COptInt*>(
                        settingsWindow.opt(eOptSetGeneral, "uiScalePercent"));
                    uiScaleSettingsPersist = uiScale && uiScale->minimum() == 75
                            && uiScale->maximum() == 200
                            && uiScale->singleStep() == 5
                            && uiScaleValue && uiScaleValue->text() == "150%"
                            && savedUiScale && savedUiScale->value() == 150
                            && ei::persistedUiScalePercent() == 150;
                    QMetaObject::invokeMethod(&settingsWindow,
                                              "on_buttonSaveClose_clicked",
                                              Qt::DirectConnection);
                    settingsSaveCloseWorks = saveCloseButton && !settingsWindow.isVisible();
                    settingsButtonLayoutWorks = applyButton && saveCloseButton
                        && saveCloseButton->text() == "Save && Close"
                        && applyButton->geometry().left() < saveCloseButton->geometry().left();
                }
            }
            {
                CSettings reopenedSettings(&primaryWindow);
                const COptDouble* reopenedTelephoto = dynamic_cast<const COptDouble*>(
                    reopenedSettings.opt(eOptSetGeneral,
                                         "telephotoCameraMultiplier"));
                const COptInt* reopenedTelephotoDirection =
                    dynamic_cast<const COptInt*>(reopenedSettings.opt(
                        eOptSetGeneral, "telephotoCameraDirection"));
                telephotoSettingsPersist = reopenedTelephoto
                    && qAbs(reopenedTelephoto->value() - 3.4) < 0.001
                    && reopenedTelephotoDirection
                    && reopenedTelephotoDirection->value()
                        == int(ei::ETelephotoDirection::NorthwestToSoutheast)
                    && !reopenedSettings.opt("telephotoCameraEnabled");
            }
            QDir::setCurrent(previousDirectory);
        }
        russianCatalogLoads = ei::installApplicationTranslator(app, QStringLiteral("ru"))
            && QCoreApplication::translate("MainWindow", "Settings") == QString::fromUtf8("Настройки");
        {
            QMessageBox standardButtonProbe;
            standardButtonProbe.setStandardButtons(QMessageBox::Yes | QMessageBox::No
                                                   | QMessageBox::Cancel);
            qtStandardDialogsTranslate = standardButtonProbe.button(QMessageBox::Yes)
                    && standardButtonProbe.button(QMessageBox::Yes)->text().contains(
                        QRegularExpression(QString::fromUtf8("[А-Яа-яЁё]")))
                && standardButtonProbe.button(QMessageBox::No)
                    && standardButtonProbe.button(QMessageBox::No)->text().contains(
                        QRegularExpression(QString::fromUtf8("[А-Яа-яЁё]")))
                && standardButtonProbe.button(QMessageBox::Cancel)
                    && standardButtonProbe.button(QMessageBox::Cancel)->text().contains(
                        QRegularExpression(QString::fromUtf8("[А-Яа-яЁё]")));
        }
        const QByteArray fallbackProbe = QByteArrayLiteral("Uncatalogued ")
            + QByteArrayLiteral("fallback probe");
        russianFallbackWorks = QCoreApplication::translate(
            "MainWindow", fallbackProbe.constData()) == QString::fromLatin1(fallbackProbe);
        validationTemplatesTranslate = localizedMobFamilyUiText(
            "NID 7 is used by 2 objects in map.mob").contains(QString::fromUtf8("объект"));
        {
            const QString previousDirectory = QDir::currentPath();
            QTemporaryDir russianLayoutDirectory;
            if(russianLayoutDirectory.isValid())
                QDir::setCurrent(russianLayoutDirectory.path());
            {
            CSettings russianSettings(&primaryWindow);
            russianSettings.setAttribute(Qt::WA_DontShowOnScreen);
            russianSettings.show();
            CResourceSetupDialog russianSetup;
            russianSetup.setAttribute(Qt::WA_DontShowOnScreen);
            russianSetup.show();
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QComboBox* russianLanguage = russianSettings.findChild<QComboBox*>("language");
            QPushButton* russianApply = russianSettings.findChild<QPushButton*>("buttonApply");
            QPushButton* russianSaveClose = russianSettings.findChild<QPushButton*>("buttonSaveClose");
            QTableWidget russianProperties;
            CTableManager russianPropertyManager(&russianProperties);
            QList<QSharedPointer<IPropertyBase>> russianPropertyValues;
            russianPropertyValues << QSharedPointer<IPropertyBase>(
                new propStr(eObjParam_TEMPLATE, "model_name"));
            russianPropertyManager.setNewData(russianPropertyValues);
            russianProperties.resizeColumnToContents(0);
            const QTableWidgetItem* russianPropertyLabel = russianProperties.item(0, 0);
            russianLayoutsFit = russianLanguage && russianApply && russianSaveClose
                && russianLanguage->width() >= QFontMetrics(russianLanguage->font())
                    .horizontalAdvance(russianLanguage->currentText()) + 28
                && russianApply->width() >= russianApply->sizeHint().width()
                && russianSaveClose->width() >= russianSaveClose->sizeHint().width()
                && russianSettings.sizeHint().width() <= 1280
                && russianSettings.sizeHint().height() <= 900
                && russianSetup.sizeHint().width() <= 1280
                && russianSetup.sizeHint().height() <= 900
                && russianPropertyLabel
                && russianPropertyLabel->text() != "Model"
                && russianPropertyLabel->text().contains(
                    QRegularExpression(QString::fromUtf8("[А-Яа-яЁё]")))
                && russianProperties.columnWidth(0) <= 260;
            QTextStream(stdout) << "russianLayout language="
                << (russianLanguage ? russianLanguage->width() : -1) << "/"
                << (russianLanguage ? QFontMetrics(russianLanguage->font())
                    .horizontalAdvance(russianLanguage->currentText()) + 28 : -1)
                << " apply=" << (russianApply ? russianApply->width() : -1) << "/"
                << (russianApply ? russianApply->sizeHint().width() : -1)
                << " saveClose=" << (russianSaveClose ? russianSaveClose->width() : -1) << "/"
                << (russianSaveClose ? russianSaveClose->sizeHint().width() : -1)
                << " settings=" << russianSettings.sizeHint().width() << "x"
                << russianSettings.sizeHint().height()
                << " setup=" << russianSetup.sizeHint().width() << "x"
                << russianSetup.sizeHint().height()
                << " property=" << (russianPropertyLabel ? russianPropertyLabel->text() : QString())
                << "/" << russianProperties.columnWidth(0) << "\n";
            russianSettings.close();
            russianSetup.close();
            }
            QDir::setCurrent(previousDirectory);
        }
        const QFont unscaledFont = QApplication::font();
        ei::applyUiScalePercent(150);
        const QFont scaledFont = QApplication::font();
        const QFont scaledExistingFont = primaryWindow.font();
        const QFont scaledExistingChildFont = existingWindowChild.font();
        uiScaleFontApplied = qApp->property("enduranceUiScalePercent").toInt() == 150
            && ((unscaledFont.pointSizeF() > 0.0
                 && qAbs(scaledFont.pointSizeF()
                         - unscaledFont.pointSizeF() * 1.5) < 0.1)
                || (unscaledFont.pixelSize() > 0
                    && scaledFont.pixelSize() > unscaledFont.pixelSize()));
        uiScaleExistingWindowApplied =
            (scaledFont.pointSizeF() > 0.0
             && qAbs(scaledExistingFont.pointSizeF() - scaledFont.pointSizeF()) < 0.1)
            || (scaledFont.pixelSize() > 0
                && scaledExistingFont.pixelSize() == scaledFont.pixelSize());
        uiScaleExistingWindowApplied = uiScaleExistingWindowApplied
            && ((scaledFont.pointSizeF() > 0.0
                 && qAbs(scaledExistingChildFont.pointSizeF()
                         - scaledFont.pointSizeF()) < 0.1)
                || (scaledFont.pixelSize() > 0
                    && scaledExistingChildFont.pixelSize() == scaledFont.pixelSize()));
        ei::applyUiScalePercent(100);
        QWidget ownedWindow(&primaryWindow, Qt::Window);
        QWidget orphanWindow(nullptr, Qt::Window);
        QPointer<QWidget> deletingWindow = new QWidget(nullptr, Qt::Window);
        ownedWindow.setAttribute(Qt::WA_DontShowOnScreen);
        orphanWindow.setAttribute(Qt::WA_DontShowOnScreen);
        deletingWindow->setAttribute(Qt::WA_DontShowOnScreen);
        deletingWindow->setAttribute(Qt::WA_DeleteOnClose);
        ownedWindow.show();
        orphanWindow.show();
        deletingWindow->show();
        ei::closeSecondaryWindows(&primaryWindow);
        const bool secondaryWindowsClosed = !ownedWindow.isVisible() && !orphanWindow.isVisible();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const bool deletingSecondaryWindowClosedSafely = deletingWindow.isNull();

        // Paste creation and its automatic placement transform must be one
        // top-level history item. This mirrors CView::operationApply().
        QUndoStack pasteHistory;
        int pastedObjects = 0;
        int placementChanges = 0;
        pasteHistory.beginMacro("Paste 1 object");
        pasteHistory.push(new CCounterUndoCommand(&pastedObjects, 1));
        pasteHistory.push(new CCounterUndoCommand(&placementChanges, 1));
        pasteHistory.endMacro();
        const bool pasteSingleAction = pasteHistory.count() == 1
                && pastedObjects == 1 && placementChanges == 1;
        pasteHistory.undo();
        const bool pasteUndoAtomic = pasteHistory.index() == 0
                && pastedObjects == 0 && placementChanges == 0;
        pasteHistory.redo();
        const bool pasteRedoAtomic = pasteHistory.index() == 1
                && pastedObjects == 1 && placementChanges == 1;

        QUndoStack logicPointHistory;
        int logicPoints = 0;
        int logicPlacementChanges = 0;
        logicPointHistory.beginMacro("Create and place logic point");
        logicPointHistory.push(new CCounterUndoCommand(&logicPoints, 1));
        logicPointHistory.push(new CCounterUndoCommand(&logicPlacementChanges, 1));
        logicPointHistory.endMacro();
        logicPointHistory.undo();
        const bool logicPointUndoAtomic = logicPointHistory.index() == 0
                && logicPoints == 0 && logicPlacementChanges == 0;
        logicPointHistory.redo();
        const bool logicPointRedoAtomic = logicPointHistory.index() == 1
                && logicPoints == 1 && logicPlacementChanges == 1;

        CSessionDataManager* sessionManager = CSessionDataManager::getInstance();
        sessionManager->reset();
        sessionManager->addWorkspaceIdentity("collection", "C:/maps/test.collection",
                                             "Test collection", "zone7");
        QString sessionKind;
        QString sessionIdentifier;
        QString sessionDisplayName;
        QString sessionMapZoneId;
        const bool sessionIdentityRoundTrip = sessionManager->getWorkspaceIdentity(
            sessionKind, sessionIdentifier, sessionDisplayName, sessionMapZoneId)
            && sessionKind == "collection"
            && sessionIdentifier == "C:/maps/test.collection"
            && sessionDisplayName == "Test collection"
            && sessionMapZoneId == "zone7";
        sessionManager->reset();

        CMousePressProbe viewportProbe;
        viewportProbe.resize(240, 160);
        CViewportWidget viewportWidget(&viewportProbe);
        viewportWidget.setGeometry(20, 20, 180, 100);
        QLabel viewportLabel(QStringLiteral("Viewport control"), &viewportWidget);
        viewportLabel.setGeometry(10, 10, 120, 24);

        QMouseEvent viewportBackgroundPress(QEvent::MouseButtonPress, QPointF(150, 70),
                                            Qt::LeftButton, Qt::LeftButton,
                                            Qt::NoModifier);
        viewportBackgroundPress.setAccepted(false);
        QApplication::sendEvent(&viewportWidget, &viewportBackgroundPress);
        const bool viewportBackgroundConsumesClick = viewportBackgroundPress.isAccepted()
                && viewportProbe.presses == 0;

        QMouseEvent viewportChildPress(QEvent::MouseButtonPress, QPointF(5, 5),
                                       Qt::LeftButton, Qt::LeftButton,
                                       Qt::NoModifier);
        viewportChildPress.setAccepted(false);
        QApplication::sendEvent(&viewportLabel, &viewportChildPress);
        const bool viewportChildConsumesClick = viewportChildPress.isAccepted()
                && viewportProbe.presses == 0;

        viewportWidget.setViewportSurfaceColor(QColor(24, 36, 48));
        viewportWidget.setObjectName(QStringLiteral("viewportSurfaceRegression"));
        viewportWidget.setStyleSheet(QStringLiteral(
            "QFrame#viewportSurfaceRegression { background: rgb(60, 70, 80); "
            "border: 1px solid #777; }"));
        QImage viewportSurface(viewportWidget.size(), QImage::Format_ARGB32);
        viewportSurface.fill(Qt::transparent);
        viewportWidget.render(&viewportSurface);
        const bool viewportSurfaceFullyOwned = viewportWidget.testAttribute(
                    Qt::WA_OpaquePaintEvent)
                && viewportWidget.testAttribute(Qt::WA_StyledBackground)
                && viewportSurface.pixelColor(1, 1).alpha() == 255
                && viewportSurface.pixelColor(viewportSurface.width() - 2,
                                              viewportSurface.height() - 2).alpha() == 255;

        CMessageLogPanel messageLog;
        CLogger::getInstance()->setLogLevel(eLogDebug);
        ei::log(eLogInfo, "ui-check searchable information");
        ei::log(eLogWarning, "ui-check searchable warning");
        ei::log(eLogDebug, "ui-check severity debug");
        ei::log(eLogFatal, "ui-check severity error");
        const int messagesBeforeTexturelessDraw =
                CLogger::getInstance()->sessionMessages().size();
        CObjectBase texturelessObject;
        texturelessObject.draw(false, nullptr);
        const bool texturelessDrawDoesNotSpamLog =
                CLogger::getInstance()->sessionMessages().size()
                == messagesBeforeTexturelessDraw;
        QEventLoop messageEvents;
        QTimer::singleShot(60, &messageEvents, &QEventLoop::quit);
        messageEvents.exec();
        messageLog.setSearchText(QStringLiteral("searchable"));
        const bool messageLogSearchWorks = messageLog.visibleMessageCount() == 2;
        messageLog.setTypeVisible(eLogWarning, false);
        const bool messageLogTypeFilterWorks = messageLog.visibleMessageCount() == 1;
        messageLog.setTypeVisible(eLogWarning, true);
        QTableWidget* messageTable = messageLog.findChild<QTableWidget*>(
                    QStringLiteral("messageLogTable"));
        bool messageLogColumnsResizable = messageTable != nullptr;
        bool messageLogSeverityColorsWork = messageTable != nullptr;
        if(messageTable)
        {
            for(int column = 0; column < messageTable->columnCount(); ++column)
                messageLogColumnsResizable = messageLogColumnsResizable
                        && messageTable->horizontalHeader()->sectionResizeMode(column)
                        == QHeaderView::Interactive;
            messageTable->clearSelection();
            for(int row = 0; row < messageTable->rowCount(); ++row)
            {
                QTableWidgetItem* body = messageTable->item(row, 2);
                QTableWidgetItem* type = messageTable->item(row, 1);
                if(body && type && body->text() == QStringLiteral("ui-check searchable information"))
                    messageLogSeverityColorsWork = messageLogSeverityColorsWork
                            && type->foreground().style() == Qt::NoBrush;
                else if(body && type && body->text() == QStringLiteral("ui-check searchable warning"))
                    messageLogSeverityColorsWork = messageLogSeverityColorsWork
                            && type->foreground().color() == QColor(231, 195, 79);
                else if(body && type && body->text() == QStringLiteral("ui-check severity debug"))
                    messageLogSeverityColorsWork = messageLogSeverityColorsWork
                            && type->foreground().color() == QColor(143, 150, 158);
                else if(body && type && body->text() == QStringLiteral("ui-check severity error"))
                    messageLogSeverityColorsWork = messageLogSeverityColorsWork
                            && type->foreground().color() == QColor(239, 91, 91);
                if(body && body->text().startsWith(QStringLiteral("ui-check searchable")))
                    messageTable->selectionModel()->select(
                        messageTable->model()->index(row, 0),
                        QItemSelectionModel::Select | QItemSelectionModel::Rows);
            }
        }
        QApplication::clipboard()->clear();
        QMetaObject::invokeMethod(&messageLog, "copySelectedMessages",
                                  Qt::DirectConnection);
        const QString copiedMessages = QApplication::clipboard()->text();
        const bool messageLogMultiCopyWorks = copiedMessages.contains("{INFO}")
                && copiedMessages.contains("{WARNING}")
                && copiedMessages.contains("ui-check searchable information")
                && copiedMessages.contains("ui-check searchable warning")
                && copiedMessages.contains(QRegularExpression(
                    QStringLiteral("\\[\\d{2}\\.\\d{2}\\.\\d{4} ")));
        messageLog.setSearchText(QString());
        messageLog.setAttribute(Qt::WA_DontShowOnScreen);
        messageLog.resize(800, 160);
        messageLog.show();
        QApplication::processEvents();
        QScrollBar* messageScroll = messageTable
                ? messageTable->verticalScrollBar() : nullptr;
        if(messageScroll) messageScroll->setValue(messageScroll->maximum());
        quint64 testSequence = CLogger::getInstance()->sessionMessages().isEmpty()
                ? 1000000 : CLogger::getInstance()->sessionMessages().last().sequence + 1000;
        const auto appendLogProbe = [&messageLog, &testSequence](const QString& text) {
            SLogMessage message;
            message.sequence = testSequence++;
            message.type = eLogInfo;
            message.timestamp = QDateTime::currentDateTime();
            message.message = text;
            message.formatted = text;
            QMetaObject::invokeMethod(&messageLog, "queueMessage", Qt::DirectConnection,
                                      Q_ARG(SLogMessage, message));
            QMetaObject::invokeMethod(&messageLog, "flushPending", Qt::DirectConnection);
        };
        appendLogProbe(QStringLiteral("ui-check follow newest"));
        const bool messageLogStartsAtBottom = messageScroll
                && messageScroll->value() == messageScroll->maximum();
        const bool messageLogCanScroll = messageScroll
                && messageScroll->maximum() > messageScroll->minimum();
        if(messageLogCanScroll) messageScroll->setValue(messageScroll->minimum());
        appendLogProbe(QStringLiteral("ui-check preserve manual scroll"));
        const bool messageLogManualScrollPreserved = !messageLogCanScroll
                || messageScroll->value() < messageScroll->maximum();
        if(messageScroll) messageScroll->setValue(messageScroll->maximum());
        appendLogProbe(QStringLiteral("ui-check resume follow newest"));
        const bool messageLogFollowResumesAtBottom = messageScroll
                && messageScroll->value() == messageScroll->maximum();
        messageLog.hide();

        const auto dockDiagnosticCount = []() {
            int count = 0;
            const QVector<SLogMessage> messages = CLogger::getInstance()->sessionMessages();
            for(const SLogMessage& message : messages)
                if(message.message.contains(QStringLiteral("DOCK_WINDOW"))) ++count;
            return count;
        };
        const int dockDiagnosticsBefore = dockDiagnosticCount();
        QMainWindow dockWindow;
        dockWindow.setDockOptions(dockWindow.dockOptions()
            | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks
            | QMainWindow::GroupedDragging);
        dockWindow.setCentralWidget(new QWidget(&dockWindow));
        QDockWidget dockLabelProbe(QStringLiteral("Message Log"), &dockWindow);
        QDockWidget validationDockProbe(QStringLiteral("Validation"), &dockWindow);
        dockLabelProbe.setObjectName(QStringLiteral("messageLogDockProbe"));
        validationDockProbe.setObjectName(QStringLiteral("validationDockProbe"));
        CPaintProbe* floatingPaintProbe = new CPaintProbe;
        dockLabelProbe.setWidget(floatingPaintProbe);
        validationDockProbe.setWidget(new QWidget(&validationDockProbe));
        dockWindow.addDockWidget(Qt::BottomDockWidgetArea, &dockLabelProbe);
        dockWindow.addDockWidget(Qt::BottomDockWidgetArea, &validationDockProbe);
        dockWindow.tabifyDockWidget(&dockLabelProbe, &validationDockProbe);
        ei::enableNativeFloatingDockControls(&dockLabelProbe);
        const bool floatingDockGroupingEnabled = dockWindow.dockOptions().testFlag(
                    QMainWindow::GroupedDragging)
                && dockWindow.tabifiedDockWidgets(&dockLabelProbe).contains(
                    &validationDockProbe);
        CDockHiddenTab hiddenTab(&dockWindow, &dockLabelProbe,
                                 QStringLiteral("Message Log"),
                                 QStringLiteral("dockHiddenTabProbe"));
        dockWindow.show();
        QAction* validationViewAction = validationDockProbe.toggleViewAction();
        validationDockProbe.hide();
        QApplication::processEvents();
        const bool validationViewStartsUnchecked = !validationViewAction->isChecked();
        validationViewAction->trigger();
        QApplication::processEvents();
        const bool validationViewShowsAndChecks = validationDockProbe.isVisible()
                && validationViewAction->isChecked();
        validationViewAction->trigger();
        QApplication::processEvents();
        const bool validationViewHidesAndUnchecks = !validationDockProbe.isVisible()
                && !validationViewAction->isChecked();
        validationViewAction->trigger();
        QApplication::processEvents();
        const bool validationViewReopensBesideMessageLog = validationDockProbe.isVisible()
                && validationViewAction->isChecked() && dockLabelProbe.isVisible();
        dockLabelProbe.hide();
        QEventLoop hiddenDockEvents;
        QTimer::singleShot(25, &hiddenDockEvents, &QEventLoop::quit);
        hiddenDockEvents.exec();
        const bool dockedHiddenLabelAppears = hiddenTab.tabBar()->isVisible();
        dockLabelProbe.setFloating(true);
        QApplication::processEvents();
#ifdef Q_OS_WIN
        const HWND floatingDockInitialHwnd = reinterpret_cast<HWND>(
                    dockLabelProbe.winId());
        const LONG_PTR floatingDockInitialStyle = GetWindowLongPtrW(
                    floatingDockInitialHwnd, GWL_STYLE);
        const bool floatingDockWindowControls =
                (floatingDockInitialStyle & WS_MINIMIZEBOX) != 0
                && (floatingDockInitialStyle & WS_MAXIMIZEBOX) != 0
                && (floatingDockInitialStyle & WS_SYSMENU) != 0;
        const bool floatingDockIsNormalWindow =
                (floatingDockInitialStyle & WS_POPUP) == 0
                && (floatingDockInitialStyle & WS_CAPTION) != 0
                && (floatingDockInitialStyle & WS_THICKFRAME) != 0;
#else
        const Qt::WindowFlags floatingFlags = dockLabelProbe.windowFlags();
        const bool floatingDockWindowControls = floatingFlags.testFlag(
                    Qt::WindowMinimizeButtonHint)
                && floatingFlags.testFlag(Qt::WindowMaximizeButtonHint)
                && floatingFlags.testFlag(Qt::WindowCloseButtonHint);
        const bool floatingDockIsNormalWindow =
                dockLabelProbe.windowType() == Qt::Window;
#endif
        dockLabelProbe.showMaximized();
        QApplication::processEvents();
#ifdef Q_OS_WIN
        const HWND floatingDockHwnd = reinterpret_cast<HWND>(dockLabelProbe.winId());
        const LONG_PTR floatingDockExStyle = GetWindowLongPtrW(
                    floatingDockHwnd, GWL_EXSTYLE);
        const bool floatingDockTaskbarIdentity =
                GetWindowLongPtrW(floatingDockHwnd, GWLP_HWNDPARENT) == 0
                && (floatingDockExStyle & WS_EX_APPWINDOW) != 0
                && (floatingDockExStyle & WS_EX_TOOLWINDOW) == 0;
#else
        const bool floatingDockTaskbarIdentity = true;
#endif
        ei::saveFloatingDockWindowState(&dockLabelProbe);
        dockLabelProbe.showNormal();
        ei::restoreFloatingDockWindowState(&dockLabelProbe);
        QEventLoop floatingRestoreEvents;
        // Native maximize restoration is asynchronous on Windows; allow the
        // real state-change event to arrive instead of making this test depend
        // on a single scheduler slice.
        QTimer::singleShot(500, &floatingRestoreEvents, &QEventLoop::quit);
        floatingRestoreEvents.exec();
        const bool floatingDockMaximizedRestores = dockLabelProbe.isMaximized();
        const int paintsBeforeTabRefresh = floatingPaintProbe->paints;
        ei::refreshFloatingDockTabs(&dockLabelProbe, &validationDockProbe);
        QApplication::processEvents();
        const bool floatingDockTabRefreshRepaints =
                floatingPaintProbe->paints > paintsBeforeTabRefresh;
        QSettings().remove(QStringLiteral("ui/floatingDocks/messageLogDockProbe"));
        QPointer<QWidget> floatingDockHost = dockLabelProbe.window();
        ei::closeFloatingDockHost(&dockLabelProbe);
        QEventLoop floatingDockEvents;
        QTimer::singleShot(25, &floatingDockEvents, &QEventLoop::quit);
        floatingDockEvents.exec();
        const bool floatingClosedLabelRemoved = !hiddenTab.tabBar()->isVisible();
        const bool floatingDockHostClosed = !dockLabelProbe.isVisible()
                && (!floatingDockHost || !floatingDockHost->isVisible());
        const bool routineDockDiagnosticsSuppressed =
                dockDiagnosticCount() == dockDiagnosticsBefore;
        CResourceBrowser resourceBrowser;
        QTreeWidget* resourceArchiveTree = resourceBrowser.findChild<QTreeWidget*>(
                    QStringLiteral("resourceArchiveTree"));
        const bool resourceBrowserContract = resourceBrowser.uiContractSatisfied()
                && resourceBrowser.findChild<QLineEdit*>(
                    QStringLiteral("resourceBrowserSearch"))
                && resourceBrowser.findChild<QTreeWidget*>(
                    QStringLiteral("resourceContentView"))
                && resourceArchiveTree
                && !resourceArchiveTree->rootIsDecorated()
                && resourceArchiveTree->columnCount() == 3
                && resourceBrowser.findChild<QWidget*>(
                    QStringLiteral("resourceImagePanZoom"))
                && resourceBrowser.findChild<QWidget*>(
                    QStringLiteral("resourceModelCanvas"))
                && resourceBrowser.findChild<QComboBox*>(
                    QStringLiteral("resourceAnimationList"))
                && resourceBrowser.findChild<QTreeWidget*>(
                    QStringLiteral("resourceModelParts"));

        // Catalog insertion order and the sorted visual order intentionally
        // disagree. Resource navigation must resolve the stable entry carried
        // by the visual item, not reuse an unsorted catalog row number.
        const bool sortedResourceReferenceSelectsExactTarget=
            resourceNavigationExactTargetCheck();

        QMainWindow resourceNavigationHost;
        QDockWidget resourceNavigationDock(&resourceNavigationHost);
        resourceNavigationHost.addDockWidget(Qt::RightDockWidgetArea,
                                             &resourceNavigationDock);
        QDialog resourceNavigationSource(&resourceNavigationHost);
        resourceNavigationHost.show();
        resourceNavigationSource.show();
        resourceNavigationSource.activateWindow();
        QApplication::processEvents();
        ei::revealDockNavigationTarget(&resourceNavigationHost,
                                       &resourceNavigationDock);
        QApplication::processEvents();
        const bool resourceNavigationPreservesDockedTarget =
                resourceNavigationHost.isVisible()
                && resourceNavigationDock.isVisible()
                && !resourceNavigationDock.isFloating();
        resourceNavigationSource.close();
        resourceNavigationHost.close();

        // Reproduce closing and immediately reopening the modeless constructor
        // before Qt dispatches deferred-delete events. The window is meant to
        // be reusable for the lifetime of MainWindow.
        QPointer<ei::ItemSpellConstructorDialog> constructorLifecycleProbe =
            new ei::ItemSpellConstructorDialog({}, {}, nullptr, nullptr);
        constructorLifecycleProbe->show();
        QApplication::processEvents();
        constructorLifecycleProbe->close();
        constructorLifecycleProbe->show();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
        const bool itemConstructorCloseReopenSafe = constructorLifecycleProbe
            && constructorLifecycleProbe->isVisible();
        delete constructorLifecycleProbe;

        // Exercise the production receiver chain after the isolated widget
        // checks. MainWindow initializes process-wide UI/render services, so
        // constructing it earlier contaminates otherwise independent probes.
        qApp->setProperty("enduranceDatabasePrototypeNavigationProbe", true);
        qApp->setProperty("enduranceDatabasePrototypeNavigationReceived", QString());
        MainWindow* productionWindow = new MainWindow;
        const bool productionRouteInvoked = productionWindow->view()
            && QMetaObject::invokeMethod(productionWindow->view(),
                "openDatabasePrototypeRequested", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("Human Test")));
        QApplication::processEvents();
        const bool databasePrototypeMainWindowRouteWorks = productionRouteInvoked
            && qApp->property("enduranceDatabasePrototypeNavigationReceived").toString()
                == QStringLiteral("Human Test");
        CPreview detachedPreview(nullptr, productionWindow->view());
        detachedPreview.resize(160,120);
        detachedPreview.show();
        QApplication::processEvents();
        const bool detachedPreviewSharesViewportContext = detachedPreview.isSharing();
        detachedPreview.close();
        qApp->setProperty("enduranceDatabasePrototypeNavigationProbe", false);
        // This unshown production probe owns process-wide UI/render services;
        // retain it for the remaining few statements and let process exit
        // reclaim it, matching application lifetime.

        CTileForm tileForm;
        QVector<QImage> verifiedAtlasTiles;
        QVector<qint32> verifiedAtlasTypes;
        verifiedAtlasTiles.reserve(64);
        verifiedAtlasTypes.reserve(64);
        for(int slot = 0; slot < 64; ++slot)
        {
            QImage tile(32, 32, QImage::Format_RGBA8888);
            tile.fill(QColor::fromHsv((slot * 37) % 360, 180, 180));
            verifiedAtlasTiles.append(tile);
            verifiedAtlasTypes.append(qint32(ETileType::eGrass));
        }
        const bool freshTileFormAdoptsVerifiedAtlas =
            tileForm.adoptTileImages(verifiedAtlasTiles, verifiedAtlasTypes);
        QTableWidget* quickTileTable =
            tileForm.findChild<QTableWidget*>(QStringLiteral("tableQuick"));
        bool freshTileFormQuickItemsReady = freshTileFormAdoptsVerifiedAtlas
                && quickTileTable && quickTileTable->rowCount() == 1
                && quickTileTable->columnCount() == 8;
        for(int column = 0; freshTileFormQuickItemsReady && column < 8; ++column)
            freshTileFormQuickItemsReady = quickTileTable->item(0, column) != nullptr;
        const bool adoptedPhysicalTileSelectionWorks = tileForm.selectTile(17);
        const bool expandedWorkingTileSelectionRejected =
            !tileForm.selectTile(1393);

        const bool passed = behaviorFilterUsesCachedText && behaviorFilterRebuildSafe
                && resourceBrowserContract && sortedResourceReferenceSelectsExactTarget
                && resourceNavigationPreservesDockedTarget
                && virtualLogicFocusWorks && virtualLogicFocusSurvivesRebuild
                && prefixesOnly && valueStable && focusCancel && escapeCancel
                && staticChoiceItemTooltipsWork && alarmConditionItemTooltipsWork
                && trapTargetsUseReadOnlyTable
                && diplomacyValuesExplainMeaning
                && diplomacyDiagonalUsesRelationColor
                && diplomacySelectionCrossHighlights
                && diplomacyHeadersBoldSelection
                && oneWayRedoPreservesReverse && oneWayUndoPreservesReverse
                && selfRelationEditable && selfRelationUndoable
                && diplomacyWindowAutoFitsAndResizable
                && diplomacyFirstOpenFitsContent
                && diplomacyResizeUsesFixedSections
                && diplomacyResizeRepaintCoalesced && diplomacyResizeRepaintResumes
                && diplomacyDirectionControlVisible && diplomacyWindowSizePersists
                && unitCategoryIdsVisible && helperFigureCacheIdentityDistinct
                && databasePrototypeMenuEmissionWorks
                && databasePrototypeMainWindowRouteWorks
                && detachedPreviewSharesViewportContext
                && itemConstructorCloseReopenSafe
                && enterCommit && clickCommit && settingsTabsUsable && settingsShortcutsPersist
                && autosaveSettingsPersist && settingsApplySignal && autosaveLiveReconfigure
                && cameraSpeedSettingsPersist
                && telephotoControlsContract && telephotoSettingsPersist
                && telephotoEnabledSessionOnly
                && uiScaleSettingsPersist && uiScaleLiveApply && uiScaleFontApplied
                && uiScaleExistingWindowApplied
                && settingsApplyKeepsOpen && settingsSaveCloseWorks
                && settingsButtonLayoutWorks
                && languageSettingsPersist && languageRestartPromptShown
                && russianCatalogLoads && qtStandardDialogsTranslate
                && russianFallbackWorks
                && validationTemplatesTranslate
                && russianLayoutsFit
                && sessionIdentityRoundTrip
                && renderOptionsRegistered
                && terrainShadowToggleApplies
                && secondaryWindowsClosed
                && deletingSecondaryWindowClosedSafely
                && pasteSingleAction && pasteUndoAtomic && pasteRedoAtomic
                && incompatiblePasteBlocked && compatiblePastePreview && compatiblePasteCommit
                && multiPropertyPaste && mismatchedPropertyPasteBlocked
                && fieldPastePreview && fieldPasteCommit && fieldCopyAvailable && nonTextPropertyPaste
                && numericComponentPaste && individualComponentSelection
                && selectedComponentCopy && componentPlatePaste && componentContextPaste
                && componentContextPreservesSelection && componentContextShowsOnlyComponent
                && componentContextGestureKeepsTableClear && componentContextUsesManualDispatch
                && componentContextFocusIsChildOwned
                && componentSelectionTableExclusive && componentSelectionClearsForProperty
                && componentHitTargetsExpanded && componentLayoutHasNoDeadSpace
                && lockedNumericPasteBlocked
                && partialCopyExact && partialPasteExact && partialPasteCommit
                && propertyScrollPreserved
                && mixedModelPartsVisible
                && serializedUnknownPartsVisible
                && bodyPartFilterDisplayOnly
                && bodyPartStateFilterAliases
                && bodyPartStateTextHidden
                && bodyPartColumnsResizable
                && validationColumnsResizable
                && bodyPartSourceFiltering
                && bodyPartColumnSorting
                && bodyPartMultiSelectionToggle
                && humanoidHairOptionAvailable
                && bodyPartWindowSizePersists
                && bodyPartNoOpPreservesExactArray
                && bodyPartOrderParticipatesInEquality
                && bodyPartDeltaPreservesHiddenAndDuplicates
                && bodyPartPropertyPasteIsExact
                && minimumPaintBrushIsSingleTile
                && underwaterFamiliesVisibleByDefault
                && underwaterFamiliesRemainVisible
                && landscapeLayersVisibleByDefault
                && landscapeVisibilityControlsWork
                && overlaySurfaceToolSeparate
                && overlaySurfaceAdmissionFollowsPickedLayer
                && overlaySlotLabelsAvailableInOverlaySurface
                && overlaySlotLabelsAvailableInOverlayPaint
                && overlaySlotLabelsHiddenOutsideOverlayModes
                && programmaticModeRefreshesMaterials
                && sculptRaiseShowsFalloff
                && sculptFlattenShowsTarget
                && sculptToolOrderMatchesShortcuts
                && flattenHeightCanBeSampled
                && sculptDeltaWheelAdjustmentWorks
                && flattenTargetWheelAdjustmentWorks
                && landscapeMprSettingsButtonWorks
                && embeddedMinimapDefaultsWhenAvailable
                && generatedMinimapCanBeSelected
                && viewportMinimapStartsAtSmallest
                && viewportMinimapHasThreeSizes
                && viewportMinimapCollapsesToTitle
                && viewportMinimapDragNavigationWorks
                && viewportMinimapBlocksClickThrough
                && minimapRefreshSelectsGenerated
                && viewportMinimapLockControlsWork
                && viewportMinimapUnlockWorks
                && minimapExportIsModeless
                && minimapTypedGradeControlsWork
                && minimapGradeResetWorks
                && minimapPreviewCropAndViewPersist
                && minimapViewsLinkByDefault
                && minimapFitDoesNotDrift
                && materialAtlasButtonAvailable
                && logicPointUndoAtomic && logicPointRedoAtomic
                && viewportBackgroundConsumesClick && viewportChildConsumesClick
                && viewportSurfaceFullyOwned
                && messageLogSearchWorks && messageLogTypeFilterWorks
                && messageLogColumnsResizable && messageLogMultiCopyWorks
                && messageLogSeverityColorsWork
                && messageLogStartsAtBottom && messageLogManualScrollPreserved
                && messageLogFollowResumesAtBottom
                && texturelessDrawDoesNotSpamLog
                && dockedHiddenLabelAppears && floatingClosedLabelRemoved
                && floatingDockGroupingEnabled && floatingDockWindowControls
                && floatingDockIsNormalWindow && floatingDockMaximizedRestores
                && floatingDockTabRefreshRepaints && floatingDockTaskbarIdentity
                && floatingDockHostClosed
                && validationViewStartsUnchecked && validationViewShowsAndChecks
                && validationViewHidesAndUnchecks && validationViewReopensBesideMessageLog
                && routineDockDiagnosticsSuppressed
                && QCoreApplication::testAttribute(Qt::AA_DisableHighDpiScaling)
                && !QCoreApplication::testAttribute(Qt::AA_EnableHighDpiScaling)
            && uniqueMaterialPaletteCards
            && materialVariantsRemainDistinctAndGrouped
            && materialPaletteScrollsAndFillsWidth
            && landscapeToolsAreCompactAndPaletteFillsHeight
            && threeTerrainPaintModesAvailable
            && classicPaletteFirstOpenDoesNotOverlapControls
            && terrainPaintBetaControlsAreExplicit
            && contextualStampVisibleForVertex
            && contextualStampHiddenForTile
            && sharedTerrainPaintFilterWorks
            && manualAtlasGridUsesTooltips
            && defaultAtlasSourceVisible
            && localAtlasSourceVisible
            && generatedAtlasCountVisible
            && expandedAtlasCapacityIsYellow
            && unstableAtlasCapacityIsRedButInformational
            && materialAtlasActionWorks
            && modernDraftActionWorks && modernDraftRequested
            && redundantFinalizeActionRemoved
            && paintIssueMarkerActivates
            && paintIssueMarkerMutuallyExclusive
            && paintIssueTemporarySelectPreservesEditing
            && freshTileFormAdoptsVerifiedAtlas
            && freshTileFormQuickItemsReady
            && adoptedPhysicalTileSelectionWorks
            && expandedWorkingTileSelectionRejected;
        QTextStream output(stdout);
        output << "behaviorFilterUsesCachedText=" << behaviorFilterUsesCachedText
               << " behaviorFilterRebuildSafe=" << behaviorFilterRebuildSafe
               << " virtualLogicFocusWorks=" << virtualLogicFocusWorks
               << " virtualLogicFocusSurvivesRebuild="
               << virtualLogicFocusSurvivesRebuild << "\n"
               << "asset-picker prefixes=" << completions.join(",") << "\n"
               << "prefixesOnly=" << prefixesOnly
               << " valueStable=" << valueStable
               << " focusCancel=" << focusCancel
               << " escapeCancel=" << escapeCancel
               << " enterCommit=" << enterCommit
               << " clickCommit=" << clickCommit
               << " databasePrototypeMenuEmissionWorks="
               << databasePrototypeMenuEmissionWorks
               << " databasePrototypeMainWindowRouteWorks="
               << databasePrototypeMainWindowRouteWorks
               << " detachedPreviewSharesViewportContext="
               << detachedPreviewSharesViewportContext
               << " itemConstructorCloseReopenSafe="
               << itemConstructorCloseReopenSafe
               << " staticChoiceItemDataStored=" << staticChoiceItemDataStored
               << " staticChoicePopupTooltipClear=" << staticChoicePopupTooltipClear
               << " staticChoiceItemTooltipsWork=" << staticChoiceItemTooltipsWork
               << " alarmConditionItemTooltipsWork=" << alarmConditionItemTooltipsWork
               << " trapTargetsUseReadOnlyTable=" << trapTargetsUseReadOnlyTable
               << " diplomacyValuesExplainMeaning=" << diplomacyValuesExplainMeaning
               << " diplomacyDiagonalUsesRelationColor=" << diplomacyDiagonalUsesRelationColor
               << " diplomacySelectionCrossHighlights=" << diplomacySelectionCrossHighlights
               << " diplomacyHeadersBoldSelection=" << diplomacyHeadersBoldSelection
               << " oneWayRedoPreservesReverse=" << oneWayRedoPreservesReverse
               << " oneWayUndoPreservesReverse=" << oneWayUndoPreservesReverse
               << " selfRelationEditable=" << selfRelationEditable
               << " selfRelationUndoable=" << selfRelationUndoable
               << " diplomacyWindowAutoFitsAndResizable=" << diplomacyWindowAutoFitsAndResizable
               << " diplomacyFirstOpenFitsContent=" << diplomacyFirstOpenFitsContent
               << " diplomacyResizeUsesFixedSections=" << diplomacyResizeUsesFixedSections
               << " diplomacyResizeRepaintCoalesced=" << diplomacyResizeRepaintCoalesced
               << " diplomacyResizeRepaintResumes=" << diplomacyResizeRepaintResumes
               << " diplomacyDirectionControlVisible=" << diplomacyDirectionControlVisible
               << " diplomacyWindowSizePersists=" << diplomacyWindowSizePersists
               << " unitCategoryIdsVisible=" << unitCategoryIdsVisible
               << " helperFigureCacheIdentityDistinct=" << helperFigureCacheIdentityDistinct
               << " settingsTabsUsable=" << settingsTabsUsable
               << " settingsShortcutsPersist=" << settingsShortcutsPersist
               << " autosaveSettingsPersist=" << autosaveSettingsPersist
               << " autosaveLiveReconfigure=" << autosaveLiveReconfigure
               << " cameraSpeedSettingsPersist=" << cameraSpeedSettingsPersist
               << " telephotoControlsContract=" << telephotoControlsContract
               << " telephotoSettingsPersist=" << telephotoSettingsPersist
               << " telephotoEnabledSessionOnly=" << telephotoEnabledSessionOnly
               << " uiScaleSettingsPersist=" << uiScaleSettingsPersist
               << " uiScaleLiveApply=" << uiScaleLiveApply
               << " uiScaleFontApplied=" << uiScaleFontApplied
               << " uiScaleExistingWindowApplied=" << uiScaleExistingWindowApplied
               << " settingsApplyKeepsOpen=" << settingsApplyKeepsOpen
               << " settingsSaveCloseWorks=" << settingsSaveCloseWorks
               << " settingsButtonLayoutWorks=" << settingsButtonLayoutWorks
               << " languageSettingsPersist=" << languageSettingsPersist
               << " languageRestartPromptShown=" << languageRestartPromptShown
               << " russianCatalogLoads=" << russianCatalogLoads
               << " qtStandardDialogsTranslate=" << qtStandardDialogsTranslate
               << " russianFallbackWorks=" << russianFallbackWorks
               << " validationTemplatesTranslate=" << validationTemplatesTranslate
               << " russianLayoutsFit=" << russianLayoutsFit
               << " sessionIdentityRoundTrip=" << sessionIdentityRoundTrip
               << " settingsApplySignal=" << settingsApplySignal
               << " renderOptionsRegistered=" << renderOptionsRegistered
               << " terrainShadowToggleApplies=" << terrainShadowToggleApplies
               << " secondaryWindowsClosed=" << secondaryWindowsClosed
               << " deletingSecondaryWindowClosedSafely=" << deletingSecondaryWindowClosedSafely
               << " pasteSingleAction=" << pasteSingleAction
               << " pasteUndoAtomic=" << pasteUndoAtomic
               << " pasteRedoAtomic=" << pasteRedoAtomic
               << " incompatiblePasteBlocked=" << incompatiblePasteBlocked
               << " compatiblePastePreview=" << compatiblePastePreview
               << " compatiblePasteCommit=" << compatiblePasteCommit
               << " multiPropertyPaste=" << multiPropertyPaste
               << " mismatchedPropertyPasteBlocked=" << mismatchedPropertyPasteBlocked
               << " fieldPastePreview=" << fieldPastePreview
               << " fieldPasteCommit=" << fieldPasteCommit
               << " fieldCopyAvailable=" << fieldCopyAvailable
               << " nonTextPropertyPaste=" << nonTextPropertyPaste << "\n"
               << " numericComponentPaste=" << numericComponentPaste << "\n"
               << " individualComponentSelection=" << individualComponentSelection
               << " selectedComponentCopy=" << selectedComponentCopy
               << " componentPlatePaste=" << componentPlatePaste
               << " componentContextPaste=" << componentContextPaste
               << " componentContextPreservesSelection=" << componentContextPreservesSelection
               << " componentContextShowsOnlyComponent=" << componentContextShowsOnlyComponent
               << " componentContextGestureKeepsTableClear="
               << componentContextGestureKeepsTableClear
               << " componentContextUsesManualDispatch=" << componentContextUsesManualDispatch
               << " componentContextFocusIsChildOwned=" << componentContextFocusIsChildOwned
               << " componentSelectionTableExclusive=" << componentSelectionTableExclusive
               << " componentSelectionClearsForProperty=" << componentSelectionClearsForProperty
               << " componentHitTargetsExpanded=" << componentHitTargetsExpanded
               << " componentLayoutHasNoDeadSpace=" << componentLayoutHasNoDeadSpace << "\n"
               << " lockedNumericPasteBlocked=" << lockedNumericPasteBlocked << "\n"
               << " partialCopyExact=" << partialCopyExact
               << " partialPasteExact=" << partialPasteExact
               << " partialPasteCommit=" << partialPasteCommit << "\n"
               << " propertyScrollPreserved=" << propertyScrollPreserved << "\n"
               << " mixedModelPartsVisible=" << mixedModelPartsVisible
               << " serializedUnknownPartsVisible=" << serializedUnknownPartsVisible
               << " bodyPartFilterDisplayOnly=" << bodyPartFilterDisplayOnly
               << " bodyPartStateFilterAliases=" << bodyPartStateFilterAliases
               << " bodyPartStateTextHidden=" << bodyPartStateTextHidden
               << " bodyPartColumnsResizable=" << bodyPartColumnsResizable
               << " validationColumnsResizable=" << validationColumnsResizable
               << " bodyPartSourceFiltering=" << bodyPartSourceFiltering
               << " bodyPartColumnSorting=" << bodyPartColumnSorting
               << " bodyPartMultiSelectionToggle=" << bodyPartMultiSelectionToggle
               << " humanoidHairOptionAvailable=" << humanoidHairOptionAvailable
               << " bodyPartWindowSizePersists=" << bodyPartWindowSizePersists
               << " bodyPartNoOpPreservesExactArray=" << bodyPartNoOpPreservesExactArray
               << " bodyPartOrderParticipatesInEquality="
               << bodyPartOrderParticipatesInEquality
               << " bodyPartDeltaPreservesHiddenAndDuplicates="
               << bodyPartDeltaPreservesHiddenAndDuplicates
               << " bodyPartPropertyPasteIsExact=" << bodyPartPropertyPasteIsExact
               << " minimumPaintBrushIsSingleTile=" << minimumPaintBrushIsSingleTile
               << " underwaterFamiliesVisibleByDefault=" << underwaterFamiliesVisibleByDefault
               << " underwaterFamiliesRemainVisible=" << underwaterFamiliesRemainVisible
               << " landscapeLayersVisibleByDefault=" << landscapeLayersVisibleByDefault
               << " landscapeVisibilityControlsWork=" << landscapeVisibilityControlsWork
               << " overlaySurfaceToolSeparate=" << overlaySurfaceToolSeparate
               << " overlaySurfaceAdmissionFollowsPickedLayer="
               << overlaySurfaceAdmissionFollowsPickedLayer
               << " overlaySlotLabelsAvailableInOverlaySurface="
               << overlaySlotLabelsAvailableInOverlaySurface
               << " overlaySlotLabelsAvailableInOverlayPaint="
               << overlaySlotLabelsAvailableInOverlayPaint
               << " overlaySlotLabelsHiddenOutsideOverlayModes="
               << overlaySlotLabelsHiddenOutsideOverlayModes
               << " programmaticModeRefreshesMaterials="
               << programmaticModeRefreshesMaterials
               << " sculptRaiseShowsFalloff=" << sculptRaiseShowsFalloff
               << " sculptFlattenShowsTarget=" << sculptFlattenShowsTarget
               << " sculptToolOrderMatchesShortcuts="
               << sculptToolOrderMatchesShortcuts
               << " flattenHeightCanBeSampled=" << flattenHeightCanBeSampled
               << " sculptDeltaWheelAdjustmentWorks="
               << sculptDeltaWheelAdjustmentWorks
               << " flattenTargetWheelAdjustmentWorks="
               << flattenTargetWheelAdjustmentWorks
               << " landscapeMprSettingsButtonWorks="
               << landscapeMprSettingsButtonWorks
               << " embeddedMinimapDefaultsWhenAvailable="
               << embeddedMinimapDefaultsWhenAvailable
               << " generatedMinimapCanBeSelected=" << generatedMinimapCanBeSelected
               << " viewportMinimapStartsAtSmallest="
               << viewportMinimapStartsAtSmallest
               << " viewportMinimapHasThreeSizes=" << viewportMinimapHasThreeSizes
               << " viewportMinimapCollapsesToTitle=" << viewportMinimapCollapsesToTitle
               << " viewportMinimapDragNavigationWorks="
               << viewportMinimapDragNavigationWorks
               << " minimapDragPointCount=" << minimapDragPoints.size()
               << " viewportMinimapBlocksClickThrough="
               << viewportMinimapBlocksClickThrough
               << " viewportMinimapLockControlsWork=" << viewportMinimapLockControlsWork
               << " minimapRefreshRequested=" << minimapRefreshRequested
               << " minimapLockSignalWhenLocked=" << minimapLockSignalWhenLocked
               << " minimapRefreshDisabledWhenLocked="
               << minimapRefreshDisabledWhenLocked
               << " minimapLockCheckedWhenLocked=" << minimapLockCheckedWhenLocked
               << " minimapOverlayPaintContract=" << minimapOverlayPaintContract
               << " minimapRefreshSelectsGenerated=" << minimapRefreshSelectsGenerated
               << " viewportMinimapUnlockWorks=" << viewportMinimapUnlockWorks
               << " minimapExportIsModeless=" << minimapExportIsModeless
               << " minimapTypedGradeControlsWork=" << minimapTypedGradeControlsWork
               << " minimapGradeResetWorks=" << minimapGradeResetWorks
               << " minimapPreviewCropAndViewPersist="
               << minimapPreviewCropAndViewPersist
               << " minimapViewsLinkByDefault=" << minimapViewsLinkByDefault
               << " minimapFitDoesNotDrift=" << minimapFitDoesNotDrift
               << " materialAtlasButtonAvailable=" << materialAtlasButtonAvailable
               << " programmaticModeRefreshesMaterials="
               << programmaticModeRefreshesMaterials
               << " uniqueMaterialPaletteCards="
               << uniqueMaterialPaletteCards
               << " materialVariantsRemainDistinctAndGrouped="
               << materialVariantsRemainDistinctAndGrouped
               << " materialPaletteScrollsAndFillsWidth="
               << materialPaletteScrollsAndFillsWidth
               << " landscapeToolsAreCompactAndPaletteFillsHeight="
               << landscapeToolsAreCompactAndPaletteFillsHeight
               << " threeTerrainPaintModesAvailable="
               << threeTerrainPaintModesAvailable
               << " classicPaletteFirstOpenDoesNotOverlapControls="
               << classicPaletteFirstOpenDoesNotOverlapControls
               << " terrainPaintBetaControlsAreExplicit="
               << terrainPaintBetaControlsAreExplicit
               << " contextualStampVisibleForVertex="
               << contextualStampVisibleForVertex
               << " contextualStampHiddenForTile="
               << contextualStampHiddenForTile
               << " classicPaletteBottom=" << paletteBottom
               << " terrainPaintRadiusTop=" << radiusTop
               << " sharedTerrainPaintFilterWorks="
               << sharedTerrainPaintFilterWorks
               << " manualAtlasGridUsesTooltips="
               << manualAtlasGridUsesTooltips
               << " landscapeToolsMinimumWidth="
               << landscapeTools.minimumSizeHint().width()
               << " landscapeToolsWidth=" << landscapeTools.width()
               << " materialPaletteTabsHeight="
               << (materialPaletteTabs ? materialPaletteTabs->height() : -1)
               << " groundMaterialScrollHeight="
               << (groundMaterialScroll ? groundMaterialScroll->height() : -1)
               << " defaultAtlasSourceVisible=" << defaultAtlasSourceVisible
               << " localAtlasSourceVisible=" << localAtlasSourceVisible
               << " generatedAtlasCountVisible=" << generatedAtlasCountVisible
               << " modernDraftActionWorks=" << modernDraftActionWorks
               << " modernDraftRequested=" << modernDraftRequested
               << " redundantFinalizeActionRemoved="
               << redundantFinalizeActionRemoved
               << " expandedAtlasCapacityIsYellow="
               << expandedAtlasCapacityIsYellow
               << " unstableAtlasCapacityIsRedButInformational="
               << unstableAtlasCapacityIsRedButInformational
               << " materialAtlasActionWorks=" << materialAtlasActionWorks
               << " paintIssueMarkerActivates=" << paintIssueMarkerActivates
               << " paintIssueMarkerMutuallyExclusive="
               << paintIssueMarkerMutuallyExclusive
               << " paintIssueTemporarySelectPreservesEditing="
               << paintIssueTemporarySelectPreservesEditing
               << " freshTileFormAdoptsVerifiedAtlas="
               << freshTileFormAdoptsVerifiedAtlas
               << " freshTileFormQuickItemsReady="
               << freshTileFormQuickItemsReady
               << " adoptedPhysicalTileSelectionWorks="
               << adoptedPhysicalTileSelectionWorks
               << " expandedWorkingTileSelectionRejected="
               << expandedWorkingTileSelectionRejected
               << " logicPointUndoAtomic=" << logicPointUndoAtomic
               << " logicPointRedoAtomic=" << logicPointRedoAtomic
               << " viewportBackgroundConsumesClick=" << viewportBackgroundConsumesClick
               << " viewportChildConsumesClick=" << viewportChildConsumesClick
               << " viewportSurfaceFullyOwned=" << viewportSurfaceFullyOwned
               << " messageLogSearchWorks=" << messageLogSearchWorks
               << " messageLogTypeFilterWorks=" << messageLogTypeFilterWorks
               << " messageLogColumnsResizable=" << messageLogColumnsResizable
               << " messageLogMultiCopyWorks=" << messageLogMultiCopyWorks
               << " messageLogSeverityColorsWork=" << messageLogSeverityColorsWork
               << " messageLogStartsAtBottom=" << messageLogStartsAtBottom
               << " messageLogManualScrollPreserved=" << messageLogManualScrollPreserved
               << " messageLogFollowResumesAtBottom=" << messageLogFollowResumesAtBottom
               << " texturelessDrawDoesNotSpamLog=" << texturelessDrawDoesNotSpamLog
               << " dockedHiddenLabelAppears=" << dockedHiddenLabelAppears
               << " floatingClosedLabelRemoved=" << floatingClosedLabelRemoved
               << " floatingDockGroupingEnabled=" << floatingDockGroupingEnabled
               << " floatingDockWindowControls=" << floatingDockWindowControls
               << " floatingDockIsNormalWindow=" << floatingDockIsNormalWindow
               << " floatingDockMaximizedRestores=" << floatingDockMaximizedRestores
               << " floatingDockTabRefreshRepaints=" << floatingDockTabRefreshRepaints
               << " floatingDockTaskbarIdentity=" << floatingDockTaskbarIdentity
               << " floatingDockHostClosed=" << floatingDockHostClosed
               << " validationViewStartsUnchecked=" << validationViewStartsUnchecked
               << " validationViewShowsAndChecks=" << validationViewShowsAndChecks
               << " validationViewHidesAndUnchecks=" << validationViewHidesAndUnchecks
               << " validationViewReopensBesideMessageLog="
               << validationViewReopensBesideMessageLog
               << " routineDockDiagnosticsSuppressed=" << routineDockDiagnosticsSuppressed
               << " resourceBrowserContract=" << resourceBrowserContract
               << " physicalPixelViewportScaling="
               << QCoreApplication::testAttribute(Qt::AA_DisableHighDpiScaling)
               << " viewportSurfaceTopLeftAlpha=" << viewportSurface.pixelColor(1, 1).alpha()
               << " viewportSurfaceBottomRightAlpha="
               << viewportSurface.pixelColor(viewportSurface.width() - 2,
                                             viewportSurface.height() - 2).alpha() << "\n"
               << (passed ? "PASS" : "FAIL")
               << ": widget interaction and secondary-window lifecycle checks\n";
        output.flush();
        return passed ? 0 : 1;
    }

    if(mobDiff)
    {
        if(!parser.isSet(againstOption))
        {
            QTextStream(stderr) << "--mob-diff requires --against <candidate.mob>.\n";
            return 2;
        }
        const QFileInfo baselineInfo(parser.value(mobDiffOption));
        const QFileInfo candidateInfo(parser.value(againstOption));
        CValidationReport baselineReport;
        CValidationReport candidateReport;
        CMobDocument baseline;
        CMobDocument candidate;
        const bool baselineLoaded = baseline.load(baselineInfo, &baselineReport);
        const bool candidateLoaded = candidate.load(candidateInfo, &candidateReport);
        if(!baselineLoaded || !candidateLoaded)
        {
            if(parser.isSet(jsonOption))
            {
                QJsonObject failure;
                failure.insert("baselineLoaded", baselineLoaded);
                failure.insert("candidateLoaded", candidateLoaded);
                failure.insert("baselineFindings", baselineReport.toJson());
                failure.insert("candidateFindings", candidateReport.toJson());
                QTextStream(stdout) << QJsonDocument(failure).toJson(QJsonDocument::Indented);
            }
            else
                QTextStream(stdout) << baselineReport.toText() << candidateReport.toText();
            return 1;
        }
        const CMobSemanticDiff diff = CMobSemanticDiff::compare(baseline, candidate);
        if(parser.isSet(jsonOption))
        {
            QJsonObject result = diff.toJson();
            result.insert("baseline", baselineInfo.absoluteFilePath());
            result.insert("candidate", candidateInfo.absoluteFilePath());
            QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Indented);
        }
        else
            QTextStream(stdout) << diff.toText();
        return 0;
    }

    if(profileLoad)
    {
        SResourceProfileEvaluation selected;
        for(const SResourceProfile& profile : loadResourceProfiles())
        {
            const SResourceProfileEvaluation candidate =
                evaluateResourceProfile(parser.value(profileLoadOption), profile);
            if(candidate.valid)
            {
                selected = candidate;
                break;
            }
            if(selected.profileId.isEmpty())
                selected = candidate;
        }
        QJsonObject root;
        root.insert("valid", selected.valid);
        root.insert("profileId", selected.profileId);
        root.insert("profileName", selected.profileName);
        root.insert("gameRoot", selected.gameRoot);
        root.insert("modRoot", selected.modRoot);
        root.insert("archiveCount", selected.archiveCount);
        root.insert("runtimeManifestId", selected.runtimeManifestId);
        root.insert("runtimeStackComplete", selected.runtimeStackComplete);
        root.insert("runtimeStackValidated", selected.runtimeStackValidated);
        QJsonArray runtimeComponents;
        for(const SRuntimeComponentEvaluation& component : selected.runtimeComponents)
        {
            QJsonObject value;
            value.insert("id", component.id);
            value.insert("path", component.path);
            value.insert("exists", component.exists);
            value.insert("size", double(component.size));
            value.insert("sha256", component.sha256);
            value.insert("hashAccepted", component.hashAccepted);
            value.insert("enforceHash", component.enforceHash);
            runtimeComponents.append(value);
        }
        root.insert("runtimeComponents", runtimeComponents);
        QJsonObject capabilities;
        for(auto it = selected.capabilities.constBegin(); it != selected.capabilities.constEnd(); ++it)
        {
            QJsonObject value;
            value.insert("status", profileCapabilityStatusName(it.value().status));
            value.insert("reason", it.value().reason);
            value.insert("runtimeManifestId", it.value().runtimeManifestId);
            capabilities.insert(it.key(), value);
        }
        root.insert("capabilities", capabilities);
        root.insert("resourceDirectories", QJsonArray::fromStringList(selected.resourceDirectories));
        root.insert("mapDirectories", QJsonArray::fromStringList(selected.mapDirectories));
        root.insert("messages", QJsonArray::fromStringList(selected.messages));
        QTextStream(stdout) << QJsonDocument(root).toJson(QJsonDocument::Indented);
        return selected.valid ? 0 : 1;
    }

    const QString path = parser.value(validate ? validateOption : roundtripOption);
    const QFileInfo info(path);
    CValidationReport report;
    QByteArray original;
    QByteArray serialized;
    bool loaded = false;
    if(info.suffix().compare("mob", Qt::CaseInsensitive) == 0)
    {
        CMobDocument document;
        loaded = document.load(info, &report);
        original = document.originalBytes();
        serialized = document.serialize();
    }
    else if(info.suffix().compare("mpr", Qt::CaseInsensitive) == 0)
    {
        CMprDocument document;
        loaded = document.load(info, &report);
        original = document.originalBytes();
        serialized = document.serialize();
    }
    else
    {
        report.add("file.type", EValidationSeverity::Error, "Expected a .mob or .mpr file", path);
    }

    const bool byteIdentical = loaded && original == serialized;
    if(roundtrip && !byteIdentical)
        report.add("roundtrip.changed", EValidationSeverity::Error, "Unchanged serialization differs from the source", path);

    QTextStream output(stdout);
    if(parser.isSet(jsonOption))
    {
        QJsonObject root;
        root.insert("file", info.absoluteFilePath());
        root.insert("loaded", loaded);
        root.insert("byteIdentical", byteIdentical);
        root.insert("findings", report.toJson());
        output << QJsonDocument(root).toJson(QJsonDocument::Indented);
    }
    else
    {
        output << report.toText();
        if(roundtrip)
            output << (byteIdentical ? "PASS" : "FAIL") << ": byte-identical unchanged round trip\n";
    }
    return (!loaded || report.hasErrors()) ? 1 : 0;
}
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Automated smoke runs must never leave a modal Windows crash dialog on
    // the operator's desktop. The normal unhandled-exception logger still
    // records the failure and writes its private minidump.
    if(automatedWorkspaceSmokeRequested(argc, argv))
        SetErrorMode(SetErrorMode(0) | SEM_NOGPFAULTERRORBOX);
#endif
    // The legacy OpenGL viewport and its embedded overlays use physical pixel
    // coordinates. Qt's global logical-coordinate conversion makes the GL
    // framebuffer render into only part of the widget on scaled displays.
    // Keep physical-pixel behavior until the entire viewport pipeline is made
    // device-pixel-ratio aware; Endurance's explicit UI scale remains usable.
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);

    const char* openGlBackend = requestedOpenGlBackend(argc, argv);
    const bool renderBenchmark = renderBenchmarkRequested(argc, argv);
    if(std::strcmp(openGlBackend, "angle") == 0)
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    else if(std::strcmp(openGlBackend, "desktop") == 0)
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

    QApplication app(argc, argv);
    g_previousQtMessageHandler = qInstallMessageHandler(
        enduranceQtMessageHandler);
    app.setWindowIcon(QIcon(":/endurance_gate.png"));

    // QGLWidget reads the default format when the first preview/view widget is
    // constructed. Normal editing follows display VSync; benchmark mode is an
    // explicit restart-time mode so uncapped throughput is measurable.
    const bool configuredVsync = persistedRenderVsync();
    const int swapInterval = ei::renderSwapInterval(configuredVsync, renderBenchmark);
    QGLFormat glFormat = QGLFormat::defaultFormat();
    glFormat.setSwapInterval(swapInterval);
    QGLFormat::setDefaultFormat(glFormat);
    app.setProperty("renderBenchmark", renderBenchmark);
    app.setProperty("renderConfiguredVSync", configuredVsync);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(enduranceUnhandledException);
#endif

    QCoreApplication::setOrganizationName("EI Mapper Endurance");
    QCoreApplication::setOrganizationDomain("ei-mapper-endurance.local");
    QCoreApplication::setApplicationName("EI Mapper Endurance Edition");
    // Version is independent from upstream EI Mapper releases.
    QCoreApplication::setApplicationVersion(ENDURANCE_VERSION);
    ei::log(eLogInfo, QString("Runtime version:%1 qt:%2 architecture:%3 OpenGL-request:%4 presentation:%5")
            .arg(QCoreApplication::applicationVersion(), QT_VERSION_STR,
                 QString::number(sizeof(void*) * 8), QString::fromLatin1(openGlBackend),
                 renderBenchmark ? "uncapped-benchmark"
                                 : (configuredVsync ? "display-vsync" : "vsync-off")));

    const int commandResult = runHeadlessCommand(app);
    if(commandResult >= 0)
        return commandResult;

    // Install the selected UI catalog before constructing any visible widget.
    // Headless validation and runtime diagnostics deliberately stay English.
    ei::installApplicationTranslator(app);

    qApp->setStyle(new CEnduranceStyle(QStyleFactory::create("Fusion")));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53,53,53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25,25,25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(250, 248, 240));
    darkPalette.setColor(QPalette::ToolTipText, QColor(32, 32, 32));
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53,53,53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    //darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(20, 208, 28));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    darkPalette.setColor(QPalette::Disabled, QPalette::Base, QColor(65, 65, 65));
    const QColor disabledText(128, 128, 128);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);

    qApp->setPalette(darkPalette);

    qApp->setStyleSheet(
        "QToolTip { color: #202020; background-color: #faf8f0; border: 1px solid #8a8a8a; }"
        "QToolButton:pressed { background-color: #3c583d; }"
        "QWidget:disabled { color: #808080; }"
        "QMenu::item:disabled { color: #808080; }");

    // Apply the saved scale before constructing the splash, first-run dialogs,
    // or main window so a high-DPI user never has to navigate an undersized
    // setup flow just to reach the setting.
    ei::applyUiScalePercent(ei::persistedUiScalePercent());


    CStartupSplash splash;
    splash.showCentered();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    MainWindow window;
    window.setWindowTitle(QString("EI Mapper: Endurance Edition %1").arg(QCoreApplication::applicationVersion()));
    //window.resize(window.sizeHint());

    splash.setStatus(QObject::tr("Opening main window..."));
    window.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    splash.close();
    qApp->setProperty("enduranceStartupScreenGeometry", QVariant());
    window.raise();
    window.activateWindow();
    const QString visualSequence = qApp->property("workspaceVisualSequence").toString();
    if(!visualSequence.isEmpty())
    {
        QVector<QPair<QString, QString>> stages;
        for(const QString& stageText : visualSequence.split(';', QString::SkipEmptyParts))
        {
            const int separator = stageText.indexOf('|');
            if(separator <= 0 || separator >= stageText.size() - 1)
                continue;
            stages.append(qMakePair(stageText.left(separator), stageText.mid(separator + 1)));
        }
        QSharedPointer<int> stageIndex(new int(0));
        QSharedPointer<std::function<void()>> advance(new std::function<void()>);
        *advance = [&window, stages, stageIndex, advance]()
        {
            if(*stageIndex >= stages.size())
            {
                const QString graphOutput = qEnvironmentVariable(
                    "EI_ENDURANCE_GRAPH_CAPTURE");
                if(!graphOutput.isEmpty())
                {
                    QByteArray graph;
                    QString graphError;
                    QString graphSource;
                    if(!window.view()->buildGeneratedAiGraphCandidate(
                            &graph, &graphError, &graphSource, true))
                    {
                        ei::log(eLogWarning,
                                "AI graph capture failed: " + graphError);
                        QCoreApplication::exit(4);
                        return;
                    }
                    QFile output(graphOutput);
                    if(!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
                            || output.write(graph) != graph.size())
                    {
                        ei::log(eLogWarning,
                                "AI graph capture could not write: " + graphOutput);
                        QCoreApplication::exit(5);
                        return;
                    }
                    output.close();
                    ei::log(eLogInfo,
                            QString("AI graph capture wrote:%1 bytes:%2 source:%3")
                                .arg(graphOutput).arg(graph.size()).arg(graphSource));
                }
                ei::log(eLogInfo, QString("Workspace visual sequence complete stages:%1")
                        .arg(stages.size()));
                QTimer::singleShot(500, &window, &QWidget::close);
                return;
            }
            const int current = (*stageIndex)++;
            const QPair<QString, QString>& stage = stages.at(current);
            // This diagnostic models replacing the current map, not layering
            // every prior MOB into one workspace. Keeping old MOBs made the
            // cache regression itself invalid and also flooded family
            // validation with unrelated cross-zone collisions.
            if(current > 0)
            {
                window.view()->unloadMob(QString());
                window.view()->unloadLand(false);
            }
            ei::log(eLogInfo, QString("Workspace visual sequence stage:%1/%2 mpr:%3 mob:%4")
                    .arg(current + 1).arg(stages.size()).arg(stage.first, stage.second));
            if(!window.replaceLoadedLandscape(QFileInfo(stage.first))
                    || !window.loadMobWithProgress(QFileInfo(stage.second)))
            {
                ei::log(eLogWarning, QString("Workspace visual sequence stage failed:%1")
                        .arg(current + 1));
                QCoreApplication::exit(3);
                return;
            }
            const QString earlyGraphOutput = qEnvironmentVariable(
                "EI_ENDURANCE_GRAPH_CAPTURE_EARLY");
            if(current == stages.size() - 1 && !earlyGraphOutput.isEmpty())
            {
                QByteArray graph;
                QString graphError;
                QString graphSource;
                if(!window.view()->buildGeneratedAiGraphCandidate(
                        &graph, &graphError, &graphSource, true))
                {
                    ei::log(eLogWarning,
                            "Early AI graph capture failed: " + graphError);
                    QCoreApplication::exit(4);
                    return;
                }
                QFile output(earlyGraphOutput);
                if(!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
                        || output.write(graph) != graph.size())
                {
                    ei::log(eLogWarning,
                            "Early AI graph capture could not write: "
                                + earlyGraphOutput);
                    QCoreApplication::exit(5);
                    return;
                }
                output.close();
                ei::log(eLogInfo,
                        QString("Early AI graph capture wrote:%1 bytes:%2 source:%3")
                            .arg(earlyGraphOutput).arg(graph.size()).arg(graphSource));
            }
        };
        QObject::connect(window.view(), &CView::unitVisualPreparationComplete,
                         &window, [advance]() { QTimer::singleShot(100, *advance); });
        QTimer::singleShot(0, *advance);
        return app.exec();
    }
    QStringList startupDocuments;
    for(int i = 1; i < app.arguments().size(); ++i)
    {
        const QFileInfo candidate(app.arguments().at(i));
        const QString suffix = candidate.suffix().toLower();
        if(candidate.exists() && (suffix == "mpr" || suffix == "mob"))
            startupDocuments.append(candidate.absoluteFilePath());
    }
    if(!startupDocuments.isEmpty())
    {
        QTimer::singleShot(0, &window, [&window, startupDocuments]()
        {
            for(const QString& path : startupDocuments)
                window.openDocument(QFileInfo(path));
            if(qApp->property("terrainStampRegression").toBool())
            {
                QSharedPointer<int> attempts(new int(0));
                QSharedPointer<std::function<void()>> waitForReady(
                    new std::function<void()>);
                *waitForReady = [&window, attempts, waitForReady]() {
                    ++*attempts;
                    if(!window.view()->terrainStampRegressionReady()
                       && *attempts < 300)
                    {
                        QTimer::singleShot(100, *waitForReady);
                        return;
                    }
                    const QString outputPath = qApp->property(
                        "terrainStampRegressionResult").toString();
                    if(!window.view()->terrainStampRegressionReady())
                    {
                        QJsonObject evidence;
                        evidence.insert(QStringLiteral("success"), false);
                        evidence.insert(QStringLiteral("attempts"), *attempts);
                        evidence.insert(QStringLiteral("error"),
                            QStringLiteral("The loaded MPR did not become ready for terrain painting."));
                        const QByteArray json = QJsonDocument(evidence).toJson(
                            QJsonDocument::Indented);
                        if(!outputPath.isEmpty())
                        {
                            QFile output(outputPath);
                            if(output.open(QIODevice::WriteOnly
                                           | QIODevice::Truncate))
                                output.write(json);
                        }
                        QCoreApplication::exit(6);
                        return;
                    }
                    window.showMaximized();
                    window.raise();
                    window.activateWindow();
                    if(!outputPath.isEmpty())
                    {
                        QFile ready(outputPath + QStringLiteral(".ready"));
                        if(ready.open(QIODevice::WriteOnly
                                      | QIODevice::Truncate))
                            ready.write("viewport-ready\n");
                    }
                    QTimer::singleShot(2000, &window,
                        [&window, attempts, outputPath]() {
                        QJsonObject evidence;
                        const bool passed =
                            window.view()->runTerrainStampRegression(
                                2039464354, 6, 6, &evidence);
                        evidence.insert(QStringLiteral("attempts"), *attempts);
                        const QByteArray json = QJsonDocument(evidence).toJson(
                            QJsonDocument::Indented);
                        if(!outputPath.isEmpty())
                        {
                            QFile output(outputPath);
                            if(output.open(QIODevice::WriteOnly
                                           | QIODevice::Truncate))
                                output.write(json);
                        }
                        ei::log(passed ? eLogInfo : eLogWarning,
                            QStringLiteral("Terrain stamp regression result:\n%1")
                                .arg(QString::fromUtf8(json)));
                        QTimer::singleShot(2000, qApp,
                            [passed]() { QCoreApplication::exit(passed ? 0 : 6); });
                    });
                };
                QTimer::singleShot(100, *waitForReady);
                return;
            }
            if(qApp->property("workspaceLoadSmoke").toBool())
            {
                const QString graphOutput = qEnvironmentVariable(
                    "EI_ENDURANCE_GRAPH_CAPTURE");
                if(!graphOutput.isEmpty())
                {
                    QByteArray graph;
                    QString graphError;
                    QString graphSource;
                    if(!window.view()->buildGeneratedAiGraphCandidate(
                            &graph, &graphError, &graphSource, true))
                    {
                        ei::log(eLogWarning,
                                "AI graph capture failed: " + graphError);
                        QCoreApplication::exit(4);
                        return;
                    }
                    QFile output(graphOutput);
                    if(!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
                            || output.write(graph) != graph.size())
                    {
                        ei::log(eLogWarning,
                                "AI graph capture could not write: " + graphOutput);
                        QCoreApplication::exit(5);
                        return;
                    }
                    output.close();
                    ei::log(eLogInfo,
                            QString("AI graph capture wrote:%1 bytes:%2 source:%3")
                                .arg(graphOutput).arg(graph.size()).arg(graphSource));
                }
                ei::log(eLogInfo, QString("Workspace load smoke complete documents:%1")
                        .arg(startupDocuments.size()));
                window.close();
            }
        });
    }
    return app.exec();
}

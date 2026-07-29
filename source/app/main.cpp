#include "main_window.h"
#include <QApplication>
#include <QProgressBar>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QIcon>
#include <QCommandLineParser>
#include <QFileInfo>
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
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QLabel>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QUndoStack>
#include <QClipboard>
#include <QMimeData>
#include <QMenu>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QFormLayout>
#include <QEventLoop>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWidget>
#include <QVariant>
#include <QSettings>
#include <cstdio>
#include <cstring>

#include "endurance_document.h"
#include "log.h"
#include "resource_profile.h"
#include "resourcemanager.h"
#include "layout_components/tablemanager.h"
#include "ui/landscape_tools.h"
#include "ui/bodypartedit_form.h"
#include "window_lifecycle.h"
#include "settings.h"
#include "jmv_map_test_launcher.h"
#include "render_scheduling.h"
#include "utils.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace {
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
    QCommandLineOption testRunReportOption("test-run-report",
        "Validate the authoritative result of a completed Endurance map-test run.",
        "run-root");
    QCommandLineOption againstOption("against", "Candidate MOB used with --mob-diff.", "candidate");
    QCommandLineOption angleOpenGlOption("angle-opengl", "Use Qt's ANGLE/OpenGL ES implementation (diagnostic GUI mode).");
    QCommandLineOption desktopOpenGlOption("desktop-opengl", "Use the system desktop OpenGL implementation (diagnostic GUI mode).");
    QCommandLineOption renderBenchmarkOption("render-benchmark",
        "Disable VSync and run the viewport continuously for renderer throughput capture.");
    parser.addOption(validateOption);
    parser.addOption(roundtripOption);
    parser.addOption(jsonOption);
    parser.addOption(profileLoadOption);
    parser.addOption(mobDiffOption);
    parser.addOption(uiWidgetCheckOption);
    parser.addOption(testRunReportOption);
    parser.addOption(againstOption);
    parser.addOption(angleOpenGlOption);
    parser.addOption(desktopOpenGlOption);
    parser.addOption(renderBenchmarkOption);
    parser.addPositionalArgument("documents", "MPR and MOB documents to open in order.", "[documents...]");
    parser.process(app);

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
    const bool testRunReport = parser.isSet(testRunReportOption);
    if(!validate && !roundtrip && !profileLoad && !mobDiff && !uiWidgetCheck
       && !testRunReport)
        return -1;
    if((validate ? 1 : 0) + (roundtrip ? 1 : 0) + (profileLoad ? 1 : 0)
            + (mobDiff ? 1 : 0) + (uiWidgetCheck ? 1 : 0)
            + (testRunReport ? 1 : 0) > 1)
    {
        QTextStream(stderr) << "Choose one headless command.\n";
        return 2;
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
            QFormLayout* form = qobject_cast<QFormLayout*>(plate->parentWidget()->layout());
            componentLayoutHasNoDeadSpace = componentLayoutHasNoDeadSpace
                    && form && form->horizontalSpacing() == 0 && form->verticalSpacing() == 0;
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
        const bool bodyPartSourceFiltering =
            filterShowsOnly("model option", {"optional_part"})
            && filterShowsOnly("unknown", {"future_part"});
        bodyPartFilter->clear();
        bodyPartTable->sortItems(0, Qt::DescendingOrder);
        const bool bodyPartColumnSorting = bodyPartTable->isSortingEnabled()
            && bodyPartTable->horizontalHeader()->sectionsClickable()
            && bodyPartTable->item(0, 0)->text() == "optional_part";
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
        landscapeTools.activateMode(ELandscapeEditMode::TilePaint, false);
        bool landModeSelectionValid = false;
        const bool programmaticModeRefreshesMaterials =
            !landscapeTools.selectPaintTerrainType(5)
            && landscapeTools.paintTerrainType(&landModeSelectionValid) == 1
            && landModeSelectionValid;
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
            && materialPaletteTabs && materialPaletteTabs->height() >= 300
            && groundMaterialScroll && groundMaterialScroll->height() >= 260;
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

        QWidget primaryWindow;
        bool settingsTabsUsable = false;
        bool settingsShortcutsPersist = false;
        bool autosaveSettingsPersist = false;
        bool autosaveLiveReconfigure = false;
        bool cameraSpeedSettingsPersist = false;
        bool settingsApplySignal = false;
        bool renderOptionsRegistered = false;
        {
            const QString previousDirectory = QDir::currentPath();
            QTemporaryDir settingsDirectory;
            if(settingsDirectory.isValid())
                QDir::setCurrent(settingsDirectory.path());
            {
                CSettings settingsWindow(&primaryWindow);
                QObject::connect(&settingsWindow, &CSettings::settingsApplied,
                                 [&settingsApplySignal]() { settingsApplySignal = true; });
                QObject::connect(&settingsWindow, &CSettings::autosaveSettingsApplied,
                    [&autosaveLiveReconfigure](bool enabled, int intervalMinutes)
                    {
                        autosaveLiveReconfigure = enabled && intervalMinutes == 1;
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
                renderOptionsRegistered = animateUnits && animateUnits->value()
                        && performanceCounters && !performanceCounters->value()
                        && renderVsync && renderVsync->value()
                        && renderFpsCap && renderFpsCap->value() == 0
                        && renderLightingMode && renderLightingMode->value() == 2
                        && renderMipmapping && renderMipmapping->value()
                        && settingsWindow.findChild<QCheckBox*>("renderVSync")
                        && settingsWindow.findChild<QCheckBox*>("renderTextureMipmapping")
                        && settingsWindow.findChild<QLineEdit*>("renderFpsCap");
                if(historyShortcut && duplicateShortcut && stopTestShortcut)
                {
                    historyShortcut->setKeySequence(QKeySequence("Alt+L"));
                    if(autosaveEnabled)
                        autosaveEnabled->setChecked(true);
                    if(autosaveInterval)
                        autosaveInterval->setText("1");
                    if(cameraWheelGesture)
                        cameraWheelGesture->setCurrentIndex(2);
                    QMetaObject::invokeMethod(&settingsWindow, "on_buttonApply_clicked", Qt::DirectConnection);
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
                    COptDouble* savedCameraMultiplier = dynamic_cast<COptDouble*>(
                        settingsWindow.opt(eOptSetGeneral, "cameraSpeedMultiplier"));
                    cameraSpeedSettingsPersist = cameraWheelGesture
                            && savedCameraGesture && savedCameraGesture->value() == "Shift+Wheel"
                            && savedCameraMultiplier
                            && qAbs(savedCameraMultiplier->value() - 1.0) < 0.001;
                }
            }
            QDir::setCurrent(previousDirectory);
        }
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

        const bool passed = prefixesOnly && valueStable && focusCancel && escapeCancel
                && enterCommit && clickCommit && settingsTabsUsable && settingsShortcutsPersist
                && autosaveSettingsPersist && settingsApplySignal && autosaveLiveReconfigure
                && cameraSpeedSettingsPersist
                && sessionIdentityRoundTrip
                && renderOptionsRegistered
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
                && bodyPartSourceFiltering
                && bodyPartColumnSorting
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
            && programmaticModeRefreshesMaterials
            && uniqueMaterialPaletteCards
            && materialVariantsRemainDistinctAndGrouped
            && materialPaletteScrollsAndFillsWidth
            && landscapeToolsAreCompactAndPaletteFillsHeight
            && defaultAtlasSourceVisible
            && localAtlasSourceVisible
            && generatedAtlasCountVisible
            && expandedAtlasCapacityIsYellow
            && unstableAtlasCapacityIsRedButInformational
                && materialAtlasActionWorks
                && logicPointUndoAtomic && logicPointRedoAtomic;
        QTextStream output(stdout);
        output << "asset-picker prefixes=" << completions.join(",") << "\n"
               << "prefixesOnly=" << prefixesOnly
               << " valueStable=" << valueStable
               << " focusCancel=" << focusCancel
               << " escapeCancel=" << escapeCancel
               << " enterCommit=" << enterCommit
               << " clickCommit=" << clickCommit
               << " settingsTabsUsable=" << settingsTabsUsable
               << " settingsShortcutsPersist=" << settingsShortcutsPersist
               << " autosaveSettingsPersist=" << autosaveSettingsPersist
               << " autosaveLiveReconfigure=" << autosaveLiveReconfigure
               << " cameraSpeedSettingsPersist=" << cameraSpeedSettingsPersist
               << " sessionIdentityRoundTrip=" << sessionIdentityRoundTrip
               << " settingsApplySignal=" << settingsApplySignal
               << " renderOptionsRegistered=" << renderOptionsRegistered
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
               << " bodyPartSourceFiltering=" << bodyPartSourceFiltering
               << " bodyPartColumnSorting=" << bodyPartColumnSorting
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
               << " expandedAtlasCapacityIsYellow="
               << expandedAtlasCapacityIsYellow
               << " unstableAtlasCapacityIsRedButInformational="
               << unstableAtlasCapacityIsRedButInformational
               << " materialAtlasActionWorks=" << materialAtlasActionWorks
               << " logicPointUndoAtomic=" << logicPointUndoAtomic
               << " logicPointRedoAtomic=" << logicPointRedoAtomic << "\n"
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
    const char* openGlBackend = requestedOpenGlBackend(argc, argv);
    const bool renderBenchmark = renderBenchmarkRequested(argc, argv);
    if(std::strcmp(openGlBackend, "angle") == 0)
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    else if(std::strcmp(openGlBackend, "desktop") == 0)
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

    QApplication app(argc, argv);
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

    qApp->setStyle(new CEnduranceStyle(QStyleFactory::create("Fusion")));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53,53,53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25,25,25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
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
        "QToolTip { color: #ffffff; background-color: #07be07; border: 1px solid white; }"
        "QToolButton:pressed { background-color: #3c583d; }"
        "QWidget:disabled { color: #808080; }"
        "QMenu::item:disabled { color: #808080; }");


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
        });
    }
    return app.exec();
}

#include "main_window.h"
#include <QApplication>
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
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QUndoCommand>
#include <QUndoStack>
#include <QClipboard>
#include <QMimeData>
#include <QMenu>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QFormLayout>
#include <cstdio>
#include <cstring>

#include "endurance_document.h"
#include "log.h"
#include "resource_profile.h"
#include "resourcemanager.h"
#include "layout_components/tablemanager.h"
#include "window_lifecycle.h"
#include "settings.h"
#include "jmv_map_test_launcher.h"
#include "render_scheduling.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace {
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
                QKeySequenceEdit* activateOwnerShortcut = settingsWindow.findChild<QKeySequenceEdit*>("shortcutActivateOwnerMob");
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
                renderOptionsRegistered = animateUnits && animateUnits->value()
                        && performanceCounters && !performanceCounters->value()
                        && renderVsync && renderVsync->value()
                        && renderFpsCap && renderFpsCap->value() == 0
                        && settingsWindow.findChild<QCheckBox*>("renderVSync")
                        && settingsWindow.findChild<QLineEdit*>("renderFpsCap");
                if(historyShortcut && duplicateShortcut && stopTestShortcut && activateOwnerShortcut)
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
                            && stopTestShortcut->keySequence() == QKeySequence("Shift+Esc")
                            && activateOwnerShortcut->keySequence() == QKeySequence("Ctrl+T");
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
        ownedWindow.setAttribute(Qt::WA_DontShowOnScreen);
        orphanWindow.setAttribute(Qt::WA_DontShowOnScreen);
        ownedWindow.show();
        orphanWindow.show();
        ei::closeSecondaryWindows(&primaryWindow);
        const bool secondaryWindowsClosed = !ownedWindow.isVisible() && !orphanWindow.isVisible();

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

        const bool passed = prefixesOnly && valueStable && focusCancel && escapeCancel
                && enterCommit && clickCommit && settingsTabsUsable && settingsShortcutsPersist
                && autosaveSettingsPersist && settingsApplySignal && autosaveLiveReconfigure
                && cameraSpeedSettingsPersist
                && renderOptionsRegistered
                && secondaryWindowsClosed
                && pasteSingleAction && pasteUndoAtomic && pasteRedoAtomic
                && incompatiblePasteBlocked && compatiblePastePreview && compatiblePasteCommit
                && multiPropertyPaste && mismatchedPropertyPasteBlocked
                && fieldPastePreview && fieldPasteCommit && fieldCopyAvailable && nonTextPropertyPaste
                && numericComponentPaste && individualComponentSelection
                && selectedComponentCopy && componentPlatePaste && componentContextPaste
                && componentContextPreservesSelection && componentContextShowsOnlyComponent
                && componentContextGestureKeepsTableClear && componentContextUsesManualDispatch
                && componentSelectionTableExclusive && componentSelectionClearsForProperty
                && componentHitTargetsExpanded && componentLayoutHasNoDeadSpace
                && lockedNumericPasteBlocked
                && partialCopyExact && partialPasteExact && partialPasteCommit
                && propertyScrollPreserved
                && mixedModelPartsVisible
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
               << " settingsApplySignal=" << settingsApplySignal
               << " renderOptionsRegistered=" << renderOptionsRegistered
               << " secondaryWindowsClosed=" << secondaryWindowsClosed
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


    MainWindow window;
    window.setWindowTitle(QString("EI Mapper: Endurance Edition %1").arg(QCoreApplication::applicationVersion()));
    //window.resize(window.sizeHint());

    window.show();
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

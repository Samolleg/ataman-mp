#include "main_window.h"
#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <cstdio>
#include <cstring>

#include "endurance_document.h"
#include "log.h"
#include "resource_profile.h"
#include "resourcemanager.h"
#include "layout_components/tablemanager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <DbgHelp.h>
#endif

namespace {
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
    parser.addVersionOption();
    QCommandLineOption validateOption("validate", "Validate a MOB or MPR without changing it.", "file");
    QCommandLineOption roundtripOption("roundtrip-check", "Verify byte-identical unchanged serialization.", "file");
    QCommandLineOption jsonOption("json", "Emit machine-readable JSON.");
    QCommandLineOption profileLoadOption("profile-load", "Discover the recommended resource profile for a game installation.", "game-root");
    QCommandLineOption mobDiffOption("mob-diff", "Compare a baseline MOB with another MOB.", "baseline");
    QCommandLineOption uiWidgetCheckOption("ui-widget-check", "Run non-interactive asset-picker regression checks.");
    QCommandLineOption againstOption("against", "Candidate MOB used with --mob-diff.", "candidate");
    QCommandLineOption angleOpenGlOption("angle-opengl", "Use Qt's ANGLE/OpenGL ES implementation (diagnostic GUI mode).");
    QCommandLineOption desktopOpenGlOption("desktop-opengl", "Use the system desktop OpenGL implementation (diagnostic GUI mode).");
    parser.addOption(validateOption);
    parser.addOption(roundtripOption);
    parser.addOption(jsonOption);
    parser.addOption(profileLoadOption);
    parser.addOption(mobDiffOption);
    parser.addOption(uiWidgetCheckOption);
    parser.addOption(againstOption);
    parser.addOption(angleOpenGlOption);
    parser.addOption(desktopOpenGlOption);
    parser.addPositionalArgument("documents", "MPR and MOB documents to open in order.", "[documents...]");
    parser.process(app);

    const bool validate = parser.isSet(validateOption);
    const bool roundtrip = parser.isSet(roundtripOption);
    const bool profileLoad = parser.isSet(profileLoadOption);
    const bool mobDiff = parser.isSet(mobDiffOption);
    const bool uiWidgetCheck = parser.isSet(uiWidgetCheckOption);
    if(!validate && !roundtrip && !profileLoad && !mobDiff && !uiWidgetCheck)
        return -1;
    if((validate ? 1 : 0) + (roundtrip ? 1 : 0) + (profileLoad ? 1 : 0)
            + (mobDiff ? 1 : 0) + (uiWidgetCheck ? 1 : 0) > 1)
    {
        QTextStream(stderr) << "Choose one headless command.\n";
        return 2;
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
        const bool passed = prefixesOnly && valueStable && focusCancel && escapeCancel
                && enterCommit && clickCommit;
        QTextStream output(stdout);
        output << "asset-picker prefixes=" << completions.join(",") << "\n"
               << "prefixesOnly=" << prefixesOnly
               << " valueStable=" << valueStable
               << " focusCancel=" << focusCancel
               << " escapeCancel=" << escapeCancel
               << " enterCommit=" << enterCommit
               << " clickCommit=" << clickCommit << "\n"
               << (passed ? "PASS" : "FAIL")
               << ": prefix preview cancels on focus/Escape and commits on Enter/click\n";
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
    if(std::strcmp(openGlBackend, "angle") == 0)
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
    else if(std::strcmp(openGlBackend, "desktop") == 0)
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

    QApplication app(argc, argv);

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(enduranceUnhandledException);
#endif

    QCoreApplication::setOrganizationName("EI Mapper Endurance");
    QCoreApplication::setOrganizationDomain("ei-mapper-endurance.local");
    QCoreApplication::setApplicationName("EI Mapper Endurance Edition");
    // Version is independent from upstream EI Mapper releases.
    QCoreApplication::setApplicationVersion(ENDURANCE_VERSION);
    ei::log(eLogInfo, QString("Runtime version:%1 qt:%2 architecture:%3 OpenGL-request:%4")
            .arg(QCoreApplication::applicationVersion(), QT_VERSION_STR,
                 QString::number(sizeof(void*) * 8), QString::fromLatin1(openGlBackend)));

    const int commandResult = runHeadlessCommand(app);
    if(commandResult >= 0)
        return commandResult;

    qApp->setStyle(QStyleFactory::create("Fusion"));

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
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);

    qApp->setPalette(darkPalette);

    //qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
    qApp->setStyleSheet("QToolTip { color: #ffffff; background-color: #07be07; border: 1px solid white; }"); //background color (tooltips foreground also)
    qApp->setStyleSheet("QToolButton:pressed { background-color: #3c583d }"); //background color for pressed button


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

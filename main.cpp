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

#include "endurance_document.h"
#include "log.h"
#include "resource_profile.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_WIN
namespace {
LONG WINAPI enduranceUnhandledException(EXCEPTION_POINTERS* exceptionInfo)
{
    const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
            ? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
    const ULONG_PTR address = exceptionInfo && exceptionInfo->ExceptionRecord
            ? reinterpret_cast<ULONG_PTR>(exceptionInfo->ExceptionRecord->ExceptionAddress) : 0;
    const ULONG_PTR moduleBase = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    SYSTEMTIME now;
    GetLocalTime(&now);

    char record[3072];
    const int length = std::snprintf(record, sizeof(record),
            "[%02u.%02u.%04u %02u:%02u:%02u] {CRASH} code=0x%08lx address=0x%p module_offset=0x%llx thread=%lu context=%s\r\n",
            unsigned(now.wDay), unsigned(now.wMonth), unsigned(now.wYear),
            unsigned(now.wHour), unsigned(now.wMinute), unsigned(now.wSecond),
            static_cast<unsigned long>(code), reinterpret_cast<void*>(address),
            static_cast<unsigned long long>(address >= moduleBase ? address - moduleBase : 0),
            static_cast<unsigned long>(GetCurrentThreadId()), ei::crashContext());

    CreateDirectoryW(L"logs", nullptr);
    HANDLE logFile = CreateFileW(L"logs\\workflow.log", FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(logFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(logFile, record, DWORD(length > 0 ? length : 0), &written, nullptr);
        FlushFileBuffers(logFile);
        CloseHandle(logFile);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}
#endif

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
    QCommandLineOption againstOption("against", "Candidate MOB used with --mob-diff.", "candidate");
    parser.addOption(validateOption);
    parser.addOption(roundtripOption);
    parser.addOption(jsonOption);
    parser.addOption(profileLoadOption);
    parser.addOption(mobDiffOption);
    parser.addOption(againstOption);
    parser.addPositionalArgument("documents", "MPR and MOB documents to open in order.", "[documents...]");
    parser.process(app);

    const bool validate = parser.isSet(validateOption);
    const bool roundtrip = parser.isSet(roundtripOption);
    const bool profileLoad = parser.isSet(profileLoadOption);
    const bool mobDiff = parser.isSet(mobDiffOption);
    if(!validate && !roundtrip && !profileLoad && !mobDiff)
        return -1;
    if((validate ? 1 : 0) + (roundtrip ? 1 : 0) + (profileLoad ? 1 : 0) + (mobDiff ? 1 : 0) > 1)
    {
        QTextStream(stderr) << "Choose one headless command.\n";
        return 2;
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
    QApplication app(argc, argv);

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(enduranceUnhandledException);
#endif

    QCoreApplication::setOrganizationName("EI Mapper Endurance");
    QCoreApplication::setOrganizationDomain("ei-mapper-endurance.local");
    QCoreApplication::setApplicationName("EI Mapper Endurance Edition");
    // Version is independent from upstream EI Mapper releases.
    QCoreApplication::setApplicationVersion(ENDURANCE_VERSION);

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

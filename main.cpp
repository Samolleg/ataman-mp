#include "main_window.h"
#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

#include "endurance_document.h"
#include "log.h"

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
    parser.addOption(validateOption);
    parser.addOption(roundtripOption);
    parser.addOption(jsonOption);
    parser.addPositionalArgument("documents", "MPR and MOB documents to open in order.", "[documents...]");
    parser.process(app);

    const bool validate = parser.isSet(validateOption);
    const bool roundtrip = parser.isSet(roundtripOption);
    if(!validate && !roundtrip)
        return -1;
    if(validate && roundtrip)
    {
        QTextStream(stderr) << "Choose either --validate or --roundtrip-check.\n";
        return 2;
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

#include "main_window.h"
#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "endurance_document.h"

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
    return app.exec();
}

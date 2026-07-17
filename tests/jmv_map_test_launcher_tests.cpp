#include <QtTest>

#include "jmv_map_test_launcher.h"

class JmvMapTestLauncherTests : public QObject
{
    Q_OBJECT

private slots:
    void injectsIntoExistingWorldScript();
    void ignoresWorldScriptInStringsAndComments();
    void rejectsMalformedExistingWorldScript();
};

void JmvMapTestLauncherTests::injectsIntoExistingWorldScript()
{
    const QString source = "WorldScript(\nConsoleString(\"original\")\n)\n";
    QString output;
    QString error;
    QVERIFY2(CJmvMapTestLauncher::injectWorldScriptBootstrap(
                 source, "ConsoleString(\"test\")", &output, &error), qPrintable(error));
    QVERIFY(output.contains("WorldScript(\nConsoleString(\"test\")\n\nConsoleString(\"original\")"));
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

QTEST_APPLESS_MAIN(JmvMapTestLauncherTests)
#include "jmv_map_test_launcher_tests.moc"

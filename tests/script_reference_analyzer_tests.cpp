#include <QtTest>

#include "script_reference_analyzer.h"

class ScriptReferenceAnalyzerTests : public QObject
{
    Q_OBJECT

private slots:
    void findsStandaloneIds();
    void ignoresPartialNumbers();
};

void ScriptReferenceAnalyzerTests::findsStandaloneIds()
{
    const QString script =
        "local wolf = GetObjectByID(\"1000421\")\n"
        "-- keep object 1000421 alive\n"
        "SetCharacterParam(77, 1)\n";
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(script, {1000421, 77});
    QCOMPARE(references.size(), 3);
    QCOMPARE(references.at(0).objectId, uint(77));
    QCOMPARE(references.at(0).line, 3);
    QCOMPARE(references.at(1).objectId, uint(1000421));
    QCOMPARE(references.at(1).line, 1);
    QCOMPARE(references.at(2).line, 2);
}

void ScriptReferenceAnalyzerTests::ignoresPartialNumbers()
{
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds("10004210 x1000421y 21000421", {1000421});
    QCOMPARE(references.size(), 1);
    QCOMPARE(references.first().column, 11);
}

QTEST_APPLESS_MAIN(ScriptReferenceAnalyzerTests)
#include "script_reference_analyzer_tests.moc"

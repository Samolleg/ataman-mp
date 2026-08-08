#include <QtTest>

#include "script_reference_analyzer.h"

class ScriptReferenceAnalyzerTests : public QObject
{
    Q_OBJECT

private slots:
    void findsTypedObjectLookups();
    void ignoresPartialNumbers();
    void ignoresUnrelatedZeroValues();
    void ignoresCommentedAndQuotedLookups();
};

void ScriptReferenceAnalyzerTests::findsTypedObjectLookups()
{
    const QString script =
        "local wolf = GetObjectByID(\"1000421\")\n"
        "AddObject(group, GetObject(77))\n";
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(script, {1000421, 77});
    QCOMPARE(references.size(), 2);
    QCOMPARE(references.at(0).objectId, uint(77));
    QCOMPARE(references.at(0).line, 2);
    QCOMPARE(references.at(1).objectId, uint(1000421));
    QCOMPARE(references.at(1).line, 1);
}

void ScriptReferenceAnalyzerTests::ignoresPartialNumbers()
{
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(
            "GetObject(10004210) GetObjectByID(\"21000421\")", {1000421});
    QVERIFY(references.isEmpty());
}

void ScriptReferenceAnalyzerTests::ignoresUnrelatedZeroValues()
{
    const QString script =
        "SetWind(0, 0, 0)\n"
        "SetCP(GetObject(4515), 22, 140, 0)\n"
        "if IsEqual(flag, 0) then value = 0\n";
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(script, {0, 4515});
    QCOMPARE(references.size(), 1);
    QCOMPARE(references.first().objectId, uint(4515));
    QCOMPARE(references.first().line, 2);
}

void ScriptReferenceAnalyzerTests::ignoresCommentedAndQuotedLookups()
{
    const QString script =
        "// GetObject(77)\n"
        "/* GetObjectByID(\"77\") */\n"
        "message = \"GetObject(77)\"\n"
        "live = GetObjectByID('77')\n";
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(script, {77});
    QCOMPARE(references.size(), 1);
    QCOMPARE(references.first().line, 4);
}

QTEST_APPLESS_MAIN(ScriptReferenceAnalyzerTests)
#include "script_reference_analyzer_tests.moc"

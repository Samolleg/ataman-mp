#include <QtTest>
#include "scenario_analysis.h"

using namespace scenario;

class ScenarioLayerTests : public QObject
{
    Q_OBJECT
    Request fixture(const QString& text) {
        Request r;
        Source s; s.key = "map.mob"; s.text = text;
        Object a; a.key = "object/7"; a.id = 7; a.name = "Guard";
        a.position = QVector3D(1,2,3); a.hasPosition = true;
        Object b = a; b.key = "object/8"; b.id = 8; b.name = "Lever";
        s.objects = {a,b}; r.sources = {s}; r.activeSource = s.key;
        r.familyRoots = QStringList{s.key}; r.prototypes.insert("fixture.guard");
        return r;
    }
    QVector<Reference> calls(const Result& r, const QString& name) {
        QVector<Reference> out;
        for(const auto& ref : r.references) if(ref.command == name) out.append(ref);
        return out;
    }
private slots:
    void originalScenario() {
        auto r = fixture("DeclareScript Gate(this : object)\nScript Gate(\n"
                         " if(IsAlive(GetObject(7))) then(\n"
                         " AddUnitToServer(\"visitor\",\"fixture.guard\",\"Visitor\",12,14,2)\n"
                         " SetCP(GetObjectByName(\"visitor\"),18,20,3)\n"
                         " SwitchLeverState(GetObjectByID(\"8\"),1)\n"
                         " MoveToPoint(this,GetX(GetLeader()),9)\n))");
        const auto result = analyze(r);
        QCOMPARE(result.markers.size(), 2);
        QCOMPARE(result.markers[0].position, QVector3D(12,14,2));
        QCOMPARE(result.markers[1].position, QVector3D(18,20,3));
        auto uses = calls(result, "SwitchLeverState");
        QCOMPARE(uses.size(), 1);
        QCOMPARE(uses[0].function, QString("Gate"));
        QCOMPARE(uses[0].line, 6);
        QVERIFY(uses[0].conditions.join(" ").contains("IsAlive"));
        QCOMPARE(uses[0].targets[0].resolution, Resolution::Local);
        QCOMPARE(calls(result,"MoveToPoint")[0].targets[0].resolution, Resolution::Dynamic);
        QVERIFY(!calls(result,"MoveToPoint")[0].hasDestination);
    }
    void excludesProseAndComments() {
        auto r = fixture("// GetObject(7)\n/* SetCP(GetObject(8),1,2,3) */\n"
                         "ConsoleString(\"GetObject(7) \\\"prose\\\"\")\n"
                         "SetWind(7,8,0)\nGetObject /* trivia */ (8)\n");
        const auto result = analyze(r);
        QCOMPARE(result.references.size(), 1);
        QCOMPARE(result.references[0].line, 5);
        QCOMPARE(result.references[0].targets[0].objectKey, QString("object/8"));
    }
    void lineFidelityAndDraftErrors() {
        auto r = fixture(QString::fromUtf8("// Привет\r\nWorldScript(\r  GetObject(7)\nSetCP(GetObject(8),1,"));
        const auto result = analyze(r);
        auto lookup = calls(result,"GetObject");
        QCOMPARE(lookup[0].line, 3);
        QCOMPARE(lookup[0].column, 3);
        QCOMPARE(r.sources[0].text.mid(lookup[0].offset,9), QString("GetObject"));
        QVERIFY(!result.diagnostics.isEmpty());
        QVERIFY(result.markers.isEmpty());
    }
    void unknownPrototypesAndCoordinates() {
        auto r = fixture("WorldScript("
                         "AddUnitToServer(\"x\",\"missing\",\"X\",1,2,3) "
                         "AddUnitToServer(\"y\",\"fixture.guard\",\"Y\",GetX(GetLeader()),2,3) "
                         "SetCP(GetObject(7),1,2,1e999) "
                         "MoveToPoint(GetObject(8),10,11))");
        const auto result = analyze(r);
        QCOMPARE(result.markers.size(),1);
        QVERIFY(!result.markers[0].hasZ);
        QCOMPARE(result.markers[0].position.x(),10.f);
        QVERIFY(!calls(result,"AddUnitToServer")[0].status.isEmpty());
    }
    void duplicateIdsAndZero() {
        auto r = fixture("WorldScript(SetCP(GetObject(7),1,2,3) GetObject(0) GetObject(16777217))");
        auto duplicate = r.sources[0].objects[0]; duplicate.key = "another";
        r.sources[0].objects.append(duplicate);
        auto result = analyze(r);
        QCOMPARE(calls(result,"SetCP")[0].targets[0].resolution,Resolution::Ambiguous);
        QVERIFY(result.markers.isEmpty());
        const auto lookups = calls(result,"GetObject");
        QCOMPARE(lookups[1].targets[0].resolution,Resolution::Invalid);
        QCOMPARE(lookups[2].targets[0].resolution,Resolution::Invalid);
    }
    void familyAndExternalScopes() {
        auto r = fixture("WorldScript(AddMob(\"quest.mob\") GetObject(9) GetObject(10))");
        Source quest; quest.key = "quest.mob";
        Object a; a.key="q9"; a.id=9; quest.objects={a};
        Source unrelated; unrelated.key="other.mob"; a.key="o10"; a.id=10; unrelated.objects={a};
        r.sources << quest << unrelated;
        auto result = analyze(r);
        const auto lookups = calls(result,"GetObject");
        QCOMPARE(lookups[0].targets[0].resolution,Resolution::Family);
        QCOMPARE(lookups[1].targets[0].resolution,Resolution::External);
        r.sources[0].text = "WorldScript(GetObject(9))"; // un-applied AddMob removal
        QCOMPARE(calls(analyze(r),"GetObject")[0].targets[0].resolution,Resolution::External);
        r.sources[0].text = "WorldScript(AddMob(variable) SetCP(GetObject(7),1,2,3))";
        result = analyze(r);
        QVERIFY(!result.familyComplete);
        QVERIFY(result.markers.isEmpty());
    }
    void dependencyNamesWithoutExtension() {
        auto r = fixture("WorldScript(AddMob(\"QUEST\") GetObject(9))");
        Source quest; quest.key="quest.mob";
        quest.text="WorldScript(AddMob(\"nested.mob\") SetCP(GetObject(7),4,5,6))";
        Source nested; nested.key="nested.mob";
        Object a; a.key="nested/9"; a.id=9; nested.objects={a};
        nested.text="WorldScript(SwitchLeverState(GetObject(8),1))";
        r.sources << quest << nested;
        const auto result=analyze(r);
        QVERIFY(result.familyComplete);
        QCOMPARE(result.family.size(),3);
        QCOMPARE(calls(result,"GetObject")[0].targets[0].resolution,Resolution::Family);
        QCOMPARE(result.markers.size(),1);
        QCOMPARE(result.markers[0].position,QVector3D(4,5,6));
    }
    void familyCollisionAndDuplicateSpawn() {
        auto r = fixture("WorldScript(AddMob(\"quest.mob\") GetObject(7) "
                         "AddUnitToServer(\"x\",\"fixture.guard\",\"X\",1,2,3) "
                         "AddUnitToServer(\"X\",\"fixture.guard\",\"X\",4,5,6) "
                         "SetCP(GetObjectByName(\"x\"),7,8,9))");
        auto quest = r.sources[0]; quest.key="quest.mob"; quest.text.clear();
        r.sources.append(quest);
        auto result = analyze(r);
        QCOMPARE(calls(result,"GetObject")[0].targets[0].resolution,Resolution::Ambiguous);
        QCOMPARE(calls(result,"SetCP")[0].targets[0].resolution,Resolution::Ambiguous);
        QVERIFY(result.markers.isEmpty());
    }
    void conditionalAndNestedBlocks() {
        auto r=fixture("Script #Gate#1(if(IsAlive(GetObject(7))) then("
                       "if(IsDead(GetObject(8))) then(SwitchLeverState(GetObject(8),1)))"
                       "if() then(EnableLever(GetObject(8),0)))");
        auto result=analyze(r);
        QCOMPARE(calls(result,"SwitchLeverState")[0].conditions.size(),2);
        QCOMPARE(calls(result,"EnableLever")[0].conditions.size(),1);
        QCOMPARE(calls(result,"SwitchLeverState")[0].function,QString("#Gate#1"));
    }
    void arityAndGroupAreNotCreation() {
        auto r=fixture("WorldScript(AddObject(group,GetObject(7)) "
                       "SetCP(GetObject(7),1,2) AddUnitToServer(\"a\",\"fixture.guard\",1,2,3))");
        const auto result=analyze(r);
        QVERIFY(result.markers.isEmpty());
        QCOMPARE(calls(result,"AddObject")[0].kind,Kind::Group);
        QVERIFY(!calls(result,"SetCP")[0].status.isEmpty());
    }
    void immutableSnapshotsAndRefresh() {
        auto r=fixture("WorldScript(SetCP(GetObject(7),1,2,3))");
        const auto text=r.sources[0].text;
        auto a=analyze(r); auto b=analyze(r);
        QCOMPARE(a.references.size(),b.references.size());
        QCOMPARE(r.sources[0].text,text);
        QCOMPARE(r.sources[0].objects[0].position,QVector3D(1,2,3));
        r.sources[0].objects.removeFirst();
        QVERIFY(analyze(r).markers.isEmpty());
        r.sources.clear();
        QVERIFY(analyze(r).references.isEmpty());
    }
    void malformedBlocksAndEscapesStayUnresolved() {
        auto r=fixture("WorldScript(SetCP(GetObject(7),1,2,3)");
        QVERIFY(analyze(r).markers.isEmpty());
        r.sources[0].text="WorldScript(GetObjectByID(\"\\7\"))";
        QCOMPARE(calls(analyze(r),"GetObjectByID")[0].targets[0].resolution,Resolution::Dynamic);
    }
    void loopConditionsAndDuplicateDependencyNames() {
        auto r=fixture("Script Visit(ForIf(x,group,IsAlive(x))(SetCP(GetObject(7),1,2,3)))");
        QVERIFY(calls(analyze(r),"SetCP")[0].conditions[0].contains("ForIf"));
        r.sources[0].text="WorldScript(AddMob(\"quest.mob\") SetCP(GetObject(7),1,2,3))";
        Source a; a.key="one/quest.mob"; Source b; b.key="two/quest.mob"; r.sources << a << b;
        const auto result=analyze(r);
        QVERIFY(!result.familyComplete); QVERIFY(result.markers.isEmpty());
    }
};
QTEST_APPLESS_MAIN(ScenarioLayerTests)
#include "scenario_layer_tests.moc"

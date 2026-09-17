#include <QtTest>
#include <limits>
#include "world_coordinate_edit.h"
class WorldCoordinateEditTests : public QObject {
    Q_OBJECT
private slots:
    void preservesUneditedCoordinates() {
        for(double original : {0.125, -0.0000000123456789, 1e20,
                               std::numeric_limits<double>::max()}) {
            CWorldCoordinateEdit edit(original);
            double actual = 0;
            QVERIFY(edit.coordinate(&actual)); QCOMPARE(actual,original);
        }
    }
    void acceptsIntentionalEditAndRejectsInvalidInput() {
        CWorldCoordinateEdit edit(0.125); double result;
        edit.setText("0.13"); QVERIFY(edit.coordinate(&result)); QCOMPARE(result,0.13);
        for(const QString& invalid : {QString(),QString("NaN"),QString("inf"),
                                     QString("1e999"),QString("1,000"),QString("oops")}) {
            edit.setText(invalid); QVERIFY(!edit.coordinate(&result));
        }
    }
};
QTEST_MAIN(WorldCoordinateEditTests)
#include "world_coordinate_edit_tests.moc"

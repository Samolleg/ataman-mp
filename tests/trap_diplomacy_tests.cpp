#include <QtTest>

#include "trap_diplomacy.h"

class TrapDiplomacyTests : public QObject
{
    Q_OBJECT

private slots:
    void derivesWarTargetsFromOwningPlayerRow();
    void storedMaskCannotChangeEffectiveTargets();
    void reportsMissingAndInvalidDiplomacyClearly();
};

namespace {

QVector<QVector<uint>> neutralTable()
{
    return QVector<QVector<uint>>(32, QVector<uint>(32, 0));
}

QStringList playerNames()
{
    QStringList names;
    for(int index = 0; index < 32; ++index)
        names.append(QString("Player-%1").arg(index));
    names[4] = "Guards";
    names[12] = "Orcs";
    return names;
}

}

void TrapDiplomacyTests::derivesWarTargetsFromOwningPlayerRow()
{
    QVector<QVector<uint>> table = neutralTable();
    table[3][4] = 2;
    table[3][12] = 2;
    table[3][7] = 1;

    const ei::STrapTargetSummary summary = ei::deriveTrapTargetSummary(
        3, table, playerNames());

    QVERIFY(summary.available);
    QCOMPARE(summary.playerGroup, 3);
    QCOMPARE(summary.mask, quint32((1u << 4) | (1u << 12)));
    QCOMPARE(summary.groups, QVector<int>({4, 12}));
    QCOMPARE(summary.labels, QStringList({"4 (Guards)", "12 (Orcs)"}));
    QCOMPARE(summary.display,
             QString("Player group 3 → 4 (Guards), 12 (Orcs)"));
}

void TrapDiplomacyTests::storedMaskCannotChangeEffectiveTargets()
{
    QVector<QVector<uint>> table = neutralTable();
    table[2][1] = 2;

    const ei::STrapTargetSummary fromZero = ei::deriveTrapTargetSummary(
        2, table, playerNames(), 0x00000000u);
    const ei::STrapTargetSummary fromAll = ei::deriveTrapTargetSummary(
        2, table, playerNames(), 0xffffffffu);

    QCOMPARE(fromZero.mask, quint32(1u << 1));
    QCOMPARE(fromAll.mask, fromZero.mask);
    QCOMPARE(fromAll.display, fromZero.display);
}

void TrapDiplomacyTests::reportsMissingAndInvalidDiplomacyClearly()
{
    const ei::STrapTargetSummary missing = ei::deriveTrapTargetSummary(
        2, {}, {}, 0xffffffffu);
    QVERIFY(!missing.available);
    QCOMPARE(missing.display,
             QString("Unavailable — no complete diplomacy row for Player group 2"));

    const ei::STrapTargetSummary invalid = ei::deriveTrapTargetSummary(
        32, neutralTable(), playerNames());
    QVERIFY(!invalid.available);
    QCOMPARE(invalid.display, QString("Unavailable — invalid Player group 32"));
}

QTEST_APPLESS_MAIN(TrapDiplomacyTests)
#include "trap_diplomacy_tests.moc"

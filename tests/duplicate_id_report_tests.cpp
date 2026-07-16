#include <QtTest>

#include "../duplicate_id_report.h"

class DuplicateIdReportTests : public QObject
{
    Q_OBJECT

private slots:
    void groupsRepeatedOccurrencesById();
    void limitsPreviewToTenUniqueIds();
};

void DuplicateIdReportTests::groupsRepeatedOccurrencesById()
{
    QVector<uint> duplicates;
    for(int i = 0; i < 13; ++i)
        duplicates.append(1000100);

    const QString text = ei::duplicateIdWarningText(duplicates);
    QVERIFY(text.contains("showing 1 of 1"));
    QVERIFY(text.contains("ID 1000100: 13 duplicate occurrences"));
    QCOMPARE(text.count("ID 1000100"), 1);
    QVERIFY(text.contains("logs\\workflow.log"));
}

void DuplicateIdReportTests::limitsPreviewToTenUniqueIds()
{
    QVector<uint> duplicates;
    for(uint id = 1; id <= 12; ++id)
        duplicates.append(id);

    const QString text = ei::duplicateIdWarningText(duplicates);
    QVERIFY(text.contains("showing 10 of 12"));
    QVERIFY(text.contains("ID 10: 1 duplicate occurrence"));
    QVERIFY(!text.contains("ID 11:"));
    QVERIFY(text.contains("and 2 more duplicate IDs"));
}

QTEST_APPLESS_MAIN(DuplicateIdReportTests)

#include "duplicate_id_report_tests.moc"

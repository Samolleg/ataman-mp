#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "settings_persistence.h"

class SettingsPersistenceTests : public QObject
{
    Q_OBJECT
private slots:
    void writesCompleteReplacement();
    void failedReplacementPreservesExistingFile();
};

void SettingsPersistenceTests::writesCompleteReplacement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("endurance-options.json");
    QString diagnostic;
    QVERIFY2(ei::writeSettingsFileAtomically(path, QByteArray("{\"Version\":\"1\"}\n"),
                                             &diagnostic), qPrintable(diagnostic));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), QByteArray("{\"Version\":\"1\"}\n"));
}

void SettingsPersistenceTests::failedReplacementPreservesExistingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("endurance-options.json");
    QFile original(path);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("original-settings"), qint64(17));
    original.close();
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser));

    QString diagnostic;
    QVERIFY(!ei::writeSettingsFileAtomically(path, QByteArray("replacement"), &diagnostic));
    QVERIFY(!diagnostic.isEmpty());
    QFile preserved(path);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray("original-settings"));
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ReadUser | QFileDevice::WriteUser);
}

QTEST_APPLESS_MAIN(SettingsPersistenceTests)
#include "settings_persistence_tests.moc"

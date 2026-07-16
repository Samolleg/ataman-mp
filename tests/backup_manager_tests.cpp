#include <QtTest>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "backup_manager.h"

class BackupManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void openSnapshotIsHashAddressedAndDeduplicated();
    void preSaveSnapshotsAreTimestampedAndExact();
    void retentionKeepsNewestFiftyPreSaveRevisions();
    void failureIsReportedWithoutChangingSource();
};

void BackupManagerTests::openSnapshotIsHashAddressedAndDeduplicated()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("zone1.mob");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("baseline-data"), qint64(13));
    source.close();

    CBackupManager manager(temporary.filePath("backups"));
    const SBackupResult first = manager.snapshotOnOpen(QFileInfo(sourcePath));
    QVERIFY2(first.success, qPrintable(first.error));
    QVERIFY(!first.skipped);
    QCOMPARE(QFileInfo(first.backupPath).suffix(), QString("backup"));
    QFile backup(first.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), QByteArray("baseline-data"));
    QVERIFY(QFileInfo::exists(first.backupPath + ".json"));

    const SBackupResult second = manager.snapshotOnOpen(QFileInfo(sourcePath));
    QVERIFY(second.success);
    QVERIFY(second.skipped);
    QCOMPARE(second.backupPath, first.backupPath);

    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(source.write("changed-data"), qint64(12));
    source.close();
    const SBackupResult changed = manager.snapshotOnOpen(QFileInfo(sourcePath));
    QVERIFY(changed.success);
    QVERIFY(!changed.skipped);
    QVERIFY(changed.backupPath != first.backupPath);
}

void BackupManagerTests::preSaveSnapshotsAreTimestampedAndExact()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("zone.mpr");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("mpr-before-save"), qint64(15));
    source.close();

    CBackupManager manager(temporary.filePath("backups"));
    const SBackupResult first = manager.snapshotBeforeSave(QFileInfo(sourcePath));
    const SBackupResult second = manager.snapshotBeforeSave(QFileInfo(sourcePath));
    QVERIFY2(first.success, qPrintable(first.error));
    QVERIFY2(second.success, qPrintable(second.error));
    QVERIFY(first.backupPath != second.backupPath);
    QVERIFY(first.backupPath.contains("pre-save"));
    QFile backup(first.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), QByteArray("mpr-before-save"));

    QFile metadata(first.backupPath + ".json");
    QVERIFY(metadata.open(QIODevice::ReadOnly));
    const QJsonObject json = QJsonDocument::fromJson(metadata.readAll()).object();
    QCOMPARE(json.value("event").toString(), QString("pre-save"));
    QCOMPARE(json.value("sourcePath").toString(), QFileInfo(sourcePath).absoluteFilePath());
    QCOMPARE(json.value("sourceSha256").toString(), first.sha256);
}

void BackupManagerTests::failureIsReportedWithoutChangingSource()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("source.mob");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("protected"), qint64(9));
    source.close();

    const QString invalidRoot = temporary.filePath("not-a-directory");
    QFile blocker(invalidRoot);
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    CBackupManager manager(invalidRoot);
    const SBackupResult result = manager.snapshotBeforeSave(QFileInfo(sourcePath));
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), QByteArray("protected"));
}

void BackupManagerTests::retentionKeepsNewestFiftyPreSaveRevisions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("zone.mob");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("revision"), qint64(8));
    source.close();

    CBackupManager manager(temporary.filePath("backups"));
    const SBackupResult openSnapshot = manager.snapshotOnOpen(QFileInfo(sourcePath));
    QVERIFY(openSnapshot.success);
    for(int index = 0; index < 52; ++index)
        QVERIFY2(manager.snapshotBeforeSave(QFileInfo(sourcePath)).success, "pre-save snapshot failed");

    QDir backupDirectory(QFileInfo(openSnapshot.backupPath).absolutePath());
    QCOMPARE(backupDirectory.entryList(QStringList() << "*-pre-save-*.backup", QDir::Files).size(), 50);
    QVERIFY(QFileInfo::exists(openSnapshot.backupPath));
}

QTEST_MAIN(BackupManagerTests)
#include "backup_manager_tests.moc"

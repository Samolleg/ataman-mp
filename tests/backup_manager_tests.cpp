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
    void autosavesAreVersionedAndDeduplicated();
    void uncleanSessionNotifiesEachRecoveryOnlyOnce();
    void cleanSessionDoesNotNotifyAndKeepsRecovery();
    void pairedMprAutosavesAreCompleteAndVerified();
    void pairedMprRetentionRemovesWholeGenerations();
};

void BackupManagerTests::pairedMprAutosavesAreCompleteAndVerified()
{
    QTemporaryDir temporary;
    const QString sourcePath = temporary.filePath("zone.mpr");
    QFile source(sourcePath); QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("baseline"); source.close();
    CBackupManager manager(temporary.filePath("backups"));
    QVector<SRecoverableAutosave> recoveries; QString error;
    QVERIFY(manager.beginEditorSession(&recoveries, &error));
    const auto stale=manager.snapshotAutosave(QFileInfo(sourcePath),"changed","atlas-one",QString(64,'0'));
    QVERIFY(!stale.success);
    QCOMPARE(QDir(manager.rootPath()).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size(),0);
    const auto first = manager.snapshotAutosave(QFileInfo(sourcePath), "changed", "atlas-one");
    QVERIFY2(first.success, qPrintable(first.error));
    QVERIFY(CBackupManager::validateAutosave(QFileInfo(first.backupPath), &error));
    QCOMPARE(manager.snapshotAutosave(QFileInfo(sourcePath), "changed", "atlas-one").skipped, true);
    const QString copyDirectory=temporary.filePath("copied"); QVERIFY(QDir().mkpath(copyDirectory));
    const QString copied=QDir(copyDirectory).filePath(QFileInfo(first.backupPath).fileName());
    QVERIFY(QFile::copy(first.backupPath,copied));
    QVERIFY(QFile::copy(first.backupPath+".json",copied+".json"));
    const QString originalAtlas=QFileInfo(first.backupPath).dir().filePath(QFileInfo(first.backupPath).completeBaseName()+"-atlas.res");
    QVERIFY(QFile::copy(originalAtlas,QDir(copyDirectory).filePath(QFileInfo(originalAtlas).fileName())));
    QVERIFY(CBackupManager::validateAutosave(QFileInfo(copied)));
    QVERIFY(QFile::remove(copied+".json")); QVERIFY(!CBackupManager::validateAutosave(QFileInfo(copied)));

    QVERIFY(manager.beginEditorSession(&recoveries, &error));
    QCOMPARE(recoveries.size(), 1);
    const QString atlasPath = recoveries.first().terrainAtlasPath;
    QCOMPARE(atlasPath, QFileInfo(first.backupPath).dir().filePath(
        QFileInfo(first.backupPath).completeBaseName() + "-atlas.res"));
    QFile atlas(atlasPath); QVERIFY(atlas.open(QIODevice::ReadOnly));
    QCOMPARE(atlas.readAll(), QByteArray("atlas-one")); atlas.close();
    QVERIFY(atlas.open(QIODevice::WriteOnly)); atlas.write("corrupt"); atlas.close();
    QVERIFY(!CBackupManager::validateAutosave(QFileInfo(first.backupPath), &error));
    QVERIFY(manager.beginEditorSession(&recoveries, &error)); QVERIFY(recoveries.isEmpty());
    // A damaged latest generation must not prevent a fresh exact retry.
    const auto retry = manager.snapshotAutosave(QFileInfo(sourcePath), "changed", "atlas-one");
    QVERIFY(retry.success); QVERIFY(!retry.skipped);
    QVERIFY(manager.beginEditorSession(&recoveries, &error)); QCOMPARE(recoveries.size(), 1);
    QVERIFY(QFile::remove(recoveries.first().terrainAtlasPath));
    QVERIFY(!CBackupManager::validateAutosave(QFileInfo(retry.backupPath)));
    const auto second = manager.snapshotAutosave(QFileInfo(sourcePath), "changed", "atlas-two");
    QVERIFY(second.success); QVERIFY(!second.skipped);
    // Atlas-only changes still need recovery even when MPR bytes match disk.
    const auto atlasOnly = manager.snapshotAutosave(QFileInfo(sourcePath), "baseline", "atlas-three");
    QVERIFY(atlasOnly.success); QVERIFY(!atlasOnly.skipped);
    QFile damaged(second.backupPath); QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("wrong"); damaged.close();
    QVERIFY(!CBackupManager::validateAutosave(QFileInfo(second.backupPath)));
    QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("baseline"));
}

void BackupManagerTests::pairedMprRetentionRemovesWholeGenerations()
{
    QTemporaryDir temporary;
    const QString sourcePath = temporary.filePath("zone.mpr");
    QFile source(sourcePath); QVERIFY(source.open(QIODevice::WriteOnly)); source.write("baseline"); source.close();
    CBackupManager manager(temporary.filePath("backups"));
    for(int i = 0; i < 23; ++i)
    {
        const auto result = manager.snapshotAutosave(QFileInfo(sourcePath), "mpr" + QByteArray::number(i), "res" + QByteArray::number(i));
        QVERIFY2(result.success, qPrintable(result.error));
        QVERIFY(CBackupManager::validateAutosave(QFileInfo(result.backupPath)));
    }
    QDir root(manager.rootPath()); const auto dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(dirs.size(), 1); QDir generations(root.filePath(dirs.first()));
    QCOMPARE(generations.entryList(QStringList() << "*.mpr", QDir::Files).size(), 20);
    QCOMPARE(generations.entryList(QStringList() << "*.res", QDir::Files).size(), 20);
    QCOMPARE(generations.entryList(QStringList() << "*.json", QDir::Files).size(), 20);
}

void BackupManagerTests::autosavesAreVersionedAndDeduplicated()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("zone.mob");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("baseline"), qint64(8));
    source.close();

    CBackupManager manager(temporary.filePath("backups"));
    const SBackupResult first = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("candidate-one"));
    QVERIFY2(first.success, qPrintable(first.error));
    QVERIFY(!first.skipped);
    QVERIFY(QFileInfo::exists(first.backupPath));
    QVERIFY(QFileInfo::exists(first.backupPath + ".json"));
    QFile autosave(first.backupPath);
    QVERIFY(autosave.open(QIODevice::ReadOnly));
    QCOMPARE(autosave.readAll(), QByteArray("candidate-one"));

    const SBackupResult duplicate = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("candidate-one"));
    QVERIFY(duplicate.success);
    QVERIFY(duplicate.skipped);
    QCOMPARE(duplicate.backupPath, first.backupPath);

    const SBackupResult second = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("candidate-two"));
    QVERIFY2(second.success, qPrintable(second.error));
    QVERIFY(!second.skipped);
    QVERIFY(second.backupPath != first.backupPath);

    const SBackupResult clean = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("baseline"));
    QVERIFY(clean.success);
    QVERIFY(clean.skipped);
    QVERIFY(clean.backupPath.isEmpty());
}

void BackupManagerTests::uncleanSessionNotifiesEachRecoveryOnlyOnce()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("zone.mob");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("baseline"), qint64(8));
    source.close();

    CBackupManager manager(temporary.filePath("backups"));
    QVector<SRecoverableAutosave> recoveries;
    QString error;
    QVERIFY2(manager.beginEditorSession(&recoveries, &error), qPrintable(error));
    QVERIFY(recoveries.isEmpty());
    const SBackupResult first = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("candidate-one"));
    QVERIFY2(first.success, qPrintable(first.error));

    // Starting again without finishing simulates a forced/crashed shutdown.
    QVERIFY2(manager.beginEditorSession(&recoveries, &error), qPrintable(error));
    QCOMPARE(recoveries.size(), 1);
    QCOMPARE(recoveries.first().autosavePath, first.backupPath);
    QVERIFY(!recoveries.first().notificationKey.isEmpty());
    QVERIFY2(manager.acknowledgeRecoveries(
        QStringList() << recoveries.first().notificationKey, &error), qPrintable(error));

    // Another crash without another generation must not repeat the warning.
    QVERIFY2(manager.beginEditorSession(&recoveries, &error), qPrintable(error));
    QVERIFY(recoveries.isEmpty());
    const SBackupResult second = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("candidate-two"));
    QVERIFY2(second.success, qPrintable(second.error));
    QVERIFY2(manager.beginEditorSession(&recoveries, &error), qPrintable(error));
    QCOMPARE(recoveries.size(), 1);
    QCOMPARE(recoveries.first().autosavePath, second.backupPath);
    QVERIFY(QFileInfo::exists(first.backupPath));
    QVERIFY(QFileInfo::exists(second.backupPath));
}

void BackupManagerTests::cleanSessionDoesNotNotifyAndKeepsRecovery()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath("zone.mob");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("baseline"), qint64(8));
    source.close();

    CBackupManager manager(temporary.filePath("backups"));
    QVector<SRecoverableAutosave> recoveries;
    QString error;
    QVERIFY2(manager.beginEditorSession(&recoveries, &error), qPrintable(error));
    const SBackupResult autosave = manager.snapshotAutosave(
        QFileInfo(sourcePath), QByteArray("unsaved-editor-state"));
    QVERIFY2(autosave.success, qPrintable(autosave.error));
    QVERIFY2(manager.finishEditorSession(&error), qPrintable(error));

    QVERIFY2(manager.beginEditorSession(&recoveries, &error), qPrintable(error));
    QVERIFY(recoveries.isEmpty());
    QVERIFY(QFileInfo::exists(autosave.backupPath));
    QVERIFY(QFileInfo::exists(autosave.backupPath + ".json"));
}

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

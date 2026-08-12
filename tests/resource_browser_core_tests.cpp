#include <QtTest>

#include "res_archive_index.h"
#include "resource_catalog.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QElapsedTimer>
#include <QTemporaryDir>

namespace {
QByteArray archiveBytes(const QVector<QPair<QByteArray, QByteArray>>& input)
{
    struct Member { QByteArray name; QByteArray payload; quint32 offset{0}; };
    QVector<Member> members;
    QByteArray bytes(16, '\0');
    QByteArray names;
    for(const auto& value : input)
    {
        Member member;
        member.name = value.first;
        member.payload = value.second;
        bytes.append(QByteArray((16 - bytes.size() % 16) % 16, '\0'));
        member.offset = quint32(bytes.size());
        bytes.append(member.payload);
        members.append(member);
    }
    bytes.append(QByteArray((16 - bytes.size() % 16) % 16, '\0'));
    const quint32 tableOffset = quint32(bytes.size());
    QByteArray tableBytes;
    QDataStream table(&tableBytes, QIODevice::WriteOnly);
    table.setByteOrder(QDataStream::LittleEndian);
    for(int index = 0; index < members.size(); ++index)
    {
        const Member& member = members.at(index);
        table << quint32(-1) << quint32(member.payload.size()) << member.offset
              << quint32(0x12340000 + index) << quint16(member.name.size())
              << quint32(names.size());
        names.append(member.name);
    }
    bytes.append(tableBytes);
    bytes.append(names);
    QDataStream header(&bytes, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(0x019CE23C) << quint32(members.size()) << tableOffset
           << quint32(names.size());
    return bytes;
}

QString writeArchive(QTemporaryDir& directory, const QString& name,
                     const QVector<QPair<QByteArray, QByteArray>>& members)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(archiveBytes(members)) < 0)
        return QString();
    return path;
}

QByteArray sha(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
}

class ResourceBrowserCoreTests : public QObject
{
    Q_OBJECT
private slots:
    void fileIndexIsMetadataOnlyAndReadsLazily();
    void sparse765MiBArchiveIndexesWithoutPayloadRead();
    void staleArchiveRejectsLazyRead();
    void catalogPreservesRuntimePrecedenceAndOverrideChain();
    void transactionSupportsReplaceAddRenameDelete();
    void transactionRejectsCaseAndCp1251Collisions();
    void representativeArchivesIndexReadOnly();
};

void ResourceBrowserCoreTests::fileIndexIsMetadataOnlyAndReadsLazily()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray large(8 * 1024 * 1024, 'x');
    const QString path = writeArchive(directory, "large.res",
        {{"large.bin", large}, {"note.txt", "hello"}});
    QVERIFY(!path.isEmpty());

    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    QCOMPARE(index.memberCount(), 2);
    QCOMPARE(index.retainedPayloadBytes(), qint64(0));
    QCOMPARE(index.member(0).dataSize, quint32(large.size()));
    QCOMPARE(index.readMemberPrefix(0, 32, &error), QByteArray(32, 'x'));
    QCOMPARE(index.retainedPayloadBytes(), qint64(0));
    QCOMPARE(index.memberSha256(1, &error), sha("hello"));
}

void ResourceBrowserCoreTests::sparse765MiBArchiveIndexesWithoutPayloadRead()
{
    QTemporaryDir directory;
    const QString path = directory.filePath("jmv-sized.res");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const quint32 payloadOffset = 16;
    const quint32 payloadSize = 765u * 1024u * 1024u - 64u;
    const quint32 tableOffset = payloadOffset + payloadSize;
    QByteArray header(16, '\0');
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setByteOrder(QDataStream::LittleEndian);
    headerStream << quint32(0x019CE23C) << quint32(1) << tableOffset << quint32(8);
    QCOMPARE(file.write(header), qint64(16));
    QVERIFY(file.seek(tableOffset));
    QByteArray table;
    QDataStream tableStream(&table, QIODevice::WriteOnly);
    tableStream.setByteOrder(QDataStream::LittleEndian);
    tableStream << quint32(-1) << payloadSize << payloadOffset << quint32(0)
                << quint16(8) << quint32(0);
    QCOMPARE(file.write(table), qint64(22));
    QCOMPARE(file.write("huge.bin", 8), qint64(8));
    file.close();

    QElapsedTimer timer;
    timer.start();
    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString("indexing took %1 ms").arg(timer.elapsed())));
    QCOMPARE(index.sourceSize(), qint64(tableOffset) + 30);
    QCOMPARE(index.retainedPayloadBytes(), qint64(0));
    QCOMPARE(index.member(0).dataSize, payloadSize);
}

void ResourceBrowserCoreTests::staleArchiveRejectsLazyRead()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "stale.res", {{"one.bin", "old"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY(index.open(path, &error));
    QFile changed(path);
    QVERIFY(changed.open(QIODevice::Append));
    QCOMPARE(changed.write("changed"), qint64(7));
    changed.close();
    QVERIFY(index.readMember(0, &error).isNull());
    QVERIFY(error.contains("changed", Qt::CaseInsensitive));
}

void ResourceBrowserCoreTests::catalogPreservesRuntimePrecedenceAndOverrideChain()
{
    QTemporaryDir directory;
    const QString high = writeArchive(directory, "high.res",
        {{"shared.mmp", "high"}, {"only-high.txt", "a"}});
    const QString low = writeArchive(directory, "low.res",
        {{"SHARED.MMP", "low"}, {"only-low.lua", "b"}});
    CResourceCatalog catalog;
    QStringList errors;
    QVERIFY2(catalog.build({high, low}, &errors), qPrintable(errors.join('\n')));
    const QVector<SResourceCatalogEntry> shared = catalog.providers("shared.mmp");
    QCOMPARE(shared.size(), 2);
    QVERIFY(shared.at(0).winning);
    QVERIFY(!shared.at(1).winning);
    QCOMPARE(shared.at(0).archivePath, high);
    QCOMPARE(shared.at(1).archivePath, low);
    QCOMPARE(catalog.entries().size(), 4);
}

void ResourceBrowserCoreTests::transactionSupportsReplaceAddRenameDelete()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "edit.res",
        {{"keep.bin", "keep"}, {"replace.txt", "before"}, {"rename.lua", "lua"},
         {"delete.bin", "gone"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY2(index.open(path, &error), qPrintable(error));
    CResArchiveTransaction transaction(index);
    QVERIFY(transaction.replace("replace.txt", sha("before"), "after", &error));
    QVERIFY(transaction.add("new.txt", "new", 7, &error));
    QVERIFY(transaction.rename("rename.lua", "renamed.lua", &error));
    QVERIFY(transaction.remove("delete.bin", &error));
    const QString output = directory.filePath("candidate.res");
    QVERIFY2(transaction.writeCandidate(output, &error), qPrintable(error));

    CResArchiveIndex reopened;
    QVERIFY2(reopened.open(output, &error), qPrintable(error));
    QCOMPARE(reopened.readMember("keep.bin", &error), QByteArray("keep"));
    QCOMPARE(reopened.readMember("replace.txt", &error), QByteArray("after"));
    QCOMPARE(reopened.readMember("new.txt", &error), QByteArray("new"));
    QCOMPARE(reopened.readMember("renamed.lua", &error), QByteArray("lua"));
    QVERIFY(reopened.memberIndex("delete.bin") < 0);
}

void ResourceBrowserCoreTests::transactionRejectsCaseAndCp1251Collisions()
{
    QTemporaryDir directory;
    const QString path = writeArchive(directory, "edit.res", {{"Name.TXT", "one"}});
    CResArchiveIndex index;
    QString error;
    QVERIFY(index.open(path, &error));
    CResArchiveTransaction transaction(index);
    QVERIFY(!transaction.add("name.txt", "two", 0, &error));
    QVERIFY(error.contains("collision", Qt::CaseInsensitive));
    error.clear();
    QVERIFY(!transaction.add(QString::fromUtf8("日本語.txt"), "two", 0, &error));
    QVERIFY(error.contains("CP1251", Qt::CaseInsensitive));
}

void ResourceBrowserCoreTests::representativeArchivesIndexReadOnly()
{
    const QString configured = qEnvironmentVariable("EI_RES_READONLY_PROBES");
    if(configured.isEmpty()) QSKIP("No read-only representative archive paths configured.");
    for(const QString& path : configured.split(';', Qt::SkipEmptyParts))
    {
        QElapsedTimer timer;
        timer.start();
        CResArchiveIndex index;
        QString error;
        QVERIFY2(index.open(path, &error), qPrintable(path + ": " + error));
        QCOMPARE(index.retainedPayloadBytes(), qint64(0));
        qInfo().noquote() << QString("readonly-res-index path=%1 size=%2 members=%3 elapsedMs=%4 retainedPayloadBytes=0")
            .arg(path).arg(index.sourceSize()).arg(index.memberCount()).arg(timer.elapsed());
    }
}

QTEST_APPLESS_MAIN(ResourceBrowserCoreTests)
#include "resource_browser_core_tests.moc"

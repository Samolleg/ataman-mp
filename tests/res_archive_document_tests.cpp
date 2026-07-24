#include <QtTest>

#include "res_archive_document.h"
#include "res_save_coordinator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

namespace {
QByteArray syntheticRes()
{
    struct Member { QByteArray name; QByteArray payload; quint32 time; quint32 offset; };
    QVector<Member> members;
    members.append({"opaque.bin", QByteArray::fromHex("001122334455"), 0x10203040, 0});
    members.append({"map.txt", "#zone old island game\r\n#res\r\nold old\r\n", 0x50607080, 0});
    members.append({"mixed.CaSe", "untouched authored payload", 0x90A0B0C0, 0});

    QByteArray bytes(16, '\0');
    for(int index = 0; index < members.size(); ++index)
    {
        bytes.append(QByteArray((16 - (bytes.size() % 16)) % 16, char(0xA0 + index)));
        members[index].offset = quint32(bytes.size());
        bytes.append(members[index].payload);
    }
    bytes.append(QByteArray((16 - (bytes.size() % 16)) % 16, char(0xD0)));
    const quint32 tableOffset = quint32(bytes.size());
    QByteArray names;
    QByteArray tableBytes;
    QDataStream table(&tableBytes, QIODevice::WriteOnly);
    table.setByteOrder(QDataStream::LittleEndian);
    for(int index = 0; index < members.size(); ++index)
    {
        const Member& member = members.at(index);
        table << quint32(-1) << quint32(member.payload.size()) << member.offset
              << member.time << quint16(member.name.size()) << quint32(names.size());
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

QByteArray sha(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
}

class ResArchiveDocumentTests : public QObject
{
    Q_OBJECT
private slots:
    void noOpIsByteIdentical();
    void replacementPreservesUntouchedMembersAndMetadata();
    void staleMemberHashRefusesOutput();
    void malformedArchiveIsRejected();
    void stagedCommitBacksUpAndReopens();
    void failedAtomicCommitLeavesDestinationUntouched();
    void sourceRaceRefusesSave();
};

void ResArchiveDocumentTests::noOpIsByteIdentical()
{
    const QByteArray bytes = syntheticRes();
    CResArchiveDocument document;
    QVERIFY(document.loadBytes(bytes, "synthetic.res"));
    QByteArray output;
    QVERIFY(document.replaceMember("MAP.TXT", sha(document.memberPayload("map.txt")),
                                   document.memberPayload("map.txt"), &output));
    QCOMPARE(output, bytes);
}

void ResArchiveDocumentTests::replacementPreservesUntouchedMembersAndMetadata()
{
    const QByteArray bytes = syntheticRes();
    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(bytes));
    const QByteArray replacement =
        "#zone old island game\r\n#res\r\nold old\r\n"
        "#maps\r\n8 6\r\noldmap oldquest\r\n";
    QByteArray output;
    QStringList errors;
    QVERIFY2(baseline.replaceMember("map.txt", sha(baseline.memberPayload("map.txt")),
                                    replacement, &output, &errors), qPrintable(errors.join('\n')));
    QVERIFY(output.startsWith(bytes.left(8)));
    QCOMPARE(output.mid(12, bytes.size() - 12), bytes.mid(12));
    QVERIFY(output.size() > bytes.size());

    CResArchiveDocument reopened;
    QVERIFY2(reopened.loadBytes(output, "staged.res", &errors), qPrintable(errors.join('\n')));
    QVERIFY2(CResArchiveDocument::validateReplacement(
        baseline, reopened, "map.txt", replacement, &errors), qPrintable(errors.join('\n')));
    QCOMPARE(reopened.members().size(), baseline.members().size());
    QCOMPARE(reopened.members().at(0).name, QString("opaque.bin"));
    QCOMPARE(reopened.members().at(2).name, QString("mixed.CaSe"));
    QCOMPARE(reopened.members().at(0).lastWriteTime, quint32(0x10203040));
    QCOMPARE(reopened.members().at(2).payload, QByteArray("untouched authored payload"));
}

void ResArchiveDocumentTests::staleMemberHashRefusesOutput()
{
    CResArchiveDocument document;
    QVERIFY(document.loadBytes(syntheticRes()));
    QByteArray output("must be cleared");
    QStringList errors;
    QVERIFY(!document.replaceMember("map.txt", QByteArray(64, '0'), "changed", &output, &errors));
    QVERIFY(output.isEmpty());
    QVERIFY(!errors.isEmpty());
}

void ResArchiveDocumentTests::malformedArchiveIsRejected()
{
    QByteArray bytes = syntheticRes();
    bytes[8] = char(0xFF);
    bytes[9] = char(0xFF);
    bytes[10] = char(0xFF);
    bytes[11] = char(0x7F);
    CResArchiveDocument document;
    QStringList errors;
    QVERIFY(!document.loadBytes(bytes, "broken.res", &errors));
    QVERIFY(!errors.isEmpty());
}

void ResArchiveDocumentTests::stagedCommitBacksUpAndReopens()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString archivePath = directory.filePath("texts.res");
    QFile archive(archivePath);
    QVERIFY(archive.open(QIODevice::WriteOnly));
    const QByteArray original = syntheticRes();
    QCOMPARE(archive.write(original), qint64(original.size()));
    archive.close();

    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(original));
    const QByteArray replacement = "#zone custom island game\r\n#res\r\ncustom custom\r\n";
    SResMemberSaveRequest request;
    request.source = QFileInfo(archivePath);
    request.destination = QFileInfo(archivePath);
    request.memberName = "map.txt";
    request.expectedMemberSha256 = sha(baseline.memberPayload("map.txt"));
    request.replacementPayload = replacement;
    request.backupRootOverride = directory.filePath("backups");
    const SResMemberSaveResult result = commitStagedResMemberReplacement(baseline, request);
    QVERIFY2(result.success, qPrintable(result.phase + ": " + result.error));
    QVERIFY(!result.backupPath.isEmpty());

    QFile backup(result.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), original);
    QFile saved(archivePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    CResArchiveDocument reopened;
    QVERIFY(reopened.loadBytes(saved.readAll()));
    QCOMPARE(reopened.memberPayload("map.txt"), replacement);
}

void ResArchiveDocumentTests::failedAtomicCommitLeavesDestinationUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString archivePath = directory.filePath("texts.res");
    const QByteArray original = syntheticRes();
    QFile archive(archivePath);
    QVERIFY(archive.open(QIODevice::WriteOnly));
    archive.write(original);
    archive.close();
    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(original));
    SResMemberSaveRequest request;
    request.source = QFileInfo(archivePath);
    request.destination = QFileInfo(archivePath);
    request.memberName = "map.txt";
    request.expectedMemberSha256 = sha(baseline.memberPayload("map.txt"));
    request.replacementPayload = "changed map table";
    request.backupRootOverride = directory.filePath("backups");
    request.simulateAtomicCommitFailure = true;
    const SResMemberSaveResult result = commitStagedResMemberReplacement(baseline, request);
    QVERIFY(!result.success);
    QCOMPARE(result.phase, QString("atomic-replacement"));
    QFile unchanged(archivePath);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
}

void ResArchiveDocumentTests::sourceRaceRefusesSave()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original = syntheticRes();
    const QString sourcePath = directory.filePath("source.res");
    const QString destinationPath = directory.filePath("copy.res");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(original);
    source.close();
    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(original));
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write("externally changed");
    source.close();

    SResMemberSaveRequest request;
    request.source = QFileInfo(sourcePath);
    request.destination = QFileInfo(destinationPath);
    request.memberName = "map.txt";
    request.expectedMemberSha256 = sha(baseline.memberPayload("map.txt"));
    request.replacementPayload = "changed";
    request.backupRootOverride = directory.filePath("backups");
    const SResMemberSaveResult result = commitStagedResMemberReplacement(baseline, request);
    QVERIFY(!result.success);
    QCOMPARE(result.phase, QString("source-check"));
    QVERIFY(!QFileInfo::exists(destinationPath));
}

QTEST_APPLESS_MAIN(ResArchiveDocumentTests)
#include "res_archive_document_tests.moc"

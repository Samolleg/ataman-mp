#include <QtTest>

#include "res_archive_document.h"
#include "res_save_coordinator.h"
#include "synthetic_map_table_archive.h"
#include "map_table_document.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include "res_file.h"

// The focused target links the runtime reader without the unrelated core
// utility implementation. Keep its stream format identical to util::formatStream
// so the regression exercises the production parser's byte order.
namespace util {
void formatStream(QDataStream& stream)
{
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
}
}

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

QByteArray runtimeMemberPayload(const QByteArray& archive, const QString& requested)
{
    const quint32 count = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(archive.constData() + 4));
    const quint32 tableOffset = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(archive.constData() + 8));
    const quint32 namesSize = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(archive.constData() + 12));
    const quint32 entrySize = 22;
    const quint32 namesOffset = tableOffset + count * entrySize;
    const QByteArray encoded = requested.toLower().toLocal8Bit();
    quint32 sum = 0;
    for(const unsigned char byte : encoded) sum += byte;
    quint32 index = sum % count;
    for(quint32 guard = 0; guard < count; ++guard)
    {
        const quint32 entry = tableOffset + index * entrySize;
        const quint32 next = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(archive.constData() + entry));
        const quint32 size = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(archive.constData() + entry + 4));
        const quint32 offset = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(archive.constData() + entry + 8));
        const quint16 nameLength = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(archive.constData() + entry + 16));
        const quint32 nameOffset = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(archive.constData() + entry + 18));
        if(nameOffset + nameLength <= namesSize)
        {
            const QByteArray name = archive.mid(int(namesOffset + nameOffset), nameLength);
            if(QString::fromLocal8Bit(name).compare(requested, Qt::CaseInsensitive) == 0)
                return archive.mid(int(offset), int(size));
        }
        if(next == quint32(-1) || next >= count) break;
        index = next;
    }
    return QByteArray();
}
}

class ResArchiveDocumentTests : public QObject
{
    Q_OBJECT
private slots:
    void noOpIsByteIdentical();
    void legacyResReaderUsesExactNameLengthAndRejectsInvalidRanges();
    void replacementPreservesUntouchedMembersAndMetadata();
    void staleMemberHashRefusesOutput();
    void malformedArchiveIsRejected();
    void stagedCommitBacksUpAndReopens();
    void failedAtomicCommitLeavesDestinationUntouched();
    void sourceRaceRefusesSave();
    void syntheticMapTableAddsOwnedMaplessZoneLosslessly();
    void syntheticPlaceholderBuildsStandaloneResolvableChecker();
    void bulkAppendBuildsAllMembersInOneDerivedArchive();
    void publishMembersRequiresExplicitReplacementAndPreservesUnrelatedBytes();
    void publishedMembersCommitInPlaceWithOptionalBackup();
    void failedPublishedMembersCommitLeavesArchiveUntouched();
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
    QCOMPARE(output.size(), bytes.size() + replacement.size()
             - baseline.memberPayload("map.txt").size());

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

void ResArchiveDocumentTests::syntheticMapTableAddsOwnedMaplessZoneLosslessly()
{
    const QByteArray original = syntheticRes();
    QByteArray staged;
    QStringList errors;
    QVERIFY2(buildSyntheticMapTableArchive(
        original, "texts.res", "ed1234abcd", "island", "ed1234abcd",
        7, 5, QPointF(12.5, 18.25), &staged, &errors),
        qPrintable(errors.join('\n')));
    QVERIFY(staged != original);

    CResArchiveDocument baseline;
    CResArchiveDocument candidate;
    QVERIFY(baseline.loadBytes(original));
    QVERIFY(candidate.loadBytes(staged));
    QCOMPARE(candidate.members().size(), baseline.members().size());
    QCOMPARE(candidate.members().at(0).payload, baseline.members().at(0).payload);
    QCOMPARE(candidate.members().at(2).payload, baseline.members().at(2).payload);

    CMapTableDocument table;
    QVERIFY2(table.loadBytes(candidate.memberPayload("map.txt"), &errors),
             qPrintable(errors.join('\n')));
    int matches = 0;
    for(const SMapTableZone& zone : table.zones())
    {
        if(zone.zoneId != "ed1234abcd")
            continue;
        ++matches;
        QCOMPARE(zone.mprName, QString("ed1234abcd"));
        QCOMPARE(zone.mobName, QString("ed1234abcd"));
        QVERIFY(zone.maps.present);
        QCOMPARE(zone.maps.sectorWidth, 7);
        QCOMPARE(zone.maps.sectorHeight, 5);
        QCOMPARE(zone.maps.minimapTexture, syntheticMapPlaceholderTextureName());
        QCOMPARE(zone.maps.objectivesTexture, syntheticMapPlaceholderTextureName());
        QCOMPARE(zone.exits.size(), 1);
        QCOMPARE(zone.exits.first().destinationZoneId, QString("none"));
        QCOMPARE(zone.exits.first().deployRectangle.center(), QPointF(12.5, 18.25));
    }
    QCOMPARE(matches, 1);

    QByteArray customStaged;
    errors.clear();
    QVERIFY2(buildSyntheticMapTableArchive(
        original, "texts.res", "ed5678abcd", "island", "ed5678abcd",
        7, 5, QPointF(12.5, 18.25), &customStaged, &errors,
        "customatlas007"), qPrintable(errors.join('\n')));
    CResArchiveDocument customCandidate;
    QVERIFY(customCandidate.loadBytes(customStaged));
    CMapTableDocument customTable;
    QVERIFY2(customTable.loadBytes(customCandidate.memberPayload("map.txt"), &errors),
             qPrintable(errors.join('\n')));
    int customMatches = 0;
    for(const SMapTableZone& zone : customTable.zones())
    {
        if(zone.zoneId != "ed5678abcd")
            continue;
        ++customMatches;
        QCOMPARE(zone.maps.minimapTexture, QString("customatlas007"));
        QCOMPARE(zone.maps.objectivesTexture, QString("customatlas007"));
    }
    QCOMPARE(customMatches, 1);
}

void ResArchiveDocumentTests::legacyResReaderUsesExactNameLengthAndRejectsInvalidRanges()
{
    const QByteArray bytes = syntheticRes();

    // syntheticRes deliberately concatenates its names without a trailing NUL.
    // CResFile must decode exactly NamesLenght bytes rather than scanning beyond
    // the filename table.
    CResFile valid(bytes);
    QCOMPARE(valid.bufferOfFiles().size(), 3);
    QCOMPARE(valid.bufferOfFiles().value(QStringLiteral("opaque.bin")),
             QByteArray::fromHex("001122334455"));
    QCOMPARE(valid.bufferOfFiles().value(QStringLiteral("mixed.CaSe")),
             QByteArray("untouched authored payload"));

    // A short names table used to be accepted after readRawData() returned a
    // partial read. The incomplete buffer was then passed to the NUL-terminated
    // QTextCodec overload.
    const QByteArray truncatedNames = bytes.left(bytes.size() - 1);
    CResFile truncated(truncatedNames);
    QVERIFY(truncated.bufferOfFiles().isEmpty());

    const quint32 tableOffset = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + 8));

    QByteArray outOfRangeName = bytes;
    qToLittleEndian<quint16>(quint16(2),
        reinterpret_cast<uchar*>(outOfRangeName.data() + tableOffset + 16));
    qToLittleEndian<quint32>(quint32(0xFFFFFFFF),
        reinterpret_cast<uchar*>(outOfRangeName.data() + tableOffset + 18));
    CResFile invalidNameRange(outOfRangeName);
    QVERIFY(invalidNameRange.bufferOfFiles().isEmpty());

    QByteArray overflowingPayloadRange = bytes;
    qToLittleEndian<quint32>(quint32(1),
        reinterpret_cast<uchar*>(overflowingPayloadRange.data() + tableOffset + 4));
    qToLittleEndian<quint32>(quint32(0xFFFFFFFF),
        reinterpret_cast<uchar*>(overflowingPayloadRange.data() + tableOffset + 8));
    CResFile invalidPayloadRange(overflowingPayloadRange);
    QVERIFY(invalidPayloadRange.bufferOfFiles().isEmpty());
}

void ResArchiveDocumentTests::publishedMembersCommitInPlaceWithOptionalBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString archivePath = directory.filePath("textures.res");
    const QByteArray original = syntheticRes();
    QFile archive(archivePath);
    QVERIFY(archive.open(QIODevice::WriteOnly));
    QCOMPARE(archive.write(original), qint64(original.size()));
    archive.close();

    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(original, archivePath));
    SResPublishedMembersSaveRequest request;
    request.source = QFileInfo(archivePath);
    request.destination = QFileInfo(archivePath);
    SResArchiveNewMember page;
    page.name = QStringLiteral("zone2_deadbeef000.mmp");
    page.payload = QByteArray("new terrain page");
    request.published.append(page);
    request.allowReplaceExisting = false;
    request.createBackup = false;
    request.backupRootOverride = directory.filePath("backups");
    const SResPublishedMembersSaveResult withoutBackup =
        commitStagedResPublishedMembers(baseline, request);
    QVERIFY2(withoutBackup.success,
             qPrintable(withoutBackup.phase + ": " + withoutBackup.error));
    QVERIFY(withoutBackup.backupPath.isEmpty());

    QFile saved(archivePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray firstPublished = saved.readAll();
    saved.close();
    CResArchiveDocument first;
    QVERIFY(first.loadBytes(firstPublished, archivePath));
    QCOMPARE(first.memberPayload("zone2_deadbeef000.mmp"),
             QByteArray("new terrain page"));
    QCOMPARE(first.memberPayload("opaque.bin"), baseline.memberPayload("opaque.bin"));

    SResPublishedMembersSaveRequest replacement = request;
    replacement.published[0].payload = QByteArray("replacement terrain page");
    replacement.allowReplaceExisting = true;
    replacement.createBackup = true;
    CResArchiveDocument replacementBaseline;
    QVERIFY(replacementBaseline.loadBytes(firstPublished, archivePath));
    const SResPublishedMembersSaveResult withBackup =
        commitStagedResPublishedMembers(replacementBaseline, replacement);
    QVERIFY2(withBackup.success,
             qPrintable(withBackup.phase + ": " + withBackup.error));
    QVERIFY(!withBackup.backupPath.isEmpty());
    QFile backup(withBackup.backupPath);
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), firstPublished);
}

void ResArchiveDocumentTests::failedPublishedMembersCommitLeavesArchiveUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString archivePath = directory.filePath("textures.res");
    const QByteArray original = syntheticRes();
    QFile archive(archivePath);
    QVERIFY(archive.open(QIODevice::WriteOnly));
    QCOMPARE(archive.write(original), qint64(original.size()));
    archive.close();

    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(original, archivePath));
    SResPublishedMembersSaveRequest request;
    request.source = QFileInfo(archivePath);
    request.destination = QFileInfo(archivePath);
    SResArchiveNewMember page;
    page.name = QStringLiteral("zone2_deadbeef000.mmp");
    page.payload = QByteArray("new terrain page");
    request.published.append(page);
    request.createBackup = false;
    request.simulateAtomicCommitFailure = true;
    const SResPublishedMembersSaveResult result =
        commitStagedResPublishedMembers(baseline, request);
    QVERIFY(!result.success);
    QCOMPARE(result.phase, QStringLiteral("atomic-replacement"));
    QFile unchanged(archivePath);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original);
}

void ResArchiveDocumentTests::bulkAppendBuildsAllMembersInOneDerivedArchive()
{
    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(syntheticRes()));
    QVector<SResArchiveNewMember> appended;
    for(int page = 0; page < 16; ++page)
    {
        SResArchiveNewMember member;
        member.name = QString("edstress00%1.mmp").arg(page, 3, 10, QLatin1Char('0'));
        member.payload = QByteArray(257 + page, char(page));
        appended.append(member);
    }
    QByteArray output;
    QStringList errors;
    QVERIFY2(baseline.buildWithAppendedMembers(appended, &output, &errors),
             qPrintable(errors.join('\n')));
    CResArchiveDocument reopened;
    QVERIFY2(reopened.loadBytes(output, "bulk.res", &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(reopened.members().size(), baseline.members().size() + appended.size());
    QCOMPARE(reopened.memberPayload("opaque.bin"), baseline.memberPayload("opaque.bin"));
    for(const SResArchiveNewMember& member : appended)
        QCOMPARE(reopened.memberPayload(member.name), member.payload);
}

void ResArchiveDocumentTests::publishMembersRequiresExplicitReplacementAndPreservesUnrelatedBytes()
{
    CResArchiveDocument baseline;
    QVERIFY(baseline.loadBytes(syntheticRes()));
    QVector<SResArchiveNewMember> firstPages;
    SResArchiveNewMember firstPage;
    firstPage.name = QStringLiteral("zone2_deadbeef000.mmp");
    firstPage.payload = QByteArray("first-page");
    firstPages.append(firstPage);
    QByteArray withFirstIdentity;
    QStringList errors;
    QVERIFY2(baseline.buildWithPublishedMembers(
                 firstPages, false, &withFirstIdentity, &errors),
             qPrintable(errors.join('\n')));

    CResArchiveDocument installed;
    QVERIFY(installed.loadBytes(withFirstIdentity, QStringLiteral("textures.res")));
    const QByteArray unrelated = installed.memberPayload(QStringLiteral("opaque.bin"));

    QVector<SResArchiveNewMember> changedPages;
    SResArchiveNewMember changedPage;
    changedPage.name = QStringLiteral("zone2_deadbeef000.mmp");
    changedPage.payload = QByteArray("replacement-page");
    changedPages.append(changedPage);
    QByteArray refused;
    errors.clear();
    QVERIFY(!installed.buildWithPublishedMembers(
        changedPages, false, &refused, &errors));
    QVERIFY(refused.isEmpty());

    QByteArray replaced;
    errors.clear();
    QVERIFY2(installed.buildWithPublishedMembers(
                 changedPages, true, &replaced, &errors),
             qPrintable(errors.join('\n')));
    CResArchiveDocument reopened;
    QVERIFY(reopened.loadBytes(replaced, QStringLiteral("published-textures.res")));
    QCOMPARE(reopened.memberPayload(QStringLiteral("zone2_deadbeef000.mmp")),
             QByteArray("replacement-page"));
    QCOMPARE(reopened.memberPayload(QStringLiteral("opaque.bin")), unrelated);

    // One missing page beside colliding identity pages is an inconsistent,
    // partial publication and must not be accepted as either update or reuse.
    QVector<SResArchiveNewMember> partialIdentity = changedPages;
    SResArchiveNewMember missingPage;
    missingPage.name = QStringLiteral("zone2_deadbeef001.mmp");
    missingPage.payload = QByteArray("second-page");
    partialIdentity.append(missingPage);
    QByteArray partialRefused;
    errors.clear();
    QVERIFY(!installed.buildWithPublishedMembers(
        partialIdentity, false, &partialRefused, &errors));
    QVERIFY(partialRefused.isEmpty());

    // Publishing identical bytes is safe reuse and must not require the
    // replacement authority reserved for an identity collision.
    QByteArray reused;
    errors.clear();
    QVERIFY2(reopened.buildWithPublishedMembers(
                 changedPages, false, &reused, &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(reused, replaced);
}

void ResArchiveDocumentTests::syntheticPlaceholderBuildsStandaloneResolvableChecker()
{
    QByteArray staged;
    QStringList errors;
    QVERIFY2(buildSyntheticMapPlaceholderArchive(&staged, &errors),
             qPrintable(errors.join('\n')));
    CResArchiveDocument candidate;
    QVERIFY2(candidate.loadBytes(staged, "temporary-placeholder.res", &errors),
             qPrintable(errors.join('\n')));
    QCOMPARE(candidate.members().size(), 1);
    QCOMPARE(candidate.members().first().name,
             syntheticMapPlaceholderTextureName() + ".mmp");
    const QByteArray checker = candidate.memberPayload(
        syntheticMapPlaceholderTextureName() + ".mmp");
    QCOMPARE(runtimeMemberPayload(
        staged, syntheticMapPlaceholderTextureName() + ".mmp"), checker);
    QCOMPARE(checker.size(), 76 + 256 * 256 * 4);
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData())), quint32(0x00504D4D));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 4)), quint32(256));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 12)),
        quint32(1));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 16)), quint32(0x00008888));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 24)), quint32(0xFF000000));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 36)), quint32(0x00FF0000));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 48)), quint32(0x0000FF00));
    QCOMPARE(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(checker.constData() + 60)), quint32(0x000000FF));
}

QTEST_APPLESS_MAIN(ResArchiveDocumentTests)
#include "res_archive_document_tests.moc"

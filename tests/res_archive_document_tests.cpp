#include <QtTest>
#include "travel_document.h"
#include "travel_area_geometry.h"
#include "travel_panel.h"
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QCompleter>
#include <QAbstractItemView>

#include "res_archive_document.h"
#include "res_save_coordinator.h"
#include "synthetic_map_table_archive.h"
#include "map_table_document.h"
#include "terrain_save_flow_policy.h"

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
    void pairedTravelRoute();
    void travelRoundTripAndConflict();
    void travelSaveBatchRollback();
    void travelGeometryAndPanel();
    void travelMergeAndLinkValidation();
    void ordinaryMapSaveUsesBoundHdArchiveWithBackup()
    {
        QTemporaryDir temp;QVERIFY(temp.isValid());
        const QString archivePath=QDir(temp.path()).filePath("HDPack_Terrain.res");
        const QByteArray original=syntheticRes();
        QFile file(archivePath);QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original),qint64(original.size()));file.close();
        const auto plan=terrainPublicationResourceChoicePlan(
            {temp.path()},QDir(temp.path()).filePath("zone3obr.mpr"),archivePath,true,false);
        QVERIFY(!plan.requiresExplicitChoice);
        QCOMPARE(plan.initialPath,QFileInfo(archivePath).absoluteFilePath());
        CResArchiveDocument baseline;QStringList errors;
        QVERIFY(baseline.loadBytes(original,archivePath,&errors));
        SResArchiveNewMember page;page.name="zone3obr_a27158a988f4_000.mmp";page.payload="synthetic-new-page";
        SResPublishedMembersSaveRequest request;request.source=QFileInfo(plan.initialPath);
        request.destination=request.source;request.published={page};request.createBackup=true;
        request.allowReplaceExisting=false;request.backupRootOverride=temp.path()+"/backups";
        const auto saved=commitStagedResPublishedMembers(baseline,request);
        QVERIFY2(saved.success,qPrintable(saved.error));
        QVERIFY(file.open(QIODevice::ReadOnly));const auto result=file.readAll();file.close();
        CResArchiveDocument reopened;QVERIFY(reopened.loadBytes(result,archivePath,&errors));
        QCOMPARE(reopened.memberPayload("opaque.bin"),baseline.memberPayload("opaque.bin"));
        QCOMPARE(reopened.memberPayload("map.txt"),baseline.memberPayload("map.txt"));
        QCOMPARE(reopened.memberPayload(page.name),page.payload);
        QFile backup(saved.backupPath);QVERIFY(backup.open(QIODevice::ReadOnly));QCOMPARE(backup.readAll(),original);
        page.payload="conflicting-page";request.published={page};
        const auto refused=commitStagedResPublishedMembers(reopened,request);
        QVERIFY(!refused.success);
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),result);file.close();
        QVERIFY(terrainPublicationResourceChoicePlan({temp.path()},"copy.mpr",archivePath,false,false).requiresExplicitChoice);
        QVERIFY(terrainPublicationResourceChoicePlan({temp.path()},"zone3obr.mpr",QString(),true,false).requiresExplicitChoice);
    }
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

void ResArchiveDocumentTests::travelRoundTripAndConflict()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path=dir.filePath("texts.res");
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(syntheticRes());file.close();
    CTravelDocument doc;QString error;QVERIFY2(doc.open(path,&error),qPrintable(error));
    QVERIFY(!doc.dirty());
    SMapSaveFile initial;QVERIFY(doc.prepareSave(&initial,&error));
    QCOMPARE(initial.before,initial.after);
    const auto original=doc.snapshot();auto zone=doc.table().zones()[0];
    SMapTableExit exit;exit.exitId=1;exit.destinationZoneId="none";exit.destinationExitId=0;
    exit.hasDeployRectangle=true;exit.deployRectangle=QRectF(0.125,2.5,10,15);
    exit.hasRemoveRectangle=true;exit.removeRectangle=QRectF(25,30,6,7);zone.exits.append(exit);
    QVERIFY(doc.replaceZone(0,zone,&error));QVERIFY(doc.dirty());
    const auto edited=doc.snapshot();QVERIFY(doc.restore(original));QVERIFY(!doc.dirty());
    QVERIFY(doc.restore(edited));QVERIFY(doc.dirty());
    SMapSaveFile save;QVERIFY2(doc.prepareSave(&save,&error),qPrintable(error));
    const auto result=commitMapSaveBatch({save},dir.filePath("backups"));
    QVERIFY2(result.success,qPrintable(result.error));doc.acceptSaved();QVERIFY(!doc.dirty());
    CTravelDocument reopened;QVERIFY(reopened.open(path,&error));
    QCOMPARE(reopened.table().zones()[0].exits[0].deployRectangle,exit.deployRectangle);
    QCOMPARE(reopened.table().zones()[0].exits[0].removeRectangle,exit.removeRectangle);
    CResArchiveDocument archive;QStringList errors;
    QVERIFY(archive.loadBytes(save.after,path,&errors));
    QCOMPARE(archive.memberPayload("opaque.bin"),QByteArray::fromHex("001122334455"));
    // Undo after saving is a new dirty edit, and stale table changes are refused.
    QVERIFY(doc.restore(original));QVERIFY(doc.dirty());
    QVERIFY(file.open(QIODevice::WriteOnly));file.write(syntheticRes());file.close();
    QVERIFY(!doc.prepareSave(&save,&error));
}

void ResArchiveDocumentTests::travelSaveBatchRollback()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());
    QVector<SMapSaveFile> files;
    for(int i=0;i<3;++i) {
        SMapSaveFile item;item.path=dir.filePath(QString::number(i));
        item.before="old";item.after="new";files.append(item);
        QFile f(item.path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(item.before);
    }
    auto failed=commitMapSaveBatch(files,dir.filePath("backup"),1);
    QVERIFY(!failed.success);QVERIFY(failed.rollbackComplete);
    for(const auto& item:files){QFile f(item.path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),item.before);}
    auto passed=commitMapSaveBatch(files,dir.filePath("backup"));QVERIFY2(passed.success,qPrintable(passed.error));
    for(const auto& item:files){QFile f(item.path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),item.after);}
    // A stale baseline aborts before replacing any destination.
    QVERIFY(!commitMapSaveBatch(files,dir.filePath("backup")).success);
}

void ResArchiveDocumentTests::travelGeometryAndPanel()
{
    const QRectF r(10,20,30,40),bounds(0,0,100,100);
    QCOMPARE(travelDraggedRect(r,QPointF(7,-3),-1,bounds),QRectF(17,17,30,40));
    QCOMPARE(travelDraggedRect(r,QPointF(100,100),-1,bounds),QRectF(70,60,30,40));
    QCOMPARE(travelDraggedRect(r,QPointF(5,8),0,bounds),QRectF(15,28,25,32));
    QCOMPARE(travelDraggedRect(r,QPointF(5,8),3,bounds),QRectF(10,20,35,40));
    QCOMPARE(travelDraggedRect(r,QPointF(-35,0),3,bounds),QRectF(5,20,5,40));
    QCOMPARE(travelAreaHandles(r).size(),8);
    SMapTableZone z;z.zoneId="test";SMapTableExit e;e.exitId=1;e.destinationZoneId="none";
    e.hasDeployRectangle=true;e.deployRectangle=r;e.hasRemoveRectangle=true;e.removeRectangle=QRectF(60,60,10,10);z.exits.append(e);
    CTravelPanel panel;panel.populate({z},0,0,false);
    int edits=0;int expectedIndex=0;SMapTableExit changed;
    QObject::connect(&panel,&CTravelPanel::exitEdited,[&](int index,const SMapTableExit& value){QCOMPARE(index,expectedIndex);++edits;changed=value;});
    int requested=-1,focus=-1;bool preference=false;bool expectNew=true;
    QObject::connect(&panel,&CTravelPanel::drawRequested,[&](bool remove,bool create){QCOMPARE(create,expectNew);requested=remove?1:0;});
    panel.findChild<QPushButton*>("travelAdd0")->click();QCOMPARE(requested,0);
    panel.findChild<QPushButton*>("travelAdd1")->click();QCOMPARE(requested,1);
    QObject::connect(&panel,&CTravelPanel::focusRequested,[&](int index,bool remove){focus=index;preference=remove;});
    panel.resize(400,900);panel.setAttribute(Qt::WA_DontShowOnScreen);panel.show();QApplication::processEvents();
    auto* list=panel.findChild<QListWidget*>("travelExitList");QVERIFY(list);
    panel.revealArea(true);
    QTest::mouseClick(list->viewport(),Qt::RightButton,Qt::NoModifier,list->visualItemRect(list->item(0)).center());
    QCOMPARE(focus,0);QVERIFY(preference);
    focus=-1;QTest::mouseClick(list->viewport(),Qt::RightButton,Qt::NoModifier,QPoint(5,list->viewport()->height()-2));QCOMPARE(focus,-1);
    auto* x=panel.findChild<QLineEdit*>("travelRect_0_0");QVERIFY(x);x->setText("12.125");
    QVERIFY(panel.flush());QCOMPARE(edits,1);QCOMPARE(changed.deployRectangle.x(),12.125);QCOMPARE(changed.removeRectangle,e.removeRectangle);
    auto* width=panel.findChild<QLineEdit*>("travelRect_0_2");QVERIFY(width);width->setText("-1");
    QVERIFY(!panel.flush());QCOMPARE(edits,1);
    width->setText("30");x->setText("nan");QVERIFY(!panel.flush());QCOMPARE(edits,1);
    // Redrawing can replace invalid numeric draft fields rather than deadlocking.
    requested=-1;expectNew=false;
    panel.findChild<QPushButton*>("travelRedraw0")->click();QCOMPARE(requested,0);QCOMPARE(edits,1);
    z.exits[0].hasDeployRectangle=false;z.exits[0].deployRectangle=QRectF();
    panel.populate({z},0,0,false);requested=-1;
    panel.findChild<QCheckBox*>("travelPresent0")->click();QCOMPARE(requested,0);
    QVERIFY(!panel.findChild<QCheckBox*>("travelPresent0")->isChecked());
    QVERIFY(panel.flush());QCOMPARE(edits,1);
    requested=-1;panel.findChild<QPushButton*>("travelAdd0")->click();QCOMPARE(requested,0);
    // Display every area combination without changing identifiers or links.
    z.exits.clear();
    for(int mask=0;mask<4;++mask) {
        auto entry=e;entry.exitId=6+mask;entry.hasDeployRectangle=(mask&1)!=0;
        entry.hasRemoveRectangle=(mask&2)!=0;entry.destinationZoneId="gz7g";
        entry.destinationExitId=mask==2?0:1;z.exits.append(entry);
    }
    panel.populate({z},0,2,false);
    QCOMPARE(list->item(0)->text(),QString::fromUtf8("Запись без областей №6 → gz7g, вход №1"));
    QCOMPARE(list->item(1)->text(),QString::fromUtf8("Связь с областью появления №7 → gz7g, вход №1"));
    QCOMPARE(list->item(2)->text(),QString::fromUtf8("Переход №8 → gz7g, вход не выбран"));
    QCOMPARE(list->item(3)->text(),QString::fromUtf8("Появление и переход №9 → gz7g, вход №1"));
    z.exits[2].destinationZoneId="none";panel.populate({z},0,2,false);
    QCOMPARE(list->item(2)->text(),QString::fromUtf8("Переход №8 — без перехода"));
    QVERIFY(panel.flush());QCOMPARE(edits,1);
    SMapTableZone other;other.zoneId="other";
    auto from=e;from.exitId=17;from.destinationZoneId="TEST";from.destinationExitId=7;other.exits.append(from);
    panel.populate({z,other},0,1,false);
    auto* incoming=panel.findChild<QLabel*>("travelIncoming");QVERIFY(incoming);
    QVERIFY(incoming->text().contains(QString::fromUtf8("other, запись №17 → сюда")));
    auto* source=panel.findChild<QComboBox*>("travelSource");QVERIFY(source);
    auto* link=panel.findChild<QPushButton*>("travelLinkIncoming");QVERIFY(link);QVERIFY(link->isEnabled());
    int linkedZone=-1,linkedExit=-1;
    QObject::connect(&panel,&CTravelPanel::incomingRequested,[&](int a,int b){linkedZone=a;linkedExit=b;});
    source->setCurrentIndex(1);link->click();QCOMPARE(linkedZone,1);QCOMPARE(linkedExit,0);
    QCOMPARE(edits,1); // Choosing arrival source does not edit this record's outgoing link.
    expectedIndex=1;
    // Search does not commit arbitrary text or invent destination records.
    auto* destination=panel.findChild<QComboBox*>("travelDestination");
    auto* entry=panel.findChild<QComboBox*>("travelDestinationEntry");
    QVERIFY(destination && entry);QCOMPARE(destination->maxVisibleItems(),10);
    QCOMPARE(destination->completer()->filterMode(),Qt::MatchContains);
    destination->setEditText("unselected search");QVERIFY(!panel.flush());QCOMPARE(edits,1);
    panel.populate({z,other},0,1,false);
    destination->setCurrentIndex(destination->findData("other"));
    QMetaObject::invokeMethod(destination,"activated",Q_ARG(int,destination->currentIndex()));
    QCOMPARE(entry->findData(17)>0,true);QCOMPARE(entry->currentData().toInt(),0);
    entry->setCurrentIndex(entry->findData(17));QVERIFY(panel.flush());
    QCOMPARE(changed.destinationZoneId,QString("other"));QCOMPARE(changed.destinationExitId,17);
    other.exits[0].hasDeployRectangle=false;
    z.exits[1].destinationZoneId="other";z.exits[1].destinationExitId=17;
    panel.populate({z,other},0,1,false);
    QCOMPARE(entry->currentData().toInt(),17);
    QVERIFY(!(entry->model()->flags(entry->model()->index(entry->currentIndex(),0)) & Qt::ItemIsEnabled));
    QVERIFY(panel.findChild<QLabel*>("travelDirectionNote")->text().contains(QString::fromUtf8("не готово")));
    int routeRequests=0;QObject::connect(&panel,&CTravelPanel::routeRequested,[&](){++routeRequests;});
    panel.findChild<QPushButton*>("travelDestinationCreate")->click();QCOMPARE(routeRequests,1);

    panel.populate({z,other},0,2,false);QVERIFY(!link->isEnabled());
    QVERIFY(incoming->text().contains(QString::fromUtf8("пока никто")));
}
void ResArchiveDocumentTests::travelMergeAndLinkValidation()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());QString error;QStringList errors;const auto path=dir.filePath("texts.res");
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(syntheticRes());file.close();
    CTravelDocument doc;QVERIFY(doc.open(path,&error));auto zone=doc.table().zones()[0];
    SMapTableExit e;e.exitId=1;e.destinationZoneId="missing";e.destinationExitId=1;
    e.hasDeployRectangle=true;e.deployRectangle=QRectF(1,2,3,4);zone.exits.append(e);
    QVERIFY(doc.replaceZone(0,zone,&error));SMapSaveFile save;QVERIFY(!doc.prepareSave(&save,&error));
    zone.exits[0].destinationZoneId="old";QVERIFY(doc.replaceZone(0,zone,&error));QVERIFY2(doc.prepareSave(&save,&error),qPrintable(error));
    // An independent member update after opening must survive this edit.
    CResArchiveDocument archive;QVERIFY(archive.loadBytes(syntheticRes(),path,&errors));QByteArray external;
    QVERIFY(archive.replaceMember("opaque.bin",QCryptographicHash::hash(archive.memberPayload("opaque.bin"),QCryptographicHash::Sha256).toHex(),"external",&external,&errors));
    QVERIFY(file.open(QIODevice::WriteOnly));file.write(external);file.close();
    QVERIFY(doc.prepareSave(&save,&error));QVERIFY(archive.loadBytes(save.after,path,&errors));QCOMPARE(archive.memberPayload("opaque.bin"),QByteArray("external"));
    // Roll back a newly-created resource as well as an existing resource.
    SMapSaveFile added;added.path=dir.filePath("new.res");added.existed=false;added.after="new";
    const auto failed=commitMapSaveBatch({added,save},dir.filePath("backups"),1);
    QVERIFY(!failed.success);QVERIFY(failed.rollbackComplete);QVERIFY(!QFileInfo::exists(added.path));
    QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),external);
}
void ResArchiveDocumentTests::pairedTravelRoute()
{
    QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("texts.res");
    const QByteArray map="#zone a island game\r\n#res\r\na a\r\n#exit 6\r\nb 6\r\n#deploy\r\n1 2 3 4\r\n#view\r\n90\r\n#opaque\r\nkeep\r\n"
        "#zone b island game\r\n#res\r\nb b\r\n#exit 6\r\na 4\r\n#deploy\r\n10 20 30 40\r\n";
    CResFile archive;archive.addFiledata("map.txt",map);archive.addFiledata("opaque.bin","untouched");archive.saveToFile(path);
    CTravelDocument doc;QString error;QVERIFY(doc.open(path,&error));const auto before=doc.snapshot();
    STravelRoute r;r.sourceZone="a";r.targetZone="b";r.edgeZone="a_b";r.sourceExit=r.targetExit=6;
    r.sourceDeploy=QRectF(1,2,2,2);r.sourceRemove=QRectF(3,2,2,2);
    r.targetDeploy=QRectF(10,20,20,20);r.targetRemove=QRectF(5,20,5,20);r.worldMapPosition=QVector3D(-2,0.5f,0.25f);
    auto bad=r;bad.targetRemove.setWidth(0);QVERIFY(!doc.createPairedRoute(bad,&error));QCOMPARE(doc.snapshot(),before);
    bad=r;bad.targetExit=100;QVERIFY(!doc.createPairedRoute(bad,&error));QCOMPARE(doc.snapshot(),before);
    bad=r;bad.edgeZone="a";QVERIFY(!doc.createPairedRoute(bad,&error));QCOMPARE(doc.snapshot(),before);
    QVERIFY2(doc.createPairedRoute(r,&error),qPrintable(error));const auto after=doc.snapshot();
    const auto zones=doc.table().zones();QCOMPARE(zones.size(),3);
    QCOMPARE(zones[0].exits[0].destinationZoneId,QString("a_b"));QCOMPARE(zones[0].exits[0].destinationExitId,1);
    QCOMPARE(zones[1].exits[0].destinationZoneId,QString("a_b"));QCOMPARE(zones[1].exits[0].destinationExitId,2);
    QCOMPARE(zones[2].zoneType,QString("edge"));QCOMPARE(zones[2].exits.size(),2);
    QCOMPARE(zones[2].exits[0].destinationZoneId,QString("a"));QCOMPARE(zones[2].exits[0].destinationExitId,6);
    QCOMPARE(zones[2].exits[1].destinationZoneId,QString("b"));QCOMPARE(zones[2].exits[1].destinationExitId,6);
    QCOMPARE(zones[0].exits[0].viewAngle,90.0);QVERIFY(after.contains("#opaque\r\nkeep"));
    QVERIFY(after.contains("#position\r\n-2 0.5 0.25"));
    QVERIFY(!doc.createPairedRoute(r,&error));QCOMPARE(doc.snapshot(),after);
    SMapSaveFile save;QVERIFY2(doc.prepareSave(&save,&error),qPrintable(error));
    const auto result=commitMapSaveBatch({save},dir.filePath("backups"));QVERIFY2(result.success,qPrintable(result.error));
    CTravelDocument reopened;QVERIFY(reopened.open(path,&error));QCOMPARE(reopened.snapshot(),after);
    CResArchiveDocument saved;QStringList errors;QVERIFY(saved.loadBytes(save.after,path,&errors));
    QCOMPARE(saved.memberPayload("opaque.bin"),QByteArray("untouched"));
    QVERIFY(doc.restore(before));r.sourceExit=r.targetExit=0;r.edgeZone="new_route";
    QVERIFY2(doc.createPairedRoute(r,&error),qPrintable(error));
    QCOMPARE(doc.table().zones()[0].exits.size(),2);QCOMPARE(doc.table().zones()[0].exits[0].destinationZoneId,QString("b"));
    QCOMPARE(doc.table().zones()[2].exits[0].destinationExitId,1);
}
QTEST_MAIN(ResArchiveDocumentTests)
#include "res_archive_document_tests.moc"

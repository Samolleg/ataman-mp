#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>

#include "mob_family_validation.h"

namespace {
QByteArray section(quint32 type, const QByteArray& payload = QByteArray())
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << type << quint32(8 + payload.size());
    bytes.append(payload);
    return bytes;
}

QByteArray u32(quint32 value)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << value;
    return bytes;
}

QByteArray mobWithObject(quint32 nid)
{
    const QByteArray object = section(0xB001,
        section(0xB002, u32(nid)) + section(0xB003, u32(50))
        + section(0xB004, QByteArray("unit")));
    return section(0xA000, section(0xC000) + section(0xB000, object));
}

QString writeMob(const QString& directory, const QString& name, const QByteArray& bytes)
{
    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return QString();
    return QFileInfo(path).absoluteFilePath();
}
}

class MobFamilyValidationTests : public QObject
{
    Q_OBJECT
private slots:
    void workspaceCopyShadowsInstalledLogicalMob();
    void differentLogicalNamesRemainIndependent();
};

void MobFamilyValidationTests::workspaceCopyShadowsInstalledLogicalMob()
{
    QTemporaryDir workspace;
    QTemporaryDir installed;
    QVERIFY(workspace.isValid());
    QVERIFY(installed.isValid());
    const QString installedPrimary = writeMob(installed.path(), "zone7.mob", mobWithObject(41));
    const QString workspacePrimary = writeMob(workspace.path(), "zone7.mob", mobWithObject(77));
    const QString supplemental = writeMob(workspace.path(), "Autozone7altar.mob", mobWithObject(77));
    QVERIFY(!installedPrimary.isEmpty() && !workspacePrimary.isEmpty() && !supplemental.isEmpty());

    SMobFamilyValidationRequest request;
    request.familyLabel = "zone7-copy";
    request.profileId = "test";
    request.seedMobPaths << workspacePrimary << supplemental;
    request.mappedPrimaryMobPath = installedPrimary;
    request.preferredMobPath = workspacePrimary;
    request.workspaceRoots << workspace.path();
    request.mapRoots << installed.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);

    QCOMPARE(report.members.size(), 2);
    int primaryCount = 0;
    for(const SMobFamilyMember& member : report.members)
    {
        if(member.logicalName == "zone7.mob")
        {
            ++primaryCount;
            QCOMPARE(QDir::cleanPath(member.path), QDir::cleanPath(workspacePrimary));
            QVERIFY(member.shadowedPaths.contains(installedPrimary, Qt::CaseInsensitive));
        }
    }
    QCOMPARE(primaryCount, 1);
    QVERIFY(report.nidOwners.contains(77));
    QCOMPARE(report.nidOwners.value(77).size(), 2);
    QVERIFY(!report.nidOwners.contains(41));
}

void MobFamilyValidationTests::differentLogicalNamesRemainIndependent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray bytes = mobWithObject(88);
    const QString first = writeMob(directory.path(), "first.mob", bytes);
    const QString second = writeMob(directory.path(), "second.mob", bytes);
    SMobFamilyValidationRequest request;
    request.familyLabel = "different-names";
    request.profileId = "test";
    request.seedMobPaths << first << second;
    request.workspaceRoots << directory.path();
    request.includeProfileWideMobs = false;
    const CMobFamilyValidationReport report = CMobFamilyValidator::validate(request);
    QCOMPARE(report.members.size(), 2);
    QCOMPARE(report.nidOwners.value(88).size(), 2);
}

QTEST_APPLESS_MAIN(MobFamilyValidationTests)
#include "mob_family_validation_tests.moc"

#include <QtTest>
#include <QTemporaryDir>
#include "../source/runtime/portable_launch_profile.h"

class PortableProfileTests : public QObject {
    Q_OBJECT
    static void save(const QString& path, const QByteArray& data) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(data), qint64(data.size())); }
private slots:
    void mergesSavedSettingsAndKeepsTypes() {
        QTemporaryDir dir;
        const QString mod = dir.path()+"/MODS/DejaVu";
        QVERIFY(QDir().mkpath(mod));
        save(dir.path()+"/portable-defaults.xml", "<profile version='1'><value section='Settings' name='Flag' kind='4'>1</value><value section='EvilIslands\\Camera Settings' name='Float' kind='3'>AACAPw==</value></profile>");
        save(dir.path()+"/portable-settings.xml", "<profile version='1'><value section='Settings' name='Flag' kind='4'>2</value></profile>");
        QMap<QString, PortableLaunchProfile::Value> values;
        QString error;
        QVERIFY2(PortableLaunchProfile::read(dir.path()+"/portable-defaults.xml", values, &error), qPrintable(error));
        QVERIFY(PortableLaunchProfile::read(dir.path()+"/portable-settings.xml", values, &error));
        QCOMPARE(values[QString("settings")+QChar(0)+"flag"].bytes, QByteArray::fromHex("02000000"));
        QCOMPARE(values[QString("evilislands\\camera settings")+QChar(0)+"float"].bytes, QByteArray::fromHex("0000803f"));
        QVERIFY2(PortableLaunchProfile::write(dir.path(), mod, dir.path()+"/profile.bin", &error), qPrintable(error));
        QFile f(dir.path()+"/profile.bin"); QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.contains(PortableLaunchProfile::wide(QDir::toNativeSeparators(mod+"/addon.dll"))));
        QVERIFY(bytes.contains(PortableLaunchProfile::wide("800x600")));
        QVERIFY(!PortableLaunchProfile::write(dir.path(), dir.path(), dir.path()+"/bad.bin", &error));
    }
    void rejectsInvalidProfiles_data() {
        QTest::addColumn<QByteArray>("xml");
        QTest::newRow("broken") << QByteArray("<profile");
        QTest::newRow("version") << QByteArray("<profile version='2'/>");
        QTest::newRow("scope") << QByteArray("<profile version='1'><value section='Other' name='x' kind='1'>x</value></profile>");
        QTest::newRow("duplicate") << QByteArray("<profile version='1'><value section='Settings' name='X' kind='4'>1</value><value section='settings' name='x' kind='4'>2</value></profile>");
        QTest::newRow("number") << QByteArray("<profile version='1'><value section='Settings' name='x' kind='4'>999999999999</value></profile>");
        QTest::newRow("base64") << QByteArray("<profile version='1'><value section='Settings' name='x' kind='3'>!!</value></profile>");
        QTest::newRow("traversal") << QByteArray("<profile version='1'><value section='Settings\\..\\x' name='x' kind='1'>x</value></profile>");
    }
    void rejectsInvalidProfiles() {
        QFETCH(QByteArray, xml); QTemporaryDir dir; QString error;
        save(dir.path()+"/profile.xml", xml);
        QMap<QString, PortableLaunchProfile::Value> values;
        QVERIFY(!PortableLaunchProfile::read(dir.path()+"/profile.xml", values, &error));
        QVERIFY(!error.isEmpty());
    }
    void installedProfileReadOnly() {
        const QString root = qEnvironmentVariable("EI_TEST_PORTABLE_ROOT");
        if(root.isEmpty()) QSKIP("Set EI_TEST_PORTABLE_ROOT for installed profile read-only check.");
        QTemporaryDir dir; QString error;
        QVERIFY2(PortableLaunchProfile::write(root, root+"/MODS/DejaVu", dir.path()+"/profile.bin", &error), qPrintable(error));
        const QString output = qEnvironmentVariable("EI_TEST_PROFILE_OUTPUT");
        if(!output.isEmpty()) QVERIFY(QFile::copy(dir.path()+"/profile.bin", output));
    }
};
QTEST_GUILESS_MAIN(PortableProfileTests)
#include "portable_launch_profile_tests.moc"

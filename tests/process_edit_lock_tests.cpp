#include <QtCore>
#include <QtTest>
#include <cstdio>
#include <memory>
#include "process_edit_lock.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
bool writeFixture(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("unchanged fixture") == 17;
}

struct Owner {
    QProcess process;
    ~Owner() { if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);} }
    QByteArray start(const QString& path,const QString& mode="map",const QString& executable={}) {
        process.setWorkingDirectory(QFileInfo(path).absolutePath());
        process.start(executable.isEmpty()?QCoreApplication::applicationFilePath():executable,
            {"--owner",path,mode});
        if(!process.waitForStarted(5000))return process.errorString().toUtf8();
        QByteArray output;QElapsedTimer deadline;deadline.start();
        while(!output.contains('\n')&&deadline.elapsed()<5000) {
            process.waitForReadyRead(50);output+=process.readAllStandardOutput();
            if(process.state()==QProcess::NotRunning)break;
        }
        return output.trimmed();
    }
    bool release() {
        process.write("release\n");process.closeWriteChannel();
        return process.waitForFinished(5000)&&process.exitCode()==0;
    }
    bool crash() { process.kill();return process.waitForFinished(5000); }
};

#ifdef Q_OS_WIN
struct Junction {
    QString path;
    ~Junction() { if(!path.isEmpty())RemoveDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16())); }
};
#endif
}

class ProcessEditLockTests:public QObject {
    Q_OBJECT
private slots:
    void usesServiceDirectoryWithoutChangingDocuments() {
        QTemporaryDir root;QVERIFY(root.isValid());
        const auto map=root.filePath(QString::fromUtf8("карта с пробелами.v2.mpr"));
        const auto mob=root.filePath(QString::fromUtf8("карта с пробелами.v2.mob"));
        QVERIFY(writeFixture(map));QVERIFY(writeFixture(mob));
        const auto lockPath=CProcessEditLock::lockFilePath(map);
        QCOMPARE(CProcessEditLock::identity(QFileInfo(lockPath).absolutePath()),CProcessEditLock::identity(root.path())+"/.mapper-service/locks");
        QVERIFY(QFileInfo(lockPath).fileName().contains(QRegularExpression("^[0-9a-f]{64}\\.mapper-edit\\.lock$")));
        auto lease=CProcessEditLock::acquire(map);QVERIFY(lease);
        QVERIFY(QFileInfo::exists(lockPath));
        QCOMPARE(QDir(root.path()).entryList(QDir::Files),QStringList({QFileInfo(mob).fileName(),QFileInfo(map).fileName()}));
        for(const auto& path:{map,mob}){QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("unchanged fixture"));}
        lease.clear();QVERIFY(!QFileInfo::exists(lockPath));
    }

    void sameMapMobAndAliasRefuseOtherProcess() {
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("sub"));
        const auto map=root.filePath("zone.mpr"),mob=root.filePath("zone.mob");
        QVERIFY(writeFixture(map));QVERIFY(writeFixture(mob));
        auto lease=CProcessEditLock::acquire(map);QVERIFY(lease);
        const auto alias=root.filePath("sub/../ZONE.MOB");
        QCOMPARE(CProcessEditLock::lockFilePath(alias),CProcessEditLock::lockFilePath(map));
        Owner other;QCOMPARE(other.start(alias),QByteArray("REFUSED"));
        lease.clear();Owner next;QCOMPARE(next.start(mob),QByteArray("LOCKED"));QVERIFY(next.release());
    }

    void differentInstallationsAndApplicationNamesShareLock() {
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("other-installation"));
        const auto map=root.filePath("zone.mpr");QVERIFY(writeFixture(map));
        const auto otherExe=root.filePath("other-installation/another-mapper.exe");
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),otherExe));
        auto lease=CProcessEditLock::acquire(map);QVERIFY(lease);
        Owner other;QCOMPARE(other.start(map,"map",otherExe),QByteArray("REFUSED"));
    }

    void independentMapsAndResourcesCanBeEdited() {
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("other"));
        const auto first=root.filePath("zone.mpr"),second=root.filePath("other/zone.mpr"),resource=root.filePath("shared.res");
        QVERIFY(writeFixture(first));QVERIFY(writeFixture(second));QVERIFY(writeFixture(resource));
        auto lease=CProcessEditLock::acquire(first);QVERIFY(lease);
        Owner other;QCOMPARE(other.start(second),QByteArray("LOCKED"));QVERIFY(other.release());
        Owner res;QCOMPARE(res.start(resource,"resource"),QByteArray("LOCKED"));
        QString error;QVERIFY(!CProcessEditLock::acquire(resource,&error,false));QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(resource+".mapper-resource.lock"));QVERIFY(res.release());
        auto resLease=CProcessEditLock::acquire(resource,nullptr,false);QVERIFY(resLease);
    }

    void lastSharedReferenceReleasesLock() {
        QTemporaryDir root;const auto map=root.filePath("zone.mpr"),mob=root.filePath("zone.mob");
        QVERIFY(writeFixture(map));QVERIFY(writeFixture(mob));
        auto first=CProcessEditLock::acquire(map),second=CProcessEditLock::acquire(mob);
        QVERIFY(first);QCOMPARE(first.data(),second.data());first.clear();
        Owner blocked;QCOMPARE(blocked.start(map),QByteArray("REFUSED"));
        second.clear();Owner free;QCOMPARE(free.start(map),QByteArray("LOCKED"));QVERIFY(free.release());
    }

    void newDestinationKeepsIdentityAfterCreation() {
        QTemporaryDir root;const auto map=root.filePath("new.mpr"),mob=root.filePath("new.mob");
        const auto before=CProcessEditLock::lockFilePath(map);
        auto lease=CProcessEditLock::acquire(mob);QVERIFY(lease);QVERIFY(writeFixture(map));
        QCOMPARE(CProcessEditLock::lockFilePath(map),before);
        Owner blocked;QCOMPARE(blocked.start(map),QByteArray("REFUSED"));
    }

    void crashedOwnerIsRecovered() {
        QTemporaryDir root;const auto map=root.filePath("zone.mpr");QVERIFY(writeFixture(map));
        Owner owner;QCOMPARE(owner.start(map),QByteArray("LOCKED"));QVERIFY(owner.crash());
        const auto lockPath=CProcessEditLock::lockFilePath(map);QVERIFY(QFileInfo::exists(lockPath));
        auto recovered=CProcessEditLock::acquire(map);QVERIFY(recovered);
        Owner blocked;QCOMPARE(blocked.start(map),QByteArray("REFUSED"));
        recovered.clear();QVERIFY(!QFileInfo::exists(lockPath));
    }

    void liveLegacyOwnerIsRespectedAndDeadOwnerIsMigrated() {
        for(bool map:{true,false}) {
            QTemporaryDir root;const auto path=root.filePath(map?"zone.mpr":"shared.res");QVERIFY(writeFixture(path));
            const auto legacy=CProcessEditLock::identity(path)+(map?".mapper-edit.lock":".mapper-resource.lock");
            Owner old;QCOMPARE(old.start(legacy,"legacy"),QByteArray("LOCKED"));
            QString error;QVERIFY(!CProcessEditLock::acquire(path,&error,map));QVERIFY(!error.isEmpty());
            QVERIFY(QFileInfo::exists(legacy));QVERIFY(!QFileInfo::exists(CProcessEditLock::lockFilePath(path,map)));
            QVERIFY(old.crash());QVERIFY(QFileInfo::exists(legacy));
            auto migrated=CProcessEditLock::acquire(path,&error,map);QVERIFY(migrated);
            QVERIFY(!QFileInfo::exists(legacy));QVERIFY(QFileInfo::exists(CProcessEditLock::lockFilePath(path,map)));
        }
    }

    void unavailableServiceStoreRefusesWithoutSidecarFallback() {
        QTemporaryDir root;const auto map=root.filePath("zone.mpr");QVERIFY(writeFixture(map));
        QVERIFY(writeFixture(root.filePath(".mapper-service")));
        QString error;QVERIFY(!CProcessEditLock::acquire(map,&error));
        QVERIFY(error.contains(".mapper-service/locks"));QVERIFY(!QFileInfo::exists(map+".mapper-edit.lock"));
    }

    void junctionUsesPhysicalDocumentStore() {
#ifdef Q_OS_WIN
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkpath("physical"));
        const auto map=root.filePath("physical/zone.mpr");QVERIFY(writeFixture(map));
        Junction alias;alias.path=root.filePath("alias");
        QCOMPARE(QProcess::execute("cmd.exe",{"/d","/c","mklink","/J",QDir::toNativeSeparators(alias.path),QDir::toNativeSeparators(root.filePath("physical"))}),0);
        const auto aliasMap=alias.path+"/zone.mpr";
        QCOMPARE(CProcessEditLock::lockFilePath(aliasMap),CProcessEditLock::lockFilePath(map));
        auto lease=CProcessEditLock::acquire(aliasMap);QVERIFY(lease);
        Owner blocked;QCOMPARE(blocked.start(map),QByteArray("REFUSED"));
        const auto newMap=root.filePath("physical/new.mpr"),newAlias=alias.path+"/new.mob";
        QCOMPARE(CProcessEditLock::lockFilePath(newAlias),CProcessEditLock::lockFilePath(newMap));
        auto newLease=CProcessEditLock::acquire(newAlias);QVERIFY(newLease);QVERIFY(writeFixture(newMap));
        Owner newBlocked;QCOMPARE(newBlocked.start(newMap),QByteArray("REFUSED"));
#else
        QSKIP("Windows junction regression");
#endif
    }
};

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()==4&&args[1]=="--owner") {
        app.setOrganizationName("Independent installation");app.setApplicationName("Different mapper profile");
        QSharedPointer<CProcessEditLock> lease;std::unique_ptr<QLockFile> legacy;
        bool acquired=false;
        if(args[3]=="legacy") {
            legacy.reset(new QLockFile(args[2]));legacy->setStaleLockTime(0);acquired=legacy->tryLock(0);
        } else {lease=CProcessEditLock::acquire(args[2],nullptr,args[3]!="resource");acquired=!lease.isNull();}
        QTextStream output(stdout);output<<(acquired?"LOCKED\n":"REFUSED\n");output.flush();
        if(!acquired)return 73;
        std::getchar();return 0;
    }
    ProcessEditLockTests tests;return QTest::qExec(&tests,argc,argv);
}

#include "process_edit_lock_tests.moc"

#include <QtCore>
#include <QtTest>
#include <cstdio>
#include <memory>
#include "process_edit_lock.h"
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#endif

namespace {
QString oldStorePath(const QString& path,bool map) {
    const auto current=CProcessEditLock::lockFilePath(path,map);
    return QDir(QFileInfo(CProcessEditLock::identity(path)).absolutePath()).filePath(
        ".mapper-service/locks/"+QFileInfo(current).fileName());
}
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
        QCOMPARE(QFileInfo(lockPath).absolutePath(),mapper_edit_locks::directory());
        QVERIFY(QFileInfo(lockPath).fileName().contains(QRegularExpression("^[0-9a-f]{64}\\.mapper-edit\\.lock$")));
        auto lease=CProcessEditLock::acquire(map);QVERIFY(lease);
        QVERIFY(QFileInfo::exists(lockPath));
        QCOMPARE(QDir(root.path()).entryList(QDir::Files),QStringList({QFileInfo(mob).fileName(),QFileInfo(map).fileName()}));
        for(const auto& path:{map,mob}){QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("unchanged fixture"));}
        lease.clear();QVERIFY(!QFileInfo::exists(lockPath));
        QVERIFY(!QFileInfo::exists(root.filePath(".mapper-service")));
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

    void liveOldOwnersAreRespectedAndDeadOwnersAreUntouched() {
        for(bool map:{true,false}) {
          for(bool service:{false,true}) {
            QTemporaryDir root;const auto path=root.filePath(map?"zone.mpr":"shared.res");QVERIFY(writeFixture(path));
            const auto legacy=service?oldStorePath(path,map):CProcessEditLock::identity(path)+(map?".mapper-edit.lock":".mapper-resource.lock");
            if(service)QVERIFY(QDir().mkpath(QFileInfo(legacy).absolutePath()));
            Owner old;QCOMPARE(old.start(legacy,"legacy"),QByteArray("LOCKED"));
            QString error;QVERIFY(!CProcessEditLock::acquire(path,&error,map));QVERIFY(!error.isEmpty());
            QVERIFY(QFileInfo::exists(legacy));QVERIFY(!QFileInfo::exists(CProcessEditLock::lockFilePath(path,map)));
            QVERIFY(old.crash());QVERIFY(QFileInfo::exists(legacy));
            QFile before(legacy);QVERIFY(before.open(QIODevice::ReadOnly));const auto contents=before.readAll();before.close();
            const auto modified=QFileInfo(legacy).lastModified();
            auto migrated=CProcessEditLock::acquire(path,&error,map);QVERIFY(migrated);
            QVERIFY(QFileInfo::exists(legacy));QVERIFY(QFileInfo::exists(CProcessEditLock::lockFilePath(path,map)));
            QFile after(legacy);QVERIFY(after.open(QIODevice::ReadOnly));QCOMPARE(after.readAll(),contents);
            QCOMPARE(QFileInfo(legacy).lastModified(),modified);
            if(!service)QVERIFY(!QFileInfo::exists(root.filePath(".mapper-service")));
          }
        }
    }

    void remoteOrInvalidLegacyPidIsNotAssumedDead() {
        QTemporaryDir root;const auto map=root.filePath("zone.mpr");QVERIFY(writeFixture(map));
        const auto old=CProcessEditLock::identity(map)+".mapper-edit.lock";
        for(const auto& contents:{QByteArray("4000000000\nmapper\nother-machine-271\n\n\n"),
            QByteArray("4294967296\nmapper\n")+QSysInfo::machineHostName().toUtf8()+"\n\n\n"}) {
            QFile fixture(old);QVERIFY(fixture.open(QIODevice::WriteOnly));QCOMPARE(fixture.write(contents),qint64(contents.size()));fixture.close();
            QString error;QVERIFY(!CProcessEditLock::acquire(map,&error));QVERIFY(error.contains(QString::fromUtf8("Карта открыта")));
            QVERIFY(QFileInfo::exists(old));QVERIFY(!QFileInfo::exists(CProcessEditLock::lockFilePath(map)));
            QVERIFY(!QFileInfo::exists(root.filePath(".mapper-service")));
        }
    }

    void unavailableServiceStoreRefusesWithoutSidecarFallback() {
        QTemporaryDir root;const auto map=root.filePath("zone.mpr");QVERIFY(writeFixture(map));
        QTemporaryDir store;QVERIFY(store.isValid());
        const auto previous=qgetenv("EI_MAPPER_TEST_LOCK_ROOT");
        qputenv("EI_MAPPER_TEST_LOCK_ROOT",store.path().toUtf8());
        const auto restore=qScopeGuard([&](){qputenv("EI_MAPPER_TEST_LOCK_ROOT",previous);});
        QVERIFY(writeFixture(store.filePath("EI Mapper Ataman")));
        QString error;QVERIFY(!CProcessEditLock::acquire(map,&error));
        QVERIFY(error.contains(QDir::toNativeSeparators(mapper_edit_locks::directory())));QVERIFY(!QFileInfo::exists(map+".mapper-edit.lock"));
        QVERIFY(!QFileInfo::exists(root.filePath(".mapper-service")));
    }

    void existingRestrictedAclIsRejectedWithoutModification() {
#ifdef Q_OS_WIN
        QTemporaryDir store;QVERIFY(store.isValid());
        const auto previous=qgetenv("EI_MAPPER_TEST_LOCK_ROOT");
        qputenv("EI_MAPPER_TEST_LOCK_ROOT",store.path().toUtf8());
        const auto restore=qScopeGuard([&](){qputenv("EI_MAPPER_TEST_LOCK_ROOT",previous);});
        const auto path=mapper_edit_locks::directory();QVERIFY(QDir().mkpath(path));
        const auto native=QDir::toNativeSeparators(path);
        PACL acl=nullptr;PSECURITY_DESCRIPTOR sd=nullptr;
        QCOMPARE(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())),SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&sd),DWORD(ERROR_SUCCESS));
        QByteArray before(reinterpret_cast<char*>(acl),acl->AclSize);LocalFree(sd);
        QString error;QVERIFY(!mapper_edit_locks::prepareDirectory(path,&error));
        QVERIFY(error.contains("S-1-5-32-545"));QVERIFY(error.contains(native));
        QCOMPARE(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())),SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,nullptr,nullptr,&acl,nullptr,&sd),DWORD(ERROR_SUCCESS));
        QByteArray after(reinterpret_cast<char*>(acl),acl->AclSize);LocalFree(sd);QCOMPARE(after,before);
#endif
    }

    void liveOwnerNeverExpires() {
        QTemporaryDir root;const auto map=root.filePath("zone.mpr");QVERIFY(writeFixture(map));
        Owner owner;QCOMPARE(owner.start(map),QByteArray("LOCKED"));
        const auto native=QDir::toNativeSeparators(CProcessEditLock::lockFilePath(map));
        const HANDLE file=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        QVERIFY(file!=INVALID_HANDLE_VALUE);
        ULARGE_INTEGER time;time.QuadPart=116444736000000000ULL+quint64(QDateTime::currentDateTime().addYears(-10).toMSecsSinceEpoch())*10000;
        FILETIME old={time.LowPart,time.HighPart};const bool dated=SetFileTime(file,nullptr,nullptr,&old);CloseHandle(file);QVERIFY(dated);
        QString error;QVERIFY(!CProcessEditLock::acquire(map,&error));QVERIFY(error.contains(QString::fromUtf8("Карта открыта")));
        QVERIFY(error.contains(QDir::toNativeSeparators(mapper_edit_locks::directory())));QVERIFY(owner.release());
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
    QTemporaryDir store;
    if(qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))qputenv("EI_MAPPER_TEST_LOCK_ROOT",store.path().toUtf8());
    if(args.size()==3&&args[1]=="--programdata-acl-check") {
        qputenv("EI_MAPPER_TEST_LOCK_ROOT",args[2].toUtf8());QString error;
        if(!mapper_edit_locks::prepareDirectory(mapper_edit_locks::directory(),&error)){QTextStream(stderr)<<error;return 1;}
        QTextStream(stdout)<<mapper_edit_locks::directory()<<"\n";return 0;
    }
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

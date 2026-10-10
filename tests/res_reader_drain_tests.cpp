#include <QtTest>
#include <QtConcurrent>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QDataStream>
#include <QVector3D>
#include <QApplication>
#include <QTreeWidget>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QSettings>
#include <QUuid>
#include <atomic>
#include <future>
#include "res_read_lease.h"
#include "res_archive_index.h"
#include "res_archive_document.h"
#include "map_save_batch.h"
#include "res_save_coordinator.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_profile.h"
#include "mmp_codec.h"
#include "model_gallery.h"
#include "resource_browser.h"
#include "gallery_thumbnail_cache.h"

namespace {
QByteArray figure() {
    QByteArray fig;
    QDataStream stream(&fig, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(0x38474946) << quint32(1) << quint32(1) << quint32(3)
           << quint32(3) << quint32(3) << quint32(0) << quint32(0)
           << quint32(0) << quint32(0);
    for(int block = 0; block < 3; ++block)
        for(int morph = 0; morph < 8; ++morph)
            stream << 0.0f << 0.0f << 0.0f;
    for(int morph = 0; morph < 8; ++morph) stream << 1.0f;
    const QVector<QVector3D> vertices = {QVector3D(-1, 0, 0), QVector3D(1, 0, 0),
                                         QVector3D(0, 0, 1), QVector3D(0, 0, 0)};
    for(int axis = 0; axis < 3; ++axis)
        for(int morph = 0; morph < 8; ++morph)
            for(const QVector3D& vertex : vertices) stream << vertex[axis];
    for(int axis = 0; axis < 4; ++axis)
        for(int lane = 0; lane < 4; ++lane) stream << (axis == 2 ? 1.0f : 0.0f);
    for(int uv = 0; uv < 3; ++uv) stream << 0.0f << 0.0f;
    stream << quint16(0) << quint16(1) << quint16(2);
    stream << quint16(0) << quint16(0) << quint16(0)
           << quint16(1) << quint16(0) << quint16(1)
           << quint16(2) << quint16(0) << quint16(2);
    return fig;
}

bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray archiveBytes(const QVector<QPair<QByteArray,QByteArray>>& members) {
    QByteArray bytes(16,'\0'), tableBytes, names;
    QVector<quint32> offsets;
    for(const auto& member:members) {
        bytes.append(QByteArray((16-bytes.size()%16)%16,'\0'));
        offsets.append(bytes.size()); bytes.append(member.second);
    }
    const quint32 tableOffset=bytes.size();
    QDataStream table(&tableBytes,QIODevice::WriteOnly); table.setByteOrder(QDataStream::LittleEndian);
    for(int i=0;i<members.size();++i) {
        table << quint32(-1) << quint32(members[i].second.size()) << offsets[i]
              << quint32(0) << quint16(members[i].first.size()) << quint32(names.size());
        names.append(members[i].first);
    }
    bytes.append(tableBytes); bytes.append(names);
    QDataStream header(&bytes,QIODevice::WriteOnly); header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(0x019CE23C) << quint32(members.size()) << tableOffset << quint32(names.size());
    return bytes;
}
QByteArray changedArchive(const QByteArray& original) {
    CResArchiveDocument doc; QByteArray candidate; QStringList errors;
    if(!doc.loadBytes(original,"fixture.res",&errors)
        || !doc.replaceMember("opaque.bin",QCryptographicHash::hash(doc.memberPayload("opaque.bin"),QCryptographicHash::Sha256).toHex(),
                              "after",&candidate,&errors)) return {};
    return candidate;
}
QString tempPattern() { return QDir::current().filePath("fixture-XXXXXX"); }
}

class ResReaderDrainTests : public QObject {
    Q_OBJECT
private slots:
    void ownReaderDrainsAndCheckpointRunsAfterRelease() {
        QTemporaryDir dir(tempPattern()); QVERIFY(dir.isValid());
        const QString path=dir.filePath("held.res"); QVERIFY(write(path,"before"));
        QSemaphore ready;
        auto reader=QtConcurrent::run([&] {
            resIo::ReadLease lease(path); QFile file(path);
            const bool opened=lease.acquired() && file.open(QIODevice::ReadOnly);
            ready.release(); QThread::msleep(160); return opened;
        });
        ready.acquire(); QElapsedTimer timer; timer.start(); int checkpoints=0;
        const auto saved=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups"),-1,[&] {
            resIo::ReadLease lease(path); if(lease.acquired()) ++checkpoints;
        });
        QVERIFY(reader.result()); QVERIFY2(saved.success,qPrintable(saved.error));
        QVERIFY(timer.elapsed()>=100); QCOMPARE(saved.commitAttempts,1);
        QCOMPARE(checkpoints,1); QCOMPARE(read(path),QByteArray("after"));
        qInfo("own reader wait: %lld ms, 1 commit, 1 checkpoint",timer.elapsed());
    }
    void timeoutAndAdmissionAreBounded() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("timeout.res");
        QVERIFY(write(path,"before")); QSemaphore ready,release;
        auto reader=QtConcurrent::run([&] {
            resIo::ReadLease lease(path); QFile file(path); const bool ok=file.open(QIODevice::ReadOnly);
            ready.release(); release.acquire(); return ok && lease.acquired();
        });
        ready.acquire(); QString error; QElapsedTimer timer; timer.start();
        { resIo::PublicationLease lease(path,&error,40); QVERIFY(!lease.acquired()); }
        release.release(); QVERIFY(reader.result());
        QVERIFY(error.contains("reader-drain timeout")); QVERIFY(timer.elapsed()<1000);
        { resIo::PublicationLease lease(path); QVERIFY(lease.acquired());
          auto denied=std::async(std::launch::async,[&] { QString reason; resIo::ReadLease readLease(path,&reason,40);
              return !readLease.acquired() && reason.contains("reader-admission timeout"); });
          QVERIFY(denied.get());
          const QString other=dir.filePath("other.res"); resIo::ReadLease unrelated(other); QVERIFY(unrelated.acquired());
        }
        resIo::ReadLease reopened(path); QVERIFY(reopened.acquired()); QCOMPARE(read(path),QByteArray("before"));
    }
    void externalHolderFailsWithoutLoss() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("external.res");
        QVERIFY(write(path,"before")); QFile held(path); QVERIFY(held.open(QIODevice::ReadOnly));
        const auto failed=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups"));
        QVERIFY(!failed.success); QVERIFY(failed.rollbackComplete); QCOMPARE(failed.commitAttempts,3);
        QVERIFY2(failed.error.contains("commit attempt 3"),qPrintable(failed.error));
        QVERIFY(failed.error.contains("Qt file error")); QVERIFY(failed.error.contains("DELETE-sharing probe 32"));
        held.close(); QCOMPARE(read(path),QByteArray("before"));
        QVERIFY(!failed.backups.isEmpty()); QCOMPARE(read(failed.backups.first()),QByteArray("before"));
        const auto retry=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups"));
        QVERIFY2(retry.success,qPrintable(retry.error)); QCOMPARE(read(path),QByteArray("after"));
    }
    void nativeExclusiveOpenDiagnosis() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("exclusive.res"); QVERIFY(write(path,"before"));
        HANDLE held=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
        QVERIFY(held!=INVALID_HANDLE_VALUE);
        const auto failed=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups")); CloseHandle(held);
        QVERIFY(!failed.success); QCOMPARE(failed.commitAttempts,0); QVERIFY(failed.error.contains("open:"));
        QVERIFY(failed.error.contains("Qt file error")); QCOMPARE(read(path),QByteArray("before"));
    }
    void missingAndMismatchAreNotRetried() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("missing.res");
        const auto missing=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups"));
        QVERIFY(!missing.success); QVERIFY(missing.error.contains("missing:")); QCOMPARE(missing.commitAttempts,0);
        QVERIFY(write(path,"foreign"));
        const auto mismatch=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups"));
        QVERIFY(!mismatch.success); QVERIFY(mismatch.error.contains("mismatch:")); QVERIFY(mismatch.error.contains("SHA256"));
        QCOMPARE(mismatch.commitAttempts,0); QCOMPARE(read(path),QByteArray("foreign"));
    }
    void byteRangeLockIsReadFailure() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("range.res");QVERIFY(write(path,"before"));
        HANDLE held=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        QVERIFY(held!=INVALID_HANDLE_VALUE);OVERLAPPED range={};
        const bool locked=LockFileEx(held,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&range);
        if(!locked) {CloseHandle(held);QFAIL("Native byte-range fixture lock failed");}
        const auto failed=commitMapSaveBatch({{path,"before","after",true}},dir.filePath("backups"));
        UnlockFileEx(held,0,1,0,&range);CloseHandle(held);
        QVERIFY(!failed.success);QCOMPARE(failed.commitAttempts,0);
        QVERIFY2(failed.error.contains("read:"),qPrintable(failed.error));
        QVERIFY(failed.error.contains("Qt file error"));QCOMPARE(read(path),QByteArray("before"));
    }
    void transientRetryUsesFreshStaging() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("retry.res"); QVERIFY(write(path,"before"));
        HANDLE native=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,0,nullptr); QVERIFY(native!=INVALID_HANDLE_VALUE);
        std::atomic<bool> observed{false}; QSemaphore released;
        auto releaser=QtConcurrent::run([&] { while(!observed.load()) QThread::msleep(1); QThread::msleep(20); CloseHandle(native); released.release(); });
        resIo::PublicationLease lease(path); QVERIFY(lease.acquired()); QSaveFile output(path);
        output.setDirectWriteFallback(false); QVERIFY(output.open(QIODevice::WriteOnly)); QCOMPARE(output.write("after"),qint64(5));
        int checks=0, attempts=0; QString error;
        const bool ok=resIo::commitWithRetry(output,path,"after",[&](QString* reason) {
            if(++checks==2) { observed=true; if(!released.tryAcquire(1,1000)) return false; }
            return resIo::matches(path,"before",true,reason);
        },&error,&attempts);
        observed=true; releaser.waitForFinished();
        QVERIFY2(ok,qPrintable(error)); QCOMPARE(attempts,2); QVERIFY(error.isEmpty()); QCOMPARE(read(path),QByteArray("after"));
    }
    void retryRechecksRealMismatch() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("race.res"); QVERIFY(write(path,"before"));
        QFile held(path); QVERIFY(held.open(QIODevice::ReadOnly));
        resIo::PublicationLease lease(path); QSaveFile output(path); output.setDirectWriteFallback(false);
        QVERIFY(output.open(QIODevice::WriteOnly)); QCOMPARE(output.write("after"),qint64(5));
        int checks=0,attempts=0; QString error;
        const bool ok=resIo::commitWithRetry(output,path,"after",[&](QString* reason) {
            if(++checks==2) { held.close(); if(!write(path,"foreign")) return false; }
            return resIo::matches(path,"before",true,reason);
        },&error,&attempts);
        QVERIFY(!ok); QCOMPARE(attempts,1); QVERIFY(error.contains("mismatch:")); QCOMPARE(read(path),QByteArray("foreign"));
    }
    void rollbackAndRepeatedSave() {
        QTemporaryDir dir(tempPattern()); const QString a=dir.filePath("a.res"),b=dir.filePath("b.mpr");
        QVERIFY(write(a,"a-before")); QVERIFY(write(b,"b-before"));
        const QVector<SMapSaveFile> batch{{a,"a-before","a-after",true},{b,"b-before","b-after",true}};
        const auto failed=commitMapSaveBatch(batch,dir.filePath("backups"),1);
        QVERIFY(!failed.success); QVERIFY(failed.rollbackComplete); QCOMPARE(failed.committedPaths.size(),1);
        QCOMPARE(read(a),QByteArray("a-before")); QCOMPARE(read(b),QByteArray("b-before"));
        const auto retried=commitMapSaveBatch(batch,dir.filePath("backups")); QVERIFY2(retried.success,qPrintable(retried.error));
        QCOMPARE(read(a),QByteArray("a-after")); QCOMPARE(read(b),QByteArray("b-after"));
    }
    void rollbackRefusesForeignBytesAndKeepsJournal() {
        QTemporaryDir dir(tempPattern()); const QString a=dir.filePath("a.res"),b=dir.filePath("b.res");
        QVERIFY(write(a,"a-before")); QVERIFY(write(b,"b-before")); QFile held(b); QVERIFY(held.open(QIODevice::ReadOnly));
        auto foreign=QtConcurrent::run([&] {
            QElapsedTimer timer;timer.start(); while(read(a)!="a-after" && timer.elapsed()<3000) QThread::msleep(1);
            return read(a)=="a-after" && write(a,"foreign");
        });
        const auto failed=commitMapSaveBatch({{a,"a-before","a-after",true},{b,"b-before","b-after",true}},dir.filePath("backups"));
        held.close(); QVERIFY(foreign.result()); QVERIFY(!failed.success); QVERIFY(!failed.rollbackComplete);
        QVERIFY(failed.error.contains("Recovery journal:")); QCOMPARE(read(a),QByteArray("foreign")); QCOMPARE(read(b),QByteArray("b-before"));
        QCOMPARE(QDir(dir.filePath("backups/map-save-recovery")).entryList({"*.json"},QDir::Files).size(),1);
    }
    void fourProductionThumbnailsAndHydrationDuringSave() {
        QTemporaryDir dir(tempPattern()); const QString path=dir.filePath("parallel.res");
        QImage page(64,64,QImage::Format_RGBA8888); page.fill(Qt::green);
        const QByteArray mmp=ei::encodeMmpDxt1(page,false);
        QImage tile; QString diagnostic; QVERIFY(ei::decodeMmpDxt1AtlasTile(mmp,0,&tile,&diagnostic));
        const QByteArray hash=CTerrainAtlasProfileInference::pixelSha256({tile.convertToFormat(QImage::Format_RGBA8888)});
        QVector<QPair<QByteArray,QByteArray>> members{{"sample.fig",figure()},{"stone.mmp",mmp},{"zone000.mmp",mmp},{"opaque.bin","before"}};
        for(int i=0;i<4;++i) {members.append({QByteArray("sample")+QByteArray::number(i)+".fig",figure()});
            members.append({QByteArray("stone")+QByteArray::number(i)+".mmp",mmp});}
        STerrainAtlasProfile profile;profile.mprNamespace="zone";profile.atlasPixelSha256=hash;
        profile.atlasSlotCount=1;profile.textureCount=1;profile.approved=true;
        STerrainAtlasProfileFamily family;family.id=1;family.name="Ground";family.thumbnailSlot=0;family.tileType=1;
        profile.families.append(family);
        STerrainAtlasProfileSlot slot;slot.slot=0;slot.assigned=true;
        slot.signature.northWest=slot.signature.northEast=slot.signature.southEast=slot.signature.southWest=1;
        profile.slotDefinitions.append(slot);QVERIFY(profile.normalizeMembership(nullptr));
        members.append({CTerrainAtlasProfileStore::embeddedMemberName("zone").toLatin1(),profile.toBytes()});
        const QByteArray before=archiveBytes(members);
        const QByteArray after=changedArchive(before); QVERIFY(!after.isEmpty()); QVERIFY(write(path,before));
        auto provider=CModelGallery::resourceThumbnails({path});
        QVERIFY(!provider("sample","stone").isNull()); // validate the cold fixture
        QSemaphore ready,start; QVector<QFuture<bool>> jobs;
        for(int i=0;i<5;++i) jobs.append(QtConcurrent::run([&,i] {
            // Deliberately hold an admitted file reader while publication starts;
            // the real provider/resolver run within that admitted read generation.
            resIo::ReadLease lease(path); QFile file(path); const bool open=lease.acquired()&&file.open(QIODevice::ReadOnly);
            ready.release(); start.acquire(); QThread::msleep(160); file.close();
            if(!open) return false;
            if(i<4) return !provider(QString("sample%1").arg(i),QString("stone%1").arg(i)).isNull();
            const auto binding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({path},"zone",path);
            return binding.success() && binding.decodedAtlasTiles.size()==1 && binding.decodedAtlasPixelSha256==hash;
        }));
        const bool allReady=ready.tryAcquire(5,5000);start.release(5);
        if(!allReady) {for(auto& job:jobs) job.waitForFinished();QFAIL("5 reader jobs did not become ready");}
        QElapsedTimer timer;timer.start(); const auto saved=commitMapSaveBatch({{path,before,after,true}},dir.filePath("backups"));
        int passed=0; for(auto& job:jobs) if(job.result()) ++passed;
        QCOMPARE(passed,5); QVERIFY2(saved.success,qPrintable(saved.error)); QVERIFY(timer.elapsed()>=100);
        QCOMPARE(read(path),after); QVERIFY(!provider("sample","stone").isNull()); // generation rebuild
        qInfo("parallel production consumers: 4/4 thumbnails + 1/1 hydration, drain %lld ms",timer.elapsed());
        // Now start fresh consumers AFTER the real batch writer is queued.
        // An admitted holder keeps that writer in drain until all jobs arrive.
        resIo::ReadLease held(path);QVERIFY(held.acquired());
        auto writer=std::async(std::launch::async,[&] {
            return commitMapSaveBatch({{path,after,before,true}},dir.filePath("backups"));
        });
        QElapsedTimer queued;queued.start();bool waiting=false;
        while(queued.elapsed()<1000) {
            auto& r=resIo::registry();QMutexLocker lock(&r.mutex);
            waiting=resIo::state(resIo::pathKey(path))->waitingWriters>0;
            if(waiting) break;
            lock.unlock();QThread::msleep(1);
        }
        QSemaphore lateReady;std::vector<std::future<bool>> late;
        for(int i=0;i<5;++i) late.push_back(std::async(std::launch::async,[&,i] {
            lateReady.release();
            bool ok=i<4 ? !provider(QString("sample%1").arg(i),QString("stone%1").arg(i)).isNull()
                : CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({path},"zone",path).success();
            CResArchiveIndex fresh;QString error;
            return ok && fresh.open(path,&error) && fresh.readMember("opaque.bin",&error)=="before";
        }));
        lateReady.acquire(5);QThread::msleep(60);
        int blocked=0;for(auto& job:late) if(job.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout) ++blocked;
        held.release();const auto lateSave=writer.get();
        int completed=0;for(auto& job:late) if(job.get()) ++completed;
        QVERIFY(waiting);QCOMPARE(blocked,5);QVERIFY2(lateSave.success,qPrintable(lateSave.error));QCOMPARE(completed,5);
        qInfo("new consumers during batch publication: 5/5 waited, 4/4 thumbnails + 1/1 hydration, 5/5 read new bytes");
    }
    void productionReadersWaitAndOldIndex() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("index.res");
        const QByteArray before=archiveBytes({{"opaque.bin","before"}});QVERIFY(write(path,before));
        CResArchiveIndex index;QString error;QVERIFY(index.open(path,&error));
        std::future<QByteArray> blocked;
        QSemaphore started;
        {
            resIo::PublicationLease lease(path);QVERIFY(lease.acquired());
            blocked=std::async(std::launch::async,[&] {
                CResArchiveIndex fresh;QString reason;
                started.release();
                return fresh.open(path,&reason) ? fresh.readMember("opaque.bin",&reason) : reason.toUtf8();
            });
            started.acquire();
            QCOMPARE(blocked.wait_for(std::chrono::milliseconds(60)),std::future_status::timeout);
            QSaveFile file(path);file.setDirectWriteFallback(false);QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray after=changedArchive(before);QCOMPARE(file.write(after),qint64(after.size()));QVERIFY(file.commit());
        }
        QCOMPARE(blocked.get(),QByteArray("after"));
        QVERIFY(!index.sourceIsCurrent(&error));QVERIFY(error.contains("generation changed"));
        QVERIFY(index.open(path,&error));QCOMPARE(index.readMember(0,&error),QByteArray("after"));
    }
    void guiReaderNeverWaitsForWorkerPublication() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("gui.res");QVERIFY(write(path,"before"));
        QSemaphore ready,release;
        auto writer=std::async(std::launch::async,[&] {
            resIo::PublicationLease publication(path);ready.release();release.acquire();return publication.acquired();
        });
        ready.acquire();QString error;QElapsedTimer timer;timer.start();
        resIo::ReadLease lease(path,&error);const bool acquired=lease.acquired();
        release.release();QVERIFY(writer.get());QVERIFY(!acquired);
        QVERIFY(error.contains("GUI read cannot wait"));QVERIFY(timer.elapsed()<1000);
        auto binding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({path},"zone");
        QVERIFY(!binding.success()); // invalid fixture, rather than admission failure
    }
    void resolverReportsAdmissionError() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("resolver.res");QVERIFY(write(path,"before"));
        QSemaphore ready,release;auto writer=std::async(std::launch::async,[&] {
            resIo::PublicationLease publication(path);ready.release();release.acquire();return publication.acquired();
        });
        ready.acquire();
        const auto embedded=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({path},"zone");
        const auto exact=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"zone",1,1,QByteArray(64,'a'));
        release.release();QVERIFY(writer.get());
        QVERIFY(embedded.error.contains("reader-admission"));QVERIFY(exact.error.contains("reader-admission"));
    }
    void admissionTimeoutThenRetry() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("retry-read.res");QVERIFY(write(path,"before"));
        std::future<QString> first;
        {
            resIo::PublicationLease writer(path);QVERIFY(writer.acquired());
            first=std::async(std::launch::async,[&] {
                QString error;resIo::ReadLease lease(path,&error,40);
                return lease.acquired()?QString():error;
            });
            const QString error=first.get();QVERIFY2(error.contains("reader-admission timeout"),qPrintable(error));
            QVERIFY(write(path,"after"));
        }
        auto retry=std::async(std::launch::async,[&] {
            QString error;resIo::ReadLease lease(path,&error,40);
            return lease.acquired()?read(path):error.toUtf8();
        });QCOMPARE(retry.get(),QByteArray("after"));
    }
    void thumbnailAdmissionTimeoutThenRetry() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("preview-timeout.res");
        QImage page(64,64,QImage::Format_RGBA8888);page.fill(Qt::green);
        QVERIFY(write(path,archiveBytes({{"sample.fig",figure()},{"stone.mmp",ei::encodeMmpDxt1(page,false)}})));
        auto provider=CModelGallery::resourceThumbnails({path});
        QString error;QElapsedTimer elapsed;elapsed.start();
        {
            resIo::PublicationLease publication(path);QVERIFY(publication.acquired());
            auto preview=std::async(std::launch::async,[&] {
                try { provider("sample","stone"); return QString(); }
                catch(const resIo::ReadFailure& failure) { return failure.error; }
            });
            error=preview.get();
        }
        QVERIFY2(error.contains("reader-admission timeout"),qPrintable(error));
        QVERIFY(elapsed.elapsed()>=9900);QVERIFY(elapsed.elapsed()<15000);
        auto retry=std::async(std::launch::async,[&] {return provider("sample","stone");});
        QVERIFY(!retry.get().isNull());
        qInfo("production thumbnail admission timeout: %lld ms; explicit exception; same provider retry succeeded",elapsed.elapsed());
    }
    void browserIndexAndPreviewWaitForPublication() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("browser.res");
        const QByteArray before=archiveBytes({{"notes.txt","old"}}),after=archiveBytes({{"notes.txt","new bytes after publication"}});
        QVERIFY(write(path,before));
        CResourceBrowser browser;browser.setAttribute(Qt::WA_DontShowOnScreen);
        browser.beginInternalPublication(path);
        auto* tree=browser.findChild<QTreeWidget*>("resourceContentView");
        QVERIFY(tree);
        {
            resIo::PublicationLease publication(path);QVERIFY(publication.acquired());
            browser.setActiveProfileSources({path},"test");QTest::qWait(60);
            QCOMPARE(tree->topLevelItemCount(),0);
            QVERIFY(write(path,after));
        }
        QElapsedTimer timer;timer.start();
        while(tree->topLevelItemCount()==0 && timer.elapsed()<3000) QTest::qWait(5);
        QCOMPARE(tree->topLevelItemCount(),1);
        browser.beginInternalPublication(path);
        {
            resIo::PublicationLease publication(path);QVERIFY(publication.acquired());
            QVERIFY(browser.selectResourceReference("notes.txt"));QTest::qWait(60);
            QVERIFY(write(path,before));
        }
        auto* text=browser.findChild<QPlainTextEdit*>();QVERIFY(text);
        timer.restart();while(!text->toPlainText().contains("old") && timer.elapsed()<3000) QTest::qWait(5);
        QVERIFY2(text->toPlainText().contains("old"),qPrintable(text->toPlainText()));
    }
    void galleryDeliveryErrorIsNotCachedAndRetries() {
        QTemporaryDir dir(tempPattern());ei::TechnicalModelCatalog catalog;QString error;
        QVERIFY(catalog.load(dir.filePath("catalog.json"),&error));
        catalog.registerModels({"sample"});catalog.bindTexture("test","sample","stone");
        ei::TechnicalTextureCatalog scan;std::atomic<int> calls{0};
        const QString identity=QUuid::createUuid().toString();
        const QString key=ei::galleryThumbnailKey(identity,"test","sample","stone");
        const QString directory=ei::galleryThumbnailDirectory();QImage diskImage;
        CModelGallery gallery({"sample"},&catalog,"test",{"stone"},&scan,
            [&](const QString&,const QString&) {
                if(++calls==1) throw resIo::ReadFailure("reader-admission timeout: review fixture");
                QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::green);return image;
            },nullptr,{}, {},identity);
        gallery.setAttribute(Qt::WA_DontShowOnScreen);gallery.show();
        auto* list=gallery.findChild<QListWidget*>("modelGalleryTiles");QVERIFY(list);QCOMPARE(list->count(),1);
        QElapsedTimer timer;timer.start();
        while(!list->item(0)->toolTip().contains("reader-admission timeout") && timer.elapsed()<3000) QTest::qWait(5);
        QVERIFY(list->item(0)->toolTip().contains("reader-admission timeout"));
        QCOMPARE(calls.load(),1);QVERIFY(list->item(0)->data(Qt::UserRole+2).isNull());
        QVERIFY(!ei::readGalleryThumbnail(directory,key,&diskImage));
        gallery.resize(gallery.width()+1,gallery.height());timer.restart();
        while(list->item(0)->icon().isNull() && timer.elapsed()<3000) QTest::qWait(5);
        QVERIFY(!list->item(0)->icon().isNull());QCOMPARE(calls.load(),2);
        QCOMPARE(list->item(0)->data(Qt::UserRole+2).toString(),key);
        QVERIFY(ei::readGalleryThumbnail(directory,key,&diskImage));QVERIFY(!diskImage.isNull());
        qInfo("delivery review: explicit error, no local/shared/disk failure cache, next request succeeds");
    }
    void coordinatorPublicationDrains() {
        QTemporaryDir dir(tempPattern());const QString path=dir.filePath("publish.res");
        const QByteArray before=archiveBytes({{"opaque.bin","before"}});QVERIFY(write(path,before));
        CResArchiveDocument doc;QStringList errors;QVERIFY(doc.loadBytes(before,path,&errors));
        SResPublishedMembersSaveRequest request;request.source=QFileInfo(path);request.destination=QFileInfo(path);
        SResArchiveNewMember member;member.name="added.bin";member.payload="new";request.published.append(member);request.backupRootOverride=dir.filePath("backups");
        QSemaphore ready;auto reader=QtConcurrent::run([&] {resIo::ReadLease lease(path);QFile file(path);
            bool ok=file.open(QIODevice::ReadOnly);ready.release();QThread::msleep(160);return ok;});
        ready.acquire();const auto saved=commitStagedResPublishedMembers(doc,request);QVERIFY(reader.result());
        QVERIFY2(saved.success,qPrintable(saved.error));QVERIFY(saved.committed);QCOMPARE(saved.commitAttempts,1);
        QVERIFY(doc.loadBytes(read(path),path,&errors));QCOMPARE(doc.memberPayload("added.bin"),QByteArray("new"));
    }
};

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    // Every temporary archive/backup is confined to the owned test tree.
    const QString root=QDir(QCoreApplication::applicationDirPath()).filePath("../fixtures-resdrain");
    if(!QDir().mkpath(root) || !QDir::setCurrent(root)) return 2;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root);
    QThreadPool::globalInstance()->setMaxThreadCount(8);
    ResReaderDrainTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "res_reader_drain_tests.moc"

#include "gallery_thumbnail_cache.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QtConcurrent>

class GalleryCacheTests : public QObject {
    Q_OBJECT
private slots:
    void identity() {
        const auto key=ei::galleryThumbnailKey("resource-size-mtime","profile","house","stone");
        QCOMPARE(key,ei::galleryThumbnailKey("resource-size-mtime","profile","HOUSE","STONE"));
        QCOMPARE(key,ei::galleryThumbnailKey("resource-size-mtime","profile","house","stone",1));
        QVERIFY(key!=ei::galleryThumbnailKey("new-resource-size-mtime","profile","house","stone"));
        QVERIFY(key!=ei::galleryThumbnailKey("resource-size-mtime","profile2","house","stone"));
        QVERIFY(key!=ei::galleryThumbnailKey("resource-size-mtime","profile","bridge","stone"));
        QVERIFY(key!=ei::galleryThumbnailKey("resource-size-mtime","profile","house","wood"));
        QVERIFY(key!=ei::galleryThumbnailKey("resource-size-mtime","profile","house","stone",2));
    }
    void roundtripAndCorruption() {
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto key=ei::galleryThumbnailKey("res","profile","house","stone");
        QImage image(192,132,QImage::Format_ARGB32);image.fill(QColor(17,33,77,255));
        QVERIFY(ei::writeGalleryThumbnail(temp.path(),key,image));
        QImage loaded;QVERIFY(ei::readGalleryThumbnail(temp.path(),key,&loaded));QCOMPARE(loaded,image);
        QFile file(temp.filePath(key+".thumb"));QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(file.size()-1));QCOMPARE(file.write("x"),qint64(1));file.close();
        QVERIFY(!ei::readGalleryThumbnail(temp.path(),key,&loaded));
        QVERIFY(ei::writeGalleryThumbnail(temp.path(),key,image));
        QVERIFY(file.open(QIODevice::ReadWrite));QVERIFY(file.resize(30));file.close();
        QVERIFY(!ei::readGalleryThumbnail(temp.path(),key,&loaded));
        QVERIFY(ei::writeGalleryThumbnail(temp.path(),key,image));
        QVERIFY(file.open(QIODevice::ReadWrite));QCOMPARE(file.write("BAD"),qint64(3));file.close();
        QVERIFY(!ei::readGalleryThumbnail(temp.path(),key,&loaded));
        QVERIFY(ei::writeGalleryThumbnail(temp.path(),key,image));
        QVERIFY(ei::readGalleryThumbnail(temp.path(),key,&loaded));QCOMPARE(loaded,image);
        QCOMPARE(QDir(temp.path()).entryList(QDir::Files).size(),1); // no abandoned temp files
    }
    void failedWriteAndUnavailable() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto key=ei::galleryThumbnailKey("res","p","m","t");
        QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::red);
        QFile blocker(temp.filePath("blocked"));QVERIFY(blocker.open(QIODevice::WriteOnly));blocker.write("keep");blocker.close();
        QVERIFY(!ei::writeGalleryThumbnail(blocker.fileName(),key,image));
        QVERIFY(!ei::writeGalleryThumbnail(temp.path(),key,QImage()));
        QVERIFY(!ei::writeGalleryThumbnail(temp.path(),"../escape",image));
        QVERIFY(!ei::writeGalleryThumbnail(temp.path(),key,QImage(8,8,QImage::Format_ARGB32)));
        QVERIFY(ei::writeGalleryThumbnail(temp.path(),key,image)); // failure does not poison future use
        QVERIFY(blocker.open(QIODevice::ReadOnly));QCOMPARE(blocker.readAll(),QByteArray("keep"));
    }
    void boundedAndIsolated() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto own=temp.filePath("gallery-thumbnails");
        QVERIFY(QDir().mkpath(own));QVERIFY(QDir().mkpath(temp.filePath("character-textures")));
        QFile foreign(temp.filePath("character-textures/keep"));QVERIFY(foreign.open(QIODevice::WriteOnly));foreign.write("foreign");foreign.close();
        QFile unowned(own+"/user.thumb");QVERIFY(unowned.open(QIODevice::WriteOnly));unowned.write("user");unowned.close();
        QImage image(192,132,QImage::Format_ARGB32);image.fill(Qt::green);
        const auto first=ei::galleryThumbnailKey("res","p","first","t");
        QVERIFY(ei::writeGalleryThumbnail(own,first,image));
        const qint64 entryBytes=QFileInfo(own+"/"+first+".thumb").size();
        QTest::qWait(20);
        const auto second=ei::galleryThumbnailKey("res","p","second","t");
        QVERIFY(ei::writeGalleryThumbnail(own,second,image));QTest::qWait(20);
        QImage loaded;QVERIFY(ei::readGalleryThumbnail(own,first,&loaded));QTest::qWait(20);
        const auto third=ei::galleryThumbnailKey("res","p","third","t");
        QVERIFY(ei::writeGalleryThumbnail(own,third,image,2*entryBytes));
        QVERIFY(ei::readGalleryThumbnail(own,first,&loaded));
        QVERIFY(!ei::readGalleryThumbnail(own,second,&loaded));
        QVERIFY(ei::readGalleryThumbnail(own,third,&loaded));
        QVERIFY(foreign.open(QIODevice::ReadOnly));QCOMPARE(foreign.readAll(),QByteArray("foreign"));
        QVERIFY(unowned.open(QIODevice::ReadOnly));QCOMPARE(unowned.readAll(),QByteArray("user"));
    }
    void concurrentAtomicRecords() {
        QTemporaryDir temp;QVERIFY(temp.isValid());QVector<QFuture<bool>> futures;
        const auto key=ei::galleryThumbnailKey("res","p","m","t");const QString directory=temp.path();
        for(int i=0;i<16;++i) futures.append(QtConcurrent::run([=]{
            QImage image(192,132,QImage::Format_ARGB32);image.fill(i%2?Qt::red:Qt::blue);
            if(!ei::writeGalleryThumbnail(directory,key,image))return false;
            QImage loaded;if(!ei::readGalleryThumbnail(directory,key,&loaded))return false;
            const auto color=loaded.pixelColor(0,0);if(color!=QColor(Qt::red)&&color!=QColor(Qt::blue))return false;
            for(int y=0;y<loaded.height();++y)for(int x=0;x<loaded.width();++x)if(loaded.pixelColor(x,y)!=color)return false;
            return true;
        }));
        for(auto& future:futures)QVERIFY(future.result());
        QCOMPARE(QDir(temp.path()).entryList(QDir::Files).size(),1);
    }
};
QTEST_GUILESS_MAIN(GalleryCacheTests)
#include "gallery_thumbnail_cache_tests.moc"

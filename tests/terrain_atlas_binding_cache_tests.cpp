#include <QtTest>
#include <QtEndian>
#include <QTemporaryDir>
#include <QFile>
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_profile.h"
#include "res_archive_document.h"
#include "mmp_codec.h"

class TerrainAtlasBindingCacheTests : public QObject {
    Q_OBJECT
    static QByteArray page(int tileSize,const QColor& color) {
        QImage raster(tileSize*8,tileSize*8,QImage::Format_RGBA8888);raster.fill(color);
        return ei::encodeMmpDxt1(raster,true);
    }
    static QVector<QImage> pixels(const QVector<QByteArray>& pages) {
        QVector<QImage> result;
        for(const auto& bytes:pages)for(int slot=0;slot<64;++slot) {
            QImage image;if(!ei::decodeMmpDxt1AtlasTile(bytes,slot,&image))return {};
            result.append(image.convertToFormat(QImage::Format_RGBA8888));
        }
        return result;
    }
    static bool archive(const QString& path,const QVector<QByteArray>& pages,bool alias=false) {
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));
        qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument doc;if(!doc.loadBytes(empty))return false;
        QVector<SResArchiveNewMember> members;
        for(const auto& ns:alias?QStringList{"cache","alias"}:QStringList{"cache"})
            for(int i=0;i<pages.size();++i) {
                SResArchiveNewMember member;member.name=QStringLiteral("%1%2.mmp").arg(ns).arg(i,3,10,QLatin1Char('0'));
                member.payload=pages[i];members.append(member);
            }
        QByteArray bytes;if(!doc.buildWithAppendedMembers(members,&bytes))return false;
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
private slots:
    void differentExpectedHashesAndAliasesRetainExactSourceProof() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto bytes=page(256,QColor(120,91,54));
        const auto expected=pixels({bytes});const auto hash=CTerrainAtlasProfileInference::pixelSha256(expected);
        const auto path=temp.filePath("source.res");QVERIFY(archive(path,{bytes},true));
        const auto rejected=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,QByteArray(64,'0'));
        QVERIFY(!rejected.success());QCOMPARE(rejected.completeArchivePixelSha256.value(path),hash);
        const auto other=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"alias",1,64,QByteArray(64,'1'));
        QVERIFY(!other.success());QCOMPARE(other.completeArchivePixelSha256.value(path),hash);
        const auto accepted=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"alias",1,64,hash);
        QVERIFY2(accepted.success(),qPrintable(accepted.error));QCOMPARE(accepted.decodedAtlasPixelSha256,hash);
        QVector<QImage> images;
        const auto decoded=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,hash,{},&images);
        QVERIFY(decoded.success());QCOMPARE(images,expected);
    }
    void sameSizeAndTimestampPayloadChangeCannotReuseOldProof() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto bytes=page(256,QColor(70,180,55));
        const auto hash=CTerrainAtlasProfileInference::pixelSha256(pixels({bytes}));const auto path=temp.filePath("source.res");
        QVERIFY(archive(path,{bytes}));QVERIFY(CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,hash).success());
        const QFileInfo before(path);const auto modified=before.lastModified();const auto size=before.size();
        QFile file(path);QVERIFY(file.open(QIODevice::ReadWrite));const auto original=file.readAll();
        CResArchiveDocument doc;QVERIFY(doc.loadBytes(original));const auto member=doc.members().first();
        QVERIFY(file.seek(member.dataOffset+76));QByteArray altered(8,'\0');altered[1]=char(0xf8); // one valid red BC1 block
        QCOMPARE(file.write(altered),qint64(altered.size()));QVERIFY(file.flush());QVERIFY(file.setFileTime(modified,QFileDevice::FileModificationTime));file.close();
        QCOMPARE(QFileInfo(path).size(),size);QCOMPARE(QFileInfo(path).lastModified().toMSecsSinceEpoch(),modified.toMSecsSinceEpoch());
        QVERIFY(file.open(QIODevice::ReadOnly));CResArchiveDocument changed;QVERIFY(changed.loadBytes(file.readAll()));file.close();
        const auto changedHash=CTerrainAtlasProfileInference::pixelSha256(pixels({changed.memberPayload("cache000.mmp")}));
        QVERIFY(changedHash!=hash);
        const auto rejected=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,hash);
        QVERIFY(!rejected.success());const auto newHash=rejected.completeArchivePixelSha256.value(path);
        QVERIFY(!newHash.isEmpty());QVERIFY(newHash!=hash);QCOMPARE(newHash,changedHash);
        QVector<QImage> fresh;const auto accepted=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,newHash,{},&fresh);
        QVERIFY(accepted.success());QCOMPARE(CTerrainAtlasProfileInference::pixelSha256(fresh),newHash);
    }
    void mutableReturnedPixelsCannotPoisonFutureBindings() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto bytes=page(256,QColor(61,110,220));
        const auto expected=pixels({bytes});const auto hash=CTerrainAtlasProfileInference::pixelSha256(expected);const auto path=temp.filePath("source.res");
        QVERIFY(archive(path,{bytes}));QVector<QImage> first,second;
        QVERIFY(CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,hash,{},&first).success());
        first[0].setPixelColor(0,0,Qt::red);QVERIFY(first!=expected);
        QVERIFY(CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,hash,{},&second).success());
        QCOMPARE(second,expected);QCOMPARE(CTerrainAtlasProfileInference::pixelSha256(second),hash);
    }
    void nativeWaterAndDifferentPhysicalCountsRemainDistinct() {
        QTemporaryDir temp;QVERIFY(temp.isValid());const QVector<QByteArray> bytes{page(256,QColor(161,80,20)),page(128,QColor(18,120,180))};
        const auto expected=pixels(bytes);const auto allHash=CTerrainAtlasProfileInference::pixelSha256(expected);
        const auto halfHash=CTerrainAtlasProfileInference::pixelSha256(expected.mid(0,64));QVERIFY(halfHash!=allHash);
        const auto path=temp.filePath("source.res");QVERIFY(archive(path,bytes));
        QVERIFY(CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",2,128,allHash).success());
        const auto wrong=CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,allHash);QVERIFY(!wrong.success());
        QVector<QImage> images;QVERIFY(CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",2,128,allHash,{},&images).success());
        QCOMPARE(images,expected);QCOMPARE(images.first().size(),QSize(256,256));QCOMPARE(images.last().size(),QSize(128,128));
        QVERIFY(CTerrainAtlasArchiveBindingResolver::findExactArchive({path},"cache",1,64,halfHash).success());
    }
};
QTEST_APPLESS_MAIN(TerrainAtlasBindingCacheTests)
#include "terrain_atlas_binding_cache_tests.moc"

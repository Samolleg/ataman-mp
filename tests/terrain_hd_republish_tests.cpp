#include <QtTest>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QtEndian>
#include "terrain_hd_republish.h"
#include "terrain_virtual_atlas.h"
#include "mmp_codec.h"

namespace {
QImage pattern(int n)
{
    QImage image(256,256,QImage::Format_RGBA8888);
    for(int y=0;y<256;++y)for(int x=0;x<256;++x)
        image.setPixel(x,y,qRgb(n?25+(x%17):130+(x%17),n?130+(y%17):35+(y%17),30+(x+y)%31));
    return image;
}
QByteArray lowMip(const QByteArray& hd)
{
    QByteArray low=hd.left(76)+hd.mid(76+2048*2048/2);
    qToLittleEndian<quint32>(1024,reinterpret_cast<uchar*>(low.data()+4));
    qToLittleEndian<quint32>(1024,reinterpret_cast<uchar*>(low.data()+8));
    qToLittleEndian<quint32>(11,reinterpret_cast<uchar*>(low.data()+12));return low;
}
STerrainAtlasProfile profile(const QVector<QImage>& images)
{
    STerrainAtlasProfile p;p.mprNamespace="published";p.approved=true;p.atlasSlotCount=64;p.textureCount=1;
    p.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
    for(int id=1;id<=2;++id){STerrainAtlasProfileFamily f;f.id=id;f.name=id==1?"Grass":"Soil";f.tileType=id;p.families.append(f);}
    for(int slot=0;slot<64;++slot){STerrainAtlasProfileSlot d;d.slot=slot;const int id=slot==1?2:1;
        d.signature.northWest=d.signature.northEast=d.signature.southEast=d.signature.southWest=id;p.slotDefinitions.append(d);}
    return p;
}
}
class TerrainHdRepublishTests:public QObject
{
    Q_OBJECT
private slots:
    void originalBlocksAndDeterminism()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));tiles[1]=pattern(1);
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QVERIFY(!hd.isEmpty());
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;
        QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res";QSaveFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));QVERIFY(file.commit());
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open({path},&error),qPrintable(error));
        const QByteArray sd=lowMip(hd);QVector<QImage> before;
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));before.append(image);}
        auto p=profile(before);const auto first=rebuildTerrainAtlasHd(p,{sd},sources);
        QVERIFY2(first.success,qPrintable(first.error));QCOMPARE(first.receipt["hdApplied"].toInt(),64);QCOMPARE(first.receipt["retained128"].toInt(),0);
        // All slots come from this same page: exclusively owned mip blocks
        // must be its original BC1 bytes, not merely similar decoded pixels.
        QCOMPARE(first.pages[0].mid(76,2048*2048/2),hd.mid(76,2048*2048/2));
        const auto repeat=rebuildTerrainAtlasHd(p,{sd},sources);QCOMPARE(repeat.pages,first.pages);QCOMPARE(repeat.profile.toBytes(),first.profile.toBytes());
        STerrainTransitionSignature sig;sig.northWest=sig.northEast=1;sig.southWest=sig.southEast=2;
        QMap<qint32,QImage> lowSources{{1,before[0]},{2,before[1]}};
        QMap<qint32,QImage> highSources{{1,first.after[0]},{2,first.after[1]}};
        QMap<qint32,ETerrainBlendStyle> styles{{1,ETerrainBlendStyle::Grass},{2,ETerrainBlendStyle::Soil}};
        CTerrainTransitionCatalog noAuthored;
        CTerrainVirtualAtlas old(noAuthored,{before[0]},0,lowSources,styles,true);
        before[2]=old.reviewGeneratedBlend(sig,0,nullptr,nullptr,1);QVERIFY(!before[2].isNull());
        const auto generatedSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(before,0,128),true,nullptr);
        QVector<QImage> decoded;for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(generatedSd,slot,&image));decoded.append(image);}
        p=profile(decoded);p.slotDefinitions[2].signature=CTerrainTransitionCatalog::mirroredGridY(sig);p.slotDefinitions[2].routeOwnerMaterial=1;
        const auto upgraded=rebuildTerrainAtlasHd(p,{generatedSd},sources);QVERIFY2(upgraded.success,qPrintable(upgraded.error));
        QCOMPARE(upgraded.receipt["regeneratedApplied"].toInt(),1);QCOMPARE(upgraded.after[2].size(),QSize(256,256));
        QCOMPARE(upgraded.profile.slotDefinitions[2].signature,p.slotDefinitions[2].signature);
        QCOMPARE(upgraded.profile.slotDefinitions[2].routeOwnerMaterial,qint32(1));
        CTerrainVirtualAtlas next(noAuthored,{first.after[0]},0,highSources,styles,true);
        const auto expected=next.reviewGeneratedBlend(sig,0,nullptr,nullptr,1);
        const auto encoded=ei::encodeMmpDxt1(expected,false,nullptr);QImage expectedDecoded;
        QVERIFY(ei::decodeDxtImage(encoded.mid(76),256,256,ei::DxtFormat::Dxt1,&expectedDecoded));QCOMPARE(upgraded.after[2],expectedDecoded);
    }
    void missingSourceKeepsOriginalPage()
    {
        QVector<QImage> tiles(64,pattern(0).scaled(128,128));
        const auto sd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,128),true,nullptr);QVector<QImage> before;
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));before.append(image);}
        CTerrainHdSources empty;auto result=rebuildTerrainAtlasHd(profile(before),{sd},empty);
        QVERIFY(result.success);QCOMPARE(result.pages[0],sd);QCOMPARE(result.receipt["retained128"].toInt(),64);QCOMPARE(result.profile.atlasPixelSha256,CTerrainAtlasProfileInference::pixelSha256(before));
    }
};
QTEST_GUILESS_MAIN(TerrainHdRepublishTests)
#include "terrain_hd_republish_tests.moc"

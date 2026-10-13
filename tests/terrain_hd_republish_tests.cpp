#include <QtTest>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QtEndian>
#include <QDataStream>
#include <QTransform>
#include "terrain_hd_republish.h"
#include "terrain_hd_library.h"
#include "terrain_hd_variant_bank.h"
#include "terrain_virtual_atlas.h"
#include "mmp_codec.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "terrain_semantic_field_store.h"
#include "terrain_mixed_edge.h"
#include <QCryptographicHash>
#include "terrain_hd_progress_dialog.h"
#include "verified_clipboard.h"
#include <QtConcurrent>
#include "ui_theme.h"
#include <QStyleFactory>
#include <QCoreApplication>
#include <cstring>
#include <vector>

namespace {
int headlessTestEntries=0;
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
QByteArray mapBytes()
{
    QByteArray mp;QDataStream out(&mp,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out<<quint32(0xCE4AF672)<<float(100)<<quint32(1)<<quint32(1)<<quint32(1)<<quint32(1024)<<quint32(64)<<quint32(128)<<quint16(0)<<quint32(1);
    for(int i=0;i<64;++i)out<<qint32(i==1?2:1);out<<quint16(60)<<quint16(4);
    QByteArray sector;QDataStream sec(&sector,QIODevice::WriteOnly);sec.setByteOrder(QDataStream::LittleEndian);sec<<quint32(0xCF4BF774)<<quint8(0);
    sector.append(QByteArray(33*33*8+16*16*2,'\0'));
    for(int i=0;i<256;++i){SMprPackedTile t;t.tileIndex=i%64;t.rotation=i%4;qToLittleEndian<quint16>(t.encode(),reinterpret_cast<uchar*>(sector.data()+5+33*33*8+i*2));}
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument res;res.loadBytes(empty);QByteArray bytes;SResArchiveNewMember a,b;a.name="published.mp";a.payload=mp;b.name="published000000.sec";b.payload=sector;
    res.buildWithAppendedMembers({a,b},&bytes);return bytes;
}
}
class TerrainHdRepublishTests:public QObject
{
    Q_OBJECT
private slots:
    void nativeArtworkStillPublishesLegacyMapDimensions()
    {
        ++headlessTestEntries;CMprDocument map;QVERIFY(map.loadBytes(mapBytes(),"original.mpr"));
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(QVector<QImage>(64,pattern(0)),0,256),true,nullptr);
        QVector<QImage> images;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(hd,i,&image));images.append(image);}
        CTerrainHdSources sources;auto result=rebuildTerrainAtlasHd(profile(images),{hd},sources);
        QVERIFY2(result.success,qPrintable(result.error));QCOMPARE(result.receipt["hdApplied"].toInt(),0);
        QVERIFY(terrainHdPublicationNeeded(map,result));QVERIFY(prepareTerrainHdPublication(map,{},&result));
        QVERIFY(!result.publishedMembers.isEmpty());CMprDocument saved;QVERIFY(saved.loadBytes(result.mprBytes,"saved.mpr"));
        QCOMPARE(saved.mapInfo().tileSize,quint32(256));QCOMPARE(saved.mapInfo().textureSize,quint32(2048));
        auto repeat=rebuildTerrainAtlasHd(result.profile,result.pages,sources);
        QVERIFY(repeat.success);QVERIFY(!terrainHdPublicationNeeded(saved,repeat));
        QVERIFY(prepareTerrainHdPublication(saved,{},&repeat));QVERIFY(repeat.receipt["noOp"].toBool());
        QCOMPARE(repeat.mprBytes,saved.originalBytes());
    }
    void localLibrarySurvivesGameChangesAndCancellation()
    {
        ++headlessTestEntries;QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(QVector<QImage>(64,pattern(0)),0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember terrain,unrelated;
        terrain.name="stock000.mmp";terrain.payload=hd;unrelated.name="unrelated-model.bin";unrelated.payload="not terrain";QByteArray bytes;
        QVERIFY(archive.buildWithAppendedMembers({terrain,unrelated},&bytes));
        const QString game=dir.filePath("game.res"),library=dir.filePath("hd-library");QFile file(game);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        QStringList local;QString error;CTerrainHdProgress cancelled;cancelled.cancel();
        QVERIFY(!TerrainHdLibrary::ensure({game},&local,&error,&cancelled,library));QVERIFY(!QFileInfo::exists(library+"/sources"));
        QVERIFY2(TerrainHdLibrary::ensure({game},&local,&error,nullptr,library),qPrintable(error));QCOMPARE(local.size(),1);
        CResArchiveIndex imported;QVERIFY(imported.open(local.first()));QCOMPARE(imported.readMember(terrain.name),hd);QVERIFY(imported.readMember(unrelated.name).isEmpty());
        CTerrainHdSources first;QVERIFY2(first.open(local,&error,nullptr,library+"/index"),qPrintable(error));QVERIFY(first.saveCache());QVERIFY(!first.cacheHit());
        QVERIFY(QFile::remove(game));QStringList repeated;QVERIFY(TerrainHdLibrary::ensure({game},&repeated,&error,nullptr,library));QCOMPARE(repeated,local);
        CTerrainHdSources next;QVERIFY(next.open(repeated,&error,nullptr,library+"/index"));QVERIFY(next.cacheHit());
        CTerrainHdSources::Source donor;QVERIFY(next.find("stock",0,&donor));QCOMPARE(next.payload(donor),hd);
        // Painting validates local pages/inputs, without the deleted game RES.
        const QString bankId(64,'a'),bankRoot=library+"/variants",bankDirectory=bankRoot+"/"+bankId;
        QVERIFY(QDir().mkpath(bankDirectory));
        auto put=[](const QString& path,const QByteArray& bytes){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();};
        auto hash=[](const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());};
        QFile presets(":/terrain_atlas_presets.json");QVERIFY(presets.open(QIODevice::ReadOnly));
        const QJsonObject manifest{{"algorithm",TerrainHdVariants::AlgorithmVersion},{"id",bankId},{"presetSha",hash(presets.readAll())},
            {"inputs",QJsonArray{QJsonObject{{"path","@library/sources/"+QFileInfo(local.first()).fileName()},{"member",terrain.name},{"sha256",hash(hd)}}}},
            {"pages",QJsonArray{QJsonObject{{"file","variants000.mmp"},{"sha256",hash(hd)}}}},
            {"entries",QJsonArray{QJsonObject{{"slot",0},{"id","fixture"},{"material","Grass"},{"reviewedClass",0},{"physicalClass",0},{"layer","land"}}}}};
        QVERIFY(put(bankDirectory+"/variants000.mmp",hd));QVERIFY(put(bankDirectory+"/manifest.json",QJsonDocument(manifest).toJson()));
        QVERIFY(put(bankRoot+"/current.json",QJsonDocument(QJsonObject{{"id",bankId}}).toJson()));
        const auto bank=TerrainHdVariantBank::load(bankRoot);QVERIFY2(bank.success,qPrintable(bank.error));QCOMPARE(bank.entries.size(),1);
        const QString moved=dir.filePath("moved-library");QVERIFY(QDir().rename(library,moved));
        QVERIFY(TerrainHdLibrary::ensure({},&local,&error,nullptr,moved));
        CTerrainHdSources relocated;QVERIFY(relocated.open(local,&error,nullptr,moved+"/index"));QVERIFY(relocated.cacheHit());
        const auto movedBank=TerrainHdVariantBank::load(moved+"/variants");QVERIFY2(movedBank.success,qPrintable(movedBank.error));QCOMPARE(movedBank.entries.first().blocks,bank.entries.first().blocks);
        QFile broken(local.first());QVERIFY(broken.open(QIODevice::WriteOnly|QIODevice::Truncate));broken.write("broken");broken.close();
        QVERIFY(!TerrainHdLibrary::ensure({},&repeated,&error,nullptr,moved));
    }
    void lowContrastTransitionKeepsReviewedTopology_data()
    {
        QTest::addColumn<int>("materials");
        for(int n:{2,3,4})QTest::newRow(qPrintable(QString::number(n)))<<n;
    }
    void lowContrastTransitionKeepsReviewedTopology()
    {
        QFETCH(int,materials);
        ++headlessTestEntries;QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));tiles[1]=pattern(1);
        const int donorSlots[]={0,1,3,4};
        for(int id=0;id<materials;++id)for(int y=0;y<256;++y)for(int x=0;x<256;++x)
            tiles[donorSlots[id]].setPixel(x,y,qRgb(56+id*8+(x%17)/2,88-id*8+(y%17)/2,24+id*3));
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));
        qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));
        SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;QByteArray res;
        QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(res),qint64(res.size()));file.close();
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open({path},&error),qPrintable(error));
        QVector<QImage> low;const auto sd=lowMip(hd);
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));low.append(image);}
        // No usable color/texture evidence remains in this authored carrier.
        // Its reviewed semantic corners still distinguish the two materials.
        low[2].fill(qRgb(100,100,100));
        const auto authored=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(low,0,128),true,nullptr);
        for(int slot=0;slot<64;++slot)QVERIFY(ei::decodeMmpDxt1AtlasTile(authored,slot,&low[slot]));
        auto p=profile(low);auto& s=p.slotDefinitions[2].signature;
        for(int id=3;id<=materials;++id) {
            STerrainAtlasProfileFamily family;family.id=id;family.name=QString("Grass %1").arg(id);family.tileType=id;
            p.families.append(family);
            auto& donor=p.slotDefinitions[donorSlots[id-1]].signature;
            donor.northWest=donor.northEast=donor.southEast=donor.southWest=id;
        }
        s.northWest=1;s.northEast=2;s.southEast=qMin(materials,3);s.southWest=materials;
        const auto result=rebuildTerrainAtlasHd(p,{authored},sources);
        QVERIFY2(result.success,qPrintable(result.error));
        QVERIFY(result.receipt["allStaticSlotsTrueHd"].toBool());
        const auto row=result.receipt["slots"].toArray()[2].toObject();
        QCOMPARE(row["result"].toString(),QStringLiteral("regenerated-256"));
        if(materials>=3)QCOMPARE(row["recipe"].toObject()["materialCount"].toInt(),materials);
        const auto output=result.after[result.slotRemap.value(2)];
        QCOMPARE(output.size(),QSize(256,256));
        QVERIFY(output!=low[2].scaled(256,256));
        QVERIFY(output.pixel(8,8)!=output.pixel(247,247));
        if(materials>=3) {
            p.approved=false;
            const auto unreviewed=rebuildTerrainAtlasHd(p,{authored},sources);
            QVERIFY(!unreviewed.success||!unreviewed.receipt["allStaticSlotsTrueHd"].toBool());
        }
    }
    void multiMaterialJunctionUsesHdPixels()
    {
        ++headlessTestEntries;QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));const int donorSlots[]={0,1,3,4};
        for(int id=0;id<4;++id)for(int y=0;y<256;++y)for(int x=0;x<256;++x)
            tiles[donorSlots[id]].setPixel(x,y,qRgb(180+id*10+((x+y)%2?6:-6),170,130));
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));file.close();
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open({path},&error),qPrintable(error));
        QVector<QImage> low;const auto sd=lowMip(hd);for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));low.append(image);}
        for(int y=0;y<128;++y)for(int x=0;x<128;++x){const int id=y<64?(x<64?0:1):(x<64?3:2);low[2].setPixel(x,y,low[donorSlots[id]].pixel(x,y));}
        const auto authored=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(low,0,128),true,nullptr);for(int slot=0;slot<64;++slot)QVERIFY(ei::decodeMmpDxt1AtlasTile(authored,slot,&low[slot]));
        auto p=profile(low);for(int id=3;id<=4;++id){auto f=p.families[0];f.id=id;f.name=QStringLiteral("Material %1").arg(id);f.tileType=id;p.families.append(f);}
        for(int id=0;id<4;++id){auto& s=p.slotDefinitions[donorSlots[id]].signature;s.northWest=s.northEast=s.southEast=s.southWest=id+1;}
        auto& s=p.slotDefinitions[2].signature;s.northWest=1;s.northEast=2;s.southEast=3;s.southWest=4;
        const auto result=rebuildTerrainAtlasHd(p,{authored},sources);QVERIFY2(result.success,qPrintable(result.error));
        const auto row=result.receipt["slots"].toArray()[2].toObject();QCOMPARE(row["result"].toString(),QStringLiteral("regenerated-256"));
        QCOMPARE(row["recipe"].toObject()["sources"].toObject().size(),4);
        QVERIFY(result.after[result.slotRemap[2]]!=low[2].scaled(256,256,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
    }
    void textureOwnershipUsesHdPixels()
    {
        ++headlessTestEntries;
        QTemporaryDir dir;QVERIFY(dir.isValid());QVector<QImage> tiles(64,pattern(0));
        for(int id=0;id<2;++id)for(int y=0;y<256;++y)for(int x=0;x<256;++x) {
            // Similar average colors, but paving carries a strong mortar grid.
            const int delta=id?((x%16<2||y%16<2)?-40:14):(x+y)%5-2;
            tiles[id].setPixel(x,y,qRgb(200+delta,180+delta,140+delta));
        }
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));file.close();
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open({path},&error),qPrintable(error));
        QVector<QImage> low;const auto sd=lowMip(hd);for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));low.append(image);}
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)low[2].setPixel(x,y,y<64+int(8*std::sin(x*.08))?low[0].pixel(x,y):low[1].pixel(x,y));
        const auto authored=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(low,0,128),true,nullptr);
        for(int slot=0;slot<64;++slot)QVERIFY(ei::decodeMmpDxt1AtlasTile(authored,slot,&low[slot]));
        auto p=profile(low);auto& signature=p.slotDefinitions[2].signature;signature.northWest=signature.northEast=1;signature.southWest=signature.southEast=2;
        const auto result=rebuildTerrainAtlasHd(p,{authored},sources);
        QVERIFY2(result.success,qPrintable(result.error));const auto row=result.receipt["slots"].toArray()[2].toObject();
        QCOMPARE(row["result"].toString(),QStringLiteral("regenerated-256"));
        QVERIFY(result.after[result.slotRemap[2]]!=low[2].scaled(256,256,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
    }
    void semanticTransitionUsesHdPixels()
    {
        ++headlessTestEntries;
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));tiles[1]=pattern(1);
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;
        QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));file.close();
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open({path},&error),qPrintable(error));
        const auto sd=lowMip(hd);QVector<QImage> low;
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));low.append(image);}
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)low[2].setPixel(x,y,(x<64+int(12*std::sin(y*.09)))?low[0].pixel(x,y):low[1].pixel(x,y));
        const auto authored=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(low,0,128),true,nullptr);
        for(int slot=0;slot<64;++slot)QVERIFY(ei::decodeMmpDxt1AtlasTile(authored,slot,&low[slot]));
        auto p=profile(low);p.slotDefinitions[2].ambiguous=true;
        p.slotDefinitions[2].signature.northWest=3;p.slotDefinitions[2].signature.southEast=4;
        CMprDocument initial,map;QVERIFY(initial.loadBytes(mapBytes(),"initial.mpr"));
        auto sector=initial.members()[initial.sectors()[0].memberIndex];QByteArray payload=sector.payload;
        for(int cell=0;cell<256;++cell){SMprPackedTile tile;tile.tileIndex=cell==0?2:0;tile.rotation=cell==0?3:0;qToLittleEndian<quint16>(tile.encode(),reinterpret_cast<uchar*>(payload.data()+initial.sectors()[0].landTileOffset+cell*2));}
        CMprEditTransaction edit;edit.replaceMemberPayload(sector.name,sector.sha256,payload);QByteArray bytes;QVERIFY(initial.applyTransaction(edit,&bytes));QVERIFY(map.loadBytes(bytes,"context.mpr"));
        STerrainTransitionSignature physical;physical.northWest=physical.southWest=1;physical.northEast=physical.southEast=2;
        const auto grid=CTerrainTransitionCatalog::rotated(CTerrainTransitionCatalog::mirroredGridY(physical),3);
        STerrainSemanticFieldDocument field;field.mprNamespace=p.mprNamespace;field.vertexWidth=field.vertexHeight=17;field.land=QVector<qint32>(289,1);field.overlay=QVector<qint32>(289,0);
        field.land[0]=grid.northWest;field.land[1]=grid.northEast;field.land[17]=grid.southWest;field.land[18]=grid.southEast;
        field.atlasPixelSha256=QByteArray::fromHex(p.atlasPixelSha256);field.profileSha256=QCryptographicHash::hash(p.toBytes(),QCryptographicHash::Sha256);field.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(map);
        const auto result=rebuildTerrainAtlasHd(p,{authored},sources,true,map.mapInfo().animatedTiles,{},nullptr,&map,field.toBytes());
        QVERIFY2(result.success,qPrintable(result.error));
        const auto row=result.receipt["slots"].toArray()[2].toObject();QCOMPARE(row["result"].toString(),QStringLiteral("regenerated-256"));
        QVERIFY(row["recipe"].toObject()["sources"].toObject().size()==2);
        QVERIFY(result.after[result.slotRemap[2]]!=low[2].scaled(256,256,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));
        for(int slot=60;slot<64;++slot)QCOMPARE(result.after[slot],low[slot]);
        auto published=result;QVERIFY2(prepareTerrainHdPublication(map,field.toBytes(),&published),qPrintable(published.error));
        CMprDocument reopened;QVERIFY(reopened.loadBytes(published.mprBytes,"published.mpr"));QCOMPARE(reopened.mapInfo().animatedTiles,map.mapInfo().animatedTiles);
        field.profileSha256=QByteArray(32,'x');
        const auto rejected=rebuildTerrainAtlasHd(p,{authored},sources,true,map.mapInfo().animatedTiles,{},nullptr,&map,field.toBytes());
        QCOMPARE(rejected.receipt["semanticContextDiagnostic"].toString(),QStringLiteral("semantic-field-binding-mismatch"));
    }
    void initTestCase()
    {
        // Keep the user's LocalAppData bank read-only and out of synthetic
        // assertions; these tests exercise same-material source selection.
        qputenv("EI_MAPPER_HD_VARIANT_CACHE",QByteArray("build-tests-hdall-no-bank"));
    }
    void stockStoneCopies()
    {
        const QString fixture=qEnvironmentVariable("EI_STONE_HD_FIXTURE");
        if(fixture.isEmpty())QSKIP("Private stock fixture not configured");
        QFile file(fixture);QVERIFY(file.open(QIODevice::ReadOnly));const auto cfg=QJsonDocument::fromJson(file.readAll()).object();
        QStringList archives;for(const auto& value:cfg["sources"].toArray())archives.append(value.toString());
        CTerrainHdSources sources;QString error;QVERIFY2(sources.open(archives,&error,nullptr,cfg["cache"].toString()),qPrintable(error));
        CResArchiveIndex stock;QVERIFY(stock.open(archives.last(),&error));
        QVector<QImage> tiles;
        for(int page=0;page<8;++page) {
            const auto payload=stock.readMember(QString("zone19%1.mmp").arg(page,3,10,QLatin1Char('0')),&error);
            for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(payload,slot,&image));tiles.append(image);}
        }
        const auto p=CTerrainAtlasPresetCatalog::exactProfile("zone19",CTerrainAtlasProfileInference::pixelSha256(tiles),tiles.size());
        QVERIFY(p.approved);QJsonArray rows;
        for(int slot:{43,44,45}) {
            STerrainAtlasProfileSlot definition;for(const auto& d:p.slotDefinitions)if(d.slot==slot)definition=d;
            STerrainAtlasProfileFamily family;for(const auto& f:p.families)if(f.id==definition.signature.northWest)family=f;
            QCOMPARE(family.name,QString("Stone / Desert"));QCOMPARE(family.tileType,3);
            CTerrainHdSources::Source source;QString reason;QJsonObject evidence;
            QVERIFY2(sources.match(tiles[slot],definition,&source,&reason,&evidence,&family),qPrintable(reason));
            QCOMPARE(source.page,QString("bz16h007.mmp"));QCOMPARE(source.slot,15);QCOMPARE(source.rotation,0);
            const auto high=sources.image(source,256);QCOMPARE(high.size(),QSize(256,256));
            QCOMPARE(terrainProvenancePixelSha(high),QByteArray("d5855c1f92eca2b4e85d6f539fb616de44e2ce4a650dd7eaf25d24cc99737bcb"));
            QCOMPARE(evidence["method"].toString(),slot==44?QString("exact-SD-alias"):QString("owner-confirmed-provenance"));
            evidence["stockSlot"]=slot;evidence["targetClass"]=family.tileType;rows.append(evidence);
        }
        bool palette=false;
        for(const auto& family:CTerrainAtlasPresetCatalog::globalUniformFamilies())if(family.name=="Stone / Desert [class 3]") {
            QMap<QString,QByteArray> pages;QSet<QString> missing;QMap<QString,QImage> decoded;STerrainGlobalMaterialSource selected;
            const auto image=CTerrainAtlasPresetCatalog::loadGlobalUniformSource(family,{},QSize(256,256),&pages,&missing,&decoded,
                [&](const QString& page,QByteArray* bytes){*bytes=stock.readMember(page);return !bytes->isEmpty();},false,&selected);
            QCOMPARE(image.size(),QSize(256,256));QCOMPARE(terrainProvenancePixelSha(image),QByteArray("d5855c1f92eca2b4e85d6f539fb616de44e2ce4a650dd7eaf25d24cc99737bcb"));
            QCOMPARE(family.runtimeTerrainType,3);palette=true;
        }
        QVERIFY(palette);QVERIFY(sources.saveCache());
        QFile receipt(cfg["stockReceipt"].toString());QVERIFY(receipt.open(QIODevice::WriteOnly));
        receipt.write(QJsonDocument(QJsonObject{{"stockSlots",rows},{"paletteClass3Hd",palette}}).toJson());
    }
    void exactAliasesAndConfirmedProvenance()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> highTiles(64,pattern(0));highTiles[1]=pattern(1);
        const auto highPage=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(highTiles,0,256),true,nullptr);
        QImage high,other;QVERIFY(ei::decodeMmpDxt1AtlasTile(highPage,0,&high));QVERIFY(ei::decodeMmpDxt1AtlasTile(highPage,1,&other));
        QImage witness(128,128,QImage::Format_RGBA8888);
        for(int y=0;y<128;++y)for(int x=0;x<128;++x)witness.setPixel(x,y,qRgb((x*13+y*7)%256,(x*17+y*11)%256,(x*19+y*3)%256));
        const auto sdPage=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(QVector<QImage>(64,witness),0,128),true,nullptr);
        QVERIFY(ei::decodeMmpDxt1AtlasTile(sdPage,0,&witness));
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));
        SResArchiveNewMember member;member.name="stock000.mmp";member.payload=highPage;
        QByteArray bytes;QVERIFY(archive.buildWithAppendedMembers({member},&bytes));
        const QString path=dir.path()+"/sources.res";QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));file.close();
        QString error,reason;CTerrainHdSources::Source donor,second,matched;QJsonObject evidence;
        CTerrainHdSources exact;QVERIFY(exact.open({path},&error,nullptr,dir.path()+"/cache"));
        QVERIFY(exact.find("stock",0,&donor));QVERIFY(exact.find("stock",1,&second));
        QVERIFY(exact.addExactSdWitness(witness,donor,terrainProvenancePixelSha(high)));
        STerrainAtlasProfileSlot definition;
        QVERIFY(exact.match(witness,definition,&matched,&reason,&evidence));QCOMPARE(evidence["method"].toString(),QString("exact-SD-alias"));
        QCOMPARE(exact.image(matched,256),high);
        const auto turned=witness.transformed(QTransform().rotate(90));
        QVERIFY(exact.match(turned,definition,&matched,&reason,&evidence));QCOMPARE(matched.rotation,1);
        QImage changed=witness.copy();changed.setPixel(11,17,changed.pixel(11,17)^0x00010000);
        QVERIFY(!exact.match(changed,definition,&matched,&reason));
        QVERIFY(exact.addExactSdWitness(witness,second,terrainProvenancePixelSha(other)));
        QVERIFY(!exact.match(witness,definition,&matched,&reason));QCOMPARE(reason,QString("ambiguous-exact-SD-alias"));
        QVector<QImage> lows(64,witness);auto p=profile(lows);
        const auto ambiguous=rebuildTerrainAtlasHd(p,{sdPage},exact);
        QVERIFY2(ambiguous.success,qPrintable(ambiguous.error));QCOMPARE(ambiguous.receipt["hdApplied"].toInt(),0);QCOMPARE(ambiguous.receipt["upscaled128To256"].toInt(),64);QCOMPARE(ambiguous.receipt["native128Slots"].toInt(),0);

        STerrainConfirmedHdProvenance rule{terrainProvenancePixelSha(witness),"Test Stone",3,ETerrainAtlasLayer::Land,0,"stock",0,terrainProvenancePixelSha(high),"synthetic-owner"};
        STerrainAtlasProfileFamily family;family.id=1;family.name=rule.material;family.tileType=3;
        CTerrainHdSources confirmed({rule});QVERIFY(confirmed.open({path},&error,nullptr,dir.path()+"/cache"));
        QVERIFY(confirmed.match(witness,definition,&matched,&reason,&evidence,&family));
        QCOMPARE(evidence["method"].toString(),QString("owner-confirmed-provenance"));QCOMPARE(confirmed.image(matched,256),high);
        QVERIFY(!confirmed.match(changed,definition,&matched,&reason,&evidence,&family));
        QCOMPARE(reason,QString("confirmed-provenance-SD-SHA-mismatch"));
        auto wrongMaterial=family;wrongMaterial.name="Other Stone";QVERIFY(!confirmed.match(witness,definition,&matched,&reason,&evidence,&wrongMaterial));
        auto publishedFamily=family;publishedFamily.tileType=2;QVERIFY(confirmed.match(witness,definition,&matched,&reason,&evidence,&publishedFamily));
        QCOMPARE(evidence["targetReviewedClass"].toInt(),2);
        p.families={family};for(auto& d:p.slotDefinitions)d.signature.northWest=d.signature.northEast=d.signature.southEast=d.signature.southWest=1;
        const auto upgraded=rebuildTerrainAtlasHd(p,{sdPage},confirmed);
        QVERIFY2(upgraded.success,qPrintable(upgraded.error));QCOMPARE(upgraded.receipt["hdApplied"].toInt(),64);QCOMPARE(upgraded.receipt["ownerConfirmedHdApplied"].toInt(),64);QCOMPARE(upgraded.profile.families.first().tileType,3);
        rule.hdPixelSha256=terrainProvenancePixelSha(other);
        CTerrainHdSources wrongHd({rule});QVERIFY(wrongHd.open({path},&error,nullptr,dir.path()+"/cache"));
        QVERIFY(!wrongHd.match(witness,definition,&matched,&reason,&evidence,&family));QCOMPARE(reason,QString("confirmed-provenance-HD-SHA-mismatch"));
        const auto retained=rebuildTerrainAtlasHd(p,{sdPage},wrongHd);
        QVERIFY2(retained.success,qPrintable(retained.error));QCOMPARE(retained.receipt["hdApplied"].toInt(),0);QCOMPARE(retained.receipt["upscaled128To256"].toInt(),64);QCOMPARE(retained.receipt["native128Slots"].toInt(),0);

        STerrainGlobalMaterialSource paletteSource;paletteSource.mprNamespace="stock";paletteSource.uniformSlot=0;paletteSource.confirmedSdPage="sd000.mmp";paletteSource.confirmedSdSlot=0;
        paletteSource.confirmedSdPixelSha256=terrainProvenancePixelSha(witness);paletteSource.confirmedHdPixelSha256=terrainProvenancePixelSha(high);
        const auto loader=[&](const QString& name,QByteArray* payload){if(name=="stock000.mmp"){*payload=highPage;return true;}if(name!="sd000.mmp")return false;*payload=sdPage;return true;};
        QVERIFY(CTerrainAtlasPresetCatalog::validateConfirmedSource(paletteSource,high,loader));
        QVERIFY(!CTerrainAtlasPresetCatalog::validateConfirmedSource(paletteSource,other,loader));
        paletteSource.confirmedSdPixelSha256=terrainProvenancePixelSha(changed);
        QVERIFY(!CTerrainAtlasPresetCatalog::validateConfirmedSource(paletteSource,high,loader));
        paletteSource.confirmedSdPixelSha256=terrainProvenancePixelSha(witness);
        const auto missingHd=[&](const QString& name,QByteArray* payload){if(name=="stock000.mmp")return false;return loader(name,payload);};
        QVERIFY(!CTerrainAtlasPresetCatalog::validateConfirmedSource(paletteSource,high,missingHd));
        bool foundClass3=false;
        for(const auto& f:CTerrainAtlasPresetCatalog::globalUniformFamilies())if(f.name=="Stone / Desert [class 3]") {
            foundClass3=true;QCOMPARE(f.runtimeTerrainType,3);QVERIFY(!f.sources.isEmpty());
            for(const auto& source:f.sources){QCOMPARE(source.mprNamespace,QString("bz16h"));QCOMPARE(source.uniformSlot,463);QVERIFY(!source.confirmedHdPixelSha256.isEmpty());}
        }
        QVERIFY(foundClass3);
    }
    void progressCacheAndCancellation()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QVector<QImage> tiles(64,pattern(0));tiles[1]=pattern(1);
        const auto hd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument archive;QVERIFY(archive.loadBytes(empty));SResArchiveNewMember member;member.name="stock000.mmp";member.payload=hd;
        QByteArray res;QVERIFY(archive.buildWithAppendedMembers({member},&res));
        const QString path=dir.path()+"/stock.res",cache=dir.path()+"/cache";
        QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(res),qint64(res.size()));file.close();
        const auto sd=lowMip(hd);QVector<QImage> before;
        for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,i,&image));before.append(image);}
        const auto p=profile(before);QString error;
        CMprDocument map;CValidationReport report;QVERIFY(map.loadBytes(mapBytes(),"test.mpr",&report));
        for(int stage=CTerrainHdProgress::Opening;stage<=CTerrainHdProgress::Preparing;++stage) {
            bool reached=false;int last=0;
            CTerrainHdProgress progress([&](const CTerrainHdProgress::Snapshot& s) {
                QVERIFY(s.percent>=last);last=s.percent;
                if(int(s.stage)==stage&&(stage!=CTerrainHdProgress::Indexing||s.done>=8)) {reached=true;progress.cancel();}
            });
            CTerrainHdSources sources;
            if(sources.open({path},&error,&progress,cache)) {
                const auto result=rebuildTerrainAtlasHd(p,{sd},sources,true,{}, {},&progress);
                QVERIFY(result.cancelled);QVERIFY(!result.success);QVERIFY(result.publishedMembers.isEmpty());QVERIFY(result.mprBytes.isEmpty());
            }
            QVERIFY(reached);QVERIFY(progress.cancelled());QVERIFY(!sources.saveCache());
            QCOMPARE(QDir(dir.path()).entryList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot),QStringList{"stock.res"});
            QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),res);file.close();
        }
        int last=0;QSet<int> stages;
        CTerrainHdProgress progress([&](const CTerrainHdProgress::Snapshot& s){QVERIFY(s.percent>=last);QVERIFY(s.percent<=100);last=s.percent;stages.insert(s.stage);});
        CTerrainHdSources first;QVERIFY(first.open({path},&error,&progress,cache));QVERIFY(!first.cacheHit());
        auto result=rebuildTerrainAtlasHd(p,{sd},first,true,{}, {},&progress);QVERIFY(result.success);
        QVERIFY(prepareTerrainHdPublication(map,{},&result));progress.update(CTerrainHdProgress::Complete,1,1);
        QCOMPARE(last,100);QCOMPARE(stages.size(),6);QVERIFY(first.saveCache());
        CTerrainHdSources cached;QVERIFY(cached.open({path},&error,nullptr,cache));QVERIFY(cached.cacheHit());
        auto again=rebuildTerrainAtlasHd(p,{sd},cached);QVERIFY(prepareTerrainHdPublication(map,{},&again));
        QCOMPARE(again.pages,result.pages);QCOMPARE(again.mprBytes,result.mprBytes);QCOMPARE(again.profile.toBytes(),result.profile.toBytes());
        QCOMPARE(again.receipt,result.receipt);QCOMPARE(again.publishedMembers.size(),result.publishedMembers.size());
        for(int i=0;i<again.publishedMembers.size();++i)QCOMPARE(again.publishedMembers[i].payload,result.publishedMembers[i].payload);
        // A corrupt cache is ignored; source content is still authoritative.
        const auto entries=QDir(cache).entryList({"*.cache"},QDir::Files);QCOMPARE(entries.size(),1);
        QFile broken(cache+"/"+entries.first());QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("broken");broken.close();
        CTerrainHdSources fallback;QVERIFY(fallback.open({path},&error,nullptr,cache));QVERIFY(!fallback.cacheHit());QVERIFY(fallback.saveCache());
        // Invalidate even when size and mtime stay identical (full SHA key).
        const auto stamp=QFileInfo(path).lastModified();QVERIFY(file.open(QIODevice::ReadWrite));
        const auto changedOffset=res.indexOf(hd)+hd.size()-1;QVERIFY(changedOffset>=0);QVERIFY(file.seek(changedOffset));char byte=0;QCOMPARE(file.read(&byte,1),qint64(1));byte^=1;QVERIFY(file.seek(changedOffset));QCOMPARE(file.write(&byte,1),qint64(1));
        QVERIFY(file.setFileTime(stamp,QFileDevice::FileModificationTime));file.close();QCOMPARE(QFileInfo(path).lastModified(),stamp);
        CTerrainHdSources changed;QVERIFY(changed.open({path},&error,nullptr,cache));QVERIFY(!changed.cacheHit());
    }
    void progressDialogResponsiveAndCancel()
    {
        qApp->setStyle(QStyleFactory::create("Fusion"));
        ei::installDarkUiPalette();
        CTerrainHdProgress progress;CTerrainHdProgressDialog dialog(progress);
        dialog.show();
        auto future=QtConcurrent::run([&](){for(int i=0;i<=1000&&!progress.cancelled();++i){progress.update(CTerrainHdProgress::Recovering,i,1000);QThread::msleep(10);}});
        QTRY_VERIFY(dialog.findChild<QProgressBar*>()->value()>55);
        auto label=dialog.findChild<QLabel*>("hdProgressLabel");QVERIFY(label);QVERIFY(label->text().contains(QStringLiteral("Восстановление переходов")));
        QTest::qWait(3200);dialog.refresh();QVERIFY(progress.snapshot().stageRemainingMs>0);
        const QString png=qEnvironmentVariable("EI_HD_PROGRESS_PNG");
        if(!png.isEmpty())QVERIFY(dialog.grab().save(png,"PNG"));
        QTest::mouseClick(dialog.findChild<QPushButton*>("hdProgressCancel"),Qt::LeftButton);
        QVERIFY(progress.cancelled());QVERIFY(!dialog.findChild<QPushButton*>("hdProgressCancel")->isEnabled());
        future.waitForFinished();QVERIFY(dialog.isVisible());dialog.accept();
        CTerrainHdProgress escape;CTerrainHdProgressDialog escaped(escape);escaped.show();QTest::keyClick(&escaped,Qt::Key_Escape);QVERIFY(escape.cancelled());escaped.accept();
    }
    void publicationDialogResponsiveAndLocked()
    {
        CTerrainHdProgress progress;CTerrainHdProgressDialog dialog(progress);dialog.setAttribute(Qt::WA_DontShowOnScreen);dialog.lockForCommit();dialog.show();
        progress.operation(QStringLiteral("Запись HDPack"),2,6,10,100);dialog.refresh();
        QVERIFY(!dialog.findChild<QPushButton*>("hdProgressCancel")->isEnabled());
        QTest::keyClick(&dialog,Qt::Key_Escape);QVERIFY(!progress.cancelled());QVERIFY(dialog.isVisible());
        int ticks=0;QTimer timer;connect(&timer,&QTimer::timeout,[&](){++ticks;});timer.start(30);
        auto future=QtConcurrent::run([&](){for(int i=10;i<=90;++i){progress.operation(QStringLiteral("Запись HDPack"),2,6,i,100);QThread::msleep(45);}});
        QTRY_VERIFY(future.isFinished());QVERIFY(ticks>30);QVERIFY(progress.snapshot().stageRemainingMs>=0);
        progress.operation(QStringLiteral("Применение карты"),5,6);dialog.refresh();
        QCOMPARE(dialog.findChild<QProgressBar*>()->maximum(),0);
        QVERIFY(dialog.findChild<QLabel*>("hdProgressLabel")->text().contains(QStringLiteral("Шаг 5 из 6")));
        const auto png=qEnvironmentVariable("EI_HD_PROGRESS_PNG");if(!png.isEmpty())QVERIFY(dialog.grab().save(png));
        progress.operation(QStringLiteral("Раскладка завершена"),6,6,100,100);dialog.refresh();
        QCOMPARE(dialog.findChild<QProgressBar*>()->value(),100);dialog.accept();
    }
    void originalBlocksAndDeterminism()
    {
        ++headlessTestEntries;
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
        auto p=profile(before);SMprAnimatedTileRecord frameRange;frameRange.tileIndex=60;frameRange.frameCount=4;
        const QVector<SMprAnimatedTileRecord> animations{frameRange};
        const auto first=rebuildTerrainAtlasHd(p,{sd},sources,true,animations);
        QVERIFY2(first.success,qPrintable(first.error));QCOMPARE(first.receipt["hdApplied"].toInt(),60);QCOMPARE(first.receipt["retained128"].toInt(),4);
        QCOMPARE(first.receipt["animationUnchangedFrames"].toInt(),4);QCOMPARE(first.receipt["native128Slots"].toInt(),4);
        // The four animation frames stay byte-pixel identical while all other
        // slots publish at 256 px.
        for(int frame=60;frame<64;++frame)QCOMPARE(first.after[first.slotRemap[frame]],before[frame]);
        const auto repeat=rebuildTerrainAtlasHd(p,{sd},sources,true,animations);QCOMPARE(repeat.pages,first.pages);QCOMPARE(repeat.profile.toBytes(),first.profile.toBytes());
        CMprDocument map;CValidationReport report;QVERIFY(map.loadBytes(mapBytes(),"test.mpr",&report));
        // Real published maps have fewer MP class records than physical page slots.
        CMprEditTransaction shorten;QByteArray shortMp=map.memberPayload(map.mapInfo().memberName);
        shortMp.remove(38+61*4,3*4);qToLittleEndian<quint32>(61,reinterpret_cast<uchar*>(shortMp.data()+24));
        qToLittleEndian<quint16>(1,reinterpret_cast<uchar*>(shortMp.data()+38+61*4+2));
        for(const auto& member:map.members())if(member.name==map.mapInfo().memberName)shorten.replaceMemberPayload(member.name,member.sha256,shortMp);
        const auto sectorInfo=map.sectors()[0];const auto sectorMember=map.members()[sectorInfo.memberIndex];QByteArray shortSec=sectorMember.payload;
        for(int cell=0;cell<256;++cell){uchar* at=reinterpret_cast<uchar*>(shortSec.data()+sectorInfo.landTileOffset+cell*2);auto tile=SMprPackedTile::decode(qFromLittleEndian<quint16>(at));if(tile.atlasSlot()>=61){tile.tileIndex=0;qToLittleEndian<quint16>(tile.encode(),at);}}
        shorten.replaceMemberPayload(sectorMember.name,sectorMember.sha256,shortSec);QByteArray shortBytes;QVERIFY(map.applyTransaction(shorten,&shortBytes,&report));
        CMprDocument shortMap;QVERIFY(shortMap.loadBytes(shortBytes,"short.mpr",&report));auto shortResult=first;
        QVERIFY2(prepareTerrainHdPublication(shortMap,{},&shortResult),qPrintable(shortResult.error));
        CMprDocument paddedMap;QVERIFY(paddedMap.loadBytes(shortResult.mprBytes,"padded.mpr",&report));
        for(int slot=0;slot<61;++slot)QCOMPARE(paddedMap.mapInfo().tileTypes[shortResult.slotRemap[slot]],shortMap.mapInfo().tileTypes[slot]);
        auto candidate=first;QVERIFY2(prepareTerrainHdPublication(map,{},&candidate),qPrintable(candidate.error));
        QVERIFY(QRegularExpression("^[a-zA-Z0-9_]+_[0-9a-f]{12}$").match(candidate.profile.mprNamespace).hasMatch());
        CMprDocument reopened;QVERIFY(reopened.loadBytes(candidate.mprBytes,"candidate.mpr",&report));
        auto legacyRecovery=candidate.receipt;legacyRecovery.remove("outputSlots");
        auto legacyRows=legacyRecovery["slots"].toArray();
        for(int i=0;i<legacyRows.size();++i){auto row=legacyRows[i].toObject();row.remove("outputSlot");legacyRows[i]=row;}legacyRecovery["slots"]=legacyRows;
        auto legacyPages=legacyRecovery["pages"].toArray();
        for(int i=0;i<legacyPages.size();++i){auto page=legacyPages[i].toObject();page.remove("tileSize");legacyPages[i]=page;}legacyRecovery["pages"]=legacyPages;
        const auto legacyAgain=rebuildTerrainAtlasHd(candidate.profile,candidate.pages,sources,true,reopened.mapInfo().animatedTiles,QJsonDocument(legacyRecovery).toJson());
        QVERIFY(!legacyAgain.success); // No-remap legacy evidence cannot represent relocated static slots.
        for(int slot=0;slot<map.mapInfo().tileTypes.size();++slot) {
            const int output=candidate.slotRemap[slot];
            if(output<0||output>=reopened.mapInfo().tileTypes.size()||reopened.mapInfo().tileTypes.value(output)!=map.mapInfo().tileTypes[slot])
            QCOMPARE(reopened.mapInfo().tileTypes.value(output),map.mapInfo().tileTypes[slot]);
        }
        QCOMPARE(reopened.mapInfo().animatedTiles,map.mapInfo().animatedTiles);
        QCOMPARE(reopened.sectors().size(),map.sectors().size());
        const auto originalSector=map.sectors()[0],publishedSector=reopened.sectors()[0];
        const auto originalSec=map.members()[originalSector.memberIndex].payload,publishedSec=reopened.members()[publishedSector.memberIndex].payload;
        for(int cell=0;cell<256;++cell) {
            const auto a=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(originalSec.constData()+originalSector.landTileOffset+cell*2)));
            const auto b=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(publishedSec.constData()+publishedSector.landTileOffset+cell*2)));
            QCOMPARE(b.atlasSlot(),candidate.slotRemap[a.atlasSlot()]);QCOMPARE(b.rotation,a.rotation);
        }
        STerrainSemanticFieldDocument field;field.mprNamespace="published";field.vertexWidth=field.vertexHeight=17;
        field.land=QVector<qint32>(289,1);field.overlay=QVector<qint32>(289,0);
        field.atlasPixelSha256=QByteArray::fromHex(p.atlasPixelSha256);field.profileSha256=QCryptographicHash::hash(p.toBytes(),QCryptographicHash::Sha256);
        field.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(map);field.hasRandomPaintSeed=true;field.randomPaintSeed=12345;
        auto withSemantic=first;QVERIFY2(prepareTerrainHdPublication(map,field.toBytes(),&withSemantic),qPrintable(withSemantic.error));
        CMprDocument semanticMap;QVERIFY(semanticMap.loadBytes(withSemantic.mprBytes,"semantic.mpr",&report));STerrainSemanticFieldDocument migrated;
        QVERIFY(STerrainSemanticFieldDocument::fromBytes(semanticMap.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&migrated));
        QCOMPARE(migrated.land,field.land);QCOMPARE(migrated.overlay,field.overlay);QCOMPARE(migrated.randomPaintSeed,field.randomPaintSeed);
        QVERIFY(migrated.validateBinding(withSemantic.profile.mprNamespace,{},QByteArray::fromHex(withSemantic.profile.atlasPixelSha256),QCryptographicHash::hash(withSemantic.profile.toBytes(),QCryptographicHash::Sha256),17,17,&report,STerrainSemanticFieldDocument::tileBindingSha256(semanticMap)));
        auto repeatSemantic=repeat;const bool repeatSemanticPrepared=prepareTerrainHdPublication(map,field.toBytes(),&repeatSemantic);
        QVERIFY(repeatSemanticPrepared);
        QCOMPARE(repeatSemantic.mprBytes,withSemantic.mprBytes);
        // Recompressed rotations are matched physically; SEC rotations never change.
        QVector<QImage> turned=before;turned[3]=before[0].transformed(QTransform().rotate(90));
        const auto turnedMmp=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(turned,0,128),true,nullptr);
        QVector<QImage> turnedDecoded;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(turnedMmp,i,&image));turnedDecoded.append(image);}
        const auto turnedHd=rebuildTerrainAtlasHd(profile(turnedDecoded),{turnedMmp},sources,true,animations);
        QVERIFY(turnedHd.success);QCOMPARE(turnedHd.receipt["hdApplied"].toInt(),60);
        QCOMPARE(turnedHd.after[3],first.after[0].transformed(QTransform().rotate(90)));
        QImage unrelated(128,128,QImage::Format_RGBA8888);unrelated.fill(Qt::blue);
        CTerrainHdSources::Source wrong;QString reason;QJsonObject evidence;
        auto invalid=p.slotDefinitions[0];invalid.sourceNamespace="stock";invalid.sourceSlotAliases={0};invalid.sourcePixelSha256=p.atlasPixelSha256;
        QVERIFY(!sources.match(unrelated,invalid,&wrong,&reason,&evidence));
        auto partial=before;partial[3]=unrelated;
        const auto partialSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(partial,0,128),true,nullptr);
        QVector<QImage> partialDecoded;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(partialSd,i,&image));partialDecoded.append(image);}
        const auto carried=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources,true,animations);
        QVERIFY2(carried.success,qPrintable(carried.error));QCOMPARE(carried.receipt["hdApplied"].toInt(),59);QCOMPARE(carried.receipt["retained128"].toInt(),4);QCOMPARE(carried.receipt["hdVariantsApplied"].toInt(),1);QCOMPARE(carried.receipt["sdCarried256"].toInt(),0);
        QVERIFY(carried.receipt["slots"].toArray()[3].toObject()["sameMaterialVariant"].toBool());
        QCOMPARE(carried.after[carried.slotRemap[3]].size(),QSize(256,256));
        QCOMPARE(carried.after[carried.slotRemap[3]].width(),256);
        QCOMPARE(carried.pages.size(),2);
        STerrainAtlasProfile portable;QVERIFY2(STerrainAtlasProfile::fromBytes(carried.profile.toBytes(),&portable,&report),qPrintable(report.toText()));
        QCOMPARE(portable.atlasSlotCount,128);QCOMPARE(portable.slotDefinitions.size(),128);
        QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(carried.pages[0].constData()+4)),quint32(1024));
        QCOMPARE(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(carried.pages[1].constData()+4)),quint32(2048));
        QVERIFY(carried.pages[0].size()+carried.pages[1].size()<2*hd.size());
        auto remapped=carried;const bool remappedPrepared=prepareTerrainHdPublication(map,{},&remapped);
        QVERIFY(remappedPrepared);
        CMprDocument moved;QVERIFY(moved.loadBytes(remapped.mprBytes,"moved.mpr",&report));
        QCOMPARE(moved.mapInfo().tileTypes[carried.slotRemap[1]],map.mapInfo().tileTypes[1]);
        QCOMPARE(moved.mapInfo().animatedTiles[0].tileIndex,quint16(carried.slotRemap[60]));
        auto remappedField=field;
        remappedField.atlasPixelSha256=QByteArray::fromHex(profile(partialDecoded).atlasPixelSha256);
        remappedField.profileSha256=QCryptographicHash::hash(profile(partialDecoded).toBytes(),QCryptographicHash::Sha256);
        auto semanticRemap=carried;QVERIFY2(prepareTerrainHdPublication(map,remappedField.toBytes(),&semanticRemap),qPrintable(semanticRemap.error));
        CMprDocument semanticMoved;QVERIFY(semanticMoved.loadBytes(semanticRemap.mprBytes,"repacked-semantic.mpr",&report));
        STerrainSemanticFieldDocument remappedSaved;
        QVERIFY(STerrainSemanticFieldDocument::fromBytes(semanticMoved.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&remappedSaved));
        QCOMPARE(remappedSaved.land,field.land);QCOMPARE(remappedSaved.overlay,field.overlay);QCOMPARE(remappedSaved.randomPaintSeed,field.randomPaintSeed);
        QVERIFY(remappedSaved.validateBinding(semanticRemap.profile.mprNamespace,{},QByteArray::fromHex(semanticRemap.profile.atlasPixelSha256),QCryptographicHash::hash(semanticRemap.profile.toBytes(),QCryptographicHash::Sha256),17,17,&report,STerrainSemanticFieldDocument::tileBindingSha256(semanticMoved)));

        QVERIFY(remapped.receipt["SECReferencesEquivalent"].toBool());
        QVERIFY(remapped.receipt["remappedCells"].toInt()>0);
        const auto oldSec=map.sectors()[0],newSec=moved.sectors()[0];
        const auto oldPayload=map.members()[oldSec.memberIndex].payload,newPayload=moved.members()[newSec.memberIndex].payload;
        for(int cell=0;cell<256;++cell) {
            const auto a=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(oldPayload.constData()+oldSec.landTileOffset+cell*2)));
            const auto b=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(newPayload.constData()+newSec.landTileOffset+cell*2)));
            QCOMPARE(b.atlasSlot(),carried.slotRemap[a.atlasSlot()]);QCOMPARE(b.rotation,a.rotation);
        }
        const auto recovery=QJsonDocument(remapped.receipt).toJson(QJsonDocument::Compact);
        const auto again=rebuildTerrainAtlasHd(remapped.profile,remapped.pages,sources,true,moved.mapInfo().animatedTiles,recovery);
        QVERIFY2(again.success,qPrintable(again.error));
        QCOMPARE(again.pages,remapped.pages);
        QCOMPARE(again.receipt["nativeHdSourceRecovered"].toInt(),59);
        CTerrainHdSources::Source nativeSource;
        const bool nativeMatch=sources.matchNativeHd(first.after[first.slotRemap[0]],before[0],&nativeSource);QVERIFY(nativeMatch);
        QCOMPARE(sources.image(nativeSource,128),before[0]);
        const bool unrelatedMatch=sources.matchNativeHd(first.after[first.slotRemap[0]],partialDecoded[3],&nativeSource);QVERIFY(!unrelatedMatch);
        const auto order=again.receipt["sourceOrder"].toArray();QCOMPARE(order.size(),128);
        for(int i=0;i<128;++i)QCOMPARE(order[i].toInt(),i);
        auto brokenRecovery=remapped.receipt;auto rows=brokenRecovery["slots"].toArray();rows[1]=rows[0];brokenRecovery["slots"]=rows;
        const auto broken=rebuildTerrainAtlasHd(remapped.profile,remapped.pages,sources,true,moved.mapInfo().animatedTiles,QJsonDocument(brokenRecovery).toJson());QVERIFY(!broken.success);
        auto noOp=again;QVERIFY(prepareTerrainHdPublication(moved,{},&noOp));
        QCOMPARE(noOp.mprBytes,remapped.mprBytes);QVERIFY(noOp.publishedMembers.isEmpty());
        const auto third=rebuildTerrainAtlasHd(remapped.profile,remapped.pages,sources,true,moved.mapInfo().animatedTiles,QJsonDocument(noOp.receipt).toJson());
        QVERIFY2(third.success,qPrintable(third.error));QCOMPARE(third.pages,remapped.pages);
        const QString root=dir.path()+"/build-tests-cli";QVERIFY(QDir().mkpath(root+"/maps"));QVERIFY(QDir().mkpath(root+"/inputs"));
        auto saveCopy=[](const QString& path,const QByteArray& data){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();};
        QByteArray publishedArchive;QVERIFY(archive.buildWithPublishedMembers(remapped.publishedMembers,false,&publishedArchive));
        QVERIFY(saveCopy(root+"/inputs/pack.res",publishedArchive));QVERIFY(saveCopy(root+"/maps/test.mpr",remapped.mprBytes));
        QJsonObject cfg{{"testRoot",root},{"archive",root+"/inputs/pack.res"},{"sources",QJsonArray{root+"/inputs/pack.res"}},
            {"maps",QJsonArray{root+"/maps/test.mpr"}},{"mode","build"},{"output",root+"/out"},{"receipt",root+"/receipt.json"}};
        QVERIFY(saveCopy(root+"/fixture.json",QJsonDocument(cfg).toJson()));
        QCOMPARE(runTerrainHdRepublish(root+"/fixture.json"),0);
        QFile repeatedMap(root+"/out/test/test.mpr");const bool repeatedMapOpened=repeatedMap.open(QIODevice::ReadOnly);QVERIFY(repeatedMapOpened);const QByteArray repeatedMapBytes=repeatedMap.readAll();QCOMPARE(repeatedMapBytes,remapped.mprBytes);
        QFile repeatedPack(root+"/out/test/HDPack_Terrain.res");const bool repeatedPackOpened=repeatedPack.open(QIODevice::ReadOnly);QVERIFY(repeatedPackOpened);const QByteArray repeatedPackBytes=repeatedPack.readAll();QCOMPARE(repeatedPackBytes,publishedArchive);
        cfg["receipt"]=dir.path()+"/outside.json";QVERIFY(saveCopy(root+"/fixture.json",QJsonDocument(cfg).toJson()));
        QCOMPARE(runTerrainHdRepublish(root+"/fixture.json"),2);QVERIFY(!QFileInfo::exists(dir.path()+"/outside.json"));

        // An SD frame keeps the complete animation native, never stretched.
        SMprAnimatedTileRecord interval;interval.tileIndex=2;interval.frameCount=3;
        const auto animation=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources,true,{interval});
        QVERIFY(animation.success);QCOMPARE(animation.receipt["animationUnchangedFrames"].toInt(),3);
        for(int frame=2;frame<5;++frame)QCOMPARE(animation.after[animation.slotRemap[frame]].width(),128);
        QCOMPARE(animation.slotRemap[4],animation.slotRemap[2]+2);
        const auto conservative=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources,false,animations);
        QVERIFY(conservative.success);QCOMPARE(conservative.receipt["native128Slots"].toInt(),4);
        for(int slot=0;slot<60;++slot)QCOMPARE(conservative.after[conservative.slotRemap[slot]].size(),QSize(256,256));
        // Different native HD tiles with an identical SD mip must be refused.
        auto collision=hd;const auto otherBase=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(QVector<QImage>(64,pattern(1)),0,256),true,nullptr);
        collision.replace(76,2048*2048/2,otherBase.mid(76,2048*2048/2));
        for(int row=0;row<64;++row){const int offset=76+((448+row)*512)*8;collision.replace(offset,64*8,hd.mid(offset,64*8));}
        SResArchiveNewMember alias;alias.name="other000.mmp";alias.payload=collision;
        CResArchiveDocument collisionArchive;QVERIFY(collisionArchive.loadBytes(empty));QByteArray collisionRes;QVERIFY(collisionArchive.buildWithAppendedMembers({member,alias},&collisionRes));
        QSaveFile second(dir.path()+"/collision.res");QVERIFY(second.open(QIODevice::WriteOnly));QVERIFY(second.write(collisionRes)==collisionRes.size());QVERIFY(second.commit());
        CTerrainHdSources ambiguous;QVERIFY(ambiguous.open({second.fileName()},&error));
        QVERIFY(!ambiguous.match(before[0],{},&wrong,&reason));QCOMPARE(reason,QString("ambiguous-HD-collision"));
        STerrainAtlasProfileSlot conflicting;conflicting.sourceNamespace="other";conflicting.sourceSlotAliases={0,2};
        QVERIFY(!ambiguous.match(before[0],conflicting,&wrong,&reason));QCOMPARE(reason,QString("ambiguous-provenance-HD-aliases"));
        STerrainTransitionSignature sig;sig.northWest=sig.northEast=1;sig.southWest=sig.southEast=2;
        QMap<qint32,QImage> lowSources{{1,before[0]},{2,before[1]}};
        QMap<qint32,QImage> highSources{{1,first.after[first.slotRemap[0]]},{2,first.after[first.slotRemap[1]]}};
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
        CTerrainVirtualAtlas next(noAuthored,{first.after[first.slotRemap[0]]},0,highSources,styles,true);
        const auto expected=next.reviewGeneratedBlend(sig,0,nullptr,nullptr,1);
        const auto encoded=ei::encodeMmpDxt1(expected,false,nullptr);QImage expectedDecoded;
        QVERIFY(ei::decodeDxtImage(encoded.mid(76),256,256,ei::DxtFormat::Dxt1,&expectedDecoded));QCOMPARE(upgraded.after[2],expectedDecoded);
        QMap<QPair<qint32,qint32>,STerrainRecipeBrush> bank;
        QVERIFY(CTerrainMixedEdge::buildStockBrushes(decoded,{{1,{0}},{2,{1}}},{{qMakePair(1,2),{2}}},{{2,sig}},{qMakePair(1,2)},&bank));
        STerrainRecipeBakeInput input;input.tileWidth=input.tileHeight=1;input.vertexMaterials={1,1,2,2};input.preferredRuntimeTypes={1};
        for(int id=1;id<=2;++id){STerrainRecipeMaterial material;material.materialId=id;material.runtimeTerrainType=id;material.uniformTiles={decoded[id-1]};input.materials.append(material);}
        input.brushes={bank.first()};STerrainRecipeBakeOptions options;options.tileSize=128;options.stableCoordinateVariants=true;options.preserveReviewedCellRuntimeTypes=true;
        const auto baked=CTerrainRecipeBaker::bake(input,options);QVERIFY2(baked.success,qPrintable(baked.diagnostics.join('\n')));
        decoded[3]=baked.tiles[baked.tileByCell.first()].pixels;
        const auto mixedSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(decoded,0,128),true,nullptr);QVector<QImage> mixedDecoded;
        for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(mixedSd,i,&image));mixedDecoded.append(image);}
        auto mixedProfile=profile(mixedDecoded);mixedProfile.slotDefinitions[2]=p.slotDefinitions[2];mixedProfile.slotDefinitions[3].signature=p.slotDefinitions[2].signature;
        QByteArray recipeKey("crisp-stock-v1|1,1,2,2,|");recipeKey+=QByteArray::number(baked.recipes.first().variant)+"|1";
        mixedProfile.slotDefinitions[3].mixedEdgeRecipes=QStringList{"mixed-edge-b2:"+QString::fromLatin1(QCryptographicHash::hash(recipeKey,QCryptographicHash::Sha256).toHex())};
        const auto mixedHd=rebuildTerrainAtlasHd(mixedProfile,{mixedSd},sources);
        QVERIFY2(mixedHd.success,qPrintable(mixedHd.error));
        QVERIFY2(mixedHd.receipt["slots"].toArray()[3].toObject()["regenerated"].toBool(),qPrintable(QString(QJsonDocument(mixedHd.receipt).toJson())));
        QCOMPARE(mixedHd.after[3].size(),QSize(256,256));QCOMPARE(mixedHd.profile.slotDefinitions[3].mixedEdgeRecipes,mixedProfile.slotDefinitions[3].mixedEdgeRecipes);
    }
    void cleanupOrphanAndSharedNamespace()
    {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        const QString ns="published_123456789abc",next="published_987654321abc";
        QDir().mkpath(dir.path()+"/Dejavu");QDir().mkpath(dir.path()+"/Base");
        const QString current=dir.path()+"/Dejavu/current.mpr",baseMap=dir.path()+"/Base/base.mpr";
        auto save=[](const QString& path,const QByteArray& bytes){QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();};
        CMprDocument map;QVERIFY(map.loadBytes(mapBytes(),"fixture"));
        auto named=[&](const QString& name){QByteArray bytes;CMprEditTransaction tx;map.applyIsolatedNamespaceTransaction(tx,name,&bytes);return bytes;};
        QVERIFY(save(current,named(next)));QVERIFY(save(baseMap,mapBytes()));
        QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
        CResArchiveDocument blank;QVERIFY(blank.loadBytes(empty));
        auto p=profile(QVector<QImage>(64,pattern(0)));p.mprNamespace=ns;p.provenance=CTerrainAtlasProfileStore::workingAtlasProvenance();
        QVector<SResArchiveNewMember> members;
        for(const auto& pair:QVector<QPair<QString,QByteArray>>{{ns+"000.mmp","old-page"},{CTerrainAtlasProfileStore::embeddedMemberName(ns),p.toBytes()},{"stock000.mmp","stock"},{next+"000.mmp","new-page"},{ns+"000.mmp.extra","unrelated"}}) {
            SResArchiveNewMember m;m.name=pair.first;m.payload=pair.second;m.lastWriteTime=123;members.append(m);
        }
        QByteArray bytes;QVERIFY(blank.buildWithAppendedMembers(members,&bytes));CResArchiveDocument archive;QVERIFY(archive.loadBytes(bytes));
        const QStringList roots{dir.path()+"/Dejavu",dir.path()+"/Base"};QByteArray candidate;QJsonObject receipt;QString error;
        auto stockProfile=p;stockProfile.provenance="authored-template:stock";
        SResArchiveNewMember stockMetadata;stockMetadata.name=CTerrainAtlasProfileStore::embeddedMemberName(ns);stockMetadata.payload=stockProfile.toBytes();
        QByteArray stockBytes;QVERIFY(archive.buildWithPublishedMembers({stockMetadata},true,&stockBytes));
        CResArchiveDocument stockArchive;QVERIFY(stockArchive.loadBytes(stockBytes));
        QVERIFY(buildTerrainHdCleanup(stockArchive,ns,next,current,roots,&candidate,&receipt,&error));
        QCOMPARE(candidate,stockBytes);QCOMPARE(receipt["reason"].toString(),QString("stock-or-unverified-original-atlas"));

        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));
        QCOMPARE(receipt["removedMembers"].toInt(),2);CResArchiveDocument cleaned;QVERIFY(cleaned.loadBytes(candidate));
        QCOMPARE(cleaned.members().size(),3);QCOMPARE(cleaned.memberPayload("stock000.mmp"),QByteArray("stock"));
        QCOMPARE(cleaned.memberPayload(ns+"000.mmp.extra"),QByteArray("unrelated"));
        QVERIFY(save(baseMap,named(ns)));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));QCOMPARE(candidate,bytes);QCOMPARE(receipt["reason"].toString(),QString("shared-namespace"));
        QVERIFY(save(baseMap,QByteArray("broken")));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));QCOMPARE(candidate,bytes);QVERIFY(receipt["reason"].toString().startsWith("unreadable-MPR"));
        QVERIFY(buildTerrainHdCleanup(archive,"stock",next,current,roots,&candidate,&receipt,&error));QCOMPARE(candidate,bytes);QCOMPARE(receipt["reason"].toString(),QString("stock-or-unverified-original-atlas"));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,{roots.first()},&candidate,&receipt,&error));QCOMPARE(candidate,bytes);
        QVERIFY(save(baseMap,mapBytes()));
        QVERIFY(buildTerrainHdCleanup(archive,ns,next,current,roots,&candidate,&receipt,&error));
        const QString archivePath=dir.path()+"/pack.res";QVERIFY(save(archivePath,bytes));
        // An MPR appearing after the orphan scan cancels cleanup before any write.
        const QString lateMap=dir.path()+"/Base/late.mpr";QVERIFY(save(lateMap,named(ns)));
        QVERIFY(!commitTerrainHdCleanup(archivePath,bytes,candidate,receipt,&error));
        QFile readback(archivePath);QVERIFY(readback.open(QIODevice::ReadOnly));QCOMPARE(readback.readAll(),bytes);readback.close();
        QVERIFY(QFile::remove(lateMap));
        // Exact archive baseline is mandatory even if the candidate is valid.
        QVERIFY(!commitTerrainHdCleanup(archivePath,bytes+"changed",candidate,receipt,&error));
        QVERIFY2(commitTerrainHdCleanup(archivePath,bytes,candidate,receipt,&error),qPrintable(error));
        QVERIFY(readback.open(QIODevice::ReadOnly));QCOMPARE(readback.readAll(),candidate);

    }
    void expandedAtlasKeepsNativeSd()
    {
        ++headlessTestEntries;
        QVector<QImage> tile(64,pattern(0).scaled(128,128));
        const auto page=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tile,0,128),true,nullptr);
        QVector<QImage> decoded;
        for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(page,i,&image));decoded.append(image);}
        QVector<QImage> images;for(int i=0;i<18;++i)images+=decoded;
        auto p=profile(decoded);p.textureCount=18;p.atlasSlotCount=1152;p.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
        for(int i=64;i<1152;++i){auto d=p.slotDefinitions[i%64];d.slot=i;p.slotDefinitions.append(d);}
        CTerrainHdSources empty;const auto result=rebuildTerrainAtlasHd(p,QVector<QByteArray>(18,page),empty);
        QVERIFY2(result.success,qPrintable(result.error));QCOMPARE(result.after.size(),1152);QCOMPARE(result.receipt["upscaled128To256"].toInt(),1152);
        for(const auto& output:result.after)QCOMPARE(output.size(),QSize(256,256));
    }
    void missingSourceKeepsOriginalPage()
    {
        ++headlessTestEntries;
        QVector<QImage> tiles(64,pattern(0).scaled(128,128));
        const auto sd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,128),true,nullptr);QVector<QImage> before;
        for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(sd,slot,&image));before.append(image);}
        CTerrainHdSources empty;auto result=rebuildTerrainAtlasHd(profile(before),{sd},empty);
        QVERIFY(result.success);QCOMPARE(result.receipt["upscaled128To256"].toInt(),64);QCOMPARE(result.receipt["retained128"].toInt(),0);QCOMPARE(result.pages.size(),1);
        QCOMPARE(result.receipt["regeneratedApplied"].toInt(),0);
        QVERIFY(!result.receipt["allStaticSlotsTrueHd"].toBool());
        CMprDocument validMap;QVERIFY(validMap.loadBytes(mapBytes(),"missing-source.mpr"));
        auto refusedPublication=result;
        QVERIFY(!prepareTerrainHdPublication(validMap,{},&refusedPublication));
        QVERIFY(refusedPublication.error.contains("diagnostic only"));
        QVERIFY(refusedPublication.mprBytes.isEmpty()&&refusedPublication.publishedMembers.isEmpty());
        for(const auto& output:result.after)QCOMPARE(output.size(),QSize(256,256));
        const auto repeat=rebuildTerrainAtlasHd(profile(before),{sd},empty);
        QVERIFY(repeat.success);QCOMPARE(repeat.pages,result.pages);QCOMPARE(repeat.profile.toBytes(),result.profile.toBytes());
        QImage transparent(128,128,QImage::Format_RGBA8888);transparent.fill(Qt::transparent);
        const auto padding=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(QVector<QImage>(64,transparent),0,128),true,nullptr);
        QVector<QImage> padded;for(int slot=0;slot<64;++slot){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(padding,slot,&image));padded.append(image);}
        auto paddingProfile=profile(padded);for(auto& d:paddingProfile.slotDefinitions)d.assigned=false;
        const auto blank=rebuildTerrainAtlasHd(paddingProfile,{padding},empty);QVERIFY2(blank.success,qPrintable(blank.error));
        QCOMPARE(blank.receipt["emptyStaticPaddingSlots"].toInt(),64);QCOMPARE(blank.receipt["upscaled128To256"].toInt(),0);
        QCOMPARE(blank.receipt["regeneratedApplied"].toInt(),0);QVERIFY(blank.receipt["allStaticSlotsTrueHd"].toBool());
    }
};
int main(int argc,char** argv)
{
    if(argc>1&&std::strcmp(argv[1],"--headless-only")==0) {
        const QSet<QByteArray> safeFunctions{"nativeArtworkStillPublishesLegacyMapDimensions","localLibrarySurvivesGameChangesAndCancellation","lowContrastTransitionKeepsReviewedTopology","originalBlocksAndDeterminism","expandedAtlasKeepsNativeSd","missingSourceKeepsOriginalPage","progressCacheAndCancellation","semanticTransitionUsesHdPixels","textureOwnershipUsesHdPixels","multiMaterialJunctionUsesHdPixels"};
        if(argc<3)return 2;
        std::vector<char*> filtered;filtered.reserve(size_t(argc));filtered.push_back(argv[0]);
        for(int i=2;i<argc;++i) {
            if(!safeFunctions.contains(QByteArray(argv[i]).split(':').first()))return 2;
            filtered.push_back(argv[i]);
        }
        const int selectorCount=int(filtered.size())-1;
        char verbosity[]="-v2";filtered.push_back(verbosity);
        char outputOption[]="-o";
        const QByteArray output=qEnvironmentVariable("EI_MAPPER_HD_TEST_LOG").toLocal8Bit()+",txt";
        if(output!=",txt"){filtered.push_back(outputOption);filtered.push_back(const_cast<char*>(output.constData()));}
        int appArgc=1;char* appArgv[]={argv[0],nullptr};QCoreApplication app(appArgc,appArgv);
        int testArgc=int(filtered.size());TerrainHdRepublishTests tests;
        const int result=QTest::qExec(&tests,testArgc,filtered.data());
        std::fprintf(stderr,"headless QtTest result=%d selectors=%d entered=%d\n",result,selectorCount,headlessTestEntries);
        return result;
    }
    ei::TestClipboardSession clipboard(true);if(!clipboard.prepared())return 2;
    QApplication app(argc,argv);if(!clipboard.install())return 2;
    TerrainHdRepublishTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "terrain_hd_republish_tests.moc"

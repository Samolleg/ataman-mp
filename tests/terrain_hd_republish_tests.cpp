#include <QtTest>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QtEndian>
#include <QDataStream>
#include <QTransform>
#include "terrain_hd_republish.h"
#include "terrain_virtual_atlas.h"
#include "mmp_codec.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "terrain_semantic_field_store.h"
#include "terrain_mixed_edge.h"
#include <QCryptographicHash>

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
QByteArray mapBytes()
{
    QByteArray mp;QDataStream out(&mp,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out<<quint32(0xCE4AF672)<<float(100)<<quint32(1)<<quint32(1)<<quint32(1)<<quint32(1024)<<quint32(64)<<quint32(128)<<quint16(0)<<quint32(1);
    for(int i=0;i<64;++i)out<<qint32(i==1?2:1);out<<quint16(60)<<quint16(4);
    QByteArray sector;QDataStream sec(&sector,QIODevice::WriteOnly);sec.setByteOrder(QDataStream::LittleEndian);sec<<quint32(0xCF4BF774)<<quint8(0);
    sector.append(QByteArray(33*33*8+16*16*2,'\0'));
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument res;res.loadBytes(empty);QByteArray bytes;SResArchiveNewMember a,b;a.name="published.mp";a.payload=mp;b.name="published000000.sec";b.payload=sector;
    res.buildWithAppendedMembers({a,b},&bytes);return bytes;
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
        CMprDocument map;CValidationReport report;QVERIFY(map.loadBytes(mapBytes(),"test.mpr",&report));
        auto candidate=first;QVERIFY2(prepareTerrainHdPublication(map,{},&candidate),qPrintable(candidate.error));
        QVERIFY(QRegularExpression("^[a-zA-Z0-9_]+_[0-9a-f]{12}$").match(candidate.profile.mprNamespace).hasMatch());
        CMprDocument reopened;QVERIFY(reopened.loadBytes(candidate.mprBytes,"candidate.mpr",&report));
        QCOMPARE(reopened.mapInfo().tileTypes,map.mapInfo().tileTypes);QCOMPARE(reopened.mapInfo().animatedTiles,map.mapInfo().animatedTiles);
        QCOMPARE(reopened.sectors().size(),map.sectors().size());
        QCOMPARE(reopened.members()[reopened.sectors()[0].memberIndex].payload,map.members()[map.sectors()[0].memberIndex].payload);
        STerrainSemanticFieldDocument field;field.mprNamespace="published";field.vertexWidth=field.vertexHeight=17;
        field.land=QVector<qint32>(289,1);field.overlay=QVector<qint32>(289,0);
        field.atlasPixelSha256=QByteArray::fromHex(p.atlasPixelSha256);field.profileSha256=QCryptographicHash::hash(p.toBytes(),QCryptographicHash::Sha256);
        field.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(map);field.hasRandomPaintSeed=true;field.randomPaintSeed=12345;
        auto withSemantic=first;QVERIFY2(prepareTerrainHdPublication(map,field.toBytes(),&withSemantic),qPrintable(withSemantic.error));
        CMprDocument semanticMap;QVERIFY(semanticMap.loadBytes(withSemantic.mprBytes,"semantic.mpr",&report));STerrainSemanticFieldDocument migrated;
        QVERIFY(STerrainSemanticFieldDocument::fromBytes(semanticMap.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&migrated));
        QCOMPARE(migrated.land,field.land);QCOMPARE(migrated.overlay,field.overlay);QCOMPARE(migrated.randomPaintSeed,field.randomPaintSeed);
        QVERIFY(migrated.validateBinding(withSemantic.profile.mprNamespace,{},QByteArray::fromHex(withSemantic.profile.atlasPixelSha256),QCryptographicHash::hash(withSemantic.profile.toBytes(),QCryptographicHash::Sha256),17,17,&report,STerrainSemanticFieldDocument::tileBindingSha256(semanticMap)));
        auto repeatSemantic=repeat;QVERIFY(prepareTerrainHdPublication(map,field.toBytes(),&repeatSemantic));QCOMPARE(repeatSemantic.mprBytes,withSemantic.mprBytes);
        // Recompressed rotations are matched physically; SEC rotations never change.
        QVector<QImage> turned=before;turned[3]=before[0].transformed(QTransform().rotate(90));
        const auto turnedMmp=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(turned,0,128),true,nullptr);
        QVector<QImage> turnedDecoded;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(turnedMmp,i,&image));turnedDecoded.append(image);}
        const auto turnedHd=rebuildTerrainAtlasHd(profile(turnedDecoded),{turnedMmp},sources);
        QVERIFY(turnedHd.success);QCOMPARE(turnedHd.receipt["hdApplied"].toInt(),64);
        QCOMPARE(turnedHd.after[3],first.after[0].transformed(QTransform().rotate(90)));
        QImage unrelated(128,128,QImage::Format_RGBA8888);unrelated.fill(Qt::blue);
        CTerrainHdSources::Source wrong;QString reason;QJsonObject evidence;
        auto invalid=p.slotDefinitions[0];invalid.sourceNamespace="stock";invalid.sourceSlotAliases={0};invalid.sourcePixelSha256=p.atlasPixelSha256;
        QVERIFY(!sources.match(unrelated,invalid,&wrong,&reason,&evidence));
        auto partial=before;partial[3]=unrelated;
        const auto partialSd=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(partial,0,128),true,nullptr);
        QVector<QImage> partialDecoded;for(int i=0;i<64;++i){QImage image;QVERIFY(ei::decodeMmpDxt1AtlasTile(partialSd,i,&image));partialDecoded.append(image);}
        const auto carried=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources);
        QVERIFY2(carried.success,qPrintable(carried.error));QCOMPARE(carried.receipt["hdApplied"].toInt(),63);QCOMPARE(carried.receipt["retained128"].toInt(),1);QCOMPARE(carried.receipt["sdCarried256"].toInt(),1);
        QCOMPARE(carried.after[3],partialDecoded[3].scaled(256,256,Qt::IgnoreAspectRatio,Qt::FastTransformation));
        const auto retainedMip=lowMip(carried.pages[0]);
        for(int row=0;row<32;++row) {const int offset=76+((224+row)*256+96)*8;QCOMPARE(retainedMip.mid(offset,32*8),partialSd.mid(offset,32*8));}
        const auto conservative=rebuildTerrainAtlasHd(profile(partialDecoded),{partialSd},sources,false);QCOMPARE(conservative.pages[0],partialSd);
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

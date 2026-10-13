#include <QtTest>
#include "terrain_hd_republish.h"
#include "mmp_codec.h"
#include "mpr_document.h"
#include <QJsonDocument>
#include <QJsonArray>

class TerrainHdRecoveryTests:public QObject {
 Q_OBJECT
 static QByteArray page(int tileSize) {
  QImage image(tileSize*8,tileSize*8,QImage::Format_RGBA8888);image.fill(QColor(37,83,117));
  return ei::encodeMmpDxt1(image,true);
 }
 static STerrainAtlasProfile profile(int count) {
  STerrainAtlasProfile p;p.mprNamespace="test";p.provenance="portable-working-modern-atlas-v1";
  p.atlasSlotCount=count;p.textureCount=count/64;return p;
 }
 static QJsonObject receipt(const QVector<QByteArray>& pages) {
  QJsonArray rows,order,pageRows;
  for(int i=0;i<128;++i) {
   rows.append(QJsonObject{{"slot",i},{"outputSlot",i},{"result",i<64?"retained-128":"already-HD-unchanged"}});
   order.append(127-i);
  }
  for(int i=0;i<2;++i)pageRows.append(QJsonObject{{"page",i},{"tileSize",i==0?128:256},{"bytesAfter",pages[i].size()}});
  auto row=rows[66].toObject();row["result"]="upscaled-128-to-256";row["source"]="unproven-sd:66";rows[66]=row;
  return {{"publishedNamespace","test"},{"physicalSlots",128},{"outputSlots",128},
          {"slots",rows},{"pages",pageRows},{"sourceOrder",order},
          {"animationPagePaddingSlots",QJsonArray{32,33}}};
 }
private slots:
 void originalProfileConstructorsDeclarePages() {
  auto builtin=CTerrainAtlasPresetCatalog::exactProfile("bz2g",
      "10c214c7fcd677931efe2def80fafb5762f14527d7858514e38bd11bd8437156",512);
  QVERIFY(builtin.approved);QCOMPARE(builtin.textureCount,8);
  STerrainAtlasProfile parsed;QVERIFY(STerrainAtlasProfile::fromBytes(builtin.toBytes(),&parsed));
  QCOMPARE(parsed.textureCount,8);
  STerrainAtlasAnalysis analysis;auto authored=CTerrainAtlasProfileInference::fromAuthoredAnalysis("test","hash",128,analysis);
  QCOMPARE(authored.textureCount,2);
  QVector<QImage> images(64,QImage(128,128,QImage::Format_RGBA8888));
  for(auto& image:images)image.fill(QColor(37,83,117));
  auto inferred=CTerrainAtlasProfileInference::propose("test",images,QVector<qint32>(64,1),QVector<STerrainAtlasSlotUsage>(64));
  QCOMPARE(inferred.textureCount,1);
 }
 void nativeStaticWaterIsPreservedAndOtherSdStillRefused() {
  const auto payload=page(128);QVector<QImage> tiles;
  for(int i=0;i<64;++i){QImage tile;QVERIFY(ei::decodeMmpDxt1AtlasTile(payload,i,&tile));tiles.append(tile);}
  auto p=profile(64);p.approved=true;p.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(tiles);
  STerrainAtlasProfileFamily family;family.id=1;family.name="Water / Still";family.tileType=6;
  family.layer=ETerrainAtlasLayer::Overlay;family.overlayAllowed=true;family.landAllowed=false;
  for(int i=0;i<64;++i){STerrainAtlasProfileSlot d;d.slot=i;
   d.signature.northWest=d.signature.northEast=d.signature.southEast=d.signature.southWest=1;
   p.slotDefinitions.append(d);family.memberSlots.append(i);}
  p.families.append(family);CTerrainHdSources sources;
  const auto water=rebuildTerrainAtlasHd(p,{payload},sources,true,{});
  QVERIFY2(water.success,qPrintable(water.error));QVERIFY(water.receipt["allStaticSlotsTrueHd"].toBool());
  QCOMPARE(water.receipt["waterUnchangedSlots"].toInt(),64);QCOMPARE(water.receipt["animationUnchangedFrames"].toInt(),0);
  QCOMPARE(water.receipt["upscaled128To256"].toInt(),0);QCOMPARE(water.pages.first(),payload);
  for(int i=0;i<64;++i)QCOMPARE(water.after[water.slotRemap[i]],tiles[i]);
  p.slotDefinitions[63].assigned=false;
  const auto mixed=rebuildTerrainAtlasHd(p,{payload},sources,true,{});
  QVERIFY2(mixed.success,qPrintable(mixed.error));QVERIFY(!mixed.receipt["allStaticSlotsTrueHd"].toBool());
  QCOMPARE(mixed.receipt["waterUnchangedSlots"].toInt(),63);QCOMPARE(mixed.receipt["upscaled128To256"].toInt(),1);
  for(int i=0;i<63;++i)QCOMPARE(mixed.after[mixed.slotRemap[i]],tiles[i]);
 }
 void emptyNativeAnimationPaddingIsNotAnUpscaledTexture() {
  QImage raster(1024,1024,QImage::Format_RGBA8888);raster.fill(Qt::transparent);
  for(int y=0;y<128;++y)for(int x=0;x<1024;++x)raster.setPixelColor(x,y,QColor(31,71,109));
  const auto payload=ei::encodeMmpDxt1(raster,true);QVERIFY(!payload.isEmpty());
  QVector<QImage> tiles;for(int i=0;i<64;++i){QImage tile;QVERIFY(ei::decodeMmpDxt1AtlasTile(payload,i,&tile));tiles.append(tile);}
  auto p=profile(64);p.approved=true;p.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(tiles);
  STerrainAtlasProfileFamily family;family.id=1;family.name="Water / Still";family.tileType=1;
  family.layer=ETerrainAtlasLayer::Overlay;family.thumbnailSlot=0;
  QVector<SMprAnimatedTileRecord> animations;
  for(int i=0;i<64;++i){STerrainAtlasProfileSlot d;d.slot=i;bool opaque=false;
   for(int y=0;y<tiles[i].height()&&!opaque;++y)for(int x=0;x<tiles[i].width();++x)
    if(qAlpha(tiles[i].pixel(x,y))){opaque=true;break;}
   d.assigned=opaque;
   if(opaque){d.signature.northWest=d.signature.northEast=d.signature.southEast=d.signature.southWest=1;family.memberSlots.append(i);
    SMprAnimatedTileRecord animation;animation.tileIndex=i;animation.frameCount=1;animations.append(animation);}p.slotDefinitions.append(d);}
  QVERIFY(!animations.isEmpty());QVERIFY(animations.size()<64);
  p.families.append(family);
  CTerrainHdSources sources;const auto result=rebuildTerrainAtlasHd(p,{payload},sources,true,animations);
  QVERIFY2(result.success,qPrintable(result.error));QVERIFY2(result.receipt["allStaticSlotsTrueHd"].toBool(),
    qPrintable(QString::fromUtf8(QJsonDocument(result.receipt).toJson(QJsonDocument::Compact))));
  QCOMPARE(result.receipt["upscaled128To256"].toInt(),0);QCOMPARE(result.receipt["animationUnchangedFrames"].toInt(),animations.size());
  QCOMPARE(result.pages.first(),payload);
 }
 void growsWithoutLosingOldEvidence() {
  const QVector<QByteArray> pages{page(128),page(256),page(256)};auto old=receipt(pages);
  QByteArray current;QString error;
  QVERIFY2(reconcileWorkingTerrainHdRecovery(profile(192),pages,QJsonDocument(old).toJson(),&current,&error),qPrintable(error));
  const auto now=QJsonDocument::fromJson(current).object();QCOMPARE(now["physicalSlots"].toInt(),192);QCOMPARE(now["outputSlots"].toInt(),192);
  const auto rows=now["slots"].toArray(),order=now["sourceOrder"].toArray();QCOMPARE(rows.size(),192);QCOMPARE(order.size(),192);
  for(int i=0;i<128;++i){QCOMPARE(rows[i],old["slots"].toArray()[i]);QCOMPARE(order[i],old["sourceOrder"].toArray()[i]);}
  QSet<int> outputs;
  for(const auto& value:rows){auto row=value.toObject();outputs.insert(row["outputSlot"].toInt());}
  QCOMPARE(outputs.size(),192);QCOMPARE(now["animationPagePaddingSlots"],old["animationPagePaddingSlots"]);
  QCOMPARE(rows[66].toObject()["result"].toString(),QString("upscaled-128-to-256"));
  QCOMPARE(now["pages"].toArray().size(),3);
 }
 void coversReusedPaddingWithoutMovingOldSlots() {
  const QVector<QByteArray> pages{page(128),page(256)};auto old=receipt(pages);
  auto rows=old["slots"].toArray();while(rows.size()>64)rows.removeLast();old["slots"]=rows;old["physicalSlots"]=64;
  QByteArray current;QString error;
  QVERIFY2(reconcileWorkingTerrainHdRecovery(profile(128),pages,QJsonDocument(old).toJson(),&current,&error),qPrintable(error));
  auto now=QJsonDocument::fromJson(current).object();QCOMPARE(now["slots"].toArray().size(),128);
  for(int i=0;i<64;++i)QCOMPARE(now["slots"].toArray()[i],rows[i]);
  QCOMPARE(now["sourceOrder"],old["sourceOrder"]);
 }
 void refusesCorruptEvidence_data() {
  QTest::addColumn<int>("kind");
  for(int i=0;i<6;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
 }
 void refusesCorruptEvidence() {
  QFETCH(int,kind);const QVector<QByteArray> pages{page(128),page(256),page(256)};auto old=receipt(pages);
  if(kind==0)old["publishedNamespace"]="other-map";
  if(kind==1)old["outputSlots"]=256;
  if(kind==2){auto rows=old["slots"].toArray();auto row=rows[1].toObject();row["outputSlot"]=0;rows[1]=row;old["slots"]=rows;}
  if(kind==3){auto order=old["sourceOrder"].toArray();order[1]=order[0];old["sourceOrder"]=order;}
  if(kind==4){auto rows=old["slots"].toArray();auto row=rows[0].toObject();row["result"]="original-HD-BC1";rows[0]=row;old["slots"]=rows;}
  if(kind==5){auto ps=old["pages"].toArray();auto p=ps[0].toObject();p["bytesAfter"]=17;ps[0]=p;old["pages"]=ps;}
  QByteArray current;QString error;
  QVERIFY(!reconcileWorkingTerrainHdRecovery(profile(192),pages,QJsonDocument(old).toJson(),&current,&error));QVERIFY(!error.isEmpty());
 }
 void publishedProfilesKeepTheStrictExistingContract() {
  auto p=profile(192);p.provenance="published-HD";QByteArray input="untrusted receipt",output;
  QVERIFY(reconcileWorkingTerrainHdRecovery(p,{},input,&output));QCOMPARE(output,input);
 }
};
QTEST_GUILESS_MAIN(TerrainHdRecoveryTests)
#include "terrain_hd_recovery_tests.moc"

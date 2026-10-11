#include <QtTest>
#include <QTemporaryDir>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include "terrain_hd_variant_bank.h"
#include "terrain_hd_republish.h"
#include "terrain_virtual_atlas.h"
#include "terrain_semantic_field_store.h"
#include "terrain_hd_progress_dialog.h"
#include "landscape_tools.h"
#include "mmp_codec.h"
#include "endurance_document.h"
#include "verified_clipboard.h"
#include "view.h"
#include "terrain_mixed_edge.h"
#include <QtEndian>
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "landscape.h"
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QToolButton>

namespace {
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
bool write(const QString& p,const QByteArray& b){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();}
QByteArray sectors(const CTerrainEditModel& m){QByteArray b;for(int y=0;y<m.tileCountY()/16;++y)for(int x=0;x<m.tileCountX()/16;++x)b+=m.sectorPayload(x,y);return b;}
QByteArray syntheticMap(){
 QByteArray mp;QDataStream out(&mp,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
 out<<quint32(0xCE4AF672)<<float(100)<<quint32(1)<<quint32(1)<<quint32(1)<<quint32(2048)<<quint32(64)<<quint32(256)<<quint16(0)<<quint32(0);
 for(int i=0;i<64;++i)out<<qint32(0);
 QByteArray sec;QDataStream sector(&sec,QIODevice::WriteOnly);sector.setByteOrder(QDataStream::LittleEndian);sector<<quint32(0xCF4BF774)<<quint8(0);sec.append(QByteArray(33*33*8+16*16*2,'\0'));
 QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
 CResArchiveDocument archive;archive.loadBytes(empty);SResArchiveNewMember a,b;a.name="Variants.mp";a.payload=mp;b.name="Variants000000.sec";b.payload=sec;
 QByteArray bytes;archive.buildWithAppendedMembers({a,b},&bytes);return bytes;
}
}
class TerrainHdVariantBankTests:public QObject {
 Q_OBJECT
 TerrainHdVariantBank::Bank bank;
 QString root;
private slots:
 void initTestCase(){
  root=qEnvironmentVariable("EI_VARIANTS_TEST_ROOT");QVERIFY(!root.isEmpty());
  bank=TerrainHdVariantBank::load(root+"/bank");QVERIFY2(bank.success,qPrintable(bank.error));
  QVERIFY(!bank.entries.isEmpty());QCOMPARE(bank.manifest["algorithm"].toInt(),TerrainHdVariants::AlgorithmVersion);
  for(const auto& v:bank.manifest["groups"].toArray()){const auto g=v.toObject();
   QVERIFY(TerrainHdVariantBank::staticMaterial(g["material"].toString(),g["reviewedClass"].toInt(),g["physicalClass"].toInt()));}
  QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/variant-settings");
  qputenv("EI_MAPPER_HD_VARIANT_CACHE",(root+"/bank").toUtf8());TerrainHdVariantBank::forget();
 }
 void nativePagesAndRejections(){
  const auto corpus=QJsonDocument::fromJson(read(root+"/corpus-final/results.json")).object()["groups"].toArray();
  QMap<QString,QJsonObject> expected;for(const auto& g:corpus)for(const auto& v:g.toObject()["variants"].toArray())expected[v.toObject()["id"].toString()]=v.toObject();
  for(const auto& e:bank.entries){
   QVERIFY(expected.contains(e.id));QCOMPARE(e.metadata["pixelSha256"],expected[e.id]["pixelSha256"]);
   QVERIFY(TerrainHdVariants::rejectReasons(e.metadata["metrics"].toObject()).isEmpty());
  }
  for(int first=0;first<bank.entries.size();first+=64) {
   QVector<QImage> tiles;for(int i=first;i<qMin(first+64,bank.entries.size());++i)tiles.append(bank.entries[i].pixels);
   const auto payload=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(tiles,0,256),true,nullptr);
   for(int i=0;i<tiles.size();++i)QCOMPARE(ei::terrainTileMipChain(payload,i),bank.entries[first+i].blocks);
  }
  const auto e=bank.entries.first();const auto layer=e.layer=="land"?ETerrainAtlasLayer::Land:ETerrainAtlasLayer::Overlay;
  auto all=TerrainHdVariantBank::forMaterial(bank,e.material,e.reviewedClass,layer);
  const auto filtered=TerrainHdVariantBank::forMaterial(bank,e.material,e.reviewedClass,layer,{e.id});
  QCOMPARE(filtered.size(),all.size()-1);for(const auto& v:filtered)QVERIFY(v.id!=e.id);
  QVERIFY(TerrainHdVariantBank::rejects().isEmpty());
 }
 void sourceShaInvalidatesAndCancel(){
  QTemporaryDir temp(root+"/cache-test-XXXXXX");QVERIFY(temp.isValid());
  const QString id=bank.manifest["id"].toString(),dir=temp.path()+"/"+id;QVERIFY(QDir().mkpath(dir));
  auto manifest=bank.manifest;const QString source=temp.path()+"/input.res";
  auto resource=[](const QByteArray& donor,const QByteArray& unrelated){
   QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
   CResArchiveDocument res;res.loadBytes(empty);SResArchiveNewMember a,b;a.name="stock000.mmp";a.payload=donor;b.name="another-map.mp";b.payload=unrelated;
   QByteArray result;res.buildWithAppendedMembers({a,b},&result);return result;};
  QVERIFY(write(source,resource("abc","first publication")));
  manifest["inputs"]=QJsonArray{QJsonObject{{"path",source},{"member","stock000.mmp"},{"sha256",QString(QCryptographicHash::hash("abc",QCryptographicHash::Sha256).toHex())}}};
  for(const auto& v:manifest["pages"].toArray()){auto name=v.toObject()["file"].toString();QVERIFY(QFile::copy(bank.directory+"/"+name,dir+"/"+name));}
  QVERIFY(write(dir+"/manifest.json",QJsonDocument(manifest).toJson()));
  QVERIFY(write(temp.path()+"/current.json",QJsonDocument(QJsonObject{{"id",id}}).toJson()));
  QVERIFY(TerrainHdVariantBank::load(temp.path()).success);
  qputenv("EI_MAPPER_HD_VARIANT_CACHE",temp.path().toUtf8());TerrainHdVariantBank::forget();QVERIFY(TerrainHdVariantBank::available());
  QVERIFY(write(source,resource("abc","another map republished with different length and RES offsets")));
  QVERIFY(TerrainHdVariantBank::load(temp.path()).success);QVERIFY(TerrainHdVariantBank::available());
  const QFileInfo info(source);
  QVERIFY(write(source,resource("abd","another map republished with different length and RES offsets")));QFile f(source);QVERIFY(f.open(QIODevice::ReadWrite));QVERIFY(f.setFileTime(info.lastModified(),QFileDevice::FileModificationTime));f.close();
  QVERIFY(!TerrainHdVariantBank::load(temp.path()).success);
  qputenv("EI_MAPPER_HD_VARIANT_CACHE",(root+"/bank").toUtf8());TerrainHdVariantBank::forget();
  const auto cfg=QJsonDocument::fromJson(read(root+"/bank-fixture.json")).object();TerrainHdVariantBank::Config c;
  for(const auto& v:cfg["archives"].toArray())c.archives.append(v.toString());for(const auto& v:cfg["maps"].toArray())c.maps.append(v.toString());c.cacheRoot=temp.path()+"/cancelled";
  CTerrainHdProgress p;p.cancel();const auto cancelled=TerrainHdVariantBank::generate(c,&p);
  QVERIFY(cancelled.cancelled&&!cancelled.success);QVERIFY(!QFileInfo::exists(c.cacheRoot));
 }
 void contextualStampsHaveNoGeneratedMenu(){
  CView view(nullptr,nullptr);CLandscapeTools panel;view.m_landscapeTools=&panel;
  CTerrainTransitionCatalog catalog;STerrainTransitionVariant variant;
  variant.signature.northWest=1;variant.signature.northEast=variant.signature.southEast=variant.signature.southWest=2;
  QVERIFY(catalog.addVariant(variant));QImage pixels(128,128,QImage::Format_ARGB32);pixels.fill(Qt::green);
  view.m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(catalog,QVector<QImage>(64,pixels),146,512));
  STerrainPaintMaterialChoice choice;choice.id=1;choice.name="Contextual soil";choice.generatedIds=QStringList{"should-not-be-offered"};choice.generatedThumbnails={pixels};
  view.m_modernLandPaintChoices={choice};view.refreshClassicAutoPalette();int stamps=0;
  for(auto* button:panel.findChildren<QToolButton*>())if(button->property("terrainContextualStamp").toBool()){
   ++stamps;QVERIFY(button->menu()==nullptr);}
  QCOMPARE(stamps,1);view.m_landscapeTools=nullptr;
 }
 void controlsAndProgress(){
  QSettings().remove("ui/landscape/generatedHdVariants");CLandscapeTools panel;
  QVERIFY(panel.generatedHdVariants());auto box=panel.findChild<QCheckBox*>("generatedHdVariants");QVERIFY(box);
  auto button=panel.findChild<QPushButton*>("terrainHdVariantsGenerate");QVERIFY(button);
  QSignalSpy clicked(&panel,&CLandscapeTools::generateHdVariantsRequested);button->click();QCOMPARE(clicked.count(),1);
  box->setChecked(false);CLandscapeTools restored;QVERIFY(!restored.generatedHdVariants());box->setChecked(true);
  CTerrainHdProgress p;CTerrainHdProgressDialog dialog(p,nullptr,true);p.update(CTerrainHdProgress::Recovering,50,100);dialog.refresh();
  QCOMPARE(dialog.findChild<QProgressBar*>()->value(),72);dialog.findChild<QPushButton*>("hdProgressCancel")->click();QVERIFY(p.cancelled());
 }
 void configuredOptionalJamaisVu(){
  QTemporaryDir temp(root+"/config-test-XXXXXX");QVERIFY(temp.isValid());
  const auto dejavu=temp.path()+"/Mods/Dejavu/res/HDPack_Terrain.res",jmv=temp.path()+"/Mods/JamaisVu135/res/HDPack_Terrain.res";
  for(const auto& path:{dejavu,jmv}){QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));QVERIFY(write(path,"synthetic"));}
  QVERIFY(QDir().mkpath(temp.path()+"/Base/Maps"));QVERIFY(write(temp.path()+"/Base/Maps/fixture.mpr",syntheticMap()));
  auto c=TerrainHdVariantBank::configured({dejavu});QCOMPARE(c.archives.size(),2);QCOMPARE(c.maps.size(),1);
  QVERIFY(QFileInfo(c.archives[1]).canonicalFilePath()==QFileInfo(jmv).canonicalFilePath());
 }
 void mixedEdgeUsesGeneratedBodies(){
  const auto inputs=QJsonDocument::fromJson(read(root+"/inputs/inputs.json")).object()["groups"].toArray();
  QVector<QImage> originals;QVector<QVector<TerrainHdVariantBank::Entry>> additions;
  for(const auto& value:inputs){auto g=value.toObject();QVector<TerrainHdVariantBank::Entry> entries;
   for(const auto& e:bank.entries)if(e.metadata["key"]==g["key"]&&e.physicalClass==0)entries.append(e);
   if(entries.isEmpty())continue;originals.append(QImage(root+"/inputs/"+g["sources"].toArray().first().toObject()["png"].toString()));additions.append(entries);if(originals.size()==2)break;}
  QCOMPARE(originals.size(),2);QVector<QImage> images(64,originals[0]);images[1]=originals[1];images[2]=originals[0].copy();
  for(int y=0;y<256;++y)for(int x=128;x<256;++x)images[2].setPixel(x,y,originals[1].pixel(x,y));
  CTerrainTransitionCatalog catalog;
  for(int slot=0;slot<64;++slot)for(int r=0;r<4;++r){STerrainTransitionVariant v;SMprPackedTile p;p.tileIndex=slot;p.rotation=r;v.packedTile=p.encode();
   v.signature.northWest=v.signature.northEast=v.signature.southEast=v.signature.southWest=slot==1?2:1;
   if(slot==2){v.signature.northEast=2;v.signature.southEast=2;}v.signature=CTerrainTransitionCatalog::rotated(v.signature,-r);QVERIFY(catalog.addVariant(v));}
  CValidationReport report;CMprDocument map;QVERIFY(map.loadBytes(syntheticMap(),"synthetic.mpr",&report));CTerrainEditModel model;QVERIFY(model.load(map,&report));model.enableWorkingAtlasSlots(512);
  QVector<qint32> field(17*17,1);QSet<int> affected;for(int y=5;y<11;++y)for(int x=5;x<11;++x)field[y*17+x]=2;
  for(int y=4;y<11;++y)for(int x=4;x<11;++x)affected.insert(y*16+x);
  QVERIFY(model.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,QVector<qint32>(17*17,1),&report));
  CTerrainWorkingAtlas off(catalog,images,145,512,{}, {},QVector<qint32>(64,0),{{1,0},{2,0}});auto on=off.createStagingCopy();
  QVERIFY(TerrainHdVariantBank::import(on.data(),additions[0],1,&report));QVERIFY(TerrainHdVariantBank::import(on.data(),additions[1],2,&report));
  auto a=CTerrainMixedEdge::resolve(model,&off,affected,{{1,0},{2,0}},{},QPoint(),2,145,false,&report);QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
  auto b=CTerrainMixedEdge::resolve(model,on.data(),affected,{{1,0},{2,0}},{},QPoint(),2,145,false,&report);QVERIFY2(!report.hasErrors(),qPrintable(report.toText()));
  QVERIFY(!a.isEmpty()&&!b.isEmpty());QVERIFY(!off.mixedEdgeRecipes().isEmpty()&&!on->mixedEdgeRecipes().isEmpty());
  int changed=0;
  for(const auto& x:a.deltas)for(const auto& y:b.deltas)if(x.globalX==y.globalX&&x.globalY==y.globalY) {
   const int first=SMprPackedTile::decode(x.after).atlasSlot(),second=SMprPackedTile::decode(y.after).atlasSlot();
   if(off.mixedEdgeRecipes().values().contains(first)&&on->mixedEdgeRecipes().values().contains(second)
       &&off.tileImages()[first]!=on->tileImages()[second])++changed;
  }
  QVERIFY(changed>0);qInfo()<<"Mixed transition pixels changed by bank:"<<changed;
 }
 void confirmed144ClassAndMixedResolution(){
  const auto entries=TerrainHdVariantBank::forMaterial(bank,"Stone / Desert",3,ETerrainAtlasLayer::Land);QCOMPARE(entries.size(),5);
  QImage sd(128,128,QImage::Format_ARGB32);sd.fill(Qt::gray);CTerrainTransitionCatalog catalog;
  STerrainTransitionVariant v;v.signature.northWest=v.signature.northEast=v.signature.southEast=v.signature.southWest=123;QVERIFY(catalog.addVariant(v));
  CTerrainWorkingAtlas atlas(catalog,QVector<QImage>(64,sd),145,512,{}, {},QVector<qint32>(64,3),{{123,3}});
  CValidationReport report;QVERIFY(TerrainHdVariantBank::import(&atlas,entries,123,&report));QCOMPARE(atlas.activeTileCount(),69);
  for(int slot=64;slot<69;++slot){QCOMPARE(atlas.tileImages()[slot].size(),QSize(256,256));QCOMPARE(atlas.runtimeTerrainTypeForSlot(slot),3);}
 }
 void shuffleUploadsAndUndoesBank(){
  CValidationReport report;CMprDocument map;QVERIFY(map.loadBytes(syntheticMap(),"Variants.mpr",&report));
  const auto input=QJsonDocument::fromJson(read(root+"/inputs/inputs.json")).object()["groups"].toArray().first().toObject();
  QImage original(root+"/inputs/"+input["sources"].toArray().first().toObject()["png"].toString());QVERIFY(!original.isNull());
  QVector<QImage> images(64,original);STerrainAtlasProfile profile;profile.approved=true;profile.mprNamespace="Variants";profile.atlasSlotCount=64;profile.textureCount=1;profile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
  STerrainAtlasProfileFamily family;family.id=1;family.name="Grass / Burnt";family.tileType=0;profile.families.append(family);
  for(int i=0;i<64;++i){STerrainAtlasProfileSlot s;s.slot=i;s.signature.northWest=s.signature.northEast=s.signature.southEast=s.signature.southWest=1;profile.slotDefinitions.append(s);}
  const QString path=root+"/shuffle-ui.mpr";QVERIFY(write(path,map.originalBytes()));
  CSettings settings;QTableWidget properties;properties.setColumnCount(2);QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;CLandscapeTools panel;
  CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);view.attachLandscapeTools(&panel);
  view.resize(320,240);view.show();QApplication::processEvents();view.makeCurrent();QVERIFY(view.isValid());
  view.m_pLand->readMap(QFileInfo(path),false);view.m_mprDocument=map;QVERIFY(view.m_terrainEdit.load(map,&report));view.m_terrainEdit.enableWorkingAtlasSlots(512);
  view.m_terrainEditAvailable=true;view.m_terrainAutoCatalogPending=false;view.m_terrainAtlasProfile=profile;view.m_terrainAtlasAnalysis=profile.usableAnalysis();view.m_authoredTerrainTileImages=images;
  view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);view.m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(view.m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies(),images,145,512,{}, {},map.mapInfo().tileTypes,{{1,0}}));
  STerrainGlobalMaterialFamily global;global.id=1;global.name=family.name;global.runtimeTerrainType=0;global.layer=ETerrainAtlasLayer::Land;view.m_terrainGlobalFamilies={global};tiles.adoptTileImages(images,map.mapInfo().tileTypes);
  panel.findChild<QCheckBox*>("generatedHdVariants")->setChecked(true);const auto before=sectors(view.m_terrainEdit);
  view.shuffleTerrainTiles(false,145);QCOMPARE(history.count(),1);QCOMPARE(view.m_workingTerrainAtlas->activeTileCount(),69);QVERIFY(tiles.tileImages().size()>=69);
  const auto painted=sectors(view.m_terrainEdit);QVERIFY(painted!=before);int count=0;
  for(int y=0;y<16;++y)for(int x=0;x<16;++x)count+=SMprPackedTile::decode(view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Land)).atlasSlot()>=64;
  QVERIFY(count>0);history.undo();QCOMPARE(sectors(view.m_terrainEdit),before);QCOMPARE(view.m_workingTerrainAtlas->activeTileCount(),64);
  history.redo();QCOMPARE(sectors(view.m_terrainEdit),painted);QCOMPARE(view.m_workingTerrainAtlas->activeTileCount(),69);
  qInfo()<<"Shuffle rendered generated cells:"<<count;history.clear();view.makeCurrent();view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
 }
 void realMapPaintSaveReopenHd(){
  const auto cfg=QJsonDocument::fromJson(read(root+"/random-fixture.json")).object();const auto real=cfg["realRoot"].toString();
  CValidationReport report;CMprDocument map;QVERIFY(map.loadBytes(read(real+"/source.mpr"),"private-dejavu.mpr",&report));
  STerrainAtlasProfile profile;QVERIFY(STerrainAtlasProfile::fromJson(QJsonDocument::fromJson(read(real+"/atlas.json")).object()["catalog"].toObject(),&profile,&report));
  QVector<QImage> images;for(int i=0;i<profile.atlasSlotCount;++i)images.append(QImage(real+QString("/tiles/%1.png").arg(i)));
  const auto catalog=profile.usableAnalysis(&report).overlayCatalogWithLandFamilies();QMap<qint32,qint32> types;for(const auto& f:profile.families)types[f.id]=f.tileType;
  CTerrainEditModel model;QVERIFY(model.load(map,&report));model.enableWorkingAtlasSlots(4096);
  CView view(nullptr,nullptr);CLandscapeTools panel;view.m_landscapeTools=&panel;view.m_terrainAtlasProfile=profile;view.m_terrainEdit=model;
  view.m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(catalog,images,145,4096,{}, {},map.mapInfo().tileTypes,types));QVERIFY(view.m_workingTerrainAtlas->isValid());
  qint32 material=0;int original=-1;QVector<TerrainHdVariantBank::Entry> entries;
  for(const auto& family:profile.families){
   auto candidates=TerrainHdVariantBank::forMaterial(bank,family.name,family.tileType,ETerrainAtlasLayer::Land);
   for(const auto& e:candidates)for(const auto& d:profile.slotDefinitions)
    if(d.signature.northWest==family.id&&d.signature.northEast==family.id&&d.signature.southEast==family.id&&d.signature.southWest==family.id
       &&map.mapInfo().tileTypes.value(d.slot,-1)==e.physicalClass){material=family.id;original=d.slot;entries=candidates;break;}
   if(material)break;
  }
  QVERIFY(material&&original>=0&&!entries.isEmpty());
  QVERIFY(write(root+"/variants-off-baseline.json",QJsonDocument(QJsonObject{{"realRoot",real},{"slot",original},{"reference",root+"/variants-off.sec"},{"output",root+"/variants-off-144.sec"}}).toJson()));
  const int before=view.m_workingTerrainAtlas->activeTileCount();panel.findChild<QCheckBox*>("generatedHdVariants")->setChecked(false);
  QVERIFY(view.importTerrainHdVariants(material,ETerrainTileLayer::Land,&report));QCOMPARE(view.m_workingTerrainAtlas->activeTileCount(),before);
  STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=6;brush.shape=ETerrainTileBrushShape::Square;
  SMprPackedTile packed;packed.atlasIndex=original/64;packed.tileIndex=original%64;
  QVERIFY(view.m_terrainEdit.apply(view.m_terrainEdit.makeTilePaint(brush,ETerrainTileLayer::Land,packed.atlasIndex,packed.tileIndex,0),true,&report));
  const auto baseline=view.m_terrainEdit;
  auto old=baseline;QVERIFY(old.apply(old.makeRandomTilePaint(catalog,145,QRect(2,2,12,12)),true,&report));
  const auto oldBytes=sectors(old);QVERIFY(write(root+"/variants-off.sec",oldBytes));
  panel.findChild<QCheckBox*>("generatedHdVariants")->setChecked(true);QVERIFY(view.importTerrainHdVariants(material,ETerrainTileLayer::Land,&report));
  QVERIFY(view.m_workingTerrainAtlas->activeTileCount()>before);
  auto& atlas=*view.m_workingTerrainAtlas;
  auto on=view.m_terrainEdit;QVERIFY(on.apply(on.makeRandomTilePaint(atlas.catalog(),145,QRect(2,2,12,12)),true,&report));
  int used=0;for(int y=2;y<14;++y)for(int x=2;x<14;++x)used+=SMprPackedTile::decode(on.packedTile(x,y,ETerrainTileLayer::Land)).atlasSlot()>=before;
  QVERIFY(used>0);qInfo()<<"Generated cells in 12x12:"<<used;
  panel.findChild<QCheckBox*>("generatedHdVariants")->setChecked(false);view.applyTerrainHdVariantSelectionPolicy();
  auto off=baseline;QVERIFY(off.apply(off.makeRandomTilePaint(atlas.catalog(),145,QRect(2,2,12,12)),true,&report));QCOMPARE(sectors(off),oldBytes);
  panel.findChild<QCheckBox*>("generatedHdVariants")->setChecked(true);view.applyTerrainHdVariantSelectionPolicy();
  QVERIFY(on.initializeVirtualMaterialField(ETerrainTileLayer::Land,atlas.catalog(),&report,ETerrainMaterialFieldRecovery::RecoverDeterministically));
  CTerrainVirtualMaterialField field;QVERIFY(on.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false));
  QVector<quint16> placed,pinned;QVector<qint32> cellTypes;QVector<bool> preserve;
  for(int y=0;y<on.tileCountY();++y)for(int x=0;x<on.tileCountX();++x){auto p=on.packedTile(x,y,ETerrainTileLayer::Land);placed.append(p);cellTypes.append(on.tileType(p));preserve.append(true);
   bool found=false;auto over=on.packedTile(x,y,ETerrainTileLayer::Overlay,&found);if(found&&over!=0xffff)pinned.append(over);}
  QVector<STerrainAtlasAnimationRange> animations;for(const auto& a:map.mapInfo().animatedTiles){STerrainAtlasAnimationRange r;r.tileIndex=a.tileIndex;r.frameCount=a.frameCount;animations.append(r);}
  CTerrainVirtualAtlas publisher(atlas.catalog(),atlas.activeTileImages(),145,{}, {},true,atlas.activeTileTypes(),types);
  auto final=publisher.finalize(field,profile.mprNamespace,4096,atlas.activeTileTypes(),&report,pinned,cellTypes,placed,{},&atlas.catalog(),preserve,animations);
  QVERIFY2(final.success,qPrintable(final.error+report.toText()));STerrainWorkingFinalizationOrigins origins;
  QVERIFY(atlas.restoreFinalizedProvenance(&final,&origins,profile.mprNamespace,field.stableSha256(),pinned,&report));
  QVector<QImage> decoded;for(const auto& page:final.pageMmpPayloads)for(int slot=0;slot<64;++slot){QImage tile;QVERIFY(ei::decodeMmpDxt1AtlasTile(page,slot,&tile));decoded.append(tile);}
  const auto savedProfile=CTerrainAtlasProfileStore::portableFinalizedProfile(final,decoded,profile,ETerrainAtlasLayer::Land,{},types,&report);
  const auto package=CTerrainVirtualAtlas::buildPortablePackage(map,on,final,ETerrainTileLayer::Land,&report);QVERIFY2(package.success,qPrintable(package.error+report.toText()));
  CResArchiveDocument savedArchive;QVERIFY(savedArchive.loadBytes(package.resourceArchiveBytes));
  SResArchiveNewMember profileMember;profileMember.name=CTerrainAtlasProfileStore::embeddedMemberName(savedProfile.mprNamespace);profileMember.payload=savedProfile.toBytes();QByteArray resourceBytes;
  QVERIFY(savedArchive.buildWithPublishedMembers({profileMember},true,&resourceBytes));QVERIFY(write(root+"/painted-variants.res",resourceBytes));
  QVERIFY(write(root+"/painted-variants.mpr",package.mprBytes));QVERIFY(write(root+"/painted-variants-profile.json",savedProfile.toBytes()));
  CMprDocument reopened;QVERIFY(reopened.load(QFileInfo(root+"/painted-variants.mpr"),&report));CTerrainEditModel again;QVERIFY(again.load(reopened,&report));
  int restored=0;for(const auto& d:savedProfile.slotDefinitions)if(d.sourceNamespace.startsWith("hd-variant:")){
   const auto id=d.sourceNamespace.mid(11);for(const auto& e:bank.entries)if(e.id==id){QCOMPARE(ei::terrainTileMipChain(final.pageMmpPayloads[d.slot/64],d.slot%64),e.blocks);++restored;}}
  QVERIFY(restored>0);CTerrainHdSources hd;const auto hdCfg=QJsonDocument::fromJson(read(root+"/hd-fixture.json")).object();QStringList sources;for(const auto& p:hdCfg["sources"].toArray())sources.append(p.toString());QString error;
  QVERIFY2(hd.open(sources,&error,nullptr,root+"/variant-hd-cache"),qPrintable(error));
  QVERIFY(hd.saveCache());
  auto converted=rebuildTerrainAtlasHd(savedProfile,final.pageMmpPayloads,hd,true,reopened.mapInfo().animatedTiles);QVERIFY2(converted.success,qPrintable(converted.error));
  QVERIFY2(prepareTerrainHdPublication(reopened,{},&converted),qPrintable(converted.error));
  QVERIFY(write(root+"/painted-variants-hd.mpr",converted.mprBytes));CMprDocument hdMap;QVERIFY(hdMap.loadBytes(converted.mprBytes,"hd.mpr",&report));
  QCOMPARE(hdMap.mapInfo().tileTypes,reopened.mapInfo().tileTypes);QVERIFY(hdMap.mapInfo().animatedTiles==reopened.mapInfo().animatedTiles);
  int retained=0;for(const auto& d:converted.profile.slotDefinitions)if(d.sourceNamespace.startsWith("hd-variant:"))
   for(const auto& e:bank.entries)if(e.id==d.sourceNamespace.mid(11)){QCOMPARE(ei::terrainTileMipChain(converted.pages[d.slot/64],d.slot%64),e.blocks);++retained;}
  QCOMPARE(retained,restored);QVERIFY(savedArchive.loadBytes(resourceBytes));
  QByteArray hdResources=resourceBytes;
  if(!converted.publishedMembers.isEmpty())QVERIFY(savedArchive.buildWithPublishedMembers(converted.publishedMembers,true,&hdResources));
  QVERIFY(write(root+"/painted-variants-hd.res",hdResources));
  CResArchiveIndex resourceReadback;QVERIFY(resourceReadback.open(root+"/painted-variants-hd.res"));
  for(const auto& member:converted.publishedMembers)QCOMPARE(resourceReadback.readMember(member.name),member.payload);
  for(int page=0;page<converted.pages.size();++page)QCOMPARE(resourceReadback.readMember(QString("%1%2.mmp").arg(hdMap.mapInfo().innerNamespace.toLower()).arg(page,3,10,QLatin1Char('0'))),converted.pages[page]);
  qInfo()<<"Retained generated identities:"<<restored<<"HD pages:"<<converted.pages.size();view.m_landscapeTools=nullptr;
 }
};
int main(int argc,char** argv){ei::TestClipboardSession clipboard(true);if(!clipboard.prepared())return 2;QApplication app(argc,argv);if(!clipboard.install())return 2;TerrainHdVariantBankTests test;return QTest::qExec(&test,argc,argv);}
#include "terrain_hd_variant_bank_tests.moc"

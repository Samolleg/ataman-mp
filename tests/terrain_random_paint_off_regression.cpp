#include <QCoreApplication>
#include <algorithm>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QTextStream>
#include <QCryptographicHash>
#include "terrain_atlas_profile.h"
#include "terrain_edit_model.h"
#include "terrain_virtual_atlas.h"
#include "mpr_document.h"
#include "endurance_document.h"
QByteArray bytes(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;QTextStream out(stdout);
 auto fixture=QJsonDocument::fromJson(bytes(argv[1])).object();auto atlasJson=QJsonDocument::fromJson(bytes(fixture["atlas"].toString())).object();
 CValidationReport report;STerrainAtlasProfile profile;
 if(!STerrainAtlasProfile::fromJson(atlasJson["catalog"].toObject(),&profile,&report))return 3;
 auto catalog=profile.usableAnalysis(&report).overlayCatalogWithLandFamilies();
 QVector<QImage> images;for(int i=0;i<profile.atlasSlotCount;++i)images.append(QImage(fixture["tiles"].toString()+QString("/%1.png").arg(i)));
 QMap<qint32,qint32> materialTypes;for(const auto& f:profile.families)materialTypes.insert(f.id,f.tileType);
 QVector<qint32> field;auto map=QJsonDocument::fromJson(bytes(fixture["map"].toString())).object();for(auto v:map["field"].toArray())field.append(v.toInt());
 CMprDocument document;if(!document.loadBytes(bytes(fixture["mpr"].toString()),"isolated.mpr",&report))return 4;
 QDir().mkpath(argv[2]);
 for(int mode=0;mode<6;++mode){
  CTerrainEditModel model;if(!model.load(document,&report))return 5;model.enableWorkingAtlasSlots(4096);
  if(!model.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,field,field,&report))return 6;
  CTerrainWorkingAtlas atlas(catalog,images,17,4096,{}, {},document.mapInfo().tileTypes,materialTypes);
  STerrainTileBrush brush;brush.centerX=114;brush.centerY=100;brush.radius=1;brush.shape=ETerrainTileBrushShape::Square;
  STerrainTileCommand command;
  if(mode==0)command=model.makeVirtualMaterialPaint(114,100,brush,ETerrainTileLayer::Land,1974896831,atlas.catalog(),17,&report,false);
  if(mode==1)command=model.makeSemanticVertexPaint(114,100,brush,ETerrainTileLayer::Land,1974896831,atlas.catalog(),17,&report);
  if(mode==2)command=model.makeTilePaint(brush,ETerrainTileLayer::Land,0,0,1,atlas.catalog(),&report);
  if(mode==3)command=model.makeVirtualTileMaterialPaint(brush,ETerrainTileLayer::Land,1974896831,atlas.catalog(),17,&report,false);
  if(mode==4)command=model.makeSemanticTilePaint(brush,ETerrainTileLayer::Land,1974896831,atlas.catalog(),17,&report);
  if(mode==5){STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=1974896831;command=model.makeUniformTilePaint(brush,ETerrainTileLayer::Land,pure,atlas.catalog(),17,false,&report);}
  if(command.isEmpty()||report.hasErrors()||!model.apply(command,true,&report)){out<<report.toText();return 7;}
  if(mode==0 || mode==3){
   CTerrainVirtualMaterialField v;model.copyVirtualMaterialField(ETerrainTileLayer::Land,&v,false);QSet<int> cells;
   for(const auto& p:command.semanticAudit.paintedVertices)for(int y:{p.y()-1,p.y()})for(int x:{p.x()-1,p.x()})cells.insert(y*model.tileCountX()+x);
   QMap<int,quint64> usage;
   auto ordered=cells.values();std::sort(ordered.begin(),ordered.end());
   for(int c:ordered){int x=c%model.tileCountX(),y=c/model.tileCountX();atlas.resolve(v.cellSignature(x,y),x,y,&usage,{},nullptr,&report,1974896831);}
   model.installWorkingAtlasSlotTypes(atlas.activeTileTypes());
   auto resolve=model.makeVirtualStrokeResolution(command,atlas.catalog(),17,&report,false,1974896831);
   if(report.hasErrors()||!model.apply(resolve,true,&report)){out<<report.toText();return 8;}
  }
  QByteArray result;QDataStream data(&result,QIODevice::WriteOnly);data.setByteOrder(QDataStream::LittleEndian);
  for(int y=0;y<model.tileCountY();++y)for(int x=0;x<model.tileCountX();++x)data<<model.packedTile(x,y,ETerrainTileLayer::Land);
  QVector<qint32> current,baseline;model.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&current,&baseline);data<<current<<baseline<<atlas.activeTileTypes();
  for(const auto& image:atlas.activeTileImages()){auto rgba=image.convertToFormat(QImage::Format_RGBA8888);data<<rgba.width()<<rgba.height();for(int y=0;y<rgba.height();++y)data.writeRawData(reinterpret_cast<const char*>(rgba.constScanLine(y)),rgba.width()*4);}
  QFile f(QString(argv[2])+QString("/mode%1.bin").arg(mode));if(!f.open(QIODevice::WriteOnly)||f.write(result)!=result.size())return 9;
  out<<"mode="<<mode<<" bytes="<<result.size()<<" sha256="<<QCryptographicHash::hash(result,QCryptographicHash::Sha256).toHex()<<"\n";
 }
 return 0;
}

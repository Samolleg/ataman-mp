// Version-neutral harness linked to the accepted version144 application objects.
// The candidate reference comes from the 145 integration test with its switch OFF.
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include "terrain_edit_model.h"
#include "terrain_atlas_profile.h"
#include "mpr_document.h"
#include "verified_clipboard.h"
namespace {
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
}
int main(int argc,char** argv){
 ei::TestClipboardSession clipboard(true);if(!clipboard.prepared())return 2;
 QApplication app(argc,argv);if(!clipboard.install()||argc!=2)return 2;
 auto cfg=QJsonDocument::fromJson(read(QString::fromLocal8Bit(argv[1]))).object();const auto root=cfg["realRoot"].toString();
 CMprDocument map;STerrainAtlasProfile profile;CTerrainEditModel model;
 if(!map.loadBytes(read(root+"/source.mpr"),"private-dejavu.mpr")
    ||!STerrainAtlasProfile::fromJson(QJsonDocument::fromJson(read(root+"/atlas.json")).object()["catalog"].toObject(),&profile)
    ||!model.load(map))return 3;
 const auto catalog=profile.usableAnalysis().overlayCatalogWithLandFamilies();model.enableWorkingAtlasSlots(4096);
 const int slot=cfg["slot"].toInt(-1);if(slot<0)return 3;
 STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=6;brush.shape=ETerrainTileBrushShape::Square;
 if(!model.apply(model.makeTilePaint(brush,ETerrainTileLayer::Land,slot/64,slot%64,0),true)
    ||!model.apply(model.makeRandomTilePaint(catalog,145,QRect(2,2,12,12)),true))return 3;
 QByteArray bytes;for(int y=0;y<model.tileCountY()/16;++y)for(int x=0;x<model.tileCountX()/16;++x)bytes+=model.sectorPayload(x,y);
 QFile out(cfg["output"].toString());if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size())return 3;out.close();
 const bool same=bytes==read(cfg["reference"].toString());QTextStream(stdout)<<(same?"PASS ":"FAIL ")<<"144 versus 145 OFF: "<<bytes.size()<<" sector bytes\n";return same?0:1;
}

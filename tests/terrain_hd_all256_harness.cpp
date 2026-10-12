#include "terrain_hd_republish.h"
#include "res_archive_index.h"
#include "terrain_atlas_profile.h"
#include "terrain_semantic_field_store.h"
#include "mpr_document.h"
#include <QCoreApplication>
#include <QtEndian>
#include <algorithm>
#include <QFileInfo>
#include <QDir>
#include "mmp_codec.h"
#include <QTextStream>
#include <QCryptographicHash>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if(argc==4&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--animations")) {
        CResArchiveIndex archive;CMprDocument map;QTextStream out(stdout);
        if(!archive.open(QString::fromLocal8Bit(argv[2]))||!map.load(QFileInfo(QString::fromLocal8Bit(argv[3]))))return 3;
        for(const auto& a:map.mapInfo().animatedTiles) {
            out<<"range="<<a.tileIndex<<","<<a.frameCount<<"\n";
            for(int i=0;i<a.frameCount;++i) {
                const int slot=a.tileIndex+i;QImage image;
                const auto page=archive.readMember(QStringLiteral("%1%2.mmp").arg(map.mapInfo().innerNamespace).arg(slot/64,3,10,QLatin1Char('0')));
                if(!ei::decodeMmpDxt1AtlasTile(page,slot%64,&image))return 4;
                image=image.convertToFormat(QImage::Format_RGBA8888);
                out<<"frame="<<i<<" size="<<image.width()<<"x"<<image.height()<<" rgba="
                   <<QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),int(image.sizeInBytes())),QCryptographicHash::Sha256).toHex()<<"\n";
            }
        }
        return 0;
    }
    if(argc==4&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--compare-semantics")) {
        CMprDocument before,after;STerrainSemanticFieldDocument a,b;
        if(!before.load(QFileInfo(QString::fromLocal8Bit(argv[2])))
           ||!after.load(QFileInfo(QString::fromLocal8Bit(argv[3])))
           ||!STerrainSemanticFieldDocument::fromBytes(before.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&a)
           ||!STerrainSemanticFieldDocument::fromBytes(after.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&b))return 3;
        const bool same=a.land==b.land&&a.overlay==b.overlay&&a.vertexWidth==b.vertexWidth&&a.vertexHeight==b.vertexHeight
            &&a.hasRandomPaintSeed==b.hasRandomPaintSeed&&a.randomPaintSeed==b.randomPaintSeed
            &&before.mapInfo().materials==after.mapInfo().materials&&before.mapInfo().animatedTiles==after.mapInfo().animatedTiles;
        QTextStream(stdout)<<"land="<<(a.land==b.land)<<" overlay="<<(a.overlay==b.overlay)
            <<" seed="<<(a.hasRandomPaintSeed==b.hasRandomPaintSeed&&a.randomPaintSeed==b.randomPaintSeed)
            <<" materials="<<(before.mapInfo().materials==after.mapInfo().materials)
            <<" animation="<<(before.mapInfo().animatedTiles==after.mapInfo().animatedTiles)<<endl;
        return same?0:4;
    }
    if((argc==5||argc==6)&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--dump-slot")) {
        CResArchiveIndex archive;CMprDocument map;STerrainAtlasProfile profile;
        if(!archive.open(QString::fromLocal8Bit(argv[2]))||!map.load(QFileInfo(QString::fromLocal8Bit(argv[3])))
           ||!STerrainAtlasProfile::fromBytes(archive.readMember(CTerrainAtlasProfileStore::embeddedMemberName(map.mapInfo().innerNamespace)),&profile))return 3;
        const int slot=QString::fromLocal8Bit(argv[4]).toInt();QTextStream out(stdout);
        const auto definition=std::find_if(profile.slotDefinitions.cbegin(),profile.slotDefinitions.cend(),[&](const STerrainAtlasProfileSlot& d){return d.slot==slot;});
        if(definition==profile.slotDefinitions.cend())return 4;
        const auto& d=*definition;out<<"slot="<<slot<<" assigned="<<d.assigned<<" ambiguous="<<d.ambiguous<<" signature="
            <<d.signature.northWest<<','<<d.signature.northEast<<','<<d.signature.southEast<<','<<d.signature.southWest
            <<" mixedEdgeRecipes="<<d.mixedEdgeRecipes.size()<<endl;
        if(argc==6) {
            const QString destination=QString::fromLocal8Bit(argv[5]);
            const QString parent=QFileInfo(destination).absolutePath();
            if(!QDir::cleanPath(parent).contains(QStringLiteral("/build-tests-")))return 5;
            QImage image;
            const auto page=archive.readMember(QStringLiteral("%1%2.mmp").arg(profile.mprNamespace).arg(slot/64,3,10,QLatin1Char('0')));
            if(!ei::decodeMmpDxt1AtlasTile(page,slot%64,&image)||!image.save(destination))return 5;
        }
        for(qint32 id:{d.signature.northWest,d.signature.northEast,d.signature.southEast,d.signature.southWest}) {
            const auto f=std::find_if(profile.families.cbegin(),profile.families.cend(),[&](const STerrainAtlasProfileFamily& family){return family.id==id;});
            if(f!=profile.families.cend())out<<"family "<<id<<" name="<<f->name<<" class="<<f->tileType<<" layer="<<int(f->layer)<<endl;
        }
        STerrainSemanticFieldDocument field;const auto semantic=map.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
        if(!semantic.isEmpty()&&STerrainSemanticFieldDocument::fromBytes(semantic,&field)) {
            QMap<QString,int> counts;
            for(const auto& sector:map.sectors())if(sector.landTileOffset&&sector.memberIndex>=0&&sector.memberIndex<map.members().size()) {
                const auto& payload=map.members()[sector.memberIndex].payload;
                for(int cell=0;cell<256;++cell) {
                    const auto packed=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(payload.constData()+sector.landTileOffset+cell*2)));
                    if(packed.atlasSlot()!=slot)continue;
                    const int x=sector.sectorX*16+cell%16,y=sector.sectorY*16+cell/16;
                    auto v=[&](int vx,int vy){return field.land[vy*field.vertexWidth+vx];};
                    STerrainTransitionSignature s;s.northWest=v(x,y);s.northEast=v(x+1,y);s.southEast=v(x+1,y+1);s.southWest=v(x,y+1);
                    s=CTerrainTransitionCatalog::mirroredGridY(CTerrainTransitionCatalog::rotated(s,-int(packed.rotation)));
                    const QString key=QStringLiteral("%1,%2,%3,%4").arg(s.northWest).arg(s.northEast).arg(s.southEast).arg(s.southWest);
                    ++counts[key];
                }
            }
            for(auto it=counts.cbegin();it!=counts.cend();++it)out<<"semantic "<<it.key()<<" count="<<it.value()<<endl;
        } else out<<"semantic=absent-or-invalid"<<endl;
        return 0;
    }
    if(argc != 2) return 2;
    return runTerrainHdRepublish(QString::fromLocal8Bit(argv[1]));
}

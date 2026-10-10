// Read-only QCoreApplication: links the production MPR/RES/DXT loaders, no UI/GL.
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QSet>
#include <QTextStream>
#include <cstdio>
#include <cstdlib>
#include "mpr_document.h"
#include "terrain_edit_model.h"
#include "endurance_document.h"
#include "res_archive_document.h"
#include "mmp_inspector.h"
#include "mmp_codec.h"
#undef slots

static QByteArray read(const QString& p) {
    QFile f(p); if(!f.open(QIODevice::ReadOnly)) qFatal("Input open failed"); return f.readAll();
}
static QString sha(const QByteArray& b) {
    return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());
}
static void require(bool ok,const char* message) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::fflush(stderr); std::exit(3); }
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc!=3) return 2;
    const QDir kit(app.arguments()[1]);
    const auto catalog=QJsonDocument::fromJson(read(kit.filePath("fixtures.json"))).object();
    const auto baselineBytes=read(kit.filePath("baseline/zone6.mpr"));
    require(sha(baselineBytes)==catalog["sourceMprSha256"].toString(),"Baseline hash mismatch");
    CMprDocument baseline; CTerrainEditModel original; CValidationReport report;
    require(baseline.loadBytes(baselineBytes,"baseline",&report)&&original.load(baseline,&report),"Baseline load failed");
    QJsonArray results;
    for(const auto& value:catalog["fixtures"].toArray()) {
        const auto item=value.toObject(); const QString id=item["id"].toString();
        const QDir folder(kit.filePath(id));
        const auto manifest=QJsonDocument::fromJson(read(folder.filePath("manifest.json"))).object();
        const auto mprBytes=read(folder.filePath(id+".mpr"));
        const auto resBytes=read(folder.filePath(id+"-atlas.res"));
        require(sha(mprBytes)==item["mprSha256"].toString()&&sha(resBytes)==item["resSha256"].toString(),"Fixture hash mismatch");
        require(sha(read(folder.filePath(id+".mob")))==catalog["sourceMobSha256"].toString(),"MOB changed");
        CMprDocument doc; CTerrainEditModel model; CResArchiveDocument res; QStringList errors;
        require(doc.loadBytes(mprBytes,id,&report)&&model.load(doc,&report),"MPR/model load failed");
        require(res.loadBytes(resBytes,id,&errors),"RES load failed");
        const auto& map=doc.mapInfo(); const auto& source=baseline.mapInfo();
        const int slots=item["slots"].toInt(),pixels=item["pixels"].toInt(),pages=slots/64;
        require(map.tileCount==quint32(slots)&&map.textureCount==quint32(pages)&&map.tileSize==quint32(pixels)&&map.textureSize==quint32(pixels*8),"MP dimensions disagree");
        require(map.materials==source.materials&&map.animatedTiles==source.animatedTiles,"Materials/animations changed");
        const auto provenance=manifest["sourceForSlot"].toArray();
        require(provenance.size()==slots&&map.tileTypes.size()==slots,"Class/provenance length mismatch");
        for(int s=0;s<slots;++s) {
            const int predecessor=provenance[s].toInt();
            require(predecessor>=0&&predecessor<int(source.textureCount)*64,"Invalid slot provenance");
            // Stock physical tail can exceed the authored class table; packer
            // explicitly gives those unused padding slots class zero.
            const int expected=predecessor<source.tileTypes.size()?source.tileTypes[predecessor]:0;
            require(map.tileTypes[s]==expected,"Slot class changed");
        }
        require(res.members().size()==pages,"RES page count mismatch");
        int decodedMips=0,decodedSlots=0; qint64 dxtBytes=0,rgbaBytes=0;
        QJsonArray pageEvidence;
        for(const auto& nameValue:item["resourceMembers"].toArray()) {
            const QString name=nameValue.toString();
            require(res.uniqueMemberIndex(name)>=0,"Missing/ambiguous page");
            const auto b=res.memberPayload(name); const auto inspected=ei::inspectMmp(b);
            require(inspected.valid&&inspected.formatName=="DXT1"&&inspected.width==pixels*8&&inspected.height==pixels*8,"MMP inspector failed");
            require(inspected.authoredMipSizes.last()==QSize(1,1),"Incomplete mip chain");
            int offset=76;
            for(const auto& size:inspected.authoredMipSizes) {
                const int count=qMax(1,(size.width()+3)/4)*qMax(1,(size.height()+3)/4)*8;
                QImage decoded; QString error;
                require(offset+count<=b.size()&&ei::decodeDxtImage(b.mid(offset,count),size.width(),size.height(),ei::DxtFormat::Dxt1,&decoded,&error),"Mip decoding failed");
                require(decoded.size()==size,"Mip dimensions mismatch");
                offset+=count;dxtBytes+=count;rgbaBytes+=qint64(size.width())*size.height()*4;++decodedMips;
            }
            require(offset==b.size(),"Trailing/missing mip bytes");
            for(int s=0;s<64;++s) {
                QImage tile;QString error;
                require(ei::decodeMmpDxt1AtlasTile(b,s,&tile,&error)&&tile.size()==QSize(pixels,pixels),"Slot decoding failed");++decodedSlots;
            }
            pageEvidence.append(QJsonObject{{"name",name},{"sha256",sha(b)},{"mips",inspected.authoredMipSizes.size()},{"bytes",b.size()}});
        }
        QSet<int> used,usedLand,usedPages;int landRefs=0,overlayRefs=0;
        for(int y=0;y<model.tileCountY();++y)for(int x=0;x<model.tileCountX();++x) {
            const auto before=original.packedTile(x,y,ETerrainTileLayer::Land),after=model.packedTile(x,y,ETerrainTileLayer::Land);
            const int slot=after&0x3fff;
            require(slot<slots&&provenance[slot].toInt()==(before&0x3fff),"Invalid SEC predecessor");
            require((before&0xc000)==(after&0xc000)&&original.tileType(before)==model.tileType(after),"Rotation/class changed");
            used.insert(slot);usedLand.insert(slot);usedPages.insert(slot/64);++landRefs;
            const auto over=model.packedTile(x,y,ETerrainTileLayer::Overlay);
            require(over==original.packedTile(x,y,ETerrainTileLayer::Overlay)&&model.overlayMaterialIndex(x,y)==original.overlayMaterialIndex(x,y),"Overlay changed");
            if(model.overlayMaterialIndex(x,y)>=0&&over!=0xffff) {
                require((over&0x3fff)<slots,"Invalid overlay SEC reference");
                used.insert(over&0x3fff);usedPages.insert((over&0x3fff)/64);++overlayRefs;
            }
        }
        require(usedPages.size()==pages&&used.size()==item["referencedSlots"].toInt(),"Reference coverage mismatch");
        for(const auto& sector:baseline.sectors()) {
            auto before=baseline.members()[sector.memberIndex].payload;
            auto after=model.sectorPayload(sector.sectorX,sector.sectorY);
            before.replace(int(sector.landTileOffset),512,QByteArray(512,0));
            after.replace(int(sector.landTileOffset),512,QByteArray(512,0));
            require(before==after,"Unrelated SEC bytes changed");
        }
        for(const auto& member:baseline.members()) if(member.kind==EMprMemberKind::Unknown)
            require(doc.memberPayload(member.name)==member.payload,"Unknown member changed");
        results.append(QJsonObject{{"id",id},{"status","PASS"},{"pages",pages},{"decodedSlots",decodedSlots},{"decodedMips",decodedMips},
            {"sectors",doc.sectors().size()},{"landReferences",landRefs},{"activeOverlayReferences",overlayRefs},
            {"referencedSlots",used.size()},{"referencedLandSlots",usedLand.size()},{"referencedPages",usedPages.size()},
            {"classMismatch",0},{"rotationMismatch",0},{"overlayMismatch",0},{"unrelatedSecBytesPreserved",true},
            {"materialsAndAnimationsPreserved",true},{"dxtBytes",double(dxtBytes)},{"rgbaMipBytes",double(rgbaBytes)},
            {"mprSha256",sha(mprBytes)},{"resSha256",sha(resBytes)},{"pageEvidence",pageEvidence}});
        QTextStream(stdout)<<id<<" PASS pages="<<pages<<" slots="<<decodedSlots<<" mips="<<decodedMips<<"\n";
    }
    QFile out(app.arguments()[2]);require(out.open(QIODevice::WriteOnly),"Output open failed");
    const auto bytes=QJsonDocument(QJsonObject{{"loader","production Ataman QCoreApplication; no window/GL"},{"fixtures",results}}).toJson();
    require(out.write(bytes)==bytes.size(),"Output short write");return 0;
}

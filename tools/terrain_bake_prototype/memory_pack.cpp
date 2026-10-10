// Offline, private capacity fixtures; never installs or launches the game.
// Uses ordinary Ataman codecs and transaction/reopen validation.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include "endurance_document.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"
#include "terrain_virtual_atlas.h"
#include "terrain_atlas_profile.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
#include "mmp_inspector.h"

static QByteArray sha(const QByteArray& b) {
    return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();
}
static QByteArray read(const QString& p) {
    QFile f(p); if(!f.open(QIODevice::ReadOnly)) qFatal("Cannot read input");
    return f.readAll();
}
static void save(const QString& p,const QByteArray& b) {
    QFile f(p); if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size()) qFatal("Cannot write private output");
}
static QByteArray resourceMember(const QString& path,const QString& name) {
    QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {};
    QDataStream s(&f);s.setByteOrder(QDataStream::LittleEndian);
    quint32 magic,n,off,namesSize;s>>magic>>n>>off>>namesSize;
    if(magic!=0x019ce23c || n>10000000 || quint64(off)+quint64(n)*22+namesSize>quint64(f.size())) return {};
    f.seek(off);
    struct R {quint32 next,size,off,time;quint16 len;quint32 name;};
    QVector<R> rows(int(n),R{});
    for(auto& r:rows)s>>r.next>>r.size>>r.off>>r.time>>r.len>>r.name;
    const QByteArray names=f.read(namesSize);
    for(const auto& r:rows) if(quint64(r.name)+r.len<=quint64(names.size())
       && QString::fromLatin1(names.mid(r.name,r.len)).compare(name,Qt::CaseInsensitive)==0
       && quint64(r.off)+r.size<=quint64(f.size())) {
        f.seek(r.off);return f.read(r.size);
    }
    return {};
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=2) return 2;
    const QJsonObject cfg=QJsonDocument::fromJson(read(app.arguments()[1])).object();
    QDir root(cfg["outputRoot"].toString());
    if(root.dirName()!="build-tests-terrain-bake-stage-1") return 2;
    const QByteArray source=read(cfg["sourceMpr"].toString());
    if(sha(source)!=cfg["sourceMprSha256"].toString().toLatin1()) qFatal("Source MPR changed");
    CMprDocument doc;CValidationReport report;CTerrainEditModel model;
    if(!doc.loadBytes(source,cfg["sourceMpr"].toString(),&report)||!model.load(doc,&report)) qFatal("Source load failed");
    const int sourceSlots=cfg["sourceSlots"].toInt();
    const auto map=doc.mapInfo();
    if(sourceSlots!=int(map.textureCount)*64) qFatal("Source slot count mismatch");
    QVector<QImage> originals;
    for(int i=0;i<sourceSlots;++i) {
        QImage im(QDir(cfg["atlasDirectory"].toString()).filePath(QString::number(i)+".png"));
        if(im.isNull()||im.width()!=128||im.height()!=128) qFatal("Source texture missing or unexpected size");
        originals.append(im.convertToFormat(QImage::Format_RGBA8888));
    }
    const auto atlas=cfg["sourceAtlas"].toObject();
    if(CTerrainAtlasProfileInference::pixelSha256(originals)!=atlas["pixelSha256"].toString().toLatin1()) qFatal("Source pixel binding drift");
    QVector<QByteArray> originalPages;
    for(const auto& v:atlas["pages"].toArray()) {
        const auto p=v.toObject();const auto b=resourceMember(p["source"].toString(),p["name"].toString());
        if(b.isEmpty()||sha(b)!=p["sha256"].toString().toLatin1()) qFatal("Source RES member drift");
        originalPages.append(b);
    }
    const int w=model.tileCountX(),h=model.tileCountY();
    QVector<quint16> land;QSet<int> animated,firstSeen;
    for(const auto& a:map.animatedTiles) for(int s=a.tileIndex;s<int(a.tileIndex)+qMax(1,int(a.frameCount));++s) animated.insert(s);
    QVector<int> eligible;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const quint16 p=model.packedTile(x,y,ETerrainTileLayer::Land);land.append(p);
        const int s=p&0x3fff;
        // Retain one original reference and every animated reference.
        if(firstSeen.contains(s)&&!animated.contains(s)) eligible.append(y*w+x);
        firstSeen.insert(s);
    }
    std::sort(eligible.begin(),eligible.end(),[](int a,int b) {
        const quint32 ha=quint32(a)*2654435761u,hb=quint32(b)*2654435761u;
        return ha<hb;
    });
    QJsonArray fixtures;
    for(int pixels:{128,256})for(int count:{2048,4096}) {
        if(eligible.size()<count-sourceSlots) qFatal("Insufficient distinct nonanimated cells");
        const QString id=QString("tb1_%1_%2").arg(count).arg(pixels);
        const QString dir=root.filePath("memory-kit/"+id);QDir().mkpath(dir);
        QVector<int> sourceForSlot;
        for(int s=0;s<sourceSlots;++s)sourceForSlot.append(s);
        STerrainAtlasFinalization f;f.success=true;f.requiredSlots=count;f.textureCount=count/64;
        f.isolatedNamespace=id;f.packedCells=land;
        for(int s=0;s<sourceSlots;++s)f.authoredSlotRemap.insert(s,s);
        for(const auto& a:map.animatedTiles) {STerrainAtlasAnimationRange r;r.tileIndex=a.tileIndex;r.frameCount=a.frameCount;f.animations.append(r);}
        for(int s=sourceSlots;s<count;++s) {
            const int cell=eligible[s-sourceSlots];const quint16 p=land[cell];
            sourceForSlot.append(p&0x3fff);f.packedCells[cell]=quint16(s|(p&0xc000));
        }
        for(int s=0;s<count;++s) {
            const int original=sourceForSlot[s];
            f.tileTypes.append(original<map.tileTypes.size()?map.tileTypes[original]:0);
        }
        qint64 payloadBytes=0;
        for(int page=0;page<f.textureCount;++page) {
            QVector<QImage> tiles;
            for(int local=0;local<64;++local) {
                QImage im=originals[sourceForSlot[page*64+local]];
                if(pixels!=128) im=im.scaled(pixels,pixels,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                tiles.append(im);
            }
            const QImage image=CTerrainVirtualAtlas::composePage(tiles,0,pixels);
            if(page==0) {f.pages.append(image);STerrainFinalizedTile t;t.pixels=tiles[0];f.tiles.append(t);}
            QString error;
            QByteArray b=(pixels==128&&page<originalPages.size())?originalPages[page]:ei::encodeMmpDxt1(image,true,&error);
            const auto inspected=ei::inspectMmp(b);
            if(!inspected.valid||inspected.baseImage.width()!=pixels*8) qFatal("MMP encode/reopen failed");
            f.pageMmpPayloads.append(b);payloadBytes+=b.size();
            QTextStream(stdout)<<id<<" page "<<page+1<<"/"<<f.textureCount<<"\n";
        }
        const auto package=CTerrainVirtualAtlas::buildPortablePackage(doc,f,ETerrainTileLayer::Land,&report);
        if(!package.success) {QTextStream(stderr)<<package.error<<"\n"<<report.toText();return 3;}
        CMprDocument reopen;CTerrainEditModel check;CValidationReport verify;
        if(!reopen.loadBytes(package.mprBytes,id+".mpr",&verify)||!check.load(reopen,&verify)) qFatal("Fixture reopen failed");
        QSet<int> usedPages,usedSlots,usedLandSlots;
        int classMismatch=0,rotationMismatch=0,overlayMismatch=0;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const auto before=model.packedTile(x,y,ETerrainTileLayer::Land),after=check.packedTile(x,y,ETerrainTileLayer::Land);
            const int s=after&0x3fff;usedSlots.insert(s);usedLandSlots.insert(s);usedPages.insert(s/64);
            classMismatch+=model.tileType(before)!=check.tileType(after);
            rotationMismatch+=(before&0xc000)!=(after&0xc000);
            overlayMismatch+=model.packedTile(x,y,ETerrainTileLayer::Overlay)!=check.packedTile(x,y,ETerrainTileLayer::Overlay);
            overlayMismatch+=model.overlayMaterialIndex(x,y)!=check.overlayMaterialIndex(x,y);
            if(check.overlayMaterialIndex(x,y)>=0) {
                const auto over=check.packedTile(x,y,ETerrainTileLayer::Overlay);
                if(over!=0xffff) {usedSlots.insert(over&0x3fff);usedPages.insert((over&0x3fff)/64);}
            }
        }
        if(classMismatch||rotationMismatch||overlayMismatch||usedPages.size()!=f.textureCount) qFatal("Fixture conservation or page coverage failed");
        // Verify entire SEC payload after masking only the intended land array.
        for(const auto& sector:doc.sectors()) {
            const auto candidate=check.sectorPayload(sector.sectorX,sector.sectorY);
            auto before=doc.members()[sector.memberIndex].payload,after=candidate;
            before.replace(int(sector.landTileOffset),512,QByteArray(512,'\0'));
            after.replace(int(sector.landTileOffset),512,QByteArray(512,'\0'));
            if(before!=after) qFatal("Unrelated SEC bytes changed");
        }
        save(QDir(dir).filePath(id+".mpr"),package.mprBytes);
        save(QDir(dir).filePath(id+"-atlas.res"),package.resourceArchiveBytes);
        const auto mob=read(cfg["sourceMob"].toString());
        save(QDir(dir).filePath(id+".mob"),mob);
        QJsonArray members;for(const auto& name:package.resourceMembers)members.append(name);
        QJsonObject item{{"id",id},{"slots",count},{"pixels",pixels},{"pages",f.textureCount},
            {"referencedSlots",usedSlots.size()},{"referencedLandSlots",usedLandSlots.size()},{"referencedPages",usedPages.size()},
            {"classMismatch",classMismatch},{"rotationMismatch",rotationMismatch},{"overlayMismatch",overlayMismatch},
            {"unrelatedSecBytesPreserved",true},{"payloadAndMmpHeadersBytes",double(payloadBytes)},
            {"mprSha256",QString(sha(package.mprBytes))},{"resSha256",QString(sha(package.resourceArchiveBytes))},
            {"mobSha256",QString(sha(mob))},{"sourceForSlotCount",sourceForSlot.size()},{"resourceMembers",members}};
        QJsonArray provenance;for(int s:sourceForSlot)provenance.append(s);item["sourceForSlot"]=provenance;
        save(QDir(dir).filePath("manifest.json"),QJsonDocument(item).toJson());
        item.remove("sourceForSlot");fixtures.append(item);
    }
    if(read(cfg["sourceMpr"].toString())!=source) qFatal("Source MPR changed during generation");
    save(root.filePath("memory-kit/fixtures.json"),QJsonDocument(QJsonObject{
        {"sourceMprSha256",QString(sha(source))},{"sourceNamespace",map.innerNamespace},
        {"sourceSlots",sourceSlots},{"sourceTileCount",int(map.tileCount)},
        {"sourceMobSha256",QString(sha(read(cfg["sourceMob"].toString())))},
        {"runtimeValidated",false},{"fixtures",fixtures}}).toJson());
    return 0;
}

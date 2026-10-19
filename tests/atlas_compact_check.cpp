#include "terrain_atlas_compact.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_semantic_field_store.h"
#include "terrain_atlas_recovery_store.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include "staged_save_validator.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QTransform>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTemporaryDir>
#include <QtEndian>

namespace {
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
QByteArray hash(const QByteArray& p){return QCryptographicHash::hash(p,QCryptographicHash::Sha256).toHex();}
bool write(const QString& p,const QByteArray& b){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();}
int failures=0,checks=0;
bool check(bool ok,const QString& name){QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<name<<"\n";QTextStream(stdout).flush();++checks;if(!ok)++failures;return ok;}
QString pageName(const QString& ns,int p){return QString("%1%2.mmp").arg(ns).arg(p,3,10,QLatin1Char('0'));}
quint16 packed(const CMprDocument& m,int x,int y,int layer){for(const auto& s:m.sectors())if(s.sectorX==x/16&&s.sectorY==y/16){if(layer&&(!s.hasOverlay||s.overlayMaterialIndices[(y%16)*16+x%16]<0))return 0xffff;
    const auto b=m.members()[s.memberIndex].payload;return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+int(layer?s.overlayTileOffset:s.landTileOffset)+((y%16)*16+x%16)*2));}return 0xffff;}
QImage patch(const CMprDocument& m,const QVector<QByteArray>& pages){QImage image(1024,1024,QImage::Format_RGBA8888);image.fill(Qt::black);QPainter painter(&image);
    // A fixed 16x16 source patch, both layers with their authored quarter-turns.
    for(int y=0;y<16;++y)for(int x=0;x<16;++x)for(int layer=0;layer<2;++layer){auto value=packed(m,x+64,y+48,layer);if(value==0xffff)continue;
        auto p=SMprPackedTile::decode(value);QImage tile;QString error;if(!ei::decodeMmpDxt1AtlasTile(pages[p.atlasSlot()/64],p.atlasSlot()%64,&tile,&error))continue;
        if(p.rotation)tile=tile.transformed(QTransform().rotate(p.rotation*90),Qt::FastTransformation);
        painter.drawImage(QRect(x*64,y*64,64,64),tile);}return image;}
}
int runAssemblyCatalogCheck()
{
    const QString root=qEnvironmentVariable("EI_ATLAS_COMPACT_ROOT");
    if(!QFileInfo(root).fileName().startsWith("build-tests-atlas-compact")||QFileInfo(root).canonicalFilePath().isEmpty())return 2;
    const QString res=QDir(root).filePath("res/HDPack_Terrain.res");QJsonArray receipts;
    for(const QString mapName:{QString("zone3obr"),QString("zone1")}){
        const QString path=QDir(root).filePath("Maps/"+mapName+".mpr");const QByteArray before=read(path);
        CMprDocument source;CValidationReport report;
        if(!check(source.loadBytes(before,path,&report)&&!report.hasErrors(),mapName+" open source"))return 1;
        const auto m=source.mapInfo();QStringList names;for(int i=0;i<int(m.textureCount);++i)names<<pageName(m.innerNamespace,i);
        const auto profileMember=CTerrainAtlasProfileStore::embeddedMemberName(m.innerNamespace);
        const auto semanticMember=STerrainSemanticFieldDocument::memberName(m.innerNamespace,QFileInfo(path));names<<profileMember<<semanticMember;
        QMap<QString,QByteArray> payloads;QString error;
        if(!check(CTerrainAtlasArchiveBindingResolver::readArchiveMembers(res,names,&payloads,nullptr,&error),"read published source: "+error))return 1;
        STerrainAtlasProfile profile;
        if(!check(STerrainAtlasProfile::fromBytes(payloads.value(profileMember),&profile,&report),"decode profile: "+report.toText()))return 1;
        QVector<QByteArray> pages;for(int i=0;i<int(m.textureCount);++i)pages<<payloads.value(names[i]);
        auto semantic=source.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());if(semantic.isEmpty())semantic=payloads.value(semanticMember);
        auto plan=prepareTerrainAtlasCompact(source,profile,pages,semantic,QFileInfo(path));
        if(!check(plan.error.isEmpty(),mapName+" prepare: "+plan.error))return 1;
        if(!check(!plan.noOp,mapName+" fixture has removable slots"))return 1;
        if(mapName=="zone3obr"){
            // Explicitly exercise shared/unreadable/racing inventory policy
            // without another publication or another game-data test suite.
            QTemporaryDir policyDirectory(QDir(root).filePath("policy-maps-XXXXXX"));const QString policy=policyDirectory.path();
            QByteArray empty(16,0);qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
            CResArchiveDocument shell;QStringList errors;shell.loadBytes(empty,"empty",&errors);
            SResArchiveNewMember page;page.name=pageName(profile.mprNamespace,0);page.payload="owned-policy-fixture";
            QByteArray bytes;shell.buildWithAppendedMembers({page},&bytes,&errors);CResArchiveDocument owned;owned.loadBytes(bytes,"policy",&errors);
            QMap<QString,QByteArray> inventory;QString reason;
            check(!terrainCompactRetiredMembers(owned,profile,path,{policy},&inventory,&reason).isEmpty(),"unreferenced hash-owned namespace eligible");
            check(write(QDir(policy).filePath("shared.mpr"),before),"shared namespace fixture");
            check(terrainCompactRetiredMembers(owned,profile,path,{policy},&inventory,&reason).isEmpty()&&reason.contains("shared"),"shared namespace retained");
            check(!terrainCompactInventoryUnchanged({}, {policy},path),"changed inventory rejected");
            check(terrainCompactRetiredMembers(owned,profile,path,{policy+"/missing"},&inventory,&reason).isEmpty(),"missing inventory retained");
            auto stock=profile;stock.provenance="stock";
            check(terrainCompactRetiredMembers(owned,stock,path,{policy},&inventory,&reason).isEmpty(),"original namespace retained");
            QByteArray removed;QString removalError;
            check(buildTerrainArchiveWithout(owned,{page.name},&removed,&removalError),"owned member removal builds valid empty archive");
            auto corrupt=pages;corrupt[0].chop(1);auto invalid=prepareTerrainAtlasCompact(source,profile,corrupt,semantic,QFileInfo(path));
            check(!invalid.error.isEmpty()&&read(path)==before,"truncated source fails before any write");
            check(estimateTerrainCompactPublication(&plan,profile,path,res,{QDir(root).filePath("Maps"),QDir(root).filePath("BaseMaps")}),"HDPack projection prepared before confirmation");
        }
        CMprDocument candidate;
        if(!check(candidate.loadBytes(plan.mprBytes,path,&report)&&!report.hasErrors(),"fresh candidate reopen"))return 1;
        check(candidate.mapInfo().materials==m.materials&&candidate.mapInfo().maxZ==m.maxZ,"all water/material records and maxZ preserved");
        int maximumPageSize=0;for(const auto& page:plan.pages)maximumPageSize=qMax(maximumPageSize,int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(page.constData()+4))));
        check(candidate.mapInfo().textureSize==quint32(maximumPageSize)&&candidate.mapInfo().tileSize==quint32(maximumPageSize/8),"MP sizes describe maximum native page/tile, independent of page ordering");
        bool exact=true;quint64 cellCount=0;
        QMap<int,QImage> oldImages;
        // Every retained source slot, not just pixels visible in one screenshot.
        for(auto it=plan.remap.cbegin();it!=plan.remap.cend();++it){QImage image;
            exact&=ei::decodeMmpDxt1AtlasTile(pages[it.key()/64],it.key()%64,&image,&error)&&image==plan.images[it.value()];
            exact&=m.tileTypes.value(it.key())==candidate.mapInfo().tileTypes.value(it.value());}
        check(exact,"every retained decoded tile and runtime class exact");
        bool sectors=true;
        for(int s=0;s<source.sectors().size();++s){const auto a=source.sectors()[s];SMprSectorInfo b;bool found=false;
            for(const auto& sector:candidate.sectors())if(sector.sectorX==a.sectorX&&sector.sectorY==a.sectorY){b=sector;found=true;break;}
            if(!found){sectors=false;continue;}
            auto old=source.members()[a.memberIndex].payload,now=candidate.members()[b.memberIndex].payload;
            for(int layer=0;layer<2;++layer){if(layer&&!a.hasOverlay)continue;int offset=int(layer?a.overlayTileOffset:a.landTileOffset);
                for(int i=0;i<256;++i){if(layer&&a.overlayMaterialIndices[i]<0)continue;++cellCount;
                    auto x=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(old.constData()+offset+i*2)));
                    auto y=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(now.constData()+offset+i*2)));
                    sectors&=x.rotation==y.rotation&&plan.remap.value(x.atlasSlot(),-1)==y.atlasSlot();
                    now.replace(offset+i*2,2,old.mid(offset+i*2,2));}}
            sectors&=old==now;}
        check(sectors,"all "+QString::number(cellCount)+" active cells: exact tile+rotation; all other SEC bytes unchanged");
        bool animations=m.animatedTiles.size()==candidate.mapInfo().animatedTiles.size();
        for(int i=0;i<m.animatedTiles.size();++i){auto a=m.animatedTiles[i],b=candidate.mapInfo().animatedTiles[i];animations&=a.frameCount==b.frameCount;
            for(int frame=0;frame<qMax(1,int(a.frameCount));++frame)animations&=plan.remap.value(a.tileIndex+frame,-1)==b.tileIndex+frame;
            animations&=b.tileIndex/64==(b.tileIndex+qMax(1,int(b.frameCount))-1)/64;}
        check(animations,"complete ordered animation ranges contiguous on a native-size page");
        bool families=profile.families.size()==plan.profile.families.size();
        for(int i=0;i<profile.families.size();++i){auto a=profile.families[i],b=plan.profile.families[i];
            a.memberSlots.clear();b.memberSlots.clear();a.thumbnailSlot=b.thumbnailSlot=-1;
            families&=a.id==b.id&&a.name==b.name&&a.tileType==b.tileType&&a.underwater==b.underwater&&a.landAllowed==b.landAllowed&&a.overlayAllowed==b.overlayAllowed&&a.paintSelectable==b.paintSelectable;}
        check(families,"semantic families, palette, underwater and layer eligibility preserved");
        const auto paletteKeys=[](const STerrainAtlasProfile& p,const QVector<QByteArray>& bytes,const QVector<qint32>& types){QSet<QString> keys;
            for(const auto& s:p.slotDefinitions){const auto a=s.signature;
                if(s.assigned&&s.paintSelectable&&a.northWest&&a.northWest==a.northEast&&a.northWest==a.southEast&&a.northWest==a.southWest){
                    const int size=int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes[s.slot/64].constData()+4)))/8;
                    keys.insert(QString("%1/%2/%3/%4").arg(a.northWest).arg(size).arg(types.value(s.slot)).arg(s.referencePaintSelectable));}}
            return keys;};
        check(paletteKeys(profile,pages,m.tileTypes)==paletteKeys(plan.profile,plan.pages,candidate.mapInfo().tileTypes),"every palette seed category still represented exactly");
        bool definitions=true;QMap<int,STerrainAtlasProfileSlot> definitionsBySlot;for(const auto& s:plan.profile.slotDefinitions)definitionsBySlot[s.slot]=s;
        for(const auto& s:profile.slotDefinitions)if(plan.remap.contains(s.slot)){
            const auto b=definitionsBySlot.value(plan.remap[s.slot]);
            definitions&=b.signature==s.signature&&b.assigned==s.assigned&&b.ambiguous==s.ambiguous
                &&b.paintSelectable==s.paintSelectable&&b.referencePaintSelectable==s.referencePaintSelectable
                &&b.sourceNamespace==s.sourceNamespace&&b.sourceSlotAliases==s.sourceSlotAliases
                &&b.sourcePixelSha256==s.sourcePixelSha256&&b.routeOwnerMaterial==s.routeOwnerMaterial;}
        check(definitions,"retained slot semantics and recovery/provenance aliases unchanged");
        const auto semanticAfter=candidate.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
        if(!semantic.isEmpty()){STerrainSemanticFieldDocument a,b;
            const bool good=STerrainSemanticFieldDocument::fromBytes(semantic,&a,&report)&&STerrainSemanticFieldDocument::fromBytes(semanticAfter,&b,&report);
            check(good&&a.land==b.land&&a.overlay==b.overlay&&b.validateBinding(plan.profile.mprNamespace,QByteArray::fromHex(hash(plan.mprBytes)),QByteArray::fromHex(plan.profile.atlasPixelSha256),QByteArray::fromHex(hash(plan.profile.toBytes())),int(m.sectorCountX)*16+1,int(m.sectorCountY)*16+1,&report,STerrainSemanticFieldDocument::tileBindingSha256(candidate)),"exact semantic fields and rebound hashes");}
        auto a=patch(source,pages),b=patch(candidate,plan.pages);
        check(a==b&&a.save(QDir(root).filePath(mapName+"-before.png"))&&b.save(QDir(root).filePath(mapName+"-after.png")),"PNG patch identical before/after");
        if(failures)return 1;
        // Publication uses the same production coordinators as the UI action.
        QStringList dirs{QDir(root).filePath("Maps"),QDir(root).filePath("BaseMaps")};
        const auto committed=commitTerrainAtlasCompact(source,profile,plan,path,res,dirs,QDir(root).filePath("backups"),QDir(root).filePath("recovery"));
        if(!check(committed.success,"publish private copy: "+committed.error+" "+committed.cleanup))return 1;
        check(read(path)==plan.mprBytes,"committed MPR readback exact");
        auto binding=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive({res},plan.profile.mprNamespace,res);
        check(binding.success()&&binding.profile.toBytes()==plan.profile.toBytes(),"fresh RES/profile/pixels binding on reopen: "+binding.error);binding.decodedAtlasTiles.clear();
        STerrainAtlasRecoverySnapshot recovered;check(CTerrainAtlasRecoveryStore(QDir(root).filePath("recovery")).findExact(plan.profile.mprNamespace,plan.profile.atlasPixelSha256,&recovered,&error)&&recovered.mprSha256==hash(plan.mprBytes),"new exact recovery snapshot retained");
        QByteArray recoveryArchive;
        check(CTerrainAtlasRecoveryStore(QDir(root).filePath("recovery")).buildStandaloneArchive(recovered,&recoveryArchive,&error),"recovery reconstructs a complete validated atlas: "+error);
        const auto second=prepareTerrainAtlasCompact(candidate,plan.profile,plan.pages,semanticAfter,QFileInfo(path));
        check(second.error.isEmpty()&&second.noOp&&second.mprBytes==plan.mprBytes,"repeat compaction is exact no-op: "+second.error);
        // The no-op commit never writes, backs up or changes archive size/hash.
        const auto beforeStat=QFileInfo(res);const auto noop=commitTerrainAtlasCompact(candidate,plan.profile,second,path,res,dirs);
        check(noop.success&&QFileInfo(res).size()==beforeStat.size()&&QFileInfo(res).lastModified()==beforeStat.lastModified(),"repeat commit does not touch RES");
        QJsonObject receipt;receipt["map"]=mapName;receipt["pagesBefore"]=plan.oldPages;receipt["pagesAfter"]=plan.pages.size();receipt["slotsBefore"]=plan.oldSlots;receipt["slotsAfter"]=plan.pages.size()*64;receipt["retainedSlots"]=plan.retainedSlots;
        receipt["dxtBytesBefore"]=double(plan.oldDxt);receipt["dxtBytesAfter"]=double(plan.newDxt);receipt["rgbaBaseBefore"]=double(plan.oldRgba);receipt["rgbaBaseAfter"]=double(plan.newRgba);
        receipt["archiveBytesBefore"]=double(committed.archiveBefore);receipt["archiveBytesAfter"]=double(committed.archiveAfter);receipt["cleanup"]=committed.cleanup;receipt["namespace"]=plan.profile.mprNamespace;receipt["mprSha256"]=QString::fromLatin1(hash(plan.mprBytes));receipt["activeCells"]=double(cellCount);receipts<<receipt;
        write(QDir(root).filePath(mapName+"-result.json"),QJsonDocument(receipt).toJson());
    }
    write(QDir(root).filePath("results.json"),QJsonDocument(receipts).toJson());
    QTextStream(stdout)<<"RESULT checks="<<checks<<" failures="<<failures<<" nativeAttempts=0\n";return failures?1:0;
}

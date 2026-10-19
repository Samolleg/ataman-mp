#include "terrain_atlas_compact.h"
#include "terrain_semantic_field_store.h"
#include "terrain_virtual_atlas.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include "res_save_coordinator.h"
#include "staged_save_validator.h"
#include "terrain_atlas_recovery_store.h"
#include "native_sha256.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace {
QByteArray hash(const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
int u32(const QByteArray& b,int o){return int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+o)));}
void put(QByteArray& b,int o,int n){qToLittleEndian<quint32>(quint32(n),reinterpret_cast<uchar*>(b.data()+o));}
QString pageName(const QString& ns,int p){return QString("%1%2.mmp").arg(ns).arg(p,3,10,QLatin1Char('0'));}
bool inventory(const QStringList& dirs,const QString& current,QMap<QString,QByteArray>* out){
    out->clear();if(dirs.isEmpty())return false;
    for(const auto& d:dirs){QDir dir(d);if(!dir.exists()||!QFileInfo(d).isReadable())return false;
        const auto files=dir.entryInfoList(QDir::Files|QDir::Hidden|QDir::System);
        for(const auto& f:files){if(f.suffix().compare("mpr",Qt::CaseInsensitive))continue;
            if(f.isSymLink())return false;
            if(f.absoluteFilePath().compare(QFileInfo(current).absoluteFilePath(),Qt::CaseInsensitive)==0)continue;
            const auto b=read(f.absoluteFilePath());CMprDocument doc;CValidationReport r;
            if(b.isEmpty()||!doc.loadBytes(b,f.fileName(),&r)||r.hasErrors())return false;
            out->insert(f.absoluteFilePath(),hash(b));}}
    return true;
}
}

STerrainAtlasCompactPlan prepareTerrainAtlasCompact(const CMprDocument& source,
    const STerrainAtlasProfile& input,const QVector<QByteArray>& pages,
    const QByteArray& semanticBytes,const QFileInfo& mpr,const std::atomic_bool* cancelled)
{
    STerrainAtlasCompactPlan p;CValidationReport report;const auto m=source.mapInfo();
    const auto fail=[&](const QString& s){p.error=s;return p;};
    if(!m.valid||pages.size()!=int(m.textureCount)||input.mprNamespace!=m.innerNamespace
       ||input.atlasSlotCount!=pages.size()*64||input.slotDefinitions.isEmpty())return fail("Missing exact published atlas/profile.");
    p.oldPages=pages.size();p.oldSlots=m.tileCount;
    QVector<int> sizes;QVector<int> widths;
    for(const auto& b:pages){if(b.size()<76||u32(b,0)!=0x00504d4d||b.mid(16,4)!="DXT1")return fail("Only complete DXT1 terrain pages can be compacted.");
        int w=u32(b,4);if((w!=1024&&w!=2048)||u32(b,8)!=w)return fail("Unsupported terrain page dimensions.");
        int length=76;for(int n=w;n;n/=2)length+=qMax(1,(n+3)/4)*qMax(1,(n+3)/4)*8;
        if(b.size()!=length)return fail("Truncated DXT1 mip chain.");widths<<w;p.oldDxt+=b.size();p.oldRgba+=qint64(w)*w*4;}
    for(int i=0;i<int(m.tileCount);++i)sizes<<widths[i/64]/8;
    // Validate the complete source binding without retaining gigabytes of
    // discarded images. Use the exact persisted ordered pixel-hash stream.
    CNativeSha256 native;QCryptographicHash portable(QCryptographicHash::Sha256);
    const bool systemHash=native.isAvailable();
    const auto add=[&](const QByteArray& bytes){if(systemHash)native.addData(bytes.constData(),bytes.size());else portable.addData(bytes);};
    add(QByteArray::number(pages.size()*64)+"\n");
    for(int page=0;page<pages.size();++page){
        if(cancelled&&cancelled->load())return fail("Cancelled");
        for(int slot=0;slot<64;++slot){QImage decoded;QString error;
            if(!ei::decodeMmpDxt1AtlasTile(pages[page],slot,&decoded,&error))return fail(error);
            const auto image=decoded.convertToFormat(QImage::Format_RGBA8888);
            add(QByteArray::number(image.width())+"x"+QByteArray::number(image.height())+"\n");
            for(int y=0;y<image.height();++y){const char* row=reinterpret_cast<const char*>(image.constScanLine(y));
                if(systemHash)native.addData(row,image.width()*4);else portable.addData(row,image.width()*4);}
        }
    }
    if((systemHash?native.result():portable.result()).toHex()!=input.atlasPixelSha256)
        return fail("Published profile does not match the complete source atlas pixels.");
    QSet<int> keep;bool invalid=false;
    const auto pin=[&](int slot){if(slot<0||slot>=int(m.tileCount))invalid=true;else keep.insert(slot);};
    for(const auto& sector:source.sectors()){
        const auto b=source.members()[sector.memberIndex].payload;
        for(int layer=0;layer<2;++layer){if(layer&&!sector.hasOverlay)continue;
            const int offset=int(layer?sector.overlayTileOffset:sector.landTileOffset);
            for(int i=0;i<256;++i){if(layer&&sector.overlayMaterialIndices[i]<0)continue;
                const auto packed=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+offset+i*2));
                pin(SMprPackedTile::decode(packed).atlasSlot());}}}
    // Palette buttons address material families, not every historical physical
    // brush output. Retain explicit thumbnails and one exact selectable uniform
    // seed per family/native size/runtime class/reference eligibility. Prefer
    // already used cells, then the lowest source slot. Global source identities
    // and provenance aliases are metadata, never local physical references.
    for(const auto& f:input.families)if(f.thumbnailSlot>=0)pin(f.thumbnailSlot);
    QMap<QString,int> seeds;
    for(const auto& s:input.slotDefinitions){const auto a=s.signature;
        if(s.assigned&&s.paintSelectable&&a.northWest&&a.northWest==a.northEast
           &&a.northWest==a.southEast&&a.northWest==a.southWest){
            if(s.slot<0||s.slot>=sizes.size()){invalid=true;continue;}
            const QString key=QString("%1/%2/%3/%4").arg(a.northWest).arg(sizes[s.slot])
                .arg(m.tileTypes.value(s.slot)).arg(s.referencePaintSelectable);
            const int previous=seeds.value(key,-1);
            if(previous<0||(keep.contains(s.slot)&&!keep.contains(previous))
               ||(keep.contains(s.slot)==keep.contains(previous)&&s.slot<previous))seeds[key]=s.slot;
        }}
    for(int slot:seeds)pin(slot);
    // Preserve even currently unused MP flipbooks: no record or phase is lost.
    QVector<QPair<int,int>> ranges;
    for(const auto& a:m.animatedTiles){int start=a.tileIndex,end=start+qMax(1,int(a.frameCount));
        for(int i=start;i<end;++i)pin(i);ranges<<qMakePair(start,end);}
    if(invalid)return fail("A cell, palette entry or animation references a missing logical slot.");
    std::sort(ranges.begin(),ranges.end());QVector<QPair<int,int>> merged;
    for(const auto& r:ranges){if(!merged.isEmpty()&&r.first<merged.last().second)merged.last().second=qMax(merged.last().second,r.second);else merged<<r;}
    QMap<int,QVector<QVector<int>>> groups;QSet<int> animationSlots;
    for(const auto& r:merged){QVector<int> block;for(int i=r.first;i<r.second;++i){
            if(sizes[i]!=sizes[r.first])return fail("An animation spans different native tile sizes.");block<<i;animationSlots.insert(i);}
        if(block.size()>64)return fail("An animation exceeds one 64-slot texture page.");groups[sizes[r.first]]<<block;}
    auto sorted=keep.values();std::sort(sorted.begin(),sorted.end());
    for(int slot:sorted)if(!animationSlots.contains(slot))groups[sizes[slot]]<<QVector<int>{slot};
    QVector<QVector<int>> layout;
    for(auto it=groups.cbegin();it!=groups.cend();++it){QVector<int> page;
        for(const auto& block:it.value()){
            if(page.size()+block.size()>64){page.resize(64);layout<<page;page.clear();}
            for(int slot:block)page<<slot+1; // zero denotes padding
        }if(!page.isEmpty()){page.resize(64);layout<<page;}}
    if(layout.size()>256||layout.isEmpty())return fail("Packed atlas capacity exceeded.");
    for(int page=0;page<layout.size();++page)for(int i=0;i<64;++i)if(layout[page][i])p.remap[layout[page][i]-1]=page*64+i;
    p.retainedSlots=keep.size();bool identity=layout.size()==pages.size();
    for(auto it=p.remap.cbegin();it!=p.remap.cend();++it)identity&=it.key()==it.value();
    const int maximumTileSize=groups.lastKey();
    identity&=int(m.textureSize)==maximumTileSize*8&&int(m.tileSize)==maximumTileSize;
    // Sparse tails already form a normal page; avoid namespace/padding churn.
    if(identity){p.noOp=true;p.mprBytes=source.originalBytes();p.profile=input;p.pages=pages;p.newDxt=p.oldDxt;p.newRgba=p.oldRgba;return p;}
    QCryptographicHash content(QCryptographicHash::Sha256);content.addData("atlas-compact-208\n");
    content.addData(input.toBytes());content.addData(hash(semanticBytes));
    content.addData(hash(source.memberPayload(m.memberName)));
    QVector<qint32> types;
    for(int page=0;page<layout.size();++page){if(cancelled&&cancelled->load())return fail("Cancelled");
        const auto pageSlots=layout[page];int first=0;while(first<64&&!pageSlots[first])++first;
        int sourcePage=(pageSlots[first]-1)/64,w=widths[sourcePage];
        QByteArray b=pages[sourcePage].left(76);int length=76;
        for(int n=w;n;n/=2)length+=qMax(1,(n+3)/4)*qMax(1,(n+3)/4)*8;
        b.resize(length);std::memset(b.data()+76,0,length-76);
        // All independent tile mip blocks move verbatim, including native SD.
        int offset=76;
        for(int n=w;n>=32;n/=2){int blocks=n/32,stride=n/4;
            for(int dst=0;dst<64;++dst)if(pageSlots[dst]){int old=pageSlots[dst]-1,local=old%64;
                for(int y=0;y<blocks;++y){int a=offset+(((7-local/8)*blocks+y)*stride+(local%8)*blocks)*8;
                    int z=offset+(((7-dst/8)*blocks+y)*stride+(dst%8)*blocks)*8;
                    std::memcpy(b.data()+z,pages[old/64].constData()+a,blocks*8);}}
            offset+=stride*stride*8;
        }
        QImage tiny;QString error;
        const int tinyOffset=offset-32*32/2;
        if(!ei::decodeDxtImage(b.mid(tinyOffset,512),32,32,ei::DxtFormat::Dxt1,&tiny,&error))return fail(error);
        auto shared=tiny.scaled(16,16,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        for(int y=0;y<shared.height();++y)for(int x=0;x<shared.width();++x){auto color=shared.pixelColor(x,y);color.setAlpha(color.alpha()<128?0:255);shared.setPixelColor(x,y,color);}
        auto tail=ei::encodeMmpDxt1(shared,true,&error);
        if(tail.size()-76!=b.size()-offset)return fail("Invalid shared mip tail: "+error);
        std::memcpy(b.data()+offset,tail.constData()+76,tail.size()-76);
        for(int dst=0;dst<64;++dst){QImage image;
            if(!ei::decodeMmpDxt1AtlasTile(b,dst,&image,&error))return fail(error);
            p.images<<image;types<< (pageSlots[dst]?m.tileTypes.value(pageSlots[dst]-1):0);}
        content.addData(hash(b));p.pages<<b;p.newDxt+=b.size();p.newRgba+=qint64(w)*w*4;
    }
    const QString ns=CTerrainVirtualAtlas::contentResourceIdentity(m.innerNamespace,content.result().toHex());
    auto profile=input;profile.mprNamespace=ns;profile.atlasSlotCount=layout.size()*64;profile.textureCount=layout.size();
    profile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(p.images);profile.slotDefinitions.clear();
    for(auto s:input.slotDefinitions)if(p.remap.contains(s.slot)){s.slot=p.remap[s.slot];profile.slotDefinitions<<s;}
    QSet<int> defined;for(const auto& s:profile.slotDefinitions)defined.insert(s.slot);
    for(int slot=0;slot<profile.atlasSlotCount;++slot)if(!defined.contains(slot)){
        STerrainAtlasProfileSlot padding;padding.slot=slot;padding.assigned=false;
        padding.paintSelectable=false;padding.referencePaintSelectable=false;
        padding.signature=input.slotDefinitions.first().signature;profile.slotDefinitions<<padding;
    }
    std::sort(profile.slotDefinitions.begin(),profile.slotDefinitions.end(),[](const STerrainAtlasProfileSlot& a,const STerrainAtlasProfileSlot& b){return a.slot<b.slot;});
    for(auto& f:profile.families){QVector<int> members;for(int old:f.memberSlots)if(p.remap.contains(old))members<<p.remap[old];
        std::sort(members.begin(),members.end());f.memberSlots=members;if(f.thumbnailSlot>=0)f.thumbnailSlot=p.remap.value(f.thumbnailSlot,-1);}
    if(!profile.normalizeMembership(&report)||report.hasErrors())return fail(report.toText());
    STerrainAtlasProfile reopenedProfile;
    if(!STerrainAtlasProfile::fromBytes(profile.toBytes(),&reopenedProfile,&report)
       ||reopenedProfile.toBytes()!=profile.toBytes())return fail("Compacted profile failed complete fresh reopen: "+report.toText());
    p.profile=profile;
    CMprEditTransaction tx("Compact published terrain atlas");
    const auto original=source.memberPayload(m.memberName);int typeOffset=38+int(m.materialCount)*44;
    QByteArray mp=original.left(typeOffset);for(auto type:types){char b[4];qToLittleEndian<qint32>(type,reinterpret_cast<uchar*>(b));mp.append(b,4);}
    for(auto a:m.animatedTiles){a.tileIndex=p.remap.value(a.tileIndex);char b[4];qToLittleEndian<quint16>(a.tileIndex,reinterpret_cast<uchar*>(b));qToLittleEndian<quint16>(a.frameCount,reinterpret_cast<uchar*>(b+2));mp.append(b,4);}
    mp.append(original.mid(int(m.parsedSize)));put(mp,16,p.pages.size());
    // Match buildWorkingPreviewPackage: MP describes the maximum native tile
    // size, even when the first page happens to contain smaller SD slots.
    put(mp,20,maximumTileSize*8);put(mp,24,types.size());put(mp,28,maximumTileSize);
    for(const auto& member:source.members())if(member.name==m.memberName)tx.replaceMemberPayload(member.name,member.sha256,mp);
    for(const auto& sector:source.sectors()){const auto member=source.members()[sector.memberIndex];auto b=member.payload;
        for(int layer=0;layer<2;++layer){if(layer&&!sector.hasOverlay)continue;int offset=int(layer?sector.overlayTileOffset:sector.landTileOffset);
            for(int i=0;i<256;++i){if(layer&&sector.overlayMaterialIndices[i]<0)continue;
                auto packed=SMprPackedTile::decode(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+offset+i*2)));
                int slot=p.remap[packed.atlasSlot()];packed.atlasIndex=slot/64;packed.tileIndex=slot%64;
                qToLittleEndian<quint16>(packed.encode(),reinterpret_cast<uchar*>(b.data()+offset+i*2));}}
        tx.replaceMemberPayload(member.name,member.sha256,b);}
    if(!source.applyIsolatedNamespaceTransaction(tx,ns,&p.mprBytes,&report))return fail(report.toText());
    CMprDocument reopened;if(!reopened.loadBytes(p.mprBytes,mpr.fileName(),&report))return fail(report.toText());
    if(!semanticBytes.isEmpty()){STerrainSemanticFieldDocument semantic;
        if(!STerrainSemanticFieldDocument::fromBytes(semanticBytes,&semantic,&report)
           ||!semantic.validateBinding(m.innerNamespace,QByteArray::fromHex(hash(source.originalBytes())),QByteArray::fromHex(input.atlasPixelSha256),QByteArray::fromHex(hash(input.toBytes())),int(m.sectorCountX)*16+1,int(m.sectorCountY)*16+1,&report,STerrainSemanticFieldDocument::tileBindingSha256(source)))return fail(report.toText());
        semantic.schemaVersion=2;semantic.mprNamespace=ns;semantic.mprSha256=QByteArray::fromHex(hash(p.mprBytes));
        semantic.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(reopened);
        semantic.atlasPixelSha256=QByteArray::fromHex(profile.atlasPixelSha256);semantic.profileSha256=QByteArray::fromHex(hash(profile.toBytes()));
        const auto bytes=semantic.toBytes(&report);QByteArray embedded;
        if(!STerrainSemanticFieldDocument::embedInMpr(p.mprBytes,bytes,&embedded,&report))return fail(report.toText());p.mprBytes=embedded;
        SResArchiveNewMember member;member.name=STerrainSemanticFieldDocument::memberName(ns,mpr);member.payload=bytes;p.members<<member;
    }
    for(int i=0;i<p.pages.size();++i){SResArchiveNewMember member;member.name=pageName(ns,i);member.payload=p.pages[i];p.members<<member;}
    SResArchiveNewMember member;member.name=CTerrainAtlasProfileStore::embeddedMemberName(ns);member.payload=profile.toBytes();p.members<<member;
    if(report.hasErrors())return fail(report.toText());return p;
}

bool buildTerrainArchiveWithout(const CResArchiveDocument& source,const QStringList& names,QByteArray* output,QString* error){
    QByteArray empty(16,0);put(empty,0,0x019ce23c);put(empty,8,16);CResArchiveDocument target;QStringList errors;
    if(!target.loadBytes(empty,"empty",&errors))return false;QSet<QString> remove;for(const auto& n:names)remove.insert(n.toCaseFolded());
    QVector<SResArchiveNewMember> members;for(const auto& m:source.members())if(!remove.contains(m.name.toCaseFolded())){SResArchiveNewMember n;n.name=m.name;n.payload=m.payload;n.lastWriteTime=m.lastWriteTime;members<<n;}
    if(members.isEmpty()){*output=empty;return true;}
    if(!target.buildWithAppendedMembers(members,output,&errors)){*error=errors.join('\n');return false;}return true;
}
QStringList terrainCompactRetiredMembers(const CResArchiveDocument& archive,const STerrainAtlasProfile& profile,const QString& current,const QStringList& dirs,QMap<QString,QByteArray>* snapshots,QString* reason){
    const auto ns=profile.mprNamespace;
    if(!profile.approved||!QRegularExpression("_[a-f0-9]{12}$").match(ns).hasMatch()
       ||!(profile.provenance.startsWith("portable-working")||profile.provenance.startsWith("portable-finalized"))){*reason="Original or unowned atlas retained.";return {};}
    if(!inventory(dirs,current,snapshots)){*reason="Incomplete readable MPR inventory; old atlas retained.";return {};}
    for(auto it=snapshots->cbegin();it!=snapshots->cend();++it){CMprDocument doc;doc.loadBytes(read(it.key()),it.key());
        if(doc.mapInfo().innerNamespace.compare(ns,Qt::CaseInsensitive)==0){*reason="Namespace shared by another map; retained.";return {};}}
    QStringList names;QRegularExpression pages("^"+QRegularExpression::escape(ns)+"[0-9]{3}\\.mmp$",QRegularExpression::CaseInsensitiveOption);
    const QString semantic=STerrainSemanticFieldDocument::memberName(ns,QFileInfo(current));
    for(const auto& m:archive.members())if(pages.match(m.name).hasMatch()||m.name==CTerrainAtlasProfileStore::embeddedMemberName(ns)||m.name==semantic)names<<m.name;
    *reason="Unreferenced owned namespace can be removed after successful map publication.";return names;
}
bool terrainCompactInventoryUnchanged(const QMap<QString,QByteArray>& previous,const QStringList& dirs,const QString& current){QMap<QString,QByteArray> now;return inventory(dirs,current,&now)&&now==previous;}

bool estimateTerrainCompactPublication(STerrainAtlasCompactPlan* plan,const STerrainAtlasProfile& oldProfile,
    const QString& mapPath,const QString& archivePath,const QStringList& dirs)
{
    CResArchiveDocument archive;QStringList errors;const auto bytes=read(archivePath);
    if(!archive.loadBytes(bytes,archivePath,&errors)){plan->error=errors.join('\n');return false;}
    QMap<QString,QByteArray> snapshots;
    const auto retired=terrainCompactRetiredMembers(archive,oldProfile,mapPath,dirs,&snapshots,&plan->retirementReason);
    // Size projection includes profile/semantic payload, table/name overhead
    // and per-member alignment. Retirement remains conditional at commit.
    qint64 total=bytes.size();
    for(const auto& member:plan->members)if(archive.uniqueMemberIndex(member.name)<0)
        total+=member.payload.size()+member.name.toLocal8Bit().size()+22+15;
    for(const auto& name:retired){int i=archive.uniqueMemberIndex(name);if(i>=0){const auto& member=archive.members()[i];total-=member.payload.size()+member.encodedName.size()+22;}}
    plan->archiveBefore=bytes.size();plan->archiveEstimatedAfter=total;return true;
}

STerrainAtlasCompactCommit commitTerrainAtlasCompact(const CMprDocument& source,const STerrainAtlasProfile& oldProfile,
    const STerrainAtlasCompactPlan& plan,const QString& mapPath,const QString& archivePath,const QStringList& dirs,
    const QString& backupRoot,const QString& recoveryRoot)
{
    STerrainAtlasCompactCommit result;
    if(!plan.error.isEmpty()){result.error=plan.error;return result;}
    if(plan.noOp){result.success=true;result.cleanup="Already compact; no files written.";return result;}
    const auto original=read(archivePath);CResArchiveDocument archive;QStringList errors;
    result.archiveBefore=original.size();
    if(read(mapPath)!=source.originalBytes()||!archive.loadBytes(original,archivePath,&errors)){
        result.error="Map changed or RES cannot be reopened: "+errors.join('\n');return result;}
    // Exact source pages/profile are rechecked at the write boundary.
    if(archive.memberPayload(CTerrainAtlasProfileStore::embeddedMemberName(oldProfile.mprNamespace))!=oldProfile.toBytes()){
        result.error="Published source profile changed.";return result;}
    // Every remapped pixel is validated against the current archive, so even a
    // same-size external MMP replacement cannot be published from stale inputs.
    for(auto it=plan.remap.cbegin();it!=plan.remap.cend();++it){QImage image;QString e;
        if(!ei::decodeMmpDxt1AtlasTile(archive.memberPayload(pageName(oldProfile.mprNamespace,it.key()/64)),it.key()%64,&image,&e)
           ||image!=plan.images[it.value()]){result.error="Source atlas pixels changed: "+e;return result;}}
    QMap<QString,QByteArray> snapshots;QString reason;
    const auto retired=terrainCompactRetiredMembers(archive,oldProfile,mapPath,dirs,&snapshots,&reason);result.cleanup=reason;
    CMprDocument candidate;CValidationReport report;
    if(!candidate.loadBytes(plan.mprBytes,mapPath,&report)||report.hasErrors()){result.error=report.toText();return result;}
    STerrainAtlasRecoverySnapshot snapshot;snapshot.resourceIdentity=plan.profile.mprNamespace;
    snapshot.atlasPixelSha256=plan.profile.atlasPixelSha256;snapshot.profileBytes=plan.profile.toBytes();
    snapshot.mprPath=mapPath;snapshot.resPath=archivePath;snapshot.mprSha256=hash(plan.mprBytes);snapshot.runtimeTerrainTypes=candidate.mapInfo().tileTypes;
    for(const auto& member:plan.members){STerrainAtlasRecoveryPage page;page.name=member.name;page.payload=member.payload;
        if(member.name.endsWith(".mmp"))snapshot.pages<<page;
        else if(member.name!=CTerrainAtlasProfileStore::embeddedMemberName(plan.profile.mprNamespace))snapshot.metadataMembers<<page;}
    const auto recovery=CTerrainAtlasRecoveryStore(recoveryRoot).saveSnapshot(snapshot);
    if(!recovery.success){result.error=recovery.error;return result;}
    SResPublishedMembersSaveRequest resRequest;resRequest.source=QFileInfo(archivePath);resRequest.destination=resRequest.source;
    resRequest.published=plan.members;resRequest.backupRootOverride=backupRoot;
    const auto published=commitStagedResPublishedMembers(archive,resRequest);
    if(!published.success){
        result.error=published.error;
        if(published.phase=="post-commit-validation"){
            const auto current=read(archivePath);QString rollback;
            const bool restored=hash(current)==published.candidateSha256.toLatin1()
                &&atomicallyReplaceVerifiedFile(QFileInfo(archivePath),current,original,&rollback);
            result.error+="; RES rollback: "+(restored?QString("complete"):QString("manual recovery required: ")+published.backupPath);
        }
        return result;
    }
    const auto publishedBytes=read(archivePath);
    SMprBytesSaveCommitRequest mapRequest;mapRequest.source=QFileInfo(mapPath);mapRequest.destination=mapRequest.source;
    mapRequest.expectedSourceBytes=source.originalBytes();mapRequest.candidateBytes=plan.mprBytes;mapRequest.backupRootOverride=backupRoot;
    const auto saved=commitValidatedMprBytes(mapRequest);
    if(!saved.success){
        QString mapError,resError;const auto currentMap=read(mapPath);
        const bool mapRestored=currentMap==source.originalBytes()||(currentMap==plan.mprBytes
            &&atomicallyReplaceVerifiedFile(QFileInfo(mapPath),plan.mprBytes,source.originalBytes(),&mapError));
        // If an external writer changed the map, keep both atlas identities in
        // RES rather than guessing which one that new map needs.
        const bool resRestored=mapRestored&&atomicallyReplaceVerifiedFile(QFileInfo(archivePath),publishedBytes,original,&resError);
        result.error=saved.error+"; MPR rollback: "+(mapRestored?QString("complete"):QString("external change; retained"))
            +"; RES rollback: "+(resRestored?QString("complete"):QString("both namespaces retained; ")+resError);return result;
    }
    result.success=true;result.archiveAfter=publishedBytes.size();
    // Retirement is a separate recoverable operation after the new pair exists.
    if(!retired.isEmpty()){
        if(!terrainCompactInventoryUnchanged(snapshots,dirs,mapPath)||read(mapPath)!=plan.mprBytes){result.cleanup="Inventory changed; previous namespace retained.";return result;}
        CResArchiveDocument current;QByteArray clean;QString error;
        if(!current.loadBytes(publishedBytes,archivePath,&errors)||!buildTerrainArchiveWithout(current,retired,&clean,&error)){
            result.cleanup="Previous namespace retained: "+error;return result;}
        if(!terrainCompactInventoryUnchanged(snapshots,dirs,mapPath)||read(mapPath)!=plan.mprBytes){result.cleanup="Inventory changed; previous namespace retained.";return result;}
        SResArchiveBytesSaveRequest cleanup;cleanup.destination=QFileInfo(archivePath);cleanup.candidateBytes=clean;
        cleanup.expectedSourceBytes=publishedBytes;cleanup.backupRootOverride=backupRoot;
        const auto removed=commitValidatedResArchiveBytes(cleanup);
        if(removed.success){result.cleanup=QString("Removed %1 unreferenced namespace members.").arg(retired.size());result.archiveAfter=clean.size();}
        else result.cleanup="Previous namespace retained: "+removed.error;
    }
    return result;
}

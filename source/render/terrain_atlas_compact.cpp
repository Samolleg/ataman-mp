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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "res_archive_index.h"

namespace {
QByteArray hash(const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
QByteArray read(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
int u32(const QByteArray& b,int o){return int(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+o)));}
void put(QByteArray& b,int o,int n){qToLittleEndian<quint32>(quint32(n),reinterpret_cast<uchar*>(b.data()+o));}
QString pageName(const QString& ns,int p){return QString("%1%2.mmp").arg(ns).arg(p,3,10,QLatin1Char('0'));}
bool inventory(const QStringList& dirs,const QString& current,QMap<QString,QByteArray>* out,QMap<QString,QString>* namespaces=nullptr){
    out->clear();if(dirs.isEmpty())return false;
    for(const auto& d:dirs){QDir dir(d);if(!dir.exists()||!QFileInfo(d).isReadable())return false;
        const auto files=dir.entryInfoList(QDir::Files|QDir::Hidden|QDir::System);
        for(const auto& f:files){if(f.suffix().compare("mpr",Qt::CaseInsensitive))continue;
            if(f.isSymLink())return false;
            if(f.absoluteFilePath().compare(QFileInfo(current).absoluteFilePath(),Qt::CaseInsensitive)==0)continue;
            const auto b=read(f.absoluteFilePath());CMprDocument doc;CValidationReport r;
            if(b.isEmpty()||!doc.loadBytes(b,f.fileName(),&r)||r.hasErrors())return false;
            out->insert(f.absoluteFilePath(),hash(b));
            if(namespaces)namespaces->insert(f.absoluteFilePath(),doc.mapInfo().innerNamespace);}}
    return true;
}
}

bool terrainCompactProfileMatchesPublished(const STerrainAtlasProfile& profile,
    const QByteArray& publishedBytes)
{
    STerrainAtlasProfile published;
    return STerrainAtlasProfile::fromBytes(publishedBytes, &published)
        && published.toBytes() == profile.toBytes();
}

STerrainAtlasCompactPlan prepareTerrainAtlasCompact(const CMprDocument& source,
    const STerrainAtlasProfile& input,const QVector<QByteArray>& pages,
    const QByteArray& semanticBytes,const QFileInfo& mpr,const std::atomic_bool* cancelled,bool forcePublication,
    const QVector<QImage>* decodedSource,const QByteArray* publishedProfileBytes)
{
    STerrainAtlasCompactPlan p;CValidationReport report;const auto m=source.mapInfo();
    const auto fail=[&](const QString& s){p.error=s;return p;};
    if(!m.valid||pages.size()!=int(m.textureCount)||input.mprNamespace!=m.innerNamespace
       ||input.atlasSlotCount!=pages.size()*64||input.slotDefinitions.isEmpty())return fail("Missing exact published atlas/profile.");
    p.sourceProfileBytes=publishedProfileBytes?*publishedProfileBytes:input.toBytes();
    p.sourceProfilePresent=!p.sourceProfileBytes.isEmpty();
    if(publishedProfileBytes&&p.sourceProfilePresent
       &&!terrainCompactProfileMatchesPublished(input,p.sourceProfileBytes))
        return fail("Published source profile differs from the loaded profile.");
    p.oldPages=pages.size();p.oldSlots=m.tileCount;
    QVector<int> sizes;QVector<int> widths;
    for(const auto& b:pages){if(b.size()<76||u32(b,0)!=0x00504d4d||b.mid(16,4)!="DXT1")return fail("Only complete DXT1 terrain pages can be compacted.");
        int w=u32(b,4);if((w!=1024&&w!=2048)||u32(b,8)!=w)return fail("Unsupported terrain page dimensions.");
        int length=76;for(int n=w;n;n/=2)length+=qMax(1,(n+3)/4)*qMax(1,(n+3)/4)*8;
        if(b.size()!=length)return fail("Truncated DXT1 mip chain.");
        p.sourcePageSha256<<hash(b);widths<<w;p.oldDxt+=b.size();p.oldRgba+=qint64(w)*w*4;}
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
    QString limitError;
    if(!terrainAtlasPageLimit(layout.size(),&limitError))return fail(limitError);
    for(int page=0;page<layout.size();++page)for(int i=0;i<64;++i)if(layout[page][i])p.remap[layout[page][i]-1]=page*64+i;
    p.retainedSlots=keep.size();
    qint64 packedRgba=0;
    for(const auto& page:layout)for(int value:page)if(value){
        const qint64 width=qint64(sizes[value-1])*8;packedRgba+=width*width*4;break;}
    const int maximumTileSize=groups.lastKey();
    // Moving animation ranges or normalizing legacy SD size hints is not a
    // saving. Avoid publishing a duplicate atlas unless pages or native pixel
    // storage actually decrease. Forced save publication still proceeds.
    if(!forcePublication&&layout.size()>=pages.size()&&packedRgba>=p.oldRgba){
        p.noOp=true;p.mprBytes=source.originalBytes();p.profile=input;p.pages=pages;
        p.remap.clear();for(int slot:keep)p.remap[slot]=slot;
        p.newDxt=p.oldDxt;p.newRgba=p.oldRgba;return p;}
    QCryptographicHash content(QCryptographicHash::Sha256);content.addData("atlas-compact-209\n");content.addData(mpr.fileName().toCaseFolded().toUtf8());
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
            const int old=pageSlots[dst]-1;
            // Keep exact decoded verification, but share an existing equal
            // image instead of retaining a second complete RGBA atlas.
            if(decodedSource&&old>=0&&old<decodedSource->size()
               &&image==decodedSource->at(old))p.images<<decodedSource->at(old);
            else p.images<<image;
            types<< (pageSlots[dst]?m.tileTypes.value(old):0);}
        content.addData(hash(b));p.pages<<b;p.newDxt+=b.size();p.newRgba+=qint64(w)*w*4;
    }
    const QString ns=CTerrainVirtualAtlas::contentResourceIdentity(mpr.baseName(),content.result().toHex());
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
        if(!STerrainSemanticFieldDocument::fromBytes(semanticBytes,&semantic,&report))return fail(report.toText());
        QByteArray profileHash=QByteArray::fromHex(hash(input.toBytes()));
        // Earlier serialized profiles may hash differently after the ordinary
        // load-time migration. Accept their exact original binding only after
        // proving that their decoded meaning is the current input profile.
        if(p.sourceProfilePresent&&semantic.profileSha256!=profileHash
           &&terrainCompactProfileMatchesPublished(input,p.sourceProfileBytes))
            profileHash=QByteArray::fromHex(hash(p.sourceProfileBytes));
        if(!semantic.validateBinding(m.innerNamespace,QByteArray::fromHex(hash(source.originalBytes())),QByteArray::fromHex(input.atlasPixelSha256),profileHash,int(m.sectorCountX)*16+1,int(m.sectorCountY)*16+1,&report,STerrainSemanticFieldDocument::tileBindingSha256(source)))return fail(report.toText());
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
    if(!target.buildWithAppendedMembers(members,output,&errors)){*error=errors.join('\n');return false;}
    CResArchiveDocument check;if(!check.loadBytes(*output,"cleanup-readback",&errors)){*error=errors.join('\n');return false;}
    for(const auto& m:source.members())if(!remove.contains(m.name.toCaseFolded())){
        int i=check.uniqueMemberIndex(m.name);
        if(i<0||check.members()[i].payload!=m.payload||check.members()[i].lastWriteTime!=m.lastWriteTime){*error="Unrelated RES member changed";return false;}}
    return true;
}
QStringList terrainCompactRetiredMembers(const CResArchiveDocument& archive,const STerrainAtlasProfile& profile,const QString& current,const QStringList& dirs,QMap<QString,QByteArray>* snapshots,QString* reason){
    const auto ns=profile.mprNamespace;
    if(!terrainAtlasHasHash(ns)){*reason="Original or unowned atlas retained.";return {};}
    if(!inventory(dirs,current,snapshots)){*reason="Incomplete readable MPR inventory; old atlas retained.";return {};}
    for(auto it=snapshots->cbegin();it!=snapshots->cend();++it){CMprDocument doc;doc.loadBytes(read(it.key()),it.key());
        if(doc.mapInfo().innerNamespace.compare(ns,Qt::CaseInsensitive)==0){*reason="Namespace shared by another map; retained.";return {};}}
    QStringList names;QRegularExpression pages("^"+QRegularExpression::escape(ns)+"[0-9]{3}\\.mmp$",QRegularExpression::CaseInsensitiveOption);
    const QString semantic=STerrainSemanticFieldDocument::memberName(ns,QFileInfo(current));
    for(const auto& m:archive.members())if(pages.match(m.name).hasMatch()||m.name==CTerrainAtlasProfileStore::embeddedMemberName(ns)||m.name==semantic
        ||(m.name.startsWith("ei_")&&m.name.endsWith(".json")
           &&QJsonDocument::fromJson(m.payload).object().value("mprNamespace").toString().compare(ns,Qt::CaseInsensitive)==0))names<<m.name;
    *reason="Unreferenced owned namespace can be removed after successful map publication.";return names;
}
QStringList terrainCompactRetiredMembers(const CResArchiveIndex& archive,const STerrainAtlasProfile& profile,const QString& current,const QStringList& dirs,QMap<QString,QByteArray>* snapshots,QString* reason){
    const auto ns=profile.mprNamespace;
    if(!terrainAtlasHasHash(ns)){*reason="Original or unowned atlas retained.";return {};}
    if(!inventory(dirs,current,snapshots)){*reason="Incomplete readable MPR inventory; old atlas retained.";return {};}
    for(auto it=snapshots->cbegin();it!=snapshots->cend();++it){CMprDocument doc;doc.loadBytes(read(it.key()),it.key());
        if(doc.mapInfo().innerNamespace.compare(ns,Qt::CaseInsensitive)==0){*reason="Namespace shared by another map; retained.";return {};}}
    QStringList names;QRegularExpression pages("^"+QRegularExpression::escape(ns)+"[0-9]{3}\\.mmp$",QRegularExpression::CaseInsensitiveOption);
    const QString semantic=STerrainSemanticFieldDocument::memberName(ns,QFileInfo(current));
    for(const auto& m:archive.members())if(pages.match(m.name).hasMatch()||m.name==CTerrainAtlasProfileStore::embeddedMemberName(ns)||m.name==semantic
        ||(m.name.startsWith("ei_")&&m.name.endsWith(".json")
           &&QJsonDocument::fromJson(archive.readMember(m.tableIndex)).object().value("mprNamespace").toString().compare(ns,Qt::CaseInsensitive)==0))names<<m.name;
    *reason="Unreferenced owned namespace can be removed after successful map publication.";return names;
}
bool terrainCompactInventoryUnchanged(const QMap<QString,QByteArray>& previous,const QStringList& dirs,const QString& current){QMap<QString,QByteArray> now;return inventory(dirs,current,&now)&&now==previous;}

bool estimateTerrainCompactPublication(STerrainAtlasCompactPlan* plan,const STerrainAtlasProfile& oldProfile,
    const QString& mapPath,const QString& archivePath,const QStringList& dirs)
{
    CResArchiveIndex archive;QString error;
    if(!archive.open(archivePath,&error)){plan->error=error;return false;}
    QMap<QString,QByteArray> snapshots;
    const auto retired=terrainCompactRetiredMembers(archive,oldProfile,mapPath,dirs,&snapshots,&plan->retirementReason);
    qint64 total=archive.sourceSize();
    for(const auto& member:plan->members)if(archive.memberIndex(member.name)<0)
        total+=member.payload.size()+member.name.toLocal8Bit().size()+22+15;
    for(const auto& name:retired){int i=archive.memberIndex(name);if(i>=0){const auto& member=archive.member(i);total-=member.dataSize+member.encodedName.size()+22;}}
    plan->archiveBefore=archive.sourceSize();plan->archiveEstimatedAfter=total;return true;
}

STerrainAtlasCompactCommit commitTerrainAtlasCompact(const CMprDocument& source,const STerrainAtlasProfile& oldProfile,
    const STerrainAtlasCompactPlan& plan,const QString& mapPath,const QString& archivePath,const QStringList& dirs,
    const QString& backupRoot,const QString& recoveryRoot)
{
    STerrainAtlasCompactCommit result;
    if(!plan.error.isEmpty()){result.error=plan.error;return result;}
    if(plan.noOp){result.success=true;result.cleanup="Already compact; no files written.";return result;}
    CResArchiveIndex archive;QString error;
    result.archiveBefore=QFileInfo(archivePath).size();
    if(read(mapPath)!=source.originalBytes()||!archive.open(archivePath,&error)){
        result.error="Map changed or RES cannot be reopened: "+error;return result;}
    // A clean original atlas may have no embedded editor profile. Preserve
    // that observed absence as strictly as an existing member's exact bytes.
    const int sourceProfile=archive.memberIndex(CTerrainAtlasProfileStore::embeddedMemberName(oldProfile.mprNamespace));
    if(sourceProfile==-2||(sourceProfile>=0)!=plan.sourceProfilePresent
       ||(sourceProfile>=0&&archive.readMember(sourceProfile,&error)!=plan.sourceProfileBytes)
       ||!error.isEmpty()){
        result.error="Published source profile changed.";return result;}
    // Check each complete encoded page once. Base-pixel checks alone miss an
    // external mip change or truncation and repeatedly decode the same page.
    if(plan.sourcePageSha256.size()!=int(source.mapInfo().textureCount)){
        result.error="Missing source atlas page snapshot.";return result;}
    for(int page=0;page<plan.sourcePageSha256.size();++page)
        if(archive.memberSha256(pageName(oldProfile.mprNamespace,page),&error)!=plan.sourcePageSha256[page]){
            result.error="Source atlas page changed: "+error;return result;}
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
    const auto published=commitStreamedResMembers(archive,resRequest);
    if(!published.success){result.error=published.error;return result;}
    SMprBytesSaveCommitRequest mapRequest;mapRequest.source=QFileInfo(mapPath);mapRequest.destination=mapRequest.source;
    mapRequest.expectedSourceBytes=source.originalBytes();mapRequest.candidateBytes=plan.mprBytes;mapRequest.backupRootOverride=backupRoot;
    const auto saved=commitValidatedMprBytes(mapRequest);
    if(!saved.success){
        QString mapError,resError;const auto currentMap=read(mapPath);
        const bool mapRestored=currentMap==source.originalBytes()||(currentMap==plan.mprBytes
            &&atomicallyReplaceVerifiedFile(QFileInfo(mapPath),plan.mprBytes,source.originalBytes(),&mapError));
        // If an external writer changed the map, keep both atlas identities in
        // RES rather than guessing which one that new map needs.
        const bool resRestored=mapRestored&&!published.backupPath.isEmpty()
            &&replaceResFromFile(archivePath,published.backupPath,published.candidateSha256.toLatin1(),&resError);
        result.error=saved.error+"; MPR rollback: "+(mapRestored?QString("complete"):QString("external change; retained"))
            +"; RES rollback: "+(resRestored?QString("complete"):QString("both namespaces retained; ")+resError);return result;
    }
    result.success=true;result.archiveAfter=QFileInfo(archivePath).size();
    CTerrainAtlasRecoveryStore(recoveryRoot).markSaved(mapPath,hash(plan.mprBytes));
    if(!retired.isEmpty()){
        CResArchiveIndex current;QString e;
        if(!current.open(archivePath,&e)){result.cleanup=e;return result;}
        SResPublishedMembersSaveRequest cleanup;cleanup.source=cleanup.destination=QFileInfo(archivePath);cleanup.backupRootOverride=backupRoot;
        const auto guard=[&](){return terrainCompactInventoryUnchanged(snapshots,dirs,mapPath)&&read(mapPath)==plan.mprBytes;};
        const auto removed=commitStreamedResMembers(current,cleanup,retired,published.candidateSha256.toLatin1(),guard);
        result.cleanup=removed.success?QString("Removed %1 unreferenced namespace members.").arg(retired.size()):"Previous namespace retained: "+removed.error;
        result.archiveAfter=QFileInfo(archivePath).size();
    }
    return result;
}


bool terrainAtlasHasHash(const QString& ns){return QRegularExpression("_[a-f0-9]{12}$",QRegularExpression::CaseInsensitiveOption).match(ns).hasMatch();}
bool terrainAtlasPageLimit(int pages,QString* error){
    if(pages>0&&pages<=126)return true;
    if(error)*error=QString::fromUtf8("Атлас: %1 страниц; предел игры — 126. Сохранение отменено. Сожмите атлас или сократите число используемых вариантов/материалов.").arg(pages);
    return false;
}
QStringList terrainAtlasMapDirectories(const QString& map,const QStringList& archives){
    QStringList dirs;const QFileInfo m(map);dirs<<m.absolutePath();
    QDir root=m.dir();root.cdUp();QDir mods=root;mods.cdUp();
    if(mods.dirName().compare("MODS",Qt::CaseInsensitive)==0){mods.cdUp();
        const auto base=mods.filePath("Base/Maps");
        dirs<<(QDir(base).exists()?base:mods.filePath("Maps"));}
    for(const auto& a:archives){QDir d=QFileInfo(a).dir();for(int i=0;i<2;++i){if(QDir(d.filePath("Maps")).exists())dirs<<QFileInfo(d.filePath("Maps")).absoluteFilePath();d.cdUp();}}
    dirs.removeDuplicates();return dirs;
}
void terrainAtlasCopySourceIdentities(STerrainAtlasProfile* profile,const CTerrainWorkingAtlas& atlas){
    for(auto& slot:profile->slotDefinitions){STerrainWorkingTileIdentity id;
        if(atlas.tileIdentityForSlot(slot.slot,&id)&&id.sourceNamespace.startsWith("base-hd")&&id.sourceSlot>=0){
            slot.sourceNamespace=id.sourceNamespace;slot.sourceSlotAliases={id.sourceSlot};slot.sourcePixelSha256=id.physicalPixelSha256;}}
}
STerrainPackCleanupPlan prepareTerrainPackCleanup(const QString& path,const QStringList& dirs){
    STerrainPackCleanupPlan p;
    QMap<QString,QString> namespaces;
    if(dirs.size()<2||!inventory(dirs,QString(),&p.inventory,&namespaces)){p.error=QString::fromUtf8("Нужен полный читаемый инвентарь MPR Dejavu и Base. Очистка запрещена.");return p;}
    QMap<QString,QStringList> refs;
    for(auto it=namespaces.cbegin();it!=namespaces.cend();++it)refs[it.value().toCaseFolded()]<<it.key();
    CResArchiveIndex archive;
    if(!archive.open(path,&p.error))return p;
    p.before=archive.sourceSize();
    QMap<QString,STerrainPackCleanupRow> rows;QMap<QString,QStringList> members;
    const QRegularExpression page("^(.+)([0-9]{3})\\.mmp$",QRegularExpression::CaseInsensitiveOption);
    for(const auto& m:archive.members()){
        QString ns;auto match=page.match(m.name);
        if(match.hasMatch())ns=match.captured(1).toCaseFolded();
        else if(m.name.startsWith("ei_")&&m.name.endsWith(".json")){
            ns=QJsonDocument::fromJson(archive.readMember(m.tableIndex)).object().value("mprNamespace").toString().toCaseFolded();
        }
        if(ns.isEmpty())continue;
        auto& row=rows[ns];row.name=ns;row.bytes+=m.dataSize;if(match.hasMatch())++row.pages;
        members[ns]<<m.name;
    }
    for(auto it=rows.begin();it!=rows.end();++it){auto row=it.value();const auto uses=refs.value(it.key());
        row.removable=terrainAtlasHasHash(row.name)&&uses.isEmpty();
        QStringList labels;bool baseOnly=!uses.isEmpty();
        for(const auto& use:uses){const auto dir=QDir::fromNativeSeparators(QFileInfo(use).absolutePath()).toCaseFolded();
            const bool base=dir.endsWith("/basemaps")||dir.endsWith("/base/maps");baseOnly&=base;
            const auto label=base?QString("Base"):(dir==QDir::fromNativeSeparators(QFileInfo(dirs.first()).absoluteFilePath()).toCaseFolded()?QString("Dejavu"):QFileInfo(dir).dir().dirName());
            labels<<label+"/"+QFileInfo(use).fileName();}
        row.usage=row.removable?QString::fromUtf8("Удалить: ссылок нет"):
            (uses.isEmpty()?QString::fromUtf8("Исходник, MPR-ссылок нет; сохранить"):
             (baseOnly?QString::fromUtf8("Используются только картами Base: "):QString())+labels.join(", "));
        p.rows<<row;if(row.removable)p.members+=members[it.key()];
    }
    p.after=p.before;
    if(p.members.isEmpty())return p; // inventory-only no-op: no hashing/rebuild/RES validation
    p.archiveSha256=resFileSha256(path,&p.error);if(p.archiveSha256.isEmpty())return p;
    QSet<QString> remove;for(const auto& n:p.members)remove.insert(n.toCaseFolded());
    qint64 size=16,names=0,count=0;
    for(const auto& m:archive.members())if(!remove.contains(m.name.toCaseFolded())){
        size+=(16-size%16)%16+m.dataSize;names+=m.encodedName.size();++count;}
    p.after=size+(16-size%16)%16+count*22+names;return p;
}
bool commitTerrainPackCleanup(const QString& path,const QStringList& dirs,const STerrainPackCleanupPlan& p,QString* error,const QString& backup){
    if(!p.error.isEmpty()){*error=p.error;return false;}
    if(p.members.isEmpty())return true;
    CResArchiveIndex source;if(!source.open(path,error))return false;
    SResPublishedMembersSaveRequest r;r.source=r.destination=QFileInfo(path);r.backupRootOverride=backup;
    const auto saved=commitStreamedResMembers(source,r,p.members,p.archiveSha256,[&](){return terrainCompactInventoryUnchanged(p.inventory,dirs,QString());});
    if(!saved.success)*error=saved.error;return saved.success;
}
QString retireTerrainAtlasAfterSave(const QString& path,const STerrainAtlasProfile& profile,const QString& map,const QStringList& dirs,const QString& backup){
    if(!terrainAtlasHasHash(profile.mprNamespace))return "Original namespace retained.";
    CResArchiveIndex archive;QString error,reason;QMap<QString,QByteArray> snapshots;
    if(dirs.size()<2||!archive.open(path,&error))return "Incomplete inventory or archive; previous namespace retained.";
    const auto names=terrainCompactRetiredMembers(archive,profile,map,dirs,&snapshots,&reason);
    if(names.isEmpty())return reason;
    CMprDocument current;if(!current.loadBytes(read(map),map)||current.mapInfo().innerNamespace.compare(profile.mprNamespace,Qt::CaseInsensitive)==0)return "Current map still uses namespace; retained.";
    const auto mapHash=hash(read(map));
    SResPublishedMembersSaveRequest r;r.source=r.destination=QFileInfo(path);r.backupRootOverride=backup;
    auto result=commitStreamedResMembers(archive,r,names,{},[&](){return terrainCompactInventoryUnchanged(snapshots,dirs,map)&&hash(read(map))==mapHash;});
    return result.success?QString("Removed previous namespace %1 (%2 members)").arg(profile.mprNamespace).arg(names.size()):"Previous namespace retained: "+result.error;
}

bool compactTerrainPublication(QByteArray* bytes,STerrainAtlasProfile* profile,QVector<SResArchiveNewMember>* members,QVector<QImage>* images,const QFileInfo& map,QString* error){
    CMprDocument doc;CValidationReport report;
    if(!doc.loadBytes(*bytes,map.fileName(),&report)||report.hasErrors()){*error=report.toText();return false;}
    QVector<QByteArray> pages;
    for(int i=0;i<int(doc.mapInfo().textureCount);++i){QByteArray page;
        for(const auto& m:*members)if(m.name.compare(pageName(doc.mapInfo().innerNamespace,i),Qt::CaseInsensitive)==0)page=m.payload;
        if(page.isEmpty()){*error="Missing publication page";return false;}pages<<page;}
    auto plan=prepareTerrainAtlasCompact(doc,*profile,pages,doc.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),map,nullptr,true,images);
    if(!plan.error.isEmpty()){*error=plan.error;return false;}
    *bytes=plan.mprBytes;*profile=plan.profile;*members=plan.members;*images=plan.images;return true;
}

namespace {
QByteArray publicationMeaning(const QByteArray& bytes, bool semantic=false){
    auto j=QJsonDocument::fromJson(bytes).object();
    j.remove("mprNamespace");
    if(semantic){j.remove("mprSha256");j.remove("terrainTilesSha256");j.remove("profileSha256");j.remove("version");}
    return QJsonDocument(j).toJson(QJsonDocument::Compact);
}
}
bool reuseTerrainPublication(const CResArchiveIndex& archive,const STerrainAtlasProfile& previous,
    QByteArray* bytes,STerrainAtlasProfile* profile,QVector<SResArchiveNewMember>* members,const QFileInfo& map){
    if(!terrainAtlasHasHash(previous.mprNamespace)||previous.textureCount!=profile->textureCount)return false;
    if(publicationMeaning(previous.toBytes())!=publicationMeaning(profile->toBytes()))return false;
    for(int i=0;i<profile->textureCount;++i){QByteArray candidate;
        for(const auto& m:*members)if(m.name==pageName(profile->mprNamespace,i))candidate=m.payload;
        if(candidate.isEmpty()||archive.readMember(pageName(previous.mprNamespace,i))!=candidate)return false;}
    QByteArray newSemantic;
    for(const auto& m:*members)if(m.name.endsWith(".json")&&m.name!=CTerrainAtlasProfileStore::embeddedMemberName(profile->mprNamespace))newSemantic=m.payload;
    const auto oldSemantic=archive.readMember(STerrainSemanticFieldDocument::memberName(previous.mprNamespace,map));
    if(newSemantic.isEmpty()!=oldSemantic.isEmpty()||(!newSemantic.isEmpty()&&publicationMeaning(newSemantic,true)!=publicationMeaning(oldSemantic,true)))return false;
    CMprDocument d;CValidationReport r;CMprEditTransaction tx("Reuse unchanged terrain publication");QByteArray renamed;
    if(!d.loadBytes(*bytes,map.fileName(),&r)||!d.applyIsolatedNamespaceTransaction(tx,previous.mprNamespace,&renamed,&r))return false;
    if(!oldSemantic.isEmpty()){
        // Version 2 binds tile references. If those changed, rebind the embedded
        // field only; the external publication remains the exact old bytes.
        STerrainSemanticFieldDocument field;CMprDocument candidate;
        if(!STerrainSemanticFieldDocument::fromBytes(oldSemantic,&field,&r)||!candidate.loadBytes(renamed,map.fileName(),&r))return false;
        field.schemaVersion=2;field.mprNamespace=previous.mprNamespace;
        field.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(candidate);
        field.profileSha256=QByteArray::fromHex(hash(previous.toBytes()));
        if(!STerrainSemanticFieldDocument::embedInMpr(renamed,field.toBytes(&r),bytes,&r))return false;
    }else *bytes=renamed;
    *profile=previous;members->clear();return true;
}

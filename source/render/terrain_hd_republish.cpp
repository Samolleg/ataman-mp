#include "terrain_hd_republish.h"
#include "terrain_virtual_atlas.h"
#include "terrain_semantic_field_store.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QRegularExpression>
#include <QtEndian>
#include <QTextStream>
#include <algorithm>

namespace {
quint32 word(const QByteArray& b,int p)
{ return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+p)); }
void put(QByteArray& b,int p,quint32 v)
{ qToLittleEndian<quint32>(v,reinterpret_cast<uchar*>(b.data()+p)); }
QByteArray pixelsHash(const QImage& image)
{
    const QImage rgba=image.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash h(QCryptographicHash::Sha256);
    qint32 w=rgba.width(),t=rgba.height();
    h.addData(reinterpret_cast<const char*>(&w),4);h.addData(reinterpret_cast<const char*>(&t),4);
    for(int y=0;y<t;++y) h.addData(reinterpret_cast<const char*>(rgba.constScanLine(y)),w*4);
    return h.result().toHex();
}
QByteArray mipPage(QByteArray b,int tileSize)
{
    if(b.size()<76 || word(b,16)!=0x31545844) return {};
    int w=int(word(b,4)),h=int(word(b,8)),levels=int(word(b,12)),offset=76;
    while(w>tileSize*8 && levels>1) {
        offset+=qMax(1,(w+3)/4)*qMax(1,(h+3)/4)*8;
        w=qMax(1,w/2);h=qMax(1,h/2);--levels;
    }
    if(w!=tileSize*8 || h!=w || offset>b.size()) return {};
    QByteArray out=b.left(76)+b.mid(offset);put(out,4,w);put(out,8,h);put(out,12,levels);return out;
}
QImage roundTrip(const QImage& image)
{
    const QByteArray mmp=ei::encodeMmpDxt1(image,false,nullptr);QImage out;
    if(mmp.size()>76) ei::decodeDxtImage(mmp.mid(76),image.width(),image.height(),ei::DxtFormat::Dxt1,&out);
    return out;
}
QString identity(const CTerrainHdSources::Source& s)
{ return s.page.left(s.page.size()-7)+":"+QString::number((s.page.mid(s.page.size()-7,3).toInt()*64)+s.slot); }
bool read(const QString& path,QByteArray* b)
{ QFile f(path);if(!f.open(QIODevice::ReadOnly))return false;*b=f.readAll();return f.error()==QFile::NoError; }
bool write(const QString& path,const QByteArray& b)
{ QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit(); }
bool transplantTile(QByteArray* destination,int to,const QByteArray& source,int from)
{
    if(!destination||destination->size()<76||source.size()<76
       ||word(*destination,4)!=word(source,4)||word(*destination,8)!=word(source,8)
       ||word(*destination,16)!=0x31545844||word(source,16)!=0x31545844)return false;
    int w=word(source,4),h=word(source,8),dstOffset=76,srcOffset=76;
    const int levels=qMin(int(word(source,12)),int(word(*destination,12)));
    for(int level=0;level<levels;++level) {
        const int blocks=qMax(1,(w+3)/4),rows=qMax(1,(h+3)/4),bytes=blocks*rows*8;
        if(srcOffset+bytes>source.size()||dstOffset+bytes>destination->size())return false;
        const int tile=w/8;
        // Once slots share one BC1 block, no source block belongs exclusively
        // to this tile. Those final mips come from the coherent rebuilt page.
        if(tile>=4) {
            const int span=tile/4;
            const int fromX=(from%8)*span,fromY=(7-from/8)*span;
            const int toX=(to%8)*span,toY=(7-to/8)*span;
            for(int row=0;row<span;++row)
                std::copy(source.constData()+srcOffset+((fromY+row)*blocks+fromX)*8,
                          source.constData()+srcOffset+((fromY+row)*blocks+fromX+span)*8,
                          destination->data()+dstOffset+((toY+row)*blocks+toX)*8);
        }
        srcOffset+=bytes;dstOffset+=bytes;w=qMax(1,w/2);h=qMax(1,h/2);
    }
    return true;
}
}

bool CTerrainHdSources::open(const QStringList& paths,QString* error)
{
    m_archives.clear();m_sources.clear();m_byPixels.clear();m_byIdentity.clear();m_hdHashes.clear();m_payloadCache.clear();
    QSet<QString> seenPages;
    const QRegularExpression stock(QStringLiteral("^(.+?)([0-9]{3})\\.mmp$"));
    const QRegularExpression published(QStringLiteral("_[0-9a-f]{12}$"));
    for(const QString& path:paths) {
        QSharedPointer<CResArchiveIndex> archive(new CResArchiveIndex);
        if(!archive->open(path,error))return false;
        const int a=m_archives.size();m_archives.append(archive);
        QVector<QString> names;
        for(const auto& member:archive->members()) {
            const auto match=stock.match(member.name.toLower());
            if(match.hasMatch()&&!published.match(match.captured(1)).hasMatch()) names.append(member.name.toLower());
        }
        std::sort(names.begin(),names.end());
        for(const QString& name:names) {
            if(seenPages.contains(name))continue;
            const QByteArray b=archive->readMember(name,error);
            if(b.size()<76 || word(b,16)!=0x31545844 || word(b,4)!=word(b,8))continue;
            const int size=word(b,4)/8;
            if(size!=256)continue; // an SD primary must not hide an HD fallback
            const QByteArray low=mipPage(b,128);
            if(low.isEmpty()) {if(error)*error="Invalid stock mip chain: "+name;return false;}
            seenPages.insert(name);
            for(int slot=0;slot<64;++slot) {
                Source s;s.archive=a;s.page=name;s.slot=slot;s.size=size;
                QImage image;
                if(!ei::decodeMmpDxt1AtlasTile(low,slot,&image,error))return false;
                const int id=m_sources.size();m_sources.append(s);m_byIdentity.insert(identity(s),id);
                // Retained stock has either original mip pixels or one/more
                // deterministic publication compression generations. Each
                // candidate is exact; collisions are resolved against HD pixels.
                QSet<QByteArray> hashes;
                for(int generation=0;generation<=3;++generation) {
                    const QByteArray hash=pixelsHash(image);
                    if(hashes.contains(hash))break;
                    hashes.insert(hash);m_byPixels[hash].append(id);
                    if(generation<3)image=roundTrip(image);
                }
            }
        }
    }
    return current(error);
}
bool CTerrainHdSources::current(QString* error) const
{ for(const auto& a:m_archives)if(!a->sourceIsCurrent(error))return false;return true; }
bool CTerrainHdSources::find(const QString& ns,int slot,Source* source) const
{
    const int id=m_byIdentity.value(ns.toLower()+":"+QString::number(slot),-1);
    if(id<0)return false;
    *source=m_sources[id];return true;
}
QByteArray CTerrainHdSources::payload(const Source& s,QString* error) const
{
    if(s.archive<0||s.archive>=m_archives.size())return {};
    const QString key=QString::number(s.archive)+":"+s.page;
    if(!m_payloadCache.contains(key)) {
        if(m_payloadCache.size()>=16)m_payloadCache.clear();
        m_payloadCache.insert(key,m_archives[s.archive]->readMember(s.page,error));
    }
    return m_payloadCache.value(key);
}
QImage CTerrainHdSources::image(const Source& s,int size,QString* error) const
{
    QByteArray b=payload(s,error);if(size==128)b=mipPage(b,128);QImage out;
    if(!ei::decodeMmpDxt1AtlasTile(b,s.slot,&out,error))return {};
    return out;
}
bool CTerrainHdSources::match(const QImage& published,const STerrainAtlasProfileSlot& p,Source* source,QString* reason) const
{
    const QByteArray hash=pixelsHash(published);
    QVector<int> candidates=m_byPixels.value(hash);
    // A source hash records pre-publication pixels. It may prove provenance
    // despite additional BC1 generations. Never trust namespace/slot alone.
    if(!p.sourceNamespace.isEmpty())for(int alias:p.sourceSlotAliases) {
        const int id=m_byIdentity.value(p.sourceNamespace.toLower()+":"+QString::number(alias),-1);
        if(id<0)continue;
        const auto& s=m_sources[id];
        if(candidates.contains(id)||pixelsHash(image(s,128))==p.sourcePixelSha256) {
            *source=s;return true;
        }
    }
    if(candidates.isEmpty()) {if(reason)*reason="no-exact-stock-match";return false;}
    QByteArray hd;
    for(int id:candidates) {
        if(!m_hdHashes.contains(id))m_hdHashes.insert(id,pixelsHash(image(m_sources[id],256)));
        const QByteArray next=m_hdHashes.value(id);
        if(!hd.isEmpty()&&hd!=next) {if(reason)*reason="ambiguous-HD-collision";return false;}
        hd=next;
    }
    *source=m_sources[candidates.first()];return true;
}

STerrainHdRepublishResult rebuildTerrainAtlasHd(const STerrainAtlasProfile& profile,
    const QVector<QByteArray>& authored,const CTerrainHdSources& sources)
{
    STerrainHdRepublishResult r;r.profile=profile;
    if(authored.size()*64!=profile.atlasSlotCount) {r.error="Physical page/profile count differs";return r;}
    for(const auto& page:authored)for(int slot=0;slot<64;++slot) {
        QImage image;if(!ei::decodeMmpDxt1AtlasTile(page,slot,&image,&r.error))return r;r.before.append(image);
    }
    if(!profile.validateBinding(profile.mprNamespace,CTerrainAtlasProfileInference::pixelSha256(r.before),r.before.size())) {
        r.error="Source profile does not bind exactly to native pixels";return r;
    }
    QJsonArray slotRows;int hd=0,generated=0;
    QMap<int,STerrainAtlasProfileSlot> defs;
    for(const auto& d:profile.slotDefinitions)defs.insert(d.slot,d);
    QVector<CTerrainHdSources::Source> matchedSources(r.before.size());
    QVector<QImage> upgraded(r.before.size());
    for(int slot=0;slot<r.before.size();++slot) {
        CTerrainHdSources::Source s;QString reason;
        bool matched=sources.match(r.before[slot],defs.value(slot),&s,&reason);
        QJsonObject row;row["slot"]=slot;row["assigned"]=defs.value(slot).assigned;
        row["stockMatched"]=matched;
        if(matched) {row["source"]=identity(s);matchedSources[slot]=s;upgraded[slot]=sources.image(s,256);++hd;}else row["reason"]=reason;
        slotRows.append(row);
    }
    const auto global=CTerrainAtlasPresetCatalog::globalUniformFamilies();
    QMap<qint32,ETerrainBlendStyle> styles;
    for(const auto& f:profile.families)styles[f.id]=CTerrainVirtualAtlas::blendStyleForMaterialName(f.name);
    // Historical profiles omit variation/source selection. Recover only by
    // reproducing every pixel of the old encoded result with the production
    // compositor. Signature alone is never a recipe witness.
    QVector<QMap<qint32,CTerrainHdSources::Source>> policies;
    QMap<qint32,CTerrainHdSources::Source> firstLocal,selectedLocal,firstGlobal;
    const auto analysis=profile.usableAnalysis();
    for(const auto& d:profile.slotDefinitions) {
        const auto s=d.signature;
        if(!d.assigned||s.northWest!=s.northEast||s.northWest!=s.southWest||s.northWest!=s.southEast)continue;
        if(d.slot<0||d.slot>=matchedSources.size()||matchedSources[d.slot].archive<0)continue;
        if(!firstLocal.contains(s.northWest))firstLocal.insert(s.northWest,matchedSources[d.slot]);
    }
    for(qint32 id:analysis.landCatalog.materialIds()) {
        STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=id;
        bool found=false;const auto packed=analysis.landCatalog.selectForPaint(pure,0,0,0,&found);
        const int slot=SMprPackedTile::decode(packed).atlasSlot();
        if(found&&slot<matchedSources.size()&&matchedSources[slot].archive>=0)selectedLocal[id]=matchedSources[slot];
    }
    const QString preferred=profile.mprNamespace.section('_',0,-2);
    for(const auto& family:global) {
        QVector<STerrainGlobalMaterialSource> ordered=family.sources;
        std::stable_sort(ordered.begin(),ordered.end(),[&](const STerrainGlobalMaterialSource& a,const STerrainGlobalMaterialSource& b) {
            return a.mprNamespace.compare(preferred,Qt::CaseInsensitive)==0&&b.mprNamespace.compare(preferred,Qt::CaseInsensitive)!=0;
        });
        for(const auto& candidate:ordered) {
            CTerrainHdSources::Source source;
            if(sources.find(candidate.mprNamespace,candidate.uniformSlot,&source)){firstGlobal[family.id]=source;break;}
        }
    }
    policies.append(selectedLocal);policies.append(firstLocal);policies.append(firstGlobal);
    for(int slot=0;slot<r.before.size();++slot) {
        if(!upgraded[slot].isNull()||!defs.value(slot).assigned)continue;
        const auto d=defs.value(slot);
        if(!d.mixedEdgeRecipes.isEmpty())continue; // hash-only B2 is not a recoverable brush bank
        const auto signature=CTerrainTransitionCatalog::mirroredGridY(d.signature);
        QSet<qint32> ids{signature.northWest,signature.northEast,signature.southEast,signature.southWest};
        if(ids.size()<2)continue;
        QImage witness;QJsonObject recipe;bool ambiguous=false;
        for(int policy=0;policy<policies.size();++policy) {
            auto selected=policies[policy];QMap<qint32,QImage> low,high;bool complete=true;
            for(qint32 id:ids) {
                if(!selected.contains(id)) {complete=false;break;}
                low[id]=sources.image(selected[id],128);high[id]=sources.image(selected[id],256);
            }
            if(!complete)continue;
            CTerrainTransitionCatalog empty;
            QVector<QImage> lowSize{low.first()},highSize{high.first()};
            const bool opaque=std::all_of(profile.families.cbegin(),profile.families.cend(),[&](const STerrainAtlasProfileFamily& f){return !ids.contains(f.id)||!f.overlayAllowed;});
            CTerrainVirtualAtlas next(empty,highSize,0,high,styles,opaque);
            auto owners=ids.values();std::sort(owners.begin(),owners.end());
            if(d.routeOwnerMaterial>0)owners={d.routeOwnerMaterial};else owners.prepend(0);
            for(int sourceGeneration=0;sourceGeneration<=3;++sourceGeneration) {
            CTerrainVirtualAtlas old(empty,lowSize,0,low,styles,opaque);
            for(qint32 owner:owners)for(int variation=0;variation<CTerrainVirtualAtlas::blendVariationLimit(signature);++variation) {
                QString label;const QImage original=old.reviewGeneratedBlend(signature,variation,&label,nullptr,owner);
                if(original.isNull()||pixelsHash(roundTrip(original))!=pixelsHash(r.before[slot]))continue;
                const QImage detail=next.reviewGeneratedBlend(signature,variation,nullptr,nullptr,owner);
                if(detail.isNull())continue;
                if(!witness.isNull()&&pixelsHash(detail)!=pixelsHash(witness)){ambiguous=true;continue;}
                witness=detail;recipe["algorithm"]="Modern-exact";recipe["mask"]=label;
                recipe["variation"]=variation;recipe["owner"]=owner;recipe["sourcePolicy"]=policy;
                recipe["sourceCompressionGeneration"]=sourceGeneration;
                QJsonObject sourceRows;for(qint32 id:ids)sourceRows[QString::number(id)]=identity(selected[id]);
                recipe["sources"]=sourceRows;
            }
            for(auto it=low.begin();it!=low.end();++it)it.value()=roundTrip(it.value());
            }
        }
        QJsonObject row=slotRows[slot].toObject();
        if(!witness.isNull()&&!ambiguous){upgraded[slot]=witness;row["regenerated"]=true;row["recipe"]=recipe;row.remove("reason");++generated;}
        else row["reason"]=ambiguous?"ambiguous-generated-HD-recipe":"legacy-recipe-not-reproducible";
        slotRows[slot]=row;
    }
    int retained=0,appliedHd=0,appliedGenerated=0,blockedHd=0,blockedGenerated=0;
    QJsonArray pageRows;
    for(int page=0;page<authored.size();++page) {
        bool complete=true;
        for(int local=0;local<64;++local)complete=complete&&!upgraded[page*64+local].isNull();
        QByteArray payload=authored[page];
        if(complete) {
            QImage pixels=CTerrainVirtualAtlas::composePage(upgraded,page*64,256);
            payload=ei::encodeMmpDxt1(pixels,true,&r.error);
            if(payload.isEmpty())return r;
            for(int local=0;local<64;++local) {
                const auto source=matchedSources[page*64+local];
                if(source.archive>=0&&!transplantTile(&payload,local,sources.payload(source),source.slot)) {
                    r.error="Could not retain original HD BC1 blocks";return r;
                }
            }
        }
        r.pages.append(payload);
        for(int local=0;local<64;++local) {
            const int slot=page*64+local;
            QImage pixels;if(!ei::decodeMmpDxt1AtlasTile(payload,local,&pixels,&r.error))return r;
            r.after.append(pixels);
            QJsonObject row=slotRows[slot].toObject();
            if(!complete) {
                ++retained;row["result"]="retained-128";
                if(!upgraded[slot].isNull()) {
                    row["reason"]="page-has-unrecoverable-slots";
                    if(matchedSources[slot].archive>=0)++blockedHd;else ++blockedGenerated;
                }
            } else if(matchedSources[slot].archive>=0){row["result"]="original-HD-BC1";++appliedHd;}
            else {row["result"]="regenerated-256";++appliedGenerated;}
            slotRows[slot]=row;
        }
        QJsonObject row;row["page"]=page;row["upgraded"]=complete;row["bytesBefore"]=authored[page].size();row["bytesAfter"]=payload.size();pageRows.append(row);
    }
    r.profile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(r.after);
    r.receipt["namespace"]=profile.mprNamespace;r.receipt["physicalSlots"]=r.before.size();
    r.receipt["stockMatched"]=hd;r.receipt["regenerated"]=generated;r.receipt["unmatched"]=r.before.size()-hd-generated;r.receipt["slots"]=slotRows;
    r.receipt["hdApplied"]=appliedHd;r.receipt["regeneratedApplied"]=appliedGenerated;r.receipt["retained128"]=retained;
    r.receipt["pageBlockedHd"]=blockedHd;r.receipt["pageBlockedRegenerated"]=blockedGenerated;r.receipt["pages"]=pageRows;
    r.success=sources.current(&r.error);return r;
}

bool prepareTerrainHdPublication(const CMprDocument& map,const QByteArray& oldSemantic,
    STerrainHdRepublishResult* r)
{
    if(!r||!r->success||r->pages.isEmpty())return false;
    QCryptographicHash content(QCryptographicHash::Sha256);
    for(const auto& page:r->pages)content.addData(page);
    const QString oldNamespace=map.mapInfo().innerNamespace;
    const auto oldProfile=r->profile;
    const QString ns=CTerrainVirtualAtlas::contentResourceIdentity(oldNamespace+"_hd",content.result());
    CValidationReport report;
    QByteArray mp=map.memberPayload(map.mapInfo().memberName);
    int size=128;for(const auto& image:r->after)size=qMax(size,image.width());
    put(mp,20,size*8);put(mp,28,size);
    CMprEditTransaction change(QStringLiteral("Republish exact-layout HD terrain"));
    for(const auto& member:map.members())if(member.name==map.mapInfo().memberName)
        change.replaceMemberPayload(member.name,member.sha256,mp);
    if(!map.applyIsolatedNamespaceTransaction(change,ns,&r->mprBytes,&report)) {
        r->error=report.toText();return false;
    }
    CMprDocument reopened;
    if(!reopened.loadBytes(r->mprBytes,"HD-candidate.mpr",&report)) {r->error=report.toText();return false;}
    // The names are isolated, but every SEC payload, rotation, overlay,
    // height, material record, runtime class and flipbook range is unchanged.
    if(reopened.mapInfo().tileTypes!=map.mapInfo().tileTypes
       ||reopened.mapInfo().materials!=map.mapInfo().materials
       ||reopened.mapInfo().animatedTiles!=map.mapInfo().animatedTiles
       ||reopened.sectors().size()!=map.sectors().size()) {r->error="Map layout metadata changed";return false;}
    QMap<QPair<int,int>,QByteArray> oldSectors;
    for(const auto& sector:map.sectors())oldSectors[qMakePair(sector.sectorX,sector.sectorY)]=map.members()[sector.memberIndex].payload;
    for(const auto& sector:reopened.sectors())if(oldSectors.value(qMakePair(sector.sectorX,sector.sectorY))!=reopened.members()[sector.memberIndex].payload) {
        r->error="SEC payload changed";return false;
    }
    r->profile.mprNamespace=ns;
    const QByteArray profileBytes=r->profile.toBytes();
    for(int page=0;page<r->pages.size();++page) {
        SResArchiveNewMember m;m.name=QStringLiteral("%1%2.mmp").arg(ns).arg(page,3,10,QLatin1Char('0'));m.payload=r->pages[page];r->publishedMembers.append(m);
    }
    SResArchiveNewMember p;p.name=CTerrainAtlasProfileStore::embeddedMemberName(ns);p.payload=profileBytes;r->publishedMembers.append(p);
    if(!oldSemantic.isEmpty()) {
        STerrainSemanticFieldDocument field;
        if(!STerrainSemanticFieldDocument::fromBytes(oldSemantic,&field,&report)) {r->error=report.toText();return false;}
        // r.profile already contains the new hash. Obtain the old native hash
        // from unchanged before images; field arrays remain the authority.
        const QByteArray oldAtlas=QByteArray::fromHex(CTerrainAtlasProfileInference::pixelSha256(r->before));
        auto originalProfile=oldProfile;originalProfile.atlasPixelSha256=oldAtlas.toHex();originalProfile.mprNamespace=oldNamespace;
        const QByteArray oldProfileSha=QCryptographicHash::hash(originalProfile.toBytes(),QCryptographicHash::Sha256);
        if(!field.validateBinding(oldNamespace,QCryptographicHash::hash(map.originalBytes(),QCryptographicHash::Sha256),oldAtlas,
             oldProfileSha,int(map.mapInfo().sectorCountX)*16+1,int(map.mapInfo().sectorCountY)*16+1,&report,
             STerrainSemanticFieldDocument::tileBindingSha256(map))) {r->error=report.toText();return false;}
        field.schemaVersion=2;field.mprNamespace=ns;field.mprSha256.clear();
        field.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(reopened);
        field.atlasPixelSha256=QByteArray::fromHex(r->profile.atlasPixelSha256);
        field.profileSha256=QCryptographicHash::hash(profileBytes,QCryptographicHash::Sha256);
        const QByteArray semantic=field.toBytes(&report);
        QByteArray embedded;
        if(semantic.isEmpty()||!STerrainSemanticFieldDocument::embedInMpr(r->mprBytes,semantic,&embedded,&report)) {r->error=report.toText();return false;}
        r->mprBytes=embedded;
        SResArchiveNewMember s;s.name=STerrainSemanticFieldDocument::memberName(ns,QFileInfo("map.mpr"));s.payload=semantic;r->publishedMembers.append(s);
    }
    r->receipt["publishedNamespace"]=ns;r->receipt["SECIdentical"]=true;r->receipt["classesAndAnimationsIdentical"]=true;
    r->receipt["semanticPresent"]=!oldSemantic.isEmpty();r->receipt["mprBytesBefore"]=map.originalBytes().size();r->receipt["mprBytesAfter"]=r->mprBytes.size();
    return true;
}

int runTerrainHdRepublish(const QString& path)
{
    QByteArray cfgBytes;if(!read(path,&cfgBytes))return 2;
    const QJsonObject cfg=QJsonDocument::fromJson(cfgBytes).object();
    QStringList archives;for(const auto& a:cfg["sources"].toArray())archives.append(a.toString());
    CTerrainHdSources sources;QString error;
    if(!sources.open(archives,&error)) {QTextStream(stderr)<<error<<'\n';return 2;}
    CResArchiveIndex published;if(!published.open(cfg["archive"].toString(),&error))return 2;
    QJsonArray results;
    const QString outputRoot=cfg["output"].toString();
    const bool build=cfg["mode"].toString()==QStringLiteral("build");
    if(build) {
        if(outputRoot.isEmpty()||QFileInfo::exists(outputRoot)) {QTextStream(stderr)<<"Build output must be a new directory\n";return 2;}
        const QString absolute=QDir::cleanPath(QFileInfo(outputRoot).absoluteFilePath());
        for(const auto& value:cfg["maps"].toArray())if(absolute.startsWith(QFileInfo(value.toString()).absolutePath()+"/",Qt::CaseInsensitive)||absolute==QFileInfo(value.toString()).absolutePath())return 2;
        for(const auto& a:archives)if(absolute.startsWith(QFileInfo(a).absolutePath()+"/",Qt::CaseInsensitive)||absolute==QFileInfo(a).absolutePath())return 2;
        if(!QDir().mkpath(outputRoot))return 2;
    }
    for(const auto& value:cfg["maps"].toArray()) {
        const QString mapPath=value.toString();QByteArray bytes;CMprDocument map;CValidationReport report;
        if(!read(mapPath,&bytes)||!map.loadBytes(bytes,mapPath,&report))return 2;
        const QString ns=map.mapInfo().innerNamespace;STerrainAtlasProfile p;
        if(!QRegularExpression(QStringLiteral("_[0-9a-f]{12}$")).match(ns).hasMatch()) {
            QTextStream(stderr)<<"Only published namespaces are eligible: "<<ns<<'\n';return 2;
        }
        if(!STerrainAtlasProfile::fromBytes(published.readMember(CTerrainAtlasProfileStore::embeddedMemberName(ns),&error),&p,&report))return 2;
        QVector<QByteArray> pages;
        for(int page=0;page<int(map.mapInfo().textureCount);++page)pages.append(published.readMember(QStringLiteral("%1%2.mmp").arg(ns.toLower()).arg(page,3,10,QLatin1Char('0')),&error));
        auto result=rebuildTerrainAtlasHd(p,pages,sources);
        if(!result.success){QTextStream(stderr)<<result.error<<'\n';return 2;}
        if(build) {
            const QString output=QDir(outputRoot).filePath(QFileInfo(mapPath).completeBaseName());
            if(!QDir().mkpath(output))return 2;
            QByteArray semantic=map.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
            if(semantic.isEmpty())semantic=published.readMember(STerrainSemanticFieldDocument::memberName(ns,QFileInfo(mapPath)));
            if(!prepareTerrainHdPublication(map,semantic,&result)){QTextStream(stderr)<<result.error<<'\n';return 2;}
            QByteArray empty(16,'\0');put(empty,0,0x019ce23c);put(empty,8,16);
            CResArchiveDocument archive;QStringList errors;QByteArray bytes;
            if(!archive.loadBytes(empty)||!archive.buildWithAppendedMembers(result.publishedMembers,&bytes,&errors))return 2;
            if(!write(QDir(output).filePath(QFileInfo(mapPath).fileName()),result.mprBytes)
               ||!write(QDir(output).filePath("HDPack_Terrain.res"),bytes)
               ||!write(QDir(output).filePath("receipt.json"),QJsonDocument(result.receipt).toJson()))return 2;
            result.receipt["output"]=output;result.receipt["resBytes"]=bytes.size();
        }
        result.receipt["map"]=mapPath;results.append(result.receipt);
        QTextStream(stdout)<<ns<<" exact-stock "<<result.receipt["stockMatched"].toInt()<<'/'<<p.atlasSlotCount<<" regenerated "<<result.receipt["regenerated"].toInt()<<" applied "<<result.receipt["hdApplied"].toInt()<<'+'<<result.receipt["regeneratedApplied"].toInt()<<'\n';
        QJsonObject incremental;incremental["complete"]=false;incremental["maps"]=results;
        if(!write(cfg["receipt"].toString(),QJsonDocument(incremental).toJson()))return 2;
    }
    QJsonObject output;output["complete"]=true;output["sources"]=sources.sourceCount();output["maps"]=results;
    return write(cfg["receipt"].toString(),QJsonDocument(output).toJson())?0:2;
}

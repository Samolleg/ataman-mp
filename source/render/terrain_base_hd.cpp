#include "terrain_base_hd.h"
#include "terrain_base_hd_seams.h"
#include "terrain_base_hd_native.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include "res_archive_index.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTransform>
#include <QtEndian>
#include <cmath>
#include <limits>

namespace {
QByteArray hash(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();
}
QByteArray pixelHash(const QImage& image,bool dimensions=true) {
    const QImage rgba=image.convertToFormat(QImage::Format_RGBA8888);
    QCryptographicHash h(QCryptographicHash::Sha256);
    if(dimensions)h.addData(QByteArray::number(rgba.width())+"x"+QByteArray::number(rgba.height())+"\n");
    for(int y=0;y<rgba.height();++y)h.addData(reinterpret_cast<const char*>(rgba.constScanLine(y)),rgba.width()*4);
    return h.result().toHex();
}
QByteArray read(const QString& path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
QImage rotate(const QImage& image,int turn) {
    return turn?image.transformed(QTransform().rotate(90*turn),Qt::FastTransformation):image;
}
double mse(const QImage& first,const QImage& second,bool border=false) {
    if(first.size()!=second.size()||first.isNull())return 1e20;
    const auto a=first.convertToFormat(QImage::Format_RGBA8888),b=second.convertToFormat(QImage::Format_RGBA8888);
    double sum=0;quint64 n=0;
    // Include the visible UV boundary, not just the unused outermost texel.
    const int rim=qMax(1,a.width()*24/256);
    for(int y=0;y<a.height();++y){const auto* p=a.constScanLine(y);const auto* q=b.constScanLine(y);
        for(int x=0;x<a.width();++x){if(border&&x>=rim&&y>=rim&&x<a.width()-rim&&y<a.height()-rim)continue;
            for(int c=0;c<3;++c){const int d=int(p[x*4+c])-q[x*4+c];sum+=d*d;++n;}}}
    return n?sum/n:1e20;
}
quint32 randomAt(quint32 seed,int x,int y,int salt) {
    quint32 h=seed^quint32(x)*0x9e3779b9u^quint32(y)*0x85ebca6bu^quint32(salt)*0xc2b2ae35u;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;return h^(h>>16);
}
struct Source {QString id;QImage hd;QImage nativeSd;QVector<QImage> variants;};
struct Group {QString material;int physical{-1};QVector<Source> sources;};
bool pure(const STerrainTransitionSignature& s){return s.northWest&&s.northWest==s.northEast&&s.northWest==s.southEast&&s.northWest==s.southWest;}
}

SBaseHdPlan prepareBaseTerrainHd(const QString& library,
    const CTerrainWorkingAtlas& original,const CTerrainEditModel& terrain,
    const STerrainAtlasProfile& profile,const QVector<SMprAnimatedTileRecord>& animations,
    quint32 seed,const std::atomic_bool* cancelled,const QVector<QByteArray>& sourcePages)
{
    SBaseHdPlan result;
    const auto fail=[&](const QString& error){result.error=error;result.atlas.clear();return result;};
    const auto stop=[&](){return cancelled&&cancelled->load();};
    if(!original.isValid())return fail(original.error());
    const QDir root(library);
    const auto current=QJsonDocument::fromJson(read(root.filePath("variants/current.json"))).object();
    const QString id=current.value("id").toString();
    if(!QRegularExpression("^[a-f0-9]{64}$").match(id).hasMatch())return fail(QString::fromUtf8("Не найден указатель локального HD-банка."));
    const QDir bank(root.filePath("variants/"+id));
    const auto manifest=QJsonDocument::fromJson(read(bank.filePath("manifest.json"))).object();
    if(manifest.value("id").toString()!=id||manifest.value("algorithm").toInt()!=2)
        return fail(QString::fromUtf8("Неподдерживаемый или повреждённый HD-банк."));
    QMap<QString,CResArchiveIndex> archives;
    const auto sources=QJsonDocument::fromJson(read(root.filePath("sources/manifest.json"))).object();
    QString error;
    for(const auto& item:sources.value("archives").toArray()) {
        const auto a=item.toObject();const QString file=a.value("file").toString();
        if(QFileInfo(file).fileName()!=file)return fail("Invalid library source path");
        CResArchiveIndex index;
        if(!index.open(root.filePath("sources/"+file),&error))return fail(error);
        archives.insert("@library/sources/"+file,index);
    }
    QMap<QString,QVector<QPair<QString,QByteArray>>> inputs;
    for(const auto& value:manifest.value("inputs").toArray()) {
        const auto a=value.toObject();const QString member=a.value("member").toString().toLower();
        if(member.endsWith(".mmp"))inputs[member].append(qMakePair(a.value("path").toString(),a.value("sha256").toString().toLatin1()));
    }
    QMap<QString,QByteArray> donorPages;
    QMap<QString,QImage> donorSd;
    const auto donor=[&](const QString& sourceId)->QImage {
        const auto parts=sourceId.split(':');if(parts.size()!=3)return {};
        bool ok=false;int slot=parts[1].toInt(&ok);if(!ok||slot<0)return {};
        const QString name=QStringLiteral("%1%2.mmp").arg(parts[0]).arg(slot/64,3,10,QLatin1Char('0')).toLower();
        for(const auto& input:inputs.value(name)) {
            if(!archives.contains(input.first))continue;
            const QString key=input.first+"/"+name;
            if(!donorPages.contains(key)) {
                const auto bytes=archives.value(input.first).readMember(name,&error);
                if(bytes.isEmpty()||hash(bytes)!=input.second){error="HD source hash mismatch: "+name;return {};}
                donorPages.insert(key,bytes);
            }
            QImage image;
            if(ei::decodeMmpDxt1AtlasTile(donorPages.value(key),slot%64,&image,&error)
               && image.size()==QSize(256,256)&&pixelHash(image)==parts[2].toLatin1())
            {
                const auto bytes=donorPages.value(key);
                const int width=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()+4));
                if(width==2048 && bytes.size()>76+width*width/2) {
                    QByteArray mip=bytes.left(76)+bytes.mid(76+width*width/2);
                    qToLittleEndian<quint32>(quint32(width/2),reinterpret_cast<uchar*>(mip.data()+4));
                    qToLittleEndian<quint32>(quint32(width/2),reinterpret_cast<uchar*>(mip.data()+8));
                    QImage sd;
                    if(ei::decodeMmpDxt1AtlasTile(mip,slot%64,&sd,&error))donorSd.insert(sourceId,sd);
                }
                return image.convertToFormat(QImage::Format_RGBA8888);
            }
        }
        error="Missing verified native HD anchor: "+sourceId;return {};
    };
    QVector<QByteArray> pages;
    for(const auto& value:manifest.value("pages").toArray()) {
        if(stop())return fail("Cancelled");
        const auto p=value.toObject();const QString file=p.value("file").toString();
        if(QFileInfo(file).fileName()!=file)return fail("Invalid variant page path");
        const auto bytes=read(bank.filePath(file));
        if(bytes.size()!=p.value("bytes").toInt()||hash(bytes)!=p.value("sha256").toString().toLatin1())
            return fail("HD variant page hash mismatch: "+file);
        pages.append(bytes);
    }
    QMap<QString,QVector<QImage>> variants;
    for(const auto& value:manifest.value("entries").toArray()) {
        const auto e=value.toObject();int slot=e.value("slot").toInt(-1);
        if(slot<0||slot/64>=pages.size())return fail("Invalid variant slot");
        QImage image;
        if(!ei::decodeMmpDxt1AtlasTile(pages[slot/64],slot%64,&image,&error)
           ||image.size()!=QSize(256,256)||pixelHash(image,false)!=e.value("pixelSha256").toString().toLatin1())
            return fail("HD variant pixels do not match manifest");
        variants[e.value("key").toString()+"/"+e.value("anchor").toString()].append(image.convertToFormat(QImage::Format_RGBA8888));
    }
    QMap<qint32,STerrainAtlasProfileFamily> families;
    for(const auto& f:profile.families)families.insert(f.id,f);
    QSet<int> protectedSlots;
    for(const auto& a:animations)for(int i=0;i<qMax(1,int(a.frameCount));++i)protectedSlots.insert(a.tileIndex+i);
    // Any slot used by an overlay stays byte-identical even if also used on land.
    for(int y=0;y<terrain.tileCountY();++y)for(int x=0;x<terrain.tileCountX();++x){
        bool found=false;auto packed=terrain.packedTile(x,y,ETerrainTileLayer::Overlay,&found);
        if(found&&packed!=0xffff)protectedSlots.insert(SMprPackedTile::decode(packed).atlasSlot());
    }
    QSet<QString> needed;
    for(const auto& f:profile.families)
        if(f.landAllowed&&!f.underwater&&!f.ambiguous&&!f.name.contains("Water",Qt::CaseInsensitive))needed.insert(f.name);
    QVector<Group> groups;
    for(const auto& value:manifest.value("groups").toArray()) {
        if(stop())return fail("Cancelled");
        const auto g=value.toObject();if(g.value("layer").toString()!="land"||!needed.contains(g.value("material").toString()))continue;
        Group group;group.material=g.value("material").toString();group.physical=g.value("physicalClass").toInt(-1);
        for(const auto& o:g.value("originals").toArray()){
            Source source;source.id=o.toString();source.hd=donor(source.id);
            if(source.hd.isNull())return fail(error);
            source.nativeSd=donorSd.value(source.id);
            source.variants=variants.value(g.value("key").toString()+"/"+source.id);
            for(const auto& image:source.variants)
                if(mse(image,source.hd,true)!=0)return fail("HD variant changed its protected anchor border");
            group.sources.append(source);
        }
        groups.append(group);
    }
    struct Replacement {QVector<int> physicalSlots;QVector<int> rotations;};
    QMap<int,Replacement> replacements;
    const auto images=original.activeTileImages();
    result.atlas=original.createStagingCopy();
    CValidationReport report;
    QSet<int> used;
    for(int y=0;y<terrain.tileCountY();++y)for(int x=0;x<terrain.tileCountX();++x){
        bool ok=false;auto p=terrain.packedTile(x,y,ETerrainTileLayer::Land,&ok);
        if(ok)used.insert(SMprPackedTile::decode(p).atlasSlot());
    }
    for(int slot:used) {
        if(stop())return fail("Cancelled");
        if(protectedSlots.contains(slot)||slot<0||slot>=images.size())continue;
        SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);
        bool known=false;const auto sig=original.catalog().gridSignatureForPacked(packed.encode(),&known);
        if(!known||!pure(sig)||!families.contains(sig.northWest))continue;
        const auto f=families.value(sig.northWest);
        if(!needed.contains(f.name))continue;
        Replacement replacement;
        const int type=original.runtimeTerrainTypeForSlot(slot);
        // Water remains protected even when an imported profile names it as land.
        if(type==6)continue;
        double best=1e20,second=1e20;Source selected;int selectedTurn=0;
        QSet<QByteArray> tried;
        // Material identity comes from the bound atlas profile. Physical terrain
        // properties belong to the destination map, not to a donor texture.
        for(const auto& g:groups)if(g.material==f.name)
            for(const auto& source:g.sources)for(int turn=0;turn<4;++turn){
                const QImage oriented=rotate(source.hd,turn);
                const auto identity=pixelHash(oriented);if(tried.contains(identity))continue;tried.insert(identity);
                const auto reduced=oriented.scaled(images[slot].size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                double score=mse(images[slot],reduced);
                if(images[slot].size()==source.nativeSd.size())
                    score=qMin(score,mse(images[slot],rotate(source.nativeSd,turn)));
                if(score<best){second=best;best=score;selected=source;selectedTurn=turn;}
                else second=qMin(second,score);
            }
        // HD detail deliberately differs from SD pixels. Similarity selects the
        // closest boundary/orientation within the confirmed material; it must
        // not reject that material merely because its HD detail is different.
        if(selected.hd.isNull()){
            result.unmatchedSources.append(QStringLiteral("slot:%1 material:%2 type:%3 best:%4 next:%5")
                .arg(slot).arg(f.name).arg(type).arg(best).arg(second));
            replacements.insert(slot,replacement);continue;
        }
        QVector<QImage> pool={selected.hd};pool+=selected.variants;
        // Include every ready variant sharing this boundary, including variants
        // stored under another original or physical-class group. Different
        // boundaries stay with their nearest original to avoid new seams.
        for(const auto& group:groups)if(group.material==f.name)
            for(const auto& source:group.sources)for(int turn=0;turn<4;++turn){
                const auto candidate=rotate(source.hd,turn);
                if(mse(candidate,selected.hd,true)!=0)continue;
                pool.append(candidate);
                for(const auto& variant:source.variants)pool.append(rotate(variant,turn));
            }
        QByteArray oldBlocks;
        if(slot/64<sourcePages.size())oldBlocks=baseHdNativeBlocks(sourcePages[slot/64],slot%64);
        if(oldBlocks.isEmpty())oldBlocks=original.preservedDxtTiles().value(slot);
        if(oldBlocks.isEmpty())oldBlocks=ei::encodeMmpDxt1(images[slot].scaled(256,256,Qt::IgnoreAspectRatio,Qt::FastTransformation),false,&error).mid(76,32768);
        if(oldBlocks.size()!=32768)return fail("Original compressed terrain page unavailable for HD border preservation");
        QSet<QByteArray> unique,inputsSeen;
        for(const auto& image:pool)for(int turn=0;turn<4;++turn) {
            if(stop())return fail("Cancelled");
            const auto oriented=rotate(image,(selectedTurn+turn)%4);
            const auto inputKey=pixelHash(oriented);if(inputsSeen.contains(inputKey))continue;inputsSeen.insert(inputKey);
            QByteArray blocks;
            const auto hybrid=baseHdWithOriginalRim(oriented,images[slot],oldBlocks,&blocks,&error);
            if(hybrid.isNull())return fail(error);
            const auto key=pixelHash(hybrid);if(unique.contains(key))continue;unique.insert(key);
            const int imported=result.atlas->importPhysicalTile(hybrid,type,&sig,
                "base-hd-rim:"+selected.id+":"+QString::number(slot),-1,&report,true);
            if(imported<0||report.hasErrors())return fail(report.toText());
            result.atlas->preserveDxtTile(imported,blocks);
            if(!replacement.physicalSlots.contains(imported))replacement.physicalSlots.append(imported);
        }
        result.importedVariants+=replacement.physicalSlots.size();++result.changedSources;
        replacements.insert(slot,replacement);
    }
    result.command.description=QString::fromUtf8("Перевести базовые тайлы в HD");
    for(int y=0;y<terrain.tileCountY();++y)for(int x=0;x<terrain.tileCountX();++x){
        bool ok=false;const auto before=terrain.packedTile(x,y,ETerrainTileLayer::Land,&ok);if(!ok)continue;
        auto packed=SMprPackedTile::decode(before);const int source=packed.atlasSlot();
        if(!replacements.contains(source))continue;
        ++result.eligibleCells;const auto& r=replacements[source];
        if(r.physicalSlots.isEmpty()){++result.unmatchedCells;continue;}
        int chosen=r.physicalSlots.at(int(randomAt(seed,x,y,source)%quint32(r.physicalSlots.size())));
        packed.atlasIndex=quint8(chosen/64);packed.tileIndex=quint8(chosen%64);
        if(!r.rotations.isEmpty())packed.rotation=quint8((packed.rotation+r.rotations.at(int(randomAt(seed,y,x,source+1)%quint32(r.rotations.size()))))%4);
        if(packed.encode()!=before){STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.before=before;delta.after=packed.encode();result.command.deltas.append(delta);}
    }
    // Validate both the preview and the actual DXT1 pixels written by Save.
    // DXT blocks are independent and tile/page boundaries are block aligned.
    const auto active=result.atlas->activeTileImages();auto saved=active;
    for(int slot=images.size();slot<saved.size();++slot){
        if(stop())return fail("Cancelled");
        const auto& image=active[slot];
        auto blocks=result.atlas->preservedDxtTiles().value(slot);
        if(blocks.isEmpty())blocks=ei::encodeMmpDxt1(image,false,&error).mid(76);
        if(blocks.isEmpty()||!ei::decodeDxtImage(blocks,image.width(),image.height(),ei::DxtFormat::Dxt1,&saved[slot],&error))return fail(error);
    }
    CBaseHdSeams previewSeams(images,active),savedSeams(images,saved);
    const int width=terrain.tileCountX(),height=terrain.tileCountY();
    QVector<quint16> old(width*height),chosenCells(width*height);QVector<bool> valid(width*height),changed(width*height,false);
    QVector<int> queue;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){bool ok=false;const int cell=y*width+x;
        old[cell]=chosenCells[cell]=terrain.packedTile(x,y,ETerrainTileLayer::Land,&ok);valid[cell]=ok;}
    for(const auto& delta:result.command.deltas){const int cell=delta.globalY*width+delta.globalX;
        chosenCells[cell]=delta.after;changed[cell]=true;queue.append(cell);}
    const int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
    const auto reject=[&](int cell){if(cell>=0&&changed[cell]){changed[cell]=false;chosenCells[cell]=old[cell];++result.seamRejectedCells;
        for(int side=0;side<4;++side){int x=cell%width+dx[side],y=cell/width+dy[side];
            if(x>=0&&y>=0&&x<width&&y<height&&changed[y*width+x])queue.append(y*width+x);}}};
    // Removing an unsafe replacement can expose another edge. Revisit its
    // neighbors until stable; at most one removal per cell is possible.
    for(int index=0;index<queue.size();++index){
        if(stop())return fail("Cancelled");const int cell=queue[index];if(!changed[cell])continue;
        for(int side=0;side<4&&changed[cell];++side){
            const int x=cell%width+dx[side],y=cell/width+dy[side];
            const int neighbor=x>=0&&y>=0&&x<width&&y<height?y*width+x:-1;
            bool safe=false;
            if(neighbor<0||!valid[neighbor])
                safe=previewSeams.boundarySafe(old[cell],chosenCells[cell],side)&&savedSeams.boundarySafe(old[cell],chosenCells[cell],side);
            else safe=previewSeams.safe(old[cell],old[neighbor],chosenCells[cell],chosenCells[neighbor],side)
                &&savedSeams.safe(old[cell],old[neighbor],chosenCells[cell],chosenCells[neighbor],side);
            safe=safe&&previewSeams.boundarySafe(old[cell],chosenCells[cell],side)
                &&savedSeams.boundarySafe(old[cell],chosenCells[cell],side);
            if(!safe){reject(cell);if(neighbor>=0)reject(neighbor);}
        }
    }
    QVector<STerrainTileDelta> accepted;
    for(const auto& delta:result.command.deltas)if(changed[delta.globalY*width+delta.globalX])accepted.append(delta);
    result.command.deltas=accepted;
    result.atlas->setPreserveNativePages(true);
    return result;
}

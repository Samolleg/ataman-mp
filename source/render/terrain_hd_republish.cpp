#include "terrain_hd_republish.h"
#include "terrain_virtual_atlas.h"
#include "terrain_semantic_field_store.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include "terrain_mixed_edge.h"
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
#include <cmath>
#include <QTransform>
#include <QPainter>
#include <functional>

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
QImage rotated(const QImage& image,int rotation)
{ return rotation?image.transformed(QTransform().rotate(90*rotation)):image; }
QVector<float> descriptor(const QImage& source)
{
    const QImage image=source.convertToFormat(QImage::Format_RGBA8888);
    QVector<float> means(48,0);const int span=image.width()/4;
    for(int y=0;y<image.height();++y) {
        const uchar* line=image.constScanLine(y);
        for(int x=0;x<image.width();++x)for(int c=0;c<3;++c)
            means[((y/span)*4+x/span)*3+c]+=line[x*4+c]/float(span*span);
    }
    return means;
}
// RGB MSE, alpha disagreement, block-average MSE and global RGB SSIM.
QJsonObject metrics(const QImage& a,const QImage& b)
{
    if(a.size()!=b.size())return {{"mse",1e9},{"alpha",1.0},{"ssim",0.0}};
    const QImage x=a.convertToFormat(QImage::Format_RGBA8888),y=b.convertToFormat(QImage::Format_RGBA8888);
    double mse=0,alpha=0,sx=0,sy=0,sxx=0,syy=0,sxy=0;
    const double n=x.width()*x.height()*3;
    for(int row=0;row<x.height();++row) {
        const uchar* p=x.constScanLine(row);const uchar* q=y.constScanLine(row);
        for(int col=0;col<x.width();++col) {
            alpha+=(p[col*4+3]!=q[col*4+3]);
            for(int c=0;c<3;++c) {double u=p[col*4+c],v=q[col*4+c];mse+=(u-v)*(u-v);sx+=u;sy+=v;sxx+=u*u;syy+=v*v;sxy+=u*v;}
        }
    }
    const double mx=sx/n,my=sy/n,vx=sxx/n-mx*mx,vy=syy/n-my*my,cov=sxy/n-mx*my;
    const double ssim=((2*mx*my+6.5025)*(2*cov+58.5225))/((mx*mx+my*my+6.5025)*(vx+vy+58.5225));
    const auto dx=descriptor(x),dy=descriptor(y);double block=0;
    for(int i=0;i<dx.size();++i)block+=(dx[i]-dy[i])*(dx[i]-dy[i]);
    return {{"mse",mse/n},{"psnr",mse?10*std::log10(65025*n/mse):99.0},
            {"ssim",ssim},{"blockMse",block/48},{"alpha",alpha/(x.width()*x.height())}};
}
QString identity(const CTerrainHdSources::Source& s)
{ return s.page.left(s.page.size()-7)+":"+QString::number((s.page.mid(s.page.size()-7,3).toInt()*64)+s.slot); }
bool read(const QString& path,QByteArray* b)
{ QFile f(path);if(!f.open(QIODevice::ReadOnly))return false;*b=f.readAll();return f.error()==QFile::NoError; }
bool write(const QString& path,const QByteArray& b)
{ QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit(); }
bool previews(const CMprDocument& map,STerrainHdRepublishResult* result,const QString& directory)
{
    CTerrainEditModel model;CValidationReport report;if(!model.load(map,&report))return false;
    const auto rows=result->receipt["slots"].toArray();int best=-1;QPoint center(model.tileCountX()/2,model.tileCountY()/2);
    for(int y=0;y<model.tileCountY();++y)for(int x=0;x<model.tileCountX();++x) {
        bool available=false;int slot=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land,&available)).atlasSlot();
        if(!available||slot>=rows.size())continue;
        const auto row=rows[slot].toObject();int weight=row["result"].toString()=="regenerated-256"?100:row["result"].toString()=="original-HD-BC1"?1:0;
        if(weight>best){best=weight;center=QPoint(x,y);}
    }
    const int width=qMin(6,model.tileCountX()),height=qMin(4,model.tileCountY());
    const QRect region(qBound(0,center.x()-width/2,model.tileCountX()-width),qBound(0,center.y()-height/2,model.tileCountY()-height),width,height);
    for(int side=0;side<2;++side) {
        const auto& tiles=side?result->after:result->before;
        QImage image(width*256,height*256,QImage::Format_RGB32);image.fill(Qt::magenta);QPainter painter(&image);
        for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x) {
            bool available=false;const auto packed=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land,&available));
            if(available&&packed.atlasSlot()<tiles.size()) {
                const QImage tile=tiles[packed.atlasSlot()].transformed(QTransform().rotate(-90*packed.rotation));
                painter.drawImage(QRect((x-region.x())*256,(region.bottom()-y)*256,256,256),tile);
            }
        }
        painter.end();const QString label=side?"after":"before";
        if(!image.save(directory+"/"+label+".png"))return false;
        const QRect detail((center.x()-region.x())*256,(region.bottom()-center.y())*256,256,256);
        if(!image.copy(detail).scaled(1024,1024,Qt::IgnoreAspectRatio,Qt::FastTransformation).save(directory+"/transition-"+label+".png"))return false;
    }
    result->receipt["preview"]=QJsonObject{{"x",region.x()},{"y",region.y()},{"width",width},{"height",height},
        {"detailX",center.x()},{"detailY",center.y()},{"projection","SEC Ground RGB, north-up, 256 output pixels/cell; no 3D lighting or overlay"}};
    return true;
}
bool transplantTile(QByteArray* destination,int to,const QByteArray& source,int from,int rotation=0)
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
            for(int row=0;row<span;++row)for(int col=0;col<span;++col) {
                int x=col,y=row;
                for(int r=0;r<rotation;++r){const int previous=x;x=y;y=span-1-previous;}
                const char* block=source.constData()+srcOffset+((fromY+y)*blocks+fromX+x)*8;
                char* out=destination->data()+dstOffset+((toY+row)*blocks+toX+col)*8;
                std::copy(block,block+4,out); // original endpoints, no recompression
                quint32 indices=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(block+4)),turned=0;
                for(int py=0;py<4;++py)for(int px=0;px<4;++px) {
                    int bx=px,by=py;for(int r=0;r<rotation;++r){int previous=bx;bx=by;by=3-previous;}
                    turned|=((indices>>(2*(by*4+bx)))&3)<<(2*(py*4+px));
                }
                qToLittleEndian<quint32>(turned,reinterpret_cast<uchar*>(out+4));
            }
        }
        srcOffset+=bytes;dstOffset+=bytes;w=qMax(1,w/2);h=qMax(1,h/2);
    }
    return true;
}
bool carrySdTile(QByteArray* destination,const QByteArray& original,int slot)
{
    if(!destination||word(*destination,4)!=2048||word(original,4)!=1024)return false;
    const int oldBlocks=256,newBlocks=512,span=32;
    const int oldX=(slot%8)*span,oldY=(7-slot/8)*span;
    const int newX=oldX*2,newY=oldY*2;
    // A source BC1 4x4 block becomes four BC1 blocks. Reuse its endpoints and
    // duplicate its two-bit selectors: the decoded raster is exactly nearest
    // neighbour, including alpha, with no compression or colour quantization.
    for(int y=0;y<span;++y)for(int x=0;x<span;++x) {
        const char* source=original.constData()+76+((oldY+y)*oldBlocks+oldX+x)*8;
        const quint32 codes=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(source+4));
        for(int qy=0;qy<2;++qy)for(int qx=0;qx<2;++qx) {
            char* out=destination->data()+76+((newY+y*2+qy)*newBlocks+newX+x*2+qx)*8;
            std::copy(source,source+4,out);quint32 doubled=0;
            for(int py=0;py<4;++py)for(int px=0;px<4;++px) {
                const int ox=(qx*4+px)/2,oy=(qy*4+py)/2;
                doubled|=((codes>>(2*(oy*4+ox)))&3)<<(2*(py*4+px));
            }
            qToLittleEndian<quint32>(doubled,reinterpret_cast<uchar*>(out+4));
        }
    }
    // At 128..4 px per tile, preserve the original exclusively-owned mip
    // blocks byte-for-byte. Final shared blocks belong to the whole new page.
    const int baseBytes=2048*2048/2;
    QByteArray tail=destination->left(76)+destination->mid(76+baseBytes);
    put(tail,4,1024);put(tail,8,1024);put(tail,12,word(*destination,12)-1);
    if(!transplantTile(&tail,slot,original,slot))return false;
    destination->replace(76+baseBytes,tail.size()-76,tail.mid(76));return true;
}
QImage mixedEdgeHd(const STerrainAtlasProfile& profile,int target,
    const QVector<QImage>& before,const QVector<CTerrainHdSources::Source>& matched,
    const CTerrainHdSources& sources,QJsonObject* evidence,QString* reason)
{
    const auto definition=std::find_if(profile.slotDefinitions.cbegin(),profile.slotDefinitions.cend(),[&](const STerrainAtlasProfileSlot& d){return d.slot==target;});
    if(definition==profile.slotDefinitions.cend())return {};
    const auto signature=CTerrainTransitionCatalog::mirroredGridY(definition->signature);
    QSet<qint32> required{signature.northWest,signature.northEast,signature.southEast,signature.southWest};
    QMap<qint32,QVector<int>> uniforms;QMap<QPair<qint32,qint32>,QVector<int>> stock;
    QMap<int,STerrainTransitionSignature> signatures;QMap<qint32,qint32> types;
    for(const auto& f:profile.families)types[f.id]=f.tileType;
    for(const auto& d:profile.slotDefinitions) {
        if(!d.assigned||d.slot<0||d.slot>=before.size()||!d.mixedEdgeRecipes.isEmpty())continue;
        const auto s=CTerrainTransitionCatalog::mirroredGridY(d.signature);signatures[d.slot]=s;
        QSet<qint32> ids{s.northWest,s.northEast,s.southEast,s.southWest};auto sorted=ids.values();std::sort(sorted.begin(),sorted.end());
        if(ids.size()==1)uniforms[sorted.first()].append(d.slot);
        else if(ids.size()==2)stock[qMakePair(sorted[0],sorted[1])].append(d.slot);
    }
    STerrainRecipeBakeInput low;low.tileWidth=low.tileHeight=1;
    low.vertexMaterials={signature.northWest,signature.northEast,signature.southWest,signature.southEast};
    auto ordered=required.values();std::sort(ordered.begin(),ordered.end());
    auto high=low;
    for(qint32 id:ordered) {
        if(uniforms.value(id).isEmpty()) {*reason="mixed-edge-missing-uniform-source";return {};}
        const int slot=uniforms[id].first();QImage hd;
        if(matched[slot].archive>=0)hd=sources.image(matched[slot],256);else if(before[slot].width()==256)hd=before[slot];
        if(hd.isNull()) {*reason="mixed-edge-uniform-source-has-no-HD";return {};}
        STerrainRecipeMaterial m;m.materialId=id;m.runtimeTerrainType=types.value(id,-1);m.uniformTiles={before[slot].scaled(128,128,Qt::IgnoreAspectRatio,Qt::FastTransformation)};low.materials.append(m);
        m.uniformTiles={hd};high.materials.append(m);
    }
    QSet<QPair<qint32,qint32>> pairs;
    for(int i=0;i<ordered.size();++i)for(int j=i+1;j<ordered.size();++j)pairs.insert(qMakePair(ordered[i],ordered[j]));
    QMap<QPair<qint32,qint32>,STerrainRecipeBrush> bank;CValidationReport report;
    if(!CTerrainMixedEdge::buildStockBrushes(before,uniforms,stock,signatures,pairs,&bank,&report)) {*reason="mixed-edge-ownership-bank-not-recoverable";return {};}
    for(const auto& brush:bank){low.brushes.append(brush);high.brushes.append(brush);}
    QImage witness;QByteArray witnessHash;bool ambiguous=false;
    for(int variant=0;variant<4;++variant)for(int type=0;type<16;++type) {
        QByteArray key("crisp-stock-v1|");for(qint32 id:{signature.northWest,signature.northEast,signature.southEast,signature.southWest})key+=QByteArray::number(id)+',';
        key+='|'+QByteArray::number(variant)+'|'+QByteArray::number(type);
        const QString recipe="mixed-edge-b2:"+QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
        if(!definition->mixedEdgeRecipes.contains(recipe))continue;
        low.preferredRuntimeTypes=high.preferredRuntimeTypes={type};
        STerrainRecipeBakeOptions options;options.tileSize=128;options.stableCoordinateVariants=true;options.preserveReviewedCellRuntimeTypes=true;
        STerrainRecipeBakeResult old;
        for(int y=0;y<8&&(!old.success||old.recipes.first().variant!=variant);++y)for(int x=0;x<8;++x) {
            options.origin=QPoint(x,y);old=CTerrainRecipeBaker::bake(low,options);
            if(old.success&&old.recipes.first().variant==variant)break;
        }
        if(!old.success||old.recipes.first().variant!=variant)continue;
        const auto score=metrics(roundTrip(old.tiles[old.tileByCell.first()].pixels),before[target]);
        if(score["mse"].toDouble()>sources.calibration()["acceptedMseMax"].toDouble()||score["ssim"].toDouble()<sources.calibration()["ssimMin"].toDouble()||score["alpha"].toDouble()!=0)continue;
        options.tileSize=256;const auto next=CTerrainRecipeBaker::bake(high,options);if(!next.success)continue;
        const QImage detail=next.tiles[next.tileByCell.first()].pixels;const QByteArray hash=pixelsHash(detail);
        if(!witness.isNull()&&witnessHash!=hash){ambiguous=true;continue;}witness=detail;witnessHash=hash;
        *evidence={{"algorithm","mixed-edge-b2/crisp-stock-v1"},{"recipe",recipe},{"variant",variant},{"runtimeType",type},
            {"originX",options.origin.x()},{"originY",options.origin.y()},{"score",score},{"frozenSdOwnership",true}};
    }
    if(ambiguous){*reason="ambiguous-mixed-edge-HD-recipe";return {};}
    if(witness.isNull())*reason="mixed-edge-legacy-recipe-not-reproducible";
    return witness;
}
}

bool CTerrainHdSources::open(const QStringList& paths,QString* error)
{
    m_archives.clear();m_sources.clear();m_byPixels.clear();m_byIdentity.clear();m_hdHashes.clear();m_payloadCache.clear();m_descriptors.clear();m_matchCache.clear();m_sdOnly.clear();m_origins.clear();
    QVector<double> compressionErrors,structuralScores;
    QSet<QString> seenPages;QSet<QByteArray> descriptorSeen;
    const QRegularExpression stock(QStringLiteral("^(.+?)([0-9]{3})\\.mmp$"));
    const QRegularExpression published(QStringLiteral("_[0-9a-f]{12}$"));
    for(const QString& path:paths) {
        QSharedPointer<CResArchiveIndex> archive(new CResArchiveIndex);
        if(!archive->open(path,error))return false;
        const int a=m_archives.size();m_archives.append(archive);
        QVector<QString> names;
        for(const auto& member:archive->members()) {
            if(member.name.startsWith(QStringLiteral("ei_atlas_profile_"),Qt::CaseInsensitive)&&member.name.endsWith(QStringLiteral(".json"),Qt::CaseInsensitive)) {
                STerrainAtlasProfile p;
                if(STerrainAtlasProfile::fromBytes(archive->readMember(member.name),&p)&&!m_origins.contains(p.mprNamespace.toLower()))m_origins.insert(p.mprNamespace.toLower(),p);
            }
            const auto match=stock.match(member.name.toLower());
            if(match.hasMatch()&&!published.match(match.captured(1)).hasMatch()) names.append(member.name.toLower());
        }
        std::sort(names.begin(),names.end());
        for(const QString& name:names) {
            if(seenPages.contains(name))continue;
            const QByteArray b=archive->readMember(name,error);
            if(b.size()<76 || word(b,16)!=0x31545844 || word(b,4)!=word(b,8))continue;
            const int size=word(b,4)/8;
            if(size==128)for(int slot=0;slot<64;++slot) {
                QImage image;if(!ei::decodeMmpDxt1AtlasTile(b,slot,&image,error))return false;
                Source s;s.page=name;s.slot=slot;
                for(int generation=0;generation<4;++generation) {
                    m_sdOnly[pixelsHash(image)].append(identity(s));image=roundTrip(image);
                }
            }
            if(size!=256)continue; // an SD primary must not hide an HD fallback
            const QByteArray low=mipPage(b,128);
            if(low.isEmpty()) {if(error)*error="Invalid stock mip chain: "+name;return false;}
            seenPages.insert(name);
            for(int slot=0;slot<64;++slot) {
                Source s;s.archive=a;s.page=name;s.slot=slot;s.size=size;
                QImage image;
                if(!ei::decodeMmpDxt1AtlasTile(low,slot,&image,error))return false;
                const int id=m_sources.size();m_sources.append(s);m_byIdentity.insert(identity(s),id);
                QImage high; if(!ei::decodeMmpDxt1AtlasTile(b,slot,&high,error))return false;
                m_hdHashes.insert(id,pixelsHash(high));
                const QImage box=high.scaled(128,128,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                for(int reduction=0;reduction<2;++reduction)for(int rotation=0;rotation<4;++rotation) {
                    const QImage candidate=rotated(reduction?box:image,rotation);
                    const QByteArray key=pixelsHash(candidate)+pixelsHash(rotated(high,rotation));
                    if(descriptorSeen.contains(key))continue;descriptorSeen.insert(key);
                    Descriptor d;d.source=id;d.rotation=rotation;d.reduction=reduction;
                    d.mean=descriptor(candidate);m_descriptors.append(d);
                }
                const QImage native=image;
                if(id%16==0) {
                    QImage recompressed=native;
                    for(int generation=0;generation<3;++generation) {
                        recompressed=roundTrip(recompressed);const auto score=metrics(native,recompressed);
                        compressionErrors.append(score["mse"].toDouble());structuralScores.append(score["ssim"].toDouble());
                    }
                    const auto score=metrics(native,roundTrip(box));
                    compressionErrors.append(score["mse"].toDouble());structuralScores.append(score["ssim"].toDouble());
                }
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
    std::sort(compressionErrors.begin(),compressionErrors.end());
    std::sort(structuralScores.begin(),structuralScores.end());
    const double p99=compressionErrors.isEmpty()?0:compressionErrors[qMin(compressionErrors.size()-1,int(compressionErrors.size()*.99))];
    const double s01=structuralScores.isEmpty()?1:structuralScores[int(structuralScores.size()*.01)];
    m_mseLimit=qBound(9.0,p99*1.5,64.0);
    m_calibration={{"samples",compressionErrors.size()},{"compressionMseP99",p99},{"acceptedMseMax",m_mseLimit},
        {"mseP50",compressionErrors.isEmpty()?0:compressionErrors[compressionErrors.size()/2]},
        {"mseP95",compressionErrors.isEmpty()?0:compressionErrors[int(compressionErrors.size()*.95)]},
        {"ssimP01",s01},{"ssimMin",qBound(.95,s01-.005,.985)},
        {"differentHdMseRatioMin",1.8},{"differentHdMseGapMin",4.0},{"rotations",4},{"reflections",false}};
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
    return rotated(out,s.rotation);
}
bool CTerrainHdSources::match(const QImage& published,const STerrainAtlasProfileSlot& p,Source* source,QString* reason,QJsonObject* evidence) const
{
    const QByteArray hash=pixelsHash(published);
    QVector<int> candidates=m_byPixels.value(hash);
    QSet<int> originIds;QSet<QString> visited;
    std::function<void(const STerrainAtlasProfileSlot&,int)> trace=[&](const STerrainAtlasProfileSlot& d,int depth) {
        if(depth>8)return;
        for(int alias:d.sourceSlotAliases) {
            const QString key=d.sourceNamespace.toLower()+":"+QString::number(alias);
            if(visited.contains(key))continue;visited.insert(key);
            const int id=m_byIdentity.value(key,-1);if(id>=0){originIds.insert(id);continue;}
            const auto old=m_origins.constFind(d.sourceNamespace.toLower());if(old==m_origins.cend())continue;
            for(const auto& definition:old->slotDefinitions)if(definition.slot==alias){trace(definition,depth+1);break;}
        }
    };trace(p,0);
    // A source hash records pre-publication pixels. It may prove provenance
    // despite additional BC1 generations. Never trust namespace/slot alone.
    auto orderedOrigins=originIds.values();std::sort(orderedOrigins.begin(),orderedOrigins.end());
    int originMatch=-1;QByteArray originHd;
    for(int id:orderedOrigins) {
        if(id<0)continue;
        const auto& s=m_sources[id];
        if(candidates.contains(id)) {
            const QByteArray detail=m_hdHashes.value(id);
            if(originMatch>=0&&originHd!=detail) {
                if(reason)*reason="ambiguous-provenance-HD-aliases";
                if(evidence)*evidence={{"method","exact-provenance"},{"reason","ambiguous-provenance-HD-aliases"}};
                return false;
            }
            originMatch=id;originHd=detail;
        }
    }
    if(originMatch>=0){*source=m_sources[originMatch];if(evidence)*evidence={{"method","exact-provenance"}};return true;}
    if(candidates.isEmpty()) {
        if(m_sdOnly.contains(hash)) {
            QStringList sd=m_sdOnly.value(hash);sd.removeDuplicates();bool hdAlias=false;
            for(const auto& alias:sd)hdAlias=hdAlias||m_byIdentity.contains(alias);
            if(!hdAlias) {
                if(reason)*reason="stock-only-SD-source";
                if(evidence)*evidence={{"method","exact-SD-witness"},{"sdSources",QJsonArray::fromStringList(sd)}};
                return false;
            }
        }
        QByteArray key=hash;for(int id:orderedOrigins)key+=':'+QByteArray::number(id);
        if(m_matchCache.contains(key)) {
            const auto cached=m_matchCache[key];if(evidence)*evidence=cached;
            if(reason)*reason=cached["reason"].toString();const int id=cached["sourceId"].toInt(-1);
            if(id<0)return false;*source=m_sources[id];source->rotation=cached["rotation"].toInt();return true;
        }
        const auto target=descriptor(published);
        QVector<QPair<double,int>> nearest;
        // Jensen's inequality makes block-average error a lower bound on RGB MSE.
        for(int i=0;i<m_descriptors.size();++i) {
            const auto& d=m_descriptors[i];double distance=0;
            for(int k=0;k<48;++k)distance+=(target[k]-d.mean[k])*(target[k]-d.mean[k]);
            distance/=48;
            if(distance<=m_mseLimit*1.8+4)nearest.append(qMakePair(distance,i));
        }
        std::sort(nearest.begin(),nearest.end());
        struct Candidate { int id,rotation; QJsonObject score; QByteArray hd; };
        QVector<Candidate> scored;
        // No top-N cap: all candidates within the lower bound are compared,
        // including duplicates, so an unseen second candidate cannot be ignored.
        for(const auto& item:nearest) {
            const auto& d=m_descriptors[item.second];auto s=m_sources[d.source];s.rotation=d.rotation;
            QImage low=d.reduction?image(s,256).scaled(128,128,Qt::IgnoreAspectRatio,Qt::SmoothTransformation):image(s,128);
            const auto score=metrics(published,low);
            if(score["mse"].toDouble()>m_mseLimit*1.8+4||score["alpha"].toDouble()!=0)continue;
            Candidate c{d.source,d.rotation,score,{}};scored.append(c);
        }
        std::sort(scored.begin(),scored.end(),[](const Candidate& a,const Candidate& b){return a.score["mse"].toDouble()<b.score["mse"].toDouble();});
        QJsonObject receipt;receipt["method"]="calibrated-fuzzy";receipt["considered"]=nearest.size();
        int accepted=-1;
        if(!scored.isEmpty()) {
            auto best=scored.first();
            // Recorded multi-generation slot aliases can disambiguate sources,
            // but never relax the measured pixel/structural bounds.
            if(originIds.size()==1)for(const auto& candidate:scored)if(originIds.contains(candidate.id)&&candidate.score["mse"].toDouble()<=m_mseLimit&&candidate.score["ssim"].toDouble()>=m_calibration["ssimMin"].toDouble()){best=candidate;receipt["provenanceQualified"]=true;break;}
            auto s=m_sources[best.id];s.rotation=best.rotation;best.hd=pixelsHash(image(s,256));
            double second=1e9,secondSameOrigin=1e9;
            for(int i=0;i<scored.size();++i) {
                auto next=m_sources[scored[i].id];next.rotation=scored[i].rotation;
                if((next.rotation?pixelsHash(image(next,256)):m_hdHashes.value(scored[i].id))!=best.hd) {
                    second=qMin(second,scored[i].score["mse"].toDouble());
                    if(originIds.contains(scored[i].id))secondSameOrigin=qMin(secondSameOrigin,scored[i].score["mse"].toDouble());
                }
            }
            const double mse=best.score["mse"].toDouble();receipt["best"]=identity(s);receipt["rotation"]=best.rotation;
            receipt["score"]=best.score;receipt["secondDifferentHdMse"]=second;
            if(mse<=m_mseLimit&&best.score["ssim"].toDouble()>=m_calibration["ssimMin"].toDouble()&&
               (receipt["provenanceQualified"].toBool()?secondSameOrigin:second)>=qMax(mse*1.8,mse+4))accepted=best.id;
            else receipt["reason"]=mse<=m_mseLimit?"ambiguous-fuzzy-HD-candidates":"no-HD-within-calibrated-error";
        } else receipt["reason"]="no-HD-within-calibrated-error";
        if(accepted<0&&m_sdOnly.contains(hash)) {
            receipt["reason"]="stock-only-SD-source";
            receipt["sdSources"]=QJsonArray::fromStringList(m_sdOnly.value(hash));
        }
        receipt["sourceId"]=accepted;m_matchCache.insert(key,receipt);if(evidence)*evidence=receipt;
        if(accepted<0){if(reason)*reason=receipt["reason"].toString();return false;}
        *source=m_sources[accepted];source->rotation=receipt["rotation"].toInt();return true;
    }
    QByteArray hd;
    for(int id:candidates) {
        if(!m_hdHashes.contains(id))m_hdHashes.insert(id,pixelsHash(image(m_sources[id],256)));
        const QByteArray next=m_hdHashes.value(id);
        if(!hd.isEmpty()&&hd!=next) {if(reason)*reason="ambiguous-HD-collision";return false;}
        hd=next;
    }
    *source=m_sources[candidates.first()];if(evidence)*evidence={{"method","exact-pixels"}};return true;
}

STerrainHdRepublishResult rebuildTerrainAtlasHd(const STerrainAtlasProfile& profile,
    const QVector<QByteArray>& authored,const CTerrainHdSources& sources,bool upgradePartialPages)
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
        if(r.before[slot].width()==256) {
            upgraded[slot]=r.before[slot];slotRows.append(QJsonObject{{"slot",slot},{"alreadyHd",true}});continue;
        }
        CTerrainHdSources::Source s;QString reason;
        QJsonObject evidence;
        bool matched=sources.match(r.before[slot],defs.value(slot),&s,&reason,&evidence);
        QJsonObject row;row["slot"]=slot;row["assigned"]=defs.value(slot).assigned;
        row["stockMatched"]=matched;
        row["matching"]=evidence;
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
        if(!d.mixedEdgeRecipes.isEmpty()) {
            QJsonObject recipe;QString why;
            const auto detail=mixedEdgeHd(profile,slot,r.before,matchedSources,sources,&recipe,&why);
            auto row=slotRows[slot].toObject();
            if(!detail.isNull()){upgraded[slot]=detail;row["regenerated"]=true;row["recipe"]=recipe;row.remove("reason");++generated;}
            else row["reason"]=why;
            slotRows[slot]=row;continue;
        }
        const auto signature=CTerrainTransitionCatalog::mirroredGridY(d.signature);
        QSet<qint32> ids{signature.northWest,signature.northEast,signature.southEast,signature.southWest};
        if(ids.size()<2)continue;
        auto slotPolicies=policies;
        QMap<qint32,CTerrainHdSources::Source> cornerPolicy;
        const qint32 cornerIds[]={d.signature.northWest,d.signature.northEast,d.signature.southEast,d.signature.southWest};
        const QPoint corners[]={QPoint(0,0),QPoint(112,0),QPoint(112,112),QPoint(0,112)};
        for(qint32 id:ids) {
            double best=1e9;CTerrainHdSources::Source selected;
            QSet<QString> seen;
            for(const auto& uniform:profile.slotDefinitions) {
                const auto sig=uniform.signature;
                if(!uniform.assigned||sig.northWest!=id||sig.northEast!=id||sig.southEast!=id||sig.southWest!=id||uniform.slot<0||uniform.slot>=matchedSources.size())continue;
                const auto candidate=matchedSources[uniform.slot];
                if(candidate.archive<0||seen.contains(identity(candidate)))continue;seen.insert(identity(candidate));
                const QImage low=sources.image(candidate,128);double score=0;
                for(int corner=0;corner<4;++corner)if(cornerIds[corner]==id) {
                    const QRect patch(corners[corner],QSize(16,16));
                    score+=metrics(low.copy(patch),r.before[slot].copy(patch))["mse"].toDouble();
                }
                if(score<best){best=score;selected=candidate;}
            }
            if(selected.archive>=0)cornerPolicy[id]=selected;
        }
        if(cornerPolicy.size()==ids.size())slotPolicies.append(cornerPolicy);
        QImage witness;QJsonObject recipe;bool ambiguous=false;
        double bestError=1e9,secondError=1e9;QByteArray bestHd;
        for(int policy=0;policy<slotPolicies.size();++policy) {
            auto selected=slotPolicies[policy];QMap<qint32,QImage> low,high;bool complete=true;
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
                if(original.isNull())continue;
                const QImage encoded=roundTrip(original);
                const bool exact=pixelsHash(encoded)==pixelsHash(r.before[slot]);
                const auto score=exact?QJsonObject{{"mse",0.0},{"ssim",1.0},{"alpha",0.0}}:metrics(encoded,r.before[slot]);
                const double error=score["mse"].toDouble();
                if(error>sources.calibration()["acceptedMseMax"].toDouble()*1.8+4||score["alpha"].toDouble()!=0)continue;
                const QImage detail=next.reviewGeneratedBlend(signature,variation,nullptr,nullptr,owner);
                if(detail.isNull())continue;
                const QByteArray hdHash=pixelsHash(detail);
                if(error>=bestError) {if(hdHash!=bestHd)secondError=qMin(secondError,error);continue;}
                if(hdHash!=bestHd)secondError=qMin(secondError,bestError);
                bestError=error;bestHd=hdHash;
                witness=detail;recipe["algorithm"]="Modern-exact";recipe["mask"]=label;
                recipe["variation"]=variation;recipe["owner"]=owner;recipe["sourcePolicy"]=policy;
                recipe["sourceCompressionGeneration"]=sourceGeneration;
                recipe["matching"]=exact?"exact-witness":"calibrated-recipe-witness";recipe["score"]=score;
                QJsonObject sourceRows;for(qint32 id:ids)sourceRows[QString::number(id)]=identity(selected[id]);
                recipe["sources"]=sourceRows;
            }
            for(auto it=low.begin();it!=low.end();++it)it.value()=roundTrip(it.value());
            }
        }
        ambiguous=!witness.isNull()&&secondError<qMax(bestError*1.8,bestError+4);
        if(bestError>sources.calibration()["acceptedMseMax"].toDouble()||recipe["score"].toObject()["ssim"].toDouble()<sources.calibration()["ssimMin"].toDouble())witness=QImage();
        recipe["secondDifferentHdMse"]=secondError;
        QJsonObject row=slotRows[slot].toObject();
        if(!witness.isNull()&&!ambiguous){upgraded[slot]=witness;row["regenerated"]=true;row["recipe"]=recipe;row.remove("reason");++generated;}
        else row["reason"]=ambiguous?"ambiguous-generated-HD-recipe":"legacy-recipe-not-reproducible";
        slotRows[slot]=row;
    }
    int retained=0,appliedHd=0,appliedGenerated=0,blockedHd=0,blockedGenerated=0,carriedSd=0,native128=0;
    QJsonArray pageRows;
    for(int page=0;page<authored.size();++page) {
        bool complete=true,any=false;
        for(int local=0;local<64;++local) {const bool known=!upgraded[page*64+local].isNull();complete=complete&&known;any=any||known;}
        const bool publishHd=complete||(upgradePartialPages&&any);
        QByteArray payload=authored[page];
        const bool existingHd=word(authored[page],4)==2048;
        if(publishHd&&!existingHd) {
            auto assembled=upgraded;
            for(int local=0;local<64;++local)if(assembled[page*64+local].isNull())assembled[page*64+local]=r.before[page*64+local].scaled(256,256,Qt::IgnoreAspectRatio,Qt::FastTransformation);
            QImage pixels=CTerrainVirtualAtlas::composePage(assembled,page*64,256);
            payload=ei::encodeMmpDxt1(pixels,true,&r.error);
            if(payload.isEmpty())return r;
            for(int local=0;local<64;++local) {
                const auto source=matchedSources[page*64+local];
                if(source.archive>=0&&!transplantTile(&payload,local,sources.payload(source),source.slot,source.rotation)) {
                    r.error="Could not retain original HD BC1 blocks";return r;
                }
                if(upgraded[page*64+local].isNull()&&!carrySdTile(&payload,authored[page],local)) {
                    r.error="Could not carry unchanged SD pixels and mip blocks";return r;
                }
            }
        }
        r.pages.append(payload);
        for(int local=0;local<64;++local) {
            const int slot=page*64+local;
            QImage pixels;if(!ei::decodeMmpDxt1AtlasTile(payload,local,&pixels,&r.error))return r;
            r.after.append(pixels);
            QJsonObject row=slotRows[slot].toObject();
            if(existingHd)row["result"]="already-HD-unchanged";
            else if(!publishHd) {
                ++native128;
                ++retained;row["result"]="retained-128";
                if(!upgraded[slot].isNull()) {
                    row["reason"]="page-has-unrecoverable-slots";
                    if(matchedSources[slot].archive>=0)++blockedHd;else ++blockedGenerated;
                }
            } else if(upgraded[slot].isNull()) {
                ++retained;++carriedSd;row["result"]="retained-128-detail-in-256-carrier";
                if(pixels!=r.before[slot].scaled(256,256,Qt::IgnoreAspectRatio,Qt::FastTransformation)) {r.error="SD carrier altered decoded pixels";return r;}
            } else if(matchedSources[slot].archive>=0){row["result"]="original-HD-BC1";++appliedHd;}
            else {row["result"]="regenerated-256";++appliedGenerated;}
            slotRows[slot]=row;
        }
        QJsonObject row;row["page"]=page;row["upgraded"]=publishHd;row["bytesBefore"]=authored[page].size();row["bytesAfter"]=payload.size();pageRows.append(row);
    }
    r.profile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(r.after);
    r.receipt["namespace"]=profile.mprNamespace;r.receipt["physicalSlots"]=r.before.size();
    r.receipt["calibration"]=sources.calibration();
    QJsonArray sourcePageHashes;for(const auto& page:authored)sourcePageHashes.append(QString::fromLatin1(QCryptographicHash::hash(page,QCryptographicHash::Sha256).toHex()));
    r.receipt["sourcePageSha256"]=sourcePageHashes;
    r.receipt["stockMatched"]=hd;r.receipt["regenerated"]=generated;r.receipt["unmatched"]=r.before.size()-hd-generated;r.receipt["slots"]=slotRows;
    r.receipt["hdApplied"]=appliedHd;r.receipt["regeneratedApplied"]=appliedGenerated;r.receipt["retained128"]=retained;
    r.receipt["sdCarried256"]=carriedSd;r.receipt["native128Slots"]=native128;r.receipt["upgradePartialPages"]=upgradePartialPages;
    r.receipt["pageBlockedHd"]=blockedHd;r.receipt["pageBlockedRegenerated"]=blockedGenerated;r.receipt["pages"]=pageRows;
    r.success=sources.current(&r.error);return r;
}

bool prepareTerrainHdPublication(const CMprDocument& map,const QByteArray& oldSemantic,
    STerrainHdRepublishResult* r)
{
    if(!r||!r->success||r->pages.isEmpty())return false;
    QCryptographicHash content(QCryptographicHash::Sha256);
    for(const auto& page:r->pages)content.addData(page);
    // Pixels alone do not identify a portable atlas: identical artwork may
    // carry different semantic labels, reviewed classes or animation ranges.
    content.addData(r->profile.toBytes());
    content.addData(STerrainSemanticFieldDocument::tileBindingSha256(map));
    const QString oldNamespace=map.mapInfo().innerNamespace;
    const auto oldProfile=r->profile;
    const QString ns=CTerrainVirtualAtlas::contentResourceIdentity(oldNamespace,content.result().toHex());
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
    // Keep recovery evidence outside canonical profile JSON, so 137 and 138
    // serialize the same profile binding and do not lose shared-corner state.
    SResArchiveNewMember evidence;evidence.name="ei_atlas_hd_recovery_"+ns+".json";
    evidence.payload=QJsonDocument(r->receipt).toJson(QJsonDocument::Compact);r->publishedMembers.append(evidence);
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
        auto result=rebuildTerrainAtlasHd(p,pages,sources,cfg["upgradePartialPages"].toBool(true));
        if(!result.success){QTextStream(stderr)<<result.error<<'\n';return 2;}
        if(build) {
            const QString output=QDir(outputRoot).filePath(QFileInfo(mapPath).completeBaseName());
            if(!QDir().mkpath(output))return 2;
            QByteArray semantic=map.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
            if(semantic.isEmpty())semantic=published.readMember(STerrainSemanticFieldDocument::memberName(ns,QFileInfo(mapPath)));
            if(!prepareTerrainHdPublication(map,semantic,&result)){QTextStream(stderr)<<result.error<<'\n';return 2;}
            if(cfg["pngMaps"].toArray().contains(QFileInfo(mapPath).completeBaseName()))
                if(!previews(map,&result,output)){QTextStream(stderr)<<"Preview export failed\n";return 2;}
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

#include "terrain_hd_variants.h"
#include "../resources/mmp_codec.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace TerrainHdVariants {
namespace {
const int N=256, P=80, O=16;
// Fully specified xorshift32 plus unbiased bounded draws. No implementation-
// dependent std::distribution or shuffle; versioned independently of NumPy.
struct Rng {
    quint32 value;
    explicit Rng(quint32 seed):value(seed?seed:0x6d2b79f5u){}
    quint32 next(){value^=value<<13;value^=value>>17;value^=value<<5;return value;}
    int bounded(int n){const quint32 bound=quint32(n),limit=quint32(-bound)%bound;
        quint32 x;do{x=next();}while(x<limit);return int(x%bound);}
    template<class T> void shuffle(T& a){for(int i=int(a.size())-1;i>0;--i)std::swap(a[i],a[bounded(i+1)]);}
};
QImage orient(const QImage& a,int d){
    QImage b=(d>=4)?a.mirrored(true,false):a;
    return b.transformed(QTransform().rotate(-90*(d%4))).convertToFormat(QImage::Format_ARGB32);
}
QByteArray hash(const QImage& a){
    QCryptographicHash h(QCryptographicHash::Sha256);
    const auto b=a.convertToFormat(QImage::Format_RGBA8888);
    for(int y=0;y<b.height();++y)h.addData(reinterpret_cast<const char*>(b.constScanLine(y)),b.width()*4);
    return h.result().toHex();
}
double luma(QRgb p){return .2126*qRed(p)+.7152*qGreen(p)+.0722*qBlue(p);}
int channel(QRgb p,int c){return c==0?qRed(p):c==1?qGreen(p):qBlue(p);}
double cost(QRgb a,QRgb b){double sum=0;for(int c=0;c<3;++c){int d=channel(a,c)-channel(b,c);sum+=d*d;}return sum/3.;}
std::array<int,P> cut(const std::array<double,P*P>& costs,int side){
    double sums[P][O];int parents[P][O];
    const auto sample=[&](int y,int x){return costs[side==0?y*P+x:side==1?y*P+P-1-x:side==2?x*P+y:(P-1-x)*P+y];};
    for(int x=0;x<O;++x)sums[0][x]=sample(0,x);
    for(int y=1;y<P;++y)for(int x=0;x<O;++x){
        int best=std::max(0,x-1);
        for(int k=x;k<=std::min(O-1,x+1);++k)if(sums[y-1][k]<sums[y-1][best])best=k;
        sums[y][x]=sums[y-1][best]+sample(y,x);parents[y][x]=best;
    }
    std::array<int,P> seam;seam[P-1]=0;
    for(int x=1;x<O;++x)if(sums[P-1][x]<sums[P-1][seam[P-1]])seam[P-1]=x;
    for(int y=P-1;y>0;--y)seam[y-1]=parents[y][seam[y]];
    return seam;
}
void histogram(QImage& out,const QImage& anchor){
    const int side=N-2*Rim;
    std::vector<int> order(side*side),ref(side*side),values(side*side);
    for(int c=0;c<3;++c){
        for(int y=0;y<side;++y)for(int x=0;x<side;++x){int i=y*side+x;
            values[i]=channel(out.pixel(x+Rim,y+Rim),c);ref[i]=channel(anchor.pixel(x+Rim,y+Rim),c);}
        std::iota(order.begin(),order.end(),0);std::stable_sort(order.begin(),order.end(),[&](int a,int b){return values[a]<values[b];});
        std::sort(ref.begin(),ref.end());
        for(int i=0;i<int(order.size());++i){int x=order[i]%side+Rim,y=order[i]/side+Rim;QRgb p=out.pixel(x,y);
            out.setPixel(x,y,qRgb(c==0?ref[i]:qRed(p),c==1?ref[i]:qGreen(p),c==2?ref[i]:qBlue(p)));}
    }
}
QImage small(QImage a){while(a.width()>64)a=boxHalf(a);return a;}
double rmse(const QImage& a,const QImage& b){double sum=0;
    for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x)sum+=cost(a.pixel(x,y),b.pixel(x,y));
    return std::sqrt(sum/(a.width()*a.height()));}
QVector<QByteArray> mipBlocks(const QImage& image){
    const QByteArray mmp=ei::encodeMmpDxt1(image,true,nullptr);QVector<QByteArray> out;int offset=76;
    if(mmp.isEmpty())return out;
    for(int size=N;size>=4;size/=2){int bytes=size*size/2;out.append(mmp.mid(offset,bytes));offset+=bytes;}
    return out;
}
bool protectedBlock(int x,int y,int size){
    // Round the 24px footprint outwards to complete BC1 blocks, and protect
    // at least one whole block at small mips. Thus BC1 endpoint fitting cannot
    // contaminate the bilinear UV boundary (1/16 inset), including mip16/8/4.
    int pixels=std::max(4,((Rim*size+N-1)/N+3)/4*4);
    return x<pixels/4||y<pixels/4||x>=size/4-pixels/4||y>=size/4-pixels/4;
}
}
QByteArray canonicalD4(const QImage& a){QByteArray value;for(int d=0;d<8;++d){auto h=hash(orient(a,d));if(value.isEmpty()||h<value)value=h;}return value;}
QImage boxHalf(const QImage& a){
    QImage b(a.width()/2,a.height()/2,QImage::Format_ARGB32);
    for(int y=0;y<b.height();++y)for(int x=0;x<b.width();++x){int c[3]={0,0,0};
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)for(int k=0;k<3;++k)c[k]+=channel(a.pixel(x*2+dx,y*2+dy),k);
        b.setPixel(x,y,qRgb((c[0]+2)/4,(c[1]+2)/4,(c[2]+2)/4));}return b;
}
QImage quilt(const QVector<Source>& sources,int anchor,quint32 seed,QJsonArray* trace,const std::function<bool()>& cancelled){
    if(sources.isEmpty()||anchor<0||anchor>=sources.size())return {};
    for(const auto& s:sources)if(s.pixels.size()!=QSize(N,N))return {};
    Rng rng(seed);QImage out=sources[anchor].pixels.convertToFormat(QImage::Format_ARGB32);
    QVector<QVector<QImage>> donors;for(const auto& source:sources){QVector<QImage> d;for(int i=0;i<8;++i)d.append(orient(source.pixels,i));donors.append(d);}
    std::array<int,16> order;std::iota(order.begin(),order.end(),0);rng.shuffle(order);
    std::vector<int> sourceOrder(sources.size());std::iota(sourceOrder.begin(),sourceOrder.end(),0);rng.shuffle(sourceOrder);
    std::array<int,N*N> ownership;ownership.fill(-1);
    const int positions[]={24,68,112,152};QVector<QJsonObject> records;
    for(int step=0;step<16;++step){
        if(cancelled&&cancelled())return {};
        int x=positions[order[step]%4],y=positions[order[step]/4],source=sourceOrder[step%sources.size()];
        double best=std::numeric_limits<double>::infinity();int sx=0,sy=0,orientation=0;
        std::array<double,P*P> costs,bestCosts;
        for(int candidate=0;candidate<48;++candidate){
            int d=rng.bounded(8),cy=rng.bounded(N-P+1),cx=rng.bounded(N-P+1);double error=0;
            for(int py=0;py<P;++py){auto old=reinterpret_cast<const QRgb*>(out.constScanLine(y+py))+x;
                auto src=reinterpret_cast<const QRgb*>(donors[source][d].constScanLine(cy+py))+cx;
                for(int px=0;px<P;++px){double e=cost(old[px],src[px]);costs[py*P+px]=e;
                    if(px<O||py<O||px>=P-O||py>=P-O)error+=e;}}
            if(error<best){best=error;bestCosts=costs;sx=cx;sy=cy;orientation=d;}
        }
        auto left=cut(bestCosts,0),right=cut(bestCosts,1),top=cut(bestCosts,2),bottom=cut(bestCosts,3);int inserted=0;
        for(int py=0;py<P;++py)for(int px=0;px<P;++px)
            if(px>left[py]&&P-1-px>right[py]&&py>top[px]&&P-1-py>bottom[px]){
                out.setPixel(x+px,y+py,donors[source][orientation].pixel(sx+px,sy+py));ownership[(y+py)*N+x+px]=step;++inserted;}
        records.append(QJsonObject{{"source",sources[source].id},{"sourceIndex",source},{"x",x},{"y",y},{"sourceX",sx},{"sourceY",sy},{"d4",orientation},{"insertedPixels",inserted}});
    }
    QVector<int> retained(sources.size(),0);for(int step=0;step<records.size();++step){
        int count=int(std::count(ownership.begin(),ownership.end(),step));records[step]["survivingPixels"]=count;
        retained[records[step]["sourceIndex"].toInt()]+=count;if(trace)trace->append(records[step]);}
    for(int count:retained)if(!count)return {};
    histogram(out,sources[anchor].pixels);return out;
}
QJsonObject measure(const QImage& image,const QImage& anchor,const QVector<QImage>& prior){
    double mean=0,mean0=0,square=0,square0=0,sumX=0,sumY=0,maxX=0,maxY=0;int rimDelta=0;
    std::array<double,223> columns{},rows{};
    for(int y=0;y<N;++y)for(int x=0;x<N;++x){
        if(x<Rim||y<Rim||x>=N-Rim||y>=N-Rim)for(int c=0;c<3;++c)rimDelta=std::max(rimDelta,std::abs(channel(image.pixel(x,y),c)-channel(anchor.pixel(x,y),c)));
        if(x<16||y<16||x>=240||y>=240)continue;
        double v=luma(image.pixel(x,y)),a=luma(anchor.pixel(x,y));mean+=v;mean0+=a;square+=v*v;square0+=a*a;
        if(x<239){double d=std::abs(v-luma(image.pixel(x+1,y)));columns[x-16]+=d;sumX+=d;}
        if(y<239){double d=std::abs(v-luma(image.pixel(x,y+1)));rows[y-16]+=d;sumY+=d;}
    }
    mean/=224*224;mean0/=224*224;square=std::sqrt(std::max(0.,square/(224*224)-mean*mean));square0=std::sqrt(std::max(0.,square0/(224*224)-mean0*mean0));
    for(double x:columns)maxX=std::max(maxX,x);
    for(double y:rows)maxY=std::max(maxY,y);
    double minC4=1e9,minD4=1e9;auto a=small(image);
    for(const auto& previous:prior){auto b=small(previous);for(int d=0;d<8;++d){double e=rmse(a,orient(b,d));minD4=std::min(minD4,e);if(d<4)minC4=std::min(minC4,e);}}
    return {{"rimDelta",rimDelta},{"lumaDrift",std::abs(mean-mean0)},{"std",square},{"anchorStd",square0},
        {"stdRatio",square0>1e-9?square/square0:(square<1e-9?1.:1e9)},
        {"lineRatio",std::max(maxX/std::max(sumX/223.,1e-6),maxY/std::max(sumY/223.,1e-6))},
        {"minC4Rmse64",minC4},{"minD4Rmse64",minD4}};
}
QStringList rejectReasons(const QJsonObject& m){QStringList r;
    if(m["rimDelta"].toInt()!=0)r<<"changed anchor rim";
    if(m["lumaDrift"].toDouble()>8.)r<<"luma drift >8";
    if(m["stdRatio"].toDouble()<.8||m["stdRatio"].toDouble()>1.25)r<<"std outside 0.8..1.25";
    if(m["lineRatio"].toDouble()>2.5)r<<"internal straight line >2.5";
    if(m["minC4Rmse64"].toDouble()<5.)r<<"min C4 RMSE64 <5";
    if(m["minD4Rmse64"].toDouble()<5.)r<<"min D4 RMSE64 <5";
    return r;
}
Result generate(const QString& key,const QVector<Source>& input,const std::function<bool(int,int)>& progress){
    Result result;QVector<Source> sources;QSet<QByteArray> seen;
    for(const auto& s:input){if(s.pixels.size()!=QSize(N,N)){result.error="HD source must be 256x256";return result;}
        auto h=canonicalD4(s.pixels);if(!seen.contains(h)){sources.append(s);seen.insert(h);}}
    result.originalCount=sources.size();if(sources.isEmpty()){result.error="No static opaque HD source";return result;}
    QVector<QImage> prior;for(const auto& s:sources)prior.append(s.pixels);
    const auto digest=QCryptographicHash::hash((QString::number(AlgorithmVersion)+":"+key).toUtf8(),QCryptographicHash::Sha256).toHex();
    QByteArray sourceIdentity=digest;
    for(const auto& source:sources) {
        sourceIdentity+=canonicalD4(source.pixels);
        for(const auto& level:source.blocks)sourceIdentity+=QCryptographicHash::hash(level,QCryptographicHash::Sha256);
    }
    const auto idDigest=QCryptographicHash::hash(sourceIdentity,QCryptographicHash::Sha256).toHex();
    bool ok=false;const quint32 base=digest.left(8).toUInt(&ok,16)^20261011u;Q_UNUSED(ok);
    int attempts=0;
    while(prior.size()<TargetCount){
        const int index=prior.size(),anchor=index%sources.size();bool accepted=false;
        for(int attempt=0;attempts<MaxAttemptsPerVariant;++attempt,++attempts){
            if(progress&&!progress(result.variants.size(),TargetCount-sources.size())){result.cancelled=true;result.error="Cancelled";return result;}
            quint32 seed=base+quint32(index)*0x9e3779b9u+quint32(attempt);QJsonArray trace;
            auto image=quilt(sources,anchor,seed,&trace,[&](){return progress&&!progress(result.variants.size(),TargetCount-sources.size());});
            if(image.isNull()){
                if(progress&&!progress(result.variants.size(),TargetCount-sources.size())){result.cancelled=true;result.error="Cancelled";return result;}
                ++result.rejected;result.rejections.append(QJsonObject{{"seed",double(seed)},{"reason","donor lost"}});continue;}
            auto metrics=measure(image,sources[anchor].pixels,prior);auto reasons=rejectReasons(metrics);
            if(seen.contains(canonicalD4(image)))reasons<<"exact D4 duplicate";
            if(!reasons.isEmpty()){++result.rejected;result.rejections.append(QJsonObject{{"seed",double(seed)},{"metrics",metrics},{"reasons",QJsonArray::fromStringList(reasons)}});continue;}
            Variant v;v.seed=seed;v.anchor=sources[anchor].id;v.id=QStringLiteral("a%1-%2-%3").arg(AlgorithmVersion).arg(QString::fromLatin1(idDigest.left(16))).arg(seed,8,16,QLatin1Char('0'));
            v.blocks=mipBlocks(image);auto anchorBlocks=sources[anchor].blocks;
            if(anchorBlocks.isEmpty())anchorBlocks=mipBlocks(sources[anchor].pixels);
            if(v.blocks.size()!=7||anchorBlocks.size()!=7){result.error="Incomplete BC1 mip chain";return result;}
            for(int level=0,size=N;level<7;++level,size/=2){
                if(anchorBlocks[level].size()!=size*size/2){result.error="Invalid anchor BC1 mip size";return result;}
                for(int by=0;by<size/4;++by)for(int bx=0;bx<size/4;++bx)if(protectedBlock(bx,by,size)){
                    int offset=(by*(size/4)+bx)*8;std::copy(anchorBlocks[level].constData()+offset,anchorBlocks[level].constData()+offset+8,v.blocks[level].data()+offset);}}
            QString error;if(!ei::decodeDxtImage(v.blocks[0],N,N,ei::DxtFormat::Dxt1,&v.pixels,&error)){result.error=error;return result;}
            // Re-check delivered BC1 pixels, not just an uncompressed proposal.
            QImage decodedAnchor;if(!ei::decodeDxtImage(anchorBlocks[0],N,N,ei::DxtFormat::Dxt1,&decodedAnchor,&error)){result.error=error;return result;}
            auto delivered=measure(v.pixels,decodedAnchor,prior);reasons=rejectReasons(delivered);
            if(!reasons.isEmpty()){++result.rejected;result.rejections.append(QJsonObject{{"seed",double(seed)},{"stage","BC1"},{"metrics",delivered},{"reasons",QJsonArray::fromStringList(reasons)}});continue;}
            v.metadata={{"id",v.id},{"algorithm",AlgorithmVersion},{"key",key},{"seed",double(seed)},
                {"anchor",v.anchor},{"patches",trace},{"metrics",delivered},{"uncompressedMetrics",metrics},{"pixelSha256",QString::fromLatin1(hash(v.pixels))}};
            result.variants.append(v);prior.append(v.pixels);seen.insert(canonicalD4(v.pixels));accepted=true;++attempts;break;
        }
        if(!accepted){
            result.shortfall=true;
            QStringList reasons;
            for(const auto& entry:result.rejections) {
                const auto r=entry.toObject();
                for(const auto& reason:r["reasons"].toArray())
                    if(!reasons.contains(reason.toString()))reasons.append(reason.toString());
                if(r.contains("reason")&&!reasons.contains(r["reason"].toString()))reasons.append(r["reason"].toString());
            }
            result.shortfallReason=QStringLiteral("Недобор: %1 из 6; причина: %2 (%3 кандидатов на комбинацию материал/класс/слой)")
                .arg(prior.size()).arg(reasons.join(QStringLiteral("; "))).arg(MaxAttemptsPerVariant);
            break;
        }
    }
    result.success=true;return result;
}
}

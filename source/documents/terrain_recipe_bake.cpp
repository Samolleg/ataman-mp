#include "terrain_recipe_bake.h"

#include <QCryptographicHash>
#include <QCache>
#include <QDataStream>
#include <QVarLengthArray>
#include <QtEndian>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace
{
QVector<qint32> corners(const STerrainTransitionSignature& s)
{
    return {s.northWest, s.northEast, s.southEast, s.southWest};
}

quint32 seed(qint64 x, qint64 y, qint64 salt)
{
    const QByteArray bytes = QByteArray::number(x) + ',' + QByteArray::number(y)
        + ',' + QByteArray::number(salt);
    const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(hash.constData()));
}

double smooth(double a)
{
    a = qBound(0.0, a, 1.0);
    return a * a * (3.0 - 2.0 * a);
}

double lattice(qint64 x, qint64 y, quint64 salt)
{
    quint64 z = quint64(x) * Q_UINT64_C(0x9e3779b1)
        ^ quint64(y) * Q_UINT64_C(0x85ebca77) ^ salt;
    z = (z ^ (z >> 16)) * Q_UINT64_C(0x7feb352d);
    z = (z ^ (z >> 15)) * Q_UINT64_C(0x846ca68b);
    z ^= z >> 16;
    // Match the prototype's float32 lattice values and uint64 wraparound.
    return float(float(z & 0xffffff) / float(0xffffff) * 2.0f - 1.0f);
}

double noise(double x, double y, quint64 salt)
{
    const qint64 ix = qint64(std::floor(x)), iy = qint64(std::floor(y));
    const double fx = smooth(x - ix), fy = smooth(y - iy);
    return ((1-fx)*lattice(ix,iy,salt)+fx*lattice(ix+1,iy,salt))*(1-fy)
        + ((1-fx)*lattice(ix,iy+1,salt)+fx*lattice(ix+1,iy+1,salt))*fy;
}

QByteArray groupKey(const STerrainRecipe& r)
{
    QByteArray key;
    for(qint32 c : corners(r.signature)) key += QByteArray::number(c) + ',';
    return key + QByteArray::number(r.runtimeTerrainType);
}

QByteArray pixelHash(const QImage& image)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y = 0; y < image.height(); ++y)
        hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)), image.width()*4);
    return hash.result();
}

bool opaqueSquare(const QImage& image)
{
    if(image.isNull() || image.width() != image.height() || image.width() < 2) return false;
    static thread_local QCache<qint64,bool> cache(8192);
    if(const auto* known=cache.object(image.cacheKey()))return *known;
    for(int y = 0; y < image.height(); ++y)
        for(int x = 0; x < image.width(); ++x)
            if(qAlpha(image.pixel(x,y)) != 255) {
                cache.insert(image.cacheKey(),new bool(false));return false;
            }
    cache.insert(image.cacheKey(),new bool(true));
    return true;
}

bool validStrip(const QVector<float>& strip)
{
    if(strip.isEmpty() || strip.size()%65) return false;
    for(float v : strip) if(!std::isfinite(v) || v < 0 || v > 1) return false;
    return true;
}

struct SPrepared
{
    QVector<qint32> ids;
    QVector<QVector<QImage>> textures;
    QMap<QPair<qint32,qint32>, const STerrainRecipeBrush*> brushes;
    QVector<QVector<quint32>> pairPhases;
};

using Scores=QVarLengthArray<double,4>;
Scores scores(const SPrepared& p, const QVector<qint32>& s,
                       double x, double y, int variant)
{
    const double weights[] = {(1-x)*(1-y), x*(1-y), x*y, (1-x)*y};
    const double fade = 16*x*(1-x)*y*(1-y);
    Scores result(p.ids.size());std::fill(result.begin(),result.end(),0.0);
    for(int i = 0; i < p.ids.size(); ++i)
    {
        double raw = 0;
        for(int c = 0; c < 4; ++c) if(s[c] == p.ids[i]) raw += weights[c];
        result[i] = raw + .045*noise(x*2,y*2,quint64(p.ids[i]&0xffff)+quint64(variant)*71)
            *fade*4*raw*(1-raw);
    }
    return result;
}

QRgb sample(const QVector<QImage>& textures, double x, double y, int variant, int rotation)
{
    const QImage& image = textures[variant%textures.size()];
    const double ox = x;
    if(rotation == 1) { x = y; y = 1-ox; }
    else if(rotation == 2) { x = 1-x; y = 1-y; }
    else if(rotation == 3) { x = 1-y; y = ox; }
    const int n = image.width();
    const int ix = int(std::floor((x-std::floor(x))*n+1e-7))%n;
    const int iy = int(std::floor((y-std::floor(y))*n+1e-7))%n;
    return image.pixel(ix,n-1-iy);
}

// Pair distance is the first-order metric score difference, not an exact SDF.
// x/y are the fractional coordinates of the world grid. Periodic pair phase
// and zero interior fade on every grid line keep the finite recipe grammar.
double pairOwnership(const SPrepared& p, const QVector<qint32>& s,
    const Scores& sc, const Scores& xp, const Scores& xm,
    const Scores& yp, const Scores& ym,
    int a, int b, double x, double y, int v, bool polished, double* distanceOut)
{
    const auto* brush=p.brushes.value(qMakePair(p.ids[a],p.ids[b]),nullptr);
    const double eps=1e-4;
    const double nx=((xp[b]-xm[b])-(xp[a]-xm[a]))/(2*eps)*smooth(qMin(x,1-x)*8);
    const double ny=((yp[b]-ym[b])-(yp[a]-ym[a]))/(2*eps)*smooth(qMin(y,1-y)*8);
    const double distance=(sc[b]-sc[a])/qMax(std::hypot(nx,ny),.15)*127/1.15;
    if(distanceOut) *distanceOut=distance;
    // A periodic phase advances at full texel density up to every grid edge.
    // Multiplying the coordinate by fade froze a whole stock row there and
    // stretched its flecks into long bars. A 128-row period makes opposite
    // grid edges agree for every topology and variant, without a phase reset
    // inside the cell. Geometry noise still provides the four shape labels.
    const double along=(x+y)*128;
    int ti=(int(std::floor(along+1e-7))%128
        + int(p.pairPhases[a][b]))%512;
    if(!polished)
    {
        // Exact crisp-stock-v1 transport, retained for 138 HD witnesses.
        const double fade=16*x*(1-x)*y*(1-y);
        double q[4];
        for(int c=0;c<4;++c)q[c]=int(s[c]==p.ids[b])-int(s[c]==p.ids[a]);
        double cx=.5*((q[1]-q[0])+(q[2]-q[3]));
        double cy=.5*((q[3]-q[0])+(q[2]-q[1]));
        double cn=std::hypot(cx,cy);
        if(cn<1e-6){cx=1;cy=1;cn=std::sqrt(2.0);}
        const double oldAlong=((-cy*x+cx*y)/cn*128+v*47)*fade+p.pairPhases[a][b];
        ti=int(std::floor(oldAlong-std::floor(oldAlong/512)*512));
    }
    const int di=qBound(0,int(std::nearbyint(distance))+32,64);
    return brush ? std::nearbyint(brush->ownership[ti*65+di]*4)/4 : 1;
}

QImage render(const SPrepared& p, const STerrainRecipe& r, int size, bool legacy, bool polished,
    const std::function<bool()>& cancelled)
{
    const QVector<qint32> s = corners(r.signature);
    QImage image(size,size,QImage::Format_RGBA8888);
    if(image.isNull()) return image;
    const int v = r.variant;
    QSet<qint32> distinct; for(qint32 material:s) distinct.insert(material);
    const bool junction=distinct.size()>2 && !legacy;
    for(int py = 0; py < size; ++py)
    {
        if(cancelled && cancelled())return {};
        uchar* row = image.scanLine(size-1-py);
        const double y = double(py)/(size-1);
        for(int px = 0; px < size; ++px)
        {
            const double x = double(px)/(size-1), fade = 16*x*(1-x)*y*(1-y);
            const Scores sc = scores(p,s,x,y,v);
            int a = -1, b = -1;
            for(int i = 0; i < sc.size(); ++i)
            {
                if(b < 0 || sc[i] >= sc[b]) { a = b; b = i; }
                else if(a < 0 || sc[i] >= sc[a]) a = i;
            }
            double alpha = 1;
            Scores weights(sc.size());std::fill(weights.begin(),weights.end(),0.0);
            if(a >= 0 && sc[a] > 0 && sc[b] > 0)
            {
                if(a > b) std::swap(a,b);
                const STerrainRecipeBrush* brush = p.brushes.value(qMakePair(p.ids[a],p.ids[b]),nullptr);
                // All required banks were checked before rendering.
                if(brush)
                {
                    const double eps = 1e-4;
                    const auto xp = scores(p,s,x+eps,y,v), xm = scores(p,s,x-eps,y,v);
                    const auto yp = scores(p,s,x,y+eps,v), ym = scores(p,s,x,y-eps,v);
                    alpha=pairOwnership(p,s,sc,xp,xm,yp,ym,a,b,x,y,v,polished,nullptr);
                    if(junction && fade>0)
                    {
                        // Product of pair victories avoids switching the top-two
                        // identity. Every pair keeps its own transported stock bank.
                        for(int i=0;i<sc.size();++i) if(sc[i]>0) weights[i]=1;
                        for(int i=0;i<sc.size();++i) if(sc[i]>0)
                            for(int j=i+1;j<sc.size();++j) if(sc[j]>0)
                            {
                                double d=0;
                                double win=pairOwnership(p,s,sc,xp,xm,yp,ym,i,j,x,y,v,polished,&d);
                                double third=0;
                                for(int k=0;k<sc.size();++k) if(k!=i && k!=j) third=qMax(third,sc[k]);
                                // At triple competition replace high-frequency
                                // fragments with a compact smooth blend. It fades
                                // continuously, and never modifies boundary bytes.
                                const double centre=smooth((third/qMax(qMin(sc[i],sc[j]),1e-12)-.35)/.55)
                                    *smooth(fade*4);
                                win=win*(1-centre)+smooth((d+12)/24)*centre;
                                weights[i]*=1-win; weights[j]*=win;
                            }
                        double sum=0; for(double w:weights) sum+=w;
                        // Quantized pair banks can contain a cyclic tournament.
                        // Bilinear scores provide a deterministic, finite fallback.
                        if(sum<=1e-12) { weights=sc; sum=0; for(double w:weights) sum+=w; }
                        for(double& w:weights) w/=sum;
                    }
                }
            }
            else { a = -1; }
            if(!junction || fade==0) { if(a>=0) weights[a]=1-alpha; weights[b]=alpha; }
            else if(a<0) weights[b]=1;
            double rgb[3] = {0,0,0};
            for(int i=0;i<weights.size();++i)
            {
                if(weights[i]==0) continue;
                const double weight = weights[i];
                const QRgb base = sample(p.textures[i],x,y,0,0);
                const QRgb alt = polished?sample(p.textures[i],x,y,v+1,0)
                    :sample(p.textures[i],x+.27,y+.43,v,v%4);
                // Only a compact boundary collar uses the common anchor.
                // Preserve source artwork in the body, including its grain;
                // no offset/rotation stretching or broad .55 ghost overlay.
                const double mix = polished?smooth(qMin(qMin(x,1-x),qMin(y,1-y))*16)
                    :fade*.55*smooth((noise(x*2,y*2,quint64(p.ids[i]&0xffff)+quint64(v)*33)+1)/2);
                const int bc[] = {qRed(base),qGreen(base),qBlue(base)};
                const int ac[] = {qRed(alt),qGreen(alt),qBlue(alt)};
                for(int c=0;c<3;++c) rgb[c] += (bc[c]*(1-mix)+ac[c]*mix)*weight;
            }
            const double modulation = polished?1:1+.07*noise(x*2,y*2,quint64(v)+211)*fade;
            for(int c=0;c<3;++c) row[px*4+c] = uchar(qBound(0,int(std::nearbyint(rgb[c]*modulation)),255));
            row[px*4+3] = 255;
        }
    }
    return image;
}
}

STerrainRecipeBudget CTerrainRecipeBaker::budget(quint64 recipes, quint64 unique, quint64 reserved)
{
    STerrainRecipeBudget b;
    b.recipeSlots=recipes; b.uniqueSlots=unique; b.reservedSlots=reserved;
    b.requiredSlots = unique > std::numeric_limits<quint64>::max()-reserved
        ? std::numeric_limits<quint64>::max() : unique+reserved;
    b.pages = b.requiredSlots/64 + (b.requiredSlots%64 != 0);
    b.exceeds2048=b.requiredSlots>2048;
    b.exceeds4096=b.requiredSlots>4096;
    b.exceeds8192=b.requiredSlots>8192;
    return b;
}

bool CTerrainRecipeBaker::estimateStockOwnership(const QImage& t, const QImage& a,
    const QImage& b, QVector<float>* output, double* rmse, QString* error)
{
    if(!output || !rmse || !opaqueSquare(t) || !opaqueSquare(a) || !opaqueSquare(b)
       || t.size()!=a.size() || t.size()!=b.size() || qint64(t.width())*t.height()>INT_MAX)
    {
        if(error) *error=QStringLiteral("Stock ownership requires matching opaque square images and output pointers.");
        return false;
    }
    QVector<float> result; result.reserve(t.width()*t.height()); double sum=0;
    for(int y=0;y<t.height();++y) for(int x=0;x<t.width();++x)
    {
        const QRgb tp=t.pixel(x,y), ap=a.pixel(x,y), bp=b.pixel(x,y);
        const int tv[]={qRed(tp),qGreen(tp),qBlue(tp)}, av[]={qRed(ap),qGreen(ap),qBlue(ap)}, bv[]={qRed(bp),qGreen(bp),qBlue(bp)};
        double dot=0,den=0;
        for(int c=0;c<3;++c) { dot+=(tv[c]-av[c])*(bv[c]-av[c]); den+=(bv[c]-av[c])*(bv[c]-av[c]); }
        // The old absolute floor biased low-contrast ownership toward first.
        // An indistinguishable pixel carries no recoverable ownership evidence.
        const double alpha=den>0 ? qBound(0.0,dot/den,1.0) : .5;
        result.append(float(alpha));
        for(int c=0;c<3;++c) { const double d=tv[c]-(av[c]+alpha*(bv[c]-av[c])); sum+=d*d; }
    }
    *output=result; *rmse=std::sqrt(sum/(t.width()*double(t.height())*3));
    if(error) error->clear();
    return true;
}

bool CTerrainRecipeBaker::makeBrush(qint32 a, qint32 b,
    const QVector<QVector<float>>& strips, STerrainRecipeBrush* output, QString* error)
{
    if(!output || a<=0 || b<=a || strips.isEmpty())
    {
        if(error) *error=QStringLiteral("Brush requires ordered positive materials and ownership strips.");
        return false;
    }
    for(const auto& strip : strips) if(!validStrip(strip))
    {
        if(error) *error=QStringLiteral("Ownership strip must be finite, 65-wide and in [0,1].");
        return false;
    }
    STerrainRecipeBrush result; result.firstMaterial=a; result.secondMaterial=b;
    result.ownership.resize(512*65);
    for(int chunk=0;chunk<8;++chunk)
    {
        const auto& strip=strips[int(seed(chunk,a,b)%quint32(strips.size()))];
        const int length=strip.size()/65, start=int(seed(chunk,b,a)%quint32(length));
        for(int row=0;row<64;++row) for(int c=0;c<65;++c)
        {
            const double taper=smooth((32-std::abs(c-32))/12.0);
            double alpha=strip[((start+row)%length)*65+c]*taper+(c>=32 ? 1 : 0)*(1-taper);
            if(c<2) alpha=0; else if(c>=63) alpha=1;
            result.ownership[(chunk*64+row)*65+c]=float(alpha);
        }
    }
    *output=result; if(error) error->clear(); return true;
}

QImage CTerrainRecipeBaker::makeOpaqueMaterialSource(const QImage& source)
{
    if(source.isNull() || source.width()!=source.height())return {};
    bool fullyOpaque=true;
    for(int y=0;y<source.height()&&fullyOpaque;++y)
        for(int x=0;x<source.width();++x)
            if(qAlpha(source.pixel(x,y))!=255){fullyOpaque=false;break;}
    if(fullyOpaque)return source;

    const QImage input=source.convertToFormat(QImage::Format_ARGB32);
    const int width=input.width(),height=input.height(),count=width*height;
    QVector<int> nearest(count,-1),queue;queue.reserve(count);
    int maxAlpha=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        maxAlpha=qMax(maxAlpha,qAlpha(input.pixel(x,y)));
    if(maxAlpha==0)return {};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const int index=y*width+x;
        if(qAlpha(input.pixel(x,y))==255) {
            nearest[index]=index;queue.append(index);
        }
    }
    if(queue.isEmpty())for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const int index=y*width+x;
        if(qAlpha(input.pixel(x,y))==maxAlpha){nearest[index]=index;queue.append(index);}
    }
    for(int head=0;head<queue.size();++head) {
        const int index=queue[head],x=index%width,y=index/width;
        const int neighbors[4]={x>0?index-1:-1,x+1<width?index+1:-1,
            y>0?index-width:-1,y+1<height?index+width:-1};
        for(int other:neighbors)if(other>=0&&nearest[other]<0) {
            nearest[other]=nearest[index];queue.append(other);
        }
    }
    QImage result(width,height,QImage::Format_ARGB32);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const int index=y*width+x;
        const int baseIndex=nearest[index];
        const QRgb pixel=input.pixel(x,y),base=input.pixel(baseIndex%width,baseIndex/width);
        const int alpha=qAlpha(pixel),inverse=255-alpha;
        const auto blend=[&](int value,int background) {
            return (value*alpha+background*inverse+127)/255;
        };
        result.setPixel(x,y,qRgba(blend(qRed(pixel),qRed(base)),
            blend(qGreen(pixel),qGreen(base)),blend(qBlue(pixel),qBlue(base)),255));
    }
    return result;
}

bool CTerrainRecipeBaker::makeDeterministicWorldBrush(qint32 first,qint32 second,
    STerrainRecipeBrush* output,QString* error)
{
    if(!output||first<=0||second<=first) {
        if(error)*error=QStringLiteral("World brush requires an ordered positive material pair.");
        return false;
    }
    QVector<float> strip(512*65,0.f);
    quint32 state=quint32(first)*0x9e3779b9u
        ^ quint32(second)*0x85ebca6bu ^ 0x43524f53u;
    if(state==0)state=1;
    const double phase=double((state>>8)&0xffffu)/65536.0*6.283185307179586;
    for(int row=0;row<512;++row) {
        const double wave=32.0+5.0*std::sin(row*6.283185307179586/83.0+phase)
            +2.0*std::sin(row*6.283185307179586/27.0+phase*1.7);
        state=state*1664525u+1013904223u;
        const double edge=wave+(double((state>>24)&0xffu)/255.0-.5)*2.0;
        for(int column=0;column<65;++column) {
            const double t=qBound(0.0,(column-edge+2.0)/4.0,1.0);
            strip[row*65+column]=float(t*t*(3.0-2.0*t));
        }
    }
    if(!makeBrush(first,second,{strip},output,error))return false;
    sharpenStockBrush(output);
    return true;
}

void CTerrainRecipeBaker::sharpenStockBrush(STerrainRecipeBrush* brush)
{
    if(!brush || brush->ownership.size()!=512*65) return;
    for(int row=0;row<512;++row) for(int c=0;c<65;++c)
    {
        const double level=std::nearbyint(brush->ownership[row*65+c]*4)/4;
        // Quantize the recovered coverage first, then distribute its uncertain
        // quarter levels as stock flecks. Pure source pixels remain pure. This
        // phase lives in the periodic bank, so shared world edges use it alike.
        const double threshold=(seed(row,c,0x43524953u)%256+.5)/256;
        brush->ownership[row*65+c]=level>threshold ? 1.f : 0.f;
    }
}

bool CTerrainRecipeBaker::extractStockStrip(const QImage& transition,
    const QImage& firstUniform, const QImage& secondUniform,
    qint32 firstMaterial, qint32 secondMaterial,
    const STerrainTransitionSignature& signature, QVector<float>* output, QString* error)
{
    auto refuse=[&](const QString& message) { if(error) *error=message; return false; };
    if(!output || firstMaterial<=0 || secondMaterial<=firstMaterial || transition.width()!=128)
        return refuse(QStringLiteral("Stock strip requires an ordered pair and a 128px profile tile."));
    const auto s=corners(signature);
    bool hasFirst=false,hasSecond=false;
    for(qint32 material:s)
    {
        if(material!=firstMaterial && material!=secondMaterial)
            return refuse(QStringLiteral("Stock strip only supports the exact two-material signature."));
        hasFirst |= material==firstMaterial; hasSecond |= material==secondMaterial;
    }
    if(!hasFirst || !hasSecond) return refuse(QStringLiteral("Stock signature is uniform."));
    QVector<float> alpha; double rmse=0;
    if(!estimateStockOwnership(transition,firstUniform,secondUniform,&alpha,&rmse,error)) return false;
    if(rmse>=14) return refuse(QStringLiteral("Stock RGB ownership fit exceeds the trusted residual."));
    // Infer physical orientation from corner pixels, without reassigning IDs.
    const int ox[]={0,112,112,0}, oy[]={0,0,112,112}; double means[4]={0,0,0,0};
    for(int c=0;c<4;++c) for(int y=0;y<16;++y) for(int x=0;x<16;++x)
        means[c]+=alpha[(oy[c]+y)*128+ox[c]+x]/256.0;
    QVector<int> oriented(4); double bestCorner=std::numeric_limits<double>::max();
    for(int mirror=0;mirror<2;++mirror) for(int turn=0;turn<4;++turn)
    {
        QVector<int> candidate(4); double e=0;
        for(int c=0;c<4;++c)
        {
            candidate[c]=s[((mirror ? 3-c : c)+turn)%4]==secondMaterial;
            e+=(means[c]-candidate[c])*(means[c]-candidate[c])/4;
        }
        if(e<bestCorner) { bestCorner=e; oriented=candidate; }
    }
    if(std::sqrt(bestCorner)>=.3) return refuse(QStringLiteral("Stock corner ownership is ambiguous."));
    // Gaussian coarse guide only; transported samples use the original alpha.
    // This CPU kernel is explicit and deterministic, unlike platform filters.
    auto blur=[](const QVector<double>& values,int width,int height,double sigma) {
        const int radius=int(std::ceil(sigma*3)); QVector<double> kernel(radius*2+1);
        double total=0; for(int k=-radius;k<=radius;++k) { kernel[k+radius]=std::exp(-k*k/(2*sigma*sigma)); total+=kernel[k+radius]; }
        for(auto& v:kernel) v/=total;
        QVector<double> temp(values.size()),result(values.size());
        for(int y=0;y<height;++y) for(int x=0;x<width;++x)
            for(int k=-radius;k<=radius;++k) temp[y*width+x]+=values[y*width+qBound(0,x+k,width-1)]*kernel[k+radius];
        for(int y=0;y<height;++y) for(int x=0;x<width;++x)
            for(int k=-radius;k<=radius;++k) result[y*width+x]+=temp[qBound(0,y+k,height-1)*width+x]*kernel[k+radius];
        return result;
    };
    QVector<float> bestStrip; double bestPurity=1,bestFar=1;
    QVector<float> rotated=alpha; QVector<int> cv=oriented;
    for(int turn=0;turn<4;++turn)
    {
        QVector<double> raw(rotated.size()); for(int i=0;i<raw.size();++i) raw[i]=rotated[i];
        const auto coarse=blur(raw,128,128,6);
        QVector<double> rows,centers;
        for(int row=8;row<120;++row)
        {
            const double y=row/127.0, left=cv[0]*(1-y)+cv[3]*y, right=cv[1]*(1-y)+cv[2]*y;
            const double slope=2*(right-left), ideal=-(2*left-1)/(std::abs(slope)>.001 ? slope:1);
            if(std::abs(slope)<=.35 || ideal<=.06 || ideal>=.94) continue;
            const int lo=qMax(4,int(ideal*127)-30),hi=qMin(124,int(ideal*127)+31);
            int center=lo;
            for(int x=lo+1;x<hi;++x) if(std::abs(coarse[row*128+x]-.5)<std::abs(coarse[row*128+center]-.5)) center=x;
            rows.append(row); centers.append(center);
        }
        if(rows.size()>=28)
        {
            centers=blur(centers,centers.size(),1,8);
            QVector<double> arc(rows.size(),0),nx(rows.size()),ny(rows.size());
            for(int i=0;i<rows.size();++i)
            {
                const int lo=qMax(0,i-1),hi=qMin(rows.size()-1,i+1);
                const double derivative=(centers[hi]-centers[lo])/(rows[hi]-rows[lo]);
                const double y=rows[i]/127, slope=2*((cv[1]-cv[0])*(1-y)+(cv[2]-cv[3])*y);
                const double sign=slope>=0 ? 1:-1,norm=std::sqrt(1+derivative*derivative);
                nx[i]=sign/norm; ny[i]=-sign*derivative/norm;
                if(i) arc[i]=arc[i-1]+std::hypot(centers[i]-centers[i-1],rows[i]-rows[i-1]);
            }
            const int length=int(arc.back())+1; QVector<float> strip(length*65); int index=0;
            double purity=0,far=0;
            for(int row=0;row<length;++row)
            {
                const double at=arc.back()*row/(length-1);
                while(index+1<arc.size()-1 && arc[index+1]<at) ++index;
                const double f=(at-arc[index])/(arc[index+1]-arc[index]);
                const double px=centers[index]*(1-f)+centers[index+1]*f,py=rows[index]*(1-f)+rows[index+1]*f;
                const double normalX=nx[index]*(1-f)+nx[index+1]*f,normalY=ny[index]*(1-f)+ny[index+1]*f;
                for(int c=0;c<65;++c)
                {
                    const int x=qBound(0,int(std::nearbyint(px+normalX*(c-32))),127);
                    const int y=qBound(0,int(std::nearbyint(py+normalY*(c-32))),127);
                    const float value=rotated[y*128+x]; strip[row*65+c]=value;
                    if(c<4) purity+=(value>=.5)/(length*8.0);
                    if(c>=61) purity+=(value<.5)/(length*8.0);
                    if(c>=8 && c<28) far+=(value>=.5)/(length*20.0);
                    if(c>=37 && c<57) far+=(value<.5)/(length*20.0);
                }
            }
            if(purity<bestPurity || (purity==bestPurity && far>bestFar))
            { bestStrip=strip; bestPurity=purity; bestFar=far; }
        }
        QVector<float> next(rotated.size());
        for(int y=0;y<128;++y) for(int x=0;x<128;++x) next[y*128+x]=rotated[x*128+127-y];
        rotated=next; cv={cv[1],cv[2],cv[3],cv[0]};
    }
    if(bestStrip.isEmpty() || bestPurity>=.3) return refuse(QStringLiteral("No trusted coarse contour with pure sides."));
    *output=bestStrip; if(error) error->clear(); return true;
}

int CTerrainRecipeBaker::stableVariant(const STerrainTransitionSignature& signature,
    const QPoint& origin, quint32 materialSeed, int variants)
{
    if(variants<1)return -1;
    qint64 salt=0;for(qint32 material:corners(signature))salt+=material;
    return int(seed(origin.x(),origin.y(),salt+materialSeed)%quint64(variants));
}

STerrainRecipeBakeResult CTerrainRecipeBaker::bake(const STerrainRecipeBakeInput& input,
                                                const STerrainRecipeBakeOptions& options)
{
    STerrainRecipeBakeResult result;
    const qint64 cells=qint64(input.tileWidth)*input.tileHeight;
    const qint64 vertices=(qint64(input.tileWidth)+1)*(qint64(input.tileHeight)+1);
    if(input.tileWidth<=0 || input.tileHeight<=0 || cells>INT_MAX || vertices>INT_MAX
       || input.vertexMaterials.size()!=vertices || options.variants<1 || options.variants>256
       || options.tileSize<2 || options.tileSize>1024 || options.slotLimit<1
       || (!input.preferredMaterials.isEmpty() && input.preferredMaterials.size()!=cells)
       || (!input.preferredRuntimeTypes.isEmpty() && input.preferredRuntimeTypes.size()!=cells))
    {
        result.diagnostics.append(QStringLiteral("terrain.recipe.invalid_input")); return result;
    }
    if(options.preserveReviewedCellRuntimeTypes && input.preferredRuntimeTypes.size()!=cells)
    {
        result.diagnostics.append(QStringLiteral("terrain.recipe.missing_reviewed_cell_classes")); return result;
    }
    QMap<qint32,STerrainRecipeMaterial> materials;
    SPrepared prepared;
    for(const auto& m : input.materials)
    {
        if(m.materialId<=0 || materials.contains(m.materialId) || m.runtimeTerrainType<0
           || m.runtimeTerrainType>15 || m.uniformTiles.isEmpty())
        {
            result.diagnostics.append(QStringLiteral("terrain.recipe.unknown_class_or_source: %1").arg(m.materialId)); return result;
        }
        if(!m.uniformWeights.isEmpty() && m.uniformWeights.size()!=m.uniformTiles.size())
        { result.diagnostics.append(QStringLiteral("terrain.recipe.invalid_uniform_weights")); return result; }
        for(int weight:m.uniformWeights) if(weight<=0)
        { result.diagnostics.append(QStringLiteral("terrain.recipe.invalid_uniform_weights")); return result; }
        for(const QImage& image : m.uniformTiles) if(!opaqueSquare(image))
        {
            result.diagnostics.append(QStringLiteral("terrain.recipe.invalid_uniform: %1").arg(m.materialId)); return result;
        }
        materials.insert(m.materialId,m);
    }
    for(auto it=materials.cbegin();it!=materials.cend();++it)
    {
        if(options.cancelled && options.cancelled()) {
            result.diagnostics.append(QStringLiteral("terrain.recipe.cancelled"));return result;
        }
        prepared.ids.append(it.key()); QVector<QImage> textures;
        // Repeated one-cell calls share immutable source preparation. Image
        // cache keys change with pixels; weights/seed/mode belong to the key.
        static thread_local QCache<QByteArray,QVector<QImage>> sourceCache(64*1024);
        QByteArray sourceKey;QDataStream keyStream(&sourceKey,QIODevice::WriteOnly);
        keyStream<<it.key()<<options.variants<<options.materialSeed<<options.polishedMaterialEdges<<it->uniformWeights;
        for(const auto& image:it->uniformTiles)keyStream<<image.cacheKey();
        if(const auto* cached=sourceCache.object(sourceKey)) {
            prepared.textures.append(*cached);continue;
        }
        if(!options.polishedMaterialEdges)
        {
            for(const QImage& image:it->uniformTiles) {
                const int pad=image.width()/16;
                textures.append(image.copy(pad,pad,image.width()-2*pad,image.height()-2*pad));
            }
            int cost=0;for(const auto& image:textures)cost+=image.width()*image.height()*4/1024+1;
            sourceCache.insert(sourceKey,new QVector<QImage>(textures),qMax(1,cost));
            prepared.textures.append(textures);continue;
        }
        const auto weights=it->uniformWeights.isEmpty()
            ? QVector<int>(it->uniformTiles.size(),1):it->uniformWeights;
        quint64 total=0;for(int weight:weights)total+=quint64(weight);
        QVector<int> selected{0};
        for(int label=0;label<options.variants;++label) {
            quint64 draw=seed(it.key(),label,options.materialSeed)%total;int index=0;
            while(index+1<weights.size() && draw>=quint64(weights[index]))draw-=quint64(weights[index++]);
            selected.append(index);
        }
        QMap<int,QImage> periodicSources;
        for(int index:selected)
        {
            if(periodicSources.contains(index)){textures.append(periodicSources[index]);continue;}
            const QImage& image=it->uniformTiles[index];
            // Symmetric narrow collars make the full-size source periodic.
            // Cropping then wrapping jumped to a different source column at
            // every cell edge, accentuating the repeated first-source bars.
            QImage periodic=image.convertToFormat(QImage::Format_RGBA8888);
            const int n=image.width(), band=qMax(1,n/16);
            for(int y=0;y<n;++y) for(int x=0;x<band;++x)
            {
                const double mix=.5*(1-smooth(double(x)/band));
                const QRgb a=image.pixel(x,y),b=image.pixel(n-1-x,y);
                auto blend=[&](QRgb c,QRgb d) { return qRgb(int(std::nearbyint(qRed(c)*(1-mix)+qRed(d)*mix)),
                    int(std::nearbyint(qGreen(c)*(1-mix)+qGreen(d)*mix)),
                    int(std::nearbyint(qBlue(c)*(1-mix)+qBlue(d)*mix))); };
                periodic.setPixelColor(x,y,QColor::fromRgb(blend(a,b)));
                periodic.setPixelColor(n-1-x,y,QColor::fromRgb(blend(b,a)));
            }
            const QImage horizontal=periodic;
            for(int y=0;y<band;++y) for(int x=0;x<n;++x)
            {
                const double mix=.5*(1-smooth(double(y)/band));
                const QRgb a=horizontal.pixel(x,y),b=horizontal.pixel(x,n-1-y);
                auto blend=[&](QRgb c,QRgb d) { return qRgb(int(std::nearbyint(qRed(c)*(1-mix)+qRed(d)*mix)),
                    int(std::nearbyint(qGreen(c)*(1-mix)+qGreen(d)*mix)),
                    int(std::nearbyint(qBlue(c)*(1-mix)+qBlue(d)*mix))); };
                periodic.setPixelColor(x,y,QColor::fromRgb(blend(a,b)));
                periodic.setPixelColor(x,n-1-y,QColor::fromRgb(blend(b,a)));
            }
            textures.append(periodic);
            periodicSources.insert(index,periodic);
        }
        prepared.textures.append(textures);
        int cost=0;for(const auto& image:textures)cost+=image.width()*image.height()*4/1024+1;
        sourceCache.insert(sourceKey,new QVector<QImage>(textures),qMax(1,cost));
    }
    prepared.pairPhases=QVector<QVector<quint32>>(prepared.ids.size(),QVector<quint32>(prepared.ids.size(),0));
    for(int a=0;a<prepared.ids.size();++a)for(int b=a+1;b<prepared.ids.size();++b)
        prepared.pairPhases[a][b]=seed(prepared.ids[a],prepared.ids[b],0)%512;
    for(const auto& brush : input.brushes)
    {
        const auto key=qMakePair(brush.firstMaterial,brush.secondMaterial);
        if(brush.firstMaterial<=0 || brush.secondMaterial<=brush.firstMaterial
           || !materials.contains(brush.firstMaterial) || !materials.contains(brush.secondMaterial)
           || brush.ownership.size()!=512*65 || !validStrip(brush.ownership) || prepared.brushes.contains(key))
        {
            result.diagnostics.append(QStringLiteral("terrain.recipe.invalid_brush")); return result;
        }
        prepared.brushes.insert(key,&brush);
    }
    QMap<QByteArray,QVector<int>> groups;
    bool invalid=false;
    for(int y=0;y<input.tileHeight;++y) for(int x=0;x<input.tileWidth;++x)
    {
        const int cell=y*input.tileWidth+x, nw=y*(input.tileWidth+1)+x;
        STerrainRecipe r;
        r.signature.northWest=input.vertexMaterials[nw];
        r.signature.northEast=input.vertexMaterials[nw+1];
        r.signature.southEast=input.vertexMaterials[nw+input.tileWidth+2];
        r.signature.southWest=input.vertexMaterials[nw+input.tileWidth+1];
        const auto s=corners(r.signature); QMap<qint32,int> coverage; QSet<qint32> distinct;
        for(qint32 mat : s)
        {
            distinct.insert(mat);
            if(!materials.contains(mat)) { invalid=true; result.diagnostics.append(QStringLiteral("terrain.recipe.missing_material: cell %1 material %2").arg(cell).arg(mat)); }
            else ++coverage[materials[mat].runtimeTerrainType];
        }
        if(distinct.size()>2 && options.diagnosticJunctionPreview)
        {
            r.supported=false; result.unsupportedCells.append(cell);
            result.diagnostics.append(QStringLiteral("terrain.recipe.unsupported_junction: cell %1 materials %2").arg(cell).arg(distinct.size()));
        }
        int maximum=0; QVector<qint32> tied;
        for(auto it=coverage.cbegin();it!=coverage.cend();++it)
        {
            if(it.value()>maximum) { maximum=it.value(); tied={it.key()}; }
            else if(it.value()==maximum) tied.append(it.key());
        }
        if(options.preserveReviewedCellRuntimeTypes)
        {
            const qint32 type=input.preferredRuntimeTypes[cell];
            if(type>=0 && type<16) r.runtimeTerrainType=type;
            else { invalid=true; result.diagnostics.append(QStringLiteral("terrain.recipe.invalid_reviewed_cell_class: cell %1").arg(cell)); }
        }
        else if(tied.size()==1) r.runtimeTerrainType=tied.front();
        else
        {
            qint32 preferred=input.preferredRuntimeTypes.isEmpty() ? -1 : input.preferredRuntimeTypes[cell];
            if(preferred<0 && !input.preferredMaterials.isEmpty() && materials.contains(input.preferredMaterials[cell]))
                preferred=materials[input.preferredMaterials[cell]].runtimeTerrainType;
            if(tied.contains(preferred)) r.runtimeTerrainType=preferred;
            else { invalid=true; result.diagnostics.append(QStringLiteral("terrain.recipe.class_tie: cell %1").arg(cell)); }
        }
        const int ends[4][2]={{0,1},{1,2},{3,2},{0,3}};
        for(int e=0;e<4;++e) { r.edges[e].firstMaterial=s[ends[e][0]]; r.edges[e].secondMaterial=s[ends[e][1]]; }
        if(r.supported || options.diagnosticJunctionPreview)
            for(qint32 a : distinct) for(qint32 b : distinct) if(a<b && !prepared.brushes.contains(qMakePair(a,b)))
            {
                invalid=true; result.diagnostics.append(QStringLiteral("terrain.recipe.missing_pair_brush: cell %1 pair %2,%3").arg(cell).arg(a).arg(b));
            }
        result.recipes.append(r); groups[groupKey(r)].append(cell);
    }
    quint64 recipeCount=0;
    for(auto it=groups.begin();it!=groups.end();++it)
    {
        auto& members=it.value();
        const auto s=corners(result.recipes[members.front()].signature);
        if(options.stableCoordinateVariants)
        {
            QSet<int> labels;
            for(int cell : members)
            {
                const int label=stableVariant(result.recipes[cell].signature,
                    options.origin+QPoint(cell%input.tileWidth,cell/input.tileWidth),options.materialSeed,options.variants);
                result.recipes[cell].variant=label;
                labels.insert(label);
            }
            recipeCount+=quint64(labels.size());
            continue;
        }
        const int n=qMin(members.size(),s[0]==s[1] && s[0]==s[2] && s[0]==s[3] ? qMin(4,options.variants) : options.variants);
        recipeCount+=quint64(n);
        qint64 salt=0; for(qint32 mat : s) salt+=mat;
        std::stable_sort(members.begin(),members.end(),[&](int a,int b) {
            return seed(qint64(options.origin.x())+a%input.tileWidth,qint64(options.origin.y())+a/input.tileWidth,salt)
                < seed(qint64(options.origin.x())+b%input.tileWidth,qint64(options.origin.y())+b/input.tileWidth,salt);
        });
        for(int rank=0;rank<members.size();++rank) result.recipes[members[rank]].variant=rank%n;
    }
    result.budget=budget(recipeCount,recipeCount,options.reservedSlots);
    result.budget.pixelDedupComplete=false;
    result.tileByCell.fill(-1,int(cells));
    auto budgetDiagnostics=[&]() {
        if(result.budget.exceeds2048) result.diagnostics.append(QStringLiteral("terrain.recipe.budget_above_2048"));
        if(result.budget.exceeds4096) result.diagnostics.append(QStringLiteral("terrain.recipe.budget_above_4096"));
        if(result.budget.exceeds8192) result.diagnostics.append(QStringLiteral("terrain.recipe.budget_above_8192"));
        if(result.budget.requiredSlots>options.slotLimit) result.diagnostics.append(QStringLiteral("terrain.recipe.slot_limit_exceeded"));
    };
    if(invalid || (!result.unsupportedCells.isEmpty() && !options.diagnosticJunctionPreview))
    { budgetDiagnostics(); return result; }
    result.previewOnly=!result.unsupportedCells.isEmpty();
    QMap<QByteArray,int> recipeCache, pixelCache;
    for(int cell=0;cell<result.recipes.size();++cell)
    {
        const auto& r=result.recipes[cell];
        const QByteArray key=groupKey(r)+':'+QByteArray::number(r.variant);
        int tile=recipeCache.value(key,-1);
        if(tile<0)
        {
            STerrainRecipeBakedTile baked; baked.pixels=render(prepared,r,options.tileSize,options.diagnosticJunctionPreview,options.polishedMaterialEdges,options.cancelled);
            if(baked.pixels.isNull()) { result.diagnostics.append(options.cancelled&&options.cancelled()
                ?QStringLiteral("terrain.recipe.cancelled"):QStringLiteral("terrain.recipe.allocation_failed")); return result; }
            baked.pixelSha256=pixelHash(baked.pixels); baked.runtimeTerrainType=r.runtimeTerrainType;
            const QByteArray typed=baked.pixelSha256+':'+QByteArray::number(r.runtimeTerrainType);
            tile=pixelCache.value(typed,-1);
            if(tile<0) { tile=result.tiles.size(); result.tiles.append(baked); pixelCache.insert(typed,tile); }
            recipeCache.insert(key,tile);
        }
        result.tileByCell[cell]=tile;
    }
    result.budget=budget(recipeCount,quint64(result.tiles.size()),options.reservedSlots);
    budgetDiagnostics();
    result.success=!result.previewOnly && result.budget.requiredSlots<=options.slotLimit;
    return result;
}

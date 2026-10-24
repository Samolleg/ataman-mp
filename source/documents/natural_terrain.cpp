#include "natural_terrain.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr double pi=3.14159265358979323846;
double smooth(double t){t=qBound(0.0,t,1.0);return t*t*(3-2*t);}
void cancellation(const std::function<bool()>& cancelled){if(cancelled())throw std::runtime_error("Cancelled; live map unchanged");}
quint32 hash(int x,int y,quint32 seed){quint32 n=quint32(x)*0x9e3779b9u^quint32(y)*0x85ebca6bu^seed;n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);}
}
double natural::noise(double x,double y,quint32 seed)
{
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const double u=smooth(x-ix),v=smooth(y-iy);
    const auto value=[&](int a,int b){return double(hash(a,b,seed)&0xffffff)/8388607.5-1;};
    return (1-v)*((1-u)*value(ix,iy)+u*value(ix+1,iy))+v*((1-u)*value(ix,iy+1)+u*value(ix+1,iy+1));
}
double natural::fbm(double x,double y,quint32 seed,int octaves)
{
    double sum=0,norm=0,amplitude=1;
    for(int i=0;i<octaves;++i){sum+=noise(x,y,seed+quint32(i)*7919)*amplitude;norm+=amplitude;
        // Rotation keeps lattice directions from lining up across octaves.
        const double a=x;x=1.61*x+1.19*y+13.7;y=-1.19*a+1.61*y+7.3;amplitude*=0.48;}
    return sum/norm;
}
QJsonObject natural::preset(const QString& name)
{
    // Calibrated world-scale rules, not copies of a reference height field.
    double base=4,amplitude=22,scale=55,ridge=0.1,warp=30,terrace=0.85,detail=4.5;
    if(name=="hills"){base=2;amplitude=42;scale=70;ridge=0.35;warp=32;terrace=0.55;}
    else if(name=="mountains"){base=3;amplitude=35;scale=85;ridge=0.82;warp=40;terrace=0.7;detail=28;}
    else if(name=="coast"){base=4;amplitude=20;scale=90;ridge=0.18;warp=32;terrace=0.65;detail=4.6;}
    else if(name=="swamp"){base=3;amplitude=8;scale=35;ridge=0;warp=24;terrace=0.75;}
    else if(name=="river_valley"){base=4;amplitude=32;scale=75;ridge=0.25;warp=32;terrace=0.7;}
    else if(name!="plain")throw std::runtime_error("Unknown preset: plain/hills/mountains/coast/swamp/river_valley");
    return {{"baseHeight",base},{"amplitude",amplitude},{"scale",scale},{"ridge",ridge},{"warp",warp},{"terrace",terrace},{"detail",detail},
        {"thermalIterations",2},{"hydraulicIterations",16},{"talus",0.65},{"rain",0.04},
        {"erosion",0.12},{"waterLevel",name=="coast"||name=="swamp"?6:3},{"rockSlope",32},{"rockFraction",name=="mountains"?0.19:name=="coast"?0.10:0.09},
        {"patchScale",5},{"shoreWidth",1.5},{"boundaryNoise",1.5}};
}
double natural::Grid::sample(double x,double y) const
{
    x=qBound(0.0,x-origin.x(),double(width-1));y=qBound(0.0,y-origin.y(),double(height-1));
    const int ix=int(x),iy=int(y),jx=qMin(width-1,ix+1),jy=qMin(height-1,iy+1);const double u=x-ix,v=y-iy;
    return (1-v)*((1-u)*heights[iy*width+ix]+u*heights[iy*width+jx])+v*((1-u)*heights[jy*width+ix]+u*heights[jy*width+jx]);
}
void natural::generate(Grid& g,const QString& name,const QJsonObject& p,quint32 seed,const std::function<bool()>& cancelled)
{
    const double scale=p["scale"].toDouble(),warp=p["warp"].toDouble(),amplitude=p["amplitude"].toDouble();
    g.heights.resize(g.width*g.height);g.wetness.fill(0,g.heights.size());
    for(int y=0;y<g.height;++y){cancellation(cancelled);for(int x=0;x<g.width;++x){
        const double wx=x+g.origin.x(),wy=y+g.origin.y();
        const double u=(wx+warp*fbm(wx/scale*0.7,wy/scale*0.7,seed^9137,3))/scale;
        const double v=(wy+warp*fbm(wx/scale*0.7+23,wy/scale*0.7-19,seed^5813,3))/scale;
        const double broad=fbm(u,v,seed),r=1-std::abs(fbm(u*0.85+41,v*0.85-11,seed^3741,4));
        const double ridge=p["ridge"].toDouble();
        double h=p["baseHeight"].toDouble()+amplitude*((1-ridge)*(broad*0.7+0.35)+ridge*std::pow(r,5));
        if(name=="coast"){
            const double coast=g.origin.x()+g.width*0.58+g.width*0.22*fbm(wy/65,12,seed^7123,4);
            // Curving bay, a broad shelf, then a steeper inland escarpment.
            const double bay=g.width*0.18*std::exp(-std::pow((wy-g.origin.y()-g.height*0.55)/(g.height*0.22),2));
            const double inland=wx-coast-bay;
            h=p["waterLevel"].toDouble()+inland*0.065+amplitude*0.25*broad*smooth((inland+8)/45);
            h+=2.5*fbm(wx/23,wy/23,seed^8107,4)*smooth((inland+5)/15);
            h+=4*smooth((inland-18+8*noise(wy/45,10,seed^1711))/8);
            h+=5*smooth((fbm(wx/24,wy/35,seed^4133,3)-0.12)/0.12)*smooth(inland/16);
            // Independent shoals intersect the sea plane to form coves,
            // peninsulas and small irregular islands, not a displaced straight rim.
            const double shoal=p["waterLevel"].toDouble()-2.2+12*fbm(wx/17,wy/25,seed^7753,5);
            if(inland<12)h=qMax(h,shoal);
        }else if(name=="river_valley"){
            const double centre=g.origin.x()+g.width*0.5+22*fbm(wy/95,7,seed^1987,3);
            h=p["waterLevel"].toDouble()+1+(h-p["waterLevel"].toDouble())*smooth(std::abs(wx-centre)/55);
        }
        // EI references have broad walkable shelves separated by irregular
        // steep outcrops. Continuous warped terraces preserve both populations.
        const double step=qMax(2.0,amplitude*0.32),floor=std::floor(h/step),fraction=h/step-floor;
        const double shelf=step*(floor+smooth((fraction-0.39)/0.22));
        h+=(shelf-h)*p["terrace"].toDouble(0);
        const double dx=wx+3*noise(wx/19,wy/21,seed^3151),dy=wy+3*noise(wx/21,wy/19,seed^8191);
        const double fine=0.62*noise(dx/2.4,dy/3.3,seed^8273)+(name=="mountains"?0.58:0.32)*noise(dx/1.05,dy/1.3,seed^5297)
            +0.45*(std::pow(1-std::abs(noise(dx/6.5,dy/9,seed^7219)),4)-0.4);
        const double detailGate=smooth((noise(dx/10.5,dy/15.5,seed^1791)-(name=="mountains"?0.38:0.18))/0.10);
        h+=p["detail"].toDouble(2.4)*fine*detailGate;
        if(name=="mountains"){const double cap=p["baseHeight"].toDouble()+amplitude*1.1;if(h>cap)h=cap+(h-cap)*0.35;}
        // Optional cliff band in world X, with a warped trace and rounded lip.
        if(p.contains("cliffX"))h+=p["cliffHeight"].toDouble(12)*smooth((wx-p["cliffX"].toDouble()+8*noise(wy/40,3,seed))/p["cliffWidth"].toDouble(2));
        g.heights[y*g.width+x]=qMax(0.0,h);
    }}
    erode(g,p,cancelled);
    // Narrow connected incision channels follow warped ridged contours. Their
    // strength is tied to hydraulic work and to the local fall of the surface.
    const auto before=g.heights;
    for(int y=1;y<g.height-1;++y){cancellation(cancelled);for(int x=1;x<g.width-1;++x){const int i=y*g.width+x;
        const double slope=std::hypot((before[i+1]-before[i-1])/2,(before[i+g.width]-before[i-g.width])/2);
        const double wx=x+g.origin.x(),wy=y+g.origin.y();
        const double channel=std::exp(-std::pow(noise(wx/9+2*fbm(wx/37,wy/37,seed^933,3),wy/12,seed^3821)/0.05,2));
        const double incision=qMin(12.0,p["detail"].toDouble())*0.9*smooth((slope-0.2)/0.9)*channel
            *smooth((g.wetness[i]-0.22)/0.25)*qMin(1.0,p["hydraulicIterations"].toDouble()/16);
        g.heights[i]=qMax(0.0,before[i]-incision);g.wetness[i]=qMax(g.wetness[i],channel*0.6);
    }}
}
void natural::erode(Grid& g,const QJsonObject& p,const std::function<bool()>& cancelled)
{
    const int n=g.heights.size(),w=g.width,h=g.height;
    const int thermal=p["thermalIterations"].toInt(8),hydraulic=p["hydraulicIterations"].toInt(16);
    QVector<double> delta(n),water(n,0),sediment(n,0),dw(n),ds(n);
    g.wetness.fill(0,n);
    const int steps[]={-1,1,-w,w};
    for(int iteration=0;iteration<thermal;++iteration){cancellation(cancelled);delta.fill(0);
        for(int y=1;y<h-1;++y)for(int x=1;x<w-1;++x){const int i=y*w+x;double excess[4],total=0;
            for(int d=0;d<4;++d){excess[d]=qMax(0.0,g.heights[i]-g.heights[i+steps[d]]-p["talus"].toDouble(0.65));total+=excess[d];}
            if(total>0){const double moved=*std::max_element(excess,excess+4)*0.18;
                for(int d=0;d<4;++d){const double a=moved*excess[d]/total;delta[i]-=a;delta[i+steps[d]]+=a;}}}
        for(int i=0;i<n;++i)g.heights[i]+=delta[i];
    }
    // Bounded shallow-water routing. Transported sediment is deposited when
    // capacity falls; all transfers use previous-iteration values (no scan bias).
    for(int iteration=0;iteration<hydraulic;++iteration){cancellation(cancelled);dw.fill(0);ds.fill(0);delta.fill(0);
        for(int i=0;i<n;++i)water[i]+=p["rain"].toDouble(0.04);
        for(int y=1;y<h-1;++y)for(int x=1;x<w-1;++x){const int i=y*w+x;double drops[4],total=0;
            for(int d=0;d<4;++d){const int j=i+steps[d];drops[d]=qMax(0.0,g.heights[i]+water[i]-g.heights[j]-water[j]);total+=drops[d];}
            if(total<=0)continue;
            const double flow=qMin(water[i]*0.5,total*0.2),capacity=flow*qMin(3.0,total)*0.8;
            const double change=(capacity>sediment[i])?qMin(g.heights[i],(capacity-sediment[i])*p["erosion"].toDouble(0.12)):(capacity-sediment[i])*0.3;
            delta[i]-=change;ds[i]+=change;g.wetness[i]+=flow;
            const double carried=sediment[i]*flow/qMax(1e-8,water[i]);
            for(int d=0;d<4;++d){const double ratio=drops[d]/total;const int j=i+steps[d];dw[j]+=flow*ratio;dw[i]-=flow*ratio;ds[j]+=carried*ratio;ds[i]-=carried*ratio;}
        }
        for(int i=0;i<n;++i){g.heights[i]=qMax(0.0,g.heights[i]+delta[i]);water[i]=qMax(0.0,(water[i]+dw[i])*0.88);sediment[i]=qMax(0.0,sediment[i]+ds[i]);}
    }
    for(int i=0;i<n;++i){g.heights[i]+=sediment[i];g.wetness[i]=qBound(0.0,g.wetness[i]/qMax(1,hydraulic),1.0);}
    if(hydraulic>0){
        QVector<int> order(n);QVector<double> catchment(n,1);for(int i=0;i<n;++i)order[i]=i;
        std::sort(order.begin(),order.end(),[&](int a,int b){return g.heights[a]==g.heights[b]?a<b:g.heights[a]>g.heights[b];});
        delta.fill(0);int checked=0;
        for(int i:order){if((++checked&4095)==0)cancellation(cancelled);const int x=i%w,y=i/w;int next=-1;double fall=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if((dx==0&&dy==0)||x+dx<0||y+dy<0||x+dx>=w||y+dy>=h)continue;
                const int j=(y+dy)*w+x+dx;const double slope=(g.heights[i]-g.heights[j])/(dx&&dy?1.4142135623730951:1);
                if(slope>fall){fall=slope;next=j;}}
            if(next>=0){catchment[next]+=catchment[i];const double amount=qMin(g.heights[i]*0.12,
                    0.12*hydraulic*p["erosion"].toDouble(0.12)*qMax(0.0,std::sqrt(catchment[i])-1.4)*qMin(1.0,fall));
                delta[i]-=amount;delta[next]+=amount*0.65;g.wetness[i]=qMax(g.wetness[i],qMin(1.0,std::log1p(catchment[i])/6));}
        }
        for(int i=0;i<n;++i)g.heights[i]=qMax(0.0,g.heights[i]+delta[i]);
    }
}
void natural::river(Grid& g,const QJsonObject& args,quint32 seed,const std::function<bool()>& cancelled)
{
    const auto points=args["points"].toArray();if(points.size()<2)throw std::runtime_error("River requires at least two points");
    QVector<QPointF> knots;for(const auto& point:points){auto a=point.toArray();knots.append({a[0].toDouble(),a[1].toDouble()});}
    QVector<QPointF> line;
    for(int k=0;k<knots.size()-1;++k){const auto a=knots[qMax(0,k-1)],b=knots[k],c=knots[k+1],d=knots[qMin(knots.size()-1,k+2)];
        for(int step=0;step<4;++step){const double t=step/4.0,t2=t*t,t3=t2*t;
            line.append((b*2+(c-a)*t+(a*2-b*5+c*4-d)*t2+(-a+b*3-c*3+d)*t3)*0.5);}}
    line.append(knots.last());
    const double half=args["width"].toDouble(8)/2,depth=args["depth"].toDouble(3),flood=qMax(args["floodplain"].toDouble(half*3),depth*4+8);
    // A monotone thalweg follows the specified direction downstream.
    QVector<double> beds;double bed=args["level"].toDouble(g.sample(line[0].x(),line[0].y())-depth);
    for(const auto& point:line){bed=qMin(bed,g.sample(point.x(),point.y())-depth);beds.append(qMax(0.0,bed));}
    for(int y=0;y<g.height;++y){cancellation(cancelled);for(int x=0;x<g.width;++x){
        const double wx=x+g.origin.x(),wy=y+g.origin.y();double distance=1e20,target=0;
        for(int k=1;k<line.size();++k){const auto a=line[k-1],d=line[k]-a;const double len=d.x()*d.x()+d.y()*d.y();
            const double t=len>0?qBound(0.0,((wx-a.x())*d.x()+(wy-a.y())*d.y())/len,1.0):0;
            const double dist=std::hypot(wx-a.x()-d.x()*t,wy-a.y()-d.y()*t);
            if(dist<distance){distance=dist;target=beds[k-1]*(1-t)+beds[k]*t;}}
        const double width=half*(1+0.22*fbm(wx/21,wy/21,seed^631,3));
        const double d=qMax(0.0,distance+width*0.23*noise(wx/13,wy/13,seed));const int i=y*g.width+x;
        if(d<width+flood){const double channel=smooth(d/width),bank=smooth((d-width)/qMax(1.0,flood));
            const double plain=target+depth*(0.9+0.15*bank);
            const double blend=smooth((d-width-flood*0.35)/(flood*0.65));
            const double carved=d<width?target+depth*0.75*channel*channel:plain+(g.heights[i]-plain)*blend;
            g.heights[i]=qMin(g.heights[i],qMax(0.0,carved));g.wetness[i]=qMax(g.wetness.value(i),1-smooth(d/(width+flood)));}
    }}
}
QJsonObject natural::metrics(const Grid& g)
{
    QVector<double> heights=g.heights,slopes;double sum=0;int steep=0;
    for(double a:heights)sum+=a;
    for(int y=1;y<g.height-1;++y)for(int x=1;x<g.width-1;++x){const int i=y*g.width+x;
        const double s=std::atan(std::hypot((heights[i+1]-heights[i-1])/2,(heights[i+g.width]-heights[i-g.width])/2))*180/pi;slopes.append(s);steep+=s>=32;}
    std::sort(heights.begin(),heights.end());std::sort(slopes.begin(),slopes.end());
    const auto percentile=[](const QVector<double>& values,double p){return values.isEmpty()?0:values[int((values.size()-1)*p)];};
    QJsonObject scales;
    for(int step:{2,4,8,16,32}){double difference=0,bend=0;int pairs=0,centres=0;
        for(int y=0;y<g.height;++y)for(int x=0;x<g.width;++x){const int i=y*g.width+x;
            if(x+step<g.width){const double d=g.heights[i+step]-g.heights[i];difference+=d*d;++pairs;}
            if(y+step<g.height){const double d=g.heights[i+step*g.width]-g.heights[i];difference+=d*d;++pairs;}
            if(x>=step&&y>=step&&x+step<g.width&&y+step<g.height){
                const double d=g.heights[i]-(g.heights[i-step]+g.heights[i+step]+g.heights[i-step*g.width]+g.heights[i+step*g.width])/4;
                bend+=d*d;++centres;}}
        const double energy=difference/qMax(1,pairs);
        scales[QString::number(step)]=QJsonObject{{"heightDifferenceEnergy",energy},{"rmsSlope",std::sqrt(energy)/step},{"highPassEnergy",bend/qMax(1,centres)}};
    }
    return {{"scalesMetres",scales},{"min",heights.first()},{"max",heights.last()},{"mean",sum/heights.size()},
        {"heightP10",percentile(heights,0.1)},{"heightP90",percentile(heights,0.9)},
        {"slopeP50",percentile(slopes,0.5)},{"slopeP90",percentile(slopes,0.9)},{"slopeP99",percentile(slopes,0.99)},
        {"steepFraction",double(steep)/qMax(1,slopes.size())}};
}

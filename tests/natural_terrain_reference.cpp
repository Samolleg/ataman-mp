// Read-only reference measurement. Output contains numbers/rules, never heights.
#include <QtCore>
#include <QtEndian>
#include <cmath>
#include <algorithm>
#include "natural_terrain.h"
namespace {
quint32 u32(const QByteArray& b,int p){return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(b.constData()+p));}
quint16 u16(const QByteArray& b,int p){return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+p));}
float f32(const QByteArray& b,int p){auto u=u32(b,p);float v;memcpy(&v,&u,4);return v;}
QByteArray read(QString path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read input");return f.readAll();}
QJsonObject distribution(QVector<double> a){if(a.isEmpty())return {};std::sort(a.begin(),a.end());double sum=0;for(double v:a)sum+=v;
    return {{"count",a.size()},{"mean",sum/a.size()},{"p10",a[int((a.size()-1)*0.1)]},{"p50",a[int((a.size()-1)*0.5)]},{"p90",a[int((a.size()-1)*0.9)]},{"max",a.last()}};}
QJsonObject measure(QString path){
    const auto bytes=read(path);if(bytes.size()<16||u32(bytes,0)!=0x019ce23c)throw std::runtime_error("Invalid MPR");
    const int count=u32(bytes,4),table=u32(bytes,8),names=table+count*22;QMap<QString,QByteArray> members;
    for(int i=0;i<count;++i){int p=table+i*22;members[QString::fromLocal8Bit(bytes.mid(names+u32(bytes,p+18),u16(bytes,p+16)))]=bytes.mid(u32(bytes,p+8),u32(bytes,p+4));}
    QByteArray mp;QString ns;for(auto it=members.begin();it!=members.end();++it)if(it.key().endsWith(".mp",Qt::CaseInsensitive)){mp=it.value();ns=it.key().chopped(3);}
    if(mp.size()<38)throw std::runtime_error("Missing MP");
    natural::Grid g;g.width=int(u32(mp,8))*32+1;g.height=int(u32(mp,12))*32+1;const double maxZ=f32(mp,4);
    g.heights.fill(0,g.width*g.height);const int tw=(g.width-1)/2,th=(g.height-1)/2;QVector<int> tiles(tw*th),water(tw*th,0);double waterSum=0;int waterCount=0;
    for(auto it=members.begin();it!=members.end();++it){auto match=QRegularExpression("(\\d{3})(\\d{3})\\.sec$",QRegularExpression::CaseInsensitiveOption).match(it.key());if(!match.hasMatch())continue;
        const int sx=match.captured(1).toInt(),sy=match.captured(2).toInt();const auto b=it.value();const bool overlay=quint8(b[4])==3;
        for(int y=0;y<33;++y)for(int x=0;x<33;++x)g.heights[(sy*32+y)*g.width+sx*32+x]=u16(b,7+(y*33+x)*8)*maxZ/65535;
        const int offset=5+1089*8*(overlay?2:1);
        for(int y=0;y<16;++y)for(int x=0;x<16;++x){const int local=y*16+x,i=(sy*16+y)*tw+sx*16+x;tiles[i]=u16(b,offset+local*2)&0x3fff;
            if(overlay&&qint16(u16(b,offset+1024+local*2))!=-1){water[i]=1;waterSum+=u16(b,5+1089*8+((y*2)*33+x*2)*8+2)*maxZ/65535;++waterCount;}}
    }
    auto result=natural::metrics(g);result["map"]=QFileInfo(path).fileName();result["sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    result["maxZ"]=maxZ;result["sectorsX"]=(g.width-1)/32;result["sectorsY"]=(g.height-1)/32;result["overlayFraction"]=double(waterCount)/(tw*th);if(waterCount)result["meanOverlayHeight"]=waterSum/waterCount;
    int boundary=0;for(int y=0;y<th;++y)for(int x=0;x<tw;++x){const int i=y*tw+x;if(x+1<tw)boundary+=water[i]!=water[i+1];if(y+1<th)boundary+=water[i]!=water[i+tw];}result["overlayBoundaryMetres"]=boundary*2;
    // Horizontal/vertical runs above P75 and below P25: reproducible proxies
    // for feature width/length, not a claim of hand-labelled ridge centrelines.
    auto sorted=g.heights;std::sort(sorted.begin(),sorted.end());QJsonObject runs;
    for(int high=0;high<2;++high){QVector<double> lengths;const double threshold=sorted[int((sorted.size()-1)*(high?0.75:0.25))];
        for(int vertical=0;vertical<2;++vertical)for(int a=0;a<(vertical?g.width:g.height);++a){int length=0;
            for(int b=0;b<=(vertical?g.height:g.width);++b){const bool inside=b<(vertical?g.height:g.width);
                const double z=inside?g.heights[vertical?b*g.width+a:a*g.width+b]:0;
                if(inside&&(high?z>threshold:z<threshold))++length;else{if(length>=3)lengths.append(length);length=0;}}}
        runs[high?"highP75RunsMetres":"lowP25RunsMetres"]=distribution(lengths);}
    result["featureRunProxies"]=runs;
    QJsonObject components;
    for(int high=0;high<2;++high){QVector<bool> seen(g.heights.size(),false);QVector<double> lengths,widths;const double threshold=sorted[int((sorted.size()-1)*(high?0.75:0.25))];
        const auto qualifies=[&](int i){return high?g.heights[i]>threshold:g.heights[i]<threshold;};
        for(int i=0;i<g.heights.size();++i){if(seen[i]||!qualifies(i))continue;QVector<int> queue{i};seen[i]=true;
            double sx=0,sy=0,sxx=0,syy=0,sxy=0;
            for(int k=0;k<queue.size();++k){const int a=queue[k],x=a%g.width,y=a/g.width;sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;
                for(int d:{-1,1,-g.width,g.width}){int b=a+d;if(b<0||b>=g.heights.size()||std::abs(b%g.width-x)+std::abs(b/g.width-y)!=1||seen[b]||!qualifies(b))continue;seen[b]=true;queue.append(b);}}
            if(queue.size()<16)continue;const double n=queue.size(),a=sxx/n-sx*sx/n/n,b=syy/n-sy*sy/n/n,c=sxy/n-sx*sy/n/n;
            const double disc=std::sqrt((a-b)*(a-b)+4*c*c);lengths.append(std::sqrt(qMax(0.0,(a+b+disc)/2)*12));widths.append(std::sqrt(qMax(0.0,(a+b-disc)/2)*12));}
        components[high?"highP75":"lowP25"]=QJsonObject{{"principalLengthMetres",distribution(lengths)},{"principalWidthMetres",distribution(widths)}};
    }result["thresholdComponents"]=components;
    // Slot-based patch size/edge density remain exact even when no reviewed
    // semantic binding exists. Do not infer grass/stone from runtime type0.
    QVector<bool> visited(tiles.size(),false);QVector<double> patches;int edges=0;
    for(int i=0;i<tiles.size();++i){if(i%tw+1<tw)edges+=tiles[i]!=tiles[i+1];if(i+tw<tiles.size())edges+=tiles[i]!=tiles[i+tw];
        if(visited[i])continue;QVector<int> queue{i};visited[i]=true;
        for(int n=0;n<queue.size();++n){int a=queue[n];for(int d:{-1,1,-tw,tw}){int b=a+d;if(b<0||b>=tiles.size()||std::abs(b%tw-a%tw)+std::abs(b/tw-a/tw)!=1||visited[b]||tiles[b]!=tiles[i])continue;visited[b]=true;queue.append(b);}}
        patches.append(queue.size()*4.0);}
    result["physicalSlotPatchSquareMetres"]=distribution(patches);result["physicalSlotEdgeFraction"]=double(edges)/qMax(1,2*tw*th-tw-th);
    return result;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{QJsonArray measurements;
    if(app.arguments().contains("--profiles"))for(const auto& entry:QJsonDocument::fromJson(read(app.arguments().value(3))).array()){
        const auto o=entry.toObject();const auto name=o["preset"].toString();auto p=natural::preset(name);const auto overrides=o["parameters"].toObject();for(auto i=overrides.begin();i!=overrides.end();++i)p[i.key()]=i.value();
        natural::Grid g;g.width=g.height=257;natural::generate(g,name,p,o["seed"].toInt(221),[](){return false;});auto m=natural::metrics(g);m["parameters"]=p;m["preset"]=name;measurements.append(m);
    }else if(app.arguments().contains("--generate"))for(const auto& name:{"plain","hills","mountains","coast","swamp","river_valley"}){
        natural::Grid g;g.width=g.height=257;QElapsedTimer timer;timer.start();natural::generate(g,name,natural::preset(name),QString(name)=="mountains"?221:QString(name)=="coast"?222:220,[](){return false;});
        auto m=natural::metrics(g);m["preset"]=name;m["parameters"]=natural::preset(name);m["computeMs"]=int(timer.elapsed());measurements.append(m);
    }else for(int i=2;i<argc;++i)measurements.append(measure(QString::fromLocal8Bit(argv[i])));
    QFile output(QString::fromLocal8Bit(argv[1]));if(!output.open(QIODevice::WriteOnly))return 2;output.write(QJsonDocument(measurements).toJson());return 0;
}catch(const std::exception& e){qCritical()<<e.what();return 1;}}

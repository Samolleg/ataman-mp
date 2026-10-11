#include "../source/documents/terrain_hd_variants.h"
#include "../source/resources/mmp_codec.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>
#include <QTransform>
#include <stdexcept>
#include <algorithm>

using namespace TerrainHdVariants;
namespace {
int passes=0;quint64 blockComparisons=0,edgeComparisons=0;
void require(bool condition,const QString& message){if(!condition)throw std::runtime_error(message.toStdString());++passes;}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error(("read "+path).toStdString());return f.readAll();}
void write(const QString& path,const QByteArray& bytes){QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"write "+path);}
QVector<QByteArray> blocks(const QImage& image){auto mmp=ei::encodeMmpDxt1(image,true,nullptr);QVector<QByteArray> result;int offset=76;for(int n=256;n>=4;n/=2){result.append(mmp.mid(offset,n*n/2));offset+=n*n/2;}return result;}
QImage decode(const QByteArray& bytes,int n){QImage image;QString error;require(ei::decodeDxtImage(bytes,n,n,ei::DxtFormat::Dxt1,&image,&error),error);return image;}
void verifyEdges(const QVector<Source>& sources,const Result& generated){
    QVector<QVector<QImage>> actual,anchors;
    if(!generated.variants.isEmpty())for(const auto& source:sources) {
        const auto levels=source.blocks.isEmpty()?blocks(source.pixels):source.blocks;
        QVector<QImage> mip;for(int i=0,n=256;i<7;++i,n/=2)mip.append(decode(levels[i],n));
        actual.append(mip);anchors.append(mip);
    }
    for(const auto& v:generated.variants){
        auto it=std::find_if(sources.begin(),sources.end(),[&](const Source& s){return s.id==v.anchor;});require(it!=sources.end(),"known anchor");
        auto anchorBlocks=it->blocks.isEmpty()?blocks(it->pixels):it->blocks;
        QVector<QImage> a,b;
        for(int level=0,n=256;level<7;++level,n/=2){
            int rim=qMax(4,((24*n+255)/256+3)/4*4);
            for(int y=0;y<n/4;++y)for(int x=0;x<n/4;++x)if(x<rim/4||y<rim/4||x>=n/4-rim/4||y>=n/4-rim/4){
                int offset=(y*(n/4)+x)*8;require(v.blocks[level].mid(offset,8)==anchorBlocks[level].mid(offset,8),"native BC1 rim block exact");++blockComparisons;}
            a.append(decode(v.blocks[level],n));b.append(decode(anchorBlocks[level],n));
        }actual.append(a);anchors.append(b);
    }
    // Every ordered generated pair / independent rotations / both axes / all
    // seven mips, compared to precisely the corresponding original pair.
    for(int i=0;i<actual.size();++i)for(int j=0;j<actual.size();++j)for(int r=0;r<4;++r)for(int s=0;s<4;++s)
        for(int level=0,n=256;level<7;++level,n/=2){
            auto a=actual[i][level].transformed(QTransform().rotate(90*r)),b=actual[j][level].transformed(QTransform().rotate(90*s));
            auto x=anchors[i][level].transformed(QTransform().rotate(90*r)),y=anchors[j][level].transformed(QTransform().rotate(90*s));
            const int inset=n/16;
            for(int p=0;p<n;++p){require(a.pixel(n-1-inset,p)==x.pixel(n-1-inset,p)&&b.pixel(inset,p)==y.pixel(inset,p),"horizontal pair anchor footprint");
                require(a.pixel(p,n-1-inset)==x.pixel(p,n-1-inset)&&b.pixel(p,inset)==y.pixel(p,inset),"vertical pair anchor footprint");edgeComparisons+=2;}
        }
}
void synthetic(){
    Source source;source.id="synthetic-noise";source.pixels=QImage(256,256,QImage::Format_ARGB32);quint32 rng=143;
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;int v=32+int(rng%192);source.pixels.setPixel(x,y,qRgb(v,v,v));}
    source.blocks=blocks(source.pixels);source.pixels=decode(source.blocks[0],256);
    const QVector<Source> sources{source};
    auto a=generate("synthetic|0|0|land",sources),b=generate("synthetic|0|0|land",sources);
    require(a.success,a.error);require(b.success,b.error);require(a.variants.size()==5,"one original plus five new");
    for(int i=0;i<a.variants.size();++i){require(a.variants[i].pixels==b.variants[i].pixels,"deterministic pixels");
        require(a.variants[i].blocks==b.variants[i].blocks,"deterministic all BC1 mip bytes");
        require(QJsonDocument(a.variants[i].metadata).toJson()==QJsonDocument(b.variants[i].metadata).toJson(),"deterministic metadata");}
    verifyEdges(sources,a);
    auto rejected=measure(source.pixels,source.pixels,{source.pixels});require(!rejectReasons(rejected).isEmpty(),"duplicate rejected");
    auto good=a.variants[0].metadata["metrics"].toObject();
    for(const auto& field:QStringList{"rimDelta","lumaDrift","stdRatio","lineRatio"}){auto bad=good;bad[field]=99;require(!rejectReasons(bad).isEmpty(),"reject "+field);}
    auto bad=good;bad["minC4Rmse64"]=0;require(!rejectReasons(bad).isEmpty(),"C4 duplicate rejected");
    auto cancelled=generate("cancel",sources,[](int,int){return false;});require(cancelled.cancelled&&!cancelled.success,"cancel before generation");
    int calls=0;auto midway=generate("cancel",sources,[&](int,int){return ++calls<8;});
    require(midway.cancelled&&!midway.success&&midway.variants.isEmpty(),"cancel during quilting");
    require(!generate("empty",{}).success,"missing HD source fails closed");
    Source rotated=source;rotated.id="rotated";rotated.pixels=source.pixels.transformed(QTransform().rotate(90));
    auto dedup=generate("cancel",{source,rotated},[](int,int){return false;});require(dedup.originalCount==1,"D4 source dedup");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTextStream out(stdout);QElapsedTimer timer;timer.start();
    QJsonArray rows;QString output;int generatedCount=0,rejectedCount=0;
    try{
        const auto args=app.arguments();
        if(args.size()==1){synthetic();out<<"PASS synthetic "<<passes<<" assertions, "<<blockComparisons<<" blocks, "<<edgeComparisons<<" edge pixels, "<<timer.elapsed()<<" ms\n";return 0;}
        if(args.size()==4&&args[1]=="--verify-saved") {
            const auto input=QJsonDocument::fromJson(read(args[2])).object()["groups"].toArray();
            const QDir root=QFileInfo(args[2]).absoluteDir();const auto rows=QJsonDocument::fromJson(read(args[3]+"/results.json")).object()["groups"].toArray();
            require(input.size()==rows.size(),"complete saved corpus");
            for(int i=0;i<input.size();++i) {
                QVector<Source> sources;for(const auto& value:input[i].toObject()["sources"].toArray()) {
                    auto o=value.toObject();Source s;s.id=o["id"].toString();s.pixels=QImage(root.filePath(o["png"].toString()));
                    for(const auto& b:o["blocks"].toArray())s.blocks.append(read(root.filePath(b.toString())));sources.append(s);
                }
                Result result;for(const auto& value:rows[i].toObject()["variants"].toArray()) {
                    auto o=value.toObject();Variant v;v.id=o["id"].toString();v.anchor=o["anchor"].toString();
                    const auto bytes=read(args[3]+"/"+QString::number(i).rightJustified(3,'0')+"/"+v.id+".bc1");int offset=0;
                    for(int n=256;n>=4;n/=2){v.blocks.append(bytes.mid(offset,n*n/2));offset+=n*n/2;}require(offset==bytes.size(),"saved mip chain size");result.variants.append(v);
                }
                verifyEdges(sources,result);
            }
            write(args[3]+"/verification-all-pairs.json",QJsonDocument(QJsonObject{{"pass",passes},{"fail",0},{"blockComparisons",double(blockComparisons)},{"edgeComparisons",double(edgeComparisons)}}).toJson());return 0;
        }
        require(args.size()==3,"Usage: tests input.json output-directory");output=args[2];require(!QFileInfo::exists(output),"new owned output directory");require(QDir().mkpath(output),"create output");
        const auto input=QJsonDocument::fromJson(read(args[1])).object();const QDir root=QFileInfo(args[1]).absoluteDir();
        for(const auto& item:input["groups"].toArray()){
            const auto group=item.toObject();QVector<Source> sources;
            for(const auto& record:group["sources"].toArray()){auto object=record.toObject();Source s;s.id=object["id"].toString();s.pixels=QImage(root.filePath(object["png"].toString()));
                for(const auto& level:object["blocks"].toArray())s.blocks.append(read(root.filePath(level.toString())));
                sources.append(s);}
            QString key=group["key"].toString();QElapsedTimer groupTime;groupTime.start();auto result=generate(key,sources);
            QString name=QString::number(rows.size()).rightJustified(3,'0');require(QDir().mkpath(output+"/"+name),"create group directory");
            QJsonArray variants;for(const auto& v:result.variants){require(v.pixels.save(output+"/"+name+"/"+v.id+".png"),"save delivered PNG");
                QByteArray payload;for(const auto& b:v.blocks)payload+=b;write(output+"/"+name+"/"+v.id+".bc1",payload);variants.append(v.metadata);}
            QJsonObject row{{"key",key},{"success",result.success},{"error",result.error},{"originalCount",result.originalCount},{"generated",result.variants.size()},
                {"shortfall",result.shortfall},{"shortfallReason",result.shortfallReason},
                {"rejected",result.rejected},{"seconds",groupTime.elapsed()/1000.},{"variants",variants},{"rejections",result.rejections}};
            rows.append(row);generatedCount+=result.variants.size();rejectedCount+=result.rejected;
            write(output+"/results.json",QJsonDocument(QJsonObject{{"groups",rows},{"generated",generatedCount},{"rejected",rejectedCount},{"seconds",timer.elapsed()/1000.}}).toJson());
            out<<(result.success?"PASS ":"FAIL ")<<key<<" originals="<<result.originalCount<<" generated="<<result.variants.size()<<" rejected="<<result.rejected<<" seconds="<<groupTime.elapsed()/1000.<<endl;
            require(result.success,result.error);
            auto replay=generate(key,sources);require(replay.success,replay.error);require(replay.variants.size()==result.variants.size(),"replay count");
            require(replay.shortfall==result.shortfall&&replay.shortfallReason==result.shortfallReason,"shortfall replay");
            for(int i=0;i<result.variants.size();++i){require(replay.variants[i].blocks==result.variants[i].blocks,"byte-identical second generation");require(replay.variants[i].metadata==result.variants[i].metadata,"metadata replay");}
            verifyEdges(sources,result);
        }
        write(output+"/verification.json",QJsonDocument(QJsonObject{{"pass",passes},{"fail",0},{"blockComparisons",double(blockComparisons)},{"edgeComparisons",double(edgeComparisons)}}).toJson());
        return 0;
    }catch(const std::exception& e){out<<"FAIL: "<<e.what()<<endl;
        if(!output.isEmpty())write(output+"/failure.json",QJsonDocument(QJsonObject{{"error",e.what()},{"passesBeforeFailure",passes},{"seconds",timer.elapsed()/1000.}}).toJson());
        return 1;}
}

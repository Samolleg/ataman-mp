#include "terrain_recipe_bake.h"
#include <QCoreApplication>
#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>

namespace
{
int passed=0, failed=0;
quint64 seamChannels=0, seamEdges=0;
void check(bool value, const QString& label)
{
    if(value) ++passed;
    else { ++failed; QTextStream(stderr) << "FAIL " << label << '\n'; }
}

QImage texture(int material, bool flat=false)
{
    QImage image(32,32,QImage::Format_RGBA8888);
    for(int y=0;y<32;++y) for(int x=0;x<32;++x)
        image.setPixelColor(x,y,QColor(flat ? 100 : (material*51+x*3+y)%256,
            flat ? 100 : (material*13+x+y*5)%256,flat ? 100 : (material*79+x*7+y*2)%256));
    return image;
}

STerrainRecipeBakeInput input(int width=2,int height=1,int count=2,bool flat=false)
{
    STerrainRecipeBakeInput in; in.tileWidth=width; in.tileHeight=height;
    in.vertexMaterials.fill(1,(width+1)*(height+1));
    in.preferredMaterials.fill(1,width*height);
    for(int m=1;m<=count;++m)
    {
        STerrainRecipeMaterial source; source.materialId=m; source.runtimeTerrainType=m-1;
        source.uniformTiles={texture(m,flat)}; in.materials.append(source);
    }
    for(int a=1;a<=count;++a) for(int b=a+1;b<=count;++b)
    {
        QVector<float> strip(97*65);
        for(int y=0;y<97;++y) for(int x=0;x<65;++x)
            strip[y*65+x]=float(qBound(0.0,(x-25+std::sin(y*.4)*5)/14,1.0));
        STerrainRecipeBrush brush;
        check(CTerrainRecipeBaker::makeBrush(a,b,{strip},&brush),"synthetic brush construction");
        in.brushes.append(brush);
    }
    return in;
}

void seams(const STerrainRecipeBakeResult& r,int width,int height,int size)
{
    bool all=true;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
    {
        const QImage& a=r.tiles[r.tileByCell[y*width+x]].pixels;
        if(x+1<width)
        {
            const QImage& b=r.tiles[r.tileByCell[y*width+x+1]].pixels;
            ++seamEdges;
            for(int t=0;t<size;++t) { all &= a.pixel(size-1,t)==b.pixel(0,t); seamChannels+=4; }
        }
        if(y+1<height)
        {
            const QImage& b=r.tiles[r.tileByCell[(y+1)*width+x]].pixels;
            ++seamEdges;
            for(int t=0;t<size;++t) { all &= a.pixel(t,0)==b.pixel(t,size-1); seamChannels+=4; }
        }
    }
    check(all,"all shared RGBA edges");
}

void synthetic()
{
    STerrainRecipeBakeOptions o; o.tileSize=17;
    for(int axis=0;axis<2;++axis) for(int pattern=0;pattern<64;++pattern)
    {
        auto in=input(axis ? 1:2,axis ? 2:1);
        for(int i=0;i<6;++i) in.vertexMaterials[i]=1+((pattern>>i)&1);
        auto r=CTerrainRecipeBaker::bake(in,o);
        check(r.success,"all six-vertex binary edge combinations");
        if(r.success) seams(r,in.tileWidth,in.tileHeight,o.tileSize);
    }
    auto in=input(32,32);
    quint32 random=123;
    for(auto& m : in.vertexMaterials) { random=random*1664525+1013904223; m=1+int(random>>31); }
    o.tileSize=33;
    auto r=CTerrainRecipeBaker::bake(in,o), again=CTerrainRecipeBaker::bake(in,o);
    check(r.success && again.success,"N4 random grid bake");
    if(r.success) seams(r,32,32,33);
    bool same=r.tileByCell==again.tileByCell && r.tiles.size()==again.tiles.size();
    for(int i=0;same && i<r.tiles.size();++i)
        same &= r.tiles[i].pixelSha256==again.tiles[i].pixelSha256 && r.tiles[i].pixels==again.tiles[i].pixels;
    check(same,"repeatable labels, bytes and dedup order");
    QMap<QByteArray,QSet<int>> labels;
    for(const auto& recipe : r.recipes)
    {
        QByteArray key=QByteArray::number(recipe.signature.northWest)+','+QByteArray::number(recipe.signature.northEast)
            +','+QByteArray::number(recipe.signature.southEast)+','+QByteArray::number(recipe.signature.southWest);
        labels[key].insert(recipe.variant);
    }
    check(labels.size()==16,"all sixteen ordered signatures, no rotation folding");
    bool n4=true; for(const auto& values:labels) n4 &= values.size()==4;
    check(n4,"four variants for every populated signature");
    // Reordering unrelated input containers cannot change label or raster output.
    std::reverse(in.materials.begin(),in.materials.end());
    auto reordered=CTerrainRecipeBaker::bake(in,o);
    check(reordered.tileByCell==r.tileByCell && reordered.tiles.size()==r.tiles.size(),"material container order independent");
    bool pixels=true; for(int i=0;i<r.tiles.size();++i) pixels &= r.tiles[i].pixels==reordered.tiles[i].pixels;
    check(pixels,"material order bytes independent");
    o.variants=8; o.tileSize=17;
    const auto n8=CTerrainRecipeBaker::bake(in,o);
    check(n8.success && n8.budget.recipeSlots>r.budget.recipeSlots,"N8 grows finite recipe budget");
    if(n8.success) seams(n8,32,32,17);
    o.variants=4;

    in=input(1,1); o.tileSize=17;
    in.vertexMaterials={1,2,1,1};
    r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.recipes[0].runtimeTerrainType==0,"majority corner class");
    const QVector<QVector<qint32>> rotations={{1,2,1,1},{1,1,1,2},{1,1,2,1},{2,1,1,1}};
    for(const auto& field:rotations)
    {
        in.vertexMaterials=field; r=CTerrainRecipeBaker::bake(in,o);
        check(r.success && r.recipes[0].runtimeTerrainType==0,"class invariant under quarter turns");
    }
    in.vertexMaterials={1,2,1,2}; in.preferredMaterials={2};
    r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.recipes[0].runtimeTerrainType==1,"tie uses painted material class");
    in.preferredMaterials.clear(); r=CTerrainRecipeBaker::bake(in,o);
    check(!r.success && r.tiles.isEmpty(),"tie without preference refused");
    in.preferredRuntimeTypes={16}; r=CTerrainRecipeBaker::bake(in,o);
    check(!r.success,"unknown tie class refused");
    in.preferredRuntimeTypes={0}; r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.recipes[0].runtimeTerrainType==0,"explicit tie class");
    in.materials[1].runtimeTerrainType=-1; r=CTerrainRecipeBaker::bake(in,o);
    check(!r.success && r.tiles.isEmpty(),"unknown source class refused");
    in.materials[1].runtimeTerrainType=16;
    check(!CTerrainRecipeBaker::bake(in,o).success,"class above fifteen refused");
    in.materials[0].runtimeTerrainType=15; in.materials[1].runtimeTerrainType=15;
    check(CTerrainRecipeBaker::bake(in,o).success,"class fifteen supported");
    o.preserveReviewedCellRuntimeTypes=true; in.preferredRuntimeTypes={7};
    r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.recipes[0].runtimeTerrainType==7,"explicit reviewed predecessor class preserved");
    in.preferredRuntimeTypes={16};
    check(!CTerrainRecipeBaker::bake(in,o).success,"invalid reviewed predecessor class refused");
    in.preferredRuntimeTypes.clear();
    check(!CTerrainRecipeBaker::bake(in,o).success,"missing reviewed predecessor class refused");
    o.preserveReviewedCellRuntimeTypes=false;

    in=input(2,1,2,true); in.vertexMaterials={1,1,2,1,1,2};
    in.preferredMaterials={1,2}; o.tileSize=2;
    r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.tiles.size()==2,"identical bytes with different runtime classes remain separate");
    in.materials[1].runtimeTerrainType=0; r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.tiles.size()==1,"identical bytes and agreeing class deduplicate");
    check(r.recipes[0].signature.northWest!=r.recipes[1].signature.northEast,"cell signatures retained after pixel aliasing");
    o.reservedSlots=4096;
    r=CTerrainRecipeBaker::bake(in,o);
    check(!r.success && r.tiles.size()==1 && r.budget.requiredSlots==4097
        && r.diagnostics.contains("terrain.recipe.slot_limit_exceeded"),"budget overflow preserves pixels and reports refusal");
    o.reservedSlots=0; o.tileSize=17;
    for(quint64 slotCount : {quint64(2047),quint64(2048),quint64(2049),quint64(4095),quint64(4096),quint64(4097),quint64(8191),quint64(8192),quint64(8193)})
    {
        const auto b=CTerrainRecipeBaker::budget(slotCount,slotCount-1,1);
        check(b.requiredSlots==slotCount && b.pages==(slotCount+63)/64 && b.exceeds2048==(slotCount>2048)
            && b.exceeds4096==(slotCount>4096) && b.exceeds8192==(slotCount>8192),"strict budget threshold and page rounding");
    }
    check(CTerrainRecipeBaker::budget(1,1,std::numeric_limits<quint64>::max()).exceeds8192,"budget arithmetic saturates");
    in=input(1,1,4); in.preferredMaterials={1};
    for(const QVector<qint32>& field : {QVector<qint32>{1,2,1,3},QVector<qint32>{1,2,4,3}})
    {
        in.vertexMaterials=field; r=CTerrainRecipeBaker::bake(in,o);
        check(r.success && r.tiles.size()==1 && r.unsupportedCells.isEmpty() && r.recipes[0].supported,"3/4-material junction supported");
        o.diagnosticJunctionPreview=true; r=CTerrainRecipeBaker::bake(in,o);
        check(!r.success && r.previewOnly && r.tiles.size()==1 && !r.recipes[0].supported,"junction diagnostic preview never success");
        o.diagnosticJunctionPreview=false;
    }
    in=input(1,1); in.vertexMaterials={1,2,1,1}; in.brushes.clear();
    check(!CTerrainRecipeBaker::bake(in,o).success,"missing pair brush refused without generic fallback");
    in=input(1,1); in.vertexMaterials[0]=999;
    check(!CTerrainRecipeBaker::bake(in,o).success,"unknown semantic material refused");
    in=input(); in.vertexMaterials.removeLast();
    check(!CTerrainRecipeBaker::bake(in,o).success,"incomplete field refused");
    in=input(); o.variants=0;
    check(!CTerrainRecipeBaker::bake(in,o).success,"invalid N refused");
    o.variants=4; in.brushes[0].ownership[0]=std::numeric_limits<float>::quiet_NaN();
    check(!CTerrainRecipeBaker::bake(in,o).success,"nonfinite brush refused");
    QVector<float> fit; double rmse=0; QString error;
    QImage a(32,32,QImage::Format_RGB32), b=a, t=a;
    a.fill(qRgb(20,40,60)); b.fill(qRgb(220,140,160)); t.fill(qRgb(120,90,110));
    check(CTerrainRecipeBaker::estimateStockOwnership(t,a,b,&fit,&rmse,&error)
        && rmse==0 && fit.size()==1024 && fit[0]==.5f,"runtime RGB ownership extraction on known mixture");
    check(!CTerrainRecipeBaker::estimateStockOwnership(QImage(),a,b,&fit,&rmse,&error) && !error.isEmpty(),"bad stock fit inputs refused");
    STerrainRecipeBrush brush;
    check(!CTerrainRecipeBaker::makeBrush(2,1,{{.5f}},&brush,&error),"invalid brush pair/shape refused");
    QImage sa(128,128,QImage::Format_RGB32),sb=sa,st=sa;
    sa.fill(qRgb(20,40,60)); sb.fill(qRgb(220,140,160));
    for(int y=0;y<128;++y) for(int x=0;x<128;++x)
    {
        const double amount=qBound(0.0,(x-58+std::sin(y*.35)*4)/12,1.0);
        st.setPixel(x,y,qRgb(int(20+200*amount),int(40+100*amount),int(60+100*amount)));
    }
    STerrainTransitionSignature signature; signature.northWest=1; signature.northEast=2;
    signature.southEast=2; signature.southWest=1;
    QVector<float> strip;
    check(CTerrainRecipeBaker::extractStockStrip(st,sa,sb,1,2,signature,&strip,&error)
        && strip.size()>=28*65,"runtime stock contour and unblurred fringe extraction");
    check(CTerrainRecipeBaker::makeBrush(1,2,{strip},&brush,&error),"runtime extracted brush bank");
    bool tails=true; for(int y=0;y<512;++y) tails &= brush.ownership[y*65]==0 && brush.ownership[y*65+64]==1;
    check(tails,"extracted bank has pure tails");
    QVector<float> stripAgain;
    check(CTerrainRecipeBaker::extractStockStrip(st,sa,sb,1,2,signature,&stripAgain,&error)
        && stripAgain==strip,"runtime extraction repeatable");
    for(int pattern=1;pattern<15;++pattern)
    {
        signature.northWest=1+((pattern>>0)&1); signature.northEast=1+((pattern>>1)&1);
        signature.southEast=1+((pattern>>2)&1); signature.southWest=1+((pattern>>3)&1);
        const double q0=2*((pattern>>0)&1)-1,q1=2*((pattern>>1)&1)-1;
        const double q2=2*((pattern>>2)&1)-1,q3=2*((pattern>>3)&1)-1;
        for(int y=0;y<128;++y) for(int x=0;x<128;++x)
        {
            const double fx=x/127.0,fy=y/127.0;
            const double score=q0*(1-fx)*(1-fy)+q1*fx*(1-fy)+q2*fx*fy+q3*(1-fx)*fy;
            const double amount=qBound(0.0,.5+score*8+.08*std::sin(y*.5),1.0);
            st.setPixel(x,y,qRgb(int(20+200*amount),int(40+100*amount),int(60+100*amount)));
        }
        check(CTerrainRecipeBaker::extractStockStrip(st,sa,sb,1,2,signature,&strip,&error),
            QString("all rotated 1/3, straight and alternating stock contours %1: %2").arg(pattern).arg(error));
    }
    signature.northWest=3;
    check(!CTerrainRecipeBaker::extractStockStrip(st,sa,sb,1,2,signature,&strip,&error),"stock signature cannot substitute unrelated material");
}

void junctions()
{
    STerrainRecipeBakeOptions o; o.tileSize=9; o.origin=QPoint(-77,136);
    // All 4^6 adjacent corner fields along both axes: includes every ordered
    // 3/4-node next to every compatible uniform/2/3/4-node. Alternate classes
    // are reviewed here to separate edge continuity from tie classification.
    for(int axis=0;axis<2;++axis)
    {
        auto in=input(axis ? 1:2,axis ? 2:1,4);
        in.preferredRuntimeTypes={0,1}; o.preserveReviewedCellRuntimeTypes=true;
        for(int pattern=0;pattern<4096;++pattern)
        {
            int digits=pattern;
            for(auto& m:in.vertexMaterials) { m=1+digits%4; digits/=4; }
            const auto r=CTerrainRecipeBaker::bake(in,o);
            check(r.success && r.unsupportedCells.isEmpty(),"all six-vertex four-material configurations supported");
            if(r.success) seams(r,in.tileWidth,in.tileHeight,o.tileSize);
        }
    }
    auto in=input(1,1,4); o.preserveReviewedCellRuntimeTypes=false; o.tileSize=33;
    for(int pattern=0;pattern<256;++pattern)
    {
        int digits=pattern; QMap<int,int> counts;
        for(auto& m:in.vertexMaterials) { m=1+digits%4; digits/=4; ++counts[m-1]; }
        int maximum=0,chosen=-1;
        for(auto it=counts.cbegin();it!=counts.cend();++it)
            if(it.value()>=maximum) { maximum=it.value(); chosen=it.key(); }
        in.preferredRuntimeTypes={chosen};
        const auto r=CTerrainRecipeBaker::bake(in,o), again=CTerrainRecipeBaker::bake(in,o);
        check(r.success && r.unsupportedCells.isEmpty() && r.recipes[0].runtimeTerrainType==chosen,
              "all ordered signatures majority/tie including diagonal 2+2 and 1+1+2");
        check(r.success && again.success && r.tileByCell==again.tileByCell && r.tiles[0].pixels==again.tiles[0].pixels,
              "all ordered signatures deterministic bytes");
    }
    in.vertexMaterials={1,2,3,4}; in.preferredMaterials.clear(); in.preferredRuntimeTypes.clear();
    check(!CTerrainRecipeBaker::bake(in,o).success,"four-way tie requires explicit preference");
    in.preferredRuntimeTypes={7};
    check(!CTerrainRecipeBaker::bake(in,o).success,"four-way tie outside tied classes refused");
    for(int type=0;type<4;++type)
    {
        in.preferredRuntimeTypes={type};
        const auto r=CTerrainRecipeBaker::bake(in,o);
        check(r.success && r.recipes[0].runtimeTerrainType==type,"each four-way preferred class accepted");
    }
    in.vertexMaterials={1,2,3,4}; in.materials[2].runtimeTerrainType=1;
    in.preferredRuntimeTypes={3};
    auto r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.recipes[0].runtimeTerrainType==1,"class aliases accumulate majority over preference");
    in.brushes.removeLast();
    check(!CTerrainRecipeBaker::bake(in,o).success,"missing junction pair bank refused");
    in=input(32,32,4); in.preferredRuntimeTypes.fill(0,1024);
    o.preserveReviewedCellRuntimeTypes=true; o.tileSize=128;
    quint32 random=456;
    for(auto& m:in.vertexMaterials) { random=random*1664525+1013904223; m=1+int(random>>30); }
    r=CTerrainRecipeBaker::bake(in,o);
    check(r.success && r.unsupportedCells.isEmpty(),"N4 random four-material grid supported at 128px");
    if(r.success) seams(r,32,32,128);
    std::reverse(in.materials.begin(),in.materials.end()); std::reverse(in.brushes.begin(),in.brushes.end());
    const auto reordered=CTerrainRecipeBaker::bake(in,o);
    bool same=r.success && reordered.success && r.tileByCell==reordered.tileByCell && r.tiles.size()==reordered.tiles.size();
    for(int i=0;same && i<r.tiles.size();++i) same &= r.tiles[i].pixels==reordered.tiles[i].pixels;
    check(same,"junction material/brush order independent at 128px");
    o.variants=8; o.tileSize=33;
    r=CTerrainRecipeBaker::bake(in,o);
    check(r.success,"junction N8 grid supported");
    if(r.success) seams(r,32,32,33);
}

bool zone(const QString& path)
{
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) return false;
    const auto j=QJsonDocument::fromJson(file.readAll()).object();
    STerrainRecipeBakeInput in; in.tileWidth=32; in.tileHeight=32;
    for(const auto& v:j["field"].toArray()) in.vertexMaterials.append(v.toInt());
    for(const auto& v:j["preferredTypes"].toArray()) in.preferredRuntimeTypes.append(v.toInt());
    for(const auto& v:j["materials"].toArray())
    {
        const auto m=v.toObject(); STerrainRecipeMaterial source;
        source.materialId=m["id"].toInt(); source.runtimeTerrainType=m["class"].toInt();
        for(const auto& tile:m["tiles"].toArray()) source.uniformTiles.append(QImage(tile.toString()));
        in.materials.append(source);
    }
    for(const auto& v:j["brushes"].toArray())
    {
        const auto b=v.toObject(); STerrainRecipeBrush brush;
        brush.firstMaterial=b["a"].toInt(); brush.secondMaterial=b["b"].toInt();
        QFile binary(b["file"].toString()); if(!binary.open(QIODevice::ReadOnly)) return false;
        const QByteArray bytes=binary.readAll(); if(bytes.size()!=512*65*int(sizeof(float))) return false;
        brush.ownership.resize(512*65); std::memcpy(brush.ownership.data(),bytes.constData(),size_t(bytes.size()));
        in.brushes.append(brush);
    }
    STerrainRecipeBakeOptions o; o.origin=QPoint(88,136);
    o.diagnosticJunctionPreview=j["legacy"].toBool(false);
    o.preserveReviewedCellRuntimeTypes=true;
    const auto r=CTerrainRecipeBaker::bake(in,o);
    QTextStream(stdout) << "ZONE recipes=" << r.budget.recipeSlots << " typedUnique=" << r.tiles.size()
        << " unsupported=" << r.unsupportedCells.size() << " previewOnly=" << r.previewOnly << " success=" << r.success << '\n';
    if(r.tiles.isEmpty()) { for(const auto& d:r.diagnostics) QTextStream(stderr) << d << '\n'; return false; }
    QImage image(4096,4096,QImage::Format_RGBA8888);
    for(int y=0;y<32;++y) for(int x=0;x<32;++x)
    {
        const auto& tile=r.tiles[r.tileByCell[y*32+x]].pixels;
        for(int row=0;row<128;++row) std::memcpy(image.scanLine((31-y)*128+row)+x*128*4,tile.constScanLine(row),128*4);
    }
    check(image.save(j["output"].toString()),"Zone6 PNG saved");
    seams(r,32,32,128);
    check(o.diagnosticJunctionPreview ? (r.previewOnly && !r.success && r.unsupportedCells.size()==250)
          : (r.success && !r.previewOnly && r.unsupportedCells.isEmpty()),"Zone6 junction support/legacy comparison");
    check(r.tiles.size()==439 && r.budget.recipeSlots==439,"Zone6 N4 prototype budget");
    return true;
}
}

void incrementalMaps()
{
    auto full=input(2,1,4);full.vertexMaterials={1,2,3,4,3,2};full.preferredMaterials={1,2};
    STerrainRecipeBakeOptions options;options.tileSize=33;options.stableCoordinateVariants=true;
    options.origin=QPoint(-19,27);
    const auto combined=CTerrainRecipeBaker::bake(full,options);
    check(combined.success,"incremental paired-map reference bake");
    QVector<QImage> splitPixels;
    for(int cell=0;cell<2;++cell)
    {
        auto single=full;single.tileWidth=single.tileHeight=1;
        single.vertexMaterials={full.vertexMaterials[cell],full.vertexMaterials[cell+1],full.vertexMaterials[cell+3],full.vertexMaterials[cell+4]};
        single.preferredMaterials={full.preferredMaterials[cell]};
        auto local=options;local.origin+=QPoint(cell,0);
        const auto part=CTerrainRecipeBaker::bake(single,local);
        check(part.success,"separate neighboring map bake");
        if(!part.success || !combined.success)continue;
        check(part.recipes.first().variant==combined.recipes[cell].variant,"stable variant independent of neighboring group membership");
        const QImage pixels=part.tiles[part.tileByCell.first()].pixels;
        check(pixels==combined.tiles[combined.tileByCell[cell]].pixels,"incremental map recipe bytes equal combined bake");
        splitPixels.append(pixels);
    }
    bool joined=splitPixels.size()==2;
    if(joined)for(int y=0;y<33;++y){joined&=splitPixels[0].pixel(32,y)==splitPixels[1].pixel(0,y);seamChannels+=4;}
    ++seamEdges;check(joined,"synthetic independent map pair shared edge exact");
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    if(app.arguments().size()==3 && app.arguments()[1]=="--zone") check(zone(app.arguments()[2]),"Zone6 fixture loaded");
    else { synthetic(); junctions(); incrementalMaps(); }
    QTextStream(stdout) << "RESULT PASS=" << passed << " FAIL=" << failed << " SKIP=0 edges=" << seamEdges
        << " RGBA_channel_comparisons=" << seamChannels << '\n';
    return failed ? 1:0;
}

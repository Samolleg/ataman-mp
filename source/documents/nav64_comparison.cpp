#include "nav64_comparison.h"
#include <QElapsedTimer>
#include <QtMath>
#include <algorithm>
#include <QSaveFile>
#include <QFileInfo>

int nav64LegacyLayer(const QString& profile){
    // Legacy radius classes are morphological Star3/5/7, not FIG half-widths.
    // Match nominal standing clearance and levitation, never profile ordinal.
    if(profile.startsWith("levitate"))return 0;
    if(profile=="giant")return 7;
    if(profile=="large")return 6;
    if(profile=="human")return 3;
    return 1;
}
QString nav64LegacyLayerLabel(int layer){
    const QStringList names={QStringLiteral("парящие, 2 м"),QStringLiteral("узкий / лёжа, 0,4 м"),QStringLiteral("узкий / ползком, 1 м"),QStringLiteral("в рост человека, 1,6 м"),QStringLiteral("узкий / высокий, 2,2 м"),QStringLiteral("широкий / низкий, 1,7 м"),QStringLiteral("широкий / высокий, 3 м"),QStringLiteral("очень крупный, 6 м")};
    return QStringLiteral("Слой %1 — %2").arg(layer).arg(names.value(layer));
}
SNav64Comparison compareNav64(const SNav64Document& mesh,const SAiGraphPreviewBuildInput& input,int layer,const std::shared_ptr<std::atomic_bool>& cancelled){
    SNav64Comparison out;QElapsedTimer timer;timer.start();out.layer=layer;
    if(mesh.header.profile=="flying"){out.error=QStringLiteral("У летающего профиля нет наземного слоя для сравнения");return out;}
    if(cancelled->load())return out;
    const auto old=CAiGraphPreviewBuilder::passability(input,layer);
    if(!old.valid){out.error=old.error;return out;}
    out.width=input.graphWidth*8;out.height=input.graphHeight*8;
    const int count=out.width*out.height;
    out.cells.fill(0,count);out.regions.fill(-1,count);out.heights=old.navigationSurfaceHeights;
    QVector<quint8> nav(count,0);
    const float height=float(CAiGraphDocument::canonicalHeight(layer));
    const auto clearance=Navigation::ClearanceMask(mesh.header.body,height);
    // Rasterize actual detail triangles at cell centres, with the requested
    // pose bit and the same floor as the legacy classifier. No nearest-poly
    // snapping across a wall/hole. A cell represents 0.25 square metres.
    for(const auto& t:mesh.triangles){
        if(cancelled->load())return {};
        if(!(t.clearance&clearance)||(mesh.header.algorithm>=25&&(t.clearance&(1u<<(layer+4)))))continue;
        const float denom=(t.b.y()-t.c.y())*(t.a.x()-t.c.x())+(t.c.x()-t.b.x())*(t.a.y()-t.c.y());
        if(qAbs(denom)<1e-8f)continue;
        const int x0=qMax(0,qCeil(2*qMin(t.a.x(),qMin(t.b.x(),t.c.x()))-.5f)),x1=qMin(out.width-1,qFloor(2*qMax(t.a.x(),qMax(t.b.x(),t.c.x()))-.5f));
        const int y0=qMax(0,qCeil(2*qMin(t.a.y(),qMin(t.b.y(),t.c.y()))-.5f)),y1=qMin(out.height-1,qFloor(2*qMax(t.a.y(),qMax(t.b.y(),t.c.y()))-.5f));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            const float px=(x+.5f)*.5f,py=(y+.5f)*.5f;
            const float u=((t.b.y()-t.c.y())*(px-t.c.x())+(t.c.x()-t.b.x())*(py-t.c.y()))/denom;
            const float v=((t.c.y()-t.a.y())*(px-t.c.x())+(t.a.x()-t.c.x())*(py-t.c.y()))/denom;
            if(u<-.00001f||v<-.00001f||u+v>1.00001f)continue;
            const int i=y*out.width+x;
            // Modern terrain polygons are connectivity footprints. Their
            // fan can bridge a shallow hollow by much more than 0.5 m.
            // Match declared terrain support to the actual MPR floor;
            // bridge cells retain the separate-floor height comparison.
            const float z=t.terrainOnly&&!old.bridgeMask.value(i)?input.terrainHeights[i]:
                u*t.a.z()+v*t.b.z()+(1-u-v)*t.c.z()-.12f;
            if(qAbs(z-out.heights[i])<=qMax(.5f,mesh.header.body.climb+2*mesh.header.body.cellHeight))nav[i]=1;
        }
    }
    const auto& mask=old.layerPassability[layer];
    for(int i=0;i<count;++i){if(mask[i]&&!nav[i]){out.cells[i]=1;++out.lost;}else if(!mask[i]&&nav[i]){out.cells[i]=2;++out.gained;}}
    // Four-connected components support list navigation and scene selection.
    QVector<int> queue;for(int i=0;i<count;++i){
        if(cancelled->load())return {};
        if(!out.cells[i]||out.regions[i]>=0)continue;
        const int region=out.areas.size();SNav64DifferenceRegion area;area.lost=out.cells[i]==1;
        const int sx=i%out.width,sy=i/out.width;int lx=sx,hx=sx,ly=sy,hy=sy;
        queue.clear();queue<<i;out.regions[i]=region;
        for(int at=0;at<queue.size();++at){const int j=queue[at],x=j%out.width,y=j/out.width;++area.cells;area.center+=QVector3D((x+.5f)*.5f,(y+.5f)*.5f,out.heights[j]);lx=qMin(lx,x);hx=qMax(hx,x);ly=qMin(ly,y);hy=qMax(hy,y);
            const int neighbors[]={x>0?j-1:-1,x+1<out.width?j+1:-1,y>0?j-out.width:-1,y+1<out.height?j+out.width:-1};
            for(int n:neighbors)if(n>=0&&out.cells[n]==out.cells[i]&&out.regions[n]<0){out.regions[n]=region;queue<<n;}
        }area.center/=float(area.cells);area.bounds=QRect(lx,ly,hx-lx+1,hy-ly+1);out.areas<<area;
    }
    const int across=(out.width+31)/32,down=(out.height+31)/32;
    out.renderTiles.resize(across*down);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x){const int i=y*out.width+x;if(!out.cells[i])continue;
        auto& tile=out.renderTiles[(y/32)*across+x/32];if(tile.areas.isEmpty()){tile.areas.resize(2);tile.minimum={float(x/32*16),float(y/32*16),-10000};tile.maximum=tile.minimum+QVector3D(16,16,20000);}
        auto& batch=tile.areas[out.cells[i]-1];batch.color=out.cells[i]==1?QVector4D(1,.15f,.16f,.7f):QVector4D(.1f,.65f,1,.7f);
        const float a=x*.5f,b=y*.5f,z=out.heights[i]+.2f;
        batch.vertices<<QVector3D(a,b,z)<<QVector3D(a+.5f,b,z)<<QVector3D(a+.5f,b+.5f,z)<<QVector3D(a,b,z)<<QVector3D(a+.5f,b+.5f,z)<<QVector3D(a,b+.5f,z);
    }
    out.valid=true;out.milliseconds=timer.elapsed();return out;
}

bool exportNav64ComparisonCsv(const QString& path,const QString& map,const QString& profile,const SNav64Comparison& comparison,QString* error){
    if(error)error->clear();auto fail=[&](const QString& message){if(error)*error=message;return false;};
    if(!comparison.valid)return fail(QStringLiteral("Сначала дождитесь расчёта потерь."));
    if(QFileInfo(path).suffix().compare("csv",Qt::CaseInsensitive)!=0)return fail(QStringLiteral("Выберите файл с расширением .csv."));
    auto quote=[](QString value){value.replace('"',"\"\"");return '"'+value+'"';};
    QByteArray data=QByteArray::fromHex("efbbbf")+QStringLiteral("карта,профиль,слой_старого_графа,тип,площадь_м2,центр_X,центр_Y,bbox_min_X,bbox_min_Y,bbox_max_X,bbox_max_Y\r\n").toUtf8();
    auto number=[](double value){return QString::number(value,'f',6);};
    for(const auto& region:comparison.areas){
        const auto& b=region.bounds;
        const QStringList fields={quote(map),quote(profile),QString::number(comparison.layer),quote(region.lost?QStringLiteral("потеряно"):QStringLiteral("появилось")),number(region.cells*.25),number(region.center.x()),number(region.center.y()),number(b.left()*.5),number(b.top()*.5),number((b.right()+1)*.5),number((b.bottom()+1)*.5)};
        data+=fields.join(',').toUtf8()+"\r\n";
    }
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())return fail(file.errorString());
    if(!file.commit())return fail(file.errorString());return true;
}

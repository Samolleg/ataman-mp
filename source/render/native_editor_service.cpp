#include "native_editor_service.h"
#include "opening_trace.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include <QPainter>
#include <QTextDocument>
#include <QtMath>
#include <algorithm>
#include <cmath>

QImage CNativeEditorService::raster(const QSize& size,const QVector<float>& depth) const {
    opening::Scope timing("worker.service.draw");
    QImage image(size,QImage::Format_RGBA8888_Premultiplied);image.fill(Qt::transparent);
    if(size.isEmpty()||depth.size()!=size.width()*size.height())return image;
    const int w=size.width(),h=size.height();const float scale=float(w)/qMax(1,logicalSize.width());
    const auto pixel=[&](int x,int y,float z,const QVector4D& color,bool test){
        if(x<0||x>=w||y<0||y>=h||!std::isfinite(z)||z<0||z>1)return;
        if(test&&z>depth[y*w+x]+.00002f)return;
        auto* p=image.scanLine(y)+4*x;const float a=qBound(0.f,color.w(),1.f),b=1-a;
        for(int c=0;c<3;++c)p[c]=uchar(qBound(0.f,color[c],1.f)*255*a+p[c]*b+.5f);
        p[3]=uchar(255*a+p[3]*b+.5f);
    };
    const auto projected=[&](const QVector4D& v){return QVector3D((v.x()/v.w()*.5f+.5f)*w,(.5f-v.y()/v.w()*.5f)*h,v.z()/v.w()*.5f+.5f);};
    const auto line=[&](QVector4D a,QVector4D b,const Batch& batch){
        // Homogeneous clipping prevents near-plane explosions and preserves
        // depth interpolation at a clipped edge.
        for(int plane=0;plane<6;++plane){const int axis=plane/2;const float sign=plane%2?1.f:-1.f;
            const float da=a.w()+sign*a[axis],db=b.w()+sign*b[axis];if(da<0&&db<0)return;
            if((da<0)!=(db<0)){const auto p=a+(b-a)*(da/(da-db));if(da<0)a=p;else b=p;}}
        if(a.w()<=0||b.w()<=0)return;const auto p=projected(a),q=projected(b);
        const int steps=qMax(1,int(std::ceil(qMax(qAbs(q.x()-p.x()),qAbs(q.y()-p.y())))));
        const int r=qMax(0,int(batch.width*scale*.5f));
        for(int i=0;i<=steps;++i){const auto v=p+(q-p)*(float(i)/steps);for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)pixel(qRound(v.x())+xx,qRound(v.y())+yy,v.z(),batch.color,batch.depth);}
    };
    const auto triangle=[&](const QVector4D& aa,const QVector4D& bb,const QVector4D& cc,const Batch& batch){
        QVector<QVector4D> polygon{aa,bb,cc};
        for(int plane=0;plane<6&&!polygon.isEmpty();++plane){QVector<QVector4D> clipped;const int axis=plane/2;const float sign=plane%2?1.f:-1.f;
            auto a=polygon.back();float da=a.w()+sign*a[axis];for(const auto& b:polygon){const float db=b.w()+sign*b[axis];if((da<0)!=(db<0))clipped.append(a+(b-a)*(da/(da-db)));if(db>=0)clipped.append(b);a=b;da=db;}polygon=std::move(clipped);}
        for(int n=1;n+1<polygon.size();++n){if(polygon[0].w()<=0||polygon[n].w()<=0||polygon[n+1].w()<=0)continue;
            const auto a=projected(polygon[0]),b=projected(polygon[n]),c=projected(polygon[n+1]);
            const auto edge=[](const QVector3D& a,const QVector3D& b,float x,float y){return (x-a.x())*(b.y()-a.y())-(y-a.y())*(b.x()-a.x());};
            const float area=edge(a,b,c.x(),c.y());if(qAbs(area)<1e-8f)continue;
            const int x0=qMax(0,int(std::floor(qMin(a.x(),qMin(b.x(),c.x()))))),x1=qMin(w-1,int(std::ceil(qMax(a.x(),qMax(b.x(),c.x())))));
            const int y0=qMax(0,int(std::floor(qMin(a.y(),qMin(b.y(),c.y()))))),y1=qMin(h-1,int(std::ceil(qMax(a.y(),qMax(b.y(),c.y())))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const float u=edge(b,c,x+.5f,y+.5f)/area,v=edge(c,a,x+.5f,y+.5f)/area,t=1-u-v;if(u>=0&&v>=0&&t>=0)pixel(x,y,u*a.z()+v*b.z()+t*c.z(),batch.color,batch.depth);}
        }
    };
    auto grid=this->grid,graphTerrain=this->graphTerrain;
    if(!terrainBytes.isEmpty()&&((grid&&!grid->vertexCountX())||(graphTerrain&&!graphTerrain->vertexCountX()))){
        thread_local QByteArray cachedBytes;
        thread_local std::shared_ptr<const CTerrainEditModel> cachedTerrain;
        if(cachedBytes!=terrainBytes){
            opening::Scope prepare("worker.service.terrain_prepare");CMprDocument document;CValidationReport report;auto model=std::make_shared<CTerrainEditModel>();
            cachedTerrain.reset();if(document.loadBytes(terrainBytes,"native-service-memory.mpr",&report)&&model->load(document,&report))cachedTerrain=model;cachedBytes=terrainBytes;
        }
        if(grid&&!grid->vertexCountX())grid=cachedTerrain;if(graphTerrain&&!graphTerrain->vertexCountX())graphTerrain=cachedTerrain;
    }
    QVector<Batch> prepared=batches;
    if(graph.isValid()){
        const int gw=graph.width(),gh=graph.height();QVector<QVector3D> positions(gw*gh);QVector<bool> valid(gw*gh,false);
        Batch nodes,isolated,links;nodes.primitive=isolated.primitive=Points;nodes.width=3;isolated.width=6;nodes.color={.95f,.82f,.2f,.88f};isolated.color={1,.16f,.12f,1};const QVector4D colors[8]={{.55f,.3f,1,.9f},{.42f,.38f,1,.9f},{.3f,.5f,.94f,.9f},{.2f,.62f,.84f,.9f},{.14f,.7f,.72f,.9f},{.12f,.75f,.59f,.9f},{.15f,.79f,.45f,.9f},{.28f,.82f,.36f,.9f}};links.color=colors[qBound(0,graphLayer,7)];links.width=1.5f;
        for(int y=0;y<gh;++y)for(int x=0;x<gw;++x){const int lx=graph.localX(graphLayer,x,y),ly=graph.localY(graphLayer,x,y);if(lx>7||ly>7)continue;
            QVector3D p((x*8+lx+.5f)*.5f,(y*8+ly+.5f)*.5f,0);const int i=(y*8+ly)*(gw*8)+x*8+lx;
            if(i<graphHeights.size())p.setZ(graphHeights[i]);else if(graphTerrain)p.setZ(graphTerrain->terrainSurfaceHeight(p.x(),p.y()));else continue;
            p.setZ(p.z()+.2f);positions[y*gw+x]=p;valid[y*gw+x]=true;nodes.vertices<<p;}
        for(int y=0;y<gh;++y)for(int x=0;x<gw;++x){const int cell=y*gw+x;if(!valid[cell])continue;bool connected=false;
            for(int d=0;d<8;++d){if(graph.weight(graphLayer,x,y,d)<0)continue;connected=true;const auto delta=CAiGraphDocument::directionDelta(d);const int nx=x+delta.x(),ny=y+delta.y();if(nx<0||ny<0||nx>=gw||ny>=gh||!valid[ny*gw+nx])continue;
                const int n=ny*gw+nx;if(n<cell&&graph.weight(graphLayer,nx,ny,(d+4)%8)>=0)continue;links.vertices<<positions[cell]<<positions[n];}
            if(!connected)isolated.vertices<<positions[cell];}
        prepared<<links<<nodes<<isolated;
    }
    if(grid){Batch batch;batch.color={.05f,.9f,1,1};const auto point=[&](int x,int y){const auto xy=grid->terrainVertexPosition(x,y);return QVector3D(xy.x(),xy.y(),grid->height(x,y)*grid->maxZ()/65535.f+.015f);};
        for(int y=0;y<grid->vertexCountY();y+=2)for(int x=0;x+1<grid->vertexCountX();++x)batch.vertices<<point(x,y)<<point(x+1,y);
        for(int x=0;x<grid->vertexCountX();x+=2)for(int y=0;y+1<grid->vertexCountY();++y)batch.vertices<<point(x,y)<<point(x,y+1);prepared.append(std::move(batch));}
    for(const auto& batch:prepared){auto vertices=batch.vertices;
        Primitive primitive=batch.primitive;
        if(primitive==Ring){vertices.clear();for(const auto& center:batch.vertices)for(int i=0;i<24;++i){const float a=i*6.2831853f/24,b=(i+1)*6.2831853f/24;vertices<<center+QVector3D(qCos(a)*.8f,qSin(a)*.8f,.2f)<<center+QVector3D(qCos(b)*.8f,qSin(b)*.8f,.2f);}primitive=Lines;}
        if(primitive==Box&&vertices.size()==2){const auto lo=vertices[0],hi=vertices[1];vertices.clear();for(int i=0;i<8;++i)for(int a=0;a<3;++a)if(!(i&(1<<a))){const int j=i|(1<<a);vertices<<QVector3D(i&1?hi.x():lo.x(),i&2?hi.y():lo.y(),i&4?hi.z():lo.z())<<QVector3D(j&1?hi.x():lo.x(),j&2?hi.y():lo.y(),j&4?hi.z():lo.z());}primitive=Lines;}
        QVector<QVector4D> points;points.reserve(vertices.size());const auto matrix=clip*batch.model;for(const auto& p:vertices)points.append(matrix*QVector4D(p,1));
        if(primitive==Lines)for(int i=0;i+1<points.size();i+=2)line(points[i],points[i+1],batch);
        else if(primitive==Loop)for(int i=0;i<points.size();++i)line(points[i],points[(i+1)%points.size()],batch);
        else if(primitive==Strip)for(int i=1;i<points.size();++i)line(points[i-1],points[i],batch);
        else if(primitive==Points)for(const auto& p:points){if(p.w()<=0)continue;const auto q=projected(p);const int r=qMax(1,int(batch.width*scale*.5f));for(int y=-r;y<=r;++y)for(int x=-r;x<=r;++x)pixel(qRound(q.x())+x,qRound(q.y())+y,q.z(),batch.color,batch.depth);}
        else if(primitive==Triangles)for(int i=0;i+2<points.size();i+=3)triangle(points[i],points[i+1],points[i+2],batch);
        else if(primitive==Fan)for(int i=1;i+1<points.size();++i)triangle(points[0],points[i],points[i+1],batch);
    }
    QPainter painter(&image);painter.scale(scale,scale);painter.setRenderHint(QPainter::Antialiasing);
    for(const auto& label:labels){const auto p=clip*QVector4D(label.position,1);if(p.w()<=0||p.z()<-p.w()||p.z()>p.w())continue;const auto q=projected(p)/scale;painter.drawImage(QPointF(q.x()-label.image.width()/2.,q.y()-label.image.height()/2.),label.image);}
    if(!selectionRect.isEmpty()){painter.setPen(QPen(QColor(35,215,245),1));painter.setBrush(QColor(25,190,230,25));painter.drawRect(selectionRect);}
    if(!legend.isEmpty()){QTextDocument doc;doc.setDefaultFont(QFont("Segoe UI",9));doc.setDefaultStyleSheet("body {color:white;} p {margin:0;}");doc.setHtml("<body>"+legend+"</body>");doc.setTextWidth(legendRect.width()-12);QRect box=legendRect;box.setHeight(qCeil(doc.size().height())+10);box.moveTop(qMax(12,qMin(box.top(),logicalSize.height()-box.height()-12)));painter.setPen(QColor(180,190,200,180));painter.setBrush(QColor(18,22,26,220));painter.drawRoundedRect(box,5,5);painter.translate(box.topLeft()+QPoint(6,5));doc.drawContents(&painter);}
    return image;
}

#ifndef WORLD_MATERIAL_BOUNDARY_H
#define WORLD_MATERIAL_BOUNDARY_H
#include "terrain_virtual_atlas.h"
#include <QPointF>
#include <QRectF>
#include <cmath>

struct SWorldMaterialField
{
    QPointF origin;
    CTerrainVirtualMaterialField field;
};

struct SWorldMaterialRepairMap
{
    QString id;
    QPointF origin;
    CTerrainVirtualMaterialField field;
    const CTerrainEditModel* geometry{nullptr};
    const CTerrainTransitionCatalog* catalog{nullptr};
    ETerrainTileLayer layer{ETerrainTileLayer::Land};
    bool editable{true};
};
struct SWorldMaterialRepairPlan
{
    QMap<QString,CTerrainVirtualMaterialField> fields;
    QMap<QString,QSet<int>> changedCells;
    QMap<QString,QSet<int>> seamCells;
    QMap<QString,QVector<QPoint>> conflicts;
    int changedVertices{0};
};

// Immutable cohort planning. Material IDs must already use the global catalog;
// physical atlas indices are never compared between maps.
inline SWorldMaterialRepairPlan planWorldMaterialRepair(
        const QVector<SWorldMaterialRepairMap>& maps)
{
    SWorldMaterialRepairPlan plan;
    struct Edge { int map,x,y; QPointF p[3]; qint32 first,last,pure; };
    QVector<Edge> edges;
    const auto point=[](const SWorldMaterialRepairMap& m,int x,int y) {
        return m.origin+(m.geometry?m.geometry->terrainVertexPosition(x,y):QPointF(x,y));
    };
    const auto adjacent=[](const SWorldMaterialRepairMap& a,const SWorldMaterialRepairMap& b) {
        const QRectF ar(a.origin,QSizeF(a.field.tileWidth()*2,a.field.tileHeight()*2));
        const QRectF br(b.origin,QSizeF(b.field.tileWidth()*2,b.field.tileHeight()*2));
        return ((qAbs(ar.right()-br.left())<1e-5 || qAbs(ar.left()-br.right())<1e-5)
                && qMin(ar.bottom(),br.bottom())-qMax(ar.top(),br.top())>1e-5)
            || ((qAbs(ar.bottom()-br.top())<1e-5 || qAbs(ar.top()-br.bottom())<1e-5)
                && qMin(ar.right(),br.right())-qMax(ar.left(),br.left())>1e-5);
    };
    for(int i=0;i<maps.size();++i) {
        const auto& m=maps[i];plan.fields[m.id]=m.field;
        const int w=m.field.tileWidth(),h=m.field.tileHeight();
        for(bool vertical:{false,true})for(int side:{0,1})for(int k=0;k<(vertical?h:w);++k) {
            const int x=vertical?(side?w:0):k,y=vertical?k:(side?h:0);
            Edge e;e.map=i;e.x=vertical?(side?w-1:0):k;e.y=vertical?k:(side?h-1:0);
            for(int j=0;j<3;++j)e.p[j]=point(m,x*2+(vertical?0:j),y*2+(vertical?j:0));
            e.first=m.field.material(x,y);e.last=m.field.material(x+(vertical?0:1),y+(vertical?1:0));
            bool found=false;STerrainTransitionSignature sig;
            if(m.catalog && m.geometry) {
                bool available=false;const auto packed=m.geometry->packedTile(e.x,e.y,m.layer,&available);
                if(!available || (m.layer==ETerrainTileLayer::Overlay && packed==0xffff))continue;
                sig=m.catalog->gridSignatureForPacked(packed,&found);
            } else sig=m.field.cellSignature(e.x,e.y,&found);
            e.pure=found && sig.northWest>0 && sig.northWest==sig.northEast
                && sig.northWest==sig.southEast && sig.northWest==sig.southWest?sig.northWest:0;
            edges.append(e);
        }
    }
    for(int i=0;i<maps.size();++i) {
        const auto& m=maps[i];if(!m.editable)continue;
        const int w=m.field.tileWidth(),h=m.field.tileHeight();
        for(int y=0;y<=h;++y)for(int x=0;x<=w;++x) {
            if(x && y && x<w && y<h)continue;
            const auto p=point(m,2*x,2*y);QSet<qint32> pure,values;bool contact=false;
            for(const auto& e:edges) {
                if(e.map!=i && !adjacent(m,maps[e.map]))continue;
                bool on=false;double position=0;
                for(int j=0;j<2;++j) {
                    const auto d=e.p[j+1]-e.p[j];const double length=d.x()*d.x()+d.y()*d.y();
                    if(length<1e-12)continue;
                    const auto v=p-e.p[j];const double t=(v.x()*d.x()+v.y()*d.y())/length;
                    const auto distance=v-d*t;
                    if(t>=-1e-6 && t<=1+1e-6 && std::hypot(distance.x(),distance.y())<1e-5) {
                        on=true;position=(j+qBound(0.0,t,1.0))/2;break;
                    }
                }
                if(!on)continue;
                if(e.map!=i)contact=true;
                if(e.pure>0)pure.insert(e.pure);
                if(position<1e-6 && e.first>0)values.insert(e.first);
                else if(position>1-1e-6 && e.last>0)values.insert(e.last);
                else if(e.first==e.last && e.first>0)values.insert(e.first);
            }
            if(!contact)continue;
            for(int cy:{y-1,y})for(int cx:{x-1,x})if(cx>=0 && cy>=0 && cx<w && cy<h)
                plan.seamCells[m.id].insert(cy*w+cx);
            const auto& evidence=pure.isEmpty()?values:pure;
            if(evidence.size()!=1) {plan.conflicts[m.id].append(QPoint(x,y));continue;}
            const auto material=*evidence.cbegin();
            if(material==m.field.material(x,y))continue;
            plan.fields[m.id].setMaterial(x,y,material);++plan.changedVertices;
            for(int cy:{y-1,y})for(int cx:{x-1,x})if(cx>=0 && cy>=0 && cx<w && cy<h)
                plan.changedCells[m.id].insert(cy*w+cx);
        }
    }
    return plan;
}
// Only a shared edge with positive length is adjacency. Corner contacts,
// gaps and overlapping maps cannot supply boundary evidence. At a shifted
// edge, categorical interpolation is valid only when both endpoints agree.
inline int reconcileWorldMaterialBoundary(const QPointF& origin,
        CTerrainVirtualMaterialField* field,
        const QVector<SWorldMaterialField>& neighbors)
{
    if(!field)return 0;
    const double epsilon=0.00001;
    const QRectF active(origin,QSizeF(field->tileWidth()*2,field->tileHeight()*2));
    int changed=0;
    for(int y=0;y<field->vertexHeight();++y)for(int x=0;x<field->vertexWidth();++x) {
        if(x && y && x<field->tileWidth() && y<field->tileHeight())continue;
        const QPointF world=origin+QPointF(x*2,y*2);
        QSet<qint32> evidence;
        bool ambiguous=false;
        for(const auto& neighbor:neighbors) {
            const auto& f=neighbor.field;
            const QRectF other(neighbor.origin,QSizeF(f.tileWidth()*2,f.tileHeight()*2));
            const bool vertical=(std::abs(active.right()-other.left())<epsilon
                || std::abs(active.left()-other.right())<epsilon)
                && qMin(active.bottom(),other.bottom())-qMax(active.top(),other.top())>epsilon;
            const bool horizontal=(std::abs(active.bottom()-other.top())<epsilon
                || std::abs(active.top()-other.bottom())<epsilon)
                && qMin(active.right(),other.right())-qMax(active.left(),other.left())>epsilon;
            if(!vertical && !horizontal)continue;
            const double nx=(world.x()-neighbor.origin.x())/2;
            const double ny=(world.y()-neighbor.origin.y())/2;
            if(nx<-epsilon || ny<-epsilon || nx>f.tileWidth()+epsilon || ny>f.tileHeight()+epsilon)continue;
            if(vertical && std::abs(nx)>epsilon && std::abs(nx-f.tileWidth())>epsilon)continue;
            if(horizontal && std::abs(ny)>epsilon && std::abs(ny-f.tileHeight())>epsilon)continue;
            const int x0=qBound(0,int(std::floor(nx+epsilon)),f.tileWidth());
            const int x1=qBound(0,int(std::ceil(nx-epsilon)),f.tileWidth());
            const int y0=qBound(0,int(std::floor(ny+epsilon)),f.tileHeight());
            const int y1=qBound(0,int(std::ceil(ny-epsilon)),f.tileHeight());
            bool foundA=false,foundB=false;
            const auto a=f.material(x0,y0,&foundA),b=f.material(x1,y1,&foundB);
            if(!foundA || !foundB || a<=0 || b<=0 || a!=b){ambiguous=true;continue;}
            evidence.insert(a);
        }
        if(ambiguous || evidence.size()!=1)continue;
        const auto value=*evidence.cbegin();
        if(field->material(x,y)!=value && field->setMaterial(x,y,value))++changed;
    }
    return changed;
}
#endif

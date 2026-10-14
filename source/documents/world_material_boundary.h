#ifndef WORLD_MATERIAL_BOUNDARY_H
#define WORLD_MATERIAL_BOUNDARY_H
#include "terrain_virtual_atlas.h"
#include <QPointF>
#include <QRectF>
#include <cmath>
#include <algorithm>

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
    QSet<int> protectedCells;
};
inline QSet<QString> worldMaterialIncompatibleStitchMaps(
        const QMap<QString,QPointF>& origins,const QMap<QString,QSizeF>& sizes,
        const QString& activeMap = {})
{
    QSet<QString> result;const double epsilon=1e-5;
    for(auto a=origins.cbegin();a!=origins.cend();++a) {
        const QRectF ar(a.value(),sizes.value(a.key()));
        if(ar.width()<=0||ar.height()<=0)continue;
        auto b=a;++b;
        for(;b!=origins.cend();++b) {
            if(!activeMap.isEmpty() && a.key()!=activeMap && b.key()!=activeMap)continue;
            const QRectF br(b.value(),sizes.value(b.key()));
            if(br.width()<=0||br.height()<=0)continue;
            const bool touching=((qAbs(ar.right()-br.left())<epsilon||qAbs(ar.left()-br.right())<epsilon)
                    &&qMin(ar.bottom(),br.bottom())-qMax(ar.top(),br.top())>epsilon)
                ||((qAbs(ar.bottom()-br.top())<epsilon||qAbs(ar.top()-br.bottom())<epsilon)
                    &&qMin(ar.right(),br.right())-qMax(ar.left(),br.left())>epsilon);
            const QPointF phase=a.value()-b.value();
            const bool aligned=qAbs(phase.x()/2-qRound(phase.x()/2))<epsilon
                &&qAbs(phase.y()/2-qRound(phase.y()/2))<epsilon;
            if(touching&&!aligned){result.insert(a.key());result.insert(b.key());}
        }
    }
    return result;
}
struct SWorldMaterialRepairPlan
{
    QMap<QString,CTerrainVirtualMaterialField> fields;
    QMap<QString,QSet<int>> changedCells;
    QMap<QString,QSet<int>> seamCells;
    QMap<QString,QVector<QPoint>> conflicts;
    int changedVertices{0};
};

inline bool worldMaterialRepairCellAvailable(const SWorldMaterialRepairMap& map,int x,int y)
{
    if(x<0 || y<0 || x>=map.field.tileWidth() || y>=map.field.tileHeight())return false;
    if(map.protectedCells.contains(y*map.field.tileWidth()+x))return false;
    if(!map.geometry)return true; // abstract fields have no physical presence mask
    bool available=false;const auto packed=map.geometry->packedTile(x,y,map.layer,&available);
    // An absent overlay returns packed0, which is also a valid active atlas slot.
    return available && !(map.layer==ETerrainTileLayer::Overlay && packed==0xffff);
}

// Select complete shared tile edges only; excluded maps, corner contacts and
// old half-tile placements do not acquire invented raster correspondence.
inline QMap<QString,QMap<int,int>> worldMaterialSeamSides(
        const QVector<SWorldMaterialRepairMap>& maps,const QString& activeMap = {})
{
    QMap<QString,QMap<int,int>> result;
    for(const auto& a:maps)for(const auto& b:maps) {
        if(a.id==b.id || !a.editable || !b.editable)continue;
        if(!activeMap.isEmpty() && a.id!=activeMap && b.id!=activeMap)continue;
        const auto phase=a.origin-b.origin;
        if(qAbs(phase.x()/2-qRound(phase.x()/2))>1e-5
           || qAbs(phase.y()/2-qRound(phase.y()/2))>1e-5)continue;
        const int w=a.field.tileWidth(),h=a.field.tileHeight();
        const QRectF ar(a.origin,QSizeF(w*2,h*2));
        const QRectF br(b.origin,QSizeF(b.field.tileWidth()*2,b.field.tileHeight()*2));
        for(int side=0;side<4;++side) {
            const bool vertical=side==1 || side==3;
            const double fixed=side==0?ar.top():side==1?ar.right():side==2?ar.bottom():ar.left();
            const double other=side==0?br.bottom():side==1?br.left():side==2?br.top():br.right();
            if(qAbs(fixed-other)>1e-5)continue;
            const double low=qMax(vertical?ar.top():ar.left(),vertical?br.top():br.left());
            const double high=qMin(vertical?ar.bottom():ar.right(),vertical?br.bottom():br.right());
            for(int k=0;k<(vertical?h:w);++k) {
                const double start=(vertical?a.origin.y():a.origin.x())+k*2;
                if(start<low-1e-5 || start+2>high+1e-5)continue;
                const int x=vertical?(side==1?w-1:0):k,y=vertical?k:(side==2?h-1:0);
                const int bx=vertical?(side==1?0:b.field.tileWidth()-1):qRound((start-b.origin.x())/2);
                const int by=vertical?qRound((start-b.origin.y())/2):(side==2?0:b.field.tileHeight()-1);
                if(!worldMaterialRepairCellAvailable(a,x,y)
                   || !worldMaterialRepairCellAvailable(b,bx,by))continue;
                result[a.id][y*w+x]|=1<<side;
            }
        }
    }
    return result;
}

// Immutable cohort planning. Material IDs must already use the global catalog;
// physical atlas indices are never compared between maps.
inline SWorldMaterialRepairPlan planWorldMaterialRepair(
        const QVector<SWorldMaterialRepairMap>& maps, bool stitchAlignedEdges = false)
{
    SWorldMaterialRepairPlan plan;
    struct Edge { int map,x,y; QPointF p[3]; qint32 first,last,pure; };
    QVector<Edge> edges;
    const auto point=[stitchAlignedEdges](const SWorldMaterialRepairMap& m,int x,int y) {
        // A snapped tile grid supplies the shared semantic coordinates. SEC
        // offsets deform the artwork, not the identity of a material vertex.
        if(stitchAlignedEdges)return m.origin+QPointF(x,y);
        return m.origin+(m.geometry?m.geometry->terrainVertexPosition(x,y):QPointF(x,y));
    };
    const auto touchesAtEdgeOrCorner=[](const SWorldMaterialRepairMap& a,const SWorldMaterialRepairMap& b) {
        const QRectF ar(a.origin,QSizeF(a.field.tileWidth()*2,a.field.tileHeight()*2));
        const QRectF br(b.origin,QSizeF(b.field.tileWidth()*2,b.field.tileHeight()*2));
        const double overlapX=qMin(ar.right(),br.right())-qMax(ar.left(),br.left());
        const double overlapY=qMin(ar.bottom(),br.bottom())-qMax(ar.top(),br.top());
        return overlapX>=-1e-5 && overlapY>=-1e-5
            && (qAbs(overlapX)<1e-5 || qAbs(overlapY)<1e-5);
    };
    for(int i=0;i<maps.size();++i) {
        const auto& m=maps[i];plan.fields[m.id]=m.field;
        if(stitchAlignedEdges && !m.editable)continue;
        const int w=m.field.tileWidth(),h=m.field.tileHeight();
        for(bool vertical:{false,true})for(int side:{0,1})for(int k=0;k<(vertical?h:w);++k) {
            const int x=vertical?(side?w:0):k,y=vertical?k:(side?h:0);
            Edge e;e.map=i;e.x=vertical?(side?w-1:0):k;e.y=vertical?k:(side?h-1:0);
            if(!worldMaterialRepairCellAvailable(m,e.x,e.y))continue;
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
            bool protectedVertex=false;
            for(int cy:{y-1,y})for(int cx:{x-1,x})
                if(cx>=0 && cy>=0 && cx<w && cy<h
                   && m.protectedCells.contains(cy*w+cx))protectedVertex=true;
            if(protectedVertex)continue;
            const auto p=point(m,2*x,2*y);QSet<qint32> pure,values;bool contact=false;
            bool aligned=true;
            bool ownSurface=m.layer!=ETerrainTileLayer::Overlay || !m.geometry;
            for(const auto& e:edges) {
                // A diagonal map shares the same world vertex at a four-map
                // corner even though it has no positive-length raster seam.
                // Its endpoint is material evidence for vertex reconciliation.
                if(e.map!=i && !touchesAtEdgeOrCorner(m,maps[e.map]))continue;
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
                if(e.map==i)ownSurface=true;
                if(e.map!=i) {
                    contact=true;
                    const auto phase=m.origin-maps[e.map].origin;
                    aligned &= qAbs(phase.x()/2-qRound(phase.x()/2))<1e-5
                        && qAbs(phase.y()/2-qRound(phase.y()/2))<1e-5;
                }
                if(e.pure>0)pure.insert(e.pure);
                if(position<1e-6 && e.first>0)values.insert(e.first);
                else if(position>1-1e-6 && e.last>0)values.insert(e.last);
                else if(e.first==e.last && e.first>0)values.insert(e.first);
            }
            // Neighbor evidence must not fill the unknown corners of an absent
            // surface, even if the neighboring water has a complete signature.
            if(!contact || !ownSurface)continue;
            for(int cy:{y-1,y})for(int cx:{x-1,x})if(worldMaterialRepairCellAvailable(m,cx,cy))
                plan.seamCells[m.id].insert(cy*w+cx);
            const auto& evidence=pure.isEmpty()?values:pure;
            if(evidence.isEmpty() || (evidence.size()!=1 && (!stitchAlignedEdges || !aligned))) {
                plan.conflicts[m.id].append(QPoint(x,y));continue;
            }
            // Resolve a disputed snapped vertex once for the immutable cohort.
            // Pure physical evidence has precedence; the stable global ID
            // breaks a tie without depending on the active map or iteration.
            const auto material=*std::min_element(evidence.cbegin(),evidence.cend());
            if(material==m.field.material(x,y))continue;
            plan.fields[m.id].setMaterial(x,y,material);++plan.changedVertices;
            for(int cy:{y-1,y})for(int cx:{x-1,x})if(worldMaterialRepairCellAvailable(m,cx,cy))
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

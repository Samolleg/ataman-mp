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

#ifndef UNIT_SUPPORT_SURFACE_H
#define UNIT_SUPPORT_SURFACE_H

#include "object_base.h"
#include <QHash>
#include <QSet>
#include <QtMath>

// Authored BASE figure volumes are the runtime's bridge surfaces. Do not
// promote arbitrary visible geometry (tree crowns, props, roofs) to bridges.
// Cache world-space upper faces in an XY grid, independently of render culling.
class CUnitSupportSurface
{
public:
    void clear() { m_faces.clear(); m_cells.clear(); }
    void add(const SNavigationCollisionProxy& proxy)
    {
        if(!proxy.authored || proxy.kind != ENavigationVolumeKind::Bridge
           || proxy.boxCorners.size() != 8) return;
        static const int faces[6][4] = {
            {0,1,3,2}, {0,2,6,4}, {2,3,7,6},
            {4,6,7,5}, {1,5,7,3}, {0,4,5,1}};
        for(int f=0;f<6;++f) if(proxy.upperFaceMask & (1u<<f))
            for(int t=0;t<2;++t) {
                Face face{proxy.boxCorners[faces[f][0]],
                    proxy.boxCorners[faces[f][t ? 2 : 1]],
                    proxy.boxCorners[faces[f][t ? 3 : 2]]};
                const auto normal=QVector3D::crossProduct(face.b-face.a,face.c-face.a);
                if(qAbs(normal.z()) < 1e-6f) continue;
                const int index=m_faces.size(); m_faces.append(face);
                const int x0=qFloor(qMin(face.a.x(),qMin(face.b.x(),face.c.x()))/32);
                const int x1=qFloor(qMax(face.a.x(),qMax(face.b.x(),face.c.x()))/32);
                const int y0=qFloor(qMin(face.a.y(),qMin(face.b.y(),face.c.y()))/32);
                const int y1=qFloor(qMax(face.a.y(),qMax(face.b.y(),face.c.y()))/32);
                for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x)
                    m_cells[key(x,y)].append(index);
            }
    }
    bool height(QVector3D* point, int* tested=nullptr) const
    {
        if(tested)*tested=0;
        bool found=false; float z=point->z();
        for(int index:m_cells.value(key(qFloor(point->x()/32),qFloor(point->y()/32)))) {
            if(tested)++*tested;
            const Face& f=m_faces[index];
            const auto n=QVector3D::crossProduct(f.b-f.a,f.c-f.a);
            const float h=f.a.z()-(n.x()*(point->x()-f.a.x())+n.y()*(point->y()-f.a.y()))/n.z();
            const QVector3D hit(point->x(),point->y(),h);
            if(inside(f,hit) && h>z) { z=h;found=true; }
        }
        if(found)point->setZ(z);
        return found;
    }
    bool intersect(const QVector3D& start,const QVector3D& end,
                   QVector3D* hit,int* tested=nullptr) const
    {
        if(tested)*tested=0;
        const auto ray=end-start; float nearest=1.0f;bool found=false;
        // Walk only cells crossed by the ray, including both sides of grid
        // boundaries. A set prevents testing long bridge faces repeatedly.
        QSet<int> candidates;
        const int steps=qMax(1,qCeil(qMax(qAbs(ray.x()),qAbs(ray.y()))/16));
        for(int i=0;i<=steps;++i) {
            const auto p=start+ray*(float(i)/steps);
            for(int y=qFloor((p.y()-16)/32);y<=qFloor((p.y()+16)/32);++y)
                for(int x=qFloor((p.x()-16)/32);x<=qFloor((p.x()+16)/32);++x)
                    for(int index:m_cells.value(key(x,y)))candidates.insert(index);
        }
        for(int index:candidates) {
            if(tested)++*tested;
            const Face& f=m_faces[index];const auto n=QVector3D::crossProduct(f.b-f.a,f.c-f.a);
            const float divisor=QVector3D::dotProduct(n,ray);
            if(qAbs(divisor)<1e-7f)continue;
            const float t=QVector3D::dotProduct(n,f.a-start)/divisor;
            if(t<0 || t>nearest)continue;
            const auto p=start+ray*t;
            if(inside(f,p)){nearest=t;*hit=p;found=true;}
        }
        return found;
    }
    int faceCount() const {return m_faces.size();}
private:
    struct Face {QVector3D a,b,c;};
    static quint64 key(int x,int y){return (quint64(quint32(x))<<32)|quint32(y);}
    static bool inside(const Face& f,const QVector3D& p) {
        const auto v0=f.b-f.a,v1=f.c-f.a,v2=p-f.a;
        const double d00=QVector3D::dotProduct(v0,v0),d01=QVector3D::dotProduct(v0,v1);
        const double d11=QVector3D::dotProduct(v1,v1),d20=QVector3D::dotProduct(v2,v0),d21=QVector3D::dotProduct(v2,v1);
        const double det=d00*d11-d01*d01;if(qAbs(det)<1e-12)return false;
        const double u=(d11*d20-d01*d21)/det,v=(d00*d21-d01*d20)/det;
        return u>=-1e-5 && v>=-1e-5 && u+v<=1.00001;
    }
    QVector<Face> m_faces;
    QHash<quint64,QVector<int>> m_cells;
};
#endif

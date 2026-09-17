#include "prepared_static_visual.h"
#include <QHash>
#include <QSet>
#include <QtMath>
#include <cmath>

QVector<SVertexData> simplifyStaticMesh(const QVector<SVertexData>& vertices)
{
    if(vertices.size() < 192 || vertices.size() % 3) return vertices;
    QVector3D lo=vertices.first().position, hi=lo;
    for(const auto& v : vertices)
    {
        if(!std::isfinite(v.texCoord.x()) || !std::isfinite(v.texCoord.y())
           || qAbs(v.texCoord.x())>1e6f || qAbs(v.texCoord.y())>1e6f) return vertices;
        for(int axis=0; axis<3; ++axis)
        {
            if(!std::isfinite(v.position[axis]) || !std::isfinite(v.normal[axis])
               || qAbs(v.normal[axis])>2.0f) return vertices;
            lo[axis]=qMin(lo[axis],v.position[axis]);
            hi[axis]=qMax(hi[axis],v.position[axis]);
        }
    }
    const auto extent=hi-lo;
    const float cell=qMax(extent.x(),qMax(extent.y(),extent.z()))/24.0f;
    if(cell<1e-5f) return vertices;
    // Quantize position, UV and normal together so opposite faces and texture
    // seams never collapse into one cluster. Bound displacement to one cell.
    QHash<QString,int> clusters;
    QVector<SVertexData> representatives;
    QVector<int> indices; indices.reserve(vertices.size());
    for(const auto& v : vertices)
    {
        QString key;
        for(int axis=0; axis<3; ++axis)
            key+=QString::number(qRound((v.position[axis]-lo[axis])/cell))+',';
        for(int axis=0; axis<3; ++axis)
            key+=QString::number(qRound(v.normal[axis]*4))+',';
        key+=QString::number(qRound(v.texCoord.x()*24))+','
            +QString::number(qRound(v.texCoord.y()*24));
        auto it=clusters.constFind(key);
        int index;
        if(it==clusters.constEnd())
        {
            index=representatives.size(); clusters.insert(key,index);
            representatives.append(v);
        }
        else
        {
            index=it.value();
            const auto boundaryCount=[&](const SVertexData& point) {
                int count=0;
                for(int axis=0;axis<3;++axis)
                    if(extent[axis]>1e-5f && (point.position[axis]==lo[axis] || point.position[axis]==hi[axis])) ++count;
                return count;
            };
            if(boundaryCount(v)>boundaryCount(representatives[index])) representatives[index]=v;
        }
        indices.append(index);
    }
    QVector<SVertexData> reduced;
    QSet<QString> triangles;
    for(int i=0; i<indices.size(); i+=3)
    {
        const int a=indices[i], b=indices[i+1], c=indices[i+2];
        if(a==b || b==c || a==c) continue;
        const auto& va=representatives[a];
        const auto& vb=representatives[b];
        const auto& vc=representatives[c];
        if(QVector3D::crossProduct(vb.position-va.position,vc.position-va.position).lengthSquared()<1e-12f) continue;
        const QString key=QString::number(a)+','+QString::number(b)+','+QString::number(c);
        if(triangles.contains(key)) continue;
        triangles.insert(key);
        reduced << va << vb << vc;
    }
    // Never replace a silhouette by an empty/degenerate mesh or pay for a
    // second GPU mesh when the reduction is negligible.
    return reduced.size()>=3 && reduced.size()<vertices.size()*0.9 ? reduced : vertices;
}

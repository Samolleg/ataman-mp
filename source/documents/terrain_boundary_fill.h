#ifndef TERRAIN_BOUNDARY_FILL_H
#define TERRAIN_BOUNDARY_FILL_H

#include <QMap>
#include <QVector>
#include <QPoint>
#include <cmath>
#include <algorithm>

// Discrete Dirichlet fill: retain the outermost lattice vertices, solve the
// interior Laplace equation. Unlike a local smoothing pass, the target depends
// only on the contour, not on the original bumps or holes inside it.
namespace TerrainBoundaryFill {
struct Node {
    double before{0};
    QVector<int> neighbors;
};
inline bool solve(const QVector<Node>& nodes, QVector<double>* heights)
{
    const int n=nodes.size();
    heights->resize(n);
    QVector<int> unknown(n,-1), vertices;
    for(int i=0;i<n;++i) {
        (*heights)[i]=nodes[i].before;
        if(nodes[i].neighbors.size()==4) { unknown[i]=vertices.size(); vertices.append(i); }
    }
    const int count=vertices.size();
    if(!count) return true;
    QVector<double> x(count), b(count,0), r(count), p(count), ap(count);
    for(int j=0;j<count;++j) {
        const auto& node=nodes[vertices[j]];
        x[j]=node.before;
        for(int k:node.neighbors) if(unknown[k]<0) b[j]+=nodes[k].before;
    }
    const auto multiply=[&](const QVector<double>& v,QVector<double>& out) {
        for(int j=0;j<count;++j) {
            double value=4*v[j];
            for(int k:nodes[vertices[j]].neighbors) if(unknown[k]>=0) value-=v[unknown[k]];
            out[j]=value;
        }
    };
    multiply(x,ap);
    double rr=0;
    for(int j=0;j<count;++j) {r[j]=p[j]=b[j]-ap[j];rr+=r[j]*r[j];}
    // Absolute accuracy well below one raw-height quantum, also for world units.
    const double tolerance=1e-14;
    for(int iteration=0;rr>tolerance && iteration<2048;++iteration) {
        multiply(p,ap);
        double pap=0;for(int j=0;j<count;++j) pap+=p[j]*ap[j];
        if(!(pap>0) || !std::isfinite(pap)) return false;
        const double alpha=rr/pap;
        double next=0;
        for(int j=0;j<count;++j) {x[j]+=alpha*p[j];r[j]-=alpha*ap[j];next+=r[j]*r[j];}
        const double beta=next/rr;
        for(int j=0;j<count;++j) p[j]=r[j]+beta*p[j];
        rr=next;
    }
    if(rr>tolerance || !std::isfinite(rr)) return false;
    for(int j=0;j<count;++j) (*heights)[vertices[j]]=x[j];
    return true;
}

// A missing neighbor (map edge, hole in an overlay, or outside the circle)
// fixes this vertex as part of the contour. No inferred heights beyond data.
template<class Sample>
QMap<QPair<int,int>,double> grid(double cx,double cy,double radius,
                               int width,int height,Sample sample,
                               bool square=false)
{
    using XY=QPair<int,int>;
    QMap<XY,int> indices;
    QVector<Node> nodes;
    if(!std::isfinite(cx)||!std::isfinite(cy)||!std::isfinite(radius)||radius<=0
       ||width<=0||height<=0) return {};
    const int x0=int(qBound(0.0,std::floor(cx-radius),double(width-1)));
    const int x1=int(qBound(0.0,std::ceil(cx+radius),double(width-1)));
    const int y0=int(qBound(0.0,std::floor(cy-radius),double(height-1)));
    const int y1=int(qBound(0.0,std::ceil(cy+radius),double(height-1)));
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        const double dx=std::fabs(x-cx),dy=std::fabs(y-cy);
        if(square ? qMax(dx,dy)>radius : std::hypot(dx,dy)>radius) continue;
        double value=0;if(!sample(x,y,&value)||!std::isfinite(value)) continue;
        indices[{x,y}]=nodes.size();Node node;node.before=value;nodes.append(node);
    }
    const XY steps[]={{-1,0},{1,0},{0,-1},{0,1}};
    for(auto it=indices.cbegin();it!=indices.cend();++it)
        for(const auto& step:steps) {
            auto neighbor=indices.constFind({it.key().first+step.first,it.key().second+step.second});
            if(neighbor!=indices.cend()) nodes[it.value()].neighbors.append(neighbor.value());
        }
    QVector<double> solved;
    if(!solve(nodes,&solved)) return {};
    QMap<XY,double> result;
    for(auto it=indices.cbegin();it!=indices.cend();++it)
        if(nodes[it.value()].neighbors.size()==4) result[it.key()]=solved[it.value()];
    return result;
}
}
#endif

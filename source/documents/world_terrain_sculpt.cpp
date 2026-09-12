#include "world_terrain_sculpt.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using XY = QPair<double, double>;
struct Vertex { int map; int x; int y; };
double height(const SWorldTerrainTarget& t, int x, int y)
{ return double(t.model->height(x,y)) * t.model->maxZ() / 65535.0; }
XY point(const SWorldTerrainTarget& t, int x, int y)
{
    const QPointF p = t.model->terrainVertexPosition(x,y);
    return qMakePair(t.origin.x()+p.x(), t.origin.y()+p.y());
}
double mean(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    double sum = 0;
    for(double v : values) sum += v;
    return sum / values.size();
}

struct Segment { int a,b; XY p,q; };
double cross(const XY& a,const XY& b) { return a.first*b.second-a.second*b.first; }
XY subtract(const XY& a,const XY& b) { return {a.first-b.first,a.second-b.second}; }

void reconcileShiftedEdges(const QVector<SWorldTerrainTarget>& targets,
                          const QVector<QVector<Vertex>>& groups,
                          QVector<double>& proposed,const QVector<double>& limits,
                          QVector<bool>& affected)
{
    QVector<QMap<QPair<int,int>,int>> indices(targets.size());
    QVector<XY> positions(groups.size());
    QVector<int> parent(groups.size());
    for(int i=0;i<groups.size();++i)
    {
        parent[i]=i;
        const auto v=groups[i].first();positions[i]=point(targets[v.map],v.x,v.y);
        if(!std::isfinite(proposed[i])) continue;
        for(const auto& vertex:groups[i])
            indices[vertex.map][{vertex.x,vertex.y}]=i;
    }
    const auto root=[&](int i) { while(parent[i]!=i) {parent[i]=parent[parent[i]];i=parent[i];}return i; };
    const auto join=[&](int a,int b) {a=root(a);b=root(b);if(a!=b) parent[qMax(a,b)]=qMin(a,b);};
    QVector<QVector<Segment>> sides(targets.size()*4);
    for(int m=0;m<targets.size();++m)
    {
        if(indices[m].isEmpty()) continue;
        const int w=targets[m].model->vertexCountX(),h=targets[m].model->vertexCountY();
        // Trace complete enabled-map boundaries, including outside the brush.
        // Only connected components reached by this stamp are reconciled.
        for(auto it=indices[m].cbegin();it!=indices[m].cend();++it)
        {
            const int x=it.key().first,y=it.key().second;
            for(int side=0;side<4;++side)
            {
                const bool vertical=side<2;
                if((side==0&&x!=0)||(side==1&&x!=w-1)
                   ||(side==2&&y!=0)||(side==3&&y!=h-1)) continue;
                const QPair<int,int> next(x+(vertical?0:1),y+(vertical?1:0));
                const auto end=indices[m].constFind(next);
                if(end==indices[m].cend()) continue;
                sides[m*4+side].append({it.value(),end.value(),point(targets[m],x,y),
                                      point(targets[m],next.first,next.second)});
            }
        }
    }
    QVector<double> longest(sides.size(),0);
    for(int side=0;side<sides.size();++side)
    {
        const auto coordinate=[&](const XY& p){return side%4<2?p.second:p.first;};
        for(auto& s:sides[side])
        {
            if(coordinate(s.q)<coordinate(s.p)) {qSwap(s.a,s.b);qSwap(s.p,s.q);}
            longest[side]=qMax(longest[side],coordinate(s.q)-coordinate(s.p));
        }
        std::sort(sides[side].begin(),sides[side].end(),[&](const Segment& a,const Segment& b){return coordinate(a.p)<coordinate(b.p);});
    }
    for(int a=0;a<sides.size();++a) for(int b=a+1;b<sides.size();++b)
    {
        if(a/4==b/4 || ((a%4)^1)!=b%4) continue;
        const bool vertical=a%4<2;
        const auto coordinate=[&](const XY& p){return vertical?p.second:p.first;};
        int first=0;
        for(const auto& s:sides[a])
        {
          while(first<sides[b].size() && coordinate(sides[b][first].p)+longest[b]<coordinate(s.p)) ++first;
          for(int j=first;j<sides[b].size() && coordinate(sides[b][j].p)<coordinate(s.q);++j)
          {
            const auto& t=sides[b][j];
            const double lo=qMax(coordinate(s.p),coordinate(t.p));
            const double hi=qMin(coordinate(s.q),coordinate(t.q));
            const XY direction=subtract(s.q,s.p);
            const double length=std::hypot(direction.first,direction.second);
            if(hi-lo>1e-9 && length>1e-9
               && qAbs(cross(direction,subtract(t.p,s.p)))<=1e-9*length
               && qAbs(cross(direction,subtract(t.q,s.p)))<=1e-9*length
               && !(s.a==t.a && s.b==t.b))
            {
                // With staggered knots, equality of both piecewise-linear
                // profiles requires one affine profile across this chain.
                join(s.a,s.b);join(s.a,t.a);join(s.a,t.b);
            }
          }
        }
    }
    QMap<int,QVector<int>> components;
    for(int i=0;i<groups.size();++i) if(std::isfinite(proposed[i])) components[root(i)].append(i);
    for(auto component:components)
    {
        if(component.size()<3) continue;
        bool touched=false;
        for(int i:component) touched=touched||affected[i];
        if(!touched) continue;
        // Geometric ordering makes fitting independent of target enumeration.
        std::sort(component.begin(),component.end(),[&](int a,int b){return positions[a]<positions[b];});
        const XY anchor=positions[component.first()];
        double mx=0,my=0,mz=0,limit=std::numeric_limits<double>::max();
        for(int i:component)
        { mx+=positions[i].first-anchor.first;my+=positions[i].second-anchor.second;mz+=proposed[i];limit=qMin(limit,limits[i]); }
        mx/=component.size();my/=component.size();mz/=component.size();
        double xx=0,yy=0,xy=0,xz=0,yz=0;
        for(int i:component)
        {
            double x=positions[i].first-anchor.first-mx,y=positions[i].second-anchor.second-my,z=proposed[i]-mz;
            xx+=x*x;yy+=y*y;xy+=x*y;xz+=x*z;yz+=y*z;
        }
        double sx=0,sy=0,det=xx*yy-xy*xy;
        if(det>1e-12*(xx+yy)*(xx+yy)) {sx=(xz*yy-yz*xy)/det;sy=(yz*xx-xz*xy)/det;}
        else if(xx>=yy && xx>0) sx=xz/xx;
        else if(yy>0) sy=yz/yy;
        const double base=qBound(0.0,mz,limit);
        double scale=1;
        for(int i:component)
        {
            const double delta=sx*(positions[i].first-anchor.first-mx)+sy*(positions[i].second-anchor.second-my);
            if(delta>0) scale=qMin(scale,(limit-base)/delta);
            if(delta<0) scale=qMin(scale,-base/delta);
        }
        for(int i:component)
        {
            proposed[i]=base+scale*(sx*(positions[i].first-anchor.first-mx)+sy*(positions[i].second-anchor.second-my));
            affected[i]=true;
        }
    }
}
}

void SWorldTerrainStroke::append(const SWorldTerrainStroke& stamp)
{
    for(auto it=stamp.maps.cbegin(); it!=stamp.maps.cend(); ++it)
        appendTerrainHeightCommand(&maps[it.key()], it.value());
}

SWorldTerrainStroke makeWorldTerrainStamp(
    const QVector<SWorldTerrainTarget>& targets, const STerrainBrush& brush,
    EWorldSculptMode mode, double worldDelta, double worldFlatten, double strength)
{
    SWorldTerrainStroke result;
    if(!std::isfinite(brush.centerX) || !std::isfinite(brush.centerY)
       || !std::isfinite(brush.radius) || brush.radius < 0
       || !std::isfinite(worldDelta) || !std::isfinite(worldFlatten)
       || !std::isfinite(strength) || strength<=0) return result;
    QMap<XY, QVector<Vertex>> edges;
    QVector<QVector<Vertex>> groups;
    for(int i=0; i<targets.size(); ++i)
    {
        const auto& t=targets[i];
        if(!t.editable || !t.model || !(t.model->maxZ()>0) || !std::isfinite(t.model->maxZ())
           || !std::isfinite(t.origin.x()) || !std::isfinite(t.origin.y())) continue;
        const int w=t.model->vertexCountX(), h=t.model->vertexCountY();
        const auto edge=[&](int x,int y){edges[point(t,x,y)].append({i,x,y});};
        for(int x=0;x<w;++x) {edge(x,0);if(h>1) edge(x,h-1);}
        for(int y=1;y<h-1;++y) {edge(0,y);if(w>1) edge(w-1,y);}
        // Packed XY offsets are bounded by half a grid step.
        const int x0=qMax(0,int(std::floor(brush.centerX-t.origin.x()-brush.radius-1)));
        const int x1=qMin(w-1,int(std::ceil(brush.centerX-t.origin.x()+brush.radius+1)));
        const int y0=qMax(0,int(std::floor(brush.centerY-t.origin.y()-brush.radius-1)));
        const int y1=qMin(h-1,int(std::ceil(brush.centerY-t.origin.y()+brush.radius+1)));
        for(int y=y0; y<=y1; ++y) for(int x=x0; x<=x1; ++x)
        {
            if(x==0 || y==0 || x==w-1 || y==h-1) continue;
            const XY p=point(t,x,y);
            if(std::hypot(p.first-brush.centerX,p.second-brush.centerY)>brush.radius) continue;
            Vertex v{i,x,y};
            groups.append(QVector<Vertex>{v});
        }
    }
    for(const auto& edge : edges) groups.append(edge);
    QVector<double> proposed(groups.size(),std::numeric_limits<double>::quiet_NaN()),limits(groups.size());
    QVector<bool> affected(groups.size(),false);
    for(int groupIndex=0;groupIndex<groups.size();++groupIndex)
    {
        const auto& group=groups[groupIndex];
        const auto& first=group.first();
        const XY p=point(targets[first.map],first.x,first.y);
        const double distance=std::hypot(p.first-brush.centerX,p.second-brush.centerY);
        double weight=1;
        if(brush.radius>0 && brush.falloff!=ETerrainBrushFalloff::Constant)
        {
            weight=qBound(0.0,1.0-distance/brush.radius,1.0);
            if(brush.falloff==ETerrainBrushFalloff::Smooth) weight=weight*weight*(3-2*weight);
        }
        QVector<double> initial;
        double limit=std::numeric_limits<double>::max();
        for(const auto& v:group)
        {
            initial.append(height(targets[v.map],v.x,v.y));
            limit=qMin(limit,double(targets[v.map].model->maxZ()));
        }
        const double before=mean(initial);
        proposed[groupIndex]=before;limits[groupIndex]=limit;
        if(distance>brush.radius || weight<=0) continue;
        affected[groupIndex]=true;
        double after=before;
        if(mode==EWorldSculptMode::Raise || mode==EWorldSculptMode::Lower)
            after+= (mode==EWorldSculptMode::Raise ? 1 : -1)*worldDelta*weight*strength;
        else
        {
            double target=worldFlatten;
            if(mode==EWorldSculptMode::Smooth)
            {
                QMap<XY,QVector<double>> samples;
                for(const auto& v:group)
                {
                    const auto& t=targets[v.map];
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                    {
                        const int x=v.x+dx,y=v.y+dy;
                        if(x<0 || y<0 || x>=t.model->vertexCountX() || y>=t.model->vertexCountY()) continue;
                        samples[point(t,x,y)].append(height(t,x,y));
                    }
                }
                QVector<double> unique;
                for(const auto& sample:samples) unique.append(mean(sample));
                target=mean(unique);
            }
            after+=(target-before)*weight*qBound(0.0,strength,1.0);
        }
        // The shared target is representable within each map's existing range.
        // Each file rounds only once to its own uint16 height quantum.
        after=qBound(0.0,after,limit);
        proposed[groupIndex]=after;limits[groupIndex]=limit;
    }
    reconcileShiftedEdges(targets,groups,proposed,limits,affected);
    for(int groupIndex=0;groupIndex<groups.size();++groupIndex)
    {
        const auto& group=groups[groupIndex];
        const double after=proposed[groupIndex];
        if(!affected[groupIndex] || !std::isfinite(after)) continue;
        for(const auto& v:group)
        {
            const auto& t=targets[v.map];
            STerrainHeightDelta delta;
            delta.globalX=v.x; delta.globalY=v.y;
            delta.before=t.model->height(v.x,v.y);
            delta.after=quint16(qBound(0,qRound(after*65535.0/t.model->maxZ()),65535));
            if(delta.before==delta.after) continue;
            auto& command=result.maps[t.id];
            command.description=QStringLiteral("World terrain stroke");
            command.deltas.append(delta);
        }
    }
    return result;
}

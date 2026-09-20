#include "world_terrain_sculpt.h"
#include "terrain_boundary_fill.h"
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

SWorldTerrainStroke boundaryFillClipped(const QVector<SWorldTerrainTarget>& targets,
                                const STerrainBrush& brush,double strength)
{
    QMap<XY,QVector<Vertex>> positions;
    for(int m=0;m<targets.size();++m) {
        const auto& t=targets[m];
        if(!t.editable || !t.model || !(t.model->maxZ()>0) || !std::isfinite(t.model->maxZ())
           || !std::isfinite(t.origin.x()) || !std::isfinite(t.origin.y())) continue;
        const int w=t.model->vertexCountX(),h=t.model->vertexCountY();
        if(w<=0 || h<=0) continue;
        const int x0=int(qBound(0.0,std::floor(brush.centerX-t.origin.x()-brush.radius-1),double(w-1)));
        const int x1=int(qBound(0.0,std::ceil(brush.centerX-t.origin.x()+brush.radius+1),double(w-1)));
        const int y0=int(qBound(0.0,std::floor(brush.centerY-t.origin.y()-brush.radius-1),double(h-1)));
        const int y1=int(qBound(0.0,std::ceil(brush.centerY-t.origin.y()+brush.radius+1),double(h-1)));
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            const auto p=point(t,x,y);
            if(std::hypot(p.first-brush.centerX,p.second-brush.centerY)<=brush.radius)
                positions[p].append({m,x,y});
        }
    }
    QVector<QVector<Vertex>> groups;
    QVector<TerrainBoundaryFill::Node> nodes;
    QVector<QMap<QPair<int,int>,int>> lookup(targets.size());
    for(const auto& group:positions) {
        QVector<double> values;
        for(const auto& v:group) {
            lookup[v.map][{v.x,v.y}]=nodes.size();
            values.append(height(targets[v.map],v.x,v.y));
        }
        TerrainBoundaryFill::Node node;node.before=mean(values);
        nodes.append(node);groups.append(group);
    }
    const QPair<int,int> steps[]={{-1,0},{1,0},{0,-1},{0,1}};
    for(int i=0;i<nodes.size();++i) {
        QSet<int> neighbors;
        for(const auto& v:groups[i]) for(const auto& step:steps) {
            const auto it=lookup[v.map].constFind({v.x+step.first,v.y+step.second});
            if(it!=lookup[v.map].cend() && it.value()!=i) neighbors.insert(it.value());
        }
        nodes[i].neighbors=neighbors.values().toVector();
        std::sort(nodes[i].neighbors.begin(),nodes[i].neighbors.end());
    }
    QVector<double> solved;
    SWorldTerrainStroke result;
    if(!TerrainBoundaryFill::solve(nodes,&solved)) return result;
    for(int i=0;i<nodes.size();++i) {
        if(nodes[i].neighbors.size()!=4) continue;
        const auto first=groups[i].first();const auto p=point(targets[first.map],first.x,first.y);
        double weight=qBound(0.0,strength,1.0);
        if(brush.falloff!=ETerrainBrushFalloff::Constant && brush.radius>0) {
            double f=qBound(0.0,1-std::hypot(p.first-brush.centerX,p.second-brush.centerY)/brush.radius,1.0);
            if(brush.falloff==ETerrainBrushFalloff::Smooth) f=f*f*(3-2*f);
            weight*=f;
        }
        double limit=std::numeric_limits<double>::max();
        for(const auto& v:groups[i]) limit=qMin(limit,double(targets[v.map].model->maxZ()));
        const double after=qBound(0.0,nodes[i].before+(solved[i]-nodes[i].before)*weight,limit);
        for(const auto& v:groups[i]) {
            const auto& t=targets[v.map];
            STerrainHeightDelta d;d.globalX=v.x;d.globalY=v.y;d.before=t.model->height(v.x,v.y);
            d.after=quint16(qBound(0,qRound(after*65535.0/t.model->maxZ()),65535));
            if(d.after==d.before) continue;
            auto& command=result.maps[t.id];command.description=QStringLiteral("Fill terrain from boundary");
            command.deltas.append(d);
        }
    }
    // Staggered map edges remain fixed contours: do not extend this operation
    // beyond the brush to reconcile a pre-existing seam profile.
    return result;
}

// Sample the visible displaced triangle, recording its vertices as immutable
// contour anchors. Only permitted maps participate; interior map seams are not
// anchors. This is separate from the runtime/navigation height samplers.
bool contourSample(const SWorldTerrainTarget& t,double wx,double wy,double* z,
                   QVector<QPair<int,int>>* anchors)
{
    const double x=wx-t.origin.x(),y=wy-t.origin.y();
    if(x < -0.51 || y < -0.51 || x > t.model->vertexCountX()-0.49
       || y > t.model->vertexCountY()-0.49) return false;
    const int baseX=int(std::floor(x)),baseY=int(std::floor(y));
    for(int cy=qMax(0,baseY-1);cy<=qMin(t.model->vertexCountY()-2,baseY+1);++cy)
        for(int cx=qMax(0,baseX-1);cx<=qMin(t.model->vertexCountX()-2,baseX+1);++cx) {
            const QPair<int,int> corners[]={{cx,cy},{cx+1,cy},{cx+1,cy+1},{cx,cy+1}};
            const int triangles[2][3]={{0,1,2},{0,2,3}};
            for(const auto& triangle:triangles) {
                const auto a=corners[triangle[0]],b=corners[triangle[1]],c=corners[triangle[2]];
                const QPointF pa=t.model->terrainVertexPosition(a.first,a.second);
                const QPointF pb=t.model->terrainVertexPosition(b.first,b.second);
                const QPointF pc=t.model->terrainVertexPosition(c.first,c.second);
                const double det=(pb.x()-pa.x())*(pc.y()-pa.y())-(pb.y()-pa.y())*(pc.x()-pa.x());
                if(qAbs(det)<1e-12) continue;
                const double u=((x-pa.x())*(pc.y()-pa.y())-(y-pa.y())*(pc.x()-pa.x()))/det;
                const double v=((pb.x()-pa.x())*(y-pa.y())-(pb.y()-pa.y())*(x-pa.x()))/det;
                if(u < -1e-8 || v < -1e-8 || u+v > 1+1e-8) continue;
                *z=(1-u-v)*height(t,a.first,a.second)+u*height(t,b.first,b.second)+v*height(t,c.first,c.second);
                anchors->append(a);anchors->append(b);anchors->append(c);
                return true;
            }
        }
    return false;
}

SWorldTerrainStroke boundaryFill(const QVector<SWorldTerrainTarget>& targets,
                                const STerrainBrush& brush,double strength)
{
    if(brush.radius<=0) return {};
    QVector<int> maps;
    for(int m=0;m<targets.size();++m) {
        const auto& t=targets[m];
        if(t.editable && t.model && t.model->maxZ()>0 && std::isfinite(t.model->maxZ())
           && std::isfinite(t.origin.x()) && std::isfinite(t.origin.y())) maps.append(m);
    }
    const double pi=std::acos(-1.0);
    const int count=qBound(64,int(std::ceil(qMin(double(brush.radius),64.0)*8*pi)),2048);
    QVector<QPointF> ring;
    QVector<double> ringHeights;
    QVector<QSet<QPair<int,int>>> frozen(targets.size());
    for(int i=0;i<count;++i) {
        const double angle=2*pi*i/count;
        const QPointF p(brush.centerX+brush.radius*std::cos(angle),brush.centerY+brush.radius*std::sin(angle));
        QVector<double> values;
        for(int m:maps) {
            double z=0;QVector<QPair<int,int>> anchors;
            if(!contourSample(targets[m],p.x(),p.y(),&z,&anchors)) continue;
            values.append(z);for(const auto& anchor:anchors) frozen[m].insert(anchor);
        }
        // An incomplete circle at the edge of available data uses the existing
        // clipped-domain fill. Never invent samples from an unapproved map.
        if(values.isEmpty()) return boundaryFillClipped(targets,brush,strength);
        ring.append(p);ringHeights.append(mean(values));
    }
    SWorldTerrainStroke result;
    const double innerRadius=brush.radius*std::cos(pi/count);
    QSet<XY> frozenPositions;
    QMap<XY,double> limits;
    for(int m:maps) {
        const auto& t=targets[m];
        for(const auto& anchor:frozen[m]) frozenPositions.insert(point(t,anchor.first,anchor.second));
        const int w=t.model->vertexCountX(),h=t.model->vertexCountY();
        if(w<=0 || h<=0) continue;
        const int x0=int(qBound(0.0,std::floor(brush.centerX-t.origin.x()-brush.radius-1),double(w-1)));
        const int x1=int(qBound(0.0,std::ceil(brush.centerX-t.origin.x()+brush.radius+1),double(w-1)));
        const int y0=int(qBound(0.0,std::floor(brush.centerY-t.origin.y()-brush.radius-1),double(h-1)));
        const int y1=int(qBound(0.0,std::ceil(brush.centerY-t.origin.y()+brush.radius+1),double(h-1)));
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            const auto p=point(t,x,y);
            limits[p]=qMin(limits.value(p,std::numeric_limits<double>::max()),double(t.model->maxZ()));
        }
    }
    QVector<double> distances(count),tangents(count);
    QVector<QPointF> vectors(count);
    for(int m:maps) {
        const auto& t=targets[m];const int w=t.model->vertexCountX(),h=t.model->vertexCountY();
        if(w<=0 || h<=0) continue;
        const int x0=int(qBound(0.0,std::floor(brush.centerX-t.origin.x()-brush.radius-1),double(w-1)));
        const int x1=int(qBound(0.0,std::ceil(brush.centerX-t.origin.x()+brush.radius+1),double(w-1)));
        const int y0=int(qBound(0.0,std::floor(brush.centerY-t.origin.y()-brush.radius-1),double(h-1)));
        const int y1=int(qBound(0.0,std::ceil(brush.centerY-t.origin.y()+brush.radius+1),double(h-1)));
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            const auto xy=point(t,x,y);const QPointF p(xy.first,xy.second);
            const double distance=std::hypot(p.x()-brush.centerX,p.y()-brush.centerY);
            if(distance>=innerRadius || frozenPositions.contains(xy)) continue;
            // Mean-value coordinates reproduce affine slopes and interpolate
            // all contour heights. Interior heights cannot influence the target.
            for(int i=0;i<count;++i) {vectors[i]=ring[i]-p;distances[i]=std::hypot(vectors[i].x(),vectors[i].y());}
            for(int i=0;i<count;++i) {
                const int j=(i+1)%count;
                tangents[i]=(vectors[i].x()*vectors[j].y()-vectors[i].y()*vectors[j].x())
                    /(distances[i]*distances[j]+QPointF::dotProduct(vectors[i],vectors[j]));
            }
            double total=0,sum=0;
            for(int i=0;i<count;++i) {
                const double weight=(tangents[(i+count-1)%count]+tangents[i])/distances[i];
                total+=weight;sum+=weight*ringHeights[i];
            }
            if(!(total>0) || !std::isfinite(sum)) continue;
            double blend=qBound(0.0,strength,1.0);
            if(brush.falloff!=ETerrainBrushFalloff::Constant) {
                double f=qBound(0.0,1-distance/brush.radius,1.0);
                if(brush.falloff==ETerrainBrushFalloff::Smooth) f=f*f*(3-2*f);
                blend*=f;
            }
            const double target=qBound(0.0,sum/total,limits.value(xy));
            const double before=height(t,x,y),after=before+(target-before)*blend;
            STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=t.model->height(x,y);
            d.after=quint16(qBound(0,qRound(after*65535.0/t.model->maxZ()),65535));
            if(d.after==d.before) continue;
            auto& command=result.maps[t.id];command.description=QStringLiteral("Fill terrain from boundary");
            command.deltas.append(d);
        }
    }
    return result;
}

double cross(const XY& a,const XY& b) { return a.first*b.second-a.second*b.first; }
XY subtract(const XY& a,const XY& b) { return {a.first-b.first,a.second-b.second}; }

int protectShiftedEdges(const QVector<SWorldTerrainTarget>& targets,
                          const QVector<QVector<Vertex>>& groups,
                          const QVector<double>& proposed,
                          QVector<bool>& affected)
{
    QVector<QMap<QPair<int,int>,int>> indices(targets.size());
    QVector<int> parent(groups.size());
    for(int i=0;i<groups.size();++i)
    {
        parent[i]=i;
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
        // Trace full boundaries to identify incompatible staggered chains.
        // This discovery must never enlarge the write footprint.
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
                // Different knots on the same edge: retain the shared chain.
                // An affine refit would destroy remote terrain features.
                join(s.a,s.b);join(s.a,t.a);join(s.a,t.b);
            }
          }
        }
    }
    QMap<int,QVector<int>> components;
    for(int i=0;i<groups.size();++i) if(std::isfinite(proposed[i])) components[root(i)].append(i);
    int protectedVertices=0;
    for(const auto& component:components)
    {
        if(component.size()<3) continue;
        // Staggered piecewise-linear edges cannot acquire a local, identical
        // height profile without changing their knots. Never flatten the whole
        // connected rim to achieve equality. Preserve its original samples,
        // including any pre-existing mismatch, and edit only the interior.
        for(int i:component) {
            if(affected[i]) ++protectedVertices;
            affected[i]=false;
        }
    }
    return protectedVertices;
}
}

void SWorldTerrainStroke::append(const SWorldTerrainStroke& stamp)
{
    protectedBoundaryVertices += stamp.protectedBoundaryVertices;
    for(auto it=stamp.paint.cbegin();it!=stamp.paint.cend();++it) {
        if(paint.contains(it.key()))paint[it.key()].after=it.value().after;
        else paint.insert(it.key(),it.value());
    }

    for(auto it=stamp.maps.cbegin(); it!=stamp.maps.cend(); ++it)
        appendTerrainHeightCommand(&maps[it.key()], it.value());
    for(auto it=stamp.overlays.cbegin();it!=stamp.overlays.cend();++it)
        appendOverlaySurfaceCommand(&overlays[it.key()],it.value());
    for(auto it=stamp.tiles.cbegin();it!=stamp.tiles.cend();++it)
        appendTerrainTileCommand(&tiles[it.key()],it.value());
}

SWorldTerrainStroke makeWorldTerrainStamp(
    const QVector<SWorldTerrainTarget>& targets, const STerrainBrush& brush,
    EWorldSculptMode mode, double worldDelta, double worldFlatten, double strength, quint32 noiseSeed)
{
    SWorldTerrainStroke result;
    if(!std::isfinite(brush.centerX) || !std::isfinite(brush.centerY)
       || !std::isfinite(brush.radius) || brush.radius < 0
       || !std::isfinite(worldDelta) || !std::isfinite(worldFlatten)
       || !std::isfinite(strength) || strength<=0) return result;
    if(mode==EWorldSculptMode::BoundaryFill) return boundaryFill(targets,brush,strength);
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
    QVector<double> proposed(groups.size(),std::numeric_limits<double>::quiet_NaN());
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
        proposed[groupIndex]=before;
        if(distance>brush.radius || weight<=0) continue;
        affected[groupIndex]=true;
        double after=before;
        if(mode==EWorldSculptMode::Raise || mode==EWorldSculptMode::Lower)
            after+= (mode==EWorldSculptMode::Raise ? 1 : -1)*worldDelta*weight*strength;
        else if(mode==EWorldSculptMode::Noise) {
            quint32 hash=noiseSeed ^ quint32(qRound64(p.first*1024))*73856093u ^ quint32(qRound64(p.second*1024))*19349663u;
            hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
            after+=(double(hash)/double(0xffffffffu)*2-1)*worldDelta*weight*strength;
        }
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
        proposed[groupIndex]=after;
    }
    result.protectedBoundaryVertices=protectShiftedEdges(targets,groups,proposed,affected);
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

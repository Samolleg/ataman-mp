#include "world_terrain_sculpt.h"
#include "terrain_boundary_fill.h"
#include "endurance_document.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using XY = QPair<double, double>;
struct Vertex { int map; int x; int y; };
using VertexKey = QPair<int,QPair<int,int>>;
using RimKeys = QMap<VertexKey,XY>;
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

bool touching(const SWorldTerrainTarget& a,const SWorldTerrainTarget& b)
{
    if(!a.model || !b.model) return false;
    const double dx=qMin(a.origin.x()+a.model->vertexCountX()-1,b.origin.x()+b.model->vertexCountX()-1)-qMax(a.origin.x(),b.origin.x());
    const double dy=qMin(a.origin.y()+a.model->vertexCountY()-1,b.origin.y()+b.model->vertexCountY()-1)-qMax(a.origin.y(),b.origin.y());
    return dx>=-1e-8 && dy>=-1e-8 && (qAbs(dx)<1e-8 || qAbs(dy)<1e-8);
}
XY sharedPoint(const SWorldTerrainTarget& t,int map,int x,int y,const RimKeys& keys)
{
    const auto it=keys.constFind({map,{x,y}});
    return it==keys.cend()?point(t,x,y):it.value();
}

SWorldTerrainStroke boundaryFillClipped(const QVector<SWorldTerrainTarget>& targets,
                                const STerrainBrush& brush,double strength,const RimKeys& rimKeys)
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
                positions[sharedPoint(t,m,x,y,rimKeys)].append({m,x,y});
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
                                const STerrainBrush& brush,double strength,const RimKeys& rimKeys)
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
        if(values.isEmpty()) return boundaryFillClipped(targets,brush,strength,rimKeys);
        ring.append(p);ringHeights.append(mean(values));
    }
    SWorldTerrainStroke result;
    const double innerRadius=brush.radius*std::cos(pi/count);
    QSet<XY> frozenPositions;
    QMap<XY,double> limits;
    for(int m:maps) {
        const auto& t=targets[m];
        for(const auto& anchor:frozen[m]) frozenPositions.insert(sharedPoint(t,m,anchor.first,anchor.second,rimKeys));
        const int w=t.model->vertexCountX(),h=t.model->vertexCountY();
        if(w<=0 || h<=0) continue;
        const int x0=int(qBound(0.0,std::floor(brush.centerX-t.origin.x()-brush.radius-1),double(w-1)));
        const int x1=int(qBound(0.0,std::ceil(brush.centerX-t.origin.x()+brush.radius+1),double(w-1)));
        const int y0=int(qBound(0.0,std::floor(brush.centerY-t.origin.y()-brush.radius-1),double(h-1)));
        const int y1=int(qBound(0.0,std::ceil(brush.centerY-t.origin.y()+brush.radius+1),double(h-1)));
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            const auto key=sharedPoint(t,m,x,y,rimKeys);
            limits[key]=qMin(limits.value(key,std::numeric_limits<double>::max()),double(t.model->maxZ()));
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
            const auto xy=sharedPoint(t,m,x,y,rimKeys);const QPointF p(xy.first,xy.second);
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
                          QVector<bool>& affected,const RimKeys& rimKeys)
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
            QSet<int> maps;
            bool knitted=!groups[i].isEmpty();
            for(const auto& v:groups[i]) {
                maps.insert(v.map);
                if(!rimKeys.contains({v.map,{v.x,v.y}})) knitted=false;
            }
            // A locally knitted knot already has one shared sculpt target for
            // every incident map. Freezing it would leave the quarter-shifted
            // rim unchanged while only the nearby interiors move.
            if(knitted && maps.size()>1) continue;
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

static SWorldTerrainStroke sculptPreparedWorld(
    const QVector<SWorldTerrainTarget>& targets, const STerrainBrush& brush,
    EWorldSculptMode mode, double worldDelta, double worldFlatten, double strength, quint32 noiseSeed,
    const RimKeys& rimKeys,
    const std::function<double(double,double)>& influence,
    const std::function<double(double,double,double)>& transform)
{
    SWorldTerrainStroke result;
    if(!std::isfinite(brush.centerX) || !std::isfinite(brush.centerY)
       || !std::isfinite(brush.radius) || brush.radius < 0
       || !std::isfinite(worldDelta) || !std::isfinite(worldFlatten)
       || !std::isfinite(strength) || strength<=0) return result;
    if(mode==EWorldSculptMode::BoundaryFill) return boundaryFill(targets,brush,strength,rimKeys);
    QMap<XY, QVector<Vertex>> edges;
    QVector<QVector<Vertex>> groups;
    for(int i=0; i<targets.size(); ++i)
    {
        const auto& t=targets[i];
        if(!t.editable || !t.model || !(t.model->maxZ()>0) || !std::isfinite(t.model->maxZ())
           || !std::isfinite(t.origin.x()) || !std::isfinite(t.origin.y())) continue;
        const int w=t.model->vertexCountX(), h=t.model->vertexCountY();
        const auto edge=[&](int x,int y){edges[sharedPoint(t,i,x,y,rimKeys)].append({i,x,y});};
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
        const XY p=sharedPoint(targets[first.map],first.map,first.x,first.y,rimKeys);
        const double distance=std::hypot(p.first-brush.centerX,p.second-brush.centerY);
        double weight=1;
        if(brush.radius>0 && brush.falloff!=ETerrainBrushFalloff::Constant)
        {
            weight=qBound(0.0,1.0-distance/brush.radius,1.0);
            if(brush.falloff==ETerrainBrushFalloff::Smooth) weight=weight*weight*(3-2*weight);
        }
        if(influence) weight=influence(p.first,p.second);
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
        if(transform)after+=(transform(p.first,p.second,before)-before)*weight*qBound(0.0,strength,1.0);
        else if(mode==EWorldSculptMode::Raise || mode==EWorldSculptMode::Lower)
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
                QVector<double> unique;
                // One world-space stencil, including the other side of a seam.
                // Duplicated rim vertices must not overweight the boundary.
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    QVector<double> samples;
                    for(const auto& t:targets) if(t.editable && t.model
                        && (t.id==targets[first.map].id || touching(t,targets[first.map]))) {
                        double z;QVector<QPair<int,int>> anchors;
                        if(contourSample(t,p.first+dx,p.second+dy,&z,&anchors)) samples.append(z);
                    }
                    if(!samples.isEmpty()) unique.append(mean(samples));
                }
                if(!unique.isEmpty()) target=mean(unique);
            }
            after+=(target-before)*weight*qBound(0.0,strength,1.0);
        }
        // The shared target is representable within each map's existing range.
        // Each file rounds only once to its own uint16 height quantum.
        after=qBound(0.0,after,limit);
        proposed[groupIndex]=after;
    }
    result.protectedBoundaryVertices=protectShiftedEdges(targets,groups,proposed,affected,rimKeys);
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

namespace {
// Reparameterize only the shared rim near this stamp. The file format already
// stores signed XY offsets: knitting existing knots avoids the old global
// affine flattening and leaves sector count, triangles and tile IDs intact.
SWorldTerrainStroke knitLocalRims(const QVector<SWorldTerrainTarget>& targets,
                                 const STerrainBrush& brush,RimKeys* rimKeys,
                                 const std::function<double(double,double)>& influence)
{
    SWorldTerrainStroke result;
    QMap<XY,QVector<Vertex>> knots;
    const auto valid=[](const SWorldTerrainTarget& t){return t.editable && t.model
        && t.model->maxZ()>0 && std::isfinite(t.model->maxZ())
        && std::isfinite(t.origin.x()) && std::isfinite(t.origin.y());};
    QVector<int> parent(targets.size());for(int i=0;i<parent.size();++i)parent[i]=i;
    const auto root=[&](int i){while(parent[i]!=i)i=parent[i];return i;};
    for(int a=0;a<targets.size();++a) for(int b=a+1;b<targets.size();++b)
        if(valid(targets[a]) && valid(targets[b]) && touching(targets[a],targets[b]))parent[root(b)]=root(a);
    QVector<QPointF> phases(targets.size(),QPointF(1,1));
    for(int i=0;i<targets.size();++i) if(valid(targets[i])) {
        auto& phase=phases[root(i)];const auto& o=targets[i].origin;
        phase.setX(qMin(phase.x(),o.x()-std::floor(o.x())));
        phase.setY(qMin(phase.y(),o.y()-std::floor(o.y())));
    }
    for(int a=0;a<targets.size();++a) for(int b=a+1;b<targets.size();++b) {
        const auto& ta=targets[a];const auto& tb=targets[b];
        if(!valid(ta) || !valid(tb)) continue;
        for(bool vertical:{true,false}) for(int side:{0,1}) {
            const auto origin=[&](const SWorldTerrainTarget& t,bool along){
                return (vertical==along)?t.origin.y():t.origin.x();};
            const auto count=[&](const SWorldTerrainTarget& t,bool along){
                return (vertical==along)?t.model->vertexCountY():t.model->vertexCountX();};
            const double normal=origin(ta,false)+(side?count(ta,false)-1:0);
            const double other=origin(tb,false)+(side?0:count(tb,false)-1);
            if(qAbs(normal-other)>1e-8) continue; // no inventing bridges over layout gaps
            const double lo=qMax(origin(ta,true),origin(tb,true));
            const double hi=qMin(origin(ta,true)+count(ta,true)-1,origin(tb,true)+count(tb,true)-1);
            if(hi-lo<1e-8) continue;
            const double center=vertical?brush.centerY:brush.centerX;
            const double perpendicular=normal-(vertical?brush.centerX:brush.centerY);
            if(qAbs(perpendicular)>brush.radius) continue;
            for(int i=qMax(0,int(std::floor(center-brush.radius-2-origin(ta,true))));
                i<count(ta,true) && origin(ta,true)+i<=center+brush.radius+2;++i) {
                // Fixed world knots make repeated strokes and active-map changes
                // deterministic. Half-step ties consistently use the lower knot.
                const double phase=vertical?phases[root(a)].y():phases[root(a)].x();
                double along=phase+std::floor(origin(ta,true)+i-phase+0.5-1e-8);
                // An overlap endpoint is a mandatory knot: at a T junction it
                // is also the corner shared by the two maps on the other side.
                // Preserve the canonical knot's inverse mapping when it lies
                // on the shared phase; for a half-step corner select the lower
                // vertex. Every incident edge therefore chooses the same vertex
                // without stealing the vertex assigned to the preceding knot.
                bool endpoint=false;double endpointTie=1e-8;
                for(double end:{lo,hi}) {
                    const double tie=qAbs(end-phase-std::round(end-phase))<1e-8?1e-8:-1e-8;
                    const int nearest=int(std::floor(end-origin(ta,true)+0.5+tie));
                    if(i==nearest && qAbs(end-origin(ta,true)-i)<=0.50000001) {
                        along=end;endpoint=true;endpointTie=tie;break;
                    }
                }
                if(along<lo || along>hi || std::hypot(along-center,perpendicular)>brush.radius+1) continue;
                const int j=int(std::floor(along-origin(tb,true)+0.5+(endpoint?endpointTie:1e-8)));
                if(j<0 || j>=count(tb,true)) continue;
                XY xy=vertical?XY(normal,along):XY(along,normal);
                const Vertex va{a,vertical?(side?count(ta,false)-1:0):i,vertical?i:(side?count(ta,false)-1:0)};
                const Vertex vb{b,vertical?(side?0:count(tb,false)-1):j,vertical?j:(side?0:count(tb,false)-1)};
                const auto oldA=point(ta,va.x,va.y),oldB=point(tb,vb.x,vb.y);
                if(!endpoint && oldA==oldB) xy=oldA; // Keep already matching authored XY.
                // MPR stores offsets in steps of 1/254. Quarter-grid world
                // origins (zone6/zone4) differ by half that quantum, so their
                // actual XY cannot be bit-identical. Keep one logical knot for
                // sculpting while rounding each file to its nearest offset.
                bool representable=true;
                for(const auto& v:{va,vb}) {
                    const auto& t=targets[v.map];
                    const double ox=(xy.first-t.origin.x()-v.x)*254,oy=(xy.second-t.origin.y()-v.y)*254;
                    if(qAbs(std::round(ox))>127 || qAbs(std::round(oy))>127
                       || qAbs(ox-std::round(ox))>0.500001
                       || qAbs(oy-std::round(oy))>0.500001) representable=false;
                }
                if(!representable) {
                    result.error=QString::fromUtf8("Положение соседних карт не позволяет совместить вершины в пределах точности MPR. Выровняйте карты по сетке размещения; мазок не применён.");
                    return result;
                }
                if(influence && influence(xy.first,xy.second)<=0)continue;
                auto& group=knots[xy];
                for(const auto& v:{va,vb}) {
                    bool found=false;for(const auto& existing:group)
                        if(existing.map==v.map && existing.x==v.x && existing.y==v.y) found=true;
                    if(!found) group.append(v);
                }
            }
        }
    }
    QMap<QPair<int,QPair<int,int>>,XY> assignments;
    for(auto it=knots.cbegin();it!=knots.cend();++it) {
        QVector<double> samples;double limit=std::numeric_limits<double>::max();
        for(const auto& v:it.value()) {
            const auto& t=targets[v.map];
            // Sample the old edge along its real displaced knots, before moving
            // anything. This preserves an existing continuous affine seam.
            const bool vertical=(v.x==0 || v.x==t.model->vertexCountX()-1)
                && qAbs(it.key().first-t.origin.x()-v.x)<1e-8;
            const double along=vertical?it.key().second:it.key().first;
            double sampled=height(t,v.x,v.y);
            const int n=vertical?t.model->vertexCountY():t.model->vertexCountX();
            const int at=vertical?v.y:v.x;
            for(int k=qMax(0,at-2);k<qMin(n-1,at+2);++k) {
                const int x=vertical?v.x:k,y=vertical?k:v.y;
                const auto p=point(t,x,y),q=point(t,x+(vertical?0:1),y+(vertical?1:0));
                const double u=vertical?p.second:p.first,w=vertical?q.second:q.first;
                if(w>u && along>=u-1e-9 && along<=w+1e-9) {
                    const double f=qBound(0.0,(along-u)/(w-u),1.0);
                    sampled=height(t,x,y)*(1-f)+height(t,x+(vertical?0:1),y+(vertical?1:0))*f;
                    break;
                }
            }
            samples.append(sampled);limit=qMin(limit,double(t.model->maxZ()));
        }
        const double shared=qBound(0.0,mean(samples),limit);
        for(const auto& v:it.value()) {
            const auto key=qMakePair(v.map,qMakePair(v.x,v.y));
            if(assignments.contains(key) && assignments[key]!=it.key()) {
                result.maps.clear();result.error=QStringLiteral("Ambiguous terrain junction: a rim vertex has two world positions.");return result;
            }
            assignments[key]=it.key();
            rimKeys->insert(key,it.key());
            const auto& t=targets[v.map];STerrainHeightDelta d;
            d.globalX=v.x;d.globalY=v.y;d.before=t.model->height(v.x,v.y);
            d.after=quint16(qBound(0,qRound(shared*65535/t.model->maxZ()),65535));
            d.beforeOffset=t.model->terrainVertexOffset(v.x,v.y);
            d.afterOffset=QPoint(qRound((it.key().first-t.origin.x()-v.x)*254),qRound((it.key().second-t.origin.y()-v.y)*254));
            d.changesPosition=d.beforeOffset!=d.afterOffset;
            if(d.before!=d.after || d.changesPosition) {
                auto& cmd=result.maps[t.id];cmd.description=QStringLiteral("World terrain rim");cmd.deltas.append(d);
            }
        }
    }
    return result;
}
}

SWorldTerrainStroke makeWorldTerrainStamp(const QVector<SWorldTerrainTarget>& targets,
    const STerrainBrush& brush,EWorldSculptMode mode,double worldDelta,double worldFlatten,
    double strength,quint32 noiseSeed,
    const std::function<double(double,double)>& influence,
    const std::function<double(double,double,double)>& transform)
{
    if(!std::isfinite(brush.centerX) || !std::isfinite(brush.centerY)
       || !std::isfinite(brush.radius) || brush.radius<0 || !std::isfinite(strength) || strength<=0
       || !std::isfinite(worldDelta) || !std::isfinite(worldFlatten)) return {};
    RimKeys rimKeys;
    auto result=knitLocalRims(targets,brush,&rimKeys,influence);
    if(!result.error.isEmpty()) return result;
    QVector<CTerrainEditModel> models(targets.size());auto prepared=targets;
    for(int i=0;i<targets.size();++i) if(result.maps.contains(targets[i].id)) {
        models[i]=*targets[i].model;CValidationReport report;
        if(!models[i].apply(result.maps[targets[i].id],true,&report)) {
            result.maps.clear();result.error=report.toText();return result;
        }
        prepared[i].model=&models[i];
    }
    result.append(sculptPreparedWorld(prepared,brush,mode,worldDelta,worldFlatten,strength,noiseSeed,rimKeys,influence,transform));
    // Stable order is independent of map loading order and preparatory deltas.
    for(auto it=result.maps.begin();it!=result.maps.end();++it)
        std::sort(it->deltas.begin(),it->deltas.end(),[](const STerrainHeightDelta& a,const STerrainHeightDelta& b){
            return qMakePair(a.globalY,a.globalX)<qMakePair(b.globalY,b.globalX);});
    return result;
}

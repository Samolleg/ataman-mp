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
       || !std::isfinite(strength)) return result;
    QMap<XY, QVector<Vertex>> edges;
    QVector<QVector<Vertex>> groups;
    for(int i=0; i<targets.size(); ++i)
    {
        const auto& t=targets[i];
        if(!t.editable || !t.model || !(t.model->maxZ()>0) || !std::isfinite(t.model->maxZ())
           || !std::isfinite(t.origin.x()) || !std::isfinite(t.origin.y())) continue;
        const int w=t.model->vertexCountX(), h=t.model->vertexCountY();
        // Packed XY offsets are bounded by half a grid step.
        const int x0=qMax(0,int(std::floor(brush.centerX-t.origin.x()-brush.radius-1)));
        const int x1=qMin(w-1,int(std::ceil(brush.centerX-t.origin.x()+brush.radius+1)));
        const int y0=qMax(0,int(std::floor(brush.centerY-t.origin.y()-brush.radius-1)));
        const int y1=qMin(h-1,int(std::ceil(brush.centerY-t.origin.y()+brush.radius+1)));
        for(int y=y0; y<=y1; ++y) for(int x=x0; x<=x1; ++x)
        {
            const XY p=point(t,x,y);
            if(std::hypot(p.first-brush.centerX,p.second-brush.centerY)>brush.radius) continue;
            Vertex v{i,x,y};
            if(x==0 || y==0 || x==w-1 || y==h-1) edges[p].append(v);
            else groups.append(QVector<Vertex>{v});
        }
    }
    for(const auto& edge : edges) groups.append(edge);
    for(const auto& group : groups)
    {
        const auto& first=group.first();
        const XY p=point(targets[first.map],first.x,first.y);
        const double distance=std::hypot(p.first-brush.centerX,p.second-brush.centerY);
        double weight=1;
        if(brush.radius>0 && brush.falloff!=ETerrainBrushFalloff::Constant)
        {
            weight=qBound(0.0,1.0-distance/brush.radius,1.0);
            if(brush.falloff==ETerrainBrushFalloff::Smooth) weight=weight*weight*(3-2*weight);
        }
        if(weight<=0 || strength<=0) continue;
        QVector<double> initial;
        double limit=std::numeric_limits<double>::max();
        for(const auto& v:group)
        {
            initial.append(height(targets[v.map],v.x,v.y));
            limit=qMin(limit,double(targets[v.map].model->maxZ()));
        }
        const double before=mean(initial);
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

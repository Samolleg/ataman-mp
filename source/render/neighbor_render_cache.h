#ifndef NEIGHBOR_RENDER_CACHE_H
#define NEIGHBOR_RENDER_CACHE_H
#include "render_queue.h"
#include "view_frustum.h"
#include <QMap>
#include <QtMath>

struct SNeighborDrawRecord
{
    QVector<SRenderItem> full, distant;
    QVector3D center;
    float radius{0};
    bool distantSelected{false};
};
struct SNeighborSpatialGroup
{
    QVector<int> objects;
    QVector3D minimum,maximum;
    void append(int index,const QVector3D& center,float radius)
    {
        const QVector3D extent(radius,radius,radius);
        const auto lo=center-extent,hi=center+extent;
        if(objects.isEmpty()) { minimum=lo; maximum=hi; }
        else for(int axis=0;axis<3;++axis)
        {
            minimum[axis]=qMin(minimum[axis],lo[axis]);
            maximum[axis]=qMax(maximum[axis],hi[axis]);
        }
        objects.append(index);
    }
    bool withinDistance(const QVector3D& point,float distance) const
    {
        float square=0;
        for(int axis=0;axis<3;++axis)
        {
            const float delta=qMax(minimum[axis]-point[axis],qMax(0.0f,point[axis]-maximum[axis]));
            square+=delta*delta;
        }
        return square<=distance*distance;
    }
};
inline QPair<int,int> neighborCell(const QVector3D& center)
{ return qMakePair(qFloor(center.x()/32.0f),qFloor(center.y()/32.0f)); }
inline bool neighborDistantLod(float distance,float pixelRadius,bool previous)
{
    // Hysteresis prevents camera jitter from switching geometry every frame.
    return previous ? distance>110.0f && pixelRadius<56.0f
                    : distance>140.0f && pixelRadius<40.0f;
}
#endif

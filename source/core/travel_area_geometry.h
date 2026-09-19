#pragma once
#include <QRectF>
#include <QVector>
#include <QString>
#include <cmath>

struct STravelArea
{
    int exitIndex{-1};
    bool remove{false};
    QRectF rectangle;
    double angle{0};
    QString label;
    QString mapPath; // Empty for the active map; otherwise read-only neighbor terrain.
};
inline QPointF travelRotatedPoint(const QPointF& p,const QPointF& center,double degrees)
{
    const double radians=degrees*3.14159265358979323846/180.0;
    const auto d=p-center;const double c=std::cos(radians),s=std::sin(radians);
    return center+QPointF(c*d.x()-s*d.y(),s*d.x()+c*d.y());
}
inline QVector<QPointF> travelAreaHandles(const QRectF& r)
{
    return {r.topLeft(),QPointF(r.center().x(),r.top()),r.topRight(),
        QPointF(r.right(),r.center().y()),r.bottomRight(),QPointF(r.center().x(),r.bottom()),
        r.bottomLeft(),QPointF(r.left(),r.center().y())};
}
// handle -1 means translation, 0..7 resize a corner/side.
inline QRectF travelDraggedRect(const QRectF& original,const QPointF& delta,int handle,const QRectF& bounds)
{
    QRectF r=original;
    if(handle<0) {
        r.moveTo(qBound(bounds.left(),r.left()+delta.x(),qMax(bounds.left(),bounds.right()-r.width())),
                 qBound(bounds.top(),r.top()+delta.y(),qMax(bounds.top(),bounds.bottom()-r.height())));
    } else {
        if(handle==0||handle==6||handle==7)r.setLeft(original.left()+delta.x());
        if(handle==2||handle==3||handle==4)r.setRight(original.right()+delta.x());
        if(handle==0||handle==1||handle==2)r.setTop(original.top()+delta.y());
        if(handle==4||handle==5||handle==6)r.setBottom(original.bottom()+delta.y());
    }
    return r.normalized().intersected(bounds);
}

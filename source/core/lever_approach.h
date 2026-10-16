#ifndef LEVER_APPROACH_H
#define LEVER_APPROACH_H
#include "map_object_placement_policy.h"
#include <QVector2D>
#include <cmath>
namespace ei {
inline float leverApproachScale(float databaseScale, float height) {
    return databaseScale * height + (1.0f - height);
}
inline QVector3D leverApproachWorld(const QVector2D& offset, const QVector3D& origin,
                                  const QQuaternion& rotation, float scale, float reach) {
    const float length = offset.length();
    const QVector2D direction = length > 0 ? offset / length : offset;
    const QVector2D expanded = direction * (length * scale + reach);
    return origin + runtimeNavigationRotateVector(rotation, QVector3D(expanded, 0));
}
// Invert the XY projection, including tilted and non-unit stored quaternions.
inline bool leverApproachLocal(const QVector2D& world, const QVector3D& origin,
                              const QQuaternion& rotation, float scale, float reach,
                              QVector2D* result) {
    if(!result || !std::isfinite(scale) || scale <= 0 || !std::isfinite(reach) || reach < 0)
        return false;
    const auto x = runtimeNavigationRotateVector(rotation, QVector3D(1,0,0));
    const auto y = runtimeNavigationRotateVector(rotation, QVector3D(0,1,0));
    const double determinant = double(x.x()) * y.y() - double(x.y()) * y.x();
    if(!std::isfinite(determinant) || std::abs(determinant) < 1e-7) return false;
    const QVector2D delta = world - QVector2D(origin);
    const QVector2D expanded(float((delta.x()*double(y.y())-delta.y()*double(y.x()))/determinant),
                             float((double(x.x())*delta.y()-double(x.y())*delta.x())/determinant));
    const float length = expanded.length();
    if(!std::isfinite(length)) return false;
    if(length == 0) { *result = QVector2D(); return true; }
    if(length <= reach) return false;
    *result = expanded * ((length - reach) / (scale * length));
    return std::isfinite(result->x()) && std::isfinite(result->y());
}
}
#endif

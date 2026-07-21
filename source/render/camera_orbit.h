#ifndef CAMERA_ORBIT_H
#define CAMERA_ORBIT_H

#include <QMatrix4x4>
#include <QVector3D>

namespace ei {

inline QVector3D reanchoredCameraPosition(const QVector3D& position,
                                          const QVector3D& oldPivot,
                                          const QVector3D& newPivot,
                                          float xRotation, float zRotation)
{
    QMatrix4x4 rotation;
    rotation.setToIdentity();
    rotation.rotate(xRotation, 1.0f, 0.0f, 0.0f);
    rotation.rotate(zRotation, 0.0f, 0.0f, 1.0f);
    const QVector3D delta = newPivot - oldPivot;
    return position + delta - rotation * delta;
}

} // namespace ei

#endif // CAMERA_ORBIT_H

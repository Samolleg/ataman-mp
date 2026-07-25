#ifndef CAMERA_ORBIT_H
#define CAMERA_ORBIT_H

#include <QMatrix4x4>
#include <QtGlobal>
#include <QVector3D>

namespace ei {

inline float cameraFrameMovementScale(double elapsedSeconds)
{
    // Preserve the legacy 60 FPS feel while integrating keyboard movement by
    // elapsed time. Bound long UI stalls so focus recovery cannot launch the
    // camera across the map in one update.
    const double bounded = qBound(0.0, elapsedSeconds, 0.1);
    return float(bounded * 60.0);
}

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

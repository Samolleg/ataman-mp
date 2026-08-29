#ifndef CAMERA_TELEPHOTO_H
#define CAMERA_TELEPHOTO_H

#include <QMatrix4x4>
#include <QtMath>
#include <QVector3D>

#include <cmath>

namespace ei {

constexpr float kTelephotoMultiplierMinimum = 1.0f;
constexpr float kTelephotoMultiplierDefault = 2.0f;
constexpr float kTelephotoMultiplierMaximum = 4.0f;
constexpr float kTelephotoYawDegrees = 45.0f;
constexpr float kTelephotoPitchDegrees = 35.264f;

inline float boundedTelephotoMultiplier(float multiplier)
{
    if(!std::isfinite(double(multiplier)))
        return kTelephotoMultiplierDefault;
    return qBound(kTelephotoMultiplierMinimum, multiplier,
                  kTelephotoMultiplierMaximum);
}

inline QVector3D telephotoViewDirection()
{
    const float yaw = qDegreesToRadians(kTelephotoYawDegrees);
    const float pitch = qDegreesToRadians(kTelephotoPitchDegrees);
    return QVector3D(qCos(pitch) * qCos(yaw),
                     qCos(pitch) * qSin(yaw),
                     -qSin(pitch)).normalized();
}

inline QVector3D telephotoGroundForward()
{
    QVector3D direction = telephotoViewDirection();
    direction.setZ(0.0f);
    return direction.normalized();
}

inline QVector3D telephotoGroundRight()
{
    return QVector3D::crossProduct(telephotoViewDirection(),
                                   QVector3D(0.0f, 0.0f, 1.0f)).normalized();
}

inline QVector3D cameraEyeFromMatrix(const QMatrix4x4& view, bool* ok = nullptr)
{
    bool invertible = false;
    const QMatrix4x4 inverse = view.inverted(&invertible);
    const QVector3D eye = inverse.map(QVector3D());
    const bool finite = invertible
        && std::isfinite(double(eye.x()))
        && std::isfinite(double(eye.y()))
        && std::isfinite(double(eye.z()));
    if(ok)
        *ok = finite;
    return finite ? eye : QVector3D();
}

struct STelephotoCameraFrame
{
    QVector3D target;
    float logicalDistance{0.0f};
    QVector3D logicalEye;
    QVector3D renderEye;
    QVector3D cullingCenter;
    QMatrix4x4 view;
};

inline STelephotoCameraFrame telephotoCameraFrame(const QMatrix4x4& normalView,
                                                  const QVector3D& target,
                                                  float multiplier)
{
    STelephotoCameraFrame frame;
    frame.target = target;
    bool eyeOk = false;
    const QVector3D normalEye = cameraEyeFromMatrix(normalView, &eyeOk);
    frame.logicalDistance = eyeOk ? normalEye.distanceToPoint(target) : 0.0f;
    const QVector3D direction = telephotoViewDirection();
    frame.logicalEye = target - direction * frame.logicalDistance;
    frame.renderEye = target - direction * (frame.logicalDistance
        * boundedTelephotoMultiplier(multiplier));
    frame.cullingCenter = frame.logicalEye;
    frame.view.setToIdentity();
    frame.view.lookAt(frame.renderEye, target, QVector3D(0.0f, 0.0f, 1.0f));
    return frame;
}

inline QMatrix4x4 telephotoProjectionMatrix(const QMatrix4x4& normalProjection,
                                            float multiplier)
{
    QMatrix4x4 projection = normalProjection;
    const float bounded = boundedTelephotoMultiplier(multiplier);
    projection(0, 0) *= bounded;
    projection(1, 1) *= bounded;
    return projection;
}

inline QMatrix4x4 activeCameraViewMatrix(const QMatrix4x4& normalView,
                                         const QVector3D& target,
                                         bool telephotoEnabled,
                                         float multiplier)
{
    if(!telephotoEnabled)
        return normalView;
    return telephotoCameraFrame(normalView, target, multiplier).view;
}

inline QMatrix4x4 activeCameraProjectionMatrix(
        const QMatrix4x4& normalProjection, bool telephotoEnabled,
        float multiplier)
{
    if(!telephotoEnabled)
        return normalProjection;
    return telephotoProjectionMatrix(normalProjection, multiplier);
}

inline bool cameraRotationInputAllowed(bool telephotoEnabled)
{
    return !telephotoEnabled;
}

inline QVector3D telephotoZoomedPosition(const QVector3D& position,
                                         const QVector3D& target,
                                         float amount, bool zoomIn)
{
    QVector3D away = position - target;
    const float distance = away.length();
    if(distance <= 0.000001f)
        away = -telephotoViewDirection();
    else
        away /= distance;
    const float nextDistance = qMax(0.1f,
        distance + (zoomIn ? -qAbs(amount) : qAbs(amount)));
    return target + away * nextDistance;
}

} // namespace ei

#endif // CAMERA_TELEPHOTO_H

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

enum class ETelephotoDirection
{
    SouthwestToNortheast = 0,
    SoutheastToNorthwest = 1,
    NortheastToSouthwest = 2,
    NorthwestToSoutheast = 3
};

constexpr ETelephotoDirection kTelephotoDirectionDefault =
    ETelephotoDirection::SouthwestToNortheast;

inline ETelephotoDirection boundedTelephotoDirection(int direction)
{
    if(direction < int(ETelephotoDirection::SouthwestToNortheast)
       || direction > int(ETelephotoDirection::NorthwestToSoutheast))
        return kTelephotoDirectionDefault;
    return static_cast<ETelephotoDirection>(direction);
}

inline float telephotoYawDegrees(ETelephotoDirection direction)
{
    switch(direction)
    {
    case ETelephotoDirection::SoutheastToNorthwest:
        return 135.0f;
    case ETelephotoDirection::NortheastToSouthwest:
        return 225.0f;
    case ETelephotoDirection::NorthwestToSoutheast:
        return 315.0f;
    case ETelephotoDirection::SouthwestToNortheast:
    default:
        return kTelephotoYawDegrees;
    }
}

inline float boundedTelephotoMultiplier(float multiplier)
{
    if(!std::isfinite(double(multiplier)))
        return kTelephotoMultiplierDefault;
    return qBound(kTelephotoMultiplierMinimum, multiplier,
                  kTelephotoMultiplierMaximum);
}

inline QVector3D telephotoViewDirection(
        ETelephotoDirection cameraDirection = kTelephotoDirectionDefault)
{
    const float yaw = qDegreesToRadians(telephotoYawDegrees(cameraDirection));
    const float pitch = qDegreesToRadians(kTelephotoPitchDegrees);
    return QVector3D(qCos(pitch) * qCos(yaw),
                     qCos(pitch) * qSin(yaw),
                     -qSin(pitch)).normalized();
}

inline QVector3D telephotoGroundForward(
        ETelephotoDirection cameraDirection = kTelephotoDirectionDefault)
{
    QVector3D direction = telephotoViewDirection(cameraDirection);
    direction.setZ(0.0f);
    return direction.normalized();
}

inline QVector3D telephotoGroundRight(
        ETelephotoDirection cameraDirection = kTelephotoDirectionDefault)
{
    return QVector3D::crossProduct(telephotoViewDirection(cameraDirection),
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
                                                  float multiplier,
                                                  ETelephotoDirection cameraDirection =
                                                      kTelephotoDirectionDefault)
{
    STelephotoCameraFrame frame;
    frame.target = target;
    bool eyeOk = false;
    const QVector3D normalEye = cameraEyeFromMatrix(normalView, &eyeOk);
    frame.logicalDistance = eyeOk ? normalEye.distanceToPoint(target) : 0.0f;
    const QVector3D direction = telephotoViewDirection(cameraDirection);
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
                                         float multiplier,
                                         ETelephotoDirection cameraDirection =
                                             kTelephotoDirectionDefault)
{
    if(!telephotoEnabled)
        return normalView;
    return telephotoCameraFrame(normalView, target, multiplier,
                                cameraDirection).view;
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
                                         float amount, bool zoomIn,
                                         ETelephotoDirection cameraDirection =
                                             kTelephotoDirectionDefault)
{
    QVector3D away = position - target;
    const float distance = away.length();
    if(distance <= 0.000001f)
        away = -telephotoViewDirection(cameraDirection);
    else
        away /= distance;
    const float nextDistance = qMax(0.1f,
        distance + (zoomIn ? -qAbs(amount) : qAbs(amount)));
    return target + away * nextDistance;
}

} // namespace ei

#endif // CAMERA_TELEPHOTO_H

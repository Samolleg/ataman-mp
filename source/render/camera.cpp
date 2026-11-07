#include <QGLWidget>
#include <QtMath>
#include "camera.h"
#include "camera_orbit.h"
#include "camera_telephoto.h"
#include "key_manager.h"
#include "settings.h"
#include "log.h"

CCamera::CCamera()
    :m_speedMultiplier(1.0f)
    ,m_telephotoMultiplier(ei::kTelephotoMultiplierDefault)
    ,m_telephotoDirection(ei::kTelephotoDirectionDefault)
    ,m_frameMovementScale(1.0f)
    ,m_keyManager(nullptr)
    ,m_pSettings(nullptr)
{
    m_pos = QVector3D(0.0, 1.0, 10.0);
    m_pivot = QVector3D(0.0, 0.0, 0.0);
    m_xRot = -90.0f;
    m_zRot = 0.0f;
    m_step = 0.5f;
    syncMotionInterpolation();
}

CCamera::CCamera(QVector3D pos, QVector3D pivot, float xRot, float zRot)
    :m_pos(pos)
    ,m_pivot(pivot)
    ,m_xRot(xRot)
    ,m_zRot(zRot)
    ,m_step(0.5f)
    ,m_speedMultiplier(1.0f)
    ,m_telephotoMultiplier(ei::kTelephotoMultiplierDefault)
    ,m_telephotoDirection(ei::kTelephotoDirectionDefault)
    ,m_frameMovementScale(1.0f)
    ,m_keyManager(nullptr)
    ,m_pSettings(nullptr)
{
    syncMotionInterpolation();
}

void CCamera::reset()
{
    m_updateTimer.invalidate();
    m_yRot = 0;
    COptBool* pOpt = dynamic_cast<COptBool*>(m_pSettings->opt("freeCamera"));
    Q_ASSERT(pOpt);
    if (pOpt->value())
    { // free camera
        m_pos = QVector3D(0.0, 1.0, 10.0);
        m_pivot = QVector3D(0.0, 0.0, 0.0);
        m_xRot = -90.0f;
        m_zRot = 0.0f;
        m_step = 0.5f;
    }
    else
    { // horizontal camera
        m_pos = QVector3D(0.0, 1.0, 30.0);
        m_pivot = QVector3D(0.0, 0.0, 20.0);
        m_xRot = -70.0f;
        m_zRot = 45.0f;
        m_step = 1.0f;
    }
    syncMotionInterpolation();
}

void CCamera::adoptView(const QMatrix4x4& view)
{
    const float y=qAsin(qBound(-1.0f,view(0,2),1.0f));
    const float x=qAbs(qCos(y))>1e-5f?qAtan2(-view(1,2),view(2,2)):qAtan2(view(2,1),view(1,1));
    const float z=qAbs(qCos(y))>1e-5f?qAtan2(-view(0,1),view(0,0)):0.0f;
    m_previewActive=false;m_telephotoEnabled=false;
    m_xRot=qRadiansToDegrees(x);m_yRot=qRadiansToDegrees(y);m_zRot=qRadiansToDegrees(z);
    const auto inverse=view.inverted();m_pivot=inverse.map(QVector3D(0,0,-10));
    QMatrix4x4 rotation;rotation.rotate(m_xRot,1,0,0);rotation.rotate(m_yRot,0,1,0);rotation.rotate(m_zRot,0,0,1);
    m_pos=m_pivot-rotation.mapVector(m_pivot)-QVector3D(view(0,3),view(1,3),view(2,3));
    syncMotionInterpolation();m_updateTimer.invalidate();
}

void CCamera::setPreviewView(const QMatrix4x4& view)
{
    if(!m_previewActive) {
        getLocation(m_beforePreviewPosition, m_beforePreviewPivot, m_beforePreviewRotation);
        m_beforePreviewTelephoto = m_telephotoEnabled;
    }
    m_previewView = view;
    m_previewActive = true;
    m_telephotoEnabled = false;
}

void CCamera::clearPreviewView()
{
    if(!m_previewActive) return;
    m_previewActive = false;
    m_telephotoEnabled = m_beforePreviewTelephoto;
    setLocation(m_beforePreviewPosition, m_beforePreviewPivot, m_beforePreviewRotation);
    m_updateTimer.invalidate();
}

void CCamera::getLocation(QVector3D& pos, QVector3D& pivot,
                          QVector3D& rot) const
{
    pos = m_motionStateInitialized ? m_renderPos : m_pos;
    pivot = m_motionStateInitialized ? m_renderPivot : m_pivot;
    rot = QVector3D(m_xRot, m_yRot, m_zRot);
}

void CCamera::setLocation(const QVector3D& pos, const QVector3D& pivot, const QVector3D& rot)
{
    m_pos = pos;
    m_pivot = pivot;
    m_xRot = rot.x();
    m_yRot = rot.y();
    m_zRot = rot.z();
    syncMotionInterpolation();
}

void CCamera::setOrbitPivotPreservingView(const QVector3D& pivot)
{
    // The legacy view matrix is T(pivot) * T(-position) * R * T(-pivot).
    // Replacing only the pivot changes the current view before the user has
    // moved the mouse. Compensate the stored position so the matrix remains
    // identical, then subsequent rotations orbit the newly selected point.
    const auto view = matrixFor(m_pos,m_pivot);
    QMatrix4x4 rotation;rotation.rotate(m_xRot,1,0,0);rotation.rotate(m_yRot,0,1,0);rotation.rotate(m_zRot,0,0,1);
    m_pos = pivot - rotation.mapVector(pivot) - QVector3D(view(0,3),view(1,3),view(2,3));
    m_pivot = pivot;
    syncMotionInterpolation();
}

void CCamera::xRotate(float& angle)
{
    if(!ei::cameraRotationInputAllowed(m_telephotoEnabled))
        return;
    m_xRot += angle;
    util::normalizeAngle(m_xRot);
}

void CCamera::zRotate(float& angle)
{
    if(!ei::cameraRotationInputAllowed(m_telephotoEnabled))
        return;
    m_zRot += angle;
    util::normalizeAngle(m_zRot);
}

void CCamera::setSpeedMultiplier(float multiplier)
{
    m_speedMultiplier = qBound(0.1f, multiplier, 10.0f);
}

void CCamera::setTelephotoMultiplier(float multiplier)
{
    m_telephotoMultiplier = ei::boundedTelephotoMultiplier(multiplier);
}

void CCamera::setTelephotoDirection(ei::ETelephotoDirection direction)
{
    m_telephotoDirection = ei::boundedTelephotoDirection(int(direction));
}

bool CCamera::movementInputActive() const
{
    if(!m_keyManager)
        return false;
    return m_keyManager->isPressed(eKey_W)
        || m_keyManager->isPressed(eKey_A)
        || m_keyManager->isPressed(eKey_S)
        || m_keyManager->isPressed(eKey_D)
        || m_keyManager->isPressed(eKey_Q)
        || m_keyManager->isPressed(eKey_E)
        || m_keyManager->isPressed(eKey_UpArrow)
        || m_keyManager->isPressed(eKey_DownArrow)
        || m_keyManager->isPressed(eKey_LeftArrow)
        || m_keyManager->isPressed(eKey_RightArrow);
}

QMatrix4x4 CCamera::update()
{
    if(m_previewActive) return m_previewView;
    double elapsedSeconds = 1.0 / 60.0;
    if(m_updateTimer.isValid())
    {
        elapsedSeconds = double(m_updateTimer.nsecsElapsed()) / 1000000000.0;
        m_updateTimer.restart();
    }
    else
        m_updateTimer.start();
    if(!m_motionStateInitialized)
        syncMotionInterpolation();
    // The camera has no collision or physics state that benefits from a fixed
    // simulation quantum. Quantizing it to 120 Hz made VSync-limited 60 Hz
    // strafing alternate between interpolation phases. Integrate the actual
    // bounded frame delta so every presented frame advances continuously.
    m_integratingKeyboardMotion = true;
    m_previousMotionPos = m_pos;
    m_previousMotionPivot = m_pivot;
    m_frameMovementScale = ei::cameraFrameMovementScale(elapsedSeconds);
    move();
    m_integratingKeyboardMotion = false;
    m_frameMovementScale = 1.0f;
    m_renderPos = m_pos;
    m_renderPivot = m_pivot;
    const QMatrix4x4 normalView = matrixFor(m_renderPos, m_renderPivot);
    return ei::activeCameraViewMatrix(normalView, m_renderPivot,
                                      m_telephotoEnabled,
                                      m_telephotoMultiplier,
                                      m_telephotoDirection);
}

QMatrix4x4 CCamera::viewMatrix()
{
    if(m_previewActive) return m_previewView;
    const QVector3D position = m_motionStateInitialized ? m_renderPos : m_pos;
    const QVector3D pivot = m_motionStateInitialized ? m_renderPivot : m_pivot;
    return ei::activeCameraViewMatrix(matrixFor(position, pivot), pivot,
                                      m_telephotoEnabled,
                                      m_telephotoMultiplier,
                                      m_telephotoDirection);
}

QVector3D CCamera::logicalCullingPosition() const
{
    if(m_previewActive) return ei::cameraEyeFromMatrix(m_previewView);
    const QVector3D position = m_motionStateInitialized ? m_renderPos : m_pos;
    const QVector3D pivot = m_motionStateInitialized ? m_renderPivot : m_pivot;
    const QMatrix4x4 normalView = matrixFor(position, pivot);
    if(m_telephotoEnabled)
        return ei::telephotoCameraFrame(normalView, pivot,
                                        m_telephotoMultiplier,
                                        m_telephotoDirection).cullingCenter;
    bool eyeOk = false;
    const QVector3D eye = ei::cameraEyeFromMatrix(normalView, &eyeOk);
    return eyeOk ? eye : position;
}

QMatrix4x4 CCamera::matrixFor(const QVector3D& position, const QVector3D& pivot) const
{
    QMatrix4x4 matrix;
    matrix.setToIdentity();
    matrix.translate(pivot.x(), pivot.y(), pivot.z());
    matrix.translate(-position.x(), -position.y(), -position.z());
    matrix.rotate(m_xRot, 1.0, 0.0, 0.0);
    matrix.rotate(m_yRot, 0.0, 1.0, 0.0);
    matrix.rotate(m_zRot, 0.0, 0.0, 1.0);
    matrix.translate(-pivot.x(), -pivot.y(), -pivot.z());
    return matrix;
}

void CCamera::syncMotionInterpolation()
{
    m_previousMotionPos = m_pos;
    m_previousMotionPivot = m_pivot;
    m_renderPos = m_pos;
    m_renderPivot = m_pivot;
    m_motionAccumulator = 0.0;
    m_motionStateInitialized = true;
}

void CCamera::strafeRight(bool bShift)
{
    if(m_telephotoEnabled)
    {
        translate(ei::telephotoGroundRight(m_telephotoDirection), bShift);
        return;
    }
    QVector3D dir(1.0, 0.0, 0.0);
    move(dir, bShift);
}

void CCamera::strafeLeft(bool bShift)
{
    if(m_telephotoEnabled)
    {
        translate(-ei::telephotoGroundRight(m_telephotoDirection), bShift);
        return;
    }
    QVector3D dir(-1.0, 0.0, 0.0);
    move(dir, bShift);
}

void CCamera::strafeForward(bool bShift)
{
    if(m_telephotoEnabled)
    {
        translate(ei::telephotoGroundForward(m_telephotoDirection), bShift);
        return;
    }
    QVector3D dir(m_pivot-m_pos);
    move(dir, bShift);
}

void CCamera::shiftForward(bool bShift)
{
    if(m_telephotoEnabled)
    {
        translate(ei::telephotoGroundForward(m_telephotoDirection), bShift);
        return;
    }
    QVector3D dir(m_pivot-m_pos);
    shift(dir, bShift);
}

void CCamera::strafeBack(bool bShift)
{
    if(m_telephotoEnabled)
    {
        translate(-ei::telephotoGroundForward(m_telephotoDirection), bShift);
        return;
    }
    QVector3D dir(m_pos-m_pivot);
    move(dir, bShift);
}

void CCamera::shiftBack(bool bShift)
{
    if(m_telephotoEnabled)
    {
        translate(-ei::telephotoGroundForward(m_telephotoDirection), bShift);
        return;
    }
    QVector3D dir(m_pos-m_pivot);
    shift(dir, bShift);
}

void CCamera::strafeUp(bool bShift)
{
    QVector3D dir(0.0f, 1.0f, 0.0f);
    move(dir, bShift);
}

void CCamera::strafeUpward(bool bShift)
{
    QVector3D dir(0.0f, 0.0f, 1.0f);
    const float amount = step();
    m_pos += dir*(bShift ? amount*2.0f : amount);
    m_pivot += dir*(bShift ? amount*2.0f : amount);
    if(!m_integratingKeyboardMotion)
        syncMotionInterpolation();
}

void CCamera::strafeDown(bool bShift)
{
    QVector3D dir(0.0f, -1.0f, 0.0f);
    move(dir, bShift);
}

void CCamera::strafeDownward(bool bShift)
{
    QVector3D dir(0.0f, 0.0f, -1.0f);
    const float amount = step();
    m_pos += dir*(bShift ? amount*2.0f : amount);
    m_pivot += dir*(bShift ? amount*2.0f : amount);
    if(!m_integratingKeyboardMotion)
        syncMotionInterpolation();
}

void CCamera::move(QVector3D& orDir, bool bShift)
{
    QMatrix4x4 rot;
    rot.setToIdentity();
    rot.rotate(m_xRot, 1.0, 0.0, 0.0);
    rot.rotate(m_zRot, 0.0, 0.0, 1.0);
    orDir=rot.transposed()*orDir;
    orDir.normalize();

    const float amount = step();
    m_pos += orDir*(bShift ? amount*2.0f : amount);
    m_pivot += orDir*(bShift ? amount*2.0f : amount);
    if(!m_integratingKeyboardMotion)
        syncMotionInterpolation();
}

void CCamera::shift(QVector3D& orDir, bool bShift)
{
    QMatrix4x4 rot;
    rot.setToIdentity();
    rot.rotate(m_xRot, 1.0, 0.0, 0.0);
    rot.rotate(m_zRot, 0.0, 0.0, 1.0);
    orDir=rot.transposed()*orDir;
    orDir.setZ(0.0f);
    orDir.normalize();

    const float amount = step();
    const QVector3D movement = orDir*(bShift ? amount*2.0f : amount);
    m_pos += movement;
    m_pivot += movement;
    if(!m_integratingKeyboardMotion)
        syncMotionInterpolation();
}

void CCamera::translate(const QVector3D& direction, bool bShift)
{
    const float amount = step() * (bShift ? 2.0f : 1.0f);
    const QVector3D movement = direction.normalized() * amount;
    m_pos += movement;
    m_pivot += movement;
    if(!m_integratingKeyboardMotion)
        syncMotionInterpolation();
}

void CCamera::enlarge(const bool bZoom)
{
    if(m_telephotoEnabled)
    {
        m_pos = ei::telephotoZoomedPosition(
            m_pos, m_pivot, step(), bZoom, m_telephotoDirection);
        if(!m_integratingKeyboardMotion)
            syncMotionInterpolation();
        return;
    }
    COptBool* pOpt = dynamic_cast<COptBool*>(m_pSettings->opt("freeCamera"));
    Q_ASSERT(pOpt);
    if (pOpt->value())
    {
        QVector3D dir = bZoom ? m_pivot-m_pos : m_pos-m_pivot;
        dir.normalize();
        m_pos += dir*step();
    }
    else
    {
        if(bZoom)
            strafeDownward(nullptr == m_keyManager ? false : m_keyManager->isPressed(eKey_Shift));
        else
            strafeUpward(nullptr == m_keyManager ? false : m_keyManager->isPressed(eKey_Shift));
    }
}

void CCamera::move()
{
    if(nullptr ==  m_keyManager)
    {
        return;
    }

    COptBool* pOpt = dynamic_cast<COptBool*>(m_pSettings->opt("freeCamera"));
    Q_ASSERT(pOpt);
    if (pOpt->value())
    { // free camera
        if(m_keyManager->isPressed(eKey_W) || m_keyManager->isPressed(eKey_UpArrow))
            strafeForward(m_keyManager->isPressed(eKey_Shift));
        if(m_keyManager->isPressed(eKey_S) || m_keyManager->isPressed(eKey_DownArrow))
            strafeBack(m_keyManager->isPressed(eKey_Shift));
    }
    else
    {
        if(m_keyManager->isPressed(eKey_W) || m_keyManager->isPressed(eKey_UpArrow))
            shiftForward(m_keyManager->isPressed(eKey_Shift));
        if(m_keyManager->isPressed(eKey_S) || m_keyManager->isPressed(eKey_DownArrow))
            shiftBack(m_keyManager->isPressed(eKey_Shift));
    }
    if(m_keyManager->isPressed(eKey_D) || m_keyManager->isPressed(eKey_RightArrow))
        strafeRight(m_keyManager->isPressed(eKey_Shift));
    if(m_keyManager->isPressed(eKey_A) || m_keyManager->isPressed(eKey_LeftArrow))
        strafeLeft(m_keyManager->isPressed(eKey_Shift));
    if(m_keyManager->isPressed(eKey_E))
        strafeUpward(m_keyManager->isPressed(eKey_Shift));
    if(m_keyManager->isPressed(eKey_Q))
        strafeDownward(m_keyManager->isPressed(eKey_Shift));
}

// move camera to 'posTarget' point
void CCamera::moveTo(QVector3D posTarget)
{
    QVector3D dif = m_pos - m_pivot;
    m_pivot = posTarget;
    m_pos = m_pivot + dif;
    syncMotionInterpolation();
}

//move camera away on 'distance' point from pivot
void CCamera::moveAwayOn(float distance)
{
    if(distance==0)
    {
        ei::log(eLogWarning, "show distance is null. object empty");
        return;
    }
    QVector3D dir(m_pos-m_pivot);
    dir.normalize();
    m_pos = m_pivot + dir*distance;
    syncMotionInterpolation();
}

void CCamera::attachSettings(CSettings *pSetting)
{
    m_pSettings = pSetting;
    if (m_pSettings)
    {
        COptBool* pOpt = dynamic_cast<COptBool*>(m_pSettings->opt("freeCamera"));
        Q_ASSERT(pOpt);
        if (pOpt->value())
        { // free camera
            m_pos = QVector3D(0.0, 1.0, 30.0);
            m_pivot = QVector3D(0.0, 0.0, 10.0);
            m_xRot = -70.0f;
            m_zRot = 45.0f;
        }
        if(COptDouble* speed = dynamic_cast<COptDouble*>(
               m_pSettings->opt("cameraSpeedMultiplier")))
            setSpeedMultiplier(float(speed->value()));
    }
    syncMotionInterpolation();
}

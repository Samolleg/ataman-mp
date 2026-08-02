#ifndef SHADOW_CASCADE_H
#define SHADOW_CASCADE_H

#include <QByteArray>
#include <QMatrix4x4>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QtGlobal>

namespace ei
{

class ShadowCachePublicationGate
{
public:
    void reset()
    {
        m_pendingNear.clear();
        m_pendingFar.clear();
        m_pendingSinceMs = -1;
    }

    bool replacementReady(const QByteArray& currentNear,
                          const QByteArray& currentFar,
                          const QByteArray& candidateNear,
                          const QByteArray& candidateFar,
                          qint64 nowMs,
                          qint64 settleMs)
    {
        if(candidateNear == currentNear && candidateFar == currentFar)
        {
            reset();
            return false;
        }
        if(candidateNear != m_pendingNear || candidateFar != m_pendingFar)
        {
            m_pendingNear = candidateNear;
            m_pendingFar = candidateFar;
            m_pendingSinceMs = nowMs;
            return false;
        }
        if(m_pendingSinceMs < 0 || nowMs - m_pendingSinceMs < settleMs)
            return false;
        reset();
        return true;
    }

private:
    QByteArray m_pendingNear;
    QByteArray m_pendingFar;
    qint64 m_pendingSinceMs{-1};
};

inline int shadowAtlasResolution(int quality, int maximumTextureSize)
{
    if(quality <= 0 || maximumTextureSize <= 0)
        return 0;
    const int requested = quality >= 2 ? 4096 : 2048;
    return qMin(requested, maximumTextureSize);
}

inline QVector3D cameraEyeFromView(const QMatrix4x4& view, bool* ok = nullptr)
{
    // Do not replace this with CCamera::getLocation().pos. That API exposes
    // legacy orbit-controller state, not necessarily the rendered eye.
    bool inverseOk = false;
    const QMatrix4x4 inverseView = view.inverted(&inverseOk);
    if(ok)
        *ok = inverseOk;
    return inverseOk ? (inverseView * QVector4D(0.0f, 0.0f, 0.0f, 1.0f)).toVector3D()
                     : QVector3D();
}

// Return the eight world-space corners of a perspective-camera slice. Depths
// are positive view-space distances (-viewPosition.z), matching v_viewDepth
// in the color shaders. Reconstructing in view space is important for the
// editor's legacy orbit camera: its stored position is an orbit anchor, not
// the eye after the view rotations have been applied.
inline QVector<QVector3D> cameraFrustumSliceCorners(const QMatrix4x4& projection,
                                                    const QMatrix4x4& view,
                                                    float nearDepth,
                                                    float farDepth,
                                                    bool* ok = nullptr)
{
    QVector<QVector3D> corners;
    bool projectionOk = false;
    bool viewOk = false;
    const QMatrix4x4 inverseProjection = projection.inverted(&projectionOk);
    const QMatrix4x4 inverseView = view.inverted(&viewOk);
    if(!projectionOk || !viewOk || nearDepth <= 0.0f || farDepth < nearDepth)
    {
        if(ok)
            *ok = false;
        return corners;
    }

    corners.reserve(8);
    for(int y = -1; y <= 1; y += 2)
        for(int x = -1; x <= 1; x += 2)
        {
            QVector4D farView = inverseProjection
                * QVector4D(float(x), float(y), 1.0f, 1.0f);
            if(qFuzzyIsNull(farView.w()))
            {
                if(ok)
                    *ok = false;
                return {};
            }
            farView /= farView.w();
            const float rayDepth = -farView.z();
            if(rayDepth <= 0.000001f)
            {
                if(ok)
                    *ok = false;
                return {};
            }
            for(const float depth : {nearDepth, farDepth})
            {
                QVector4D world = inverseView * QVector4D(
                    farView.x() * depth / rayDepth,
                    farView.y() * depth / rayDepth,
                    -depth, 1.0f);
                if(!qFuzzyIsNull(world.w()))
                    world /= world.w();
                corners.append(world.toVector3D());
            }
        }
    if(ok)
        *ok = corners.size() == 8;
    return corners;
}

}

#endif // SHADOW_CASCADE_H

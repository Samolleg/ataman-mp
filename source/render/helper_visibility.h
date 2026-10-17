#pragma once
#include "view_frustum.h"
#include <QVector>
#include <QtGlobal>

namespace ei {
inline const CViewFrustum*& helperFrustum()
{ static thread_local const CViewFrustum* frustum = nullptr; return frustum; }
class CHelperVisibilityScope
{
public:
    explicit CHelperVisibilityScope(const CViewFrustum* frustum)
        : m_previous(helperFrustum()) { helperFrustum() = frustum; }
    ~CHelperVisibilityScope() { helperFrustum() = m_previous; }
private:
    const CViewFrustum* m_previous;
};
// Include actual projected geometry, not just the owner: a route may enter
// the view while its NPC and both ends of a segment are outside it.
class CHelperBounds
{
public:
    void add(const QVector3D& point, float radius = 0.1f)
    {
        if(!qIsFinite(point.x()) || !qIsFinite(point.y()) || !qIsFinite(point.z())
           || !qIsFinite(radius)) { m_uncertain = true; return; }
        const QVector3D extent(qMax(radius, 0.1f), qMax(radius, 0.1f), qMax(radius, 0.1f));
        const QVector3D low = point - extent, high = point + extent;
        if(!m_valid) { m_low = low; m_high = high; m_valid = true; return; }
        for(int axis = 0; axis < 3; ++axis)
        { m_low[axis] = qMin(m_low[axis], low[axis]); m_high[axis] = qMax(m_high[axis], high[axis]); }
    }
    void add(const QVector<QVector3D>& points)
    { for(const auto& point : points) add(point); }
    bool visible() const
    { return !helperFrustum() || !m_valid || m_uncertain || helperFrustum()->intersectsAabb(m_low, m_high); }
private:
    QVector3D m_low, m_high;
    bool m_valid{false}, m_uncertain{false};
};
}

#ifndef VIEW_FRUSTUM_H
#define VIEW_FRUSTUM_H

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <array>
#include <cmath>

class CViewFrustum
{
public:
    explicit CViewFrustum(const QMatrix4x4& clip)
    {
        m_planes = {{
            plane(clip, 3, 0, 1.0f), plane(clip, 3, 0, -1.0f),
            plane(clip, 3, 1, 1.0f), plane(clip, 3, 1, -1.0f),
            plane(clip, 3, 2, 1.0f), plane(clip, 3, 2, -1.0f)
        }};
    }

    bool intersectsSphere(const QVector3D& center, float radius) const
    {
        const float safeRadius = qMax(0.0f, radius);
        for(const QVector4D& p : m_planes)
        {
            const float distance = p.x() * center.x() + p.y() * center.y()
                    + p.z() * center.z() + p.w();
            if(distance < -safeRadius)
                return false;
        }
        return true;
    }

    bool intersectsAabb(const QVector3D& minimum, const QVector3D& maximum) const
    {
        for(const QVector4D& p : m_planes)
        {
            const QVector3D positive(
                p.x() >= 0.0f ? maximum.x() : minimum.x(),
                p.y() >= 0.0f ? maximum.y() : minimum.y(),
                p.z() >= 0.0f ? maximum.z() : minimum.z());
            if(p.x() * positive.x() + p.y() * positive.y()
                    + p.z() * positive.z() + p.w() < 0.0f)
                return false;
        }
        return true;
    }

private:
    static QVector4D plane(const QMatrix4x4& matrix, int baseRow,
                           int axisRow, float axisSign)
    {
        QVector4D result(
            matrix(baseRow, 0) + axisSign * matrix(axisRow, 0),
            matrix(baseRow, 1) + axisSign * matrix(axisRow, 1),
            matrix(baseRow, 2) + axisSign * matrix(axisRow, 2),
            matrix(baseRow, 3) + axisSign * matrix(axisRow, 3));
        const float length = std::sqrt(result.x() * result.x()
            + result.y() * result.y() + result.z() * result.z());
        if(length > 0.000001f)
            result /= length;
        return result;
    }

    std::array<QVector4D, 6> m_planes;
};

#endif

#ifndef RENDER_QUEUE_H
#define RENDER_QUEUE_H

#include "render_material.h"

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector>
#include <QVector3D>

#include <algorithm>

class CPart;

struct SRenderItem
{
    CPart* part{nullptr};
    QOpenGLTexture* texture{nullptr};
    QMatrix4x4 modelMatrix;
    ERenderMaterialClass materialClass{ERenderMaterialClass::Conservative};
    float cameraDistanceSquared{0.0f};
    bool highlighted{false};
    quintptr meshIdentity{0};
    quint64 sequence{0};
};

inline void sortRenderItems(QVector<SRenderItem>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const SRenderItem& left,
                                                    const SRenderItem& right)
    {
        if(left.highlighted != right.highlighted)
            return !left.highlighted;
        const bool leftTransparent = left.materialClass == ERenderMaterialClass::Transparent
            || left.materialClass == ERenderMaterialClass::Conservative;
        const bool rightTransparent = right.materialClass == ERenderMaterialClass::Transparent
            || right.materialClass == ERenderMaterialClass::Conservative;
        if(leftTransparent != rightTransparent)
            return !leftTransparent;
        if(leftTransparent)
        {
            if(left.cameraDistanceSquared != right.cameraDistanceSquared)
                return left.cameraDistanceSquared > right.cameraDistanceSquared;
            return left.sequence < right.sequence;
        }
        if(left.materialClass != right.materialClass)
            return int(left.materialClass) < int(right.materialClass);
        if(left.texture != right.texture)
            return quintptr(left.texture) < quintptr(right.texture);
        if(left.meshIdentity != right.meshIdentity)
            return left.meshIdentity < right.meshIdentity;
        return left.sequence < right.sequence;
    });
}

class RenderQueueContext
{
public:
    static RenderQueueContext& instance();

    void begin(QOpenGLShaderProgram* program, const QVector3D& cameraPosition);
    void end();
    bool collecting() const { return m_collecting; }
    void submit(CPart* part, QOpenGLTexture* texture, const QMatrix4x4& modelMatrix,
                ERenderMaterialClass materialClass, bool highlighted);
    void flush();

private:
    bool m_collecting{false};
    QOpenGLShaderProgram* m_program{nullptr};
    QVector3D m_cameraPosition;
    QVector<SRenderItem> m_items;
    quint64 m_sequence{0};
};

#endif // RENDER_QUEUE_H

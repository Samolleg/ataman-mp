#ifndef RENDER_QUEUE_H
#define RENDER_QUEUE_H

#include "render_material.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

#include <algorithm>

class CPart;

struct SRenderItem
{
    CPart* part{nullptr};
    QOpenGLTexture* texture{nullptr};
    QMatrix4x4 modelMatrix;
    QMatrix4x4 instanceMatrix;
    ERenderMaterialClass materialClass{ERenderMaterialClass::Conservative};
    float cameraDistanceSquared{0.0f};
    bool highlighted{false};
    quintptr meshIdentity{0};
    float textureUvScale{1.0f};
    bool instanceEligible{false};
    QVector4D pickingColor;
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
        if(left.textureUvScale != right.textureUvScale)
            return left.textureUvScale < right.textureUvScale;
        return left.sequence < right.sequence;
    });
}

inline bool shareInstanceBatch(const SRenderItem& left, const SRenderItem& right)
{
    return left.instanceEligible && right.instanceEligible
        && !left.highlighted && !right.highlighted
        && left.materialClass != ERenderMaterialClass::Transparent
        && left.materialClass != ERenderMaterialClass::Conservative
        && left.materialClass == right.materialClass
        && left.texture == right.texture
        && left.meshIdentity == right.meshIdentity
        && left.textureUvScale == right.textureUvScale;
}

inline int instanceBatchLength(const QVector<SRenderItem>& items, int first)
{
    if(first < 0 || first >= items.size() || !items.at(first).instanceEligible)
        return 1;
    int end = first + 1;
    while(end < items.size() && shareInstanceBatch(items.at(first), items.at(end)))
        ++end;
    return end - first;
}

inline bool supportsInstancedRenderPath(bool isGles, int majorVersion, int minorVersion)
{
    return !isGles && (majorVersion > 3
        || (majorVersion == 3 && minorVersion >= 3));
}

class RenderQueueContext
{
public:
    static RenderQueueContext& instance();

    void begin(QOpenGLShaderProgram* program, const QVector3D& cameraPosition);
    void beginSelection(QOpenGLShaderProgram* program);
    void end();
    void releaseGpuResources();
    bool collecting() const { return m_collecting; }
    bool collectingSelection() const { return m_collecting && m_selectionPass; }
    void submit(CPart* part, QOpenGLTexture* texture, const QMatrix4x4& modelMatrix,
                ERenderMaterialClass materialClass, bool highlighted);
    void submitSelection(CPart* part, const QMatrix4x4& modelMatrix,
                         const QVector4D& pickingColor);
    void flush();

private:
    int reserveInstanceBytes(int bytes);
    bool m_collecting{false};
    bool m_selectionPass{false};
    QOpenGLShaderProgram* m_program{nullptr};
    QVector3D m_cameraPosition;
    QVector<SRenderItem> m_items;
    quint64 m_sequence{0};
    QOpenGLBuffer m_instanceBuffer{QOpenGLBuffer::VertexBuffer};
    bool m_instancingEnabled{false};
    bool m_instancingLogged{false};
    int m_instanceBufferCapacity{0};
    int m_instanceBufferWriteOffset{0};
    bool m_instanceBufferPrepared{false};
};

#endif // RENDER_QUEUE_H

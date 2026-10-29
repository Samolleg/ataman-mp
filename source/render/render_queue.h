#ifndef RENDER_QUEUE_H
#define RENDER_QUEUE_H

#include "render_material.h"

#include <QMatrix4x4>
#include <QElapsedTimer>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <QByteArray>
#include <QSet>

#include <algorithm>
#include <cmath>

class CPart;

struct SRenderItem
{
    CPart* part{nullptr};
    QOpenGLTexture* texture{nullptr};
    QMatrix4x4 modelMatrix;
    QMatrix4x4 instanceMatrix;
    ERenderMaterialClass materialClass{ERenderMaterialClass::Conservative};
    float cameraDistanceSquared{0.0f};
    float cullingDistanceSquared{0.0f};
    bool highlighted{false};
    QVector4D diplomacyTint;
    quintptr meshIdentity{0};
    float textureUvScale{1.0f};
    bool instanceEligible{false};
    QVector4D pickingColor;
    quint64 sequence{0};
    bool castsShadow{false};
    bool dynamicShadow{false};
    bool backgroundScenery{false};
    bool colorVisible{true};
    QVector3D shadowBoundsCenter;
    float shadowBoundsRadius{0.0f};
    QVector3D materialEmission;
    quintptr shadowOwner{0};
};

inline bool renderTextureReady(const QOpenGLTexture* texture)
{
    return texture && texture->isCreated();
}

struct SShadowCasterBounds
{
    QVector3D center;
    float radius{0.0f};
};

struct SShadowCasterGroup
{
    quintptr owner{0};
    QVector3D center;
    float radius{0.0f};
    float cullingDistanceSquared{0.0f};
    bool dynamic{false};
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
        if(left.dynamicShadow != right.dynamicShadow)
            return !left.dynamicShadow;
        if(left.texture != right.texture)
            return quintptr(left.texture) < quintptr(right.texture);
        if(left.meshIdentity != right.meshIdentity)
            return left.meshIdentity < right.meshIdentity;
        if(left.textureUvScale != right.textureUvScale)
            return left.textureUvScale < right.textureUvScale;
        if(left.materialEmission != right.materialEmission)
        {
            if(left.materialEmission.x() != right.materialEmission.x())
                return left.materialEmission.x() < right.materialEmission.x();
            if(left.materialEmission.y() != right.materialEmission.y())
                return left.materialEmission.y() < right.materialEmission.y();
            return left.materialEmission.z() < right.materialEmission.z();
        }
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
        && left.dynamicShadow == right.dynamicShadow
        && left.backgroundScenery == right.backgroundScenery
        && left.shadowOwner == right.shadowOwner
        && left.texture == right.texture
        && left.meshIdentity == right.meshIdentity
        && left.textureUvScale == right.textureUvScale
        && left.materialEmission == right.materialEmission
        && left.diplomacyTint == right.diplomacyTint;
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

inline bool shadowItemWithinDistance(const SRenderItem& item, float maximumDistance)
{
    const float safeDistance = qMax(0.0f, maximumDistance);
    return item.cullingDistanceSquared <= safeDistance * safeDistance;
}

inline bool shadowItemIntersectsLightSpace(const SRenderItem& item,
                                           const QMatrix4x4& lightSpaceMatrix)
{
    const QVector4D projected = lightSpaceMatrix
        * QVector4D(item.shadowBoundsCenter, 1.0f);
    const float absoluteW = qAbs(projected.w());
    if(absoluteW < 0.000001f)
        return false;

    // Shadow cascades use orthographic projections. A directional-light
    // caster cannot affect receivers outside its projected X/Y footprint, so
    // reject it before sorting and submission. This retains off-camera trees
    // and buildings whose shadows enter the viewport while avoiding the old
    // all-casters-within-distance pass on large maps.
    const float radius = qMax(0.0f, item.shadowBoundsRadius);
    const float radiusX = radius * std::sqrt(
        lightSpaceMatrix(0, 0) * lightSpaceMatrix(0, 0)
        + lightSpaceMatrix(0, 1) * lightSpaceMatrix(0, 1)
        + lightSpaceMatrix(0, 2) * lightSpaceMatrix(0, 2)) / absoluteW;
    const float radiusY = radius * std::sqrt(
        lightSpaceMatrix(1, 0) * lightSpaceMatrix(1, 0)
        + lightSpaceMatrix(1, 1) * lightSpaceMatrix(1, 1)
        + lightSpaceMatrix(1, 2) * lightSpaceMatrix(1, 2)) / absoluteW;
    const float x = projected.x() / projected.w();
    const float y = projected.y() / projected.w();
    return x + radiusX >= -1.0f && x - radiusX <= 1.0f
        && y + radiusY >= -1.0f && y - radiusY <= 1.0f;
}

class RenderQueueContext
{
public:
    static RenderQueueContext& instance();

    void begin(QOpenGLShaderProgram* program, const QVector3D& cameraPosition);
    void begin(QOpenGLShaderProgram* program, const QVector3D& cameraPosition,
               const QVector3D& cullingPosition);
    void beginSelection(QOpenGLShaderProgram* program);
    void end();
    void releaseGpuResources();
    bool collecting() const { return m_collecting; }
    bool collectingSelection() const { return m_collecting && m_selectionPass; }
    void setShadowOnlyCollection(bool enabled) { m_shadowOnlyCollection = enabled; }
    void setDiplomacyTint(const QVector4D& tint) { m_diplomacyTint = tint; }
    void setLocalShadowOwners(const QVector<quintptr>& owners)
        { m_localShadowOwners = owners; }
    void submit(CPart* part, QOpenGLTexture* texture, const QMatrix4x4& modelMatrix,
                ERenderMaterialClass materialClass, bool highlighted,
                bool castsShadow = false, bool dynamicShadow = false,
                const QVector3D& materialEmission = QVector3D(),
                quintptr shadowOwner = 0);
    static SRenderItem prepareStaticItem(CPart* part, QOpenGLTexture* texture,
                                        const QMatrix4x4& matrix,
                                        ERenderMaterialClass material, bool shadow,
                                        quintptr owner);
    void submitPrepared(const SRenderItem& item, bool colorVisible, bool shadow);
    void submitSelection(CPart* part, QOpenGLTexture* texture, const QMatrix4x4& modelMatrix,
                         const QVector4D& pickingColor);
    void flush();
    void flushShadow(QOpenGLShaderProgram* shadowProgram,
                     float maximumDistance,
                     const QMatrix4x4& lightSpaceMatrix,
                     bool dynamicOnly = false, bool staticOnly = false,
                     const QSet<quintptr>* excludedOwners = nullptr,
                     quintptr onlyOwner = 0);
    QByteArray staticShadowFingerprint() const;
    QVector<SShadowCasterBounds> shadowCasterBounds(float maximumDistance) const;
    QVector<SShadowCasterGroup> shadowCasterGroups(float maximumDistance) const;

private:
    int reserveInstanceBytes(int bytes);
    bool m_collecting{false};
    bool m_selectionPass{false};
    bool m_shadowOnlyCollection{false};
    QOpenGLShaderProgram* m_program{nullptr};
    QVector3D m_cameraPosition;
    QVector3D m_cullingPosition;
    QVector<SRenderItem> m_items;
    QVector<QMatrix4x4> m_matrixScratch;
    QElapsedTimer m_phaseTimer;
    quint64 m_sequence{0};
    QOpenGLBuffer m_instanceBuffer{QOpenGLBuffer::VertexBuffer};
    bool m_instancingEnabled{false};
    bool m_instancingLogged{false};
    int m_instanceBufferCapacity{0};
    int m_instanceBufferWriteOffset{0};
    bool m_instanceBufferPrepared{false};
    int m_modelMatrixLocation{-1};
    int m_colorLocation{-1};
    int m_instancedLocation{-1};
    int m_partAnimatedLocation{-1};
    int m_highlightLocation{-1};
    int m_materialEmissionLocation{-1};
    int m_diplomacyTintLocation{-1};
    QVector4D m_diplomacyTint;
    QVector<quintptr> m_localShadowOwners;
};

#endif // RENDER_QUEUE_H

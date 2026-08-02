#include "render_queue.h"
#include "gl_dispatch.h"

#include "part.h"
#include "render_performance.h"
#include "log.h"

#include <algorithm>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QCryptographicHash>

namespace
{
bool supportsFixedFunctionAlphaTest()
{
    QOpenGLContext* context = QOpenGLContext::currentContext();
    return context && !context->isOpenGLES();
}
}

RenderQueueContext& RenderQueueContext::instance()
{
    static RenderQueueContext context;
    return context;
}

void RenderQueueContext::begin(QOpenGLShaderProgram* program, const QVector3D& cameraPosition)
{
    m_program = program;
    m_cameraPosition = cameraPosition;
    m_items.clear();
    if(m_items.capacity() < 8192)
        m_items.reserve(8192);
    m_sequence = 0;
    m_instanceBufferWriteOffset = 0;
    m_instanceBufferPrepared = false;
    m_collecting = program != nullptr;
    m_selectionPass = false;
    m_shadowOnlyCollection = false;
    m_modelMatrixLocation = program ? program->uniformLocation("u_modelMmatrix") : -1;
    m_colorLocation = program ? program->uniformLocation("u_color") : -1;
    m_instancedLocation = program ? program->uniformLocation("u_instanced") : -1;
    m_partAnimatedLocation = program ? program->uniformLocation("u_partAnimated") : -1;
    m_highlightLocation = program ? program->uniformLocation("u_highlight") : -1;
    m_materialEmissionLocation = program
        ? program->uniformLocation("u_materialEmission") : -1;
    QOpenGLContext* context = QOpenGLContext::currentContext();
    const QSurfaceFormat format = context ? context->format() : QSurfaceFormat();
    m_instancingEnabled = context && supportsInstancedRenderPath(
        QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES,
        format.majorVersion(), format.minorVersion());
    if(m_instancingEnabled && !m_instanceBuffer.isCreated())
    {
        m_instanceBuffer.create();
        m_instanceBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    }
    m_instancingEnabled = m_instancingEnabled && m_instanceBuffer.isCreated();
    if(!m_instancingLogged)
    {
        ei::log(eLogInfo, QString("Render instancing path:%1 vertex-arrays:%2 OpenGL:%3.%4")
                .arg(m_instancingEnabled ? "enabled" : "legacy-fallback")
                .arg(m_instancingEnabled ? "enabled" : "legacy-fallback")
                .arg(format.majorVersion()).arg(format.minorVersion()));
        m_instancingLogged = true;
    }
    if(m_program)
        m_program->setUniformValue(m_instancedLocation, false);
    m_phaseTimer.restart();
}

void RenderQueueContext::beginSelection(QOpenGLShaderProgram* program)
{
    begin(program, QVector3D());
    m_selectionPass = true;
}

void RenderQueueContext::end()
{
    flush();
    m_collecting = false;
    m_selectionPass = false;
    m_shadowOnlyCollection = false;
    m_program = nullptr;
}

void RenderQueueContext::submitSelection(CPart* part, const QMatrix4x4& modelMatrix,
                                         const QVector4D& pickingColor)
{
    if(!collectingSelection() || !part || !part->isDrawable())
        return;
    SRenderItem item;
    item.part = part;
    item.modelMatrix = modelMatrix;
    item.instanceMatrix = modelMatrix * part->poseMatrix();
    item.materialClass = ERenderMaterialClass::Opaque;
    item.meshIdentity = part->meshIdentity();
    item.textureUvScale = part->textureUvScale();
    item.instanceEligible = part->canInstance();
    item.pickingColor = pickingColor;
    item.sequence = m_sequence++;
    m_items.append(item);
}

void RenderQueueContext::releaseGpuResources()
{
    if(m_instanceBuffer.isCreated())
        m_instanceBuffer.destroy();
    m_instancingEnabled = false;
    m_instanceBufferCapacity = 0;
    m_instanceBufferWriteOffset = 0;
    m_instanceBufferPrepared = false;
}

int RenderQueueContext::reserveInstanceBytes(int bytes)
{
    if(!m_instancingEnabled || bytes <= 0)
        return -1;
    const int required = m_instanceBufferWriteOffset + bytes;
    if(!m_instanceBufferPrepared || required > m_instanceBufferCapacity)
    {
        int capacity = qMax(4096, m_instanceBufferCapacity);
        while(capacity < bytes || (m_instanceBufferPrepared && capacity < required))
            capacity *= 2;
        m_instanceBuffer.bind();
        m_instanceBuffer.allocate(capacity);
        m_instanceBuffer.release();
        m_instanceBufferCapacity = capacity;
        m_instanceBufferWriteOffset = 0;
        m_instanceBufferPrepared = true;
    }
    const int offset = m_instanceBufferWriteOffset;
    m_instanceBufferWriteOffset += bytes;
    return offset;
}

void RenderQueueContext::submit(CPart* part, QOpenGLTexture* texture,
                                const QMatrix4x4& modelMatrix,
                                ERenderMaterialClass materialClass, bool highlighted,
                                bool castsShadow, bool dynamicShadow,
                                const QVector3D& materialEmission,
                                quintptr shadowOwner)
{
    if(!m_collecting || !part || !texture || !part->isDrawable())
        return;
    SRenderItem item;
    item.part = part;
    item.texture = texture;
    item.modelMatrix = modelMatrix;
    item.instanceMatrix = modelMatrix * part->poseMatrix();
    item.materialClass = materialClass;
    item.highlighted = highlighted;
    item.meshIdentity = part->meshIdentity();
    item.textureUvScale = part->textureUvScale();
    item.instanceEligible = part->canInstance();
    item.sequence = m_sequence++;
    item.castsShadow = castsShadow;
    item.dynamicShadow = dynamicShadow;
    item.materialEmission = materialEmission;
    item.shadowOwner = shadowOwner;
    item.colorVisible = !m_shadowOnlyCollection;
    item.shadowBoundsCenter = item.instanceMatrix.map(part->localBoundsCenter());
    const QVector3D scaleX(item.instanceMatrix(0, 0), item.instanceMatrix(1, 0),
                           item.instanceMatrix(2, 0));
    const QVector3D scaleY(item.instanceMatrix(0, 1), item.instanceMatrix(1, 1),
                           item.instanceMatrix(2, 1));
    const QVector3D scaleZ(item.instanceMatrix(0, 2), item.instanceMatrix(1, 2),
                           item.instanceMatrix(2, 2));
    item.shadowBoundsRadius = part->localBoundsRadius()
        * qMax(scaleX.length(), qMax(scaleY.length(), scaleZ.length()));
    const QVector3D position(modelMatrix(0, 3), modelMatrix(1, 3), modelMatrix(2, 3));
    item.cameraDistanceSquared = (position - m_cameraPosition).lengthSquared();
    m_items.append(item);
}

QVector<SShadowCasterGroup> RenderQueueContext::shadowCasterGroups(
        float maximumDistance) const
{
    QVector<SShadowCasterGroup> result;
    QHash<quintptr, int> indices;
    for(const SRenderItem& item : m_items)
    {
        if(!item.castsShadow || !item.shadowOwner
                || !shadowItemWithinDistance(item, maximumDistance)
                || item.shadowBoundsRadius <= 0.0f)
            continue;
        const int existing = indices.value(item.shadowOwner, -1);
        if(existing < 0)
        {
            SShadowCasterGroup group;
            group.owner = item.shadowOwner;
            group.center = item.shadowBoundsCenter;
            group.radius = item.shadowBoundsRadius;
            group.cameraDistanceSquared = item.cameraDistanceSquared;
            group.dynamic = item.dynamicShadow;
            indices.insert(item.shadowOwner, result.size());
            result.append(group);
            continue;
        }
        SShadowCasterGroup& group = result[existing];
        const QVector3D delta = item.shadowBoundsCenter - group.center;
        const float distance = delta.length();
        if(distance + item.shadowBoundsRadius > group.radius)
        {
            const float newRadius = (group.radius + distance
                                     + item.shadowBoundsRadius) * 0.5f;
            if(distance > 0.000001f)
                group.center += delta / distance * (newRadius - group.radius);
            group.radius = newRadius;
        }
        group.cameraDistanceSquared = qMin(group.cameraDistanceSquared,
                                            item.cameraDistanceSquared);
        group.dynamic = group.dynamic || item.dynamicShadow;
    }
    std::sort(result.begin(), result.end(), [](const SShadowCasterGroup& left,
                                               const SShadowCasterGroup& right)
    {
        if(left.cameraDistanceSquared != right.cameraDistanceSquared)
            return left.cameraDistanceSquared < right.cameraDistanceSquared;
        return left.owner < right.owner;
    });
    return result;
}

QVector<SShadowCasterBounds> RenderQueueContext::shadowCasterBounds(
        float maximumDistance) const
{
    QVector<SShadowCasterBounds> result;
    result.reserve(m_items.size());
    for(const SRenderItem& item : m_items)
    {
        if(!item.castsShadow || !shadowItemWithinDistance(item, maximumDistance)
                || item.shadowBoundsRadius <= 0.0f)
            continue;
        SShadowCasterBounds bounds;
        bounds.center = item.shadowBoundsCenter;
        bounds.radius = item.shadowBoundsRadius;
        result.append(bounds);
    }
    return result;
}

QByteArray RenderQueueContext::staticShadowFingerprint() const
{
    QVector<QByteArray> casterRecords;
    casterRecords.reserve(m_items.size());
    for(const SRenderItem& item : m_items)
    {
        if(!item.castsShadow || item.dynamicShadow)
            continue;
        QByteArray record;
        record.reserve(int(sizeof(item.meshIdentity) + sizeof(quintptr)
                           + sizeof(int) + 16 * sizeof(float)));
        record.append(reinterpret_cast<const char*>(&item.meshIdentity),
                      int(sizeof(item.meshIdentity)));
        const quintptr textureIdentity = quintptr(item.texture);
        record.append(reinterpret_cast<const char*>(&textureIdentity),
                      int(sizeof(textureIdentity)));
        const int materialClass = int(item.materialClass);
        record.append(reinterpret_cast<const char*>(&materialClass),
                      int(sizeof(materialClass)));
        record.append(reinterpret_cast<const char*>(item.instanceMatrix.constData()),
                      16 * int(sizeof(float)));
        casterRecords.append(record);
    }
    // Selection changes when a caster is submitted (normal color pass versus
    // shadow-only collection), not what it is. Canonicalize records so that UI
    // selection cannot invalidate and blank/rebuild the static atlases.
    std::sort(casterRecords.begin(), casterRecords.end());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(const QByteArray& record : casterRecords)
        hash.addData(record);
    return hash.result();
}

void RenderQueueContext::flushShadow(QOpenGLShaderProgram* shadowProgram,
                                     float maximumDistance,
                                     const QMatrix4x4& lightSpaceMatrix,
                                     bool dynamicOnly, bool staticOnly,
                                     const QSet<quintptr>* excludedOwners,
                                     quintptr onlyOwner)
{
    if(!shadowProgram || m_items.isEmpty())
        return;
    QElapsedTimer shadowTimer;
    shadowTimer.start();
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    const bool collectPerformance = performance.collecting();
    if(collectPerformance)
    {
        ++performance.counters().shadowPasses;
        performance.counters().shadowCandidates += quint64(m_items.size());
    }
    QVector<SRenderItem> shadowItems;
    shadowItems.reserve(m_items.size());
    for(const SRenderItem& source : m_items)
    {
        if(!source.castsShadow || !shadowItemWithinDistance(source, maximumDistance)
                || !shadowItemIntersectsLightSpace(source, lightSpaceMatrix)
                || (dynamicOnly && !source.dynamicShadow)
                || (staticOnly && source.dynamicShadow)
                || (onlyOwner && source.shadowOwner != onlyOwner)
                || (excludedOwners && excludedOwners->contains(source.shadowOwner)))
            continue;
        SRenderItem item = source;
        item.highlighted = false;
        shadowItems.append(item);
    }
    sortRenderItems(shadowItems);
    if(collectPerformance)
        performance.counters().shadowItems += quint64(shadowItems.size());
    const int modelLocation = shadowProgram->uniformLocation("u_modelMmatrix");
    const int instancedLocation = shadowProgram->uniformLocation("u_instanced");
    const int partAnimatedLocation = shadowProgram->uniformLocation("u_partAnimated");
    const int alphaTestLocation = shadowProgram->uniformLocation("u_shadowAlphaTest");
    QOpenGLTexture* boundTexture = nullptr;
    for(int itemIndex = 0; itemIndex < shadowItems.size(); )
    {
        const SRenderItem& item = shadowItems.at(itemIndex);
        // EI foliage and other textured silhouettes can use either binary or
        // intermediate alpha. Shadow maps need their visible coverage, not an
        // opaque rectangle; threshold both classes conservatively.
        const bool alphaTest = item.materialClass != ERenderMaterialClass::Opaque;
        shadowProgram->setUniformValue(alphaTestLocation, alphaTest);
        if(alphaTest && item.texture && item.texture != boundTexture)
        {
            item.texture->bind(0);
            boundTexture = item.texture;
        }
        const int runLength = m_instancingEnabled
            ? instanceBatchLength(shadowItems, itemIndex) : 1;
        bool instanced = false;
        if(runLength >= 2)
        {
            m_matrixScratch.clear();
            m_matrixScratch.reserve(runLength);
            for(int offset = 0; offset < runLength; ++offset)
                m_matrixScratch.append(shadowItems.at(itemIndex + offset).instanceMatrix);
            const int bufferOffset = reserveInstanceBytes(
                m_matrixScratch.size() * int(sizeof(QMatrix4x4)));
            instanced = bufferOffset >= 0 && item.part->drawInstanced(
                shadowProgram, &m_instanceBuffer, bufferOffset, m_matrixScratch);
        }
        if(!instanced)
        {
            shadowProgram->setUniformValue(instancedLocation, false);
            shadowProgram->setUniformValue(modelLocation, item.modelMatrix);
            item.part->drawImmediate(shadowProgram);
        }
        itemIndex += instanced ? runLength : 1;
    }
    shadowProgram->setUniformValue(instancedLocation, false);
    shadowProgram->setUniformValue(partAnimatedLocation, false);
    shadowProgram->setUniformValue(alphaTestLocation, false);
    if(collectPerformance)
        performance.counters().shadowNanoseconds += quint64(shadowTimer.nsecsElapsed());
}

void RenderQueueContext::flush()
{
    if(!m_program || m_items.isEmpty())
        return;

    QVector<SRenderItem> colorItems;
    colorItems.reserve(m_items.size());
    for(const SRenderItem& item : m_items)
        if(item.colorVisible)
            colorItems.append(item);
    if(colorItems.isEmpty())
    {
        m_items.clear();
        m_phaseTimer.restart();
        return;
    }

    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    const bool collectPerformance = performance.collecting();
    if(collectPerformance)
        performance.counters().queueCollectNanoseconds += quint64(m_phaseTimer.nsecsElapsed());
    m_phaseTimer.restart();
    sortRenderItems(colorItems);
    if(collectPerformance)
        performance.counters().queueSortNanoseconds += quint64(m_phaseTimer.nsecsElapsed());
    m_phaseTimer.restart();

    if(m_selectionPass)
    {
        for(int itemIndex = 0; itemIndex < colorItems.size(); )
        {
            const SRenderItem& item = colorItems.at(itemIndex);
            const int runLength = m_instancingEnabled
                ? instanceBatchLength(colorItems, itemIndex) : 1;
            bool instanced = false;
            if(runLength >= 2)
            {
                QVector<SPartPickingInstance> instances;
                instances.reserve(runLength);
                for(int offset = 0; offset < runLength; ++offset)
                {
                    const SRenderItem& source = colorItems.at(itemIndex + offset);
                    SPartPickingInstance instance;
                    instance.matrix = source.instanceMatrix;
                    instance.color = source.pickingColor;
                    instances.append(instance);
                }
                const int bufferOffset = reserveInstanceBytes(
                    instances.size() * int(sizeof(SPartPickingInstance)));
                instanced = bufferOffset >= 0 && item.part->drawSelectInstanced(
                    m_program, &m_instanceBuffer, bufferOffset, instances);
            }
            if(!instanced)
            {
                m_program->setUniformValue(m_instancedLocation, false);
                m_program->setUniformValue(m_modelMatrixLocation, item.modelMatrix);
                m_program->setUniformValue(m_colorLocation, item.pickingColor);
                item.part->drawSelect(m_program);
            }
            itemIndex += instanced ? runLength : 1;
        }
        m_program->setUniformValue(m_instancedLocation, false);
        m_program->setUniformValue(m_partAnimatedLocation, false);
        m_items.clear();
        m_phaseTimer.restart();
        return;
    }

    if(collectPerformance)
    {
        for(const SRenderItem& item : colorItems)
        {
            if(item.materialClass == ERenderMaterialClass::Opaque)
                ++performance.counters().opaqueQueueSize;
            else if(item.materialClass == ERenderMaterialClass::Cutout)
                ++performance.counters().cutoutQueueSize;
            else
                ++performance.counters().transparentQueueSize;
        }
    }

    QOpenGLTexture* boundTexture = nullptr;
    ERenderMaterialClass activeClass = ERenderMaterialClass::Conservative;
    quintptr previousMesh = 0;
    QOpenGLTexture* previousTexture = nullptr;
    ERenderMaterialClass previousClass = ERenderMaterialClass::Conservative;
    bool previousHighlight = false;
    QVector3D previousEmission;
    bool haveEmission = false;
    int activeLocalShadowSkip = -2;
    float previousUvScale = 0.0f;
    bool haveBatch = false;
    bool activeHighlight = false;
    m_program->setUniformValue(m_highlightLocation, false);
    m_program->setUniformValue("u_receiveDynamicShadow", true);
    for(int itemIndex = 0; itemIndex < colorItems.size(); )
    {
        const SRenderItem& item = colorItems.at(itemIndex);
        if(item.materialClass != activeClass || !haveBatch)
        {
            activeClass = item.materialClass;
            if(activeClass == ERenderMaterialClass::Opaque)
            {
                ei::gl::Disable(GL_BLEND);
                if(supportsFixedFunctionAlphaTest())
                    ei::gl::Disable(GL_ALPHA_TEST);
            }
            else if(activeClass == ERenderMaterialClass::Cutout)
            {
                ei::gl::Disable(GL_BLEND);
                if(supportsFixedFunctionAlphaTest())
                    ei::gl::Enable(GL_ALPHA_TEST);
            }
            else
            {
                if(supportsFixedFunctionAlphaTest())
                    ei::gl::Enable(GL_ALPHA_TEST);
                ei::gl::Enable(GL_BLEND);
            }
        }
        if(item.texture != boundTexture)
        {
            item.texture->bind(0);
            boundTexture = item.texture;
            if(performance.collecting())
                ++performance.counters().textureBinds;
        }
        const quintptr mesh = item.meshIdentity;
        if(!haveBatch || mesh != previousMesh || item.texture != previousTexture
                || item.materialClass != previousClass || item.highlighted != previousHighlight
                || item.textureUvScale != previousUvScale
                || !haveEmission || item.materialEmission != previousEmission)
        {
            if(performance.collecting())
                ++performance.counters().batches;
            haveBatch = true;
            previousMesh = mesh;
            previousTexture = item.texture;
            previousClass = item.materialClass;
            previousHighlight = item.highlighted;
            previousUvScale = item.textureUvScale;
            previousEmission = item.materialEmission;
            haveEmission = true;
        }
        const int runLength = m_instancingEnabled
            ? instanceBatchLength(colorItems, itemIndex) : 1;
        bool instanced = false;
        const int localShadowSkip = m_localShadowOwners.indexOf(item.shadowOwner);
        if(localShadowSkip != activeLocalShadowSkip)
        {
            activeLocalShadowSkip = localShadowSkip;
            m_program->setUniformValue("u_localShadowSkipIndex", localShadowSkip);
        }
        m_program->setUniformValue("u_receiveDynamicShadow", !item.dynamicShadow);
        m_program->setUniformValue(m_materialEmissionLocation, item.materialEmission);
        if(item.highlighted != activeHighlight)
        {
            activeHighlight = item.highlighted;
            m_program->setUniformValue(m_highlightLocation, activeHighlight);
        }
        if(runLength >= 2)
        {
            m_matrixScratch.clear();
            m_matrixScratch.reserve(runLength);
            for(int offset = 0; offset < runLength; ++offset)
                m_matrixScratch.append(colorItems.at(itemIndex + offset).instanceMatrix);
            const int bufferOffset = reserveInstanceBytes(
                m_matrixScratch.size() * int(sizeof(QMatrix4x4)));
            instanced = bufferOffset >= 0 && item.part->drawInstanced(
                m_program, &m_instanceBuffer, bufferOffset, m_matrixScratch);
        }
        if(!instanced)
        {
            m_program->setUniformValue(m_modelMatrixLocation, item.modelMatrix);
            item.part->drawImmediate(m_program);
        }
        itemIndex += instanced ? runLength : 1;
    }
    m_program->setUniformValue(m_instancedLocation, false);
    m_program->setUniformValue(m_partAnimatedLocation, false);
    m_program->setUniformValue(m_highlightLocation, false);
    m_program->setUniformValue("u_receiveDynamicShadow", true);
    m_program->setUniformValue("u_localShadowSkipIndex", -1);
    m_program->setUniformValue(m_materialEmissionLocation, QVector3D());
    if(supportsFixedFunctionAlphaTest())
        ei::gl::Enable(GL_ALPHA_TEST);
    ei::gl::Enable(GL_BLEND);
    if(collectPerformance)
        performance.counters().queueDrawNanoseconds += quint64(m_phaseTimer.nsecsElapsed());
    m_items.clear();
    m_phaseTimer.restart();
}

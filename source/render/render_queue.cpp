#include "render_queue.h"

#include "part.h"
#include "render_performance.h"
#include "log.h"

#include <algorithm>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QCryptographicHash>

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
    m_modelMatrixLocation = program ? program->uniformLocation("u_modelMmatrix") : -1;
    m_colorLocation = program ? program->uniformLocation("u_color") : -1;
    m_instancedLocation = program ? program->uniformLocation("u_instanced") : -1;
    m_partAnimatedLocation = program ? program->uniformLocation("u_partAnimated") : -1;
    m_highlightLocation = program ? program->uniformLocation("u_highlight") : -1;
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
                                bool castsShadow, bool dynamicShadow)
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
    const QVector3D position(modelMatrix(0, 3), modelMatrix(1, 3), modelMatrix(2, 3));
    item.cameraDistanceSquared = (position - m_cameraPosition).lengthSquared();
    m_items.append(item);
}

QByteArray RenderQueueContext::staticShadowFingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(const SRenderItem& item : m_items)
    {
        if(!item.castsShadow || item.dynamicShadow)
            continue;
        hash.addData(reinterpret_cast<const char*>(&item.meshIdentity), sizeof(item.meshIdentity));
        hash.addData(reinterpret_cast<const char*>(item.instanceMatrix.constData()),
                     16 * int(sizeof(float)));
    }
    return hash.result();
}

void RenderQueueContext::flushShadow(QOpenGLShaderProgram* shadowProgram,
                                     bool dynamicOnly, bool staticOnly)
{
    if(!shadowProgram || m_items.isEmpty())
        return;
    QVector<SRenderItem> shadowItems;
    shadowItems.reserve(m_items.size());
    for(const SRenderItem& source : m_items)
    {
        if(!source.castsShadow || (dynamicOnly && !source.dynamicShadow)
                || (staticOnly && source.dynamicShadow))
            continue;
        SRenderItem item = source;
        item.texture = nullptr;
        item.materialClass = ERenderMaterialClass::Opaque;
        item.highlighted = false;
        shadowItems.append(item);
    }
    sortRenderItems(shadowItems);
    const int modelLocation = shadowProgram->uniformLocation("u_modelMmatrix");
    const int instancedLocation = shadowProgram->uniformLocation("u_instanced");
    const int partAnimatedLocation = shadowProgram->uniformLocation("u_partAnimated");
    for(int itemIndex = 0; itemIndex < shadowItems.size(); )
    {
        const SRenderItem& item = shadowItems.at(itemIndex);
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
}

void RenderQueueContext::flush()
{
    if(!m_program || m_items.isEmpty())
        return;

    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    const bool collectPerformance = performance.collecting();
    if(collectPerformance)
        performance.counters().queueCollectNanoseconds += quint64(m_phaseTimer.nsecsElapsed());
    m_phaseTimer.restart();
    sortRenderItems(m_items);
    if(collectPerformance)
        performance.counters().queueSortNanoseconds += quint64(m_phaseTimer.nsecsElapsed());
    m_phaseTimer.restart();

    if(m_selectionPass)
    {
        for(int itemIndex = 0; itemIndex < m_items.size(); )
        {
            const SRenderItem& item = m_items.at(itemIndex);
            const int runLength = m_instancingEnabled
                ? instanceBatchLength(m_items, itemIndex) : 1;
            bool instanced = false;
            if(runLength >= 2)
            {
                QVector<SPartPickingInstance> instances;
                instances.reserve(runLength);
                for(int offset = 0; offset < runLength; ++offset)
                {
                    const SRenderItem& source = m_items.at(itemIndex + offset);
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
        for(const SRenderItem& item : m_items)
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
    float previousUvScale = 0.0f;
    bool haveBatch = false;
    bool activeHighlight = false;
    m_program->setUniformValue(m_highlightLocation, false);
    for(int itemIndex = 0; itemIndex < m_items.size(); )
    {
        const SRenderItem& item = m_items.at(itemIndex);
        if(item.materialClass != activeClass || !haveBatch)
        {
            activeClass = item.materialClass;
            if(activeClass == ERenderMaterialClass::Opaque)
            {
                glDisable(GL_BLEND);
                glDisable(GL_ALPHA_TEST);
            }
            else if(activeClass == ERenderMaterialClass::Cutout)
            {
                glDisable(GL_BLEND);
                glEnable(GL_ALPHA_TEST);
            }
            else
            {
                glEnable(GL_ALPHA_TEST);
                glEnable(GL_BLEND);
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
                || item.textureUvScale != previousUvScale)
        {
            if(performance.collecting())
                ++performance.counters().batches;
            haveBatch = true;
            previousMesh = mesh;
            previousTexture = item.texture;
            previousClass = item.materialClass;
            previousHighlight = item.highlighted;
            previousUvScale = item.textureUvScale;
        }
        const int runLength = m_instancingEnabled
            ? instanceBatchLength(m_items, itemIndex) : 1;
        bool instanced = false;
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
                m_matrixScratch.append(m_items.at(itemIndex + offset).instanceMatrix);
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
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    if(collectPerformance)
        performance.counters().queueDrawNanoseconds += quint64(m_phaseTimer.nsecsElapsed());
    m_items.clear();
    m_phaseTimer.restart();
}

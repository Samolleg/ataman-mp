#include "render_queue.h"

#include "part.h"
#include "render_performance.h"

#include <algorithm>

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
    m_sequence = 0;
    m_collecting = program != nullptr;
}

void RenderQueueContext::end()
{
    flush();
    m_collecting = false;
    m_program = nullptr;
}

void RenderQueueContext::submit(CPart* part, QOpenGLTexture* texture,
                                const QMatrix4x4& modelMatrix,
                                ERenderMaterialClass materialClass, bool highlighted)
{
    if(!m_collecting || !part || !texture || !part->isDrawable())
        return;
    SRenderItem item;
    item.part = part;
    item.texture = texture;
    item.modelMatrix = modelMatrix;
    item.materialClass = materialClass;
    item.highlighted = highlighted;
    item.meshIdentity = part->meshIdentity();
    item.sequence = m_sequence++;
    const QVector3D position(modelMatrix(0, 3), modelMatrix(1, 3), modelMatrix(2, 3));
    item.cameraDistanceSquared = (position - m_cameraPosition).lengthSquared();
    m_items.append(item);
}

void RenderQueueContext::flush()
{
    if(!m_program || m_items.isEmpty())
        return;

    sortRenderItems(m_items);

    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
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
    bool haveBatch = false;
    for(const SRenderItem& item : m_items)
    {
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
                || item.materialClass != previousClass || item.highlighted != previousHighlight)
        {
            if(performance.collecting())
                ++performance.counters().batches;
            haveBatch = true;
            previousMesh = mesh;
            previousTexture = item.texture;
            previousClass = item.materialClass;
            previousHighlight = item.highlighted;
        }
        m_program->setUniformValue("u_modelMmatrix", item.modelMatrix);
        m_program->setUniformValue("u_highlight", item.highlighted);
        item.part->drawImmediate(m_program);
    }
    m_program->setUniformValue("u_partAnimated", false);
    m_program->setUniformValue("u_highlight", false);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    m_items.clear();
}

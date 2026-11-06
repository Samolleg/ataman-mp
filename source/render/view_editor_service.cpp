#include "view.h"
#include "camera.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "unit.h"
#include "scene.h"
#include "settings.h"
#include "render_queue.h"
#include "render_performance.h"
#include "render_stall_trace.h"
#include "view_frustum.h"
#include "helper_visibility.h"
#include "shader_locations.h"
#include "gl_dispatch.h"
#include "ogl_utils.h"

void CView::drawEditorObjectServices(const QMatrix4x4& camMatrix,bool cullingEnabled,bool traverseHelpers,CRenderStallTrace* trace)
{
    const CViewFrustum frustum(m_projection*camMatrix);
    auto& renderQueue=RenderQueueContext::instance();auto& performance=RenderPerformanceContext::instance();
    const QVector4D activeHighlight(.05f,.78f,.93f,0),lockedHighlight(1,.55f,.05f,0);
    ei::gl::DepthFunc(GL_LEQUAL);
    for(CMob* mob : m_aMob)
        if(!m_minimapCaptureActive && isMobVisible(mob))
            for(CNode* node : mob->nodes())
                if(node->nodeState() == ENodeState::eSelect)
                {
                    if(node==m_connectionGhostNode && !m_connectionGhostVertices.isEmpty())continue;
                    if(!isObjectVisualVisible(node))
                        continue;
                    m_program.setUniformValue(ei::uniformLocation(&m_program, "u_highlightColor"),
                        mob == m_activeMob ? activeHighlight : lockedHighlight);
                    drawDiplomacyObjectVisual(node, mob == m_activeMob);
                    renderQueue.flush();
                    if(performance.collecting())
                    {
                        ++performance.counters().nodesSubmitted;
                    }
                }
    m_program.setUniformValue(ei::uniformLocation(&m_program, "u_highlightColor"), activeHighlight);
    renderQueue.flush();
    if(m_editorServicePass) {
        for(CMob* mob:m_aMob)if(isMobVisible(mob))for(CNode* node:mob->nodes())
            if(node->nodeState()!=ENodeState::eSelect && isObjectVisualVisible(node)
               && (node->nodeType()==ENodeType::eLight || node->nodeType()==ENodeType::eSound || node->nodeType()==ENodeType::eParticle))
                drawDiplomacyObjectVisual(node,mob==m_activeMob);
        renderQueue.flush();
    }
    const auto drawBehaviorHelpers = [&]()
    {
        if(m_minimapCaptureActive)
            return;
        ei::CHelperVisibilityScope helperVisibility(cullingEnabled ? &frustum : nullptr);
        for(CMob* mob : m_aMob)
        {
            if(!isMobVisible(mob))
                continue;
            for(CNode* node : mob->nodes())
            {
                if(!traverseHelpers && node->nodeState() != ENodeState::eSelect)
                    continue;
                if(node->nodeType() != ENodeType::eUnit && node->nodeType() != ENodeType::eMagicTrap)
                    continue;
                node->drawLogicVisual(mob == m_activeMob, &m_program);
                if(performance.collecting())
                    ++performance.counters().logicSubmissions;
            }
        }
        // Patrol/look/trap marker models join the object queue, while their
        // relationship lines draw immediately. Flush only after terrain
        // and selected objects are complete so neither can paint over the
        // helpers.
        renderQueue.flush();
    };
    if(trace)trace->mark("selection-highlight");
    drawBehaviorHelpers();
    ei::gl::DepthFunc(GL_LESS);
}

QList<CNode*> CView::drawEditorPickIds(const QMatrix4x4& viewMatrix)
{
    m_selectProgram.setUniformValue(ei::uniformLocation(&m_selectProgram, "u_projMmatrix"), m_projection);
    m_selectProgram.setUniformValue(ei::uniformLocation(&m_selectProgram, "u_viewMmatrix"), viewMatrix);

    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const bool cullingEnabled = (!cullingOption || cullingOption->value())
        && (!drawLogicOption || !drawLogicOption->value())
        && CScene::getInstance()->getMode() != eEditModeLogic;
    const CViewFrustum frustum(m_projection * viewMatrix);

    QList<CNode*> pickableNodes;
    const bool logicPicking = !m_diplomacyMapMode && CScene::getInstance()->getMode() == eEditModeLogic;
    if(!logicPicking)
        RenderQueueContext::instance().beginSelection(&m_selectProgram);
    for(CMob* mob : m_aMob)
        if(isMobVisible(mob))
        {
            const QVector<CNode*> spatialCandidates = logicPicking
                ? QVector<CNode*>()
                : spatiallyVisibleNodes(mob, cullingEnabled ? &frustum : nullptr, nullptr);
            const QList<CNode*>& legacyCandidates = logicPicking
                ? mob->logicNodes() : mob->nodes();
            if(logicPicking)
                for(CNode* node : legacyCandidates)
                {
                    if(node && isObjectVisualVisible(node)
                       && !pickableNodes.contains(node))
                    {
                        pickableNodes.append(node);
                        node->drawSelect(&m_selectProgram);
                    }
                }
            else
                for(CNode* node : spatialCandidates)
                {
                    if(m_diplomacyMapMode && !dynamic_cast<CUnit*>(node)) continue;
                    if(node && isObjectVisualVisible(node)
                       && !pickableNodes.contains(node))
                    {
                        pickableNodes.append(node);
                        node->drawSelect(&m_selectProgram);
                    }
                }
        }
    if(!logicPicking)
        RenderQueueContext::instance().end();


    return pickableNodes;
}

void CView::drawEditorServiceScene()
{
    const auto camera=m_cam->viewMatrix();
    ei::gl::Enable(GL_DEPTH_TEST);ei::gl::DepthFunc(GL_LESS);ei::gl::DepthMask(GL_TRUE);
    ei::gl::ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    ei::gl::ClearColor(0,0,0,0);ei::gl::Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    // Reuse the editor query geometry/alpha policy as depth. No GL scene colour,
    // lighting, shadow-map producer, water, particle or material postprocessing.
    m_landProgram.bind();
    for(const char* name:{"u_environmentEnabled","u_fogEnabled","u_shadowsEnabled"})m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,name),false);
    m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,"u_pointLightCount"),0);
    drawSelectionTerrainDepth(camera);
    if(m_selectProgram.bind())drawEditorPickIds(camera);
    ei::gl::ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);ei::gl::ClearColor(0,0,0,0);ei::gl::Clear(GL_COLOR_BUFFER_BIT);
    if(!m_program.bind())return;
    QMatrix4x4 identity;
    m_program.setUniformValue(ei::uniformLocation(&m_program,"u_modelMmatrix"),identity);
    m_program.setUniformValue(ei::uniformLocation(&m_program,"u_projMmatrix"),m_projection);
    m_program.setUniformValue(ei::uniformLocation(&m_program,"u_viewMmatrix"),camera);
    m_program.setUniformValue(ei::uniformLocation(&m_program,"qt_Texture0"),0);
    m_program.setUniformValue(ei::uniformLocation(&m_program,"u_cartographicPass"),0);
    m_program.setUniformValue(ei::uniformLocation(&m_program,"u_highlightColor"),QVector4D(.05f,.78f,.93f,0));
    m_program.setUniformValue(ei::uniformLocation(&m_program,"u_pointLightCount"),0);
    for(const char* name:{"u_environmentEnabled","u_fogEnabled","u_shadowsEnabled","u_instanced","u_partAnimated"})m_program.setUniformValue(ei::uniformLocation(&m_program,name),false);
    ei::gl::Enable(GL_BLEND);QOpenGLContext::currentContext()->functions()->glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    const auto eye=camera.inverted().map(QVector3D());auto& queue=RenderQueueContext::instance();queue.begin(&m_program,eye,eye);
    const auto flag=[this](const char* name,bool value){const auto* option=dynamic_cast<const COptBool*>(m_pSettings->opt(name));return option?option->value():value;};
    drawEditorObjectServices(camera,flag("frustumCulling",true),flag("drawLogic",false)||flag("drawHelp",false)||CScene::getInstance()->getMode()==eEditModeLogic);
    if(m_editorServicePass && m_tileGridVisible && m_landscapeVisible && m_bDrawLand && m_pLand) {
        m_landProgram.bind();
        m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,"u_solidColorEnabled"),true);
        m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,"u_solidColor"),QVector4D(.05f,.9f,1,1));
        ei::gl::DepthFunc(GL_LEQUAL);m_pLand->drawTileGrid(&m_landProgram);ei::gl::DepthFunc(GL_LESS);
        m_landProgram.setUniformValue(ei::uniformLocation(&m_landProgram,"u_solidColorEnabled"),false);
    }

    queue.end();m_program.bind();
    if(m_pLand && m_pLand->isMprLoad()&&!m_aMob.isEmpty()) {
        m_program.setUniformValue(ei::uniformLocation(&m_program,"u_projMmatrix"),identity);
        m_program.setUniformValue(ei::uniformLocation(&m_program,"u_viewMmatrix"),identity);m_selectFrame->draw(&m_program);
    }
}

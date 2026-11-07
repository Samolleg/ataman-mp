#include "opening_trace.h"
#include "view.h"
#include <QVariantList>

void CView::useLegacyViewport(const QString& action)
{
    if(property("rebuild64Selected").toBool())
        emit legacyViewportRequested(action);
}

QVariantList CView::legacyVisualState() const
{
    // Neighbours, editor helpers, entrances and the world clock have a shared
    // native input/service path. These GL image policies do not yet have one.
    return {m_sceneLightsVisible, m_terrainShadowsVisible, m_objectShadowsVisible,
        m_particleVisualsVisible, m_freezeAnimations};
}

void CView::ensureLegacyQueryContext()
{
    opening::Scope queryTiming("gui.gl.query_context");
    makeCurrent();
    if(!m_program.isLinked()) {glInit();resizeGL(qMax(1,width()),qMax(1,height()));}
}

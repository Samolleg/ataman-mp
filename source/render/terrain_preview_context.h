#pragma once
#include <QGLWidget>

// Keep the viewport context alive across nested active/neighbor GPU uploads.
class CTerrainPreviewContext
{
public:
    explicit CTerrainPreviewContext(QGLWidget* widget)
        : m_widget(widget), m_previous(QGLContext::currentContext()),
          m_acquired(widget && widget->isValid() && m_previous != widget->context())
    { if(m_acquired) m_widget->makeCurrent(); }
    ~CTerrainPreviewContext()
    {
        if(!m_acquired) return;
        if(m_previous) const_cast<QGLContext*>(m_previous)->makeCurrent();
        else m_widget->doneCurrent();
    }
    CTerrainPreviewContext(const CTerrainPreviewContext&) = delete;
    CTerrainPreviewContext& operator=(const CTerrainPreviewContext&) = delete;
private:
    QGLWidget* m_widget;
    const QGLContext* m_previous;
    bool m_acquired;
};

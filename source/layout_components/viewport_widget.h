#ifndef VIEWPORT_WIDGET_H
#define VIEWPORT_WIDGET_H

#include <QContextMenuEvent>
#include <QBrush>
#include <QFrame>
#include <QHideEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

// Base for controls displayed over CView.  Plain QWidget/QFrame backgrounds
// ignore pointer events by default, which lets Qt propagate them to CView and
// accidentally starts viewport picking or transforms underneath the control.
class CViewportWidget: public QFrame
{
public:
    explicit CViewportWidget(QWidget* parent = nullptr):
        QFrame(parent),
        m_surfaceBrush(palette().brush(QPalette::Window))
    {
        // CView is an OpenGL surface.  Overlay widgets must own and paint every
        // pixel of their backing rectangle; otherwise an old GL frame can be
        // retained in newly exposed portions after an overlay changes size.
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAttribute(Qt::WA_StyledBackground, true);
        setAutoFillBackground(true);
    }

    void setViewportSurfaceColor(const QColor& color)
    {
        QColor opaque = color;
        opaque.setAlpha(255);
        m_surfaceBrush = QBrush(opaque);
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }
    void wheelEvent(QWheelEvent* event) override { event->accept(); }
    void contextMenuEvent(QContextMenuEvent* event) override { event->accept(); }

    void paintEvent(QPaintEvent* event) override
    {
        // Do not rely on a stylesheet or a child widget to cover the backing
        // store.  Give the complete overlay a deterministic opaque base first.
        {
            QPainter painter(this);
            // Keep this independent from palette(). Stylesheets can replace
            // QPalette::Window with a translucent rounded brush, which would
            // reintroduce holes in the overlay's GL backing surface.
            painter.fillRect(rect(), m_surfaceBrush);
        }
        QFrame::paintEvent(event);
    }

    void moveEvent(QMoveEvent* event) override
    {
        const QRect oldGeometry(event->oldPos(), size());
        QFrame::moveEvent(event);
        invalidateParent(oldGeometry, geometry());
    }

    void resizeEvent(QResizeEvent* event) override
    {
        const QRect oldGeometry(pos(), event->oldSize());
        QFrame::resizeEvent(event);
        invalidateParent(oldGeometry, geometry());
    }

    void showEvent(QShowEvent* event) override
    {
        QFrame::showEvent(event);
        invalidateParent(QRect(), geometry());
    }

    void hideEvent(QHideEvent* event) override
    {
        const QRect oldGeometry = geometry();
        QFrame::hideEvent(event);
        invalidateParent(oldGeometry, QRect());
    }

private:
    QBrush m_surfaceBrush;

    void invalidateParent(const QRect& oldGeometry, const QRect& newGeometry)
    {
        QRegion dirty;
        if(oldGeometry.isValid() && !oldGeometry.isEmpty())
            dirty += oldGeometry.adjusted(-1, -1, 1, 1);
        if(newGeometry.isValid() && !newGeometry.isEmpty())
            dirty += newGeometry.adjusted(-1, -1, 1, 1);

        if(QWidget* viewport = parentWidget())
            viewport->update(dirty);

        // A resize can otherwise leave unchanged child backing-store fragments
        // composited over the freshly painted frame until their next input.
        update();
        const QList<QWidget*> children = findChildren<QWidget*>(
            QString(), Qt::FindChildrenRecursively);
        for(QWidget* child : children)
            child->update();
    }
};

#endif // VIEWPORT_WIDGET_H

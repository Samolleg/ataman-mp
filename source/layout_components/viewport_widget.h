#ifndef VIEWPORT_WIDGET_H
#define VIEWPORT_WIDGET_H

#include <QContextMenuEvent>
#include <QFrame>
#include <QMouseEvent>
#include <QWheelEvent>

// Base for controls displayed over CView.  Plain QWidget/QFrame backgrounds
// ignore pointer events by default, which lets Qt propagate them to CView and
// accidentally starts viewport picking or transforms underneath the control.
class CViewportWidget: public QFrame
{
public:
    explicit CViewportWidget(QWidget* parent = nullptr):
        QFrame(parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }
    void wheelEvent(QWheelEvent* event) override { event->accept(); }
    void contextMenuEvent(QContextMenuEvent* event) override { event->accept(); }
};

#endif // VIEWPORT_WIDGET_H

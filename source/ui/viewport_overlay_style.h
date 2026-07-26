#ifndef VIEWPORT_OVERLAY_STYLE_H
#define VIEWPORT_OVERLAY_STYLE_H

#include <QColor>
#include <QPalette>
#include <QWidget>

// Native child widgets composited over QGLWidget must paint every pixel of
// their backing surface. Without this contract, stale OpenGL frames can show
// through after camera motion or partial child-widget updates.
inline void configureViewportOverlaySurface(
    QWidget* widget, const QColor& background = QColor(25, 25, 25))
{
    if(!widget) return;
    widget->setAttribute(Qt::WA_OpaquePaintEvent, true);
    widget->setAutoFillBackground(true);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, background);
    widget->setPalette(palette);
}

#endif

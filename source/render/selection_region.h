#pragma once

#include <QRect>
#include <QSize>

// Qt mouse coordinates name inclusive pixels from the top left. OpenGL reads
// from the bottom left. Normalize before clipping, then flip the last row.
inline QRect selectionReadRegion(const QRect& mouseRect, const QSize& viewport)
{
    if(viewport.isEmpty() || mouseRect.width() == 0 || mouseRect.height() == 0)
        return QRect();
    const QRect clipped = mouseRect.normalized().intersected(
        QRect(QPoint(0, 0), viewport));
    if(clipped.isEmpty())
        return QRect();
    return QRect(clipped.left(), viewport.height() - 1 - clipped.bottom(),
                 clipped.width(), clipped.height());
}

#ifndef WINDOW_GEOMETRY_H
#define WINDOW_GEOMETRY_H
#include <QRect>
#include <QVector>
#include <QString>
class QWidget;
namespace ei {
// Explicit, stable IDs keep unrelated dialogs (and translated titles) separate.
void rememberWindowGeometry(QWidget* window, const QString& id);
// Used by ordinary windows and floating dock hosts after a monitor disappears.
QRect accessibleWindowGeometry(const QRect& frame, const QVector<QRect>& screens,
                               int preferredScreen = 0);
void keepWindowOnScreen(QWidget* window);
}
#endif

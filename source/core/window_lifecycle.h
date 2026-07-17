#ifndef WINDOW_LIFECYCLE_H
#define WINDOW_LIFECYCLE_H

#include <QApplication>
#include <QWidget>

namespace ei {

inline void closeSecondaryWindows(QWidget* primaryWindow)
{
    // Every top-level widget in this process belongs to Endurance. Closing the
    // complete snapshot covers correctly parented tool windows as well as future
    // dialogs that are accidentally constructed without an owner.
    const QWidgetList windows = QApplication::topLevelWidgets();
    for(QWidget* window : windows)
    {
        if(!window || window == primaryWindow)
            continue;
        window->close();
        if(window->isVisible())
            window->hide();
    }
}

}

#endif // WINDOW_LIFECYCLE_H

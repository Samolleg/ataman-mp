#ifndef WINDOW_LIFECYCLE_H
#define WINDOW_LIFECYCLE_H

#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>
#include <QWidget>

namespace ei {

inline void revealDockNavigationTarget(QMainWindow* window, QDockWidget* dock,
                                       QWidget* sourceWindow = nullptr)
{
    if(!window || !dock)
        return;
    dock->show();
    if(dock->isFloating())
    {
        if(dock->isMinimized())dock->showNormal();
        dock->raise();
        dock->activateWindow();
    }
    else
    {
        QMainWindow* host=qobject_cast<QMainWindow*>(dock->window());
        if(!host)host=window;
        if(host->isMinimized())host->showNormal();
        else host->show();
        host->raise();
        host->activateWindow();
        // QDockWidget::raise() activates this dock when it is one tab in a
        // tabified dock area without changing its docking/floating state.
        dock->raise();
        QWidget* source=sourceWindow?sourceWindow->window():nullptr;
        if(source&&source!=host
           &&!source->frameGeometry().intersects(host->frameGeometry()))
            source->raise();
    }
    dock->setFocus(Qt::OtherFocusReason);
}

inline void closeSecondaryWindows(QWidget* primaryWindow)
{
    // Every top-level widget in this process belongs to Endurance. Closing the
    // complete snapshot covers correctly parented tool windows as well as future
    // dialogs that are accidentally constructed without an owner.
    QList<QPointer<QWidget>> windows;
    for(auto* window:QApplication::topLevelWidgets())windows.append(window);
    for(const auto& window : windows)
    {
        if(!window || window == primaryWindow)
            continue;
        // A WA_DeleteOnClose tool window (or a custom close handler) may
        // destroy itself from close(). Guard the follow-up visibility check.
        QPointer<QWidget> guardedWindow(window);
        guardedWindow->close();
        if(guardedWindow && guardedWindow->isVisible())
            guardedWindow->hide();
    }
}

}

#endif // WINDOW_LIFECYCLE_H

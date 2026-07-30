#ifndef DOCK_HIDDEN_TAB_H
#define DOCK_HIDDEN_TAB_H

#include <QObject>

class QMainWindow;
class QDockWidget;
class QTabBar;

namespace ei {
void enableNativeFloatingDockControls(QDockWidget* dock);
}

class CDockHiddenTab : public QObject
{
    Q_OBJECT
public:
    CDockHiddenTab(QMainWindow* window, QDockWidget* dock,
                   const QString& label, const QString& objectName);
    QTabBar* tabBar() const { return m_tab; }
    void refresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateNow();
    QMainWindow* m_window{nullptr};
    QDockWidget* m_dock{nullptr};
    QTabBar* m_tab{nullptr};
    quint64 m_generation{0};
};

#endif

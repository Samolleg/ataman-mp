#include "dock_hidden_tab.h"

#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QTabBar>
#include <QTimer>
#include <QWidget>
#include <QVariant>

CDockHiddenTab::CDockHiddenTab(QMainWindow* window, QDockWidget* dock,
                               const QString& label, const QString& objectName)
    : QObject(window), m_window(window), m_dock(dock)
{
    m_tab = new QTabBar(window);
    m_tab->setObjectName(objectName);
    m_tab->setExpanding(false);
    m_tab->setDrawBase(false);
    m_tab->addTab(label);
    m_tab->hide();
    connect(m_tab, &QTabBar::tabBarClicked, this, [this](int) {
        m_dock->show();
        m_dock->raise();
        m_dock->setFocus(Qt::ShortcutFocusReason);
        refresh();
    });
    connect(m_dock, &QDockWidget::visibilityChanged, this, [this](bool) { refresh(); });
    connect(m_dock, &QDockWidget::topLevelChanged, this, [this](bool) { refresh(); });
    connect(m_dock, &QDockWidget::dockLocationChanged, this,
            [this](Qt::DockWidgetArea) { refresh(); });
    window->installEventFilter(this);
    refresh();
}

void CDockHiddenTab::refresh()
{
    const quint64 generation = ++m_generation;
    QTimer::singleShot(0, this, [this, generation]() {
        if(generation == m_generation) updateNow();
    });
}

bool CDockHiddenTab::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == m_window && (event->type() == QEvent::Resize
       || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show))
        refresh();
    return QObject::eventFilter(watched, event);
}

void CDockHiddenTab::updateNow()
{
    const Qt::DockWidgetArea area = m_window->dockWidgetArea(m_dock);
    const bool visible = !m_window->property("enduranceViewportOnly").toBool()
            && !m_dock->isVisible() && !m_dock->isFloating()
            && area != Qt::NoDockWidgetArea;
    if(!visible) { m_tab->hide(); return; }

    switch(area) {
    case Qt::LeftDockWidgetArea: m_tab->setShape(QTabBar::RoundedWest); break;
    case Qt::RightDockWidgetArea: m_tab->setShape(QTabBar::RoundedEast); break;
    case Qt::TopDockWidgetArea: m_tab->setShape(QTabBar::RoundedNorth); break;
    default: m_tab->setShape(QTabBar::RoundedSouth); break;
    }
    m_tab->adjustSize();
    const QRect target = m_window->centralWidget()
        ? m_window->centralWidget()->geometry() : m_window->rect();
    const QSize size = m_tab->sizeHint();
    if(area == Qt::LeftDockWidgetArea)
        m_tab->move(target.left(), target.top() + 12);
    else if(area == Qt::RightDockWidgetArea)
        m_tab->move(target.right() - size.width() + 1, target.top() + 12);
    else if(area == Qt::TopDockWidgetArea)
        m_tab->move(target.left() + 12, target.top());
    else
        m_tab->move(target.left() + 12, target.bottom() - size.height() + 1);
    m_tab->show();
    m_tab->raise();
}

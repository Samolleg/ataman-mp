#include "dock_hidden_tab.h"

#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QTabBar>
#include <QTimer>
#include <QWidget>
#include <QVariant>
#include <QApplication>
#include <QIcon>
#include <QScreen>
#include <QSettings>
#include <QWindow>

namespace {
const char* kFloatingMaximized = "enduranceFloatingMaximized";
const char* kFloatingNormalGeometry = "enduranceFloatingNormalGeometry";
const char* kFloatingScreen = "enduranceFloatingScreen";

QString settingsPrefix(QDockWidget* dock)
{
    return QStringLiteral("ui/floatingDocks/%1/").arg(dock->objectName());
}

QWidget* separateHost(QDockWidget* dock)
{
    QWidget* host = dock ? dock->window() : nullptr;
    return host && !qobject_cast<QMainWindow*>(host) ? host : nullptr;
}

QScreen* hostScreen(QWidget* host)
{
    if(!host) return nullptr;
    if(host->windowHandle() && host->windowHandle()->screen())
        return host->windowHandle()->screen();
    return QApplication::screenAt(host->geometry().center());
}

void rememberHost(QDockWidget* dock, QWidget* host, bool maximized)
{
    if(!dock || !host) return;
    dock->setProperty(kFloatingMaximized, maximized);
    const QRect normal = host->normalGeometry().isValid()
            ? host->normalGeometry() : host->geometry();
    if(normal.isValid()) dock->setProperty(kFloatingNormalGeometry, normal);
    if(QScreen* screen = hostScreen(host))
        dock->setProperty(kFloatingScreen, screen->name());
}

void refreshContents(QDockWidget* dock)
{
    if(!dock || !dock->widget()) return;
    dock->widget()->update();
    const QList<QWidget*> descendants = dock->widget()->findChildren<QWidget*>();
    for(QWidget* child : descendants)
        if(child->isVisible()) child->update();
    dock->widget()->repaint();
    if(QWidget* host = separateHost(dock))
    {
        host->update();
        host->repaint();
    }
}

void applyNativeWindowType(QDockWidget* dock, QWidget* host)
{
    if(!dock || !host || host->property("enduranceApplyingDockWindowFlags").toBool())
        return;
    const Qt::WindowFlags required = Qt::Window | Qt::WindowTitleHint |
        Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint |
        Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint;
    const Qt::WindowFlags current = host->windowFlags();
    if(host->windowType() == Qt::Window && (current & required) == required)
        return;

    const bool visible = host->isVisible();
    const QRect geometry = host->geometry();
    const QRect normalGeometry = host->normalGeometry().isValid()
            ? host->normalGeometry() : geometry;
    const Qt::WindowStates state = host->windowState();
    QScreen* screen = hostScreen(host);
    host->setProperty("enduranceApplyingDockWindowFlags", true);
    const Qt::WindowFlags nativeFlags =
            (current & ~Qt::WindowType_Mask) | required;
    host->setWindowFlags(nativeFlags);
    const QIcon icon = dock->windowIcon().isNull()
        ? QApplication::windowIcon() : dock->windowIcon();
    if(!icon.isNull()) host->setWindowIcon(icon);
    if(visible)
    {
        host->setGeometry(normalGeometry);
        host->show();
        if(screen && host->windowHandle()) host->windowHandle()->setScreen(screen);
        if(state.testFlag(Qt::WindowMaximized)) host->showMaximized();
        else if(state.testFlag(Qt::WindowMinimized)) host->showMinimized();
        else host->setGeometry(geometry);
    }
    host->setProperty("enduranceApplyingDockWindowFlags", false);
}

void restoreHostFromDock(QDockWidget* dock)
{
    QWidget* host = separateHost(dock);
    if(!host) return;
    applyNativeWindowType(dock, host);
    const QRect normal = dock->property(kFloatingNormalGeometry).toRect();
    const QString screenName = dock->property(kFloatingScreen).toString();
    QScreen* target = nullptr;
    for(QScreen* screen : QApplication::screens())
        if(screen->name() == screenName) { target = screen; break; }
    if(target && host->windowHandle()) host->windowHandle()->setScreen(target);
    if(normal.isValid()) host->setGeometry(normal);
    if(dock->property(kFloatingMaximized).toBool()) host->showMaximized();
}

class CFloatingDockController final : public QObject
{
public:
    explicit CFloatingDockController(QDockWidget* dock)
        : QObject(dock), m_dock(dock)
    {
        setObjectName(QStringLiteral("enduranceFloatingDockController"));
        connect(dock, &QDockWidget::topLevelChanged, this,
                [this](bool) { scheduleSync(); });
        connect(dock, &QDockWidget::visibilityChanged, this,
                [this](bool visible) {
            scheduleSync();
            if(visible) QTimer::singleShot(0, m_dock, [this]() { refreshContents(m_dock); });
        });
        scheduleSync();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if(watched != m_host) return QObject::eventFilter(watched, event);
        if(event->type() == QEvent::WindowStateChange)
        {
            if(m_host->isMaximized()) rememberHost(m_dock, m_host, true);
            else
            {
                // Qt briefly restores a maximized private group host while a
                // second floating dock is inserted. Delay clearing the intent;
                // a newly adopted group host can reapply it first.
                QWidget* observed = m_host;
                QTimer::singleShot(350, this, [this, observed]() {
                    if(m_host == observed && observed && !observed->isMaximized())
                        rememberHost(m_dock, observed, false);
                });
            }
        }
        else if(event->type() == QEvent::Move || event->type() == QEvent::Resize)
        {
            QWidget* observed = m_host;
            QTimer::singleShot(250, this, [this, observed]() {
                if(m_host == observed && observed && !observed->isMaximized())
                    rememberHost(m_dock, observed, false);
            });
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void scheduleSync()
    {
        QTimer::singleShot(0, this, [this]() { sync(); });
    }

    void sync()
    {
        QWidget* host = separateHost(m_dock);
        if(host != m_host)
        {
            if(m_host) m_host->removeEventFilter(this);
            m_host = host;
            if(m_host) m_host->installEventFilter(this);
        }
        if(!m_host) return;
        applyNativeWindowType(m_dock, m_host);
        // A host change is the reliable signal that Qt has created a floating
        // tab group. Reapply the preserved second-screen/maximized intent.
        if(m_dock->property(kFloatingMaximized).toBool())
            restoreHostFromDock(m_dock);
        refreshContents(m_dock);
    }

    QDockWidget* m_dock{nullptr};
    QPointer<QWidget> m_host;
};
}

void ei::enableNativeFloatingDockControls(QDockWidget* dock)
{
    if(!dock || dock->property("enduranceFloatingDockControllerInstalled").toBool())
        return;
    dock->setProperty("enduranceFloatingDockControllerInstalled", true);
    new CFloatingDockController(dock);
}

void ei::saveFloatingDockWindowState(QDockWidget* dock)
{
    QWidget* host = separateHost(dock);
    if(host) rememberHost(dock, host, host->isMaximized());
    QSettings settings;
    const QString prefix = settingsPrefix(dock);
    settings.setValue(prefix + QStringLiteral("maximized"),
                      dock->property(kFloatingMaximized));
    settings.setValue(prefix + QStringLiteral("normalGeometry"),
                      dock->property(kFloatingNormalGeometry));
    settings.setValue(prefix + QStringLiteral("screen"),
                      dock->property(kFloatingScreen));
}

void ei::restoreFloatingDockWindowState(QDockWidget* dock)
{
    if(!dock) return;
    QSettings settings;
    const QString prefix = settingsPrefix(dock);
    dock->setProperty(kFloatingMaximized,
                      settings.value(prefix + QStringLiteral("maximized"), false));
    dock->setProperty(kFloatingNormalGeometry,
                      settings.value(prefix + QStringLiteral("normalGeometry")));
    dock->setProperty(kFloatingScreen,
                      settings.value(prefix + QStringLiteral("screen")));
    QTimer::singleShot(0, dock, [dock]() { restoreHostFromDock(dock); });
}

void ei::refreshFloatingDockTabs(QDockWidget* active, QDockWidget* peer)
{
    if(!active) return;
    QTimer::singleShot(0, active, [active, peer]() {
        // Either tab may retain the maximized intent while Qt is replacing the
        // two standalone hosts with one QDockWidgetGroupWindow.
        if(active->property(kFloatingMaximized).toBool()) restoreHostFromDock(active);
        else if(peer && peer->property(kFloatingMaximized).toBool()) restoreHostFromDock(peer);
        refreshContents(active);
        if(peer) refreshContents(peer);
    });
}

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

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
#include <QLayout>
#include <QScreen>
#include <QSettings>
#include <QWindow>
#include "log.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

QString hostDescription(QDockWidget* dock, QWidget* host, const QString& reason)
{
    const QRect geometry = host ? host->geometry() : QRect();
    const QRect normal = host && host->normalGeometry().isValid()
            ? host->normalGeometry() : QRect();
    QScreen* screen = hostScreen(host);
    return QStringLiteral("DOCK_WINDOW reason:%1 dock:%2 host:0x%3 class:%4 type:%5 "
                          "visible:%6 maximized:%7 geometry:%8,%9,%10,%11 "
                          "normal:%12,%13,%14,%15 screen:%16 dpi:%17 dpr:%18")
            .arg(reason, dock ? dock->objectName() : QStringLiteral("none"))
            .arg(quintptr(host), 0, 16)
            .arg(host ? host->metaObject()->className() : "none")
            .arg(host ? int(host->windowType()) : -1)
            .arg(host && host->isVisible()).arg(host && host->isMaximized())
            .arg(geometry.x()).arg(geometry.y()).arg(geometry.width()).arg(geometry.height())
            .arg(normal.x()).arg(normal.y()).arg(normal.width()).arg(normal.height())
            .arg(screen ? screen->name() : QStringLiteral("none"))
            .arg(screen ? screen->logicalDotsPerInch() : 0.0, 0, 'f', 2)
            .arg(screen ? screen->devicePixelRatio() : 1.0, 0, 'f', 2);
}

void logHost(QDockWidget* dock, QWidget* host, const QString& reason)
{
    ei::log(eLogInfo, hostDescription(dock, host, reason));
}

void applyTaskbarIdentity(QWidget* host)
{
#ifdef Q_OS_WIN
    if(!host) return;
    HWND hwnd = reinterpret_cast<HWND>(host->winId());
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~LONG_PTR(WS_POPUP);
    style |= WS_OVERLAPPEDWINDOW;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle &= ~LONG_PTR(WS_EX_TOOLWINDOW);
    exStyle |= WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    // Retain Qt ownership for docking, but remove the native owner that makes
    // Windows intentionally omit the floating diagnostics host from taskbar.
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(host);
#endif
}

void copyRememberedState(QDockWidget* source, QDockWidget* target)
{
    if(!source || !target) return;
    target->setProperty(kFloatingMaximized, source->property(kFloatingMaximized));
    target->setProperty(kFloatingNormalGeometry, source->property(kFloatingNormalGeometry));
    target->setProperty(kFloatingScreen, source->property(kFloatingScreen));
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
        if(host->layout())
        {
            host->layout()->invalidate();
            host->layout()->activate();
        }
        host->update();
        host->repaint();
#ifdef Q_OS_WIN
        RedrawWindow(reinterpret_cast<HWND>(host->winId()), nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                     RDW_FRAME | RDW_UPDATENOW);
#endif
    }
}

void applyNativeWindowType(QDockWidget* dock, QWidget* host)
{
    if(!dock || !host || host->property("enduranceApplyingDockWindowFlags").toBool())
        return;
#ifdef Q_OS_WIN
    // Do not call QWidget::setWindowFlags() on QDockWidgetGroupWindow. Qt 5.12
    // destroys/recreates its HWND and backing store, which can duplicate the
    // host and leave the inactive tab painted until a resize. Update the same
    // native HWND in place instead.
    applyTaskbarIdentity(host);
    const QIcon icon = dock->windowIcon().isNull()
        ? QApplication::windowIcon() : dock->windowIcon();
    if(!icon.isNull()) host->setWindowIcon(icon);
    logHost(dock, host, QStringLiteral("native-window-applied-in-place"));
    return;
#else
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
    applyTaskbarIdentity(host);
    host->setProperty("enduranceApplyingDockWindowFlags", false);
    logHost(dock, host, QStringLiteral("native-window-applied"));
#endif
}

void restoreHostFromDock(QDockWidget* dock)
{
    QWidget* host = separateHost(dock);
    if(!host) return;
    applyNativeWindowType(dock, host);
    applyTaskbarIdentity(host);
    const bool wantsMaximized = dock->property(kFloatingMaximized).toBool();
    // A different tab may already have restored this shared host. Never let a
    // stale small normal geometry resize an already-maximized group window.
    if(host->isMaximized())
    {
        if(wantsMaximized) rememberHost(dock, host, true);
        logHost(dock, host, QStringLiteral("restore-skipped-host-already-maximized"));
        return;
    }
    const QRect normal = dock->property(kFloatingNormalGeometry).toRect();
    const QString screenName = dock->property(kFloatingScreen).toString();
    QScreen* target = nullptr;
    for(QScreen* screen : QApplication::screens())
        if(screen->name() == screenName) { target = screen; break; }
    if(target && host->windowHandle()) host->windowHandle()->setScreen(target);
    if(normal.isValid()) host->setGeometry(normal);
    if(wantsMaximized) host->showMaximized();
    applyTaskbarIdentity(host);
    logHost(dock, host, QStringLiteral("state-restored"));
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
            QWidget* previous = m_host;
            if(m_host) m_host->removeEventFilter(this);
            m_host = host;
            if(m_host) m_host->installEventFilter(this);
            ei::log(eLogInfo, QStringLiteral(
                    "DOCK_WINDOW host-changed dock:%1 old:0x%2 new:0x%3")
                    .arg(m_dock->objectName()).arg(quintptr(previous), 0, 16)
                    .arg(quintptr(host), 0, 16));
        }
        if(!m_host) return;
        applyNativeWindowType(m_dock, m_host);
        applyTaskbarIdentity(m_host);
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
    logHost(dock, host, QStringLiteral("state-saved"));
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
    ei::log(eLogInfo, QStringLiteral(
            "DOCK_WINDOW restore-request dock:%1 maximized:%2 screen:%3")
            .arg(dock->objectName())
            .arg(dock->property(kFloatingMaximized).toBool())
            .arg(dock->property(kFloatingScreen).toString()));
    QTimer::singleShot(0, dock, [dock]() { restoreHostFromDock(dock); });
}

void ei::refreshFloatingDockTabs(QDockWidget* active, QDockWidget* peer)
{
    if(!active) return;
    QTimer::singleShot(0, active, [active, peer]() {
        QWidget* activeHost = separateHost(active);
        const bool sharedHost = peer && activeHost && separateHost(peer) == activeHost;
        // Reconcile once for the whole shared host. A maximized member wins;
        // its monitor and normal geometry become the group's state.
        QDockWidget* source = active;
        if(peer && peer->property(kFloatingMaximized).toBool()
           && !active->property(kFloatingMaximized).toBool())
            source = peer;
        if(sharedHost && peer)
        {
            if(activeHost->isMaximized()) rememberHost(source, activeHost, true);
            copyRememberedState(source, active);
            copyRememberedState(source, peer);
        }
        restoreHostFromDock(source);
        logHost(source, activeHost, QStringLiteral("tabs-reconciled"));
        active->show();
        active->raise();
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

#include "window_geometry.h"
#include <QApplication>
#include <QEvent>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QWidget>

namespace {
class WindowGeometry final : public QObject
{
public:
    WindowGeometry(QWidget* window, const QString& id)
        : QObject(window), m_window(window), m_key("ui/windows/" + id + "/geometry")
    {
        m_timer.setSingleShot(true);
        m_timer.setInterval(250);
        connect(&m_timer, &QTimer::timeout, this, [this]() { save(); });
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { save(); });
        window->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if(!m_window->isWindow()) return false;
        if(event->type() == QEvent::Show && !m_restoring)
        {
            m_restoring = true;
            // Restore after constructor defaults and layouts, also for reused forms.
            const QByteArray geometry = m_geometry.isEmpty()
                ? QSettings().value(m_key).toByteArray() : m_geometry;
            if(!geometry.isEmpty()) m_window->restoreGeometry(geometry);
            ei::keepWindowOnScreen(m_window);
            m_seen = true;
            m_window->setProperty("windowGeometryRestored", !geometry.isEmpty());
            m_restoring = false;
            capture();
        }
        else if(m_seen && !m_restoring)
        {
            if(event->type() == QEvent::Hide)
                save(); // Accept, Cancel, Esc and a closeEvent that only hides.
            else if((event->type() == QEvent::Move || event->type() == QEvent::Resize
                    || event->type() == QEvent::WindowStateChange) && m_window->isVisible())
                capture();
        }
        return false;
    }
private:
    void capture()
    {
        if(!m_window->isMinimized()) m_geometry = m_window->saveGeometry();
        m_timer.start();
    }
    void save()
    {
        m_timer.stop();
        if(!m_seen || m_geometry.isEmpty()) return;
        QSettings settings;
        if(settings.value(m_key).toByteArray() != m_geometry)
        {
            settings.setValue(m_key, m_geometry);
            settings.sync();
        }
    }
    QWidget* m_window;
    QString m_key;
    QTimer m_timer;
    QByteArray m_geometry;
    bool m_seen = false, m_restoring = false;
};
}

void ei::rememberWindowGeometry(QWidget* window, const QString& id)
{
    if(!window || id.isEmpty() || window->property("windowGeometryTracked").toBool()) return;
    window->setProperty("windowGeometryTracked", true);
    new WindowGeometry(window, id);
}

QRect ei::accessibleWindowGeometry(const QRect& frame, const QVector<QRect>& screens,
                                  int preferredScreen)
{
    if(screens.isEmpty() || !frame.isValid()) return frame;
    int target = qBound(0, preferredScreen, screens.size()-1);
    int area = 0;
    for(int i=0; i<screens.size(); ++i)
    {
        const QRect overlap = frame.intersected(screens[i]);
        const int candidate = overlap.width()*overlap.height();
        if(candidate > area) { area = candidate; target = i; }
    }
    const QRect available = screens[target];
    QRect result(frame.topLeft(), frame.size().boundedTo(available.size()));
    result.moveLeft(qBound(available.left(), result.left(), available.right()-result.width()+1));
    result.moveTop(qBound(available.top(), result.top(), available.bottom()-result.height()+1));
    return result;
}

void ei::keepWindowOnScreen(QWidget* window)
{
    if(!window || window->isMaximized() || window->isFullScreen()) return;
    QVector<QRect> available;
    int preferred = 0;
    const auto screens = QApplication::screens();
    for(int i=0; i<screens.size(); ++i)
    {
        available.append(screens[i]->availableGeometry());
        if(window->parentWidget() && screens[i]->geometry().contains(
               window->parentWidget()->frameGeometry().center())) preferred = i;
    }
    const QRect frame = window->frameGeometry();
    const QRect adjusted = accessibleWindowGeometry(frame, available, preferred);
    if(frame == adjusted) return;
    const QSize borders = frame.size() - window->size();
    window->resize(adjusted.size() - borders);
    window->move(adjusted.topLeft());
}

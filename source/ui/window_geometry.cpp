#include "window_geometry.h"
#include <QApplication>
#include <QDataStream>
#include <QEvent>
#include <QLayout>
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
            m_timer.stop();
            const QByteArray geometry = m_geometry.isEmpty()
                ? QSettings().value(m_key).toByteArray() : m_geometry;
            if(!geometry.isEmpty()) ei::restoreWindowGeometry(m_window, geometry);
            ei::keepWindowOnScreen(m_window);
            m_seen = true;
            m_window->setProperty("windowGeometryRestored", !geometry.isEmpty());
            // An event filter runs BEFORE the widget's showEvent. Dynamic
            // forms can still have a temporary, larger minimum size here.
            // Keep the requested bytes until showEvent and its layouts finish,
            // otherwise that temporary clamp becomes the next saved geometry.
            QTimer::singleShot(0, this, [this, geometry]() {
                if(m_window->layout()) m_window->layout()->activate();
                if(!geometry.isEmpty()) ei::restoreWindowGeometry(m_window, geometry);
                ei::keepWindowOnScreen(m_window);
                m_restoring = false;
                capture();
            });
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

bool ei::restoreWindowGeometry(QWidget* window, const QByteArray& geometry)
{
    // Qt 5.12 rejects otherwise valid saved geometry when the logical screen
    // width differs by more than 25%. Keep its rectangles, states and unknown
    // suffix intact, updating only that versioned screen-width sanity field.
    QByteArray adjusted = geometry;
    QDataStream input(geometry);
    input.setVersion(QDataStream::Qt_4_0);
    quint32 magic = 0;
    quint16 major = 0, minor = 0;
    input >> magic >> major >> minor;
    if(input.status() == QDataStream::Ok && magic == 0x1d9d0cb && (major == 2 || major == 3))
    {
        QRect frame, normal;
        qint32 screen = 0, savedWidth = 0;
        quint8 maximized = 0, fullscreen = 0;
        input >> frame >> normal >> screen >> maximized >> fullscreen;
        const qint64 widthOffset = input.device()->pos();
        input >> savedWidth;
        const auto screens = QApplication::screens();
        QScreen* target = screens.value(screen, QApplication::primaryScreen());
        if(input.status() == QDataStream::Ok && target && savedWidth > 0)
        {
            const int width = target->geometry().width();
            const qreal ratio = qreal(savedWidth) / qMax(1, width);
            if(ratio < 0.8 || ratio > 1.25)
            {
                QDataStream output(&adjusted, QIODevice::ReadWrite);
                output.setVersion(QDataStream::Qt_4_0);
                output.device()->seek(widthOffset);
                output << qint32(width);
            }
        }
    }
    return window && window->restoreGeometry(adjusted);
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

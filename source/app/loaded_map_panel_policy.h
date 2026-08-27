#ifndef LOADED_MAP_PANEL_POLICY_H
#define LOADED_MAP_PANEL_POLICY_H

#include <QTimer>

#include <functional>

// QTreeWidgetItem::setData emits itemChanged before returning. Loaded-map
// visibility handlers can synchronously change the view state, so rebuilding
// (and clearing) that same tree in the notification stack destroys the item
// whose setData call is still active. Always cross one event-loop boundary and
// coalesce redundant state notifications before performing a destructive
// loaded-map panel refresh.
inline bool scheduleLoadedMapPanelRefresh(
    QObject* context, bool* pending, const std::function<void()>& refresh)
{
    if(!context || !pending || *pending || !refresh)
        return false;
    *pending = true;
    QTimer::singleShot(0, context, [pending, refresh]() {
        *pending = false;
        refresh();
    });
    return true;
}

#endif

#pragma once
#include <QObject>
#include <QByteArray>

// Publication events, rather than dirty flags: failed saves and Undo back to
// the saved geometry must not start the external baker.
class CNavigationSaveEvents : public QObject {
    Q_OBJECT
public:
    static CNavigationSaveEvents* instance();
    static bool geometryChanged(const QString& path, const QByteArray& before,
                                const QByteArray& after);
    static void committed(const QString& path, const QByteArray& before,
                          const QByteArray& after);
signals:
    void mapSaved(const QString& path, bool navigationChanged);
};

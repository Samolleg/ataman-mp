#ifndef EXTERNAL_SCRIPT_WORKSPACE_H
#define EXTERNAL_SCRIPT_WORKSPACE_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QPointer>

class CMob;
class QWidget;

class CExternalScriptWorkspaceManager final : public QObject
{
    Q_OBJECT
public:
    explicit CExternalScriptWorkspaceManager(QWidget* promptParent, QObject* parent = nullptr);

    QString registerMob(CMob* mob);
    void unregisterMob(CMob* mob);
    bool openEditor(CMob* mob);
    QString workspacePath(CMob* mob) const;

signals:
    void scriptImportAccepted(CMob* mob, const QString& storedScript,
                              const QString& displayScript);

private slots:
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& directory);
    void processPendingChange();

private:
    static QString displayScript(CMob* mob);
    static bool encodeScript(const QString& display, QString* stored);
    QString createWorkspacePath(CMob* mob) const;
    void schedulePath(const QString& path, int delayMs = 250);
    void continuePending();

    QPointer<QWidget> m_promptParent;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QHash<CMob*, QString> m_mobPaths;
    QHash<QString, CMob*> m_pathMobs;
    QHash<QString, int> m_readRetries;
    QHash<QString, QString> m_lastHandledText;
    QSet<QString> m_pendingPaths;
    bool m_promptOpen;
};

#endif

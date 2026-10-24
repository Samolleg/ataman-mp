#ifndef AI_AGENT_SERVER_H
#define AI_AGENT_SERVER_H
#include "ai_agent_edit.h"
#include <QtNetwork>
#include <QtWidgets>
#include <QFutureWatcher>
class MainWindow;
class CView;

// Human-owned authority and lifetime. No transport operation can enable this server.
class CAiAgentServer : public QObject {
    Q_OBJECT
    friend int runAssemblyCatalogCheck();
public:
    explicit CAiAgentServer(MainWindow* window);
    ~CAiAgentServer();
    QDockWidget* dock() const { return m_dock; }
    void stop();
    bool enabled() const { return m_server.isListening(); }
private:
    struct Job {
        ai::Cancellation cancel{std::make_shared<std::atomic_bool>(false)};
        QPointer<QProcess> process;
        QPointer<QFutureWatcher<QJsonObject>> watcher;
        QPointer<QFutureWatcher<ai::EditPlan>> editWatcher;
        QPointer<QLocalSocket> owner;
        QSharedPointer<ai::EditPlan> plan;
        bool edit{false};
        QJsonObject result;
        QDateTime created{QDateTime::currentDateTimeUtc()};
        int deadlineMs{30000};
        bool complete{false};
    };
    void setEnabled(int mode);
    void event(const QString& message);
    void accept();
    void receive(QLocalSocket* socket);
    QJsonObject dispatch(const QString& method, const QJsonObject& args);
    QJsonObject startJob(const QString& method, const QJsonObject& args);
    QJsonObject startEdit(const QString& method,const QJsonObject& args);
    QJsonObject startCommit(const QSharedPointer<ai::EditPlan>& plan,bool snapshot);
    void finishCommit(const QString& id,const QSharedPointer<ai::EditPlan>& plan,bool snapshot);
    void cancelJob(Job& job);
    void prune();
    void configureClient();
    QPointer<MainWindow> m_window;
    QPointer<CView> m_view;
    QLocalServer m_server;
    QMap<QLocalSocket*,QByteArray> m_clients;
    QMap<QString,Job> m_jobs;
    QString m_id, m_secret, m_descriptor;
    QDockWidget* m_dock;
    QComboBox* m_mode;
    QCheckBox* m_allowSave;
    QPointer<QLocalSocket> m_currentClient,m_writer;
    bool m_committing{false};
    std::shared_ptr<std::atomic_int> m_editWorkers{std::make_shared<std::atomic_int>(0)};
    QLabel* m_status;
    QPlainTextEdit* m_events;
    QTimer m_expiry;
};
#endif

#ifndef MALACHITE_BRIDGE_H
#define MALACHITE_BRIDGE_H
#include <QWidget>
#include <QProcess>
#include <QJsonObject>
class CMob;
class CView;

// Owns only its companion process. CMob/CView retain data, history and Save.
class CMalachiteBridge final : public QWidget
{
    Q_OBJECT
public:
    CMalachiteBridge(CMob* mob, CView* view, QWidget* parent = nullptr);
    ~CMalachiteBridge() override;
    static QString executable();
    void start(bool verification = false);
    int command(const QString& name, QJsonObject data = {});
    void synchronize();
    QString text() const { return m_text; }
    bool ready() const { return m_ready; }
    bool hasDraft() const { return m_draft; }
    QString failure() const { return m_failure; }
signals:
    void opened(bool embedded);
    void draftChanged();
    void failed(const QString& message);
    void response(int id, const QJsonObject& result);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void receive();
    void send(const QJsonObject& object);
    void fail(const QString& message);
    QString displayText() const;
    QJsonObject snapshot() const;
    CMob* m_mob;
    CView* m_view;
    QProcess m_process;
    QByteArray m_input;
    QString m_text, m_failure;
    qint64 m_revision = 0;
    int m_serial = 0;
    bool m_ready = false, m_inCall = false, m_stopping = false, m_draft = false, m_failed = false;
};
#endif

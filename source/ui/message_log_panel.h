#ifndef MESSAGE_LOG_PANEL_H
#define MESSAGE_LOG_PANEL_H

#include <QWidget>
#include <QVector>
#include "../core/log.h"

class QLineEdit;
class QTableWidget;
class QToolButton;
class QLabel;
class QTimer;

class CMessageLogPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CMessageLogPanel(QWidget* parent = nullptr);
    int visibleMessageCount() const;
    int totalMessageCount() const { return m_messages.size(); }
    void setTypeVisible(ELogMessageType type, bool visible);
    void setSearchText(const QString& text);

private slots:
    void queueMessage(const SLogMessage& message);
    void flushPending();
    void refreshFilter();

private:
    bool matches(const SLogMessage& message) const;
    void appendRow(const SLogMessage& message);
    QString typeName(ELogMessageType type) const;

    QLineEdit* m_search{nullptr};
    QTableWidget* m_table{nullptr};
    QLabel* m_status{nullptr};
    QToolButton* m_error{nullptr};
    QToolButton* m_warning{nullptr};
    QToolButton* m_info{nullptr};
    QToolButton* m_debug{nullptr};
    QTimer* m_flushTimer{nullptr};
    QVector<SLogMessage> m_messages;
    QVector<SLogMessage> m_pending;
};

#endif

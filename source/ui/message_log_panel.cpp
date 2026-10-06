#include "ui_theme.h"
#include "message_log_panel.h"

#include <QApplication>
#include <QClipboard>
#include "verified_clipboard.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QSet>
#include <QShortcut>
#include <QColor>
#include <algorithm>

CMessageLogPanel::CMessageLogPanel(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("messageLogPanel"));
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    QHBoxLayout* filters = new QHBoxLayout;
    filters->setSpacing(4);
    const auto addType = [filters, this](const QString& text, const QString& name)
    {
        QToolButton* button = new QToolButton(this);
        button->setObjectName(name);
        button->setText(text);
        button->setCheckable(true);
        button->setChecked(true);
        filters->addWidget(button);
        connect(button, &QToolButton::toggled, this, &CMessageLogPanel::refreshFilter);
        return button;
    };
    m_error = addType(tr("Errors"), QStringLiteral("messageLogErrors"));
    m_warning = addType(tr("Warnings"), QStringLiteral("messageLogWarnings"));
    m_info = addType(tr("Info"), QStringLiteral("messageLogInfo"));
    m_debug = addType(tr("Debug"), QStringLiteral("messageLogDebug"));
    m_error->setStyleSheet(QStringLiteral("QToolButton:checked { %1 }").arg(ei::uiTextStyle(ei::UiTextTone::Error)));
    m_warning->setStyleSheet(QStringLiteral("QToolButton:checked { %1 }").arg(ei::uiTextStyle(ei::UiTextTone::Warning)));
    m_debug->setStyleSheet(QStringLiteral("QToolButton:checked { %1 }").arg(ei::uiTextStyle(ei::UiTextTone::Muted)));
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("messageLogSearch"));
    m_search->setPlaceholderText(tr("Filter messages..."));
    m_search->setClearButtonEnabled(true);
    filters->addWidget(m_search, 1);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("messageLogStatus"));
    filters->addWidget(m_status);
    root->addLayout(filters);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("messageLogTable"));
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("Type"), tr("Message")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(1, 90);
    m_table->setColumnWidth(2, 640);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(m_table, 1);
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int value) {
        if(m_programmaticScroll) return;
        QScrollBar* bar = m_table->verticalScrollBar();
        m_followNewest = value >= bar->maximum() - 1;
    });

    connect(m_search, &QLineEdit::textChanged, this, &CMessageLogPanel::refreshFilter);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        QMenu menu(this);
        QAction* copy = menu.addAction(tr("Copy selected messages"));
        copy->setEnabled(!m_table->selectedItems().isEmpty());
        if(menu.exec(m_table->viewport()->mapToGlobal(point)) != copy)
            return;
        copySelectedMessages();
    });
    QShortcut* copyShortcut = new QShortcut(QKeySequence::Copy, m_table);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated,
            this, &CMessageLogPanel::copySelectedMessages);

    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(25);
    connect(m_flushTimer, &QTimer::timeout, this, &CMessageLogPanel::flushPending);
    CLogger* logger = CLogger::getInstance();
    connect(logger, &CLogger::messageLogged, this, &CMessageLogPanel::queueMessage,
            Qt::QueuedConnection);
    m_messages = logger->sessionMessages();
    for(const SLogMessage& message : m_messages) appendRow(message);
    refreshFilter();
    QTimer::singleShot(0, this, [this]() {
        if(!m_followNewest) return;
        m_programmaticScroll = true;
        m_table->scrollToBottom();
        m_programmaticScroll = false;
    });
}

QString CMessageLogPanel::typeName(ELogMessageType type) const
{
    switch(type) {
    case eLogFatal: return tr("Error");
    case eLogWarning: return tr("Warning");
    case eLogDebug: return tr("Debug");
    case eLogInit: return tr("Session");
    default: return tr("Info");
    }
}

void CMessageLogPanel::queueMessage(const SLogMessage& message)
{
    if(!m_messages.isEmpty() && message.sequence <= m_messages.last().sequence)
        return;
    m_pending.append(message);
    if(!m_flushTimer->isActive()) m_flushTimer->start();
}

void CMessageLogPanel::flushPending()
{
    const bool follow = m_followNewest;
    m_programmaticScroll = true;
    for(const SLogMessage& message : qAsConst(m_pending)) {
        m_messages.append(message);
        appendRow(message);
    }
    m_pending.clear();
    refreshFilter();
    if(follow) m_table->scrollToBottom();
    m_programmaticScroll = false;
    m_followNewest = follow;
}

void CMessageLogPanel::appendRow(const SLogMessage& message)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    QTableWidgetItem* time = new QTableWidgetItem(message.timestamp.toString("hh:mm:ss"));
    QTableWidgetItem* type = new QTableWidgetItem(typeName(message.type));
    QTableWidgetItem* body = new QTableWidgetItem(message.message);
    body->setData(Qt::UserRole, message.formatted);
    body->setData(Qt::UserRole + 1, int(message.type));
    if(message.type == eLogFatal)
        type->setForeground(ei::uiTextColor(ei::UiTextTone::Error));
    else if(message.type == eLogWarning)
        type->setForeground(ei::uiTextColor(ei::UiTextTone::Warning));
    else if(message.type == eLogDebug)
        type->setForeground(ei::uiTextColor(ei::UiTextTone::Muted));
    m_table->setItem(row, 0, time);
    m_table->setItem(row, 1, type);
    m_table->setItem(row, 2, body);
}

bool CMessageLogPanel::matches(const SLogMessage& message) const
{
    bool typeVisible = true;
    if(message.type == eLogFatal) typeVisible = m_error->isChecked();
    else if(message.type == eLogWarning) typeVisible = m_warning->isChecked();
    else if(message.type == eLogDebug) typeVisible = m_debug->isChecked();
    else typeVisible = m_info->isChecked();
    const QString search = m_search->text().trimmed();
    return typeVisible && (search.isEmpty()
        || message.message.contains(search, Qt::CaseInsensitive)
        || typeName(message.type).contains(search, Qt::CaseInsensitive));
}

void CMessageLogPanel::refreshFilter()
{
    const bool follow = m_followNewest;
    m_programmaticScroll = true;
    int visible = 0;
    for(int row = 0; row < m_table->rowCount() && row < m_messages.size(); ++row) {
        const bool show = matches(m_messages.at(row));
        m_table->setRowHidden(row, !show);
        if(show) ++visible;
    }
    m_status->setText(tr("%1 / %2").arg(visible).arg(m_messages.size()));
    if(follow) m_table->scrollToBottom();
    m_programmaticScroll = false;
    m_followNewest = follow;
}

int CMessageLogPanel::visibleMessageCount() const
{
    int count = 0;
    for(int row = 0; row < m_table->rowCount(); ++row) if(!m_table->isRowHidden(row)) ++count;
    return count;
}

void CMessageLogPanel::setTypeVisible(ELogMessageType type, bool visible)
{
    QToolButton* button = type == eLogFatal ? m_error : type == eLogWarning ? m_warning
        : type == eLogDebug ? m_debug : m_info;
    button->setChecked(visible);
}

void CMessageLogPanel::setSearchText(const QString& text) { m_search->setText(text); }

void CMessageLogPanel::copySelectedMessages()
{
    QStringList lines;
    QSet<int> rows;
    for(QTableWidgetItem* item : m_table->selectedItems()) rows.insert(item->row());
    QList<int> ordered = rows.values();
    std::sort(ordered.begin(), ordered.end());
    for(int row : ordered)
    {
        if(QTableWidgetItem* message = m_table->item(row, 2))
            lines << message->data(Qt::UserRole).toString();
    }
    if(!lines.isEmpty()) ei::copyTextToClipboard(lines.join('\n'), this);
}

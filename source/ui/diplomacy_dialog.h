#pragma once
#include <QDialog>
#include <QFutureWatcher>
#include <functional>
#include "diplomacy_analysis.h"
class QTreeWidget;
class QTableWidget;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTabWidget;

class CDiplomacyDialog final : public QDialog
{
    Q_OBJECT
public:
    using Snapshot = std::function<diplomacy::Request()>;
    explicit CDiplomacyDialog(Snapshot snapshot, QWidget* parent = nullptr);
    void invalidate();
    const diplomacy::Result& result() const { return m_result; }
    bool busy() const;
signals:
    void editRelationsRequested(const QString& source);
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    void refresh();
    void populateSources();
    void populateGroups();
    void populateDetails();
    void populateRelations();
    bool includesSource(const QString& key) const;
    const diplomacy::Source* relationSource() const;
    Snapshot m_snapshot;
    diplomacy::Request m_request;
    diplomacy::Result m_result;
    QFutureWatcher<diplomacy::Result>* m_worker;
    QTimer* m_timer;
    quint64 m_generation = 0, m_workerGeneration = 0;
    bool m_workerPending = false;
    QTreeWidget *m_groups, *m_npcs, *m_scripts;
    QTableWidget* m_relations;
    QComboBox* m_sources;
    QLineEdit* m_filter;
    QLabel *m_summary, *m_relationNotice;
    QPushButton* m_edit;
    QTabWidget* m_tabs;
};

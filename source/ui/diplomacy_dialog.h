#pragma once
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
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
class QTreeWidgetItem;
class QCheckBox;
class QPlainTextEdit;
class QSplitter;
class QMenu;

class CDiplomacyDialog final : public QDialog
{
    Q_OBJECT
public:
    using Snapshot = std::function<diplomacy::Request()>;
    explicit CDiplomacyDialog(Snapshot snapshot, QWidget* parent = nullptr);
    void invalidate();
    const diplomacy::Result& result() const { return m_result; }
    bool busy() const;
    void setMapMode(bool enabled);
    void setHistoryAvailable(bool undo, bool redo);
signals:
    void historyStepRequested(bool redo);
    void mapModeRequested(bool enabled);
    void npcFocusRequested(const QString& source, const QString& key);
    void relationChangeRequested(const QString& source, int row, int column,
                                 uint value, bool symmetric, uint expected, uint expectedReverse);
    void createRelationsRequested(const QString& source);
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    void refresh();
    void populateSources();
    void populateGroups();
    void populateDetails();
    void populateRelations();
    void populateRelationSources();
    void filterGroups();
    void showDetails();
    void showRelationDetails();
    void sizeRelationCells();
    void focusNpc();
    void applySelectedRelation(uint value);
    void openRelationMenu(int row, int column);
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
    QComboBox *m_sources, *m_relationSources;
    QLineEdit* m_filter;
    QLabel *m_summary, *m_subtitle, *m_relationNotice, *m_relationSelection, *m_detailsTitle, *m_emptyNotice, *m_help;
    QPushButton* m_edit;
    QPushButton* m_focus;
    QPushButton *m_undo, *m_redo;
    QVector<QPushButton*> m_relationButtons;
    QCheckBox *m_mapMode, *m_symmetric;
    QPointer<QMenu> m_relationMenu;
    QTabWidget* m_tabs;
    QCheckBox* m_hideEmpty;
    QPlainTextEdit* m_details;
    QWidget* m_detailPanel;
    QSplitter *m_split, *m_detailsSplit;
    int m_selectedGroup = -2;
    int m_relationRow = -1, m_relationColumn = -1;
    QString m_selectedRelationSource;
    QString m_selectedNpcKey, m_selectedScriptKey;
};

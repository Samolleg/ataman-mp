#ifndef DATABASE_EDITOR_DIALOG_H
#define DATABASE_EDITOR_DIALOG_H
#include <QDialog>
#include <QSharedPointer>
#include "database_document.h"
class QListWidget; class QLineEdit; class QTableView; class QLabel; class QPushButton; class QPoint; class QTimer; class QModelIndex;
namespace ei {
class DatabaseTableModel;
class DatabaseFilterModel;
class DatabaseFrozenTableView;
class DatabaseEditorDialog : public QDialog
{
    Q_OBJECT
public: explicit DatabaseEditorDialog(const QSharedPointer<DatabaseDocument>&, QWidget* = nullptr); ~DatabaseEditorDialog() override; void refreshFromHistory(); bool selectPrototype(const QString& prototype); bool selectFinding(const QString& tableKey,const QString& rowKey);
signals: void transactionCommitted(const QString& description, const QString& tableKey, const QString& rowKey);
    void itemSpellConstructorRequested(const QString& initialCode);
    void resourceBrowserRequested(const QString& reference);
    void undoRequested();
    void redoRequested();
private slots: void selectTable(); void rebuildGrid(); void editFieldDialog(const QModelIndex&); void showRowContextMenu(const QPoint&); void copySelectedRowId(); void cloneSelected(); void deleteSelected(); void review(); void apply(); void saveAs(); void exportXlsx(); void importXlsx(); void importDatabaseRes(); void importTablesLua(); void importDatabaseBundle();
private: QString currentTableKey() const; QString selectedRowKey() const; void updateStatus(); void publishLive(); bool applyDocument(QString* backupPath=nullptr,QString* diagnostic=nullptr);
    bool importNative(const QByteArray& databaseBytes,const QByteArray& luaBytes,const QString& description);
    void finishImport(const QString& warningTitle,const QString& description);
    QString runtimeRowIdentity(const QString& tableKey,const QString& rowKey) const;
    bool commitGridValue(const QString& rowKey,const QString& fieldKey,const QVariant& value);
    bool selectTableRow(const QString& tableKey,const QString& identity);
    void openCurrentReference();
    void showRowContextMenuForIndex(const QModelIndex& index, const QPoint& globalPosition);
    void showColumnContextMenu(int logicalColumn, const QPoint& globalPosition);
    void saveCurrentColumnState();
    void restoreCurrentColumnState();
    QString columnSettingsKey(const QString& suffix) const;
    QSharedPointer<DatabaseDocument> m_document; QListWidget* m_tables; QLineEdit* m_filter; DatabaseFrozenTableView* m_grid; DatabaseTableModel* m_model; DatabaseFilterModel* m_proxy; QLabel* m_status; QPushButton* m_cloneButton; QPushButton* m_deleteButton; QTimer* m_filterTimer; QTimer* m_columnStateTimer;
};
}
#endif

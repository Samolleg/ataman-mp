#ifndef DATABASE_EDITOR_DIALOG_H
#define DATABASE_EDITOR_DIALOG_H
#include <QDialog>
#include <QSharedPointer>
#include "database_document.h"
class QListWidget; class QLineEdit; class QTableWidget; class QLabel; class QPushButton; class QCheckBox;
namespace ei {
class DatabaseEditorDialog : public QDialog
{
    Q_OBJECT
public: explicit DatabaseEditorDialog(const QSharedPointer<DatabaseDocument>&, QWidget* = nullptr); void refreshFromHistory();
signals: void transactionCommitted(const QString& description);
private slots: void selectTable(); void rebuildGrid(); void editCell(int,int); void cloneSelected(); void deleteSelected(); void review(); void apply(); void saveAs(); void exportXlsx(); void importXlsx();
private: QString currentTableKey() const; QString selectedRowKey() const; void updateStatus(); void publishLive();
    QSharedPointer<DatabaseDocument> m_document; QListWidget* m_tables; QLineEdit* m_filter; QTableWidget* m_grid; QLabel* m_status; QCheckBox* m_showOpaque; bool m_refreshing{false};
};
}
#endif

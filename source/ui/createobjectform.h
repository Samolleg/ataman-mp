#ifndef CREATEOBJECTFORM_H
#define CREATEOBJECTFORM_H

#include <QDialog>
#include <QSharedPointer>

#include "layout_components/tablemanager.h"
#include "node.h"
#include "database_map_catalog.h"

namespace Ui {
class CCreateObjectForm;
}

class CView;
class CPreview;
class CMob;
class QComboBox;
class QLineEdit;
class QListWidget;
class QLabel;
class QCheckBox;

///
/// \brief The CCreateObjectForm class provides a form for creating a new object in the scene
///
class CCreateObjectForm : public QDialog
{
    Q_OBJECT

public:
    explicit CCreateObjectForm(QWidget *parent = nullptr);
    ~CCreateObjectForm();
    void attach(CView* pView, QUndoStack* pStack);
    CPreview* viewWidget();

    void closeEvent(QCloseEvent *event) override;

signals:
    void sendNewBbbox(CBox);

private slots:
    void onObjectChoose(QString& object);
    void onParamChange(const QSharedPointer<IPropertyBase>);
    void onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>& properties);

    void on_buttonCancel_clicked();
    void on_buttonCreate_clicked();

protected:
    void showEvent(QShowEvent* pEvent) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initViewWidget();
    void updateTable();
    void refreshDatabase();
    void filterDatabase();
    void selectDatabaseEntry();
    void cancelPlacement();
    void placeDatabaseObject(const QPoint& position);

private:
    Ui::CCreateObjectForm *ui;
    QMap<ENodeType, QString> m_objType;
    CNode* m_pNode;
    QSharedPointer<CTableManager> m_tableManager;
    CView* m_pView;
    CPreview * m_pPreview;
    QUndoStack* m_pUndoStack;
    QComboBox* m_databaseCategory{nullptr};
    QLineEdit* m_databaseSearch{nullptr};
    QListWidget* m_databaseList{nullptr};
    QLabel* m_databaseDetails{nullptr};
    QCheckBox* m_advanced{nullptr};
    QVector<ei::SDatabaseMapEntry> m_databaseEntries;
    quint64 m_databaseGeneration{0};
    bool m_databaseMode{true};
    bool m_placementPending{false};
    bool m_consumePlacementRelease{false};
    CMob* m_placementMob{nullptr};
};

#endif // CREATEOBJECTFORM_H

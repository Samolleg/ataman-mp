#ifndef CREATEOBJECTFORM_H
#define CREATEOBJECTFORM_H

#include <QDialog>
#include <QSharedPointer>

#include "layout_components/tablemanager.h"
#include "node.h"
#include "database_map_catalog.h"
#include "item_spell_constructor.h"
#include "technical_texture_catalog.h"
#include "technical_model_catalog.h"
#include <QTimer>

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
class QPushButton;

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
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initViewWidget();
    void adjustPropertyColumns();
    void saveInnerLayout();
    void updateTable();
    void refreshDatabase();
    void filterDatabase();
    void selectDatabaseEntry();
    void refreshTechnicalTextureChoices();
    void scanTechnicalTextureFile();
    void applyTechnicalTextureChoice();
    void openModelGallery();
    void saveTechnicalCatalog();

private:
    Ui::CCreateObjectForm *ui;
    bool m_layoutSeen{false}, m_updatingTable{false}, m_adjustingColumns{false};
    int m_parameterColumnWidth{0};
    QMap<ENodeType, QString> m_objType;
    CNode* m_pNode;
    QSharedPointer<CTableManager> m_tableManager;
    CView* m_pView;
    CPreview * m_pPreview;
    QUndoStack* m_pUndoStack;
    QComboBox* m_databaseCategory{nullptr};
    QComboBox* m_itemMaterial{nullptr};
    ei::ItemSpellConstructorCatalog m_itemCatalog;
    QLineEdit* m_databaseSearch{nullptr};
    QListWidget* m_databaseList{nullptr};
    QLabel* m_databaseDetails{nullptr};
    QCheckBox* m_advanced{nullptr};
    QVector<ei::SDatabaseMapEntry> m_databaseEntries;
    quint64 m_databaseGeneration{0};
    bool m_databaseMode{true};
    QComboBox* m_technicalTexture{nullptr};
    QLabel* m_technicalTextureLabel{nullptr};
    QLabel* m_technicalTextureStatus{nullptr};
    ei::TechnicalTextureCatalog m_technicalTextures;
    QTimer m_technicalTextureScan;
    bool m_autoPickExactTexture{false};
    bool m_rebuildingTextureChoices{false};
    QPushButton* m_modelGalleryButton{nullptr};
    ei::TechnicalModelCatalog m_modelCatalog;
    QString m_modelCatalogProfile;
    QString m_modelCatalogError;
};

#endif // CREATEOBJECTFORM_H

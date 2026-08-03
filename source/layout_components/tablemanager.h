#ifndef CTABLEMANAGER_H
#define CTABLEMANAGER_H
#include <QTableWidget>
#include <QSharedPointer>
#include <QTableWidgetItem>
#include <QComboBox>
#include <QList>
#include <QToolButton>
#include <QSharedPointer>
#include <QLineEdit>
#include <QStringListModel>
#include <QCompleter>
#include <QMap>

#include "types.h"
#include "undo.h"
#include "property.h"

class CLineEditEventFilter;
class CMultiLineEditForm;
class CUnitStatForm;
class CBodyPartEditForm;
class QDialog;
class QContextMenuEvent;
class CMagicTrap;
class QBoxLayout;
class QResizeEvent;

///
/// \brief The CValueItem class supports any type of property that can be displayed with a single line.
/// \details converting to(from) string must be overrided for each property.
///
class CValueItem : public QLineEdit
{
    Q_OBJECT
public:
    CValueItem() = delete;
    CValueItem(const QSharedPointer<IPropertyBase>& prop);

    const EObjParam& param(){return m_pValue->type();}
    bool applyChanges(const QString& text);
    const QSharedPointer<IPropertyBase>& value() {return m_pValue;}
    void skipNextCheck() {bSkip = true;} //skip checking value for reverting (on lost focus)
    void renewValue(QString value);
    void setCompactDisplay(bool compact);
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

protected:
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateDisplayedText();

signals:
    void onParamChange(const QSharedPointer<IPropertyBase>&);

public slots:
    void onTextChangeEnd();

private:
    QSharedPointer<IPropertyBase> m_pValue; //stored value;
    QSharedPointer<CLineEditEventFilter> m_filter;
    bool bSkip;
    bool m_compactDisplay{false};
};

///
/// \brief The CLineEditEventFilter class overrides the behavior for QLineEdit (loss of focus, keystrokes).
///
class CLineEditEventFilter : public QObject
{
public:
    explicit CLineEditEventFilter(CValueItem *parent, const QString oldValue):
        QObject(parent), m_value(oldValue)
    {}

    void updateValue(const QString value) {m_value = value;}
    bool eventFilter(QObject* obj, QEvent* e)
    {
        if(e->type() == QEvent::FocusOut)
            restoreValue();

        if (e->type() == QEvent::KeyPress)
        {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(e);
            if (keyEvent->nativeVirtualKey() == eKey_Esc)
                restoreValue();
        }
        // standard event processing
        return QObject::eventFilter(obj, e);
    }

private:
    void restoreValue();

private:
    QString m_value;
};

///
/// \brief The CComboStItem class extends the behavior of the drop-down list. Gets string values from CResourceStringList.
///
class CComboStItem: public QComboBox
{
    Q_OBJECT
public:
    CComboStItem() = delete;
    CComboStItem(const QSharedPointer<IPropertyBase>& prop);
    // update value on choosing new one
    const QSharedPointer<IPropertyBase>& value() {return m_pValue;}

    void wheelEvent(QWheelEvent *e) override
    {
        if(hasFocus())
            QComboBox::wheelEvent(e);
    }
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void _onChange(QString str); //override default 'on change event'

signals:
    void onParamChange(const QSharedPointer<IPropertyBase>&);

private:
    QSharedPointer<IPropertyBase> m_pValue; //stored value;
    QMap<uint, QString> m_valueList;
};


///
/// \brief The CComboStItem class extends the behavior of the drop-down list. Gets string values from CResourceStringList when it clicked.
///
class CComboDynItem: public QComboBox
{
    Q_OBJECT
public:
    CComboDynItem() = delete;
    CComboDynItem(const QSharedPointer<IPropertyBase>& prop);
    // update value on choosing new one
    const QSharedPointer<IPropertyBase>& value() {return m_pValue;}

    void wheelEvent(QWheelEvent *e) override
    {
        if(hasFocus())
            QComboBox::wheelEvent(e);
    }
    void showPopup() override;
    void hidePopup() override;
    bool popupActive() const { return m_popupActive; }
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

public slots:
    void _onChange(QString str); //override default 'on change event'
    void _onEditingFinished();
    void _onFilterTextChanged(const QString& text);

signals:
    void onParamChange(const QSharedPointer<IPropertyBase>&);

private:
    void ensureOptionsLoaded();
    void updateDatabaseDiagnostic(const QString& value);

    QSharedPointer<IPropertyBase> m_pValue; //stored value;
    QStringList m_valueList;
    QSharedPointer<QStringListModel> m_pListModel;
    QCompleter* m_pCompleter;
    bool m_optionsLoaded;
    bool m_popupActive;
    QString m_committedText;

    void cancelPendingEdit();
};

///
/// \brief The C3DItem class provides editing of a value consisting of 3 components (x,y,z).
///
class C3DItem: public QWidget
{
    Q_OBJECT
public:
    C3DItem() = delete;
    C3DItem(const QSharedPointer<IPropertyBase>& propX,const QSharedPointer<IPropertyBase>& propY, const QSharedPointer<IPropertyBase>& propZ);
    // update value on choosing new one
    //const QSharedPointer<IPropertyBase>& value() {return m_pValue;}

    void keyPressEvent(QKeyEvent *event) override;

protected:
    void resizeEvent(QResizeEvent* event) override;

signals:
    void onParamChange(const QSharedPointer<IPropertyBase>&);

public slots:
    void _onParamChange(const QSharedPointer<IPropertyBase>&); // slot for inner valueItem signal. re-translator

private:
    void updateResponsiveLayout(int availableWidth);
    void syncTableRowHeight();
    QSharedPointer<IPropertyBase> m_xValue; //stored value;
    QSharedPointer<IPropertyBase> m_yValue; //stored value;
    QSharedPointer<IPropertyBase> m_zValue; //stored value;
    QBoxLayout* m_componentLayout{nullptr};
    QVector<CValueItem*> m_componentEditors;
    int m_horizontalWidthHint{0};
    bool m_horizontalLayout{false};
};

///
/// \brief The CColorButtonItem class provides color editing for light sources
///
class CColorButtonItem : public QToolButton
{
    Q_OBJECT
public:
    CColorButtonItem() = delete;
    ~CColorButtonItem();
    CColorButtonItem(const QSharedPointer<IPropertyBase>& prop);
    void renewColor(const QVector3D& prop); // for usage in tile form
    void keyPressEvent(QKeyEvent* event) override;
private:
    void updateColor(const QColor& color);
    void setColor(QVector3D clr);

signals:
    void onColorChange(const QSharedPointer<IPropertyBase>);

public slots:
    void applyColor();

private:
    QSharedPointer<IPropertyBase> m_pValue;

};

///
/// \brief The CMultiLineButtonItem class provides editing of multiline values. For example, quest items, equipment.
///
class CMultiLineButtonItem: public QToolButton
{
    Q_OBJECT
public:
    CMultiLineButtonItem() = delete;
    CMultiLineButtonItem(const QSharedPointer<IPropertyBase>& prop);
    void keyPressEvent(QKeyEvent* event) override;

signals:
    void onTextChange(const QSharedPointer<IPropertyBase>);
    void onParamChange(const QSharedPointer<IPropertyBase>&);

public slots:
    void onTextEditOpen();
    void onTextEdit(QString str);

private:
    QSharedPointer<IPropertyBase> m_pValue;
    QSharedPointer<CMultiLineEditForm> m_pTextForm;

};

///
/// \brief The CUnitStatItem class provides editing of unit parameters as a table
///
class CUnitStatItem: public QToolButton
{
    Q_OBJECT
public:
    CUnitStatItem() = delete;
    CUnitStatItem(const QSharedPointer<IPropertyBase>& prop);
    void keyPressEvent(QKeyEvent* event) override;

signals:
    void onTextChange(const QSharedPointer<IPropertyBase>);
    void onParamChange(const QSharedPointer<IPropertyBase>&);

public slots:
    void onStatEditOpen();
    void _onParamChange(const QSharedPointer<IPropertyBase>& prop); // slot for inner valueItem signal. re-translator

private:
    QSharedPointer<IPropertyBase> m_pValue;
    QSharedPointer<CUnitStatForm> m_pTextForm;

};

///
/// \brief The CBodyPartItem class provides editing the visibility of body parts as a table
///
class CBodyPartItem: public QToolButton
{
    Q_OBJECT
public:
    CBodyPartItem() = delete;
    CBodyPartItem(const QSharedPointer<IPropertyBase>& prop);
    void keyPressEvent(QKeyEvent* event) override;

signals:
    void onTextChange(const QSharedPointer<IPropertyBase>);
    void onParamChange(const QSharedPointer<IPropertyBase>&);

public slots:
    void onPartEditOpen();
    void _onParamChange(const QSharedPointer<IPropertyBase>& prop); // slot for inner valueItem signal. re-translator

private:
    QSharedPointer<IPropertyBase> m_pValue;
    QSharedPointer<CBodyPartEditForm> m_pPartForm;

};

/// Read-only, parts-style launcher for the diplomacy-derived trap target table.
class CTrapTargetItem: public QToolButton
{
    Q_OBJECT
public:
    CTrapTargetItem() = delete;
    explicit CTrapTargetItem(const QSharedPointer<IPropertyBase>& prop);

public slots:
    void onTargetViewOpen();

private:
    QSharedPointer<IPropertyBase> m_pValue;
    QSharedPointer<QDialog> m_pTargetDialog;
};

///
/// \brief The CTableManager class managements property table (signals trasnfer, show data, trasfer applying changes)
///
class CTableManager : public QObject
{
    Q_OBJECT
public:
    CTableManager() = delete;
    CTableManager(QTableWidget* pTable);

    void reset();
    void setNewData(const QList<QSharedPointer<IPropertyBase>>& aProp,
                    bool magicTrapContext = false,
                    bool lookPointContext = false);
    void setBehaviorCollection(CUnit* unit, int logicSlot);
    void setTrapCollection(CMagicTrap* trap);
    void setLogicZeroContext(bool logicZero);
    void setReadOnly(bool readOnly);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initRowName();
    bool isValidValue(const EObjParam param, const QString& value);
    void registerPropertyRow(int row, const QSharedPointer<IPropertyBase>& value);
    void installClipboardFilter(QWidget* widget, int row);
    QList<int> selectedPropertyRows(int fallbackRow = -1) const;
    QString propertyDisplayText(int row) const;
    void copyRows(const QList<int>& rows);
    bool buildPasteValues(const QList<int>& rows,
                          QList<QSharedPointer<IPropertyBase>>* values,
                          QString* error) const;
    void pasteRows(const QList<int>& rows);
    void showPropertyContextMenu(const QList<int>& rows, const QPoint& globalPos);
    void restoreScrollPosition();
    void syncCollectionRowHeight();

signals:
    void onUpdateProperty(const QSharedPointer<IPropertyBase>); //retranslate value changing from ui to view class
    void onUpdateProperties(const QList<QSharedPointer<IPropertyBase>>& properties);
    void onBehaviorPointSelected(CNode* point);
    void onBehaviorPatrolMove(CUnit* unit, int logicSlot, int from, int to);
    void onBehaviorLookMove(CUnit* unit, int logicSlot, int patrol, int from, int to);
    void onBehaviorPatrolAdd(CUnit* unit);
    void onBehaviorLookAdd(CPatrolPoint* patrol);
    void onTrapActivationAreaMove(CMagicTrap* trap, int from, int to);
    void onTrapCastTargetMove(CMagicTrap* trap, int from, int to);
    void onTrapActivationAreaAdd(CMagicTrap* trap);
    void onTrapCastTargetAdd(CMagicTrap* trap);

public slots:
    void onParamChange(const QSharedPointer<IPropertyBase>& pProp); //get value for translating it to view class
    void onCellEdit(QTableWidgetItem*); // emulate signal-slot system  for QTableWidget

private:
    QTableWidget* m_pTable;
    QMap<EObjParam, QString> m_aRowName;
    QMap<EObjParam, QString> m_aRowToolTip;
    QMap<EObjParam, QMap<uint, QString>> m_comboMap;
    QMap<int, QSharedPointer<IPropertyBase>> m_rowValues;
    bool m_readOnly{false};
    bool m_torchPropertyContext{false};
    bool m_magicTrapPropertyContext{false};
    bool m_lookPointPropertyContext{false};
    bool m_logicZeroContext{false};
    int m_savedVerticalScroll{0};
    int m_savedHorizontalScroll{0};
    int m_collectionRow{-1};
};

#endif // CTABLEMANAGER_H

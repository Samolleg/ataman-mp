#include <QHeaderView>
#include <QToolButton>
#include <QColorDialog>
#include <QVBoxLayout>
#include <QBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGuiApplication>
#include <QClipboard>
#include <QSignalBlocker>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QApplication>
#include <QToolTip>
#include <QHelpEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSet>
#include <QScrollBar>
#include <QItemSelectionModel>
#include <QResizeEvent>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QtMath>
#include <cmath>
#include <functional>
#include <limits>

#include "tablemanager.h"
#include "resourcemanager.h"
#include "utils.h"
#include "multiline_edit_form.h"
#include "unitstat_form.h"
#include "bodypartedit_form.h"
#include "profile_database.h"
#include "behavior_collection_widget.h"
#include "trap_collection_widget.h"
#include "objects/magictrap.h"
#include "objects/unit.h"

namespace {
const char* kPropertyMimeType = "application/x-ei-mapper-property-type";
QList<QSharedPointer<IPropertyBase>> g_propertyClipboard;

QString staticChoiceToolTip(EObjParam type, uint value)
{
    if(type == eObjParam_LOGIC_BEHAVIOUR)
    {
        switch(value)
        {
        case 0: return QObject::tr("Remains in place and faces the configured direction.");
        case 1: return QObject::tr("Roams to random points within Guard radius around Guard position.");
        case 2: return QObject::tr("Follows the configured patrol points.");
        case 3: return QObject::tr("Remains in place and looks around.");
        case 4: return QObject::tr("Leaves control to the player or map scripts.");
        case 5: return QObject::tr("Roams to random points within Guard radius around the active alarm coordinates.");
        }
    }
    if(type == eObjParam_AGRESSION_MODE)
    {
        switch(value)
        {
        case 0: return QObject::tr("Attacks enemies on sight.");
        case 1: return QObject::tr("Attacks only in response to aggression.");
        case 2: return QObject::tr("Flees from enemies.");
        case 3: return QObject::tr("Flees from enemies and the player's party.");
        }
    }
    if(type == eObjParam_LOGIC_ALARM_CONDITION)
    {
        switch(value)
        {
        case 0: return QObject::tr("Raises the configured alarm when the unit sees an enemy.");
        case 1: return QObject::tr("Raises the configured alarm when the unit is attacked.");
        case 2: return QObject::tr("Raises the configured alarm when the unit becomes suspicious.");
        }
    }
    return QString();
}

bool numericValue(const IPropertyBase* property, double* value)
{
    if(!property || !property->isInit() || !value || dynamic_cast<const propBool*>(property))
        return false;
    if(const propFloat* number = dynamic_cast<const propFloat*>(property))
        *value = number->value();
    else if(const propUint* number = dynamic_cast<const propUint*>(property))
        *value = number->value();
    else if(const propInt* number = dynamic_cast<const propInt*>(property))
        *value = number->value();
    else if(const propChar* number = dynamic_cast<const propChar*>(property))
        *value = number->value();
    else
        return false;
    return std::isfinite(*value);
}

QSharedPointer<IPropertyBase> convertedNumericValue(
        const IPropertyBase* source, const IPropertyBase* destination, QString* error = nullptr)
{
    double value = 0.0;
    if(!numericValue(source, &value) || !destination || dynamic_cast<const propBool*>(destination))
        return QSharedPointer<IPropertyBase>();
    if(dynamic_cast<const propFloat*>(destination))
    {
        if(value < -std::numeric_limits<float>::max() || value > std::numeric_limits<float>::max())
        {
            if(error) *error = QObject::tr("The numeric value is outside this field's range.");
            return QSharedPointer<IPropertyBase>();
        }
        return QSharedPointer<IPropertyBase>(new propFloat(destination->type(), float(value)));
    }
    if(value < double(std::numeric_limits<qint64>::min())
       || value > double(std::numeric_limits<qint64>::max()))
    {
        if(error) *error = QObject::tr("The rounded value is outside this field's range.");
        return QSharedPointer<IPropertyBase>();
    }
    const qint64 rounded = qRound64(value);
    if(dynamic_cast<const propUint*>(destination))
    {
        if(rounded < 0 || quint64(rounded) > std::numeric_limits<uint>::max())
        {
            if(error) *error = QObject::tr("The rounded value is outside this unsigned field's range.");
            return QSharedPointer<IPropertyBase>();
        }
        return QSharedPointer<IPropertyBase>(new propUint(destination->type(), uint(rounded)));
    }
    if(dynamic_cast<const propInt*>(destination))
    {
        if(rounded < std::numeric_limits<int>::min() || rounded > std::numeric_limits<int>::max())
        {
            if(error) *error = QObject::tr("The rounded value is outside this integer field's range.");
            return QSharedPointer<IPropertyBase>();
        }
        return QSharedPointer<IPropertyBase>(new propInt(destination->type(), int(rounded)));
    }
    if(dynamic_cast<const propChar*>(destination))
    {
        if(rounded < std::numeric_limits<char>::min() || rounded > std::numeric_limits<char>::max())
        {
            if(error) *error = QObject::tr("The rounded value is outside this byte field's range.");
            return QSharedPointer<IPropertyBase>();
        }
        return QSharedPointer<IPropertyBase>(new propChar(destination->type(), char(rounded)));
    }
    return QSharedPointer<IPropertyBase>();
}

QSharedPointer<IPropertyBase> clonePropertyValue(const QSharedPointer<IPropertyBase>& property)
{
    if(property.isNull())
        return QSharedPointer<IPropertyBase>();
    if(!property->isInit())
        return property->createEmptyCopy();
    if(const propBodyPart* parts = dynamic_cast<const propBodyPart*>(property.get()))
        return QSharedPointer<IPropertyBase>(parts->clone());
    if(const propUnitStat* stats = dynamic_cast<const propUnitStat*>(property.get()))
    {
        QVector<QSharedPointer<IPropertyBase>> copied;
        for(const QSharedPointer<IPropertyBase>& value : stats->value())
            copied.append(QSharedPointer<IPropertyBase>(value->clone()));
        return QSharedPointer<IPropertyBase>(new propUnitStat(property->type(), copied));
    }
    return QSharedPointer<IPropertyBase>(property->clone());
}

QSharedPointer<IPropertyBase> clonePropertyForPaste(
    const QSharedPointer<IPropertyBase>& property)
{
    QSharedPointer<IPropertyBase> copy = clonePropertyValue(property);
    if(propBodyPart* parts = dynamic_cast<propBodyPart*>(copy.get()))
        parts->replaceSerializedValue(parts->effectiveSerializedValue());
    return copy;
}

void copyProperty(const QSharedPointer<IPropertyBase>& property, const QString& displayText)
{
    if(property.isNull())
        return;
    g_propertyClipboard.clear();
    g_propertyClipboard.append(clonePropertyValue(property));
    QMimeData* mime = new QMimeData;
    mime->setText(displayText);
    mime->setData(kPropertyMimeType, QByteArray::number(int(property->type())));
    QGuiApplication::clipboard()->setMimeData(mime);
}

class CVectorComponentPlate final : public QToolButton
{
public:
    using ApplyValue = std::function<void(const QSharedPointer<IPropertyBase>&)>;

    CVectorComponentPlate(const QString& label,
                          const QSharedPointer<IPropertyBase>& property,
                          const ApplyValue& applyValue,
                          QWidget* parent)
        : QToolButton(parent), m_property(clonePropertyValue(property)),
          m_applyValue(applyValue)
    {
        setText(label);
        setAutoRaise(true);
        setCheckable(true);
        // Exclusivity is managed across the complete property table, not just
        // the three buttons belonging to this one Vector3 editor.
        setAutoExclusive(false);
        setFocusPolicy(Qt::StrongFocus);
        // Qt may propagate a ContextMenu event from a child through C3DItem,
        // whose table-level filter opens the aggregate property menu. Open the
        // component menu explicitly from the consumed right-button release.
        setContextMenuPolicy(Qt::PreventContextMenu);
        setMinimumSize(32, 22);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setProperty("enduranceComponentType", int(property->type()));
        setStyleSheet("QToolButton:checked { background-color: #315f46; "
                      "border: 1px solid #55b878; }");
        setToolTip(QObject::tr(
            "Select this component only. Ctrl+C/Ctrl+V and the context menu operate on this value."));
        QObject::connect(this, &QToolButton::clicked, this,
                         [this]() { selectComponent(); });
    }

    void clearComponentSelection()
    {
        setChecked(false);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::RightButton)
        {
            prepareContextSelection();
            m_rightPressArmed = true;
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if(event->button() == Qt::RightButton)
        {
            const bool openMenu = m_rightPressArmed && rect().contains(event->pos());
            m_rightPressArmed = false;
            event->accept();
            if(openMenu)
                showComponentMenu(event->globalPos());
            else
                restoreContextSelection();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if(event->matches(QKeySequence::Copy) && !m_property.isNull())
        {
            copyProperty(m_property, m_property->toString());
            event->accept();
            return;
        }
        if(event->matches(QKeySequence::Paste))
        {
            pasteValue();
            event->accept();
            return;
        }
        if(event->key() == Qt::Key_Menu
           || (event->key() == Qt::Key_F10
               && event->modifiers().testFlag(Qt::ShiftModifier)))
        {
            prepareContextSelection();
            showComponentMenu(mapToGlobal(QPoint(0, height())));
            event->accept();
            return;
        }
        QToolButton::keyPressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        // PreventContextMenu normally suppresses this. Accept defensively so
        // an OS-generated event can never bubble into the aggregate C3DItem.
        event->accept();
    }

private:
    void showComponentMenu(const QPoint& globalPos)
    {
        QMenu menu(this);
        QAction* copy = menu.addAction(QObject::tr("Copy value"));
        QAction* paste = menu.addAction(QObject::tr("Paste value"));
        copy->setShortcut(QKeySequence::Copy);
        paste->setShortcut(QKeySequence::Paste);
        paste->setEnabled(canPaste(nullptr));
        QObject::connect(copy, &QAction::triggered, this, [this]()
        {
            if(!m_property.isNull())
                copyProperty(m_property, m_property->toString());
        });
        QObject::connect(paste, &QAction::triggered, this,
                         [this]() { pasteValue(); });
        menu.exec(globalPos);
        restoreContextSelection();
    }
    QTableWidget* parentTable() const
    {
        QWidget* ancestor = parentWidget();
        while(ancestor && !qobject_cast<QTableWidget*>(ancestor))
            ancestor = ancestor->parentWidget();
        return qobject_cast<QTableWidget*>(ancestor);
    }

    QList<CVectorComponentPlate*> tablePlates() const
    {
        QTableWidget* table = parentTable();
        return table ? table->findChildren<CVectorComponentPlate*>()
                     : QList<CVectorComponentPlate*>();
    }

    void selectOnly(CVectorComponentPlate* selected)
    {
        for(CVectorComponentPlate* plate : tablePlates())
            plate->setChecked(plate == selected);
    }

    void clearParentTableSelection()
    {
        if(QTableWidget* table = parentTable())
        {
            const QSignalBlocker blocker(table);
            // clearSelection() does not invalidate QAbstractItemView's current
            // index. That current Vector3 cell can still paint as active when
            // focus moves between two component context menus.
            if(table->selectionModel())
                table->selectionModel()->clear();
            table->setCurrentCell(-1, -1, QItemSelectionModel::Clear);
        }
    }

    void prepareContextSelection()
    {
        if(!m_contextSelectionPrepared)
        {
            m_previousCheckedPlate = nullptr;
            for(CVectorComponentPlate* plate : tablePlates())
                if(plate->isChecked())
                {
                    m_previousCheckedPlate = plate;
                    break;
                }
            m_contextSelectionPrepared = true;
        }
        selectOnly(this);
        // Match the working left-click path: the component itself must own
        // focus before QMenu starts its nested event loop. Otherwise focus can
        // remain on C3DItem and paint the aggregate Vector3 panel behind it.
        setFocus(Qt::MouseFocusReason);
        clearParentTableSelection();
    }

    void restoreContextSelection()
    {
        if(!m_contextSelectionPrepared)
            return;
        const QList<CVectorComponentPlate*> plates = tablePlates();
        for(CVectorComponentPlate* plate : plates)
            plate->setChecked(plate == m_previousCheckedPlate);
        m_previousCheckedPlate = nullptr;
        m_contextSelectionPrepared = false;
        clearParentTableSelection();
    }

    void selectComponent()
    {
        selectOnly(this);
        setFocus(Qt::MouseFocusReason);
        clearParentTableSelection();
    }

    bool canPaste(QSharedPointer<IPropertyBase>* value) const
    {
        if(m_property.isNull() || property("enduranceReadOnly").toBool()
           || (parentWidget() && parentWidget()->property("enduranceReadOnly").toBool()))
            return false;
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        if(!mime)
            return false;
        QSharedPointer<IPropertyBase> converted;
        if(mime->hasFormat(kPropertyMimeType) && g_propertyClipboard.size() == 1)
        {
            const QSharedPointer<IPropertyBase> source = g_propertyClipboard.first();
            if(!source.isNull() && source->type() == m_property->type())
                converted = clonePropertyValue(source);
            else
                converted = convertedNumericValue(source.get(), m_property.get());
        }
        else if(mime->hasText())
        {
            bool ok = false;
            const double number = mime->text().trimmed().toDouble(&ok);
            if(ok && std::isfinite(number)
               && number >= -std::numeric_limits<float>::max()
               && number <= std::numeric_limits<float>::max())
            {
                const propFloat source(m_property->type(), float(number));
                converted = convertedNumericValue(&source, m_property.get());
            }
        }
        if(converted.isNull())
            return false;
        if(value)
            *value = converted;
        return true;
    }

    void pasteValue()
    {
        QSharedPointer<IPropertyBase> value;
        if(!canPaste(&value))
        {
            QApplication::beep();
            QToolTip::showText(mapToGlobal(QPoint(0, height())),
                QObject::tr("Clipboard value is not compatible with this component."), this);
            return;
        }
        m_property = clonePropertyValue(value);
        if(m_applyValue)
            m_applyValue(m_property);
    }

    QSharedPointer<IPropertyBase> m_property;
    ApplyValue m_applyValue;
    CVectorComponentPlate* m_previousCheckedPlate{nullptr};
    bool m_contextSelectionPrepared{false};
    bool m_rightPressArmed{false};
};

bool clipboardMatches(EObjParam destination)
{
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if(!mime || !mime->hasFormat(kPropertyMimeType))
        return true;
    bool ok = false;
    const int source = mime->data(kPropertyMimeType).toInt(&ok);
    return ok && source == int(destination);
}

bool compatibleText(EObjParam param, const QString& value)
{
    switch(param)
    {
    case eObjParam_NID:
    case eObjParam_RANGE:
    case eObjParam_SOUND_INNER_RADIUS:
    case eObjParam_SOUND_RANGE:
    case eObjParam_SOUND_MIN:
    case eObjParam_SOUND_MAX:
    case eObjParam_LEVER_SCIENCE_STATS_Key_ID:
    case eObjParam_LEVER_SCIENCE_STATS_Hands_Sleight:
    case eObjParam_LEVER_TOTAL_STATE:
    case eObjParam_LEVER_CUR_STATE:
    case eObjParam_VIEW_WAIT:
    case eObjParam_VIEW_TURN_SPEED:
    case eObjParam_TRAP_CAST_INTERVAL:
        return QRegExp("[0-9]+").exactMatch(value);
    case eObjParam_PARENT_ID:
    case eObjParam_TRAP_DIPLOMACY:
        return QRegExp("-?[0-9]+").exactMatch(value);
    case eObjParam_GUARD_RADIUS:
    case eObjParam_GUARD_ALARM:
    case eObjParam_LIGHT_RADIUS:
    case eObjParam_LOGIC_WAIT:
    case eObjParam_TORCH_STRENGHT:
    case eObjParam_PARTICL_SCALE:
    case eObjParam_TRAP_AREA_RADIUS:
        return QRegExp("\\s*-?\\d+(\\.\\d+)?").exactMatch(value);
    case eObjParam_UNIT_PROTOTYPE:
    case eObjParam_TEMPLATE:
    case eObjParam_PRIM_TXTR:
        return !value.trimmed().isEmpty();
    default:
        return true;
    }
}

bool rejectIncompatiblePaste(QWidget* destination, EObjParam param, const QString& text)
{
    if(clipboardMatches(param) && compatibleText(param, text))
        return false;
    QApplication::beep();
    QToolTip::showText(destination->mapToGlobal(QPoint(0, destination->height())),
                       QObject::tr("Clipboard value is not compatible with this field."), destination);
    return true;
}
}

CTableManager::CTableManager(QTableWidget* pTable):
    m_pTable(pTable)
{
    initRowName();
    m_pTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_pTable->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pTable->installEventFilter(this);
    m_pTable->viewport()->installEventFilter(this);
    connect(m_pTable, &QTableWidget::itemSelectionChanged, this, [this]()
    {
        if(m_pTable->selectedItems().isEmpty())
            return;
        for(CVectorComponentPlate* plate :
            m_pTable->findChildren<CVectorComponentPlate*>())
        {
            plate->clearComponentSelection();
        }
    });
    reset();
    //QObject::connect(m_pTable, SIGNAL(itemChanged(QTableWidgetItem*)), this, SLOT(onCellEdit(QTableWidgetItem*)));
}

void CTableManager::reset()
{
    m_collectionRow = -1;
    m_rowValues.clear();
    m_pTable->clear();
    m_pTable->setColumnCount(2);
    m_pTable->setRowCount(0);
    QStringList header;
    header.append(tr("Parameter"));
    header.append(tr("Value"));
    m_pTable->setHorizontalHeaderLabels(header);
    m_pTable->horizontalHeader()->setVisible(true);
    m_pTable->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_pTable->verticalHeader()->hide();
    //m_pTable->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents); //adjust whole table. now both columns fill qtablewidget but it occurs blink and offset for main window
}

void CTableManager::restoreScrollPosition()
{
    m_pTable->verticalScrollBar()->setValue(
        qMin(m_savedVerticalScroll, m_pTable->verticalScrollBar()->maximum()));
    m_pTable->horizontalScrollBar()->setValue(
        qMin(m_savedHorizontalScroll, m_pTable->horizontalScrollBar()->maximum()));
}

void CTableManager::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for(int row = 0; row < m_pTable->rowCount(); ++row)
    {
        QWidget* widget = m_pTable->cellWidget(row, 1);
        if(!widget)
            widget = m_pTable->cellWidget(row, 0);
        if(!widget)
            continue;
        const bool compoundEditor = qobject_cast<C3DItem*>(widget) != nullptr;
        const bool logicZeroEnabledField = m_logicZeroContext
            && m_rowValues.contains(row)
            && m_rowValues.value(row)->type() == eObjParam_LOGIC_USE;
        widget->setProperty("enduranceReadOnly", readOnly);
        if(QLineEdit* line = qobject_cast<QLineEdit*>(widget))
            line->setReadOnly(readOnly);
        const QList<QLineEdit*> lines = widget->findChildren<QLineEdit*>();
        for(QLineEdit* line : lines)
            line->setReadOnly(readOnly);
        // Editable combo boxes contain a QLineEdit, but leaving the combo
        // enabled would still allow a locked MOB property to change through
        // its popup.  Compound numeric editors remain enabled so their
        // read-only components can still be focused and copied.
        if(qobject_cast<QComboBox*>(widget))
            widget->setEnabled(!readOnly && !logicZeroEnabledField);
        else if(!qobject_cast<QLineEdit*>(widget) && !compoundEditor)
            widget->setEnabled(!readOnly);
        widget->setToolTip(readOnly
            ? tr("This object belongs to a locked MOB. Activate its owner to edit it.")
            : logicZeroEnabledField
                ? tr("Logic 0 is the default setup and is always enabled. Its stored flag is preserved but not editable.")
                : QString());
    }
}

void CTableManager::setBehaviorCollection(CUnit* unit, int logicSlot)
{
    if(!unit)
        return;
    const int row = m_pTable->rowCount();
    m_pTable->insertRow(row);
    m_pTable->setSpan(row, 0, 1, 2);
    CBehaviorCollectionWidget* editor = new CBehaviorCollectionWidget(
        unit, logicSlot, m_pTable);
    editor->setEnabled(!m_readOnly);
    editor->setProperty("enduranceReadOnly", m_readOnly);
    connect(editor, &CBehaviorCollectionWidget::selectPoint,
            this, &CTableManager::onBehaviorPointSelected);
    connect(editor, &CBehaviorCollectionWidget::movePatrolPoint, this,
            [this, unit, logicSlot](int from, int to)
            { emit onBehaviorPatrolMove(unit, logicSlot, from, to); });
    connect(editor, &CBehaviorCollectionWidget::moveLookPoint, this,
            [this, unit, logicSlot](int patrol, int from, int to)
            { emit onBehaviorLookMove(unit, logicSlot, patrol, from, to); });
    connect(editor, &CBehaviorCollectionWidget::addPatrolPoint, this,
            [this, unit]() { emit onBehaviorPatrolAdd(unit); });
    connect(editor, &CBehaviorCollectionWidget::addLookPoint, this,
            [this, unit, logicSlot](int patrol)
            {
                CPatrolPoint* point = unit->patrolByIndex(logicSlot, patrol);
                if(point) emit onBehaviorLookAdd(point);
            });
    m_pTable->setCellWidget(row, 0, editor);
    editor->setMinimumHeight(190);
    editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_collectionRow = row;
    m_pTable->verticalHeader()->setSectionResizeMode(row, QHeaderView::Interactive);
    syncCollectionRowHeight();
    restoreScrollPosition();
}

void CTableManager::setTrapCollection(CMagicTrap* trap)
{
    if(!trap)
        return;
    const int row = m_pTable->rowCount();
    m_pTable->insertRow(row);
    m_pTable->setSpan(row, 0, 1, 2);
    CTrapCollectionWidget* editor = new CTrapCollectionWidget(trap, m_pTable);
    editor->setEnabled(!m_readOnly);
    editor->setProperty("enduranceReadOnly", m_readOnly);
    connect(editor, &CTrapCollectionWidget::selectPoint,
            this, &CTableManager::onBehaviorPointSelected);
    connect(editor, &CTrapCollectionWidget::moveActivationArea, this,
            [this, trap](int from, int to)
            { emit onTrapActivationAreaMove(trap, from, to); });
    connect(editor, &CTrapCollectionWidget::moveCastTarget, this,
            [this, trap](int from, int to)
            { emit onTrapCastTargetMove(trap, from, to); });
    connect(editor, &CTrapCollectionWidget::addActivationArea, this,
            [this, trap]() { emit onTrapActivationAreaAdd(trap); });
    connect(editor, &CTrapCollectionWidget::addCastTarget, this,
            [this, trap]() { emit onTrapCastTargetAdd(trap); });
    m_pTable->setCellWidget(row, 0, editor);
    editor->setMinimumHeight(190);
    editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_collectionRow = row;
    m_pTable->verticalHeader()->setSectionResizeMode(row, QHeaderView::Interactive);
    syncCollectionRowHeight();
    restoreScrollPosition();
}

void CTableManager::syncCollectionRowHeight()
{
    if(m_collectionRow < 0 || m_collectionRow >= m_pTable->rowCount())
        return;
    // Give the collection one viewport of content, but keep it as a concrete
    // row so the outer per-pixel scrollbar can reach every part of it.
    const int desired = qMax(190, m_pTable->viewport()->height());
    if(m_pTable->rowHeight(m_collectionRow) != desired)
        m_pTable->setRowHeight(m_collectionRow, desired);
}

void CTableManager::setLogicZeroContext(bool logicZero)
{
    m_logicZeroContext = logicZero;
    setReadOnly(m_readOnly);
}

void CTableManager::installClipboardFilter(QWidget* widget, int row)
{
    if(!widget)
        return;
    widget->setProperty("propertyRow", row);
    // A focused text editor must retain native selection-aware clipboard
    // behaviour. Property-level copy/paste remains available from the label,
    // table selection, and non-text value widgets.
    if(!qobject_cast<QLineEdit*>(widget))
        widget->installEventFilter(this);
}

void CTableManager::registerPropertyRow(int row, const QSharedPointer<IPropertyBase>& value)
{
    if(row < 0 || value.isNull())
        return;
    m_rowValues.insert(row, clonePropertyValue(value));
    QString tooltip = m_aRowToolTip.value(value->type());
    if(value->type() == eObjParam_IS_SHADOW && m_torchPropertyContext)
        tooltip = tr("Controls only whether the torch/campfire model casts its own silhouette. "
                     "It does not disable shadows produced by the embedded point light; "
                     "TORCH records have no separate shadow flag for that light.");
    else if(value->type() == eObjParam_POSITION && m_lookPointPropertyContext)
        tooltip = tr("Look-at position stored in the MOB. X and Y determine the facing direction; "
                     "Z is stored but is not used when the look angle is calculated.");
    else if(m_magicTrapPropertyContext)
    {
        switch(value->type())
        {
        case eObjParam_NAME:
        case eObjParam_PARENT_TEMPLATE:
        case eObjParam_SEC_TXTR:
        case eObjParam_ROTATION:
        case eObjParam_USE_IN_SCRIPT:
        case eObjParam_IS_SHADOW:
        case eObjParam_PARENT_ID:
        case eObjParam_QUEST_INFO:
        case eObjParam_COMPLECTION:
            tooltip = tr("Preserved in the MOB, but no confirmed native magic-trap behavior or visualization uses this value.");
            break;
        default:
            break;
        }
    }
    if(QTableWidgetItem* label = m_pTable->item(row, 0))
    {
        label->setData(Qt::UserRole, int(value->type()));
        label->setToolTip(tooltip);
    }
    QWidget* editor = m_pTable->cellWidget(row, 1);
    if(editor && !tooltip.isEmpty())
    {
        editor->setToolTip(tooltip);
        // A combo popup resolves per-item help through the model's ToolTipRole.
        // Giving its view/viewport the property tooltip masks that item help.
        if(!qobject_cast<QComboBox*>(editor))
        {
            const QList<QWidget*> children = editor->findChildren<QWidget*>();
            for(QWidget* child : children)
                if(child->toolTip().isEmpty())
                    child->setToolTip(tooltip);
        }
    }
    installClipboardFilter(editor, row);
}

QList<int> CTableManager::selectedPropertyRows(int fallbackRow) const
{
    QSet<int> uniqueRows;
    for(QTableWidgetItem* item : m_pTable->selectedItems())
    {
        if(item && m_rowValues.contains(item->row()))
            uniqueRows.insert(item->row());
    }
    if(uniqueRows.isEmpty() && fallbackRow >= 0 && m_rowValues.contains(fallbackRow))
        uniqueRows.insert(fallbackRow);
    QList<int> rows = uniqueRows.values();
    std::sort(rows.begin(), rows.end());
    return rows;
}

QString CTableManager::propertyDisplayText(int row) const
{
    const QSharedPointer<IPropertyBase> value = m_rowValues.value(row);
    if(value.isNull() || !value->isInit())
        return QString();
    if(QComboBox* combo = qobject_cast<QComboBox*>(m_pTable->cellWidget(row, 1)))
        return combo->currentText();
    if(QLineEdit* line = qobject_cast<QLineEdit*>(m_pTable->cellWidget(row, 1)))
        return line->text();
    if(propStrAr* strings = dynamic_cast<propStrAr*>(value.get()))
        return strings->value().join('\n');
    if(propBodyPart* parts = dynamic_cast<propBodyPart*>(value.get()))
    {
        QStringList lines;
        for(auto it = parts->value().constBegin(); it != parts->value().constEnd(); ++it)
            lines.append(QString("  %1=%2").arg(it.key(), it.value()->toString()));
        return QString("{\n%1\n}").arg(lines.join('\n'));
    }
    if(propUnitStat* stats = dynamic_cast<propUnitStat*>(value.get()))
    {
        QStringList lines;
        const auto& values = stats->value();
        for(int i = 0; i < values.size(); ++i)
            lines.append(QString("%1=%2").arg(i).arg(values.at(i)->toString()));
        return lines.join('\n');
    }
    return value->toString();
}

void CTableManager::copyRows(const QList<int>& rows)
{
    if(rows.isEmpty())
        return;
    g_propertyClipboard.clear();
    QStringList types;
    QStringList plainText;
    for(int row : rows)
    {
        const QSharedPointer<IPropertyBase> value = m_rowValues.value(row);
        if(value.isNull())
            return;
        g_propertyClipboard.append(clonePropertyValue(value));
        types.append(QString::number(int(value->type())));
        const QString display = propertyDisplayText(row);
        if(rows.size() == 1)
            plainText.append(display);
        else
        {
            const QString label = m_pTable->item(row, 0) ? m_pTable->item(row, 0)->text() : QString::number(row);
            plainText.append(QString("%1: %2").arg(label, display));
        }
    }
    QMimeData* mime = new QMimeData;
    mime->setText(plainText.join('\n'));
    mime->setData(kPropertyMimeType, types.join(',').toLatin1());
    QGuiApplication::clipboard()->setMimeData(mime);
}

bool CTableManager::buildPasteValues(const QList<int>& rows,
                                     QList<QSharedPointer<IPropertyBase>>* values,
                                     QString* error) const
{
    if(values)
        values->clear();
    if(rows.isEmpty())
    {
        if(error) *error = tr("Select one or more property labels first.");
        return false;
    }
    if(m_readOnly)
    {
        if(error) *error = tr("Activate the selected object's MOB before editing its properties.");
        return false;
    }
    for(int row : rows)
    {
        const QSharedPointer<IPropertyBase> destination = m_rowValues.value(row);
        if(!destination.isNull()
           && (destination->type() == eObjParam_OBJECT_TYPE
               || destination->type() == eObjParam_TRAP_EFFECTIVE_TARGETS))
        {
            if(error) *error = destination->type() == eObjParam_OBJECT_TYPE
                ? tr("Object type is read-only. Use an explicit conversion command to change it.")
                : tr("Effective trap targets are read-only and derived from Player group and MOB diplomacy.");
            return false;
        }
    }
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if(mime && mime->hasFormat(kPropertyMimeType))
    {
        const QList<QByteArray> encodedTypes = mime->data(kPropertyMimeType).split(',');
        if(encodedTypes.size() != rows.size() || g_propertyClipboard.size() != rows.size())
        {
            if(error) *error = tr("The number of copied properties does not match the selected fields.");
            return false;
        }
        for(int i = 0; i < rows.size(); ++i)
        {
            bool ok = false;
            const int encodedType = encodedTypes.at(i).toInt(&ok);
            const QSharedPointer<IPropertyBase> destination = m_rowValues.value(rows.at(i));
            const QSharedPointer<IPropertyBase> source = g_propertyClipboard.at(i);
            if(!ok || destination.isNull() || source.isNull())
            {
                if(error) *error = tr("Copied property types do not match every selected field.");
                return false;
            }
            if(encodedType != int(destination->type()) || source->type() != destination->type())
            {
                QString conversionError;
                const QSharedPointer<IPropertyBase> converted = convertedNumericValue(
                    source.get(), destination.get(), &conversionError);
                if(converted.isNull())
                {
                    if(error) *error = conversionError.isEmpty()
                        ? tr("Copied property types do not match every selected field.")
                        : conversionError;
                    return false;
                }
                if(values) values->append(converted);
                continue;
            }
            if(values)
                values->append(clonePropertyForPaste(source));
        }
        return true;
    }

    if(rows.size() != 1 || !mime || !mime->hasText())
    {
        if(error) *error = tr("Plain text can be pasted into one compatible field at a time.");
        return false;
    }
    const int row = rows.first();
    const QSharedPointer<IPropertyBase> destination = m_rowValues.value(row);
    const QString text = mime->text();
    if(destination.isNull() || dynamic_cast<propBodyPart*>(destination.get())
       || dynamic_cast<propUnitStat*>(destination.get()))
    {
        if(error) *error = tr("This structured field requires a value copied from the same Endurance property type.");
        return false;
    }
    QSharedPointer<IPropertyBase> parsed(destination->clone());
    bool numericText = false;
    const double plainNumber = text.trimmed().toDouble(&numericText);
    if(numericText)
    {
        propFloat sourceNumber(destination->type(), float(plainNumber));
        QString conversionError;
        const QSharedPointer<IPropertyBase> converted = convertedNumericValue(
            &sourceNumber, destination.get(), &conversionError);
        if(!converted.isNull())
        {
            if(values) values->append(converted);
            return true;
        }
        if(!conversionError.isEmpty())
        {
            if(error) *error = conversionError;
            return false;
        }
    }
    if(CComboStItem* combo = qobject_cast<CComboStItem*>(m_pTable->cellWidget(row, 1)))
    {
        QMap<uint, QString> choices;
        if(!CResourceStringList::getInstance()->getPropList(choices, destination->type())
           || !choices.values().contains(text))
        {
            if(error) *error = tr("The clipboard text is not a valid choice for this field.");
            return false;
        }
        parsed->resetFromString(QString::number(choices.key(text)));
        Q_UNUSED(combo);
    }
    else if(propStrAr* strings = dynamic_cast<propStrAr*>(parsed.get()))
    {
        const QStringList lines = text.split(QRegularExpression("\\r?\\n"), QString::SkipEmptyParts);
        parsed.reset(new propStrAr(destination->type(), lines));
        Q_UNUSED(strings);
    }
    else
    {
        if(!compatibleText(destination->type(), text))
        {
            if(error) *error = tr("Clipboard text is not valid for this field.");
            return false;
        }
        parsed->resetFromString(text);
    }
    if(values)
        values->append(parsed);
    return true;
}

void CTableManager::pasteRows(const QList<int>& rows)
{
    QList<QSharedPointer<IPropertyBase>> values;
    QString error;
    if(!buildPasteValues(rows, &values, &error))
    {
        QApplication::beep();
        QToolTip::showText(QCursor::pos(), error, m_pTable);
        return;
    }
    if(values.size() == 1)
        emit onUpdateProperty(values.first());
    else
        emit onUpdateProperties(values);
}

void CTableManager::showPropertyContextMenu(const QList<int>& rows, const QPoint& globalPos)
{
    if(rows.isEmpty())
        return;
    QMenu menu(m_pTable);
    QAction* copy = menu.addAction(tr("Copy value"));
    QAction* paste = menu.addAction(tr("Paste value"));
    copy->setShortcut(QKeySequence::Copy);
    paste->setShortcut(QKeySequence::Paste);
    QString ignored;
    paste->setEnabled(buildPasteValues(rows, nullptr, &ignored));
    QAction* selected = menu.exec(globalPos);
    if(selected == copy)
        copyRows(rows);
    else if(selected == paste)
        pasteRows(rows);
}

bool CTableManager::eventFilter(QObject* watched, QEvent* event)
{
    if(event->type() == QEvent::Resize
       && (watched == m_pTable || watched == m_pTable->viewport()))
        syncCollectionRowHeight();

    const QVariant rowProperty = watched ? watched->property("propertyRow") : QVariant();
    const int widgetRow = rowProperty.isValid() ? rowProperty.toInt() : -1;
    if(event->type() == QEvent::ContextMenu)
    {
        QContextMenuEvent* context = static_cast<QContextMenuEvent*>(event);
        QList<int> rows;
        if(widgetRow >= 0)
            rows = selectedPropertyRows(widgetRow);
        else if(watched == m_pTable->viewport())
        {
            const int clickedRow = m_pTable->indexAt(context->pos()).row();
            rows = selectedPropertyRows(clickedRow);
            if(!rows.contains(clickedRow) && clickedRow >= 0)
                rows = QList<int>() << clickedRow;
        }
        else
            rows = selectedPropertyRows();
        showPropertyContextMenu(rows, context->globalPos());
        return !rows.isEmpty();
    }
    if(event->type() == QEvent::KeyPress)
    {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        const QList<int> rows = widgetRow >= 0 ? QList<int>() << widgetRow : selectedPropertyRows();
        if(key->matches(QKeySequence::Copy) && !rows.isEmpty())
        {
            copyRows(rows);
            return true;
        }
        if(key->matches(QKeySequence::Paste) && !rows.isEmpty())
        {
            pasteRows(rows);
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}


void CTableManager::initRowName()
{
    m_aRowName[eObjParam_BODYPARTS] = tr("Model parts");
    m_aRowName[eObjParam_PLAYER] = tr("Player group");
    m_aRowName[eObjParam_NID] = tr("Map ID");
    m_aRowName[eObjParam_TYPE] = tr("Unit category");
    m_aRowName[eObjParam_OBJECT_TYPE] = tr("Object type");
    m_aRowName[eObjParam_NAME] = tr("Name");
    m_aRowName[eObjParam_TEMPLATE] = tr("Model");
    m_aRowName[eObjParam_PARENT_TEMPLATE] = tr("Parent template");
    m_aRowName[eObjParam_PRIM_TXTR] = tr("Primary texture");
    m_aRowName[eObjParam_SEC_TXTR] = tr("Secondary texture");
    m_aRowName[eObjParam_COMMENTS] = tr("Comments");
    m_aRowName[eObjParam_POSITION] = tr("Position");
    m_aRowName[eObjParam_ROTATION] = tr("Rotation");
    m_aRowName[eObjParam_USE_IN_SCRIPT] = tr("Available to scripts");
    m_aRowName[eObjParam_IS_SHADOW] = tr("Cast shadow");
    m_aRowName[eObjParam_PARENT_ID] = tr("Parent ID");
    m_aRowName[eObjParam_QUEST_INFO] = tr("Attached quest variable");
    m_aRowName[eObjParam_COMPLECTION] = tr("Scale");
    m_aRowName[eObjParam_TORCH_PTLINK] = tr("Light and effect offset");
    m_aRowName[eObjParam_TORCH_STRENGHT] = tr("Light power");
    m_aRowName[eObjParam_TORCH_SOUND] = tr("Sound");
    m_aRowName[eObjParam_RANGE] = tr("Radius");
    m_aRowName[eObjParam_LIGHT_RADIUS] = tr("Radius");
    m_aRowName[eObjParam_SOUND_INNER_RADIUS] = tr("Inner radius");
    m_aRowName[eObjParam_SOUND_RANGE] = tr("Outer radius");
    m_aRowName[eObjParam_SOUND_MIN] = tr("Minimum delay");
    m_aRowName[eObjParam_SOUND_MAX] = tr("Maximum delay");
    m_aRowName[eObjParam_SOUND_RESNAME] = tr("Sound files");
    m_aRowName[eObjParam_SOUND_AMBIENT] = tr("Ambient");
    m_aRowName[eObjParam_SOUND_IS_MUSIC] = tr("Music");
    m_aRowName[eObjParam_LIGHT_SHADOW] = tr("Cast shadows");
    m_aRowName[eObjParam_LIGHT_COLOR] = tr("Color");
    m_aRowName[eObjParam_PARTICL_TYPE] = tr("Particle type");
    m_aRowName[eObjParam_PARTICL_SCALE] = tr("Particle scale");
    m_aRowName[eObjParam_LEVER_SCIENCE_STATS_Type_Open] = tr("Allowed interaction");
    m_aRowName[eObjParam_LEVER_SCIENCE_STATS_Key_ID] = tr("Required key number");
    m_aRowName[eObjParam_LEVER_SCIENCE_STATS_Hands_Sleight] = tr("Required Science skill");
    m_aRowName[eObjParam_LEVER_CUR_STATE] = tr("Current state");
    m_aRowName[eObjParam_LEVER_TOTAL_STATE] = tr("Number of states");
    m_aRowName[eObjParam_LEVER_IS_CYCLED] = tr("Repeatable");
    m_aRowName[eObjParam_LEVER_IS_DOOR] = tr("Door interaction");
    m_aRowName[eObjParam_LEVER_RECALC_GRAPH] = tr("Recalculate navigation graph");
    m_aRowName[eObjParam_TRAP_DIPLOMACY] = tr("Stored target mask (legacy)");
    m_aRowName[eObjParam_TRAP_EFFECTIVE_TARGETS] = tr("Will attack");
    m_aRowName[eObjParam_TRAP_SPELL] = tr("Spell");
    m_aRowName[eObjParam_TRAP_AREA_RADIUS] = tr("Activation radius");
    m_aRowName[eObjParam_TRAP_CAST_INTERVAL] = tr("Cast interval");
    m_aRowName[eObjParam_TRAP_CAST_ONCE] = tr("Nearest unit only");
    m_aRowName[eObjParam_UNIT_NEED_IMPORT] = tr("Use MOB unit values");
    m_aRowName[eObjParam_UNIT_PROTOTYPE] = tr("Database prototype");
    m_aRowName[eObjParam_UNIT_ARMORS] = tr("Equipped armor");
    m_aRowName[eObjParam_UNIT_WEAPONS] = tr("Equipped weapons");
    m_aRowName[eObjParam_UNIT_SPELLS] = tr("Spells");
    m_aRowName[eObjParam_UNIT_QUICK_ITEMS] = tr("Quick items");
    m_aRowName[eObjParam_UNIT_QUEST_ITEMS] = tr("Quest items");
    m_aRowName[eObjParam_UNIT_STATS] = tr("Unit parameters");
    m_aRowName[eObjParam_ALWAYS_ACTIVE] = tr("Always active");
    m_aRowName[eObjParam_GUARD_PLACE] = tr("Guard position");
    m_aRowName[eObjParam_GUARD_RADIUS] = tr("Guard radius");
    m_aRowName[eObjParam_GUARD_ALARM] = tr("Call-for-help radius");
    m_aRowName[eObjParam_LOGIC_BEHAVIOUR] = tr("Behavior");
    m_aRowName[eObjParam_AGRESSION_MODE] = tr("Aggression mode");
    m_aRowName[eObjParam_LOGIC_USE] = tr("Logic enabled");
    m_aRowName[eObjParam_LOGIC_CYCLIC] = tr("Patrol route repeats");
    m_aRowName[eObjParam_LOGIC_WAIT] = tr("Guard idle time");
    m_aRowName[eObjParam_LOGIC_ALARM_CONDITION] = tr("Alarm condition");
    m_aRowName[eObjParam_LOGIC_ALARM_NUMBER] = tr("Alarm raised");
    m_aRowName[eObjParam_VIEW_WAIT] = tr("Wait time");
    m_aRowName[eObjParam_VIEW_TURN_SPEED] = tr("Turn speed");
    m_aRowName[eObjParam_VIEW_FLAGS] = tr("Action flags");

    m_aRowToolTip[eObjParam_NID] = tr("Object identifier used by the map and scripts.");
    m_aRowToolTip[eObjParam_OBJECT_TYPE] = tr("Determines how the game handles this object. Use an explicit conversion command to change it.");
    m_aRowToolTip[eObjParam_PLAYER] = tr("Ownership and diplomacy group used by this object.");
    m_aRowToolTip[eObjParam_NAME] = tr("Name stored with this object in the MOB.");
    m_aRowToolTip[eObjParam_TEMPLATE] = tr("Model used to render this object.");
    m_aRowToolTip[eObjParam_BODYPARTS] = tr("Model parts stored for this object. WARNING: An empty list enables every model part; incompatible combinations can crash the game, especially on units.");
    m_aRowToolTip[eObjParam_PARENT_TEMPLATE] = tr("Preserved object-template value. Normal units use Database prototype instead.");
    m_aRowToolTip[eObjParam_PRIM_TXTR] = tr("Texture used by the model's primary texture set.");
    m_aRowToolTip[eObjParam_SEC_TXTR] = tr("Texture used by the model's second texture set, when the model has one.");
    m_aRowToolTip[eObjParam_POSITION] = tr("Object position on the map.");
    m_aRowToolTip[eObjParam_ROTATION] = tr("Object orientation.");
    m_aRowToolTip[eObjParam_COMPLECTION] = tr("Object size along each axis. 1, 1, 1 is the model's original size.");
    m_aRowToolTip[eObjParam_USE_IN_SCRIPT] = tr("Keep this object available to scripts that modify it during play.");
    m_aRowToolTip[eObjParam_IS_SHADOW] = tr("Allow this object to cast a shadow.");
    m_aRowToolTip[eObjParam_QUEST_INFO] = tr("Zone-local quest variable. Value 1 shows the active quest marker.");
    m_aRowToolTip[eObjParam_COMMENTS] = tr("Editor notes. The game does not use this text.");

    m_aRowToolTip[eObjParam_TYPE] = tr("Serialized unit category.");
    m_aRowToolTip[eObjParam_UNIT_NEED_IMPORT] = tr("On uses the equipment, inventory, spells, and parameters stored in this MOB. Off uses the Database prototype.");
    m_aRowToolTip[eObjParam_UNIT_PROTOTYPE] = tr("Unit definition used by the game.");
    m_aRowToolTip[eObjParam_UNIT_ARMORS] = tr("Armor stored directly in this MOB. Used when Use MOB unit values is on.");
    m_aRowToolTip[eObjParam_UNIT_WEAPONS] = tr("Weapons stored directly in this MOB. Used when Use MOB unit values is on.");
    m_aRowToolTip[eObjParam_UNIT_SPELLS] = tr("Spells stored directly in this MOB. Used when Use MOB unit values is on.");
    m_aRowToolTip[eObjParam_UNIT_QUICK_ITEMS] = tr("Quick-use items stored directly in this MOB. Used when Use MOB unit values is on.");
    m_aRowToolTip[eObjParam_UNIT_QUEST_ITEMS] = tr("Quest inventory stored directly in this MOB. Used when Use MOB unit values is on.");
    m_aRowToolTip[eObjParam_UNIT_STATS] = tr("Unit parameters stored directly in this MOB. Used when Use MOB unit values is on.");

    m_aRowToolTip[eObjParam_LOGIC_USE] = tr("For Logic 1-4, allow this setup to activate when its corresponding world alarm is raised. Logic 0 is the default enabled slot and ignores this setting.");
    m_aRowToolTip[eObjParam_LOGIC_BEHAVIOUR] = tr("Controls how the unit moves and guards while this logic is active.");
    m_aRowToolTip[eObjParam_GUARD_ALARM] = tr("Distance within which allied units can respond for help.");
    m_aRowToolTip[eObjParam_GUARD_RADIUS] = tr("Maximum roaming distance for Guard area and Alarm guard. The unit moves to random points inside this radius.");
    m_aRowToolTip[eObjParam_GUARD_PLACE] = tr("Center of random roaming for Guard area. Alarm guard ignores this value and uses the active alarm coordinates.");
    m_aRowToolTip[eObjParam_ALWAYS_ACTIVE] = tr("Keep this unit's AI running regardless of distance. Otherwise it activates within twice the player's maximum vision range plus 20 metres.");
    m_aRowToolTip[eObjParam_AGRESSION_MODE] = tr("Controls how the unit reacts to enemies.");
    m_aRowToolTip[eObjParam_LOGIC_ALARM_CONDITION] = tr("Event that makes this unit raise its configured alarm.");
    m_aRowToolTip[eObjParam_LOGIC_CYCLIC] = tr("On returns from the last patrol point to the first. Off follows the route back in reverse.");
    m_aRowToolTip[eObjParam_LOGIC_WAIT] = tr("Stored and preserved in the MOB. The audited unit AI does not use this value.");
    m_aRowToolTip[eObjParam_LOGIC_ALARM_NUMBER] = tr("World alarm this unit raises when its alarm condition occurs. 0 raises no alarm.");
    m_aRowToolTip[eObjParam_VIEW_WAIT] = tr("Time spent looking toward this point. 15 ticks equal one second.");
    m_aRowToolTip[eObjParam_VIEW_TURN_SPEED] = tr("How quickly the unit turns toward this point. 0 turns immediately.");
    m_aRowToolTip[eObjParam_VIEW_FLAGS] = tr("0 uses normal behavior. 1 rests at the point.");

    m_aRowToolTip[eObjParam_LEVER_SCIENCE_STATS_Type_Open] = tr("Ways the player may operate this lever.");
    m_aRowToolTip[eObjParam_LEVER_SCIENCE_STATS_Key_ID] = tr("Key number required when Key is an allowed interaction.");
    m_aRowToolTip[eObjParam_LEVER_SCIENCE_STATS_Hands_Sleight] = tr("Minimum Science skill required for manual interaction.");
    m_aRowToolTip[eObjParam_LEVER_CUR_STATE] = tr("State active when the map loads.");
    m_aRowToolTip[eObjParam_LEVER_TOTAL_STATE] = tr("Number of states available to this lever.");
    m_aRowToolTip[eObjParam_LEVER_IS_CYCLED] = tr("Allow the lever to be operated repeatedly.");
    m_aRowToolTip[eObjParam_LEVER_IS_DOOR] = tr("Treat this lever as a door approached from its usable sides.");
    m_aRowToolTip[eObjParam_LEVER_RECALC_GRAPH] = tr("Update AI navigation when this object's state changes. Enable it when states change passability.");

    m_aRowToolTip[eObjParam_TRAP_DIPLOMACY] = tr("Serialized 32-bit player-group mask. Normal map loading replaces it using Player group and the MOB diplomacy table.");
    m_aRowToolTip[eObjParam_TRAP_EFFECTIVE_TARGETS] = tr("Groups marked War in this trap's Player group row of the containing MOB's diplomacy table. This is the target mask the game calculates when the map loads.");
    m_aRowToolTip[eObjParam_TRAP_SPELL] = tr("Spell cast when the trap activates.");
    m_aRowToolTip[eObjParam_TRAP_CAST_INTERVAL] = tr("Delay before the trap can cast again. 15 ticks equal one second.");
    m_aRowToolTip[eObjParam_TRAP_CAST_ONCE] = tr("For unit-targeted spells, affect only the nearest eligible unit. Off affects all eligible units.");
    m_aRowToolTip[eObjParam_TRAP_AREA_RADIUS] = tr("Radius checked around this activation center.");

    m_aRowToolTip[eObjParam_TORCH_PTLINK] = tr("Position of the torch's light and particle effect relative to the object.");
    m_aRowToolTip[eObjParam_TORCH_STRENGHT] = tr("Strength of the torch light and effect.");
    m_aRowToolTip[eObjParam_TORCH_SOUND] = tr("Sound played by this torch.");
    m_aRowToolTip[eObjParam_LIGHT_SHADOW] = tr("Allow this light to cast shadows.");
    m_aRowToolTip[eObjParam_LIGHT_COLOR] = tr("Light color.");
    m_aRowToolTip[eObjParam_LIGHT_RADIUS] = tr("Distance affected by the light. Negative values darken instead.");
    m_aRowToolTip[eObjParam_SOUND_INNER_RADIUS] = tr("Area where the sound plays at full volume.");
    m_aRowToolTip[eObjParam_SOUND_RANGE] = tr("Distance where the sound fades to silence.");
    m_aRowToolTip[eObjParam_SOUND_RESNAME] = tr("Sound resources used by this source.");
    m_aRowToolTip[eObjParam_SOUND_MIN] = tr("Shortest stored delay between ambient playbacks.");
    m_aRowToolTip[eObjParam_SOUND_MAX] = tr("Longest stored delay between ambient playbacks.");
    m_aRowToolTip[eObjParam_SOUND_AMBIENT] = tr("Play this source repeatedly using its delay range.");
    m_aRowToolTip[eObjParam_SOUND_IS_MUSIC] = tr("Stored and preserved in the MOB. No confirmed runtime audio behavior uses this flag.");
    m_aRowToolTip[eObjParam_PARTICL_TYPE] = tr("Effect generated by this particle source.");
    m_aRowToolTip[eObjParam_PARTICL_SCALE] = tr("Size multiplier for the particle effect.");
}

bool CTableManager::isValidValue(const EObjParam param, const QString &value)
{
    bool bRes = false;
    switch (param) {
    case eObjParam_NID:
    case eObjParam_RANGE:
    case eObjParam_SOUND_INNER_RADIUS:
    case eObjParam_SOUND_RANGE:
    case eObjParam_SOUND_MIN:
    case eObjParam_SOUND_MAX:
    case eObjParam_LEVER_SCIENCE_STATS_Key_ID:
    case eObjParam_LEVER_SCIENCE_STATS_Hands_Sleight:
    case eObjParam_LEVER_TOTAL_STATE:
    case eObjParam_LEVER_CUR_STATE:
    case eObjParam_VIEW_WAIT:
    case eObjParam_VIEW_TURN_SPEED:
    case eObjParam_TRAP_CAST_INTERVAL:
    { // uint
        QRegExp rx("[0-9]+");
        bRes = rx.exactMatch(value);
        break;
    }
    case eObjParam_PARENT_ID:
    case eObjParam_TRAP_DIPLOMACY:
    { // int
        QRegExp rx("-?[0-9]+");
        bRes = rx.exactMatch(value);
        break;
    }
    case eObjParam_GUARD_RADIUS:
    case eObjParam_GUARD_ALARM:
    case eObjParam_LIGHT_RADIUS:
    case eObjParam_LOGIC_WAIT:
    case eObjParam_TORCH_STRENGHT:
    case eObjParam_PARTICL_SCALE:
    case eObjParam_TRAP_AREA_RADIUS:
    { // float
        QRegExp rx("^\\s*-?\\d+(\\.\\d+)?");
        bRes = rx.exactMatch(value);
        break;
    }
    case eObjParam_GUARD_PLACE:
    case eObjParam_TORCH_PTLINK:
    case eObjParam_LIGHT_COLOR: //todo: use custom palette choose
    case eObjParam_COMPLECTION:
    //case eObjParam_POSITION:
    case eObjParam_ROTATION:
    {// vector3d. checked value: (111.66,94.46,-0.25) ( 0.21, 0.13, 1.20)
        QRegExp rx("\\((\\s*-?\\d+(\\.\\d+)?\\,){2}(\\s*-?\\d+(\\.\\d+)?)\\)");
        bRes = rx.exactMatch(value);
        break;
    }
    case eObjParam_UNIT_PROTOTYPE:
    { // allow any not-empty string
        bRes = !value.isEmpty();
        break;
    }
    case eObjParam_BODYPARTS: //todo: use custom popup instead string value
    case eObjParam_SEC_TXTR:
    case eObjParam_NAME:
    case eObjParam_QUEST_INFO:
    case eObjParam_TORCH_SOUND:
    case eObjParam_PARENT_TEMPLATE:
    case eObjParam_SOUND_RESNAME:
    case eObjParam_COMMENTS:
    { // allow any string
        bRes = true;
        break;
    }
    default:
    {
        bRes = true;
        break;
    }

    //case eObjParam_TRAP_SPELL
//    case eObjParam_UNIT_STATS
//    case eObjParam_UNIT_WEAPONS
//    case eObjParam_UNIT_ARMORS
//    case eObjParam_UNIT_SPELLS
//    case eObjParam_UNIT_QUICK_ITEMS
//    case eObjParam_UNIT_QUEST_ITEMS

    }

    return bRes;
}

void CTableManager::onParamChange(const QSharedPointer<IPropertyBase>& pProp)
{
    if(m_readOnly)
        return;
    for(auto it = m_rowValues.begin(); it != m_rowValues.end(); ++it)
    {
        if(it.value()->type() == pProp->type())
        {
            it.value() = clonePropertyValue(pProp);
            break;
        }
        prop3D* vector = dynamic_cast<prop3D*>(it.value().get());
        const EObjParam vectorType = it.value()->type();
        const int component = int(pProp->type()) - (int(it.value()->type()) - 3);
        if(vector && (vectorType == eObjParam_POSITION || vectorType == eObjParam_ROTATION
                      || vectorType == eObjParam_COMPLECTION || vectorType == eObjParam_GUARD_PLACE)
           && component >= 0 && component < 3)
        {
            QVector3D value = vector->value();
            if(component == 0) value.setX(pProp->toString().toFloat());
            if(component == 1) value.setY(pProp->toString().toFloat());
            if(component == 2) value.setZ(pProp->toString().toFloat());
            it.value().reset(new prop3D(it.value()->type(), value));
            break;
        }
    }
    emit onUpdateProperty(pProp);
}

///
/// \brief CTableManager::onCellEdit
/// Manually connect to cell widget and get new value if possible. QTableWidget cant use signal-slot system
/// \param pItem
///
void CTableManager::onCellEdit(QTableWidgetItem* pItem)
{
    CValueItem* pValue = dynamic_cast<CValueItem*>(pItem);
    if(nullptr == pValue)
        return;
}

void blockEditWidget(QTableWidgetItem* pItem)
{
    pItem->setFlags(pItem->flags() & ~Qt::ItemIsEditable);
}

// function to sort prop in table
bool variantLessThan(const std::pair<QSharedPointer<IPropertyBase>, bool> &v1, const std::pair<QSharedPointer<IPropertyBase>, bool> &v2)
{
    return v1.first->type() < v2.first->type();
}

void CTableManager::setNewData(const QList<QSharedPointer<IPropertyBase>>& aProp,
                               bool magicTrapContext,
                               bool lookPointContext)
{
    m_savedVerticalScroll = m_pTable->verticalScrollBar()->value();
    m_savedHorizontalScroll = m_pTable->horizontalScrollBar()->value();
    reset();
    m_logicZeroContext = false;
    m_torchPropertyContext = false;
    m_magicTrapPropertyContext = magicTrapContext;
    m_lookPointPropertyContext = lookPointContext;
    for(const QSharedPointer<IPropertyBase>& property : aProp)
        if(property && property->type() == eObjParam_TORCH_STRENGHT)
        {
            m_torchPropertyContext = true;
            break;
        }
    int i(0); // row number
    EObjParam type;
    m_pTable->blockSignals(true); //block sending signals


    auto insert3DProp =[this, &aProp](const int row, const EObjParam xPosProp)
    {
        m_pTable->insertRow(row);
        m_pTable->setItem(row, 0, new QTableWidgetItem(m_aRowName[EObjParam(xPosProp+3)]));
        blockEditWidget(m_pTable->item(row, 0));
        auto* pItem = new C3DItem(util::constProp(aProp, xPosProp), util::constProp(aProp, EObjParam(xPosProp+1)), util::constProp(aProp, EObjParam(xPosProp+2)));
        QObject::connect(pItem, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
        m_pTable->setCellWidget(row,1, pItem);
        const QSharedPointer<IPropertyBase> x = util::constProp(aProp, xPosProp);
        const QSharedPointer<IPropertyBase> y = util::constProp(aProp, EObjParam(xPosProp+1));
        const QSharedPointer<IPropertyBase> z = util::constProp(aProp, EObjParam(xPosProp+2));
        QSharedPointer<IPropertyBase> aggregate;
        if(x->isInit() && y->isInit() && z->isInit())
            aggregate.reset(new prop3D(EObjParam(xPosProp+3), x->toString().toFloat(),
                                       y->toString().toFloat(), z->toString().toFloat()));
        else
            aggregate.reset(new prop3D(EObjParam(xPosProp+3)));
        registerPropertyRow(row, aggregate);
        m_pTable->setRowHeight(row, pItem->height()*3);
    };

    for(int iParam(0); iParam < EObjParam::eObjParamCount; ++iParam)
    for (const auto& item : aProp)
    {
        type = item->type();
        if(type != iParam)
            continue; // fcking sort of properties

        switch (type)
        {
        //dropdown list next
        case eObjParam_PLAYER:
        case eObjParam_IS_SHADOW:
        case eObjParam_LEVER_IS_CYCLED:
        case eObjParam_LEVER_IS_DOOR:
        case eObjParam_USE_IN_SCRIPT:
        case eObjParam_LEVER_RECALC_GRAPH:
        case eObjParam_SOUND_AMBIENT:
        case eObjParam_SOUND_IS_MUSIC:
        case eObjParam_LIGHT_SHADOW:
        case eObjParam_TYPE:
        case eObjParam_PARTICL_TYPE:
        case eObjParam_AGRESSION_MODE:
        case eObjParam_TRAP_CAST_ONCE:
        case eObjParam_LEVER_SCIENCE_STATS_Type_Open:
        case eObjParam_ALWAYS_ACTIVE:
        case eObjParam_LOGIC_USE:
        case eObjParam_LOGIC_CYCLIC:
        case eObjParam_LOGIC_ALARM_CONDITION:
        {
            m_pTable->insertRow(i);
            //https://doc.qt.io/archives/qt-4.8/qtablewidget.html#setItem
            //The table takes ownership of the item.
            // => table widget take care about memory
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            //todo: collect all created items and delete them before create new (or update). reduce memory leaks
            CComboStItem* pCombo = new CComboStItem(item);
            QObject::connect(pCombo, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pCombo);
            registerPropertyRow(i, item);
            ++i;
            break;
        }
        case eObjParam_PRIM_TXTR:
        case eObjParam_UNIT_PROTOTYPE:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CComboDynItem* pCombo = new CComboDynItem(item);
            QObject::connect(pCombo, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pCombo);
            registerPropertyRow(i, item);
            ++i;
            break;
        }
        case eObjParam_BODYPARTS:
        { //this case process below
            break;
        }
        case eObjParam_OBJECT_TYPE:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            auto* value = new QLineEdit(item->toString());
            value->setReadOnly(true);
            value->setFocusPolicy(Qt::StrongFocus);
            m_pTable->setCellWidget(i, 1, value);
            registerPropertyRow(i, item);
            ++i;
            break;
        }
        case eObjParam_TRAP_EFFECTIVE_TARGETS:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CTrapTargetItem* targets = new CTrapTargetItem(item);
            m_pTable->setCellWidget(i, 1, targets);
            registerPropertyRow(i, item);
            ++i;
            break;
        }
        case eObjParam_TEMPLATE:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CComboDynItem* pCombo = new CComboDynItem(item);
            QObject::connect(pCombo, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pCombo);
            registerPropertyRow(i, item);
            ++i;
            auto bodyPart = util::constProp(aProp, eObjParam_BODYPARTS);
            if(!bodyPart.isNull())
            {
                m_pTable->insertRow(i);
                m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[eObjParam_BODYPARTS]));
                blockEditWidget(m_pTable->item(i, 0));
                CBodyPartItem* pPart = new CBodyPartItem(bodyPart);
                QObject::connect(pPart, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
                m_pTable->setCellWidget(i, 1, pPart);
                registerPropertyRow(i, bodyPart);
                ++i;
            }
            break;
        }
//        case eObjParam_TRAP_CAST_INTERVAL:
//        {// will be processed below
//            break;
//        }
//        case eObjParam_TRAP_CAST_ONCE:
//        {
//            m_pTable->insertRow(i);
//            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[item.first]));
//            m_pTable->item(i, 0)->setFlags(m_pTable->item(i, 0)->flags() & ~Qt::ItemIsEditable);
//            CComboBoxItem* pCombo = new CComboBoxItem(item.second, item.first);
//            QObject::connect(pCombo, SIGNAL(updateValueOver(CComboBoxItem*)), this, SLOT(onParamChange(CComboBoxItem*)));
//            m_pTable->setCellWidget(i, 1, pCombo);
//            ++i;
//            if(item.second == "0") // cast once == false
//            {
//                m_pTable->insertRow(i);
//                m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[eObjParam_TRAP_CAST_INTERVAL]));
//                m_pTable->item(i, 0)->setFlags(m_pTable->item(i, 0)->flags() & ~Qt::ItemIsEditable);
//                m_pTable->setItem(i, 1, new CStringItem(aParam[eObjParam_TRAP_CAST_INTERVAL], eObjParam_TRAP_CAST_INTERVAL));
//                m_pTable->resizeColumnToContents(0);
//                ++i;
//            }
//            break;
//        }
        case eObjParam_GUARD_RADIUS:
        { //this case process below
            break;
        }
        case eObjParam_LOGIC_BEHAVIOUR:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CComboStItem* pCombo = new CComboStItem(item);
            QObject::connect(pCombo, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pCombo);
            registerPropertyRow(i, item);
            ++i;

            const EBehaviourType behavior = item->isInit()
                ? EBehaviourType(dynamic_cast<propUint*>(item.get())->value())
                : EBehaviourType::eIdle;
            if(item->isInit() && (behavior == EBehaviourType::eRadius
                                  || behavior == EBehaviourType::eGuardAlaram))
            {
                m_pTable->insertRow(i);
                m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[eObjParam_GUARD_RADIUS]));
                blockEditWidget(m_pTable->item(i, 0));
                auto* pCellValue = new CValueItem(util::constProp(aProp, eObjParam_GUARD_RADIUS));
                QObject::connect(pCellValue, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
                m_pTable->setCellWidget(i, 1, pCellValue);
                registerPropertyRow(i, util::constProp(aProp, eObjParam_GUARD_RADIUS));
                m_pTable->resizeColumnToContents(0);
                ++i;
            }
            break;
        }
        case eObjParam_UNIT_STATS:
        case eObjParam_UNIT_WEAPONS:
        case eObjParam_UNIT_ARMORS:
        case eObjParam_UNIT_SPELLS:
        case eObjParam_UNIT_QUICK_ITEMS:
        case eObjParam_UNIT_QUEST_ITEMS:
        {//will be processed below
            break;
        }
        case eObjParam_UNIT_NEED_IMPORT:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CComboStItem* pCombo = new CComboStItem(item);
            QObject::connect(pCombo, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pCombo);
            registerPropertyRow(i, item);
            ++i;
            if(item->isInit() && dynamic_cast<propBool*>(item.get())->value() == true) // is need import unit stats == true
            {
                QVector<EObjParam> importStats;
                importStats.resize(5);
                auto ustat = util::constProp(aProp, eObjParam_UNIT_STATS);
                m_pTable->insertRow(i);
                m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[eObjParam_UNIT_STATS]));
                blockEditWidget(m_pTable->item(i, 0));
                auto* pMulti = new CUnitStatItem(ustat);
                QObject::connect(pMulti, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
                m_pTable->setCellWidget(i, 1, pMulti);
                registerPropertyRow(i, ustat);
                m_pTable->resizeColumnToContents(0);
                ++i;
                //importStats[0] = eObjParam_UNIT_STATS;
                importStats[0] = eObjParam_UNIT_WEAPONS;
                importStats[1] = eObjParam_UNIT_ARMORS;
                importStats[2] = eObjParam_UNIT_SPELLS;
                importStats[3] = eObjParam_UNIT_QUICK_ITEMS;
                importStats[4] = eObjParam_UNIT_QUEST_ITEMS;
                for(int j(0); j<importStats.size();++j)
                {
                    m_pTable->insertRow(i);
                    m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[importStats[j]]));
                    blockEditWidget(m_pTable->item(i, 0));
                    auto* pMulti = new CMultiLineButtonItem(util::constProp(aProp, importStats[j]));
                    QObject::connect(pMulti, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
                    m_pTable->setCellWidget(i, 1, pMulti);
                    registerPropertyRow(i, util::constProp(aProp, importStats[j]));
                    m_pTable->resizeColumnToContents(0);
                    ++i;
                }
            }
            break;
        }
        case eObjParam_SOUND_RESNAME:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            auto* pMulti = new CMultiLineButtonItem(item);
            QObject::connect(pMulti, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pMulti);
            registerPropertyRow(i, item);
            ++i;
            break;
        }
        case eObjParam_LIGHT_COLOR:
        {// for color create tool button with color
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CColorButtonItem* pColorButton = nullptr;
            pColorButton = new CColorButtonItem(item);
            QObject::connect(pColorButton, SIGNAL(onColorChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pColorButton);
            registerPropertyRow(i, item);
            ++i;
            break;
        }
        case eObjParam_POSITION_Y:
        case eObjParam_POSITION_Z:
        case eObjParam_POSITION:
        case eObjParam_ROTATION_Y:
        case eObjParam_ROTATION_Z:
        case eObjParam_ROTATION:
        case eObjParam_COMPLECTION_Y:
        case eObjParam_COMPLECTION_Z:
        case eObjParam_COMPLECTION:
        case eObjParam_GUARD_PLACE_Y:
        case eObjParam_GUARD_PLACE_Z:
        case eObjParam_GUARD_PLACE:
        {//will be processed below
            break;
        }
        case eObjParam_POSITION_X:
        case eObjParam_ROTATION_X:
        case eObjParam_COMPLECTION_X:
        case eObjParam_GUARD_PLACE_X:
        {
            insert3DProp(i, type);
            ++i;
            break;
        }
        default:
        {
            m_pTable->insertRow(i);
            m_pTable->setItem(i, 0, new QTableWidgetItem(m_aRowName[type]));
            blockEditWidget(m_pTable->item(i, 0));
            CValueItem* pCellValue = new CValueItem(item);
            QObject::connect(pCellValue, SIGNAL(onParamChange(QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(QSharedPointer<IPropertyBase>)));
            m_pTable->setCellWidget(i, 1, pCellValue);
            registerPropertyRow(i, item);
            m_pTable->resizeColumnToContents(0);
            ++i;
            break;
        }
        } //switch
    }
    m_pTable->blockSignals(false);
    restoreScrollPosition();
}

void CColorButtonItem::applyColor()
{
    QColor initColor(Qt::black);
    if(m_pValue->isInit())
    {
        const auto& clr = dynamic_cast<prop3D*>(m_pValue.get())->value();
        initColor = QColor(clr.x()*255, clr.y()*255, clr.z()*255);
    }
    QColor color = QColorDialog::getColor(initColor);
    if(color.isValid())
    {
        updateColor(color);
        dynamic_cast<prop3D*>(m_pValue.get())->setValue(color.redF(), color.greenF(), color.blueF());
        emit onColorChange(m_pValue);
    }
}

CColorButtonItem::CColorButtonItem(const QSharedPointer<IPropertyBase>& prop)
{
    setFocusPolicy(Qt::StrongFocus);
    if(prop->isInit())
        m_pValue.reset(prop->clone());
    else
    {
        m_pValue.reset(new prop3D(prop->type(), 0.0f, 0.0f, 0.0f)); //todo: display value dif
    }
    const auto& clr = dynamic_cast<prop3D*>(m_pValue.get())->value();
    setColor(clr);
    QObject::connect(this, SIGNAL(clicked()), this, SLOT(applyColor()));
}

CColorButtonItem::~CColorButtonItem()
{
}

void CColorButtonItem::renewColor(const QVector3D& prop)
{
    m_pValue.reset(new prop3D(EObjParam::eObjParam_LIGHT_COLOR, prop.x(), prop.y(), prop.z()));
    const auto& clr = dynamic_cast<prop3D*>(m_pValue.get())->value();
    setColor(clr);
}

void CColorButtonItem::updateColor(const QColor &color)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Button, color);
    setAutoFillBackground(true);
    setPalette(pal);
    update();
}

void CColorButtonItem::setColor(QVector3D clr)
{
    QColor color;
    color.setRedF(clr.x());
    color.setGreenF(clr.y());
    color.setBlueF(clr.z());
    updateColor(color);

}

bool CValueItem::applyChanges(const QString& text)
{
    if(m_pValue->toString() != text)
    {
        m_pValue->resetFromString(text);
        qDebug() << m_pValue->toString();
        return true;
    }
    //todo: validate data. return value if checks failed
    return false;
}

void CValueItem::renewValue(QString value)
{
    if(!value.isEmpty())
    {
        setText(value);
    }
    m_pValue->resetFromString(value);

    m_filter.reset(new CLineEditEventFilter(this, text())); // create text backuper with old text
    this->installEventFilter(m_filter.get());
    updateDisplayedText();
}

void CValueItem::setCompactDisplay(bool compact)
{
    if(m_compactDisplay == compact)
        return;
    m_compactDisplay = compact;
    updateDisplayedText();
}

void CValueItem::updateDisplayedText()
{
    if(hasFocus())
        return;
    const QString fullValue = m_pValue->isInit() ? m_pValue->toString() : QString();
    const int textWidth = qMax(0, contentsRect().width() - 8);
    const QString visibleValue = m_compactDisplay
        ? fontMetrics().elidedText(fullValue, Qt::ElideRight, textWidth)
        : fullValue;
    setText(visibleValue);
}

void CValueItem::focusInEvent(QFocusEvent* event)
{
    if(m_pValue->isInit())
        setText(m_pValue->toString());
    QLineEdit::focusInEvent(event);
}

void CValueItem::focusOutEvent(QFocusEvent* event)
{
    QLineEdit::focusOutEvent(event);
    updateDisplayedText();
}

void CValueItem::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    updateDisplayedText();
}

void CValueItem::onTextChangeEnd()
{
    if(bSkip)
    {
        bSkip = false;
        return;
    }
    const QString& val = text();
    if(m_pValue->isInit() && (m_pValue->toString() == val))
        return;

    if(!compatibleText(m_pValue->type(), val))
    {
        QApplication::beep();
        const QString oldValue = m_pValue->isInit() ? m_pValue->toString() : QString();
        setText(oldValue);
        m_filter->updateValue(oldValue);
        return;
    }
    m_pValue->resetFromString(val);
    m_filter->updateValue(val);
    emit onParamChange(m_pValue);
}

CComboStItem::CComboStItem(const QSharedPointer<IPropertyBase>& prop)
{
    m_pValue.reset(prop->clone());
    if(!CResourceStringList::getInstance()->getPropList(m_valueList, prop->type()))
    {// case for property that has self-named value (without int-string convertion
        return;
    }

    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    view()->setMouseTracking(true);
    view()->viewport()->setMouseTracking(true);
    view()->viewport()->installEventFilter(this);
    //lineEdit()->setPlaceholderText("Select item");
    //addItems(m_valueList.values());
    // QComboBox's default QStandardItemModel stores ToolTipRole. QStringListModel
    // rejects that role, leaving every popup entry without its specific help.
    addItems(m_valueList.values());
    int index = 0;
    for(auto it = m_valueList.constBegin(); it != m_valueList.constEnd(); ++it, ++index)
    {
        const QString tooltip = staticChoiceToolTip(prop->type(), it.key());
        if(!tooltip.isEmpty())
            setItemData(index, tooltip, Qt::ToolTipRole);
    }
    if(m_pValue->isInit())
        setCurrentText(m_valueList[m_pValue->toString().toUInt()]);
    else
    {
        insertItem(0, "undefined/different");
        setCurrentIndex(0);
    }
    QObject::connect(this, SIGNAL(currentIndexChanged(QString)), this, SLOT(_onChange(QString))); //reconnect default currentIndexChanged to override
}

bool CComboStItem::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == view()->viewport() && event->type() == QEvent::ToolTip)
    {
        QHelpEvent* help = static_cast<QHelpEvent*>(event);
        const QModelIndex index = view()->indexAt(help->pos());
        const QString tooltip = index.data(Qt::ToolTipRole).toString();
        if(!tooltip.isEmpty())
        {
            QToolTip::showText(help->globalPos(), tooltip, view());
            event->accept();
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void CComboStItem::_onChange(QString str)
{
    QSharedPointer<IPropertyBase> valueNew(m_pValue->clone());
    QString sourceValue = QString::number(m_valueList.key(str));
    valueNew->resetFromString(sourceValue);
    if(m_pValue->isInit() && valueNew->isEqual(m_pValue.get()))
        return;

    m_pValue->resetFromString(sourceValue);
    emit onParamChange(m_pValue);
}


CValueItem::CValueItem(const QSharedPointer<IPropertyBase>& prop):
    bSkip(false)
{
    m_pValue.reset(prop->clone());
    if(false)
    {
        // todo: add validator for each text type (int, uint, float, string)
        QRegExp re("\\d+");
        QRegExpValidator *validator = new QRegExpValidator(re, this);
        setValidator(validator);
    }
    setPlaceholderText(tr("undefined/different"));
    //setFrame(false); //with frames look nicer

    QString value;

    if(m_pValue->isInit())
    {
        //setText(m_pValue->toString());
        value = m_pValue->toString();
    }
    QObject::connect(this, SIGNAL(editingFinished()), this, SLOT(onTextChangeEnd()));
    renewValue(value);
}


CComboDynItem::CComboDynItem(const QSharedPointer<IPropertyBase> &prop)
    : m_pCompleter(nullptr)
    , m_optionsLoaded(false)
    , m_popupActive(false)
{
    setFocusPolicy(Qt::FocusPolicy::StrongFocus);
    setStyleSheet("combobox-popup: 0;");
    setMaxVisibleItems(20);
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pListModel.reset(new QStringListModel());
    setModel(m_pListModel.get());
    m_pCompleter = new QCompleter(m_pListModel.get(), this);
    m_pCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_pCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_pCompleter->setFilterMode(Qt::MatchStartsWith);
    setCompleter(m_pCompleter);
    m_pValue.reset(prop->clone());

    if(m_pValue->isInit())
    {
        setEditText(m_pValue->toString());
        m_committedText = m_pValue->toString();
    }
    else
    {
        lineEdit()->setPlaceholderText(tr("undefined/different"));
        m_committedText.clear();
    }
    // A proxy-model refresh may highlight its first row. Only a deliberate
    // user activation is allowed to commit a dropdown value.
    QObject::connect(this, SIGNAL(activated(QString)), this, SLOT(_onChange(QString)));
    QObject::connect(m_pCompleter, SIGNAL(activated(QString)), this, SLOT(_onChange(QString)));
    QObject::connect(lineEdit(), SIGNAL(returnPressed()), this, SLOT(_onEditingFinished()));
    QObject::connect(lineEdit(), SIGNAL(textEdited(QString)), this, SLOT(_onFilterTextChanged(QString)));
    lineEdit()->installEventFilter(this);
    updateDatabaseDiagnostic(m_committedText);
}

void CColorButtonItem::keyPressEvent(QKeyEvent* event)
{
    if(event->matches(QKeySequence::Copy))
    {
        copyProperty(m_pValue, m_pValue->toString());
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste))
    {
        if(!clipboardMatches(m_pValue->type()) || g_propertyClipboard.size() != 1
           || g_propertyClipboard.first()->type() != m_pValue->type())
        {
            rejectIncompatiblePaste(this, m_pValue->type(), QGuiApplication::clipboard()->text());
            event->accept();
            return;
        }
        m_pValue.reset(g_propertyClipboard.first()->clone());
        const QVector3D color = dynamic_cast<prop3D*>(m_pValue.get())->value();
        setColor(color);
        emit onColorChange(m_pValue);
        event->accept();
        return;
    }
    QToolButton::keyPressEvent(event);
}

void CComboDynItem::ensureOptionsLoaded()
{
    if(m_optionsLoaded)
        return;
    const QString current = currentText();
    const QSignalBlocker blocker(this);
    if(m_pValue->type() == eObjParam_PRIM_TXTR)
        m_pListModel->setStringList(CTextureList::getInstance()->textureList());
    else if(m_pValue->type() == eObjParam_TEMPLATE)
        m_pListModel->setStringList(CObjectList::getInstance()->figureList());
    else if(m_pValue->type() == eObjParam_UNIT_PROTOTYPE)
    {
        QStringList names;
        const auto database = ei::ProfileDatabaseContext::instance().current();
        if(!database.isNull())
            for(const ei::SMonsterDatabaseRecord& monster : database->monsters)
                names.append(monster.name);
        names.sort(Qt::CaseInsensitive);
        m_pListModel->setStringList(names);
    }
    else
    {
        Q_ASSERT(false && "not defined");
        return;
    }
    setCurrentIndex(-1);
    setEditText(current);
    m_optionsLoaded = true;
}

void CComboDynItem::showPopup()
{
    const QString current = currentText();
    const QSignalBlocker blocker(this);
    ensureOptionsLoaded();
    setEditText(current);
    m_popupActive = true;
    QComboBox::showPopup();
}

void CComboDynItem::hidePopup()
{
    m_popupActive = false;
    QComboBox::hidePopup();
}

void CComboDynItem::_onChange(QString str)
{
    m_committedText = str;
    updateDatabaseDiagnostic(str);
    if(m_pValue->isEqual(str))
        return;

    m_pValue->resetFromString(str);
    emit onParamChange(m_pValue);
}

void CComboDynItem::updateDatabaseDiagnostic(const QString& value)
{
    if(m_pValue->type() != eObjParam_UNIT_PROTOTYPE)
        return;
    const auto database = ei::ProfileDatabaseContext::instance().current();
    QString diagnostic;
    if(database.isNull() || !database->usable())
        diagnostic = tr("The active profile database is unavailable; this value is preserved but cannot be validated.");
    else if(!database->monsters.contains(value.trimmed().toLower()))
        diagnostic = tr("Prototype is not present in the active profile database. The value is preserved.");
    setToolTip(diagnostic);
    lineEdit()->setToolTip(diagnostic);
    lineEdit()->setStyleSheet(diagnostic.isEmpty() ? QString() : QString("QLineEdit { border: 1px solid #c88b2a; }"));
}

void CComboStItem::keyPressEvent(QKeyEvent* event)
{
    if(event->matches(QKeySequence::Copy))
    {
        copyProperty(m_pValue, currentText());
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste))
    {
        const QString value = QGuiApplication::clipboard()->text();
        if(rejectIncompatiblePaste(this, m_pValue->type(), value)
           || !m_valueList.values().contains(value))
        {
            QApplication::beep();
            event->accept();
            return;
        }
        setCurrentText(value);
        _onChange(value);
        event->accept();
        return;
    }
    QComboBox::keyPressEvent(event);
}

void CValueItem::keyPressEvent(QKeyEvent* event)
{
    // Inside the editable text, Ctrl+C/Ctrl+V are ordinary text operations:
    // copy the exact highlighted characters and replace only the current
    // selection. Validation and property application still happen on commit.
    // A typed property copied from a plate retains compatibility checks; a
    // numeric cross-type paste inserts its safely converted display value at
    // the current selection without committing the edit early.
    if(event->matches(QKeySequence::Paste))
    {
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        if(mime && mime->hasFormat(kPropertyMimeType)
           && !clipboardMatches(m_pValue->type()))
        {
            QString conversionError;
            const QSharedPointer<IPropertyBase> converted = g_propertyClipboard.size() == 1
                ? convertedNumericValue(g_propertyClipboard.first().get(),
                                        m_pValue.get(), &conversionError)
                : QSharedPointer<IPropertyBase>();
            if(!converted.isNull())
            {
                insert(converted->toString());
                event->accept();
                return;
            }
            rejectIncompatiblePaste(this, m_pValue->type(), mime->text());
            event->accept();
            return;
        }
    }
    QLineEdit::keyPressEvent(event);
}

void CValueItem::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* menu = createStandardContextMenu();
    menu->exec(event->globalPos());
    delete menu;
}

void CComboDynItem::_onEditingFinished()
{
    const QString value = currentText().trimmed();
    if(value.isEmpty())
        return;
    _onChange(value);
}

void CComboDynItem::_onFilterTextChanged(const QString& text)
{
    ensureOptionsLoaded();
    // Keep the combo model stable while typing. Filtering the combo's own
    // model promotes its first row to the current value; a popup completer
    // provides prefix-only suggestions without changing or applying a value.
    m_pCompleter->setCompletionPrefix(text);
    m_pCompleter->complete();
}

void CComboDynItem::keyPressEvent(QKeyEvent* event)
{
    if(event->matches(QKeySequence::Copy))
    {
        copyProperty(m_pValue, currentText());
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste))
    {
        const QString text = QGuiApplication::clipboard()->text().trimmed();
        if(!rejectIncompatiblePaste(this, m_pValue->type(), text))
        {
            setEditText(text);
            _onChange(text);
        }
        event->accept();
        return;
    }
    if(event->key() == Qt::Key_Escape)
    {
        cancelPendingEdit();
        event->accept();
        return;
    }
    QComboBox::keyPressEvent(event);
}

void CComboDynItem::focusOutEvent(QFocusEvent* event)
{
    // Typed text is only a filter preview until Enter or an explicit
    // suggestion activation commits it. Opening the combo transfers focus to
    // its popup; that is not an abandoned edit and must not close the popup.
    QComboBox::focusOutEvent(event);
    if(!m_popupActive && event->reason() != Qt::PopupFocusReason)
        cancelPendingEdit();
}

bool CComboDynItem::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == lineEdit() && event->type() == QEvent::FocusOut)
    {
        const QFocusEvent* focusEvent = static_cast<QFocusEvent*>(event);
        if(!m_popupActive && focusEvent->reason() != Qt::PopupFocusReason)
            cancelPendingEdit();
    }
    if(watched == lineEdit() && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if(keyEvent->matches(QKeySequence::Copy))
            return false;
        if(keyEvent->matches(QKeySequence::Paste))
        {
            // Typed property bundles still cannot cross incompatible fields,
            // but ordinary text (including a partial selection copied from
            // another editor) is inserted natively and remains uncommitted
            // until Enter or an explicit dropdown choice.
            const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
            if(mime && mime->hasFormat(kPropertyMimeType)
               && !clipboardMatches(m_pValue->type()))
            {
                rejectIncompatiblePaste(this, m_pValue->type(), mime->text());
                return true;
            }
            return false;
        }
        if(keyEvent->key() == Qt::Key_Escape)
        {
            cancelPendingEdit();
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void CComboDynItem::cancelPendingEdit()
{
    if(m_pCompleter && m_pCompleter->popup())
        m_pCompleter->popup()->hide();
    hidePopup();
    setEditText(m_committedText);
}

C3DItem::C3DItem(const QSharedPointer<IPropertyBase>& propX,const QSharedPointer<IPropertyBase>& propY, const QSharedPointer<IPropertyBase>& propZ)
{
    m_xValue.reset(propX->clone());
    m_yValue.reset(propY->clone());
    m_zValue.reset(propZ->clone());
    // Component plates and line edits own keyboard focus. The aggregate
    // editor remains selectable through its table row but must not acquire
    // focus behind a child component's context menu.
    setFocusPolicy(Qt::FocusPolicy::NoFocus);
    m_componentLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    m_componentLayout->setContentsMargins(0, 0, 0, 0);
    m_componentLayout->setSpacing(2);
    int horizontalWidthHint = 0;
    const auto setProp = [this, &horizontalWidthHint](
            const QString& rowName, const QSharedPointer<IPropertyBase>& prop)
    {
        QWidget* component = new QWidget(this);
        QHBoxLayout* componentLayout = new QHBoxLayout(component);
        componentLayout->setContentsMargins(0, 0, 0, 0);
        componentLayout->setSpacing(0);
        auto* pLine = new CValueItem(prop);
        auto* plate = new CVectorComponentPlate(rowName, prop,
            [this, pLine](const QSharedPointer<IPropertyBase>& value)
            {
                pLine->renewValue(value->toString());
                _onParamChange(value);
            }, this);
        QObject::connect(pLine, SIGNAL(onParamChange(const QSharedPointer<IPropertyBase>&)), this, SLOT(_onParamChange(const QSharedPointer<IPropertyBase>&)));
        componentLayout->addWidget(plate);
        componentLayout->addWidget(pLine, 1);
        m_componentLayout->addWidget(component, 1);
        m_componentEditors.append(pLine);
        horizontalWidthHint += plate->sizeHint().width()
            + pLine->fontMetrics().horizontalAdvance(QStringLiteral("-000…")) + 12;
    };

    setProp("X:", propX);
    setProp("Y:", propY);
    setProp("Z:", propZ);
    horizontalWidthHint += m_componentLayout->spacing() * 2;
    m_horizontalWidthHint = horizontalWidthHint;
    setLayout(m_componentLayout);
    updateResponsiveLayout(width());
    QTimer::singleShot(0, this, [this]()
    {
        updateResponsiveLayout(width());
        syncTableRowHeight();
    });
}

void C3DItem::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout(event->size().width());
}

void C3DItem::updateResponsiveLayout(int availableWidth)
{
    const bool horizontal = availableWidth >= m_horizontalWidthHint;
    if(horizontal != m_horizontalLayout)
    {
        m_horizontalLayout = horizontal;
        m_componentLayout->setDirection(horizontal
            ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        updateGeometry();
        QTimer::singleShot(0, this, [this]() { syncTableRowHeight(); });
    }
    for(CValueItem* editor : m_componentEditors)
        editor->setCompactDisplay(horizontal);
}

void C3DItem::syncTableRowHeight()
{
    QWidget* ancestor = parentWidget();
    while(ancestor && !qobject_cast<QTableWidget*>(ancestor))
        ancestor = ancestor->parentWidget();
    QTableWidget* table = qobject_cast<QTableWidget*>(ancestor);
    if(!table)
        return;
    for(int row = 0; row < table->rowCount(); ++row)
        if(table->cellWidget(row, 1) == this)
        {
            const int desired = qMax(table->verticalHeader()->minimumSectionSize(),
                                     sizeHint().height() + 2);
            if(table->rowHeight(row) != desired)
                table->setRowHeight(row, desired);
            return;
        }
}

void C3DItem::keyPressEvent(QKeyEvent *event)
{

    if(event->matches(QKeySequence::Copy))
    {
        if(!m_xValue->isInit() || !m_yValue->isInit() || !m_zValue->isInit())
            return;

        auto val = QString("%1, %2, %3").arg(m_xValue->toString(), m_yValue->toString(), m_zValue->toString());
        const QVector3D vector(m_xValue->toString().toFloat(), m_yValue->toString().toFloat(),
                               m_zValue->toString().toFloat());
        QSharedPointer<IPropertyBase> aggregate(new prop3D(EObjParam(m_xValue->type() + 3), vector));
        copyProperty(aggregate, val);
        event->accept();
        return;
    }
    if((event->matches(QKeySequence::Paste)))
    {
        if(property("enduranceReadOnly").toBool())
        {
            QApplication::beep();
            event->accept();
            return;
        }
        QClipboard *pClipboard = QGuiApplication::clipboard();
        QString val = pClipboard->text();
        if(rejectIncompatiblePaste(this, EObjParam(m_xValue->type() + 3), val))
        {
            event->accept();
            return;
        }
        QRegExp re(R"((\s*-?\d+(\.\d+)?\,){2}(\s*-?\d+(\.\d+)?))");
        if(re.exactMatch(val))
        {
            QStringList list = val.split(", ");
            m_xValue->resetFromString(list[0]);
            m_yValue->resetFromString(list[1]);
            m_zValue->resetFromString(list[2]);
            //TODO: send 1 event for x,y,z components (x+3)
            emit onParamChange(m_xValue);
            emit onParamChange(m_yValue);
            emit onParamChange(m_zValue);
        }
        event->accept();
        return;
    }
}

void C3DItem::_onParamChange(const QSharedPointer<IPropertyBase> &prop)
{
    if(prop.isNull())
        return;
    if(prop->type() == m_xValue->type())
        m_xValue = clonePropertyValue(prop);
    else if(prop->type() == m_yValue->type())
        m_yValue = clonePropertyValue(prop);
    else if(prop->type() == m_zValue->type())
        m_zValue = clonePropertyValue(prop);
    emit onParamChange(prop);
}

void CLineEditEventFilter::restoreValue()
{
    auto pLine = reinterpret_cast<CValueItem*>(parent());
    pLine->setText(m_value);
    pLine->skipNextCheck();
}

CMultiLineButtonItem::CMultiLineButtonItem(const QSharedPointer<IPropertyBase> &prop)
{
    setFocusPolicy(Qt::StrongFocus);
    if(prop->isInit())
    {
        m_pValue.reset(prop->clone());
        QString text = dynamic_cast<propStrAr*>(m_pValue.get())->value().join(' ');
        if (text.isEmpty())
            text = "<empty>";
        else if (text.length() > 23)
        {
            text = text.mid(0, 22);
            text += "...";
        }
        setText(text);
    }
    else
    {
        QStringList list;
        m_pValue.reset(new propStrAr(prop->type(), list)); //todo: display value dif
        setText(tr("undefined/different"));
    }
    QObject::connect(this, SIGNAL(clicked()), this, SLOT(onTextEditOpen()));
}

void CMultiLineButtonItem::keyPressEvent(QKeyEvent* event)
{
    if(event->matches(QKeySequence::Copy))
    {
        const QString text = dynamic_cast<propStrAr*>(m_pValue.get())->value().join('\n');
        copyProperty(m_pValue, text);
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste))
    {
        const QString text = QGuiApplication::clipboard()->text();
        if(rejectIncompatiblePaste(this, m_pValue->type(), text))
        {
            event->accept();
            return;
        }
        onTextEdit(text);
        event->accept();
        return;
    }
    QToolButton::keyPressEvent(event);
}

void CMultiLineButtonItem::onTextEditOpen()
{
    if(m_pTextForm.isNull())
    {
        m_pTextForm.reset(new CMultiLineEditForm());
        QObject::connect(m_pTextForm.get(), SIGNAL(onTextApplySignal(QString)), this, SLOT(onTextEdit(QString)));
    }

    QStringList suggestions;
    const auto database = ei::ProfileDatabaseContext::instance().current();
    if(!database.isNull())
    {
        switch(m_pValue->type())
        {
        case eObjParam_UNIT_WEAPONS: suggestions = database->weapons; break;
        case eObjParam_UNIT_ARMORS: suggestions = database->armor; break;
        case eObjParam_UNIT_SPELLS: suggestions = database->spells; break;
        case eObjParam_UNIT_QUICK_ITEMS: suggestions = database->quickItems; break;
        case eObjParam_UNIT_QUEST_ITEMS: suggestions = database->questItems; break;
        default: break;
        }
    }
    m_pTextForm->setSuggestions(suggestions,
        m_pValue->type() == eObjParam_UNIT_WEAPONS || m_pValue->type() == eObjParam_UNIT_ARMORS
        || m_pValue->type() == eObjParam_UNIT_QUICK_ITEMS || m_pValue->type() == eObjParam_UNIT_QUEST_ITEMS);

    if(m_pValue->isInit())
    {
        const auto& val = dynamic_cast<propStrAr*>(m_pValue.get())->value();
        QString text = val.join('\n');
        m_pTextForm->setText(text);
    }

    m_pTextForm->show();
}

void CMultiLineButtonItem::onTextEdit(QString str)
{
    QStringList list = str.split('\n');
    QSharedPointer<propStrAr> prop(new propStrAr(m_pValue->type(), list));
    if(m_pValue->isEqual(prop.get()))
        return;

    m_pValue.reset(prop->clone());
    emit onParamChange(m_pValue);
}

CUnitStatItem::CUnitStatItem(const QSharedPointer<IPropertyBase> &prop)
{
    setFocusPolicy(Qt::StrongFocus);
    setText(tr("Edit unit parameters"));
    if(prop->type() != eObjParam_UNIT_STATS)
    {
        Q_ASSERT(false && "check caller");
        return;
    }
    m_pValue.reset(prop->clone());
    QObject::connect(this, SIGNAL(clicked()), this, SLOT(onStatEditOpen()));

}

void CUnitStatItem::keyPressEvent(QKeyEvent* event)
{
    if(event->matches(QKeySequence::Copy))
    {
        copyProperty(m_pValue, m_pValue->toString());
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste))
    {
        if(!clipboardMatches(m_pValue->type()) || g_propertyClipboard.size() != 1
           || g_propertyClipboard.first()->type() != m_pValue->type())
        {
            rejectIncompatiblePaste(this, m_pValue->type(), QGuiApplication::clipboard()->text());
            event->accept();
            return;
        }
        m_pValue = clonePropertyForPaste(g_propertyClipboard.first());
        emit onParamChange(m_pValue);
        event->accept();
        return;
    }
    QToolButton::keyPressEvent(event);
}

void CUnitStatItem::onStatEditOpen()
{
    if(m_pTextForm.isNull())
    {
        m_pTextForm.reset(new CUnitStatForm());
        QObject::connect(m_pTextForm.get(), SIGNAL(onApplyChangesSignal(QSharedPointer<IPropertyBase>)), this, SLOT(_onParamChange(QSharedPointer<IPropertyBase>)));
    }
    m_pTextForm->setStat(m_pValue);
    m_pTextForm->show();
}

void CUnitStatItem::_onParamChange(const QSharedPointer<IPropertyBase>& prop)
{
    m_pValue.reset(prop->clone());
    emit onParamChange(prop);
}

CBodyPartItem::CBodyPartItem(const QSharedPointer<IPropertyBase>& prop)
{
    setFocusPolicy(Qt::StrongFocus);
    setText(tr("Edit model parts"));
    m_pValue.reset(prop->clone());
    QObject::connect(this, SIGNAL(clicked()), this, SLOT(onPartEditOpen()));
}

void CBodyPartItem::keyPressEvent(QKeyEvent* event)
{
    if(event->matches(QKeySequence::Copy))
    {
        copyProperty(m_pValue, m_pValue->toString());
        event->accept();
        return;
    }
    if(event->matches(QKeySequence::Paste))
    {
        if(!clipboardMatches(m_pValue->type()) || g_propertyClipboard.size() != 1
           || g_propertyClipboard.first()->type() != m_pValue->type())
        {
            rejectIncompatiblePaste(this, m_pValue->type(), QGuiApplication::clipboard()->text());
            event->accept();
            return;
        }
        m_pValue = clonePropertyForPaste(g_propertyClipboard.first());
        emit onParamChange(m_pValue);
        event->accept();
        return;
    }
    QToolButton::keyPressEvent(event);
}

void CBodyPartItem::onPartEditOpen()
{
    if(m_pPartForm.isNull())
    {
        m_pPartForm.reset(new CBodyPartEditForm());
        QObject::connect(m_pPartForm.get(), SIGNAL(onApplyChangesSignal(QSharedPointer<IPropertyBase>)), this, SLOT(_onParamChange(QSharedPointer<IPropertyBase>)));
    }
    m_pPartForm->setPartData(m_pValue);
    m_pPartForm->show();
}

void CBodyPartItem::_onParamChange(const QSharedPointer<IPropertyBase>& prop)
{
    m_pValue.reset(prop->clone());
    emit onParamChange(prop);
}

CTrapTargetItem::CTrapTargetItem(const QSharedPointer<IPropertyBase>& prop)
{
    setFocusPolicy(Qt::StrongFocus);
    m_pValue = prop.isNull()
        ? QSharedPointer<IPropertyBase>()
        : QSharedPointer<IPropertyBase>(prop->clone());
    const propTrapTargets* targets = dynamic_cast<const propTrapTargets*>(m_pValue.get());
    if(!targets || !targets->isInit())
        setText(tr("View mixed targets"));
    else if(!targets->value().available)
        setText(tr("Targets unavailable"));
    else
        setText(tr("View targets (%1)").arg(targets->value().labels.size()));
    connect(this, &QToolButton::clicked, this, &CTrapTargetItem::onTargetViewOpen);
}

void CTrapTargetItem::onTargetViewOpen()
{
    const propTrapTargets* targets = dynamic_cast<const propTrapTargets*>(m_pValue.get());
    if(m_pTargetDialog.isNull())
    {
        m_pTargetDialog.reset(new QDialog());
        m_pTargetDialog->setObjectName(QStringLiteral("trapTargetDialog"));
        m_pTargetDialog->setAttribute(Qt::WA_QuitOnClose, false);
        m_pTargetDialog->setWindowTitle(tr("Trap targets"));
        QVBoxLayout* layout = new QVBoxLayout(m_pTargetDialog.get());

        if(!targets || !targets->isInit())
            layout->addWidget(new QLabel(tr("Selected traps have different target sets."),
                                         m_pTargetDialog.get()));
        else if(!targets->value().available)
            layout->addWidget(new QLabel(targets->value().display, m_pTargetDialog.get()));
        else
        {
            layout->addWidget(new QLabel(
                tr("Player group %1 attacks factions marked War in its diplomacy row.")
                    .arg(targets->value().playerGroup), m_pTargetDialog.get()));
            const QStringList labels = targets->value().labels;
            if(labels.isEmpty())
                layout->addWidget(new QLabel(tr("No factions are marked War."),
                                             m_pTargetDialog.get()));
            else
            {
                QTableWidget* table = new QTableWidget(1, labels.size(),
                                                       m_pTargetDialog.get());
                table->setObjectName(QStringLiteral("trapTargetTable"));
                table->setHorizontalHeaderLabels(labels);
                table->setVerticalHeaderLabels({tr("Will attack")});
                table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                table->setSelectionMode(QAbstractItemView::NoSelection);
                for(int column = 0; column < labels.size(); ++column)
                {
                    QTableWidgetItem* war = new QTableWidgetItem(tr("War"));
                    war->setTextAlignment(Qt::AlignCenter);
                    table->setItem(0, column, war);
                }
                table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
                table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
                table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
                layout->addWidget(table);
            }
        }

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close,
                                                         m_pTargetDialog.get());
        connect(buttons, &QDialogButtonBox::rejected, m_pTargetDialog.get(), &QDialog::close);
        layout->addWidget(buttons);
    }
    m_pTargetDialog->show();
    m_pTargetDialog->raise();
    m_pTargetDialog->activateWindow();
}

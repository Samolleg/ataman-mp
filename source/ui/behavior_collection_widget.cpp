#include "behavior_collection_widget.h"

#include <functional>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "objects/unit.h"

namespace {
QSet<const CPatrolPoint*> g_expandedPatrols;
const int PointRole = Qt::UserRole;
const int PatrolIndexRole = Qt::UserRole + 1;
const int LookIndexRole = Qt::UserRole + 2;
const int LookPointRole = Qt::UserRole + 3;

QColor worldSelectionBackground()
{
    return QColor(82, 58, 18);
}

QColor worldSelectionForeground()
{
    return QColor(255, 220, 128);
}

void markWorldSelection(QTreeWidgetItem* item, CNode* point)
{
    if(!item || !point || point->nodeState() != ENodeState::eSelect)
        return;
    QFont selectedFont = item->font(0);
    selectedFont.setBold(true);
    for(int column = 0; column < item->columnCount(); ++column)
    {
        item->setBackground(column, QBrush(worldSelectionBackground()));
        item->setForeground(column, QBrush(worldSelectionForeground()));
        item->setFont(column, selectedFont);
    }
    item->setToolTip(0, QObject::tr(
        "Gold indicates the point currently selected in the viewport and outliner."));
}

class CBehaviorCollectionTree final : public QTreeWidget
{
public:
    using MoveRequest = std::function<void(bool, int, int, int)>;

    explicit CBehaviorCollectionTree(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {
    }

    void setMoveRequest(const MoveRequest& request)
    {
        m_moveRequest = request;
    }

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        m_dragItem = currentItem();
        if(!m_dragItem)
            return;
        m_dragLookPoint = m_dragItem->data(0, LookPointRole).toBool();
        if((m_dragLookPoint && !m_dragItem->parent())
           || (!m_dragLookPoint && m_dragItem->parent()))
        {
            m_dragItem = nullptr;
            return;
        }
        m_dragParent = m_dragItem->parent();
        m_originalOrder = siblings();
        m_originalIndex = m_originalOrder.indexOf(m_dragItem);
        m_previewIndex = m_originalIndex;
        m_dropCommitted = false;
        QTreeWidget::startDrag(supportedActions);
        if(!m_dropCommitted)
            restoreOriginalOrder();
        m_dragItem = nullptr;
        m_dragParent = nullptr;
        m_originalOrder.clear();
        m_originalIndex = -1;
        m_previewIndex = -1;
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if(m_dragItem)
        {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        }
        else
            event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if(!m_dragItem || !event)
            return;
        QTreeWidgetItem* target = normalizedTarget(itemAt(event->pos()));
        bool after = false;
        if(!target)
        {
            const QVector<QTreeWidgetItem*> current = siblings();
            if(current.isEmpty())
            {
                event->ignore();
                return;
            }
            QTreeWidgetItem* first = current.first();
            QTreeWidgetItem* last = current.last();
            if(event->pos().y() < visualItemRect(first).top())
                target = first;
            else if(event->pos().y() > visualItemRect(last).bottom())
            {
                target = last;
                after = true;
            }
            else
            {
                event->ignore();
                return;
            }
        }
        else
            after = event->pos().y() >= visualItemRect(target).center().y();

        if(!sameCollection(target))
        {
            event->ignore();
            return;
        }
        previewOrder(target, after);
        const int edge = 24;
        if(event->pos().y() < edge)
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
        else if(event->pos().y() > viewport()->height() - edge)
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        restoreOriginalOrder();
        event->accept();
    }

    void dropEvent(QDropEvent* event) override
    {
        if(!requestDropMove(event))
        {
            restoreOriginalOrder();
            event->ignore();
        }
    }

private:
    QVector<QTreeWidgetItem*> siblings() const
    {
        QVector<QTreeWidgetItem*> result;
        const int count = m_dragParent ? m_dragParent->childCount()
                                       : topLevelItemCount();
        result.reserve(count);
        for(int index = 0; index < count; ++index)
            result.append(m_dragParent ? m_dragParent->child(index)
                                       : topLevelItem(index));
        return result;
    }

    QTreeWidgetItem* normalizedTarget(QTreeWidgetItem* target) const
    {
        if(!target || !m_dragItem)
            return nullptr;
        if(!m_dragLookPoint && target->parent())
            target = target->parent();
        return target;
    }

    bool sameCollection(QTreeWidgetItem* target) const
    {
        if(!target || target == m_dragItem
           || target->data(0, LookPointRole).toBool() != m_dragLookPoint)
            return target == m_dragItem;
        return m_dragLookPoint ? target->parent() == m_dragParent
                               : target->parent() == nullptr;
    }

    void movePreviewItem(int from, int to)
    {
        if(from == to || from < 0 || to < 0)
            return;
        QTreeWidgetItem* item = m_dragParent
            ? m_dragParent->takeChild(from) : takeTopLevelItem(from);
        if(!item)
            return;
        if(m_dragParent)
            m_dragParent->insertChild(to, item);
        else
            insertTopLevelItem(to, item);
        setCurrentItem(item);
    }

    void previewOrder(QTreeWidgetItem* target, bool after)
    {
        const QVector<QTreeWidgetItem*> current = siblings();
        const int sourceIndex = current.indexOf(m_dragItem);
        const int targetIndex = current.indexOf(target);
        if(sourceIndex < 0 || targetIndex < 0)
            return;
        int insertion = targetIndex + (after ? 1 : 0);
        if(sourceIndex < insertion)
            --insertion;
        const int finalIndex = qBound(0, insertion, current.size() - 1);
        if(finalIndex == m_previewIndex)
            return;
        movePreviewItem(sourceIndex, finalIndex);
        m_previewIndex = finalIndex;
        updatePreviewLabels();
        viewport()->update();
    }

    void updatePreviewLabels()
    {
        const auto updateLooks = [](QTreeWidgetItem* patrol, int patrolIndex)
        {
            if(!patrol)
                return;
            for(int look = 0; look < patrol->childCount(); ++look)
            {
                QTreeWidgetItem* item = patrol->child(look);
                item->setText(0, QStringLiteral("%1.%2").arg(patrolIndex).arg(look));
                item->setText(1, QObject::tr("Look %1").arg(look));
            }
        };
        if(m_dragLookPoint)
        {
            updateLooks(m_dragParent, indexOfTopLevelItem(m_dragParent));
            return;
        }
        for(int patrol = 0; patrol < topLevelItemCount(); ++patrol)
        {
            QTreeWidgetItem* item = topLevelItem(patrol);
            item->setText(0, QString::number(patrol));
            item->setText(1, QObject::tr("Patrol %1").arg(patrol));
            updateLooks(item, patrol);
        }
    }

    void restoreOriginalOrder()
    {
        if(!m_dragItem || m_originalOrder.isEmpty()
           || m_previewIndex == m_originalIndex)
            return;
        setUpdatesEnabled(false);
        while(m_dragParent ? m_dragParent->childCount() : topLevelItemCount())
        {
            if(m_dragParent)
                m_dragParent->takeChild(0);
            else
                takeTopLevelItem(0);
        }
        for(QTreeWidgetItem* item : m_originalOrder)
            if(m_dragParent)
                m_dragParent->addChild(item);
            else
                addTopLevelItem(item);
        m_previewIndex = m_originalIndex;
        updatePreviewLabels();
        setCurrentItem(m_dragItem);
        setUpdatesEnabled(true);
        viewport()->update();
    }

    bool requestDropMove(QDropEvent* event)
    {
        if(!event || !m_dragItem || !m_moveRequest || m_previewIndex < 0)
            return false;
        if(m_previewIndex != m_originalIndex)
        {
            const QVector<QTreeWidgetItem*> current = siblings();
            const int neighbourIndex = m_previewIndex < m_originalIndex
                ? m_previewIndex + 1 : m_previewIndex - 1;
            QTreeWidgetItem* neighbour = current.value(neighbourIndex);
            if(!neighbour)
                return false;
            const int from = m_dragLookPoint
                ? m_dragItem->data(0, LookIndexRole).toInt()
                : m_dragItem->data(0, PatrolIndexRole).toInt();
            const int to = m_dragLookPoint
                ? neighbour->data(0, LookIndexRole).toInt()
                : neighbour->data(0, PatrolIndexRole).toInt();
            const int patrol = m_dragItem->data(0, PatrolIndexRole).toInt();
            m_moveRequest(m_dragLookPoint, patrol, from, to);
        }
        m_dropCommitted = true;
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return true;
    }

    MoveRequest m_moveRequest;
    QTreeWidgetItem* m_dragItem{nullptr};
    QTreeWidgetItem* m_dragParent{nullptr};
    QVector<QTreeWidgetItem*> m_originalOrder;
    int m_originalIndex{-1};
    int m_previewIndex{-1};
    bool m_dragLookPoint{false};
    bool m_dropCommitted{false};
};

QString positionText(CNode* point)
{
    if(!point)
        return QString();
    const QVector3D pos = point->position();
    return QStringLiteral("%1, %2, %3")
        .arg(pos.x(), 0, 'f', 1).arg(pos.y(), 0, 'f', 1).arg(pos.z(), 0, 'f', 1);
}

QToolButton* toolButton(QWidget* parent, const QString& text,
                        const QString& tooltip)
{
    QToolButton* button = new QToolButton(parent);
    button->setText(text);
    button->setAutoRaise(true);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}
}

CBehaviorCollectionWidget::CBehaviorCollectionWidget(
        CUnit* unit, int logicSlot, QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 5, 4, 5);
    layout->setSpacing(5);

    CLogic* logic = unit ? unit->logicAt(logicSlot) : nullptr;
    int patrolCount = 0;
    int lookCount = 0;
    if(logic)
        for(CPatrolPoint* patrol : logic->patrolPoints())
            if(patrol && !patrol->isMarkDeleted())
            {
                ++patrolCount;
                for(CLookPoint* look : patrol->lookPoint())
                    if(look && !look->isMarkDeleted())
                        ++lookCount;
            }

    QHBoxLayout* heading = new QHBoxLayout;
    QLabel* title = new QLabel(tr("Patrol route — Logic %1 — %2 patrols — %3 looks")
                               .arg(logicSlot).arg(patrolCount).arg(lookCount), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    heading->addWidget(title, 1);
    QPushButton* addPatrol = new QPushButton(tr("+ Add patrol"), this);
    addPatrol->setToolTip(tr("Append a patrol point to this Logic slot."));
    connect(addPatrol, &QPushButton::clicked, this, &CBehaviorCollectionWidget::addPatrolPoint);
    heading->addWidget(addPatrol);
    layout->addLayout(heading);

    CBehaviorCollectionTree* collectionTree = new CBehaviorCollectionTree(this);
    m_tree = collectionTree;
    m_tree->setObjectName(QStringLiteral("behaviorCollectionTree"));
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({tr("#"), tr("Point"), tr("Position"), tr("Children"), tr("Order / select")});
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(false);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tree->setToolTip(tr(
        "Drag patrols to reorder the route, or drag look points within one patrol.\n"
        "The top half of a row places the point before it; the bottom half places it after.\n"
        "The complete order previews while dragging; leaving the list or pressing Esc cancels it.\n"
        "Gold indicates the point selected in the viewport/outliner; clicking another row only focuses it here."));
    collectionTree->setMoveRequest(
        [this](bool lookPoint, int patrol, int from, int to)
        {
            if(lookPoint) emit moveLookPoint(patrol, from, to);
            else emit movePatrolPoint(from, to);
        });
    layout->addWidget(m_tree, 1);

    if(!logic)
        return;

    QVector<int> visiblePatrols;
    for(int patrol = 0; patrol < logic->patrolPoints().size(); ++patrol)
        if(CPatrolPoint* point = logic->patrolPoints().at(patrol))
            if(!point->isMarkDeleted())
                visiblePatrols.append(patrol);

    for(int visible = 0; visible < visiblePatrols.size(); ++visible)
    {
        const int patrolIndex = visiblePatrols.at(visible);
        CPatrolPoint* patrol = logic->patrolPoints().at(patrolIndex);
        int visibleLooks = 0;
        QVector<int> lookIndices;
        for(int look = 0; look < patrol->lookPoint().size(); ++look)
            if(CLookPoint* point = patrol->lookPoint().at(look))
                if(!point->isMarkDeleted())
                {
                    ++visibleLooks;
                    lookIndices.append(look);
                }

        QTreeWidgetItem* patrolItem = new QTreeWidgetItem(m_tree);
        patrolItem->setText(0, QString::number(visible));
        patrolItem->setText(1, tr("Patrol %1").arg(visible));
        patrolItem->setText(2, positionText(patrol));
        patrolItem->setText(3, tr("%1 look(s)").arg(visibleLooks));
        patrolItem->setData(0, PointRole, QVariant::fromValue<qulonglong>(
                                reinterpret_cast<quintptr>(patrol)));
        patrolItem->setData(0, PatrolIndexRole, patrolIndex);
        patrolItem->setData(0, LookIndexRole, -1);
        patrolItem->setData(0, LookPointRole, false);
        markWorldSelection(patrolItem, patrol);
        const int previousPatrol = visible > 0 ? visiblePatrols.at(visible - 1) : -1;
        const int nextPatrol = visible + 1 < visiblePatrols.size()
            ? visiblePatrols.at(visible + 1) : -1;
        m_tree->setItemWidget(patrolItem, 4,
            actionWidget(patrol, patrolIndex, patrolIndex,
                         previousPatrol, nextPatrol, false));

        for(int lookVisible = 0; lookVisible < lookIndices.size(); ++lookVisible)
        {
            const int lookIndex = lookIndices.at(lookVisible);
            CLookPoint* look = patrol->lookPoint().at(lookIndex);
            QTreeWidgetItem* lookItem = new QTreeWidgetItem(patrolItem);
            lookItem->setText(0, QStringLiteral("%1.%2").arg(visible).arg(lookVisible));
            lookItem->setText(1, tr("Look %1").arg(lookVisible));
            lookItem->setText(2, positionText(look));
            lookItem->setText(3, QStringLiteral("—"));
            lookItem->setData(0, PointRole, QVariant::fromValue<qulonglong>(
                                  reinterpret_cast<quintptr>(look)));
            lookItem->setData(0, PatrolIndexRole, patrolIndex);
            lookItem->setData(0, LookIndexRole, lookIndex);
            lookItem->setData(0, LookPointRole, true);
            markWorldSelection(lookItem, look);
            const int previousLook = lookVisible > 0 ? lookIndices.at(lookVisible - 1) : -1;
            const int nextLook = lookVisible + 1 < lookIndices.size()
                ? lookIndices.at(lookVisible + 1) : -1;
            m_tree->setItemWidget(lookItem, 4,
                actionWidget(look, patrolIndex, lookIndex,
                             previousLook, nextLook, true));
        }
        patrolItem->setExpanded(g_expandedPatrols.contains(patrol) || visible == 0);
    }

    connect(m_tree, &QTreeWidget::itemExpanded, this, [](QTreeWidgetItem* item)
    {
        CNode* point = reinterpret_cast<CNode*>(quintptr(item->data(0, PointRole).toULongLong()));
        if(CPatrolPoint* patrol = dynamic_cast<CPatrolPoint*>(point))
            g_expandedPatrols.insert(patrol);
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [](QTreeWidgetItem* item)
    {
        CNode* point = reinterpret_cast<CNode*>(quintptr(item->data(0, PointRole).toULongLong()));
        if(CPatrolPoint* patrol = dynamic_cast<CPatrolPoint*>(point))
            g_expandedPatrols.remove(patrol);
    });
}

QWidget* CBehaviorCollectionWidget::actionWidget(
        CNode* point, int patrol, int index, int previous, int next, bool lookPoint)
{
    QWidget* container = new QWidget(m_tree);
    QHBoxLayout* actions = new QHBoxLayout(container);
    actions->setContentsMargins(1, 0, 1, 0);
    actions->setSpacing(1);

    QToolButton* up = toolButton(container, QStringLiteral("↑"),
        lookPoint ? tr("Move this look point earlier within its patrol.")
                  : tr("Move this patrol point earlier in the route."));
    up->setEnabled(previous >= 0);
    connect(up, &QToolButton::clicked, this, [this, patrol, index, previous, lookPoint]()
    {
        if(lookPoint) emit moveLookPoint(patrol, index, previous);
        else emit movePatrolPoint(index, previous);
    });
    actions->addWidget(up);

    QToolButton* down = toolButton(container, QStringLiteral("↓"),
        lookPoint ? tr("Move this look point later within its patrol.")
                  : tr("Move this patrol point later in the route."));
    down->setEnabled(next >= 0);
    connect(down, &QToolButton::clicked, this, [this, patrol, index, next, lookPoint]()
    {
        if(lookPoint) emit moveLookPoint(patrol, index, next);
        else emit movePatrolPoint(index, next);
    });
    actions->addWidget(down);

    if(!lookPoint)
    {
        QToolButton* addLook = toolButton(container, QStringLiteral("+"),
                                          tr("Add a look point to this patrol."));
        connect(addLook, &QToolButton::clicked, this,
                [this, patrol]() { emit addLookPoint(patrol); });
        actions->addWidget(addLook);
    }

    QToolButton* aim = toolButton(container, QStringLiteral("◎"),
                                  tr("Select in outliner and viewport"));
    connect(aim, &QToolButton::clicked, this,
            [this, point]() { emit selectPoint(point); });
    actions->addWidget(aim);
    return container;
}

#include "behavior_collection_widget.h"

#include <functional>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
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
    void dropEvent(QDropEvent* event) override
    {
        if(!requestDropMove(event))
            event->ignore();
    }

private:
    bool requestDropMove(QDropEvent* event)
    {
        QTreeWidgetItem* source = currentItem();
        QTreeWidgetItem* target = event ? itemAt(event->pos()) : nullptr;
        if(!source || !target || source == target || !m_moveRequest)
            return false;

        const bool lookPoint = source->data(0, LookPointRole).toBool();
        if(!lookPoint && target->parent())
            target = target->parent();
        if(source == target || target->data(0, LookPointRole).toBool() != lookPoint)
            return false;

        const int patrol = source->data(0, PatrolIndexRole).toInt();
        if(lookPoint && (source->parent() != target->parent()
                         || patrol != target->data(0, PatrolIndexRole).toInt()))
            return false;

        const int from = lookPoint
            ? source->data(0, LookIndexRole).toInt()
            : patrol;
        const int targetIndex = lookPoint
            ? target->data(0, LookIndexRole).toInt()
            : target->data(0, PatrolIndexRole).toInt();
        bool below = dropIndicatorPosition() == QAbstractItemView::BelowItem;
        if(dropIndicatorPosition() == QAbstractItemView::OnItem)
            below = event->pos().y() >= visualItemRect(target).center().y();
        if(dropIndicatorPosition() == QAbstractItemView::OnViewport)
            return false;

        int to = targetIndex;
        if(below && from > targetIndex)
            ++to;
        else if(!below && from < targetIndex)
            --to;
        if(from == to)
        {
            event->acceptProposedAction();
            return true;
        }

        m_moveRequest(lookPoint, patrol, from, to);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return true;
    }

    MoveRequest m_moveRequest;
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
    m_tree->setDropIndicatorShown(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tree->setToolTip(tr("Drag patrols to reorder the route, or drag look points within one patrol. "
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

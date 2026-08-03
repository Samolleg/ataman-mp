#include "trap_collection_widget.h"

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
#include <QScrollBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "objects/magictrap.h"

namespace {
const int PointRole = Qt::UserRole;
const int PointIndexRole = Qt::UserRole + 1;
const int CastTargetRole = Qt::UserRole + 2;

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

class CTrapCollectionTree final : public QTreeWidget
{
public:
    using MoveRequest = std::function<void(bool, int, int)>;

    explicit CTrapCollectionTree(QWidget* parent = nullptr)
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
        if(!m_dragItem || !m_dragItem->parent())
        {
            m_dragItem = nullptr;
            return;
        }
        m_dragParent = m_dragItem->parent();
        m_castTarget = m_dragItem->data(0, CastTargetRole).toBool();
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
        QTreeWidgetItem* target = itemAt(event->pos());
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
        if(!target->parent() || target->parent() != m_dragParent
           || target->data(0, CastTargetRole).toBool() != m_castTarget)
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
        if(!m_dragParent)
            return result;
        result.reserve(m_dragParent->childCount());
        for(int index = 0; index < m_dragParent->childCount(); ++index)
            result.append(m_dragParent->child(index));
        return result;
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
        QTreeWidgetItem* item = m_dragParent->takeChild(sourceIndex);
        if(!item)
            return;
        m_dragParent->insertChild(finalIndex, item);
        setCurrentItem(item);
        m_previewIndex = finalIndex;
        updatePreviewLabels();
        viewport()->update();
    }

    void updatePreviewLabels()
    {
        if(!m_dragParent)
            return;
        for(int index = 0; index < m_dragParent->childCount(); ++index)
        {
            QTreeWidgetItem* item = m_dragParent->child(index);
            item->setText(0, QString::number(index));
            item->setText(1, m_castTarget
                ? QObject::tr("Cast target %1").arg(index)
                : QObject::tr("Activation area %1").arg(index));
        }
    }

    void restoreOriginalOrder()
    {
        if(!m_dragItem || !m_dragParent || m_originalOrder.isEmpty()
           || m_previewIndex == m_originalIndex)
            return;
        setUpdatesEnabled(false);
        while(m_dragParent->childCount())
            m_dragParent->takeChild(0);
        for(QTreeWidgetItem* item : m_originalOrder)
            m_dragParent->addChild(item);
        m_previewIndex = m_originalIndex;
        updatePreviewLabels();
        setCurrentItem(m_dragItem);
        setUpdatesEnabled(true);
        viewport()->update();
    }

    bool requestDropMove(QDropEvent* event)
    {
        if(!event || !m_dragItem || !m_dragParent || !m_moveRequest
           || m_previewIndex < 0)
            return false;
        if(m_previewIndex != m_originalIndex)
        {
            const QVector<QTreeWidgetItem*> current = siblings();
            const int neighbourIndex = m_previewIndex < m_originalIndex
                ? m_previewIndex + 1 : m_previewIndex - 1;
            QTreeWidgetItem* neighbour = current.value(neighbourIndex);
            if(!neighbour)
                return false;
            m_moveRequest(m_castTarget,
                          m_dragItem->data(0, PointIndexRole).toInt(),
                          neighbour->data(0, PointIndexRole).toInt());
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
    bool m_castTarget{false};
    bool m_dropCommitted{false};
};

QString positionText(CNode* point)
{
    if(!point)
        return QString();
    const QVector3D pos = point->position();
    return QStringLiteral("%1, %2")
        .arg(pos.x(), 0, 'f', 1).arg(pos.y(), 0, 'f', 1);
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

CTrapCollectionWidget::CTrapCollectionWidget(CMagicTrap* trap, QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 5, 4, 5);
    layout->setSpacing(5);

    int areaCount = 0;
    int targetCount = 0;
    if(trap)
    {
        for(CActivationZone* area : trap->actZones())
            if(area && !area->isMarkDeleted()) ++areaCount;
        for(CTrapCastPoint* target : trap->castPoints())
            if(target && !target->isMarkDeleted()) ++targetCount;
    }

    QHBoxLayout* heading = new QHBoxLayout;
    QLabel* title = new QLabel(
        tr("Magic trap points — %1 activation areas — %2 cast targets")
            .arg(areaCount).arg(targetCount), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    heading->addWidget(title, 1);
    QPushButton* addArea = new QPushButton(tr("+ Activation area"), this);
    addArea->setToolTip(tr("Append an activation area to this trap."));
    connect(addArea, &QPushButton::clicked,
            this, &CTrapCollectionWidget::addActivationArea);
    heading->addWidget(addArea);
    QPushButton* addTarget = new QPushButton(tr("+ Cast target"), this);
    addTarget->setToolTip(tr("Append a cast target to this trap."));
    connect(addTarget, &QPushButton::clicked,
            this, &CTrapCollectionWidget::addCastTarget);
    heading->addWidget(addTarget);
    layout->addLayout(heading);

    CTrapCollectionTree* collectionTree = new CTrapCollectionTree(this);
    m_tree = collectionTree;
    m_tree->setObjectName(QStringLiteral("trapCollectionTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("#"), tr("Point"), tr("Position / radius"),
                             tr("Order / select")});
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
    m_tree->setToolTip(tr(
        "Activation areas and cast targets are independent sibling arrays.\n"
        "Drag within one section to reorder it; the top half of a row places the point before it,\n"
        "and the bottom half places it after. The complete order previews while dragging;\n"
        "leaving the list or pressing Esc cancels it. Gold indicates the point selected\n"
        "in the viewport/outliner; clicking another row only focuses it here."));
    collectionTree->setMoveRequest([this](bool castTarget, int from, int to)
    {
        if(castTarget) emit moveCastTarget(from, to);
        else emit moveActivationArea(from, to);
    });
    layout->addWidget(m_tree, 1);

    if(!trap)
        return;

    QTreeWidgetItem* areas = new QTreeWidgetItem(m_tree);
    areas->setText(0, QString::number(areaCount));
    areas->setText(1, tr("Activation areas"));
    areas->setToolTip(1, tr(
        "Independent circular regions that can activate this trap."));
    areas->setFlags(areas->flags() & ~Qt::ItemIsDragEnabled);
    QVector<int> visibleAreas;
    for(int index = 0; index < trap->actZones().size(); ++index)
        if(CActivationZone* area = trap->actZones().at(index))
            if(!area->isMarkDeleted()) visibleAreas.append(index);
    for(int visible = 0; visible < visibleAreas.size(); ++visible)
    {
        const int index = visibleAreas.at(visible);
        CActivationZone* area = trap->actZones().at(index);
        QTreeWidgetItem* item = new QTreeWidgetItem(areas);
        item->setText(0, QString::number(visible));
        item->setText(1, tr("Activation area %1").arg(visible));
        item->setText(2, tr("%1 — radius %2")
                      .arg(positionText(area)).arg(area->radius(), 0, 'f', 1));
        item->setData(0, PointRole, QVariant::fromValue<qulonglong>(
                          reinterpret_cast<quintptr>(area)));
        item->setData(0, PointIndexRole, index);
        item->setData(0, CastTargetRole, false);
        markWorldSelection(item, area);
        const int previous = visible > 0 ? visibleAreas.at(visible - 1) : -1;
        const int next = visible + 1 < visibleAreas.size()
            ? visibleAreas.at(visible + 1) : -1;
        m_tree->setItemWidget(item, 3,
            actionWidget(area, index, previous, next, false));
    }
    areas->setExpanded(true);

    QTreeWidgetItem* targets = new QTreeWidgetItem(m_tree);
    targets->setText(0, QString::number(targetCount));
    targets->setText(1, tr("Cast targets"));
    targets->setToolTip(1, tr(
        "Independent 2D destination points. The MOB stores no target rotation or height."));
    targets->setFlags(targets->flags() & ~Qt::ItemIsDragEnabled);
    QVector<int> visibleTargets;
    for(int index = 0; index < trap->castPoints().size(); ++index)
        if(CTrapCastPoint* target = trap->castPoints().at(index))
            if(!target->isMarkDeleted()) visibleTargets.append(index);
    for(int visible = 0; visible < visibleTargets.size(); ++visible)
    {
        const int index = visibleTargets.at(visible);
        CTrapCastPoint* target = trap->castPoints().at(index);
        QTreeWidgetItem* item = new QTreeWidgetItem(targets);
        item->setText(0, QString::number(visible));
        item->setText(1, tr("Cast target %1").arg(visible));
        item->setText(2, positionText(target));
        item->setData(0, PointRole, QVariant::fromValue<qulonglong>(
                          reinterpret_cast<quintptr>(target)));
        item->setData(0, PointIndexRole, index);
        item->setData(0, CastTargetRole, true);
        markWorldSelection(item, target);
        const int previous = visible > 0 ? visibleTargets.at(visible - 1) : -1;
        const int next = visible + 1 < visibleTargets.size()
            ? visibleTargets.at(visible + 1) : -1;
        m_tree->setItemWidget(item, 3,
            actionWidget(target, index, previous, next, true));
    }
    targets->setExpanded(true);
}

QWidget* CTrapCollectionWidget::actionWidget(
        CNode* point, int index, int previous, int next, bool castTarget)
{
    QWidget* container = new QWidget(m_tree);
    QHBoxLayout* actions = new QHBoxLayout(container);
    actions->setContentsMargins(1, 0, 1, 0);
    actions->setSpacing(1);

    QToolButton* up = toolButton(container, QStringLiteral("↑"),
        castTarget ? tr("Move this cast target earlier.")
                   : tr("Move this activation area earlier."));
    up->setEnabled(previous >= 0);
    connect(up, &QToolButton::clicked, this,
            [this, index, previous, castTarget]()
    {
        if(castTarget) emit moveCastTarget(index, previous);
        else emit moveActivationArea(index, previous);
    });
    actions->addWidget(up);

    QToolButton* down = toolButton(container, QStringLiteral("↓"),
        castTarget ? tr("Move this cast target later.")
                   : tr("Move this activation area later."));
    down->setEnabled(next >= 0);
    connect(down, &QToolButton::clicked, this,
            [this, index, next, castTarget]()
    {
        if(castTarget) emit moveCastTarget(index, next);
        else emit moveActivationArea(index, next);
    });
    actions->addWidget(down);

    QToolButton* aim = toolButton(container, QStringLiteral("◎"),
                                  tr("Select in outliner and viewport"));
    connect(aim, &QToolButton::clicked, this,
            [this, point]() { emit selectPoint(point); });
    actions->addWidget(aim);
    return container;
}

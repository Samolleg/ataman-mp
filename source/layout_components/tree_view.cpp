#include "tree_view.h"
#include <QKeyEvent>
#include <QDebug>
#include <QApplication>
#include <QKeySequence>
#include "view.h"
#include "node.h"
#include <QHeaderView>
#include <QTimer>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QSignalBlocker>
#include <algorithm>
#include "objects/unit.h"
#include "mob/mob.h"
#include "log.h"

CTreeView::CTreeView(QWidget *parent) : QTreeWidget(parent), m_pView(nullptr),
    m_filterTimer(new QTimer(this))
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(150);
    connect(m_filterTimer, &QTimer::timeout, this, &CTreeView::applyFilter);
    QObject::connect(this, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(itemClickedOver(QTreeWidgetItem*,int)));
    QObject::connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(itemDoubleClickedOver(QTreeWidgetItem*,int)));
}

void CTreeView::prepareForRebuild()
{
    // A delayed filter must never run against QTreeWidgetItems that are about
    // to be destroyed by collectObjectTreeData().
    m_filterTimer->stop();
    m_expansionBeforeFilter.clear();
}

void CTreeView::setValidationNids(const QSet<uint>& critical,
                                  const QSet<uint>& warning)
{
    m_criticalNids = critical;
    m_warningNids = warning;
    if(!m_filterText.trimmed().isEmpty())
        applyFilter();
}

void CTreeView::setFilterText(const QString& text)
{
    const bool wasEmpty = m_filterText.trimmed().isEmpty();
    m_filterText = text;
    if(wasEmpty && !m_filterText.trimmed().isEmpty())
    {
        m_expansionBeforeFilter.clear();
        QTreeWidgetItemIterator it(this);
        while(*it)
        {
            m_expansionBeforeFilter.insert(*it, (*it)->isExpanded());
            ++it;
        }
    }
    m_filterTimer->start();
}

bool CTreeView::textMatches(const QString& candidate, const QString& query,
                            ETextMatchMode mode) const
{
    const QString haystack = candidate.toCaseFolded();
    const QString needle = query.toCaseFolded();
    if(needle.isEmpty())
        return true;
    if(mode == ETextMatchMode::Exact)
        return haystack == needle;
    if(mode == ETextMatchMode::Contains)
        return haystack.contains(needle);
    int at = 0;
    for(const QChar ch : haystack)
        if(at < needle.size() && ch == needle.at(at))
            ++at;
    return at == needle.size();
}

QString CTreeView::validationState(CNode* node) const
{
    if(!node)
        return QString();
    if(m_criticalNids.contains(node->mapId()))
        return "critical";
    if(m_warningNids.contains(node->mapId()))
        return "warning";
    return "clean";
}

bool CTreeView::nodeMatches(CNode* node, const QStringList& terms) const
{
    if(!node)
        return false;
    CUnit* unit = dynamic_cast<CUnit*>(node);
    const QString type = objectNameByType(node->nodeType()) + " "
        + QString::number(int(node->nodeType()));
    const QString database = unit ? unit->databaseName() : QString();
    CMob* owner = m_pView ? m_pView->ownerMob(node) : nullptr;
    const QString mob = owner ? owner->mobName() : QString();
    const QMap<QString, QString> fields{
        {"name", node->mapName()}, {"id", QString::number(node->mapId())},
        {"nid", QString::number(node->mapId())}, {"type", type},
        {"model", node->modelName()}, {"texture", node->textureName()},
        {"db", database}, {"database", database}, {"prototype", database},
        {"mob", mob}, {"validation", validationState(node)}
    };
    const QString everything = QStringList(fields.values()).join(' ');
    for(const QString& rawTerm : terms)
    {
        const int colon = rawTerm.indexOf(':');
        const int equals = rawTerm.indexOf('=');
        const int separator = colon > 0 && equals > 0 ? qMin(colon, equals)
            : qMax(colon, equals);
        if(separator > 0)
        {
            const QString key = rawTerm.left(separator).toCaseFolded();
            QString value = rawTerm.mid(separator + 1);
            if(fields.contains(key))
            {
                ETextMatchMode mode = rawTerm.at(separator) == '='
                    ? ETextMatchMode::Exact : ETextMatchMode::Contains;
                if(mode != ETextMatchMode::Exact && value.startsWith('~'))
                {
                    value.remove(0, 1);
                    mode = ETextMatchMode::Fuzzy;
                }
                if(!textMatches(fields.value(key), value, mode))
                    return false;
                continue;
            }
        }
        QString value = rawTerm;
        ETextMatchMode mode = ETextMatchMode::Contains;
        if(value.startsWith('~'))
        {
            value.remove(0, 1);
            mode = ETextMatchMode::Fuzzy;
        }
        if(!textMatches(everything, value, mode))
            return false;
    }
    return true;
}

bool CTreeView::filterItem(QTreeWidgetItem* item, const QStringList& terms,
                           bool ancestorMatched, int* visible, int* total)
{
    const bool behaviorItem = item->data(0, BehaviorItemRole).toBool();
    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(item);
    CNode* node = objectItem ? objectItem->node() : nullptr;
    if(node && total)
        ++(*total);
    const QString cachedText = item->data(0, FilterTextRole).toString();
    const bool cachedTextMatch = behaviorItem && !terms.isEmpty()
        && std::all_of(terms.begin(), terms.end(), [this, &cachedText](const QString& term)
           {
               QString value = term;
               ETextMatchMode mode = ETextMatchMode::Contains;
               if(value.startsWith('~'))
               {
                   value.remove(0, 1);
                   mode = ETextMatchMode::Fuzzy;
               }
               return !term.contains(':') && !term.contains('=')
                   && textMatches(cachedText, value, mode);
           });
    const bool textualGroupMatch = !node && !terms.isEmpty()
        && std::all_of(terms.begin(), terms.end(), [this, item](const QString& term)
           {
               QString value = term;
               ETextMatchMode mode = ETextMatchMode::Contains;
               if(value.startsWith('~'))
               {
                   value.remove(0, 1);
                   mode = ETextMatchMode::Fuzzy;
               }
               return !term.contains(':') && !term.contains('=')
                   && textMatches(item->text(0), value, mode);
           });
    // If a parent category already matched (for example "unit" matching the
    // Units group), every descendant is visible. Avoid rebuilding expensive
    // object metadata for thousands of children that cannot change the result.
    const bool selfMatches = terms.isEmpty() || ancestorMatched
        // Behavior rows can outlive a logic projection refresh until the next
        // tree rebuild. Their searchable text is cached when the row is built,
        // so filtering never dereferences a possibly stale logic-node pointer.
        || cachedTextMatch
        || (node && !behaviorItem && nodeMatches(node, terms))
        || textualGroupMatch;
    bool childMatches = false;
    for(int index = 0; index < item->childCount(); ++index)
        childMatches = filterItem(item->child(index), terms,
                                  ancestorMatched || selfMatches,
                                  visible, total) || childMatches;
    const bool shown = ancestorMatched || selfMatches || childMatches;
    item->setHidden(!shown);
    if(node && shown && visible)
        ++(*visible);
    if(!terms.isEmpty() && shown && item->childCount() > 0)
        item->setExpanded(true);
    return shown;
}

void CTreeView::applyFilter()
{
    QElapsedTimer elapsed;
    elapsed.start();
    const bool updatesWereEnabled = updatesEnabled();
    setUpdatesEnabled(false);
    QSignalBlocker signalBlocker(this);
    QString normalized = m_filterText.trimmed();
    normalized.replace(QRegularExpression("\\s*([:=])\\s*"), "\\1");
    QStringList terms;
    const QRegularExpression tokenExpression("\\\"([^\\\"]+)\\\"|(\\S+)");
    QRegularExpressionMatchIterator matches = tokenExpression.globalMatch(normalized);
    while(matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        terms.append(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
    }
    int visible = 0;
    int total = 0;
    for(int index = 0; index < topLevelItemCount(); ++index)
        filterItem(topLevelItem(index), terms, false, &visible, &total);
    if(terms.isEmpty() && !m_expansionBeforeFilter.isEmpty())
    {
        for(auto it = m_expansionBeforeFilter.constBegin();
            it != m_expansionBeforeFilter.constEnd(); ++it)
            if(it.key()) it.key()->setExpanded(it.value());
        m_expansionBeforeFilter.clear();
    }
    if(updatesWereEnabled)
        setUpdatesEnabled(true);
    viewport()->update();
    signalBlocker.unblock();
    emit filterMatchCountChanged(visible, total);
    const qint64 elapsedMs = elapsed.elapsed();
    if(elapsedMs >= 100)
        ei::log(eLogInfo, QString("Outliner filter applied elapsedMs:%1 visible:%2 total:%3 query:%4")
                .arg(elapsedMs).arg(visible).arg(total).arg(m_filterText));
}

void CTreeView::keyPressEvent(QKeyEvent *pEvent)
{
    if(pEvent->matches(QKeySequence::Copy))
    {
        m_pView->selectedObjectToClipboardBuffer();
        pEvent->accept();
        return;
    }
    if(pEvent->matches(QKeySequence::Paste))
    {
        m_pView->clipboradObjectsToScene();
        pEvent->accept();
        return;
    }
    auto key = pEvent->nativeVirtualKey();
    if(key == eKey_Delete)
    {
        auto pItem = currentItem();
        if(currentColumn() != 0 || nullptr == pItem) // clicked on count field. ignore it
            return;

        int parentCount = parentDeepCount(pItem);
        if(parentCount > 0)
        {
            m_pView->deleteSelectedNodes();
        }
        clearSelection();
        pEvent->accept();
        return;
    }
    QTreeWidget::keyPressEvent(pEvent);
}

int CTreeView::parentDeepCount(QTreeWidgetItem *pItem)
{
    int parentCount=0;
    auto pParent = pItem->parent();
    while(nullptr != pParent)
    {
        pParent = pParent->parent();
        ++parentCount;
    }
    return parentCount;
}

void CTreeView::recalcParent(QTreeWidgetItem *pItem)
{
    int nChildren = pItem->childCount();
    switch (nChildren)
    {
    case 0:
        if(pItem->parent()) // empty children, not root elem
        {
            auto pTopParent = pItem->parent();
            pTopParent->removeChild(pItem);
            removeItemWidget(pItem, 0);
        }
        break;
    case 1:
        if(pItem->parent()) //not root elem
        {
            //move child elem to parent
            auto pObject = dynamic_cast<CTreeObject*>(pItem);
            auto pChild = dynamic_cast<CTreeObject*>(pItem->child(0));
            pObject->setId(pChild->nodeId());
            pObject->removeChild(pChild);
            removeItemWidget(pChild, 0);
            pObject->setText(1, "1");
        }
        break;
    default:
        pItem->setText(1, QString::number(nChildren));
        break;
    }

}

QTreeWidgetItem *CTreeView::category(QString &categoryName)
{
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        if ((*it)->text(0) == categoryName)
            return (*it);
        ++it;
    }
    return nullptr;
}

QString CTreeView::objectNameByType(ENodeType type) const
{
    QMap<ENodeType, QString> type2str;
    type2str.insert(ENodeType::eWorldObject, "World objects");
    type2str.insert(ENodeType::eUnit, "Units");
    type2str.insert(ENodeType::eTorch, "Torches");
    type2str.insert(ENodeType::eLever, "Levers");
    type2str.insert(ENodeType::eMagicTrap, "Magic traps");
    type2str.insert(ENodeType::eLight, "Light source");
    type2str.insert(ENodeType::eSound, "Sound sources");
    type2str.insert(ENodeType::eParticle, "Particle sources");

    return type2str[type];
}

ENodeType CTreeView::objectTypeByString(QString str) const
{
    QMap<ENodeType, QString> type2str;
    type2str.insert(ENodeType::eWorldObject, "World objects");
    type2str.insert(ENodeType::eUnit, "Units");
    type2str.insert(ENodeType::eTorch, "Torches");
    type2str.insert(ENodeType::eLever, "Levers");
    type2str.insert(ENodeType::eMagicTrap, "Magic traps");
    type2str.insert(ENodeType::eLight, "Light source");
    type2str.insert(ENodeType::eSound, "Sound sources");
    type2str.insert(ENodeType::eParticle, "Particle sources");
    return type2str.key(str);
}

void CTreeView::itemClickedOver(QTreeWidgetItem *pItem, int column)
{
    if(column != 0) // clicked on count field. ignore it
    {
        clearSelection();
        return;
    }

    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(pItem);
    if(!objectItem)
        return;
    QList<CNode*> nodes;
    if(objectItem->node())
        nodes.append(objectItem->node());
    else if(parentDeepCount(pItem) > 0)
    {
        QList<QTreeWidgetItem*> pending;
        pending.append(pItem);
        while(!pending.isEmpty())
        {
            QTreeWidgetItem* next = pending.takeLast();
            if(CTreeObject* childObject = dynamic_cast<CTreeObject*>(next))
                if(childObject->node() && !nodes.contains(childObject->node()))
                {
                    nodes.append(childObject->node());
                    continue;
                }
            for(int index = 0; index < next->childCount(); ++index)
                pending.append(next->child(index));
        }
    }
    int logicSlot = -1;
    for(QTreeWidgetItem* ancestor = pItem; ancestor; ancestor = ancestor->parent())
        if(ancestor->data(0, Qt::UserRole + 10).isValid())
        {
            logicSlot = ancestor->data(0, Qt::UserRole + 10).toInt();
            if(nodes.isEmpty())
            {
                CNode* owner = reinterpret_cast<CNode*>(
                    ancestor->data(0, Qt::UserRole + 11).toULongLong());
                if(owner) nodes.append(owner);
            }
            break;
        }
    if(logicSlot >= 0)
        m_pView->setLogicSlotIndex(logicSlot);
    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
    m_pView->selectNodes(nodes,
        modifiers.testFlag(Qt::ShiftModifier)
        || modifiers.testFlag(Qt::ControlModifier));
}

void CTreeView::onNodeDelete(uint nodeId)
{
    Q_UNUSED(nodeId);
    if(m_pView) m_pView->collectObjectTreeData();
}

void CTreeView::addNodeToTree(CNode *pNode)
{
    Q_UNUSED(pNode);
    if(m_pView)
        m_pView->collectObjectTreeData();
    return;
#if 0
    QString catName = "";
    QString groupName = "";
    ENodeType type = pNode->nodeType();

    switch (type)
    {
    case ENodeType::eTorch:
    case ENodeType::eLever:
    case ENodeType::eMagicTrap:
    case ENodeType::eLight:
    case ENodeType::eSound:
    case ENodeType::eParticle:
    case ENodeType::eWorldObject:
    {
        groupName = pNode->mapName();
        catName = objectNameByType(type);
        break;
    }
    case ENodeType::eUnit:
    {
        catName = objectNameByType(type);
        auto pUnitItem = dynamic_cast<CUnit*>(pNode);
        groupName = pUnitItem->databaseName();
        break;
    }
    default:
    {
        //do not collect nothing for Tree View
        break;
    }
    }

    if(!catName.isEmpty())
    {
        auto pCatItem = category(catName);
        if(pCatItem)
        {
            bool bFound = false;
            for(int i(0); i<pCatItem->childCount(); ++i)
            {
                auto pChild = pCatItem->child(i);
                if(pChild->text(0) == groupName)
                {
                    bFound = true;
                    auto pItem = new CTreeObject(dynamic_cast<CTreeObject*>(pChild), pNode);
                    pItem->setText(0, QString::number(pNode->mapId()));
                    recalcParent(pChild);
                }
            }
            if(!bFound)
            {
                auto pItem = new CTreeObject(dynamic_cast<CTreeObject*>(pCatItem), pNode);
                pItem->setText(0, groupName);
                pItem->setText(1, "1");
                recalcParent(pCatItem);
            }
        }
        clearSelection();
        sortItems(0, Qt::SortOrder::AscendingOrder);
        applyFilter();
    }
#endif
}

void CTreeView::itemDoubleClickedOver(QTreeWidgetItem* pItem, int column)
{
    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(pItem);
    if(!objectItem || !objectItem->node() || column != 0)
        return;
    CNode* node = objectItem->node();
    for(QTreeWidgetItem* ancestor = pItem; ancestor; ancestor = ancestor->parent())
        if(ancestor->data(0, Qt::UserRole + 10).isValid())
        {
            m_pView->setLogicSlotIndex(
                ancestor->data(0, Qt::UserRole + 10).toInt());
            break;
        }
    m_pView->selectNode(node, false, true);
    m_pView->moveCamToSelectedObjects();
}

void CTreeView::revealNode(CNode* pNode)
{
    markNode(pNode, true);
}

void CTreeView::markNode(CNode* pNode, bool scrollToNode)
{
    if(!pNode)
        return;
    QList<CNode*> nodes;
    if(m_pView)
        nodes = m_pView->selectionSnapshot();
    if(!nodes.contains(pNode))
        nodes.append(pNode);
    markNodes(nodes, pNode, scrollToNode);
}

void CTreeView::markNodes(const QList<CNode*>& nodes, CNode* primaryNode,
                          bool scrollToPrimary)
{
    QSet<CNode*> wanted;
    for(CNode* node : nodes)
        if(node) wanted.insert(node);
    if(wanted.isEmpty())
    {
        clearViewportSelectionMarker();
        return;
    }

    clearSelection();
    CTreeObject* primaryItem = nullptr;
    QTreeWidgetItemIterator it(this);
    while(*it)
    {
        CTreeObject* item = dynamic_cast<CTreeObject*>(*it);
        if(item && wanted.contains(item->node()))
        {
            item->setSelected(true);
            for(QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
                parent->setExpanded(true);
            if(item->node() == primaryNode)
                primaryItem = item;
        }
        ++it;
    }
    if(primaryItem)
    {
        setCurrentItem(primaryItem, 0, QItemSelectionModel::NoUpdate);
        if(scrollToPrimary)
            scrollToItem(primaryItem, QAbstractItemView::PositionAtCenter);
    }
    applyFilter();
}

void CTreeView::clearViewportSelectionMarker()
{
    clearSelection();
    setCurrentItem(nullptr);
}

void CTreeView::onChangeNodeId(uint oldId, uint newId)
{
    Q_UNUSED(oldId);
    Q_UNUSED(newId);
    if(m_pView) m_pView->collectObjectTreeData();
    return;
#if 0
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        auto pNode = dynamic_cast<CTreeObject*>(*it);
        if (pNode && pNode->storedId()==oldId)
        {
            pNode->setId(newId);
            if(pNode->text(0) == QString::number(oldId))
                pNode->setText(0, QString::number(newId));
            break;
        }
        ++it;
    }
    sortItems(0, Qt::SortOrder::AscendingOrder);
    applyFilter();
#endif
}

void CTreeView::onChangeObjectName(CNode *pNode)
{
    Q_UNUSED(pNode);
    if(m_pView) m_pView->collectObjectTreeData();
    return;
#if 0
    uint nodeId = pNode->mapId();
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        auto pItem = dynamic_cast<CTreeObject*>(*it);
        if (pItem && pItem->nodeId()==nodeId)
        {
            auto pParent = (*it)->parent();
            pParent->removeChild(*it);
            removeItemWidget(*it, 0);
            recalcParent(pParent);
            break;
        }
        ++it;
    }
    addNodeToTree(pNode);

    //sortItems(0, Qt::SortOrder::AscendingOrder);
#endif
}

CTreeObject::CTreeObject(QTreeWidget *pParent): QTreeWidgetItem(pParent)
  ,m_nodeId(0)
  ,m_pNode(nullptr)
{

}

CTreeObject::CTreeObject(QTreeWidgetItem *pParent, CNode* pNode) : QTreeWidgetItem(pParent)
  ,m_nodeId(pNode ? pNode->mapId() : 0)
  ,m_pNode(pNode)
{

}

CTreeObject::CTreeObject(CTreeObject *pParent, CNode* pNode) : QTreeWidgetItem(pParent)
  ,m_nodeId(pNode ? pNode->mapId() : 0)
  ,m_pNode(pNode)
{
    CNode* parentNode = pParent->node();
    if(parentNode)
    {
        pParent->setId(0);
        pParent->setNode(nullptr);
        auto pItem = new CTreeObject(pParent, parentNode);
        pItem->setText(0, QString::number(parentNode->mapId()));
    }
}

uint CTreeObject::nodeId() const
{
    return m_pNode ? m_pNode->mapId() : m_nodeId;
}

void CTreeObject::setNode(CNode* node)
{
    m_pNode = node;
    m_nodeId = node ? node->mapId() : 0;
}

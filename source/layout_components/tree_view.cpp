#include "tree_view.h"
#include <QKeyEvent>
#include <QDebug>
#include "view.h"
#include "node.h"
#include <QHeaderView>
#include <QTimer>
#include <QRegularExpression>
#include <algorithm>
#include "objects/unit.h"

CTreeView::CTreeView(QWidget *parent) : QTreeWidget(parent), m_pView(nullptr),
    m_filterTimer(new QTimer(this))
{
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(150);
    connect(m_filterTimer, &QTimer::timeout, this, &CTreeView::applyFilter);
    QObject::connect(this, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(itemClickedOver(QTreeWidgetItem*,int)));
    QObject::connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(itemDoubleClickedOver(QTreeWidgetItem*,int)));
}

void CTreeView::prepareForRebuild()
{
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

bool CTreeView::textMatches(const QString& candidate, const QString& query) const
{
    const QString haystack = candidate.toCaseFolded();
    const QString needle = query.toCaseFolded();
    if(needle.isEmpty() || haystack.contains(needle))
        return true;
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
    const QString mob = m_pView && m_pView->currentMob()
        ? m_pView->currentMob()->mobName() : QString();
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
            const QString value = rawTerm.mid(separator + 1);
            if(fields.contains(key))
            {
                if(!textMatches(fields.value(key), value))
                    return false;
                continue;
            }
        }
        if(!textMatches(everything, rawTerm))
            return false;
    }
    return true;
}

bool CTreeView::filterItem(QTreeWidgetItem* item, const QStringList& terms,
                           bool ancestorMatched, int* visible, int* total)
{
    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(item);
    CNode* node = objectItem ? objectItem->node() : nullptr;
    if(node && total)
        ++(*total);
    const bool textualGroupMatch = !node && !terms.isEmpty()
        && std::all_of(terms.begin(), terms.end(), [this, item](const QString& term)
           { return !term.contains(':') && !term.contains('=')
               && textMatches(item->text(0), term); });
    const bool selfMatches = terms.isEmpty() || (node && nodeMatches(node, terms))
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
    emit filterMatchCountChanged(visible, total);
}

void CTreeView::keyPressEvent(QKeyEvent *pEvent)
{
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
    }

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

    int parentCount = parentDeepCount(pItem);

    SSelect sel;
    switch (parentCount)
    {
    case 0:
        return; // for now skip clicking root items
    case 1:
    { // first child
        ENodeType baseType = objectTypeByString(pItem->parent()->text(0));
        sel.type = baseType == ENodeType::eUnit ? eSelectType_Database_name : eSelectType_Map_name;
        sel.param1 = pItem->text(0);
        //sel.param2 = sel.param1; //for hard comparison
        sel.exactMatch = true;
        m_pView->select(sel, false);
        break;
    }
    case 2:
    { // second child
        //ENodeType baseType = objectTypeByString(pItem->parent()->parent()->text(0));
        sel.type = eSelectType_Id_range;
        sel.param1 = pItem->text(0);
        sel.param2 = pItem->text(0);
        m_pView->select(sel, false);
        break;
    }

    }
}

void CTreeView::onNodeDelete(uint nodeId)
{
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        auto pNode = dynamic_cast<CTreeObject*>(*it);
        if (pNode && pNode->nodeId()==nodeId)
        {
            auto pParent = (*it)->parent();
            pParent->removeChild(*it);
            removeItemWidget(*it, 0);
            recalcParent(pParent);
            break;
        }
        ++it;
    }
}

void CTreeView::addNodeToTree(CNode *pNode)
{
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
}

void CTreeView::itemDoubleClickedOver(QTreeWidgetItem* pItem, int column)
{
    if(!pItem || column != 0 || parentDeepCount(pItem) == 0)
        return;
    m_pView->moveCamToSelectedObjects();
}

void CTreeView::revealNode(CNode* pNode)
{
    if(!pNode)
        return;
    QTreeWidgetItemIterator it(this);
    while(*it)
    {
        CTreeObject* item = dynamic_cast<CTreeObject*>(*it);
        if(item && item->node() == pNode)
        {
            for(QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
                parent->setExpanded(true);
            setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect);
            scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return;
        }
        ++it;
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
}

void CTreeView::onChangeObjectName(CNode *pNode)
{
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

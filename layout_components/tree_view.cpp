#include "tree_view.h"
#include <QKeyEvent>
#include <QDebug>
#include "view.h"
#include "node.h"
#include <QHeaderView>
#include "objects/unit.h"

CTreeView::CTreeView(QWidget *parent) : QTreeWidget(parent)
{
    QObject::connect(this, SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(itemClickedOver(QTreeWidgetItem*,int)));
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

QString CTreeView::objectNameByType(ENodeType type)
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

ENodeType CTreeView::objectTypeByString(QString str)
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
        if(pItem->text(1).toInt() == 1)
            m_pView->moveCamToSelectedObjects();
        break;
    }
    case 2:
    { // second child
        //ENodeType baseType = objectTypeByString(pItem->parent()->parent()->text(0));
        sel.type = eSelectType_Id_range;
        sel.param1 = pItem->text(0);
        sel.param2 = pItem->text(0);
        m_pView->select(sel, false);
        m_pView->moveCamToSelectedObjects();
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
    }
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

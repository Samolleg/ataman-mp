#include "undo.h"
#include "node.h"
#include "main_window.h"
#include "mob\mob.h"
#include "objects/unit.h"
#include "objects/magictrap.h"
#include "options.h"
#include "settings.h"
#include "log.h"
#include "property.h"
#include "map_object_placement_policy.h"
#include "layout_components/tree_view.h"
#include <QElapsedTimer>
#include <QCoreApplication>


//CChangeModelParam::CChangeModelParam(CView* pView, uint nodeId, EObjParam &objParam, QString value, QUndoCommand *parent):
//    CChangeStringParam(pView, nodeId, objParam, value, parent)
//{
//    //setText("Change model to " + value);
//}

//void CChangeModelParam::undo()
//{
//    auto pNode = m_pView->currentMob()->nodeByMapId(m_nodeId);
//    pNode->applyParam(m_objParam, m_oldValue);
//    pNode->applyParam(eObjParam_BODYPARTS, util::makeString(m_oldBodyparts));
//    emit updatePosOnLand(pNode);
//    emit updateParam();
//    m_pView->setDurty();
//}

//void CChangeModelParam::redo()
//{
//    setText("Change model to " + m_newValue);
//    auto pNode = m_pView->currentMob()->nodeByMapId(m_nodeId);
//    m_oldValue = pNode->getParam(m_objParam);
//    QString partsList = pNode->getParam(eObjParam_BODYPARTS);
//    m_oldBodyparts = util::strListFromString(partsList);
//    pNode->applyParam(m_objParam, m_newValue);
//    QString empty("");
//    pNode->applyParam(eObjParam_BODYPARTS, empty);
//    emit updatePosOnLand(pNode);
//    emit updateParam();
//    m_pView->setDurty();
//}

CDeleteNodeCommand::CDeleteNodeCommand(CView* pView, uint nodeId, QUndoCommand *parent)
    :QUndoCommand(parent)
    ,m_pView(pView)
    ,m_pMob(pView ? pView->currentMob() : nullptr)
    ,m_pNode(m_pMob ? m_pMob->nodeByMapId(nodeId) : nullptr)
    ,m_nodeId(nodeId)
    ,m_originalIndex(-1)
{
    if(m_pMob && m_pNode)
        m_originalIndex = m_pMob->nodes().indexOf(m_pNode);
}

bool CDeleteNodeCommand::invalidatesAiGraphPreview() const
{
    return m_pNode
        && ei::contributesStaticNavigationCollision(m_pNode->nodeType());
}

void CDeleteNodeCommand::undo()
{
    auto pNode = m_pMob ? m_pMob->restoreDeletedNode(m_pNode, m_originalIndex) : nullptr;
    if(nullptr != pNode)
        emit undo_deleteNodeSignal(pNode);
    m_pView->setDurty(m_pMob);
}

void CDeleteNodeCommand::redo()
{
    auto pNode = m_pNode;
    if(!m_pMob || !pNode || !m_pMob->nodes().contains(pNode))
    {
        ei::log(eLogWarning, QString("Delete redo skipped because exact node %1 is unavailable").arg(m_nodeId));
        return;
    }
    setText(QCoreApplication::translate("UndoCommands", "Delete object ID: %1")
            .arg(pNode->mapId()));
    ei::log(eLogInfo, "Delete node ID: " + QString::number(pNode->mapId()));
    m_pMob->deleteNode(pNode);
    emit deleteNodeSignal(m_nodeId);
    m_pView->setDurty(m_pMob);
}

CCreateNodeCommand::CCreateNodeCommand(CView* pView, QJsonObject nodeData, QUndoCommand *parent):
    QUndoCommand(parent)
    ,m_pView(pView)
    ,m_pMob(pView ? pView->currentMob() : nullptr)
    ,m_nodeData(nodeData)
    ,m_pCreatedNode(nullptr)
    ,m_createdNodeId(0)
    ,m_createdNodeIndex(-1)
    ,m_created(false)
    ,m_skipInitialRedo(false)
{
    //setText("Node created");
}

CCreateNodeCommand::CCreateNodeCommand(CView* pView, CMob* pMob,
                                       CNode* existingNode, QUndoCommand *parent):
    QUndoCommand(parent)
    ,m_pView(pView)
    ,m_pMob(pMob)
    ,m_pCreatedNode(existingNode)
    ,m_createdNodeId(existingNode ? existingNode->mapId() : 0)
    ,m_createdNodeIndex(pMob && existingNode ? pMob->nodes().indexOf(existingNode) : -1)
    ,m_created(existingNode != nullptr)
    ,m_skipInitialRedo(existingNode != nullptr)
{
    if(existingNode && existingNode->hasRawObjectProvenance())
    {
        m_rawTemplatePath = existingNode->rawSourceObjectPath();
        m_rawTemplateSubtreeSha256 = existingNode->rawSourceSubtreeSha256();
    }
}

bool CCreateNodeCommand::invalidatesAiGraphPreview() const
{
    return m_pCreatedNode
        && ei::contributesStaticNavigationCollision(m_pCreatedNode->nodeType());
}

void CCreateNodeCommand::undo()
{
    if(!m_pMob || !m_pCreatedNode || !m_pMob->nodes().contains(m_pCreatedNode))
    {
        ei::log(eLogWarning, QString("Create undo skipped because exact node %1 is unavailable").arg(m_createdNodeId));
        return;
    }
    // A save rebinds the node to the saved document and clears its creation
    // template. Retain that exact record before undo: the user may save the
    // deletion, then redo after the source record has left the document.
    QByteArray source, clean;
    if(m_pMob->rawObjectTemplateBytes(m_pCreatedNode, &source, &clean))
    {
        m_pCreatedNode->setRawObjectTemplateBytes(source, clean);
        m_rawTemplatePath = m_pCreatedNode->rawSourceObjectPath();
        m_rawTemplateSubtreeSha256 = m_pCreatedNode->rawSourceSubtreeSha256();
    }
    m_pMob->deleteNode(m_pCreatedNode);
    m_pView->setDurty(m_pMob);
    emit undo_addNodeSignal(m_createdNodeId);
}

void CCreateNodeCommand::redo()
{
    CNode* pNode = nullptr;
    const bool adoptingExistingNode = m_skipInitialRedo;
    if(adoptingExistingNode)
    {
        m_skipInitialRedo = false;
        pNode = m_pCreatedNode;
    }
    else if(!m_created)
    {
        pNode = m_pMob ? m_pMob->createNode(m_nodeData) : nullptr;
        if(!pNode)
        {
            ei::log(eLogWarning, "Create node command failed");
            return;
        }
        m_pCreatedNode = pNode;
        m_createdNodeId = pNode->mapId();
        m_createdNodeIndex = m_pMob->nodes().indexOf(pNode);
        m_created = true;
    }
    else
    {
        if(m_pMob->nodes().contains(m_pCreatedNode))
        {
            ei::log(eLogWarning, QString("Create redo refused duplicate insertion for node %1").arg(m_createdNodeId));
            return;
        }
        pNode = m_pMob->restoreDeletedNode(m_pCreatedNode, m_createdNodeIndex);
        if(!pNode)
        {
            ei::log(eLogWarning, QString("Create redo failed to restore exact node %1").arg(m_createdNodeId));
            return;
        }
    }
    if(!pNode)
        return;
    if(m_rawTemplateSubtreeSha256.size() == 32 && !m_rawTemplatePath.isEmpty()
       && !m_pMob->rebaseCopiedObjectProvenance(pNode, m_rawTemplatePath,
                                                m_rawTemplateSubtreeSha256))
    {
        ei::log(eLogWarning, QString("Create redo restored node %1 without rebased raw template")
                .arg(m_createdNodeId));
    }
    else if(m_rawTemplateSubtreeSha256.isEmpty() && pNode->hasRawObjectProvenance())
    {
        m_rawTemplatePath = pNode->rawSourceObjectPath();
        m_rawTemplateSubtreeSha256 = pNode->rawSourceSubtreeSha256();
    }
    pNode->setState(ENodeState::eSelect);
    setText(QCoreApplication::translate("UndoCommands", "Created object ID: %1")
            .arg(m_createdNodeId));
    m_pView->setDurty(m_pMob);
    if(!adoptingExistingNode)
        emit addNodeSignal(pNode);
}

CSelectionCommand::CSelectionCommand(CView* pView, const QList<CNode*>& before,
                                     const QList<CNode*>& after, const QString& text,
                                     QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_pView(pView)
    , m_before(before)
    , m_after(after)
    , m_firstRedo(true)
{
    setText(text);
}

CConvertWorldObjectToLeverCommand::CConvertWorldObjectToLeverCommand(
    CView* view, CMob* mob, CNode* worldObject, CNode* lever, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob),
      m_worldObject(worldObject), m_lever(lever),
      m_index(mob && worldObject ? mob->nodes().indexOf(worldObject) : -1),
      m_selected(worldObject && worldObject->nodeState() == ENodeState::eSelect)
{
    setText(QCoreApplication::translate("UndoCommands",
            "Convert world object ID %1 to lever")
            .arg(worldObject ? worldObject->mapId() : 0));
}

void CConvertWorldObjectToLeverCommand::apply(CNode* from, CNode* to)
{
    if(!m_view || !m_mob || !from || !to || !m_mob->replaceNode(from, to, m_index))
    {
        ei::log(eLogWarning, "World-object/lever replacement could not be applied");
        return;
    }
    from->setState(ENodeState::eDraw);
    to->setState(m_selected ? ENodeState::eSelect : ENodeState::eDraw);
    m_view->collectObjectTreeData();
    m_view->viewParameters();
    m_view->setDurty(m_mob);
    m_view->update();
}

void CConvertWorldObjectToLeverCommand::undo()
{
    apply(m_lever, m_worldObject);
}

void CConvertWorldObjectToLeverCommand::redo()
{
    apply(m_worldObject, m_lever);
}

void CSelectionCommand::undo()
{
    if(m_pView)
        m_pView->restoreSelection(m_before);
}

void CSelectionCommand::redo()
{
    // recordSelectionChange() is called after the selection was applied.
    // QUndoStack::push() invokes redo() immediately, so replaying here would
    // redundantly rebuild the property panel and scroll the outliner. Viewport
    // picking performs its own explicit reveal after the command is recorded.
    if(m_firstRedo)
    {
        m_firstRedo = false;
        return;
    }
    if(m_pView)
        m_pView->restoreSelection(m_after);
}

CChangeLogicParam::CChangeLogicParam(CView* pView, CNode* pNode, const QSharedPointer<IPropertyBase>& prop, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_pMob(pView ? pView->currentMob() : nullptr)
  ,m_pNode(pNode)
  ,m_nodeType(pNode ? pNode->nodeType() : eUnknown)
  ,m_ownerMapId(pNode ? pNode->mapId() : 0)
  ,m_patrolId(-1)
  ,m_viewId(-1)
  ,m_auxPointId(-1)
  ,m_logicSlot(pView ? pView->logicSlotIndex() : 0)
  ,m_oldValueCaptured(false)
{
    m_newValue.reset(prop->clone());
    if(!m_pMob || !pNode)
        return;
    if(m_nodeType == ePatrolPoint)
    {
        int owner = 0;
        m_pMob->getPatrolHash(owner, m_patrolId, static_cast<CPatrolPoint*>(pNode));
        m_ownerMapId = uint(owner);
    }
    else if(m_nodeType == eLookPoint)
    {
        int owner = 0;
        m_pMob->getViewHash(owner, m_patrolId, m_viewId, static_cast<CLookPoint*>(pNode));
        m_ownerMapId = uint(owner);
    }
    else if(m_nodeType == eTrapActZone)
    {
        int owner = 0;
        m_pMob->getTrapZoneHash(owner, m_auxPointId, static_cast<CActivationZone*>(pNode));
        m_ownerMapId = uint(owner);
    }
    else if(m_nodeType == eTrapCastPoint)
    {
        int owner = 0;
        m_pMob->getTrapCastHash(owner, m_auxPointId, static_cast<CTrapCastPoint*>(pNode));
        m_ownerMapId = uint(owner);
    }
}

CNode* CChangeLogicParam::resolveNode() const
{
    if(!m_pMob)
        return nullptr;
    switch(m_nodeType)
    {
    case ePatrolPoint: return m_pMob->patrolPointById(int(m_ownerMapId), m_patrolId, m_logicSlot);
    case eLookPoint: return m_pMob->viewPointById(int(m_ownerMapId), m_patrolId, m_viewId, m_logicSlot);
    case eTrapActZone: return m_pMob->actZoneById(int(m_ownerMapId), m_auxPointId);
    case eTrapCastPoint: return m_pMob->trapCastById(int(m_ownerMapId), m_auxPointId);
    default: return m_pMob->nodeByMapId(m_ownerMapId);
    }
}

void CChangeLogicParam::undo()
{
    m_pNode = resolveNode();
    if(!m_pNode || !m_oldValueCaptured)
    {
        ei::log(eLogWarning, "Logic property undo skipped because its target no longer exists");
        return;
    }
    if(CUnit* unit = dynamic_cast<CUnit*>(m_pNode))
        unit->applyLogicParamAt(m_logicSlot, m_oldValue);
    else
        m_pNode->applyLogicParam(m_oldValue);

    emit updateParam();
    if(m_newValue && (m_newValue->type() == eObjParam_LOGIC_USE
                      || m_newValue->type() == eObjParam_LOGIC_BEHAVIOUR))
        m_pView->refreshLogicSlotOutliner();
    m_pView->setDurty(m_pMob);
}

void CChangeLogicParam::redo()
{
    m_pNode = resolveNode();
    if(!m_pNode)
    {
        ei::log(eLogWarning, "Logic property redo skipped because its target no longer exists");
        return;
    }
    if(!m_oldValueCaptured)
    {
        if(CUnit* unit = dynamic_cast<CUnit*>(m_pNode))
            unit->getLogicParamAt(m_logicSlot, m_oldValue, m_newValue->type());
        else
            m_pNode->getLogicParam(m_oldValue, m_newValue->type());
        m_oldValueCaptured = !m_oldValue.isNull();
    }
    if(CUnit* unit = dynamic_cast<CUnit*>(m_pNode))
        unit->applyLogicParamAt(m_logicSlot, m_newValue);
    else
        m_pNode->applyLogicParam(m_newValue);

    emit updateParam();
    if(m_newValue && (m_newValue->type() == eObjParam_LOGIC_USE
                      || m_newValue->type() == eObjParam_LOGIC_BEHAVIOUR))
        m_pView->refreshLogicSlotOutliner();
    m_pView->setDurty(m_pMob);
    setText(QCoreApplication::translate("UndoCommands", "Change value to %1")
            .arg(m_newValue->toString()));
}

CChangeMobParametersCommand::CChangeMobParametersCommand(
        CView* view, CMob* mob, const SMobParameterState& before,
        const SMobParameterState& after, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob), m_before(before),
      m_after(after), m_firstRedo(true)
{
    setText(QCoreApplication::translate("UndoCommands", "Edit MOB parameters"));
}

void CChangeMobParametersCommand::apply(const SMobParameterState& state)
{
    if(!m_mob)
        return;
    m_mob->applyParameterState(state);
    if(m_view)
    {
        m_view->setDurty(m_mob);
        m_view->viewParameters();
    }
}

void CChangeMobParametersCommand::undo()
{
    apply(m_before);
}

void CChangeMobParametersCommand::redo()
{
    if(m_firstRedo)
    {
        m_firstRedo = false;
        if(m_view)
            m_view->setDurty(m_mob);
        return;
    }
    apply(m_after);
}

CChangeMobScriptCommand::CChangeMobScriptCommand(
        CView* view, CMob* mob, const QString& before, const QString& after,
        const QString& text, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob), m_before(before), m_after(after)
{
    setText(text);
}

void CChangeMobScriptCommand::apply(const QString& script)
{
    if(!m_mob)
        return;
    m_mob->setScript(script);
    if(m_view)
    {
        m_view->setDurty(m_mob);
        m_view->notifyMobScriptChanged(m_mob);
    }
}

void CChangeMobScriptCommand::undo()
{
    apply(m_before);
}

void CChangeMobScriptCommand::redo()
{
    apply(m_after);
}

CCreatePatrolCommand::CCreatePatrolCommand(
    CView* pView, QString pointHash, const QList<CNode*>& originalSelection,
    QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_mob(pView ? pView->currentMob() : nullptr)
  ,m_pointHash(pointHash)
  ,m_logicSlot(pView ? pView->logicSlotIndex() : 0)
  ,m_firstPointUnit(nullptr)
  ,m_previousBehaviour(0)
  ,m_switchesToPath(false)
  ,m_originalSelection(originalSelection)
{
    const QStringList parts = pointHash.split(".");
    if(m_mob && parts.size() == 2 && parts[1].toInt() == -1)
    {
        m_firstPointUnit = dynamic_cast<CUnit*>(m_mob->nodeByMapId(parts[0].toUInt()));
        if(m_firstPointUnit)
        {
            QSharedPointer<IPropertyBase> behaviour;
            m_firstPointUnit->getLogicParamAt(m_logicSlot, behaviour, eObjParam_LOGIC_BEHAVIOUR);
            const propUint* value = dynamic_cast<const propUint*>(behaviour.get());
            if(value)
            {
                m_previousBehaviour = value->value();
                m_switchesToPath = m_previousBehaviour != uint(EBehaviourType::ePath);
            }
        }
    }
}

void CCreatePatrolCommand::undo()
{
    if(!m_mob)
        return;
    m_mob->undo_createPatrolByHash(m_pointHash, m_logicSlot);
    if(m_switchesToPath && m_firstPointUnit)
    {
        QSharedPointer<IPropertyBase> behaviour(
            new propUint(eObjParam_LOGIC_BEHAVIOUR, m_previousBehaviour));
        m_firstPointUnit->applyLogicParamAt(m_logicSlot, behaviour);
    }
    m_pView->collectObjectTreeData();
    m_pView->restoreSelection(m_originalSelection);
    m_pView->setDurty(m_mob);
    m_pView->update();
}

void CCreatePatrolCommand::redo()
{
    for(CNode* node : m_originalSelection)
        if(node)
            node->setState(ENodeState::eDraw);
    if(m_switchesToPath && m_firstPointUnit)
    {
        QSharedPointer<IPropertyBase> behaviour(
            new propUint(eObjParam_LOGIC_BEHAVIOUR, uint(EBehaviourType::ePath)));
        m_firstPointUnit->applyLogicParamAt(m_logicSlot, behaviour);
    }
    if(!m_mob)
        return;
    m_mob->createPatrolByHash(m_pointHash, m_logicSlot);
    setText(m_switchesToPath
        ? QCoreApplication::translate("UndoCommands",
            "Create first patrol point and enable Path behavior")
        : QCoreApplication::translate("UndoCommands", "Create patrol point"));
    m_pView->collectObjectTreeData();
    m_pView->viewParameters();
    m_pView->setDurty(m_mob);
    m_pView->update();
}

CCreateTrapPointCommand::CCreateTrapPointCommand(
    CView *pView, uint trapId, bool bActZone,
    const QList<CNode*>& originalSelection, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_trapId(trapId)
  ,m_bActZone(bActZone)
  ,m_originalSelection(originalSelection)
{
}

void CCreateTrapPointCommand::undo()
{
    CMob* mob = m_pView ? m_pView->currentMob() : nullptr;
    CMagicTrap* pTrap = mob
        ? dynamic_cast<CMagicTrap*>(mob->nodeByMapId(m_trapId)) : nullptr;
    if(!pTrap)
        return;
    if(m_bActZone)
        pTrap->deleteLastActZone();
    else
        pTrap->deleteLastCastPoint();
    mob->logicNodesUpdate();
    m_pView->collectObjectTreeData();
    m_pView->restoreSelection(m_originalSelection);
    m_pView->setDurty(mob);
    m_pView->update();
}

void CCreateTrapPointCommand::redo()
{
    CMob* mob = m_pView ? m_pView->currentMob() : nullptr;
    CMagicTrap* pTrap = mob
        ? dynamic_cast<CMagicTrap*>(mob->nodeByMapId(m_trapId)) : nullptr;
    if(!pTrap)
        return;
    for(CNode* node : m_originalSelection)
        if(node)
            node->setState(ENodeState::eDraw);
    if(m_bActZone)
    {
        auto pZone = pTrap->createActZone();
        pZone->setState(ENodeState::eSelect);
    }
    else
    {
        auto pCast = pTrap->createCastPoint();
        pCast->setState(ENodeState::eSelect);
    }
    mob->logicNodesUpdate();
    m_pView->collectObjectTreeData();
    m_pView->viewParameters();
    m_pView->setDurty(mob);
    m_pView->update();
}

CDeleteLogicPoint::CDeleteLogicPoint(CView *pView, QString hash, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_pMob(pView ? pView->currentMob() : nullptr)
  ,m_nodeHash(hash)
  ,m_logicSlot(pView ? pView->logicSlotIndex() : 0)
{
}

CAppendBehaviorPointCommand::CAppendBehaviorPointCommand(
        CView* view, CMob* mob, uint unitId, int logicSlot, int patrolId,
        const QJsonObject& pointData, bool lookPoint, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob), m_unitId(unitId),
      m_logicSlot(logicSlot), m_patrolId(patrolId), m_previousIndex(-1),
      m_pointData(pointData), m_lookPoint(lookPoint)
{
    CUnit* unit = m_mob
        ? dynamic_cast<CUnit*>(m_mob->nodeByMapId(m_unitId)) : nullptr;
    if(unit)
    {
        if(m_lookPoint)
        {
            CPatrolPoint* patrol = unit->patrolByIndex(m_logicSlot, m_patrolId);
            if(patrol) m_previousIndex = patrol->lookPoint().size() - 1;
        }
        else if(CLogic* logic = unit->logicAt(m_logicSlot))
            m_previousIndex = logic->patrolPoints().size() - 1;
    }
    setText(m_lookPoint
        ? QCoreApplication::translate("UndoCommands", "Paste look point")
        : QCoreApplication::translate("UndoCommands", "Paste patrol point"));
}

void CAppendBehaviorPointCommand::refresh()
{
    if(!m_mob || !m_view)
        return;
    if(CUnit* unit = dynamic_cast<CUnit*>(m_mob->nodeByMapId(m_unitId)))
        unit->refreshLogicProjection();
    m_mob->logicNodesUpdate();
    m_view->collectObjectTreeData();
    m_view->viewParameters();
    m_view->setDurty(m_mob);
    m_view->update();
}

void CAppendBehaviorPointCommand::redo()
{
    CUnit* unit = m_mob
        ? dynamic_cast<CUnit*>(m_mob->nodeByMapId(m_unitId)) : nullptr;
    if(!unit || m_previousIndex < -1)
        return;
    if(m_lookPoint)
    {
        unit->createViewByIndex(m_logicSlot, m_patrolId, m_previousIndex);
        CLookPoint* created = unit->viewByIndex(
            m_logicSlot, m_patrolId, m_previousIndex + 1);
        if(created) created->deSerializeJson(m_pointData);
    }
    else
    {
        unit->createPatrolByIndex(m_logicSlot, m_previousIndex);
        CPatrolPoint* created = unit->patrolByIndex(
            m_logicSlot, m_previousIndex + 1);
        if(created) created->deSerializeJson(m_pointData);
    }
    refresh();
}

void CAppendBehaviorPointCommand::undo()
{
    CUnit* unit = m_mob
        ? dynamic_cast<CUnit*>(m_mob->nodeByMapId(m_unitId)) : nullptr;
    if(!unit)
        return;
    if(m_lookPoint)
        unit->undo_createViewByIndex(m_logicSlot, m_patrolId, m_previousIndex);
    else
        unit->undo_createPatrolByIndex(m_logicSlot, m_previousIndex);
    refresh();
}

CBehaviorPasteCommand::CBehaviorPasteCommand(
        CView* view, CMob* mob, const QList<CNode*>& pastedNodes,
        const QList<CNode*>& originalSelection, const QString& pointKind,
        QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob),
      m_pastedNodes(pastedNodes), m_originalSelection(originalSelection),
      m_firstRedo(true)
{
    setText(QCoreApplication::translate("UndoCommands", "Paste %1 %2")
        .arg(m_pastedNodes.size()).arg(pointKind));
}

void CBehaviorPasteCommand::refresh(const QList<CNode*>& selection)
{
    if(!m_view || !m_mob)
        return;
    m_mob->logicNodesUpdate();
    m_view->collectObjectTreeData();
    m_view->restoreSelection(selection);
    m_view->setDurty(m_mob);
    m_view->update();
}

void CBehaviorPasteCommand::undo()
{
    for(CNode* node : m_pastedNodes)
        if(node) node->markAsDeleted(true);
    refresh(m_originalSelection);
}

void CBehaviorPasteCommand::redo()
{
    if(m_firstRedo)
    {
        m_firstRedo = false;
        return;
    }
    for(CNode* node : m_pastedNodes)
        if(node) node->markAsDeleted(false);
    refresh(m_pastedNodes);
}

CMoveBehaviorPointCommand::CMoveBehaviorPointCommand(
        CView* view, CMob* mob, uint unitId, int logicSlot, int patrol,
        int from, int to, bool lookPoint, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob), m_unitId(unitId),
      m_logicSlot(logicSlot), m_patrol(patrol), m_from(from), m_to(to),
      m_lookPoint(lookPoint)
{
    setText(m_lookPoint
        ? QCoreApplication::translate("UndoCommands", "Reorder look point")
        : QCoreApplication::translate("UndoCommands", "Reorder patrol point"));
}

void CMoveBehaviorPointCommand::apply(int from, int to)
{
    QElapsedTimer timer;
    timer.start();
    CUnit* unit = m_mob
        ? dynamic_cast<CUnit*>(m_mob->nodeByMapId(m_unitId)) : nullptr;
    CLogic* logic = unit ? unit->logicAt(m_logicSlot) : nullptr;
    bool changed = false;
    if(logic && m_lookPoint)
    {
        CPatrolPoint* patrol = logic->patrolByIndex(m_patrol);
        changed = patrol && patrol->moveLookPoint(from, to);
    }
    else if(logic)
        changed = logic->movePatrolPoint(from, to);
    if(!changed || !m_view || !m_mob)
        return;
    unit->refreshLogicProjection();
    const qint64 projectionMs = timer.elapsed();
    m_view->refreshBehaviorOrder(unit, m_logicSlot);
    const qint64 outlinerMs = timer.elapsed() - projectionMs;
    m_view->viewParameters();
    const qint64 propertiesMs = timer.elapsed() - projectionMs - outlinerMs;
    m_view->setDurty(m_mob);
    m_view->update();
    ei::log(eLogInfo, QString(
        "Behavior collection reorder kind:%1 from:%2 to:%3 projectionMs:%4 outlinerMs:%5 propertiesMs:%6 totalMs:%7")
        .arg(m_lookPoint ? "look" : "patrol").arg(from).arg(to)
        .arg(projectionMs).arg(outlinerMs).arg(propertiesMs).arg(timer.elapsed()));
}

void CMoveBehaviorPointCommand::undo()
{
    apply(m_to, m_from);
}

void CMoveBehaviorPointCommand::redo()
{
    apply(m_from, m_to);
}

CMoveTrapPointCommand::CMoveTrapPointCommand(
        CView* view, CMob* mob, uint trapId, int from, int to,
        bool castTarget, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob), m_trapId(trapId),
      m_from(from), m_to(to), m_castTarget(castTarget)
{
    setText(m_castTarget
        ? QCoreApplication::translate("UndoCommands", "Reorder cast target")
        : QCoreApplication::translate("UndoCommands", "Reorder activation area"));
}

void CMoveTrapPointCommand::apply(int from, int to)
{
    QElapsedTimer timer;
    timer.start();
    CMagicTrap* trap = m_mob
        ? dynamic_cast<CMagicTrap*>(m_mob->nodeByMapId(m_trapId)) : nullptr;
    const bool changed = trap && (m_castTarget
        ? trap->moveCastTarget(from, to)
        : trap->moveActivationArea(from, to));
    if(!changed || !m_view || !m_mob)
        return;
    m_view->refreshBehaviorOrder(trap, -1);
    const qint64 outlinerMs = timer.elapsed();
    m_view->viewParameters();
    const qint64 propertiesMs = timer.elapsed() - outlinerMs;
    m_view->setDurty(m_mob);
    m_view->update();
    ei::log(eLogInfo, QString(
        "Trap collection reorder kind:%1 from:%2 to:%3 outlinerMs:%4 propertiesMs:%5 totalMs:%6")
        .arg(m_castTarget ? "cast" : "activation").arg(from).arg(to)
        .arg(outlinerMs).arg(propertiesMs).arg(timer.elapsed()));
}

void CMoveTrapPointCommand::undo()
{
    apply(m_to, m_from);
}

void CMoveTrapPointCommand::redo()
{
    apply(m_from, m_to);
}

CReplaceLogicSlotsCommand::CReplaceLogicSlotsCommand(
        CView* view, CMob* mob,
        const QVector<SLogicSlotReplacement>& replacements,
        uint focusedUnitId, int focusedLogicSlot, QUndoCommand* parent)
    : QUndoCommand(parent), m_view(view), m_mob(mob),
      m_replacements(replacements), m_focusedUnitId(focusedUnitId),
      m_focusedLogicSlot(focusedLogicSlot)
{
    for(SLogicSlotReplacement& replacement : m_replacements)
    {
        CUnit* unit = m_mob ? dynamic_cast<CUnit*>(
            m_mob->nodeByMapId(replacement.unitId)) : nullptr;
        if(!unit || !unit->logicAt(replacement.logicSlot))
            continue;
        replacement.alternate = new CLogic(unit);
        replacement.alternate->replaceFromJson(replacement.after);
    }
    setText(replacements.size() == 1
        ? QCoreApplication::translate("UndoCommands", "Paste Logic slot")
        : QCoreApplication::translate("UndoCommands", "Paste Logic slot to %1 units")
            .arg(replacements.size()));
}

CReplaceLogicSlotsCommand::~CReplaceLogicSlotsCommand()
{
    for(SLogicSlotReplacement& replacement : m_replacements)
        delete replacement.alternate;
}

void CReplaceLogicSlotsCommand::apply(bool useAfter)
{
    Q_UNUSED(useAfter);
    if(!m_view || !m_mob)
        return;
    QList<CNode*> units;
    for(SLogicSlotReplacement& replacement : m_replacements)
    {
        CUnit* unit = dynamic_cast<CUnit*>(
            m_mob->nodeByMapId(replacement.unitId));
        if(!unit || !replacement.alternate)
            continue;
        replacement.alternate = unit->swapLogicAt(
            replacement.logicSlot, replacement.alternate);
        unit->refreshLogicProjection();
        units.append(unit);
    }
    m_mob->logicNodesUpdate();
    m_view->collectObjectTreeData();
    m_view->restoreSelection(units);
    CUnit* focused = dynamic_cast<CUnit*>(m_mob->nodeByMapId(m_focusedUnitId));
    if(focused && m_view->objectTree())
        m_view->objectTree()->focusLogicSlot(
            focused, m_focusedLogicSlot, false);
    m_view->viewParameters();
    m_view->setDurty(m_mob);
    m_view->update();
}

void CReplaceLogicSlotsCommand::undo()
{
    apply(false);
}

void CReplaceLogicSlotsCommand::redo()
{
    apply(true);
}

void CDeleteLogicPoint::undo()
{
    QStringList list = m_nodeHash.split(".");
    if(list.size()==1)
    {
        Q_ASSERT(false);
    }
    else if(list.size()==2)
    {//patrol point
        CPatrolPoint* pPoint = m_pMob->patrolPointById(list[0].toInt(), list[1].toInt(), m_logicSlot);
        if(pPoint) pPoint->markAsDeleted(false);
    }
    else if(list.size()==3)
    {//look point
        CLookPoint* pPoint = m_pMob->viewPointById(list[0].toInt(), list[1].toInt(), list[2].toInt(), m_logicSlot);
        if(pPoint) pPoint->markAsDeleted(false);
    }
    else if(list.size() == 4) //trap zone
    {
        CActivationZone* pZone = m_pMob->actZoneById(list[0].toInt(), list[3].toInt());
        pZone->markAsDeleted(false);
    }
    else if(list.size() == 5) //trap cast point
    {
        CTrapCastPoint* pCast = m_pMob->trapCastById(list[0].toInt(), list[4].toInt());
        pCast->markAsDeleted(false);
    }
    m_pMob->logicNodesUpdate();
    m_pView->collectObjectTreeData();
    m_pView->viewParameters();
    m_pView->setDurty(m_pMob);
    m_pView->update();
}

void CDeleteLogicPoint::redo()
{
    QStringList list = m_nodeHash.split(".");
    if(list.size()==1)
    {
        Q_ASSERT(false);
    }
    else if(list.size()==2)
    {//patrol point
        CPatrolPoint* pPoint = m_pMob->patrolPointById(list[0].toInt(), list[1].toInt(), m_logicSlot);
        if(pPoint) pPoint->markAsDeleted(true);
    }
    else if(list.size()==3)
    {//look point
        CLookPoint* pPoint = m_pMob->viewPointById(list[0].toInt(), list[1].toInt(), list[2].toInt(), m_logicSlot);
        if(pPoint) pPoint->markAsDeleted(true);
    }
    else if(list.size() == 4) //trap zone
    {
        CActivationZone* pZone = m_pMob->actZoneById(list[0].toInt(), list[3].toInt());
        pZone->markAsDeleted(true);
    }
    else if(list.size() == 5) //trap cast point
    {
        CTrapCastPoint* pCast = m_pMob->trapCastById(list[0].toInt(), list[4].toInt());
        pCast->markAsDeleted(true);
    }
    setText(QCoreApplication::translate("UndoCommands", "Point deleted"));
    m_pMob->logicNodesUpdate();
    m_pView->collectObjectTreeData();
    m_pView->viewParameters();
    m_pView->setDurty(m_pMob);
    m_pView->update();
}

CRoundMobCommand::CRoundMobCommand(CView *pView, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pView(pView)
{
}

void CRoundMobCommand::undo()
{
    m_pView->undo_roundActiveMob();
}

void CRoundMobCommand::redo()
{
    m_pView->roundActiveMob();
    setText(QCoreApplication::translate("UndoCommands", "Switch MOB to %1")
            .arg(m_pView->currentMob()->mobName()));
}

CSwitchToQuestMobCommand::CSwitchToQuestMobCommand(CMob* pMob, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pMob(pMob)
  ,m_userAnswer(QMessageBox::NoButton)
{
    m_bQuestMob = m_pMob->isQuestMob();
    m_oldWS = m_pMob->worldSet();
    m_arrOldMnR = m_pMob->ranges(true);
    m_arrOldScR = m_pMob->ranges(false);
    m_oldDiplomacyFoF = m_pMob->diplomacyField();
    m_arrOldDiplomacyFieldName = m_pMob->diplomacyNames();
}

void CSwitchToQuestMobCommand::undo()
{
    m_pMob->setQuestMob(m_bQuestMob);
    m_pMob->setWorldSet(m_oldWS);
    m_pMob->setRanges(true, m_arrOldMnR);
    m_pMob->setRanges(false, m_arrOldScR);
    m_pMob->setDiplomacyField(m_oldDiplomacyFoF);
    m_pMob->setDiplomacyNames(m_arrOldDiplomacyFieldName);
    emit switchQuestMobSignal();
}

void CSwitchToQuestMobCommand::redo()
{
    auto arrRange = m_pMob->ranges(!m_bQuestMob);
    if(!arrRange.isEmpty())
    {
        if(m_userAnswer == QMessageBox::NoButton)
            m_userAnswer = QMessageBox::question(nullptr, "Switching MOB Type", "Do you want to switch Id ranges as well?", QMessageBox::Yes|QMessageBox::No);

        if(m_userAnswer == QMessageBox::Yes)
            m_pMob->setRanges(m_bQuestMob, arrRange);
    }

    CWorldSet ws;
    m_pMob->setWorldSet(ws);
    m_pMob->clearRanges(!m_bQuestMob);
    if(m_bQuestMob)
        m_pMob->generateDiplomacyTable();
    else
        m_pMob->clearDiplomacyTable();

    m_pMob->setQuestMob(!m_bQuestMob);
    setText(m_bQuestMob
        ? QCoreApplication::translate("UndoCommands", "Quest MOB to Base")
        : QCoreApplication::translate("UndoCommands", "Base MOB to Quest"));
    emit switchQuestMobSignal();
}

CChangeWorldSetCommand::CChangeWorldSetCommand(CMob *pMob, EWsType paramType, QString &value, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pMob(pMob)
  ,m_paramType(paramType)
  ,m_newValue(value)
{
    CWorldSet ws = m_pMob->worldSet();
    m_oldValue = ws.data(paramType);
}

void CChangeWorldSetCommand::undo()
{
    CWorldSet ws = m_pMob->worldSet();
    ws.setData(m_paramType, m_oldValue);
    m_pMob->setWorldSet(ws);
    emit changeWsSignal();
}

void CChangeWorldSetCommand::redo()
{
    CWorldSet ws = m_pMob->worldSet();
    ws.setData(m_paramType, m_newValue);
    m_pMob->setWorldSet(ws);
    setText(QCoreApplication::translate("UndoCommands", "Change value to %1")
            .arg(m_newValue));
    emit changeWsSignal();
}

CChangeRangeCommand::CChangeRangeCommand(CMob *pMob, int index, SRange range, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pMob(pMob)
  ,m_index(index)
  ,m_newRange(range)
{
    auto arrRanges = m_pMob->ranges(!m_pMob->isQuestMob());
    if(m_index < arrRanges.count())
    {
        m_oldRange = arrRanges[index];
    }
}

void CChangeRangeCommand::undo()
{
    auto arrRanges(m_pMob->ranges(!m_pMob->isQuestMob()));
    if(m_newRange.isEmpty()) //undo deleting
        arrRanges.insert(m_index, m_oldRange);
    else if(m_oldRange.isEmpty()) // undo appending
        arrRanges.pop_back();
    else //undo changing
        arrRanges[m_index] = m_oldRange;

    m_pMob->setRanges(!m_pMob->isQuestMob(), arrRanges);
    emit changeRangeSignal();
}

void CChangeRangeCommand::redo()
{
    auto arrRanges(m_pMob->ranges(!m_pMob->isQuestMob()));
    if(m_index < arrRanges.count())
    {
        if(m_newRange.isEmpty())
        {
            setText(QCoreApplication::translate("UndoCommands", "Deleted range %1–%2")
                    .arg(m_oldRange.minRange).arg(m_oldRange.maxRange));
            arrRanges.takeAt(m_index);
        }
        else
        {
            setText(QCoreApplication::translate("UndoCommands", "Changed range to %1–%2")
                    .arg(m_newRange.minRange).arg(m_newRange.maxRange));
            arrRanges[m_index] = m_newRange;
        }
    }
    else
    {
        arrRanges.append(m_newRange);
        setText(QCoreApplication::translate("UndoCommands", "Added range %1–%2")
                .arg(m_newRange.minRange).arg(m_newRange.maxRange));
    }
    m_pMob->setRanges(!m_pMob->isQuestMob(), arrRanges);
    emit changeRangeSignal();
}

CChangeDiplomacyTableItem::CChangeDiplomacyTableItem(
    CMob *pMob, int row, int column, bool symmetric, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pMob(pMob)
  ,m_row(row)
  ,m_column(column)
  ,m_bSymmetric(symmetric)
{
}

void CChangeDiplomacyTableItem::undo()
{
    if (m_oldValue < 0)
        return;

    QVector<QVector<uint>>& dipTable = m_pMob->diplomacyField();
    if(dipTable.isEmpty())
    {
        m_oldValue = -1;
        return;
    }
    dipTable[m_row][m_column] = m_oldValue;
    emit changeDipGroup(m_row, m_column);
    if(m_bSymmetric && m_row != m_column)
    {
        dipTable[m_column][m_row] = m_oldValueSymetric;
        emit changeDipGroup(m_column, m_row);
    }
}

void CChangeDiplomacyTableItem::redo()
{
    QVector<QVector<uint>>& dipTable = m_pMob->diplomacyField();
    if(dipTable.isEmpty())
    {
        m_oldValue = -1;
        return;
    }
    m_oldValue = dipTable[m_row][m_column];
    m_oldValueSymetric = dipTable[m_column][m_row];
    int value = m_oldValue;
    ++ value;
    value = value % 3;
    dipTable[m_row][m_column] = value;
    emit changeDipGroup(m_row, m_column);
    if(m_bSymmetric && m_row != m_column)
    {
        dipTable[m_column][m_row] = value;
        emit changeDipGroup(m_column, m_row);
    }
    setText(QCoreApplication::translate("UndoCommands",
        "Diplomacy group (%1, %2) changed to %3").arg(m_row).arg(m_column).arg(value));
}

CReplaceDiplomacyTableCommand::CReplaceDiplomacyTableCommand(
        CMob* pMob, const QList<QString>& names,
        const QVector<QVector<uint>>& table, const QString& description,
        QUndoCommand* parent)
    : QUndoCommand(parent), m_pMob(pMob),
      m_oldNames(pMob ? pMob->diplomacyNames() : QList<QString>()),
      m_oldTable(pMob ? pMob->diplomacyField() : QVector<QVector<uint>>()),
      m_newNames(names), m_newTable(table)
{
    setText(description);
}

void CReplaceDiplomacyTableCommand::apply(
        const QList<QString>& names, const QVector<QVector<uint>>& table)
{
    if(!m_pMob)
        return;
    m_pMob->setDiplomacyNames(names);
    m_pMob->setDiplomacyField(table);
    emit diplomacyTableReplaced();
}

void CReplaceDiplomacyTableCommand::undo()
{
    apply(m_oldNames, m_oldTable);
}

void CReplaceDiplomacyTableCommand::redo()
{
    apply(m_newNames, m_newTable);
}

CChangeActiveMobCommand::CChangeActiveMobCommand(CView *pView, QString mobName, QUndoCommand *parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_mobName(mobName)
{
    m_oldMobName = m_pView->currentMob()->mobName();
}

void CChangeActiveMobCommand::undo()
{
    m_pView->changeCurrentMob(m_oldMobName);
}

void CChangeActiveMobCommand::redo()
{
    m_pView->changeCurrentMob(m_mobName);
    setText(QCoreApplication::translate("UndoCommands", "Change MOB to: %1")
            .arg(m_mobName));
}

CChangeProp::CChangeProp(CView* pView, CNode* pNode, const QSharedPointer<IPropertyBase>& prop, QUndoCommand* parent):
    CChangeProp(pView, pView ? pView->currentMob() : nullptr, pNode, prop, parent)
{
}

CChangeProp::CChangeProp(CView* pView, CMob* pMob, CNode* pNode,
                         const QSharedPointer<IPropertyBase>& prop, QUndoCommand* parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_pMob(pMob)
  ,m_pNode(pNode)
  ,m_nodeId(pNode ? pNode->mapId() : 0)
  ,m_unitMovementStateCaptured(false)
  ,m_modelStateCaptured(false)
  ,m_bodyPartStateCaptured(false)
{
    m_newValue.reset(prop->clone());
}

bool CChangeProp::invalidatesAiGraphPreview() const
{
    return m_pNode && m_newValue
        && aiGraphObjectPropertyInvalidatesPreview(
            m_pNode->nodeType(), m_newValue->type());
}

void CChangeProp::undo()
{
    auto pNode = m_pNode;
    if(!pNode)
        return;
    CUnit* unit = dynamic_cast<CUnit*>(pNode);
    if(unit && m_unitMovementStateCaptured && m_oldValue->type() == eObjParam_POSITION)
        unit->restoreMovementState(m_oldUnitMovementState);
    else if(m_modelStateCaptured && m_oldValue->type() == eObjParam_TEMPLATE)
    {
        pNode->applyParam(m_oldValue);
        pNode->applyParam(m_oldModelParts);
    }
    else if(m_bodyPartStateCaptured && m_oldValue->type() == eObjParam_BODYPARTS)
        pNode->applyParam(m_oldValue);
    else
        pNode->applyParam(m_oldValue);
    if(m_oldValue->type() == eObjParam_NID)
    {
        m_nodeId = dynamic_cast<propUint*>(m_oldValue.get())->value();
        uint id = dynamic_cast<propUint*>(m_newValue.get())->value();
        emit changeIdSignal(id, m_nodeId);
    }
    else if(m_oldValue->type() == eObjParam_NAME || m_oldValue->type() == eObjParam_UNIT_PROTOTYPE)
    {
        emit changeTreeName(pNode);
    }
    emit updateParam();
    //emit updatePosOnLand(m_pNode);
    m_pView->setDurty(m_pMob);
}

void CChangeProp::redo()
{
    setText(QCoreApplication::translate("UndoCommands", "Change value to %1")
            .arg(m_newValue->toString()));
    auto pNode = m_pNode;
    if(!pNode)
        return;
    CUnit* unit = dynamic_cast<CUnit*>(pNode);
    if(unit && m_newValue->type() == eObjParam_POSITION)
    {
        if(!m_unitMovementStateCaptured)
        {
            m_oldUnitMovementState = unit->movementState();
            pNode->getParam(m_oldValue, m_newValue->type());
            pNode->applyParam(m_newValue);
            m_newUnitMovementState = unit->movementState();
            m_unitMovementStateCaptured = true;
        }
        else
            unit->restoreMovementState(m_newUnitMovementState);
    }
    else if(m_newValue->type() == eObjParam_TEMPLATE)
    {
        if(!m_modelStateCaptured)
        {
            pNode->getParam(m_oldValue, m_newValue->type());
            pNode->getParam(m_oldModelParts, eObjParam_BODYPARTS);
            pNode->applyParam(m_newValue);
            pNode->getParam(m_newModelParts, eObjParam_BODYPARTS);
            m_modelStateCaptured = true;
        }
        else
        {
            pNode->applyParam(m_newValue);
            pNode->applyParam(m_newModelParts);
        }
    }
    else if(m_newValue->type() == eObjParam_BODYPARTS)
    {
        if(!m_bodyPartStateCaptured)
        {
            pNode->getParam(m_oldValue, eObjParam_BODYPARTS);
            if(propBodyPart* oldParts = dynamic_cast<propBodyPart*>(m_oldValue.get()))
                oldParts->replaceSerializedValue(oldParts->effectiveSerializedValue());
            pNode->applyParam(m_newValue);
            QSharedPointer<IPropertyBase> appliedValue;
            pNode->getParam(appliedValue, eObjParam_BODYPARTS);
            if(propBodyPart* appliedParts = dynamic_cast<propBodyPart*>(appliedValue.get()))
                appliedParts->replaceSerializedValue(appliedParts->effectiveSerializedValue());
            m_newValue = appliedValue;
            m_bodyPartStateCaptured = true;
        }
        else
            pNode->applyParam(m_newValue);
    }
    else
    {
        pNode->getParam(m_oldValue, m_newValue->type());
        pNode->applyParam(m_newValue);
    }
    switch (m_newValue->type()) {
    case eObjParam_NID:
    {
        //todo: check if changes allowed
        uint oldNodeId = dynamic_cast<propUint*>(m_oldValue.get())->value();
        m_nodeId = dynamic_cast<propUint*>(m_newValue.get())->value();
        emit changeIdSignal(oldNodeId, m_nodeId);
        break;
    }
    case eObjParam_NAME:
    case eObjParam_UNIT_PROTOTYPE:
    {
        emit changeTreeName(pNode);
        break;
    }
    default: {break;}
    }

    emit updateParam();
    //emit updatePosOnLand(m_pNode);
    m_pView->setDurty(m_pMob);
}

CResetIdCommand::CResetIdCommand(CView* pView, QMap<uint, uint>& reconnectId, QUndoCommand* parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_reconnectId(reconnectId)
{

}

bool CResetIdCommand::invalidatesAiGraphPreview() const
{
    CMob* mob = m_pView ? m_pView->currentMob() : nullptr;
    if(!mob)
        return false;
    for(CNode* node : mob->nodes())
        if(node && ei::contributesStaticNavigationCollision(node->nodeType())
           && (m_reconnectId.contains(node->mapId())
               || m_reconnectId.key(node->mapId(), 0) != 0))
            return true;
    return false;
}

void CResetIdCommand::undo()
{
    CNode* pNode = nullptr;
    foreach(pNode, m_pView->currentMob()->nodes())
    {
        if(!m_reconnectId.values().contains(pNode->mapId()))
            continue;
        pNode->setMapId(m_reconnectId.key(pNode->mapId()));
    }
    emit updateParam();
}

void CResetIdCommand::redo()
{
    setText(QCoreApplication::translate("UndoCommands", "Reset IDs for %1 objects")
            .arg(m_reconnectId.size()));
    CNode* pNode = nullptr;
    foreach(pNode, m_pView->currentMob()->nodes())
    {
        if(!m_reconnectId.contains(pNode->mapId()))
            continue;
        pNode->setMapId(m_reconnectId[pNode->mapId()]);
    }
    emit updateParam();
}

CBrushTileCommand::CBrushTileCommand(CView* pView, STileInfo tileInfoNew, STileLocation tileLoc, STileInfo tileInfoOld, short tileBrushCommandId, QUndoCommand* parent):
    QUndoCommand(parent)
  ,m_pView(pView)
  ,m_tileInfoNew(tileInfoNew)
  ,m_tileLoc(tileLoc)
  ,m_tileInfoOld(tileInfoOld)
  ,m_tileBrushCommandId(tileBrushCommandId)
{
    m_arrTileBrushNew[tileLoc] = tileInfoNew;
    m_arrTileBrushOld[tileLoc] = tileInfoOld;
}

void CBrushTileCommand::undo()
{
    m_pView->setTile(m_arrTileBrushOld);
}

void CBrushTileCommand::redo()
{
    setText(QCoreApplication::translate("UndoCommands", "Paint tiles (%1)")
            .arg(m_arrTileBrushNew.size()));
    m_pView->setTile(m_arrTileBrushNew);
    m_pView->setDirtyMpr();
}

bool CBrushTileCommand::mergeWith(const QUndoCommand* command)
{
    const CBrushTileCommand* otherCmd = dynamic_cast<const CBrushTileCommand*>(command);
    if (otherCmd && otherCmd->m_tileInfoNew == m_tileInfoNew && m_tileBrushCommandId == otherCmd->m_tileBrushCommandId)
    {
        if(!m_arrTileBrushNew.contains(otherCmd->m_tileLoc))
        {
            m_arrTileBrushNew[otherCmd->m_tileLoc] = otherCmd->m_tileInfoNew;
            m_arrTileBrushOld[otherCmd->m_tileLoc] = otherCmd->m_tileInfoOld;
        }
        setText(QCoreApplication::translate("UndoCommands", "Paint tiles (%1)")
                .arg(m_arrTileBrushNew.size()));
        return true;
    }
    return false;
}

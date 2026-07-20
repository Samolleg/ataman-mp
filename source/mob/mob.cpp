#include <QDebug>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextCodec>
#include <QTextDecoder>
#include <QDir>
#include <QMessageBox>
#include <QHeaderView>
#include <QSaveFile>
#include <QCryptographicHash>

#include "mob.h"
#include "utils.h"
#include "objects\lever.h"
#include "objects\light.h"
#include "objects\magictrap.h"
#include "objects\particle.h"
#include "objects\sound.h"
#include "objects\torch.h"
#include "objects\worldobj.h"
#include "objects\unit.h"
#include "view.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "settings.h"
#include "layout_components/progressview.h"
#include "log.h"
#include "scene.h"
#include "layout_components/tree_view.h"
#include "backup_manager.h"
#include "duplicate_id_report.h"
#include "staged_save_validator.h"
#include "endurance_document.h"

CMob::CMob():
    m_view(nullptr)
  ,m_pProgress(nullptr)
  ,m_hasVssSection(false)
  ,m_bDirty(false)
  ,m_activeRangeId(0)
{
    m_aNode.clear();
    m_mobType = eEMobTypeBase;
}

CMob::~CMob()
{
    QSet<CNode*> ownedNodes;
    for(CNode* node : m_aNode)
        ownedNodes.insert(node);
    for(CNode* node : m_aDeletedNode)
        ownedNodes.insert(node);
    for(CNode* node : ownedNodes)
        delete node;
}

void CMob::attach(CView *view, CProgressView *pProgress)
{
    m_view = view;
    m_pProgress = pProgress;
}

void CMob::generateDiplomacyTable()
{
    const int fieldNum(32);
    m_diplomacyFoF.resize(fieldNum);
    for(int i(0); i<m_diplomacyFoF.size(); ++i)
        m_diplomacyFoF[i].resize(fieldNum);

    m_aDiplomacyFieldName.clear();
    for(int i(0); i<fieldNum; ++i)
    {
        m_aDiplomacyFieldName.append("Player-"+QString::number(i));
    }
}

void CMob::clearDiplomacyTable()
{
    m_aDiplomacyFieldName.clear();
    m_diplomacyFoF.clear();
}

void CMob::addRange(bool bMain, const SRange range)
{
    QVector<SRange>& mobRange = bMain ? m_aMainRange : m_aSecRange;
    mobRange.append(range);
    m_activeRangeId = mobRange.length()-1;
}

const SRange &CMob::activeRange()
{
    return isQuestMob() ? m_aSecRange[m_activeRangeId] : m_aMainRange[m_activeRangeId];
}

void CMob::setActiveRange(uint rangeId)
{
    m_activeRangeId = rangeId;
}

bool CMob::deserialize(QByteArray data)
{
    ei::log(eLogInfo, "Start read mob: " + m_filePath.absoluteFilePath());
    //todo: global check len of nodes
    uint readByte(0);
    util::CMobParser parser(data);
    double step = 50;
    if (parser.isNextTag("OBJECT_DB_FILE"))
        readByte += parser.skipHeader();
    else
        return false;

    if(parser.isNextTag("SC_OBJECT_DB_FILE"))
    {
        readByte += parser.skipHeader();
        //readByte += parser.readHeader();
        m_mobType = eEMobTypeBase;
    }
    else if (parser.isNextTag("PR_OBJECT_DB_FILE"))
    {
        readByte += parser.skipHeader();
        m_mobType = eEMobTypeQuest;
    }

    while(true)
    {
        if(parser.isNextTag("SS_TEXT"))
        {
            readByte += parser.readHeader();
            readByte += parser.readStringEncrypted(m_script, m_scriptKey, parser.nodeLen());
        }
        else if(parser.isNextTag("SS_TEXT_OLD"))
        {
            readByte += parser.readHeader();
            readByte += parser.readString(m_textOld, parser.nodeLen());
        }
        else if(parser.isNextTag("MAIN_RANGE"))
        {
            readByte += parser.skipHeader();
            while(parser.isNextTag("RANGE"))
            {
                SRange range;
                readByte += parser.skipHeader(); // range
                if (parser.isNextTag("MIN_ID"))
                {
                    readByte += parser.skipHeader();
                    readByte += parser.readDword(range.minRange);
                }
                if(parser.isNextTag("MAX_ID"))
                {
                    readByte += parser.skipHeader();
                    readByte += parser.readDword(range.maxRange);
                }
                addRange(true, range);
            }
        }
        else if(parser.isNextTag("SEC_RANGE"))
        {
            readByte += parser.skipHeader();
            while(parser.isNextTag("RANGE"))
            {
                SRange range;
                readByte += parser.skipHeader(); // range
                if (parser.isNextTag("MIN_ID"))
                {
                    readByte += parser.skipHeader();
                    readByte += parser.readDword(range.minRange);
                }
                if(parser.isNextTag("MAX_ID"))
                {
                    readByte += parser.skipHeader();
                    readByte += parser.readDword(range.maxRange);
                }
                addRange(false, range);
            }
        }
        else if(parser.isNextTag("DIPLOMATION"))
        {
            readByte += parser.skipHeader(); //diplomation
            while(true)
            {
                if(parser.isNextTag("DIPLOMATION_FOF"))
                {
                    readByte += parser.skipHeader();
                    readByte += parser.readDiplomacy(m_diplomacyFoF);
                }
                else if(parser.isNextTag("DIPLOMATION_PL_NAMES"))
                {
                    readByte += parser.skipHeader();
                    readByte += parser.readStringArray(m_aDiplomacyFieldName);
                }
                else {
                    break;
                }
            }
        }
        else if(parser.isNextTag("WORLD_SET"))
        {
            readByte += parser.skipHeader(); // world set
            while(true)
            {
                if(parser.isNextTag("WS_WIND_DIR"))
                {
                    readByte += parser.skipHeader();
                    QVector3D dir;
                    readByte += parser.readPlot(dir);
                    m_worldSet.setData(eWsTypeWindDir,
                        QString("(%1,%2,%3)")
                            .arg(QString::number(dir.x(), 'g', 9))
                            .arg(QString::number(dir.y(), 'g', 9))
                            .arg(QString::number(dir.z(), 'g', 9)));
                }
                else if(parser.isNextTag("WS_WIND_STR"))
                {
                    readByte += parser.skipHeader();
                    float str;
                    readByte += parser.readFloat(str);
                    m_worldSet.setData(eWsTypeWindStr, QString::number(str));
                }
                else if(parser.isNextTag("WS_TIME"))
                {
                    readByte += parser.skipHeader();
                    float time;
                    readByte += parser.readFloat(time);
                    m_worldSet.setData(eWsTypeTime, QString::number(time));
                }
                else if(parser.isNextTag("WS_AMBIENT"))
                {
                    readByte += parser.skipHeader();
                    float ambient;
                    readByte += parser.readFloat(ambient);
                    m_worldSet.setData(eWsTypeAmbient, QString::number(ambient));
                }
                else if(parser.isNextTag("WS_SUN_LIGHT"))
                {
                    readByte += parser.skipHeader();
                    float sunLight;
                    readByte += parser.readFloat(sunLight);
                    m_worldSet.setData(eWsTypeSunLight, QString::number(sunLight));
                }
                else {
                    break;
                }
            }
        }

        //vss section
        else if(parser.isNextTag("VSS_SECTION"))
        {
            m_hasVssSection = true;
            readByte += parser.readHeader();
            readByte += parser.readByteArray(m_vss_section, parser.nodeLen());
        }

        //dirictory
        else if(parser.isNextTag("DIRICTORY"))
        {
            readByte += parser.readHeader();
            readByte += parser.readByteArray(m_directory, parser.nodeLen());
        }

        //dirictory elements
        else if(parser.isNextTag("DIRICTORY_ELEMENTS"))
        {
            readByte += parser.readHeader();
            readByte += parser.readByteArray(m_directoryElements, parser.nodeLen());
        }
        //object section
        else if(parser.isNextTag("OBJECT_SECTION"))
        {
            step = step/3;
            m_pProgress->update(step);
            uint objSecLen = parser.nodeLen();
            uint readSecByte(0);
            readByte += parser.skipHeader(); // object section
            while(readSecByte < objSecLen)
            {
                if(parser.isNextTag("OBJECT"))
                {
                    CWorldObj* obj = new CWorldObj();
                    readSecByte += parser.skipHeader(); // "OBJECT";
                    readSecByte += obj->deserialize(parser);
                    addNode(obj);
                }
                else if (parser.isNextTag("LEVER"))
                {
                    CLever* lever = new CLever();
                    readSecByte += parser.skipHeader(); // "LEVER";
                    readSecByte += lever->deserialize(parser);
                    addNode(lever);
                }
                else if (parser.isNextTag("UNIT"))
                {
                    CUnit* unit = new CUnit();
                    readSecByte += parser.skipHeader(); // "UNIT";
                    readSecByte += unit->deserialize(parser);
                    addNode(unit);
                }
                else if (parser.isNextTag("TORCH"))
                {
                    CTorch* torch = new CTorch();
                    readSecByte += parser.skipHeader(); // "TORCH";
                    readSecByte += torch->deserialize(parser);
                    addNode(torch);
                }
                else if (parser.isNextTag("MAGIC_TRAP"))
                {
                    CMagicTrap* trap = new CMagicTrap();
                    readSecByte += parser.skipHeader();  // "MAGIC_TRAP";
                    readSecByte += trap->deserialize(parser);
                    addNode(trap);
                }
                else if(parser.isNextTag("LIGHT"))
                {
                    CLight* light = new CLight();
                    readSecByte += parser.skipHeader(); // "LIGHT";
                    readSecByte += light->deserialize(parser);
                    addNode(light);
                }
                else if(parser.isNextTag("SOUND"))
                {
                    CSound* sound = new CSound();
                    readSecByte += parser.skipHeader(); // "SOUND";
                    readSecByte += sound->deserialize(parser);
                    addNode(sound);
                }
                else if(parser.isNextTag("PARTICL"))
                {
                    CParticle* particle = new CParticle();
                    readSecByte += parser.skipHeader(); // "PARTICL";
                    readSecByte += particle->deserialize(parser);
                    addNode(particle);
                }
                else {
                    ei::log(eLogFatal, QString("Unsupported object record while projecting MOB tag:%1 bytes:%2/%3")
                        .arg(parser.nextTag()).arg(readSecByte).arg(objSecLen));
                    return false;
                }
            }
            if(readSecByte != objSecLen)
            {
                ei::log(eLogFatal, QString("Object-section projection length mismatch read:%1 expected:%2")
                    .arg(readSecByte).arg(objSecLen));
                return false;
            }
            readByte += readSecByte;
            m_pProgress->update(step);
        }
        else if (parser.isNextTag("LIGHT_SECTION"))
        {
            Q_ASSERT("light section" && false);
            readByte += parser.skipTag();
        }
        else if (parser.isNextTag("PARTICL_SECTION"))
        {
            Q_ASSERT("particle section" && false);
            readByte += parser.skipTag();
        }
        else if (parser.isNextTag("SOUND_SECTION"))
        {
            Q_ASSERT("sound section" && false);
            readByte += parser.skipTag();
        }
        else if(parser.isNextTag("AI_GRAPH"))
        {
            readByte += parser.readHeader();
            readByte += parser.readAiGraph(m_aiGraph, parser.nodeLen());
        }
        else
        {
            const QString tag = parser.nextTag();
            if(tag == "ROOT")
                break;
            // Strict structural parsing has already proved this record is
            // bounded. Keep it in the raw document and omit it only from the
            // editable legacy projection; transaction saves patch the source
            // tree and therefore retain these bytes exactly.
            const uint skipped = parser.skipTag();
            if(skipped < 8)
                return false;
            readByte += skipped;
            ei::log(eLogWarning, QString("Editable projection retained an opaque top-level MOB record tag:%1 bytes:%2 length:%3")
                .arg(tag.isEmpty() ? QString("unknown") : tag)
                .arg(readByte - skipped).arg(skipped));
        }
    }
    m_pProgress->update(step);
    ei::log(eLogInfo, "End read mob");
    return true;
}

void CMob::updateObjects()
{
    ei::log(eLogInfo, "Start update " + QString::number(m_aNode.size())+ " objects");
    for(int i = 0; i < m_aNode.size(); ++i)
    {
        CNode* node = m_aNode.at(i);
        const QString context = QString("MOB object %1/%2 mapId=%3 type=%4 model=%5 texture=%6")
                .arg(i + 1).arg(m_aNode.size()).arg(node->mapId()).arg(int(node->nodeType()))
                .arg(node->modelName(), node->textureName());
        ei::setCrashContext(context + " phase=loadFigure");
        node->loadFigure();
        ei::setCrashContext(context + " phase=loadTexture");
        node->loadTexture();
    }
    ei::setCrashContext("MOB projection phase");
    CLandscape::getInstance()->projectPositions(m_aNode);
    m_pProgress->update(50);
    ei::log(eLogInfo, "End update objects");
    ei::setCrashContext("idle after MOB update");
}

void CMob::reprojectForCurrentTerrain()
{
    CLandscape::getInstance()->projectPositions(m_aNode);
    for(CNode* node : m_aNode)
    {
        if(CUnit* unit = dynamic_cast<CUnit*>(node))
            unit->refreshLogicProjection();
    }
    ei::log(eLogInfo, QString("Reprojected %1 objects for %2 terrain mode")
            .arg(m_aNode.size())
            .arg(CLandscape::getInstance()->isMprLoad() ? "attached" : "standalone"));
}

void CMob::delNodes()
{
    QVector<int> aInd;
    for (int i(0); i < m_aNode.size(); ++i)
    {
        if(m_aNode[i]->nodeState() & ENodeState::eSelect)
            aInd.append(i);
    }
    //aInd.
    for (auto i = aInd.crbegin(); i != aInd.crend(); ++i)
    {
        delete m_aNode[*i];
        m_aNode.removeAt(*i);
    }
    logicNodesUpdate();
}

CNode *CMob::nodeByMapId(uint id)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aNode)
    {
        if(pNode->mapId()==id)
            break;
    }
    return pNode;
}

bool CMob::isFreeId(const uint id)
{
    QVector<uint> arrId;
    for(auto& node: m_aNode)
    {
        if(node->mapId() == id)
            return false;
    }
    //todo: add check for ranges (ranges is not necessary property of mob file)
    return true;
}

QString CMob::mobName()
{
    return m_filePath.fileName();
}

const QVector<SRange> &CMob::ranges(bool bMain)
{
    return bMain ? m_aMainRange : m_aSecRange;
}

void CMob::setRanges(bool bMain, const QVector<SRange> &range)
{
    clearRanges(bMain);
    for(const auto& ran: range)
        addRange(bMain, ran);
}

void CMob::clearRanges(bool bMain)
{
    bMain ? m_aMainRange.clear() : m_aSecRange.clear();
}

void CMob::getPatrolHash(int &unitMapIdOut, int &pointIdOut, CPatrolPoint *pPoint)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eUnit)
        {
            CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
            int index = pUnit->getPatrolId(pPoint);
            if(index >=0)
            {
                unitMapIdOut = pUnit->mapId();
                pointIdOut = index;
                return;
            }
        }
    }
    return;
}

void CMob::getViewHash(int &unitMapIdOut, int &pointIdOut, int &viewIdOut, CLookPoint *pPoint)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() != eUnit)
            continue;

        int parentPatrol;
        int parentView;
        CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
        pUnit->getViewId(parentPatrol, parentView, pPoint);
        if(parentPatrol >=0)
        {
            unitMapIdOut = pUnit->mapId();
            pointIdOut = parentPatrol;
            viewIdOut = parentView;
            return;
        }
    }
    return;
}

int CMob::getPatrolId(uint unitMapId, CPatrolPoint* pPoint)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->mapId() == unitMapId && pNode->nodeType() == eUnit)
        {
            CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
            return pUnit->getPatrolId(pPoint);
        }
    }
    return -1;
}

void CMob::createPatrolByHash(QString hash)
{
    QStringList list = hash.split(".");
    if(list.size()<2)
    {
        ei::log(eLogWarning, "Creating logic point failed. hash incorrect" + hash);
    }

    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eUnit && pNode->mapId() == list[0].toUInt())
        {
            CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
            if(list.size()==2)
                pUnit->createPatrolByIndex(list[1].toInt());
            else if(list.size()==3)
                pUnit->createViewByIndex(list[1].toInt(), list[2].toInt());

            break;
        }
    }

    logicNodesUpdate();
}

void CMob::undo_createPatrolByHash(QString hash)
{
    QStringList list = hash.split(".");
    if(list.size()==2) // patrol point
    {
        CNode* pNode = nullptr;
        foreach(pNode, m_aLogicNode)
        {
            if(pNode->nodeType() == eUnit && pNode->mapId() == list[0].toUInt())
            {
                CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
                pUnit->undo_createPatrolByIndex(list[1].toInt());
                break;
            }
        }
    }
    else if(list.size()==3)
    {
        CNode* pNode = nullptr;
        foreach(pNode, m_aLogicNode)
        {
            if(pNode->nodeType() == eUnit && pNode->mapId() == list[0].toUInt())
            {
                CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
                pUnit->undo_createViewByIndex(list[1].toInt(), list[2].toInt());
                break;
            }
        }
    }
    logicNodesUpdate();
}

CPatrolPoint *CMob::patrolPointById(int unitId, int patrolId)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eUnit && pNode->mapId() == (uint)unitId)
        {
            CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
            return pUnit->patrolByIndex(patrolId);
        }
    }
    Q_ASSERT(false && "cant find patrol point by unit map id");
    return nullptr;
}

CLookPoint *CMob::viewPointById(int unitId, int patrolId, int viewId)
{
    CPatrolPoint* pPoint = patrolPointById(unitId, patrolId);
    return pPoint->viewByIndex(viewId);
}

void CMob::getTrapZoneHash(int &unitMapIdOut, int &zoneIdOut, CActivationZone *pZone)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eMagicTrap)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            int index = pTrap->getZoneId(pZone);
            if(index >=0)
            {
                unitMapIdOut = pTrap->mapId();
                zoneIdOut = index;
                return;
            }
        }
    }
    return;
}

CActivationZone *CMob::actZoneById(int trapId, int zoneId)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eMagicTrap && pNode->mapId() == (uint)trapId)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            return pTrap->actZoneById(zoneId);
        }
    }
    Q_ASSERT(false && "cant find act zone by trap map id");
    return nullptr;
}

void CMob::getTrapCastHash(int &unitMapIdOut, int &pointIdOut, CTrapCastPoint* pCast)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eMagicTrap)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            int index = pTrap->getCastPointId(pCast);
            if(index >=0)
            {
                unitMapIdOut = pTrap->mapId();
                pointIdOut = index;
                return;
            }
        }
    }
    return;
}

CTrapCastPoint *CMob::trapCastById(int trapId, int pointId)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eMagicTrap && pNode->mapId() == (uint)trapId)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            return pTrap->castPointById(pointId);
        }
    }
    Q_ASSERT(false && "cant find cast point by trap map id");
    return nullptr;
}

uint CMob::trapIdByPoint(CActivationZone *pZone)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eMagicTrap)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            int index = pTrap->actZones().indexOf(pZone);
            if(index >=0)
                return pTrap->mapId();
        }
    }
    Q_ASSERT(false);
    return 0;
}

uint CMob::trapIdByPoint(CTrapCastPoint *pPoint)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aLogicNode)
    {
        if(pNode->nodeType() == eMagicTrap)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            int index = pTrap->castPoints().indexOf(pPoint);
            if(index >=0)
                return pTrap->mapId();
        }
    }
    Q_ASSERT(false);
    return 0;
}

CNode* CMob::createNode(QJsonObject data)
{
    const QJsonObject enduranceProvenance = data.value("_Endurance raw provenance").toObject();
    auto wo = data;
    if (data.find("World object") != data.end())
        wo = wo["World object"].toObject();

    auto base = wo["Base object"].toObject();

    CNode* pNode = nullptr;
    ENodeType type = (ENodeType)base["Node type"].toInt(0);
    if (type == ENodeType::eUnknown)
    {
        qDebug() << "cant recognize type of obj";
        return pNode;
    }

    switch (type)
    {
    case ENodeType::eUnit:
    {
        pNode = new CUnit(data);
        pNode->loadFigure();
        pNode->loadTexture();
        break;
    }
    case ENodeType::eTorch:
    {
        pNode = new CTorch(data);
        pNode->loadFigure();
        pNode->loadTexture();
        break;
    }
    case ENodeType::eMagicTrap:
    {
        pNode = new CMagicTrap(data);
        break;
    }
    case ENodeType::eLever:
    {
        pNode = new CLever(data);
        pNode->loadFigure();
        pNode->loadTexture();
        break;
    }
    case ENodeType::eLight:
    {
        pNode = new CLight(data);
        break;
    }
    case ENodeType::eSound:
    {
        pNode = new CSound(data);
        break;
    }
    case ENodeType::eParticle:
    {
        pNode = new CParticle(data);
        break;
    }
    case ENodeType::eWorldObject:
    {
        pNode = new CWorldObj(data);
        pNode->loadFigure();
        pNode->loadTexture();
        break;
    }
    default:
    {
        Q_ASSERT("unknown node type" && false);
        break;
    }
    }

    if(nullptr == pNode)
    {
        Q_ASSERT("Failed to create new node" && false);
    }

    addNode(pNode);
    logicNodesUpdate();

    if(nullptr == pNode)
        return nullptr;

    pNode->setMapId(freeMapId());
    pNode->setState(ENodeState::eSelect);
    const QByteArray sourceDocumentSha256 = QByteArray::fromHex(
        enduranceProvenance.value("sourceDocumentSha256").toString().toLatin1());
    const QByteArray sourceSubtreeSha256 = QByteArray::fromHex(
        enduranceProvenance.value("sourceSubtreeSha256").toString().toLatin1());
    const QString sourceObjectPath = enduranceProvenance.value("sourceObjectPath").toString();
    if(sourceDocumentSha256.size() == 32 && sourceSubtreeSha256.size() == 32
       && !sourceObjectPath.isEmpty())
    {
        pNode->setRawObjectProvenance(sourceDocumentSha256, sourceObjectPath,
                                      sourceSubtreeSha256, true);
        const QByteArray sourceSubtree = QByteArray::fromBase64(
            enduranceProvenance.value("sourceSubtreeBase64").toString().toLatin1());
        const QByteArray cleanSubtree = QByteArray::fromBase64(
            enduranceProvenance.value("cleanSubtreeBase64").toString().toLatin1());
        if(!sourceSubtree.isEmpty() && !cleanSubtree.isEmpty()
           && QCryptographicHash::hash(sourceSubtree, QCryptographicHash::Sha256)
                == sourceSubtreeSha256)
            pNode->setRawObjectTemplateBytes(sourceSubtree, cleanSubtree);
    }


    return pNode;
}

void CMob::undo_createNode(uint mapId)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aNode)
    {
        if(pNode->mapId() == mapId)
        {
            //m_aDeletedNode.append(pNode);
            m_aNode.removeAt(m_aNode.indexOf(pNode));
            ei::log(eLogDebug, QString("undo create node with %1 map ID").arg(pNode->mapId()));
            logicNodesUpdate();
            return;
        }
    }
}

QList<CNode*>& CMob::nodes()
{
    return m_aNode;
}

QList<CNode *> &CMob::logicNodes()
{
    //collect logic nodes
    if(m_aLogicNode.isEmpty()) //can be optimized but need to control add\delete nodes/patrol points\view
        logicNodesUpdate();

    return m_aLogicNode;
}

void CMob::logicNodesUpdate()
{
    m_aLogicNode.clear();

    CNode* pNode = nullptr;
    foreach(pNode, m_aNode)
    {
        ENodeType type = pNode->nodeType();
        switch(type)
        {
        case ENodeType::eUnit:
        {
            m_aLogicNode.append(pNode);
            CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
            Q_ASSERT(pUnit);
            pUnit->collectLogicNodes(m_aLogicNode);
            break;
        }
        case ENodeType::eMagicTrap:
        {
            m_aLogicNode.append(pNode);
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            pTrap->collectLogicNodes(m_aLogicNode);
            break;
        }
        default:
            break;
        }
    }
}

void CMob::deleteNode(uint mapId)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aNode)
    {
        if(pNode->mapId() == mapId)
        {
            pNode->setState(ENodeState::eDraw); // for restoring draw state
            m_aDeletedNode.append(pNode);
            m_aNode.removeOne(pNode);
            ei::log(eLogDebug, QString("delete node with %1").arg(pNode->mapId()));
            logicNodesUpdate();
            break;
        }
    }
}

CNode *CMob::undo_deleteNode(uint mapId, int restoreIndex)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aDeletedNode)
    {
        if(pNode->mapId() == mapId)
        {
            if(restoreIndex >= 0 && restoreIndex <= m_aNode.size())
                m_aNode.insert(restoreIndex, pNode);
            else
                m_aNode.append(pNode);
            m_aDeletedNode.removeOne(pNode);
            ei::log(eLogDebug, QString("node with %1 restored").arg(pNode->mapId()));
            logicNodesUpdate();
            return pNode;
        }
    }
    return nullptr;
}

int CMob::readMob(QFileInfo &path)
{
    if (!path.exists())
        return 1;

    m_filePath = path;
    QFile file(m_filePath.filePath());
    try
    {
        file.open(QIODevice::ReadOnly);
        if (file.error() != QFile::NoError)
        {
            qDebug() << file.fileName() << " Error while open mob-file";
            return 2;
        }

        m_originalData = file.readAll();
        CValidationReport structuralReport;
        CMobDocument structuralDocument;
        if(!structuralDocument.load(m_filePath, &structuralReport))
        {
            ei::log(eLogFatal, QString("Refusing MOB open after strict structural validation failed path:%1 findings:%2")
                .arg(m_filePath.absoluteFilePath(), structuralReport.toText().trimmed()));
            QMessageBox::critical(nullptr, "Invalid MOB structure",
                QString("Endurance could not account for the complete MOB structure. The file was not opened.\n\n%1")
                    .arg(structuralReport.toText().trimmed()));
            file.close();
            return 3;
        }
        for(const SValidationFinding& finding : structuralReport.findings())
        {
            if(finding.severity != EValidationSeverity::Warning)
                continue;
            ei::log(eLogWarning,
                    QString("MOB validation warning code:%1 path:%2 location:%3 message:%4")
                        .arg(finding.code, m_filePath.absoluteFilePath(),
                             finding.location, finding.message));
        }
        const SBackupResult openBackup = CBackupManager().snapshotOnOpen(m_filePath);
        if(!openBackup.success)
        {
            ei::log(eLogWarning, QString("MOB opened without recovery snapshot:%1 error:%2")
                .arg(m_filePath.absoluteFilePath(), openBackup.error));
            QMessageBox::warning(nullptr, "Endurance backup warning",
                QString("The MOB was opened, but its recovery snapshot could not be created.\n\n%1\n\n"
                        "Do not edit or save this file until the backup location is writable.")
                    .arg(openBackup.error));
        }
        if(!deserialize(m_originalData))
        {
            ei::log(eLogFatal, QString("Legacy MOB projection stopped before completing path:%1")
                .arg(m_filePath.absoluteFilePath()));
            QMessageBox::critical(nullptr, "Unsupported MOB projection",
                "The MOB is structurally valid, but the current editor projection could not interpret it completely. "
                "No editable document was opened; details are in logs/workflow.log.");
            file.close();
            return 4;
        }

        QByteArray cleanProjectionBytes;
        serializeMob(cleanProjectionBytes, false);
        CMobDocument cleanProjectionDocument;
        CValidationReport projectionReport;
        const bool projectionParsed = cleanProjectionDocument.loadBytes(
            cleanProjectionBytes, "clean-ui-projection.mob", &projectionReport);
        const QVector<SMobObjectSnapshot>& rawObjects = structuralDocument.objects();
        const QByteArray sourceDocumentSha256 = QCryptographicHash::hash(
            m_originalData, QCryptographicHash::Sha256);
        QVector<int> projectedToRaw(m_aNode.size(), -1);
        bool provenanceAligned = projectionParsed
                && rawObjects.size() == m_aNode.size()
                && cleanProjectionDocument.objects().size() == m_aNode.size();
        if(provenanceAligned)
        {
            const CMobSemanticDiff alignment = CMobSemanticDiff::compare(
                structuralDocument, cleanProjectionDocument);
            QSet<int> usedRawObjects;
            for(const SMobDiffEntry& entry : alignment.entries())
            {
                if(entry.baselineIndex < 0 || entry.candidateIndex < 0
                   || entry.candidateIndex >= projectedToRaw.size()
                   || usedRawObjects.contains(entry.baselineIndex)
                   || projectedToRaw.at(entry.candidateIndex) >= 0)
                {
                    provenanceAligned = false;
                    break;
                }
                projectedToRaw[entry.candidateIndex] = entry.baselineIndex;
                usedRawObjects.insert(entry.baselineIndex);
            }
            provenanceAligned = provenanceAligned
                    && usedRawObjects.size() == rawObjects.size()
                    && !projectedToRaw.contains(-1);
        }
        if(provenanceAligned)
        {
            for(int projectedIndex = 0; projectedIndex < projectedToRaw.size(); ++projectedIndex)
            {
                const SMobObjectSnapshot& rawObject = rawObjects.at(
                    projectedToRaw.at(projectedIndex));
                m_aNode.at(projectedIndex)->setRawObjectProvenance(
                    sourceDocumentSha256, rawObject.sectionPath,
                    rawObject.subtreeSha256, false);
            }
            ei::log(eLogInfo, QString("Bound raw MOB provenance to %1 projected objects sourceSha256:%2")
                .arg(rawObjects.size())
                .arg(QString::fromLatin1(sourceDocumentSha256.toHex())));
        }
        else
        {
            ei::log(eLogWarning, QString("Raw MOB provenance binding unavailable because semantic projection alignment was incomplete path:%1 rawObjects:%2 projectedObjects:%3 findings:%4")
                .arg(m_filePath.absoluteFilePath()).arg(rawObjects.size()).arg(m_aNode.size())
                .arg(projectionReport.toText().trimmed()));
        }

        file.close();
    }
    catch (const std::exception& ex)
    {
        ei::log(eLogFatal, QString("Exception while opening MOB path:%1 error:%2")
            .arg(m_filePath.absoluteFilePath(), QString::fromLocal8Bit(ex.what())));
        file.close();
        return 5;
    }
    m_duplicateIds = findIdDuplicate();
    if(!m_duplicateIds.isEmpty())
    {
        QMessageBox::warning(nullptr, "Duplicate object IDs",
            ei::duplicateIdWarningText(m_duplicateIds));
    }
    updateObjects();
    logicNodesUpdate();
    captureCleanProjection();
    return 0;
}

CNode* CMob::restoreDeletedNode(CNode* pNode, int restoreIndex)
{
    if(!pNode || m_aNode.contains(pNode) || !m_aDeletedNode.contains(pNode))
        return nullptr;

    if(restoreIndex >= 0 && restoreIndex <= m_aNode.size())
        m_aNode.insert(restoreIndex, pNode);
    else
        m_aNode.append(pNode);
    m_aDeletedNode.removeOne(pNode);
    logicNodesUpdate();
    ei::log(eLogDebug, QString("exact node with %1 restored").arg(pNode->mapId()));
    return pNode;
}

QVector<uint> CMob::checkUniqueId(QSet<uint> &aId)
{
    QVector<uint> duplicates;
    for(int objectIndex = 0; objectIndex < m_aNode.size(); ++objectIndex)
    {
        CNode* node = m_aNode.at(objectIndex);
        if(aId.contains(node->mapId()))
        {
            duplicates.append(node->mapId());
            ei::log(eLogWarning,
                QString("Duplicate object ID during save validation mob:%1 objectIndex:%2 objectName:%3 id:%4")
                    .arg(mobName())
                    .arg(objectIndex)
                    .arg(node->mapName())
                    .arg(node->mapId()));
        }
        else
            aId.insert(node->mapId());
    }
    return duplicates;
}

QString CMob::getAuxDirName()
{
    return "aux_files";
}

/// mob - inout. json object of Mob file
/// file - in. map fileName
/// key - type of data
void CMob::writeData(QJsonObject& mob, const QFileInfo& file, const QString key, const QString value)
{
    const QString fileRelPath = getAuxDirName() + QDir::separator() + file.baseName() + '.' + key + ".txt";
    const QFileInfo auxFolder(file.dir().path() + QDir::separator() + getAuxDirName());
    if (!auxFolder.exists())
    {
        QDir().mkdir(auxFolder.filePath());
    }

    QFile temp(file.dir().path() + QDir::separator() + fileRelPath);

    try
    {
        temp.open(QIODevice::WriteOnly);
        if (temp.error() != QFile::NoError)
        {
            qDebug() << file.fileName() << " Error writing script";
            return;
        }

        //convert string to unicode(win-1251 to utf-8)
        QTextCodec* defaultTextCodec = QTextCodec::codecForName("Windows-1251");
        QTextDecoder *decoder = new QTextDecoder(defaultTextCodec);
        QString str = decoder->toUnicode(value.toLatin1());

        QTextStream stream(&temp);
        stream << str;
        temp.close();
    }
    catch (std::exception ex)
    {
        qDebug() << ex.what();
        temp.close();
    }
    mob.insert(key, fileRelPath);
}

/// mob - inout. json object of Mob file
/// file - in. map fileName
/// key - type of data
void CMob::writeData(QJsonObject& mob, const QFileInfo& file, const QString key, QByteArray& value)
{
    const QString fileRelPath = getAuxDirName() + QDir::separator() + file.baseName() + '.' + key;
    const QFileInfo auxFolder(file.dir().path() + QDir::separator() + getAuxDirName());
    if (!auxFolder.exists())
    {
        QDir().mkdir(auxFolder.filePath());
    }

    QFile temp(file.dir().path() + QDir::separator() + fileRelPath);
    try
    {
        temp.open(QIODevice::WriteOnly);
        if (temp.error() != QFile::NoError)
        {
            qDebug() << file.fileName() << " Error writing aux file";
            return;
        }
        temp.write(value);
        temp.close();
    }
    catch (std::exception ex)
    {
        qDebug() << ex.what();
        temp.close();
    }
    mob.insert(key, fileRelPath);
}

bool CMob::isFreeMapId(uint id)
{
    CNode* pNode = nullptr;
    foreach(pNode, m_aNode)
    {
        if(pNode->mapId() == id)
            return false;
    }
    return true;
}

uint CMob::freeMapId()
{
    uint id = 0; //future map ID
    // try to find suit id from active range
    const SRange& range = activeRange();
    for(uint i(range.minRange); i<range.maxRange; ++i)
    {
        if(isFreeMapId(i))
        {
            id = i;
            ei::log(ELogMessageType::eLogInfo, "Found map ID from active range:" + QString::number(i));
            break;
        }
    }
    //TODO: we MUST guarantee setting free id or undo creating object

    // try to find suit id from any mob range. TODO: delete this
    if (id == 0)
    {
        QVector<SRange> arrRange;
        for(auto& range : m_aMainRange)
            arrRange.append(SRange(range));
        for(auto& range : m_aSecRange)
            arrRange.append(SRange(range));

        for(auto& range : arrRange)
        {
            if(id > 0)
                break;

            for(uint i(range.minRange); i<range.maxRange; ++i)
                if(isFreeMapId(i))
                {
                    id = i;
                    ei::log(ELogMessageType::eLogWarning, "Found map ID from any mob ranges:" + QString::number(i));
                    break;
                }
        }
    }


    if(id == 0)
    { // try to set any free number
        uint freeId(1000);
        QVector<uint> arrId;
        arrId.resize(m_aNode.size());

        for (int i(0); i<m_aNode.size(); ++i)
            arrId[i] = m_aNode[i]->mapId();

        for (; freeId<100000; ++freeId)
        {
            if(!arrId.contains(freeId))
            {
                id = freeId;
                break;
            }
        }
    }
    if(id == 0)
    {
        ei::log(ELogMessageType::eLogFatal, "Cant find suit ID for object");
        Q_ASSERT(false);
    }
    if(m_view)
        id = m_view->suggestFamilyNid(this, id);
    return id;
}

QVector<uint> CMob::findIdDuplicate()
{
    QSet<uint> arrId;
    QVector<uint> arrDuplicate;
    uint mapId;
    CNode* pNode = nullptr;
    int objectIndex = 0;
    foreach(pNode, m_aNode)
    {
        mapId = pNode->mapId();
        if(arrId.contains(mapId))
        {
            ei::log(eLogWarning,
                QString("Duplicate object ID during load validation mob:%1 objectIndex:%2 objectName:%3 id:%4")
                    .arg(mobName())
                    .arg(objectIndex)
                    .arg(pNode->mapName())
                    .arg(mapId));
            arrDuplicate.append(mapId);
        }
        else
            arrId.insert(mapId);
        ++objectIndex;
    }
    return arrDuplicate;
}

/// file - inout. mob file
void CMob::serializeJson(const QFileInfo& file)
{
    //ranges
    QJsonArray aRange;
    auto prepareRange = [&aRange](QVector<SRange>& rangeList)
    {
        aRange = QJsonArray();
        for(auto& mainRange : rangeList)
        {
            QJsonObject range;
            range.insert("min", QJsonValue::fromVariant(mainRange.minRange));
            range.insert("max", QJsonValue::fromVariant(mainRange.maxRange));
            aRange.append(range);
        }
    };

    QJsonObject rangeObj;
    prepareRange(m_aMainRange);
    rangeObj.insert("Main ranges", aRange);
    prepareRange(m_aSecRange);
    rangeObj.insert("Sec ranges", aRange);
    QJsonObject mob;
    mob.insert("Ranges", rangeObj);

    //scripts
    writeData(mob, file, "Script", m_script);
    writeData(mob, file, "Old_script", m_textOld);

    QJsonArray aDiplomacyName;
    for(auto& name : m_aDiplomacyFieldName)
    {
        aDiplomacyName.append(name);
    }
    mob.insert("Diplomacy Names", aDiplomacyName);

    QJsonArray diplomacyTable;
    for(auto& line: m_diplomacyFoF)
    {
        QString rowStr;
        for(auto& row: line)
          rowStr += QString::number(row);
        diplomacyTable.append(rowStr);

    }
    mob.insert("Diplomacy table", diplomacyTable);

    QJsonObject worldSetObj;
    m_worldSet.serializeJson(worldSetObj);
    mob.insert("World set", worldSetObj);

    //binary aux data
    writeData(mob, file, "vss", m_vss_section);
    writeData(mob, file, "dir", m_directory);
    writeData(mob, file, "dirElem", m_directoryElements);
    writeData(mob, file, "graph", m_aiGraph);

    //units
    QJsonArray unitArr;
    for (auto& node: m_aNode)
    {
        QJsonObject unitObj;
        node->serializeJson(unitObj);
        unitArr.append(unitObj);
    }

    mob.insert("Objects", unitArr);

    QJsonDocument doc(mob);
    QFile f(file.absoluteFilePath());
    if (!f.open(QIODevice::WriteOnly)) {
        Q_ASSERT("Couldn't open option file." && false);
        return;
    }
    f.write(doc.toJson(QJsonDocument::JsonFormat::Indented));
    f.close();
}

void CMob::serializeMob(QByteArray& data, bool writeLog)
{
    if(writeLog)
        ei::log(eLogInfo, "Start write mob");
    uint writeByte(0);
    util::CMobParser parser(data, true);
    //header "OBJECT_DB_FILE"
    writeByte += parser.startSection("OBJECT_DB_FILE");

    switch (m_mobType) {
    case eEMobTypeQuest:
        writeByte += parser.startSection("PR_OBJECT_DB_FILE");
        break;
    case eEMobTypeBase:
        writeByte += parser.startSection("SC_OBJECT_DB_FILE");
        break;
    }
    parser.endSection(); //"PR/SC_OBJECT_DB_FILE"

    if (!m_script.isEmpty())
    {
        writeByte += parser.startSection("SS_TEXT");
        writeByte += parser.writeStringEncrypted(m_script, m_scriptKey);
        parser.endSection(); //SS_TEXT
    }


    if (!m_textOld.isEmpty())
    {
        writeByte += parser.startSection("SS_TEXT_OLD");
        writeByte += parser.writeString(m_textOld);
        parser.endSection(); //SS_TEXT_OLD
    }


    if (!m_aMainRange.isEmpty())
    {//main range
        writeByte += parser.startSection("MAIN_RANGE");
        for (const auto& elem: m_aMainRange)
        {
            writeByte += parser.startSection("RANGE");
            {
                writeByte += parser.startSection("MIN_ID");
                writeByte += parser.writeDword(elem.minRange);
                parser.endSection();

                writeByte += parser.startSection("MAX_ID");
                writeByte += parser.writeDword(elem.maxRange);
                parser.endSection();
            }
            parser.endSection();
        }
        parser.endSection();
    }

    if (!m_aSecRange.isEmpty())
    {
        writeByte += parser.startSection("SEC_RANGE");
        for (const auto& elem: m_aSecRange)
        {
            writeByte += parser.startSection("RANGE");
            {
                writeByte += parser.startSection("MIN_ID");
                writeByte += parser.writeDword(elem.minRange);
                parser.endSection();

                writeByte += parser.startSection("MAX_ID");
                writeByte += parser.writeDword(elem.maxRange);
                parser.endSection();
            }
            parser.endSection(); // RANGE
        }
        parser.endSection();//sec range
    }

    if (!m_diplomacyFoF.empty() || !m_aDiplomacyFieldName.isEmpty())
    {
        writeByte += parser.startSection("DIPLOMATION");
        //Diplomacy table
        if (!m_diplomacyFoF.empty())
        {
            writeByte += parser.startSection("DIPLOMATION_FOF");
            writeByte += parser.writeDiplomacy(m_diplomacyFoF);
            parser.endSection();
        }

        //Diplomacy Names
        if (!m_aDiplomacyFieldName.isEmpty())
        {
            writeByte += parser.startSection("DIPLOMATION_PL_NAMES");
            writeByte += parser.writeStringArray(m_aDiplomacyFieldName, "DIPLOMATION_PL_NAMES");
            parser.endSection();
        }
        parser.endSection();
    }

    if(m_mobType == eEMobTypeBase)
    {
        writeByte += parser.startSection("WORLD_SET");
        writeByte += parser.startSection("WS_WIND_DIR");
        writeByte += parser.writePlot(util::vec3FromString(m_worldSet.data(eWsTypeWindDir)));
        parser.endSection(); //"WS_WIND_DIR"

        writeByte += parser.startSection("WS_WIND_STR");
        writeByte += parser.writeFloat(m_worldSet.data(eWsTypeWindStr).toFloat());
        parser.endSection(); //"WS_WIND_STR"

        writeByte += parser.startSection("WS_TIME");
        writeByte += parser.writeFloat(m_worldSet.data(eWsTypeTime).toFloat());
        parser.endSection(); //"WS_TIME"

        writeByte += parser.startSection("WS_AMBIENT");
        writeByte += parser.writeFloat(m_worldSet.data(eWsTypeAmbient).toFloat());
        parser.endSection(); //"WS_AMBIENT"

        writeByte += parser.startSection("WS_SUN_LIGHT");
        writeByte += parser.writeFloat(m_worldSet.data(eWsTypeSunLight).toFloat());
        parser.endSection(); //Sun Light
        parser.endSection();//"WORLD_SET"
    }

    if (m_hasVssSection || !m_vss_section.isEmpty())
    {
        writeByte += parser.startSection("VSS_SECTION");
        writeByte += parser.writeByteArray(m_vss_section, uint(m_vss_section.size()));
        parser.endSection(); //"VSS_SECTION"
    }

    if (!m_directory.isEmpty())
    {
        writeByte += parser.startSection("DIRICTORY");
        writeByte += parser.writeByteArray(m_directory, uint(m_directory.size()));
        parser.endSection();//"DIRICTORY"
    }

    if (!m_directoryElements.isEmpty())
    {
        writeByte += parser.startSection("DIRICTORY_ELEMENTS");
        writeByte += parser.writeByteArray(m_directoryElements, uint(m_directoryElements.size()));
        parser.endSection(); //"DIRICTORY_ELEMENTS"
    }

    if(!m_aNode.empty())
    {
        bool bSaveSelect = false; //todo: save selected objects with file->save selected objects only

        writeByte += parser.startSection("OBJECT_SECTION");
        for (const auto& node : m_aNode)
        {

            if (bSaveSelect && (node->nodeState() != ENodeState::eSelect)) //save selected objects only
                continue;

            writeByte += node->serialize(parser);
        }
        parser.endSection(); // OBJECT_SECTION
    }
    //selected nodes?

    parser.endSection(); //OBJECT_DB_FILE

    //graph
    if (!m_aiGraph.isEmpty())
    {
        writeByte += parser.startSection("AI_GRAPH");
        writeByte += parser.writeAiGraph(m_aiGraph, m_aiGraph.length());
        parser.endSection();
    }
    if(writeLog)
        ei::log(eLogInfo, "End write mob");
    return;
}

void CMob::captureCleanProjection()
{
    m_cleanProjection.clear();
    serializeMob(m_cleanProjection, false);
    m_bDirty = false;
}

bool CMob::refreshDirtyFromProjection()
{
    const bool wasDirty = m_bDirty;
    if(m_cleanProjection.isEmpty())
    {
        m_bDirty = true;
        return m_bDirty;
    }

    QByteArray currentProjection;
    serializeMob(currentProjection, false);
    m_bDirty = currentProjection != m_cleanProjection;
    if(wasDirty != m_bDirty)
    {
        ei::log(eLogInfo, QString("MOB projected dirty state changed mob:%1 dirty:%2")
                .arg(mobName()).arg(m_bDirty ? "true" : "false"));
    }
    return m_bDirty;
}

void CMob::createNode(CNode *pNode)
{
    CNode* pNewNode = nullptr;
    ENodeType type = pNode->nodeType();
    if (type == ENodeType::eUnknown)
    {
        qDebug() << "cant recognize type of obj";
        return;
    }

    switch (type)
    {
    case ENodeType::eUnit:
    {
        pNewNode = new CUnit(*dynamic_cast<CUnit*>(pNode));
        break;
    }
    case ENodeType::eTorch:
    {
        pNewNode = new CTorch(*dynamic_cast<CTorch*>(pNode));
        break;
    }
    case ENodeType::eMagicTrap:
    {
        pNewNode = new CMagicTrap(*dynamic_cast<CMagicTrap*>(pNode));
        break;
    }
    case ENodeType::eLever:
    {
        pNewNode = new CLever(*dynamic_cast<CLever*>(pNode));
        break;
    }
    case ENodeType::eLight:
    {
        pNewNode = new CLight(*dynamic_cast<CLight*>(pNode));
        break;
    }
    case ENodeType::eSound:
    {
        pNewNode = new CSound(*dynamic_cast<CSound*>(pNode));
        break;
    }
    case ENodeType::eParticle:
    {
        pNewNode = new CParticle(*dynamic_cast<CParticle*>(pNode));
        break;
    }
    case ENodeType::eWorldObject:
    {
        pNewNode = new CWorldObj(*dynamic_cast<CWorldObj*>(pNode));
        break;
    }
    default:
    {
        Q_ASSERT("unknown node type" && false);
        break;
    }
    }

    if(nullptr == pNode)
    {
        Q_ASSERT("Failed to create new node" && false);
    }
    addNode(pNewNode);

    if(nullptr == pNewNode)
        return;

    pNewNode->setMapId(freeMapId());
    pNewNode->setState(ENodeState::eSelect);


    return;
}

void CMob::deleteNode(CNode *pNode)
{
    const int ind = m_aNode.indexOf(pNode);
    if(ind >=0)
    {
        if(!m_aDeletedNode.contains(pNode))
            m_aDeletedNode.append(pNode);
        //m_aNode.at(ind)->setState(ENodeState::eDraw);
        m_aNode.removeAt(ind);
        ei::log(eLogDebug, QString("delete node with %1").arg(pNode->mapId()));
        logicNodesUpdate();
    }
}

void CMob::discardUncommittedNode(CNode* pNode)
{
    if(!pNode)
        return;
    const bool removedActive = m_aNode.removeOne(pNode);
    const bool removedDeleted = m_aDeletedNode.removeOne(pNode);
    if(!removedActive && !removedDeleted)
        return;
    logicNodesUpdate();
    delete pNode;
}

void CMob::clearSelect(bool bClearLogic)
{
    CNode* pNode;
    foreach (pNode, m_aNode)
    {
        if(pNode->nodeState() == ENodeState::eSelect)
            pNode->setState(ENodeState::eDraw);
        if(bClearLogic && pNode->nodeType() == ENodeType::eUnit)
        {
            CUnit* pUnit = dynamic_cast<CUnit*>(pNode);
            pUnit->clearLogicSelect();
        }
        else if(bClearLogic && pNode->nodeType() == ENodeType::eMagicTrap)
        {
            CMagicTrap* pTrap = dynamic_cast<CMagicTrap*>(pNode);
            pTrap->clearLogicSelect();
        }
    }
}

bool CMob::buildRawObjectSequenceCandidate(
    const QByteArray& currentProjection, CMobEditTransaction* transaction,
    QByteArray* candidate, CValidationReport* report, QString* saveClass) const
{
    if(!transaction || !candidate || !saveClass)
        return false;
    *transaction = CMobEditTransaction("raw object sequence save");
    candidate->clear();
    saveClass->clear();

    CMobDocument sourceDocument;
    CMobDocument cleanDocument;
    CMobDocument editedDocument;
    if(!sourceDocument.loadBytes(m_originalData, m_filePath.filePath(), report) ||
       !cleanDocument.loadBytes(m_cleanProjection, "clean-ui-projection.mob", report) ||
       !editedDocument.loadBytes(currentProjection, "edited-ui-projection.mob", report))
        return false;

    QMap<QString, const SMobObjectSnapshot*> cleanBySourcePath;
    const CMobSemanticDiff alignment = CMobSemanticDiff::compare(sourceDocument, cleanDocument);
    for(const SMobDiffEntry& entry : alignment.entries())
    {
        if(entry.baselineIndex < 0 || entry.candidateIndex < 0)
            return false;
        const SMobObjectSnapshot& source = sourceDocument.objects().at(entry.baselineIndex);
        const SMobObjectSnapshot& clean = cleanDocument.objects().at(entry.candidateIndex);
        if(cleanBySourcePath.contains(source.sectionPath))
            return false;
        cleanBySourcePath.insert(source.sectionPath, &clean);
    }
    if(cleanBySourcePath.size() != sourceDocument.objects().size())
        return false;

    const QByteArray sourceDocumentSha256 = QCryptographicHash::hash(
        m_originalData, QCryptographicHash::Sha256);
    QVector<QByteArray> sourceSubtrees;
    QVector<QByteArray> sourceHashes;
    QVector<QByteArray> cleanSubtrees;
    QVector<QByteArray> cleanHashes;
    sourceSubtrees.reserve(m_aNode.size());
    sourceHashes.reserve(m_aNode.size());
    cleanSubtrees.reserve(m_aNode.size());
    cleanHashes.reserve(m_aNode.size());
    for(CNode* node : m_aNode)
    {
        if(!node || !node->hasRawObjectProvenance())
        {
            if(report)
                report->add("mob.object_sequence.ui_provenance", EValidationSeverity::Error,
                            "A current object has no matching raw source subtree",
                            node ? QString::number(node->mapId()) : QString("null"));
            return false;
        }
        QByteArray sourceRaw;
        QByteArray cleanRaw;
        QByteArray cleanHash;
        if(node->rawSourceDocumentSha256() == sourceDocumentSha256
           && cleanBySourcePath.contains(node->rawSourceObjectPath()))
        {
            const SMobObjectSnapshot* clean = cleanBySourcePath.value(
                node->rawSourceObjectPath());
            if(!sourceDocument.objectSubtreeBytes(node->rawSourceObjectPath(),
                                                  node->rawSourceSubtreeSha256(),
                                                  &sourceRaw)
               || !cleanDocument.objectSubtreeBytes(clean->sectionPath,
                                                     clean->subtreeSha256,
                                                     &cleanRaw))
                return false;
            cleanHash = clean->subtreeSha256;
        }
        else if(node->hasRawObjectTemplateBytes())
        {
            sourceRaw = node->rawTemplateSourceSubtree();
            cleanRaw = node->rawTemplateCleanSubtree();
            if(QCryptographicHash::hash(sourceRaw, QCryptographicHash::Sha256)
                   != node->rawSourceSubtreeSha256())
            {
                if(report)
                    report->add("mob.object_sequence.external_template", EValidationSeverity::Error,
                                "A cross-MOB raw object template failed its provenance hash",
                                QString::number(node->mapId()));
                return false;
            }
            cleanHash = QCryptographicHash::hash(cleanRaw, QCryptographicHash::Sha256);
        }
        else
        {
            if(report)
                report->add("mob.object_sequence.ui_provenance", EValidationSeverity::Error,
                            "A current object has no matching local or cross-MOB raw source subtree",
                            QString::number(node->mapId()));
            return false;
        }
        sourceSubtrees.append(sourceRaw);
        sourceHashes.append(node->rawSourceSubtreeSha256());
        cleanSubtrees.append(cleanRaw);
        cleanHashes.append(cleanHash);
    }

    QByteArray structuralSource;
    QByteArray structuralClean;
    if(!sourceDocument.rebuildObjectSequenceFromRaw(sourceSubtrees, sourceHashes,
                                                    &structuralSource, report) ||
       !cleanDocument.rebuildObjectSequenceFromRaw(cleanSubtrees, cleanHashes,
                                                   &structuralClean, report))
        return false;

    CMobDocument structuralSourceDocument;
    CMobDocument structuralCleanDocument;
    if(!structuralSourceDocument.loadBytes(structuralSource,
                                           "raw-object-sequence.mob", report) ||
       !structuralCleanDocument.loadBytes(structuralClean,
                                          "clean-object-sequence.mob", report))
        return false;
    QByteArray reconciledSource;
    QByteArray reconciledClean;
    if(!structuralSourceDocument.reconcileLogicPointStructure(
           structuralCleanDocument, editedDocument, &reconciledSource, report) ||
       !structuralCleanDocument.reconcileLogicPointStructure(
           structuralCleanDocument, editedDocument, &reconciledClean, report))
        return false;
    structuralSource = reconciledSource;
    structuralClean = reconciledClean;
    if(structuralSource == m_originalData && structuralClean == m_cleanProjection)
        return false;

    if(currentProjection == structuralClean ||
       (m_aNode.isEmpty() && editedDocument.objects().isEmpty()))
    {
        *candidate = structuralSource;
        *saveClass = "RAW_OBJECT_SEQUENCE";
    }
    else
    {
        if(!CMobDocument::buildProjectionTransactionCandidate(
               structuralSource, m_filePath.filePath(), structuralClean,
               currentProjection, transaction, candidate, report) ||
           !transaction->validatedChangedSaveClass(saveClass))
            return false;
        *saveClass = "RAW_OBJECT_SEQUENCE+" + *saveClass;
    }

    CMobDocument candidateDocument;
    if(!candidateDocument.loadBytes(*candidate, "object-sequence-candidate.mob", report) ||
       candidateDocument.objects().size() != m_aNode.size())
    {
        candidate->clear();
        return false;
    }
    return true;
}

bool CMob::buildValidatedCandidate(QByteArray* data,
                                   CValidationReport* report,
                                   QString* saveClass)
{
    if(!data)
        return false;
    data->clear();
    if(saveClass)
        saveClass->clear();

    QByteArray currentProjection;
    serializeMob(currentProjection, false);
    const bool projectionDirty = m_cleanProjection.isEmpty()
        || currentProjection != m_cleanProjection;
    if(!projectionDirty && !m_originalData.isEmpty())
    {
        *data = m_originalData;
        if(saveClass)
            *saveClass = "BYTE_IDENTICAL";
        ei::log(eLogInfo, "Prepared byte-identical MOB candidate (document is unchanged)");
    }
    else
    {
        CValidationReport transactionReport;
        CMobEditTransaction transaction;
        QByteArray transactionCandidate;
        bool candidateReady = CMobDocument::buildProjectionTransactionCandidate(
            m_originalData, m_filePath.filePath(), m_cleanProjection,
            currentProjection, &transaction, &transactionCandidate, &transactionReport);
        QString validatedClassName;
        if(candidateReady)
            candidateReady = transaction.validatedChangedSaveClass(&validatedClassName);
        if(!candidateReady)
        {
            CValidationReport structuralReport;
            CMobEditTransaction structuralTransaction;
            QByteArray structuralCandidate;
            QString structuralClass;
            if(buildRawObjectSequenceCandidate(currentProjection,
                                               &structuralTransaction,
                                               &structuralCandidate,
                                               &structuralReport,
                                               &structuralClass))
            {
                transaction = structuralTransaction;
                transactionCandidate = structuralCandidate;
                validatedClassName = structuralClass;
                transactionReport = structuralReport;
                candidateReady = true;
            }
            else
                transactionReport = structuralReport;
        }
        if(candidateReady)
        {
            const QString candidateHash = QString::fromLatin1(
                QCryptographicHash::hash(transactionCandidate, QCryptographicHash::Sha256).toHex());
            ei::log(eLogInfo, QString("MOB transaction candidate validated path:%1 edits:%2 size:%3 sha256:%4")
                .arg(m_filePath.filePath()).arg(transaction.edits().size())
                .arg(transactionCandidate.size()).arg(candidateHash));
            for(int editIndex = 0; editIndex < transaction.edits().size(); ++editIndex)
            {
                const SMobLeafEdit& edit = transaction.edits().at(editIndex);
                const QString replacementHash = QString::fromLatin1(
                    QCryptographicHash::hash(edit.replacementPayload,
                                             QCryptographicHash::Sha256).toHex());
                ei::log(eLogInfo, QString("MOB transaction leaf edit index:%1/%2 type:0x%3 path:%4 sourceSectionSha256:%5 replacementSize:%6 replacementSha256:%7")
                    .arg(editIndex + 1).arg(transaction.edits().size())
                    .arg(edit.sectionType, 8, 16, QLatin1Char('0'))
                    .arg(edit.sectionPath,
                         QString::fromLatin1(edit.expectedSectionSha256.toHex()))
                    .arg(edit.replacementPayload.size()).arg(replacementHash));
            }
            *data = transactionCandidate;
            if(saveClass)
                *saveClass = validatedClassName;
            ei::log(eLogInfo, QString("MOB transaction candidate authorized class:%1 path:%2 edits:%3")
                .arg(validatedClassName, m_filePath.filePath()).arg(transaction.edits().size()));
        }
        else
        {
            if(report)
                *report = transactionReport;
            ei::log(eLogWarning, QString("MOB transaction candidate not applicable path:%1 findings:%2")
                .arg(m_filePath.filePath(), transactionReport.toText().trimmed()));
            return false;
        }
    }

    if(data->size() < 16 || (!m_originalData.isEmpty() && data->left(4) != m_originalData.left(4)))
    {
        if(report)
            report->add("mob.candidate.envelope", EValidationSeverity::Error,
                        "The generated MOB candidate failed its envelope check",
                        m_filePath.filePath());
        data->clear();
        return false;
    }
    CValidationReport freshReport;
    CMobDocument freshDocument;
    if(!freshDocument.loadBytes(*data, "in-memory-save-candidate.mob", &freshReport))
    {
        if(report)
            *report = freshReport;
        data->clear();
        return false;
    }
    return true;
}

bool CMob::saveAs(const QFileInfo& path, const SaveProgress& progress)
{
    const QString filePath = path.filePath();
    try
    {
        QByteArray data;
        CValidationReport candidateReport;
        QString saveClass;
        if(progress) progress("Serializing verified MOB edits...", 0, 5);
        if(!buildValidatedCandidate(&data, &candidateReport, &saveClass))
        {
            ei::log(eLogFatal, QString("Refusing dirty MOB save because the edit could not be mapped safely to the original raw tree path:%1")
                .arg(filePath));
            QMessageBox::critical(nullptr, "Lossless MOB save not available for this edit",
                "Endurance could not map this complete edit set onto verified raw MOB records. "
                "Properties, transforms, same-MOB copy/paste, deletion, and known patrol/look-point "
                "changes save automatically. A newly created object requires a selected same-type raw "
                "template, and unrecognized structural records remain protected.\n\n"
                "No destination or backup file was changed. Endurance will not fall back to the legacy "
                "rebuilding serializer.\n\n" + candidateReport.toText().trimmed());
            return false;
        }

        if(progress) progress("Freshly validating the staged MOB...", 1, 5);
        const SStagedSaveValidation validation = validateStagedSave(data, path);
        if(!validation.success)
        {
            ei::log(eLogFatal, QString("Refusing MOB save because staged validation failed path:%1 error:%2")
                .arg(filePath, validation.error));
            QMessageBox::critical(nullptr, "Endurance safe save",
                QString("The staged MOB failed fresh validation, so the destination was not replaced.\n\n%1")
                    .arg(validation.error));
            return false;
        }
        ei::log(eLogInfo, QString("Staged MOB validation passed path:%1 size:%2 sha256:%3")
            .arg(filePath).arg(validation.size).arg(validation.sha256));

        if(progress) progress("Creating the pre-save backup...", 2, 5);
        if(QFileInfo(filePath).exists())
        {
            const SBackupResult saveBackup = CBackupManager().snapshotBeforeSave(QFileInfo(filePath));
            if(!saveBackup.success)
            {
                ei::log(eLogFatal, QString("Refusing MOB save because pre-save backup failed:%1 error:%2")
                    .arg(filePath, saveBackup.error));
                QMessageBox::critical(nullptr, "Endurance safe save",
                    QString("The pre-save backup failed, so the source file was not replaced.\n\n%1")
                        .arg(saveBackup.error));
                return false;
            }
        }

        if(progress) progress("Atomically replacing the destination...", 3, 5);
        QSaveFile file(filePath);
        if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            ei::log(eLogFatal, QString("Atomic MOB save failed:%1 error:%2").arg(filePath, file.errorString()));
            QMessageBox::critical(nullptr, "Endurance safe save", "Atomic save failed. The source file was not replaced.");
            return false;
        }
        m_originalData = data;
        captureCleanProjection();
        if(progress) progress("Rebinding saved MOB provenance...", 4, 5);
        if(!rebindRawObjectProvenance())
            ei::log(eLogWarning, QString("Saved MOB reopened, but raw object provenance could not be rebound path:%1")
                    .arg(filePath));
        if(progress) progress("MOB save complete.", 5, 5);
        return true;
    }
    catch (const std::exception& ex)
    {
        ei::log(eLogFatal, ex.what());
        QMessageBox::critical(nullptr, "Endurance safe save",
            QString("The MOB save failed before replacement.\n\n%1").arg(ex.what()));
        return false;
    }
}

bool CMob::save(const SaveProgress& progress)
{
    return saveAs(m_filePath, progress);
}
bool SMobParameterState::operator==(const SMobParameterState& other) const
{
    if(questMob != other.questMob || mainRanges != other.mainRanges
       || secondaryRanges != other.secondaryRanges || diplomacy != other.diplomacy
       || diplomacyNames != other.diplomacyNames || script != other.script
       || activeRangeId != other.activeRangeId)
        return false;
    for(int i = 0; i < eWsTypeCount; ++i)
    {
        CWorldSet left = worldSet;
        CWorldSet right = other.worldSet;
        if(left.data(static_cast<EWsType>(i)) != right.data(static_cast<EWsType>(i)))
            return false;
    }
    return true;
}

SMobParameterState CMob::parameterState()
{
    SMobParameterState state;
    state.questMob = isQuestMob();
    state.worldSet = m_worldSet;
    state.mainRanges = m_aMainRange;
    state.secondaryRanges = m_aSecRange;
    state.diplomacy = m_diplomacyFoF;
    state.diplomacyNames = m_aDiplomacyFieldName;
    state.script = m_script;
    state.activeRangeId = m_activeRangeId;
    return state;
}

void CMob::applyParameterState(const SMobParameterState& state)
{
    setQuestMob(state.questMob);
    m_worldSet = state.worldSet;
    m_aMainRange = state.mainRanges;
    m_aSecRange = state.secondaryRanges;
    m_diplomacyFoF = state.diplomacy;
    m_aDiplomacyFieldName = state.diplomacyNames;
    m_script = state.script;
    const QVector<SRange>& active = state.questMob ? m_aSecRange : m_aMainRange;
    m_activeRangeId = active.isEmpty() ? 0 : qMin(state.activeRangeId, uint(active.size() - 1));
    logicNodesUpdate();
}

bool CMob::rebindRawObjectProvenance()
{
    CValidationReport report;
    CMobDocument sourceDocument;
    CMobDocument projectionDocument;
    if(!sourceDocument.loadBytes(m_originalData, m_filePath.filePath(), &report)
       || !projectionDocument.loadBytes(m_cleanProjection, "saved-ui-projection.mob", &report)
       || sourceDocument.objects().size() != m_aNode.size()
       || projectionDocument.objects().size() != m_aNode.size())
        return false;

    QVector<int> projectedToSource(m_aNode.size(), -1);
    QSet<int> usedSource;
    const CMobSemanticDiff alignment = CMobSemanticDiff::compare(sourceDocument, projectionDocument);
    for(const SMobDiffEntry& entry : alignment.entries())
    {
        if(entry.baselineIndex < 0 || entry.candidateIndex < 0
           || entry.candidateIndex >= projectedToSource.size()
           || usedSource.contains(entry.baselineIndex)
           || projectedToSource.at(entry.candidateIndex) >= 0)
            return false;
        projectedToSource[entry.candidateIndex] = entry.baselineIndex;
        usedSource.insert(entry.baselineIndex);
    }
    if(projectedToSource.contains(-1) || usedSource.size() != m_aNode.size())
        return false;

    const QByteArray documentHash = QCryptographicHash::hash(m_originalData, QCryptographicHash::Sha256);
    for(int i = 0; i < projectedToSource.size(); ++i)
    {
        const SMobObjectSnapshot& object = sourceDocument.objects().at(projectedToSource.at(i));
        m_aNode.at(i)->setRawObjectProvenance(documentHash, object.sectionPath,
                                              object.subtreeSha256, false);
    }
    ei::log(eLogInfo, QString("Rebound raw MOB provenance after save objects:%1 sourceSha256:%2")
            .arg(m_aNode.size()).arg(QString::fromLatin1(documentHash.toHex())));
    return true;
}

bool CMob::rebaseCopiedObjectProvenance(CNode* node, const QString& templatePath,
                                        const QByteArray& templateSubtreeSha256)
{
    if(!node || templateSubtreeSha256.size() != 32 || m_originalData.isEmpty())
        return false;
    if(node->hasRawObjectTemplateBytes()
       && QCryptographicHash::hash(node->rawTemplateSourceSubtree(),
                                  QCryptographicHash::Sha256)
            == templateSubtreeSha256)
    {
        // A cross-MOB copy deliberately keeps the donor document identity and
        // its complete raw/clean subtrees. There is nothing to rebase into the
        // destination document until the first successful save.
        return true;
    }
    CValidationReport report;
    CMobDocument document;
    if(!document.loadBytes(m_originalData, m_filePath.filePath(), &report))
        return false;

    const SMobObjectSnapshot* selected = nullptr;
    for(const SMobObjectSnapshot& object : document.objects())
    {
        if(object.sectionPath == templatePath && object.subtreeSha256 == templateSubtreeSha256)
        {
            selected = &object;
            break;
        }
    }
    if(!selected)
    {
        for(const SMobObjectSnapshot& object : document.objects())
        {
            if(object.subtreeSha256 == templateSubtreeSha256)
            {
                selected = &object;
                break;
            }
        }
    }
    if(!selected)
    {
        ei::log(eLogWarning, QString("Could not rebase copied-object raw template mapId:%1 path:%2 hash:%3")
                .arg(node->mapId()).arg(templatePath,
                     QString::fromLatin1(templateSubtreeSha256.toHex())));
        return false;
    }

    const QByteArray documentHash = QCryptographicHash::hash(m_originalData,
                                                              QCryptographicHash::Sha256);
    node->setRawObjectProvenance(documentHash, selected->sectionPath,
                                 selected->subtreeSha256, true);
    ei::log(eLogInfo, QString("Rebased copied-object raw template after redo mapId:%1 path:%2 sourceSha256:%3")
            .arg(node->mapId()).arg(selected->sectionPath,
                 QString::fromLatin1(documentHash.toHex())));
    return true;
}

bool CMob::rawObjectTemplateBytes(const CNode* node, QByteArray* sourceSubtree,
                                  QByteArray* cleanSubtree) const
{
    if(!node || !sourceSubtree || !cleanSubtree || !node->hasRawObjectProvenance())
        return false;
    sourceSubtree->clear();
    cleanSubtree->clear();
    if(node->hasRawObjectTemplateBytes())
    {
        *sourceSubtree = node->rawTemplateSourceSubtree();
        *cleanSubtree = node->rawTemplateCleanSubtree();
        return QCryptographicHash::hash(*sourceSubtree, QCryptographicHash::Sha256)
                == node->rawSourceSubtreeSha256();
    }
    const QByteArray documentHash = QCryptographicHash::hash(
        m_originalData, QCryptographicHash::Sha256);
    if(node->rawSourceDocumentSha256() != documentHash)
        return false;

    CValidationReport report;
    CMobDocument sourceDocument;
    CMobDocument cleanDocument;
    if(!sourceDocument.loadBytes(m_originalData, m_filePath.filePath(), &report)
       || !cleanDocument.loadBytes(m_cleanProjection, "clean-ui-projection.mob", &report)
       || !sourceDocument.objectSubtreeBytes(node->rawSourceObjectPath(),
                                             node->rawSourceSubtreeSha256(),
                                             sourceSubtree))
        return false;

    const CMobSemanticDiff alignment = CMobSemanticDiff::compare(sourceDocument, cleanDocument);
    for(const SMobDiffEntry& entry : alignment.entries())
    {
        if(entry.baselineIndex < 0 || entry.candidateIndex < 0)
            continue;
        const SMobObjectSnapshot& source = sourceDocument.objects().at(entry.baselineIndex);
        if(source.sectionPath != node->rawSourceObjectPath())
            continue;
        const SMobObjectSnapshot& clean = cleanDocument.objects().at(entry.candidateIndex);
        return cleanDocument.objectSubtreeBytes(clean.sectionPath, clean.subtreeSha256,
                                                cleanSubtree);
    }
    sourceSubtree->clear();
    return false;
}

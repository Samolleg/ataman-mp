#include "view.h"
#include "node.h"
#include "objects/object_base.h"
#include "mob.h"
#include "camera.h"
#include "scene.h"
#include "operationmanager.h"
#include "profile_database.h"
#include "model_connection_editor.h"
#include <QFileInfo>
#include <QLineF>
#include <QKeyEvent>
#include <algorithm>

bool CView::event(QEvent* event) {
    if(event->type()==QEvent::KeyPress) {
        auto* key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Tab && key->modifiers()==Qt::NoModifier && m_modelMagnetEnabled
            && m_pOp && m_pOp->operationMethod()==eOperationMethodMoveAxis) {
            cycleModelConnectionTarget();key->accept();return true;
        }
    }
    return QGLWidget::event(event);
}

void CView::clearModelMagnet() {
    m_sourceConnections.clear();m_connectionTargets.clear();m_connectionIndex.clear();m_connectionChoices.clear();m_connectionGlyphs.clear();
    m_connectionHeldTarget=-1;m_connectionHeldSource=-1;m_modelMagnetApplied=false;m_connectionOrientations.clear();
    m_connectionSourceId.clear();
    m_connectionGhostNode.clear();m_connectionGhostVertices.clear();
    emit modelConnectionSourcesChanged({},{});emit modelConnectionStatusChanged(QString());
}
void CView::prepareModelMagnet() {
    clearModelMagnet();
    if(CScene::getInstance()->getMode()!=eEditModeObjects) return;
    for(auto* node:m_operationBackup.keys()) if(node->nodeType()==eWorldObject || node->nodeType()==eLever) m_connectionOrientations[node]=node->rotation();
    if(!m_modelMagnetEnabled) return;
    if(m_operationBackup.size()!=1 || m_connectionOrientations.size()!=1) {emit modelConnectionStatusChanged(QString::fromUtf8("Магнит: выберите одну деталь здания"));return;}
    auto* source=m_operationBackup.firstKey();m_connectionFreePosition=source->position();
    const auto database=ei::ProfileDatabaseContext::instance().current();QString error;
    const auto path=ei::TechnicalModelCatalog::pathForDatabase(database?database->sourceArchivePath:QString());
    if(path.isEmpty()) {emit modelConnectionStatusChanged(QString::fromUtf8("Магнит: настройте database.res профиля"));return;}
    if(!m_connectionCatalog.load(path,&error)
        || !m_connectionCatalog.connections(source->modelName(),&m_sourceConnections,&error)) {emit modelConnectionStatusChanged(error);return;}
    QStringList labels{QString::fromUtf8("Авто: все стыки")},ids{QString()};
    for(const auto& p:m_sourceConnections) {labels<<((p.label.isEmpty()?p.id:p.label)+" ["+p.type+"]");ids<<p.id;}
    if(!ids.contains(m_connectionSourceId))m_connectionSourceId.clear();
    emit modelConnectionSourcesChanged(labels,ids);
    if(m_sourceConnections.isEmpty()) {emit modelConnectionStatusChanged(QString::fromUtf8("У модели нет стыков — задайте их в витрине"));return;}
    if(auto* object=dynamic_cast<CObjectBase*>(source)) {
        const float scale=object->connectionScale();
        for(auto& p:m_sourceConnections)p.position*=scale;
        m_connectionGhostVertices=object->connectionPreviewVertices();
        if(!m_connectionGhostVertices.isEmpty())m_connectionGhostNode=source;
    }
    QHash<QString,QVector<ei::ModelConnection>> cached;
    auto add=[&](CNode* node,const QVector3D& position,const QString& owner) {
        if(!node || node->isMarkDeleted() || node->nodeState()==eHidden || m_operationBackup.contains(node)
            || (node->nodeType()!=eWorldObject && node->nodeType()!=eLever))return;
        const auto key=ei::TechnicalModelCatalog::modelKey(node->modelName());
        if(!cached.contains(key)) {QVector<ei::ModelConnection> points;QString message;
            if(!m_connectionCatalog.connections(key,&points,&message)) {emit modelConnectionStatusChanged(message);return;}cached.insert(key,points);}
        for(const auto& p:cached.value(key)) {
            ConnectionTarget t;t.owner=node;t.point=p;t.pose.position=position;t.pose.rotation=node->rotation();
            if(auto* object=dynamic_cast<CObjectBase*>(node))t.point.position*=object->connectionScale();
            t.world=position+t.pose.rotation.rotatedVector(t.point.position);t.label=owner+" / "+node->mapName()+" / "+(p.label.isEmpty()?p.id:p.label);
            m_connectionIndex.add(t.world,m_connectionTargets.size());m_connectionTargets.append(t);
        }
    };
    if(m_staticObjectsVisible) for(auto* mob:m_aMob) if(mob && isMobVisible(mob)) for(auto* node:mob->nodes()) add(node,node->drawPosition(),mob->mobName());
    if(m_neighborLandscapesVisible && m_staticObjectsVisible) for(const auto& neighbor:m_neighborLandscapes)
        for(int i=0;i<neighbor.staticObjects.size();++i) if(i<neighbor.staticLocalPositions.size())
            add(neighbor.staticObjects[i].data(),neighbor.staticLocalPositions[i]+QVector3D(neighbor.placement.origin.x(),neighbor.placement.origin.y(),0),neighbor.placement.zoneId);
    emit modelConnectionStatusChanged(QString::fromUtf8("Магнит: подведите стык к детали; Tab — другой вариант, Esc — отмена"));
}
void CView::setModelMagnetEnabled(bool enabled) {
    restoreFreeModelMove();m_modelMagnetEnabled=enabled;
    // A toggle during a move preserves the original gesture orientation.
    const auto originals=m_connectionOrientations;
    if(!m_operationBackup.isEmpty() && m_pOp && m_pOp->operationMethod()==eOperationMethodMoveAxis) {prepareModelMagnet();for(auto it=originals.begin();it!=originals.end();++it)m_connectionOrientations[it.key()]=it.value();}
    update();
}
void CView::setModelConnectionSource(const QString& id) {
    restoreFreeModelMove();m_connectionSourceId=id;m_connectionHeldTarget=-1;m_connectionHeldSource=-1;
    updateModelMagnet(m_connectionPointer);
}
void CView::restoreFreeModelMove() {
    if(!m_modelMagnetApplied || m_operationBackup.size()!=1)return;
    auto* node=m_operationBackup.firstKey();auto position=m_connectionFreePosition;
    if(m_connectionOrientations.contains(node))node->setRot(m_connectionOrientations.value(node));
    node->updatePos(position);
    m_modelMagnetApplied=false;
}
bool CView::connectionScreenPoint(const QVector3D& world,QPointF* screen) const {
    if(!m_cam || width()<=0 || height()<=0)return false;
    const auto p=world.project(m_cam->viewMatrix(),m_projection,QRect(0,0,width(),height()));
    if(!qIsFinite(p.x()) || !qIsFinite(p.y()) || !qIsFinite(p.z()) || p.z()<0 || p.z()>1)return false;
    *screen=QPointF(p.x(),height()-p.y());return true;
}
bool CView::applyModelConnection(int source,int target) {
    if(source<0 || source>=m_sourceConnections.size() || target<0 || target>=m_connectionTargets.size() || m_operationBackup.size()!=1)return false;
    const auto& t=m_connectionTargets[target];if(!t.owner || t.owner->isMarkDeleted())return false;
    ei::ConnectionPose result;if(!ei::connectModelFrames(m_sourceConnections[source],t.point,t.pose,&result))return false;
    auto* node=m_operationBackup.firstKey();
    // updatePos samples terrain at the new XY. Recover its exact height offset
    // from the actual display position, including standalone MOB semantics.
    node->setRot(result.rotation);
    auto position=result.position;position.setZ(0);node->updatePos(position);
    position.setZ(result.position.z()-node->drawPosition().z());node->updatePos(position);
    m_modelMagnetApplied=true;m_connectionHeldSource=source;m_connectionHeldTarget=target;
    emit modelConnectionStatusChanged(QString::fromUtf8("Стык: ")+t.label+QString::fromUtf8(" · ЛКМ — поставить · Tab — вариант · Esc — отмена"));
    update();return true;
}
void CView::updateModelMagnet(const QPoint& pointer) {
    m_connectionPointer=pointer;
    if(!m_modelMagnetEnabled || m_operationBackup.size()!=1 || m_sourceConnections.isEmpty())return;
    restoreFreeModelMove();auto* node=m_operationBackup.firstKey();m_connectionFreePosition=node->position();
    m_connectionChoices.clear();m_connectionGlyphs.clear();
    struct Match {int source,target;float distance;};QVector<Match> matches;
    const auto orientation=m_connectionOrientations.value(node,node->rotation());
    bool held=false;
    for(int s=0;s<m_sourceConnections.size();++s) {
        const auto& p=m_sourceConnections[s];if(!m_connectionSourceId.isEmpty() && p.id!=m_connectionSourceId)continue;
        const auto world=node->drawPosition()+orientation.rotatedVector(p.position);QPointF origin;
        if(!connectionScreenPoint(world,&origin))continue;
        m_connectionGlyphs<<world+QVector3D(-0.15f,0,0)<<world+QVector3D(0.15f,0,0)<<world+QVector3D(0,-0.15f,0)<<world+QVector3D(0,0.15f,0);
        for(int t:m_connectionIndex.queryNear(world,16)) {
            const auto& target=m_connectionTargets[t];if(!target.owner || target.owner->isMarkDeleted() || target.point.type!=p.type)continue;
            // Index cells are a broad phase, then exact XY and screen tests.
            const auto delta=world-target.world;if(delta.x()*delta.x()+delta.y()*delta.y()>256)continue;
            QPointF screen;if(!connectionScreenPoint(target.world,&screen))continue;
            const float pixels=QLineF(origin,screen).length();const bool previous=s==m_connectionHeldSource && t==m_connectionHeldTarget;
            if(pixels>(previous?48:28))continue;
            matches.append({s,t,pixels});held=held||previous;
        }
    }
    std::stable_sort(matches.begin(),matches.end(),[](const Match& a,const Match& b){return a.distance<b.distance;});
    for(const auto& match:matches)m_connectionChoices.append(qMakePair(match.source,match.target));
    const int show=qMin(matches.size(),32);
    for(int i=0;i<show;++i) {const auto& t=m_connectionTargets[matches[i].target];
        m_connectionGlyphs<<t.world+QVector3D(-0.25f,0,0)<<t.world+QVector3D(0.25f,0,0)<<t.world+QVector3D(0,-0.25f,0)<<t.world+QVector3D(0,0.25f,0);
    }
    if(!matches.isEmpty()) {const auto best=matches.first();applyModelConnection(held?m_connectionHeldSource:best.source,held?m_connectionHeldTarget:best.target);}
    else {m_connectionHeldSource=-1;m_connectionHeldTarget=-1;emit modelConnectionStatusChanged(QString::fromUtf8("Магнит: ищу совместимый стык"));update();}
}
void CView::cycleModelConnectionTarget() {
    if(m_connectionChoices.isEmpty())return;
    int i=m_connectionChoices.indexOf(qMakePair(m_connectionHeldSource,m_connectionHeldTarget));
    const auto next=m_connectionChoices[(i+1)%m_connectionChoices.size()];restoreFreeModelMove();applyModelConnection(next.first,next.second);
}

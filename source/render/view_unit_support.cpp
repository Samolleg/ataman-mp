#include "opening_trace.h"
#include "view.h"
#include "landscape.h"
#include "camera.h"
#include "mob.h"
#include "unit.h"
#include "map_object_placement_policy.h"

void CView::ensureUnitSupportSurface()
{
    opening::Scope timing("gui.geometry.support");
    const auto revision=CObjectBase::supportGeometryRevision();
    if(m_unitSupportRevision==revision && m_unitSupportMobs==m_aMob)return;
    m_unitSupportSurface.clear();
    // A resolver can run inside a MOB/node projection loop. Read-only scans
    // must not detach the live Qt containers while such an iterator exists.
    const auto mobs = m_aMob;
    for(CMob* mob:mobs) if(mob) {
        const auto& nodes=mob->nodes();
        for(CNode* node:nodes)
            if(node && !node->isMarkDeleted()
               && ei::contributesStaticNavigationCollision(node->nodeType()))
                if(auto* object=dynamic_cast<CObjectBase*>(node))
                    for(const auto& proxy:object->navigationCollisionProxies())
                        m_unitSupportSurface.add(proxy);
    }
    m_unitSupportRevision=revision;
    m_unitSupportMobs=mobs;
}

bool CView::unitPlacementPositionAt(const QPoint& cursor,QVector3D* position)
{
    if(!position || !rect().contains(cursor))return false;
    if(!m_pLand || !m_pLand->isMprLoad())return objectPlacementPositionAt(cursor,position);
    const bool ground=terrainPositionAt(cursor,position);
    const QRect viewport(0,0,width(),height());
    const auto view=m_cam->viewMatrix();
    const QVector3D start=QVector3D(cursor.x(),height()-cursor.y(),0).unproject(view,m_projection,viewport);
    const QVector3D end=ground ? *position
        : QVector3D(cursor.x(),height()-cursor.y(),1).unproject(view,m_projection,viewport);
    ensureUnitSupportSurface();
    QVector3D support;
    if(m_unitSupportSurface.intersect(start,end,&support)) {*position=support;return true;}
    return ground;
}

QVector3D CView::movementPointerPosition(const QPoint& cursor)
{
    for(CNode* node:selectionSnapshot())
        if(node && (node->nodeType()==eUnit || node->nodeType()==ePatrolPoint || node->nodeType()==eLookPoint)) {
            QVector3D surface;
            if(unitPlacementPositionAt(cursor,&surface))return surface;
            break;
        }
    return getTerrainPos(cursor.x(),cursor.y());
}

void CView::moveAlongUnitSurface(QVector3D direction)
{
    // Preserve relative offsets for NPCs and explicit Z for vertical edits.
    // XY logic movement intentionally places the point on its new surface.
    bool supported=false;
    for(CNode* node:m_operationBackup.keys())
        supported |= node->nodeType()==eUnit || node->nodeType()==ePatrolPoint || node->nodeType()==eLookPoint;
    if(!supported) {moveTo(direction);return;}
    restoreFreeModelMove();
    for(CNode* node:m_operationBackup.keys()) {
        if(!node->assemblyTag().isEmpty()) {setAssemblyWorldPosition(node,node->drawPosition()+direction);continue;}
        auto position=node->position()+direction;
        if(node->nodeType()==ePatrolPoint || node->nodeType()==eLookPoint)
            position=m_pLand->unitSurfacePosition(position);
        node->updatePos(position);
    }
    updateParameter(eObjParam_POSITION);
}

#include "view.h"
#include "native_editor_service.h"
#include "camera.h"
#include "landscape.h"
#include "mob.h"
#include "node.h"
#include "unit.h"
#include "magictrap.h"
#include "objects/object_base.h"
#include "operationmanager.h"
#include "navmesh_panel.h"
#include "settings.h"
#include "scene.h"
#include "ogl_utils.h"
#include "opening_trace.h"
#include <QSlider>
#include <QTextDocument>

CNativeEditorService CView::nativeServiceSnapshot() {
    opening::Scope timing("gui.service.snapshot");
    CNativeEditorService s;s.clip=m_projection*m_cam->viewMatrix();s.logicalSize=size();
    using S=CNativeEditorService;
    const auto add=[&](const QVector<QVector3D>& v,const QVector4D& c,S::Primitive p=S::Lines,float width=1,bool depth=true){s.add(v,c,p,width,depth);};
    if(m_selectFrame){const auto& points=m_selectFrame->points();if(points.size()==4){const auto a=points[0],b=points[2];s.selectionRect=QRect(QPoint(qRound((a.x()+1)*width()/2),qRound((1-a.y())*height()/2)),QPoint(qRound((b.x()+1)*width()/2),qRound((1-b.y())*height()/2))).normalized();}}
    const auto flag=[&](const char* key){const auto* o=dynamic_cast<const COptBool*>(m_pSettings->opt(key));return o&&o->value();};
    const bool helpers=flag("drawHelp")||flag("drawLogic")||CScene::getInstance()->getMode()==eEditModeLogic;
    for(auto* mob:m_aMob)if(isMobVisible(mob))for(auto* node:mob->nodes()){
        if(!node||node->isMarkDeleted()||node->nodeState()==eHidden||!isObjectVisualVisible(node))continue;
        const bool selected=node->nodeState()==eSelect;const auto center=node->drawPosition();
        const QVector4D color=mob==m_activeMob?QVector4D(.05f,.78f,.93f,1):QVector4D(1,.55f,.05f,1);
        if(selected){auto box=node->getBBox();box.move(node->drawPosition());if(box.isInit())add({box.minimum(),box.maximum()},color,S::Box,2);}
        if(m_diplomacyMapMode)if(auto* unit=dynamic_cast<CUnit*>(node)){const auto c=diplomacyNpcColor(unit);add({center},{float(c.redF()),float(c.greenF()),float(c.blueF()),1},S::Ring,3,false);}
        if(m_objectHelpersVisible&&(node->nodeType()==eLight||node->nodeType()==eSound||node->nodeType()==eParticle||node->nodeType()==eMagicTrap)){
            add({center},{1,.8f,.2f,1},S::Ring,1);add({center-QVector3D(0,0,.7f),center+QVector3D(0,0,.7f)},{1,.8f,.2f,1});}
        if((selected||helpers)&&m_objectHelpersVisible)if(auto* unit=dynamic_cast<CUnit*>(node))if(auto* logic=unit->logicAt(unit->editorLogicIndex())){
            const bool enabled=unit->editorLogicIndex()==0||logic->isUse();const bool authoring=CScene::getInstance()->getMode()==eEditModeLogic&&mob==m_activeMob;
            if(enabled||authoring){const auto lineColor=!enabled?QVector4D(.46f,.46f,.46f,.82f):logic->behaviorType()==EBehaviourType::eRadius?QVector4D(.9f,.4f,.1f,1):QVector4D(.8f,.8f,.2f,1);
                add(logic->editorPreviewPath(),lineColor,S::Strip,2,false);
                if(enabled&&authoring&&(selected||flag("drawHelp")))add(logic->editorPreviewHelp(),{.8f,0,1,1},S::Strip,2,false);
                for(auto* point:logic->patrolPoints()){add({point->drawPosition()},lineColor,S::Ring,1,false);for(auto* look:point->lookPoint()){add({point->drawPosition(),look->drawPosition()},lineColor,S::Lines,1,false);add({look->drawPosition()},{1,.8f,.2f,1},S::Points,5,false);}}
            }
        }
    }
    if(m_objectHelpersVisible)for(auto* mob:m_aMob)if(isMobVisible(mob))for(auto* node:mob->nodes())if(auto* trap=dynamic_cast<CMagicTrap*>(node))if(!trap->isMarkDeleted()&&trap->nodeState()!=eHidden&&(helpers||trap->nodeState()==eSelect)){
        QVector<QVector3D> links;for(auto* zone:trap->actZones()){add(zone->editorPreviewArea(),{.2f,1,.2f,1},S::Strip,2,false);links<<trap->drawPosition()<<zone->drawPosition();}for(auto* cast:trap->castPoints())links<<trap->drawPosition()<<cast->drawPosition();add(links,{.2f,1,.2f,1},S::Lines,2,false);}
    if(hasPendingOperation()){
        QVector3D center;int count=0;for(auto* node:selectionSnapshot()){center+=node->drawPosition();++count;}if(count){center/=count;
            add({center,center+QVector3D(4,0,0)},{1,.15f,.1f,1},S::Lines,2,false);
            add({center,center+QVector3D(0,4,0)},{.1f,1,.2f,1},S::Lines,2,false);
            add({center,center+QVector3D(0,0,4)},{.2f,.5f,1,1},S::Lines,2,false);}}
    add(m_scenarioParticipants,{.1f,.9f,1,1},S::Ring,2);const auto diamond=[&](const QVector<QVector3D>& centers,const QVector4D& color){for(const auto& c:centers){add({c+QVector3D(.8f,0,.2f),c+QVector3D(0,.8f,.2f),c+QVector3D(-.8f,0,.2f),c+QVector3D(0,-.8f,.2f)},color,S::Loop,2);add({c,c+QVector3D(0,0,1.8f)},color,S::Lines,2);}};diamond(m_scenarioSpawns,{.9f,.35f,1,1});diamond(m_scenarioDestinations,{1,.75f,.15f,1});add(m_scenarioLinks,{1,.8f,.3f,.9f});
    add(m_leverApproachLines,{.2f,1,.4f,1},S::Lines,2,false);add(m_connectionGlyphs,m_modelMagnetApplied?QVector4D(1,.8f,.2f,1):QVector4D(.1f,.9f,1,1),S::Lines,2);
    if(m_connectionGhostNode&&!m_connectionGhostVertices.isEmpty()){QMatrix4x4 model;model.translate(m_connectionGhostNode->drawPosition());model.rotate(m_connectionGhostNode->rotation());s.add(m_connectionGhostVertices,{.1f,.9f,1,.35f},S::Triangles,1,true,model);}
    if(!m_camPreviewActive)add(m_cameraTrackLines,{1,.7f,.1f,1},S::Lines,1);
    if(m_terrainPaintOutlineValid)add(m_terrainPaintOutlineVertices,m_terrainPaintProjectionLand?QVector4D(1,.72f,.08f,.95f):QVector4D(.1f,.82f,1,.95f),S::Loop,2.5f,false);
    if(m_sculptOutlineValid)add(m_sculptOutlineVertices,{1,.72f,.08f,.95f},S::Loop,2,false);
    if(m_worldPlacementActive&&m_worldPlacementValid){const auto p=m_worldPlacementOrigin-m_activeMapWorldOrigin;const float z=m_worldPlacementZ+.15f;QVector<QVector3D> corners{{float(p.x()),float(p.y()),z},{float(p.x()+m_worldPlacementSize.width()),float(p.y()),z},{float(p.x()+m_worldPlacementSize.width()),float(p.y()+m_worldPlacementSize.height()),z},{float(p.x()),float(p.y()+m_worldPlacementSize.height()),z}};
        add(corners,m_worldPlacementOverlaps?QVector4D(1,.5f,.1f,.25f):QVector4D(.1f,.9f,1,.25f),S::Fan,1,false);add(corners,{.1f,.9f,1,1},S::Loop,1,false);}
    if(m_neighborLayoutDragActive||m_worldPlacementActive){QVector<QVector3D> lines;const float z=(m_worldPlacementActive?m_worldPlacementZ:m_neighborLayoutDragPlaneZ)+.2f;for(const auto& seam:m_worldMapSnapPreview.seams)for(const auto& p:{seam.p1(),seam.p2()}){const auto q=p-m_activeMapWorldOrigin;lines<<QVector3D(q.x(),q.y(),z);}add(lines,{.2f,1,.35f,1},S::Lines,3,false);}
    if(m_objectHelpersVisible)for(auto* mob:m_aMob)if(isMobVisible(mob))for(auto* node:mob->logicNodes())if(node&&!node->isMarkDeleted()&&node->nodeState()!=eHidden&&(helpers||node->nodeState()==eSelect))add({node->drawPosition()},{.3f,.95f,.9f,1},S::Ring,2);
    if(m_aiGraphPreviewVisible&&m_aiGraphDocument.isValid()&&!(m_navmeshPanel&&m_navmeshPanel->layerVisible())){
        s.graph=m_aiGraphDocument;s.graphLayer=m_aiGraphLayerSlider?m_aiGraphLayerSlider->value():3;
        s.graphHeights=m_aiGraphPreviewUsesLoadedSource?m_aiGraphLoadedSurfaceHeights:m_aiGraphRegeneratedResult.navigationSurfaceHeights;
        s.graphTerrain=m_terrainEditAvailable?std::make_shared<CTerrainEditModel>(m_terrainEdit):std::make_shared<CTerrainEditModel>();
    }
    if(m_navmeshPanel&&m_navmeshPanel->layerVisible()){
        m_navmeshDrawnTiles=0;m_navmeshDrawnVertices=0;
        const auto batch=[&](const QVector<QVector3D>& v,const QVector4D& c,S::Primitive p){m_navmeshDrawnVertices+=v.size();add(v,c,p,1,false);};
        if(m_navmeshPanel->lossesVisible())for(const auto& tile:m_navmeshPanel->comparison().renderTiles){++m_navmeshDrawnTiles;for(const auto& area:tile.areas)batch(area.vertices,area.color,S::Triangles);}
        else {const auto& doc=m_navmeshPanel->document();for(const auto& tile:doc.renderTiles){++m_navmeshDrawnTiles;
            if(m_navmeshPanel->fillVisible())for(const auto& area:tile.areas)batch(area.vertices,area.color,S::Triangles);
            batch(tile.edges,{.22f,.95f,.45f,.8f},S::Lines);if(m_navmeshPanel->narrowVisible())batch(tile.narrow,{1,.45f,.05f,1},S::Lines);}
            if(m_navmeshPanel->islandsVisible())add(doc.islands,{1,.1f,.15f,1},S::Points,4,false);}
        if(m_navmeshPanel->legendVisible()&&!property("rebuild64NativeLegend").toBool()){
            s.legend=m_navmeshPanel->legendHtml();const int w=qMin(337,qMax(132,width()-40));QTextDocument doc;doc.setDefaultFont(QFont("Segoe UI",9));doc.setDefaultStyleSheet("body {color:white;} p {margin:0;}");doc.setHtml("<body>"+s.legend+"</body>");doc.setTextWidth(w-12);const int h=qCeil(doc.size().height())+10;
            QList<QRect> occupied;auto* surface=parentWidget()?parentWidget()->findChild<QWidget*>("rebuild64NativeSurface"):nullptr;
            if(surface)for(auto* child:surface->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))if(child->isVisible())occupied<<child->geometry();
            s.legendRect=QRect(12,12,w,h);bool placed=false;
            for(int y=12;y+h<=height()-12&&!placed;y+=24)for(int x=width()-w-12;x>=12;x-=24){QRect candidate(x,y,w,h);bool blocked=false;for(const auto& box:occupied)blocked|=candidate.intersects(box);if(!blocked){s.legendRect=candidate;placed=true;break;}}
            m_navmeshLegendRect=s.legendRect;
        }
    }
    for(int i=0;i<m_testEntranceRectangles.size();++i){const auto area=m_testEntranceRectangles[i].normalized();QVector<QVector3D> corners;
        for(const auto& p:{area.topLeft(),area.topRight(),area.bottomRight(),area.bottomLeft()}){QVector3D v(p.x(),p.y(),0);if(projectTravelOverlay(i,v))corners<<v+QVector3D(0,0,.15f);}
        if(corners.size()!=4)continue;const bool selected=i==m_selectedTestEntrance;
        add(corners,selected?QVector4D(.2f,1,.38f,.3f):QVector4D(.92f,.7f,.12f,.2f),S::Fan);add(corners,selected?QVector4D(.38f,1,.5f,1):QVector4D(.92f,.75f,.18f,.8f),S::Loop,selected?3:1);
        if(m_travelEditing&&selected){QVector<QVector3D> handles;for(const auto& p:travelAreaHandles(area)){QVector3D v(p.x(),p.y(),0);if(projectTravelOverlay(i,v))handles<<v+QVector3D(0,0,.2f);}add(handles,{1,1,1,1},S::Points,9);}
        if(i<m_testEntranceLabelImages.size()){QVector3D center(area.center().x(),area.center().y(),0);if(projectTravelOverlay(i,center))s.labels.append({center+QVector3D(0,0,.25f),m_testEntranceLabelImages[i]});}
    }
    if(m_testSpawnMarkerValid){add({m_testSpawnMarker},{.31f,1,.47f,1},S::Ring,3);add({m_testSpawnMarker,m_testSpawnMarker+QVector3D(0,0,2)},{.31f,1,.47f,1},S::Lines,3);}
    if(m_tileGridVisible&&m_landscapeVisible&&m_bDrawLand)s.grid=m_terrainEditAvailable?std::make_shared<CTerrainEditModel>(m_terrainEdit):std::make_shared<CTerrainEditModel>();
    if(!m_terrainEditAvailable&&(s.grid||s.graphTerrain))s.terrainBytes=m_pLand->originalMprData();
    return s;
}

bool CView::nativeTerrainPositionAt(const QPoint& point,QVector3D* position,bool land) const {
    if(!position||m_nativeQueryPixels.isEmpty()||m_nativeQueryClip!=m_projection*m_cam->viewMatrix()||!rect().contains(point))return false;
    const int x=qBound(0,int((point.x()+.5)*m_nativeQuerySize.width()/qMax(1,width())),m_nativeQuerySize.width()-1),y=qBound(0,int((point.y()+.5)*m_nativeQuerySize.height()/qMax(1,height())),m_nativeQuerySize.height()-1);
    const auto& p=m_nativeQueryPixels[y*m_nativeQuerySize.width()+x];const float z=land?p.terrain_depth:p.overlay_depth;if(!qIsFinite(z)||z<0||z>=1)return false;
    const auto world=m_nativeQueryClip.inverted()*QVector4D(2.f*(x+.5f)/m_nativeQuerySize.width()-1,1-2.f*(y+.5f)/m_nativeQuerySize.height(),2*z-1,1);*position=world.toVector3DAffine();return true;
}

QList<CNode*> CView::nativePickNodes(const QRect& rect) {
    QList<CNode*> result;if(m_nativeQueryPixels.isEmpty()||m_nativeQueryClip!=m_projection*m_cam->viewMatrix())return result;
    const auto area=rect.normalized().intersected(this->rect());if(area.isEmpty())return result;
    QVector<CMob*> mobs;for(auto* mob:m_aMob)if(mob&&isMobVisible(mob))mobs.append(mob);QSet<quint64> keys;
    for(int y=area.top();y<=area.bottom();++y)for(int x=area.left();x<=area.right();++x){const int px=qBound(0,int((x+.5)*m_nativeQuerySize.width()/width()),m_nativeQuerySize.width()-1),py=qBound(0,int((y+.5)*m_nativeQuerySize.height()/height()),m_nativeQuerySize.height()-1);const auto& p=m_nativeQueryPixels[py*m_nativeQuerySize.width()+px];if(p.renderer_id){keys.insert((quint64(p.mob)<<32)|p.nid);if(p.mob==UINT32_MAX)setProperty("nativeNeighborPickId",p.nid);}}
    const bool logic=CScene::getInstance()->getMode()==eEditModeLogic&&!m_diplomacyMapMode;
    if(!logic)for(int i=0;i<mobs.size();++i)for(auto* node:mobs[i]->nodes())if(node&&isObjectVisualVisible(node)&&keys.contains((quint64(i)<<32)|node->mapId())&&(!m_diplomacyMapMode||dynamic_cast<CUnit*>(node)))result.append(node);
    // Service locators are authored points, not runtime world meshes. Test
    // their own visible marker against the same native depth used to draw it.
    if(m_objectHelpersVisible||m_diplomacyMapMode)for(auto* mob:mobs){const auto candidates=logic?mob->logicNodes():mob->nodes();for(auto* node:candidates){
        if(!node||node->isMarkDeleted()||node->nodeState()==eHidden||!isObjectVisualVisible(node))continue;
        if(m_diplomacyMapMode&&!dynamic_cast<CUnit*>(node))continue;
        if(!logic&&node->nodeType()!=eLight&&node->nodeType()!=eSound&&node->nodeType()!=eParticle&&node->nodeType()!=eMagicTrap&&!(m_diplomacyMapMode&&dynamic_cast<CUnit*>(node)))continue;
        const auto v=m_nativeQueryClip*QVector4D(node->drawPosition(),1);if(v.w()<=0)continue;const float z=.5f+.5f*v.z()/v.w();
        const QPoint p(qRound((v.x()/v.w()*.5f+.5f)*width()),qRound((.5f-v.y()/v.w()*.5f)*height()));if(!area.adjusted(-6,-6,6,6).contains(p)||!this->rect().contains(p)||z<0||z>=1)continue;
        const int x=qBound(0,p.x()*m_nativeQuerySize.width()/width(),m_nativeQuerySize.width()-1),y=qBound(0,p.y()*m_nativeQuerySize.height()/height(),m_nativeQuerySize.height()-1);
        if((m_diplomacyMapMode||z<=m_nativeQueryPixels[y*m_nativeQuerySize.width()+x].depth+.00002f)&&!result.contains(node))result.append(node);
    }}
    return result;
}

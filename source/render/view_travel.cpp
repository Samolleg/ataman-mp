#include "view.h"
#include "camera.h"
#include "landscape.h"
#include <QMouseEvent>
#include <QLineF>
#include <QDir>

void CView::focusTravelArea(int exitIndex,bool remove)
{
    cancelTravelGesture();m_travelSelectedRemove=remove;
    setTravelAreas(m_travelAreas,exitIndex);
    for(const auto& area:m_travelAreas)if(area.exitIndex==exitIndex && area.remove==remove){
        const auto center=area.rectangle.center();QVector3D point(center.x(),center.y(),0);
        if(m_pLand->isMprLoad())m_pLand->projectPt(point);
        focusScenarioPosition(point);return;
    }
}

void CView::setTravelAreas(const QVector<STravelArea>& areas,int selectedExit)
{
    if(m_travelDragging)return;
    m_travelAreas=areas;m_travelSelectedExit=selectedExit;
    QVector<QRectF> rectangles;QStringList labels;int selected=-1;
    for(const auto& area:areas) {
        if(area.exitIndex==selectedExit && (selected<0 || area.remove==m_travelSelectedRemove))selected=rectangles.size();
        rectangles.append(area.rectangle);labels.append(area.label);
    }
    auto display=areas;
    if(m_entranceOverlaysVisible)for(const auto& area:m_worldTravelAreas)if(!area.mapPath.isEmpty()) {
        display.append(area);rectangles.append(area.rectangle);labels.append(area.label);
    }
    setTestEntranceOverlays(rectangles,labels,selected);
    m_displayTravelAreas=display;
}
void CView::setWorldTravelAreas(const QVector<STravelArea>& areas)
{
    m_worldTravelAreas=areas;
    if(m_travelEditing){setTravelAreas(m_travelAreas,m_travelSelectedExit);return;}
    QVector<QRectF> rectangles;QStringList labels;
    if(m_entranceOverlaysVisible)for(const auto& area:areas){rectangles.append(area.rectangle);labels.append(area.label);}
    setTestEntranceOverlays(rectangles,labels,-1);
    if(m_entranceOverlaysVisible)m_displayTravelAreas=areas;
}
bool CView::projectTravelOverlay(int index,QVector3D& point) const
{
    if(index>=0 && index<m_displayTravelAreas.size()) {
        const auto& area=m_displayTravelAreas[index];
        const auto rect=index<m_testEntranceRectangles.size()?m_testEntranceRectangles[index]:area.rectangle;
        const auto rotated=travelRotatedPoint(QPointF(point.x(),point.y()),rect.center(),area.angle);
        point.setX(rotated.x());point.setY(rotated.y());
    }
    if(index>=m_displayTravelAreas.size() || m_displayTravelAreas[index].mapPath.isEmpty())
        return m_pLand->projectPt(point);
    if(!m_neighborLandscapesVisible)return false;
    const auto& path=m_displayTravelAreas[index].mapPath;
    for(const auto& neighbor:m_neighborLandscapes)if(QDir::cleanPath(neighbor.placement.mprPath).compare(QDir::cleanPath(path),Qt::CaseInsensitive)==0) {
        if(!neighbor.landscape->projectPt(point))return false;
        point+=QVector3D(neighbor.placement.origin.x(),neighbor.placement.origin.y(),0);
        return true;
    }
    return false;
}
void CView::setTravelEditing(bool enabled)
{
    cancelTravelGesture();m_travelEditing=enabled;update();
}
void CView::beginTravelArea(bool remove,bool newExit)
{
    cancelTravelGesture();m_travelDrawing=true;m_travelRemove=remove;m_travelNewExit=newExit;
    m_travelSelectedRemove=remove;
    setCursor(Qt::CrossCursor);setFocus();
    emit travelDrawingStateChanged(true,remove,newExit);
}
void CView::cancelTravelGesture()
{
    const bool wasDrawing=m_travelDrawing;
    if(m_travelDragging && m_travelHit>=0 && m_travelHit<m_testEntranceRectangles.size()) {
        if(m_travelDrawing) {
            m_travelAreas.removeLast();
        } else m_testEntranceRectangles[m_travelHit]=m_travelOriginal;
    }
    m_travelDragging=false;m_travelDrawing=false;m_travelHit=-1;
    if(wasDrawing)setTravelAreas(m_travelAreas,m_travelSelectedExit);
    m_selectedTestEntrance=-1;
    for(int i=0;i<m_travelAreas.size();++i)if(m_travelAreas[i].exitIndex==m_travelSelectedExit
        && (m_selectedTestEntrance<0 || m_travelAreas[i].remove==m_travelSelectedRemove))m_selectedTestEntrance=i;
    unsetCursor();update();
    if(wasDrawing)emit travelDrawingStateChanged(false,m_travelRemove,m_travelNewExit);
}
bool CView::travelMousePress(QMouseEvent* event)
{
    if(!m_travelEditing)return false;
    if(event->button()==Qt::RightButton) {
        cancelTravelGesture();event->accept();return true;
    }
    if(event->button()!=Qt::LeftButton)return false;
    event->accept();QVector3D position;
    if(!terrainPositionAt(event->pos(),&position))return true;
    const QPointF point(position.x(),position.y());
    if(m_travelDrawing) {
        STravelArea a;a.exitIndex=m_travelNewExit?-1:m_travelSelectedExit;
        a.remove=m_travelRemove;a.rectangle=QRectF(point,QSizeF(0,0));
        m_travelHit=m_travelAreas.size();m_travelAreas.append(a);
        setTravelAreas(m_travelAreas,m_travelSelectedExit);
        m_travelOriginal=a.rectangle;m_travelStart=point;m_travelDragging=true;
        m_selectedTestEntrance=m_travelHit;return true;
    }
    int hit=-1,handle=-1;
    // Screen-space handles remain usable at any camera zoom.
    for(int i=0;i<m_travelAreas.size();++i) {
        if(i!=m_selectedTestEntrance)continue;
        const auto handles=travelAreaHandles(m_travelAreas[i].rectangle);
        for(int h=0;h<handles.size();++h) {
            QVector3D p(handles[h].x(),handles[h].y(),0);
            if(!projectTravelOverlay(i,p))continue;
            p.setZ(p.z()+0.2f);
            const auto screen=p.project(m_cam->viewMatrix(),m_projection,QRect(0,0,width(),height()));
            if(QLineF(event->pos(),QPointF(screen.x(),height()-screen.y())).length()<=10) {hit=i;handle=h;}
        }
    }
    if(hit<0)for(int i=m_travelAreas.size()-1;i>=0;--i)
        if(m_travelAreas[i].rectangle.contains(travelRotatedPoint(point,m_travelAreas[i].rectangle.center(),-m_travelAreas[i].angle))){hit=i;break;}
    if(hit<0)return true;
    const int exit=m_travelAreas[hit].exitIndex;
    m_travelSelectedRemove=m_travelAreas[hit].remove;
    bool accepted=true;emit travelAreaSelected(exit,&accepted);
    if(!accepted)return true;
    if(hit>=m_travelAreas.size())return true;
    m_travelHit=hit;m_travelHandle=handle;m_travelStart=point;
    m_travelOriginal=m_travelAreas[hit].rectangle;m_travelDragging=true;
    m_selectedTestEntrance=hit;setCursor(handle<0?Qt::SizeAllCursor:Qt::CrossCursor);update();return true;
}
bool CView::travelMouseMove(QMouseEvent* event)
{
    if(!m_travelEditing)return false;
    if(!m_travelDragging){
        if(event->buttons()&Qt::MiddleButton)return false;
        if(m_travelDrawing)setCursor(Qt::CrossCursor);
        event->accept();return true;
    }
    QVector3D position;
    if(terrainPositionAt(event->pos(),&position)) {
        const QPointF p(position.x(),position.y());
        const QSize size=activeTerrainSectorSize();
        const QRectF bounds(0,0,size.width()*32,size.height()*32);
        QPointF delta=p-m_travelStart;
        if(!m_travelDrawing && m_travelHandle>=0)
            delta=travelRotatedPoint(delta,QPointF(),-m_travelAreas[m_travelHit].angle);
        QRectF r=m_travelDrawing?QRectF(m_travelStart,p).normalized().intersected(bounds)
            :travelDraggedRect(m_travelOriginal,delta,m_travelHandle,bounds);
        if(!m_travelDrawing && m_travelHandle>=0) {
            const auto shift=r.center()-m_travelOriginal.center();
            r.moveCenter(m_travelOriginal.center()+travelRotatedPoint(shift,QPointF(),m_travelAreas[m_travelHit].angle));
        }
        m_testEntranceRectangles[m_travelHit]=r;update();
    }
    event->accept();return true;
}
bool CView::travelMouseRelease(QMouseEvent* event)
{
    if(m_travelEditing && event->button()==Qt::RightButton){event->accept();return true;}
    if(!m_travelEditing || event->button()!=Qt::LeftButton)return false;
    event->accept();
    if(!m_travelDragging)return true;
    travelMouseMove(event);
    const auto area=m_travelAreas[m_travelHit];
    const QRectF r=m_testEntranceRectangles[m_travelHit];
    const bool isNew=m_travelDrawing&&m_travelNewExit;
    cancelTravelGesture();
    if(r.width()>0.001 && r.height()>0.001)
        emit travelAreaCommitted(area.exitIndex,area.remove,r,isNew);
    return true;
}

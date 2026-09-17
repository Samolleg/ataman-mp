#include "view.h"
#include "landscape.h"
#include <QToolButton>
#include <QLabel>
#include <cmath>

void CView::beginWorldMapPlacement(const QSizeF& size)
{
    cancelPendingOperation();
    if(m_neighborLayoutEditToggle) m_neighborLayoutEditToggle->setChecked(false);
    m_worldPlacementSize = size;
    m_worldPlacementActive = true;
    m_worldPlacementValid = false;
    QVector3D point;
    m_worldPlacementZ = terrainPositionAt(rect().center(), &point, false) ? point.z() : 0.f;
    setFocus(); setCursor(Qt::CrossCursor);
    updateWorldMapPlacement(rect().center());
}

void CView::cancelWorldMapPlacement()
{
    if(!m_worldPlacementActive) return;
    m_worldPlacementActive = false; m_worldPlacementValid = false;
    unsetCursor();
    if(m_neighborLayoutStatus) m_neighborLayoutStatus->hide();
    emit worldMapPlacementCancelled(); update();
}

void CView::showWorldMap(const QString& mapId)
{
    m_preferredWorldMap = mapId;
    if(m_neighborLandscapesVisualToggle) m_neighborLandscapesVisualToggle->setChecked(true);
}

bool CView::updateWorldMapPlacement(const QPoint& position)
{
    QPointF local;
    m_worldPlacementValid = viewportPointOnHorizontalPlane(position, m_worldPlacementZ, &local);
    if(!m_worldPlacementValid) { update(); return false; }
    QPointF origin = local + m_activeMapWorldOrigin;
    QVector<QRectF> bounds;
    if(m_pLand && m_pLand->isMprLoad())
        bounds.append(QRectF(m_activeMapWorldOrigin, QSizeF(
            m_mprDocument.mapInfo().sectorCountX*32, m_mprDocument.mapInfo().sectorCountY*32)));
    for(const auto& map : m_neighborPlacements) bounds.append(QRectF(map.worldOrigin, map.size));
    // Snap each axis to the nearest edge within two world units.
    double bestX=2.0, bestY=2.0, dx=0, dy=0;
    for(const auto& rect : bounds)
    {
        for(double edge : {rect.left(),rect.right()})
            for(double side : {origin.x(),origin.x()+m_worldPlacementSize.width()})
                if(std::abs(edge-side)<bestX) {bestX=std::abs(edge-side);dx=edge-side;}
        for(double edge : {rect.top(),rect.bottom()})
            for(double side : {origin.y(),origin.y()+m_worldPlacementSize.height()})
                if(std::abs(edge-side)<bestY) {bestY=std::abs(edge-side);dy=edge-side;}
    }
    origin += QPointF(dx,dy);
    m_worldPlacementOrigin = QPointF(std::round(origin.x()*4)/4,std::round(origin.y()*4)/4);
    m_worldPlacementOverlaps = false;
    for(const auto& rect : bounds)
        m_worldPlacementOverlaps |= rect.intersects(QRectF(m_worldPlacementOrigin,m_worldPlacementSize));
    if(m_neighborLayoutStatus)
    {
        m_neighborLayoutStatus->setText(tr("Place map: X %1, Y %2 вЂ” %3 Г— %4. Left click: place; Esc: cancel.%5")
            .arg(m_worldPlacementOrigin.x()).arg(m_worldPlacementOrigin.y())
            .arg(m_worldPlacementSize.width()).arg(m_worldPlacementSize.height())
            .arg(m_worldPlacementOverlaps ? tr(" Overlaps another map.") : QString()));
        m_neighborLayoutStatus->show();
    }
    update(); return true;
}

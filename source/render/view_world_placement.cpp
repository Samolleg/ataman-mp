#include "view.h"
#include "landscape.h"
#include <QToolButton>
#include <QLabel>
#include <cmath>

void CView::beginWorldMapPlacement(const QSizeF& size)
{
    beginWorldMapPlacement(size,m_worldTileGridAnchor,m_hasWorldTileGridAnchor);
}

void CView::beginWorldMapPlacement(const QSizeF& size, const QPointF& anchor, bool hasAnchor)
{
    cancelPendingOperation();
    if(m_neighborLayoutEditToggle) m_neighborLayoutEditToggle->setChecked(false);
    m_worldPlacementSize = size;
    m_worldPlacementActive = true;
    m_worldPlacementValid = false;
    m_worldPlacementTileAnchor=anchor;
    m_hasWorldPlacementTileAnchor=hasAnchor;
    m_worldMapSnapPreview=SWorldMapSnapResult();
    QVector3D point;
    const bool foundPlane=terrainPositionAt(rect().center(), &point, false)
        || neighborMapAtViewport(rect().center(),nullptr,&point,true);
    m_worldPlacementZ = foundPlane ? point.z() : 0.f;
    setFocus(); setCursor(Qt::CrossCursor);
    updateWorldMapPlacement(rect().center());
}

void CView::cancelWorldMapPlacement()
{
    if(!m_worldPlacementActive) return;
    m_worldPlacementActive = false; m_worldPlacementValid = false;
    m_worldMapSnapPreview=SWorldMapSnapResult();
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
    m_worldMapSnapPreview=snapWorldMap(local+m_activeMapWorldOrigin,m_worldPlacementSize,
        QString(),position,m_worldPlacementZ,m_worldPlacementTileAnchor,m_hasWorldPlacementTileAnchor);
    m_worldPlacementValid=m_worldMapSnapPreview.valid;
    if(!m_worldPlacementValid)
    {
        if(m_neighborLayoutStatus) m_neighborLayoutStatus->setText(QString::fromUtf8(
            "Не найдена опорная карта для сетки тайлов. Проверьте раскладку мира."));
        update(); return false;
    }
    m_worldPlacementOrigin=m_worldMapSnapPreview.origin;
    m_worldPlacementOverlaps=m_worldMapSnapPreview.overlaps;
    if(m_neighborLayoutStatus)
    {
        m_neighborLayoutStatus->setText(tr("Place map: X %1, Y %2 — %3 × %4. Left click: place; Esc: cancel.%5")
            .arg(m_worldPlacementOrigin.x()).arg(m_worldPlacementOrigin.y())
            .arg(m_worldPlacementSize.width()).arg(m_worldPlacementSize.height())
            .arg(m_worldPlacementOverlaps ? tr(" Overlaps another map.") : QString()));
        m_neighborLayoutStatus->show();
        if(m_worldMapSnapPreview.incompatibleEdge)
            m_neighborLayoutStatus->setText(m_neighborLayoutStatus->text()+QString::fromUtf8(
                "\nУ соседней карты другая сетка. Используйте «Выровнять по тайлам»."));
    }
    update(); return true;
}

SWorldMapSnapResult CView::snapWorldMap(const QPointF& origin, const QSizeF& size,
    const QString& excludedZone, const QPoint& pointer, float planeZ,
    const QPointF& anchor, bool hasAnchor)
{
    QVector<SWorldMapSnapBounds> bounds;
    if(m_pLand && m_pLand->isMprLoad() && (excludedZone.isEmpty() || m_neighborActiveZoneId!=excludedZone))
        bounds.append({m_neighborActiveZoneId,QRectF(m_activeMapWorldOrigin,
            QSizeF(m_pLand->widthInWorldUnits(),m_pLand->heightInWorldUnits()))});
    for(const auto& map:m_neighborPlacements)
        if(map.zoneId!=excludedZone) bounds.append({map.zoneId,QRectF(map.worldOrigin,map.size)});
    double radius=2.0;
    QPointF p,x,y;
    if(viewportPointOnHorizontalPlane(pointer,planeZ,&p)
       && viewportPointOnHorizontalPlane(pointer+QPoint(12,0),planeZ,&x)
       && viewportPointOnHorizontalPlane(pointer+QPoint(0,12),planeZ,&y))
        radius=qMax(QLineF(p,x).length(),QLineF(p,y).length());
    return CWorldMapSnap::place(origin,size,anchor,hasAnchor,m_worldTileSnapEnabled,
                               bounds,radius,m_worldMapSnapPreview.latch);
}

void CView::previewWorldMapOrigins(const QMap<QString,QPointF>& origins)
{
    if(origins.contains(m_neighborActiveZoneId)) m_activeMapWorldOrigin=origins.value(m_neighborActiveZoneId);
    for(auto& map:m_neighborPlacements)
    {
        if(origins.contains(map.zoneId)) map.worldOrigin=origins.value(map.zoneId);
        map.origin=map.worldOrigin-m_activeMapWorldOrigin;
    }
    for(auto& loaded:m_neighborLandscapes)
    {
        if(origins.contains(loaded.placement.zoneId)) loaded.placement.worldOrigin=origins.value(loaded.placement.zoneId);
        loaded.placement.origin=loaded.placement.worldOrigin-m_activeMapWorldOrigin;
        loaded.drawCacheDirty=true;
        if(loaded.landscape) loaded.landscape->setWorldOffset(QVector3D(
            float(loaded.placement.origin.x()),float(loaded.placement.origin.y()),0));
    }
    m_worldMapSnapPreview=SWorldMapSnapResult();
    update();
}

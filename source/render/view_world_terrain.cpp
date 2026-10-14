#include "window_geometry.h"
#include "view.h"
#include "node.h"
#include "resourcemanager.h"
#include "resource_name.h"
#include "mmp_codec.h"
#include "tile_form.h"
#include "terrain_preview_context.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "staged_save_validator.h"
#include "res_archive_document.h"
#include "terrain_save_flow_policy.h"
#include "terrain_mixed_edge.h"
#include "world_material_boundary.h"
#include "terrain_atlas_shape_policy.h"
#include "terrain_resolve_task.h"
#include <QtWidgets>
#include <QCryptographicHash>
#include <cmath>

namespace {
bool collectWorldRasterSize(const CTerrainWorkingAtlas& atlas,QSize* size,
    CValidationReport* report,const QString& id)
{
    const auto native=terrainAtlasMaximumNativeTileSize(atlas.authoredTileImages());
    if(native.isEmpty()) {
        report->add("terrain.world.tile_size",EValidationSeverity::Error,
            QObject::tr("Атлас карты содержит пустые или неквадратные тайлы."),QFileInfo(id).fileName());
        return false;
    }
    const int maximum=qMax(size->width(),native.width());
    *size=QSize(maximum,maximum);return true;
}

QMap<qint32,QImage> worldRasterSources(const QMap<qint32,QImage>& sources,const QSize& native)
{
    QMap<qint32,QImage> result;
    for(auto it=sources.cbegin();it!=sources.cend();++it)
        result.insert(it.key(),terrainAtlasRasterTile(it.value(),native));
    return result;
}

class CWorldRasterAtlasBank
{
public:
    CWorldRasterAtlasBank(const QMap<qint32,QImage>& sources,
        const QMap<qint32,ETerrainBlendStyle>& styles,bool opaque)
        :m_sources(sources),m_styles(styles),m_opaque(opaque) {}
    QSharedPointer<CTerrainVirtualAtlas> forSize(const QSize& native)
    {
        auto& atlas=m_atlases[native.width()];
        if(!atlas) {
            QImage placeholder(native,QImage::Format_RGBA8888);placeholder.fill(Qt::transparent);
            atlas.reset(new CTerrainVirtualAtlas(CTerrainTransitionCatalog(),{placeholder},0,
                worldRasterSources(m_sources,native),m_styles,m_opaque));
        }
        return atlas;
    }
private:
    QMap<qint32,QImage> m_sources;
    QMap<qint32,ETerrainBlendStyle> m_styles;
    bool m_opaque;
    QMap<int,QSharedPointer<CTerrainVirtualAtlas>> m_atlases;
};

QSet<int> nativeWorldRepairCells(const SWorldTerrainDocument& document,
    const CTerrainWorkingAtlas& atlas,const CTerrainEditModel& terrain,ETerrainTileLayer layer)
{
    QSet<int> animated,protectedCells;
    for(const auto& animation:document.baseline.mapInfo().animatedTiles)
        for(int frame=0;frame<qMax(1,int(animation.frameCount));++frame)
            animated.insert(int(animation.tileIndex)+frame);
    for(int y=0;y<terrain.tileCountY();++y)for(int x=0;x<terrain.tileCountX();++x) {
        bool available=false;const auto packed=terrain.packedTile(x,y,layer,&available);
        if(!available || (layer==ETerrainTileLayer::Overlay && packed==0xffff))continue;
        const int slot=SMprPackedTile::decode(packed).atlasSlot();
        if(animated.contains(slot) || atlas.runtimeTerrainTypeForSlot(slot)==eWater)
            protectedCells.insert(y*terrain.tileCountX()+x);
    }
    return protectedCells;
}

QVector<SMaterial> worldRenderMaterials(const QVector<SMprMaterialRecord>& records);
bool inferWorldMaterialRuntimeTypes(const QMap<QString,SWorkingTerrainResolveStage>& stages,
        const QSet<qint32>& required,QMap<qint32,qint32>* types,CValidationReport* report)
{
    if(!types||!report)return false;
    QMap<qint32,QMap<qint32,int>> evidence;
    for(auto stage=stages.cbegin();stage!=stages.cend();++stage) {
        if(!stage->atlas)continue;
        for(int slot=0;slot<stage->atlas->activeTileCount();++slot) {
            const int runtime=stage->atlas->runtimeTerrainTypeForSlot(slot);
            if(runtime<0||runtime>15)continue;
            SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);
            bool known=false;const auto signature=stage->atlas->catalog().gridSignatureForPacked(packed.encode(),&known);
            if(!known)continue;
            const qint32 ids[4]={signature.northWest,signature.northEast,signature.southEast,signature.southWest};
            for(qint32 id:ids)if(required.contains(id)&&!types->contains(id))++evidence[id][runtime];
        }
    }
    for(qint32 material:required) {
        if(types->contains(material))continue;
        const auto candidates=evidence.value(material);
        int bestType=-1,bestCount=0,ties=0;
        for(auto it=candidates.cbegin();it!=candidates.cend();++it) {
            if(it.value()>bestCount){bestType=it.key();bestCount=it.value();ties=1;}
            else if(it.value()==bestCount)++ties;
        }
        if(bestType<0||ties!=1) {
            report->add("terrain.world.material_type",EValidationSeverity::Error,
                bestType<0?QStringLiteral("Не найден подтверждённый класс рельефа для материала %1 в выбранных картах.").arg(material)
                          :QStringLiteral("Класс рельефа материала %1 неоднозначен в выбранных картах.").arg(material));
            return false;
        }
        types->insert(material,bestType);
    }
    return true;
}
}

void CView::updateWorldTerrainStatus()
{
    if(!m_landscapeTools) return;
    QStringList names;
    for(const auto& path:m_worldTerrainSession.dirtyPaths()) names.append(QFileInfo(path).fileName());
    const QString status=tr("World terrain: %1. Enabled neighbors: %2. Modified: %3")
        .arg(m_worldTerrainEnabled?tr("on"):tr("off"))
        .arg(m_worldTerrainEnabledPaths.size()).arg(names.isEmpty()?tr("none"):names.join(", "));
    m_landscapeTools->setWorldTerrainStatus(status,m_worldTerrainSession.canUndo(),
        m_worldTerrainSession.canRedo(),!names.isEmpty(),m_worldTerrainEnabled,m_worldTerrainEnabledPaths.size());
}

void CView::syncWorldTerrainActive()
{
    if(!m_pLand || !m_pLand->isMprLoad() || !m_terrainEditAvailable) return;
    const QString id=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(!m_worldTerrainSession.documents.contains(id)) return;
    auto doc=m_worldTerrainSession.documents[id];
    doc->baseline=m_mprDocument;
    doc->terrain=m_terrainEdit;
    if(doc->atlas){doc->atlas=m_workingTerrainAtlas;doc->materials=m_mprMaterials;}
}

void CView::restoreWorldTerrainActive()
{
    CTerrainPreviewContext contextGuard(this);
    const QString id=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(!m_worldTerrainSession.documents.contains(id)) return;
    auto doc=m_worldTerrainSession.documents[id];
    if(doc->baseline.originalBytes()!=m_mprDocument.originalBytes())
    {
        // Never overwrite retained edits with a newly loaded external version.
        m_terrainEditAvailable=false;
        m_pLand->setCanonicalTerrainModel(nullptr);
        QMessageBox::critical(this,tr("Retained terrain differs from disk"),
            tr("The MPR changed externally. Retained edits are still in the world session. Save/recover or explicitly discard that session before reopening this file."));
        return;
    }
    if(m_adoptedNeighborTerrainDocument==doc) {
        // The adopted sector VBOs and preview atlas already show this model.
        // Restore editor state without republishing every sector or atlas.
        m_terrainEdit=doc->terrain;m_mprMaterials=doc->materials;
        if(doc->atlas) {
            const auto images=doc->atlas->activeTileImages();
            if(!m_pTileForm->adoptTileImages(images,doc->atlas->activeTileTypes())) {
                m_terrainEditAvailable=false;return;
            }
            m_authoredTerrainTileImages=doc->atlas->authoredTileImages();
            m_workingTerrainAtlas=doc->atlas->createStagingCopy();
            const auto source=m_worldTerrainAtlasSources.constFind(id);
            if(source!=m_worldTerrainAtlasSources.cend()) {
                m_terrainAtlasProfile=source->profile;m_activeTerrainArchivePath=source->archive;
            }
            if(m_landscapeTools) {
                m_landscapeTools->setOverlayMaterials(worldRenderMaterials(m_mprMaterials));
                m_landscapeTools->setGeneratedAtlasTileCount(m_workingTerrainAtlas->generatedTileCount());
            }
            refreshWorkingTerrainPalettes();
        }
        m_pLand->setCanonicalTerrainModel(&m_terrainEdit);syncMprDirtyState();
        updateWorldTerrainStatus();return;
    }
    if(doc->atlas) {
        if(!previewWorldPaintState(id)){m_terrainEditAvailable=false;return;}
        updateWorldTerrainStatus();return;
    }
    for(const auto& sector:doc->baseline.sectors())
    {
        const auto before=m_terrainEdit.sectorPayload(sector.sectorX,sector.sectorY);
        const auto after=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
        if(before!=after) m_pLand->previewCanonicalSector(sector.sectorX,sector.sectorY,after);
    }
    m_terrainEdit=doc->terrain;
    m_pLand->setCanonicalTerrainModel(&m_terrainEdit);
    m_pLand->setDirty(m_terrainEdit.isDirty());
    m_pLand->invalidateTerrainShadowField();
    updateWorldTerrainStatus();
}

void CView::refreshWorldTerrainNeighbors()
{
    CTerrainPreviewContext contextGuard(this);
    for(auto& loaded:m_neighborLandscapes)
    {
        const QString id=CWorldTerrainSession::key(loaded.placement.mprPath);
        if(!m_worldTerrainSession.documents.contains(id)) continue;
        auto doc=m_worldTerrainSession.documents[id];
        loaded.retainedTerrainDocument=doc;
        loaded.landscape->setCanonicalTerrainModel(&doc->terrain);
        if(doc->atlas && loaded.terrainPreviewAtlas!=doc->atlas) {
            if(!previewWorldPaintState(id)){emit updateMsg(tr("Не удалось обновить атлас соседней карты."));continue;}
        }
        bool changed=false;
        for(const auto& sector:doc->baseline.sectors())
        {
            const auto key=qMakePair(sector.sectorX,sector.sectorY);
            const auto before=loaded.terrainPreviewPayloads.value(key,
                doc->baseline.memberPayload(sector.memberName));
            const auto after=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
            if(before==after) continue;
            if(loaded.landscape->previewCanonicalSector(sector.sectorX,sector.sectorY,after))
            { loaded.terrainPreviewPayloads[key]=after;changed=true; }
            else emit updateMsg(tr("Neighbor terrain preview failed: %1").arg(loaded.placement.zoneId));
        }
        if(changed)
        {
            loaded.landscape->invalidateTerrainShadowField();
            loaded.drawCacheDirty=true;
            for(int index=0; index<loaded.staticObjects.size(); ++index)
            {
                loaded.landscape->projectPosition(loaded.staticObjects.at(index).data());
                loaded.staticLocalPositions[index]=loaded.staticObjects.at(index)->drawPosition();
            }
        }
    }
    update();
}

void CView::chooseWorldTerrainMaps()
{
    finishSculptStroke();
    if(!m_hasActiveMapWorldOrigin || !m_neighborLayoutStorageWritable
       || !m_pLand->isMprLoad() || !ensureTerrainAutoCatalogReady())
    {
        QMessageBox::information(this,tr("World terrain"),
            tr("Open a map with a saved world layout and available canonical terrain first.")); return;
    }
    syncWorldTerrainActive();
    QDialog dialog(this);ei::rememberWindowGeometry(&dialog, QStringLiteral("worldBrushMaps")); dialog.setWindowTitle(tr("World terrain maps"));
    QVBoxLayout* layout=new QVBoxLayout(&dialog);
    QCheckBox* enabled=new QCheckBox(tr("Edit neighboring-map terrain"),&dialog);
    enabled->setChecked(m_worldTerrainEnabled);layout->addWidget(enabled);
    QLabel* description=new QLabel(QString::fromUtf8("Выбранные карты редактируются как одно полотно. На стыке кисть согласует вершины обеих карт; для соединения краёв используется дополнительный ряд вершин возле мазка. Исключённые карты не изменяются. Отмена возвращает весь мазок на всех затронутых картах."),&dialog);
    description->setWordWrap(true);layout->addWidget(description);
    QListWidget* list=new QListWidget(&dialog); layout->addWidget(list);
    QSet<QString> shown;
    for(const auto& placement:m_neighborPlacements)
    {
        const QString id=CWorldTerrainSession::key(placement.mprPath);
        if(shown.contains(id)) continue; shown.insert(id);
        const bool dirty=m_worldTerrainSession.documents.contains(id)
            && m_worldTerrainSession.documents[id]->terrain.isDirty();
        auto item=new QListWidgetItem(QStringLiteral("%1 — %2%3")
            .arg(placement.zoneId,QFileInfo(placement.mprPath).fileName(),dirty?tr(" (modified)"):QString()),list);
        item->setData(Qt::UserRole,placement.mprPath);
        item->setToolTip(placement.mprPath);
        item->setCheckState(!m_worldTerrainExcludedPaths.contains(id)?Qt::Checked:Qt::Unchecked);
    }
    QLabel* dirty=new QLabel(tr("Retained modified MPRs:\n%1")
        .arg(m_worldTerrainSession.dirtyPaths().join('\n')),&dialog);
    dirty->setWordWrap(true);layout->addWidget(dirty);
    QDialogButtonBox* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(620,440);
    if(dialog.exec()!=QDialog::Accepted) return;
    QSet<QString> selected;
    QString error;
    if(enabled->isChecked())
    {
        const QString active=m_pLand->filePath().absoluteFilePath();
        if(!m_worldTerrainSession.documents.contains(CWorldTerrainSession::key(active))
           && m_pLand->isDirty())
        {
            QMessageBox::information(this,tr("World terrain"),
                tr("Save existing local edits before starting a world terrain session.")); return;
        }
        if(!m_worldTerrainSession.load(active,&error))
        { QMessageBox::warning(this,tr("World terrain unavailable"),error);return; }
        syncWorldTerrainActive();
        for(int row=0;row<list->count();++row)
        {
            auto item=list->item(row);
            if(item->checkState()!=Qt::Checked) continue;
            const QString path=item->data(Qt::UserRole).toString();
            selected.insert(CWorldTerrainSession::key(path));
        }
        if(m_neighborLandscapesVisualToggle) m_neighborLandscapesVisualToggle->setChecked(true);
        if(m_neighborLayoutEditToggle) m_neighborLayoutEditToggle->setChecked(false);
    }
    selected.clear();
    m_worldTerrainEnabled=enabled->isChecked();
    for(int row=0;row<list->count();++row) {
        const auto item=list->item(row);const auto id=CWorldTerrainSession::key(item->data(Qt::UserRole).toString());
        if(item->checkState()==Qt::Checked){m_worldTerrainExcludedPaths.remove(id);selected.insert(id);}
        else m_worldTerrainExcludedPaths.insert(id);
    }
    m_worldTerrainEnabledPaths=selected;
    updateWorldTerrainStatus();
    refreshWorldTerrainNeighbors();
    emit updateMsg(tr("World terrain: %1; %2 explicitly enabled neighboring MPR(s).")
        .arg(m_worldTerrainEnabled?tr("enabled"):tr("disabled")).arg(selected.size()));
}

bool CView::worldTerrainPosition(const QPoint& viewport,QVector3D* position)
{
    const bool land=!m_landscapeTools || !m_landscapeTools->overlaySculptEnabled();
    if(!m_worldTerrainEnabled) return terrainPositionAt(viewport,position,land);
    SNeighborMapPlacement placement; QVector3D neighbor;
    if(neighborMapAtViewport(viewport,&placement,&neighbor,false,land)
       && m_worldTerrainEnabledPaths.contains(CWorldTerrainSession::key(placement.mprPath)))
    { *position=neighbor;return true; }
    return terrainPositionAt(viewport,position,land);
}

bool CView::sculptWorldTerrainAt(const QVector3D& position,float fraction)
{
    if(!m_worldTerrainEnabled || !m_landscapeTools) return false;
    if(!m_hasActiveMapWorldOrigin || !m_neighborLayoutStorageWritable) return false;
    if(!m_terrainEditAvailable || m_neighborLayoutEditEnabled) return true;
    QString error;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(!m_worldTerrainSession.load(m_pLand->filePath().absoluteFilePath(),&error))
    { emit updateMsg(error);return true; }
    syncWorldTerrainActive();
    QVector<SWorldTerrainTarget> targets;
    SWorldTerrainTarget current;current.id=active;current.origin=m_activeMapWorldOrigin;
    current.model=&m_worldTerrainSession.documents[active]->terrain;current.editable=true;
    targets.append(current);
    QSet<QString> seen;seen.insert(active);
    for(const auto& placement:m_neighborPlacements)
    {
        const QString id=CWorldTerrainSession::key(placement.mprPath);
        if(seen.contains(id) || !m_worldTerrainEnabledPaths.contains(id)) continue;
        seen.insert(id);
        const double radius=m_landscapeTools->radius()+2;
        const QPointF center(position.x()+m_activeMapWorldOrigin.x(),position.y()+m_activeMapWorldOrigin.y());
        if(!QRectF(placement.worldOrigin,placement.size).adjusted(-radius,-radius,radius,radius).contains(center))continue;
        if(!m_worldTerrainSession.load(placement.mprPath,&error)){emit updateMsg(error);return true;}
        SWorldTerrainTarget t;t.id=id;t.origin=placement.worldOrigin;
        t.model=&m_worldTerrainSession.documents[id]->terrain;t.editable=true;targets.append(t);
    }
    STerrainBrush brush;brush.centerX=position.x()+m_activeMapWorldOrigin.x();
    brush.centerY=position.y()+m_activeMapWorldOrigin.y();brush.radius=m_landscapeTools->radius();
    brush.shape=m_landscapeTools->paintShape();brush.falloff=m_landscapeTools->falloff();
    EWorldSculptMode mode=EWorldSculptMode::Raise;
    switch(m_landscapeTools->sculptTool())
    {
    case ELandscapeSculptTool::Lower:mode=EWorldSculptMode::Lower;break;
    case ELandscapeSculptTool::Flatten:mode=EWorldSculptMode::Flatten;break;
    case ELandscapeSculptTool::Smooth:mode=EWorldSculptMode::Smooth;break;
    case ELandscapeSculptTool::BoundaryFill:mode=EWorldSculptMode::BoundaryFill;break;
    case ELandscapeSculptTool::Noise:mode=EWorldSculptMode::Noise;break;
    default:break;
    }
    const double quantum=m_terrainEdit.maxZ()/65535.0;
    const double strength=m_landscapeTools->strength();
    const double blend=1-std::pow(1-qBound(0.0,strength,1.0),double(fraction));
    const bool additive=mode==EWorldSculptMode::Raise || mode==EWorldSculptMode::Lower || mode==EWorldSculptMode::Noise;
    SWorldTerrainStroke stamp;
    if(m_landscapeTools->overlaySculptEnabled()) {
        CValidationReport report;
        for(const auto& target:targets) {
            if(!(target.model->maxZ()>0)) {emit updateMsg(tr("Invalid terrain height scale."));return true;}
            STerrainBrush local=brush;local.centerX-=target.origin.x();local.centerY-=target.origin.y();
            const float delta=m_landscapeTools->rawDelta()*quantum*65535.0/target.model->maxZ()*fraction*strength;
            const auto flatten=quint16(qBound(0,qRound(m_landscapeTools->flattenHeight()*quantum*65535.0/target.model->maxZ()),65535));
            SOverlaySurfaceCommand command;
            switch(mode) {
            case EWorldSculptMode::Raise:command=target.model->makeOverlayRaiseLower(local,delta,&report,true);break;
            case EWorldSculptMode::Lower:command=target.model->makeOverlayRaiseLower(local,-delta,&report,true);break;
            case EWorldSculptMode::Flatten:command=target.model->makeOverlayFlatten(local,flatten,blend,&report);break;
            case EWorldSculptMode::Smooth:command=target.model->makeOverlaySmooth(local,blend,&report);break;
            case EWorldSculptMode::BoundaryFill:command=target.model->makeOverlayBoundaryFill(local,blend,&report);break;
            case EWorldSculptMode::Noise:command=target.model->makeOverlayNoise(local,delta,m_sculptStrokeNoiseSeed,&report,true);break;
            }
            if(!command.isEmpty())stamp.overlays.insert(target.id,command);
        }
        if(report.hasErrors()){emit updateMsg(report.toText());return true;}
    } else stamp=makeWorldTerrainStamp(targets,brush,mode,
        m_landscapeTools->rawDelta()*quantum*fraction,
        m_landscapeTools->flattenHeight()*quantum,additive?strength:blend,m_sculptStrokeNoiseSeed);
    if(!stamp.error.isEmpty()) {emit updateMsg(stamp.error);return true;}
    if(stamp.protectedBoundaryVertices>0)
        emit updateMsg(QString::fromUtf8("Часть стыка не согласована: проверьте размещение карт и доступность соседей для общей кисти."));
    if(stamp.isEmpty()) return true;
    if(!m_worldTerrainSession.apply(stamp,true,&error)) { emit updateMsg(error);return true; }
    if(stamp.maps.contains(active) && !applyTerrainHeightPreview(stamp.maps[active],true))
    { m_worldTerrainSession.apply(stamp,false,&error);return true; }
    if(stamp.overlays.contains(active) && !applyOverlaySurfacePreview(stamp.overlays[active],true))
    {m_worldTerrainSession.apply(stamp,false,&error);return true;}
    m_worldTerrainStroke.append(stamp);
    refreshWorldTerrainNeighbors();
    return true;
}

bool CView::worldTerrainHistory(bool forward,bool force)
{
    if(!force && !(m_worldTerrainEnabled && m_landscapeTools && m_landscapeTools->editingEnabled())) return false;
    finishSculptStroke();syncWorldTerrainActive();
    const auto next=m_worldTerrainSession.nextCommand(forward);
    if(!next) return force;
    if(!force && m_pUndoStack && next->localUndoIndex>=0
       && next->localUndoIndex!=m_pUndoStack->index())return false;
    const auto stroke=*next;
    if(!terrainMobHeightStateAvailable(stroke.mobHeightState.data()))
    {
        emit updateMsg(tr("Cannot replay terrain and object heights after the affected MOB was unloaded."));
        return true;
    }
    QString error;
    if(!prepareHistoryNavigation()) return true;
    if(!(forward?m_worldTerrainSession.redo(&error):m_worldTerrainSession.undo(&error)))
    { QMessageBox::warning(this,tr("World terrain history refused"),error);return true; }
    if(m_pLand->isMprLoad() && m_terrainEditAvailable)
    {
        const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
        if(stroke.maps.contains(active)) applyTerrainHeightPreview(stroke.maps[active],forward);
        if(stroke.overlays.contains(active))applyOverlaySurfacePreview(stroke.overlays[active],forward);
        if(stroke.tiles.contains(active))applyTerrainTilePreview(stroke.tiles[active],forward);
    }
    for(const auto& id:stroke.paint.keys())previewWorldPaintState(id);
    applyTerrainMobHeights(stroke.mobHeightState.data(),forward);
    refreshWorldTerrainNeighbors();
    emit updateMsg(forward?tr("Redid one complete world terrain stroke."):tr("Undid one complete world terrain stroke."));
    updateWorldTerrainStatus();
    return true;
}

bool CView::saveWorldTerrainMaps()
{
    finishSculptStroke();endTileBrushGroup();syncWorldTerrainActive();
    const QStringList paths=m_worldTerrainSession.dirtyPaths();
    if(paths.isEmpty())return true;
    if(QMessageBox::question(this,tr("Сохранение общего ландшафта"),
        tr("Сохранить перечисленные карты и их атласы с резервными копиями?\n\n%1\n\nПосле сохранения история общей кисти будет очищена.").arg(paths.join('\n')),
        QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Save)return false;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    QVector<SMapSaveFile> files;
    QMap<QString,QByteArray> archives;
    QMap<QString,QString> archivePaths;
    QMap<QString,QMap<QString,QByteArray>> published;
    QMap<QString,STerrainAtlasProfile> profiles;
    QMap<QString,QVector<QImage>> physical;
    QMap<QString,CMprDocument> reopened;
    CValidationReport report;QString error;
    auto refuse=[&](const QString& detail) {
        QMessageBox::warning(this,tr("Карты не сохранены"),detail+"\n"+report.toText());return false;
    };
    for(const auto& path:paths) {
        const QString id=CWorldTerrainSession::key(path);const auto doc=m_worldTerrainSession.documents[id];
        CMprDocument staged=doc->baseline;
        if(id==active && m_workingTerrainAtlas && !ensureWorldTerrainAtlas(id,&report))return refuse(report.toText());
        const bool publishAtlas = doc->atlas && doc->terrain.usesUnsavedAtlasArtwork(*doc->atlas);
        if(publishAtlas) {
            const auto source=m_worldTerrainAtlasSources.value(id);
            if(source.archive.isEmpty())return refuse(tr("Не определён архив текстур карты %1.").arg(path));
            const QString archiveId=CWorldTerrainSession::key(source.archive);
            if(!archives.contains(archiveId)) {
                QFile f(source.archive);if(!f.open(QIODevice::ReadOnly))return refuse(f.errorString());
                archives[archiveId]=f.readAll();archivePaths[archiveId]=source.archive;
            }
            CResArchiveDocument archive;QStringList errors;
            if(!archive.loadBytes(archives[archiveId],source.archive,&errors))return refuse(errors.join('\n'));
            const auto names=ei::terrainTextureResourceNames(staged.mapInfo().innerNamespace,int(staged.mapInfo().textureCount));
            if(names.size()!=source.pages.size())return refuse(tr("Размер исходного атласа изменился."));
            auto pages=source.pages;const auto images=doc->atlas->activeTileImages();
            for(int page=0;page<names.size();++page) {
                if(archive.memberPayload(names[page])!=source.pages[page])return refuse(tr("Текстуры изменены другой программой: %1").arg(names[page]));
                QSet<int> changed;
                for(int slot=0;slot<64;++slot) {
                    QImage old;
                    if(!ei::decodeMmpDxt1AtlasTile(pages[page],slot,&old,&error))return refuse(error);
                    if(page*64+slot>=images.size())return refuse(tr("Рабочий атлас потерял исходные страницы."));
                    if(old.convertToFormat(QImage::Format_RGBA8888)!=images[page*64+slot].convertToFormat(QImage::Format_RGBA8888))changed.insert(slot);
                }
                if(!changed.isEmpty()) {
                    const int nativePageTileSize=doc->atlas->authoredTileImages().at(page*64).width();
                    const auto encoded=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(images,page*64,nativePageTileSize),true,&error);
                    if(encoded.isEmpty())return refuse(error);
                    pages[page]=ei::patchMmpDxt1AtlasTiles(pages[page],encoded,changed,&error);
                    if(pages[page].isEmpty())return refuse(error);
                }
            }
            const auto animations=id==active?m_mprAnimatedTiles:doc->baseline.mapInfo().animatedTiles;
            const auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(staged,images,doc->atlas->activeTileTypes(),pages,doc->materials,animations,&report);
            if(!package.success)return refuse(package.error);
            if(!staged.loadBytes(package.mprBytes,path,&report))return refuse(report.toText());
            CResArchiveDocument encoded;
            if(!encoded.loadBytes(package.resourceArchiveBytes,"world-atlas.res",&errors))return refuse(errors.join('\n'));
            for(const auto& name:package.resourceMembers) {
                const auto bytes=encoded.memberPayload(name);
                if(published[archiveId].contains(name) && published[archiveId][name]!=bytes)
                    return refuse(tr("Карты используют одну текстуру %1 с разными изменениями. Сохранение отменено.").arg(name));
                published[archiveId][name]=bytes;
                for(int slot=0;slot<64;++slot) {
                    QImage image;if(!ei::decodeMmpDxt1AtlasTile(bytes,slot,&image,&error))return refuse(error);
                    physical[id].append(image.convertToFormat(QImage::Format_RGBA8888));
                }
            }
            profiles[id]=CTerrainAtlasProfileStore::workingAtlasProfile(staged.mapInfo().innerNamespace,physical[id],doc->atlas->catalog(),source.profile,m_terrainGlobalFamilies,&report);
            profiles[id].textureCount=package.resourceMembers.size();
            profiles[id].resourceArchiveHint=QFileInfo(source.archive).fileName();
            if(report.hasErrors())return refuse(report.toText());
        }
        CMprEditTransaction transaction(QStringLiteral("World terrain and materials"));
        QByteArray candidate;
        if(!doc->terrain.appendSectorEdits(&transaction,&report)
           || (publishAtlas ? !staged.appendMaxZEdit(doc->terrain.maxZ(),&transaction,&report)
               : !staged.appendMapParametersEdit(doc->terrain.maxZ(),id==active?m_mprMaterials:doc->materials,
                 id==active?m_mprAnimatedTiles:staged.mapInfo().animatedTiles,&transaction,&report))
           || !staged.applyTransaction(transaction,&candidate,&report))return refuse(report.toText());
        CTerrainEditModel check;
        if(!reopened[id].loadBytes(candidate,path,&report) || !check.load(reopened[id],&report) || report.hasErrors())return refuse(report.toText());
        if(doc->atlas) {
            const auto source=m_worldTerrainAtlasSources[id];
            const auto profile=publishAtlas ? profiles[id] : source.profile;
            SResArchiveNewMember semantic;
            if(!buildTerrainSemanticFieldMember(candidate,QFileInfo(path),
                    profile.mprNamespace,profile.atlasPixelSha256,profile.toBytes(),
                    &semantic,&report,&doc->terrain,&candidate)) return refuse(report.toText());
            if(!reopened[id].loadBytes(candidate,path,&report))return refuse(report.toText());
            if(publishAtlas) {
                const QString archiveId=CWorldTerrainSession::key(source.archive);
                const QString name=CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace);
                if(published[archiveId].contains(name) && published[archiveId][name]!=profile.toBytes())
                    return refuse(tr("Несовместимые профили общего атласа."));
                published[archiveId][name]=profile.toBytes();
                if(!semantic.name.isEmpty()) {
                    if(published[archiveId].contains(semantic.name) && published[archiveId][semantic.name]!=semantic.payload)
                        return refuse(tr("Несовместимые поля материалов одного пространства имён."));
                    published[archiveId][semantic.name]=semantic.payload;
                }
            }
        }
        SMapSaveFile file;file.path=path;file.before=doc->baseline.originalBytes();file.after=candidate;files.append(file);
    }
    QStringList destinations;
    for(auto it=published.cbegin();it!=published.cend();++it) {
        CResArchiveDocument archive;QStringList errors;QByteArray candidate;
        QVector<SResArchiveNewMember> members;
        for(auto member=it.value().cbegin();member!=it.value().cend();++member) {
            SResArchiveNewMember entry;entry.name=member.key();entry.payload=member.value();members.append(entry);
        }
        if(!archive.loadBytes(archives[it.key()],archivePaths[it.key()],&errors)
           || !archive.buildWithPublishedMembers(members,true,&candidate,&errors))return refuse(errors.join('\n'));
        SMapSaveFile file;file.path=archivePaths[it.key()];file.before=archives[it.key()];file.after=candidate;files.append(file);
        destinations.append(file.path);
    }
    // Combine with the enclosing map/travel save. Never overwrite a companion
    // archive independently when the owner already staged another edit to it.
    if(m_mapCohortActive)for(const auto& companion:m_mapCompanions) {
        bool duplicate=false;
        for(const auto& file:files)if(CWorldTerrainSession::key(file.path)==CWorldTerrainSession::key(companion.path)) {
            if(file.before!=companion.before || file.after!=companion.after)return refuse(tr("Другой компонент изменяет тот же файл: %1").arg(file.path));
            duplicate=true;break;
        }
        if(!duplicate)files.append(companion);
    }
    QStringList prepared;
    for(const auto& destination:destinations) {
        bool ready=false;emit terrainPublicationPreflightRequested(destination,&ready);
        if(!ready) {for(const auto& prior:prepared)emit terrainPublicationFinished(prior,false);return refuse(tr("Архив пока недоступен для записи: %1").arg(destination));}
        prepared.append(destination);
    }
    const auto saved=commitMapSaveBatch(files,m_mapCohortActive?m_mapCohortBackupRoot:QString(),-1,[this](){
        if(m_mapCohortActive)acceptMapCompanionsCheckpoint();
    });
    if(m_mapCohortActive)m_mapCohortCommitted=saved.success;
    for(const auto& destination:destinations)emit terrainPublicationFinished(destination,saved.success);
    if(!saved.success)return refuse(saved.error);
    // A successful multi-map save is a history checkpoint, like atlas publication.
    // No old snapshot may retain hashes or slot tables from the former baseline.
    m_worldTerrainSession.clearHistory();
    for(const auto& path:paths) {
        const QString id=CWorldTerrainSession::key(path);const auto doc=m_worldTerrainSession.documents[id];
        if(id==active) {
            const QString hash=QString::fromLatin1(QCryptographicHash::hash(reopened[id].originalBytes(),QCryptographicHash::Sha256).toHex());
            if(profiles.contains(id)) {
                if(!adoptCompleteTerrainBaseline(QFileInfo(path),hash,physical[id],m_worldTerrainAtlasSources[id].archive,profiles[id],false))
                    return refuse(tr("Файлы сохранены, но предпросмотр требует повторного открытия карты."));
            } else {
                SMprSaveCommitResult commit;commit.candidateSha256=hash;
                if(!adoptCommittedLandscapeBaseline(QFileInfo(path),commit))return refuse(tr("Файлы сохранены; откройте карту заново."));
            }
        }
        doc->baseline=reopened[id];doc->terrain.load(doc->baseline,&report);
        doc->materials=doc->baseline.mapInfo().materials;doc->atlas.clear();m_worldTerrainAtlasSources.remove(id);
        if(profiles.contains(id)) {
            if(!ensureWorldTerrainAtlas(id,&report) || !previewWorldPaintState(id))return refuse(tr("Файлы сохранены; откройте карту заново."));
        }
    }
    refreshWorldTerrainNeighbors();updateWorldTerrainStatus();
    emit updateMsg(tr("Сохранено карт: %1. Атласы проверены, резервные копии сохранены. История общей кисти очищена.").arg(paths.size()));
    return true;
}

bool CView::resolveWorldTerrainClose(bool retain)
{
    syncWorldTerrainActive();
    if(m_worldTerrainSession.documents.isEmpty()) return true;
    if(retain)
    {
        // Parameter/atlas publication remains owned by the existing save flow.
        // Refuse to hide unsupported pending publication on a map switch.
        if(m_pLand->isMprLoad() && m_terrainEditAvailable && m_pLand->isDirty())
        {
            QByteArray candidate;CValidationReport report;
            const auto retained=m_worldTerrainSession.documents.value(CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath()));
            // A height-only world session has no atlas yet. A later local
            // paint/repair can introduce working artwork; sync alone only
            // updates an already-owned atlas. Capture its source pages before
            // retaining the map, so reopening/saving keeps the complete pair.
            const bool needsAtlas=retained && m_workingTerrainAtlas
                && m_terrainEdit.usesUnsavedAtlasArtwork(*m_workingTerrainAtlas);
            const bool atlasReady=!needsAtlas || ensureWorldTerrainAtlas(
                CWorldTerrainSession::key(retained->path),&report);
            const bool ownsAtlas=retained && retained->atlas && m_worldTerrainAtlasSources.contains(CWorldTerrainSession::key(retained->path));
            if(!atlasReady
               || (mprParametersDirty() && (!ownsAtlas || m_mprAnimatedTiles!=m_mprDocument.mapInfo().animatedTiles))
               || (!ownsAtlas && !buildValidatedLandscapeCandidate(&candidate,&report)))
            {
                QMessageBox::information(this,tr("Save before switching maps"),
                    tr("Save or finalize pending map-parameter/atlas changes before retaining this map in the world terrain session.\n%1").arg(report.toText()));
                return false;
            }
        }
        return true;
    }
    const QStringList paths=m_worldTerrainSession.dirtyPaths();
    if(!paths.isEmpty())
    {
        const auto choice=QMessageBox::question(this,tr("Close world terrain session"),
            tr("These MPRs contain unsaved changes:\n%1\n\nSave all, discard the entire session, or cancel?").arg(paths.join('\n')),
            QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
        if(choice==QMessageBox::Cancel) return false;
        if(choice==QMessageBox::Save && !saveWorldTerrainMaps()) return false;
        if(choice==QMessageBox::Discard)
        {
            // Explicit discard resets all retained models before detaching views.
            for(auto doc:m_worldTerrainSession.documents)
            { CValidationReport report;doc->terrain.load(doc->baseline,&report);doc->atlas.clear();doc->materials=doc->baseline.mapInfo().materials; }
            if(m_pLand->isMprLoad() && m_terrainEditAvailable) restoreWorldTerrainActive();
            refreshWorldTerrainNeighbors();
        }
    }
    for(auto& loaded:m_neighborLandscapes) loaded.landscape->setCanonicalTerrainModel(nullptr);
    m_worldTerrainAtlasSources.clear();
    m_worldTerrainSession.clear();m_worldTerrainEnabledPaths.clear();m_worldTerrainExcludedPaths.clear();m_worldTerrainEnabled=true;
    updateWorldTerrainStatus();
    return true;
}

// Read neighbor artwork from one complete archive, never from the active
// map's texture override. Semantic IDs are globalized before comparing edges.
bool CView::worldMaterialNeighbors(QVector<SWorldMaterialField>* land,
        QVector<SWorldMaterialField>* overlay,CValidationReport* report)
{
    if(!m_worldTerrainEnabled || !m_hasActiveMapWorldOrigin)return true;
    const QRectF active(m_activeMapWorldOrigin,QSizeF(m_terrainEdit.tileCountX()*2,m_terrainEdit.tileCountY()*2));
    QSet<QString> seen;
    for(const auto& placement:m_neighborPlacements) {
        const QString id=CWorldTerrainSession::key(placement.mprPath);
        if(seen.contains(id))continue;seen.insert(id);
        const QRectF other(placement.worldOrigin,placement.size);
        const double epsilon=0.00001;
        const bool touchingX=(std::abs(active.right()-other.left())<epsilon || std::abs(active.left()-other.right())<epsilon)
            && qMin(active.bottom(),other.bottom())-qMax(active.top(),other.top())>epsilon;
        const bool touchingY=(std::abs(active.bottom()-other.top())<epsilon || std::abs(active.top()-other.bottom())<epsilon)
            && qMin(active.right(),other.right())-qMax(active.left(),other.left())>epsilon;
        if(!touchingX && !touchingY)continue;
        CMprDocument document;CTerrainEditModel model;
        if(m_worldTerrainSession.documents.contains(id)) {
            document=m_worldTerrainSession.documents[id]->baseline;
            model=m_worldTerrainSession.documents[id]->terrain;
        } else if(!document.load(QFileInfo(placement.mprPath),report) || !model.load(document,report))return false;
        QString error;
        if(!m_worldTerrainSession.load(placement.mprPath,&error)) {
            report->add("terrain.world.load",EValidationSeverity::Error,error);return false;
        }
        if(!ensureWorldTerrainAtlas(id,report))return false;
        model=m_worldTerrainSession.documents[id]->terrain;
        const auto& catalog=m_worldTerrainSession.documents[id]->atlas->catalog();
        for(int layerIndex=0;layerIndex<2;++layerIndex) {
            const auto layer=layerIndex?ETerrainTileLayer::Overlay:ETerrainTileLayer::Land;
            SWorldMaterialField neighbor;neighbor.origin=placement.worldOrigin;
            if(!model.copyVirtualMaterialField(layer,&neighbor.field,false)) {
                if(!model.initializeVirtualMaterialField(layer,catalog,report,ETerrainMaterialFieldRecovery::RecoverDeterministically)
                   || !model.copyVirtualMaterialField(layer,&neighbor.field,false))continue;
            }
            (layerIndex?overlay:land)->append(neighbor);
        }
    }
    return !report->hasErrors();
}

bool CView::eraseWorldOverlayAt(const QVector3D& position)
{
    if(!m_worldTerrainEnabled || !m_hasActiveMapWorldOrigin || !m_neighborLayoutStorageWritable
       || !m_landscapeTools->overlaySurfaceEnabled()
       || m_landscapeTools->overlaySurfaceOperation()!=EOverlaySurfaceOperation::Deactivate)return false;
    QString error;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(!m_worldTerrainSession.load(m_pLand->filePath().absoluteFilePath(),&error)){emit updateMsg(error);return true;}
    syncWorldTerrainActive();
    QMap<QString,QPointF> origins;origins.insert(active,m_activeMapWorldOrigin);
    const QPointF world(position.x()+m_activeMapWorldOrigin.x(),position.y()+m_activeMapWorldOrigin.y());
    const double extent=m_landscapeTools->paintRadius()*2+2;
    for(const auto& placement:m_neighborPlacements) {
        const QString id=CWorldTerrainSession::key(placement.mprPath);
        if(!m_worldTerrainEnabledPaths.contains(id) || origins.contains(id))continue;
        if(!QRectF(placement.worldOrigin,placement.size).adjusted(-extent,-extent,extent,extent).contains(world))continue;
        if(!m_worldTerrainSession.load(placement.mprPath,&error)){emit updateMsg(error);return true;}
        origins.insert(id,placement.worldOrigin);
    }
    SWorldTerrainStroke stamp;CValidationReport report;
    for(auto it=origins.cbegin();it!=origins.cend();++it) {
        STerrainTileBrush brush;brush.centerX=int(std::floor((world.x()-it.value().x())/2));
        brush.centerY=int(std::floor((world.y()-it.value().y())/2));
        brush.radius=m_landscapeTools->paintRadius();brush.shape=m_landscapeTools->paintShape();
        const auto command=m_worldTerrainSession.documents[it.key()]->terrain.makeOverlayDeactivation(brush,&report);
        if(!command.isEmpty())stamp.overlays.insert(it.key(),command);
    }
    if(report.hasErrors()){emit updateMsg(report.toText());return true;}
    if(stamp.isEmpty())return true;
    if(!m_worldTerrainSession.apply(stamp,true,&error)){emit updateMsg(error);return true;}
    if(stamp.overlays.contains(active) && !applyOverlaySurfacePreview(stamp.overlays[active],true)) {
        m_worldTerrainSession.apply(stamp,false,&error);return true;
    }
    m_worldTerrainStroke.append(stamp);refreshWorldTerrainNeighbors();return true;
}

bool CView::ensureWorldTerrainAtlas(const QString& id,CValidationReport* report)
{
    const auto doc=m_worldTerrainSession.documents.value(id);if(!doc)return false;
    if(doc->atlas && m_worldTerrainAtlasSources.contains(id))return true;
    SWorldTerrainAtlasSource source;
    const auto& document=doc->baseline;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(id==active) {
        if(!ensureTerrainAutoCatalogReady() || !m_workingTerrainAtlas)return false;
        doc->atlas=m_workingTerrainAtlas;
        doc->terrain=m_terrainEdit;doc->materials=m_mprMaterials;
        source.profile=m_terrainAtlasProfile;source.archive=m_activeTerrainArchivePath;
    } else {
        const QString ns=document.mapInfo().innerNamespace;
        const QFileInfo file(doc->path);
        QStringList archives;
        const QString privateAtlas=file.dir().filePath(file.completeBaseName()+QStringLiteral("-atlas.res"));
        const QString adjacent=file.dir().filePath(QStringLiteral("textures.res"));
        QStringList otherConfiguredArchives;
        QString preferredHDPack;
        for(const QString& configured : CTextureList::getInstance()
                ->configuredResourceArchivePaths()) {
            if(terrainPublicationIsHDPackArchive(configured)) {
                archives.append(configured);
                if(preferredHDPack.isEmpty())preferredHDPack=configured;
            }
            else otherConfiguredArchives.append(configured);
        }
        if(QFileInfo::exists(privateAtlas))archives.append(privateAtlas);
        if(QFileInfo::exists(adjacent))archives.append(adjacent);
        archives.append(otherConfiguredArchives);
        archives.removeDuplicates();
        const auto embedded=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(archives,ns,
            preferredHDPack);
        STerrainAtlasProfile profile;
        QVector<QImage> images;
        if(embedded.success()) {profile=embedded.profile;images=embedded.decodedAtlasTiles;source.archive=embedded.archivePath;}
        else {
            const QStringList names=ei::terrainTextureResourceNames(ns,int(document.mapInfo().textureCount));
            QMap<QString,QByteArray> payloads;
            for(const auto& archive:archives) {
                payloads.clear();
                if(!CTerrainAtlasArchiveBindingResolver::readArchiveMembers(archive,names,&payloads))continue;
                bool complete=true;for(const auto& name:names)if(payloads.value(name).isEmpty())complete=false;
                if(complete){source.archive=archive;break;}payloads.clear();
            }
            if(payloads.isEmpty()) {
                report->add("terrain.world.atlas",EValidationSeverity::Error,
                    tr("Не найден полный атлас соседней карты %1. Исправление отменено.").arg(QFileInfo(doc->path).fileName()));return false;
            }
            for(const auto& name:names)for(int slot=0;slot<64;++slot) {
                QImage tile;QString error;
                if(!ei::decodeMmpDxt1AtlasTile(payloads.value(name),slot,&tile,&error)) {
                    report->add("terrain.world.atlas.decode",EValidationSeverity::Error,error);return false;
                }
                images.append(tile);
            }
            const auto hash=CTerrainAtlasProfileInference::pixelSha256(images);
            QString profilePath;bool legacy=false;
            if(!CTerrainAtlasProfileStore::loadExplicitOverrideForMpr(ns,hash,images.size(),file,&profile,&profilePath,&legacy,report)
               && !CTerrainAtlasProfileStore::loadExplicitOverride(ns,hash,images.size(),&profile,QString(),&profilePath,report))
                profile=CTerrainAtlasPresetCatalog::exactProfile(ns,hash,images.size(),report);
            if(profile.slotDefinitions.isEmpty())
                profile=CTerrainAtlasProfileInference::propose(ns,images,document.mapInfo().tileTypes,{},report);
        }
        const auto analysis=profile.usableAnalysis(report);
        if(!analysis.usable) {
            report->add("terrain.world.catalog",EValidationSeverity::Error,
                tr("Не удалось определить материалы соседней карты %1.").arg(QFileInfo(doc->path).fileName()));return false;
        }
        auto catalog=CTerrainAtlasPresetCatalog::globalizedCatalog(profile,analysis.overlayCatalogWithLandFamilies(),&m_terrainGlobalFamilies);

        for(const auto& family:m_terrainGlobalFamilies)catalog.declareMaterial(family.id);
        QMap<qint32,ETerrainBlendStyle> styles;QMap<qint32,qint32> runtimeTypes;
        for(const auto& family:m_terrainGlobalFamilies) {
            styles[family.id]=CTerrainVirtualAtlas::blendStyleForMaterialName(family.name);
            runtimeTypes[family.id]=family.runtimeTerrainType;
        }
        doc->atlas.reset(new CTerrainWorkingAtlas(catalog,images,m_terrainSemanticSeed,
            kMprPackedTerrainAtlasSlotLimit,m_terrainGlobalMaterialSources,styles,
            document.mapInfo().tileTypes,runtimeTypes));
        if(!doc->atlas->isValid()) {
            report->add("terrain.world.atlas",EValidationSeverity::Error,doc->atlas->error());doc->atlas.clear();return false;
        }
        source.profile=profile;
        restoreTerrainSemanticFieldFromArchive(document,profile,&doc->terrain,QFileInfo(doc->path),source.archive,false);
    }
    QMap<QString,QByteArray> pages;
    const auto names=ei::terrainTextureResourceNames(document.mapInfo().innerNamespace,int(document.mapInfo().textureCount));
    if(!CTerrainAtlasArchiveBindingResolver::readArchiveMembers(source.archive,names,&pages)) {
        report->add("terrain.world.pages",EValidationSeverity::Error,"Cannot read the complete original terrain atlas.");doc->atlas.clear();return false;
    }
    for(const auto& name:names) {
        if(pages.value(name).isEmpty()){doc->atlas.clear();return false;}
        source.pages.append(pages.value(name));
    }
    doc->terrain.enableWorkingAtlasSlots(doc->atlas->capacityLimit());
    doc->terrain.installWorkingAtlasSlotTypes(doc->atlas->activeTileTypes());
    m_worldTerrainAtlasSources[id]=source;return true;
}

bool CView::resolveWorldMaterialBlends(QString* summary,CValidationReport* report,
    STerrainResolveAuditSession* audit,bool seamsOnly)
{
    if(!m_worldTerrainEnabled || !m_hasActiveMapWorldOrigin || !m_neighborLayoutStorageWritable)return false;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    QMap<QString,QPointF> origins;origins[active]=m_activeMapWorldOrigin;
    const QRectF ar(m_activeMapWorldOrigin,QSizeF(m_terrainEdit.tileCountX()*2,m_terrainEdit.tileCountY()*2));
    bool expanded=true;
    while(expanded) {expanded=false;for(const auto& p:m_neighborPlacements) {
        const QString id=CWorldTerrainSession::key(p.mprPath);
        if(!m_worldTerrainEnabledPaths.contains(id) || origins.contains(id))continue;
        const QRectF br(p.worldOrigin,p.size);
        for(auto included=origins.cbegin();included!=origins.cend();++included) {
            QSizeF size=ar.size();
            if(included.key()!=active)for(const auto& known:m_neighborPlacements)
                if(CWorldTerrainSession::key(known.mprPath)==included.key()){size=known.size;break;}
            const QRectF edge(included.value(),size);
            const bool touching=((qAbs(edge.right()-br.left())<1e-5 || qAbs(edge.left()-br.right())<1e-5)
                && qMin(edge.bottom(),br.bottom())-qMax(edge.top(),br.top())>1e-5)
                || ((qAbs(edge.bottom()-br.top())<1e-5 || qAbs(edge.top()-br.bottom())<1e-5)
                && qMin(edge.right(),br.right())-qMax(edge.left(),br.left())>1e-5);
            if(touching){origins[id]=p.worldOrigin;expanded=true;break;}
        }
    }}
    bool incompatibleSkipped=false;
    if(m_landscapeTools->modernMixedEdge() && origins.size()>1) {
        QMap<QString,QSizeF> sizes;
        sizes[active]=QSizeF(m_terrainEdit.tileCountX()*2,m_terrainEdit.tileCountY()*2);
        for(const auto& placement:m_neighborPlacements) {
            const auto id=CWorldTerrainSession::key(placement.mprPath);
            if(origins.contains(id))sizes[id]=placement.size;
        }
        auto incompatible=worldMaterialIncompatibleStitchMaps(origins,sizes);
        incompatibleSkipped=!incompatible.isEmpty();
        for(const auto& id:incompatible)
            emit updateMsg(tr("Mixed-edge пропустил геометрически несовпадающий стык карты %1; эта пара не изменена.")
                .arg(QFileInfo(id).fileName()));
        incompatible.remove(active);
        for(const auto& id:incompatible)origins.remove(id);
    }
    if(origins.size()<2) {
        if(incompatibleSkipped) {
            *summary=tr("Переходы не изменены: геометрически несовпадающий стык пропущен.");
            return true;
        }
        return false;
    }
    bool completed=false;
    // Keep the already presented scene while CPU-only staging runs. A busy
    // render timer would redraw shadows/terrain behind the modal dialog.
    const bool renderTimerWasActive=m_timer&&m_timer->isActive();
    if(renderTimerWasActive)m_timer->stop();
    struct ResumeResolveRendering {
        std::function<void()> resume;
        ~ResumeResolveRendering(){resume();}
    } resumeRendering{[this,renderTimerWasActive]() {
        if(renderTimerWasActive)m_timer->start();update();
    }};
    CTerrainResolveTask progress(this);
    const auto cancelRepair=[&]() {
        completed=true;
        *summary=tr("Исправление переходов отменено. Изменения не применены.");
        if(audit)audit->outcome=QStringLiteral("cancelled");
        return true;
    };
    struct FailureReport {
        bool& completed;CValidationReport* report;
        ~FailureReport(){if(!completed && !report->hasErrors())report->add("terrain.world.resolve_stage",EValidationSeverity::Error,
            "The complete world material repair could not be staged; no repair was committed.");}
    } failureReport{completed,report};
    *summary=tr("Исправление переходов между картами не выполнено. Изменения не применены.");
    QString error;
    if(!m_worldTerrainSession.load(active,&error)) {
        report->add("terrain.world.load",EValidationSeverity::Error,error);return true;
    }
    syncWorldTerrainActive();
    QMap<QString,SWorkingTerrainResolveStage> stages;
    SWorldTerrainStroke stamp;
    const auto layers={ETerrainTileLayer::Land,ETerrainTileLayer::Overlay};
    const auto failedMapStage=[this,report](const QString& id,ETerrainTileLayer layer) {
        report->add("terrain.world.resolve_map",EValidationSeverity::Error,
            tr("Не удалось подготовить переходы; общая операция отменена."),
            QFileInfo(id).fileName()+QStringLiteral(" / ")
                +(layer==ETerrainTileLayer::Land?tr("земля"):tr("покрытие")));
    };
    for(auto it=origins.cbegin();it!=origins.cend();++it) {
        if(!progress.checkpoint(tr("Подготовка карты %1").arg(QFileInfo(it.key()).fileName())))return cancelRepair();
        if(!m_worldTerrainSession.load(it.key(),&error)) {
            report->add("terrain.world.load",EValidationSeverity::Error,error);return true;
        }
        if(!ensureWorldTerrainAtlas(it.key(),report))return true;
        const auto doc=m_worldTerrainSession.documents[it.key()];
        auto& stage=stages[it.key()];stage.terrainEdit=doc->terrain;stage.atlas=doc->atlas->createStagingCopy();
        stage.allowSlotRecycling=false;
        // Cohort snapshots and original compressed pages remain immutable.
        // Repair may append artwork, never recycle a pre-existing slot/page.
        for(int slot=0;slot<stage.atlas->activeTileImages().size();++slot)stage.protectedSlots.insert(slot);
        stage.materialSources=m_terrainGlobalMaterialSources;stage.materialSourceIdentities=m_terrainGlobalMaterialSourceIdentities;
        stage.boundAtlasCache=m_terrainGlobalBoundAtlasCache;stage.missingPages=m_terrainGlobalTransitionMissingPages;
        stage.decodedTileCache=m_terrainGlobalTransitionTileCache;stage.uniformTileCache=m_terrainGlobalUniformTileCache;
        for(const auto& animation:doc->baseline.mapInfo().animatedTiles)
            for(int frame=0;frame<qMax(1,int(animation.frameCount));++frame)stage.protectedSlots.insert(animation.tileIndex+frame);
        for(auto layer:layers)
            if(!stage.terrainEdit.initializeVirtualMaterialField(layer,stage.atlas->catalog(),report,
                    ETerrainMaterialFieldRecovery::RecoverDeterministically))return true;
    }
    QMap<QString,QSet<int>> preserve[2];QMap<QString,QMap<int,int>> seamSides[2];
    QMap<QString,QSet<int>> worldPlanChangedCells;
    QSet<QString> semanticChanges;int conflicts=0,boundaryVertices=0;
    for(auto layer:layers) {
        if(!progress.checkpoint(tr("Согласование материалов стыков")))return cancelRepair();
        const int li=layer==ETerrainTileLayer::Land?0:1;
        QVector<SWorldMaterialRepairMap> maps;
        for(auto it=stages.cbegin();it!=stages.cend();++it) {
            SWorldMaterialRepairMap m;m.id=it.key();m.origin=origins[it.key()];
            m.geometry=&it->terrainEdit;m.catalog=&it->atlas->catalog();m.layer=layer;
            m.protectedCells=nativeWorldRepairCells(*m_worldTerrainSession.documents[it.key()],
                *it->atlas,it->terrainEdit,layer);
            preserve[li][it.key()].unite(m.protectedCells);
            if(!it->terrainEdit.copyVirtualMaterialField(layer,&m.field,false)) {
                report->add("terrain.world.field",EValidationSeverity::Error,tr("Не удалось прочитать материалы карты %1.").arg(QFileInfo(it.key()).fileName()));return true;
            }
            maps.append(m);
        }
        const auto plan=planWorldMaterialRepair(maps,true);boundaryVertices+=plan.changedVertices;
        seamSides[li]=worldMaterialSeamSides(maps);
        for(auto changes=plan.changedCells.cbegin();changes!=plan.changedCells.cend();++changes)
            if(!changes->isEmpty()) {
                semanticChanges.insert(changes.key());
                worldPlanChangedCells[changes.key()].unite(changes.value());
            }
        for(auto it=stages.begin();it!=stages.end();++it) {
            if(!progress.checkpoint(tr("Поиск недостающих переходов: %1").arg(QFileInfo(it.key()).fileName())))return cancelRepair();
            const auto& field=plan.fields[it.key()];const int w=field.tileWidth(),h=field.tileHeight();
            for(const auto& vertex:plan.conflicts.value(it.key())) {
                ++conflicts;
                if(conflicts<=100)report->add("terrain.world.material_conflict",EValidationSeverity::Warning,
                    tr("%1, %2, вершина %3,%4: нет единственного материала стыка; исходное значение сохранено.")
                        .arg(QFileInfo(it.key()).fileName()).arg(li?tr("покрытие"):tr("земля")).arg(vertex.x()).arg(vertex.y()));
            }
            // Only actually shared neighbor edges belong to this command.
            if(it.key()!=active)for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                if(!plan.seamCells.value(it.key()).contains(y*w+x))preserve[li][it.key()].insert(y*w+x);
            QVector<qint32> current,baseline;it->terrainEdit.copyVirtualMaterialFieldState(layer,&current,&baseline);
            current.resize((w+1)*(h+1));
            for(int y=0;y<=h;++y)for(int x=0;x<=w;++x)current[y*(w+1)+x]=field.material(x,y);
            if(!it->terrainEdit.restoreVirtualMaterialFieldState(layer,current,baseline,report))return true;
        }
    }
    QMap<QString,QSet<int>> mixedRepairCells;
    QMap<qint32,qint32> mixedMaterialTypes;
    if(m_landscapeTools->modernMixedEdge()) {
        QSet<qint32> requiredMaterials;QSet<QPair<qint32,qint32>> requiredPairs;QSize sourceSize;
        for(auto it=stages.begin();it!=stages.end();++it) {
            const auto images=it->atlas->activeTileImages();
            if(images.isEmpty()) {report->add("terrain.world.mixed_atlas",EValidationSeverity::Error,
                tr("РђС‚Р»Р°СЃ РєР°СЂС‚С‹ %1 РїСѓСЃС‚.").arg(QFileInfo(it.key()).fileName()));return true;}
            if(!collectWorldRasterSize(*it->atlas,&sourceSize,report,it.key()))return true;
            CTerrainVirtualMaterialField field;
            if(!it->terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false))return true;
            QSet<int> candidate=seamsOnly
                ?seamSides[0].value(it.key()).keys().toSet()
                :worldPlanChangedCells.value(it.key());
            if(!seamsOnly)candidate.unite(seamSides[0].value(it.key()).keys().toSet());
            const auto& excluded=preserve[0][it.key()];
            for(int y=0;y<field.tileHeight();++y)for(int x=0;x<field.tileWidth();++x)
                if(!seamsOnly&&!excluded.contains(y*field.tileWidth()+x)) {
                    STerrainTransitionSignature signature;
                    if(it->terrainEdit.missingBlendRepairSignature(x,y,ETerrainTileLayer::Land,
                        it->atlas->catalog(),&field,true,&signature))candidate.insert(y*field.tileWidth()+x);
                }
            if(!seamsOnly)
                candidate=CTerrainMixedEdge::expandAffectedCells(it->terrainEdit,it->atlas.data(),candidate);
            mixedRepairCells[it.key()]=candidate;
            for(int cell:candidate) {
                bool found=false;const auto signature=field.cellSignature(cell%field.tileWidth(),cell/field.tileWidth(),&found);
                if(!found)continue;
                requiredMaterials<<signature.northWest<<signature.northEast<<signature.southEast<<signature.southWest;
                QSet<qint32> ids{signature.northWest,signature.northEast,signature.southEast,signature.southWest};
                auto ordered=ids.values();std::sort(ordered.begin(),ordered.end());
                for(int a=0;a<ordered.size();++a)for(int b=a+1;b<ordered.size();++b)
                    requiredPairs.insert(qMakePair(ordered[a],ordered[b]));
            }
        }
        for(const auto& family:m_terrainGlobalFamilies)
            if(family.layer==ETerrainAtlasLayer::Land&&family.runtimeTerrainType>=0&&family.runtimeTerrainType<16)
                mixedMaterialTypes[family.id]=family.runtimeTerrainType;
        for(const auto& family:m_terrainGlobalFamilies)
            if(!mixedMaterialTypes.contains(family.id)&&family.runtimeTerrainType>=0&&family.runtimeTerrainType<16)
                mixedMaterialTypes[family.id]=family.runtimeTerrainType;
        if(!inferWorldMaterialRuntimeTypes(stages,requiredMaterials,&mixedMaterialTypes,report))return true;
        QMap<qint32,QImage> bankSources;
        if(!requiredMaterials.isEmpty()&&!loadWorldTerrainMaterialSources(requiredMaterials,sourceSize,&bankSources,report,
            [&progress](){return progress.checkpoint(tr("Загрузка общих источников материалов"));})) {
            if(progress.cancelled())return cancelRepair();
            return true;
        }
        for(auto target=stages.begin();target!=stages.end();++target) {
            if(!progress.checkpoint(tr("Подготовка общих текстур: %1").arg(QFileInfo(target.key()).fileName())))return cancelRepair();
            const bool imported=progress.run(tr("Общие текстуры карты %1").arg(QFileInfo(target.key()).fileName()),[&]() {
            const auto native=terrainAtlasMaximumNativeTileSize(target->atlas->authoredTileImages());
            const auto rasterBank=worldRasterSources(bankSources,native);
            for(auto source=rasterBank.cbegin();source!=rasterBank.cend();++source)
                if(!target->atlas->hasMaterialSource(source.key())
                   &&!target->atlas->addExternalMaterialSource(source.key(),source.value())) {
                    report->add("terrain.world.material_source",EValidationSeverity::Error,
                        tr("РћР±С‰РёР№ РёСЃС‚РѕС‡РЅРёРє РјР°С‚РµСЂРёР°Р»Р° %1 РЅРµ РїРѕРґС…РѕРґРёС‚ Р°С‚Р»Р°СЃСѓ РєР°СЂС‚С‹ %2.")
                            .arg(source.key()).arg(QFileInfo(target.key()).fileName()));return false;
                }
            for(auto source=rasterBank.cbegin();source!=rasterBank.cend();++source) {
                const int runtime=mixedMaterialTypes.value(source.key(),-1);
                if(runtime<0||runtime>15)continue;
                STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=source.key();
                if(target->atlas->importPhysicalTile(source.value(),runtime,&pure,
                    QStringLiteral("world-bank:uniform:%1").arg(source.key()),0,report,true)<0)return false;
            }
            for(auto source=stages.cbegin();source!=stages.cend();++source) {
                const auto images=source->atlas->authoredTileImages();
                for(int slot=0;slot<source->atlas->authoredSlotCount();++slot) {
                    if(!progress.updateWork(slot,source->atlas->authoredSlotCount()))return false;
                    SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);
                    bool known=false;const auto signature=source->atlas->catalog().gridSignatureForPacked(packed.encode(),&known);
                    if(!known||slot>=images.size()||images[slot].isNull())continue;
                    QSet<qint32> ids{signature.northWest,signature.northEast,signature.southEast,signature.southWest};
                    if(ids.size()!=2)continue;
                    auto ordered=ids.values();std::sort(ordered.begin(),ordered.end());
                    if(!requiredPairs.contains(qMakePair(ordered[0],ordered[1])))continue;
                    const int runtime=source->atlas->runtimeTerrainTypeForSlot(slot);
                    if(runtime<0||runtime>15)continue;
                    if(images[slot].width()<native.width())continue;
                    const auto raster=terrainAtlasRasterTile(images[slot],native);
                    if(target->atlas->importPhysicalTile(raster,runtime,&signature,
                        QStringLiteral("world-bank:edge:%1:%2").arg(QFileInfo(source.key()).fileName()).arg(slot),
                        slot,report,true)<0)return false;
                }
            }
            return true;
            });
            if(progress.cancelled())return cancelRepair();
            if(!imported)return true;
        }
        if(!importWorldMixedEdgeTransitionBank(&stages,requiredPairs,mixedMaterialTypes,
            sourceSize,report,[&progress](){return progress.checkpoint(tr("Подготовка общего банка переходов"));})) {
            if(progress.cancelled())return cancelRepair();
            return true;
        }
    }
    int cells=0,generatedRecipes=0,reusedRecipes=0,generatedSeams=0,reusedSeams=0;
    for(auto it=stages.begin();it!=stages.end();++it) {
        if(!progress.checkpoint(tr("Подготовка переходов: %1").arg(QFileInfo(it.key()).fileName())))return cancelRepair();
        auto& stage=it.value();const auto doc=m_worldTerrainSession.documents[it.key()];
        QMap<int,quint64> usage;
        for(int y=0;y<stage.terrainEdit.tileCountY();++y)for(int x=0;x<stage.terrainEdit.tileCountX();++x)
            for(auto layer:layers) {
                bool available=false;const auto packed=stage.terrainEdit.packedTile(x,y,layer,&available);
                if(available && packed!=0xffff)++usage[SMprPackedTile::decode(packed).atlasSlot()];
            }
        for(auto layer:layers) {
            const int li=layer==ETerrainTileLayer::Land?0:1;
            CTerrainVirtualMaterialField field;
            if(!stage.terrainEdit.copyVirtualMaterialField(layer,&field,false))return true;
            const auto& excluded=preserve[li][it.key()];
            if(m_landscapeTools->modernMixedEdge() && layer==ETerrainTileLayer::Land) {
                const QSet<int> cells=mixedRepairCells.value(it.key());
                const QPoint cellOrigin(qRound(origins[it.key()].x()/2),qRound(origins[it.key()].y()/2));
                STerrainTileCommand resolved;STerrainMixedEdgeStatistics statistics;
                const auto animations=doc->baseline.mapInfo().animatedTiles;
                const auto distributionSeed=m_tileStrokeDistributionSeed;
                const bool randomRotation=m_landscapeTools->randomizeUniformRotation();
                const bool prepared=progress.run(tr("Переходы карты %1").arg(QFileInfo(it.key()).fileName()),
                    [&stage,&resolved,&statistics,&progress,&mixedMaterialTypes,animations,cellOrigin,cells,
                     distributionSeed,randomRotation,report,seamsOnly]() {
                        resolved=CTerrainMixedEdge::resolve(stage.terrainEdit,stage.atlas.data(),cells,
                            mixedMaterialTypes,animations,cellOrigin,0,distributionSeed,randomRotation,report,
                            kMprPackedTerrainAtlasSlotLimit,true,!seamsOnly,
                            [&progress](int done,int total){return progress.updateWork(done,total);},&statistics);
                        return !report->hasErrors();
                    });
                if(progress.cancelled())return cancelRepair();
                generatedRecipes+=statistics.rasterized;reusedRecipes+=statistics.reused;
                ei::log(eLogInfo,QString("World mixed-edge map:%1 rasterized:%2 reused:%3")
                    .arg(QFileInfo(it.key()).fileName()).arg(statistics.rasterized).arg(statistics.reused));
                if(!prepared){failedMapStage(it.key(),layer);return true;}
                if(report->hasErrors() || !stage.terrainEdit.installWorkingAtlasSlotTypes(stage.atlas->activeTileTypes())
                   ||(!resolved.isEmpty()&&!stage.terrainEdit.apply(resolved,true,report))) {
                    failedMapStage(it.key(),layer);return true;
                }
                continue;
            }
            for(int y=0;y<field.tileHeight();++y)for(int x=0;x<field.tileWidth();++x) {
                if(excluded.contains(y*field.tileWidth()+x))continue;
                STerrainTransitionSignature signature;
                if(!stage.terrainEdit.missingBlendRepairSignature(x,y,layer,stage.atlas->catalog(),&field,true,&signature))continue;
                const int runtime=stage.atlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(stage.terrainEdit.packedTile(x,y,layer)).atlasSlot());
                if(runtime<0 || !realizeWorkingTerrainSignature(signature,x,y,&usage,report,nullptr,0,runtime,&stage,true)) {
                    if(!report->hasErrors())report->add("terrain.world.resolve",EValidationSeverity::Error,tr("Не удалось подготовить точный переход %1: %2,%3.").arg(QFileInfo(it.key()).fileName()).arg(x).arg(y));
                    return true;
                }
            }
            if(!stage.terrainEdit.installWorkingAtlasSlotTypes(stage.atlas->activeTileTypes()))return true;
            const auto command=stage.terrainEdit.makeVirtualMapResolution(layer,stage.atlas->catalog(),m_terrainSemanticSeed,
                report,false,&field,true,true,nullptr,nullptr,&excluded);
            if(report->hasErrors() || !stage.terrainEdit.apply(command,true,report)) {
                failedMapStage(it.key(),layer);return true;
            }
            cells+=command.deltas.size();
        }
    }
    // The same canonical source set is used by every member of the cohort.
    // Local atlases may contain different authored masks for identical tuples.
    for(auto layer:layers) {
        const int li=layer==ETerrainTileLayer::Land?0:1;
        QSet<qint32> required;QSize size;
        for(auto it=stages.cbegin();it!=stages.cend();++it) {
            if(!collectWorldRasterSize(*it->atlas,&size,report,it.key()))return true;
            CTerrainVirtualMaterialField field;
            it->terrainEdit.copyVirtualMaterialField(layer,&field,false);
            const auto edges=seamSides[li].value(it.key());
            for(auto edge=edges.cbegin();edge!=edges.cend();++edge) {
                const int x=edge.key()%field.tileWidth(),y=edge.key()/field.tileWidth();
                bool available=false;const auto packed=it->terrainEdit.packedTile(x,y,layer,&available);
                if(!available || (layer==ETerrainTileLayer::Overlay && packed==0xffff))continue;
                bool found=false;const auto sig=field.cellSignature(x,y,&found);
                if(found)required<<sig.northWest<<sig.northEast<<sig.southEast<<sig.southWest;
            }
        }
        if(required.isEmpty())continue;
        if(!progress.checkpoint(tr("Подготовка рисунка общих границ")))return cancelRepair();
        QMap<qint32,QImage> sources;
        if(!loadWorldTerrainMaterialSources(required,size,&sources,report,
            [&progress](){return progress.checkpoint(tr("Загрузка текстур рисунка стыков"));})) {
            if(progress.cancelled())return cancelRepair();
            return true;
        }
        QMap<qint32,ETerrainBlendStyle> styles;
        for(const auto& family:m_terrainGlobalFamilies)
            styles[family.id]=CTerrainVirtualAtlas::blendStyleForMaterialName(family.name);
        CWorldRasterAtlasBank sharedBank(sources,styles,layer==ETerrainTileLayer::Land);
        QMap<QByteArray,QImage> canonicalCache;
        for(auto it=stages.begin();it!=stages.end();++it) {
            auto& stage=it.value();CTerrainVirtualMaterialField field;
            stage.terrainEdit.copyVirtualMaterialField(layer,&field,false);
            STerrainTileCommand command;command.description=tr("Согласовать рисунок стыка карт");
            const auto edges=seamSides[li].value(it.key());
            if(!progress.checkpoint(tr("Согласование рисунка стыков: %1").arg(QFileInfo(it.key()).fileName())))return cancelRepair();
            for(auto edge=edges.cbegin();edge!=edges.cend();++edge) {
                const int x=edge.key()%field.tileWidth(),y=edge.key()/field.tileWidth();
                bool available=false;const quint16 before=stage.terrainEdit.packedTile(x,y,layer,&available);
                if(!available || (layer==ETerrainTileLayer::Overlay && before==0xffff))continue;
                bool found=false;const auto sig=field.cellSignature(x,y,&found);
                if(!found)continue;
                const auto bodyTile=SMprPackedTile::decode(before);
                const auto bodyImages=stage.atlas->activeTileImages();
                // Mixed resolve rebuilds B2 cells before this pass. It retains
                // already-matching uniform artwork, which can be an earlier
                // anchored result; do not repeatedly feather that result.
                const bool mixedSignature=!(sig.northWest==sig.northEast&&sig.northWest==sig.southEast&&sig.northWest==sig.southWest);
                const bool preserveMixedBody=layer==ETerrainTileLayer::Land&&m_landscapeTools->modernMixedEdge()&&mixedSignature;
                if(preserveMixedBody&&(bodyTile.atlasSlot()>=bodyImages.size()||bodyImages[bodyTile.atlasSlot()].isNull())) {
                    failedMapStage(it.key(),layer);return true;
                }
                const auto native=preserveMixedBody?bodyImages[bodyTile.atlasSlot()].size()
                    :terrainAtlasMaximumNativeTileSize(stage.atlas->authoredTileImages());
                const auto shared=sharedBank.forSize(native);
                const int runtime=stage.atlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(before).atlasSlot());
                const QImage body=preserveMixedBody?bodyImages[bodyTile.atlasSlot()]:QImage();
                const quint8 rotation=preserveMixedBody?bodyTile.rotation:0;
                QByteArray recipeKey=shared->worldBoundaryRecipeKey(sig,stage.terrainEdit,origins[it.key()],
                    x,y,edge.value(),body,rotation);
                recipeKey.append(char(runtime));
                int slot=stage.atlas->worldBoundarySlot(recipeKey);
                if(slot<0) {
                    QImage pixels;
                    const bool prepared=progress.run(tr("Рисунок стыков: %1").arg(QFileInfo(it.key()).fileName()),[&]() {
                        pixels=shared->worldBoundaryTile(sig,stage.terrainEdit,origins[it.key()],
                            x,y,edge.value(),&canonicalCache,report,body,rotation);
                        return !pixels.isNull();
                    });
                    if(progress.cancelled())return cancelRepair();
                    if(!prepared){failedMapStage(it.key(),layer);return true;}
                    slot=stage.atlas->importWorldBoundaryTile(pixels,runtime,sig,report);
                    stage.atlas->rememberWorldBoundaryRecipe(recipeKey,slot);
                    ++generatedSeams;
                } else ++reusedSeams;
                if(slot<0){failedMapStage(it.key(),layer);return true;}
                SMprPackedTile tile;tile.atlasIndex=quint8(slot/64);tile.tileIndex=quint8(slot%64);
                if(tile.encode()==before)continue;
                STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=layer;
                delta.before=before;delta.after=tile.encode();command.deltas.append(delta);
            }
            stage.terrainEdit.installWorkingAtlasSlotTypes(stage.atlas->activeTileTypes());
            if(!stage.terrainEdit.apply(command,true,report)){failedMapStage(it.key(),layer);return true;}
        }
    }
    cells=0;QMap<QString,QVector<STerrainTileDelta>> worldDeltas;
    for(auto it=stages.begin();it!=stages.end();++it) {
        auto& stage=it.value();const auto doc=m_worldTerrainSession.documents[it.key()];
        for(int y=0;y<stage.terrainEdit.tileCountY();++y)for(int x=0;x<stage.terrainEdit.tileCountX();++x)
            for(auto layer:layers) {
                const auto before=doc->terrain.packedTile(x,y,layer),after=stage.terrainEdit.packedTile(x,y,layer);
                if(before==after)continue;
                STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=layer;
                delta.before=before;delta.after=after;worldDeltas[it.key()].append(delta);++cells;
            }
        if(worldDeltas.value(it.key()).isEmpty() && !semanticChanges.contains(it.key()))continue;
        SWorldTerrainPaintChange change;change.before.terrain=doc->terrain;change.before.atlas=doc->atlas->createStagingCopy();
        change.before.materials=doc->materials;change.after.terrain=stage.terrainEdit;
        change.after.atlas=stage.atlas;change.after.materials=doc->materials;stamp.paint[it.key()]=change;
    }
    if(!stamp.isEmpty()) {
        if(progress.cancelled())return cancelRepair();
        progress.lockForCommit();
        if(!m_worldTerrainSession.apply(stamp,true,&error)) {report->add("terrain.world.apply",EValidationSeverity::Error,error);return true;}
        bool previewed=true;
        for(const auto& id:stamp.paint.keys()) {
            progress.checkpoint(tr("Обновление карты %1").arg(QFileInfo(id).fileName()));
            previewed=previewWorldPaintState(id) && previewed;
        }
        if(!previewed) {
            m_worldTerrainSession.apply(stamp,false,&error);
            for(const auto& id:stamp.paint.keys())previewWorldPaintState(id);
            report->add("terrain.world.preview",EValidationSeverity::Error,tr("Предпросмотр не обновлён; исправление отменено."));return true;
        }
        stamp.localUndoIndex=m_pUndoStack?m_pUndoStack->index():-1;m_worldTerrainSession.record(stamp);
        refreshWorldTerrainNeighbors();updateWorldTerrainStatus();
    }
    if(audit) {
        audit->generatedTupleCount=generatedRecipes+generatedSeams;
        audit->worldPackedDeltas=worldDeltas;
        audit->stagedCellCount=cells;
        audit->skippedCellCount=conflicts;
        if(conflicts){STerrainResolveSkippedReason reason;reason.reason="world-boundary-material-conflict";
            reason.count=conflicts;audit->skippedReasons.append(reason);}
        if(!stamp.isEmpty()){audit->undoCommandId="world-terrain-cohort";audit->undoCommandDescription="World material boundary repair";}
    }
    *summary=tr("Исправлено тайлов: %1; карт: %2. Согласовано граничных вершин: %3.\nОтменить исправление можно кнопкой общей кисти.")
        .arg(cells).arg(stamp.paint.size()).arg(boundaryVertices);
    if(conflicts)*summary+=tr("\nНеоднозначных точек на стыках: %1. Их исходные материалы сохранены; подробности — в отчёте.").arg(conflicts);
    ei::log(eLogInfo,QString("World material repair maps:%1 cells:%2 boundaryVertices:%3 conflicts:%4").arg(stamp.paint.size()).arg(cells).arg(boundaryVertices).arg(conflicts));
    ei::log(eLogInfo,QString("World repair reuse recipesBaked:%1 recipesReused:%2 seamsBaked:%3 seamsReused:%4")
        .arg(generatedRecipes).arg(reusedRecipes).arg(generatedSeams).arg(reusedSeams));
    completed=true;
    return true;
}

bool CView::paintWorldTerrainAt(const QVector3D& position,bool land)
{
    if(!m_worldTerrainEnabled || !m_hasActiveMapWorldOrigin || !m_neighborLayoutStorageWritable
       || !m_landscapeTools || m_terrainStampRegressionFootprintOverride)return false;
    if(m_landscapeTools->overlaySurfaceEnabled()
       && m_landscapeTools->overlaySurfaceOperation()==EOverlaySurfaceOperation::Deactivate)
        return eraseWorldOverlayAt(position);
    QString error;CValidationReport report;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(!m_worldTerrainSession.load(m_pLand->filePath().absoluteFilePath(),&error)){emit updateMsg(error);return true;}
    syncWorldTerrainActive();
    const QPointF world(position.x()+m_activeMapWorldOrigin.x(),position.y()+m_activeMapWorldOrigin.y());
    QMap<QString,QPointF> origins;origins.insert(active,m_activeMapWorldOrigin);
    const double extent=m_landscapeTools->paintRadius()*2+2;
    for(const auto& placement:m_neighborPlacements) {
        const QString id=CWorldTerrainSession::key(placement.mprPath);
        if(origins.contains(id) || !m_worldTerrainEnabledPaths.contains(id))continue;
        if(!QRectF(placement.worldOrigin,placement.size).adjusted(-extent,-extent,extent,extent).contains(world))continue;
        if(!m_worldTerrainSession.load(placement.mprPath,&error)){emit updateMsg(error);return true;}
        origins.insert(id,placement.worldOrigin);
    }
    if(m_tileStrokeMixedEdge && origins.size()<2)
        return false; // Keep the established single-map transaction byte-identical.
    if(m_tileStrokeMixedEdge && origins.size()>1) {
        QMap<QString,QSizeF> sizes;
        sizes[active]=QSizeF(m_terrainEdit.tileCountX()*2,m_terrainEdit.tileCountY()*2);
        for(const auto& placement:m_neighborPlacements) {
            const auto id=CWorldTerrainSession::key(placement.mprPath);
            if(origins.contains(id))sizes[id]=placement.size;
        }
        auto incompatible=worldMaterialIncompatibleStitchMaps(origins,sizes);
        for(const auto& id:incompatible)
            emit updateMsg(tr("Mixed-edge пропустил геометрически несовпадающий стык карты %1; эта пара не изменена.")
                .arg(QFileInfo(id).fileName()));
        incompatible.remove(active);
        for(const auto& id:incompatible)origins.remove(id);
        if(origins.size()<2)return true;
    }
    if(!ensureWorldTerrainAtlas(active,&report)){emit updateMsg(report.toText());return true;}
    const auto palette=m_landscapeTools->paintPaletteMode();
    const bool manual=manualTilePaintingEnabled();
    const bool classic=palette==ETerrainPaintPaletteMode::ClassicAuto;
    if(m_tileStrokeMixedEdge && (palette!=ETerrainPaintPaletteMode::Modern || !land)) {
        emit updateMsg(tr("Mixed-edge общей кисти поддерживает только покраску Ground в режиме Modern."));
        return true;
    }
    const bool tileTarget=m_landscapeTools->paintTargetMode()==ETerrainPaintTargetMode::Tile;
    const bool activating=m_landscapeTools->overlaySurfaceEnabled()
        && m_landscapeTools->overlaySurfaceOperation()==EOverlaySurfaceOperation::Activate;
    const auto layer=land?ETerrainTileLayer::Land:ETerrainTileLayer::Overlay;
    bool selected=false;const auto material=m_landscapeTools->paintTerrainType(&selected);
    int selectedSlot=-1,rotation=0;
    if(manual)m_pTileForm->getSelectedTile(selectedSlot,rotation);
    if(sampledWaterPaintingEnabled()) {
        const auto sample=SMprPackedTile::decode(quint16(m_sampledWaterPacked));
        selectedSlot=sample.atlasSlot();rotation=sample.rotation;
    }
    if((manual && selectedSlot<0) || (!manual && !selected)){emit updateMsg(tr("Выберите материал или тайл."));return true;}
    const auto activeAtlas=m_worldTerrainSession.documents[active]->atlas;
    int liquid=-1;
    if(activating) {
        liquid=m_landscapeTools->overlayMaterialIndex(&selected);
        if(!selected || liquid<0 || liquid>=m_mprMaterials.size()){emit updateMsg(tr("Выберите материал покрытия."));return true;}
    }
    int planned=0;
    int changedTiles=0;
    SWorldTerrainStroke stamp;
    const bool mixedEdge=m_tileStrokeMixedEdge;
    QMap<QString,SWorkingTerrainResolveStage> mixedStages;
    QMap<QString,QSet<int>> mixedPaintCells;
    for(auto target=origins.cbegin();target!=origins.cend();++target) {
        if(!ensureWorldTerrainAtlas(target.key(),&report))break;
        const auto doc=m_worldTerrainSession.documents[target.key()];
        SWorldTerrainPaintChange change;
        change.before.terrain=doc->terrain;change.before.materials=doc->materials;
        change.before.atlas=doc->atlas->createStagingCopy();
        SWorkingTerrainResolveStage stage;stage.terrainEdit=doc->terrain;
        stage.atlas=doc->atlas->createStagingCopy();stage.materialSources=m_terrainGlobalMaterialSources;
        stage.materialSourceIdentities=m_terrainGlobalMaterialSourceIdentities;
        stage.boundAtlasCache=m_terrainGlobalBoundAtlasCache;stage.missingPages=m_terrainGlobalTransitionMissingPages;
        stage.decodedTileCache=m_terrainGlobalTransitionTileCache;stage.uniformTileCache=m_terrainGlobalUniformTileCache;
        stage.atlas->setDistributionSeed(m_tileStrokeDistributionSeed);
        QMap<int,quint64> usage;
        if(!manual)for(int y=0;y<stage.terrainEdit.tileCountY();++y)for(int x=0;x<stage.terrainEdit.tileCountX();++x)
            for(const auto l:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                bool available=false;const auto packed=stage.terrainEdit.packedTile(x,y,l,&available);
                if(available && !(l==ETerrainTileLayer::Overlay && packed==0xffff))++usage[SMprPackedTile::decode(packed).atlasSlot()];
            }
        for(const auto& animation:doc->baseline.mapInfo().animatedTiles)
            for(int frame=0;frame<qMax(1,int(animation.frameCount));++frame)stage.protectedSlots.insert(animation.tileIndex+frame);
        STerrainTileBrush brush;brush.centerX=std::floor((world.x()-target.value().x())/2);
        brush.centerY=std::floor((world.y()-target.value().y())/2);
        brush.radius=m_landscapeTools->paintRadius();brush.shape=m_landscapeTools->paintShape();
        STerrainTileBrush vertices=brush;vertices.centerX=qRound((world.x()-target.value().x())/2);
        vertices.centerY=qRound((world.y()-target.value().y())/2);
        STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=material;
        int slot=-1;
        if(manual) {
            const auto images=activeAtlas->activeTileImages();
            if(selectedSlot>=images.size()){report.add("terrain.world.selection",EValidationSeverity::Error,"Invalid selected atlas slot.");break;}
            SMprPackedTile packed;packed.atlasIndex=quint8(selectedSlot/64);packed.tileIndex=quint8(selectedSlot%64);
            bool known=false;const auto signature=activeAtlas->catalog().gridSignatureForPacked(packed.encode(),&known);
            slot=target.key()==active ? selectedSlot : stage.atlas->importPhysicalTile(images[selectedSlot],activeAtlas->runtimeTerrainTypeForSlot(selectedSlot),
                known?&signature:nullptr,m_mprDocument.mapInfo().innerNamespace,selectedSlot,&report);
            if(slot<0)break;
        } else {
            qint32 requiredType=-1;
            if(activating)for(const auto& family:m_terrainGlobalFamilies)if(family.id==material)requiredType=family.runtimeTerrainType;
            if(!realizeWorkingTerrainSignature(pure,qRound(brush.centerX),qRound(brush.centerY),&usage,&report,
                nullptr,material,requiredType,&stage,activating))break;
            bool found=false;const auto packed=stage.atlas->catalog().selectForPaint(pure,qRound(brush.centerX),qRound(brush.centerY),m_tileStrokeDistributionSeed,&found);
            if(!found){report.add("terrain.world.uniform",EValidationSeverity::Error,"Material uniform tile unavailable.");break;}
            slot=SMprPackedTile::decode(packed).atlasSlot();rotation=SMprPackedTile::decode(packed).rotation;
        }
        stage.terrainEdit.enableWorkingAtlasSlots(stage.atlas->capacityLimit());
        if(!stage.terrainEdit.installWorkingAtlasSlotTypes(stage.atlas->activeTileTypes()))break;
        change.after.materials=doc->materials;
        bool changed=false;
        if(activating) {
            int localMaterial=change.after.materials.indexOf(m_mprMaterials[liquid]);
            if(localMaterial<0){localMaterial=change.after.materials.size();change.after.materials.append(m_mprMaterials[liquid]);}
            stage.terrainEdit.extendMaterialTable(change.after.materials.size());
            const auto surface=stage.terrainEdit.makeOverlayActivation(brush,qint16(localMaterial),quint8(slot/64),quint8(slot%64),quint8(rotation),&report);
            if(!surface.isEmpty()) {if(!stage.terrainEdit.apply(surface,true,&report))break;changed=true;}
        }
        STerrainTileCommand command;
        stage.atlas->setUniformPaintSeed(!manual && layer==ETerrainTileLayer::Land
            && m_landscapeTools->randomPaintVariants(),stage.terrainEdit.hasRandomPaintSeed()
                ? stage.terrainEdit.randomPaintSeed() : m_terrainSemanticSeed);
        const auto& catalog=stage.atlas->catalog();
        if(manual)command=stage.terrainEdit.makeTilePaint(brush,layer,quint8(slot/64),quint8(slot%64),quint8(rotation),catalog,&report);
        else if(classic) {
            const auto policy=m_landscapeTools->classicAutoSafeMode()?ETerrainSemanticPaintPolicy::ExactExistingOnly:ETerrainSemanticPaintPolicy::AllowLocalHardEdges;
            if(tileTarget)command=stage.terrainEdit.makeSemanticTilePaint(brush,layer,material,catalog,m_tileStrokeDistributionSeed,&report,policy);
            else command=stage.terrainEdit.makeSemanticVertexPaint(qBound(0,qRound(brush.centerX),stage.terrainEdit.tileCountX()-1),
                qBound(0,qRound(brush.centerY),stage.terrainEdit.tileCountY()-1),vertices,layer,material,catalog,m_tileStrokeDistributionSeed,
                &report,m_landscapeTools->paintTerrainSupport(),policy);
        } else if(!m_tileStrokeAutoResolveBlends)command=stage.terrainEdit.makeUniformTilePaint(tileTarget?brush:vertices,layer,pure,catalog,m_tileStrokeDistributionSeed,m_landscapeTools->randomizeUniformRotation(),&report);
        else if(tileTarget || activating)command=stage.terrainEdit.makeVirtualTileMaterialPaint(brush,layer,material,catalog,m_tileStrokeDistributionSeed,&report,m_landscapeTools->randomizeUniformRotation());
        else command=stage.terrainEdit.makeVirtualMaterialPaint(qBound(0,qRound(brush.centerX),stage.terrainEdit.tileCountX()-1),
            qBound(0,qRound(brush.centerY),stage.terrainEdit.tileCountY()-1),vertices,layer,material,catalog,m_tileStrokeDistributionSeed,&report,m_landscapeTools->randomizeUniformRotation());
        if(!manual && m_landscapeTools->randomPaintVariants()) {
            QSet<int> animated;
            for(const auto& animation : doc->baseline.mapInfo().animatedTiles)
                for(int frame = 0; frame < animation.frameCount; ++frame)
                    animated.insert(int(animation.tileIndex) + frame);
            for(auto& delta : command.deltas) {
                if(layer == ETerrainTileLayer::Overlay
                   || animated.contains(SMprPackedTile::decode(delta.after).atlasSlot())) {
                    bool known=false;
                    const auto signature=catalog.gridSignatureForPacked(delta.after,&known);
                    if(known && signature.northWest==signature.northEast
                       && signature.northWest==signature.southEast
                       && signature.northWest==signature.southWest)
                        delta.after=catalog.lowestPaintRotationForSlot(signature,delta.after);
                }
            }
        }
        if(report.hasErrors() || (!command.isEmpty() && !stage.terrainEdit.apply(command,true,&report)))break;
        changedTiles+=command.deltas.size();
        changed=changed || !command.isEmpty();
        if(!mixedEdge && !manual && !classic && m_tileStrokeAutoResolveBlends && !command.isEmpty()) {
            CTerrainVirtualMaterialField field;
            if(!stage.terrainEdit.copyVirtualMaterialField(layer,&field,false))break;
            QSet<QPair<int,int>> cells;
            auto verticesChanged=command.semanticAudit.paintedVertices;
            for(const auto& v:command.virtualVertices)verticesChanged.append(QPoint(v.globalX,v.globalY));
            for(const auto& v:verticesChanged)for(int y:{v.y()-1,v.y()})for(int x:{v.x()-1,v.x()})
                if(x>=0 && y>=0 && x<field.tileWidth() && y<field.tileHeight())cells.insert(qMakePair(x,y));
            for(const auto& cell:cells) {
                bool found=false;const auto signature=field.cellSignature(cell.first,cell.second,&found);
                if(found && !realizeWorkingTerrainSignature(signature,cell.first,cell.second,&usage,&report,nullptr,material,-1,&stage))break;
            }
            if(report.hasErrors())break;
            const auto resolution=stage.terrainEdit.makeVirtualStrokeResolution(command,stage.atlas->catalog(),m_tileStrokeDistributionSeed,&report,m_landscapeTools->randomizeUniformRotation(),material);
            if(!resolution.isEmpty() && !stage.terrainEdit.apply(resolution,true,&report))break;
        }
        if(report.hasErrors())break;
        if(!mixedEdge && !manual && layer == ETerrainTileLayer::Land && m_landscapeTools->randomPaintVariants()) {
            QVector<QPoint> randomCells = command.semanticAudit.affectedCells;
            for(const auto& delta : command.deltas) randomCells.append(QPoint(delta.globalX, delta.globalY));
            QSet<int> excluded;
            for(const auto& animation : doc->baseline.mapInfo().animatedTiles)
                for(int frame = 0; frame < animation.frameCount; ++frame)
                    excluded.insert(int(animation.tileIndex) + frame);
            for(int slot : stage.atlas->mixedEdgeRecipes()) excluded.insert(slot);
            if(!randomCells.isEmpty()) {
                const auto random = stage.terrainEdit.makeRandomTilePaint(stage.atlas->catalog(),
                    stage.terrainEdit.hasRandomPaintSeed() ? stage.terrainEdit.randomPaintSeed() : m_terrainSemanticSeed,
                    QRect(0, 0, stage.terrainEdit.tileCountX(), stage.terrainEdit.tileCountY()), excluded, randomCells, false);
                if(!stage.terrainEdit.apply(random, true, &report)) break;
                changed |= !random.isEmpty();
            }
        }
        ++planned;
        if(!changed && !mixedEdge)continue;
        if(mixedEdge) {
            QSet<int> cells;
            const int w=stage.terrainEdit.tileCountX(),h=stage.terrainEdit.tileCountY();
            QVector<QPoint> verticesChanged=command.semanticAudit.paintedVertices;
            for(const auto& vertex:command.virtualVertices)
                verticesChanged.append(QPoint(vertex.globalX,vertex.globalY));
            for(const auto& vertex:verticesChanged)
                for(int y:{vertex.y()-1,vertex.y()})for(int x:{vertex.x()-1,vertex.x()})
                    if(x>=0&&x<w&&y>=0&&y<h)cells.insert(y*w+x);
            for(const auto& cell:command.semanticAudit.paintedCoreCells)
                if(cell.x()>=0&&cell.x()<w&&cell.y()>=0&&cell.y()<h)
                    cells.insert(cell.y()*w+cell.x());
            for(const auto& delta:command.deltas)
                if(delta.layer==ETerrainTileLayer::Land&&delta.globalX>=0&&delta.globalX<w
                   &&delta.globalY>=0&&delta.globalY<h)
                    cells.insert(delta.globalY*w+delta.globalX);
            mixedPaintCells[target.key()]=cells;
            mixedStages[target.key()]=stage;
        }
        change.after.terrain=stage.terrainEdit;change.after.atlas=stage.atlas;
        stamp.paint.insert(target.key(),change);
    }
    if(mixedEdge && !report.hasErrors() && !mixedStages.isEmpty()) {
        QVector<SWorldMaterialRepairMap> maps;
        for(auto it=mixedStages.cbegin();it!=mixedStages.cend();++it) {
            SWorldMaterialRepairMap map;map.id=it.key();map.origin=origins.value(it.key());
            map.geometry=&it->terrainEdit;map.catalog=&it->atlas->catalog();
            map.layer=ETerrainTileLayer::Land;
            if(!it->terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&map.field,false)) {
                report.add("terrain.world.mixed_field",EValidationSeverity::Error,
                    tr("Не удалось прочитать общий материал перехода карты %1.").arg(QFileInfo(it.key()).fileName()));break;
            }
            maps.append(map);
        }
        if(!report.hasErrors()) {
            const auto plan=planWorldMaterialRepair(maps,true);
            const auto seamSides=worldMaterialSeamSides(maps);
            QMap<QString,QSet<int>> seamCellsToUpdate;
            for(const auto& map:maps) {
                QSet<int> affected=mixedPaintCells.value(map.id);
                affected.unite(plan.changedCells.value(map.id));
                for(auto edge=seamSides.value(map.id).cbegin();edge!=seamSides.value(map.id).cend();++edge)
                    if(affected.contains(edge.key()))seamCellsToUpdate[map.id].insert(edge.key());
            }
            QSet<qint32> requiredMaterials;
            QSet<QPair<qint32,qint32>> requiredPairs;
            QSize sharedSourceSize;
            for(const auto& map:maps) {
                const auto& stage=mixedStages[map.id];
                const auto images=stage.atlas->activeTileImages();
                if(images.isEmpty()) {
                    report.add("terrain.world.mixed_atlas",EValidationSeverity::Error,
                        tr("РђС‚Р»Р°СЃ РєР°СЂС‚С‹ %1 РїСѓСЃС‚.").arg(QFileInfo(map.id).fileName()));break;
                }
                if(!collectWorldRasterSize(*stage.atlas,&sharedSourceSize,&report,map.id))break;
                CTerrainVirtualMaterialField field;
                if(!stage.terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false))continue;
                QSet<int> cells=mixedPaintCells.value(map.id);
                cells.unite(plan.changedCells.value(map.id));
                cells.unite(seamCellsToUpdate.value(map.id));
                for(int cell:cells) {
                    bool found=false;
                    const auto signature=field.cellSignature(cell%field.tileWidth(),cell/field.tileWidth(),&found);
                    if(!found)continue;
                    requiredMaterials<<signature.northWest<<signature.northEast
                        <<signature.southEast<<signature.southWest;
                    QSet<qint32> ids{signature.northWest,signature.northEast,
                        signature.southEast,signature.southWest};
                    auto ordered=ids.values();std::sort(ordered.begin(),ordered.end());
                    for(int a=0;a<ordered.size();++a)for(int b=a+1;b<ordered.size();++b)
                        requiredPairs.insert(qMakePair(ordered[a],ordered[b]));
                }
            }
            QMap<qint32,QImage> sharedMaterialSources;
            if(!report.hasErrors()&&!requiredMaterials.isEmpty()
               &&!loadWorldTerrainMaterialSources(requiredMaterials,sharedSourceSize,
                    &sharedMaterialSources,&report)) {}
            QMap<qint32,qint32> sharedMaterialTypes;
            for(const auto& family:m_terrainGlobalFamilies)
                if(family.layer==ETerrainAtlasLayer::Land
                   &&family.runtimeTerrainType>=0&&family.runtimeTerrainType<16)
                    sharedMaterialTypes[family.id]=family.runtimeTerrainType;
            for(const auto& family:m_terrainGlobalFamilies)
                if(!sharedMaterialTypes.contains(family.id)
                   &&family.runtimeTerrainType>=0&&family.runtimeTerrainType<16)
                    sharedMaterialTypes[family.id]=family.runtimeTerrainType;
            if(!inferWorldMaterialRuntimeTypes(mixedStages,requiredMaterials,&sharedMaterialTypes,&report)){}
            if(!report.hasErrors())for(auto it=mixedStages.begin();it!=mixedStages.end();++it) {
                const auto native=terrainAtlasMaximumNativeTileSize(it->atlas->authoredTileImages());
                const auto rasterBank=worldRasterSources(sharedMaterialSources,native);
                for(auto source=rasterBank.cbegin();source!=rasterBank.cend();++source)
                    if(!it->atlas->hasMaterialSource(source.key())
                       &&!it->atlas->addExternalMaterialSource(source.key(),source.value())) {
                        report.add("terrain.world.material_source",EValidationSeverity::Error,
                            tr("РћР±С‰РёР№ РёСЃС‚РѕС‡РЅРёРє РјР°С‚РµСЂРёР°Р»Р° %1 РЅРµ РїРѕРґС…РѕРґРёС‚ Р°С‚Р»Р°СЃСѓ РєР°СЂС‚С‹ %2.")
                                .arg(source.key()).arg(QFileInfo(it.key()).fileName()));break;
                    }
            }
            if(!report.hasErrors())for(auto target=mixedStages.begin();target!=mixedStages.end();++target) {
                const auto native=terrainAtlasMaximumNativeTileSize(target->atlas->authoredTileImages());
                const auto rasterBank=worldRasterSources(sharedMaterialSources,native);
                for(auto source=rasterBank.cbegin();source!=rasterBank.cend();++source) {
                    const int runtime=sharedMaterialTypes.value(source.key(),-1);
                    if(runtime<0||runtime>15)continue;
                    STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=source.key();
                    if(target->atlas->importPhysicalTile(source.value(),runtime,&pure,
                        QStringLiteral("world-bank:uniform:%1").arg(source.key()),0,&report,true)<0)break;
                }
                for(auto source=mixedStages.cbegin();source!=mixedStages.cend()&&!report.hasErrors();++source) {
                    const auto images=source->atlas->authoredTileImages();
                    for(int slot=0;slot<source->atlas->authoredSlotCount();++slot) {
                        SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);
                        bool known=false;const auto sig=source->atlas->catalog().gridSignatureForPacked(packed.encode(),&known);
                        if(!known||slot>=images.size()||images[slot].isNull())continue;
                        QSet<qint32> ids{sig.northWest,sig.northEast,sig.southEast,sig.southWest};
                        if(ids.size()!=2)continue;
                        auto ordered=ids.values();std::sort(ordered.begin(),ordered.end());
                        if(!requiredPairs.contains(qMakePair(ordered[0],ordered[1])))continue;
                        const int runtime=source->atlas->runtimeTerrainTypeForSlot(slot);
                        if(runtime<0||runtime>15)continue;
                        if(images[slot].width()<native.width())continue;
                        const auto raster=terrainAtlasRasterTile(images[slot],native);
                        if(target->atlas->importPhysicalTile(raster,runtime,&sig,
                            QStringLiteral("world-bank:edge:%1:%2").arg(QFileInfo(source.key()).fileName()).arg(slot),
                            slot,&report,true)<0)break;
                    }
                }
                if(report.hasErrors())break;
            }
            if(!report.hasErrors()&&!importWorldMixedEdgeTransitionBank(&mixedStages,
                requiredPairs,sharedMaterialTypes,sharedSourceSize,&report)) {}
            for(auto it=mixedStages.begin();it!=mixedStages.end();++it) {
                if(report.hasErrors())break;
                const int w=it->terrainEdit.tileCountX(),h=it->terrainEdit.tileCountY();
                const auto& field=plan.fields[it.key()];
                QVector<qint32> current,baseline;
                it->terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&current,&baseline);
                current.resize((w+1)*(h+1));
                for(int y=0;y<=h;++y)for(int x=0;x<=w;++x)
                    current[y*(w+1)+x]=field.material(x,y);
                if(!it->terrainEdit.restoreVirtualMaterialFieldState(ETerrainTileLayer::Land,current,baseline,&report))break;
                QSet<int> cells=mixedPaintCells.value(it.key());
                cells.unite(plan.changedCells.value(it.key()));
                cells.unite(seamCellsToUpdate.value(it.key()));
                for(const QPoint& conflict:plan.conflicts.value(it.key()))
                    report.add("terrain.world.material_conflict",EValidationSeverity::Warning,
                        tr("%1: материал на геометрически неоднозначной вершине оставлен без изменения.").arg(QFileInfo(it.key()).fileName()),
                        QStringLiteral("%1,%2").arg(conflict.x()).arg(conflict.y()));
                const auto doc=m_worldTerrainSession.documents.value(it.key());
                QVector<SMprAnimatedTileRecord> animations=doc->baseline.mapInfo().animatedTiles;
                const QPoint cellOrigin(qRound(origins.value(it.key()).x()/2),qRound(origins.value(it.key()).y()/2));
                auto resolved=CTerrainMixedEdge::resolve(it->terrainEdit,it->atlas.data(),cells,
                    sharedMaterialTypes,animations,cellOrigin,material,m_tileStrokeDistributionSeed,
                    m_landscapeTools->randomizeUniformRotation(),&report,kMprPackedTerrainAtlasSlotLimit,true);
                if(report.hasErrors())break;
                if(!it->terrainEdit.installWorkingAtlasSlotTypes(it->atlas->activeTileTypes())
                   ||(!resolved.isEmpty()&&!it->terrainEdit.apply(resolved,true,&report)))break;
                auto change=stamp.paint.value(it.key());
                change.after.terrain=it->terrainEdit;change.after.atlas=it->atlas;
                stamp.paint[it.key()]=change;
            }
            // Each map retains its own atlas namespace. Decode the source bank
            // once, then derive every seam cell from the same world-coordinate
            // cache so opposite sides receive identical edge pixels.
            if(!report.hasErrors()) {
                QSize size;QSet<qint32> required;
                for(const auto& map:maps) {
                    auto& stage=mixedStages[map.id];
                    const auto images=stage.atlas->activeTileImages();
                    if(images.isEmpty()) {report.add("terrain.world.mixed_atlas",EValidationSeverity::Error,
                        tr("Атлас карты %1 пуст.").arg(QFileInfo(map.id).fileName()));break;}
                    if(!collectWorldRasterSize(*stage.atlas,&size,&report,map.id))break;
                    CTerrainVirtualMaterialField field;
                    if(!stage.terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false))continue;
                    for(int seamCell:seamCellsToUpdate.value(map.id)) {
                        const int x=seamCell%field.tileWidth(),y=seamCell/field.tileWidth();
                        bool found=false;const auto signature=field.cellSignature(x,y,&found);
                        if(found)required<<signature.northWest<<signature.northEast<<signature.southEast<<signature.southWest;
                    }
                }
                QMap<qint32,QImage> sources;
                if(!report.hasErrors()&&!required.isEmpty()
                   &&!loadWorldTerrainMaterialSources(required,size,&sources,&report)) {}
                if(!report.hasErrors()&&!required.isEmpty()) {
                    QMap<qint32,ETerrainBlendStyle> styles;
                    for(const auto& family:m_terrainGlobalFamilies)
                        styles[family.id]=CTerrainVirtualAtlas::blendStyleForMaterialName(family.name);
                    CWorldRasterAtlasBank sharedBank(sources,styles,true);
                    QMap<QByteArray,QImage> canonicalCache;
                    for(const auto& map:maps) {
                        auto& stage=mixedStages[map.id];CTerrainVirtualMaterialField field;
                        if(!stage.terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false))continue;
                        for(int seamCell:seamCellsToUpdate.value(map.id)) {
                            const int x=seamCell%field.tileWidth(),y=seamCell/field.tileWidth();
                            bool available=false;const quint16 before=stage.terrainEdit.packedTile(x,y,ETerrainTileLayer::Land,&available);
                            if(!available)continue;
                            bool found=false;const auto signature=field.cellSignature(x,y,&found);if(!found)continue;
                            const auto side=seamSides.value(map.id).value(seamCell,0);
                            const auto bodyTile=SMprPackedTile::decode(before);
                            const auto bodyImages=stage.atlas->activeTileImages();
                            const bool mixedSignature=!(signature.northWest==signature.northEast&&signature.northWest==signature.southEast&&signature.northWest==signature.southWest);
                            if(mixedSignature&&(bodyTile.atlasSlot()>=bodyImages.size()||bodyImages[bodyTile.atlasSlot()].isNull())) {
                                report.add("terrain.world.boundary_body",EValidationSeverity::Error,
                                    tr("Не удалось прочитать готовый рисунок перехода; общий мазок отменён."));break;
                            }
                            const auto native=mixedSignature?bodyImages[bodyTile.atlasSlot()].size()
                                :terrainAtlasMaximumNativeTileSize(stage.atlas->authoredTileImages());
                            const auto shared=sharedBank.forSize(native);
                            const auto pixels=shared->worldBoundaryTile(signature,stage.terrainEdit,origins.value(map.id),x,y,side,&canonicalCache,&report,
                                mixedSignature?bodyImages[bodyTile.atlasSlot()]:QImage(),mixedSignature?bodyTile.rotation:0);
                            const int runtime=stage.atlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(before).atlasSlot());
                            const int slot=pixels.isNull()?-1:stage.atlas->importWorldBoundaryTile(pixels,runtime,signature,&report);
                            if(slot<0)break;
                            SMprPackedTile packed;packed.atlasIndex=quint8(slot/64);packed.tileIndex=quint8(slot%64);
                            if(packed.encode()==before)continue;
                            STerrainTileCommand edge;edge.description=tr("Согласовать пиксели стыка mixed-edge");
                            STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=ETerrainTileLayer::Land;
                            delta.before=before;delta.after=packed.encode();edge.deltas.append(delta);
                            if(!stage.terrainEdit.installWorkingAtlasSlotTypes(stage.atlas->activeTileTypes())
                               ||!stage.terrainEdit.apply(edge,true,&report))break;
                            auto change=stamp.paint.value(map.id);change.after.terrain=stage.terrainEdit;
                            change.after.atlas=stage.atlas;stamp.paint[map.id]=change;
                        }
                        if(report.hasErrors())break;
                    }
                }
            }
        }
    }
    if(planned!=origins.size() && !report.hasErrors())report.add("terrain.world.stage",EValidationSeverity::Error,"Не удалось подготовить все карты; мазок отменён.");
    if(report.hasErrors()){++m_tileStrokeRefused;emit updateMsg(report.toText());return true;}
    ++m_tileStrokeActions;
    m_tileStrokeLayer=layer;
    m_tileStrokeMaterialId=manual?-1:material;
    if(stamp.isEmpty()){++m_tileStrokeNoOps;return true;}
    if(!m_worldTerrainSession.apply(stamp,true,&error)){emit updateMsg(error);return true;}
    bool previewed=true;
    for(const auto& id:stamp.paint.keys())previewed=previewWorldPaintState(id,true) && previewed;
    if(!previewed) {
        m_worldTerrainSession.apply(stamp,false,&error);
        for(const auto& id:stamp.paint.keys())previewWorldPaintState(id);
        emit updateMsg(tr("Предпросмотр общей кисти не обновлён; мазок отменён."));return true;
    }
    ++m_tileStrokeApplied;
    m_tileStrokeChangedTiles+=changedTiles;
    m_worldTerrainStroke.append(stamp);refreshWorldTerrainNeighbors();updateWorldTerrainStatus();return true;
}

namespace {
QVector<SMaterial> worldRenderMaterials(const QVector<SMprMaterialRecord>& records)
{
    QVector<SMaterial> result;
    result.reserve(records.size());
    for(const SMprMaterialRecord& record : records)
    {
        SMaterial material{};
        material.type = ETerrainType(record.type);
        material.R = record.red;
        material.G = record.green;
        material.B = record.blue;
        material.A = record.opacity;
        material.selfIllumination = record.selfIllumination;
        material.waveMultiplier = record.waveMultiplier;
        material.warpSpeed = record.warpSpeed;
        material.reserved1 = record.reserved1;
        material.reserved2 = record.reserved2;
        material.reserved3 = record.reserved3;
        result.append(material);
    }
    return result;
}
}

bool CView::previewWorldPaintState(const QString& id, bool incremental)
{
    const auto doc=m_worldTerrainSession.documents.value(id);if(!doc || !doc->atlas)return false;
    CTerrainPreviewContext guard(this);
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    CLandscape* landscape=nullptr;
    if(id==active)landscape=m_pLand;
    else for(auto& neighbor:m_neighborLandscapes)
        if(CWorldTerrainSession::key(neighbor.placement.mprPath)==id){landscape=neighbor.landscape.data();break;}
    if(!landscape)return true; // unloaded neighbors still own the complete document
    const auto images=doc->atlas->activeTileImages();
    const bool activeMap=id==active;
    const auto previousImages=incremental && activeMap && m_workingTerrainAtlas
        ? m_workingTerrainAtlas->activeTileImages() : QVector<QImage>();
    QSet<int> changedImages;
    for(int index=0;index<images.size();++index)
        if(index>=previousImages.size() || images[index].cacheKey()!=previousImages[index].cacheKey())
            changedImages.insert(index);
    const bool atlasChanged=images.size()!=previousImages.size() || !changedImages.isEmpty();
    const bool typesChanged=!activeMap || !m_workingTerrainAtlas
        || m_workingTerrainAtlas->activeTileTypes()!=doc->atlas->activeTileTypes();
    if(atlasChanged && !landscape->previewTerrainAtlas(images,doc->atlas->activeTextureCount(),changedImages))return false;
    const bool materialsChanged=!incremental || !activeMap || m_mprMaterials!=doc->materials;
    if(materialsChanged)landscape->setMaterials(worldRenderMaterials(doc->materials));
    for(const auto& sector:doc->baseline.sectors()) {
        const auto payload=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
        if(incremental && activeMap && payload==m_terrainEdit.sectorPayload(sector.sectorX,sector.sectorY))continue;
        if(!landscape->previewCanonicalSector(sector.sectorX,sector.sectorY,payload))return false;
    }
    if(id==active) {
        int selectedSlot=-1,selectedRotation=0;
        m_pTileForm->getSelectedTile(selectedSlot,selectedRotation);
        // Restoring the inspector selection is not a user material change.
        // Its onSelect signal would cancel the currently executing stroke.
        const QSignalBlocker pickerSignals(m_pTileForm);
        if((atlasChanged || typesChanged) && !m_pTileForm->adoptTileImages(images,doc->atlas->activeTileTypes()))return false;
        // Atlas uploads rebuild the legacy picker. Preserve its cursor so a
        // held-C drag keeps painting after the first world-brush stamp.
        if((atlasChanged || typesChanged) && selectedSlot>=0 && m_pTileForm->selectTile(selectedSlot))
            m_pTileForm->setTileRotation(selectedRotation);
        m_terrainEdit=doc->terrain;m_mprMaterials=doc->materials;
        // Restore the immutable source together with its working cache. The
        // renderer/picker contain edited slots and cannot serve as that source.
        m_authoredTerrainTileImages=doc->atlas->authoredTileImages();
        const auto source=m_worldTerrainAtlasSources.constFind(id);
        if(source!=m_worldTerrainAtlasSources.cend()) {
            m_terrainAtlasProfile=source->profile;
            m_activeTerrainArchivePath=source->archive;
        }
        m_workingTerrainAtlas=doc->atlas->createStagingCopy();
        m_pLand->setCanonicalTerrainModel(&m_terrainEdit);syncMprDirtyState();
        if(materialsChanged)m_landscapeTools->setOverlayMaterials(worldRenderMaterials(m_mprMaterials));
        m_landscapeTools->setGeneratedAtlasTileCount(m_workingTerrainAtlas->generatedTileCount());
        if(atlasChanged || typesChanged)refreshWorkingTerrainPalettes();
    } else for(auto& neighbor:m_neighborLandscapes)
        if(CWorldTerrainSession::key(neighbor.placement.mprPath)==id) {
            neighbor.terrainPreviewAtlas=doc->atlas;neighbor.drawCacheDirty=true;
            for(const auto& sector:doc->baseline.sectors())neighbor.terrainPreviewPayloads[qMakePair(sector.sectorX,sector.sectorY)]=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
        }
    update();return true;
}

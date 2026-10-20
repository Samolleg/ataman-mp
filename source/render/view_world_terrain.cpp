#include "terrain_atlas_compact.h"
#include "window_geometry.h"
#include "log.h"
#include "view.h"
#include "node.h"
#include "resourcemanager.h"
#include "resource_name.h"
#include "mmp_codec.h"
#include "tile_form.h"
#include "terrain_preview_context.h"
#include "terrain_shadow_inputs.h"
#include "render_stall_trace.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "staged_save_validator.h"
#include "res_archive_document.h"
#include "terrain_save_flow_policy.h"
#include "terrain_semantic_field_store.h"
#include <QtWidgets>
#include <QCryptographicHash>
#include <cmath>

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
    const bool pending=!names.isEmpty();
    if(pending!=m_worldTerrainSavePendingNotified) {
        m_worldTerrainSavePendingNotified=pending;
        emit mprSaveStateChanged();
    }
}

void CView::syncWorldTerrainActive()
{
    if(!m_pLand || !m_pLand->isMprLoad() || !m_terrainEditAvailable) return;
    const QString id=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    if(!m_worldTerrainSession.documents.contains(id)) return;
    auto doc=m_worldTerrainSession.documents[id];
    doc->baseline=m_mprDocument;
    doc->terrain=m_terrainEdit;
    doc->materials=m_mprMaterials;
    if(doc->atlas)doc->atlas=m_workingTerrainAtlas;
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
        loaded.landscape->setCanonicalTerrainModel(&doc->terrain);
        if(doc->atlas && loaded.terrainPreviewAtlas!=doc->atlas) {
            if(!previewWorldPaintState(id)){emit updateMsg(tr("Не удалось обновить атлас соседней карты."));continue;}
        }
        bool changed=false, shadowChanged=false;
        for(const auto& sector:doc->baseline.sectors())
        {
            const auto key=qMakePair(sector.sectorX,sector.sectorY);
            const auto before=loaded.terrainPreviewPayloads.value(key,
                doc->baseline.memberPayload(sector.memberName));
            const auto after=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
            if(before==after) continue;
            if(loaded.landscape->previewCanonicalSector(sector.sectorX,sector.sectorY,after))
            { loaded.terrainPreviewPayloads[key]=after;changed=true;
              shadowChanged |= terrainShadowInputsChanged(before,after); }
            else emit updateMsg(tr("Neighbor terrain preview failed: %1").arg(loaded.placement.zoneId));
        }
        if(changed)
        {
            if(shadowChanged)loaded.landscape->invalidateTerrainShadowField();
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
    brush.centerY=position.y()+m_activeMapWorldOrigin.y();brush.radius=m_landscapeTools->radius();brush.falloff=m_landscapeTools->falloff();
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
    for(auto it=stroke.paint.cbegin();it!=stroke.paint.cend();++it)
        previewWorldPaintState(it.key(), forward ? &it->before.terrain : &it->after.terrain);
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
        tr("Сохранить перечисленные карты и их атласы с резервными копиями?\n\n%1\n\nПосле перепаковки атласа история Undo/Redo начнётся заново.").arg(paths.join('\n')),
        QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Save)return false;
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    QVector<SMapSaveFile> files;
    QMap<QString,QByteArray> archives;
    QMap<QString,QString> archivePaths;
    QMap<QString,QMap<QString,QByteArray>> published;
    QMap<QString,STerrainAtlasProfile> profiles;
    QMap<QString,QVector<QImage>> physical;
    QMap<QString,CMprDocument> reopened;
    CValidationReport report;QString error;bool diskCommitted=false;
    auto refuse=[&](const QString& detail) {
        CValidationReport failures;
        for(const auto& finding:report.findings())
            if(finding.severity!=EValidationSeverity::Info)
                failures.add(finding.code,finding.severity,finding.message,finding.location);
        const QString diagnostics=failures.toText().trimmed();
        const QString message=detail==report.toText() ? diagnostics : detail;
        QMessageBox::warning(this,diskCommitted?tr("Файлы сохранены; предпросмотр не обновлён"):tr("Карты не сохранены"),
            message+(diagnostics.isEmpty()||message.contains(diagnostics)?QString():"\n"+diagnostics));return false;
    };
    for(const auto& path:paths) {
        const QString id=CWorldTerrainSession::key(path);const auto doc=m_worldTerrainSession.documents[id];
        CMprDocument staged=doc->baseline;
        if(!ensureWorldTerrainAtlas(id,&report))return refuse(report.toText());
        const bool publishAtlas = bool(doc->atlas);
        QVector<SResArchiveNewMember> mapMembers;
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
            auto pages=source.pages;auto images=doc->atlas->activeTileImages();auto types=doc->atlas->activeTileTypes();
            while(types.size()<staged.mapInfo().tileTypes.size())types.append(staged.mapInfo().tileTypes[types.size()]);
            for(int page=0;page<names.size();++page) {
                if(archive.memberPayload(names[page])!=source.pages[page])return refuse(tr("Текстуры изменены другой программой: %1").arg(names[page]));
                QSet<int> changed;
                for(int slot=0;slot<64;++slot) {
                    QImage old;
                    if(!ei::decodeMmpDxt1AtlasTile(pages[page],slot,&old,&error))return refuse(error);
                    if(page*64+slot>=images.size())images.append(old); // Unused saved tail remains available after Undo.
                    if(old.convertToFormat(QImage::Format_RGBA8888)!=images[page*64+slot].convertToFormat(QImage::Format_RGBA8888))changed.insert(slot);
                }
                if(!changed.isEmpty()) {
                    const auto encoded=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(images,page*64,images.at(page*64).width()),true,&error);
                    if(encoded.isEmpty())return refuse(error);
                    pages[page]=ei::patchMmpDxt1AtlasTiles(pages[page],encoded,changed,&error);
                    if(pages[page].isEmpty())return refuse(tr("Не удалось сохранить страницу атласа %1: %2").arg(names[page],error));
                }
            }
            const auto animations=id==active?m_mprAnimatedTiles:doc->baseline.mapInfo().animatedTiles;
            const auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(staged,images,types,pages,doc->materials,animations,&report,doc->atlas->preservedDxtTiles());
            if(!package.success)return refuse(package.error);
            if(!staged.loadBytes(package.mprBytes,path,&report))return refuse(report.toText());
            CResArchiveDocument encoded;
            if(!encoded.loadBytes(package.resourceArchiveBytes,"world-atlas.res",&errors))return refuse(errors.join('\n'));
            for(const auto& name:package.resourceMembers) {
                const auto bytes=encoded.memberPayload(name);
                if(published[archiveId].contains(name) && published[archiveId][name]!=bytes)
                    return refuse(tr("Карты используют одну текстуру %1 с разными изменениями. Сохранение отменено.").arg(name));
                SResArchiveNewMember entry;entry.name=name;entry.payload=bytes;mapMembers<<entry;
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
            auto profile=publishAtlas ? profiles[id] : source.profile;
            terrainAtlasCopySourceIdentities(&profile,*doc->atlas);profiles[id]=profile;
            SResArchiveNewMember semantic;
            if(!buildTerrainSemanticFieldMember(candidate,QFileInfo(path),
                    profile.mprNamespace,profile.atlasPixelSha256,profile.toBytes(),
                    &semantic,&report,&doc->terrain,&candidate)) return refuse(report.toText());
            if(!reopened[id].loadBytes(candidate,path,&report))return refuse(report.toText());
            // Validate the exact embedded field before publishing any file.
            // In particular, an empty current field must suppress stale RES metadata.
            STerrainSemanticFieldDocument semantics;
            const QByteArray embedded=reopened[id].memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
            if(!STerrainSemanticFieldDocument::fromBytes(embedded,&semantics,&report,path)
               || !semantics.validateBinding(reopened[id].mapInfo().innerNamespace,QByteArray(),
                    QByteArray::fromHex(profile.atlasPixelSha256),
                    QCryptographicHash::hash(profile.toBytes(),QCryptographicHash::Sha256),
                    doc->terrain.tileCountX()+1,doc->terrain.tileCountY()+1,&report,
                    STerrainSemanticFieldDocument::tileBindingSha256(reopened[id])))return refuse(report.toText());
            if(publishAtlas) {
                const QString archiveId=CWorldTerrainSession::key(source.archive);
                const QString name=CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace);
                if(published[archiveId].contains(name) && published[archiveId][name]!=profile.toBytes())
                    return refuse(tr("Несовместимые профили общего атласа."));
                SResArchiveNewMember entry;entry.name=name;entry.payload=profile.toBytes();mapMembers<<entry;
                if(!semantic.name.isEmpty()) {
                    if(published[archiveId].contains(semantic.name) && published[archiveId][semantic.name]!=semantic.payload)
                        return refuse(tr("Несовместимые поля материалов одного пространства имён."));
                    mapMembers<<semantic;
                }
            }
        }
        if(publishAtlas){
            if(!compactTerrainPublication(&candidate,&profiles[id],&mapMembers,&physical[id],QFileInfo(path),&error))return refuse(error);
            if(profiles[id].textureCount>=100&&QMessageBox::warning(this,tr("Большой атлас"),
                tr("%1: %2 страниц, предел126. Сохранить?").arg(path).arg(profiles[id].textureCount),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return false;
            const auto archiveId=CWorldTerrainSession::key(m_worldTerrainAtlasSources[id].archive);
            for(const auto& member:mapMembers)published[archiveId][member.name]=member.payload;
            if(!reopened[id].loadBytes(candidate,path,&report))return refuse(report.toText());
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
    // Stage detached document ownership before publication. Compacted slot
    // numbers start a new history; semantic vertex fields remain authoritative.
    auto rebased=m_worldTerrainSession;rebased.clearHistory();
    for(const auto& path:paths){const auto id=CWorldTerrainSession::key(path);
        auto doc=QSharedPointer<SWorldTerrainDocument>(new SWorldTerrainDocument(*rebased.documents[id]));
        doc->baseline=reopened[id];doc->materials=reopened[id].mapInfo().materials;
        if(!doc->terrain.load(reopened[id],&report))return refuse(report.toText());
        for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}){
            QVector<qint32> current,baseline;
            if(m_worldTerrainSession.documents[id]->terrain.copyVirtualMaterialFieldState(layer,&current,&baseline)
               &&!doc->terrain.restoreVirtualMaterialFieldState(layer,current,current,&report))return refuse(report.toText());}
        const auto analysis=profiles[id].usableAnalysis(&report);
        auto catalog=CTerrainAtlasPresetCatalog::globalizedCatalog(profiles[id],analysis.overlayCatalogWithLandFamilies(),&m_terrainGlobalFamilies);
        QMap<qint32,ETerrainBlendStyle> styles;QMap<qint32,qint32> runtimeTypes;
        for(const auto& family:m_terrainGlobalFamilies){styles[family.id]=CTerrainVirtualAtlas::blendStyleForMaterialName(family.name);runtimeTypes[family.id]=family.runtimeTerrainType;}
        doc->atlas.reset(new CTerrainWorkingAtlas(catalog,physical[id],m_terrainSemanticSeed,kMprPackedTerrainAtlasSlotLimit,
            m_terrainGlobalMaterialSources,styles,reopened[id].mapInfo().tileTypes,runtimeTypes));
        if(!doc->atlas->isValid())return refuse(doc->atlas->error());
        doc->terrain.enableWorkingAtlasSlots(kMprPackedTerrainAtlasSlotLimit);rebased.documents[id]=doc;
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
    diskCommitted=true;
    for(const auto& path:paths){const auto id=CWorldTerrainSession::key(path);const auto source=m_worldTerrainAtlasSources[id];
        ei::log(eLogInfo,retireTerrainAtlasAfterSave(source.archive,source.profile,path,
            terrainAtlasMapDirectories(path,CTextureList::getInstance()->configuredResourceArchivePaths())));}

    // Adopt the validated compact disk bindings and the new history.
    for(const auto& path:paths) {
        const QString id=CWorldTerrainSession::key(path);const auto doc=m_worldTerrainSession.documents[id];
        if(id==active) {
            const QString hash=QString::fromLatin1(QCryptographicHash::hash(reopened[id].originalBytes(),QCryptographicHash::Sha256).toHex());
            if(profiles.contains(id)) {
                if(!adoptCompleteTerrainBaseline(QFileInfo(path),hash,physical[id],m_worldTerrainAtlasSources[id].archive,profiles[id],false,false))
                    return refuse(tr("Файлы сохранены, но предпросмотр требует повторного открытия карты."));
            } else {
                SMprSaveCommitResult commit;commit.candidateSha256=hash;
                if(!adoptCommittedLandscapeBaseline(QFileInfo(path),commit))return refuse(tr("Файлы сохранены; откройте карту заново."));
            }
        }
    }
    m_worldTerrainSession=rebased;
    for(const auto& path:paths) {
        const auto id=CWorldTerrainSession::key(path);
        if(profiles.contains(id)) {
            auto& source=m_worldTerrainAtlasSources[id];source.profile=profiles[id];source.pages.clear();
            const auto names=ei::terrainTextureResourceNames(reopened[id].mapInfo().innerNamespace,int(reopened[id].mapInfo().textureCount));
            for(const auto& name:names)source.pages.append(published[CWorldTerrainSession::key(source.archive)].value(name));
        }
        if(m_worldTerrainSession.documents[id]->atlas) {
            if(!previewWorldPaintState(id))return refuse(tr("Файлы сохранены; откройте карту заново."));
        } else if(id==active)restoreWorldTerrainActive();
    }
    refreshWorldTerrainNeighbors();updateWorldTerrainStatus();
    emit updateMsg(tr("Сохранено карт: %1. Атласы проверены, резервные копии сохранены. История Undo/Redo начата заново.").arg(paths.size()));
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
        const auto* textures=CTextureList::getInstance();
        if(!textures->terrainAuthoringArchivePath().isEmpty()) {
            const auto reference=textures->terrainAuthoringReferenceSha256();
            catalog.setPaintReference(reference.isEmpty()?QByteArray(64,'0'):reference,!reference.isEmpty()
                && profile.paintReferenceSha256==reference);
        }

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

bool CView::resolveWorldMaterialBlends(QString* summary,CValidationReport* report,STerrainResolveAuditSession* audit)
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
    if(origins.size()<2)return false;
    bool completed=false;
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
    QMap<QString,QSet<int>> preserve[2];
    QSet<QString> semanticChanges;int conflicts=0,boundaryVertices=0;
    for(auto layer:layers) {
        const int li=layer==ETerrainTileLayer::Land?0:1;
        QVector<SWorldMaterialRepairMap> maps;
        for(auto it=stages.cbegin();it!=stages.cend();++it) {
            SWorldMaterialRepairMap m;m.id=it.key();m.origin=origins[it.key()];
            m.geometry=&it->terrainEdit;m.catalog=&it->atlas->catalog();m.layer=layer;
            if(!it->terrainEdit.copyVirtualMaterialField(layer,&m.field,false)) {
                report->add("terrain.world.field",EValidationSeverity::Error,tr("Не удалось прочитать материалы карты %1.").arg(QFileInfo(it.key()).fileName()));return true;
            }
            maps.append(m);
        }
        const auto plan=planWorldMaterialRepair(maps,true);boundaryVertices+=plan.changedVertices;
        for(auto changes=plan.changedCells.cbegin();changes!=plan.changedCells.cend();++changes)
            if(!changes->isEmpty())semanticChanges.insert(changes.key());
        for(auto it=stages.begin();it!=stages.end();++it) {
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
    int cells=0;
    for(auto it=stages.begin();it!=stages.end();++it) {
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
    // The normal131 resolver above keeps every already exact packed tile.
    // Do not replace that resolved artwork with a second, context-free
    // world-boundary generator or repaint a collar over its authored edge.
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
        if(!m_worldTerrainSession.apply(stamp,true,&error)) {report->add("terrain.world.apply",EValidationSeverity::Error,error);return true;}
        bool previewed=true;
        for(auto it=stamp.paint.cbegin();it!=stamp.paint.cend();++it)
            previewed=previewWorldPaintState(it.key(), &it->before.terrain) && previewed;
        if(!previewed) {
            m_worldTerrainSession.apply(stamp,false,&error);
            for(const auto& id:stamp.paint.keys())previewWorldPaintState(id);
            report->add("terrain.world.preview",EValidationSeverity::Error,tr("Предпросмотр не обновлён; исправление отменено."));return true;
        }
        stamp.localUndoIndex=m_pUndoStack?m_pUndoStack->index():-1;m_worldTerrainSession.record(stamp);
        refreshWorldTerrainNeighbors();updateWorldTerrainStatus();
    }
    if(audit) {
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
    CRenderStallTrace timing("world-paint");
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
    if(!ensureWorldTerrainAtlas(active,&report)){emit updateMsg(report.toText());return true;}
    timing.mark("catalog");
    const auto palette=m_landscapeTools->paintPaletteMode();
    const bool manual=palette==ETerrainPaintPaletteMode::Manual;
    const bool classic=palette==ETerrainPaintPaletteMode::ClassicAuto;
    const bool tileTarget=m_landscapeTools->paintTargetMode()==ETerrainPaintTargetMode::Tile;
    const bool activating=m_landscapeTools->overlaySurfaceEnabled()
        && m_landscapeTools->overlaySurfaceOperation()==EOverlaySurfaceOperation::Activate;
    const auto layer=land?ETerrainTileLayer::Land:ETerrainTileLayer::Overlay;
    bool selected=false;const auto material=m_landscapeTools->paintTerrainType(&selected);
    int selectedSlot=-1,rotation=0;
    if(manual)m_pTileForm->getSelectedTile(selectedSlot,rotation);
    if((manual && selectedSlot<0) || (!manual && !selected)){emit updateMsg(tr("Выберите материал или тайл."));return true;}
    const auto activeAtlas=m_worldTerrainSession.documents[active]->atlas;
    int liquid=-1;
    if(activating) {
        liquid=m_landscapeTools->overlayMaterialIndex(&selected);
        if(!selected || liquid<0 || liquid>=m_mprMaterials.size()){emit updateMsg(tr("Выберите материал покрытия."));return true;}
    }
    int planned=0;
    SWorldTerrainStroke stamp;
    QMap<QString,STerrainTileCommand> joinAreas;
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
        for(int y=0;y<stage.terrainEdit.tileCountY();++y)for(int x=0;x<stage.terrainEdit.tileCountX();++x)
            for(const auto l:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                bool available=false;const auto packed=stage.terrainEdit.packedTile(x,y,l,&available);
                if(available && !(l==ETerrainTileLayer::Overlay && packed==0xffff))++usage[SMprPackedTile::decode(packed).atlasSlot()];
            }
        for(const auto& animation:doc->baseline.mapInfo().animatedTiles)
            for(int frame=0;frame<qMax(1,int(animation.frameCount));++frame)stage.protectedSlots.insert(animation.tileIndex+frame);
        timing.mark("stage-usage");
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
            slot=stage.atlas->importPhysicalTile(images[selectedSlot],activeAtlas->runtimeTerrainTypeForSlot(selectedSlot),
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
        if(report.hasErrors() || (!command.isEmpty() && !stage.terrainEdit.apply(command,true,&report)))break;
        changed=changed || !command.isEmpty();
        if(!manual && !classic && m_tileStrokeAutoResolveBlends && !command.isEmpty()) {
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
            const auto resolution=stage.atlas->joinAuthoredTransitionEdges(stage.terrainEdit,
                stage.terrainEdit.makeVirtualStrokeResolution(command,stage.atlas->catalog(),m_tileStrokeDistributionSeed,&report,m_landscapeTools->randomizeUniformRotation(),material),&report);
            if(!resolution.isEmpty() && !stage.terrainEdit.apply(resolution,true,&report))break;
            joinAreas.insert(target.key(),resolution);
            changed=changed || !resolution.isEmpty();
        }
        if(report.hasErrors())break;
        // These are verified source pixels/bindings, not live atlas/history
        // state. Retain them even for a no-op, and share them with the next map
        // in this stamp instead of re-hashing the same source for every tuple.
        retainTerrainSourceCaches(stage);
        ++planned;
        if(!changed)continue;
        change.after.terrain=stage.terrainEdit;change.after.atlas=stage.atlas;
        stamp.paint.insert(target.key(),change);
        timing.mark("resolve");
    }
    if(planned!=origins.size() && !report.hasErrors())report.add("terrain.world.stage",EValidationSeverity::Error,"Не удалось подготовить все карты; мазок отменён.");
    // Both maps must be staged before comparing a shared border. Their packed
    // slot numbers are local identities, so pass the neighbor's actual image
    // and rotation, never interpret its slot through the active map's atlas.
    for(int pass=0;pass<4 && !report.hasErrors();++pass) {
        bool adjusted=false;
        for(auto it=stamp.paint.begin();it!=stamp.paint.end();++it) {
            if(!joinAreas.contains(it.key()))continue;
            auto& state=it.value().after;
            const auto origin=origins.value(it.key());
            QMap<QPair<int,int>,QPair<QImage,quint8>> boundary;
            for(const auto& cell:joinAreas[it.key()].semanticAudit.affectedCells)
                for(const auto& offset:{QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)}) {
                    const auto local=cell+offset;
                    if(local.x()>=0 && local.x()<state.terrain.tileCountX()
                       && local.y()>=0 && local.y()<state.terrain.tileCountY())continue;
                    const QPointF world=origin+QPointF(local.x()*2,local.y()*2);
                    for(auto other=origins.cbegin();other!=origins.cend();++other) {
                        if(other.key()==it.key())continue;
                        const auto doc=m_worldTerrainSession.documents.value(other.key());
                        if(!doc || !doc->atlas)continue;
                        const auto* neighborTerrain=&doc->terrain;
                        auto neighborAtlas=doc->atlas;
                        const auto staged=stamp.paint.constFind(other.key());
                        if(staged!=stamp.paint.constEnd()) {
                            neighborTerrain=&staged.value().after.terrain;
                            neighborAtlas=staged.value().after.atlas;
                        }
                        const auto p=(world-other.value())/2.0;
                        const int x=qRound(p.x()),y=qRound(p.y());
                        if(std::abs(p.x()-x)>0.001 || std::abs(p.y()-y)>0.001
                           || x<0 || y<0 || x>=neighborTerrain->tileCountX()
                           || y>=neighborTerrain->tileCountY())continue;
                        bool available=false;
                        const auto packed=SMprPackedTile::decode(neighborTerrain->packedTile(x,y,layer,&available));
                        if(!available || packed.atlasSlot()>=neighborAtlas->tileImages().size())continue;
                        boundary.insert(qMakePair(local.x(),local.y()),
                            qMakePair(neighborAtlas->tileImages().at(packed.atlasSlot()),packed.rotation));
                        break;
                    }
                }
            if(boundary.isEmpty())continue;
            auto area=joinAreas.value(it.key());area.deltas.clear();
            const auto joined=state.atlas->joinAuthoredTransitionEdges(state.terrain,area,&report,boundary);
            if(!joined.isEmpty()) {
                if(!state.terrain.apply(joined,true,&report))break;
                adjusted=true;
            }
        }
        if(!adjusted)break;
    }
    if(report.hasErrors()){emit updateMsg(report.toText());return true;}
    timing.mark("join");
    if(stamp.isEmpty())return true;
    if(!m_worldTerrainSession.apply(stamp,true,&error)){emit updateMsg(error);return true;}
    timing.mark("apply");
    bool previewed=true;
    for(auto it=stamp.paint.cbegin();it!=stamp.paint.cend();++it)
        previewed=previewWorldPaintState(it.key(), &it->before.terrain) && previewed;
    if(!previewed) {
        m_worldTerrainSession.apply(stamp,false,&error);
        for(const auto& id:stamp.paint.keys())previewWorldPaintState(id);
        emit updateMsg(tr("Предпросмотр общей кисти не обновлён; мазок отменён."));return true;
    }
    timing.mark("publish");
    m_worldTerrainStroke.append(stamp);refreshWorldTerrainNeighbors();updateWorldTerrainStatus();
    timing.mark("history-neighbors");
    return true;
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

bool CView::previewWorldPaintState(const QString& id,
                                   const CTerrainEditModel* previousTerrain)
{
    const auto doc=m_worldTerrainSession.documents.value(id);if(!doc || !doc->atlas)return false;
    CTerrainPreviewContext guard(this);
    const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
    CLandscape* landscape=nullptr;
    SLoadedNeighborLandscape* loadedNeighbor=nullptr;
    if(id==active)landscape=m_pLand;
    else for(auto& neighbor:m_neighborLandscapes)
        if(CWorldTerrainSession::key(neighbor.placement.mprPath)==id){landscape=neighbor.landscape.data();loadedNeighbor=&neighbor;break;}
    if(!landscape)return true; // unloaded neighbors still own the complete document
    const auto images=doc->atlas->activeTileImages();
    QElapsedTimer publicationTimer;publicationTimer.start();
    if(!landscape->previewTerrainAtlasIfChanged(images,doc->atlas->activeTextureCount()))return false;
    landscape->setMaterials(worldRenderMaterials(doc->materials));
    int updatedSectors=0;
    bool shadowChanged=false;
    for(const auto& sector:doc->baseline.sectors()) {
        const auto payload=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
        const auto key=qMakePair(int(sector.sectorX),int(sector.sectorY));
        // A background renderer may have been loaded after the command was
        // recorded. Its own published payloads, not history, are authoritative.
        // No predecessor means restore/rebind/rollback: force a full sector
        // publication even if a failed partial update left the cache unchanged.
        const QByteArray before=!previousTerrain ? QByteArray()
            : loadedNeighbor ? loadedNeighbor->terrainPreviewPayloads.value(key,
                doc->baseline.memberPayload(sector.memberName))
            : previousTerrain->sectorPayload(sector.sectorX,sector.sectorY);
        if(before==payload)continue;
        if(!landscape->previewCanonicalSector(sector.sectorX,sector.sectorY,payload))return false;
        shadowChanged |= terrainShadowInputsChanged(before,payload);
        ++updatedSectors;
        if(loadedNeighbor)loadedNeighbor->terrainPreviewPayloads[key]=payload;
    }
    if(id==active) {
        if(!m_pTileForm->adoptTileImages(images,doc->atlas->activeTileTypes()))return false;
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
        m_landscapeTools->setOverlayMaterials(worldRenderMaterials(m_mprMaterials));
        m_landscapeTools->setGeneratedAtlasTileCount(m_workingTerrainAtlas->generatedTileCount());
    } else for(auto& neighbor:m_neighborLandscapes)
        if(CWorldTerrainSession::key(neighbor.placement.mprPath)==id) {
            neighbor.terrainPreviewAtlas=doc->atlas;
            neighbor.drawCacheDirty |= shadowChanged;
        }
    if(shadowChanged)landscape->invalidateTerrainShadowField();
    ei::log(eLogInfo,QString("World paint publication map:%1 sectors:%2/%3 elapsedMs:%4")
        .arg(QFileInfo(id).fileName()).arg(updatedSectors).arg(doc->baseline.sectors().size())
        .arg(publicationTimer.elapsed()));
    update();return true;
}

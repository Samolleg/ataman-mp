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
        m_worldTerrainSession.canRedo(),!names.isEmpty());
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
    QDialog dialog(this); dialog.setWindowTitle(tr("World terrain maps"));
    QVBoxLayout* layout=new QVBoxLayout(&dialog);
    QCheckBox* enabled=new QCheckBox(tr("Edit neighboring-map terrain"),&dialog);
    enabled->setChecked(m_worldTerrainEnabled);layout->addWidget(enabled);
    QLabel* description=new QLabel(tr("The active map is editable. Check each neighbor you permit the brush to change. Unchecked maps stay protected. Touching shifted boundaries are reconciled along the entire connected seam reached by the brush, including outside its radius. This can straighten a long boundary. Saved heights use each MPR's precision."),&dialog);
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
        if(doc->atlas) {
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
                    const auto encoded=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(images,page*64,images.first().width()),true,&error);
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
           || (doc->atlas ? !staged.appendMaxZEdit(doc->terrain.maxZ(),&transaction,&report)
               : !staged.appendMapParametersEdit(doc->terrain.maxZ(),id==active?m_mprMaterials:doc->materials,
                 id==active?m_mprAnimatedTiles:staged.mapInfo().animatedTiles,&transaction,&report))
           || !staged.applyTransaction(transaction,&candidate,&report))return refuse(report.toText());
        CTerrainEditModel check;
        if(!reopened[id].loadBytes(candidate,path,&report) || !check.load(reopened[id],&report) || report.hasErrors())return refuse(report.toText());
        if(doc->atlas) {
            const QString archiveId=CWorldTerrainSession::key(m_worldTerrainAtlasSources[id].archive);
            const auto profile=profiles[id];
            SResArchiveNewMember semantic;
            if(!buildTerrainSemanticFieldMember(candidate,QFileInfo(path),profile.mprNamespace,profile.atlasPixelSha256,profile.toBytes(),&semantic,&report,&doc->terrain))return refuse(report.toText());
            const QString name=CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace);
            if(published[archiveId].contains(name) && published[archiveId][name]!=profile.toBytes())return refuse(tr("Несовместимые профили общего атласа."));
            published[archiveId][name]=profile.toBytes();
            if(!semantic.name.isEmpty()) {
                if(published[archiveId].contains(semantic.name) && published[archiveId][semantic.name]!=semantic.payload)return refuse(tr("Несовместимые поля материалов одного пространства имён."));
                published[archiveId][semantic.name]=semantic.payload;
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
    const auto saved=commitMapSaveBatch(files,m_mapCohortActive?m_mapCohortBackupRoot:QString());
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
            if(doc->atlas) {
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
            const bool ownsAtlas=retained && retained->atlas && m_worldTerrainAtlasSources.contains(CWorldTerrainSession::key(retained->path));
            if((mprParametersDirty() && (!ownsAtlas || m_mprAnimatedTiles!=m_mprDocument.mapInfo().animatedTiles))
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
        if(QFileInfo::exists(privateAtlas))archives.append(privateAtlas);
        if(QFileInfo::exists(adjacent))archives.append(adjacent);
        archives.append(CTextureList::getInstance()->configuredResourceArchivePaths());
        archives.removeDuplicates();
        const auto embedded=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(archives,ns,
            QFileInfo::exists(privateAtlas)?privateAtlas:QString());
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
    if(!ensureWorldTerrainAtlas(active,&report)){emit updateMsg(report.toText());return true;}
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
            const auto resolution=stage.terrainEdit.makeVirtualStrokeResolution(command,stage.atlas->catalog(),m_tileStrokeDistributionSeed,&report,m_landscapeTools->randomizeUniformRotation(),material);
            if(!resolution.isEmpty() && !stage.terrainEdit.apply(resolution,true,&report))break;
        }
        if(report.hasErrors())break;
        ++planned;
        if(!changed)continue;
        change.after.terrain=stage.terrainEdit;change.after.atlas=stage.atlas;
        stamp.paint.insert(target.key(),change);
    }
    if(planned!=origins.size() && !report.hasErrors())report.add("terrain.world.stage",EValidationSeverity::Error,"Не удалось подготовить все карты; мазок отменён.");
    if(report.hasErrors()){emit updateMsg(report.toText());return true;}
    if(stamp.isEmpty())return true;
    if(!m_worldTerrainSession.apply(stamp,true,&error)){emit updateMsg(error);return true;}
    bool previewed=true;
    for(const auto& id:stamp.paint.keys())previewed=previewWorldPaintState(id) && previewed;
    if(!previewed) {
        m_worldTerrainSession.apply(stamp,false,&error);
        for(const auto& id:stamp.paint.keys())previewWorldPaintState(id);
        emit updateMsg(tr("Предпросмотр общей кисти не обновлён; мазок отменён."));return true;
    }
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

bool CView::previewWorldPaintState(const QString& id)
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
    if(!landscape->previewTerrainAtlas(images,doc->atlas->activeTextureCount()))return false;
    landscape->setMaterials(worldRenderMaterials(doc->materials));
    for(const auto& sector:doc->baseline.sectors()) {
        const auto payload=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);

        if(!landscape->previewCanonicalSector(sector.sectorX,sector.sectorY,payload))return false;
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
            neighbor.terrainPreviewAtlas=doc->atlas;neighbor.drawCacheDirty=true;
            for(const auto& sector:doc->baseline.sectors())neighbor.terrainPreviewPayloads[qMakePair(sector.sectorX,sector.sectorY)]=doc->terrain.sectorPayload(sector.sectorX,sector.sectorY);
        }
    landscape->invalidateTerrainShadowField();update();return true;
}

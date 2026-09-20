#include "view.h"
#include "node.h"
#include "terrain_preview_context.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "staged_save_validator.h"
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
        item->setCheckState(m_worldTerrainEnabledPaths.contains(id)?Qt::Checked:Qt::Unchecked);
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
    m_worldTerrainEnabled=enabled->isChecked();
    for(int row=0;row<list->count();++row) {
        const auto item=list->item(row);const auto id=CWorldTerrainSession::key(item->data(Qt::UserRole).toString());
        if(item->checkState()==Qt::Checked)m_worldTerrainExcludedPaths.remove(id);
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
    if(!m_worldTerrainEnabled) return terrainPositionAt(viewport,position,true);
    SNeighborMapPlacement placement; QVector3D neighbor;
    if(neighborMapAtViewport(viewport,&placement,&neighbor,false)
       && m_worldTerrainEnabledPaths.contains(CWorldTerrainSession::key(placement.mprPath)))
    { *position=neighbor;return true; }
    return terrainPositionAt(viewport,position,true);
}

bool CView::sculptWorldTerrainAt(const QVector3D& position,float fraction)
{
    if(!m_worldTerrainEnabled || !m_landscapeTools || m_landscapeTools->overlaySculptEnabled()) return false;
    if(!m_hasActiveMapWorldOrigin || !m_neighborLayoutStorageWritable) return false;
    if(!m_terrainEditAvailable || m_neighborLayoutEditEnabled) return true;
    if(m_landscapeTools->sculptTool()==ELandscapeSculptTool::Noise)
    { emit updateMsg(tr("World terrain supports Raise, Lower, Flatten and Smooth. Disable it to use local Noise."));return true; }
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
    default:break;
    }
    const double quantum=m_terrainEdit.maxZ()/65535.0;
    const double strength=m_landscapeTools->strength();
    const double blend=1-std::pow(1-qBound(0.0,strength,1.0),double(fraction));
    const bool additive=mode==EWorldSculptMode::Raise || mode==EWorldSculptMode::Lower;
    const auto stamp=makeWorldTerrainStamp(targets,brush,mode,
        m_landscapeTools->rawDelta()*quantum*fraction,
        m_landscapeTools->flattenHeight()*quantum,additive?strength:blend);
    if(stamp.isEmpty()) return true;
    if(!m_worldTerrainSession.apply(stamp,true,&error)) { emit updateMsg(error);return true; }
    if(stamp.maps.contains(active) && !applyTerrainHeightPreview(stamp.maps[active],true))
    { m_worldTerrainSession.apply(stamp,false,&error);return true; }
    m_worldTerrainStroke.append(stamp);
    refreshWorldTerrainNeighbors();
    return true;
}

bool CView::worldTerrainHistory(bool forward,bool force)
{
    if(!force && !(m_worldTerrainEnabled && m_landscapeTools && m_landscapeTools->sculptEnabled()
       && !m_landscapeTools->overlaySculptEnabled())) return false;
    finishSculptStroke();syncWorldTerrainActive();
    const auto next=m_worldTerrainSession.nextCommand(forward);
    if(!next) return true;
    const auto stroke=*next;
    QString error;
    if(!prepareHistoryNavigation()) return true;
    if(!(forward?m_worldTerrainSession.redo(&error):m_worldTerrainSession.undo(&error)))
    { QMessageBox::warning(this,tr("World terrain history refused"),error);return true; }
    if(m_pLand->isMprLoad() && m_terrainEditAvailable)
    {
        const QString active=CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath());
        if(stroke.maps.contains(active)) applyTerrainHeightPreview(stroke.maps[active],forward);
    }
    refreshWorldTerrainNeighbors();
    emit updateMsg(forward?tr("Redid one complete world terrain stroke."):tr("Undid one complete world terrain stroke."));
    updateWorldTerrainStatus();
    return true;
}

bool CView::saveWorldTerrainMaps()
{
    finishSculptStroke();syncWorldTerrainActive();
    const QStringList paths=m_worldTerrainSession.dirtyPaths();
    if(paths.isEmpty()) return true;
    if(QMessageBox::question(this,tr("Save world terrain"),
        tr("Validate, back up and save all these MPRs?\n\n%1\n\nA failed write restores the whole set when possible. An interrupted save keeps recovery markers and backups; this is not atomic across files.").arg(paths.join('\n')),
        QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Save) return false;
    QVector<SWorldTerrainSaveItem> items;
    const QString active=m_pLand->isMprLoad()?CWorldTerrainSession::key(m_pLand->filePath().absoluteFilePath()):QString();
    for(const auto& path:paths)
    {
        const QString id=CWorldTerrainSession::key(path);const auto doc=m_worldTerrainSession.documents[id];
        SWorldTerrainSaveItem item;item.path=path;item.original=doc->baseline.originalBytes();
        CValidationReport report;
        if(id==active && m_terrainEditAvailable)
        {
            if(!buildValidatedLandscapeCandidate(&item.candidate,&report))
            { QMessageBox::warning(this,tr("World terrain save refused"),report.toText());return false; }
        }
        else
        {
            CMprEditTransaction transaction(QStringLiteral("World terrain"));
            if(!doc->terrain.appendSectorEdits(&transaction,&report)
               || !doc->baseline.appendMaxZEdit(doc->terrain.maxZ(),&transaction,&report)
               || !doc->baseline.applyTransaction(transaction,&item.candidate,&report))
            { QMessageBox::warning(this,tr("World terrain save refused"),report.toText());return false; }
        }
        items.append(item);
    }
    if(m_mapCohortActive) {
        auto files=m_mapCompanions;
        for(const auto& item:items){SMapSaveFile file;file.path=item.path;file.before=item.original;file.after=item.candidate;files.append(file);}
        const auto saved=commitMapSaveBatch(files,m_mapCohortBackupRoot);m_mapCohortCommitted=saved.success;
        if(!saved.success){QMessageBox::critical(this,tr("World terrain save failed"),saved.error);return false;}
    } else {
        const auto saved=saveWorldTerrainCohort(items);
        if(!saved.success)
        { QMessageBox::critical(this,tr("World terrain save failed"),saved.error+"\n"+saved.recoveryMarker);return false; }
    }
    for(const auto& item:items)
    {
        const QString id=CWorldTerrainSession::key(item.path);
        auto doc=m_worldTerrainSession.documents[id];CValidationReport report;
        CMprDocument reopened;CTerrainEditModel terrain;
        if(!reopened.load(QFileInfo(item.path),&report) || reopened.originalBytes()!=item.candidate
           || !terrain.load(reopened,&report))
        { QMessageBox::critical(this,tr("Saved terrain needs reopen"),report.toText());return false; }
        if(id==active && m_terrainEditAvailable)
        {
            SMprSaveCommitResult commit;commit.candidateSha256=QString::fromLatin1(QCryptographicHash::hash(item.candidate,QCryptographicHash::Sha256).toHex());
            if(!adoptCommittedLandscapeBaseline(QFileInfo(item.path),commit)) return false;
        }
        doc->baseline=reopened;doc->terrain=terrain;
    }
    emit updateMsg(tr("Saved and reopened %1 world MPR(s); verified backups retained.").arg(items.size()));
    updateWorldTerrainStatus();
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
            if(mprParametersDirty() || !buildValidatedLandscapeCandidate(&candidate,&report))
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
            { CValidationReport report;doc->terrain.load(doc->baseline,&report); }
            if(m_pLand->isMprLoad() && m_terrainEditAvailable) restoreWorldTerrainActive();
            refreshWorldTerrainNeighbors();
        }
    }
    for(auto& loaded:m_neighborLandscapes) loaded.landscape->setCanonicalTerrainModel(nullptr);
    m_worldTerrainSession.clear();m_worldTerrainEnabledPaths.clear();m_worldTerrainExcludedPaths.clear();m_worldTerrainEnabled=true;
    updateWorldTerrainStatus();
    return true;
}

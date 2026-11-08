#include "window_geometry.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "resource_profile.h"
#include "mpr_document.h"
#include "neighbor_scene_data.h"
#include "opening_trace.h"
#include "endurance_document.h"
#include "mob.h"
#include <QDialog>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QListWidget>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QDirIterator>
#include <QStatusBar>
#include <QUuid>
#include "world_coordinate_edit.h"
#include "world_map_snap.h"
#include "world_map_layout_command.h"
#include "log.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QtConcurrent/QtConcurrentRun>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString pathKey(const QString& path)
{
    if(path.isEmpty()) return QString();
    const QFileInfo info(path);
    return (info.exists() ? info.canonicalFilePath() : info.absoluteFilePath()).toCaseFolded();
}
QString metadataKey(const QFileInfo& info)
{
    // Size metadata needs a stable file revision, not a cold content hash of
    // every map on Qt. Windows identity includes file ID and ChangeTime, so a
    // same-size rewrite/restored mtime still invalidates this cache. The worker
    // rechecks the same token after parsing; unsupported filesystems keep SHA.
    const auto revision=neighborFileRevision(info.absoluteFilePath());
    return pathKey(info.absoluteFilePath()) + "|"
        + QString::fromLatin1((revision.isEmpty()?neighborContentFingerprint(info.absoluteFilePath()):revision).toHex());
}
}

QString MainWindow::resolvedMobPathForMpr(const QFileInfo& mpr)
{
    const auto profile=activeResourceProfile();
    CWorldMapLayoutDocument document;
    document.setProfileDirectory(profile.valid
        ? (profile.modRoot.isEmpty() ? profile.gameRoot : profile.modRoot) : QString(),
        profile.profileId, QString());
    const auto layout=document.loadEditorWorld(profile.profileId, activeMapRoots().toList());
    for(const auto& map:layout.zones)
        if(pathKey(document.absoluteResourcePath(map.mprPath))==pathKey(mpr.absoluteFilePath())
           && !map.mobPath.isEmpty())
            return document.absoluteResourcePath(map.mobPath);
    return CWorldMapLayoutDocument::companionMobPath(mpr.absoluteFilePath());
}

bool MainWindow::bindActiveMapMob(const QString& mobPath, QString* error,
                                   const QString& previousMobPath, bool allowUnboundPrimary)
{
    if(!CLandscape::getInstance()->isMprLoad()) return true;
    if(m_workspaceIdentityKind=="collection" || !m_currentCollectionPath.isEmpty()) return true;
    const auto profile=activeResourceProfile();
    if(!profile.valid) return true; // standalone documents have no profile layout
    const QFileInfo mpr=CLandscape::getInstance()->filePath();
    if(previousMobPath.isEmpty() && !allowUnboundPrimary
       && pathKey(resolvedMobPathForMpr(mpr))!=pathKey(mobPath))
        return true; // an additional/opened comparison MOB is not the map's MOB
    CWorldMapLayoutDocument document;
    document.setProfileDirectory(profile.modRoot.isEmpty() ? profile.gameRoot : profile.modRoot,
                                 profile.profileId, QString());
    document.loadEditorWorld(profile.profileId, activeMapRoots().toList());
    if(!document.bindMobToMap(mpr.absoluteFilePath(), mobPath, error, previousMobPath))
        return false;
    refreshMapTestContext(true, false);
    return true;
}

bool MainWindow::attachCompanionMob(const QFileInfo& mpr)
{
    const QString path=resolvedMobPathForMpr(mpr);
    if(path.isEmpty()) return true;
    if(!QFileInfo::exists(path))
    {
        // A missing explicit file remains an explicit choice. In particular,
        // do not substitute some other same-name MOB for it.
        statusBar()->showMessage(tr("The linked MOB is missing: %1").arg(path), 15000);
        return true;
    }
    for(CMob* mob:m_pView->mobs())
        if(pathKey(mob->filePath().absoluteFilePath())==pathKey(path))
        {
            QString error;
            if(bindActiveMapMob(path,&error)) return true;
            QMessageBox::warning(this,tr("Map MOB binding"),error);
            return false;
        }
    return loadMobWithProgress(QFileInfo(path));
}

bool MainWindow::validateWorldMap(SWorldMapZoneOrigin* map, QString* error)
{
    CMprDocument mpr; CValidationReport report;
    const QString mprPath = m_worldMapLayoutDocument.absoluteResourcePath(map->mprPath);
    if(!mpr.load(QFileInfo(mprPath), &report) || report.hasErrors())
    { *error = tr("Cannot read MPR:\n%1\n%2").arg(mprPath,report.toText()); return false; }
    map->sectorWidth = mpr.mapInfo().sectorCountX;
    map->sectorHeight = mpr.mapInfo().sectorCountY;
    map->mprIdentity = QFileInfo(mprPath).completeBaseName();
    if(map->sectorWidth <= 0 || map->sectorHeight <= 0)
    { *error = tr("The MPR dimensions are invalid."); return false; }
    if(!map->mobPath.isEmpty())
    {
        CMobDocument mob; CValidationReport mobReport;
        const QString mobPath = m_worldMapLayoutDocument.absoluteResourcePath(map->mobPath);
        if(!mob.load(QFileInfo(mobPath), &mobReport) || mobReport.hasErrors())
        { *error = tr("Cannot read MOB:\n%1\n%2").arg(mobPath,mobReport.toText()); return false; }
    }
    map->mprPath = m_worldMapLayoutDocument.portableResourcePath(mprPath);
    map->mobPath = m_worldMapLayoutDocument.portableResourcePath(
        m_worldMapLayoutDocument.absoluteResourcePath(map->mobPath));
    return true;
}

void MainWindow::refreshEditorWorld()
{
    opening::Scope timing294("gui.294.editor_world");
    // Defer metadata-driven scene replacement while checked rows are previewed.
    if(m_worldAlignmentPreviewActive) return;
    const auto profile = activeResourceProfile();
    const QString root = !profile.valid ? QString() :
        (profile.modRoot.isEmpty() ? profile.gameRoot : profile.modRoot);
    m_worldMapLayoutDocument.setProfileDirectory(root,profile.profileId,QString());
    QStringList roots;
    if(auto* opt=dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource,"mapRoots")))
        for(const auto& dir:opt->value()) roots.append(dir);
    m_worldMapLayoutDocument.reload();
    const auto layoutStart294=opening::clock();
    const auto result = m_worldMapLayoutDocument.loadEditorWorld(profile.profileId,roots);
    opening::span("gui.294.world_layout",layoutStart294);
    m_editorWorldMaps = result.zones;
    m_editorActiveMap = SWorldMapZoneOrigin();
    const QString loaded = CLandscape::getInstance()->isMprLoad()
        ? pathKey(CLandscape::getInstance()->filePath().absoluteFilePath()) : QString();
    QVector<SMapCatalogEntry> topology;
    QMap<QString,QString> pendingMetadata;
    const auto metadataStart294=opening::clock();
    for(auto& map : m_editorWorldMaps)
    {
        const QString path = m_worldMapLayoutDocument.absoluteResourcePath(map.mprPath);
        const QFileInfo info(path);
        if(path.isEmpty() || !info.isFile()) continue;
        // Dimensions come from the actual MPR, not map.txt or a stale JSON size.
        const QString cacheKey = metadataKey(info);
        const bool verified=m_worldMapSizeCache.contains(cacheKey);
        QSize size=m_worldMapSizeCache.value(cacheKey);
        if(!verified)
        {
            pendingMetadata.insert(cacheKey,path);
            // The open terrain already has dimensions for immediate placement.
            // Disk metadata is still verified by the worker before persistence.
            if(pathKey(path)==loaded) size=m_pView->activeTerrainSectorSize();
            else continue;
        }
        if(size.width()<=0 || size.height()<=0) continue;
        map.sectorWidth=size.width();map.sectorHeight=size.height();
        if(verified) m_worldMapLayoutDocument.updateResourceMetadata(map);
        SMapCatalogEntry entry;
        entry.zoneId=map.zoneId;entry.allodId=map.allodId;entry.mprName=map.mprIdentity;
        entry.mprPath=path;entry.mobPath=m_worldMapLayoutDocument.absoluteResourcePath(map.mobPath);
        entry.sectorWidth=map.sectorWidth;entry.sectorHeight=map.sectorHeight;
        topology.append(entry);
        if(pathKey(path)==loaded && (m_editorActiveMap.zoneId.isEmpty() || map.zoneId==m_preferredEditorActiveMap)) m_editorActiveMap=map;
    }
    opening::span("gui.294.world_metadata",metadataStart294);
    const auto neighborsStart294=opening::clock();
    m_pView->setNeighborMapContext(topology,m_editorWorldMaps,profile.profileId,
        m_editorActiveMap.allodId,m_editorActiveMap.zoneId,result.warning);
    opening::span("gui.294.world_neighbors",neighborsStart294);
    if(!result.warning.isEmpty() && result.warning!=m_lastWorldMapLayoutWarning)
        statusBar()->showMessage(result.warning,15000);
    m_lastWorldMapLayoutWarning=result.warning;
    if(!pendingMetadata.isEmpty() && !m_worldMetadataWatcher)
    {
        auto* watcher=new QFutureWatcher<QMap<QString,QSize>>(this);
        m_worldMetadataWatcher=watcher;
        connect(watcher,&QFutureWatcher<QMap<QString,QSize>>::finished,this,[this,watcher]() {
            const auto sizes=watcher->result();
            m_worldMetadataWatcher=nullptr;watcher->deleteLater();
            for(auto it=sizes.constBegin();it!=sizes.constEnd();++it) m_worldMapSizeCache.insert(it.key(),it.value());
            refreshEditorWorld(); // Re-evaluate current profile and resource revisions.
        });
        watcher->setFuture(QtConcurrent::run([pendingMetadata]() {
            QMap<QString,QSize> result;
            for(auto it=pendingMetadata.constBegin();it!=pendingMetadata.constEnd();++it) {
                CMprDocument doc;CValidationReport report;
                QSize size;
                if(doc.load(QFileInfo(it.value()),&report) && !report.hasErrors())
                    size=QSize(doc.mapInfo().sectorCountX,doc.mapInfo().sectorCountY);
                if(metadataKey(QFileInfo(it.value()))==it.key()) result.insert(it.key(),size);
            }
            return result;
        }));
    }
}

void MainWindow::addMapToWorld()
{
    if(m_workspaceLoadInProgress) return;
    m_pView->cancelWorldMapPlacement();
    refreshEditorWorld();
    if(!m_lastWorldMapLayoutWarning.isEmpty())
    { QMessageBox::warning(this,tr("Add map to world"),m_lastWorldMapLayoutWarning);return; }
    const auto profile = activeResourceProfile();
    QDialog dialog(this);ei::rememberWindowGeometry(&dialog, QStringLiteral("worldAddMap"));dialog.setWindowTitle(tr("Add map to world"));
    auto* layout=new QVBoxLayout(&dialog);auto* form=new QFormLayout;
    layout->addLayout(form);
    auto* name=new QLineEdit(&dialog);
    auto* mpr=new QComboBox(&dialog);mpr->setEditable(true);mpr->addItem(QString());
    QStringList roots;
    if(auto* opt=dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource,"mapRoots")))
        for(const auto& dir:opt->value()) roots.append(dir);
    QSet<QString> seen;
    for(const auto& root:roots)
    {
        QDirIterator it(root,QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext())
        {
            QFileInfo info(it.next());
            if(info.suffix().compare("mpr",Qt::CaseInsensitive)!=0) continue;
            if(seen.contains(pathKey(info.filePath()))) continue;
            seen.insert(pathKey(info.filePath()));mpr->addItem(info.absoluteFilePath());
        }
    }
    auto* browseMpr=new QPushButton(tr("Browse..."),&dialog);
    auto* mprRow=new QHBoxLayout;mprRow->addWidget(mpr,1);mprRow->addWidget(browseMpr);
    auto* mob=new QLineEdit(&dialog);auto* browseMob=new QPushButton(tr("Browse..."),&dialog);
    auto* mobRow=new QHBoxLayout;mobRow->addWidget(mob,1);mobRow->addWidget(browseMob);
    auto* worlds=new QComboBox(&dialog);worlds->setEditable(true);
    if(!m_editorActiveMap.zoneId.isEmpty())
    {worlds->addItem(m_editorActiveMap.allodId);worlds->setEnabled(false);}
    else
    {
        QSet<QString> ids;
        for(const auto& map:m_editorWorldMaps) if(!ids.contains(map.allodId))
        {ids.insert(map.allodId);worlds->addItem(map.allodId);}
        worlds->setEditText(tr("New world"));
    }
    auto* dimensions=new QLabel(&dialog);
    form->addRow(tr("World:"),worlds);form->addRow(tr("Name:"),name);
    form->addRow(tr("MPR:"),mprRow);form->addRow(tr("MOB (optional):"),mobRow);
    layout->addWidget(dimensions);
    auto* hint=new QLabel(tr("Select a profile MPR or browse for a file. Files stay in place. After validation, click in the scene to place the map; Esc cancels. An unregistered open map is added to this world as its base."),&dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    layout->addWidget(buttons);
    connect(browseMpr,&QPushButton::clicked,&dialog,[&](){
        const QString path=QFileDialog::getOpenFileName(&dialog,tr("Select MPR"),QString(),"MPR (*.mpr)");
        if(!path.isEmpty()) mpr->setEditText(path);
    });
    connect(browseMob,&QPushButton::clicked,&dialog,[&](){
        const QString path=QFileDialog::getOpenFileName(&dialog,tr("Select MOB"),QString(),"MOB (*.mob)");
        if(!path.isEmpty()) mob->setText(path);
    });
    connect(mpr,&QComboBox::currentTextChanged,&dialog,[&](const QString& path){
        const QFileInfo info(path);name->setText(info.completeBaseName());
        const QString sibling=info.dir().filePath(info.completeBaseName()+".mob");
        mob->setText(QFileInfo::exists(sibling)?sibling:QString());
        dimensions->clear();
        if(info.isFile())
        {
            CMprDocument probe; CValidationReport report;
            if(probe.load(info,&report) && !report.hasErrors())
                dimensions->setText(tr("Size: %1 × %2").arg(probe.mapInfo().sectorCountX*32).arg(probe.mapInfo().sectorCountY*32));
        }
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    SWorldMapZoneOrigin map;
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&](){
        map=SWorldMapZoneOrigin();map.profileId=profile.profileId;map.allodId=worlds->currentText().trimmed();
        map.zoneId=newId();map.displayName=name->text().trimmed();map.mprPath=mpr->currentText().trimmed();
        map.mobPath=mob->text().trimmed();map.provenance=EWorldMapOriginProvenance::Manual;
        if(map.allodId.isEmpty() || map.allodId.contains('/') || map.allodId.contains('\\') || map.allodId.contains(':'))
        {QMessageBox::warning(&dialog,tr("Add map to world"),tr("Enter a world name without path separators."));return;}
        QString error;
        if(!validateWorldMap(&map,&error)) {QMessageBox::warning(&dialog,tr("Add map to world"),error);return;}
        for(const auto& existing:m_editorWorldMaps)
            if(pathKey(m_worldMapLayoutDocument.absoluteResourcePath(existing.mprPath))==pathKey(m_worldMapLayoutDocument.absoluteResourcePath(map.mprPath)))
            {QMessageBox::warning(&dialog,tr("Add map to world"),tr("This MPR is already in the world layout. Use World maps to open or arrange it."));return;}
        dimensions->setText(tr("Size: %1 × %2").arg(map.sectorWidth*32).arg(map.sectorHeight*32));
        dialog.accept();
    });
    dialog.resize(680,300);
    if(dialog.exec()!=QDialog::Accepted) return;
    m_pendingWorldMaps.clear();
    QString anchor=m_editorActiveMap.componentAnchorZoneId;
    for(const auto& existing:m_editorWorldMaps)
        if(anchor.isEmpty() && existing.allodId==map.allodId) anchor=existing.componentAnchorZoneId;
    if(CLandscape::getInstance()->isMprLoad() && m_editorActiveMap.zoneId.isEmpty())
    {
        SWorldMapZoneOrigin base;
        base.profileId=profile.profileId;base.allodId=map.allodId;base.zoneId=newId();
        base.mprPath=CLandscape::getInstance()->filePath().absoluteFilePath();
        if(pathKey(base.mprPath)==pathKey(m_worldMapLayoutDocument.absoluteResourcePath(map.mprPath)))
        {map.componentAnchorZoneId=anchor.isEmpty()?map.zoneId:anchor;m_pendingWorldMaps.append(map);commitWorldMapPlacement(QPointF());return;}
        if(auto* primary=mapTestStagingMob()) base.mobPath=primary->filePath().absoluteFilePath();
        base.displayName=QFileInfo(base.mprPath).completeBaseName();base.provenance=EWorldMapOriginProvenance::Manual;
        base.componentAnchorZoneId=anchor.isEmpty()?base.zoneId:anchor;
        QString error;
        if(!validateWorldMap(&base,&error)){QMessageBox::warning(this,tr("Add map to world"),error);return;}
        anchor=base.componentAnchorZoneId;m_pendingWorldMaps.append(base);
    }
    map.componentAnchorZoneId=anchor.isEmpty()?map.zoneId:anchor;
    m_pendingWorldMaps.append(map);
    if(!CLandscape::getInstance()->isMprLoad()) {commitWorldMapPlacement(QPointF());return;}
    QPointF gridAnchor; bool hasGridAnchor=false;
    for(const auto& candidate:m_editorWorldMaps+m_pendingWorldMaps)
        if(candidate.allodId==map.allodId && candidate.zoneId==map.componentAnchorZoneId)
        { gridAnchor=candidate.origin; hasGridAnchor=true; break; }
    m_pView->beginWorldMapPlacement(QSizeF(map.sectorWidth*32,map.sectorHeight*32),gridAnchor,hasGridAnchor);
}

void MainWindow::commitWorldMapPlacement(const QPointF& origin)
{
    if(m_pendingWorldMaps.isEmpty()) return;
    auto maps=m_pendingWorldMaps;m_pendingWorldMaps.clear();
    maps.last().origin=origin;
    QString error;
    if(maps.last().profileId!=activeResourceProfile().profileId)
    {QMessageBox::warning(this,tr("Add map to world"),tr("The resource profile changed; placement was cancelled."));return;}
    for(auto& map:maps) if(!validateWorldMap(&map,&error))
    {QMessageBox::warning(this,tr("Add map to world"),error);return;}
    QVector<SWorldMapLayoutChange> changes;
    for(const auto& map:maps) { SWorldMapLayoutChange change;change.hadBefore=false;change.after=map;changes.append(change); }
    if(!commitWorldLayoutChanges(changes,QString::fromUtf8("Добавление карты в мир"))) return;
    m_pView->showWorldMap(maps.last().zoneId);refreshEditorWorld();
    if(!CLandscape::getInstance()->isMprLoad())
        openNeighborMapPair(m_worldMapLayoutDocument.absoluteResourcePath(maps.last().mprPath),
            m_worldMapLayoutDocument.absoluteResourcePath(maps.last().mobPath),maps.last().zoneId,origin,origin);
    statusBar()->showMessage(tr("Map added to world. Source files were not changed."),6000);
}

void MainWindow::manageWorldMaps()
{
    m_pView->cancelWorldMapPlacement();refreshEditorWorld();
    const auto maps=m_editorWorldMaps; // Async metadata refresh must not retarget list rows.
    QDialog dialog(this);ei::rememberWindowGeometry(&dialog, QStringLiteral("worldMaps"));dialog.setWindowTitle(tr("World maps"));
    auto* layout=new QVBoxLayout(&dialog);auto* list=new QListWidget(&dialog);layout->addWidget(list);
    for(const auto& map:maps)
    {
        const QString path=m_worldMapLayoutDocument.absoluteResourcePath(map.mprPath);
        auto* item=new QListWidgetItem(QString("%1 / %2 — X %3, Y %4%5")
            .arg(map.allodId,map.displayName.isEmpty()?map.mprIdentity:map.displayName)
            .arg(map.origin.x()).arg(map.origin.y())
            .arg(QFileInfo(path).isFile()?QString():tr(" — File not found")),list);
        item->setToolTip(path);
    }
    auto* open=new QPushButton(tr("Open map"),&dialog);
    auto* show=new QPushButton(tr("Show among neighbors"),&dialog);
    auto* remove=new QPushButton(tr("Remove from world"),&dialog);
    auto* locate=new QPushButton(tr("Map files and position..."),&dialog);
    auto* align=new QPushButton(QString::fromUtf8("Выровнять по тайлам…"),&dialog);
    connect(align,&QPushButton::clicked,&dialog,[&](){dialog.accept();alignWorldMapTiles();});
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    layout->addWidget(open);layout->addWidget(show);layout->addWidget(locate);layout->addWidget(align);layout->addWidget(remove);layout->addWidget(close);
    auto* importWorld=new QPushButton(QString::fromUtf8("Перенести координаты из другого профиля…"),&dialog);
    layout->insertWidget(0,importWorld);
    const QString target=activeResourceProfile().profileId;
    QStringList sources=m_worldMapLayoutDocument.availableProfiles();
    bool destinationExists=false;
    for(const auto& id:sources) destinationExists |= id.compare(target,Qt::CaseInsensitive)==0;
    importWorld->setEnabled(!destinationExists && !sources.isEmpty());
    connect(importWorld,&QPushButton::clicked,&dialog,[&]() {
        bool accepted=false;
        const QString source=QInputDialog::getItem(&dialog,QString::fromUtf8("Перенос координат"),
            QString::fromUtf8("Из какого профиля перенести размещение карт в %1?").arg(target),
            sources,0,false,&accepted);
        if(!accepted) return;
        if(QMessageBox::question(&dialog,QString::fromUtf8("Перенос координат"),
            QString::fromUtf8("Скопировать размещение карт из %1 в %2?\nКоординаты и пути карт сохранятся. Будет создана резервная копия файла мира.\n%3")
                .arg(source,target,m_worldMapLayoutDocument.path()),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        QString error;
        if(!m_worldMapLayoutDocument.importProfile(source,target,&error))
        { QMessageBox::warning(&dialog,tr("World maps"),error); return; }
        dialog.accept(); refreshEditorWorld();
    });
    connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(open,&QPushButton::clicked,&dialog,[&](){
        const int index=list->currentRow();if(index<0)return;
        const auto map=maps[index];dialog.accept();
        openNeighborMapPair(m_worldMapLayoutDocument.absoluteResourcePath(map.mprPath),
            m_worldMapLayoutDocument.absoluteResourcePath(map.mobPath),map.zoneId,m_editorActiveMap.origin,map.origin);
    });
    connect(show,&QPushButton::clicked,&dialog,[&](){
        const int index=list->currentRow();if(index<0)return;
        const auto map=maps[index];
        if(m_editorActiveMap.zoneId.isEmpty() || map.allodId!=m_editorActiveMap.allodId || map.zoneId==m_editorActiveMap.zoneId)
        {QMessageBox::information(&dialog,tr("World maps"),tr("Open another map of this world first."));return;}
        m_pView->showWorldMap(map.zoneId);dialog.accept();refreshEditorWorld();
    });
    connect(locate,&QPushButton::clicked,&dialog,[&](){
        const int index=list->currentRow();if(index<0)return;
        auto map=maps[index];
        QDialog edit(&dialog);ei::rememberWindowGeometry(&edit, QStringLiteral("worldMapProperties"));edit.setWindowTitle(tr("Map files and position"));
        auto* form=new QFormLayout(&edit);
        auto* name=new QLineEdit(map.displayName,&edit);
        auto* mpr=new QLineEdit(m_worldMapLayoutDocument.absoluteResourcePath(map.mprPath),&edit);
        auto* mob=new QLineEdit(m_worldMapLayoutDocument.absoluteResourcePath(map.mobPath),&edit);
        auto* x=new CWorldCoordinateEdit(map.origin.x(),&edit);
        auto* y=new CWorldCoordinateEdit(map.origin.y(),&edit);
        form->addRow(tr("Name:"),name);
        const auto fileRow=[&](QLineEdit* field,const QString& label,const QString& filter){
            auto* row=new QHBoxLayout;auto* browse=new QPushButton(tr("Browse..."),&edit);
            row->addWidget(field);row->addWidget(browse);form->addRow(label,row);
            connect(browse,&QPushButton::clicked,&edit,[&,field,filter](){
                const QString selected=QFileDialog::getOpenFileName(&edit,tr("Select map file"),field->text(),filter);
                if(!selected.isEmpty())field->setText(selected);
            });
        };
        fileRow(mpr,tr("MPR:"),"MPR (*.mpr)");fileRow(mob,tr("MOB (optional):"),"MOB (*.mob)");
        form->addRow("X:",x);form->addRow("Y:",y);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&edit);form->addRow(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&edit,&QDialog::reject);
        connect(buttons,&QDialogButtonBox::accepted,&edit,[&](){
            double originX,originY;
            if(!x->coordinate(&originX) || !y->coordinate(&originY))
            {QMessageBox::warning(&edit,tr("World maps"),tr("Enter finite X and Y coordinates using a decimal point."));return;}
            map.displayName=name->text().trimmed();map.mprPath=mpr->text().trimmed();map.mobPath=mob->text().trimmed();
            map.origin=QPointF(originX,originY);map.provenance=EWorldMapOriginProvenance::Manual;
            QString error;
            if(!validateWorldMap(&map,&error) || !m_worldMapLayoutDocument.replaceEditorMap(map,&error))
            {QMessageBox::warning(&edit,tr("World maps"),error);return;}
            edit.accept();
        });
        edit.resize(660,280);
        if(edit.exec()==QDialog::Accepted){dialog.accept();refreshEditorWorld();}
    });
    connect(remove,&QPushButton::clicked,&dialog,[&](){
        const int index=list->currentRow();if(index<0)return;
        const auto map=maps[index];
        if(QMessageBox::question(&dialog,tr("Remove from world"),tr("Remove this map from the world layout? MPR and MOB files will remain on disk."))!=QMessageBox::Yes)return;
        QString error;
        if(!m_worldMapLayoutDocument.removeEditorMap(map.profileId,map.allodId,map.zoneId,&error))
        {QMessageBox::warning(&dialog,tr("World maps"),error);return;}
        dialog.accept();refreshEditorWorld();
    });
    dialog.resize(660,460);dialog.exec();
}

bool MainWindow::commitWorldLayoutChanges(const QVector<SWorldMapLayoutChange>& changes, const QString& label)
{
    if(changes.isEmpty()) return true;
    const QString path=m_worldMapLayoutDocument.path();
    const QString profile=activeResourceProfile().profileId;
    auto success=QSharedPointer<bool>::create(false);
    m_undoStack->push(new CWorldMapLayoutCommand(label,
        [this,path,profile,changes,success](bool forward,QString* error) {
            if(m_worldMapLayoutDocument.path()!=path || activeResourceProfile().profileId!=profile)
            { *error=QString::fromUtf8("Профиль мира изменился; прежняя раскладка не изменена."); return false; }
            *success=m_worldMapLayoutDocument.applyChanges(changes,forward,error);
            if(*success) refreshEditorWorld();
            return *success;
        },[this](const QString& error) {
            refreshEditorWorld();
            ei::log(eLogWarning,error);
            statusBar()->showMessage(QString::fromUtf8("Раскладка не изменена: ")+error,15000);
        }));
    return *success;
}

void MainWindow::alignWorldMapTiles()
{
    m_pView->cancelPendingOperation(); m_pView->cancelWorldMapPlacement();
    refreshEditorWorld();
    if(m_editorActiveMap.zoneId.isEmpty() || !m_lastWorldMapLayoutWarning.isEmpty())
    { statusBar()->showMessage(QString::fromUtf8("Откройте карту мира с доступной раскладкой."),8000); return; }
    QVector<SWorldMapZoneOrigin> maps;
    for(const auto& map:m_editorWorldMaps) if(map.allodId==m_editorActiveMap.allodId) maps.append(map);
    QDialog dialog(this);dialog.setWindowTitle(QString::fromUtf8("Выровнять по тайлам"));
    ei::rememberWindowGeometry(&dialog,QStringLiteral("alignWorldMapTiles"));
    auto* layout=new QVBoxLayout(&dialog);
    auto* hint=new QLabel(QString::fromUtf8("Выберите карты для выравнивания. Опорные карты остаются на месте. Изменения видны в сцене; «Отмена» возвращает исходное положение."),&dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto* table=new QTableWidget(maps.size(),3,&dialog);table->setObjectName("worldTileAlignmentTable");
    table->setHorizontalHeaderLabels({QString::fromUtf8("Карта"),QString::fromUtf8("Сейчас X / Y"),QString::fromUtf8("После X / Y")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(table);
    const auto coords=[](QPointF p) { return QString("%1 / %2").arg(p.x(),0,'g',12).arg(p.y(),0,'g',12); };
    QVector<SWorldMapLayoutChange> available;
    for(int i=0;i<maps.size();++i)
    {
        const auto& map=maps[i];SWorldMapLayoutChange change;change.before=map;change.after=map;
        const auto* anchor=CWorldMapLayout::zone(maps,map.profileId,map.allodId,map.componentAnchorZoneId);
        const bool movable=anchor && map.zoneId!=map.componentAnchorZoneId;
        if(movable) { change.after.origin=CWorldMapSnap::align(map.origin,anchor->origin);change.after.provenance=EWorldMapOriginProvenance::Manual; }
        auto* name=new QTableWidgetItem(map.displayName.isEmpty()?map.mprIdentity:map.displayName);
        const bool changed=movable && change.after.origin!=map.origin;
        name->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled|(changed?Qt::ItemIsUserCheckable:Qt::NoItemFlags));
        if(changed) name->setCheckState(Qt::Checked);
        else name->setToolTip(anchor?QString::fromUtf8("Положение уже соответствует сетке или карта является опорной."):QString::fromUtf8("Опорная карта не найдена."));
        table->setItem(i,0,name);table->setItem(i,1,new QTableWidgetItem(coords(map.origin)));
        table->setItem(i,2,new QTableWidgetItem(coords(change.after.origin)));available.append(change);
    }
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Apply)->setText(QString::fromUtf8("Применить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    QVector<SWorldMapLayoutChange> selected;
    QMap<QString,QPointF> original;
    for(const auto& map:maps) original.insert(map.zoneId,map.origin);
    const auto preview=[&]() {
        auto positions=original;selected.clear();
        for(int i=0;i<available.size();++i)
            if(table->item(i,0)->checkState()==Qt::Checked)
            { selected.append(available[i]);positions[available[i].after.zoneId]=available[i].after.origin; }
        buttons->button(QDialogButtonBox::Apply)->setEnabled(!selected.isEmpty());
        m_pView->previewWorldMapOrigins(positions);
    };
    connect(table,&QTableWidget::itemChanged,&dialog,[&](QTableWidgetItem*){preview();});
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    m_worldAlignmentPreviewActive=true;
    preview();dialog.resize(680,420);
    const bool accepted=dialog.exec()==QDialog::Accepted;
    m_pView->previewWorldMapOrigins(original);
    m_worldAlignmentPreviewActive=false;
    if(accepted) commitWorldLayoutChanges(selected,QString::fromUtf8("Выравнивание карт по тайлам"));
    else refreshEditorWorld();
}

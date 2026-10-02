#include "window_geometry.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "resource_profile.h"
#include "mpr_document.h"
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
    return pathKey(info.absoluteFilePath()) + "|" + QString::number(info.size())
        + "|" + QString::number(info.lastModified().toMSecsSinceEpoch());
}
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
    const auto profile = activeResourceProfile();
    const QString root = !profile.valid ? QString() :
        (profile.modRoot.isEmpty() ? profile.gameRoot : profile.modRoot);
    m_worldMapLayoutDocument.setProfileDirectory(root,profile.profileId,QString());
    QStringList roots;
    if(auto* opt=dynamic_cast<COptStringList*>(m_settings->opt(eOptSetResource,"mapRoots")))
        for(const auto& dir:opt->value()) roots.append(dir);
    m_worldMapLayoutDocument.reload();
    const auto result = m_worldMapLayoutDocument.loadEditorWorld(profile.profileId,roots);
    m_editorWorldMaps = result.zones;
    m_editorActiveMap = SWorldMapZoneOrigin();
    const QString loaded = CLandscape::getInstance()->isMprLoad()
        ? pathKey(CLandscape::getInstance()->filePath().absoluteFilePath()) : QString();
    QVector<SMapCatalogEntry> topology;
    QMap<QString,QString> pendingMetadata;
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
    m_pView->setNeighborMapContext(topology,m_editorWorldMaps,profile.profileId,
        m_editorActiveMap.allodId,m_editorActiveMap.zoneId,result.warning);
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
    m_pView->beginWorldMapPlacement(QSizeF(map.sectorWidth*32,map.sectorHeight*32));
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
    if(!m_worldMapLayoutDocument.addEditorMaps(maps,&error))
    {QMessageBox::warning(this,tr("Add map to world"),error);return;}
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
    auto* close=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    layout->addWidget(open);layout->addWidget(show);layout->addWidget(locate);layout->addWidget(remove);layout->addWidget(close);
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

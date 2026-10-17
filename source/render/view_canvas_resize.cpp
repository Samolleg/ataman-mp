#include "view.h"
#include "canvas_navigation_state.h"
#include "landscape.h"
#include "mob.h"
#include "resourcemanager.h"
#include "new_terrain_canvas.h"
#include "terrain_semantic_field_store.h"
#include "res_archive_document.h"
#include "technical_model_catalog.h"
#include "profile_database.h"
#include "staged_save_validator.h"
#include "scene.h"
#include <QtWidgets>
#include <memory>
#include <vector>

namespace {
const char* staleSchema = "ei-mapper-canvas-navigation-stale-v1";
QByteArray fileBytes(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString fileKey(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded(); }
}
QString canvasNavigationMarkerPath(const QFileInfo& mpr) {
    return mpr.absoluteFilePath() + QStringLiteral(".navigation-stale.json");
}
bool canvasNavigationIsStale(const QFileInfo& mpr) {
    return !mpr.filePath().isEmpty() && QFileInfo::exists(canvasNavigationMarkerPath(mpr));
}
bool CView::clearCanvasNavigationStale(QString* error) {
    const QString path=canvasNavigationMarkerPath(m_pLand->filePath());
    if(!QFileInfo::exists(path)) { m_canvasNavigationStale=false; return true; }
    const auto bytes=fileBytes(path);
    if(QJsonDocument::fromJson(bytes).object().value("schema").toString()!=staleSchema
       || bytes!=fileBytes(path) || !QFile::remove(path)) {
        if(error)*error=tr("Не удалось снять отметку устаревшей навигации: %1").arg(path);
        return false;
    }
    m_canvasNavigationStale=false; return true;
}

bool CView::applyCanvasResizeToCurrent(const SMprCanvasResizeRequest& resize,
                                      bool removeOutsideObjects, QString* error)
{
    const auto refuse=[&](const QString& text){if(error)*error=text;return false;};
    if(!m_pLand || !m_pLand->isMprLoad() || !ensureTerrainAutoCatalogReady())
        return refuse(tr("Откройте MPR перед изменением холста."));
    if(CScene::getInstance()->getMode()!=eEditModeObjects)
        return refuse(tr("Переключитесь из редактирования логики в режим объектов."));
    // Resolve editor drafts while cancellation still leaves the disk cohort
    // untouched. Unloading after a successful batch must not ask for edits.
    if(!closeAllMobParameterWindows() || !closeAllMobScriptWindows())return true;
    finishSculptStroke();endTileBrushGroup();syncWorldTerrainActive();
    const QFileInfo destination=m_pLand->filePath();
    const QString currentKey=CWorldTerrainSession::key(destination.absoluteFilePath());
    for(const auto& path:m_worldTerrainSession.dirtyPaths())
        if(CWorldTerrainSession::key(path)!=currentKey)
            return refuse(tr("Сначала сохраните изменения других карт общей кисти."));
    const auto original=m_mprDocument.originalBytes();
    const QString atlasPath=destination.dir().filePath(destination.completeBaseName()+"-atlas.res");
    const bool hadAtlas=QFileInfo::exists(atlasPath);
    const QByteArray atlasBefore=fileBytes(atlasPath);
    if(fileBytes(destination.absoluteFilePath())!=original)
        return refuse(tr("MPR изменилась на диске. Перечитайте её перед изменением холста."));
    CValidationReport report;
    auto task=prepareLandscapeRecoveryTask(&report);
    if(!task) return refuse(report.toText());
    const auto prepared=task();
    if(!prepared.success) return refuse(prepared.report.toText());
    CMprDocument baseline,resized;
    QByteArray candidate;
    if(!baseline.loadBytes(prepared.mprBytes,"resize-baseline.mpr",&report)
       || !baseline.buildResizedCanvas(resize,&candidate,&report)
       || !resized.loadBytes(candidate,"resized.mpr",&report) || report.hasErrors())
        return refuse(report.toText());
    CTerrainEditModel resizedTerrain;
    if(!resizedTerrain.load(resized,&report) || resizedTerrain.hasSeamConflicts())
        return refuse(tr("Изменённый холст не прошёл проверку рельефа/стыков.\n%1").arg(report.toText()));
    for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
        QVector<qint32> old,ignored;
        m_terrainEdit.copyVirtualMaterialFieldState(layer,&old,&ignored);
        if(old.isEmpty()) continue;
        const int oldW=m_terrainEdit.tileCountX()+1,oldH=m_terrainEdit.tileCountY()+1;
        const int newW=resizedTerrain.tileCountX()+1,newH=resizedTerrain.tileCountY()+1;
        QVector<qint32> field(newW*newH);
        for(int y=0;y<newH;++y) for(int x=0;x<newW;++x) {
            const int sx=x-resize.oldTileOffsetX,sy=y-resize.oldTileOffsetY;
            const bool outside=sx<0 || sy<0 || sx>=oldW || sy>=oldH;
            field[y*newW+x]=layer==ETerrainTileLayer::Overlay && outside ? 0
                : old[qBound(0,sy,oldH-1)*oldW+qBound(0,sx,oldW-1)];
        }
        if(!resizedTerrain.restoreVirtualMaterialFieldState(layer,field,field,&report))
            return refuse(report.toText());
    }
    QByteArray atlas=prepared.atlasBytes;
    if(atlas.isEmpty() && !buildNewTerrainAtlas(destination,prepared.mprBytes,
          QStringList{m_activeTerrainArchivePath}+CTextureList::getInstance()->configuredResourceArchivePaths(),
          &atlas,error)) return false;
    CResArchiveDocument archive;
    QStringList errors;
    if(!archive.loadBytes(atlas,"resize-atlas.res",&errors)) return refuse(errors.join('\n'));
    const QString ns=resized.mapInfo().innerNamespace;
    const QString profileName=CTerrainAtlasProfileStore::embeddedMemberName(ns);
    const QByteArray profileBytes=archive.memberPayload(profileName);
    STerrainAtlasProfile profile;
    if(!STerrainAtlasProfile::fromBytes(profileBytes,&profile,&report))return refuse(report.toText());
    SResArchiveNewMember semantics;
    QByteArray embedded;
    if(!buildTerrainSemanticFieldMember(candidate,destination,ns,profile.atlasPixelSha256,
          profileBytes,&semantics,&report,&resizedTerrain,&embedded))return refuse(report.toText());
    if(!embedded.isEmpty())candidate=embedded;
    if(!semantics.name.isEmpty() && !archive.buildWithPublishedMembers({semantics},true,&atlas,&errors))
        return refuse(errors.join('\n'));
    const auto staged=validateStagedSave(candidate,destination);
    if(!staged.success)return refuse(staged.error);
    QVector<SMapSaveFile> files;
    auto addFile=[&](const QString& path,const QByteArray& after,const QByteArray* expected=nullptr){
        SMapSaveFile file;file.path=path;file.existed=QFileInfo::exists(path);
        if(file.existed){QFile input(path);if(!input.open(QIODevice::ReadOnly))return false;file.before=input.readAll();}
        if(expected && file.before!=*expected)return false;
        file.after=after;files.append(file);return true;
    };
    if(hadAtlas!=QFileInfo::exists(atlasPath)
       || !addFile(destination.absoluteFilePath(),candidate,&original)
       || !addFile(atlasPath,atlas,&atlasBefore))
        return refuse(tr("Не удалось подготовить согласованную пару MPR/атлас."));
    QStringList paths,loadedPaths;QMap<QString,bool> visibility;QString activePath;
    for(CMob* mob:m_aMob) if(mob) {
        const QString path=mob->filePath().absoluteFilePath();paths<<path;loadedPaths<<path;
        visibility[fileKey(path)]=isMobVisible(mob);if(mob==m_activeMob)activePath=path;
    }
    const QString sibling=destination.dir().filePath(destination.completeBaseName()+".mob");
    if(QFileInfo::exists(sibling))paths<<sibling;
    emit canvasResizeMobPathsRequested(&paths);
    const QString environmentPath=m_environmentMob
        ? m_environmentMob->filePath().absoluteFilePath() : QString();
    QSet<QString> seen;
    std::vector<std::unique_ptr<CMob>> privateMobs;
    struct RestoreContext { CView* view; ~RestoreContext(){view->makeCurrent();} } contextGuard{this};
    int objects=0,removed=0;
    const auto db=ei::ProfileDatabaseContext::instance().current();
    const QString catalogPath=ei::TechnicalModelCatalog::pathForDatabase(db?db->sourceArchivePath:QString());
    ei::TechnicalModelCatalog catalog;bool catalogChanged=false;
    const QByteArray catalogBefore=fileBytes(catalogPath);
    if(!catalogPath.isEmpty() && QFileInfo::exists(catalogPath)
       && (!catalog.load(catalogPath,error) || fileBytes(catalogPath)!=catalogBefore))return false;
    for(const QString& path:paths) {
        const QString key=fileKey(path);if(seen.contains(key))continue;seen.insert(key);
        CMob* mob=nullptr;
        for(CMob* loaded:m_aMob)if(loaded && fileKey(loaded->filePath().absoluteFilePath())==key)mob=loaded;
        if(!mob) {
            std::unique_ptr<CMob> owned(new CMob);owned->attach(this,m_pProgress);makeCurrent();
            QFileInfo source(path);if(owned->readMob(source,false)!=0)return refuse(tr("Не удалось прочитать связанный MOB: %1").arg(path));
            mob=owned.get();privateMobs.push_back(std::move(owned));
        }
        const QByteArray before=mob->originalData();
        if(before.isEmpty() || fileBytes(path)!=before)return refuse(tr("Связанный MOB изменился на диске: %1").arg(path));
        if(!removeOutsideObjects)for(CNode* node:mob->nodes())if(node && !node->isMarkDeleted()) {
            const float x=node->position().x()+resize.oldTileOffsetX*2.0f,y=node->position().y()+resize.oldTileOffsetY*2.0f;
            const float width=resize.sectorCountX*32.0f,height=resize.sectorCountY*32.0f;
            const float taper=resize.closeEdges?resize.edgeTaperTiles*2.0f:0;
            if(x<0 || y<0 || x>width || y>height || (taper>0 && (x<taper||y<taper||x>width-taper||y>height-taper)))
                return refuse(tr("Объекты %1 выходят за сохраняемую область. Измените холст/смещение либо явно включите удаление объектов.").arg(path));
        }
        QByteArray bytes;CMob::SCanvasCropResult crop;
        if(!mob->buildCanvasCropCandidate(resize.oldTileOffsetX*2.0f,resize.oldTileOffsetY*2.0f,
              resize.sectorCountX*32.0f,resize.sectorCountY*32.0f,removeOutsideObjects,
              resize.closeEdges?resize.edgeTaperTiles*2.0f:0,&bytes,&crop,&report,true))return refuse(report.toText());
        const auto validation=validateStagedSave(bytes,QFileInfo(path));
        if(!validation.success || !addFile(path,bytes,&before))return refuse(tr("Кандидат MOB не прошёл проверку: %1\n%2").arg(path,validation.error));
        objects+=crop.translatedObjects;removed+=crop.removedObjects;
        // Refresh only this MOB's grouping/hash. Unrelated catalog entries survive.
        QJsonArray groups;CMobDocument mobDoc;
        if(!mobDoc.loadBytes(bytes,path,&report))return refuse(report.toText());
        QSet<quint32> kept;for(const auto& object:mobDoc.objects())if(object.hasNid)kept.insert(object.nid);
        QJsonObject grouped;
        for(CNode* node:mob->nodes())if(kept.contains(node->mapId())&&!node->assemblyTag().isEmpty()) {
            const auto tag=node->assemblyTag();const auto id=tag.value("instanceId").toString();auto group=grouped.value(id).toObject();
            group.insert("tag",tag);auto members=group.value("members").toArray();members.append(QJsonObject{{"id",double(node->mapId())},{"type",int(node->nodeType())},{"model",node->modelName()}});group.insert("members",members);grouped.insert(id,group);
        }
        if(!grouped.isEmpty() || (catalog.writable()&&!catalog.assemblyInstances(path).isEmpty())) {
            if(!catalog.writable())return refuse(tr("Нет каталога профиля для сохранения группировки MOB."));
            for(auto it=grouped.begin();it!=grouped.end();++it)groups.append(it.value());
            catalog.setAssemblyInstances(path,groups.isEmpty()?QJsonObject():QJsonObject{{"mobSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"groups",groups}});catalogChanged=true;
        }
    }
    if(catalogChanged && (catalog.stagedBytes().isEmpty() || !addFile(catalogPath,catalog.stagedBytes(),&catalogBefore)))
        return refuse(tr("Не удалось подготовить группировку MOB."));
    const QString markerPath=canvasNavigationMarkerPath(destination);
    const QByteArray previousMarker=fileBytes(markerPath);
    if(QFileInfo::exists(markerPath) && QJsonDocument::fromJson(previousMarker).object().value("schema").toString()!=staleSchema)
        return refuse(tr("Файл отметки навигации занят неизвестными данными: %1").arg(markerPath));
    const QByteArray marker=QJsonDocument(QJsonObject{{"schema",staleSchema},{"stale",true},
        {"reason","canvas-resize"},{"mprSha256",staged.sha256},
        {"offsetX",resize.oldTileOffsetX},{"offsetY",resize.oldTileOffsetY},
        {"sectorsX",int(resize.sectorCountX)},{"sectorsY",int(resize.sectorCountY)}}).toJson();
    if(!addFile(markerPath,marker,&previousMarker))return refuse(tr("Отметка навигации изменилась."));
    QStringList names;for(const auto& file:files)names<<file.path;
    if(QMessageBox::question(this,tr("Применить размер к текущей MPR"),
       tr("Имя и путь карты сохранятся. Перенос объектов MOB: %1; удаление: %2.\nГраф останется для сравнения и будет помечен устаревшим; пересчёт отдельной кнопкой.\n\n%3\n\nПосле проверенной записи с резервом начнётся новая история Undo/Redo. Применить?")
           .arg(objects).arg(removed).arg(names.join('\n')),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)
        return true;
    makeCurrent();privateMobs.clear();
    const auto committed=commitMapSaveBatch(files);
    if(!committed.success)return refuse(committed.error);
    cancelPendingAiGraphPreviewRegeneration();
    m_pUndoStack->clear();m_worldTerrainSession.clear();
    unloadMob("");unloadLand(false);loadLandscape(destination,false);
    if(!m_pLand->isMprLoad() || !ensureTerrainAutoCatalogReady())return refuse(tr("Файлы записаны; не удалось открыть новую базу MPR."));
    CMob* restoredActive=nullptr;
    for(const auto& path:loadedPaths) {
        QFileInfo source(path);if(!loadMob(source))return refuse(tr("Файлы записаны; откройте MOB вручную: %1").arg(path));
        for(CMob* mob:m_aMob)if(mob && fileKey(mob->filePath().absoluteFilePath())==fileKey(path)) {
            setMobVisible(mob,visibility.value(fileKey(path),true));
            if(fileKey(path)==fileKey(activePath))restoredActive=mob;
        }
    }
    if(restoredActive)changeCurrentMob(restoredActive);
    for(CMob* mob:m_aMob)
        if(fileKey(mob->filePath().absoluteFilePath())==fileKey(environmentPath))
            m_environmentMob=mob;
    refreshAiGraphOwnership();refreshMprSettingsDock();emit loadedMapStateChanged();
    emit updateMsg(tr("Размер текущей MPR применён. Граф устарел — нажмите «Обновить граф»."));
    return true;
}

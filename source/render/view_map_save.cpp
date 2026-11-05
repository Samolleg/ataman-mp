#include "view.h"
#include "landscape.h"
#include <QFile>
#include "res_archive_index.h"
#include "terrain_atlas_compact.h"
#include "settings.h"
#include "resourcemanager.h"
#include "mob.h"
#include "file_write_activity.h"

void CView::acceptMapCompanionsCheckpoint()
{
    // The first successful batch owns these exact companion bytes, even if a
    // subsequent atlas publication or renderer adoption fails. Notify once.
    auto accepted=std::move(m_mapCompanionsCheckpoint);
    m_mapCompanionsCheckpoint={};
    if(accepted)accepted();
}

bool CView::stageMapCompanion(const QString& path,const QByteArray& bytes,QString* error,const QByteArray* expected)
{
    SMapSaveFile item;item.path=path;item.after=bytes;item.existed=QFileInfo::exists(path);
    if(item.existed){QFile f(path);if(!f.open(QIODevice::ReadOnly)){if(error)*error=f.errorString();return false;}item.before=f.readAll();}
    if(expected && item.before!=*expected){if(error)*error=tr("Архив изменился во время подготовки: ")+path;return false;}
    m_mapCompanions.append(item);return true;
}
bool CView::commitMapCohortMpr(const QFileInfo& destination,const QByteArray& bytes,QString* error)
{
    SMapSaveFile mpr;mpr.path=destination.absoluteFilePath();mpr.before=m_mprDocument.originalBytes();mpr.after=bytes;
    // Cohort save is only used for the current map, never Save As.
    if(destination.absoluteFilePath().compare(m_pLand->filePath().absoluteFilePath(),Qt::CaseInsensitive)!=0){
        if(error)*error=tr("Общее сохранение не может менять путь текущей карты.");return false;}
    auto files=m_mapCompanions;files.append(mpr);
    const auto result=commitMapSaveBatch(files,m_mapCohortBackupRoot,-1,[this](){acceptMapCompanionsCheckpoint();});if(error)*error=result.error;
    m_mapCohortCommitted=result.success;return result.success;
}
bool CView::saveMapCompanions(const QVector<SMapSaveFile>& files,QString* error,const QString& backupRoot,const std::function<void()>& committed,bool forceTerrain)
{
    if(m_mapCohortActive){if(error)*error=tr("Сохранение уже выполняется.");return false;}
    finishSculptStroke();endTileBrushGroup();syncWorldTerrainActive();
    m_mapCohortActive=true;m_mapCohortCommitted=false;m_mapCompanions=files;m_mapCohortBackupRoot=backupRoot;
    m_mapCompanionsCheckpoint=committed;
    if((forceTerrain&&m_pLand->isMprLoad()) || m_pLand->isDirty() || !m_worldTerrainSession.dirtyPaths().isEmpty())saveLand();
    else {
        const auto result=commitMapSaveBatch(files,m_mapCohortBackupRoot,-1,[this](){acceptMapCompanionsCheckpoint();});m_mapCohortCommitted=result.success;
        if(error)*error=result.error;
    }
    const bool saved=m_mapCohortCommitted;m_mapCohortActive=false;m_mapCompanions.clear();m_mapCohortBackupRoot.clear();
    m_mapCompanionsCheckpoint={};
    if(!saved && error && error->isEmpty())*error=tr("Общее сохранение карты не завершено.");
    return saved;
}

bool CView::stageMapCompanionFile(const QString& path,const QString& candidate,const QByteArray& expected,QString* error){
    SMapSaveFile item;item.path=path;item.afterFile=candidate;item.beforeSha256=expected;
    item.afterSha256=resFileSha256(candidate,error);item.existed=QFileInfo::exists(path);
    if(item.afterSha256.isEmpty()||resFileSha256(path,error)!=expected)return false;
    m_mapCompanions.append(item);return true;
}

namespace {
using MobProgress=std::function<void(CMob*,const QString&,int,int)>;
struct PreparedMobs { QVector<SMapSaveFile> files;QVector<QPair<CMob*,QByteArray>> accepted; };
bool prepareMobs(const QList<CMob*>& mobs,const QFileInfo& singleDestination,PreparedMobs* prepared,QString* error,const MobProgress& progress={})
{
    QMap<QString,ei::TechnicalModelCatalog> catalogs;
    for(int index=0;index<mobs.size();++index){auto* mob=mobs[index];if(!mob)continue;
        const auto destination=mobs.size()==1&&!singleDestination.filePath().isEmpty()?singleDestination:mob->filePath();
        const auto update=[&](const QString& label,int value,int maximum){Q_UNUSED(maximum);if(progress)progress(mob,label,index*5+value,mobs.size()*5);};
        QByteArray bytes;if(!mob->prepareSaveBytes(destination,&bytes,update)){if(error)*error=mob->lastSaveError();return false;}
        SMapSaveFile file;file.path=destination.absoluteFilePath();file.after=bytes;file.existed=QFileInfo::exists(file.path);
        if(QFileInfo(file.path).absoluteFilePath().compare(mob->filePath().absoluteFilePath(),Qt::CaseInsensitive)==0)file.before=mob->originalData();
        else if(file.existed){QFile source(file.path);if(!source.open(QIODevice::ReadOnly)){if(error)*error=source.errorString();return false;}file.before=source.readAll();}
        if(!mob->prepareAssemblySave(bytes,&catalogs,error,file.path))return false;
        prepared->files.append(file);prepared->accepted.append(qMakePair(mob,bytes));
    }
    return CMob::appendAssemblySaveFiles(catalogs,&prepared->files,error);
}
void acceptMobs(const PreparedMobs& prepared){for(const auto& mob:prepared.accepted){mob.first->adoptCommittedBytes(mob.second);mob.first->acceptAssemblySave();}}
}
bool CView::saveMobCohort(const QList<CMob*>& mobs,const QFileInfo& destination,QString* error,const std::function<void(CMob*,const QString&,int,int)>& progress)
{
    CFileWriteActivity writing;PreparedMobs prepared;
    if(!prepareMobs(mobs,destination,&prepared,error,progress))return false;
    if(progress&&!mobs.isEmpty())progress(mobs.last(),"Creating the pre-save backup...",qMax(0,mobs.size()*5-3),mobs.size()*5);
    if(progress&&!mobs.isEmpty())progress(mobs.last(),"Atomically replacing the destination...",qMax(0,mobs.size()*5-2),mobs.size()*5);
    const auto result=commitMapSaveBatch(prepared.files,QString(),-1,[&](){
        if(progress&&!mobs.isEmpty())progress(mobs.last(),"Rebinding saved MOB provenance...",mobs.size()*5-1,mobs.size()*5);
        acceptMobs(prepared);
    });
    if(error)*error=result.error;
    if(result.success&&progress&&!mobs.isEmpty())progress(mobs.last(),"MOB save complete.",mobs.size()*5,mobs.size()*5);
    return result.success;
}
QStringList CView::terrainAtlasInventoryDirectories(const QString& map) const
{
    QStringList roots;if(auto* option=dynamic_cast<COptStringList*>(m_pSettings->opt(eOptSetResource,"mapRoots")))roots=option->value().toList();
    return terrainAtlasMapDirectories(map,CTextureList::getInstance()->configuredResourceArchivePaths(),roots);
}
bool CView::saveMapFamily(QString* error,const QString& backupRoot,const QVector<SMapSaveFile>& companions,const std::function<void()>& committed)
{
    CFileWriteActivity writing;
    if(m_pLand->isMprLoad()&&!ensureTerrainAutoCatalogReady()){if(error)*error=tr("Подготовка карты ещё выполняется; повторите сохранение после её завершения.");return false;}
    // Only the MCP Save command selects the full dirty MOB family.
    finishSculptStroke();endTileBrushGroup();syncWorldTerrainActive();
    QList<CMob*> dirty;for(auto* mob:m_aMob)if(mob&&mob->isDirty())dirty.append(mob);
    PreparedMobs prepared;if(!prepareMobs(dirty,{},&prepared,error))return false;
    auto files=companions;files+=prepared.files;
    return saveMapCompanions(files,error,backupRoot,[&](){if(committed)committed();acceptMobs(prepared);},true);
}

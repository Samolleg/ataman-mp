#include "view.h"
#include "landscape.h"
#include <QFile>

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
bool CView::saveMapCompanions(const QVector<SMapSaveFile>& files,QString* error,const QString& backupRoot,const std::function<void()>& committed)
{
    if(m_mapCohortActive){if(error)*error=tr("Сохранение уже выполняется.");return false;}
    finishSculptStroke();endTileBrushGroup();syncWorldTerrainActive();
    m_mapCohortActive=true;m_mapCohortCommitted=false;m_mapCompanions=files;m_mapCohortBackupRoot=backupRoot;
    m_mapCompanionsCheckpoint=committed;
    if(m_pLand->isDirty() || !m_worldTerrainSession.dirtyPaths().isEmpty())saveLand();
    else {
        const auto result=commitMapSaveBatch(files,m_mapCohortBackupRoot,-1,[this](){acceptMapCompanionsCheckpoint();});m_mapCohortCommitted=result.success;
        if(error)*error=result.error;
    }
    const bool saved=m_mapCohortCommitted;m_mapCohortActive=false;m_mapCompanions.clear();m_mapCohortBackupRoot.clear();
    m_mapCompanionsCheckpoint={};
    if(!saved && error && error->isEmpty())*error=tr("Общее сохранение карты не завершено.");
    return saved;
}

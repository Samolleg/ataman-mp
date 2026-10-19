#include "view.h"
#include "terrain_atlas_compact.h"
#include "terrain_semantic_field_store.h"
#include "terrain_atlas_archive_binding.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "resourcemanager.h"
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QtConcurrent>
#include <QCryptographicHash>

void CView::compactTerrainAtlasRequested()
{
    const auto warn=[&](const QString& s){QMessageBox::warning(this,tr("Сжать атлас карты"),s);};
    if(!m_pLand||!m_pLand->isMprLoad()){warn(tr("Сначала откройте карту."));return;}
    finishSculptStroke();endTileBrushGroup();
    if(m_terrainEdit.isDirty()||mprParametersDirty()||hasDirtyWorldTerrain()){
        warn(tr("Сначала сохраните изменения карты. Сжатие перепаковывает уже опубликованный атлас и начинает новую историю редактирования."));return;}
    const auto source=m_mprDocument;const auto profile=m_terrainAtlasProfile;
    const QFileInfo map=m_pLand->filePath();const QString archivePath=m_activeTerrainArchivePath;
    if(archivePath.isEmpty()){warn(tr("Не найден опубликованный атлас карты."));return;}
    const auto ns=source.mapInfo().innerNamespace;
    QStringList names;for(int i=0;i<int(source.mapInfo().textureCount);++i)names<<QString("%1%2.mmp").arg(ns).arg(i,3,10,QLatin1Char('0'));
    names<<STerrainSemanticFieldDocument::memberName(ns,map)<<CTerrainAtlasProfileStore::embeddedMemberName(ns);
    QMap<QString,QByteArray> payloads;QString error;
    if(!CTerrainAtlasArchiveBindingResolver::readArchiveMembers(archivePath,names,&payloads,nullptr,&error)){
        warn(error);return;}
    if(payloads.value(CTerrainAtlasProfileStore::embeddedMemberName(ns))!=profile.toBytes()){
        warn(tr("Профиль атласа отличается от опубликованного. Сначала сохраните карту."));return;}
    QVector<QByteArray> pages;for(int i=0;i<int(source.mapInfo().textureCount);++i)pages<<payloads.value(names[i]);
    QByteArray semantic=source.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
    if(semantic.isEmpty())semantic=payloads.value(STerrainSemanticFieldDocument::memberName(ns,map));
    QStringList dirs{map.absolutePath()};
    // In the standard mod layout the Base Maps inventory is mandatory even
    // when only the mod's HDPack appears in the configured texture archives.
    QDir modRoot=map.dir();modRoot.cdUp();QDir modsRoot=modRoot;modsRoot.cdUp();
    if(modsRoot.dirName().compare(QStringLiteral("MODS"),Qt::CaseInsensitive)==0){
        modsRoot.cdUp();dirs<<modsRoot.filePath(QStringLiteral("Maps"));}
    for(const auto& path:CTextureList::getInstance()->configuredResourceArchivePaths()){
        QDir d=QFileInfo(path).dir();for(int level=0;level<2;++level){const auto maps=d.filePath("Maps");if(QDir(maps).exists())dirs<<QFileInfo(maps).absoluteFilePath();d.cdUp();}}
    dirs.removeDuplicates();
    std::atomic_bool cancelled(false);STerrainAtlasCompactPlan plan;
    {
        QProgressDialog progress(tr("Подготовка компактного атласа…"),tr("Отмена"),0,0,this);
        progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);progress.show();
        const auto connection=connect(&progress,&QProgressDialog::canceled,[&](){cancelled.store(true);});
        QFutureWatcher<STerrainAtlasCompactPlan> watcher;QEventLoop loop;
        connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&](){
            auto prepared=prepareTerrainAtlasCompact(source,profile,pages,semantic,map,&cancelled);
            if(prepared.error.isEmpty()&&!prepared.noOp&&!cancelled.load())
                estimateTerrainCompactPublication(&prepared,profile,map.absoluteFilePath(),archivePath,dirs);
            return prepared;
        }));
        loop.exec();plan=watcher.result();disconnect(connection);progress.close();
    }
    if(cancelled.load())return;if(!plan.error.isEmpty()){warn(plan.error);return;}
    if(plan.noOp){QMessageBox::information(this,tr("Сжать атлас карты"),tr("Атлас уже упакован. Файлы не изменены."));return;}
    const QString text=tr("Страниц: %1 → %2\nСлотов MP: %3 → %4 (нужных: %5)\nDXT1 с mip: %6 → %7 МиБ\nRGBA базовых уровней: %8 → %9 МиБ\nHDPack, оценка: %10 → %11 МиБ (изменение %12 МиБ).\n\nУдаление прежнего namespace зависит от повторной проверки ссылок всех карт. Если инвентарь изменится, старый атлас останется; новый добавит около %7 МиБ. Общий/исходный namespace сохраняется.\n\nКарта и атлас будут сохранены с резервными копиями. История редактирования начнётся заново. Продолжить?")
        .arg(plan.oldPages).arg(plan.pages.size()).arg(plan.oldSlots).arg(plan.pages.size()*64).arg(plan.retainedSlots)
        .arg(plan.oldDxt/1048576.0,0,'f',2).arg(plan.newDxt/1048576.0,0,'f',2)
        .arg(plan.oldRgba/1048576.0,0,'f',2).arg(plan.newRgba/1048576.0,0,'f',2)
        .arg(plan.archiveBefore/1048576.0,0,'f',2).arg(plan.archiveEstimatedAfter/1048576.0,0,'f',2)
        .arg((plan.archiveEstimatedAfter-plan.archiveBefore)/1048576.0,0,'f',2);
    if(QMessageBox::question(this,tr("Сжать атлас карты"),text,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    bool ready=true;emit terrainPublicationPreflightRequested(archivePath,&ready);if(!ready)return;
    QProgressDialog progress(tr("Резервное копирование и публикация компактного атласа…"),QString(),0,0,this);
    progress.setWindowModality(Qt::ApplicationModal);progress.setCancelButton(nullptr);progress.setMinimumDuration(0);progress.show();
    QFutureWatcher<STerrainAtlasCompactCommit> watcher;QEventLoop loop;
    connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([&](){return commitTerrainAtlasCompact(source,profile,plan,map.absoluteFilePath(),archivePath,dirs);}));
    loop.exec(QEventLoop::ExcludeUserInputEvents);const auto result=watcher.result();progress.close();
    if(!result.success){emit terrainPublicationFinished(archivePath,true);warn(result.error);return;}
    const QString digest=QString::fromLatin1(QCryptographicHash::hash(plan.mprBytes,QCryptographicHash::Sha256).toHex());
    const bool adopted=adoptCompleteTerrainBaseline(map,digest,plan.images,archivePath,plan.profile,!semantic.isEmpty());
    m_worldTerrainSession.clearHistory();m_worldTerrainSession.documents.remove(CWorldTerrainSession::key(map.absoluteFilePath()));
    m_worldTerrainAtlasSources.remove(CWorldTerrainSession::key(map.absoluteFilePath()));updateWorldTerrainStatus();
    emit terrainPublicationFinished(archivePath,true);
    if(!adopted){warn(tr("Карта и атлас сохранены. Для продолжения редактирования откройте карту заново."));return;}
    QMessageBox::information(this,tr("Сжать атлас карты"),tr("Готово: %1 → %2 страниц. HDPack: %3 → %4 МиБ.\n%5")
        .arg(plan.oldPages).arg(plan.pages.size()).arg(result.archiveBefore/1048576.0,0,'f',2).arg(result.archiveAfter/1048576.0,0,'f',2).arg(result.cleanup));
}

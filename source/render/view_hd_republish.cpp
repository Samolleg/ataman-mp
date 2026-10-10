#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "terrain_hd_republish.h"
#include "terrain_semantic_field_store.h"
#include "res_save_coordinator.h"
#include "staged_save_validator.h"
#include <QFile>
#include <QSaveFile>
#include <QMessageBox>
#include <QProgressDialog>
#include <QCryptographicHash>
#include <QApplication>
#include <QRegularExpression>
#include <QtConcurrent>

void CView::republishTerrainAtlasHd()
{
    const auto option=m_pSettings?dynamic_cast<const COptString*>(m_pSettings->opt("resourceProfile")):nullptr;
    if(!option||option->value()!=QStringLiteral("deja-vu")||!m_pLand||!m_pLand->isMprLoad()) {
        QMessageBox::information(this,tr("HD атлас"),tr("Пересборка доступна для опубликованной карты профиля Dejavu."));return;
    }
    if(m_pLand->isDirty()||mprParametersDirty()||persistedModernDraftRequiresPublication()) {
        QMessageBox::information(this,tr("HD атлас"),tr("Сначала сохраните текущие изменения карты и её опубликованный атлас."));return;
    }
    if(!ensureTerrainAutoCatalogReady()||!m_terrainAtlasProfile.approved)return;
    const QString archivePath=m_activeTerrainArchivePath;
    if(!QRegularExpression(QStringLiteral("_[0-9a-f]{12}$")).match(m_terrainAtlasProfile.mprNamespace).hasMatch()
       ||QFileInfo(archivePath).fileName().compare(QStringLiteral("HDPack_Terrain.res"),Qt::CaseInsensitive)!=0) {
        QMessageBox::information(this,tr("HD атлас"),tr("Команда работает только с опубликованным атласом Dejavu в HDPack_Terrain.res. Легаси-атласы не изменяются."));return;
    }
    QProgressDialog progress(tr("Проверка точных HD-источников и рецептов переходов…"),QString(),0,0,this);
    progress.setWindowModality(Qt::ApplicationModal);progress.setCancelButton(nullptr);progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const STerrainAtlasProfile profile=m_terrainAtlasProfile;
    auto future=QtConcurrent::run([archivePath,profile]() {
        STerrainHdRepublishResult result;CTerrainHdSources sources;QString error;
        if(!sources.open({archivePath},&error)){result.error=error;return result;}
        CResArchiveIndex archive;if(!archive.open(archivePath,&error)){result.error=error;return result;}
        QVector<QByteArray> pages;
        for(int page=0;page<profile.textureCount;++page)pages.append(archive.readMember(QStringLiteral("%1%2.mmp").arg(profile.mprNamespace.toLower()).arg(page,3,10,QLatin1Char('0')),&error));
        return rebuildTerrainAtlasHd(profile,pages,sources);
    });
    QFutureWatcher<STerrainHdRepublishResult> watcher;QEventLoop loop;
    connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);watcher.setFuture(future);loop.exec(QEventLoop::ExcludeUserInputEvents);
    auto result=future.result();progress.close();
    if(!result.success){QMessageBox::critical(this,tr("HD пересборка отказана"),result.error);return;}
    const int hd=result.receipt["hdApplied"].toInt(),generated=result.receipt["regeneratedApplied"].toInt();
    if(hd+generated==0) {
        QMessageBox::information(this,tr("HD атлас"),tr("Нет страниц с полным подтверждённым HD-происхождением. Старые 128 px страницы сохранены."));return;
    }
    QByteArray semantic=m_mprDocument.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
    if(semantic.isEmpty()) {
        CResArchiveIndex archive;QString error;
        if(archive.open(archivePath,&error))semantic=archive.readMember(STerrainSemanticFieldDocument::memberName(profile.mprNamespace,m_pLand->filePath()));
    }
    if(!prepareTerrainHdPublication(m_mprDocument,semantic,&result)) {
        QMessageBox::critical(this,tr("HD пересборка отказана"),result.error);return;
    }
    const auto answer=QMessageBox::question(this,tr("Пересобрать атлас карты в HD"),
        tr("Из HD: %1; переходов 256 px: %2; сохранено 128 px: %3.\n\nБудут опубликованы новая идентичность атласа в HDPack_Terrain.res и текущая MPR с резервными копиями. Слоты, повороты, классы и вода сохраняются.\n\nПродолжить?")
            .arg(hd).arg(generated).arg(result.receipt["retained128"].toInt()));
    if(answer!=QMessageBox::Yes)return;
    bool ready=true;emit terrainPublicationPreflightRequested(archivePath,&ready);if(!ready)return;
    QFile archiveFile(archivePath);CResArchiveDocument baseline;QStringList errors;
    if(!archiveFile.open(QIODevice::ReadOnly)||!baseline.loadBytes(archiveFile.readAll(),archivePath,&errors)) {
        emit terrainPublicationFinished(archivePath,false);QMessageBox::critical(this,tr("HD пересборка отказана"),errors.join('\n'));return;
    }
    archiveFile.close();
    SResPublishedMembersSaveRequest request;request.source=QFileInfo(archivePath);request.destination=request.source;request.published=result.publishedMembers;request.createBackup=true;
    const auto committed=commitStagedResPublishedMembers(baseline,request);
    if(!committed.success) {
        emit terrainPublicationFinished(archivePath,committed.committed);QMessageBox::critical(this,tr("HD публикация не завершена"),committed.error);return;
    }
    SMprBytesSaveCommitRequest mapRequest;mapRequest.source=m_pLand->filePath();mapRequest.destination=mapRequest.source;
    mapRequest.expectedSourceBytes=m_mprDocument.originalBytes();mapRequest.candidateBytes=result.mprBytes;mapRequest.createBackup=true;
    const auto saved=commitValidatedMprBytes(mapRequest);
    if(!saved.success) {
        QFile current(archivePath);bool restored=false;
        if(current.open(QIODevice::ReadOnly)) {
            const QByteArray now=current.readAll();current.close();
            if(QString::fromLatin1(QCryptographicHash::hash(now,QCryptographicHash::Sha256).toHex())==committed.candidateSha256) {
                QSaveFile rollback(archivePath);const auto old=baseline.originalBytes();
                restored=rollback.open(QIODevice::WriteOnly)&&rollback.write(old)==old.size()&&rollback.commit();
            }
        }
        emit terrainPublicationFinished(archivePath,true);
        QMessageBox::critical(this,tr("HD публикация не завершена"),saved.error+tr("\nОткат RES: %1").arg(restored?tr("выполнен"):tr("нужна ручная проверка")));return;
    }
    const bool adopted=adoptCompleteTerrainBaseline(mapRequest.destination,saved.candidateSha256,result.after,archivePath,result.profile,!semantic.isEmpty());
    emit terrainPublicationFinished(archivePath,true);
    if(!adopted){QMessageBox::warning(this,tr("HD атлас сохранён"),tr("Пара MPR/RES сохранена. Откройте карту заново для продолжения редактирования."));return;}
    emit mprSaveStateChanged();emit updateMsg(tr("Атлас карты пересобран в HD: %1 слотов, %2 переходов.").arg(hd).arg(generated));
}

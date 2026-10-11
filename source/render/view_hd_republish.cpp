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
#include <QJsonArray>
#include <QtConcurrent>

void CView::republishTerrainAtlasHd()
{
    const auto option=m_pSettings?dynamic_cast<const COptString*>(m_pSettings->opt("resourceProfile")):nullptr;
    if(!option||option->value()!=QStringLiteral("deja-vu")||!m_pLand||!m_pLand->isMprLoad()) {
        QMessageBox::information(this,tr("HD атлас"),tr("Пересборка доступна для опубликованной карты профиля Dejavu."));return;
    }
    if(m_pLand->isDirty()||mprParametersDirty()) {
        QMessageBox::information(this,tr("HD атлас"),tr("Сначала сохраните текущие изменения карты и её опубликованный атлас."));return;
    }
    if(!ensureTerrainAutoCatalogReady()||!m_terrainAtlasProfile.approved)return;
    const QString archivePath=m_activeTerrainArchivePath;
    if(!QRegularExpression(QStringLiteral("_[0-9a-f]{12}$")).match(m_terrainAtlasProfile.mprNamespace).hasMatch()
       ||QFileInfo(archivePath).fileName().compare(QStringLiteral("HDPack_Terrain.res"),Qt::CaseInsensitive)!=0) {
        QMessageBox::information(this,tr("HD атлас"),tr("Команда работает только с опубликованным атласом Dejavu в HDPack_Terrain.res. Легаси-атласы не изменяются."));return;
    }
    QProgressDialog progress(tr("Подбор HD-источников и восстановление рецептов переходов…"),QString(),0,0,this);
    progress.setWindowModality(Qt::ApplicationModal);progress.setCancelButton(nullptr);progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const STerrainAtlasProfile profile=m_terrainAtlasProfile;
    QStringList sourcePaths{archivePath};
    const QDir modDirectory=QFileInfo(archivePath).dir().filePath("..");
    if(QFileInfo(modDirectory.absolutePath()).fileName().compare(QStringLiteral("Dejavu"),Qt::CaseInsensitive)==0) {
        const QString fallback=modDirectory.filePath("../JamaisVu135/res/HDPack_Terrain.res");
        if(QFileInfo::exists(fallback))sourcePaths.append(QFileInfo(fallback).absoluteFilePath());
    }
    const auto animations=m_mprDocument.mapInfo().animatedTiles;
    auto future=QtConcurrent::run([archivePath,sourcePaths,profile,animations]() {
        STerrainHdRepublishResult result;CTerrainHdSources sources;QString error;
        if(!sources.open(sourcePaths,&error)){result.error=error;return result;}
        CResArchiveIndex archive;if(!archive.open(archivePath,&error)){result.error=error;return result;}
        // A portable working profile can already be published in HDPack.
        // Accept only the exact saved profile, never unsaved palette metadata.
        STerrainAtlasProfile savedProfile;
        if(!STerrainAtlasProfile::fromBytes(archive.readMember(CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace)),&savedProfile)
           ||savedProfile.toBytes()!=profile.toBytes()) {
            result.error=QStringLiteral("Профиль атласа отличается от сохранённого в HDPack_Terrain.res. Сначала сохраните изменения.");return result;
        }
        QVector<QByteArray> pages;
        for(int page=0;page<profile.textureCount;++page)pages.append(archive.readMember(QStringLiteral("%1%2.mmp").arg(profile.mprNamespace.toLower()).arg(page,3,10,QLatin1Char('0')),&error));
        const auto previous=archive.readMember("ei_atlas_hd_recovery_"+profile.mprNamespace+".json");
        return rebuildTerrainAtlasHd(profile,pages,sources,true,animations,previous);
    });
    QFutureWatcher<STerrainHdRepublishResult> watcher;QEventLoop loop;
    connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);watcher.setFuture(future);loop.exec(QEventLoop::ExcludeUserInputEvents);
    auto result=future.result();progress.close();
    if(!result.success){QMessageBox::critical(this,tr("HD пересборка отказана"),result.error);return;}
    const int hd=result.receipt["hdApplied"].toInt(),generated=result.receipt["regeneratedApplied"].toInt();
    if(hd+generated==0) {
        QMessageBox::information(this,tr("HD атлас"),tr("Новых подтверждённых HD-источников или рецептов нет. Текущие страницы сохранены."));return;
    }
    QByteArray semantic=m_mprDocument.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
    if(semantic.isEmpty()) {
        CResArchiveIndex archive;QString error;
        if(archive.open(archivePath,&error))semantic=archive.readMember(STerrainSemanticFieldDocument::memberName(profile.mprNamespace,m_pLand->filePath()));
    }
    if(!prepareTerrainHdPublication(m_mprDocument,semantic,&result)) {
        QMessageBox::critical(this,tr("HD пересборка отказана"),result.error);return;
    }
    QFile archiveFile(archivePath);CResArchiveDocument baseline;QStringList errors;
    if(!archiveFile.open(QIODevice::ReadOnly)||!baseline.loadBytes(archiveFile.readAll(),archivePath,&errors)) {
        QMessageBox::critical(this,tr("HD пересборка отказана"),errors.join('\n'));return;
    }
    archiveFile.close();
    const auto cleanupReason=[this](const QJsonObject& detail) {
        const QString reason=detail["reason"].toString();
        if(reason=="shared-namespace")return tr("на него ссылаются другие карты: %1").arg(detail["references"].toArray().size());
        if(reason=="stock-or-unverified-original-atlas")return tr("исходный атлас или неподтверждённое происхождение");
        if(reason=="same-namespace")return tr("карта продолжает использовать этот атлас");
        if(reason=="current-map-changed")return tr("карта изменилась во время подготовки");
        if(reason=="current-map-outside-profile-inventory")return tr("текущая карта находится вне проверяемых папок профиля");
        if(reason.contains(':'))return tr("не удалось проверить все MPR Dejavu/Base: %1").arg(reason.mid(reason.indexOf(':')+1).trimmed());
        return tr("не удалось проверить все MPR Dejavu/Base");
    };
    const QStringList mapRoots{modDirectory.filePath("Maps"),modDirectory.filePath("../../Base/Maps")};
    QByteArray publishedBytes,estimatedBytes;CResArchiveDocument published;QJsonObject cleanup;QString cleanupError;
    if(!baseline.buildWithPublishedMembers(result.publishedMembers,false,&publishedBytes,&errors)
       ||!published.loadBytes(publishedBytes)
       ||!buildTerrainHdCleanup(published,profile.mprNamespace,result.profile.mprNamespace,m_pLand->filePath().absoluteFilePath(),mapRoots,&estimatedBytes,&cleanup,&cleanupError)) {
        QMessageBox::critical(this,tr("HD пересборка отказана"),errors.join('\n')+cleanupError);return;
    }
    const double expectedGrowth=(estimatedBytes.size()-baseline.originalBytes().size())/1048576.0;
    const double expectedTotal=estimatedBytes.size()/1048576.0;
    // The size estimate has served its purpose. Do not retain multiple full
    // RES copies while the owner considers the dialog or during publication.
    publishedBytes.clear();estimatedBytes.clear();published=CResArchiveDocument();
    const int blockedAnimation=result.receipt["animationBlockedHd"].toInt();
    const QString animationNotice=blockedAnimation>0
        ?tr("\nКадров с HD-источником оставлено 128 px: %1. В их анимации есть SD-кадры; непрерывный порядок кадров сохранён.").arg(blockedAnimation):QString();
    const auto answer=QMessageBox::question(this,tr("Пересобрать атлас карты в HD"),
        tr("Из HD: %1; перерисовано переходов: %2; останется 128 px: %3.\nОжидаемый прирост HDPack_Terrain.res: %4 МиБ; итог: %5 МиБ.\nСтарый атлас: %6.%7\n\nСсылки ячеек, классы, вода и анимация сохраняются при упаковке страниц. Будут созданы резервные копии.\n\nПродолжить?")
        .arg(hd).arg(generated).arg(result.receipt["native128Slots"].toInt())
        .arg(expectedGrowth,0,'f',2)
        .arg(expectedTotal,0,'f',2)
        .arg(cleanup["removedMembers"].toInt()?tr("будет удалён после сохранения карты"):tr("сохранён (%1)").arg(cleanupReason(cleanup))).arg(animationNotice),
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
    if(answer!=QMessageBox::Yes)return;
    bool ready=true;emit terrainPublicationPreflightRequested(archivePath,&ready);if(!ready)return;
    // The archive baseline used by the confirmation is also the commit guard.
    const auto hashes=result.receipt["sourcePageSha256"].toArray();
    bool unchanged=hashes.size()==profile.textureCount;
    for(int page=0;page<hashes.size();++page) {
        const QString member=QStringLiteral("%1%2.mmp").arg(profile.mprNamespace.toLower()).arg(page,3,10,QLatin1Char('0'));
        unchanged=unchanged&&QString::fromLatin1(QCryptographicHash::hash(baseline.memberPayload(member),QCryptographicHash::Sha256).toHex())==hashes[page].toString();
    }
    if(!unchanged) {
        emit terrainPublicationFinished(archivePath,false);
        QMessageBox::critical(this,tr("HD пересборка отказана"),tr("Исходные страницы атласа изменились во время анализа. Повторите команду на актуальной карте."));return;
    }
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
    // Only a committed new MPR may make its previous atlas an orphan.
    // Cleanup failure retains a valid pair and reports the recoverable extra data.
    QString cleanupNotice;
    QFile currentArchive(archivePath);QByteArray currentBytes;
    if(currentArchive.open(QIODevice::ReadOnly))currentBytes=currentArchive.readAll();
    currentArchive.close();
    CResArchiveDocument current;QByteArray compacted;QJsonObject finalCleanup;
    if(QString::fromLatin1(QCryptographicHash::hash(currentBytes,QCryptographicHash::Sha256).toHex())!=committed.candidateSha256
       ||!current.loadBytes(currentBytes)
       ||!buildTerrainHdCleanup(current,profile.mprNamespace,result.profile.mprNamespace,mapRequest.destination.absoluteFilePath(),mapRoots,&compacted,&finalCleanup,&cleanupError))
        cleanupNotice=tr("Старый атлас сохранён: пакет или список карт изменился. %1").arg(cleanupError);
    else if(finalCleanup["removedMembers"].toInt()) {
        if(!commitTerrainHdCleanup(archivePath,currentBytes,compacted,finalCleanup,&cleanupError))
            cleanupNotice=tr("Карта сохранена; очистка старого атласа не завершена: %1").arg(cleanupError);
    } else cleanupNotice=tr("Старый атлас сохранён: %1").arg(cleanupReason(finalCleanup));
    baseline=CResArchiveDocument();current=CResArchiveDocument();currentBytes.clear();compacted.clear();
    if(!cleanupNotice.isEmpty())QMessageBox::information(this,tr("HD атлас"),cleanupNotice);
    const bool adopted=adoptCompleteTerrainBaseline(mapRequest.destination,saved.candidateSha256,result.after,archivePath,result.profile,!semantic.isEmpty());
    emit terrainPublicationFinished(archivePath,true);
    if(!adopted){QMessageBox::warning(this,tr("HD атлас сохранён"),tr("Пара MPR/RES сохранена. Откройте карту заново для продолжения редактирования."));return;}
    emit mprSaveStateChanged();emit updateMsg(tr("Атлас карты пересобран в HD: %1 слотов, %2 переходов.").arg(hd).arg(generated));
}

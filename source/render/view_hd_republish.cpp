#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "log.h"
#include "terrain_hd_republish.h"
#include "terrain_semantic_field_store.h"
#include "res_save_coordinator.h"
#include "staged_save_validator.h"
#include <QFile>
#include <QSaveFile>
#include <QMessageBox>
#include "terrain_hd_progress_dialog.h"
#include <QCryptographicHash>
#include <QApplication>
#include <QRegularExpression>
#include <QJsonArray>
#include <QCheckBox>
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
    int loggedStage=-1,loggedPercent=-1;
    CTerrainHdProgress state([&](const CTerrainHdProgress::Snapshot& value) {
        if(int(value.stage)!=loggedStage || value.percent>=loggedPercent+5) {
            ei::log(eLogInfo,QString("HD publication preparation stage:%1 progress:%2 work:%3/%4")
                .arg(int(value.stage)).arg(value.percent).arg(value.done).arg(value.total));
            loggedStage=int(value.stage);loggedPercent=value.percent;
        }
    });
    CTerrainHdProgressDialog progress(state,this);progress.show();
    const STerrainAtlasProfile profile=m_terrainAtlasProfile;
    QStringList sourcePaths{archivePath};
    const QDir modDirectory=QFileInfo(archivePath).dir().filePath("..");
    if(QFileInfo(modDirectory.absolutePath()).fileName().compare(QStringLiteral("Dejavu"),Qt::CaseInsensitive)==0) {
        const QString fallback=modDirectory.filePath("../JamaisVu135/res/HDPack_Terrain.res");
        if(QFileInfo::exists(fallback))sourcePaths.append(QFileInfo(fallback).absoluteFilePath());
    }
    const auto map=m_mprDocument;
    const QString mapPath=m_pLand->filePath().absoluteFilePath();
    const QStringList mapRoots{modDirectory.filePath("Maps"),modDirectory.filePath("../../Base/Maps")};
    struct Prepared {
        STerrainHdRepublishResult result;
        QByteArray semantic,baselineSha;
        QJsonObject cleanup;
        double expectedGrowth{0},expectedTotal{0};
    };
    QSharedPointer<CTerrainHdSources> sources(new CTerrainHdSources);
    auto future=QtConcurrent::run([archivePath,sourcePaths,profile,map,mapPath,mapRoots,sources,&state]() {
        Prepared prepared;auto& result=prepared.result;QString error;
        auto cancelled=[&]() {if(!state.cancelled())return false;result.success=false;result.cancelled=true;result.error=QStringLiteral("Подготовка отменена. RES и MPR не изменены.");return true;};
        if(!sources->open(sourcePaths,&error,&state)){result.error=error;cancelled();return prepared;}
        CResArchiveIndex archive;if(!archive.open(archivePath,&error)){result.error=error;return prepared;}
        STerrainAtlasProfile savedProfile;
        if(!STerrainAtlasProfile::fromBytes(archive.readMember(CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace)),&savedProfile)
           ||savedProfile.toBytes()!=profile.toBytes()) {
            result.error=QStringLiteral("Профиль атласа отличается от сохранённого в HDPack_Terrain.res. Сначала сохраните изменения.");return prepared;
        }
        QVector<QByteArray> pages;
        for(int page=0;page<profile.textureCount;++page) {
            if(cancelled())return prepared;
            pages.append(archive.readMember(QStringLiteral("%1%2.mmp").arg(profile.mprNamespace.toLower()).arg(page,3,10,QLatin1Char('0')),&error));
        }
        const auto previous=archive.readMember("ei_atlas_hd_recovery_"+profile.mprNamespace+".json");
        prepared.semantic=map.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
        if(prepared.semantic.isEmpty())prepared.semantic=archive.readMember(STerrainSemanticFieldDocument::memberName(profile.mprNamespace,QFileInfo(mapPath)));
        result=rebuildTerrainAtlasHd(profile,pages,*sources,true,map.mapInfo().animatedTiles,previous,&state,&map,prepared.semantic);
        if(!result.success||cancelled())return prepared;
        if(!result.receipt["allStaticSlotsTrueHd"].toBool()) {
            result.success=false;
            result.error=QStringLiteral("Для части статических тайлов нет проверенного HD-источника или маски. Растянутые SD-текстуры не публикуются как HD. MPR и RES не изменены.");
            return prepared;
        }
        if(result.receipt["hdApplied"].toInt()+result.receipt["regeneratedApplied"].toInt()==0) {
            sources->saveCache();
            state.update(CTerrainHdProgress::Complete,1,1);return prepared;
        }
        if(!prepareTerrainHdPublication(map,prepared.semantic,&result,&state)){result.success=false;return prepared;}
        state.update(CTerrainHdProgress::Preparing,2,5);if(cancelled())return prepared;
        QFile file(archivePath);CResArchiveDocument baseline;QStringList errors;
        if(!file.open(QIODevice::ReadOnly)||!baseline.loadBytes(file.readAll(),archivePath,&errors)) {
            result.success=false;result.error=errors.join('\n')+file.errorString();return prepared;
        }
        file.close();
        const qint64 baselineSize=baseline.originalBytes().size();
        prepared.baselineSha=QCryptographicHash::hash(baseline.originalBytes(),QCryptographicHash::Sha256);
        state.update(CTerrainHdProgress::Preparing,3,5);if(cancelled())return prepared;
        QByteArray publishedBytes,estimatedBytes;
        if(!baseline.buildWithPublishedMembers(result.publishedMembers,false,&publishedBytes,&errors)) {
            result.success=false;result.error=errors.join('\n');return prepared;
        }
        // Release the old parsed archive before parsing its replacement. Each
        // document retains both original bytes and per-member payloads.
        baseline=CResArchiveDocument();
        state.update(CTerrainHdProgress::Preparing,4,5);if(cancelled())return prepared;
        CResArchiveDocument published;
        if(!published.loadBytes(publishedBytes)) {result.success=false;result.error=QStringLiteral("Не удалось проверить подготовленный RES");return prepared;}
        publishedBytes.clear();
        if(!buildTerrainHdCleanup(published,profile.mprNamespace,result.profile.mprNamespace,mapPath,mapRoots,&estimatedBytes,&prepared.cleanup,&error,&state)) {
            result.success=false;result.error=error;return prepared;
        }
        prepared.expectedGrowth=(estimatedBytes.size()-baselineSize)/1048576.0;
        prepared.expectedTotal=estimatedBytes.size()/1048576.0;
        if(cancelled())return prepared;
        sources->saveCache();
        state.update(CTerrainHdProgress::Complete,1,1);return prepared;
    });
    QFutureWatcher<Prepared> watcher;QEventLoop loop;
    connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);watcher.setFuture(future);
    if(!future.isFinished())loop.exec();
    auto prepared=future.result();progress.accept();auto result=prepared.result;
    if(state.cancelled()||result.cancelled) {QMessageBox::information(this,tr("HD атлас"),tr("Подготовка отменена. RES и MPR не изменены."));return;}
    if(!result.success){QMessageBox::critical(this,tr("HD пересборка отказана"),result.error);return;}
    const int hd=result.receipt["hdApplied"].toInt(),generated=result.receipt["regeneratedApplied"].toInt();
    if(hd+generated==0) {QMessageBox::information(this,tr("HD атлас"),tr("Новых подтверждённых HD-источников или рецептов нет. Текущие страницы сохранены."));return;}
    const auto semantic=prepared.semantic;
    const auto cleanup=prepared.cleanup;
    const double expectedGrowth=prepared.expectedGrowth,expectedTotal=prepared.expectedTotal;
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
    const int unchangedAnimationFrames=result.receipt["animationUnchangedFrames"].toInt();
    QString animationNotice;
    if(result.receipt["ownerConfirmedHdApplied"].toInt()>0)
        animationNotice+=tr("\nHD по подтверждённому происхождению: %1 слотов.").arg(result.receipt["ownerConfirmedHdApplied"].toInt());
    int repaintedTransitions=0;
    for(const auto& value:result.receipt["slots"].toArray())
        if(value.toObject()["result"].toString()==QStringLiteral("regenerated-256"))++repaintedTransitions;
    const int hdVariants=result.receipt["hdVariantsApplied"].toInt();
    const int enlarged=result.receipt["upscaled128To256"].toInt();
    QMessageBox confirmation(QMessageBox::Question,tr("Пересобрать атлас карты в HD"),
        tr("HD-оригинал: %1; HD-вариант: %2; перерисовано переходов: %3; увеличено 128→256: %4; анимации без изменений: %5 кадров.\nОжидаемый прирост HDPack_Terrain.res: %6 МиБ; итог: %7 МиБ.\nСтарый атлас: %8.%9\n\nСсылки ячеек, классы, вода и анимация сохраняются при упаковке страниц. Будут созданы резервные копии.\n\nПродолжить?")
        .arg(hd).arg(hdVariants).arg(repaintedTransitions).arg(enlarged).arg(unchangedAnimationFrames)
        .arg(expectedGrowth,0,'f',2)
        .arg(expectedTotal,0,'f',2)
        .arg(cleanup["removedMembers"].toInt()?tr("будет удалён после сохранения карты"):tr("сохранён (%1)").arg(cleanupReason(cleanup))).arg(animationNotice),
        QMessageBox::Yes|QMessageBox::No,this);
    confirmation.setDefaultButton(QMessageBox::No);
    QCheckBox distribute(tr("Разложить варианты по карте"),&confirmation);
    distribute.setObjectName(QStringLiteral("terrainHdDistributeVariants"));
    distribute.setChecked(false);
    distribute.setToolTip(tr("После перевода перемешать тайлы с новым случайным зерном. Раскладку можно отменить и повторить; сохраните карту для записи раскладки."));
    confirmation.setCheckBox(&distribute);
    if(confirmation.exec()!=QMessageBox::Yes)return;
    const bool distributeVariants=distribute.isChecked();
    // Preparation used a snapshot. Refuse a changed RES before requesting the
    // publication lease; the existing coordinator also checks the commit source.
    QFile archiveFile(archivePath);CResArchiveDocument baseline;QStringList errors;QString cleanupError;
    if(!archiveFile.open(QIODevice::ReadOnly)||!baseline.loadBytes(archiveFile.readAll(),archivePath,&errors)
       ||QCryptographicHash::hash(baseline.originalBytes(),QCryptographicHash::Sha256)!=prepared.baselineSha) {
        QMessageBox::critical(this,tr("HD пересборка отказана"),tr("RES изменился во время подготовки. Повторите команду."));return;
    }
    archiveFile.close();
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
    // Use the normal terrain transaction: one undo command with captured packed
    // values and a fresh seed. Saving persists that command through the usual
    // MPR/semantic-field path; the unchecked default keeps the existing workflow.
    QString completion=tr("Атлас карты пересобран в HD: %1 слотов, %2 переходов.").arg(hd).arg(generated);
    if(distributeVariants) {
        const bool hadSeed=m_terrainEdit.hasRandomPaintSeed();
        const quint32 previousSeed=m_terrainEdit.randomPaintSeed();
        QString detail;
        const auto observer=connect(this,&CView::updateMsg,this,[&detail](const QString& text){detail=text;});
        shuffleTerrainTiles(false);disconnect(observer);
        const bool applied=m_terrainEdit.hasRandomPaintSeed()
            &&(!hadSeed||m_terrainEdit.randomPaintSeed()!=previousSeed)&&m_pLand->isDirty();
        if(!applied) {
            const QString message=tr("HD-пара сохранена, но раскладка вариантов не применена. %1").arg(detail);
            emit updateMsg(message);QMessageBox::warning(this,tr("Раскладка вариантов"),message);
            emit mprSaveStateChanged();return;
        }
        completion+=tr(" Варианты разложены в рабочей карте; сохраните MPR для записи раскладки. Undo/Redo доступны.");
    }
    emit mprSaveStateChanged();emit updateMsg(completion);
}

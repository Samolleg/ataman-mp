#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "log.h"
#include "terrain_hd_republish.h"
#include "terrain_hd_library.h"
#include "terrain_hd_variant_bank.h"
#include "terrain_semantic_field_store.h"
#include "res_save_coordinator.h"
#include "staged_save_validator.h"
#include "resourcemanager.h"
#include "terrain_save_flow_policy.h"
#include <QFile>
#include <QSaveFile>
#include <QMessageBox>
#include "terrain_hd_progress_dialog.h"
#include <QCryptographicHash>
#include <QApplication>
#include <QJsonArray>
#include <QCheckBox>
#include <QtConcurrent>

void CView::republishTerrainAtlasHd()
{
    const auto option=m_pSettings?dynamic_cast<const COptString*>(m_pSettings->opt("resourceProfile")):nullptr;
    if(!option||option->value()!=QStringLiteral("deja-vu")||!m_pLand||!m_pLand->isMprLoad()) {
        QMessageBox::information(this,tr("HD атлас"),tr("Перевод в HD доступен для загруженной карты профиля Dejavu."));return;
    }
    finishSculptStroke();endTileBrushGroup();syncWorldTerrainActive();
    if(m_pLand->isDirty()||mprParametersDirty()||!m_worldTerrainSession.dirtyPaths().isEmpty()) {
        saveLand();
        if(m_pLand->isDirty()||mprParametersDirty()||!m_worldTerrainSession.dirtyPaths().isEmpty())return;
    }
    if(!ensureTerrainAutoCatalogReady()||!m_terrainAtlasProfile.approved)return;
    QStringList resourceRoots;
    if(auto* configured=dynamic_cast<COptStringList*>(m_pSettings->opt(eOptSetResource,"resourceRoots")))
        for(const auto& root:configured->value())resourceRoots.append(root);
    const auto choice=terrainPublicationResourceChoicePlan(resourceRoots,
        m_pLand->filePath().absoluteFilePath(),m_activeTerrainArchivePath,true,false);
    const QString archivePath=choice.initialPath;
    if(choice.requiresExplicitChoice || !terrainPublicationPathInResourceRoots(archivePath,resourceRoots)) {
        QMessageBox::warning(this,tr("HD атлас"),tr("В ресурсах текущего профиля не найден HDPack_Terrain.res для сохранения HD-атласа."));return;
    }
    // Pages may come from a stock archive, independently of the HD destination.
    // Capture the exact resolved payloads; rebuilding also binds their pixels
    // to the loaded profile, and they are read back before committing.
    QVector<QByteArray> authoredPages;QStringList authoredSources,pageNames;
    for(int page=0;page<m_terrainAtlasProfile.textureCount;++page) {
        const QString name=QStringLiteral("%1%2.mmp").arg(m_terrainAtlasProfile.mprNamespace.toLower()).arg(page,3,10,QLatin1Char('0'));
        QByteArray payload;QString source;
        if(!CTextureList::getInstance()->terrainMmpPayload(name,&payload,&source)) {
            QMessageBox::critical(this,tr("HD атлас"),tr("Не удалось прочитать исходную страницу %1.").arg(name));return;
        }
        authoredPages.append(payload);authoredSources.append(source);pageNames.append(name);
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
    QStringList sourcePaths=TerrainHdLibrary::configuredSources(CTextureList::getInstance()->configuredResourceArchivePaths());
    sourcePaths.append(authoredSources);sourcePaths.removeDuplicates();
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
    auto future=QtConcurrent::run([archivePath,sourcePaths,profile,map,mapPath,mapRoots,sources,authoredPages,&state]() {
        Prepared prepared;auto& result=prepared.result;QString error;
        auto cancelled=[&]() {if(!state.cancelled())return false;result.success=false;result.cancelled=true;result.error=QStringLiteral("Подготовка отменена. RES и MPR не изменены.");return true;};
        QStringList localSources;
        if(!TerrainHdLibrary::ensure(sourcePaths,&localSources,&error,&state)
           ||!sources->open(localSources,&error,&state,QDir(TerrainHdLibrary::root()).filePath("index"))){result.error=error;cancelled();return prepared;}
        // Donor indexing is reusable even when this particular map cannot be
        // converted. Publishing the game's output archive never invalidates it.
        if(!sources->saveCache()){result.error=QStringLiteral("Не удалось сохранить индекс HD-библиотеки.");cancelled();return prepared;}
        if(!TerrainHdVariantBank::migrateLegacy(localSources,&error)){result.error=error;return prepared;}
        CResArchiveIndex archive;if(!archive.open(archivePath,&error)){result.error=error;return prepared;}
        STerrainAtlasProfile savedProfile;
        const auto embeddedProfile=archive.readMember(CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace));
        if(!embeddedProfile.isEmpty() && (!STerrainAtlasProfile::fromBytes(embeddedProfile,&savedProfile)
           ||savedProfile.toBytes()!=profile.toBytes())) {
            result.error=QStringLiteral("Профиль атласа отличается от сохранённого в HDPack_Terrain.res. Сначала сохраните изменения.");return prepared;
        }
        const auto previous=archive.readMember("ei_atlas_hd_recovery_"+profile.mprNamespace+".json");
        prepared.semantic=map.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName());
        if(prepared.semantic.isEmpty())prepared.semantic=archive.readMember(STerrainSemanticFieldDocument::memberName(profile.mprNamespace,QFileInfo(mapPath)));
        result=rebuildTerrainAtlasHd(profile,authoredPages,*sources,true,map.mapInfo().animatedTiles,previous,&state,&map,prepared.semantic);
        if(!result.success||cancelled())return prepared;
        if(!result.receipt["allStaticSlotsTrueHd"].toBool()) {
            QJsonArray enlarged;
            for(const auto& value:result.receipt["slots"].toArray())
                if(value.toObject()["result"].toString()==QStringLiteral("upscaled-128-to-256"))enlarged.append(value);
            ei::log(eLogWarning,QStringLiteral("HD quality refusal enlarged:%1 previous:%2 rows:%3")
                .arg(result.receipt["upscaled128To256"].toInt())
                .arg(result.receipt["previouslyUpscaled256"].toInt())
                .arg(QString::fromUtf8(QJsonDocument(enlarged).toJson(QJsonDocument::Compact))));
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
    if(hd+generated==0) {QMessageBox::information(this,tr("HD атлас"),tr("Статические тайлы карты уже имеют размер 256×256. Повторная конвертация не требуется; анимация остаётся в исходном размере."));return;}
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
    CTerrainHdProgress completionState;
    CTerrainHdProgressDialog completionDialog(completionState,this);
    completionDialog.lockForCommit();
    const int completionSteps=distributeVariants?6:5;
    completionState.operation(tr("Проверка исходных файлов"),1,completionSteps);
    completionDialog.show();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    bool ready=true;emit terrainPublicationPreflightRequested(archivePath,&ready);if(!ready)return;
    SMprBytesSaveCommitRequest mapRequest;mapRequest.source=m_pLand->filePath();mapRequest.destination=mapRequest.source;
    mapRequest.expectedSourceBytes=m_mprDocument.originalBytes();mapRequest.candidateBytes=result.mprBytes;mapRequest.createBackup=true;
    struct Published {bool success{false},archiveTouched{false};QString error,cleanupNotice;SMprBytesSaveCommitResult saved;};
    const auto publicationHashes=result.receipt["sourcePageSha256"].toArray();
    auto publication=QtConcurrent::run([archivePath,profile,prepared,result,publicationHashes,authoredSources,pageNames,sources,mapRequest,mapRoots,completionSteps,&completionState]() {
    Published outcome;
    const auto& hashes=publicationHashes;
    // Preparation used a snapshot. Refuse a changed RES before requesting the
    // publication lease; the existing coordinator also checks the commit source.
    QFile archiveFile(archivePath);CResArchiveDocument baseline;QStringList errors;QString cleanupError;
    if(!archiveFile.open(QIODevice::ReadOnly)||!baseline.loadBytes(archiveFile.readAll(),archivePath,&errors)
       ||QCryptographicHash::hash(baseline.originalBytes(),QCryptographicHash::Sha256)!=prepared.baselineSha) {
        outcome.error=QStringLiteral("RES изменился во время подготовки. Повторите команду.");return outcome;
    }
    archiveFile.close();
    // The archive baseline used by the confirmation is also the commit guard.
    bool unchanged=hashes.size()==profile.textureCount;
    for(int page=0;page<hashes.size();++page) {
        CResArchiveIndex source;QString error;
        unchanged=unchanged&&source.open(authoredSources.value(page),&error)
            &&QString::fromLatin1(QCryptographicHash::hash(source.readMember(pageNames.value(page)),QCryptographicHash::Sha256).toHex())==hashes[page].toString();
    }
    QString sourceError;unchanged=unchanged&&sources->current(&sourceError);
    if(!unchanged) {
        outcome.error=QStringLiteral("Исходные страницы атласа изменились во время анализа. Повторите команду на актуальной карте.");return outcome;
    }
    SResPublishedMembersSaveRequest request;request.source=QFileInfo(archivePath);request.destination=request.source;request.published=result.publishedMembers;request.createBackup=true;
    request.progress=[&](const QString& phase,int percent){
        QString text=QStringLiteral("Сохранение HD-атласа");
        if(phase.contains("Reading"))text=QStringLiteral("Чтение исходного HDPack");
        else if(phase.contains("Building"))text=QStringLiteral("Сборка и проверка HDPack");
        else if(phase.contains("Staging"))text=QStringLiteral("Подготовка HDPack к записи");
        else if(phase.contains("Waiting"))text=QStringLiteral("Ожидание освобождения ресурсов");
        else if(phase.contains("backup"))text=QStringLiteral("Создание проверенной резервной копии");
        else if(phase.contains("Writing"))text=QStringLiteral("Запись HDPack");
        else if(phase.contains("Reopening"))text=QStringLiteral("Проверка записанного HDPack");
        completionState.operation(text,2,completionSteps,percent,100);
    };
    const auto committed=commitStagedResPublishedMembers(baseline,request);
    outcome.archiveTouched=committed.committed;
    if(!committed.success) {
        outcome.error=committed.error;return outcome;
    }
    completionState.operation(QStringLiteral("Сохранение и проверка карты"),3,completionSteps);
    const auto saved=commitValidatedMprBytes(mapRequest);
    outcome.saved=saved;
    if(!saved.success) {
        QFile current(archivePath);bool restored=false;
        if(current.open(QIODevice::ReadOnly)) {
            const QByteArray now=current.readAll();current.close();
            if(QString::fromLatin1(QCryptographicHash::hash(now,QCryptographicHash::Sha256).toHex())==committed.candidateSha256) {
                QSaveFile rollback(archivePath);const auto old=baseline.originalBytes();
                restored=rollback.open(QIODevice::WriteOnly)&&rollback.write(old)==old.size()&&rollback.commit();
            }
        }
        outcome.error=saved.error+QStringLiteral("\nОткат RES: %1").arg(restored?QStringLiteral("выполнен"):QStringLiteral("нужна ручная проверка"));return outcome;
    }
    // Only a committed new MPR may make its previous atlas an orphan.
    // Cleanup failure retains a valid pair and reports the recoverable extra data.
    QString cleanupNotice;
    completionState.operation(QStringLiteral("Проверка старого атласа"),4,completionSteps);
    QFile currentArchive(archivePath);QByteArray currentBytes;
    if(currentArchive.open(QIODevice::ReadOnly))currentBytes=currentArchive.readAll();
    currentArchive.close();
    CResArchiveDocument current;QByteArray compacted;QJsonObject finalCleanup;
    if(QString::fromLatin1(QCryptographicHash::hash(currentBytes,QCryptographicHash::Sha256).toHex())!=committed.candidateSha256
       ||!current.loadBytes(currentBytes)
       ||!buildTerrainHdCleanup(current,profile.mprNamespace,result.profile.mprNamespace,mapRequest.destination.absoluteFilePath(),mapRoots,&compacted,&finalCleanup,&cleanupError))
        cleanupNotice=QStringLiteral("Старый атлас сохранён: пакет или список карт изменился. %1").arg(cleanupError);
    else if(finalCleanup["removedMembers"].toInt()) {
        if(!commitTerrainHdCleanup(archivePath,currentBytes,compacted,finalCleanup,&cleanupError))
            cleanupNotice=QStringLiteral("Карта сохранена; очистка старого атласа не завершена: %1").arg(cleanupError);
    } else if(finalCleanup["reason"].toString()!=QStringLiteral("stock-or-unverified-original-atlas"))cleanupNotice=QStringLiteral("Старый атлас сохранён (%1).").arg(finalCleanup["reason"].toString());
    baseline=CResArchiveDocument();current=CResArchiveDocument();currentBytes.clear();compacted.clear();
    outcome.cleanupNotice=cleanupNotice;outcome.success=true;return outcome;
    });
    QFutureWatcher<Published> publicationWatcher;QEventLoop publicationLoop;
    connect(&publicationWatcher,&QFutureWatcherBase::finished,&publicationLoop,&QEventLoop::quit);
    publicationWatcher.setFuture(publication);if(!publication.isFinished())publicationLoop.exec();
    const auto published=publication.result();
    if(!published.success){completionDialog.accept();emit terrainPublicationFinished(archivePath,published.archiveTouched);QMessageBox::critical(this,tr("HD публикация не завершена"),published.error);return;}
    auto adoptionProgress=[&](const QString& phase,int percent){completionState.operation(phase,5,completionSteps,percent,100);completionDialog.refresh();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);};
    adoptionProgress(tr("Применение сохранённого атласа"),0);
    const bool adopted=adoptCompleteTerrainBaseline(mapRequest.destination,published.saved.candidateSha256,result.after,archivePath,result.profile,!semantic.isEmpty(),adoptionProgress);
    emit terrainPublicationFinished(archivePath,true);
    if(!adopted){completionDialog.accept();QMessageBox::warning(this,tr("HD атлас сохранён"),tr("Пара MPR/RES сохранена. Откройте карту заново для продолжения редактирования."));return;}
    // An open shared-brush document must not retain the former slot layout or
    // source pages after this map changes its atlas namespace.
    syncWorldTerrainActive();
    m_worldTerrainSession.clearHistory();
    const auto active=CWorldTerrainSession::key(mapPath);
    if(auto doc=m_worldTerrainSession.documents.value(active))doc->atlas.clear();
    m_worldTerrainAtlasSources.remove(active);
    updateWorldTerrainStatus();
    // Use the normal terrain transaction: one undo command with captured packed
    // values and a fresh seed. Saving persists that command through the usual
    // MPR/semantic-field path; the unchecked default keeps the existing workflow.
    QString completion=tr("Атлас карты пересобран в HD: %1 слотов, %2 переходов.").arg(hd).arg(generated);
    if(distributeVariants) {
        const bool hadSeed=m_terrainEdit.hasRandomPaintSeed();
        const quint32 previousSeed=m_terrainEdit.randomPaintSeed();
        QString detail;
        const auto observer=connect(this,&CView::updateMsg,this,[&detail](const QString& text){detail=text;});
        auto shuffleProgress=[&](const QString& phase,int percent){completionState.operation(phase,6,completionSteps,qMax(0,percent),percent<0?0:100);completionDialog.refresh();QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);};
        shuffleProgress(tr("Раскладка вариантов по карте"),0);
        shuffleTerrainTiles(false,shuffleProgress);disconnect(observer);
        const bool applied=m_terrainEdit.hasRandomPaintSeed()
            &&(!hadSeed||m_terrainEdit.randomPaintSeed()!=previousSeed)&&m_pLand->isDirty();
        if(!applied) {
            const QString message=tr("HD-пара сохранена, но раскладка вариантов не применена. %1").arg(detail);
            completionDialog.accept();emit updateMsg(message);QMessageBox::warning(this,tr("Раскладка вариантов"),message);
            emit mprSaveStateChanged();return;
        }
        completion+=tr(" Варианты разложены в рабочей карте; сохраните MPR для записи раскладки. Undo/Redo доступны.");
    }
    completionDialog.accept();
    if(!published.cleanupNotice.isEmpty())completion+=QStringLiteral(" ")+published.cleanupNotice;
    emit mprSaveStateChanged();emit updateMsg(completion);
}

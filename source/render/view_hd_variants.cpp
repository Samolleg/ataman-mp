#include "view.h"
#include "landscape_tools.h"
#include "resourcemanager.h"
#include "terrain_hd_variant_bank.h"
#include "terrain_hd_library.h"
#include "terrain_hd_progress_dialog.h"
#include "mmp_codec.h"
#include "resource_name.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QMessageBox>

void CView::generateTerrainHdVariants()
{
    const auto resources=CTextureList::getInstance()->configuredResourceArchivePaths();
    auto config=TerrainHdVariantBank::configured(resources);
    const auto sources=TerrainHdLibrary::configuredSources(resources);
    CTerrainHdProgress state;CTerrainHdProgressDialog dialog(state,this,true);
    dialog.setWindowTitle(tr("HD-варианты материалов"));dialog.show();
    auto future=QtConcurrent::run([config,sources,&state]() mutable {
        QString error;QStringList local;
        if(!TerrainHdLibrary::ensure(sources,&local,&error,&state)) {
            TerrainHdVariantBank::Bank failed;failed.error=error;failed.cancelled=state.cancelled();return failed;
        }
        config.archives=local;
        return TerrainHdVariantBank::generate(config,&state);
    });
    QFutureWatcher<TerrainHdVariantBank::Bank> watcher;QEventLoop loop;
    connect(&watcher,&QFutureWatcherBase::finished,&loop,&QEventLoop::quit);watcher.setFuture(future);
    if(!future.isFinished())loop.exec();auto bank=future.result();dialog.accept();
    if(bank.cancelled||(!bank.success&&state.cancelled())){emit updateMsg(tr("Генерация отменена. Карта не изменена."));return;}
    if(!bank.success){QMessageBox::warning(this,tr("HD-варианты"),bank.error);return;}
    TerrainHdVariantBank::forget();++m_terrainFamilyPrewarmGeneration;m_preparedTerrainFamilyStage.clear();
    // Rebuilding the palette is a read-only presentation update. Keep the live
    // working atlas, edited SEC and undo history; variants import at paint time.
    refreshTerrainHdVariantChoices();
    refreshTerrainFamilyUsage();
    QMessageBox::information(this,tr("HD-варианты"),tr("Сгенерировано: %1; отклонено: %2; материалов с недобором: %3.\nВремя: %4 с.\nБанк: %5")
        .arg(bank.manifest["generated"].toInt()).arg(bank.manifest["rejected"].toInt()).arg(bank.manifest["shortfalls"].toInt())
        .arg(bank.manifest["elapsedMs"].toDouble()/1000.,0,'f',1).arg(bank.directory));
}

void CView::refreshTerrainHdVariantChoices()
{
    const auto bank=m_landscapeTools&&m_landscapeTools->generatedHdVariantsRequested()
        ?TerrainHdVariantBank::available():QSharedPointer<const TerrainHdVariantBank::Bank>();
    auto update=[&](QVector<STerrainPaintMaterialChoice>& choices,ETerrainAtlasLayer layer) {
        for(auto& choice:choices) {
            choice.generatedIds.clear();choice.generatedThumbnails.clear();
            if(!bank)continue;
            for(const auto& family:m_terrainGlobalFamilies)if(family.id==choice.id)
                for(const auto& e:TerrainHdVariantBank::forMaterial(*bank,family.name,family.runtimeTerrainType,layer)) {
                    choice.generatedIds.append(e.id);choice.generatedThumbnails.append(e.pixels.scaled(64,64));
                }
        }
    };
    update(m_modernLandPaintChoices,ETerrainAtlasLayer::Land);
    update(m_modernUnderwaterPaintChoices,ETerrainAtlasLayer::Land);
    update(m_modernOverlayPaintChoices,ETerrainAtlasLayer::Overlay);
}

bool CView::importTerrainHdVariants(qint32 material,ETerrainTileLayer layer,CValidationReport* report,SWorkingTerrainResolveStage* staging)
{
    if(!m_landscapeTools||!m_landscapeTools->generatedHdVariants()||!m_workingTerrainAtlas)return true;
    const auto bank=TerrainHdVariantBank::available();if(!bank)return true;
    QString name;int reviewed=-1;
    for(const auto& f:m_terrainGlobalFamilies)if(f.id==material){name=f.name;reviewed=f.runtimeTerrainType;break;}
    if(name.isEmpty())for(const auto& f:m_terrainAtlasProfile.families)if(f.id==material){name=f.name;reviewed=f.tileType;break;}
    const auto entries=TerrainHdVariantBank::forMaterial(*bank,name,reviewed,layer==ETerrainTileLayer::Land?ETerrainAtlasLayer::Land:ETerrainAtlasLayer::Overlay);
    if(entries.isEmpty())return true;
    auto atlas=staging?staging->atlas:m_workingTerrainAtlas->createStagingCopy();
    const auto before=atlas->activeTileCount();
    if(!TerrainHdVariantBank::import(atlas.data(),entries,material,report))return false;
    if(atlas->activeTileCount()==before)return true;
    auto& terrain=staging?staging->terrainEdit:m_terrainEdit;
    if(!terrain.canInstallWorkingAtlasSlotTypes(atlas->activeTileTypes()))return false;
    if(!staging&&!m_workingTerrainAtlas->commitStaging(*atlas))return false;
    return terrain.installWorkingAtlasSlotTypes(atlas->activeTileTypes());
}

void CView::applyTerrainHdVariantSelectionPolicy()
{
    if(!m_workingTerrainAtlas)return;
    const bool enabled=m_landscapeTools&&m_landscapeTools->generatedHdVariants();const auto excluded=TerrainHdVariantBank::rejects();
    QMap<int,QByteArray> pages;
    for(const auto& definition:m_terrainAtlasProfile.slotDefinitions)if(definition.sourceNamespace.startsWith(QStringLiteral("hd-variant:"))) {
        m_workingTerrainAtlas->restoreHdVariantIdentity(definition.slot,definition.sourceNamespace,definition.sourceSlotAliases);
        const int page=definition.slot/64;
        if(!pages.contains(page)){QByteArray payload;CTextureList::getInstance()->terrainMmpPayload(
            QStringLiteral("%1%2.mmp").arg(m_terrainAtlasProfile.mprNamespace.toLower()).arg(page,3,10,QLatin1Char('0')),&payload);pages.insert(page,payload);}
        const auto levels=ei::terrainTileMipChain(pages[page],definition.slot%64);
        if(definition.slot<m_workingTerrainAtlas->tileImages().size())ei::rememberTerrainMipChain(m_workingTerrainAtlas->tileImages()[definition.slot],levels);
    }
    for(int slot=0;slot<m_workingTerrainAtlas->activeTileCount();++slot){STerrainWorkingTileIdentity id;
        if(m_workingTerrainAtlas->tileIdentityForSlot(slot,&id)&&id.sourceNamespace.startsWith(QStringLiteral("hd-variant:")))
            m_workingTerrainAtlas->setSlotPaintSelectable(slot,enabled&&!excluded.contains(id.sourceNamespace.mid(11)));
    }
}

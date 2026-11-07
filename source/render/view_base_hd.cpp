#include "terrain_atlas_compact.h"
#include "mmp_codec.h"
#include "view.h"
#include "terrain_base_hd.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "log.h"
#include "resourcemanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QtConcurrent>
#include <QUndoStack>

namespace {
class CBaseHdCommand : public QUndoCommand {
public:
    CBaseHdCommand(CView* view,const STerrainTileCommand& command):m_view(view),m_command(command){setText(command.description);}
    void undo() override {m_view->applyTerrainTilePreview(m_command,false);}
    void redo() override {if(m_first){m_first=false;return;}m_view->applyTerrainTilePreview(m_command,true);}
private:
    CView* m_view;STerrainTileCommand m_command;bool m_first{true};
};
}

void CView::convertBaseTerrainToHdRequested()
{
    useLegacyViewport(QString::fromUtf8("Преобразование Base в HD"));
    QString summary;
    const bool ok=convertBaseTerrainToHd(QDir(QCoreApplication::applicationDirPath()).filePath("hd-library"),
        QRandomGenerator::global()->generate(),&summary,true);
    ei::log(eLogInfo,QStringLiteral("Base HD conversion %1: %2").arg(ok?"completed":"stopped",summary));
    if(ok)QMessageBox::information(this,tr("Базовые тайлы в HD"),summary);
    else if(summary!="Cancelled")QMessageBox::warning(this,tr("Базовые тайлы в HD"),summary);
}

bool CView::convertBaseTerrainToHd(const QString& library,quint32 seed,QString* summary,bool interactive)
{
    const auto fail=[&](const QString& text){if(summary)*summary=text;return false;};
    if(!m_pLand||!m_pLand->isMprLoad()||!m_pUndoStack)
        return fail(tr("Сначала откройте карту."));
    finishSculptStroke();endTileBrushGroup();
    if(!ensureTerrainAutoCatalogReady()||!m_workingTerrainAtlas||!m_workingTerrainAtlas->isValid())
        return fail(tr("Для этой карты пока нет подтверждённого каталога базовых материалов."));
    // Capture immutable inputs before a background worker reads the local bank.
    const auto before=m_workingTerrainAtlas->createStagingCopy();
    const auto terrain=m_terrainEdit;const auto profile=m_terrainAtlasProfile;
    const auto animations=m_mprAnimatedTiles;
    QVector<QByteArray> sourcePages;
    const auto map=m_mprDocument.mapInfo();
    for(int page=0;page<int(map.textureCount);++page){
        QByteArray bytes;QString path;
        const auto name=QStringLiteral("%1%2.mmp").arg(map.innerNamespace).arg(page,3,10,QLatin1Char('0'));
        if(!CTextureList::getInstance()->terrainMmpPayload(name,&bytes,&path))return fail(tr("Не удалось прочитать исходную страницу: %1").arg(name));
        sourcePages.append(bytes);
    }
    std::atomic_bool cancelled(false);
    const auto sourceDocument=m_mprDocument;const auto materials=m_mprMaterials;const auto globals=m_terrainGlobalFamilies;
    const auto prepare=[&](){
        auto p=prepareBaseTerrainHd(library,*before,terrain,profile,animations,seed,&cancelled,sourcePages);
        if(!p.error.isEmpty()||p.command.isEmpty()||cancelled.load())return p;
        CValidationReport report;auto edited=terrain;
        CMprEditTransaction tx("HD atlas forecast");QByteArray bytes;CMprDocument doc;
        if(!edited.apply(p.command,true,&report)||!edited.appendSectorEdits(&tx,&report)
           ||!sourceDocument.appendMaxZEdit(edited.maxZ(),&tx,&report)||!sourceDocument.applyTransaction(tx,&bytes,&report)
           ||!doc.loadBytes(bytes,"hd-forecast.mpr",&report)){p.error=report.toText();return p;}
        auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(doc,p.atlas->activeTileImages(),p.atlas->activeTileTypes(),
            sourcePages,materials,animations,&report,p.atlas->preservedDxtTiles());
        CResArchiveDocument archive;QStringList errors;
        if(!package.success||!doc.loadBytes(package.mprBytes,"hd-forecast.mpr",&report)
           ||!archive.loadBytes(package.resourceArchiveBytes,"hd-forecast.res",&errors)){p.error=package.error+report.toText()+errors.join('\n');return p;}
        QVector<QByteArray> pages;QVector<QImage> images;QString error;
        for(const auto& name:package.resourceMembers){auto page=archive.memberPayload(name);pages<<page;
            for(int i=0;i<64;++i){QImage image;if(!ei::decodeMmpDxt1AtlasTile(page,i,&image,&error)){p.error=error;return p;}images<<image;}}
        auto binding=CTerrainAtlasProfileStore::workingAtlasProfile(doc.mapInfo().innerNamespace,images,p.atlas->catalog(),profile,globals,&report);
        const auto compact=prepareTerrainAtlasCompact(doc,binding,pages,QByteArray(),QFileInfo("hd-forecast.mpr"),&cancelled);
        if(!compact.error.isEmpty()){p.error=compact.error;return p;}
        p.forecastPages=compact.pages.size();p.forecastDxt=compact.newDxt;p.forecastRgba=compact.newRgba;return p;
    };
    SBaseHdPlan plan;
    if(interactive){
        QProgressDialog progress(tr("Подбор базовых HD-текстур и вариантов…"),tr("Отмена"),0,0,this);
        progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);progress.show();
        QFutureWatcher<SBaseHdPlan> watcher;QEventLoop loop;
        const auto cancelConnection=connect(&progress,&QProgressDialog::canceled,[&](){cancelled.store(true);});
        connect(&watcher,&QFutureWatcher<SBaseHdPlan>::finished,&loop,&QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&](){return prepare();}));
        loop.exec();plan=watcher.result();
        // close() emits canceled even on successful completion. Only user input
        // while the worker is active may cancel the prepared transaction.
        disconnect(cancelConnection);
        progress.close();
    }else plan=prepare();
    if(cancelled.load())return fail("Cancelled");
    if(!plan.error.isEmpty())return fail(plan.error);
    for(const auto& detail:plan.unmatchedSources)ei::log(eLogInfo,"Base HD unmatched: "+detail);
    if(plan.command.isEmpty())return fail(tr("Безопасных замен нет. Без HD-соответствия: %1 клеток. Сохранены исходными ради стыков: %2 клеток.").arg(plan.unmatchedCells).arg(plan.seamRejectedCells));
    if(interactive&&QMessageBox::question(this,tr("Перевести базовые тайлы в HD"),
        tr("После сохранения: %1 → %2 страниц. DXT1 с mip: %3 МиБ; RGBA базовых уровней: %4 МиБ.\nЗамен: %5; импортируется используемых вариантов: %6. Применить?")
            .arg(sourceDocument.mapInfo().textureCount).arg(plan.forecastPages).arg(plan.forecastDxt/1048576.0,0,'f',2)
            .arg(plan.forecastRgba/1048576.0,0,'f',2).arg(plan.command.deltas.size()).arg(plan.importedVariants),
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return fail("Cancelled");
    CValidationReport report;
    if(!m_workingTerrainAtlas->commitStaging(*plan.atlas)||!syncWorkingTerrainAtlas(&report)) {
        m_workingTerrainAtlas->commitStaging(*before);syncWorkingTerrainAtlas(&report);
        return fail(report.toText());
    }
    if(!applyTerrainTilePreview(plan.command,true)){
        m_workingTerrainAtlas->commitStaging(*before);syncWorkingTerrainAtlas(&report);
        return fail(tr("Не удалось применить замену тайлов."));
    }
    m_pUndoStack->push(new CBaseHdCommand(this,plan.command));
    if(summary)*summary=tr("Заменено клеток: %1. Исходных тайлов: %2. Используемых HD-вариантов: %3.\nБез надёжного соответствия: %4 клеток.\nСохранены исходными ради стыков: %5 клеток.\n\nВода и переходы сохранены. Изменение можно отменить; для записи на диск сохраните карту.")
        .arg(plan.command.deltas.size()).arg(plan.changedSources).arg(plan.importedVariants).arg(plan.unmatchedCells).arg(plan.seamRejectedCells);
    return true;
}

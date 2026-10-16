#include "view.h"
#include "terrain_base_hd.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "log.h"
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
    QString summary;
    const bool ok=convertBaseTerrainToHd(QDir(QCoreApplication::applicationDirPath()).filePath("hd-library"),
        QRandomGenerator::global()->generate(),&summary,true);
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
    std::atomic_bool cancelled(false);
    SBaseHdPlan plan;
    if(interactive){
        QProgressDialog progress(tr("Подбор базовых HD-текстур и вариантов…"),tr("Отмена"),0,0,this);
        progress.setWindowModality(Qt::ApplicationModal);progress.setMinimumDuration(0);progress.show();
        QFutureWatcher<SBaseHdPlan> watcher;QEventLoop loop;
        connect(&progress,&QProgressDialog::canceled,[&](){cancelled.store(true);});
        connect(&watcher,&QFutureWatcher<SBaseHdPlan>::finished,&loop,&QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&](){return prepareBaseTerrainHd(library,*before,terrain,profile,animations,seed,&cancelled);}));
        loop.exec();plan=watcher.result();progress.close();
    }else plan=prepareBaseTerrainHd(library,*before,terrain,profile,animations,seed,&cancelled);
    if(cancelled.load())return fail("Cancelled");
    if(!plan.error.isEmpty())return fail(plan.error);
    for(const auto& detail:plan.unmatchedSources)ei::log(eLogInfo,"Base HD unmatched: "+detail);
    if(plan.command.isEmpty())return fail(tr("Подходящих замен нет. Базовых клеток без надёжного HD-соответствия: %1.").arg(plan.unmatchedCells));
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
    if(summary)*summary=tr("Заменено клеток: %1. Исходных тайлов: %2. HD-вариантов в наборах: %3.\nБез надёжного соответствия: %4 клеток.\n\nВода и переходы сохранены. Изменение можно отменить; для записи на диск сохраните карту.")
        .arg(plan.command.deltas.size()).arg(plan.changedSources).arg(plan.importedVariants).arg(plan.unmatchedCells);
    return true;
}

#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "landscape.h"
#include "resourcemanager.h"
#include "background_mob_load.h"
#include "process_edit_lock.h"
#include "mob_family_validation.h"
#include "log.h"
#include <QtConcurrent>
#include <QApplication>
#include <QStatusBar>
#include <QTimer>
#include <QScopedValueRollback>

struct SBackgroundMobState {
    std::shared_ptr<std::atomic_bool> cancel{std::make_shared<std::atomic_bool>(false)};
    QString mapKey,resourceKey;
    quint64 resourceGeneration{0};
    QStringList roots,paths,warnings;
    QMap<QString,QString> scripts;
    QVector<QPair<QString,QString>> sources;
    QSet<QString> visited,scanned;
    bool working{false};
    SPreparedMobLoad prepared;
    SPreparedMobProjection projection;
    std::unique_ptr<CMob> mob;
    QSharedPointer<CProcessEditLock> lease;
    int nextObject{0},loaded{0};
};
namespace {
struct Dependencies {QStringList paths,warnings;};
QString mapKey(CView*) {
    return CLandscape::getInstance()->isMprLoad()?CProcessEditLock::identity(CLandscape::getInstance()->filePath().absoluteFilePath()):QString();
}
}

void MainWindow::cancelBackgroundMobs(bool resumeNeighbors)
{
    ++m_backgroundMobGeneration;
    if(m_backgroundMobs) {
        m_backgroundMobs->cancel->store(true);
        if(m_pView&&m_pView->isValid())m_pView->makeCurrent();
        m_backgroundMobs.clear();
    }
    if(m_pView)m_pView->setProperty("loadingAddMobs",false);
    if(m_pView&&resumeNeighbors)m_pView->deferNeighborLoading(false);
}

void MainWindow::queueBackgroundMobSource(CMob* source,bool root)
{
    if(!source)return;
    if(!m_backgroundMobs) {
        m_backgroundMobs=QSharedPointer<SBackgroundMobState>::create();
        m_pView->setProperty("loadingAddMobs",true);
        m_backgroundMobs->mapKey=mapKey(m_pView);
        m_backgroundMobs->resourceKey=configuredResourceFingerprint(m_pView->settings());
        m_backgroundMobs->resourceGeneration=CObjectList::getInstance()->sourceGeneration();
    }
    auto& state=*m_backgroundMobs;
    const QString path=source->filePath().absoluteFilePath(),key=CProcessEditLock::identity(path);
    if(root&&!state.roots.contains(key))state.roots.append(key);
    state.visited.insert(key);
    if(state.scanned.contains(key))return;
    state.scanned.insert(key);
    const auto script=m_pView->scenarioScriptText(source);
    state.scripts[key]=script;state.sources.append(qMakePair(path,script));
}

void MainWindow::pumpBackgroundMobs(quint64 generation)
{
    if(generation!=m_backgroundMobGeneration||!m_backgroundMobs||!m_pView||m_shutdownInProgress)return;
    auto state=m_backgroundMobs;
    const auto later=[this,generation](int delay=8) {
        QTimer::singleShot(delay,this,[this,generation](){pumpBackgroundMobs(generation);});
    };
    if(m_workspaceLoadInProgress||m_pView->hasPendingOperation()||QApplication::activeModalWidget()) {later(30);return;}
    if(state->working)return;
    QMap<QString,CMob*> loaded;
    for(auto* mob:m_pView->mobs())loaded[CProcessEditLock::identity(mob->filePath().absoluteFilePath())]=mob;
    bool current=state->mapKey==mapKey(m_pView)
        &&state->resourceGeneration==CObjectList::getInstance()->sourceGeneration()
        &&state->resourceKey==configuredResourceFingerprint(m_pView->settings());
    for(const auto& root:state->roots)current=current&&loaded.contains(root);
    for(auto it=state->scripts.cbegin();current&&it!=state->scripts.cend();++it)
        current=loaded.contains(it.key())&&m_pView->scenarioScriptText(loaded[it.key()])==it.value();
    if(!current) {
        ei::log(eLogInfo,"Background MOB load cancelled: workspace, script, or resources changed.");
        cancelBackgroundMobs(true);return;
    }

    if(state->prepared.valid()) {
        const QScopedValueRollback<bool> attaching(m_backgroundMobAttaching,true);
        const auto discard=[&]() {
            m_pView->makeCurrent();state->mob.reset();state->lease.clear();
            state->prepared={};state->projection={};state->nextObject=0;
        };
        if(!state->prepared.sourceIsCurrent()) {
            state->warnings.append("AddMob changed before adoption: "+state->prepared.source.fileName());
            discard();later();return;
        }
        m_pView->makeCurrent();QElapsedTimer slice;slice.start();int count=0;
        if(!state->mob) {
            setProperty("backgroundLoadPhase","addmob-project");
            state->mob.reset(new CMob);state->mob->attach(m_pView,nullptr);state->nextObject=0;
            if(!state->mob->beginPreparedLoad(state->prepared)) {
                state->warnings.append("Unsupported AddMob metadata: "+state->prepared.source.fileName());
                discard();later();return;
            }
        }
        if(state->nextObject==-1) {
            if(!state->projection.error.isEmpty()) {
                state->warnings.append(state->projection.error);discard();later();return;
            }
            const auto key=CProcessEditLock::identity(state->prepared.source.absoluteFilePath());
            if(!loaded.contains(key)) {
                setProperty("backgroundLoadPhase","addmob-adopt");
                state->mob->finishPreparedLoad(state->prepared,state->projection);
                state->mob->setLoadedFromAddMob(true);
                auto* mob=state->mob.release();
                m_pView->adoptLoadedMob(mob,state->lease,false);
                queueBackgroundMobSource(mob,false);++state->loaded;
            } else queueBackgroundMobSource(loaded[key],false);
            discard();later();return;
        }
        while(state->nextObject<state->prepared.objects.size()&&count<16&&slice.elapsed()<4) {
            if(!state->mob->appendPreparedObject(state->prepared.objects[state->nextObject++])) {
                state->warnings.append("Unsupported AddMob projection: "+state->prepared.source.fileName());
                discard();later();return;
            }
            ++count;
        }
        if(state->nextObject<state->prepared.objects.size()){later();return;}
        QByteArray projection;state->mob->serializeMob(projection,false);
        setProperty("backgroundLoadPhase","addmob-validate");
        state->working=true;
        const auto prepared=state->prepared;
        auto* watcher=new QFutureWatcher<SPreparedMobProjection>(this);
        connect(watcher,&QFutureWatcher<SPreparedMobProjection>::finished,this,[this,generation,watcher]() {
            const auto result=watcher->result();watcher->deleteLater();
            if(generation!=m_backgroundMobGeneration||!m_backgroundMobs||!m_pView)return;
            auto state=m_backgroundMobs;state->working=false;
            // Adoption waits for the context/gesture gates in the next pulse.
            state->projection=result;
            state->nextObject=-1; // validated; next GUI turn performs guarded adoption
            QTimer::singleShot(8,this,[this,generation](){pumpBackgroundMobs(generation);});
        });
        watcher->setFuture(QtConcurrent::run([prepared,projection](){return validateBackgroundMobProjection(prepared,projection);}));
        return;
    }

    if(!state->paths.isEmpty()) {
        const auto path=state->paths.takeFirst(),key=CProcessEditLock::identity(path);
        if(state->visited.contains(key)){later();return;}
        state->visited.insert(key);
        if(loaded.contains(key)){queueBackgroundMobSource(loaded[key],false);later();return;}
        QString error;state->lease=CProcessEditLock::acquire(path,&error);
        if(!state->lease){state->warnings.append(error);later();return;}
        state->working=true;const auto cancel=state->cancel;
        setProperty("backgroundLoadPhase","addmob-read");
        statusBar()->showMessage(tr("Фон: загрузка дополнительного MOB %1").arg(QFileInfo(path).fileName()));
        auto* watcher=new QFutureWatcher<SPreparedMobLoad>(this);
        connect(watcher,&QFutureWatcher<SPreparedMobLoad>::finished,this,[this,generation,watcher]() {
            const auto result=watcher->result();watcher->deleteLater();
            if(generation!=m_backgroundMobGeneration||!m_backgroundMobs||!m_pView)return;
            auto state=m_backgroundMobs;state->working=false;state->warnings.append(result.warnings);
            if(!result.valid()||!result.sourceIsCurrent()) {
                state->warnings.append("AddMob "+result.source.fileName()+": "+result.error);state->lease.clear();
            } else {
                state->prepared=result;
            }
            QTimer::singleShot(8,this,[this,generation](){pumpBackgroundMobs(generation);});
        });
        watcher->setFuture(QtConcurrent::run([path,cancel](){return prepareBackgroundMob(QFileInfo(path),cancel);}));
        return;
    }
    if(!state->sources.isEmpty()) {
        const auto source=state->sources.takeFirst();const auto roots=activeMapRoots().toList();
        state->working=true;auto* watcher=new QFutureWatcher<Dependencies>(this);
        connect(watcher,&QFutureWatcher<Dependencies>::finished,this,[this,generation,watcher]() {
            const auto result=watcher->result();watcher->deleteLater();
            if(generation!=m_backgroundMobGeneration||!m_backgroundMobs)return;
            m_backgroundMobs->working=false;m_backgroundMobs->paths.append(result.paths);
            m_backgroundMobs->warnings.append(result.warnings);
            QTimer::singleShot(8,this,[this,generation](){pumpBackgroundMobs(generation);});
        });
        watcher->setFuture(QtConcurrent::run([source,roots](){Dependencies r;r.paths=CMobFamilyValidator::addMobPaths(source.second,source.first,roots,&r.warnings);return r;}));
        return;
    }
    ei::log(eLogInfo,QString("Workspace background AddMob complete loaded:%1 warnings:%2").arg(state->loaded).arg(state->warnings.size()));
    if(!state->warnings.isEmpty()) {
        for(const auto& warning:state->warnings)ei::log(eLogWarning,warning);
        showTransientResultNotification(tr("Загрузка MOB из AddMob: предупреждения\n%1").arg(state->warnings.mid(0,5).join('\n')),
            false,true,QString(),ETransientNotificationAction::None,15000);
    }
    m_backgroundMobs.clear();
    m_pView->setProperty("loadingAddMobs",false);
    statusBar()->clearMessage();
    setProperty("backgroundLoadPhase","neighbors");
    m_pView->deferNeighborLoading(false);
    scheduleMobFamilyValidation(0);
}

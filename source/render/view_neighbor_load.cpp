#include "view.h"
#include "landscape.h"
#include "resourcemanager.h"
#include "neighbor_visual_preparation.h"
#include "log.h"
#include <QtConcurrent>
#include <QApplication>
#include <QTimer>

void CView::deferNeighborLoading(bool deferred)
{
    if(m_neighborLoadingDeferred==deferred)return;
    m_neighborLoadingDeferred=deferred;
    if(deferred) {
        m_neighborLoadRequested=true;
        clearNeighborLandscapes(true);
    } else if(m_neighborLoadRequested)loadNeighborLandscapes();
}

void CView::loadNeighborLandscapes()
{
    if(m_applicationShutdown)return;
    m_neighborLoadRequested=true;
    if(m_neighborLoadingDeferred)return;
    m_neighborLoadRequested=false;
    const bool reloading=m_neighborLandscapesVisible && !m_neighborPlacements.isEmpty()
        && m_pLand && m_pLand->isMprLoad() && isValid();
    const auto contextKey=m_neighborContextKey;
    clearNeighborLandscapes(reloading);
    m_neighborContextKey=contextKey;
    if(!reloading){setProperty("neighborLoadPhase",0);return;}
    m_neighborLoadStage=1;
    setProperty("neighborLoadPhase",1);
    ei::log(eLogInfo,"Workspace background stage: touching terrain");
    finishNeighborLoadStage(m_neighborSceneGeneration->load());
}

// Dispatch at most one placement per pulse, with two CPU workers. Fingerprints
// consult GUI-owned resource caches, so do not batch every map in one GUI turn.
void CView::startNeighborLoadStage(quint64 generation)
{
    if(generation!=m_neighborSceneGeneration->load())return;
    m_neighborLoadPulseScheduled=false;
    if(m_neighborLoadingDeferred || !m_neighborLandscapesVisible || m_applicationShutdown)return;
    if(hasPendingOperation() || QApplication::activeModalWidget()) {
        m_neighborLoadPulseScheduled=true;
        QTimer::singleShot(30,this,[this,generation](){startNeighborLoadStage(generation);});return;
    }
    if(m_neighborLoadJobs>=2 || !m_neighborLoadStage)return;
    const QSizeF activeSize(m_pLand->widthInWorldUnits(),m_pLand->heightInWorldUnits());
    while(m_neighborLoadNext<m_neighborPlacements.size()) {
        const auto placement=m_neighborPlacements[m_neighborLoadNext++];
        const bool touches=CNeighborMapLayout::touchesActiveMap(activeSize,placement);
        if(touches!=(m_neighborLoadStage!=3))continue;
        const bool objects=m_neighborLoadStage==2;
        const auto terrainFingerprint=neighborTerrainFingerprint(placement.mprPath,neighborResourceFingerprint());
        const auto objectFingerprint=objects?neighborContentFingerprint(placement.mobPath)+neighborObjectResourceFingerprint():QByteArray();
        int visible=-1;
        for(int i=0;i<m_neighborLandscapes.size();++i)
            if(CWorldTerrainSession::key(m_neighborLandscapes[i].placement.mprPath)==CWorldTerrainSession::key(placement.mprPath)){visible=i;break;}
        if(!objects && visible<0) {
            for(int i=m_neighborRenderCache.size()-1;i>=0;--i) {
                auto& cached=m_neighborRenderCache[i];
                if(CWorldTerrainSession::key(cached.placement.mprPath)!=CWorldTerrainSession::key(placement.mprPath))continue;
                makeCurrent();
                if(cached.terrainFingerprint!=terrainFingerprint || !cached.landscape || !cached.landscape->isMprLoad()) {
                    m_neighborRenderCache.remove(i);doneCurrent();continue;
                }
                auto restored=m_neighborRenderCache.takeAt(i);
                restored.placement=placement;restored.showObjects=false;
                restored.lastUse=++m_neighborCacheClock;restored.drawCacheDirty=true;
                restored.landscape->setWorldOffset(QVector3D(placement.origin.x(),placement.origin.y(),0));
                doneCurrent();m_neighborLandscapes.append(restored);visible=m_neighborLandscapes.size()-1;
                ei::log(eLogInfo,"Neighbor render cache hit/rebase zone:"+placement.zoneId);break;
            }
        }
        if(objects) {
            // A failed terrain must not bring objects into the wrong landscape.
            if(visible<0){finishNeighborLoadStage(generation);return;}
            if(m_neighborLandscapes[visible].terrainFingerprint!=terrainFingerprint) {
                loadNeighborLandscapes();return;
            }
            auto& loaded=m_neighborLandscapes[visible];loaded.showObjects=true;
            if(loaded.objectFingerprint==objectFingerprint && loaded.objectsPrepared) {
                if(!loaded.pendingObjects.isEmpty() && !m_neighborObjectUploadScheduled) {
                    m_neighborObjectUploadScheduled=true;
                    QTimer::singleShot(8,this,[this,generation](){populateNeighborObjects(generation);});
                }
                update();finishNeighborLoadStage(generation);return;
            }
            makeCurrent();
            loaded.staticObjects.clear();loaded.textureLeases.clear();loaded.staticLocalPositions.clear();loaded.distantParts.clear();
            loaded.pendingObjects.clear();loaded.pendingVisuals.clear();loaded.nextObject=0;
            loaded.drawRecords.clear();loaded.spatialGroups.clear();loaded.objectsPrepared=false;
            doneCurrent();
        } else if(visible>=0) {
            refreshWorldTerrainNeighbors();update();finishNeighborLoadStage(generation);return;
        }
        ++m_neighborLoadJobs;
        const auto epoch=m_neighborSceneGeneration;
        const auto archives=CTextureList::getInstance()->configuredResourceArchivePaths();
        auto* watcher=new QFutureWatcher<SNeighborSceneData>(this);
        connect(watcher,&QFutureWatcher<SNeighborSceneData>::finished,this,
            [this,watcher,generation,terrainFingerprint,objectFingerprint,objects]() {
                const auto result=watcher->result();watcher->deleteLater();
                acceptNeighborScene(result,generation,terrainFingerprint,objectFingerprint,objects);
            });
        watcher->setFuture(QtConcurrent::run(m_neighborScenePool,[placement,objects,epoch,generation,archives]() {
            if(epoch->load()!=generation)return SNeighborSceneData();
            auto scene=prepareNeighborScene(placement,objects,!objects);
            if(scene.valid && objects && !scene.staticObjects.isEmpty())
                prepareNeighborVisuals(scene,archives,[epoch,generation](){return epoch->load()==generation;});
            return scene;
        }));
        finishNeighborLoadStage(generation);return;
    }
    finishNeighborLoadStage(generation);
}

void CView::acceptNeighborScene(const SNeighborSceneData& result,quint64 generation,
    const QByteArray& terrainFingerprint,const QByteArray& objectFingerprint,bool objects)
{
    if(generation!=m_neighborSceneGeneration->load() || !m_neighborLandscapesVisible || !isValid())return;
    if(hasPendingOperation() || QApplication::activeModalWidget()) {
        QTimer::singleShot(30,this,[this,result,generation,terrainFingerprint,objectFingerprint,objects]() {
            acceptNeighborScene(result,generation,terrainFingerprint,objectFingerprint,objects);
        });return;
    }
    --m_neighborLoadJobs;
    for(const auto& diagnostic:result.diagnostics)ei::log(eLogWarning,diagnostic);
    if(!result.valid){finishNeighborLoadStage(generation);return;}
    if(neighborTerrainFingerprint(result.placement.mprPath,neighborResourceFingerprint())!=terrainFingerprint
       || (objects && neighborContentFingerprint(result.placement.mobPath)+neighborObjectResourceFingerprint()!=objectFingerprint)) {
        loadNeighborLandscapes();return;
    }
    makeCurrent();
    SLoadedNeighborLandscape loaded;loaded.placement=result.placement;
    for(const auto& current:m_neighborPlacements)
        if(current.zoneId==loaded.placement.zoneId)loaded.placement=current;
    for(int i=0;i<m_neighborLandscapes.size();++i)
        if(m_neighborLandscapes[i].placement.mprPath==loaded.placement.mprPath) {
            loaded=m_neighborLandscapes.takeAt(i);break;
        }
    if(!loaded.landscape) {
        loaded.landscape=CLandscape::createReadOnlyBackground();
        if(objects || !loaded.landscape->readPreparedBackground(QFileInfo(loaded.placement.mprPath),result.terrainMembers)) {
            loaded.landscape.clear();doneCurrent();finishNeighborLoadStage(generation);return;
        }
    }
    loaded.landscape->setWorldOffset(QVector3D(loaded.placement.origin.x(),loaded.placement.origin.y(),0));
    loaded.terrainFingerprint=terrainFingerprint;loaded.showObjects=objects;
    if(objects) {
        loaded.objectFingerprint=objectFingerprint;loaded.objectsPrepared=true;
        loaded.pendingObjects=result.staticObjects;loaded.pendingVisuals=result.visuals;loaded.nextObject=0;
    }
    loaded.lastUse=++m_neighborCacheClock;m_neighborLandscapes.append(loaded);
    trimNeighborRenderCache();doneCurrent();
    ei::log(eLogInfo,QString("Neighbor scene accepted zone:%1 stage:%2 staticObjects:%3 generation:%4")
        .arg(loaded.placement.zoneId).arg(m_neighborLoadStage).arg(result.staticObjects.size()).arg(generation));
    refreshWorldTerrainNeighbors();
    if(objects && !m_neighborObjectUploadScheduled) {
        m_neighborObjectUploadScheduled=true;
        QTimer::singleShot(8,this,[this,generation](){populateNeighborObjects(generation);});
    }
    update();finishNeighborLoadStage(generation);
}

void CView::finishNeighborLoadStage(quint64 generation)
{
    if(generation!=m_neighborSceneGeneration->load() || !m_neighborLoadStage || m_neighborLoadPulseScheduled)return;
    if(m_neighborLoadNext>=m_neighborPlacements.size()) {
        if(m_neighborLoadJobs || m_neighborObjectUploadScheduled)return;
        ++m_neighborLoadStage;m_neighborLoadNext=0;
        if(m_neighborLoadStage>3) {
            m_neighborLoadStage=0;setProperty("neighborLoadPhase",0);
            makeCurrent();trimNeighborRenderCache();doneCurrent();
            ei::log(eLogInfo,"Workspace background stages complete");return;
        }
        setProperty("neighborLoadPhase",m_neighborLoadStage);
        ei::log(eLogInfo,m_neighborLoadStage==2?"Workspace background stage: touching MOB objects":"Workspace background stage: remaining terrain");
    }
    if(m_neighborLoadJobs<2) {
        m_neighborLoadPulseScheduled=true;
        QTimer::singleShot(8,this,[this,generation](){startNeighborLoadStage(generation);});
    }
}

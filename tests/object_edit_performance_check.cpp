#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "settings.h"
#include "resourcemanager.h"
#include "resource_setup_dialog.h"
#include "operationmanager.h"
#include "property.h"
#include "scene.h"
#include "landscape.h"
#include "camera.h"
#include "viewport_minimap_widget.h"
#include "mmp_codec.h"
#include "endurance_document.h"
#include <QMouseEvent>
#include <QCryptographicHash>
#include <QOpenGLTexture>
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QTimer>
#include <QElapsedTimer>
#include <QTextStream>
#include <QFile>
#include <QJsonDocument>

namespace {
class HiddenWindows : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
class Module : public CWorldObj {
public:
    Module(){m_modelName="synthetic_wall";m_primaryTexture="synthetic_skin";}
};
}

// Both MainWindow and CView already expose this native integration entrypoint.
int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int failed=0;
    const auto check=[&](bool ok,const char* name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)++failed;};
    QTemporaryDir scratch;if(!scratch.isValid())return 2;
    const QString oldDirectory=QDir::currentPath();
    struct Restore{QString p;~Restore(){QDir::setCurrent(p);}}restore{oldDirectory};
    QDir::setCurrent(scratch.path());QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
    HiddenWindows hidden;qApp->installEventFilter(&hidden);
    QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){
        for(QWidget* w:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<CResourceSetupDialog*>(w))dialog->reject();
    });setup.start(1);qApp->setProperty("workspaceLoadSmoke",true);
    MainWindow window;setup.stop();window.setAttribute(Qt::WA_DontShowOnScreen);
    window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
    window.m_currentCollectionPath=scratch.filePath("explicit.collection");
    window.resize(1200,900);window.show();
    auto* view=window.view();view->show();QApplication::processEvents();view->makeCurrent();
    check(view->isValid(),"hidden native GL context");if(!view->isValid())return 3;
    CScene::getInstance()->changeMode(eEditModeObjects);
    auto* mob=new CMob;mob->setFileName(QFileInfo(scratch.filePath("bulk.mob")));
    mob->setRanges(true,{SRange(10000,20000)});
    for(int i=0;i<2000;++i){auto* n=new Module;n->setMapId(10000+i);QVector3D p(float(i%40),float(i/40),0);n->updatePos(p);
        n->applyParam(QSharedPointer<IPropertyBase>(new propStr(eObjParam_COMMENTS,QString(512,'x'))));mob->nodes()<<n;}
    QByteArray initial;mob->serializeMob(initial,false);QFile file(mob->filePath().absoluteFilePath());
    if(!file.open(QIODevice::WriteOnly)||file.write(initial)!=initial.size())return 4;file.close();
    mob->adoptCommittedBytes(initial);mob->attach(view,nullptr);view->m_aMob<<mob;view->m_activeMob=mob;
    view->m_clipboard_buffer_file.setFileName(scratch.filePath("clipboard.json"));
    out<<"FIXTURE nodes "<<mob->nodes().size()<<" bytes "<<initial.size()<<"\n";out.flush();
    for(int i=0;i<24;++i)mob->nodes()[i]->setState(eSelect);
    QElapsedTimer timer;timer.start();view->selectedObjectToClipboardBuffer();
    out<<"TIME copy24 "<<timer.elapsed()<<" ms\n";out.flush();
    QFile clipboard(view->m_clipboard_buffer_file.fileName());clipboard.open(QIODevice::ReadOnly);
    const auto copied=QJsonDocument::fromJson(clipboard.readAll()).object();clipboard.close();
    check(copied.value("Data").toArray().size()==24,"full copy command emits every selected raw record");
    out<<"COPY "<<QCryptographicHash::hash(QJsonDocument(copied).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()<<"\n";
    timer.restart();view->clipboradObjectsToScene();
    out<<"TIME paste24 "<<timer.elapsed()<<" ms\n";out.flush();
    check(view->m_pendingPasteNodes.size()==24,"full paste command creates complete preview");
    QMouseEvent release(QEvent::MouseButtonRelease,QPoint(320,240),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    timer.restart();view->m_pOp->mouseReleaseEvent(&release);
    out<<"TIME pasteCommit24 "<<timer.elapsed()<<" ms\n";out.flush();
    check(!view->hasPendingOperation() && view->m_pUndoStack->count()==1,"paste click commits one history action");
    check(mob->nodes().size()==2024,"committed paste preserves all existing and pasted nodes");
    QSet<uint> ids;bool unique=true;for(auto* n:mob->nodes()){unique&=!ids.contains(n->mapId());ids.insert(n->mapId());}
    check(unique && ids.size()==2024,"every pasted family ID is unique");
    timer.restart();view->m_pUndoStack->undo();out<<"TIME pasteUndo24 "<<timer.elapsed()<<" ms\n";out.flush();
    timer.restart();view->m_pUndoStack->redo();out<<"TIME pasteRedo24 "<<timer.elapsed()<<" ms\n";out.flush();
    QByteArray beforeMove;mob->serializeMob(beforeMove,false);
    view->clearObjectSelection();for(int i=0;i<24;++i)mob->nodes()[i]->setState(eSelect);
    view->operationSetBackup(eMove);QVector3D delta(1,2,0);view->moveTo(delta);
    timer.restart();view->operationApply(eMove);
    out<<"TIME moveCommit24 "<<timer.elapsed()<<" ms\n";out.flush();
    QByteArray afterMove;mob->serializeMob(afterMove,false);
    timer.restart();view->m_pUndoStack->undo();out<<"TIME moveUndo24 "<<timer.elapsed()<<" ms\n";out.flush();
    QByteArray undone;mob->serializeMob(undone,false);check(undone==beforeMove,"move Undo restores exact entire MOB projection");
    timer.restart();view->m_pUndoStack->redo();out<<"TIME moveRedo24 "<<timer.elapsed()<<" ms\n";out.flush();
    QByteArray redone;mob->serializeMob(redone,false);check(redone==afterMove,"move Redo restores exact entire MOB projection");
    out<<"MOVE "<<QCryptographicHash::hash(redone,QCryptographicHash::Sha256).toHex()<<"\n";
    CMprDocument source;CValidationReport report;QByteArray map;
    SMprCreationRequest request;request.innerNamespace="edit184";request.maxZ=100;
    request.flatRawHeight=0;request.sectorCountX=24;request.sectorCountY=24;
    if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&report)
       ||!source.buildNewCanvas(request,&map,&report))return 5;
    const QString mapPath=scratch.filePath("edit184.mpr");QFile mpr(mapPath);mpr.open(QIODevice::WriteOnly);mpr.write(map);mpr.close();
    view->makeCurrent();view->m_pLand->readMap(QFileInfo(mapPath),false);
    view->m_cam->setLocation({20,20,40},{20,20,0},{-45,0,0});
    view->resizeGL(view->width(),view->height());
    view->m_projection.setToIdentity();view->m_projection.perspective(45,float(view->width())/view->height(),1,1000);
    out<<"VIEWPORT "<<view->width()<<" "<<view->height()<<" "<<view->m_width<<" "<<view->m_height<<"\n";
    QByteArray hits;QDataStream hitStream(&hits,QIODevice::WriteOnly);
    QVector<QVector3D> terrainHits;
    // The legacy reader unprojects the pixel edge rather than its center.
    // Keep that subpixel convention identical; allow less than one world pixel.
    bool allHit=true;timer.restart();for(int i=0;i<20;++i){bool valid=false;const auto hit=view->getTerrainPos(view->width()/2-10+i,view->height()/2,true,&valid);allHit&=valid && qAbs(hit.z())<.1f;hitStream<<hit;terrainHits.append(hit);}
    out<<"TIME terrainQueries20 "<<timer.elapsed()<<" ms\n";out.flush();
    out<<"HITS "<<QCryptographicHash::hash(hits,QCryptographicHash::Sha256).toHex()<<"\n";
    for(int i:{0,10,19})out<<"HIT "<<i<<" "<<terrainHits[i].x()<<" "<<terrainHits[i].y()<<" "<<terrainHits[i].z()<<"\n";
    check(allHit,"every pointer query hits actual flat terrain at its correct height");
    QImage skin(8,8,QImage::Format_RGBA8888);skin.fill(QColor(210,30,20,255));
    for(int y=0;y<8;++y)for(int x=0;x<2;++x)skin.setPixelColor(x,y,QColor(0,0,0,0));
    QOpenGLTexture texture(skin);
    SPreparedStaticPart part;part.name="minimap-cutout";
    const QVector3D vertices[]={{0,0,1},{64,0,1},{64,64,1},{0,0,1},{64,64,1},{0,64,1}};
    const QVector2D uvs[]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
    for(int i=0;i<6;++i){SVertexData v;v.position=vertices[i];v.normal={0,0,1};v.texCoord=uvs[i];part.vertices.append(v);}
    for(int i=0;i<8;++i){auto* n=static_cast<CObjectBase*>(mob->nodes()[i]);n->installPreparedStaticParts({part});n->setTexture(&texture);n->setDrawPosition({100.f+80*i,200,0});}
    view->m_spatialIndexDirty=true;
    timer.restart();auto capture=view->captureMinimapScene(true,1024);
    out<<"TIME minimap "<<timer.elapsed()<<" ms\n";out.flush();
    check(capture.bake.success && capture.bake.gameImage.size()==QSize(1024,1024),"full quality minimap capture");
    const auto imageHash=[](const QImage& image){return QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),int(image.sizeInBytes())),QCryptographicHash::Sha256).toHex();};
    out<<"IMAGE lit "<<imageHash(capture.bake.gameImage)<<" unlit "<<imageHash(capture.unlitGameImage)<<"\n";out.flush();
#ifndef EI_EDIT_BASELINE
    auto raw=view->captureMinimapScene(true,1024,false);
    check(!raw.capturedReviewImage.isNull() && raw.capturedReviewImage.size()==QSize(3072,3072),"worker input retains full resolution capture");
    auto finished=CView::finishMinimapSceneCapture(raw);
    check(finished.bake.gameImage==capture.bake.gameImage && finished.unlitGameImage==capture.unlitGameImage,"split bake exactly equals synchronous lit and unlit images");
    check(finished.bake.reviewImage==capture.bake.reviewImage,"worker retains exact full resolution review pixels");
    QApplication::processEvents();view->updateGL();QApplication::processEvents();
    window.m_minimapRefreshTimer->stop();window.m_embeddedMinimapImage=QImage();
    ++window.m_minimapGeneration;window.m_minimapNeedsRefresh=true;
    timer.restart();window.refreshViewportMinimap();
    out<<"TIME automaticMinimapGui "<<timer.elapsed()<<" ms\n";out.flush();
    check(window.m_minimapCaptureRunning && window.m_minimapWorker,"minimap CPU processing is asynchronous");
    QElapsedTimer heartbeat;heartbeat.start();qint64 last=0,maxGap=0;int pulses=0;
    QTimer pulse;QObject::connect(&pulse,&QTimer::timeout,[&](){auto now=heartbeat.elapsed();maxGap=qMax(maxGap,now-last);last=now;++pulses;});pulse.start(5);
    const auto wait=[&](){QElapsedTimer limit;limit.start();while(window.m_minimapCaptureRunning && limit.elapsed()<15000)QCoreApplication::processEvents(QEventLoop::AllEvents,10);return !window.m_minimapCaptureRunning;};
    check(wait(),"automatic minimap completes");pulse.stop();
    out<<"HEARTBEAT pulses "<<pulses<<" maxGap "<<maxGap<<" ms\n";out.flush();
    const auto expected=gradeMinimapImage(capture.unlitGameImage,capture.bake.gameImage,window.m_minimapGrade);
    QString error;const auto expectedMmp=ei::encodeMmpDxt1(expected,&error);
    check(window.m_generatedMinimapImage==expected && window.m_generatedMinimapMmp==expectedMmp,"asynchronous minimap retains exact grading and encoded bytes");
    check(pulses>2 && maxGap<150,"UI heartbeat continues during minimap CPU work");
    const auto accepted=window.m_generatedMinimapMmp;
    ++window.m_minimapGeneration;window.m_minimapNeedsRefresh=true;window.refreshViewportMinimap();
    ++window.m_minimapGeneration;window.m_minimapNeedsRefresh=false;
    check(wait() && window.m_generatedMinimapMmp==accepted,"invalidated worker cannot publish its result");window.m_minimapRefreshTimer->stop();
    ++window.m_minimapGeneration;window.m_minimapNeedsRefresh=true;window.refreshViewportMinimap();
    window.m_mapTestEntry.zoneId="replaced-context";window.m_minimapNeedsRefresh=false;
    check(wait() && window.m_generatedMinimapMmp==accepted,"replaced workspace rejects previous minimap worker");window.m_minimapRefreshTimer->stop();
    window.m_mapTestEntry.zoneId.clear();
    ++window.m_minimapGeneration;window.m_minimapNeedsRefresh=true;window.refreshViewportMinimap();
    check(window.ensureGeneratedMinimap(),"explicit generation completes while older CPU job is pending");
    const auto explicitResult=window.m_generatedMinimapMmp;
    check(wait() && window.m_generatedMinimapMmp==explicitResult && !window.m_minimapNeedsRefresh,"older worker cannot overwrite explicit current image");window.m_minimapRefreshTimer->stop();
    window.m_embeddedMinimapImage=QImage(8,8,QImage::Format_ARGB32);window.m_embeddedMinimapImage.fill(Qt::green);
    window.m_viewportMinimap->setEmbeddedPreview("owned",window.m_embeddedMinimapImage,"synthetic");window.m_viewportMinimap->preferGenerated(false);
    window.scheduleViewportMinimapRefresh();
    check(!window.m_minimapRefreshTimer->isActive() && window.m_minimapNeedsRefresh,"embedded mode invalidates generation without unnecessary capture");
    check(window.ensureGeneratedMinimap() && !window.m_minimapNeedsRefresh,"explicit generation produces current complete minimap");
    window.setMinimapGenerationLocked(true);window.scheduleViewportMinimapRefresh(0,true);
    check(!window.m_minimapRefreshTimer->isActive(),"locked minimap remains unchanged");window.setMinimapGenerationLocked(false);window.m_minimapRefreshTimer->stop();
    view->operationSetBackup(eMove);view->m_pOp->changeState(new CMoveAxis(view,EOperateAxisXY));
    window.refreshViewportMinimap();check(!window.m_minimapCaptureRunning && window.m_minimapRefreshTimer->isActive(),"automatic minimap waits for unfinished pointer gesture");
    window.m_minimapRefreshTimer->stop();view->cancelPendingOperation();
#endif
    // Dispose temporary dirty state without invoking save/owner prompts.
    view->m_pUndoStack->clear();mob->setDirty(false);view->m_activeMob=nullptr;view->m_aMob.removeAll(mob);delete mob;
    window.m_minimapRefreshTimer->stop();qApp->removeEventFilter(&hidden);
    out<<"RESULT failures "<<failed<<"\n";out.flush();return failed?1:0;
}

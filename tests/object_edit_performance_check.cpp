#include <QtWidgets>
#include <QtOpenGL>
#define private public
#include "landscape.h"
#include "tile_form.h"
#undef private
#include "ui_tile_form.h"
#include "resource_profile.h"
#include "terrain_image_sizes.h"
#include "profile_database.h"
#include "database_document.h"
#include "database_editor_dialog.h"
#include "main_window.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "unit.h"
#include "magictrap.h"
#include "tree_view.h"
#include "undo.h"
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
#include <QThread>
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
    // Use the ordinary validation scheduler with a real, retained database.
    // The old workspace smoke flag masked this entire performance path.
    QSharedPointer<const ei::SProfileDatabaseSnapshot> databaseSnapshot;
    for(const auto& profile : loadResourceProfiles()) if(profile.id == "deja-vu") {
        const auto evaluated=evaluateResourceProfile("C:/GOG Games/PZ",profile);
        if(!evaluated.valid)return 21;
        ei::SProfileDatabaseLoadRequest request;const auto provider=evaluated.providers.value("database");
        request.providerId=provider.id;request.providerInterfaceVersion=provider.interfaceVersion;
        request.profileId=evaluated.profileId;request.runtimeManifestId=evaluated.runtimeManifestId;
        request.resourceDirectories=evaluated.resourceDirectories;
        ei::ProfileDatabaseService service;databaseSnapshot=service.snapshot(request);
    }
    QString databaseError;
    window.m_databaseDocument=ei::DatabaseDocument::fromSnapshot(databaseSnapshot,&databaseError);
    check(!window.m_databaseDocument.isNull(),"ordinary session retains actual DejaVu database");
    if(window.m_databaseDocument.isNull())return 22;
    ei::ProfileDatabaseContext::instance().activate(databaseSnapshot);
    qApp->setProperty("workspaceLoadSmoke",false);
    window.m_validationTimer->stop();window.m_autosaveTimer->stop();
    QElapsedTimer panelTimer;panelTimer.start();window.updateMobFamilyValidationPanel();
    out<<"TIME databasePanelCold "<<panelTimer.elapsed()<<" ms\n";
    panelTimer.restart();for(int i=0;i<3;++i)window.updateMobFamilyValidationPanel();
    out<<"TIME databasePanelWarm3 "<<panelTimer.elapsed()<<" ms\n";out.flush();
    check(panelTimer.elapsed()<180,"unchanged database panel avoids repeated full validation");
    for(int i=0;i<8;++i)window.m_undoStack->push(new CSelectionCommand(view,{}, {mob->nodes()[i]},"selection"));
    check(!window.m_validationTimer->isActive(),"selection history does not schedule family validation with database open");
    window.m_undoStack->setIndex(0);
    check(!window.m_validationTimer->isActive(),"multi-step selection Undo does not schedule family validation");
    window.m_undoStack->clear();view->clearObjectSelection();
#ifndef EI_EDIT_BASELINE
    // A real database transaction, followed by native history Undo/Redo, must
    // still schedule family validation and invalidate only the panel revision.
    window.showDatabaseEditor();window.m_databaseEditorDialog->hide();
    const auto* monsters=window.m_databaseDocument->table("monsters");
    check(monsters && !monsters->rows.isEmpty(),"database edit fixture");
    const QString row=monsters->rows.first().stableKey;
    const auto beforeRevision=window.m_databaseDocument->stateRevision();
    const double value=monsters->rows.first().values.value("complexionX").toDouble();
    check(window.m_databaseDocument->setValue("monsters",row,"complexionX",value+.01,&databaseError),"actual database edit accepted");
    window.m_databaseEditorDialog->transactionCommitted("test complexion","monsters",row);
    check(window.m_validationTimer->isActive(),"database history push retains validation trigger");window.m_validationTimer->stop();
    window.updateMobFamilyValidationPanel();
    check(window.m_validatedDatabaseRevision!=beforeRevision,"database panel invalidates on actual edit");
    window.m_undoStack->undo();check(window.m_validationTimer->isActive(),"database Undo retains validation trigger");window.m_validationTimer->stop();
    window.updateMobFamilyValidationPanel();check(window.m_validatedDatabaseRevision==beforeRevision,"database Undo refreshes panel revision");
    window.m_undoStack->redo();check(window.m_validationTimer->isActive(),"database Redo retains validation trigger");window.m_validationTimer->stop();
    window.updateMobFamilyValidationPanel();check(window.m_validatedDatabaseRevision==window.m_databaseDocument->stateRevision(),"database Redo refreshes panel revision");
    window.m_undoStack->undo();window.m_undoStack->clear();window.m_validationTimer->stop();
    window.m_undoStack->setUndoLimit(2);
    window.m_undoStack->push(new CSelectionCommand(view,{}, {},"selection1"));
    window.m_undoStack->push(new CSelectionCommand(view,{}, {},"selection2"));
    check(window.m_databaseDocument->setValue("monsters",row,"complexionX",value+.02,&databaseError),"database edit at history limit");
    window.m_databaseEditorDialog->transactionCommitted("limit edit","monsters",row);
    check(window.m_undoStack->count()==2 && window.m_validationTimer->isActive(),"database push at fixed full history index requests validation");window.m_validationTimer->stop();
    window.m_undoStack->setIndex(0);check(window.m_validationTimer->isActive(),"multi-step Undo crossing database command requests validation");window.m_validationTimer->stop();
    window.m_undoStack->setIndex(2);check(window.m_validationTimer->isActive(),"multi-step Redo crossing database command requests validation");window.m_validationTimer->stop();
    window.m_undoStack->undo();window.m_undoStack->clear();window.m_undoStack->setUndoLimit(512);window.m_validationTimer->stop();
#endif
    // Exercise the actual Delete entrypoint and native tree, including
    // non-adjacent indices: reverse Undo must restore the exact original order.
    const auto originalNodes=mob->nodes();
    QList<CNode*> deletedNodes,remainingNodes;
    for(int i=0;i<originalNodes.size();++i) {
        const bool selected=i<24*7 && i%7==0;
        originalNodes[i]->setState(selected?eSelect:eDraw);
        (selected?deletedNodes:remainingNodes).append(originalNodes[i]);
    }
    view->collectObjectTreeData();
    int publications=0,treePublications=0;
    auto changed=QObject::connect(view,&CView::mobStateChanged,[&](){++publications;});
    auto treeChanged=QObject::connect(view->m_pTree,&CTreeView::filterMatchCountChanged,[&](int,int){++treePublications;});
    QElapsedTimer deletionTimer;deletionTimer.start();view->deleteSelectedNodes();
    out<<"TIME delete24 "<<deletionTimer.elapsed()<<" ms\n";
    const auto generation=window.m_validationGeneration;
    int validationTimeouts=0;auto validationConnection=QObject::connect(window.m_validationTimer,&QTimer::timeout,[&](){++validationTimeouts;});
    QElapsedTimer ordinaryWait;ordinaryWait.start();while(ordinaryWait.elapsed()<550){QApplication::processEvents();QThread::msleep(1);}
    check(validationTimeouts==0 && window.m_validationGeneration==generation,"dirty Delete remains responsive after normal validation debounce");
    QObject::disconnect(validationConnection);

    out<<"PUBLICATIONS delete "<<publications<<" tree "<<treePublications<<"\n";out.flush();
    check(mob->nodes()==remainingNodes && view->m_pUndoStack->count()==1,"group Delete removes exactly selected nodes in one history action");
    bool missingRows=true;for(auto* n:deletedNodes)missingRows &= !view->m_pTree->rowForNode(n);
    check(missingRows && mob->isDirty(),"Delete publishes current tree and dirty state");
    check(aiGraphUndoCommandInvalidatesPreview(view->m_pUndoStack->command(0)),"Delete batch retains recursive navigation invalidation");
#ifndef EI_EDIT_BASELINE
    check(publications==1 && treePublications==1,"group Delete publishes MOB and tree once");
#endif
    QByteArray deletedProjection;mob->serializeMob(deletedProjection,false);
    QByteArray deletionCandidate;CValidationReport deletionReport;QString deletionClass;
    check(mob->buildValidatedCandidate(&deletionCandidate,&deletionReport,&deletionClass)
          && deletionCandidate==deletedProjection,"deleted objects produce exact validated raw-sequence save candidate");
    CMobDocument deletedDocument;
    check(deletedDocument.loadBytes(deletionCandidate,"deleted-reopen.mob",&deletionReport),"fresh document reopen validates deletion candidate");
    publications=treePublications=0;deletionTimer.restart();view->m_pUndoStack->undo();
    out<<"TIME deleteUndo24 "<<deletionTimer.elapsed()<<" ms\n";
    out<<"PUBLICATIONS undo "<<publications<<" tree "<<treePublications<<"\n";
    QByteArray restoredProjection;mob->serializeMob(restoredProjection,false);
    check(mob->nodes()==originalNodes && restoredProjection==initial && !mob->isDirty(),"Delete Undo restores exact pointers, ordering, bytes and clean state");
#ifndef EI_EDIT_BASELINE
    check(publications==1 && treePublications==1,"group Delete Undo publishes MOB and tree once");
#endif
    publications=treePublications=0;deletionTimer.restart();view->m_pUndoStack->redo();
    out<<"TIME deleteRedo24 "<<deletionTimer.elapsed()<<" ms\n";
    out<<"PUBLICATIONS redo "<<publications<<" tree "<<treePublications<<"\n";
    QByteArray deletedAgain;mob->serializeMob(deletedAgain,false);
    check(deletedAgain==deletedProjection && mob->nodes()==remainingNodes,"Delete Redo restores exact deleted projection and order");
#ifndef EI_EDIT_BASELINE
    check(publications==1 && treePublications==1,"group Delete Redo publishes MOB and tree once");
#endif
    out<<"DELETE "<<QCryptographicHash::hash(deletedAgain,QCryptographicHash::Sha256).toHex()<<"\n";
    view->m_pUndoStack->undo();view->m_pUndoStack->clear();view->clearObjectSelection();
    QObject::disconnect(changed);QObject::disconnect(treeChanged);

    // Script references must still be confirmed before opening a batch or
    // mutating history; test both outcomes without showing a desktop dialog.
    mob->setScript("RemoveObject(GetObject(10000));");
    view->restoreSelection({originalNodes[0]});
    view->m_pUndoStack->clear();
    bool warningSeen=false;int answer=QMessageBox::Cancel;
    QTimer confirmation;QObject::connect(&confirmation,&QTimer::timeout,[&](){
        for(QWidget* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)) {
            warningSeen=true;box->done(answer);
        }
    });confirmation.start(1);
    view->deleteSelectedNodes();
    check(warningSeen && mob->nodes()==originalNodes && view->m_pUndoStack->count()==0,"script-referenced Delete Cancel preserves objects and history");
    warningSeen=false;answer=QMessageBox::Yes;view->deleteSelectedNodes();confirmation.stop();
    check(warningSeen && mob->nodes().size()==1999 && view->m_pUndoStack->count()==1,"confirmed single Delete preserves script check");
    view->m_pUndoStack->undo();
    check(mob->nodes()==originalNodes && mob->script()=="RemoveObject(GetObject(10000));","single Delete Undo retains original ID, position and script");
    view->m_pUndoStack->clear();mob->setScript("");mob->refreshDirtyFromProjection();view->clearObjectSelection();
#ifndef EI_EDIT_BASELINE
    view->m_pUndoStack->clear();
    auto* inactive=new CMob;inactive->setFileName(QFileInfo(scratch.filePath("inactive.mob")));
    auto* inactiveNode=new Module;inactiveNode->setMapId(60000);inactive->nodes()<<inactiveNode;
    inactive->captureCleanProjection();inactive->attach(view,nullptr);view->m_aMob<<inactive;
    originalNodes[3]->setState(eSelect);originalNodes[8]->setState(eSelect);view->deleteSelectedNodes();
    check(view->m_activeMob==mob && inactive->nodes().size()==1 && !inactive->isDirty(),"Delete leaves inactive MOB and active ownership unchanged");
    view->m_activeMob=inactive;view->m_pUndoStack->undo();
    check(view->m_activeMob==mob && mob->nodes()==originalNodes && !mob->isDirty() && !inactive->isDirty(),"ordinary Delete Undo after MOB switch restores exact owner without dirtying other maps");
    view->m_pUndoStack->redo();view->m_pUndoStack->undo();view->m_pUndoStack->clear();
    view->clearObjectSelection();view->m_pUndoStack->clear();view->deleteSelectedNodes();
    check(view->m_pUndoStack->count()==0 && mob->nodes()==originalNodes,"empty Delete creates no history action");
    view->m_aMob.removeOne(inactive);delete inactive;
#endif

    // Use an independent MOB to cover patrol/look/activation/cast children
    // together and history after switching the active document.
    auto* logicMob=new CMob;logicMob->setFileName(QFileInfo(scratch.filePath("logic.mob")));
    auto* unit=new CUnit;unit->setMapId(50001);unit->addFirstPatrolPoint();
    auto* patrol=unit->logicAt(0)->patrolPoints().first();patrol->addFirstViewPoint();
    auto* look=patrol->lookPoint().first();
    auto* trap=new CMagicTrap;trap->setMapId(50002);
    auto* zone=trap->createActZone();auto* cast=trap->createCastPoint();
    logicMob->nodes()<<unit<<trap;logicMob->logicNodesUpdate();
    QByteArray logicBefore;logicMob->serializeMob(logicBefore,false);logicMob->captureCleanProjection();
    logicMob->attach(view,nullptr);view->m_aMob<<logicMob;view->m_activeMob=logicMob;
    CScene::getInstance()->changeMode(eEditModeLogic);view->setLogicSlotIndex(0);
    view->clearObjectSelection();for(auto* n:QList<CNode*>{patrol,look,zone,cast})n->setState(eSelect);
    view->m_pUndoStack->clear();
    view->deleteSelectedNodes();
    out<<"LOGIC history "<<view->m_pUndoStack->count()<<" flags "<<patrol->isMarkDeleted()<<look->isMarkDeleted()<<zone->isMarkDeleted()<<cast->isMarkDeleted()<<"\n";
    check(view->m_pUndoStack->count()==1 && patrol->isMarkDeleted() && look->isMarkDeleted() && zone->isMarkDeleted() && cast->isMarkDeleted(),"mixed logic-point Delete marks all four kinds in one action");
    view->m_activeMob=mob;view->m_pUndoStack->undo();
    QByteArray logicUndo;logicMob->serializeMob(logicUndo,false);
    check(logicUndo==logicBefore && !logicMob->isDirty() && view->m_activeMob==logicMob,"logic Delete Undo restores owning MOB bytes after active document switch");
    view->m_pUndoStack->redo();
    check(patrol->isMarkDeleted() && look->isMarkDeleted() && zone->isMarkDeleted() && cast->isMarkDeleted(),"logic Delete Redo retains exact targets");
    view->m_pUndoStack->undo();view->m_pUndoStack->clear();
#ifndef EI_EDIT_BASELINE
    view->clearObjectSelection();view->m_pUndoStack->clear();unit->setState(eSelect);patrol->setState(eSelect);
    view->deleteSelectedNodes();
    check(!logicMob->nodes().contains(unit) && patrol->isMarkDeleted(),"mixed owner and child Delete applies logic child before removing owner");
    view->m_pUndoStack->undo();QByteArray ownerUndo;logicMob->serializeMob(ownerUndo,false);
    check(logicMob->nodes().first()==unit && ownerUndo==logicBefore && !logicMob->isDirty(),"mixed owner-child Undo restores original hierarchy and bytes");
    view->m_pUndoStack->clear();
#endif
    view->m_aMob.removeOne(logicMob);delete logicMob;view->m_activeMob=mob;
    CScene::getInstance()->changeMode(eEditModeObjects);view->clearObjectSelection();
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
    // Mixed native-size cache: repeated changes, no-op, Undo and Redo must
    // retain GL publication while matching a fresh full upload at every mip.
    view->makeCurrent();auto atlas=CLandscape::createReadOnlyBackground();
    QImage sd(128,128,QImage::Format_RGBA8888);sd.fill(QColor(30,90,50));
    QImage hd(256,256,QImage::Format_RGBA8888);hd.fill(QColor(120,100,80));
    QVector<QImage> mixed(128,sd);for(int i=64;i<128;++i)mixed[i]=hd;
    check(atlas->previewTerrainAtlasIfChanged(mixed,2),"mixed SD/HD initial publication");
    const auto textureId=atlas->m_previewAtlasTexture->textureId();
    CTileForm picker;picker.setAttribute(Qt::WA_DontShowOnScreen);picker.resize(400,600);picker.show();
    QVector<qint32> types(128,0);check(picker.adoptTileImages(mixed,types),"mixed inspector initial publication");
    picker.selectTile(3);auto* item=picker.ui->tableTile->item(0,3);
    const auto delegates=picker.findChildren<QAbstractItemDelegate*>().size();
    const auto iconKey=item->icon().cacheKey();
    auto changedMixed=mixed;changedMixed[65]=hd.copy();changedMixed[65].fill(QColor(180,80,20));
    qint64 changedTotal=0;
    for(const auto& images:{changedMixed,mixed,changedMixed}) {
        timer.restart();check(atlas->previewTerrainAtlasIfChanged(images,2),"mixed change / Undo / Redo publishes");changedTotal+=timer.elapsed();
        check(atlas->m_previewAtlasTexture->textureId()==textureId && atlas->m_previewDisplayImages.size()==128,"mixed publication retains GL and normalized cache");
        const auto actual=atlas->m_previewAtlasTexture->textureId();auto fresh=CLandscape::createReadOnlyBackground();
        check(fresh->previewTerrainAtlasIfChanged(images,2),"fresh comparison atlas");
        auto pixels=[&](GLuint id,int level){auto* gl=QOpenGLContext::currentContext()->functions();gl->glBindTexture(GL_TEXTURE_2D,id);GLint w=0,h=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&h);QByteArray data(w*h*4,0);glGetTexImage(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,data.data());return data;};
        bool exact=true;for(int level=0;level<atlas->m_previewAtlasTexture->mipLevels();++level)exact&=pixels(actual,level)==pixels(fresh->m_previewAtlasTexture->textureId(),level);
        check(exact,"mixed incremental GPU pixels equal fresh full atlas at all mip levels");
        check(picker.adoptTileImages(images,types) && picker.ui->tableTile->item(0,3)==item && item->icon().cacheKey()==iconKey,"inspector preserves unchanged icon and selected item");
    }
    out<<"TIME mixedPublications3 "<<changedTotal<<" ms\n";
    check(picker.findChildren<QAbstractItemDelegate*>().size()==delegates,"inspector delegate count remains bounded");
    check(atlas->previewTerrainAtlasIfChanged(changedMixed,2) && atlas->m_previewAtlasTexture->textureId()==textureId,"mixed no-op retains publication");
    check(picker.ui->tableTile->currentRow()==0 && picker.ui->tableTile->currentColumn()==3,"inspector preserves selection through publication");
    QVector<QImage> uniform(128,hd);uniform[7]=hd.copy();uniform[7].fill(QColor(50,25,100));
    check(atlas->previewTerrainAtlasIfChanged(uniform,2),"mixed to uniform atlas publication");
    auto mixedAgain=uniform;mixedAgain[0]=sd;
    check(atlas->previewTerrainAtlasIfChanged(mixedAgain,2),"uniform to mixed atlas publication");
    const auto expectedMixed=terrainDisplayTiles(mixedAgain);
    check(atlas->m_previewDisplayImages==expectedMixed,"uniform roundtrip cannot reuse stale normalized pixels");
    const auto expectedGrid=CTerrainVirtualAtlas::composeAtlasGrid(expectedMixed,2,atlas->m_previewAtlasColumns);
    QImage mixedPixels(expectedGrid.size(),QImage::Format_RGBA8888);atlas->m_previewAtlasTexture->bind();
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,mixedPixels.bits());
    check(mixedPixels==expectedGrid,"uniform roundtrip GPU pixels match current native images");
    const auto retainedImages=atlas->m_previewAtlasImages;const auto retainedDisplay=atlas->m_previewDisplayImages;
    auto invalid=mixedAgain;invalid[1]=QImage();
    check(!atlas->previewTerrainAtlasIfChanged(invalid,2) && atlas->m_previewAtlasImages==retainedImages && atlas->m_previewDisplayImages==retainedDisplay,"invalid candidate retains previous valid publication");
    atlas->clearPreviewTerrainAtlas();check(atlas->m_previewAtlasImages.isEmpty() && atlas->m_previewDisplayImages.isEmpty(),"atlas reset invalidates both snapshots");
    auto grown=mixed;grown+=mixed;check(atlas->previewTerrainAtlasIfChanged(grown,4),"layout growth rebuilds valid mixed atlas");
    check(atlas->previewTerrainAtlasIfChanged(mixed,2),"layout shrink rebuilds valid mixed atlas");
    atlas->clearPreviewTerrainAtlas();atlas.clear();
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
    // Visible alpha-cutout modules must disappear from the full quality
    // minimap and return byte-for-byte after Undo, including encoded MMP.
    view->clearObjectSelection();view->m_pUndoStack->clear();
    auto* visibleA=mob->nodes()[0];auto* visibleB=mob->nodes()[1];
    visibleA->setState(eSelect);visibleB->setState(eSelect);
    const auto generationBeforeDelete=window.m_minimapGeneration;
    view->deleteSelectedNodes();
    auto removedCapture=view->captureMinimapScene(true,1024);
    check(removedCapture.bake.success && removedCapture.bake.gameImage!=capture.bake.gameImage
          && window.m_minimapGeneration>generationBeforeDelete,"Delete invalidates automatic minimap and removes visible geometry at full resolution");
    view->m_pUndoStack->undo();auto restoredCapture=view->captureMinimapScene(true,1024);
    check(restoredCapture.bake.gameImage==capture.bake.gameImage
          && restoredCapture.unlitGameImage==capture.unlitGameImage
          && restoredCapture.bake.reviewImage==capture.bake.reviewImage,"Delete Undo restores exact lit, unlit and full review minimap pixels");
    view->m_pUndoStack->redo();auto redoneCapture=view->captureMinimapScene(true,1024);
    const auto gradeDeleted=gradeMinimapImage(removedCapture.unlitGameImage,removedCapture.bake.gameImage,window.m_minimapGrade);
    const auto gradeRedone=gradeMinimapImage(redoneCapture.unlitGameImage,redoneCapture.bake.gameImage,window.m_minimapGrade);
    QString deleteMmpError;
    check(redoneCapture.bake.gameImage==removedCapture.bake.gameImage
          && gradeDeleted==gradeRedone
          && ei::encodeMmpDxt1(gradeDeleted,&deleteMmpError)==ei::encodeMmpDxt1(gradeRedone,&deleteMmpError),"Delete Redo preserves exact minimap grading and encoded MMP bytes");
    view->m_pUndoStack->undo();view->m_pUndoStack->clear();
    auto raw=view->captureMinimapScene(true,1024,false);
    check(!raw.capturedReviewImage.isNull() && raw.capturedReviewImage.size()==QSize(3072,3072),"worker input retains full resolution capture");
    auto finished=CView::finishMinimapSceneCapture(raw);
    check(finished.bake.gameImage==capture.bake.gameImage && finished.unlitGameImage==capture.unlitGameImage,"split bake exactly equals synchronous lit and unlit images");
    check(finished.bake.reviewImage==capture.bake.reviewImage,"worker retains exact full resolution review pixels");
    QApplication::processEvents();view->updateGL();QApplication::processEvents();
    // Delete/Undo above can already have started an automatic capture. Drain
    // that invalidated job before timing a fresh generation for the final state.
    QElapsedTimer drain;drain.start();
    while(window.m_minimapCaptureRunning && drain.elapsed()<15000) {
        window.m_minimapRefreshTimer->stop();QCoreApplication::processEvents(QEventLoop::AllEvents,10);
    }
    check(!window.m_minimapCaptureRunning,"previous deletion minimap worker finishes before fresh-generation comparison");
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

    view->clearObjectSelection();view->m_pUndoStack->clear();
    QByteArray beforeSavedDelete;mob->serializeMob(beforeSavedDelete,false);
    const auto beforeSavedNodes=mob->nodes();const uint heldId=beforeSavedNodes[0]->mapId();
    beforeSavedNodes[0]->setState(eSelect);view->deleteSelectedNodes();
    check(mob->save() && !mob->isDirty(),"Delete saves through real atomic MOB save and becomes clean");
    QFile savedMob(mob->filePath().absoluteFilePath());savedMob.open(QIODevice::ReadOnly);
    const auto savedBytes=savedMob.readAll();savedMob.close();CMobDocument savedDocument;CValidationReport savedReport;
    check(savedDocument.loadBytes(savedBytes,mob->filePath().absoluteFilePath(),&savedReport)
          && savedDocument.objects().size()==mob->nodes().size(),"saved Delete reopens with the exact retained object count");
    const auto nextId=window.allocateFamilyNid(mob->filePath().absoluteFilePath());
    out<<"ALLOCATE afterSavedDelete held "<<heldId<<" next "<<nextId<<"\n";
    check(nextId!=0 && nextId!=heldId,"deleted ID retained by Undo is unavailable to family allocation after saving deletion");
    view->m_pUndoStack->undo();QByteArray savedDeleteUndo;mob->serializeMob(savedDeleteUndo,false);
    check(mob->nodes()==beforeSavedNodes && savedDeleteUndo==beforeSavedDelete && mob->isDirty(),"Undo after saving deletion restores exact original IDs, bytes and dirty state");
    view->m_pUndoStack->redo();
    check(!mob->isDirty(),"Redo after saving deletion returns to saved clean state");
#endif
    // Dispose temporary dirty state without invoking save/owner prompts.
    view->m_pUndoStack->clear();mob->setDirty(false);view->m_activeMob=nullptr;view->m_aMob.removeAll(mob);delete mob;
    window.m_minimapRefreshTimer->stop();qApp->removeEventFilter(&hidden);
    out<<"RESULT failures "<<failed<<"\n";out.flush();return failed?1:0;
}

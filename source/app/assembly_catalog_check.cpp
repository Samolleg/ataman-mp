#include "assembly_catalog_check.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "light.h"
#include "tree_view.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "scene.h"
#include "operationmanager.h"
#include "profile_database.h"
#include "endurance_document.h"
#include "property.h"
#include "assembly_gallery.h"
#include "model_gallery_thumbnail.h"
#include "main_window.h"
#include "camera.h"
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextStream>
#include <QSignalBlocker>
#include <QPushButton>
#include <QListWidget>
#include <QComboBox>
#include <QTabBar>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QApplication>
#include <QElapsedTimer>
namespace {
class Module:public CWorldObj {
public:
    Module(){m_modelName="synthetic_wall";m_primaryTexture="synthetic_skin";}
    void setRot(const QQuaternion& q) override {CWorldObj::setRot(q);auto p=position();updatePos(p);}
    bool updatePos(QVector3D& p) override {setPos(p);setDrawPosition(p+QVector3D(0,0,p.x()*0.3f+p.y()*0.2f));return true;}
};
bool positionsNear(const QVector3D& a,const QVector3D& b){return (a-b).length()<0.002f;}
}
int runAssemblyCatalogCheck() {
    QTextStream out(stdout);int failures=0;auto check=[&](bool ok,const char* name){out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures;};
    QTemporaryDir scratch;if(!scratch.isValid())return 2;QString error;
    auto original=ei::ProfileDatabaseContext::instance().current();struct Guard{QSharedPointer<const ei::SProfileDatabaseSnapshot> db;~Guard(){ei::ProfileDatabaseContext::instance().publishEditedSnapshot(db);}} guard{original};
    QSharedPointer<ei::SProfileDatabaseSnapshot> db(new ei::SProfileDatabaseSnapshot);db->sourceArchivePath=scratch.filePath("database.res");ei::ProfileDatabaseContext::instance().publishEditedSnapshot(db);
    CSettings settings;QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);CProgressView progress;QLineEdit coords;CTileForm tiles;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.attach(&settings,&properties,&history,&progress,&coords,&tree,&tiles);
    auto* source=new CMob;source->setFileName(QFileInfo(scratch.filePath("source.mob")));auto* a=new Module;auto* b=new Module;
    a->setMapId(40);b->setMapId(41);source->nodes()<<a<<b;view.m_aMob<<source;view.m_activeMob=source;
    QVector3D pa(3,5,1),pb(7,5,4);a->updatePos(pa);b->updatePos(pb);
    QSharedPointer<IPropertyBase> parent(new propInt(eObjParam_PARENT_ID,40));b->applyParam(parent);
    QByteArray baseline;source->serializeMob(baseline,false);source->adoptCommittedBytes(baseline);
    {QSignalBlocker block(CScene::getInstance());CScene::getInstance()->changeMode(eEditModeObjects);}
    a->setState(eSelect);b->setState(eSelect);ei::ObjectAssembly assembly;
    check(view.captureSelectedAssembly(QString::fromUtf8("Дом из двух частей"),&assembly,&error),"capture full records from selected architecture");
    if(assembly.parts.size()!=2){out<<error<<"\n";return 1;}
    check(assembly.parts[1].parentPart==0 && positionsNear(assembly.parts[1].offset-assembly.parts[0].offset,b->drawPosition()-a->drawPosition()),"capture uses world positions on slope and maps internal parent by part index");
    auto extra=ei::writeObjectAssembly(assembly);extra.insert("authorNote","unknown metadata retained");ei::ObjectAssembly authored;
    check(ei::readObjectAssembly(extra,&authored,&error),"assembly schema reads unknown author metadata");assembly=authored;
    ei::TechnicalModelCatalog catalog;catalog.load(scratch.filePath("model-catalog.json"),&error);catalog.setRussianName("synthetic_wall",QString::fromUtf8("Стена"));
    check(catalog.setAssembly(assembly,&error)&&catalog.save(&error),"catalog atomically saves assembly beside profile database");
    ei::TechnicalModelCatalog reopened;reopened.load(catalog.path(),&error);auto entries=reopened.assemblies(&error);
    check(entries.size()==1 && entries[0].extra.value("authorNote")=="unknown metadata retained" && entries[0].parts[1].object==assembly.parts[1].object,"fresh JSON reopen retains all object fields and unknown metadata");
    auto invalid=assembly;invalid.parts[0].parentPart=1;
    check(!catalog.setAssembly(invalid,&error)&&catalog.assemblies().size()==1,"cyclic parent links reject catalog mutation");
    invalid=assembly;invalid.parts[0].object.remove("_Endurance raw provenance");check(!catalog.setAssembly(invalid,&error),"missing full MOB template rejects assembly");
    view.groupSelectedAssembly(assembly.name,assembly.id);check(history.count()==1&&!a->assemblyTag().isEmpty()&&a->assemblyTag()==b->assemblyTag(),"group selection creates one undo action without gameplay Parent ID changes");
    check(source->refreshDirtyFromProjection(),"editor membership makes MOB pending-save even when binary fields unchanged");
    history.undo();check(a->assemblyTag().isEmpty()&&b->assemblyTag().isEmpty()&&!source->refreshDirtyFromProjection(),"undo grouping restores clean state");history.redo();
    view.restoreSelection({});view.selectNode(a,false,false);check(a->nodeState()==eSelect&&b->nodeState()==eSelect,"selecting one assembly component selects whole group");
    view.selectNode(a,true,false);check(a->nodeState()==eDraw&&b->nodeState()==eDraw,"Shift toggle deselects whole group");view.selectNode(b,false,false);
    auto beforeA=a->drawPosition(),beforeB=b->drawPosition();view.operationSetBackup(eMove);QVector3D delta(12,9,2);view.moveTo(delta);
    check(positionsNear(a->drawPosition(),beforeA+delta)&&positionsNear(b->drawPosition(),beforeB+delta),"group move retains world geometry across sloping terrain");view.operationRevert(eMove);view.m_operationBackup.clear();
    view.operationSetBackup(eRotate);QVector3D rotate(0,0,90);view.rotateTo(rotate);
    check(qAbs((a->drawPosition()-b->drawPosition()).length()-(beforeA-beforeB).length())<0.002f && positionsNear((a->drawPosition()+b->drawPosition())*.5f,(beforeA+beforeB)*.5f),"group rotation preserves distances and one world-space pivot on slope");
    view.operationRevert(eRotate);view.m_operationBackup.clear();
    check(positionsNear(a->drawPosition(),beforeA)&&positionsNear(b->drawPosition(),beforeB),"cancel rotation restores whole assembly");
    view.disassembleSelection();check(a->assemblyTag().isEmpty()&&b->assemblyTag().isEmpty(),"disassemble returns independent components");history.undo();check(!a->assemblyTag().isEmpty()&&a->assemblyTag()==b->assemblyTag(),"Undo disassembly restores exact membership");
    auto* destination=new CMob;destination->setFileName(QFileInfo(scratch.filePath("placed.mob")));CLight existing;existing.setMapId(1);destination->nodes()<<new CLight(existing);
    QByteArray empty;destination->serializeMob(empty,false);destination->adoptCommittedBytes(empty);view.m_aMob<<destination;view.m_activeMob=destination;view.restoreSelection({destination->nodes().first()});history.clear();
    const auto originalNode=destination->nodes().first();const QVector3D anchor(10,10,8);
    check(view.beginPreparedAssemblyPlacement(assembly,{300,200},anchor,&error)&&destination->nodes().size()==3&&history.count()==0,"placement preview creates all parts outside history");
    auto pasted=view.m_pendingPasteNodes;check(pasted.size()==2&&pasted[0]->mapId()!=pasted[1]->mapId()&&pasted[0]->mapId()!=1&&pasted[1]->mapId()!=1,"allocate globally free IDs including existing light");
    if(pasted.size()==2) {
        check(positionsNear(pasted[0]->drawPosition(),anchor+assembly.parts[0].offset)&&positionsNear(pasted[1]->drawPosition(),anchor+assembly.parts[1].offset),"placement uses one common world frame");
        check(ei::assemblyWorldObject(pasted[1]->toJson()).value("Parent Id").toVariant().toUInt()==pasted[0]->mapId(),"internal gameplay parent remaps to newly allocated ID");
    }
    view.m_pOp->keyPress(eKey_Esc);check(destination->nodes().size()==1&&history.count()==0&&originalNode->nodeState()==eSelect&&!view.m_pendingAssemblyPlacement,"Escape discards all preview parts and restores original selection");
    const bool galleryBatch=view.beginPreparedAssemblyPlacement(assembly,{300,200},anchor,&error,false);
    auto galleryBatchNodes=view.m_pendingPasteNodes;
    check(galleryBatch&&galleryBatchNodes.size()==2&&galleryBatchNodes[0]->assemblyTag().isEmpty()&&galleryBatchNodes[1]->assemblyTag().isEmpty(),"gallery galleryBatch remains independent objects");
    if(galleryBatch)view.m_pOp->keyPress(eKey_Enter);
    check(galleryBatch&&history.count()==1&&destination->nodes().size()==3,"ungrouped galleryBatch commits as one action");
    history.undo();check(destination->nodes().size()==1,"ungrouped galleryBatch undo removes every object");
    history.redo();check(destination->nodes().size()==3&&galleryBatchNodes[0]->assemblyTag().isEmpty(),"ungrouped galleryBatch redo preserves independence");
    history.undo();history.clear();
    const bool pending=view.beginPreparedAssemblyPlacement(assembly,{300,200},anchor,&error);pasted=view.m_pendingPasteNodes;
    check(pending,"assembly can be placed again after cancellation");if(pending)view.m_pOp->keyPress(eKey_Enter);
    check(pending&&history.count()==1&&destination->nodes().size()==3&&!view.m_pendingAssemblyPlacement,"confirm placement is one undo action for the complete group");
    history.undo();check(destination->nodes().size()==1&&originalNode->nodeState()==eSelect,"one Undo removes all members and restores prior selection");history.redo();
    check(destination->nodes().size()==3&&pasted.size()==2&&!pasted[0]->assemblyTag().isEmpty()&&pasted[0]->assemblyTag()==pasted[1]->assemblyTag(),"one Redo restores exact group and member identities");
    QByteArray candidate;CValidationReport report;const bool validated=destination->buildValidatedCandidate(&candidate,&report);
    check(validated,"complete assembled MOB passes lossless creation validation");
    QFile output(destination->filePath().absoluteFilePath());bool written=output.open(QIODevice::WriteOnly)&&output.write(candidate)==candidate.size();output.close();if(written)destination->adoptCommittedBytes(candidate);
    check(written&&destination->saveAssemblyInstances(destination->filePath().absoluteFilePath(),&error),"group metadata persists beside database without extra game MOB fields");
    CMob restored;restored.setFileName(destination->filePath());const bool loaded=restored.deserialize(candidate);if(loaded){restored.adoptCommittedBytes(candidate);restored.restoreAssemblyInstances();}
    check(loaded&&pasted.size()==2&&restored.nodeByMapId(pasted[0]->mapId())&&restored.nodeByMapId(pasted[0]->mapId())->assemblyTag()==pasted[0]->assemblyTag(),"fresh MOB projection and catalog restore grouped membership");
    if(loaded&&pasted.size()==2){auto* n=restored.nodeByMapId(pasted[1]->mapId());check(n&&positionsNear(n->position(),pasted[1]->position())&&n->textureName()==pasted[1]->textureName(),"MOB reopen retains relative transform and texture");}
    // Undo commands retain the actual owner even after the active MOB changes.
    view.m_activeMob=source;view.restoreSelection({a,b});view.disassembleSelection();view.m_activeMob=destination;
    history.undo();check(!a->assemblyTag().isEmpty() && view.m_activeMob==source,"Undo grouping reveals owning MOB after destination change");
    view.m_activeMob=destination;view.restoreSelection(pasted);
    const int count=destination->nodes().size(),steps=history.count();invalid=assembly;auto raw=invalid.parts[0].object.value("_Endurance raw provenance").toObject();raw.insert("sourceSubtreeBase64","AA==");invalid.parts[0].object.insert("_Endurance raw provenance",raw);
    const bool rejected=!view.beginPreparedAssemblyPlacement(invalid,{300,200},anchor,&error);
    check(rejected&&destination->nodes().size()==count&&history.count()==steps,"invalid record rejects whole placement without partial nodes or history");
    view.resize(800,600);view.m_projection.setToIdentity();view.m_projection.perspective(45,800.f/600,0.1f,1000.f);
    view.m_cam->setLocation(QVector3D(20,20,40),QVector3D(20,20,0),QVector3D());view.m_cam->update();
    const int pointerSteps=history.index();
    const bool mousePending=view.beginPreparedAssemblyPlacement(assembly,{400,300},anchor,&error);
    QVector3D pointer;const bool pointerHit=view.objectPlacementPositionAt({400,300},&pointer);
    QMouseEvent move(QEvent::MouseMove,QPointF(400,300),Qt::NoButton,Qt::NoButton,Qt::NoModifier);view.m_pOp->mouseMoveEvent(&move);
    auto mouseNodes=view.m_pendingPasteNodes;
    check(mousePending&&pointerHit&&mouseNodes.size()==2&&positionsNear(mouseNodes[0]->drawPosition(),pointer+assembly.parts[0].offset)
        && positionsNear(mouseNodes[1]->drawPosition(),pointer+assembly.parts[1].offset),"real pointer move carries all parts under cursor in one frame");
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(400,300),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);view.m_pOp->mousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(400,300),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);view.m_pOp->mouseReleaseEvent(&release);
    check(mousePending&&!view.m_pendingAssemblyPlacement&&history.count()==pointerSteps+1&&destination->nodes().size()==count+2,"real LMB release confirms whole assembly as one history action");
    const bool mouseCommitted=!view.m_pendingAssemblyPlacement&&history.count()==pointerSteps+1;
    if(mouseCommitted)history.undo();else view.cancelPendingOperation();check(mouseCommitted&&destination->nodes().size()==count,"mouse placement Undo removes every new member");

    view.m_clipboard_buffer_file.setFileName(scratch.filePath("assembly-clipboard.json"));
    view.restoreSelection(pasted);view.selectedObjectToClipboardBuffer();
    QFile clipboard(scratch.filePath("assembly-clipboard.json"));clipboard.open(QIODevice::ReadOnly);
    const auto copied=QJsonDocument::fromJson(clipboard.readAll()).object();clipboard.close();
    check(copied.value("Content")=="Object assembly","Ctrl+C writes a complete assembly descriptor instead of loose objects");
    const int beforeCopy=history.index();view.clipboradObjectsToScene();auto copiedNodes=view.m_pendingPasteNodes;
    check(view.m_pendingAssemblyPlacement&&copiedNodes.size()==2&&copiedNodes[0]->assemblyTag()!=pasted[0]->assemblyTag(),"Ctrl+V creates an independent assembly instance");
    if(view.m_pendingAssemblyPlacement)view.m_pOp->keyPress(eKey_Enter);
    check(history.index()==beforeCopy+1 && copiedNodes.size()==2 && ei::assemblyWorldObject(copiedNodes[1]->toJson()).value("Parent Id").toVariant().toUInt()==copiedNodes[0]->mapId(),"assembly clipboard remaps parents and commits in one action");
    if(history.index()==beforeCopy+1)history.undo();
    QHash<CNode*,QPair<QByteArray,QByteArray>> batched;QByteArray rawSource,rawClean;
    const bool batch=source->rawObjectTemplatesBytes({a,b},&batched) && source->rawObjectTemplateBytes(b,&rawSource,&rawClean);
    check(batch&&batched.value(b)==qMakePair(rawSource,rawClean),"batched capture retains exact single-record provenance bytes");
    auto badRotation=assembly;auto object=badRotation.parts[0].object;auto world=ei::assemblyWorldObject(object);auto base=world.value("Base object").toObject();base.insert("Rotation",QJsonArray{0,0,0,0});world.insert("Base object",base);ei::setAssemblyWorldObject(&object,world);badRotation.parts[0].object=object;
    check(!catalog.setAssembly(badRotation,&error),"zero quaternion rejects assembly before node construction");
    // A catalog I/O failure leaves metadata dirty and can be retried without
    // falsely reporting the complete save as successful.
    QFile blocker(scratch.filePath("blocked"));blocker.open(QIODevice::WriteOnly);blocker.write("owned test blocker");blocker.close();
    QSharedPointer<ei::SProfileDatabaseSnapshot> blockedDb(new ei::SProfileDatabaseSnapshot(*db));blockedDb->sourceArchivePath=scratch.filePath("blocked/database.res");
    ei::ProfileDatabaseContext::instance().publishEditedSnapshot(blockedDb);
    check(!destination->saveAssemblyInstances(destination->filePath().absoluteFilePath(),&error)&&destination->refreshDirtyFromProjection(),"catalog write failure remains dirty instead of claiming complete save");
    ei::ProfileDatabaseContext::instance().publishEditedSnapshot(db);
    check(destination->saveAssemblyInstances(destination->filePath().absoluteFilePath(),&error)&&!destination->refreshDirtyFromProjection(),"catalog metadata save retries successfully after I/O recovery");
    if(loaded) {for(auto* node:restored.nodes())node->setAssemblyTag({});restored.m_originalData.append("external change");restored.restoreAssemblyInstances();
        bool empty=true;for(auto* node:restored.nodes())empty&=node->assemblyTag().isEmpty();check(empty,"changed MOB digest rejects stale instance groups instead of reusing IDs");}
    view.restoreSelection(pasted);view.disassembleSelection();
    check(destination->saveAssemblyInstances(destination->filePath().absoluteFilePath(),&error),"disassembly metadata removal saves");
    ei::TechnicalModelCatalog noGroups;noGroups.load(catalog.path(),&error);
    check(noGroups.assemblyInstances(destination->filePath().absoluteFilePath()).isEmpty()&&noGroups.assemblies().size()==1,"saving disassembly removes only instance grouping and retains catalog definition");
    history.undo();check(destination->refreshDirtyFromProjection(),"Undo after saved disassembly makes restored grouping dirty");
    check(destination->saveAssemblyInstances(destination->filePath().absoluteFilePath(),&error),"restored group metadata saves again");
    // Catalog descriptors and placed groups share a file. A stale writer must
    // refuse an external update, preserving both existing definitions and groups.
    catalog.setRussianName("external_writer_probe","changed");
    check(!catalog.save(&error),"stale catalog writer cannot overwrite newer group metadata");
    reopened.load(catalog.path(),&error);
    {CAssemblyGallery gallery(&reopened,{},&view);gallery.setAttribute(Qt::WA_DontShowOnScreen);gallery.show();QApplication::processEvents();
        auto* list=gallery.findChild<QListWidget*>("assemblyTiles");auto* search=gallery.findChild<QLineEdit*>("assemblySearch");auto* details=gallery.findChild<QListWidget*>("assemblyParts");
        check(list&&list->count()==1&&details&&details->count()==2,"real gallery presents a ready assembly tile and full composition");
        check(list&&list->count()&&list->visualItemRect(list->item(0)).width()>=260&&list->visualItemRect(list->item(0)).height()>=215,"native gallery allocates full thumbnail tile rather than text-size rect");
        if(search)search->setText(QString::fromUtf8("Стена"));check(list&&list->count()==1,"assembly search finds Russian component names");
        if(search)search->setText("synthetic_skin");check(list&&list->count()==1,"assembly search finds bound textures");
        if(search)search->clear();auto* name=gallery.findChild<QLineEdit*>("assemblyName");if(name)name->setText(QString::fromUtf8("Арка"));
        gallery.findChild<QComboBox*>("assemblyCategory")->setCurrentIndex(2);gallery.findChild<QComboBox*>("assemblyRegion")->setCurrentIndex(3);gallery.findChild<QPushButton*>("assemblySave")->click();
        ei::TechnicalModelCatalog renamed;renamed.load(catalog.path(),&error);auto saved=renamed.assemblies();check(saved.size()==1&&saved[0].name==QString::fromUtf8("Арка")&&saved[0].category=="architecture"&&saved[0].region=="suslanger","gallery edits name, category and island through persistent catalog");
        gallery.findChild<QTabBar*>("assemblyCategories")->setCurrentIndex(1);check(list&&list->count()==0,"category tabs filter assembled objects");gallery.reject();}
    ei::SGalleryTriangle first;first.triangle.vertices[0].position={-3,-2,0};first.triangle.vertices[1].position={0,-2,0};first.triangle.vertices[2].position={0,2,0};
    auto second=first;second.textureIndex=1;for(auto& v:second.triangle.vertices)v.position+=QVector3D(4,0,0);
    QImage red(2,2,QImage::Format_ARGB32),blue(red.size(),red.format());red.fill(Qt::red);blue.fill(Qt::blue);
    const auto thumbnail=ei::renderGalleryTriangles({first,second},{red,blue});int reds=0,blues=0;
    for(int y=0;y<thumbnail.height();++y)for(int x=0;x<thumbnail.width();++x){auto p=thumbnail.pixel(x,y);if(qRed(p)>100&&qBlue(p)<20)++reds;if(qBlue(p)>100&&qRed(p)<20)++blues;}
    check(reds>50&&blues>50,"composite renderer keeps separate textures in shared depth buffer");std::atomic_bool canceled(true);
    check(ei::renderGalleryTriangles({first,second},{red,blue},QSize(192,132),&canceled).isNull(),"closing gallery cancels render safely");
    const auto screenshot=qEnvironmentVariable("EI_ASSEMBLY_CHECK_SCREENSHOT");
    if(!screenshot.isEmpty()) {CAssemblyGallery preview(&reopened,{},&view);preview.setAttribute(Qt::WA_DontShowOnScreen);preview.show();QApplication::processEvents();
        auto* list=preview.findChild<QListWidget*>("assemblyTiles");if(list&&list->count())list->item(0)->setIcon(QPixmap::fromImage(thumbnail));
        QApplication::processEvents();check(preview.grab().save(screenshot),"assembly gallery screenshot captured from native widgets");preview.reject();}

    out<<"assembly-catalog-check: "<<failures<<" failures\n";return failures?1:0;
}

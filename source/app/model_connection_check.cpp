#include "model_connection_check.h"
#include "model_connections.h"
#include "model_connection_editor.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "part.h"
#include "prepared_static_visual.h"
#include "tree_view.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "scene.h"
#include "operationmanager.h"
#include "camera.h"
#include "profile_database.h"
#include "endurance_document.h"
#include <QTemporaryDir>
#include <QTableWidget>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextStream>
#include <QSignalBlocker>
#include <QPushButton>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QFutureWatcher>
namespace {
bool positionsNear(const QVector3D& a,const QVector3D& b) {return (a-b).length()<0.0002f;}
bool same(const QQuaternion& a,const QQuaternion& b) {return qAbs(QQuaternion::dotProduct(a.normalized(),b.normalized()))>0.99999f;}
class SlopingModule : public CWorldObj {
public:
    SlopingModule() {m_modelName="test_wall";m_primaryTexture="synthetic_skin";}
    bool updatePos(QVector3D& p) override {setPos(p);setDrawPosition(p+QVector3D(0,0,p.x()*0.3f+p.y()*0.2f));return true;}
    void setRot(const QQuaternion& q) override {CWorldObj::setRot(q);auto p=position();updatePos(p);}
    void installSyntheticGeometry() {SPreparedStaticPart part;part.name="test_wall";SVertexData vertex;vertex.position={0,0,0};part.vertices.append(vertex);vertex.position={1,0,0};part.vertices.append(vertex);vertex.position={0,1,0};part.vertices.append(vertex);installPreparedStaticParts({part});}
};
}
int runModelConnectionCheck() {
    QTextStream out(stdout);int failures=0;
    auto check=[&](bool ok,const char* name){out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failures;};
    QTemporaryDir scratch;if(!scratch.isValid())return 2;
    ei::ModelConnection left;left.id="left";left.label="Left";left.type="wall";left.position={-2,0,1};left.normal={-1,0,0};left.up={0,0,1};
    auto right=left;right.id="right";right.label="Right";right.position={2,0,1};right.normal={1,0,0};right.extra.insert("authorNote","preserve");
    ei::ConnectionPose target;target.position={12,14,7};target.rotation=QQuaternion::fromEulerAngles(23,12,78);ei::ConnectionPose placed;
    check(ei::connectModelFrames(left,right,target,&placed)
        && positionsNear(placed.position+placed.rotation.rotatedVector(left.position),target.position+target.rotation.rotatedVector(right.position))
        && positionsNear(placed.rotation.rotatedVector(left.normal),-target.rotation.rotatedVector(right.normal))
        && positionsNear(placed.rotation.rotatedVector(left.up),target.rotation.rotatedVector(right.up)),"tilted frame: points coincide, normals oppose, up aligns");
    auto incompatible=right;incompatible.type="roof";
    check(!ei::connectModelFrames(left,incompatible,target,&placed),"incompatible connector type is rejected");
    auto invalid=right;invalid.up=invalid.normal;
    check(!ei::validConnection(invalid),"degenerate frame is rejected");
    ei::TechnicalModelCatalog catalog;QString error;const auto catalogPath=scratch.filePath("model-catalog.json");catalog.load(catalogPath,&error);catalog.setRussianName("test_wall","Wall");
    check(catalog.setConnections("test_wall",{left,right},&error)&&catalog.save(&error),"catalog saves connectors beside profile database path");
    ei::TechnicalModelCatalog reopened;QVector<ei::ModelConnection> points;reopened.load(catalogPath,&error);
    check(reopened.connections("test_wall",&points,&error)&&points.size()==2&&points[1].extra.value("authorNote")=="preserve"&&reopened.russianName("test_wall")=="Wall","catalog reload retains names and unknown connector metadata");
    check(!reopened.setConnections("test_wall",{left,left},&error)&&reopened.connections("test_wall",&points)&&points.size()==2,"duplicate connector IDs refuse mutation");
    ei::ConnectionIndex index;index.add({-0.1f,-0.1f,100},17);index.add({1000,1000,0},18);
    check(index.queryNear({0,0,0},1).contains(17)&&!index.queryNear({0,0,0},1).contains(18),"spatial lookup includes negative cells and upper floors, excludes distant objects");
    QElapsedTimer clock;clock.start();for(int i=0;i<100000;++i)index.add({float(i*8),float(i*8),0},i);
    clock.restart();bool local=true;for(int i=0;i<1000;++i)local=local&&index.queryNear({0,0,0},2).size()<10;
    check(local&&clock.elapsed()<1000,"1000 local queries over 100000 indexed anchors are bounded");
    const auto original=ei::ProfileDatabaseContext::instance().current();
    struct Guard {QSharedPointer<const ei::SProfileDatabaseSnapshot> previous;~Guard(){ei::ProfileDatabaseContext::instance().publishEditedSnapshot(previous);}} guard{original};
    QSharedPointer<ei::SProfileDatabaseSnapshot> db(new ei::SProfileDatabaseSnapshot);db->sourceArchivePath=scratch.filePath("database.res");ei::ProfileDatabaseContext::instance().publishEditedSnapshot(db);
    CSettings settings;QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);CProgressView progress;QLineEdit coords;CTileForm tiles;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.attach(&settings,&properties,&history,&progress,&coords,&tree,&tiles);
    auto* mob=new CMob;mob->setFileName(QFileInfo(scratch.filePath("fixture.mob")));auto* a=new SlopingModule;auto* b=new SlopingModule;
    a->setMapId(77);b->setMapId(77);mob->nodes()<<a<<b;view.m_aMob<<mob;view.m_activeMob=mob;
    {QSignalBlocker block(CScene::getInstance());CScene::getInstance()->changeMode(eEditModeObjects);}
    QVector3D start(1,1,0),destination(7,9,4);a->updatePos(start);b->updatePos(destination);b->setRot(target.rotation);a->setState(eSelect);b->setState(eDraw);
    a->installSyntheticGeometry();
    QString magnetStatus;
    QObject::connect(&view,&CView::modelConnectionStatusChanged,[&](const QString& status){magnetStatus=status;});
    const auto initialRotation=a->rotation();const auto initialScale=a->constitution();view.setModelMagnetEnabled(true);
    check(magnetStatus.contains("G") && view.m_sourceConnections.isEmpty(),"enabling idle magnet explains G without preparing a false source");
    view.operationSetBackup(eMove);
    check(view.m_connectionGhostNode==a&&view.m_connectionGhostVertices.size()==3&&positionsNear(view.m_connectionGhostVertices[1],{1,0,0}),"preview snapshots complete visible geometry once per gesture");
    check(view.m_sourceConnections.size()==2 && view.m_connectionTargets.size()==2,"gesture indexes loaded objects once and excludes moving source despite duplicate IDs");
    check(view.applyModelConnection(0,1)&&positionsNear(a->drawPosition()+a->rotation().rotatedVector(left.position),b->drawPosition()+b->rotation().rotatedVector(right.position))&&a->constitution()==initialScale,"sloping terrain uses stored height offset with unchanged scale");
    const auto aligned=a->position();const auto alignedRotation=a->rotation();view.operationApply(eMove);
    check(history.count()==1&&same(a->rotation(),alignedRotation),"confirm creates one history action for position and quaternion");history.undo();
    check(positionsNear(a->position(),start)&&same(a->rotation(),initialRotation),"one Undo restores original position and rotation");history.redo();
    check(positionsNear(a->position(),aligned)&&same(a->rotation(),alignedRotation),"one Redo restores aligned transform");
    view.operationSetBackup(eMove);view.applyModelConnection(0,1);view.operationRevert(eMove);
    check(positionsNear(a->position(),aligned)&&same(a->rotation(),alignedRotation),"cancel restores gesture baseline");
    view.operationSetBackup(eMove);view.applyModelConnection(0,1);view.restoreFreeModelMove();
    check(positionsNear(a->position(),aligned)&&same(a->rotation(),alignedRotation),"releasing snap restores unconstrained preview");
    // Real state machine Escape cancels both transform components.
    view.clearModelMagnet();view.m_operationBackup.clear();view.m_pOp->changeState(new CMoveAxis(&view,EOperateAxisXY));view.applyModelConnection(0,1);view.m_pOp->keyPress(eKey_Esc);
    check(positionsNear(a->position(),aligned)&&same(a->rotation(),alignedRotation)&&view.m_connectionTargets.isEmpty(),"Escape cancels real move gesture and clears target lifetime");
    QJsonObject json=a->toJson();CWorldObj roundtrip(json);
    check(positionsNear(roundtrip.position(),aligned)&&same(roundtrip.rotation(),alignedRotation),"ordinary MOB node JSON retains aligned transform without connector gameplay fields");
    b->setMapId(78);QByteArray serialized;mob->serializeMob(serialized,false);QFile binary(scratch.filePath("aligned.mob"));
    bool written=binary.open(QIODevice::WriteOnly);if(written){written=binary.write(serialized)==serialized.size();binary.close();}
    QFile input(binary.fileName());bool opened=input.open(QIODevice::ReadOnly);const auto bytes=opened?input.readAll():QByteArray();input.close();
    CMobDocument document;CValidationReport report;CMob restored;
    const bool reopenedMob=written&&opened&&document.loadBytes(bytes,"aligned.mob",&report)&&restored.deserialize(bytes,&document);
    auto* persisted=restored.nodeByMapId(77);
    check(reopenedMob&&persisted&&positionsNear(persisted->position(),aligned)&&same(persisted->rotation(),alignedRotation),"temporary binary MOB fresh reopen preserves aligned position and rotation");
    view.resize(800,600);view.m_projection.setToIdentity();view.m_projection.perspective(45,800.f/600,0.1f,1000.f);
    view.m_cam->setLocation(QVector3D(15,15,40),QVector3D(15,15,0),QVector3D(-90,0,0));view.m_cam->update();
    view.operationSetBackup(eMove);
    // Two distinct source sockets at the same location provide deterministic
    // alternatives without depending on the camera's pixel scale.
    auto alternate=view.m_sourceConnections.first();alternate.id="alternate";view.m_sourceConnections.append(alternate);
    view.updateModelMagnet({400,300});
    check(view.m_modelMagnetApplied&&!view.m_connectionChoices.isEmpty(),"pointer search uses spatial index and visible projected anchors");
    const auto chosen=qMakePair(view.m_connectionHeldSource,view.m_connectionHeldTarget);
    view.cycleModelConnectionTarget();
    check(view.m_connectionChoices.size()>1&&qMakePair(view.m_connectionHeldSource,view.m_connectionHeldTarget)!=chosen,"cycle selects another compatible connector pair");
    const auto cycled=qMakePair(view.m_connectionHeldSource,view.m_connectionHeldTarget);view.updateModelMagnet({401,301});
    check(qMakePair(view.m_connectionHeldSource,view.m_connectionHeldTarget)==cycled,"small pointer changes retain chosen pair without jumping");
    view.setModelConnectionSource("alternate");
    check(view.m_modelMagnetApplied&&view.m_connectionHeldSource==2,"explicit source connector filters candidate pairs");view.setModelConnectionSource({});
    const int indexed=view.m_connectionTargets.size();QVector3D farDisplacement(1000,0,0);view.moveTo(farDisplacement);view.updateModelMagnet({700,300});
    check(!view.m_modelMagnetApplied&&view.m_connectionTargets.size()==indexed,"moving away releases magnet without rescanning MOB objects");view.operationRevert(eMove);view.clearModelMagnet();view.m_operationBackup.clear();
    check(view.m_connectionGhostNode.isNull()&&view.m_connectionGhostVertices.isEmpty()&&view.m_connectionSourceId.isEmpty(),"ending preview releases geometry and resets source choice to Auto");
    auto provider=[](const QString&) {ei::SResourceModelScene scene;scene.valid=true;scene.kind="FIG8";ei::SResourceModelPart part;part.name="test_wall";
        ei::SResourceModelTriangle first,second;first.vertices[0].position={-4,-4,0};first.vertices[1].position={4,-4,0};first.vertices[2].position={4,4,0};
        second.vertices[0].position={-4,-4,0};second.vertices[1].position={4,4,0};second.vertices[2].position={-4,4,0};part.triangles={first,second};scene.parts={part};scene.triangleCount=2;return scene;};
    CModelConnectionEditor editor(&reopened,"test_wall",provider);editor.setAttribute(Qt::WA_DontShowOnScreen);editor.show();
    QElapsedTimer loadedScene;loadedScene.start();bool sceneFinished=false;
    do {QApplication::processEvents();const auto jobs=editor.findChildren<QFutureWatcher<ei::SResourceModelScene>*>();sceneFinished=jobs.isEmpty()||jobs.first()->isFinished();}while(!sceneFinished&&loadedScene.elapsed()<1500);
    QApplication::processEvents();check(sceneFinished,"preview worker completes within bounded wait");
    auto* canvas=editor.findChild<QWidget*>("connectionCanvas");const auto preview=canvas?canvas->grab():QPixmap();
    if(canvas){const auto center=canvas->rect().center();QMouseEvent press(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,center,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas,&release);}
    const auto fields=editor.findChildren<QDoubleSpinBox*>();
    check(!preview.isNull()&&fields.size()==9&&qAbs(fields[2]->value())<0.001,"real orbit preview surface click places connector in model coordinates");
    if(canvas && fields.size()==9) {
        auto vector=[&](int offset){return QVector3D(fields[offset]->value(),fields[offset+1]->value(),fields[offset+2]->value());};
        auto mouse=[&](QEvent::Type type,const QPointF& point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers){
            QMouseEvent event(type,point,button,buttons,modifiers);QApplication::sendEvent(canvas,&event);};
        auto drag=[&](const QPointF& start,const QPointF& end,Qt::MouseButton button,Qt::KeyboardModifiers modifiers){
            mouse(QEvent::MouseButtonPress,start,button,button,modifiers);mouse(QEvent::MouseMove,end,Qt::NoButton,button,modifiers);
            mouse(QEvent::MouseButtonRelease,end,button,Qt::NoButton,modifiers);canvas->grab();};
        const auto position=vector(0),normal=vector(3),up=vector(6);
        const QPointF start=canvas->rect().center()+QPoint(40,-30);
        drag(start,start+QPointF(70,35),Qt::LeftButton,Qt::AltModifier);
        check(positionsNear(vector(0),position)&&(vector(3)-normal).length()>0.1f,"Alt mouse drag rotates direction without moving socket on surface");
        check(qAbs(vector(3).length()-normal.length())<0.001f&&qAbs(vector(6).length()-up.length())<0.001f
            &&qAbs(QVector3D::dotProduct(vector(3).normalized(),vector(6).normalized()))<0.001f,"mouse rotation preserves valid normal/up frame and vector lengths");
        const auto beforeTip=vector(3);const auto handle=canvas->property("connectionDirectionHandle");
        if(handle.isValid())drag(handle.toPointF(),handle.toPointF()+QPointF(-35,55),Qt::LeftButton,Qt::NoModifier);
        check(handle.isValid()&&positionsNear(vector(0),position)&&(vector(3)-beforeTip).length()>0.05f,"yellow handle mouse drag rotates arrow instead of relocating socket");
        const auto beforeEscape=vector(3),upBeforeEscape=vector(6);
        mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);
        mouse(QEvent::MouseMove,start+QPointF(50,-50),Qt::NoButton,Qt::LeftButton,Qt::AltModifier);
        const bool changed=(vector(3)-beforeEscape).length()>0.05f;
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(canvas,&escape);
        mouse(QEvent::MouseButtonRelease,start,Qt::LeftButton,Qt::NoButton,Qt::AltModifier);
        check(changed&&editor.isVisible()&&positionsNear(vector(3),beforeEscape)&&positionsNear(vector(6),upBeforeEscape)
            &&positionsNear(vector(0),position),"Escape cancels active arrow drag without closing editor or moving socket");
        const auto beforeOrbit=vector(3);
        drag(start,start+QPointF(40,20),Qt::RightButton,Qt::NoModifier);
        check(positionsNear(vector(0),position)&&positionsNear(vector(3),beforeOrbit),"right mouse orbit leaves connector coordinates unchanged");
    } else check(false,"arrow mouse checks require loaded production canvas and fields");
    const auto screenshot=qEnvironmentVariable("EI_MODEL_CONNECTION_CHECK_SCREENSHOT");if(!screenshot.isEmpty())editor.grab().save(screenshot);
    auto* list=editor.findChild<QListWidget*>("connectionList");auto* name=editor.findChild<QLineEdit*>("connectionLabel");
    check(list&&list->count()==2&&name,"real connector editor displays catalog points");
    if(name)name->setText("Edited");editor.reject();reopened.connections("test_wall",&points);
    check(points.size()==2&&points[0].label=="Left","cancel editor preserves catalog");
    CModelConnectionEditor saving(&reopened,"test_wall",{});saving.setAttribute(Qt::WA_DontShowOnScreen);saving.show();QApplication::processEvents();
    auto* savingCanvas=saving.findChild<QWidget*>("connectionCanvas");
    auto savingFields=saving.findChildren<QDoubleSpinBox*>();
    QVector3D savedDirection;
    if(savingCanvas&&savingFields.size()==9) {
        const auto start=savingCanvas->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);QApplication::sendEvent(savingCanvas,&press);
        QMouseEvent move(QEvent::MouseMove,start+QPoint(50,30),Qt::NoButton,Qt::LeftButton,Qt::AltModifier);QApplication::sendEvent(savingCanvas,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,start+QPoint(50,30),Qt::LeftButton,Qt::NoButton,Qt::AltModifier);QApplication::sendEvent(savingCanvas,&release);
        savedDirection={float(savingFields[3]->value()),float(savingFields[4]->value()),float(savingFields[5]->value())};
    }
    saving.findChild<QLineEdit*>("connectionLabel")->setText("Saved");saving.findChild<QPushButton*>("connectionSave")->click();
    ei::TechnicalModelCatalog saved;saved.load(catalogPath,&error);saved.connections("test_wall",&points);
    check(saving.result()==QDialog::Accepted&&points[0].label=="Saved","editor explicit save survives fresh catalog reopen");
    check(points.size()==2&&positionsNear(points[0].normal,savedDirection)&&(points[0].normal-left.normal).length()>0.1f
        &&positionsNear(points[0].position,left.position)&&ei::validConnection(points[0]),"mouse-edited direction saves and reloads valid frame even without geometry");
    auto* creationMob=new CMob;creationMob->setFileName(QFileInfo(scratch.filePath("creation.mob")));
    const bool loaded=creationMob->deserialize(bytes,&document);view.m_aMob.append(creationMob);view.m_activeMob=creationMob;
    if(loaded) {creationMob->m_originalData=bytes;creationMob->serializeMob(creationMob->m_cleanProjection,false);creationMob->rebindRawObjectProvenance();}
    auto* selected=creationMob->nodeByMapId(77);for(auto* n:mob->nodes())n->setState(eDraw);if(selected)selected->setState(eSelect);
    history.clear();SlopingModule prototype;QJsonObject descriptor;CValidationReport creationReport;
    const bool prepared=CMob::prepareFreshTechnicalNode(&prototype,&descriptor,&creationReport);
    const int originalCount=creationMob->nodes().size();
    const bool pending=loaded&&prepared&&view.beginPreparedObjectPlacement(descriptor,{400,300},&error);
    if(!pending)out<<"creation fixture loaded="<<loaded<<" prepared="<<prepared<<" diagnostic="<<error<<"\n"<<creationReport.toText()<<"\n";
    check(pending&&creationMob->nodes().size()==originalCount+1&&history.count()==0,"technical creation preview remains outside history until confirmation");
    if(pending)view.m_pOp->keyPress(eKey_Esc);
    check(pending&&creationMob->nodes().size()==originalCount&&history.count()==0&&selected&&selected->nodeState()==eSelect,"Escape removes uncommitted technical module and restores prior selection");
    const bool pendingAgain=view.beginPreparedObjectPlacement(descriptor,{400,300},&error);
    CNode* created=pendingAgain?view.m_pendingPasteNodes.first():nullptr;
    const bool snappedCreation=pendingAgain&&view.applyModelConnection(0,0);
    const auto createdPosition=created?created->position():QVector3D();const auto createdRotation=created?created->rotation():QQuaternion();
    if(pendingAgain)view.operationApply(eMove);
    check(snappedCreation&&history.count()==1&&creationMob->nodes().size()==originalCount+1&&positionsNear(created->position(),createdPosition)&&same(created->rotation(),createdRotation),"snapped technical creation confirmation is one history action");
    if(pendingAgain)history.undo();
    check(pendingAgain&&creationMob->nodes().size()==originalCount,"one Undo removes newly placed module");
    if(pendingAgain)history.redo();
    check(snappedCreation&&creationMob->nodes().contains(created)&&positionsNear(created->position(),createdPosition)&&same(created->rotation(),createdRotation),"one Redo recreates module with its aligned transform");
    out<<"model-connection-check: "<<failures<<" failures\n";return failures?1:0;
}

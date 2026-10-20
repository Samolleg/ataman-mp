#include <QtWidgets>
#include <QtOpenGL>
#define private public
#include "unit.h"
#undef private
#include "view.h"
#include "camera.h"
#include "settings.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "scene.h"
#include "undo.h"
#include "property.h"
#include "operationmanager.h"
#include "map_object_placement_policy.h"
#include "resource_profile.h"
#include "profile_database.h"
namespace {
class HiddenWindows:public QObject {
 bool eventFilter(QObject* o,QEvent* e) override {
  if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
   if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
  return false;
 }
};
QJsonArray vec(const QVector3D& p){return {p.x(),p.y(),p.z()};}
QByteArray read(const QString& p){QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
}
int runNeighborPerformanceCheck()
{
 QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
 auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passes;};
 try {
 HiddenWindows hidden;qApp->installEventFilter(&hidden);
 QTemporaryDir scratch;QDir::setCurrent(scratch.path());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
 const QString root=qEnvironmentVariable("EI_UNITS_ROOT"),output=qEnvironmentVariable("EI_UNITS_OUTPUT");const QString profile=qEnvironmentVariable("EI_UNITS_PROFILE","deja-vu");
 const QString mapName=qEnvironmentVariable("EI_UNITS_MAP","zone9");const QString reopenReceipt=qEnvironmentVariable("EI_UNITS_REOPEN_RECEIPT");check(!root.isEmpty()&&!output.isEmpty(),"explicit private fixture/output");QDir().mkpath(output);
 CSettings settings;
 auto boolean=[&](const char* name,bool value){if(auto* opt=dynamic_cast<COptBool*>(settings.opt(name)))opt->setValue(value);};
 dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue(profile);
 dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue("C:/GOG Games/PZ");
 QStringList roots={"C:/GOG Games/PZ/Base/Res"};if(profile=="deja-vu")roots.prepend("C:/GOG Games/PZ/MODS/DejaVu/res");
 dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue(roots.toVector());
 CTextureList::getInstance()->attachSettings(&settings);CObjectList::getInstance()->attachSettings(&settings);
 ei::SProfileDatabaseLoadRequest request;QMap<QString,QString> bindings;
 for(const auto& p:builtInResourceProfiles())if(p.id==profile) {
  request.profileId=profile;request.runtimeManifestId=p.runtimeManifestId;request.providerId=p.providers.value("database").id;request.resourceDirectories=roots;
  for(auto it=p.providers.begin();it!=p.providers.end();++it)bindings.insert(it.key(),it.value().id);
 }
 ei::ProfileDatabaseService databases;auto database=databases.snapshot(request);check(database && database->usable(),"actual profile database");ei::ProfileDatabaseContext::instance().activate(database,bindings);
 QUndoStack undo;QTableWidget table;table.setColumnCount(2);CProgressView progress;QLineEdit coords;CTreeView tree;CTileForm tiles;
 CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.attach(&settings,&table,&undo,&progress,&coords,&tree,&tiles);view.resize(1280,900);view.show();qApp->processEvents();view.makeCurrent();check(view.isValid(),"hidden native OpenGL");
 check(view.m_pLand->unitSurfacePosition(QVector3D(2,3,7))==QVector3D(2,3,7),"standalone MOB keeps absolute position");
 view.setWorkspaceLoadInProgress(true);view.m_particleVisualsVisible=false;view.m_freezeAnimations=true;view.m_animationFreezeTime=0;
 boolean("animateUnits",false);boolean("drawHelp",false);boolean("drawLogic",true);
 QFileInfo mpr(root+"/"+mapName+".mpr"),mobPath(qEnvironmentVariable("EI_UNITS_REOPEN_MOB",root+"/"+mapName+".mob"));view.m_pLand->readMap(mpr,false);check(view.loadMob(mobPath),"real map copy loaded");view.makeCurrent();
 auto* mob=view.m_activeMob;const QByteArray original=read(mobPath.absoluteFilePath());
 auto bytes=[&](){QByteArray b;mob->serializeMob(b,false);return b;};
 const QByteArray baseline=bytes();check(view.saveMobDocument(mob,QFileInfo(output+"/noop.mob")),"atomic no-op Save");check(read(output+"/noop.mob")==original,"no-op open/save preserves every MOB byte including Z");
 view.ensureUnitSupportSurface();check(view.m_unitSupportSurface.faceCount()>0,"authored BASE surfaces indexed");
 QJsonArray supports,existing;CUnit* donor=nullptr;
 for(CNode* n:mob->nodes()) {
  if(n->nodeType()==eUnit) {auto* u=static_cast<CUnit*>(n);if(!donor && u->mapName().contains("Skeleton",Qt::CaseInsensitive))donor=u;
   auto record=[&](const QString& kind,const QVector3D& p){auto terrain=p;terrain.setZ(0);view.m_pLand->projectPt(terrain);auto surface=terrain;int tested=0;
    if(view.m_unitSupportSurface.height(&surface,&tested) && surface.z()>terrain.z()+.4f)
     existing.append(QJsonObject{{"id",int(u->mapId())},{"name",u->mapName()},{"kind",kind},{"stored",vec(p)},{"terrainZ",terrain.z()},{"supportZ",surface.z()},{"trianglesTested",tested}});
   };record("unit",u->position());for(int l=0;l<u->logicCount();++l){auto* logic=u->logicAt(l);record("guard",logic->m_guardPlacement);for(auto* pp:logic->patrolPoints()){record("patrol",pp->position());for(auto* lp:pp->lookPoint())record("look",lp->position());}}
  }else if(auto* o=dynamic_cast<CObjectBase*>(n)) for(const auto& p:o->navigationCollisionProxies()) if(p.authored&&p.kind==ENavigationVolumeKind::Bridge){
   QVector3D c;for(const auto& v:p.boxCorners)c+=v;c/=8;auto ground=c;ground.setZ(0);view.m_pLand->projectPt(ground);auto surface=ground;CUnitSupportSurface own;own.add(p);
   if(own.height(&surface))supports.append(QJsonObject{{"id",int(n->mapId())},{"model",n->modelName()},{"part",p.partName},{"center",vec(surface)},{"terrainZ",ground.z()}});
  }
 }
 {QFile f(output+"/map-research.json");check(f.open(QIODevice::WriteOnly),"research output");f.write(QJsonDocument(QJsonObject{{"map",mapName},{"profile",profile},{"nodes",mob->nodes().size()},{"faces",view.m_unitSupportSurface.faceCount()},{"supports",supports},{"existing",existing}}).toJson());}
 if(!reopenReceipt.isEmpty()) {
  const auto expected=QJsonDocument::fromJson(read(reopenReceipt)).object()["scenarios"].toArray();
  CScene::getInstance()->changeMode(eEditModeLogic);
  for(const auto& v:expected) {
   const auto record=v.toObject();auto* u=dynamic_cast<CUnit*>(mob->nodeByMapId(record["id"].toInt()));check(u!=nullptr,"reopened added NPC exists");check(u->refreshTransientVisual(true),"reopened NPC visual resolved");view.m_pLand->projectPosition(u);u->refreshLogicProjection();
   const auto a=record["surface"].toArray();const QVector3D target(a[0].toDouble(),a[1].toDouble(),a[2].toDouble());
   check(qAbs(u->drawPosition().z()+u->minPosition().z()-target.z())<.2f,"new view reopened NPC feet height");
   bool heights=true;for(auto* pp:u->logicAt(0)->patrolPoints()) {
    heights &= qAbs(pp->drawPosition().z()-pp->position().z())<.01f;
    for(auto* lp:pp->lookPoint())heights &= qAbs(lp->drawPosition().z()-lp->position().z())<.01f;
   }check(heights,"new view reopened patrol/look display Z");
   if(record["name"].toString()=="bridge") {
    view.m_cam->moveTo(target);view.m_cam->moveAwayOn(23);view.draw();view.makeCurrent();
    view.restoreSelection({u});QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);QOpenGLFramebufferObject fbo(1280,900,format);check(fbo.bind(),"reopen scene framebuffer");glViewport(0,0,1280,900);
    view.m_minimapCaptureActive=true;view.m_minimapCaptureView=view.m_cam->viewMatrix();view.m_minimapCaptureCameraPosition=view.m_cam->viewMatrix().inverted().map(QVector3D());view.m_minimapCaptureIncludeStaticGeometry=true;view.m_minimapCaptureUnlitPass=false;view.draw();
    view.m_program.bind();view.m_program.setUniformValue("u_projMmatrix",view.m_projection);view.m_program.setUniformValue("u_viewMmatrix",view.m_cam->viewMatrix());u->drawObjectVisual(true,&view.m_program);u->drawLogicVisual(true,&view.m_program);glFinish();const auto image=fbo.toImage();view.m_minimapCaptureActive=false;QOpenGLFramebufferObject::bindDefault();check(image.save(output+"/bridge-reopened.png"),"reopened bridge renderer capture");
   }
  }
  check(bytes()==baseline,"reopen view/render leaves serialized values unchanged");view.clearObjectSelection();undo.clear();view.unloadMobDocument(mob,false);out<<"RESULT reopen "<<passes<<" PASS\n";return 0;
 }
 if(mapName!="zone9" || profile!="deja-vu") {out<<"RESULT research "<<passes<<" PASS\n";view.clearObjectSelection();undo.clear();view.unloadMobDocument(mob,false);return 0;}
 check(donor!=nullptr,"NPC donor");
 QVector3D bridge,wall;int wallId=-1;QString wallModel;
 for(const auto& v:supports){const auto o=v.toObject();auto c=o["center"].toArray();QVector3D p(c[0].toDouble(),c[1].toDouble(),c[2].toDouble());if(o["id"].toInt()==19)bridge=p;
  if(wallId<0 && o["id"].toInt()!=19 && o["model"].toString().contains("wall",Qt::CaseInsensitive) && p.z()>o["terrainZ"].toDouble()+1){wall=p;wallId=o["id"].toInt();wallModel=o["model"].toString();}}
 check(!bridge.isNull(),"zone9 deck24 ID19 authored surface");
 int tested=0;auto supportQuery=bridge;supportQuery.setZ(-1000);view.m_unitSupportSurface.height(&supportQuery,&tested);check(tested<view.m_unitSupportSurface.faceCount()/4,"point broad phase rejects distant bridge faces");
 const auto revision=view.m_unitSupportRevision;QElapsedTimer timer;timer.start();for(int i=0;i<2000;++i){auto point=bridge;point.setZ(0);view.ensureUnitSupportSurface();view.m_unitSupportSurface.height(&point);}const auto queryMs=timer.elapsed();check(view.m_unitSupportRevision==revision,"2000 warm queries reuse geometry cache");out<<"PERFORMANCE pointTriangles="<<tested<<" totalFaces="<<view.m_unitSupportSurface.faceCount()<<" 2000queriesMs="<<queryMs<<"\n";
 auto* deck=dynamic_cast<CObjectBase*>(mob->nodeByMapId(19));check(deck!=nullptr,"real support object");const auto deckPos=deck->position();auto raised=deckPos+QVector3D(0,0,2);deck->updatePos(raised);const auto changed=view.m_pLand->unitSurfacePosition(bridge);check(qAbs(changed.z()-bridge.z()-2)<.01f,"support movement invalidates cache");auto restore=deckPos;deck->updatePos(restore);deck->markAsDeleted();const auto removed=view.m_pLand->unitSurfacePosition(bridge);check(removed.z()<bridge.z()-.5f,"deleted support invalidates cache");deck->markAsDeleted(false);check(qAbs(view.m_pLand->unitSurfacePosition(bridge).z()-bridge.z())<.01f,"support restore invalidates cache");
 // Arbitrary visible trees/props without authored BASE metadata stay excluded.
 CUnitSupportSurface filter;auto proxies=deck->navigationCollisionProxies();check(!proxies.isEmpty(),"support source metadata");auto proxy=proxies.first();proxy.kind=ENavigationVolumeKind::Easy;filter.add(proxy);proxy.kind=ENavigationVolumeKind::Blocking;filter.add(proxy);proxy.kind=ENavigationVolumeKind::Bridge;proxy.authored=false;filter.add(proxy);check(filter.faceCount()==0,"crowns ordinary blocking props and approximate bounds are not supports");
 if(wallId<0)for(const auto& v:supports){auto o=v.toObject();if(o["id"].toInt()!=19 && o["model"].toString().contains("z9",Qt::CaseInsensitive)){auto c=o["center"].toArray();wall=QVector3D(c[0].toDouble(),c[1].toDouble(),c[2].toDouble());wallId=o["id"].toInt();wallModel=o["model"].toString();break;}}
 check(wallId>=0,"zone9 wall support located");out<<"BRIDGE "<<QJsonDocument(vec(bridge)).toJson(QJsonDocument::Compact)<<" WALL "<<wallId<<" "<<wallModel<<" "<<QJsonDocument(vec(wall)).toJson(QJsonDocument::Compact)<<"\n";
 CScene::getInstance()->changeMode(eEditModeLogic);
 QJsonArray scenarios;QList<uint> newIds;
 auto screen=[&](const QVector3D& p){const auto v=p.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));return QPoint(qRound(v.x()),qRound(view.height()-v.y()));};
 auto aim=[&](const QVector3D& p){view.m_cam->moveTo(p);view.m_cam->moveAwayOn(28);view.draw();view.makeCurrent();};
 auto scenario=[&](const QString& name,QVector3D target){
  CScene::getInstance()->changeMode(name=="bridge" ? eEditModeObjects : eEditModeLogic);
  aim(target);QPoint cursor=screen(target);QVector3D picked;check(view.unitPlacementPositionAt(cursor,&picked),name+" ray surface");check(qAbs(picked.z()-target.z())<.2f,name+" ray hits top, not terrain");
  QString error;check(view.beginDatabasePlacement(donor,&error),name+" native NPC placement "+error);check(view.updateDatabasePlacementPointer(cursor),name+" NPC pointer update");
  auto* unit=dynamic_cast<CUnit*>(view.m_pendingPasteNodes.first());check(unit!=nullptr,name+" pending NPC");view.operationApply(EOperationAxisType::eMove);view.m_pOp->changeState(new CSelect(&view));
  CScene::getInstance()->changeMode(eEditModeLogic);
  newIds.append(unit->mapId());check(unit->position().z()==0,name+" NPC keeps map offset Z0");check(unit->refreshTransientVisual(true),name+" profile NPC visual resolved");view.m_pLand->projectPosition(unit);
  check(qAbs(unit->drawPosition().z()+unit->minPosition().z()-picked.z())<.2f,name+" NPC feet on support");
  view.restoreSelection({unit});const auto oldUnitBytes=bytes();auto* unitMove=new CMoveAxis(&view,EOperateAxisXY);view.m_pOp->changeState(unitMove);const auto unitPointer=screen(picked);unitMove->synchronizePointerAfterCameraNavigation(unitPointer);QMouseEvent unitEvent(QEvent::MouseMove,unitPointer+QPoint(5,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);view.mouseMoveEvent(&unitEvent);view.operationApply(EOperationAxisType::eMove);view.m_pOp->changeState(new CSelect(&view));const auto movedUnitBytes=bytes();check(movedUnitBytes!=oldUnitBytes,name+" NPC surface XY movement");undo.undo();check(bytes()==oldUnitBytes,name+" NPC move Undo exact");undo.redo();check(bytes()==movedUnitBytes,name+" NPC move Redo exact");undo.undo();
  view.restoreSelection({unit});undo.push(new CCreatePatrolCommand(&view,unit,QString::number(unit->mapId())+".-1",{unit}));
  check(!unit->logicAt(0)->patrolPoints().isEmpty(),name+" first patrol created");auto* pp=unit->logicAt(0)->patrolByIndex(0);check(qAbs(pp->position().z()-picked.z())<.2f,name+" first patrol world Z");
  undo.push(new CCreatePatrolCommand(&view,pp,QString::number(unit->mapId())+".0",{pp}));auto* pp2=unit->logicAt(0)->patrolByIndex(1);
  view.restoreSelection({pp2});view.m_pendingLogicPlacementMacro=true;view.m_operationBackup.clear();view.m_operationBackup.insert(pp2,pp2->position());check(view.updateLogicPlacementPointer(cursor),name+" patrol pointer on support");view.m_pendingLogicPlacementMacro=false;view.m_operationBackup.clear();
  undo.push(new CCreatePatrolCommand(&view,pp2,QString::number(unit->mapId())+".1.-1",{pp2}));auto* look=pp2->viewByIndex(0);check(look!=nullptr,name+" look created");
  view.m_pendingLogicPlacementMacro=true;view.m_operationBackup.insert(look,look->position());check(view.updateLogicPlacementPointer(cursor+QPoint(12,0)),name+" look pointer placement");view.m_pendingLogicPlacementMacro=false;view.m_operationBackup.clear();
  auto* logic=unit->logicAt(0);logic->setGuardPlacement(picked);logic->createLogicLines();
  check(qAbs(pp2->drawPosition().z()-picked.z())<.2f && qAbs(look->drawPosition().z()-look->position().z())<.2f,name+" patrol/look display height");
  {bool correct=true;for(auto p:logic->m_aDrawPoint)correct &= p.z()>=target.z()-.3f;check(correct,name+" patrol line at surface");}
  const auto oldState=unit->movementState();const auto before=bytes();
  view.restoreSelection({pp2});auto* moveState=new CMoveAxis(&view,EOperateAxisXY);view.m_pOp->changeState(moveState);const auto pointer=screen(pp2->position());moveState->synchronizePointerAfterCameraNavigation(pointer);
  QMouseEvent move(QEvent::MouseMove,pointer+QPoint(8,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);view.mouseMoveEvent(&move);view.operationApply(EOperationAxisType::eMove);view.m_pOp->changeState(new CSelect(&view));const auto after=bytes();check(after!=before,name+" real XY patrol movement changes coordinates");undo.undo();check(bytes()==before,name+" movement Undo exact");undo.redo();check(bytes()==after,name+" movement Redo exact");
  const auto carriedState=unit->movementState();const auto carriedBytes=bytes();
  CScene::getInstance()->changeMode(eEditModeObjects);auto destination=unit->position();destination.setX(15);destination.setY(15);unit->updatePos(destination);
  check(qAbs(logic->m_guardPlacement.z()-view.m_pLand->unitSurfacePosition(destination).z())<.2f,name+" Objects-mode logic follows change of supporting level");
  check(unit->restoreMovementState(carriedState) && bytes()==carriedBytes,name+" Objects-mode movement restores exact full state");CScene::getInstance()->changeMode(eEditModeLogic);
  logic->setGuardPlacement(picked);logic->m_behaviour=EBehaviourType::eRadius;logic->createLogicLines();check(!logic->m_aDrawPoint.isEmpty(),name+" guard circle");{bool correct=true;for(auto p:logic->m_aDrawPoint)correct &= qAbs(p.z()-(name=="ground" ? view.m_pLand->unitSurfacePosition(p).z() : picked.z()))<.01f;check(correct,name+" guard circle uses object level or original terrain");}logic->m_behaviour=EBehaviourType::ePath;logic->createLogicLines();
  scenarios.append(QJsonObject{{"name",name},{"surface",vec(picked)},{"id",int(unit->mapId())},{"unitStored",vec(unit->position())},{"unitDraw",vec(unit->drawPosition())},{"patrol",vec(pp2->position())},{"look",vec(look->position())}});
  aim(picked);view.restoreSelection({unit});for(int i=0;i<3;++i){view.draw();qApp->processEvents();}view.makeCurrent();
  QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);QOpenGLFramebufferObject fbo(1280,900,format);check(fbo.bind(),name+" owned scene framebuffer");glViewport(0,0,1280,900);
  view.m_minimapCaptureActive=true;view.m_minimapCaptureView=view.m_cam->viewMatrix();view.m_minimapCaptureCameraPosition=view.m_cam->viewMatrix().inverted().map(QVector3D());view.m_minimapCaptureIncludeStaticGeometry=true;view.m_minimapCaptureUnlitPass=false;
  view.draw();view.m_program.bind();view.m_program.setUniformValue("u_projMmatrix",view.m_projection);view.m_program.setUniformValue("u_viewMmatrix",view.m_cam->viewMatrix());
  unit->drawObjectVisual(true,&view.m_program);unit->drawLogicVisual(true,&view.m_program);glFinish();
  const auto image=fbo.toImage();view.m_minimapCaptureActive=false;QOpenGLFramebufferObject::bindDefault();
  check(image.save(output+"/"+name+".png"),name+" real renderer screenshot");
 };
 scenario("bridge",bridge);scenario("wall",wall);
 QVector3D ground(15,15,0);view.m_pLand->projectPt(ground);ground=view.m_pLand->unitSurfacePosition(ground);scenario("ground",ground);
 const QByteArray edited=bytes();QJsonArray expected;for(uint id:newIds)expected.append(mob->nodeByMapId(id)->toJson());
 check(view.saveMobDocument(mob,QFileInfo(output+"/edited.mob")),"atomic edited MOB Save");auto* reopened=new CMob;reopened->attach(&view,&progress);QFileInfo saved(output+"/edited.mob");check(reopened->readMob(saved)==0,"fresh MOB reopen");
 for(int i=0;i<newIds.size();++i){auto* u=dynamic_cast<CUnit*>(reopened->nodeByMapId(newIds[i]));check(u&&u->toJson()==expected[i].toObject(),"reopened NPC/patrol/look/guard values exact");if(u){view.m_pLand->projectPosition(u);u->refreshLogicProjection();auto* originalUnit=static_cast<CUnit*>(mob->nodeByMapId(newIds[i]));
 check(qAbs(u->drawPosition().z()+u->minPosition().z()-originalUnit->drawPosition().z()-originalUnit->minPosition().z())<.01f,"reopen feet Z exact with deferred figure");}}
 delete reopened;check(bytes()==edited,"reopen/render never mutate original editing projection");
 {QFile f(output+"/receipt.json");check(f.open(QIODevice::WriteOnly),"receipt output");f.write(QJsonDocument(QJsonObject{{"result","PASS"},{"checks",passes},{"scenarios",scenarios},{"wallId",wallId},{"wallModel",wallModel},{"faces",view.m_unitSupportSurface.faceCount()}}).toJson());}
 view.clearObjectSelection();undo.clear();view.unloadMobDocument(mob,false);out<<"RESULT "<<passes<<" PASS\n";return 0;
 }catch(const QString& reason){out<<"FAIL "<<reason<<"\n";return 1;}
}

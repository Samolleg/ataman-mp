#include <QtWidgets>
#include <QtOpenGL>
#include "view.h"
#include "camera.h"
#include "settings.h"
#include "resourcemanager.h"
#include "landscape.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "scene.h"
#include "unit.h"
#include "undo.h"
#include "property.h"
#include "render_queue.h"
#include "helper_visibility.h"
#include "shader_locations.h"
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QCryptographicHash>

namespace {
class HiddenWindows : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
using Query=decltype(QOpenGLFunctionsPrivate::Functions::GetUniformLocation);
Query originalQuery;int driverQueries=0;
GLint QOPENGLF_APIENTRY countQuery(GLuint program,const char* name)
{++driverQueries;return originalQuery(program,name);}
class QueryHook:public QOpenGLFunctions {
public:QueryHook(){initializeOpenGLFunctions();originalQuery=d_ptr->f.GetUniformLocation;d_ptr->f.GetUniformLocation=countQuery;}
~QueryHook(){d_ptr->f.GetUniformLocation=originalQuery;}
};
QByteArray imageHash(const QImage& source){const auto image=source.convertToFormat(QImage::Format_RGBA8888);QCryptographicHash hash(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)),image.width()*4);return hash.result().toHex();}
}
int runNeighborPerformanceCheck()
{
 QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
 auto check=[&](bool ok,const char* name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw QString::fromLatin1(name);++passes;};
 try {
 HiddenWindows hidden;qApp->installEventFilter(&hidden);
 QTemporaryDir scratch;QDir::setCurrent(scratch.path());QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
 const QString fixture=qEnvironmentVariable("EI_PATROL_FIXTURE"),output=qEnvironmentVariable("EI_PATROL_OUTPUT");check(!fixture.isEmpty()&&!output.isEmpty(),"explicit private fixtures/output");QDir().mkpath(output);
 CSettings settings;auto boolean=[&](const char* name,bool value){QString key=QString::fromLatin1(name);if(auto* opt=dynamic_cast<COptBool*>(settings.opt(key)))opt->setValue(value);};
 dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue("C:/GOG Games/PZ");
 dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue({"C:/GOG Games/PZ/MODS/DejaVu/res","C:/GOG Games/PZ/Base/Res"});
 CTextureList::getInstance()->attachSettings(&settings);CObjectList::getInstance()->attachSettings(&settings);
 QUndoStack undo;QTableWidget table;table.setColumnCount(2);CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
 CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);view.attach(&settings,&table,&undo,&progress,&coordinates,&tree,&tiles);view.resize(1280,900);view.show();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"hidden native GL context");
 view.setWorkspaceLoadInProgress(true);view.m_particleVisualsVisible=false;view.m_freezeAnimations=true;view.m_animationFreezeTime=0;
 QFileInfo map(fixture+"/zone3obr.mpr");view.m_pLand->readMap(map,false);QFileInfo mobFile(fixture+"/zone3obr.mob");check(view.loadMob(mobFile),"private owner MOB loaded");view.makeCurrent();
 boolean("animateUnits",false);boolean("drawHelp",false);boolean("drawLogic",true);CScene::getInstance()->changeMode(eEditModeLogic);
 auto* mob=view.m_activeMob;CUnit* unit=nullptr;
 for(auto* node:mob->nodes())if(auto* u=dynamic_cast<CUnit*>(node))if(u->logicAt(0)->patrolPoints().size()>2){unit=u;break;}
 check(unit!=nullptr,"NPC with existing patrol and look points");auto* point=unit->logicAt(0)->patrolByIndex(0);view.moveCameraToPoint(point->drawPosition());view.m_cam->moveAwayOn(45);unit->setState(eSelect);
 // Prime the normal renderer state. All measured/output helper frames use a
 // real owned FBO; no window presentation is performed on the user's desktop.
 view.draw();view.makeCurrent();QueryHook hook;
 QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);QOpenGLFramebufferObject fbo(1280,900,format);check(fbo.isValid(),"complete private framebuffer");
 const auto mobBytes=[&](){QByteArray bytes;mob->serializeMob(bytes,false);return bytes;};const QByteArray original=mobBytes();
 QJsonArray frames;QJsonArray queryCounts;
 auto frame=[&](const QString& name,bool culling){view.makeCurrent();check(fbo.bind(),"bind owned helper framebuffer");glViewport(0,0,1280,900);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  view.m_program.bind();auto* program=&view.m_program;program->setUniformValue(ei::uniformLocation(program,"u_projMmatrix"),view.m_projection);program->setUniformValue(ei::uniformLocation(program,"u_viewMmatrix"),view.m_cam->viewMatrix());program->setUniformValue(ei::uniformLocation(program,"u_environmentEnabled"),false);program->setUniformValue(ei::uniformLocation(program,"u_highlightColor"),QVector4D(.1f,.85f,1,1));
  CViewFrustum frustum(view.m_projection*view.m_cam->viewMatrix());ei::CHelperVisibilityScope visibility(culling?&frustum:nullptr);
  auto& queue=RenderQueueContext::instance();queue.begin(program,{241,54,40});const int before=driverQueries;QElapsedTimer timer;timer.start();for(auto* node:mob->nodes())node->drawLogicVisual(true,program);queue.end();const auto ms=timer.elapsed();glFinish();const auto pixels=fbo.toImage();
  check(glGetError()==GL_NO_ERROR,"helper frame has no GL errors");const auto hash=imageHash(pixels);if(!name.isEmpty()){pixels.save(output+"/"+name+".png");frames.append(QJsonObject{{"name",name},{"sha256",QString::fromLatin1(hash)},{"ms",ms}});queryCounts.append(driverQueries-before);}
  return hash;};
 frame(QString(),false);const auto all=frame("all",false);const auto culled=frame("culled",true);check(all==culled,"culling preserves every helper pixel including crossing routes");
 const int warm=driverQueries;frame("warm",true);
#ifndef EI_PATROL_BASELINE
 check(driverQueries==warm,"warm helper rendering performs zero driver uniform queries");
#endif
 check(mobBytes()==original,"rendering leaves complete MOB bytes unchanged");
#ifndef EI_PATROL_BASELINE
 view.draw();view.draw();const int sceneWarm=driverQueries;view.draw();
 check(driverQueries==sceneWarm,"warm complete scene performs zero driver uniform queries");
#endif
#ifdef EI_PATROL_BASELINE
 {QFile receipt(output+"/receipt.json");check(receipt.open(QIODevice::WriteOnly),"baseline renderer receipt");receipt.write(QJsonDocument(QJsonObject{{"result","PASS"},{"frames",frames},{"queries",queryCounts},{"checks",passes},{"renderOnly",true}}).toJson());}
 view.clearObjectSelection();undo.clear();view.unloadMobDocument(mob,false);out<<"RESULT baseline renderer "<<passes<<" passed\n";return 0;
#endif
 view.restoreSelection({point});const int patrols=unit->logicAt(0)->patrolPoints().size();undo.push(new CCreatePatrolCommand(&view,point,QString::number(unit->mapId())+".0",{point}));
 check(unit->logicAt(0)->patrolPoints().size()==patrols+1,"native add-patrol command");const auto added=mobBytes();check(added!=original,"added patrol is serialized");undo.undo();check(mobBytes()==original,"add Undo exact MOB bytes");undo.redo();check(mobBytes()==added,"add Redo exact MOB bytes");
 auto* created=unit->logicAt(0)->patrolByIndex(1);undo.push(new CChangeLogicParam(&view,created,QSharedPointer<IPropertyBase>(new propFloat(eObjParam_POSITION_X,created->position().x()+2))));const auto moved=mobBytes();check(moved!=added,"patrol axis edit changes data");undo.undo();check(mobBytes()==added,"move Undo exact");undo.redo();check(mobBytes()==moved,"move Redo exact");
 undo.push(new CCreatePatrolCommand(&view,created,QString::number(unit->mapId())+".1.-1",{created}));auto* look=created->viewByIndex(0);check(look!=nullptr,"native add-look command");const auto looked=mobBytes();undo.undo();check(mobBytes()==moved,"look Undo exact");undo.redo();check(mobBytes()==looked,"look Redo exact");
 look=created->viewByIndex(0);view.restoreSelection({look});frame("selected-look",true);undo.push(new CDeleteLogicPoint(&view,QString::number(unit->mapId())+".1.0"));const auto deleted=mobBytes();check(deleted!=looked,"delete look point");undo.undo();check(mobBytes()==looked,"delete look Undo exact");undo.redo();check(mobBytes()==deleted,"delete look Redo exact");undo.undo();
 view.restoreSelection({});undo.push(new CDeleteLogicPoint(&view,QString::number(unit->mapId())+".1"));check(mobBytes()!=looked,"delete patrol with child look");undo.undo();check(mobBytes()==looked,"delete patrol Undo exact");undo.redo();undo.undo();check(mobBytes()==looked,"delete patrol Redo then Undo exact");
 view.setLogicSlotIndex(2);check(view.logicSlotIndex()==2,"switch logic slot");frame("slot2",true);view.setLogicSlotIndex(0);check(mobBytes()==looked,"slot switching does not change MOB");
 const QString saved=scratch.filePath("saved.mob");QJsonObject expected;unit->serializeJson(expected);check(view.saveMobDocument(mob,QFileInfo(saved)),"real atomic MOB Save");
 auto* reopened=new CMob;reopened->attach(&view,&progress);QFileInfo savedInfo(saved);check(reopened->readMob(savedInfo)==0,"reopen saved MOB");auto* savedUnit=dynamic_cast<CUnit*>(reopened->nodeByMapId(unit->mapId()));QJsonObject actual;if(savedUnit)savedUnit->serializeJson(actual);check(savedUnit&&actual==expected,"saved patrol/look data reopen exactly");delete reopened;
 undo.undo();undo.redo();check(mobBytes()==looked,"Undo/Redo after Save preserves edits");
 QFile receipt(output+"/receipt.json");check(receipt.open(QIODevice::WriteOnly),"receipt output");receipt.write(QJsonDocument(QJsonObject{{"result","PASS"},{"frames",frames},{"queries",queryCounts},{"checks",passes},{"nodes",mob->nodes().size()}}).toJson());receipt.close();
 view.clearObjectSelection();undo.clear();view.unloadMobDocument(mob,false);out<<"RESULT "<<passes<<" passed\n";return 0;
 }catch(const QString& reason){out<<"FAIL "<<reason<<"\n";return 1;}
}

#include <QtWidgets>
#include <QOpenGLFunctions>
#define private public
#define protected public
#include "main_window.h"
#include "view.h"
#undef private
#undef protected
#include "landscape.h"
#include "camera.h"
#include "mob.h"
#include "node.h"
#include "settings.h"
#include "profile_database.h"
#include "resource_profile.h"
#include "ai_graph_preview_builder.h"
#include <functional>
#include <cmath>

namespace {
QTextStream out(stdout);
void require(bool b,const QString& s){out<<(b?"PASS ":"FAIL ")<<s<<'\n';out.flush();if(!b)throw s;}
QByteArray read(const QString& p){QFile f(p);require(f.open(QIODevice::ReadOnly),"read "+p);return f.readAll();}
void write(const QString& p,const QByteArray& b){QSaveFile f(p);require(f.open(QIODevice::WriteOnly)&&f.write(b)==b.size()&&f.commit(),"write "+p);}
bool until(std::function<bool()> done,int ms){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms){qApp->processEvents(QEventLoop::AllEvents,15);QThread::msleep(1);}return done();}
struct Hidden:QObject{
 QStringList dialogs;
 bool eventFilter(QObject* o,QEvent* e)override{
  if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
  if(e->type()==QEvent::Show)if(auto* box=qobject_cast<QMessageBox*>(o)){dialogs.append(box->text());QTimer::singleShot(0,box,&QDialog::reject);}
  return false;
 }
};
}
int main(int argc,char** argv){
 QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
 QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);app.setProperty("workspaceLoadSmoke",true);
 QCoreApplication::setOrganizationName("EI Mapper Endurance");QCoreApplication::setApplicationName("EI Mapper Endurance Edition");
 const QString root=qEnvironmentVariable("EI_BRIEF_ROOT"),mode=qEnvironmentVariable("EI_BRIEF_MODE");
 Hidden hidden;app.installEventFilter(&hidden);out.setCodec("UTF-8");
 try{
  require(!root.isEmpty()&&(mode=="old"||mode=="new"||mode=="verify"),"explicit isolated root and mode");
  QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/render-run/settings");
  QSettings().setValue("ui/viewportVisuals/neighborLandscapes",false);
  MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);window.resize(1750,1200);window.show();
  auto& view=*window.view();view.show();app.processEvents();view.makeCurrent();view.m_timer->stop();view.m_neighborLandscapesVisible=false;
  require(view.isValid()&&window.testAttribute(Qt::WA_DontShowOnScreen),"production MainWindow native GL without desktop window");
  ei::SProfileDatabaseLoadRequest request;QMap<QString,QString> bindings;
  request.resourceDirectories=dynamic_cast<COptStringList*>(view.settings()->opt(eOptSetResource,"resourceRoots"))->value().toList();
  for(const auto& profile:builtInResourceProfiles())if(profile.id=="deja-vu"){
   request.profileId=profile.id;request.runtimeManifestId=profile.runtimeManifestId;request.providerId=profile.providers.value("database").id;
   for(auto it=profile.providers.begin();it!=profile.providers.end();++it)bindings[it.key()]=it.value().id;
  }
  ei::ProfileDatabaseService databases;auto db=databases.snapshot(request);require(db&&db->usable(),"DejaVu database available");ei::ProfileDatabaseContext::instance().activate(db,bindings);
  const QString dir=root+(mode=="old"?"/in":"/out");
  require(window.loadLandscapeWithProgress(QFileInfo(dir+"/basegipat.mpr")),"MPR through production loader");
  require(window.loadMobWithProgress(QFileInfo(dir+"/basecam.mob"),false),"MOB through production loader");
  require(until([&](){return !window.backgroundMobLoading();},90000),"primary MOB ready");
  require(view.mobs().size()==1,"one map, no AddMob");view.m_timer->stop();
  if(mode=="verify") {
   const QByteArray bytes=read(dir+"/basecam.mob");QByteArray candidate;
   require(view.mobs().first()->buildValidatedCandidate(&candidate)&&candidate==bytes,"production CMob no-op save is byte-identical after final graph write");
   require(view.m_aiGraphLoadedDocument.isValid()&&view.m_aiGraphLoadedDocument.width()==88&&view.m_aiGraphLoadedDocument.height()==72,"final embedded 88x72 graph loaded by production view");
   int count=0;for(auto* n:view.mobs().first()->nodes())if(n->nodeType()==eUnit)++count;require(count==51,"all 51 original units in final production scene");
   require(hidden.dialogs.isEmpty(),"no final load-error dialogs");
   write(root+"/render-verify.json",QJsonDocument(QJsonObject{{"units",count},{"noOpByteIdentical",true},{"graphWidth",88},{"graphHeight",72},{"dialogs",QJsonArray::fromStringList(hidden.dialogs)}}).toJson());
   window.closeAll();window.close();return 0;
  }
  const QVector3D center=mode=="old"?QVector3D(69,79,0):QVector3D(197,143,0);
  auto warm=[&](float angle,float distance,const QVector3D& target){view.m_cam->setLocation(target+QVector3D(0,0,distance),target,{angle,0,0});
   for(int i=0;i<14;++i){view.makeCurrent();view.paintGL();app.processEvents(QEventLoop::AllEvents,20);QThread::msleep(20);}};
  QDir().mkpath(root+"/screenshots");
  auto capture=[&](const QString& name,float angle,float distance,const QVector3D& target){warm(angle,distance,target);auto img=view.grabFrameBuffer();require(!img.isNull()&&img.save(root+"/screenshots/"+name+".png"),"capture "+name);};
  capture(mode+"-village-top",0,130,center);capture(mode+"-village-angle",-45,130,center);
  QJsonObject result{{"mode",mode},{"mobCount",view.mobs().size()},{"viewportWidth",view.width()},{"viewportHeight",view.height()},{"dialogs",QJsonArray::fromStringList(hidden.dialogs)}};
  QJsonArray positions;for(auto* n:view.mobs().first()->nodes())if(n->nodeType()==eUnit){const auto pos=n->position(),draw=n->drawPosition();positions.append(QJsonObject{{"id",double(n->mapId())},{"stored",QJsonArray{pos.x(),pos.y(),pos.z()}},{"draw",QJsonArray{draw.x(),draw.y(),draw.z()}}});}result["units"]=positions;
  if(mode=="new"){
   capture("new-full-map-top",0,430,{176,144,0});
   view.m_canvasNavigationStale=true;SAiGraphPreviewBuildInput input;QString error;
   require(view.captureAiGraphPreviewBuildInput(&input,&error),"production graph inputs: "+error);
   require(input.graphWidth==88&&input.graphHeight==72,"new full terrain graph 88x72");
   const auto built=CAiGraphPreviewBuilder::build(input);require(built.valid,"production AI graph builder: "+built.error);
   auto graph=built.document;require(graph.recolorConnectedComponents(&error),"standard graph component finalization: "+error);
   QStringList warnings;require(graph.validateRuntimeCompatibility(88,72,&error,&warnings),"graph runtime structure: "+error);
   const auto graphBytes=graph.serialize(&error);require(!graphBytes.isEmpty(),"graph serialization");
   CMobDocument d;CValidationReport report;const auto source=read(dir+"/basecam.mob");require(d.loadBytes(source,"basecam.mob",&report),"strict graph target parse");QByteArray candidate;
   require(d.replaceEmbeddedAiGraph(graphBytes,&candidate,&report),"replace only embedded AI graph");CMobDocument check;
   require(check.loadBytes(candidate,"basecam.mob",&report)&&!report.hasErrors()&&check.serialize()==candidate,"strict final MOB and byte-identical no-op");write(dir+"/basecam.mob",candidate);
   result["graph"]=QJsonObject{{"width",88},{"height",72},{"bytes",graphBytes.size()},{"collisionProxies",input.collisionProxies.size()},{"authoredProxies",built.authoredCollisionProxyCount},{"approximateCollisionMetadata",input.collisionMetadataApproximate},{"blockedAiTiles",built.blockedAiTiles},{"warnings",QJsonArray::fromStringList(warnings+built.warnings)}};
   const auto plan=QJsonDocument::fromJson(read(root+"/sample-request.json")).object();QJsonArray points;
   const int w=input.graphWidth*8,h=input.graphHeight*8;
   for(const auto v:plan["points"].toArray()) {auto q=v.toObject();const double x=q["x"].toDouble(),y=q["y"].toDouble();
    const int ix=int(std::floor(x*2)),iy=int(std::floor(y*2));const bool in=ix>=0&&iy>=0&&ix<w&&iy<h;q["inBounds"]=in;
    if(in){const int i=iy*w+ix;q["waterDepth"]=input.waterDepths[i];q["humanPassable"]=built.humanPassability.value(i)!=0;QVector3D projected;q["nearPassable"]=built.projectHumanPosition({x,y},&projected,0.8f);if(q["nearPassable"].toBool())q["nearestPassable"]=QJsonArray{projected.x(),projected.y(),projected.z()};}
    points.append(q);
   }write(root+"/navigation-review.json",QJsonDocument(points).toJson());
  }
  require(hidden.dialogs.isEmpty(),"no load-error dialogs");write(root+"/render-"+mode+".json",QJsonDocument(result).toJson());
  window.closeAll();window.close();return 0;
 }catch(const QString& e){out<<"FAIL "<<e<<'\n';out.flush();return 1;}
}

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
#include "map_table_document.h"
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
  const bool compare=mode.startsWith("compare-"),mark=mode.startsWith("mark-"),squad=mode.startsWith("squad-"),oldScene=mode=="old"||mode.endsWith("-old");
  require(!root.isEmpty()&&(mode=="old"||mode=="new"||mode=="verify"||compare||mark||squad),"explicit isolated root and mode");
  QJsonObject scene;if(QFileInfo::exists(root+"/scene.json"))scene=QJsonDocument::fromJson(read(root+"/scene.json")).object();
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
  const QString dir=root+(oldScene?"/in":"/out");
  const QString mprPath=oldScene?root+"/in/basegipat.mpr":scene["mpr"].toString(dir+"/basegipat.mpr");
  if(!oldScene&&scene.contains("catalog")){
   CMapTableDocument catalog;QStringList errors;require(catalog.loadBytes(read(scene["catalog"].toString()),&errors),"candidate catalog parse: "+errors.join(";"));
   bool pair=false;for(const auto& z:catalog.zones())if(z.zoneId=="bz1g")pair=z.mprName==QFileInfo(mprPath).completeBaseName()&&z.mobName=="basecam"&&z.zoneType=="brief";
   require(pair,"bz1g catalog selects the shared zone1 MPR and separate basecam MOB");
  }
  if(!oldScene&&scene.contains("catalog")){
   window.openMapPair(mprPath,dir+"/basecam.mob","bz1g");
   require(until([&](){return !window.m_workspaceLoadInProgress&&!window.backgroundMobLoading()&&view.mobs().size()==1;},90000),"catalog pair through production openMapPair");
   require(window.m_mapTestEntry.zoneId=="bz1g"&&window.m_mapTestEntry.isBriefing(),"production identity is bz1g brief, not gz1g");
  }else{
   require(window.loadLandscapeWithProgress(QFileInfo(mprPath)),"MPR through production loader: "+mprPath);
   require(window.loadMobWithProgress(QFileInfo(dir+"/basecam.mob"),false),"MOB through production loader");
  }
  require(until([&](){return !window.backgroundMobLoading();},90000),"primary MOB ready");
  require(view.mobs().size()==1,"one map, no AddMob");view.m_timer->stop();
  if(compare){
   require(until([&](){if(view.m_terrainEditAvailable&&!view.m_terrainAutoCatalogBuilding)return true;view.makeCurrent();view.paintGL();return false;},90000),"canonical terrain ready after production first-frame deferred work");
   const QVector3D focus=oldScene?QVector3D(64,64,0):QVector3D(192,128,0);view.m_cam->setLocation(focus+QVector3D(0,0,175),focus,{0,0,0});
   for(int i=0;i<14;++i){view.makeCurrent();view.paintGL();app.processEvents(QEventLoop::AllEvents,20);QThread::msleep(20);}
   view.m_canvasNavigationStale=true;SAiGraphPreviewBuildInput input;QString error;
   const bool captured=view.captureAiGraphPreviewBuildInput(&input,&error);
   require(captured,"production classifier inputs: "+error+QString(" editAvailable:%1 MPR:%2 vertices:%3x%4").arg(view.m_terrainEditAvailable).arg(view.m_pLand->isMprLoad()).arg(view.m_terrainEdit.vertexCountX()).arg(view.m_terrainEdit.vertexCountY()));
   const auto pass=CAiGraphPreviewBuilder::passability(input,3);require(pass.valid,"native human layer classification: "+pass.error);
   const auto quality=pass.layerPassability.value(3);const int w=input.graphWidth*8,h=input.graphHeight*8;require(quality.size()==w*h,"complete human passability grid");
   const auto request=QJsonDocument::fromJson(read(root+(oldScene?"/sample-old-request.json":"/sample-request.json"))).object();QJsonArray points;
   for(const auto v:request["points"].toArray()){
    auto q=v.toObject();const float x=float(q["x"].toDouble()),y=float(q["y"].toDouble());const int ix=int(std::floor(x*2)),iy=int(std::floor(y*2));const bool inside=ix>=0&&iy>=0&&ix<w&&iy<h;q["inBounds"]=inside;
    q["humanPassable"]=inside&&quality[iy*w+ix]!=0;
    if(inside)q["waterDepth"]=input.waterDepths[iy*w+ix];
    double best=4.00000001;QVector3D nearest;bool found=false;
    for(int py=qMax(0,iy-4);py<=qMin(h-1,iy+4);++py)for(int px=qMax(0,ix-4);px<=qMin(w-1,ix+4);++px){
     if(!quality[py*w+px])continue;const float nx=(px+.5f)*.5f,ny=(py+.5f)*.5f;const double dx=nx-x,dy=ny-y,d2=dx*dx+dy*dy;if(d2>=best)continue;
     bool available=false;const float z=view.m_terrainEdit.terrainSurfaceHeight(nx,ny,&available);if(!available)continue;best=d2;nearest={nx,ny,z};found=true;
    }
    q["passableWithin2m"]=found;if(found){q["nearestPassable"]=QJsonArray{nearest.x(),nearest.y(),nearest.z()};q["projectionDistance"]=std::sqrt(best);}points.append(q);
   }
   QJsonObject comparison{{"points",points},{"graphWidth",input.graphWidth},{"graphHeight",input.graphHeight},{"collisionProxies",input.collisionProxies.size()},{"approximate",input.collisionMetadataApproximate}};
   if(mode=="compare-rigid"){
    // Investigate a common bounded correction without breaking the group's
    // geometry or writing any map coordinates. Include the original offset.
    const auto rows=QJsonDocument::fromJson(read(root+"/rows.json")).array();const auto previous=QJsonDocument::fromJson(read(root+"/compare-old.json")).object()["points"].toArray();QSet<int> wasPassable;
    for(const auto v:previous){const auto q=v.toObject();if(q["inBounds"].toBool()&&q["humanPassable"].toBool())wasPassable.insert(q["index"].toInt());}
    int bestBlocked=INT_MAX,tested=0;QJsonArray options;
    for(int sy=-8;sy<=8;++sy)for(int sx=-8;sx<=8;++sx){const double dx=sx*.25,dy=sy*.25;if(dx*dx+dy*dy>4.000001)continue;QJsonArray blocked;
     for(const auto v:rows){const auto row=v.toObject();const auto kind=row["kind"].toString();if(!row["rigidGroup"].toBool()||!wasPassable.contains(row["index"].toInt())||(kind!="unit"&&kind!="guard"&&kind!="patrol"))continue;
      const auto p=row["new"].toArray();const int x=int(std::floor((p[0].toDouble()+dx)*2)),y=int(std::floor((p[1].toDouble()+dy)*2));if(x<0||y<0||x>=w||y>=h||!quality[y*w+x])blocked.append(row["index"]);
     }++tested;if(blocked.size()<bestBlocked){bestBlocked=blocked.size();options={};}if(blocked.size()==bestBlocked)options.append(QJsonObject{{"offset",QJsonArray{dx,dy}},{"blockedRows",blocked}});
    }comparison["rigidOffsetSearch"]=QJsonObject{{"radius",2},{"step",.25},{"tested",tested},{"minimumBlocked",bestBlocked},{"bestOptions",options},{"applied",false}};
   }
   write(root+"/"+mode+".json",QJsonDocument(comparison).toJson());
   require(hidden.dialogs.isEmpty(),"no classifier load-error dialogs");window.closeAll();window.close();return 0;
  }
  if(mode=="verify") {
   const QByteArray bytes=read(dir+"/basecam.mob");QByteArray candidate;
   require(view.mobs().first()->buildValidatedCandidate(&candidate)&&candidate==bytes,"production CMob no-op save is byte-identical after final graph write");
   require(view.m_aiGraphLoadedDocument.isValid()&&view.m_aiGraphLoadedDocument.width()==88&&view.m_aiGraphLoadedDocument.height()==72,"final embedded 88x72 graph loaded by production view");
   int count=0;for(auto* n:view.mobs().first()->nodes())if(n->nodeType()==eUnit)++count;require(count==51,"all 51 original units in final production scene");
   require(hidden.dialogs.isEmpty(),"no final load-error dialogs");
   write(root+"/render-verify.json",QJsonDocument(QJsonObject{{"units",count},{"noOpByteIdentical",true},{"graphWidth",88},{"graphHeight",72},{"dialogs",QJsonArray::fromStringList(hidden.dialogs)}}).toJson());
   window.closeAll();window.close();return 0;
  }
  const QVector3D center=oldScene?QVector3D(69,79,0):QVector3D(197,143,0);
  auto warm=[&](float angle,float distance,const QVector3D& target){view.m_cam->setLocation(target+QVector3D(0,0,distance),target,{angle,0,0});
   for(int i=0;i<14;++i){view.makeCurrent();view.paintGL();app.processEvents(QEventLoop::AllEvents,20);QThread::msleep(20);}};
  QDir().mkpath(root+"/screenshots");
  if(squad){
   const auto camera=scene[oldScene?"squadOld":"squadNew"].toObject();const auto f=camera["focus"].toArray();require(f.size()==3,"explicit squad camera");
   window.resize(2100,1550);app.processEvents();view.setWorldTravelAreas({});
   warm(camera["angle"].toDouble(-45),camera["distance"].toDouble(55),{float(f[0].toDouble()),float(f[1].toDouble()),float(f[2].toDouble())});
   const auto image=view.grabFrameBuffer();require(!image.isNull()&&image.save(root+"/screenshots/"+mode+".png"),"squad production capture");
   require(hidden.dialogs.isEmpty(),"no squad load-error dialogs");write(root+"/render-"+mode+".json",QJsonDocument(QJsonObject{{"mpr",mprPath},{"mob",dir+"/basecam.mob"},{"camera",camera},{"dialogs",QJsonArray::fromStringList(hidden.dialogs)}}).toJson());window.closeAll();window.close();return 0;
  }
  if(mark){
   // Native production scene, with only the requested review markers added.
   window.resize(2100,1850);app.processEvents();view.setWorldTravelAreas({});
   const QVector3D focus=oldScene?QVector3D(64,64,0):QVector3D(192,128,0);warm(0,175,focus);view.setWorldTravelAreas({});view.makeCurrent();view.paintGL();
   QImage image=view.grabFrameBuffer();require(!image.isNull(),"native review capture");
   const auto request=QJsonDocument::fromJson(read(root+"/markers.json")).array();QJsonArray projected;
   struct Label {QPointF point;QStringList numbers;};QMap<QString,Label> labels;
   for(const auto v:request){auto q=v.toObject();const auto xy=q[oldScene?"old":"new"].toArray();const float x=xy[0].toDouble(),y=xy[1].toDouble();bool available=false;const float z=view.m_terrainEdit.terrainSurfaceHeight(x,y,&available);
    const auto pixel=QVector3D(x,y,available?z:0).project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));const QPointF point(pixel.x()*image.width()/view.width(),(view.height()-pixel.y())*image.height()/view.height());
    q["pixel"]=QJsonArray{point.x(),point.y()};q["visible"]=pixel.z()>=0&&pixel.z()<=1&&QRectF(image.rect()).contains(point);projected.append(q);
    const QString key=QString::number(x,'f',2)+","+QString::number(y,'f',2);auto& l=labels[key];l.point=point;l.numbers.append(QString::number(q["csvLine"].toInt()));
   }
   QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);QFont font("Arial",11,QFont::Bold);painter.setFont(font);const QFontMetrics metrics(font);
   QVector<QRectF> occupied{QRectF(10,10,650,29)};
   for(auto it=labels.cbegin();it!=labels.cend();++it){const auto& l=it.value();
    const QString text=l.numbers.join(",");const QRectF shape=metrics.boundingRect(text).adjusted(-3,-2,3,2);QRectF box;bool placed=false;
    for(int radius=10;radius<=330&&!placed;radius+=16)for(int direction=0;direction<16&&!placed;++direction){const double angle=direction*6.283185307179586/16;QRectF option=shape;option.moveCenter(l.point+QPointF(std::cos(angle)*(radius+shape.width()/2),std::sin(angle)*(radius+shape.height()/2)));
     if(!QRectF(image.rect()).adjusted(4,42,-4,-4).contains(option))continue;bool overlap=false;for(const auto& prev:occupied)if(prev.adjusted(-3,-3,3,3).intersects(option)){overlap=true;break;}if(!overlap){box=option;placed=true;}
    }
    if(!placed){box=shape;box.moveTopLeft(l.point+QPointF(6,-shape.height()-3));}occupied.append(box);
    painter.setPen(QPen(QColor(255,100,80),1));painter.drawLine(l.point,box.center());painter.setPen(QPen(QColor(255,70,65),2));painter.setBrush(QColor(255,70,65));painter.drawEllipse(l.point,3,3);
    painter.fillRect(box,QColor(0,0,0,215));painter.setPen(QColor(255,235,125));painter.drawText(box,Qt::AlignCenter,text);
   }
   painter.fillRect(QRect(10,10,650,29),QColor(0,0,0,205));painter.setPen(Qt::white);painter.drawText(QRect(17,10,640,29),Qt::AlignVCenter,QString::fromUtf8(oldScene?"СТАРАЯ | номера строк review.csv (заголовок = 1)":"НОВАЯ | номера строк review.csv (заголовок = 1)"));painter.end();
   require(image.save(root+"/screenshots/"+mode+".png"),"save numbered review image");write(root+"/screenshots/"+mode+"-points.json",QJsonDocument(projected).toJson());
   require(hidden.dialogs.isEmpty(),"no review capture dialogs");window.closeAll();window.close();return 0;
  }
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

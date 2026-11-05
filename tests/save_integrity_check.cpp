// EI-SAVE-INTEGRITY-280. Production objects, isolated fixtures, no desktop control.
#include <QtWidgets>
#include <QtTest>
#include <QtNetwork>
#include <QtConcurrent>
#include <windows.h>
#include <cstring>
#define private public
#include "view.h"
#include "main_window.h"
#include "mob.h"
#include "resource_browser.h"
#undef private
#include "landscape.h"
#include "settings.h"
#include "resource_setup_dialog.h"
#include "resourcemanager.h"
#include "terrain_atlas_compact.h"
#include "res_archive_index.h"
#include "res_save_coordinator.h"
#include "staged_save_validator.h"
#include "res_file.h"
#include "ai_agent_read.h"
#include "ai_agent_server.h"
#include "log.h"
#include "res_write_audit.h"
#include "profile_database.h"
#include "travel_document.h"
namespace {
QTextStream out(stdout);QString root;int passes=0;
void check(bool value,const QString& label){out<<(value?"PASS ":"FAIL ")<<label<<'\n';out.flush();if(!value)throw label;++passes;}
QByteArray read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw "Cannot read "+path;return file.readAll();}
void write(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw "Cannot write "+path;}
QByteArray digest(const QByteArray& b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
QString input(const QString& name){return qEnvironmentVariable("EI_SAVE_FIXTURE")+"/"+name;}
QByteArray renamed(const QByteArray& bytes,const QString& ns){CMprDocument map;CValidationReport report;CMprEditTransaction tx("test namespace");QByteArray result;check(map.loadBytes(bytes,"zone1.mpr",&report)&&map.applyIsolatedNamespaceTransaction(tx,ns,&result,&report),"real MPR namespace candidate");return result;}
QByteArray archive(const QString& ns,const QByteArray& data){CResFile file;file.addFiledata(ns+"000.mmp",data);file.addFiledata("preserved.bin","unchanged");return file.generateResData();}
QVector<SMapSaveFile> packet(bool prepare){
 QVector<SMapSaveFile> files;
 const QStringList names{"zone1.mpr","HDPack_Terrain.res","zone1.mob","second.mob"};
 if(prepare){
  const auto original=read(input("zone1.mpr"));CMprDocument map;CValidationReport report;CMprEditTransaction tx("test maxZ");QByteArray after;
  check(map.loadBytes(original,"zone1.mpr",&report)&&map.appendMaxZEdit(map.mapInfo().maxZ+1,&tx,&report)&&map.applyTransaction(tx,&after,&report),"real DejaVu MPR candidate");
  write(root+"/before/zone1.mpr",original);write(root+"/after/zone1.mpr",after);
  const auto originalMob=read(input("zone1.mob"));CMob mob;check(mob.deserialize(originalMob),"real DejaVu MOB projection");mob.adoptCommittedBytes(originalMob);mob.setScript(mob.script()+"\r\n// save-integrity-280\r\n");QByteArray mobAfter;QString kind;
  check(mob.buildValidatedCandidate(&mobAfter,&report,&kind),"real MOB candidate: "+report.toText());const auto verified=validateStagedSave(mobAfter,QFileInfo(root+"/zone1.mob"));check(verified.success,"validated edited real MOB: "+verified.error);
  for(const auto& name:QStringList{"zone1.mob","second.mob"}){write(root+"/before/"+name,originalMob);write(root+"/after/"+name,mobAfter);}
  write(root+"/before/HDPack_Terrain.res",archive("old_aaaaaaaaaaaa","before"));write(root+"/after/HDPack_Terrain.res",archive("new_bbbbbbbbbbbb","after"));
 }
 for(const auto& name:names){SMapSaveFile f;f.path=root+"/"+name;f.before=read(root+"/before/"+name);f.after=read(root+"/after/"+name);files.append(f);if(prepare)write(f.path,f.before);}
 return files;
}
void nested(){
 const auto old=QString("zone1_aaaaaaaaaaaa");const auto newns=QString("zone1_bbbbbbbbbbbb");
 const auto real=read(input("zone1.mpr"));write(root+"/Maps/A.mpr",renamed(real,newns));write(root+"/Maps/subdir/deeper/B.MPR",renamed(real,old));QDir().mkpath(root+"/OtherMaps");
 const auto path=root+"/HDPack_Terrain.res";write(path,archive(old,"old atlas bytes"));STerrainAtlasProfile profile;profile.mprNamespace=old;
 const auto cleanup=retireTerrainAtlasAfterSave(path,profile,root+"/Maps/A.mpr",{root+"/Maps",root+"/OtherMaps"},root+"/backups");
 CResArchiveIndex index;check(index.open(path),"fresh retired archive reopen");
#if defined(EI_BASELINE_279)
 check(index.memberIndex(old+"000.mmp")==-1,"279 reproduces erroneous nested atlas deletion");
#else
 check(index.memberIndex(old+"000.mmp")>=0,"280 preserves atlas referenced by nested MPR");
 CResArchiveDocument memory;check(memory.loadBytes(read(path),path),"memory archive reopen");QMap<QString,QByteArray> snapshots;QString reason;
 check(terrainCompactRetiredMembers(memory,profile,root+"/Maps/A.mpr",{root+"/missing"},&snapshots,&reason).isEmpty()&&reason.contains("missing"),"incomplete inventory retains namespace and reason");
 write(root+"/OtherMaps/malformed.mpr","bad");
 check(terrainCompactRetiredMembers(memory,profile,root+"/Maps/A.mpr",{root+"/OtherMaps"},&snapshots,&reason).isEmpty()&&reason.contains("malformed.mpr"),"unreadable MPR prevents cleanup with exact path");
#endif
 out<<"CLEANUP "<<cleanup<<'\n';out.flush();
}
struct Hidden:QObject{bool eventFilter(QObject* obj,QEvent* e)override{if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(obj))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
bool until(const std::function<bool()>& done,int timeout=60000){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<timeout)QTest::qWait(5);return done();}
void uiSave(bool mcp){
 const bool assembly=qEnvironmentVariableIsSet("EI_TEST_ASSEMBLY");
 Hidden hidden;qApp->installEventFilter(&hidden);
 QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
 MainWindow window;setup.stop();window.setAttribute(Qt::WA_DontShowOnScreen);window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.m_currentCollectionPath=root+"/test.collection";
 auto& settings=*window.m_settings;const auto resource=QDir::fromNativeSeparators(qEnvironmentVariable("EI_SAVE_RESOURCE_ROOTS")).split(';',QString::SkipEmptyParts);
 dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(root);
 dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue(resource.toVector());dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"mapRoots"))->setValue({root+"/Maps"});
 CTextureList::getInstance()->attachSettings(&settings);auto& v=*window.view();window.show();v.show();QCoreApplication::processEvents();v.makeCurrent();check(v.isValid(),"hidden production GL initialized");
 QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){out<<"DIALOG "<<box->text()<<'\n';out.flush();if(auto* save=box->button(QMessageBox::Save))save->click();else box->reject();}});dialogs.start(10);
 const auto mapPath=root+"/Maps/zone1.mpr";write(mapPath,read(input("zone1.mpr")));write(root+"/Maps/zone1.mob",read(input("zone1.mob")));
 v.loadLandscape(QFileInfo(mapPath),false);check(v.m_pLand->isMprLoad(),"real zone1 copy opened");
 QFileInfo mobPath(root+"/Maps/zone1.mob");QStringList warnings;check(v.loadMob(mobPath,&warnings),"real zone1 MOB loaded");
 check(until([&](){return !window.backgroundMobLoading();}),"MOB load completed");
 check(until([&](){return v.ensureTerrainAutoCatalogReady();},120000),"terrain catalog ready before editing");
 auto* mob=v.currentMob();check(mob!=nullptr,"active MOB available");
 const auto before=read(mapPath);const auto beforeMob=read(mobPath.absoluteFilePath());
 STerrainBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=2;CValidationReport report;
 const auto edit=v.m_terrainEdit.makeRaiseLower(brush,40);v.m_terrainEdit.apply(edit,true,&report);v.m_pLand->setDirty(true);
 mob->setScript(mob->script()+"\r\n// ui-save-280\r\n");mob->setDirty(true);
 if(assembly){auto db=QSharedPointer<ei::SProfileDatabaseSnapshot>::create();db->sourceArchivePath=root+"/database.res";db->profileId="deja-vu";ei::ProfileDatabaseContext::instance().activate(db);
 check(!mob->nodes().isEmpty(),"assembly fixture node available");mob->nodes().first()->setAssemblyTag({{"instanceId",QUuid::createUuid().toString()},{"name","save280 test group"}});}

 QElapsedTimer timer;timer.start();
 if(!mcp){
#if !defined(EI_BASELINE_279)
 if(assembly){qputenv("EI_TEST_FAIL","2");window.on_actionSave_landscape_MPR_triggered();qunsetenv("EI_TEST_FAIL");
 check(read(mapPath)==before&&read(mobPath.absoluteFilePath())==beforeMob&&!QFileInfo::exists(root+"/model-catalog.json"),"failed MPR restores MOB and removes newly created assembly catalog");}
#endif
#if !defined(EI_BASELINE_279)
 QString familyError;check(v.saveMapFamily(&familyError),"explicit family save: "+familyError);
#else
 window.on_actionSave_landscape_MPR_triggered();
#endif
#if defined(EI_BASELINE_279)
  v.saveAllMob();
#endif
 }else{
  window.findChild<QCheckBox*>("aiAgentAllowSave")->setChecked(true);window.findChild<QComboBox*>("aiAgentMode")->setCurrentIndex(2);
  check(window.m_aiAgent->enabled(),"isolated production agent server enabled");
  QProcess bridge;bridge.start(qEnvironmentVariable("EI_SAVE_BRIDGE"),QStringList{});check(bridge.waitForStarted(),"own stdio MCP bridge starts");QByteArray buffer;int serial=0;
  const auto call=[&](const QString& method,const QJsonObject& params){const auto id=++serial;bridge.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');while(true){check(until([&](){buffer+=bridge.readAllStandardOutput();return buffer.contains('\n');}),"MCP reply received");QJsonObject result;QString error;if(!ai::takeFrame(buffer,&result,&error))throw error;if(result.contains("id"))return result;}};
  call("initialize",{{"protocolVersion","2025-11-25"},{"clientInfo",QJsonObject{{"name","save-integrity-280"},{"version","1"}}},{"capabilities",QJsonObject{}}});bridge.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
  const auto tool=[&](const QString& name,const QJsonObject& args){const auto envelope=call("tools/call",{{"name",name},{"arguments",args}});const auto content=envelope["result"].toObject()["content"].toArray();if(content.isEmpty())throw QString::fromUtf8(QJsonDocument(envelope).toJson());return QJsonDocument::fromJson(content[0].toObject()["text"].toString().toUtf8()).object();};
  const auto selected=tool("connect_instance",{{"target","editor"}});check(!selected.contains("error"),"connect only own isolated endpoint");
  const auto ctx=tool("get_context",{});check(ctx["mpr"].toString()==mapPath,"MCP context identifies owned map");
  const auto revision=tool("get_revision",{});const auto started=tool("save",{{"revision",revision["revision"]}});check(started.contains("operation"),"MCP normal Save started");QJsonObject done;
  check(until([&](){done=tool("operation_status",{{"operation",started["operation"]}});return done["status"]=="complete";},120000),"MCP Save completed");check(done["result"].toObject()["status"]=="saved","MCP Save result saved");
  bridge.closeWriteChannel();check(bridge.waitForFinished(4000),"own MCP bridge exits");
 }
 out<<"METRIC saveMs="<<timer.elapsed()<<" version="<<QCoreApplication::applicationVersion()<<" mcp="<<mcp<<" mprBytes="<<before.size()<<" mobBytes="<<beforeMob.size()<<'\n';out.flush();
 check(!v.m_pLand->isDirty()&&!mob->isDirty(),QString("whole production family clean after Save mpr=%1 mob=%2").arg(v.m_pLand->isDirty()).arg(mob->isDirty()));check(read(mapPath)!=before&&read(mobPath.absoluteFilePath())!=beforeMob,"MPR and MOB published together");
 if(assembly){const auto catalog=QJsonDocument::fromJson(read(root+"/model-catalog.json")).object();check(!catalog["assemblyInstances"].toObject().isEmpty(),"assembly metadata published in same successful batch");}
 CMprDocument reopened;check(reopened.load(QFileInfo(mapPath),&report)&&!report.hasErrors(),"saved zone1 fresh reopen");CMobDocument mobReopen;check(mobReopen.loadBytes(read(mobPath.absoluteFilePath()),mobPath.absoluteFilePath(),&report)&&!report.hasErrors(),"saved real MOB fresh reopen");
 window.m_aiAgent->stop();v.m_worldTerrainSession.clear();v.unloadLand(false);for(auto* loaded:v.mobs())loaded->setDirty(false);qApp->removeEventFilter(&hidden);
}
void commandScope(bool crashOnly)
{
 const auto resources=QDir::fromNativeSeparators(qEnvironmentVariable("EI_SAVE_RESOURCE_ROOTS")).split(';',QString::SkipEmptyParts);
 for(const auto& source:resources)check(QDir::fromNativeSeparators(source).contains("/work/"),"resource source belongs to work profile");
 QJsonArray resourceArray;for(const auto& path:resources)resourceArray.append(path);
 write(root+"/endurance-options.json",QJsonDocument(QJsonObject{{"Version","1"},{"General",QJsonObject{{"resourceProfile","deja-vu"},{"gameRoot",root},{"autosaveEnabled",false},{"language","en"}}},{"Resource",QJsonObject{{"resourceRoots",resourceArray},{"mapRoots",QJsonArray{root+"/Maps"}},{"terrainAuthoringArchive",""}}}}).toJson());
 QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<CResourceSetupDialog*>(widget))dialog->reject();});setup.start(1);
 MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.m_currentCollectionPath=root+"/scope.collection";
 auto& settings=*window.m_settings;dynamic_cast<COptString*>(settings.opt("resourceProfile"))->setValue("deja-vu");dynamic_cast<COptString*>(settings.opt("gameRoot"))->setValue(root);dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"))->setValue(resources.toVector());dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"mapRoots"))->setValue({root+"/Maps"});CTextureList::getInstance()->attachSettings(&settings);
 auto& v=*window.view();window.setAttribute(Qt::WA_DontShowOnScreen);window.show();v.show();QCoreApplication::processEvents();v.makeCurrent();check(v.isValid(),"hidden scope GL initialized");
 QStringList dialogTexts;
 QTimer dialogs;QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)){
  dialogTexts.append(box->windowTitle()+"\n"+box->text()+"\n"+box->detailedText());out<<"DIALOG "<<box->windowTitle()<<" "<<box->text()<<'\n';out.flush();
  if(auto* save=box->button(QMessageBox::Save))save->click();else if(auto* ok=box->button(QMessageBox::Ok))ok->click();else box->reject();
 }});dialogs.start(10);
 const auto mapPath=root+"/Maps/zone1.mpr",firstPath=root+"/Maps/zone1.mob",secondPath=root+"/Maps/second.mob";
 write(mapPath,read(input("zone1.mpr")));write(firstPath,read(input("zone1.mob")));write(secondPath,read(input("zone1.mob")));
 v.loadLandscape(QFileInfo(mapPath),false);check(v.m_pLand->isMprLoad(),"zone1 scope map loaded");
 QFileInfo firstInfo(firstPath),secondInfo(secondPath);QStringList warnings;check(v.loadMob(firstInfo,&warnings),"first real MOB loaded");auto* first=v.currentMob();check(v.loadMob(secondInfo,&warnings),"second real MOB loaded");auto* second=v.currentMob();v.changeCurrentMob(first);
 check(until([&](){return !window.backgroundMobLoading()&&v.ensureTerrainAutoCatalogReady();},120000),"complete scope documents ready");
 const auto resPath=QDir(resources.first()).filePath("HDPack_Terrain.res");const auto resBefore=resFileSha256(resPath);check(!resBefore.isEmpty(),"private large RES snapshot");
 const auto mapBefore=read(mapPath),mobBefore=read(firstPath);CValidationReport report;int editSerial=0;
 const auto editMap=[&](){STerrainBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=2;const auto command=v.m_terrainEdit.makeRaiseLower(brush,40);check(v.m_terrainEdit.apply(command,true,&report),"change trial terrain");v.m_pLand->setDirty(true);};
 const auto editMob=[&](CMob* mob){mob->setScript(mob->script()+QString("\r\n// scope280-%1\r\n").arg(++editSerial));mob->setDirty(true);};
 editMap();editMob(first);editMob(second);
 // Same shared catalog, two independent MOB group records; only selected records may be adopted.
 auto db=QSharedPointer<ei::SProfileDatabaseSnapshot>::create();db->sourceArchivePath=root+"/database.res";db->profileId="deja-vu";ei::ProfileDatabaseContext::instance().activate(db);
 first->nodes().first()->setAssemblyTag({{"instanceId",QUuid::createUuid().toString()},{"name","first group"}});second->nodes().first()->setAssemblyTag({{"instanceId",QUuid::createUuid().toString()},{"name","second group"}});
 if(crashOnly){write(root+"/scope-before/zone1.mpr",mapBefore);write(root+"/scope-before/zone1.mob",mobBefore);write(root+"/scope-before/second.mob",mobBefore);qputenv("EI_TEST_TERMINATE","0");window.on_actionSave_all_MOB_s_triggered();throw QString("TerminateProcess hook did not run");}
 QElapsedTimer timer;timer.start();window.on_actionSave_triggered();const auto singleMs=timer.elapsed();
 out<<"METRIC singleSaveMs="<<singleMs<<" version="<<QCoreApplication::applicationVersion()<<'\n';out.flush();
 check(read(firstPath)!=mobBefore&&!first->isDirty(),"Save writes and cleans active MOB");check(read(secondPath)==mobBefore&&second->isDirty()&&read(mapPath)==mapBefore&&v.m_pLand->isDirty(),"Save preserves trial MPR and second MOB bytes and dirty flags");
 ei::TechnicalModelCatalog catalog;QString error;check(catalog.load(root+"/model-catalog.json",&error)&&!catalog.assemblyInstances(firstPath).isEmpty()&&catalog.assemblyInstances(secondPath).isEmpty(),"Save publishes only active MOB group record");
 editMob(first);window.on_actionSave_all_MOB_s_triggered();check(read(firstPath)!=mobBefore&&read(secondPath)!=mobBefore&&!first->isDirty()&&!second->isDirty(),"Save all MOB writes both MOBs");check(read(mapPath)==mapBefore&&v.m_pLand->isDirty(),"Save all preserves trial MPR bytes and dirty state");
 check(catalog.load(root+"/model-catalog.json",&error)&&!catalog.assemblyInstances(firstPath).isEmpty()&&!catalog.assemblyInstances(secondPath).isEmpty(),"Save all commits both group records");
 editMob(first);editMob(second);const auto firstDisk=read(firstPath),secondDisk=read(secondPath);window.on_actionSave_landscape_MPR_triggered();
 check(read(mapPath)!=mapBefore&&!v.m_pLand->isDirty(),"Save MPR publishes terrain");check(read(firstPath)==firstDisk&&read(secondPath)==secondDisk&&first->isDirty()&&second->isDirty(),"Save MPR preserves both dirty MOB files");
 const QFileInfo mapAfterSave(mapPath);const auto currentMap=read(mapPath);const auto catalogBefore=read(root+"/model-catalog.json");
 window.on_actionSave_landscape_MPR_triggered();check(read(mapPath)==currentMap&&QFileInfo(mapPath).lastModified()==mapAfterSave.lastModified()&&read(root+"/model-catalog.json")==catalogBefore,"clean Save MPR adds no file write or group update");
 check(resFileSha256(resPath)==resBefore,"human command matrix preserves unchanged RES");
#if !defined(EI_BASELINE_279)
 editMap();
  window.findChild<QCheckBox*>("aiAgentAllowSave")->setChecked(true);window.findChild<QComboBox*>("aiAgentMode")->setCurrentIndex(2);
  check(window.m_aiAgent->enabled(),"isolated production agent server enabled");
  QProcess bridge;bridge.start(qEnvironmentVariable("EI_SAVE_BRIDGE"),QStringList{});check(bridge.waitForStarted(),"own stdio MCP bridge starts");QByteArray buffer;int serial=0;
  const auto call=[&](const QString& method,const QJsonObject& params){const auto id=++serial;bridge.write(QJsonDocument(QJsonObject{{"jsonrpc","2.0"},{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n');while(true){check(until([&](){buffer+=bridge.readAllStandardOutput();return buffer.contains('\n');}),"MCP reply received");QJsonObject result;QString error;if(!ai::takeFrame(buffer,&result,&error))throw error;if(result.contains("id"))return result;}};
  call("initialize",{{"protocolVersion","2025-11-25"},{"clientInfo",QJsonObject{{"name","save-integrity-280"},{"version","1"}}},{"capabilities",QJsonObject{}}});bridge.write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
  const auto tool=[&](const QString& name,const QJsonObject& args){const auto envelope=call("tools/call",{{"name",name},{"arguments",args}});const auto content=envelope["result"].toObject()["content"].toArray();if(content.isEmpty())throw QString::fromUtf8(QJsonDocument(envelope).toJson());return QJsonDocument::fromJson(content[0].toObject()["text"].toString().toUtf8()).object();};
  const auto selected=tool("connect_instance",{{"target","editor"}});check(!selected.contains("error"),"connect only own isolated endpoint");
  const auto ctx=tool("get_context",{});check(ctx["mpr"].toString()==mapPath,"MCP context identifies owned map");
  const auto revision=tool("get_revision",{});const auto started=tool("save",{{"revision",revision["revision"]}});check(started.contains("operation"),"MCP normal Save started");QJsonObject done;
  check(until([&](){done=tool("operation_status",{{"operation",started["operation"]}});return done["status"]=="complete";},120000),"MCP Save completed");check(done["result"].toObject()["status"]=="saved","MCP Save result saved");
  bridge.closeWriteChannel();check(bridge.waitForFinished(4000),"own MCP bridge exits");
 check(!v.m_pLand->isDirty()&&!first->isDirty()&&!second->isDirty()&&read(mapPath)!=currentMap&&read(firstPath)!=firstDisk&&read(secondPath)!=secondDisk,"MCP Save writes full MPR and two-MOB family");
 editMap();editMob(first);editMob(second);const auto failureMap=read(mapPath),failureFirst=read(firstPath),failureSecond=read(secondPath),failureCatalog=read(root+"/model-catalog.json");const int priorDialogs=dialogTexts.size();
 qputenv("EI_TEST_FAIL","1");window.on_actionSave_all_MOB_s_triggered();qunsetenv("EI_TEST_FAIL");
 check(read(firstPath)==failureFirst&&read(secondPath)==failureSecond&&read(mapPath)==failureMap&&read(root+"/model-catalog.json")==failureCatalog,"second MOB failure rolls back entire MOB/catalog set and never writes MPR");check(first->isDirty()&&second->isDirty()&&v.m_pLand->isDirty(),"failed Save all retains all dirty flags");check(dialogTexts.size()>priorDialogs&&dialogTexts.join('\n').contains(secondPath),"batch error retains failing MOB path");
 window.on_actionSave_all_MOB_s_triggered();check(!first->isDirty()&&!second->isDirty()&&v.m_pLand->isDirty()&&read(mapPath)==failureMap,"Save all retry saves only MOB subset");

 // The historical travel command publishes its own RES, dirty landscape and active MOB only.
 editMob(first);editMob(second);const auto travelFirst=read(firstPath),travelSecond=read(secondPath),travelMap=read(mapPath);
 CResFile travelArchive;travelArchive.addFiledata("map.txt","#zone zone1 island game\n#res\nzone1 zone1\n#exit 1\nnone 0\n#deploy\n1 2 3 4\n");travelArchive.addFiledata("retained.bin","preserve");const auto travelPath=root+"/texts.res";write(travelPath,travelArchive.generateResData());
 auto travel=QSharedPointer<CTravelDocument>::create();check(travel->open(travelPath,&error),"owned travel document opened");auto travelEdit=travel->snapshot();travelEdit.replace("1 2 3 4","2 3 4 5");check(travel->restore(travelEdit,&error)&&travel->dirty(),"travel edit prepared");
 window.m_travelDocuments.insert(travelPath,travel);window.m_travelDocument=travel;window.m_travelZone=0;window.m_travelExit=0;window.refreshTravelEditor();
 window.on_actionSave_triggered();check(!travel->dirty()&&read(mapPath)!=travelMap&&read(firstPath)!=travelFirst&&!first->isDirty()&&read(secondPath)==travelSecond&&second->isDirty(),"Save with travel retains 279 active-MOB-only companion selection");
 editMap();editMob(first);const auto travelAgentFirst=read(firstPath),travelAgentSecond=read(secondPath);travelEdit=travel->snapshot();travelEdit.replace("2 3 4 5","3 4 5 6");check(travel->restore(travelEdit,&error),"second travel edit prepared");window.refreshTravelEditor();
 check(window.saveMapWithTravel(QString(),true)&&!travel->dirty()&&!first->isDirty()&&!second->isDirty()&&read(firstPath)!=travelAgentFirst&&read(secondPath)!=travelAgentSecond,"explicit agent travel package includes all dirty MOBs");
 window.m_travelDocuments.clear();window.m_travelDocument.clear();window.refreshTravelEditor();editMap();

 // Validation failure remains visible with the same explanatory text and parser findings.
 editMob(first);const auto raw=first->m_originalData;first->m_originalData="invalid raw MOB";const int validationStart=dialogTexts.size();window.on_actionSave_triggered();first->m_originalData=raw;
 check(dialogTexts.size()>validationStart&&dialogTexts.last().contains("Endurance could not map this complete edit set")&&dialogTexts.last().contains("ERROR"),"human MOB validation text and parser report remain available");
 // Recovery autosave writes only private generations; it must not publish any source or clear dirty state.
 editMob(second);const auto autoMap=read(mapPath),autoFirst=read(firstPath),autoSecond=read(secondPath);window.performAutosave();check(until([&](){return window.m_mprAutosaveWatcher==nullptr;},120000),"autosave background generation completed");
 check(read(mapPath)==autoMap&&read(firstPath)==autoFirst&&read(secondPath)==autoSecond&&v.m_pLand->isDirty()&&first->isDirty()&&second->isDirty(),"autosave leaves original files and dirty state unchanged");
 // A close prompt chooses the same single MOB writer; the trial landscape stays unsaved.
 check(v.unloadMobDocument(second,true),"MOB close Save accepted");second=nullptr;check(read(mapPath)==autoMap&&v.m_pLand->isDirty()&&first->isDirty()&&read(firstPath)==autoFirst&&read(secondPath)!=autoSecond,"close prompt saves only closing MOB");
 // Save As remains a single-MOB command; the original source and trial terrain stay unchanged.
 const auto copyPath=root+"/Maps/scope-copy.mob";QTimer fileDialog;QObject::connect(&fileDialog,&QTimer::timeout,[&](){for(auto* widget:QApplication::topLevelWidgets())if(auto* dialog=qobject_cast<QFileDialog*>(widget)){dialog->selectFile(copyPath);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});fileDialog.start(10);
 v.saveMobAs();fileDialog.stop();check(QFileInfo::exists(copyPath)&&read(firstPath)==autoFirst&&read(mapPath)==autoMap&&v.m_pLand->isDirty()&&first->filePath().absoluteFilePath()==copyPath&&!first->isDirty(),"Save As writes only chosen MOB destination and its group record");

#endif
 CMprDocument reopenedMap;CMobDocument reopenedFirst,reopenedSecond;check(reopenedMap.load(QFileInfo(mapPath),&report)&&reopenedFirst.loadBytes(read(firstPath),firstPath,&report)&&reopenedSecond.loadBytes(read(secondPath),secondPath,&report)&&!report.hasErrors(),"source subset files reopen after matrix");
 check(resFileSha256(resPath)==resBefore,"RES byte hash unchanged after scope matrix");
 window.m_aiAgent->stop();v.m_worldTerrainSession.clear();v.m_pLand->setDirty(false);v.unloadLand(false);for(auto* mob:v.mobs())mob->setDirty(false);
}

}
void observedChange(){
 qApp->setProperty("agentHeadless",true);
 const auto path=root+"/observed.res";const auto before=archive("old_aaaaaaaaaaaa","before"),after=archive("new_bbbbbbbbbbbb","after");write(path,before);
 CResourceBrowser browser;browser.setAttribute(Qt::WA_DontShowOnScreen);browser.setActiveProfileSources({path},"owned observer");
 check(until([&](){return knownResState(path).sha==digest(before);}),"browser establishes before SHA in worker");
 write(path,after);
 check(until([&](){for(const auto& item:CLogger::getInstance()->sessionMessages())if(item.message.startsWith("Resource Browser observed external archive change ")){
  const auto row=QJsonDocument::fromJson(item.message.mid(QString("Resource Browser observed external archive change ").size()).toUtf8()).object();
  if(row["path"]==path)return row["before"].toObject()["sha256"].toString().toLatin1()==digest(before)&&row["after"].toObject()["sha256"].toString().toLatin1()==digest(after)
   &&row["before"].toObject()["bytes"].toInt()==before.size()&&row["after"].toObject()["bytes"].toInt()==after.size();}return false;}),"external-change workflow log includes exact before/after SHA size and mtime");
 browser.m_loadWatcher->waitForFinished();
}
int mapSaveFailureForTest(){bool ok=false;int i=qEnvironmentVariable("EI_TEST_FAIL").toInt(&ok);return ok?i:-1;}
void mapSaveReplacedForTest(int i){bool ok=false;const auto stop=qEnvironmentVariable("EI_TEST_TERMINATE").toInt(&ok);if(ok&&stop==i){write(root+"/replaced.marker",QByteArray::number(i));if(qEnvironmentVariableIsSet("EI_TEST_HOLD"))Sleep(INFINITE);TerminateProcess(GetCurrentProcess(),77);}}
// Process-only test shim: Qt on Windows ignores LOCALAPPDATA for known
// folders. Redirect writable QStandardPaths in this owned process before Qt
// constructs any application/widget. No DLL on disk is changed.
QString isolatedWritableLocation(QStandardPaths::StandardLocation location){
    return QDir::fromNativeSeparators(qEnvironmentVariable("EI_SAVE_CASE"))+"/known-folders/"+QString::number(int(location));
}
void isolateKnownFolders(){
    auto module=GetModuleHandleW(L"Qt5Core.dll");
    auto entry=GetProcAddress(module,"_ZN14QStandardPaths16writableLocationENS_16StandardLocationE");
    if(!entry)throw QString("Cannot isolate Qt writable locations");
    unsigned char jump[12]={0x48,0xb8};const auto address=reinterpret_cast<quint64>(&isolatedWritableLocation);std::memcpy(jump+2,&address,8);jump[10]=0xff;jump[11]=0xe0;
    DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(entry),12,PAGE_EXECUTE_READWRITE,&old))throw QString("Cannot isolate Qt known folders");
    std::memcpy(reinterpret_cast<void*>(entry),jump,12);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),12);DWORD ignored;VirtualProtect(reinterpret_cast<void*>(entry),12,old,&ignored);
}
extern int qMain(int,char**);
int main(int argc,char** argv){SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);isolateKnownFolders();
 if(argc>1&&std::strcmp(argv[1],"startup")==0){argv[1]=const_cast<char*>("--agent-headless");return qMain(argc,argv);}
QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);QApplication app(argc,argv);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);Hidden hiddenAll;app.installEventFilter(&hiddenAll);QCoreApplication::setOrganizationName("Ataman isolated save280");QCoreApplication::setApplicationName("save-integrity-check");QCoreApplication::setApplicationVersion(ENDURANCE_VERSION);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,qEnvironmentVariable("EI_MAPPER_SETTINGS_DIR"));root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_SAVE_CASE"));out.setCodec("UTF-8");try{
 const auto mode=app.arguments().value(1);check(!root.isEmpty()&&QDir::isAbsolutePath(root),"explicit owned case");
 if(mode=="nested")nested();
 else if(mode=="observed")observedChange();
 else if(mode=="scope")commandScope(false);
 else if(mode=="scope-crash")commandScope(true);
 else if(mode=="scope-reopen"){
  CValidationReport report;CMprDocument map;CMobDocument first,second;
  check(map.load(QFileInfo(root+"/Maps/zone1.mpr"),&report)&&map.serialize()==read(root+"/Maps/zone1.mpr"),"strict recovered MPR reopen");
  check(first.loadBytes(read(root+"/Maps/zone1.mob"),"zone1.mob",&report)&&first.serialize()==read(root+"/Maps/zone1.mob"),"strict recovered first MOB reopen");
  check(second.loadBytes(read(root+"/Maps/second.mob"),"second.mob",&report)&&second.serialize()==read(root+"/Maps/second.mob")&&!report.hasErrors(),"strict recovered second MOB reopen");
 }
 else if(mode=="prepare")packet(true);
 else if(mode=="commit"||mode=="locked"){const auto files=packet(false);HANDLE held=INVALID_HANDLE_VALUE;
 if(mode=="locked"){const auto path=QDir::toNativeSeparators(files[qEnvironmentVariable("EI_TEST_LOCK_INDEX").toInt()].path);held=CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"real OS replacement denial established");}
 const auto result=commitMapSaveBatch(files,root+"/backups");if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);out<<"RESULT "<<QJsonDocument(QJsonObject{{"success",result.success},{"rollbackComplete",result.rollbackComplete},{"error",result.error}}).toJson(QJsonDocument::Compact)<<'\n';out.flush();return result.success?0:2;}
#if !defined(EI_BASELINE_279)
 else if(mode=="recover"){int questions=0;const auto warnings=recoverMapSaveBatches(root+"/backups",qEnvironmentVariableIsSet("EI_TEST_HEADLESS"),[&](const QString& question){++questions;out<<"QUESTION "<<question<<'\n';return !qEnvironmentVariableIsSet("EI_TEST_DECLINE");});write(root+"/warnings.json",QJsonDocument(QJsonObject{{"warnings",QJsonArray::fromStringList(warnings)},{"questions",questions}}).toJson());}
#endif
 else if(mode=="ui")uiSave(false);else if(mode=="mcp")uiSave(true);else throw QString("unknown mode");
 out<<"PASS TOTAL "<<passes<<'\n';return 0;
 }catch(const QString& e){out<<"FAIL "<<e<<'\n';return 1;}catch(const std::exception& e){out<<"FAIL "<<e.what()<<'\n';return 1;}}
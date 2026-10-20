#include <QtWidgets>
#include <QtConcurrent>
#include <QtOpenGL>
#include <memory>
#define private public
#define protected public
#include "main_window.h"
#include "view.h"
#undef private
#undef protected
#include "settings.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "resourcemanager.h"
#include "res_archive_index.h"
#include "backup_manager.h"
#include "log.h"
#include "log_rotation.h"
#include "file_write_activity.h"
namespace {
QTextStream out(stdout);QString root;int checks=0;
void check(bool value,const QString& message){if(!value)throw message;++checks;out<<"PASS "<<message<<"\n";out.flush();}
void write(const QString& path,const QByteArray& data){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"write fixture "+QFileInfo(path).fileName());}
void age(const QString& path,int days){QFile f(path);check(f.open(QIODevice::ReadWrite)&&f.setFileTime(QDateTime::currentDateTimeUtc().addDays(-days),QFileDevice::FileModificationTime),"age copy");}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw path;return f.readAll();}
void wait(const std::function<bool()>& done,int ms){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms){qApp->processEvents();QThread::msleep(5);}check(done(),"bounded background completion");}
void storage(){
 if(!qEnvironmentVariableIsSet("EI_HOUSEKEEPING_LOG_ONLY")) {
 const auto folder=root+"/retention";QDir().mkpath(folder);const auto source=folder+"/source.bin";
 CBackupManager manager(folder+"/backups");QStringList copies;
 for(int i=0;i<7;++i){write(source,QByteArray::number(i));auto b=manager.snapshotBeforeSave(QFileInfo(source));check(b.success,"create generation");copies<<b.backupPath;age(b.backupPath,10-i);}
 check(manager.collectBackups(true)>0,"retention dry run");manager.collectBackups();int remains=0;for(const auto& p:copies)remains+=QFileInfo::exists(p);check(remains==3,"last three retained per source");
 write(source,"young");auto young=manager.snapshotBeforeSave(QFileInfo(source));check(young.success,"young backup");young.lease.reset();manager.collectBackups();remains=0;for(const auto& p:copies)remains+=QFileInfo::exists(p);check(remains==2&&QFileInfo::exists(young.backupPath),"young backup counts among latest three");
 // All young generations exceed N but cannot be pruned.
 for(int i=0;i<5;++i){write(source,QByteArray("young")+QByteArray::number(i));auto b=manager.snapshotBeforeSave(QFileInfo(source));check(b.success,"create young generation");}
 manager.collectBackups();check(QDir(QFileInfo(young.backupPath).absolutePath()).entryList({"*.backup"},QDir::Files).size()==6,"all backups younger than three days retained");
 // An old pinned operation must survive even below N.
 auto pin=manager.snapshotBeforeSave(QFileInfo(source));age(pin.backupPath,20);manager.collectBackups();check(QFileInfo::exists(pin.backupPath),"current operation protected below retention");pin.lease.reset();manager.collectBackups();check(!QFileInfo::exists(pin.backupPath),"completed old operation becomes collectible");
 QSettings().setValue("backups/generations",0);CBackupManager minimum(folder+"/minimum");QString last;
 for(int i=0;i<3;++i){write(source,QByteArray::number(i));auto b=minimum.snapshotBeforeSave(QFileInfo(source));check(b.success,"minimum fixture");last=b.backupPath;age(last,10-i);}minimum.collectBackups();check(QDir(QFileInfo(last).absolutePath()).entryList({"*.backup"},QDir::Files).size()==1,"configured retention clamped to minimum one");QSettings().setValue("backups/generations",3);
 const auto large=root+"/fixture/MODS/DejaVu/res/HDPack_Terrain.res";check(QFileInfo(large).size()>200LL*1024*1024,"real large RES copied");
 CBackupManager big(folder+"/large-backups");auto first=big.snapshotBeforeSave(QFileInfo(large));check(first.success&&!first.skipped,"first large backup copied");auto again=big.snapshotBeforeSave(QFileInfo(large));check(again.success&&again.skipped&&again.backupPath==first.backupPath,"unchanged large RES reuses exact SHA backup even with active lease");
 const auto journal=folder+"/backups/map-save-recovery/pending.json";auto protectedCopy=manager.snapshotBeforeSave(QFileInfo(source));age(protectedCopy.backupPath,30);protectedCopy.lease.reset();
 write(journal,QJsonDocument(QJsonObject{{"files",QJsonArray{QJsonObject{{"backup",protectedCopy.backupPath}}}}}).toJson());manager.collectBackups();check(QFileInfo::exists(protectedCopy.backupPath),"210 save recovery manifest protects old backup");
 const auto copyRoot=root+"/backup-copy";CBackupManager copied(copyRoot);const auto expected=copied.collectBackups(true);const auto freed=copied.collectBackups();check(freed==expected,"owner inventory copy dry run equals actual sparse-copy cleanup");check(copied.collectBackups()==0,"retention copy cleanup idempotent");out<<"OWNER_BACKUPS freedBytes="<<freed<<"\n";
 }
 const auto logs=QDir::currentPath()+"/rotation";QDir().mkpath(logs);const auto path=logs+"/render-performance.log";
 for(int i=0;i<9;++i){check(ei::rotateLog(path,1024*1024),"rotate log");QFile f(path);check(f.open(QIODevice::WriteOnly|QIODevice::Append)&&f.write(QByteArray(1024*1024,'x'))==1024*1024,"log append");}
 // production limit: push beyond 10 MiB enough times to cover deletion of oldest
 for(int i=0;i<60;++i){check(ei::rotateLog(path,1024*1024),"production log rollover");QFile f(path);check(f.open(QIODevice::WriteOnly|QIODevice::Append)&&f.write(QByteArray(1024*1024,'x'))==1024*1024,"append bounded log");}
 const auto oversized=logs+"/legacy.log";write(oversized,QByteArray(12*1024*1024,'x')+"\nlast full line\n");check(ei::rotateLog(oversized,100),"oversized legacy log bounded");check(QFileInfo(oversized).size()<=10LL*1024*1024 && read(oversized).endsWith("last full line\n"),"legacy tail retained within limit");
 auto files=QDir(logs).entryInfoList({"render-performance.log*"},QDir::Files);check(files.size()==5,"five log files total");for(const auto& f:files)check(f.size()<=10LL*1024*1024,"each log at most 10 MiB");
 for(int i=0;i<36;++i)write(logs+QString("/workflow-%1.log").arg(i,3,10,QLatin1Char('0')),"historic");ei::pruneWorkflowLogs(QDir(logs));check(QDir(logs).entryList({"workflow-*.log"},QDir::Files).size()==30,"thirty workflow sessions retained");
 // Actual workflow writer uses same limit and maintains UTF-8 after rollover.
 for(int i=0;i<7;++i)ei::log(eLogInfo,QString(2*1024*1024,QLatin1Char('x')));ei::log(eLogInfo,QString::fromUtf8("Журнал после ротации"));
 check(QFileInfo(CLogger::getInstance()->logFilePath()).size()<=10LL*1024*1024,"production workflow writer bounded");check(read(CLogger::getInstance()->logFilePath()).contains(QString::fromUtf8("Журнал после ротации").toUtf8()),"UTF-8 survives workflow rollover");
}
struct Hidden:QObject{bool eventFilter(QObject* o,QEvent* e)override{if(e->type()==QEvent::Polish)if(auto* w=qobject_cast<QWidget*>(o))w->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
void scene(const QString& mode){
 qApp->setAttribute(Qt::AA_DontUseNativeDialogs);
 Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
 MainWindow window;window.configureAutosaveTimer(false,5);window.resize(900,700);window.show();qApp->processEvents();auto& view=*window.view();view.makeCurrent();check(view.isValid(),"hidden Release MainWindow GL valid");
 const auto map=root+"/fixture/MODS/DejaVu/Maps/zone7.mpr";check(window.loadLandscapeWithProgress(QFileInfo(map)),"open real zone7 copy");view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"palette available");check(view.m_workingTerrainAtlas&&view.m_workingTerrainAtlas->isValid(),"palette not empty");
 QTimer saveDialogs;int unexpectedSaveDialogs=0;
 QObject::connect(&saveDialogs,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)) {
    out<<"SAVE_DIALOG "<<box->windowTitle()<<" "<<box->text()<<"\n";out.flush();
    if(box->windowTitle()=="RES archive changed" && box->text().contains(root+"/fixture/") && box->button(QMessageBox::No))box->button(QMessageBox::No)->click();
    else if(box->windowTitle()==QString::fromUtf8("??????? ?????")&&box->button(QMessageBox::Yes))box->button(QMessageBox::Yes)->click();
    else {++unexpectedSaveDialogs;box->reject();}
 }});saveDialogs.start(10);
 if(mode=="owner"){
  auto& panel=*window.m_landscapeTools;panel.activateMode(ELandscapeEditMode::TilePaint,true);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
  bool selected=false;for(const auto& f:view.m_terrainAtlasProfile.families)if(f.landAllowed&&f.paintSelectable&&panel.selectPaintTerrainType(f.id)){selected=true;break;}check(selected,"palette material selected");
  if(auto* b=panel.findChild<QToolButton*>("vertexPaintTarget"))b->click();if(auto* b=panel.findChild<QCheckBox*>("modernAutoResolveBlends"))b->setChecked(true);if(auto* b=panel.findChild<QSlider*>("terrainPaintRadius"))b->setValue(1);
  view.m_worldTerrainEnabled=false;QVector3D point(20,20,0);view.m_pLand->projectPt(point,false);view.updatePreviewTile(point,true);view.beginTileBrushGroup();view.setTile(point,true);view.endTileBrushGroup();wait([&](){return !view.m_tileStrokeFinishing;},120000);check(view.m_terrainEdit.isDirty(),"real brush stroke changes terrain");
  QVector<qint32> before,scratch,actual;view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&before,&scratch);check(view.saveChangedLandscapeDocument(QFileInfo(map),true,true),"save real edited copy");view.unloadLand(false);view.makeCurrent();view.loadLandscape(QFileInfo(map),false);check(view.ensureTerrainAutoCatalogReady(),"reopen palette");view.m_terrainEdit.copyVirtualMaterialFieldState(ETerrainTileLayer::Land,&actual,&scratch);check(before==actual,"reopen preserves brush material field");
 }
 saveDialogs.stop();check(unexpectedSaveDialogs==0,"no unexpected Save dialogs");
 // Two true background scene workers and accepted neighbors.
 view.clearNeighborLandscapes();view.m_neighborPlacements.clear();view.m_neighborLandscapesVisible=true;
 for(const QString& name:{QString("zone6"),QString("zone8")}){SNeighborMapPlacement p;p.zoneId=name;p.mprPath=root+"/fixture/MODS/DejaVu/Maps/"+name+".mpr";p.mobPath=root+"/fixture/MODS/DejaVu/Maps/"+name+".mob";p.origin={double(view.m_pLand->widthInWorldUnits()),0};CMprDocument neighbor;CValidationReport report;check(neighbor.load(QFileInfo(p.mprPath),&report),"read neighbor dimensions");p.size={double(neighbor.mapInfo().sectorCountX*32),double(neighbor.mapInfo().sectorCountY*32)};view.m_neighborPlacements.append(p);}view.loadNeighborLandscapes();
 wait([&](){return view.m_neighborLandscapes.size()==2;},90000);check(view.m_neighborLandscapes.size()==2,"real neighbors accepted");
 window.hide();qApp->processEvents();check(!view.m_timer->isActive(),"hidden window stops render timer");window.show();qApp->processEvents();check(view.m_timer->isActive(),"show resumes render timer");
 QTimer dialogs;int prompts=0;bool cancelFirst=(mode=="close");QObject::connect(&dialogs,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){
  out<<"DIALOG "<<box->text()<<"\n";out.flush();++prompts;
  if(cancelFirst&&box->button(QMessageBox::Cancel)){cancelFirst=false;box->button(QMessageBox::Cancel)->click();}
  else if(box->button(QMessageBox::No))box->button(QMessageBox::No)->click();else box->reject();}});dialogs.start(10);
 if(mode=="close"||mode=="close-save"){
  STerrainBrush brush;brush.centerX=20;brush.centerY=20;brush.radius=1;auto command=view.m_terrainEdit.makeRaiseLower(brush,1);CValidationReport report;check(!command.isEmpty()&&view.m_terrainEdit.apply(command,true,&report),"dirty terrain for close preflight");view.m_pLand->setDirty(true);
  if(mode=="close") {check(!window.close(),"dirty Cancel rejects close");check(window.view()&&!window.shutdownPending(),"Cancel retains live viewport");}
  else {check(!window.m_mprAutosaveWatcher,"background autosave ready to start");}
 }
 if(mode=="close")CLogger::getInstance()->setLogLevel(eLogFatal);
 QElapsedTimer elapsed;elapsed.start();QTimer watchdog;watchdog.setSingleShot(true);QObject::connect(&watchdog,&QTimer::timeout,[](){out<<"FAIL close deadline 30000ms\n";out.flush();std::_Exit(3);});watchdog.start(30000);
 // Disable implicit Qt quit and retain a floating window: successful process
 // exit must come from the primary-window lifecycle, independent of tool flags.
 qApp->setQuitOnLastWindowClosed(false);QDialog floating;floating.setAttribute(Qt::WA_DontShowOnScreen);floating.show();
 QTimer::singleShot(0,&window,[&](){if(mode=="close-save"){window.performAutosave();check(window.m_mprAutosaveWatcher&&window.m_mprAutosaveWatcher->isRunning(),"real background recovery write running at Close");}window.close();});qApp->exec();
 check(!window.view(),"viewport destroyed before application exits");check(elapsed.elapsed()<30000,"main close completes within 30 seconds");check(prompts>0||mode=="owner","dirty close prompt retained");
 const auto log=read(CLogger::getInstance()->logFilePath());check(log.contains("Shutdown started")&&log.contains("Shutdown complete status:clean"),"shutdown start/end journal");
 if(mode=="close-save") {check(log.contains("Shutdown waiting for active save")&&log.contains("Shutdown active write finished"),"Close waited for background save");QDirIterator it(CBackupManager::defaultRootPath(),{"*-autosave-*.json"},QDir::Files,QDirIterator::Subdirectories);check(it.hasNext(),"background recovery committed before close");check(CBackupManager::validateAutosave(QFileInfo(it.next().chopped(5))),"background recovery exact after close");}
 out<<"CLOSE elapsedMs="<<elapsed.elapsed()<<" mode="<<mode<<"\n";qApp->removeEventFilter(&hidden);
}
}
int runAssemblyCatalogCheck(){try{out.setCodec("UTF-8");root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_HOUSEKEEPING_ROOT"));check(QFileInfo(root).fileName()=="build-tests-housekeeping-211","explicit isolated root");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QDir::currentPath()+"/settings");const auto mode=qEnvironmentVariable("EI_HOUSEKEEPING_MODE");if(mode=="storage"||mode=="logs")storage();else scene(mode);out<<"PASS HOUSEKEEPING "<<mode<<" checks="<<checks<<" nativeAttempts=0\n";out.flush();return 0;}catch(const QString& e){out<<"FAIL HOUSEKEEPING "<<e<<"\n";out.flush();return 1;}}

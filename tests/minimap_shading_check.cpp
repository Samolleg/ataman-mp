#include <QtWidgets>
#include <QtTest>
#include <QOpenGLFunctions>
#include <windows.h>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "settings.h"
#include "minimap_export_dialog.h"
#include "resource_setup_dialog.h"
#include "mpr_document.h"
namespace {
struct Hidden:QObject {bool eventFilter(QObject* o,QEvent* e)override{if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
int visible=0;BOOL CALLBACK inspect(HWND w,LPARAM){DWORD pid;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++visible;return TRUE;}
bool until(const std::function<bool()>& done){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<120000)QTest::qWait(10);return done();}
QJsonObject stats(const QImage& input){auto im=input.convertToFormat(QImage::Format_ARGB32);double r=0,g=0,b=0;int red=0,n=0;for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x){auto c=im.pixel(x,y);if(!qRed(c)&&!qGreen(c)&&!qBlue(c))continue;++n;r+=qRed(c);g+=qGreen(c);b+=qBlue(c);if(qRed(c)>qGreen(c)+30&&qRed(c)>qBlue(c)+30)++red;}return {{"format",int(input.format())},{"width",im.width()},{"height",im.height()},{"r",r/qMax(n,1)},{"g",g/qMax(n,1)},{"b",b/qMax(n,1)},{"redFraction",double(red)/qMax(n,1)}};}
}
int runAssemblyCatalogCheck(){QTextStream out(stdout);int passes=0;auto check=[&](bool ok,QString what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
try{const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT")),maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));check(!root.isEmpty()&&!qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"),"explicit isolated profile and lock store");
QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);Hidden hidden;qApp->installEventFilter(&hidden);QTimer visibility;QObject::connect(&visibility,&QTimer::timeout,[](){EnumWindows(inspect,0);});visibility.start(10);qApp->setProperty("workspaceLoadSmoke",true);
QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_currentCollectionPath=root+"/private.collection";window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.resize(1200,800);window.setAttribute(Qt::WA_DontShowOnScreen);window.show();auto& view=*window.view();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"production hidden desktop GL");view.m_neighborLandscapesVisible=false;
// A fresh one-sector hill uses the authorized DejaVu atlas only.
CMprDocument doc;CValidationReport report;check(doc.load(QFileInfo(maps+"/zone1.mpr"),&report),"DejaVu terrain template");SMprCreationRequest req;req.innerNamespace=doc.mapInfo().innerNamespace;req.heightmapWidth=33;req.heightmapHeight=33;req.heightmapMaximumRaw=65535;req.initialPackedTile=0;for(int y=0;y<33;++y)for(int x=0;x<33;++x)req.heightmapSamples.append(5000+18000*qExp(-((x-16)*(x-16)+(y-16)*(y-16))/100.0));QByteArray bytes;check(doc.buildNewCanvas(req,&bytes,&report),"synthetic one-sector hill from DejaVu");QFile simple(maps+"/simple274.mpr");check(simple.open(QIODevice::WriteOnly)&&simple.write(bytes)==bytes.size(),"private simple map created");simple.close();
QJsonObject evidence;
for(const auto name:{QString("zone1"),QString("basecam"),QString("simple274"),QString("zone1-dawn")}){
 if(qEnvironmentVariableIsSet("EI_MINIMAP_ONLY_SIMPLE")&&name!="simple274")continue;
 if(!qEnvironmentVariable("EI_MINIMAP_SCENARIO").isEmpty()&&qEnvironmentVariable("EI_MINIMAP_SCENARIO")!=name)continue;
 if(name=="simple274")check(window.replaceLoadedLandscape(QFileInfo(maps+"/"+name+".mpr"),false),"open simple terrain");else {const auto map=name=="zone1-dawn"?QString("zone1"):name;check(window.loadLandscapeWithProgress(QFileInfo(maps+"/"+map+".mpr")),"fixture MPR open");
view.m_timer->stop();view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"fixture terrain ready before MOB");
check(window.loadMobWithProgress(QFileInfo(maps+"/"+map+".mob"),false),"fixture MOB open");}
 check(until([&](){return !window.backgroundMobLoading();}),"complete scene "+name);view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"native atlas ready "+name);window.m_minimapRefreshTimer->stop();view.paintGL();
 if(name=="zone1-dawn"){check(view.m_environmentMob!=nullptr,"authored environment available");auto ws=view.m_environmentMob->worldSet();ws.setData(eWsTypeTime,"4");view.m_environmentMob->setWorldSet(ws);}
 auto raw=view.captureMinimapScene(name!="simple274",1024,false);check(!raw.capturedReviewImage.isNull()&&!raw.capturedUnlitReviewImage.isNull(),"both GL readbacks "+name);check(raw.capturedReviewImage.size()==raw.capturedUnlitReviewImage.size(),"same readback dimensions "+name);check(raw.capturedReviewImage.save(root+"/"+name+"-shaded-review.png")&&raw.capturedUnlitReviewImage.save(root+"/"+name+"-unlit-review.png"),"intermediate PNGs "+name);
 auto capture=CView::finishMinimapSceneCapture(raw);check(capture.bake.success,"production bake "+name);capture.bake.gameImage.save(root+"/"+name+"-shaded.png");capture.unlitGameImage.save(root+"/"+name+"-unlit.png");QJsonObject images{{"shaded",stats(capture.bake.gameImage)},{"unlit",stats(capture.unlitGameImage)}};
 CMinimapExportDialog dialog(capture,{},"","mini274",name,SMinimapGradeSettings(100,100,100,100,0),false,&window);dialog.setAttribute(Qt::WA_DontShowOnScreen);dialog.setAttribute(Qt::WA_DeleteOnClose,false);auto* slider=dialog.findChild<QSlider*>("minimapShadingSlider");check(slider&&slider->minimum()==0&&slider->maximum()==150,"production shading slider "+name);
 for(int shade:{0,50,100,128,150}){slider->setValue(shade);const auto image=dialog.gradedImage();check(!image.isNull()&&image.save(root+"/"+name+"-"+QString::number(shade)+".png"),"dialog export "+name+" "+QString::number(shade));images[QString::number(shade)]=stats(image);check(dialog.gradeSettings().hueDegrees==0&&dialog.gradeSettings().saturationPercent==100&&dialog.gradeSettings().brightnessPercent==100&&dialog.gradeSettings().contrastPercent==100,"shading leaves other sliders unchanged");if(shade==100){check(image==capture.bake.gameImage.convertToFormat(QImage::Format_ARGB32),"100% equals original shaded "+name);const auto before=qEnvironmentVariable("EI_MINIMAP_BEFORE");if(!before.isEmpty()){
    auto baseline=capture;baseline.bake.gameImage=QImage(before+"/"+name+"-shaded.png");baseline.unlitGameImage=QImage(before+"/"+name+"-unlit.png");
    for(bool useDefaults:{false,true}){CMinimapExportDialog unchanged(baseline,{},"","",name,useDefaults?SMinimapGradeSettings():SMinimapGradeSettings(100,100,100,100,0),false,&window);unchanged.setAttribute(Qt::WA_DeleteOnClose,false);for(int pct:{0,50,100}){unchanged.findChild<QSlider*>("minimapShadingSlider")->setValue(pct);const auto old=QImage(before+"/"+name+(useDefaults?"-default-":"-")+QString::number(pct)+".png").convertToFormat(QImage::Format_ARGB32);check(unchanged.gradedImage()==old,"0/50/100% exact original pixels and color controls "+name+" "+QString::number(pct));}unchanged.close();}
}}}
 CMinimapExportDialog defaults(capture,{},"","mini274",name,SMinimapGradeSettings(),false,&window);defaults.setAttribute(Qt::WA_DeleteOnClose,false);for(int shade:{0,50,100,128,150}){defaults.findChild<QSlider*>("minimapShadingSlider")->setValue(shade);defaults.gradedImage().save(root+"/"+name+"-default-"+QString::number(shade)+".png");images["default-"+QString::number(shade)]=stats(defaults.gradedImage());}defaults.close();
 evidence[name]=images;dialog.close();
}
QFile result(root+"/images.json");check(result.open(QIODevice::WriteOnly),"evidence JSON");result.write(QJsonDocument(evidence).toJson());result.close();EnumWindows(inspect,0);check(visible==0,"0 visible native windows");window.close();out<<"RESULT "<<passes<<" PASS 0 FAIL\n";return 0;
}catch(const QString& e){out<<"ERROR "<<e<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";return 1;}}

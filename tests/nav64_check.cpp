#include <QtWidgets>
#include <QtTest>
#include <QOpenGLFunctions>
#include <algorithm>
#include "nav64_document.h"
#include "navmesh_panel.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "camera.h"
#include "mob.h"
#include "settings.h"
#include "resource_setup_dialog.h"

namespace {
struct Hidden:QObject {bool eventFilter(QObject* o,QEvent* e) override {if(e->type()==QEvent::Polish||e->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);return false;}};
QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& path,const QByteArray& data){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw QString("fixture write failed");}
bool until(const std::function<bool()>& done,int ms=60000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
}
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try{
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_TEST_ROOT"));check(!root.isEmpty(),"explicit private fixture root");
        const QString game=root+"/game",mod=game+"/MODS/Dejavu",maps=mod+"/Maps",nav=maps+"/Nav64";
        if(qEnvironmentVariableIsSet("EI_NAV64_BAKE_PROBE")){
            CNavmeshPanel panel;panel.setScene(maps+"/bz19g.mpr",maps+"/bz19g.mob",game);panel.setBakerPath(qEnvironmentVariable("EI_NAV64_BAKER"));
            check(panel.startBake(false,root+"/bake-output"),"focused baker invocation via production function");check(until([&](){return !panel.busy();},300000),"focused baker finishes");out<<panel.bakeLog()<<"\n";out.flush();
            check(panel.lastBakeSucceeded(),"focused baker exit0 + BAKE_DONE");write(root+"/baker-log.txt",panel.bakeLog().toUtf8());out<<"RESULT "<<passes<<" PASS 0 FAIL\n";return 0;
        }
        for(const auto& map:{"bz19g","zone1","zone14"})for(const auto& profile:nav64Profiles()){
            QElapsedTimer loadTimer;loadTimer.start();
            const auto d=loadNav64(nav+"/"+map+"--"+map+"."+profile+".nav64");
            if(!d.error.isEmpty())out<<"READER_ERROR "<<d.error<<"\n";
            check(d.valid&&d.header.mpr==QString(map)+".mpr"&&d.header.mob==QString(map)+".mob"&&d.header.profile==profile&&d.header.container==3&&d.header.algorithm==8&&d.tileCount>0&&d.polygonCount>0,QString("r13 LZMS/unshuffle/ABI/detail/identity %1 %2").arg(map,profile));
            out<<"MESH "<<map<<" "<<profile<<" tiles="<<d.tileCount<<" polys="<<d.polygonCount<<" islands="<<d.islandCount<<" narrow="<<d.narrowCount<<" load_ms="<<loadTimer.elapsed()<<"\n";
            out<<"DETAIL_OUTLIERS "<<map<<" "<<profile<<" count="<<d.detailOutlierVertices<<"\n";
            bool bounded=true;for(const auto& tile:d.renderTiles)for(const auto& area:tile.areas)for(const auto& v:area.vertices)bounded=bounded&&v.z()>=tile.minimum.z()-2*d.header.body.cellHeight&&v.z()<=tile.maximum.z()+2*d.header.body.cellHeight;
            check(d.detailOutlierVertices==0&&d.warnings.isEmpty()&&bounded,QString("r13 no height spikes or outliers %1 %2").arg(map,profile));
        }
        QDir().mkpath(root+"/resolver");const auto raw=bytes(nav+"/bz19g--bz19g.human.nav64");write(root+"/resolver/renamed.nav64",raw);QString error;
        check(findNav64ForScene(root+"/resolver","bz19g.mpr","bz19g.mob","human",&error).endsWith("renamed.nav64"),"resolver uses embedded identity after rename");
        check(findNav64ForScene(root+"/resolver","zone14.mpr","zone14.mob","human",&error).isEmpty(),"resolver rejects wrong embedded map");
        {CNavmeshPanel fallback;fallback.setScene(game+"/Base/Maps/bz19g.mpr",maps+"/bz19g.mob",game,maps);check(until([&](){return fallback.document().valid;}),"Base MPR fallback reads active mod Nav64 directory");}
        auto bad=raw;bad[12]=char(99);write(root+"/bad.nav64",bad);check(loadNav64(root+"/bad.nav64").error.contains(QStringLiteral("Алгоритм")),"unknown algorithm gives update/rebake error");
        bad=raw;bad[12]=char(7);write(root+"/bad.nav64",bad);check(loadNav64(root+"/bad.nav64").error.contains(QStringLiteral("игра не примет")),"algorithm7 explicitly rejected as unplayable stale mesh");
        bad=raw;bad[8]=char(2);write(root+"/bad.nav64",bad);check(loadNav64(root+"/bad.nav64").error.contains(QStringLiteral("Контейнер")),"obsolete container2 requests rebake");
        write(root+"/bad.nav64",raw.left(raw.size()-1));check(!loadNav64(root+"/bad.nav64").valid,"truncated container rejected");
        QStandardPaths::setTestModeEnabled(true);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        auto& view=*window.view();window.resize(1200,800);window.show();view.setParent(nullptr);view.resize(1100,820);view.show();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"hidden production OpenGL context");
        if(view.m_aiGraphPreviewWidget)view.m_aiGraphPreviewWidget->hide();
        view.m_neighborLandscapesVisible=false;
        auto* panel=view.navmeshPanel();check(panel!=nullptr,"production Navmesh dock installed beside AI controls");
        auto* mode=view.m_navigationSource;auto* profiles=panel->findChild<QComboBox*>("navmeshProfile");
        auto* fill=panel->findChild<QComboBox*>("navmeshFill");check(fill&&fill->count()==2&&panel->fillVisible(),"default fill and outline-only selector");
        check(profiles&&profiles->count()==8&&mode,"eight profiles and old graph switch");
        auto open=[&](const QString& map){window.openNeighborMapPair(maps+"/"+map+".mpr",maps+"/"+map+".mob",map,{},{});view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(view.m_pLand->filePath().baseName()==map&&view.m_environmentMob&&view.m_environmentMob->filePath().baseName()==map,"production map pair open: "+map);};
        QDir().mkpath(root+"/screenshots");
        for(const auto& name:{"bz19g","zone1","zone14"}){
            open(name);const auto mpr=bytes(maps+"/"+name+".mpr"),mob=bytes(maps+"/"+name+".mob");
            QImage profilesFrame(2200,900,QImage::Format_RGB32);profilesFrame.fill(QColor(25,25,25));int profileIndex=0;
            for(const auto& profile:nav64Profiles()){
                QElapsedTimer layerTimer;layerTimer.start();
                profiles->setCurrentIndex(profiles->findData(profile));mode->setCurrentIndex(1);view.m_aiGraphPreviewToggle->setChecked(true);panel->reload();
                check(until([&](){return panel->document().valid&&panel->document().header.profile==profile;}),QString("production layer %1 %2").arg(name,profile));
                out<<"LAYER "<<name<<" "<<profile<<" ready_ms="<<layerTimer.elapsed()<<"\n";
                const auto& doc=panel->document();
                if(profile=="tiny"){QVector3D target=(doc.minimum+doc.maximum)*.5f;if(QString(name)=="zone1"){target=QVector3D(186.5f,114.f,0);view.m_pLand->projectPt(target,true);}view.moveCameraToPoint(target);view.m_cam->moveAwayOn(QString(name)=="zone1"?65.f:qMax(doc.maximum.x()-doc.minimum.x(),doc.maximum.y()-doc.minimum.y())*1.1f);}
                view.makeCurrent();view.paintGL();
                QPainter painter(&profilesFrame);const int x=(profileIndex%4)*550,y=(profileIndex/4)*450;painter.setPen(Qt::white);painter.drawText(x+8,y+20,QString(name)+" / "+profile);painter.drawImage(x,y+30,view.grabFrameBuffer().scaled(550,410,Qt::KeepAspectRatio,Qt::SmoothTransformation));++profileIndex;
            }
            check(profilesFrame.save(root+"/screenshots/"+name+"-eight-profiles.png"),"all eight profiles framebuffer montage: "+QString(name));
            profiles->setCurrentIndex(profiles->findData("human"));check(until([&](){return panel->document().valid&&panel->document().header.profile=="human";}),"human ready for frame");
            mode->setCurrentIndex(1);view.m_aiGraphPreviewToggle->setChecked(true);view.makeCurrent();view.paintGL();auto meshFrame=view.grabFrameBuffer();
            mode->setCurrentIndex(0);view.makeCurrent();view.paintGL();auto graphFrame=view.grabFrameBuffer();
            check(view.m_aiGraphDocument.isValid()&&view.m_aiGraphPreviewVisible,"legacy AI graph remains available: "+QString(name));
            check(meshFrame!=graphFrame,"Navmesh changes actual framebuffer: "+QString(name));
            QImage combined(meshFrame.width()*2,meshFrame.height()+40,QImage::Format_RGB32);combined.fill(QColor(25,25,25));QPainter p(&combined);p.setPen(Qt::white);p.drawText(15,25,QString(name)+" / human / Navmesh r13 / fill 33%");p.drawText(meshFrame.width()+15,25,QString(name)+" / legacy AI graph");p.drawImage(0,40,meshFrame);p.drawImage(meshFrame.width(),40,graphFrame);p.end();
            check(combined.save(root+"/screenshots/"+name+"-navmesh-ai.png"),"side by side production framebuffer: "+QString(name));
            if(QString(name)=="zone1"){
                mode->setCurrentIndex(1);view.m_aiGraphPreviewToggle->setChecked(true);fill->setCurrentIndex(1);view.makeCurrent();view.paintGL();const auto outlines=view.grabFrameBuffer();
                check(!panel->fillVisible()&&outlines!=meshFrame,"outline-only mode changes actual framebuffer");
                QPainter contourPainter(&combined);contourPainter.fillRect(combined.rect(),QColor(25,25,25));contourPainter.setPen(Qt::white);contourPainter.drawText(15,25,"zone1 / human / r13 / contours only");contourPainter.drawText(meshFrame.width()+15,25,"zone1 / legacy AI graph");contourPainter.drawImage(0,40,outlines);contourPainter.drawImage(meshFrame.width(),40,graphFrame);contourPainter.end();
                check(combined.save(root+"/screenshots/zone1-contours-ai.png"),"zone1 contours beside old AI framebuffer");fill->setCurrentIndex(0);
                mode->setCurrentIndex(1);view.m_aiGraphPreviewToggle->setChecked(true);QVector<double> times;auto* gl=QOpenGLContext::currentContext()->functions();
                for(int frame=0;frame<23;++frame){QElapsedTimer timer;timer.start();view.paintGL();gl->glFinish();if(frame>=3)times<<double(timer.nsecsElapsed())/1.e6;}
                std::sort(times.begin(),times.end());out<<"ZONE1_RENDER viewport="<<view.width()<<"x"<<view.height()<<" tiles="<<view.m_navmeshDrawnTiles<<"/"<<panel->document().tileCount<<" vertices="<<view.m_navmeshDrawnVertices<<" paint_gpu_finish_median_ms="<<times[10]<<" p95_ms="<<times[18]<<"\n";
                check(view.m_navmeshDrawnTiles>0&&view.m_navmeshDrawnTiles<panel->document().tileCount,"zone1 gate view culls offscreen tile batches");check(gl->glGetError()==GL_NO_ERROR,"zone1 layer has no OpenGL errors");
            }
            panel->setUnsavedChanges(true);check(!panel->startBake(false,root+"/bake-output"),"dirty map cannot bake stale disk inputs");panel->setUnsavedChanges(false);
            check(bytes(maps+"/"+name+".mpr")==mpr&&bytes(maps+"/"+name+".mob")==mob,"layer never writes MPR/MOB: "+QString(name));
        }
        open("zone7");check(window.loadMobWithProgress(QFileInfo(maps+"/Autozone7letter.mob")),"227 supplementary MOB loaded");open("zone6");open("zone7");check(view.m_aMob.size()==1,"227 map switching detaches supplementary MOB");
        open("bz19g");panel->setUnsavedChanges(false);panel->setBakerPath(qEnvironmentVariable("EI_NAV64_BAKER"));
        check(panel->startBake(false,root+"/bake-output"),"production function launches real r13 baker into private output-root");
        check(until([&](){return !panel->busy();},300000),"real baker finishes");out<<"REAL_BAKER_LOG\n"<<panel->bakeLog()<<"\n";out.flush();
        check(panel->lastBakeSucceeded()&&panel->lastBakeExitCode()==0&&panel->bakeLog().contains("BAKE_DONE ")&&panel->bakeLog().contains("MAP_DONE ")&&panel->bakeLog().contains("PROFILE "),"real exit0 + BAKE_DONE and progress parser");
        check(panel->bakeLog().count("height_outliers=0")==8&&panel->findChild<QLabel*>("navmeshHeightOutliers")->text().contains("height_outliers=0"),"all eight actual PROFILE height_outliers=0 parsed and shown");
        check(until([&](){return panel->document().valid&&panel->document().path.startsWith(root+"/bake-output/");}),"layer refreshes from real baked output");
        for(const auto& profile:nav64Profiles())check(loadNav64(root+"/bake-output/Maps/Nav64/bz19g--bz19g."+profile+".nav64").valid,"real baker output profile: "+profile);
        panel->setScene(maps+"/missing.mpr",maps+"/missing.mob",game);
        check(panel->startBake(false,root+"/error-output"),"real error scenario starts");check(until([&](){return !panel->busy();},60000),"real error finishes");
        out<<"REAL_ERROR_LOG\n"<<panel->bakeLog()<<"\n";check(!panel->lastBakeSucceeded()&&panel->lastBakeExitCode()==5&&panel->bakeLog().contains("MAP_MISSING"),"real missing map: MAP_MISSING + exit5 + explanation");
        write(maps+"/corrupt.mpr",QByteArray(16,char(0)));write(maps+"/corrupt.mob",bytes(maps+"/bz19g.mob"));panel->setScene(maps+"/corrupt.mpr",maps+"/corrupt.mob",game);
        check(panel->startBake(false,root+"/error-output"),"real corrupt source scenario starts");check(until([&](){return !panel->busy();},60000),"real corrupt source finishes");out<<"REAL_CORRUPT_LOG\n"<<panel->bakeLog()<<"\n";check(!panel->lastBakeSucceeded()&&panel->lastBakeExitCode()==4&&panel->bakeLog().contains("MAP_ERROR"),"real corrupt source: MAP_ERROR + exit4");
        panel->setScene(maps+"/bz19g.mpr",maps+"/bz19g.mob",game);panel->setBakerPath(root+"/not-found.exe");check(!panel->startBake(false,root+"/error-output"),"missing executable gives actionable error");
        panel->setBakerPath(root+"/app/nav64_mock_baker.exe");
        for(const auto& scenario:{"missing","exit","map_missing","outliers","ok","cancel"}){
            qputenv("NAV64_MOCK",scenario);check(panel->startBake(false,root+"/bake-output"),"mock lifecycle launch: "+QString(scenario));if(QString(scenario)=="cancel")panel->cancelBake();
            check(until([&](){return !panel->busy();},10000),"mock finishes: "+QString(scenario));check(panel->lastBakeSucceeded()==(QString(scenario)=="ok"),"exit/BAKE_DONE/cancel acceptance: "+QString(scenario));
            if(QString(scenario)=="outliers")check(panel->findChild<QLabel*>("navmeshHeightOutliers")->text().contains(QStringLiteral("Предупреждение")),"nonzero PROFILE height_outliers is a visible warning");
        }
        check(panel->bakeLog().contains("BACKUP "),"existing private nav64 backed up before replacement");
        qputenv("NAV64_MOCK","ok");check(panel->startBake(true,root+"/bake-output"),"all maps isolated invocation starts");check(until([&](){return !panel->busy();},10000)&&panel->lastBakeSucceeded()&&panel->bakeLog().contains("--all"),"all maps uses --all through same function and parser");
        write(root+"/baker-log.txt",panel->bakeLog().toUtf8());
        out<<"RESULT "<<passes<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"ERROR "<<e<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";out.flush();return 1;}
}

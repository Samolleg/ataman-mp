#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "navmesh_panel.h"
#include "landscape.h"
#include "mob.h"
#include "camera.h"
#include "localization.h"
#include "resource_setup_dialog.h"

namespace {
struct OffscreenWindows : QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))if(widget->isWindow()) {
                widget->setAttribute(Qt::WA_DontShowOnScreen);
                widget->setAttribute(Qt::WA_ShowWithoutActivating);
            }
        return false;
    }
};
bool until(const std::function<bool()>& done,int ms=60000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& path,const QByteArray& data){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw QString("write failed: ")+path;}
}
// Links the unmodified production MainWindow/CView/renderer objects. The view
// stays inside MainWindow throughout. All top-levels are prevented from being
// mapped to the owner's desktop. Captures read only this process's Qt widgets
// and OpenGL back buffer, never the screen or another process.
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_TEST_ROOT"));
        const QString evidence=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_EVIDENCE"));
        check(!root.isEmpty()&&!evidence.isEmpty(),"explicit private fixture and evidence");
        const QString game=root+"/game",maps=game+"/MODS/Dejavu/Maps",nav=maps+"/Nav64";
        const auto mprBefore=bytes(maps+"/zone1.mpr"),mobBefore=bytes(maps+"/zone1.mob");
        QStandardPaths::setTestModeEnabled(true);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        OffscreenWindows hidden;qApp->installEventFilter(&hidden);
        check(ei::installApplicationTranslator(*qApp,QStringLiteral("ru")),"production Russian translator");
        qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        // An isolated profile has no saved dock topology. Select an ordinary
        // editor workspace (maps at left, properties at right) using the real
        // docks; leave the production central widget and scene intact.
        for(auto* dock:window.findChildren<QDockWidget*>())dock->hide();
        window.m_mapsDock->show();window.m_propertiesDock->show();
        window.addDockWidget(Qt::LeftDockWidgetArea,window.m_mapsDock);
        window.addDockWidget(Qt::RightDockWidgetArea,window.m_propertiesDock);
        auto& view=*window.view();window.resize(1500,950);window.show();QTest::qWait(300);view.makeCurrent();
        check(view.isValid()&&view.window()==&window,"real MainWindow hosts production OpenGL CView");
        check(window.testAttribute(Qt::WA_DontShowOnScreen)&&!window.isActiveWindow(),"MainWindow is offscreen and never activated");
        view.m_neighborLandscapesVisible=false;
        auto* panel=view.navmeshPanel();auto* outer=view.m_aiGraphPreviewWidget;
        auto* source=outer->findChild<QComboBox*>("navigationSource");
        check(panel&&source&&panel->isAncestorOf(panel->findChild<QPushButton*>("navmeshBakeMap"))&&outer->isAncestorOf(panel),"Navmesh controls belong to AI navigation viewport panel");
        check(!window.findChild<QDockWidget*>("navmeshDock"),"no separate Navmesh dock");
        check(source->count()==2&&source->currentIndex()==0&&!panel->layerVisible()&&!view.m_aiGraphPreviewVisible,"old graph default, overlays off");
        auto open=[&](const QString& map){window.openNeighborMapPair(maps+"/"+map+".mpr",maps+"/"+map+".mob",map,{},{});view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(view.m_pLand->filePath().baseName()==map&&view.m_environmentMob&&view.m_environmentMob->filePath().baseName()==map,"production map pair: "+map);};
        auto visible=[&](QWidget* w){return w&&w->isVisible()&&outer->rect().contains(QRect(w->mapTo(outer,QPoint()),w->size()));};
        auto bounded=[&](){return view.rect().contains(outer->geometry());};
        auto checkpoint=[&](const QString& phase){
            QTest::qWait(250);view.makeCurrent();view.paintGL();QCoreApplication::processEvents();
            check(bounded(),"panel stays inside production scene: "+phase);
            check(window.testAttribute(Qt::WA_DontShowOnScreen)&&!window.isActiveWindow(),"capture remains offscreen: "+phase);
            const QImage scene=view.grabFrameBuffer();
            check(!scene.isNull()&&scene.width()==view.width()&&scene.height()==view.height(),"hidden OpenGL framebuffer: "+phase);
            const QPixmap panelGrab=outer->grab();
            check(panelGrab.save(evidence+"/"+phase+"-panel.png"),"production panel QWidget grab: "+phase);
            check(scene.save(evidence+"/"+phase+"-scene.png"),"offscreen scene framebuffer saved: "+phase);
            QImage main=window.grab().toImage();
            check(main.save(evidence+"/"+phase+"-mainwindow-raw.png"),"raw production MainWindow QWidget grab: "+phase);
            // QGLWidget is a native GL child. Its back buffer is separate from
            // QWidget::grab on Windows; place those actual pixels at its real
            // MainWindow geometry, preserving every visible overlay's Qt grab.
            const QPoint origin=view.mapTo(&window,QPoint());
            QRegion sceneClip(QRect(origin,view.size()));
            for(auto* child:view.findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
                if(child->isVisible())sceneClip-=QRect(child->mapTo(&window,QPoint()),child->size());
            QPainter painter(&main);painter.setClipRegion(sceneClip);painter.drawImage(origin,scene);painter.end();
            check(main.save(evidence+"/"+phase+"-mainwindow.png"),"MainWindow plus its own GL back buffer: "+phase);
            check(main.copy(QRect(origin,view.size())).save(evidence+"/"+phase+".png"),"production viewport and panel crop: "+phase);
            QJsonObject request{{"phase",phase},{"capture","WA_DontShowOnScreen QWidget::grab plus own QGLWidget::grabFrameBuffer"},{"windowWidth",window.width()},{"windowHeight",window.height()},
                {"panelX",outer->mapTo(&window,QPoint()).x()},{"panelY",outer->mapTo(&window,QPoint()).y()},
                {"panelWidth",outer->width()},{"panelHeight",outer->height()}};
            write(evidence+"/"+phase+".json",QJsonDocument(request).toJson());
            out<<"CAPTURE "<<phase<<"\n";out.flush();
        };
        open("zone1");QVector3D target(186.5f,114.f,0);view.m_pLand->projectPt(target,true);view.moveCameraToPoint(target);view.m_cam->moveAwayOn(65.f);
        view.m_aiGraphPreviewToggle->click();
        check(view.m_aiGraphDocument.isValid()&&view.m_aiGraphPreviewVisible,"old embedded graph visible on zone1");
        check(visible(source)&&visible(view.m_aiGraphLayerSlider)&&visible(view.m_aiGraphRegenerateButton)&&visible(view.m_aiGraphCompareButton)&&visible(view.m_aiGraphPersistButton),"legacy slider and all actions visible inside panel");
        check(view.m_aiGraphLayerSlider->minimum()==0&&view.m_aiGraphLayerSlider->maximum()==7,"legacy layers 0..7 retained");
        checkpoint("01-old-ai");
        view.setFocus();QTest::keyClick(&view,Qt::Key_H);QTest::qWait(50);check(!view.m_aiGraphPreviewVisible,"H hides old graph");QTest::keyClick(&view,Qt::Key_H);QTest::qWait(50);check(view.m_aiGraphPreviewVisible,"H shows old graph");
        check(QDir().rename(nav,maps+"/Nav64-held"),"isolate no-mesh scenario");
        source->setCurrentIndex(1);panel->reload();panel->setBakerPath(root+"/absent.exe");QTest::qWait(150);
        auto* status=panel->findChild<QLabel*>("navmeshStatus");auto* browse=panel->findChild<QPushButton*>("navmeshBakerBrowse");
        check(status->text().contains(QStringLiteral("не запечена"))&&browse->text().contains(QStringLiteral("не найден")),"missing mesh and baker explained in same panel");
        for(const auto& name:{"navmeshProfile","navmeshFill","navmeshNarrow","navmeshIslands","navmeshBakeMap","navmeshBakeAll","navmeshBakerBrowse","navmeshLogToggle"})check(visible(panel->findChild<QWidget*>(name)),QString("Navmesh control visible and contained: ")+name);
        check(!panel->findChild<QLineEdit*>()&&!panel->findChild<QWidget*>("navmeshNavsrcReserved"),"no empty path field or inactive navsrc");
        checkpoint("02-navmesh-missing");
        const auto filesBefore=QDir(maps).entryList();
        for(const auto& name:{"navmeshBakeMap","navmeshBakeAll"}){panel->findChild<QPushButton*>(name)->click();check(!panel->busy()&&status->text().contains(QStringLiteral("Запекатель не найден")),QString("missing baker button refusal: ")+name);}
        check(QDir(maps).entryList()==filesBefore&&!QDir(nav).exists(),"missing baker creates no output");
        check(QDir().rename(maps+"/Nav64-held",nav),"restore private r14 meshes");panel->reload();
        check(until([&](){return panel->document().valid;}),"r14 zone1 human loaded");
        auto* profiles=panel->findChild<QComboBox*>("navmeshProfile");auto* fill=panel->findChild<QComboBox*>("navmeshFill");
        check(profiles->count()==8&&profiles->currentData()=="human"&&profiles->currentText().contains(QStringLiteral("Человек")),"eight readable profiles, human selected");
        check(panel->document().header.algorithm==9&&panel->document().detailOutlierVertices==0&&panel->layerVisible(),"r14 algorithm8 layer visible, no height outliers");
        panel->setBakerPath(qEnvironmentVariable("EI_NAV64_BAKER"));
        check(nav64SupportedAlgorithms()==QVector<quint32>({8,9}),"one supported algorithm registry: 8 and 9");
        check(browse->text().contains("r14")&&browse->text().contains(QStringLiteral("алгоритм 9")),"certified installed r14 algorithm9 caption");
        const auto humanPath=panel->document().path;const auto humanBytes=bytes(humanPath);
        for(const auto& id:nav64Profiles()){const auto d=loadNav64(nav+"/zone1--zone1."+id+".nav64");check(d.valid&&d.header.algorithm==9&&d.detailOutlierVertices==0,"live copied algorithm9 profile: "+id);}
        auto* legend=panel->findChild<QCheckBox*>("navmeshLegend");
        check(legend&&legend->isVisible()&&legend->isChecked()&&panel->legendVisible(),"legend default on and visible in production panel");
        panel->findChild<QCheckBox*>("navmeshIslands")->setChecked(true);
        check(panel->legendHtml().contains(QStringLiteral("Центры островов"))&&panel->legendHtml().contains(QStringLiteral("Границы тайлов")),"legend describes actual red island centres and storage tiles");
        checkpoint("03-navmesh-human");
        view.makeCurrent();view.paintGL();const auto legendOn=view.grabFrameBuffer();legend->setChecked(false);view.paintGL();const auto legendOff=view.grabFrameBuffer();check(legendOn!=legendOff&&!panel->legendVisible(),"legend changes real offscreen scene pixels and toggles off");legend->setChecked(true);
        auto* mode=panel->findChild<QComboBox*>("navmeshFill");
        check(mode->count()==3&&mode->itemText(2)==QStringLiteral("Потери проходимости"),"losses mode available beside Fill and Contours");
        QElapsedTimer snapshotTimer;snapshotTimer.start();mode->setCurrentIndex(2);const auto snapshotMs=snapshotTimer.elapsed();
        check(until([&](){return panel->comparison().valid;}),"background comparison human vs layer3 completes");
        const auto comparison=panel->comparison();
        check(panel->comparisonLayer()==3&&comparison.layer==3&&comparison.width==view.m_aiGraphLoadedDocument.width()*8,"human mapped to legacy layer3 on 0.5m grid");
        auto* counter=panel->findChild<QLabel*>("navmeshCompareStatus");auto* layer=panel->findChild<QComboBox*>("navmeshCompareLayer");
        check(counter->isVisible()&&counter->text().contains(QStringLiteral("потеряно"))&&layer->isVisible()&&comparison.lost>0&&comparison.gained>0,"visible area counter with nonzero lost and gained cells");
        check(!panel->legendHtml().contains(QStringLiteral("Границы тайлов"))&&!panel->legendHtml().contains(QStringLiteral("Центры островов")),"loss legend omits non-rendered tile, island and area diagnostics");
        out<<"COMPARISON lost_m2="<<comparison.lost*.25<<" gained_m2="<<comparison.gained*.25<<" regions="<<comparison.areas.size()<<" background_ms="<<comparison.milliseconds<<" snapshot_ms="<<snapshotMs<<"\n";out.flush();
        write(evidence+"/comparison.json",QJsonDocument(QJsonObject{{"lostCells",comparison.lost},{"gainedCells",comparison.gained},{"lostM2",comparison.lost*.25},{"gainedM2",comparison.gained*.25},{"regions",comparison.areas.size()},{"layer",comparison.layer},{"backgroundMs",double(comparison.milliseconds)},{"snapshotMs",double(snapshotMs)},{"source","legacy algorithm recomputed mask, not serialized graph mask"}}).toJson());
        checkpoint("06-passability-losses");
        auto* regions=panel->findChild<QListWidget*>("navmeshRegions");check(regions->isVisible()&&regions->count()==comparison.areas.size(),"difference regions navigable in panel");
        const auto oldCamera=view.m_cam->viewMatrix();regions->setCurrentRow(0);QTest::mouseClick(regions->viewport(),Qt::LeftButton,Qt::NoModifier,regions->visualItemRect(regions->item(0)).center());
        check(panel->selectedRegion()>=0&&oldCamera!=view.m_cam->viewMatrix(),"click area selects region and moves production camera");
        // Race two selected layers; no stale result may replace the latest.
        layer->setCurrentIndex(layer->findData(1));layer->setCurrentIndex(layer->findData(3));
        check(until([&](){return panel->comparison().valid&&panel->comparison().layer==3;}),"explicit layer override and stale worker result guard");
        mode->setCurrentIndex(0);view.moveCameraToPoint(target);view.m_cam->moveAwayOn(65.f);
        auto synthetic=humanBytes;synthetic[12]=char(8);write(humanPath,synthetic);panel->reload();
        check(until([&](){return panel->document().valid&&panel->document().header.algorithm==8;}),"algorithm8 remains readable");
        auto* mismatch=panel->findChild<QLabel*>("navmeshAlgorithmStatus");
        check(mismatch->isVisible()&&mismatch->text()==QStringLiteral("сетка алгоритма 8, запекатель — 9: перепеките")&&panel->bakeLog().contains(mismatch->text()),"algorithm mismatch visible in panel and log");
        checkpoint("07-algorithm-mismatch");
        synthetic[12]=char(99);write(humanPath,synthetic);panel->reload();
        check(mismatch->isVisible()&&mismatch->text().contains("99"),"unsupported algorithm still reports actual baker mismatch");
        write(humanPath,humanBytes);panel->reload();check(until([&](){return panel->document().valid&&panel->document().header.algorithm==9;}),"restore private original algorithm9");
        panel->setBakerPath(root+"/work/handoffs/old.exe");check(panel->findChild<QPushButton*>("navmeshUseInstalled")->isVisible(),"manual handoff path offers installed baker without overwrite");
        panel->findChild<QPushButton*>("navmeshUseInstalled")->click();check(browse->text().contains(QStringLiteral("установленный"))&&browse->text().contains("r14")&&!QSettings().contains("nav64/bakerPath"),"explicit installed-baker choice clears manual override");
        view.setFocus();QTest::keyClick(&view,Qt::Key_H);check(!panel->layerVisible(),"H hides Navmesh");QTest::keyClick(&view,Qt::Key_H);check(panel->layerVisible(),"H shows Navmesh");
        QKeyEvent russianH(QEvent::KeyPress,0x0420,Qt::NoModifier,0x23,0x48,0,QStringLiteral("р"));QApplication::sendEvent(&view,&russianH);check(!panel->layerVisible(),"physical H works with Russian layout");QApplication::sendEvent(&view,&russianH);check(panel->layerVisible(),"Russian physical H toggles back");
        view.makeCurrent();view.paintGL();const auto filled=view.grabFrameBuffer();fill->setCurrentIndex(1);view.makeCurrent();view.paintGL();check(!panel->fillVisible()&&filled!=view.grabFrameBuffer(),"contours change production framebuffer");fill->setCurrentIndex(0);
        for(const auto& name:{"navmeshNarrow","navmeshIslands"}){auto* box=panel->findChild<QCheckBox*>(name);box->click();check(box->isChecked(),QString("overlay option responds: ")+name);box->click();}
        auto* log=panel->findChild<QToolButton*>("navmeshLogToggle");log->click();QTest::qWait(50);check(visible(panel->findChild<QPlainTextEdit*>("navmeshBakeLog"))&&bounded(),"expanded log contained in real scene");log->click();
        view.m_aiGraphPreviewCollapse->click();check(view.m_navigationScroll->isHidden()&&bounded(),"minus collapses both source modes");view.m_aiGraphPreviewCollapse->click();check(visible(profiles)&&bounded(),"expand restores Navmesh controls");
        window.resize(1100,740);QTest::qWait(150);check(bounded()&&visible(profiles)&&visible(panel->findChild<QPushButton*>("navmeshBakeAll")),"smaller MainWindow keeps Navmesh actions inside scene");window.resize(1500,950);QTest::qWait(150);
        panel->setUnsavedChanges(true);check(!panel->startBake(false,root+"/bake-output")&&status->text().contains(QStringLiteral("сохраните")),"dirty disk inputs refused with visible reason");panel->setUnsavedChanges(false);
        check(panel->startBake(false,root+"/bake-output"),"real r14 baker launched with isolated output-root");
        check(visible(panel->findChild<QPushButton*>("navmeshBakeCancel"))&&visible(panel->findChild<QProgressBar*>("navmeshBakeProgress")),"live progress and cancel visible inside AI panel");
        checkpoint("04-baking");
        check(until([&](){return !panel->busy();},300000),"real bake finished");
        check(panel->lastBakeSucceeded()&&panel->lastBakeExitCode()==0&&panel->bakeLog().contains("BAKE_DONE "),"exit0 BAKE_DONE, all PROFILE diagnostics passed");
        check(until([&](){return panel->document().valid&&panel->document().path.startsWith(root+"/bake-output");}),"displayed layer automatically reloaded from new bake output");
        check(panel->layerVisible()&&QDir(root+"/bake-output/Maps/Nav64").entryList({"*.nav64"},QDir::Files).size()==8,"eight baked profiles and layer visible");
        write(evidence+"/real-baker.log",panel->bakeLog().toUtf8());
        checkpoint("05-baked-updated");
        source->setCurrentIndex(0);view.m_aiGraphRegenerateButton->click();
        check(until([&](){return view.m_aiGraphRegeneratedFresh&&view.m_aiGraphBuildWatcher&&!view.m_aiGraphBuildWatcher->isRunning();},120000),"legacy regenerate on actual zone1");
        check(view.m_aiGraphLoadedDocument.compareNavigation(view.m_aiGraphRegeneratedDocument).comparable,"legacy graph comparison remains available");
        bool compared=false;QTimer closer;QObject::connect(&closer,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* msg=qobject_cast<QMessageBox*>(w)){compared=msg->text().contains(QStringLiteral("навигац"),Qt::CaseInsensitive)||!msg->informativeText().isEmpty();msg->accept();}});closer.start(10);view.m_aiGraphCompareButton->click();closer.stop();check(compared,"real Compare graph button opens report");
        open("zone7");check(window.loadMobWithProgress(QFileInfo(maps+"/Autozone7letter.mob")),"227 additional MOB loaded");open("zone6");open("zone7");check(view.m_aMob.size()==1,"227 switching detaches additional MOB safely");
        check(bytes(maps+"/zone1.mpr")==mprBefore&&bytes(maps+"/zone1.mob")==mobBefore,"MPR and legacy embedded graph bytes unchanged");
        // The new fast mask entry point must exactly match full legacy builds.
        SAiGraphPreviewBuildInput flat;flat.graphWidth=4;flat.graphHeight=4;flat.terrainHeights.fill(0,1024);flat.waterDepths.fill(0,1024);flat.terrainTypes.fill(0,1024);
        const auto full=CAiGraphPreviewBuilder::build(flat);check(full.valid,"synthetic full legacy build");
        for(int i=0;i<8;++i){const auto fast=CAiGraphPreviewBuilder::passability(flat,i);check(fast.valid&&fast.layerPassability[i]==full.layerPassability[i],QString("mask-only equals full legacy classifier layer %1").arg(i));}
        out<<"RESULT "<<passes<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"ERROR "<<e<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";out.flush();return 1;}
}

#include <QtWidgets>
#include <QtTest>
#include "nav64_document.h"
#include "navmesh_panel.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "resource_setup_dialog.h"

namespace {
struct Hidden:QObject {
    bool eventFilter(QObject* o,QEvent* e) override {
        if(e->type()==QEvent::Polish||e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
bool until(const std::function<bool()>& done){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<60000)QTest::qWait(10);return done();}
QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
}
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passes;};
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_TEST_ROOT"));
        check(!root.isEmpty(),"explicit private fixture");
        const QString game=root+"/game",maps=game+"/MODS/Dejavu/Maps";
        const auto navPath=maps+"/Nav64/bz19g--bz19g.human.nav64";
        const auto navBefore=bytes(navPath),mprBefore=bytes(maps+"/bz19g.mpr"),mobBefore=bytes(maps+"/bz19g.mob");
        check(!navBefore.isEmpty(),"one r13 private copy");
        QStandardPaths::setTestModeEnabled(true);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        auto& view=*window.view();window.resize(1200,800);window.show();view.setParent(nullptr);view.resize(1100,820);view.show();QCoreApplication::processEvents();view.makeCurrent();
        check(view.isValid(),"hidden production OpenGL context");view.m_neighborLandscapesVisible=false;
        auto* panel=view.navmeshPanel();check(panel!=nullptr,"production Navmesh panel");
        auto* mode=view.m_navigationSource;
        check(mode&&mode->currentIndex()==0&&!panel->layerVisible(),"layer is OFF by default");
        auto open=[&](const QString& map){window.openNeighborMapPair(maps+"/"+map+".mpr",maps+"/"+map+".mob",map,{},{});view.makeCurrent();view.ensureTerrainAutoCatalogReady();check(view.m_pLand->filePath().baseName()==map&&view.m_environmentMob&&view.m_environmentMob->filePath().baseName()==map,"production map pair: "+map);};
        open("bz19g");panel->setBakerPath(root+"/absent-baker.exe");
        const auto filesBefore=QDir(maps+"/Nav64").entryList(QDir::Files);
        QSignalSpy spy(panel,SIGNAL(bakeFinished(bool)));
        for(const auto& name:{"navmeshBakeMap","navmeshBakeAll"}){
            auto* button=panel->findChild<QPushButton*>(name);check(button&&button->isEnabled(),QString("bake button available: ")+name);button->click();
            check(!panel->busy()&&panel->bakeLog().contains(QStringLiteral("Запекатель не найден")),QString("missing baker explained: ")+name);
        }
        check(spy.count()==2&&!QFileInfo(game+"/Engine64").exists()&&QDir(maps+"/Nav64").entryList(QDir::Files)==filesBefore,"both buttons refuse without output");
        check(until([&](){return panel->document().valid;}),"one r13 layer loaded");
        check(panel->document().header.container==3&&panel->document().header.algorithm==8&&panel->document().detailOutlierVertices==0,"container3 algorithm8 no height outliers");
        mode->setCurrentIndex(1);view.m_aiGraphPreviewToggle->setChecked(true);view.makeCurrent();view.paintGL();const auto navFrame=view.grabFrameBuffer();
        check(panel->layerVisible(),"Navmesh enabled explicitly");
        mode->setCurrentIndex(0);view.makeCurrent();view.paintGL();
        check(view.m_aiGraphDocument.isValid()&&view.m_aiGraphPreviewVisible,"legacy AI graph loaded beside Navmesh");
        check(navFrame!=view.grabFrameBuffer(),"Navmesh and old graph produce distinct frames");
        check(bytes(navPath)==navBefore&&bytes(maps+"/bz19g.mpr")==mprBefore&&bytes(maps+"/bz19g.mob")==mobBefore,"layer and absent baker preserve nav64 MPR MOB bytes");
        open("zone7");check(window.loadMobWithProgress(QFileInfo(maps+"/Autozone7letter.mob")),"227 supplementary MOB loaded");
        open("zone6");open("zone7");check(view.m_aMob.size()==1,"227 neighbor switching detaches supplementary MOB");
        out<<"RESULT "<<passes<<" PASS 0 FAIL\n";out.flush();return 0;
    } catch(const QString& e){out<<"ERROR "<<e<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";out.flush();return 1;}
}

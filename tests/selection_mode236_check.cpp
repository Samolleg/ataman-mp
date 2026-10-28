#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "mob.h"
#include "worldobj.h"
#include "unit.h"
#include "camera.h"
#include "scene.h"
#include "operationmanager.h"
#include "prepared_static_visual.h"
#include "resourcemanager.h"
#include "resource_setup_dialog.h"

namespace {
struct HiddenWindows:QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(object))if(w->isWindow()){
                w->setAttribute(Qt::WA_DontShowOnScreen);w->setAttribute(Qt::WA_ShowWithoutActivating);
            }
        return false;
    }
};
void write(const QString& path,const QByteArray& data){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw path;}
}
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passed;};
    try {
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_SELECTION_CHECK_ROOT"));
        check(!root.isEmpty(),"explicit private output");
        HiddenWindows hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(1);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        window.resize(1400,900);window.show();QTest::qWait(100);auto& view=*window.view();view.makeCurrent();
        check(view.isValid()&&window.testAttribute(Qt::WA_DontShowOnScreen)&&!window.isActiveWindow(),"production MainWindow/CView offscreen");
        CMprDocument source;CValidationReport report;
        check(source.load(QFileInfo(qEnvironmentVariable("EI_SELECTION_MPR_SOURCE")),&report),"private MPR source");
        SMprCreationRequest request;request.innerNamespace="select236";request.maxZ=512;request.flatRawHeight=32768;request.sectorCountX=1;request.sectorCountY=1;
        QByteArray plane;check(source.buildNewCanvas(request,&plane,&report),"synthetic surface above100");
        const auto path=root+"/select236.mpr";write(path,plane);view.m_pLand->readMap(QFileInfo(path),false);
        view.m_neighborLandscapesVisible=false;view.m_landscapeVisible=true;view.m_bDrawLand=true;view.m_bDrawWater=false;
        auto* mob=new CMob;mob->attach(&view,nullptr);mob->setFileName(QFileInfo(root+"/select236.mob"));view.m_aMob.append(mob);view.m_activeMob=mob;view.m_environmentMob=mob;
        auto* object=new CWorldObj;object->setMapId(1);QVector3D position(16,16,0);object->setPos(position);object->setDrawPosition({16,16,256.004f});object->setTexture(CTextureList::getInstance()->textureDefault());
        SPreparedStaticPart part;part.name="exposed-static";
        const QVector3D vertices[]={{-3,0,0},{3,0,0},{3,0,7},{-3,0,0},{3,0,7},{-3,0,7}};
        for(const auto& p:vertices){SVertexData v;v.position=p;v.normal={0,-1,0};v.texCoord={.5f,.5f};part.vertices.append(v);}
        object->installPreparedStaticParts({part});mob->addNode(object);
        auto* unit=new CUnit;unit->setMapId(2);mob->addNode(unit);mob->logicNodesUpdate();
        view.collectObjectTreeData();window.updateOperationAvailability();
        view.m_cam->setLocation({16,16,291.004f},{16,16,256.004f},{-45,0,0});
        view.m_normalProjection.setToIdentity();view.m_normalProjection.perspective(45,float(view.width())/view.height(),1,1000);view.updateCameraProjection();
        auto screen=[&](QVector3D world){const auto p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));return QPoint(qRound(p.x()),view.height()-1-qRound(p.y()));};
        const auto exposed=screen({16,16,260.004f});
        auto click=[&](const QPoint& p){QMouseEvent press(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&press);QMouseEvent release(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&release);};
        auto* status=window.findChild<QLabel*>("statusBar");auto* coordinates=window.findChild<QLineEdit*>("mousePosText");check(status&&coordinates,"real status controls");
        window.on_actionChange_mod_e_triggered();
        check(CScene::getInstance()->getMode()==eEditModeLogic&&mob->logicNodes().contains(unit)&&!mob->logicNodes().contains(object),"behavior retains its own unit/logic selection");
        click(exposed);check(view.selectionSnapshot().isEmpty(),"original case: static object excluded in behavior");
        window.m_operationActions.value(EButtonOpSelect)->trigger();
        check(CScene::getInstance()->getMode()==eEditModeObjects&&!window.m_behaviorModeButton->isChecked(),"Select button returns to objects");
        click(exposed);check(view.selectionSnapshot()==QList<CNode*>{object},"same real mouse click selects static object after Select");
        check(status->text().startsWith(QStringLiteral("Выбор объектов")),"ordinary selection has correct hint");
        window.on_actionChange_mod_e_triggered();window.handleEscapeShortcut();
        check(CScene::getInstance()->getMode()==eEditModeObjects,"Esc exits idle behavior");
        view.m_landscapeTools->activateMode(ELandscapeEditMode::Sculpt,false);view.setLandscapeEditingEnabled(true);
        check(view.m_landscapeTools->editingEnabled()&&status->text().contains("LMB-sculpt"),"sculpt owns its context hint");
        view.refreshSelectContextHelp();check(status->text().contains("LMB-sculpt"),"selection refresh cannot erase active sculpt hint");
        window.hideLandscapeTools();check(!view.m_landscapeTools->editingEnabled()&&!status->text().contains("LMB-sculpt"),"closing even hidden dock clears brush and stale hint");
        view.setLandscapeEditingEnabled(true);window.handleEscapeShortcut();check(!view.m_landscapeTools->editingEnabled(),"Esc clears hidden active sculpt");
        view.setLandscapeEditingEnabled(true);window.m_operationActions.value(EButtonOpSelect)->trigger();check(!view.m_landscapeTools->editingEnabled(),"Select clears hidden active sculpt");
        click(exposed);check(view.selectionSnapshot()==QList<CNode*>{object},"static mouse selection restored after sculpt");
        window.on_actionChange_mod_e_triggered();unit->setState(eSelect);view.viewParameters();view.changeOperation(EButtonOpMove);
        check(view.m_pOp->operationMethod()==eOperationMethodMoveAxis,"behavior move starts");
        const auto original=unit->position();auto moved=original+QVector3D(2,3,4);unit->setPos(moved);
        check(unit->position()!=original,"pending transform changed unit position");
        window.handleEscapeShortcut();
        check(view.m_pOp->operationMethod()==eOperationMethodSelect&&CScene::getInstance()->getMode()==eEditModeLogic&&unit->position()==original,"first Esc cancels transform while preserving behavior and position");
        window.handleEscapeShortcut();check(CScene::getInstance()->getMode()==eEditModeObjects,"second Esc returns to objects");
        view.m_landscapeTools->activateMode(ELandscapeEditMode::TilePaint,false);view.setLandscapeEditingEnabled(true);
        check(view.m_pOp->operationMethod()==eOperationMethodTileBrush,"tile paint active before Esc");
        window.handleEscapeShortcut();check(!view.m_landscapeTools->editingEnabled()&&view.m_pOp->operationMethod()==eOperationMethodSelect,"one Esc exits persistent tile paint");
        view.m_landscapeTools->activateMode(ELandscapeEditMode::Sculpt,false);view.setLandscapeEditingEnabled(true);window.on_actionChange_mod_e_triggered();
        check(CScene::getInstance()->getMode()==eEditModeLogic&&!view.m_landscapeTools->editingEnabled(),"explicit behavior activation closes terrain tool");
        window.handleEscapeShortcut();
        const auto ground=screen({16,16,256.004f});bool hit=false;const auto point=view.getTerrainPos(ground.x(),ground.y(),true,&hit);
        check(hit&&point.z()>100,"actual terrain query hits high surface");
        QMouseEvent move(QEvent::MouseMove,ground,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&move);
        check(!coordinates->text().isEmpty()&&coordinates->text()!=QStringLiteral("not defined"),"high surface mouse coordinates displayed");
        const QPoint sky(1,1);view.getTerrainPos(sky.x(),sky.y(),true,&hit);check(!hit,"control ray misses surface");
        QMouseEvent miss(QEvent::MouseMove,sky,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&miss);
        check(coordinates->text()==QStringLiteral("not defined"),"miss has no fabricated coordinates");
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"ERROR "<<error<<"\nRESULT "<<passed<<" PASS 1 FAIL\n";out.flush();return 1;}
}

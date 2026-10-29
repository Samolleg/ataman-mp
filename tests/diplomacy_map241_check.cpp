#include <QtWidgets>
#include <QtTest>
#include <QOpenGLTexture>
#include <QOpenGLFramebufferObject>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "unit.h"
#include "worldobj.h"
#include "mob.h"
#include "camera.h"
#include "property.h"
#include "scene.h"
#include "operationmanager.h"
#include "prepared_static_visual.h"
#include "diplomacy_colors.h"
#include "diplomacy_dialog.h"
#include "resource_setup_dialog.h"

namespace {
struct Hidden : QObject {
    bool eventFilter(QObject* o,QEvent* e) override {
        if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o)) if(w->isWindow()) {
                w->setAttribute(Qt::WA_DontShowOnScreen); w->setAttribute(Qt::WA_ShowWithoutActivating);
            }
        return false;
    }
};
void write(const QString& path,const QByteArray& data) {
    QFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size()) throw path;
}
}
int runAssemblyCatalogCheck()
{
    QTextStream out(stdout); out.setCodec("UTF-8"); int passed=0;
    auto check=[&](bool ok,const QString& name) { out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passed; };
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_DIPLOMACY_ROOT"));
        check(!root.isEmpty() && QDir(root).exists(),"explicit owned output");
        Hidden hidden; qApp->installEventFilter(&hidden); qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup; QObject::connect(&setup,&QTimer::timeout,[] {
            for(auto* w:QApplication::topLevelWidgets()) if(auto* d=qobject_cast<CResourceSetupDialog*>(w)) d->reject();
        }); setup.start(10);
        MainWindow window; setup.stop(); window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop(); window.m_minimapRefreshTimer->stop();
        window.resize(1500,980); window.show(); auto& view=*window.view(); QTest::qWait(80); view.makeCurrent(); view.suspendRendering();
        check(view.isValid() && window.testAttribute(Qt::WA_DontShowOnScreen) && !window.isActiveWindow(),"native production viewport stays invisible");
        CMprDocument source; CValidationReport report;
        check(source.load(QFileInfo(qEnvironmentVariable("EI_DIPLOMACY_MPR_SOURCE")),&report),"read-only MPR template");
        SMprCreationRequest request; request.innerNamespace="diplomacy241"; request.maxZ=256;
        request.flatRawHeight=32768; request.sectorCountX=request.sectorCountY=1;
        QByteArray plane; check(source.buildNewCanvas(request,&plane,&report),"owned flat map for real picking/depth");
        const QString path=root+"/diplomacy241.mpr"; write(path,plane); view.m_pLand->readMap(QFileInfo(path),false);
        view.m_neighborLandscapesVisible=false; view.m_landscapeVisible=true; view.m_bDrawLand=true; view.m_bDrawWater=false;
        auto* main=new CMob; main->attach(&view,nullptr); main->setFileName(QFileInfo(root+"/first.mob"));
        auto* other=new CMob; other->attach(&view,nullptr); other->setFileName(QFileInfo(root+"/second.mob"));
        view.m_aMob.append(main); view.m_aMob.append(other); view.m_activeMob=main; view.m_environmentMob=main;
        QImage gray(8,8,QImage::Format_RGBA8888); gray.fill(QColor(180,180,180)); QOpenGLTexture texture(gray);
        SPreparedStaticPart part; part.name="synthetic-npc";
        const QVector3D positions[]={{-1.3f,0,0},{1.3f,0,0},{1.3f,0,4},{-1.3f,0,0},{1.3f,0,4},{-1.3f,0,4}};
        for(const auto& p:positions) { SVertexData v; v.position=p; v.normal={0,-1,0}; v.texCoord={.5f,.5f}; part.vertices.append(v); }
        QVector<CUnit*> units;
        for(int i=0;i<3;++i) {
            auto* unit=new CUnit; unit->setMapId(42); QVector3D p(9+i*7,16,0.01f); unit->setPos(p); unit->setDrawPosition(p+QVector3D(0,0,128));
            QSharedPointer<IPropertyBase> player(new propChar(eObjParam_PLAYER,char(i+1))); unit->applyParam(player);
            unit->setTexture(&texture); unit->installPreparedStaticParts({part}); (i==2?other:main)->addNode(unit); units.append(unit);
        }
        auto* scenery=new CWorldObj; scenery->setMapId(43); QVector3D p(16,23,128.01f); scenery->setPos(p); scenery->setDrawPosition(p);
        scenery->setTexture(&texture); scenery->installPreparedStaticParts({part}); main->addNode(scenery);
        QVector<QVector<uint>> table(32,QVector<uint>(32,1));
        for(int g=0;g<32;++g) table[g][g]=0;
        table[1][3]=2; table[2][1]=2;
        main->setDiplomacyField(table); other->setDiplomacyField(table);
        main->captureCleanProjection(); other->captureCleanProjection(); main->setDirty(false); other->setDirty(false);
        view.m_spatialIndexDirty=true; view.collectObjectTreeData();
        const QVector3D eye(16,16,164),pivot(16,16,128),rot(-45,0,0);
        view.m_cam->setLocation(eye,pivot,rot); view.m_normalProjection.setToIdentity();
        view.m_normalProjection.perspective(45,float(view.width())/view.height(),1,1000); view.updateCameraProjection();
        auto screen=[&](CNode* node) { const auto pt=(node->drawPosition()+QVector3D(0,0,2)).project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height())); return QPoint(qRound(pt.x()),view.height()-1-qRound(pt.y())); };
        auto frame=[&](const QString& name) {
            view.makeCurrent(); QOpenGLFramebufferObjectFormat format; format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            QOpenGLFramebufferObject fbo(view.size(),format); if(!fbo.isValid()) throw QString("capture framebuffer");
            view.m_snapshotCaptureActive=true; view.m_snapshotFramebuffer=fbo.handle(); view.m_snapshotSize=view.size();
            fbo.bind(); view.draw(); view.drawTestOverlaysOpenGl(); const auto image=fbo.toImage();
            view.m_snapshotCaptureActive=false; view.m_snapshotFramebuffer=0; fbo.release();
            if(!name.isEmpty()) check(image.save(root+"/"+name+".png"),"viewport capture "+name); return image;
        };
        auto click=[&](const QPoint& at) { QMouseEvent press(QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(&view,&press); QMouseEvent release(QEvent::MouseButtonRelease,at,Qt::LeftButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(&view,&release); };
        const auto before=main->parameterState(),beforeOther=other->parameterState();
        // First frame resolves the deliberately absent prototype to its fallback.
        // Install owned geometry after that resolution; subsequent frames use the cache.
        for(auto* unit:units) { unit->preloadVisualResources(); unit->setTexture(&texture); unit->installPreparedStaticParts({part}); }
        view.m_spatialIndexDirty=true;
        const auto normal=frame("normal"); view.setDiplomacyMapMode(true); const auto grouped=frame("groups");
        out<<"MODE different="<<(normal!=grouped)<<" dirty="<<main->isDirty()<<","<<other->isDirty()<<"\n";
        check(normal!=grouped && !main->isDirty() && !other->isDirty(),"map mode changes actual scene pixels without dirtying MOBs");
        const QString groupLegend=view.findChild<QLabel*>("diplomacyMapLegend")->text();
        check(groupLegend.contains("● 1") && groupLegend.contains("● 2") && groupLegend.contains("● 3"),"map legend identifies every visible group by color and number");
        const auto sceneryPoint=screen(scenery);
        check(normal.pixelColor(sceneryPoint)==grouped.pixelColor(sceneryPoint),"ordinary scenery keeps its rendered color");
        click(screen(units[0])); const auto relations=frame("relations");
        check(view.m_diplomacyAnchor==units[0] && view.selectionSnapshot()==QList<CNode*>{units[0]},"real viewport mouse click selects diplomatic anchor");
        const auto green=relations.pixelColor(screen(units[0])),yellow=relations.pixelColor(screen(units[1])),red=relations.pixelColor(screen(units[2]));
        out<<"PIXELS "<<green.name()<<" "<<yellow.name()<<" "<<red.name()<<"\n";
        check(green.green()>green.red()*1.4 && yellow.red()>yellow.blue()*1.6 && yellow.green()>yellow.blue()*1.6 && red.red()>red.green()*1.5,
              "production CView/CUnit/queue/shader show green ally, yellow neutral, red enemy across two MOBs");
        click(screen(units[2])); check(view.m_diplomacyAnchor==units[2] && view.currentMob()==main,"locked second MOB NPC is pickable without changing active MOB");
        const auto position=units[1]->position(); view.changeOperation(EButtonOpMove); const QPoint at=screen(units[1]);
        QMouseEvent press(QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(&view,&press);
        QMouseEvent move(QEvent::MouseMove,at+QPoint(45,20),Qt::NoButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(&view,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,at+QPoint(45,20),Qt::LeftButton,Qt::NoButton,Qt::NoModifier); QApplication::sendEvent(&view,&release);
        check(units[1]->position()==position && view.m_diplomacyAnchor==units[1],"diplomacy click/drag cannot accidentally move NPC even with Move tool selected");
        const QString otherSource=other->filePath().absoluteFilePath();
        check(view.focusDiplomacyNpc(otherSource,QString("node:%1").arg(units[2]->innerId())),"precise source/internal key focus");
        QVector3D actualEye,actualPivot,actualRot; view.m_cam->getLocation(actualEye,actualPivot,actualRot);
        check((actualPivot-units[2]->drawPosition()).length()<.001f && actualRot==rot,"navigation centers camera on exact NPC and preserves viewing angle");
        view.m_cam->setLocation(eye,pivot,rot); view.clearDiplomacyAnchor();
        click(QPoint(2,view.height()-2)); check(view.m_diplomacyAnchor.isNull(),"empty viewport click returns to group colors");
        const QString firstSource=main->filePath().absoluteFilePath(); view.setDiplomacyAnchor(units[0]);
        check(view.editDiplomacyRelation(firstSource,1,3,0,false,2,1),"relation editor command accepted");
        const auto edited=frame("edited"); const auto nowAlly=edited.pixelColor(screen(units[2]));
        check(nowAlly.green()>nowAlly.red()*1.4,"relation edit immediately recolors actual second-MOB model");
        window.m_undoStack->undo();
        check(view.diplomacyNpcColor(units[2])==diplomacy::relationColor(2),"Undo returns map relation to red");
        view.setDiplomacyMapMode(false); view.restoreSelection({});
        check(main->parameterState()==before && other->parameterState()==beforeOther,"inspection/navigation and undone edit preserve complete MOB parameters");
        const auto restored=frame("restored");
        for(auto* unit:units) check(normal.pixelColor(screen(unit))==restored.pixelColor(screen(unit)),"mode off restores ordinary NPC pixels");
        view.setDiplomacyMapMode(true); view.setDiplomacyAnchor(units[2]);
        units[2]->markAsDeleted(); view.updateDiplomacyOverlay();
        check(view.m_diplomacyAnchor.isNull(),"deleted anchor cannot leave stale relations");
        units[2]->markAsDeleted(false); window.m_undoStack->clear();
        view.setDiplomacyMapMode(false); view.m_aMob.clear(); view.m_activeMob=view.m_environmentMob=nullptr;
        delete main; delete other; window.hide(); qApp->removeEventFilter(&hidden);
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n"; return 0;
    } catch(const QString& error) { out<<"ERROR "<<error<<"\nRESULT "<<passed<<" PASS 1 FAIL\n";return 1; }
}

#include "world_map_snap_check.h"
#include "view.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "landscape.h"
#include "camera.h"
#include "operationmanager.h"
#include "endurance_document.h"
#include "world_map_layout_document.h"
#include "world_map_layout_command.h"
#include "main_window.h"
#include "resource_setup_dialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QToolButton>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QTextStream>
#include <QFile>
#include <QCryptographicHash>
#include <QTimer>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QThread>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>

namespace {
class HiddenSnapWindows : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
}

// Hidden native viewport; only owned synthetic MPRs and layout JSON are written.
int runWorldMapSnapCheck()
{
    QTextStream out(stdout);int failures=0;
    const auto check=[&](bool pass,const char* label) {
        out<<(pass?"PASS: ":"FAIL: ")<<label<<"\n";out.flush();if(!pass) ++failures;
    };
    QTemporaryDir scratch;if(!scratch.isValid()) return 2;
    CMprDocument source;CValidationReport validation;
    if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&validation)) return 2;
    SMprCreationRequest request;request.innerNamespace="snap118";request.maxZ=100;
    request.flatRawHeight=10000;request.sectorCountX=request.sectorCountY=1;
    QByteArray terrain;if(!source.buildNewCanvas(request,&terrain,&validation)) return 2;
    QVector<SWorldMapZoneOrigin> maps;
    const QPointF anchor(0.25,-0.5);
    for(const QString& id:{QStringLiteral("a"),QStringLiteral("b"),QStringLiteral("c")}) {
        const auto path=scratch.filePath(id+".mpr");QFile file(path);
        if(!file.open(QIODevice::WriteOnly)||file.write(terrain)!=terrain.size()) return 2;
        SWorldMapZoneOrigin map;map.profileId="p";map.allodId="w";map.zoneId=id;
        map.componentAnchorZoneId="a";map.mprIdentity=id;map.mprPath=id+".mpr";
        map.sectorWidth=map.sectorHeight=1;
        map.origin=id=="a"?anchor:(id=="b"?QPointF(32.25,0.25):QPointF(0.25,-32.25));maps.append(map);
    }
    const auto digest=[](const QString& path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return QByteArray();return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256);};
    const auto terrainHash=digest(scratch.filePath("a.mpr"));
    CWorldMapLayoutDocument doc(scratch.filePath("world.json"));QString error;
    if(!doc.addEditorMaps(maps,&error)) {out<<error<<"\n";return 2;}
    const auto originalLayout=digest(doc.path());
    CSettings settings;QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);
    CProgressView progress;QLineEdit coordinates;CTileForm tiles;CLandscapeTools tools;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);view.attachLandscapeTools(&tools);
    view.resize(800,600);view.show();QApplication::processEvents();view.makeCurrent();
    check(view.isValid(),"hidden native OpenGL context");if(!view.isValid()) return 1;
    view.m_pLand->readMap(QFileInfo(scratch.filePath("a.mpr")),false);
    QVector<SMapCatalogEntry> topology;
    for(const auto& map:maps) {
        SMapCatalogEntry entry;entry.zoneId=map.zoneId;entry.allodId=map.allodId;entry.mprName=map.mprIdentity;
        entry.mprPath=scratch.filePath(map.mprPath);entry.sectorWidth=entry.sectorHeight=1;topology.append(entry);
    }
    view.m_neighborLandscapesVisualToggle->setChecked(false);
    view.setNeighborMapContext(topology,maps,"p","w","a");
    view.updateVisualChannelAvailability();
    view.m_neighborLayoutEditToggle->setChecked(true);
    QElapsedTimer backgroundLoad;backgroundLoad.start();
    while(view.m_neighborLandscapes.size()!=2 && backgroundLoad.elapsed()<5000) {
        QApplication::processEvents();QThread::msleep(5);
    }
    check(view.m_worldTileSnapToggle&&view.m_worldTileSnapToggle->isChecked()
          &&view.m_neighborLayoutEditEnabled,"default tile checkbox and real arrangement toggle");
    view.m_cam->setLocation({48,16,130},{48,16,0},{0,0,0});
    QVector3D surface(16,16,0);view.m_pLand->projectPt(surface,true);
    check(view.m_pLand->isMprLoad()&&view.m_pLand->widthInWorldUnits()==32
          &&view.m_neighborLandscapes.size()==2&&view.m_neighborLandscapes[0].landscape->isMprLoad(),"synthetic active and background MPRs loaded");
    const auto screen=[&](QVector3D p) {const auto q=p.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,view.width(),view.height()));return QPoint(qRound(q.x()),qRound(view.height()-q.y()));};
    const auto mouse=[&](QEvent::Type type,QPoint p,Qt::MouseButton button,Qt::MouseButtons held) {
        QMouseEvent event(type,p,button,held,Qt::NoModifier);
        if(type==QEvent::MouseButtonPress)view.mousePressEvent(&event);
        else if(type==QEvent::MouseButtonRelease)view.mouseReleaseEvent(&event);
        else view.mouseMoveEvent(&event);
    };
    const auto origin=[&](const QString& id) {const auto records=doc.loadEditorWorld("p",{});const auto* map=CWorldMapLayout::zone(records.zones,"p","w",id);return map?map->origin:QPointF(999,999);};
    const auto push=[&](const QVector<SWorldMapLayoutChange>& changes) {
        history.push(new CWorldMapLayoutCommand("layout",[&doc,&view,changes](bool forward,QString* error) {
            if(!doc.applyChanges(changes,forward,error)) return false;
            QMap<QString,QPointF> positions;for(const auto& map:doc.loadEditorWorld("p",{}).zones)positions[map.zoneId]=map.origin;
            view.previewWorldMapOrigins(positions);return true;
        },[&](const QString& why){out<<why<<"\n";++failures;}));
    };
    int commits=0;
    QObject::connect(&view,&CView::neighborMapOriginChangeRequested,[&](const QString& id,const QPointF& p) {
        const auto records=doc.loadEditorWorld("p",{});const auto* before=CWorldMapLayout::zone(records.zones,"p","w",id);
        if(!before){++failures;return;}SWorldMapLayoutChange change;change.before=*before;change.after=*before;
        change.after.origin=p;change.after.provenance=EWorldMapOriginProvenance::Manual;push({change});++commits;
    });
    const QPoint start=screen({48,16.75f,surface.z()}),end=screen({48.7f,19.8f,surface.z()});
    SNeighborMapPlacement probe;QVector3D hit;QPointF rayPoint;
    out<<"viewport="<<view.width()<<","<<view.height()<<" z="<<surface.z()
       <<" start="<<start.x()<<","<<start.y()<<" end="<<end.x()<<","<<end.y()
       <<" hit="<<view.neighborMapAtViewport(start,&probe,&hit,true)<<" zone="<<probe.zoneId
       <<" plane="<<view.viewportPointOnHorizontalPlane(start,surface.z(),&rayPoint)
       <<" local="<<rayPoint.x()<<","<<rayPoint.y()<<"\n";out.flush();
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    check(view.m_neighborLayoutDragActive&&view.m_neighborLayoutDragZoneId=="b","real LMB hit starts neighbor drag");
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    check(view.m_neighborLayoutDragValid&&CWorldMapSnap::onGrid(view.m_neighborLayoutDragWorldOrigin,anchor)
          &&!view.m_worldMapSnapPreview.seams.isEmpty(),"drag preview aligns along seam on fractional common grid");
    check(digest(doc.path())==originalLayout&&history.count()==0,"drag preview does not save JSON or history");
    // A hidden native window need not resize or swap its default framebuffer.
    // Render the production overlay into an owned target of a known size.
    view.makeCurrent();QOpenGLFramebufferObject target(view.size(),QOpenGLFramebufferObject::CombinedDepthStencil);
    target.bind();auto* gl=QOpenGLContext::currentContext()->functions();
    gl->glViewport(0,0,view.width(),view.height());gl->glClearColor(0,0,0,1);
    gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);view.drawTestOverlaysOpenGl();
    const QImage frame=target.toImage();QOpenGLFramebufferObject::bindDefault();
    int greenPixels=0;
    for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x) {
        const auto color=frame.pixelColor(x,y);
        if(color.green()>240 && color.red()>40 && color.red()<65 && color.blue()>75 && color.blue()<105)++greenPixels;
    }
    check(!frame.isNull()&&greenPixels>10,"compatible seam is drawn green by native OpenGL");
    frame.save(QDir::current().filePath("snap-preview.png"));
    const auto preview=view.m_neighborLayoutDragWorldOrigin;
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(commits==1&&history.count()==1&&origin("b")==preview,"release commits snapped origin once");
    history.undo();check(origin("b")==maps[1].origin,"native drag Undo restores exact legacy origin");
    history.redo();check(origin("b")==preview,"native drag Redo restores snapped origin");history.undo();history.clear();
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);
    check(!view.m_neighborLayoutDragActive&&origin("b")==maps[1].origin&&history.count()==0
          &&commits==1,"Esc cancels dragged preview without saving");
    view.m_worldTileSnapToggle->setChecked(false);
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    const auto free=view.m_neighborLayoutDragWorldOrigin;
    check(view.m_neighborLayoutDragActive&&view.m_neighborLayoutDragValid
          &&!CWorldMapSnap::onGrid(free,anchor),"unchecked setting permits actual fractional free drag");
    view.cancelNeighborLayoutDrag(true);view.m_worldTileSnapToggle->setChecked(true);
    view.beginWorldMapPlacement({32,32},anchor,true);
    check(qAbs(view.m_worldPlacementZ-surface.z())<0.001f,"new placement derives plane from visible neighbor terrain");
    const auto place=screen({32.7f,3.8f,surface.z()});mouse(QEvent::MouseMove,place,Qt::NoButton,Qt::NoButton);
    check(view.m_worldPlacementValid&&view.m_worldPlacementOrigin==preview,"new placement uses the same grid and magnet as drag");
    int placements=0;QPointF confirmed;
    QObject::connect(&view,&CView::worldMapPlacementConfirmed,[&](QPointF p){++placements;confirmed=p;});
    mouse(QEvent::MouseButtonPress,place,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,place,Qt::LeftButton,Qt::NoButton);
    check(placements==1&&confirmed==preview&&!view.m_worldPlacementActive,"real placement click emits snapped coordinate once");
    view.beginWorldMapPlacement({32,32},anchor,true);QApplication::sendEvent(&view,&escape);
    check(!view.m_worldPlacementActive&&placements==1,"new placement Esc does not add a map");
    QString navigationZone;QPointF navigationSource,navigationTarget;
    QObject::connect(&view,&CView::neighborMapOpenRequested,[&](const QString&,const QString&,const QString& zone,const QPointF& source,const QPointF& target) {
        navigationZone=zone;navigationSource=source;navigationTarget=target;
    });
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,start,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    view.mouseDoubleClickEvent(&doubleClick);
    check(navigationZone=="b"&&navigationSource==anchor&&navigationTarget==maps[1].origin,
          "double click outside arrangement retains navigation and exact world origins");
    bool alignRequested=false;QObject::connect(&view,&CView::worldMapAlignmentRequested,[&](){alignRequested=true;});
    view.findChild<QToolButton*>("alignWorldMapTiles")->click();check(alignRequested,"native alignment button requests editor action");
    QMap<QString,QPointF> original,aligned;QVector<SWorldMapLayoutChange> group;
    for(const auto& map:maps) {
        original[map.zoneId]=map.origin;aligned[map.zoneId]=CWorldMapSnap::align(map.origin,anchor);
        if(map.origin!=aligned[map.zoneId]){SWorldMapLayoutChange change;change.before=map;change.after=map;change.after.origin=aligned[map.zoneId];group.append(change);}
    }
    view.previewWorldMapOrigins(aligned);
    bool alignedScene=true;for(const auto& loaded:view.m_neighborLandscapes) alignedScene&=loaded.placement.worldOrigin==aligned[loaded.placement.zoneId];
    check(alignedScene&&origin("b")==maps[1].origin,"group preview moves scene but leaves persistent coordinates unchanged");
    view.previewWorldMapOrigins(original);push(group);
    check(history.count()==1&&origin("b")==aligned["b"]&&origin("c")==aligned["c"],"group alignment commits all origins as one history command");
    history.undo();check(origin("b")==maps[1].origin&&origin("c")==maps[2].origin,"one Undo restores whole group");
    history.redo();CWorldMapLayoutDocument reopened(doc.path());const auto reopenedMaps=reopened.loadEditorWorld("p",{});
    bool persisted=true;for(const auto& map:reopenedMaps.zones)persisted&=CWorldMapSnap::onGrid(map.origin,anchor);
    check(persisted,"fresh layout reopen retains common grid after Redo");
    check(digest(scratch.filePath("a.mpr"))==terrainHash&&digest(scratch.filePath("b.mpr"))==terrainHash
          &&digest(scratch.filePath("c.mpr"))==terrainHash,"all MPR bytes stay unchanged through layout operations");
    history.clear();view.clearNeighborLandscapes();view.m_pLand->unloadMpr();
    // Exercise the production alignment dialog and MainWindow history hook.
    {
        HiddenSnapWindows hidden;qApp->installEventFilter(&hidden);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* dialog=qobject_cast<CResourceSetupDialog*>(widget))dialog->reject();
        });setup.start(1);qApp->setProperty("workspaceLoadSmoke",true);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);setup.stop();
        QDir().mkpath(scratch.filePath("Res"));QDir().mkpath(scratch.filePath("Maps"));
        for(const QString& relative:{QStringLiteral("game.exe"),QStringLiteral("Res/synthetic.res")}) {
            QFile file(scratch.filePath(relative));file.open(QIODevice::WriteOnly);file.write("owned placeholder, never executed");
        }
        dynamic_cast<COptString*>(window.m_settings->opt(eOptSetGeneral,"gameRoot"))->setValue(scratch.path());
        dynamic_cast<COptString*>(window.m_settings->opt(eOptSetResource,"resourceProfile"))->setValue("base-game");
        auto editorMaps=maps;for(auto& map:editorMaps)map.profileId="base-game";
        window.m_worldMapLayoutDocument.setProfileDirectory(scratch.path(),"base-game",{});
        check(window.m_worldMapLayoutDocument.addEditorMaps(editorMaps,&error),"owned profile world prepared for production alignment dialog");
        window.m_pView->m_neighborLandscapesVisible=false;
        window.m_pView->m_pLand->readMap(QFileInfo(scratch.filePath("a.mpr")),false);
        window.refreshEditorWorld();QElapsedTimer metadata;metadata.start();
        while(window.m_worldMetadataWatcher && metadata.elapsed()<5000) {QApplication::processEvents();QThread::msleep(5);}
        check(window.m_editorActiveMap.zoneId=="a"&&window.m_editorWorldMaps.size()==3,"production world resolves synthetic maps and permanent anchor");
        const auto editorDigest=digest(window.m_worldMapLayoutDocument.path());
        bool dialogDriven=false;
        QTimer driver;QObject::connect(&driver,&QTimer::timeout,[&](){
            for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QDialog*>(widget)) {
                auto* table=dialog->findChild<QTableWidget*>("worldTileAlignmentTable");if(!table)continue;
                driver.stop();dialogDriven=true;
                check(table->rowCount()==3&&!(table->item(0,0)->flags()&Qt::ItemIsUserCheckable),"native alignment table keeps anchor fixed");
                check(window.m_pView->m_neighborPlacements[0].worldOrigin==aligned[window.m_pView->m_neighborPlacements[0].zoneId]
                      &&digest(window.m_worldMapLayoutDocument.path())==editorDigest,"dialog checked preview changes scene only");
                table->item(1,0)->setCheckState(Qt::Unchecked);
                auto* buttons=dialog->findChild<QDialogButtonBox*>();
                check(buttons->button(QDialogButtonBox::Apply)->isEnabled(),"unchecking one map retains selectable group preview");
                QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(dialog,&escape);
            }
        });driver.start(5);window.alignWorldMapTiles();driver.stop();
        check(dialogDriven&&window.m_undoStack->count()==0&&digest(window.m_worldMapLayoutDocument.path())==editorDigest
              &&window.m_pView->m_activeMapWorldOrigin==anchor,"real dialog Esc restores scene with no save/history");
        dialogDriven=false;QObject::disconnect(&driver,nullptr,nullptr,nullptr);
        QObject::connect(&driver,&QTimer::timeout,[&](){
            for(auto* widget:QApplication::topLevelWidgets()) if(auto* dialog=qobject_cast<QDialog*>(widget)) {
                if(!dialog->findChild<QTableWidget*>("worldTileAlignmentTable"))continue;
                driver.stop();dialogDriven=true;dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
            }
        });driver.start(5);window.alignWorldMapTiles();driver.stop();
        const auto editorOrigin=[&](const QString& id) {const auto records=window.m_worldMapLayoutDocument.loadEditorWorld("base-game",{});return CWorldMapLayout::zone(records.zones,"base-game","w",id)->origin;};
        check(dialogDriven&&window.m_undoStack->count()==1&&editorOrigin("b")==aligned["b"]
              &&editorOrigin("c")==aligned["c"],"native Apply uses production atomic group history hook");
        window.m_undoStack->undo();check(editorOrigin("b")==maps[1].origin&&editorOrigin("c")==maps[2].origin,"production group Undo exact");
        window.m_undoStack->redo();check(editorOrigin("b")==aligned["b"]&&editorOrigin("c")==aligned["c"],"production group Redo exact");
        window.m_pView->m_pLand->unloadMpr();window.m_undoStack->clear();qApp->removeEventFilter(&hidden);
    }
    out<<"World map snap failures="<<failures<<"\n";out.flush();return failures?1:0;
}

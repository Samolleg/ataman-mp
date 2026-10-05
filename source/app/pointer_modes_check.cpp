#include "pointer_modes_check.h"
#include "view.h"
#include "mob.h"
#include "worldobj.h"
#include "scene.h"
#include "operationmanager.h"
#include "camera.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "landscape.h"
#include "tile.h"
#include "endurance_document.h"
#include <QApplication>
#include <QLineEdit>
#include <QTextStream>
#include <QUndoStack>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QFile>
#include <QFocusEvent>

int runPointerModesCheck()
{
    QTextStream out(stdout);int failed=0;
    auto check=[&](bool ok,const char* name){out<<(ok?"PASS: ":"FAIL: ")<<name<<"\n";out.flush();if(!ok)++failed;};
    QTemporaryDir temp;if(!temp.isValid())return 2;
    CSettings settings;QUndoStack history;CTreeView tree;QTableWidget props;props.setColumnCount(2);
    CProgressView progress;QLineEdit coords;CTileForm tiles;CLandscapeTools tools;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&props,&history,&progress,&coords,&tree,&tiles);view.attachLandscapeTools(&tools);
    {QSignalBlocker block(CScene::getInstance());CScene::getInstance()->changeMode(eEditModeObjects);}
    auto* mob=new CMob;mob->setFileName(QFileInfo(temp.filePath("fixture.mob")));
    auto* a=new CWorldObj;auto* b=new CWorldObj;a->setMapId(101);b->setMapId(102);
    mob->nodes()<<a<<b;view.m_aMob<<mob;view.m_activeMob=mob;
    QVector3D pa(10,10,0),pb(14,10,0);
    auto reset=[&](){view.cancelPendingOperation();view.changeOperation(EButtonOpSelect);
        a->updatePos(pa);b->updatePos(pb);a->setState(eSelect);b->setState(eSelect);};
    auto selected=[&](){return a->nodeState()==eSelect&&b->nodeState()==eSelect;};
    auto mouse=[&](QEvent::Type type,QPoint pos,Qt::MouseButton button,Qt::MouseButtons buttons){
        QMouseEvent event(type,pos,button,buttons,Qt::NoModifier);
        if(type==QEvent::MouseButtonPress)view.mousePressEvent(&event);
        else if(type==QEvent::MouseButtonRelease)view.mouseReleaseEvent(&event);
        else view.mouseMoveEvent(&event);
    };
    const QPoint start(320,240),end(380,300),miss(600,400);
    auto* locked=new CMob;locked->setFileName(QFileInfo(temp.filePath("locked.mob")));
    auto* other=new CWorldObj;other->setMapId(501);locked->nodes()<<other;view.m_aMob<<locked;
    a->setState(eDraw);b->setState(eDraw);other->setState(eSelect);
    QString message;QObject::connect(&view,&CView::updateMsg,[&](const QString& text){message=text;});
    for(EKeyCode key : {eKey_G,eKey_R}) {
        message.clear();view.m_pOp->keyPress(key);
        check(view.m_pOp->operationMethod()==eOperationMethodSelect&&view.m_operationBackup.isEmpty()
              &&!message.isEmpty()&&view.m_activeMob==mob&&other->nodeState()==eSelect,
              "locked owner rejects keyboard transform with feedback and unchanged document/selection");
    }
    other->setState(eDraw);reset();
    CMprDocument source;CValidationReport report;
    if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&report))return 2;
    SMprCreationRequest request;request.innerNamespace="pointer117";request.maxZ=100;
    request.flatRawHeight=10000;request.sectorCountX=2;request.sectorCountY=2;
    QByteArray bytes;if(!source.buildNewCanvas(request,&bytes,&report))return 2;
    const auto path=temp.filePath("pointer117.mpr");QFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return 2;file.close();
    view.resize(640,480);view.show();QApplication::processEvents();view.makeCurrent();
    view.m_pLand->readMap(QFileInfo(path),false);
    if(!view.m_mprDocument.loadBytes(bytes,path,&report)||!view.m_terrainEdit.load(view.m_mprDocument,&report))return 2;
    view.m_pPreviewTile.reset(new CPreviewTile);
    view.m_terrainEditAvailable=true;view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);view.m_worldTerrainEnabled=false;
    auto camera=[&](){view.m_cam->setLocation(QVector3D(32,32,80),QVector3D(32,32,0),QVector3D(0,0,0));};camera();
    for(auto mode : {ELandscapeEditMode::Sculpt,ELandscapeEditMode::OverlaySculpt,
                    ELandscapeEditMode::TilePaint,ELandscapeEditMode::OverlayPaint,ELandscapeEditMode::OverlaySurface}) {
        reset();tools.activateMode(mode);check(tools.editingEnabled(),"brush activation retains editing");
        view.m_lastViewportMousePos=start;view.m_hasViewportMousePos=true;
        view.changeOperation(EButtonOpMove);
        check(!tools.editingEnabled()&&view.m_operationBackup.size()==2,"move disables every landscape mode and owns both objects");
        mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
        const auto afterA=a->position(),afterB=b->position();
        check(afterA!=pa&&afterB-pb==afterA-pa&&!view.m_sculptStrokeActive,"LMB moves group instead of starting terrain stroke");
        mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
        check(view.m_pOp->operationMethod()==eOperationMethodSelect&&history.count()==1,"move applies as one history item");
        history.undo();check(a->position()==pa&&b->position()==pb,"group move undo exact");
        history.redo();check(a->position()==afterA&&b->position()==afterB,"group move redo exact");
        history.undo();history.clear();
    }
    reset();tools.activateMode(ELandscapeEditMode::Sculpt);
    view.m_lastViewportMousePos=start;view.m_pOp->keyPress(eKey_G);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::NoButton);view.m_pOp->keyPress(eKey_Esc);
    check(!tools.editingEnabled()&&a->position()==pa&&b->position()==pb&&history.count()==0,"G exits sculpt and Esc restores group without history");
    for(auto mode : {ELandscapeEditMode::Sculpt,ELandscapeEditMode::OverlaySculpt}) {
        reset();tools.activateMode(mode);
        const auto point=mode==ELandscapeEditMode::Sculpt?QPoint(-1000,-1000):miss;
        mouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);
        check(view.m_sculptGestureCaptured&&!view.m_sculptStrokeActive,"sculpt miss captures gesture without stroke");
        mouse(QEvent::MouseMove,point+QPoint(10,10),Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,point+QPoint(10,10),Qt::LeftButton,Qt::NoButton);
        check(selected()&&!view.m_sculptGestureCaptured&&view.m_landscapeGestureButtons==Qt::NoButton,"missed sculpt release preserves object selection");
    }
    reset();tools.activateMode(ELandscapeEditMode::Sculpt);
    const auto beforeHeight=view.m_terrainEdit.terrainSurfaceHeight(32,32);
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    check(view.m_sculptStrokeActive,"real ground sculpt starts stroke");
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected()&&!view.m_sculptStrokeActive&&history.count()==1,"ground sculpt preserves selection and commits terrain");
    history.undo();check(qAbs(view.m_terrainEdit.terrainSurfaceHeight(32,32)-beforeHeight)<0.0001f,"sculpt undo restores terrain");history.clear();
    reset();tools.activateMode(ELandscapeEditMode::OverlaySculpt);
    mouse(QEvent::MouseButtonPress,miss,Qt::LeftButton,Qt::LeftButton);
    view.changeOperation(EButtonOpSelect);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected()&&!tools.editingEnabled(),"switch to Select mid-brush does not steal release");
    // Positive control: an actual Select gesture still picks (fixture has no mesh).
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(!selected(),"Select-owned click/rectangle remains operational");
    reset();mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    QFocusEvent focus(QEvent::FocusOut);view.focusOutEvent(&focus);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected(),"focus loss cancels Select gesture without orphan selection");
    reset();tools.activateMode(ELandscapeEditMode::TilePaint);
    view.m_terrainTileInformationPolicy.rightPress(QPoint(1,1));
    mouse(QEvent::MouseButtonRelease,start,Qt::RightButton,Qt::NoButton);
    check(!view.m_terrainTileInformationPolicy.rightHeld()&&selected(),"paint RMB release ends tile-information gesture without selection");
    QVector<QImage> images;QVector<qint32> types;
    for(int i=0;i<64;++i){QImage image(32,32,QImage::Format_RGBA8888);image.fill(Qt::green);images<<image;types<<qint32(ETileType::eGrass);}
    check(tiles.adoptTileImages(images,types),"synthetic texture atlas adopted");
    tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);tiles.selectTile(17);
    auto packed=[&](){QVector<quint16> result;for(int y=0;y<32;++y)for(int x=0;x<32;++x)result<<view.m_terrainEdit.packedTile(x,y,ETerrainTileLayer::Land);return result;};
    // The positive Select control above intentionally recorded selection history.
    history.clear();
    const auto beforePaint=packed();
    mouse(QEvent::MouseMove,start,Qt::NoButton,Qt::NoButton);
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    const auto afterPaint=packed();
    out<<"paint selected="<<selected()<<" changed="<<(beforePaint!=afterPaint)<<" history="<<history.count()<<" index="<<history.index()<<"\n";out.flush();
    check(selected()&&beforePaint!=afterPaint&&history.count()==1,"actual textured tile stroke changes terrain only in one history item");
    history.undo();check(packed()==beforePaint,"paint undo exact");history.redo();check(packed()==afterPaint,"paint redo exact");history.undo();history.clear();
    reset();tools.activateMode(ELandscapeEditMode::OverlaySculpt);
    mouse(QEvent::MouseButtonPress,miss,Qt::LeftButton,Qt::LeftButton);
    tools.activateMode(ELandscapeEditMode::TilePaint);
    const auto beforeSwitch=packed();
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected()&&packed()==beforeSwitch,"switch between brushes cannot paint with abandoned sculpt gesture");
    reset();tools.activateMode(ELandscapeEditMode::OverlaySculpt);
    mouse(QEvent::MouseButtonPress,miss,Qt::LeftButton,Qt::LeftButton);
    view.m_lastViewportMousePos=start;view.changeOperation(EButtonOpMove);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected()&&a->position()==pa&&b->position()==pb
          &&view.m_pOp->operationMethod()==eOperationMethodMoveAxis,
          "brush to Move does not move or confirm from abandoned brush gesture");
    view.m_pOp->keyPress(eKey_Esc);
    reset();tools.activateMode(ELandscapeEditMode::TilePaint);
    mouse(QEvent::MouseButtonPress,start,Qt::RightButton,Qt::RightButton);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::RightButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::RightButton,Qt::NoButton);
    check(selected()&&!view.m_terrainTileInformationPolicy.rightHeld()
          &&view.m_pOp->operationMethod()==eOperationMethodTileBrush,
          "real RMB sampling retains brush and selection and releases tooltip");
    reset();tools.activateMode(ELandscapeEditMode::TilePaint);
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    view.focusOutEvent(&focus);
    const auto afterLostFocus=packed();
    const auto lostFocusHistory=history.count();
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected()&&packed()==afterLostFocus&&history.count()==lostFocusHistory,
          "paint focus loss cannot restart a cancelled stroke from orphan drag/release");
    history.clear();
    for(auto operation : {EButtonOpRotate,EButtonOpScale}) {
        reset();tools.activateMode(ELandscapeEditMode::Sculpt);
        view.changeOperation(operation);
        check(!tools.editingEnabled(),"rotate/scale also exit sculpt editing");
        view.m_pOp->keyPress(eKey_Esc);
    }
    reset();tools.activateMode(ELandscapeEditMode::Sculpt);view.m_terrainEditAvailable=false;
    mouse(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
    check(selected(),"unavailable terrain does not turn sculpt into selection");view.m_terrainEditAvailable=true;
    camera();mouse(QEvent::MouseButtonPress,start,Qt::MiddleButton,Qt::MiddleButton);
    mouse(QEvent::MouseMove,end,Qt::NoButton,Qt::MiddleButton);
    mouse(QEvent::MouseButtonRelease,end,Qt::MiddleButton,Qt::NoButton);
    check(selected()&&tools.editingEnabled()&&!view.m_sculptStrokeActive,"MMB camera navigation remains available during sculpt");
    history.clear();view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
    out<<"Pointer modes failures="<<failed<<"\n";out.flush();return failed?1:0;
}

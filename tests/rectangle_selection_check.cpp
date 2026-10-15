#include "view.h"
#include "camera.h"
#include <QGLFunctions>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include "landscape.h"
#include "prepared_static_visual.h"
#include "endurance_document.h"
#include "operationmanager.h"
#include "key_manager.h"
#include <QOpenGLFunctions>
#include "settings.h"
#include "tablemanager.h"
#include "tree_view.h"
#include "tile_form.h"
#include "progressview.h"
#include "scene.h"
#include "unit.h"
#include "light.h"
#include "magictrap.h"
#include "resourcemanager.h"
#include "utils.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QTextStream>
#include <QUndoStack>
#include <QLineEdit>

// Reuse the existing native integration entrypoint; no new production CLI.
int runNeighborPerformanceCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");
    QTemporaryDir scratch;if(!scratch.isValid())return 2;
    const QString oldDirectory=QDir::currentPath();
    struct Restore {QString path;~Restore(){QDir::setCurrent(path);}} restore{oldDirectory};
    QDir::setCurrent(scratch.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
    int passed=0,failed=0;
    const auto check=[&](bool ok,const char* label){out<<(ok?"PASS ":"FAIL ")<<label<<"\n";out.flush();ok?++passed:++failed;};
    CSettings settings;
    for(QString option:{QString("resourceRoots"),QString("mapRoots")})
        if(auto* roots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,option)))roots->setValue({});
    CTextureList::getInstance()->attachSettings(&settings);CObjectList::getInstance()->attachSettings(&settings);
    QUndoStack undo;CTreeView tree;QTableWidget actual;actual.setColumnCount(2);
    CProgressView progress;QLineEdit coordinates;CTileForm tiles;CView view(nullptr,nullptr);
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&actual,&undo,&progress,&coordinates,&tree,&tiles);
    view.resize(640,480);view.show();QCoreApplication::processEvents();
    check(view.isValid(),"hidden native GL context available for mouse selection");
    if(!view.isValid())return 3;

    CScene::getInstance()->changeMode(eEditModeObjects);
    QString culling="frustumCulling";dynamic_cast<COptBool*>(settings.opt(culling))->setValue(false);
    CMprDocument source;CValidationReport report;
    if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&report)){out<<report.toText();return 4;}
    SMprCreationRequest request;request.innerNamespace="select183";request.maxZ=100;
    request.flatRawHeight=0;request.sectorCountX=1;request.sectorCountY=1;
    QByteArray bytes;if(!source.buildNewCanvas(request,&bytes,&report)){out<<report.toText();return 5;}
    const QString path=scratch.filePath("select183.mpr");QFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return 6;file.close();
    view.makeCurrent();view.m_pLand->readMap(QFileInfo(path),false);
    check(view.m_pLand->isMprLoad(),"synthetic flat terrain loaded without recovery writes");
    view.m_landscapeVisible=true;view.m_bDrawLand=true;view.m_bDrawWater=false;
    view.m_cam->setLocation({16,16,35},{16,16,0},{-45,0,0});
    view.m_projection.setToIdentity();view.m_projection.perspective(45,640.f/480,1,1000);
    auto* mob=new CMob;view.m_aMob.append(mob);view.m_activeMob=mob;
    QImage image(8,8,QImage::Format_RGBA8888);image.fill(Qt::white);QOpenGLTexture texture(image);
    auto* object=new CObjectBase;object->setMapId(1);object->setTexture(&texture);object->setDrawPosition({16,16,0});
    SPreparedStaticPart part;part.name="buried-post";
    const QVector3D positions[]={{-3,0,-8},{3,0,-8},{3,0,6},{-3,0,-8},{3,0,6},{-3,0,6}};
    for(int i=0;i<6;++i){SVertexData v;v.position=positions[i];v.normal={0,-1,0};v.texCoord={.5f,.5f};part.vertices.append(v);}
    object->installPreparedStaticParts({part});mob->addNode(object);
    const auto screen=[&](QVector3D world){auto p=world.project(view.m_cam->viewMatrix(),view.m_projection,QRect(0,0,640,480));return QPoint(qRound(p.x()),479-qRound(p.y()));};
    const QPoint buried=screen({16,16,-4}),visible=screen({16,16,4});
    out<<"PIXELS buried "<<buried.x()<<","<<buried.y()<<" visible "<<visible.x()<<","<<visible.y()<<"\n";
    const auto rectAt=[](QPoint p){return QRect(p-QPoint(10,10),QSize(21,21));};
    const auto reference=[&](bool ground){
      view.beginEditorQueryFramebuffer();glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
      if(ground){
        view.m_landProgram.bind();view.m_landProgram.setUniformValue("u_projMmatrix",view.m_projection);
        view.m_landProgram.setUniformValue("u_viewMmatrix",view.m_cam->viewMatrix());
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);view.m_pLand->draw(&view.m_landProgram);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
      }
      view.m_selectProgram.bind();view.m_selectProgram.setUniformValue("u_projMmatrix",view.m_projection);
      view.m_selectProgram.setUniformValue("u_viewMmatrix",view.m_cam->viewMatrix());object->drawSelect(&view.m_selectProgram);
      QVector<SColor> colors;view.getColorFromRect(rectAt(buried),colors);view.endEditorQueryFramebuffer();return !colors.isEmpty();
    };
    check(reference(false),"control: buried mesh has picking pixels without ground");
    check(!reference(true),"visible terrain depth hides all buried mesh pixels in rectangle");
    const auto drag=[&](QRect rect){
      QMouseEvent press(QEvent::MouseButtonPress,rect.topLeft(),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&press);
      QMouseEvent move(QEvent::MouseMove,rect.bottomRight(),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&move);
      QMouseEvent release(QEvent::MouseButtonRelease,rect.bottomRight(),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&release);
    };
    drag(QRect(buried-QPoint(10,10),QSize(21,21)));
    check(view.selectionSnapshot().isEmpty(),"full mouse rectangle over ground does not select buried post outside visible footprint");
    view.clearObjectSelection();undo.clear();
    drag(QRect(visible-QPoint(10,10),QSize(21,21)));
    check(view.selectionSnapshot()==QList<CNode*>{object},"full mouse rectangle on exposed post selects it");

    undo.undo();check(view.selectionSnapshot().isEmpty(),"selection Undo clears exposed post");
    undo.redo();check(view.selectionSnapshot()==QList<CNode*>{object},"selection Redo restores exposed post");
    QList<CNode*> pool{object};
    check(view.pickObject(pool,buried.x(),buried.y())==nullptr,"point-query overload rejects buried geometry");
    check(view.pickObject(pool,visible.x(),visible.y())==object,"point-query overload retains visible geometry");
    view.pickObject(buried,false);check(view.selectionSnapshot().isEmpty(),"single click rejects buried geometry");
    view.pickObject(visible,false);check(view.selectionSnapshot()==QList<CNode*>{object},"single click retains exposed geometry");
    view.pickObject(rectAt(buried),true);check(view.selectionSnapshot()==QList<CNode*>{object},"Shift on buried geometry keeps existing selection unchanged");
    view.pickObject(rectAt(visible),true);check(view.selectionSnapshot().isEmpty(),"Shift toggles exposed selection off");
    view.m_landscapeVisible=false;
    view.pickObject(rectAt(buried),false);check(view.selectionSnapshot()==QList<CNode*>{object},"hidden landscape does not occlude selectable model");
    view.m_landscapeVisible=true;view.m_bDrawLand=false;
    view.pickObject(rectAt(buried),false);check(view.selectionSnapshot()==QList<CNode*>{object},"disabled ground pass does not occlude selectable model");
    view.m_bDrawLand=true;
    const QVector3D eye=view.m_cam->viewMatrix().inverted().map(QVector3D());
    const QVector3D under(16,16,-4);
    const QVector3D front=eye+(under-eye)*((2-eye.z())/(under.z()-eye.z()));
    auto* inside=new CObjectBase;inside->setMapId(2);inside->setTexture(&texture);inside->setDrawPosition(front);
    SPreparedStaticPart small;small.name="inside";
    for(int i=0;i<6;++i){auto v=part.vertices[i];v.position.setX(v.position.x()/12);v.position.setZ((v.position.z()+1)/14);small.vertices.append(v);}
    inside->installPreparedStaticParts({small});mob->addNode(inside);view.m_spatialIndexDirty=true;
    for(int corner=0;corner<4;++corner){
      const QRect r=rectAt(buried);const QPoint a=corner==0?r.topLeft():corner==1?r.topRight():corner==2?r.bottomLeft():r.bottomRight();
      const QPoint b=corner==0?r.bottomRight():corner==1?r.bottomLeft():corner==2?r.topRight():r.topLeft();
      QMouseEvent press(QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&press);
      QMouseEvent move(QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&move);
      QMouseEvent release(QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&release);
      check(view.selectionSnapshot()==QList<CNode*>{inside},"four mouse directions select inside object without buried outside neighbor");
    }
    inside->setState(eHidden);view.m_spatialIndexDirty=true;
    view.pickObject(rectAt(buried),false);check(view.selectionSnapshot().isEmpty(),"hidden inside object and buried neighbor both excluded");
    inside->setState(eDraw);inside->markAsDeleted(true);view.m_spatialIndexDirty=true;
    view.pickObject(rectAt(buried),false);check(view.selectionSnapshot().isEmpty(),"deleted inside object never leaks into selection");
    auto neighbor=CLandscape::createReadOnlyBackground();
    check(neighbor->readBackgroundMap(QFileInfo(path)),"neighbor terrain loads from same synthetic fixture");
    neighbor->setWorldOffset({32,0,0});CView::SLoadedNeighborLandscape background;background.landscape=neighbor;
    view.m_neighborLandscapes.append(background);view.m_neighborLandscapesVisible=true;
    object->setDrawPosition({48,16,0});view.m_spatialIndexDirty=true;
    view.m_cam->setLocation({48,16,35},{48,16,0},{-45,0,0});
    const QPoint neighborBuried=screen({48,16,-4}),neighborVisible=screen({48,16,4});
    view.pickObject(rectAt(neighborBuried),false);check(view.selectionSnapshot().isEmpty(),"translated visible neighbor terrain occludes buried model");
    view.pickObject(rectAt(neighborVisible),false);check(view.selectionSnapshot()==QList<CNode*>{object},"translated neighbor retains exposed model");
    view.m_neighborLandscapesVisible=false;
    view.pickObject(rectAt(neighborBuried),false);check(view.selectionSnapshot()==QList<CNode*>{object},"hidden neighbor terrain stops occluding model");

    view.m_neighborLandscapes.clear();view.m_neighborLandscapesVisible=true;
    object->setDrawPosition({16,16,0});inside->markAsDeleted(false);inside->setState(eDraw);view.m_spatialIndexDirty=true;
    view.m_cam->setLocation({16,16,35},{16,16,0},{-45,0,0});
    object->setAssemblyTag(QJsonObject{{"instanceId","selection-regression-group"}});
    inside->setAssemblyTag(QJsonObject{{"instanceId","selection-regression-group"}});
    view.clearObjectSelection();undo.clear();view.pickObject(rectAt(visible),false);
    check(view.selectionSnapshot().size()==2,"explicit assembly still expands a visible hit to both members");
    undo.undo();check(view.selectionSnapshot().isEmpty(),"assembly selection Undo clears full group");
    undo.redo();check(view.selectionSnapshot().size()==2,"assembly selection Redo restores full group");
    object->setAssemblyTag({});inside->setAssemblyTag({});
    view.clearObjectSelection();undo.clear();
    drag(rectAt(buried));
    check(view.selectionSnapshot()==QList<CNode*>{inside},"mixed rectangle adds inside model without outside post");
    undo.undo();check(view.selectionSnapshot().isEmpty(),"mixed rectangle Undo restores exact empty selection");
    undo.redo();check(view.selectionSnapshot()==QList<CNode*>{inside},"mixed rectangle Redo preserves only inside model");
    view.m_pOp->keyManager()->press(eKey_Shift);drag(rectAt(visible));view.m_pOp->keyManager()->release(eKey_Shift);
    check(view.selectionSnapshot().size()==2,"full mouse Shift rectangle adds exposed model");
    view.m_pOp->keyManager()->press(eKey_Shift);drag(rectAt(visible));view.m_pOp->keyManager()->release(eKey_Shift);
    check(view.selectionSnapshot()==QList<CNode*>{inside},"full mouse Shift rectangle removes exposed model");
    GLboolean mask[4];glGetBooleanv(GL_COLOR_WRITEMASK,mask);
    check(mask[0]&&mask[1]&&mask[2]&&mask[3],"depth prepass restores full color mask");
    check(glGetError()==GL_NO_ERROR,"OpenGL status after selection regression");
    out<<"TOTAL "<<passed<<" PASS "<<failed<<" FAIL\n";out.flush();return failed?1:0;
}

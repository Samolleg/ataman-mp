#include <QtWidgets>
#include <QtOpenGL>
#include "view.h"
#include "node.h"
#include "mob.h"
#include "lever.h"
#include "settings.h"
#include "resourcemanager.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "camera.h"
#include "database_document.h"
#include "lever_approach.h"
#define private public
#include "lever_approach_dialog.h"
#undef private

int runScenarioLayerCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passes;};
    try {
        QTemporaryDir scratch;check(scratch.isValid(),"private scratch");
        const QString old=QDir::currentPath();struct Restore{QString path;~Restore(){QDir::setCurrent(path);}} restore{old};
        QDir::setCurrent(scratch.path());QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
        auto* settings=new CSettings;
        dynamic_cast<COptString*>(settings->opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings->opt("gameRoot"))->setValue(qEnvironmentVariable("EI_LEVER_GAME"));
        const QStringList roots=qEnvironmentVariable("EI_LEVER_ROOTS").split(';',QString::SkipEmptyParts);
        dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"resourceRoots"))->setValue(roots.toVector());
        CTextureList::getInstance()->attachSettings(settings);
        auto* history=new QUndoStack;auto* table=new QTableWidget;table->setColumnCount(2);
        auto* view=new CView(nullptr,nullptr);view->setAttribute(Qt::WA_DontShowOnScreen);
        view->attach(settings,table,history,new CProgressView,new QLineEdit,new CTreeView,new CTileForm);
        view->resize(800,600);view->show();QApplication::processEvents();check(view->isValid(),"hidden OpenGL");
        view->makeCurrent();CTextureList::getInstance()->initResource();
        const QString map=scratch.filePath("test.mpr");check(QFile::copy(qEnvironmentVariable("EI_LEVER_MAP"),map),"isolated map copy");
        view->loadLandscape(QFileInfo(map),false);view->makeCurrent();
        ei::ProfileDatabaseService service;ei::SProfileDatabaseLoadRequest request;
        request.providerId="base.standard.v1";request.profileId="base";request.resourceDirectories=roots;
        const auto snapshot=service.snapshot(request);ei::ProfileDatabaseContext::instance().activate(snapshot);QString error;
        const auto document=ei::DatabaseDocument::fromSnapshot(snapshot,&error);check(bool(document),"active database: "+error);
        const auto* levers=document->table("levers.leverprototypes");check(levers&&!levers->rows.isEmpty(),"lever table");
        QString model;
        for(const auto& row:levers->rows){const auto a=QJsonDocument::fromJson(row.values.value("fld.1.0").toString().toUtf8()).array();
            bool finite=a.size()==4;for(const auto& v:a)finite=finite&&v.isDouble()&&std::isfinite(float(v.toDouble()));
            if(finite&&row.values.value("fld.3.0").toFloat()>0){model=row.values.value("fld.0.0").toString();break;}}
        check(!model.isEmpty(),"usable lever record");
        auto* mob=new CMob;mob->setFileName(QFileInfo(scratch.filePath("test.mob")));
        CLever defaults;auto data=defaults.toJson();auto world=data.value("World object").toObject();world["Model name"]=model;data["World object"]=world;
        auto* lever=new CLever(data);lever->setMapId(31);lever->setState(eSelect);
        QVector3D pos(32,32,0);lever->setPos(pos);lever->updatePos(pos);
        mob->nodes().append(lever);mob->captureCleanProjection();mob->setDirty(false);
        view->m_aMob.append(mob);view->m_activeMob=mob;
        view->m_cam->moveTo(lever->drawPosition());view->m_cam->moveAwayOn(20);view->viewParameters();view->updateGL();
        auto* button=table->findChild<QPushButton*>("leverApproachButton");check(button&&button->isEnabled(),"property button");
        int requested=0;QObject::connect(view,&CView::leverApproachRequested,view,[&]{++requested;});button->click();check(requested==1,"button signal route");
        const QByteArray before=document->buildArchive(&error);check(!before.isEmpty(),"baseline archive");
        auto* dialog=new ei::LeverApproachDialog(document,view,nullptr);dialog->setAttribute(Qt::WA_DontShowOnScreen);
        const bool opened=dialog->initialize(&error);check(opened,"open tool: "+error);dialog->show();
        dialog->m_count->setCurrentIndex(1);dialog->m_reach->setValue(.5);dialog->m_active->setCurrentIndex(1);
        QPoint pointer;QVector3D ground;bool picked=false;
        for(int y=170;y<450&&!picked;y+=25)for(int x=180;x<650&&!picked;x+=25){QVector2D local;
            if(view->terrainPositionAt(QPoint(x,y),&ground)&&ei::leverApproachLocal(QVector2D(ground),lever->drawPosition(),lever->rotation(),dialog->m_scale,.5,&local)){
                pointer=QPoint(x,y);picked=true;}}
        check(picked,"terrain pointer available");
        QMouseEvent press(QEvent::MouseButtonPress,pointer,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,pointer,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&release);
        const auto placed=ei::leverApproachWorld(dialog->m_points[1],lever->drawPosition(),lever->rotation(),dialog->m_scale,.5);
        check((QVector2D(placed)-QVector2D(ground)).length()<.001,"mouse maps to runtime XY");
        QMouseEvent dragPress(QEvent::MouseButtonPress,pointer,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&dragPress);
        const QPoint dragged=pointer+QPoint(12,9);QVector3D dragGround;
        check(view->terrainPositionAt(dragged,&dragGround),"drag terrain hit");
        QMouseEvent move(QEvent::MouseMove,dragged,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view,&move);
        QMouseEvent dragRelease(QEvent::MouseButtonRelease,dragged,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&dragRelease);
        check((QVector2D(ei::leverApproachWorld(dialog->m_points[1],lever->drawPosition(),lever->rotation(),dialog->m_scale,.5))-QVector2D(dragGround)).length()<.001,"drag updates same marker");
        check(!view->m_leverApproachLines.isEmpty(),"native GL marker lines");
        view->makeCurrent();view->resizeGL(view->width(),view->height());view->updateGL();
        QVector3D low=lever->drawPosition(),high=low;
        for(const auto& p:view->m_leverApproachLines){low.setX(qMin(low.x(),p.x()));low.setY(qMin(low.y(),p.y()));high.setX(qMax(high.x(),p.x()));high.setY(qMax(high.y(),p.y()));}
        view->m_cam->setLocation(QVector3D(0,0,100),QVector3D(),QVector3D());
        view->m_projection.setToIdentity();view->m_projection.ortho(low.x()-3,high.x()+3,low.y()-3,high.y()+3,.1f,300.f);
        QOpenGLFramebufferObject frame(800,600,QOpenGLFramebufferObject::CombinedDepthStencil);
        check(frame.bind(),"overlay framebuffer");auto* gl=QOpenGLContext::currentContext()->functions();
        gl->glViewport(0,0,800,600);gl->glClearColor(0,0,0,1);gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        while(gl->glGetError()!=GL_NO_ERROR){}view->drawTestOverlaysOpenGl();
        check(gl->glGetError()==GL_NO_ERROR,"overlay GL state");
        const QImage rendered=frame.toImage();int green=0;
        for(int y=0;y<rendered.height();++y)for(int x=0;x<rendered.width();++x){const auto c=rendered.pixelColor(x,y);if(c.green()>180&&c.red()<100&&c.blue()<150)++green;}
        check(green>50,"visible marker pixels");frame.release();
        if(!qEnvironmentVariable("EI_LEVER_OUTPUT").isEmpty()) {
            const QDir output(qEnvironmentVariable("EI_LEVER_OUTPUT"));
            check(rendered.save(output.filePath("markers.png")),"marker capture");
            check(dialog->grab().save(output.filePath("panel.png")),"panel capture");
        }
        check(view->selectedNodes().size()==1&&view->selectedNodes().first()==lever,"placement preserves selection");
        check(document->buildArchive()==before&&!mob->isDirty(),"preview changes no data");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(view,&escape);
        check(view->m_leverApproachLines.isEmpty()&&document->buildArchive()==before,"Escape cancels draft");delete dialog;
        dialog=new ei::LeverApproachDialog(document,view,nullptr);dialog->setAttribute(Qt::WA_DontShowOnScreen);check(dialog->initialize(&error),"reopen tool");
        dialog->m_count->setCurrentIndex(1);dialog->m_coordinates[0]->setValue(2.25);dialog->m_coordinates[1]->setValue(-1.75);
        int committed=0;QObject::connect(dialog,&ei::LeverApproachDialog::transactionCommitted,view,[&]{++committed;});dialog->apply();
        check(committed==1&&document->isDirty(),"Apply commits once");check(document->undo()&&document->buildArchive()==before,"single Undo restores all points");
        check(document->redo(),"Redo");delete dialog;
        lever->setState(eDraw);view->viewParameters();check(table->rowCount()==0,"no button without selection");
        out<<"PASS total="<<passes<<"\n";out.flush();return 0;
    }catch(const QString& error){out<<"FAILED "<<error<<"\n";out.flush();return 1;}
}

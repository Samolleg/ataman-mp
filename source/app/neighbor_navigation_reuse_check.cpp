#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "camera.h"
#include "resourcemanager.h"
#include "resource_setup_dialog.h"
#include "res_read_lease.h"
#include <QtWidgets>
#include <QThreadPool>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <functional>

// Opt-in native check: the real double-click/MainWindow workspace flow, with
// private copies of MPR/MOB and an owned profile. Never opens a visible editor.
int runNeighborNavigationReuseCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int failures=0;
    const auto check=[&](bool ok,const char* label) {
        out<<(ok?"PASS: ":"FAIL: ")<<label<<"\n";out.flush();if(!ok)++failures;
    };
    QTemporaryDir profile;
    if(!profile.isValid())return 1;
    QDir().mkpath(profile.filePath("Maps"));QDir().mkpath(profile.filePath("Res"));
    const QString source=qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT")+"/Maps/"
        +qEnvironmentVariable("EI_NEIGHBOR_PERF_ZONE","zone1");
    CMprDocument original;CValidationReport validation;
    if(!original.load(QFileInfo(source+".mpr"),&validation))return 1;
    const QSize sectors(original.mapInfo().sectorCountX,original.mapInfo().sectorCountY);
    const QSizeF size(sectors.width()*32,sectors.height()*32);
    QVector<SWorldMapZoneOrigin> maps;
    for(int i=0;i<3;++i) {
        SWorldMapZoneOrigin map;map.zoneId=QString(QChar('a'+i));
        map.profileId="base-game";map.allodId="reuse-world";map.mprIdentity=map.zoneId;
        map.mprPath=profile.filePath("Maps/"+map.zoneId+".mpr");
        map.mobPath=profile.filePath("Maps/"+map.zoneId+".mob");
        map.componentAnchorZoneId="a";map.sectorWidth=sectors.width();map.sectorHeight=sectors.height();
        map.origin=i==0?QPointF():i==1?QPointF(size.width(),0):QPointF(size.width()/2,size.height());
        check(QFile::copy(source+".mpr",map.mprPath)&&QFile::copy(source+".mob",map.mobPath),
              "private real MPR/MOB pair copied");
        maps.append(map);
    }
    // These profile signatures are only probed for existence, never executed.
    for(const QString& path:{profile.filePath("game.exe"),profile.filePath("Res/fixture.res")}) {
        QFile file(path);if(!file.open(QIODevice::WriteOnly))return 1;file.write("owned native test signature");
    }
    qApp->setProperty("workspaceLoadSmoke",true);
    QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){
        for(auto* widget:QApplication::topLevelWidgets())
            if(auto* dialog=qobject_cast<CResourceSetupDialog*>(widget))dialog->reject();
    });setup.start(1);
    MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);setup.stop();
    auto* settings=window.m_settings.data();
    dynamic_cast<COptString*>(settings->opt(eOptSetGeneral,"gameRoot"))->setValue(profile.path());
    dynamic_cast<COptString*>(settings->opt(eOptSetResource,"resourceProfile"))->setValue("base-game");
    dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"mapRoots"))->setValue({profile.filePath("Maps")});
    dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"resourceRoots"))->setValue(
        qEnvironmentVariable("EI_NEIGHBOR_PERF_RES").split(';',QString::SkipEmptyParts).toVector());
    CTextureList::getInstance()->attachSettings(settings);CObjectList::getInstance()->attachSettings(settings);
    window.m_worldMapLayoutDocument.setProfileDirectory(profile.path(),"base-game",{});
    QString error;
    check(window.m_worldMapLayoutDocument.addEditorMaps(maps,&error),"private editor world persisted");
    auto& view=*window.m_pView;view.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1200,800);window.show();QApplication::processEvents();
    check(view.isValid(),"hidden production MainWindow OpenGL context");
    if(!view.isValid()||failures)return 1;
    view.m_neighborLandscapesVisible=true;
    window.openMapPair(maps[0].mprPath,maps[0].mobPath,"a");
    const auto find=[&](const QString& id)->CView::SLoadedNeighborLandscape* {
        for(auto& scene:view.m_neighborLandscapes)if(scene.placement.zoneId==id)return &scene;
        return nullptr;
    };
    const auto wait=[&]() {
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<120000) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            bool ready=view.m_neighborLandscapes.size()==2&&!window.m_worldMetadataWatcher;
            for(const auto& scene:view.m_neighborLandscapes)ready &= scene.pendingObjects.isEmpty();
#ifndef EI_NEIGHBOR_REUSE_BASELINE
            ready &= view.m_neighborSceneRequests.isEmpty();
#endif
            if(ready)return true;
            QThread::msleep(4);
        }
        return false;
    };
    check(wait()&&find("b")&&find("c"),"initial world reaches two complete background scenes");
    if(!find("b")||!find("c"))return 1;
    auto* c=find("c");
    check(c->staticObjects.size()>1000,"real neighboring map includes over 1000 static objects");
    if(c->staticObjects.isEmpty())return 1;
    const auto cLandscape=c->landscape.toWeakRef();const auto cObject=c->staticObjects.first().toWeakRef();
    const auto cTexture=c->landscape->glTexture();const int cCount=c->staticObjects.size();
    const QString cId=CWorldTerrainSession::key(maps[2].mprPath);
    check(view.m_worldTerrainSession.load(maps[2].mprPath,&error),"neighbor canonical working model loaded");
    auto doc=view.m_worldTerrainSession.documents.value(cId);
    const auto before=doc->terrain.sectorPayload(0,0);
    STerrainBrush brush;brush.centerX=16;brush.centerY=16;brush.radius=4;
    SWorldTerrainStroke stroke;stroke.maps[cId]=doc->terrain.makeRaiseLower(brush,100);
    check(!stroke.maps[cId].isEmpty()&&view.m_worldTerrainSession.apply(stroke,true,&error),
          "unsaved shared terrain stroke applied to neighboring map");
    view.m_worldTerrainSession.record(stroke);view.refreshWorldTerrainNeighbors();
    const auto after=doc->terrain.sectorPayload(0,0);
    check(before!=after&&doc->terrain.isDirty(),"working terrain really differs from disk baseline");
    const auto capture=[&](const QString& name) {
        QVector3D pos,pivot,rot;view.captureCameraLocation(&pos,&pivot,&rot);
        const auto projection=view.m_projection;
        const auto origin=maps[2].origin-view.m_activeMapWorldOrigin;
        const QVector3D center(float(origin.x()+size.width()/2),float(origin.y()+size.height()/2),0);
        view.m_cam->setTelephotoEnabled(false);view.m_bDrawWater=false;
        view.m_fogVisible=false;view.m_sceneLightsVisible=false;
        dynamic_cast<COptInt*>(settings->opt("renderLightingMode"))->setValue(0);
        view.m_cam->setLocation(center+QVector3D(0,0,400),{},{});
        view.m_projection.setToIdentity();
        view.m_projection.ortho(float(-size.width()*0.6),float(size.width()*0.6),
                                float(-size.height()*0.6),float(size.height()*0.6),1,1024);
        view.makeCurrent();
        QOpenGLFramebufferObjectFormat format;format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject target(QSize(1024,1024),format);view.m_sceneReviewTarget=&target;
        view.draw();QOpenGLContext::currentContext()->functions()->glFinish();
        const auto frame=target.toImage();view.m_sceneReviewTarget=nullptr;target.release();
        view.m_projection=projection;
        const QString directory=qEnvironmentVariable("EI_NEIGHBOR_NAV_OUTPUT");
        if(!directory.isEmpty())frame.save(QDir(directory).filePath(name+".png"));
        view.m_cam->setLocation(pos,pivot,rot);return frame;
    };
    const auto beforeFrame=capture("before-c");
    int navigations=0;QObject::connect(&view,&CView::neighborMapOpenRequested,&view,
        [&](const QString&,const QString&,const QString&,const QPointF&,const QPointF&){++navigations;});
    for(int i=0;i<3;++i) {
        const auto target=maps[i%2?0:1];const auto* scene=find(target.zoneId);
        if(!scene){check(false,"target background available for navigation");break;}
        QVector3D center(float(scene->placement.origin.x()+size.width()/2),
                         float(scene->placement.origin.y()+size.height()/2),0);
        QVector3D localCenter(float(size.width()/2),float(size.height()/2),0);
        scene->landscape->projectPt(localCenter);center.setZ(localCenter.z());
        view.m_cam->setLocation(center+QVector3D(0,0,400),center,{});view.updateGL();
        QVector3D pos,pivot,rot;view.captureCameraLocation(&pos,&pivot,&rot);
        const QPointF sourceOrigin=view.m_activeMapWorldOrigin;
        const QVector3D point=center.project(view.m_cam->viewMatrix(),view.m_projection,
                                            QRect(0,0,view.width(),view.height()));
        const QPoint click(qRound(point.x()),view.height()-qRound(point.y()));
        QElapsedTimer timer;timer.start();
        QMouseEvent event(QEvent::MouseButtonDblClick,click,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&event);
        const qint64 openMs=timer.elapsed();check(wait(),"navigation reaches complete world scene");
        out<<"TIMING: navigation "<<i+1<<" openMs="<<openMs<<" settledMs="<<timer.elapsed()<<"\n";out.flush();
        check(navigations==i+1&&view.m_pLand->filePath().absoluteFilePath()==target.mprPath,
              "actual double click opens the target through production MainWindow");
        QVector3D newPos,newPivot,newRot;view.captureCameraLocation(&newPos,&newPivot,&newRot);
        const auto delta=sourceOrigin-target.origin;
        check((newPos-pos-QVector3D(float(delta.x()),float(delta.y()),0)).length()<0.001f
              &&(newPivot-pivot-QVector3D(float(delta.x()),float(delta.y()),0)).length()<0.001f&&newRot==rot,
              "camera world position and orientation survive navigation");
        c=find("c");
        check(c&&c->landscape==cLandscape.toStrongRef()&&c->landscape->glTexture()==cTexture,
              "unrelated neighbor retains original terrain and GPU atlas");
        check(c&&c->staticObjects.size()==cCount&&c->staticObjects.first()==cObject.toStrongRef(),
              "unrelated neighbor retains every ready object and original node");
        check(doc->terrain.sectorPayload(0,0)==after&&view.m_worldTerrainSession.canUndo(),
              "unsaved shared terrain and history survive navigation");
#ifndef EI_NEIGHBOR_REUSE_BASELINE
        check(view.m_parkedNeighborLandscapes.size()==1,"only one active-map background is parked");
#endif
    }
    const auto afterFrame=capture("after-c");
    QSet<QRgb> colors;
    for(int y=0;y<beforeFrame.height();y+=4)for(int x=0;x<beforeFrame.width();x+=4)
        colors.insert(beforeFrame.pixel(x,y));
    check(!beforeFrame.isNull()&&!afterFrame.isNull()&&colors.size()>64,"before/after native frames contain real terrain and objects");
    check(beforeFrame.copy(102,102,820,820)==afterFrame.copy(102,102,820,820),
          "unrelated neighbor viewport pixels remain identical after navigation");
    check(view.m_worldTerrainSession.undo(&error)&&doc->terrain.sectorPayload(0,0)==before,
          "shared terrain Undo after navigation restores exact original sectors");
    view.refreshWorldTerrainNeighbors();
    check(view.m_worldTerrainSession.redo(&error)&&doc->terrain.sectorPayload(0,0)==after,
          "shared terrain Redo after navigation restores edited sectors");
    view.refreshWorldTerrainNeighbors();
    int cancelled=0;QTimer cancel;QObject::connect(&cancel,&QTimer::timeout,[&](){
        for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget))
            if(box->standardButtons().testFlag(QMessageBox::Cancel)) {++cancelled;box->button(QMessageBox::Cancel)->click();}
    });cancel.start(5);
    const auto activePath=view.m_pLand->filePath().absoluteFilePath();
    window.openMapPair(maps[0].mprPath,maps[0].mobPath,"a");cancel.stop();
    check(cancelled==1&&view.m_pLand->filePath().absoluteFilePath()==activePath
          &&find("c")&&find("c")->landscape==cLandscape.toStrongRef()&&doc->terrain.sectorPayload(0,0)==after,
          "ordinary open Cancel preserves current map, cached scene and dirty working edit");
    // Move C away and back: its geometry/atlas stays, only the near objects change.
    auto distant=maps[2];distant.origin+=QPointF(size.width()*4,size.height()*4);
    check(window.m_worldMapLayoutDocument.replaceEditorMap(distant,&error),"private map moved far away");
    window.refreshEditorWorld();check(wait(),"distant role settles");
    c=find("c");check(c&&c->landscape==cLandscape.toStrongRef()&&c->staticObjects.isEmpty(),
        "far role releases objects without rebuilding terrain");
    check(window.m_worldMapLayoutDocument.replaceEditorMap(maps[2],&error),"private map returned near");
    window.refreshEditorWorld();check(wait(),"near role settles");
    c=find("c");check(c&&c->landscape==cLandscape.toStrongRef()&&c->staticObjects.size()==cCount,
        "near role reloads only objects and keeps edited terrain");
    check(view.m_worldTerrainSession.undo(&error),"private working edit reverted before source checks");
    view.refreshWorldTerrainNeighbors();
    // A publication changes the revision even if external metadata is identical.
    auto terrainBefore=c->landscape.toWeakRef();auto objectBefore=c->staticObjects.first().toWeakRef();
    {resIo::PublicationLease publication(maps[2].mobPath);check(publication.acquired(),"owned MOB source publication admitted");}
    window.refreshEditorWorld();check(wait(),"changed MOB source settles");c=find("c");
    check(c&&c->landscape==terrainBefore.toStrongRef()&&c->staticObjects.first()!=objectBefore.toStrongRef(),
          "MOB revision reloads objects and preserves terrain");
    {resIo::PublicationLease publication(maps[2].mprPath);check(publication.acquired(),"owned MPR source publication admitted");}
    window.refreshEditorWorld();check(wait(),"changed MPR source settles");c=find("c");
    check(c&&c->landscape!=terrainBefore.toStrongRef(),"MPR revision replaces the affected terrain scene");
    terrainBefore=c->landscape.toWeakRef();
    const QString previousOverride=CTextureList::getInstance()->mapLocalTerrainArchive();
    // An MPR is itself a valid RES container. This private override falls back
    // to configured texture archives for members it does not contain.
    CTextureList::getInstance()->setMapLocalTerrainArchive(maps[0].mprPath);
    window.refreshEditorWorld();check(wait(),"changed map-local resource source settles");c=find("c");
    check(c&&c->landscape!=terrainBefore.toStrongRef(),"map-local resource override invalidates affected background resources");
    CTextureList::getInstance()->setMapLocalTerrainArchive(previousOverride);
    view.clearNeighborLandscapes();view.loadNeighborLandscapes();view.clearNeighborLandscapes();
    view.m_neighborScenePool->waitForDone();QApplication::processEvents();
    check(view.m_neighborLandscapes.isEmpty(),"cancelled workers cannot resurrect cleared scenes");
    view.m_worldTerrainSession.clear();view.unloadMob("");view.unloadLand(false);
#ifndef EI_NEIGHBOR_REUSE_BASELINE
    check(view.m_neighborLandscapes.isEmpty()&&view.m_parkedNeighborLandscapes.isEmpty(),
          "ordinary unload releases visible and parked background resources");
#endif
    out<<"Neighbor navigation reuse failures: "<<failures<<"\n";out.flush();return failures?1:0;
}

#include "view.h"
#include "landscape.h"
#include "worldobj.h"
#include "utils.h"
#include <QJsonArray>
#include "mob.h"
#include "settings.h"
#include "tree_view.h"
#include "progressview.h"
#include "tile_form.h"
#include "neighbor_visual_preparation.h"
#include "resourcemanager.h"
#include "endurance_document.h"
#include "camera.h"
#include "world_map_layout_document.h"
#include "neighbor_map_layout.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QLineEdit>
#include <QUndoStack>
#include <QTextStream>
#include <QThread>
#include <QtConcurrent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCryptographicHash>

// Opt-in hidden integration check. Real resources are read only; output and
// settings are restricted to scratch. No normal editor window is shown.
int runNeighborPerformanceCheck()
{
    QTextStream out(stdout); int failures=0;
    const auto check=[&](bool pass,const char* label) {
        out<<(pass?"PASS: ":"FAIL: ")<<label<<"\n"; out.flush(); if(!pass) ++failures;
    };
    QTemporaryDir scratch;
    if(!scratch.isValid()) return 1;
    struct Restore { QString path=QDir::currentPath(); ~Restore(){QDir::setCurrent(path);} } restore;
    QDir::setCurrent(scratch.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
    CSettings settings;
    auto* roots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"));
    if(!roots) return 1;
    roots->setValue(qEnvironmentVariable("EI_NEIGHBOR_PERF_RES").split(';',QString::SkipEmptyParts).toVector());
    CTextureList::getInstance()->attachSettings(&settings);
    CObjectList::getInstance()->attachSettings(&settings);
    QUndoStack history; CTreeView tree; QTableWidget properties; properties.setColumnCount(2);
    CProgressView progress; QLineEdit coordinates; CTileForm tiles;
    CView view(nullptr,nullptr); view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    view.resize(640,480);
    // WA_DontShowOnScreen creates the backing surface without displaying an
    // interactive window. renderPixmap would reinitialize this legacy widget's
    // shaders on each call and cannot capture its bindDefault() presentation.
    view.show(); QCoreApplication::processEvents();
    check(view.isValid(),"hidden desktop OpenGL context");
    if(!view.isValid()) return 1;
    const QString root=qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT");
    SNeighborMapPlacement placement;
    placement.zoneId="neighbor-test";
    const QString zone=qEnvironmentVariable("EI_NEIGHBOR_PERF_ZONE","zone9");
    placement.mprPath=root+"/Maps/"+zone+".mpr"; placement.mobPath=root+"/Maps/"+zone+".mob";
    if(qEnvironmentVariableIsSet("EI_WORLD_PATHS_CHECK"))
    {
        CWorldMapLayoutDocument world(root+"/world-map-layouts.json");
        const auto records=world.loadEditorWorld("jamais-vu",
            qEnvironmentVariable("EI_WORLD_ROOTS_CHECK").split(';',QString::SkipEmptyParts));
        QVector<SMapCatalogEntry> topology;
        QString activeId,activeWorld;
        for(const auto& record:records.zones)
        {
            const QString path=world.absoluteResourcePath(record.mprPath);
            if(path.isEmpty()) continue;
            CMprDocument terrain; CValidationReport validation;
            const bool loaded=terrain.load(QFileInfo(path),&validation) && !validation.hasErrors();
            check(loaded,"resolved JSON terrain parses for neighbor context");
            if(!loaded) continue;
            SMapCatalogEntry entry;
            entry.zoneId=record.zoneId;entry.allodId=record.allodId;entry.mprName=record.mprIdentity;
            entry.mprPath=path;entry.mobPath=world.absoluteResourcePath(record.mobPath);
            entry.sectorWidth=terrain.mapInfo().sectorCountX;entry.sectorHeight=terrain.mapInfo().sectorCountY;
            topology.append(entry);
            if(record.mprIdentity==zone) {
                activeId=record.zoneId;activeWorld=record.allodId;
                placement.mprPath=entry.mprPath;placement.mobPath=entry.mobPath;
            }
        }
        const auto layout=CNeighborMapLayout::build(topology,records.zones,"jamais-vu",activeWorld,activeId,5);
        check(layout.hasActiveWorldOrigin && layout.placements.size()==5,
              "real JSON and ordered roots restore active map and five neighbors");
        bool objectsAvailable=true;
        for(const auto& neighbor:layout.placements)
            objectsAvailable &= QFileInfo(neighbor.mobPath).isFile();
        check(objectsAvailable && !layout.placements.isEmpty(),"resolved neighbors retain MOB object files");
    }
    CMprDocument document; CValidationReport report;
    if(!document.load(QFileInfo(placement.mprPath),&report)) return 1;
    placement.size=QSizeF(document.mapInfo().sectorCountX*32,document.mapInfo().sectorCountY*32);
    const auto digest=[](const QString& path) {
        QFile file(path); if(!file.open(QIODevice::ReadOnly)) return QByteArray();
        QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(&file); return hash.result();
    };
    const auto terrainHash=digest(placement.mprPath),mobHash=digest(placement.mobPath);
    const auto archives=CTextureList::getInstance()->configuredResourceArchivePaths();
    QElapsedTimer timer; timer.start();
    bool workerHasNoGl=false;
    auto future=QtConcurrent::run([&]() {
        workerHasNoGl=QOpenGLContext::currentContext()==nullptr && QThread::currentThread()!=qApp->thread();
        auto scene=prepareNeighborScene(placement);
        prepareNeighborVisuals(scene,archives,[]{return true;}); return scene;
    });
    future.waitForFinished();
    auto prepared=future.result();
    check(workerHasNoGl,"model/texture preparation on CPU worker without GL context");
    check(prepared.valid && prepared.visuals.size()==prepared.staticObjects.size(),"real scene visual preparation complete");
    // Compare the worker's JSON convention with the active MOB reader, not
    // with a second object constructed from the same potentially wrong JSON.
    CMobDocument rotationDocument; CValidationReport rotationReport;
    bool sameRotations=rotationDocument.load(QFileInfo(placement.mobPath),&rotationReport);
    QHash<QString,const SMobSection*> indexed;
    std::function<void(const QVector<SMobSection>&)> indexSections;
    indexSections=[&](const QVector<SMobSection>& sections) {
        for(const auto& section:sections) {
            indexed.insert(section.path,&section); indexSections(section.children);
        }
    };
    indexSections(rotationDocument.sections());
    int rotationCount=0;
    for(const auto& object:rotationDocument.objects()) {
        if(object.unit || object.sectionType!=0xB001 || !object.hasPosition || object.model.isEmpty()) continue;
        const auto* section=indexed.value(object.sectionPath,nullptr);
        if(!section || rotationCount>=prepared.staticObjects.size()) {sameRotations=false;break;}
        QVector4D activeRotation(0,0,0,1);
        for(const auto& field:section->children)
            if(field.type==0xB00A) {
                QByteArray raw=field.rawBytes.mid(8);
                util::CMobParser parser(raw); parser.readQuaternion(activeRotation);
            }
        const auto a=prepared.staticObjects[rotationCount++].value("Base object").toObject().value("Rotation").toArray();
        if(a.size()!=4) {sameRotations=false;continue;}
        const QQuaternion neighbor(float(a[3].toDouble()),float(a[0].toDouble()),
                                   float(a[1].toDouble()),float(a[2].toDouble()));
        const QQuaternion active(activeRotation);
        for(const auto& vertex:QVector<QVector3D>{{1,0,0},{0,1,0},{0,0,1},{2,-3,5}})
            if((active.rotatedVector(vertex)-neighbor.rotatedVector(vertex)).length()>0.00001f)
                sameRotations=false;
    }
    out<<"Compared active/neighbor rotations: "<<rotationCount<<"\n";
    check(sameRotations && rotationCount>0 && rotationCount==prepared.staticObjects.size(),
          "all real static object rotations match active MOB parser and transformed vertices");
    quint64 high=0,low=0; int usable=0,textured=0;
    for(const auto& visual:prepared.visuals)
    {
        if(!visual->parts.isEmpty() || visual->specializedVisual) ++usable;
        if(visual->texture.valid()) ++textured;
        for(const auto& part:visual->parts) { high+=part.vertices.size()/3; low+=part.distantVertices.size()/3; }
    }
    out<<"Prepared "<<usable<<"/"<<prepared.visuals.size()<<" objects, textures "<<textured
       <<", triangles "<<high<<" -> "<<low<<", CPU ms "<<timer.elapsed()<<"\n"; out.flush();
    check(usable>1000 && textured>1000,"real zone9 geometry and textures available");
    check(low<high,"real scene distant geometry reduces triangles");
    view.makeCurrent();
    QSet<QString> compared;
    bool sameGeometry=true;
    for(int i=0;i<prepared.visuals.size() && compared.size()<24;++i)
    {
        const auto& visual=prepared.visuals[i];
        if(visual->specializedVisual || compared.contains(visual->key)) continue;
        compared.insert(visual->key);
        CWorldObj reference(prepared.staticObjects[i]);
        reference.loadFigure(); reference.loadTexture();
        const auto oldItems=reference.staticRenderItems();
        QVector<const SPreparedStaticPart*> drawableParts;
        for(const auto& part:visual->parts)
            if(!part.vertices.isEmpty()) drawableParts.append(&part);
        // e.g. stst8 also contains an empty assembly helper. The legacy queue
        // excludes it through isDrawable(); compare the same rendered parts.
        if(oldItems.size()!=drawableParts.size()) {
            out<<"Mismatch model "<<prepared.staticObjects[i].value("Model name").toString()
               <<" parts "<<oldItems.size()<<" vs "<<drawableParts.size()<<"\n";
            sameGeometry=false; continue;
        }
        for(int p=0;p<oldItems.size();++p)
        {
            const auto& oldVertices=oldItems[p].part->vertData();
            const auto& newVertices=drawableParts[p]->vertices;
            if(oldVertices.size()!=newVertices.size()) {
                out<<"Mismatch model "<<prepared.staticObjects[i].value("Model name").toString()
                   <<" part "<<p<<" vertices "<<oldVertices.size()<<" vs "<<newVertices.size()<<"\n";
                sameGeometry=false; continue;
            }
            for(int v=0;v<oldVertices.size();++v)
                if(oldVertices[v].position!=newVertices[v].position
                    || oldVertices[v].normal!=newVertices[v].normal || oldVertices[v].texCoord!=newVertices[v].texCoord)
                {
                    out<<"Mismatch model "<<prepared.staticObjects[i].value("Model name").toString()
                       <<" part "<<p<<" vertex "<<v<<" position "<<(oldVertices[v].position==newVertices[v].position)
                       <<" normal "<<(oldVertices[v].normal==newVertices[v].normal)<<" UV "<<(oldVertices[v].texCoord==newVertices[v].texCoord)<<"\n";
                    sameGeometry=false; break;
                }
        }
    }
    check(sameGeometry && compared.size()==24,"24 real visual variants match legacy full-detail vertices/normals/UV");
    auto landscape=CLandscape::createReadOnlyBackground();
    check(landscape->readPreparedBackground(QFileInfo(placement.mprPath),prepared.terrainMembers),"prepared read-only terrain upload");
    view.m_pLand=landscape.data();
    CView::SLoadedNeighborLandscape neighbor;
    neighbor.placement=placement; neighbor.landscape=landscape;
    neighbor.pendingObjects=prepared.staticObjects; neighbor.pendingVisuals=prepared.visuals;
    view.m_neighborLandscapesVisible=true;
    view.m_neighborLandscapes.append(neighbor);
    timer.restart();
    while(!view.m_neighborLandscapes[0].pendingObjects.isEmpty() && timer.elapsed()<60000)
        view.populateNeighborObjects(view.m_neighborSceneGeneration->load());
    auto& loaded=view.m_neighborLandscapes[0];
    check(loaded.pendingObjects.isEmpty() && loaded.staticObjects.size()==usable,"bounded GPU upload completes");
    view.refreshNeighborRenderCache(loaded);
    check(loaded.drawRecords.size()==usable && loaded.spatialGroups.size()>1,"spatial groups and draw cache constructed");
    if(!loaded.drawRecords.isEmpty() && !loaded.drawRecords.first().full.isEmpty())
    {
        const auto first=loaded.drawRecords.first().full.first();
        const auto* storage=loaded.drawRecords.constData();
        view.refreshNeighborRenderCache(loaded);
        check(storage==loaded.drawRecords.constData(),"stationary frame reuses cached records");
        loaded.placement.origin+=QPointF(32,-64);
        view.refreshNeighborRenderCache(loaded);
        const auto moved=loaded.drawRecords.first().full.first();
        check(qAbs(moved.modelMatrix(0,3)-first.modelMatrix(0,3)-32)<0.001f
            && qAbs(moved.modelMatrix(1,3)-first.modelMatrix(1,3)+64)<0.001f,"moving neighbor invalidates world transforms");
        loaded.staticLocalPositions[0]+=QVector3D(0,0,3); loaded.drawCacheDirty=true;
        view.refreshNeighborRenderCache(loaded);
        check(qAbs(loaded.drawRecords.first().full.first().modelMatrix(2,3)-moved.modelMatrix(2,3)-3)<0.001f,"terrain reprojection invalidates cached height");
        view.makeCurrent();
        auto& queue=RenderQueueContext::instance();
        queue.begin(&view.m_program,QVector3D(0,0,100));
        for(const auto& item:loaded.drawRecords.first().full) queue.submitPrepared(item,true,true);
        check(!queue.shadowCasterBounds(10000).isEmpty(),"cached commands retain shadow caster bounds");
        queue.end();
    }
    else check(false,"nonempty render commands");
    view.m_neighborPlacements={placement};
    view.loadNeighborLandscapes();
    view.clearNeighborLandscapes();
    view.m_neighborScenePool->waitForDone();
    QCoreApplication::processEvents();
    check(view.m_neighborLandscapes.isEmpty(),"cancelled generation cannot repopulate neighbor scene");
    view.loadNeighborLandscapes();
    timer.restart();
    while(timer.elapsed()<60000)
    {
        QCoreApplication::processEvents();
        if(!view.m_neighborLandscapes.isEmpty() && view.m_neighborLandscapes.first().pendingObjects.isEmpty()) break;
        QThread::msleep(2);
    }
    check(view.m_neighborLandscapes.size()==1 && view.m_neighborLandscapes.first().staticObjects.size()==usable,
          "current asynchronous generation reaches complete GPU scene");
    auto* mob=new CMob; view.m_aMob.append(mob); view.m_activeMob=mob;
    QVector3D center(float(placement.size.width()/2),float(placement.size.height()/2),0);
    landscape->projectPt(center);
    view.m_cam->setTelephotoEnabled(false);
    view.m_cam->setLocation(center+QVector3D(0,0,400),center,QVector3D());
    view.m_projection.setToIdentity();
    view.m_projection.perspective(45,640.0f/480.0f,1,2000);
    view.updateGL();
    const auto frame=view.grabFrameBuffer();
    QSet<QRgb> colors;
    for(int y=0;y<frame.height();y+=2) for(int x=0;x<frame.width();x+=2) colors.insert(frame.pixel(x,y));
    check(!frame.isNull() && colors.size()>64,"hidden scene renders terrain and cached object queue");
    STravelArea arrival;arrival.mapPath=placement.mprPath;arrival.label="Arrival";
    arrival.rectangle=QRectF(center.x()-6,center.y()-6,12,12);
    auto transition=arrival;transition.remove=true;transition.label="Transition";
    transition.rectangle.translate(15,0);
    view.m_entranceOverlaysVisible=true;view.setWorldTravelAreas({arrival,transition});
    QVector3D probe(center.x(),center.y(),0);
    const auto oldOrigin=view.m_neighborLandscapes[0].placement.origin;
    view.m_neighborLandscapes[0].placement.origin=QPointF(70,-30);
    check(view.projectTravelOverlay(0,probe) && (probe-(center+QVector3D(70,-30,0))).length()<0.01f,
          "neighbor travel overlay uses its terrain height and world offset");
    view.m_neighborLandscapes[0].placement.origin=oldOrigin;
    view.updateGL();
    check(view.m_displayTravelAreas.size()==2 && view.grabFrameBuffer()!=frame,
          "arrival and transition reach rendered neighbor frame");
    view.m_neighborLandscapesVisible=false;probe=QVector3D(center.x(),center.y(),0);
    check(!view.projectTravelOverlay(0,probe),"hidden neighbor has no travel overlay");
    view.m_neighborLandscapesVisible=true;
    view.m_entranceOverlaysVisible=false;view.setWorldTravelAreas({arrival,transition});
    check(view.m_testEntranceRectangles.isEmpty(),"transition display toggle hides both area kinds");
    view.m_entranceOverlaysVisible=true;view.setWorldTravelAreas({});
    const QString imagePath=qEnvironmentVariable("EI_NEIGHBOR_PERF_IMAGE");
    if(!imagePath.isEmpty()) frame.save(imagePath);
    if(qEnvironmentVariableIsSet("EI_WORLD_JSON_CHECK"))
    {
        view.m_mprDocument=document;
        const QMatrix4x4 cameraBefore=view.m_cam->viewMatrix();
        int cancelled=0,confirmed=0;QPointF chosen;
        QObject::connect(&view,&CView::worldMapPlacementCancelled,&view,[&](){++cancelled;});
        QObject::connect(&view,&CView::worldMapPlacementConfirmed,&view,[&](const QPointF& point){++confirmed;chosen=point;});
        view.beginWorldMapPlacement(QSizeF(64,64));
        view.m_worldPlacementZ=center.z();view.updateWorldMapPlacement(view.rect().center());
        check(view.m_worldPlacementActive && view.m_worldPlacementValid,"world placement previews without changing documents");
        view.updateGL();
        const auto placementFrame=view.grabFrameBuffer();
        if(!imagePath.isEmpty()) placementFrame.save(imagePath + ".placement.png");
        out<<"Placement origin "<<view.m_worldPlacementOrigin.x()<<","<<view.m_worldPlacementOrigin.y()<<" z "<<view.m_worldPlacementZ<<"\n";
        check(!placementFrame.isNull() && placementFrame!=frame,"world placement outline reaches rendered frame");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(&view,&escape);
        check(cancelled==1 && confirmed==0 && !view.m_worldPlacementActive,"Escape cancels placement without commit");
        view.beginWorldMapPlacement(QSizeF(64,64));
        view.m_worldPlacementZ=center.z();
        QMouseEvent click(QEvent::MouseButtonPress,QPointF(view.rect().center()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&click);
        check(confirmed==1 && !view.m_worldPlacementActive && std::isfinite(chosen.x()),"left click confirms one world placement");
        check(view.m_cam->viewMatrix()==cameraBefore,"world placement preserves camera");
        view.clearNeighborLandscapes();view.m_neighborLandscapesVisible=false;
        QVector<SMapCatalogEntry> topology;QVector<SWorldMapZoneOrigin> zones;
        for(int i=0;i<9;++i)
        {
            SMapCatalogEntry entry;entry.zoneId=QString::number(i);entry.allodId="json-world";
            entry.mprName=zone;entry.mprPath=QString("layout-fixture-%1.mpr").arg(i);entry.sectorWidth=4;entry.sectorHeight=4;
            topology.append(entry);
            SWorldMapZoneOrigin item;item.profileId="json-profile";item.allodId="json-world";
            item.zoneId=entry.zoneId;item.mprIdentity=zone;item.componentAnchorZoneId=item.zoneId;
            item.origin=QPointF(i*128,0);zones.append(item);
        }
        view.m_preferredWorldMap="8";
        view.setNeighborMapContext(topology,zones,"json-profile","json-world","0");
        check(view.m_neighborPlacements.size()==8 && view.m_neighborPlacements.first().zoneId=="8",
              "all eight neighbors remain available with preferred map first");
        const auto unchangedGeneration=view.m_neighborSceneGeneration->load();
        view.setNeighborMapContext(topology,zones,"json-profile","json-world","0");
        check(view.m_neighborSceneGeneration->load()==unchangedGeneration,
              "unchanged world context keeps neighbor scenes and pending workers");
        zones[8].origin+=QPointF(16,0);
        view.setNeighborMapContext(topology,zones,"json-profile","json-world","0");
        check(view.m_neighborSceneGeneration->load()!=unchangedGeneration,
              "changed placement invalidates the neighbor context");
    }
    view.clearNeighborLandscapes();
    check(digest(placement.mprPath)==terrainHash && digest(placement.mobPath)==mobHash,"live MPR and MOB remain byte-identical");
    view.m_pLand=CLandscape::getInstance();
    view.makeCurrent(); neighbor.landscape.clear(); landscape.clear(); view.doneCurrent();
    out<<"Neighbor performance integration failures: "<<failures<<"\n"; out.flush();
    return failures?1:0;
}

#include "view.h"
#include "travel_global_map.h"
#include "travel_document.h"
#include <QDialog>
#include <QTimer>
#include <QDialogButtonBox>
#include <QPushButton>
#include "landscape.h"
#include "worldobj.h"
#include "utils.h"
#include <QJsonArray>
#include "mob.h"
#include "settings.h"
#include "tree_view.h"
#include "progressview.h"
#include "tile_form.h"
#include "landscape_tools.h"
#include <QAbstractButton>
#include <QMessageBox>
#include "neighbor_visual_preparation.h"
#include "resourcemanager.h"
#include "endurance_document.h"
#include "camera.h"
#include "world_map_layout_document.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
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
    out.setCodec("UTF-8");
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
    if(qEnvironmentVariableIsSet("EI_GLOBAL_MAP_CHECK")) {
        CTravelDocument travel;QString error;
        check(travel.open(qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT")+"/res/texts.res",&error),"read real map.txt");
        QWidget parent;parent.setAttribute(Qt::WA_DontShowOnScreen);QVector3D point;
        const QString image=qEnvironmentVariable("EI_GLOBAL_MAP_IMAGE");
        bool clicked=false;
        QTimer::singleShot(500,&parent,[&](){
            auto* dialog=parent.findChild<QDialog*>("travelGlobalDialog");
            auto* surface=parent.findChild<QWidget*>("travelGlobalSurface");
            if(!dialog||!surface)return;
            // Search the surface, verifying that a real selectable pixel exists.
            auto* buttons=dialog->findChild<QDialogButtonBox*>();
            for(int y=surface->height()/4;y<surface->height()*3/4&&!clicked;y+=10)
                for(int x=surface->width()/4;x<surface->width()*3/4&&!clicked;x+=10){
                    QMouseEvent event(QEvent::MouseButtonPress,QPointF(x,y),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QApplication::sendEvent(surface,&event);clicked=buttons->button(QDialogButtonBox::Ok)->isEnabled();
                }
            if(!image.isEmpty())dialog->grab().save(image);
            if(clicked)dialog->accept();else dialog->reject();
        });
        check(chooseTravelGlobalPosition(&parent,travel.table().zones(),"gipat",QString(),&point,false,&error)&&clicked,"real global model point selection");
        const auto accepted=point;
        QTimer::singleShot(100,&parent,[&](){auto* dialog=parent.findChild<QDialog*>("travelGlobalDialog");if(dialog)dialog->reject();});
        error.clear();
        check(!chooseTravelGlobalPosition(&parent,travel.table().zones(),"gipat",QString(),&point,true,&error)
            &&error.isEmpty()&&point==accepted,"cancel retains original marker");
        out<<error<<" point "<<point.x()<<","<<point.y()<<","<<point.z()<<"\n";
        return failures?1:0;
    }
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
    if(qEnvironmentVariableIsSet("EI_WORLD_BRUSH_CHECK")) {
        QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,&view,[&]() {
            for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)) {
                out<<"Unexpected dialog: "<<box->text()<<"\n";out.flush();++failures;box->reject();
            }
        });dismiss.start(50);
        CLandscapeTools tools;view.attachLandscapeTools(&tools);
        // This opt-in diagnostic changes only in-memory models: no snapshots
        // of installed game resources are needed or written.
        view.loadLandscape(QFileInfo(placement.mprPath),false);
        check(view.ensureTerrainAutoCatalogReady() && view.m_terrainEditAvailable,"active real map and material catalog load");
        if(!view.m_terrainEditAvailable)return 1;
        view.m_hasActiveMapWorldOrigin=true;view.m_activeMapWorldOrigin={0,0};view.m_neighborLayoutStorageWritable=true;
        view.m_worldTerrainEnabled=true;view.m_neighborPlacements.clear();
        SNeighborMapPlacement adjacent;adjacent.mprPath=root+"/Maps/"+qEnvironmentVariable("EI_WORLD_BRUSH_NEIGHBOR","zone2")+".mpr";
        CMprDocument other;CValidationReport validation;
        if(!other.load(QFileInfo(adjacent.mprPath),&validation))return 1;
        adjacent.worldOrigin={view.m_terrainEdit.tileCountX()*2.0,0};adjacent.origin=adjacent.worldOrigin;
        adjacent.size={other.mapInfo().sectorCountX*32.0,other.mapInfo().sectorCountY*32.0};
        view.m_neighborPlacements.append(adjacent);
        view.m_worldTerrainEnabledPaths.insert(CWorldTerrainSession::key(adjacent.mprPath));
        tools.activateMode(ELandscapeEditMode::TilePaint,true);tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        bool selected=false;
        for(const auto& family:view.m_terrainGlobalFamilies)if(tools.selectPaintTerrainType(family.id)){selected=true;break;}
        check(selected,"select a global painting family");
        const auto activeId=CWorldTerrainSession::key(placement.mprPath),neighborId=CWorldTerrainSession::key(adjacent.mprPath);
        if(qEnvironmentVariableIsSet("EI_WORLD_READY_ARTWORK_CHECK")) {
            // A full retained atlas needs no uniform bank and no new image
            // when both maps already agree at their shared material vertices.
            // Exercise the actual CView command, including all four packed
            // rotations, distinct map-local artwork and both terrain layers.
            SMprCreationRequest request;request.innerNamespace="ready-artwork";
            request.createOverlayPlane=true;request.overlayMaterialIndex=0;
            QByteArray fixture;CValidationReport setup;
            check(document.buildNewCanvas(request,&fixture,&setup),"create private ready-artwork canvas");
            if(fixture.isEmpty())return 1;
            CTerrainTransitionCatalog catalog;
            for(int slot=0;slot<2;++slot) {
                STerrainTransitionSignature signature;
                signature.northWest=signature.southEast=1+slot*2;
                signature.northEast=signature.southWest=2+slot*2;
                for(int rotation=0;rotation<4;++rotation) {
                    SMprPackedTile packed;packed.tileIndex=quint8(slot);packed.rotation=quint8(rotation);
                    STerrainTransitionVariant variant;variant.packedTile=packed.encode();
                    variant.signature=CTerrainTransitionCatalog::rotated(signature,-rotation);
                    check(catalog.addVariant(variant,&setup),"register ready physical rotation");
                }
            }
            QMap<QString,QByteArray> originalModels,originalAtlases;
            QMap<QString,QVector<QImage>> originalPixels;
            const auto modelHash=[](const SWorldTerrainDocument& doc) {
                QCryptographicHash hash(QCryptographicHash::Sha256);
                for(const auto& sector:doc.baseline.sectors())
                    hash.addData(doc.terrain.sectorPayload(sector.sectorX,sector.sectorY));
                for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                    CTerrainVirtualMaterialField field;doc.terrain.copyVirtualMaterialField(layer,&field,false);
                    hash.addData(field.stableSha256());
                }
                return hash.result();
            };
            view.m_worldTerrainSession.clear();view.m_worldTerrainAtlasSources.clear();
            int mapIndex=0;
            for(const auto& id:QStringList{activeId,neighborId}) {
                QSharedPointer<SWorldTerrainDocument> doc(new SWorldTerrainDocument);doc->path=id;
                check(doc->baseline.loadBytes(fixture,id,&setup) && doc->terrain.load(doc->baseline,&setup),"load private canvas in retained session");
                QVector<QImage> images;
                for(int slot=0;slot<64;++slot) {
                    QImage image(32,32,QImage::Format_RGBA8888);
                    for(int y=0;y<32;++y)for(int x=0;x<32;++x)
                        image.setPixelColor(x,y,QColor((x*7+slot*3+mapIndex*37)%256,
                            (y*5+slot*11)%256,(x+y+slot*17)%256,255));
                    images.append(image);
                }
                QVector<qint32> types(64,3);types[1]=5;
                doc->atlas.reset(new CTerrainWorkingAtlas(catalog,images,19,64,{}, {},types));
                check(doc->atlas->isValid() && doc->atlas->activeTileCount()==64,"retain completely full authored atlas");
                doc->terrain.enableWorkingAtlasSlots(64);
                check(doc->terrain.installWorkingAtlasSlotTypes(types),"install original runtime surface classes");
                STerrainTileCommand paint;paint.description="private ready artwork";
                for(int y=0;y<16;++y)for(int x=0;x<16;++x)
                    for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                        SMprPackedTile packed;packed.tileIndex=layer==ETerrainTileLayer::Land?0:1;
                        packed.rotation=quint8((x+y)%4);
                        STerrainTileDelta delta;delta.globalX=x;delta.globalY=y;delta.layer=layer;
                        delta.before=doc->terrain.packedTile(x,y,layer);delta.after=packed.encode();paint.deltas.append(delta);
                    }
                check(doc->terrain.apply(paint,true,&setup),"install coherent ready tuples in both layers");
                for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})
                    check(doc->terrain.initializeVirtualMaterialField(layer,catalog,&setup,
                        ETerrainMaterialFieldRecovery::RecoverDeterministically),"recover unchanged shared-corner field");
                doc->materials=doc->baseline.mapInfo().materials;
                view.m_worldTerrainSession.documents[id]=doc;
                view.m_worldTerrainAtlasSources[id]=SWorldTerrainAtlasSource();
                originalModels[id]=modelHash(*doc);originalAtlases[id]=doc->atlas->stateFingerprint();
                originalPixels[id]=doc->atlas->activeTileImages();++mapIndex;
            }
            if(setup.hasErrors()){out<<setup.toText()<<"\n";return 1;}
            const auto active=view.m_worldTerrainSession.documents[activeId];
            view.m_mprDocument=active->baseline;view.m_terrainEdit=active->terrain;
            view.m_workingTerrainAtlas=active->atlas;view.m_mprMaterials=active->materials;
            view.m_neighborPlacements[0].worldOrigin={32,0};view.m_neighborPlacements[0].origin={32,0};
            view.m_neighborPlacements[0].size={32,32};
            for(int invocation=0;invocation<2;++invocation) {
                QString summary;CValidationReport validation;STerrainResolveAuditSession audit;
                check(view.resolveWorldMaterialBlends(&summary,&validation,&audit) && !validation.hasErrors(),
                    "production world repair reuses ready artwork without free slots or uniform sources");
                out<<summary<<"\n"<<validation.toText()<<"\n";out.flush();
                check(audit.stagedCellCount==0 && audit.worldPackedDeltas.isEmpty(),"ready packed slots and all four rotations stay unchanged");
                for(const auto& id:QStringList{activeId,neighborId}) {
                    const auto doc=view.m_worldTerrainSession.documents[id];
                    check(modelHash(*doc)==originalModels[id],"ready material fields and sector bytes stay unchanged");
                    check(doc->atlas->stateFingerprint()==originalAtlases[id],"ready atlas identity and runtime classes stay unchanged");
                    check(doc->atlas->activeTileImages()==originalPixels[id],"every ready pixel including transition borders stays unchanged");
                }
                check(!view.m_worldTerrainSession.canUndo(),"no new artwork or empty repair is added to world history");
            }
            check(digest(placement.mprPath)==terrainHash && digest(placement.mobPath)==mobHash,"source map and objects remain unchanged");
            return failures?1:0;
        }
        if(qEnvironmentVariableIsSet("EI_WORLD_MATERIAL_CHECK")) {
            const bool seamPair=qEnvironmentVariableIsSet("EI_WORLD_SEAM_PAIR_CHECK");
            view.m_activeMapWorldOrigin=seamPair?(zone=="zone4"?QPointF(960,190):QPointF(704,64)):QPointF(0,0);
            view.m_neighborPlacements[0].worldOrigin=seamPair?(zone=="zone4"?QPointF(704,64):QPointF(960,190)):QPointF(512,324.5);
            if(seamPair)view.m_neighborPlacements[0].origin=view.m_neighborPlacements[0].worldOrigin-view.m_activeMapWorldOrigin;
            SNeighborMapPlacement third=adjacent;third.mprPath=root+"/Maps/zone13.mpr";
            third.worldOrigin={512,-59.5};third.size={448,384};
            const bool layoutCohort=qEnvironmentVariableIsSet("EI_WORLD_LAYOUT_MATERIAL_CHECK");
            if(!seamPair && !layoutCohort){view.m_neighborPlacements.append(third);
                view.m_worldTerrainEnabledPaths.insert(CWorldTerrainSession::key(third.mprPath));}
            if(layoutCohort) {
                CWorldMapLayoutDocument world(root+"/world-map-layouts.json");
                const auto records=world.loadEditorWorld(qEnvironmentVariable("EI_WORLD_LAYOUT_PROFILE","deja-vu"),{root});
                QString allod;
                for(const auto& record:records.zones)if(record.mprIdentity==zone) {
                    allod=record.allodId;view.m_activeMapWorldOrigin=record.origin;
                }
                check(!allod.isEmpty() && !records.storageBlocked,"read actual layout cohort without persisting it");
                if(allod.isEmpty() || records.storageBlocked)return 1;
                view.m_neighborPlacements.clear();view.m_worldTerrainEnabledPaths.clear();
                for(const auto& record:records.zones)if(record.allodId==allod && record.mprIdentity!=zone) {
                    SNeighborMapPlacement member;member.mprPath=world.absoluteResourcePath(record.mprPath);
                    member.worldOrigin=record.origin;member.origin=record.origin-view.m_activeMapWorldOrigin;
                    member.size={record.sectorWidth*32.0,record.sectorHeight*32.0};
                    view.m_neighborPlacements.append(member);
                    view.m_worldTerrainEnabledPaths.insert(CWorldTerrainSession::key(member.mprPath));
                }
                check(view.m_neighborPlacements.size()==7,"original eight-map cohort is selected");
            }
            const auto thirdHash=digest(third.mprPath),neighborHash=digest(adjacent.mprPath);
            const auto modelState=[](const SWorldTerrainDocument& doc) {
                QCryptographicHash hash(QCryptographicHash::Sha256);
                for(const auto& sec:doc.baseline.sectors())hash.addData(doc.terrain.sectorPayload(sec.sectorX,sec.sectorY));
                QByteArray fields;QDataStream stream(&fields,QIODevice::WriteOnly);
                for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                    QVector<qint32> current,baseline;doc.terrain.copyVirtualMaterialFieldState(layer,&current,&baseline);
                    stream<<current<<baseline;
                }
                hash.addData(fields);return hash.result();
            };
            QMap<QString,QByteArray> beforeModels,beforeAtlases,inputHashes;
            QMap<QString,CTerrainVirtualMaterialField> beforeOverlayFields;
            QSet<QString> cohort=view.m_worldTerrainEnabledPaths;cohort.insert(activeId);
            QString setupError;CValidationReport setupReport;
            if(!view.m_worldTerrainSession.load(activeId,&setupError))return 1;
            view.syncWorldTerrainActive();
            for(const auto& id:cohort) {
                inputHashes[id]=digest(id);
                const bool ready=view.m_worldTerrainSession.load(id,&setupError) && view.ensureWorldTerrainAtlas(id,&setupReport);
                check(ready && !setupReport.hasErrors(),"cohort atlas and pre-repair snapshot load");
                if(!ready || setupReport.hasErrors()){out<<setupError<<setupReport.toText()<<"\n";return 1;}
                const auto doc=view.m_worldTerrainSession.documents[id];
                beforeModels[id]=modelState(*doc);beforeAtlases[id]=doc->atlas->stateFingerprint();
                auto recovered=doc->terrain;
                check(recovered.initializeVirtualMaterialField(ETerrainTileLayer::Overlay,doc->atlas->catalog(),&setupReport,
                    ETerrainMaterialFieldRecovery::RecoverDeterministically),"recover original sparse overlay field in a copy");
                recovered.copyVirtualMaterialField(ETerrainTileLayer::Overlay,&beforeOverlayFields[id],false);
            }
            inputHashes[root+"/world-map-layouts.json"]=digest(root+"/world-map-layouts.json");
            QString summary;CValidationReport repair;STerrainResolveAuditSession audit;
            check(view.resolveWorldMaterialBlends(&summary,&repair,&audit) && !repair.hasErrors(),"world physical blend repair completes");
            out<<summary<<"\n"<<repair.toText()<<"\n";out.flush();
            if(repair.hasErrors())return 1;
            QMap<QString,QVector<QByteArray>> after;
            QMap<QString,QByteArray> materialHashes,afterModels,afterAtlases;
            for(auto it=view.m_worldTerrainSession.documents.cbegin();it!=view.m_worldTerrainSession.documents.cend();++it)
                for(const auto& sec:it.value()->baseline.sectors())after[it.key()].append(it.value()->terrain.sectorPayload(sec.sectorX,sec.sectorY));
            for(auto it=view.m_worldTerrainSession.documents.cbegin();it!=view.m_worldTerrainSession.documents.cend();++it) {
                CTerrainEditModel original;CValidationReport validation;original.load(it.value()->baseline,&validation);
                bool exact=true,types=true,interior=true,authored=true,presence=true,emptyVertices=true;int changed=0;
                for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                    CTerrainVirtualMaterialField field;it.value()->terrain.copyVirtualMaterialField(layer,&field,false);
                    materialHashes[it.key()]+=field.stableSha256();
                    for(int y=0;y<original.tileCountY();++y)for(int x=0;x<original.tileCountX();++x) {
                        bool beforeAvailable=false,nowAvailable=false;
                        const auto before=original.packedTile(x,y,layer,&beforeAvailable),now=it.value()->terrain.packedTile(x,y,layer,&nowAvailable);
                        presence &= beforeAvailable==nowAvailable && (beforeAvailable || before==now);
                        if(before==now)continue;++changed;
                        bool physical=false,semantic=false;
                        exact &= it.value()->atlas->catalog().gridSignatureForPacked(now,&physical)==field.cellSignature(x,y,&semantic)
                            && physical && semantic;
                        types &= original.tileType(before)==it.value()->terrain.tileType(now);
                        if(it.key()!=activeId)interior &= x==0 || y==0 || x==original.tileCountX()-1 || y==original.tileCountY()-1;
                    }
                    if(layer==ETerrainTileLayer::Overlay)for(int y=0;y<=original.tileCountY();++y)for(int x=0;x<=original.tileCountX();++x) {
                        bool incidentSurface=false;
                        for(int cy:{y-1,y})for(int cx:{x-1,x}) {
                            bool available=false;original.packedTile(cx,cy,layer,&available);incidentSurface |= available;
                        }
                        if(!incidentSurface)emptyVertices &= field.material(x,y)==beforeOverlayFields.value(it.key()).material(x,y);
                    }
                }
                const auto images=it.value()->atlas->activeTileImages(),old=it.value()->atlas->authoredTileImages();
                for(int slot=0;slot<old.size();++slot)authored &= images[slot]==old[slot];
                out<<QFileInfo(it.key()).fileName()<<" changed physical cells: "<<changed<<"\n";
                check(exact,"changed physical artwork has the requested material corners");
                check(types,"repaired physical tiles retain runtime terrain types");
                check(interior,"neighbor interiors remain unchanged");
                check(authored,"original authored pixels remain unchanged");
                check(presence,"overlay presence mask and inactive cells remain unchanged");
                check(emptyVertices,"vertices without an overlay surface retain their exact material values");
                afterModels[it.key()]=modelState(*it.value());afterAtlases[it.key()]=it.value()->atlas->stateFingerprint();
            }
            if(seamPair) {
                const auto a=view.m_worldTerrainSession.documents[CWorldTerrainSession::key(root+"/Maps/zone4.mpr")];
                const auto b=view.m_worldTerrainSession.documents[CWorldTerrainSession::key(root+"/Maps/zone6.mpr")];
                CTerrainVirtualMaterialField af,bf;a->terrain.copyVirtualMaterialField(ETerrainTileLayer::Land,&af);
                b->terrain.copyVirtualMaterialField(ETerrainTileLayer::Land,&bf);
                int mismatch=0,pixelMismatch=0,pairs=0,physicalMismatch=0;
                QCryptographicHash seamHash(QCryptographicHash::Sha256);
                for(int y=0;y<=129;++y)if(af.material(0,y)!=bf.material(128,y+63))++mismatch;
                for(int y=0;y<129;++y) {
                    const auto ap=a->terrain.packedTile(0,y,ETerrainTileLayer::Land),bp=b->terrain.packedTile(127,y+63,ETerrainTileLayer::Land);
                    const auto as=a->atlas->catalog().gridSignatureForPacked(ap),bs=b->atlas->catalog().gridSignatureForPacked(bp);
                    physicalMismatch+=as.northWest!=bs.northEast || as.southWest!=bs.southEast;
                    const QImage ai=a->atlas->activeTileImages()[SMprPackedTile::decode(ap).atlasSlot()];
                    const QImage bi=b->atlas->activeTileImages()[SMprPackedTile::decode(bp).atlasSlot()];
                    for(const auto& image:{ai,bi})seamHash.addData(reinterpret_cast<const char*>(image.constBits()),int(image.sizeInBytes()));
                    if(y+63>=179 && y+63<=182)continue; // SEC along-edge deformation has different sampling positions
                    for(int row=0;row<ai.height();++row)pixelMismatch+=ai.pixel(0,row)!=bi.pixel(bi.width()-1,row);
                    ++pairs;
                }
                out<<"zone4/zone6 shared vertices mismatched: "<<mismatch<<" physical edges: "<<physicalMismatch
                    <<" pixels: "<<pixelMismatch<<" compared pairs: "<<pairs<<" audit cells: "<<terrainResolveChangedCellCount(audit)<<"\n";
                out<<"zone4/zone6 artwork sha256: "<<seamHash.result().toHex()<<"\n";
                check(mismatch==0 && physicalMismatch==0,"zone4/zone6 material corners agree on both sides");
                check(pixelMismatch==0 && pairs==125,"undeformed zone4/zone6 physical edge pixels agree");
                check(audit.worldPackedDeltas.size()==2 && terrainResolveChangedCellCount(audit)>0,"world audit records changes to both maps");
            }
            QString error;
            if(view.m_worldTerrainSession.canUndo()) {
                check(view.worldTerrainHistory(false,true),"three-map repair undo");
                for(auto it=beforeModels.cbegin();it!=beforeModels.cend();++it) {
                    const auto doc=view.m_worldTerrainSession.documents[it.key()];
                    check(modelState(*doc)==it.value() && doc->atlas->stateFingerprint()==beforeAtlases[it.key()],
                        "undo exactly restores physical payloads, both semantic field states and atlas");
                }
                check(view.worldTerrainHistory(true,true),"three-map repair redo");
                for(auto it=after.cbegin();it!=after.cend();++it) {
                    QVector<QByteArray> restored;const auto doc=view.m_worldTerrainSession.documents[it.key()];
                    for(const auto& sec:doc->baseline.sectors())restored.append(doc->terrain.sectorPayload(sec.sectorX,sec.sectorY));
                    check(restored==it.value(),"redo restores every physical sector");
                    check(modelState(*doc)==afterModels[it.key()] && doc->atlas->stateFingerprint()==afterAtlases[it.key()],
                        "redo exactly restores semantic fields and atlas");
                }
            }
            CValidationReport repeat;check(view.resolveWorldMaterialBlends(&summary,&repeat) && !repeat.hasErrors(),"repeat repair completes");
            out<<summary<<"\n"<<repeat.toText()<<"\n";
            QMap<QString,QVector<QImage>> savedEdges;
            for(auto it=after.cbegin();it!=after.cend();++it) {
                QVector<QByteArray> repeated;const auto doc=view.m_worldTerrainSession.documents[it.key()];
                for(const auto& sec:doc->baseline.sectors())repeated.append(doc->terrain.sectorPayload(sec.sectorX,sec.sectorY));
                check(repeated==it.value(),"repeat repair is physically idempotent");
                QByteArray materials;for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                    CTerrainVirtualMaterialField field;doc->terrain.copyVirtualMaterialField(layer,&field,false);materials+=field.stableSha256();
                }
                check(materials==materialHashes[it.key()],"repeat repair preserves semantic fields");
                check(doc->atlas->stateFingerprint()==afterAtlases[it.key()],"repeat repair preserves every working atlas");
                CValidationReport serialized;
                const auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(doc->baseline,
                    doc->atlas->activeTileImages(),doc->atlas->activeTileTypes(),view.m_worldTerrainAtlasSources[it.key()].pages,
                    doc->materials,doc->baseline.mapInfo().animatedTiles,&serialized);
                CMprDocument saved,reopened;CTerrainEditModel model;CMprEditTransaction tx;QByteArray bytes;
                const bool valid=package.success && saved.loadBytes(package.mprBytes,"memory.mpr",&serialized)
                    && doc->terrain.appendSectorEdits(&tx,&serialized) && saved.applyTransaction(tx,&bytes,&serialized)
                    && reopened.loadBytes(bytes,"reopened.mpr",&serialized) && model.load(reopened,&serialized);
                check(valid && !serialized.hasErrors(),"world repair MPR and atlas serialize and reopen in memory");
                if(valid) {
                    QVector<QByteArray> payloads;for(const auto& sec:doc->baseline.sectors())payloads.append(model.sectorPayload(sec.sectorX,sec.sectorY));
                    check(payloads==it.value(),"reopened physical sectors match repaired sectors");
                    if(seamPair) {
                        CResArchiveDocument archive;QMap<int,QImage> decoded;QVector<QImage> edge;
                        bool pixels=archive.loadBytes(package.resourceArchiveBytes,"memory.res");
                        for(int y=0;y<129 && pixels;++y) {
                            const bool zone4=it.key()==CWorldTerrainSession::key(root+"/Maps/zone4.mpr");
                            const auto packed=SMprPackedTile::decode(model.packedTile(zone4?0:127,zone4?y:y+63,ETerrainTileLayer::Land));
                            const int slot=packed.atlasSlot();
                            if(!decoded.contains(slot)) {
                                QImage tile;pixels=ei::decodeMmpDxt1AtlasTile(archive.memberPayload(package.resourceMembers.value(packed.atlasIndex)),packed.tileIndex,&tile);
                                if(pixels)decoded[slot]=tile;
                            }
                            edge.append(decoded.value(slot));
                        }
                        check(pixels && edge.size()==129,"saved game-format MMP border tiles decode");
                        savedEdges[it.key()]=edge;
                    }
                } else out<<serialized.toText()<<"\n";
            }
            if(seamPair && savedEdges.size()==2) {
                const auto a=savedEdges[CWorldTerrainSession::key(root+"/Maps/zone4.mpr")];
                const auto b=savedEdges[CWorldTerrainSession::key(root+"/Maps/zone6.mpr")];
                int mismatch=0;
                if(a.size()==129 && b.size()==129)for(int y=0;y<129;++y) {
                    if(y+63>=179 && y+63<=182)continue;
                    for(int row=0;row<a[y].height();++row)mismatch+=a[y].pixel(0,row)!=b[y].pixel(b[y].width()-1,row);
                } else ++mismatch;
                out<<"saved MMP shared-edge pixel mismatches: "<<mismatch<<"\n";
                check(mismatch==0,"saved DXT1 border pixels still agree between zone4 and zone6");
            }
            check(digest(placement.mprPath)==terrainHash && digest(placement.mobPath)==mobHash
                && digest(adjacent.mprPath)==neighborHash && digest(third.mprPath)==thirdHash,"all real game files unchanged");
            bool inputUnchanged=true;for(auto it=inputHashes.cbegin();it!=inputHashes.cend();++it)inputUnchanged &= digest(it.key())==it.value();
            check(inputUnchanged,"all cohort MPR files and layout remain byte-identical on disk");
            return failures?1:0;
        }
        const auto before=view.m_terrainEdit;
        QString message;QObject::connect(&view,&CView::updateMsg,&view,[&](const QString& text){message=text;});
        view.beginTileBrushGroup();
        check(view.paintWorldTerrainAt(QVector3D(adjacent.origin.x(),16,0),true),"shared painter handles boundary stamp");
        out<<"Painter message: "<<message<<"\n";
        check(view.m_worldTerrainStroke.paint.contains(activeId)&&view.m_worldTerrainStroke.paint.contains(neighborId),"one stroke changes both real independent atlases");
        view.endTileBrushGroup();
        QString error;check(view.m_worldTerrainSession.undo(&error),"undo complete paint cohort");out<<error<<"\n";
        check(view.m_worldTerrainSession.documents.value(activeId) && view.m_worldTerrainSession.documents[activeId]->terrain.sectorPayload(0,0)==before.sectorPayload(0,0),"undo restores active terrain");
        check(view.m_worldTerrainSession.redo(&error),"redo complete paint cohort");
        for(const auto& id:QStringList{activeId,neighborId})check(view.previewWorldPaintState(id),"restore redo preview for each world document");
        tools.activateMode(ELandscapeEditMode::OverlaySurface,true);tools.adjustPaintSize(1);
        selected=false;
        for(const auto& family:view.m_terrainGlobalFamilies)if(tools.selectPaintTerrainType(family.id)){selected=true;break;}
        check(selected,"select a compatible overlay family");
        int surfaceY=-1;
        for(int y=2;y<qMin(view.m_terrainEdit.tileCountY(),int(other.mapInfo().sectorCountY)*16)-2;++y) {
            bool a=false,b=false;
            const auto left=view.m_terrainEdit.packedTile(view.m_terrainEdit.tileCountX()-1,y,ETerrainTileLayer::Overlay,&a);
            const auto right=view.m_worldTerrainSession.documents[neighborId]->terrain.packedTile(0,y,ETerrainTileLayer::Overlay,&b);
            if((!a || left==0xffff)&&(!b || right==0xffff)){surfaceY=y;break;}
        }
        check(surfaceY>=0,"find boundary without existing surfaces on either map");
        view.beginTileBrushGroup();message.clear();
        check(view.paintWorldTerrainAt(QVector3D(adjacent.origin.x(),surfaceY*2,0),false),"shared surface creation handles boundary");
        out<<"Surface message: "<<message<<" mode "<<int(tools.editMode())<<" operation "<<int(tools.overlaySurfaceOperation())<<" paint "<<view.m_worldTerrainStroke.paint.size()<<" overlays "<<view.m_worldTerrainStroke.overlays.size()<<" history "<<view.m_worldTerrainSession.canUndo()<<"\n";
        check(view.m_worldTerrainStroke.paint.contains(activeId)&&view.m_worldTerrainStroke.paint.contains(neighborId),"surface creation changes both maps");
        view.endTileBrushGroup();
        check(view.worldTerrainHistory(false,true),"surface undo through view history");
        check(view.worldTerrainHistory(true,true),"surface redo through view history");
        QVector<SWorldMaterialField> landFields,overlayFields;
        check(view.worldMaterialNeighbors(&landFields,&overlayFields,&validation)&&!landFields.isEmpty(),"boundary repair reads retained neighbor material field");
        CTerrainVirtualMaterialField field;
        check(view.m_terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false),"active semantic field available");
        reconcileWorldMaterialBoundary(view.m_activeMapWorldOrigin,&field,landFields);
        for(int y=0;y<qMin(field.vertexHeight(),landFields.first().field.vertexHeight());++y)
            check(field.material(field.tileWidth(),y)==landFields.first().field.material(0,y),"border semantic vertex matches touching neighbor");
        check(digest(placement.mprPath)==terrainHash && digest(placement.mobPath)==mobHash,"installed files unchanged by painter regression");
        if(qEnvironmentVariableIsSet("EI_WORLD_ATLAS_SWITCH_CHECK")) {
            CTileForm reopenedPicker;
            reopenedPicker.fillTable(other.mapInfo().innerNamespace,int(other.mapInfo().textureCount));
            view.m_authoredTerrainTileImages=reopenedPicker.tileImages();
            const auto stage=view.m_worldTerrainSession.documents[neighborId]->atlas->createStagingCopy();
            const auto images=stage->activeTileImages();const int count=stage->authoredSlotCount();
            int first=-1;for(int i=0;i<qMin(count,view.m_authoredTerrainTileImages.size());++i)
                if(images[i]!=view.m_authoredTerrainTileImages[i]){first=i;break;}
            out<<"Authored mismatch slot "<<first<<" counts "<<count<<"/"<<view.m_authoredTerrainTileImages.size()<<"\n";
            if(first>=0)out<<"formats "<<int(images[first].format())<<"/"<<int(view.m_authoredTerrainTileImages[first].format())
                <<" RGBA equal "<<(images[first].convertToFormat(QImage::Format_RGBA8888)==view.m_authoredTerrainTileImages[first].convertToFormat(QImage::Format_RGBA8888))<<"\n";
            CValidationReport upload;check(view.syncWorkingTerrainAtlas(&upload,stage.data()),"retained neighbor atlas passes authored protection");
            out<<upload.toText()<<"\n";
            const auto stale=view.m_workingTerrainAtlas->createStagingCopy();
            CValidationReport staleReport;QImage pending=stale->authoredTileImages().first();pending.fill(QColor(31,57,91));
            check(stale->importPhysicalTile(pending,0,nullptr,"regression",0,&staleReport)>=0,"stage repair upload while another map baseline is selected");
            check(!view.syncWorkingTerrainAtlas(&staleReport,stale.data()) && staleReport.hasErrors(),"reproduce authored protection refusal with stale same-sized baseline");
            // Same slot count is not proof of the same map. Rebind the retained
            // active cache after loading another map's reference/picker.
            check(view.previewWorldPaintState(activeId),"restore retained active atlas and immutable baseline together");
            const auto restored=view.m_workingTerrainAtlas->createStagingCopy();
            CValidationReport restoredUpload;
            check(view.m_authoredTerrainTileImages==restored->authoredTileImages(),"restored baseline belongs to the retained document");
            check(view.syncWorkingTerrainAtlas(&restoredUpload,restored.data()),"repair upload accepts restored atlas without disabling protection");
            out<<restoredUpload.toText()<<"\n";
            CValidationReport retry;
            check(view.syncWorkingTerrainAtlas(&retry,stale.data()),"identical refused candidate passes after restoring its own baseline");
            if(!view.m_authoredTerrainTileImages.isEmpty()) {
                view.m_authoredTerrainTileImages[0].fill(Qt::magenta);
                CValidationReport rejected;
                const auto tamperedStage=restored->createStagingCopy();
                QImage probe=restored->authoredTileImages().first();probe.fill(QColor(17,33,71));
                check(tamperedStage->importPhysicalTile(probe,0,nullptr,"regression",0,&rejected)>=0,"stage a pending upload for protection test");
                check(!view.syncWorkingTerrainAtlas(&rejected,tamperedStage.data()) && rejected.hasErrors(),"real authored pixel mismatch remains rejected");
                view.previewWorldPaintState(activeId);
            }
        }
        view.m_worldTerrainSession.clear();view.m_worldTerrainStroke={};
        view.attachLandscapeTools(nullptr);view.m_pLand->setDirty(false);
        return failures?1:0;
    }
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

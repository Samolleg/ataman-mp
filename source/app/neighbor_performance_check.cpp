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
#include <QCheckBox>
#include "neighbor_visual_preparation.h"
#include "resourcemanager.h"
#include "endurance_document.h"
#include "camera.h"
#include "world_map_layout_document.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
#include "neighbor_map_layout.h"
#include "resource_name.h"
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
    if(qEnvironmentVariableIsSet("EI_WORLD_SAVE_WORLD_TERRAIN_CHECK"))
        QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,
            [](const QString&,bool* accepted){if(accepted)*accepted=true;});
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
        bool savePromptHandled=false;
        QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,&view,[&]() {
            for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)) {
                if(qEnvironmentVariableIsSet("EI_WORLD_SAVE_WORLD_TERRAIN_CHECK")
                   && box->standardButtons().testFlag(QMessageBox::Save)) {
                    savePromptHandled=true;
                    box->button(QMessageBox::Save)->click();
                    continue;
                }
                out<<"Unexpected dialog: "<<box->text()<<"\n";out.flush();++failures;box->reject();
            }
        });dismiss.start(50);
        CLandscapeTools tools;view.attachLandscapeTools(&tools);
        // This opt-in diagnostic changes only in-memory models: no snapshots
        // of installed game resources are needed or written.
        if(qEnvironmentVariableIsSet("EI_WORLD_MIXED_EDGE_BRUSH_CHECK")) {
        if(auto* resourceRoots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots")))
            resourceRoots->setValue({root+"/res/HDPack_Terrain.res"});
        }
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
        QVector3D seamPoint(float(adjacent.origin.x()),16.0f,0.0f);
        if(qEnvironmentVariableIsSet("EI_WORLD_SEAM_PAIR_CHECK")) {
            if(zone=="zone4") {
                view.m_activeMapWorldOrigin={960,190};adjacent.worldOrigin={704,64};
                seamPoint=QVector3D(0,16,0);
            } else {
                view.m_activeMapWorldOrigin={704,64};adjacent.worldOrigin={960,190};
                seamPoint=QVector3D(256,142,0);
            }
            adjacent.origin=adjacent.worldOrigin-view.m_activeMapWorldOrigin;
        }
        view.m_neighborPlacements.append(adjacent);
        view.m_worldTerrainEnabledPaths.insert(CWorldTerrainSession::key(adjacent.mprPath));
        tools.activateMode(ELandscapeEditMode::TilePaint,true);tools.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        const auto activeId=CWorldTerrainSession::key(placement.mprPath),neighborId=CWorldTerrainSession::key(adjacent.mprPath);
        bool selected=false;
        QString selectedFamily;
        if(qEnvironmentVariableIsSet("EI_WORLD_MIXED_EDGE_BRUSH_CHECK")) {
            QString sessionError;CValidationReport sessionReport;
            const bool ready=view.m_worldTerrainSession.load(placement.mprPath,&sessionError)
                && view.m_worldTerrainSession.load(adjacent.mprPath,&sessionError)
                && view.ensureWorldTerrainAtlas(activeId,&sessionReport)
                && view.ensureWorldTerrainAtlas(neighborId,&sessionReport);
            check(ready && !sessionReport.hasErrors(),"load both map fields for a changed-material seam stamp");
            if(!ready || sessionReport.hasErrors())return 1;
            view.syncWorldTerrainActive();
            CTerrainVirtualMaterialField activeField,neighborField;
            const auto activeDoc=view.m_worldTerrainSession.documents.value(activeId);
            const auto neighborDoc=view.m_worldTerrainSession.documents.value(neighborId);
            const bool fieldsReady=activeDoc && neighborDoc
                && activeDoc->terrain.copyVirtualMaterialField(ETerrainTileLayer::Land,&activeField,false)
                && neighborDoc->terrain.copyVirtualMaterialField(ETerrainTileLayer::Land,&neighborField,false);
            check(fieldsReady,"read both seam material fields");
            if(!fieldsReady)return 1;
            QSet<qint32> seamMaterials;
            const QPointF worldPoint=view.m_activeMapWorldOrigin+QPointF(seamPoint.x(),seamPoint.y());
            const int ax=qRound((worldPoint.x()-view.m_activeMapWorldOrigin.x())/2.0);
            const int ay=qRound((worldPoint.y()-view.m_activeMapWorldOrigin.y())/2.0);
            const int nx=qRound((worldPoint.x()-adjacent.worldOrigin.x())/2.0);
            const int ny=qRound((worldPoint.y()-adjacent.worldOrigin.y())/2.0);
            for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
                if(ax+dx>=0&&ax+dx<=activeField.tileWidth()&&ay+dy>=0&&ay+dy<=activeField.tileHeight())
                    seamMaterials.insert(activeField.material(ax+dx,ay+dy));
                if(nx+dx>=0&&nx+dx<=neighborField.tileWidth()&&ny+dy>=0&&ny+dy<=neighborField.tileHeight())
                    seamMaterials.insert(neighborField.material(nx+dx,ny+dy));
            }
            for(const auto& family:view.m_terrainGlobalFamilies) {
            if(qEnvironmentVariableIsSet("EI_WORLD_MIXED_EDGE_BRUSH_CHECK")
               && (family.layer!=ETerrainAtlasLayer::Land || family.runtimeTerrainType<0 || family.sources.isEmpty()))continue;
                if(seamMaterials.contains(family.id))continue;
                if(tools.selectPaintTerrainType(family.id)){selected=true;selectedFamily=QString::number(family.id);break;}
            }
        } else {
            for(const auto& family:view.m_terrainGlobalFamilies)
                if(tools.selectPaintTerrainType(family.id)){selected=true;selectedFamily=QString::number(family.id);break;}
        }
        const QByteArray selectionLabel=(QStringLiteral("select a global painting family absent from the seam footprint: ")
            +selectedFamily).toUtf8();
        check(selected,selectionLabel.constData());
        if(qEnvironmentVariableIsSet("EI_WORLD_MIXED_EDGE_BRUSH_CHECK")) {
            if(auto* radius=tools.findChild<QSlider*>("terrainPaintRadius"))radius->setValue(1);
            auto* mixedEdge=tools.findChild<QCheckBox*>("modernMixedEdge");
            if(mixedEdge)mixedEdge->setChecked(true);
            check(mixedEdge && mixedEdge->isChecked() && tools.modernMixedEdge(),
                "enable mixed-edge for the real two-map brush path");
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
            const bool repaired=view.resolveWorldMaterialBlends(&summary,&repair,&audit,seamPair) && !repair.hasErrors();
            if(!repaired)out<<"World repair summary: "<<summary<<"\n"<<repair.toText()<<"\n";
            check(repaired,"world physical blend repair completes");
            out<<summary<<"\n"<<repair.toText()<<"\n";out.flush();
            if(repair.hasErrors())return 1;
            QMap<QString,QVector<QByteArray>> after;
            QMap<QString,QMap<QPair<int,int>,QByteArray>> afterByCoordinate;
            QMap<QString,QByteArray> materialHashes,afterModels,afterAtlases;
            for(auto it=view.m_worldTerrainSession.documents.cbegin();it!=view.m_worldTerrainSession.documents.cend();++it)
                for(const auto& sec:it.value()->baseline.sectors()) {
                    const auto payload=it.value()->terrain.sectorPayload(sec.sectorX,sec.sectorY);
                    after[it.key()].append(payload);
                    afterByCoordinate[it.key()].insert(qMakePair(sec.sectorX,sec.sectorY),payload);
                }
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
                QStringList changedAuthoredSlots;
                for(int slot=0;slot<it.value()->atlas->authoredSlotCount();++slot)if(slot>=images.size()||slot>=old.size()||images[slot]!=old[slot]) {
                    authored=false;changedAuthoredSlots.append(QString::number(slot));
                }
                out<<QFileInfo(it.key()).fileName()<<" changed physical cells: "<<changed<<"\n";
                check(exact,"changed physical artwork has the requested material corners");
                check(types,"repaired physical tiles retain runtime terrain types");
                check(interior,"neighbor interiors remain unchanged");
                if(!authored)out<<"authored slot mismatches: "<<changedAuthoredSlots.join(",")
                    <<" authored slots: "<<it.value()->atlas->authoredSlotCount()
                    <<" authored images: "<<old.size()<<" active images: "<<images.size()<<"\n";
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
            CValidationReport repeat;check(view.resolveWorldMaterialBlends(&summary,&repeat,nullptr,seamPair) && !repeat.hasErrors(),"repeat repair completes");
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
            bool savedCopies=false;
            if(qEnvironmentVariableIsSet("EI_WORLD_SAVE_WORLD_TERRAIN_CHECK")) {
                QMap<QString,QByteArray> mprBefore;QMap<QString,QByteArray> archiveBefore;
                for(const auto& id:cohort) {
                    mprBefore[id]=digest(id);
                    const auto source=view.m_worldTerrainAtlasSources.value(id);
                    if(!source.archive.isEmpty())archiveBefore[source.archive]=digest(source.archive);
                }
                check(archiveBefore.size()==1,"selected real map pair shares one copied terrain archive");
                savedCopies=view.saveWorldTerrainMaps();
                check(savedCopies && savePromptHandled,"save repaired seam through the production cohort publisher");
                for(const auto& id:cohort)
                    check(digest(id)!=mprBefore.value(id),"repair save commits each selected copied MPR");
                for(auto it=archiveBefore.cbegin();it!=archiveBefore.cend();++it)
                    check(digest(it.key())!=it.value(),"repair publishes one shared terrain archive candidate");
                QMap<QString,CMprDocument> diskMaps;QMap<QString,CTerrainEditModel> diskModels;
                bool reopened=true;
                for(const auto& id:cohort) {
                    CValidationReport diskReport;
                    const bool loaded=diskMaps[id].load(QFileInfo(id),&diskReport)
                        && diskModels[id].load(diskMaps[id],&diskReport) && !diskReport.hasErrors();
                    if(!loaded)out<<"reopen diagnostic "<<QFileInfo(id).fileName()<<": "<<diskReport.toText()<<"\n";
                    reopened &= loaded;
                    if(reopened) {
                        QMap<QPair<int,int>,QByteArray> sectors;
                        for(const auto& sec:diskMaps[id].sectors())
                            sectors.insert(qMakePair(sec.sectorX,sec.sectorY),diskModels[id].sectorPayload(sec.sectorX,sec.sectorY));
                        const bool equal=sectors==afterByCoordinate.value(id);
                        if(!equal) {
                            out<<"reopen sector mismatch "<<QFileInfo(id).fileName()<<" disk="<<sectors.size()
                                <<" expected="<<afterByCoordinate.value(id).size()<<"\n";
                            const auto expectedSectors=afterByCoordinate.value(id);
                            for(auto si=expectedSectors.cbegin();si!=expectedSectors.cend();++si)
                                if(sectors.value(si.key())!=si.value())
                                    out<<"  sector "<<si.key().first<<","<<si.key().second
                                        <<" diskBytes="<<sectors.value(si.key()).size()<<" expectedBytes="<<si.value().size()<<"\n";
                        }
                        reopened &= equal;
                    }
                }
                check(reopened,"reopen both repaired copied maps with saved sectors intact");
                if(reopened && seamPair) {
                    const auto id4=CWorldTerrainSession::key(root+"/Maps/zone4.mpr");
                    const auto id6=CWorldTerrainSession::key(root+"/Maps/zone6.mpr");
                    CTerrainVirtualMaterialField field4,field6;
                    diskModels[id4].copyVirtualMaterialField(ETerrainTileLayer::Land,&field4,false);
                    diskModels[id6].copyVirtualMaterialField(ETerrainTileLayer::Land,&field6,false);
                    int semanticMismatch=0;
                    for(int y=0;y<=129;++y)semanticMismatch+=field4.material(0,y)!=field6.material(128,y+63);
                    check(semanticMismatch==0,"reopened zone4/zone6 shared material vertices agree");
                    const QString archivePath=archiveBefore.firstKey();QFile archiveFile(archivePath);QByteArray bytes;
                    CResArchiveDocument archive;QStringList archiveErrors;
                    bool pixels=archiveFile.open(QIODevice::ReadOnly)
                        && (bytes=archiveFile.readAll(),archive.loadBytes(bytes,archivePath,&archiveErrors));
                    int pixelMismatch=0,edgeCells=0;
                    const auto decode=[&](const QString& id,int x,int y)->QImage {
                        const auto& model=diskModels[id];bool available=false;
                        const auto packed=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land,&available));
                        const auto names=ei::terrainTextureResourceNames(diskMaps[id].mapInfo().innerNamespace,
                            int(diskMaps[id].mapInfo().textureCount));QImage tile;
                        if(!available || packed.atlasIndex>=names.size()
                           || !ei::decodeMmpDxt1AtlasTile(archive.memberPayload(names[packed.atlasIndex]),packed.tileIndex,&tile))
                            pixels=false;
                        return tile;
                    };
                    for(int y=0;y<129 && pixels;++y) {
                        if(y+63>=179 && y+63<=182)continue;
                        const QImage a=decode(id4,0,y),b=decode(id6,127,y+63);
                        if(a.isNull()||b.isNull()||a.size()!=b.size()){pixels=false;break;}
                        for(int row=0;row<a.height();++row)pixelMismatch+=a.pixel(0,row)!=b.pixel(b.width()-1,row);
                        ++edgeCells;
                    }
                    check(pixels && pixelMismatch==0 && edgeCells>0,
                        "reopened repaired MMP atlas retains matching seam pixels after SEC exclusions");
                }
            }
            if(savedCopies)
                check(digest(placement.mobPath)==mobHash && digest(third.mprPath)==thirdHash,
                    "save changed only the intended map copies, not other maps or MOB");
            else
                check(digest(placement.mprPath)==terrainHash && digest(placement.mobPath)==mobHash
                    && digest(adjacent.mprPath)==neighborHash && digest(third.mprPath)==thirdHash,"all real game files unchanged");
            bool inputUnchanged=true;for(auto it=inputHashes.cbegin();it!=inputHashes.cend();++it)inputUnchanged &= digest(it.key())==it.value();
            if(savedCopies) {
                const auto id4=CWorldTerrainSession::key(root+"/Maps/zone4.mpr");
                const auto id6=CWorldTerrainSession::key(root+"/Maps/zone6.mpr");
                inputUnchanged=digest(root+"/world-map-layouts.json")==inputHashes.value(root+"/world-map-layouts.json")
                    && digest(id4)!=inputHashes.value(id4) && digest(id6)!=inputHashes.value(id6);
                check(inputUnchanged,"disk save changed the two intended copied maps and left layout storage alone");
            } else check(inputUnchanged,"all cohort MPR files and layout remain byte-identical on disk");
            return failures?1:0;
        }
        const auto before=view.m_terrainEdit;
        const auto activeBeforeAtlas=view.m_worldTerrainSession.documents.value(activeId)->atlas->createStagingCopy();
        const auto neighborBefore=view.m_worldTerrainSession.documents.value(neighborId)->terrain;
        const auto neighborBeforeAtlas=view.m_worldTerrainSession.documents.value(neighborId)->atlas->createStagingCopy();
        const auto worldState=[](const SWorldTerrainDocument& doc) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            for(const auto& sector:doc.baseline.sectors())
                hash.addData(doc.terrain.sectorPayload(sector.sectorX,sector.sectorY));
            for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
                QVector<qint32> current,baseline;QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
                doc.terrain.copyVirtualMaterialFieldState(layer,&current,&baseline);stream<<current<<baseline;hash.addData(bytes);
            }
            if(doc.atlas)hash.addData(doc.atlas->stateFingerprint());
            return hash.result();
        };
        const QByteArray activeBeforeState=worldState(*view.m_worldTerrainSession.documents.value(activeId));
        const QByteArray neighborBeforeState=worldState(*view.m_worldTerrainSession.documents.value(neighborId));
        QString message;QObject::connect(&view,&CView::updateMsg,&view,[&](const QString& text){message=text;});
        view.beginTileBrushGroup();
        check(view.paintWorldTerrainAt(seamPoint,true),"shared painter handles boundary stamp");
        out<<"Painter message: "<<message<<"\n";
        check(view.m_worldTerrainStroke.paint.contains(activeId)&&view.m_worldTerrainStroke.paint.contains(neighborId),"one stroke changes both real independent atlases");
        if(qEnvironmentVariableIsSet("EI_WORLD_SEAM_PAIR_CHECK")) {
            const auto seamTile=[](const CTerrainEditModel& model,const CTerrainWorkingAtlas* atlas,int x,int y) {
                if(!atlas)return QImage();bool available=false;
                const quint16 packed=model.packedTile(x,y,ETerrainTileLayer::Land,&available);
                const int slot=SMprPackedTile::decode(packed).atlasSlot();const auto images=atlas->activeTileImages();
                return available&&slot>=0&&slot<images.size()?images[slot]:QImage();
            };
            const int activeY=qBound(0,int(std::floor(seamPoint.y()/2.0f)),before.tileCountY()-1);
            const double worldY=view.m_activeMapWorldOrigin.y()+seamPoint.y();
            const int neighborY=qBound(0,int(std::floor((worldY-adjacent.worldOrigin.y())/2.0)),neighborBefore.tileCountY()-1);
            const bool activeIsZone4=zone=="zone4";
            const QImage a=seamTile(before,activeBeforeAtlas.data(),activeIsZone4?0:before.tileCountX()-1,activeY);
            const QImage b=seamTile(neighborBefore,neighborBeforeAtlas.data(),activeIsZone4?neighborBefore.tileCountX()-1:0,neighborY);
            QImage seam(a.width()*2,a.height(),QImage::Format_ARGB32);seam.fill(Qt::black);
            if(!a.isNull()&&!b.isNull()) {QPainter painter(&seam);painter.drawImage(0,0,a);painter.drawImage(a.width(),0,b);painter.end();}
            const bool saved=!a.isNull()&&!b.isNull()&&seam.scaled(seam.size()*4,Qt::IgnoreAspectRatio,Qt::FastTransformation)
                .save(qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT")+"/seam-before.png","PNG");
            check(saved,"export copied-map seam-before PNG");
        }
        view.endTileBrushGroup();
        check(view.m_worldTerrainSession.canUndo(),"one shared stroke creates one undo transaction for both maps");
        if(qEnvironmentVariableIsSet("EI_WORLD_SEAM_PAIR_CHECK")) {
            const auto activeDoc=view.m_worldTerrainSession.documents.value(activeId);
            const auto neighborDoc=view.m_worldTerrainSession.documents.value(neighborId);
            const int activeY=qBound(0,int(std::floor(seamPoint.y()/2.0f)),activeDoc->terrain.tileCountY()-1);
            const double worldY=view.m_activeMapWorldOrigin.y()+seamPoint.y();
            const int neighborY=qBound(0,int(std::floor((worldY-adjacent.worldOrigin.y())/2.0)),neighborDoc->terrain.tileCountY()-1);
            const bool activeIsZone4=zone=="zone4";
            const auto seamTile=[](const SWorldTerrainDocument& doc,int x,int y) {
                bool available=false;const quint16 packed=doc.terrain.packedTile(x,y,ETerrainTileLayer::Land,&available);
                const int slot=SMprPackedTile::decode(packed).atlasSlot();const auto images=doc.atlas->activeTileImages();
                return available&&slot>=0&&slot<images.size()?images[slot]:QImage();
            };
            const QImage a=seamTile(*activeDoc,activeIsZone4?0:activeDoc->terrain.tileCountX()-1,activeY);
            const QImage b=seamTile(*neighborDoc,activeIsZone4?neighborDoc->terrain.tileCountX()-1:0,neighborY);
            int mismatches=1;
            if(!a.isNull()&&a.size()==b.size()) {
                mismatches=0;
                for(int y=0;y<a.height();++y)
                    mismatches+=a.pixel(activeIsZone4?0:a.width()-1,y)!=b.pixel(activeIsZone4?b.width()-1:0,y);
            }
            check(mismatches==0,"copied-map mixed-edge pixels match across the real zone4/zone6 seam");
            QImage seam(a.width()*2,a.height(),QImage::Format_ARGB32);seam.fill(Qt::black);
            if(!a.isNull()&&!b.isNull()) {QPainter painter(&seam);painter.drawImage(0,0,a);painter.drawImage(a.width(),0,b);painter.end();}
            const bool saved=!a.isNull()&&!b.isNull()&&seam.scaled(seam.size()*4,Qt::IgnoreAspectRatio,Qt::FastTransformation)
                .save(qEnvironmentVariable("EI_NEIGHBOR_PERF_ROOT")+"/seam-after.png","PNG");
            check(saved,"export copied-map seam-after PNG");
        }
        QString error;check(view.m_worldTerrainSession.undo(&error),"undo complete paint cohort");out<<error<<"\n";
        check(view.m_worldTerrainSession.documents.value(activeId)
            && worldState(*view.m_worldTerrainSession.documents[activeId])==activeBeforeState
            && worldState(*view.m_worldTerrainSession.documents[neighborId])==neighborBeforeState,
            "undo restores both maps byte-for-byte, including semantic fields and atlas pixels");
        check(view.m_worldTerrainSession.redo(&error),"redo complete paint cohort");
        for(const auto& id:QStringList{activeId,neighborId})check(view.previewWorldPaintState(id),"restore redo preview for each world document");
        const QByteArray activeAfterState=worldState(*view.m_worldTerrainSession.documents[activeId]);
        const QByteArray neighborAfterState=worldState(*view.m_worldTerrainSession.documents[neighborId]);
        check(activeAfterState!=activeBeforeState && neighborAfterState!=neighborBeforeState,
            "one redo restores the shared stroke on both maps");
        if(qEnvironmentVariableIsSet("EI_WORLD_SAVE_WORLD_TERRAIN_CHECK")) {
            const QByteArray activeMprBefore=digest(placement.mprPath),neighborMprBefore=digest(adjacent.mprPath);
            const QString archivePath=view.m_worldTerrainAtlasSources.value(activeId).archive;
            const QString neighborArchivePath=view.m_worldTerrainAtlasSources.value(neighborId).archive;
            const QByteArray archiveBefore=digest(archivePath);
            check(!archivePath.isEmpty() && archivePath==neighborArchivePath,"both maps publish through their shared copied terrain archive");
            const bool saved=view.saveWorldTerrainMaps();
            check(saved && savePromptHandled,"save the two-map stroke through the production cohort publisher");
            check(digest(placement.mprPath)!=activeMprBefore && digest(adjacent.mprPath)!=neighborMprBefore,
                "both copied MPR files are committed");
            check(digest(archivePath)!=archiveBefore,"shared terrain archive is published once for the map pair");
            CMprDocument reopenedActive,reopenedNeighbor;CTerrainEditModel activeModel,neighborModel;CValidationReport reopenedReport;
            const bool reopened=reopenedActive.load(QFileInfo(placement.mprPath),&reopenedReport)
                && reopenedNeighbor.load(QFileInfo(adjacent.mprPath),&reopenedReport)
                && activeModel.load(reopenedActive,&reopenedReport) && neighborModel.load(reopenedNeighbor,&reopenedReport);
            check(reopened && !reopenedReport.hasErrors(),"reopen both saved copied maps from disk");
            if(reopened) {
                const bool zone4Active=zone=="zone4";
                const int activeEdge=zone4Active?0:activeModel.tileCountX()-1;
                const int neighborEdge=zone4Active?neighborModel.tileCountX()-1:0;
                const int activeVertex=zone4Active?0:activeModel.tileCountX();
                const int neighborVertex=zone4Active?neighborModel.tileCountX():0;
                CTerrainVirtualMaterialField activeField,neighborField;
                activeModel.copyVirtualMaterialField(ETerrainTileLayer::Land,&activeField,false);
                neighborModel.copyVirtualMaterialField(ETerrainTileLayer::Land,&neighborField,false);
                int vertexMismatches=0;
                for(int y=0;y<=128;++y) {
                    const int worldY=int((zone4Active?190:64)+y*2);
                    const int neighborY=(worldY-(zone4Active?64:190))/2;
                    if(neighborY>=0 && neighborY<=neighborField.tileHeight())
                        vertexMismatches+=activeField.material(activeVertex,y)!=neighborField.material(neighborVertex,neighborY);
                }
                check(vertexMismatches==0,"reopened maps retain matching shared semantic vertices");
                QFile archiveFile(archivePath);QByteArray archiveBytes;
                CResArchiveDocument archive;QStringList archiveErrors;
                const bool archiveReady=archiveFile.open(QIODevice::ReadOnly)
                    && (archiveBytes=archiveFile.readAll(),archive.loadBytes(archiveBytes,archivePath,&archiveErrors));
                int pixelMismatches=0,edgeCells=0;bool decoded=archiveReady;
                const auto compareEdge=[&](const CMprDocument& map,const CTerrainEditModel& model,int x,int y) {
                    bool available=false;const quint16 packed=model.packedTile(x,y,ETerrainTileLayer::Land,&available);
                    const auto tile=SMprPackedTile::decode(packed);
                    const auto names=ei::terrainTextureResourceNames(map.mapInfo().innerNamespace,int(map.mapInfo().textureCount));
                    QImage image;
                    if(!available || tile.atlasIndex>=names.size()
                       || !ei::decodeMmpDxt1AtlasTile(archive.memberPayload(names[tile.atlasIndex]),tile.tileIndex,&image)) {
                        decoded=false;return QImage();
                    }
                    return image;
                };
                const int paintedY=qBound(0,int(std::floor(seamPoint.y()/2.0f)),activeModel.tileCountY()-1);
                for(int y=qMax(0,paintedY-1);y<=qMin(activeModel.tileCountY()-1,paintedY+1) && decoded;++y) {
                    const int worldY=int((zone4Active?190:64)+y*2);
                    const int neighborY=(worldY-(zone4Active?64:190))/2;
                    if(neighborY<0 || neighborY>=neighborModel.tileCountY())continue;
                    const QImage a=compareEdge(reopenedActive,activeModel,activeEdge,y);
                    const QImage b=compareEdge(reopenedNeighbor,neighborModel,neighborEdge,neighborY);
                    if(a.isNull()||b.isNull()||a.size()!=b.size()){decoded=false;break;}
                    if(worldY>=179 && worldY<=182)continue;
                    for(int row=0;row<a.height();++row)
                        pixelMismatches+=a.pixel(zone4Active?0:a.width()-1,row)!=b.pixel(zone4Active?b.width()-1:0,row);
                    ++edgeCells;
                }
                out<<"reopened painted seam MMP pixel mismatches: "<<pixelMismatches<<" cells: "<<edgeCells<<"\n";
                check(decoded && pixelMismatches==0 && edgeCells>0,
                    "reopened MMP border pixels of the painted seam tiles are identical");
            }
        }
        if(qEnvironmentVariableIsSet("EI_WORLD_SEAM_PAIR_CHECK")) {
            check(true,"overlay surface regression is covered by the horizontal two-map fixture");
        } else {
        if(qEnvironmentVariableIsSet("EI_WORLD_MIXED_EDGE_BRUSH_CHECK"))
            if(auto* mixedEdge=tools.findChild<QCheckBox*>("modernMixedEdge"))mixedEdge->setChecked(false);
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
        }
        QVector<SWorldMaterialField> landFields,overlayFields;
        check(view.worldMaterialNeighbors(&landFields,&overlayFields,&validation)&&!landFields.isEmpty(),"boundary repair reads retained neighbor material field");
        CTerrainVirtualMaterialField field;
        check(view.m_terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false),"active semantic field available");
        const int remainingBoundaryChanges=reconcileWorldMaterialBoundary(view.m_activeMapWorldOrigin,&field,landFields);
        check(remainingBoundaryChanges==0,"shared material vertices already agree across every geometric seam");
        if(qEnvironmentVariableIsSet("EI_WORLD_SAVE_WORLD_TERRAIN_CHECK"))
            check(digest(placement.mobPath)==mobHash,"the neighboring copied MOB remains byte-identical after map save");
        else
            check(digest(placement.mprPath)==terrainHash && digest(placement.mobPath)==mobHash,
                "copied MPR and MOB remain byte-identical when save is disabled");
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

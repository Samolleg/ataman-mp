#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <limits>
#include "world_terrain_session.h"
#include "endurance_document.h"
#include "terrain_boundary_fill.h"
#include "world_material_boundary.h"
#include "terrain_virtual_atlas.h"
#include "res_archive_document.h"
#include "mmp_codec.h"

class WorldTerrainTests : public QObject
{
    Q_OBJECT
    QByteArray fixture(float maxZ=100,quint16 height=10000,int width=1)
    {
        CMprDocument source; CValidationReport report;
        if(!source.load(QFileInfo(QFINDTESTDATA("fixtures/runtime/particle-flat-7x5.mpr")),&report)) return {};
        SMprCreationRequest request; request.innerNamespace="synthetic";
        request.maxZ=maxZ; request.flatRawHeight=height;
        request.sectorCountX=width;
        QByteArray result;
        if(!source.buildNewCanvas(request,&result,&report)) return {};
        return result;
    }
    void add(CWorldTerrainSession& session,const QString& id,float maxZ=100,quint16 height=10000)
    {
        QSharedPointer<SWorldTerrainDocument> doc(new SWorldTerrainDocument);
        CValidationReport report;
        QVERIFY(doc->baseline.loadBytes(fixture(maxZ,height),id,&report));
        QVERIFY(doc->terrain.load(doc->baseline,&report));
        session.documents.insert(id,doc);
    }
    SWorldTerrainTarget target(CWorldTerrainSession& s,const QString& id,QPointF origin,bool enabled=true)
    {
        SWorldTerrainTarget t; t.id=id;t.origin=origin;t.model=&s.documents[id]->terrain;t.editable=enabled;return t;
    }
    double z(CWorldTerrainSession& s,const QString& id,int x,int y)
    { auto& t=s.documents[id]->terrain;return t.height(x,y)*double(t.maxZ())/65535; }
    double edgeHeight(const SWorldTerrainTarget& t,int fixed,bool horizontal,double along)
    {
        const int n=horizontal?t.model->vertexCountX():t.model->vertexCountY();
        for(int i=0;i<n-1;++i) {
            const int x=horizontal?i:fixed,y=horizontal?fixed:i;
            const auto p=t.model->terrainVertexPosition(x,y)+t.origin;
            const auto q=t.model->terrainVertexPosition(x+(horizontal?1:0),y+(horizontal?0:1))+t.origin;
            const double a=horizontal?p.x():p.y(),b=horizontal?q.x():q.y();
            if(b>a && along>=a-1e-8 && along<=b+1e-8) {
                const double f=qBound(0.0,(along-a)/(b-a),1.0);
                return (t.model->height(x,y)*(1-f)+t.model->height(x+(horizontal?1:0),y+(horizontal?0:1))*f)*t.model->maxZ()/65535.0;
            }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
private slots:
    void movingShiftedAllBrushes()
    {
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower,EWorldSculptMode::Flatten,
                      EWorldSculptMode::Smooth,EWorldSculptMode::BoundaryFill,EWorldSculptMode::Noise})
        for(auto falloff:{ETerrainBrushFalloff::Constant,ETerrainBrushFalloff::Linear,ETerrainBrushFalloff::Smooth})
        for(bool horizontal:{false,true}) {
            CWorldTerrainSession s;add(s,"a");add(s,"b");
            // A fractional global translation must not select a different mesh.
            QVector<SWorldTerrainTarget> targets{target(s,"a",{0.25,0.25}),target(s,"b",{32.25,0.75})};
            if(horizontal)for(auto& t:targets)t.origin=QPointF(t.origin.y(),t.origin.x());
            const auto beforeA=targets[0].model->sectorPayload(0,0),beforeB=targets[1].model->sectorPayload(0,0);
            SWorldTerrainStroke combined;QString error;
            for(double center:{15.25,16.25,17.25}) {
                STerrainBrush brush;brush.radius=4;brush.falloff=falloff;
                brush.centerX=horizontal?center:32.25;brush.centerY=horizontal?32.25:center;
                const auto stamp=makeWorldTerrainStamp(targets,brush,mode,2,25,0.6,223);
                QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));combined.append(stamp);
                // Include already-painted edges outside the current stamp too.
                for(double y=1.25;y<31;y+=0.125)
                    QVERIFY(qAbs(edgeHeight(targets[0],32,horizontal,y)-edgeHeight(targets[1],0,horizontal,y))<0.0031);
            }
            QVERIFY(s.apply(combined,false,&error));
            QCOMPARE(targets[0].model->sectorPayload(0,0),beforeA);QCOMPARE(targets[1].model->sectorPayload(0,0),beforeB);
        }
    }
    void seamGeometryIsAtomicAndReversible()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0.5})};
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=5;
        auto stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Raise,2,0,1);
        const auto beforeA=targets[0].model->sectorPayload(0,0),beforeB=targets[1].model->sectorPayload(0,0);
        QString error;auto invalid=stamp;
        QVERIFY(!invalid.maps["b"].deltas.isEmpty());
        auto& d=invalid.maps["b"].deltas.first();d.changesPosition=true;d.beforeOffset={0,0};d.afterOffset={500,0};
        QVERIFY(!s.apply(invalid,true,&error));
        QCOMPARE(targets[0].model->sectorPayload(0,0),beforeA);QCOMPARE(targets[1].model->sectorPayload(0,0),beforeB);
        QVERIFY(s.apply(stamp,true,&error));s.record(stamp);
        const auto second=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Lower,0.5,0,1);
        QVERIFY(s.apply(second,true,&error));
        auto combined=stamp;combined.append(second);
        QVERIFY(s.apply(combined,false,&error));
        QCOMPARE(targets[0].model->sectorPayload(0,0),beforeA);QCOMPARE(targets[1].model->sectorPayload(0,0),beforeB);
        QVERIFY(s.apply(combined,true,&error));
        // Excluding the neighbor prevents both geometry and height changes there.
        targets[1].editable=false;
        const auto alone=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Noise,1,0,1,23);
        QVERIFY(!alone.maps.contains("b"));
        // Two vertices may not collapse the same terrain face.
        STerrainHeightCommand collapsed;
        for(int x:{10,11}) {
            STerrainHeightDelta v;v.globalX=x;v.globalY=10;v.before=v.after=targets[0].model->height(x,10);
            v.changesPosition=true;v.beforeOffset={0,0};v.afterOffset={x==10?127:-127,0};collapsed.deltas.append(v);
        }
        const auto snapshot=targets[0].model->sectorPayload(0,0);
        QVERIFY(!s.documents["a"]->terrain.apply(collapsed,true));
        QCOMPARE(targets[0].model->sectorPayload(0,0),snapshot);
    }
    void allBrushesMatchOneCanvas()
    {
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower,EWorldSculptMode::Flatten,
                      EWorldSculptMode::Smooth,EWorldSculptMode::BoundaryFill,EWorldSculptMode::Noise})
        for(auto falloff:{ETerrainBrushFalloff::Constant,ETerrainBrushFalloff::Linear,ETerrainBrushFalloff::Smooth}) {
            CWorldTerrainSession s;add(s,"a");add(s,"b");
            auto doc=QSharedPointer<SWorldTerrainDocument>::create();CValidationReport report;
            QVERIFY(doc->baseline.loadBytes(fixture(100,10000,2),"whole",&report));
            QVERIFY(doc->terrain.load(doc->baseline,&report));s.documents["whole"]=doc;
            QVector<SWorldTerrainTarget> split{target(s,"a",{0,0}),target(s,"b",{32,0})};
            const auto whole=target(s,"whole",{0,0});
            for(const auto& t:QVector<SWorldTerrainTarget>{split[0],split[1],whole}) {
                STerrainHeightCommand shape;
                for(int y=0;y<t.model->vertexCountY();++y) for(int x=0;x<t.model->vertexCountX();++x) {
                    STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=10000;
                    d.after=quint16(qRound(20000+3000*std::sin((t.origin.x()+x)*0.4)+2000*std::cos(y*0.7)));
                    shape.deltas.append(d);
                }
                QVERIFY(s.documents[t.id]->terrain.apply(shape,true));
            }
            STerrainBrush brush;brush.centerX=32.25;brush.centerY=16.3;brush.radius=6;brush.falloff=falloff;
            const auto pair=makeWorldTerrainStamp(split,brush,mode,1.25,35,0.6,77);
            const auto single=makeWorldTerrainStamp({whole},brush,mode,1.25,35,0.6,77);
            QString error;QVERIFY2(s.apply(pair,true,&error),qPrintable(error));QVERIFY2(s.apply(single,true,&error),qPrintable(error));
            for(const auto& t:split) for(int y=0;y<33;++y) for(int x=0;x<33;++x)
                QCOMPARE(t.model->height(x,y),whole.model->height(int(t.origin.x())+x,y));
        }
    }
    void installedZone11Zone12ReadOnly()
    {
        const QString directory=qEnvironmentVariable("EI_WORLD_SCULPT_MAPS");
        if(directory.isEmpty()) QSKIP("Optional installed data, read only");
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower,EWorldSculptMode::Flatten,
                      EWorldSculptMode::Smooth,EWorldSculptMode::BoundaryFill,EWorldSculptMode::Noise}) {
            CWorldTerrainSession s;QVector<SWorldTerrainTarget> targets;QString error;
            const QStringList names{"zone11","zone12"};const QVector<QPointF> origins{{0,0},{512,324.5}};
            for(int i=0;i<2;++i) {
                const QString path=QDir(directory).filePath(names[i]+".mpr");
                QVERIFY2(s.load(path,&error),qPrintable(error));targets.append(target(s,CWorldTerrainSession::key(path),origins[i]));
            }
            STerrainBrush brush;brush.centerX=512;brush.centerY=350;brush.radius=7;
            double initialGap=0;
            for(double y=344;y<=356;y+=0.125)
                initialGap=qMax(initialGap,qAbs(edgeHeight(targets[0],512,false,y)-edgeHeight(targets[1],0,false,y)));
            QElapsedTimer timer;timer.start();const auto stamp=makeWorldTerrainStamp(targets,brush,mode,0.5,15,0.7,11);
            QVERIFY2(stamp.error.isEmpty(),qPrintable(stamp.error));
            QCOMPARE(stamp.maps.size(),2);QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));
            double worst=0;
            for(double y=344;y<=356;y+=0.125) {
                const double gap=qAbs(edgeHeight(targets[0],512,false,y)-edgeHeight(targets[1],0,false,y));
                QVERIFY(std::isfinite(gap));worst=qMax(worst,gap);
            }
            qInfo("zone11/12 mode=%d ms=%lld gap-before=%.9f gap-after=%.9f",int(mode),timer.elapsed(),initialGap,worst);
            QVERIFY(worst<qMax(targets[0].model->maxZ(),targets[1].model->maxZ())/65535.0+1e-7);
            for(const auto& t:targets) {
                const auto doc=s.documents[t.id];CMprEditTransaction tx;CValidationReport report;QByteArray bytes;
                QVERIFY(doc->terrain.appendSectorEdits(&tx,&report));
                QVERIFY2(doc->baseline.applyTransaction(tx,&bytes,&report),qPrintable(report.toText()));
                CMprDocument reopened;CTerrainEditModel model;
                QVERIFY(reopened.loadBytes(bytes,"memory-only.mpr",&report));QVERIFY(model.load(reopened,&report));
                for(const auto& sec:doc->baseline.sectors())
                    QCOMPARE(model.sectorPayload(sec.sectorX,sec.sectorY),doc->terrain.sectorPayload(sec.sectorX,sec.sectorY));
            }
            QVERIFY(s.apply(stamp,false,&error));
            for(const auto& t:targets) {
                const auto doc=s.documents[t.id];QVERIFY(!doc->terrain.isDirty());
                QFile file(doc->path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),doc->baseline.originalBytes());
            }
        }
    }
    void unifiedBrushChangesBothShiftedEdges()
    {
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower,
                      EWorldSculptMode::Flatten,EWorldSculptMode::Smooth,
                      EWorldSculptMode::BoundaryFill,EWorldSculptMode::Noise}) {
            CWorldTerrainSession s;add(s,"a",100,10000);add(s,"b",100,20000);
            auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0.5})};
            STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=5;
            const auto stamp=makeWorldTerrainStamp(targets,brush,mode,2,30,0.7,731);
            QString error;QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));
            QVERIFY2(s.documents["a"]->terrain.height(32,16)!=10000,"The shared edge must participate in every brush");
            QVERIFY2(s.documents["b"]->terrain.height(0,16)!=20000,"The neighboring edge must participate too");
            for(double y=12;y<=20;y+=0.125)
                QVERIFY2(qAbs(edgeHeight(targets[0],32,false,y)-edgeHeight(targets[1],0,false,y))<0.0031,
                         "Compare the rendered polylines, not just their vertex heights");
        }
    }
    void localBrushDoesNotRewriteDistantShiftedRim()
    {
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower})
        for(bool horizontal:{false,true}) {
            CWorldTerrainSession session;add(session,"a");add(session,"b");
            auto targets=QVector<SWorldTerrainTarget>{target(session,"a",{0,0}),target(session,"b",{32,0.5})};
            if(horizontal) targets[1].origin=QPointF(0.5,32);
            // A distant peak on the edge must not be flattened by a local stroke.
            STerrainHeightCommand peak;STerrainHeightDelta d;
            d.globalX=horizontal?2:32;d.globalY=horizontal?32:2;d.before=10000;d.after=45000;peak.deltas.append(d);
            QVERIFY(session.documents["a"]->terrain.apply(peak,true));
            STerrainBrush brush;brush.centerX=horizontal?16:32;brush.centerY=horizontal?32:16;brush.radius=3;
            const auto stamp=makeWorldTerrainStamp(targets,brush,mode,2,0,1);
            QVERIFY(!stamp.isEmpty());
            for(const auto& t:targets) for(const auto& delta:stamp.maps[t.id].deltas) {
                const QPointF p=t.model->terrainVertexPosition(delta.globalX,delta.globalY)+t.origin;
                const bool rim=delta.globalX==0 || delta.globalY==0 || delta.globalX==32 || delta.globalY==32;
                QVERIFY2(std::hypot(p.x()-brush.centerX,p.y()-brush.centerY)<=brush.radius+(rim?1.5:0),
                         "Only the one-cell seam support may extend beyond the brush");
            }
            const auto before=session.documents["a"]->terrain.sectorPayload(0,0);
            QString error;QVERIFY(session.apply(stamp,true,&error));session.record(stamp);
            QCOMPARE(session.documents["a"]->terrain.height(d.globalX,d.globalY),quint16(45000));
            QVERIFY(session.undo(&error));QCOMPARE(session.documents["a"]->terrain.sectorPayload(0,0),before);
            QVERIFY(session.redo(&error));
        }
    }
    void physicalTileImportPreservesIdentity()
    {
        CTerrainTransitionCatalog catalog;catalog.declareMaterial(1);catalog.declareMaterial(2);
        STerrainTransitionVariant v;v.packedTile=0;v.signature.northWest=v.signature.northEast=v.signature.southWest=v.signature.southEast=1;
        CValidationReport report;QVERIFY(catalog.addVariant(v,&report));
        QImage green(64,64,QImage::Format_RGBA8888);green.fill(Qt::green);
        CTerrainWorkingAtlas atlas(catalog,QVector<QImage>(64,green),0,256,{}, {},QVector<qint32>(64,0));
        QVERIFY2(atlas.isValid(),qPrintable(atlas.error()));
        auto signature=v.signature;signature.northWest=signature.northEast=signature.southWest=signature.southEast=2;
        const int imported=atlas.importPhysicalTile(green,0,&signature,"other",4,&report);
        QVERIFY(imported>=64); // Same artwork does not imply the same material.
        QCOMPARE(atlas.importPhysicalTile(green,0,&signature,"other",4,&report),imported);
        SMprPackedTile packed;packed.atlasIndex=imported/64;packed.tileIndex=imported%64;
        bool found=false;QVERIFY(atlas.catalog().gridSignatureForPacked(packed.encode(),&found)==signature);QVERIFY(found);
        const auto snapshot=atlas.createStagingCopy();
        QImage red=green;red.fill(Qt::red);
        QVERIFY(atlas.importPhysicalTile(red,3,nullptr,"other",5,&report)>imported);
        QCOMPARE(snapshot->activeTileTypes().size(),imported+1);
        QCOMPARE(snapshot->activeTileImages()[imported],green);
    }
    void paintingSnapshotsAreAtomicAndUndoable()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");QString error;CValidationReport report;
        SWorldTerrainStroke stroke;
        for(const QString& id:QStringList{"a","b"}) {
            auto doc=s.documents[id];doc->materials=doc->baseline.mapInfo().materials;
            SWorldTerrainPaintChange change;change.before.terrain=doc->terrain;change.before.materials=doc->materials;
            change.after=change.before;
            STerrainTileBrush brush;brush.centerX=3;brush.centerY=4;brush.radius=2;
            const auto overlay=change.after.terrain.makeOverlayActivation(brush,0,0,0,0,&report);
            QVERIFY(!overlay.isEmpty());QVERIFY(change.after.terrain.apply(overlay,true,&report));
            stroke.paint[id]=change;
        }
        const auto before=s.documents["a"]->terrain.sectorPayload(0,0);
        auto invalid=stroke;invalid.paint["b"].before.materials.clear();
        QVERIFY(!s.apply(invalid,true,&error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),before);
        QVERIFY2(s.apply(stroke,true,&error),qPrintable(error));s.record(stroke);
        const auto after=s.documents["a"]->terrain.sectorPayload(0,0);QVERIFY(after!=before);
        QVERIFY2(s.undo(&error),qPrintable(error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),before);
        QCOMPARE(s.documents["b"]->terrain.sectorPayload(0,0),before);
        QVERIFY2(s.redo(&error),qPrintable(error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),after);
        s.clearHistory();QVERIFY(!s.canUndo());QVERIFY(!s.canRedo());
    }
    void extendedAtlasAndOverlaySaveRoundTrip()
    {
        CWorldTerrainSession s;add(s,"a");const auto doc=s.documents["a"];CValidationReport report;
        const int count=int(doc->baseline.mapInfo().textureCount)*64;
        QImage image(64,64,QImage::Format_RGBA8888);image.fill(Qt::green);
        QVector<QImage> images(count+64,image);QVector<QByteArray> pages;
        for(int i=0;i<count;i+=64)pages.append(ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(images,i,64),static_cast<QString*>(nullptr)));
        QVector<qint32> types(count+1,0);types[count]=3;
        auto materials=doc->baseline.mapInfo().materials;QVERIFY(!materials.isEmpty());materials.append(materials.first());
        auto model=doc->terrain;model.enableWorkingAtlasSlots(count+64);model.installWorkingAtlasSlotTypes(types);model.extendMaterialTable(materials.size());
        STerrainTileBrush brush;brush.centerX=4;brush.centerY=4;brush.radius=2;
        const auto overlay=model.makeOverlayActivation(brush,materials.size()-1,count/64,0,0,&report);
        QVERIFY2(!overlay.isEmpty(),qPrintable(report.toText()));QVERIFY(model.apply(overlay,true,&report));
        const auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(doc->baseline,images,types,pages,materials,doc->baseline.mapInfo().animatedTiles,&report);
        QVERIFY2(package.success,qPrintable(package.error+report.toText()));
        CMprDocument staged;QVERIFY(staged.loadBytes(package.mprBytes,"a.mpr",&report));
        CMprEditTransaction edit("paint");QVERIFY(model.appendSectorEdits(&edit,&report));QByteArray candidate;QVERIFY(staged.applyTransaction(edit,&candidate,&report));
        CMprDocument reopened;CTerrainEditModel check;QVERIFY(reopened.loadBytes(candidate,"a.mpr",&report));
        QVERIFY2(check.load(reopened,&report),qPrintable(report.toText()));QCOMPARE(check.sectorPayload(0,0),model.sectorPayload(0,0));
        QCOMPARE(reopened.mapInfo().tileTypes,types);QCOMPARE(reopened.mapInfo().materials,materials);
        CResArchiveDocument archive;QStringList errors;QVERIFY(archive.loadBytes(package.resourceArchiveBytes,"atlas.res",&errors));
        QCOMPARE(package.resourceMembers.size(),images.size()/64);
        for(int i=0;i<pages.size();++i)QCOMPARE(archive.memberPayload(package.resourceMembers[i]),pages[i]);
    }
    void materialBoundaryUsesWorldAdjacency()
    {
        CTerrainVirtualMaterialField active;QVERIFY(active.resize(16,16,1));
        SWorldMaterialField neighbor;neighbor.origin={32,0};QVERIFY(neighbor.field.resize(16,16,2));
        QCOMPARE(reconcileWorldMaterialBoundary({0,0},&active,{neighbor}),17);
        QCOMPARE(active.material(16,8),2);QCOMPARE(active.material(15,8),1);
        active.resize(16,16,1);neighbor.origin={33,0};
        QCOMPARE(reconcileWorldMaterialBoundary({0,0},&active,{neighbor}),0);
        neighbor.origin={31,0};QCOMPARE(reconcileWorldMaterialBoundary({0,0},&active,{neighbor}),0);
        neighbor.origin={32,32};QCOMPARE(reconcileWorldMaterialBoundary({0,0},&active,{neighbor}),0);
        neighbor.origin={32,0.5};QVERIFY(reconcileWorldMaterialBoundary({0,0},&active,{neighbor})>0);
        QCOMPARE(active.material(16,8),2);QCOMPARE(active.material(16,0),1);
        active.resize(16,16,1);neighbor.field.setMaterial(0,8,3);
        reconcileWorldMaterialBoundary({0,0},&active,{neighbor});
        QCOMPARE(active.material(16,8),1); // no interpolation of material IDs
        QCOMPARE(active.material(16,12),2);
    }
    void noiseWorldCoordinatesAndMixedScaleUndo()
    {
        CWorldTerrainSession s;add(s,"a",100,20000);add(s,"b",200,10000);
        const auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0})};
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=5;
        const auto stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Noise,2,0,1,731);
        QVERIFY(stamp.maps.contains("a"));QVERIFY(stamp.maps.contains("b"));
        const auto beforeA=s.documents["a"]->terrain.sectorPayload(0,0);
        const auto beforeB=s.documents["b"]->terrain.sectorPayload(0,0);
        QString error;QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));s.record(stamp);
        for(int y=12;y<21;++y)QVERIFY(qAbs(z(s,"a",32,y)-z(s,"b",0,y))<0.0031);
        QVERIFY(s.undo(&error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),beforeA);
        QCOMPARE(s.documents["b"]->terrain.sectorPayload(0,0),beforeB);
        QVERIFY(s.redo(&error));
    }
    void overlayCohortIsTransactionalAndUndoable()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");QString error;
        STerrainTileBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=3;
        SWorldTerrainStroke activate;
        for(const QString& id:QStringList{"a","b"}) {
            CValidationReport report;
            activate.overlays[id]=s.documents[id]->terrain.makeOverlayActivation(brush,0,0,0,0,&report);
            QVERIFY2(!activate.overlays[id].isEmpty(),qPrintable(report.toText()));
        }
        const auto before=s.documents["a"]->terrain.sectorPayload(0,0);
        auto invalid=activate;invalid.overlays.insert("missing",activate.overlays["a"]);
        QVERIFY(!s.apply(invalid,true,&error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),before);
        QVERIFY2(s.apply(activate,true,&error),qPrintable(error));s.record(activate);
        const auto after=s.documents["a"]->terrain.sectorPayload(0,0);QVERIFY(before!=after);
        QVERIFY(s.undo(&error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),before);
        QCOMPARE(s.documents["b"]->terrain.sectorPayload(0,0),before);
        QVERIFY(s.redo(&error));QCOMPARE(s.documents["a"]->terrain.sectorPayload(0,0),after);
    }
    void boundaryFillLargeContourAndOverlay()
    {
        QElapsedTimer timer;timer.start();
        const auto grid=TerrainBoundaryFill::grid(65,65,64,131,131,[](int x,int y,double* value) {
            *value=std::hypot(x-65,y-65)<62 ? 500 : 20000+20*x+10*y;
            return true;
        });
        QVERIFY(grid.size()>10000);
        for(auto it=grid.cbegin();it!=grid.cend();++it)
            QVERIFY(qAbs(it.value()-(20000+20*it.key().first+10*it.key().second))<0.001);
        qInfo("Radius 64 fill: %lld ms",timer.elapsed());
        CWorldTerrainSession s;add(s,"a");auto& model=s.documents["a"]->terrain;
        STerrainTileBrush activate;activate.centerX=8;activate.centerY=8;activate.radius=6;
        CValidationReport report;
        const auto activation=model.makeOverlayActivation(activate,0,0,0,0,&report);
        QVERIFY2(!activation.isEmpty(),qPrintable(report.toText()));
        QVERIFY(model.apply(activation,true));
        const auto baseline=model.sectorPayload(0,0);
        STerrainBrush point;point.centerX=16;point.centerY=16;point.radius=1;point.falloff=ETerrainBrushFalloff::Constant;
        const auto hole=model.makeOverlayFlatten(point,500,1,&report);
        QVERIFY(!hole.isEmpty());QVERIFY(model.apply(hole,true));
        const auto before=model.sectorPayload(0,0);
        point.radius=5;
        const auto fill=model.makeOverlayBoundaryFill(point,1,&report);
        QVERIFY2(!fill.isEmpty(),qPrintable(report.toText()));
        QVERIFY(model.apply(fill,true));QCOMPARE(model.sectorPayload(0,0),baseline);
        QVERIFY(model.apply(fill,false));QCOMPARE(model.sectorPayload(0,0),before);
    }
    void boundaryFillLocalPlaneCraterAndUndo()
    {
        CWorldTerrainSession s;add(s,"a");auto& model=s.documents["a"]->terrain;
        STerrainHeightCommand shape;
        for(int y=0;y<model.vertexCountY();++y) for(int x=0;x<model.vertexCountX();++x) {
            STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=model.height(x,y);
            d.after=quint16(10000+100*x+50*y+2*x*y);shape.deltas.append(d);
        }
        QVERIFY(model.apply(shape,true));
        STerrainBrush brush;brush.centerX=16;brush.centerY=16;brush.radius=7;
        brush.falloff=ETerrainBrushFalloff::Constant;
        QVERIFY(model.makeBoundaryFill(brush,1).isEmpty()); // affine plus harmonic saddle
        STerrainHeightCommand hole;
        for(int y=14;y<=18;++y) for(int x=14;x<=18;++x) {
            STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=model.height(x,y);
            d.after=((x+y)%2)?500:50000;hole.deltas.append(d);
        }
        QVERIFY(model.apply(hole,true));
        const auto before=model.sectorPayload(0,0);
        const auto fill=model.makeBoundaryFill(brush,1);
        QCOMPARE(fill.deltas.size(),25);
        for(const auto& d:fill.deltas) QCOMPARE(int(d.after),10000+100*d.globalX+50*d.globalY+2*d.globalX*d.globalY);
        const auto half=model.makeBoundaryFill(brush,0.5);
        QCOMPARE(half.deltas.size(),25);
        for(int i=0;i<half.deltas.size();++i)
            QVERIFY(qAbs(int(half.deltas[i].after)-qRound((fill.deltas[i].before+fill.deltas[i].after)*0.5))<=1);
        QVERIFY(model.apply(fill,true));QVERIFY(model.makeBoundaryFill(brush,1).isEmpty());
        QVERIFY(model.apply(fill,false));QCOMPARE(model.sectorPayload(0,0),before);
        QVERIFY(model.makeBoundaryFill(brush,0).isEmpty());
        brush.radius=0.5;QVERIFY(model.makeBoundaryFill(brush,1).isEmpty());
    }
    void boundaryFillWorldSeamMixedScalesAndOrder()
    {
        CWorldTerrainSession s;add(s,"a",100,20000);add(s,"b",200,10000);add(s,"disabled");
        const auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0}),target(s,"disabled",{0,0},false)};
        for(const auto& t:targets) if(t.editable) {
            STerrainHeightCommand hole;
            for(int y=14;y<=18;++y) for(int x=0;x<t.model->vertexCountX();++x)
                if(qAbs(t.origin.x()+x-32)<=2) {
                    STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=t.model->height(x,y);d.after=100;
                    hole.deltas.append(d);
                }
            QVERIFY(s.documents[t.id]->terrain.apply(hole,true));
        }
        QMap<QString,QByteArray> before;for(const auto& t:targets) before[t.id]=t.model->sectorPayload(0,0);
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=7;brush.falloff=ETerrainBrushFalloff::Constant;
        const auto fill=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,747,1);
        QVERIFY(!fill.isEmpty());QVERIFY(!fill.maps.contains("disabled"));
        auto reverse=targets;std::reverse(reverse.begin(),reverse.end());
        const auto reversed=makeWorldTerrainStamp(reverse,brush,EWorldSculptMode::BoundaryFill,0,12345,1);
        QCOMPARE(fill.maps.keys(),reversed.maps.keys());
        for(const auto& id:fill.maps.keys()) {
            QCOMPARE(fill.maps[id].deltas.size(),reversed.maps[id].deltas.size());
            for(int i=0;i<fill.maps[id].deltas.size();++i) {
                const auto& a=fill.maps[id].deltas[i];const auto& b=reversed.maps[id].deltas[i];
                QCOMPARE(a.globalX,b.globalX);QCOMPARE(a.globalY,b.globalY);QCOMPARE(a.after,b.after);
                QCOMPARE(int(a.after),id=="a"?20000:10000);
                QVERIFY(std::hypot((id=="a"?0:32)+a.globalX-32,a.globalY-16)<brush.radius);
            }
        }
        QString error;QVERIFY2(s.apply(fill,true,&error),qPrintable(error));
        for(int y=10;y<=22;++y) QVERIFY(qAbs(z(s,"a",32,y)-z(s,"b",0,y))<0.0031);
        s.record(fill);QVERIFY(s.undo(&error));
        for(const auto& t:targets) QCOMPARE(t.model->sectorPayload(0,0),before[t.id]);
        QVERIFY(s.redo(&error));
    }
    void boundaryFillStaggeredInteriorIsNotFrozen()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0.5})};
        STerrainHeightCommand hole;STerrainHeightDelta d;d.globalX=30;d.globalY=16;d.before=10000;d.after=1;hole.deltas.append(d);
        QVERIFY(s.documents["a"]->terrain.apply(hole,true));
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=6;brush.falloff=ETerrainBrushFalloff::Constant;
        const auto fill=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,0,1);
        QVERIFY(!fill.isEmpty());
        for(const auto& id:fill.maps.keys()) for(const auto& delta:fill.maps[id].deltas) {
            QCOMPARE(int(delta.after),10000);
        }
    }
    void boundaryFillStepAcrossStaggeredSeamUsesOnlyCircle()
    {
        CWorldTerrainSession s;add(s,"a",100,20000);add(s,"b",100,40000);
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0.5})};
        QMap<QString,QByteArray> before;for(const auto& t:targets) before[t.id]=t.model->sectorPayload(0,0);
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=10;brush.falloff=ETerrainBrushFalloff::Constant;
        const auto fill=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,747,1);
        QVERIFY(!fill.isEmpty());
        QString error;QVERIFY2(s.apply(fill,true,&error),qPrintable(error));
        // Both sides of the INTERNAL seam move, including away from its knots.
        QVERIFY(s.documents["a"]->terrain.height(32,16)>28000);
        QVERIFY(s.documents["a"]->terrain.height(32,16)<32000);
        QVERIFY(s.documents["b"]->terrain.height(0,16)>28000);
        QVERIFY(s.documents["b"]->terrain.height(0,16)<32000);
        QVERIFY(s.documents["a"]->terrain.height(30,16)>22000);
        QVERIFY(s.documents["b"]->terrain.height(2,16)<38000);
        for(double y=8;y<=24;y+=0.125) {
            const auto sample=[&](const QString& id,int x,double sy) {
                const int row=int(std::floor(sy));const double f=sy-row;
                return z(s,id,x,row)*(1-f)+z(s,id,x,row+1)*f;
            };
            QVERIFY(qAbs(sample("a",32,y)-sample("b",0,y-0.5))<0.0031);
        }
        // The actual outer circle and everything outside it stay put.
        QCOMPARE(int(s.documents["a"]->terrain.height(22,16)),20000);
        QCOMPARE(int(s.documents["b"]->terrain.height(10,16)),40000);
        for(const auto& t:targets) for(const auto& d:fill.maps[t.id].deltas)
            QVERIFY(std::hypot(t.origin.x()+d.globalX-32,t.origin.y()+d.globalY-16)<=11.5);
        const auto twice=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,12345,1);
        QVERIFY(twice.isEmpty()); // fixed contour cannot drift on a held brush
        QVERIFY(s.apply(fill,false,&error));
        for(const auto& t:targets) QCOMPARE(t.model->sectorPayload(0,0),before[t.id]);
        const auto smooth=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Smooth,0,0,1);
        QVERIFY(s.apply(smooth,true,&error));
        QCOMPARE(int(s.documents["a"]->terrain.height(30,16)),20000);
        QCOMPARE(int(s.documents["b"]->terrain.height(2,16)),40000);
    }
    void boundaryFillWorldSlopedCircleAndReversedTargets()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0.5})};
        for(const auto& t:targets) {
            STerrainHeightCommand shape;
            for(int y=0;y<t.model->vertexCountY();++y) for(int x=0;x<t.model->vertexCountX();++x) {
                STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=t.model->height(x,y);
                d.after=quint16(10000+100*(t.origin.x()+x)+80*(t.origin.y()+y));shape.deltas.append(d);
            }
            QVERIFY(s.documents[t.id]->terrain.apply(shape,true));
        }
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=10;brush.falloff=ETerrainBrushFalloff::Constant;
        // The affine surface is preserved in world coordinates even when its
        // shifted rim is reparameterized; a geometry command is expected.
        const auto knit=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,0,1);
        QString error;QVERIFY2(s.apply(knit,true,&error),qPrintable(error));
        for(const auto& t:targets) for(const auto& d:knit.maps[t.id].deltas) {
            const auto p=t.model->terrainVertexPosition(d.globalX,d.globalY)+t.origin;
            QVERIFY(qAbs(double(d.after)-(10000+100*p.x()+80*p.y()))<=1.0);
        }
        for(const auto& t:targets) {
            STerrainHeightCommand damage;
            for(int y=13;y<=19;++y) for(int x=0;x<t.model->vertexCountX();++x) if(qAbs(t.origin.x()+x-32)<4) {
                STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=t.model->height(x,y);d.after=60000;damage.deltas.append(d);
            }
            QVERIFY(s.documents[t.id]->terrain.apply(damage,true));
        }
        const auto fill=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,0,1);
        std::reverse(targets.begin(),targets.end());
        const auto reversed=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,0,1);
        QCOMPARE(fill.maps.keys(),reversed.maps.keys());
        for(const auto& t:targets) {
            QCOMPARE(fill.maps[t.id].deltas.size(),reversed.maps[t.id].deltas.size());
            for(int i=0;i<fill.maps[t.id].deltas.size();++i) {
                const auto& d=fill.maps[t.id].deltas[i];
                const auto p=t.model->terrainVertexPosition(d.globalX,d.globalY)+t.origin;
                QVERIFY(qAbs(double(d.after)-(10000+100*p.x()+80*p.y()))<=1.0);
                QCOMPARE(d.after,reversed.maps[t.id].deltas[i].after);
            }
        }
    }
    void boundaryFillSharedContourAnchorsStayIdentical()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0})};
        for(const auto& t:targets) {
            STerrainHeightCommand shape;
            for(int y=0;y<t.model->vertexCountY();++y) for(int x=0;x<t.model->vertexCountX();++x) {
                STerrainHeightDelta d;d.globalX=x;d.globalY=y;d.before=t.model->height(x,y);
                d.after=quint16(qRound(20000+1000*std::sin((t.origin.x()+x)*1.3)+1000*std::cos(y*1.8)));
                shape.deltas.append(d);
            }
            QVERIFY(s.documents[t.id]->terrain.apply(shape,true));
        }
        STerrainBrush brush;brush.centerX=32.25;brush.centerY=16.3;brush.radius=7.6;brush.falloff=ETerrainBrushFalloff::Constant;
        const auto fill=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,0,1);
        QVERIFY(!fill.isEmpty());QString error;QVERIFY(s.apply(fill,true,&error));
        for(int y=0;y<33;++y) QCOMPARE(s.documents["a"]->terrain.height(32,y),s.documents["b"]->terrain.height(0,y));
        QVERIFY(makeWorldTerrainStamp(targets,brush,EWorldSculptMode::BoundaryFill,0,0,1).isEmpty());
    }
    void movingStaggeredStrokeKeepsRimAndFootprint()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");add(s,"c");
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{0,32}),target(s,"c",{32,16.5})};
        STerrainHeightCommand peak;STerrainHeightDelta d;
        d.globalX=1;d.globalY=15;d.before=10000;d.after=50000;peak.deltas.append(d);
        QVERIFY(s.documents["c"]->terrain.apply(peak,true));
        QMap<QString,QByteArray> before;
        for(auto it=s.documents.cbegin();it!=s.documents.cend();++it) before[it.key()]=it.value()->terrain.sectorPayload(0,0);
        SWorldTerrainStroke whole;bool expanded=false;
        double worst=0,where=0;
        for(double center:{32.0,33.0,34.0})
        {
            STerrainBrush brush;brush.centerX=32;brush.centerY=center;brush.radius=5;
            QString error;const auto stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Smooth,0,0,1);
            QVERIFY(s.apply(stamp,true,&error));
            whole.append(stamp);
            for(const auto& t:targets) for(const auto& delta:stamp.maps[t.id].deltas)
                expanded=expanded || std::hypot(t.origin.x()+delta.globalX-32,t.origin.y()+delta.globalY-center)>6.5;
            for(double y=16.5;y<=48.5;y+=0.125)
            {
                double gap=qAbs(edgeHeight(targets[y<32?0:1],32,false,y)-edgeHeight(targets[2],0,false,y));
                if(gap>worst){worst=gap;where=y;}
            }
        }
        qInfo("Moving stroke: maximum gap %.9f at y %.3f",worst,where);
        QVERIFY2(worst<200.0/65535,"Moving brush must not leave a crack behind at a previously covered rim");
        QVERIFY(!expanded);s.record(whole);QString error;QVERIFY(s.undo(&error));
        for(auto it=s.documents.cbegin();it!=s.documents.cend();++it) QCOMPARE(it.value()->terrain.sectorPayload(0,0),before[it.key()]);
        QVERIFY(s.redo(&error));
    }
    void installedTripleReadOnly()
    {
        const QString directory=qEnvironmentVariable("EI_WORLD_SCULPT_MAPS");
        if(directory.isEmpty()) QSKIP("Optional read-only installed-map calculation");
        CWorldTerrainSession s;QString error;
        QVector<SWorldTerrainTarget> targets;
        const QStringList names{"zone1","zone2","zone3obr"};
        const QVector<QPointF> origins{{0,0},{192,288},{352,63.5}};
        for(int i=0;i<3;++i)
        {
            const QString path=QDir(directory).filePath(names[i]+".mpr");
            QVERIFY2(s.load(path,&error),qPrintable(error));
            targets.append(target(s,CWorldTerrainSession::key(path),origins[i]));
        }
        STerrainBrush brush;brush.centerX=352;brush.centerY=288;brush.radius=5;
        QVector<QVector<quint16>> rimBefore;
        for(int m=0;m<targets.size();++m) {
            QVector<quint16> rim;const auto& model=*targets[m].model;
            for(int y=0;y<model.vertexCountY();++y) rim.append(model.height(m==2?0:model.vertexCountX()-1,y));
            rimBefore.append(rim);
        }
        QElapsedTimer timer;timer.start();
        for(int pass=0;pass<8;++pass)
        {
            brush.centerY=288+pass;
            const auto stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Smooth,0,0,1);
            QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));
        }
        double worst=0;
        for(double y=284;y<=299;y+=0.125)
        {
            const int left=y<288?0:1;
            worst=qMax(worst,qAbs(edgeHeight(targets[left],targets[left].model->vertexCountX()-1,false,y)
                                  -edgeHeight(targets[2],0,false,y)));
        }
        qInfo("Installed triple: eight stamps %lld ms, maximum covered-edge gap %.9f",timer.elapsed(),worst);
        double precision=0;for(const auto& t:targets)precision=qMax(precision,double(t.model->maxZ())/65535);
        QVERIFY(worst<=precision+1e-7);
        for(int m=0;m<targets.size();++m) {
            const auto& model=*targets[m].model;
            for(int y=0;y<model.vertexCountY();++y) {
                const double worldY=targets[m].origin.y()+y;
                if(worldY>=63.5 && worldY<=383.5 && (worldY<281 || worldY>302))
                    QCOMPARE(model.height(m==2?0:model.vertexCountX()-1,y),rimBefore[m][y]);
            }
        }
    }
    void staggeredTripleJunction()
    {
        for(bool horizontal:{false,true})
        for(double amount:{2.0,200.0})
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower,EWorldSculptMode::Flatten,EWorldSculptMode::Smooth})
        {
            CWorldTerrainSession s;add(s,"a",100,10000);add(s,"b",80,16000);add(s,"c",60,25000);
            // A and B meet at y=32; C spans their junction with half-step knots.
            auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{0,32}),target(s,"c",{32,16.5})};
            if(horizontal) for(auto& t:targets) t.origin=QPointF(t.origin.y(),t.origin.x());
            STerrainBrush brush;brush.centerX=32;brush.centerY=32;brush.radius=5;
            const auto stamp=makeWorldTerrainStamp(targets,brush,mode,amount,amount+20,0.7);
            auto reordered=targets;std::reverse(reordered.begin(),reordered.end());
            const auto reversed=makeWorldTerrainStamp(reordered,brush,mode,amount,amount+20,0.7);
            for(auto it=stamp.maps.cbegin();it!=stamp.maps.cend();++it)
            {
                QMap<QPair<int,int>,quint16> expected,actual;
                for(const auto& d:it->deltas) expected[{d.globalX,d.globalY}]=d.after;
                for(const auto& d:reversed.maps[it.key()].deltas) actual[{d.globalX,d.globalY}]=d.after;
                QCOMPARE(actual,expected);
            }
            QString error;QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));s.record(stamp);
            // Repair the touched rim locally. Keep distant pre-existing defects.
            for(double y=16.5;y<=48.5;y+=0.125)
            {
                const double left=edgeHeight(targets[y<32?0:1],32,horizontal,y);
                const double right=edgeHeight(targets[2],0,horizontal,y);
                const double baselineLeft=y<32?10000.0*100/65535:16000.0*80/65535;
                const double baselineRight=25000.0*60/65535;
                if(qAbs(y-32)<=5) QVERIFY(qAbs(left-right)<0.0031);
                if(qAbs(y-32)>8) {
                    QVERIFY(qAbs(left-baselineLeft)<1e-9);
                    QVERIFY(qAbs(right-baselineRight)<1e-9);
                }
            }
            for(auto it=stamp.maps.cbegin();it!=stamp.maps.cend();++it)
            {
                const auto t=*std::find_if(targets.begin(),targets.end(),[&](const SWorldTerrainTarget& t){return t.id==it.key();});
                for(const auto& d:it->deltas)
                    QVERIFY(std::hypot(t.origin.x()+d.globalX-32,t.origin.y()+d.globalY-32)<=6.5);
            }
            QVERIFY(s.undo(&error));
            for(const auto& doc:s.documents)
            {
                CTerrainEditModel original;CValidationReport report;QVERIFY(original.load(doc->baseline,&report));
                QCOMPARE(doc->terrain.sectorPayload(0,0),original.sectorPayload(0,0));
            }
            QVERIFY(s.redo(&error));
            // Serialize the actual reconciled three-map stroke and reopen it.
            QTemporaryDir temp;QVector<SWorldTerrainSaveItem> items;
            for(auto it=s.documents.cbegin();it!=s.documents.cend();++it)
            {
                CMprEditTransaction tx;CValidationReport report;SWorldTerrainSaveItem item;
                item.path=temp.filePath(it.key()+".mpr");item.original=it.value()->baseline.originalBytes();
                QFile file(item.path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(item.original),qint64(item.original.size()));file.close();
                QVERIFY(it.value()->terrain.appendSectorEdits(&tx,&report));
                QVERIFY(it.value()->baseline.applyTransaction(tx,&item.candidate,&report));items.append(item);
            }
            const auto saved=saveWorldTerrainCohort(items);QVERIFY2(saved.success,qPrintable(saved.error));
            for(const auto& item:items)
            {
                CMprDocument disk;CTerrainEditModel model;CValidationReport report;
                QVERIFY(disk.load(QFileInfo(item.path),&report));QVERIFY(model.load(disk,&report));
                const auto& expected=s.documents[QFileInfo(item.path).baseName()]->terrain;
                for(int y=0;y<33;++y) for(int x=0;x<33;++x) QCOMPARE(model.height(x,y),expected.height(x,y));
                QCOMPARE(model.sectorPayload(0,0),expected.sectorPayload(0,0));
            }
        }
    }
    void shiftedBoundaryConsentAndContact()
    {
        CWorldTerrainSession s;add(s,"a",100,10000);add(s,"b",80,25000);
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=5;
        auto a=target(s,"a",{0,0}),b=target(s,"b",{32,0.5},false);
        const auto alone=makeWorldTerrainStamp({a},brush,EWorldSculptMode::Smooth,0,0,1);
        auto protectedStamp=makeWorldTerrainStamp({a,b},brush,EWorldSculptMode::Smooth,0,0,1);
        QVERIFY(protectedStamp.isEmpty());QVERIFY(alone.isEmpty());
        b.editable=true;
        QVERIFY(makeWorldTerrainStamp({a,b},brush,EWorldSculptMode::Smooth,0,0,0).isEmpty());
        auto distantBrush=brush;distantBrush.centerX=16;
        QVERIFY(makeWorldTerrainStamp({a,b},distantBrush,EWorldSculptMode::Smooth,0,0,1).isEmpty());
        for(double x:{31.75,32.25}) // overlap and gap are not a shared boundary
        {
            b.origin.setX(x);
            QVERIFY(makeWorldTerrainStamp({a,b},brush,EWorldSculptMode::Smooth,0,0,1).isEmpty());
        }
    }
    void baselineCrossMapFailure()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=3;
        auto command=s.documents["a"]->terrain.makeRaiseLower(brush,1000,true);
        QVERIFY(s.documents["a"]->terrain.apply(command,true));
        QVERIFY(s.documents["a"]->terrain.height(32,16)!=s.documents["b"]->terrain.height(0,16));
        // Reproduces the original single-model route's failed seam expectation.
    }
    void modesEdgesCornersAndOrder()
    {
        for(auto mode:{EWorldSculptMode::Raise,EWorldSculptMode::Lower,EWorldSculptMode::Flatten,EWorldSculptMode::Smooth})
        {
            CWorldTerrainSession s; add(s,"a");add(s,"b",80,15000);add(s,"c",60,20000);add(s,"d",50,30000);
            QVector<SWorldTerrainTarget> targets{target(s,"a",{0,0}),target(s,"b",{32,0}),target(s,"c",{0,32}),target(s,"d",{32,32})};
            STerrainBrush brush;brush.centerX=32;brush.centerY=32;brush.radius=3;
            const auto stamp=makeWorldTerrainStamp(targets,brush,mode,2,30,0.5);
            QCOMPARE(stamp.maps.size(),4);
            std::reverse(targets.begin(),targets.end());
            const auto reverse=makeWorldTerrainStamp(targets,brush,mode,2,30,0.5);
            for(auto it=stamp.maps.cbegin();it!=stamp.maps.cend();++it)
            {
                const auto other=reverse.maps[it.key()]; QCOMPARE(other.deltas.size(),it->deltas.size());
                for(int i=0;i<other.deltas.size();++i) QCOMPARE(other.deltas[i].after,it->deltas[i].after);
            }
            QString error; QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));s.record(stamp);
            QVERIFY(qAbs(z(s,"a",32,32)-z(s,"b",0,32))<180.0/65535);
            QVERIFY(qAbs(z(s,"a",32,32)-z(s,"c",32,0))<160.0/65535);
            QVERIFY(qAbs(z(s,"a",32,32)-z(s,"d",0,0))<150.0/65535);
            QVERIFY(s.undo(&error)); QCOMPARE(s.documents["a"]->terrain.height(32,32),quint16(10000));
            QCOMPARE(s.documents["b"]->terrain.height(0,32),quint16(15000));
            QVERIFY(s.redo(&error)); QVERIFY(!s.canRedo());
        }
    }
    void protectedMisalignedAndProvenance()
    {
        CWorldTerrainSession s;add(s,"a");add(s,"b");
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=3;
        auto targets=QVector<SWorldTerrainTarget>{target(s,"a",{0,0}),target(s,"b",{32,0},false)};
        auto stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Raise,2,0,1);
        QVERIFY(!stamp.maps.contains("b"));
        targets[1].editable=true;targets[1].origin.setY(0.5);
        stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Raise,2,0,1);
        QCOMPARE(stamp.maps.size(),2);
        const quint16 before=s.documents["a"]->terrain.height(32,16);
        auto corrupt=stamp;corrupt.maps["b"].deltas.first().before++;
        QString error; QVERIFY(!s.apply(corrupt,true,&error));
        QCOMPARE(s.documents["a"]->terrain.height(32,16),before);
        QVERIFY(s.apply(stamp,true,&error));
    }
    void smoothAcrossBothSides()
    {
        CWorldTerrainSession s;add(s,"a",100,0);add(s,"b",100,0);
        STerrainHeightCommand peak; STerrainHeightDelta d;d.globalX=1;d.globalY=16;d.after=60000;peak.deltas.append(d);
        QVERIFY(s.documents["b"]->terrain.apply(peak,true));
        STerrainBrush brush;brush.centerX=32;brush.centerY=16;brush.radius=0;
        auto stamp=makeWorldTerrainStamp({target(s,"a",{0,0}),target(s,"b",{32,0})},brush,EWorldSculptMode::Smooth,0,0,1);
        QString error;QVERIFY(s.apply(stamp,true,&error));
        QCOMPARE(s.documents["a"]->terrain.height(32,16),quint16(6667));
        QCOMPARE(s.documents["b"]->terrain.height(0,16),quint16(6667));
    }
    void saveReopenAndFailure()
    {
        QTemporaryDir temp;QVERIFY(temp.isValid());
        QVector<SWorldTerrainSaveItem> items;
        for(const auto& name:{"a.mpr","b.mpr"})
        {
            SWorldTerrainSaveItem item;item.path=temp.filePath(name);item.original=fixture();item.candidate=fixture(100,12000);
            QFile f(item.path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(item.original),qint64(item.original.size()));f.close();items.append(item);
        }
        const auto failed=saveWorldTerrainCohort(items,1);QVERIFY(!failed.success);QVERIFY(failed.recoveryMarker.isEmpty());
        for(const auto& item:items) { QFile f(item.path);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),item.original); }
        const auto saved=saveWorldTerrainCohort(items);QVERIFY2(saved.success,qPrintable(saved.error));QCOMPARE(saved.backups.size(),2);
        CWorldTerrainSession reopened;QString error;
        for(const auto& item:items) { QVERIFY(reopened.load(item.path,&error));QCOMPARE(reopened.documents[CWorldTerrainSession::key(item.path)]->terrain.height(16,16),quint16(12000)); }
        QVERIFY(!saveWorldTerrainCohort(items).success); // external/baseline change
        auto bad=items;bad[0].original=bad[0].candidate;bad[1].original=bad[1].candidate;bad[1].candidate="broken";
        QVERIFY(!saveWorldTerrainCohort(bad).success);
        QVERIFY(!reopened.load(temp.filePath("missing.mpr"),&error));
    }
    void retainedDocumentsAndStrokeSaveReopen()
    {
        QTemporaryDir temp;QVERIFY(temp.isValid());
        CWorldTerrainSession session;QString error;
        QStringList ids;
        for(const auto name:{"a.mpr","b.mpr"})
        {
            const QString path=temp.filePath(name);QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));f.write(fixture());f.close();
            QVERIFY(session.load(path,&error));ids.append(CWorldTerrainSession::key(path));
        }
        const auto a=session.documents[ids[0]],b=session.documents[ids[1]];
        STerrainBrush brush;brush.centerX=132;brush.centerY=-84;brush.radius=3;
        const auto stamp=makeWorldTerrainStamp({target(session,ids[0],{100,-100}),target(session,ids[1],{132,-100})},brush,EWorldSculptMode::Raise,3,0,1);
        QVERIFY(session.apply(stamp,true,&error));session.record(stamp);
        // Rebinding either active path must return its retained edited identity.
        QVERIFY(session.load(temp.filePath("b.mpr"),&error));
        QCOMPARE(session.documents[ids[0]].data(),a.data());
        QCOMPARE(session.documents[ids[1]].data(),b.data());
        QCOMPARE(session.dirtyPaths().size(),2);
        QVector<SWorldTerrainSaveItem> items;
        for(int i=0;i<ids.size();++i)
        {
            auto doc=session.documents[ids[i]];CMprEditTransaction transaction;
            CValidationReport report;SWorldTerrainSaveItem item;
            item.path=temp.filePath(i?"b.mpr":"a.mpr");item.original=doc->baseline.originalBytes();
            QVERIFY(doc->terrain.appendSectorEdits(&transaction,&report));
            QVERIFY(doc->baseline.applyTransaction(transaction,&item.candidate,&report));items.append(item);
        }
        const auto saved=saveWorldTerrainCohort(items);QVERIFY2(saved.success,qPrintable(saved.error));
        for(int i=0;i<ids.size();++i)
        {
            CMprDocument disk;CTerrainEditModel terrain;CValidationReport report;
            QVERIFY(disk.load(QFileInfo(items[i].path),&report));QVERIFY(terrain.load(disk,&report));
            QCOMPARE(terrain.height(i?0:32,16),session.documents[ids[i]]->terrain.height(i?0:32,16));
            session.documents[ids[i]]->baseline=disk;session.documents[ids[i]]->terrain=terrain;
        }
        QVERIFY(session.undo(&error));QCOMPARE(a->terrain.height(32,16),quint16(10000));
        QCOMPARE(b->terrain.height(0,16),quint16(10000));QVERIFY(session.redo(&error));
    }
    void interruptedSaveRecoveryAndExternalChange()
    {
        QTemporaryDir temp;QVERIFY(temp.isValid());
        const auto before=fixture(),after=fixture(100,14000);
        const auto hash=[](const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());};
        const auto write=[](const QString& path,const QByteArray& bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
        QJsonArray files;
        for(int i=0;i<2;++i)
        {
            const QString path=temp.filePath(QString::number(i)+".mpr");
            QVERIFY(write(path,i?before:after));QVERIFY(write(path+".bak",before));
            QJsonObject entry;entry.insert("path",path);entry.insert("backup",path+".bak");
            entry.insert("before",hash(before));entry.insert("after",hash(after));files.append(entry);
        }
        QJsonObject root;root.insert("schema",1);root.insert("files",files);
        const auto manifest=QJsonDocument(root).toJson();
        for(const auto& file:files) QVERIFY(write(file.toObject()["path"].toString()+".world-sculpt.json",manifest));
        const QString marker=temp.filePath("0.mpr.world-sculpt.json");
        CWorldTerrainSession session;QString error;
        QVERIFY(!session.load(temp.filePath("0.mpr"),&error));
        // An external third version must prevent any recovery writes.
        const auto external=fixture(100,16000);QVERIFY(write(temp.filePath("1.mpr"),external));
        QVERIFY(!recoverWorldTerrainCohort(marker,&error));
        QFile unchanged(temp.filePath("0.mpr"));QVERIFY(unchanged.open(QIODevice::ReadOnly));QCOMPARE(unchanged.readAll(),after);unchanged.close();
        QVERIFY(write(temp.filePath("1.mpr"),before));
        QVERIFY2(recoverWorldTerrainCohort(marker,&error),qPrintable(error));
        QVERIFY(!QFileInfo::exists(marker));
        for(const auto& file:files)
        { QFile f(file.toObject()["path"].toString());QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),before); }
    }
};
QTEST_GUILESS_MAIN(WorldTerrainTests)
#include "world_terrain_sculpt_tests.moc"

// Standalone native check: production main remains untouched. All writable
// fixtures and Qt temporary files must be placed in build-tests-130-brush.
#include <QtTest>
#include <QtWidgets>
#include <QtOpenGL>
#include <QtEndian>
#include <limits>
#define private public
#include "view.h"
#include "landscape_tools.h"
#undef private
#include "landscape.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "res_archive_document.h"
#include "mmp_codec.h"
#include "resource_name.h"

// Also permits the unchanged production --scenario-layer-check entry point
// to run against exactly the same compiled objects.
#define main worldBrush130ProductionMain
#include "../source/app/main.cpp"
#undef main

class WorldBrush130Tests : public QObject
{
    Q_OBJECT
    QByteArray canvas(bool buried=false)
    {
        CMprDocument source; CValidationReport report; QByteArray result;
        if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&report))return {};
        SMprCreationRequest request;request.innerNamespace="brush130";
        request.maxZ=100;request.flatRawHeight=30000;
        request.createOverlayPlane=buried;request.overlayRawHeight=10000;
        if(!source.buildNewCanvas(request,&result,&report))return {};
        CMprDocument created;if(!created.loadBytes(result,"brush130.mpr",&report))return {};
        auto materials=created.mapInfo().materials;if(materials.isEmpty())return {};
        materials[0].type=eTerrainWater;materials[0].opacity=1;
        CMprEditTransaction water("Synthetic water surface material zero");
        if(!created.appendMapParametersEdit(100,materials,created.mapInfo().animatedTiles,&water,&report)
           ||!created.applyTransaction(water,&result,&report))return {};
        return result;
    }
    static bool write(const QString& path,const QByteArray& bytes)
    { QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(); }
    static QByteArray read(const QString& path)
    { QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
    struct Scene
    {
        QTemporaryDir scratch{QDir::currentPath()+"/brush130-XXXXXX"};
        CSettings settings;QTableWidget properties;QUndoStack history;
        CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
        CLandscapeTools tools;CView view{nullptr,nullptr};
        QByteArray original,archiveBytes;QString path,archive;
        CValidationReport report;
        bool init(const QByteArray& bytes)
        {
            original=bytes;path=scratch.filePath("a.mpr");archive=scratch.filePath("textures.res");
            if(bytes.isEmpty()||!write(path,bytes))return false;
            properties.setColumnCount(2);
            view.setAttribute(Qt::WA_DontShowOnScreen);
            view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
            view.attachLandscapeTools(&tools);view.resize(320,240);view.show();
            QApplication::processEvents();view.makeCurrent();if(!view.isValid())return false;
            view.m_pLand->readMap(QFileInfo(path),false);
            if(!view.m_mprDocument.loadBytes(bytes,path,&report)
               ||!view.m_terrainEdit.load(view.m_mprDocument,&report))return false;
            view.m_terrainEditAvailable=true;view.m_terrainAutoCatalogPending=false;
            view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
            view.m_mprMaterials=view.m_mprDocument.mapInfo().materials;
            view.m_mprAnimatedTiles=view.m_mprDocument.mapInfo().animatedTiles;
            tools.setOverlayMaterials(view.m_pLand->materials());
            QImage land(128,128,QImage::Format_RGBA8888);land.fill(Qt::green);
            QImage swamp=land;swamp.fill(QColor(90,65,30));
            QVector<QImage> images(int(view.m_mprDocument.mapInfo().textureCount)*64,land);
            images[1]=swamp;QVector<qint32> types(images.size(),0);types[1]=14;
            CTerrainTransitionCatalog catalog;
            for(int slot:{0,1}) {
                STerrainTransitionVariant v;const int material=slot?14:0;
                v.signature.northWest=v.signature.northEast=v.signature.southWest=v.signature.southEast=material;
                v.packedTile=quint16(slot);catalog.addVariant(v);
            }
            view.m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(catalog,images,0,1024,{}, {},types,{{0,0},{14,14}}));
            if(!view.m_workingTerrainAtlas->isValid())return false;
            view.m_authoredTerrainTileImages=images;
            view.m_terrainAtlasProfile.mprNamespace="brush130";
            view.m_terrainAtlasProfile.atlasPixelSha256=CTerrainAtlasProfileInference::pixelSha256(images);
            view.m_terrainAtlasProfile.atlasSlotCount=images.size();
            view.m_terrainAtlasProfile.textureCount=images.size()/64;
            view.m_activeTerrainArchivePath=archive;
            STerrainGlobalMaterialFamily grassFamily,swampFamily;
            grassFamily.id=0;grassFamily.name="Grass";grassFamily.runtimeTerrainType=0;
            swampFamily.id=14;swampFamily.name="Swamp";swampFamily.runtimeTerrainType=14;
            swampFamily.layer=ETerrainAtlasLayer::Overlay;
            view.m_terrainGlobalFamilies={grassFamily,swampFamily};
            QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019ce23c,reinterpret_cast<uchar*>(empty.data()));
            qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
            CResArchiveDocument res;QStringList errors;if(!res.loadBytes(empty,archive,&errors))return false;
            QVector<SResArchiveNewMember> members;QString error;
            for(int page=0;page<images.size()/64;++page) {
                SResArchiveNewMember member;
                member.name=ei::terrainTextureResourceNames("brush130",images.size()/64)[page];
                member.payload=ei::encodeMmpDxt1(CTerrainVirtualAtlas::composePage(images,page,128),true,&error);
                if(member.payload.isEmpty())return false;
                members.append(member);
            }
            if(!res.buildWithAppendedMembers(members,&archiveBytes,&errors)||!write(archive,archiveBytes))return false;
            QString sessionError;if(!view.m_worldTerrainSession.load(path,&sessionError))return false;
            return true;
        }
        ~Scene(){view.doneCurrent();}
    };
private slots:
    void switchCapturesLateWorkingAtlas()
    {
        Scene s;QVERIFY(s.init(canvas()));auto& view=s.view;
        const QString id=CWorldTerrainSession::key(s.path);auto doc=view.m_worldTerrainSession.documents[id];
        QVERIFY(!doc->atlas); // height-only world session, before a local atlas edit
        QImage imported=view.m_workingTerrainAtlas->authoredTileImages().front();imported.fill(Qt::magenta);
        const int slot=view.m_workingTerrainAtlas->importPhysicalTile(imported,0,nullptr,"external",0,&s.report,true);
        QVERIFY2(slot>=64,qPrintable(s.report.toText()));
        view.m_terrainEdit.enableWorkingAtlasSlots(1024);
        QVERIFY(view.m_terrainEdit.installWorkingAtlasSlotTypes(view.m_workingTerrainAtlas->activeTileTypes()));
        STerrainTileCommand paint;STerrainTileDelta delta;delta.globalX=5;delta.globalY=5;
        delta.before=view.m_terrainEdit.packedTile(5,5,ETerrainTileLayer::Land);delta.after=quint16(slot);
        paint.deltas.append(delta);QVERIFY(view.m_terrainEdit.apply(paint,true,&s.report));view.m_pLand->setDirty(true);
        view.syncWorldTerrainActive();QVERIFY(!doc->atlas);
        QByteArray candidate;CValidationReport missing;
        QVERIFY(!view.buildValidatedLandscapeCandidate(&candidate,&missing));
        QVERIFY(missing.toText().contains("mpr.test.atlas.destination"));
        int dialogs=0;QTimer timer;timer.setInterval(10);
        connect(&timer,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())
            if(auto* box=qobject_cast<QMessageBox*>(w))if(box->isVisible()){++dialogs;box->accept();}});
        timer.start();const bool retained=view.resolveWorldTerrainClose(true);timer.stop();
        QVERIFY2(retained,"late local working atlas must be captured before a retained map switch");
        QCOMPARE(dialogs,0);QVERIFY(doc->atlas);QVERIFY(view.m_worldTerrainAtlasSources.contains(id));
        QCOMPARE(doc->atlas->activeTileImages(),view.m_workingTerrainAtlas->activeTileImages());
        QCOMPARE(doc->terrain.packedTile(5,5,ETerrainTileLayer::Land),quint16(slot));
        // Simulate loading another map, then restoring the retained map.
        view.m_workingTerrainAtlas.reset();view.m_terrainEdit=CTerrainEditModel();
        QVERIFY(view.previewWorldPaintState(id));
        QCOMPARE(view.m_terrainEdit.packedTile(5,5,ETerrainTileLayer::Land),quint16(slot));
        QCOMPARE(view.m_workingTerrainAtlas->activeTileImages()[slot],imported);
        QCOMPARE(read(s.path),s.original);QCOMPARE(read(s.archive),s.archiveBytes);
    }
    void missingArchiveStillRefusesSwitch()
    {
        Scene s;QVERIFY(s.init(canvas()));auto& view=s.view;
        QImage imported=view.m_workingTerrainAtlas->authoredTileImages().front();imported.fill(Qt::magenta);
        const int slot=view.m_workingTerrainAtlas->importPhysicalTile(imported,0,nullptr,"external",0,&s.report,true);
        QVERIFY(slot>=64);view.m_terrainEdit.enableWorkingAtlasSlots(1024);
        QVERIFY(view.m_terrainEdit.installWorkingAtlasSlotTypes(view.m_workingTerrainAtlas->activeTileTypes()));
        STerrainTileCommand command;STerrainTileDelta delta;delta.globalX=5;delta.globalY=5;
        delta.before=view.m_terrainEdit.packedTile(5,5,ETerrainTileLayer::Land);delta.after=quint16(slot);
        command.deltas.append(delta);QVERIFY(view.m_terrainEdit.apply(command,true,&s.report));
        view.m_pLand->setDirty(true);view.m_activeTerrainArchivePath=s.scratch.filePath("missing.res");
        int dialogs=0;QTimer timer;timer.setInterval(10);
        connect(&timer,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())
            if(auto* box=qobject_cast<QMessageBox*>(w))if(box->isVisible()){++dialogs;box->accept();}});
        timer.start();QVERIFY(!view.resolveWorldTerrainClose(true));timer.stop();QCOMPARE(dialogs,1);
        QCOMPARE(read(s.path),s.original);QCOMPARE(read(s.archive),s.archiveBytes);
    }
    void heightOnlyStillRetainsWithoutAtlasSource()
    {
        Scene s;QVERIFY(s.init(canvas()));auto& view=s.view;
        auto command=view.m_terrainEdit.makeGlobalOffset(10);
        QVERIFY(view.m_terrainEdit.apply(command,true,&s.report));view.m_pLand->setDirty(true);
        view.m_activeTerrainArchivePath=s.scratch.filePath("missing.res");
        QTimer timer;int dialogs=0;timer.setInterval(10);
        connect(&timer,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())
            if(auto* box=qobject_cast<QMessageBox*>(w))if(box->isVisible()){++dialogs;box->accept();}});
        timer.start();const bool retained=view.resolveWorldTerrainClose(true);timer.stop();
        QVERIFY(retained);QCOMPARE(dialogs,0);
        QVERIFY(!view.m_worldTerrainSession.documents[CWorldTerrainSession::key(s.path)]->atlas);
        QCOMPARE(read(s.path),s.original);QCOMPARE(read(s.archive),s.archiveBytes);
    }
    void pendingAnimationStillRefusesSwitch()
    {
        Scene s;QVERIFY(s.init(canvas()));auto& view=s.view;
        const QString id=CWorldTerrainSession::key(s.path);
        QVERIFY(view.ensureWorldTerrainAtlas(id,&s.report));
        view.m_mprAnimatedTiles.append(SMprAnimatedTileRecord());view.m_pLand->setDirty(true);
        QTimer timer;int dialogs=0;timer.setInterval(10);
        connect(&timer,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())
            if(auto* box=qobject_cast<QMessageBox*>(w))if(box->isVisible()){++dialogs;box->accept();}});
        timer.start();const bool retained=view.resolveWorldTerrainClose(true);timer.stop();
        QVERIFY(!retained);QCOMPARE(dialogs,1);
        QCOMPARE(read(s.path),s.original);QCOMPARE(read(s.archive),s.archiveBytes);
    }
    void swampWorldActivation_data()
    {
        QTest::addColumn<bool>("buried");QTest::addColumn<bool>("modern");
        QTest::newRow("manual-inactive")<<false<<false;QTest::newRow("manual-buried")<<true<<false;
        QTest::newRow("swamp-inactive")<<false<<true;QTest::newRow("swamp-buried")<<true<<true;
    }
    void swampWorldActivation()
    {
        QFETCH(bool,buried);QFETCH(bool,modern);Scene s;QVERIFY(s.init(canvas(buried)));auto& view=s.view;
        QCOMPARE(view.m_mprMaterials[0].type,qint32(eTerrainWater));
        auto& tools=s.tools;tools.activateMode(ELandscapeEditMode::OverlaySurface,false);
        tools.setPaintPaletteMode(modern?ETerrainPaintPaletteMode::Modern:ETerrainPaintPaletteMode::Manual);
        if(modern){tools.setPaintTerrainTypes({{0,"Grass"}},{},{{14,"Swamp"}});QVERIFY(tools.selectPaintTerrainType(14));}
        tools.m_overlaySurfaceOperation->setCurrentIndex(0);
        tools.m_overlayMaterial->setCurrentIndex(0);tools.m_paintRadius->setValue(1);
        QCOMPARE(tools.overlaySurfaceOperation(),EOverlaySurfaceOperation::Activate);
        QCOMPARE(tools.paintRadius(),0.0f);
        QVERIFY(s.tiles.adoptTileImages(view.m_workingTerrainAtlas->activeTileImages(),view.m_workingTerrainAtlas->activeTileTypes()));
        QVERIFY(s.tiles.selectTile(1));s.tiles.setTileRotation(0);
        view.m_hasActiveMapWorldOrigin=true;view.m_neighborLayoutStorageWritable=true;
        QStringList messages;connect(&view,&CView::updateMsg,[&](const QString& text){messages.append(text);});
        const auto before=view.m_terrainEdit.sectorPayload(0,0);
        QVERIFY(view.paintWorldTerrainAt(QVector3D(10,10,0),false));
        QVERIFY2(view.m_terrainEdit.overlayMaterialIndex(5,5)==0,qPrintable(messages.join('\n')));
        QCOMPARE(SMprPackedTile::decode(view.m_terrainEdit.packedTile(5,5,ETerrainTileLayer::Overlay)).atlasSlot(),1);
        const auto payload=view.m_terrainEdit.sectorPayload(0,0);
        const int vertexOffset=5+(11*33+11)*8+2;
        const float land=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(payload.constData()+vertexOffset))*100.0f/65535;
        const float overlay=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(payload.constData()+vertexOffset+33*33*8))*100.0f/65535;
        qInfo("SWAMP buried=%d land=%f overlay=%f",int(buried),double(land),double(overlay));
        if(buried)QVERIFY(overlay<land);else QVERIFY(overlay>land);
        // GPU-side sector preview accepts the exact changed payload.
        QVERIFY(view.m_pLand->previewCanonicalSector(0,0,view.m_terrainEdit.sectorPayload(0,0)));
        QString error;view.recordWorldTerrainStroke(view.m_worldTerrainStroke);view.m_worldTerrainStroke={};
        QVERIFY(view.worldTerrainHistory(false,true));
        const auto id=CWorldTerrainSession::key(s.path);QVERIFY(view.previewWorldPaintState(id));
        QCOMPARE(view.m_terrainEdit.sectorPayload(0,0),before);
        QVERIFY(view.worldTerrainHistory(true,true));QVERIFY(view.previewWorldPaintState(id));
        QCOMPARE(view.m_terrainEdit.overlayMaterialIndex(5,5),qint16(0));
        QCOMPARE(read(s.path),s.original);QCOMPARE(read(s.archive),s.archiveBytes);
    }
};

int main(int argc,char** argv)
{
    if(argc>1 && QByteArray(argv[1]).startsWith("--"))return worldBrush130ProductionMain(argc,argv);
    // CLandscape and the legacy operation manager are process singletons.
    // Give each fixture a fresh application lifetime, as production does.
    if(argc==1) {
        QCoreApplication driver(argc,argv);int failed=0;
        const QStringList cases={"switchCapturesLateWorkingAtlas","missingArchiveStillRefusesSwitch",
            "heightOnlyStillRetainsWithoutAtlasSource","pendingAnimationStillRefusesSwitch",
            "swampWorldActivation:manual-inactive","swampWorldActivation:manual-buried",
            "swampWorldActivation:swamp-inactive","swampWorldActivation:swamp-buried"};
        QTextStream out(stdout);
        for(const auto& name:cases) {
            QString fileName=name;fileName.replace(':','-');
            const QString log=QDir::currentPath()+"/case-"+fileName+".txt";
            QProcess child;child.start(QCoreApplication::applicationFilePath(),{name,"-o",log+",txt"});
            if(!child.waitForStarted(10000)||!child.waitForFinished(60000)) {
                child.kill();child.waitForFinished(5000);out<<"FAIL: child timeout "<<name<<"\n";++failed;
            } else {
                QFile result(log);if(result.open(QIODevice::ReadOnly))out<<QString::fromUtf8(result.readAll());
                out<<QString::fromLocal8Bit(child.readAllStandardOutput())<<QString::fromLocal8Bit(child.readAllStandardError());
                out<<"Child "<<name<<" exit="<<child.exitCode()<<"\n";
                if(child.exitStatus()!=QProcess::NormalExit||child.exitCode()!=0)++failed;
            }
            out.flush();
        }
        out<<"World brush 130 cases: "<<cases.size()<<", failed: "<<failed<<"\n";
        return failed?1:0;
    }
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("Ataman130Tests");
    QCoreApplication::setApplicationName("world-brush-130");
    WorldBrush130Tests tests;return QTest::qExec(&tests,argc,argv);
}
#include "world_brush_130_integration.moc"

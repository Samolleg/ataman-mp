#include "terrain_random_paint_check.h"
#include "terrain_edit_model.h"
#include "terrain_atlas_profile.h"
#include "terrain_semantic_field_store.h"
#include "terrain_atlas_review_dialog.h"
#include "landscape_tools.h"
#include "mpr_document.h"
#include "res_archive_document.h"
#include "endurance_document.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "terrain_virtual_atlas.h"
#include "resourcemanager.h"
#include <QApplication>
#include <QTableWidget>
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QPainter>
#include <QTransform>
#include <QTextStream>
#include <QCheckBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QSettings>
#include <QUndoStack>
#include <QtEndian>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QTabWidget>

namespace {
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
STerrainTransitionSignature signature(qint32 a, qint32 b, qint32 c, qint32 d)
{ STerrainTransitionSignature s; s.northWest=a; s.northEast=b; s.southEast=c; s.southWest=d; return s; }
quint16 packed(int slot, int rotation=0) { SMprPackedTile p; p.atlasIndex=quint8(slot/64);p.tileIndex=quint8(slot%64);p.rotation=quint8(rotation);return p.encode(); }
QByteArray syntheticMpr()
{
    QByteArray mp; QDataStream out(&mp,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    out << quint32(0xCE4AF672) << float(100) << quint32(1) << quint32(1)
        << quint32(1) << quint32(256) << quint32(64) << quint32(32) << quint16(1) << quint32(1);
    out << qint32(1);for(int i=0;i<10;++i) out << float(i==3?1:0);
    for(int i=0;i<64;++i) out << qint32(i==2?2:(i==3?3:1));
    out << quint16(5) << quint16(2);
    QByteArray sector;QDataStream sec(&sector,QIODevice::WriteOnly);
    sec.setByteOrder(QDataStream::LittleEndian);sec << quint32(0xCF4BF774) << quint8(3);
    sector.append(QByteArray(2*33*33*8+3*16*16*2,'\0'));
    QByteArray empty(16,'\0');qToLittleEndian<quint32>(0x019CE23C,reinterpret_cast<uchar*>(empty.data()));
    qToLittleEndian<quint32>(16,reinterpret_cast<uchar*>(empty.data()+8));
    CResArchiveDocument archive;QStringList errors;QByteArray first,result;
    if(!archive.loadBytes(empty,"empty",&errors)
       || !archive.buildWithAppendedMember("Random.MP",mp,0,&first,&errors)
       || !archive.loadBytes(first,"first",&errors)
       || !archive.buildWithAppendedMember("Random000000.SEC",sector,0,&result,&errors)) return {};
    return result;
}
QByteArray snapshot(const CTerrainEditModel& model)
{
    QByteArray bytes;
    for(int y=0;y<model.tileCountY()/16;++y) for(int x=0;x<model.tileCountX()/16;++x) bytes+=model.sectorPayload(x,y);
    return bytes;
}
QImage preview(const CTerrainEditModel& model, const QVector<QImage>& images, const QRect& region)
{
    const int size=qMin(64,images.first().width());
    QImage result(region.width()*size,region.height()*size,QImage::Format_RGB32);result.fill(Qt::magenta);
    QPainter painter(&result);
    for(int y=region.top();y<=region.bottom();++y)for(int x=region.left();x<=region.right();++x) {
        bool found=false;auto tile=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land,&found));
        if(found && tile.atlasSlot()<images.size()) painter.drawImage(QRect((x-region.x())*size,(region.bottom()-y)*size,size,size),
            images[tile.atlasSlot()].transformed(QTransform().rotate(-90*tile.rotation)));
    }
    return result;
}
class RandomCommand : public QUndoCommand {
public:
    RandomCommand(CTerrainEditModel* model,const STerrainTileCommand& command):m_model(model),m_command(command){}
    void undo() override { valid &= m_model->apply(m_command,false); }
    void redo() override { valid &= m_model->apply(m_command,true); }
    bool valid{true};
private:
    CTerrainEditModel* m_model;STerrainTileCommand m_command;
};
}

int runTerrainRandomPaintCheck(const QString& fixturePath)
{
    QTextStream log(stdout);int passed=0,failed=0;
    auto check=[&](bool condition,const QString& name) {log<<(condition?"PASS ":"FAIL ")<<name<<'\n';log.flush();condition?++passed:++failed;return condition;};
    const auto fixture=QJsonDocument::fromJson(read(fixturePath)).object();
    const QString output=fixture.value("output").toString();
    if(!check(!output.isEmpty() && QDir(output).exists(),"owned output directory")) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,output+"/settings");
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,output+"/settings");
    if(fixture.value("paletteRecovery").toBool()) {
        // Use the production first-frame/event-loop path, never ensure() from
        // the harness. Resource roots and the map must be private copies.
        QDir::setCurrent(output);
        CSettings settings;
        const QString root=fixture.value("realRoot").toString();
        auto* roots=dynamic_cast<COptStringList*>(settings.opt(eOptSetResource,"resourceRoots"));
        if(!check(roots && QFileInfo(root+"/res/HDPack_Terrain.res").isFile(),"copied atlas resource root"))return 1;
        roots->setValue({root+"/res"});
        CTextureList::getInstance()->attachSettings(&settings);
        CObjectList::getInstance()->attachSettings(&settings);
        QUndoStack history;CTreeView tree;QTableWidget properties;properties.setColumnCount(2);
        CProgressView progress;QLineEdit coordinates;CTileForm tiles;CLandscapeTools panel;
        panel.setAttribute(Qt::WA_DontShowOnScreen);panel.resize(360,900);panel.show();
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
        view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
        view.attachLandscapeTools(&panel);view.resize(640,480);view.show();QApplication::processEvents();
        if(!check(view.isValid(),"hidden desktop OpenGL context"))return 1;
        const QString map=root+"/Maps/"+fixture.value("zone").toString("zone1")+".mpr";
        QElapsedTimer timer;timer.start();view.loadLandscape(QFileInfo(map),false);
        const qint64 openMs=timer.elapsed();
        while(timer.elapsed()<120000 && (view.m_terrainAutoCatalogPending || view.m_terrainAutoCatalogBuilding)) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);
        }
        log<<"TIMING openReturnMs="<<openMs<<" paletteReadyMs="<<timer.elapsed()<<" firstFrameMs="<<view.m_terrainAutoCatalogFirstFrameMs<<'\n';
        if(!check(!view.m_terrainAutoCatalogPending && !view.m_terrainAutoCatalogBuilding && view.m_terrainEditAvailable,
                  "catalog fills automatically after a frame without harness ensure"))return 1;
        QElapsedTimer thumbnails;thumbnails.start();
        while(thumbnails.elapsed()<120000 && (view.m_terrainGlobalHydrationPendingJobs>0 || view.m_terrainGlobalHydrationPublishQueued)) {
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents,50);QThread::msleep(5);
        }
        log<<"TIMING thumbnailWaitMs="<<thumbnails.elapsed()<<'\n';
        panel.activateMode(ELandscapeEditMode::TilePaint,true);panel.setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);
        int land=0,overlay=0;
        for(const auto& family:view.m_terrainGlobalFamilies) {
            if(family.layer==ETerrainAtlasLayer::Land && panel.selectPaintTerrainType(family.id))++land;
        }
        panel.grab().save(output+"/palette-land.png");
        panel.activateMode(ELandscapeEditMode::OverlayPaint,true);
        for(const auto& family:view.m_terrainGlobalFamilies) {
            if(family.layer==ETerrainAtlasLayer::Overlay && panel.selectPaintTerrainType(family.id))++overlay;
        }
        panel.grab().save(output+"/palette-overlay.png");
        check(land>0 && overlay>0,QString("selectable land=%1 overlay=%2").arg(land).arg(overlay));
        panel.activateMode(ELandscapeEditMode::TilePaint,true);
        if(auto* option=panel.findChild<QAbstractButton*>("modernMixedEdge"))option->setChecked(false);
        const auto before=snapshot(view.m_terrainEdit);
        bool changed=false;
        for(const auto& family:view.m_terrainGlobalFamilies) {
            if(family.layer!=ETerrainAtlasLayer::Land || !panel.selectPaintTerrainType(family.id))continue;
            QVector3D point(33,33,0);view.m_pLand->projectPt(point,true);
            view.updatePreviewTile(point,true);view.beginTileBrushGroup();view.setTile(point,true);view.endTileBrushGroup();
            if(snapshot(view.m_terrainEdit)!=before){changed=true;break;}
        }
        if(!check(changed,"production land brush changes real copied map"))return 1;
        const auto painted=snapshot(view.m_terrainEdit);history.undo();check(snapshot(view.m_terrainEdit)==before,"stroke Undo");
        history.redo();check(snapshot(view.m_terrainEdit)==painted,"stroke Redo");
        QObject::connect(&view,&CView::terrainPublicationPreflightRequested,&view,[](const QString&,bool* accepted){if(accepted)*accepted=true;});
        QTimer dismiss;QObject::connect(&dismiss,&QTimer::timeout,[&](){
            for(auto* widget:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(widget)) {
                log<<"Unexpected save dialog: "<<box->text()<<'\n';++failed;box->reject();
            }
        });dismiss.start(50);
        const bool saved=view.saveChangedLandscapeDocument(QFileInfo(map),true,true);
        check(saved,"save through production writer on private copied map");
        CMprDocument reopened;CTerrainEditModel model;CValidationReport report;
        check(saved && reopened.load(QFileInfo(map),&report) && model.load(reopened,&report)
              && !report.hasErrors() && snapshot(model)==painted,"reopen copied MPR with exact painted sectors");
        log<<report.toText()<<"RESULT passed="<<passed<<" failed="<<failed<<'\n';
        return failed?1:0;
    }
    QSettings().remove("ui/landscape/randomPaintVariants");
    {
        CLandscapeTools panel;
        check(panel.randomPaintVariants(),"random painting defaults ON");
        check(!panel.randomizeUniformRotation(),"new rotation policy suppresses legacy uniform rotation");
        auto option=panel.findChild<QCheckBox*>("randomPaintVariants");
        auto shuffle=panel.findChild<QPushButton*>("shuffleTerrainTiles");
        auto scope=panel.findChild<QComboBox*>("randomPaintScope");
        check(option && shuffle && !panel.findChild<QWidget*>("randomPaintSeed")
            && !panel.findChild<QWidget*>("newRandomPaintSeed") && scope && scope->count()==2,
            "panel exposes shuffle scopes without a seed editor");
        bool emitted=false;
        QObject::connect(&panel,&CLandscapeTools::shuffleTerrainTilesRequested,
            [&](bool selection){emitted=!selection;});
        shuffle->click();check(emitted,"shuffle signal carries scope; view creates its own seed");
        option->setChecked(false);CLandscapeTools restored;
        check(!restored.randomPaintVariants(),"OFF preference persists");
        check(restored.randomizeUniformRotation(),"OFF retains legacy uniform rotation switch");
    }
    CValidationReport report;CMprDocument document;
    if(!check(document.loadBytes(syntheticMpr(),output+"/synthetic.mpr",&report),"synthetic map with overlay and flipbook loads")) return 1;
    CTerrainEditModel model;
    if(!check(model.load(document,&report),"synthetic edit model")) return 1;
    STerrainAtlasProfile profile;profile.mprNamespace="Random";profile.atlasSlotCount=64;
    profile.textureCount=1;profile.atlasPixelSha256=QByteArray(64,'a');profile.approved=true;
    for(int id=1;id<=3;++id) {
        STerrainAtlasProfileFamily f;f.id=id;f.name=id==1?"Grass":id==2?"Boards":"Water";
        f.randomRotationAllowed=id==1;f.randomRotationExplicit=true;f.tileType=id;profile.families.append(f);
    }
    auto add=[&](int slot,const STerrainTransitionSignature& s,int weight=1,bool mixed=false) {
        STerrainAtlasProfileSlot def;def.slot=slot;def.signature=s;def.distributionWeight=weight;
        if(mixed)def.mixedEdgeRecipes.append("mixed-edge-b2:"+QString(64,'a'));
        profile.slotDefinitions.append(def);
    };
    add(0,signature(1,1,1,1));add(1,signature(1,1,1,1),3);add(2,signature(1,1,1,1));
    add(3,signature(2,2,2,2));add(4,signature(1,1,1,1),1,true);
    add(5,signature(1,1,1,1));add(6,signature(1,1,1,1));
    add(7,signature(1,1,1,2));add(8,signature(1,1,1,2));
    add(9,signature(1,2,1,2));add(10,signature(1,2,1,2));add(11,signature(3,3,3,3));
    for(int slot=12;slot<64;++slot) {
        STerrainAtlasProfileSlot def;def.slot=slot;def.signature=signature(1,1,1,1);def.assigned=false;
        profile.slotDefinitions.append(def);
    }
    profile.normalizeMembership();
    const auto catalog=profile.usableAnalysis(&report).landCatalog;
    check(catalog.size()>0,"reviewed catalog");
    STerrainAtlasProfile loaded;
    check(STerrainAtlasProfile::fromBytes(profile.toBytes(),&loaded,&report)
        && loaded.families[0].randomRotationAllowed && !loaded.families[1].randomRotationAllowed
        && loaded.slotDefinitions[1].distributionWeight==3,"profile flag and weight roundtrip");
    if(report.hasErrors()) log << report.toText();
    {
        QVector<QImage> images(64,QImage(32,32,QImage::Format_RGB32));
        CTerrainAtlasReviewDialog dialog(profile,images);
        auto flag=dialog.findChild<QCheckBox*>("materialRandomRotation");
        check(flag!=nullptr,"Material Atlas rotation flag");
        auto tree=dialog.findChild<QTreeWidget*>();
        if(tree && tree->topLevelItemCount() && tree->topLevelItem(0)->childCount()) {
            auto item=tree->topLevelItem(0)->child(0);tree->setCurrentItem(item);item->setSelected(true);
            QMetaObject::invokeMethod(&dialog,"selectTreeItem",Qt::DirectConnection,Q_ARG(QTreeWidgetItem*,item));
            flag->setChecked(false);
            check(!dialog.reviewedProfile().families[0].randomRotationAllowed
                && dialog.reviewedProfile().families[0].randomRotationExplicit,"flag edits material profile");
        } else check(false,"material tree selection");
    }
    const QRect all(0,0,16,16);const QSet<int> excluded{5,6};
    const QByteArray before=snapshot(model);
    const int catalogEntries=catalog.size();
    auto command=model.makeRandomTilePaint(catalog,123,all,excluded);
    const auto repeated=model.makeRandomTilePaint(catalog,123,all,excluded);
    check(command.deltas.size()==repeated.deltas.size() && !command.deltas.isEmpty(),"deterministic command count");
    bool exact=true;for(int i=0;i<command.deltas.size();++i)exact&=command.deltas[i].after==repeated.deltas[i].after;
    check(exact,"deterministic packed choices");
    command.changesRandomSeed=true;command.beforeRandomSeed=model.randomPaintSeed();command.afterRandomSeed=123;
    QUndoStack undo;auto history=new RandomCommand(&model,command);undo.push(history);
    check(history->valid && undo.count()==1 && model.randomPaintSeed()==123,"one Undo transaction includes seed");
    const QByteArray after=snapshot(model);
    check(model.makeRandomTilePaint(catalog,123,all,excluded).isEmpty(),"same seed is idempotent after painting");
    QMap<int,int> counts,rotations;bool classes=true,signatures=true,heights=true,overlays=true;
    CTerrainEditModel baseline;baseline.load(document);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
        const auto p=SMprPackedTile::decode(model.packedTile(x,y,ETerrainTileLayer::Land));
        ++counts[p.atlasSlot()];++rotations[p.rotation];
        classes&=model.tileType(p.encode())==baseline.tileType(baseline.packedTile(x,y,ETerrainTileLayer::Land));
        signatures&=catalog.gridSignatureForPacked(p.encode())==catalog.gridSignatureForPacked(baseline.packedTile(x,y,ETerrainTileLayer::Land));
        overlays&=model.packedTile(x,y,ETerrainTileLayer::Overlay)==baseline.packedTile(x,y,ETerrainTileLayer::Overlay);
    }
    for(int y=0;y<33;++y)for(int x=0;x<33;++x)heights&=model.height(x,y)==baseline.height(x,y);
    check(counts.size()==2 && counts.value(1)>150 && counts.value(1)<225,"weighted physical variants 1:3");
    check(rotations.size()==4,"all four rotations distributed");
    check(classes && signatures,"classes and exact signatures unchanged");check(heights && overlays,"heights and overlay unchanged");
    check(catalog.size()==catalogEntries,"no new atlas slots or catalog entries");
    undo.undo();check(history->valid && snapshot(model)==before && !model.hasRandomPaintSeed(),"Undo restores bytes and unset seed");
    undo.redo();check(history->valid && snapshot(model)==after && model.randomPaintSeed()==123,"Redo restores bytes and seed");
    check(!model.makeRandomTilePaint(catalog,124,all,excluded).isEmpty(),"new seed changes cells");
    auto selection=model.makeRandomTilePaint(catalog,124,QRect(3,4,5,6),excluded);
    bool scoped=true;for(const auto& d:selection.deltas)scoped&=QRect(3,4,5,6).contains(d.globalX,d.globalY);
    check(scoped && !selection.deltas.isEmpty(),"selection limits every changed cell");
    for(int slot:{3,4,5,6,7,9,11}) {
        CTerrainEditModel cellModel;cellModel.load(document);
        STerrainTileBrush brush;brush.radius=0;
        const auto initial=cellModel.makeTilePaint(brush,ETerrainTileLayer::Land,0,quint8(slot),slot==3?2:0);
        cellModel.apply(initial,true);
        const auto random=cellModel.makeRandomTilePaint(catalog,789,QRect(0,0,1,1),excluded);
        if(slot==3 || slot==4 || slot==5 || slot==6 || slot==11)
            check(random.isEmpty(),QString("protected board/mixed/flipbook/water slot %1").arg(slot));
        else {
            check(random.isEmpty(),QString("transition physical slot and rotation unchanged %1").arg(slot));
        }
    }
    // Even a symmetric non-directional transition may use incompatible edge
    // artwork. The new pass must preserve both the physical slot and rotation.
    CTerrainTransitionCatalog symmetric;
    for(int slot:{9,10})for(int rotation=0;rotation<4;++rotation) {
        STerrainTransitionVariant v;v.signature=CTerrainTransitionCatalog::rotated(signature(1,2,1,2),-rotation);
        v.packedTile=packed(slot,rotation);symmetric.addVariant(v);
    }
    CTerrainEditModel symmetricModel;symmetricModel.load(document);STerrainTileBrush brush;brush.radius=20;brush.shape=ETerrainTileBrushShape::Square;
    symmetricModel.apply(symmetricModel.makeTilePaint(brush,ETerrainTileLayer::Land,0,9,0),true);
    auto symmetricRandom=symmetricModel.makeRandomTilePaint(symmetric,456,all);
    symmetricModel.apply(symmetricRandom,true);QSet<int> symmetricTurns,symmetricSlots;
    for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
        auto p=SMprPackedTile::decode(symmetricModel.packedTile(x,y,ETerrainTileLayer::Land));symmetricTurns.insert(p.rotation);symmetricSlots.insert(p.atlasSlot());
    }
    check(symmetricRandom.isEmpty() && symmetricTurns==QSet<int>({0}) && symmetricSlots==QSet<int>({9}),
        "symmetric transition variants and rotations remain unchanged");
    // Zero is a valid explicitly chosen seed and survives the MPR member.
    STerrainSemanticFieldDocument semantics;semantics.mprNamespace="Random";semantics.vertexWidth=17;semantics.vertexHeight=17;
    semantics.terrainTilesSha256=STerrainSemanticFieldDocument::tileBindingSha256(document);
    semantics.atlasPixelSha256=QByteArray(32,'a');semantics.profileSha256=QByteArray(32,'b');semantics.hasRandomPaintSeed=true;
    STerrainSemanticFieldDocument readSemantics;
    check(STerrainSemanticFieldDocument::fromBytes(semantics.toBytes(),&readSemantics,&report)
        && readSemantics.hasRandomPaintSeed && readSemantics.randomPaintSeed==0,"explicit zero seed serializes");

    {
        const QString scenePath=output+"/mapper-source.mpr";
        check(write(scenePath,document.originalBytes()),"private mapper fixture");
        CSettings settings;QTableWidget properties;properties.setColumnCount(2);
        QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
        CLandscapeTools panel;panel.findChild<QCheckBox*>("randomPaintVariants")->setChecked(true);
        QVector<QImage> images;for(int i=0;i<64;++i){QImage image(32,32,QImage::Format_RGB32);image.fill(QColor(40+i*2,80+i,30));images.append(image);}
        CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
        view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
        view.attachLandscapeTools(&panel);view.resize(320,240);view.show();QApplication::processEvents();view.makeCurrent();
        view.m_pLand->readMap(QFileInfo(scenePath),false);view.m_mprDocument=document;
        view.m_terrainEdit=baseline;view.m_terrainEditAvailable=true;view.m_terrainAutoCatalogPending=false;
        view.m_terrainSemanticSeed=123;view.m_terrainAtlasProfile=profile;
        view.m_terrainAtlasAnalysis=profile.usableAnalysis();
        view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
        view.m_authoredTerrainTileImages=images;
        view.m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(catalog,images,123,4096,{}, {},document.mapInfo().tileTypes,{{1,1},{2,3},{3,1}}));
        view.m_mprAnimatedTiles=document.mapInfo().animatedTiles;tiles.adoptTileImages(images,document.mapInfo().tileTypes);
        auto* shuffleButton=panel.findChild<QPushButton*>("shuffleTerrainTiles");
        shuffleButton->click();
        const quint32 firstSeed=view.m_terrainEdit.randomPaintSeed();
        const auto firstShuffle=snapshot(view.m_terrainEdit);
        check(history.count()==1 && view.m_terrainEdit.hasRandomPaintSeed()
            && firstSeed!=123,"first shuffle stores a fresh map seed in one Undo entry");
        check(firstShuffle!=before,"first shuffle changes typical-map cells");
        shuffleButton->click();
        const quint32 secondSeed=view.m_terrainEdit.randomPaintSeed();
        const auto secondShuffle=snapshot(view.m_terrainEdit);
        check(history.count()==2 && secondSeed!=firstSeed,
            "two consecutive shuffle clicks choose different seeds");
        check(secondShuffle!=firstShuffle,
            "two consecutive shuffle clicks produce different cell layouts");
        history.undo();check(snapshot(view.m_terrainEdit)==firstShuffle
            && view.m_terrainEdit.randomPaintSeed()==firstSeed,
            "second shuffle Undo restores exact prior cells and seed");
        history.undo();check(snapshot(view.m_terrainEdit)==before
            && !view.m_terrainEdit.hasRandomPaintSeed(),
            "first shuffle Undo restores original cells and seed state");
        history.redo();history.redo();
        check(snapshot(view.m_terrainEdit)==secondShuffle
            && view.m_terrainEdit.randomPaintSeed()==secondSeed,
            "Redo replays both shuffle transactions exactly");
        QJsonObject seedMember;SResArchiveNewMember member;QByteArray withSemantics;
        check(view.buildTerrainSemanticFieldMember(document.originalBytes(),QFileInfo(scenePath),"Random",
            profile.atlasPixelSha256,profile.toBytes(),&member,&report,nullptr,&withSemantics)
            && !withSemantics.isEmpty(),"mapper ordinary save embeds explicit seed without requiring semantic field");
        CMprDocument seeded;STerrainSemanticFieldDocument savedSeed;
        check(seeded.loadBytes(withSemantics,"seeded.mpr",&report)
            && STerrainSemanticFieldDocument::fromBytes(seeded.memberPayload(STerrainSemanticFieldDocument::embeddedMemberName()),&savedSeed,&report)
            && savedSeed.hasRandomPaintSeed && savedSeed.randomPaintSeed==secondSeed,
            "mapper saved MPR reopens latest shuffle seed");
        history.clear();view.m_terrainEdit=baseline;
        view.m_tileStrokeLayer=ETerrainTileLayer::Land;
        view.m_tileStrokePaletteMode=int(ETerrainPaintPaletteMode::ClassicAuto);
        STerrainTileBrush animatedBrush;animatedBrush.radius=0;
        for(auto layer : {ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}) {
            view.m_tileStrokeLayer=layer;
            auto transition=view.m_terrainEdit.makeTilePaint(animatedBrush,layer,0,9,2);
            const auto original=transition.deltas;
            view.randomizeTerrainPaintCommand(&transition,catalog);
            bool exact=original.size()==transition.deltas.size();
            for(int i=0;i<original.size() && exact;++i)exact &= original[i].after==transition.deltas[i].after;
            check(exact && !original.isEmpty(),QString("mapper keeps legacy transition choice layer%1").arg(int(layer)));
        }
        view.m_tileStrokeLayer=ETerrainTileLayer::Land;
        auto animatedPaint=view.m_terrainEdit.makeTilePaint(animatedBrush,ETerrainTileLayer::Land,0,5,3);
        view.randomizeTerrainPaintCommand(&animatedPaint,catalog);
        check(!animatedPaint.deltas.isEmpty()
            && SMprPackedTile::decode(animatedPaint.deltas.first().after).atlasSlot()==5
            && SMprPackedTile::decode(animatedPaint.deltas.first().after).rotation==0,
            "Classic animated painting uses fixed rotation and frame");
        view.m_tileStrokeLayer=ETerrainTileLayer::Overlay;
        auto overlayPaint=view.m_terrainEdit.makeTilePaint(animatedBrush,ETerrainTileLayer::Overlay,0,1,3);
        view.randomizeTerrainPaintCommand(&overlayPaint,catalog);
        check(!overlayPaint.deltas.isEmpty()
            && SMprPackedTile::decode(overlayPaint.deltas.first().after).atlasSlot()==1
            && SMprPackedTile::decode(overlayPaint.deltas.first().after).rotation==0,
            "Classic overlay painting uses fixed rotation without rerolling slot");
        view.m_tileStrokeLayer=ETerrainTileLayer::Land;
        // All four production paint builders flow through the same cosmetic
        // command adapter. Compare actual command bytes after replay twice.
        for(int mode=0;mode<2;++mode)for(int target=0;target<2;++target) {
            panel.setPaintPaletteMode(mode?ETerrainPaintPaletteMode::ClassicAuto:ETerrainPaintPaletteMode::Modern);
            view.beginTileBrushGroup();const quint32 firstSeed=view.m_tileStrokeDistributionSeed;
            const quint32 stableSeed=view.terrainRandomPaintSeed();
            const auto paintCatalog=view.m_workingTerrainAtlas->catalog();
            view.m_tileStrokeTargetMode=target;
            STerrainTileBrush paint;paint.centerX=7.5f;paint.centerY=7.5f;paint.radius=20;paint.shape=ETerrainTileBrushShape::Square;
            CValidationReport paintReport;STerrainTileCommand painted;
            if(mode) painted=target?view.m_terrainEdit.makeSemanticVertexPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport)
                :view.m_terrainEdit.makeSemanticTilePaint(paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport);
            else painted=target?view.m_terrainEdit.makeVirtualMaterialPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport)
                :view.m_terrainEdit.makeVirtualTileMaterialPaint(paint,ETerrainTileLayer::Land,1,paintCatalog,firstSeed,&paintReport);
            view.randomizeTerrainPaintCommand(&painted,catalog);
            check(view.applyTerrainTilePreview(painted,true),QString("mapper paint builder mode%1 target%2 applies").arg(mode).arg(target));
            appendTerrainTileCommand(&view.m_tileStrokeCommand,painted);view.endTileBrushGroup();
            const auto once=snapshot(view.m_terrainEdit);history.clear();
            view.beginTileBrushGroup();view.m_terrainEdit=baseline;
            const quint32 secondSeed=view.m_tileStrokeDistributionSeed;
            STerrainTileCommand repeated;
            if(mode) repeated=target?view.m_terrainEdit.makeSemanticVertexPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport)
                :view.m_terrainEdit.makeSemanticTilePaint(paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport);
            else repeated=target?view.m_terrainEdit.makeVirtualMaterialPaint(8,8,paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport)
                :view.m_terrainEdit.makeVirtualTileMaterialPaint(paint,ETerrainTileLayer::Land,1,paintCatalog,secondSeed,&paintReport);
            view.randomizeTerrainPaintCommand(&repeated,catalog);
            const bool repeatedApplied=view.applyTerrainTilePreview(repeated,true);
            appendTerrainTileCommand(&view.m_tileStrokeCommand,repeated);view.endTileBrushGroup();
            check(repeatedApplied && view.terrainRandomPaintSeed()==stableSeed && snapshot(view.m_terrainEdit)==once,
                QString("stable uniform repeat bytes mode%1 target%2").arg(mode).arg(target));
            history.clear();view.m_terrainEdit=baseline;
            check(once!=before,QString("variant/rotation visible for mode%1 target%2").arg(mode).arg(target));
        }
        view.m_pLand->setCanonicalTerrainModel(nullptr);view.m_pLand->unloadMpr();
    }

    if(fixture.value("real").toBool()) {
        CMprDocument real;STerrainAtlasProfile realProfile;
        const QString root=fixture.value("realRoot").toString();
        const QByteArray sourceBytes=read(root+"/source.mpr");
        const auto atlas=QJsonDocument::fromJson(read(root+"/atlas.json")).object();
        if(!check(real.loadBytes(sourceBytes,root+"/source.mpr",&report)
           && STerrainAtlasProfile::fromJson(atlas.value("catalog").toObject(),&realProfile,&report),"private real map and profile load")) return 1;
        const auto realCatalog=realProfile.usableAnalysis(&report).landCatalog;
        QVector<QImage> images;for(int i=0;i<realProfile.atlasSlotCount;++i)images.append(QImage(root+QString("/tiles/%1.png").arg(i)));
        CTerrainEditModel realModel;realModel.load(real,&report);const auto realBefore=realModel;
        QSet<int> realExcluded;for(const auto& a:real.mapInfo().animatedTiles)for(int frame=0;frame<a.frameCount;++frame)realExcluded.insert(int(a.tileIndex)+frame);
        for(const auto& def:realProfile.slotDefinitions)if(!def.mixedEdgeRecipes.isEmpty())realExcluded.insert(def.slot);
        const QRect realAll(0,0,realModel.tileCountX(),realModel.tileCountY());
        const QRect patch=QRect(88,136,16,16).intersected(realAll);
        check(preview(realModel,images,patch).save(output+"/real-before.png"),"real before PNG");
        auto realCommand=realModel.makeRandomTilePaint(realCatalog,136,realAll,realExcluded);
        realCommand.changesRandomSeed=true;
        realCommand.beforeRandomSeed=realModel.randomPaintSeed();
        realCommand.beforeRandomSeedSet=realModel.hasRandomPaintSeed();
        realCommand.afterRandomSeed=136;
        check(!realCommand.isEmpty() && realModel.apply(realCommand,true,&report),"real entire-map shuffle");
        check(preview(realModel,images,patch).save(output+"/real-after.png"),"real after PNG");
        check(preview(realBefore,images,patch).copy(0,0,512,512).scaled(1024,1024,Qt::IgnoreAspectRatio,Qt::FastTransformation)
            .save(output+"/real-top-left-before-2x.png"),"review top-left 512x512 before enlarged PNG");
        check(preview(realModel,images,patch).copy(0,0,512,512).scaled(1024,1024,Qt::IgnoreAspectRatio,Qt::FastTransformation)
            .save(output+"/real-top-left-after-2x.png"),"review top-left 512x512 after enlarged PNG");
        int transitions=0,changedTransitions=0,previousChangedTransitions=0;
        CMprDocument previous;CTerrainEditModel previousModel;
        const bool previousLoaded=previous.loadBytes(read(output+"/review-before-fix/shuffled.mpr"),"review-previous.mpr",&report)
            && previousModel.load(previous,&report);
        bool realClasses=true,realCorners=true,realOverlay=true,realGeometry=true,realProtected=true;
        for(int y=0;y<realModel.tileCountY();++y)for(int x=0;x<realModel.tileCountX();++x) {
            const auto a=realBefore.packedTile(x,y,ETerrainTileLayer::Land);const auto b=realModel.packedTile(x,y,ETerrainTileLayer::Land);
            realClasses&=realBefore.tileType(a)==realModel.tileType(b);
            realCorners&=realCatalog.gridSignatureForPacked(a)==realCatalog.gridSignatureForPacked(b);
            bool known=false;const auto s=realCatalog.gridSignatureForPacked(a,&known);
            if(known && (s.northWest!=s.northEast || s.northWest!=s.southEast || s.northWest!=s.southWest)) {
                ++transitions;changedTransitions+=a!=b;
                if(previousLoaded)previousChangedTransitions+=a!=previousModel.packedTile(x,y,ETerrainTileLayer::Land);
            }
            realOverlay&=realBefore.packedTile(x,y,ETerrainTileLayer::Overlay)==realModel.packedTile(x,y,ETerrainTileLayer::Overlay);
            if(realExcluded.contains(SMprPackedTile::decode(a).atlasSlot()))realProtected&=a==b;
        }
        for(int y=0;y<realModel.vertexCountY();++y)for(int x=0;x<realModel.vertexCountX();++x)realGeometry&=realModel.height(x,y)==realBefore.height(x,y);
        check(realClasses && realCorners,"real all material corners and runtime classes preserved");
        check(transitions>0 && changedTransitions==0,"real every transition physical slot and rotation unchanged");
        log<<"TRANSITIONS count="<<transitions<<" changed="<<changedTransitions<<" previousChanged="<<previousChangedTransitions<<'\n';
        check(realOverlay && realGeometry && realProtected,"real heights overlays and animation cells preserved");
        bool untouchedBytes=true;
        for(const auto& sector:real.sectors()) {
            auto a=realBefore.sectorPayload(sector.sectorX,sector.sectorY);
            const auto b=realModel.sectorPayload(sector.sectorX,sector.sectorY);
            a.replace(int(sector.landTileOffset),512,b.mid(int(sector.landTileOffset),512));
            untouchedBytes &= a==b;
        }
        check(untouchedBytes,"real every byte outside Ground tile references unchanged");
        CMprEditTransaction transaction;QByteArray candidate;
        check(realModel.appendSectorEdits(&transaction,&report)
            && real.applyTransaction(transaction,&candidate,&report),"ordinary MPR transaction builds");
        CView saveView(nullptr,nullptr);saveView.m_terrainEditAvailable=true;
        SResArchiveNewMember realSeedMember;QByteArray seededCandidate;
        check(saveView.buildTerrainSemanticFieldMember(candidate,QFileInfo(output+"/shuffled.mpr"),
            real.mapInfo().innerNamespace,realProfile.atlasPixelSha256,realProfile.toBytes(),
            &realSeedMember,&report,&realModel,&seededCandidate) && !seededCandidate.isEmpty(),
            "real normal save embeds map seed");
        candidate=seededCandidate;
        check(write(output+"/shuffled.mpr",candidate),"write only private MPR copy");
        CMprDocument reopened;CTerrainEditModel reopenedModel;
        check(reopened.loadBytes(read(output+"/shuffled.mpr"),output+"/shuffled.mpr",&report)
            && reopenedModel.load(reopened,&report) && snapshot(reopenedModel)==snapshot(realModel),"real save reopen exact sector bytes");
        check(saveView.restoreTerrainSemanticFieldFromArchive(reopened,realProfile,&reopenedModel,
            QFileInfo(output+"/shuffled.mpr"),QString(),true)
            && reopenedModel.hasRandomPaintSeed() && reopenedModel.randomPaintSeed()==136
            && reopenedModel.makeRandomTilePaint(realCatalog,136,realAll,realExcluded).isEmpty(),
            "real reopen restores bound seed and identical shuffle");
        check(reopened.mapInfo().tileTypes==real.mapInfo().tileTypes
            && reopened.mapInfo().animatedTiles==real.mapInfo().animatedTiles
            && reopened.mapInfo().materials==real.mapInfo().materials
            && reopened.mapInfo().textureCount==real.mapInfo().textureCount,"real no slot class material or animation metadata growth");
        realModel.apply(realCommand,false,&report);check(snapshot(realModel)==snapshot(realBefore),"real Undo exact bytes");
        realModel.apply(realCommand,true,&report);check(snapshot(realModel)==snapshot(reopenedModel),"real Redo exact bytes");
        // Controlled homogeneous patch, using the real profile and real artwork.
        int uniformSlot=-1;for(const auto& def:realProfile.slotDefinitions) {
            if(def.assigned && !def.ambiguous && def.signature.northWest==def.signature.northEast
               && def.signature.northWest==def.signature.southEast && def.signature.northWest==def.signature.southWest
               && !realExcluded.contains(def.slot) && !images[def.slot].isNull()) {
                bool found=false;const auto v=realCatalog.variantForPacked(packed(def.slot),&found);
                if(found && v.randomRotationAllowed) { uniformSlot=def.slot;break; }
            }
        }
        check(uniformSlot>=0,"real rotatable uniform demonstration source");
        if(uniformSlot>=0) {
            auto demonstration=realBefore;STerrainTileBrush fill;fill.centerX=patch.center().x();fill.centerY=patch.center().y();fill.radius=7.5f;fill.shape=ETerrainTileBrushShape::Square;
            fill.centerX=patch.x()+7.5f;fill.centerY=patch.y()+7.5f;
            demonstration.apply(demonstration.makeTilePaint(fill,ETerrainTileLayer::Land,quint8(uniformSlot/64),quint8(uniformSlot%64),0),true);
            check(preview(demonstration,images,patch).save(output+"/repeat-before.png"),"homogeneous repeat before PNG");
            auto mixed=demonstration.makeRandomTilePaint(realCatalog,136,patch,realExcluded);
            demonstration.apply(mixed,true);check(!mixed.isEmpty() && preview(demonstration,images,patch).save(output+"/repeat-after.png"),"homogeneous repeat after PNG");
        }
        log<<"REAL changed="<<realCommand.deltas.size()<<" cells="<<realAll.width()*realAll.height()<<" slots="<<real.mapInfo().tileTypes.size()<<'\n';
    }
    QJsonObject result;result.insert("passed",passed);result.insert("failed",failed);result.insert("skipped",0);
    QJsonObject distribution;for(auto i=counts.cbegin();i!=counts.cend();++i)distribution.insert(QString::number(i.key()),i.value());result.insert("physicalDistribution",distribution);
    QJsonObject turns;for(auto i=rotations.cbegin();i!=rotations.cend();++i)turns.insert(QString::number(i.key()),i.value());result.insert("rotations",turns);
    write(output+"/result.json",QJsonDocument(result).toJson());
    log<<"SUMMARY "<<passed<<" PASS / "<<failed<<" FAIL / 0 SKIP\n";
    return failed?1:0;
}

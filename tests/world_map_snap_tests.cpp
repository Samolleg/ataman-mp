#include <QtTest>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QLockFile>
#include "world_map_snap.h"
#include "neighbor_map_layout.h"
#include "world_map_layout_document.h"
#include "world_map_layout_command.h"
#include <limits>
namespace {
SWorldMapZoneOrigin map(QString id,QPointF p=QPointF()) {
    SWorldMapZoneOrigin m;m.profileId="p";m.allodId="w";m.zoneId=id;m.mprIdentity=id;
    m.mprPath=id+".mpr";m.origin=p;m.componentAnchorZoneId="a";m.sectorWidth=m.sectorHeight=1;return m;
}
QByteArray bytes(QString p) { QFile f(p);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll(); }
bool seedMapFiles(const QTemporaryDir& directory) {
    for(const QString& id : {QStringLiteral("a"),QStringLiteral("b"),QStringLiteral("c"),QStringLiteral("d")}) {
        QFile file(directory.filePath(id+".mpr"));
        if(!file.open(QIODevice::WriteOnly) || file.write("owned test fixture")<0) return false;
    }
    return true;
}
QVector<SWorldMapSnapBounds> neighbors() { return {{"a",QRectF(0,0,32,32)}}; }
}
class WorldMapSnapTests : public QObject {
    Q_OBJECT
private slots:
    void fourSides_data() {
        QTest::addColumn<QPointF>("requested");QTest::addColumn<QPointF>("expected");
        QTest::newRow("right")<<QPointF(32.25,4.25)<<QPointF(32,4);
        QTest::newRow("left")<<QPointF(-31.75,4.25)<<QPointF(-32,4);
        QTest::newRow("top")<<QPointF(4.25,-31.75)<<QPointF(4,-32);
        QTest::newRow("bottom")<<QPointF(4.25,32.25)<<QPointF(4,32);
    }
    void fourSides() {
        QFETCH(QPointF,requested);QFETCH(QPointF,expected);
        const auto r=CWorldMapSnap::place(requested,{32,32},{},true,true,neighbors());
        QVERIFY(r.valid);QCOMPARE(r.origin,expected);QVERIFY(!r.overlaps);QVERIFY(!r.seams.isEmpty());
    }
    void cornerAndTJunction() {
        auto bounds=neighbors();bounds.append({"b",QRectF(32,0,32,32)});
        const auto r=CWorldMapSnap::place({32.25,32.25},{32,32},{},true,true,bounds);
        QCOMPARE(r.origin,QPointF(32,32));QVERIFY(r.seams.size()>=2);QVERIFY(!r.overlaps);
    }
    void fourMapDiagram() {
        QVector<QPointF> aligned;
        for(QPointF p:QVector<QPointF>({{0,0},{32,0.25},{0,-31.75},{32,-31.5}})) aligned.append(CWorldMapSnap::align(p,{}));
        QCOMPARE(aligned,QVector<QPointF>({{0,0},{32,0},{0,-32},{32,-32}}));
        for(auto p:aligned) QVERIFY(CWorldMapSnap::onGrid(p,{}));
    }
    void fractionalAnchorAndNegativeCoordinates() {
        const QPointF anchor(0.25,-0.5);
        QCOMPARE(CWorldMapSnap::align({-32.4,-10.9},anchor),QPointF(-31.75,-10.5));
        QCOMPARE(CWorldMapSnap::align({-0.75,-1.5},anchor),anchor);
        QCOMPARE(CWorldMapSnap::align({1.25,0.5},anchor),QPointF(2.25,1.5));
        QVERIFY(!CWorldMapSnap::onGrid({1.25,-0.5},anchor));
    }
    void differentSizesAndDistantEdge() {
        const auto r=CWorldMapSnap::place({32.7,7.3},{64,96},{},true,true,neighbors());
        QCOMPARE(r.origin,QPointF(32,8));
        const auto far=CWorldMapSnap::place({33.3,500},{32,32},{},true,true,neighbors());
        QCOMPARE(far.origin,QPointF(34,500));QVERIFY(far.latch.isEmpty());QVERIFY(far.seams.isEmpty());
    }
    void freePlacement() {
        const QPointF p(31.8125,-0.625);const auto r=CWorldMapSnap::place(p,{32,32},{},false,false,neighbors());
        QVERIFY(r.valid);QCOMPARE(r.origin,p);QVERIFY(r.latch.isEmpty());
    }
    void missingAnchorAndInvalidData() {
        QVERIFY(!CWorldMapSnap::place({32,0},{32,32},{},false,true,neighbors()).valid);
        QVERIFY(!CWorldMapSnap::place({32,0},{0,32},{},true,true,neighbors()).valid);
        QVERIFY(!CWorldMapSnap::place({std::numeric_limits<double>::quiet_NaN(),0},{32,32},{},true,true,neighbors()).valid);
    }
    void incompatibleLegacyNeighbor() {
        const auto r=CWorldMapSnap::place({32.25,4.25},{32,32},{},true,true,{{"legacy",QRectF(0,0.25,32,32)}});
        QVERIFY(r.incompatibleEdge);QVERIFY(r.latch.isEmpty());QVERIFY(r.seams.isEmpty());QVERIFY(CWorldMapSnap::onGrid(r.origin,{}));
    }
    void holdsCompatibleSeam() {
        const auto first=CWorldMapSnap::place({32.5,8},{32,32},{},true,true,neighbors(),2);
        QCOMPARE(first.origin,QPointF(32,8));QVERIFY(!first.latch.isEmpty());
        const auto held=CWorldMapSnap::place({34.5,8},{32,32},{},true,true,neighbors(),2,first.latch);
        QCOMPARE(held.origin,QPointF(32,8));
        const auto released=CWorldMapSnap::place({35.5,8},{32,32},{},true,true,neighbors(),2,held.latch);
        QCOMPARE(released.origin,QPointF(36,8));
    }
    void activeMapDoesNotChangeAnchor() {
        const auto a=map("a",{0.25,-0.5}),b=map("b",{32.25,0.25});QVector<SMapCatalogEntry> topology;
        for(const auto& m:{a,b}) { SMapCatalogEntry t;t.zoneId=m.zoneId;t.allodId="w";t.mprName=m.mprIdentity;t.mprPath=m.mprPath;t.sectorWidth=t.sectorHeight=1;topology.append(t); }
        const auto first=CNeighborMapLayout::build(topology,{a,b},"p","w","a");
        const auto second=CNeighborMapLayout::build(topology,{a,b},"p","w","b");
        QVERIFY(first.hasTileGridAnchor && second.hasTileGridAnchor);QCOMPARE(first.tileGridAnchor,a.origin);QCOMPARE(second.tileGridAnchor,a.origin);
    }
    void legacyLoadAndAtomicGroupHistory() {
        QTemporaryDir tmp;const auto path=tmp.filePath("world.json");CWorldMapLayoutDocument doc(path);
        QVERIFY(seedMapFiles(tmp));
        const auto a=map("a"),b=map("b",{32,0.25}),c=map("c",{0,-31.75}),d=map("d",{32,-31.5});
        QString error;QVERIFY2(doc.addEditorMaps({a,b,c,d},&error),qPrintable(error));const auto original=bytes(path);
        QCOMPARE(doc.loadEditorWorld("p",{}).zones.size(),4);QCOMPARE(bytes(path),original);
        QVector<SWorldMapLayoutChange> changes;
        for(const auto& m:{b,c,d}) { SWorldMapLayoutChange x;x.before=m;x.after=m;x.after.origin=CWorldMapSnap::align(m.origin,{});x.after.provenance=EWorldMapOriginProvenance::Manual;changes.append(x); }
        QUndoStack history;QString failed;
        history.push(new CWorldMapLayoutCommand("align",[&](bool f,QString* e){return doc.applyChanges(changes,f,e);},[&](const QString& e){failed=e;}));
        QCOMPARE(history.count(),1);QVERIFY(failed.isEmpty());
        CWorldMapLayoutDocument reopened(path);auto result=reopened.loadEditorWorld("p",{});
        for(const auto& m:result.zones) QVERIFY(CWorldMapSnap::onGrid(m.origin,{}));
        history.undo();result=doc.loadEditorWorld("p",{});
        QCOMPARE(CWorldMapLayout::zone(result.zones,"p","w","b")->origin,b.origin);
        QCOMPARE(CWorldMapLayout::zone(result.zones,"p","w","b")->provenance,b.provenance);
        history.redo();result=doc.loadEditorWorld("p",{});QCOMPARE(CWorldMapLayout::zone(result.zones,"p","w","b")->origin,QPointF(32,0));
    }
    void addedMapHistory() {
        QTemporaryDir tmp;QVERIFY(seedMapFiles(tmp));CWorldMapLayoutDocument doc(tmp.filePath("world.json"));QString error;QVERIFY(doc.addEditorMaps({map("a")},&error));
        SWorldMapLayoutChange added;added.hadBefore=false;added.after=map("b",{32,0});QUndoStack history;QString failed;
        history.push(new CWorldMapLayoutCommand("add",[&](bool f,QString* e){return doc.applyChanges({added},f,e);},[&](const QString& e){failed=e;}));
        QCOMPARE(doc.loadEditorWorld("p",{}).zones.size(),2);history.undo();QCOMPARE(doc.loadEditorWorld("p",{}).zones.size(),1);
        history.redo();QCOMPARE(doc.loadEditorWorld("p",{}).zones.size(),2);QVERIFY(failed.isEmpty());
    }
    void staleChangeAndExternalFileAreNotOverwritten() {
        QTemporaryDir tmp;const auto path=tmp.filePath("world.json");CWorldMapLayoutDocument doc(path);QString error;
        QVERIFY(seedMapFiles(tmp));
        QVERIFY(doc.addEditorMaps({map("a"),map("b",{32,0.25})},&error));SWorldMapLayoutChange x;x.before=map("b",{32,0.25});x.after=map("b",{32,0});
        SWorldMapLayoutChange stale=x;stale.before.origin={33,0.25};const auto original=bytes(path);
        QVERIFY(!doc.applyChanges({x,stale},true,&error));QCOMPARE(bytes(path),original);
        QFile external(path);QVERIFY(external.open(QIODevice::Append));external.write("\n ");external.close();
        const auto changed=bytes(path);QVERIFY(!doc.applyChanges({x},true,&error));QCOMPARE(bytes(path),changed);
    }
    void writeFailureDoesNotApplyPartialGroup() {
        QTemporaryDir tmp;const auto path=tmp.filePath("world.json");CWorldMapLayoutDocument doc(path);QString error;
        QVERIFY(seedMapFiles(tmp));
        const auto b=map("b",{32,0.25});QVERIFY(doc.addEditorMaps({map("a"),b},&error));SWorldMapLayoutChange x;x.before=b;x.after=b;x.after.origin={32,0};
        QLockFile lock(path+".lock");QVERIFY(lock.tryLock());const auto original=bytes(path);
        QVERIFY(!doc.applyChanges({x},true,&error));QCOMPARE(bytes(path),original);
        QCOMPARE(CWorldMapLayout::zone(doc.loadEditorWorld("p",{}).zones,"p","w","b")->origin,b.origin);
    }
    void verifiedDimensionsSurviveOriginUndo() {
        QTemporaryDir tmp;QVERIFY(seedMapFiles(tmp));CWorldMapLayoutDocument doc(tmp.filePath("world.json"));QString error;
        const auto b=map("b",{32,0.25});QVERIFY(doc.addEditorMaps({map("a"),b},&error));
        SWorldMapLayoutChange x;x.before=b;x.after=b;x.after.origin={32,0};
        auto verified=b;verified.sectorWidth=3;verified.sectorHeight=2;QVERIFY(doc.updateResourceMetadata(verified));
        QVERIFY2(doc.applyChanges({x},true,&error),qPrintable(error));QVERIFY(doc.applyChanges({x},false,&error));
        const auto result=doc.loadEditorWorld("p",{});const auto* restored=CWorldMapLayout::zone(result.zones,"p","w","b");
        QCOMPARE(restored->origin,b.origin);QCOMPARE(restored->sectorWidth,3);QCOMPARE(restored->sectorHeight,2);
    }
    void invalidAdditionAndAnchorRemovalAreAtomic() {
        QTemporaryDir tmp;QVERIFY(seedMapFiles(tmp));const auto path=tmp.filePath("world.json");CWorldMapLayoutDocument doc(path);QString error;
        QVERIFY(doc.addEditorMaps({map("a"),map("b",{32,0})},&error));const auto original=bytes(path);
        SWorldMapLayoutChange add;add.hadBefore=false;add.after=map("c",{64,0});add.after.mprPath="missing.mpr";
        QVERIFY(!doc.applyChanges({add},true,&error));QCOMPARE(bytes(path),original);
        add.after.mprPath="b.mpr";QVERIFY(!doc.applyChanges({add},true,&error));QCOMPARE(bytes(path),original);
        SWorldMapLayoutChange remove;remove.before=map("a");remove.hasAfter=false;
        QVERIFY(!doc.applyChanges({remove},true,&error));QCOMPARE(bytes(path),original);
    }
};
QTEST_GUILESS_MAIN(WorldMapSnapTests)
#include "world_map_snap_tests.moc"

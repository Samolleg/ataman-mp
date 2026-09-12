#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include "world_terrain_session.h"
#include "endurance_document.h"

class WorldTerrainTests : public QObject
{
    Q_OBJECT
    QByteArray fixture(float maxZ=100,quint16 height=10000)
    {
        CMprDocument source; CValidationReport report;
        if(!source.load(QFileInfo(QFINDTESTDATA("fixtures/runtime/particle-flat-7x5.mpr")),&report)) return {};
        SMprCreationRequest request; request.innerNamespace="synthetic";
        request.maxZ=maxZ; request.flatRawHeight=height;
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
private slots:
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

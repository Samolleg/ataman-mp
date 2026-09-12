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
        QElapsedTimer timer;timer.start();
        for(int pass=0;pass<8;++pass)
        {
            const auto stamp=makeWorldTerrainStamp(targets,brush,EWorldSculptMode::Smooth,0,0,1);
            QVERIFY2(s.apply(stamp,true,&error),qPrintable(error));
        }
        double worst=0;
        for(double y=285;y<=291;y+=0.125)
        {
            const int left=y<288?0:1;
            const auto sample=[&](int map,int x){
                const auto& t=targets[map];const double local=y-t.origin.y();
                const int i=int(std::floor(local));const double f=local-i;
                return z(s,t.id,x,i)*(1-f)+z(s,t.id,x,i+1)*f;
            };
            worst=qMax(worst,qAbs(sample(left,targets[left].model->vertexCountX()-1)-sample(2,0)));
        }
        qInfo("Installed triple: eight stamps %lld ms, maximum covered-edge gap %.9f",timer.elapsed(),worst);
        QVERIFY2(worst<0.002,"Installed covered seam exceeds height quantization");
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
            // Compare actual piecewise-linear surfaces, not just coincident vertices.
            for(double y=29;y<=35;y+=0.125)
            {
                auto interpolate=[&](const QString& id,int x,double localY){
                    int i=int(std::floor(localY));double f=localY-i;
                    return horizontal ? z(s,id,i,x)*(1-f)+z(s,id,i+1,x)*f
                                      : z(s,id,x,i)*(1-f)+z(s,id,x,i+1)*f;
                };
                const double left=y<32?interpolate("a",32,y):interpolate("b",32,y-32);
                const double right=interpolate("c",0,y-16.5);
                QVERIFY2(qAbs(left-right)<180.0/65535,"Staggered boundary surfaces must agree throughout covered segments");
            }
            for(auto it=stamp.maps.cbegin();it!=stamp.maps.cend();++it)
            {
                const auto t=*std::find_if(targets.begin(),targets.end(),[&](const SWorldTerrainTarget& t){return t.id==it.key();});
                for(const auto& d:it->deltas)
                    QVERIFY(std::hypot(t.origin.x()+d.globalX-32,t.origin.y()+d.globalY-32)<=5);
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

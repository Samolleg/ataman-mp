#include <QtWidgets>
#include <QtTest>
#include <QtEndian>
#include "nav64_document.h"
#include "navmesh_panel.h"
namespace {
QByteArray bytes(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& p,const QByteArray& b){QSaveFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size()||!f.commit())throw p;}
bool until(const std::function<bool()>& done,int ms=180000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
}
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passed;};
    try{
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_TEST_ROOT")),game="C:/GOG Games/PZ",maps=game+"/MODS/DejaVu/Maps";
        check(!root.isEmpty(),"explicit private fixture root");
        check(nav64SupportedContainers()==QVector<quint32>({3,8,9})&&nav64SupportedAlgorithms()==QVector<quint32>({8,9,10,21,24}),"single accepted wire-pair table");
        for(const auto& profile:nav64Profiles()){
            const auto d=loadNav64(root+"/input24/zone1--zone1."+profile+".nav64");
            if(!d.valid)out<<"READER_ERROR "<<d.error<<"\n";
            check(d.valid&&d.header.container==9&&d.header.algorithm==24&&d.header.fingerprint.isEmpty()&&d.tileCount>0&&d.polygonCount>0&&!d.triangles.isEmpty(),"real r8 profile: "+profile);
            check(!d.detailOutlierVertices&&d.header.body.tileCells==96&&d.header.body.materialClass<=7,"r8 ABI and finite geometry: "+profile);
        }
        for(int algorithm:{8,9,10}){const auto d=loadNav64(root+QString("/legacy%1.nav64").arg(algorithm));check(d.valid&&d.header.algorithm==quint32(algorithm)&&d.header.container==3,"legacy container3: "+QString::number(algorithm));}
        const auto legacy=loadNav64(root+"/input21/zone1--zone1.human.nav64");check(legacy.valid&&legacy.header.container==8&&legacy.header.algorithm==21,"real previous container8/21");
        check(readNav64Bodies(root+"/input24/bodies.navbody").valid&&readNav64Bodies(root+"/input21/bodies.navbody").valid,"BODY1 algorithms24 and21");
        for(const auto& name:{"wrong-domain","wrong-support-count","zero-support-id","truncated"})check(!loadNav64(root+"/"+name+".nav64").valid,QString("modern corrupted input rejected: ")+name);
        const auto good=bytes(root+"/input24/zone1--zone1.human.nav64");
        for(const auto pair:QVector<QPair<quint32,quint32>>{{9,25},{9,21},{8,24},{3,24},{8,23},{9,7}}){auto bad=good;qToLittleEndian<quint32>(pair.first,reinterpret_cast<uchar*>(bad.data()+8));qToLittleEndian<quint32>(pair.second,reinterpret_cast<uchar*>(bad.data()+12));write(root+"/bad.nav64",bad);check(!loadNav64(root+"/bad.nav64").valid,QString("reject incompatible wire pair %1/%2").arg(pair.first).arg(pair.second));}
        write(root+"/truncated24.nav64",good.left(good.size()-1));check(!loadNav64(root+"/truncated24.nav64").valid,"truncated real container9 rejected");
        CNavmeshPanel panel;panel.setAttribute(Qt::WA_DontShowOnScreen);panel.setAttribute(Qt::WA_ShowWithoutActivating);
        panel.setScene(maps+"/zone1.mpr",maps+"/zone1.mob",game,root+"/fixture/Maps");panel.resize(650,760);panel.show();
        check(until([&](){return panel.document().valid&&panel.document().header.algorithm==24;}),"production panel loads installed24 format");
        panel.setScene(maps+"/zone1.mpr",maps+"/zone1.mob",game,maps);panel.setBakerPath(game+"/Engine64/EIRebuild64NavBake.exe");
        check(panel.startBake(false,root+"/output"),"production Ataman232 starts current installed24");
        check(until([&](){return !panel.busy();}),"real zone1 bake finished");
        write(root+"/real-baker.log",panel.bakeLog().toUtf8());
        check(panel.lastBakeSucceeded()&&panel.lastBakeExitCode()==0&&panel.bakeLog().contains("algorithm=24")&&!panel.bakeLog().contains("MAP_ERROR"),"zone1 real exit0 and complete BAKE_DONE errors0");
        check(panel.bakeLog().contains("--fingerprint")==false&&panel.bakeLog().startsWith(QStringLiteral("Отчёты и журнал: ")+root),"private reports outside game, no fingerprint command");
        for(const auto& profile:nav64Profiles()){
            const auto d=loadNav64(root+"/output/Maps/Nav64/zone1--zone1."+profile+".nav64");
            check(d.valid&&d.header.algorithm==24&&d.header.container==9&&!d.detailOutlierVertices,"freshly baked r8 profile read: "+profile);
        }
        check(until([&](){return panel.document().valid&&panel.document().path.startsWith(root+"/output")&&panel.document().header.algorithm==24;}),"successful bake updates production layer to fresh24");
        check(readNav64Bodies(root+"/output/Maps/Nav64/bodies.navbody").valid,"fresh BODY1/24");
        check(panel.grab().save(root+"/baked-zone1-panel.png"),"hidden panel image saved");
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& e){out<<"ERROR "<<e<<"\nRESULT "<<passed<<" PASS 1 FAIL\n";out.flush();return 1;}
}

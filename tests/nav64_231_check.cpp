#include <QtWidgets>
#include <QtTest>
#include "nav64_document.h"
#include "navmesh_panel.h"

namespace {
QByteArray bytes(const QString& p){QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
void write(const QString& p,const QByteArray& b){QSaveFile f(p);if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size()||!f.commit())throw p;}
bool until(const std::function<bool()>& done,int ms=60000){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<ms)QTest::qWait(10);return done();}
}
int runAssemblyCatalogCheck(){
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& what){out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();if(!ok)throw what;++passed;};
    try{
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_NAV64_TEST_ROOT"));
        const QString live=qEnvironmentVariable("EI_NAV64_GAME"),maps=root+"/maps/Maps",nav=maps+"/Nav64";
        check(!root.isEmpty()&&!live.isEmpty(),"explicit isolated run and source");
        for(const auto& profile:nav64Profiles()){
            const auto d=loadNav64(nav+"/zone1--zone1."+profile+".nav64");
            if(!d.valid)out<<"READER_ERROR "<<d.error<<"\n";
            check(d.valid&&d.header.algorithm==21&&d.header.container==8&&d.header.fingerprint.isEmpty()&&d.tileCount>0&&d.polygonCount>0&&!d.triangles.isEmpty(),"real container8 algorithm21: "+profile);
            check(d.detailOutlierVertices==0&&d.header.body.tileCells==96&&d.header.body.materialClass<=7,"profile and finite geometry: "+profile);
        }
        for(int algorithm:{8,9,10}){const auto d=loadNav64(root+QString("/legacy%1.nav64").arg(algorithm));check(d.valid&&d.header.algorithm==quint32(algorithm)&&d.header.container==3,"legacy wire branch: "+QString::number(algorithm));}
        for(const auto& name:{"algorithm22","wrong-pair","wrong-class","wrong-domain","wrong-support-count","zero-support-id","truncated"})check(!loadNav64(root+"/"+name+".nav64").valid,QString("reject malformed/unsupported: ")+name);
        QString error;check(findNav64ForScene(nav,"zone1.mpr","zone1.mob","human",&error).endsWith("human.nav64"),"embedded scene identity");
        check(findNav64ForScene(nav,"wrong.mpr","zone1.mob","human",&error).isEmpty(),"wrong scene rejected");
        check(readNav64Bodies(nav+"/bodies.navbody").valid,"real BODY1 algorithm21");
        auto body=bytes(nav+"/bodies.navbody");body[8]=10;write(root+"/legacy-body.navbody",body);check(readNav64Bodies(root+"/legacy-body.navbody").valid,"legacy BODY1 algorithm10");
        write(root+"/truncated.navbody",body.left(body.size()-1));check(!readNav64Bodies(root+"/truncated.navbody").valid,"truncated body rejected");
        CNavmeshPanel panel;panel.setAttribute(Qt::WA_DontShowOnScreen);panel.setAttribute(Qt::WA_ShowWithoutActivating);
        panel.setScene(maps+"/zone1.mpr",maps+"/zone1.mob",live,maps);panel.setBakerPath(qEnvironmentVariable("EI_NAV64_MOCK"));
        panel.resize(650,850);panel.show();
        check(until([&](){return panel.document().valid;}),"production panel loads algorithm21");
        auto* status=panel.findChild<QLabel*>("navmeshStatus");
        auto* log=panel.findChild<QPlainTextEdit*>("navmeshBakeLog");
        auto run=[&](const char* mode,bool all){qputenv("NAV64_MOCK",mode);check(panel.startBake(all,root+"/mock-output"),QString("production start: ")+mode);check(until([&](){return !panel.busy();}),QString("production finished: ")+mode);write(root+"/"+mode+".log",panel.bakeLog().toUtf8());};
        run("success",false);check(panel.lastBakeSucceeded()&&panel.lastBakeExitCode()==0,"exit0 with complete BAKE_DONE succeeds");
        check(panel.bakeLog().contains("BACKUP zone1--zone1.navgeom")&&panel.bakeLog().contains("BACKUP zone1--zone1.navworld")&&panel.bakeLog().contains("BACKUP bodies.navbody"),"new sidecars protected by backup");
        check(panel.bakeLog().contains("--report-root")&&panel.bakeLog().contains("--map")&&!panel.bakeLog().contains("--all"),"map invocation has mandatory private reports");
        run("all",true);check(panel.lastBakeSucceeded()&&panel.bakeLog().contains("--all"),"all-map command and success");
        run("quality",false);check(!panel.lastBakeSucceeded()&&panel.lastBakeExitCode()==4&&status->text().contains("quality failures=4")&&log->isVisible(),"quality error preserved in status and log opens");
        check(panel.grab().save(root+"/quality-panel.png"),"offscreen quality panel saved");
        run("missing_done",false);check(!panel.lastBakeSucceeded(),"exit0 without BAKE_DONE cannot succeed");
        run("map_missing",false);check(!panel.lastBakeSucceeded()&&panel.lastBakeExitCode()==5,"missing input remains failure");
        qputenv("NAV64_MOCK","cancel");check(panel.startBake(false,root+"/mock-output"),"cancel scenario starts");panel.cancelBake();check(until([&](){return !panel.busy();})&&!panel.lastBakeSucceeded(),"cancel never succeeds");
        panel.setBakerPath(qEnvironmentVariable("EI_NAV64_BAKER"));
        const QString liveMaps=live+"/MODS/DejaVu/Maps";
        panel.setScene(liveMaps+"/zone1.mpr",liveMaps+"/zone1.mob",live,liveMaps);
        check(panel.startBake(false,root+"/real-output"),"real installed baker through production startBake");
        check(until([&](){return !panel.busy();},180000),"real baker finished");
        write(root+"/real-baker.log",panel.bakeLog().toUtf8());
        check(panel.lastBakeExitCode()!=2&&panel.bakeLog().contains("BAKE_BEGIN")&&panel.bakeLog().contains("algorithm=21"),"new required arguments accepted by installed algorithm21");
        if(panel.lastBakeSucceeded()){
            check(panel.bakeLog().contains("BAKE_DONE")&&!panel.bakeLog().contains("MAP_ERROR"),"real successful bake has no MAP_ERROR");
            for(const auto& profile:nav64Profiles()){const auto d=loadNav64(root+"/real-output/Maps/Nav64/zone1--zone1."+profile+".nav64");check(d.valid&&d.header.algorithm==21,"read actual baked profile: "+profile);}
        }else{
            check(panel.lastBakeExitCode()==4&&panel.bakeLog().contains("route-to-step release quality failures=4"),"external algorithm21 quality blocker remains explicit");
            check(status->text().contains("quality failures=4"),"real failure cause remains visible");
            out<<"REAL_BAKE_NOT_SUCCESSFUL exit="<<panel.lastBakeExitCode()<<"\n";
        }
        check(panel.grab().save(root+"/real-bake-panel.png"),"offscreen real bake panel saved");
        panel.setScene(liveMaps+"/bz19g.mpr",liveMaps+"/bz19g.mob",live,liveMaps);
        check(panel.startBake(false,root+"/second-output"),"independent bz19g successful-output scenario starts");
        check(until([&](){return !panel.busy();},180000),"bz19g baker finishes");
        write(root+"/bz19g-baker.log",panel.bakeLog().toUtf8());
        check(panel.lastBakeSucceeded()&&panel.lastBakeExitCode()==0,"bz19g real exit0 and complete BAKE_DONE");
        for(const auto& profile:nav64Profiles()){const auto d=loadNav64(root+"/second-output/Maps/Nav64/bz19g--bz19g."+profile+".nav64");check(d.valid&&d.header.algorithm==21,"read freshly baked bz19g profile: "+profile);}
        check(until([&](){return panel.document().valid&&panel.document().header.mpr=="bz19g.mpr";}),"successful bake refreshes actual production layer");
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"ERROR "<<error<<"\nRESULT "<<passed<<" PASS 1 FAIL\n";out.flush();return 1;}
}

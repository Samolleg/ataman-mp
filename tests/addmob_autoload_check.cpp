#include <QtWidgets>
#include <QtTest>
#include <QtEndian>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "resource_setup_dialog.h"
#include "collection_document.h"
#include "process_edit_lock.h"

namespace {
struct Hidden : QObject {
    bool eventFilter(QObject* o, QEvent* e) override {
        if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o)) if(w->isWindow()) w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
void write(const QString& path,const QByteArray& bytes) { QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())throw path; }
QByteArray u32(quint32 v) { QByteArray b(4,'\0');qToLittleEndian(v,reinterpret_cast<uchar*>(b.data()));return b; }
QByteArray section(quint32 id,const QByteArray& b={}) { return u32(id)+u32(b.size()+8)+b; }
QByteArray mob(const QByteArray& text) {
    quint32 key=0x12345678;QByteArray encrypted=u32(key);
    for(char c:text) { quint32 t=((((key*13)<<4)+key)<<8)-key;key+=(t<<2)+2531011;encrypted.append(char(c^char(key>>16))); }
    return section(0xA000,section(0xC000)+section(0xB000)+section(0xACCEECCB,encrypted));
}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    auto check=[&](bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<"\n";out.flush();if(!ok)throw text;++passed;};
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_ADDMOB_ROOT")),maps=root+"/maps";
        check(!root.isEmpty()&&QDir(maps).exists(),"explicit isolated map root");
        QDir().mkpath(root+"/synthetic");const QString synth=root+"/synthetic";
        write(synth+"/root.mob",mob("AddMob(\"first.mob\")\nAddMob(\"FIRST.MOB\")\nAddMob(\"broken.mob\")\nAddMob(\"last.mob\")\nAddMob(\"missing.mob\")\nAddMob(GetQuestMob())"));
        write(synth+"/first.mob",mob("AddMob(StrCat(\"nest\",\"ed.mob\"))\nAddMob(\"last.mob\")"));
        write(synth+"/nested.mob",mob("AddMob(\"root.mob\")\nAddMob(\"first.mob\")"));
        write(synth+"/last.mob",mob("// AddMob(\"comment.mob\")\n/* AddMob(\"other.mob\") */\nPrint(\"AddMob(\\\"string.mob\\\")\")"));
        write(synth+"/broken.mob","invalid");
        QStringList warnings;
        auto paths=CMobFamilyValidator::addMobPaths("AddMob(\"nest\" + \"ed.mob\")\nAddMob(((\"last\")))",synth+"/root.mob",{},&warnings);
        check(paths.size()==2&&warnings.isEmpty(),"constant concatenation, parentheses and missing suffix");
        paths=CMobFamilyValidator::addMobPaths("AddMob(\"last.mob\"",synth+"/root.mob",{},&warnings);
        check(paths.isEmpty()&&warnings.size()==1,"incomplete call is diagnosed");
        warnings.clear();
        paths=CMobFamilyValidator::addMobPaths("// AddMob(\"x.mob\")\n/* AddMob(\"y.mob\") */\nPrint(\"AddMob(\\\"last.mob\\\")\")",synth+"/root.mob",{},&warnings);
        check(paths.isEmpty()&&warnings.isEmpty(),"comments and quoted pseudo-calls ignored");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        auto& view=*window.view();window.resize(1200,800);window.show();view.show();qApp->processEvents();view.makeCurrent();
        check(view.isValid(),"production offscreen OpenGL context");
        int unexpectedDialogs=0;QTimer dialogGuard;
        QObject::connect(&dialogGuard,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){++unexpectedDialogs;out<<"UNEXPECTED_DIALOG "<<box->text()<<"\n";box->reject();}});dialogGuard.start(10);
        view.m_neighborLandscapesVisible=false;
        QMap<QString,QByteArray> initial;
        for(const QString& dir:{maps,synth})for(const auto& file:QDir(dir).entryInfoList(QDir::Files))initial[file.absoluteFilePath()]=read(file.absoluteFilePath());
        auto settle=[&](){QElapsedTimer timer;timer.start();while(window.backgroundMobLoading()&&timer.elapsed()<90000)QTest::qWait(10);check(!window.backgroundMobLoading(),"background AddMob traversal settled");};
        auto load=[&](QFileInfo file){const bool ok=window.loadMobWithProgress(file);settle();return ok;};
        auto openPair=[&](const QString& mpr,const QString& mob,const QString& zone){window.openMapPair(mpr,mob,zone);settle();};
        auto listed=[&](int expected){window.rebuildLoadedMapPanel();check(window.m_loadedMapTree->topLevelItem(0)->childCount()==expected,"all opened MOBs appear in loaded map list");};
        check(load(QFileInfo(synth+"/root.mob")),"standalone MOB opens recursive family despite failed dependency");
        check(view.mobs().size()==4,"nested/shared/cyclic dependencies load exactly once and continue after malformed MOB");
        check(view.currentMob()->filePath().fileName()=="root.mob"&&!view.currentMob()->loadedFromAddMob(),"primary remains active and explicitly opened");
        listed(4);
        check(unexpectedDialogs==0,"malformed dependency continues without a modal error dialog");
        for(auto* m:view.mobs()) {check(!m->isDirty(),"auto-open leaves document clean: "+m->mobName());if(m!=view.currentMob())check(m->loadedFromAddMob(),"dependency retains conditional runtime provenance: "+m->mobName());}
        const auto* primary=view.currentMob();
        check(load(QFileInfo(synth+"/ROOT.MOB"))&&view.mobs().size()==4&&view.currentMob()==primary,"reopen/case alias does not duplicate family");
        check(window.m_mapTestResultNotification&&!window.m_mapTestResultNotification->isHidden(),"missing/dynamic/malformed dependency warning visible");
        check(load(QFileInfo(synth+"/last.mob"))&&!view.currentMob()->loadedFromAddMob(),"explicit manual open promotes auto dependency for test composition");
        openPair(maps+"/zone15.mpr",maps+"/zone15.mob","gz15h");
        check(view.mobs().size()==4&&view.currentMob()->mobName().compare("zone15.mob",Qt::CaseInsensitive)==0,"owner gz15h opens primary and three AddMob files");
        listed(4);
        QSet<QString> names;for(auto* m:view.mobs())names.insert(m->filePath().fileName().toLower());
        check(names==QSet<QString>{"zone15.mob","autozone15heroes.mob","autozone15canyon.mob","autozone15night.mob"},"exact owner family is loaded");
        window.m_loadedMapTree->grab().save(root+"/loaded-zone15.png");
        check(window.isExitAllowed(),"all owner-family documents remain clean");
        openPair(maps+"/zone2.mpr",synth+"/root.mob","synthetic");
        check(view.mobs().size()==4&&view.currentMob()->mobName()=="root.mob","switch removes previous family and recursively loads next");
        listed(4);
        auto* dirty=view.currentMob();dirty->setScript(dirty->script()+"\n// unsaved");dirty->setDirty();
        dialogGuard.stop();
        int prompts=0;QTimer cancel;QObject::connect(&cancel,&QTimer::timeout,[&](){for(auto* w:QApplication::topLevelWidgets())if(auto* box=qobject_cast<QMessageBox*>(w)){++prompts;auto* b=box->button(QMessageBox::Cancel);if(b)b->click();else box->reject();}});cancel.start(10);
        openPair(maps+"/zone15.mpr",maps+"/zone15.mob","gz15h");cancel.stop();
        check(prompts>0&&view.currentMob()==dirty&&dirty->isDirty()&&view.mobs().size()==4,"Cancel switching preserves unsaved primary and complete family");dirty->setDirty(false);dialogGuard.start(10);
        SEnduranceCollection collection;collection.name="auto";collection.mprReference=maps+"/zone2.mpr";collection.mobReferences<<synth+"/first.mob"<<synth+"/last.mob";
        QString error;check(CCollectionDocument::save(root+"/test.collection",collection,&error),"isolated collection saved");window.openCollection(root+"/test.collection");settle();
        check(view.mobs().size()==4&&view.currentMob()->mobName()=="last.mob"&&!view.currentMob()->loadedFromAddMob(),"collection adds nested dependencies and keeps last explicit MOB active");listed(4);
        openPair(maps+"/zone15.mpr",maps+"/zone15.mob","gz15h");check(view.mobs().size()==4,"repeated owner-map opening restores complete family");
        for(auto i=initial.begin();i!=initial.end();++i)check(read(i.key())==i.value(),"source bytes unchanged: "+QFileInfo(i.key()).fileName());
        check(unexpectedDialogs==0,"all normal map/collection transitions complete without unexpected dialogs");dialogGuard.stop();
        window.closeAll();window.close();qApp->removeEventFilter(&hidden);
        out<<"RESULT "<<passed<<" PASS 0 FAIL\n";out.flush();return 0;
    }catch(const QString& error){out<<"RESULT FAIL "<<error<<"\n";out.flush();return 1;}
}

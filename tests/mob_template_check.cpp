#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "settings.h"
#include "landscape.h"
#include "resourcemanager.h"
#include "resource_setup_dialog.h"
#include "mob.h"
#include "endurance_document.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* o, QEvent* e) override {
        if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o))
                if(w->isWindow()) w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray read(const QString& p) {QFile f(p);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
}

int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");int passed=0;
    const auto check=[&](bool ok,const QString& name){out<<(ok?"PASS ":"FAIL ")<<name<<"\n";out.flush();if(!ok)throw name;++passed;};
    try {
        const QString fixture=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MOB_TEMPLATE_FIXTURE"));
        const QString output=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MOB_TEMPLATE_OUTPUT"));
        check(!fixture.isEmpty()&&!output.isEmpty(),"explicit private fixture/output");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,output+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTranslator translator;
        check(translator.load(":/i18n/ei_mapper_ru.qm"),"Russian UI translations loaded");
        qApp->installTranslator(&translator);
        Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.m_currentCollectionPath=output+"/private.collection";
        auto& view=*window.view();window.resize(1200,850);window.show();view.show();QCoreApplication::processEvents();view.makeCurrent();
        check(view.isValid(),"production MainWindow hidden OpenGL context");
        view.loadLandscape(QFileInfo(fixture+"/Maps/gipat2.mpr"),false);
        check(CLandscape::getInstance()->isMprLoad()&&view.mobs().isEmpty(),"open private gipat2 MPR without MOB");
        const QString source=fixture+"/Maps/zone1.mob";const QByteArray sourceBytes=read(source);
        CMob expected;expected.setFileName(QFileInfo(source));
        check(expected.deserialize(sourceBytes),"independent full legacy source projection");
        check(!expected.nodes().isEmpty(),"zone1 source really contains objects");
        const SMobParameterState expectedState=expected.parameterState();
        const QString corrupt=fixture+"/Maps/corrupt.mob";
        {QFile f(corrupt);check(f.open(QIODevice::WriteOnly)&&f.write("broken",6)==6,"private corrupt source");}
        SMobParameterState state;QString error;
        check(!CMob::readTemplateParameters(QFileInfo(corrupt),&state,&error)&&!error.isEmpty(),"corrupt source returns diagnostic");
        check(!CMob::readTemplateParameters(QFileInfo(fixture+"/missing.mob"),&state,&error)&&!error.isEmpty(),"unreadable source returns diagnostic");
        check(CMob::readTemplateParameters(QFileInfo(source),&state,&error)&&state==expectedState,"disk extraction equals full source settings");
        check(read(source)==sourceBytes,"source bytes preserved by metadata extraction");
        const auto exclusiveSource=[&](){HANDLE h=CreateFileW(reinterpret_cast<LPCWSTR>(source.utf16()),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);bool ok=h!=INVALID_HANDLE_VALUE;if(ok)CloseHandle(h);return ok;};
        check(exclusiveSource(),"source handle released after reading");

        // Drive the actual production modal, both file dialogs, save and reopen.
        const auto create=[&](const QString& target,int mode,bool screenshot){
            QString failure;bool started=false;bool sourceChosen=false;int warnings=0;int fileStep=0;
            QTimer driver;driver.setInterval(20);
            QTimer dialogs;dialogs.setInterval(20);
            QObject::connect(&dialogs,&QTimer::timeout,[&](){
                for(auto* widget:QApplication::topLevelWidgets()) {
                    if(auto* message=qobject_cast<QMessageBox*>(widget)) {
                        if(!message->isVisible())continue;
                        out<<"DIALOG "<<message->text()<<"\n";out.flush();
                        ++warnings;
                        if(mode!=2 || warnings>1)failure="unexpected message: "+message->text();
                        message->accept();return;
                    }
                    if(auto* file=qobject_cast<QFileDialog*>(widget)) {
                        if(!file->isVisible())continue;
                        if(file->property("driven217").toBool())continue;
                        file->setProperty("driven217",true);
                        if(file->fileMode()==QFileDialog::ExistingFile) {
                            const QString input=(mode==2&&fileStep++==0)?corrupt:source;
                            file->selectFile(input);
                        } else file->selectFile(target);
                        QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);return;
                    }
                }
            });
            QObject::connect(&driver,&QTimer::timeout,[&](){
                if(started)return;
                for(auto* widget:QApplication::topLevelWidgets()) {
                    auto* dialog=qobject_cast<QDialog*>(widget);if(!dialog)continue;
                    auto* combo=dialog->findChild<QComboBox*>("templateMob");if(!combo)continue;
                    started=true;
                    try {
                        check(dialog->width()==650,"new MOB dialog width remains 650");
                        check(combo->count()>3,"profile MOB choices populated without source loaded");
                        bool zone=false;int picker=-1;int loaded=-1;int profile=-1;
                        for(int i=0;i<combo->count();++i) {
                            const QString path=combo->itemData(i,Qt::UserRole+1).toString();
                            if(QFileInfo(path).fileName().compare("zone1.mob",Qt::CaseInsensitive)==0&&profile<0){zone=true;profile=i;}
                            if(combo->itemData(i,Qt::UserRole+2).toBool())picker=i;
                            if(combo->itemData(i).toULongLong()!=0)loaded=i;
                        }
                        check(zone&&picker>=0,"profile zone/map names plus file picker exist");
                        dialog->findChild<QComboBox*>("newMobRole")->setCurrentIndex(0);
                        dialog->findChild<QCheckBox*>("newMobOpenParameters")->setChecked(false);
                        if(screenshot) {
                            // Native Qt widget snapshot includes the real expanded popup.
                            combo->showPopup();QTest::qWait(100);
                            QAbstractItemView* popup=combo->view();QWidget* popupWindow=popup->window();
                            const QRect dialogRect(QPoint(0,0),dialog->size());
                            const QRect popupRect(dialog->mapFromGlobal(popupWindow->mapToGlobal(QPoint())),popupWindow->size());
                            const QRect bounds=dialogRect.united(popupRect);
                            QPixmap snapshot(bounds.size());snapshot.fill(Qt::transparent);QPainter p(&snapshot);
                            p.drawPixmap(-bounds.topLeft(),dialog->grab());p.drawPixmap(popupRect.topLeft()-bounds.topLeft(),popupWindow->grab());p.end();
                            check(snapshot.save(output+"/new-mob-expanded.png"),"native dialog and expanded source list snapshot");
                            popup->scrollToBottom();QTest::qWait(50);
                            QPixmap bottom(bounds.size());bottom.fill(Qt::transparent);QPainter pb(&bottom);
                            pb.drawPixmap(-bounds.topLeft(),dialog->grab());pb.drawPixmap(popupRect.topLeft()-bounds.topLeft(),popupWindow->grab());pb.end();
                            check(bottom.save(output+"/new-mob-expanded-file.png"),"expanded source list with Choose file entry snapshot");combo->hidePopup();
                        }
                        if(mode==1) {check(loaded>=0,"loaded MOB entry remains available");combo->setCurrentIndex(loaded);}
                        else if(mode==3)combo->setCurrentIndex(profile);
                        else {
                            check(view.mobs().isEmpty(),"file source is not loaded in mapper");
                            combo->setCurrentIndex(picker);
                            if(mode==2) {
                                check(combo->currentIndex()==0&&warnings==1,"corrupt selection keeps dialog usable and previous choice");
                                combo->setCurrentIndex(picker);
                            }
                        }
                        sourceChosen=true;
                        check(exclusiveSource(),"source is not locked while creation dialog stays open");
                        for(const char* name:{"copyMobRanges","copyMobScript","copyMobDiplomacy","copyMobWorld"}) {
                            auto* box=dialog->findChild<QCheckBox*>(name);check(box&&box->isEnabled(),QString("enabled ")+name);box->setChecked(true);
                        }
                        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                    } catch(const QString& e){failure=e;dialog->reject();}
                    return;
                }
            });
            QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,[&](){failure="modal scenario timed out";for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))d->reject();});
            dialogs.start();driver.start();timeout.start(60000);window.on_actionNew_MOB_triggered();driver.stop();dialogs.stop();timeout.stop();
            check(failure.isEmpty()&&sourceChosen,"production create/save path: "+failure);
            CMob reopened;reopened.attach(&view,view.m_pProgress);
            QFileInfo saved(target);check(reopened.readMob(saved,false)==0,"fresh reopen generated MOB");
            check(reopened.parameterState()==expectedState,"all four copied settings match independent zone1 projection");
            CMobDocument document;CValidationReport report;check(document.load(saved,&report),"saved MOB passes structural parser");
            check(document.objects().isEmpty()&&reopened.nodes().isEmpty(),"no objects copied");
            check(!document.hasEmbeddedAiGraph()&&!document.hasSection(0x1E00)&&!document.hasSection(0xE000)&&!document.hasSection(0xF000),"no AI graph VSS or editor directories copied");
            check(read(source)==sourceBytes,"source zone1 MOB byte-identical after create/save/reopen");
            for(CMob* mob:view.mobs())view.unloadMobDocument(mob,false);
        };
        create(fixture+"/Maps/gipat2.mob",0,true);
        create(fixture+"/Maps/recovered.mob",2,false);
        create(fixture+"/Maps/profile.mob",3,false);
        // Load the original source through the old in-memory selection route.
        CMob* loadedSource=new CMob;
        loadedSource->setFileName(QFileInfo(source));
        check(loadedSource->deserialize(sourceBytes),"full source loaded as existing MOB document");
        view.m_aMob.append(loadedSource);
        create(fixture+"/Maps/loaded.mob",1,false);
        out<<"PASS mob-template "<<passed<<" checks\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL mob-template "<<e<<"\n";out.flush();return 1;}
}

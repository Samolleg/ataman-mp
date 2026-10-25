#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "camera.h"
#include "operationmanager.h"
#include "key_manager.h"
#include "travel_panel.h"
#include "travel_panel225.h"
#include "travel_destination.h"
#include "travel_document.h"
#include "res_archive_document.h"
#include "mob.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "settings.h"
#include "endurance_document.h"
#include "game_text_catalog.h"

namespace {
QTextStream out(stdout);int checks=0;
void check(bool ok,const QString& label){out<<(ok?"PASS ":"FAIL ")<<label<<"\n";out.flush();if(!ok)throw label;++checks;}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw path;return f.readAll();}
struct Hidden:QObject {
    bool eventFilter(QObject* o,QEvent* e)override {
        if(e->type()==QEvent::Polish || e->type()==QEvent::Show)
            if(auto* w=qobject_cast<QWidget*>(o))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
void activate(QComboBox* box,int row){check(row>=0,"destination option exists");box->setCurrentIndex(row);emit box->activated(row);}
}

// Linked to the freshly built production objects, using the existing isolated
// native-check entry point. Never sends desktop input or uses the clipboard.
int runAssemblyCatalogCheck()
{
    out.setCodec("UTF-8");Hidden hidden;qApp->installEventFilter(&hidden);
    try {
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_TRAVEL_TEST_ROOT"));
        check(!root.isEmpty(),"explicit private test root");
        const auto maps=root+"/Starter/MODS/DejaVu/Maps";
        CMprDocument mpr;CMobDocument mob;CValidationReport report;
        const auto mobBefore=read(maps+"/zone5_1.mob"),mprBefore=read(maps+"/zone5_1.mpr");
        check(mpr.load(QFileInfo(maps+"/zone5_1.mpr"),&report)&&mob.load(QFileInfo(maps+"/zone5_1.mob"),&report),"actual copied zone5_1 MPR/MOB validate");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        const auto archivePath=root+"/Starter/MODS/DejaVu/res/texts.res";
        const auto original=read(archivePath);CResArchiveDocument archive;
        check(archive.loadBytes(original),"copied Dejavu texts.res parses");
        auto doc=QSharedPointer<CTravelDocument>::create();QString error;
        check(doc->open(archivePath,&error),"production travel document opens actual map.txt: "+error);
        const auto zones=doc->table().zones();const int source=doc->uniqueZone("gz5g");
        check(source>=0 && zones[source].mprName.compare("zone5_1",Qt::CaseInsensitive)==0,"zone5_1 resolves to gz5g");
        const int selected=travel::entryIndex(zones[source],2);check(selected>=0,"transition2 exists");
        const auto e=zones[source].exits[selected];const auto d=travel::resolve(zones,e.destinationZoneId,e.destinationExitId);
        check(d.error.isEmpty() && d.route>=0 && zones[d.route].zoneId=="gz5g_gz9g2" && zones[d.zone].zoneId=="gz9g" && zones[d.zone].exits[d.entry].exitId==4,"actual route chain is gz5g:2 -> route near1 -> gz9g:4");
        CTravelPanel225 before;before.setAttribute(Qt::WA_DontShowOnScreen);before.resize(700,1400);
#ifndef TRAVEL_UX_R2
        before.populate(zones,source,selected,false);before.revealArea(true);before.show();QApplication::processEvents();
        check(before.grab().save(root+"/panel-before.png"),"before PNG rendered from225 panel source in fresh harness");before.hide();
#endif
        qApp->setProperty("workspaceLoadSmoke",true);MainWindow window;window.configureAutosaveTimer(false,300000);
        window.resize(1280,950);window.show();window.view()->makeCurrent();window.view()->setMouseTracking(true);
        const bool sceneOnly=qEnvironmentVariableIsSet("EI_TRAVEL_SCENE_ONLY") && qEnvironmentVariable("EI_TRAVEL_SCENE_ONLY")=="1";
        if(sceneOnly){
            window.openMapPair(maps+"/zone5_1.mpr",maps+"/zone5_1.mob","gz5g");
            check(CLandscape::getInstance()->isMprLoad() && window.view()->currentMob(),"actual copied zone5_1 scene and MOB opened through production MainWindow");
        }
        auto* toolbar=window.findChild<QToolButton*>("travelToolbarButton");check(toolbar,"production travel toolbar button exists");
        toolbar->click();
        auto* view=window.view();check(view->travelEditing(),"click toolbar enters travel mode");
        // Native events reach the same CView methods as real keyboard input;
        // advancing the real camera verifies motion, not only a key-set bit.
        const auto motion=[&](QWidget* receiver,int key,bool expected){
            QVector3D pos,pivot,rot,after,afterPivot,afterRot;view->m_cam->getLocation(pos,pivot,rot);
            QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier,0,key<256?quint32(key):0,0);
            QApplication::sendEvent(receiver,&press);view->m_cam->move();
            QKeyEvent release(QEvent::KeyRelease,key,Qt::NoModifier,0,key<256?quint32(key):0,0);
            QApplication::sendEvent(receiver,&release);view->m_cam->getLocation(after,afterPivot,afterRot);
            check(((pos!=after || pivot!=afterPivot)==expected),QString("camera key %1 receiver %2 expected motion %3").arg(key).arg(receiver->metaObject()->className()).arg(expected));
            check(!view->m_cam->movementInputActive(),"key release stops movement");
        };
#ifdef TRAVEL_UX_R2
        {
        const auto initial=doc->snapshot();
        window.m_travelDocument=doc;window.m_travelDocuments.insert(archivePath,doc);window.m_travelZone=source;window.m_travelExit=selected;window.refreshTravelEditor();
        auto* panel=window.m_travelPanel;panel->revealArea(true);
        QMap<QString,QString> names;const auto texts=ei::loadGameTextCatalog({QFileInfo(archivePath).absolutePath()});
        for(const auto& z:zones){const auto title=texts.entries.value("zone "+z.zoneId).section('\n',0,0).trimmed();if(!title.isEmpty())names.insert(z.zoneId,title);}
        before.setMapNames(names);panel->setMapNames(names);
        const auto screenshot=[&](QWidget* widget,const QString& name){
            widget->setParent(nullptr);widget->setAttribute(Qt::WA_DontShowOnScreen);widget->resize(560,820);widget->show();QApplication::processEvents();
            check(widget->grab().save(root+"/"+name+".png"),"native panel screenshot "+name);widget->hide();
        };
        for(int state=0;state<3;++state){
            auto shown=zones;
            const QString name=state==0?"route":state==1?"no-return":"direct";
            if(state==1){shown[d.zone].exits[d.entry].destinationZoneId="none";shown[d.zone].exits[d.entry].destinationExitId=0;}
            if(state==2){shown[source].exits[selected].destinationZoneId="gz7g";shown[source].exits[selected].destinationExitId=1;}
            before.populate(shown,source,selected,false);before.revealArea(true);screenshot(&before,"r1-"+name);
            panel->populate(shown,source,selected,false);panel->revealArea(true);screenshot(panel,"r2-"+name);
            check(!panel->findChild<QWidget*>("travelRouteEditor")->isVisibleTo(panel),"route details collapsed in "+name);
            auto* routeToggle=panel->findChild<QToolButton*>("travelRouteToggle");
            check(routeToggle->isHidden()==(state==2),"route section only exists for routed destination: "+name);
            if(state==0){
                auto* list=panel->findChild<QListWidget*>("travelExitList");auto* target=panel->findChild<QComboBox*>("travelFinalDestination");
                check(list->horizontalScrollBarPolicy()==Qt::ScrollBarAlwaysOff && list->textElideMode()==Qt::ElideRight,"list has ellipsis and no horizontal scroll");
                check(list->item(selected)->text()==QString::fromUtf8("№2 → Мертвый Город, вход 4"),"list shows only destination title and entrance");
                check(target->currentText()==QString::fromUtf8("Мертвый Город (Zone9), вход 4"),"destination field is compact");
                check(target->toolTip().contains("gz5g_gz9g2") && target->toolTip().contains("gz9g") && list->item(selected)->toolTip().contains("gz5g_gz9g2"),"full route chain retained in both tooltips");
                check(panel->findChild<QPushButton*>("travelReverse")->isHidden() && panel->findChild<QPushButton*>("travelProblem")->isHidden(),"healthy link hides repair and reverse actions");
                const auto* area=panel->findChild<QCheckBox*>("travelPresent1");
                check(area->mapTo(panel,QPoint()).y()-target->mapTo(panel,QPoint()).y()<180,"default destination controls take fewer than six rows above area");
                for(auto* label:panel->findChildren<QLabel*>())if(label->isVisibleTo(panel))check(!label->text().contains('\n') && label->text().size()<85,"no visible explanatory paragraph");
            }
            if(state==1)check(!panel->findChild<QPushButton*>("travelReverse")->isHidden() && !panel->findChild<QPushButton*>("travelProblem")->isHidden(),"missing reverse exposes focused repair actions");
            for(int k=0;k<2;++k){
                auto* x=panel->findChild<QLineEdit*>(QString("travelRect_%1_0").arg(k));auto* y=panel->findChild<QLineEdit*>(QString("travelRect_%1_1").arg(k));
                check(x->geometry().y()==y->geometry().y() && x->width()<=105 && y->width()<=105,"coordinates compact in both tabs");
            }
        }
        panel->populate(zones,source,selected,false);panel->revealArea(false);screenshot(panel,"r2-deploy");
        panel->setParent(window.m_travelDock);window.m_travelDock->setWidget(panel);panel->show();panel->revealArea(true);window.refreshTravelEditor();
        view->setFocus();motion(view,Qt::Key_W,true);auto* coordinate=panel->findChild<QLineEdit*>("travelRect_1_0");const auto oldCoordinate=coordinate->text();
        coordinate->setFocus();motion(coordinate,Qt::Key_W,false);coordinate->setText(oldCoordinate);
        auto* add=panel->findChild<QPushButton*>("travelAdd0");add->setFocus();motion(add,Qt::Key_W,true);
        coordinate->setText(QString::number(oldCoordinate.toDouble()+1,'g',17));emit coordinate->editingFinished();
        check(doc->snapshot()!=initial,"compact coordinate field still edits area");window.on_actionUndo_triggered();check(doc->snapshot()==initial,"area edit Undo remains exact");
        int reminders=0;const auto reminderConnection=QObject::connect(window.statusBar(),&QStatusBar::messageChanged,&window,[&](const QString& message){if(message.contains(QString::fromUtf8("перезапустите игру")))++reminders;});
        auto* targets=panel->findChild<QComboBox*>("travelFinalDestination");int direct=-1;
        for(int i=0;i<targets->count();++i){const auto pair=targets->itemData(i).toList();if(pair.value(0).toString()=="gz7g" && pair.value(1).toInt()==1)direct=i;}
        activate(targets,direct);check(window.saveMapWithTravel(root+"/backups"),"compact destination uses normal Save");
        CTravelDocument reread;check(reread.open(archivePath,&error) && reread.table().zones()[source].exits[selected].destinationZoneId=="gz7g","direct selection persists and rereads");
        check(reminders==1,"restart reminder shown after first successful Save");window.on_actionUndo_triggered();check(doc->snapshot()==initial && window.saveMapWithTravel(root+"/backups"),"destination Undo and Save restore original");
        auto* toggle=panel->findChild<QToolButton*>("travelRouteToggle");toggle->click();
        auto* endpoint=panel->findChild<QComboBox*>("travelFarEntry");check(endpoint->isVisible(),"collapsed route expands to editable endpoint");
        endpoint->setCurrentIndex(endpoint->findData(2));panel->findChild<QPushButton*>("travelApplyFarEnd")->click();
        check(doc->table().zones()[d.route].exits[d.farEnd].destinationExitId==2 && window.saveMapWithTravel(root+"/backups"),"expanded route endpoint still saves");
        check(reread.open(archivePath,&error) && reread.table().zones()[d.route].exits[d.farEnd].destinationExitId==2,"route endpoint freshly rereads");
        window.on_actionUndo_triggered();check(doc->snapshot()==initial && window.saveMapWithTravel(root+"/backups"),"route endpoint Undo and Save restore original");
        check(reminders==1,"restart reminder is not repeated on later saves");
        check(read(archivePath)==original && read(maps+"/zone5_1.mob")==mobBefore && read(maps+"/zone5_1.mpr")==mprBefore,"full copied RES/MOB/MPR exact after round trips");
        window.m_undoStack->clear();window.m_travelDocuments.clear();window.m_travelDocument.clear();window.m_travelZone=-1;window.m_travelExit=-1;window.refreshTravelEditor();
        QObject::disconnect(reminderConnection);out<<"RESULT R2 "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
        }
#endif
#ifdef TRAVEL_BASELINE_KEYS
        motion(view,Qt::Key_W,false);
        out<<"RESULT baseline225 travel key swallowing reproduced\n";out.flush();return 0;
#endif
        for(int key:{Qt::Key_W,Qt::Key_A,Qt::Key_S,Qt::Key_D,Qt::Key_Up,Qt::Key_Down,Qt::Key_Left,Qt::Key_Right,Qt::Key_Q,Qt::Key_E})motion(view,key,true);
        if(sceneOnly){
            check(window.m_travelDocument && window.m_travelZone==source,"loaded scene selects actual profile catalog gz5g");
            window.m_travelExit=selected;window.refreshTravelEditor();auto* scenePanel=window.m_travelPanel;
            check(scenePanel->findChild<QComboBox*>("travelFinalDestination")->toolTip().contains(QString::fromUtf8("Мертвый Город / Zone9 (gz9g), вход №4")),"loaded scene panel resolves profile name and complete final destination");
            auto* control=scenePanel->findChild<QPushButton*>("travelAdd0");control->setFocus();motion(control,Qt::Key_W,true);
            QMouseEvent press(QEvent::MouseButtonPress,QPointF(5,5),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,QPointF(5,5),Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&release);
            check(view->focusWidget()==view,"loaded scene click returns keyboard focus");motion(view,Qt::Key_W,true);
            check(read(maps+"/zone5_1.mob")==mobBefore && read(maps+"/zone5_1.mpr")==mprBefore && read(archivePath)==original,"loaded scene camera/panel leave all map data unchanged");
            out<<"RESULT loaded scene "<<checks<<" PASS, 0 FAIL\n";out.flush();return 0;
        }
        window.m_travelDocument=doc;window.m_travelDocuments.insert(archivePath,doc);window.m_travelZone=source;window.m_travelExit=selected;window.refreshTravelEditor();
        auto* panel=window.m_travelPanel;panel->revealArea(true);
        check(panel->findChild<QComboBox*>("travelFinalDestination")->toolTip().contains("gz9g") && panel->findChild<QComboBox*>("travelFinalDestination")->toolTip().contains(QString::fromUtf8("вход №4")),"panel shows final Zone9 entry4 rather than route end1");
        panel->setParent(nullptr);panel->setAttribute(Qt::WA_DontShowOnScreen);panel->resize(700,1450);panel->show();QApplication::processEvents();
        check(panel->grab().save(root+"/panel-after.png"),"after PNG rendered from production226 panel");
        panel->hide();panel->setParent(window.m_travelDock);window.m_travelDock->setWidget(panel);panel->show();
        auto* raw=panel->findChild<QComboBox*>("travelDestination");check(!raw->isVisible(),"advanced raw fields collapsed");
        auto* edit=panel->findChild<QLineEdit*>("travelRect_1_0");const auto coordinate=edit->text();edit->setFocus();motion(edit,Qt::Key_W,false);edit->setText(coordinate);
        auto* button=panel->findChild<QPushButton*>("travelAdd0");button->setFocus();motion(button,Qt::Key_W,true);
        auto* search=panel->findChild<QComboBox*>("travelFinalDestination")->lineEdit();
        const auto searchText=search->text();search->setFocus();motion(search,Qt::Key_W,false);search->setText(searchText);
        button->setFocus();
        QMouseEvent click(QEvent::MouseButtonPress,QPointF(5,5),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);QApplication::sendEvent(view,&click);
        check(view->focusWidget()==view,"scene click returns keyboard focus to view");
        motion(view,Qt::Key_W,true);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(5,5),Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&release);
        // The same global routing is mode-independent; exercise actual toolbar entries.
        for(const char* name:{"groundPaintToolbarButton","behaviorModeButton","behaviorModeButton","scenarioToolbarButton"}) {
            auto* mode=window.findChild<QToolButton*>(name);check(mode,QString("mode toolbar exists: ")+name);
            mode->click();motion(view,Qt::Key_W,true);motion(mode,Qt::Key_Right,true);
        }
        toolbar->click();window.m_travelDocument=doc;window.m_travelDocuments.insert(archivePath,doc);window.m_travelZone=source;window.m_travelExit=selected;window.refreshTravelEditor();panel->revealArea(true);
        const auto baseline=doc->snapshot();
        auto* destinations=panel->findChild<QComboBox*>("travelFinalDestination");int other=-1;
        for(int i=0;i<destinations->count();++i)if(destinations->itemData(i).toList().value(0).toString()=="gz5g_gz6g")other=i;
        activate(destinations,other);
        check(doc->table().zones()[source].exits[selected].destinationZoneId=="gz5g_gz6g","friendly selection edits exact near end via production signal/Undo");
        SMapSaveFile candidate;check(doc->prepareSave(&candidate,&error),"destination change validates: "+error);
        check(window.saveMapWithTravel(root+"/backups"),"destination uses production atomic catalog Save");
        QDirIterator backups(root+"/backups",{"*.backup"},QDir::Files,QDirIterator::Subdirectories);check(backups.hasNext(),"atomic catalog Save creates backup");
        CTravelDocument reloaded;check(reloaded.open(archivePath,&error) && reloaded.snapshot()==doc->snapshot(),"destination saved and freshly reread");
        window.on_actionUndo_triggered();check(doc->snapshot()==baseline,"one Undo restores original destination bytes");
        check(window.saveMapWithTravel(root+"/backups"),"production Save restores original destination");
        destinations=panel->findChild<QComboBox*>("travelFinalDestination");int direct=-1;
        for(int i=0;i<destinations->count();++i){const auto pair=destinations->itemData(i).toList();if(pair.value(0).toString()=="gz7g" && pair.value(1).toInt()==1)direct=i;}
        activate(destinations,direct);check(window.saveMapWithTravel(root+"/backups"),"direct Zone7 entry1 saves through production pipeline");
        check(reloaded.open(archivePath,&error) && reloaded.table().zones()[source].exits[selected].destinationZoneId=="gz7g","direct destination freshly reread");
        window.on_actionUndo_triggered();check(doc->snapshot()==baseline && window.saveMapWithTravel(root+"/backups"),"direct destination Undo restores exact original catalog");
        window.m_travelExit=selected;window.refreshTravelEditor();panel->revealArea(true);
        const auto beforeFar=read(archivePath);const auto beforeZones=doc->table().zones();
        auto* endpoint=panel->findChild<QComboBox*>("travelFarEntry");activate(endpoint,endpoint->findData(2));
        panel->findChild<QPushButton*>("travelApplyFarEnd")->click();
        const auto changed=doc->table().zones();check(changed[d.route].exits[d.farEnd].destinationExitId==2,"endpoint-end control edits Zone9 entry4 to entry2");
        for(int i=0;i<changed.size();++i)if(i!=d.route)check(changed[i].rawBlock==beforeZones[i].rawBlock,"unrelated map.txt zone unchanged: "+changed[i].zoneId);
        check(window.saveMapWithTravel(root+"/backups"),"endpoint endpoint uses production validated atomic Save");
        CResArchiveDocument previous,after;check(previous.loadBytes(beforeFar)&&after.loadBytes(read(archivePath)),"before/after RES parse");
        int unchanged=0;for(const auto& member:previous.members())if(member.name.compare("map.txt",Qt::CaseInsensitive)!=0){check(after.memberPayload(member.name)==member.payload,"unrelated RES payload bytes unchanged: "+member.name);++unchanged;}
        check(after.members().size()==previous.members().size(),"RES member inventory unchanged");
        const auto oldMember=previous.members()[previous.uniqueMemberIndex("map.txt")];
        const auto newMember=after.members()[after.uniqueMemberIndex("map.txt")];
        // Entry4 -> entry2 is a same-length edit: even directory, offsets,
        // padding and every byte outside map.txt must remain identical.
        const auto farBytes=read(archivePath);
        check(oldMember.dataSize==newMember.dataSize && beforeFar.left(oldMember.dataOffset)==farBytes.left(newMember.dataOffset)
              && beforeFar.mid(oldMember.dataOffset+oldMember.dataSize)==farBytes.mid(newMember.dataOffset+newMember.dataSize),"binary comparison: entire RES outside map.txt is byte-identical");
        check(read(maps+"/zone5_1.mob")==mobBefore && read(maps+"/zone5_1.mpr")==mprBefore,"catalog-only destination edits preserve actual MOB and MPR byte-for-byte");
        check(reloaded.open(archivePath,&error)&&reloaded.table().zones()[d.route].exits[d.farEnd].destinationExitId==2,"endpoint endpoint freshly rereads as entry2");
        const auto beforeReverse=doc->snapshot();bool reverseDialog=false,confirmed=false;
        QTimer::singleShot(0,[&](){
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if(!dialog)return;reverseDialog=dialog->windowTitle().contains("gz9g");
            out<<"REVERSE_DIALOG "<<dialog->windowTitle()<<"\n";out.flush();
            auto* buttons=dialog->findChild<QDialogButtonBox*>();if(!buttons){dialog->reject();return;}
            if(!buttons->button(QDialogButtonBox::Ok)){out<<"REVERSE_DIALOG no OK\n";out.flush();dialog->reject();return;}
            QTimer::singleShot(0,[&](){auto* confirm=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(confirm){
                out<<"REVERSE_CONFIRM "<<confirm->windowTitle()<<" buttons="<<int(confirm->standardButtons())<<"\n";out.flush();
                if(auto* yes=confirm->button(QMessageBox::Yes)){confirmed=true;yes->click();}else confirm->reject();
            }});
            buttons->button(QDialogButtonBox::Ok)->click();
        });
        emit panel->reverseRequested();
        const auto repaired=doc->table().zones();const auto newTarget=travel::resolve(repaired,repaired[source].exits[selected].destinationZoneId,repaired[source].exits[selected].destinationExitId);
        check(reverseDialog && confirmed && travel::reciprocal(repaired,source,repaired[source].exits[selected],newTarget),"create reverse edits final Zone9 entry2 via route, not an edge rectangle");
        window.on_actionUndo_triggered();check(doc->snapshot()==beforeReverse,"reverse repair is one exact Undo");
        auto invalid=doc->table().zones()[source];invalid.exits[selected].destinationExitId=999;
        check(doc->replaceZone(source,invalid,&error) && !doc->prepareSave(&candidate,&error),"2xx validation still rejects nonexistent target entry");doc->discard();
        window.m_undoStack->clear();window.m_travelDocuments.clear();window.m_travelDocument.clear();window.m_travelZone=-1;window.m_travelExit=-1;window.refreshTravelEditor();
        out<<"RESULT "<<checks<<" PASS, 0 FAIL; unchanged RES members "<<unchanged<<"\n";out.flush();return 0;
    }catch(const QString& e){out<<"FAIL "<<e<<"\nRESULT "<<checks<<" PASS, 1 FAIL\n";out.flush();return 1;}
}

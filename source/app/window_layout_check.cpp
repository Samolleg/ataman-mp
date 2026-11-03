#include "window_layout_check.h"
#include "main_window.h"
#include "resource_setup_dialog.h"
#include "model_gallery.h"
#include "createobjectform.h"
#include "window_geometry.h"
#include "utils.h"
#include "view.h"
#include "mob.h"
#include <QStatusBar>
#include <QFile>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDockWidget>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMouseEvent>
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QSplitter>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include "layout_components/dynamiccombobox.h"

namespace {
class HiddenCheckWindows : public QObject
{
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};

class DynamicMinimumDialog : public QDialog
{
    void showEvent(QShowEvent* event) override
    {
        setMinimumSize(400, 300);
        QDialog::showEvent(event);
    }
};
}

int runWindowLayoutCheck()
{
    QTemporaryDir temporary;
    if(!temporary.isValid()) return 2;
    const QString fixturePath=qEnvironmentVariable("EI_WINDOW_LAYOUT_FIXTURE_PATH");
    const QString fixturePhase=qEnvironmentVariable("EI_WINDOW_LAYOUT_FIXTURE_PHASE");
    const QString settingsPath=fixturePath.isEmpty() ? temporary.path() : fixturePath;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsPath);
    QDir::setCurrent(settingsPath);
    qApp->setProperty("workspaceLoadSmoke", true);
    qApp->setProperty("windowLayoutCheck", true);
    qApp->setQuitOnLastWindowClosed(false);
    HiddenCheckWindows hideWindows;
    qApp->installEventFilter(&hideWindows);
    int failures = 0;
    auto check = [&failures](const char* name, bool passed) {
        QTextStream(stdout) << (passed ? "PASS " : "FAIL ") << name << "\n";
        if(!passed) ++failures;
    };
    auto pump = []() { for(int i=0; i<6; ++i) QApplication::processEvents(); };
    const auto closeWindow = [&pump](MainWindow& window) {
        window.close();
        QElapsedTimer deadline;
        deadline.start();
        while(window.isVisible() && deadline.elapsed() < 20000) {
            QEventLoop events;
            QTimer::singleShot(10, &events, &QEventLoop::quit);
            events.exec();
        }
        pump();
        return !window.isVisible();
    };
    auto hidden = [](QWidget& window) { window.setAttribute(Qt::WA_DontShowOnScreen); };
    if(!fixturePhase.isEmpty())
    {
        const int mode=fixturePhase.right(1).toInt();
        const bool saving=fixturePhase.startsWith("save");
        QTimer setupDriver;
        QObject::connect(&setupDriver,&QTimer::timeout, [&check]() {
            for(QWidget* window:QApplication::topLevelWidgets())
                if(auto* setup=qobject_cast<CResourceSetupDialog*>(window)) setup->reject();
                else if(auto* box=qobject_cast<QMessageBox*>(window))
                    if(box->isVisible()) {QTextStream(stdout)<<"Unexpected dialog: "<<box->text()<<"\n";check("no unexpected modal during restoration",false);box->reject();}
        });
        setupDriver.start(1);
        MainWindow window; hidden(window);window.show();pump();
        auto* travel=window.findChild<QDockWidget*>("TravelEditorDock");
        auto* scenario=window.findChild<QDockWidget*>("mapScenarioDock");
        auto* form=window.findChild<CCreateObjectForm*>();
        if(!travel || !scenario || !form) return 2;
        if(mode >= 3)
        {
            auto* maps=window.findChild<QDockWidget*>("mapsAndCollectionsDock");
            auto* outliner=window.findChild<QDockWidget*>("objectOutlinerDock");
            auto* properties=window.findChild<QDockWidget*>("objectPropertiesDock");
            auto* loaded=window.findChild<QDockWidget*>("loadedMapDock");
            auto* validation=window.findChild<QDockWidget*>("mobFamilyValidationDock");
            auto* log=window.findChild<QDockWidget*>("messageLogDock");
            if(!maps || !outliner || !properties || !loaded || !validation || !log) return 2;
            // Independent processes exercise the owner's two vertically split
            // columns, rather than only floating travel/scenario windows.
            const QList<QDockWidget*> docks{outliner,maps,mode==6 ? log : properties,loaded};
            QSettings settings;
            if(saving)
            {
                for(auto* dock:window.findChildren<QDockWidget*>()) dock->hide();
                const auto left=mode==4 ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
                const auto right=mode==4 ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
                window.addDockWidget(left,outliner);
                window.splitDockWidget(outliner,maps,Qt::Vertical);
                window.addDockWidget(right,properties);
                window.splitDockWidget(properties,loaded,Qt::Vertical);
                window.tabifyDockWidget(properties,validation);
                window.tabifyDockWidget(properties,log);
                for(auto* dock:{outliner,maps,properties,loaded}) dock->show();
                validation->show();log->show();properties->raise();
                if(mode==6) log->raise();
                check("fixture properties has both diagnostic tabs",window.tabifiedDockWidgets(properties).contains(validation)&&window.tabifiedDockWidgets(properties).contains(log));
                window.showNormal();
                window.resize(1700,900);
                if(mode!=4) window.showMaximized();
                pump();
                window.resizeDocks({outliner,properties},{520,410},Qt::Horizontal);
                window.resizeDocks({outliner,maps},{320,590},Qt::Vertical);
                window.resizeDocks({properties,loaded},{590,240},Qt::Vertical);
                pump();
                if(mode==5) {
                    QByteArray bytes;
                    util::CMobParser writer(bytes,true);
                    writer.startSection("OBJECT_DB_FILE");
                    writer.startSection("SC_OBJECT_DB_FILE");writer.endSection();
                    writer.startSection("OBJECT_SECTION");writer.endSection();writer.endSection();
                    QFile fixture(QDir(settingsPath).filePath("layout-fixture.mob"));
                    const bool written=fixture.open(QIODevice::WriteOnly)&&fixture.write(bytes)==bytes.size();fixture.close();
                    auto* mob=new CMob;
                    const bool decoded=written&&mob->deserialize(bytes);
                    mob->setFileName(QFileInfo(fixture.fileName()));mob->adoptCommittedBytes(bytes);
                    window.view()->m_aMob.append(mob);window.view()->m_activeMob=mob;
                    window.view()->collectObjectTreeData();emit window.view()->mobStateChanged();pump();
                    check("synthetic MOB displayed by production panels",decoded);
                }
                for(auto* dock:docks) {
                    settings.setValue("expectedDock/"+dock->objectName()+"/size",dock->size());
                    settings.setValue("expectedDock/"+dock->objectName()+"/area",int(window.dockWidgetArea(dock)));
                }
                check("fixture has different left/right widths",qAbs(outliner->width()-loaded->width())>60);
            }
            else
            {
                check("main window maximized state survives restart",window.isMaximized()==(mode!=4));
                auto checkSizes=[&]() { for(auto* dock:docks) {
                    const QSize expected=settings.value("expectedDock/"+dock->objectName()+"/size").toSize();
                    QTextStream(stdout)<<dock->objectName()<<" expected "<<expected.width()<<"x"<<expected.height()<<" actual "<<dock->width()<<"x"<<dock->height()<<"\n";
                    check(qPrintable(dock->objectName()+" area restored"),int(window.dockWidgetArea(dock))==settings.value("expectedDock/"+dock->objectName()+"/area").toInt());
                    check(qPrintable(dock->objectName()+" width restored"),qAbs(dock->width()-expected.width())<=2);
                    check(qPrintable(dock->objectName()+" height restored"),qAbs(dock->height()-expected.height())<=2);
                }};
                checkSizes();
                if(mode==5) {
                    QFile fixture(QDir(settingsPath).filePath("layout-fixture.mob"));
                    const bool read=fixture.open(QIODevice::ReadOnly);const auto bytes=fixture.readAll();
                    auto* mob=new CMob;const bool decoded=read&&mob->deserialize(bytes);
                    mob->setFileName(QFileInfo(fixture.fileName()));mob->adoptCommittedBytes(bytes);
                    window.view()->m_aMob.append(mob);window.view()->m_activeMob=mob;
                    window.view()->collectObjectTreeData();emit window.view()->mobStateChanged();pump();
                    check("synthetic MOB reopens after layout restoration",decoded);
                    checkSizes();
                }
                check("properties retain validation/log tabs",window.tabifiedDockWidgets(properties).contains(validation)&&window.tabifiedDockWidgets(properties).contains(log));
            }
            check("split-column production window closes normally",closeWindow(window));
            return failures ? 1 : 0;
        }
        if(saving)
        {
            if(mode==2) {window.tabifyDockWidget(travel,scenario);travel->show();scenario->show();}
            else if(mode==0) {travel->setFloating(true);scenario->setFloating(true);travel->resize(780,600);scenario->resize(700,530);travel->show();scenario->show();}
            else {travel->hide();scenario->hide();}
            pump();
            QSettings settings;settings.setValue("expectedTravelSize",travel->size());settings.setValue("expectedScenarioSize",scenario->size());
            if(mode==0)
            {
                form->show();pump();form->resize(1500,800);pump();
                auto* table=form->findChild<QTableWidget*>("tableParameters");
                auto* divider=form->findChild<QSplitter*>("creationSplitter");
                auto* preview=form->findChild<QWidget*>("openGLWidget");
                auto* type=form->findChild<QComboBox*>("comboObjectType");
                auto* category=form->findChild<QComboBox*>("databaseCategory");
                if(category)category->setCurrentIndex(3);pump();
                if(auto* dynamic=qobject_cast<CDynamicComboBox*>(type)){dynamic->showPopup();dynamic->setCurrentIndex(dynamic->findText(CCreateObjectForm::tr("World object")));dynamic->hidePopup();}pump();
                check("create properties receive usable share of wide dialog",table&&table->isVisible()&&table->rowCount()>0&&table->width()>form->width()*0.4);
                bool moved=false;
                if(divider&&table&&preview) {
                    const int propertiesBefore=table->width(),previewBefore=preview->width();
                    auto* handle=divider->handle(1);const auto point=handle->rect().center();const auto global=handle->mapToGlobal(point);
                    QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(handle,&press);
                    QMouseEvent move(QEvent::MouseMove,point+QPoint(140,0),global+QPoint(140,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(handle,&move);pump();
                    QMouseEvent release(QEvent::MouseButtonRelease,point+QPoint(140,0),global+QPoint(140,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(handle,&release);pump();
                    moved=table->width()>propertiesBefore+80&&preview->width()<previewBefore-80;
                }
                check("create inner divider mouse drag resizes both panes",moved);
                if(table) {
                    auto* header=table->horizontalHeader();
                    auto* viewport=header->viewport();const auto point=QPoint(header->sectionSize(0)-1,header->height()/2);const auto target=QPoint(185,point.y());
                    QMouseEvent press(QEvent::MouseButtonPress,point,viewport->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&press);
                    QMouseEvent move(QEvent::MouseMove,target,viewport->mapToGlobal(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(viewport,&move);
                    QMouseEvent release(QEvent::MouseButtonRelease,target,viewport->mapToGlobal(target),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(viewport,&release);pump();
                    check("create parameter boundary responds to mouse drag",qAbs(table->columnWidth(0)-185)<=2);
                    settings.setValue("expectedCreateColumn",table->columnWidth(0));
                    table->horizontalHeader()->resizeSection(0,600);pump();
                    form->resize(720,740);pump();
                    check("create narrow dialog keeps value column and no horizontal scroll",table->columnWidth(1)>=140&&table->horizontalScrollBar()->maximum()==0);
                    form->resize(1500,800);pump();
                    check("create expanding dialog restores preferred parameter width",table->columnWidth(0)==600);
                    table->horizontalHeader()->resizeSection(0,settings.value("expectedCreateColumn").toInt());pump();
                }
                form->resize(1050,740);form->move(60,85);pump();
                check("create value editor remains usable without horizontal scroll",table&&table->horizontalScrollBar()->maximum()==0&&table->columnWidth(1)>=140);
                const QString screenshot=qEnvironmentVariable("EI_CREATE_LAYOUT_CHECK_SCREENSHOT");
                if(!screenshot.isEmpty())form->grab().save(screenshot);
                if(divider)settings.setValue("expectedCreateSplitSizes",QVariantList{divider->sizes().value(0),divider->sizes().value(1)});
                settings.setValue("expectedFormSize",form->size());settings.setValue("expectedFormPosition",form->pos());form->close();check("production create form close hides",form->isHidden());
                form->show();pump();
                check("create parameter column survives reused form refresh",table&&table->columnWidth(0)==settings.value("expectedCreateColumn").toInt());
                form->close();
            }
        }
        else
        {
            QSettings settings;
            if(mode==0)
            {
                check("late production docks restore floating geometry",travel->isFloating() && scenario->isFloating() && travel->size()==settings.value("expectedTravelSize").toSize() && scenario->size()==settings.value("expectedScenarioSize").toSize());
                form->show();pump();check("production create form restores geometry",form->size()==settings.value("expectedFormSize").toSize() && form->pos()==settings.value("expectedFormPosition").toPoint());
                auto* divider=form->findChild<QSplitter*>("creationSplitter");
                auto* table=form->findChild<QTableWidget*>("tableParameters");
                const auto expected= settings.value("expectedCreateSplitSizes").toList();
                check("create divider survives independent process reopen",divider&&expected.size()==2&&divider->sizes()==QList<int>{expected[0].toInt(),expected[1].toInt()});
                auto* type=form->findChild<QComboBox*>("comboObjectType");
                auto* category=form->findChild<QComboBox*>("databaseCategory");
                if(category)category->setCurrentIndex(3);pump();
                if(auto* dynamic=qobject_cast<CDynamicComboBox*>(type)){dynamic->showPopup();dynamic->setCurrentIndex(dynamic->findText(CCreateObjectForm::tr("World object")));dynamic->hidePopup();}pump();
                check("create parameter column survives independent process and object refresh",table&&table->columnWidth(0)==settings.value("expectedCreateColumn").toInt());form->close();
            }
            if(mode==1) check("late production docks retain hidden state",travel->isHidden() && scenario->isHidden());
            if(mode==2) check("late production docks retain tab topology",window.tabifiedDockWidgets(travel).contains(scenario));
            check("landscape does not automatically resume editing",window.findChild<QDockWidget*>("landscapeToolsDock")->isHidden());
        }
        auto* magnet=window.findChild<QToolButton*>("modelMagnetButton");
        auto* sourceChoice=window.findChild<QComboBox*>("modelConnectionSource");
        if(!magnet || !sourceChoice) return 2;
        magnet->setChecked(true);pump();
        auto* feedback=window.findChild<QLabel*>("modelConnectionFeedback");
        check("idle magnet visibly explains selection and G",feedback && feedback->isVisible() && feedback->text().contains("G"));
        check("idle source choice cannot suggest ready anchors",!sourceChoice->isEnabled());
        check("next pair is unavailable before a match",!window.findChild<QToolButton*>("nextModelConnection")->isEnabled());
        auto* movable=window.findChild<QDockWidget*>("objectOutlinerDock");
        if(!movable) return 2;
        movable->show();pump();
        const auto initial=movable->mapToGlobal(QPoint(40,8));
        const auto destination=initial+QPoint(180,180);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(40,8),QPointF(initial),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(movable,&press);
        QMouseEvent move(QEvent::MouseMove,QPointF(220,188),QPointF(destination),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(movable,&move);pump();
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(220,188),QPointF(destination),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(movable,&release);pump();
        check("title drag detaches restored production dock",movable->isFloating());
        check("production window closes normally",closeWindow(window));
        return failures ? 1 : 0;
    }
    const QSize wanted(800,600);
    QPoint position;
    for(int action=0; action<4; ++action)
    {
        QDialog original; hidden(original); ei::rememberWindowGeometry(&original, "checkDialog");
        original.show(); pump(); original.resize(wanted); original.move(50,80); pump();
        position=original.pos();
        if(action==0) original.accept(); else if(action==1) original.reject();
        else if(action==2) original.close();
        else {QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&original,&escape);}
        QDialog restored; hidden(restored); restored.resize(300,200);
        ei::rememberWindowGeometry(&restored, "checkDialog"); restored.show(); pump();
        check("dialog accept/reject/close/Esc restores geometry", original.isHidden() && restored.size()==wanted && restored.pos()==position);
        restored.hide(); restored.resize(320,210); restored.show(); pump();
        check("reused hidden dialog ignores constructor/default resize", restored.size()==wanted);
        restored.hide();
    }
    {
        QDialog neverShown; ei::rememberWindowGeometry(&neverShown, "checkDialog"); neverShown.resize(200,100);
    }
    {
        DynamicMinimumDialog dynamic;
        hidden(dynamic);
        dynamic.setMinimumSize(400, 760);
        ei::rememberWindowGeometry(&dynamic, "checkDialog");
        dynamic.show();
        pump();
        check("showEvent minimum change preserves saved geometry",
              dynamic.size() == wanted && dynamic.pos() == position);
        dynamic.hide();
    }
    QDialog other; hidden(other); other.resize(410,270); ei::rememberWindowGeometry(&other, "otherDialog");
    other.show(); pump(); check("unrelated dialog uses independent key", other.size()==QSize(410,270)); other.hide();
    check("removed monitor returns entire window to screen", ei::accessibleWindowGeometry(
        QRect(2400,30,1200,900), {QRect(0,0,1000,700)})==QRect(0,0,1000,700));
    check("negative-coordinate monitor retains geometry", ei::accessibleWindowGeometry(
        QRect(-1000,80,800,500), {QRect(0,0,1920,1080),QRect(-1280,0,1280,1024)})==QRect(-1000,80,800,500));
    // Actual production gallery: no model resources or game files are involved.
    ei::TechnicalModelCatalog catalog;
    ei::TechnicalTextureCatalog scan;
    {
        CModelGallery gallery({}, &catalog, "synthetic", {}, &scan, {}); hidden(gallery);
        gallery.show(); pump(); gallery.resize(980,700); gallery.move(70,90); pump(); position=gallery.pos(); gallery.reject();
    }
    {
        CModelGallery gallery({}, &catalog, "synthetic", {}, &scan, {}); hidden(gallery);
        gallery.show(); pump(); check("production gallery remembers position and size",gallery.size()==QSize(980,700) && gallery.pos()==position); gallery.reject();
    }
    for(int mode=0; mode<7; ++mode)
    {
        QSettings().remove("ui/mainWindowState");
        QStringList phases{QString("save%1").arg(mode),QString("restore%1").arg(mode)};
        if(mode>=3) phases.append(QString("restore%1").arg(mode));
        if(mode==3) phases.append(QStringLiteral("restore7"));
        for(const QString& phase : phases)
        {
            if(phase==QStringLiteral("restore7")) {
                // Migration from an existing Qt state must ignore obsolete,
                // stale side extents without resetting the user's topology.
                QSettings settings;settings.remove("ui/mainDockWidthsV2");
                settings.setValue("ui/mainDockExtentsV1/properties/width",119);
                settings.setValue("ui/mainDockExtentsV1/properties/height",145);
                settings.setValue("ui/mainDockExtentsV1/messageLog/width",2976);settings.sync();
            }
            QProcess child;
            auto environment=QProcessEnvironment::systemEnvironment();
            environment.insert("EI_WINDOW_LAYOUT_FIXTURE_PATH",temporary.path());
            environment.insert("EI_WINDOW_LAYOUT_FIXTURE_PHASE",phase);
            child.setProcessEnvironment(environment);
            child.start(QCoreApplication::applicationFilePath(),{"--window-layout-check"});
            const bool started=child.waitForStarted(5000);
            const bool finished=started && child.waitForFinished(20000);
            if(!finished) {child.kill();child.waitForFinished(5000);}
            QTextStream(stdout)<<child.readAllStandardOutput();
            check(qPrintable("independent editor process "+phase),finished && child.exitStatus()==QProcess::NormalExit && child.exitCode()==0);
        }
    }
    QTextStream(stdout)<<"Window layout failures: "<<failures<<"\n";
    return failures ? 1 : 0;
}

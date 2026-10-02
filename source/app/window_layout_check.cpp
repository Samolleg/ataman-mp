#include "window_layout_check.h"
#include "main_window.h"
#include "resource_setup_dialog.h"
#include "model_gallery.h"
#include "createobjectform.h"
#include "window_geometry.h"
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
        if(saving)
        {
            if(mode==2) {window.tabifyDockWidget(travel,scenario);travel->show();scenario->show();}
            else if(mode==0) {travel->setFloating(true);scenario->setFloating(true);travel->resize(780,600);scenario->resize(700,530);travel->show();scenario->show();}
            else {travel->hide();scenario->hide();}
            pump();
            QSettings settings;settings.setValue("expectedTravelSize",travel->size());settings.setValue("expectedScenarioSize",scenario->size());
            if(mode==0)
            {
                form->show();pump();form->resize(1050,740);form->move(60,85);pump();
                settings.setValue("expectedFormSize",form->size());settings.setValue("expectedFormPosition",form->pos());form->close();check("production create form close hides",form->isHidden());
            }
        }
        else
        {
            QSettings settings;
            if(mode==0)
            {
                check("late production docks restore floating geometry",travel->isFloating() && scenario->isFloating() && travel->size()==settings.value("expectedTravelSize").toSize() && scenario->size()==settings.value("expectedScenarioSize").toSize());
                form->show();pump();check("production create form restores geometry",form->size()==settings.value("expectedFormSize").toSize() && form->pos()==settings.value("expectedFormPosition").toPoint());form->close();
            }
            if(mode==1) check("late production docks retain hidden state",travel->isHidden() && scenario->isHidden());
            if(mode==2) check("late production docks retain tab topology",window.tabifiedDockWidgets(travel).contains(scenario));
            check("landscape does not automatically resume editing",window.findChild<QDockWidget*>("landscapeToolsDock")->isHidden());
        }
        check("production window closes normally",window.close());pump();
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
    for(int mode=0; mode<3; ++mode)
    {
        QSettings().remove("ui/mainWindowState");
        for(const QString& phase : {QString("save%1").arg(mode),QString("restore%1").arg(mode)})
        {
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

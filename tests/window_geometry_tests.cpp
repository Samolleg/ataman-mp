#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include "window_geometry.h"

class WindowGeometryTests : public QObject
{
    Q_OBJECT
private slots:
    void independentProcesses()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        for(const QString& action : {QString("save"), QString("restore"), QString("maximize"), QString("restoreMaximized")})
        {
            QProcess child;
            child.start(QCoreApplication::applicationFilePath(), {"--fixture", action, data.path()});
            QVERIFY(child.waitForStarted());
            QVERIFY2(child.waitForFinished(15000), qPrintable(child.errorString()));
            QCOMPARE(child.exitStatus(), QProcess::NormalExit);
            QCOMPARE(child.exitCode(), 0);
        }
    }
    void missingAndNegativeMonitors()
    {
        const QVector<QRect> screens{QRect(0,0,1920,1040),QRect(-1280,0,1280,984)};
        QCOMPARE(ei::accessibleWindowGeometry(QRect(-1200,30,800,600),screens),QRect(-1200,30,800,600));
        QCOMPARE(ei::accessibleWindowGeometry(QRect(2400,200,900,700),{screens[0]}),QRect(1020,200,900,700));
        QCOMPARE(ei::accessibleWindowGeometry(QRect(120,50,2500,1600),{screens[0]}),screens[0]);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc,argv);
    if(app.arguments().value(1)=="--fixture")
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName("MapperSyntheticCheck");
        QCoreApplication::setApplicationName("Geometry");
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,app.arguments().value(3));
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,app.arguments().value(3));
        QDialog dialog;dialog.setAttribute(Qt::WA_DontShowOnScreen);dialog.resize(300,200);
        ei::rememberWindowGeometry(&dialog,"workingDialog");dialog.show();app.processEvents();
        const QString action=app.arguments()[2];
        if(action=="save")
        {
            dialog.resize(700,500);dialog.move(100,110);app.processEvents();
            QSettings().setValue("expectedPosition",dialog.pos());dialog.reject();return 0;
        }
        if(action=="maximize") {dialog.showMaximized();app.processEvents();dialog.reject();return 0;}
        if(action=="restoreMaximized") return dialog.isMaximized() ? 0 : 1;
        const bool passed=dialog.size()==QSize(700,500) && dialog.pos()==QSettings().value("expectedPosition").toPoint();
        dialog.reject();return passed ? 0 : 1;
    }
    WindowGeometryTests checks;
    return QTest::qExec(&checks,argc,argv);
}
#include "window_geometry_tests.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtWidgets>
#include <windows.h>
#include "windows_ui_scaling.h"
#include "localization.h"
#include "settings.h"

namespace {
class HiddenWindows : public QObject
{
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if(event->type() == QEvent::Polish || event->type() == QEvent::Show)
            if(auto* widget = qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
int visibleWindows = 0;
BOOL CALLBACK inspectWindow(HWND window, LPARAM)
{
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if(pid == GetCurrentProcessId() && IsWindowVisible(window)) ++visibleWindows;
    return TRUE;
}
}

int runAssemblyCatalogCheck()
{
    int failures = 0, checks = 0;
    const auto check = [&](bool condition, const char* text) {
        ++checks;
        QTextStream(stdout) << (condition ? "PASS " : "FAIL ") << text << "\n";
        if(!condition) ++failures;
    };
    const QString root = qEnvironmentVariable("EI_SCALE_CHECK_ROOT");
    if(root.isEmpty()) return 2;
    qApp->setQuitOnLastWindowClosed(false);
    HiddenWindows hidden;
    qApp->installEventFilter(&hidden);
    QTimer dialogs;
    QObject::connect(&dialogs, &QTimer::timeout, [] {
        for(auto* widget : QApplication::topLevelWidgets())
            if(auto* message = qobject_cast<QMessageBox*>(widget)) message->accept();
    });
    dialogs.start(1);
    ei::installApplicationTranslator(*qApp, "ru");

    QVector<ei::SUiMonitorDpi> monitors;
    ei::SUiMonitorDpi primary; primary.name="A"; primary.dpi=168; primary.primary=true;
    ei::SUiMonitorDpi secondary; secondary.name="B"; secondary.dpi=144;
    monitors << primary << secondary;
    const auto automatic = ei::uiScaleEnvironment(monitors, "auto");
    check(automatic.globalFactor == "1" && automatic.screenFactors == "A=1.75;B=1.5",
          "fractional automatic factors for two different monitors");
    for(const QString mode : {"100", "125", "150", "175", "200"})
    {
        const auto manual = ei::uiScaleEnvironment(monitors, mode);
        check(manual.screenFactors == "A=1.75;B=1.75", "manual override cancels native font DPI");
        check(qAbs(manual.globalFactor.toDouble()*1.75-mode.toDouble()/100)<0.00001,
              "manual effective DPR remains fractional");
    }
    monitors[0].dpi=144;
    const auto changedSinceLogon=ei::uiScaleEnvironment(monitors,"auto");
    check(changedSinceLogon.screenFactors=="A=1.5;B=1.5", "current monitor 150 percent wins over previous 175 percent");

    QJsonObject result;
    result["startupMode"]=qApp->property("uiScaleStartupMode").toString();
    result["startupSource"]=qApp->property("uiScaleStartupSource").toString();
    result["monitors"]=qApp->property("uiScaleMonitors").toJsonArray();
    result["globalFactor"]=qEnvironmentVariable("QT_SCALE_FACTOR");
    result["screenFactors"]=qEnvironmentVariable("QT_SCREEN_SCALE_FACTORS");
    result["fontSource"]=qApp->property("uiScaleFontSource").toString();
    result["systemFontPixels"]=qApp->property("uiScaleSystemFontPixels").toInt();
    CSettings settings;
    settings.setAttribute(Qt::WA_DontShowOnScreen);
    settings.onShow();
    QApplication::processEvents();
    auto* mode=settings.findChild<QComboBox*>("uiScaleMode");
    check(mode && mode->count()==6 && mode->itemData(0).toString()=="auto", "settings expose Auto and all five fixed scales");
    if(!mode) return 2;
    const qreal dpr=settings.devicePixelRatioF();
    const QFont font=QApplication::font();
    const int height=QFontMetrics(font).height();
    result["dpr"]=dpr;
    result["fontHeight"]=height;
    result["logicalDpi"]=settings.logicalDpiX();
    result["fontPointSize"]=font.pointSizeF();
    result["fontPixelSize"]=font.pixelSize();
    result["displayedMode"]=mode->currentData().toString();
    mode->setCurrentIndex(mode->findData("200"));
    check(QMetaObject::invokeMethod(&settings,"on_buttonApply_clicked",Qt::DirectConnection), "apply through normal settings slot");
    check(ei::persistedUiScaleMode()=="200", "mode saved in ordinary options JSON");
    check(settings.devicePixelRatioF()==dpr && QApplication::font()==font && QFontMetrics(QApplication::font()).height()==height,
          "scale applies only after restart and does not multiply current fonts");
    check(dynamic_cast<COptInt*>(settings.opt(eOptSetGeneral,"uiScalePercent"))->value()==150,
          "legacy font setting retained for rollback");
    settings.close();
    EnumWindows(inspectWindow,0);
    check(visibleWindows==0,"zero visible native windows");
    result["checks"]=checks; result["failures"]=failures;
    QFile output(root+"/startup.json");output.open(QIODevice::WriteOnly);output.write(QJsonDocument(result).toJson());
    return failures ? 1 : 0;
}

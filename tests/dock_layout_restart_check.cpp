// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtWidgets>
#include <QtTest>
#include <windows.h>
#include "main_window.h"
#include "view.h"
#include "resource_setup_dialog.h"

namespace {
int visibleWindows = 0;

BOOL CALLBACK inspectWindow(HWND window, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if(pid == GetCurrentProcessId() && IsWindowVisible(window))
        ++visibleWindows;
    return TRUE;
}

class HiddenWindows : public QObject
{
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if(event->type() == QEvent::Polish || event->type() == QEvent::Show)
            if(auto* widget = qobject_cast<QWidget*>(object))
                if(widget->isWindow())
                    widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};

void pump(int milliseconds = 400)
{
    QTest::qWait(milliseconds);
    EnumWindows(inspectWindow, 0);
}

bool waitUntil(const std::function<bool()>& ready)
{
    QElapsedTimer deadline;
    deadline.start();
    while(!ready() && deadline.elapsed() < 30000)
        pump(10);
    return ready();
}
}

// Replace only this CLI check entry. MainWindow, its lifecycle and the rest of
// the application use production Release objects. Each phase is a new process.
int runAssemblyCatalogCheck()
{
    QTextStream output(stdout);
    int failures = 0;
    const auto check = [&](bool passed, const QString& description) {
        output << (passed ? "PASS " : "FAIL ") << description << "\n";
        output.flush();
        if(!passed) ++failures;
    };
    const QString root = qEnvironmentVariable("EI_DOCK_LAYOUT_ROOT");
    const QString phase = qEnvironmentVariable("EI_DOCK_LAYOUT_PHASE");
    if(root.isEmpty() || phase.isEmpty()
       || qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"))
        return 2;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root + "/settings");
    qApp->setQuitOnLastWindowClosed(false);
    qApp->setProperty("workspaceLoadSmoke", true);
    HiddenWindows hiddenWindows;
    qApp->installEventFilter(&hiddenWindows);
    QTimer guard;
    QObject::connect(&guard, &QTimer::timeout, [&]() {
        EnumWindows(inspectWindow, 0);
        for(QWidget* widget : QApplication::topLevelWidgets()) {
            if(auto* setup = qobject_cast<CResourceSetupDialog*>(widget))
                setup->reject();
            else if(auto* box = qobject_cast<QMessageBox*>(widget)) {
                if(box->isVisible()) {
                    check(false, "unexpected modal " + box->text());
                    box->reject();
                }
            }
        }
    });
    guard.start(10);

    MainWindow window;
    window.configureAutosaveTimer(false, 300000);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    pump();
    QSettings settings;
    output << "profile=" << settings.fileName() << " phase=" << phase << "\n";
    const auto dock = [&](const char* name) {
        return window.findChild<QDockWidget*>(name);
    };
    auto* maps = dock("mapsAndCollectionsDock");
    auto* loaded = dock("loadedMapDock");
    auto* properties = dock("objectPropertiesDock");
    auto* outliner = dock("objectOutlinerDock");
    auto* log = dock("messageLogDock");
    auto* validation = dock("mobFamilyValidationDock");
    auto* travel = dock("TravelEditorDock");
    auto* scenario = dock("mapScenarioDock");
    auto* landscape = dock("landscapeToolsDock");
    const QList<QDockWidget*> trackedDocks {
        maps, loaded, properties, outliner, log, validation, travel, scenario, landscape
    };
    for(QDockWidget* current : trackedDocks)
        if(!current) return 2;

    const auto capture = [&]() {
        QJsonObject state;
        state["main"] = QJsonArray {window.x(), window.y(), window.width(), window.height()};
        state["mainMaximized"] = window.isMaximized();
        for(QDockWidget* current : trackedDocks) {
            QStringList tabs;
            for(QDockWidget* tab : window.tabifiedDockWidgets(current))
                tabs.append(tab->objectName());
            tabs.sort();
            const bool active = !current->isHidden()
                && (current->isFloating() || window.rect().intersects(current->geometry()));
            QJsonObject value {
                {"floating", current->isFloating()},
                {"hidden", current->isHidden()},
                {"area", int(window.dockWidgetArea(current))},
                {"tabs", QJsonArray::fromStringList(tabs)},
                {"active", active}
            };
            // Qt puts inactive tab contents at negative coordinates. Their
            // obsolete rectangles do not describe a visible panel's size.
            if(active)
                value["geometry"] = QJsonArray {
                    current->x(), current->y(), current->width(), current->height()
                };
            state[current->objectName()] = value;
        }
        return state;
    };
    const auto compare = [&](const QJsonObject& expected) {
        const QJsonObject actual = capture();
        for(auto entry = expected.begin(); entry != expected.end(); ++entry) {
            const QString name = entry.key();
            if(name == "main" || name == "mainMaximized") {
                check(actual[name] == entry.value(), name + " restored");
                continue;
            }
            const QJsonObject savedDock = entry.value().toObject();
            const QJsonObject actualDock = actual[name].toObject();
            for(const char* key : {"floating", "hidden", "area", "tabs", "active"})
                check(actualDock[key] == savedDock[key], name + " " + key);
            if(savedDock.contains("geometry")) {
                const QJsonArray savedRect = savedDock["geometry"].toArray();
                const QJsonArray actualRect = actualDock["geometry"].toArray();
                bool sameGeometry = actualRect.size() == 4;
                for(int index = 0; index < actualRect.size() && index < savedRect.size(); ++index)
                    sameGeometry &= qAbs(actualRect[index].toInt() - savedRect[index].toInt()) <= 2;
                const auto describe = [](const QJsonArray& value) {
                    return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
                };
                check(sameGeometry, name + " geometry expected=" + describe(savedRect)
                      + " actual=" + describe(actualRect));
            }
        }
    };
    const auto saveExpected = [&](const QJsonObject& expected) {
        QFile file(root + "/expected.json");
        const QByteArray bytes = QJsonDocument(expected).toJson();
        check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
              "write expected layout");
    };

    QJsonObject expected;
    if(phase == "save") {
        window.showNormal();
        window.setGeometry(80, 90, 1650, 950);
        for(QDockWidget* current : window.findChildren<QDockWidget*>())
            current->hide();
        for(QDockWidget* current : {maps, loaded, properties, outliner, log, validation, landscape})
            current->setFloating(false);
        window.addDockWidget(Qt::RightDockWidgetArea, maps);
        window.splitDockWidget(maps, outliner, Qt::Vertical);
        window.addDockWidget(Qt::LeftDockWidgetArea, properties);
        window.splitDockWidget(properties, loaded, Qt::Vertical);
        window.addDockWidget(Qt::BottomDockWidgetArea, log);
        window.tabifyDockWidget(log, validation);
        window.addDockWidget(Qt::LeftDockWidgetArea, landscape);
        for(QDockWidget* current : {maps, properties, outliner, log, validation})
            current->show();
        properties->raise();
        log->raise();
        loaded->hide();
        travel->setFloating(true);
        travel->show();
        scenario->setFloating(true);
        scenario->show();
        pump();
        travel->setGeometry(140, 180, 800, 620);
        scenario->setGeometry(1020, 230, 740, 580);
        window.resizeDocks({maps, properties}, {430, 520}, Qt::Horizontal);
        window.resizeDocks({maps, outliner}, {290, 480}, Qt::Vertical);
        pump();
        log->raise();
        pump();
        check(window.rect().intersects(log->geometry()),
              "chosen log tab remains active after events");
        if(qEnvironmentVariableIsSet("EI_DOCK_LAYOUT_MAXIMIZED")) {
            window.showMaximized();
            pump();
        }
        expected = capture();
        saveExpected(expected);
    } else {
        QFile file(root + "/expected.json");
        check(file.open(QIODevice::ReadOnly), "read expected layout");
        expected = QJsonDocument::fromJson(file.readAll()).object();
        compare(expected);
    }

    const QString mpr = qEnvironmentVariable("EI_DOCK_LAYOUT_MPR");
    if(!mpr.isEmpty()) {
        check(window.loadLandscapeWithProgress(QFileInfo(mpr)), "production MPR open");
        pump();
        compare(expected);
        if(phase == "save" && qEnvironmentVariableIsSet("EI_DOCK_LAYOUT_LANDSCAPE")) {
            window.showLandscapeTools();
            pump();
            check(!landscape->isHidden(), "landscape panel intentionally visible before close");
            expected = capture();
            saveExpected(expected);
        }
    }
    window.close();
    check(waitUntil([&]() { return !window.isVisible(); }),
          "ordinary asynchronous close completes");
    pump();
    check(!settings.value("ui/mainWindowState").toByteArray().isEmpty(),
          "mainWindowState written on normal close");
    check(visibleWindows == 0, "zero visible native windows");
    output << "RESULT failures=" << failures << "\n";
    return failures ? 1 : 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtWidgets>
#include <QtTest>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <windows.h>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "resource_setup_dialog.h"
#include "viewport_minimap_widget.h"
#include "endurance_ui_scale.h"
#include "resourcemanager.h"
#include "camera.h"
#include "localization.h"

namespace {
int visibleWindows = 0;
BOOL CALLBACK inspectWindow(HWND window, LPARAM)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if(pid == GetCurrentProcessId() && IsWindowVisible(window)) ++visibleWindows;
    return TRUE;
}
class HiddenWindows : public QObject {
    bool eventFilter(QObject* object, QEvent* event) override {
        if(event->type() == QEvent::Polish || event->type() == QEvent::Show)
            if(auto* widget = qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QJsonArray rectJson(const QRect& r) { return {r.x(),r.y(),r.width(),r.height()}; }
}

// Replaces the existing friend check entry, links all other production objects.
int runStartCameraCheck()
{
    const QString root = qEnvironmentVariable("EI_DPI_ROOT");
    if(root.isEmpty() || qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT")) return 2;
    qApp->setQuitOnLastWindowClosed(false);
    qApp->setProperty("workspaceLoadSmoke",true);
    if(qEnvironmentVariableIsSet("EI_DPI_OWNER_LAYOUT")) {
        QSettings owner(QSettings::NativeFormat,QSettings::UserScope,"EI Mapper Endurance","EI Mapper Endurance Edition");
        QSettings local;
        for(const auto& key:owner.allKeys()) if(key.startsWith("ui/")) local.setValue(key,owner.value(key));
        local.sync();
    }
    ei::installApplicationTranslator(*qApp,"ru");
    HiddenWindows hidden;
    qApp->installEventFilter(&hidden);
    QTimer modals;
    QObject::connect(&modals,&QTimer::timeout,[] {
        EnumWindows(inspectWindow,0);
        for(auto* widget : QApplication::topLevelWidgets()) {
            if(auto* setup=qobject_cast<CResourceSetupDialog*>(widget)) setup->reject();
            if(auto* box=qobject_cast<QMessageBox*>(widget)) if(box->isVisible()) box->reject();
        }
    });
    modals.start(10);
    const int uiScale=qEnvironmentVariableIntValue("EI_DPI_UI_SCALE");
    if(qEnvironmentVariableIsSet("EI_DPI_CALIBRATE_FONT")) {
        QFont base=qApp->font(); base.setPixelSize(12); qApp->setFont(base);
    }
    ei::applyUiScalePercent(uiScale ? uiScale : 150);
    MainWindow window;
    window.configureAutosaveTimer(false,300000);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setWindowState(Qt::WindowNoState);
    window.resize(2560,1340);
    window.show();
    QTest::qWait(300);
    if(qEnvironmentVariableIsSet("EI_DPI_MAXIMIZED")) {
        window.showMaximized(); QTest::qWait(300);
    }
    if(qEnvironmentVariableIsSet("EI_DPI_CONSTRAIN_WINDOW")) {
        const qreal dpr=window.devicePixelRatioF();
        window.setFixedSize(qRound(2560/dpr),qRound(1340/dpr));
        QTest::qWait(300);
    }
    auto* view=window.m_pView;
    dynamic_cast<COptStringList*>(window.m_settings->opt(eOptSetResource,"mapRoots"))->setValue({root+"/Maps"});
    window.refreshMapsBrowser();
    const QString mpr=root+"/Maps/zone1.mpr", mob=root+"/Maps/zone1.mob";
    window.openMapPair(mpr,mob,"gz1g");
    QElapsedTimer deadline; deadline.start();
    while((view->hasPendingOperation() || window.backgroundMobLoading()) && deadline.elapsed()<120000) QTest::qWait(20);
    QTest::qWait(500);
    view->ensureTerrainAutoCatalogReady();
    // Keep the measured viewport at the owner's physical window dimensions.
    QVector3D center(187.5f,117.5f,0);
    view->m_pLand->projectPt(center);
    QMatrix4x4 camera;
    camera.lookAt(center+QVector3D(-35,-50,55),center,QVector3D(0,0,1));
    view->m_cam->setPreviewView(camera);
    view->makeCurrent();
    view->updateGL();
    GLint viewport[4]={}, framebuffer=0;
    auto* gl=QOpenGLContext::currentContext()->functions();
    gl->glGetIntegerv(GL_VIEWPORT,viewport);
    gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING,&framebuffer);
    const auto nativeWindow=GetParent(reinterpret_cast<HWND>(view->winId()));
    RECT client{}; GetClientRect(reinterpret_cast<HWND>(view->winId()),&client);
    Q_UNUSED(nativeWindow);
    QJsonObject result;
    result["qtScaleFactor"]=qEnvironmentVariable("QT_SCALE_FACTOR");
    result["qtScreenScaleFactors"]=qEnvironmentVariable("QT_SCREEN_SCALE_FACTORS");
    result["uiScale"]=uiScale;
    result["window"]=rectJson(window.geometry());
    result["widget"]=rectJson(view->geometry());
    QJsonArray ancestors;
    for(QWidget* parent=view->parentWidget();parent;parent=parent->parentWidget())
        ancestors.append(QJsonObject{{"name",parent->objectName()},{"rect",rectJson(parent->geometry())}});
    result["ancestors"]=ancestors;
    result["visibleRegion"]=rectJson(view->visibleRegion().boundingRect());
    QRect clippedRect=view->rect();
    for(QWidget* parent=view->parentWidget();parent;parent=parent->parentWidget())
        clippedRect=clippedRect.intersected(QRect(view->mapFrom(parent,QPoint()),parent->size()));
    result["ancestorVisibleRect"]=rectJson(clippedRect);
    result["clippedByAncestor"]=clippedRect != view->rect();
    result["screenGeometry"]=rectJson(window.windowHandle()->screen()->geometry());
    result["logicalDpi"]=window.windowHandle()->screen()->logicalDotsPerInch();
    result["fontHeight"]=window.fontMetrics().height();
    result["calibratedFont"]=qEnvironmentVariableIsSet("EI_DPI_CALIBRATE_FONT");
    QJsonArray catalog;
    for(const auto& entry:window.m_mapCatalog.entries) if(entry.mprName=="zone1")
        catalog.append(QJsonObject{{"zone",entry.zoneId},{"mpr",entry.mprPath},{"mob",entry.mobPath},{"type",entry.zoneType}});
    result["catalog"]=catalog;
    result["dpr"]=view->devicePixelRatioF();
    result["nativeClient"]=QJsonArray{int(client.right-client.left),int(client.bottom-client.top)};
    result["glViewport"]=QJsonArray{viewport[0],viewport[1],viewport[2],viewport[3]};
    result["rendererSize"]=QJsonArray{view->m_width,view->m_height};
    result["framebuffer"]=framebuffer;
    const bool queryReady=view->beginEditorQueryFramebuffer();
    result["queryReady"]=queryReady;
    if(queryReady) {
        result["queryFboSize"]=QJsonArray{view->m_editorQueryFbo->width(),view->m_editorQueryFbo->height()};
        const QPoint point(view->width()/2,view->height()/2);
        const qreal dpr=view->devicePixelRatioF();
        const int x=qRound(point.x()*dpr), y=view->m_height-1-qRound(point.y()*dpr);
        gl->glEnable(GL_SCISSOR_TEST);
        gl->glScissor(x,y,1,1);
        gl->glClearColor(1,0,0,1); gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glDisable(GL_SCISSOR_TEST);
        QVector<SColor> colors;view->getColorFromRect(QRect(point,point),colors);
        result["logicalPickReadsPhysicalPixel"]=!colors.isEmpty() && colors.first().rgb[0]==255;
        view->endEditorQueryFramebuffer();
    }
    result["zone"]=window.m_mapTestEntry.zoneId;
    result["zoneType"]=window.m_mapTestEntry.zoneType;
    result["requestedZone"]=window.m_openedMapZoneId;
    result["minimapVisible"]=window.m_viewportMinimap->isVisible();
    result["minimap"]=rectJson(window.m_viewportMinimap->geometry());
    result["minimapInside"]=view->rect().contains(window.m_viewportMinimap->geometry());
    QJsonArray overlays;
    for(auto* widget:view->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
        if(widget->isVisible()) overlays.append(QJsonObject{{"name",widget->objectName()},{"rect",rectJson(widget->geometry())}});
    result["overlays"]=overlays;
    QJsonArray clipped;
    for(auto* button:window.findChildren<QToolButton*>())
        if(button->isVisible() && !button->text().isEmpty()
           && (button->icon().isNull() || button->toolButtonStyle()!=Qt::ToolButtonIconOnly)
           && button->width()+2<button->sizeHint().width())
            clipped.append(QJsonObject{{"text",button->text()},{"width",button->width()},{"hint",button->sizeHint().width()}});
    result["clippedButtons"]=clipped;
    const QString tag=qEnvironmentVariable("EI_DPI_TAG");
    auto frame=view->grabFrameBuffer();
    result["frameSize"]=QJsonArray{frame.width(),frame.height()};
    QString captureError;
    auto snapshot=view->captureTopdownPass(ETopdownPass::None,frame.size(),{},&captureError);
    result["captureError"]=captureError;
    if(!snapshot.isNull()) frame=snapshot;
    deadline.restart();
    const auto sceneBusy=[&] {
        return (view->m_unitAppearanceWatcher && view->m_unitAppearanceWatcher->isRunning())
            || view->m_unitAppearanceBatchPending || view->m_terrainAutoCatalogPending
            || view->m_terrainAutoCatalogBuilding;
    };
    while(sceneBusy() && deadline.elapsed()<60000) {
        view->captureTopdownPass(ETopdownPass::None,QSize(640,400),{},&captureError);
        QTest::qWait(20);
    }
    result["sceneReady"]=!sceneBusy();
    if(result["sceneReady"].toBool()) {
        snapshot=view->captureTopdownPass(ETopdownPass::None,frame.size(),{},&captureError);
        if(!snapshot.isNull()) frame=snapshot;
    }
    frame.save(root+"/"+tag+"-frame.png");
    auto canvas=window.grab().toImage();
    // QGLWidget is a native child. Composite its owning GL readback into grab.
    QPainter painter(&canvas);
    const QPoint offset=view->mapTo(&window,QPoint());
    painter.drawImage(QRect(offset,view->size()),frame);
    for(auto* widget:view->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly))
        if(widget->isVisible()) painter.drawPixmap(offset+widget->pos(),widget->grab());
    painter.end();
    canvas.save(root+"/"+tag+"-window.png");
    // Explicit identity, direct MPR context, and briefing regression.
    QJsonArray identities;
    for(const QString& zone:QStringList{"gz1g","bz1g","gz1g"}) {
        window.m_openedMapZoneId=zone;
        window.refreshMapTestContext(true,false);
        identities.append(QJsonObject{{"requested",zone},{"resolved",window.m_mapTestEntry.zoneId},
            {"briefing",window.m_mapTestEntry.isBriefing()},{"visible",window.m_viewportMinimap->isVisible()}});
    }
    result["identities"]=identities;
    window.m_openedMapZoneId.clear();
    window.refreshMapTestContext(true,false);
    result["inferredZone"]=window.m_mapTestEntry.zoneId;
    result["inferredMinimapVisible"]=window.m_viewportMinimap->isVisible();
    window.m_openedMapZoneId="gz1g";
    window.refreshMapTestContext(true,false);
    QJsonArray sizeModes;
    auto* larger=window.m_viewportMinimap->findChild<QToolButton*>("viewportMinimapLarger");
    for(int i=0;i<3;++i) {
        window.positionViewportMinimap();
        sizeModes.append(QJsonObject{{"level",window.m_viewportMinimap->sizeLevel()},
            {"inside",view->rect().contains(window.m_viewportMinimap->geometry())}});
        if(larger->isEnabled()) larger->click();
    }
    result["sizeModes"]=sizeModes;
    auto* expand=window.m_viewportMinimap->findChild<QToolButton*>("viewportMinimapExpand");
    expand->click();
    result["collapsedInside"]=view->rect().contains(window.m_viewportMinimap->geometry());
    expand->click();
    view->m_cam->clearPreviewView();
    result["visibleNativeWindows"]=visibleWindows;
    QFile output(root+"/"+tag+".json"); output.open(QIODevice::WriteOnly);
    output.write(QJsonDocument(result).toJson()); output.close();
    QTextStream(stdout)<<QJsonDocument(result).toJson()<<"\n";
    window.close();
    deadline.restart();
    while(window.isVisible() && deadline.elapsed()<30000) QTest::qWait(10);
    result["closed"]=!window.isVisible();
    output.open(QIODevice::WriteOnly|QIODevice::Truncate);
    output.write(QJsonDocument(result).toJson()); output.close();
    return visibleWindows ? 1 : 0;
}

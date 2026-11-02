#include <QtWidgets>
#include <QtTest>
#include <windows.h>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "settings.h"
#include "minimap_export_dialog.h"
#include "resource_setup_dialog.h"

namespace {
struct Hidden : QObject {
    bool eventFilter(QObject* object, QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
int visibleWindows=0;
BOOL CALLBACK inspect(HWND window, LPARAM) {
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId() && IsWindowVisible(window)) ++visibleWindows;
    return TRUE;
}
bool until(const std::function<bool()>& ready) {
    QElapsedTimer timer;timer.start();
    while(!ready() && timer.elapsed()<120000) QTest::qWait(10);
    return ready();
}
QJsonArray rgbMean(const QImage& input) {
    auto image=input.convertToFormat(QImage::Format_ARGB32);
    double red=0,green=0,blue=0,count=0;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        const auto color=image.pixel(x,y);
        if((color&0xffffff)==0) continue;
        red+=qRed(color);green+=qGreen(color);blue+=qBlue(color);++count;
    }
    return {red/qMax(1.0,count),green/qMax(1.0,count),blue/qMax(1.0,count)};
}
}

int runAssemblyCatalogCheck() {
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    const auto check=[&](bool ok,const QString& what) {
        out<<(ok?"PASS ":"FAIL ")<<what<<"\n";out.flush();
        if(!ok) throw what;
        ++passes;
    };
    try {
        const QString root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_ROOT"));
        const QString maps=QDir::fromNativeSeparators(qEnvironmentVariable("EI_MCP_TEST_MAPS"));
        check(!root.isEmpty()&&!qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"),"explicit private profile/lock root");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);
        qApp->setProperty("workspaceLoadSmoke",true);
        QTimer visibility;QObject::connect(&visibility,&QTimer::timeout,[](){EnumWindows(inspect,0);});visibility.start(10);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* dialog=qobject_cast<CResourceSetupDialog*>(widget)) dialog->reject();
        });setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        window.m_currentCollectionPath=root+"/private.collection";
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,800);window.show();
        auto& view=*window.view();QCoreApplication::processEvents();view.makeCurrent();
        check(view.isValid(),"production MainWindow desktop GL offscreen");
        view.m_neighborLandscapesVisible=false;
        // The subject is export of an already loaded scene. Prepare terrain
        // before starting deferred MOB publication; do not exercise the separate
        // known open-pair/catalog reentrancy failure as part of this fixture.
        check(window.loadLandscapeWithProgress(QFileInfo(maps+"/zone1.mpr")),"production MPR open");
        view.m_timer->stop();view.makeCurrent();
        check(view.ensureTerrainAutoCatalogReady(),"production terrain ready before MOB publication");
        check(window.loadMobWithProgress(QFileInfo(maps+"/zone1.mob"),false),"production primary MOB open");
        check(until([&](){return !window.backgroundMobLoading();}),"complete real zone1/AddMob scene");
        window.m_minimapRefreshTimer->stop();window.m_minimapCameraTimer->stop();
        check(view.m_environmentMob!=nullptr,"native primary MOB environment");
        const auto originalWorld=view.m_environmentMob->worldSet();
        QJsonObject evidence{{"originalWsTime",originalWorld.data(eWsTypeTime)},
            {"originalRegion",view.m_weatherRegion},{"originalCave",view.m_caveEnvironment}};
        QMap<int,QImage> first;
        const auto capture=[&](const QString& tag,int minutes,bool compareViewport) {
            view.m_environmentPreviewTime->setValue(minutes);
            // Exercise the real UI signal, including private options persistence.
            check(dynamic_cast<COptBool*>(window.m_settings->opt("overrideWorldTime"))->value(),"viewport override enabled "+tag);
            const int clock=(minutes/60)*100+minutes%60;
            check(dynamic_cast<COptInt*>(window.m_settings->opt("renderTimeOfDay"))->value()==clock,"UI persists selected time "+tag);
            view.makeCurrent();view.paintGL();
            auto raw=view.captureMinimapScene(true,1024,false);
            check(!raw.capturedReviewImage.isNull()&&!raw.capturedUnlitReviewImage.isNull(),"GL readbacks "+tag);
            check(raw.capturedReviewImage.save(root+"/"+tag+"-shaded-review.png")&&raw.capturedUnlitReviewImage.save(root+"/"+tag+"-unlit-review.png"),"intermediate PNGs "+tag);
            auto baked=CView::finishMinimapSceneCapture(raw);
            check(baked.bake.success,"production bake "+tag);
            baked.bake.gameImage.save(root+"/"+tag+"-shaded.png");
            baked.unlitGameImage.save(root+"/"+tag+"-unlit.png");
            CMinimapExportDialog dialog(baked,{},"","mini274","zone1",SMinimapGradeSettings(100,100,100,100,0),false,&window);
            dialog.setAttribute(Qt::WA_DeleteOnClose,false);
            auto* slider=dialog.findChild<QSlider*>("minimapShadingSlider");
            check(slider&&slider->maximum()==150,"production relief slider "+tag);
            QJsonObject images;
            for(int shade:{0,50,100,128,150}) {
                slider->setValue(shade);const auto image=dialog.gradedImage();
                check(image.save(root+"/"+tag+"-"+QString::number(shade)+".png"),"export "+tag+" "+QString::number(shade));
                images[QString::number(shade)]=rgbMean(image);
                if(compareViewport) {
                    if(first.contains(shade)) check(image==first[shade],"viewport time cannot recolor minimap "+tag+" "+QString::number(shade));
                    else first[shade]=image;
                }
                const auto grade=dialog.gradeSettings();
                check(grade.hueDegrees==0&&grade.saturationPercent==100&&grade.brightnessPercent==100&&grade.contrastPercent==100,"other grading controls unchanged");
            }
            evidence[tag]=images;dialog.close();
        };
        if(!qEnvironmentVariableIsSet("EI_MINIMAP_REFERENCE_ONLY")) {
        capture("viewport-noon",720,true);
        capture("viewport-morning",360,true);
        capture("viewport-evening",1200,true);
        capture("owner-saved-1240",760,true);
        if(!qEnvironmentVariable("EI_MINIMAP_BEFORE").isEmpty()) {
            view.setMapSkyType("outdoor","ingos");
            capture("viewport-other-region",720,true);
            view.setMapSkyType("outdoor",evidence["originalRegion"].toString());
            dynamic_cast<COptBool*>(window.m_settings->opt("renderFog"))->setValue(true);
            dynamic_cast<COptInt*>(window.m_settings->opt("renderLightingMode"))->setValue(0);
            view.m_shadowCascadeDiagnostic=true;
            capture("viewport-unlit-fog-debug",720,true);
            view.m_shadowCascadeDiagnostic=false;
            dynamic_cast<COptBool*>(window.m_settings->opt("renderFog"))->setValue(false);
            dynamic_cast<COptInt*>(window.m_settings->opt("renderLightingMode"))->setValue(2);
        }
        }
        // These are separate diagnostics, never substitutes for viewport tests.
        auto ws=originalWorld;ws.setData(eWsTypeTime,"12");view.m_environmentMob->setWorldSet(ws);
        capture("map-noon-before-reference",720,false);
        if(!qEnvironmentVariableIsSet("EI_MINIMAP_REFERENCE_ONLY")) {
        ws.setData(eWsTypeTime,"20");view.m_environmentMob->setWorldSet(ws);
        capture("map-evening-diagnostic",1200,false);
        }
        view.m_environmentMob->setWorldSet(originalWorld);
        QFile result(root+"/images.json");check(result.open(QIODevice::WriteOnly),"evidence JSON");
        result.write(QJsonDocument(evidence).toJson());result.close();
        EnumWindows(inspect,0);check(visibleWindows==0,"0 visible native windows");
        window.close();out<<"RESULT "<<passes<<" PASS 0 FAIL\n";return 0;
    } catch(const QString& error) {
        out<<"ERROR "<<error<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";return 1;
    }
}

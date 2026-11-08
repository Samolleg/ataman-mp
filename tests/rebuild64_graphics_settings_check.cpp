#include "rebuild64_graphics_settings.h"
#include "endurance_ui_scale.h"
#include <QtWidgets>
#include <cmath>
#include <cstdio>
#include <windows.h>
int main(int argc,char** argv){
    QApplication app(argc,argv);QTranslator translator;
    if(argc<2||!translator.load(QString::fromLocal8Bit(argv[1])))return 2;app.installTranslator(&translator);
    ei::applyUiScalePercent(ei::persistedUiScalePercent());
    CRebuild64GraphicsDialog dialog;dialog.setAttribute(Qt::WA_DontShowOnScreen);dialog.setAttribute(Qt::WA_ShowWithoutActivating);dialog.show();app.processEvents();
    int bad=0;const auto check=[&](bool value,const QString& what){std::fprintf(stderr,"%s %s\n",value?"PASS":"FAIL",what.toUtf8().constData());if(!value)++bad;};
    check(dialog.windowTitle()==QString::fromUtf8("Графика Rebuild64"),"Russian title");
    for(const auto& o:rebuild64graphics::options()) {
        auto* c=dialog.findChild<QWidget*>(o.key);check(c&&c->isEnabled()==o.enabled,QString(o.key)+" supported state");
        if(auto* s=dialog.findChild<QSlider*>(QString(o.key)+"Slider")){const int expected=o.type==2?int(std::round((o.initial.toDouble()-o.minimum)/o.step)):o.initial.toInt();check(s->value()==expected,QString(o.key)+" initial slider matches numeric value");}
        if(auto* s=qobject_cast<QSpinBox*>(c))check(s->minimum()==o.minimum&&s->maximum()==o.maximum,QString(o.key)+" integer bounds");
        if(auto* s=qobject_cast<QDoubleSpinBox*>(c))check(qAbs(s->minimum()-o.minimum)<.001&&qAbs(s->maximum()-o.maximum)<.001,QString(o.key)+" floating bounds");
    }
    auto values=rebuild64graphics::defaults();values["water_surf_no_sea_maps"]="zone7,zone8";values["upscaler"]=2;QString error;
    check(rebuild64graphics::save(values,&error),"own INI save");check(rebuild64graphics::load().value("water_surf_no_sea_maps").toString()=="zone7,zone8","map list roundtrip");check(rebuild64graphics::load().value("upscaler").toInt()==0,"mandatory override");
    auto* tabs=dialog.findChild<QTabWidget*>();for(int i=0;i<tabs->count();++i){tabs->setCurrentIndex(i);app.processEvents();dialog.grab().save(QString("dialog-%1.png").arg(i));auto* area=qobject_cast<QScrollArea*>(tabs->currentWidget());check(area&&area->horizontalScrollBar()->maximum()==0,QString("no horizontal clipping tab %1").arg(i));}
    check(!IsWindowVisible(reinterpret_cast<HWND>(dialog.winId())),"no visible native window");
    return bad?1:0;
}

#include "rebuild64_graphics_settings.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QLineEdit>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <cmath>
namespace {
QString trG(const char* text){return QCoreApplication::translate("Rebuild64Graphics",text);}
QStringList choices(const QString& key) {
    if(key=="anisotropy")return {"1×","2×","4×","8×","16×"};
    if(key=="shadow_cascade_quality")return {"1024","2048","4096"};
    if(key=="point_shadow_quality")return {"128","256","512"};
    if(key=="fur_mode")return {trG("Off"),trG("Fur"),trG("Fur and hair")};
    if(key=="fog_preset")return {trG("Original"),trG("Atmospheric"),trG("Volumetric")};
    if(key=="background_mode")return {trG("Continue drawing"),trG("Pause")};
    if(key=="terrain_blend")return {trG("Original"),trG("Terrain 2.0")};
    if(key=="sun_shafts")return {trG("Off"),trG("Screen space"),trG("Volumetric"),trG("Both")};
    if(key=="material_quality")return {trG("Off"),trG("Sun and sky"),trG("Lamps and relief"),trG("Full")};
    if(key=="water_reflection_quality")return {trG("Low"),trG("Medium"),trG("High")};
    if(key=="water_surf_quality")return {trG("Low"),trG("Medium"),trG("High")};
    if(key=="terrain_quality")return {trG("Low"),trG("Medium"),trG("High")};
    if(key=="bloom")return {trG("Off"),trG("Low"),trG("High")};
    if(key=="depth_of_field")return {trG("Off"),trG("Low"),trG("High")};
    if(key=="critters")return {trG("Off"),trG("Low"),trG("High")};
    if(key=="ambient_particles")return {trG("Off"),trG("Low"),trG("High")};
    if(key=="ao_quality")return {trG("Off"),trG("Low"),trG("High")};
    if(key=="cave_gi")return {trG("Off"),trG("Low"),trG("High")};
    if(key=="sky_clouds")return {trG("Original"),trG("Low"),trG("High")};
    if(key=="ground_cover")return {trG("Off"),trG("Low"),trG("Normal"),trG("High")};
    if(key=="fur_quality")return {trG("Low"),trG("High")};
    if(key=="fog_volume_quality")return {trG("Low"),trG("High")};
    if(key=="terrain_tessellation")return {trG("Off"),trG("On")};
    if(key=="critter_labels")return {trG("Off"),trG("On")};
    if(key=="wind_trails")return {trG("Off"),trG("Normal"),trG("Dense")};
    if(key=="precipitation")return {trG("Off"),trG("Low"),trG("Medium"),trG("High"),trG("Ultra")};
    if(key=="precip_strength")return {trG("Automatic"),trG("Light"),trG("Medium"),trG("Heavy")};
    if(key=="precip_strength_override")return {trG("Use setting"),trG("Light"),trG("Medium"),trG("Heavy")};
    if(key=="precip_test")return {trG("Follow weather"),trG("Rain"),trG("Snow"),trG("Sandstorm")};
    if(key=="sky_weather_test")return {trG("Follow weather"),trG("Storm"),trG("Clear")};
    if(key=="material_view")return {trG("Shaded"),trG("Roughness"),trG("Metal"),trG("Material channels")};
    if(key=="water_surf_debug")return {trG("Off"),trG("Sea exposure"),trG("Shore impact"),trG("Foam")};
    if(key=="fog_debug_component")return {trG("All"),trG("Air haze"),trG("Height fog"),trG("Mist"),trG("Sun shafts"),trG("Lamp scatter")};
    if(key=="shadow_neighbour_casters")return {trG("Off"),trG("Close cascades"),trG("All cascades")};
    if(key=="shadow_softness")return {trG("Fixed edge"),trG("Contact: low"),trG("Contact: high")};
    if(key=="emissive_lights")return {trG("Off"),trG("Normal (8 m)"),trG("Fine (4 m)")};
    if(key=="ground_cover_shadows")return {trG("Off"),trG("Large cover"),trG("All eligible cover")};
    if(key=="terrain2_cache")return {trG("Off"),trG("On"),trG("Cache debug")};
    if(key=="critter_showcase")return {trG("Off"),trG("Normal size"),trG("Double size"),trG("Ignore depth")};
    if(key=="renderer_backend")return {trG("Direct3D 11"),trG("Direct3D 12")};
    if(key=="upscaler")return {trG("Off"),trG("NVIDIA DLSS"),trG("AMD FSR 2")};
    if(key=="upscaler_quality")return {trG("Quality"),trG("Balanced"),trG("Performance"),trG("Ultra performance"),trG("Native AA")};
    if(key=="frame_generation")return {trG("Off"),trG("NVIDIA DLSS FG"),trG("AMD FSR FG")};
    if(key=="reflex")return {trG("Off"),trG("Low latency"),trG("Low latency and sleep")};
    return {};
}
}
namespace rebuild64graphics {
const QVector<Option>& options(){static const QVector<Option> value={
#include "rebuild64_graphics_options.inc"
};return value;}
QString filePath(){return QDir::current().absoluteFilePath("rebuild64-graphics.ini");}
QVariantMap defaults(){QVariantMap out;for(const auto& o:options())out[o.key]=o.initial;return out;}
QVariantMap load(){
    auto out=defaults();QSettings local(filePath(),QSettings::IniFormat);local.setIniCodec("UTF-8");
    for(const auto& o:options()) {
        if(!o.enabled)continue;const auto raw=local.value(QString("graphics/")+o.key,o.initial);
        bool ok=true;QVariant value=raw;
        if(o.type==0)value=raw.toBool();
        else if(o.type==1){const int n=raw.toInt(&ok);value=qBound(int(o.minimum),n,int(o.maximum));}
        else if(o.type==2){const double n=raw.toDouble(&ok);ok=ok&&std::isfinite(n);value=qBound(o.minimum,n,o.maximum);}
        if(o.type==3&&raw.type()==QVariant::StringList)value=raw.toStringList().join(",");
        if(ok)out[o.key]=value;
    }
    return out;
}
QByteArray encode(const QVariantMap& values){
    QByteArray ini("[graphics]\n");
    for(const auto& o:options()) {
        const auto value=o.enabled?values.value(o.key,o.initial):o.initial;
        QByteArray text=o.type==0?QByteArray(value.toBool()?"true":"false"):o.type==2?QByteArray::number(value.toDouble(),'g',9):value.toString().toUtf8();
        text.replace('\n',' ');text.replace('\r',' ');ini+=QByteArray(o.key)+"="+text+"\n";
    }
    return ini;
}
bool save(const QVariantMap& values,QString* error){
    QSaveFile file(filePath());const auto bytes=encode(values);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
bool needsRestart(const QVariantMap& before,const QVariantMap& after){
    // Native map admission/material preparation consumes these at creation.
    for(const auto* key:{"water_surf_no_sea_maps","neighbour_maps","neighbour_fog_range","draw_distance","terrain_blend_neighbours","terrain_blend","terrain_tessellation","terrain2_cache","render_thread"})
        if(before.value(key)!=after.value(key))return true;
    return false;
}
QString pipelineDirectory(const QString& root,const QString& dllPath){
    QFile dll(dllPath);if(!dll.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&dll))return {};
    // Native program keys include defines and all shader inputs. The DLL hash
    // isolates producer/ABI changes; settings variants coexist inside it.
    return QDir(root).filePath("pipelines/"+QString::fromLatin1(hash.result().toHex()));
}
}
CRebuild64GraphicsDialog::CRebuild64GraphicsDialog(QWidget* parent):QDialog(parent),m_applied(rebuild64graphics::load()){
    setObjectName("rebuild64GraphicsDialog");setWindowTitle(trG("Rebuild64 graphics"));resize(1000,760);
    auto* layout=new QVBoxLayout(this);auto* tabs=new QTabWidget(this);layout->addWidget(tabs);
    const char* groups[]={"General settings","Shadow settings","Water settings","Terrain settings","Material settings","Fog settings","Fur settings","Grass settings"};
    QVector<QFormLayout*> forms;
    for(const auto* group:groups){auto* area=new QScrollArea(tabs);area->setWidgetResizable(true);auto* content=new QWidget(area);auto* form=new QFormLayout(content);form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);form->setRowWrapPolicy(QFormLayout::WrapAllRows);area->setWidget(content);tabs->addTab(area,trG(group));forms.append(form);}
    for(const auto& o:rebuild64graphics::options()) {
        QWidget* control=nullptr;QWidget* row=nullptr;const QString key=o.key;
        if(o.type==0)control=new QCheckBox(this);
        else if(o.type==3)control=new QLineEdit(this);
        else if(o.type==1&&o.maximum-o.minimum<=5&&o.minimum==0){
            auto* combo=new QComboBox(this);const auto names=choices(key);
            for(int i=int(o.minimum);i<=int(o.maximum);++i)combo->addItem(i<names.size()?names[i]:QString::number(i),i);
            control=combo;
        } else {
            row=new QWidget(this);auto* line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);auto* slider=new QSlider(Qt::Horizontal,row);slider->setObjectName(key+"Slider");line->addWidget(slider,1);
            if(o.type==1){auto* spin=new QSpinBox(row);spin->setRange(int(o.minimum),int(o.maximum));spin->setSingleStep(int(o.step));slider->setRange(int(o.minimum),int(o.maximum));slider->setSingleStep(int(o.step));connect(slider,&QSlider::valueChanged,spin,&QSpinBox::setValue);connect(spin,QOverload<int>::of(&QSpinBox::valueChanged),slider,&QSlider::setValue);control=spin;}
            else {auto* spin=new QDoubleSpinBox(row);spin->setDecimals(3);spin->setRange(o.minimum,o.maximum);spin->setSingleStep(o.step);slider->setRange(0,int(std::ceil((o.maximum-o.minimum)/o.step)));
                connect(slider,&QSlider::valueChanged,spin,[spin,o](int value){spin->setValue(qMin(o.maximum,o.minimum+value*o.step));});
                connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),slider,[slider,o](double value){QSignalBlocker block(slider);slider->setValue(int(std::round((value-o.minimum)/o.step)));});control=spin;}
            line->addWidget(control);
        }
        control->setObjectName(key);control->setEnabled(o.enabled);m_controls[key]=control;
        if(!o.enabled)control->setToolTip(trG("Unavailable in the mapper: fixed Direct3D 11, no upscaler, frame generation or Reflex."));
        if(!row)row=control;row->setEnabled(o.enabled);auto* label=new QLabel(trG(o.label),this);label->setWordWrap(true);forms[o.group]->addRow(label,row);
    }
    auto* note=new QLabel(trG("Changes affect only Rebuild64. Map, edits and camera are preserved. Some changes prepare the render session again."),this);note->setWordWrap(true);layout->addWidget(note);
    auto* error=new QLabel(this);error->setObjectName("graphicsError");error->setWordWrap(true);layout->addWidget(error);
    auto* buttons=new QDialogButtonBox(this);auto* apply=buttons->addButton(trG("Apply"),QDialogButtonBox::ApplyRole);apply->setObjectName("graphicsApply");
    auto* reset=buttons->addButton(trG("Rebuild64 defaults"),QDialogButtonBox::ResetRole);reset->setObjectName("graphicsDefaults");
    auto* cancel=buttons->addButton(trG("Cancel"),QDialogButtonBox::RejectRole);cancel->setObjectName("graphicsCancel");layout->addWidget(buttons);
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(reset,&QPushButton::clicked,this,[this]{display(rebuild64graphics::defaults());});
    connect(apply,&QPushButton::clicked,this,[this,error]{const auto next=values();QString why;if(!rebuild64graphics::save(next,&why)){error->setText(trG("Cannot save graphics settings: ")+why);return;}error->clear();if(next!=m_applied){const bool restart=rebuild64graphics::needsRestart(m_applied,next);m_applied=next;emit graphicsApplied(rebuild64graphics::encode(next),restart);}});
    display(m_applied);
}
void CRebuild64GraphicsDialog::display(const QVariantMap& values){
    for(const auto& o:rebuild64graphics::options()){auto* c=m_controls.value(o.key);const auto v=values.value(o.key,o.initial);
        if(auto* check=qobject_cast<QCheckBox*>(c))check->setChecked(v.toBool());
        else if(auto* combo=qobject_cast<QComboBox*>(c))combo->setCurrentIndex(combo->findData(v.toInt()));
        else if(auto* spin=qobject_cast<QSpinBox*>(c))spin->setValue(v.toInt());
        else if(auto* spin=qobject_cast<QDoubleSpinBox*>(c))spin->setValue(v.toDouble());
        else if(auto* text=qobject_cast<QLineEdit*>(c))text->setText(v.toString());
        if(auto* slider=findChild<QSlider*>(QString(o.key)+"Slider")){QSignalBlocker block(slider);slider->setValue(o.type==2?int(std::round((v.toDouble()-o.minimum)/o.step)):v.toInt());}
    }
}
QVariantMap CRebuild64GraphicsDialog::values()const {
    auto out=rebuild64graphics::defaults();for(const auto& o:rebuild64graphics::options()){auto* c=m_controls.value(o.key);if(!o.enabled)continue;
        if(auto* check=qobject_cast<QCheckBox*>(c))out[o.key]=check->isChecked();
        else if(auto* combo=qobject_cast<QComboBox*>(c))out[o.key]=combo->currentData();
        else if(auto* spin=qobject_cast<QSpinBox*>(c))out[o.key]=spin->value();
        else if(auto* spin=qobject_cast<QDoubleSpinBox*>(c))out[o.key]=spin->value();
        else if(auto* text=qobject_cast<QLineEdit*>(c))out[o.key]=text->text();
    }return out;
}

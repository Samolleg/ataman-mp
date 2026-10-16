#include "lever_approach_dialog.h"
#include "database_document.h"
#include "lever_approach.h"
#include "view.h"
#include "node.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTimer>
#include <QLineF>
#include <QMessageBox>
#include <QSignalBlocker>

namespace ei {
LeverApproachDialog::LeverApproachDialog(const QSharedPointer<DatabaseDocument>& document,
                                       CView* view, QWidget* parent)
    : QDialog(parent,Qt::Tool),m_document(document),m_view(view)
{
    setObjectName("leverApproachDialog");
    setWindowTitle(QString::fromUtf8("Точки подхода"));
    setAttribute(Qt::WA_DeleteOnClose);
    auto* layout=new QFormLayout(this);
    auto* explanation=new QLabel(QString::fromUtf8(
        "Точки общие для всех рычагов этой модели.\n"
        "Выберите точку и щёлкните по земле; маркер можно перетащить.\n"
        "Дистанция персонажа влияет только на предпросмотр, в БД не сохраняется."),this);
    explanation->setWordWrap(true);layout->addRow(explanation);
    m_count=new QComboBox(this);m_count->setObjectName("leverPointCount");
    m_count->addItems({QString::fromUtf8("1 точка"),QString::fromUtf8("2 точки")});
    layout->addRow(QString::fromUtf8("Количество"),m_count);
    m_active=new QComboBox(this);m_active->setObjectName("leverActivePoint");
    m_active->addItems({QString::fromUtf8("Точка 1"),QString::fromUtf8("Точка 2")});
    layout->addRow(QString::fromUtf8("Установить мышью"),m_active);
    for(int i=0;i<4;++i) {
        auto* spin=new QDoubleSpinBox(this);m_coordinates[i]=spin;
        spin->setObjectName(QString("leverCoordinate%1").arg(i));
        spin->setDecimals(8);spin->setRange(-1e20,1e20);spin->setSingleStep(.1);
        layout->addRow(QString::fromUtf8("Точка %1 — %2 (БД)").arg(i/2+1).arg(i%2?"Y":"X"),spin);
        connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,i](double value){
            if(i%2)m_points[i/2].setY(float(value));else m_points[i/2].setX(float(value)); preview();
        });
    }
    m_reach=new QDoubleSpinBox(this);m_reach->setObjectName("leverPreviewReach");
    m_reach->setDecimals(3);m_reach->setRange(0,100);m_reach->setSingleStep(.1);m_reach->setValue(.5);
    layout->addRow(QString::fromUtf8("Дистанция персонажа (пример)"),m_reach);
    m_status=new QLabel(this);m_status->setWordWrap(true);layout->addRow(m_status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,this);
    buttons->button(QDialogButtonBox::Apply)->setText(QString::fromUtf8("Применить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("Отмена"));
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,&LeverApproachDialog::apply);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);layout->addRow(buttons);
    connect(m_count,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index){
        if(index==0)m_active->setCurrentIndex(0);m_active->setEnabled(index==1);
        m_coordinates[2]->setEnabled(index==1);m_coordinates[3]->setEnabled(index==1);preview();
    });
    connect(m_reach,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this]{preview();});
    auto* timer=new QTimer(this);timer->setInterval(100);
    connect(timer,&QTimer::timeout,this,[this]{if(m_ready&&!contextValid())reject();});timer->start();
}
LeverApproachDialog::~LeverApproachDialog() { if(m_view){m_view->removeEventFilter(this);m_view->setLeverApproachOverlay({});} }
bool LeverApproachDialog::initialize(QString* error) {
    auto fail=[&](const QString& message){if(error)*error=message;return false;};
    const auto selection=m_view->selectedNodes();
    if(selection.size()!=1 || selection.first()->nodeType()!=eLever || !m_view->hasEditableSelection())
        return fail(QString::fromUtf8("Выберите один редактируемый рычаг."));
    m_node=selection.first();m_model=m_node->modelName();m_origin=m_node->drawPosition();
    m_position=m_node->position();m_complexion=m_node->constitution();m_rotation=m_node->rotation();
    m_profile=m_document->profileId();
    const auto* table=m_document->table("levers.leverprototypes");
    if(!table)return fail(QString::fromUtf8("В активной БД нет таблицы рычагов."));
    int matches=0;
    for(const auto& row:table->rows)if(!row.deleted&&row.values.value("fld.0.0").toString()==m_model){
        ++matches;m_rowKey=row.stableKey;m_original=row.values;
    }
    if(matches!=1)return fail(QString::fromUtf8("Для модели %1 нужна ровно одна запись levers.ldb; найдено: %2. Откройте редактор БД.").arg(m_model).arg(matches));
    const auto array=QJsonDocument::fromJson(m_original.value("fld.1.0").toString().toUtf8()).array();
    if(array.size()!=4)return fail(QString::fromUtf8("Запись должна содержать ровно четыре координаты."));
    const int count=m_original.value("fld.2.0").toInt();
    if(count<0||count>2)return fail(QString::fromUtf8("Некорректное количество точек в БД."));
    for(int i=0;i<4;++i){
        const bool finite=array[i].isDouble()&&std::isfinite(float(array[i].toDouble()));
        if(!finite&&i<count*2)return fail(QString::fromUtf8("Активные координаты БД повреждены."));
        const float value=finite?float(array[i].toDouble()):0.f;
        QSignalBlocker blocker(m_coordinates[i]);m_coordinates[i]->setValue(value);
        if(i%2)m_points[i/2].setY(value);else m_points[i/2].setX(value);
    }
    if(!m_document->fieldEditable(table->key,m_rowKey,"fld.1.0",error)
       ||!m_document->fieldEditable(table->key,m_rowKey,"fld.2.0",error))return false;
    m_scale=leverApproachScale(m_original.value("fld.3.0").toFloat(),m_complexion.z());
    if(!std::isfinite(m_scale)||m_scale<=0)return fail(QString::fromUtf8("Масштаб подхода должен быть положительным; проверьте масштаб БД и рост рычага."));
    m_count->setCurrentIndex(count==2?1:0);m_active->setEnabled(count==2);
    m_coordinates[2]->setEnabled(count==2);m_coordinates[3]->setEnabled(count==2);
    setWindowTitle(QString::fromUtf8("Точки подхода — %1").arg(m_model));
    m_ready=true;m_view->installEventFilter(this);preview();return true;
}
bool LeverApproachDialog::contextValid() const {
    if(!m_node||!m_view||m_node->isMarkDeleted()||!m_view->hasEditableSelection())return false;
    const auto selection=m_view->selectedNodes();const auto snapshot=ProfileDatabaseContext::instance().current();
    return selection.size()==1&&selection.first()==m_node&&m_node->modelName()==m_model
        &&m_node->position()==m_position&&m_node->drawPosition()==m_origin
        &&m_node->constitution()==m_complexion&&m_node->rotation()==m_rotation
        &&snapshot&&snapshot->profileId==m_profile&&snapshot->sourceArchivePath==m_document->sourceArchivePath();
}
void LeverApproachDialog::preview() {
    if(!m_ready||!m_view)return;
    QVector<QVector3D> lines;
    for(int i=0;i<=m_count->currentIndex();++i){
        const auto p=leverApproachWorld(m_points[i],m_origin,m_rotation,m_scale,float(m_reach->value()));
        lines<<m_origin<<p;
        for(int s=0;s<24;++s){const float a=s*6.2831853f/24,b=(s+1)*6.2831853f/24;
            lines<<p+QVector3D(.3f*std::cos(a),.3f*std::sin(a),.08f)<<p+QVector3D(.3f*std::cos(b),.3f*std::sin(b),.08f);}
        // Digits in the map plane remain identifiable from a top-down camera.
        const auto digit=[&](float x1,float y1,float x2,float y2){lines<<p+QVector3D(x1,y1,.12f)<<p+QVector3D(x2,y2,.12f);};
        if(i==0){digit(0,-.18f,0,.18f);digit(-.1f,.08f,0,.18f);}
        else {digit(-.12f,.18f,.12f,.18f);digit(.12f,.18f,.12f,0);digit(.12f,0,-.12f,0);digit(-.12f,0,-.12f,-.18f);digit(-.12f,-.18f,.12f,-.18f);}
    }
    m_view->setLeverApproachOverlay(lines);
    m_status->setText(QString::fromUtf8("Коэффициент подхода: %1. ЛКМ — поставить, Esc — отменить.\nПосле применения сохраните изменения БД.").arg(m_scale));
}
void LeverApproachDialog::place(const QPoint& position) {
    if(!contextValid()){reject();return;}
    QVector3D world;QVector2D local;
    if(!m_view->terrainPositionAt(position,&world))return;
    if(!leverApproachLocal(QVector2D(world),m_origin,m_rotation,m_scale,float(m_reach->value()),&local)){
        m_status->setText(QString::fromUtf8("Точка слишком близко для этой дистанции персонажа либо поворот не позволяет определить X/Y. Выберите другое место или уменьшите дистанцию предпросмотра."));return;
    }
    const int index=qMin(m_active->currentIndex(),m_count->currentIndex());m_points[index]=local;
    {QSignalBlocker x(m_coordinates[index*2]),y(m_coordinates[index*2+1]);m_coordinates[index*2]->setValue(local.x());m_coordinates[index*2+1]->setValue(local.y());}
    preview();
}
bool LeverApproachDialog::eventFilter(QObject* object,QEvent* event) {
    if(object!=m_view||!m_ready)return false;
    if(event->type()==QEvent::KeyPress||event->type()==QEvent::ShortcutOverride){
        auto* key=static_cast<QKeyEvent*>(event);
        if(event->type()==QEvent::KeyPress&&key->key()==Qt::Key_Escape)reject();
        event->accept();return true;
    }
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonDblClick){
        auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return false;
        if(!contextValid()){reject();return true;}
        bool hit=false;
        for(int i=0;i<=m_count->currentIndex();++i){QPointF screen;
            if(m_view->leverApproachScreenPoint(leverApproachWorld(m_points[i],m_origin,m_rotation,m_scale,float(m_reach->value())),&screen)
               &&QLineF(screen,mouse->pos()).length()<16){m_active->setCurrentIndex(i);hit=true;break;}}
        m_dragging=true;if(!hit)place(mouse->pos());return true;
    }
    if(event->type()==QEvent::MouseMove&&m_dragging){place(static_cast<QMouseEvent*>(event)->pos());return true;}
    if(event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){m_dragging=false;return true;}
    if(event->type()==QEvent::FocusOut)m_dragging=false;
    return false;
}
void LeverApproachDialog::apply() {
    if(!contextValid()){reject();return;}
    const int count=m_count->currentIndex()+1;
    QJsonArray array=QJsonDocument::fromJson(m_original.value("fld.1.0").toString().toUtf8()).array();
    for(int i=0;i<count;++i){array[i*2]=double(m_points[i].x());array[i*2+1]=double(m_points[i].y());}
    const QString points=QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    if(QJsonDocument::fromJson(m_original.value("fld.1.0").toString().toUtf8()).array()==array
       &&m_original.value("fld.2.0").toInt()==count){accept();return;}
    QString error;
    if(!m_document->setLeverApproach(m_rowKey,points,count,m_original,&error)){
        QMessageBox::warning(this,windowTitle(),error);return;
    }
    emit transactionCommitted(m_rowKey);accept();
}
void LeverApproachDialog::done(int result) {
    m_ready=false;if(m_view){m_view->removeEventFilter(this);m_view->setLeverApproachOverlay({});}
    QDialog::done(result);
}
}

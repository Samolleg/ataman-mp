#include "model_connection_editor.h"
#include "resource_catalog.h"
#include "window_geometry.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QQuaternion>
#include <QLineF>
#include <QtMath>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QUuid>
#include <algorithm>

namespace {
class ConnectionCanvas : public QWidget {
public:
    QVector<ei::ModelConnection> points;
    int selected=-1;
    std::function<void()> changed;
    ei::SResourceModelScene scene;
    explicit ConnectionCanvas(QWidget* parent):QWidget(parent) {
        setMinimumSize(360,300);setObjectName("connectionCanvas");
        setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
    }
    void fit() {
        QVector3D lo(1e20f,1e20f,1e20f),hi(-1e20f,-1e20f,-1e20f); bool any=false;
        for(const auto& part:scene.parts) for(const auto& t:part.triangles) for(const auto& v:t.vertices)
            for(int i=0;i<3;++i) { lo[i]=qMin(lo[i],v.position[i]); hi[i]=qMax(hi[i],v.position[i]); any=true; }
        center=any?(lo+hi)*0.5f:QVector3D(); extent=any?qMax(0.1f,(hi-lo).length()):1; update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.fillRect(rect(),palette().base()); painter.setRenderHint(QPainter::Antialiasing);
        updateProjection();
        setProperty("connectionDirectionHandle",QVariant());
        struct Face { QPolygonF polygon; float z; }; QVector<Face> faces;
        const auto visible=ei::defaultVisibleModelParts(scene,model);
        for(const auto& part:scene.parts) if(visible.contains(part.name.toLower())) for(const auto& t:part.triangles) {
            Face f; f.z=0; for(const auto& v:t.vertices) {f.polygon<<project(v.position); f.z+=(matrix*(v.position-center)).z();} faces.append(f);
        }
        std::sort(faces.begin(),faces.end(),[](const Face& a,const Face& b){return a.z<b.z;});
        painter.setPen(QPen(QColor(115,130,146),0.5)); painter.setBrush(QColor(77,88,99));
        for(const auto& f:faces) painter.drawPolygon(f.polygon);
        for(int i=0;i<points.size();++i) {
            const auto& p=points[i]; const auto xy=project(p.position);
            painter.setPen(QPen(i==selected?QColor(255,205,60):QColor(80,210,235),2));
            painter.setBrush(i==selected?QColor(255,205,60):QColor(80,210,235)); painter.drawEllipse(xy,6,6);
            const auto tip=project(p.position+p.normal.normalized()*extent*0.15f);
            painter.drawLine(xy,tip);
            if(i==selected) {
                // A visible handle separates rotating the frame from moving its origin.
                painter.drawEllipse(tip,5,5);
                setProperty("connectionDirectionHandle",tip);
            }
            painter.setPen(QPen(QColor(110,230,140),2)); painter.drawLine(xy,project(p.position+p.up.normalized()*extent*0.10f));
            painter.setPen(palette().text().color()); painter.drawText(xy+QPointF(9,-9),p.label.isEmpty()?p.id:p.label);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        updateProjection();last=e->pos();dragging=e->button();
        if(e->button()!=Qt::LeftButton || selected<0 || selected>=points.size())return;
        setFocus(Qt::MouseFocusReason);
        dragIndex=selected;beforeDrag=points[selected];dragStart=e->localPos();
        rotating=e->modifiers().testFlag(Qt::AltModifier) || directionHit(e->localPos());
        freeRotation=e->modifiers().testFlag(Qt::AltModifier);
        if(rotating) {
            dragCenter=project(beforeDrag.position);
            dragRadius=qMax(20.f,extent*0.15f*scale);
            depthSign=matrix.mapVector(beforeDrag.normal).z()<0 ? -1.f : 1.f;
            dragVector=trackball(dragStart);
            dragView=QQuaternion::fromRotationMatrix(matrix.normalMatrix());
            setCursor(Qt::ClosedHandCursor);
        } else place(e->pos());
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        const auto delta=e->pos()-last; last=e->pos();
        if(dragging==Qt::RightButton || dragging==Qt::MiddleButton) {yaw+=delta.x()*0.5f; pitch+=delta.y()*0.5f;update();}
        else if(dragging==Qt::LeftButton && selected>=0) {
            if(rotating) rotateDirection(e->localPos());else place(e->pos());
        } else {updateProjection();setCursor(directionHit(e->localPos())?Qt::OpenHandCursor:Qt::ArrowCursor);}
    }
    void mouseReleaseEvent(QMouseEvent*) override {dragging=Qt::NoButton;rotating=false;dragIndex=-1;unsetCursor();}
    void keyPressEvent(QKeyEvent* e) override {
        if(e->key()==Qt::Key_Escape && dragging==Qt::LeftButton && dragIndex>=0) {
            if(dragIndex<points.size())points[dragIndex]=beforeDrag;
            dragging=Qt::NoButton;rotating=false;dragIndex=-1;unsetCursor();
            update();if(changed)changed();e->accept();return;
        }
        QWidget::keyPressEvent(e);
    }
    void wheelEvent(QWheelEvent* e) override {
        if(dragging!=Qt::NoButton){e->accept();return;}
        zoom=qBound(0.2f,zoom*(e->angleDelta().y()>0?1.12f:0.89f),8.0f);update();
    }
public:
    QString model;
private:
    void updateProjection() {
        matrix.setToIdentity();matrix.rotate(pitch,1,0,0);matrix.rotate(yaw,0,0,1);
        scale=qMin(width(),height())*zoom/extent;
    }
    bool directionHit(const QPointF& mouse) const {
        if(selected<0 || selected>=points.size())return false;
        const auto& p=points[selected];const auto origin=project(p.position);
        const auto tip=project(p.position+p.normal.normalized()*extent*0.15f);
        const auto segment=tip-origin;const double length2=segment.x()*segment.x()+segment.y()*segment.y();
        // A foreshortened handle overlaps the origin: leave it movable, use Alt.
        if(length2<18*18 || QLineF(mouse,origin).length()<14)return false;
        if(QLineF(mouse,tip).length()<=11)return true;
        const auto relative=mouse-origin;
        const double t=(relative.x()*segment.x()+relative.y()*segment.y())/length2;
        return t>0 && t<1 && QLineF(mouse,origin+segment*t).length()<=7;
    }
    QVector3D trackball(const QPointF& mouse) const {
        const auto delta=(mouse-dragCenter)/dragRadius;
        QVector3D v(delta.x(),-delta.y(),0);const float radius2=v.lengthSquared();
        if(radius2>1)return v.normalized();
        v.setZ(depthSign*qSqrt(qMax(0.f,1-radius2)));return v.normalized();
    }
    void rotateDirection(const QPointF& mouse) {
        if(dragIndex!=selected || selected<0 || selected>=points.size())return;
        QQuaternion viewRotation;
        if(freeRotation) {
            const auto delta=mouse-dragStart;
            viewRotation=QQuaternion::fromAxisAndAngle(0,1,0,delta.x()*0.6f)
                *QQuaternion::fromAxisAndAngle(1,0,0,delta.y()*0.6f);
        } else viewRotation=QQuaternion::rotationTo(dragVector,trackball(mouse));
        const auto rotation=(dragView.conjugated()*viewRotation*dragView).normalized();
        // Rotate the full connector frame to retain a valid up direction when tilted.
        points[selected].normal=rotation.rotatedVector(beforeDrag.normal);
        points[selected].up=rotation.rotatedVector(beforeDrag.up);
        update();if(changed)changed();
    }
    QPointF project(const QVector3D& p) const {const auto v=matrix*(p-center); return QPointF(width()*0.5+v.x()*scale,height()*0.5-v.y()*scale);}
    void place(const QPointF& mouse) {
        float best=-1e30f; QVector3D hit; bool found=false;
        const auto visible=ei::defaultVisibleModelParts(scene,model);
        for(const auto& part:scene.parts) if(visible.contains(part.name.toLower())) for(const auto& t:part.triangles) {
            const auto a=project(t.vertices[0].position),b=project(t.vertices[1].position),c=project(t.vertices[2].position);
            const double d=(b.y()-c.y())*(a.x()-c.x())+(c.x()-b.x())*(a.y()-c.y()); if(qAbs(d)<1e-6) continue;
            const float u=((b.y()-c.y())*(mouse.x()-c.x())+(c.x()-b.x())*(mouse.y()-c.y()))/d;
            const float v=((c.y()-a.y())*(mouse.x()-c.x())+(a.x()-c.x())*(mouse.y()-c.y()))/d;
            if(u<0 || v<0 || u+v>1) continue;
            const auto p=u*t.vertices[0].position+v*t.vertices[1].position+(1-u-v)*t.vertices[2].position;
            const float z=(matrix*(p-center)).z(); if(z>best) {best=z;hit=p;found=true;}
        }
        if(found && selected<points.size()) {points[selected].position=hit;update();if(changed)changed();}
    }
    QMatrix4x4 matrix;
    QVector3D center;
    float extent=1,scale=1,zoom=0.85f,pitch=-62,yaw=-40;
    QPoint last; Qt::MouseButton dragging=Qt::NoButton;
    bool rotating=false,freeRotation=false;
    int dragIndex=-1;
    ei::ModelConnection beforeDrag;
    QPointF dragStart,dragCenter;
    float dragRadius=20,depthSign=1;
    QVector3D dragVector;
    QQuaternion dragView;
};
}
ModelSceneProvider CModelConnectionEditor::resourceScenes(const QStringList& paths) {
    return [paths](const QString& name) {
        CResourceCatalog resources; resources.build(paths.toVector());
        auto read=[&](const QString& logical) {const auto entries=resources.providers(logical); if(entries.isEmpty() || entries.first().size>64*1024*1024) return QByteArray();
            const auto* archive=resources.archive(entries.first().archivePriority); return archive?archive->readMember(entries.first().memberIndex):QByteArray();};
        const auto stem=ei::TechnicalModelCatalog::modelKey(name); const auto mod=read(stem+".mod");
        return mod.isEmpty()?ei::buildFig8Scene(read(stem+".fig")):ei::buildModScene(mod,read(stem+".bon"));
    };
}
CModelConnectionEditor::CModelConnectionEditor(ei::TechnicalModelCatalog* catalog,const QString& model,ModelSceneProvider provider,QWidget* parent):QDialog(parent) {
    setObjectName("modelConnectionEditor"); setWindowTitle(QString::fromUtf8("Точки стыковки — ")+model); resize(1000,680);
    ei::rememberWindowGeometry(this,"modelConnectionEditor");
    auto* root=new QVBoxLayout(this); auto* row=new QHBoxLayout;root->addLayout(row,1);
    auto* canvas=new ConnectionCanvas(this);canvas->model=model;row->addWidget(canvas,1);
    auto* form=new QFormLayout;row->addLayout(form);
    auto* list=new QListWidget(this);list->setObjectName("connectionList");form->addRow(list);
    QString error;const bool valid=catalog->connections(model,&canvas->points,&error);
    auto* label=new QLineEdit(this);label->setObjectName("connectionLabel");form->addRow(QString::fromUtf8("Название"),label);
    auto* type=new QComboBox(this);type->setEditable(true);type->addItems({"wall","floor","roof"});type->setObjectName("connectionType");form->addRow(QString::fromUtf8("Тип соединения"),type);
    QVector<QDoubleSpinBox*> values;
    for(const auto& title:QStringList{QString::fromUtf8("Положение"),QString::fromUtf8("Направление"),QString::fromUtf8("Верх")}) {
        auto* line=new QHBoxLayout;for(const auto& axis:QStringList{"X","Y","Z"}) {line->addWidget(new QLabel(axis,this));auto* spin=new QDoubleSpinBox(this);spin->setRange(-100000,100000);spin->setDecimals(4);spin->setSingleStep(0.1);line->addWidget(spin);values.append(spin);}form->addRow(title,line);
    }
    auto* actions=new QHBoxLayout;auto* add=new QPushButton(QString::fromUtf8("Добавить"),this);add->setObjectName("connectionAdd");auto* remove=new QPushButton(QString::fromUtf8("Удалить"),this);actions->addWidget(add);actions->addWidget(remove);form->addRow(actions);
    auto* status=new QLabel(error,this);status->setWordWrap(true);root->addWidget(status);
    auto* help=new QLabel(QString::fromUtf8("ЛКМ за жёлтый конец или линию — повернуть направление. Alt+ЛКМ в любом месте превью — повернуть, даже если стрелка смотрит в камеру. Esc во время жеста — отменить жест. ЛКМ по модели — передвинуть стык. ПКМ — вращать вид; колесо — масштаб. Зелёная стрелка поворачивается вместе с направлением и задаёт верх детали. Направление смотрит наружу; одинаковые типы соединяются друг с другом. Координаты заданы относительно начала модели."),this);help->setWordWrap(true);root->addWidget(help);
    auto* buttons=new QHBoxLayout;buttons->addStretch();auto* save=new QPushButton(QString::fromUtf8("Сохранить стыки"),this);save->setObjectName("connectionSave");save->setEnabled(valid && catalog->writable());auto* cancel=new QPushButton(QString::fromUtf8("Отмена"),this);buttons->addWidget(save);buttons->addWidget(cancel);root->addLayout(buttons);
    auto loading=QSharedPointer<bool>::create(false);
    auto select=[=] { *loading=true;canvas->selected=list->currentRow();const int i=canvas->selected;const bool exists=i>=0 && i<canvas->points.size();label->setEnabled(exists);type->setEnabled(exists);remove->setEnabled(exists);
        for(auto* spin:values) spin->setEnabled(exists);
        if(exists) {const auto& p=canvas->points[i];label->setText(p.label);type->setCurrentText(p.type);for(int k=0;k<3;++k){values[k]->setValue(p.position[k]);values[k+3]->setValue(p.normal[k]);values[k+6]->setValue(p.up[k]);}}
        *loading=false;canvas->update();};
    auto rebuild=[=] {const int selected=canvas->selected;list->clear();for(const auto& p:canvas->points)list->addItem(p.label.isEmpty()?p.id:p.label);list->setCurrentRow(canvas->points.isEmpty()?-1:qBound(0,selected,canvas->points.size()-1));select();};
    auto update=[=] {if(*loading || canvas->selected<0 || canvas->selected>=canvas->points.size())return;auto& p=canvas->points[canvas->selected];p.label=label->text();p.type=type->currentText().trimmed();for(int k=0;k<3;++k){p.position[k]=values[k]->value();p.normal[k]=values[k+3]->value();p.up[k]=values[k+6]->value();}list->item(canvas->selected)->setText(p.label.isEmpty()?p.id:p.label);canvas->update();};
    connect(list,&QListWidget::currentRowChanged,this,[=](int){select();});
    connect(label,&QLineEdit::textChanged,this,[=]{update();});connect(type,&QComboBox::currentTextChanged,this,[=]{update();});
    for(auto* spin:values) connect(spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[=](double){update();});
    canvas->changed=select;
    connect(add,&QPushButton::clicked,this,[=]{ei::ModelConnection p;p.id=QUuid::createUuid().toString(QUuid::WithoutBraces);p.label=QString::fromUtf8("Стык %1").arg(canvas->points.size()+1);p.type=type->currentText().trimmed();if(p.type.isEmpty())p.type="wall";canvas->points.append(p);canvas->selected=canvas->points.size()-1;rebuild();});
    connect(remove,&QPushButton::clicked,this,[=]{if(canvas->selected>=0)canvas->points.removeAt(canvas->selected);rebuild();});
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(save,&QPushButton::clicked,this,[=]{ei::TechnicalModelCatalog next=*catalog;QString message;if(!next.setConnections(model,canvas->points,&message)||!next.save(&message)){status->setText(message);return;}*catalog=next;accept();});
    rebuild();add->setEnabled(valid && catalog->writable());
    if(provider) {
        if(valid)status->setText(QString::fromUtf8("Загрузка геометрии модели…"));
        auto* job=new QFutureWatcher<ei::SResourceModelScene>(this);
        connect(job,&QFutureWatcher<ei::SResourceModelScene>::finished,this,[=]{canvas->scene=job->result();canvas->fit();if(!canvas->scene.valid)status->setText(QString::fromUtf8("Не удалось загрузить геометрию. Точки можно задать координатами."));else if(valid)status->clear();job->deleteLater();});
        job->setFuture(QtConcurrent::run([provider,model]{return provider(model);}));
        // The worker owns only value data; it never references this dialog.
    } else status->setText(QString::fromUtf8("Превью недоступно; точки можно задать координатами."));
}

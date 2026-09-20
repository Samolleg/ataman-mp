#include "travel_global_map.h"
#include "resource_model_preview.h"
#include "resourcemanager.h"
#include "mmp_inspector.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <functional>
#include <limits>

bool travelGlobalPosition(const SMapTableZone& zone,QVector3D* position)
{
    const auto m=QRegularExpression(QStringLiteral(R"((?:^|\n)[ \t]*#position[^\r\n]*\r?\n[ \t]*(\S+)[ \t]+(\S+)[ \t]+(\S+))")).match(zone.rawBlock);
    if(!m.hasMatch())return false;
    QVector3D value;
    for(int i=0;i<3;++i){bool ok=false;value[i]=m.captured(i+1).toFloat(&ok);if(!ok||!std::isfinite(value[i]))return false;}
    *position=value;return true;
}

namespace {
class GlobalSurface final:public QWidget {
public:
    QVector<ei::SResourceModelTriangle> triangles;
    QImage texture, raster;
    QVector<float> heights;
    QMap<QString,QVector3D> markers;
    QVector3D selected;
    bool hasSelection{false};
    std::function<void()> changed;
    QRectF bounds;
    GlobalSurface(QWidget* parent):QWidget(parent){setMinimumSize(640,420);setMouseTracking(true);setObjectName("travelGlobalSurface");}
    QRectF canvas() const {
        if(bounds.isEmpty())return QRectF();
        const double scale=qMin(width()/bounds.width(),height()/bounds.height());
        const QSizeF extent(bounds.width()*scale,bounds.height()*scale);
        return QRectF(QPointF((width()-extent.width())*.5,(height()-extent.height())*.5),extent);
    }
    QPointF screen(const QVector3D& v) const {const auto area=canvas();return QPointF(area.left()+(v.x()-bounds.left())/bounds.width()*area.width(),area.top()+(bounds.bottom()-v.y())/bounds.height()*area.height());}
    void rebuild() {
        raster=QImage(size(),QImage::Format_RGB32);raster.fill(QColor(24,27,32));
        heights.fill(-std::numeric_limits<float>::infinity(),width()*height());
        if(texture.isNull()||bounds.isEmpty())return;
        for(const auto& triangle:triangles) {
            const auto& a=triangle.vertices[0];const auto& b=triangle.vertices[1];const auto& c=triangle.vertices[2];
            const auto p=screen(a.position),q=screen(b.position),r=screen(c.position);
            const double den=(q.y()-r.y())*(p.x()-r.x())+(r.x()-q.x())*(p.y()-r.y());
            if(!std::isfinite(den)||std::abs(den)<1e-9)continue;
            const int x0=qMax(0,int(std::floor(qMin(p.x(),qMin(q.x(),r.x()))))),x1=qMin(width()-1,int(std::ceil(qMax(p.x(),qMax(q.x(),r.x())))));
            const int y0=qMax(0,int(std::floor(qMin(p.y(),qMin(q.y(),r.y()))))),y1=qMin(height()-1,int(std::ceil(qMax(p.y(),qMax(q.y(),r.y())))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
                const double u=((q.y()-r.y())*(x+.5-r.x())+(r.x()-q.x())*(y+.5-r.y()))/den;
                const double v=((r.y()-p.y())*(x+.5-r.x())+(p.x()-r.x())*(y+.5-r.y()))/den,w=1-u-v;
                if(u<0||v<0||w<0)continue;
                const float z=u*a.position.z()+v*b.position.z()+w*c.position.z();const int offset=y*width()+x;
                if(z<=heights[offset])continue;
                heights[offset]=z;
                const auto uv=a.uv*u+b.uv*v+c.uv*w;
                const int tx=qBound(0,int(uv.x()*texture.width()),texture.width()-1),ty=qBound(0,int(uv.y()*texture.height()),texture.height()-1);
                reinterpret_cast<QRgb*>(raster.scanLine(y))[x]=texture.pixel(tx,ty);
            }
        }
    }
    void pick(QPoint p) {
        if(!rect().contains(p)||heights.size()!=width()*height())return;
        const float z=heights[p.y()*width()+p.x()];if(!std::isfinite(z))return;
        const auto area=canvas();
        selected=QVector3D(bounds.left()+(p.x()+.5-area.left())*bounds.width()/area.width(),bounds.bottom()-(p.y()+.5-area.top())*bounds.height()/area.height(),z);
        hasSelection=true;if(changed)changed();update();
    }
protected:
    void resizeEvent(QResizeEvent*) override {rebuild();}
    void mousePressEvent(QMouseEvent* e) override {if(e->button()==Qt::LeftButton)pick(e->pos());}
    void mouseMoveEvent(QMouseEvent* e) override {
        if(e->buttons()&Qt::LeftButton)pick(e->pos());
        QString tip;for(auto i=markers.cbegin();i!=markers.cend();++i)if(QLineF(screen(i.value()),e->localPos()).length()<10)tip+=i.key()+"\n";
        setToolTip(tip.trimmed());
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.drawImage(0,0,raster);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::black,1));p.setBrush(QColor(245,192,64));
        for(const auto& marker:markers)p.drawEllipse(screen(marker),4,4);
        if(hasSelection){const auto at=screen(selected);p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::cyan,2));p.drawEllipse(at,9,9);p.drawLine(at-QPointF(14,0),at+QPointF(14,0));p.drawLine(at-QPointF(0,14),at+QPointF(0,14));}
    }
};
}

bool chooseTravelGlobalPosition(QWidget* parent,const QVector<SMapTableZone>& zones,
    const QString& allod,const QString& selectedZone,QVector3D* position,bool hasPosition,QString* error)
{
    const auto fail=[error](const QString& text){if(error)*error=text;return false;};
    // Original allods have fixed global texture names; custom allods require an
    // explicit supported texture binding, never a guessed coordinate scale.
    const QMap<QString,QString> textures={{"gipat","gipatmap.mmp"},{"ingos","ingosmap.mmp"},{"suslanger","suslangermap.mmp"}};
    const auto textureName=textures.value(allod.toLower());
    if(textureName.isEmpty())return fail(QObject::tr("Для аллода %1 не задана текстура глобальной карты.").arg(allod));
    QDialog dialog(parent);dialog.setObjectName("travelGlobalDialog");dialog.setWindowTitle(QObject::tr("Метка маршрута — %1").arg(allod));
    dialog.setAttribute(Qt::WA_DontShowOnScreen,parent->testAttribute(Qt::WA_DontShowOnScreen));
    QVBoxLayout layout(&dialog);QLabel help(QObject::tr("Вид сверху. Нажмите на поверхность или перетащите метку ЛКМ. Высота определяется по модели. Золотые точки — существующие маршруты (название при наведении)."));help.setWordWrap(true);layout.addWidget(&help);
    GlobalSurface surface(&dialog);layout.addWidget(&surface,1);
    surface.texture=ei::inspectMmp(CObjectList::getInstance()->resourceData(textureName)).baseImage;
    if(surface.texture.isNull())return fail(QObject::tr("Не удалось прочитать %1 из ресурсов профиля.").arg(textureName));
    QSet<QString> figures;
    const QRegularExpression figure(QStringLiteral(R"((?:^|\n)[ \t]*#figure[^\r\n]*\r?\n[ \t]*(\S+))"));
    for(const auto& zone:zones)if(zone.allodId.compare(allod,Qt::CaseInsensitive)==0){
        QVector3D at;if(zone.zoneType=="edge"&&travelGlobalPosition(zone,&at)&&zone.zoneId!=selectedZone)surface.markers.insert(zone.zoneId,at);
        if(zone.zoneType!="game")continue;
        const auto match=figure.match(zone.rawBlock);if(!match.hasMatch())continue;
        QString name=match.captured(1);if(!name.endsWith(".fig",Qt::CaseInsensitive))name+=".fig";
        if(figures.contains(name.toLower()))continue;figures.insert(name.toLower());
        const auto scene=ei::buildFig8Scene(CObjectList::getInstance()->resourceData(name));
        if(!scene.valid)return fail(QObject::tr("Не удалось прочитать глобальную модель %1. Выбор отменён.").arg(name));
        for(const auto& part:scene.parts)for(const auto& triangle:part.triangles){
            for(const auto& vertex:triangle.vertices)for(int axis=0;axis<3;++axis)
                if(!std::isfinite(vertex.uv.x())||!std::isfinite(vertex.uv.y())||std::abs(vertex.uv.x())>10000||std::abs(vertex.uv.y())>10000||!std::isfinite(vertex.position[axis])||std::abs(vertex.position[axis])>100000)return fail(QObject::tr("Неверная геометрия глобальной карты."));
            surface.triangles.append(triangle);
        }
    }
    if(surface.triangles.isEmpty())return fail(QObject::tr("В map.txt нет доступных моделей поверхности этого аллода."));
    double minX=1e20,minY=1e20,maxX=-1e20,maxY=-1e20;
    for(const auto& t:surface.triangles)for(const auto& v:t.vertices){minX=qMin(minX,double(v.position.x()));maxX=qMax(maxX,double(v.position.x()));minY=qMin(minY,double(v.position.y()));maxY=qMax(maxY,double(v.position.y()));}
    if(maxX-minX<1e-6||maxY-minY<1e-6)return fail(QObject::tr("Пустая поверхность глобальной карты."));
    const double margin=qMax(maxX-minX,maxY-minY)*.04;
    surface.bounds=QRectF(QPointF(minX-margin,minY-margin),QPointF(maxX+margin,maxY+margin));
    surface.selected=*position;surface.hasSelection=hasPosition;
    QLabel coordinates;layout.addWidget(&coordinates);QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText(QObject::tr("Выбрать точку"));buttons.button(QDialogButtonBox::Cancel)->setText(QObject::tr("Отмена"));layout.addWidget(&buttons);
    surface.changed=[&](){coordinates.setText(QObject::tr("X: %1   Y: %2   Z: %3").arg(surface.selected.x(),0,'f',5).arg(surface.selected.y(),0,'f',5).arg(surface.selected.z(),0,'f',5));buttons.button(QDialogButtonBox::Ok)->setEnabled(surface.hasSelection);};surface.changed();
    QObject::connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    dialog.resize(940,730);
    if(dialog.exec()!=QDialog::Accepted)return false;
    *position=surface.selected;return true;
}

// CPU-only regression: sloped surface interpolation, nearest layer and empty space.
bool travelGlobalSurfaceCheck()
{
    GlobalSurface surface(nullptr);surface.resize(640,420);
    surface.texture=QImage(2,2,QImage::Format_RGB32);surface.texture.fill(Qt::green);
    surface.bounds=QRectF(0,0,1,1);
    ei::SResourceModelTriangle triangle;
    triangle.vertices[0].position=QVector3D(0,0,1);
    triangle.vertices[1].position=QVector3D(1,0,2);
    triangle.vertices[2].position=QVector3D(0,1,3);
    surface.triangles.append(triangle);surface.rebuild();
    surface.pick(QPoint(200,300));
    if(!surface.hasSelection||std::abs(surface.selected.z()-(1+surface.selected.x()+2*surface.selected.y()))>1e-5)return false;
    const auto before=surface.selected;surface.pick(QPoint(639,0));
    if(surface.selected!=before)return false;
    for(auto& v:triangle.vertices)v.position.setZ(v.position.z()+5);
    surface.triangles.append(triangle);surface.rebuild();surface.pick(QPoint(200,300));
    return std::abs(surface.selected.z()-before.z()-5)<1e-5;
}

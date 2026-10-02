#include "model_catalog_icons.h"
#include <QPainter>
#include <QtMath>
namespace ei {
// Small vector symbols keep tabs recognizable at any display scale and do
// not depend on a theme icon pack being installed on the user's computer.
QIcon modelCatalogCategoryIcon(int category)
{
    QPixmap image(64,64); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(38,45,53),3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    switch(category)
    {
    case 0: // house
        p.setBrush(QColor(233,195,125)); p.drawRect(12,28,40,28);
        p.setBrush(QColor(199,95,66)); p.drawPolygon(QPolygonF(QVector<QPointF>{QPointF(6,29),QPointF(32,7),QPointF(58,29)}));
        p.setBrush(QColor(105,76,58)); p.drawRect(27,39,11,17); break;
    case 1: // architecture: columns and lintel
        p.setBrush(QColor(201,212,225)); p.drawRect(7,13,50,8); p.drawRect(9,51,46,7);
        for(int x:{14,29,44}) {p.drawRect(x,21,6,30);} break;
    case 2: // gates
        p.setBrush(QColor(167,185,206)); p.drawRect(7,11,9,47); p.drawRect(48,11,9,47);
        p.setBrush(QColor(207,153,79)); p.drawRect(17,25,30,30);
        for(int x:{24,32,40}) p.drawLine(x,26,x,54);
        p.drawLine(18,38,46,38); break;
    case 3: // utensils: jar and bowl
        p.setBrush(QColor(205,141,89)); p.drawRoundedRect(10,21,25,33,9,9); p.drawRect(15,12,15,10);
        p.setBrush(QColor(237,202,142)); p.drawEllipse(34,37,24,17); p.drawLine(35,40,57,40); break;
    case 4: // tree
        p.setBrush(QColor(151,104,69)); p.drawRect(27,31,10,27);
        p.setBrush(QColor(88,167,107)); p.drawEllipse(12,17,24,24); p.drawEllipse(29,17,24,24); p.drawEllipse(21,5,23,25); break;
    case 5: // bush
        p.setBrush(QColor(111,180,100)); p.drawEllipse(7,32,23,22); p.drawEllipse(34,30,24,24); p.drawEllipse(19,20,27,32);
        p.setPen(QPen(QColor(229,197,104),4)); p.drawPoint(23,38); p.drawPoint(40,41); break;
    case 6: // chest and lever
        p.setBrush(QColor(196,135,69)); p.drawRoundedRect(6,28,36,28,4,4); p.drawLine(7,38,41,38);
        p.setBrush(QColor(240,203,94)); p.drawRect(21,36,7,12);
        p.setPen(QPen(QColor(157,185,214),5)); p.drawLine(48,51,57,18);
        p.setBrush(QColor(209,97,74)); p.drawEllipse(51,10,11,11); break;
    default: // decorative star
        QPolygonF star;
        for(int i=0;i<10;++i) {const double a=qDegreesToRadians(double(i*36-90));const double r=i%2?12:26;star<<QPointF(32+qCos(a)*r,32+qSin(a)*r);}
        p.setBrush(QColor(197,152,224)); p.drawPolygon(star); break;
    }
    return QIcon(image);
}
}

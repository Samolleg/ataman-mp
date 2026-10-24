#ifndef NATURAL_TERRAIN_H
#define NATURAL_TERRAIN_H
#include <QJsonObject>
#include <QPointF>
#include <QVector>
#include <functional>

// Detached, metre-spaced working grid. No game resources or GUI state.
namespace natural {
struct Grid {
    int width{0},height{0};
    QPointF origin;
    QVector<double> heights,wetness;
    double sample(double x,double y) const;
};
QJsonObject preset(const QString& name);
double noise(double x,double y,quint32 seed);
double fbm(double x,double y,quint32 seed,int octaves=5);
void generate(Grid& grid,const QString& name,const QJsonObject& parameters,quint32 seed,
              const std::function<bool()>& cancelled);
void erode(Grid& grid,const QJsonObject& parameters,const std::function<bool()>& cancelled);
void river(Grid& grid,const QJsonObject& args,quint32 seed,const std::function<bool()>& cancelled);
QJsonObject metrics(const Grid& grid);
QVector<double> blurred(const QVector<double>& field,int width,int height,int radius);
void coherentMask(QVector<quint8>& mask,int width,int height,int radius,int minimumArea,
                  bool fillHoles,const std::function<bool()>& cancelled);
QJsonObject shapeMetrics(const Grid& grid);
QJsonObject maskMetrics(const QVector<quint8>& mask,int width,int height,double spacing);
}
#endif

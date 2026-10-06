#ifndef WORLD_MAP_SNAP_H
#define WORLD_MAP_SNAP_H

#include <QLineF>
#include <QRectF>
#include <QString>
#include <QVector>

struct SWorldMapSnapBounds
{
    QString id;
    QRectF rect;
};

struct SWorldMapSnapResult
{
    QPointF origin;
    QVector<QLineF> seams;
    QString latch;
    bool valid{false};
    bool overlaps{false};
    bool incompatibleEdge{false};
};

class CWorldMapSnap
{
public:
    static double tileStep() { return 2.0; }
    static QPointF align(const QPointF& requested, const QPointF& anchor);
    static bool onGrid(const QPointF& origin, const QPointF& anchor);
    static SWorldMapSnapResult place(const QPointF& requested, const QSizeF& size,
        const QPointF& anchor, bool hasAnchor, bool enabled,
        const QVector<SWorldMapSnapBounds>& neighbors, double radius = 2.0,
        const QString& previousLatch = QString());
};

#endif

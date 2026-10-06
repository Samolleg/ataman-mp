#include "world_map_snap.h"
#include <cmath>
#include <limits>

namespace {
constexpr double epsilon = 1e-7;
bool finite(const QPointF& p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
bool overlaps(const QRectF& a, const QRectF& b)
{
    return qMin(a.right(), b.right()) - qMax(a.left(), b.left()) > epsilon
        && qMin(a.bottom(), b.bottom()) - qMax(a.top(), b.top()) > epsilon;
}
QVector<QLineF> seams(const QRectF& a, const QRectF& b)
{
    QVector<QLineF> result;
    const double loY = qMax(a.top(), b.top()), hiY = qMin(a.bottom(), b.bottom());
    const double loX = qMax(a.left(), b.left()), hiX = qMin(a.right(), b.right());
    if(hiY >= loY - epsilon)
        for(const auto edge : {qMakePair(a.right(), b.left()), qMakePair(a.left(), b.right())})
            if(std::abs(edge.first - edge.second) < epsilon)
                result.append(QLineF(edge.first, loY, edge.first, qMax(loY, hiY)));
    if(hiX >= loX - epsilon)
        for(const auto edge : {qMakePair(a.bottom(), b.top()), qMakePair(a.top(), b.bottom())})
            if(std::abs(edge.first - edge.second) < epsilon)
                result.append(QLineF(loX, edge.first, qMax(loX, hiX), edge.first));
    return result;
}
}

QPointF CWorldMapSnap::align(const QPointF& requested, const QPointF& anchor)
{
    // Ties always choose the greater grid coordinate, also below zero. Unlike
    // absolute rounding, this is invariant under changing the active map.
    const auto axis = [](double p, double a) {
        return a + 2.0 * std::floor((p - a) / 2.0 + 0.5);
    };
    return QPointF(axis(requested.x(), anchor.x()), axis(requested.y(), anchor.y()));
}

bool CWorldMapSnap::onGrid(const QPointF& origin, const QPointF& anchor)
{
    return finite(origin) && finite(anchor) && QLineF(origin, align(origin, anchor)).length() < epsilon;
}

SWorldMapSnapResult CWorldMapSnap::place(const QPointF& requested, const QSizeF& size,
    const QPointF& anchor, bool hasAnchor, bool enabled,
    const QVector<SWorldMapSnapBounds>& neighbors, double radius, const QString& previousLatch)
{
    SWorldMapSnapResult result;
    if(!finite(requested) || !std::isfinite(size.width()) || !std::isfinite(size.height())
       || size.width() <= 0 || size.height() <= 0
       || (enabled && (!hasAnchor || !finite(anchor)))) return result;
    result.origin = enabled ? align(requested, anchor) : requested;
    if(!finite(result.origin)) return result;
    result.valid = true;
    radius = std::isfinite(radius) ? qMax(0.0, radius) : 0.0;
    double best = std::numeric_limits<double>::max();
    QString bestKey;
    const auto consider = [&](QPointF p, const SWorldMapSnapBounds& peer, const QString& suffix) {
        const QString latch = peer.id + QLatin1Char('/') + suffix;
        const double tolerance = radius * (latch == previousLatch ? 1.4 : 1.0);
        const double dx = std::abs(p.x() - requested.x()), dy = std::abs(p.y() - requested.y());
        if(dx > tolerance || dy > tolerance || !onGrid(p, anchor)) return;
        const QRectF candidate(p, size);
        if(seams(candidate, peer.rect).isEmpty()) return; // no distant edge extensions
        for(const auto& other : neighbors)
            if(overlaps(candidate, other.rect)) return;
        // Prefer the held compatible seam until the pointer leaves its radius.
        const double score = dx * dx + dy * dy - (latch == previousLatch ? radius * radius : 0.0);
        if(score < best - epsilon || (std::abs(score - best) < epsilon && latch < bestKey))
        { best = score; bestKey = latch; result.origin = p; result.latch = latch; }
    };
    if(enabled)
        for(const auto& peer : neighbors)
        {
            const QRectF r = peer.rect;
            if(!r.isValid() || !finite(r.topLeft()) || !finite(r.bottomRight())) continue;
            const QPointF base = align(requested, anchor);
            const double xs[] = {r.left() - size.width(), r.right()};
            const double ys[] = {r.top() - size.height(), r.bottom()};
            if(!onGrid(r.topLeft(), anchor) || !onGrid(r.bottomRight(), anchor))
            {
                for(double x : xs)
                    if(std::abs(x-requested.x()) <= radius
                       && qMin(requested.y()+size.height(),r.bottom()) >= qMax(requested.y(),r.top()))
                        result.incompatibleEdge = true;
                for(double y : ys)
                    if(std::abs(y-requested.y()) <= radius
                       && qMin(requested.x()+size.width(),r.right()) >= qMax(requested.x(),r.left()))
                        result.incompatibleEdge = true;
                continue;
            }
            for(int x=0; x<2; ++x)
                consider(QPointF(xs[x],base.y()),peer,QStringLiteral("x%1").arg(x));
            for(int y=0; y<2; ++y)
                consider(QPointF(base.x(),ys[y]),peer,QStringLiteral("y%1").arg(y));
            for(int x=0; x<2; ++x) for(int y=0; y<2; ++y)
                consider(QPointF(xs[x],ys[y]),peer,QStringLiteral("c%1%2").arg(x).arg(y));
        }
    const QRectF placed(result.origin, size);
    for(const auto& peer : neighbors)
    {
        result.overlaps |= overlaps(placed,peer.rect);
        if(enabled && onGrid(peer.rect.topLeft(),anchor) && onGrid(peer.rect.bottomRight(),anchor))
            result.seams += seams(placed,peer.rect);
    }
    return result;
}

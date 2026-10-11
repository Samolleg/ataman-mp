#ifndef TERRAIN_HD_VARIANTS_H
#define TERRAIN_HD_VARIANTS_H

#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QVector>
#include <functional>

// Algorithm A: local four-sided minimum-error quilting; never synthesizes a
// material boundary. Changing any pixel/seed/acceptance rule requires a new ID.
namespace TerrainHdVariants {
const int AlgorithmVersion = 2;
const int Rim = 24;
const int TargetCount = 6;
const int MaxAttemptsPerVariant = 128;
struct Source {
    QString id;
    QImage pixels;
    // Seven native BC1 tile levels (256..4), in image/block row order.
    // Empty is permitted for synthetic tests, never for resource-bank input.
    QVector<QByteArray> blocks;
};
struct Variant {
    QString id, anchor;
    quint32 seed{0};
    QImage pixels;
    QVector<QByteArray> blocks;
    QJsonObject metadata;
};
struct Result {
    bool success{false}, cancelled{false};
    QString error;
    bool shortfall{false};
    QString shortfallReason;
    int originalCount{0}, rejected{0};
    QVector<Variant> variants;
    QJsonArray rejections;
};
QByteArray canonicalD4(const QImage& image);
QImage boxHalf(const QImage& image);
QImage quilt(const QVector<Source>& sources, int anchor, quint32 seed,
             QJsonArray* trace, const std::function<bool()>& cancelled = {});
QJsonObject measure(const QImage& image, const QImage& anchor,
                    const QVector<QImage>& prior);
QStringList rejectReasons(const QJsonObject& metrics);
// key includes material identity, reviewed class, physical MP class and layer.
// progress returns false to cancel; no filesystem, RNG globals or GUI access.
Result generate(const QString& key, const QVector<Source>& sources,
                const std::function<bool(int,int)>& progress = {});
}
#endif

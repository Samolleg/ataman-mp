#ifndef WORLD_MAP_LAYOUT_H
#define WORLD_MAP_LAYOUT_H

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "map_catalog.h"

enum class EWorldMapOriginProvenance
{
    Inferred,
    Manual
};

struct SWorldMapZoneOrigin
{
    QString profileId;
    QString allodId;
    QString zoneId;
    QString mprIdentity;
    QPointF origin;
    EWorldMapOriginProvenance provenance{
        EWorldMapOriginProvenance::Inferred};
    QString componentAnchorZoneId;
};

struct SWorldMapInferenceResult
{
    QVector<SWorldMapZoneOrigin> zones;
    QStringList diagnostics;
};

class CWorldMapLayout
{
public:
    static SWorldMapInferenceResult infer(
        const QString& profileId,
        const QVector<SMapCatalogEntry>& topology);
    static const SWorldMapZoneOrigin* zone(
        const QVector<SWorldMapZoneOrigin>& zones,
        const QString& profileId, const QString& allodId,
        const QString& zoneId);
    static bool originFor(
        const QVector<SWorldMapZoneOrigin>& zones,
        const QString& profileId, const QString& allodId,
        const QString& zoneId, QPointF* origin);
    static QPointF toWorld(const QPointF& mapOrigin,
                           const QPointF& localPosition);
    static QPointF toLocal(const QPointF& mapOrigin,
                           const QPointF& worldPosition);
};

QString worldMapOriginProvenanceName(EWorldMapOriginProvenance provenance);
bool parseWorldMapOriginProvenance(
    const QString& name, EWorldMapOriginProvenance* provenance);

#endif // WORLD_MAP_LAYOUT_H

#ifndef MAP_CATALOG_H
#define MAP_CATALOG_H

#include <QString>
#include <QStringList>
#include <QPointF>
#include <QRectF>
#include <QVector>

struct SMapTravelZone
{
    int exitId{0};
    QString destinationZoneId;
    int destinationExitId{0};
    QRectF deployRectangle;
    bool hasDeployRectangle{false};
    double viewAngle{0.0};
    bool hasViewAngle{false};

    QPointF spawnPoint() const { return deployRectangle.center(); }
};

struct SMapCatalogEntry
{
    QString zoneId;
    QString allodId;
    QString zoneType;
    QString skyType;
    QString mprName;
    QString mobName;
    QString mprPath;
    QString mobPath;
    QString sourceArchive;
    int sectorWidth{0};
    int sectorHeight{0};
    QString minimapTexture;
    QString objectivesTexture;
    QVector<SMapTravelZone> travelZones;

    bool isComplete() const { return !mprPath.isEmpty() && !mobPath.isEmpty(); }
    bool isBriefing() const
    { return zoneType.compare(QStringLiteral("brief"), Qt::CaseInsensitive) == 0; }
};

struct SMapCatalogResult
{
    QVector<SMapCatalogEntry> entries;
    QVector<SMapCatalogEntry> topologyEntries;
    QString sourceArchive;
    QStringList messages;
};

QString mapTravelRouteKey(const QVector<SMapTravelZone>& routes, int index);

class CMapCatalog
{
public:
    static QVector<SMapCatalogEntry> parseMapTopology(
        const QString& text, const QString& sourceArchive = QString());
    static QVector<SMapCatalogEntry> parseMapTable(const QString& text,
                                                   const QString& sourceArchive = QString());
    static SMapCatalogResult load(const QVector<QString>& resourceRoots,
                                  const QVector<QString>& mapRoots);
};

#endif // MAP_CATALOG_H

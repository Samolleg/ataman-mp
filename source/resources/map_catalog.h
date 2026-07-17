#ifndef MAP_CATALOG_H
#define MAP_CATALOG_H

#include <QString>
#include <QStringList>
#include <QVector>

struct SMapCatalogEntry
{
    QString zoneId;
    QString allodId;
    QString zoneType;
    QString mprName;
    QString mobName;
    QString mprPath;
    QString mobPath;
    QString sourceArchive;

    bool isComplete() const { return !mprPath.isEmpty() && !mobPath.isEmpty(); }
};

struct SMapCatalogResult
{
    QVector<SMapCatalogEntry> entries;
    QString sourceArchive;
    QStringList messages;
};

class CMapCatalog
{
public:
    static QVector<SMapCatalogEntry> parseMapTable(const QString& text,
                                                   const QString& sourceArchive = QString());
    static SMapCatalogResult load(const QVector<QString>& resourceRoots,
                                  const QVector<QString>& mapRoots);
};

#endif // MAP_CATALOG_H

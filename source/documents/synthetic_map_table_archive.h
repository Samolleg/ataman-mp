#ifndef SYNTHETIC_MAP_TABLE_ARCHIVE_H
#define SYNTHETIC_MAP_TABLE_ARCHIVE_H

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QStringList>

// Builds a run-local texts.res which differs only by one appended map.txt
// zone. The installed archive is never modified.
bool buildSyntheticMapTableArchive(const QByteArray& sourceArchive,
                                   const QString& sourceName,
                                   const QString& zoneId,
                                   const QString& allodId,
                                   const QString& mapName,
                                   int mapSizeX,
                                   int mapSizeY,
                                   const QPointF& spawn,
                                   QByteArray* outputArchive,
                                   QStringList* errors = nullptr);

// Adds a neutral checkerboard MMP to a run-local copy of menus.res. The game's
// texture manager mounts menus.res, whereas the map table's texts.res is not a
// texture source.
bool buildSyntheticMapPlaceholderArchive(const QByteArray& sourceArchive,
                                         const QString& sourceName,
                                         QByteArray* outputArchive,
                                         QStringList* errors = nullptr);

QString syntheticMapPlaceholderTextureName();

#endif // SYNTHETIC_MAP_TABLE_ARCHIVE_H

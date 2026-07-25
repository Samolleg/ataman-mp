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

// Replaces one existing MMP with a neutral checkerboard in a run-local,
// append-only copy of menus.res. This preserves the shipped hash table and
// lookup chains; the installed archive remains untouched.
bool buildSyntheticMapPlaceholderArchive(const QByteArray& sourceArchive,
                                         const QString& sourceName,
                                         QByteArray* outputArchive,
                                         QStringList* errors = nullptr);

QString syntheticMapPlaceholderTextureName();

#endif // SYNTHETIC_MAP_TABLE_ARCHIVE_H

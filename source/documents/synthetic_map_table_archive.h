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
                                   QStringList* errors = nullptr,
                                   const QString& textureProbeName = QString());

// Builds a standalone run-owned RES containing only Endurance's neutral
// checkerboard. It does not borrow, replace, or rebuild any installed archive.
bool buildSyntheticMapPlaceholderArchive(QByteArray* outputArchive,
                                         QStringList* errors = nullptr);

QString syntheticMapPlaceholderTextureName();

#endif // SYNTHETIC_MAP_TABLE_ARCHIVE_H

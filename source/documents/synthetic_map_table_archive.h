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

// Builds the same run-owned one-member RES with a caller-reviewed 256x256 MMP.
// This is used only by Test Current Map and never writes an installed archive.
bool buildSyntheticMapTextureArchive(const QString& textureName,
                                     const QByteArray& mmpPayload,
                                     QByteArray* outputArchive,
                                     QStringList* errors = nullptr);

// Updates only the selected zone's minimap field inside an already run-owned
// texts.res candidate. The objectives field and every other member are
// preserved. This never writes a file.
bool stageTemporaryMinimapReference(const QByteArray& stagedMapTableArchive,
                                    const QString& sourceName,
                                    const QString& zoneId,
                                    const QString& textureName,
                                    QByteArray* outputArchive,
                                    QStringList* errors = nullptr);

// Produces a run-scoped texture name so the generated preview cannot be
// shadowed by an identically named member already loaded from textures.res.
QString temporaryMinimapRuntimeTextureName(const QByteArray& mmpPayload,
                                           const QString& runId);

QString syntheticMapPlaceholderTextureName();

#endif // SYNTHETIC_MAP_TABLE_ARCHIVE_H

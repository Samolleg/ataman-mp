#pragma once
#include <QByteArray>
#include <QFileInfo>
#include <QStringList>
#include "map_save_batch.h"

// Copies authored pages without recompression and binds their material profile
// to the new namespace. No game resources or source MPR are changed.
bool buildNewTerrainAtlas(const QFileInfo& terrainTemplate,
                          const QByteArray& candidateMpr,
                          const QStringList& resourceArchives,
                          QByteArray* atlas, QString* error);
SMapSaveBatchResult commitNewTerrainCanvas(const QFileInfo& terrainTemplate,
                                          const QByteArray& candidateMpr,
                                          const QFileInfo& destination,
                                          const QStringList& resourceArchives,
                                          const QString& backupRoot = QString());

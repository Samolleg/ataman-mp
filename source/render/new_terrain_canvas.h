#pragma once
#include <QByteArray>
#include <QFileInfo>
#include <QStringList>
#include "map_save_batch.h"

// Copies authored pages without recompression and binds their material profile
// to the candidate namespace. This preparation does not change any file.
bool buildNewTerrainAtlas(const QFileInfo& terrainTemplate,
                          const QByteArray& candidateMpr,
                          const QStringList& resourceArchives,
                          QByteArray* atlas, QString* error);
// Publish a compact hash-bound atlas into the configured HDPack and commit
// the new MPR in one backed-up cohort. The template is never modified.
SMapSaveBatchResult commitNewTerrainCanvas(const QFileInfo& terrainTemplate,
                                          const QByteArray& candidateMpr,
                                          const QFileInfo& destination,
                                          const QStringList& resourceArchives,
                                          const QString& backupRoot = QString());

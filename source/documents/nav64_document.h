#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <memory>
#include "Navigation/Types.h"

struct SNav64Header {
    QString fingerprint, mpr, mob, profile;
    quint32 container=0, algorithm=0;
    Navigation::Profile body;
};
struct SNav64Batch {
    QVector<QVector3D> vertices;
    QVector4D color;
};
struct SNav64RenderTile {
    QVector3D minimum, maximum;
    QVector<SNav64Batch> areas;
    QVector<QVector3D> edges, narrow;
};
// CPU-only, immutable after load; no editor document or MOB ownership.
struct SNav64Document {
    SNav64Header header;
    QString path, error;
    QStringList warnings;
    QVector<SNav64RenderTile> renderTiles;
    QVector3D minimum, maximum;
    QVector<QVector3D> tiles, islands;
    int polygonCount=0, tileCount=0, islandCount=0, narrowCount=0, detailOutlierVertices=0;
    bool valid=false;
};
QStringList nav64Profiles();
QVector<quint32> nav64SupportedAlgorithms();
QVector<quint32> nav64SupportedContainers();
SNav64Header readNav64Header(const QString& path, QString* error);
// The sole scene-to-file resolver: embedded triple is authoritative.
QString findNav64ForScene(const QString& directory, const QString& mpr,
                         const QString& mob, const QString& profile, QString* error);
SNav64Document loadNav64(const QString& path);

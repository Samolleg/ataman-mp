#ifndef PERSISTENT_VISUAL_CACHE_H
#define PERSISTENT_VISUAL_CACHE_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>
#include <QVector3D>

#include "types.h"

namespace ei {

struct SCachedVisualPart
{
    QString name;
    QString parentName;
    QVector3D restOffset;
    int textureNumber{0};
    QVector<SVertexData> vertices;
    QVector<int> sourceVertexIndices;
};

QString visualCacheDirectoryForApplication(const QString& applicationDirectory,
                                           const QString& category);
QString visualCacheDirectory(const QString& category);
QString visualCacheKey(const QByteArray& identity);

inline bool shouldPersistReusableVisualArtifact(int useCount)
{
    return useCount >= 2;
}

bool readVisualCachePayload(const QString& directory, const QString& key,
                            quint32 payloadSchema, QByteArray* payload);
bool writeVisualCachePayload(const QString& directory, const QString& key,
                             quint32 payloadSchema, const QByteArray& payload);

bool readCachedVisualImage(const QString& directory, const QString& key, QImage* image);
bool writeCachedVisualImage(const QString& directory, const QString& key, const QImage& image);

bool readCachedFigureAssembly(const QString& directory, const QString& key,
                              QVector<SCachedVisualPart>* parts);
bool writeCachedFigureAssembly(const QString& directory, const QString& key,
                               const QVector<SCachedVisualPart>& parts);

}

#endif

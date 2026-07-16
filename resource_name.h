#ifndef RESOURCE_NAME_H
#define RESOURCE_NAME_H

#include <QString>
#include <QStringList>

namespace ei {

QString textureCacheKey(const QString& textureName, const QString& modelName = QString());
QStringList textureResourceCandidates(const QString& textureName, const QString& modelName = QString());
QStringList terrainTextureResourceNames(const QString& mapName, int textureAtlasCount);

}

#endif // RESOURCE_NAME_H

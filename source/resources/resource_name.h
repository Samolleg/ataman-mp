#ifndef RESOURCE_NAME_H
#define RESOURCE_NAME_H

#include <QString>
#include <QStringList>

namespace ei {

QString textureCacheKey(const QString& textureName, const QString& modelName = QString());
QStringList textureResourceCandidates(const QString& textureName, const QString& modelName = QString());
QStringList terrainTextureResourceNames(const QString& mapName, int textureAtlasCount);
float textureUvScale(const QString& textureName);
float textureUvScaleForBounds(const QString& textureName, float minU, float maxU,
                              float minV, float maxV);
bool usesUnlayeredHumanoidTexture(const QString& partName);

}

#endif // RESOURCE_NAME_H

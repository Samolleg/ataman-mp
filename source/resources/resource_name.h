#ifndef RESOURCE_NAME_H
#define RESOURCE_NAME_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace ei {

struct SUvTransform
{
    float scaleU{1.0f};
    float scaleV{1.0f};
    float offsetU{0.0f};
    float offsetV{0.0f};
    bool normalizedAtlasCell{false};
};

QString textureCacheKey(const QString& textureName, const QString& modelName = QString());
QStringList textureResourceCandidates(const QString& textureName, const QString& modelName = QString());
QStringList terrainTextureResourceNames(const QString& mapName, int textureAtlasCount);
float textureUvScale(const QString& textureName);
float textureUvScaleForBounds(const QString& textureName, float minU, float maxU,
                              float minV, float maxV);
SUvTransform standaloneAtlasCellUvTransform(float minU, float maxU,
                                             float minV, float maxV,
                                             int columns = 4, int rows = 4);
bool usesUnlayeredHumanoidTexture(const QString& partName);
// Resource identity is stable for one configured profile generation. Archive
// metadata is deliberately sampled once and invalidated by the explicit
// resource-refresh path instead of being restatted for every map object.
QString cachedResourceSourceFingerprint(const QVector<QString>& sources);
void invalidateResourceSourceFingerprintCache();
// Expands configured resource directories in the same first-match order as
// the active mod's [RES] routing table, then appends unlisted archives.
QVector<QString> orderedResourceSources(const QVector<QString>& explicitSources);

}

#endif // RESOURCE_NAME_H

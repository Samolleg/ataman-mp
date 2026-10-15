#ifndef PERSISTENT_VISUAL_CACHE_H
#define PERSISTENT_VISUAL_CACHE_H

#include <QByteArray>
#include <QImage>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

#include "types.h"

namespace ei {

// Cost-aware recency bookkeeping shared by the in-session visual caches.
// The policy is deliberately independent of OpenGL so unload/return behavior
// can be regression-tested without creating a rendering context.
class CVisualCacheLru
{
public:
    void touch(const QString& key, qint64 costBytes);
    void touchExisting(const QString& key);
    QStringList trim(qint64 maximumBytes);
    void remove(const QString& key);
    void clear();
    qint64 totalBytes() const { return m_totalBytes; }
    QStringList oldestFirst() const { return m_oldestFirst; }

private:
    QHash<QString, qint64> m_costs;
    QStringList m_oldestFirst;
    qint64 m_totalBytes{0};
};

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
bool removeDeprecatedVisualAssemblyCacheForApplication(const QString& applicationDirectory);
QString visualCacheKey(const QByteArray& identity);
void setVisualCacheLimitMiB(int limitMiB);
int visualCacheLimitMiB();
// Maintenance is coalesced off the caller thread; the disk budget is eventual.
bool visualCacheMaintenancePending();
// Account for cache files written by a sibling cache format (for example the
// JSON appearance-plan cache) so the single user-visible disk budget covers
// the complete character cache tree.
void recordVisualCacheFileWrite(const QString& directory, qint64 previousBytes,
                                qint64 currentBytes);

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

#ifndef TERRAIN_ATLAS_ARCHIVE_BINDING_H
#define TERRAIN_ATLAS_ARCHIVE_BINDING_H

#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include "terrain_atlas_profile.h"

struct STerrainAtlasArchiveBindingResult
{
    QString archivePath;
    QByteArray decodedAtlasPixelSha256;
    QStringList completeArchives;
    QMap<QString, QByteArray> completeArchivePixelSha256;
    qint64 archiveSize{-1};
    qint64 archiveLastModifiedMs{-1};
    QString error;

    bool success() const { return !archivePath.isEmpty(); }
    bool hasCompleteArchive(const QString& path) const;
};

struct STerrainAtlasEmbeddedBindingResult
{
    QString archivePath;
    QString profileMemberName;
    STerrainAtlasProfile profile;
    QByteArray decodedAtlasPixelSha256;
    QVector<QImage> decodedAtlasTiles;
    qint64 archiveSize{-1};
    qint64 archiveLastModifiedMs{-1};
    QString error;

    bool success() const { return !archivePath.isEmpty(); }
    bool matchesLoadedAtlas(const QString& activeArchivePath,
                            const QString& currentNamespace,
                            const QByteArray& currentAtlasPixelSha256,
                            int currentAtlasSlotCount,
                            CValidationReport* report = nullptr) const;
    // Sizes are native per-page sizes and may differ. Reuse never normalizes
    // pixels: the resolver's namespace/physical-count/native-SHA proof applies.
    bool canReuseDecodedAtlasTiles(const QString& activeArchivePath,
                                   const QString& currentNamespace,
                                   int currentAtlasSlotCount,
                                   CValidationReport* report = nullptr) const;
};

class CTerrainAtlasArchiveBindingResolver
{
public:
    // Reads only the RES header, hash table, filename buffer, and requested
    // payloads. Large unrelated atlas pages are never copied or hashed.
    static bool readArchiveMembers(
        const QString& archivePath, const QStringList& memberNames,
        QMap<QString, QByteArray>* payloads, qint64* bytesRead = nullptr,
        QString* error = nullptr);
    static STerrainAtlasEmbeddedBindingResult findEmbeddedArchive(
        const QStringList& candidateArchives,
        const QString& mprNamespace,
        const QString& preferredArchive = QString());
    static STerrainAtlasArchiveBindingResult findExactArchive(
        const QStringList& candidateArchives,
        const QString& mprNamespace,
        int textureCount,
        int atlasSlotCount,
        const QByteArray& expectedAtlasPixelSha256,
        const QString& preferredArchive = QString(),
        QVector<QImage>* decodedTiles = nullptr);
};

#endif // TERRAIN_ATLAS_ARCHIVE_BINDING_H

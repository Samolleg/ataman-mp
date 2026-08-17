#ifndef TERRAIN_ATLAS_ARCHIVE_BINDING_H
#define TERRAIN_ATLAS_ARCHIVE_BINDING_H

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include "terrain_atlas_profile.h"

struct STerrainAtlasArchiveBindingResult
{
    QString archivePath;
    QByteArray decodedAtlasPixelSha256;
    QStringList completeArchives;
    QMap<QString, QByteArray> completeArchivePixelSha256;
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
    QString error;

    bool success() const { return !archivePath.isEmpty(); }
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
        const QString& preferredArchive = QString());
};

#endif // TERRAIN_ATLAS_ARCHIVE_BINDING_H

#ifndef TERRAIN_ATLAS_RECOVERY_STORE_H
#define TERRAIN_ATLAS_RECOVERY_STORE_H

#include <QByteArray>
#include <QString>
#include <QVector>

struct STerrainAtlasRecoveryPage
{
    QString name;
    QByteArray payload;
};

struct STerrainAtlasRecoverySnapshot
{
    QString resourceIdentity;
    QByteArray atlasPixelSha256;
    QByteArray profileBytes;
    QString mprPath;
    QString resPath;
    QByteArray mprSha256;
    QVector<qint32> runtimeTerrainTypes;
    QVector<STerrainAtlasRecoveryPage> pages;
    // Small editor-only RES members (for example the exact shared-corner
    // semantic field). They are content-addressed like pages and restored
    // verbatim, but are never interpreted as game texture pages.
    QVector<STerrainAtlasRecoveryPage> metadataMembers;
};

struct STerrainAtlasRecoveryResult
{
    bool success{false};
    bool skipped{false};
    QString manifestPath;
    QString error;
};

// Durable, editor-private recovery storage for published semantic atlases.
// Large MMP payloads and the exact profile bytes are content-addressed, so
// repeated saves create tiny manifests rather than copying whole atlases.
class CTerrainAtlasRecoveryStore
{
public:
    explicit CTerrainAtlasRecoveryStore(const QString& rootOverride = QString());

    QString rootPath() const { return m_rootPath; }
    QString objectDirectory() const;
    STerrainAtlasRecoveryResult saveSnapshot(
        const STerrainAtlasRecoverySnapshot& snapshot) const;
    bool findExact(const QString& resourceIdentity,
                   const QByteArray& atlasPixelSha256,
                   STerrainAtlasRecoverySnapshot* snapshot,
                   QString* error = nullptr) const;
    bool findForMpr(const QString& resourceIdentity,
                    const QByteArray& mprSha256,
                    STerrainAtlasRecoverySnapshot* snapshot,
                    QString* error = nullptr) const;
    bool buildStandaloneArchive(
        const STerrainAtlasRecoverySnapshot& snapshot,
        QByteArray* archiveBytes,
        QString* error = nullptr) const;

private:
    QString identityDirectory(const QString& resourceIdentity) const;
    bool writeObject(const QByteArray& payload, QByteArray* sha256,
                     QString* error) const;
    bool readObject(const QByteArray& sha256, QByteArray* payload,
                    QString* error) const;
    bool loadManifest(const QString& manifestPath,
                      STerrainAtlasRecoverySnapshot* snapshot,
                      QString* error) const;

    QString m_rootPath;
};

#endif // TERRAIN_ATLAS_RECOVERY_STORE_H

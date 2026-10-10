#ifndef TERRAIN_HD_REPUBLISH_H
#define TERRAIN_HD_REPUBLISH_H

#include "terrain_atlas_profile.h"
#include "res_archive_index.h"
#include "res_archive_document.h"
#include <QJsonObject>
#include <QSharedPointer>

// Exact evidence only: never substitute a visually similar tile or a source
// selected from the map name. One index covers all configured stock namespaces.
class CTerrainHdSources
{
public:
    struct Source { int archive{-1}; QString page; int slot{-1}; int size{0}; };
    bool open(const QStringList& archives, QString* error);
    bool match(const QImage& published, const STerrainAtlasProfileSlot& provenance,
               Source* source, QString* reason) const;
    QImage image(const Source& source, int size, QString* error = nullptr) const;
    QByteArray payload(const Source& source, QString* error = nullptr) const;
    bool current(QString* error) const;
    bool find(const QString& nameSpace, int slot, Source* source) const;
    int sourceCount() const { return m_sources.size(); }
private:
    QVector<QSharedPointer<CResArchiveIndex>> m_archives;
    QVector<Source> m_sources;
    QHash<QByteArray,QVector<int>> m_byPixels;
    QHash<QString,int> m_byIdentity;
    mutable QHash<int,QByteArray> m_hdHashes;
    mutable QMap<QString,QByteArray> m_payloadCache;
};

struct STerrainHdRepublishResult
{
    bool success{false};
    QString error;
    STerrainAtlasProfile profile;
    QVector<QImage> before, after;
    QVector<QByteArray> pages;
    QByteArray mprBytes;
    QVector<SResArchiveNewMember> publishedMembers;
    QJsonObject receipt;
};

STerrainHdRepublishResult rebuildTerrainAtlasHd(
    const STerrainAtlasProfile& profile, const QVector<QByteArray>& authoredPages,
    const CTerrainHdSources& sources);

class CMprDocument;
bool prepareTerrainHdPublication(const CMprDocument& map,
    const QByteArray& oldSemantic, STerrainHdRepublishResult* result);

int runTerrainHdRepublish(const QString& fixturePath);

#endif

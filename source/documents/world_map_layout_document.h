#ifndef WORLD_MAP_LAYOUT_DOCUMENT_H
#define WORLD_MAP_LAYOUT_DOCUMENT_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "world_map_layout.h"

struct SWorldMapLayoutUpdateResult
{
    QVector<SWorldMapZoneOrigin> zones;
    QStringList diagnostics;
    QString warning;
    bool persisted{false};
    bool storageBlocked{false};
};

class CWorldMapLayoutDocument
{
public:
    explicit CWorldMapLayoutDocument(const QString& path = QString());

    static QString defaultPath();
    QString path() const { return m_path; }
    SWorldMapLayoutUpdateResult loadAndUpdate(
        const QString& profileId,
        const QVector<SMapCatalogEntry>& topology);

    // These persistence APIs are intentionally UI-independent. The following
    // stacked stage can bind map dragging and reset commands to them without
    // teaching the renderer how the JSON document is owned.
    bool setZoneOrigin(const QString& profileId, const QString& allodId,
                       const QString& zoneId, const QPointF& origin,
                       QString* error = nullptr);
    bool resetZoneToInferred(
        const QString& profileId, const QString& allodId,
        const QString& zoneId,
        const QVector<SMapCatalogEntry>& topology,
        QString* error = nullptr);
    bool resetAllodLayout(
        const QString& profileId, const QString& allodId,
        const QVector<SMapCatalogEntry>& topology,
        QString* error = nullptr);

private:
    enum class ELoadState { NotLoaded, Ready, Blocked };

    bool ensureLoaded();
    bool save(const QVector<SWorldMapZoneOrigin>& zones,
              QString* error = nullptr) const;
    QVector<SWorldMapZoneOrigin> profileZones(
        const QVector<SWorldMapZoneOrigin>& zones,
        const QString& profileId) const;
    SWorldMapZoneOrigin alignInferredZone(
        const SWorldMapZoneOrigin& inferred,
        const QVector<SWorldMapZoneOrigin>& inference,
        const QVector<SWorldMapZoneOrigin>& existing) const;

    QString m_path;
    ELoadState m_loadState{ELoadState::NotLoaded};
    bool m_hasPersistentFile{false};
    QString m_loadWarning;
    QVector<SWorldMapZoneOrigin> m_zones;
};

#endif // WORLD_MAP_LAYOUT_DOCUMENT_H

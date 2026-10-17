#ifndef WORLD_TERRAIN_SESSION_H
#define WORLD_TERRAIN_SESSION_H
#include <QSharedPointer>
#include "mpr_document.h"
#include "world_terrain_sculpt.h"

struct SWorldTerrainDocument
{
    QString path;
    CMprDocument baseline;
    CTerrainEditModel terrain;
    QSharedPointer<CTerrainWorkingAtlas> atlas;
    QVector<SMprMaterialRecord> materials;
};
class CWorldTerrainSession
{
public:
    QMap<QString,QSharedPointer<SWorldTerrainDocument>> documents;
    static QString key(const QString& path);
    bool load(const QString& path, QString* error);
    bool apply(const SWorldTerrainStroke& stroke, bool forward, QString* error);
    bool rebaseSavedDocument(const QString& id, const CMprDocument& baseline, QString* error);
    QStringList dirtyPaths() const;
    void record(const SWorldTerrainStroke& stroke);
    bool undo(QString* error);
    bool redo(QString* error);
    bool canUndo() const { return m_index>0; }
    bool canRedo() const { return m_index<m_history.size(); }
    const SWorldTerrainStroke* nextCommand(bool forward) const;
    void clearHistory() { m_history.clear(); m_index=0; }
    void clear() { documents.clear(); m_history.clear(); m_index=0; }
private:
    QVector<SWorldTerrainStroke> m_history;
    int m_index{0};
};

struct SWorldTerrainSaveItem
{
    QString path;
    QByteArray original;
    QByteArray candidate;
};
struct SWorldTerrainSaveResult
{
    bool success{false};
    QString error;
    QStringList backups;
    QString recoveryMarker;
};
// Durable backups + a per-target discovery marker are written before replacing
// any target. Recovery validates identities and restores the complete cohort.
// This is recoverable publication, not atomicity across files/volumes.
SWorldTerrainSaveResult saveWorldTerrainCohort(
    const QVector<SWorldTerrainSaveItem>& items, int failBeforePublish=-1);
bool recoverWorldTerrainCohort(const QString& marker, QString* error);
#endif

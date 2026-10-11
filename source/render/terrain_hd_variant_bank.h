#ifndef TERRAIN_HD_VARIANT_BANK_H
#define TERRAIN_HD_VARIANT_BANK_H
#include "terrain_hd_variants.h"
#include "terrain_hd_progress.h"
#include "terrain_atlas_profile.h"
#include <QSharedPointer>

class CTerrainWorkingAtlas;
class CValidationReport;
namespace TerrainHdVariantBank {
struct Config { QStringList archives, maps; QString cacheRoot; };
struct Entry { QString material,layer,id;int reviewedClass{-1},physicalClass{-1},slot{-1};QImage pixels;QVector<QByteArray> blocks;QJsonObject metadata; };
struct Bank {
    QString directory,error;
    QJsonObject manifest;
    QVector<Entry> entries;
    // Runtime fast guard, never part of deterministic cache metadata.
    QMap<QString,QPair<qint64,qint64>> sourceStats;
    bool success{false},cancelled{false};
};
QString defaultRoot();
Config configured(const QStringList& resources);
Bank generate(const Config& config,CTerrainHdProgress* progress=nullptr);
Bank load(const QString& root,QString* error=nullptr);
QSharedPointer<const Bank> available();
void forget();
QSet<QString> rejects();
QStringList groupNames(const Bank& bank);
QVector<Entry> forMaterial(const Bank& bank,const QString& name,int reviewedClass,ETerrainAtlasLayer layer,
                           const QSet<QString>& excluded=rejects());
bool import(CTerrainWorkingAtlas* atlas,const QVector<Entry>& entries,qint32 material,
            CValidationReport* report);
int runCheck(const QString& configPath);
}
#endif

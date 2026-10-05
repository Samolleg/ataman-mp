#ifndef MAP_WEAPON_CATALOG_H
#define MAP_WEAPON_CATALOG_H

#include "item_spell_constructor.h"
#include <QMap>

class CWorldObj;
namespace ei {
struct SMapWeaponModel { QString model, mod, bon; };

// Read-only catalog next to the active database. No MOB discovery or catalog writes.
class MapWeaponCatalog
{
public:
    static QString pathForDatabase(const QString& databasePath);
    bool load(const QString& path, const ItemSpellConstructorCatalog& database);
    bool present() const { return m_present; }
    QString diagnostic() const { return m_diagnostic; }
    QString archivePath() const { return m_archive; }
    QVector<SMapWeaponModel> models(const QString& weapon) const;
    CWorldObj* create(const SConstructorItem& item, const SConstructorMaterial* material,
                      const QString& model, bool highResolution, QString* diagnostic) const;
private:
    bool m_present{false};
    QString m_archive, m_diagnostic;
    QMap<QString, QVector<SMapWeaponModel>> m_models;
};
}
#endif

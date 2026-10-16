#ifndef MAP_WEAPON_CATALOG_H
#define MAP_WEAPON_CATALOG_H

#include "item_spell_constructor.h"
#include <QMap>

class CWorldObj;
namespace ei {
class DatabaseDocument;
struct SMapInventoryVariant {
    QString material, label, descriptor, texture;
    QStringList textures;
};
struct SMapInventoryItem {
    QString name, label, category, model, texture;
    QMap<QString, QString> modelResources;
    QStringList textures;
    QVector<SMapInventoryVariant> variants;
    QString unavailableReason;
};

class MapInventoryCatalog
{
public:
    static QString pathForDatabase(const QString& databasePath);
    bool load(const QString& path, const DatabaseDocument* database);
    bool containsModel(const QString& model) const;
    bool present() const { return m_present; }
    QString diagnostic() const { return m_diagnostic; }
    const SMapInventoryItem* find(const QString& name) const;
    CWorldObj* create(const QString& name, QString* diagnostic, const QString& material = QString()) const;
private:
    bool m_present{false};
    QString m_diagnostic;
    QMap<QString, SMapInventoryItem> m_items;
};

struct SMapWeaponModel { QString model, mod, bon; };

// Read-only catalog next to the active database. No MOB discovery or catalog writes.
class MapWeaponCatalog
{
public:
    static QString pathForDatabase(const QString& databasePath);
    static QString textureName(const SConstructorItem& item, const SConstructorMaterial* material,
                               bool highResolution);
    bool load(const QString& path, const ItemSpellConstructorCatalog& database);
    bool containsModel(const QString& model) const;
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

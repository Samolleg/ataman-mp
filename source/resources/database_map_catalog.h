#ifndef DATABASE_MAP_CATALOG_H
#define DATABASE_MAP_CATALOG_H

#include "profile_database.h"
#include <QJsonObject>

namespace ei {

enum class EDatabaseMapCategory { Creature, Item, Lever };

struct SDatabaseMapEntry
{
    EDatabaseMapCategory category{EDatabaseMapCategory::Creature};
    QString key;
    QString name;
    QString label;
    QString model;
    QString primaryTexture;
    QString secondaryTexture;
    QString unavailableReason;
    int unitType{-1};
    float complexionX{1}, complexionY{1}, complexionZ{1};
};

QVector<SDatabaseMapEntry> databaseMapCatalog(const SProfileDatabaseSnapshot& database);
// Applies database identity to a fresh type's defaults, never to a selected map object.
bool applyDatabaseMapEntry(const SDatabaseMapEntry& entry, const QJsonObject& defaults,
                          QJsonObject* result);

}
#endif

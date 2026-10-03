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
    bool labelFromTexts{false};
    QString model;
    QString primaryTexture;
    QString secondaryTexture;
    QStringList bodyParts;
    QString unavailableReason;
    int unitType{-1};
    float complexionX{1}, complexionY{1}, complexionZ{1};
};

QVector<SDatabaseMapEntry> databaseMapCatalog(const SProfileDatabaseSnapshot& database);
void applyDatabaseMapTexts(QVector<SDatabaseMapEntry>* entries, const QMap<QString, QString>& texts);
// Converted item textures use a separate map resource, without the file extension.
QString databaseItemMapTextureName(QString textureName);
// Applies database identity to a fresh type's defaults, never to a selected map object.
bool applyDatabaseMapEntry(const SDatabaseMapEntry& entry, const QJsonObject& defaults,
                          QJsonObject* result);

}
#endif

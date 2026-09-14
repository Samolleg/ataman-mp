#include "database_map_catalog.h"
#include "resource_profile.h"
#include "../runtime/character_bodypart_runtime.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <cmath>

namespace {
QString tr(const char* source) { return QCoreApplication::translate("DatabaseMapCatalog", source); }
QString texture(const QString& model, QString value)
{
    value = value.trimmed();
    if(value.startsWith("skin_", Qt::CaseInsensitive)) value.prepend(model);
    if(value.endsWith(".mmp", Qt::CaseInsensitive)) value.chop(4);
    return value;
}
}

namespace ei {
QString databaseItemMapTextureName(QString textureName)
{
    textureName = texture(QString(), textureName);
    if(!textureName.isEmpty() && !textureName.endsWith("map", Qt::CaseInsensitive))
        textureName += "map";
    return textureName;
}

QVector<SDatabaseMapEntry> databaseMapCatalog(const SProfileDatabaseSnapshot& database)
{
    QVector<SDatabaseMapEntry> result;
    for(auto it = database.monsters.constBegin(); it != database.monsters.constEnd(); ++it)
    {
        const auto& monster = it.value();
        SDatabaseMapEntry entry;
        entry.key = it.key(); entry.name = monster.name; entry.label = monster.name;
        const auto race = database.raceModels.constFind(monster.baseRace.trimmed().toLower());
        if(race == database.raceModels.constEnd())
            entry.unavailableReason = tr("The creature's base race is missing from this database.");
        else
        {
            entry.model = race->model; entry.unitType = race->typeId;
            QString primary = race->primaryTextures.value(monster.skinIndex);
            for(const QString& token : race->primaryTextures)
                if(token.startsWith("skin_", Qt::CaseInsensitive) && monster.skinIndex >= 0)
                {
                    primary = QString("skin_%1").arg(monster.skinIndex, 2, 10, QChar('0'));
                    break;
                }
            entry.primaryTexture = texture(entry.model, primary);
            // Same numbered wearable skin convention as the active JMV
            // appearance resolver, including skins beyond RaceModels' examples.
            if(usesJmvRuntime(database.profileId) && standardWearableAssembly(entry.model)
               && monster.skinIndex >= 0)
                entry.primaryTexture = texture(entry.model,
                    QString("skin_%1_x4").arg(monster.skinIndex, 2, 10, QChar('0')));
            entry.bodyParts = freshDatabaseBodyParts(entry.model, monster.hairIndex);
            entry.secondaryTexture = texture(entry.model, race->secondaryTextures.value(0));
            if(entry.model.isEmpty() || entry.primaryTexture.isEmpty())
                entry.unavailableReason = tr("The database does not define a model and skin for this creature.");
            else if(entry.unitType < 50 || entry.unitType > 52)
                entry.unavailableReason = tr("This creature's database type is not supported for placement.");
        }
        entry.complexionX = monster.complexionX; entry.complexionY = monster.complexionY;
        entry.complexionZ = monster.complexionZ;
        result.append(entry);
    }
    for(auto it = database.items.constBegin(); it != database.items.constEnd(); ++it)
    {
        SDatabaseMapEntry entry; entry.category = EDatabaseMapCategory::Item;
        entry.key = it.key(); entry.name = it->name; entry.label = it->name;
        result.append(entry);
    }
    for(auto it = database.levers.constBegin(); it != database.levers.constEnd(); ++it)
    {
        SDatabaseMapEntry entry; entry.category = EDatabaseMapCategory::Lever;
        entry.key = it.key(); entry.name = it->name; entry.model = it->name;
        entry.label = it->text.isEmpty() ? it->name : it->text;
        // Lever FLD3 is not the map instance's three figure interpolation
        // parameters. Keep the new lever's geometry defaults independent.
        result.append(entry);
    }
    for(auto& entry : result)
        if(!std::isfinite(entry.complexionX) || !std::isfinite(entry.complexionY)
           || !std::isfinite(entry.complexionZ) || entry.complexionX < 0
           || entry.complexionY < 0 || entry.complexionZ < 0)
            entry.unavailableReason = tr("The database contains an invalid object scale.");
    return result;
}

bool applyDatabaseMapEntry(const SDatabaseMapEntry& entry, const QJsonObject& defaults,
                          QJsonObject* result)
{
    if(!result) return false;
    *result = QJsonObject();
    if(!entry.unavailableReason.isEmpty())
        return false;
    QJsonObject node = defaults;
    if(entry.category == EDatabaseMapCategory::Item)
    {
        // Map fields contain a texture identifier; the game appends .mmp.
        // Preview lookup also accepts filenames and must not define save syntax.
        const QString primaryTexture = databaseItemMapTextureName(entry.primaryTexture);
        QJsonObject base = node.value("Base object").toObject();
        if(base.value("Node type").toInt() != 3 || entry.model.isEmpty()
           || primaryTexture.isEmpty()) return false;
        base.insert("Map name", entry.name);
        node.insert("Base object", base);
        node.insert("Model name", entry.model);
        node.insert("Primary texture", primaryTexture);
        node.insert("Secondary texture", primaryTexture);
        *result = node;
        return true;
    }
    QJsonObject world = node.value("World object").toObject();
    QJsonObject base = world.value("Base object").toObject();
    const int expectedType = entry.category == EDatabaseMapCategory::Creature ? 0x7 : 0x23;
    if(base.value("Node type").toInt() != expectedType) return false;
    base.insert("Map name", entry.name);
    if(entry.category == EDatabaseMapCategory::Creature)
        base.insert("Complection", QJsonArray({entry.complexionX, entry.complexionY, entry.complexionZ}));
    world.insert("Base object", base);
    world.insert("Model name", entry.model);
    world.insert("Primary texture", entry.primaryTexture);
    world.insert("Secondary texture", entry.secondaryTexture);
    if(entry.category == EDatabaseMapCategory::Creature)
        world.insert("Object parts", QJsonArray::fromStringList(entry.bodyParts));
    node.insert("World object", world);
    if(entry.category == EDatabaseMapCategory::Creature)
    {
        node.insert("Subtype", entry.unitType);
        node.insert("Prototype name", entry.name);
        // False means use the prototype's game values, not embedded imported stats.
        node.insert("Is use mob stats?", false);
    }
    *result = node;
    return true;
}
}

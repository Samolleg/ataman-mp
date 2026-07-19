#include "profile_visual_resolver_support.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {

SRuntimeProviderResult failure(const QString& code, const QString& message)
{
    SRuntimeProviderResult result;
    result.code = code;
    result.message = message;
    return result;
}

QStringList expandedList(const QStringList& source)
{
    QStringList result;
    for(const QString& value : source)
        for(const QString& part : value.split(',', QString::SkipEmptyParts))
            result.append(part.trimmed());
    return result;
}

QString textureName(const QString& model, const QString& token)
{
    QString value = token.trimmed().toLower();
    if(value.isEmpty())
        return value;
    if(value.startsWith("skin_"))
        value = model.toLower() + value;
    if(!value.endsWith(".mmp"))
        value += ".mmp";
    return value;
}

QString monsterTextureName(const QString& model, const QStringList& primary, int skinIndex)
{
    if(skinIndex < 0)
        return QString();
    // Human/orc RaceModels describe the texture family, while the Monsters
    // skin field is the actual numbered member. JMV legitimately uses numbers
    // beyond the representative RaceModels list (for example skin 39).
    for(const QString& token : primary)
        if(token.trimmed().startsWith("skin_", Qt::CaseInsensitive))
            return textureName(model, QString("skin_%1").arg(skinIndex, 2, 10, QChar('0')));
    return skinIndex < primary.size() ? textureName(model, primary.at(skinIndex)) : QString();
}

bool numberedHumanoidSkin(const QString& model)
{
    static const QSet<QString> models = {"unhuma", "unhufe", "unorma", "unorfe", "unhusk"};
    return models.contains(model.trimmed().toLower());
}

bool numberedZombieSkin(const QString& model)
{
    static const QSet<QString> models = {"unhuzm", "unhuzf"};
    return models.contains(model.trimmed().toLower());
}

bool databaseEquippedHumanoid(const QString& model)
{
    // Corpus-backed wearable/equipped actor assemblies. Creature actors keep
    // their already-resolved MOB assembly, texture, and integral parts.
    static const QSet<QString> models = {
        "unhuma", "unhufe", "unorma", "unorfe", "unhusk", "unhuzm", "unhuzf",
        "unmosk", "unmoud", "unmozo", "unmozo0"
    };
    return models.contains(model.trimmed().toLower());
}

QString jmvMonsterTextureName(const SCharacterAppearanceRequest& request, const QString& model,
                              const QStringList& primary, int skinIndex)
{
    if(numberedHumanoidSkin(model))
        return skinIndex < 0 ? QString()
            : textureName(model, QString("skin_%1_x4").arg(skinIndex, 2, 10, QChar('0')));

    if(numberedZombieSkin(model))
        return skinIndex < 0 ? QString()
            : textureName(model, QString("skin_%1_x4").arg(skinIndex, 2, 10, QChar('0')));

    // JMV frequently changes a creature's RaceModels assembly while the MOB
    // retains the older embedded model/token.  A bounded database texture row
    // is authoritative for a normal (non-imported) unit and is compatible with
    // the database-selected assembly by construction.
    if(skinIndex >= 0 && skinIndex < primary.size())
        return textureName(model, primary.at(skinIndex));

    // Some JMV RaceModels rows contain representative members of a numbered
    // texture family rather than an exhaustive list. Derive only when every
    // listed member proves a single sequential family (for example
    // vurdalak_00.x4 ... vurdalak_03.x4); otherwise fail closed below.
    if(skinIndex >= 0 && !primary.isEmpty())
    {
        static const QRegularExpression numberedFamily("^(.*?)([0-9]+)(\\.x[0-9]+)?(?:\\.mmp)?$");
        QString prefix, suffix;
        int width = 0;
        bool sequential = true;
        for(int index = 0; index < primary.size(); ++index)
        {
            const QRegularExpressionMatch match = numberedFamily.match(primary.at(index).trimmed());
            if(!match.hasMatch() || match.captured(2).toInt() != index)
            {
                sequential = false;
                break;
            }
            if(index == 0)
            {
                prefix = match.captured(1); suffix = match.captured(3);
                width = match.captured(2).size();
            }
            else if(match.captured(1).compare(prefix, Qt::CaseInsensitive) != 0
                    || match.captured(3).compare(suffix, Qt::CaseInsensitive) != 0
                    || match.captured(2).size() != width)
            {
                sequential = false;
                break;
            }
        }
        if(sequential)
            return textureName(model, prefix
                + QString("%1").arg(skinIndex, width, 10, QChar('0')) + suffix);
    }

    // JMV's non-humanoid numeric Graphics Data Skin Index is not an index into
    // RaceModels.FLD31 (GoblinSentryF2 is 91 while the race row has two names).
    // The MOB stores the already-resolved graphical token. It is safe to use
    // only when the database-selected model agrees with the embedded model.
    if(model.compare(request.model.trimmed(), Qt::CaseInsensitive) == 0
            && !request.primaryTexture.trimmed().isEmpty())
        return textureName(model, request.primaryTexture);
    return QString();
}

QString itemPrototype(const QString& reference)
{
    return reference.section('[', 0, 0).section('.', 0, 0).trimmed().toLower();
}

QString itemMaterial(const QString& reference)
{
    const QString base = reference.section('[', 0, 0);
    return base.section('.', 1, 1).trimmed().toLower();
}

enum class EWeaponClass { OneHand, Ranged, Shield, TwoHand, Unknown };

EWeaponClass weaponClass(int typeId)
{
    // JMV keeps the ordinary staff (TypeID 11) in a hand-sized family. Its
    // database contains staff+shield and paired-staff actors; only TypeID 15
    // is the dedicated two-handed staff family.
    if((typeId >= 0 && typeId <= 4) || typeId == 11) return EWeaponClass::OneHand;
    if(typeId == 5 || typeId == 6) return EWeaponClass::Ranged;
    if(typeId == 9) return EWeaponClass::Shield;
    if(typeId == 10 || (typeId >= 12 && typeId <= 15)) return EWeaponClass::TwoHand;
    return EWeaponClass::Unknown;
}

QString weaponFamily(int typeId)
{
    static const QMap<int, QString> families = {
        {0, "sword"}, {1, "axe"}, {2, "dagger"}, {3, "pike"}, {4, "club"},
        {5, "bow"}, {6, "crossbow"}, {9, "shield"}, {10, "dpike"},
        {11, "staff"}, {12, "dsword"}, {13, "daxe"}, {14, "dclub"}, {15, "dstaff"}
    };
    return families.value(typeId);
}

QString componentSuffix(const QString& family, int tti)
{
    return QString(".%1%2").arg(family).arg(tti, 2, 10, QChar('0'));
}

void appendComponents(const QString& family, int tti, const SEquipmentLayoutRequest& request,
                      QJsonArray* components, QJsonArray* placeholders, QJsonArray* diagnostics,
                      QStringList* matchedComponents = nullptr)
{
    if(family.isEmpty() || tti < 0)
        return;
    const QString suffix = componentSuffix(family, tti);
    if(request.assemblyParts.isEmpty())
    {
        components->append(suffix);
        return;
    }
    int matched = 0;
    for(const QString& part : request.assemblyParts)
        if(part.endsWith(suffix, Qt::CaseInsensitive))
        {
            components->append(part);
            if(matchedComponents) matchedComponents->append(part);
            ++matched;
        }
    if(matched == 0)
    {
        placeholders->append(suffix);
        diagnostics->append("Selected model assembly has no component matching *" + suffix);
    }
}

void appendHandComponent(const QString& hand, const QString& family, int tti,
                         const SEquipmentLayoutRequest& request, QJsonArray* components,
                         QJsonArray* placeholders, QJsonArray* diagnostics,
                         QStringList* matchedComponents)
{
    const QString expected = hand + componentSuffix(family, tti);
    if(request.assemblyParts.isEmpty())
    {
        components->append(expected);
        matchedComponents->append(expected);
        return;
    }
    for(const QString& part : request.assemblyParts)
        if(part.compare(expected, Qt::CaseInsensitive) == 0)
        {
            components->append(part);
            matchedComponents->append(part);
            return;
        }
    placeholders->append(expected);
    diagnostics->append("Selected model assembly has no component " + expected);
}

QStringList jmvEmbeddedCreatureParts(const QString& model, QStringList parts)
{
    // The goblin assembly includes the sling as its unarmed/default integral
    // weapon. Spear/axe variants can retain that member in OBJ_BODYPARTS, but
    // the explicit right-hand weapon is mutually exclusive in the game.
    if(model.compare("unmogo", Qt::CaseInsensitive) == 0)
    {
        bool explicitWeapon = false;
        for(const QString& part : parts)
            if(part.startsWith("rh3.pike", Qt::CaseInsensitive)
                    || part.startsWith("rh3.daxe", Qt::CaseInsensitive))
                explicitWeapon = true;
        if(explicitWeapon)
            for(int i = parts.size() - 1; i >= 0; --i)
                if(parts.at(i).compare("sling", Qt::CaseInsensitive) == 0)
                    parts.removeAt(i);
    }
    return parts;
}

void appendRangedComponents(int typeId, int tti, const SEquipmentLayoutRequest& request,
                            QJsonArray* components, QJsonArray* placeholders,
                            QJsonArray* diagnostics, QStringList* matchedComponents)
{
    QStringList expected;
    if(typeId == 5)
    {
        const QString index = QString::number(tti).rightJustified(2, '0');
        expected << "bwparta" + index << "lh3.bwpartb" + index
                 << "bwtetivaa" + index << "bwtetivab" + index
                 << "quiver" << "arrows";
    }
    else if(typeId == 6)
    {
        const QString index = QString::number(tti).rightJustified(2, '0');
        expected << "rh3.crbow" + index + "main"
                 << "crbow" + index + "part01" << "crbow" + index + "part02"
                 << "crbow" + index + "tetiva01" << "crbow" + index + "tetiva02";
    }
    for(const QString& wanted : expected)
    {
        const auto found = std::find_if(request.assemblyParts.constBegin(), request.assemblyParts.constEnd(),
            [&](const QString& part) { return part.compare(wanted, Qt::CaseInsensitive) == 0; });
        if(found != request.assemblyParts.constEnd())
        {
            components->append(*found);
            matchedComponents->append(*found);
        }
    }
    if(matchedComponents->isEmpty())
    {
        placeholders->append(typeId == 5 ? "bow" : "crossbow");
        diagnostics->append("Selected model assembly has no compatible ranged-weapon components.");
    }
}

QStringList armorSlots(const QString& type, bool jmv)
{
    static const QMap<QString, QStringList> armorPartMap = {
        {"helm", {"hd"}}, {"plate", {"bd"}}, {"leggings", {"hp"}},
        {"shirt", {"bd", "lh1", "rh1"}}, {"pants", {"hp", "ll1", "rl1"}},
        {"boots", {"ll2", "ll3", "rl2", "rl3"}},
        {"gloves", {"lh2", "lh3", "rh2", "rh3"}}
    };
    const QString normalized = type.trimmed().toLower();
    QStringList parts = armorPartMap.value(normalized);
    // JMV top armor can contribute geometry throughout its lower-layer group.
    // Only claim an overlap when that TTI component actually exists in the
    // active model assembly (appendArmorComponents performs that check). The
    // game gives plate priority over shirt/gloves and leggings priority over
    // pants/boots, while retaining lower geometry where the top TTI has none.
    if(jmv && normalized == "plate")
        parts << armorPartMap.value("shirt") << armorPartMap.value("gloves");
    else if(jmv && normalized == "leggings")
        parts << armorPartMap.value("pants") << armorPartMap.value("boots");
    return parts;
}

int armorLayerPriority(const QString& type)
{
    const QString normalized = type.trimmed().toLower();
    return normalized == "helm" || normalized == "plate" || normalized == "leggings" ? 1 : 0;
}

bool optionalJmvTopArmorComponent(const QString& type)
{
    const QString normalized = type.trimmed().toLower();
    // JMV top armor is an availability-driven overlay. Every exact
    // part.armorNN member is optional: when the ANHUM assembly has it, it
    // replaces that anatomical/lower-armor part; when absent, the lower part
    // remains. Do not synthesize a placeholder merely from the database slot.
    return normalized == "helm" || normalized == "plate" || normalized == "leggings";
}

bool isUpperArmor(const QString& type)
{
    const QString normalized = type.trimmed().toLower();
    return normalized == "helm" || normalized == "plate" || normalized == "leggings";
}

QString armorComponentSlot(const QString& component)
{
    const int separator = component.indexOf(".armor", 0, Qt::CaseInsensitive);
    return separator > 0 ? component.left(separator) : component;
}

QString equipmentTextureName(const QString& model, const QString& itemType, int typeId,
                             int tti, int textureVariant, const QString& materialCode,
                             bool weapon)
{
    static const QMap<QString, QString> armorCodes = {
        {"helm", "hl"}, {"plate", "pl"}, {"leggings", "lg"}, {"shirt", "sh"},
        {"pants", "pt"}, {"boots", "bt"}, {"gloves", "gl"}
    };
    static const QMap<int, QString> weaponCodes = {
        {0, "sw"}, {1, "ax"}, {2, "dg"}, {3, "sp"}, {4, "hm"},
        {5, "bw"}, {6, "cb"}, {9, "sd"}, {10, "tp"}, {11, "sf"},
        {12, "ts"}, {13, "ta"}, {14, "th"}, {15, "tf"}
    };
    const QString code = weapon ? weaponCodes.value(typeId)
                                : armorCodes.value(itemType.trimmed().toLower());
    if(code.isEmpty() || tti < 0 || textureVariant < 0 || materialCode.isEmpty())
        return QString();
    // The standard humanoid assemblies share the Unhuma equipment atlases.
    // Female, orc, and skeleton model names select different geometry/UV
    // regions, but there are no parallel Unhufe/Unorfe/Unhusk equipment MMP
    // families in the shipped Base/JMV corpus.
    QString textureModel = model.trimmed().toLower();
    static const QSet<QString> sharedHumanoidAtlases = {
        "unhuma", "unhufe", "unorma", "unorfe", "unhusk", "unhuzm", "unhuzf"
    };
    if(sharedHumanoidAtlases.contains(textureModel))
        textureModel = "unhuma";
    return QString("%1%2_%3.%4.%5.%6.mmp")
        .arg(textureModel, code)
        // Database TTI is the literal geometry and MMP family index. Archive
        // inventories show, for example, bow TTI 3 -> UnhumaBW_03 and shield
        // TTI 6 -> UnhumaSD_06; incrementing it selects a different item.
        .arg(tti, 2, 10, QChar('0')).arg(materialCode.trimmed().toLower())
        .arg(textureVariant).arg(!weapon && itemType.compare("helm", Qt::CaseInsensitive) != 0
                                               ? "x4" : "x2");
}

QStringList appendArmorComponents(const ei::SItemDatabaseRecord& item,
                                  bool jmv,
                                  const SEquipmentLayoutRequest& request,
                                  QJsonArray* components, QJsonArray* placeholders,
                                  QJsonArray* diagnostics)
{
    QStringList matched;
    const QStringList armorParts = armorSlots(item.type, jmv);
    if(armorParts.isEmpty())
    {
        placeholders->append(item.name);
        diagnostics->append(item.name + ": unsupported armor slot type " + item.type);
        return matched;
    }
    const QString suffix = componentSuffix("armor", item.textureType1);
    const bool upperArmor = isUpperArmor(item.type);
    for(const QString& slot : armorParts)
    {
        // Shirt, gloves, pants, and boots are the lower armor texture layer;
        // their TTI selects an atlas but never selects .armorNN geometry.
        // Helm, plate, and leggings are upper armor: their exact .armorNN
        // members replace base anatomy only where the selected ANHUM actually
        // contains that member. TTI zero still means base anatomy.
        const QString expected = !upperArmor || item.textureType1 == 0
            ? slot : slot + suffix;
        if(request.assemblyParts.isEmpty())
        {
            components->append(expected);
            matched.append(expected);
            continue;
        }
        int index = -1;
        for(int candidate = 0; candidate < request.assemblyParts.size(); ++candidate)
            if(request.assemblyParts.at(candidate).compare(expected, Qt::CaseInsensitive) == 0)
            {
                index = candidate;
                break;
            }
        if(index >= 0)
        {
            const QString actual = request.assemblyParts.at(index);
            components->append(actual);
            matched.append(actual);
        }
        else
        {
            if(jmv && optionalJmvTopArmorComponent(item.type))
                continue;
            placeholders->append(expected);
            diagnostics->append("Selected model assembly has no component " + expected);
        }
    }
    return matched;
}

}

SRuntimeProviderResult resolveDatabaseAppearance(
    const QString& providerId, bool jmv,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    const SCharacterAppearanceRequest& request, QJsonObject* appearance)
{
    if(!appearance)
        return failure("appearance.output.missing", "Appearance output is required.");
    appearance->insert("providerId", providerId);
    if(request.dataSource == EUnitVisualDataSource::EmbeddedMob)
    {
        if(request.model.trimmed().isEmpty() || request.primaryTexture.trimmed().isEmpty())
        {
            appearance->insert("placeholder", true);
            return failure("appearance.import.incomplete",
                "Imported unit has incomplete embedded appearance; database fallback is forbidden.");
        }
        appearance->insert("source", "embeddedMob");
        appearance->insert("model", request.model);
        appearance->insert("primaryTexture", request.primaryTexture);
        appearance->insert("secondaryTexture", request.secondaryTexture);
        appearance->insert("bodyParts", QJsonArray::fromStringList(request.bodyParts));
        SRuntimeProviderResult result; result.success = true; result.code = "appearance.import.resolved";
        return result;
    }
    if(database.isNull() || !database->usable())
        return failure("appearance.database.unavailable", "Active profile database is unavailable.");
    const auto monster = database->monsters.constFind(request.prototype.trimmed().toLower());
    if(monster == database->monsters.constEnd())
        return failure("appearance.prototype.missing", "Unit prototype is absent from the active profile database.");
    const auto race = database->raceModels.constFind(monster->baseRace.trimmed().toLower());
    if(race == database->raceModels.constEnd())
        return failure("appearance.race.missing", "Monster base-race link is absent from RaceModels.");
    // Normal non-humanoids are not imports, but their MOB graphical fields are
    // authoritative. Applying JMV RaceModels here loses creature textures and
    // integral weapons when a prototype points at a different model variant.
    if(!databaseEquippedHumanoid(race->model))
    {
        if(request.model.trimmed().isEmpty() || request.primaryTexture.trimmed().isEmpty())
            return failure("appearance.creature.incomplete",
                "Non-humanoid MOB appearance is incomplete; database substitution is forbidden.");
        appearance->insert("source", "embeddedMobCreature");
        appearance->insert("model", request.model);
        appearance->insert("primaryTexture", request.primaryTexture);
        appearance->insert("secondaryTexture", request.secondaryTexture);
        appearance->insert("bodyParts", QJsonArray::fromStringList(
            jmv ? jmvEmbeddedCreatureParts(request.model, request.bodyParts) : request.bodyParts));
        appearance->insert("armor", QJsonArray());
        appearance->insert("weapons", QJsonArray());
        SRuntimeProviderResult result; result.success = true;
        result.code = "appearance.creature.resolved";
        return result;
    }
    const QStringList primary = expandedList(race->primaryTextures);
    const QString primaryTexture = jmv
        ? jmvMonsterTextureName(request, race->model, primary, monster->skinIndex)
        : monsterTextureName(race->model, primary, monster->skinIndex);
    if(primaryTexture.isEmpty())
        return failure("appearance.skin.invalid", jmv
            ? "JMV database model and embedded resolved texture are inconsistent."
            : "Monster skin index is outside the RaceModels primary texture list.");
    const QStringList secondary = expandedList(race->secondaryTextures);
    appearance->insert("source", "profileDatabase");
    appearance->insert("model", race->model);
    appearance->insert("primaryTexture", primaryTexture);
    appearance->insert("secondaryTexture", secondary.isEmpty() ? QString() : textureName(race->model, secondary.first()));
    appearance->insert("armor", QJsonArray::fromStringList(monster->wears));
    appearance->insert("hairIndex", monster->hairIndex);
    QJsonArray complexion;
    complexion.append(monster->complexionX); complexion.append(monster->complexionY);
    complexion.append(monster->complexionZ);
    appearance->insert("complexion", complexion);
    QStringList weapons;
    if(!monster->weapon.isEmpty()) weapons.append(monster->weapon);
    if(!monster->secondWeapon.isEmpty()) weapons.append(monster->secondWeapon);
    appearance->insert("weapons", QJsonArray::fromStringList(weapons));
    SRuntimeProviderResult result; result.success = true; result.code = "appearance.database.resolved";
    return result;
}

SRuntimeProviderResult resolveEquipmentLayout(
    const QString& providerId, bool jmv, const SJmvEquipmentGates& gates,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    const SEquipmentLayoutRequest& request, QJsonObject* equipmentLayout)
{
    if(!equipmentLayout)
        return failure("equipment.output.missing", "Equipment output is required.");
    equipmentLayout->insert("providerId", providerId);
    equipmentLayout->insert("source", request.dataSource == EUnitVisualDataSource::EmbeddedMob
        ? "embeddedMob" : "profileDatabase");
    QJsonArray components, placeholders, diagnostics;
    QJsonObject componentTextures;
    QJsonArray bodyTextureLayers;
    QJsonArray replacedParts;
    bool hidesHair = false;
    if(database.isNull())
        return failure("equipment.database.unavailable", "Item-name interpretation requires the active profile database.");

    QStringList armorReferences = request.armor;
    std::stable_sort(armorReferences.begin(), armorReferences.end(), [&](const QString& left,
                                                                         const QString& right) {
        const auto leftItem = database->items.constFind(itemPrototype(left));
        const auto rightItem = database->items.constFind(itemPrototype(right));
        const int leftPriority = leftItem == database->items.constEnd()
            ? 0 : armorLayerPriority(leftItem->type);
        const int rightPriority = rightItem == database->items.constEnd()
            ? 0 : armorLayerPriority(rightItem->type);
        return leftPriority < rightPriority;
    });
    for(const QString& reference : armorReferences)
    {
        QString syntax;
        if(!ei::validateItemReferenceSyntax(reference, &syntax))
        {
            placeholders.append(reference);
            diagnostics.append(reference + ": " + syntax);
            continue;
        }
        const auto item = database->items.constFind(itemPrototype(reference));
        if(item == database->items.constEnd() || item->category != "armor")
        {
            placeholders.append(reference);
            diagnostics.append(reference + ": armor prototype is absent from the active database");
            continue;
        }
        const QStringList matched = appendArmorComponents(item.value(), jmv, request, &components,
                                                          &placeholders, &diagnostics);
        // The stable layer ordering puts JMV top armor last. Replace any
        // already-selected lower-layer component in the same anatomical slot,
        // while retaining the lower layer when the top TTI has no geometry.
        if(jmv && armorLayerPriority(item->type) > 0)
            for(const QString& component : matched)
            {
                const QString slot = armorComponentSlot(component);
                bool keptWinner = false;
                for(int old = components.size() - 1; old >= 0; --old)
                {
                    const QString oldComponent = components.at(old).toString();
                    if(armorComponentSlot(oldComponent).compare(slot, Qt::CaseInsensitive) == 0)
                    {
                        if(!keptWinner && oldComponent.compare(component, Qt::CaseInsensitive) == 0)
                            keptWinner = true;
                        else
                        {
                            components.removeAt(old);
                            componentTextures.remove(oldComponent);
                        }
                    }
                }
            }
        if(item->type.compare("helm", Qt::CaseInsensitive) == 0 && !matched.isEmpty())
            hidesHair = true;
        const QString materialName = itemMaterial(reference);
        const auto material = database->materialRecords.constFind(materialName);
        if(materialName.isEmpty() || material == database->materialRecords.constEnd()
                || material->code.trimmed().isEmpty())
        {
            placeholders.append(reference);
            diagnostics.append(reference + ": material is absent or has no texture code");
        }
        else
        {
            // Material.Type describes gameplay/stat families, not a strict
            // rendering compatibility constraint. JMV deliberately equips
            // Metal prototypes with Hide materials (for example hmsh.dragon
            // red) and ships the corresponding .dr texture atlases.
            if(material->type.compare(item->materialType, Qt::CaseInsensitive) != 0)
                diagnostics.append(reference + ": material type " + material->type
                                   + " differs from item type " + item->materialType
                                   + "; using the explicit database material texture code");
            const QString texture = equipmentTextureName(request.model, item->type, item->typeId,
                item->textureType1, item->textureType2, material->code, false);
            if(texture.isEmpty())
            {
                placeholders.append(reference);
                diagnostics.append(reference + ": armor texture resource name cannot be derived");
            }
            else if(item->type.compare("helm", Qt::CaseInsensitive) == 0)
                for(const QString& component : matched)
                    componentTextures.insert(component, texture);
            else
                bodyTextureLayers.append(texture);
        }
        if(item->textureType1 > 0 && item->type.compare("helm", Qt::CaseInsensitive) != 0)
            for(const QString& component : matched)
            {
                const int separator = component.indexOf(".armor", 0, Qt::CaseInsensitive);
                if(separator > 0)
                {
                    const QString slot = component.left(separator);
                    if(!replacedParts.contains(slot))
                        replacedParts.append(slot);
                }
            }
    }

    struct Weapon { QString reference; ei::SItemDatabaseRecord item; EWeaponClass kind; };
    QVector<Weapon> weapons;
    QStringList weaponReferences = request.weapons;
    const int weaponLimit = jmv ? 2 : 1;
    if(weaponReferences.size() > weaponLimit)
    {
        diagnostics.append(jmv
            ? "Only the first two JMV weapon slots participate in layout resolution."
            : "Base supports one one-handed or ranged weapon; additional weapons are ignored.");
        weaponReferences = weaponReferences.mid(0, weaponLimit);
    }
    for(const QString& reference : weaponReferences)
    {
        const auto item = database->items.constFind(itemPrototype(reference));
        if(item == database->items.constEnd() || item->category != "weapons")
        {
            placeholders.append(reference);
            diagnostics.append(reference + ": weapon prototype is absent from the active database");
            continue;
        }
        weapons.append({reference, item.value(), weaponClass(item->typeId)});
    }

    QString layout = "ordinary";
    bool renderWeapons = true;
    if(!jmv && !weapons.isEmpty()
            && weapons.first().kind != EWeaponClass::OneHand && weapons.first().kind != EWeaponClass::Ranged)
    {
        placeholders.append(weapons.first().reference);
        diagnostics.append("Base does not authorize this weapon class for humanoid equipment rendering.");
        renderWeapons = false;
    }
    if(jmv)
    {
        if(weapons.size() == 1 && weapons[0].kind == EWeaponClass::TwoHand)
            layout = "singleTwoHand", renderWeapons = gates.singleTwoHand;
        else if(weapons.size() >= 2)
        {
            const EWeaponClass a = weapons[0].kind, b = weapons[1].kind;
            if((a == EWeaponClass::OneHand && b == EWeaponClass::Shield)
                    || (b == EWeaponClass::OneHand && a == EWeaponClass::Shield))
                layout = "oneHandShield", renderWeapons = gates.oneHandShield;
            else if(a == EWeaponClass::OneHand && b == EWeaponClass::OneHand)
                layout = "dualOneHand", renderWeapons = gates.dualOneHand;
            else if(a == EWeaponClass::TwoHand && b == EWeaponClass::TwoHand)
                layout = "dualTwoHand", renderWeapons = gates.dualTwoHand;
            else if((a == EWeaponClass::TwoHand && b == EWeaponClass::Shield)
                    || (b == EWeaponClass::TwoHand && a == EWeaponClass::Shield))
                layout = "twoHandShield", renderWeapons = gates.twoHandShield;
            else
            {
                diagnostics.append("JMV weapon combination has no established equipment layout.");
                renderWeapons = false;
            }
        }
        else
            renderWeapons = gates.ordinary;
        if(!renderWeapons)
        {
            diagnostics.append(layout + " is diagnostic-only until its JMV authorization/layout gate is established.");
            for(const Weapon& weapon : weapons) placeholders.append(weapon.reference);
        }
    }
    if(renderWeapons)
        for(int weaponIndex = 0; weaponIndex < weapons.size(); ++weaponIndex)
        {
            const Weapon& weapon = weapons.at(weaponIndex);
            const QString family = weaponFamily(weapon.item.typeId);
            if(family.isEmpty() || weapon.item.textureType1 < 0)
            {
                placeholders.append(weapon.reference);
                diagnostics.append(weapon.reference + ": weapon TTI component mapping is incomplete");
                continue;
            }
            QStringList matched;
            if(weapon.kind == EWeaponClass::Shield)
                appendHandComponent("lh2", family, weapon.item.textureType1, request, &components,
                                    &placeholders, &diagnostics, &matched);
            else if(weapon.kind == EWeaponClass::OneHand || weapon.kind == EWeaponClass::TwoHand)
                appendHandComponent(weaponIndex == 0 ? "rh3" : "lh3", family,
                                    weapon.item.textureType1, request, &components,
                                    &placeholders, &diagnostics, &matched);
            else if(weapon.kind == EWeaponClass::Ranged)
                appendRangedComponents(weapon.item.typeId, weapon.item.textureType1, request,
                                       &components, &placeholders, &diagnostics, &matched);
            else
                appendComponents(family, weapon.item.textureType1, request, &components,
                                 &placeholders, &diagnostics, &matched);
            const QString materialName = itemMaterial(weapon.reference);
            const auto material = database->materialRecords.constFind(materialName);
            if(materialName.isEmpty() || material == database->materialRecords.constEnd()
                    || material->code.trimmed().isEmpty())
            {
                placeholders.append(weapon.reference);
                diagnostics.append(weapon.reference + ": material is absent or has no texture code");
            }
            else
            {
                if(material->type.compare(weapon.item.materialType, Qt::CaseInsensitive) != 0)
                    diagnostics.append(weapon.reference + ": material type " + material->type
                                       + " differs from item type " + weapon.item.materialType
                                       + "; using the explicit database material texture code");
                const QString texture = equipmentTextureName(request.model, weapon.item.type,
                    weapon.item.typeId, weapon.item.textureType1, weapon.item.textureType2,
                    material->code, true);
                if(texture.isEmpty())
                {
                    placeholders.append(weapon.reference);
                    diagnostics.append(weapon.reference + ": weapon texture resource name cannot be derived");
                }
                else
                    for(const QString& component : matched) componentTextures.insert(component, texture);
            }
        }
    equipmentLayout->insert("layout", layout);
    equipmentLayout->insert("components", components);
    equipmentLayout->insert("componentTextures", componentTextures);
    equipmentLayout->insert("bodyTextureLayers", bodyTextureLayers);
    equipmentLayout->insert("replacedParts", replacedParts);
    equipmentLayout->insert("hidesHair", hidesHair);
    equipmentLayout->insert("placeholders", placeholders);
    equipmentLayout->insert("diagnostics", diagnostics);
    SRuntimeProviderResult result;
    result.success = placeholders.isEmpty();
    result.code = result.success ? "equipment.resolved" : "equipment.partial";
    result.message = diagnostics.isEmpty() ? QString() : "Equipment plan contains diagnostics/placeholders.";
    return result;
}

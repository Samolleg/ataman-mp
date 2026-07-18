#include "profile_visual_resolver_support.h"

#include <QJsonArray>
#include <QSet>

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

QString itemPrototype(const QString& reference)
{
    return reference.section('[', 0, 0).section('.', 0, 0).trimmed().toLower();
}

enum class EWeaponClass { OneHand, Ranged, Shield, TwoHand, Unknown };

EWeaponClass weaponClass(int typeId)
{
    if(typeId >= 0 && typeId <= 4) return EWeaponClass::OneHand;
    if(typeId == 5 || typeId == 6) return EWeaponClass::Ranged;
    if(typeId == 9) return EWeaponClass::Shield;
    if(typeId >= 10 && typeId <= 15) return EWeaponClass::TwoHand;
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

QString component(const QString& model, const QString& family, int tti)
{
    QString normalized = model.toLower();
    if(normalized.endsWith(".mod")) normalized.chop(4);
    return QString("%1.%2%3").arg(normalized, family).arg(tti, 2, 10, QChar('0'));
}

void appendComponent(const QString& value, const SEquipmentLayoutRequest& request,
                     QJsonArray* components, QJsonArray* placeholders, QJsonArray* diagnostics)
{
    if(value.isEmpty())
        return;
    if(!request.assemblyParts.isEmpty()
            && !request.assemblyParts.contains(value, Qt::CaseInsensitive))
    {
        placeholders->append(value);
        diagnostics->append("Selected model assembly has no component " + value);
        return;
    }
    components->append(value);
}

}

SRuntimeProviderResult resolveDatabaseAppearance(
    const QString& providerId,
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
    const QStringList primary = expandedList(race->primaryTextures);
    if(monster->skinIndex < 0 || monster->skinIndex >= primary.size())
        return failure("appearance.skin.invalid", "Monster skin index is outside the RaceModels primary texture list.");
    const QStringList secondary = expandedList(race->secondaryTextures);
    appearance->insert("source", "profileDatabase");
    appearance->insert("model", race->model);
    appearance->insert("primaryTexture", textureName(race->model, primary.at(monster->skinIndex)));
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
    if(database.isNull())
        return failure("equipment.database.unavailable", "Item-name interpretation requires the active profile database.");

    for(const QString& reference : request.armor)
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
        if(item->textureType1 >= 0)
            appendComponent(component(request.model, "armor", item->textureType1), request,
                            &components, &placeholders, &diagnostics);
        if(item->textureType2 > 0)
            appendComponent(component(request.model, "armor", item->textureType2), request,
                            &components, &placeholders, &diagnostics);
    }

    struct Weapon { QString reference; ei::SItemDatabaseRecord item; EWeaponClass kind; };
    QVector<Weapon> weapons;
    for(const QString& reference : request.weapons)
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
    if(!jmv && weapons.size() > 1)
    {
        diagnostics.append("Base supports one one-handed or ranged weapon; additional weapons are ignored.");
        weapons.resize(1);
    }
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
            if(weapons.size() > 2)
                diagnostics.append("Only the first two JMV weapon slots participate in layout resolution.");
            weapons.resize(2);
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
        for(const Weapon& weapon : weapons)
        {
            const QString family = weaponFamily(weapon.item.typeId);
            if(family.isEmpty() || weapon.item.textureType1 < 0)
            {
                placeholders.append(weapon.reference);
                diagnostics.append(weapon.reference + ": weapon TTI component mapping is incomplete");
                continue;
            }
            appendComponent(component(request.model, family, weapon.item.textureType1), request,
                            &components, &placeholders, &diagnostics);
            if(weapon.item.textureType2 > 0)
                appendComponent(component(request.model, family, weapon.item.textureType2), request,
                                &components, &placeholders, &diagnostics);
        }
    equipmentLayout->insert("layout", layout);
    equipmentLayout->insert("components", components);
    equipmentLayout->insert("placeholders", placeholders);
    equipmentLayout->insert("diagnostics", diagnostics);
    SRuntimeProviderResult result;
    result.success = placeholders.isEmpty();
    result.code = result.success ? "equipment.resolved" : "equipment.partial";
    result.message = diagnostics.isEmpty() ? QString() : "Equipment plan contains diagnostics/placeholders.";
    return result;
}

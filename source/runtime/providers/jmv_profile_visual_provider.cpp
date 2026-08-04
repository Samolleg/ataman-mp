#include "jmv_profile_visual_provider.h"
#include "profile_visual_resolver_support.h"
#include "../unit_animation.h"
#include <QJsonArray>

namespace {

class JmvAppearanceResolver final : public ICharacterAppearanceResolver
{
public:
    explicit JmvAppearanceResolver(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
        : m_database(database) {}
    QString providerId() const override { return "jmv.v1"; }
    SRuntimeProviderResult resolve(const SCharacterAppearanceRequest& request,
                                   QJsonObject* appearance) const override
    { return resolveDatabaseAppearance(providerId(), true, m_database, request, appearance); }
private:
    QSharedPointer<const ei::SProfileDatabaseSnapshot> m_database;
};

class JmvEquipmentResolver final : public IEquipmentLayoutResolver
{
public:
    explicit JmvEquipmentResolver(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
        : m_database(database) {}
    QString providerId() const override { return "jmv.v1"; }
    SRuntimeProviderResult resolve(const SEquipmentLayoutRequest& request,
                                   QJsonObject* equipment) const override
    {
        SJmvEquipmentGates gates;
        const bool titanGrip = m_database
            && m_database->jmvUnitPerkTableStatus
                == ei::EJmvUnitPerkTableStatus::Valid
            && m_database->jmvStrongManByPrototype.value(
                request.prototype.trimmed().toLower()) != 0;
        gates.dualTwoHand = titanGrip;
        gates.twoHandShield = titanGrip;
        return resolveEquipmentLayout(providerId(), true, gates, m_database, request, equipment);
    }
private:
    QSharedPointer<const ei::SProfileDatabaseSnapshot> m_database;
};

class JmvAnimationResolver final : public IUnitAnimationResolver
{
public:
    explicit JmvAnimationResolver(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
        : m_database(database) {}
    QString providerId() const override { return "jmv.v1"; }
    SRuntimeProviderResult resolve(const SUnitAnimationRequest& request,
                                   QJsonObject* animation) const override;
private:
    QSharedPointer<const ei::SProfileDatabaseSnapshot> m_database;
};

SRuntimeProviderResult JmvAnimationResolver::resolve(const SUnitAnimationRequest& request,
                                                      QJsonObject* animation) const
{
    SRuntimeProviderResult result;
    if(!animation || m_database.isNull())
    { result.code = "animation.database.unavailable"; result.message = "Active profile ADB data is unavailable."; return result; }
    const QByteArray adb = m_database->actionTables.value(request.model.toLower() + ".adb");
    QStringList diagnostics;
    const QVector<SAdbAction> actions = parseAdbActions(adb, &diagnostics);
    QVector<int> weaponTypes;
    int weaponType = -1;
    for(const QString& weapon : request.weapons)
    {
        const QString key = weapon.section('[', 0, 0).section('.', 0, 0).trimmed().toLower();
        if(m_database->items.contains(key))
        {
            const int typeId = m_database->items.value(key).typeId;
            weaponTypes.append(typeId);
            if(weaponType < 0)
                weaponType = typeId;
        }
    }
    const bool titanGrip = m_database->jmvUnitPerkTableStatus
            == ei::EJmvUnitPerkTableStatus::Valid
        && m_database->jmvStrongManByPrototype.value(
            request.prototype.trimmed().toLower()) != 0;
    if(titanGrip && weaponTypes.size() >= 2)
    {
        const int first = weaponTypes.at(0);
        const int second = weaponTypes.at(1);
        const bool firstTwoHand = first >= 10 && first <= 15;
        const bool secondTwoHand = second >= 10 && second <= 15;
        const bool twoHandShield = (firstTwoHand && second == 9)
            || (secondTwoHand && first == 9);
        if((firstTwoHand && secondTwoHand) || twoHandShield)
            // JMV has no dedicated Titan Grip standing action. Its ordinary
            // cidle01 is the authored one-hand/shield and dual-wield stance.
            weaponType = 0;
    }
    SAdbAction selected;
    if(!selectJmvDefaultIdleAction(actions, request.model, weaponType, &selected, &result.message))
    { result.code = "animation.idle.missing"; result.evidence.insert("diagnostics", QJsonArray::fromStringList(diagnostics)); return result; }
    animation->insert("providerId", providerId()); animation->insert("actionName", selected.name);
    animation->insert("actionNumber", int(selected.number)); animation->insert("length", int(selected.length));
    animation->insert("weaponMask", int(selected.weaponMask));
    result.success = true; result.code = "animation.idle.resolved"; return result;
}

}

QSharedPointer<ICharacterAppearanceResolver> JmvProfileVisualProvider::appearanceResolver(
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const
{
    return QSharedPointer<ICharacterAppearanceResolver>(new JmvAppearanceResolver(database));
}

QSharedPointer<IEquipmentLayoutResolver> JmvProfileVisualProvider::equipmentResolver(
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const
{
    return QSharedPointer<IEquipmentLayoutResolver>(new JmvEquipmentResolver(database));
}

QSharedPointer<IUnitAnimationResolver> JmvProfileVisualProvider::animationResolver(
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const
{
    return QSharedPointer<IUnitAnimationResolver>(new JmvAnimationResolver(database));
}

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
        // These four layouts are established in the current JMV corpus.
        // dualTwoHand and twoHandShield remain explicitly disabled until the
        // perk/addon authorization source is proven.
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
    int weaponType = -1;
    for(const QString& weapon : request.weapons)
    {
        const QString key = weapon.section('[', 0, 0).section('.', 0, 0).trimmed().toLower();
        if(m_database->items.contains(key)) { weaponType = m_database->items.value(key).typeId; break; }
    }
    SAdbAction selected;
    if(!selectDefaultIdleAction(actions, weaponType, true, &selected, &result.message))
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

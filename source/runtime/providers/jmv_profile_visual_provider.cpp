#include "jmv_profile_visual_provider.h"
#include "profile_visual_resolver_support.h"

namespace {

class JmvAppearanceResolver final : public ICharacterAppearanceResolver
{
public:
    explicit JmvAppearanceResolver(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
        : m_database(database) {}
    QString providerId() const override { return "jmv.v1"; }
    SRuntimeProviderResult resolve(const SCharacterAppearanceRequest& request,
                                   QJsonObject* appearance) const override
    { return resolveDatabaseAppearance(providerId(), m_database, request, appearance); }
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

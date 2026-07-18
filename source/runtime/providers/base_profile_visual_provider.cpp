#include "base_profile_visual_provider.h"
#include "profile_visual_resolver_support.h"

namespace {

class BaseAppearanceResolver final : public ICharacterAppearanceResolver
{
public:
    explicit BaseAppearanceResolver(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
        : m_database(database) {}
    QString providerId() const override { return "base.standard.v1"; }
    SRuntimeProviderResult resolve(const SCharacterAppearanceRequest& request,
                                   QJsonObject* appearance) const override
    { return resolveDatabaseAppearance(providerId(), m_database, request, appearance); }
private:
    QSharedPointer<const ei::SProfileDatabaseSnapshot> m_database;
};

class BaseEquipmentResolver final : public IEquipmentLayoutResolver
{
public:
    explicit BaseEquipmentResolver(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database)
        : m_database(database) {}
    QString providerId() const override { return "base.standard.v1"; }
    SRuntimeProviderResult resolve(const SEquipmentLayoutRequest& request,
                                   QJsonObject* equipment) const override
    { return resolveEquipmentLayout(providerId(), false, {}, m_database, request, equipment); }
private:
    QSharedPointer<const ei::SProfileDatabaseSnapshot> m_database;
};

}

QSharedPointer<ICharacterAppearanceResolver> BaseProfileVisualProvider::appearanceResolver(
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const
{
    return QSharedPointer<ICharacterAppearanceResolver>(new BaseAppearanceResolver(database));
}

QSharedPointer<IEquipmentLayoutResolver> BaseProfileVisualProvider::equipmentResolver(
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const
{
    return QSharedPointer<IEquipmentLayoutResolver>(new BaseEquipmentResolver(database));
}

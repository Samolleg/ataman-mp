#include "profile_visual_provider.h"
#include "providers/base_profile_visual_provider.h"
#include "providers/jmv_profile_visual_provider.h"

ProfileVisualProviderService::ProfileVisualProviderService()
{
    registerProvider(QSharedPointer<IProfileVisualProvider>(new BaseProfileVisualProvider));
    registerProvider(QSharedPointer<IProfileVisualProvider>(new JmvProfileVisualProvider));
}

bool ProfileVisualProviderService::registerProvider(
    const QSharedPointer<IProfileVisualProvider>& provider, QString* diagnostic)
{
    if(provider.isNull() || provider->providerId().trimmed().isEmpty())
    {
        if(diagnostic) *diagnostic = "Visual provider is null or has no ID.";
        return false;
    }
    const QString key = provider->providerId().trimmed().toLower();
    if(m_providers.contains(key))
    {
        if(diagnostic) *diagnostic = "Visual provider is already registered: " + provider->providerId();
        return false;
    }
    m_providers.insert(key, provider);
    return true;
}

QSharedPointer<ICharacterAppearanceResolver> ProfileVisualProviderService::appearanceResolver(
    const QString& providerId, const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    QString* diagnostic) const
{
    const auto provider = m_providers.value(providerId.trimmed().toLower());
    if(provider.isNull())
    {
        if(diagnostic) *diagnostic = "Unknown appearance provider; no fallback was selected.";
        return {};
    }
    return provider->appearanceResolver(database);
}

QSharedPointer<IEquipmentLayoutResolver> ProfileVisualProviderService::equipmentResolver(
    const QString& providerId, const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    QString* diagnostic) const
{
    const auto provider = m_providers.value(providerId.trimmed().toLower());
    if(provider.isNull())
    {
        if(diagnostic) *diagnostic = "Unknown equipment provider; no fallback was selected.";
        return {};
    }
    return provider->equipmentResolver(database);
}

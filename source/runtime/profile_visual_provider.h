#ifndef PROFILE_VISUAL_PROVIDER_H
#define PROFILE_VISUAL_PROVIDER_H

#include "runtime_providers.h"
#include "../resources/profile_database.h"

#include <QSharedPointer>

class IProfileVisualProvider
{
public:
    virtual ~IProfileVisualProvider() = default;
    virtual QString providerId() const = 0;
    virtual QSharedPointer<ICharacterAppearanceResolver> appearanceResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const = 0;
    virtual QSharedPointer<IEquipmentLayoutResolver> equipmentResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const = 0;
};

class ProfileVisualProviderService
{
public:
    ProfileVisualProviderService();
    bool registerProvider(const QSharedPointer<IProfileVisualProvider>& provider,
                          QString* diagnostic = nullptr);
    QSharedPointer<ICharacterAppearanceResolver> appearanceResolver(
        const QString& providerId,
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
        QString* diagnostic = nullptr) const;
    QSharedPointer<IEquipmentLayoutResolver> equipmentResolver(
        const QString& providerId,
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
        QString* diagnostic = nullptr) const;

private:
    QMap<QString, QSharedPointer<IProfileVisualProvider>> m_providers;
};

#endif

#ifndef BASE_PROFILE_VISUAL_PROVIDER_H
#define BASE_PROFILE_VISUAL_PROVIDER_H

#include "../profile_visual_provider.h"

class BaseProfileVisualProvider final : public IProfileVisualProvider
{
public:
    QString providerId() const override { return "base.standard.v1"; }
    QSharedPointer<ICharacterAppearanceResolver> appearanceResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const override;
    QSharedPointer<IEquipmentLayoutResolver> equipmentResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const override;
    QSharedPointer<IUnitAnimationResolver> animationResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const override;
};

#endif

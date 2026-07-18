#ifndef JMV_PROFILE_VISUAL_PROVIDER_H
#define JMV_PROFILE_VISUAL_PROVIDER_H

#include "../profile_visual_provider.h"

class JmvProfileVisualProvider final : public IProfileVisualProvider
{
public:
    QString providerId() const override { return "jmv.v1"; }
    QSharedPointer<ICharacterAppearanceResolver> appearanceResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const override;
    QSharedPointer<IEquipmentLayoutResolver> equipmentResolver(
        const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database) const override;
};

#endif

#ifndef PROFILE_VISUAL_RESOLVER_SUPPORT_H
#define PROFILE_VISUAL_RESOLVER_SUPPORT_H

#include "../runtime_providers.h"
#include "../../resources/profile_database.h"

struct SJmvEquipmentGates
{
    bool ordinary{true};
    bool oneHandShield{true};
    bool dualOneHand{true};
    bool singleTwoHand{true};
    bool dualTwoHand{false};
    bool twoHandShield{false};
};

SRuntimeProviderResult resolveDatabaseAppearance(
    const QString& providerId, bool jmv,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    const SCharacterAppearanceRequest& request, QJsonObject* appearance);
SRuntimeProviderResult resolveEquipmentLayout(
    const QString& providerId, bool jmv,
    const SJmvEquipmentGates& gates,
    const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
    const SEquipmentLayoutRequest& request, QJsonObject* equipmentLayout);

#endif

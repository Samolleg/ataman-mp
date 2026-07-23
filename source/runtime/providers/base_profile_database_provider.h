#ifndef BASE_PROFILE_DATABASE_PROVIDER_H
#define BASE_PROFILE_DATABASE_PROVIDER_H

#include "../../resources/profile_database.h"

namespace ei {

class BaseProfileDatabaseProvider final : public IProfileDatabaseProvider
{
public:
    QString providerId() const override { return "base.standard.v1"; }
    QSharedPointer<SProfileDatabaseSnapshot> load(
        const SProfileDatabaseLoadRequest& request) const override;
};

}

#endif

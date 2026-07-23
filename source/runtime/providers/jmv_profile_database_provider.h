#ifndef JMV_PROFILE_DATABASE_PROVIDER_H
#define JMV_PROFILE_DATABASE_PROVIDER_H

#include "../../resources/profile_database.h"

namespace ei {

class JmvProfileDatabaseProvider final : public IProfileDatabaseProvider
{
public:
    QString providerId() const override { return "jmv.v1"; }
    QSharedPointer<SProfileDatabaseSnapshot> load(
        const SProfileDatabaseLoadRequest& request) const override;
};

}

#endif

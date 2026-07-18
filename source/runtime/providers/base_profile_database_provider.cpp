#include "base_profile_database_provider.h"
#include "profile_database_provider_support.h"

namespace ei {

QSharedPointer<SProfileDatabaseSnapshot> BaseProfileDatabaseProvider::load(
    const SProfileDatabaseLoadRequest& request) const
{
    QSharedPointer<SProfileDatabaseSnapshot> result(new SProfileDatabaseSnapshot);
    result->providerId = providerId();
    result->profileId = request.profileId;
    result->runtimeManifestId = request.runtimeManifestId;
    const QString database = activeDatabaseArchive(request.resourceDirectories, false);
    const QMap<QString, QByteArray> members = readDatabaseArchive(database, &result->diagnostics);
    if(!members.isEmpty())
    {
        applyRelationalMembers(members, database, result.data());
        applyActionMembers(members, database, false, result.data());
    }
    return result;
}

}

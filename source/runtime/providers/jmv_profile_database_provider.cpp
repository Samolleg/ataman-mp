#include "jmv_profile_database_provider.h"
#include "profile_database_provider_support.h"

namespace ei {

QSharedPointer<SProfileDatabaseSnapshot> JmvProfileDatabaseProvider::load(
    const SProfileDatabaseLoadRequest& request) const
{
    QSharedPointer<SProfileDatabaseSnapshot> result(new SProfileDatabaseSnapshot);
    result->providerId = providerId();
    result->profileId = request.profileId;
    result->runtimeManifestId = request.runtimeManifestId;
    // JMV relational tables are authoritative only in the active mod resource
    // root. Falling through to Base would mix profiles and hide broken installs.
    const QString database = activeDatabaseArchive(request.resourceDirectories, true);
    const QMap<QString, QByteArray> members = readDatabaseArchive(database, &result->diagnostics);
    if(!members.isEmpty())
    {
        applyRelationalMembers(members, database, result.data());
        applyActionMembers(members, database, false, result.data());
    }
    const QString overlay = siblingArchive(database, "databaseJmv.res");
    if(overlay.isEmpty())
        result->diagnostics.append({EDatabaseDiagnosticSeverity::Warning, "database.jmv.overlay.missing",
            "databaseJmv.res is unavailable; JMV supplemental ADB actions are disabled.", database});
    else
        applyActionMembers(readDatabaseArchive(overlay, &result->diagnostics), overlay, true, result.data());
    return result;
}

}

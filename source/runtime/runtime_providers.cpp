#include "runtime_providers.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

namespace {
bool isWithin(const QString& root, const QString& candidate)
{
    const QString normalizedRoot = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    const QString normalizedCandidate = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(candidate).absoluteFilePath()));
    const QString prefix = normalizedRoot.endsWith('/')
        ? normalizedRoot : normalizedRoot + '/';
    return normalizedCandidate.compare(normalizedRoot, Qt::CaseInsensitive) == 0 ||
           normalizedCandidate.startsWith(prefix, Qt::CaseInsensitive);
}
}

QString profileProviderRoleName(EProfileProviderRole role)
{
    switch(role)
    {
    case EProfileProviderRole::Database: return "database";
    case EProfileProviderRole::CharacterAppearance: return "characterAppearance";
    case EProfileProviderRole::EquipmentLayout: return "equipmentLayout";
    case EProfileProviderRole::UnitAnimation: return "unitAnimation";
    case EProfileProviderRole::SemanticValidation: return "semanticValidation";
    }
    return QString();
}

bool ProfileProviderRegistry::registerProvider(const SProfileProviderDescriptor& descriptor,
                                               QString* diagnostic)
{
    const QString key = descriptor.id.trimmed().toLower();
    if(key.isEmpty() || descriptor.interfaceVersion < 1 || descriptor.roles.isEmpty())
    {
        if(diagnostic)
            *diagnostic = "Provider registration requires an ID, a positive interface version, and at least one role.";
        return false;
    }
    if(m_providers.contains(key))
    {
        if(diagnostic)
            *diagnostic = "Provider ID is already registered: " + descriptor.id;
        return false;
    }
    m_providers.insert(key, descriptor);
    return true;
}

SProfileProviderSelection ProfileProviderRegistry::select(EProfileProviderRole role,
                                                           const QString& providerId,
                                                           int requestedInterfaceVersion) const
{
    SProfileProviderSelection result;
    result.role = role;
    result.providerId = providerId.trimmed();
    const auto it = m_providers.constFind(result.providerId.toLower());
    if(it == m_providers.constEnd())
    {
        result.diagnostic = QString("Unknown %1 provider '%2'; resolution is disabled.")
            .arg(profileProviderRoleName(role), result.providerId);
        return result;
    }
    if(it->interfaceVersion != requestedInterfaceVersion)
    {
        result.diagnostic = QString("Provider '%1' implements interface v%2, but the profile requests v%3.")
            .arg(it->id).arg(it->interfaceVersion).arg(requestedInterfaceVersion);
        return result;
    }
    if(!it->roles.contains(role))
    {
        result.diagnostic = QString("Provider '%1' does not implement the %2 role.")
            .arg(it->id, profileProviderRoleName(role));
        return result;
    }
    result.valid = true;
    result.providerId = it->id;
    return result;
}

QVector<SProfileProviderDescriptor> ProfileProviderRegistry::providers() const
{
    return m_providers.values().toVector();
}

ProfileProviderRegistry ProfileProviderRegistry::builtIns()
{
    ProfileProviderRegistry registry;
    const QSet<EProfileProviderRole> roles = {
        EProfileProviderRole::Database, EProfileProviderRole::CharacterAppearance,
        EProfileProviderRole::EquipmentLayout, EProfileProviderRole::UnitAnimation,
        EProfileProviderRole::SemanticValidation
    };
    registry.registerProvider({"base.standard.v1", 1, roles,
        "Base Evil Islands resource semantics."});
    registry.registerProvider({"jmv.v1", 1, roles,
        "Jamais Vu extensions; reference implementation for mod providers."});
    return registry;
}

namespace {
void requireStagedPath(const QString& label, const QString& path,
                       const QString& runRoot, QStringList* errors)
{
    if(path.isEmpty() || !QFileInfo(path).isAbsolute())
        errors->append(label + " must be an absolute path");
    else if(!isWithin(runRoot, path))
        errors->append(label + " must remain inside the disposable test-run root");
}
}

QJsonObject mapTestLaunchRequestToJson(const SMapTestLaunchRequest& request)
{
    QJsonObject result;
    result.insert("protocolVersion", request.protocolVersion);
    result.insert("runId", request.runId);
    result.insert("profileId", request.profileId);
    result.insert("runtimeManifestId", request.runtimeManifestId);
    result.insert("runtimeComponentHashes", request.runtimeComponentHashes);
    result.insert("stagedMprPath", request.stagedMprPath);
    result.insert("internalMapName", request.internalMapName);
    result.insert("orderedStagedMobPaths", QJsonArray::fromStringList(request.orderedStagedMobPaths));
    result.insert("activeMobPath", request.activeMobPath);
    result.insert("sourceSha256", request.sourceSha256);
    result.insert("stagedSha256", request.stagedSha256);
    result.insert("sessionSettings", request.sessionSettings);
    result.insert("logPath", request.logPath);
    result.insert("resultPath", request.resultPath);
    return result;
}

bool validateMapTestLaunchRequest(const SMapTestLaunchRequest& request,
                                  const QString& runRoot, QStringList* errors)
{
    QStringList localErrors;
    if(runRoot.isEmpty() || !QFileInfo(runRoot).isAbsolute())
        localErrors.append("Test-run root must be absolute");
    if(request.protocolVersion != 1)
        localErrors.append("Unsupported test-run protocol version");
    static const QRegularExpression safeRunId("^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$");
    if(!safeRunId.match(request.runId).hasMatch())
        localErrors.append("Run ID contains unsafe characters or has an invalid length");
    if(request.profileId.compare("jamais-vu", Qt::CaseInsensitive) != 0)
        localErrors.append("Direct test launch is currently scoped to the JMV profile");
    if(request.runtimeManifestId.isEmpty() || request.runtimeComponentHashes.isEmpty())
        localErrors.append("A fingerprinted runtime-stack manifest is required");
    if(request.internalMapName.isEmpty())
        localErrors.append("Internal map name is required");
    requireStagedPath("Staged MPR", request.stagedMprPath, runRoot, &localErrors);
    if(request.orderedStagedMobPaths.isEmpty())
        localErrors.append("At least one staged MOB is required");
    for(int index = 0; index < request.orderedStagedMobPaths.size(); ++index)
        requireStagedPath(QString("Staged MOB %1").arg(index),
                          request.orderedStagedMobPaths.at(index), runRoot, &localErrors);
    requireStagedPath("Active MOB", request.activeMobPath, runRoot, &localErrors);
    if(!request.orderedStagedMobPaths.contains(request.activeMobPath, Qt::CaseInsensitive))
        localErrors.append("Active MOB must be present in the ordered staged MOB list");
    requireStagedPath("Log", request.logPath, runRoot, &localErrors);
    requireStagedPath("Result", request.resultPath, runRoot, &localErrors);
    if(request.sourceSha256.isEmpty() || request.stagedSha256.isEmpty())
        localErrors.append("Source and staged SHA-256 inventories are required");
    if(errors)
        *errors = localErrors;
    return localErrors.isEmpty();
}

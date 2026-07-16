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

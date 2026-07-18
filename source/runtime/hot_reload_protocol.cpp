#include "hot_reload_protocol.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
bool isOwnedPath(const QString& path, const QString& root)
{
    const QString cleanPath = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QString cleanRoot = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    if(!cleanRoot.endsWith('/')) cleanRoot += '/';
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
}
}

QJsonObject mapHotReloadRequestToJson(const SMapHotReloadRequest& request)
{
    QJsonObject object;
    object.insert("protocolVersion", request.protocolVersion);
    object.insert("sequence", QString::number(request.sequence));
    object.insert("runId", request.runId);
    object.insert("profileId", request.profileId);
    object.insert("zoneId", request.zoneId);
    object.insert("entranceId", request.entranceId);
    object.insert("mprPath", request.mprPath);
    object.insert("mobPath", request.mobPath);
    object.insert("mprSha256", request.mprSha256);
    object.insert("mobSha256", request.mobSha256);
    object.insert("preservePosition", request.preservePosition);
    return object;
}

bool mapHotReloadRequestFromJson(const QJsonObject& object,
                                 SMapHotReloadRequest* request,
                                 QStringList* errors)
{
    if(!request) return false;
    SMapHotReloadRequest parsed;
    parsed.protocolVersion = object.value("protocolVersion").toInt();
    bool sequenceValid = false;
    parsed.sequence = object.value("sequence").toString().toULongLong(&sequenceValid);
    parsed.runId = object.value("runId").toString();
    parsed.profileId = object.value("profileId").toString();
    parsed.zoneId = object.value("zoneId").toString();
    parsed.entranceId = object.value("entranceId").toInt();
    parsed.mprPath = object.value("mprPath").toString();
    parsed.mobPath = object.value("mobPath").toString();
    parsed.mprSha256 = object.value("mprSha256").toString();
    parsed.mobSha256 = object.value("mobSha256").toString();
    parsed.preservePosition = object.value("preservePosition").toBool();
    if(!sequenceValid && errors) errors->append("Hot-reload sequence is invalid.");
    *request = parsed;
    return sequenceValid;
}

bool validateMapHotReloadRequest(const SMapHotReloadRequest& request,
                                 const QString& ownedRunRoot,
                                 QStringList* errors)
{
    QStringList local;
    if(request.protocolVersion != 1) local.append("Unsupported hot-reload protocol version.");
    if(request.sequence == 0) local.append("Hot-reload sequence must be positive.");
    if(request.runId.isEmpty() || request.profileId.isEmpty() || request.zoneId.isEmpty())
        local.append("Run, profile, and zone identities are required.");
    if(request.entranceId <= 0) local.append("A valid map-table entrance is required.");
    if(!isOwnedPath(request.mprPath, ownedRunRoot)
       || !isOwnedPath(request.mobPath, ownedRunRoot))
        local.append("Hot-reload payloads must remain inside the owned test-run directory.");
    if(!QFileInfo::exists(request.mprPath) || !QFileInfo::exists(request.mobPath))
        local.append("Hot-reload payload files are missing.");
    const QRegularExpression hash("^[0-9a-f]{64}$", QRegularExpression::CaseInsensitiveOption);
    if(!hash.match(request.mprSha256).hasMatch()
       || !hash.match(request.mobSha256).hasMatch())
        local.append("Hot-reload payload hashes are invalid.");
    if(errors) errors->append(local);
    return local.isEmpty();
}

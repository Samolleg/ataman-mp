#include "hot_reload_protocol.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

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
    object.insert("bootstrapMobPath", request.bootstrapMobPath);
    object.insert("bootstrapMobSha256", request.bootstrapMobSha256);
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
    parsed.bootstrapMobPath = object.value("bootstrapMobPath").toString();
    parsed.bootstrapMobSha256 = object.value("bootstrapMobSha256").toString();
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
    if(!request.bootstrapMobPath.isEmpty()
       && !isOwnedPath(request.bootstrapMobPath, ownedRunRoot))
        local.append("The hot-reload bootstrap MOB must remain inside the owned test-run directory.");
    if(!QFileInfo::exists(request.mprPath) || !QFileInfo::exists(request.mobPath))
        local.append("Hot-reload payload files are missing.");
    if(!request.bootstrapMobPath.isEmpty() && !QFileInfo::exists(request.bootstrapMobPath))
        local.append("The hot-reload bootstrap MOB is missing.");
    const QRegularExpression hash("^[0-9a-f]{64}$", QRegularExpression::CaseInsensitiveOption);
    if(!hash.match(request.mprSha256).hasMatch()
       || !hash.match(request.mobSha256).hasMatch())
        local.append("Hot-reload payload hashes are invalid.");
    if(!request.bootstrapMobPath.isEmpty()
       && !hash.match(request.bootstrapMobSha256).hasMatch())
        local.append("The hot-reload bootstrap MOB hash is invalid.");
    if(errors) errors->append(local);
    return local.isEmpty();
}

QByteArray mapHotReloadRequestToControlBytes(const SMapHotReloadRequest& request)
{
    QByteArray bytes;
    QTextStream stream(&bytes, QIODevice::WriteOnly);
    stream.setCodec("UTF-8");
    const auto line = [&stream](const QString& key, QString value)
    {
        value.replace('\r', ' ');
        value.replace('\n', ' ');
        stream << key << '=' << value << '\n';
    };
    line("protocol", QString::number(request.protocolVersion));
    line("sequence", QString::number(request.sequence));
    line("runId", request.runId);
    line("profile", request.profileId);
    line("zone", request.zoneId);
    line("entrance", QString::number(request.entranceId));
    line("mpr", QDir::toNativeSeparators(request.mprPath));
    line("mob", QDir::toNativeSeparators(request.mobPath));
    line("mprSha256", request.mprSha256.toLower());
    line("mobSha256", request.mobSha256.toLower());
    line("bootstrapMob", QDir::toNativeSeparators(request.bootstrapMobPath));
    line("bootstrapMobSha256", request.bootstrapMobSha256.toLower());
    line("preservePosition", request.preservePosition ? "1" : "0");
    stream.flush();
    return bytes;
}

bool mapHotReloadRequestFromControlBytes(const QByteArray& bytes,
                                         SMapHotReloadRequest* request,
                                         QStringList* errors)
{
    if(!request) return false;
    QHash<QString, QString> values;
    const QString text = QString::fromUtf8(bytes);
    for(const QString& rawLine : text.split('\n'))
    {
        const QString line = rawLine.trimmed();
        if(line.isEmpty() || line.startsWith('#')) continue;
        const int separator = line.indexOf('=');
        if(separator <= 0)
        {
            if(errors) errors->append("Hot-reload control line is not key=value.");
            return false;
        }
        values.insert(line.left(separator).trimmed(), line.mid(separator + 1).trimmed());
    }
    SMapHotReloadRequest parsed;
    bool protocolOk = false;
    bool sequenceOk = false;
    bool entranceOk = false;
    parsed.protocolVersion = values.value("protocol").toInt(&protocolOk);
    parsed.sequence = values.value("sequence").toULongLong(&sequenceOk);
    parsed.runId = values.value("runId");
    parsed.profileId = values.value("profile");
    parsed.zoneId = values.value("zone");
    parsed.entranceId = values.value("entrance").toInt(&entranceOk);
    parsed.mprPath = QDir::fromNativeSeparators(values.value("mpr"));
    parsed.mobPath = QDir::fromNativeSeparators(values.value("mob"));
    parsed.mprSha256 = values.value("mprSha256");
    parsed.mobSha256 = values.value("mobSha256");
    parsed.bootstrapMobPath = QDir::fromNativeSeparators(values.value("bootstrapMob"));
    parsed.bootstrapMobSha256 = values.value("bootstrapMobSha256");
    parsed.preservePosition = values.value("preservePosition") == "1";
    if(!protocolOk || !sequenceOk || !entranceOk)
    {
        if(errors) errors->append("Hot-reload control numeric fields are invalid.");
        return false;
    }
    *request = parsed;
    return true;
}
